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
