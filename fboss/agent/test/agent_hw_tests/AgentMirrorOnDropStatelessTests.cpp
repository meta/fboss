// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/agent_hw_tests/AgentMirrorOnDropStatelessTest.h"

#include <gtest/gtest.h>

#include "fboss/agent/TxPacket.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/PortTestUtils.h"
#include "fboss/lib/CommonUtils.h"

namespace facebook::fboss {

using namespace ::testing;

// Each test delegates to the platform-agnostic template method on
// AgentMirrorOnDropStatelessTest. ASIC-specific behavior (packet format,
// report config, drop reason codes) is dispatched inside the template method
// via the MirrorOnDropImpl Strategy.

TEST_F(AgentMirrorOnDropStatelessTest, DefaultRouteDrop) {
  testDefaultRouteDrop();
}

TEST_F(AgentMirrorOnDropStatelessTest, MmuDrop) {
  testMmuDrop();
}

TEST_F(AgentMirrorOnDropStatelessTest, AclDrop) {
  testAclDrop();
}

// MirrorOnDropWithSampling drives high drop volume by creating an ERSPAN
// loop via the Strategy's configureErspanMirror dispatch — no direct
// references to ASIC-specific helpers.
//
// Flow:
// 1. Traffic loops on trafficPortId (MAC loopback + route + TTL decrement
//    disabled). Seed packets are injected and loop continuously.
// 2. An ERSPAN (GRE) tunnel mirror on trafficPortId encapsulates each
//    packet with outer dst IP = kMirrorTunnelDstIp and sends the copy to
//    mirrorEgressPortId (resolved from route by MirrorManager).
// 3. mirrorEgressPortId is in MAC loopback, so mirrored copies loop back
//    and re-enter the pipeline. The non-local dest MAC results in L2 drops
//    on loopback.
// 4. MirrorOnDrop with sampling captures a fraction (1/kSamplingRate) of
//    those drops.
//
// Statistical model:
//   n = total dropped packets, p = 1/kSamplingRate
//   E[MoD] = n * p / (1 - p), bounds = [0.99, 1.01] * E[MoD]
//   With n=1B and p=0.001: z-score threshold = 10, false failure < 1 in 10^23.
TEST_F(AgentMirrorOnDropStatelessTest, MirrorOnDropWithSampling) {
  PortID trafficPortId = masterLogicalInterfacePortIds()[0];
  PortID mirrorEgressPortId = masterLogicalInterfacePortIds()[1];
  PortID collectorPortId = masterLogicalInterfacePortIds()[2];
  XLOG(DBG3) << "Traffic loop port: " << portDesc(trafficPortId);
  XLOG(DBG3) << "Mirror egress (drop) port: " << portDesc(mirrorEgressPortId);
  XLOG(DBG3) << "Collector port: " << portDesc(collectorPortId);

  const int kSamplingRate = 1000;
  const int64_t kMinDrops = 1'000'000'000;

  // Non-local MAC triggers L2 drops on loopback (dst MAC mismatch).
  const auto kCollectorNonLocalMac = folly::MacAddress::fromHBO(
      getMacForFirstInterfaceWithPortsForTesting(getProgrammedState())
          .u64HBO() +
      10);

  const folly::IPAddressV6 kTrafficLoopIp{
      "2401:7777:7777:7777:7777:7777:7777:7777"};
  const folly::IPAddressV6 kMirrorTunnelDstIp{
      "2401:6666:6666:6666:6666:6666:6666:6666"};
  const std::string kTrafficMirrorName = "traffic-mirror";

  auto setup = [&]() {
    cfg::SwitchConfig config = getAgentEnsemble()->getCurrentConfig();

    config.mirrorOnDropReports()->push_back(
        makeMirrorOnDropReport("mod-sampling-test", kSamplingRate));

    // No explicit egress port — MirrorManager resolves from route.
    configureErspanMirror(
        config,
        kTrafficMirrorName,
        kMirrorTunnelDstIp,
        kSwitchIp_,
        trafficPortId);

    applyNewConfig(config);

    setupEcmpTraffic(
        trafficPortId,
        kTrafficLoopIp,
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState()),
        true /*disableTtlDecrement*/);

    // Non-local dest MAC will result in L2 drops on loopback.
    setupEcmpTraffic(
        mirrorEgressPortId, kMirrorTunnelDstIp, kCollectorNonLocalMac);

    setupEcmpTraffic(collectorPortId, kCollectorIp_, kCollectorNonLocalMac);

    waitForStateUpdates(getSw());

    WITH_RETRIES({
      auto mirrorState =
          getProgrammedState()->getMirrors()->getNodeIf(kTrafficMirrorName);
      ASSERT_EVENTUALLY_TRUE(mirrorState != nullptr);
      EXPECT_EVENTUALLY_TRUE(mirrorState->isResolved());
    });
  };

  auto verify = [&]() {
    auto state = getProgrammedState();
    auto mirrorOnDropReports = state->getMirrorOnDropReports();
    ASSERT_NE(mirrorOnDropReports, nullptr);
    auto report = mirrorOnDropReports->getNodeIf("mod-sampling-test");
    ASSERT_NE(report, nullptr);
    EXPECT_EQ(report->getSamplingRate(), kSamplingRate);

    const std::vector<PortID> allPorts = {
        trafficPortId, mirrorEgressPortId, collectorPortId};

    auto statsBefore = getLatestPortStats(allPorts);

    sendPackets(
        static_cast<int>(
            getAgentEnsemble()->getMinPktsForLineRate(trafficPortId)),
        trafficPortId,
        kTrafficLoopIp,
        0 /*priority*/,
        64 /*payloadSize*/);

    WITH_RETRIES_N(30, {
      auto mirrorEgressStats = getLatestPortStats(mirrorEgressPortId);
      int64_t mirrorOut = *mirrorEgressStats.outUnicastPkts_() -
          *statsBefore.at(mirrorEgressPortId).outUnicastPkts_();
      XLOG_EVERY_MS(INFO, 1000)
          << "Mirror egress outUnicastPkts so far: " << mirrorOut;
      EXPECT_EVENTUALLY_GE(mirrorOut, kMinDrops);
    });

    getAgentEnsemble()->getLinkToggler()->bringDownPorts({trafficPortId});
    getAgentEnsemble()->getLinkToggler()->bringUpPorts({trafficPortId});
    getAgentEnsemble()->waitForSpecificRateOnPort(trafficPortId, 0);
    getAgentEnsemble()->waitForSpecificRateOnPort(mirrorEgressPortId, 0);
    getAgentEnsemble()->waitForSpecificRateOnPort(collectorPortId, 0);

    waitForStatsToStabilize(allPorts);

    auto statsAfter = getLatestPortStats(allPorts);

    int64_t mirrorEgressDrops =
        *statsAfter[mirrorEgressPortId].outUnicastPkts_() -
        *statsBefore[mirrorEgressPortId].outUnicastPkts_();
    // Include inDiscards from trafficPort defensively.
    int64_t trafficPortDiscards = *statsAfter[trafficPortId].inDiscards_() -
        *statsBefore[trafficPortId].inDiscards_();
    int64_t droppedPackets = mirrorEgressDrops + trafficPortDiscards;
    int64_t modPacketsReceived =
        *statsAfter[collectorPortId].outUnicastPkts_() -
        *statsBefore[collectorPortId].outUnicastPkts_();

    XLOG(INFO) << "droppedPackets=" << droppedPackets
               << " (mirrorEgressDrops=" << mirrorEgressDrops
               << " trafficPortDiscards=" << trafficPortDiscards << ")"
               << " modPacketsReceived=" << modPacketsReceived;
    // Statistical bounds below assume the kMinDrops floor was reached. If
    // WITH_RETRIES_N(30) above timed out before hitting kMinDrops, the
    // bounds collapse and could yield a misleading pass — fail loudly.
    ASSERT_GE(droppedPackets, kMinDrops);
    ASSERT_GT(modPacketsReceived, 0);

    // inUnicastPkts may not be populated on MAC loopback for all platforms;
    // use bytes for portability.
    int64_t trafficInBytes = *statsAfter[trafficPortId].inBytes_() -
        *statsBefore[trafficPortId].inBytes_();
    int64_t trafficOutBytes = *statsAfter[trafficPortId].outBytes_() -
        *statsBefore[trafficPortId].outBytes_();
    XLOG(INFO) << "trafficPort verification: inBytes=" << trafficInBytes
               << " outBytes=" << trafficOutBytes;
    EXPECT_GT(trafficInBytes, 0);
    EXPECT_GT(trafficOutBytes, 0);

    const double p = 1.0 / kSamplingRate;
    const double expectedModPackets = droppedPackets * p / (1.0 - p);
    const double deviation = 0.01 * expectedModPackets;
    const int64_t minExpected =
        static_cast<int64_t>(std::floor(expectedModPackets - deviation));
    const int64_t maxExpected =
        static_cast<int64_t>(std::ceil(expectedModPackets + deviation));

    XLOG(INFO) << "MirrorOnDrop sampling: received=" << modPacketsReceived
               << " expected=" << expectedModPackets << " bounds=["
               << minExpected << ", " << maxExpected << "]"
               << " drops=" << droppedPackets
               << " samplingRate=" << kSamplingRate;

    EXPECT_GE(modPacketsReceived, minExpected) << "Below expected range";
    EXPECT_LE(modPacketsReceived, maxExpected) << "Above expected range";
  };

  verifyAcrossWarmBoots(setup, verify);
}

// MirrorOnDropAggregateRateLimit verifies aggregateDropPacketRateThreshold
// caps the combined MoD rate across two drop reasons. Like
// MirrorOnDropWithSampling, line-rate traffic loops with ERSPAN mirrors
// generate the drops. Tajo has no egress mirroring, so each drop case uses
// its own traffic port:
// 1. Mirror 1 copies egress mirrorEgressPort1 with a non-local dest MAC and
//    are L2-dropped on loopback.
// 2. Mirror 2 copies egress mirrorEgressPort2 and are dropped on loopback by
//    a deny ACL.
// Both are ingress-pipeline drops with no per-reason MoD rate limit, so the
// MoD rate on the collector port should sit at kAggregateRate. Each drop case
// alone exceeds the cap.
class AgentMirrorOnDropAggregateRateLimitTest
    : public AgentMirrorOnDropStatelessTest {
 protected:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    auto features =
        AgentMirrorOnDropStatelessTest::getProductionFeaturesVerified();
    features.push_back(ProductionFeature::MIRROR_ON_DROP_AGGREGATE_RATE_LIMIT);
    return features;
  }
};

TEST_F(
    AgentMirrorOnDropAggregateRateLimitTest,
    MirrorOnDropAggregateRateLimit) {
  const auto ports = masterLogicalInterfacePortIds();
  const PortID trafficPort1 = ports[0];
  const PortID mirrorEgressPort1 = ports[1];
  const PortID trafficPort2 = ports[2];
  const PortID mirrorEgressPort2 = ports[3];
  const PortID collectorPortId = ports[4];

  const std::string kReportName = "mod-aggregate-rate-test";
  constexpr int32_t kAggregateRate = 1000;
  constexpr int kModIncreases = 10;
  constexpr double kRateTolerance = 0.1;

  const auto kRouterMac =
      getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
  const auto kNonLocalMac =
      folly::MacAddress::fromHBO(kRouterMac.u64HBO() + 10);
  const folly::IPAddressV6 kTrafficLoopIp1{"2401:7777::1"};
  const folly::IPAddressV6 kTrafficLoopIp2{"2401:7777::2"};
  const folly::IPAddressV6 kMirrorTunnelDstIp1{"2401:6666::1"};
  const folly::IPAddressV6 kMirrorTunnelDstIp2{"2401:6666::2"};

  auto addErspanMirror = [&](cfg::SwitchConfig& config,
                             const std::string& name,
                             const folly::IPAddressV6& tunnelDstIp,
                             const PortID& srcPortId) {
    cfg::Mirror mirror;
    mirror.name() = name;
    mirror.destination().ensure().tunnel().ensure().greTunnel().ensure().ip() =
        tunnelDstIp.str();
    mirror.destination()->tunnel()->srcIp() = kSwitchIp_.str();
    mirror.truncate() = true;
    config.mirrors()->push_back(mirror);
    utility::findCfgPort(config, srcPortId)->ingressMirror() = name;
  };

  auto setup = [&]() {
    auto config = getAgentEnsemble()->getCurrentConfig();
    auto report = makeMirrorOnDropReport(kReportName);
    report.aggregateDropPacketRateThreshold() = kAggregateRate;
    config.mirrorOnDropReports()->push_back(report);
    addErspanMirror(
        config, "mirror-l2-drop", kMirrorTunnelDstIp1, trafficPort1);
    addErspanMirror(
        config, "mirror-acl-drop", kMirrorTunnelDstIp2, trafficPort2);
    addDropPacketAcl(&config, mirrorEgressPort2);
    applyNewConfig(config);

    setupEcmpTraffic(
        trafficPort1,
        kTrafficLoopIp1,
        kRouterMac,
        true /*disableTtlDecrement*/);
    setupEcmpTraffic(
        trafficPort2,
        kTrafficLoopIp2,
        kRouterMac,
        true /*disableTtlDecrement*/);
    setupEcmpTraffic(mirrorEgressPort1, kMirrorTunnelDstIp1, kNonLocalMac);
    setupEcmpTraffic(mirrorEgressPort2, kMirrorTunnelDstIp2, kRouterMac);
    setupEcmpTraffic(collectorPortId, kCollectorIp_, kCollectorNextHopMac_);
    waitForStateUpdates(getSw());
  };

  auto verify = [&]() {
    for (const auto& [port, loopIp] :
         {std::pair{trafficPort1, kTrafficLoopIp1},
          std::pair{trafficPort2, kTrafficLoopIp2}}) {
      sendPackets(
          static_cast<int>(getAgentEnsemble()->getMinPktsForLineRate(port)),
          port,
          loopIp,
          0 /*priority*/,
          64 /*payloadSize*/);
    }
    const std::vector<PortID> measuredPorts = {
        mirrorEgressPort1, mirrorEgressPort2, collectorPortId};
    auto modPkts = [&](const std::map<PortID, HwPortStats>& stats) {
      return *stats.at(collectorPortId).outUnicastPkts_();
    };
    auto statsTime = [&](const std::map<PortID, HwPortStats>& stats) {
      return *stats.at(collectorPortId).timestamp_();
    };

    // The window opens at the first poll that shows MoD packets, so both drop
    // cases are flowing and the initial meter burst is excluded. It closes
    // once MoD packets have increased kModIncreases more times.
    auto last = getLatestPortStats(measuredPorts);
    std::optional<decltype(last)> first;
    int modIncreases = 0;
    WITH_RETRIES({
      auto cur = getLatestPortStats(measuredPorts);
      if (modPkts(cur) > modPkts(last)) {
        if (first.has_value()) {
          ++modIncreases;
        } else {
          first = cur;
        }
      }
      last = std::move(cur);
      EXPECT_EVENTUALLY_EQ(modIncreases, kModIncreases);
    });
    ASSERT_TRUE(first.has_value());

    getAgentEnsemble()->getLinkToggler()->bringDownPorts(
        {trafficPort1, trafficPort2});
    getAgentEnsemble()->getLinkToggler()->bringUpPorts(
        {trafficPort1, trafficPort2});
    getAgentEnsemble()->waitForSpecificRateOnPort(trafficPort1, 0);
    getAgentEnsemble()->waitForSpecificRateOnPort(trafficPort2, 0);

    auto outPkts = [&](const PortID& port) {
      return *last.at(port).outUnicastPkts_() -
          *first->at(port).outUnicastPkts_();
    };
    const int64_t windowSecs = statsTime(last) - statsTime(*first);
    const double expected = static_cast<double>(kAggregateRate) * windowSecs;
    const int64_t modPackets = outPkts(collectorPortId);
    XLOG(INFO) << "windowSecs=" << windowSecs
               << " l2Drops=" << outPkts(mirrorEgressPort1)
               << " aclDrops=" << outPkts(mirrorEgressPort2)
               << " modPackets=" << modPackets << " expected=" << expected;

    ASSERT_GT(outPkts(mirrorEgressPort1), expected);
    ASSERT_GT(outPkts(mirrorEgressPort2), expected);
    EXPECT_GE(modPackets, expected * (1.0 - kRateTolerance));
    EXPECT_LE(modPackets, expected * (1.0 + kRateTolerance));
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Config changes across warmboot require HW writes, so failHwCallsOnWarmboot
// must be false.
class AgentMirrorOnDropStatelessWarmbootTest
    : public AgentMirrorOnDropStatelessTest {
 protected:
  bool failHwCallsOnWarmboot() const override {
    return false;
  }
};

TEST_F(AgentMirrorOnDropStatelessWarmbootTest, WarmbootEnableSampling) {
  testWarmbootEnableSampling(1000);
}

TEST_F(AgentMirrorOnDropStatelessWarmbootTest, WarmbootDisableSampling) {
  testWarmbootDisableSampling(1000);
}

} // namespace facebook::fboss
