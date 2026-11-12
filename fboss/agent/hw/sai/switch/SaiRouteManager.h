/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include "fboss/agent/hw/sai/api/NextHopGroupApi.h"
#include "fboss/agent/hw/sai/api/RouteApi.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/types.h"

#include "folly/container/F14Map.h"

#include "fboss/agent/hw/sai/store/SaiObjectEventSubscriber.h"

#include <memory>
#include <mutex>

DECLARE_bool(disable_valid_route_check);
DECLARE_bool(classid_for_unresolved_routes);

namespace facebook::fboss {

class SaiManagerTable;
class SaiPlatform;
struct SaiNextHopGroupHandle;
class SaiStore;
class SaiRouteManager;
struct SaiCounterHandle;

using SaiRoute = SaiObject<SaiRouteTraits>;

template <typename T>
class ManagedNextHop;
class SaiVxlanNextHopHandle;

template <typename NextHopTraitsT>
class ManagedRouteNextHop
    : public detail::SaiObjectEventSubscriber<NextHopTraitsT> {
 public:
  using PublisherObject = std::shared_ptr<const SaiObject<NextHopTraitsT>>;
  ManagedRouteNextHop(
      PortSaiId cpuPort,
      SaiRouteManager* routeManager,
      SaiRouteTraits::AdapterHostKey routeKey,
      std::shared_ptr<ManagedNextHop<NextHopTraitsT>> managedNextHop,
      bool routeMetadataSupported,
      std::optional<SaiRouteTraits::Attributes::Metadata> metadata,
      bool localNextHop);
  void afterCreate(PublisherObject nexthop) override;
  void beforeRemove() override;
  void linkDown() override {}
  sai_object_id_t adapterKey() const;
  using detail::SaiObjectEventSubscriber<NextHopTraitsT>::isReady;
  typename NextHopTraitsT::AdapterHostKey adapterHostKey() const;
  std::optional<SaiRouteTraits::Attributes::Metadata> getMetadata() const;
  void setMetadata(
      std::optional<SaiRouteTraits::Attributes::Metadata> metadata);
  void setLocalNextHop(bool localNextHop);
  ~ManagedRouteNextHop() override;

 private:
  void updateMetadata(
      SaiRouteTraits::Attributes::Metadata currentMetadata) const;

  PortSaiId cpuPort_;
  SaiRouteManager* routeManager_;
  typename SaiRouteTraits::AdapterHostKey routeKey_;
  std::shared_ptr<ManagedNextHop<NextHopTraitsT>> managedNextHop_;
  bool routeMetadataSupported_;
  bool localNextHop_;
  std::optional<SaiRouteTraits::Attributes::Metadata> metadata_;
};

using ManagedRouteIpNextHop = ManagedRouteNextHop<SaiIpNextHopTraits>;
using ManagedRouteMplsNextHop = ManagedRouteNextHop<SaiMplsNextHopTraits>;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
using ManagedRouteSrv6NextHop =
    ManagedRouteNextHop<SaiSrv6SidlistNextHopTraits>;
#endif

struct SaiRouteHandle {
  using NextHopHandle = std::variant<
      std::shared_ptr<SaiNextHopGroupHandle>,
      std::shared_ptr<ManagedRouteIpNextHop>,
      std::shared_ptr<ManagedRouteMplsNextHop>,
      std::shared_ptr<SaiVxlanNextHopHandle>
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
      ,
      std::shared_ptr<ManagedRouteSrv6NextHop>
#endif
      >;
  NextHopHandle nexthopHandle_;
  std::shared_ptr<SaiCounterHandle> counterHandle_;
  std::shared_ptr<SaiRoute> route;
  sai_object_id_t nextHopAdapterKey() const;
  std::shared_ptr<SaiNextHopGroupHandle> nextHopGroupHandle() const;
};

using SwitchStateRoutesMap = folly::F14FastMap<
    SaiRouteTraits::RouteEntry,
    std::variant<std::shared_ptr<RouteV4>, std::shared_ptr<RouteV6>>>;

class SaiRouteManager {
 public:
  SaiRouteManager(
      SaiStore* saiStore,
      SaiManagerTable* managerTable,
      const SaiPlatform* platform);
  // Helper function to create a SAI RouteEntry from an FBOSS SwitchState
  // Route (e.g., Route<IPAddressV6>)
  template <typename AddrT>
  SaiRouteTraits::RouteEntry routeEntryFromSwRoute(
      RouterID routerId,
      const std::shared_ptr<Route<AddrT>>& swEntry) const;

  template <typename AddrT>
  void changeRoute(
      const std::shared_ptr<Route<AddrT>>& oldSwRoute,
      const std::shared_ptr<Route<AddrT>>& newSwRoute,
      RouterID routerId,
      const std::shared_ptr<SwitchState>& state);

  std::vector<std::shared_ptr<SaiRoute>> makeInterfaceToMeRoutes(
      const std::shared_ptr<Interface>& swInterface);

  template <typename AddrT>
  void addRoute(
      const std::shared_ptr<Route<AddrT>>& swRoute,
      RouterID routerId,
      const std::shared_ptr<SwitchState>& state);

  template <typename AddrT>
  void removeRoute(
      const std::shared_ptr<Route<AddrT>>& swRoute,
      RouterID routerId);

  template <typename AddrT>
  void removeRouteForRollback(
      const std::shared_ptr<Route<AddrT>>& swRoute,
      RouterID routerId);

  SaiRouteHandle* getRouteHandle(const SaiRouteTraits::RouteEntry& entry);
  const SaiRouteHandle* getRouteHandle(
      const SaiRouteTraits::RouteEntry& entry) const;

  void clear();

  std::shared_ptr<SaiObject<SaiRouteTraits>> getRouteObject(
      SaiRouteTraits::AdapterHostKey routeKey);

  void checkMetadata(SaiRouteTraits::RouteEntry entry);

  const SwitchStateRoutesMap& getSwitchStateRoutesMap() const {
    return swRoutes_;
  }

 private:
  SaiRouteHandle* getRouteHandleImpl(
      const SaiRouteTraits::RouteEntry& entry) const;
  template <typename AddrT>
  void addOrUpdateRoute(
      SaiRouteHandle* routeHandle,
      RouterID routerId,
      const std::shared_ptr<Route<AddrT>>& oldRoute,
      const std::shared_ptr<Route<AddrT>>& newRoute,
      const std::shared_ptr<SwitchState>& state);

  template <typename AddrT>
  bool validRoute(const std::shared_ptr<Route<AddrT>>& swRoute);

  template <
      typename NextHopTraitsT,
      typename ManagedNextHopT = ManagedNextHop<NextHopTraitsT>,
      typename ManagedRouteNextHopT = ManagedRouteNextHop<NextHopTraitsT>>
  std::shared_ptr<ManagedRouteNextHopT> refOrCreateManagedRouteNextHop(
      SaiRouteHandle* routeHandle,
      SaiRouteTraits::RouteEntry entry,
      std::shared_ptr<ManagedNextHopT> nexthop,
      std::optional<SaiRouteTraits::Attributes::Metadata> metadata,
      bool localNextHop);

  template <typename AddrT>
  std::shared_ptr<SaiCounterHandle> getCounterHandleForRoute(
      const std::shared_ptr<Route<AddrT>>& newRoute,
      const std::shared_ptr<Route<AddrT>>& oldRoute,
      std::optional<SaiRouteTraits::Attributes::CounterID>& counterID);

  SaiStore* saiStore_;
  SaiManagerTable* managerTable_;
  const SaiPlatform* platform_;
  folly::F14FastMap<SaiRouteTraits::RouteEntry, std::unique_ptr<SaiRouteHandle>>
      handles_;
  SwitchStateRoutesMap swRoutes_;
};

} // namespace facebook::fboss
