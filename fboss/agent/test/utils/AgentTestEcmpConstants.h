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

namespace facebook::fboss {

// Default ECMP width for single next-hop tests
inline constexpr auto kDefaultEcmpWidth = 1;

// Wide ECMP width for multi-path tests (load balancing, overflow, etc.)
inline constexpr auto kWideEcmpWidth = 4;

} // namespace facebook::fboss
