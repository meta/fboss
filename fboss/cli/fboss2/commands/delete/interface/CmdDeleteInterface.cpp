/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/interface/CmdDeleteInterface.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/String.h>
#include <algorithm>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/interface/InterfaceIpUtils.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"
#include "fboss/lib/config/agent/PortConfigUtils.h"

namespace facebook::fboss {

namespace {

// Valueless delete attributes: reset a port/interface attribute to default.
// The lldp-expected-* names come from the shared lldpAttrToTag() list so the
// config and delete commands cannot drift apart.
const std::unordered_set<std::string> kValuelessDeleteAttributes = [] {
  std::unordered_set<std::string> attrs = {
      "description", "loopback-mode", "lookup-class", "mtu", "queue-config"};
  for (const auto& name : lldpAttrNames()) {
    attrs.insert(name);
  }
  return attrs;
}();

// All known attributes: valueless resets + valued IP address removals.
const std::unordered_set<std::string> kKnownDeleteAttributes = [] {
  std::unordered_set<std::string> attrs = kValuelessDeleteAttributes;
  attrs.insert("ip-address");
  attrs.insert("ipv6-address");
  return attrs;
}();

const std::string kValidDeleteAttrs = fmt::format(
    "description, loopback-mode, lookup-class, mtu, queue-config, {}, ip-address, ipv6-address",
    folly::join(", ", lldpAttrNames()));

} // namespace

InterfaceDeleteConfig::InterfaceDeleteConfig(const std::vector<std::string>& v)
    : InterfaceAttrArgsBase(
          kKnownDeleteAttributes,
          kValuelessDeleteAttributes,
          "delete attribute",
          kValidDeleteAttrs) {
  auto portNames = parseTokens(v);

  // If no known attribute delimited the port list, catch a mistyped attribute:
  // interface names always contain '/' (e.g. "eth1/1/1"); attribute names never
  // do but do contain '-'.
  if (attributes_.empty()) {
    for (const auto& tok : portNames) {
      if (tok.find('-') != std::string::npos &&
          tok.find('/') == std::string::npos) {
        throw std::invalid_argument(
            fmt::format(
                "Unknown delete attribute '{}'. Valid attributes are: {}",
                tok,
                kValidDeleteAttrs));
      }
    }
  }

  // Validate ip-address / ipv6-address values as CIDR networks.
  for (const auto& [attr, value] : attributes_) {
    if (attr == "ip-address" || attr == "ipv6-address") {
      validateInterfaceIpAttr(attr, value);
    }
  }

  // Resolve port names to InterfaceList (throws if any port is not found).
  interfaces_ = utils::InterfaceList(std::move(portNames));
}

CmdDeleteInterfaceTraits::RetType CmdDeleteInterface::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& deleteConfig) {
  const auto& interfaces = deleteConfig.getInterfaces();
  const auto& attributes = deleteConfig.getAttributes();

  if (interfaces.empty()) {
    throw std::invalid_argument("No interface name provided");
  }

  // No attributes => delete the whole port(s) from the config.
  if (attributes.empty()) {
    auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
    std::set<PortID> portsToDelete;
    std::vector<std::string> deletedNames;
    for (const utils::Intf& intf : interfaces) {
      const cfg::Port* port = intf.getPort();
      if (!port) {
        continue;
      }
      portsToDelete.insert(PortID(*port->logicalID()));
      deletedNames.push_back(intf.name());
    }
    if (portsToDelete.empty()) {
      throw std::invalid_argument(
          "No port found for the specified interface(s)");
    }
    utility::removePortsFromConfig(
        swConfig,
        portsToDelete,
        utility::PortRemovalMode::Erase,
        /*pruneEmptyVlansAndInterfaces=*/true);
    // Removing a port is a HITLESS change: the agent's reloadConfig() applies
    // the port-set delta live, matching how 'config interface <port> profile'
    // adds/removes ports. No agent warmboot is needed.
    ConfigSession::getInstance().saveConfig();
    return fmt::format(
        "Deleted interface(s): {}", folly::join(", ", deletedNames));
  }

  std::vector<std::string> results;
  bool changed = false;

  for (const auto& [attr, value] : attributes) {
    if (attr == "ip-address" || attr == "ipv6-address") {
      // Remove a specific IP address from each interface's ipAddresses list.
      bool expectV6 = (attr == "ipv6-address");
      std::vector<std::string> doneNames;
      std::vector<std::string> missingNames;
      for (const utils::Intf& intf : interfaces) {
        cfg::Interface* iface = intf.getInterface();
        if (!iface) {
          missingNames.push_back(intf.name());
          continue;
        }
        auto& ipAddresses = *iface->ipAddresses();
        auto it = std::find(ipAddresses.begin(), ipAddresses.end(), value);
        if (it != ipAddresses.end()) {
          ipAddresses.erase(it);
          changed = true;
          doneNames.push_back(intf.name());
        } else {
          results.push_back(
              fmt::format(
                  "{} {} not configured on interface {}",
                  expectV6 ? "IPv6 address" : "IP address",
                  value,
                  intf.name()));
        }
      }
      if (!doneNames.empty()) {
        results.push_back(
            fmt::format(
                "Successfully removed {} {} from interface(s): {}",
                expectV6 ? "IPv6 address" : "IP address",
                value,
                folly::join(", ", doneNames)));
      }
      if (!missingNames.empty()) {
        results.push_back(
            fmt::format(
                "No interface config found for: {}",
                folly::join(", ", missingNames)));
      }
    } else if (attr == "mtu") {
      // Interface-level reset: mtu is an optional field, and the agent falls
      // back to Interface::kDefaultMtu when it is unset.
      std::vector<std::string> resetNames;
      std::vector<std::string> missingNames;
      for (const utils::Intf& intf : interfaces) {
        cfg::Interface* iface = intf.getInterface();
        if (!iface) {
          missingNames.push_back(intf.name());
          continue;
        }
        if (iface->mtu().has_value()) {
          iface->mtu().reset();
          changed = true;
        }
        resetNames.push_back(intf.name());
      }
      if (!resetNames.empty()) {
        results.push_back(
            fmt::format(
                "Successfully reset attribute 'mtu' for interface(s): {}",
                folly::join(", ", resetNames)));
      }
      if (!missingNames.empty()) {
        results.push_back(
            fmt::format(
                "No interface config found for: {}",
                folly::join(", ", missingNames)));
      }
    } else {
      // Port-level valueless reset (description, loopback-mode, lookup-class,
      // queue-config, lldp-expected-*).
      std::vector<std::string> resetNames;
      std::vector<std::string> skippedNames;
      for (const utils::Intf& intf : interfaces) {
        cfg::Port* port = intf.getPort();
        if (!port) {
          skippedNames.push_back(intf.name());
          continue;
        }
        if (attr == "description") {
          if (port->description().has_value()) {
            port->description().reset();
            changed = true;
          }
        } else if (attr == "loopback-mode") {
          if (*port->loopbackMode() != cfg::PortLoopbackMode::NONE) {
            port->loopbackMode() = cfg::PortLoopbackMode::NONE;
            changed = true;
          }
        } else if (attr == "lookup-class") {
          if (!port->lookupClasses()->empty()) {
            port->lookupClasses()->clear();
            changed = true;
          }
        } else if (attr == "queue-config") {
          // Same reset as `config interface <intf> queue-config default`: an
          // unset portQueueConfigName resolves to
          // SwitchConfig::defaultPortQueues.
          if (port->portQueueConfigName().has_value()) {
            port->portQueueConfigName().reset();
            changed = true;
          }
        } else if (auto tag = lldpTagForAttr(attr); tag.has_value()) {
          changed |= port->expectedLLDPValues()->erase(*tag) > 0;
        }
        resetNames.push_back(intf.name());
      }

      if (resetNames.empty()) {
        results.push_back(
            fmt::format(
                "Attribute '{}' not reset: no port found for interface(s) {}",
                attr,
                folly::join(", ", skippedNames)));
      } else if (skippedNames.empty()) {
        results.push_back(
            fmt::format(
                "Successfully reset attribute '{}' for interface(s): {}",
                attr,
                folly::join(", ", resetNames)));
      } else {
        results.push_back(
            fmt::format(
                "Successfully reset attribute '{}' for interface(s): {}; "
                "no port found for {}",
                attr,
                folly::join(", ", resetNames),
                folly::join(", ", skippedNames)));
      }
    }
  }

  if (changed) {
    ConfigSession::getInstance().saveConfig();
  }

  return folly::join("\n", results);
}

void CmdDeleteInterface::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<CmdDeleteInterface, CmdDeleteInterfaceTraits>::run();

} // namespace facebook::fboss
