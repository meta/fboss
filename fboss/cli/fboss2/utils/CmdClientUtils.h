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

#include <chrono>
#include <optional>

#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/agent/if/gen-cpp2/FbossHwCtrl.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/fsdb/if/gen-cpp2/FsdbService.h"
#include "fboss/led_service/if/gen-cpp2/LedService.h"
#include "fboss/platform/fan_service/if/gen-cpp2/FanService.h"
#include "fboss/qsfp_service/if/gen-cpp2/QsfpService.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"

namespace facebook::fboss::utils {

using RunForHwAgentFn = std::function<void(
    apache::thrift::Client<facebook::fboss::FbossHwCtrl>& client)>;

using RunForAgentFn =
    std::function<void(apache::thrift::Client<FbossCtrl>& client)>;

std::unique_ptr<facebook::fboss::FbossCtrlAsyncClient> createAgentClient(
    const HostInfo& hostInfo);

std::unique_ptr<facebook::fboss::FbossCtrlAsyncClient> createAgentClient(
    const HostInfo& hostInfo,
    const std::chrono::milliseconds& timeout);

std::unique_ptr<apache::thrift::Client<FbossCtrl>> createAgentClient(
    const HostInfo& hostInfo,
    int switchIndex);

std::unique_ptr<apache::thrift::Client<FbossHwCtrl>> createHwAgentClient(
    const HostInfo& hostInfo,
    int switchIndex);

std::unique_ptr<facebook::fboss::QsfpServiceAsyncClient> createQsfpClient(
    const HostInfo& hostInfo);

std::unique_ptr<
    apache::thrift::Client<facebook::fboss::led_service::LedService>>
createLedClient(const HostInfo& hostInfo);

std::unique_ptr<apache::thrift::Client<facebook::fboss::fsdb::FsdbService>>
createFsdbClient(const HostInfo& hostInfo);

std::unique_ptr<
    apache::thrift::Client<facebook::fboss::platform::fan_service::FanService>>
createFanServiceClient(const HostInfo& hostInfo);

std::unique_ptr<
    apache::thrift::Client<facebook::neteng::fboss::bgp::thrift::TBgpService>>
createBgpClient(const HostInfo& hostInfo);

MultiSwitchRunState getMultiSwitchRunState(const HostInfo& hostInfo);
int getNumHwSwitches(const HostInfo& hostInfo);
bool isMultiSwitchEnabled(const HostInfo& hostInfo);

// OSS fb303 FacebookService has no getRegexExportedValues.
#ifndef IS_OSS
using HwAgentExportedValues =
    std::map<int32_t, std::optional<std::map<std::string, std::string>>>;

// Unreachable hw agents map to std::nullopt; empty result on mono switches.
HwAgentExportedValues getHwAgentExportedValues(
    const HostInfo& hostInfo,
    const std::string& regex);
HwAgentExportedValues getHwAgentExportedValues(
    const HostInfo& hostInfo,
    const MultiSwitchRunState& runState,
    const std::string& regex);
#endif

void runOnAllHwAgents(const HostInfo& hostInfo, RunForHwAgentFn fn);
void runOnAllHwAgents(const HostInfo& hostInfo, RunForAgentFn fn);

} // namespace facebook::fboss::utils
