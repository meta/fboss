// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/agent/StateObserver.h"

namespace facebook::fboss {

class SwSwitch;

class RemoteNeighborUpdater : public StateObserver {
 public:
  explicit RemoteNeighborUpdater(SwSwitch* sw);
  virtual ~RemoteNeighborUpdater() override {}
  void stateUpdated(const StateDelta& delta) override;

 private:
  // static: the stateUpdated() update lambda must not capture `this`.
  static void processChangedInterface(
      SwSwitch* sw,
      std::shared_ptr<SwitchState>* state,
      const std::shared_ptr<Interface>& newInterface);
  SwSwitch* sw_{nullptr};
};

} // namespace facebook::fboss
