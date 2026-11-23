/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/utils/CommandExit.h"

#include <atomic>
#include <cstdlib>
#include <stdexcept>

namespace facebook::fboss::utils {
namespace {
std::atomic<bool> throws{false};
}

bool setCommandExitThrows(bool enabled) {
  return throws.exchange(enabled);
}

bool commandExitThrows() {
  return throws.load();
}

[[noreturn]] void commandExit(int status) {
  if (throws.load()) {
    throw std::runtime_error("fboss2 command failed");
  }
  std::exit(status);
}

} // namespace facebook::fboss::utils
