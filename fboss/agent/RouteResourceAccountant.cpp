// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/RouteResourceAccountant.h"

#include "fboss/agent/HwAsicTable.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss {

RouteResourceAccountant::RouteResourceAccountant(const HwAsicTable* asicTable)
    : asicTable_(asicTable) {}

bool RouteResourceAccountant::checkAndUpdateRouteResource(bool add) {
  // Staring with the simpliest computation - treat all routes the same.
  // We will graually evolve this to be more accurate (e.g. v4 /32, v4 </32,
  // v6/64 etc.).
  if (add) {
    routeUsage_++;
    for (const auto& [_, hwAsic] : asicTable_->getHwAsics()) {
      const auto routeLimit = hwAsic->getMaxRoutes();
      if (routeLimit.has_value() && routeUsage_ > routeLimit.value()) {
        XLOG(DBG2) << "Route limit exceeded. Route demand from this update: "
                   << routeUsage_ << " ASIC Limit: " << routeLimit.value();
        return false;
      }
    }
    return true;
  }
  routeUsage_--;
  return true;
}

uint32_t RouteResourceAccountant::getUsage() const {
  return routeUsage_;
}

} // namespace facebook::fboss
