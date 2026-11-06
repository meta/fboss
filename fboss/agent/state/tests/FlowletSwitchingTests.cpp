/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/state/FlowletSwitchingConfig.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

#include <gtest/gtest.h>

namespace {
facebook::fboss::HwSwitchMatcher scope() {
  return facebook::fboss::HwSwitchMatcher{
      std::unordered_set<facebook::fboss::SwitchID>{
          facebook::fboss::SwitchID(0)}};
}
} // namespace

using namespace facebook::fboss;

TEST(FlowletSwitching, addUpdate) {
  auto state = std::make_shared<SwitchState>();

  cfg::FlowletSwitchingConfig flowletCfg;
  auto flowletSwitchingConfig = std::make_shared<FlowletSwitchingConfig>();
  // convert to state
  flowletSwitchingConfig->fromThrift(flowletCfg);

  // update the state with flowletCfg
  auto switchSettings = std::make_shared<SwitchSettings>();
  switchSettings->setFlowletSwitchingConfig(flowletSwitchingConfig);
  auto multiSwitchSwitchSettings = std::make_shared<MultiSwitchSettings>();
  multiSwitchSwitchSettings->addNode(scope().matcherString(), switchSettings);
  state->resetSwitchSettings(multiSwitchSwitchSettings);
  auto flowletCfg1 = state->getFlowletSwitchingConfig();
  EXPECT_TRUE(flowletCfg1);
  EXPECT_FALSE(flowletCfg1->isPublished());
  // default should kick in
  EXPECT_EQ(
      flowletCfg1->getFlowletTableSize(),
      cfg::switch_config_constants::DEFAULT_FLOWLET_TABLE_SIZE());
  EXPECT_EQ(
      flowletCfg1->getSwitchingMode(), cfg::SwitchingMode::FLOWLET_QUALITY);
  EXPECT_EQ(
      flowletCfg1->getBackupSwitchingMode(),
      cfg::SwitchingMode::FIXED_ASSIGNMENT);
  EXPECT_EQ(flowletCfg1->getStandbySwitchingMode(), std::nullopt);
  EXPECT_EQ(flowletCfg1->getStandbyInactivityIntervalUsecs(), std::nullopt);
  EXPECT_EQ(flowletCfg1->getStandbyFlowletTableSize(), std::nullopt);

  flowletCfg.inactivityIntervalUsecs() = 60;
  flowletCfg.flowletTableSize() = 1024;
  flowletCfg.dynamicEgressLoadExponent() = 3;
  flowletCfg.dynamicQueueExponent() = 3;
  flowletCfg.dynamicQueueMinThresholdBytes() = 0x5D44;
  flowletCfg.dynamicQueueMaxThresholdBytes() = 0xFE00;
  flowletCfg.dynamicSampleRate() = 62500;
  flowletCfg.dynamicEgressMinThresholdBytes() = 1000;
  flowletCfg.dynamicEgressMaxThresholdBytes() = 10000;
  flowletCfg.dynamicPhysicalQueueExponent() = 3;
  flowletCfg.maxLinks() = 9;
  flowletCfg.switchingMode() = cfg::SwitchingMode::PER_PACKET_QUALITY;
  flowletCfg.backupSwitchingMode() = cfg::SwitchingMode::PER_PACKET_RANDOM;
  flowletCfg.primaryPathQualityThreshold() = 100;
  flowletCfg.alternatePathCost() = 50;
  flowletCfg.alternatePathBias() = 25;
  flowletCfg.standbySwitchingMode() = cfg::SwitchingMode::FIXED_ASSIGNMENT;
  flowletCfg.standbyInactivityIntervalUsecs() = 128;
  flowletCfg.standbyFlowletTableSize() = 512;

  flowletSwitchingConfig->fromThrift(flowletCfg);
  switchSettings = std::make_shared<SwitchSettings>();
  switchSettings->setFlowletSwitchingConfig(flowletSwitchingConfig);
  multiSwitchSwitchSettings = std::make_shared<MultiSwitchSettings>();
  multiSwitchSwitchSettings->addNode(scope().matcherString(), switchSettings);
  state->resetSwitchSettings(multiSwitchSwitchSettings);
  auto flowletCfg2 = state->getFlowletSwitchingConfig();
  EXPECT_TRUE(flowletCfg2);
  EXPECT_FALSE(flowletCfg2->isPublished());
  EXPECT_EQ(flowletCfg2->getInactivityIntervalUsecs(), 60);
  EXPECT_EQ(flowletCfg2->getFlowletTableSize(), 1024);
  EXPECT_EQ(flowletCfg2->getDynamicEgressLoadExponent(), 3);
  EXPECT_EQ(flowletCfg2->getDynamicQueueExponent(), 3);
  EXPECT_EQ(flowletCfg2->getDynamicQueueMinThresholdBytes(), 0x5D44);
  EXPECT_EQ(flowletCfg2->getDynamicQueueMaxThresholdBytes(), 0xFE00);
  EXPECT_EQ(flowletCfg2->getDynamicSampleRate(), 62500);
  EXPECT_EQ(flowletCfg2->getDynamicEgressMinThresholdBytes(), 1000);
  EXPECT_EQ(flowletCfg2->getDynamicEgressMaxThresholdBytes(), 10000);
  EXPECT_EQ(flowletCfg2->getDynamicPhysicalQueueExponent(), 3);
  EXPECT_EQ(flowletCfg2->getMaxLinks(), 9);
  EXPECT_EQ(
      flowletCfg2->getSwitchingMode(), cfg::SwitchingMode::PER_PACKET_QUALITY);
  EXPECT_EQ(
      flowletCfg2->getBackupSwitchingMode(),
      cfg::SwitchingMode::PER_PACKET_RANDOM);
  EXPECT_TRUE(flowletCfg2->getPrimaryPathQualityThreshold().has_value());
  EXPECT_EQ(flowletCfg2->getPrimaryPathQualityThreshold().value(), 100);
  EXPECT_TRUE(flowletCfg2->getAlternatePathCost().has_value());
  EXPECT_EQ(flowletCfg2->getAlternatePathCost().value(), 50);
  EXPECT_TRUE(flowletCfg2->getAlternatePathBias().has_value());
  EXPECT_EQ(flowletCfg2->getAlternatePathBias().value(), 25);
  EXPECT_EQ(
      flowletCfg2->getStandbySwitchingMode(),
      cfg::SwitchingMode::FIXED_ASSIGNMENT);
  EXPECT_EQ(flowletCfg2->getStandbyInactivityIntervalUsecs(), 128);
  EXPECT_EQ(flowletCfg2->getStandbyFlowletTableSize(), 512);

  flowletCfg.switchingMode() = cfg::SwitchingMode::FIXED_ASSIGNMENT;
  flowletCfg.backupSwitchingMode() = cfg::SwitchingMode::PER_PACKET_RANDOM;
  flowletSwitchingConfig->fromThrift(flowletCfg);
  switchSettings = std::make_shared<SwitchSettings>();
  switchSettings->setFlowletSwitchingConfig(flowletSwitchingConfig);
  multiSwitchSwitchSettings = std::make_shared<MultiSwitchSettings>();
  multiSwitchSwitchSettings->addNode(scope().matcherString(), switchSettings);
  state->resetSwitchSettings(multiSwitchSwitchSettings);
  flowletCfg2 = state->getFlowletSwitchingConfig();
  EXPECT_TRUE(flowletCfg2);
  EXPECT_FALSE(flowletCfg2->isPublished());
  EXPECT_EQ(
      flowletCfg2->getSwitchingMode(), cfg::SwitchingMode::FIXED_ASSIGNMENT);
  EXPECT_EQ(
      flowletCfg2->getBackupSwitchingMode(),
      cfg::SwitchingMode::PER_PACKET_RANDOM);
}

TEST(FlowletSwitching, publish) {
  cfg::FlowletSwitchingConfig flowletCfg;
  auto flowletSwitchingConfig = std::make_shared<FlowletSwitchingConfig>();
  auto state = std::make_shared<SwitchState>();

  // convert to state
  flowletSwitchingConfig->fromThrift(flowletCfg);
  auto switchSettings = std::make_shared<SwitchSettings>();
  switchSettings->setFlowletSwitchingConfig(flowletSwitchingConfig);
  auto multiSwitchSwitchSettings = std::make_shared<MultiSwitchSettings>();
  multiSwitchSwitchSettings->addNode(scope().matcherString(), switchSettings);
  state->resetSwitchSettings(multiSwitchSwitchSettings);
  // update the state with flowletCfg
  state->publish();
  EXPECT_TRUE(state->getFlowletSwitchingConfig()->isPublished());
}

TEST(FlowletSwitching, serDeserSwitchState) {
  auto state = std::make_shared<SwitchState>();

  cfg::FlowletSwitchingConfig flowletCfg;
  auto flowletSwitchingConfig = std::make_shared<FlowletSwitchingConfig>();

  flowletCfg.inactivityIntervalUsecs() = 60;
  flowletCfg.flowletTableSize() = 1024;
  flowletCfg.dynamicEgressLoadExponent() = 3;
  flowletCfg.dynamicQueueExponent() = 3;
  flowletCfg.dynamicQueueMinThresholdBytes() = 0x5D44;
  flowletCfg.dynamicQueueMaxThresholdBytes() = 0xFE00;
  flowletCfg.dynamicSampleRate() = 62500;
  flowletCfg.dynamicEgressMinThresholdBytes() = 1000;
  flowletCfg.dynamicEgressMaxThresholdBytes() = 10000;
  flowletCfg.dynamicPhysicalQueueExponent() = 3;
  flowletCfg.maxLinks() = 7;
  flowletCfg.switchingMode() = cfg::SwitchingMode::PER_PACKET_QUALITY;
  flowletCfg.backupSwitchingMode() = cfg::SwitchingMode::FIXED_ASSIGNMENT;
  flowletCfg.primaryPathQualityThreshold() = 200;
  flowletCfg.alternatePathCost() = 75;
  flowletCfg.alternatePathBias() = 30;
  flowletCfg.standbySwitchingMode() = cfg::SwitchingMode::PER_PACKET_RANDOM;
  flowletCfg.standbyInactivityIntervalUsecs() = 256;
  flowletCfg.standbyFlowletTableSize() = 2048;

  // convert to state
  flowletSwitchingConfig->fromThrift(flowletCfg);
  // update the state with flowletCfg
  auto switchSettings = std::make_shared<SwitchSettings>();
  switchSettings->setFlowletSwitchingConfig(flowletSwitchingConfig);
  auto multiSwitchSwitchSettings = std::make_shared<MultiSwitchSettings>();
  multiSwitchSwitchSettings->addNode(scope().matcherString(), switchSettings);
  state->resetSwitchSettings(multiSwitchSwitchSettings);

  auto serialized = state->toThrift();
  auto stateBack = SwitchState::fromThrift(serialized);
  EXPECT_EQ(state->toThrift(), stateBack->toThrift());
}

TEST(FlowletSwitching, applyConfig) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();

  cfg::SwitchConfig config;
  cfg::FlowletSwitchingConfig flowletCfg;

  // make any random change, so that new state can be created
  // ensure that flowlet switching is not configured
  *config.switchSettings()->l2LearningMode() = cfg::L2LearningMode::SOFTWARE;
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  EXPECT_EQ(stateV1->getFlowletSwitchingConfig(), nullptr);

  config.flowletSwitchingConfig() = flowletCfg;
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  ASSERT_NE(nullptr, stateV2);
  auto flowletCfg1 = stateV2->getFlowletSwitchingConfig();
  EXPECT_TRUE(flowletCfg1);
  EXPECT_FALSE(flowletCfg1->isPublished());
  // default should kick in
  EXPECT_EQ(
      flowletCfg1->getFlowletTableSize(),
      cfg::switch_config_constants::DEFAULT_FLOWLET_TABLE_SIZE());
  EXPECT_EQ(
      flowletCfg1->getSwitchingMode(), cfg::SwitchingMode::FLOWLET_QUALITY);
  EXPECT_EQ(
      flowletCfg1->getBackupSwitchingMode(),
      cfg::SwitchingMode::FIXED_ASSIGNMENT);
  EXPECT_EQ(flowletCfg1->getStandbySwitchingMode(), std::nullopt);
  EXPECT_EQ(flowletCfg1->getStandbyInactivityIntervalUsecs(), std::nullopt);
  EXPECT_EQ(flowletCfg1->getStandbyFlowletTableSize(), std::nullopt);

  // change config
  flowletCfg.inactivityIntervalUsecs() = 60;
  flowletCfg.flowletTableSize() = 1024;
  flowletCfg.dynamicEgressLoadExponent() = 3;
  flowletCfg.dynamicQueueExponent() = 3;
  flowletCfg.dynamicQueueMinThresholdBytes() = 0x5D44;
  flowletCfg.dynamicQueueMaxThresholdBytes() = 0xFE00;
  flowletCfg.dynamicSampleRate() = 62500;
  flowletCfg.dynamicEgressMinThresholdBytes() = 1000;
  flowletCfg.dynamicEgressMaxThresholdBytes() = 10000;
  flowletCfg.dynamicPhysicalQueueExponent() = 3;
  flowletCfg.switchingMode() = cfg::SwitchingMode::PER_PACKET_QUALITY;
  flowletCfg.backupSwitchingMode() = cfg::SwitchingMode::PER_PACKET_RANDOM;
  flowletCfg.primaryPathQualityThreshold() = 150;
  flowletCfg.alternatePathCost() = 60;
  flowletCfg.alternatePathBias() = 35;
  flowletCfg.standbySwitchingMode() = cfg::SwitchingMode::PER_PACKET_RANDOM;
  flowletCfg.standbyInactivityIntervalUsecs() = 256;
  flowletCfg.standbyFlowletTableSize() = 2048;

  config.flowletSwitchingConfig() = flowletCfg;
  auto stateV3 = publishAndApplyConfig(stateV2, &config, platform.get());
  ASSERT_NE(nullptr, stateV3);
  auto flowletCfg2 = stateV3->getFlowletSwitchingConfig();
  EXPECT_TRUE(flowletCfg2);
  EXPECT_FALSE(flowletCfg2->isPublished());
  EXPECT_EQ(flowletCfg2->getInactivityIntervalUsecs(), 60);
  EXPECT_EQ(flowletCfg2->getFlowletTableSize(), 1024);
  EXPECT_EQ(flowletCfg2->getDynamicEgressLoadExponent(), 3);
  EXPECT_EQ(flowletCfg2->getDynamicQueueExponent(), 3);
  EXPECT_EQ(flowletCfg2->getDynamicQueueMinThresholdBytes(), 0x5D44);
  EXPECT_EQ(flowletCfg2->getDynamicQueueMaxThresholdBytes(), 0xFE00);
  EXPECT_EQ(flowletCfg2->getDynamicSampleRate(), 62500);
  EXPECT_EQ(flowletCfg2->getDynamicEgressMinThresholdBytes(), 1000);
  EXPECT_EQ(flowletCfg2->getDynamicEgressMaxThresholdBytes(), 10000);
  EXPECT_EQ(flowletCfg2->getDynamicPhysicalQueueExponent(), 3);
  EXPECT_EQ(
      flowletCfg2->getSwitchingMode(), cfg::SwitchingMode::PER_PACKET_QUALITY);
  EXPECT_EQ(
      flowletCfg2->getBackupSwitchingMode(),
      cfg::SwitchingMode::PER_PACKET_RANDOM);
  EXPECT_EQ(
      flowletCfg2->getStandbySwitchingMode(),
      cfg::SwitchingMode::PER_PACKET_RANDOM);
  EXPECT_EQ(flowletCfg2->getStandbyInactivityIntervalUsecs(), 256);
  EXPECT_EQ(flowletCfg2->getStandbyFlowletTableSize(), 2048);

  flowletCfg.switchingMode() = cfg::SwitchingMode::FIXED_ASSIGNMENT;
  flowletCfg.backupSwitchingMode() = cfg::SwitchingMode::FIXED_ASSIGNMENT;

  config.flowletSwitchingConfig() = flowletCfg;
  auto stateV4 = publishAndApplyConfig(stateV2, &config, platform.get());
  ASSERT_NE(nullptr, stateV4);
  flowletCfg2 = stateV4->getFlowletSwitchingConfig();
  EXPECT_TRUE(flowletCfg2);
  EXPECT_FALSE(flowletCfg2->isPublished());
  EXPECT_EQ(
      flowletCfg2->getSwitchingMode(), cfg::SwitchingMode::FIXED_ASSIGNMENT);
  EXPECT_EQ(
      flowletCfg2->getBackupSwitchingMode(),
      cfg::SwitchingMode::FIXED_ASSIGNMENT);

  // Ensure that the global and switchSettings field are set for
  // backward/forward compatibility
  EXPECT_EQ(
      stateV2->getFlowletSwitchingConfig(),
      utility::getFirstNodeIf(stateV2->getSwitchSettings())
          ->getFlowletSwitchingConfig());

  // undo flowlet switching cfg
  config.flowletSwitchingConfig().reset();
  auto stateV5 = publishAndApplyConfig(stateV3, &config, platform.get());
  ASSERT_NE(nullptr, stateV4);
  EXPECT_EQ(stateV5->getFlowletSwitchingConfig(), nullptr);
}

TEST(FlowletSwitching, modify) {
  auto platform = createMockPlatform();

  cfg::SwitchConfig config;
  cfg::FlowletSwitchingConfig flowletCfg;

  // make any random change, so that new state can be created
  // ensure that flowlet switching is not configured
  config.flowletSwitchingConfig() = flowletCfg;
  auto stateV0 = std::make_shared<SwitchState>();
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(stateV1, nullptr);
  stateV1->publish();
  EXPECT_NE(stateV1->getFlowletSwitchingConfig(), nullptr);
  EXPECT_TRUE(stateV1->isPublished());
  EXPECT_TRUE(stateV1->getSwitchSettings()->isPublished());
  EXPECT_TRUE(stateV1->getFlowletSwitchingConfig()->isPublished());
  auto origFlowletConfig = stateV1->getFlowletSwitchingConfig();
  auto newFlowletConfig = origFlowletConfig->modify(&stateV1);
  EXPECT_NE(newFlowletConfig, origFlowletConfig.get());
  EXPECT_FALSE(stateV1->isPublished());
  EXPECT_FALSE(stateV1->getSwitchSettings()->isPublished());
  EXPECT_FALSE(stateV1->getFlowletSwitchingConfig()->isPublished());
}

TEST(FlowletSwitching, mismatchedStateModify) {
  auto platform = createMockPlatform();

  cfg::SwitchConfig config;
  cfg::FlowletSwitchingConfig flowletCfg;

  // make any random change, so that new state can be created
  // ensure that flowlet switching is not configured
  config.flowletSwitchingConfig() = flowletCfg;
  auto stateV0 = std::make_shared<SwitchState>();
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(stateV1, nullptr);
  stateV1->publish();
  EXPECT_NE(stateV1->getFlowletSwitchingConfig(), nullptr);
  EXPECT_TRUE(stateV1->isPublished());
  EXPECT_TRUE(stateV1->getSwitchSettings()->isPublished());
  EXPECT_TRUE(stateV1->getFlowletSwitchingConfig()->isPublished());
  auto origFlowletConfig = stateV1->getFlowletSwitchingConfig();
  auto newFlowletConfig = origFlowletConfig->modify(&stateV1);
  EXPECT_NE(newFlowletConfig, origFlowletConfig.get());
  EXPECT_FALSE(stateV1->isPublished());
  EXPECT_FALSE(stateV1->getSwitchSettings()->isPublished());
  EXPECT_FALSE(stateV1->getFlowletSwitchingConfig()->isPublished());
  stateV1->publish();
  newFlowletConfig->publish();
  auto stateV2 = stateV1->clone();
  stateV2->publish();
  std::ignore = stateV2->getFlowletSwitchingConfig()->modify(&stateV2);
  // newFlowletConfig is part of stateV1 not stateV2
  EXPECT_THROW(newFlowletConfig->modify(&stateV2), FbossError);
}

TEST(FlowletSwitching, standbySwitchingModeMatchingPrimaryRejected) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();

  cfg::SwitchConfig config;
  cfg::FlowletSwitchingConfig flowletCfg;
  flowletCfg.switchingMode() = cfg::SwitchingMode::PER_PACKET_QUALITY;
  flowletCfg.standbySwitchingMode() = cfg::SwitchingMode::PER_PACKET_QUALITY;
  flowletCfg.standbyInactivityIntervalUsecs() = 128;
  flowletCfg.standbyFlowletTableSize() = 2048;
  config.flowletSwitchingConfig() = flowletCfg;

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(FlowletSwitching, standbySwitchingModeWithoutIdleTimeRejected) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();

  cfg::SwitchConfig config;
  cfg::FlowletSwitchingConfig flowletCfg;
  flowletCfg.switchingMode() = cfg::SwitchingMode::PER_PACKET_QUALITY;
  flowletCfg.standbySwitchingMode() = cfg::SwitchingMode::FIXED_ASSIGNMENT;
  flowletCfg.standbyFlowletTableSize() = 2048;
  config.flowletSwitchingConfig() = flowletCfg;

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(FlowletSwitching, standbySwitchingModeWithoutTableSizeRejected) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();

  cfg::SwitchConfig config;
  cfg::FlowletSwitchingConfig flowletCfg;
  flowletCfg.switchingMode() = cfg::SwitchingMode::PER_PACKET_QUALITY;
  flowletCfg.standbySwitchingMode() = cfg::SwitchingMode::FIXED_ASSIGNMENT;
  flowletCfg.standbyInactivityIntervalUsecs() = 128;
  config.flowletSwitchingConfig() = flowletCfg;

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(FlowletSwitching, standbyFieldsWithoutSwitchingModeRejected) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();

  cfg::SwitchConfig config;
  cfg::FlowletSwitchingConfig flowletCfg;
  flowletCfg.switchingMode() = cfg::SwitchingMode::PER_PACKET_QUALITY;
  flowletCfg.standbyInactivityIntervalUsecs() = 128;
  flowletCfg.standbyFlowletTableSize() = 2048;
  config.flowletSwitchingConfig() = flowletCfg;

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(FlowletSwitching, arsVirtualGroupAlternateMembersApplied) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();

  cfg::SwitchConfig config;
  cfg::FlowletSwitchingConfig flowletCfg;
  flowletCfg.maxArsVirtualGroupWidth() = 256;
  flowletCfg.arsVirtualGroupAlternateMembers() = 8;
  flowletCfg.arsVirtualGroupCommonMembersThreshold() = 4;
  config.flowletSwitchingConfig() = flowletCfg;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  auto flowletConfig = stateV1->getFlowletSwitchingConfig();
  EXPECT_EQ(flowletConfig->getArsVirtualGroupAlternateMembers(), 8);
  EXPECT_EQ(flowletConfig->getArsVirtualGroupCommonMembersThreshold(), 4);
}

TEST(FlowletSwitching, arsVirtualGroupAlternateMembersWithoutWidthRejected) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();

  cfg::SwitchConfig config;
  cfg::FlowletSwitchingConfig flowletCfg;
  flowletCfg.arsVirtualGroupAlternateMembers() = 8;
  config.flowletSwitchingConfig() = flowletCfg;

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

// Reserving the whole width for alternates would leave no primary member able
// to forward.
TEST(FlowletSwitching, arsVirtualGroupAlternateMembersFillingWidthRejected) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();

  cfg::SwitchConfig config;
  cfg::FlowletSwitchingConfig flowletCfg;
  flowletCfg.maxArsVirtualGroupWidth() = 256;
  flowletCfg.arsVirtualGroupAlternateMembers() = 256;
  config.flowletSwitchingConfig() = flowletCfg;

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(FlowletSwitching, arsVirtualGroupCommonMembersThresholdAloneRejected) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();

  cfg::SwitchConfig config;
  cfg::FlowletSwitchingConfig flowletCfg;
  flowletCfg.maxArsVirtualGroupWidth() = 256;
  flowletCfg.arsVirtualGroupCommonMembersThreshold() = 4;
  config.flowletSwitchingConfig() = flowletCfg;

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

// arsGroupWidth is a plain optional on the schema: absent unless configured,
// and carried through the thrift-cow node unchanged.
TEST(FlowletSwitching, arsGroupWidthThriftRoundTrip) {
  cfg::FlowletSwitchingConfig flowletCfg;
  EXPECT_FALSE(flowletCfg.arsGroupWidth().has_value());

  flowletCfg.arsGroupWidth() = 128;
  auto flowletSwitchingConfig = std::make_shared<FlowletSwitchingConfig>();
  flowletSwitchingConfig->fromThrift(flowletCfg);

  EXPECT_EQ(flowletSwitchingConfig->toThrift().arsGroupWidth(), 128);
}

TEST(FlowletSwitching, arsGroupWidthClearedRatherThanZeroed) {
  auto flowletSwitchingConfig = std::make_shared<FlowletSwitchingConfig>();

  flowletSwitchingConfig->setArsGroupWidth(128);
  EXPECT_EQ(flowletSwitchingConfig->toThrift().arsGroupWidth(), 128);

  flowletSwitchingConfig->setArsGroupWidth(std::nullopt);
  EXPECT_FALSE(flowletSwitchingConfig->getArsGroupWidth().has_value());
  EXPECT_FALSE(flowletSwitchingConfig->toThrift().arsGroupWidth().has_value());
}

// Both the native width and the doubled width are valid configurations, so
// the applier carries whichever was asked for onto the state unchanged.
TEST(FlowletSwitching, arsGroupWidthApplied) {
  for (auto width : {64, 128}) {
    auto platform = createMockPlatform();
    auto stateV0 = std::make_shared<SwitchState>();

    cfg::SwitchConfig config;
    cfg::FlowletSwitchingConfig flowletCfg;
    flowletCfg.arsGroupWidth() = width;
    config.flowletSwitchingConfig() = flowletCfg;

    auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
    ASSERT_NE(nullptr, stateV1);
    EXPECT_EQ(stateV1->getFlowletSwitchingConfig()->getArsGroupWidth(), width);
  }
}

TEST(FlowletSwitching, arsGroupWidthUnsetStaysUnset) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();

  cfg::SwitchConfig config;
  config.flowletSwitchingConfig() = cfg::FlowletSwitchingConfig();

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  EXPECT_FALSE(
      stateV1->getFlowletSwitchingConfig()->getArsGroupWidth().has_value());
}
