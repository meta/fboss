// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/hw/test/HwTestThriftHandler.h"

#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/sai/api/RouteApi.h"
#include "fboss/agent/hw/sai/api/SaiApiTable.h"
#include "fboss/agent/hw/sai/switch/SaiCounterManager.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiRouterInterfaceManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/sai/switch/SaiVirtualRouterManager.h"
#include "fboss/agent/types.h"

using facebook::fboss::HwSwitch;
using facebook::fboss::RouterID;
using facebook::fboss::SaiApiTable;
using facebook::fboss::SaiNextHopTraitsT;
using facebook::fboss::SaiRouteTraits;
using facebook::fboss::SaiSwitch;
using facebook::fboss::SaiSwitchTraits;
using facebook::fboss::cfg::AclLookupClass;

namespace {
bool isEgressToIp(folly::IPAddress addr, sai_object_id_t adapterKey) {
  try {
    auto ipAttr = SaiApiTable::getInstance()->nextHopApi().getAttribute(
        static_cast<facebook::fboss::NextHopSaiId>(adapterKey),
        SaiNextHopTraitsT<SAI_NEXT_HOP_TYPE_IP>::Attributes::Ip());
    return ipAttr == addr;
  } catch (const facebook::fboss::SaiApiError&) {
    return false;
  }
}

template <typename AddrT>
sai_object_id_t getNextHopId(
    const HwSwitch* hwSwitch,
    typename facebook::fboss::Route<AddrT>::Prefix prefix) {
  auto saiSwitch = static_cast<const facebook::fboss::SaiSwitch*>(hwSwitch);
  // Query the nexthop ID given the route prefix
  folly::IPAddress prefixNetwork{prefix.network()};
  folly::CIDRNetwork follyPrefix{prefixNetwork, prefix.mask()};
  auto virtualRouterHandle =
      saiSwitch->managerTable()->virtualRouterManager().getVirtualRouterHandle(
          RouterID(0));
  CHECK(virtualRouterHandle);
  auto routeEntry = SaiRouteTraits::RouteEntry(
      saiSwitch->getSaiSwitchId(),
      virtualRouterHandle->virtualRouter->adapterKey(),
      follyPrefix);
  return SaiApiTable::getInstance()->routeApi().getAttribute(
      routeEntry, SaiRouteTraits::Attributes::NextHopId());
}

template <typename AddrT>
facebook::fboss::NextHopSaiId getNextHopSaiId(
    const facebook::fboss::HwSwitch* hwSwitch,
    typename facebook::fboss::Route<AddrT>::Prefix prefix) {
  return static_cast<facebook::fboss::NextHopSaiId>(
      getNextHopId<AddrT>(hwSwitch, prefix));
}

facebook::fboss::NextHopSaiId getNextHopSaiIdForMember(
    facebook::fboss::NextHopGroupMemberSaiId member) {
  return static_cast<facebook::fboss::NextHopSaiId>(
      SaiApiTable::getInstance()->nextHopGroupApi().getAttribute(
          member,
          facebook::fboss::SaiNextHopGroupMemberTraits::Attributes::
              NextHopId{}));
}

std::vector<facebook::fboss::NextHopSaiId> getNextHopMembers(
    facebook::fboss::NextHopGroupSaiId group) {
  auto members = SaiApiTable::getInstance()->nextHopGroupApi().getAttribute(
      group,
      facebook::fboss::SaiNextHopGroupTraits::Attributes::NextHopMemberList{});
  std::vector<facebook::fboss::NextHopSaiId> nexthops{};
  for (auto member : members) {
    auto nexthop = getNextHopSaiIdForMember(
        static_cast<facebook::fboss::NextHopGroupMemberSaiId>(member));
    nexthops.push_back(nexthop);
  }
  return nexthops;
}

std::vector<facebook::fboss::NextHopSaiId> getNextHops(sai_object_id_t id) {
  auto type = sai_object_type_query(id);
  if (type == SAI_OBJECT_TYPE_NEXT_HOP) {
    return {static_cast<facebook::fboss::NextHopSaiId>(id)};
  }
  EXPECT_EQ(type, SAI_OBJECT_TYPE_NEXT_HOP_GROUP);
  return getNextHopMembers(static_cast<facebook::fboss::NextHopGroupSaiId>(id));
}

SaiRouteTraits::RouteEntry getSaiRouteAdapterKey(
    const SaiSwitch* saiSwitch,
    RouterID rid,
    const folly::CIDRNetwork& prefix) {
  auto virtualRouterHandle =
      saiSwitch->managerTable()->virtualRouterManager().getVirtualRouterHandle(
          rid);
  if (!virtualRouterHandle) {
    throw facebook::fboss::FbossError("No virtual router with id 0");
  }

  return SaiRouteTraits::RouteEntry(
      saiSwitch->getSaiSwitchId(),
      virtualRouterHandle->virtualRouter->adapterKey(),
      prefix);
}

std::optional<int> getHwRouteClassID(
    const HwSwitch* hw,
    RouterID rid,
    const folly::CIDRNetwork& prefix) {
  const auto saiSwitch = static_cast<const SaiSwitch*>(hw);

  try {
    auto r = getSaiRouteAdapterKey(saiSwitch, rid, prefix);
    auto metadata = SaiApiTable::getInstance()->routeApi().getAttribute(
        r, SaiRouteTraits::Attributes::Metadata());
    return metadata == 0 ? std::nullopt : std::optional(metadata);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

bool isHwRouteToCpu(
    const HwSwitch* hwSwitch,
    RouterID rid,
    const folly::CIDRNetwork& cidrNetwork) {
  const auto saiSwitch = static_cast<const SaiSwitch*>(hwSwitch);

  try {
    auto routeAdapterKey = getSaiRouteAdapterKey(saiSwitch, rid, cidrNetwork);
    sai_object_id_t nhop = SaiApiTable::getInstance()->routeApi().getAttribute(
        routeAdapterKey, SaiRouteTraits::Attributes::NextHopId());

    facebook::fboss::SwitchSaiId switchId =
        saiSwitch->managerTable()->switchManager().getSwitchSaiId();
    sai_object_id_t cpuPortId{
        SaiApiTable::getInstance()->switchApi().getAttribute(
            switchId, SaiSwitchTraits::Attributes::CpuPort{})};

    return nhop == cpuPortId;
  } catch (const facebook::fboss::SaiApiError&) {
    return false;
  }
}

bool isHwRouteHit(
    [[maybe_unused]] const HwSwitch* hwSwitch,
    [[maybe_unused]] RouterID /*rid*/,
    [[maybe_unused]] const folly::CIDRNetwork& cidrNetwork) {
  throw facebook::fboss::FbossError("L3 entry hitbit is unsupported for SAI");
}

void clearHwRouteHit(
    [[maybe_unused]] const HwSwitch* hwSwitch,
    [[maybe_unused]] RouterID /*rid*/,
    [[maybe_unused]] const folly::CIDRNetwork& cidrNetwork) {
  throw facebook::fboss::FbossError("L3 entry hitbit is unsupported for SAI");
}

bool isHwRouteMultiPath(
    const HwSwitch* hwSwitch,
    RouterID rid,
    const folly::CIDRNetwork& cidrNetwork) {
  const auto saiSwitch = static_cast<const SaiSwitch*>(hwSwitch);

  try {
    auto routeAdapterKey = getSaiRouteAdapterKey(saiSwitch, rid, cidrNetwork);
    sai_object_id_t nhop = SaiApiTable::getInstance()->routeApi().getAttribute(
        routeAdapterKey, SaiRouteTraits::Attributes::NextHopId());

    SaiApiTable::getInstance()->nextHopGroupApi().getAttribute(
        static_cast<facebook::fboss::NextHopGroupSaiId>(nhop),
        facebook::fboss::SaiNextHopGroupTraits::Attributes::
            NextHopMemberList{});
  } catch (const facebook::fboss::SaiApiError&) {
    // its not next hop group
    return false;
  }
  return true;
}

bool isHwRouteToNextHop(
    const HwSwitch* hwSwitch,
    RouterID rid,
    const folly::CIDRNetwork& cidrNetwork,
    folly::IPAddress ip,
    std::optional<uint64_t> weight) {
  const auto saiSwitch = static_cast<const SaiSwitch*>(hwSwitch);
  auto classId = getHwRouteClassID(hwSwitch, rid, cidrNetwork);
  if (classId.has_value() &&
      (AclLookupClass(classId.value()) ==
           AclLookupClass::DST_CLASS_L3_LOCAL_1 ||
       AclLookupClass(classId.value()) ==
           AclLookupClass::DST_CLASS_L3_LOCAL_2)) {
    XLOG(DBG2) << "resolved route has class ID 1 or 2";
    return false;
  }

  sai_object_id_t nhop;
  auto routeAdapterKey = getSaiRouteAdapterKey(saiSwitch, rid, cidrNetwork);
  try {
    nhop = SaiApiTable::getInstance()->routeApi().getAttribute(
        routeAdapterKey, SaiRouteTraits::Attributes::NextHopId());
  } catch (const facebook::fboss::SaiApiError&) {
    return false;
  }

  try {
    auto members = SaiApiTable::getInstance()->nextHopGroupApi().getAttribute(
        static_cast<facebook::fboss::NextHopGroupSaiId>(nhop),
        facebook::fboss::SaiNextHopGroupTraits::Attributes::
            NextHopMemberList{});
    for (auto member : members) {
      sai_object_id_t nextHopId =
          SaiApiTable::getInstance()->nextHopGroupApi().getAttribute(
              static_cast<facebook::fboss::NextHopGroupMemberSaiId>(member),
              facebook::fboss::SaiNextHopGroupMemberTraits::Attributes::
                  NextHopId{});
      if (!isEgressToIp(ip, nextHopId)) {
        continue;
      }
      if (!weight) {
        return true;
      }
      return weight.value() ==
          SaiApiTable::getInstance()->nextHopGroupApi().getAttribute(
              static_cast<facebook::fboss::NextHopGroupMemberSaiId>(member),
              facebook::fboss::SaiNextHopGroupMemberTraits::Attributes::
                  Weight{});
    }
  } catch (const facebook::fboss::SaiApiError&) {
    return isEgressToIp(ip, nhop);
  }
  return false;
}

bool isRoutePresent(
    const HwSwitch* hwSwitch,
    RouterID rid,
    const folly::CIDRNetwork& cidrNetwork) {
  const auto saiSwitch = static_cast<const SaiSwitch*>(hwSwitch);

  auto routeAdapterKey = getSaiRouteAdapterKey(saiSwitch, rid, cidrNetwork);
  try {
    SaiApiTable::getInstance()->routeApi().getAttribute(
        routeAdapterKey, SaiRouteTraits::Attributes::NextHopId());
  } catch (const facebook::fboss::SaiApiError&) {
    return false;
  }

  return true;
}

bool isRouteSetToDrop(
    const HwSwitch* hwSwitch,
    RouterID rid,
    const folly::CIDRNetwork& cidrNetwork) {
  const auto saiSwitch = static_cast<const SaiSwitch*>(hwSwitch);

  auto routeAdapterKey = getSaiRouteAdapterKey(saiSwitch, rid, cidrNetwork);
  auto packetAction = SaiApiTable::getInstance()->routeApi().getAttribute(
      routeAdapterKey, SaiRouteTraits::Attributes::PacketAction());

  return packetAction == SAI_PACKET_ACTION_DROP;
}

bool isRouteUnresolvedToCpuClassId(
    const HwSwitch* hwSwitch,
    RouterID rid,
    const folly::CIDRNetwork& cidrNetwork) {
  auto classId = getHwRouteClassID(hwSwitch, rid, cidrNetwork);
  return classId.has_value() &&
      facebook::fboss::cfg::AclLookupClass(classId.value()) ==
      facebook::fboss::cfg::AclLookupClass::DST_CLASS_L3_LOCAL_2;
}

// Verifies that the stack is programmed correctly.
// Reference count has no meaning in SAI and we assume that there always is a
// labelStack programmed for nexthop when this function is called.
template <typename AddrT>
bool verifyProgrammedStack(
    const HwSwitch* hwSwitch,
    typename facebook::fboss::Route<AddrT>::Prefix prefix,
    const facebook::fboss::InterfaceID& intfID,
    const facebook::fboss::LabelForwardingAction::LabelStack& stack,
    long /* unused */) {
  auto* saiSwitch = static_cast<const SaiSwitch*>(hwSwitch);
  auto& nextHopApi = SaiApiTable::getInstance()->nextHopApi();
  sai_object_id_t nexthopId =
      static_cast<sai_object_id_t>(getNextHopSaiId<AddrT>(hwSwitch, prefix));
  auto nexthopIds = getNextHops(nexthopId);
  auto intfHandle = saiSwitch->managerTable()
                        ->routerInterfaceManager()
                        .getRouterInterfaceHandle(intfID);
  auto intfKey = intfHandle->adapterKey();
  bool found = false;
  bool verified = true;
  for (auto nextHopId : nexthopIds) {
    auto intfObjId = nextHopApi.getAttribute(
        nextHopId,
        facebook::fboss::SaiMplsNextHopTraits::Attributes::RouterInterfaceId());
    if (intfObjId != intfKey) {
      continue;
    }
    found = true;
    // If stack is empty, simply check if the labelStack is programmed
    // If the stack is empty, check if the labelstack has a label with MPLS
    // Else, check that the stacks match
    if (stack.empty()) {
      if (nextHopApi.getAttribute(
              nextHopId,
              facebook::fboss::SaiMplsNextHopTraits::Attributes::Type()) !=
          SAI_NEXT_HOP_TYPE_IP) {
        XLOG(DBG2) << "nexthop is not of type IP";
        verified = false;
      }
    } else {
      if (nextHopApi.getAttribute(
              nextHopId,
              facebook::fboss::SaiMplsNextHopTraits::Attributes::Type()) !=
          SAI_NEXT_HOP_TYPE_MPLS) {
        XLOG(DBG2) << "nexthop is not of type MPLS";
        verified = false;
      }
      auto labelStack = nextHopApi.getAttribute(
          nextHopId,
          facebook::fboss::SaiMplsNextHopTraits::Attributes::LabelStack{});
      if (labelStack.size() != stack.size()) {
        XLOG(DBG2) << "label stack size mismatch " << labelStack.size() << " "
                   << stack.size();
        verified = false;
      }
      for (int i = 0; i < labelStack.size(); i++) {
        if (labelStack[i] != stack[i]) {
          XLOG(DBG2) << "label stack mismatch " << labelStack[i] << " "
                     << stack[i];
          verified = false;
        }
      }
    }
    break;
  }
  XLOG(DBG2) << "found " << found << " verified " << verified;
  return found && verified;
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
  routeInfo.exists() =
      isRoutePresent(hwSwitch_, RouterID(routerID), routePrefix);
  if (*routeInfo.exists()) {
    if (!isRouteSetToDrop(hwSwitch_, RouterID(routerID), routePrefix)) {
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
    } else {
      routeInfo.isProgrammedToDrop() = true;
    }
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
    std::unique_ptr<IpPrefix> prefix,
    std::unique_ptr<MplsLabelStack> labelStack,
    int refCount) {
  auto routePrefix = folly::CIDRNetwork(
      network::toIPAddress(*prefix->ip()), *prefix->prefixLength());
  if (routePrefix.first.isV4()) {
    auto pfx = RoutePrefix<folly::IPAddressV4>{
        network::toIPAddress(*prefix->ip()).asV4(),
        static_cast<uint8_t>(*prefix->prefixLength())};
    return verifyProgrammedStack<folly::IPAddressV4>(
        hwSwitch_, pfx, InterfaceID(intfID), *labelStack, refCount);
  } else {
    auto pfx = RoutePrefix<folly::IPAddressV6>{
        network::toIPAddress(*prefix->ip()).asV6(),
        static_cast<uint8_t>(*prefix->prefixLength())};
    return verifyProgrammedStack<folly::IPAddressV6>(
        hwSwitch_, pfx, InterfaceID(intfID), *labelStack, refCount);
  }
  return true;
}

} // namespace utility
} // namespace fboss
} // namespace facebook
