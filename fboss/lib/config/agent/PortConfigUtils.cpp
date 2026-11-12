/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/lib/config/agent/PortConfigUtils.h"

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <thrift/lib/cpp/util/EnumUtils.h>

#include "fboss/agent/FbossError.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/lib/config/agent/InterfaceConfigUtils.h"
#include "fboss/lib/config/agent/VlanConfigUtils.h"

namespace facebook::fboss::utility {
namespace {

std::optional<cfg::PortSpeed> getProfileSpeed(
    const PlatformMapping& platformMapping,
    cfg::PortProfileID profileID,
    const std::vector<PortID>& ports) {
  for (const auto& portID : ports) {
    const auto& supportedProfiles =
        *platformMapping.getPlatformPort(portID).supportedProfiles();
    if (supportedProfiles.find(profileID) == supportedProfiles.end()) {
      continue;
    }
    if (const auto profileConfig = platformMapping.getPortProfileConfig(
            PlatformPortProfileConfigMatcher(profileID, portID))) {
      return *profileConfig->speed();
    }
  }
  return std::nullopt;
}

bool isOpticalProfile(cfg::PortProfileID profileID) {
  constexpr std::string_view kOpticalSuffix = "_OPTICAL";
  const auto profileName = apache::thrift::util::enumNameSafe(profileID);
  return profileName.ends_with(kOpticalSuffix);
}

bool isRequiredPort(
    PortID portID,
    const std::optional<std::set<PortID>>& requiredPorts) {
  return !requiredPorts || requiredPorts->find(portID) != requiredPorts->end();
}

void assignGroupProfile(
    PortProfileMap& portToProfileIDs,
    const std::vector<PortID>& ports,
    cfg::PortProfileID profileID,
    const SafeProfileSelectionOptions& options) {
  // Add/remove platforms may omit ports outside the required port set.
  for (const auto& portID : ports) {
    if (options.supportsAddRemovePort && options.requiredPorts &&
        !isRequiredPort(portID, options.requiredPorts)) {
      continue;
    }
    portToProfileIDs.emplace(portID, profileID);
  }
}

} // namespace

PortProfileMap getSafeProfileIDs(
    const PlatformMapping& platformMapping,
    const std::map<PortID, std::vector<PortID>>&
        controllingPortToSubsidiaryPorts,
    const SafeProfileSelectionOptions& options) {
  // For each controlling-port group, this function:
  // 1. Collects profiles that do not subsume another required port.
  // 2. Applies any ASIC-specific fixed speed or profile requirement.
  // 3. Otherwise selects the fastest safe profile, preferring optical for an
  //    equal-speed interface or management port.
  // 4. Assigns that profile to every port which must remain in the config.
  PortProfileMap portToProfileIDs;
  const auto& platformEntries = platformMapping.getPlatformPorts();
  for (const auto& [controllingPort, ports] :
       controllingPortToSubsidiaryPorts) {
    // Find the safe profiles that can satisfy all required ports in the group.
    std::set<cfg::PortProfileID> safeProfiles;
    for (const auto& portID : ports) {
      const auto portEntry = platformEntries.find(portID);
      if (portEntry == platformEntries.end()) {
        throw FbossError("Port ", portID, " does not exist in PlatformMapping");
      }
      for (const auto& [profileID, profile] :
           *portEntry->second.supportedProfiles()) {
        auto subsumedPorts = profile.subsumedPorts();
        // A higher-speed profile is safe when its subsumed ports do not
        // overlap the group, or when the platform supports adding/removing
        // ports and none of the subsumed ports are required.
        const auto conflictsWithRequiredPort = subsumedPorts &&
            std::any_of(subsumedPorts->begin(),
                        subsumedPorts->end(),
                        [&](const auto subsumedPort) {
                          const auto port = PortID(subsumedPort);
                          return std::find(ports.begin(), ports.end(), port) !=
                              ports.end() &&
                              (!options.supportsAddRemovePort ||
                               isRequiredPort(port, options.requiredPorts));
                        });
        if (!conflictsWithRequiredPort) {
          // Profiles without subsumed ports are inherently safe; profiles
          // with subsumed ports reach here only when no required port overlaps.
          safeProfiles.insert(profileID);
        }
      }
    }

    if (safeProfiles.empty()) {
      std::string portSet;
      for (const auto& portID : ports) {
        if (!portSet.empty()) {
          portSet += ", ";
        }
        portSet += std::to_string(static_cast<int32_t>(portID));
      }
      throw FbossError("Can't find safe profiles for ports: ", portSet);
    }

    auto bestSpeed = cfg::PortSpeed::DEFAULT;
    auto bestProfile = cfg::PortProfileID::PROFILE_DEFAULT;
    const auto controllingPortEntry = platformEntries.find(controllingPort);
    if (controllingPortEntry == platformEntries.end()) {
      throw FbossError(
          "Controlling port ",
          controllingPort,
          " does not exist in PlatformMapping");
    }
    const auto portType = *controllingPortEntry->second.mapping()->portType();
    if (portType == cfg::PortType::HYPER_PORT &&
        safeProfiles.contains(cfg::PortProfileID::PROFILE_DEFAULT)) {
      // Hyper ports are configured with PROFILE_DEFAULT.
      assignGroupProfile(
          portToProfileIDs,
          ports,
          cfg::PortProfileID::PROFILE_DEFAULT,
          options);
      continue;
    }
    if ((options.asicType == cfg::AsicType::ASIC_TYPE_JERICHO3 ||
         options.asicType == cfg::AsicType::ASIC_TYPE_JERICHO4) &&
        options.dualStageRdsw3q2q &&
        portType == cfg::PortType::INTERFACE_PORT) {
      // The dual-stage RDSW 3Q2Q chip config uses 400G NIF ports, and Jericho3
      // does not yet support changing port speed dynamically.
      bestSpeed = cfg::PortSpeed::FOURHUNDREDG;
    } else if (
        options.asicType == cfg::AsicType::ASIC_TYPE_CHENAB &&
        portType == cfg::PortType::INTERFACE_PORT) {
      // Chenab production configs use this 400G profile. Changing profiles
      // may recreate ports through delete/add, which Chenab does not support.
      // Minipack3N also has a maximum port speed of 400G.
      bestSpeed = cfg::PortSpeed::FOURHUNDREDG;
      bestProfile = cfg::PortProfileID::PROFILE_400G_4_PAM4_RS544X2N_OPTICAL;
    }

    // Without a fixed speed requirement, select the fastest safe profile.
    // When requested for routed front-panel ports, prefer optical when
    // profiles have the same speed. Copper remains the fallback.
    const auto pickMaxSpeed = bestSpeed == cfg::PortSpeed::DEFAULT;
    const auto preferOptical = options.preferOpticalProfiles &&
        (portType == cfg::PortType::INTERFACE_PORT ||
         portType == cfg::PortType::MANAGEMENT_PORT);
    if (bestProfile == cfg::PortProfileID::PROFILE_DEFAULT) {
      for (const auto profileID : safeProfiles) {
        const auto speed = getProfileSpeed(platformMapping, profileID, ports);
        if (!speed) {
          throw FbossError(
              "Can't resolve speed for profile ",
              profileID,
              " in controlling-port group ",
              controllingPort);
        }
        const auto candidateBySpeed =
            (pickMaxSpeed &&
             static_cast<int>(bestSpeed) < static_cast<int>(*speed)) ||
            (!pickMaxSpeed && *speed == bestSpeed);
        if (!preferOptical) {
          if (candidateBySpeed) {
            bestSpeed = *speed;
            bestProfile = profileID;
          }
          continue;
        }

        const auto firstAtFixedSpeed = !pickMaxSpeed && *speed == bestSpeed &&
            bestProfile == cfg::PortProfileID::PROFILE_DEFAULT;
        const auto opticalTie =
            bestProfile != cfg::PortProfileID::PROFILE_DEFAULT &&
            *speed == bestSpeed && isOpticalProfile(profileID) &&
            !isOpticalProfile(bestProfile);
        if ((pickMaxSpeed && candidateBySpeed) || firstAtFixedSpeed ||
            opticalTie) {
          bestSpeed = *speed;
          bestProfile = profileID;
        }
      }
    } else if (
        safeProfiles.find(bestProfile) == safeProfiles.end() ||
        getProfileSpeed(platformMapping, bestProfile, ports) != bestSpeed) {
      throw FbossError(
          "Profile ",
          bestProfile,
          " is not a safe profile at speed ",
          bestSpeed,
          " for controlling-port group ",
          controllingPort);
    }
    if (bestProfile == cfg::PortProfileID::PROFILE_DEFAULT) {
      throw FbossError(
          "Can't find a safe profile at speed ",
          bestSpeed,
          " for controlling-port group ",
          controllingPort);
    }

    assignGroupProfile(portToProfileIDs, ports, bestProfile, options);
  }
  return portToProfileIDs;
}

namespace {

void validateProfileSupport(
    const PortProfileRequest& request,
    const std::vector<cfg::PortProfileID>& supportedProfiles,
    std::string_view supportSource = {}) {
  if (std::find(
          supportedProfiles.begin(),
          supportedProfiles.end(),
          request.profileID) != supportedProfiles.end()) {
    return;
  }

  std::string supportedNames;
  for (const auto profileID : supportedProfiles) {
    if (!supportedNames.empty()) {
      supportedNames += ", ";
    }
    supportedNames += apache::thrift::util::enumNameSafe(profileID);
  }
  throw FbossError(
      "Invalid profile '",
      apache::thrift::util::enumNameSafe(request.profileID),
      "' for port ",
      request.portName,
      supportSource,
      ". Supported profiles: [",
      supportedNames,
      "]");
}

PortProfileChangeResult planPortProfileChanges(
    const cfg::SwitchConfig& config,
    const PlatformMapping& platformMapping,
    const std::vector<PortProfileRequest>& requests,
    const PortProfileSupportMap& qsfpSupportedProfiles) {
  // Build the complete batch plan without mutating the config. Per-port checks
  // run first so the later cross-port checks can reason about every requested
  // profile together.
  std::map<PortID, const cfg::Port*> configuredPorts;
  for (const auto& port : *config.ports()) {
    configuredPorts.emplace(PortID(*port.logicalID()), &port);
  }

  // Resolve each request and validate its independent platform, transceiver,
  // and port-creation constraints.
  PortProfileChangeResult plan;
  std::map<PortID, cfg::PortProfileID> requestedProfiles;
  for (const auto& request : requests) {
    PortID portID;
    try {
      portID = platformMapping.getPortID(request.portName);
    } catch (const FbossError& error) {
      throw FbossError(
          request.portName, " is not a valid platform port: ", error.what());
    }
    if (!requestedProfiles.emplace(portID, request.profileID).second) {
      throw FbossError(
          "Port '", request.portName, "' appears more than once in the batch");
    }

    const auto& platformPort = platformMapping.getPlatformPort(portID);
    const auto configuredPort = configuredPorts.find(portID);
    const auto wasPresent = configuredPort != configuredPorts.end();
    const auto portType = *platformPort.mapping()->portType();
    if (!wasPresent && portType != cfg::PortType::INTERFACE_PORT &&
        portType != cfg::PortType::MANAGEMENT_PORT) {
      throw FbossError("Only routed ports can be created: ", request.portName);
    }

    std::vector<cfg::PortProfileID> supportedProfiles;
    for (const auto& [profileID, _] : *platformPort.supportedProfiles()) {
      supportedProfiles.push_back(profileID);
    }
    validateProfileSupport(request, supportedProfiles);

    const auto qsfpProfiles = qsfpSupportedProfiles.find(request.portName);
    if (qsfpProfiles != qsfpSupportedProfiles.end() &&
        !qsfpProfiles->second.empty()) {
      validateProfileSupport(
          request, qsfpProfiles->second, " based on QSFP capabilities");
    }

    const auto speed =
        getProfileSpeed(platformMapping, request.profileID, {portID})
            .value_or(cfg::PortSpeed::DEFAULT);
    plan.requestedPorts.push_back(
        {.portName = request.portName,
         .portID = portID,
         .profileID = request.profileID,
         .speed = speed,
         .wasPresent = wasPresent});
  }

  const auto effectiveProfile =
      [&](PortID portID) -> std::optional<cfg::PortProfileID> {
    if (const auto requested = requestedProfiles.find(portID);
        requested != requestedProfiles.end()) {
      return requested->second;
    }
    if (const auto configured = configuredPorts.find(portID);
        configured != configuredPorts.end()) {
      return *configured->second->profileID();
    }
    return std::nullopt;
  };

  // Validate the batch-wide controlling-port relationships using requested
  // profiles in preference to the currently configured profiles.
  for (const auto& request : plan.requestedPorts) {
    const auto& platformPort = platformMapping.getPlatformPort(request.portID);
    const auto controllingPort =
        PortID(*platformPort.mapping()->controllingPort());
    if (controllingPort == request.portID) {
      continue;
    }
    const auto controllingProfile = effectiveProfile(controllingPort);
    if (!controllingProfile) {
      continue;
    }
    const auto subsumedPorts =
        platformMapping.getSubsumedPorts(controllingPort, *controllingProfile);
    if (std::find(subsumedPorts.begin(), subsumedPorts.end(), request.portID) !=
        subsumedPorts.end()) {
      const auto controllingName =
          platformMapping.getPortNameByPortId(controllingPort)
              .value_or(std::to_string(static_cast<uint32_t>(controllingPort)));
      throw FbossError(
          "Cannot apply profile ",
          apache::thrift::util::enumNameSafe(request.profileID),
          " to port ",
          request.portName,
          ": its controlling port ",
          controllingName,
          " is at profile ",
          apache::thrift::util::enumNameSafe(*controllingProfile),
          " which subsumes ",
          request.portName,
          ". Change ",
          controllingName,
          " first or drop ",
          request.portName,
          " from the request.");
    }
  }

  // Collect configured ports that the requested profiles will subsume. The
  // caller decides whether its workflow should erase or disable them.
  for (const auto& request : plan.requestedPorts) {
    const auto& platformPort = platformMapping.getPlatformPort(request.portID);
    if (*platformPort.mapping()->portType() == cfg::PortType::HYPER_PORT) {
      continue;
    }
    for (const auto& subsumedPort :
         platformMapping.getSubsumedPorts(request.portID, request.profileID)) {
      const auto configured = configuredPorts.find(subsumedPort);
      if (configured == configuredPorts.end()) {
        continue;
      }
      const auto& port = *configured->second;
      plan.subsumedPorts.emplace(
          subsumedPort,
          port.name().value_or("port" + std::to_string(*port.logicalID())));
    }
  }
  return plan;
}

} // namespace

PortProfileChangeResult applyPortProfileChanges(
    cfg::SwitchConfig& config,
    const PlatformMapping& platformMapping,
    const std::vector<PortProfileRequest>& requests,
    const PortProfileSupportMap& qsfpSupportedProfiles) {
  auto plan = planPortProfileChanges(
      config, platformMapping, requests, qsfpSupportedProfiles);

  // Apply the prevalidated plan to a copy so config changes atomically.
  auto updatedConfig = config;
  for (const auto& request : plan.requestedPorts) {
    if (request.wasPresent) {
      const auto port = std::find_if(
          updatedConfig.ports()->begin(),
          updatedConfig.ports()->end(),
          [&](const auto& candidate) {
            return PortID(*candidate.logicalID()) == request.portID;
          });
      port->profileID() = request.profileID;
      port->speed() = request.speed;
      continue;
    }

    const auto vlanID = VlanID(allocateFreeVlanId(updatedConfig));
    addRoutedPortToConfig(
        updatedConfig,
        &platformMapping,
        request.portID,
        request.profileID,
        vlanID);
  }
  config = std::move(updatedConfig);
  return plan;
}

cfg::Port createDefaultPortConfig(
    const PlatformMapping* platformMapping,
    PortID id,
    cfg::PortProfileID profileID,
    int32_t ingressVlan) {
  if (!platformMapping) {
    throw FbossError("Platform mapping must not be null");
  }
  cfg::Port port;
  const auto& mapping = *platformMapping->getPlatformPort(id).mapping();
  port.name() = *mapping.name();
  port.portType() = *mapping.portType();
  port.scope() = *mapping.scope();
  port.logicalID() = id;
  port.profileID() = profileID;
  port.state() = cfg::PortState::DISABLED;
  port.ingressVlan() = ingressVlan;

  const auto profileConfig = platformMapping->getPortProfileConfig(
      PlatformPortProfileConfigMatcher(profileID, id));
  port.speed() = profileConfig.has_value() ? *profileConfig->speed()
                                           : cfg::PortSpeed::DEFAULT;
  return port;
}

cfg::Port createRoutedPortConfig(
    const PlatformMapping& platformMapping,
    PortID id,
    cfg::PortProfileID profileID,
    VlanID ingressVlan) {
  auto port = createDefaultPortConfig(
      &platformMapping, id, profileID, static_cast<int32_t>(ingressVlan));
  const auto portType = *port.portType();
  if (portType != cfg::PortType::INTERFACE_PORT &&
      portType != cfg::PortType::MANAGEMENT_PORT) {
    throw FbossError("Port ", id, " is not an interface or management port");
  }
  port.routable() = true;
  port.parserType() = cfg::ParserType::L3;
  return port;
}

cfg::Port createInterfacePortConfig(
    const PlatformMapping& platformMapping,
    PortID id,
    cfg::PortProfileID profileID,
    VlanID ingressVlan) {
  auto port =
      createRoutedPortConfig(platformMapping, id, profileID, ingressVlan);
  if (*port.portType() != cfg::PortType::INTERFACE_PORT) {
    throw FbossError("Port ", id, " is not an interface port");
  }
  return port;
}

cfg::VlanPort createVlanPortConfig(PortID portID, VlanID vlanID) {
  cfg::VlanPort vlanPort;
  vlanPort.vlanID() = static_cast<int32_t>(vlanID);
  vlanPort.logicalPort() = static_cast<int32_t>(portID);
  vlanPort.spanningTreeState() = cfg::SpanningTreeState::FORWARDING;
  vlanPort.emitTags() = false;
  return vlanPort;
}

int32_t allocateFreeVlanId(
    const cfg::SwitchConfig& config,
    int32_t minId,
    int32_t maxId) {
  std::set<int32_t> usedIds;
  for (const auto& vlan : *config.vlans()) {
    usedIds.insert(*vlan.id());
  }
  for (const auto& intf : *config.interfaces()) {
    usedIds.insert(*intf.intfID());
  }
  for (int32_t candidate = minId; candidate <= maxId; ++candidate) {
    if (usedIds.find(candidate) == usedIds.end()) {
      return candidate;
    }
  }
  throw FbossError(
      "No free vlan id available in range [", minId, ", ", maxId, "]");
}

void addRoutedPortToConfig(
    cfg::SwitchConfig& config,
    const PlatformMapping* platformMapping,
    PortID id,
    cfg::PortProfileID profileID,
    VlanID vlanID) {
  if (!platformMapping) {
    throw FbossError("Platform mapping must not be null");
  }
  const auto numericPortID = static_cast<int32_t>(id);
  const auto numericVlanID = static_cast<int32_t>(vlanID);
  if (numericVlanID == 0) {
    throw FbossError("VLAN ID 0 is reserved");
  }
  if (std::any_of(
          config.ports()->begin(),
          config.ports()->end(),
          [numericPortID](const auto& port) {
            return *port.logicalID() == numericPortID;
          })) {
    throw FbossError("Port ", id, " already exists in config");
  }
  if (std::any_of(
          config.vlans()->begin(),
          config.vlans()->end(),
          [numericVlanID](const auto& vlan) {
            return *vlan.id() == numericVlanID;
          }) ||
      std::any_of(
          config.interfaces()->begin(),
          config.interfaces()->end(),
          [numericVlanID](const auto& intf) {
            return *intf.intfID() == numericVlanID;
          })) {
    throw FbossError("VLAN/interface ID ", vlanID, " already exists in config");
  }

  auto port = createRoutedPortConfig(*platformMapping, id, profileID, vlanID);
  auto vlan = createVlanConfig(vlanID);
  auto vlanPort = createVlanPortConfig(id, vlanID);
  auto intf = createVlanInterfaceConfig(InterfaceID(numericVlanID), vlanID);

  config.ports()->push_back(std::move(port));
  config.vlans()->push_back(std::move(vlan));
  config.vlanPorts()->push_back(std::move(vlanPort));
  config.interfaces()->push_back(std::move(intf));
}

void addInterfacePortToConfig(
    cfg::SwitchConfig& config,
    const PlatformMapping* platformMapping,
    PortID id,
    cfg::PortProfileID profileID,
    VlanID vlanID) {
  if (!platformMapping) {
    throw FbossError("Platform mapping must not be null");
  }
  if (*platformMapping->getPlatformPort(id).mapping()->portType() !=
      cfg::PortType::INTERFACE_PORT) {
    throw FbossError("Port ", id, " is not an interface port");
  }
  addRoutedPortToConfig(config, platformMapping, id, profileID, vlanID);
}

int32_t addInterfacePortToConfig(
    cfg::SwitchConfig& config,
    const PlatformMapping* platformMapping,
    PortID id,
    cfg::PortProfileID profileID) {
  const int32_t n = allocateFreeVlanId(config);
  addInterfacePortToConfig(config, platformMapping, id, profileID, VlanID(n));

  return n;
}

void removePortsFromConfig(
    cfg::SwitchConfig& config,
    const std::set<PortID>& portsToRemove,
    PortRemovalMode mode,
    bool pruneEmptyVlansAndInterfaces) {
  // Phases:
  //  1. Disable mode: just mark the listed ports DISABLED and return.
  //  2. Erase mode: drop the listed ports and their vlanPorts, remembering
  //     which vlans lost a member.
  //  3. If pruneEmptyVlansAndInterfaces: drop each of those vlans that no
  //     surviving vlanPort still uses (except the default vlan), plus the VLAN
  //     interface on each.
  if (portsToRemove.empty()) {
    return;
  }

  // Disable mode: leave the port entries (and their vlanPorts/vlans/interfaces)
  // in place, just mark those ports DISABLED.
  if (mode == PortRemovalMode::Disable) {
    for (auto& p : *config.ports()) {
      if (portsToRemove.count(PortID(*p.logicalID())) > 0) {
        p.state() = cfg::PortState::DISABLED;
      }
    }
    return;
  }

  // Erase mode. Drop the ports.
  auto& ports = *config.ports();
  ports.erase(
      std::remove_if(
          ports.begin(),
          ports.end(),
          [&](const cfg::Port& p) {
            return portsToRemove.count(PortID(*p.logicalID())) > 0;
          }),
      ports.end());

  // Drop the removed ports' vlanPorts, remembering the vlans they were members
  // of -- those are the only vlans that could have become empty.
  std::set<int32_t> maybeEmptyVlans;
  auto& vlanPorts = *config.vlanPorts();
  vlanPorts.erase(
      std::remove_if(
          vlanPorts.begin(),
          vlanPorts.end(),
          [&](const cfg::VlanPort& vp) {
            if (portsToRemove.count(PortID(*vp.logicalPort())) > 0) {
              maybeEmptyVlans.insert(*vp.vlanID());
              return true;
            }
            return false;
          }),
      vlanPorts.end());

  if (!pruneEmptyVlansAndInterfaces) {
    return;
  }

  // A candidate vlan is dropped only if no surviving vlanPort still references
  // it and it is not the default vlan. This leaves originally-empty vlans
  // (never candidates) and vlans a surviving tagged/multi-vlan member still
  // uses, so no vlanPort is ever left pointing at a dropped vlan.
  std::set<int32_t> stillUsedVlans;
  for (const auto& vp : vlanPorts) {
    stillUsedVlans.insert(*vp.vlanID());
  }
  std::set<int32_t> vlansToRemove;
  for (const auto vlanId : maybeEmptyVlans) {
    if (vlanId != *config.defaultVlan() && stillUsedVlans.count(vlanId) == 0) {
      vlansToRemove.insert(vlanId);
    }
  }

  auto& vlans = *config.vlans();
  vlans.erase(
      std::remove_if(
          vlans.begin(),
          vlans.end(),
          [&](const cfg::Vlan& v) { return vlansToRemove.count(*v.id()) > 0; }),
      vlans.end());

  // Drop the VLAN interface sitting on each removed vlan. A port-router
  // (PORT-type) interface is bound to a port via portID (its vlanID field is
  // unset, defaulting to 0), so leave those untouched for now -- this helper
  // only manages VLAN-style configs. TODO: prune port-router interfaces once
  // platform support for them is verified.
  auto& interfaces = *config.interfaces();
  interfaces.erase(
      std::remove_if(
          interfaces.begin(),
          interfaces.end(),
          [&](const cfg::Interface& i) {
            if (i.portID().has_value()) {
              return false; // port-router interface: leave in place for now
            }
            return vlansToRemove.count(*i.vlanID()) > 0;
          }),
      interfaces.end());
}

} // namespace facebook::fboss::utility
