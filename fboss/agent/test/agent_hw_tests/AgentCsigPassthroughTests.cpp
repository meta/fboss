// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <fmt/core.h>
#include <folly/io/Cursor.h>
#include <gtest/gtest.h>

#include "fboss/agent/FbossError.h"
#include "fboss/agent/packet/EthFrame.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"

namespace facebook::fboss {
namespace {

constexpr uint16_t kCsigTag = 0x0f80;
constexpr uint8_t kHopLimit = 255;
const folly::IPAddress kSrcIp4{"198.51.100.10"};
const folly::IPAddress kDstIp4{"203.0.113.10"};
const folly::IPAddress kSrcIp6{"1::10"};
const folly::IPAddress kDstIp6{"2401:db00:231c:7:face:0:3ed:0"};

constexpr auto kOpaqueTagConfig = R"(
cint_reset();
int csig_passthrough_setup() {{
  bcm_switch_opaque_tag_params_t params;
  bcm_switch_opaque_tag_config_t_init(&params);
  params.ethertype = {:#x};
  params.tag_size = BCM_SWITCH_OPAQUE_TAG_SIZE_4_BYTES;
  params.tag_type = 1;
  params.valid = 1;
  return bcm_switch_opaque_tag_config_set(0, 0, &params);
}}
printf("CSIG_SETUP_RV=%d\n", csig_passthrough_setup());
)";

constexpr auto kTh5Th6OpaqueTagConfig = R"(
cint_reset();
int csig_passthrough_setup() {{
  int rv;
  bcm_switch_opaque_tag_params_t params;
  bcm_switch_opaque_tag_config_t_init(&params);
  params.ethertype = {:#x};
  params.tag_size = BCM_SWITCH_OPAQUE_TAG_SIZE_4_BYTES;
  params.tag_type = 1;
  params.valid = 1;
  rv = bcm_switch_opaque_tag_config_set(0, 0, &params);
  if (rv != BCM_E_NONE) {{
    return rv;
  }}
  return bcm_switch_payload_opaque_tag_config_set(0, 0, &params);
}}
printf("CSIG_SETUP_RV=%d\n", csig_passthrough_setup());
)";

} // namespace

class AgentCsigPassthroughTest : public AgentHwTest {
 protected:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::L3_FORWARDING,
        ProductionFeature::COPP,
        ProductionFeature::CSIG_PASSTHROUGH_SUPPORT};
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    const auto port = ensemble.masterLogicalInterfacePortIds(
        getCurrentSwitchIdForTesting())[0];
    auto config = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        {port},
        true,
        true,
        utility::kBaseVlanId,
        false,
        cfg::InterfaceType::VLAN);
    utility::setDefaultCpuTrafficPolicyConfig(
        config, ensemble.getL3Asics(), ensemble.isSai());
    utility::addCpuQueueConfig(config, ensemble.getL3Asics(), ensemble.isSai());
    return config;
  }

  PortID loopPort() const {
    return masterLogicalInterfacePortIds(getCurrentSwitchIdForTesting())[0];
  }

  void setupLoop() {
    const auto port = loopPort();
    const auto intfMac = getMacForFirstInterfaceWithPorts(getProgrammedState());
    utility::EcmpSetupTargetedPorts4 helper4(
        getProgrammedState(), getSw()->needL2EntryForNeighbor(), intfMac);
    utility::EcmpSetupTargetedPorts6 helper6(
        getProgrammedState(), getSw()->needL2EntryForNeighbor(), intfMac);
    applyNewState([&](const std::shared_ptr<SwitchState>& state) {
      return helper6.resolveNextHops(
          helper4.resolveNextHops(state, {PortDescriptor(port)}),
          {PortDescriptor(port)});
    });
    auto updater = getSw()->getRouteUpdater();
    helper4.programRoutes(
        &updater, {PortDescriptor(port)}, {{kDstIp4.asV4(), 32}});
    helper6.programRoutes(
        &updater, {PortDescriptor(port)}, {{kDstIp6.asV6(), 128}});
  }

  utility::EthFrame makeProbe(bool isV6) {
    const auto intfMac = getMacForFirstInterfaceWithPorts(getProgrammedState());
    auto packet = utility::makeUDPTxPacket(
        getSw(),
        std::nullopt,
        intfMac,
        intfMac,
        isV6 ? kSrcIp6 : kSrcIp4,
        isV6 ? kDstIp6 : kDstIp4,
        8000,
        8001,
        0,
        kHopLimit,
        std::vector<uint8_t>(50, 0xa5));
    folly::io::Cursor cursor(packet->buf());
    utility::EthFrame frame(cursor);
    auto header = frame.header();
    header.setCsigTag(CsigTag(kCsigTag));
    if (isV6) {
      const auto ipv6 = frame.v6PayLoad();
      if (!ipv6) {
        throw FbossError("CSIG probe must contain an IPv6 payload");
      }
      return utility::EthFrame(header, *ipv6);
    }
    const auto ipv4 = frame.v4PayLoad();
    if (!ipv4) {
      throw FbossError("CSIG probe must contain an IPv4 payload");
    }
    return utility::EthFrame(header, *ipv4);
  }

  void verifyReceivedProbe(utility::SwSwitchPacketSnooper& snooper, bool isV6) {
    SCOPED_TRACE(isV6 ? "IPv6" : "IPv4");
    auto received = snooper.waitForPacket(10);
    ASSERT_TRUE(received.has_value()) << "No CSIG probe returned at TTL expiry";
    folly::io::Cursor cursor(received->get());
    utility::EthFrame frame(cursor);
    // SAI_HOSTIF_TRAP_TYPE_TTL_ERROR traps before the final decrement.
    if (isV6) {
      ASSERT_TRUE(frame.v6PayLoad().has_value());
      EXPECT_EQ(frame.v6PayLoad()->header().hopLimit, 1);
    } else {
      ASSERT_TRUE(frame.v4PayLoad().has_value());
      EXPECT_EQ(frame.v4PayLoad()->header().ttl, 1);
    }
    const auto header = frame.header();
    ASSERT_TRUE(header.getCsigTag().has_value()) << frame;
    EXPECT_EQ(header.getCsigTag()->value, kCsigTag) << frame;
  }

  void verifyProbes() {
    const auto probe4 = makeProbe(false);
    const auto probe6 = makeProbe(true);
    const auto matchesProbe = [](const utility::EthFrame& sent,
                                 const utility::EthFrame& received) {
      const auto matchesIp = [](const auto& sentIp, const auto& receivedIp) {
        return receivedIp && sentIp &&
            receivedIp->header().srcAddr == sentIp->header().srcAddr &&
            receivedIp->header().dstAddr == sentIp->header().dstAddr &&
            receivedIp->udpPayload() == sentIp->udpPayload();
      };
      return matchesIp(sent.v4PayLoad(), received.v4PayLoad()) ||
          matchesIp(sent.v6PayLoad(), received.v6PayLoad());
    };
    utility::SwSwitchPacketSnooper snooper4(
        getSw(), "csigPassthroughV4", std::nullopt, probe4, matchesProbe);
    utility::SwSwitchPacketSnooper snooper6(
        getSw(), "csigPassthroughV6", std::nullopt, probe6, matchesProbe);
    const auto port = loopPort();
    const auto statsBefore = getLatestPortStats(port);
    ASSERT_TRUE(
        getSw()->sendPacketOutOfPortAsync(
            probe4.getTxPacket(utility::makeAllocator(getSw())), port));
    ASSERT_TRUE(
        getSw()->sendPacketOutOfPortAsync(
            probe6.getTxPacket(utility::makeAllocator(getSw())), port));
    verifyReceivedProbe(snooper4, false);
    verifyReceivedProbe(snooper6, true);
    WITH_RETRIES({
      const auto statsAfter = getLatestPortStats(port);
      EXPECT_EVENTUALLY_GE(
          *statsAfter.outUnicastPkts_() - *statsBefore.outUnicastPkts_(),
          2 * kHopLimit);
    });
  }

  void verifyPassthrough() {
    const auto* asic = hwAsicForPort(loopPort());
    if (asic->getAsicVendor() != HwAsic::AsicVendor::ASIC_VENDOR_BCM) {
      throw FbossError("CSIG passthrough CINT setup requires a Broadcom ASIC");
    }
    const auto asicType = asic->getAsicType();
    constexpr auto ethertype = static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_CSIG);
    const auto config = (asicType == cfg::AsicType::ASIC_TYPE_TOMAHAWK5 ||
                         asicType == cfg::AsicType::ASIC_TYPE_TOMAHAWK6)
        ? fmt::format(kTh5Th6OpaqueTagConfig, ethertype)
        : fmt::format(kOpaqueTagConfig, ethertype);
    std::string output;
    getAgentEnsemble()->runCint(config, output, switchIdForPort(loopPort()));
    ASSERT_NE(output.find("CSIG_SETUP_RV=0"), std::string::npos) << output;

    verifyProbes();
  }
};

TEST_F(AgentCsigPassthroughTest, routedLoop) {
  verifyAcrossWarmBoots(
      [this]() { setupLoop(); }, [this]() { verifyPassthrough(); });
}

} // namespace facebook::fboss
