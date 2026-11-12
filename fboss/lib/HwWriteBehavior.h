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

#include "fboss/agent/if/gen-cpp2/common_types.h"

namespace facebook::fboss {

/*
 * The write behavior is thread-local: each thread observes only the
 * behavior installed by RAII scopes on that same thread. This matters
 * because processes like qsfp_service init multiple switches (one per
 * xphy) concurrently on different threads; a process-global flag lets one
 * switch's FAIL window trip another switch's legitimate SAI calls (e.g.
 * the mandatory warmboot switch-reclaim create). Threads that never
 * install a scope observe the process-wide default (WRITE).
 */
HwWriteBehavior getHwWriteBehavior();

class HwWriteBehaviorRAII {
 public:
  explicit HwWriteBehaviorRAII(HwWriteBehavior behavior);
  ~HwWriteBehaviorRAII();

 private:
  HwWriteBehavior prevBehavior_;
};
} // namespace facebook::fboss
