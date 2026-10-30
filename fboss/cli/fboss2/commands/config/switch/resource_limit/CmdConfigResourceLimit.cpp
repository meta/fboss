// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/commands/config/switch/resource_limit/CmdConfigResourceLimit.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/String.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

using namespace resource_limit_attrs;

namespace {
// gflag names the attributes map onto, as read by initFlagDefaults from
// AgentConfig.defaultCommandLineArgs at agent start.
constexpr std::string_view kGflagMaxL2Entries = "max_l2_entries";
constexpr std::string_view kGflagMaxArpEntries = "max_arp_entries";
constexpr std::string_view kGflagMaxNdpEntries = "max_ndp_entries";
constexpr std::string_view kGflagHwUpdateProtection =
    "enable_hw_update_protection";

std::string parseNonNegativeInt(std::string_view attr, const std::string& v) {
  int32_t parsed = 0;
  try {
    parsed = folly::to<int32_t>(v);
  } catch (const folly::ConversionError&) {
    throw std::invalid_argument(
        fmt::format("Value for '{}' must be an integer, got '{}'", attr, v));
  }
  if (parsed < 0) {
    throw std::invalid_argument(
        fmt::format("Value for '{}' must be non-negative, got {}", attr, v));
  }
  return folly::to<std::string>(parsed);
}

std::optional<bool> tryParseBool(std::string v) {
  folly::toLowerAscii(v);
  if (v == "true" || v == "1") {
    return true;
  }
  if (v == "false" || v == "0") {
    return false;
  }
  return std::nullopt;
}

std::string parseBool(std::string_view attr, const std::string& v) {
  auto parsed = tryParseBool(v);
  if (!parsed.has_value()) {
    throw std::invalid_argument(
        fmt::format("Value for '{}' must be true or false, got '{}'", attr, v));
  }
  return *parsed ? "true" : "false";
}

// ResourceAccountant skips its L2/neighbor checks unless this flag is on, so
// a limit written without it would be committed and then ignored.
void requireHwUpdateProtection(
    const cfg::AgentConfig& config,
    const std::string& attr) {
  const auto& cliArgs = *config.defaultCommandLineArgs();
  auto it = cliArgs.find(std::string(kGflagHwUpdateProtection));
  if (it == cliArgs.end() || !tryParseBool(it->second).value_or(false)) {
    throw std::runtime_error(
        fmt::format(
            "Resource limit '{}' is only enforced when {} is enabled. Run "
            "`config switch resource-limit {} true` first; it can be part of "
            "the same session",
            attr,
            kHwProtection,
            kHwProtection));
  }
}
} // namespace

ResourceLimitConfigArgs::ResourceLimitConfigArgs(std::vector<std::string> v) {
  if (v.size() != 2) {
    throw std::invalid_argument(
        fmt::format(
            "Expected <attr> <value>, got {} argument(s). Valid attrs: {}",
            v.size(),
            folly::join(", ", kValidAttrs)));
  }

  if (std::find(kValidAttrs.begin(), kValidAttrs.end(), v[0]) ==
      kValidAttrs.end()) {
    throw std::invalid_argument(
        fmt::format(
            "Unknown resource-limit attribute '{}'. Valid attrs: {}",
            v[0],
            folly::join(", ", kValidAttrs)));
  }

  if (v[0] == kL2Max) {
    gflagName_ = kGflagMaxL2Entries;
    gflagValue_ = parseNonNegativeInt(kL2Max, v[1]);
  } else if (v[0] == kArpMax) {
    gflagName_ = kGflagMaxArpEntries;
    gflagValue_ = parseNonNegativeInt(kArpMax, v[1]);
  } else if (v[0] == kNdpMax) {
    gflagName_ = kGflagMaxNdpEntries;
    gflagValue_ = parseNonNegativeInt(kNdpMax, v[1]);
  } else if (v[0] == kHwProtection) {
    gflagName_ = kGflagHwUpdateProtection;
    gflagValue_ = parseBool(kHwProtection, v[1]);
  } else {
    // Guards against kValidAttrs drifting from this dispatch.
    throw std::runtime_error(
        fmt::format("Unhandled resource-limit attribute '{}'", v[0]));
  }
  isLimit_ = v[0] != kHwProtection;

  attribute_ = v[0];
  data_ = std::move(v);
}

CmdConfigResourceLimitTraits::RetType CmdConfigResourceLimit::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto& config = session.getAgentConfig();

  const auto& gflagName = args.getGflagName();
  const auto& gflagValue = args.getGflagValue();

  if (args.isLimit()) {
    requireHwUpdateProtection(config, args.getAttribute());
  }

  // Read-modify-write a single defaultCommandLineArgs entry, preserving every
  // other flag already present in the config.
  auto& cliArgs = *config.defaultCommandLineArgs();
  auto it = cliArgs.find(gflagName);
  if (it != cliArgs.end() && it->second == gflagValue) {
    return fmt::format(
        "Resource limit {} ({}) is already set to {}",
        args.getAttribute(),
        gflagName,
        gflagValue);
  }
  cliArgs[gflagName] = gflagValue;

  // These are gflags consumed once at agent start by initFlagDefaults; the
  // running agent does not re-read defaultCommandLineArgs. A coldboot is
  // required for the new limit to take effect.
  session.saveConfig(
      cli::ServiceType::AGENT,
      cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART);

  auto msg = fmt::format(
      "Successfully set resource limit {} ({}) to {}. "
      "A coldboot is required to apply the change.",
      args.getAttribute(),
      gflagName,
      gflagValue);
  if (!args.isLimit() && gflagValue == "false") {
    // Turning protection off leaves any configured limits in place but inert.
    std::vector<std::string_view> inert;
    for (auto limit :
         {kGflagMaxL2Entries, kGflagMaxArpEntries, kGflagMaxNdpEntries}) {
      if (cliArgs.contains(std::string(limit))) {
        inert.push_back(limit);
      }
    }
    if (!inert.empty()) {
      msg += fmt::format(
          " Note: {} stay configured but are not enforced while {} is off.",
          folly::join(", ", inert),
          kHwProtection);
    }
  }
  return msg;
}

void CmdConfigResourceLimit::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdConfigResourceLimit, CmdConfigResourceLimitTraits>::run();

} // namespace facebook::fboss
