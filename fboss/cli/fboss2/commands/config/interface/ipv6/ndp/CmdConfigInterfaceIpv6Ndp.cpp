/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/interface/ipv6/ndp/CmdConfigInterfaceIpv6Ndp.h"
#include "fboss/cli/fboss2/utils/ArgCompletion.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/IPAddressV6.h>
#include <folly/String.h>
#include <folly/Utility.h>
#include <algorithm>
#include <cctype>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"

namespace facebook::fboss {

namespace {

// Attributes that consume no value token
const std::unordered_set<std::string> kValuelessNdpAttrs = {
    "managed-config-flag",
    "other-config-flag",
};

bool isKnownNdpAttr(const std::string& s) {
  std::string lower = s;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  return ndpAttrNames().find(lower) != ndpAttrNames().end();
}

// Ensure the optional NDP config is present on the interface; return a ref.
cfg::NdpConfig& ensureNdp(cfg::Interface& iface) {
  if (!iface.ndp().has_value()) {
    iface.ndp() = cfg::NdpConfig{};
  }
  return *iface.ndp();
}

int32_t parseIntInRange(
    const std::string& attr,
    const std::string& value,
    int32_t min,
    int32_t max) {
  int32_t parsed = 0;
  try {
    parsed = folly::to<int32_t>(value);
  } catch (const std::exception&) {
    throw std::invalid_argument(
        fmt::format("Invalid {} value '{}': must be an integer", attr, value));
  }
  if (parsed < min || parsed > max) {
    std::string range = max == std::numeric_limits<int32_t>::max()
        ? fmt::format(">= {}", min)
        : fmt::format("{}-{}", min, max);
    throw std::invalid_argument(
        fmt::format(
            "{} value {} is out of range. Valid range: {}",
            attr,
            parsed,
            range));
  }
  return parsed;
}

int32_t parseNonNegativeSeconds(
    const std::string& attr,
    const std::string& value) {
  return parseIntInRange(attr, value, 0, std::numeric_limits<int32_t>::max());
}

folly::IPAddressV6 parseIpv6Address(const std::string& value) {
  try {
    folly::IPAddressV6 addr(value);
    if (addr.isIPv4Mapped()) {
      throw std::invalid_argument(
          fmt::format(
              "IPv4-mapped address '{}' is not valid for ra-address; "
              "use a native IPv6 address",
              value));
    }
    return addr;
  } catch (const folly::IPAddressFormatException&) {
    throw std::invalid_argument(
        fmt::format("Invalid IPv6 address '{}' for ra-address", value));
  }
}

} // namespace

const std::unordered_set<std::string>& ndpAttrNames() {
  // All known attribute names for `config|delete interface <intf> ipv6 ndp`
  static const std::unordered_set<std::string> kNdpAttrNames = {
      "ra-interval",
      "ra-lifetime",
      "hop-limit",
      "prefix-valid-lifetime",
      "prefix-preferred-lifetime",
      "managed-config-flag",
      "other-config-flag",
      "ra-address",
  };
  return kNdpAttrNames;
}

// ---------------------------------------------------------------------------
// NdpConfigAttrs constructor — parses the token list into attr/value pairs
// ---------------------------------------------------------------------------

NdpConfigAttrs::NdpConfigAttrs(std::vector<std::string> v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "No NDP attribute provided. Valid attributes: " +
        folly::join(", ", ndpAttrNames()));
  }

  std::unordered_set<std::string> seen;
  for (size_t i = 0; i < v.size();) {
    std::string attrLower = v[i];
    std::transform(
        attrLower.begin(), attrLower.end(), attrLower.begin(), ::tolower);

    if (!isKnownNdpAttr(attrLower)) {
      throw std::invalid_argument(
          fmt::format(
              "Unknown ndp attribute '{}'. Valid attributes: {}",
              v[i],
              folly::join(", ", ndpAttrNames())));
    }

    if (!seen.insert(attrLower).second) {
      throw std::invalid_argument(
          fmt::format("Duplicate ndp attribute '{}'", attrLower));
    }

    if (kValuelessNdpAttrs.count(attrLower)) {
      attributes_.emplace_back(attrLower, "");
      ++i;
      continue;
    }

    if (i + 1 >= v.size()) {
      throw std::invalid_argument(
          fmt::format("Missing value for ndp attribute '{}'", v[i]));
    }

    const std::string& value = v[i + 1];

    if (isKnownNdpAttr(value)) {
      throw std::invalid_argument(
          fmt::format(
              "Missing value for ndp attribute '{}'. Got another attribute '{}' instead.",
              v[i],
              value));
    }

    attributes_.emplace_back(attrLower, value);
    i += 2;
  }
}

// ---------------------------------------------------------------------------
// CmdConfigInterfaceIpv6Ndp::queryClient
// ---------------------------------------------------------------------------

CmdConfigInterfaceIpv6NdpTraits::RetType CmdConfigInterfaceIpv6Ndp::queryClient(
    const HostInfo& /* hostInfo */,
    const utils::InterfaceList& interfaces,
    const ObjectArgType& ndpAttrs) {
  if (interfaces.empty()) {
    throw std::invalid_argument("No interface name provided");
  }

  // NDP is an L3-interface property; reject names that resolved to a port
  // with no associated L3 interface rather than silently skipping them.
  std::vector<std::string> noL3Interface;
  for (const utils::Intf& intf : interfaces) {
    if (!intf.getInterface()) {
      noL3Interface.push_back(intf.name());
    }
  }
  if (!noL3Interface.empty()) {
    throw std::invalid_argument(
        fmt::format(
            "No L3 interface in configuration for: {}. "
            "NDP is configured on L3 interfaces.",
            folly::join(", ", noL3Interface)));
  }

  // Phase 1: validate every attribute and build the setters, so a bad value
  // for a later attribute can't leave earlier attributes half-applied.
  std::vector<std::string> results;
  std::vector<std::function<void(cfg::NdpConfig&)>> setters;

  for (const auto& [attr, value] : ndpAttrs.getAttributes()) {
    if (attr == "ra-interval") {
      int32_t seconds = parseNonNegativeSeconds("ra-interval", value);
      setters.emplace_back([seconds](cfg::NdpConfig& ndp) {
        ndp.routerAdvertisementSeconds() = seconds;
      });
      results.push_back(fmt::format("ra-interval={}", seconds));

    } else if (attr == "ra-lifetime") {
      int32_t seconds = parseNonNegativeSeconds("ra-lifetime", value);
      setters.emplace_back(
          [seconds](cfg::NdpConfig& ndp) { ndp.routerLifetime() = seconds; });
      results.push_back(fmt::format("ra-lifetime={}", seconds));

    } else if (attr == "hop-limit") {
      int32_t hopLimit = parseIntInRange("hop-limit", value, 0, 255);
      setters.emplace_back(
          [hopLimit](cfg::NdpConfig& ndp) { ndp.curHopLimit() = hopLimit; });
      results.push_back(fmt::format("hop-limit={}", hopLimit));

    } else if (attr == "prefix-valid-lifetime") {
      int32_t seconds = parseNonNegativeSeconds("prefix-valid-lifetime", value);
      setters.emplace_back([seconds](cfg::NdpConfig& ndp) {
        ndp.prefixValidLifetimeSeconds() = seconds;
      });
      results.push_back(fmt::format("prefix-valid-lifetime={}", seconds));

    } else if (attr == "prefix-preferred-lifetime") {
      int32_t seconds =
          parseNonNegativeSeconds("prefix-preferred-lifetime", value);
      setters.emplace_back([seconds](cfg::NdpConfig& ndp) {
        ndp.prefixPreferredLifetimeSeconds() = seconds;
      });
      results.push_back(fmt::format("prefix-preferred-lifetime={}", seconds));

    } else if (attr == "managed-config-flag") {
      setters.emplace_back([](cfg::NdpConfig& ndp) {
        ndp.routerAdvertisementManagedBit() = true;
      });
      results.emplace_back("managed-config-flag=true");

    } else if (attr == "other-config-flag") {
      setters.emplace_back([](cfg::NdpConfig& ndp) {
        ndp.routerAdvertisementOtherBit() = true;
      });
      results.emplace_back("other-config-flag=true");

    } else if (attr == "ra-address") {
      folly::IPAddressV6 addr = parseIpv6Address(value);
      const std::string canonical = addr.str();
      // The setter is applied once per interface, so each invocation needs
      // its own copy of the address; folly::copy makes the intentional copy
      // explicit (and a move into the Thrift field).
      setters.emplace_back([canonical](cfg::NdpConfig& ndp) {
        ndp.routerAddress() = folly::copy(canonical);
      });
      results.push_back(fmt::format("ra-address={}", canonical));
    }
  }

  // Phase 2: apply all setters to all interfaces.
  for (const utils::Intf& intf : interfaces) {
    cfg::NdpConfig& ndp = ensureNdp(*intf.getInterface());
    for (const auto& setter : setters) {
      setter(ndp);
    }
  }

  ConfigSession::getInstance().saveConfig();

  std::string interfaceList = folly::join(", ", interfaces.getNames());
  std::string attrList = folly::join(", ", results);
  return fmt::format(
      "Successfully configured interface(s) {}: {}", interfaceList, attrList);
}

void CmdConfigInterfaceIpv6Ndp::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdConfigInterfaceIpv6Ndp, CmdConfigInterfaceIpv6NdpTraits>::run();

std::vector<std::string> CmdConfigInterfaceIpv6NdpTraits::completeArgs(
    const std::vector<std::string>& typed) {
  return utils::completion::completeAttrGrammar(
      typed,
      {.attrs = utils::completion::toWords(ndpAttrNames()),
       .valueless = {kValuelessNdpAttrs.begin(), kValuelessNdpAttrs.end()}});
}

} // namespace facebook::fboss
