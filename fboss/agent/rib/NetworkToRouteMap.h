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

#include "fboss/agent/state/Route.h"
#include "fboss/agent/types.h"
#include "fboss/lib/RadixTree.h"

#include <folly/IPAddress.h>
#include <folly/json/dynamic.h>
#include <gflags/gflags.h>

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

DECLARE_bool(enable_nexthop_id_manager);

namespace facebook::fboss {

template <typename AddressT>
struct NetworkToRouteMapThriftType {
  using KeyType = std::
      conditional_t<std::is_same_v<AddressT, LabelID>, int32_t, std::string>;
  using ValueType = std::conditional_t<
      std::is_same_v<AddressT, LabelID>,
      state::LabelForwardingEntryFields,
      state::RouteFields>;
  using type = std::map<KeyType, ValueType>;
};

template <typename AddressT>
class NetworkToRouteMap
    : public std::conditional_t<
          std::is_same_v<LabelID, AddressT>,
          std::unordered_map<LabelID, std::shared_ptr<Route<LabelID>>>,
          facebook::network::
              RadixTree<AddressT, std::shared_ptr<Route<AddressT>>>> {
  static constexpr auto kRoutes = "routes";

 public:
  using Base = std::conditional_t<
      std::is_same_v<LabelID, AddressT>,
      std::unordered_map<LabelID, std::shared_ptr<Route<LabelID>>>,
      facebook::network::RadixTree<AddressT, std::shared_ptr<Route<AddressT>>>>;
  using Base::Base;
  /* implicit */ NetworkToRouteMap(Base&& radixTree)
      : Base(std::move(radixTree)) {}
  using RouteT = Route<AddressT>;
  using FilterFn = std::function<bool(const std::shared_ptr<RouteT>)>;
  using Iterator = std::conditional_t<
      std::is_same_v<LabelID, AddressT>,
      std::unordered_map<LabelID, std::shared_ptr<Route<LabelID>>>::iterator,
      typename facebook::network::
          RadixTree<AddressT, std::shared_ptr<Route<AddressT>>>::Iterator>;
  using ThriftType = typename NetworkToRouteMapThriftType<AddressT>::type;
  using RouteFilter =
      std::function<bool(const std::shared_ptr<Route<AddressT>>&)>;

  std::pair<Iterator, bool> insert(
      typename Route<AddressT>::Prefix key,
      std::shared_ptr<Route<AddressT>> route) {
    if constexpr (std::is_same_v<LabelID, AddressT>) {
      return this->emplace(
          std::make_pair(LabelID(key.label()), std::move(route)));
    } else {
      return Base::insert(key.network(), key.mask(), std::move(route));
    }
  }

  template <typename Fn>
  void forAll(const Fn& fn) {
    std::for_each(this->begin(), this->end(), fn);
  }
  void cloneAll() {
    forAll([](auto& ritr) { ritr.value() = ritr.value()->clone(); });
  }
  void publishAll() {
    forAll([](auto& ritr) { ritr.value()->publish(); });
  }

  ThriftType toThrift() const {
    return toFilteredThrift([](const auto&) { return true; });
  }

  ThriftType toFilteredThrift(RouteFilter&& filter) const {
    auto obj = ThriftType{};
    for (const auto& routeNode : *this) {
      std::shared_ptr<Route<AddressT>> route{};
      if constexpr (std::is_same_v<LabelID, AddressT>) {
        route = routeNode.second;
      } else {
        route = routeNode.value();
      }
      if (!filter(route)) {
        continue;
      }
      obj.emplace(route->getID(), route->toThrift());
    }
    return obj;
  }

  ThriftType warmBootState() const {
    // warm boot cares only for unresolved routes, resolved routes come from
    // fibs.
    return toFilteredThrift(
        [](const auto& route) { return !route->isResolved(); });
  }

  static NetworkToRouteMap<AddressT> fromThrift(const ThriftType& routes) {
    NetworkToRouteMap<AddressT> networkToRouteMap;
    for (auto& obj : routes) {
      auto route = std::make_shared<Route<AddressT>>(obj.second);
      // TODO: remove once enable_nexthop_id_manager is permanently on.
      // Flag-OFF means no IDs can be stamped, so any ID here is stale
      // from a prior flag-ON era. Wipe before a flag-OFF runtime sees it.
      if (!FLAGS_enable_nexthop_id_manager) {
        std::vector<std::pair<ClientID, RouteNextHopEntry>> updates;
        std::optional<NextHopSetID> nullId;
        for (const auto& [clientId, entry] :
             std::as_const(route->getEntryForClients())) {
          if (entry->getClientNextHopSetID().has_value()) {
            RouteNextHopEntry newEntry(entry->toThrift());
            newEntry.setClientNextHopSetID(nullId);
            updates.emplace_back(clientId, std::move(newEntry));
          }
        }
        for (auto& [clientId, newEntry] : updates) {
          route->update(clientId, newEntry);
        }
      }
      auto key = route->prefix();
      networkToRouteMap.insert(key, route);
    }
    return networkToRouteMap;
  }
};

using IPv4NetworkToRouteMap = NetworkToRouteMap<folly::IPAddressV4>;
using IPv6NetworkToRouteMap = NetworkToRouteMap<folly::IPAddressV6>;
using LabelToRouteMap = NetworkToRouteMap<LabelID>;

// One (v4RouteMap, v6RouteMap) pair per VRF. Resolution tries each in order
// and uses the first matching route.
using VrfRouteTables =
    std::vector<std::pair<IPv4NetworkToRouteMap*, IPv6NetworkToRouteMap*>>;

template <typename AddrT>
std::shared_ptr<Route<AddrT>>& value(
    typename NetworkToRouteMap<AddrT>::Iterator& iter) {
  if constexpr (std::is_same_v<LabelID, AddrT>) {
    return iter->second;
  } else {
    return iter->value();
  }
}

template <typename AddrT>
std::shared_ptr<Route<AddrT>>& value(
    facebook::network::RadixTreeNode<AddrT, std::shared_ptr<Route<AddrT>>>&
        iter) {
  return iter.value();
}

template <typename AddrT>
std::shared_ptr<Route<AddrT>>& value(
    std::pair<const AddrT, std::shared_ptr<Route<AddrT>>>& iter) {
  return iter.second;
}
} // namespace facebook::fboss
