/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <fboss/agent/gen-cpp2/switch_config_types.h>
#include <gflags/gflags.h>
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

using namespace facebook::fboss;
using std::make_shared;

namespace {
HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}};
}
} // namespace

TEST(SwitchSettingsTest, applyL2LearningConfig) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::SwitchConfig config;
  *config.switchSettings()->l2LearningMode() = cfg::L2LearningMode::SOFTWARE;
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);

  auto switchSettingsV1 = utility::getFirstNodeIf(stateV1->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_FALSE(switchSettingsV1->isPublished());
  EXPECT_EQ(
      cfg::L2LearningMode::SOFTWARE, switchSettingsV1->getL2LearningMode());
  EXPECT_FALSE(switchSettingsV1->isQcmEnable());
  EXPECT_FALSE(switchSettingsV1->isPtpTcEnable());
  EXPECT_EQ(300, switchSettingsV1->getL2AgeTimerSeconds());

  *config.switchSettings()->l2LearningMode() = cfg::L2LearningMode::HARDWARE;

  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  EXPECT_NE(nullptr, stateV2);

  auto switchSettingsV2 = utility::getFirstNodeIf(stateV2->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV2);
  EXPECT_FALSE(switchSettingsV2->isPublished());
  EXPECT_EQ(
      cfg::L2LearningMode::HARDWARE, switchSettingsV2->getL2LearningMode());
  EXPECT_FALSE(switchSettingsV2->isQcmEnable());
  EXPECT_FALSE(switchSettingsV2->isPtpTcEnable());
  EXPECT_EQ(300, switchSettingsV1->getL2AgeTimerSeconds());
}

TEST(SwitchSettingsTest, applySwitchDrainState) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  addSwitchInfo(
      stateV0, cfg::SwitchType::FABRIC, kFabricSwitchIdBegin /* switchId*/);

  cfg::SwitchConfig config = testConfigA(cfg::SwitchType::FABRIC);
  *config.switchSettings()->switchDrainState() = cfg::SwitchDrainState::DRAINED;
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);

  auto switchSettingsV1 = utility::getFirstNodeIf(stateV1->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_FALSE(switchSettingsV1->isPublished());
  EXPECT_EQ(
      cfg::SwitchDrainState::DRAINED, switchSettingsV1->getSwitchDrainState());

  *config.switchSettings()->switchDrainState() =
      cfg::SwitchDrainState::UNDRAINED;

  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  EXPECT_NE(nullptr, stateV2);

  auto switchSettingsV2 = utility::getFirstNodeIf(stateV2->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV2);
  EXPECT_FALSE(switchSettingsV2->isPublished());
  EXPECT_EQ(
      cfg::SwitchDrainState::UNDRAINED,
      switchSettingsV2->getSwitchDrainState());
}

TEST(SwitchSettingsTest, applySwitchDrainOnNpuSwitch) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  addSwitchInfo(stateV0, cfg::SwitchType::NPU, kNpuSwitchIdBegin /* switchId*/);

  // NPU switch config with drain state
  cfg::SwitchConfig npuConfig;
  *npuConfig.switchSettings()->switchDrainState() =
      cfg::SwitchDrainState::DRAINED;

  // Should now be allowed for NPU switches
  auto stateV1 = publishAndApplyConfig(stateV0, &npuConfig, platform.get());
  ASSERT_NE(nullptr, stateV1);
  auto switchSettingsV1 = utility::getFirstNodeIf(stateV1->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_EQ(
      cfg::SwitchDrainState::DRAINED, switchSettingsV1->getSwitchDrainState());

  // Change back to undrained
  *npuConfig.switchSettings()->switchDrainState() =
      cfg::SwitchDrainState::UNDRAINED;
  auto stateV2 = publishAndApplyConfig(stateV1, &npuConfig, platform.get());
  ASSERT_NE(nullptr, stateV2);
  auto switchSettingsV2 = utility::getFirstNodeIf(stateV2->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV2);
  EXPECT_EQ(
      cfg::SwitchDrainState::UNDRAINED,
      switchSettingsV2->getSwitchDrainState());
}

TEST(SwitchSettingsTest, applyQcmConfig) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::SwitchConfig config;
  *config.switchSettings()->qcmEnable() = true;
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto switchSettingsV1 = utility::getFirstNodeIf(stateV1->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_FALSE(switchSettingsV1->isPublished());
  EXPECT_TRUE(switchSettingsV1->isQcmEnable());
  EXPECT_EQ(
      cfg::L2LearningMode::HARDWARE, switchSettingsV1->getL2LearningMode());
  EXPECT_FALSE(switchSettingsV1->isPtpTcEnable());
  EXPECT_EQ(300, switchSettingsV1->getL2AgeTimerSeconds());

  *config.switchSettings()->qcmEnable() = false;
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  EXPECT_NE(nullptr, stateV2);
  auto switchSettingsV2 = utility::getFirstNodeIf(stateV2->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV2);
  EXPECT_FALSE(switchSettingsV2->isPublished());
  EXPECT_FALSE(switchSettingsV2->isQcmEnable());
  EXPECT_EQ(
      cfg::L2LearningMode::HARDWARE, switchSettingsV2->getL2LearningMode());
  EXPECT_FALSE(switchSettingsV2->isPtpTcEnable());
}

TEST(SwitchSettingsTest, applyPtpTcEnable) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::SwitchConfig config;
  config.switchSettings()->ptpTcEnable() = true;
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto switchSettingsV1 = utility::getFirstNodeIf(stateV1->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_FALSE(switchSettingsV1->isPublished());
  EXPECT_TRUE(switchSettingsV1->isPtpTcEnable());

  config.switchSettings()->ptpTcEnable() = false;
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  EXPECT_NE(nullptr, stateV2);
  auto switchSettingsV2 = utility::getFirstNodeIf(stateV2->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV2);
  EXPECT_FALSE(switchSettingsV2->isPublished());
  EXPECT_FALSE(switchSettingsV2->isPtpTcEnable());
  EXPECT_EQ(300, switchSettingsV1->getL2AgeTimerSeconds());
}

TEST(SwitchSettingsTest, applyPacketForwardingMode) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  // Apply config with CUT_THROUGH
  cfg::SwitchConfig config;
  config.switchSettings()->packetForwardingMode() =
      cfg::PacketForwardingMode::CUT_THROUGH;
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto switchSettingsV1 = utility::getFirstNodeIf(stateV1->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_EQ(
      cfg::PacketForwardingMode::CUT_THROUGH,
      switchSettingsV1->getPacketForwardingMode().value());

  // Apply config with STORE_AND_FORWARD
  config.switchSettings()->packetForwardingMode() =
      cfg::PacketForwardingMode::STORE_AND_FORWARD;
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  EXPECT_NE(nullptr, stateV2);
  auto switchSettingsV2 = utility::getFirstNodeIf(stateV2->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV2);
  EXPECT_EQ(
      cfg::PacketForwardingMode::STORE_AND_FORWARD,
      switchSettingsV2->getPacketForwardingMode().value());

  // Apply config without packetForwardingMode (unset/reset path)
  cfg::SwitchConfig config2;
  auto stateV3 = publishAndApplyConfig(stateV2, &config2, platform.get());
  EXPECT_NE(nullptr, stateV3);
  auto switchSettingsV3 = utility::getFirstNodeIf(stateV3->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV3);
  EXPECT_FALSE(switchSettingsV3->getPacketForwardingMode().has_value());
}

TEST(SwitchSettingsTest, applyEcmpGroupSettings) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  auto withSplitHorizon = [](bool enable) {
    cfg::EcmpGroupSettings settings;
    settings.enableSplitHorizon() = enable;
    return settings;
  };

  // Empty by default. Every group type then resolves to nullopt, i.e. nobody is
  // using the feature and callers leave the attribute alone.
  cfg::SwitchConfig config;
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto switchSettingsV1 = utility::getFirstNodeIf(stateV1->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_TRUE(switchSettingsV1->getEcmpGroupSettings().empty());
  EXPECT_FALSE(
      stateV1->getSplitHorizonEnabled(cfg::EcmpGroupType::ARS).has_value());

  // ARS on, FRR unnamed. Naming one type says nothing about the others: an
  // unnamed type stays nullopt rather than resolving to false, so it is left
  // alone rather than programmed off.
  config.switchSettings()->ecmpGroupSettings() = {
      {cfg::EcmpGroupType::ARS, withSplitHorizon(true)}};
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  EXPECT_NE(nullptr, stateV2);
  auto switchSettingsV2 = utility::getFirstNodeIf(stateV2->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV2);
  EXPECT_FALSE(switchSettingsV2->isPublished());
  EXPECT_EQ(1, switchSettingsV2->getEcmpGroupSettings().size());
  EXPECT_TRUE(*stateV2->getSplitHorizonEnabled(cfg::EcmpGroupType::ARS));
  EXPECT_FALSE(stateV2->getSplitHorizonEnabled(cfg::EcmpGroupType::FRR_PRIMARY)
                   .has_value());

  // A key present and false is distinct from a key absent: false is a
  // configured off, nullopt is unconfigured.
  config.switchSettings()->ecmpGroupSettings() = {
      {cfg::EcmpGroupType::ARS, withSplitHorizon(false)}};
  auto stateV2b = publishAndApplyConfig(stateV2, &config, platform.get());
  EXPECT_NE(nullptr, stateV2b);
  ASSERT_TRUE(
      stateV2b->getSplitHorizonEnabled(cfg::EcmpGroupType::ARS).has_value());
  EXPECT_FALSE(*stateV2b->getSplitHorizonEnabled(cfg::EcmpGroupType::ARS));

  // The reverse split: FRR on, ARS off. Both FRR halves must move together.
  config.switchSettings()->ecmpGroupSettings() = {
      {cfg::EcmpGroupType::FRR_PRIMARY, withSplitHorizon(true)},
      {cfg::EcmpGroupType::FRR_BACKUP, withSplitHorizon(true)}};
  auto stateV3 = publishAndApplyConfig(stateV2b, &config, platform.get());
  EXPECT_NE(nullptr, stateV3);
  EXPECT_TRUE(*stateV3->getSplitHorizonEnabled(cfg::EcmpGroupType::FRR_BACKUP));
  EXPECT_FALSE(
      stateV3->getSplitHorizonEnabled(cfg::EcmpGroupType::ARS).has_value());

  // Enabling one FRR half alone suppresses the source port with no tertiary
  // path on the other, so the config is rejected outright.
  config.switchSettings()->ecmpGroupSettings() = {
      {cfg::EcmpGroupType::FRR_PRIMARY, withSplitHorizon(true)},
      {cfg::EcmpGroupType::FRR_BACKUP, withSplitHorizon(false)}};
  EXPECT_THROW(
      publishAndApplyConfig(stateV3, &config, platform.get()), FbossError);

  // ... and the same the other way round.
  config.switchSettings()->ecmpGroupSettings() = {
      {cfg::EcmpGroupType::FRR_BACKUP, withSplitHorizon(true)}};
  EXPECT_THROW(
      publishAndApplyConfig(stateV3, &config, platform.get()), FbossError);

  // Clearing the map puts every group type back to unconfigured.
  config.switchSettings()->ecmpGroupSettings()->clear();
  auto stateV4 = publishAndApplyConfig(stateV3, &config, platform.get());
  EXPECT_NE(nullptr, stateV4);
  auto switchSettingsV4 = utility::getFirstNodeIf(stateV4->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV4);
  EXPECT_TRUE(switchSettingsV4->getEcmpGroupSettings().empty());
  EXPECT_FALSE(stateV4->getSplitHorizonEnabled(cfg::EcmpGroupType::FRR_BACKUP)
                   .has_value());
}

TEST(SwitchSettingsTest, applyL2AgeTimerSeconds) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  addSwitchInfo(stateV0, cfg::SwitchType::NPU, kNpuSwitchIdBegin /* switchId*/);

  // Check default value
  auto l2AgeTimerSeconds = 300;
  auto switchSettingsV0 = utility::getFirstNodeIf(stateV0->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV0);
  EXPECT_FALSE(switchSettingsV0->isPublished());
  EXPECT_EQ(l2AgeTimerSeconds, switchSettingsV0->getL2AgeTimerSeconds());

  // Check if value is updated
  l2AgeTimerSeconds *= 2;
  cfg::SwitchConfig config;
  config.switchSettings()->l2AgeTimerSeconds() = l2AgeTimerSeconds;
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto switchSettingsV1 = utility::getFirstNodeIf(stateV1->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_FALSE(switchSettingsV1->isPublished());
  EXPECT_EQ(l2AgeTimerSeconds, switchSettingsV1->getL2AgeTimerSeconds());
}

TEST(SwitchSettingsTest, applyMacAddrsToBlock) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  addSwitchInfo(stateV0, cfg::SwitchType::NPU, kNpuSwitchIdBegin /* switchId*/);

  // Check default value
  auto switchSettingsV0 = utility::getFirstNodeIf(stateV0->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV0);
  EXPECT_EQ(switchSettingsV0->getMacAddrsToBlock()->size(), 0);

  // Check if value is updated
  cfg::SwitchConfig config;

  cfg::MacAndVlan macAddrToBlock;
  macAddrToBlock.vlanID() = 1;
  macAddrToBlock.macAddress() = "00:11:22:33:44:55";

  config.switchSettings()->macAddrsToBlock() = {macAddrToBlock};
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto switchSettingsV1 = utility::getFirstNodeIf(stateV1->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_FALSE(switchSettingsV1->isPublished());
  EXPECT_EQ(switchSettingsV1->getMacAddrsToBlock()->size(), 1);

  EXPECT_EQ(
      int(switchSettingsV1->getMacAddrsToBlock_DEPRECATED()[0].first),
      macAddrToBlock.vlanID());
  EXPECT_EQ(
      switchSettingsV1->getMacAddrsToBlock_DEPRECATED()[0].second.toString(),
      macAddrToBlock.macAddress());
}

TEST(SwitchSettingsTest, applyMyMacs) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  addSwitchInfo(stateV0, cfg::SwitchType::NPU, kNpuSwitchIdBegin /* switchId*/);
  auto switchSettingsV0 = utility::getFirstNodeIf(stateV0->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV0);
  EXPECT_TRUE(switchSettingsV0->getMyMacs().empty());

  cfg::MacAndVlan myMac;
  myMac.vlanID() = 2000;
  myMac.macAddress() = "02:fb:00:00:00:01";
  cfg::SwitchConfig config;
  config.switchSettings()->myMacs() = {myMac};
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  auto switchSettingsV1 = utility::getFirstNodeIf(stateV1->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_EQ(switchSettingsV1->getMyMacs(), std::vector<cfg::MacAndVlan>{myMac});

  config.switchSettings()->myMacs()->clear();
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  ASSERT_NE(nullptr, stateV2);
  auto switchSettingsV2 = utility::getFirstNodeIf(stateV2->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV2);
  EXPECT_TRUE(switchSettingsV2->getMyMacs().empty());

  myMac.macAddress() = "not-a-mac";
  config.switchSettings()->myMacs() = {myMac};
  EXPECT_THROW(
      publishAndApplyConfig(stateV2, &config, platform.get()), FbossError);
}

TEST(SwitchSettingsTest, applyMyMacsUnsupportedAsic) {
  auto platform = createMockPlatform();
  cfg::MacAndVlan myMac;
  myMac.vlanID() = 2000;
  myMac.macAddress() = "02:fb:00:00:00:01";

  auto applyOnAsic = [&](cfg::AsicType asicType,
                         const std::vector<cfg::MacAndVlan>& myMacs) {
    auto state = make_shared<SwitchState>();
    addSwitchInfo(state, cfg::SwitchType::NPU, 0 /* switchId*/, asicType);
    cfg::SwitchConfig config;
    config.switchSettings()->switchIdToSwitchInfo() = {
        {0, createSwitchInfo(cfg::SwitchType::NPU, asicType)}};
    config.switchSettings()->myMacs() = myMacs;
    return publishAndApplyConfig(state, &config, platform.get());
  };

  EXPECT_NE(nullptr, applyOnAsic(cfg::AsicType::ASIC_TYPE_TOMAHAWK3, {myMac}));
  EXPECT_NO_THROW(applyOnAsic(cfg::AsicType::ASIC_TYPE_EBRO, {}));
  EXPECT_THROW(applyOnAsic(cfg::AsicType::ASIC_TYPE_EBRO, {myMac}), FbossError);
}

TEST(SwitchSettingsTest, applyMacOuis) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  addSwitchSettingsToState(stateV0);

  // Check default value
  auto matcher = HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(0)}));
  auto switchSettingsV0 =
      stateV0->getSwitchSettings()->getNode(matcher.matcherString());
  ASSERT_NE(nullptr, switchSettingsV0);
  EXPECT_EQ(switchSettingsV0->getVendorMacOuis()->size(), 0);
  EXPECT_EQ(switchSettingsV0->getMetaMacOuis()->size(), 0);

  // Check if value is updated
  cfg::SwitchConfig config;

  config.switchSettings()->vendorMacOuis() = {"01:02:03:00:00:00"};
  config.switchSettings()->metaMacOuis() = {"04:05:06:00:00:00"};
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto switchSettingsV1 =
      stateV1->getSwitchSettings()->getNode(matcher.matcherString());
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_FALSE(switchSettingsV1->isPublished());
  EXPECT_EQ(switchSettingsV1->getVendorMacOuis()->size(), 1);
  EXPECT_EQ(switchSettingsV1->getMetaMacOuis()->size(), 1);
  EXPECT_EQ(
      switchSettingsV1->getVendorMacOuis()->at(0)->toThrift(),
      "01:02:03:00:00:00");
  EXPECT_EQ(
      switchSettingsV1->getMetaMacOuis()->at(0)->toThrift(),
      "04:05:06:00:00:00");
}

TEST(SwitchSettingsTest, ThrifyMigration) {
  folly::IPAddress ip("1.1.1.1");
  auto addr = facebook::network::toBinaryAddress(ip);
  std::string jsonStr;
  apache::thrift::SimpleJSONSerializer::serialize(addr, &jsonStr);
  folly::dynamic addrDynamic = folly::parseJson(jsonStr);

  jsonStr = folly::toJson(addrDynamic);
  auto inBuf = folly::IOBuf::wrapBufferAsValue(jsonStr.data(), jsonStr.size());
  auto newAddr = apache::thrift::SimpleJSONSerializer::deserialize<
      facebook::network::thrift::BinaryAddress>(folly::io::Cursor{&inBuf});

  auto newIp = facebook::network::toIPAddress(newAddr);
  EXPECT_EQ(ip, newIp);
  EXPECT_EQ(addr, newAddr);

  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::SwitchConfig config;
  cfg::Neighbor blockNeighbor0;
  blockNeighbor0.vlanID() = 1;
  blockNeighbor0.ipAddress() = "1.1.1.1";
  cfg::Neighbor blockNeighbor1;
  blockNeighbor1.vlanID() = 2;
  blockNeighbor1.ipAddress() = "1::1";
  config.switchSettings()->blockNeighbors() = {blockNeighbor0, blockNeighbor1};

  cfg::MacAndVlan macAddrToBlock;
  macAddrToBlock.vlanID() = 1;
  macAddrToBlock.macAddress() = "00:11:22:33:44:55";
  config.switchSettings()->macAddrsToBlock() = {macAddrToBlock};

  *config.switchSettings()->qcmEnable() = true;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto switchSettingsV1 = utility::getFirstNodeIf(stateV1->getSwitchSettings());
  EXPECT_EQ(*switchSettingsV1->l3SwitchType(), cfg::SwitchType::NPU);
  EXPECT_TRUE(switchSettingsV1->vlansSupported());
  validateThriftMapMapSerialization(*stateV1->getSwitchSettings());
}

TEST(SwitchSettingsTest, applyVoqSwitch) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  addSwitchInfo(
      stateV0,
      cfg::SwitchType::VOQ,
      kVoqSwitchIdBegin /* switchId*/,
      cfg::AsicType::ASIC_TYPE_MOCK,
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN(),
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX(),
      0,
      10,
      54);

  // Check default value
  auto switchSettingsV0 = utility::getFirstNodeIf(stateV0->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV0);

  // Check if value is updated
  cfg::SwitchConfig config = testConfigA(cfg::SwitchType::VOQ);
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto switchSettingsV1 = utility::getFirstNodeIf(stateV1->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_FALSE(switchSettingsV1->isPublished());
  EXPECT_EQ(switchSettingsV1->getSwitchIds().size(), 1);
  EXPECT_EQ(
      *switchSettingsV1->getSwitchIds().begin(), SwitchID(kVoqSwitchIdBegin));
  EXPECT_EQ(switchSettingsV1->getSwitchIdToSwitchInfo().size(), 1);
  auto switchInfo =
      switchSettingsV1->getSwitchIdToSwitchInfo().at(kVoqSwitchIdBegin);
  EXPECT_EQ(switchInfo.switchType(), cfg::SwitchType::VOQ);
  EXPECT_EQ(switchInfo.switchIndex(), 0);
  EXPECT_EQ(
      switchInfo.portIdRange()->minimum(),
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN());
  EXPECT_EQ(
      switchInfo.portIdRange()->maximum(),
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX());
  EXPECT_EQ(*switchSettingsV1->l3SwitchType(), cfg::SwitchType::VOQ);
  EXPECT_EQ(
      switchSettingsV1->getSwitchIdsOfType(cfg::SwitchType::VOQ).size(), 1);
  EXPECT_EQ(
      switchSettingsV1->getSwitchIdsOfType(cfg::SwitchType::NPU).size(), 0);
  EXPECT_FALSE(switchSettingsV1->vlansSupported());
  EXPECT_EQ(switchInfo.asicType(), cfg::AsicType::ASIC_TYPE_MOCK);
  EXPECT_THROW(
      switchSettingsV1->getSwitchType(kVoqSwitchIdBegin + 1), FbossError);
  cfg::SwitchInfo switchInfo2;
  switchInfo2.switchType() = cfg::SwitchType::FABRIC;
  switchInfo2.asicType() = cfg::AsicType::ASIC_TYPE_MOCK;
  cfg::Range64 portIdRange;
  portIdRange.minimum() =
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN();
  portIdRange.maximum() =
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX();
  switchInfo2.portIdRange() = portIdRange;
  config.switchSettings()->switchIdToSwitchInfo() = {
      std::make_pair(2, switchInfo2)};
  EXPECT_THROW(
      publishAndApplyConfig(stateV1, &config, platform.get()), FbossError);
  // Should allow range and switchIndex changes
  // TODO - block this after config is rolled out everywhere
  switchInfo2.switchType() = cfg::SwitchType::VOQ;
  cfg::Range64 portIdRange2;
  portIdRange2.minimum() =
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN();
  portIdRange2.maximum() =
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX() + 1;
  switchInfo2.portIdRange() = portIdRange2;
  cfg::Range64 sysPortRange;
  sysPortRange.minimum() = 100;
  sysPortRange.maximum() = 200;
  switchInfo2.systemPortRanges()->systemPortRanges()->push_back(sysPortRange);
  switchInfo2.localSystemPortRanges()->systemPortRanges()->push_back(
      sysPortRange);
  switchInfo2.localSystemPortOffset() = *sysPortRange.minimum();
  switchInfo2.globalSystemPortOffset() = *sysPortRange.minimum();
  switchInfo2.inbandPortId() = kSingleStageInbandPortId;

  constexpr auto kMinLinksPerDeviceToRemainInVOQDomain = 5;
  constexpr auto kMinLinksPerDeviceToJoinVOQDomain = 7;
  switchInfo2.minLinksPerDeviceToRemainInVOQDomain() =
      kMinLinksPerDeviceToRemainInVOQDomain;
  switchInfo2.minLinksPerDeviceToJoinVOQDomain() =
      kMinLinksPerDeviceToJoinVOQDomain;
  config.switchSettings()->switchIdToSwitchInfo() = {
      std::make_pair(kVoqSwitchIdBegin, switchInfo2)};
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  EXPECT_NE(nullptr, stateV2);
  auto switchSettingsV2 = utility::getFirstNodeIf(stateV2->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV2);
  auto switchInfo3 =
      switchSettingsV2->getSwitchIdToSwitchInfo().at(kVoqSwitchIdBegin);
  EXPECT_EQ(
      switchInfo3.portIdRange()->minimum(),
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN());
  EXPECT_EQ(
      switchInfo3.portIdRange()->maximum(),
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX() + 1);
  EXPECT_EQ(
      switchInfo3.minLinksPerDeviceToRemainInVOQDomain(),
      kMinLinksPerDeviceToRemainInVOQDomain);
  EXPECT_EQ(
      switchInfo3.minLinksPerDeviceToJoinVOQDomain(),
      kMinLinksPerDeviceToJoinVOQDomain);
  EXPECT_EQ(switchInfo3.localSystemPortRanges()->systemPortRanges()->size(), 1);
  EXPECT_EQ(
      switchInfo3.localSystemPortRanges()->systemPortRanges()->at(0).minimum(),
      100);
  EXPECT_EQ(
      switchInfo3.localSystemPortRanges()->systemPortRanges()->at(0).maximum(),
      200);

  validateNodeSerialization(*switchSettingsV1);
}

TEST(SwitchSettingsTest, applyExactMatchTableConfig) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  addSwitchInfo(stateV0, cfg::SwitchType::NPU, kNpuSwitchIdBegin /* switchId*/);

  // Check default value
  auto switchSettingsV0 = utility::getFirstNodeIf(stateV0->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV0);
  EXPECT_EQ(switchSettingsV0->getExactMatchTableConfig()->size(), 0);

  // Check if value is updated
  cfg::SwitchConfig config;

  cfg::ExactMatchTableConfig tableConfig;
  tableConfig.name() = "TeFlowTable";
  tableConfig.dstPrefixLength() = 59;

  config.switchSettings()->exactMatchTableConfigs() = {tableConfig};
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto switchSettingsV1 = utility::getFirstNodeIf(stateV1->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_FALSE(switchSettingsV1->isPublished());
  EXPECT_EQ(switchSettingsV1->getExactMatchTableConfig()->size(), 1);
  EXPECT_EQ(
      switchSettingsV1->getExactMatchTableConfig()
          ->at(0)
          ->cref<switch_config_tags::name>()
          ->toThrift(),
      "TeFlowTable");

  EXPECT_EQ(
      switchSettingsV1->getExactMatchTableConfig()
          ->at(0)
          ->cref<switch_config_tags::dstPrefixLength>()
          ->toThrift(),
      59);

  // update prefix length
  (*config.switchSettings()->exactMatchTableConfigs())[0].dstPrefixLength() =
      51;
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  EXPECT_EQ(
      utility::getFirstNodeIf(stateV2->getSwitchSettings())
          ->getExactMatchTableConfig()
          ->at(0)
          ->cref<switch_config_tags::dstPrefixLength>()
          ->toThrift(),
      51);

  // delete the config
  cfg::SwitchConfig emptyConfig;
  auto stateV3 = publishAndApplyConfig(stateV2, &emptyConfig, platform.get());
  EXPECT_NE(nullptr, stateV3);
  EXPECT_EQ(
      utility::getFirstNodeIf(stateV3->getSwitchSettings())
          ->getExactMatchTableConfig()
          ->size(),
      0);
}

TEST(SwitchSettingsTest, applyDefaultVlanConfig) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  addSwitchInfo(stateV0, cfg::SwitchType::NPU, kNpuSwitchIdBegin /* switchId*/);

  // Check default value
  auto switchSettingsV0 = utility::getFirstNodeIf(stateV0->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV0);
  EXPECT_EQ(switchSettingsV0->getDefaultVlan(), std::nullopt);

  // Check whether value is updated
  cfg::SwitchConfig config = testConfigA();
  config.defaultVlan() = 1;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto switchSettingsV1 = utility::getFirstNodeIf(stateV1->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_FALSE(switchSettingsV1->isPublished());
  EXPECT_EQ(switchSettingsV1->getDefaultVlan(), 1);
}

TEST(SwitchSettingsTest, applyArpNdpTimeoutConfig) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  addSwitchInfo(stateV0, cfg::SwitchType::NPU, kNpuSwitchIdBegin /* switchId*/);

  // Check default value
  auto switchSettingsV0 = utility::getFirstNodeIf(stateV0->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV0);
  EXPECT_EQ(switchSettingsV0->getArpTimeout(), std::nullopt);
  EXPECT_EQ(switchSettingsV0->getNdpTimeout(), std::nullopt);
  EXPECT_EQ(switchSettingsV0->getArpAgerInterval(), std::nullopt);
  EXPECT_EQ(switchSettingsV0->getStaleEntryInterval(), std::nullopt);
  EXPECT_EQ(switchSettingsV0->getMaxNeighborProbes(), std::nullopt);

  // Check whether value is updated
  cfg::SwitchConfig config = testConfigA();
  config.arpTimeoutSeconds() = 300;
  config.arpAgerInterval() = 200;
  config.staleEntryInterval() = 200;
  config.maxNeighborProbes() = 100;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto switchSettingsV1 = utility::getFirstNodeIf(stateV1->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_FALSE(switchSettingsV1->isPublished());
  EXPECT_EQ(switchSettingsV1->getArpTimeout(), std::chrono::seconds(300));
  EXPECT_EQ(switchSettingsV1->getNdpTimeout(), std::chrono::seconds(300));
  EXPECT_EQ(switchSettingsV1->getArpAgerInterval(), std::chrono::seconds(200));
  EXPECT_EQ(
      switchSettingsV1->getStaleEntryInterval(), std::chrono::seconds(200));
  EXPECT_EQ(switchSettingsV1->getMaxNeighborProbes(), 100);
}

TEST(SwitchSettingsTest, applyDhcpConfig) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  addSwitchInfo(stateV0, cfg::SwitchType::NPU, kNpuSwitchIdBegin /* switchId*/);

  const folly::IPAddressV6 kDhcpV6RelaySrc("100::1");
  const folly::IPAddressV6 kDhcpV6ReplySrc("101::1");
  const folly::IPAddressV4 kDhcpV4RelaySrc("100.0.0.1");
  const folly::IPAddressV4 kDhcpV4ReplySrc("101.0.0.1");

  // Check default value
  auto switchSettingsV0 = utility::getFirstNodeIf(stateV0->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV0);
  EXPECT_EQ(switchSettingsV0->getDhcpV4RelaySrc(), std::nullopt);
  EXPECT_EQ(switchSettingsV0->getDhcpV6RelaySrc(), std::nullopt);
  EXPECT_EQ(switchSettingsV0->getDhcpV4ReplySrc(), std::nullopt);
  EXPECT_EQ(switchSettingsV0->getDhcpV6ReplySrc(), std::nullopt);

  // Check whether value is updated
  cfg::SwitchConfig config = testConfigA();
  config.dhcpRelaySrcOverrideV4() = "100.0.0.1";
  config.dhcpReplySrcOverrideV4() = "101.0.0.1";
  config.dhcpRelaySrcOverrideV6() = "100::1";
  config.dhcpReplySrcOverrideV6() = "101::1";

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto switchSettingsV1 = utility::getFirstNodeIf(stateV1->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_FALSE(switchSettingsV1->isPublished());
  EXPECT_EQ(switchSettingsV1->getDhcpV4RelaySrc(), kDhcpV4RelaySrc);
  EXPECT_EQ(switchSettingsV1->getDhcpV6RelaySrc(), kDhcpV6RelaySrc);
  EXPECT_EQ(switchSettingsV1->getDhcpV4ReplySrc(), kDhcpV4ReplySrc);
  EXPECT_EQ(switchSettingsV1->getDhcpV6ReplySrc(), kDhcpV6ReplySrc);
}

TEST(SwitchSettingsTest, applyHostnameConfig) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  addSwitchInfo(stateV0, cfg::SwitchType::NPU, kNpuSwitchIdBegin /* switchId*/);

  // Check the default value
  auto switchSettingsV0 = utility::getFirstNodeIf(stateV0->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV0);
  auto hostnameV0 = switchSettingsV0->getHostname();
  EXPECT_NE(hostnameV0, std::nullopt); // System hostname by default

  // Check if the value is updated
  cfg::SwitchConfig config = testConfigA();
  const std::string newHostname = "test-host";
  config.hostname() = newHostname;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto switchSettingsV1 = utility::getFirstNodeIf(stateV1->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_FALSE(switchSettingsV1->isPublished());
  auto hostnameV1 = switchSettingsV1->getHostname();
  ASSERT_NE(hostnameV1, std::nullopt);
  EXPECT_EQ(hostnameV1->cref(), newHostname);
}

TEST(SwitchSettingsTest, applyIcmpV4UnavailableSrcAddress) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  addSwitchInfo(stateV0, cfg::SwitchType::NPU, kNpuSwitchIdBegin /* switchId*/);

  const folly::IPAddressV4 kIcmpV4UnavailableSrcAddress("100.0.0.1");

  // Check the default value
  auto switchSettingsV0 = utility::getFirstNodeIf(stateV0->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV0);
  EXPECT_EQ(
      switchSettingsV0->getIcmpV4UnavailableSrcAddress(),
      folly::IPAddressV4({192, 0, 0, 8}));

  // Check if the value is updated
  cfg::SwitchConfig config = testConfigA();
  config.icmpV4UnavailableSrcAddress() = "100.0.0.1";

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto switchSettingsV1 = utility::getFirstNodeIf(stateV1->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_FALSE(switchSettingsV1->isPublished());
  EXPECT_EQ(
      switchSettingsV1->getIcmpV4UnavailableSrcAddress(),
      kIcmpV4UnavailableSrcAddress);
}

TEST(SwitchSettingsTest, modify) {
  auto stateV0 = make_shared<SwitchState>();
  auto multiSwitchSwitchSettingsV0 = make_shared<MultiSwitchSettings>();
  auto switchSettingsV0 = make_shared<SwitchSettings>();
  multiSwitchSwitchSettingsV0->addNode(
      scope().matcherString(), switchSettingsV0);
  stateV0->resetSwitchSettings(multiSwitchSwitchSettingsV0);
  EXPECT_EQ(switchSettingsV0.get(), switchSettingsV0->modify(&stateV0));
  stateV0->publish();
  auto newSwitchSettingsV0 = switchSettingsV0->modify(&stateV0);
  EXPECT_NE(newSwitchSettingsV0, switchSettingsV0.get());
  EXPECT_NE(
      stateV0->getSwitchSettings().get(), multiSwitchSwitchSettingsV0.get());
}

TEST(SwitchSettingsTest, applyMinLinksToRemainInVOQDomain) {
  constexpr auto kMinLinksToRemainInVOQDomain = 5;
  constexpr auto kMinLinksToRemainInVOQDomain2 = 7;
  auto platform = createMockPlatform();

  // Setting minLinksToRemainInVOQDomain is not supported for NPU
  {
    auto stateV0 = make_shared<SwitchState>();
    addSwitchInfo(
        stateV0, cfg::SwitchType::NPU, kNpuSwitchIdBegin /* switchId*/);
    addSwitchInfo(
        stateV0, cfg::SwitchType::FABRIC, kFabricSwitchIdBegin /* switchId*/);
    cfg::SwitchConfig npuConfig = testConfigA(cfg::SwitchType::NPU);
    npuConfig.switchSettings()->minLinksToRemainInVOQDomain() =
        kMinLinksToRemainInVOQDomain;
    EXPECT_THROW(
        publishAndApplyConfig(stateV0, &npuConfig, platform.get()), FbossError);

    // Setting minLinksToRemainInVOQDomain is not supported for FABRIC
    cfg::SwitchConfig fabricConfig = testConfigA(cfg::SwitchType::FABRIC);
    fabricConfig.switchSettings()->minLinksToRemainInVOQDomain() =
        kMinLinksToRemainInVOQDomain;
    EXPECT_THROW(
        publishAndApplyConfig(stateV0, &fabricConfig, platform.get()),
        FbossError);
  }

  // Setting minLinksToRemainInVOQDomain is supported for VOQ
  {
    auto stateV0 = make_shared<SwitchState>();
    addSwitchInfo(
        stateV0, cfg::SwitchType::VOQ, kVoqSwitchIdBegin /* switchId*/);
    cfg::SwitchConfig voqConfig = testConfigA(cfg::SwitchType::VOQ);
    voqConfig.switchSettings()->minLinksToRemainInVOQDomain() =
        kMinLinksToRemainInVOQDomain;
    auto stateV1 = publishAndApplyConfig(stateV0, &voqConfig, platform.get());
    EXPECT_NE(nullptr, stateV1);

    auto switchSettingsV1 =
        utility::getFirstNodeIf(stateV1->getSwitchSettings());
    ASSERT_NE(nullptr, switchSettingsV1);
    EXPECT_FALSE(switchSettingsV1->isPublished());
    ASSERT_TRUE(switchSettingsV1->getMinLinksToRemainInVOQDomain().has_value());
    EXPECT_EQ(
        kMinLinksToRemainInVOQDomain,
        switchSettingsV1->getMinLinksToRemainInVOQDomain().value());

    voqConfig.switchSettings()->minLinksToRemainInVOQDomain() =
        kMinLinksToRemainInVOQDomain2;
    auto stateV2 = publishAndApplyConfig(stateV1, &voqConfig, platform.get());
    EXPECT_NE(nullptr, stateV2);

    auto switchSettingsV2 =
        utility::getFirstNodeIf(stateV2->getSwitchSettings());
    ASSERT_NE(nullptr, switchSettingsV2);
    EXPECT_FALSE(switchSettingsV2->isPublished());
    ASSERT_TRUE(switchSettingsV2->getMinLinksToRemainInVOQDomain().has_value());
    EXPECT_EQ(
        kMinLinksToRemainInVOQDomain2,
        switchSettingsV2->getMinLinksToRemainInVOQDomain().value());
  }
}

TEST(SwitchSettingsTest, applyMinLinksToJoinVOQDomain) {
  constexpr auto kMinLinksToJoinVOQDomain = 5;
  constexpr auto kMinLinksToJoinVOQDomain2 = 7;
  auto platform = createMockPlatform();

  // Setting minLinksToJoinVOQDomai is not supported for NPU
  {
    auto stateV0 = make_shared<SwitchState>();
    addSwitchInfo(
        stateV0, cfg::SwitchType::NPU, kNpuSwitchIdBegin /* switchId*/);
    cfg::SwitchConfig npuConfig = testConfigA(cfg::SwitchType::NPU);
    npuConfig.switchSettings()->minLinksToJoinVOQDomain() =
        kMinLinksToJoinVOQDomain;
    EXPECT_THROW(
        publishAndApplyConfig(stateV0, &npuConfig, platform.get()), FbossError);
  }

  // Setting minLinksToJoinVOQDomai is not supported for FABRIC
  {
    auto stateV0 = make_shared<SwitchState>();
    addSwitchInfo(
        stateV0, cfg::SwitchType::FABRIC, kFabricSwitchIdBegin /* switchId*/);
    cfg::SwitchConfig fabricConfig = testConfigA(cfg::SwitchType::FABRIC);
    fabricConfig.switchSettings()->minLinksToJoinVOQDomain() =
        kMinLinksToJoinVOQDomain;
    EXPECT_THROW(
        publishAndApplyConfig(stateV0, &fabricConfig, platform.get()),
        FbossError);
  }

  // Setting minLinksToJoinVOQDomai is supported for VOQ
  {
    auto stateV0 = make_shared<SwitchState>();
    addSwitchInfo(
        stateV0, cfg::SwitchType::VOQ, kVoqSwitchIdBegin /* switchId*/);
    cfg::SwitchConfig voqConfig = testConfigA(cfg::SwitchType::VOQ);
    voqConfig.switchSettings()->minLinksToJoinVOQDomain() =
        kMinLinksToJoinVOQDomain;
    auto stateV1 = publishAndApplyConfig(stateV0, &voqConfig, platform.get());
    EXPECT_NE(nullptr, stateV1);

    auto switchSettingsV1 =
        utility::getFirstNodeIf(stateV1->getSwitchSettings());
    ASSERT_NE(nullptr, switchSettingsV1);
    EXPECT_FALSE(switchSettingsV1->isPublished());
    ASSERT_TRUE(switchSettingsV1->getMinLinksToJoinVOQDomain().has_value());
    EXPECT_EQ(
        kMinLinksToJoinVOQDomain,
        switchSettingsV1->getMinLinksToJoinVOQDomain().value());

    voqConfig.switchSettings()->minLinksToJoinVOQDomain() =
        kMinLinksToJoinVOQDomain2;
    auto stateV2 = publishAndApplyConfig(stateV1, &voqConfig, platform.get());
    EXPECT_NE(nullptr, stateV2);

    auto switchSettingsV2 =
        utility::getFirstNodeIf(stateV2->getSwitchSettings());
    ASSERT_NE(nullptr, switchSettingsV2);
    EXPECT_FALSE(switchSettingsV2->isPublished());
    ASSERT_TRUE(switchSettingsV2->getMinLinksToJoinVOQDomain().has_value());
    EXPECT_EQ(
        kMinLinksToJoinVOQDomain2,
        switchSettingsV2->getMinLinksToJoinVOQDomain().value());
  }
}

TEST(SwitchSettingsTest, applyDefaultVoqConfig) {
  const auto kEcnQueueId{4};
  auto aqmEcnConfigGet = [&](int minLength, int maxLength) {
    cfg::ActiveQueueManagement ecnAQM;
    cfg::LinearQueueCongestionDetection ecnLQCD;
    ecnLQCD.minimumLength() = minLength;
    ecnLQCD.maximumLength() = maxLength;
    ecnAQM.detection()->linear() = ecnLQCD;
    ecnAQM.behavior() = cfg::QueueCongestionBehavior::ECN;
    return ecnAQM;
  };

  auto verifyAqmsEcnState = [&](std::shared_ptr<SwitchState> state,
                                int minLength,
                                int maxLength) {
    auto& defaultVoqConfig = utility::getFirstNodeIf(state->getSwitchSettings())
                                 ->getDefaultVoqConfig();
    for (const auto& voqConfig : defaultVoqConfig) {
      if (voqConfig->getID() == kEcnQueueId) {
        if (!voqConfig->getAqms() || voqConfig->getAqms()->empty()) {
          continue;
        }
        const auto& aqms = std::as_const(*voqConfig->getAqms());
        const auto& aqm = *aqms.begin();
        auto thresholds = aqm->cref<switch_config_tags::detection>()
                              ->cref<switch_config_tags::linear>()
                              ->toThrift();
        EXPECT_EQ(*thresholds.minimumLength(), minLength);
        EXPECT_EQ(*thresholds.maximumLength(), maxLength);
        return;
      }
    }
    throw FbossError("Could not find queueId ", kEcnQueueId, " in config!");
  };

  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  cfg::SwitchConfig config;

  std::vector<cfg::PortQueue> voqConfig{};
  const std::vector<int> kQueueIds = {0, 2, kEcnQueueId, 7};
  for (const auto queueId : kQueueIds) {
    cfg::PortQueue queue;
    *queue.id() = queueId;
    queue.streamType() = cfg::StreamType::UNICAST;
    queue.name() = folly::to<std::string>("queue", queueId);
    *queue.scheduling() = cfg::QueueScheduling::INTERNAL;
    queue.scalingFactor() = cfg::MMUScalingFactor::ONE;
    queue.reservedBytes() = 1500; // Set to possible MTU!

    if (queueId == kEcnQueueId) {
      queue.aqms() = {};
      queue.aqms()->push_back(aqmEcnConfigGet(600000, 600000));
    }
    voqConfig.push_back(queue);
  }
  config.defaultVoqConfig() = voqConfig;
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  verifyAqmsEcnState(stateV1, 600000, 600000);

  // Modify ECN threshold and make sure it takes effect
  for (auto& queue : voqConfig) {
    if (*queue.id() == kEcnQueueId) {
      queue.aqms() = {};
      queue.aqms()->push_back(aqmEcnConfigGet(300000, 300000));
      break;
    }
  }
  config.defaultVoqConfig() = voqConfig;
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  EXPECT_NE(nullptr, stateV2);
  verifyAqmsEcnState(stateV2, 300000, 300000);
}

TEST(SwitchSettingsTest, ecmpWidthGetterSetterRoundTrip) {
  auto settings = make_shared<SwitchSettings>();
  EXPECT_FALSE(settings->getEcmpWidth().has_value());

  settings->setEcmpWidth(128);
  ASSERT_TRUE(settings->getEcmpWidth().has_value());
  EXPECT_EQ(128, *settings->getEcmpWidth());

  settings->setEcmpWidth(std::nullopt);
  EXPECT_FALSE(settings->getEcmpWidth().has_value());
}

TEST(SwitchSettingsTest, applyEcmpWidthFromGflag) {
  gflags::FlagSaver flagSaver;

  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  cfg::SwitchConfig config;

  FLAGS_ecmp_width = 128;
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  auto switchSettingsV1 = utility::getFirstNodeIf(stateV1->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV1);
  ASSERT_TRUE(switchSettingsV1->getEcmpWidth().has_value());
  EXPECT_EQ(128, *switchSettingsV1->getEcmpWidth());

  FLAGS_ecmp_width = 64;
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  ASSERT_NE(nullptr, stateV2);
  auto switchSettingsV2 = utility::getFirstNodeIf(stateV2->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV2);
  EXPECT_EQ(64, *switchSettingsV2->getEcmpWidth());
}

TEST(SwitchSettingsTest, applyEcmpWidthFromConfig) {
  gflags::FlagSaver flagSaver;

  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  cfg::SwitchConfig config;

  // Config value must win over the gflag.
  FLAGS_ecmp_width = 64;
  config.switchSettings()->ecmpWidth() = 512;
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  auto switchSettingsV1 = utility::getFirstNodeIf(stateV1->getSwitchSettings());
  ASSERT_NE(nullptr, switchSettingsV1);
  ASSERT_TRUE(switchSettingsV1->getEcmpWidth().has_value());
  EXPECT_EQ(512, *switchSettingsV1->getEcmpWidth());
}

TEST(SwitchSettingsTest, rejectNonPositiveEcmpWidth) {
  gflags::FlagSaver flagSaver;

  auto platform = createMockPlatform();
  FLAGS_ecmp_width = 64;
  for (const auto invalidEcmpWidth : {-1, 0}) {
    auto state = make_shared<SwitchState>();
    cfg::SwitchConfig config;
    config.switchSettings()->ecmpWidth() = invalidEcmpWidth;
    EXPECT_THROW(
        publishAndApplyConfig(state, &config, platform.get()), FbossError);
  }
}

TEST(SwitchSettingsTest, ecmpWidthSerializesToThrift) {
  auto settings = make_shared<SwitchSettings>();
  settings->setEcmpWidth(256);

  auto thrift = settings->toThrift();
  ASSERT_TRUE(thrift.ecmpWidth().has_value());
  EXPECT_EQ(256, *thrift.ecmpWidth());

  auto unset = make_shared<SwitchSettings>();
  EXPECT_FALSE(unset->toThrift().ecmpWidth().has_value());
}
