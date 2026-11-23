/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/bcm/tests/BcmTest.h"

#include "fboss/agent/AlpmUtils.h"
#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/bcm/BcmAddressFBConvertors.h"
#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmIntf.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/SwitchState.h"

#include "fboss/agent/hw/test/ConfigFactory.h"

#include <folly/logging/xlog.h>
#include <string>
#include <vector>

extern "C" {
#include <bcm/l2.h>
}

using folly::ByteRange;
using folly::IPAddress;
using std::make_shared;
using std::shared_ptr;
using std::string;
using namespace facebook::fboss;

namespace {

int L3HostCountCb(
    int /*unit*/,
    int /*index*/,
    bcm_l3_host_t* info,
    void* user_data) {
  int* count = static_cast<int*>(user_data);
  *count += 1;

  auto ip = info->l3a_flags & BCM_L3_IP6
      ? IPAddress::fromBinary(
            ByteRange(info->l3a_ip6_addr, sizeof(info->l3a_ip6_addr)))
      : IPAddress::fromLongHBO(info->l3a_ip_addr);
  XLOG(DBG1) << "L3HostEntry in vrf : " << info->l3a_vrf
             << " route for : " << ip;
  return 0;
}

int L3RouteCountCb(
    int /*unit*/,
    int /*index*/,
    bcm_l3_route_t* info,
    void* user_data) {
  int* count = static_cast<int*>(user_data);
  *count += 1;

  auto ip = info->l3a_flags & BCM_L3_IP6
      ? IPAddress::fromBinary(
            ByteRange(info->l3a_ip6_net, sizeof(info->l3a_ip6_net)))
      : IPAddress::fromLongHBO(info->l3a_subnet);
  auto mask = info->l3a_flags & BCM_L3_IP6
      ? IPAddress::fromBinary(
            ByteRange(info->l3a_ip6_mask, sizeof(info->l3a_ip6_mask)))
      : IPAddress::fromLongHBO(info->l3a_ip_mask);
  XLOG(DBG1) << "L3RouteEntry in vrf : " << info->l3a_vrf
             << " route for : " << ip << " mask: " << mask;
  return 0;
}

bcm_l3_info_t getHwStatus(int unit) {
  bcm_l3_info_t l3HwStatus;
  auto rv = bcm_l3_info(unit, &l3HwStatus);
  bcmCheckError(rv, "failed get L3 hw info");
  return l3HwStatus;
}

int getHwRouteCount(int unit, bool isV6) {
  auto l3HwStatus = getHwStatus(unit);
  int l3RouteCount = 0;
  int firstRouteEntry = 0;
  int lastRouteEntry = l3HwStatus.l3info_max_route;
  bcm_l3_route_traverse(
      unit,
      isV6 ? BCM_L3_IP6 : 0,
      firstRouteEntry,
      lastRouteEntry,
      L3RouteCountCb,
      &l3RouteCount);
  return l3RouteCount;
}

int getHwHostCount(int unit, bool isV6) {
  auto l3HwStatus = getHwStatus(unit);
  int firstHostEntry = 0;
  int lastHostEntry = l3HwStatus.l3info_max_host;
  int l3HostCount = 0;
  bcm_l3_host_traverse(
      unit,
      isV6 ? BCM_L3_IP6 : 0,
      firstHostEntry,
      lastHostEntry,
      L3HostCountCb,
      &l3HostCount);
  return l3HostCount;
}

int getHwRouteCount(int unit) {
  return getHwRouteCount(unit, false) + getHwRouteCount(unit, true);
}

int getHwHostCount(int unit) {
  return getHwHostCount(unit, false) + getHwHostCount(unit, true);
}

void checkSwHwIntfMatch(
    int unit,
    std::shared_ptr<SwitchState> state,
    bool verifyIngress) {
  for (const auto& [_, intfMap] : std::as_const(*state->getInterfaces())) {
    for (auto iter : std::as_const(*intfMap)) {
      const auto& swIntf = iter.second;
      bcm_l3_info_t l3HwStatus;
      auto rv = bcm_l3_info(unit, &l3HwStatus);
      bcmCheckError(rv, "failed get L3 hw info");
      bcm_l3_intf_t intf;
      bcm_l3_intf_t_init(&intf);
      intf.l3a_vid = swIntf->getID();
      rv = bcm_l3_intf_find_vlan(unit, &intf);
      bcmCheckError(rv, "failed to find l3 intf");
      EXPECT_EQ(swIntf->getMac(), macFromBcm(intf.l3a_mac_addr));
      ASSERT_EQ(swIntf->getMtu(), intf.l3a_mtu);
      ASSERT_EQ(RouterID(0), RouterID(intf.l3a_vrf));

      if (verifyIngress) {
        bcm_l3_ingress_t ing_intf;
        bcm_l3_ingress_t_init(&ing_intf);
        rv = bcm_l3_ingress_get(unit, intf.l3a_intf_id, &ing_intf);
        bcmCheckError(rv, "failed to get L3 ingress"); // FAILS
        ASSERT_EQ(BcmSwitch::getBcmVrfId(RouterID(0)), ing_intf.vrf);

        bcm_vlan_control_vlan_t vlan_ctrl;
        bcm_vlan_control_vlan_t_init(&vlan_ctrl);
        rv = bcm_vlan_control_vlan_get(unit, swIntf->getVlanID(), &vlan_ctrl);
        bcmCheckError(rv, "failed to get bcm_vlan");
        ASSERT_EQ(intf.l3a_intf_id, vlan_ctrl.ingress_if);
      }
    }
  }
}

} // namespace

namespace facebook::fboss {

class BcmInterfaceTest : public BcmTest {
 protected:
  cfg::SwitchConfig initialConfig() const override {
    return utility::twoL3IntfConfig(
        getHwSwitch(), masterLogicalPortIds()[0], masterLogicalPortIds()[1]);
  }
  static int
  bcm_l2_traverse_cb(int /*unit*/, bcm_l2_addr_t* info, void* user_data) {
    if (!user_data) {
      return 0;
    }
    auto* outvec = reinterpret_cast<std::vector<bcm_l2_addr_t>*>(user_data);
    outvec->push_back(*info);
    return 0;
  }
};

TEST_F(BcmInterfaceTest, InterfaceApplyConfig) {
  auto setup = [=, this]() {
    auto newCfg = initialConfig();
    newCfg.interfaces()[0].mtu() = 9000;
    applyNewConfig(newCfg);
  };
  auto verify = [=, this]() {
    // FakeAsic not supported (not all APIs are defined in FakeSDK) T92705046
    bool verifyIngress = getHwSwitch()->getPlatform()->getAsic()->isSupported(
                             HwAsic::Feature::INGRESS_L3_INTERFACE) &&
        getAsic()->getAsicType() != cfg::AsicType::ASIC_TYPE_FAKE;
    checkSwHwIntfMatch(getUnit(), getProgrammedState(), verifyIngress);
    if (getHwSwitch()->getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::HOSTTABLE)) {
      // 4 + 1 fe80::/64 link local addresses
      ASSERT_EQ(5 + numMinAlpmRoutes(), getHwRouteCount(getUnit()));
      // In total, 4 l3 hosts. 2 IPv4, 2 IPv6.
      // There are 2 v6 link-local addresses, which are not installed in
      // the host table.
      ASSERT_EQ(4, getHwHostCount(getUnit()));
    } else {
      // host entries are programmed to route table
      ASSERT_EQ(9 + numMinAlpmRoutes(), getHwRouteCount(getUnit()));
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmInterfaceTest, fromJumboToNonJumboInterface) {
  auto setup = [=, this]() {
    auto newCfg = initialConfig();
    newCfg.interfaces()[0].mtu() = 9000;
    applyNewConfig(newCfg);

    newCfg.interfaces()[0].mtu() = Interface::kDefaultMtu;
    applyNewConfig(newCfg);
  };
  auto verify = [=, this]() {
    bcm_l3_info_t l3HwStatus;
    auto rv = bcm_l3_info(getUnit(), &l3HwStatus);
    bcmCheckError(rv, "failed get L3 hw info");

    // FakeAsic not supported (not all APIs are defined in FakeSDK) T92705046
    bool verifyIngress = getHwSwitch()->getPlatform()->getAsic()->isSupported(
                             HwAsic::Feature::INGRESS_L3_INTERFACE) &&
        getAsic()->getAsicType() != cfg::AsicType::ASIC_TYPE_FAKE;
    checkSwHwIntfMatch(getUnit(), getProgrammedState(), verifyIngress);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmInterfaceTest, checkMplsEnabled) {
  auto setup = [=, this]() { applyNewConfig(initialConfig()); };
  auto verify = [=, this]() {
    std::vector<bcm_l2_addr_t> l2_addrs;
    bcm_l2_traverse(
        getHwSwitch()->getUnit(),
        &BcmInterfaceTest::bcm_l2_traverse_cb,
        &l2_addrs);
    for (const auto& l2_addr : l2_addrs) {
      auto station_flags =
          (reinterpret_cast<const bcm_l2_addr_t*>(&l2_addr))->station_flags;

      EXPECT_EQ((station_flags & BCM_L2_STATION_MPLS), BCM_L2_STATION_MPLS);
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
