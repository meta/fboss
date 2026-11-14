/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/AlpmUtils.h"
#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/bcm/BcmAddressFBConvertors.h"
#include "fboss/agent/hw/bcm/BcmEcmpUtils.h"
#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmHost.h"
#include "fboss/agent/hw/bcm/BcmIntf.h"
#include "fboss/agent/hw/bcm/BcmRoute.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/hw/bcm/tests/BcmTest.h"
#include "fboss/agent/hw/bcm/tests/BcmTestUtils.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/hw/test/HwSwitchEnsembleRouteUpdateWrapper.h"
#include "fboss/agent/platforms/tests/utils/BcmTestPlatform.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/NdpTable.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/SwitchState.h"

#include "fboss/agent/test/EcmpSetupHelper.h"

#include <boost/range/combine.hpp>
#include <fmt/format.h>
#include <folly/logging/xlog.h>

#include <string>

extern "C" {
#include <bcm/l3.h>
}

using folly::ByteRange;
using folly::CIDRNetwork;
using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::MacAddress;
using std::make_shared;
using std::shared_ptr;
using std::string;
using namespace facebook::fboss;
using facebook::fboss::utility::getEcmpSizeInHw;
using facebook::fboss::utility::kBaseVlanId;

namespace {
int L3EgressPrintCb(
    int unit,
    bcm_if_t intf,
    bcm_l3_egress_t* info,
    void* /*user_data*/) {
  XLOG(DBG3) << "L3EgressEntry in unit : " << unit << " intf : " << intf
             << " flag : " << std::showbase << std::hex << info->flags
             << " info->intf : " << std::showbase << info->intf;
  return 0;
}

int L3RouteCountCb(
    int unit,
    int /*index*/,
    bcm_l3_route_t* info,
    void* user_data) {
  int* count = static_cast<std::pair<int*, bool>*>(user_data)->first;
  bool verifyL3Intf = static_cast<std::pair<int*, bool>*>(user_data)->second;
  *count += 1;

  auto ip = info->l3a_flags & BCM_L3_IP6
      ? IPAddress::fromBinary(
            ByteRange(info->l3a_ip6_net, sizeof(info->l3a_ip6_net)))
      : IPAddress::fromLongHBO(info->l3a_subnet);
  auto mask = info->l3a_flags & BCM_L3_IP6
      ? IPAddress::fromBinary(
            ByteRange(info->l3a_ip6_mask, sizeof(info->l3a_ip6_mask)))
      : IPAddress::fromLongHBO(info->l3a_ip_mask);
  auto intf = info->l3a_intf;

  bcm_l3_egress_t egr;
  bcm_l3_egress_t_init(&egr);
  auto rv = bcm_l3_egress_get(unit, intf, &egr);
  bcmCheckError(rv, "failed to get l4 egress");
  if (!verifyL3Intf) {
    XLOG(DBG3) << "L3RouteEntry in vrf : " << info->l3a_vrf
               << " route for : " << ip << " mask: " << mask
               << " egr.intf: " << std::showbase << std::hex << egr.intf
               << std::dec << " intf: " << intf;
    return 0;
  }

  bcm_l3_intf_t intfInfo;
  bcm_l3_intf_t_init(&intfInfo);
  intfInfo.l3a_intf_id = egr.intf;
  rv = bcm_l3_intf_get(unit, &intfInfo);
  bcmCheckError(rv, fmt::format("failed to get l3 intf: {}", egr.intf));

  auto mac = MacAddress::fromBinary(
      ByteRange(intfInfo.l3a_mac_addr, MacAddress::SIZE));
  auto vlan = intfInfo.l3a_vid;

  XLOG(DBG3) << "L3RouteEntry in vrf : " << info->l3a_vrf
             << " route for : " << ip << " mask: " << mask
             << " egr.intf: " << std::showbase << std::hex << egr.intf
             << std::dec << " intf: " << intf << " mac: " << mac
             << " vlan: " << vlan;

  return 0;
}

int L3RouteToCPUCb(
    int unit,
    int /*index*/,
    bcm_l3_route_t* info,
    void* user_data) {
  bool* allToCPU = static_cast<bool*>(user_data);
  auto intf = info->l3a_intf;
  bcm_l3_egress_t egr;
  auto rv = bcm_l3_egress_get(unit, intf, &egr);
  bcmCheckError(rv, "failed to get l3 egress");

  if ((egr.flags & BCM_L3_L2TOCPU) == 0 &&
      (egr.flags & BCM_L3_DST_DISCARD) == 0) {
    XLOG(DBG2) << "egress " << intf
               << " does not have BCM_L3_L2TOCPU or DISCARD flag";
    *allToCPU = false;
  }

  return 0;
}

int getHwRouteCount(
    int unit,
    const IPAddress& networkIP,
    bcm_l3_info_t hwStatus) {
  int routeCount = 0;
  auto routeTraverseCb = [](int, int, bcm_l3_route_t*, void* user_data) {
    int* count = static_cast<int*>(user_data);
    *count += 1;

    return 0;
  };
  auto rv = bcm_l3_route_traverse(
      unit,
      networkIP.isV4() ? 0 : BCM_L3_IP6,
      0,
      hwStatus.l3info_max_route,
      routeTraverseCb,
      &routeCount);
  bcmCheckError(rv, "failed to l3 route traverse");
  return routeCount;
}

int getHwHostRouteCount(
    int unit,
    const IPAddress& networkIP,
    bcm_l3_info_t hwStatus) {
  int hostRouteCount = 0;
  auto hostTraverseCb = [](int, int, bcm_l3_host_t*, void* user_data) {
    int* count = static_cast<int*>(user_data);
    *count += 1;

    return 0;
  };
  auto rv = bcm_l3_host_traverse(
      unit,
      networkIP.isV4() ? 0 : BCM_L3_IP6,
      0,
      hwStatus.l3info_max_host,
      hostTraverseCb,
      &hostRouteCount);
  bcmCheckError(rv, "failed to l3 host traverse");
  return hostRouteCount;
}

void initBcmL3RouteT(
    bcm_l3_route_t& route,
    const IPAddress& networkIP,
    uint8_t netmask) {
  bcm_l3_route_t_init(&route);
  if (networkIP.isV4()) {
    route.l3a_subnet = networkIP.asV4().toLongHBO();
    route.l3a_ip_mask =
        folly::IPAddressV4(folly::IPAddressV4::fetchMask(netmask)).toLongHBO();
  } else { // IPv6
    ipToBcmIp6(networkIP, &route.l3a_ip6_net);
    memcpy(
        &route.l3a_ip6_mask,
        folly::IPAddressV6::fetchMask(netmask).data(),
        sizeof(route.l3a_ip6_mask));
    route.l3a_flags = BCM_L3_IP6;
  }
}

int getHwRoute(int unit, const IPAddress& networkIP, uint8_t netmask) {
  bcm_l3_route_t route;
  initBcmL3RouteT(route, networkIP, netmask);
  return bcm_l3_route_get(unit, &route);
}

int getHwRoute(
    int unit,
    const IPAddress& networkIP,
    uint8_t netmask,
    bcm_l3_route_t& route) {
  initBcmL3RouteT(route, networkIP, netmask);
  return bcm_l3_route_get(unit, &route);
}

int findHwHost(int unit, const IPAddress& networkIP, bcm_l3_host_t& host) {
  bcm_l3_host_t_init(&host);
  if (networkIP.isV4()) {
    host.l3a_ip_addr = networkIP.asV4().toLongHBO();
  } else { // IPv6
    ipToBcmIp6(networkIP, &host.l3a_ip6_addr);
    host.l3a_flags = BCM_L3_IP6;
  }

  return bcm_l3_host_find(unit, &host);
}

const RouterID kRouter0(0);

} // namespace

namespace facebook::fboss {

class BcmRouteTest : public BcmTest {
 protected:
  cfg::SwitchConfig initialConfig() const override {
    return utility::twoL3IntfConfig(
        getHwSwitch(), masterLogicalPortIds()[0], masterLogicalPortIds()[1]);
  }

  BcmHostKey getHostKey(IPAddress address, RouterID routerID = kRouter0) {
    return BcmHostKey(getHwSwitch()->getBcmVrfId(routerID), address);
  }

  std::tuple<int, int> computeNonHostInterfaceRoutes() {
    int ipv4Routes, ipv6Routes;

    ipv4Routes = ipv6Routes = 0;
    for (const auto& [_, intfMap] :
         std::as_const(*getProgrammedState()->getInterfaces())) {
      for (auto intfIter : std::as_const(*intfMap)) {
        const auto& interface = intfIter.second;
        for (auto iter : std::as_const(*interface->getAddresses())) {
          auto mask = iter.second;
          auto ipAddress = folly::IPAddress(iter.first);

          if (ipAddress.isV4() && mask != 32) {
            ipv4Routes++;
          }
          if (ipAddress.isV6() && !ipAddress.isLinkLocal() && mask != 128) {
            ipv6Routes++;
          }
        }
      }
    }

    return std::make_tuple(ipv4Routes, ipv6Routes);
  }

  long referenceCount(const BcmMultiPathNextHopKey& key) {
    return getHwSwitch()
        ->getMultiPathNextHopTable()
        ->getNextHops()
        .referenceCount(key);
  }

  void routeReferenceCountTest(
      const CIDRNetwork& network,
      const IPAddress& nexthop);

  void verifyBcmHostReference(bcm_vrf_t vrf, const RouteNextHopSet& nexthops);

  void verifyNextHopReferences(const std::vector<IPAddress>& nexthops);
  void addRoute(
      folly::CIDRNetwork network,
      const std::vector<folly::IPAddress>& nexthopAddrs) {
    auto updater = getHwSwitchEnsemble()->getRouteUpdater();
    RouteNextHopEntry::NextHopSet nexthops;

    for (const auto& nexthopAddr : nexthopAddrs) {
      nexthops.emplace(UnresolvedNextHop(nexthopAddr, ECMP_WEIGHT));
    }
    updater.addRoute(
        kRouter0,
        network.first,
        network.second,
        ClientID(1001),
        RouteNextHopEntry(
            std::move(nexthops), AdminDistance::MAX_ADMIN_DISTANCE));
    updater.program();
  }
  void delRoute(folly::CIDRNetwork network) {
    auto updater = getHwSwitchEnsemble()->getRouteUpdater();
    updater.delRoute(kRouter0, network.first, network.second, ClientID(1001));
    updater.program();
  }
};

class BcmRouteHostReferenceTest : public BcmRouteTest {
 protected:
  cfg::SwitchConfig initialConfig() const override {
    return utility::oneL3IntfConfig(getHwSwitch(), masterLogicalPortIds()[0]);
  }

  /*
   * A key in host table, based on next hop and interface id
   */
  BcmMultiPathNextHopKey getKey(
      IPAddress nexthop,
      int interface,
      NextHopWeight weight = UCMP_DEFAULT_WEIGHT) {
    return BcmMultiPathNextHopKey(
        kRouter0,
        RouteNextHopEntry(
            static_cast<NextHop>(
                ResolvedNextHop(nexthop, InterfaceID(interface), weight)),
            AdminDistance::MAX_ADMIN_DISTANCE)
            .getNextHopSet(),
        std::nullopt);
  }

  long referenceCount(const BcmMultiPathNextHopKey& key) {
    return getHwSwitch()
        ->getMultiPathNextHopTable()
        ->getNextHops()
        .referenceCount(key);
  }
};

} // namespace facebook::fboss

TEST_F(BcmRouteTest, AddRoutesAndCountPrintCheckToCPU) {
  auto setup = [=, this]() {
    applyNewConfig(initialConfig());
    std::vector<IPAddress> nexthops = {
        IPAddress("1.1.1.10"),
        IPAddress("2.2.2.10"),
        IPAddress("1::10"),
        IPAddress("2::10"),
    };
    std::vector<CIDRNetwork> networks = {
        CIDRNetwork(IPAddress("10.1.1.0"), 24),
        CIDRNetwork(IPAddress("20.1.1.0"), 24),
        CIDRNetwork(IPAddress("1001::0"), 48),
        CIDRNetwork(IPAddress("2001::0"), 48),
    };

    for (auto tuple : boost::combine(networks, nexthops)) {
      auto network = tuple.get<0>();
      auto nexthop = tuple.get<1>();
      addRoute(network, {nexthop});
    }
  };

  auto verify = [=, this]() {
    bcm_l3_info_t hwStatus;
    auto rv = bcm_l3_info(getUnit(), &hwStatus);
    bcmCheckError(rv, "failed get L3 hw info");
    bcm_l3_egress_traverse(getUnit(), L3EgressPrintCb, nullptr);

    uint32_t start = 0, end = hwStatus.l3info_max_route;
    int routeCount = 0; // number of routes

    int expectedRouteCount = 0;
    bool verifyL3Intf = true;
    if (getHwSwitch()->getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::HOSTTABLE)) {
      // 4 + 4 + 1 fe80::/64 link local address
      // 4 : 2 routes (v4 & v6) per interface for 2 interfaces
      // 4 : 4 routes added in the test
      // 1 : 1 route for link local addresses
      expectedRouteCount = 9 + numMinAlpmRoutes();
    } else {
      // In addition to above routes, add 4 + 4 host routes
      // 4 : 2 intf host routes (v4 & v6) per interface for 2 interfaces
      // 4 : 2 nexthop host routes (v4 & v6) per interface for 2 interfaces
      expectedRouteCount = 9 + 8 + numMinAlpmRoutes();
      // TH4 return invlid parameter when calling bcm_l3_intf_get() for
      // l3 intf 8191 that is going to cpu
      verifyL3Intf = false;
    }
    auto userData = std::make_pair(&routeCount, verifyL3Intf);
    bcm_l3_route_traverse(getUnit(), 0, start, end, L3RouteCountCb, &userData);
    bcm_l3_route_traverse(
        getUnit(), BCM_L3_IP6, start, end >> 1, L3RouteCountCb, &userData);

    EXPECT_EQ(expectedRouteCount, routeCount);

    // Check next hops (CPU)
    // So far we could not verify the src/dst MAC address for a specific route
    // entry, because the first packet will be punted to the CPU before the ARP
    // resolution.
    // All newly created egress object will point to intf ID 4096, with
    // BCM_L3_L2TOCPU bit set
    bool allToCPU = true;
    bcm_l3_route_traverse(getUnit(), 0, start, end, L3RouteToCPUCb, &allToCPU);
    bcm_l3_route_traverse(
        getUnit(), BCM_L3_IP6, start, end, L3RouteToCPUCb, &allToCPU);
    EXPECT_TRUE(allToCPU);
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmRouteHostReferenceTest, AddRouteAndCheckHostReference) {
  int intf1 = kBaseVlanId; /* config has one interface */
  std::vector<IPAddress> nexthops = {
      IPAddress("1.1.1.101"),
      IPAddress("1::101"),
  };
  auto setup = [=, this]() {
    applyNewConfig(initialConfig());
    std::vector<CIDRNetwork> networks = {
        CIDRNetwork(IPAddress("10.1.1.0"), 24),
        CIDRNetwork(IPAddress("1001:1::"), 48),
    };

    /* add routes */
    for (auto tuple : boost::combine(networks, nexthops)) {
      auto network = tuple.get<0>();
      auto nexthop = tuple.get<1>();
      addRoute(network, {nexthop});
    }
  };

  auto verify = [=, this]() {
    /* basic test, each next hop should be reachable only from one network, so
     * reference count should be one */
    for (auto nexthop : nexthops) {
      EXPECT_EQ(referenceCount(getKey(nexthop, intf1)), 1);
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(
    BcmRouteHostReferenceTest,
    AddRoutesWithDistinctNextHopsAndCheckHostReference) {
  int intf1 = kBaseVlanId; /* config has one interface */
  std::vector<IPAddress> nexthops = {
      IPAddress("1.1.1.101"),
      IPAddress("1.1.1.102"),
      IPAddress("1::101"),
      IPAddress("1::102"),
  };
  auto setup = [=, this]() {
    applyNewConfig(initialConfig());
    std::vector<CIDRNetwork> networks = {
        CIDRNetwork(IPAddress("10.1.1.0"), 24),
        CIDRNetwork(IPAddress("10.1.2.0"), 24),
        CIDRNetwork(IPAddress("1001:1::"), 48),
        CIDRNetwork(IPAddress("1001:2::"), 48),
    };
    /* add routes */
    for (auto tuple : boost::combine(networks, nexthops)) {
      auto network = tuple.get<0>();
      auto nexthop = tuple.get<1>();
      addRoute(network, {nexthop});
    }
  };
  auto verify = [=, this]() {
    /* each next hop is reached from only one route, so reference count for each
     * next hop should be 1 */
    for (auto nexthop : nexthops) {
      EXPECT_EQ(referenceCount(getKey(nexthop, intf1)), 1);
    }
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(
    BcmRouteHostReferenceTest,
    AddRoutesWithSharedNextHopsAndCheckHostReference) {
  int intf1 = kBaseVlanId; /* config has one interface */
  std::vector<IPAddress> nexthops = {
      IPAddress("1.1.1.101"),
      IPAddress("1.1.1.101"), /* two V4 networks have same nexthop */
      IPAddress("1::1"),
      IPAddress("1::1"), /* two V6 networks have same nexthop */
  };
  auto setup = [=, this]() {
    applyNewConfig(initialConfig());
    std::vector<CIDRNetwork> networks = {
        CIDRNetwork(IPAddress("10.1.1.0"), 24),
        CIDRNetwork(IPAddress("10.1.2.0"), 24),
        CIDRNetwork(IPAddress("1001:1::"), 48),
        CIDRNetwork(IPAddress("1001:2::"), 48),
    };

    /* add routes */
    for (auto tuple : boost::combine(networks, nexthops)) {
      auto network = tuple.get<0>();
      auto nexthop = tuple.get<1>();
      addRoute(network, {nexthop});
    }
  };

  auto verify = [=, this]() {
    /* Verify reference counts for each nexthop is 2 (as two networks share
     * each) */
    for (auto nexthop : nexthops) {
      EXPECT_EQ(referenceCount(getKey(nexthop, intf1)), 2);
    }
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmRouteHostReferenceTest, DeleteRoutesAndCheckHostReference) {
  int intf1 = kBaseVlanId; /* config has one interface */
  auto setup = [=, this]() {
    applyNewConfig(initialConfig());
    std::vector<IPAddress> nexthops = {
        IPAddress("1.1.1.101"),
        IPAddress("1::1"),
        IPAddress("1.1.1.101"), /* two V4 networks share same nexthop */
        IPAddress("1::1"), /* two V6 networks share same nexthop */
    };
    std::vector<CIDRNetwork> networks = {
        CIDRNetwork(IPAddress("10.1.1.0"), 24),
        CIDRNetwork(IPAddress("1001:1::"), 48),
        CIDRNetwork(IPAddress("10.1.2.0"), 24),
        CIDRNetwork(IPAddress("1001:2::"), 48),
    };

    /* add route */
    for (auto tuple : boost::combine(networks, nexthops)) {
      auto network = tuple.get<0>();
      auto nexthop = tuple.get<1>();
      addRoute(network, {nexthop});
    }
    for (auto nexthop : nexthops) {
      EXPECT_EQ(referenceCount(getKey(nexthop, intf1)), 2);
    }

    /* now remove one network per next hop */
    networks.pop_back();
    nexthops.pop_back();
    networks.pop_back();
    nexthops.pop_back();

    for (auto network : networks) {
      delRoute(network);
    }
  };

  auto verify = [=, this]() {
    std::vector<IPAddress> nexthops = {
        IPAddress("1.1.1.101"), IPAddress("1::1")};
    /* as route to one network for each next hop will be removed, verify
     * reference count for each next hop is 1*/
    for (auto nexthop : nexthops) {
      EXPECT_EQ(referenceCount(getKey(nexthop, intf1)), 1);
    }
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmRouteHostReferenceTest, AddRouteAndCheckInvalidHostReference) {
  int intf1 = kBaseVlanId; /* config has one interface */
  int intf2 = 2; /* unrelated interface */

  std::vector<IPAddress> nexthops = {
      IPAddress("1.1.1.101"),
      IPAddress("1::101"),
  };
  auto setup = [=, this]() {
    applyNewConfig(initialConfig());
    std::vector<CIDRNetwork> networks = {
        CIDRNetwork(IPAddress("10.1.1.0"), 24),
        CIDRNetwork(IPAddress("1001:1::"), 48),
    };

    /* add routes */
    for (auto tuple : boost::combine(networks, nexthops)) {
      auto network = tuple.get<0>();
      auto nexthop = tuple.get<1>();
      addRoute(network, {nexthop});
    }
  };

  auto verify = [=, this]() {
    /* nexthop is not resolved through through intf2, so reference count should
     * be zero */
    for (auto nexthop : nexthops) {
      EXPECT_EQ(referenceCount(getKey(nexthop, intf2)), 0);
    }
    /* unknown nexthops should have reference count zero */
    EXPECT_EQ(referenceCount(getKey(IPAddress("1.1.1.102"), intf1)), 0);
    EXPECT_EQ(referenceCount(getKey(IPAddress("1::102"), intf2)), 0);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmRouteHostReferenceTest, AddNoRoutesAndCheckDefaultHostReference) {
  auto setup = [=, this]() { applyNewConfig(initialConfig()); };
  auto verify = [=, this]() {
    std::vector<IPAddress> interfaceIps;
    std::vector<int32_t> interfaceIds;

    /* entries are created for non-local IP addresses of interface */
    for (const auto& [_, intfMap] :
         std::as_const(*getProgrammedState()->getInterfaces())) {
      for (auto intfIter : std::as_const(*intfMap)) {
        const auto& interface = intfIter.second;
        for (auto iter : std::as_const(*interface->getAddresses())) {
          auto address = folly::IPAddress(iter.first);
          if (address.isV6() && address.isLinkLocal()) {
            continue;
          }
          interfaceIps.push_back(address);
          interfaceIds.push_back(interface->getID());
        }
      }
    }
    /* verify reference count is one by default for interface address */
    for (auto tuple : boost::combine(interfaceIds, interfaceIps)) {
      auto intfId = tuple.get<0>();
      auto intfIp = tuple.get<1>();
      EXPECT_EQ(referenceCount(getKey(intfIp, intfId, 1)), 1);
    }
  };

  verifyAcrossWarmBoots(setup, verify);
}

void BcmRouteTest::routeReferenceCountTest(
    const CIDRNetwork& network,
    const IPAddress& nexthop) {
  auto setup = [=, this]() {
    applyNewConfig(initialConfig());
    addRoute(network, {nexthop});
  };

  auto verify = [=, this]() {
    const IPAddress& networkIP = network.first;
    const uint8_t netmask = network.second;
    EXPECT_TRUE(networkIP.isV4() || networkIP.isV6());
    int v4NonHostInterfaceRoutes, v6NonHostInterfaceRoutes;
    std::tie(v4NonHostInterfaceRoutes, v6NonHostInterfaceRoutes) =
        computeNonHostInterfaceRoutes();
    auto nonHostInterfaceRoutes =
        networkIP.isV4() ? v4NonHostInterfaceRoutes : v6NonHostInterfaceRoutes;
    auto numAlpmRoutes =
        networkIP.isV4() ? numMinAlpmV4Routes() : numMinAlpmV6Routes();
    bool isHostRouteAndCanUseHostTable = networkIP.bitCount() == netmask &&
        getPlatform()->canUseHostTableForHostRoutes();
    auto hostTable = getHwSwitch()->getHostTable();

    EXPECT_EQ(
        isHostRouteAndCanUseHostTable ? 1 : 0,
        hostTable->getReferenceCount(getHostKey(networkIP)));

    bcm_l3_info_t hwStatus;
    auto rv = bcm_l3_info(getUnit(), &hwStatus);
    bcmCheckError(rv, "failed to get L3 hw info");

    auto routeCount = getHwRouteCount(getUnit(), networkIP, hwStatus);
    auto interfaceAndLinkLocalRoutes = nonHostInterfaceRoutes +
        (networkIP.isV4() ? 0 : 1); // For v6 we add fe80 link local routes
    if (getHwSwitch()->getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::HOSTTABLE)) {
      EXPECT_EQ(
          interfaceAndLinkLocalRoutes + numAlpmRoutes +
              (isHostRouteAndCanUseHostTable ? 0 : 1),
          routeCount);

      auto hostRouteCount = getHwHostRouteCount(getUnit(), networkIP, hwStatus);
      EXPECT_EQ(
          nonHostInterfaceRoutes + 1 + // nexthop
              (isHostRouteAndCanUseHostTable ? 1 : 0),
          hostRouteCount);
    } else {
      // all host entries are programmed to route table,
      // if host table is not available
      EXPECT_EQ(
          interfaceAndLinkLocalRoutes + numAlpmRoutes + nonHostInterfaceRoutes +
              2, // nexthop and newly added route
          routeCount);
    }

    // A non-host route, host route if we can;t use host table for host routes
    // should be present in hardware route table
    EXPECT_EQ(
        isHostRouteAndCanUseHostTable ? BCM_E_NOT_FOUND : 0,
        getHwRoute(getUnit(), networkIP, netmask));
  };

  verifyAcrossWarmBoots(setup, verify);
}

void BcmRouteTest::verifyBcmHostReference(
    bcm_vrf_t vrf,
    const RouteNextHopSet& nexthops) {
  auto count = referenceCount(
      std::make_tuple(vrf, nexthops, std::nullopt)); // initial ref count

  auto* ecmpHost = getHwSwitch()->getMultiPathNextHopTable()->getNextHopIf(
      BcmMultiPathNextHopKey(vrf, nexthops, std::nullopt));
  EXPECT_NE(ecmpHost, nullptr);

  EXPECT_EQ(
      referenceCount(std::make_tuple(vrf, nexthops, std::nullopt)), count);
}

void BcmRouteTest::verifyNextHopReferences(
    const std::vector<IPAddress>& nexthops) {
  for (auto& nexthop : nexthops) {
    if (nexthop.isV6()) {
      const auto& route = findLongestMatchRoute(
          getHwSwitchEnsemble()->getRib(),
          RouterID(0),
          nexthop.asV6(),
          getProgrammedState());
      EXPECT_EQ(route->isResolved(), true);
      const auto& fwd = route->getForwardInfo();
      verifyBcmHostReference(0, fwd.normalizedNextHops());

    } else {
      const auto& route = findLongestMatchRoute(
          getHwSwitchEnsemble()->getRib(),
          RouterID(0),
          nexthop.asV4(),
          getProgrammedState());
      EXPECT_EQ(route->isResolved(), true);
      const auto& fwd = route->getForwardInfo();
      verifyBcmHostReference(0, fwd.normalizedNextHops());
    }
  }
}
TEST_F(BcmRouteTest, AddV4NonHostRoute) {
  routeReferenceCountTest(
      CIDRNetwork(IPAddress("10.1.1.0"), 24), IPAddress("1.1.1.10"));
}

TEST_F(BcmRouteTest, AddV4HostRoute) {
  routeReferenceCountTest(
      CIDRNetwork(IPAddress("20.1.1.0"), 32), IPAddress("2.2.2.10"));
}

TEST_F(BcmRouteTest, AddV6NonHostRoute) {
  routeReferenceCountTest(
      CIDRNetwork(IPAddress("1001::0"), 48), IPAddress("1::10"));
}

TEST_F(BcmRouteTest, AddV6HostRoute) {
  routeReferenceCountTest(
      CIDRNetwork(IPAddress("2001::0"), 128), IPAddress("2::10"));
}

TEST_F(BcmRouteTest, RemoveHWNonexistentNonHostRoute) {
  std::array<std::pair<CIDRNetwork, IPAddress>, 2> nonHostRoutes = {
      std::make_pair<CIDRNetwork, IPAddress>(
          CIDRNetwork(IPAddress("10.1.1.0"), 24), IPAddress("1.1.1.10")),
      std::make_pair<CIDRNetwork, IPAddress>(
          CIDRNetwork(IPAddress("1001::0"), 48), IPAddress("1::10"))};

  auto setup = [=, this]() {
    applyNewConfig(initialConfig());
    for (const auto& route : nonHostRoutes) {
      addRoute(route.first, {route.second});
    }

    // First delete routes directly from HW
    for (const auto& route : nonHostRoutes) {
      bcm_l3_route_t bcmL3Route;
      initBcmL3RouteT(bcmL3Route, route.first.first, route.first.second);
      auto rv = bcm_l3_route_delete(getUnit(), &bcmL3Route);
      bcmCheckError(rv, "failed to remove l3 route");
    }

    // Then call the regular remove routes logic
    for (const auto& route : nonHostRoutes) {
      delRoute(route.first);
    }
  };

  auto verify = [&]() {
    // Check whether route is removed from HW
    for (const auto& route : nonHostRoutes) {
      bcm_l3_route_t bcmL3Route;
      initBcmL3RouteT(bcmL3Route, route.first.first, route.first.second);
      auto rv = bcm_l3_route_get(getUnit(), &bcmL3Route);
      EXPECT_EQ(rv, BCM_E_NOT_FOUND);
    }

    // Check whether route is removed from SW
    auto [v4Count, v6Count] = getRouteCount(getProgrammedState());
    int v4NonHostInterfaceRoutes, v6NonHostInterfaceRoutes;
    std::tie(v4NonHostInterfaceRoutes, v6NonHostInterfaceRoutes) =
        computeNonHostInterfaceRoutes();
    EXPECT_EQ(v4Count, v4NonHostInterfaceRoutes + numMinAlpmV4Routes());
    // For v6 we add fe80 link local routes
    EXPECT_EQ(v6Count, v6NonHostInterfaceRoutes + numMinAlpmV6Routes() + 1);
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmRouteTest, HostRouteStat) {
  HwResourceStats preUpdateStat;
  auto setup = [&]() {
    applyNewConfig(initialConfig());
    preUpdateStat = getHwSwitch()->getStatUpdater()->getHwTableStats();
    auto network = CIDRNetwork(IPAddress("10.1.1.10"), 32);
    auto nexthop = IPAddress("1.1.1.10");
    addRoute(network, {nexthop});
  };

  auto verify = [&]() {
    HwResourceStats postUpdateStat;
    postUpdateStat = getHwSwitch()->getStatUpdater()->getHwTableStats();
    auto expectedV4HostRouteIncrement =
        getPlatform()->canUseHostTableForHostRoutes() ? 2 /*host route + nhop*/
                                                      : 1 /*nhop*/;
    if (!getHwSwitch()->getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::HOSTTABLE)) {
      expectedV4HostRouteIncrement = 0;
    }
    EXPECT_EQ(
        *postUpdateStat.l3_host_used(),
        *preUpdateStat.l3_host_used() + expectedV4HostRouteIncrement);
    EXPECT_EQ(
        *postUpdateStat.l3_ipv4_host_used(),
        *preUpdateStat.l3_ipv4_host_used() + expectedV4HostRouteIncrement);
    EXPECT_EQ(
        *postUpdateStat.l3_ipv6_host_used(),
        *preUpdateStat.l3_ipv6_host_used());
  };
  setup();
  verify();
}

TEST_F(BcmRouteTest, LpmRouteV6Stat128b) {
  HwResourceStats preUpdateStat;
  auto setup = [&]() {
    applyNewConfig(initialConfig());
    preUpdateStat = getHwSwitch()->getStatUpdater()->getHwTableStats();
    auto network = CIDRNetwork(IPAddress("1001::0"), 127);
    auto nexthop = IPAddress("1::10");
    addRoute(network, {nexthop});
  };

  auto verify = [&]() {
    HwResourceStats postUpdateStat;
    postUpdateStat = getHwSwitch()->getStatUpdater()->getHwTableStats();
    EXPECT_EQ(*postUpdateStat.lpm_ipv4_max(), *preUpdateStat.lpm_ipv4_max());
    EXPECT_EQ(
        *postUpdateStat.lpm_ipv6_mask_65_127_max(),
        *preUpdateStat.lpm_ipv6_mask_65_127_max());
    EXPECT_EQ(*postUpdateStat.lpm_ipv4_used(), *preUpdateStat.lpm_ipv4_used());
    EXPECT_EQ(
        *postUpdateStat.lpm_ipv6_mask_0_64_used(),
        *preUpdateStat.lpm_ipv6_mask_0_64_used());
    if (getHwSwitch()->getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::HOSTTABLE)) {
      EXPECT_EQ(
          *postUpdateStat.lpm_ipv6_mask_65_127_used(),
          *preUpdateStat.lpm_ipv6_mask_65_127_used() + 1);
    } else {
      // one more 1::10/128 host entry is programmed to route table
      EXPECT_EQ(
          *postUpdateStat.lpm_ipv6_mask_65_127_used(),
          *preUpdateStat.lpm_ipv6_mask_65_127_used() + 2);
    }
  };
  setup();
  verify();
}

TEST_F(BcmRouteTest, BcmHostReference) {
  std::vector<IPAddress> v4Nexthops = {
      IPAddress("1.1.1.101"),
      IPAddress("1.1.1.102"),
      IPAddress("1.1.1.103"),
      IPAddress("1.1.1.104"),
  };

  std::vector<IPAddress> v6Nexthops = {
      IPAddress("1::101"),
      IPAddress("1::102"),
      IPAddress("1::103"),
      IPAddress("1::104"),
  };

  auto v4Network = CIDRNetwork(IPAddress("10.1.2.0"), 24);
  auto v6Network = CIDRNetwork(IPAddress("1001:2::"), 48);

  auto setup = [=, this]() {
    // add multi path route
    applyNewConfig(initialConfig());
    /* add routes */
    addRoute(v4Network, {v4Nexthops});
    addRoute(v6Network, {v6Nexthops});
  };
  auto verify = [=, this]() {
    verifyNextHopReferences(v4Nexthops);
    verifyNextHopReferences(v6Nexthops);
    verifyNextHopReferences({IPAddress("10.1.2.100")});
    verifyNextHopReferences({IPAddress("1001:2::10")});
  };
  setup();
  verify();
}

TEST_F(BcmRouteTest, EgressUpdateOnHostRouteUpdateOneHopToManyHops) {
  auto hostRouteV4 = CIDRNetwork{folly::IPAddressV4("101.102.103.104"), 32};
  auto hostRouteV6 = CIDRNetwork{folly::IPAddressV6("2401::dead:beef"), 128};
  std::vector<IPAddress> v4Nexthops = {
      IPAddress("1.1.1.101"),
      IPAddress("2.2.2.101"),
  };

  std::vector<IPAddress> v6Nexthops = {
      IPAddress("1::101"),
      IPAddress("2::101"),
  };

  auto setup = [=, this]() {
    // add multi path route
    auto config = initialConfig();
    applyNewConfig(config);
    for (auto i = 0; i < 2; i++) {
      auto portId = masterLogicalPortIds()[i];
      auto helper4 = utility::EcmpSetupTargetedPorts<IPAddressV4>(
          getProgrammedState(), RouterID(0));
      applyNewState(helper4.resolveNextHops(
          getProgrammedState(), {PortDescriptor(portId)}));

      auto helper6 = utility::EcmpSetupTargetedPorts<IPAddressV6>(
          getProgrammedState(), RouterID(0));
      applyNewState(helper6.resolveNextHops(
          getProgrammedState(), {PortDescriptor(portId)}));
    }

    // host route has only one next hop
    addRoute(hostRouteV4, {v4Nexthops[0]});
    addRoute(hostRouteV6, {v6Nexthops[0]});

    // make it ECMP egress
    addRoute(hostRouteV4, v4Nexthops);
    addRoute(hostRouteV6, v6Nexthops);
  };

  auto verify = [=, this]() {
    auto v4SwRoute = findRoute<folly::IPAddressV4>(
        RouterID(0),
        {hostRouteV4.first, hostRouteV4.second},
        getProgrammedState());

    auto v6SwRoute = findRoute<folly::IPAddressV6>(
        RouterID(0),
        {hostRouteV6.first, hostRouteV6.second},
        getProgrammedState());

    ASSERT_NE(v4SwRoute, nullptr);
    ASSERT_NE(v6SwRoute, nullptr);
    ASSERT_EQ(v4SwRoute->isResolved(), true);
    ASSERT_EQ(v6SwRoute->isResolved(), true);

    auto* v4BcmRoute = getHwSwitch()->routeTable()->getBcmRoute(
        0, hostRouteV4.first, hostRouteV4.second);
    auto* v6BcmRoute = getHwSwitch()->routeTable()->getBcmRoute(
        0, hostRouteV6.first, hostRouteV6.second);

    if (getPlatform()->canUseHostTableForHostRoutes()) {
      bcm_l3_host_t v4HostRoute;
      bcm_l3_host_t v6HostRoute;

      findHwHost(0, hostRouteV4.first, v4HostRoute);
      findHwHost(0, hostRouteV6.first, v6HostRoute);

      ASSERT_EQ(v4BcmRoute->getEgressId(), v4HostRoute.l3a_intf);
      ASSERT_EQ(v6BcmRoute->getEgressId(), v6HostRoute.l3a_intf);
    } else {
      bcm_l3_route_t v4HostRoute;
      bcm_l3_route_t v6HostRoute;

      getHwRoute(0, hostRouteV4.first, hostRouteV4.second, v4HostRoute);
      getHwRoute(0, hostRouteV6.first, hostRouteV6.second, v6HostRoute);

      ASSERT_EQ(v4BcmRoute->getEgressId(), v4HostRoute.l3a_intf);
      ASSERT_EQ(v6BcmRoute->getEgressId(), v6HostRoute.l3a_intf);
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmTest, VerifyDropEgress) {
  auto setup = [] {
    // Default drop egress should be created during BcmUnit initialization.
    // No need to apply an extra config
  };

  auto verify = [this] {
    bcm_l3_egress_t expectedEgress;
    bcm_l3_egress_t_init(&expectedEgress);

    auto dropEgressID = getHwSwitch()->getDropEgressId();
    auto rv = bcm_l3_egress_get(getUnit(), dropEgressID, &expectedEgress);
    bcmCheckError(rv, "failed to get drop egress ", dropEgressID);

    EXPECT_TRUE(expectedEgress.flags & BCM_L3_DST_DISCARD);
  };
  verifyAcrossWarmBoots(setup, verify);
}

class BcmRouteNeighborTest : public BcmRouteTest {};

TEST_F(BcmRouteNeighborTest, UnresolveResolveNextHop) {
  auto config = this->initialConfig();
  boost::container::flat_set<PortDescriptor> ports;
  for (auto i = 0; i < 2; i++) {
    auto portId = this->masterLogicalPortIds()[i];
    ports.insert(PortDescriptor(portId));
  }
  auto route = RoutePrefixV6{folly::IPAddressV6("2401:dead:beef::"), 112};

  auto setup = [=, this]() {
    // add multi path route
    this->applyNewConfig(config);
    auto helper = utility::EcmpSetupTargetedPorts<IPAddressV6>(
        this->getProgrammedState(), RouterID(0));

    this->applyNewState(
        helper.resolveNextHops(this->getProgrammedState(), ports));

    std::map<PortDescriptor, std::shared_ptr<NdpEntry>> entries;

    // mark neighbors connected over ports pending
    auto state0 = this->getProgrammedState();
    for (auto port : ports) {
      auto ecmpNextHop = helper.nhop(port);

      NdpTable* ntable;
      EXPECT_EQ(port.type(), PortDescriptor::PortType::PHYSICAL);
      auto interfaceID =
          this->getProgrammedState()->getInterfaceIDForPort(port);
      ntable = state0->getInterfaces()
                   ->getNode(interfaceID)
                   ->getNdpTable()
                   ->modify(interfaceID, &state0);

      auto entry = ntable->getEntry(ecmpNextHop.ip);
      auto intfId = entry->getIntfID();
      ntable->removeEntry(ecmpNextHop.ip);
      ntable->addPendingEntry(ecmpNextHop.ip, intfId);
      entries[port] = std::move(entry);
    }
    this->applyNewState(state0);

    // mark neighbors connected over ports reachable
    auto state1 = this->getProgrammedState();
    for (auto port : ports) {
      NdpTable* ntable;
      EXPECT_EQ(port.type(), PortDescriptor::PortType::PHYSICAL);
      auto interfaceID =
          this->getProgrammedState()->getInterfaceIDForPort(port);
      ntable = state1->getInterfaces()
                   ->getNode(interfaceID)
                   ->getNdpTable()
                   ->modify(interfaceID, &state1);

      auto entry = entries[port];
      ntable->updateEntry(
          NeighborEntryFields<folly::IPAddressV6>::fromThrift(
              entry->toThrift()));
    }
    this->applyNewState(state1);
    helper.programRoutes(this->getRouteUpdater(), ports, {route});
  };
  auto verify = [=, this]() {
    /* route is programmed */
    auto* bcmRoute = this->getHwSwitch()->routeTable()->getBcmRoute(
        0, route.network(), route.mask());
    EXPECT_NE(bcmRoute, nullptr);
    auto egressId = bcmRoute->getEgressId();
    EXPECT_NE(egressId, BcmEgressBase::INVALID);

    /* ecmp is resolved */
    EXPECT_EQ(getEcmpSizeInHw(this->getHwSwitch(), egressId, 2), 2);
  };
  setup();
  verify();
}
