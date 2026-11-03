/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/state/RouteNextHop.h"

#include <folly/Conv.h>

#include "fboss/agent/FbossError.h"
#include "fboss/agent/if/gen-cpp2/mpls_types.h"
#include "folly/IPAddress.h"

namespace {

void validateSrv6Fields(
    const std::vector<folly::IPAddressV6>& srv6SegmentList,
    const std::optional<facebook::fboss::TunnelType>& tunnelType,
    const std::optional<std::string>& tunnelId) {
  if (!srv6SegmentList.empty()) {
    if (!tunnelType.has_value()) {
      throw facebook::fboss::FbossError(
          "tunnelType must be set when srv6SegmentList is not empty");
    }
    if (tunnelType.value() != facebook::fboss::TunnelType::SRV6_ENCAP) {
      throw facebook::fboss::FbossError(
          "tunnelType must be SRV6_ENCAP when srv6SegmentList is not empty");
    }
    if (!tunnelId.has_value() || tunnelId.value().empty()) {
      throw facebook::fboss::FbossError(
          "tunnelId must be set and non-empty when srv6SegmentList is not empty");
    }
  }
}

void validateOverlayDstMac(
    const std::optional<folly::MacAddress>& overlayDstMac,
    const std::optional<facebook::fboss::TunnelType>& tunnelType,
    const std::optional<std::string>& tunnelId) {
  if (overlayDstMac.has_value()) {
    if (!tunnelType.has_value() ||
        tunnelType.value() != facebook::fboss::TunnelType::VXLAN) {
      throw facebook::fboss::FbossError(
          "tunnelType must be VXLAN when overlayDstMac is set");
    }
    if (!tunnelId.has_value() || tunnelId.value().empty()) {
      throw facebook::fboss::FbossError(
          "tunnelId must be set and non-empty when overlayDstMac is set");
    }
  }
}

} // namespace

namespace facebook::fboss {

namespace util {
NextHop fromThrift(const NextHopThrift& nht, bool allowV6NonLinkLocal) {
  std::optional<LabelForwardingAction> action = std::nullopt;
  if (nht.mplsAction()) {
    action = LabelForwardingAction::fromThrift(nht.mplsAction().value_or({}));
  }
  std::optional<bool> disableTTLDecrement = std::nullopt;
  if (nht.disableTTLDecrement()) {
    disableTTLDecrement = *nht.disableTTLDecrement();
  }
  std::optional<NextHopWeight> adjustedWeight = std::nullopt;
  if (nht.adjustedWeight()) {
    adjustedWeight = *nht.adjustedWeight();
  }
  std::optional<NetworkTopologyInformation> topologyInfo = std::nullopt;
  if (nht.topologyInfo()) {
    topologyInfo = *nht.topologyInfo();
  }
  std::optional<TunnelType> tunnelType = std::nullopt;
  if (nht.tunnelType()) {
    tunnelType = *nht.tunnelType();
  }
  std::optional<std::string> tunnelId = std::nullopt;
  if (nht.tunnelId()) {
    tunnelId = *nht.tunnelId();
  }
  std::vector<folly::IPAddressV6> srv6SegmentList;
  for (const auto& binAddr : *nht.srv6SegmentList()) {
    srv6SegmentList.push_back(network::toIPAddress(binAddr).asV6());
  }
  std::optional<int64_t> cost = std::nullopt;
  if (nht.cost()) {
    cost = *nht.cost();
  }
  std::optional<folly::MacAddress> overlayDstMac = std::nullopt;
  if (nht.overlayDstMac()) {
    overlayDstMac = folly::MacAddress(*nht.overlayDstMac());
  }
  auto role = *nht.role();

  auto address = network::toIPAddress(*nht.address());
  NextHopWeight weight = static_cast<NextHopWeight>(*nht.weight());
  bool v6LinkLocal = address.isV6() && address.isLinkLocal();
  // Only honor interface specified over thrift if the address
  // is a v6 link-local. Otherwise, consume it as an unresolved
  // next hop and let route resolution populate the interface.
  if (apache::thrift::get_pointer(nht.address()->ifName()) &&
      (v6LinkLocal || allowV6NonLinkLocal)) {
    InterfaceID intfID = utility::getIDFromTunIntfName(
        *(apache::thrift::get_pointer(nht.address()->ifName())));
    return ResolvedNextHop(
        std::move(address),
        intfID,
        weight,
        action,
        disableTTLDecrement,
        topologyInfo,
        adjustedWeight,
        std::move(srv6SegmentList),
        tunnelType,
        tunnelId,
        cost,
        role,
        overlayDstMac);
  } else {
    return UnresolvedNextHop(
        std::move(address),
        weight,
        action,
        disableTTLDecrement,
        topologyInfo,
        adjustedWeight,
        std::move(srv6SegmentList),
        tunnelType,
        tunnelId,
        cost,
        role,
        overlayDstMac);
  }
}

NextHop nextHopFromFollyDynamic(const folly::dynamic& nhopJson) {
  folly::IPAddress address(nhopJson[kNexthop()].stringPiece());
  auto it = nhopJson.find(kInterface());
  std::optional<LabelForwardingAction> action = std::nullopt;
  auto labelAction = nhopJson.find(kLabelForwardingAction());
  if (labelAction != nhopJson.items().end()) {
    action = LabelForwardingAction::fromFollyDynamic(labelAction->second);
  }
  // NOTE: we use the ECMP weight (0) as the default here for proper
  // forward/backward compatibility. A quick explanation of the cases:
  // 1) Warm boot from agent with no notion of weight to one with a notion of
  // weight:
  //   All routes will come back from the warm boot file with a default weight
  //   of 0, which matches the fact that everything is ECMP. The first config
  //   application will result in interface/static routes getting a UCMP
  //   compatible weight of 1 (UCMP_DEFAULT_WEIGHT)
  // 2) Warm boot from agent with notion of weight back to one without a notion
  // of weight:
  //   All ECMP routes will be read in by code that is unaware of weight
  //   and will stay ECMP. Routes with UCMP weights will revert to ECMP
  //   at the SwitchState level seamlessly. TODO: test what happens to
  //   programmed UCMP routes in this case
  NextHopWeight weight(ECMP_WEIGHT);
  auto weightItr = nhopJson.find(kWeight);
  if (weightItr != nhopJson.items().end()) {
    weight = weightItr->second.asInt();
  }
  if (it != nhopJson.items().end()) {
    int64_t stored = it->second.asInt();
    if (stored < 0 || stored > std::numeric_limits<uint32_t>::max()) {
      throw FbossError("stored InterfaceID exceeds uint32_t limit");
    }
    InterfaceID intfID = InterfaceID(it->second.asInt());
    return ResolvedNextHop(std::move(address), intfID, weight, action);
  } else {
    return UnresolvedNextHop(std::move(address), weight, action);
  }
}
} // namespace util

void toAppend(const NextHop& nhop, std::string* result) {
  folly::toAppend(nhop.str(), result);
}

std::ostream& operator<<(std::ostream& os, const NextHop& nhop) {
  return os << nhop.str();
}

bool operator<(const NextHop& a, const NextHop& b) {
  if (a.intfID() != b.intfID()) {
    return a.intfID() < b.intfID();
  } else if (a.addr() != b.addr()) {
    return a.addr() < b.addr();
  } else if (a.labelForwardingAction() != b.labelForwardingAction()) {
    return a.labelForwardingAction() < b.labelForwardingAction();
  } else if (a.weight() != b.weight()) {
    return a.weight() < b.weight();
  } else if (a.disableTTLDecrement() != b.disableTTLDecrement()) {
    return a.disableTTLDecrement() < b.disableTTLDecrement();
  } else if (a.topologyInfo() != b.topologyInfo()) {
    return a.topologyInfo() < b.topologyInfo();
  } else if (a.adjustedWeight() != b.adjustedWeight()) {
    return a.adjustedWeight() < b.adjustedWeight();
  } else if (a.srv6SegmentList() != b.srv6SegmentList()) {
    return a.srv6SegmentList() < b.srv6SegmentList();
  } else if (a.tunnelType() != b.tunnelType()) {
    return a.tunnelType() < b.tunnelType();
  } else if (a.tunnelId() != b.tunnelId()) {
    return a.tunnelId() < b.tunnelId();
  } else if (a.overlayDstMac() != b.overlayDstMac()) {
    return a.overlayDstMac() < b.overlayDstMac();
  } else if (a.cost() != b.cost()) {
    return a.cost() < b.cost();
  } else {
    return a.role() < b.role();
  }
}

bool operator>(const NextHop& a, const NextHop& b) {
  return (b < a);
}

bool operator<=(const NextHop& a, const NextHop& b) {
  return !(b < a);
}

bool operator>=(const NextHop& a, const NextHop& b) {
  return !(a < b);
}

bool operator==(const NextHop& a, const NextHop& b) {
  return (
      a.intfID() == b.intfID() && a.addr() == b.addr() &&
      a.weight() == b.weight() &&
      a.labelForwardingAction() == b.labelForwardingAction() &&
      a.disableTTLDecrement() == b.disableTTLDecrement() &&
      a.adjustedWeight() == b.adjustedWeight() &&
      a.topologyInfo() == b.topologyInfo() &&
      a.srv6SegmentList() == b.srv6SegmentList() &&
      a.tunnelType() == b.tunnelType() && a.tunnelId() == b.tunnelId() &&
      a.overlayDstMac() == b.overlayDstMac() && a.cost() == b.cost() &&
      a.role() == b.role());
}

bool operator!=(const NextHop& a, const NextHop& b) {
  return !(a == b);
}

ResolvedNextHop::ResolvedNextHop(
    const folly::IPAddress& addr,
    InterfaceID intfID,
    const NextHopWeight& weight,
    const std::optional<LabelForwardingAction>& action,
    const std::optional<bool>& disableTTLDecrement,
    const std::optional<NetworkTopologyInformation>& topologyInfo,
    const std::optional<NextHopWeight>& adjustedWeight,
    const std::vector<folly::IPAddressV6>& srv6SegmentList,
    const std::optional<TunnelType>& tunnelType,
    const std::optional<std::string>& tunnelId,
    const std::optional<int64_t>& cost,
    NextHopRole role,
    const std::optional<folly::MacAddress>& overlayDstMac)
    : addr_(addr),
      intfID_(intfID),
      weight_(weight),
      labelForwardingAction_(action),
      disableTTLDecrement_(disableTTLDecrement),
      topologyInfo_(topologyInfo),
      adjustedWeight_(adjustedWeight),
      srv6SegmentList_(srv6SegmentList),
      tunnelType_(tunnelType),
      tunnelId_(tunnelId),
      cost_(cost),
      role_(role),
      overlayDstMac_(overlayDstMac) {
  validateSrv6Fields(srv6SegmentList_, tunnelType_, tunnelId_);
  validateOverlayDstMac(overlayDstMac_, tunnelType_, tunnelId_);
}

ResolvedNextHop::ResolvedNextHop(
    folly::IPAddress&& addr,
    InterfaceID intfID,
    const NextHopWeight& weight,
    std::optional<LabelForwardingAction>&& action,
    std::optional<bool>&& disableTTLDecrement,
    const std::optional<NetworkTopologyInformation>&& topologyInfo,
    const std::optional<NextHopWeight>& adjustedWeight,
    std::vector<folly::IPAddressV6>&& srv6SegmentList,
    std::optional<TunnelType>&& tunnelType,
    std::optional<std::string>&& tunnelId,
    std::optional<int64_t>&& cost,
    NextHopRole role,
    std::optional<folly::MacAddress>&& overlayDstMac)
    : addr_(std::move(addr)),
      intfID_(intfID),
      weight_(weight),
      labelForwardingAction_(std::move(action)),
      disableTTLDecrement_(disableTTLDecrement),
      topologyInfo_(topologyInfo),
      adjustedWeight_(adjustedWeight),
      srv6SegmentList_(std::move(srv6SegmentList)),
      tunnelType_(std::move(tunnelType)),
      tunnelId_(std::move(tunnelId)),
      cost_(std::move(cost)),
      role_(role),
      overlayDstMac_(std::move(overlayDstMac)) {
  validateSrv6Fields(srv6SegmentList_, tunnelType_, tunnelId_);
  validateOverlayDstMac(overlayDstMac_, tunnelType_, tunnelId_);
}

UnresolvedNextHop::UnresolvedNextHop(
    const folly::IPAddress& addr,
    const NextHopWeight& weight,
    const std::optional<LabelForwardingAction>& action,
    const std::optional<bool>& disableTTLDecrement,
    const std::optional<NetworkTopologyInformation>& topologyInfo,
    const std::optional<NextHopWeight>& adjustedWeight,
    const std::vector<folly::IPAddressV6>& srv6SegmentList,
    const std::optional<TunnelType>& tunnelType,
    const std::optional<std::string>& tunnelId,
    const std::optional<int64_t>& cost,
    NextHopRole role,
    const std::optional<folly::MacAddress>& overlayDstMac)
    : addr_(addr),
      weight_(weight),
      labelForwardingAction_(action),
      disableTTLDecrement_(disableTTLDecrement),
      topologyInfo_(topologyInfo),
      adjustedWeight_(adjustedWeight),
      srv6SegmentList_(srv6SegmentList),
      tunnelType_(tunnelType),
      tunnelId_(tunnelId),
      cost_(cost),
      role_(role),
      overlayDstMac_(overlayDstMac) {
  if (addr_.isV6() && addr_.isLinkLocal()) {
    throw FbossError(
        "Missing interface scoping for link-local nexthop ", addr.str());
  }
  validateSrv6Fields(srv6SegmentList_, tunnelType_, tunnelId_);
  validateOverlayDstMac(overlayDstMac_, tunnelType_, tunnelId_);
}

UnresolvedNextHop::UnresolvedNextHop(
    folly::IPAddress&& addr,
    const NextHopWeight& weight,
    std::optional<LabelForwardingAction>&& action,
    std::optional<bool>&& disableTTLDecrement,
    const std::optional<NetworkTopologyInformation>&& topologyInfo,
    const std::optional<NextHopWeight>& adjustedWeight,
    std::vector<folly::IPAddressV6>&& srv6SegmentList,
    std::optional<TunnelType>&& tunnelType,
    std::optional<std::string>&& tunnelId,
    std::optional<int64_t>&& cost,
    NextHopRole role,
    std::optional<folly::MacAddress>&& overlayDstMac)
    : addr_(std::move(addr)),
      weight_(weight),
      labelForwardingAction_(std::move(action)),
      disableTTLDecrement_(disableTTLDecrement),
      topologyInfo_(topologyInfo),
      adjustedWeight_(adjustedWeight),
      srv6SegmentList_(std::move(srv6SegmentList)),
      tunnelType_(std::move(tunnelType)),
      tunnelId_(std::move(tunnelId)),
      cost_(std::move(cost)),
      role_(role),
      overlayDstMac_(std::move(overlayDstMac)) {
  if (addr_.isV6() && addr_.isLinkLocal()) {
    throw FbossError(
        "Missing interface scoping for link-local nexthop ", addr_.str());
  }
  validateSrv6Fields(srv6SegmentList_, tunnelType_, tunnelId_);
  validateOverlayDstMac(overlayDstMac_, tunnelType_, tunnelId_);
}

bool operator==(const ResolvedNextHop& a, const ResolvedNextHop& b) {
  return (
      a.intfID() == b.intfID() && a.addr() == b.addr() &&
      a.weight() == b.weight() &&
      a.labelForwardingAction() == b.labelForwardingAction() &&
      a.disableTTLDecrement() == b.disableTTLDecrement() &&
      a.adjustedWeight() == b.adjustedWeight() &&
      a.topologyInfo() == b.topologyInfo() &&
      a.srv6SegmentList() == b.srv6SegmentList() &&
      a.tunnelType() == b.tunnelType() && a.tunnelId() == b.tunnelId() &&
      a.overlayDstMac() == b.overlayDstMac() && a.cost() == b.cost() &&
      a.role() == b.role());
}

bool operator==(const UnresolvedNextHop& a, const UnresolvedNextHop& b) {
  return (
      a.addr() == b.addr() && a.weight() == b.weight() &&
      a.labelForwardingAction() == b.labelForwardingAction() &&
      a.disableTTLDecrement() == b.disableTTLDecrement() &&
      a.adjustedWeight() == b.adjustedWeight() &&
      a.topologyInfo() == b.topologyInfo() &&
      a.srv6SegmentList() == b.srv6SegmentList() &&
      a.tunnelType() == b.tunnelType() && a.tunnelId() == b.tunnelId() &&
      a.overlayDstMac() == b.overlayDstMac() && a.cost() == b.cost() &&
      a.role() == b.role());
}

} // namespace facebook::fboss
