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

#include <functional>

#include <folly/FBString.h>
#include <folly/IPAddress.h>
#include <folly/json/dynamic.h>

#include <boost/container/flat_map.hpp>

#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/state/Thrifty.h"
#include "fboss/agent/types.h"

namespace facebook::fboss {

/**
 * Class relationship:
 *
 * RouteNextHopsMulti is a map from client ID to RouteNextHopEntry.
 *
 * There are two types of RouteNextHopEntry in general.
 * 1) An entry with multiple nexthop IPs (RouteNextHopSet), OR
 * 2) no nexthop IPs, but with pre-defined forwarding action
 *    (i.e. ToCPU or DROP)
 * Each RouteNextHopEntry also has other common attribute, like admin distance.
 *
 * RouteNextHopSet is a set of RouteNextHop.
 *
 * RouteNextHop contains the nexthop IP address. In case of the v6 link-local
 * IP address, the interface ID is also included in this class.
 *
 */

using ClientToNHopMap = std::map<ClientID, state::RouteNextHopEntry>;

USE_THRIFT_COW(RouteNextHopsMulti);

template <>
struct thrift_cow::ThriftStructResolver<state::RouteNextHopsMulti> {
  using type = RouteNextHopsMulti;
};

/**
 * Map form clientId -> RouteNextHopEntry
 */
// Resolves a per-client entry's nexthops. RouteNextHopsMulti is a state-layer
// node with no access to SwitchState or NextHopIDManager, so callers that have
// one supply an ID-aware resolver (getClientNextHops /
// getClientNextHopsFromRib). Required, not defaulted: a caller that silently
// fell back to inline storage would emit empty nexthops once inline is removed.
using ClientNextHopsResolver =
    std::function<RouteNextHopSet(const RouteNextHopEntry&)>;

class RouteNextHopsMulti
    : public thrift_cow::ThriftStructNode<state::RouteNextHopsMulti> {
 private:
  auto map() {
    return this->safe_ref<switch_state_tags::client2NextHopEntry>();
  }

  auto map() const {
    return this->safe_cref<switch_state_tags::client2NextHopEntry>();
  }

 protected:
  ClientID findLowestAdminDistance();

 public:
  using Base = thrift_cow::ThriftStructNode<state::RouteNextHopsMulti>;
  using Base::Base;

  std::vector<ClientAndNextHops> toThriftLegacy(
      std::optional<ClientID> preferredClient,
      const ClientNextHopsResolver& resolveNextHops) const;

  std::string strLegacy() const;
  void update(ClientID clientid, const RouteNextHopEntry& nhe);

  void clear() {
    auto clientToNHopMap = map();
    clientToNHopMap->fromThrift(ClientToNHopMap{});
  }

  // THRIFT_COPY
  bool operator==(const RouteNextHopsMulti& p2) const {
    return map()->toThrift() == p2.map()->toThrift();
  }

  auto begin() const {
    return map()->begin();
  }

  auto end() const {
    return map()->end();
  }

  bool isEmpty() const {
    // The code disallows adding/updating an empty nextHops list. So if the
    // map contains any entries, they are non-zero-length lists.
    return begin() == end();
  }

  size_t size() const {
    return map()->size();
  }
  void delEntryForClient(ClientID clientId);

  ClientID lowestAdminDistanceClientId() const {
    return cref<switch_state_tags::lowestAdminDistanceClientId>()->cref();
  }

  void setLowestAdminDistanceClientId(ClientID id) {
    set<switch_state_tags::lowestAdminDistanceClientId>(id);
  }

  std::shared_ptr<const RouteNextHopEntry> getEntryForClient(
      ClientID clientId) const;

  std::pair<ClientID, std::shared_ptr<const RouteNextHopEntry>> getBestEntry()
      const;

  bool isSame(ClientID clientId, const RouteNextHopEntry& nhe) const;

  static std::pair<ClientID, std::shared_ptr<const RouteNextHopEntry>>
  getBestEntry(const state::RouteNextHopsMulti& nexthopsmulti);

  static std::shared_ptr<const RouteNextHopEntry> getEntryForClient(
      ClientID clientId,
      const state::RouteNextHopsMulti& nexthopsmulti);

  static void update(
      ClientID clientId,
      state::RouteNextHopsMulti& nexthopsmulti,
      state::RouteNextHopEntry nhe);

  static ClientID findLowestAdminDistance(
      const state::RouteNextHopsMulti& nexthopsmulti);

  static void delEntryForClient(
      ClientID clientId,
      state::RouteNextHopsMulti& nexthopsmulti);
};

} // namespace facebook::fboss
