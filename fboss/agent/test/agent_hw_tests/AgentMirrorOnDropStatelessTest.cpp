// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/agent_hw_tests/AgentMirrorOnDropStatelessTest.h"

#include <gtest/gtest.h>

#include <folly/ScopeGuard.h>
#include <folly/logging/xlog.h>

#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/agent_hw_tests/AgentMirrorOnDropTajoImpl.h"
#include "fboss/agent/test/agent_hw_tests/AgentMirrorOnDropXgsImpl.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/test/utils/PortTestUtils.h"
#include "fboss/lib/CommonUtils.h"

namespace facebook::fboss {

namespace {

// Pack an ingress-pipeline drop code (default-route, ACL) into the wire
// format's two-slot drop-reason struct.
MirrorOnDropDropReasonCodes ingressOnly(uint16_t code) {
  return {.ingressDropReason = code, .egressDropReason = 0};
}

// Pack an MMU/egress drop code into the wire format's two-slot drop-reason
// struct.
MirrorOnDropDropReasonCodes egressOnly(uint16_t code) {
  return {.ingressDropReason = 0, .egressDropReason = code};
}

} // namespace

using namespace ::testing;

// Factory: ONE switch — adding a new ASIC means adding one case here.
std::unique_ptr<MirrorOnDropImpl> createMirrorOnDropImpl(cfg::AsicType type) {
  // NOLINTNEXTLINE(clang-diagnostic-switch-enum): default handles all others.
  switch (type) {
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK5:
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK6:
      return std::make_unique<XgsMirrorOnDropImpl>(type);
    case cfg::AsicType::ASIC_TYPE_YUBA: // gibraltar
    case cfg::AsicType::ASIC_TYPE_G202X: // graphene200
      return std::make_unique<TajoMirrorOnDropImpl>();
    default:
      throw FbossError(
          "createMirrorOnDropImpl: no MirrorOnDropImpl for AsicType ",
          static_cast<int>(type));
  }
}

MirrorOnDropImpl* AgentMirrorOnDropStatelessTest::impl() {
  if (!impl_) {
    auto* asic =
        checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
    impl_ = createMirrorOnDropImpl(asic->getAsicType());
  }
  return impl_.get();
}

std::vector<ProductionFeature>
AgentMirrorOnDropStatelessTest::getProductionFeaturesVerified() const {
  // The umbrella gate for any ASIC with a stateless MirrorOnDrop impl. We do
  // NOT call impl()->getProductionFeature() here: gtest invokes this during
  // test discovery on every platform, and the factory throws FbossError on
  // unsupported AsicTypes — calling it here would crash the test runner on
  // every non-stateless ASIC.
  return {
      ProductionFeature::MIRROR_ON_DROP,
      ProductionFeature::MIRROR_ON_DROP_STATELESS};
}

cfg::MirrorOnDropReport AgentMirrorOnDropStatelessTest::makeMirrorOnDropReport(
    const std::string& name,
    std::optional<int32_t> samplingRate) {
  return impl()->makeReport(
      name,
      kCollectorIp_,
      kMirrorDstPort,
      kMirrorSrcPort,
      kSwitchIp_,
      samplingRate);
}

MirrorOnDropPacketFields
AgentMirrorOnDropStatelessTest::parseMirrorOnDropPacket(
    const folly::IOBuf* buf) {
  return impl()->parsePacket(buf);
}

void AgentMirrorOnDropStatelessTest::verifyAsicSpecificInvariants(
    const folly::IOBuf* buf) {
  impl()->verifyInvariants(buf);
}

MirrorOnDropDropReasonCodes
AgentMirrorOnDropStatelessTest::getDefaultRouteDropReasons() {
  return ingressOnly(impl()->getDefaultRouteDropReason());
}

MirrorOnDropDropReasonCodes
AgentMirrorOnDropStatelessTest::getAclDropReasons() {
  return ingressOnly(impl()->getAclDropReason());
}

MirrorOnDropDropReasonCodes
AgentMirrorOnDropStatelessTest::getMmuDropReasons() {
  return egressOnly(impl()->getMmuDropReason());
}

MirrorOnDropDropReasonCodes
AgentMirrorOnDropStatelessTest::getSrv6MidpointIsLastSidDropReason() {
  return ingressOnly(impl()->getSrv6MidpointIsLastSidDropReason());
}

MirrorOnDropDropReasonCodes
AgentMirrorOnDropStatelessTest::getSrv6DecapNonLastSegmentDropReasons() {
  return ingressOnly(impl()->getSrv6DecapNonLastSegmentDropReason());
}

MirrorOnDropDropReasonCodes
AgentMirrorOnDropStatelessTest::getSrv6BindingSidNonLastSidDropReasons() {
  return ingressOnly(impl()->getSrv6BindingSidNonLastSidDropReason());
}

MirrorOnDropDropReasonCodes
AgentMirrorOnDropStatelessTest::getSrv6MidpointUnresolvedDropReasons() {
  return ingressOnly(impl()->getSrv6MidpointUnresolvedDropReason());
}

MirrorOnDropDropReasonCodes
AgentMirrorOnDropStatelessTest::getSrv6EncapMtuExceededDropReasons() {
  return egressOnly(impl()->getSrv6EncapMtuExceededDropReason());
}

void AgentMirrorOnDropStatelessTest::configureErspanMirror(
    cfg::SwitchConfig& config,
    const std::string& mirrorName,
    const folly::IPAddressV6& tunnelDstIp,
    const folly::IPAddressV6& tunnelSrcIp,
    const PortID& srcPortId) {
  impl()->configureErspanMirror(
      config, mirrorName, tunnelDstIp, tunnelSrcIp, srcPortId);
}

uint32_t AgentMirrorOnDropStatelessTest::getExpectedModPacketIngressPortId(
    PortID port) const {
  // Tajo exports FBOSS port IDs; XGS exports SDK logical port IDs.
  if (hwAsicForPort(port)->getAsicVendor() ==
      HwAsic::AsicVendor::ASIC_VENDOR_TAJO) {
    return static_cast<uint32_t>(port);
  }
  const auto hwLogicalPortId = getSw()->getHwLogicalPortId(port);
  if (!hwLogicalPortId.has_value()) {
    throw FbossError("Missing hardware logical port ID for port ", port);
  }
  return *hwLogicalPortId;
}

void AgentMirrorOnDropStatelessTest::validateMirrorOnDropPacket(
    const folly::IOBuf* captured,
    const PortID& injectionPortId,
    const MirrorOnDropDropReasonCodes& expectedReasons,
    std::optional<folly::IPAddressV6> expectedInnerDstIp,
    std::optional<folly::IPAddressV6> expectedInnerSrcIp) {
  verifyAsicSpecificInvariants(captured);
  auto fields = parseMirrorOnDropPacket(captured);

  // Tajo routes the MoD export out a front-panel port, so its outer source MAC
  // is the egress router-interface MAC (my_mac), not the CPU/local MAC that
  // XGS's tunnel uses.
  const bool isTajoImpl =
      dynamic_cast<TajoMirrorOnDropImpl*>(impl()) != nullptr;
  const folly::MacAddress expectedOuterSrcMac = isTajoImpl
      ? getMacForFirstInterfaceWithPortsForTesting(getProgrammedState())
      : getLocalMacAddress();
  EXPECT_EQ(fields.outerSrcMac, expectedOuterSrcMac);
  EXPECT_EQ(fields.outerDstMac, kCollectorNextHopMac_);
  EXPECT_EQ(fields.outerSrcIp, kSwitchIp_);
  EXPECT_EQ(fields.outerDstIp, kCollectorIp_);
  EXPECT_EQ(fields.outerDstPort, static_cast<uint16_t>(kMirrorDstPort));
  EXPECT_EQ(
      fields.ingressPort, getExpectedModPacketIngressPortId(injectionPortId));
  EXPECT_EQ(fields.dropReasonIngress, expectedReasons.ingressDropReason);
  EXPECT_EQ(fields.dropReasonEgress, expectedReasons.egressDropReason);
  EXPECT_EQ(fields.innerSrcIp, expectedInnerSrcIp.value_or(kPacketSrcIp_));

  if (expectedInnerDstIp.has_value()) {
    EXPECT_EQ(fields.innerDstIp, expectedInnerDstIp.value());
  }
}

void AgentMirrorOnDropStatelessTest::waitForStatsToStabilize(
    const std::vector<PortID>& ports) {
  constexpr int kStableIterations = 3;
  int stableCount = 0;
  // Compare against stats from the previous WITH_RETRIES iteration, not the
  // moment before WITH_RETRIES started — back-to-back samples with no time
  // gap can spuriously appear stable even under active traffic.
  std::optional<std::map<PortID, HwPortStats>> prevStats;
  WITH_RETRIES({
    auto curStats = getLatestPortStats(ports);
    if (prevStats.has_value()) {
      const bool unchanged =
          std::all_of(ports.begin(), ports.end(), [&](const auto& portId) {
            // .at() throws on missing port — operator[] would silently
            // default-construct HwPortStats and crash on the dereference of
            // an empty optional outUnicastPkts_(), masking the real cause.
            return *curStats.at(portId).outUnicastPkts_() ==
                *prevStats.value().at(portId).outUnicastPkts_();
          });
      stableCount = unchanged ? stableCount + 1 : 0;
    }
    prevStats = std::move(curStats);
    EXPECT_EVENTUALLY_GE(stableCount, kStableIterations);
  });
}

void AgentMirrorOnDropStatelessTest::testDefaultRouteDrop() {
  PortID injectionPortId = masterLogicalInterfacePortIds()[0];
  PortID collectorPortId = masterLogicalInterfacePortIds()[1];

  auto setup = [&]() {
    auto config = getAgentEnsemble()->getCurrentConfig();
    applyMirrorOnDropConfig(
        config,
        makeMirrorOnDropReport("mod-default-route-drop"),
        collectorPortId);
  };

  auto verify = [&]() {
    utility::SwSwitchPacketSnooper snooper(
        getSw(),
        "mod-snooper",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        impl()->snooperReceivePacketType());
    snooper.ignoreUnclaimedRxPkts();
    sendPackets(1, injectionPortId, kDropDestIp);

    WITH_RETRIES_N(5, {
      auto frameRx = snooper.waitForPacket(1);
      EXPECT_EVENTUALLY_TRUE(frameRx.has_value());
      if (frameRx.has_value()) {
        XLOG(INFO) << "Captured MirrorOnDrop packet for default-route drop:\n"
                   << PktUtil::hexDump(frameRx->get());
        validateMirrorOnDropPacket(
            frameRx->get(),
            injectionPortId,
            getDefaultRouteDropReasons(),
            kDropDestIp);
      }
    });
  };

  verifyAcrossWarmBoots(setup, verify);
}

void AgentMirrorOnDropStatelessTest::testAclDrop() {
  PortID injectionPortId = masterLogicalInterfacePortIds()[0];
  PortID collectorPortId = masterLogicalInterfacePortIds()[1];
  const folly::IPAddressV6 kAclDropDestIp{"2401:db00:e112:9100:1006::1"};

  auto setup = [&]() {
    auto config = getAgentEnsemble()->getCurrentConfig();
    cfg::AclEntry aclEntry;
    aclEntry.name() = "acl-drop-by-dstip";
    aclEntry.dstIp() = fmt::format("{}/128", kAclDropDestIp.str());
    aclEntry.actionType() = cfg::AclActionType::DENY;
    utility::addAclEntry(&config, aclEntry, utility::kDefaultAclTable());

    applyMirrorOnDropConfig(
        config, makeMirrorOnDropReport("mod-acl-drop"), collectorPortId);
  };

  auto verify = [&]() {
    utility::SwSwitchPacketSnooper snooper(
        getSw(),
        "mod-acl-snooper",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        impl()->snooperReceivePacketType());
    snooper.ignoreUnclaimedRxPkts();
    auto pkt = sendPackets(1, injectionPortId, kAclDropDestIp);
    XLOG(INFO) << "Sent packet to trigger ACL drop:\n"
               << PktUtil::hexDump(pkt->buf());

    WITH_RETRIES_N(5, {
      auto frameRx = snooper.waitForPacket(1);
      EXPECT_EVENTUALLY_TRUE(frameRx.has_value());
      if (frameRx.has_value()) {
        XLOG(INFO) << "Captured MirrorOnDrop packet for ACL drop:\n"
                   << PktUtil::hexDump(frameRx->get());
        validateMirrorOnDropPacket(
            frameRx->get(),
            injectionPortId,
            getAclDropReasons(),
            kAclDropDestIp);
      }
    });
  };

  verifyAcrossWarmBoots(setup, verify);
}

void AgentMirrorOnDropStatelessTest::testMmuDrop() {
  PortID injectionPortId = masterLogicalInterfacePortIds()[0];
  PortID collectorPortId = masterLogicalInterfacePortIds()[1];
  PortID txOffPortId = masterLogicalInterfacePortIds()[2];
  // constexpr (not just const) so Infer doesn't flag the lambda capture as a
  // dead store: the value is purely compile-time and needs no storage.
  constexpr int kPriority = 2;

  auto setup = [&]() {
    auto config = getAgentEnsemble()->getCurrentConfig();
    configureMmuDropBuffers(config, injectionPortId, kPriority);
    applyMirrorOnDropConfig(
        config, makeMirrorOnDropReport("mod-mmu-drop"), collectorPortId);
    setupEcmpTraffic(
        txOffPortId,
        kDropDestIp,
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState()));
    waitForStateUpdates(getSw());
  };

  auto verify = [&]() {
    utility::setCreditWatchdogAndPortTx(getAgentEnsemble(), txOffPortId, false);
    // Ensure TX is restored even if an assertion fails or an exception is
    // thrown below. Otherwise the port stays in TX-off across the warmboot
    // verify iteration and across subsequent tests.
    SCOPE_EXIT {
      utility::setCreditWatchdogAndPortTx(
          getAgentEnsemble(), txOffPortId, true);
    };

    utility::SwSwitchPacketSnooper snooper(
        getSw(),
        "mod-mmu-snooper",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        impl()->snooperReceivePacketType());
    snooper.ignoreUnclaimedRxPkts();

    // Snapshot inCongestionDiscards before sending. The counter is monotonic
    // across the warmboot iteration, so checking > 0 alone would pass on the
    // post-warmboot pass even if no new drops occurred.
    const int64_t discardsBefore =
        *getLatestPortStats(injectionPortId).inCongestionDiscards_();

    sendPackets(10000, injectionPortId, kDropDestIp, kPriority);

    WITH_RETRIES({
      auto portStats = getLatestPortStats(injectionPortId);
      EXPECT_EVENTUALLY_GT(*portStats.inCongestionDiscards_(), discardsBefore);
    });

    WITH_RETRIES_N(5, {
      auto frameRx = snooper.waitForPacket(1);
      EXPECT_EVENTUALLY_TRUE(frameRx.has_value());
      if (frameRx.has_value()) {
        XLOG(INFO) << "Captured MirrorOnDrop packet for MMU drop:\n"
                   << PktUtil::hexDump(frameRx->get());
        validateMirrorOnDropPacket(
            frameRx->get(), injectionPortId, getMmuDropReasons());
      }
    });
  };

  verifyAcrossWarmBoots(setup, verify);
}

void AgentMirrorOnDropStatelessTest::testWarmbootToggleSampling(
    bool enablePostWarmboot,
    int samplingRate) {
  const std::string kReportName = "mod-wb-toggle-sampling";

  auto addMod = [&]() {
    auto config = getAgentEnsemble()->getCurrentConfig();
    config.mirrorOnDropReports()->push_back(
        makeMirrorOnDropReport(kReportName, samplingRate));
    applyNewConfig(config);
    waitForStateUpdates(getSw());
  };

  auto removeMod = [&]() {
    auto config = getAgentEnsemble()->getCurrentConfig();
    config.mirrorOnDropReports()->clear();
    applyNewConfig(config);
    waitForStateUpdates(getSw());
  };

  auto verifyPresent = [&]() {
    auto state = getProgrammedState();
    auto reports = state->getMirrorOnDropReports();
    ASSERT_NE(reports, nullptr);
    auto report = reports->getNodeIf(kReportName);
    ASSERT_NE(report, nullptr);
    EXPECT_EQ(report->getSamplingRate(), samplingRate);
  };

  auto verifyAbsent = [&]() {
    auto state = getProgrammedState();
    auto reports = state->getMirrorOnDropReports();
    EXPECT_TRUE(reports == nullptr || reports->numNodes() == 0);
  };

  if (enablePostWarmboot) {
    verifyAcrossWarmBoots([]() {}, verifyAbsent, addMod, verifyPresent);
  } else {
    verifyAcrossWarmBoots(addMod, verifyPresent, removeMod, verifyAbsent);
  }
}

void AgentMirrorOnDropStatelessTest::testWarmbootEnableSampling(
    int samplingRate) {
  testWarmbootToggleSampling(/*enablePostWarmboot=*/true, samplingRate);
}

void AgentMirrorOnDropStatelessTest::testWarmbootDisableSampling(
    int samplingRate) {
  testWarmbootToggleSampling(/*enablePostWarmboot=*/false, samplingRate);
}

} // namespace facebook::fboss
