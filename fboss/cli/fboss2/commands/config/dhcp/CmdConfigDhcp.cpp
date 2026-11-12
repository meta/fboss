/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/dhcp/CmdConfigDhcp.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include <folly/String.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "fboss/agent/gen-cpp2/switch_config_types.h"

namespace facebook::fboss {

DhcpSourceOverrideArgs::DhcpSourceOverrideArgs(std::vector<std::string> v) {
  if (v.size() != 2) {
    throw std::invalid_argument(
        fmt::format(
            "Expected <family> <ip>, got {} argument(s). "
            "Valid families: {}, {}",
            v.size(),
            kFamilyIpv4,
            kFamilyIpv6));
  }

  family_ = normalizeDhcpFamily(v[0]);

  if (family_ == kFamilyIpv4) {
    auto maybeV4 = folly::IPAddressV4::tryFromString(v[1]);
    if (maybeV4.hasError()) {
      throw std::invalid_argument(
          fmt::format("Invalid IPv4 address '{}' for {}", v[1], kFamilyIpv4));
    }
  } else {
    auto maybeV6 = folly::IPAddressV6::tryFromString(v[1]);
    if (maybeV6.hasError()) {
      throw std::invalid_argument(
          fmt::format("Invalid IPv6 address '{}' for {}", v[1], kFamilyIpv6));
    }
  }

  ipAddress_ = v[1];
  v[0] = family_;
  data_ = std::move(v);
}

std::vector<std::string> dhcpFamilies() {
  return {
      std::string(DhcpSourceOverrideArgs::kFamilyIpv4),
      std::string(DhcpSourceOverrideArgs::kFamilyIpv6)};
}

std::string normalizeDhcpFamily(std::string family) {
  // Lower-case so "IPv4" / "IPV4" are accepted alongside the canonical
  // "ipv4", then reject anything that is not ipv4/ipv6.
  folly::toLowerAscii(family);
  if (family != DhcpSourceOverrideArgs::kFamilyIpv4 &&
      family != DhcpSourceOverrideArgs::kFamilyIpv6) {
    throw std::invalid_argument(
        fmt::format(
            "Unknown family '{}'. Valid families: {}, {}",
            family,
            DhcpSourceOverrideArgs::kFamilyIpv4,
            DhcpSourceOverrideArgs::kFamilyIpv6));
  }
  return family;
}

apache::thrift::optional_field_ref<std::string&> dhcpSourceOverrideField(
    cfg::SwitchConfig& swConfig,
    std::string_view kind,
    const std::string& family) {
  const bool isIpv4 = family == DhcpSourceOverrideArgs::kFamilyIpv4;
  if (kind == "relay") {
    return isIpv4 ? swConfig.dhcpRelaySrcOverrideV4()
                  : swConfig.dhcpRelaySrcOverrideV6();
  }
  if (kind == "reply") {
    return isIpv4 ? swConfig.dhcpReplySrcOverrideV4()
                  : swConfig.dhcpReplySrcOverrideV6();
  }
  throw std::invalid_argument(
      fmt::format("Unknown dhcp source-override kind '{}'", kind));
}

std::string applyDhcpSourceOverride(
    cfg::SwitchConfig& swConfig,
    std::string_view kind,
    const std::string& family,
    const std::string& ipAddress) {
  dhcpSourceOverrideField(swConfig, kind, family) = ipAddress;
  return fmt::format(
      "Set dhcp {}-source-override {} to {}", kind, family, ipAddress);
}

// Explicit template instantiation
template void CmdHandler<CmdConfigDhcp, CmdConfigDhcpTraits>::run();

} // namespace facebook::fboss
