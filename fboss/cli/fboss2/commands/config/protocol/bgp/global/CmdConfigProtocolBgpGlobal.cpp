/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/protocol/bgp/global/CmdConfigProtocolBgpGlobal.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/core.h>
#include <folly/IPAddress.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "fboss/cli/fboss2/commands/config/protocol/bgp/BgpCliValueParsers.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fmt/format.h"

#ifndef IS_OSS
#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#else
#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#endif

namespace facebook::fboss {

namespace {

// CLI attribute names. Kept here (not as raw string literals at the dispatch
// sites) so the valid-attribute set and the handler table stay in sync.
constexpr std::string_view kRouterId = "router-id";
constexpr std::string_view kLocalAsn = "local-asn";
constexpr std::string_view kHoldTime = "hold-time";
constexpr std::string_view kConfedAsn = "confed-asn";
constexpr std::string_view kNetwork6 = "network6";
constexpr std::string_view kSwitchLimit = "switch-limit";
constexpr std::string_view kSwitchLimitTotalPath = "switch-limit-total-path";
constexpr std::string_view kSwitchLimitMaxGoldenVips =
    "switch-limit-max-golden-vips";
constexpr std::string_view kSwitchLimitOverloadProtectionMode =
    "switch-limit-overload-protection-mode";
constexpr std::string_view kCountConfedsInAsPathLen =
    "count-confeds-in-as-path-len";
constexpr std::string_view kGracefulRestartTime = "graceful-restart-time";
constexpr std::string_view kRibAllocatedPathIds = "rib-allocated-path-ids";

using Tokens = std::vector<std::string>;
using BgpConfig = bgp::thrift::BgpConfig;
// Value parsers and the handler Result type are shared with the neighbor
// dispatcher (see BgpCliValueParsers.h).
using bgpcli::err;
using bgpcli::ok;
using bgpcli::parseBool;
using bgpcli::parseInt;
using bgpcli::parseNonNegInt32;
using bgpcli::Result;

// ---- per-attribute handlers ------------------------------------------------
// Each handler mutates the typed bgp::thrift::BgpConfig directly. Field names
// map 1:1 to bgp_config.thrift, so the compiler validates them.

// bgpd constructs an IPAddress from router_id at config load (a bad string
// fails the load) and later calls asV4() on it, so only an IPv4 address is
// safe to persist. Stored normalized.
Result applyRouterId(BgpConfig& cfg, const Tokens& values) {
  if (values.size() != 1) {
    return err("Error: router-id requires <ip-address>");
  }
  auto addr = folly::IPAddress::tryFromString(values[0]);
  if (!addr.hasValue()) {
    return err(fmt::format("Error: Invalid router-id address '{}'", values[0]));
  }
  if (!addr->isV4()) {
    return err(
        fmt::format(
            "Error: router-id requires an IPv4 address, got '{}'", values[0]));
  }
  cfg.router_id() = addr->str();
  return ok(fmt::format("Successfully set BGP router-id to: {}", addr->str()));
}

Result applyLocalAsn(BgpConfig& cfg, const Tokens& values) {
  if (values.size() != 1) {
    return err("Error: local-asn requires <asn>");
  }
  auto asn = bgpcli::parseAsn4Byte(values[0]);
  if (!asn) {
    return err(
        fmt::format(
            "Error: Invalid local-asn value '{}'; expected an unsigned "
            "4-byte ASN",
            values[0]));
  }
  cfg.local_as_4_byte() = *asn;
  return ok(fmt::format("Successfully set BGP local-asn to: {}", *asn));
}

Result applyConfedAsn(BgpConfig& cfg, const Tokens& values) {
  if (values.size() != 1) {
    return err("Error: confed-asn requires <asn>");
  }
  auto asn = bgpcli::parseAsn4Byte(values[0]);
  if (!asn) {
    return err(
        fmt::format(
            "Error: Invalid confed-asn value '{}'; expected an unsigned "
            "4-byte ASN",
            values[0]));
  }
  cfg.local_confed_as_4_byte() = *asn;
  return ok(
      fmt::format("Successfully set BGP confederation AS number to: {}", *asn));
}

Result applyHoldTime(BgpConfig& cfg, const Tokens& values) {
  if (values.size() != 1) {
    return err("Error: hold-time requires <seconds>");
  }
  auto seconds = parseNonNegInt32(values[0]);
  if (!seconds) {
    return err(fmt::format("Error: Invalid hold-time value '{}'", values[0]));
  }
  cfg.hold_time() = *seconds;
  return ok(
      fmt::format("Successfully set BGP hold-time to: {} seconds", *seconds));
}

Result applyGracefulRestartTime(BgpConfig& cfg, const Tokens& values) {
  if (values.size() != 1) {
    return err("Error: graceful-restart-time requires <seconds>");
  }
  auto seconds = parseNonNegInt32(values[0]);
  if (!seconds) {
    return err(
        fmt::format(
            "Error: graceful-restart-time must be a non-negative integer "
            "that fits in int32, got '{}'",
            values[0]));
  }
  cfg.graceful_restart_convergence_seconds() = *seconds;
  return ok(
      fmt::format(
          "Successfully set BGP graceful-restart-time to: {} seconds",
          *seconds));
}

Result applyCountConfedsInAsPathLen(BgpConfig& cfg, const Tokens& values) {
  if (values.size() != 1) {
    return err("Error: count-confeds-in-as-path-len requires <true|false>");
  }
  auto enable = parseBool(values[0]);
  if (!enable) {
    return err(
        fmt::format(
            "Error: Invalid value '{}'; expected true or false", values[0]));
  }
  cfg.count_confeds_in_as_path_len() = *enable;
  return ok(
      fmt::format(
          "Successfully {} count-confeds-in-as-path-len",
          *enable ? "enabled" : "disabled"));
}

Result applyRibAllocatedPathIds(BgpConfig& cfg, const Tokens& values) {
  if (values.size() != 1) {
    return err("Error: rib-allocated-path-ids requires <true|false>");
  }
  auto enable = parseBool(values[0]);
  if (!enable) {
    return err(
        fmt::format(
            "Error: Invalid value '{}'; expected true or false", values[0]));
  }
  // enable_rib_allocated_path_id lives inside the nested bgp_setting_config.
  cfg.bgp_setting_config().ensure().enable_rib_allocated_path_id() = *enable;
  return ok(
      fmt::format(
          "Successfully {} rib-allocated-path-ids",
          *enable ? "enabled" : "disabled"));
}

// Shared implementation for the switch-limit attributes, which differ only in
// the field setter, the field name in the success message, and the upper bound
// (i64 fields use INT64_MAX; the i32 max_golden_vips field uses INT32_MAX so an
// out-of-range value is rejected rather than silently truncated). All limits
// must be non-negative.
Result applyInt64Limit(
    BgpConfig& cfg,
    const Tokens& values,
    std::string_view attrName,
    std::string_view fieldName,
    int64_t maxValue,
    const std::function<void(BgpConfig&, int64_t)>& setter) {
  if (values.size() != 1) {
    return err(fmt::format("Error: {} requires <value>", attrName));
  }
  auto limit = parseInt<int64_t>(values[0]);
  if (!limit) {
    return err(
        fmt::format("Error: Invalid {} value '{}'", attrName, values[0]));
  }
  if (*limit < 0 || *limit > maxValue) {
    return err(
        fmt::format(
            "Error: {} value '{}' is out of range [0, {}]",
            attrName,
            values[0],
            maxValue));
  }
  setter(cfg, *limit);
  return ok(
      fmt::format(
          "Successfully set switch_limit_config {} to: {}", fieldName, *limit));
}

// Accepts the enum value name or its integer; anything outside the enum is
// rejected (bgpd would otherwise treat an unknown value as
// DROP_EXCESS_PREFIXES without complaint). Validated before ensure() so a
// rejected value leaves switch_limit_config untouched. The success message
// prints the integer so the j2c round-trip is stable.
Result applyOverloadProtectionMode(BgpConfig& cfg, const Tokens& values) {
  using OverloadProtectionMode = bgp::thrift::OverloadProtectionMode;
  if (values.size() != 1) {
    return err("Error: switch-limit-overload-protection-mode requires <mode>");
  }
  OverloadProtectionMode mode{};
  bool valid = false;
  if (auto parsed = parseInt<int32_t>(values[0])) {
    mode = static_cast<OverloadProtectionMode>(*parsed);
    valid = apache::thrift::TEnumTraits<OverloadProtectionMode>::findName(
                mode) != nullptr;
  } else {
    valid = apache::thrift::TEnumTraits<OverloadProtectionMode>::findValue(
        values[0], &mode);
  }
  if (!valid) {
    std::string names;
    for (auto n : apache::thrift::TEnumTraits<OverloadProtectionMode>::names) {
      names += names.empty() ? "" : ", ";
      names += std::string(n);
    }
    return err(
        fmt::format(
            "Error: switch-limit-overload-protection-mode value '{}' is not a "
            "valid mode; expected one of: {}",
            values[0],
            names));
  }
  cfg.switch_limit_config().ensure().overload_protection_mode() = mode;
  return ok(
      fmt::format(
          "Successfully set switch_limit_config overload_protection_mode to: {}",
          static_cast<int32_t>(mode)));
}

Result applyNetwork6(BgpConfig& cfg, const Tokens& values) {
  // Usage: network6 add <prefix> [policy <name>] [install-to-fib <bool>]
  //        [min-routes <int>]
  if (values.size() < 2 || values[0] != "add") {
    return err(
        "Error: usage: network6 add <prefix> [policy <name>] "
        "[install-to-fib <bool>] [min-routes <int>]");
  }
  std::string prefix = values[1];
  std::string policyName;
  bool installToFib = false;
  int32_t minSupportingRoutes = 0;

  for (size_t i = 2; i < values.size(); i++) {
    if (values[i] == "policy" && i + 1 < values.size()) {
      policyName = values[++i];
    } else if (values[i] == "install-to-fib" && i + 1 < values.size()) {
      auto parsed = parseBool(values[++i]);
      if (!parsed) {
        return err(
            fmt::format(
                "Error: Invalid install-to-fib value '{}'; expected true or false",
                values[i]));
      }
      installToFib = *parsed;
    } else if (values[i] == "min-routes" && i + 1 < values.size()) {
      auto parsed = parseNonNegInt32(values[++i]);
      if (!parsed) {
        return err(
            fmt::format("Error: Invalid min-routes value '{}'", values[i]));
      }
      minSupportingRoutes = *parsed;
    } else {
      return err(
          fmt::format("Error: unexpected network6 token '{}'", values[i]));
    }
  }

  // Find-or-update the networks6 entry keyed by prefix.
  auto& networks = *cfg.networks6();
  bgp::thrift::BgpNetwork* entry = nullptr;
  for (auto& n : networks) {
    if (*n.prefix() == prefix) {
      entry = &n;
      break;
    }
  }
  if (entry == nullptr) {
    networks.emplace_back();
    entry = &networks.back();
  }
  entry->prefix() = prefix;
  entry->policy_name() = policyName;
  entry->install_to_fib() = installToFib;
  entry->minimum_supporting_routes() = minSupportingRoutes;

  return ok(
      fmt::format(
          "Successfully added network6 prefix: {} (policy: {}, install_to_fib: {}, "
          "min_routes: {})",
          prefix,
          policyName.empty() ? "(none)" : policyName,
          installToFib ? "true" : "false",
          minSupportingRoutes));
}

using AttrHandler = std::function<Result(BgpConfig&, const Tokens&)>;

const std::map<std::string_view, AttrHandler>& attrHandlers() {
  static const std::map<std::string_view, AttrHandler> kHandlers = {
      {kRouterId, applyRouterId},
      {kLocalAsn, applyLocalAsn},
      {kConfedAsn, applyConfedAsn},
      {kHoldTime, applyHoldTime},
      {kGracefulRestartTime, applyGracefulRestartTime},
      {kCountConfedsInAsPathLen, applyCountConfedsInAsPathLen},
      {kRibAllocatedPathIds, applyRibAllocatedPathIds},
      {kNetwork6, applyNetwork6},
      {kSwitchLimit,
       [](BgpConfig& cfg, const Tokens& v) {
         return applyInt64Limit(
             cfg,
             v,
             kSwitchLimit,
             "prefix_limit",
             std::numeric_limits<int64_t>::max(),
             [](BgpConfig& c, int64_t l) {
               c.switch_limit_config().ensure().prefix_limit() = l;
             });
       }},
      {kSwitchLimitTotalPath,
       [](BgpConfig& cfg, const Tokens& v) {
         return applyInt64Limit(
             cfg,
             v,
             kSwitchLimitTotalPath,
             "total_path_limit",
             std::numeric_limits<int64_t>::max(),
             [](BgpConfig& c, int64_t l) {
               c.switch_limit_config().ensure().total_path_limit() = l;
             });
       }},
      {kSwitchLimitMaxGoldenVips,
       [](BgpConfig& cfg, const Tokens& v) {
         // max_golden_vips is an i32 field; bound it so an out-of-range value
         // is rejected rather than silently truncated by the static_cast.
         return applyInt64Limit(
             cfg,
             v,
             kSwitchLimitMaxGoldenVips,
             "max_golden_vips",
             std::numeric_limits<int32_t>::max(),
             [](BgpConfig& c, int64_t l) {
               c.switch_limit_config().ensure().max_golden_vips() =
                   static_cast<int32_t>(l);
             });
       }},
      {kSwitchLimitOverloadProtectionMode, applyOverloadProtectionMode},
  };
  return kHandlers;
}

std::string validAttrList() {
  std::string out;
  for (const auto& [name, _] : attrHandlers()) {
    if (!out.empty()) {
      out += ", ";
    }
    out += std::string(name);
  }
  return out;
}

} // namespace

// Parse + validate at construction so queryClient stays a thin dispatch.
// Throwing std::invalid_argument is how the framework surfaces arg parse
// errors (same mechanism as InterfacesConfig / parseTokens).
BgpGlobalConfig::BgpGlobalConfig(std::vector<std::string> v)
    : utils::BaseObjectArgType<std::string>(v) {
  if (v.empty()) {
    throw std::invalid_argument(
        fmt::format(
            "Error: attribute is required. Valid attributes: {}",
            validAttrList()));
  }
  if (attrHandlers().find(v[0]) == attrHandlers().end()) {
    throw std::invalid_argument(
        fmt::format(
            "Error: unknown global attribute '{}'. Valid attributes: {}",
            v[0],
            validAttrList()));
  }
  attr_ = v[0];
  values_.assign(v.begin() + 1, v.end());
}

CmdConfigProtocolBgpGlobalTraits::RetType
CmdConfigProtocolBgpGlobal::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();

  // The attribute is guaranteed valid: BgpGlobalConfig's constructor rejects
  // an empty/unknown attribute before we get here.
  auto handler = attrHandlers().find(args.attr());
  Result result = handler->second(session.getBgpConfig(), args.values());
  if (result.ok) {
    session.saveBgpConfig();
    result.message +=
        fmt::format("\nConfig saved to: {}", session.getBgpSessionConfigPath());
  }
  return result.message;
}

void CmdConfigProtocolBgpGlobal::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdConfigProtocolBgpGlobal, CmdConfigProtocolBgpGlobalTraits>::run();

} // namespace facebook::fboss
