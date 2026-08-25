/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/interface/dhcp/relay/CmdConfigInterfaceDhcpRelay.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include <folly/String.h>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"
#include "folly/IPAddressException.h"

namespace facebook::fboss {

namespace {

std::string parseIpv4RelayAddress(const std::string& value) {
  folly::IPAddressV4 addr;
  try {
    addr = folly::IPAddressV4(value);
  } catch (const folly::IPAddressFormatException&) {
    throw std::invalid_argument(
        fmt::format(
            "Invalid IPv4 address '{}' for {}",
            value,
            dhcp_relay_attrs::kIpAddress));
  }
  if (addr.isZero()) {
    throw std::invalid_argument(
        fmt::format(
            "0.0.0.0 disables DHCP relay; use "
            "'delete interface <intf> dhcp relay {}' instead",
            dhcp_relay_attrs::kIpAddress));
  }
  return addr.str();
}

std::string parseIpv6RelayAddress(const std::string& value) {
  folly::IPAddressV6 addr;
  try {
    addr = folly::IPAddressV6(value);
  } catch (const folly::IPAddressFormatException&) {
    throw std::invalid_argument(
        fmt::format(
            "Invalid IPv6 address '{}' for {}",
            value,
            dhcp_relay_attrs::kIpv6Address));
  }
  if (addr.isIPv4Mapped()) {
    throw std::invalid_argument(
        fmt::format(
            "IPv4-mapped address '{}' is not valid for {}; "
            "use a native IPv6 address",
            value,
            dhcp_relay_attrs::kIpv6Address));
  }
  if (addr.isZero()) {
    throw std::invalid_argument(
        fmt::format(
            ":: disables DHCPv6 relay; use "
            "'delete interface <intf> dhcp relay {}' instead",
            dhcp_relay_attrs::kIpv6Address));
  }
  return addr.str();
}

} // namespace

bool isKnownDhcpRelayAttr(const std::string& s) {
  std::string lower = s;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  return dhcpRelayAttrNames().find(lower) != dhcpRelayAttrNames().end();
}

cfg::Vlan* findVlanForInterface(
    cfg::SwitchConfig& swConfig,
    const cfg::Interface& iface) {
  if (*iface.type() != cfg::InterfaceType::VLAN) {
    return nullptr;
  }
  for (cfg::Vlan& vlan : *swConfig.vlans()) {
    if (*vlan.id() == *iface.vlanID()) {
      return &vlan;
    }
  }
  return nullptr;
}

const std::unordered_set<std::string>& dhcpRelayAttrNames() {
  // All known attribute names for `config|delete interface <intf> dhcp relay`
  static const std::unordered_set<std::string> kDhcpRelayAttrNames = {
      std::string(dhcp_relay_attrs::kIpAddress),
      std::string(dhcp_relay_attrs::kIpv6Address),
  };
  return kDhcpRelayAttrNames;
}

// ---------------------------------------------------------------------------
// DhcpRelayConfigAttrs constructor — parses the token list into attr/value
// pairs
// ---------------------------------------------------------------------------

DhcpRelayConfigAttrs::DhcpRelayConfigAttrs(const std::vector<std::string>& v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "No dhcp relay attribute provided. Valid attributes: " +
        folly::join(", ", dhcpRelayAttrNames()));
  }

  std::unordered_set<std::string> seen;
  for (size_t i = 0; i < v.size(); i += 2) {
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

    if (i + 1 >= v.size()) {
      throw std::invalid_argument(
          fmt::format("Missing address for dhcp relay attribute '{}'", v[i]));
    }

    const std::string& value = v[i + 1];
    if (isKnownDhcpRelayAttr(value)) {
      throw std::invalid_argument(
          fmt::format(
              "Missing address for dhcp relay attribute '{}'. "
              "Got another attribute '{}' instead.",
              v[i],
              value));
    }

    attributes_.emplace_back(attrLower, value);
  }
}

// ---------------------------------------------------------------------------
// CmdConfigInterfaceDhcpRelay::queryClient
// ---------------------------------------------------------------------------

CmdConfigInterfaceDhcpRelayTraits::RetType
CmdConfigInterfaceDhcpRelay::queryClient(
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

  // Phase 1: validate every attribute and build the setters, so a bad value
  // for a later attribute can't leave earlier attributes half-applied.
  std::vector<std::string> results;
  std::vector<std::function<void(cfg::Interface&, cfg::Vlan*)>> setters;

  for (const auto& [attr, value] : relayAttrs.getAttributes()) {
    if (attr == dhcp_relay_attrs::kIpAddress) {
      std::string canonical = parseIpv4RelayAddress(value);
      results.push_back(
          fmt::format("{}={}", dhcp_relay_attrs::kIpAddress, canonical));
      setters.emplace_back([canonical = std::move(canonical)](
                               cfg::Interface& iface, cfg::Vlan* vlan) {
        iface.dhcpRelayAddressV4() = canonical;
        if (vlan) {
          vlan->dhcpRelayAddressV4() = canonical;
        }
      });
    } else if (attr == dhcp_relay_attrs::kIpv6Address) {
      std::string canonical = parseIpv6RelayAddress(value);
      results.push_back(
          fmt::format("{}={}", dhcp_relay_attrs::kIpv6Address, canonical));
      setters.emplace_back([canonical = std::move(canonical)](
                               cfg::Interface& iface, cfg::Vlan* vlan) {
        iface.dhcpRelayAddressV6() = canonical;
        if (vlan) {
          vlan->dhcpRelayAddressV6() = canonical;
        }
      });
    }
  }

  // Phase 2: apply all setters to all interfaces (and their SVI VLANs).
  auto& session = ConfigSession::getInstance();
  cfg::SwitchConfig& swConfig = *session.getAgentConfig().sw();
  for (const utils::Intf& intf : interfaces) {
    cfg::Interface& iface = *intf.getInterface();
    cfg::Vlan* vlan = findVlanForInterface(swConfig, iface);
    for (const auto& setter : setters) {
      setter(iface, vlan);
    }
  }

  session.saveConfig();

  return fmt::format(
      "Successfully configured interface(s) {}: {}",
      folly::join(", ", interfaces.getNames()),
      folly::join(", ", results));
}

void CmdConfigInterfaceDhcpRelay::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdConfigInterfaceDhcpRelay,
    CmdConfigInterfaceDhcpRelayTraits>::run();

} // namespace facebook::fboss
