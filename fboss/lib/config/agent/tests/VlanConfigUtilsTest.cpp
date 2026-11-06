/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gtest/gtest.h>

#include "fboss/agent/hw/switch_asics/P200Asic.h"
#include "fboss/agent/hw/switch_asics/Qumran4DAsic.h"
#include "fboss/agent/hw/switch_asics/Tomahawk5Asic.h"
#include "fboss/lib/config/agent/VlanConfigUtils.h"

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

cfg::Vlan makeDefaultVlan(VlanID vlanID) {
  auto vlan = utility::createVlanConfig(vlanID);
  vlan.name() = "default";
  vlan.routable() = false;
  return vlan;
}

} // namespace

TEST(VlanConfigUtilsTest, createVlanConfigSetsBasicFields) {
  const auto vlan = utility::createVlanConfig(VlanID(2001));
  EXPECT_EQ(*vlan.id(), 2001);
  EXPECT_EQ(*vlan.name(), "vlan2001");
  EXPECT_TRUE(*vlan.routable());
  EXPECT_TRUE(*vlan.recordStats());
}

TEST(VlanConfigUtilsTest, addDefaultVlanUsesLegacyVlanForClassicNpu) {
  Tomahawk5Asic asic(
      0,
      makeSwitchInfo(cfg::AsicType::ASIC_TYPE_TOMAHAWK5, cfg::SwitchType::NPU));
  cfg::SwitchConfig config;

  utility::addDefaultVlan(config, asic);

  const std::vector<cfg::Vlan> expectedVlans{
      makeDefaultVlan(VlanID(utility::kDefaultVlanId4094)),
  };
  EXPECT_EQ(*config.vlans(), expectedVlans);
  EXPECT_EQ(*config.defaultVlan(), utility::kDefaultVlanId4094);
}

TEST(VlanConfigUtilsTest, addDefaultVlanUsesVlanOneForP200) {
  P200Asic asic(
      0, makeSwitchInfo(cfg::AsicType::ASIC_TYPE_P200, cfg::SwitchType::NPU));
  cfg::SwitchConfig config;

  utility::addDefaultVlan(config, asic);

  const std::vector<cfg::Vlan> expectedVlans{
      makeDefaultVlan(VlanID(utility::kDefaultVlanId1)),
  };
  EXPECT_EQ(*config.vlans(), expectedVlans);
  EXPECT_EQ(*config.defaultVlan(), utility::kDefaultVlanId1);
}

TEST(VlanConfigUtilsTest, addDefaultVlanSkipsNonNpu) {
  Qumran4DAsic asic(
      0,
      makeSwitchInfo(cfg::AsicType::ASIC_TYPE_QUMRAN4D, cfg::SwitchType::VOQ));
  cfg::SwitchConfig config;

  utility::addDefaultVlan(config, asic);

  EXPECT_TRUE(config.vlans()->empty());
  EXPECT_EQ(*config.defaultVlan(), 0);
}

} // namespace facebook::fboss
