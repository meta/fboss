/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/bcm/tests/BcmCosQueueManagerTest.h"

#include <folly/logging/xlog.h>
#include "fboss/agent/hw/bcm/BcmCosQueueFBConvertors.h"
#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmPortQueueManager.h"
#include "fboss/agent/hw/bcm/BcmPortTable.h"
#include "fboss/agent/platforms/tests/utils/BcmTestPlatform.h"
#include "fboss/agent/types.h"

#include "fboss/agent/hw/test/ConfigFactory.h"

using namespace facebook::fboss;

namespace {

constexpr auto kQueue0ReservedBytesCells = 16;
constexpr auto kQueue1ReservedBytesCells = 96;
constexpr auto KProbability = 100;

std::vector<cfg::PortQueue> getConfigPortQueues(int mmuCellBytes) {
  std::vector<cfg::PortQueue> portQueues;
  cfg::PortQueue queue0;
  *queue0.id() = 0;
  queue0.name() = "queue0";
  *queue0.streamType() = cfg::StreamType::UNICAST;
  *queue0.scheduling() = cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
  queue0.weight() = 1;
  queue0.scalingFactor() = cfg::MMUScalingFactor::ONE;
  queue0.reservedBytes() = kQueue0ReservedBytesCells * mmuCellBytes;
  portQueues.push_back(queue0);

  cfg::PortQueue queue1;
  *queue1.id() = 1;
  queue1.name() = "queue1";
  *queue1.streamType() = cfg::StreamType::UNICAST;
  *queue1.scheduling() = cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
  queue1.weight() = 9;
  queue1.scalingFactor() = cfg::MMUScalingFactor::EIGHT;
  queue1.reservedBytes() = kQueue1ReservedBytesCells * mmuCellBytes;
  portQueues.push_back(queue1);

  cfg::PortQueue queue7;
  *queue7.id() = 7;
  queue7.name() = "queue7";
  *queue7.streamType() = cfg::StreamType::UNICAST;
  *queue7.scheduling() = cfg::QueueScheduling::STRICT_PRIORITY;
  portQueues.push_back(queue7);

  return portQueues;
}

std::vector<cfg::PortQueue> get7ConfigPortQueues(int mmuCellBytes) {
  std::vector<cfg::PortQueue> portQueues;
  for (auto i = 0; i < 8; i++) {
    cfg::PortQueue queue0;
    *queue0.id() = i;
    queue0.name() = folly::to<std::string>("queue", i);
    *queue0.streamType() = cfg::StreamType::UNICAST;
    *queue0.scheduling() = cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
    queue0.weight() = 1;
    queue0.scalingFactor() = cfg::MMUScalingFactor::ONE;
    queue0.reservedBytes() = kQueue0ReservedBytesCells * mmuCellBytes;
    portQueues.push_back(queue0);
  }
  return portQueues;
}

cfg::ActiveQueueManagement getEarlyDropAqmConfig(int mmuCellBytes) {
  cfg::ActiveQueueManagement earlyDropAQM;
  cfg::LinearQueueCongestionDetection earlyDropLQCD;
  *earlyDropLQCD.minimumLength() = mmuCellBytes;
  *earlyDropLQCD.maximumLength() = 2 * mmuCellBytes;
  earlyDropLQCD.probability() = KProbability / 2;
  earlyDropAQM.detection()->linear() = earlyDropLQCD;
  *earlyDropAQM.behavior() = cfg::QueueCongestionBehavior::EARLY_DROP;
  return earlyDropAQM;
}

cfg::ActiveQueueManagement getECNAqmConfig(int mmuCellBytes) {
  cfg::ActiveQueueManagement ecnAQM;
  cfg::LinearQueueCongestionDetection ecnLQCD;
  *ecnLQCD.minimumLength() = 3 * mmuCellBytes;
  *ecnLQCD.maximumLength() = 3 * mmuCellBytes;
  ecnLQCD.probability() = KProbability;
  ecnAQM.detection()->linear() = ecnLQCD;
  *ecnAQM.behavior() = cfg::QueueCongestionBehavior::ECN;
  return ecnAQM;
}

} // unnamed namespace

namespace facebook::fboss {

class BcmPortQueueManagerTest : public BcmCosQueueManagerTest {
 protected:
  cfg::SwitchConfig initialConfig() const override {
    return utility::oneL3IntfConfig(getHwSwitch(), masterLogicalPortIds()[0]);
  }

  std::optional<cfg::PortQueue> getCfgQueue(int queueID) override {
    std::optional<cfg::PortQueue> cfgQueue{std::nullopt};
    // we always program the first port in config
    for (const auto& queue :
         programmedCfg_.portQueueConfigs()["queue_config"]) {
      if (*queue.id() == queueID) {
        cfgQueue = queue;
        break;
      }
    }
    return cfgQueue;
  }

  int getCfgTrafficClass(int queueId, std::string policy) {
    for (auto qosPolicy : *programmedCfg_.qosPolicies()) {
      if (*qosPolicy.name() != policy) {
        continue;
      }
      if (auto qosMap = qosPolicy.qosMap()) {
        for (auto tc2Queue : *qosMap->trafficClassToQueueId()) {
          if (tc2Queue.second != queueId) {
            continue;
          }
          return tc2Queue.first;
        }
        break;
      }
    }
    return queueId;
  }

  int getCfgTrafficClass(int portId, int queueId) {
    if (auto policy = programmedCfg_.dataPlaneTrafficPolicy()) {
      if (auto portIdToQosPolicy = policy->portIdToQosPolicy()) {
        if (portIdToQosPolicy->find(portId) != portIdToQosPolicy->end()) {
          return getCfgTrafficClass(
              queueId, portIdToQosPolicy->find(portId)->second);
        }
      }
      if (auto defaultPolicy = policy->defaultQosPolicy()) {
        return getCfgTrafficClass(queueId, *defaultPolicy);
      }
    }
    return queueId;
  }

  QueueConfig getHwQueues() override {
    auto bcmPort = getHwSwitch()->getPortTable()->getBcmPort(
        PortID(masterLogicalPortIds()[0]));
    return bcmPort->getCurrentQueueSettings();
  }

  QueueConfig getSwQueues() override {
    auto swPort =
        getProgrammedState()->getPort(PortID(masterLogicalPortIds()[0]));
    return swPort->getPortQueues()->impl();
  }
  int mmuCellBytes() const {
    return getPlatform()->getMMUCellBytes();
  }
};

} // namespace facebook::fboss

TEST_F(BcmPortQueueManagerTest, DefaultPortQueuesCheckWithoutConfig) {
  if (!isSupported(HwAsic::Feature::L3_QOS)) {
    XLOG(WARNING) << "Platform doesn't support port cos queue setting";
    GTEST_SKIP();
  }

  auto setup = [=, this]() { applyNewConfig(initialConfig()); };

  auto verify = [&]() {
    auto defaultUCQueueSize = getAsic()->getDefaultNumPortQueues(
        cfg::StreamType::UNICAST, cfg::PortType::INTERFACE_PORT);
    EXPECT_EQ(getHwQueues().size(), defaultUCQueueSize);
    // if we've never set cosq configs to any queue. The system should come up
    // with all default values.
    const auto& queues = getSwQueues();
    EXPECT_EQ(queues.size(), defaultUCQueueSize);
    // default queue settings
    for (const auto& queue : queues) {
      checkDefaultCosqMatch(queue);
    }
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmPortQueueManagerTest, ConfigPortQueuesSetup) {
  if (!isSupported(HwAsic::Feature::L3_QOS)) {
    XLOG(WARNING) << "Platform doesn't support port cos queue setting";
    GTEST_SKIP();
  }

  auto setup = [=, this]() {
    auto cfg = initialConfig();
    cfg.portQueueConfigs()["queue_config"] =
        getConfigPortQueues(getPlatform()->getMMUCellBytes());
    auto portCfg = utility::findCfgPort(cfg, masterLogicalPortIds()[0]);
    portCfg->portQueueConfigName() = "queue_config";
    applyNewConfig(cfg);
  };

  auto verify = [&]() { checkConfSwHwMatch(); };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmPortQueueManagerTest, ChangeQueue0Settings) {
  if (!isSupported(HwAsic::Feature::L3_QOS)) {
    XLOG(WARNING) << "Platform doesn't support port cos queue setting";
    GTEST_SKIP();
  }

  // prepare aqm
  QueueConfig swQueuesBefore;
  auto setup = [&]() {
    auto cfg = initialConfig();
    cfg.portQueueConfigs()["queue_config"] =
        getConfigPortQueues(mmuCellBytes());
    auto portCfg = utility::findCfgPort(cfg, masterLogicalPortIds()[0]);
    portCfg->portQueueConfigName() = "queue_config";
    applyNewConfig(cfg);

    for (const auto& queue : getSwQueues()) {
      swQueuesBefore.push_back(queue);
    }

    auto& queue0 = cfg.portQueueConfigs()["queue_config"][0];
    queue0.weight() = 5;
    queue0.reservedBytes() = 10 * getPlatform()->getMMUCellBytes();
    queue0.scalingFactor() = cfg::MMUScalingFactor::EIGHT;
    if (!queue0.aqms()) {
      queue0.aqms() = {};
    }
    queue0.aqms()->push_back(getEarlyDropAqmConfig(mmuCellBytes()));
    applyNewConfig(cfg);
  };

  auto verify = [&]() {
    checkConfSwHwMatch();

    const auto swQueuesAfter = getSwQueues();
    EXPECT_EQ(swQueuesBefore.size(), swQueuesAfter.size());
    // now explicitly check queue0
    auto& newQueue0 = swQueuesAfter[0];
    EXPECT_EQ(newQueue0->getStreamType(), cfg::StreamType::UNICAST);
    EXPECT_EQ(
        newQueue0->getScheduling(), cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN);
    EXPECT_EQ(newQueue0->getWeight(), 5);
    EXPECT_EQ(
        newQueue0->getScalingFactor().value(), cfg::MMUScalingFactor::EIGHT);
    EXPECT_EQ(
        newQueue0->getReservedBytes().value(),
        10 * getPlatform()->getMMUCellBytes());
    auto aqms = newQueue0->getAqms()->toThrift();
    std::vector<cfg::ActiveQueueManagement> expectedAqms{};
    expectedAqms.push_back(getEarlyDropAqmConfig(mmuCellBytes()));
    EXPECT_EQ(aqms, expectedAqms);

    // other queues shouldn't be affected
    for (int i = 1; i < swQueuesAfter.size(); i++) {
      EXPECT_TRUE(*swQueuesBefore[i] == *swQueuesAfter[i]);
    }
  };

  // TODO - Make this test work across warmboot
  setup();
  verify();
}

TEST_F(BcmPortQueueManagerTest, ClearPortQueueSettings) {
  if (!isSupported(HwAsic::Feature::L3_QOS)) {
    XLOG(WARNING) << "Platform doesn't support port cos queue setting";
    GTEST_SKIP();
  }

  auto setup = [=, this]() {
    auto cfg = initialConfig();
    auto portQueues = getConfigPortQueues(mmuCellBytes());
    ;
    // set aqm for queue0 for testing
    auto& queue0 = portQueues[0];
    if (!queue0.aqms()) {
      queue0.aqms() = {};
    }
    queue0.aqms()->push_back(getEarlyDropAqmConfig(mmuCellBytes()));
    cfg.portQueueConfigs()["queue_config"] = portQueues;
    auto portCfg = utility::findCfgPort(cfg, masterLogicalPortIds()[0]);
    portCfg->portQueueConfigName() = "queue_config";
    applyNewConfig(cfg);

    // remove all port queue settings
    portCfg->portQueueConfigName().reset();
    cfg.portQueueConfigs()->erase("queue_config");
    applyNewConfig(cfg);
  };

  auto verify = [&]() {
    checkConfSwHwMatch();

    // explicit check hw queues are set back to HW default value
    const auto& queues = getSwQueues();
    EXPECT_TRUE(queues.size() > 0);
    // default queue settings
    for (const auto& queue : queues) {
      checkDefaultCosqMatch(queue);
    }
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmPortQueueManagerTest, ChangePortQueueAQM) {
  if (!isSupported(HwAsic::Feature::L3_QOS)) {
    XLOG(WARNING) << "Platform doesn't support port cos queue setting";
    GTEST_SKIP();
  }

  auto cfg = initialConfig();
  applyNewConfig(cfg);

  // no aqm -> earlyDrop=false;ecn=true
  {
    cfg.portQueueConfigs()["queue_config"] =
        getConfigPortQueues(mmuCellBytes());
    auto portCfg = utility::findCfgPort(cfg, masterLogicalPortIds()[0]);
    portCfg->portQueueConfigName() = "queue_config";
    auto& queue0 = cfg.portQueueConfigs()["queue_config"][0];
    if (!queue0.aqms()) {
      queue0.aqms() = {};
    }
    queue0.aqms()->push_back(getECNAqmConfig(mmuCellBytes()));
    applyNewConfig(cfg);
    checkConfSwHwMatch();
  }

  // earlyDrop=false;ecn=true -> earlyDrop=true;ecn=false
  {
    cfg.portQueueConfigs()["queue_config"] =
        getConfigPortQueues(mmuCellBytes());
    auto portCfg = utility::findCfgPort(cfg, masterLogicalPortIds()[0]);
    portCfg->portQueueConfigName() = "queue_config";
    auto& queue0 = cfg.portQueueConfigs()["queue_config"][0];
    if (!queue0.aqms()) {
      queue0.aqms() = {};
    }
    queue0.aqms()->push_back(getEarlyDropAqmConfig(mmuCellBytes()));
    applyNewConfig(cfg);
    checkConfSwHwMatch();
  }

  // earlyDrop=true;ecn=false -> earlyDrop=true;ecn=true
  {
    cfg.portQueueConfigs()["queue_config"] =
        getConfigPortQueues(mmuCellBytes());
    auto portCfg = utility::findCfgPort(cfg, masterLogicalPortIds()[0]);
    portCfg->portQueueConfigName() = "queue_config";
    auto& queue0 = cfg.portQueueConfigs()["queue_config"][0];
    if (!queue0.aqms()) {
      queue0.aqms() = {};
    }
    queue0.aqms()->push_back(getECNAqmConfig(mmuCellBytes()));
    queue0.aqms()->push_back(getEarlyDropAqmConfig(mmuCellBytes()));
    applyNewConfig(cfg);
    checkConfSwHwMatch();
  }

  // earlyDrop=true;ecn=true -> no aqm
  {
    cfg.portQueueConfigs()["queue_config"] =
        getConfigPortQueues(mmuCellBytes());
    auto portCfg = utility::findCfgPort(cfg, masterLogicalPortIds()[0]);
    portCfg->portQueueConfigName() = "queue_config";
    applyNewConfig(cfg);
    checkConfSwHwMatch();
  }
}

TEST_F(BcmPortQueueManagerTest, InternalPriorityMappings) {
  if (!isSupported(HwAsic::Feature::L3_QOS)) {
    XLOG(WARNING) << "Platform doesn't support port cos queue setting";
    GTEST_SKIP();
  }

  auto setup = [=, this]() {
    auto cfg = initialConfig();
    cfg.portQueueConfigs()["queue_config"] =
        getConfigPortQueues(mmuCellBytes());
    auto portCfg = utility::findCfgPort(cfg, masterLogicalPortIds()[0]);
    portCfg->portQueueConfigName() = "queue_config";
    applyNewConfig(cfg);
  };

  auto verify = [&]() {
    checkConfSwHwMatch();
    auto bcmPort = getHwSwitch()->getPortTable()->getBcmPort(
        PortID(masterLogicalPortIds()[0]));

    const auto& hwQueues = bcmPort->getCurrentQueueSettings();
    auto gport = bcmPort->getBcmGport();
    for (const auto& queue : hwQueues) {
      auto cfgQueue = getCfgQueue(queue->getID());
      // If the queue isn't programmed, then the mapping isn't programmed either
      bcm_cos_queue_t expectedCosq =
          getCfgQueue(queue->getID()) ? queue->getID() : 0;
      bcm_gport_t expectedQueueGport =
          bcmPort->getQueueManager()->getQueueGPort(
              queue->getStreamType(), expectedCosq);
      int prio = BcmPortQueueManager::CosQToBcmInternalPriority(expectedCosq);
      bcm_cos_queue_t cosq;
      bcm_gport_t queueGport;
      auto rv = bcm_cosq_gport_mapping_get(
          getUnit(),
          gport,
          prio,
          BCM_COSQ_GPORT_UCAST_QUEUE_GROUP,
          &queueGport,
          &cosq);
      bcmCheckError(rv, "failed to get cosq mapping from gport");
      EXPECT_EQ(cosq, expectedCosq);
      EXPECT_EQ(queueGport, expectedQueueGport);
    }
  };
  // TODO - Make this test work across warmboot
  setup();
  verify();
}

TEST_F(BcmPortQueueManagerTest, InternalPriorityMappingsOverride) {
  if (!isSupported(HwAsic::Feature::L3_QOS)) {
    XLOG(WARNING) << "Platform doesn't support port cos queue setting";
    GTEST_SKIP();
  }

  auto setup = [=, this]() {
    auto cfg = initialConfig();
    cfg.portQueueConfigs()["queue_config"] =
        get7ConfigPortQueues(mmuCellBytes());
    auto portCfg = utility::findCfgPort(cfg, masterLogicalPortIds()[0]);
    portCfg->portQueueConfigName() = "queue_config";
    cfg::QosMap qosMap;
    for (auto i = 0; i < 8; i++) {
      /* must provide all queue traffic class to queue id */
      qosMap.trafficClassToQueueId()->emplace(7 - i, i);
    }
    cfg.qosPolicies()->resize(1);
    *cfg.qosPolicies()[0].name() = "policy";
    cfg.qosPolicies()[0].qosMap() = qosMap;
    cfg::TrafficPolicyConfig policy;
    policy.defaultQosPolicy() = "policy";
    cfg.dataPlaneTrafficPolicy() = policy;
    applyNewConfig(cfg);
  };

  auto verify = [&]() {
    checkConfSwHwMatch();
    auto bcmPort = getHwSwitch()->getPortTable()->getBcmPort(
        PortID(masterLogicalPortIds()[0]));
    const auto& hwQueues = bcmPort->getCurrentQueueSettings();
    auto gport = bcmPort->getBcmGport();
    for (const auto& queue : hwQueues) {
      auto cfgQueue = getCfgQueue(queue->getID());
      // If the queue isn't programmed, then the mapping isn't programmed either
      bcm_cos_queue_t expectedCosq = cfgQueue.has_value() ? queue->getID() : 0;
      bcm_gport_t expectedQueueGport =
          bcmPort->getQueueManager()->getQueueGPort(
              queue->getStreamType(), expectedCosq);
      int prio = BcmPortQueueManager::CosQToBcmInternalPriority(expectedCosq);
      if (cfgQueue.has_value()) {
        prio = getCfgTrafficClass(masterLogicalPortIds()[0], queue->getID());
      }
      bcm_cos_queue_t cosq;
      bcm_gport_t queueGport;
      auto rv = bcm_cosq_gport_mapping_get(
          getUnit(),
          gport,
          prio,
          BCM_COSQ_GPORT_UCAST_QUEUE_GROUP,
          &queueGport,
          &cosq);
      bcmCheckError(rv, "failed to get cosq mapping from gport");
      EXPECT_EQ(cosq, expectedCosq);
      EXPECT_EQ(queueGport, expectedQueueGport);
    }
  };
  // TODO - Make this test work across warmboot
  setup();
  verify();
}
