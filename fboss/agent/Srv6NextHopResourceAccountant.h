// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/RouteNextHopEntry.h"

#include <gtest/gtest_prod.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

namespace facebook::fboss {

class HwAsicTable;
class SwitchState;

class Srv6NextHopResourceAccountant {
 public:
  explicit Srv6NextHopResourceAccountant(const HwAsicTable* asicTable);

  template <typename AddrT>
  bool checkAndUpdateSrv6NextHopResource(
      const std::shared_ptr<Route<AddrT>>& route,
      bool add,
      const std::shared_ptr<SwitchState>& state);

  bool checkSrv6NextHopResource(bool intermediateState) const;

 private:
  size_t countSrv6NextHops(const RouteNextHopSet& nhSet) const;
  bool checkSrv6UcmpDistinctWeights(const RouteNextHopSet& nhSet) const;

  const HwAsicTable* asicTable_;
  std::optional<uint32_t> maxSrv6UcmpDistinctWeights_;
  // SRv6 next hop tracking: keyed by NextHopSetID for efficient lookup.
  // Stores {refCount, srv6Count} per unique NextHopSet.
  struct Srv6NextHopSetInfo {
    int32_t refCount{0};
    size_t srv6Count{0};
    bool isEcmp{false};
  };
  std::unordered_map<int64_t, Srv6NextHopSetInfo> srv6NextHopSetRefMap_;
  uint32_t srv6EcmpNextHopUsage_{0};
  uint32_t srv6SingleNextHopUsage_{0};

  FRIEND_TEST(
      Srv6NextHopResourceAccountantTest,
      checkAndUpdateSrv6NextHopResource);
  FRIEND_TEST(Srv6NextHopResourceAccountantTest, srv6NextHopResourceExceeded);
  FRIEND_TEST(Srv6NextHopResourceAccountantTest, srv6UcmpDistinctWeights);
};

} // namespace facebook::fboss
