// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/agent_hw_tests/AgentDropTestBase.h"
#include "fboss/agent/test/gen-cpp2/production_features_types.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/AgentDropReasonBitmapReporting.h"
#include "fboss/agent/test/utils/AgentDropReasonListReporting.h"
#include "fboss/lib/CommonUtils.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss {

// Tests for the drop reason list form of drop reporting. Scenario creation
// lives in AgentDropTestBase and is shared with AgentDropBitmapTests; only the
// assertions are here, since the two forms report drops differently and an
// ASIC supports only one of them.
//
// Reasons are read back from the HwAgent log rather than from stats: they are
// decoded and logged in the process that owns the ASIC and never cross into
// SwSwitch. installLogCapture() resets the capture window, so each test starts
// from a clean slate, and the window then accumulates every reason the ASIC
// reported. The lists are cleared on read, so each verify loop below re-sends
// the drop triggering traffic on every WITH_RETRIES iteration -- a drop missed
// in one collection window is covered by the next.

class AgentDropReasonTest : public AgentDropTestBase {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::DROP_REASON_LIST_SUPPORT};
  }

 protected:
  using DropReasons = ListDropReasonReportingImpl::DropReasons;
  using DropStage = ListDropReasonReportingImpl::DropStage;

  static void accumulate(DropReasons& dst, const DropReasons& src) {
    ListDropReasonReportingImpl::accumulate(dst, src);
  }

  DropReasons getAggregatedDropReasons() {
    return dropReasonReporting().getAggregatedDropReasons();
  }

  static std::string toString(const DropReasons& reasons) {
    return ListDropReasonReportingImpl::toString(reasons);
  }

  // Log the drop reasons after every collection, not just the final state.
  // If an expectation is wrong, the run shows what the ASIC reported, which is
  // what it takes to correct the expected name without another round trip.
  void logObserved(const char* desc, const DropReasons& reasons) {
    XLOG(INFO) << "Drop reason test [" << desc
               << "] observed: " << toString(reasons);
  }

  void verifyOnlyExpectedReason(
      const DropReasons& reasons,
      DropStage dropStage,
      const std::string& expected,
      const char* desc) {
    dropReasonReporting().verifyOnlyExpectedReason(
        reasons, dropStage, expected, desc);
  }

 private:
  ListDropReasonReportingImpl& dropReasonReporting() {
    if (!reporting_) {
      DropReasonReportingContext context;
      context.readLogMessages = [this]() {
        std::vector<std::string> messages;
        for (const auto& switchId :
             getSw()->getSwitchInfoTable().getSwitchIDs()) {
          std::vector<std::string> matches;
          getAgentEnsemble()
              ->getHwAgentTestClient(switchId)
              ->sync_getMatchingLogMessages(matches, "DROP reasons ");
          messages.insert(messages.end(), matches.begin(), matches.end());
        }
        return messages;
      };
      reporting_ =
          std::make_unique<ListDropReasonReportingImpl>(std::move(context));
    }
    return *reporting_;
  }

  std::unique_ptr<ListDropReasonReportingImpl> reporting_;
};

// A packet to an address with no matching route is discarded at L3
// destination resolution. The ASIC reports L3_DST_DISCARD, not
// L3_DST_LOOKUP_MISS, accompanied by the generic RDROP.
TEST_F(AgentDropReasonTest, ingressL3DstDiscardDrop) {
  auto verify = [&]() {
    installLogCapture();

    DropReasons reasons;
    WITH_RETRIES({
      sendPacketToUnroutedDst();
      accumulate(reasons, getAggregatedDropReasons());
      logObserved("L3 DST discard", reasons);
      EXPECT_EVENTUALLY_TRUE(reasons.ingress.contains(kL3DstDiscard));
    });
    logPortDropCounters("L3 DST discard");
    XLOG(INFO) << "Drop reason test [L3 DST discard] final: "
               << toString(reasons);
    verifyOnlyExpectedReason(
        reasons, DropStage::Ingress, kL3DstDiscard, "L3 DST discard");
    verifyDropReasonLogged("DROP reasons ingress", "L3 DST discard log");
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

// A routed packet arriving with hop limit 1 decrements to 0 and is dropped.
//
// Deliberately no verifyOnlyExpectedReason() here, unlike its siblings: the
// ASIC reports RIPD6 alongside L3_TTL_ERROR, and RIPD6 is a real specific
// reason rather than a generic one, so an exclusivity check would always
// fail.
TEST_F(AgentDropReasonTest, ingressTtlErrorDrop) {
  auto setup = [&]() { setupRouteToEgressPort(); };

  auto verify = [&]() {
    installLogCapture();

    DropReasons reasons;
    WITH_RETRIES({
      sendTtlExpiredPacket();
      accumulate(reasons, getAggregatedDropReasons());
      logObserved("L3 TTL error", reasons);
      EXPECT_EVENTUALLY_TRUE(reasons.ingress.contains(kL3TtlError));
    });
    logPortDropCounters("L3 TTL error");
    XLOG(INFO) << "Drop reason test [L3 TTL error] final: "
               << toString(reasons);
    verifyDropReasonLogged("DROP reasons ingress", "L3 TTL error log");
  };
  verifyAcrossWarmBoots(setup, verify);
}

// A packet with a multicast source MAC. The ASIC reports SRC_ROUTE_DROP,
// accompanied only by the generic RDROP.
TEST_F(AgentDropReasonTest, ingressMacSaMulticastDrop) {
  auto verify = [&]() {
    installLogCapture();

    DropReasons reasons;
    WITH_RETRIES({
      sendMulticastSmacPacket();
      accumulate(reasons, getAggregatedDropReasons());
      logObserved("MACSA multicast", reasons);
      EXPECT_EVENTUALLY_TRUE(reasons.ingress.contains(kSrcRouteDrop));
    });
    logPortDropCounters("MACSA multicast");
    XLOG(INFO) << "Drop reason test [MACSA multicast] final: "
               << toString(reasons);
    verifyOnlyExpectedReason(
        reasons, DropStage::Ingress, kSrcRouteDrop, "MACSA multicast");
    verifyDropReasonLogged("DROP reasons ingress", "MACSA multicast log");
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

// A packet denied by an ingress ACL.
//
// The ACL matches only the single routed destination this test sends to, and
// the packet is routable, so the deny is the only thing that can drop it.
// Both details matter. Denying ::/0 also denies the switch's own background
// IPv6 traffic, which reports RFILDR in every collection window whether or
// not the test sends anything, leaving the assertion below to pass without
// the injected packet contributing to it; and sending to an unrouted
// destination adds an L3_DST_DISCARD that competes with the deny for
// attribution.
class AgentDropReasonAclTest : public AgentDropReasonTest {
 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = AgentDropReasonTest::initialConfig(ensemble);
    cfg::AclEntry acl;
    acl.name() = "drop-reason-deny-v6";
    acl.actionType() = cfg::AclActionType::DENY;
    acl.dstIp() = kRoutedDstIp().str() + "/128";
    utility::addAcl(&cfg, acl, cfg::AclStage::INGRESS);
    return cfg;
  }
};

TEST_F(AgentDropReasonAclTest, ingressAclDenyDrop) {
  auto setup = [&]() { setupRouteToEgressPort(); };
  auto verify = [&]() {
    installLogCapture();

    DropReasons reasons;
    WITH_RETRIES({
      sendPacketToRoutedDst();
      accumulate(reasons, getAggregatedDropReasons());
      logObserved("ACL deny", reasons);
      EXPECT_EVENTUALLY_TRUE(reasons.ingress.contains(kRxFilterDrop));
    });
    logPortDropCounters("ACL deny");
    XLOG(INFO) << "Drop reason test [ACL deny] final: " << toString(reasons);
    verifyDropReasonLogged("DROP reasons ingress", "ACL deny log");
  };
  verifyAcrossWarmBoots(setup, verify);
}

// A routed packet larger than the egress port MTU reports EGR_MTU on the
// egress list and nothing on ingress.
TEST_F(AgentDropReasonTest, egressMtuDrop) {
  auto setup = [&]() { setupEgressMtuDropScenario(); };

  auto verify = [&]() {
    installLogCapture();

    DropReasons reasons;
    WITH_RETRIES({
      sendOversizedPacketToRoutedDst();
      accumulate(reasons, getAggregatedDropReasons());
      logObserved("egress MTU", reasons);
      EXPECT_EVENTUALLY_TRUE(reasons.egress.contains(kEgrMtu));
    });
    logPortDropCounters("egress MTU");
    XLOG(INFO) << "Drop reason test [egress MTU] final: " << toString(reasons);
    verifyOnlyExpectedReason(reasons, DropStage::Egress, kEgrMtu, "egress MTU");
    verifyDropReasonLogged("DROP reasons egress", "egress MTU log");
  };
  verifyAcrossWarmBoots(setup, verify);
}

// A routed packet whose egress port has TX disabled. This is reported on the
// *ingress* list as SRC_PORT_KNOCKOUT_DROP, not as an egress reason -- the
// pipeline drops it before the egress stage once the destination port is
// knocked out of the port bitmap.
TEST_F(AgentDropReasonTest, ingressSrcPortKnockoutDrop) {
  auto setup = [&]() { setupRouteToEgressPort(); };

  auto verify = [&]() {
    installLogCapture();

    setEgressPortTx(false);

    DropReasons reasons;
    WITH_RETRIES({
      sendPacketToRoutedDst();
      accumulate(reasons, getAggregatedDropReasons());
      logObserved("src port knockout", reasons);
      EXPECT_EVENTUALLY_TRUE(reasons.ingress.contains(kSrcPortKnockout));
    });
    logPortDropCounters("src port knockout");
    XLOG(INFO) << "Drop reason test [src port knockout] final: "
               << toString(reasons);
    verifyOnlyExpectedReason(
        reasons, DropStage::Ingress, kSrcPortKnockout, "src port knockout");
    verifyDropReasonLogged("DROP reasons ingress", "src port knockout log");
    // Re-enabled only once every check above is done. Bringing TX back up
    // flushes the held packets and can log fresh drop reasons of its own,
    // which would otherwise land in the still-open capture window that
    // verifyDropReasonLogged() reads, and would move the port counters.
    setEgressPortTx(true);
  };
  verifyAcrossWarmBoots(setup, verify);
}

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
