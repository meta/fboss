// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstdint>

namespace facebook::fboss {

class HwAsicTable;

class RouteResourceAccountant {
 public:
  explicit RouteResourceAccountant(const HwAsicTable* asicTable);
  bool checkAndUpdateRouteResource(bool add);
  uint32_t getUsage() const;

 private:
  const HwAsicTable* asicTable_;
  uint32_t routeUsage_{0};
};

} // namespace facebook::fboss
