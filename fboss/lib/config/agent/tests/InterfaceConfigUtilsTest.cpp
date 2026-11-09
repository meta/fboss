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

#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/switch_asics/P200Asic.h"
#include "fboss/agent/hw/switch_asics/Qumran4DAsic.h"
#include "fboss/agent/hw/switch_asics/Tomahawk5Asic.h"
#include "fboss/lib/config/agent/InterfaceConfigUtils.h"
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

cfg::Vlan makeLoopbackVlan() {
  auto vlan = utility::createVlanConfig(VlanID(utility::kFbossLoopbackVlanId));
  vlan.name() = "fbossLoopback0";
  return vlan;
}

cfg::Interface makeLoopbackInterface(VlanID vlanID) {
  auto intf = utility::createVlanInterfaceConfig(
      InterfaceID(utility::kFbossLoopbackVlanId), vlanID);
  intf.name().reset();
  intf.isVirtual() = true;
  intf.isStateSyncDisabled() = true;
  return intf;
}

} // namespace

TEST(InterfaceConfigUtilsTest, createVlanInterfaceConfigSetsBasicFields) {
  const auto intf =
      utility::createVlanInterfaceConfig(InterfaceID(2001), VlanID(2001));

  EXPECT_EQ(*intf.name(), "2001");
  EXPECT_EQ(*intf.intfID(), 2001);
  EXPECT_EQ(*intf.vlanID(), 2001);
  EXPECT_EQ(*intf.type(), cfg::InterfaceType::VLAN);
  EXPECT_EQ(*intf.routerID(), 0);
  EXPECT_EQ(*intf.scope(), cfg::Scope::LOCAL);
  EXPECT_EQ(*intf.mtu(), 9000);
  EXPECT_FALSE(intf.mac().has_value());
  EXPECT_TRUE(intf.ipAddresses()->empty());
}

TEST(
    InterfaceConfigUtilsTest,
    addDefaultLoopbackInterfaceCreatesBackingVlanForClassicNpu) {
  Tomahawk5Asic asic(
      0,
      makeSwitchInfo(cfg::AsicType::ASIC_TYPE_TOMAHAWK5, cfg::SwitchType::NPU));
  cfg::SwitchConfig config;
  utility::addDefaultVlan(config, asic);

  utility::addDefaultLoopbackInterface(config, asic);

  const std::vector<cfg::Vlan> expectedVlans{
      makeLoopbackVlan(),
      makeDefaultVlan(VlanID(utility::kDefaultVlanId4094)),
  };
  EXPECT_EQ(*config.vlans(), expectedVlans);
  const std::vector<cfg::Interface> expectedInterfaces{
      makeLoopbackInterface(VlanID(utility::kFbossLoopbackVlanId)),
  };
  EXPECT_EQ(*config.interfaces(), expectedInterfaces);
}

TEST(
    InterfaceConfigUtilsTest,
    addDefaultLoopbackInterfaceReusesDefaultVlanForP200) {
  P200Asic asic(
      0, makeSwitchInfo(cfg::AsicType::ASIC_TYPE_P200, cfg::SwitchType::NPU));
  cfg::SwitchConfig config;
  utility::addDefaultVlan(config, asic);

  utility::addDefaultLoopbackInterface(config, asic);

  const std::vector<cfg::Vlan> expectedVlans{
      makeDefaultVlan(VlanID(utility::kDefaultVlanId1)),
  };
  EXPECT_EQ(*config.vlans(), expectedVlans);
  const std::vector<cfg::Interface> expectedInterfaces{
      makeLoopbackInterface(VlanID(utility::kDefaultVlanId1)),
  };
  EXPECT_EQ(*config.interfaces(), expectedInterfaces);
}

TEST(
    InterfaceConfigUtilsTest,
    addDefaultLoopbackInterfaceRequiresDefaultVlanForP200) {
  P200Asic asic(
      0, makeSwitchInfo(cfg::AsicType::ASIC_TYPE_P200, cfg::SwitchType::NPU));
  cfg::SwitchConfig config;

  EXPECT_THROW(utility::addDefaultLoopbackInterface(config, asic), FbossError);
}

TEST(InterfaceConfigUtilsTest, addDefaultLoopbackInterfaceSkipsNonNpu) {
  Qumran4DAsic asic(
      0,
      makeSwitchInfo(cfg::AsicType::ASIC_TYPE_QUMRAN4D, cfg::SwitchType::VOQ));
  cfg::SwitchConfig config;

  utility::addDefaultLoopbackInterface(config, asic);

  EXPECT_TRUE(config.vlans()->empty());
  EXPECT_TRUE(config.interfaces()->empty());
}

} // namespace facebook::fboss
