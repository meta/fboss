// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/commands/delete/interface/pfc_config/CmdDeleteInterfacePfcConfig.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/String.h>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"

namespace facebook::fboss {

namespace {
constexpr std::string_view kAttrWatchdog = "watchdog";
constexpr std::string_view kAttrRxDuration = "rx-duration";
constexpr std::string_view kAttrTxDuration = "tx-duration";
constexpr auto kValidAttrs = "watchdog, rx-duration, tx-duration";
} // namespace

PfcConfigDeleteAttrs::PfcConfigDeleteAttrs(std::vector<std::string> v) {
  for (const auto& raw : v) {
    std::string attr = raw;
    std::transform(attr.begin(), attr.end(), attr.begin(), [](unsigned char c) {
      return std::tolower(c);
    });
    if (attr != kAttrWatchdog && attr != kAttrRxDuration &&
        attr != kAttrTxDuration) {
      throw std::invalid_argument(
          fmt::format(
              "Unknown pfc-config attribute '{}'. Valid attributes are: {}",
              raw,
              kValidAttrs));
    }
    attributes_.push_back(std::move(attr));
  }
  data_ = std::move(v);
}

CmdDeleteInterfacePfcConfigTraits::RetType
CmdDeleteInterfacePfcConfig::queryClient(
    const HostInfo& /* hostInfo */,
    const utils::InterfaceList& interfaces,
    const ObjectArgType& attrs) {
  if (interfaces.empty()) {
    throw std::invalid_argument("No interface name provided");
  }

  auto& session = ConfigSession::getInstance();
  const bool wholeStruct = attrs.getAttributes().empty();

  std::vector<std::string> updated;
  for (const utils::Intf& intf : interfaces) {
    cfg::Port* port = intf.getPort();
    if (!port) {
      throw std::invalid_argument(
          fmt::format("Interface '{}' is not a physical port.", intf.name()));
    }
    if (!port->pfc().has_value()) {
      // Nothing to clear; not an error so multi-port selections stay usable.
      continue;
    }
    if (wholeStruct) {
      port->pfc().reset();
    } else {
      auto& pfc = *port->pfc();
      for (const auto& attr : attrs.getAttributes()) {
        if (attr == kAttrWatchdog) {
          pfc.watchdog().reset();
        } else if (attr == kAttrRxDuration) {
          pfc.rxPfcDurationEnable().reset();
        } else {
          pfc.txPfcDurationEnable().reset();
        }
      }
    }
    updated.push_back(intf.name());
  }

  if (updated.empty()) {
    return fmt::format(
        "No pfc-config set on interface(s) {}",
        folly::join(", ", interfaces.getNames()));
  }

  session.saveConfig();

  if (wholeStruct) {
    return fmt::format(
        "Deleted pfc-config of interface(s) {}", folly::join(", ", updated));
  }
  return fmt::format(
      "Reset pfc-config {} of interface(s) {}",
      folly::join(", ", attrs.getAttributes()),
      folly::join(", ", updated));
}

void CmdDeleteInterfacePfcConfig::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdDeleteInterfacePfcConfig,
    CmdDeleteInterfacePfcConfigTraits>::run();

} // namespace facebook::fboss
