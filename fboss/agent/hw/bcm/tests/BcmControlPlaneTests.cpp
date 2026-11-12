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

#include "fboss/agent/hw/bcm/BcmControlPlane.h"
#include "fboss/agent/hw/bcm/BcmControlPlaneQueueManager.h"
#include "fboss/agent/hw/bcm/BcmCosQueueManagerUtils.h"
#include "fboss/agent/hw/bcm/RxUtils.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/hw/test/HwTestCoppUtils.h"

using namespace facebook::fboss;

namespace {
const auto kLowCpuQueueWeight = 1;
const auto kLowCpuQueueReservedMmuCellNum = 5;
const auto kLowCpuQueueSharedMmuCellNum = 50;

std::vector<cfg::PortQueue> getConfigCPUQueues(
    uint8_t mmuCellBytes,
    const HwAsic* hwAsic) {
  std::vector<cfg::PortQueue> cpuQueues;
  cfg::PortQueue high;
  *high.id() = utility::getCoppHighPriQueueId(hwAsic);
  high.name() = "cpuQueue-high";
  *high.streamType() = cfg::StreamType::MULTICAST;
  *high.scheduling() = cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
  high.weight() = 4;
  cpuQueues.push_back(high);

  cfg::PortQueue mid;
  *mid.id() = utility::kCoppMidPriQueueId;
  mid.name() = "cpuQueue-mid";
  *mid.streamType() = cfg::StreamType::MULTICAST;
  *mid.scheduling() = cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
  mid.weight() = 2;
  cpuQueues.push_back(mid);

  cfg::PortQueue defaultQ;
  *defaultQ.id() = utility::kCoppDefaultPriQueueId;
  defaultQ.name() = "cpuQueue-default";
  *defaultQ.streamType() = cfg::StreamType::MULTICAST;
  *defaultQ.scheduling() = cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
  defaultQ.weight() = 1;
  defaultQ.portQueueRate() = cfg::PortQueueRate();
  defaultQ.portQueueRate()->pktsPerSec() = utility::getRange(0, 200);
  defaultQ.reservedBytes() = 5 * mmuCellBytes;
  defaultQ.sharedBytes() = 50 * mmuCellBytes;
  cpuQueues.push_back(defaultQ);

  cfg::PortQueue low;
  *low.id() = utility::kCoppLowPriQueueId;
  low.name() = "cpuQueue-low";
  *low.streamType() = cfg::StreamType::MULTICAST;
  *low.scheduling() = cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
  low.weight() = kLowCpuQueueWeight;
  low.portQueueRate() = cfg::PortQueueRate();
  low.portQueueRate()->pktsPerSec() = utility::getRange(0, 100);
  low.reservedBytes() = kLowCpuQueueReservedMmuCellNum * mmuCellBytes;
  low.sharedBytes() = kLowCpuQueueSharedMmuCellNum * mmuCellBytes;
  cpuQueues.push_back(low);
  return cpuQueues;
}
} // namespace

namespace facebook::fboss {

class BcmControlPlaneTest : public BcmCosQueueManagerTest {
 protected:
  cfg::SwitchConfig initialConfig() const override {
    auto cfg =
        utility::oneL3IntfConfig(getHwSwitch(), masterLogicalPortIds()[0]);
    // init cputTrafficPolicy here since some tests will use it
    if (!cfg.cpuTrafficPolicy()) {
      cfg.cpuTrafficPolicy() = cfg::CPUTrafficPolicyConfig();
    }
    return cfg;
  }

  std::optional<cfg::PortQueue> getCfgQueue(int queueID) override {
    std::optional<cfg::PortQueue> cfgQueue{std::nullopt};
    for (const auto& queue : *programmedCfg_.cpuQueues()) {
      if (*queue.id() == queueID) {
        cfgQueue = queue;
        break;
      }
    }
    return cfgQueue;
  }

  QueueConfig getHwQueues() override {
    return getHwSwitch()->getControlPlane()->getMulticastQueueSettings();
  }

  QueueConfig getSwQueues() override {
    return getProgrammedState()
        ->getControlPlane()
        ->cbegin()
        ->second->getQueues()
        ->impl();
  }
};

} // namespace facebook::fboss

TEST_F(BcmControlPlaneTest, CosQueueUtils) {
  if (getHwSwitch()->getBootType() == BootType::WARM_BOOT) {
    return;
  }
  // validate cosQueueManager API
  checkCosQueueAPI();

  auto port = getHwSwitch()->getPortTable()->getBcmPort(
      PortID(masterLogicalPortIds()[0]));
  // validate exception handling
  EXPECT_THROW(
      port->getQueueManager()->getQueueGPort(cfg::StreamType::ALL, 0),
      FbossError);
  EXPECT_THROW(
      port->getQueueManager()->getNumQueues(cfg::StreamType::ALL), FbossError);
}

TEST_F(BcmControlPlaneTest, DefaultCPUQueuesCheckWithoutConfig) {
  if (getHwSwitch()->getBootType() == BootType::WARM_BOOT) {
    return;
  }

  // The following check will verify BcmControlPlaneQueueManager::getNumQueues()
  int expectedQueueSize;
  if (!getAsic()->isSupported(HwAsic::Feature::L3_QOS)) {
    expectedQueueSize = utility::getMaxCPUQueueSize(getHwSwitch()->getUnit());
  } else {
    // HW queue size should match to max cpu queue size.
    expectedQueueSize = BcmControlPlaneQueueManager::kMaxMCQueueSize;
  }

  EXPECT_EQ(getHwQueues().size(), expectedQueueSize);

  // if we've never set cosq configs to any queue. The system should come up
  // with all default values.
  const auto& queues =
      getHwSwitch()->getControlPlane()->getMulticastQueueSettings();
  EXPECT_EQ(queues.size(), expectedQueueSize);
  // default queue settings
  for (const auto& queue : queues) {
    checkDefaultCosqMatch(queue);
  }
}

TEST_F(BcmControlPlaneTest, ConfigCPUQueuesSetup) {
  auto setup = [=, this]() {
    auto cfg = initialConfig();
    *cfg.cpuQueues() =
        getConfigCPUQueues(getPlatform()->getMMUCellBytes(), getAsic());
    applyNewConfig(cfg);
  };

  auto verify = [&]() { checkConfSwHwMatch(); };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmControlPlaneTest, ChangeCPULowQueueSettings) {
  QueueConfig swQueuesBefore;
  auto setup = [&]() {
    auto cfg = initialConfig();
    // first program the default cpu queues
    *cfg.cpuQueues() =
        getConfigCPUQueues(getPlatform()->getMMUCellBytes(), getAsic());
    applyNewConfig(cfg);

    for (const auto& queue : getSwQueues()) {
      swQueuesBefore.push_back(queue);
    }

    // change low queue pps from 100 to 1000. the last one is low queue
    auto& lowQueue = cfg.cpuQueues()->at(cfg.cpuQueues()->size() - 1);
    lowQueue.portQueueRate() = cfg::PortQueueRate();
    lowQueue.portQueueRate()->pktsPerSec() = utility::getRange(0, 1000);
    lowQueue.scalingFactor() = cfg::MMUScalingFactor::ONE_8TH;

    applyNewConfig(cfg);
  };

  auto verify = [&]() {
    checkConfSwHwMatch();

    const auto swQueuesAfter = getSwQueues();
    EXPECT_EQ(swQueuesBefore.size(), swQueuesAfter.size());

    // now explicitly check low-prio
    auto lowQ = swQueuesAfter[0];
    EXPECT_EQ(lowQ->getID(), utility::kCoppLowPriQueueId);
    EXPECT_EQ(lowQ->getWeight(), kLowCpuQueueWeight);
    EXPECT_EQ(
        lowQ->getReservedBytes().value(),
        kLowCpuQueueReservedMmuCellNum * getPlatform()->getMMUCellBytes());
    EXPECT_EQ(
        lowQ->getSharedBytes().value(),
        kLowCpuQueueSharedMmuCellNum * getPlatform()->getMMUCellBytes());

    auto portQueueRate = lowQ->getPortQueueRate()->toThrift();
    EXPECT_EQ(portQueueRate.getType(), cfg::PortQueueRate::Type::pktsPerSec);
    EXPECT_EQ(portQueueRate.pktsPerSec().as_const().value().minimum(), 0);
    EXPECT_EQ(portQueueRate.pktsPerSec().as_const().value().maximum(), 1000);

    EXPECT_EQ(lowQ->getScalingFactor(), cfg::MMUScalingFactor::ONE_8TH);

    // other queues shouldn't be affected
    for (int i = 1; i < swQueuesAfter.size(); i++) {
      EXPECT_TRUE(*swQueuesBefore[i] == *swQueuesAfter[i]);
    }
  };

  setup();
  verify();
}

TEST_F(BcmControlPlaneTest, DisableCPULowQueueSettings) {
  QueueConfig swQueuesBefore;
  auto setup = [&]() {
    auto cfg = initialConfig();
    // first program the default cpu queues
    *cfg.cpuQueues() =
        getConfigCPUQueues(getPlatform()->getMMUCellBytes(), getAsic());
    applyNewConfig(cfg);

    for (const auto& queue : getSwQueues()) {
      swQueuesBefore.push_back(queue);
    }

    // remove the low-prio queue
    cfg.cpuQueues()->erase(cfg.cpuQueues()->begin() + 3);
    applyNewConfig(cfg);
  };

  auto verify = [&]() {
    checkConfSwHwMatch();

    const auto swQueuesAfter = getSwQueues();
    EXPECT_EQ(swQueuesBefore.size(), swQueuesAfter.size());
    // now check low-prio, it should be using default setting
    auto lowQ = swQueuesAfter[0];
    checkDefaultCosqMatch(lowQ);

    // other queues shouldn't be affected
    for (int i = 1; i < swQueuesAfter.size(); i++) {
      EXPECT_TRUE(*swQueuesBefore[i] == *swQueuesAfter[i]);
    }
  };

  setup();
  verify();
}

TEST_F(BcmControlPlaneTest, TestBcmAndCfgReasonConversions) {
  std::map<bcm_rx_reason_e, cfg::PacketRxReason> mapping = {
      {bcmRxReasonArp, cfg::PacketRxReason::ARP},
      {bcmRxReasonDhcp, cfg::PacketRxReason::DHCP},
      {bcmRxReasonBpdu, cfg::PacketRxReason::BPDU},
      {bcmRxReasonL3MtuFail, cfg::PacketRxReason::L3_MTU_ERROR},
      {bcmRxReasonL3Slowpath, cfg::PacketRxReason::L3_SLOW_PATH},
      {bcmRxReasonL3DestMiss, cfg::PacketRxReason::L3_DEST_MISS},
      {bcmRxReasonTtl1, cfg::PacketRxReason::TTL_1},
      {bcmRxReasonNhop, cfg::PacketRxReason::CPU_IS_NHOP}};

  // Test empty reason
  bcm_rx_reasons_t emptyReasons = RxUtils::genReasons();
  EXPECT_EQ(
      bcmReasonsToConfigReason(emptyReasons), cfg::PacketRxReason::UNMATCHED);
  EXPECT_TRUE(BCM_RX_REASON_EQ(
      configRxReasonToBcmReasons(cfg::PacketRxReason::UNMATCHED),
      emptyReasons));

  // Test supported reasons
  for (auto reason : mapping) {
    bcm_rx_reasons_t bcmReasons = RxUtils::genReasons(reason.first);
    EXPECT_EQ(bcmReasonsToConfigReason(bcmReasons), reason.second);
    EXPECT_TRUE(BCM_RX_REASON_EQ(
        configRxReasonToBcmReasons(reason.second), bcmReasons));
  }
}

TEST_F(BcmControlPlaneTest, VerifyReasonToQueueMapping) {
  const ControlPlane::RxReasonToQueue cfgReasonToQueue = {
      ControlPlane::makeRxReasonToQueueEntry(
          cfg::PacketRxReason::ARP, utility::kCoppMidPriQueueId),
      ControlPlane::makeRxReasonToQueueEntry(
          cfg::PacketRxReason::DHCP, utility::kCoppMidPriQueueId),
      ControlPlane::makeRxReasonToQueueEntry(
          cfg::PacketRxReason::BPDU, utility::kCoppDefaultPriQueueId)};
  auto setup = [&]() {
    auto cfg = initialConfig();
    cfg.cpuTrafficPolicy()->rxReasonToQueueOrderedList() = cfgReasonToQueue;
    applyNewConfig(cfg);
  };

  auto verify = [&]() {
    checkConfSwHwMatch();
    // THRIFT_COPY
    const auto hwReasonToQueue =
        getHwSwitch()->getControlPlane()->getRxReasonToQueue();
    const auto swReasonToQueue = getProgrammedState()
                                     ->getControlPlane()
                                     ->cbegin()
                                     ->second->getRxReasonToQueue()
                                     ->toThrift();
    EXPECT_EQ(hwReasonToQueue, cfgReasonToQueue);
    EXPECT_EQ(swReasonToQueue, cfgReasonToQueue);
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmControlPlaneTest, WriteOverReasonToQueue) {
  ControlPlane::RxReasonToQueue cfgReasonToQueue1 = {
      ControlPlane::makeRxReasonToQueueEntry(
          cfg::PacketRxReason::ARP, utility::kCoppMidPriQueueId),
      ControlPlane::makeRxReasonToQueueEntry(
          cfg::PacketRxReason::DHCP, utility::kCoppMidPriQueueId),
      ControlPlane::makeRxReasonToQueueEntry(
          cfg::PacketRxReason::BPDU, utility::kCoppDefaultPriQueueId)};

  ControlPlane::RxReasonToQueue cfgReasonToQueue2 = {
      ControlPlane::makeRxReasonToQueueEntry(
          cfg::PacketRxReason::DHCP, utility::kCoppMidPriQueueId),
      ControlPlane::makeRxReasonToQueueEntry(
          cfg::PacketRxReason::BPDU, utility::kCoppDefaultPriQueueId)};

  auto setup = [&]() {
    auto cfg = initialConfig();
    cfg.cpuTrafficPolicy()->rxReasonToQueueOrderedList() = cfgReasonToQueue1;
    applyNewConfig(cfg);

    auto hwReasonToQueue =
        getHwSwitch()->getControlPlane()->getRxReasonToQueue();
    EXPECT_EQ(hwReasonToQueue, cfgReasonToQueue1);

    cfg = initialConfig();
    cfg.cpuTrafficPolicy()->rxReasonToQueueOrderedList() = cfgReasonToQueue2;
    applyNewConfig(cfg);

    hwReasonToQueue = getHwSwitch()->getControlPlane()->getRxReasonToQueue();
    EXPECT_EQ(hwReasonToQueue, cfgReasonToQueue2);

    // 3rd entry should be empty since the second list only has 2 entries
    EXPECT_EQ(
        getHwSwitch()->getControlPlane()->getReasonToQueueEntry(2),
        std::nullopt);
  };

  auto verify = [&]() {
    EXPECT_EQ(
        getHwSwitch()->getControlPlane()->getRxReasonToQueue(),
        cfgReasonToQueue2);

    // 3rd entry should be empty since the second list only has 2 entries
    EXPECT_EQ(
        getHwSwitch()->getControlPlane()->getReasonToQueueEntry(2),
        std::nullopt);
  };

  verifyAcrossWarmBoots(setup, verify);
}
