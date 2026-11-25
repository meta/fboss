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

#include "fboss/agent/AclResourceAccountant.h"
#include "fboss/agent/EcmpResourceAccountant.h"
#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/L2ResourceAccountant.h"
#include "fboss/agent/MySidResourceAccountant.h"
#include "fboss/agent/NeighborResourceAccountant.h"
#include "fboss/agent/RouteCounterResourceAccountant.h"
#include "fboss/agent/RouteResourceAccountant.h"
#include "fboss/agent/Srv6NextHopResourceAccountant.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/VrfResourceAccountant.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"

#include <gtest/gtest.h>

namespace facebook::fboss {

class ResourceAccountant {
 public:
  ResourceAccountant(
      const HwAsicTable* asicTable,
      const SwitchIdScopeResolver* scopeResolver);

  bool isValidUpdate(const StateDelta& delta);
  void stateChanged(const StateDelta& delta);

 private:
  bool routeAndEcmpStateChangedImpl(const StateDelta& delta);
  bool isValidRouteUpdate(const StateDelta& delta);
  bool shouldCheckRouteUpdate() const;

  template <typename AddrT>
  bool checkAndUpdateNextHopResources(
      const std::shared_ptr<Route<AddrT>>& route,
      bool add,
      const std::shared_ptr<SwitchState>& state);

  const HwAsicTable* asicTable_;
  AclResourceAccountant aclResourceAccountant_;
  VrfResourceAccountant vrfResourceAccountant_;
  MySidResourceAccountant mySidResourceAccountant_;
  L2ResourceAccountant l2ResourceAccountant_;
  NeighborResourceAccountant neighborResourceAccountant_;
  RouteCounterResourceAccountant routeCounterResourceAccountant_;
  RouteResourceAccountant routeResourceAccountant_;
  Srv6NextHopResourceAccountant srv6NextHopResourceAccountant_;
  EcmpResourceAccountant ecmpResourceAccountant_;
  bool checkRouteUpdate_;

  FRIEND_TEST(ResourceAccountantTest, resolvedAndUnresolvedRoutes);
  FRIEND_TEST(MacTableManagerTest, MacLearnedBulkCb);
  FRIEND_TEST(StaticMacConfigTest, ApplyConfigAccountsStaticMacsOnce);
  FRIEND_TEST(StaticMacConfigTest, IsValidStateUpdateDryRun);
};
} // namespace facebook::fboss
