// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <folly/io/Cursor.h>
#include <gtest/gtest.h>

#include <vector>

#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/EthFrame.h"
#include "fboss/agent/packet/EthHdr.h"
#include "fboss/agent/packet/Ethertype.h"
#include "fboss/agent/packet/IPProto.h"
#include "fboss/agent/packet/IPv6Hdr.h"
#include "fboss/agent/packet/UDPHeader.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/test/utils/TrapPacketUtils.h"
#include "fboss/lib/CommonUtils.h"

namespace facebook::fboss {

namespace {

constexpr uint8_t kAbwcSignalType = 1;
// Probe tag: ABWC bucket 7 with the valid bit (0x10) set, link locator 0x5.
constexpr uint8_t kProbeSignal = 0x17;
constexpr uint8_t kProbeLinkLocator = 0x5;
// Link locator programmed on the egress port, stamped by UPDATE.
constexpr uint8_t kEgressLinkLocator = 0x42;
// UPDATE on an idle egress link rewrites the signal to ABWC bucket 0.
constexpr uint8_t kIdleUpdateSignal = 0x10;
constexpr uint8_t kProbeHopLimit = 64;
constexpr size_t kProbePayloadSize = 50;

const folly::IPAddressV6 kProbeSrcIp{"1::10"};

// A copy of the probe the ASIC routed to dstIp (hop limit decremented). The
// egress tag action applies only to routed packets, so bridged copies don't
// count.
bool isRoutedProbeCopy(
    const utility::EthFrame& frame,
    const folly::IPAddressV6& dstIp) {
  const auto& v6 = frame.v6PayLoad();
  return v6.has_value() && v6->header().dstAddr == dstIp &&
      v6->header().hopLimit < kProbeHopLimit;
}

} // namespace

/*
 * A CSIG-tagged IPv6 probe is injected into the pipeline from the CPU and
 * routed to a neighbor on a MAC-loopback egress port. The neighbor MAC is the
 * router MAC, so the frame egresses (where the port's CSIG tag action
 * applies), loops back in, and an ingress ACL on the egress port copies it to
 * the CPU for inspection.
 */
class AgentCsigTest : public AgentHwTest {
 protected:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::L3_FORWARDING};
  }

  bool isCsigSupported() const {
    return isSupportedOnAllAsics(HwAsic::Feature::CSIG);
  }

  PortID egressPort() const {
    return masterLogicalInterfacePortIds()[0];
  }

  utility::EcmpSetupTargetedPorts6 makeEcmpHelper() {
    return utility::EcmpSetupTargetedPorts6(
        getProgrammedState(),
        getSw()->needL2EntryForNeighbor(),
        getMacForFirstInterfaceWithPorts(getProgrammedState()));
  }

  folly::IPAddressV6 probeDstIp() {
    return makeEcmpHelper().ip(PortDescriptor(egressPort()));
  }

  void setupCsig(
      cfg::CsigTagAction tagAction,
      std::optional<uint8_t> linkLocator = std::nullopt) {
    auto config = initialConfig(*getAgentEnsemble());
    auto portCfg = utility::findCfgPort(config, egressPort());
    portCfg->csigTagAction() = tagAction;
    if (linkLocator.has_value()) {
      portCfg->csigLinkLocator() = *linkLocator;
    }
    utility::addTrapPacketAcl(
        checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics()),
        &config,
        egressPort());
    applyNewConfig(config);

    auto ecmpHelper = makeEcmpHelper();
    applyNewState(
        [&](const std::shared_ptr<SwitchState>& in) {
          return ecmpHelper.resolveNextHops(in, {PortDescriptor(egressPort())});
        },
        "resolve CSIG probe neighbor");
  }

  utility::EthFrame makeCsigProbe(const folly::IPAddressV6& dstIp) {
    const auto intfMac = getMacForFirstInterfaceWithPorts(getProgrammedState());
    EthHdr ethHdr(
        intfMac, intfMac, {}, static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6));
    ethHdr.setCsigTag(
        CsigTag(kAbwcSignalType, kProbeSignal, kProbeLinkLocator));
    IPv6Hdr ipHdr(kProbeSrcIp, dstIp);
    ipHdr.setProtocol(static_cast<uint8_t>(IP_PROTO::IP_PROTO_UDP));
    ipHdr.hopLimit = kProbeHopLimit;
    UDPHeader udpHdr;
    udpHdr.srcPort = 8000;
    udpHdr.dstPort = 8001;
    return utility::EthFrame(
        ethHdr,
        utility::IPPacket<folly::IPAddressV6>(
            ipHdr,
            utility::UDPDatagram(
                udpHdr, std::vector<uint8_t>(kProbePayloadSize, 0))));
  }

  // Sends a probe and returns the routed copies that loop back on the egress
  // port.
  std::vector<utility::EthFrame> sendProbeAndCollectRoutedCopies() {
    const auto dstIp = probeDstIp();
    const auto probe = makeCsigProbe(dstIp);
    utility::SwSwitchPacketSnooper snooper(
        getSw(), "csigProbeSnooper", egressPort());
    // A probe loops on the MAC-loopback egress port until its hop limit
    // expires, so most captured frames go unclaimed.
    snooper.ignoreUnclaimedRxPkts();
    std::vector<utility::EthFrame> copies;
    WITH_RETRIES({
      sendPacketSwitchedAsync(probe.getTxPacket(
          [sw = getSw()](uint32_t size) { return sw->allocatePacket(size); }));
      while (auto buf = snooper.waitForPacket(1 /* timeout_s */)) {
        folly::io::Cursor cursor(buf->get());
        utility::EthFrame frame(cursor);
        if (isRoutedProbeCopy(frame, dstIp)) {
          copies.push_back(std::move(frame));
        }
      }
      EXPECT_EVENTUALLY_FALSE(copies.empty());
    });
    EXPECT_FALSE(copies.empty()) << "No routed copy of the probe to " << dstIp
                                 << " looped back on port " << egressPort();
    return copies;
  }

  void verifyEgressCsigTag(uint8_t expectedSignal, uint8_t expectedLocator) {
    for (const auto& copy : sendProbeAndCollectRoutedCopies()) {
      const auto csigTag = copy.header().getCsigTag();
      ASSERT_TRUE(csigTag.has_value()) << copy;
      EXPECT_EQ(csigTag->signalType(), kAbwcSignalType) << copy;
      EXPECT_EQ(csigTag->signal(), expectedSignal) << copy;
      EXPECT_EQ(csigTag->linkLocator(), expectedLocator) << copy;
    }
  }

  void verifyEgressCsigTagStripped() {
    for (const auto& copy : sendProbeAndCollectRoutedCopies()) {
      EXPECT_FALSE(copy.header().getCsigTag().has_value()) << copy;
    }
  }
};

// STRIP egress removes the tag. The SDK default is PASSTHROUGH.
TEST_F(AgentCsigTest, strip) {
  if (!isCsigSupported()) {
    GTEST_SKIP() << "CSIG not supported";
  }
  auto setup = [this]() { setupCsig(cfg::CsigTagAction::STRIP); };
  auto verify = [this]() { verifyEgressCsigTagStripped(); };
  verifyAcrossWarmBoots(setup, verify);
}

// UPDATE egress rewrites the signal with the egress link's available bandwidth
// and stamps the egress port's link locator.
TEST_F(AgentCsigTest, update) {
  if (!isCsigSupported()) {
    GTEST_SKIP() << "CSIG not supported";
  }
  auto setup = [this]() {
    setupCsig(cfg::CsigTagAction::UPDATE, kEgressLinkLocator);
  };
  auto verify = [this]() {
    verifyEgressCsigTag(kIdleUpdateSignal, kEgressLinkLocator);
  };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
