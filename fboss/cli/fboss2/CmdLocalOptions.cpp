/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/CmdLocalOptions.h"

#include <folly/Singleton.h>
#include <folly/concurrency/AtomicSharedPtr.h>

namespace {
struct singleton_tag_type {};
} // namespace

namespace facebook::fboss {

using facebook::fboss::CmdLocalOptions;
static folly::Singleton<CmdLocalOptions, singleton_tag_type> cmdLocalOptions_{};
namespace {
folly::atomic_shared_ptr<CmdLocalOptions> invocationOverride{};
}

std::shared_ptr<CmdLocalOptions> CmdLocalOptions::exchangeInvocationOverride(
    std::shared_ptr<CmdLocalOptions> instance) {
  return invocationOverride.exchange(std::move(instance));
}

std::shared_ptr<CmdLocalOptions> CmdLocalOptions::getInstance() {
  if (auto instance = invocationOverride.load()) {
    return instance;
  }
  return cmdLocalOptions_.try_get();
}
} // namespace facebook::fboss
