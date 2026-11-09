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
#include "fboss/agent/state/Route.h"

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/agent/state/NodeBase-defs.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "folly/IPAddressV4.h"

using facebook::network::toBinaryAddress;

namespace facebook::fboss {

using folly::IPAddress;
using std::string;

// RouteFields<> Class
template <typename AddrT>
RouteFields<AddrT>::RouteFields(const Prefix& prefix)
    : RouteFields(getRouteFields(prefix)) {}

template <typename AddrT>
bool RouteFields<AddrT>::operator==(const RouteFields& rf) const {
  return this->data() == rf.data();
}

template <typename AddrT>
RouteDetails RouteFields<AddrT>::toRouteDetails(
    const RouteNextHopSet& nhopSet,
    const std::optional<RouteNextHopSet>& normalizedNhopSet,
    const ClientNextHopsResolver& resolveClientNextHops) const {
  RouteDetails rd;
  if constexpr (
      std::is_same_v<folly::IPAddressV6, AddrT> ||
      std::is_same_v<folly::IPAddressV4, AddrT>) {
    // Add the prefix
    rd.dest()->ip() = toBinaryAddress(prefix().network());
    rd.dest()->prefixLength() = prefix().mask();
  }
  // Add the action
  rd.action() = forwardActionStr(fwd().getAction());
  // Add the forwarding info
  auto fillNextHops = [](const auto& nhopSet, auto& rdetails) {
    rdetails.fwdInfo()->clear();
    std::vector<NextHopThrift> nhops;
    for (const auto& nh : nhopSet) {
      IfAndIP ifAndIp;
      if (nh.intfID().has_value()) {
        *ifAndIp.interfaceID() = nh.intf();
      } else if (nh.resolvedInNextLevel()) {
        ifAndIp.resolvedInNextLevel() = true;
      }
      *ifAndIp.ip() = toBinaryAddress(nh.addr());
      if (auto vrf = nh.vrf()) {
        ifAndIp.vrf() = static_cast<int32_t>(*vrf);
      }
      rdetails.fwdInfo()->push_back(ifAndIp);
      nhops.push_back(nh.toThrift());
    }
    return nhops;
  };
  rd.nextHops() = fillNextHops(nhopSet, rd);

  // Add the multi-nexthops
  auto bestEntry = getBestEntry();
  rd.nextHopMulti() =
      nexthopsmulti().toThriftLegacy(bestEntry.first, resolveClientNextHops);
  rd.isConnected() = isConnected();
  // add counter id
  if (fwd().getCounterID().has_value()) {
    rd.counterID() = *fwd().getCounterID();
  }
  // add class id
  if (getClassID().has_value()) {
    rd.classID() = *getClassID();
  }
  if (isResolved() && fwd().getOverrideEcmpSwitchingMode().has_value()) {
    rd.overridenEcmpMode() = *fwd().getOverrideEcmpSwitchingMode();
  }
  if (isResolved() && fwd().getOverrideNextHops().has_value()) {
    auto nhops = normalizedNhopSet.has_value() ? *normalizedNhopSet
                                               : *fwd().getOverrideNextHops();
    rd.overridenNextHops() = fillNextHops(nhops, rd);
  }
  // Add nexthop IDs if present
  if (auto setId = fwd().getResolvedNextHopSetID()) {
    rd.resolvedNextHopSetID() = static_cast<int64_t>(*setId);
  }
  if (auto setId = fwd().getNormalizedResolvedNextHopSetID()) {
    rd.normalizedResolvedNextHopSetID() = static_cast<int64_t>(*setId);
  }
  if (bestEntry.second) {
    rd.adminDistance() = bestEntry.second->getAdminDistance();
    auto nhgName = bestEntry.second->getNamedNextHopGroup();
    if (nhgName.has_value()) {
      NamedRouteDestination namedDest;
      namedDest.nextHopGroup() = *nhgName;
      rd.namedRouteDestination() = namedDest;
    }
  }
  return rd;
}

template <typename AddrT>
void RouteFields<AddrT>::update(
    ClientID clientId,
    const RouteNextHopEntry& entry) {
  this->writableData().fwd() = state::RouteNextHopEntry{};
  RouteNextHopsMulti::update(
      clientId, *(this->writableData().nexthopsmulti()), entry.toThrift());
}
template <typename AddrT>

bool RouteFields<AddrT>::has(ClientID clientId, const RouteNextHopEntry& entry)
    const {
  auto found = RouteNextHopsMulti::getEntryForClient(
      clientId, *(this->data().nexthopsmulti()));
  return found && *found == entry;
}

template <typename AddrT>
std::string RouteFields<AddrT>::strLegacy() const {
  std::string ret;
  ret = folly::to<string>(prefix(), '@');
  ret.append(nexthopsmulti().strLegacy());
  ret.append(" State:");
  if (isConnected()) {
    ret.append("C");
  }
  if (isResolved()) {
    ret.append("R");
  }
  if (isUnresolvable()) {
    ret.append("U");
  }
  if (isProcessing()) {
    ret.append("P");
  }
  ret.append(", => ");
  ret.append(fwd().str());

  auto _classID = classID();
  auto classIDStr = _classID.has_value()
      ? folly::to<std::string>(static_cast<int>(_classID.value()))
      : "None";
  ret.append(", classID: ");
  ret.append(classIDStr);

  return ret;
}

template <typename AddrT>
void RouteFields<AddrT>::delEntryForClient(ClientID clientId) {
  RouteNextHopsMulti::delEntryForClient(
      clientId, *(this->writableData().nexthopsmulti()));
}

template <typename AddrT>
ThriftFieldsT<AddrT> RouteFields<AddrT>::getRouteFields(
    const PrefixT<AddrT>& prefix,
    const RouteNextHopsMulti& multi,
    const RouteNextHopEntry& fwd,
    uint32_t flags,
    const std::optional<cfg::AclLookupClass>& classID) {
  ThriftFieldsT<AddrT> fields{};
  if constexpr (std::is_same_v<AddrT, LabelID>) {
    fields.label() = prefix.toThrift();
  } else {
    fields.prefix() = prefix.toThrift();
  }
  fields.nexthopsmulti() = multi.toThrift();
  fields.fwd() = fwd.toThrift();
  fields.flags() = flags;
  if (classID) {
    fields.classID() = *classID;
  }
  return fields;
}

template struct RouteFields<folly::IPAddressV4>;
template struct RouteFields<folly::IPAddressV6>;
template struct RouteFields<LabelID>;

template <typename AddrT>
RouteDetails Route<AddrT>::toRouteDetails(
    const RouteNextHopSet& nhopSet,
    const std::optional<RouteNextHopSet>& normalizedNhopSet,
    const ClientNextHopsResolver& resolveClientNextHops) const {
  RouteFields<AddrT> fields{this->toThrift()};
  return fields.toRouteDetails(
      nhopSet, normalizedNhopSet, resolveClientNextHops);
}

template <typename AddrT>
bool Route<AddrT>::isSame(const Route<AddrT>* rt) const {
  return *this == *rt;
}

template <typename AddrT>
std::shared_ptr<Route<AddrT>> Route<AddrT>::cloneForReresolve() const {
  auto unresolvedRoute = this->clone();

  unresolvedRoute->clearFlags();
  unresolvedRoute->clearForward();
  return unresolvedRoute;
}

template class Route<folly::IPAddressV4>;
template class Route<folly::IPAddressV6>;
template class Route<LabelID>;

} // namespace facebook::fboss
