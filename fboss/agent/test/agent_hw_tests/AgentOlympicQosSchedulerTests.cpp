/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/IPAddress.h>
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"

#include "fboss/agent/test/agent_hw_tests/AgentQosSchedulerTestBase.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/NetworkAITestUtils.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"
#include "fboss/agent/test/utils/QosTestUtils.h"
#include "fboss/lib/CommonUtils.h"

namespace facebook::fboss {

class AgentOlympicQosSchedulerTest : public AgentQosSchedulerTestBase {
 protected:
  void _setupOlympicV2Queues() {
    auto newCfg{initialConfig(*getAgentEnsemble())};
    utility::addOlympicV2WRRQueueConfig(
        &newCfg, getAgentEnsemble()->getL3Asics());
    utility::addOlympicV2QosMaps(newCfg, getAgentEnsemble()->getL3Asics());
    utility::setTTLZeroCpuConfig(getAgentEnsemble()->getL3Asics(), newCfg);
    applyNewConfig(newCfg);
  }

  void _verifyDscpQueueMappingHelper(
      const std::map<int, std::vector<uint8_t>>& queueToDscp) {
    auto portId = outPort();
    auto portStatsBefore = getLatestPortStats(portId);
    for (const auto& q2dscps : queueToDscp) {
      for (auto dscp : q2dscps.second) {
        sendUdpPkt(dscp);
      }
    }
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(
          utility::verifyQueueMappings(
              portStatsBefore, queueToDscp, getAgentEnsemble(), portId));
    });
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);
    utility::addOlympicQueueConfig(&cfg, ensemble.getL3Asics());
    utility::addOlympicQosMaps(cfg, ensemble.getL3Asics());
    utility::setTTLZeroCpuConfig(ensemble.getL3Asics(), cfg);
    return cfg;
  }
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::L3_QOS, ProductionFeature::OLYMPIC_QOS};
  }
  void verifyWRR();
  void verifySP(bool frontPanelTraffic = true);
  void verifyWRRAndICP();
  void verifyWRRAndNC();
  void verifySingleWRRAndNC();
  void verifyWRRAndSP(const std::vector<int>& queueIds, int trafficQueueId);
  void verifySingleWRRAndSP(
      const std::vector<int>& queueIds,
      int trafficQueueId);

  void verifyWRRToAllSPDscpToQueue();
  void verifyWRRToAllSPTraffic();
  void verifyDscpToQueueOlympicToOlympicV2();
  void verifyWRRForOlympicToOlympicV2();
  void verifyDscpToQueueOlympicV2ToOlympic();
  void verifyOlympicV2WRRToAllSPTraffic();
  void verifyOlympicV2AllSPTrafficToWRR();
};

void AgentOlympicQosSchedulerTest::verifyWRR() {
  auto setup = [=, this]() {
    auto newCfg{initialConfig(*getAgentEnsemble())};
    if (isDualStage3Q2QQos()) {
      auto hwAsic =
          checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
      auto streamType =
          *hwAsic->getQueueStreamTypes(cfg::PortType::INTERFACE_PORT).begin();
      utility::addNetworkAIQueueConfig(
          &newCfg,
          streamType,
          cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN,
          hwAsic);
    }
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    EXPECT_TRUE(verifyWRRHelper(
        utility::getMaxWeightWRRQueue(utility::kOlympicWRRQueueToWeight()),
        utility::kOlympicWRRQueueToWeight(),
        utility::kOlympicWRRQueueIds(),
        utility::kOlympicQueueToDscp()));
  };

  verifyAcrossWarmBoots(setup, verify);
}

void AgentOlympicQosSchedulerTest::verifySP(bool frontPanelTraffic) {
  utility::EcmpSetupAnyNPorts6 ecmpHelper6(
      getProgrammedState(), getSw()->needL2EntryForNeighbor(), dstMac());

  auto verify = [=, this]() {
    _setup(ecmpHelper6);
    EXPECT_TRUE(verifySPHelper(
        // SP queue with highest queueId
        // should starve other SP queues
        // altogether
        utility::getOlympicQueueId(utility::OlympicQueueType::NC),
        utility::kOlympicSPQueueIds(),
        utility::kOlympicQueueToDscp(),
        frontPanelTraffic));
    // stop the traffic
    bringDownPort(outPort());
    bringUpPort(outPort());
  };

  verifyAcrossWarmBoots(verify);
}

void AgentOlympicQosSchedulerTest::verifyWRRAndSP(
    const std::vector<int>& queueIds,
    int trafficQueueId) {
  auto verify = [=, this]() {
    EXPECT_TRUE(verifySPHelper(
        trafficQueueId, queueIds, utility::kOlympicQueueToDscp()));
  };

  verifyAcrossWarmBoots([]() {}, verify);
}

void AgentOlympicQosSchedulerTest::verifySingleWRRAndSP(
    const std::vector<int>& queueIds,
    int trafficQueueId) {
  utility::EcmpSetupAnyNPorts6 ecmpHelper6(
      getProgrammedState(), getSw()->needL2EntryForNeighbor(), dstMac());
  auto setup = [=, this]() { _setup(ecmpHelper6); };

  auto verify = [=, this]() {
    for (auto queue : queueIds) {
      if (queue != trafficQueueId) {
        XLOG(DBG2) << "send traffic to WRR queue " << queue << " and SP queue "
                   << trafficQueueId;
        sendUdpPktsForAllQueues(
            {queue, trafficQueueId}, utility::kOlympicQueueToDscp());
        EXPECT_TRUE(verifySPHelper(
            trafficQueueId, queueIds, utility::kOlympicQueueToDscp()));
        // toggle route to stop traffic, and then send traffic to each WRR
        // queue and SP queue
        XLOG(DBG2) << "unprogram routes";
        unprogramRoutes(ecmpHelper6);
        // wait for no traffic going out of port
        getAgentEnsemble()->waitForSpecificRateOnPort(outPort(), 0);

        XLOG(DBG2) << "program routes";
        auto wrapper = getSw()->getRouteUpdater();
        ecmpHelper6.programRoutes(&wrapper, kDefaultEcmpWidth);
      }
    }
  };

  verifyAcrossWarmBoots(setup, verify);
}

void AgentOlympicQosSchedulerTest::verifyWRRAndICP() {
  verifyWRRAndSP(
      utility::kOlympicWRRAndICPQueueIds(),
      utility::getOlympicQueueId(
          utility::OlympicQueueType::ICP)); // SP should starve WRR
                                            // queues altogether
}

void AgentOlympicQosSchedulerTest::verifyWRRAndNC() {
  verifyWRRAndSP(
      utility::kOlympicWRRAndNCQueueIds(),
      utility::getOlympicQueueId(
          utility::OlympicQueueType::NC)); // SP should starve WRR
                                           // queues altogether
}

void AgentOlympicQosSchedulerTest::verifySingleWRRAndNC() {
  verifySingleWRRAndSP(
      utility::kOlympicWRRAndNCQueueIds(),
      utility::getOlympicQueueId(
          utility::OlympicQueueType::NC)); // SP should starve WRR
                                           // queues altogether
}

/*
 * This test verifies the DSCP to queue mapping when transitioning from
 * Olympic queue ids with WRR+SP to Olympic V2 queue ids with all SP
 * over warmboot
 */
void AgentOlympicQosSchedulerTest::verifyWRRToAllSPDscpToQueue() {
  utility::EcmpSetupAnyNPorts6 ecmpHelper6(
      getProgrammedState(), getSw()->needL2EntryForNeighbor(), dstMac());

  auto setup = [=, this]() {
    resolveNeighborAndProgramRoutes(ecmpHelper6, kDefaultEcmpWidth);
  };

  auto verify = [=, this]() {
    _verifyDscpQueueMappingHelper(utility::kOlympicQueueToDscp());
  };

  auto setupPostWarmboot = [=, this]() {
    auto newCfg{initialConfig(*getAgentEnsemble())};
    utility::addOlympicAllSPQueueConfig(
        &newCfg, getAgentEnsemble()->getL3Asics());
    utility::addOlympicV2QosMaps(newCfg, getAgentEnsemble()->getL3Asics());
    utility::setTTLZeroCpuConfig(getAgentEnsemble()->getL3Asics(), newCfg);
    applyNewConfig(newCfg);
  };

  auto verifyPostWarmboot = [=, this]() {
    _verifyDscpQueueMappingHelper(utility::kOlympicV2QueueToDscp());
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

/*
 * This test verifies the traffic priority when transitioning from
 * Olympic queue ids with WRR+SP to Olympic V2 queue ids with all SP
 * over warmboot.
 */
void AgentOlympicQosSchedulerTest::verifyWRRToAllSPTraffic() {
  utility::EcmpSetupAnyNPorts6 ecmpHelper6(
      getProgrammedState(), getSw()->needL2EntryForNeighbor(), dstMac());

  auto setup = [=, this]() { _setup(ecmpHelper6); };

  auto verify = [=]() {};

  auto setupPostWarmboot = [=, this]() {
    auto newCfg{initialConfig(*getAgentEnsemble())};
    utility::addOlympicAllSPQueueConfig(
        &newCfg, getAgentEnsemble()->getL3Asics());
    utility::addOlympicV2QosMaps(newCfg, getAgentEnsemble()->getL3Asics());
    utility::setTTLZeroCpuConfig(getAgentEnsemble()->getL3Asics(), newCfg);
    applyNewConfig(newCfg);
  };

  auto verifyPostWarmboot = [=, this]() {
    EXPECT_TRUE(verifySPHelper(
        // SP queue with highest queueId
        // should starve other SP queues
        // altogether
        utility::getOlympicV2QueueId(utility::OlympicV2QueueType::NC),
        utility::kOlympicAllSPQueueIds(),
        utility::kOlympicV2QueueToDscp()));
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

/*
 * This test verifies the dscp to queue mapping
 * when transitioning from Olympic queue ids with WRR+SP to Olympic V2
 * queue ids with WRR+SP over warmboot.
 */
void AgentOlympicQosSchedulerTest::verifyDscpToQueueOlympicToOlympicV2() {
  utility::EcmpSetupAnyNPorts6 ecmpHelper6(
      getProgrammedState(), getSw()->needL2EntryForNeighbor(), dstMac());

  auto setup = [=, this]() {
    resolveNeighborAndProgramRoutes(ecmpHelper6, kDefaultEcmpWidth);
  };

  auto verify = [=, this]() {
    _verifyDscpQueueMappingHelper(utility::kOlympicQueueToDscp());
  };

  auto setupPostWarmboot = [=, this]() { _setupOlympicV2Queues(); };

  auto verifyPostWarmboot = [=, this]() {
    // Verify DSCP to Queue mapping
    _verifyDscpQueueMappingHelper(utility::kOlympicV2QueueToDscp());
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

/*
 * This test verifies the traffic priority interms of weights
 * when transitioning from Olympic queue ids with WRR+SP to Olympic V2
 * queue ids with WRR+SP over warmboot.
 */
void AgentOlympicQosSchedulerTest::verifyWRRForOlympicToOlympicV2() {
  auto setupPostWarmboot = [=, this]() { _setupOlympicV2Queues(); };

  auto verifyPostWarmboot = [=, this]() {
    /*
     * Verify whether the WRR weights are being honored
     */
    EXPECT_TRUE(verifyWRRHelper(
        utility::getMaxWeightWRRQueue(utility::kOlympicV2WRRQueueToWeight()),
        utility::kOlympicV2WRRQueueToWeight(),
        utility::kOlympicV2WRRQueueIds(),
        utility::kOlympicV2QueueToDscp()));
  };

  verifyAcrossWarmBoots(
      []() {}, []() {}, setupPostWarmboot, verifyPostWarmboot);
}

/*
 * This test verifies the dscp to queue mapping when transitioning
 * from Olympic V2 queue ids with WRR+SP to Olympic queue ids with
 * WRR+SP over warmboot.
 */
void AgentOlympicQosSchedulerTest::verifyDscpToQueueOlympicV2ToOlympic() {
  utility::EcmpSetupAnyNPorts6 ecmpHelper6(
      getProgrammedState(), getSw()->needL2EntryForNeighbor(), dstMac());

  auto setup = [=, this]() {
    resolveNeighborAndProgramRoutes(ecmpHelper6, kDefaultEcmpWidth);
  };

  auto verify = [=, this]() {
    _verifyDscpQueueMappingHelper(utility::kOlympicV2QueueToDscp());
  };

  auto setupPostWarmboot = [=, this]() {
    auto newCfg{initialConfig(*getAgentEnsemble())};
    utility::addOlympicQueueConfig(&newCfg, getAgentEnsemble()->getL3Asics());
    utility::addOlympicQosMaps(newCfg, getAgentEnsemble()->getL3Asics());
    utility::setTTLZeroCpuConfig(getAgentEnsemble()->getL3Asics(), newCfg);
    applyNewConfig(newCfg);
  };

  auto verifyPostWarmboot = [=, this]() {
    _verifyDscpQueueMappingHelper(utility::kOlympicQueueToDscp());
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

/*
 * This test verifies the traffic prioritization when transitioning
 * from Olympic V2 WRR+SP QOS policy to Olympic V2 all SP qos policy
 * over warmboot.
 */
void AgentOlympicQosSchedulerTest::verifyOlympicV2WRRToAllSPTraffic() {
  utility::EcmpSetupAnyNPorts6 ecmpHelper6(
      getProgrammedState(), getSw()->needL2EntryForNeighbor(), dstMac());

  auto setup = [=, this]() {
    _setup(ecmpHelper6);
    _setupOlympicV2Queues();
  };

  auto verify = [=]() {};

  auto setupPostWarmboot = [=, this]() {
    auto newCfg{initialConfig(*getAgentEnsemble())};
    utility::addOlympicAllSPQueueConfig(
        &newCfg, getAgentEnsemble()->getL3Asics());
    utility::addOlympicV2QosMaps(newCfg, getAgentEnsemble()->getL3Asics());
    utility::setTTLZeroCpuConfig(getAgentEnsemble()->getL3Asics(), newCfg);
    applyNewConfig(newCfg);
  };

  auto verifyPostWarmboot = [=, this]() {
    EXPECT_TRUE(verifySPHelper(
        // SP queue with highest queueId
        // should starve other SP queues
        // altogether
        utility::getOlympicV2QueueId(utility::OlympicV2QueueType::NC),
        utility::kOlympicAllSPQueueIds(),
        utility::kOlympicV2QueueToDscp()));
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

/*
 * This test verifies the traffic prioritization when transitioning
 * from Olympic V2 all SP QOS policy to Olympic V2 WRR+SP qos policy
 * over warmboot.
 */
void AgentOlympicQosSchedulerTest::verifyOlympicV2AllSPTrafficToWRR() {
  utility::EcmpSetupAnyNPorts6 ecmpHelper6(
      getProgrammedState(), getSw()->needL2EntryForNeighbor(), dstMac());

  auto setup = [=, this]() {
    _setup(ecmpHelper6);
    auto newCfg{initialConfig(*getAgentEnsemble())};
    utility::addOlympicAllSPQueueConfig(
        &newCfg, getAgentEnsemble()->getL3Asics());
    utility::addOlympicV2QosMaps(newCfg, getAgentEnsemble()->getL3Asics());
    utility::setTTLZeroCpuConfig(getAgentEnsemble()->getL3Asics(), newCfg);
    applyNewConfig(newCfg);
  };

  auto verify = [=]() {};

  auto setupPostWarmboot = [=, this]() { _setupOlympicV2Queues(); };

  auto verifyPostWarmboot = [=, this]() {
    // Verify whether the WRR weights are being honored
    EXPECT_TRUE(verifyWRRHelper(
        utility::getMaxWeightWRRQueue(utility::kOlympicV2WRRQueueToWeight()),
        utility::kOlympicV2WRRQueueToWeight(),
        utility::kOlympicV2WRRQueueIds(),
        utility::kOlympicV2QueueToDscp()));
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

TEST_F(AgentOlympicQosSchedulerTest, VerifyWRR) {
  verifyWRR();
}

TEST_F(AgentOlympicQosSchedulerTest, VerifySP) {
  verifySP();
}

/*
 * This test asserts that CPU injected traffic from a higher priority
 * queue will preempt traffic from a lower priority queue. We only
 * test for CPU traffic explicitly as VerifySP above already
 * tests front panel traffic.
 */
TEST_F(AgentOlympicQosSchedulerTest, VerifySPPreemptionCPUTraffic) {
  auto spQueueIds = utility::kOlympicSPQueueIds();
  auto getQueueIndex = [&](int queueId) {
    for (auto i = 0; i < spQueueIds.size(); ++i) {
      if (spQueueIds[i] == queueId) {
        return i;
      }
    }
    throw FbossError("Could not find queueId: ", queueId);
  };
  // Assert that ICP comes before NC in the queueIds array.
  // We will send traffic to all queues in order. So for
  // preemption we want lower pri (ICP) queue to go before
  // higher pri queue (NC).
  ASSERT_LT(
      getQueueIndex(getOlympicQueueId(utility::OlympicQueueType::ICP)),
      getQueueIndex(getOlympicQueueId(utility::OlympicQueueType::NC)));

  verifySP(false /*frontPanelTraffic*/);
}

TEST_F(AgentOlympicQosSchedulerTest, VerifyWRRAndICP) {
  verifyWRRAndICP();
}

TEST_F(AgentOlympicQosSchedulerTest, VerifyWRRAndNC) {
  verifyWRRAndNC();
}

TEST_F(AgentOlympicQosSchedulerTest, VerifySingleWRRAndNC) {
  verifySingleWRRAndNC();
}

TEST_F(AgentOlympicQosSchedulerTest, VerifyWRRToAllSPDscpToQueue) {
  verifyWRRToAllSPDscpToQueue();
}

TEST_F(AgentOlympicQosSchedulerTest, VerifyWRRToAllSPTraffic) {
  verifyWRRToAllSPTraffic();
}

TEST_F(AgentOlympicQosSchedulerTest, VerifyDscpToQueueOlympicToOlympicV2) {
  verifyDscpToQueueOlympicToOlympicV2();
}

TEST_F(AgentOlympicQosSchedulerTest, VerifyWRRForOlympicToOlympicV2) {
  verifyWRRForOlympicToOlympicV2();
}

class AgentOlympicV2MigrationQosSchedulerTest
    : public AgentOlympicQosSchedulerTest {
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = AgentOlympicQosSchedulerTest::initialConfig(ensemble);
    utility::addOlympicV2WRRQueueConfig(&cfg, ensemble.getL3Asics());
    utility::addOlympicV2QosMaps(cfg, ensemble.getL3Asics());
    utility::setTTLZeroCpuConfig(ensemble.getL3Asics(), cfg);
    return cfg;
  }
};

TEST_F(
    AgentOlympicV2MigrationQosSchedulerTest,
    VerifyDscpToQueueOlympicV2ToOlympic) {
  verifyDscpToQueueOlympicV2ToOlympic();
}

TEST_F(AgentOlympicQosSchedulerTest, VerifyOlympicV2WRRToAllSPTraffic) {
  verifyOlympicV2WRRToAllSPTraffic();
}

class AgentOlympicV2SPToWRRQosSchedulerTest
    : public AgentOlympicQosSchedulerTest {
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = AgentOlympicQosSchedulerTest::initialConfig(ensemble);
    utility::addOlympicAllSPQueueConfig(&cfg, ensemble.getL3Asics());
    utility::addOlympicV2QosMaps(cfg, ensemble.getL3Asics());
    utility::setTTLZeroCpuConfig(ensemble.getL3Asics(), cfg);
    return cfg;
  }
};

TEST_F(
    AgentOlympicV2SPToWRRQosSchedulerTest,
    VerifyOlympicV2AllSPTrafficToWRR) {
  verifyOlympicV2AllSPTrafficToWRR();
}
} // namespace facebook::fboss
