/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <optional>
#include <string>

namespace facebook::fboss::platform::fixmyfboss {

// Prints `prompt` to the controlling terminal and reads a line without
// echoing it. nullopt if there is no terminal or input ends. Ctrl-C restores
// the terminal and then interrupts as usual.
std::optional<std::string> readPasswordFromTerminal(const std::string& prompt);

} // namespace facebook::fboss::platform::fixmyfboss
