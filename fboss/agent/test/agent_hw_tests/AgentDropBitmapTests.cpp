// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/agent_hw_tests/AgentDropTestBase.h"
#include "fboss/agent/test/gen-cpp2/production_features_types.h"
#include "fboss/agent/test/utils/AgentDropReasonBitmapReporting.h"
#include "fboss/lib/CommonUtils.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss {

// Drop bitmaps are READ_AND_CLEAR: each HwAgent stats-collection cycle reads
// and clears the HW bitmap and pushes that one snapshot to the SwSwitch cache
// these tests read, so a single drop is easy to miss — especially post-warmboot
// while the HwAgent->SwSwitch stats sink is re-establishing. Each verify loop
// below re-sends the drop-triggering traffic on every WITH_RETRIES iteration
// (the ~1s collection interval) and OR-accumulates: a miss in one collection
// window is covered by the next, and the first observed drop is latched.

class AgentDropBitmapTest : public AgentDropTestBase {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::CUSTOM_DROP_BITMAP_SUPPORT};
  }

 protected:
  using DropBitmapField = BitmapDropReasonReportingImpl::DropBitmapField;

  static void orBitmapStats(
      HwSwitchDropBitmapStats& dst,
      const HwSwitchDropBitmapStats& src) {
    BitmapDropReasonReportingImpl::orBitmapStats(dst, src);
  }

  HwSwitchDropBitmapStats getAggregatedSwitchDropBitmapStats() {
    return dropReasonReporting().getAggregatedSwitchDropBitmapStats();
  }

  void waitForDropBitmapsToClear() {
    dropReasonReporting().waitForDropBitmapsToClear();
  }

  void verifyOnlyExpectedDrop(
      const HwSwitchDropBitmapStats& stats,
      DropBitmapField expectedField,
      int expectedBit,
      const char* desc) {
    dropReasonReporting().verifyOnlyExpectedDrop(
        stats, expectedField, expectedBit, desc);
  }

 private:
  BitmapDropReasonReportingImpl& dropReasonReporting() {
    if (!reporting_) {
      DropReasonReportingContext context;
      context.readSwitchStats = [this]() { return getAllHwSwitchStats(); };
      for (const auto& switchIndex :
           getSw()->getSwitchInfoTable().getSwitchIndices()) {
        context.switchIndices.push_back(switchIndex);
      }
      reporting_ =
          std::make_unique<BitmapDropReasonReportingImpl>(std::move(context));
    }
    return *reporting_;
  }

  std::unique_ptr<BitmapDropReasonReportingImpl> reporting_;
};

TEST_F(AgentDropBitmapTest, pipelineIpv4Ipv6Drops) {
  auto verify = [&]() {
    // Install log capture (in the HwAgent process) before triggering drops.
    installLogCapture();

    // Confirm all drop bitmaps are clear before we start — especially on the
    // warm-boot iteration, where residual drops from the cold-boot run can
    // linger and contaminate the OR-accumulated stats below.
    waitForDropBitmapsToClear();

    // IPv4 loopback DIP drop
    HwSwitchDropBitmapStats ipv4Stats;
    WITH_RETRIES({
      sendLoopbackDipPacket<folly::IPAddressV4>();
      orBitmapStats(ipv4Stats, getAggregatedSwitchDropBitmapStats());
      EXPECT_EVENTUALLY_TRUE(
          ipv4Stats.pipelineIpv4DropBitmap().value_or(0) &
          (1LL << PipelineIpDropBit::kDipIsLoopback));
    });
    verifyOnlyExpectedDrop(
        ipv4Stats,
        DropBitmapField::PIPELINE_IPV4,
        PipelineIpDropBit::kDipIsLoopback,
        "IPv4 loopback DIP");
    verifyDropReasonLogged(
        "DROP reasons pipelineIpv4: (0x8) PIPELINE_IPV4_DIP_IS_LOOPBACK",
        "IPv4 loopback DIP log");

    // Drain residual drops before the IPv6 phase so the IPv4 drop doesn't
    // leak into IPv6's exclusivity check.
    waitForDropBitmapsToClear();

    // IPv6 loopback DIP drop
    HwSwitchDropBitmapStats ipv6Stats;
    WITH_RETRIES({
      sendLoopbackDipPacket<folly::IPAddressV6>();
      orBitmapStats(ipv6Stats, getAggregatedSwitchDropBitmapStats());
      EXPECT_EVENTUALLY_TRUE(
          ipv6Stats.pipelineIpv6DropBitmap().value_or(0) &
          (1LL << PipelineIpDropBit::kDipIsLoopback));
    });
    verifyOnlyExpectedDrop(
        ipv6Stats,
        DropBitmapField::PIPELINE_IPV6,
        PipelineIpDropBit::kDipIsLoopback,
        "IPv6 loopback DIP");
    verifyDropReasonLogged(
        "DROP reasons pipelineIpv6: (0x8) PIPELINE_IPV6_DIP_IS_LOOPBACK",
        "IPv6 loopback DIP log");
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentDropBitmapTest, pipelineMacAndIngressMacDrops) {
  auto verify = [&]() {
    // Install log capture (in the HwAgent process) before triggering drops.
    installLogCapture();

    // Confirm all drop bitmaps are clear (and stats are flowing) before we
    // start — handles residuals from the previous warm-boot iteration.
    waitForDropBitmapsToClear();

    // Pipeline MAC: SMAC multicast drop
    HwSwitchDropBitmapStats macStats;
    WITH_RETRIES({
      sendMulticastSmacPacket();
      orBitmapStats(macStats, getAggregatedSwitchDropBitmapStats());
      EXPECT_EVENTUALLY_TRUE(
          macStats.pipelineMacDropBitmap().value_or(0) &
          (1LL << PipelineMacDropBit::kSmacIsMulticast));
    });
    verifyOnlyExpectedDrop(
        macStats,
        DropBitmapField::PIPELINE_MAC,
        PipelineMacDropBit::kSmacIsMulticast,
        "Pipeline MAC SMAC multicast");
    verifyDropReasonLogged(
        "DROP reasons pipelineMac: (0x800) PIPELINE_MAC_SMAC_IS_MC",
        "Pipeline MAC SMAC log");

    // Drain residual drops before the ingress MAC phase.
    waitForDropBitmapsToClear();

    // Ingress MAC: type out of range drop
    // EtherType 0x05E0 is in the reserved range 0x05DD..0x05FF
    // This also triggers PIPELINE_MAC_INVALID_ETHERTYPE — the packet
    // traverses multiple pipeline stages that each flag it independently.
    // We verify the primary drop (ingressMac) without cross-contamination
    // checks since multi-stage drops are expected.
    HwSwitchDropBitmapStats ingressMacStats;
    WITH_RETRIES({
      sendOutOfRangeEtherTypePacket();
      orBitmapStats(ingressMacStats, getAggregatedSwitchDropBitmapStats());
      EXPECT_EVENTUALLY_TRUE(
          ingressMacStats.ingressMacDropBitmap().value_or(0) &
          (1LL << IngressMacDropBit::kTypeOutOfRange));
    });
    verifyDropReasonLogged(
        "DROP reasons ingressMac: (0x10) INGRESS_MAC_TYPE_OUT_OF_RANGE",
        "Ingress MAC type OOR log");
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentDropBitmapTest, egressMacMtuDrop) {
  auto setup = [&]() { setupEgressMtuDropScenario(); };

  auto verify = [&]() {
    // Install log capture (in the HwAgent process) before triggering drops.
    installLogCapture();

    // Confirm all drop bitmaps are clear (and stats are flowing) before we
    // start — handles residuals from the previous warm-boot iteration.
    waitForDropBitmapsToClear();

    // Assert only the egress MAC MTU bit (not verifyOnlyExpectedDrop): the
    // oversized routed packet can also trigger collateral drops in the
    // loopback test topology, so an exclusivity check would be flaky.
    HwSwitchDropBitmapStats stats;
    WITH_RETRIES({
      sendOversizedPacketToRoutedDst();
      orBitmapStats(stats, getAggregatedSwitchDropBitmapStats());
      EXPECT_EVENTUALLY_TRUE(
          stats.egressMacDropBitmap().value_or(0) &
          (1LL << EgressMacDropBit::kEgressMtuError));
    });
    verifyDropReasonLogged(
        "DROP reasons egressMac: (0x1) EGRESS_MAC_EGRESS_MTU_ERROR",
        "Egress MTU log");
  };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
