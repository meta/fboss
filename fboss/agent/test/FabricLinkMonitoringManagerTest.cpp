/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/FabricLinkMonitoringManager.h"

#include <fb303/ServiceData.h>
#include <folly/Memory.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <folly/logging/xlog.h>
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include "fboss/agent/RxPacket.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/hw/mock/MockRxPacket.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

using ::testing::_;
using ::testing::AtLeast;

using namespace facebook::fboss;
using folly::MacAddress;
using folly::io::Cursor;
using folly::io::RWPrivateCursor;
using std::make_shared;
using std::shared_ptr;
using std::unique_ptr;

namespace {

constexpr size_t kFabricLinkMonitoringPacketSize = 480;

// Helper function to add a DSF node to config
void addDsfNode(
    cfg::SwitchConfig& config,
    int64_t switchId,
    const std::string& name,
    cfg::DsfNodeType nodeType,
    std::optional<int> fabricLevel = std::nullopt,
    std::optional<PlatformType> platformType = std::nullopt) {
  cfg::DsfNode node;
  node.switchId() = switchId;
  node.name() = name;
  node.type() = nodeType;
  if (fabricLevel.has_value()) {
    node.fabricLevel() = *fabricLevel;
  }
  if (platformType.has_value()) {
    node.platformType() = *platformType;
  } else {
    node.platformType() = PlatformType::PLATFORM_MERU800BIA;
  }
  (*config.dsfNodes())[switchId] = node;
}

// Helper function to add a fabric port with expected neighbor
void addFabricPort(
    cfg::SwitchConfig& config,
    int portId,
    const std::string& portName,
    const std::string& neighborSwitch,
    const std::string& neighborPort) {
  cfg::Port port;
  port.logicalID() = portId;
  port.name() = portName;
  port.portType() = cfg::PortType::FABRIC_PORT;
  port.expectedNeighborReachability() = std::vector<cfg::PortNeighbor>();

  cfg::PortNeighbor neighbor;
  neighbor.remoteSystem() = neighborSwitch;
  neighbor.remotePort() = neighborPort;
  port.expectedNeighborReachability()->push_back(neighbor);

  config.ports()->push_back(port);
}

// Helper to create a realistic VoQ config with 160 fabric ports
cfg::SwitchConfig createVoqConfig() {
  cfg::SwitchConfig config;
  int64_t switchId{0};
  config.switchSettings() = cfg::SwitchSettings();
  config.switchSettings()->switchId() = switchId;
  config.switchSettings()->switchType() = cfg::SwitchType::VOQ;
  config.dsfNodes() = std::map<int64_t, cfg::DsfNode>();
  config.ports() = std::vector<cfg::Port>();

  // Add VoQ switch as interface node
  addDsfNode(config, switchId, "voq0", cfg::DsfNodeType::INTERFACE_NODE);

  // Add 40 fabric switches (switchIds: 4, 8, 12, ..., 160)
  for (int i = 1; i <= 40; i++) {
    int64_t fabricSwitchId = i * 4;
    addDsfNode(
        config,
        fabricSwitchId,
        "fabric" + std::to_string(fabricSwitchId),
        cfg::DsfNodeType::FABRIC_NODE,
        1);
  }

  // Add 160 ports: 4 ports per fabric switch
  int portId = 1;
  for (int i = 1; i <= 40; i++) {
    int64_t fabricSwitchId = i * 4;
    std::string fabricSwitchName = "fabric" + std::to_string(fabricSwitchId);

    for (int portOffset = 0; portOffset < 4; portOffset++) {
      addFabricPort(
          config,
          portId,
          "fab1/" + std::to_string(i) + "/" + std::to_string(portOffset + 1),
          fabricSwitchName,
          "fab1/1/" + std::to_string(portOffset + 1));
      portId++;
    }
  }

  return config;
}

// Setup for VOQ switch with fabric ports
unique_ptr<HwTestHandle> setupTestHandle(
    SwitchFlags flags = SwitchFlags::DEFAULT) {
  auto state = testStateAWithPortsUp();
  addSwitchInfo(
      state,
      cfg::SwitchType::VOQ,
      0,
      cfg::AsicType::ASIC_TYPE_MOCK,
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN(),
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX(),
      0,
      std::nullopt,
      std::nullopt,
      MockPlatform::getMockLocalMac().toString());

  for (auto& portMap : std::as_const(*state->getPorts())) {
    for (auto& port : std::as_const(*portMap.second)) {
      auto newPort = port.second->modify(&state);
      newPort->setPortType(cfg::PortType::FABRIC_PORT);
    }
  }

  return createTestHandle(state, flags);
}

void setupL1FabricSwitch(
    std::shared_ptr<SwitchState>& state,
    int64_t l1SwitchId) {
  addSwitchInfo(
      state,
      cfg::SwitchType::FABRIC,
      l1SwitchId,
      cfg::AsicType::ASIC_TYPE_MOCK,
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN(),
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX(),
      1, // fabricLevel = 1 for L1 fabric node
      std::nullopt,
      std::nullopt,
      MockPlatform::getMockLocalMac().toString());
}

void setupDsfNodesForDualStage(
    std::shared_ptr<SwitchState>& state,
    int64_t l1SwitchId) {
  auto dsfNodeMap = std::make_shared<MultiSwitchDsfNodeMap>();

  // Add L1 fabric switch as a DSF node
  auto l1Node = std::make_shared<DsfNode>(SwitchID(l1SwitchId));
  auto l1NodeCfg = makeDsfNodeCfg(
      l1SwitchId,
      cfg::DsfNodeType::FABRIC_NODE,
      std::nullopt /* clusterId */,
      cfg::AsicType::ASIC_TYPE_MOCK,
      1 /* fabricLevel */);
  l1NodeCfg.name() = "fabric_l1_0";
  l1NodeCfg.platformType() = PlatformType::PLATFORM_MERU800BIA;
  l1Node->fromThrift(l1NodeCfg);
  dsfNodeMap->addNode(l1Node, HwSwitchMatcher());

  // Add L2 fabric switches as DSF nodes
  for (int i = 0; i < 5; i++) {
    int64_t l2SwitchId = 200 + i;
    auto l2Node = std::make_shared<DsfNode>(SwitchID(l2SwitchId));
    auto l2NodeCfg = makeDsfNodeCfg(
        l2SwitchId,
        cfg::DsfNodeType::FABRIC_NODE,
        std::nullopt /* clusterId */,
        cfg::AsicType::ASIC_TYPE_MOCK,
        2 /* fabricLevel */);
    l2NodeCfg.name() = "fabric_l2_" + std::to_string(l2SwitchId);
    l2NodeCfg.platformType() = PlatformType::PLATFORM_MERU800BIA;
    l2Node->fromThrift(l2NodeCfg);
    dsfNodeMap->addNode(l2Node, HwSwitchMatcher());
  }

  state->resetDsfNodes(dsfNodeMap);
}

void setupFabricPortsWithL2Neighbors(std::shared_ptr<SwitchState>& state) {
  int portIndex = 0;
  for (auto& portMap : std::as_const(*state->getPorts())) {
    for (auto& port : std::as_const(*portMap.second)) {
      auto newPort = port.second->modify(&state);
      newPort->setPortType(cfg::PortType::FABRIC_PORT);

      // Set expected neighbor to one of the L2 switches
      int l2Index = portIndex % 5;
      std::string expectedNeighbor =
          "fabric_l2_" + std::to_string(200 + l2Index);
      cfg::PortNeighbor neighbor;
      neighbor.remoteSystem() = expectedNeighbor;
      neighbor.remotePort() = "port" + std::to_string(portIndex);
      newPort->setExpectedNeighborReachability({neighbor});

      portIndex++;
    }
  }
}

// Setup for dual-stage FABRIC switch (L1 fabric node with L2 connections)
unique_ptr<HwTestHandle> setupDualStageFabricTestHandle() {
  auto state = testStateAWithPortsUp();
  int64_t l1SwitchId = 0;

  setupL1FabricSwitch(state, l1SwitchId);
  setupDsfNodesForDualStage(state, l1SwitchId);
  setupFabricPortsWithL2Neighbors(state);

  return createTestHandle(state);
}

TxMatchFn checkFabricMonitoringPacket() {
  return [=](const TxPacket* pkt) {
    const auto* buf = pkt->buf();
    const auto chainlen = buf->computeChainDataLength();

    EXPECT_EQ(chainlen, kFabricLinkMonitoringPacketSize);

    Cursor c(buf);

    auto sequenceNumber = c.readBE<uint64_t>();
    EXPECT_GE(sequenceNumber, 0);

    auto portId = c.readBE<uint32_t>();
    EXPECT_GE(portId, 0);

    auto payloadPattern =
        FabricLinkMonitoringManager::getPayloadPattern(sequenceNumber);
    size_t payloadSize =
        kFabricLinkMonitoringPacketSize - sizeof(uint64_t) - sizeof(uint32_t);
    for (size_t i = 0; i < payloadSize; i += 4) {
      auto value = c.readBE<uint32_t>();
      EXPECT_EQ(value, payloadPattern);
    }
  };
}

std::unique_ptr<RxPacket> createFabricMonitoringRxPacket(
    SwSwitch* /*sw*/,
    const PortID& portId,
    uint64_t sequenceNumber,
    std::optional<PacketType> packetType = std::nullopt) {
  std::vector<uint8_t> data(kFabricLinkMonitoringPacketSize);

  // Serialize sequenceNumber (uint64_t) in big-endian order
  uint64_t seq_be = htobe64(sequenceNumber);
  std::memcpy(data.data(), &seq_be, sizeof(seq_be));

  // Serialize portId (uint32_t) in big-endian order
  uint32_t port_be = htobe32(static_cast<uint32_t>(portId));
  std::memcpy(data.data() + 8, &port_be, sizeof(port_be));

  // Fill the rest of the buffer with pattern
  size_t fillStart = sizeof(uint64_t) + sizeof(uint32_t);
  ;
  auto fillPattern =
      FabricLinkMonitoringManager::getPayloadPattern(sequenceNumber);
  for (size_t i = fillStart; i + 4 <= kFabricLinkMonitoringPacketSize; i += 4) {
    std::memcpy(data.data() + i, &fillPattern, sizeof(fillPattern));
  }

  auto buf = folly::IOBuf::copyBuffer(data.data(), data.size());
  auto rxPkt = std::make_unique<MockRxPacket>(std::move(buf));
  rxPkt->setSrcPort(portId);
  rxPkt->setPacketType(packetType);
  return rxPkt;
}

// Helper function to setup VOQ switch state with fabric ports
std::shared_ptr<SwitchState> setupVoqSwitchState(
    const cfg::SwitchConfig& config) {
  auto state = std::make_shared<SwitchState>();
  addSwitchInfo(
      state,
      cfg::SwitchType::VOQ,
      0,
      cfg::AsicType::ASIC_TYPE_MOCK,
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN(),
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX(),
      0,
      std::nullopt,
      std::nullopt,
      MockPlatform::getMockLocalMac().toString());

  auto portMap = std::make_shared<MultiSwitchPortMap>();
  for (const auto& cfgPort : *config.ports()) {
    auto portId = PortID(*cfgPort.logicalID());
    auto port = std::make_shared<Port>(portId, *cfgPort.name());
    port->setPortType(*cfgPort.portType());
    port->setOperState(true);
    port->setAdminState(cfg::PortState::ENABLED);
    portMap->addNode(port, HwSwitchMatcher());
  }

  state->resetPorts(portMap);
  return state;
}

// VOQ switch state with the given fabric ports, each up or down as specified.
std::shared_ptr<SwitchState> setupVoqSwitchStateWithFabricPorts(
    const std::map<PortID, bool>& portsUp) {
  auto state = std::make_shared<SwitchState>();
  addSwitchInfo(
      state,
      cfg::SwitchType::VOQ,
      0,
      cfg::AsicType::ASIC_TYPE_MOCK,
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN(),
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX(),
      0,
      std::nullopt,
      std::nullopt,
      MockPlatform::getMockLocalMac().toString());
  auto portMap = std::make_shared<MultiSwitchPortMap>();
  for (const auto& [portId, up] : portsUp) {
    const std::string name =
        folly::to<std::string>("fab1/1/", static_cast<int>(portId));
    auto port = std::make_shared<Port>(portId, name);
    port->setPortType(cfg::PortType::FABRIC_PORT);
    port->setOperState(up);
    port->setAdminState(cfg::PortState::ENABLED);
    portMap->addNode(port, HwSwitchMatcher());
  }
  state->resetPorts(portMap);
  return state;
}

// Helper function to count fabric ports in state
int countFabricPorts(SwSwitch* sw) {
  int numFabricPorts = 0;
  for (const auto& portMap : std::as_const(*sw->getState()->getPorts())) {
    for (const auto& [portId, port] : std::as_const(*portMap.second)) {
      (void)portId;
      if (port->getPortType() == cfg::PortType::FABRIC_PORT &&
          port->isPortUp()) {
        numFabricPorts++;
      }
    }
  }
  return numFabricPorts;
}

// Helper function to setup packet tracking expectations
void setupPacketTrackingExpectations(
    SwSwitch* sw,
    std::atomic<int>& totalPacketsSent,
    int expectedFabricPorts) {
  EXPECT_HW_CALL(
      sw,
      sendPacketOutOfPortSyncForPktType_(
          TxPacketMatcher::createMatcher(
              "Fabric Monitoring Packet",
              [&totalPacketsSent](const TxPacket* /*pkt*/) {
                totalPacketsSent++;
              }),
          _,
          _))
      .Times(AtLeast(expectedFabricPorts));
}

// Helper function to verify transmission invariants
void verifyTransmissionInvariants(
    FabricLinkMonitoringManager* manager,
    int expectedFabricPorts) {
  WITH_RETRIES_N_TIMED(5, std::chrono::milliseconds(1000), {
    int portsWithTx = 0;
    int portsWithPending = 0;
    auto allStats = manager->getAllFabricLinkMonPortStats();
    for (const auto& [portId, stats] : allStats) {
      if (*stats.txCount() > 2) {
        portsWithTx++;
      }
      if (manager->getPendingSequenceNumbers(portId).size() >= 1) {
        portsWithPending++;
      }
      EXPECT_EQ(*stats.rxCount(), 0);
    }

    EXPECT_EVENTUALLY_EQ(portsWithTx, expectedFabricPorts);
    EXPECT_EVENTUALLY_EQ(portsWithPending, expectedFabricPorts);
  });
}

} // namespace

TEST(FabricLinkMonitoringManagerTest, CreateManager) {
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  auto manager = std::make_unique<FabricLinkMonitoringManager>(sw);
  EXPECT_NE(manager, nullptr);
}

TEST(FabricLinkMonitoringManagerTest, FsdbStatsWithoutMonitoring) {
  auto handle = setupTestHandle();
  auto sw = handle->getSw();
  ASSERT_EQ(sw->getFabricLinkMonitoringManager(), nullptr);
  multiswitch::HwSwitchStats hwStats;
  hwStats.hwPortStats()["port1"] = HwPortStats{};
  sw->updateHwSwitchStats(0, hwStats);
  const auto agentStats = sw->fillFsdbStats();
  const auto& portStats = agentStats.hwPortStats()->at("port1");
  EXPECT_FALSE(portStats.fabricLinkMonitoringTxPackets_().has_value());
  EXPECT_FALSE(portStats.fabricLinkMonitoringRxPackets_().has_value());
  EXPECT_FALSE(portStats.fabricLinkMonitoringDroppedPackets_().has_value());
  EXPECT_FALSE(
      portStats.fabricLinkMonitoringInvalidPayloadPackets_().has_value());
  EXPECT_FALSE(
      portStats.fabricLinkMonitoringNoPendingSeqNumPackets_().has_value());
}

TEST(FabricLinkMonitoringManagerTest, SendPacketsOnFabricPorts) {
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  EXPECT_HW_CALL(
      sw,
      sendPacketOutOfPortSyncForPktType_(
          TxPacketMatcher::createMatcher(
              "Fabric Monitoring Packet", checkFabricMonitoringPacket()),
          _,
          _))
      .Times(AtLeast(1));

  auto manager = std::make_unique<FabricLinkMonitoringManager>(sw);
  manager->start();

  waitForStateUpdates(sw);
  waitForBackgroundThread(sw);

  manager->stop();
}

TEST(FabricLinkMonitoringManagerTest, PacketPayloadValidation) {
  auto handle = setupTestHandle(SwitchFlags::ENABLE_FABRIC_LINK_MONITORING);
  auto sw = handle->getSw();

  auto* manager = sw->getFabricLinkMonitoringManager();
  ASSERT_NE(manager, nullptr);
  manager->start();

  waitForStateUpdates(sw);
  waitForBackgroundThread(sw);

  PortID testPort(1);

  // Get initial stats
  auto statsBefore = manager->getFabricLinkMonPortStats(testPort);

  auto rxPkt = createFabricMonitoringRxPacket(sw, testPort, 1);
  Cursor cursor(rxPkt->buf());

  EXPECT_NO_THROW(manager->handlePacket(std::move(rxPkt), std::move(cursor)));

  // Verify that rxCount was incremented, confirming good payload was processed
  auto statsAfter = manager->getFabricLinkMonPortStats(testPort);
  EXPECT_EQ(*statsAfter.rxCount(), *statsBefore.rxCount() + 1);
  EXPECT_EQ(
      *statsAfter.invalidPayloadCount(), *statsBefore.invalidPayloadCount());

  manager->stop();

  const auto& portName =
      sw->getState()->getPorts()->getNode(testPort)->getName();
  multiswitch::HwSwitchStats hwStats;
  hwStats.hwPortStats()[portName] = HwPortStats{};
  sw->updateHwSwitchStats(0, hwStats);
  const auto agentStats = sw->fillFsdbStats();
  const auto& portStats = agentStats.hwPortStats()->at(portName);
  // Make sure the fabric link monitoring stats are populated in FSDB.
  EXPECT_TRUE(portStats.fabricLinkMonitoringTxPackets_().has_value());
  EXPECT_TRUE(portStats.fabricLinkMonitoringRxPackets_().has_value());
  EXPECT_TRUE(portStats.fabricLinkMonitoringDroppedPackets_().has_value());
  EXPECT_TRUE(
      portStats.fabricLinkMonitoringInvalidPayloadPackets_().has_value());
  EXPECT_TRUE(
      portStats.fabricLinkMonitoringNoPendingSeqNumPackets_().has_value());
}

TEST(FabricLinkMonitoringManagerTest, PacketSizeValidation) {
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  auto pkt = sw->allocatePacket(kFabricLinkMonitoringPacketSize);
  EXPECT_EQ(
      pkt->buf()->computeChainDataLength(), kFabricLinkMonitoringPacketSize);
}

TEST(FabricLinkMonitoringManagerTest, MultipleStartStopManager) {
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  auto manager = std::make_unique<FabricLinkMonitoringManager>(sw);

  for (int i = 0; i < 10; ++i) {
    EXPECT_NO_THROW(manager->start());
    waitForBackgroundThread(sw);
    EXPECT_NO_THROW(manager->stop());
  }
}

TEST(FabricLinkMonitoringManagerTest, PacketSequenceNumberIncrement) {
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  uint64_t lastSequenceNumber = 0;
  int packetCount = 0;

  auto getPortIdToTrack = [&]() {
    auto state = sw->getState();
    for (auto& portMap : std::as_const(*state->getPorts())) {
      for (auto& port : std::as_const(*portMap.second)) {
        if (port.second->getPortType() == cfg::PortType::FABRIC_PORT) {
          return static_cast<int>(port.second->getID());
        }
      }
    }
    return -1;
  };

  auto portIdToTrack = getPortIdToTrack();
  EXPECT_HW_CALL(
      sw,
      sendPacketOutOfPortSyncForPktType_(
          TxPacketMatcher::createMatcher(
              "Fabric Monitoring Packet",
              [&lastSequenceNumber, &packetCount, &portIdToTrack](
                  const TxPacket* pkt) {
                Cursor c(pkt->buf());
                auto seqNum = c.readBE<uint64_t>();
                auto portId = c.readBE<uint32_t>();
                if (portId == portIdToTrack) {
                  if (packetCount > 0) {
                    EXPECT_GT(seqNum, lastSequenceNumber);
                  }
                  lastSequenceNumber = seqNum;
                  packetCount++;
                }
              }),
          _,
          _))
      .Times(AtLeast(5));

  auto manager = std::make_unique<FabricLinkMonitoringManager>(sw);
  manager->start();

  waitForStateUpdates(sw);
  waitForBackgroundThread(sw);
  waitForBackgroundThread(sw);
  waitForBackgroundThread(sw);
  waitForBackgroundThread(sw);
  waitForBackgroundThread(sw);
  waitForBackgroundThread(sw);

  manager->stop();
}

TEST(FabricLinkMonitoringManagerTest, OnlyFabricPortsReceivePackets) {
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  auto state = sw->getState();
  auto newState = state->clone();

  for (auto& portMap : std::as_const(*newState->getPorts())) {
    for (auto& port : std::as_const(*portMap.second)) {
      auto newPort = port.second->modify(&newState);
      if (port.second->getID() == PortID(1)) {
        newPort->setPortType(cfg::PortType::INTERFACE_PORT);
      } else {
        newPort->setPortType(cfg::PortType::FABRIC_PORT);
      }
    }
  }

  sw->updateStateBlocking(
      "Update port types", [&](const auto&) { return newState; });

  int fabricPortCount = 0;
  for (const auto& portMap : std::as_const(*sw->getState()->getPorts())) {
    for (const auto& [portId, port] : std::as_const(*portMap.second)) {
      (void)portId;
      if (port->getPortType() == cfg::PortType::FABRIC_PORT &&
          port->isPortUp()) {
        fabricPortCount++;
      }
    }
  }

  EXPECT_HW_CALL(
      sw,
      sendPacketOutOfPortSyncForPktType_(
          TxPacketMatcher::createMatcher(
              "Fabric Monitoring Packet", checkFabricMonitoringPacket()),
          _,
          _))
      .Times(AtLeast(fabricPortCount));

  auto manager = std::make_unique<FabricLinkMonitoringManager>(sw);
  manager->start();

  waitForStateUpdates(sw);
  waitForBackgroundThread(sw);

  manager->stop();
}

TEST(FabricLinkMonitoringManagerTest, PacketDropDetectionOnMaxPendingExceeded) {
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  EXPECT_HW_CALL(
      sw,
      sendPacketOutOfPortSyncForPktType_(
          TxPacketMatcher::createMatcher(
              "Fabric Monitoring Packet", checkFabricMonitoringPacket()),
          _,
          _))
      .Times(AtLeast(4));

  auto manager = std::make_unique<FabricLinkMonitoringManager>(sw);
  manager->start();

  // Wait for multiple packets to be sent
  waitForStateUpdates(sw);
  waitForBackgroundThread(sw);
  waitForBackgroundThread(sw);
  waitForBackgroundThread(sw);
  waitForBackgroundThread(sw);

  manager->stop();
}

TEST(FabricLinkMonitoringManagerTest, MultipleSequentialPackets) {
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  EXPECT_HW_CALL(
      sw,
      sendPacketOutOfPortSyncForPktType_(
          TxPacketMatcher::createMatcher(
              "Fabric Monitoring Packet", checkFabricMonitoringPacket()),
          _,
          _))
      .Times(AtLeast(5));

  auto manager = std::make_unique<FabricLinkMonitoringManager>(sw);
  manager->start();

  // Send multiple rounds of packets
  waitForStateUpdates(sw);
  waitForBackgroundThread(sw);
  waitForBackgroundThread(sw);
  waitForBackgroundThread(sw);
  waitForBackgroundThread(sw);
  waitForBackgroundThread(sw);

  // Verify packets were sent
  PortID testPort(1);
  auto statsAfter = manager->getFabricLinkMonPortStats(testPort);
  EXPECT_GT(*statsAfter.txCount(), 0);
  // Since no packets are being received, pending sequence numbers should
  // accumulate
  EXPECT_GT(manager->getPendingSequenceNumbers(testPort).size(), 0);

  manager->stop();
}

TEST(FabricLinkMonitoringManagerTest, DetectInvalidPayload) {
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  auto manager = std::make_unique<FabricLinkMonitoringManager>(sw);
  manager->start();

  waitForStateUpdates(sw);

  // Create a packet with invalid payload
  PortID portId = PortID(1);

  // Get initial stats
  auto statsBefore = manager->getFabricLinkMonPortStats(portId);
  std::vector<uint8_t> data(kFabricLinkMonitoringPacketSize);

  // Serialize sequenceNumber
  uint64_t seq_be = htobe64(0);
  std::memcpy(data.data(), &seq_be, sizeof(seq_be));

  // Serialize portId
  uint32_t port_be = htobe32(static_cast<uint32_t>(portId));
  std::memcpy(data.data() + 8, &port_be, sizeof(port_be));

  // Fill with incorrect pattern
  size_t fillStart = sizeof(uint64_t) + sizeof(uint32_t);
  uint32_t wrongPattern = 0xDEADBEEF; // Wrong pattern
  for (size_t i = fillStart; i + 4 <= kFabricLinkMonitoringPacketSize; i += 4) {
    std::memcpy(data.data() + i, &wrongPattern, sizeof(wrongPattern));
  }

  auto buf = folly::IOBuf::copyBuffer(data.data(), data.size());
  auto rxPkt = std::make_unique<MockRxPacket>(std::move(buf));
  rxPkt->setSrcPort(portId);

  folly::io::Cursor cursor(rxPkt->buf());
  manager->handlePacket(std::move(rxPkt), cursor);

  // Verify invalidPayloadCount was incremented
  auto statsAfter = manager->getFabricLinkMonPortStats(portId);
  EXPECT_EQ(
      *statsAfter.invalidPayloadCount(),
      *statsBefore.invalidPayloadCount() + 1);
  EXPECT_EQ(*statsAfter.rxCount(), *statsBefore.rxCount());

  manager->stop();
}

TEST(FabricLinkMonitoringManagerTest, DetectPortIdMismatch) {
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  auto manager = std::make_unique<FabricLinkMonitoringManager>(sw);
  manager->start();

  waitForStateUpdates(sw);

  // Create a packet with mismatched port ID
  PortID actualPort = PortID(1);

  // Get initial stats
  auto statsBefore = manager->getFabricLinkMonPortStats(actualPort);
  PortID wrongPortInPacket = PortID(99);
  std::vector<uint8_t> data(kFabricLinkMonitoringPacketSize);

  // Serialize sequenceNumber
  uint64_t seq_be = htobe64(0);
  std::memcpy(data.data(), &seq_be, sizeof(seq_be));

  // Serialize wrong portId
  uint32_t port_be = htobe32(static_cast<uint32_t>(wrongPortInPacket));
  std::memcpy(data.data() + 8, &port_be, sizeof(port_be));

  // Fill with correct pattern
  size_t fillStart = sizeof(uint64_t) + sizeof(uint32_t);
  auto fillPattern = FabricLinkMonitoringManager::getPayloadPattern(0);
  for (size_t i = fillStart; i + 4 <= kFabricLinkMonitoringPacketSize; i += 4) {
    std::memcpy(data.data() + i, &fillPattern, sizeof(fillPattern));
  }

  auto buf = folly::IOBuf::copyBuffer(data.data(), data.size());
  auto rxPkt = std::make_unique<MockRxPacket>(std::move(buf));
  rxPkt->setSrcPort(actualPort);

  folly::io::Cursor cursor(rxPkt->buf());
  manager->handlePacket(std::move(rxPkt), cursor);

  // Verify invalidPayloadCount was incremented (port ID mismatch)
  auto statsAfter = manager->getFabricLinkMonPortStats(actualPort);
  EXPECT_EQ(
      *statsAfter.invalidPayloadCount(),
      *statsBefore.invalidPayloadCount() + 1);
  EXPECT_EQ(*statsAfter.rxCount(), *statsBefore.rxCount());

  manager->stop();
}

TEST(FabricLinkMonitoringManagerTest, OutOfOrderPacketHandling) {
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  EXPECT_HW_CALL(
      sw,
      sendPacketOutOfPortSyncForPktType_(
          TxPacketMatcher::createMatcher(
              "Fabric Monitoring Packet", checkFabricMonitoringPacket()),
          _,
          _))
      .Times(AtLeast(5));

  auto manager = std::make_unique<FabricLinkMonitoringManager>(sw);
  manager->start();

  // Wait for multiple rounds of packets to build up pending sequence numbers
  waitForStateUpdates(sw);
  waitForBackgroundThread(sw);
  waitForBackgroundThread(sw);
  waitForBackgroundThread(sw);
  waitForBackgroundThread(sw);
  waitForBackgroundThread(sw);

  // Stop the manager to avoid race condition where background thread
  // sends another packet while we're reading stats
  manager->stop();

  PortID portId = PortID(1);
  auto statsBefore = manager->getFabricLinkMonPortStats(portId);

  // Verify packets were sent and are pending
  EXPECT_GT(*statsBefore.txCount(), 0);

  // Get the pending sequence numbers
  auto pendingSeqNums = manager->getPendingSequenceNumbers(portId);
  EXPECT_GT(pendingSeqNums.size(), 0);
  ASSERT_FALSE(pendingSeqNums.empty());

  // Find the last (highest) sequence number in the pending list
  uint64_t lastSeqNum =
      *std::max_element(pendingSeqNums.begin(), pendingSeqNums.end());

  // Count how many sequence numbers are before the last one
  size_t expectedDropCount = 0;
  for (auto seqNum : pendingSeqNums) {
    if (seqNum < lastSeqNum) {
      expectedDropCount++;
    }
  }

  // Receive the last packet in the pending list
  // This should cause all earlier pending packets to be marked as dropped
  auto rxPkt = createFabricMonitoringRxPacket(sw, portId, lastSeqNum);
  folly::io::Cursor cursor(rxPkt->buf());
  manager->handlePacket(std::move(rxPkt), cursor);

  auto statsAfter = manager->getFabricLinkMonPortStats(portId);
  EXPECT_EQ(*statsAfter.rxCount(), *statsBefore.rxCount() + 1);
  // All packets with sequence numbers < lastSeqNum should now be dropped
  EXPECT_EQ(
      *statsAfter.droppedCount(),
      *statsBefore.droppedCount() + expectedDropCount);
}

TEST(FabricLinkMonitoringManagerTest, GetPayloadPatternAlternates) {
  // Verify payload pattern alternates between odd and even sequence numbers
  EXPECT_EQ(FabricLinkMonitoringManager::getPayloadPattern(0), 0x5A5A5A5A);
  EXPECT_EQ(FabricLinkMonitoringManager::getPayloadPattern(1), 0xA5A5A5A5);
  EXPECT_EQ(FabricLinkMonitoringManager::getPayloadPattern(2), 0x5A5A5A5A);
  EXPECT_EQ(FabricLinkMonitoringManager::getPayloadPattern(3), 0xA5A5A5A5);
  EXPECT_EQ(FabricLinkMonitoringManager::getPayloadPattern(100), 0x5A5A5A5A);
  EXPECT_EQ(FabricLinkMonitoringManager::getPayloadPattern(101), 0xA5A5A5A5);
}

TEST(FabricLinkMonitoringManagerTest, PortsDownNotSent) {
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  // Bring all ports down
  auto state = sw->getState();
  auto newState = state->clone();
  for (auto& portMap : std::as_const(*newState->getPorts())) {
    for (auto& port : std::as_const(*portMap.second)) {
      auto newPort = port.second->modify(&newState);
      newPort->setOperState(false);
    }
  }
  sw->updateStateBlocking(
      "bring ports down", [newState](const auto&) { return newState; });

  // Should not send any packets when ports are down
  EXPECT_HW_CALL(
      sw,
      sendPacketOutOfPortSyncForPktType_(
          TxPacketMatcher::createMatcher(
              "Fabric Monitoring Packet", checkFabricMonitoringPacket()),
          _,
          _))
      .Times(0);

  auto manager = std::make_unique<FabricLinkMonitoringManager>(sw);
  manager->start();

  waitForStateUpdates(sw);
  waitForBackgroundThread(sw);

  manager->stop();
}

TEST(FabricLinkMonitoringManagerTest, HandlePacketWithNoPendingSequence) {
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  auto manager = std::make_unique<FabricLinkMonitoringManager>(sw);
  manager->start();

  waitForStateUpdates(sw);
  waitForBackgroundThread(sw);

  // Try to receive a packet when no packets were sent
  PortID portId = PortID(1);
  auto rxPkt = createFabricMonitoringRxPacket(sw, portId, 999);
  folly::io::Cursor cursor(rxPkt->buf());

  // handlePacket should not throw even when receiving unexpected packet
  EXPECT_NO_THROW(manager->handlePacket(std::move(rxPkt), cursor));

  // Verify noPendingSeqNumCount was incremented
  auto statsAfter = manager->getFabricLinkMonPortStats(portId);
  EXPECT_GT(*statsAfter.noPendingSeqNumCount(), 0);

  manager->stop();
}

TEST(
    FabricLinkMonitoringManagerTest,
    DualStageFabricSwitchSendsPacketsToL2Ports) {
  auto handle = setupDualStageFabricTestHandle();
  auto sw = handle->getSw();

  EXPECT_HW_CALL(
      sw,
      sendPacketOutOfPortSyncForPktType_(
          TxPacketMatcher::createMatcher(
              "Fabric Monitoring Packet", checkFabricMonitoringPacket()),
          _,
          _))
      .Times(AtLeast(1));

  auto manager = std::make_unique<FabricLinkMonitoringManager>(sw);
  manager->start();

  waitForStateUpdates(sw);
  waitForBackgroundThread(sw);

  manager->stop();

  // Verify that packets were sent on the L2-connected fabric ports
  auto allStats = manager->getAllFabricLinkMonPortStats();
  EXPECT_GT(allStats.size(), 0);
  for (const auto& [portId, stats] : allStats) {
    EXPECT_GT(*stats.txCount(), 0);
  }
}

TEST(
    FabricLinkMonitoringManagerTest,
    VoqSwitchWith160FabricPortsContinuesSendingWithoutResponses) {
  FabricLinkMonitoringManager::setTestMode(true);

  auto config = createVoqConfig();
  EXPECT_EQ(config.ports()->size(), 160);

  auto state = setupVoqSwitchState(config);
  auto handle = createTestHandle(state);
  auto sw = handle->getSw();

  constexpr int kExpectedFabricPorts = 160;
  int numFabricPorts = countFabricPorts(sw);
  XLOG(INFO) << "Testing with " << numFabricPorts << " fabric ports";
  EXPECT_EQ(numFabricPorts, kExpectedFabricPorts);

  std::atomic<int> totalPacketsSent{0};
  setupPacketTrackingExpectations(sw, totalPacketsSent, kExpectedFabricPorts);

  auto manager = std::make_unique<FabricLinkMonitoringManager>(sw);
  manager->start();

  verifyTransmissionInvariants(manager.get(), kExpectedFabricPorts);

  manager->stop();
  FabricLinkMonitoringManager::setTestMode(false);
}

TEST(
    FabricLinkMonitoringManagerTest,
    LastPortIndexHandlingWithOutstandingLimitReached) {
  // Enable test mode to avoid dependency on platform mapping
  FabricLinkMonitoringManager::setTestMode(true);

  // Create a simple config with 8 fabric ports
  auto config = createVoqConfig();
  config.ports()->resize(8); // Keep only first 8 ports

  auto state = std::make_shared<SwitchState>();
  addSwitchInfo(
      state,
      cfg::SwitchType::VOQ,
      0,
      cfg::AsicType::ASIC_TYPE_MOCK,
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN(),
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX(),
      0,
      std::nullopt,
      std::nullopt,
      MockPlatform::getMockLocalMac().toString());

  // Add the 8 fabric ports from config to the state
  auto portMap = std::make_shared<MultiSwitchPortMap>();
  for (const auto& cfgPort : *config.ports()) {
    auto portId = PortID(*cfgPort.logicalID());
    auto port = std::make_shared<Port>(portId, *cfgPort.name());
    port->setPortType(*cfgPort.portType());
    port->setOperState(true); // Port is UP
    port->setAdminState(cfg::PortState::ENABLED);
    portMap->addNode(port, HwSwitchMatcher());
  }
  state->resetPorts(portMap);

  auto handle = createTestHandle(state);
  auto sw = handle->getSw();

  // Set a very low outstanding packet limit to force the limit to be reached
  FLAGS_fabric_link_monitoring_max_outstanding_packets = 2;

  std::atomic<int> totalPacketsSent{0};
  EXPECT_HW_CALL(
      sw,
      sendPacketOutOfPortSyncForPktType_(
          TxPacketMatcher::createMatcher(
              "Fabric Monitoring Packet",
              [&totalPacketsSent](const TxPacket* /*pkt*/) {
                totalPacketsSent++;
              }),
          _,
          _))
      .Times(AtLeast(8)); // Expect at least one packet per port

  auto manager = std::make_unique<FabricLinkMonitoringManager>(sw);
  manager->start();

  // Wait for multiple rounds of packet transmission
  // The test verifies that even when outstanding limit is reached,
  // the lastPortIndex correctly resumes from where it left off
  WITH_RETRIES_N_TIMED(10, std::chrono::milliseconds(1500), {
    auto allStats = manager->getAllFabricLinkMonPortStats();

    // Verify all ports have been sent packets
    int portsWithTx = 0;
    for (const auto& [portId, stats] : allStats) {
      if (*stats.txCount() > 0) {
        portsWithTx++;
      }
    }

    // All 8 ports should have sent at least one packet
    EXPECT_EVENTUALLY_EQ(portsWithTx, 8);

    int portsWithMultipleTx = 0;
    for (const auto& [portId, stats] : allStats) {
      if (*stats.txCount() >= 2) {
        portsWithMultipleTx++;
      }
    }
    // Most ports should have sent multiple packets, proving round-robin works
    EXPECT_EVENTUALLY_GE(portsWithMultipleTx, 6);
  });

  manager->stop();
  FabricLinkMonitoringManager::setTestMode(false);
}

TEST(FabricLinkMonitoringManagerTest, RxClassificationPrefersPacketType) {
  FabricLinkMonitoringManager::setTestMode(true);

  auto config = createVoqConfig();
  auto state = setupVoqSwitchState(config);
  auto handle =
      createTestHandle(state, SwitchFlags::ENABLE_FABRIC_LINK_MONITORING);
  auto sw = handle->getSw();

  auto* manager = sw->getFabricLinkMonitoringManager();
  ASSERT_NE(manager, nullptr);
  manager->start();

  const auto portId = PortID(*config.ports()->front().logicalID());
  auto noPendingSeqNums = [&]() {
    return *manager->getFabricLinkMonPortStats(portId).noPendingSeqNumCount();
  };

  // Nothing was transmitted, so a consumed packet always lands on the
  // no-pending-sequence-number path. The counter therefore tracks whether
  // the packet entered the fabric link monitoring flow.
  EXPECT_EQ(noPendingSeqNums(), 0);

  // Not a fabric link monitoring packet.
  sw->packetReceived(
      createFabricMonitoringRxPacket(sw, portId, 1, PacketType::DEFAULT));
  EXPECT_EQ(noPendingSeqNums(), 0);

  sw->packetReceived(createFabricMonitoringRxPacket(
      sw, portId, 2, PacketType::FABRIC_LINK_MONITORING));
  EXPECT_EQ(noPendingSeqNums(), 1);

  // Where the type is reported, it is authoritative: an untyped packet is
  // not a monitoring packet even though it arrived on a fabric port.
  sw->packetReceived(
      createFabricMonitoringRxPacket(sw, portId, 3, std::nullopt));
  EXPECT_EQ(noPendingSeqNums(), 1);

  manager->stop();
  FabricLinkMonitoringManager::setTestMode(false);
}

TEST(
    FabricLinkMonitoringManagerTest,
    PendingPacketsOnDownPortsDoNotBlockUpPorts) {
  gflags::FlagSaver flagSaver;
  FabricLinkMonitoringManager::setTestMode(true);

  // Test mode groups ports by portId % 4, so all four share group 0.
  const std::vector<PortID> goingDown{PortID(4), PortID(8)};
  const std::vector<PortID> comingUp{PortID(12), PortID(16)};
  FLAGS_fabric_link_monitoring_max_outstanding_packets =
      static_cast<int>(goingDown.size());
  // Keep unanswered packets pending rather than aging them out, so the ports
  // go down while still holding them.
  FLAGS_fabric_link_monitoring_max_pending_seq_numbers = 1000;
  FLAGS_fabric_link_monitoring_interval_ms = 100;

  std::map<PortID, bool> portsUp;
  for (const auto& portId : goingDown) {
    portsUp[portId] = true;
  }
  for (const auto& portId : comingUp) {
    portsUp[portId] = false;
  }
  auto handle = createTestHandle(setupVoqSwitchStateWithFabricPorts(portsUp));
  auto sw = handle->getSw();
  EXPECT_HW_CALL(sw, sendPacketOutOfPortSyncForPktType_(_, _, _))
      .Times(AtLeast(static_cast<int>(goingDown.size())));

  auto manager = std::make_unique<FabricLinkMonitoringManager>(sw);
  manager->start();

  // Nothing answers, so the group fills up with packets pending on the ports
  // that are about to go down.
  WITH_RETRIES({
    for (auto portId : goingDown) {
      EXPECT_EVENTUALLY_EQ(
          manager->getPendingSequenceNumbers(portId).size(), 1);
    }
  });

  sw->updateStateBlocking(
      "Fabric ports go down and others come up", [&](const auto& in) {
        auto newState = in->clone();
        auto setUp = [&](PortID portId, bool up) {
          auto port = newState->getPorts()->getNodeIf(portId);
          port->modify(&newState)->setOperState(up);
        };
        for (const auto& portId : goingDown) {
          setUp(portId, false);
        }
        for (const auto& portId : comingUp) {
          setUp(portId, true);
        }
        return newState;
      });

  WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(200), {
    for (auto portId : comingUp) {
      EXPECT_EVENTUALLY_GT(
          *manager->getFabricLinkMonPortStats(portId).txCount(), 0);
    }
  });

  manager->stop();
  FabricLinkMonitoringManager::setTestMode(false);
}

TEST(FabricLinkMonitoringManagerTest, RestartResetsOutstandingPackets) {
  gflags::FlagSaver flagSaver;
  FabricLinkMonitoringManager::setTestMode(true);

  // Test mode groups ports by portId % 4, so both share group 0.
  const std::vector<PortID> ports{PortID(4), PortID(8)};
  FLAGS_fabric_link_monitoring_max_outstanding_packets =
      static_cast<int>(ports.size());
  FLAGS_fabric_link_monitoring_max_pending_seq_numbers = 1000;
  FLAGS_fabric_link_monitoring_interval_ms = 100;

  auto handle = createTestHandle(
      setupVoqSwitchStateWithFabricPorts({{ports[0], true}, {ports[1], true}}));
  auto sw = handle->getSw();
  EXPECT_HW_CALL(sw, sendPacketOutOfPortSyncForPktType_(_, _, _))
      .Times(AtLeast(static_cast<int>(2 * ports.size())));

  auto manager = std::make_unique<FabricLinkMonitoringManager>(sw);
  manager->start();
  WITH_RETRIES({
    for (auto portId : ports) {
      EXPECT_EVENTUALLY_EQ(
          manager->getPendingSequenceNumbers(portId).size(), 1);
    }
  });

  // start() discards the pending sequence numbers, so it must also discard
  // the outstanding count they made up.
  manager->stop();
  manager->start();
  WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(200), {
    for (auto portId : ports) {
      EXPECT_EVENTUALLY_GT(
          *manager->getFabricLinkMonPortStats(portId).txCount(), 0);
    }
  });

  manager->stop();
  FabricLinkMonitoringManager::setTestMode(false);
}

TEST(
    FabricLinkMonitoringManagerTest,
    OnePortDownReleasesOnlyItsPendingPackets) {
  gflags::FlagSaver flagSaver;
  FabricLinkMonitoringManager::setTestMode(true);

  // Test mode groups ports by portId % 4, so both share group 0. The budget
  // holds one unanswered packet per port, and none of them age out.
  const PortID downPort(4);
  const PortID upPort(8);
  FLAGS_fabric_link_monitoring_max_outstanding_packets = 2;
  FLAGS_fabric_link_monitoring_max_pending_seq_numbers = 1000;
  FLAGS_fabric_link_monitoring_interval_ms = 100;

  auto handle = createTestHandle(
      setupVoqSwitchStateWithFabricPorts({{downPort, true}, {upPort, true}}));
  auto sw = handle->getSw();
  EXPECT_HW_CALL(sw, sendPacketOutOfPortSyncForPktType_(_, _, _))
      .Times(AtLeast(3));

  auto manager = std::make_unique<FabricLinkMonitoringManager>(sw);
  manager->start();
  WITH_RETRIES({
    EXPECT_EVENTUALLY_EQ(
        manager->getPendingSequenceNumbers(downPort).size(), 1);
    EXPECT_EVENTUALLY_EQ(manager->getPendingSequenceNumbers(upPort).size(), 1);
  });

  sw->updateStateBlocking("One fabric port goes down", [&](const auto& in) {
    auto newState = in->clone();
    auto port = newState->getPorts()->getNodeIf(downPort);
    port->modify(&newState)->setOperState(false);
    return newState;
  });

  // Releasing the down port's one packet frees exactly one slot, which the up
  // port takes while still holding its own pending packet.
  WITH_RETRIES({
    EXPECT_EVENTUALLY_TRUE(
        manager->getPendingSequenceNumbers(downPort).empty());
    EXPECT_EVENTUALLY_EQ(manager->getPendingSequenceNumbers(upPort).size(), 2);
  });

  // Had the up port's share been released too, it would keep sending. Let a
  // few more intervals run (the sequence number advances every interval).
  const auto seqNum =
      *manager->getFabricLinkMonPortStats(upPort).sequenceNumber();
  WITH_RETRIES({
    EXPECT_EVENTUALLY_GE(
        *manager->getFabricLinkMonPortStats(upPort).sequenceNumber(),
        seqNum + 5);
  });
  EXPECT_EQ(manager->getPendingSequenceNumbers(upPort).size(), 2);
  EXPECT_EQ(*manager->getFabricLinkMonPortStats(upPort).txCount(), 2);

  manager->stop();
  FabricLinkMonitoringManager::setTestMode(false);
}
