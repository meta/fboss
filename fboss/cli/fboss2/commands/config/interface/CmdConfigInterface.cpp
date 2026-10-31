/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/interface/CmdConfigInterface.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/String.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <thrift/lib/cpp2/FieldRef.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>
#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/cli/fboss2/commands/config/QueueConfigUtils.h"
#include "fboss/cli/fboss2/commands/config/interface/InterfaceIpUtils.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"
#include "fboss/cli/fboss2/utils/LookupClassUtils.h"
#include "fboss/lib/config/agent/PortConfigUtils.h"
#include "fboss/qsfp_service/if/gen-cpp2/QsfpService.h"

namespace facebook::fboss {

namespace {

using lookup_class::parseLookupClassId;
// Set of known attribute names (lowercase for case-insensitive comparison).
// The lldp-expected-* names come from the shared lldpAttrToTag() list so the
// config and delete commands cannot drift apart.
const std::unordered_set<std::string> kKnownAttributes = [] {
  std::unordered_set<std::string> attrs = {
      "description",
      "name",
      "mtu",
      "ip-address",
      "ipv6-address",
      "profile",
      "loopback-mode",
      "flow-control-rx",
      "flow-control-tx",
      "type",
      "shutdown",
      "no-shutdown",
      "lookup-class",
      "queue-config",
      "mirror-ingress",
      "mirror-egress",
  };
  for (const auto& name : lldpAttrNames()) {
    attrs.insert(name);
  }
  return attrs;
}();

// Attributes that take no value token (boolean/action flags)
const std::unordered_set<std::string> kValuelessAttributes = {
    "shutdown",
    "no-shutdown",
};

constexpr auto kValidConfigAttrs =
    "description, name, mtu, ip-address, ipv6-address, profile, loopback-mode, "
    "flow-control-rx, flow-control-tx, lldp-expected-*, type, shutdown, "
    "no-shutdown, lookup-class, queue-config, mirror-ingress, mirror-egress";

// The value of the `profile` attribute if the parsed attribute list configures
// one, else nullopt. Centralized (single scan) so the InterfacesConfig
// allowMissing decision and queryClient stay in sync.
std::optional<std::string> findProfileValue(
    const std::vector<std::pair<std::string, std::string>>& attributes) {
  for (const auto& [attr, value] : attributes) {
    if (attr == "profile") {
      return value;
    }
  }
  return std::nullopt;
}

} // namespace

cfg::PortProfileID parsePortProfile(const std::string& profile) {
  cfg::PortProfileID result{};
  if (!apache::thrift::TEnumTraits<cfg::PortProfileID>::findValue(
          profile, &result)) {
    throw std::invalid_argument(
        fmt::format(
            "Invalid profile value '{}': not a valid PortProfileID. "
            "Valid values include: PROFILE_10G_1_NRZ_NOFEC, "
            "PROFILE_100G_4_NRZ_RS528_COPPER, "
            "PROFILE_400G_8_PAM4_RS544X2N_OPTICAL, ... (see PortProfileID "
            "enum in switch_config.thrift)",
            profile));
  }
  return result;
}

InterfacesConfig::InterfacesConfig(const std::vector<std::string>& v)
    : InterfaceAttrArgsBase(
          kKnownAttributes,
          kValuelessAttributes,
          "attribute",
          kValidConfigAttrs) {
  auto portNames = parseTokens(v);

  // Treat repeated names as one target so each interface is configured once,
  // while retaining the command-line order for output and diagnostics.
  std::unordered_set<std::string> seenPortNames;
  portNames.erase(
      std::remove_if(
          portNames.begin(),
          portNames.end(),
          [&seenPortNames](const auto& portName) {
            return !seenPortNames.insert(portName).second;
          }),
      portNames.end());

  // A `profile` attribute can CREATE an absent-but-valid INTERFACE_PORT, so its
  // names are allowed to be missing from the running config at resolution time
  // (they are created later in queryClient). Without it, every name must
  // already resolve.
  const bool allowMissing = findProfileValue(getAttributes()).has_value();

  // Now resolve the port names to InterfaceList. Unless allowMissing is set,
  // this throws if any port is not found.
  interfaces_ = utils::InterfaceList(std::move(portNames), allowMissing);
}

std::string applyProfileImpl(
    cfg::SwitchConfig& swConfig,
    const PlatformMapping& platformMapping,
    const utility::PortProfileSupportMap& qsfpSupportedProfiles,
    const utils::InterfaceList& interfaces,
    const std::string& value,
    cli::ConfigActionLevel* actionLevel) {
  // Parse once (throws std::invalid_argument on a bad string) and thread the
  // enum through the rest of the flow.
  const cfg::PortProfileID requestedProfile = parsePortProfile(value);
  for (const auto& intf : interfaces) {
    if (intf.getPort() != nullptr) {
      continue;
    }
    PortID portID;
    try {
      portID = platformMapping.getPortID(intf.name());
    } catch (const FbossError& error) {
      throw std::invalid_argument(
          fmt::format(
              "{} is not a valid platform port: {}",
              intf.name(),
              error.what()));
    }
    if (*platformMapping.getPlatformPort(portID).mapping()->portType() !=
        cfg::PortType::INTERFACE_PORT) {
      throw std::invalid_argument(
          fmt::format("Only interface ports can be created: {}", intf.name()));
    }
  }

  std::vector<utility::PortProfileRequest> requests;
  requests.reserve(interfaces.size());
  for (const auto& portName : interfaces.getNames()) {
    requests.push_back({.portName = portName, .profileID = requestedProfile});
  }

  utility::PortProfileChangeResult result;
  try {
    result = utility::applyPortProfileChanges(
        swConfig, platformMapping, requests, qsfpSupportedProfiles);
  } catch (const FbossError& error) {
    throw std::invalid_argument(error.what());
  }

  const auto profileSpeed = result.requestedPorts.empty()
      ? cfg::PortSpeed::DEFAULT
      : result.requestedPorts.front().speed;
  std::vector<std::string> removedNames;
  removedNames.reserve(result.subsumedPorts.size());
  for (const auto& [_, portName] : result.subsumedPorts) {
    removedNames.push_back(portName);
  }
  if (!removedNames.empty()) {
    std::set<PortID> subsumedPortIDs;
    for (const auto& [portID, _] : result.subsumedPorts) {
      subsumedPortIDs.insert(portID);
    }
    utility::removePortsFromConfig(swConfig, subsumedPortIDs);

    // Removing a port must not be applied on a live agent (crashes the SwAgent
    // -- see T281221621); escalate the commit to a coldboot.
    if (actionLevel != nullptr) {
      *actionLevel = std::max(
          *actionLevel, cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART);
    }
  }

  std::vector<std::string> createdNames;
  for (const auto& request : result.requestedPorts) {
    if (!request.wasPresent) {
      createdNames.push_back(request.portName);
    }
  }

  std::string out = fmt::format(
      "profile={}, speed={}", value, static_cast<int64_t>(profileSpeed));
  if (!removedNames.empty()) {
    out += fmt::format(
        "; auto-removed subsumed port(s): {}", folly::join(", ", removedNames));
  }
  if (!createdNames.empty()) {
    out +=
        fmt::format("; created port(s): {}", folly::join(", ", createdNames));
  }
  return out;
}

std::string applyProfile(
    const HostInfo& hostInfo,
    const utils::InterfaceList& interfaces,
    const std::string& value,
    cli::ConfigActionLevel* actionLevel = nullptr) {
  cfg::PlatformMapping platformMappingConfig;
  auto agentClient =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
  agentClient->sync_getPlatformMapping(platformMappingConfig);

  utility::PortProfileSupportMap qsfpSupportedProfiles;
  try {
    auto qsfpClient =
        utils::createClient<apache::thrift::Client<QsfpService>>(hostInfo);
    qsfpClient->sync_getAllPortSupportedProfiles(qsfpSupportedProfiles, true);
  } catch (const std::exception&) {
    // QsfpService unavailable; applyPortProfileChanges falls back to platform
    // mapping support.
  }

  const PlatformMapping platformMapping(platformMappingConfig);
  return applyProfileImpl(
      *ConfigSession::getInstance().getAgentConfig().sw(),
      platformMapping,
      qsfpSupportedProfiles,
      interfaces,
      value,
      actionLevel);
}

// Configures a port as a routed (L3) port.
void configureAsRoutedPort(cfg::Port& port, cfg::SwitchConfig& swConfig) {
  port.portType() = cfg::PortType::INTERFACE_PORT;
  port.routable() = true;
  port.ingressVlan() = 0;

  const int32_t logicalId = *port.logicalID();
  auto& vps = *swConfig.vlanPorts();
  vps.erase(
      std::remove_if(
          vps.begin(),
          vps.end(),
          [logicalId](const cfg::VlanPort& vp) {
            return *vp.logicalPort() == logicalId;
          }),
      vps.end());
}

// Applies flow-control enable/disable to all ports for the given attribute
// ("flow-control-rx" or "flow-control-tx"). Returns true if any port was
// modified.
bool applyFlowControl(
    const std::string& attr,
    const std::string& value,
    const utils::InterfaceList& interfaces) {
  std::string valueLower = value;
  std::transform(
      valueLower.begin(), valueLower.end(), valueLower.begin(), ::tolower);

  bool enabled = false;
  if (valueLower == "enable") {
    enabled = true;
  } else if (valueLower == "disable") {
    enabled = false;
  } else {
    throw std::invalid_argument(
        fmt::format(
            "Invalid {} value '{}'. Valid values: enable, disable",
            attr,
            value));
  }

  bool changed = false;
  const bool isRx = (attr == "flow-control-rx");
  for (const utils::Intf& intf : interfaces) {
    cfg::Port* port = intf.getPort();
    if (port) {
      if (isRx) {
        port->pause()->rx() = enabled;
      } else {
        port->pause()->tx() = enabled;
      }
      changed = true;
    }
  }
  return changed;
}

// Validates that value is "routed-port" and applies configureAsRoutedPort.
// Returns true if any port was modified.
bool applyPortType(
    const std::string& value,
    const utils::InterfaceList& interfaces) {
  std::string valueLower = value;
  std::transform(
      valueLower.begin(), valueLower.end(), valueLower.begin(), ::tolower);

  if (valueLower != "routed-port") {
    throw std::invalid_argument(
        fmt::format(
            "Invalid type value '{}'. Valid value: routed-port", value));
  }

  bool changed = false;
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
  for (const utils::Intf& intf : interfaces) {
    cfg::Port* port = intf.getPort();
    if (port) {
      configureAsRoutedPort(*port, swConfig);
      changed = true;
    }
  }
  return changed;
}

// Parses and applies loopback-mode to all ports. Returns true if any port
// was modified.
bool applyLoopbackMode(
    const std::string& value,
    const utils::InterfaceList& interfaces) {
  // Parse loopback mode (case-insensitive)
  std::string valueLower = value;
  std::transform(
      valueLower.begin(), valueLower.end(), valueLower.begin(), ::tolower);

  cfg::PortLoopbackMode loopbackMode{cfg::PortLoopbackMode::NONE};
  if (valueLower == "none") {
    loopbackMode = cfg::PortLoopbackMode::NONE;
  } else if (valueLower == "phy") {
    loopbackMode = cfg::PortLoopbackMode::PHY;
  } else if (valueLower == "nif") {
    loopbackMode = cfg::PortLoopbackMode::NIF;
  } else if (valueLower == "mac") {
    loopbackMode = cfg::PortLoopbackMode::MAC;
  } else {
    throw std::invalid_argument(
        fmt::format(
            "Invalid loopback-mode value '{}'. Valid values: none, PHY, NIF, MAC",
            value));
  }

  bool changed = false;
  for (const utils::Intf& intf : interfaces) {
    cfg::Port* port = intf.getPort();
    if (port) {
      port->loopbackMode() = loopbackMode;
      changed = true;
    }
  }
  return changed;
}

// Parses a comma-separated list of lookup classes
// (e.g. "10" or "10,11,12,13,14"). Rejects empty slots and duplicates.
std::vector<cfg::AclLookupClass> parseLookupClassList(
    const std::string& value) {
  std::vector<std::string> parts;
  folly::split(',', value, parts, /*ignoreEmpty=*/false);

  std::vector<cfg::AclLookupClass> classes;
  for (auto& part : parts) {
    part = folly::trimWhitespace(part).str();
    if (part.empty()) {
      throw std::invalid_argument(
          fmt::format(
              "Invalid lookup-class value '{}': empty id in list", value));
    }
    auto lookupClass = parseLookupClassId(part);
    if (std::find(classes.begin(), classes.end(), lookupClass) !=
        classes.end()) {
      throw std::invalid_argument(
          fmt::format(
              "Invalid lookup-class value '{}': duplicate id {}",
              value,
              static_cast<int32_t>(lookupClass)));
    }
    classes.push_back(lookupClass);
  }
  return classes;
}

// Parses a comma-separated list of AclLookupClass ids and applies it to all
// ports, replacing any existing lookupClasses list. Returns true if any port
// was modified.
bool applyLookupClass(
    const std::string& value,
    const utils::InterfaceList& interfaces) {
  auto lookupClasses = parseLookupClassList(value);

  bool changed = false;
  for (const utils::Intf& intf : interfaces) {
    cfg::Port* port = intf.getPort();
    if (port) {
      port->lookupClasses() = lookupClasses;
      changed = true;
    }
  }
  return changed;
}

// Set the name of the L3 interface(s) targeted by the command. Since an
// interface name must be unique, only a single target interface is allowed,
// and the new name must not collide with an existing port or interface name.
// Purely-numeric names are rejected: they would shadow lookups by port
// logical ID or interface ID.
bool applyInterfaceName(
    const std::string& value,
    const utils::InterfaceList& interfaces) {
  if (value.empty()) {
    throw std::invalid_argument("Interface name cannot be empty");
  }
  if (std::all_of(value.begin(), value.end(), ::isdigit)) {
    throw std::invalid_argument(
        fmt::format(
            "Invalid interface name '{}': a purely-numeric name would "
            "conflict with lookups by port or interface ID",
            value));
  }

  std::vector<cfg::Interface*> targets;
  for (const utils::Intf& intf : interfaces) {
    cfg::Interface* interface = intf.getInterface();
    if (interface) {
      targets.push_back(interface);
    }
  }
  if (targets.size() > 1) {
    throw std::invalid_argument(
        "Cannot set the same name on multiple interfaces");
  }

  auto& portMap = ConfigSession::getInstance().getPortMap();
  if (portMap.hasPort(value)) {
    throw std::invalid_argument(
        fmt::format("'{}' is already in use as a port name", value));
  }
  cfg::Interface* existing = portMap.getInterfaceByName(value);
  if (existing && (targets.empty() || existing != targets[0])) {
    throw std::invalid_argument(
        fmt::format(
            "'{}' is already in use by interface {}",
            value,
            *existing->intfID()));
  }

  if (targets.empty()) {
    return false;
  }
  targets[0]->name() = value;
  // Refresh the name-based lookup maps so the rest of the session sees the
  // new name.
  ConfigSession::getInstance().rebuildPortMap();
  return true;
}

// Binds a named queue config to each port, or clears the binding for the
// reserved `default`.
//
// `default` is not a portQueueConfigs entry, so there is nothing to look up and
// nothing to bind: per Port::portQueueConfigName's contract a port with the
// field unset already resolves to SwitchConfig::defaultPortQueues. Selecting it
// therefore clears any existing override rather than writing a name that
// resolves to nothing.
bool applyQueueConfig(
    const std::string& value,
    const utils::InterfaceList& interfaces) {
  // Validate the name up front so a typo fails before any port is touched.
  const utils::QueueConfigName name({value});

  if (!name.isDefault()) {
    const auto& portQueueConfigs =
        *ConfigSession::getInstance().getAgentConfig().sw()->portQueueConfigs();
    if (portQueueConfigs.find(name.getName()) == portQueueConfigs.end()) {
      throw std::invalid_argument(
          fmt::format("Queue config '{}' does not exist.", name.getName()));
    }
  }

  bool changed = false;
  for (const utils::Intf& intf : interfaces) {
    cfg::Port* port = intf.getPort();
    if (port) {
      if (name.isDefault()) {
        port->portQueueConfigName().reset();
      } else {
        port->portQueueConfigName() = name.getName();
      }
      changed = true;
    }
  }
  return changed;
}

void validateMirrorAttributes(
    const std::vector<std::pair<std::string, std::string>>& attributes,
    const cfg::SwitchConfig& swConfig) {
  for (const auto& [attr, value] : attributes) {
    if (attr != "mirror-ingress" && attr != "mirror-egress") {
      continue;
    }
    const auto& mirrors = *swConfig.mirrors();
    const auto mirror = std::find_if(
        mirrors.begin(), mirrors.end(), [&](const cfg::Mirror& candidate) {
          return *candidate.name() == value;
        });
    if (mirror == mirrors.end()) {
      throw std::invalid_argument(
          fmt::format("Mirror '{}' does not exist", value));
    }
  }
}

CmdConfigInterfaceTraits::RetType CmdConfigInterface::queryClient(
    const HostInfo& hostInfo,
    const ObjectArgType& interfaceConfig) {
  const auto& interfaces = interfaceConfig.getInterfaces();
  const auto& attributes = interfaceConfig.getAttributes();

  if (interfaces.empty()) {
    throw std::invalid_argument("No interface name provided");
  }

  if (!interfaceConfig.hasAttributes()) {
    throw std::runtime_error(
        fmt::format(
            "Incomplete command. Either provide attributes ({}) "
            "or use a subcommand (switchport)",
            kValidConfigAttrs));
  }

  std::vector<std::string> results;
  bool changed = false;

  // Validate every mirror reference before applying any attribute so a bad
  // name cannot leave earlier attributes partially staged.
  const auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
  validateMirrorAttributes(attributes, swConfig);

  // The `profile` attribute mutates the port set (adjusts/removes existing
  // ports and creates absent ones), so it must be applied first, over the
  // ORIGINAL interfaces (which still carry pending Intfs with getPort()==
  // nullptr). Re-resolve afterwards so the remaining attributes see valid
  // pointers for freshly-created ports and no stale pointers to removed ones.
  const std::optional<std::string> profileValue = findProfileValue(attributes);

  utils::InterfaceList resolved(std::vector<std::string>{});
  // The commit action level required by this command, escalated by attribute
  // handlers as needed. Starts HITLESS (reloadConfig); a profile change that
  // removes a port escalates it to DISRUPTIVE_SERVICE_RESTART, because applying
  // a port removal on a live agent can crash the SwAgent
  // (LookupClassRouteUpdater dereferences the removed port's now-absent
  // interface). See T281221621.
  cli::ConfigActionLevel actionLevel = cli::ConfigActionLevel::HITLESS;
  if (profileValue.has_value()) {
    results.push_back(
        applyProfile(hostInfo, interfaces, *profileValue, &actionLevel));
    changed = true;
    ConfigSession::getInstance().rebuildPortMap();
    resolved = utils::InterfaceList(interfaces.getNames());
  }
  const utils::InterfaceList& effectiveInterfaces =
      profileValue.has_value() ? resolved : interfaces;

  for (const auto& [attr, value] : attributes) {
    if (attr == "profile") {
      continue;
    } else if (attr == "description") {
      for (const utils::Intf& intf : effectiveInterfaces) {
        cfg::Port* port = intf.getPort();
        if (port) {
          port->description() = value;
          changed = true;
        }
      }
      results.push_back(fmt::format("description=\"{}\"", value));
    } else if (attr == "name") {
      changed |= applyInterfaceName(value, effectiveInterfaces);
      results.push_back(fmt::format("name=\"{}\"", value));
    } else if (attr == "ip-address" || attr == "ipv6-address") {
      validateInterfaceIpAttr(attr, value);

      // Add IP address to all interfaces
      for (const utils::Intf& intf : effectiveInterfaces) {
        cfg::Interface* interface = intf.getInterface();
        if (interface) {
          auto& ipAddresses = *interface->ipAddresses();
          // Only add if not already present
          if (std::find(ipAddresses.begin(), ipAddresses.end(), value) ==
              ipAddresses.end()) {
            ipAddresses.push_back(value);
          }
          changed = true;
        }
      }
      results.push_back(fmt::format("{}={}", attr, value));
    } else if (attr == "mtu") {
      int32_t mtu = 0;
      try {
        mtu = folly::to<int32_t>(value);
      } catch (const std::exception&) {
        throw std::invalid_argument(
            fmt::format("Invalid MTU value '{}': must be an integer", value));
      }

      if (mtu < utils::kMtuMin || mtu > utils::kMtuMax) {
        throw std::invalid_argument(
            fmt::format(
                "MTU value {} is out of range. Valid range is {}-{}",
                mtu,
                utils::kMtuMin,
                utils::kMtuMax));
      }

      for (const utils::Intf& intf : effectiveInterfaces) {
        cfg::Interface* interface = intf.getInterface();
        if (interface) {
          interface->mtu() = mtu;
          changed = true;
        }
      }
      results.push_back(fmt::format("mtu={}", mtu));
    } else if (attr == "loopback-mode") {
      changed |= applyLoopbackMode(value, effectiveInterfaces);
      results.push_back(fmt::format("loopback-mode={}", value));
    } else if (attr == "flow-control-rx" || attr == "flow-control-tx") {
      changed |= applyFlowControl(attr, value, effectiveInterfaces);
      results.push_back(fmt::format("{}={}", attr, value));
    } else if (auto lldpTag = lldpTagForAttr(attr); lldpTag.has_value()) {
      if (value.empty()) {
        throw std::invalid_argument(fmt::format("{} cannot be empty", attr));
      }
      for (const utils::Intf& intf : effectiveInterfaces) {
        cfg::Port* port = intf.getPort();
        if (port) {
          port->expectedLLDPValues()[*lldpTag] = value;
          changed = true;
        }
      }
      results.push_back(fmt::format("{}=\"{}\"", attr, value));
    } else if (attr == "type") {
      changed |= applyPortType(value, effectiveInterfaces);
      results.push_back(fmt::format("type={}", value));
    } else if (attr == "lookup-class") {
      changed |= applyLookupClass(value, effectiveInterfaces);
      results.push_back(fmt::format("lookup-class={}", value));
    } else if (attr == "shutdown") {
      for (const utils::Intf& intf : effectiveInterfaces) {
        cfg::Port* port = intf.getPort();
        if (port) {
          port->state() = cfg::PortState::DISABLED;
          changed = true;
        }
      }
      results.emplace_back("state=disabled");
    } else if (attr == "no-shutdown") {
      for (const utils::Intf& intf : effectiveInterfaces) {
        cfg::Port* port = intf.getPort();
        if (port) {
          port->state() = cfg::PortState::ENABLED;
          changed = true;
        }
      }
      results.emplace_back("state=enabled");
    } else if (attr == "queue-config") {
      changed |= applyQueueConfig(value, effectiveInterfaces);
      results.push_back(fmt::format("queue-config={}", value));
    } else if (attr == "mirror-ingress" || attr == "mirror-egress") {
      for (const utils::Intf& intf : effectiveInterfaces) {
        cfg::Port* port = intf.getPort();
        if (!port) {
          continue;
        }
        if (attr == "mirror-ingress") {
          if (!port->ingressMirror().has_value() ||
              *port->ingressMirror() != value) {
            port->ingressMirror() = value;
            changed = true;
          }
        } else if (
            !port->egressMirror().has_value() ||
            *port->egressMirror() != value) {
          port->egressMirror() = value;
          changed = true;
        }
      }
      results.push_back(fmt::format("{}={}", attr, value));
    }
  }

  // Save the updated config (skip the write/commit cycle if no port or
  // interface was actually modified). Commit at the escalated action level: a
  // profile change that removed a port coldboots the agent (T281221621);
  // everything else stays a hitless reloadConfig.
  if (changed) {
    ConfigSession::getInstance().saveConfig(
        cli::ServiceType::AGENT, actionLevel);
  }

  std::string interfaceList = folly::join(", ", interfaces.getNames());
  std::string attrList = folly::join(", ", results);
  return fmt::format(
      "Successfully configured interface(s) {}: {}", interfaceList, attrList);
}

void CmdConfigInterface::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<CmdConfigInterface, CmdConfigInterfaceTraits>::run();

} // namespace facebook::fboss
