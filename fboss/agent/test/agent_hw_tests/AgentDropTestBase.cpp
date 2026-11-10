// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/agent_hw_tests/AgentDropTestBase.h"

#include "fboss/agent/TxPacket.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/packet/EthHdr.h"
#include "fboss/agent/packet/IPv4Hdr.h"
#include "fboss/agent/packet/IPv6Hdr.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/state/MacTable.h"
#include "fboss/agent/test/EcmpSetupHelper.h"

#include <folly/logging/xlog.h>
#include <gtest/gtest.h>

namespace facebook::fboss {

namespace {
// Small enough that a routed packet with a 1400 byte payload exceeds it.
constexpr int kSmallEgressMtu = 200;
constexpr size_t kOversizedPayloadBytes = 1400;
// Reserved EtherType range is 0x05DD..0x05FF.
constexpr uint16_t kOutOfRangeEtherType = 0x05E0;
const folly::MacAddress kLoopbackDstMac("00:11:22:33:44:55");
} // namespace

const folly::IPAddressV6& AgentDropTestBase::kRoutedDstIp() {
  static const folly::IPAddressV6 ip("2001::1");
  return ip;
}

const folly::IPAddressV6& AgentDropTestBase::kUnroutedDstIp() {
  static const folly::IPAddressV6 ip("2401:db00:dead:beef::1");
  return ip;
}

cfg::SwitchConfig AgentDropTestBase::initialConfig(
    const AgentEnsemble& ensemble) const {
  return utility::onePortPerInterfaceConfig(
      ensemble.getSw(),
      ensemble.masterLogicalPortIds(),
      true /*interfaceHasSubnet*/,
      true /*setInterfaceMac*/,
      utility::kBaseVlanId,
      false /*enableFabricPorts*/,
      interfaceType());
}

PortDescriptor AgentDropTestBase::egressPort() const {
  return PortDescriptor(masterLogicalInterfaceOrHyperPortIds()[0]);
}

PortID AgentDropTestBase::injectionPort() const {
  return PortID(masterLogicalInterfaceOrHyperPortIds()[1]);
}

void AgentDropTestBase::setupRouteToEgressPort() {
  utility::EcmpSetupTargetedPorts6 helper(
      getProgrammedState(), getSw()->needL2EntryForNeighbor());
  applyNewState([&](const std::shared_ptr<SwitchState>& in) {
    return helper.resolveNextHops(in, {egressPort()});
  });
  auto wrapper = getSw()->getRouteUpdater();
  using Prefix = typename Route<folly::IPAddressV6>::Prefix;
  helper.programRoutes(&wrapper, {egressPort()}, {Prefix{kRoutedDstIp(), 128}});
}

void AgentDropTestBase::setupEgressMtuDropScenario() {
  auto config = initialConfig(*getAgentEnsemble());
  for (auto& port : *config.ports()) {
    if (PortID(*port.logicalID()) == egressPort().phyPortID()) {
      port.maxFrameSize() = kSmallEgressMtu;
    }
  }
  applyNewConfig(config);
  setupRouteToEgressPort();
}

void AgentDropTestBase::setupSrcPortKnockoutDropScenario() {
  const auto vlanId = getVlanIDForTx();
  ASSERT_TRUE(vlanId.has_value())
      << "Source-port knockout requires a VLAN interface";
  applyNewState([&](const std::shared_ptr<SwitchState>& in) {
    auto newState = in->clone();
    auto vlan = newState->getVlans()->getNode(*vlanId).get();
    auto macTable = vlan->getMacTable()->modify(&vlan, &newState);
    macTable->addEntry(
        std::make_shared<MacEntry>(
            kLoopbackDstMac,
            egressPort(),
            std::optional<cfg::AclLookupClass>(std::nullopt),
            MacEntryType::STATIC_ENTRY));
    return newState;
  });
}

void AgentDropTestBase::sendSrcPortKnockoutPacket() {
  const folly::MacAddress kSrcMac("00:11:22:33:44:56");
  // Use a non-router destination MAC so the packet is bridged, not routed.
  auto pkt = utility::makeUDPTxPacket(
      getSw(),
      getVlanIDForTx(),
      kSrcMac,
      kLoopbackDstMac,
      folly::IPAddressV6("1001::1"),
      kRoutedDstIp(),
      10000,
      10001);
  getSw()->sendPacketOutOfPortAsync(std::move(pkt), egressPort().phyPortID());
}

void AgentDropTestBase::sendUdpPacket(
    const folly::IPAddressV6& dstIp,
    uint8_t hopLimit,
    const std::optional<PortID>& outPort,
    size_t payloadBytes) {
  auto intfMac = getMacForFirstInterfaceWithPorts(getProgrammedState());
  auto pkt = utility::makeUDPTxPacket(
      getSw(),
      getVlanIDForTx(),
      intfMac,
      intfMac,
      folly::IPAddressV6("1001::1"),
      dstIp,
      10000,
      10001,
      0,
      hopLimit,
      std::vector<uint8_t>(payloadBytes, 0xff));
  getSw()->sendPacketOutOfPortAsync(
      std::move(pkt),
      outPort.value_or(PortID(masterLogicalInterfaceOrHyperPortIds()[0])));
}

void AgentDropTestBase::sendPacketToUnroutedDst() {
  XLOG(DBG2) << "Drop test: sending packet to unrouted dstIp="
             << kUnroutedDstIp().str();
  sendUdpPacket(kUnroutedDstIp(), 64 /*hopLimit*/);
}

void AgentDropTestBase::sendTtlExpiredPacket() {
  XLOG(DBG2) << "Drop test: sending hopLimit=1 packet to dstIp="
             << kRoutedDstIp().str();
  sendUdpPacket(kRoutedDstIp(), 1 /*hopLimit*/);
}

void AgentDropTestBase::sendPacketToRoutedDst() {
  XLOG(DBG2) << "Drop test: sending packet to dstIp=" << kRoutedDstIp().str()
             << " out of injection port " << injectionPort();
  sendUdpPacket(kRoutedDstIp(), 64 /*hopLimit*/, injectionPort());
}

void AgentDropTestBase::sendOversizedPacketToRoutedDst() {
  XLOG(DBG2) << "Drop test: sending oversized (" << kOversizedPayloadBytes
             << "B) packet to dstIp=" << kRoutedDstIp().str()
             << " out of injection port " << injectionPort();
  sendUdpPacket(
      kRoutedDstIp(), 64 /*hopLimit*/, injectionPort(), kOversizedPayloadBytes);
}

template <typename AddrT>
void AgentDropTestBase::sendInvalidIpVersionPacket() {
  auto dstIp =
      AddrT(std::is_same_v<AddrT, folly::IPAddressV4> ? "20.0.0.1" : "2001::1");
  auto srcIp =
      AddrT(std::is_same_v<AddrT, folly::IPAddressV4> ? "10.0.0.1" : "1001::1");
  auto intfMac = getMacForFirstInterfaceWithPorts(getProgrammedState());
  XLOG(DBG2) << "Drop test: sending invalid IP version packet to "
             << dstIp.str();
  auto pkt = utility::makeUDPTxPacket(
      getSw(),
      getVlanIDForTx(),
      intfMac,
      intfMac,
      srcIp,
      dstIp,
      10000,
      10001,
      0,
      64);
  folly::io::Cursor cursor(pkt->buf());
  EthHdr ethHdr(cursor);
  folly::io::RWPrivateCursor rwCursor(pkt->buf());
  rwCursor.skip(ethHdr.size());
  // Keep the EtherType and header layout intact, but use an invalid IP version.
  // Recompute the IPv4 checksum so checksum failure is not a second cause.
  if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
    IPv4Hdr ipHdr(cursor);
    ipHdr.version = 5;
    ipHdr.computeChecksum();
    ipHdr.serialize(&rwCursor);
  } else {
    IPv6Hdr ipHdr(cursor);
    ipHdr.version = 5;
    ipHdr.serialize(&rwCursor);
  }
  getSw()->sendPacketOutOfPortAsync(
      std::move(pkt), PortID(masterLogicalInterfaceOrHyperPortIds()[0]));
}

template void
AgentDropTestBase::sendInvalidIpVersionPacket<folly::IPAddressV4>();
template void
AgentDropTestBase::sendInvalidIpVersionPacket<folly::IPAddressV6>();

void AgentDropTestBase::sendMulticastSmacPacket() {
  const folly::MacAddress kMulticastSmac("01:00:5e:aa:aa:aa");
  auto intfMac = getMacForFirstInterfaceWithPorts(getProgrammedState());
  XLOG(DBG2) << "Drop test: sending eth packet with multicast srcMac="
             << kMulticastSmac;
  auto pkt = utility::makeEthTxPacket(
      getSw(),
      getVlanIDForTx(),
      kMulticastSmac,
      intfMac,
      ETHERTYPE::ETHERTYPE_IPV4);
  getSw()->sendPacketOutOfPortAsync(
      std::move(pkt), PortID(masterLogicalInterfaceOrHyperPortIds()[0]));
}

void AgentDropTestBase::sendOutOfRangeEtherTypePacket() {
  const folly::MacAddress kSrcMac("00:00:00:00:00:01");
  auto intfMac = getMacForFirstInterfaceWithPorts(getProgrammedState());
  XLOG(DBG2) << "Drop test: sending eth packet with out-of-range ethertype 0x"
             << std::hex << kOutOfRangeEtherType;
  auto pkt = utility::makeEthTxPacket(
      getSw(),
      getVlanIDForTx(),
      kSrcMac,
      intfMac,
      static_cast<ETHERTYPE>(kOutOfRangeEtherType));
  getSw()->sendPacketOutOfPortAsync(
      std::move(pkt), PortID(masterLogicalInterfaceOrHyperPortIds()[0]));
}

void AgentDropTestBase::logPortDropCounters(const char* phase) {
  for (auto portId : {egressPort().phyPortID(), injectionPort()}) {
    auto s = getLatestPortStats(portId);
    XLOG(INFO) << "Drop test [" << phase << "] port " << portId
               << ": inUnicastPkts=" << *s.inUnicastPkts_()
               << " outUnicastPkts=" << *s.outUnicastPkts_()
               << " inDiscardsRaw=" << *s.inDiscardsRaw_()
               << " inDiscards=" << *s.inDiscards_()
               << " inDstNullDiscards=" << *s.inDstNullDiscards_()
               << " outDiscards=" << *s.outDiscards_()
               << " inAclDiscards=" << s.inAclDiscards_().value_or(0)
               << " outForwardingDiscards="
               << s.outForwardingDiscards_().value_or(0);
  }
}

void AgentDropTestBase::installLogCapture() {
  for (const auto& switchId : getSw()->getSwitchInfoTable().getSwitchIDs()) {
    getAgentEnsemble()
        ->getHwAgentTestClient(switchId)
        ->sync_installLogCapture();
  }
}

void AgentDropTestBase::verifyDropReasonLogged(
    const std::string& expectedSubstring,
    const char* desc) {
  bool found = false;
  for (const auto& switchId : getSw()->getSwitchInfoTable().getSwitchIDs()) {
    std::vector<std::string> matches;
    getAgentEnsemble()
        ->getHwAgentTestClient(switchId)
        ->sync_getMatchingLogMessages(matches, expectedSubstring);
    if (!matches.empty()) {
      XLOG(DBG2) << desc << ": verified drop reason log: " << matches.front();
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found) << desc << ": expected log containing '"
                     << expectedSubstring << "' not found";
}

} // namespace facebook::fboss
