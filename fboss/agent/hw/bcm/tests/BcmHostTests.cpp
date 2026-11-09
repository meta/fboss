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

#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/hw/bcm/BcmAclEntry.h"
#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmHost.h"
#include "fboss/agent/hw/bcm/BcmIntf.h"
#include "fboss/agent/hw/bcm/BcmRoute.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/hw/bcm/tests/BcmTestUtils.h"
#include "fboss/agent/hw/test/HwSwitchEnsembleRouteUpdateWrapper.h"
#include "fboss/agent/state/Interface.h"

#include "fboss/agent/hw/test/ConfigFactory.h"

extern "C" {
#include <bcm/error.h>
#include <bcm/l3.h>
}

using folly::CIDRNetwork;
using folly::IPAddress;
using namespace facebook::fboss;
using namespace facebook::fboss::utility;
using VrfAndIP = std::pair<bcm_vrf_t, folly::IPAddress>;
using VrfAndIP2Host = boost::container::flat_map<VrfAndIP, bcm_l3_host_t>;
using VrfAndIP2Route = boost::container::flat_map<VrfAndIP, bcm_l3_route_t>;

namespace {
void initL3HostWithAddr(bcm_l3_host_t* l3Host, const folly::IPAddress& addr) {
  bcm_l3_host_t_init(l3Host);
  if (addr.isV4()) {
    l3Host->l3a_ip_addr = addr.asV4().toLongHBO();
  } else {
    memcpy(
        &l3Host->l3a_ip6_addr,
        addr.asV6().toByteArray().data(),
        sizeof(l3Host->l3a_ip6_addr));
    l3Host->l3a_flags |= BCM_L3_IP6;
  }
}

void initL3HostRouteWithAddr(
    bcm_l3_route_t* l3Route,
    const folly::IPAddress& addr) {
  bcm_l3_route_t_init(l3Route);
  if (addr.isV4()) {
    l3Route->l3a_subnet = addr.asV4().toLongHBO();
    l3Route->l3a_ip_mask =
        folly::IPAddressV4(folly::IPAddressV4::fetchMask(addr.bitCount()))
            .toLongHBO();
  } else {
    memcpy(
        &l3Route->l3a_ip6_net,
        addr.asV6().toByteArray().data(),
        sizeof(l3Route->l3a_ip6_net));
    memcpy(
        &l3Route->l3a_ip6_mask,
        folly::IPAddressV6::fetchMask(addr.bitCount()).data(),
        sizeof(l3Route->l3a_ip6_mask));
    l3Route->l3a_flags |= BCM_L3_IP6;
  }
}

int hostTraversalCallback(
    int /*unit*/,
    int /*index*/,
    bcm_l3_host_t* host,
    void* userData) {
  VrfAndIP2Host* l3HostMap = static_cast<VrfAndIP2Host*>(userData);
  auto ip = host->l3a_flags & BCM_L3_IP6
      ? folly::IPAddress::fromBinary(
            folly::ByteRange(host->l3a_ip6_addr, sizeof(host->l3a_ip6_addr)))
      : folly::IPAddress::fromLongHBO(host->l3a_ip_addr);
  l3HostMap->emplace(std::make_pair(host->l3a_vrf, ip), *host);
  return 0;
}

int hostRouteTraversalCallback(
    int /*unit*/,
    int /*index*/,
    bcm_l3_route_t* route,
    void* userData) {
  VrfAndIP2Route* l3HostRouteMap = static_cast<VrfAndIP2Route*>(userData);
  bool isIPv6 = route->l3a_flags & BCM_L3_IP6;
  auto ip = isIPv6
      ? folly::IPAddress::fromBinary(
            folly::ByteRange(route->l3a_ip6_net, sizeof(route->l3a_ip6_net)))
      : folly::IPAddress::fromLongHBO(route->l3a_subnet);
  if ((isIPv6 &&
       memcmp(
           route->l3a_ip6_mask,
           folly::IPAddressV6::fetchMask(folly::IPAddressV6::bitCount()).data(),
           sizeof(route->l3a_ip6_mask)) == 0) ||
      (!isIPv6 &&
       route->l3a_ip_mask ==
           folly::IPAddressV4(
               folly::IPAddressV4::fetchMask(folly::IPAddressV4::bitCount()))
               .toLongHBO())) {
    l3HostRouteMap->emplace(std::make_pair(route->l3a_vrf, ip), *route);
  }
  return 0;
}

VrfAndIP2Host getL3HostFromHw(int unit) {
  VrfAndIP2Host l3HostMap;

  bcm_l3_info_t l3Info;
  bcm_l3_info_t_init(&l3Info);
  bcm_l3_info(unit, &l3Info);
  // Traverse V4 hosts
  bcm_l3_host_traverse(
      unit, 0, 0, l3Info.l3info_max_host, hostTraversalCallback, &l3HostMap);
  // Traverse V6 hosts
  bcm_l3_host_traverse(
      unit,
      BCM_L3_IP6,
      0,
      // Diag shell uses this for getting # of v6 host entries
      l3Info.l3info_max_host / 2,
      hostTraversalCallback,
      &l3HostMap);

  return l3HostMap;
}

VrfAndIP2Route getL3HostRouteFromHw(int unit) {
  VrfAndIP2Route l3HostRouteMap;

  bcm_l3_info_t l3Info;
  bcm_l3_info_t_init(&l3Info);
  bcm_l3_info(unit, &l3Info);

  // Traverse V4 hosts
  bcm_l3_route_traverse(
      unit,
      0,
      0,
      l3Info.l3info_max_route,
      hostRouteTraversalCallback,
      &l3HostRouteMap);
  // Traverse V6 hosts
  bcm_l3_route_traverse(
      unit,
      BCM_L3_IP6,
      0,
      l3Info.l3info_max_route,
      hostRouteTraversalCallback,
      &l3HostRouteMap);

  return l3HostRouteMap;
}
} // unnamed namespace

namespace facebook::fboss {

class BcmHostTest : public BcmTest {
 protected:
  cfg::SwitchConfig initialConfig() const override {
    return utility::oneL3IntfConfig(getHwSwitch(), masterLogicalPortIds()[0]);
  }

  void checkBcmHostMatchInHw(const BcmHostIf* host, bool isLocalIp) {
    // check host exists in asic
    bcm_vrf_t vrf;
    bcm_if_t intf;
    int lookupClassId;
    if (getHwSwitch()->getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::HOSTTABLE)) {
      bcm_l3_host_t l3Host;
      initL3HostWithAddr(&l3Host, host->getHostKey().addr());
      auto rc = bcm_l3_host_find(getUnit(), &l3Host);
      bcmCheckError(
          rc, "failed to find L3 host object for ", host->getHostKey().str());
      vrf = l3Host.l3a_vrf;
      intf = l3Host.l3a_intf;
      lookupClassId = l3Host.l3a_lookup_class;
    } else {
      bcm_l3_route_t l3Route;
      initL3HostRouteWithAddr(&l3Route, host->getHostKey().addr());
      auto rc = bcm_l3_route_get(getUnit(), &l3Route);
      bcmCheckError(
          rc,
          "failed to find L3 host route object for ",
          host->getHostKey().str());
      vrf = l3Route.l3a_vrf;
      intf = l3Route.l3a_intf;
      lookupClassId = l3Route.l3a_lookup_class;
    }
    CHECK_EQ(host->getHostKey().getVrf(), vrf);
    CHECK_EQ(host->getEgressId(), intf);
    if (isLocalIp) {
      if (host->getHostKey().addr().isV4()) {
        CHECK_EQ(host->getLookupClassId(), BcmAclEntry::kLocalIp4DstClassL3Id);
      } else {
        CHECK_EQ(host->getLookupClassId(), BcmAclEntry::kLocalIp6DstClassL3Id);
      }
    } else {
      // other non local ip host should be 0
      CHECK_EQ(host->getLookupClassId(), 0);
    }
    CHECK_EQ(host->getLookupClassId(), lookupClassId);
  }

  void checkSwHwBcmHostNum(int expectedNum) {
    // we also assign link local address to each interface by default, but
    // we don't program it to HW
    int numSwHosts = 0;
    int numHwHosts = 0;
    if (getHwSwitch()->getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::HOSTTABLE)) {
      numSwHosts = getHwSwitch()->getHostTable()->getNumBcmHost();
      numHwHosts = getL3HostFromHw(getUnit()).size();
    } else {
      numSwHosts = getHwSwitch()->routeTable()->getNumBcmHostRoute();
      numHwHosts = getL3HostRouteFromHw(getUnit()).size();
    }
    CHECK_EQ(numSwHosts, expectedNum + 1);
    CHECK_EQ(numHwHosts, expectedNum);
  }

  BcmHostIf* getExistingBcmHost(
      InterfaceID intfID,
      folly::IPAddress ipAddress) {
    const BcmIntf* intf = getHwSwitch()->getIntfTable()->getBcmIntf(intfID);
    bcm_vrf_t vrf = BcmSwitch::getBcmVrfId(intf->getInterface()->getRouterID());
    if (getHwSwitch()->getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::HOSTTABLE)) {
      return getHwSwitch()->getHostTable()->getBcmHostIf(
          BcmHostKey(vrf, ipAddress, intfID));
    } else {
      return getHwSwitch()->routeTable()->getBcmHostIf(
          BcmHostKey(vrf, ipAddress, intfID));
    }
  }
};

TEST_F(BcmHostTest, CreateV4AndV6L3Host) {
  auto setup = [=, this]() { applyNewConfig(initialConfig()); };
  auto verify = [=, this]() {
    InterfaceID intfID = InterfaceID(utility::kBaseVlanId);
    const auto& swIntf = getProgrammedState()->getInterfaces()->getNode(intfID);
    int expectedHostNum{0};
    for (auto iter : std::as_const(*swIntf->getAddresses())) {
      auto addr = folly::IPAddress(iter.first);
      if (!addr.isLinkLocal()) {
        expectedHostNum++;
        checkBcmHostMatchInHw(getExistingBcmHost(swIntf->getID(), addr), true);
      }
    }
    checkSwHwBcmHostNum(expectedHostNum);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmHostTest, CreateInterfaceWithoutIp) {
  auto setup = [=, this]() {
    applyNewConfig(
        utility::oneL3IntfNoIPAddrConfig(
            getHwSwitch(), masterLogicalPortIds()[0]));
  };
  auto verify = [=, this]() { checkSwHwBcmHostNum(0); };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmHostTest, CreateInterfaceWithHostIp) {
  auto setup = [=, this]() {
    auto config = utility::oneL3IntfNoIPAddrConfig(
        getHwSwitch(), masterLogicalPortIds()[0]);
    config.interfaces()[0].ipAddresses()->resize(2);
    config.interfaces()[0].ipAddresses()[0] = "1.1.1.1/32";
    config.interfaces()[0].ipAddresses()[1] = "1::/128";
    applyNewConfig(config);
  };
  auto verify = [=, this]() { checkSwHwBcmHostNum(2); };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmHostTest, DeleteV4AndV6L3Host) {
  auto setup = [=, this]() {
    // first apply config which interface has ip addresses assigned
    applyNewConfig(initialConfig());
    // check HW side should has only 2 l3_host created
    if (getHwSwitch()->getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::HOSTTABLE)) {
      CHECK_EQ(getL3HostFromHw(getUnit()).size(), 2);
    } else {
      CHECK_EQ(getL3HostRouteFromHw(getUnit()).size(), 2);
    }
    // then we re-apply config with 0 ip address assigned interface
    applyNewConfig(
        utility::oneL3IntfNoIPAddrConfig(
            getHwSwitch(), masterLogicalPortIds()[0]));
  };
  auto verify = [=, this]() { checkSwHwBcmHostNum(0); };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmHostTest, HostRouteLookupClassNotSet) {
  if (!getPlatform()->canUseHostTableForHostRoutes()) {
    GTEST_SKIP();
  }
  std::array<std::pair<CIDRNetwork, IPAddress>, 2> networkAndNexthops = {
      std::make_pair(
          CIDRNetwork(IPAddress("20.1.1.0"), 32), IPAddress("2.2.2.10")),
      std::make_pair(
          CIDRNetwork(IPAddress("2001::0"), 128), IPAddress("2::10"))};
  auto setup = [=, this]() {
    applyNewConfig(initialConfig());
    auto updater = getHwSwitchEnsemble()->getRouteUpdater();
    for (const auto& networkAndNexthop : networkAndNexthops) {
      boost::container::flat_set<NextHop> nexthopSet{
          UnresolvedNextHop(networkAndNexthop.second, ECMP_WEIGHT)};
      updater.addRoute(
          RouterID(0),
          networkAndNexthop.first.first,
          networkAndNexthop.first.second,
          ClientID(1001),
          RouteNextHopEntry(
              std::move(nexthopSet), AdminDistance::MAX_ADMIN_DISTANCE));
    }
    updater.program();
  };
  auto verify = [=, this]() {
    for (const auto& networkAndNexthop : networkAndNexthops) {
      const BcmHostTableIf* hostTableIf;
      if (getHwSwitch()->getPlatform()->getAsic()->isSupported(
              HwAsic::Feature::HOSTTABLE)) {
        hostTableIf = getHwSwitch()->getHostTable();
      } else {
        hostTableIf = getHwSwitch()->routeTable();
      }
      auto hostRoute = hostTableIf->getBcmHostIf(BcmHostKey(
          getHwSwitch()->getBcmVrfId(RouterID(0)),
          networkAndNexthop.first.first));
      // Host route should not set dst class l3 id
      checkBcmHostMatchInHw(hostRoute, false);
    }
  };

  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
