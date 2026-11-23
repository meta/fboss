// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "fboss/agent/test/AgentHwTest.h"

#include <chrono>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/EcmpDataPlaneTestUtil.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"

namespace facebook::fboss {

// Exhaustive IPv6 flow-label sweep, in the shape of
// AgentL4PortBlackholingTests: one fixed 5-tuple is sent once for every
// flow label, and the test asserts that every packet egressed. Unlike
// the load-balancer tests it makes no claim about how evenly packets
// spread across ECMP members, only that none were blackholed for a
// particular hash input. Hashing is programmed with the flow label as
// a hash field, so varying only the flow label varies only the hash
// input; all other 5-tuple fields stay fixed.
class AgentFlowLabelBlackholingTest : public AgentHwTest {
 private:
  // ECMP width, matching the load-balancer tests. The injection port
  // is the next port after these, so 9 interface ports are required.
  static constexpr int kEcmpWidth = 8;
  // Every value of the 20-bit flow-label field, excluding 0.
  static constexpr int kNumFlowLabels = 0xFFFFF;
  // Egress must keep rising across stats collections; this many
  // consecutive collections with no increase means packets were lost.
  static constexpr int kMaxStatsRoundsWithoutProgress = 30;
  // Upper bound on the whole wait, however slowly egress rises. Netcastle
  // SIGABRTs each test binary run after 900s, which also covers agent
  // init and exit, so this must fail well before that with a real
  // assertion message.
  static constexpr auto kOverallTimeout = std::chrono::minutes(6);

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::L3_FORWARDING,
        ProductionFeature::ECMP_LOAD_BALANCER};
  }

  std::optional<size_t> maxRequiredInterfacePorts() const override {
    // Front-panel injection uses the port at index kEcmpWidth, so at
    // least kEcmpWidth + 1 interface ports are required.
    return kEcmpWidth + 1;
  }

  // ECMP member ports that carry the swept traffic, and the separate
  // front-panel port used only to inject it. The inject port's own
  // out counters include the looped injection itself, so it is never
  // counted as egress.
  std::pair<std::vector<PortID>, PortID> getEgressAndInjectPorts() {
    utility::HwIpV6FlowLabelEcmpDataPlaneTestUtil helper(
        getAgentEnsemble(), RouterID(0));
    auto* ecmpHelper = helper.ecmpSetupHelper();
    std::vector<PortID> egressPortIds;
    for (const auto& portDesc : ecmpHelper->ecmpPortDescs(kEcmpWidth)) {
      if (portDesc.isPhysicalPort()) {
        egressPortIds.push_back(portDesc.phyPortID());
      }
    }
    PortID injectPortId =
        ecmpHelper->ecmpPortDescriptorAt(kEcmpWidth).phyPortID();
    return {egressPortIds, injectPortId};
  }

 protected:
  void pumpTrafficAndVerify(
      const std::vector<PortID>& egressPortIds,
      PortID injectPortId) {
    const auto originalStats = getLatestPortStats(egressPortIds);
    int64_t original = 0;
    for (const auto& portId : egressPortIds) {
      auto it = originalStats.find(portId);
      EXPECT_TRUE(it != originalStats.end());
      if (it != originalStats.end()) {
        original += *it->second.outUnicastPkts_();
      }
    }

    auto mac = getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    // pumpTrafficWithFlowLabel sends flow labels (i + 1) & 0xFFFFF, so
    // sweeping kNumFlowLabels packets covers 1..0xFFFFF exactly once.
    // The 5-tuple (1001::1, 2001::1, 10000, 20000, UDP) is fixed.
    utility::pumpTrafficWithFlowLabel(
        utility::getAllocatePktFn(getAgentEnsemble()),
        utility::getSendPktFunc(getAgentEnsemble()),
        mac,
        getVlanIDForTx(),
        injectPortId,
        255,
        kNumFlowLabels);

    // Send in bulk and assert on the counter delta afterwards, rather
    // than checking counters per packet. Packet TX can lag far behind
    // the send calls (e.g. multi-switch streams them to the HW agent),
    // so keep waiting while egress is still rising and fail only once
    // it stops moving short of the sweep size.
    int64_t delta = 0;
    int64_t lastDelta = -1;
    int roundsWithoutProgress = 0;
    const auto deadline = std::chrono::steady_clock::now() + kOverallTimeout;
    while (delta < kNumFlowLabels &&
           roundsWithoutProgress < kMaxStatsRoundsWithoutProgress &&
           std::chrono::steady_clock::now() < deadline) {
      const auto newStats = getNextUpdatedPortStats(egressPortIds);
      int64_t current = 0;
      for (const auto& portId : egressPortIds) {
        auto it = newStats.find(portId);
        if (it != newStats.end()) {
          current += *it->second.outUnicastPkts_();
        }
      }
      delta = current - original;
      XLOGF(
          INFO,
          "Checking egress outPkts ({}) == "
          "number of flow labels swept ({})",
          delta,
          kNumFlowLabels);
      if (delta > lastDelta) {
        lastDelta = delta;
        roundsWithoutProgress = 0;
      } else {
        ++roundsWithoutProgress;
      }
    }
    EXPECT_EQ(delta, static_cast<int64_t>(kNumFlowLabels));
  }

  void runTest() {
    auto setup = [=, this]() {
      utility::HwIpV6FlowLabelEcmpDataPlaneTestUtil helper(
          getAgentEnsemble(), RouterID(0));
      auto asics = getAgentEnsemble()->getL3Asics();
      helper.programRoutesAndLoadBalancer(
          kEcmpWidth, {}, utility::getEcmpFullWithFlowLabelHashConfig(asics));
    };
    auto verify = [=, this]() {
      learnSrcMacOnEcmpTxPortIfPending(kEcmpWidth);
      const auto [egressPortIds, injectPortId] = getEgressAndInjectPorts();
      ASSERT_EQ(kEcmpWidth, egressPortIds.size());
      pumpTrafficAndVerify(egressPortIds, injectPortId);
    };
    verifyAcrossWarmBoots(setup, verify);
  }
};

TEST_F(AgentFlowLabelBlackholingTest, V6FlowLabelSweepEgressesAllPackets) {
  runTest();
}

} // namespace facebook::fboss
