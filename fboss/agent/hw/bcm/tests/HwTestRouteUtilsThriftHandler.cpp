// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/hw/test/HwTestThriftHandler.h"

#include "fboss/agent/HwSwitch.h"
#include "fboss/agent/hw/bcm/BcmAddressFBConvertors.h"
#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmHost.h"
#include "fboss/agent/hw/bcm/BcmIntf.h"
#include "fboss/agent/types.h"

using facebook::fboss::bcmCheckError;
using facebook::fboss::BcmError;
using facebook::fboss::BcmSwitch;
using facebook::fboss::HwAsic;
using facebook::fboss::HwSwitch;
using facebook::fboss::InterfaceID;
using facebook::fboss::ipToBcmIp6;

namespace {
void initBcmRoute(
    bcm_l3_route_t& route,
    const folly::CIDRNetwork& cidrNetwork) {
  bcm_l3_route_t_init(&route);

  const auto& [networkIP, netmask] = cidrNetwork;
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

bcm_l3_route_t
getBcmRoute(int unit, const folly::CIDRNetwork& cidrNetwork, uint32_t flags) {
  bcm_l3_route_t route;
  initBcmRoute(route, cidrNetwork);
  route.l3a_flags |= flags;
  CHECK_EQ(bcm_l3_route_get(unit, &route), 0);
  return route;
}

bool isRoutePresent(
    int unit,
    facebook::fboss::RouterID rid,
    const folly::CIDRNetwork& cidrNetwork) {
  bcm_l3_route_t route;
  initBcmRoute(route, cidrNetwork);
  route.l3a_vrf = rid;
  return (0 == bcm_l3_route_get(unit, &route));
}

std::optional<int> getHwRouteClassID(
    const HwSwitch* hwSwitch,
    facebook::fboss::RouterID /*rid*/,
    const folly::CIDRNetwork& cidrNetwork) {
  auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch);

  bcm_l3_route_t route = getBcmRoute(bcmSwitch->getUnit(), cidrNetwork, 0);

  return route.l3a_lookup_class == 0 ? std::nullopt
                                     : std::optional(route.l3a_lookup_class);
}

bool isHwRouteToCpu(
    const HwSwitch* hwSwitch,
    facebook::fboss::RouterID /*rid*/,
    const folly::CIDRNetwork& cidrNetwork) {
  auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch);

  bcm_l3_route_t route = getBcmRoute(bcmSwitch->getUnit(), cidrNetwork, 0);

  if (route.l3a_flags & BCM_L3_MULTIPATH) {
    return false;
  }
  bcm_l3_egress_t egress;
  bcm_l3_egress_t_init(&egress);
  CHECK_EQ(bcm_l3_egress_get(bcmSwitch->getUnit(), route.l3a_intf, &egress), 0);

  return (egress.flags & BCM_L3_L2TOCPU) && (egress.flags | BCM_L3_COPY_TO_CPU);
}

bool isHwRouteMultiPath(
    const HwSwitch* hwSwitch,
    facebook::fboss::RouterID /*rid*/,
    const folly::CIDRNetwork& cidrNetwork) {
  auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch);

  bcm_l3_route_t route = getBcmRoute(bcmSwitch->getUnit(), cidrNetwork, 0);

  return route.l3a_flags & BCM_L3_MULTIPATH;
}

bool isRouteUnresolvedToCpuClassId(
    const HwSwitch* hwSwitch,
    facebook::fboss::RouterID rid,
    const folly::CIDRNetwork& cidrNetwork) {
  auto classId = getHwRouteClassID(hwSwitch, rid, cidrNetwork);
  return classId.has_value() &&
      facebook::fboss::cfg::AclLookupClass(classId.value()) ==
      facebook::fboss::cfg::AclLookupClass::DST_CLASS_L3_LOCAL_2;
}

bool isHwRouteHit(
    const HwSwitch* hwSwitch,
    facebook::fboss::RouterID /*rid*/,
    const folly::CIDRNetwork& cidrNetwork) {
  auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch);
  bcm_l3_route_t route = getBcmRoute(bcmSwitch->getUnit(), cidrNetwork, 0);
  return (route.l3a_flags & BCM_L3_HIT);
}

void clearHwRouteHit(
    const HwSwitch* hwSwitch,
    facebook::fboss::RouterID /*rid*/,
    const folly::CIDRNetwork& cidrNetwork) {
  auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch);
  getBcmRoute(bcmSwitch->getUnit(), cidrNetwork, BCM_L3_HIT_CLEAR);
}

bool isEgressToIp(
    const BcmSwitch* bcmSwitch,
    facebook::fboss::RouterID rid,
    folly::IPAddress addr,
    bcm_if_t egress) {
  if (!bcmSwitch->getPlatform()->getAsic()->isSupported(
          HwAsic::Feature::HOSTTABLE)) {
    bcm_l3_route_t route = getBcmRoute(
        bcmSwitch->getUnit(), folly::CIDRNetwork(addr, addr.bitCount()), 0);
    return egress == route.l3a_intf;
  }
  bcm_l3_host_t host;
  bcm_l3_host_t_init(&host);
  if (addr.isV4()) {
    host.l3a_ip_addr = addr.asV4().toLongHBO();
  } else {
    memcpy(
        &host.l3a_ip6_addr,
        addr.asV6().toByteArray().data(),
        sizeof(host.l3a_ip6_addr));
    host.l3a_flags |= BCM_L3_IP6;
  }
  host.l3a_vrf = rid;
  CHECK_EQ(bcm_l3_host_find(bcmSwitch->getUnit(), &host), 0);
  return egress == host.l3a_intf;
}

bool isHwRouteToNextHop(
    const HwSwitch* hwSwitch,
    facebook::fboss::RouterID rid,
    const folly::CIDRNetwork& cidrNetwork,
    folly::IPAddress ip,
    std::optional<uint64_t> weight) {
  auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch);

  bcm_l3_route_t route = getBcmRoute(bcmSwitch->getUnit(), cidrNetwork, 0);

  if (route.l3a_flags & BCM_L3_MULTIPATH) {
    // check for member to ip, interface
    bcm_l3_egress_ecmp_t ecmp;
    bcm_l3_egress_ecmp_t_init(&ecmp);
    ecmp.ecmp_intf = route.l3a_intf;
    ecmp.flags = BCM_L3_WITH_ID;
    int count = 0;
    CHECK_EQ(
        bcm_l3_ecmp_get(bcmSwitch->getUnit(), &ecmp, 0, nullptr, &count), 0);
    std::vector<bcm_l3_ecmp_member_t> members;
    members.resize(count);
    CHECK_EQ(
        bcm_l3_ecmp_get(
            bcmSwitch->getUnit(),
            &ecmp,
            members.size(),
            members.data(),
            &count),
        0);
    bool found = false;
    bcm_l3_ecmp_member_t foundMember;
    bcm_l3_ecmp_member_t_init(&foundMember);
    for (auto member : members) {
      if (!isEgressToIp(bcmSwitch, rid, ip, member.egress_if)) {
        continue;
      }
      found = true;
      foundMember = member;
      break;
    }

    if (!weight) {
      return found;
    }

    return weight.value() ==
        std::count_if(
               members.begin(), members.end(), [foundMember](auto member) {
                 return member.egress_if == foundMember.egress_if;
               });
  }
  // check for next hop
  return isEgressToIp(bcmSwitch, rid, ip, route.l3a_intf);
}

} // namespace

namespace facebook {
namespace fboss {
namespace utility {

void HwTestThriftHandler::getRouteInfo(
    RouteInfo& routeInfo,
    std::unique_ptr<IpPrefix> prefix,
    int32_t routerID) {
  auto routePrefix = folly::CIDRNetwork(
      network::toIPAddress(*prefix->ip()), *prefix->prefixLength());
  routeInfo.exists() = isRoutePresent(0, RouterID(routerID), routePrefix);
  if (*routeInfo.exists()) {
    auto classID =
        getHwRouteClassID(hwSwitch_, RouterID(routerID), routePrefix);
    if (classID.has_value()) {
      routeInfo.classId() = classID.value();
    }
    routeInfo.isProgrammedToCpu() =
        isHwRouteToCpu(hwSwitch_, RouterID(routerID), routePrefix);
    routeInfo.isMultiPath() =
        isHwRouteMultiPath(hwSwitch_, RouterID(routerID), routePrefix);
    routeInfo.isRouteUnresolvedToClassId() = isRouteUnresolvedToCpuClassId(
        hwSwitch_, RouterID(routerID), routePrefix);
  }
}

bool HwTestThriftHandler::isRouteHit(std::unique_ptr<IpPrefix> prefix) {
  auto routePrefix = folly::CIDRNetwork(
      network::toIPAddress(*prefix->ip()), *prefix->prefixLength());
  return isHwRouteHit(hwSwitch_, RouterID(0), routePrefix);
}

void HwTestThriftHandler::clearRouteHit(std::unique_ptr<IpPrefix> prefix) {
  auto routePrefix = folly::CIDRNetwork(
      network::toIPAddress(*prefix->ip()), *prefix->prefixLength());
  clearHwRouteHit(hwSwitch_, RouterID(0), routePrefix);
}

bool HwTestThriftHandler::isRouteToNexthop(
    std::unique_ptr<IpPrefix> prefix,
    std::unique_ptr<network::thrift::BinaryAddress> nexthop,
    int32_t routerID) {
  auto routePrefix = folly::CIDRNetwork(
      network::toIPAddress(*prefix->ip()), *prefix->prefixLength());
  return isHwRouteToNextHop(
      hwSwitch_,
      RouterID(routerID),
      routePrefix,
      network::toIPAddress(*nexthop),
      std::nullopt);
}

bool HwTestThriftHandler::isProgrammedInHw(
    int intfID,
    std::unique_ptr<IpPrefix> /*prefix*/,
    std::unique_ptr<MplsLabelStack> labelStack,
    int refCount) {
  if (labelStack->empty()) {
    return static_cast<const facebook::fboss::BcmSwitch*>(hwSwitch_)
               ->getIntfTable()
               ->getBcmIntfIf(InterfaceID(intfID))
               ->getLabeledTunnelRefCount(*labelStack) == refCount;
  } else {
    return static_cast<const facebook::fboss::BcmSwitch*>(hwSwitch_)
               ->getIntfTable()
               ->getBcmIntfIf(InterfaceID(intfID))
               ->getLabeledTunnelRefCount(
                   LabelForwardingAction::LabelStack{
                       labelStack->begin() + 1, labelStack->end()}) == refCount;
  }
}

} // namespace utility
} // namespace fboss
} // namespace facebook
