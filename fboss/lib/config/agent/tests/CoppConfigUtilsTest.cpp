/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <array>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/hw/switch_asics/ChenabAsic.h"
#include "fboss/agent/hw/switch_asics/Qumran4DAsic.h"
#include "fboss/agent/hw/switch_asics/Tomahawk5Asic.h"
#include "fboss/lib/config/agent/AclConfigUtils.h"
#include "fboss/lib/config/agent/CoppConfigUtils.h"

namespace facebook::fboss {
namespace {

cfg::SwitchInfo makeSwitchInfo(
    cfg::AsicType asicType,
    cfg::SwitchType switchType) {
  cfg::SwitchInfo switchInfo;
  switchInfo.switchType() = switchType;
  switchInfo.asicType() = asicType;
  switchInfo.switchIndex() = 0;
  switchInfo.switchMac() = "02:00:00:00:00:01";
  return switchInfo;
}

TEST(CoppConfigUtilsTest, AddsEtherTypeFromNetworkAddressFamilyWhenRequired) {
  Tomahawk5Asic asic(
      0,
      makeSwitchInfo(cfg::AsicType::ASIC_TYPE_TOMAHAWK5, cfg::SwitchType::NPU));
  Qumran4DAsic etherTypeAsic(
      0,
      makeSwitchInfo(cfg::AsicType::ASIC_TYPE_QUMRAN4D, cfg::SwitchType::VOQ));

  const auto acls = utility::genLinkLocalCoppAcls(asic, {}, {});
  const auto etherTypeAcls =
      utility::genLinkLocalCoppAcls(etherTypeAsic, {}, {});
  ASSERT_EQ(acls.size(), 6);
  ASSERT_EQ(etherTypeAcls.size(), 6);
  for (const auto& coppAcl : acls) {
    EXPECT_FALSE(coppAcl.first.etherType().has_value());
  }
  for (const auto& coppAcl : etherTypeAcls) {
    ASSERT_TRUE(coppAcl.first.etherType().has_value());
    EXPECT_EQ(*coppAcl.first.etherType(), cfg::EtherType::IPv6);
  }

  const auto ipv4LinkLocalNetwork =
      folly::IPAddress::createNetwork("169.254.0.0/16");
  const auto ipv4Acl = utility::genLinkLocalCoppAcl(
      etherTypeAsic, "ipv4-link-local", ipv4LinkLocalNetwork);
  EXPECT_EQ(*ipv4Acl.dstIp(), "169.254.0.0/16");
  ASSERT_TRUE(ipv4Acl.etherType().has_value());
  EXPECT_EQ(*ipv4Acl.etherType(), cfg::EtherType::IPv4);
}

TEST(CoppConfigUtilsTest, GeneratesCoppQueueActionsForUserDefinedTrapFlag) {
  Tomahawk5Asic asic(
      0,
      makeSwitchInfo(cfg::AsicType::ASIC_TYPE_TOMAHAWK5, cfg::SwitchType::NPU));
  constexpr auto kQueueId = 9;

  const auto sendToQueueAction = utility::genCoppQueueAction(
      false /* enableUserDefinedTrap */,
      asic,
      kQueueId,
      cfg::ToCpuAction::COPY);
  ASSERT_TRUE(sendToQueueAction.sendToQueue().has_value());
  EXPECT_EQ(*sendToQueueAction.sendToQueue()->queueId(), kQueueId);
  EXPECT_FALSE(sendToQueueAction.setTc().has_value());
  EXPECT_FALSE(sendToQueueAction.userDefinedTrap().has_value());
  EXPECT_EQ(*sendToQueueAction.toCpuAction(), cfg::ToCpuAction::COPY);

  const auto userDefinedTrapAction = utility::genCoppQueueAction(
      true /* enableUserDefinedTrap */, asic, kQueueId, cfg::ToCpuAction::COPY);
  EXPECT_FALSE(userDefinedTrapAction.sendToQueue().has_value());
  ASSERT_TRUE(userDefinedTrapAction.setTc().has_value());
  EXPECT_EQ(*userDefinedTrapAction.setTc()->tcValue(), kQueueId);
  ASSERT_TRUE(userDefinedTrapAction.userDefinedTrap().has_value());
  EXPECT_EQ(*userDefinedTrapAction.userDefinedTrap()->queueId(), kQueueId);
  EXPECT_EQ(*userDefinedTrapAction.toCpuAction(), cfg::ToCpuAction::COPY);

  const auto forwardAction = utility::genCoppQueueAction(
      true /* enableUserDefinedTrap */, asic, kQueueId);
  EXPECT_FALSE(forwardAction.sendToQueue().has_value());
  ASSERT_TRUE(forwardAction.setTc().has_value());
  EXPECT_EQ(*forwardAction.setTc()->tcValue(), kQueueId);
  EXPECT_FALSE(forwardAction.userDefinedTrap().has_value());
  EXPECT_FALSE(forwardAction.toCpuAction().has_value());
}

TEST(CoppConfigUtilsTest, RemovesUnsupportedSetTcWithUserDefinedTrapCpuAction) {
  ChenabAsic asic(
      0, makeSwitchInfo(cfg::AsicType::ASIC_TYPE_CHENAB, cfg::SwitchType::NPU));

  const auto action = utility::genCoppQueueAction(
      true /* enableUserDefinedTrap */, asic, 3, cfg::ToCpuAction::TRAP);

  EXPECT_FALSE(action.sendToQueue().has_value());
  EXPECT_FALSE(action.setTc().has_value());
  ASSERT_TRUE(action.userDefinedTrap().has_value());
  EXPECT_EQ(*action.userDefinedTrap()->queueId(), 3);
  EXPECT_EQ(*action.toCpuAction(), cfg::ToCpuAction::TRAP);
}

TEST(CoppConfigUtilsTest, AddsBasicNpuCoppConfigWithoutFlatAcls) {
  Tomahawk5Asic asic(
      0,
      makeSwitchInfo(cfg::AsicType::ASIC_TYPE_TOMAHAWK5, cfg::SwitchType::NPU));
  cfg::SwitchConfig config;
  ASSERT_TRUE(utility::setupDefaultAclTableGroups(config, asic));

  utility::addDefaultCpuQueueConfig(config, asic);
  utility::addDefaultCpuTrafficPolicyConfig(
      config, asic, true /* useUserDefinedTrap */);

  ASSERT_EQ(config.cpuQueues()->size(), 4);
  EXPECT_EQ(
      std::vector<int16_t>({
          *config.cpuQueues()->at(0).id(),
          *config.cpuQueues()->at(1).id(),
          *config.cpuQueues()->at(2).id(),
          *config.cpuQueues()->at(3).id(),
      }),
      std::vector<int16_t>({9, 2, 1, 0}));
  EXPECT_FALSE(config.cpuQueues()->at(0).portQueueRate().has_value());
  EXPECT_FALSE(config.cpuQueues()->at(1).portQueueRate().has_value());
  EXPECT_TRUE(config.cpuQueues()->at(2).portQueueRate().has_value());
  EXPECT_TRUE(config.cpuQueues()->at(3).portQueueRate().has_value());
  for (const auto& queue : *config.cpuQueues()) {
    EXPECT_EQ(*queue.scheduling(), cfg::QueueScheduling::STRICT_PRIORITY);
    EXPECT_FALSE(queue.weight().has_value());
  }

  ASSERT_TRUE(config.cpuTrafficPolicy().has_value());
  ASSERT_EQ(
      config.cpuTrafficPolicy()->rxReasonToQueueOrderedList()->size(), 13);
  const auto& matchToActions =
      *config.cpuTrafficPolicy()->trafficPolicy()->matchToAction();
  ASSERT_EQ(matchToActions.size(), 6);
  EXPECT_EQ(
      *config.cpuTrafficPolicy()
           ->rxReasonToQueueOrderedList()
           ->back()
           .rxReason(),
      cfg::PacketRxReason::UNMATCHED);

  EXPECT_TRUE(config.acls()->empty());
  ASSERT_EQ(config.aclTableGroups()->size(), 1);
  const auto& tables = config.aclTableGroups()->front().aclTables().value();
  ASSERT_EQ(tables.size(), 1);
  const auto& aclEntries = *tables.front().aclEntries();
  ASSERT_EQ(aclEntries.size(), matchToActions.size());

  constexpr std::array<std::string_view, 2> kLinkLocalNetworks = {
      "fe80::/10", "ff02::/16"};
  for (std::size_t i = 0; i < aclEntries.size(); ++i) {
    const auto& acl = aclEntries[i];
    const auto& matchToAction = matchToActions[i];
    EXPECT_EQ(*matchToAction.matcher(), *acl.name());
    ASSERT_TRUE(acl.dstIp().has_value());
    EXPECT_EQ(*acl.dstIp(), kLinkLocalNetworks[i % kLinkLocalNetworks.size()]);

    const auto& action = *matchToAction.action();
    if (i < 2) {
      ASSERT_TRUE(acl.srcPort().has_value());
      EXPECT_EQ(
          *acl.srcPort(), cfg::switch_config_constants::CPU_PORT_LOGICALID());
      EXPECT_FALSE(action.sendToQueue().has_value());
      EXPECT_FALSE(action.setTc().has_value());
      EXPECT_FALSE(action.userDefinedTrap().has_value());
      EXPECT_FALSE(action.toCpuAction().has_value());
    } else {
      EXPECT_FALSE(acl.srcPort().has_value());
      const auto expectedQueueId =
          i < 4 ? asic.getHiPriCpuQueueId() : asic.getMidPriCpuQueueId();
      EXPECT_FALSE(action.sendToQueue().has_value());
      ASSERT_TRUE(action.setTc().has_value());
      EXPECT_EQ(*action.setTc()->tcValue(), expectedQueueId);
      ASSERT_TRUE(action.userDefinedTrap().has_value());
      EXPECT_EQ(*action.userDefinedTrap()->queueId(), expectedQueueId);
      ASSERT_TRUE(action.toCpuAction().has_value());
      EXPECT_EQ(*action.toCpuAction(), cfg::ToCpuAction::COPY);
      if (i < 4) {
        ASSERT_TRUE(acl.dscp().has_value());
        EXPECT_EQ(*acl.dscp(), 48);
      } else {
        EXPECT_FALSE(acl.dscp().has_value());
      }
    }
  }
}

TEST(CoppConfigUtilsTest, SkipsNonNpuSwitch) {
  Qumran4DAsic asic(
      0,
      makeSwitchInfo(cfg::AsicType::ASIC_TYPE_QUMRAN4D, cfg::SwitchType::VOQ));
  cfg::SwitchConfig config;

  utility::addDefaultCpuQueueConfig(config, asic);
  utility::addDefaultCpuTrafficPolicyConfig(
      config, asic, false /* useUserDefinedTrap */);

  EXPECT_TRUE(config.cpuQueues()->empty());
  EXPECT_FALSE(config.cpuTrafficPolicy().has_value());
  EXPECT_TRUE(config.acls()->empty());
  EXPECT_FALSE(config.aclTableGroups().has_value());
}

} // namespace
} // namespace facebook::fboss
