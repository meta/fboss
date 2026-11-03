// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <memory>
#include <vector>

#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/types.h"

namespace facebook::fboss {
class SwitchState;
template <typename AddrT>
class Route;

class PbrAclManager {
 public:
  std::vector<StateDelta> modifyState(const std::vector<StateDelta>& deltas);
  std::vector<StateDelta> reconstructFromSwitchState(
      const std::shared_ptr<SwitchState>& curState);
  void updateDone();
  void updateFailed(const std::shared_ptr<SwitchState>& curState);

 private:
  struct InputOutputState {
    explicit InputOutputState(const StateDelta& in);

    void publishLastDelta();

    void appendDelta(const StateDelta& delta);

    void replaceLastDelta(const StateDelta& delta);

    // When a policy's default NHG members change, its match id changes, and the
    // routes using that NHG move to the new id in the same delta. Within one
    // delta, hardware programs routes before ACLs. Left alone, routes would
    // switch to the new id before any ACL matched it, and class traffic would
    // briefly take the default path.
    //
    // To avoid that, PbrAclManager takes the routes back out of the delta. It
    // emits each unit's make delta first (the temporary rules on the new id),
    // then puts that unit's routes back with addOrUpdateRoute, then updates the
    // original rules in place.
    //
    // `withRouteDelta` covers two cases:
    // - true, the unit's first route: starts a new delta after the make. If the
    //   route joined the make delta it would be programmed before the ACL,
    //   which is the problem above.
    // - false, the unit's remaining routes: fold into that same delta via
    //   replaceLastDelta. Each unit costs one extra delta rather than one per
    //   route.
    template <typename AddrT>
    void addOrUpdateRoute(
        RouterID rid,
        const std::shared_ptr<Route<AddrT>>& route,
        bool withRouteDelta);

    StateDelta getCurrentStateDelta() const;

    size_t numDeltas() const {
      return out_.size();
    }

    std::vector<StateDelta> moveDeltas() {
      return std::move(out_);
    }

    bool updated{false};

   private:
    std::vector<StateDelta> out_;
  };

  void processDelta(const StateDelta& delta, InputOutputState* inOutState);
  std::vector<StateDelta> modifyStateImpl(
      const std::vector<StateDelta>& deltas);
};

} // namespace facebook::fboss
