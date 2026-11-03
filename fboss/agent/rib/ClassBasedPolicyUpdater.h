// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/agent/rib/NextHopIDManager.h"

#include <memory>

namespace facebook::fboss {

class SwitchState;

/*
 * ClassBasedPolicyUpdater publishes the RIB's class-based policy store into
 * SwitchState.classBasedPolicyMaps, resolving each policy's named next-hop
 * groups to NextHopSetIDs.
 */
class ClassBasedPolicyUpdater {
 public:
  explicit ClassBasedPolicyUpdater(const NextHopIDManager* nextHopIDManager);

  std::shared_ptr<SwitchState> operator()(
      const std::shared_ptr<SwitchState>& state);

 private:
  const NextHopIDManager* nextHopIDManager_;
};

} // namespace facebook::fboss
