/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <folly/IPAddress.h>
#include <folly/MacAddress.h>

#include "fboss/agent/test/AgentHwTest.h"

namespace facebook::fboss {

class TxPacket;

// Base class for Mirror on Drop tests.
// Flow of the test:
// 1. Configure MirrorOnDrop: add a MirrorOnDropReport to SwitchConfig.
// 2. Pick an interface port and add a route to the MirrorOnDrop collector IP.
// 3. Trigger some packet drops.
// 4. A MirrorOnDrop packet is generated and forwarded toward the collector.
// 5. The MirrorOnDrop packet is trapped to the CPU by an ACL that matches
//    collector MAC.
// 6. We capture the packet using PacketSnooper and verify its contents.
// Note: The specific MirrorOnDrop mechanism (e.g. recirculation port on DNX,
// tunnel on XGS) is platform-dependent and is handled by platform-specific
// subclasses.
class AgentMirrorOnDropTestBase : public AgentHwTest {
 public:
  cfg::SwitchConfig initialConfig(const AgentEnsemble& ensemble) const override;

  std::vector<ProductionFeature> getProductionFeaturesVerified() const override;

 protected:
  static constexpr auto kDefaultGlobalSharedBytes{2'000'000};

  // Packet to be dropped
  inline static const folly::IPAddressV6 kDropDestIp{
      "2401:3333:3333:3333:3333:3333:3333:3333"};

  // MOD/collector addresses
  inline static const folly::MacAddress kCollectorNextHopMac_{
      "02:88:88:88:88:88"};
  inline static const folly::IPAddressV6 kCollectorIp_{
      "2401:9999:9999:9999:9999:9999:9999:9999"};
  inline static const folly::IPAddressV6 kSwitchIp_{"2401:face:b00c::01"};
  static constexpr int16_t kMirrorSrcPort = 0x6666;
  static constexpr int16_t kMirrorDstPort = 0x7777;

  // Packet fields used in makePacket() — used to verify inner packet in MoD
  inline static const folly::IPAddressV6 kPacketSrcIp_{
      "2401:2222:2222:2222:2222:2222:2222:2222"};
  static constexpr uint16_t kPacketSrcPort = 0x4444;
  static constexpr uint16_t kPacketDstPort = 0x5555;

  std::string portDesc(const PortID& portId) const;

  void setupEcmpTraffic(
      const PortID& portId,
      const folly::IPAddressV6& addr,
      const folly::MacAddress& nextHopMac,
      bool disableTtlDecrement = false);

  void unprogramEcmpTraffic(const folly::IPAddressV6& addr);

  void unresolveEcmpTraffic(const PortID& portId);

  void addDropPacketAcl(cfg::SwitchConfig* config, const PortID& portId);

  void configureMmuDropBuffers(
      cfg::SwitchConfig& config,
      const PortID& injectionPortId,
      int priority);

  void applyMirrorOnDropConfig(
      cfg::SwitchConfig& config,
      cfg::MirrorOnDropReport report,
      const PortID& collectorPort);

  void resetMirrorOnDropFlows();

  // Create a packet that will be dropped. The entire packet is expected to be
  // copied to the MoD payload. However note that if the test involves looping
  // traffic before they are dropped (e.g. by triggering buffer exhaustion), the
  // src/dst MAC defined here may be different from the MoD payload.
  std::unique_ptr<TxPacket> makePacket(
      const folly::IPAddressV6& dstIp,
      int priority = 0,
      size_t payloadSize = 512);

  std::unique_ptr<TxPacket> sendPackets(
      int count,
      std::optional<PortID> portId,
      const folly::IPAddressV6& dstIp,
      int priority = 0,
      size_t payloadSize = 512);

  SystemPortID systemPortId(const PortID& portId);
};

} // namespace facebook::fboss
