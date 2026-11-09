/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/cli/fboss2/utils/CmdClientUtils.h"
#include <folly/logging/xlog.h>

namespace facebook::fboss::utils {

template <>
std::unique_ptr<facebook::fboss::FbossCtrlAsyncClient> createClient(
    const HostInfo& hostInfo) {
  return utils::createAgentClient(hostInfo);
}

template <>
std::unique_ptr<facebook::fboss::FbossCtrlAsyncClient> createClient(
    const HostInfo& hostInfo,
    const std::chrono::milliseconds& timeout) {
  return utils::createAgentClient(hostInfo, timeout);
}

template <>
std::unique_ptr<apache::thrift::Client<FbossCtrl>> createClient(
    const HostInfo& hostInfo,
    int switchIndex) {
  return utils::createAgentClient(hostInfo, switchIndex);
}

template <>
std::unique_ptr<apache::thrift::Client<FbossHwCtrl>> createClient(
    const HostInfo& hostInfo,
    int switchIndex) {
  return utils::createHwAgentClient(hostInfo, switchIndex);
}

template <>
std::unique_ptr<facebook::fboss::QsfpServiceAsyncClient> createClient(
    const HostInfo& hostInfo) {
  return utils::createQsfpClient(hostInfo);
}

template <>
std::unique_ptr<
    apache::thrift::Client<facebook::fboss::led_service::LedService>>
createClient(const HostInfo& hostInfo) {
  return utils::createLedClient(hostInfo);
}

template <>
std::unique_ptr<apache::thrift::Client<facebook::fboss::fsdb::FsdbService>>
createClient(const HostInfo& hostInfo) {
  return utils::createFsdbClient(hostInfo);
}

template <>
std::unique_ptr<
    apache::thrift::Client<facebook::fboss::platform::fan_service::FanService>>
createClient(const HostInfo& hostInfo) {
  return utils::createFanServiceClient(hostInfo);
}

// In non-OSS builds, this specialization is provided by
// utils/facebook/CmdClientUtils.cpp; defining it here too would cause a
// duplicate-symbol link error.
#ifdef IS_OSS
template <>
std::unique_ptr<
    apache::thrift::Client<facebook::neteng::fboss::bgp::thrift::TBgpService>>
createClient(const HostInfo& hostInfo) {
  return utils::createBgpClient(hostInfo);
}
#endif

MultiSwitchRunState getMultiSwitchRunState(const HostInfo& hostInfo) {
  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
  MultiSwitchRunState runState;
  client->sync_getMultiSwitchRunState(runState);
  return runState;
}

int getNumHwSwitches(const HostInfo& hostInfo) {
  return static_cast<int>(
      getMultiSwitchRunState(hostInfo).hwIndexToRunState()->size());
}

bool isMultiSwitchEnabled(const HostInfo& hostInfo) {
  return *getMultiSwitchRunState(hostInfo).multiSwitchEnabled();
}

#ifndef IS_OSS
HwAgentExportedValues getHwAgentExportedValues(
    const HostInfo& hostInfo,
    const std::string& regex) {
  MultiSwitchRunState runState;
  try {
    runState = getMultiSwitchRunState(hostInfo);
  } catch (const std::exception& e) {
    XLOG(WARN) << "Failed to get multi-switch run state: " << e.what();
    return {};
  }
  return getHwAgentExportedValues(hostInfo, runState, regex);
}

HwAgentExportedValues getHwAgentExportedValues(
    const HostInfo& hostInfo,
    const MultiSwitchRunState& runState,
    const std::string& regex) {
  HwAgentExportedValues hwAgentValues;
  if (!*runState.multiSwitchEnabled()) {
    return hwAgentValues;
  }
  // hwIndexToRunState is keyed by switch id, not switch index.
  const auto numHwSwitches =
      static_cast<int>(runState.hwIndexToRunState()->size());
  for (int switchIndex = 0; switchIndex < numHwSwitches; switchIndex++) {
    std::map<std::string, std::string> values;
    try {
      utils::createClient<apache::thrift::Client<FbossCtrl>>(
          hostInfo, switchIndex)
          ->sync_getRegexExportedValues(values, regex);
    } catch (const std::exception& e) {
      XLOG(WARN) << "Failed to get hw_agent " << switchIndex
                 << " exported values: " << e.what();
      hwAgentValues[switchIndex] = std::nullopt;
      continue;
    }
    hwAgentValues[switchIndex] = std::move(values);
  }
  return hwAgentValues;
}
#endif

void runOnAllHwAgents(const HostInfo& hostInfo, RunForHwAgentFn fn) {
  auto numHwSwitches = getNumHwSwitches(hostInfo);
  for (int i = 0; i < numHwSwitches; i++) {
    auto client =
        utils::createClient<apache::thrift::Client<FbossHwCtrl>>(hostInfo, i);
    try {
      fn(*client);
    } catch (const std::exception&) {
      // skip switches that cannot be reached
    }
  }
}

void runOnAllHwAgents(const HostInfo& hostInfo, RunForAgentFn fn) {
  auto numHwSwitches = getNumHwSwitches(hostInfo);
  for (int i = 0; i < numHwSwitches; i++) {
    auto client =
        utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo, i);
    try {
      fn(*client);
    } catch (const std::exception&) {
      // skip switches that cannot be reached
    }
  }
}
} // namespace facebook::fboss::utils
