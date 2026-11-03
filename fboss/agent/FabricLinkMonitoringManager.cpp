// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/FabricLinkMonitoringManager.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"

#include <folly/MacAddress.h>
#include <folly/io/Cursor.h>
#include <folly/lang/Bits.h>
#include <folly/logging/xlog.h>
#include <gflags/gflags.h>
#include <cstring>
#include "fboss/agent/RxPacket.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/gen-cpp2/agent_stats_types.h"

DEFINE_int32(
    fabric_link_monitoring_interval_ms,
    1000,
    "Interval in milliseconds for sending fabric link monitoring packets");

DEFINE_int32(
    fabric_link_monitoring_max_outstanding_packets,
    0,
    "Maximum number of outstanding packets per port group");

DEFINE_int32(
    fabric_link_monitoring_max_pending_seq_numbers,
    3,
    "Maximum number of pending sequence numbers before considering a packet dropped");

using folly::MacAddress;
using folly::io::RWPrivateCursor;

namespace facebook::fboss {

// Cell size of 512B - header size os 32B
constexpr int kFabricLinkMonitoringPacketSize{480};
constexpr int kMaxOutstandingPacketsVoqSwitch{160};
constexpr int kMaxOutstandingPacketsFabricSwitch{40};

namespace {
// Static flag for test mode
std::atomic<bool> testModeEnabled{false};
} // namespace

// Test-only function to enable test mode
void FabricLinkMonitoringManager::setTestMode(bool enabled) {
  testModeEnabled.store(enabled);
}

bool FabricLinkMonitoringManager::isTestMode() {
  return testModeEnabled.load();
}

// Initialize the FabricLinkMonitoringManager with a reference to SwSwitch and
// configure the monitoring interval. Automatically determines the maximum
// outstanding packets based on switch type (VOQ switches: 160 packets per port
// group, Fabric switches: 40 packets per port group).
FabricLinkMonitoringManager::FabricLinkMonitoringManager(SwSwitch* sw)
    : folly::AsyncTimeout(sw->getBackgroundEvb()),
      sw_(sw),
      intervalMsecs_(FLAGS_fabric_link_monitoring_interval_ms) {
  if (!FLAGS_fabric_link_monitoring_max_outstanding_packets) {
    if (sw_->getSwitchInfoTable().haveVoqSwitches()) {
      FLAGS_fabric_link_monitoring_max_outstanding_packets =
          kMaxOutstandingPacketsVoqSwitch;
    } else {
      FLAGS_fabric_link_monitoring_max_outstanding_packets =
          kMaxOutstandingPacketsFabricSwitch;
    }
  }
}

FabricLinkMonitoringManager::~FabricLinkMonitoringManager() {}

// Start fabric link monitoring by clearing all existing state, discovering
// fabric ports from switch state, and grouping them by virtual device ID.
// Filters ports based on switch topology - VOQ switches monitor all fabric
// ports in both single and dual stage topologies. In dual stage topology,
// L1 fabric switches will monitor fabric ports connected to L2 switches.
// L2 switch will not send any fabric link monitoring packets. Periodic
// packet transmission is scheduled on the background event base.
void FabricLinkMonitoringManager::start() {
  portGroupToPortsMap_.clear();
  portToGroupMap_.clear();
  auto portStatsLocked = portStats_.wlock();
  portStatsLocked->clear();
  auto portPendingSeqNumsLocked = portPendingSequenceNumbers_.wlock();
  portPendingSeqNumsLocked->clear();
  portGroupStats_.wlock()->clear();

  // Expect fabric ports to not change once the system is configured
  std::shared_ptr<SwitchState> state = sw_->getState();

  // Get the set of L1 fabric ports connected to L2 switches
  auto l2ConnectedFabricPorts = getL2ConnectedL1FabricPorts(state);

  bool voqSwitch = sw_->getSwitchInfoTable()
                       .getSwitchIdsOfType(cfg::SwitchType::VOQ)
                       .size() > 0;
  for (const auto& portMap : std::as_const(*state->getPorts())) {
    for (const auto& [_, port] : std::as_const(*portMap.second)) {
      if (port->getPortType() == cfg::PortType::FABRIC_PORT) {
        // Fabric link monitoring packets should be sent from CPU
        // only in the RDSW->FDSW and FDSW->SDSW direction, not in
        // the reverse direction.
        const PortID portId = port->getID();
        bool l2ConnectedL1Port =
            l2ConnectedFabricPorts.find(portId) != l2ConnectedFabricPorts.end();
        if (voqSwitch || l2ConnectedL1Port) {
          int groupId = getPortGroup(portId);
          portGroupToPortsMap_[groupId].push_back(portId);
          portToGroupMap_[portId] = groupId;
          portStatsLocked->emplace(
              portId, folly::Synchronized<FabricLinkMonPortStats>{});
        }
      }
    }
  }

  sw_->getBackgroundEvb()->runInFbossEventBaseThread(
      [this] { this->timeoutExpired(); });
}

// Stop fabric link monitoring by canceling the periodic timeout on the
// background event base. Ensures no new packets are sent after stop is
// called.
void FabricLinkMonitoringManager::stop() {
  sw_->getBackgroundEvb()->runInFbossEventBaseThreadAndWait(
      [this] { this->cancelTimeout(); });
}

// Timeout callback that executes periodic monitoring cycles. Called
// automatically by AsyncTimeout at configured intervals to send
// monitoring packets on all fabric ports and reschedule itself for
// the next cycle.
void FabricLinkMonitoringManager::timeoutExpired() noexcept {
  try {
    sendPacketsOnAllFabricPorts();
  } catch (const std::exception& ex) {
    XLOG(ERR) << "FabricLinkMonitoring: Failed to send fabric link "
              << "monitoring packets. Error: " << folly::exceptionStr(ex);
  }
  scheduleTimeout(intervalMsecs_);
}

// Get port group ID for a given port. Groups ports by their virtual device
// (VD) ID for outstanding packet management. For switches without virtual
// devices, all ports are considered to be in group 0.
int FabricLinkMonitoringManager::getPortGroup(PortID portId) const {
  if (isTestMode()) {
    // In unit test cases, avoid dependency on platform mapping
    return static_cast<int>(portId) % 4;
  }

  int portGroup = 0;
  const auto& pPort = sw_->getPlatformMapping()->getPlatformPort(portId);
  if (pPort.mapping()->virtualDeviceId().has_value()) {
    portGroup = *pPort.mapping()->virtualDeviceId();
  }
  return portGroup;
}

// Get expected payload pattern for a sequence number. Returns alternating
// patterns based on sequence number parity for validation - even and odd
// sequence numbers return a different alternating sequence of 0s and 1s.
// The goal here is to validate integrity of evey bit in the packet in the
// pipeline.
uint32_t FabricLinkMonitoringManager::getPayloadPattern(uint64_t sequenceNum) {
  if (sequenceNum % 2) {
    return 0xA5A5A5A5;
  } else {
    return 0x5A5A5A5A;
  }
}

// Get statistics for a specific port including TX count, RX count, dropped
// packets, and validation errors. Returns empty stats if port not found.
// Used primarily for tests/CLI.
FabricLinkMonPortStats FabricLinkMonitoringManager::getFabricLinkMonPortStats(
    const PortID& portId) const {
  auto lockedStats = portStats_.rlock();
  auto it = lockedStats->find(portId);
  if (it == lockedStats->end()) {
    return FabricLinkMonPortStats{}; // Return empty stats if port not found
  }
  auto portStats = it->second.rlock();
  return *portStats;
}

// Get statistics for all monitored ports. This is more efficient than
// calling getFabricLinkMonPortStats() for each port individually.
// Used for fb303 stats export.
std::map<PortID, FabricLinkMonPortStats>
FabricLinkMonitoringManager::getAllFabricLinkMonPortStats() const {
  std::map<PortID, FabricLinkMonPortStats> result;
  auto lockedStats = portStats_.rlock();
  for (const auto& [portId, syncedStats] : *lockedStats) {
    auto portStats = syncedStats.rlock();
    result[portId] = *portStats;
  }
  return result;
}

void FabricLinkMonitoringManager::fillAgentFabricLinkMonitoringStats(
    AgentStats& agentStats) const {
  const auto state = sw_->getState();
  for (const auto& [portId, stats] : getAllFabricLinkMonPortStats()) {
    const auto port = state->getPorts()->getNodeIf(portId);
    if (!port) {
      continue;
    }
    auto it = agentStats.hwPortStats()->find(port->getName());
    if (it == agentStats.hwPortStats()->end()) {
      continue;
    }
    auto& portStats = it->second;
    portStats.fabricLinkMonitoringTxPackets_() = *stats.txCount();
    portStats.fabricLinkMonitoringRxPackets_() = *stats.rxCount();
    portStats.fabricLinkMonitoringDroppedPackets_() = *stats.droppedCount();
    portStats.fabricLinkMonitoringInvalidPayloadPackets_() =
        *stats.invalidPayloadCount();
    portStats.fabricLinkMonitoringNoPendingSeqNumPackets_() =
        *stats.noPendingSeqNumCount();
  }
}

// Get pending sequence numbers for a specific port
std::vector<uint64_t> FabricLinkMonitoringManager::getPendingSequenceNumbers(
    const PortID& portId) const {
  auto lockedPendingSeqNums = portPendingSequenceNumbers_.rlock();
  auto it = lockedPendingSeqNums->find(portId);
  if (it == lockedPendingSeqNums->end()) {
    return {}; // Return empty vector if port not found
  }
  return std::vector<uint64_t>(it->second.begin(), it->second.end());
}

// Send monitoring packets on all fabric ports by iterating through all port
// groups and sending packets on each group's ports while respecting per-group
// outstanding packet limits to avoid resource issues in VDs.
void FabricLinkMonitoringManager::sendPacketsOnAllFabricPorts() {
  XLOG(DBG4) << "FabricLinkMonitoring: Found " << portToGroupMap_.size()
             << " fabric ports in " << portGroupToPortsMap_.size() << " groups";
  for (const auto& [groupId, ports] : portGroupToPortsMap_) {
    sendPacketsForPortGroup(groupId);
  }
}

// Get outstanding packet count for a port group. Returns the number of packets
// sent but not yet received/dropped, used to flow control to limit outstanding
// packets. Returns 0 if group not found.
size_t FabricLinkMonitoringManager::getOutstandingPacketCountForGroup(
    int portGroupId) const {
  auto lockedStats = portGroupStats_.rlock();
  auto it = lockedStats->find(portGroupId);
  if (it == lockedStats->end()) {
    return 0;
  }
  return it->second.outstandingPackets;
}

// There are limits on the number of outstanding packets possible per
// port group, and hence packet sent is grouped into a per group packet
// send.
void FabricLinkMonitoringManager::sendPacketsForPortGroup(int portGroupId) {
  auto it = portGroupToPortsMap_.find(portGroupId);
  if (it == portGroupToPortsMap_.end() || it->second.empty()) {
    return;
  }
  // Get the fabric ports in the port group
  const auto& fabricPortIds = it->second;

  // Packet send will not happen if the outstanding packets in port group
  // exceeded the limit, hence for each iteration, need to start with the
  // port we left off last time to ensure all ports are covered.
  size_t startIndex;
  {
    auto lockedStats = portGroupStats_.rlock();
    auto statsIt = lockedStats->find(portGroupId);
    if (statsIt != lockedStats->end()) {
      startIndex = statsIt->second.lastPortIndex;
    } else {
      startIndex = 0;
    }
  }

  std::shared_ptr<SwitchState> state = sw_->getState();
  size_t portsSent = 0;
  size_t currentIndex = 0;
  // Send and receive happen in different threads, so check the
  // outstanding packet count to decide when to stop sending.
  bool shouldSendPacket = true;
  size_t lastTxedPortIndex = startIndex;

  for (size_t i = 0; i < fabricPortIds.size(); ++i) {
    currentIndex = (startIndex + i) % fabricPortIds.size();
    PortID portId = fabricPortIds.at(currentIndex);
    auto port = state->getPorts()->getNodeIf(portId);
    if (!port || !port->isPortUp()) {
      // Applied only to fabric ports which are UP. A down port never ages out
      // its pending packets, so release them here (without counting them as
      // dropped) or they would hold the group's outstanding budget forever.
      releasePendingPackets(portId, portGroupId);
      continue;
    }

    if (shouldSendPacket) {
      auto lockedStats = portGroupStats_.wlock();
      auto& groupStats = (*lockedStats)[portGroupId];
      if (groupStats.outstandingPackets >=
          FLAGS_fabric_link_monitoring_max_outstanding_packets) {
        shouldSendPacket = false;
        XLOG(DBG4) << "Port group " << portGroupId
                   << " has reached max outstanding packets ("
                   << FLAGS_fabric_link_monitoring_max_outstanding_packets
                   << "), stopping at port index " << currentIndex
                   << " after sending on " << portsSent << " ports!";
      } else {
        portsSent++;
        lastTxedPortIndex = currentIndex;
      }
    }
    packetSendAndOutstandingHandling(port, portGroupId, shouldSendPacket);
  }

  // Update the last port we transmitted packet on so that we can start at
  // the next port in sequence for the next iteration. Only update if we
  // actually sent packets, otherwise keep the previous startIndex.
  if (portsSent > 0) {
    auto lockedStats = portGroupStats_.wlock();
    auto& groupStats = (*lockedStats)[portGroupId];
    // Start from the next port after the last transmitted port
    groupStats.lastPortIndex = (lastTxedPortIndex + 1) % fabricPortIds.size();
  }
}

// Send a single monitoring packet on a port as specified in the params.
// Increments TX count, creates monitoring packet with port ID and seq
// number, sends packet through SwSwitch with FABRIC_LINK_MONITORING
// type, adds sequence number to pending queue, and updates outstanding
// packet count. Even if pkt tx is not desired, make sure to keep
// dropping pending pkts based on offset from current sequence number.
// This is to ensure that outstanding count can be kept in control in
// case some ports are never getting packets. Implements drop detection
// when pending queue exceeds limit - oldest sequence number is removed
// and marked as dropped considering the packet as timed out given the
// delay involved.
void FabricLinkMonitoringManager::packetSendAndOutstandingHandling(
    const std::shared_ptr<Port>& port,
    int portGroupId,
    bool shouldSendPacket) {
  PortID portId = port->getID();
  int changeToOutstandingPacketCount{0};
  {
    auto stats = portStats_.wlock()->at(portId).wlock();
    auto pendingSeqNumsLocked = portPendingSequenceNumbers_.wlock();
    auto& pendingSeqNums = (*pendingSeqNumsLocked)[portId];
    // Get the next sequence number from stats and increment
    uint64_t sequenceNumber = *stats->sequenceNumber() + 1;
    stats->sequenceNumber() = sequenceNumber;

    if (shouldSendPacket) {
      auto pkt = createMonitoringPacket(portId, sequenceNumber);
      if (!pkt) {
        XLOG(DBG4)
            << "FabricLinkMonitoring: Failed to create monitoring packet for port "
            << portId;
      } else {
        sw_->sendPacketOutOfPortSyncForPktType(
            std::move(pkt), portId, PacketType::FABRIC_LINK_MONITORING);
        // Track pending sequence numbers
        pendingSeqNums.push_back(sequenceNumber);
        // Increment outstanding packet count.
        changeToOutstandingPacketCount += 1;
        XLOG(DBG4) << "FabricLinkMonitoring: Sent packet on port " << portId
                   << " with sequence number " << sequenceNumber;
        stats->txCount() = *stats->txCount() + 1;
      }
    }

    // Ideally, if packets are being received for the port, pending sequence
    // numbers should get cleared. But in case receive is not happening, we
    // need to clear the pending sequence numbers assuming those packets are
    // dropped as we have not received those in the interval needed to send
    // FLAGS_fabric_link_monitoring_max_pending_seq_numbers packets.
    if (pendingSeqNums.size() &&
        (pendingSeqNums.front() +
             FLAGS_fabric_link_monitoring_max_pending_seq_numbers <
         sequenceNumber)) {
      uint64_t droppedSeqNum = pendingSeqNums.front();
      pendingSeqNums.pop_front();
      stats->droppedCount() = *stats->droppedCount() + 1;
      // We decided to consider one of the outstanding packets as dropped,
      // which means we should not expect to see this packet anymore and
      // should decrement the outstanding packet count.
      changeToOutstandingPacketCount -= 1;

      XLOG(DBG4) << "FabricLinkMonitoring: Packet with sequence number "
                 << droppedSeqNum << " on port " << portId
                 << " considered dropped (not received after "
                 << FLAGS_fabric_link_monitoring_max_pending_seq_numbers
                 << " subsequent packets)";
    }
  }

  // If the outstanding packet count has changed, update the group stats.
  if (changeToOutstandingPacketCount) {
    auto lockedStats = portGroupStats_.wlock();
    auto& groupStats = (*lockedStats)[portGroupId];
    groupStats.outstandingPackets += changeToOutstandingPacketCount;
  }
}

// Release all sequence numbers still pending on a port and return them to the
// port group's outstanding budget. Used for ports that are not UP: their
// packets will not be received, and the offset-based drop in
// packetSendAndOutstandingHandling() only runs for UP ports.
//
// Released packets are deliberately not added to droppedCount: that counter
// reports loss on an UP link, while packets cut off by the link going down
// say nothing about link quality and the down state is reported separately.
void FabricLinkMonitoringManager::releasePendingPackets(
    PortID portId,
    int portGroupId) {
  size_t released{0};
  {
    auto pendingSeqNumsLocked = portPendingSequenceNumbers_.wlock();
    auto it = pendingSeqNumsLocked->find(portId);
    if (it == pendingSeqNumsLocked->end() || it->second.empty()) {
      return;
    }
    released = it->second.size();
    it->second.clear();
  }
  auto lockedStats = portGroupStats_.wlock();
  auto& groupStats = (*lockedStats)[portGroupId];
  groupStats.outstandingPackets -=
      std::min(released, groupStats.outstandingPackets);
  XLOG(DBG4) << "FabricLinkMonitoring: Released " << released
             << " pending packets on port " << portId
             << " which is not UP, port group " << portGroupId
             << " outstanding is now " << groupStats.outstandingPackets;
}

// Create a 480-byte monitoring packet with: 8 bytes sequence number
// (big-endian), 4 bytes port ID (big-endian), and rest of the bytes payload
// pattern with alternating 0s and 1s based on sequence number parity.
std::unique_ptr<TxPacket> FabricLinkMonitoringManager::createMonitoringPacket(
    const PortID& portId,
    uint64_t sequenceNumber) {
  uint32_t frameLen = kFabricLinkMonitoringPacketSize;
  auto pkt = sw_->allocatePacket(frameLen);

  RWPrivateCursor cursor(pkt->buf());
  cursor.writeBE<uint64_t>(sequenceNumber);
  cursor.writeBE<uint32_t>(static_cast<uint32_t>(portId));

  uint32_t fillPattern = getPayloadPattern(sequenceNumber);

  // Payload is the space after seq num and port ID
  size_t payloadSize =
      kFabricLinkMonitoringPacketSize - sizeof(uint64_t) - sizeof(uint32_t);
  for (size_t i = 0; i < payloadSize; i += 4) {
    cursor.writeBE<uint32_t>(fillPattern);
  }

  if (cursor.length() > 0) {
    memset(cursor.writableData(), 0, cursor.length());
  }

  return pkt;
}

// Handle received monitoring packet. Extracts sequence number and port ID,
// verifies port ID matches packet source port (detects misrouting), validates
// payload pattern matches expected value (detects corruption), and processes
// pending sequence numbers in order. If received sequence number > expected,
// all earlier pending sequence numbers are marked as dropped given out of
// order delivery is not possible. Decrements outstanding packet count for
// the port group for successfully received packets and dropped packets to
// enable continued transmission.
void FabricLinkMonitoringManager::handlePacket(
    std::unique_ptr<RxPacket> pkt,
    folly::io::Cursor cursor) {
  PortID portId = pkt->getSrcPort();

  uint64_t receivedSequenceNumber = cursor.readBE<uint64_t>();
  uint32_t receivedPortId = cursor.readBE<uint32_t>();

  if (receivedPortId != static_cast<uint32_t>(portId)) {
    auto lockedStats = portStats_.wlock();
    auto it = lockedStats->find(portId);
    if (it != lockedStats->end()) {
      auto stats = it->second.wlock();
      *stats->invalidPayloadCount() = *stats->invalidPayloadCount() + 1;
    }
    XLOG(DBG4) << "FabricLinkMonitoring: Port ID mismatch for port " << portId
               << ", received port ID " << receivedPortId
               << ", sequence number " << receivedSequenceNumber;
    return;
  }

  uint32_t fillPattern = getPayloadPattern(receivedSequenceNumber);
  size_t payloadSize =
      kFabricLinkMonitoringPacketSize - sizeof(uint64_t) - sizeof(uint32_t);
  for (size_t i = 0; i < payloadSize; i += 4) {
    uint32_t payloadData = cursor.readBE<uint32_t>();
    if (payloadData != fillPattern) {
      // Payload is not valid
      auto lockedStats = portStats_.wlock();
      auto it = lockedStats->find(portId);
      if (it != lockedStats->end()) {
        auto stats = it->second.wlock();
        *stats->invalidPayloadCount() = *stats->invalidPayloadCount() + 1;
      }
      XLOG(DBG4) << "FabricLinkMonitoring: Payload mismatch on port " << portId
                 << " for sequence number " << receivedSequenceNumber
                 << ", data seen 0x" << std::hex << payloadData
                 << ", expected 0x" << fillPattern;
      ;
      return;
    }
  }

  size_t droppedPackets = 0;
  bool packetProcessed = false;

  {
    folly::Synchronized<FabricLinkMonPortStats>::LockedPtr stats;
    {
      auto lockedStats = portStats_.wlock();
      auto it = lockedStats->find(portId);
      if (it == lockedStats->end()) {
        XLOG(DBG4) << "FabricLinkMonitoring: Received packet on port " << portId
                   << " but no stats found for port!";
        return;
      }

      stats = it->second.wlock();
    }

    // Access the pending sequence numbers
    auto pendingSeqNumsLocked = portPendingSequenceNumbers_.wlock();
    auto& pendingSeqNums = (*pendingSeqNumsLocked)[portId];

    // Receiving a packet when we are not expecting a sequence number
    // or when we get a sequence number greater than we expect to see.
    if (pendingSeqNums.empty() ||
        pendingSeqNums.back() < receivedSequenceNumber) {
      *stats->noPendingSeqNumCount() = *stats->noPendingSeqNumCount() + 1;
      XLOG(DBG4) << "FabricLinkMonitoring: Received packet on port " << portId
                 << " with sequence number " << receivedSequenceNumber
                 << ", but no pending sequence number found!";
      return;
    }

    while (!pendingSeqNums.empty() &&
           pendingSeqNums.front() < receivedSequenceNumber) {
      uint64_t droppedSeqNum = pendingSeqNums.front();
      pendingSeqNums.pop_front();
      *stats->droppedCount() = *stats->droppedCount() + 1;
      droppedPackets++;
      XLOG(DBG4) << "FabricLinkMonitoring: Pending sequence number "
                 << droppedSeqNum << " on port " << portId
                 << " considered dropped (received later packet "
                 << "sequence number " << receivedSequenceNumber << " first)";
    }

    if (!pendingSeqNums.empty() &&
        pendingSeqNums.front() == receivedSequenceNumber) {
      pendingSeqNums.pop_front();
      *stats->rxCount() = *stats->rxCount() + 1;
      packetProcessed = true;
    }

    XLOG(DBG4)
        << "FabricLinkMonitoring: Successfully received and verified packet on port "
        << portId << " with sequence number " << receivedSequenceNumber
        << ". TX count: " << *stats->txCount()
        << ", RX count: " << *stats->rxCount()
        << ", Dropped count: " << *stats->droppedCount()
        << ", Pending: " << pendingSeqNums.size();
  }

  if (packetProcessed || droppedPackets > 0) {
    auto groupIt = portToGroupMap_.find(portId);
    if (groupIt != portToGroupMap_.end()) {
      int portGroupId = groupIt->second;
      auto lockedStats = portGroupStats_.wlock();
      auto& groupStats = (*lockedStats)[portGroupId];
      size_t packetsToRelease = (packetProcessed ? 1 : 0) + droppedPackets;
      if (groupStats.outstandingPackets >= packetsToRelease) {
        groupStats.outstandingPackets -= packetsToRelease;
      } else {
        groupStats.outstandingPackets = 0;
      }
    }
  }
}

} // namespace facebook::fboss
