// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/agent_hw_tests/AgentDropTestBase.h"
#include "fboss/agent/test/gen-cpp2/production_features_types.h"
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

// Drop cause bit positions from the Chenab SDK telemetry pipeline.
// These map to sx_tele_drop_cause_*_reason_e enums in sx_tele_auto.h.
enum PipelineIpDropBit : int {
  kDipIsLoopback = 3,
};

enum PipelineMacDropBit : int {
  kSmacIsMulticast = 11,
};

enum IngressMacDropBit : int {
  kTypeOutOfRange = 4,
};

enum EgressMacDropBit : int {
  kEgressMtuError = 0,
};

class AgentDropBitmapTest : public AgentDropTestBase {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::CUSTOM_DROP_BITMAP_SUPPORT};
  }

 protected:
  // OR-accumulate src into dst. Used to aggregate across switch indices
  // and across retry iterations (bitmaps are READ_AND_CLEAR, so drops
  // observed in earlier reads must be preserved).
  static void orBitmapStats(
      HwSwitchDropBitmapStats& dst,
      const HwSwitchDropBitmapStats& src) {
#define OR_BITMAP(field) \
  dst.field() = dst.field().value_or(0) | src.field().value_or(0);

    OR_BITMAP(ingressMacDropBitmap);
    OR_BITMAP(egressPipeDropBitmap);
    OR_BITMAP(egressMacDropBitmap);
    OR_BITMAP(pipelineGeneralDropBitmap);
    OR_BITMAP(pipelineMacDropBitmap);
    OR_BITMAP(pipelineMplsDropBitmap);
    OR_BITMAP(pipelineTunnelDropBitmap);
    OR_BITMAP(pipelineIpv4DropBitmap);
    OR_BITMAP(pipelineIpv6DropBitmap);
    OR_BITMAP(bufferDropBitmap);
    OR_BITMAP(queueDropBitmap);
    OR_BITMAP(hostDropBitmap);
#undef OR_BITMAP
  }

  HwSwitchDropBitmapStats getAggregatedSwitchDropBitmapStats() {
    HwSwitchDropBitmapStats result;
    checkWithRetry([&result, this]() {
      auto switchIndex2Stats = getAllHwSwitchStats();
      for (const auto& switchIndex :
           getSw()->getSwitchInfoTable().getSwitchIndices()) {
        auto it = switchIndex2Stats.find(switchIndex);
        if (it == switchIndex2Stats.end()) {
          return false;
        }
        // OR into the persistent result (not a per-iteration local): a
        // concurrent READ_AND_CLEAR collection may consume a switch's drops
        // between retries, so a partial snapshot seen on an earlier retry
        // must not be discarded or those drops are lost permanently.
        orBitmapStats(result, *it->second.switchDropBitmapStats());
      }
      return true;
    });
    return result;
  }

  // True if any drop bitmap field is non-zero.
  static bool hasAnyDrop(const HwSwitchDropBitmapStats& s) {
    return (s.ingressMacDropBitmap().value_or(0) |
            s.egressPipeDropBitmap().value_or(0) |
            s.egressMacDropBitmap().value_or(0) |
            s.pipelineGeneralDropBitmap().value_or(0) |
            s.pipelineMacDropBitmap().value_or(0) |
            s.pipelineMplsDropBitmap().value_or(0) |
            s.pipelineTunnelDropBitmap().value_or(0) |
            s.pipelineIpv4DropBitmap().value_or(0) |
            s.pipelineIpv6DropBitmap().value_or(0) |
            s.bufferDropBitmap().value_or(0) | s.queueDropBitmap().value_or(0) |
            s.hostDropBitmap().value_or(0)) != 0;
  }

  // Drain the drop bitmaps before a capture phase so a stale drop (from a prior
  // phase or warm-boot iteration) isn't OR-latched into the next phase. A zero
  // read alone is ambiguous — value_or(0) can't tell a real zero from stats not
  // flowing yet — so require a few consecutive snapshots that are both fresh
  // (per-switch timestamp advancing) and all-zero.
  void waitForDropBitmapsToClear() {
    constexpr int kRequiredConsecutiveClearReads = 3;
    auto prevStats = getAllHwSwitchStats();
    int consecutiveClearReads = 0;
    WITH_RETRIES({
      auto curStats = getAllHwSwitchStats();
      bool freshAndClear = true;
      for (const auto& switchIndex :
           getSw()->getSwitchInfoTable().getSwitchIndices()) {
        auto curIt = curStats.find(switchIndex);
        auto prevIt = prevStats.find(switchIndex);
        if (curIt == curStats.end() || prevIt == prevStats.end()) {
          freshAndClear = false;
          continue;
        }
        if (curIt->second.timestamp().value() <=
                prevIt->second.timestamp().value() ||
            hasAnyDrop(*curIt->second.switchDropBitmapStats())) {
          freshAndClear = false;
        }
      }
      prevStats = curStats;
      consecutiveClearReads = freshAndClear ? consecutiveClearReads + 1 : 0;
      EXPECT_EVENTUALLY_TRUE(
          consecutiveClearReads >= kRequiredConsecutiveClearReads);
    });
  }

  enum class DropBitmapField {
    INGRESS_MAC,
    EGRESS_PIPE,
    EGRESS_MAC,
    PIPELINE_GENERAL,
    PIPELINE_MAC,
    PIPELINE_MPLS,
    PIPELINE_TUNNEL,
    PIPELINE_IPV4,
    PIPELINE_IPV6,
    BUFFER,
    QUEUE,
    HOST,
  };

  // Verify that only the expected drop reason fires and no other bitmap
  // categories report unexpected drops. This catches regressions where a
  // code or config change causes collateral drops in unrelated stages.
  void verifyOnlyExpectedDrop(
      const HwSwitchDropBitmapStats& stats,
      DropBitmapField expectedField,
      int expectedBit,
      const char* desc) {
    auto getBitmap = [&](DropBitmapField f) -> int64_t {
      switch (f) {
        case DropBitmapField::INGRESS_MAC:
          return stats.ingressMacDropBitmap().value_or(0);
        case DropBitmapField::EGRESS_PIPE:
          return stats.egressPipeDropBitmap().value_or(0);
        case DropBitmapField::EGRESS_MAC:
          return stats.egressMacDropBitmap().value_or(0);
        case DropBitmapField::PIPELINE_GENERAL:
          return stats.pipelineGeneralDropBitmap().value_or(0);
        case DropBitmapField::PIPELINE_MAC:
          return stats.pipelineMacDropBitmap().value_or(0);
        case DropBitmapField::PIPELINE_MPLS:
          return stats.pipelineMplsDropBitmap().value_or(0);
        case DropBitmapField::PIPELINE_TUNNEL:
          return stats.pipelineTunnelDropBitmap().value_or(0);
        case DropBitmapField::PIPELINE_IPV4:
          return stats.pipelineIpv4DropBitmap().value_or(0);
        case DropBitmapField::PIPELINE_IPV6:
          return stats.pipelineIpv6DropBitmap().value_or(0);
        case DropBitmapField::BUFFER:
          return stats.bufferDropBitmap().value_or(0);
        case DropBitmapField::QUEUE:
          return stats.queueDropBitmap().value_or(0);
        case DropBitmapField::HOST:
          return stats.hostDropBitmap().value_or(0);
      }
    };

    auto expected = getBitmap(expectedField);
    EXPECT_EQ(expected, 1LL << expectedBit)
        << desc << ": expected only bit " << expectedBit
        << " set in field, got: 0x" << std::hex << expected;

    for (auto f :
         {DropBitmapField::INGRESS_MAC,
          DropBitmapField::EGRESS_PIPE,
          DropBitmapField::EGRESS_MAC,
          DropBitmapField::PIPELINE_GENERAL,
          DropBitmapField::PIPELINE_MAC,
          DropBitmapField::PIPELINE_MPLS,
          DropBitmapField::PIPELINE_TUNNEL,
          DropBitmapField::PIPELINE_IPV4,
          DropBitmapField::PIPELINE_IPV6,
          DropBitmapField::BUFFER,
          DropBitmapField::QUEUE,
          DropBitmapField::HOST}) {
      if (f == expectedField) {
        continue;
      }
      EXPECT_EQ(getBitmap(f), 0)
          << desc << ": unexpected drop in other bitmap field, got: 0x"
          << std::hex << getBitmap(f);
    }
  }
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
