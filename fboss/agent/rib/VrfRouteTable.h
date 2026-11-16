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

#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/rib/NetworkToRouteMap.h"
#include "fboss/agent/state/Route.h"

#include <folly/IPAddress.h>

#include <memory>
#include <unordered_map>

namespace facebook::fboss {

// Per-VRF routing tables (v4, v6, MPLS) plus the per-address-family
// unresolved-routes index consumed by the rollback path on FbossHwUpdateError.
struct VrfRouteTable {
  IPv4NetworkToRouteMap v4NetworkToRoute;
  IPv6NetworkToRouteMap v6NetworkToRoute;
  LabelToRouteMap labelToRoute;

  std::unordered_map<
      folly::CIDRNetworkV4,
      std::shared_ptr<Route<folly::IPAddressV4>>>
      unresolvedV4Routes;
  std::unordered_map<
      folly::CIDRNetworkV6,
      std::shared_ptr<Route<folly::IPAddressV6>>>
      unresolvedV6Routes;
  std::unordered_map<LabelID, std::shared_ptr<Route<LabelID>>>
      unresolvedMplsRoutes;

  bool operator==(const VrfRouteTable& other) const {
    return v4NetworkToRoute == other.v4NetworkToRoute &&
        v6NetworkToRoute == other.v6NetworkToRoute;
  }
  bool operator!=(const VrfRouteTable& other) const {
    return !(*this == other);
  }
  std::shared_ptr<Route<folly::IPAddressV4>> longestMatch(
      const folly::IPAddressV4& addr) const {
    auto it = v4NetworkToRoute.longestMatch(addr, addr.bitCount());
    return it == v4NetworkToRoute.end() ? nullptr : it->value();
  }
  std::shared_ptr<Route<folly::IPAddressV6>> longestMatch(
      const folly::IPAddressV6& addr) const {
    auto it = v6NetworkToRoute.longestMatch(addr, addr.bitCount());
    return it == v6NetworkToRoute.end() ? nullptr : it->value();
  }
  state::RouteTableFields toThrift() const;
  static VrfRouteTable fromThrift(const state::RouteTableFields&);
  state::RouteTableFields warmBootState() const;
};

} // namespace facebook::fboss
