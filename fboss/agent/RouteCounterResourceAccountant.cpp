// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/RouteCounterResourceAccountant.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/HwAsicTable.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss {

namespace {
constexpr auto kHundredPercentage = 100;
} // namespace

RouteCounterResourceAccountant::RouteCounterResourceAccountant(
    const HwAsicTable* asicTable)
    : asicTable_(asicTable) {}

template <typename AddrT>
bool RouteCounterResourceAccountant::checkAndUpdateRouteCounterResource(
    const std::shared_ptr<Route<AddrT>>& route,
    bool add) {
  auto counterID = route->getForwardInfo().getCounterID();
  if (!counterID.has_value()) {
    return true;
  }
  if (add) {
    auto& refCount = routeCounterRefMap_[*counterID];
    refCount++;
    if (refCount == 1) {
      return checkRouteCounterResource(true /* intermediateState */);
    }
    return true;
  }
  auto it = routeCounterRefMap_.find(*counterID);
  CHECK(it != routeCounterRefMap_.end());
  if (--it->second == 0) {
    routeCounterRefMap_.erase(it);
  }
  return true;
}

template bool RouteCounterResourceAccountant::
    checkAndUpdateRouteCounterResource<folly::IPAddressV6>(
        const std::shared_ptr<Route<folly::IPAddressV6>>& route,
        bool add);

template bool RouteCounterResourceAccountant::
    checkAndUpdateRouteCounterResource<folly::IPAddressV4>(
        const std::shared_ptr<Route<folly::IPAddressV4>>& route,
        bool add);

bool RouteCounterResourceAccountant::checkRouteCounterResource(
    bool intermediateState) const {
  uint32_t resourcePercentage = intermediateState
      ? kHundredPercentage
      : FLAGS_route_counter_resource_percentage;

  for (const auto& [_, hwAsic] : asicTable_->getHwAsics()) {
    const auto routeCounterLimit = hwAsic->getMaxRouteCounters();
    if (routeCounterLimit.has_value()) {
      uint32_t enforcedLimit =
          (routeCounterLimit.value() * resourcePercentage) / kHundredPercentage;
      if (routeCounterRefMap_.size() > enforcedLimit) {
        XLOG(DBG2)
            << "Route counter resource limit exceeded. Unique route counters: "
            << routeCounterRefMap_.size() << " ASIC limit: " << enforcedLimit;
        return false;
      }
    }
  }
  return true;
}

} // namespace facebook::fboss
