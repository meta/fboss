// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/agent/test/AgentHwTest.h"

#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>

namespace facebook::fboss {

// Shared scaffolding for tests that induce a specific packet drop and then
// check how the ASIC reported it.
//
// How a drop is created is ASIC independent, so it lives here and is written
// once. How the drop is *reported* is not: Chenab reports per pipeline stage
// bitmaps and Broadcom XGS reports lists of drop reasons, through different
// SAI mechanisms into different thrift fields. The common test delegates
// collection and assertions to a reporting adapter while retaining the same
// packet scenarios and HwAgent log verification.
class AgentDropTestBase : public AgentHwTest {
 public:
  cfg::SwitchConfig initialConfig(const AgentEnsemble& ensemble) const override;

 protected:
  virtual std::optional<cfg::InterfaceType> interfaceType() const {
    return std::nullopt;
  }

  // Program a route to kRoutedDstIp out of egressPort. Call from setup().
  void setupRouteToEgressPort();
  // As above, and additionally shrink the egress port MTU so that a large
  // routed packet is dropped on egress. Call from setup().
  void setupEgressMtuDropScenario();
  // Resolve a bridged destination back to the ingress port.
  void setupSrcPortKnockoutDropScenario();

  // Each of these sends one packet that should induce the named drop.
  void sendPacketToUnroutedDst();
  void sendTtlExpiredPacket();
  void sendPacketToRoutedDst();
  void sendOversizedPacketToRoutedDst();
  void sendSrcPortKnockoutPacket();
  template <typename AddrT>
  void sendInvalidIpVersionPacket();
  void sendMulticastSmacPacket();
  void sendOutOfRangeEtherTypePacket();

  // The destination sendPacketToRoutedDst() uses. Exposed so that a fixture
  // can write config, such as an ACL match, that has to line up exactly with
  // the packet it wants to be the only thing acting on.
  static const folly::IPAddressV6& kRoutedDstIp();

  // Dump the discard counters for the ports these scenarios use. When a drop
  // reason test sees nothing reported, the port counters are the only thing
  // that separates "the packet was never dropped" from "it was dropped but
  // not reported", so log them either way.
  void logPortDropCounters(const char* phase);

  // Install a log capture handler in the HwAgent process, where the drop
  // reason logging is emitted. Works in both mono (in process hw agent
  // server) and multi switch (remote HwAgent). Call in verify() before the
  // drop is triggered.
  void installLogCapture();
  // Read captured logs back from the HwAgent over RPC, so this works in
  // multi switch where the log is emitted in a separate process.
  void verifyDropReasonLogged(
      const std::string& expectedSubstring,
      const char* desc);

 private:
  // Deliberately outside any configured interface subnet, so no route matches.
  static const folly::IPAddressV6& kUnroutedDstIp();
  PortDescriptor egressPort() const;
  PortID injectionPort() const;
  void sendUdpPacket(
      const folly::IPAddressV6& dstIp,
      uint8_t hopLimit,
      const std::optional<PortID>& outPort = std::nullopt,
      size_t payloadBytes = 0);
};

} // namespace facebook::fboss
