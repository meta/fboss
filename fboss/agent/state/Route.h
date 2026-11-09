/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
// Copyright 2004-present Facebook.  All rights reserved.
#pragma once

#include <folly/IPAddress.h>

#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/state/NodeBase.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/state/RouteNextHopsMulti.h"
#include "fboss/agent/state/RouteTypes.h"
#include "fboss/agent/state/Thrifty.h"
#include "fboss/agent/types.h"

#include <boost/container/flat_set.hpp>

namespace facebook::fboss {

namespace {
/**
 * Bit definition for RouteFields<>::flags
 *
 * CONNECTED: This route is directly connected. For example, a route based on
 *            an interface subnet is a directly connected route.
 * RESOLVED: This route has been resolved. 'RouteFields::fwd' is valid.
 * UNRESOLVABLE: This route is not resolvable. A route without RESOLVED set
 *               does not mean that it is UNRESOLVABLE.
 *               A route with neither RESOLVED nor UNRESOLVABLE just means
 *               the route needs to be resolved. As the result of process,
 *               the route could have either RESOLVED set (resolved) or
 *               UNRESOLVABLE set.
 * PROCESSING: This route is being processed to resolve the nexthop.
 *             This bit is set before look up a route to reach the nexthop.
 *             If the route used to reach the nexthop also has this bit set,
 *             that means we have a loop to resolve the route. In this case,
 *             none of the routes in the loop is resolvable.
 *             This bit is cleared when setting this route as RESOLVED or
 *             UNRESOLVABLE.
 */
enum : uint32_t {
  CONNECTED = 0x1,
  RESOLVED = 0x2,
  UNRESOLVABLE = 0x4,
  PROCESSING = 0x8,
};
} // namespace

template <typename AddrT>
struct RouteFields;

template <typename AddrT>
using PrefixT = std::
    conditional_t<std::is_same_v<AddrT, LabelID>, Label, RoutePrefix<AddrT>>;

template <typename AddrT>
using ThriftFieldsT = std::conditional_t<
    std::is_same_v<AddrT, LabelID>,
    state::LabelForwardingEntryFields,
    state::RouteFields>;

template <typename AddrT>
struct RouteTraits {
  using AddrType = AddrT;
  using PrefixType = PrefixT<AddrT>;
  using ThriftFieldsType = ThriftFieldsT<AddrT>;
  using RouteFieldsType = RouteFields<AddrT>;
};

/**
 * RouteFields<> Class
 *
 * Temporarily need this class when using NodeMap to store the routes
 */
template <typename AddrT>
struct RouteFields
    : public ThriftyFields<RouteFields<AddrT>, ThriftFieldsT<AddrT>> {
  using Prefix = PrefixT<AddrT>;
  using ThriftFields = ThriftFieldsT<AddrT>;

  explicit RouteFields(const Prefix& prefix);
  RouteFields(
      const Prefix& prefix,
      ClientID clientId,
      const RouteNextHopEntry& entry)
      : RouteFields(prefix) {
    if (!entry.isValid(std::is_same_v<LabelID, AddrT>)) {
      throw FbossError("Invalid label forwarding action for IP route");
    }
    update(clientId, entry);
  }
  explicit RouteFields(const ThriftFields& fields) {
    this->writableData() = fields;
  }
  template <typename Fn>
  void forEachChild(Fn /*fn*/) {}
  bool operator==(const RouteFields& rf) const;

  RouteDetails toRouteDetails(
      const RouteNextHopSet& nhopSet,
      const std::optional<RouteNextHopSet>& normalizedNhopSet,
      const ClientNextHopsResolver& resolveClientNextHops) const;
  bool isHostRoute() const {
    if constexpr (
        std::is_same_v<folly::IPAddressV6, AddrT> ||
        std::is_same_v<folly::IPAddressV4, AddrT>) {
      return prefix().mask() == prefix().network().bitCount();
    } else {
      return false;
    }
  }

  bool hasNoEntry() const {
    return nexthopsmulti().isEmpty();
  }
  std::pair<ClientID, std::shared_ptr<const RouteNextHopEntry>> getBestEntry()
      const {
    return RouteNextHopsMulti::getBestEntry(*(this->data().nexthopsmulti()));
  }
  size_t numClientEntries() const {
    return nexthopsmulti().size();
  }
  std::optional<cfg::AclLookupClass> getClassID() const {
    return classID();
  }
  void setClassID(std::optional<cfg::AclLookupClass> c) {
    if (c) {
      this->writableData().classID() = c.value();
    } else {
      this->writableData().classID().reset();
    }
  }
  void delEntryForClient(ClientID clientId);
  std::shared_ptr<const RouteNextHopEntry> getEntryForClient(
      ClientID clientId) const {
    return RouteNextHopsMulti::getEntryForClient(
        clientId, *(this->data().nexthopsmulti()));
  }

  RouteNextHopsMulti getEntryForClients() const {
    return nexthopsmulti();
  }

  ThriftFields toThrift() const override {
    return this->data();
  }

  static RouteFields fromThrift(ThriftFields const& fields) {
    return RouteFields(fields);
  }

  void clearFlags() {
    this->writableData().flags() = 0;
  }

 private:
  void setFlagsProcessing() {
    setFlags(flags() | PROCESSING);
    setFlags(flags() & (~(RESOLVED | UNRESOLVABLE | CONNECTED)));
  }
  void setFlagsResolved() {
    setFlags(flags() | RESOLVED);
    setFlags(flags() & (~(UNRESOLVABLE | PROCESSING)));
  }
  void setFlagsUnresolvable() {
    setFlags(flags() | UNRESOLVABLE);
    setFlags(flags() & (~(RESOLVED | PROCESSING | CONNECTED)));
  }
  void setFlagsConnected() {
    setFlags(flags() | CONNECTED);
  }
  void clearForwardInFlags() {
    setFlags(flags() & (~(RESOLVED | PROCESSING | CONNECTED | UNRESOLVABLE)));
  }
  void setFlags(uint32_t flags) {
    this->writableData().flags() = flags;
  }

  // private constructor for thrift to fields
  RouteFields(
      const Prefix& argsPrefix,
      const RouteNextHopsMulti& argsNexthopsmulti,
      const RouteNextHopEntry& argsFwd,
      uint32_t argsFlags,
      std::optional<cfg::AclLookupClass> argsClassID)
      : RouteFields(getRouteFields(
            argsPrefix,
            argsNexthopsmulti,
            argsFwd,
            argsFlags,
            argsClassID)) {}

  static ThriftFields getRouteFields(
      const PrefixT<AddrT>& prefix,
      const RouteNextHopsMulti& multi =
          RouteNextHopsMulti(state::RouteNextHopsMulti{}),
      const RouteNextHopEntry& fwd =
          RouteNextHopEntry{
              RouteNextHopEntry::Action::DROP,
              AdminDistance::MAX_ADMIN_DISTANCE},
      uint32_t flags = 0,
      const std::optional<cfg::AclLookupClass>& classID = std::nullopt);

 public:
  std::string strLegacy() const;
  void update(ClientID clientId, const RouteNextHopEntry& entry);
  void updateClassID(std::optional<cfg::AclLookupClass> c) {
    setClassID(c);
  }
  bool has(ClientID clientId, const RouteNextHopEntry& entry) const;

  bool isResolved() const {
    return (flags() & RESOLVED);
  }
  bool isUnresolvable() const {
    return (flags() & UNRESOLVABLE);
  }
  bool isConnected() const {
    return (flags() & CONNECTED);
  }
  bool isDrop() const {
    return isResolved() &&
        RouteNextHopEntry::isAction(
               *(this->data().fwd()), RouteForwardAction::DROP);
  }
  bool isToCPU() const {
    return isResolved() &&
        RouteNextHopEntry::isAction(
               *(this->data().fwd()), RouteForwardAction::TO_CPU);
  }
  bool isProcessing() const {
    return (flags() & PROCESSING);
  }
  bool needResolve() const {
    // not resolved, nor unresolvable, nor in processing
    return !(flags() & (RESOLVED | UNRESOLVABLE | PROCESSING));
  }
  void setProcessing() {
    CHECK(!isProcessing());
    setFlagsProcessing();
  }
  void setResolved(const RouteNextHopEntry& f) {
    this->writableData().fwd() = f.toThrift();
    setFlagsResolved();
  }
  void setUnresolvable() {
    this->writableData().fwd() = state::RouteNextHopEntry{};
    setFlagsUnresolvable();
  }
  void setConnected() {
    setFlagsConnected();
  }
  void clearForward() {
    this->writableData().fwd() = state::RouteNextHopEntry{};
    clearForwardInFlags();
  }

  Prefix prefix() const {
    if constexpr (std::is_same_v<AddrT, LabelID>) {
      return Prefix::fromThrift(*(this->data().label()));
    } else {
      return Prefix::fromThrift(*(this->data().prefix()));
    }
  }
  RouteNextHopsMulti nexthopsmulti() const {
    return RouteNextHopsMulti(*(this->data().nexthopsmulti()));
  }
  RouteNextHopEntry fwd() const {
    return RouteNextHopEntry(*(this->data().fwd()));
  }
  uint32_t flags() const {
    return *(this->data().flags());
  }
  std::optional<cfg::AclLookupClass> classID() const {
    if (auto classID = this->data().classID()) {
      return *classID;
    }
    return std::nullopt;
  }
};

/// Route<> Class
template <typename AddrT>
class Route : public ThriftStructNode<Route<AddrT>, ThriftFieldsT<AddrT>> {
 public:
  using RouteBase = ThriftStructNode<Route<AddrT>, ThriftFieldsT<AddrT>>;
  using Prefix = PrefixT<AddrT>;
  using Action = RouteForwardAction;
  using Addr = AddrT;
  using LegacyFields = RouteFields<AddrT>;
  using RouteBase::modify;

  // Constructor for a route
  explicit Route(const Prefix& prefix) {
    if constexpr (!std::is_same_v<AddrT, LabelID>) {
      this->template set<switch_state_tags::prefix>(prefix.toThrift());
    } else {
      this->template set<switch_state_tags::label>(prefix.toThrift());
    }
    this->template set<switch_state_tags::fwd>(state::RouteNextHopEntry{});
    this->template set<switch_state_tags::flags>(0);
  }

  Route(
      const Prefix& prefix,
      ClientID clientId,
      const RouteNextHopEntry& entry) {
    if constexpr (!std::is_same_v<AddrT, LabelID>) {
      this->template set<switch_state_tags::prefix>(prefix.toThrift());
    } else {
      this->template set<switch_state_tags::label>(prefix.toThrift());
    }
    this->template set<switch_state_tags::fwd>(state::RouteNextHopEntry{});
    this->template set<switch_state_tags::flags>(0);
    auto nexthopsmulti =
        this->template safe_ref<switch_state_tags::nexthopsmulti>();
    nexthopsmulti->update(clientId, entry);
  }

  ~Route() = default;

  template <typename... Args>
  static ThriftFieldsT<AddrT> makeThrift(Args&&... args) {
    auto fields = LegacyFields(std::forward<Args>(args)...);
    return fields.toThrift();
  }

  // THRIFT_COPY
  RouteDetails toRouteDetails(
      const RouteNextHopSet& nhopSet,
      const std::optional<RouteNextHopSet>& normalizedNhopSet,
      const ClientNextHopsResolver& resolveClientNextHops) const;

  /*
   * clone and clear all forwarding info. Forwarding info will be recomputed
   * via route resolution
   */
  std::shared_ptr<Route<AddrT>> cloneForReresolve() const;

  static void modify(std::shared_ptr<SwitchState>* state);

  // THRIFT_COPY
  Prefix prefix() const {
    if constexpr (!std::is_same_v<AddrT, LabelID>) {
      return Prefix::fromThrift(
          this->template safe_cref<switch_state_tags::prefix>()->toThrift());
    } else {
      return Prefix::fromThrift(
          this->template safe_cref<switch_state_tags::label>()->toThrift());
    }
  }
  auto getID() const {
    if constexpr (std::is_same_v<AddrT, LabelID>) {
      return prefix().value();
    } else {
      return prefix().str();
    }
  }
  uint32_t flags() const {
    return this->template cref<switch_state_tags::flags>()->cref();
  }
  bool isResolved() const {
    return flags() & RESOLVED;
  }
  bool isUnresolvable() const {
    return flags() & UNRESOLVABLE;
  }
  bool isConnected() const {
    return flags() & CONNECTED;
  }
  bool isHostRoute() const {
    if constexpr (
        std::is_same_v<folly::IPAddressV6, AddrT> ||
        std::is_same_v<folly::IPAddressV4, AddrT>) {
      return prefix().mask() == prefix().network().bitCount();
    } else {
      return false;
    }
  }
  bool isDrop() const {
    return isResolved() &&
        this->template cref<switch_state_tags::fwd>()->isDrop();
  }
  bool isToCPU() const {
    return isResolved() &&
        this->template cref<switch_state_tags::fwd>()->isToCPU();
  }
  bool isProcessing() const {
    return (flags() & PROCESSING);
  }
  bool needResolve() const {
    // not resolved, nor unresolvable, nor in processing
    return !(flags() & (RESOLVED | UNRESOLVABLE | PROCESSING));
  }

  std::string str() const {
    if constexpr (std::is_same_v<AddrT, LabelID>) {
      return folly::to<std::string>(prefix().value());
    } else {
      return prefix().str();
    }
  }

  // THRIFT_COPY
  std::string str_DEPRACATED() const {
    auto fields = RouteFields<AddrT>(this->toThrift());
    return fields.str();
  }
  // Return the forwarding info for this route
  const RouteNextHopEntry& getForwardInfo() const {
    auto entry = this->template safe_cref<switch_state_tags::fwd>();
    return *entry;
  }
  std::shared_ptr<const RouteNextHopEntry> getEntryForClient(
      ClientID clientId) const {
    auto multi = this->template safe_cref<switch_state_tags::nexthopsmulti>();
    return multi->getEntryForClient(clientId);
  }
  std::pair<ClientID, std::shared_ptr<const RouteNextHopEntry>> getBestEntry()
      const {
    auto multi = this->template safe_cref<switch_state_tags::nexthopsmulti>();
    return multi->getBestEntry();
  }
  bool hasNoEntry() const {
    auto multi = this->template safe_cref<switch_state_tags::nexthopsmulti>();
    return multi->isEmpty();
  }

  size_t numClientEntries() const {
    auto multi = this->template safe_cref<switch_state_tags::nexthopsmulti>();
    return multi->size();
  }

  bool has(ClientID clientId, const RouteNextHopEntry& entry) const {
    auto multi = this->template safe_cref<switch_state_tags::nexthopsmulti>();
    auto found = multi->getEntryForClient(clientId);
    return found && *found == entry;
  }

  void clearFlags() {
    this->template set<switch_state_tags::flags>(0);
  }

  bool isSame(const Route* rt) const;

  /*
   * The following functions modify the route object.
   * They should only be called on unpublished objects which are only visible
   * to a single thread
   */
  void setProcessing() {
    CHECK(!isProcessing());
    setFlags(flags() | PROCESSING);
    setFlags(flags() & (~(RESOLVED | UNRESOLVABLE | CONNECTED)));
  }
  void setConnected() {
    setFlags(flags() | CONNECTED);
  }
  // THRIFT_COPY
  void setResolved(const RouteNextHopEntry& fwd) {
    this->template set<switch_state_tags::fwd>(fwd.toThrift());
    setFlags(flags() | RESOLVED);
    setFlags(flags() & (~(UNRESOLVABLE | PROCESSING)));
  }
  void setUnresolvable() {
    setFlags(flags() | UNRESOLVABLE);
    setFlags(flags() & (~(RESOLVED | PROCESSING | CONNECTED)));
  }
  void clearForward() {
    this->template set<switch_state_tags::fwd>(state::RouteNextHopEntry{});
    setFlags(flags() & (~(RESOLVED | PROCESSING | CONNECTED | UNRESOLVABLE)));
  }

  void update(ClientID clientId, const RouteNextHopEntry& entry) {
    if (this->template cref<switch_state_tags::nexthopsmulti>()
            ->isPublished()) {
      auto node =
          this->template cref<switch_state_tags::nexthopsmulti>()->clone();
      this->template ref<switch_state_tags::nexthopsmulti>() = node;
    }
    this->template ref<switch_state_tags::nexthopsmulti>()->update(
        clientId, entry);
  }

  void updateClassID(std::optional<cfg::AclLookupClass> classID) {
    if (!classID) {
      this->template ref<switch_state_tags::classID>().reset();
    } else {
      this->template set<switch_state_tags::classID>(*classID);
    }
  }

  std::optional<cfg::AclLookupClass> getClassID() const {
    auto classID = this->template safe_cref<switch_state_tags::classID>();
    if (!classID) {
      return std::nullopt;
    }
    return classID->cref();
  }
  void delEntryForClient(ClientID clientId) {
    if (this->template cref<switch_state_tags::nexthopsmulti>()
            ->isPublished()) {
      auto node =
          this->template cref<switch_state_tags::nexthopsmulti>()->clone();
      this->template ref<switch_state_tags::nexthopsmulti>() = node;
    }
    this->template ref<switch_state_tags::nexthopsmulti>()->delEntryForClient(
        clientId);
  }

  const RouteNextHopsMulti& getEntryForClients() const {
    auto multi = this->template safe_cref<switch_state_tags::nexthopsmulti>();
    return *multi;
  }

  size_t numClientsForNamedNhg(const std::string& nhgName) const {
    size_t count = 0;
    for (const auto& [cid, entry] : getEntryForClients()) {
      if (entry->getNamedNextHopGroup() == nhgName) {
        ++count;
      }
    }
    return count;
  }

  // Takes resolved nexthops; this node cannot resolve a nexthop set ID.
  static bool isPopAndLookup(const RouteNextHopSet& nexthops) {
    if (nexthops.size() == 1) {
      // there must be exactly one next hop for POP_AND_LOOKUP action
      return nexthops.begin()->isPopAndLookup();
    }
    return false;
  }

 private:
  void setFlags(uint32_t flags) {
    this->template ref<switch_state_tags::flags>() = flags;
  }
  // no copy or assign operator
  Route(const Route&) = delete;
  Route& operator=(const Route&) = delete;
  Route(Route&&) = delete;
  Route& operator=(Route&&) = delete;

  // Inherit the constructors required for clone()
  using RouteBase::RouteBase;
  friend class CloneAllocator;
};

using RouteV4 = Route<folly::IPAddressV4>;
using RouteV6 = Route<folly::IPAddressV6>;
using RouteMpls = Route<LabelID>;

template <>
struct IsThriftCowNode<Route<folly::IPAddressV4>> {
  static constexpr bool value = true;
};
template <>
struct IsThriftCowNode<Route<folly::IPAddressV6>> {
  static constexpr bool value = true;
};

template <>
struct IsThriftCowNode<Route<LabelID>> {
  static constexpr bool value = true;
};
} // namespace facebook::fboss
