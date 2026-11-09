/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/utils/PortMap.h"

#include <folly/Conv.h>
#include <folly/CppAttributes.h>
#include <glog/logging.h>

namespace facebook::fboss::utils {

PortMap::PortMap(const cfg::AgentConfig& config) {
  const auto& switchConfig = *config.sw();

  // Build the mappings in order:
  // 1. Port name <-> logical ID
  buildPortMaps(switchConfig);

  // 2. VLAN-based mappings (logicalPort <-> vlanID)
  buildVlanMaps(switchConfig);

  // 3. Interface mappings (using portID, name, or VLAN)
  buildInterfaceMaps(switchConfig);
}

void PortMap::buildPortMaps(const cfg::SwitchConfig& switchConfig) {
  for (auto& port : *switchConfig.ports()) {
    PortID logicalId(*port.logicalID());

    // Port name is technically optional in the thrift schema for historical
    // reasons, but we require it to be set for the PortMap to work correctly
    if (!port.name().has_value()) {
      throw std::runtime_error(
          "Port with logicalID " + folly::to<std::string>(logicalId) +
          " is missing required 'name' field");
    }
    const std::string& portName = *port.name();

    portNameToLogicalId_[portName] = logicalId;
    portLogicalIdToName_[logicalId] = portName;
    portNameToPort_[portName] = const_cast<cfg::Port*>(&port);
  }

  VLOG(2) << "Built port maps with " << portNameToLogicalId_.size() << " ports";
}

void PortMap::buildVlanMaps(const cfg::SwitchConfig& switchConfig) {
  if (switchConfig.vlanPorts()->empty()) {
    return;
  }

  for (const auto& vlanPort : *switchConfig.vlanPorts()) {
    // vlanID and logicalPort are required fields
    VlanID vlanId(*vlanPort.vlanID());
    PortID logicalPort(*vlanPort.logicalPort());

    portLogicalIdToVlanId_[logicalPort] = vlanId;
    vlanIdToPortLogicalId_[vlanId] = logicalPort;
  }

  VLOG(2) << "Built VLAN maps with " << vlanIdToPortLogicalId_.size()
          << " VLAN-port mappings";
}

void PortMap::buildInterfaceMaps(const cfg::SwitchConfig& switchConfig) {
  if (switchConfig.interfaces()->empty()) {
    return;
  }

  // First pass: build interface ID to VLAN ID mapping and store interface
  // pointers
  for (auto& interface : *switchConfig.interfaces()) {
    // intfID is required
    InterfaceID intfId(*interface.intfID());

    // Store pointer to interface object
    interfaceIdToInterface_[intfId] = const_cast<cfg::Interface*>(&interface);

    // vlanID is a non-optional field but 0 indicates "not set"
    if (*interface.vlanID() != 0) {
      VlanID vlanId(*interface.vlanID());
      interfaceIdToVlanId_[intfId] = vlanId;
      vlanIdToInterfaceId_[vlanId] = intfId;
    }
  }

  // Second pass: map each interface to a port and build name mapping
  for (const auto& interface : *switchConfig.interfaces()) {
    mapInterfaceToPort(interface);

    // Build interface name mapping
    InterfaceID intfId(*interface.intfID());
    if (interface.name().has_value()) {
      const std::string& intfName = *interface.name();
      interfaceNameToInterface_[intfName] =
          const_cast<cfg::Interface*>(&interface);
      VLOG(3) << "Mapped interface name " << intfName << " to interface "
              << intfId;
    }
  }

  VLOG(2) << "Built interface maps with " << portNameToInterfaceId_.size()
          << " port-to-interface mappings and "
          << interfaceNameToInterface_.size() << " interface name mappings";
}

void PortMap::mapInterfaceToPort(const cfg::Interface& interface) {
  // intfID is required
  InterfaceID intfId(*interface.intfID());
  std::optional<PortID> portLogicalId;

  // Use portID attribute if set (optional field)
  if (interface.portID().has_value()) {
    portLogicalId = PortID(*interface.portID());
    VLOG(3) << "Interface " << intfId
            << " mapped to port via portID: " << *portLogicalId;
  } else if (*interface.vlanID() != 0) {
    // Fall back to using VLAN-based mapping (non-optional field, 0 = not set)
    // The assumption here is that there is a one-to-one mapping between VLAN
    // and port, for physical interfaces. This is true for most platforms.
    VlanID vlanId(*interface.vlanID());
    auto it = vlanIdToPortLogicalId_.find(vlanId);
    if (it != vlanIdToPortLogicalId_.end()) {
      portLogicalId = it->second;
      VLOG(3) << "Interface " << intfId
              << " mapped to port via VLAN: " << vlanId << " -> "
              << *portLogicalId;
    }
  }

  // If we found a port logical ID, create the final mapping
  if (portLogicalId) {
    auto portNameIt = portLogicalIdToName_.find(*portLogicalId);
    if (portNameIt != portLogicalIdToName_.end()) {
      const std::string& portName = portNameIt->second;

      // Check if this port is already mapped to a different interface.
      // This can happen when a single port is associated with multiple
      // VLANs (and thus multiple interfaces), which the config schema
      // allows (Port::VlanMembership is a map of VLANs). In this case, we
      // keep the first mapping and skip subsequent ones.
      auto existingMapping = portNameToInterfaceId_.find(portName);
      if (existingMapping != portNameToInterfaceId_.end()) {
        if (interface.portID().has_value()) {
          // The multi-VLAN case above only arises from the VLAN-based
          // fallback; two interfaces explicitly claiming the same port is a
          // misconfiguration, so keep it visible at default verbosity.
          LOG(WARNING) << "Port " << portName << " (logical ID "
                       << *portLogicalId
                       << ") is explicitly mapped to multiple interfaces ("
                       << existingMapping->second << " and " << intfId
                       << "); keeping the first mapping.";
        } else {
          VLOG(3) << "Port " << portName << " (logical ID " << *portLogicalId
                  << ") is already mapped to interface "
                  << existingMapping->second
                  << ". Skipping mapping to interface " << intfId
                  << " (port with multiple VLANs).";
        }
        return;
      }

      portNameToInterfaceId_[portName] = intfId;
      interfaceIdToPortName_[intfId] = portName;
      VLOG(3) << "Final mapping: port " << portName << " <-> interface "
              << intfId;
    } else {
      VLOG(2) << "Could not find port name for interface " << intfId
              << " with logical ID " << *portLogicalId;
    }
  } else {
    VLOG(3) << "Could not map interface " << intfId << " to any port";
  }
}

std::optional<InterfaceID> PortMap::getInterfaceIdForPort(
    const std::string& portName) const {
  auto it = portNameToInterfaceId_.find(portName);
  if (it != portNameToInterfaceId_.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::optional<std::string> PortMap::getPortNameForInterface(
    InterfaceID interfaceId) const {
  auto it = interfaceIdToPortName_.find(interfaceId);
  if (it != interfaceIdToPortName_.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::optional<PortID> PortMap::getPortLogicalId(
    const std::string& portName) const {
  auto it = portNameToLogicalId_.find(portName);
  if (it != portNameToLogicalId_.end()) {
    return it->second;
  }
  return std::nullopt;
}

bool PortMap::hasPort(const std::string& portName) const {
  return portNameToLogicalId_.find(portName) != portNameToLogicalId_.end();
}

bool PortMap::hasInterface(InterfaceID interfaceId) const {
  return interfaceIdToPortName_.find(interfaceId) !=
      interfaceIdToPortName_.end();
}

std::vector<std::string> PortMap::getAllPortNames() const {
  std::vector<std::string> portNames;
  portNames.reserve(portNameToLogicalId_.size());
  for (const auto& [portName, _] : portNameToLogicalId_) {
    portNames.push_back(portName);
  }
  return portNames;
}

std::vector<InterfaceID> PortMap::getAllInterfaceIds() const {
  std::vector<InterfaceID> interfaceIds;
  interfaceIds.reserve(interfaceIdToPortName_.size());
  for (const auto& [interfaceId, _] : interfaceIdToPortName_) {
    interfaceIds.push_back(interfaceId);
  }
  return interfaceIds;
}

cfg::Port* FOLLY_NULLABLE PortMap::getPort(const std::string& portName) const {
  auto it = portNameToPort_.find(portName);
  if (it != portNameToPort_.end()) {
    return it->second;
  }
  return nullptr;
}

cfg::Interface* FOLLY_NULLABLE
PortMap::getInterfaceByName(const std::string& interfaceName) const {
  auto it = interfaceNameToInterface_.find(interfaceName);
  if (it != interfaceNameToInterface_.end()) {
    return it->second;
  }
  return nullptr;
}

cfg::Interface* FOLLY_NULLABLE
PortMap::getInterface(InterfaceID interfaceId) const {
  auto it = interfaceIdToInterface_.find(interfaceId);
  if (it != interfaceIdToInterface_.end()) {
    return it->second;
  }
  return nullptr;
}

} // namespace facebook::fboss::utils
