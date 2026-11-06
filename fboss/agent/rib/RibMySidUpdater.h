// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/agent/rib/NetworkToRouteMap.h"
#include "fboss/agent/rib/RouteUpdater.h"
#include "fboss/agent/state/RouteNextHop.h"

#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace facebook::fboss {

class NextHopIDManager;
class MySid;

/**
 * RibMySidUpdater resolves the next hops for MySid entries in the MySidTable.
 *
 * For each MySid entry that has a primary or backup unresolvedNextHopsId, it:
 *   1. Retrieves the unresolved RouteNextHopSet from NextHopIDManager.
 *   2. Resolves each member next hop against the v4/v6 route tables from all
 *      VRFs (which must already be resolved by RibRouteUpdater). The first
 *      VRF that contains a matching route wins.
 *   3. Allocates or updates the corresponding resolvedNextHopsId in
 *      NextHopIDManager.
 *   4. Updates the corresponding MySid resolvedNextHopsId if the resolved set
 *      changed.
 */
class RibMySidUpdater {
 public:
  using VrfRouteTables = facebook::fboss::VrfRouteTables;

  RibMySidUpdater(
      const VrfRouteTables& routeTables,
      NextHopIDManager* nextHopIDManager,
      MySidTable* mySidTable,
      uint32_t ecmpWidth);

  void resolve();
  void resolve(const std::set<folly::CIDRNetwork>& mySidsToResolve);

 private:
  void resolveOneMySid(std::shared_ptr<MySid>& mySid);
  RouteNextHopSet resolveNextHopSet(
      const RouteNextHopSet& unresolvedNhops) const;
  void updateResolvedNextHopSetId(
      std::shared_ptr<MySid>& mySidPtr,
      const RouteNextHopSet& resolvedNhops,
      NextHopRole role);

  VrfRouteTables routeTables_;
  NextHopIDManager* nextHopIDManager_{nullptr};
  MySidTable* mySidTable_{nullptr};
  uint32_t ecmpWidth_;
};

} // namespace facebook::fboss
