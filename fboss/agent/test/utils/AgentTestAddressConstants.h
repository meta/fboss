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

// Common IPv6 test addresses used across agent HW tests.
// Prefix: 2620:0:1cfe:face:b00c::
inline constexpr auto kTestSrcIpV6 = "2620:0:1cfe:face:b00c::3";
inline constexpr auto kTestDstIpV6 = "2620:0:1cfe:face:b00c::4";

// Common L4 port numbers used across agent HW tests.
inline constexpr int kTestSrcPort = 8000;
inline constexpr int kTestDstPort = 8001;

} // namespace facebook::fboss
