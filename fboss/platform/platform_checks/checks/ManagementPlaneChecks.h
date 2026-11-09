/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <memory>

#include "fboss/platform/platform_checks/PlatformCheck.h"

// Health of the management plane: firmware tooling and the x86 <-> BMC link
// (the eth0.4088 VLAN over which the x86 reaches the BMC at fe80::1).
namespace facebook::fboss::platform::platform_checks {

std::unique_ptr<PlatformCheck> makeX86FwUtilCheck(CheckTarget x86);
std::unique_ptr<PlatformCheck> makeX86BmcLinkCheck(CheckTarget x86);
std::unique_ptr<PlatformCheck> makeIpmiMcInfoCheck(CheckTarget x86);
std::unique_ptr<PlatformCheck> makeBmcRestApiFromX86Check(CheckTarget x86);

std::unique_ptr<PlatformCheck> makeBmcFwUtilCheck(CheckTarget bmc);
std::unique_ptr<PlatformCheck> makeBmcX86LinkCheck(CheckTarget bmc);
std::unique_ptr<PlatformCheck> makeBmcRestApiCheck(CheckTarget bmc);

} // namespace facebook::fboss::platform::platform_checks
