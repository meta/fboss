// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/agent/state/Route.h"

#include <gtest/gtest_prod.h>

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace facebook::fboss {

class HwAsicTable;

class RouteCounterResourceAccountant {
 public:
  explicit RouteCounterResourceAccountant(const HwAsicTable* asicTable);

  template <typename AddrT>
  bool checkAndUpdateRouteCounterResource(
      const std::shared_ptr<Route<AddrT>>& route,
      bool add);

  bool checkRouteCounterResource(bool intermediateState) const;

 private:
  const HwAsicTable* asicTable_;
  std::unordered_map<RouteCounterID, uint32_t> routeCounterRefMap_;

  FRIEND_TEST(
      RouteCounterResourceAccountantTest,
      checkAndUpdateRouteCounterResource);
  FRIEND_TEST(RouteCounterResourceAccountantTest, routeCounterResourceExceeded);
};

} // namespace facebook::fboss
