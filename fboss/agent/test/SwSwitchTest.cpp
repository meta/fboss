/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/agent/ArpHandler.h"
#include "fboss/agent/FbossHwUpdateError.h"
#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/MultiSwitchFb303Stats.h"
#include "fboss/agent/NeighborUpdater.h"
#include "fboss/agent/PortStats.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/ValidateStateUpdate.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/state/ArpTable.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/LlrConfig.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/StateUtils.h"
#include "fboss/agent/state/SwitchState.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/test/CounterCache.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include <folly/MacAddress.h>
#include <folly/ScopeGuard.h>
#include <folly/portability/GFlags.h>
#include <folly/synchronization/Baton.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include <algorithm>
#include <thread>

using namespace facebook::fboss;
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::MacAddress;
using std::string;
using ::testing::_;
using ::testing::ByRef;
using ::testing::Eq;
using ::testing::Return;

namespace {
HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}};
}
} // namespace

class SwSwitchTest : public ::testing::Test {
 public:
  void SetUp() override {
    // Setup a default state object
    auto state = testStateA();
    state->publish();
    handle = createTestHandle(state);
    sw = handle->getSw();
    sw->initialConfigApplied(std::chrono::steady_clock::now());
    waitForStateUpdates(sw);
  }

  void TearDown() override {
    sw = nullptr;
    handle.reset();
  }

  SwSwitch* sw{nullptr};
  std::unique_ptr<HwTestHandle> handle{nullptr};
};

TEST_F(SwSwitchTest, GetPortStats) {
  // get port5 portStats for the first time
  EXPECT_EQ(sw->stats()->getPortStats()->size(), 0);
  auto portStats = sw->portStats(PortID(5));
  EXPECT_EQ(sw->stats()->getPortStats()->size(), 1);
  EXPECT_EQ(
      portStats->getPortName(), sw->getState()->getPort(PortID(5))->getName());

  // get port5 directly from PortStatsMap
  portStats = sw->portStats(PortID(5));
  EXPECT_EQ(sw->stats()->getPortStats()->size(), 1);
  EXPECT_EQ(
      portStats->getPortName(), sw->getState()->getPort(PortID(5))->getName());

  // get port0
  portStats = sw->portStats(PortID(0));
  EXPECT_EQ(sw->stats()->getPortStats()->size(), 2);
  EXPECT_EQ(portStats->getPortName(), "port0");
}

TEST_F(SwSwitchTest, PhyInfoLinkFlapCountFromLinkScanCallbacks) {
  const PortID portId{1};
  phy::PhyInfo phyInfo;
  phyInfo.state() = phy::PhyState();
  phyInfo.state()->name() = "port1";
  phyInfo.stats() = phy::PhyStats();
  ON_CALL(*getMockHw(sw), updateAllPhyInfoImpl())
      .WillByDefault(Return(std::map<PortID, phy::PhyInfo>{{portId, phyInfo}}));

  sw->linkStateChanged(portId, true, cfg::PortType::INTERFACE_PORT);
  waitForStateUpdates(sw);

  CounterCache counters(sw);
  counters.update();
  const auto linkStateFlapCount = counters.value("port1.link_state.flap.sum");

  sw->updateStats();
  std::vector<PortID> portIds{portId};
  auto phyInfos = sw->getIPhyInfo(portIds);
  ASSERT_EQ(phyInfos.size(), 1);
  EXPECT_EQ(
      phyInfos.at(portId).stats()->linkFlapCount().value(), linkStateFlapCount);

  auto agentStats = sw->fillFsdbStats();
  EXPECT_EQ(
      agentStats.phyStats()->at("port1").linkFlapCount().value(),
      linkStateFlapCount);
}

ACTION(ThrowException) {
  throw std::exception();
}

TEST_F(SwSwitchTest, VerifyEcmpWidthChangeRejected) {
  ON_CALL(*getMockHw(sw), isValidStateUpdate(_))
      .WillByDefault(testing::Return(true));

  auto withEcmpWidth = [](const std::shared_ptr<SwitchState>& base,
                          std::optional<int32_t> width) {
    auto state = base->clone();
    auto settings = utility::getFirstNodeIf(state->getSwitchSettings());
    settings->modify(&state)->setEcmpWidth(width);
    state->publish();
    return state;
  };

  auto baseState = withEcmpWidth(sw->getState(), 64);

  // Unchanged width is valid.
  EXPECT_TRUE(sw->isValidStateUpdate(
      StateDelta(baseState, withEcmpWidth(baseState, 64))));

  // First-time set (old side unset, as on coldboot) is valid.
  auto unsetState = withEcmpWidth(sw->getState(), std::nullopt);
  EXPECT_TRUE(sw->isValidStateUpdate(
      StateDelta(unsetState, withEcmpWidth(unsetState, 128))));

  // Changing the width on a running agent is rejected regardless of boot type.
  EXPECT_FALSE(sw->isValidStateUpdate(
      StateDelta(baseState, withEcmpWidth(baseState, 128))));
}

namespace {
std::shared_ptr<LlrConfig> makeLlrConfig(
    const std::string& name,
    int32_t outstandingBytesMax = 105800) {
  auto llr = std::make_shared<LlrConfig>(name);
  llr->setOutstandingFramesMax(1654);
  llr->setOutstandingBytesMax(outstandingBytesMax);
  llr->setReplayTimerMax(5000);
  llr->setReplayCountMax(2);
  llr->setPcsLostTimeout(1000);
  llr->setDataAgeTimeout(200000);
  llr->setInitFrameAction(cfg::LlrFrameAction::BEST_EFFORT);
  llr->setFlushFrameAction(cfg::LlrFrameAction::BLOCK);
  llr->setReInitOnFlush(true);
  llr->setCtlosTargetSpacing(2048);
  return llr;
}
} // namespace

TEST_F(SwSwitchTest, VerifyLlrConfigChangeRejected) {
  ON_CALL(*getMockHw(sw), isValidStateUpdate(_))
      .WillByDefault(testing::Return(true));

  const PortID kTestPort{1};
  auto withAdminState = [kTestPort](
                            const std::shared_ptr<SwitchState>& base,
                            cfg::PortState adminState) {
    auto state = base->clone();
    state->getPorts()->getNodeIf(kTestPort)->modify(&state)->setAdminState(
        adminState);
    state->publish();
    return state;
  };
  // description gives the port a field to change independently of LLR, so the
  // port lands in the delta even when its LLR config is untouched.
  auto withLlr = [kTestPort](
                     const std::shared_ptr<SwitchState>& base,
                     const std::string& name,
                     const std::string& description,
                     cfg::PortState adminState,
                     int32_t outstandingBytesMax = 105800) {
    auto state = base->clone();
    auto port = state->getPorts()->getNodeIf(kTestPort)->modify(&state);
    port->setLlrConfigName(name);
    port->setLlrConfig(makeLlrConfig(name, outstandingBytesMax));
    port->setDescription(description);
    port->setAdminState(adminState);
    state->publish();
    return state;
  };
  constexpr auto kUp = cfg::PortState::ENABLED;
  constexpr auto kDown = cfg::PortState::DISABLED;

  auto enabledNoLlr = withAdminState(sw->getState(), kUp);
  auto disabledNoLlr = withAdminState(sw->getState(), kDown);
  auto enabledLlr = withLlr(enabledNoLlr, "llr_default", "a", kUp);
  auto disabledLlr = withLlr(disabledNoLlr, "llr_default", "a", kDown);

  // A rebuilt profile node holding identical config is not a change, so it is
  // allowed even on an enabled port. updateLlrConfigs replaces every node in
  // the map whenever any profile changes, so the port ends up pointing at a new
  // node with the same contents.
  EXPECT_TRUE(sw->isValidStateUpdate(
      StateDelta(enabledLlr, withLlr(enabledLlr, "llr_default", "b", kUp))));

  // The one legal transition is the first bind on a port that is still down,
  // which is how the coldboot config arrives. The port may stay down...
  EXPECT_TRUE(sw->isValidStateUpdate(StateDelta(disabledNoLlr, disabledLlr)));

  // ...or come up in the same update, since SaiPortManager holds the enable
  // until the profile is attached.
  EXPECT_TRUE(sw->isValidStateUpdate(StateDelta(
      disabledNoLlr, withLlr(disabledNoLlr, "llr_default", "a", kUp))));

  // Binding onto a port that is already up is rejected: the attach would land
  // on a port hardware considers enabled.
  EXPECT_FALSE(sw->isValidStateUpdate(StateDelta(enabledNoLlr, enabledLlr)));

  // Once a port carries a profile, nothing may change it. Not a different
  // profile, not a retune under the same name...
  EXPECT_FALSE(sw->isValidStateUpdate(
      StateDelta(enabledLlr, withLlr(enabledLlr, "llr_other", "a", kUp))));
  EXPECT_FALSE(sw->isValidStateUpdate(StateDelta(
      enabledLlr, withLlr(enabledLlr, "llr_default", "a", kUp, 64000))));

  // ...and not unbinding it.
  EXPECT_FALSE(sw->isValidStateUpdate(StateDelta(enabledLlr, disabledNoLlr)));

  // Draining the port in the same update does not make any of those legal. The
  // SDK refuses the rebind with the port admin disabled and both LLR modes
  // cleared beforehand (CS00012478409), so there is nothing to gain by allowing
  // it and finding out from hardware.
  EXPECT_FALSE(sw->isValidStateUpdate(
      StateDelta(enabledLlr, withLlr(enabledLlr, "llr_other", "a", kDown))));
  EXPECT_FALSE(sw->isValidStateUpdate(
      StateDelta(disabledLlr, withLlr(disabledLlr, "llr_other", "a", kDown))));
}

TEST_F(SwSwitchTest, VerifyCbfcSenderChangeRejected) {
  ON_CALL(*getMockHw(sw), isValidStateUpdate(_))
      .WillByDefault(testing::Return(true));

  const PortID kTestPort{1};
  // description gives the port a field to change independently of CBFC, so the
  // port lands in the delta even when its CBFC values are untouched.
  auto withCbfc = [kTestPort](
                      const std::shared_ptr<SwitchState>& base,
                      const std::string& description,
                      int16_t creditSize = 56,
                      int16_t packetOverhead = 16,
                      int64_t creditLimit = 8192) {
    auto state = base->clone();
    auto port = state->getPorts()->getNodeIf(kTestPort)->modify(&state);
    port->setCbfcConfigName("cbfc");
    port->setCbfcSenderCreditSize(creditSize);
    port->setCbfcSenderPacketOverhead(packetOverhead);
    port->setCbfcSenderCreditLimit(creditLimit);
    port->setDescription(description);
    state->publish();
    return state;
  };
  auto withoutCbfc = [kTestPort](const std::shared_ptr<SwitchState>& base) {
    auto state = base->clone();
    auto port = state->getPorts()->getNodeIf(kTestPort)->modify(&state);
    port->setCbfcConfigName(std::nullopt);
    port->setCbfcSenderCreditSize(std::nullopt);
    port->setCbfcSenderPacketOverhead(std::nullopt);
    port->setCbfcSenderCreditLimit(std::nullopt);
    state->publish();
    return state;
  };

  auto noCbfc = withoutCbfc(sw->getState());
  auto cbfc = withCbfc(noCbfc, "a");

  // The first bind is how CBFC arrives, on a coldboot or a running agent.
  EXPECT_TRUE(sw->isValidStateUpdate(StateDelta(noCbfc, cbfc)));
  // Another port field changing leaves the sender values alone.
  EXPECT_TRUE(sw->isValidStateUpdate(StateDelta(cbfc, withCbfc(cbfc, "b"))));
  // Removing CBFC from the port needs a coldboot, as for LLR.
  EXPECT_FALSE(sw->isValidStateUpdate(StateDelta(cbfc, withoutCbfc(cbfc))));

  // Changing any sender value on a port that keeps CBFC needs a coldboot.
  EXPECT_FALSE(
      sw->isValidStateUpdate(StateDelta(cbfc, withCbfc(cbfc, "a", 32))));
  EXPECT_FALSE(
      sw->isValidStateUpdate(StateDelta(cbfc, withCbfc(cbfc, "a", 56, 0))));
  EXPECT_FALSE(sw->isValidStateUpdate(
      StateDelta(cbfc, withCbfc(cbfc, "a", 56, 16, 16384))));
}

TEST_F(SwSwitchTest, VerifyDuplicateAclPriorityRejected) {
  ON_CALL(*getMockHw(sw), isValidStateUpdate(_))
      .WillByDefault(testing::Return(true));

  // 1. Two acls at distinct priorities are fine.
  auto stateV0 = std::make_shared<SwitchState>();
  stateV0->publish();

  auto withAcls = [](const std::shared_ptr<SwitchState>& base,
                     int priorityA,
                     int priorityB) {
    auto state = base->clone();
    auto acls = state->getAcls()->modify(&state);
    auto aclA = std::make_shared<AclEntry>(priorityA, std::string("aclA"));
    aclA->setDscp(0x24);
    acls->addNode(aclA, scope());
    auto aclB = std::make_shared<AclEntry>(priorityB, std::string("aclB"));
    aclB->setDscp(0x28);
    acls->addNode(aclB, scope());
    state->publish();
    return state;
  };

  EXPECT_TRUE(
      sw->isValidStateUpdate(StateDelta(stateV0, withAcls(stateV0, 1, 2))));

  // 2. Two acls at one priority are indistinguishable in hardware, so the
  // update is rejected rather than silently collapsing onto one entry.
  EXPECT_FALSE(
      sw->isValidStateUpdate(StateDelta(stateV0, withAcls(stateV0, 1, 1))));

  // 3. Only the PBR table may share the PBR priority.
  EXPECT_FALSE(sw->isValidStateUpdate(StateDelta(
      stateV0,
      withAcls(stateV0, FLAGS_pbr_acl_priority, FLAGS_pbr_acl_priority))));
}

TEST_F(SwSwitchTest, VerifyIsValidStateUpdate) {
  ON_CALL(*getMockHw(sw), isValidStateUpdate(_))
      .WillByDefault(testing::Return(true));

  // ACL with a qualifier should succeed validation
  auto stateV0 = std::make_shared<SwitchState>();
  stateV0->publish();

  auto stateV1 = stateV0->clone();
  auto acls = stateV1->getAcls()->modify(&stateV1);

  auto aclEntry0 = std::make_shared<AclEntry>(0, std::string("acl0"));
  aclEntry0->setDscp(0x24);
  acls->addNode(aclEntry0, scope());

  stateV1->publish();

  EXPECT_TRUE(sw->isValidStateUpdate(StateDelta(stateV0, stateV1)));

  // Empty ACL matcher validity is ASIC-dependent.
  auto stateV2 = stateV0->clone();
  auto acls2 = stateV2->getAcls()->modify(&stateV2);

  auto aclEntry1 = std::make_shared<AclEntry>(0, std::string("acl1"));
  acls2->addNode(aclEntry1, scope());

  stateV2->publish();

  const auto emptyAclMatcherSupported =
      sw->getHwAsicTable()->isFeatureSupportedOnAllAsic(
          HwAsic::Feature::EMPTY_ACL_MATCHER);
  EXPECT_EQ(
      sw->isValidStateUpdate(StateDelta(stateV0, stateV2)),
      emptyAclMatcherSupported);

  // PortQueue with valid WRED probability
  auto stateV3 = stateV0->clone();
  auto portMap0 = stateV3->getPorts()->modify(&stateV3);
  state::PortFields portFields0;
  portFields0.portId() = PortID(0);
  portFields0.portName() = "port0";
  auto port0 = std::make_shared<Port>(std::move(portFields0));
  auto portQueue0 = std::make_shared<PortQueue>(static_cast<uint8_t>(0));
  cfg::ActiveQueueManagement aqm0;
  cfg::LinearQueueCongestionDetection lqcd0;
  lqcd0.minimumLength() = 0;
  lqcd0.maximumLength() = 0;
  lqcd0.probability() = 50;
  aqm0.detection()->linear() = lqcd0;
  aqm0.behavior() = cfg::QueueCongestionBehavior::EARLY_DROP;
  portQueue0->resetAqms({aqm0});
  std::vector<std::shared_ptr<PortQueue>> portQueues = {portQueue0};
  port0->resetPortQueues(portQueues);
  portMap0->addNode(port0, scope());

  stateV3->publish();

  EXPECT_TRUE(sw->isValidStateUpdate(StateDelta(stateV0, stateV3)));

  // PortQueue with valid ECN probability (50%) - valid when
  // ECN_PROBABILISTIC_MARKING supported MockAsic returns true for
  // ECN_PROBABILISTIC_MARKING by default
  auto stateV4 = stateV0->clone();
  auto portMap1 = stateV4->getPorts()->modify(&stateV4);
  state::PortFields portFields1;
  portFields1.portId() = PortID(1);
  portFields1.portName() = "port1";
  auto port1 = std::make_shared<Port>(std::move(portFields1));
  auto portQueue1 = std::make_shared<PortQueue>(static_cast<uint8_t>(1));
  cfg::ActiveQueueManagement aqm1;
  cfg::LinearQueueCongestionDetection lqcd1;
  lqcd1.minimumLength() = 0;
  lqcd1.maximumLength() = 0;
  lqcd1.probability() = 50;
  aqm1.detection()->linear() = lqcd1;
  aqm1.behavior() = cfg::QueueCongestionBehavior::ECN;
  portQueue1->resetAqms({aqm1});
  portQueues = {portQueue1};
  port1->resetPortQueues(portQueues);
  portMap1->addNode(port1, scope());

  stateV4->publish();

  EXPECT_TRUE(sw->isValidStateUpdate(StateDelta(stateV0, stateV4)));

  // PortQueue with ECN probability 100% - always valid regardless of feature
  // support
  auto stateV5 = stateV0->clone();
  auto portMap2 = stateV5->getPorts()->modify(&stateV5);
  state::PortFields portFields2;
  portFields2.portId() = PortID(2);
  portFields2.portName() = "port2";
  auto port2 = std::make_shared<Port>(std::move(portFields2));
  auto portQueue2 = std::make_shared<PortQueue>(static_cast<uint8_t>(0));
  cfg::ActiveQueueManagement aqm2;
  cfg::LinearQueueCongestionDetection lqcd2;
  lqcd2.minimumLength() = 0;
  lqcd2.maximumLength() = 0;
  lqcd2.probability() = 100;
  aqm2.detection()->linear() = lqcd2;
  aqm2.behavior() = cfg::QueueCongestionBehavior::ECN;
  portQueue2->resetAqms({aqm2});
  std::vector<std::shared_ptr<PortQueue>> portQueues2 = {portQueue2};
  port2->resetPortQueues(portQueues2);
  portMap2->addNode(port2, scope());

  stateV5->publish();

  EXPECT_TRUE(sw->isValidStateUpdate(StateDelta(stateV0, stateV5)));

  // PortQueue with ECN probability 0% - invalid (must be >0)
  auto stateV6 = stateV0->clone();
  auto portMap3 = stateV6->getPorts()->modify(&stateV6);
  state::PortFields portFields3;
  portFields3.portId() = PortID(3);
  portFields3.portName() = "port3";
  auto port3 = std::make_shared<Port>(std::move(portFields3));
  auto portQueue3 = std::make_shared<PortQueue>(static_cast<uint8_t>(0));
  cfg::ActiveQueueManagement aqm3;
  cfg::LinearQueueCongestionDetection lqcd3;
  lqcd3.minimumLength() = 0;
  lqcd3.maximumLength() = 0;
  lqcd3.probability() = 0;
  aqm3.detection()->linear() = lqcd3;
  aqm3.behavior() = cfg::QueueCongestionBehavior::ECN;
  portQueue3->resetAqms({aqm3});
  std::vector<std::shared_ptr<PortQueue>> portQueues3 = {portQueue3};
  port3->resetPortQueues(portQueues3);
  portMap3->addNode(port3, scope());

  stateV6->publish();

  EXPECT_FALSE(sw->isValidStateUpdate(StateDelta(stateV0, stateV6)));

  // PortQueue with ECN probability 101% - invalid (must be <=100)
  auto stateV7 = stateV0->clone();
  auto portMap4 = stateV7->getPorts()->modify(&stateV7);
  state::PortFields portFields4;
  portFields4.portId() = PortID(4);
  portFields4.portName() = "port4";
  auto port4 = std::make_shared<Port>(std::move(portFields4));
  auto portQueue4 = std::make_shared<PortQueue>(static_cast<uint8_t>(0));
  cfg::ActiveQueueManagement aqm4;
  cfg::LinearQueueCongestionDetection lqcd4;
  lqcd4.minimumLength() = 0;
  lqcd4.maximumLength() = 0;
  lqcd4.probability() = 101;
  aqm4.detection()->linear() = lqcd4;
  aqm4.behavior() = cfg::QueueCongestionBehavior::ECN;
  portQueue4->resetAqms({aqm4});
  std::vector<std::shared_ptr<PortQueue>> portQueues4 = {portQueue4};
  port4->resetPortQueues(portQueues4);
  portMap4->addNode(port4, scope());

  stateV7->publish();

  EXPECT_FALSE(sw->isValidStateUpdate(StateDelta(stateV0, stateV7)));
}

TEST_F(SwSwitchTest, VerifyEcnValidationWhenFeatureNotSupported) {
  // This test verifies ECN validation when ECN_PROBABILISTIC_MARKING is NOT
  // supported. In this case, only 100% probability is allowed.
  ON_CALL(*getMockHw(sw), isValidStateUpdate(_))
      .WillByDefault(testing::Return(true));

  auto stateV0 = std::make_shared<SwitchState>();
  stateV0->publish();

  // Helper to create a port with ECN configuration
  auto createPortWithEcnProbability =
      [&](PortID portId, const std::string& portName, int probability) {
        state::PortFields portFields;
        portFields.portId() = portId;
        portFields.portName() = portName;
        auto port = std::make_shared<Port>(std::move(portFields));
        auto portQueue = std::make_shared<PortQueue>(static_cast<uint8_t>(0));
        cfg::ActiveQueueManagement aqm;
        cfg::LinearQueueCongestionDetection lqcd;
        lqcd.minimumLength() = 0;
        lqcd.maximumLength() = 0;
        lqcd.probability() = probability;
        aqm.detection()->linear() = lqcd;
        aqm.behavior() = cfg::QueueCongestionBehavior::ECN;
        portQueue->resetAqms({aqm});
        std::vector<std::shared_ptr<PortQueue>> portQueues = {portQueue};
        port->resetPortQueues(portQueues);
        return port;
      };

  // Test 1: ECN probability 50% should be INVALID when feature not supported
  auto port50 = createPortWithEcnProbability(PortID(10), "port10", 50);
  EXPECT_FALSE(hasValidPortQueues(
      port50, false /* isEcnProbabilisticMarkingSupported */));

  // Test 2: ECN probability 100% should be VALID when feature not supported
  auto port100 = createPortWithEcnProbability(PortID(11), "port11", 100);
  EXPECT_TRUE(hasValidPortQueues(
      port100, false /* isEcnProbabilisticMarkingSupported */));

  // Test 3: ECN probability 0% should be INVALID when feature not supported
  auto port0 = createPortWithEcnProbability(PortID(12), "port12", 0);
  EXPECT_FALSE(hasValidPortQueues(
      port0, false /* isEcnProbabilisticMarkingSupported */));

  // Test 4: ECN probability 99% should be INVALID when feature not supported
  auto port99 = createPortWithEcnProbability(PortID(13), "port13", 99);
  EXPECT_FALSE(hasValidPortQueues(
      port99, false /* isEcnProbabilisticMarkingSupported */));
}

TEST_F(SwSwitchTest, gracefulExit) {
  auto bringPortsUpUpdateFn = [](const std::shared_ptr<SwitchState>& state) {
    return bringAllPortsUp(state);
  };
  sw->updateStateBlocking("Bring Ports Up", bringPortsUpUpdateFn);
  sw->gracefulExit();
}

TEST_F(SwSwitchTest, overlappingUpdatesWithExit) {
  auto bringPortsUpUpdateFn = [](const std::shared_ptr<SwitchState>& state) {
    return bringAllPortsUp(state);
  };
  sw->updateStateBlocking("Bring Ports Up", bringPortsUpUpdateFn);
  std::atomic<bool> done{false};
  std::thread nonCoaelescingUpdates([this, &done] {
    const PortID kPort1{1};
    while (!done) {
      // Now flap the port. This should schedule non coalescing updates.
      sw->linkStateChanged(kPort1, false, cfg::PortType::INTERFACE_PORT);
      sw->linkStateChanged(kPort1, true, cfg::PortType::INTERFACE_PORT);
    }
  });
  std::thread blockingUpdates([this, &done] {
    while (!done) {
      auto updateOperStateFn = [](const std::shared_ptr<SwitchState>& state) {
        const PortID kPort2{2};
        std::shared_ptr<SwitchState> newState(state);
        auto* port = newState->getPorts()->getNodeIf(kPort2).get();
        port = port->modify(&newState);
        // Transition up<->down
        port->setOperState(!port->isUp());
        return newState;
      };
      sw->updateStateBlocking("Flap port 2", updateOperStateFn);
    }
  });

  sw->gracefulExit();
  done = true;
  nonCoaelescingUpdates.join();
  blockingUpdates.join();
}

TEST_F(SwSwitchTest, multiSwitchFb303Stats) {
  std::map<SwitchID, const HwAsic*> asicMap;
  CounterCache counters(sw);
  asicMap[SwitchID(0)] = handle->getPlatform()->getAsic();
  auto multiSwitchStats = std::make_unique<MultiSwitchFb303Stats>(asicMap);
  auto getGlobalStats = [](int64_t val) {
    HwSwitchFb303GlobalStats globalStats;
    globalStats.tx_pkt_allocated() = val;
    globalStats.tx_pkt_freed() = val;
    globalStats.tx_pkt_sent() = val;
    globalStats.tx_pkt_sent_done() = val;
    globalStats.tx_errors() = val;
    globalStats.tx_pkt_allocation_errors() = val;
    globalStats.parity_errors() = val;
    globalStats.parity_corr() = val;
    globalStats.parity_uncorr() = val;
    globalStats.asic_error() = val;
    globalStats.global_drops() = val;
    globalStats.global_reachability_drops() = val;
    globalStats.packet_integrity_drops() = val;
    globalStats.vsq_resource_exhaustion_drops() = val;
    globalStats.dram_enqueued_bytes() = val;
    globalStats.dram_dequeued_bytes() = val;
    globalStats.dram_blocked_time_ns() = val;
    globalStats.dram_quarantined_buffer_count() = val;
    globalStats.fabric_reachability_missing() = val;
    globalStats.fabric_reachability_mismatch() = val;
    globalStats.fabric_connectivity_bogus() = val;
    globalStats.switch_reachability_change() = val;
    return globalStats;
  };
  auto val = 100;
  auto statsUpdate1 = getGlobalStats(val);
  multiSwitchStats->updateStats(statsUpdate1);
  // fb303 stat should have gotten an update
  EXPECT_EQ(
      multiSwitchStats->getHwSwitchFb303GlobalStats().getTxPktAllocCount(), 1);
  counters.update();
  auto checkValues = [&counters](const int64_t expectedVal) {
    EXPECT_EQ(counters.value("mock.tx.pkt.allocated.sum"), expectedVal);
    EXPECT_EQ(counters.value("mock.tx.pkt.freed.sum"), expectedVal);
    EXPECT_EQ(counters.value("mock.tx.pkt.sent.sum"), expectedVal);
    EXPECT_EQ(counters.value("mock.tx.pkt.sent.done.sum"), expectedVal);
    EXPECT_EQ(counters.value("mock.tx.errors.sum"), expectedVal);
    EXPECT_EQ(counters.value("mock.tx.pkt.allocation.errors.sum"), expectedVal);
    EXPECT_EQ(counters.value("mock.parity.errors.sum"), expectedVal);
    EXPECT_EQ(counters.value("mock.parity.corr.sum"), expectedVal);
    EXPECT_EQ(counters.value("mock.parity.uncorr.sum"), expectedVal);
    EXPECT_EQ(counters.value("mock.asic.error.sum"), expectedVal);
    EXPECT_EQ(counters.value("global_drops.sum"), expectedVal);
    EXPECT_EQ(counters.value("global_reachability_drops.sum"), expectedVal);
    EXPECT_EQ(counters.value("packet_integrity_drops.sum"), expectedVal);
    EXPECT_EQ(counters.value("vsq_resource_exhaustion_drops.sum"), expectedVal);
    EXPECT_EQ(counters.value("dram_enqueued_bytes.sum"), expectedVal);
    EXPECT_EQ(counters.value("dram_dequeued_bytes.sum"), expectedVal);
    EXPECT_EQ(counters.value("dram_blocked_time_ns.sum"), expectedVal);
    EXPECT_EQ(counters.value("dram_quarantined_buffer_count.sum"), expectedVal);
    EXPECT_EQ(counters.value("fabric_connectivity_missing"), expectedVal);
    EXPECT_EQ(counters.value("fabric_connectivity_mismatch"), expectedVal);
    EXPECT_EQ(counters.value("fabric_connectivity_bogus"), expectedVal);
    EXPECT_EQ(counters.value("switch_reachability_change.sum"), expectedVal);
  };
  checkValues(val);
  auto updatedVal = 110;
  auto statsUpdate2 = getGlobalStats(updatedVal);
  multiSwitchStats->updateStats(statsUpdate2);
  counters.update();
  checkValues(updatedVal);
  updatedVal = 50;
  auto statsUpdate3 = getGlobalStats(updatedVal);
  multiSwitchStats->updateStats(statsUpdate3);
  counters.update();
  checkValues(updatedVal);
}

/*
 * applyConfig waits on the rib thread, and the rib thread waits on the update
 * thread whenever it programs a fib update. Any part of applyConfig that waits
 * for the rib *from* the update thread closes that cycle and wedges the agent.
 *
 * Hold the rib thread the way an in-flight route update does, and assert the
 * update thread keeps running work while applyConfig waits on it.
 */
TEST_F(SwSwitchTest, applyConfigKeepsUpdateThreadFreeWhileRibIsBusy) {
  gflags::FlagSaver flagSaver;
  FLAGS_enable_ecmp_resource_manager = true;

  ASSERT_NE(sw->getRib(), nullptr);

  // Declared before the threads and the scope guard: locals are destroyed in
  // reverse declaration order, so anything the threads reference by pointer
  // has to outlive the guard that joins them.
  auto config = testConfigA();
  auto stateBeforeConfig = sw->getState();
  folly::Baton<> ribBusy;
  folly::Baton<> releaseRib;
  folly::Baton<> applierDone;
  std::thread ribHog([&] {
    sw->getRib()->updateStateInRibThread([&] {
      ribBusy.post();
      releaseRib.wait();
    });
  });
  std::thread applier;
  bool ribReleased = false;
  // Release the rib thread on every exit path, including a failed ASSERT.
  SCOPE_EXIT {
    if (!ribReleased) {
      releaseRib.post();
    }
    if (applier.joinable()) {
      applier.join();
    }
    if (ribHog.joinable()) {
      ribHog.join();
    }
  };
  ribBusy.wait();

  applier = std::thread([&] {
    SCOPE_EXIT {
      applierDone.post();
    };
    sw->applyConfig("test config", config);
  });

  // Each probe is a full round trip through the update thread, so the loops
  // below pace themselves without sleeping.
  auto updateThreadRunsWork = [this]() {
    // Shared so the baton outlives this scope if the update never runs.
    auto probeRan = std::make_shared<folly::Baton<>>();
    sw->updateState("probe update", [probeRan](const auto& /*state*/) {
      probeRan->post();
      return std::shared_ptr<SwitchState>{};
    });
    return probeRan->try_wait_for(std::chrono::seconds(20));
  };

  // Wait for applyConfig's own state update to land, so the assertions below
  // cannot pass before it even reaches the rib. Bounded, since a config that
  // changes nothing publishes no new state.
  constexpr int kMaxProbesAwaitingConfig = 50;
  for (int i = 0;
       i < kMaxProbesAwaitingConfig && sw->getState() == stateBeforeConfig;
       ++i) {
    ASSERT_TRUE(updateThreadRunsWork())
        << "update thread wedged on probe " << i << " awaiting config apply";
  }

  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(updateThreadRunsWork())
        << "update thread wedged on probe " << i
        << " while applyConfig waited on a busy rib thread";
  }

  releaseRib.post();
  ribReleased = true;
  EXPECT_TRUE(applierDone.try_wait_for(std::chrono::seconds(30)))
      << "applyConfig did not complete after the rib thread was released";
}

TEST_F(SwSwitchTest, swSwitchRunState) {
  auto switchSettings =
      utility::getFirstNodeIf(sw->getState()->getSwitchSettings());
  EXPECT_EQ(switchSettings->getSwSwitchRunState(), SwitchRunState::CONFIGURED);
}

class SwSwitchTestNbrs : public SwSwitchTest {
  void SetUp() override {
    SwSwitchTest::SetUp();
  }
};

TEST_F(SwSwitchTestNbrs, TestStateNonCoalescing) {
  auto sw = this->sw;

  const PortID kPort1{1};
  const VlanID kVlan1{1};
  const InterfaceID kInterfaceID1{1};

  auto verifyReachableCnt =
      [kVlan1, kInterfaceID1, this](int expectedReachableNbrCnt) {
        auto getReachableCount = [](auto nbrTable) {
          auto reachableCnt = 0;
          for (const auto& iter : std::as_const(*nbrTable)) {
            auto entry = iter.second;
            if (entry->getState() == NeighborState::REACHABLE) {
              ++reachableCnt;
            }
          }
          return reachableCnt;
        };

        std::shared_ptr<ArpTable> arpTable;
        std::shared_ptr<NdpTable> ndpTable;

        arpTable = this->sw->getState()
                       ->getInterfaces()
                       ->getNode(kInterfaceID1)
                       ->getArpTable();
        ndpTable = this->sw->getState()
                       ->getInterfaces()
                       ->getNode(kInterfaceID1)
                       ->getNdpTable();

        auto reachableCnt =
            getReachableCount(arpTable) + getReachableCount(ndpTable);
        EXPECT_EQ(expectedReachableNbrCnt, reachableCnt);
      };
  // No neighbor entries expected
  verifyReachableCnt(0);
  auto origState = sw->getState();
  auto bringPortsUpUpdateFn = [](const std::shared_ptr<SwitchState>& state) {
    return bringAllPortsUp(state);
  };
  sw->updateState("Bring Ports Up", bringPortsUpUpdateFn);

  sw->getNeighborUpdater()->receivedArpMineForIntf(
      kInterfaceID1,
      IPAddressV4("10.0.0.2"),
      MacAddress("01:02:03:04:05:06"),
      PortDescriptor(kPort1),
      ArpOpCode::ARP_OP_REPLY);

  sw->getNeighborUpdater()->receivedNdpMineForIntf(
      kInterfaceID1,
      IPAddressV6("2401:db00:2110:3001::0002"),
      MacAddress("01:02:03:04:05:06"),
      PortDescriptor(kPort1),
      ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_ADVERTISEMENT,
      0);

  sw->getNeighborUpdater()->waitForPendingUpdates();
  waitForStateUpdates(sw);
  // 2 neighbor entries expected
  verifyReachableCnt(2);
  // Now flap the port. This should schedule non coalescing updates.
  sw->linkStateChanged(kPort1, false, cfg::PortType::INTERFACE_PORT);
  sw->linkStateChanged(kPort1, true, cfg::PortType::INTERFACE_PORT);
  waitForStateUpdates(sw);
  // Neighbor purge is scheduled on BG thread. Wait for it to be scheduled.
  waitForBackgroundThread(sw);
  // And wait for purge to happen. Test ensures that
  // purge is not skipped due to port down/up being coalesced
  waitForStateUpdates(sw);
  sw->getNeighborUpdater()->waitForPendingUpdates();
  // Wait for static mac entries to be purged in response to
  // neighbor getting pruned
  waitForStateUpdates(sw);

  // 0 neighbor entries expected, i.e. entries must be purged
  verifyReachableCnt(0);
}

TEST_F(SwSwitchTest, FillFsdbStatsNullShelManager) {
  // This test exercises fillFsdbStats() on an NPU (non-VOQ) switch
  // configuration where shelManager_ is null. Without the null check
  // guard on shelManager_, this would crash with a null pointer
  // dereference.
  multiswitch::HwSwitchStats hwStats;
  sw->updateHwSwitchStats(0, hwStats);
  // Should not crash - shelManager_ is null on NPU switches
  // and the code must guard against that.
  EXPECT_NO_THROW(sw->fillFsdbStats());
}

class SwSwitchValidateConfigTest : public ::testing::Test {
 public:
  void SetUp() override {
    config_ = testConfigA();
    handle_ = createTestHandle(&config_);
    sw_ = handle_->getSw();
    sw_->initialConfigApplied(std::chrono::steady_clock::now());
    waitForStateUpdates(sw_);
  }

  void TearDown() override {
    sw_ = nullptr;
    handle_.reset();
  }

  // A config that differs from the running one but is valid.
  cfg::SwitchConfig validChange() const {
    auto config = config_;
    config.ports()[0].description() = "changed by validateConfig test";
    return config;
  }

  // A config that applyThriftConfig() rejects: moving the interface off
  // VLAN 1 leaves that VLAN, which has enabled ports, without an interface.
  cfg::SwitchConfig invalidChange() const {
    auto config = config_;
    config.interfaces()[0].vlanID() = 4000;
    return config;
  }

  cfg::SwitchConfig config_;
  SwSwitch* sw_{nullptr};
  std::unique_ptr<HwTestHandle> handle_{nullptr};
};

TEST_F(SwSwitchValidateConfigTest, ValidateConfigAccepted) {
  auto stateBefore = sw_->getState();
  auto appliedBefore = *sw_->getConfigAppliedInfo().lastAppliedInMs();

  EXPECT_NO_THROW(
      sw_->validateConfig(validChange(), thrift::ConfigApplyMethod::RELOAD));

  // Nothing was applied or recorded.
  EXPECT_EQ(stateBefore, sw_->getState());
  EXPECT_EQ(sw_->getConfig(), config_);
  EXPECT_EQ(appliedBefore, *sw_->getConfigAppliedInfo().lastAppliedInMs());
}

TEST_F(SwSwitchValidateConfigTest, ValidateConfigNoChange) {
  auto stateBefore = sw_->getState();
  EXPECT_NO_THROW(
      sw_->validateConfig(config_, thrift::ConfigApplyMethod::RELOAD));
  EXPECT_EQ(stateBefore, sw_->getState());
}

TEST_F(SwSwitchValidateConfigTest, ValidateConfigRejectedByApplyThriftConfig) {
  auto stateBefore = sw_->getState();
  EXPECT_THAT(
      [&] {
        sw_->validateConfig(invalidChange(), thrift::ConfigApplyMethod::RELOAD);
      },
      testing::ThrowsMessage<FbossError>(
          testing::HasSubstr("VLAN 1 has no interface")));
  EXPECT_EQ(stateBefore, sw_->getState());
  EXPECT_EQ(sw_->getConfig(), config_);
}

TEST_F(SwSwitchValidateConfigTest, ValidateConfigRejectedByStateValidator) {
  // The HwSwitch validity hook is part of the state update validation and
  // must be consulted by the dry run too.
  EXPECT_CALL(*getMockHw(sw_), isValidStateUpdate(_))
      .WillOnce(testing::Return(false));
  auto stateBefore = sw_->getState();
  EXPECT_THROW(
      sw_->validateConfig(validChange(), thrift::ConfigApplyMethod::RELOAD),
      FbossError);
  EXPECT_EQ(stateBefore, sw_->getState());
}

TEST_F(SwSwitchValidateConfigTest, ValidateConfigDoesNotAffectLaterApply) {
  // Dry runs, whether rejected or accepted, must not leave anything behind
  // (e.g. resource accounting) that would make a real apply fail.
  EXPECT_THROW(
      sw_->validateConfig(invalidChange(), thrift::ConfigApplyMethod::RELOAD),
      FbossError);
  EXPECT_NO_THROW(
      sw_->validateConfig(validChange(), thrift::ConfigApplyMethod::RELOAD));

  auto config = validChange();
  EXPECT_NO_THROW(sw_->applyConfig("apply after validate", config));
  waitForStateUpdates(sw_);
  EXPECT_EQ(sw_->getConfig(), config);
}

TEST_F(SwSwitchValidateConfigTest, ValidateConfigEcmpWidthNeedsColdBoot) {
  // The state update validator refuses an ECMP width change on a running
  // agent; a coldboot is how it gets changed.
  auto settings = utility::getFirstNodeIf(sw_->getState()->getSwitchSettings());
  ASSERT_TRUE(settings->getEcmpWidth().has_value());
  auto config = config_;
  config.switchSettings()->ecmpWidth() = *settings->getEcmpWidth() / 2;

  // Neither a reload nor a warmboot can make the change.
  for (auto applyMethod :
       {thrift::ConfigApplyMethod::RELOAD,
        thrift::ConfigApplyMethod::RESTART}) {
    try {
      sw_->validateConfig(config, applyMethod);
      ADD_FAILURE() << "ECMP width change accepted for "
                    << apache::thrift::util::enumNameSafe(applyMethod);
    } catch (const RestartRequiredError& ex) {
      EXPECT_EQ(
          ex.requiredApplyMethod(),
          thrift::ConfigApplyMethod::DISRUPTIVE_RESTART);
      EXPECT_THAT(ex.what(), testing::HasSubstr("requires DISRUPTIVE_RESTART"));
    }
  }
  EXPECT_NO_THROW(sw_->validateConfig(
      config, thrift::ConfigApplyMethod::DISRUPTIVE_RESTART));
}

TEST_F(SwSwitchValidateConfigTest, ValidateConfigHwSwitchNeedsColdBoot) {
  // The HwSwitch refuses some changes after the initial config application
  // (e.g. L2 learning mode on SAI) by throwing RestartRequiredError.
  EXPECT_CALL(*getMockHw(sw_), isValidStateUpdate(_))
      .WillRepeatedly([](const StateDelta& /* delta */) -> bool {
        throw RestartRequiredError(
            thrift::ConfigApplyMethod::DISRUPTIVE_RESTART,
            "not on a running agent");
      });
  EXPECT_THROW(
      sw_->validateConfig(validChange(), thrift::ConfigApplyMethod::RELOAD),
      RestartRequiredError);
  EXPECT_NO_THROW(sw_->validateConfig(
      validChange(), thrift::ConfigApplyMethod::DISRUPTIVE_RESTART));
}

TEST_F(SwSwitchValidateConfigTest, ValidateConfigColdBootStillRejectsInvalid) {
  // A coldboot only lets through changes that need a coldboot.
  EXPECT_THAT(
      [&] {
        sw_->validateConfig(
            invalidChange(), thrift::ConfigApplyMethod::DISRUPTIVE_RESTART);
      },
      testing::ThrowsMessage<FbossError>(
          testing::HasSubstr("VLAN 1 has no interface")));
}
