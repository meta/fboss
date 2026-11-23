/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/interface/pfc_config/CmdConfigInterfacePfcConfig.h"
#include "fboss/cli/fboss2/utils/ArgCompletion.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/String.h>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/interface/pfc_config/PfcConfigUtils.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"

namespace facebook::fboss {

namespace utils {

namespace {

const std::set<std::string> kValidAttrs = {
    "rx",
    "tx",
    "rx-duration",
    "tx-duration",
    "priority-group-policy",
    "watchdog-detection-time",
    "watchdog-recovery-action",
    "watchdog-recovery-time",
};

bool parseEnabledDisabled(const std::string& value) {
  std::string lower = value;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  if (lower == "enabled") {
    return true;
  } else if (lower == "disabled") {
    return false;
  }
  throw std::invalid_argument(
      fmt::format(
          "Invalid value '{}': expected 'enabled' or 'disabled'", value));
}

int32_t parseMsec(const std::string& value) {
  try {
    int32_t msec = folly::to<int32_t>(value);
    if (msec < 0) {
      throw std::invalid_argument(
          fmt::format("Millisecond value must be non-negative, got: {}", msec));
    }
    return msec;
  } catch (const folly::ConversionError&) {
    throw std::invalid_argument(
        fmt::format("Invalid millisecond value: {}", value));
  }
}

cfg::PfcWatchdogRecoveryAction parseRecoveryAction(const std::string& value) {
  std::string lower = value;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  if (lower == "drop") {
    return cfg::PfcWatchdogRecoveryAction::DROP;
  } else if (lower == "no-drop") {
    return cfg::PfcWatchdogRecoveryAction::NO_DROP;
  }
  throw std::invalid_argument(
      fmt::format(
          "Invalid recovery action '{}': expected 'drop' or 'no-drop'", value));
}

} // namespace

PfcConfigAttrs::PfcConfigAttrs(std::vector<std::string> v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "Expected: <attr> <value> [<attr> <value> ...] where <attr> is one of: "
        "rx, tx, rx-duration, tx-duration, priority-group-policy, "
        "watchdog-detection-time, watchdog-recovery-action, watchdog-recovery-time");
  }

  // Parse key-value pairs
  if (v.size() % 2 != 0) {
    throw std::invalid_argument(
        "Attribute-value pairs must come in pairs. Got odd number of arguments.");
  }

  for (size_t i = 0; i < v.size(); i += 2) {
    const std::string& attr = v[i];
    const std::string& value = v[i + 1];

    if (kValidAttrs.find(attr) == kValidAttrs.end()) {
      throw std::invalid_argument(
          fmt::format(
              "Unknown attribute: '{}'. Valid attributes are: {}",
              attr,
              folly::join(", ", kValidAttrs)));
    }

    attributes_.emplace_back(attr, value);
    data_.push_back(attr);
    data_.push_back(value);
  }
}

} // namespace utils

CmdConfigInterfacePfcConfigTraits::RetType
CmdConfigInterfacePfcConfig::queryClient(
    const HostInfo& /* hostInfo */,
    const utils::InterfaceList& interfaces,
    const ObjectArgType& config) {
  for (const utils::Intf& intf : interfaces) {
    cfg::Port* port = intf.getPort();
    if (!port) {
      throw std::runtime_error(
          fmt::format("Port not found for interface {}", intf.name()));
    }
    // Ensure pfc struct exists
    if (!port->pfc().has_value()) {
      port->pfc() = cfg::PortPfc();
    }

    // Process each attribute-value pair
    for (const auto& [attr, value] : config.getAttributes()) {
      if (attr == "rx") {
        port->pfc()->rx() = utils::parseEnabledDisabled(value);
      } else if (attr == "tx") {
        port->pfc()->tx() = utils::parseEnabledDisabled(value);
      } else if (attr == "rx-duration") {
        port->pfc()->rxPfcDurationEnable() = utils::parseEnabledDisabled(value);
      } else if (attr == "tx-duration") {
        port->pfc()->txPfcDurationEnable() = utils::parseEnabledDisabled(value);
      } else if (attr == "priority-group-policy") {
        port->pfc()->portPgConfigName() = value;
      } else if (attr == "watchdog-detection-time") {
        if (!port->pfc()->watchdog().has_value()) {
          port->pfc()->watchdog() = cfg::PfcWatchdog();
        }
        port->pfc()->watchdog()->detectionTimeMsecs() = utils::parseMsec(value);
      } else if (attr == "watchdog-recovery-action") {
        if (!port->pfc()->watchdog().has_value()) {
          port->pfc()->watchdog() = cfg::PfcWatchdog();
        }
        port->pfc()->watchdog()->recoveryAction() =
            utils::parseRecoveryAction(value);
      } else if (attr == "watchdog-recovery-time") {
        if (!port->pfc()->watchdog().has_value()) {
          port->pfc()->watchdog() = cfg::PfcWatchdog();
        }
        port->pfc()->watchdog()->recoveryTimeMsecs() = utils::parseMsec(value);
      }
    }
  }

  ConfigSession::getInstance().saveConfig();

  std::string interfaceList = folly::join(", ", interfaces.getNames());
  return fmt::format(
      "Successfully configured PFC for interface(s) {}", interfaceList);
}

void CmdConfigInterfacePfcConfig::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdConfigInterfacePfcConfig,
    CmdConfigInterfacePfcConfigTraits>::run();

std::vector<std::string> CmdConfigInterfacePfcConfigTraits::completeArgs(
    const std::vector<std::string>& typed) {
  const std::vector<std::string> enabledDisabled = {"enabled", "disabled"};
  return utils::completion::completeAttrGrammar(
      typed,
      {.attrs = utils::completion::toWords(utils::kValidAttrs),
       .values = {
           {"rx", enabledDisabled},
           {"tx", enabledDisabled},
           {"rx-duration", enabledDisabled},
           {"tx-duration", enabledDisabled},
           {"watchdog-recovery-action", {"drop", "no-drop"}},
       }});
}

} // namespace facebook::fboss
