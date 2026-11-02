// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include <folly/MacAddress.h>
#include <folly/Synchronized.h>
#include <folly/io/async/AsyncTimeout.h>
#include <gflags/gflags.h>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <vector>

#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/types.h"

DECLARE_int32(fabric_link_monitoring_interval_ms);
DECLARE_int32(fabric_link_monitoring_max_outstanding_packets);
DECLARE_int32(fabric_link_monitoring_max_pending_seq_numbers);

namespace folly {
namespace io {
class Cursor;
}
} // namespace folly

namespace facebook::fboss {

class SwSwitch;
class RxPacket;
class TxPacket;
class Port;
class SwitchState;
class AgentStats;

// FabricLinkMonitoringManager sends periodic monitoring packets on fabric ports
// to verify link health and track packet loss
class FabricLinkMonitoringManager : private folly::AsyncTimeout {
 public:
  explicit FabricLinkMonitoringManager(SwSwitch* sw);
  ~FabricLinkMonitoringManager() override;

  // Disable copy and move operations
  FabricLinkMonitoringManager(const FabricLinkMonitoringManager&) = delete;
  FabricLinkMonitoringManager& operator=(const FabricLinkMonitoringManager&) =
      delete;
  FabricLinkMonitoringManager(FabricLinkMonitoringManager&&) = delete;
  FabricLinkMonitoringManager& operator=(FabricLinkMonitoringManager&&) =
      delete;

  // Start sending monitoring packets
  void start();

  // Stop sending monitoring packets
  void stop();

  // Handle received monitoring packet
  void handlePacket(std::unique_ptr<RxPacket> pkt, folly::io::Cursor cursor);

  // Get the payload pattern for a sequence number
  static uint32_t getPayloadPattern(uint64_t sequenceNum);

  // Get statistics for a specific port (for testing)
  FabricLinkMonPortStats getFabricLinkMonPortStats(const PortID& portId) const;

  // Get statistics for all monitored ports (for fb303 export)
  std::map<PortID, FabricLinkMonPortStats> getAllFabricLinkMonPortStats() const;

  void fillAgentFabricLinkMonitoringStats(AgentStats& agentStats) const;

  // Get pending sequence numbers for a specific port (for testing)
  std::vector<uint64_t> getPendingSequenceNumbers(const PortID& portId) const;

  // Test-only methods to enable/check test mode
  static void setTestMode(bool enabled);
  static bool isTestMode();

 private:
  void timeoutExpired() noexcept override;
  void sendPacketsOnAllFabricPorts();
  void sendPacketsForPortGroup(int portGroupId);
  void packetSendAndOutstandingHandling(
      const std::shared_ptr<Port>& port,
      int portGroupId,
      bool shouldSendPacket);
  std::unique_ptr<TxPacket> createMonitoringPacket(
      const PortID& portId,
      uint64_t sequenceNumber);

  // Get port group ID for a given port (ports are grouped by portID % 4)
  int getPortGroup(PortID portId) const;
  size_t getOutstandingPacketCountForGroup(int portGroupId) const;
  void releasePendingPackets(PortID portId, int portGroupId);

  // Per-port-group statistics for flow control
  struct PortGroupStats {
    size_t outstandingPackets{0};
    size_t lastPortIndex{0};
  };

  SwSwitch* sw_;
  std::chrono::milliseconds intervalMsecs_;

  folly::Synchronized<
      std::map<PortID, folly::Synchronized<FabricLinkMonPortStats>>>
      portStats_;
  folly::Synchronized<std::map<PortID, std::deque<uint64_t>>>
      portPendingSequenceNumbers_;
  folly::Synchronized<std::map<int, PortGroupStats>> portGroupStats_;
  std::map<PortID, int> portToGroupMap_;
  std::map<int, std::vector<PortID>> portGroupToPortsMap_;
};

} // namespace facebook::fboss
