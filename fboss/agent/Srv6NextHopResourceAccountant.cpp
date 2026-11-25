// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/Srv6NextHopResourceAccountant.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/state/SwitchState.h"

#include <folly/logging/xlog.h>

#include <set>

namespace facebook::fboss {

namespace {
constexpr auto kHundredPercentage = 100;

bool hasBackupNextHop(const RouteNextHopSet& nhops) {
  return std::any_of(nhops.begin(), nhops.end(), [](const auto& nhop) {
    return nhop.role() == NextHopRole::BACKUP;
  });
}
} // namespace

Srv6NextHopResourceAccountant::Srv6NextHopResourceAccountant(
    const HwAsicTable* asicTable)
    : asicTable_(asicTable) {
  for (const auto& [_, asic] : asicTable->getHwAsics()) {
    if (auto limit = asic->getMaxSrv6UcmpDistinctWeights()) {
      maxSrv6UcmpDistinctWeights_ =
          std::min(maxSrv6UcmpDistinctWeights_.value_or(*limit), *limit);
    }
  }
}

size_t Srv6NextHopResourceAccountant::countSrv6NextHops(
    const RouteNextHopSet& nhSet) const {
  size_t count = 0;
  for (const auto& nhop : nhSet) {
    if (nhop.tunnelType() == TunnelType::SRV6_ENCAP) {
      count++;
    }
  }
  return count;
}

bool Srv6NextHopResourceAccountant::checkSrv6UcmpDistinctWeights(
    const RouteNextHopSet& nhSet) const {
  if (!maxSrv6UcmpDistinctWeights_.has_value() || hasBackupNextHop(nhSet)) {
    return true;
  }
  std::set<NextHopWeight> weights;
  for (const auto& nhop : nhSet) {
    weights.insert(nhop.weight());
  }
  return weights.size() <= *maxSrv6UcmpDistinctWeights_;
}

template <typename AddrT>
bool Srv6NextHopResourceAccountant::checkAndUpdateSrv6NextHopResource(
    const std::shared_ptr<Route<AddrT>>& route,
    bool add,
    const std::shared_ptr<SwitchState>& state) {
  const auto& fwd = route->getForwardInfo();
  if (fwd.getAction() != RouteForwardAction::NEXTHOPS) {
    return true;
  }

  auto nhSetId = fwd.getNormalizedResolvedNextHopSetID();
  if (!nhSetId.has_value()) {
    return true;
  }
  auto setIdVal = static_cast<int64_t>(nhSetId.value());

  // Check if this NextHopSetID is already tracked
  if (auto it = srv6NextHopSetRefMap_.find(setIdVal);
      it != srv6NextHopSetRefMap_.end()) {
    it->second.refCount += (add ? 1 : -1);
    CHECK_GE(it->second.refCount, 0);
    if (!add && it->second.refCount == 0) {
      if (it->second.isEcmp) {
        srv6EcmpNextHopUsage_ -= it->second.srv6Count;
      } else {
        srv6SingleNextHopUsage_ -= it->second.srv6Count;
      }
      srv6NextHopSetRefMap_.erase(it);
    }
    return true;
  }

  // NextHopSetID not in map. For remove, this means the route had no SRv6
  // next hops when it was added (srv6Count was 0), so nothing to undo.
  if (!add) {
    return true;
  }

  // New NextHopSetID on add — resolve nhops to count SRv6
  auto nhSet = getNormalizedNextHops(state, fwd);
  auto srv6Count = countSrv6NextHops(nhSet);
  if (srv6Count == 0) {
    return true;
  }

  bool isEcmpRoute = nhSet.size() > 1;
  srv6NextHopSetRefMap_[setIdVal] = {1, srv6Count, isEcmpRoute};
  if (isEcmpRoute) {
    srv6EcmpNextHopUsage_ += srv6Count;
  } else {
    srv6SingleNextHopUsage_ += srv6Count;
  }
  if (!checkSrv6UcmpDistinctWeights(nhSet)) {
    XLOG(DBG2) << "Route " << route->prefix().str()
               << " SRv6 UCMP group exceeds ASIC limit of "
               << *maxSrv6UcmpDistinctWeights_ << " distinct weights";
    return false;
  }
  return true;
}

template bool Srv6NextHopResourceAccountant::checkAndUpdateSrv6NextHopResource<
    folly::IPAddressV6>(
    const std::shared_ptr<Route<folly::IPAddressV6>>& route,
    bool add,
    const std::shared_ptr<SwitchState>& state);

template bool Srv6NextHopResourceAccountant::checkAndUpdateSrv6NextHopResource<
    folly::IPAddressV4>(
    const std::shared_ptr<Route<folly::IPAddressV4>>& route,
    bool add,
    const std::shared_ptr<SwitchState>& state);

bool Srv6NextHopResourceAccountant::checkSrv6NextHopResource(
    bool intermediateState) const {
  uint32_t resourcePercentage = intermediateState
      ? kHundredPercentage
      : FLAGS_srv6_nexthop_resource_percentage;

  for (const auto& [_, hwAsic] : asicTable_->getHwAsics()) {
    auto ecmpLimit = hwAsic->getMaxSrv6EcmpNextHops();
    if (ecmpLimit.has_value()) {
      uint32_t enforcedLimit =
          (ecmpLimit.value() * resourcePercentage) / kHundredPercentage;
      if (srv6EcmpNextHopUsage_ > enforcedLimit) {
        XLOG(DBG2) << "SRv6 ECMP next hop limit exceeded. Usage: "
                   << srv6EcmpNextHopUsage_ << " ASIC limit: " << enforcedLimit;
        return false;
      }
    }
    auto singleLimit = hwAsic->getMaxSrv6SingleNextHops();
    if (singleLimit.has_value()) {
      uint32_t enforcedLimit =
          (singleLimit.value() * resourcePercentage) / kHundredPercentage;
      if (srv6SingleNextHopUsage_ > enforcedLimit) {
        XLOG(DBG2) << "SRv6 single next hop limit exceeded. Usage: "
                   << srv6SingleNextHopUsage_
                   << " ASIC limit: " << enforcedLimit;
        return false;
      }
    }
  }
  return true;
}

} // namespace facebook::fboss
