/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiRouteManager.h"
#include "fboss/agent/Utils.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiCounterManager.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiNextHopGroupManager.h"
#include "fboss/agent/hw/sai/switch/SaiNextHopManager.h"
#include "fboss/agent/hw/sai/switch/SaiRouterInterfaceManager.h"
#include "fboss/agent/hw/sai/switch/SaiSrv6SidListManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/sai/switch/SaiVirtualRouterManager.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"

#include "fboss/agent/SwitchInfoUtils.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"

#include <algorithm>
#include <optional>

DEFINE_bool(
    disable_valid_route_check,
    false,
    "Disable valid route check when creating or changing routes in SAI switches");

namespace facebook::fboss {

sai_object_id_t SaiRouteHandle::nextHopAdapterKey() const {
  return std::visit(
      [](auto& handle) { return handle->adapterKey(); }, nexthopHandle_);
}

std::shared_ptr<SaiNextHopGroupHandle> SaiRouteHandle::nextHopGroupHandle()
    const {
  auto* nextHopGroupHandle =
      std::get_if<std::shared_ptr<SaiNextHopGroupHandle>>(&nexthopHandle_);
  if (!nextHopGroupHandle) {
    return nullptr;
  }
  return *nextHopGroupHandle;
}

SaiRouteManager::SaiRouteManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    const SaiPlatform* platform)
    : saiStore_(saiStore), managerTable_(managerTable), platform_(platform) {}

template <typename AddrT>
SaiRouteTraits::RouteEntry SaiRouteManager::routeEntryFromSwRoute(
    RouterID routerId,
    const std::shared_ptr<Route<AddrT>>& swRoute) const {
  SwitchSaiId switchId = managerTable_->switchManager().getSwitchSaiId();
  folly::IPAddress prefixNetwork{swRoute->prefix().network()};
  folly::CIDRNetwork prefix{prefixNetwork, swRoute->prefix().mask()};
  SaiVirtualRouterHandle* virtualRouterHandle =
      managerTable_->virtualRouterManager().getVirtualRouterHandle(routerId);
  if (!virtualRouterHandle) {
    throw FbossError("No virtual router with id ", routerId);
  }
  return SaiRouteTraits::RouteEntry{
      switchId, virtualRouterHandle->virtualRouter->adapterKey(), prefix};
}

std::vector<std::shared_ptr<SaiRoute>> SaiRouteManager::makeInterfaceToMeRoutes(
    const std::shared_ptr<Interface>& swInterface) {
  std::vector<std::shared_ptr<SaiRoute>> toMeRoutes;
  // Compute information common to all addresses in the interface:
  // vr id
  RouterID routerId = swInterface->getRouterID();
  SaiVirtualRouterHandle* virtualRouterHandle =
      managerTable_->virtualRouterManager().getVirtualRouterHandle(routerId);
  if (!virtualRouterHandle) {
    throw FbossError("No virtual router with id ", routerId);
  }
  VirtualRouterSaiId virtualRouterId{
      virtualRouterHandle->virtualRouter->adapterKey()};
  ;
  // packet action
  sai_packet_action_t packetAction = SAI_PACKET_ACTION_FORWARD;
  SwitchSaiId switchId = managerTable_->switchManager().getSwitchSaiId();
  PortSaiId cpuPortId = managerTable_->switchManager().getCpuPort();

  toMeRoutes.reserve(swInterface->getAddresses()->size());
  // Compute per-address information
  std::optional<SaiRouteTraits::Attributes::Metadata> metadata;
  if (FLAGS_set_classid_for_my_subnet_and_ip_routes &&
      platform_->getAsic()->isSupported(HwAsic::Feature::ROUTE_METADATA)) {
    metadata = static_cast<uint32_t>(cfg::AclLookupClass::DST_CLASS_L3_LOCAL_1);
  }
  for (auto iter : std::as_const(*swInterface->getAddresses())) {
    folly::IPAddress ipAddress(iter.first);
    // empty next hop group -- this route will not manage the
    // lifetime of a next hop group
    std::shared_ptr<SaiNextHopGroupHandle> nextHopGroup;
    // destination
    folly::CIDRNetwork destination{ipAddress, ipAddress.bitCount()};
    SaiRouteTraits::RouteEntry entry{switchId, virtualRouterId, destination};
    XLOG(DBG2) << "set my interface ip route " << ipAddress.str()
               << " class id to "
               << (metadata.has_value()
                       ? std::to_string(metadata.value().value())
                       : "none");
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
    SaiRouteTraits::CreateAttributes attributes{
        packetAction, cpuPortId, metadata, std::nullopt};
#else
    SaiRouteTraits::CreateAttributes attributes{
        packetAction, cpuPortId, metadata};
#endif
    auto& store = saiStore_->get<SaiRouteTraits>();
    auto route = store.setObject(entry, attributes);
    toMeRoutes.emplace_back(route);
  }
  return toMeRoutes;
}

template <typename AddrT>
bool SaiRouteManager::validRoute(const std::shared_ptr<Route<AddrT>>& swRoute) {
  /*
   * For each subnet on an L3 Interface configured on the switch, FBOSS will
   * generate two routes: a subnet route to the prefix of the subnet,
   * necessary for routing packets to hosts in the subnet and a ToMe route
   * for the IP address of the switch itself in the subnet.
   *
   * If an interface is configured with a /32 or /128 subnet, then SwSwitch
   * will still produce a subnet route (important for recursive resolution
   * in the RIB) but there is no meaningful subnet route from the SAI
   * perspective: the only IP in the subnet is the switch itself.
   *
   * Packets that would hit this route should be punted via the ToMe route
   * programmed alongside the router interface (via a makeInterfaceToMeRoutes
   * call in RouterInterfaceManager). If we attempt to program the
   * subnet route, it will have the same destination and we will overwrite the
   * correct route. For that reason, such routes are not valid in
   * SaiRouteManager, and must be skipped.
   */
  // N.B., for now, this code looks a bit silly (could just do direct return)
  // but we use this style to suggest the possibility of future extension
  // with other conditions for invalid routes.
  if (swRoute->isConnected() && swRoute->isHostRoute()) {
    return false;
  }
  return true;
}

template <typename AddrT>
void SaiRouteManager::addOrUpdateRoute(
    SaiRouteHandle* routeHandle,
    RouterID routerId,
    const std::shared_ptr<Route<AddrT>>& oldRoute,
    const std::shared_ptr<Route<AddrT>>& newRoute,
    const std::shared_ptr<SwitchState>& state) {
  SaiRouteTraits::RouteEntry entry = routeEntryFromSwRoute(routerId, newRoute);
  const auto& fwd = newRoute->getForwardInfo();
  sai_int32_t packetAction;
  std::optional<SaiRouteTraits::CreateAttributes> attributes;
  SaiRouteHandle::NextHopHandle nextHopHandle;
  std::optional<SaiRouteTraits::Attributes::Metadata> metadata;
  std::optional<SaiRouteTraits::Attributes::CounterID> counterID;
  std::shared_ptr<SaiCounterHandle> counterHandle;
  if (newRoute->getClassID()) {
    metadata = static_cast<sai_uint32_t>(newRoute->getClassID().value());
#if defined(BRCM_SAI_SDK_XGS)
    // TODO(daiweix): remove this #if defined(BRCM_SAI_SDK_XGS) after
    // support classid_for_unresolved_routes feature on XGS and
    // explicitly program default class id value 0 if not specified.
    // For now, keep the old behavior.
  } else if (oldRoute && oldRoute->getClassID()) {
    metadata = 0;
  }
#else
  } else {
    metadata = 0;
  }
#endif
  counterHandle = getCounterHandleForRoute(newRoute, oldRoute, counterID);

  if (fwd.getAction() == RouteForwardAction::NEXTHOPS) {
    packetAction = SAI_PACKET_ACTION_FORWARD;
    const auto nhops = getNextHops(state, fwd);
    /*
     * A route carrying a backup next hop must be programmed as a protection
     * group even when only one next hop survives resolution -- losing the
     * primary is exactly when protection matters. As a plain next hop the
     * standby semantics are dropped at the hardware boundary and traffic
     * forwards over the backup as though it were the primary path.
     *
     * Role is a per next hop property that normalization preserves, so the
     * group type can be decided from the unnormalized set and the more
     * expensive getNormalizedNextHops() stays on the group path only.
     */
    const bool needsProtectionGroup =
        isProtectionNextHopGroupType(getNextHopGroupType(nhops));
    /*
     * A Route which satisfies isConnected() is an interface subnet route.
     * It will have one NextHop with the ip configured for the interface
     * and with the configured InterfaceID.
     * We can use that InterfaceID to lookup the SAI id of the corresponding
     * router_interface which is the proper next hop for the subnet route.
     * (see sairoute.h for an explanation of router_interface_id as next hop)
     */
    if (newRoute->isConnected()) {
      CHECK_EQ(nhops.size(), 1);
      InterfaceID interfaceId{nhops.begin()->intf()};

      /*
       * For VOQ switches with multiple asics, set connected routes to drop
       * if the interface is not on the same asic.
       * TODO - filter these connected routes in switchstate
       */
      if (platform_->getAsic()->getSwitchType() == cfg::SwitchType::VOQ) {
        const auto& systemPortRanges =
            platform_->getAsic()->getSystemPortRanges();
        CHECK(!systemPortRanges.systemPortRanges()->empty());
        if (!withinRange(systemPortRanges, interfaceId)) {
          packetAction = SAI_PACKET_ACTION_DROP;
        }
      } else if (
          platform_->getAsic()->getSwitchType() == cfg::SwitchType::NPU) {
        /* Set connected routes to drop if the interface is not on the same
         * asic for multi-NPU switches.
         * TODO - Selectively add routes with nexthop interfaces on the same
         * asic, associate different ranges of interfaceIDs with each NPU.
         */
        if (!managerTable_->routerInterfaceManager().getRouterInterfaceHandle(
                interfaceId)) {
          packetAction = SAI_PACKET_ACTION_DROP;
        }
      }

      if (packetAction == SAI_PACKET_ACTION_DROP) {
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
        attributes = SaiRouteTraits::CreateAttributes{
            packetAction, SAI_NULL_OBJECT_ID, metadata, counterID};
#else
        attributes = SaiRouteTraits::CreateAttributes{
            packetAction, SAI_NULL_OBJECT_ID, metadata};
#endif
      } else {
        const SaiRouterInterfaceHandle* routerInterfaceHandle =
            managerTable_->routerInterfaceManager().getRouterInterfaceHandle(
                interfaceId);
        if (!routerInterfaceHandle) {
          throw FbossError(
              "cannot create subnet route without a sai_router_interface "
              "for InterfaceID: ",
              interfaceId);
        }
        /*
         * Remote interface routes are treated as connected for ECMP route
         * resolution. In hw, the routes are pointing to drop to avoid
         * attracting traffic.
         */
        if (!routerInterfaceHandle->isLocal()) {
          packetAction = SAI_PACKET_ACTION_DROP;
        }
        /*
         * For a subnet route pointing to a router interface BRCM-SAI uses
         * classID 2. This packet would be copied to low priority queue to CPU
         * when there is no conflict to this action.
         * If a DENY data ACL conflicts with this action, the packet will be
         * dropped before getting copied to the CPU. The reason is this classID
         * is not backed by an rx reason or any other actions in the pipeline.
         *
         * The below flag means an ACL is also added to the configuration (in
         * the cpu-policer section) which will have a higher priority than all
         * other data ACLs.
         *
         * As a side note, for a host route, BRCM-SAI uses a class ID 1 and
         * the IP2ME rx reason ensures this packet is duplicated and copied to
         * the CPU even if there is a conflicting DENY ACL.
         *
         * Not-applicable to TAJO because update metadata on interface subnet
         * route is unsupported. Also not supported on BRCM-SAI, which does not
         * fuly support route classID programming yet.
         *
         * So, set_classid_for_my_subnet_and_ip_routes is currently disabled
         * everywhere
         */
        if (FLAGS_set_classid_for_my_subnet_and_ip_routes &&
            platform_->getAsic()->isSupported(
                HwAsic::Feature::ROUTE_METADATA) &&
            routerInterfaceHandle->isLocal()) {
          XLOG(DBG2) << "set my subnet route " << newRoute->str()
                     << " class id to 2";
          metadata =
              static_cast<uint32_t>(cfg::AclLookupClass::DST_CLASS_L3_LOCAL_2);
        }
        RouterInterfaceSaiId routerInterfaceId{
            routerInterfaceHandle->adapterKey()};
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
        attributes = SaiRouteTraits::CreateAttributes{
            packetAction, routerInterfaceId, metadata, counterID};
#else
        attributes = SaiRouteTraits::CreateAttributes{
            packetAction, routerInterfaceId, metadata};
#endif

        XLOG(DBG3) << "Connected route: " << newRoute->str()
                   << " routerInterfaceId: " << routerInterfaceId;
      }
    } else if (nhops.size() > 1 || needsProtectionGroup) {
      /*
       * A Route which has more than one NextHops will create or reference an
       * existing SaiNextHopGroup corresponding to ECMP over those next hops.
       * When no route refers to a next hop set, it will be removed in SAI as
       * well.
       */
      auto normalizedNextHops = getNormalizedNextHops(state, fwd);
      const auto nextHopGroupType = getNextHopGroupType(normalizedNextHops);
      auto nextHopGroupHandle =
          managerTable_->nextHopGroupManager().incRefOrAddNextHopGroup(
              SaiNextHopGroupKey(
                  std::move(normalizedNextHops),
                  fwd.getOverrideEcmpSwitchingMode(),
                  nextHopGroupType));

      // For multi-NPU switches, if all next hops were filtered out (none have
      // router interfaces on this ASIC), the nextHopGroup will be null.
      // Set the route to DROP in this case.
      if (!nextHopGroupHandle->nextHopGroup &&
          platform_->hasMultipleSwitches()) {
        packetAction = SAI_PACKET_ACTION_DROP;
        XLOG(DBG3) << "All next hops filtered out on switchId: "
                   << platform_->getAsic()->getSwitchId().value()
                   << ", setting route to DROP: " << newRoute->str();
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
        attributes = SaiRouteTraits::CreateAttributes{
            packetAction, SAI_NULL_OBJECT_ID, metadata, counterID};
#else
        attributes = SaiRouteTraits::CreateAttributes{
            packetAction, SAI_NULL_OBJECT_ID, metadata};
#endif
      } else {
        NextHopGroupSaiId nextHopGroupId{
            nextHopGroupHandle->nextHopGroup->adapterKey()};

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
        attributes = SaiRouteTraits::CreateAttributes{
            packetAction, nextHopGroupId, metadata, counterID};
#else
        attributes = SaiRouteTraits::CreateAttributes{
            packetAction, nextHopGroupId, metadata};
#endif
        nextHopHandle = nextHopGroupHandle;

        XLOG(DBG3) << "Route nhops > 1: " << newRoute->str()
                   << " nextHopGroupId: " << nextHopGroupId;
      }
    } else {
      CHECK_EQ(nhops.size(), 1);
      /* A route which has only one next hop, create a subscriber for next hop
       * to make route point back and forth next hop or CPU
       */
      auto swNextHop = folly::poly_cast<ResolvedNextHop>(*(nhops.begin()));

      InterfaceID interfaceId{nhops.begin()->intf()};
      const SaiRouterInterfaceHandle* routerInterfaceHandle =
          managerTable_->routerInterfaceManager().getRouterInterfaceHandle(
              interfaceId);
      if (!routerInterfaceHandle) {
        if ((platform_->getAsic()->getSwitchType() == cfg::SwitchType::NPU)) {
          /* Set non-connected routes to drop if the interface is not on the
           * same asic for multi-NPU switches. as above connected branch.
           */
          packetAction = SAI_PACKET_ACTION_DROP;
          XLOG(DBG3)
              << "cannot create managed nexthop without a sai_router_interface "
                 "for InterfaceID: "
              << interfaceId << ", drop it";
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
          attributes = SaiRouteTraits::CreateAttributes{
              packetAction, SAI_NULL_OBJECT_ID, metadata, counterID};
#else
          attributes = SaiRouteTraits::CreateAttributes{
              packetAction, SAI_NULL_OBJECT_ID, metadata};
#endif
        } else {
          XLOG(ERR)
              << "cannot create managed nexthop without a sai_router_interface "
              << "for InterfaceID: " << interfaceId;
          CHECK_EQ(platform_->getAsic()->getSwitchType(), cfg::SwitchType::VOQ);
          throw FbossError(
              "cannot create managed nexthop without a sai_router_interface "
              "for InterfaceID: ",
              interfaceId);
        }
      } else {
        bool localNexthop = routerInterfaceHandle->isLocal();

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
        std::shared_ptr<SaiSrv6SidListHandle> srv6SidListHandle;
        if (!swNextHop.srv6SegmentList().empty()) {
          auto [sidListKey, sidListAttrs] = makeSrv6SidListKeyAndAttributes(
              routerInterfaceHandle->adapterKey(), swNextHop);
          srv6SidListHandle =
              managerTable_->srv6SidListManager().addOrReuseSrv6SidList(
                  sidListKey, sidListAttrs);
        }
        auto managedSaiNextHop =
            managerTable_->nextHopManager().addManagedSaiNextHop(
                swNextHop, std::move(srv6SidListHandle));
#else
        auto managedSaiNextHop =
            managerTable_->nextHopManager().addManagedSaiNextHop(swNextHop);
#endif
        sai_object_id_t nextHopId{};
        bool nextHopResolved{false};

        /* claim the next hop first */
        std::visit(
            [&](auto& managedNextHop) {
              using SharedPtrType = std::decay_t<decltype(managedNextHop)>;
              using NextHopTraits =
                  typename SharedPtrType::element_type::ObjectTraits;

              auto managedRouteNextHop =
                  refOrCreateManagedRouteNextHop<NextHopTraits>(
                      routeHandle,
                      entry,
                      managedNextHop,
                      metadata,
                      localNexthop);

              nextHopId = managedRouteNextHop->adapterKey();
              nextHopResolved = managedRouteNextHop->isReady();
              nextHopHandle = managedRouteNextHop;
            },
            managedSaiNextHop);

        if (!localNexthop && !nextHopResolved) {
          packetAction = SAI_PACKET_ACTION_DROP;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
          attributes = SaiRouteTraits::CreateAttributes{
              packetAction, SAI_NULL_OBJECT_ID, metadata, counterID};
#else
          attributes = SaiRouteTraits::CreateAttributes{
              packetAction, SAI_NULL_OBJECT_ID, metadata};
#endif

          XLOG(DBG3) << "Route unresolved remote nhop: " << newRoute->str()
                     << " localNextHop: " << localNexthop;
        } else {
          /*
           * Refer S390808 for more details.
           *
           * FBOSS behaviour:
           * Routes that uses single nexthop and are unresolved points to
           * cpu port as the nexthop to trigger neighbor resolution. This is
           * by setting the nexthop ID of the route to CPU port.
           *
           * SAI spec expectation:
           * Based on the SAI spec, any route that points to CPU port should
           * be treated as MYIP. As there is no way to differentiate between
           * interface routes and VIp/ILA routes, both the routes are
           * programmed with CPU port as the nexthop. NOTE that we configure
           * a hostif trap for MYIP which will send packets to mid pri queue.
           *
           * BCM SAI behavior:
           * Any /32 or /128 routes that points to CPU port will be
           * treated as MYIP. BCM SAI ensures that only host routes
           * are treated as IP2ME routes. For unresolved subnet routes, even
           * though nexthop is set to CPU port, the class ID is set
           * SUBNET_CLASS_ID which defaults to queue 0. For all unreoslved host
           * routes, the class ID is set to IP2ME class ID which will be
           * sent to mid pri queue due to IP2ME hostif trap. However, brcm-sai
           * is also implicitly programming route classID underlyingly, which
           * could cause conflict with our programming. So, do nothing on
           * brcm sai switches for now, until brcm-sai is enhanced to support
           * it.
           *
           * TAJO SDK behavior:
           * For Tajo, any route (host route or subnet route) that points to
           * CPU port will be treated as MYIP. Both host and subnet routes
           * that are unresolved will be sent to mid pri queue due to the
           * IP2ME hostif trap. So, FLAGS_classid_for_unresolved_routes is
           * currently only enabled on tajo switches.
           *
           * Fix for the issue:
           * In order to send these not MYIP routes to default queue,
           * 1) Program unresolved routes that points to CPU port to a class ID
           * CLASS_UNRESOLVED_ROUTE_TO_CPU
           * 2) Add an ACL with qualifer as CLASS_UNRESOLVED_ROUTE_TO_CPU and
           * action as low pri queue.
           */
          if (FLAGS_classid_for_unresolved_routes &&
              platform_->getAsic()->isSupported(
                  HwAsic::Feature::ROUTE_METADATA)) {
            if (nextHopId == managerTable_->switchManager().getCpuPort()) {
              XLOG(DBG2) << "set route class id to 2";
              metadata = static_cast<uint32_t>(
                  cfg::AclLookupClass::DST_CLASS_L3_LOCAL_2);
            }
          }
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
          attributes = SaiRouteTraits::CreateAttributes{
              packetAction, nextHopId, metadata, counterID};
#else
          attributes = SaiRouteTraits::CreateAttributes{
              packetAction, nextHopId, metadata};
#endif

          XLOG(DBG3) << "Route nhops == 1: " << newRoute->str()
                     << " nextHopId: " << nextHopId
                     << " localNextHop: " << localNexthop;
        }
      }
    }

  } else if (fwd.getAction() == RouteForwardAction::TO_CPU) {
    packetAction = SAI_PACKET_ACTION_FORWARD;
    PortSaiId cpuPortId = managerTable_->switchManager().getCpuPort();
    if (FLAGS_set_classid_for_my_subnet_and_ip_routes &&
        platform_->getAsic()->isSupported(HwAsic::Feature::ROUTE_METADATA)) {
      XLOG(DBG2) << "set my ip route " << newRoute->str() << " class id to 1";
      metadata =
          static_cast<uint32_t>(cfg::AclLookupClass::DST_CLASS_L3_LOCAL_1);
    }
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
    attributes = SaiRouteTraits::CreateAttributes{
        packetAction, cpuPortId, metadata, counterID};
#else
    attributes =
        SaiRouteTraits::CreateAttributes{packetAction, cpuPortId, metadata};
#endif

    XLOG(DBG3) << "Route action TO CPU: " << newRoute->str()
               << " cpuPortId: " << cpuPortId;
  } else if (fwd.getAction() == RouteForwardAction::DROP) {
    packetAction = SAI_PACKET_ACTION_DROP;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
    attributes = SaiRouteTraits::CreateAttributes{
        packetAction, SAI_NULL_OBJECT_ID, metadata, counterID};
#else
    attributes = SaiRouteTraits::CreateAttributes{
        packetAction, SAI_NULL_OBJECT_ID, metadata};
#endif

    XLOG(DBG3) << "Route action DROP: " << newRoute->str();
  }
  auto& store = saiStore_->get<SaiRouteTraits>();
  auto route = store.setObject(entry, attributes.value());
  routeHandle->route = route;
  routeHandle->nexthopHandle_ = nextHopHandle;
  routeHandle->counterHandle_ = counterHandle;
  if (!newRoute->isConnected()) {
    checkMetadata(entry);
  }
}

template <typename AddrT>
void SaiRouteManager::changeRoute(
    const std::shared_ptr<Route<AddrT>>& oldSwRoute,
    const std::shared_ptr<Route<AddrT>>& newSwRoute,
    RouterID routerId,
    const std::shared_ptr<SwitchState>& state) {
  SaiRouteTraits::RouteEntry entry =
      routeEntryFromSwRoute(routerId, newSwRoute);

  if (!validRoute(newSwRoute)) {
    XLOG(DBG3) << "Not a valid route, don't change:: old: " << oldSwRoute->str()
               << " new: " << newSwRoute->str();
    return;
  }

  auto itr = handles_.find(entry);
  if (itr == handles_.end()) {
    throw FbossError(
        "Failure to update route. Route does not exist ",
        newSwRoute->prefix().str());
  }
  addOrUpdateRoute(itr->second.get(), routerId, oldSwRoute, newSwRoute, state);
  swRoutes_.erase(entry);
  swRoutes_.emplace(entry, newSwRoute);
}

template <typename AddrT>
void SaiRouteManager::addRoute(
    const std::shared_ptr<Route<AddrT>>& swRoute,
    RouterID routerId,
    const std::shared_ptr<SwitchState>& state) {
  SaiRouteTraits::RouteEntry entry = routeEntryFromSwRoute(routerId, swRoute);
  auto itr = handles_.find(entry);
  if (itr != handles_.end()) {
    throw FbossError(
        "Failure to add route. A route already exists to ",
        swRoute->prefix().str());
  }
  if (!validRoute(swRoute)) {
    XLOG(DBG3) << "Not a valid route, don't add: " << swRoute->str();
    return;
  }
  auto routeHandle = std::make_unique<SaiRouteHandle>();
  addOrUpdateRoute(
      routeHandle.get(),
      routerId,
      std::shared_ptr<Route<AddrT>>{},
      swRoute,
      state);
  handles_.emplace(entry, std::move(routeHandle));
  swRoutes_.emplace(entry, swRoute);
}

template <typename AddrT>
void SaiRouteManager::removeRoute(
    const std::shared_ptr<Route<AddrT>>& swRoute,
    RouterID routerId) {
  if (!validRoute(swRoute)) {
    XLOG(DBG3) << "Not a valid route, don't remove: " << swRoute->str();
    return;
  }
  XLOG(DBG3) << "Remove route: " << swRoute->str();
  SaiRouteTraits::RouteEntry entry = routeEntryFromSwRoute(routerId, swRoute);
  size_t count = handles_.erase(entry);
  if (!count) {
    throw FbossError(
        "Failed to remove non-existent route to ", swRoute->prefix().str());
  }
  swRoutes_.erase(entry);
}

template <typename AddrT>
void SaiRouteManager::removeRouteForRollback(
    const std::shared_ptr<Route<AddrT>>& swRoute,
    RouterID routerId) {
  XLOG(DBG3) << "Remove route for rollback: " << swRoute->str();
  SaiRouteTraits::RouteEntry entry = routeEntryFromSwRoute(routerId, swRoute);
  handles_.erase(entry);
  swRoutes_.erase(entry);
}

SaiRouteHandle* SaiRouteManager::getRouteHandle(
    const SaiRouteTraits::RouteEntry& entry) {
  return getRouteHandleImpl(entry);
}
const SaiRouteHandle* SaiRouteManager::getRouteHandle(
    const SaiRouteTraits::RouteEntry& entry) const {
  return getRouteHandleImpl(entry);
}
SaiRouteHandle* SaiRouteManager::getRouteHandleImpl(
    const SaiRouteTraits::RouteEntry& entry) const {
  auto itr = handles_.find(entry);
  if (itr == handles_.end()) {
    return nullptr;
  }
  if (!itr->second.get()) {
    XLOG(FATAL) << "invalid null route for destination: "
                << entry.destination().first << "/"
                << static_cast<int>(entry.destination().second);
  }
  return itr->second.get();
}

void SaiRouteManager::clear() {
  handles_.clear();
  swRoutes_.clear();
}

std::shared_ptr<SaiObject<SaiRouteTraits>> SaiRouteManager::getRouteObject(
    SaiRouteTraits::AdapterHostKey routeKey) {
  return saiStore_->get<SaiRouteTraits>().get(routeKey);
}

template <typename AddrT>
std::shared_ptr<SaiCounterHandle> SaiRouteManager::getCounterHandleForRoute(
    const std::shared_ptr<Route<AddrT>>& newRoute,
    const std::shared_ptr<Route<AddrT>>& oldRoute,
    std::optional<SaiRouteTraits::Attributes::CounterID>& counterID) {
  std::shared_ptr<SaiCounterHandle> counterHandle;
  const auto& fwd = newRoute->getForwardInfo();
  if (fwd.getCounterID().has_value()) {
    counterHandle = managerTable_->counterManager().incRefOrAddRouteCounter(
        fwd.getCounterID().value());
    if (counterHandle) {
      counterID.emplace(counterHandle->adapterKey());
    }
  } else if (
      oldRoute && oldRoute->getForwardInfo().getCounterID().has_value()) {
    counterID = 0;
  }
  return counterHandle;
}

template <
    typename NextHopTraitsT,
    typename ManagedNextHopT,
    typename ManagedRouteNextHopT>
std::shared_ptr<ManagedRouteNextHopT>
SaiRouteManager::refOrCreateManagedRouteNextHop(
    SaiRouteHandle* routeHandle,
    SaiRouteTraits::RouteEntry entry,
    std::shared_ptr<ManagedNextHopT> nexthop,
    std::optional<SaiRouteTraits::Attributes::Metadata> metadata,
    bool localNextHop) {
  auto routeNexthopHandle = routeHandle->nexthopHandle_;
  using ManagedNextHopSharedPtr = std::shared_ptr<ManagedRouteNextHopT>;
  if (std::holds_alternative<ManagedNextHopSharedPtr>(routeNexthopHandle)) {
    auto existingManagedRouteNextHop =
        std::get<ManagedNextHopSharedPtr>(routeNexthopHandle);
    CHECK(existingManagedRouteNextHop) << "null managed route next hop";
    if (existingManagedRouteNextHop->adapterHostKey() ==
        nexthop->adapterHostKey()) {
      auto oldMetadata = existingManagedRouteNextHop->getMetadata();
      XLOG(DBG3) << "update existing managedRouteNextHop metadata from "
                 << (oldMetadata.has_value()
                         ? std::to_string(oldMetadata.value().value())
                         : "none")
                 << " to "
                 << (metadata.has_value()
                         ? std::to_string(metadata.value().value())
                         : "none");
      existingManagedRouteNextHop->setMetadata(metadata);
      existingManagedRouteNextHop->setLocalNextHop(localNextHop);
      return existingManagedRouteNextHop;
    }
  }
  auto routeMetadataSupported = FLAGS_classid_for_unresolved_routes &&
      platform_->getAsic()->isSupported(HwAsic::Feature::ROUTE_METADATA);
  PortSaiId cpuPort = managerTable_->switchManager().getCpuPort();
  auto managedRouteNextHop = std::make_shared<ManagedRouteNextHopT>(
      cpuPort,
      this,
      entry,
      nexthop,
      routeMetadataSupported,
      metadata,
      localNextHop);
  XLOG(DBG3) << "create new managedRouteNexHop with metadata "
             << (metadata.has_value() ? std::to_string(metadata.value().value())
                                      : "none");
  SaiObjectEventPublisher::getInstance()->get<NextHopTraitsT>().subscribe(
      managedRouteNextHop);
  return managedRouteNextHop;
}

void SaiRouteManager::checkMetadata(SaiRouteTraits::RouteEntry entry) {
  auto route = getRouteObject(entry);
  if (!route) {
    return;
  }

  auto attributes = route->attributes();
  auto metadata =
      std::get<std::optional<SaiRouteTraits::Attributes::Metadata>>(attributes);
  auto nexthop = std::get<std::optional<SaiRouteTraits::Attributes::NextHopId>>(
      attributes);
  const auto& toCpuClassIds = getToCpuClassIds();
  if (metadata.has_value() && metadata.value().value() &&
      std::find(
          toCpuClassIds.begin(),
          toCpuClassIds.end(),
          static_cast<cfg::AclLookupClass>(metadata.value().value())) !=
          toCpuClassIds.end() &&
      nexthop.has_value() &&
      nexthop.value() !=
          static_cast<sai_object_id_t>(
              managerTable_->switchManager().getCpuPort())) {
    // invalid
    XLOG(FATAL) << "found invalid route entry with class id value "
                << std::to_string(metadata.value().value())
                << " but cpu is not nexthop: " << entry.toString();
  }
}

template <typename NextHopTraitsT>
ManagedRouteNextHop<NextHopTraitsT>::ManagedRouteNextHop(
    PortSaiId cpuPort,
    SaiRouteManager* routeManager,
    SaiRouteTraits::AdapterHostKey routeKey,
    std::shared_ptr<ManagedNextHop<NextHopTraitsT>> managedNextHop,
    bool routeMetadataSupported,
    std::optional<SaiRouteTraits::Attributes::Metadata> metadata,
    bool localNextHop)
    : detail::SaiObjectEventSubscriber<NextHopTraitsT>(
          managedNextHop->adapterHostKey()),
      cpuPort_(cpuPort),
      routeManager_(routeManager),
      routeKey_(std::move(routeKey)),
      managedNextHop_(managedNextHop),
      routeMetadataSupported_(routeMetadataSupported),
      localNextHop_(localNextHop),
      metadata_(metadata) {}

template <typename NextHopTraitsT>
void ManagedRouteNextHop<NextHopTraitsT>::afterCreate(
    ManagedRouteNextHop<NextHopTraitsT>::PublisherObject nexthop) {
  CHECK(adapterKey() != nexthop->adapterKey())
      << "attempting to reset to the same next hop";
  this->setPublisherObject(nexthop);
  // set route to next hop
  auto route = routeManager_->getRouteObject(routeKey_);
  if (!route) {
    XLOG(DBG2) << "ManagedRouteNextHop afterCreate , route not yet created: "
               << routeKey_.toString();
    // route is not yet created.
    return;
  }
  auto& api = SaiApiTable::getInstance()->routeApi();
  SaiRouteTraits::Attributes::Metadata currentMetadata = routeMetadataSupported_
      ? api.getAttribute(
            route->adapterKey(), SaiRouteTraits::Attributes::Metadata{})
      : 0;
  auto attributes = route->attributes();
  sai_object_id_t nextHopId = nexthop->adapterKey();
  auto& currentNextHop =
      std::get<std::optional<SaiRouteTraits::Attributes::NextHopId>>(
          attributes);
  auto& metadata =
      std::get<std::optional<SaiRouteTraits::Attributes::Metadata>>(attributes);
  auto& packetAction =
      std::get<SaiRouteTraits::Attributes::PacketAction>(attributes);
  currentNextHop = nextHopId;
  packetAction = SAI_PACKET_ACTION_FORWARD;
  if (routeMetadataSupported_) {
    metadata = metadata_;
  }
  route->setAttributes(attributes);
  updateMetadata(currentMetadata);
  XLOG(DBG2) << "ManagedRouteNextHop afterCreate: " << routeKey_.toString()
             << " assign nextHopId: " << nextHopId << " metadata: "
             << (metadata.has_value() ? std::to_string(metadata.value().value())
                                      : "none");
}

template <typename NextHopTraitsT>
void ManagedRouteNextHop<NextHopTraitsT>::beforeRemove() {
  XLOG(DBG2) << "ManagedRouteNextHop beforeRemove, set local route to "
             << (localNextHop_ ? "CPU " : "DROP") << ": "
             << routeKey_.toString();

  auto route = routeManager_->getRouteObject(routeKey_);
  auto attributes = route->attributes();
  auto& api = SaiApiTable::getInstance()->routeApi();

  SaiRouteTraits::Attributes::Metadata currentMetadata = routeMetadataSupported_
      ? api.getAttribute(
            route->adapterKey(), SaiRouteTraits::Attributes::Metadata{})
      : 0;

  if (localNextHop_) {
    // set route to CPU
    std::get<std::optional<SaiRouteTraits::Attributes::NextHopId>>(attributes) =
        static_cast<sai_object_id_t>(cpuPort_);
    if (routeMetadataSupported_) {
      std::get<std::optional<SaiRouteTraits::Attributes::Metadata>>(
          attributes) =
          static_cast<uint32_t>(cfg::AclLookupClass::DST_CLASS_L3_LOCAL_2);
    }
  } else {
    // Remote next hop, set route to DROP
    std::get<std::optional<SaiRouteTraits::Attributes::NextHopId>>(attributes) =
        SAI_NULL_OBJECT_ID;
    std::get<SaiRouteTraits::Attributes::PacketAction>(attributes) =
        SAI_PACKET_ACTION_DROP;
  }

  route->setAttributes(attributes);
  updateMetadata(currentMetadata);
  this->setPublisherObject(nullptr);
}

template <typename NextHopTraitsT>
sai_object_id_t ManagedRouteNextHop<NextHopTraitsT>::adapterKey() const {
  if (!this->isReady()) {
    return static_cast<sai_object_id_t>(cpuPort_);
  }
  return this->getPublisherObject().lock()->adapterKey();
}

template <typename NextHopTraitsT>
void ManagedRouteNextHop<NextHopTraitsT>::updateMetadata(
    SaiRouteTraits::Attributes::Metadata currentMetadata) const {
  if (!routeMetadataSupported_) {
    return;
  }
  auto route = routeManager_->getRouteObject(routeKey_);
  CHECK(route);

  auto expectedMetadata =
      std::get<std::optional<SaiRouteTraits::Attributes::Metadata>>(
          route->attributes());
  if (!expectedMetadata) {
    // SAI object of route pointing to CPU, and directly reloaded from store
    // may have reserved metadata (set by SDK) in its attributes.
    expectedMetadata = SaiRouteTraits::Attributes::Metadata::defaultValue();
  }

  if (expectedMetadata.value() != currentMetadata) {
    /*
     * Set Sai object attr to actual metadata and then update.
     * In BRCM-SAI there is a case where the SAI SDK itself updates
     * metadata of a route when it points to CPU. Now in SaiObject
     * the metatdata may still be set to expectedMetadata, while the
     * HW would have it be == actualMetadata. So force our in memory
     * state to reconcile with HW value and then set to desired
     * expectedMetadata value
     */
    route->setOptionalAttribute(
        SaiRouteTraits::Attributes::Metadata{currentMetadata},
        true /*skip HW write*/);
    route->setAttribute(expectedMetadata);
  }
}

template <typename NextHopTraitsT>
typename NextHopTraitsT::AdapterHostKey
ManagedRouteNextHop<NextHopTraitsT>::adapterHostKey() const {
  return this->managedNextHop_->adapterHostKey();
}

template <typename NextHopTraitsT>
std::optional<SaiRouteTraits::Attributes::Metadata>
ManagedRouteNextHop<NextHopTraitsT>::getMetadata() const {
  return metadata_;
}

template <typename NextHopTraitsT>
void ManagedRouteNextHop<NextHopTraitsT>::setMetadata(
    std::optional<SaiRouteTraits::Attributes::Metadata> metadata) {
  metadata_ = metadata;
}

template <typename NextHopTraitsT>
void ManagedRouteNextHop<NextHopTraitsT>::setLocalNextHop(bool localNextHop) {
  localNextHop_ = localNextHop;
}

template <typename NextHopTraitsT>
ManagedRouteNextHop<NextHopTraitsT>::~ManagedRouteNextHop() {
  auto route = routeManager_->getRouteObject(routeKey_);
  if (!route || !routeMetadataSupported_) {
    return;
  }
  auto& api = SaiApiTable::getInstance()->routeApi();
  SaiRouteTraits::Attributes::Metadata currentMetadata = routeMetadataSupported_
      ? api.getAttribute(
            route->adapterKey(), SaiRouteTraits::Attributes::Metadata{})
      : 0;
  auto attributes = route->attributes();
  auto& metadata =
      std::get<std::optional<SaiRouteTraits::Attributes::Metadata>>(attributes);
  metadata = metadata_;
  route->setAttributes(attributes);
  updateMetadata(currentMetadata);
  XLOG(DBG2) << "ManagedRouteNextHop beforeDestroy: " << routeKey_.toString()
             << " metadata: "
             << (metadata.has_value() ? std::to_string(metadata.value().value())
                                      : "None");
}

template class ManagedRouteNextHop<SaiIpNextHopTraits>;
template class ManagedRouteNextHop<SaiMplsNextHopTraits>;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
template class ManagedRouteNextHop<SaiSrv6SidlistNextHopTraits>;
#endif

template SaiRouteTraits::RouteEntry
SaiRouteManager::routeEntryFromSwRoute<folly::IPAddressV6>(
    RouterID routerId,
    const std::shared_ptr<Route<folly::IPAddressV6>>& swEntry) const;
template SaiRouteTraits::RouteEntry
SaiRouteManager::routeEntryFromSwRoute<folly::IPAddressV4>(
    RouterID routerId,
    const std::shared_ptr<Route<folly::IPAddressV4>>& swEntry) const;

template void SaiRouteManager::changeRoute<folly::IPAddressV6>(
    const std::shared_ptr<Route<folly::IPAddressV6>>& oldSwEntry,
    const std::shared_ptr<Route<folly::IPAddressV6>>& newSwEntry,
    RouterID routerId,
    const std::shared_ptr<SwitchState>& state);
template void SaiRouteManager::changeRoute<folly::IPAddressV4>(
    const std::shared_ptr<Route<folly::IPAddressV4>>& oldSwEntry,
    const std::shared_ptr<Route<folly::IPAddressV4>>& newSwEntry,
    RouterID routerId,
    const std::shared_ptr<SwitchState>& state);

template void SaiRouteManager::addRoute<folly::IPAddressV6>(
    const std::shared_ptr<Route<folly::IPAddressV6>>& swEntry,
    RouterID routerId,
    const std::shared_ptr<SwitchState>& state);
template void SaiRouteManager::addRoute<folly::IPAddressV4>(
    const std::shared_ptr<Route<folly::IPAddressV4>>& swEntry,
    RouterID routerId,
    const std::shared_ptr<SwitchState>& state);

template void SaiRouteManager::removeRoute<folly::IPAddressV6>(
    const std::shared_ptr<Route<folly::IPAddressV6>>& swEntry,
    RouterID routerId);
template void SaiRouteManager::removeRoute<folly::IPAddressV4>(
    const std::shared_ptr<Route<folly::IPAddressV4>>& swEntry,
    RouterID routerId);

template void SaiRouteManager::removeRouteForRollback<folly::IPAddressV6>(
    const std::shared_ptr<Route<folly::IPAddressV6>>& swEntry,
    RouterID routerId);
template void SaiRouteManager::removeRouteForRollback<folly::IPAddressV4>(
    const std::shared_ptr<Route<folly::IPAddressV4>>& swEntry,
    RouterID routerId);

} // namespace facebook::fboss
