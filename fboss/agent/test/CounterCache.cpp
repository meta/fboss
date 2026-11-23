/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/test/CounterCache.h"

#include <fb303/ServiceData.h>
#include <fb303/ThreadCachedServiceData.h>

namespace facebook::fboss {

void CounterCache::update() {
  fb303::ThreadCachedServiceData::get()->publishStats();
  prev_.swap(current_);
  current_.clear();
  fb303::fbData->getCounters(current_);
}

} // namespace facebook::fboss
