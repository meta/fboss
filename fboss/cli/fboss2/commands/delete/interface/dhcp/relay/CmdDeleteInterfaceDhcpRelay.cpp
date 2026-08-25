/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/interface/dhcp/relay/CmdDeleteInterfaceDhcpRelay.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/IPAddress.h>
#include <folly/String.h>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/interface/dhcp/relay/CmdConfigInterfaceDhcpRelay.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"

namespace facebook::fboss {

namespace {

// Canonicalize for comparison; returns the input unchanged if it does not
// parse (the comparison will then simply not match).
std::string canonicalOrOriginal(const std::string& addr) {
  auto parsed = folly::IPAddress::tryFromString(addr);
  return parsed.hasValue() ? parsed->str() : addr;
}

} // namespace

// ---------------------------------------------------------------------------
// DhcpRelayDeleteAttrs constructor — attrs with an optional address token
// ---------------------------------------------------------------------------

DhcpRelayDeleteAttrs::DhcpRelayDeleteAttrs(const std::vector<std::string>& v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "No dhcp relay attribute provided. Valid attributes: " +
        folly::join(", ", dhcpRelayAttrNames()));
  }

  std::unordered_set<std::string> seen;
  for (size_t i = 0; i < v.size();) {
    std::string attrLower = v[i];
    std::transform(
        attrLower.begin(), attrLower.end(), attrLower.begin(), ::tolower);

    if (!isKnownDhcpRelayAttr(attrLower)) {
      throw std::invalid_argument(
          fmt::format(
              "Unknown dhcp relay attribute '{}'. Valid attributes: {}",
              v[i],
              folly::join(", ", dhcpRelayAttrNames())));
    }

    if (!seen.insert(attrLower).second) {
      throw std::invalid_argument(
          fmt::format("Duplicate dhcp relay attribute '{}'", attrLower));
    }

    // The next token, when present and not another attribute name, is the
    // address the caller expects to be deleting.
    std::string expected;
    if (i + 1 < v.size() && !isKnownDhcpRelayAttr(v[i + 1])) {
      expected = v[i + 1];
      i += 2;
    } else {
      ++i;
    }
    attributes_.emplace_back(attrLower, expected);
  }
}

// ---------------------------------------------------------------------------
// CmdDeleteInterfaceDhcpRelay::queryClient
// ---------------------------------------------------------------------------

CmdDeleteInterfaceDhcpRelayTraits::RetType
CmdDeleteInterfaceDhcpRelay::queryClient(
    const HostInfo& /* hostInfo */,
    const utils::InterfaceList& interfaces,
    const ObjectArgType& relayAttrs) {
  if (interfaces.empty()) {
    throw std::invalid_argument("No interface name provided");
  }

  // DHCP relay is an L3-interface property; reject names that resolved to a
  // port with no associated L3 interface rather than silently skipping them.
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
            "DHCP relay is configured on L3 interfaces.",
            folly::join(", ", noL3Interface)));
  }

  auto& session = ConfigSession::getInstance();
  cfg::SwitchConfig& swConfig = *session.getAgentConfig().sw();

  // Phase 1: when an expected address was given, verify it matches what is
  // configured on every target interface before clearing anything.
  for (const utils::Intf& intf : interfaces) {
    cfg::Interface& iface = *intf.getInterface();
    for (const auto& [attr, expected] : relayAttrs.getAttributes()) {
      if (expected.empty()) {
        continue;
      }
      const auto& configured = attr == dhcp_relay_attrs::kIpAddress
          ? iface.dhcpRelayAddressV4()
          : iface.dhcpRelayAddressV6();
      if (!configured.has_value() ||
          canonicalOrOriginal(*configured) != canonicalOrOriginal(expected)) {
        throw std::invalid_argument(
            fmt::format(
                "dhcp relay {} on interface {} is '{}', not '{}'; "
                "nothing deleted",
                attr,
                intf.name(),
                configured.has_value() ? *configured : "<unset>",
                expected));
      }
    }
  }

  // Phase 2: clear the relay destination on the interfaces and their VLANs.
  bool modified = false;
  for (const utils::Intf& intf : interfaces) {
    cfg::Interface& iface = *intf.getInterface();
    cfg::Vlan* vlan = findVlanForInterface(swConfig, iface);
    for (const auto& [attr, expected] : relayAttrs.getAttributes()) {
      if (attr == dhcp_relay_attrs::kIpAddress) {
        modified |= iface.dhcpRelayAddressV4().has_value();
        iface.dhcpRelayAddressV4().reset();
        if (vlan) {
          modified |= vlan->dhcpRelayAddressV4().has_value();
          vlan->dhcpRelayAddressV4().reset();
        }
      } else if (attr == dhcp_relay_attrs::kIpv6Address) {
        modified |= iface.dhcpRelayAddressV6().has_value();
        iface.dhcpRelayAddressV6().reset();
        if (vlan) {
          modified |= vlan->dhcpRelayAddressV6().has_value();
          vlan->dhcpRelayAddressV6().reset();
        }
      }
    }
  }

  if (!modified) {
    return fmt::format(
        "No DHCP relay destination configured on interface(s) {}; "
        "nothing to delete",
        folly::join(", ", interfaces.getNames()));
  }

  session.saveConfig();

  std::vector<std::string> attrNames;
  for (const auto& [attr, expected] : relayAttrs.getAttributes()) {
    attrNames.push_back(attr);
  }
  return fmt::format(
      "Successfully deleted dhcp relay on interface(s) {}: {}",
      folly::join(", ", interfaces.getNames()),
      folly::join(", ", attrNames));
}

void CmdDeleteInterfaceDhcpRelay::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdDeleteInterfaceDhcpRelay,
    CmdDeleteInterfaceDhcpRelayTraits>::run();

} // namespace facebook::fboss
