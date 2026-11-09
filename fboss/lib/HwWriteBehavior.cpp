/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/lib/HwWriteBehavior.h"

#include <atomic>
#include <optional>

namespace facebook::fboss {
namespace {
// Process-wide default observed by threads that never install a scope.
std::atomic<HwWriteBehavior> defaultBehavior{HwWriteBehavior::WRITE};
// Per-thread override installed by HwWriteBehaviorRAII. nullopt means
// "no override on this thread, fall back to the process-wide default".
thread_local std::optional<HwWriteBehavior> threadBehavior{std::nullopt};

void setHwWriteBehavior(HwWriteBehavior behavior) {
  threadBehavior = behavior;
}

} // namespace
HwWriteBehavior getHwWriteBehavior() {
  return threadBehavior.value_or(defaultBehavior.load());
}

HwWriteBehaviorRAII::HwWriteBehaviorRAII(HwWriteBehavior behavior) {
  prevBehavior_ = getHwWriteBehavior();
  setHwWriteBehavior(behavior);
}

HwWriteBehaviorRAII::~HwWriteBehaviorRAII() {
  setHwWriteBehavior(prevBehavior_);
}
} // namespace facebook::fboss
