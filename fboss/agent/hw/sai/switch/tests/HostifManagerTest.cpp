/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/switch/SaiHostifManager.h"
#include "fboss/agent/hw/sai/switch/tests/ManagerTestBase.h"

#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"

using namespace facebook::fboss;

namespace {
HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}};
}
} // namespace

class HostifManagerTest : public ManagerTestBase {};

TEST_F(HostifManagerTest, createHostifTrap) {
  uint32_t queueId = 4;
  auto trapType = cfg::PacketRxReason::ARP;
  std::optional<HostifTrapSaiId> trapIdOpt =
      saiManagerTable->hostifManager().addHostifTrap(trapType, queueId, 1);
  ASSERT_TRUE(trapIdOpt.has_value());
  HostifTrapSaiId trapId = *trapIdOpt;
  auto trapTypeExpected = saiApiTable->hostifApi().getAttribute(
      trapId, SaiHostifTrapTraits::Attributes::TrapType{});
  auto trapPacketActionExpected = saiApiTable->hostifApi().getAttribute(
      trapId, SaiHostifTrapTraits::Attributes::PacketAction{});
  std::optional<std::pair<sai_int32_t, sai_packet_action_t>> hostifTrap =
      SaiHostifManager::packetReasonToHostifTrap(trapType, saiPlatform.get());
  ASSERT_TRUE(hostifTrap.has_value());
  auto [hostifTrapId, hostifPacketAction] = *hostifTrap;
  EXPECT_EQ(hostifTrapId, trapTypeExpected);
  EXPECT_EQ(hostifPacketAction, trapPacketActionExpected);
}

TEST_F(HostifManagerTest, unsupportedHostifTrapSkipped) {
  // An rx reason that is not mappable on this platform should be skipped
  // gracefully rather than throwing, so that a control-plane delta carrying
  // it does not crash the agent.
  uint32_t queueId = 4;
  auto trapType = cfg::PacketRxReason::L3_SLOW_PATH;
  auto& hostifManager = saiManagerTable->hostifManager();
  EXPECT_FALSE(
      SaiHostifManager::packetReasonToHostifTrap(trapType, saiPlatform.get())
          .has_value());
  // add/change/remove for an unsupported reason must not throw, and must not
  // leave any trap state behind.
  std::optional<HostifTrapSaiId> trapId;
  EXPECT_NO_THROW(trapId = hostifManager.addHostifTrap(trapType, queueId, 1));
  EXPECT_FALSE(trapId.has_value());
  EXPECT_NO_THROW(hostifManager.changeHostifTrap(trapType, queueId, 2));
  EXPECT_EQ(hostifManager.getHostifTrapHandle(trapType), nullptr);
  EXPECT_NO_THROW(hostifManager.removeHostifTrap(trapType));
  EXPECT_EQ(hostifManager.getHostifTrapHandle(trapType), nullptr);
}

TEST_F(HostifManagerTest, sharedHostifTrapGroup) {
  uint32_t queueId = 4;
  auto trapType1 = cfg::PacketRxReason::ARP;
  auto trapType2 = cfg::PacketRxReason::DHCP;
  auto& hostifManager = saiManagerTable->hostifManager();
  std::optional<HostifTrapSaiId> trapId1Opt =
      hostifManager.addHostifTrap(trapType1, queueId, 1);
  ASSERT_TRUE(trapId1Opt.has_value());
  HostifTrapSaiId trapId1 = *trapId1Opt;
  std::optional<HostifTrapSaiId> trapId2Opt =
      hostifManager.addHostifTrap(trapType2, queueId, 2);
  ASSERT_TRUE(trapId2Opt.has_value());
  HostifTrapSaiId trapId2 = *trapId2Opt;
  auto trapGroup1 = saiApiTable->hostifApi().getAttribute(
      trapId1, SaiHostifTrapTraits::Attributes::TrapGroup{});
  auto trapGroup2 = saiApiTable->hostifApi().getAttribute(
      trapId2, SaiHostifTrapTraits::Attributes::TrapGroup{});
  EXPECT_EQ(trapGroup1, trapGroup2);
  auto queueIdExpected = saiApiTable->hostifApi().getAttribute(
      HostifTrapGroupSaiId{trapGroup1},
      SaiHostifTrapGroupTraits::Attributes::Queue{});
  EXPECT_EQ(queueIdExpected, queueId);
}

TEST_F(HostifManagerTest, removeHostifTrap) {
  uint32_t queueId = 4;
  auto& hostifManager = saiManagerTable->hostifManager();
  auto trapType1 = cfg::PacketRxReason::ARP;
  auto trapType2 = cfg::PacketRxReason::DHCP;
  // create two traps using the same queue
  std::optional<HostifTrapSaiId> trapId1Opt =
      hostifManager.addHostifTrap(trapType1, queueId, 1);
  ASSERT_TRUE(trapId1Opt.has_value());
  HostifTrapSaiId trapId1 = *trapId1Opt;
  std::optional<HostifTrapSaiId> trapId2Opt =
      hostifManager.addHostifTrap(trapType2, queueId, 2);
  ASSERT_TRUE(trapId2Opt.has_value());
  HostifTrapSaiId trapId2 = *trapId2Opt;
  auto trapGroup1 = saiApiTable->hostifApi().getAttribute(
      trapId1, SaiHostifTrapTraits::Attributes::TrapGroup{});
  // remove one of them
  saiManagerTable->hostifManager().removeHostifTrap(trapType1);
  // trap group for the queue should still be valid
  auto trapGroup2 = saiApiTable->hostifApi().getAttribute(
      trapId2, SaiHostifTrapTraits::Attributes::TrapGroup{});
  EXPECT_EQ(trapGroup1, trapGroup2);
}

TEST_F(HostifManagerTest, createHostifUserDefinedTrap) {
  uint32_t queueId = 4;
  // create an user defined trap
  std::shared_ptr<SaiHostifUserDefinedTrapHandle> userDefinedTrap =
      saiManagerTable->hostifManager().ensureHostifUserDefinedTrap(queueId);
  HostifUserDefinedTrapSaiId trapId = userDefinedTrap->trap->adapterKey();
  auto trapGroupId = saiApiTable->hostifApi().getAttribute(
      trapId, SaiHostifUserDefinedTrapTraits::Attributes::TrapGroup{});
  auto queueIdExpected = saiApiTable->hostifApi().getAttribute(
      HostifTrapGroupSaiId{trapGroupId},
      SaiHostifTrapGroupTraits::Attributes::Queue{});
  // user defined trap's trapGroup should point to queueId
  EXPECT_EQ(queueIdExpected, queueId);
}

TEST_F(HostifManagerTest, sharedHostifUserDefinedTrapGroup) {
  uint32_t queueId = 4;
  auto trapType = cfg::PacketRxReason::ARP;
  auto& hostifManager = saiManagerTable->hostifManager();
  // create a normal trap and user defined trap using the same queue
  std::optional<HostifTrapSaiId> trapId1Opt =
      hostifManager.addHostifTrap(trapType, queueId, 1);
  ASSERT_TRUE(trapId1Opt.has_value());
  HostifTrapSaiId trapId1 = *trapId1Opt;
  std::shared_ptr<SaiHostifUserDefinedTrapHandle> userDefinedTrap =
      saiManagerTable->hostifManager().ensureHostifUserDefinedTrap(queueId);
  HostifUserDefinedTrapSaiId trapId2 = userDefinedTrap->trap->adapterKey();
  auto trapGroup1 = saiApiTable->hostifApi().getAttribute(
      trapId1, SaiHostifTrapTraits::Attributes::TrapGroup{});
  auto trapGroup2 = saiApiTable->hostifApi().getAttribute(
      trapId2, SaiHostifUserDefinedTrapTraits::Attributes::TrapGroup{});
  // both traps should point to the same trap group
  EXPECT_EQ(trapGroup1, trapGroup2);
  auto queueIdExpected = saiApiTable->hostifApi().getAttribute(
      HostifTrapGroupSaiId{trapGroup1},
      SaiHostifTrapGroupTraits::Attributes::Queue{});
  // trap group should point to queueId
  EXPECT_EQ(queueIdExpected, queueId);
  // call ensureHostifUserDefinedTrap() using the same queueId again
  std::shared_ptr<SaiHostifUserDefinedTrapHandle> userDefinedTrap2 =
      saiManagerTable->hostifManager().ensureHostifUserDefinedTrap(queueId);
  HostifUserDefinedTrapSaiId trapId3 = userDefinedTrap2->trap->adapterKey();
  // shoud return the old trapId that is already programmed
  EXPECT_EQ(trapId2, trapId3);
}

TEST_F(HostifManagerTest, removeHostifUserDefinedTrap) {
  uint32_t queueId = 4;
  auto trapType = cfg::PacketRxReason::ARP;
  auto& hostifManager = saiManagerTable->hostifManager();
  // create a normal trap and user defined trap using the same queue
  std::optional<HostifTrapSaiId> trapId1Opt =
      hostifManager.addHostifTrap(trapType, queueId, 1);
  ASSERT_TRUE(trapId1Opt.has_value());
  HostifTrapSaiId trapId1 = *trapId1Opt;
  std::shared_ptr<SaiHostifUserDefinedTrapHandle> userDefinedTrap =
      saiManagerTable->hostifManager().ensureHostifUserDefinedTrap(queueId);
  HostifUserDefinedTrapSaiId trapId2 = userDefinedTrap->trap->adapterKey();
  auto trapGroup1 = saiApiTable->hostifApi().getAttribute(
      trapId1, SaiHostifTrapTraits::Attributes::TrapGroup{});
  // remove one of them
  saiManagerTable->hostifManager().removeHostifTrap(trapType);
  // trap group for the queue should still be valid
  auto trapGroup2 = saiApiTable->hostifApi().getAttribute(
      trapId2, SaiHostifUserDefinedTrapTraits::Attributes::TrapGroup{});
  EXPECT_EQ(trapGroup1, trapGroup2);
  // remove user defined trap
  userDefinedTrap.reset();
  // trap group should be removed
  EXPECT_THROW(
      saiApiTable->hostifApi().getAttribute(
          HostifTrapGroupSaiId{trapGroup2},
          SaiHostifTrapGroupTraits::Attributes::Queue{}),
      std::out_of_range);
}

TEST_F(HostifManagerTest, addCpuQueueAndCheckStats) {
  auto prevState = std::make_shared<SwitchState>();
  auto newState = std::make_shared<SwitchState>();
  auto newControlPlane = std::make_shared<ControlPlane>();
  std::vector<uint8_t> queueIds = {1, 2, 3, 4};
  auto queueConfig = makeQueueConfig({queueIds}, cfg::StreamType::MULTICAST);
  newControlPlane->resetQueues(queueConfig);
  auto newMultiControlPlane = std::make_shared<MultiControlPlane>();
  newMultiControlPlane->addNode(scope().matcherString(), newControlPlane);
  newState->resetControlPlane(newMultiControlPlane);
  auto delta = StateDelta(prevState, newState);
  saiManagerTable->hostifManager().processHostifDelta(
      delta.getControlPlaneDelta());
  saiManagerTable->hostifManager().updateStats();

  const auto& cpuStat = saiManagerTable->hostifManager().getCpuFb303Stats();
  for (const auto& portQueue : queueConfig) {
    for (auto statKey : HwCpuFb303Stats::kQueueMonotonicCounterStatKeys()) {
      EXPECT_TRUE(
          facebook::fbData->getStatMap()->contains(
              HwCpuFb303Stats::statName(
                  statKey, portQueue->getID(), *portQueue->getName())));
      EXPECT_EQ(
          cpuStat.getCounterLastIncrement(
              HwCpuFb303Stats::statName(
                  statKey, portQueue->getID(), *portQueue->getName())),
          0);
    }
  }
}

TEST_F(HostifManagerTest, removeCpuQueueAndCheckStats) {
  auto newControlPlane = std::make_shared<ControlPlane>();
  std::vector<uint8_t> queueIds = {1, 2, 3, 4};
  auto queueConfig = makeQueueConfig({queueIds}, cfg::StreamType::MULTICAST);
  newControlPlane->resetQueues(queueConfig);
  auto prevState = std::make_shared<SwitchState>();
  auto newState = std::make_shared<SwitchState>();
  auto newMultiSwitchControlPlane = std::make_shared<MultiControlPlane>();
  newMultiSwitchControlPlane->addNode(scope().matcherString(), newControlPlane);
  newState->resetControlPlane(newMultiSwitchControlPlane);
  auto delta0 = StateDelta(prevState, newState);
  saiManagerTable->hostifManager().processHostifDelta(
      delta0.getControlPlaneDelta());

  auto newNewControlPlane = newControlPlane->clone();
  std::vector<uint8_t> newQueueIds = {1, 2};
  auto newQueueConfig =
      makeQueueConfig({newQueueIds}, cfg::StreamType::MULTICAST);
  newNewControlPlane->resetQueues(newQueueConfig);
  auto newNewMultiSwitchControlPlane = std::make_shared<MultiControlPlane>();
  newNewMultiSwitchControlPlane->addNode(
      scope().matcherString(), newNewControlPlane);
  auto newNewState = newState->clone();
  newNewState->resetControlPlane(newNewMultiSwitchControlPlane);
  auto delta1 = StateDelta(newState, newNewState);
  saiManagerTable->hostifManager().processHostifDelta(
      delta1.getControlPlaneDelta());
  saiManagerTable->hostifManager().updateStats();

  const auto& cpuStat = saiManagerTable->hostifManager().getCpuFb303Stats();
  for (auto queueId : {1, 2}) {
    auto queueName = folly::to<std::string>("queue", queueId);
    for (auto statKey : HwCpuFb303Stats::kQueueMonotonicCounterStatKeys()) {
      EXPECT_TRUE(
          facebook::fbData->getStatMap()->contains(
              HwCpuFb303Stats::statName(statKey, queueId, queueName)));
      EXPECT_EQ(
          cpuStat.getCounterLastIncrement(
              HwCpuFb303Stats::statName(statKey, queueId, queueName)),
          0);
    }
  }
  for (auto queueId : {3, 4}) {
    for (auto statKey : HwCpuFb303Stats::kQueueMonotonicCounterStatKeys()) {
      EXPECT_FALSE(
          facebook::fbData->getStatMap()->contains(
              HwCpuFb303Stats::statName(
                  statKey, queueId, folly::to<std::string>("queue", queueId))));
    }
  }
}

TEST_F(HostifManagerTest, changeCpuQueueAndCheckStats) {
  auto newControlPlane = std::make_shared<ControlPlane>();
  auto queueConfig = makeQueueConfig({1}, cfg::StreamType::MULTICAST);
  newControlPlane->resetQueues(queueConfig);
  auto prevState = std::make_shared<SwitchState>();
  auto newState = std::make_shared<SwitchState>();
  auto newMultiControlPlane = std::make_shared<MultiControlPlane>();
  newMultiControlPlane->addNode(scope().matcherString(), newControlPlane);
  newState->resetControlPlane(newMultiControlPlane);
  auto delta0 = StateDelta(prevState, newState);
  saiManagerTable->hostifManager().processHostifDelta(
      delta0.getControlPlaneDelta());

  auto newNewControlPlane = newControlPlane->clone();
  auto newQueueConfig = makeQueueConfig({1}, cfg::StreamType::MULTICAST);
  newQueueConfig[0]->setName("high");
  newNewControlPlane->resetQueues(newQueueConfig);
  auto newNewState = newState->clone();
  auto newNewMultiSwitchControlPlane = std::make_shared<MultiControlPlane>();
  newNewMultiSwitchControlPlane->addNode(
      scope().matcherString(), newNewControlPlane);
  newNewState->resetControlPlane(newNewMultiSwitchControlPlane);
  auto delta1 = StateDelta(newState, newNewState);
  saiManagerTable->hostifManager().processHostifDelta(
      delta1.getControlPlaneDelta());
  saiManagerTable->hostifManager().updateStats();

  auto oldQueueName = folly::to<std::string>("queue", 1);
  const auto& cpuStat = saiManagerTable->hostifManager().getCpuFb303Stats();
  for (auto statKey : HwCpuFb303Stats::kQueueMonotonicCounterStatKeys()) {
    EXPECT_TRUE(
        facebook::fbData->getStatMap()->contains(
            HwCpuFb303Stats::statName(statKey, 1, "high")));
    EXPECT_FALSE(
        facebook::fbData->getStatMap()->contains(
            HwCpuFb303Stats::statName(statKey, 1, oldQueueName)));
    EXPECT_EQ(
        cpuStat.getCounterLastIncrement(
            HwCpuFb303Stats::statName(statKey, 1, "high")),
        0);
  }
}

TEST_F(HostifManagerTest, checkHostifPriority) {
  auto newControlPlane = std::make_shared<ControlPlane>();
  std::vector<std::pair<cfg::PacketRxReason, uint16_t>>
      rxReasonToQueueMappings = {
          std::pair(cfg::PacketRxReason::ARP, 1),
          std::pair(cfg::PacketRxReason::ARP_RESPONSE, 2),
          std::pair(cfg::PacketRxReason::BGP, 3),
      };
  std::vector<cfg::PacketRxReasonToQueue> rxReasonToQueues;
  for (auto rxEntry : rxReasonToQueueMappings) {
    auto rxReasonToQueue = cfg::PacketRxReasonToQueue();
    rxReasonToQueue.rxReason() = rxEntry.first;
    rxReasonToQueue.queueId() = rxEntry.second;
    rxReasonToQueues.push_back(rxReasonToQueue);
  }
  newControlPlane->resetRxReasonToQueue(rxReasonToQueues);
  auto prevState = std::make_shared<SwitchState>();
  auto newState = std::make_shared<SwitchState>();
  auto newMultiControlPlane = std::make_shared<MultiControlPlane>();
  newMultiControlPlane->addNode(scope().matcherString(), newControlPlane);
  newState->resetControlPlane(newMultiControlPlane);
  auto delta0 = StateDelta(prevState, newState);
  saiManagerTable->hostifManager().processHostifDelta(
      delta0.getControlPlaneDelta());
  int index = 0;
  for (auto rxEntry : rxReasonToQueueMappings) {
    auto hostifTrapHandle =
        saiManagerTable->hostifManager().getHostifTrapHandle(rxEntry.first);
    EXPECT_TRUE(hostifTrapHandle);
    auto priority = saiApiTable->hostifApi().getAttribute(
        hostifTrapHandle->trap->adapterKey(),
        SaiHostifTrapTraits::Attributes::TrapPriority{});
    EXPECT_EQ(priority, rxReasonToQueueMappings.size() - index++);
  }
  // Remove two traps and ensure the priority is reassigned.
  rxReasonToQueues.clear();
  auto rxReasonToQueue = cfg::PacketRxReasonToQueue();
  rxReasonToQueue.rxReason() = cfg::PacketRxReason::ARP_RESPONSE;
  rxReasonToQueue.queueId() = 1;
  rxReasonToQueues.push_back(rxReasonToQueue);
  auto newControlPlaneNew = std::make_shared<ControlPlane>();
  newControlPlaneNew->resetRxReasonToQueue(rxReasonToQueues);
  auto newNewState = std::make_shared<SwitchState>();
  auto newMultiControlPlaneNew = std::make_shared<MultiControlPlane>();
  newMultiControlPlaneNew->addNode(scope().matcherString(), newControlPlaneNew);
  newNewState->resetControlPlane(newMultiControlPlaneNew);
  auto delta = StateDelta(newState, newNewState);
  saiManagerTable->hostifManager().processHostifDelta(
      delta.getControlPlaneDelta());
  auto hostifTrapHandle = saiManagerTable->hostifManager().getHostifTrapHandle(
      cfg::PacketRxReason::ARP_RESPONSE);
  EXPECT_TRUE(hostifTrapHandle);
  auto priority = saiApiTable->hostifApi().getAttribute(
      hostifTrapHandle->trap->adapterKey(),
      SaiHostifTrapTraits::Attributes::TrapPriority{});
  EXPECT_EQ(priority, 1);
}

TEST_F(HostifManagerTest, resetSchedulerOid) {
  // Create queue 1 and a scheduler config with Stream Type ALL
  auto newControlPlane = std::make_shared<ControlPlane>();
  auto queueConfig = makeQueueConfig({1}, cfg::StreamType::MULTICAST);
  newControlPlane->resetQueues(queueConfig);
  auto prevState = std::make_shared<SwitchState>();
  auto newState = std::make_shared<SwitchState>();
  auto newMultiControlPlane = std::make_shared<MultiControlPlane>();
  newMultiControlPlane->addNode(scope().matcherString(), newControlPlane);
  newState->resetControlPlane(newMultiControlPlane);

  // Apply the config changes using processHostifDelta
  auto delta = StateDelta(prevState, newState);
  saiManagerTable->hostifManager().processHostifDelta(
      delta.getControlPlaneDelta());
  const auto queueHandle = saiManagerTable->hostifManager().getQueueHandle(
      std::pair(1, cfg::StreamType::MULTICAST));

  // Ensure scheduler is created with a valid OID.
  EXPECT_TRUE(queueHandle->scheduler);

  // Ensure queue scheduler profile id is set with a valid OID.
  auto schedulerProfileId = saiApiTable->queueApi().getAttribute(
      queueHandle->queue->adapterKey(),
      SaiQueueTraits::Attributes::SchedulerProfileId{});
  EXPECT_EQ(queueHandle->scheduler->adapterKey(), schedulerProfileId);

  // Free the scheduler and ensure queue scheduler profile OID is reset.
  queueHandle->resetQueue();
  schedulerProfileId = saiApiTable->queueApi().getAttribute(
      queueHandle->queue->adapterKey(),
      SaiQueueTraits::Attributes::SchedulerProfileId{});
  EXPECT_EQ(schedulerProfileId, SAI_NULL_OBJECT_ID);
}
