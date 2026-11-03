/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/lib/config/PlatformConfigUtils.h"
#include "fboss/agent/FbossError.h"

#include <thrift/lib/cpp/util/EnumUtils.h>

namespace {
using namespace facebook::fboss::phy;

bool recurseCheckPortOwnsChip(
    const PinConnection& pinConn,
    const DataPlanePhyChip& chip) {
  if (pinConn.a().value().chip().value() == chip.name().value()) {
    return true;
  }
  if (auto zPinRef = pinConn.z()) {
    if (zPinRef->getType() == Pin::Type::end) {
      if (zPinRef->end()->chip().value() == chip.name().value()) {
        return true;
      }
    } else if (zPinRef->getType() == Pin::Type::junction) {
      const auto& zJunction = zPinRef->junction().value();
      if (zJunction.system().value().chip().value() == chip.name().value()) {
        return true;
      }
      const auto& connections = zJunction.line().value();
      if (!connections.empty()) {
        // Because one port can only use one iphy, one xphy, one transceiver,
        // and if one PinConnection can't find this chip, you won't find
        // this chip on another PinConnection of the same port
        return recurseCheckPortOwnsChip(connections.front(), chip);
      }
    }
  }
  return false;
}

void recurseGetPinsByChipType(
    const PinConnection& pinConn,
    const std::map<std::string, DataPlanePhyChip>& chipsMap,
    std::vector<Pin>& pins) {
  if (chipsMap.find(*pinConn.a()->chip()) != chipsMap.end()) {
    Pin temp;
    temp.end() = *pinConn.a();
    pins.push_back(temp);
    return;
  }

  auto zPinRef = pinConn.z();
  if (!zPinRef) {
    return;
  }
  auto zPin = *zPinRef;
  if (zPin.getType() == Pin::Type::end) {
    if (chipsMap.find(*zPin.end().as_const()->chip()) != chipsMap.end()) {
      Pin temp;
      temp.end() = zPin.end().as_const().value();
      pins.push_back(temp);
      return;
    }
  } else if (zPin.getType() == Pin::Type::junction) {
    auto zJunction = zPin.junction().as_const().value();
    if (chipsMap.find(*zJunction.system()->chip()) != chipsMap.end()) {
      Pin temp;
      temp.junction() = zJunction;
      pins.push_back(temp);
      return;
    }
    for (const auto& connection : *zPin.junction().as_const()->line()) {
      recurseGetPinsByChipType(connection, chipsMap, pins);
    }
  }
}

void checkPinType(const Pin& pin, const DataPlanePhyChipType& expectedType) {
  switch (expectedType) {
    case DataPlanePhyChipType::IPHY:
      if (pin.getType() != Pin::Type::end) {
        throw facebook::fboss::FbossError("Unsupported pin type for iphy");
      }
      break;
    case DataPlanePhyChipType::XPHY:
      if (pin.getType() != Pin::Type::junction) {
        throw facebook::fboss::FbossError("Unsupported pin type for xphy");
      }
      break;
    case DataPlanePhyChipType::TRANSCEIVER:
      if (pin.getType() != Pin::Type::end) {
        throw facebook::fboss::FbossError(
            "Unsupported pin type for transceiver");
      }
      break;
    default:
      throw facebook::fboss::FbossError(
          "Unsupported pin type:",
          apache::thrift::util::enumNameSafe(expectedType));
  }
}

std::vector<Pin> getPinsByChipType(
    const std::map<std::string, DataPlanePhyChip>& chipsMap,
    const std::vector<PinConnection>& pinConns,
    DataPlanePhyChipType type) {
  // First get all the expected chip type into a map to make search easier
  std::map<std::string, DataPlanePhyChip> filteredChips;
  for (const auto& itChip : chipsMap) {
    if (*itChip.second.type() == type) {
      filteredChips[itChip.first] = itChip.second;
    }
  }

  // use the recurse function to get all the expected chip pin
  std::vector<Pin> results;
  for (const auto& pinConn : pinConns) {
    recurseGetPinsByChipType(pinConn, filteredChips, results);
  }

  for (auto& pin : results) {
    checkPinType(pin, type);
  }

  return results;
}

std::string getChipName(const Pin& pin) {
  if (pin.getType() == Pin::Type::junction) {
    return *pin.junction()->system()->chip();
  }
  return *pin.end()->chip();
}

std::string getChipName(const PinConfig& pinConfig) {
  return *pinConfig.id()->chip();
}

std::vector<int32_t> getPhysicalIdsByChipType(
    const std::map<std::string, DataPlanePhyChip>& chipsMap,
    DataPlanePhyChipType type) {
  std::vector<int32_t> physicalIds;
  for (auto chip : chipsMap) {
    if (*chip.second.type() != type) {
      continue;
    }
    physicalIds.push_back(*chip.second.physicalID());
  }

  return physicalIds;
}
} // unnamed namespace

namespace facebook::fboss::utility {

std::vector<phy::PinID> getTransceiverLanes(
    const cfg::PlatformPortEntry& port,
    const std::map<std::string, phy::DataPlanePhyChip>& chipsMap,
    std::optional<cfg::PortProfileID> profileID) {
  std::vector<phy::PinID> lanes;
  if (profileID && profileID != cfg::PortProfileID::PROFILE_DEFAULT) {
    auto itPortCfg = port.supportedProfiles()->find(*profileID);
    if (itPortCfg == port.supportedProfiles()->end()) {
      throw FbossError(
          "Port: ",
          *port.mapping()->name(),
          " doesn't support profile:",
          apache::thrift::util::enumNameSafe(*profileID));
    }
    if (auto tcvr = itPortCfg->second.pins()->transceiver()) {
      for (const auto& pinCfg : *tcvr) {
        lanes.push_back(*pinCfg.id());
      }
    }
  } else {
    // If it's not looking for the lanes list based on profile, return the
    // static lane mapping
    auto pins = getPinsByChipType(
        chipsMap,
        *port.mapping()->pins(),
        phy::DataPlanePhyChipType::TRANSCEIVER);
    // the return pins should always be PinID for transceiver
    for (auto pin : pins) {
      lanes.push_back(pin.end().as_const().value());
    }
  }
  return lanes;
}

std::map<int32_t, phy::PolaritySwap> getIphyPolaritySwapMap(
    const std::vector<phy::PinConnection>& pinConnections) {
  std::map<int32_t, phy::PolaritySwap> polaritySwapMap;

  for (const auto& pinConnection : pinConnections) {
    if (pinConnection.polaritySwap()) {
      polaritySwapMap.emplace(
          *pinConnection.a()->lane(), *pinConnection.polaritySwap());
    }
  }

  return polaritySwapMap;
}

std::map<int32_t, phy::LaneConfig> getIphyLaneConfigs(
    const std::vector<phy::PinConfig>& iphyPinConfigs) {
  std::map<int32_t, phy::LaneConfig> laneConfigs;
  for (auto pinConfig : iphyPinConfigs) {
    if (!pinConfig.tx()) {
      continue;
    }
    phy::LaneConfig laneConfig;
    laneConfig.tx = *pinConfig.tx();
    laneConfigs.emplace(*pinConfig.id()->lane(), laneConfig);
  }

  return laneConfigs;
}

std::map<int32_t, phy::PolaritySwap> getXphyLinePolaritySwapMap(
    const cfg::PlatformPortEntry& platformPortEntry,
    cfg::PortProfileID profileID,
    const std::map<std::string, phy::DataPlanePhyChip>& chipsMap,
    const phy::PortProfileConfig& portProfileConfig) {
  std::map<int32_t, phy::PolaritySwap> xphyPolaritySwapMap;

  auto profile = platformPortEntry.supportedProfiles()->find(profileID);
  if (profile == platformPortEntry.supportedProfiles()->end()) {
    throw FbossError(
        "Unsupported profile in getXphyLinePolaritySwapMap: ",
        apache::thrift::util::enumNameSafe(profileID));
  }

  if (!portProfileConfig.xphyLine()) {
    throw FbossError("No xphy line info in portProfileConfig");
  }

  auto numLanes = portProfileConfig.xphyLine()->numLanes();

  // If the profile has an Xphy pin config, use that to determine the number of
  //   lanes, otherwise use the default pin count in the platformPortEntry

  // If the profile has an Xphy pin config, use that to determine the mapping.
  // If there's no profile pin config, or the profile doesn't contain any
  //   polarity swap info, get the polarity swap from the default pin mapping
  //   from the platformPortEntry
  auto profilePins = profile->second.pins()->xphyLine();
  if (profilePins) {
    for (const auto& pin : *profilePins) {
      if (pin.polaritySwap()) {
        xphyPolaritySwapMap[*pin.id()->lane()] = *pin.polaritySwap();
      }
    }
  }
  if (xphyPolaritySwapMap.empty()) {
    const auto& xphyPinList = getPinsByChipType(
        chipsMap,
        *platformPortEntry.mapping()->pins(),
        phy::DataPlanePhyChipType::XPHY);
    for (const auto& pin : xphyPinList) {
      for (const auto& connection : *pin.junction()->line()) {
        if (auto pn = connection.polaritySwap()) {
          xphyPolaritySwapMap.emplace(*connection.a()->lane(), *pn);
        }
      }
    }
  }
  if (!(xphyPolaritySwapMap.size() == 0 ||
        xphyPolaritySwapMap.size() == *numLanes)) {
    throw FbossError(
        "Computed polarity swap map contained ",
        xphyPolaritySwapMap.size(),
        " lanes, but either 0 or ",
        *numLanes,
        " lanes are required.");
  }
  return xphyPolaritySwapMap;
}

std::vector<phy::PinID> getOrderedIphyLanes(
    const cfg::PlatformPortEntry& port,
    const std::map<std::string, phy::DataPlanePhyChip>& chipsMap,
    std::optional<cfg::PortProfileID> profileID) {
  std::vector<phy::PinID> lanes;
  if (profileID && profileID != cfg::PortProfileID::PROFILE_DEFAULT) {
    auto itrPortCfg = port.supportedProfiles()->find(*profileID);
    if (itrPortCfg == port.supportedProfiles()->end()) {
      throw FbossError(
          "Port: ",
          *port.mapping()->name(),
          " doesn't support profile:",
          apache::thrift::util::enumNameSafe(*profileID));
    }
    for (const auto& pinCfg : *itrPortCfg->second.pins()->iphy()) {
      lanes.push_back(*pinCfg.id());
    }
  } else {
    // If it's not looking for the lanes list based on profile, return the
    // static lane mapping
    auto pins = getPinsByChipType(
        chipsMap, *port.mapping()->pins(), phy::DataPlanePhyChipType::IPHY);
    // the return pins should always be PinID for transceiver
    for (auto pin : pins) {
      lanes.push_back(pin.end().as_const().value());
    }
  }
  if (lanes.empty()) {
    throw FbossError(
        "Port: ", *port.mapping()->name(), " doesn't have iphy lanes");
  }
  // Sort by lane
  std::sort(lanes.begin(), lanes.end(), [](const auto& lPin, const auto& rPin) {
    return *lPin.lane() < *rPin.lane();
  });
  return lanes;
}

// Get subsidiary PortID list based on controlling port
std::map<PortID, std::vector<PortID>> getSubsidiaryPortIDs(
    const std::map<int32_t, cfg::PlatformPortEntry>& platformPorts) {
  std::map<PortID, std::vector<PortID>> results;
  for (auto itPort : platformPorts) {
    auto controlPortID =
        facebook::fboss::PortID(*itPort.second.mapping()->controllingPort());
    if (results.find(controlPortID) == results.end()) {
      results[controlPortID] = std::vector<PortID>();
    }
    // Note that the subsidiary_ports map includes the controlling port
    results[controlPortID].emplace_back(itPort.first);
  }

  // Sort subsidiary port ids
  for (auto itControlPort : results) {
    std::sort(
        itControlPort.second.begin(),
        itControlPort.second.end(),
        [](const auto& lPortID, const auto& rPortID) {
          return lPortID < rPortID;
        });
  }
  return results;
}

std::vector<cfg::PlatformPortEntry> getPlatformPortsByControllingPort(
    const std::map<int32_t, cfg::PlatformPortEntry>& platformPorts,
    PortID controllingPort) {
  std::vector<cfg::PlatformPortEntry> ports;
  int32_t controllingPortID = static_cast<int32_t>(controllingPort);
  for (const auto& port : platformPorts) {
    if (*port.second.mapping()->controllingPort() == controllingPortID) {
      ports.push_back(port.second);
    }
  }
  return ports;
}

std::vector<phy::PinConfig> getPinsFromPortPinConfig(
    const phy::PortPinConfig& pinConfig,
    phy::DataPlanePhyChipType chipType) {
  std::vector<phy::PinConfig> pins;
  switch (chipType) {
    case phy::DataPlanePhyChipType::IPHY:
      pins = *pinConfig.iphy();
      break;
    case phy::DataPlanePhyChipType::XPHY:
      if (pinConfig.xphySys().has_value()) {
        pins = *pinConfig.xphySys();
      }
      if (pinConfig.xphyLine().has_value()) {
        pins.insert(
            pins.end(),
            pinConfig.xphyLine()->begin(),
            pinConfig.xphyLine()->end());
      }
      break;
    case phy::DataPlanePhyChipType::TRANSCEIVER:
      if (pinConfig.transceiver().has_value()) {
        pins = *pinConfig.transceiver();
      }
      break;
    default:
      throw FbossError(
          "Only IPHY, XPHY, and TRANSCEIVER chip types are supported.");
  }

  return pins;
}

std::map<std::string, phy::DataPlanePhyChip> getDataPlanePhyChips(
    const cfg::PlatformPortEntry& port,
    const std::map<std::string, phy::DataPlanePhyChip>& chipsMap,
    std::optional<phy::DataPlanePhyChipType> chipType,
    const std::optional<cfg::PortProfileID> profileID) {
  std::map<std::string, phy::DataPlanePhyChip> chips;

  // If chipType is not specified, we return all the chips for such port.
  std::vector<phy::DataPlanePhyChipType> types;
  if (chipType) {
    types.push_back(*chipType);
  } else {
    types.push_back(phy::DataPlanePhyChipType::IPHY);
    types.push_back(phy::DataPlanePhyChipType::XPHY);
    types.push_back(phy::DataPlanePhyChipType::TRANSCEIVER);
  }

  // If profileID is specified, we return the chips based on the
  // profile-specific pins. Else, we return the chips based on the static pin
  // mapping.
  if (profileID.has_value() &&
      port.supportedProfiles()->find(*profileID) !=
          port.supportedProfiles()->end()) {
    auto& pinConfig = *port.supportedProfiles()->at(*profileID).pins();
    for (auto type : types) {
      for (const auto& pin : getPinsFromPortPinConfig(pinConfig, type)) {
        // Backplane chips are stored in the transceiver field, so we need to
        // add an additional filter here.
        if (auto itChip = chipsMap.find(getChipName(pin));
            itChip != chipsMap.end() && *itChip->second.type() == type) {
          chips.emplace(itChip->first, itChip->second);
        }
      }
    }
  } else {
    for (auto type : types) {
      for (const auto& pinConn :
           getPinsByChipType(chipsMap, *port.mapping()->pins(), type)) {
        if (auto itChip = chipsMap.find(getChipName(pinConn));
            itChip != chipsMap.end()) {
          chips.emplace(itChip->first, itChip->second);
        }
      }
    }
  }

  return chips;
}

const std::vector<TransceiverID> getTransceiverIds(
    const cfg::PlatformPortEntry& port,
    const std::map<std::string, phy::DataPlanePhyChip>& chipsMap,
    const std::optional<cfg::PortProfileID> profileID) {
  auto transceiverChips = getDataPlanePhyChips(
      port, chipsMap, phy::DataPlanePhyChipType::TRANSCEIVER, profileID);
  // In most use cases, we expect only one transceiver per port. However, we now
  // want to support the use case of multi-transceiver ports.

  // Since this function can be used anywhere for platform mapping, we need to
  // remove the check that guarantees there's only one tcvr per port.
  std::vector<TransceiverID> ids;
  ids.reserve(transceiverChips.size());
  for (const auto& chip : transceiverChips) {
    ids.emplace_back(*chip.second.physicalID());
  }

  return ids;
}

std::vector<TransceiverID> getTransceiverIds(
    const std::map<std::string, phy::DataPlanePhyChip>& chipsMap) {
  std::vector<TransceiverID> transceiverIds;
  for (auto id : getPhysicalIdsByChipType(
           chipsMap, phy::DataPlanePhyChipType::TRANSCEIVER)) {
    transceiverIds.emplace_back(id);
  }
  return transceiverIds;
}

std::vector<cfg::PlatformPortEntry> getPlatformPortsByChip(
    const std::map<int32_t, cfg::PlatformPortEntry>& platformPorts,
    const phy::DataPlanePhyChip& chip) {
  std::vector<cfg::PlatformPortEntry> ports;
  for (const auto& idAndPort : platformPorts) {
    for (const auto& connection :
         idAndPort.second.mapping().value().pins().value()) {
      if (recurseCheckPortOwnsChip(connection, chip)) {
        ports.push_back(idAndPort.second);
        break;
      }
    }
  }
  return ports;
}
std::vector<uint32_t> getHwPortLanes(
    cfg::PlatformPortEntry& platformPortEntry,
    cfg::PortProfileID profileID,
    const std::map<std::string, phy::DataPlanePhyChip>& dataPlanePhyChips,
    std::function<uint32_t(uint32_t, uint32_t)>&& getPhysicalLaneId) {
  auto iphys = utility::getOrderedIphyLanes(
      platformPortEntry, dataPlanePhyChips, profileID);
  std::vector<uint32_t> hwLaneList;
  for (auto iphy : iphys) {
    auto chipIter = dataPlanePhyChips.find(*iphy.chip());
    if (chipIter == dataPlanePhyChips.end()) {
      throw FbossError(
          "dataplane chip does not exist for chip: ", *iphy.chip());
    }
    hwLaneList.push_back(
        getPhysicalLaneId(*chipIter->second.physicalID(), *iphy.lane()));
  }
  return hwLaneList;
}

TcvrToPortMap getTcvrToPortMap(
    const std::map<int32_t, cfg::PlatformPortEntry>& platformPorts,
    const std::map<std::string, phy::DataPlanePhyChip>& chipsMap) {
  TcvrToPortMap tcvrToPortMap;

  for (const auto& it : platformPorts) {
    auto port = it.second;
    // Get the transceiver id based on the port info from platform mapping.
    auto portIdInt = *port.mapping()->id();
    auto portId = PortID(portIdInt);
    auto transceiverIds = utility::getTransceiverIds(port, chipsMap);
    if (transceiverIds.empty()) {
      XLOG(INFO) << "Did not find corresponding TransceiverIDs for PortID: "
                 << portIdInt;
      continue;
    }

    for (const auto& transceiverId : transceiverIds) {
      // Add the port to the transceiver-indexed port group.
      auto portGroupIt = tcvrToPortMap.find(transceiverId);
      if (portGroupIt == tcvrToPortMap.end()) {
        tcvrToPortMap[transceiverId] = std::vector<PortID>{portId};
      } else {
        tcvrToPortMap.at(transceiverId).emplace_back(portId);
      }
    }
  }

  for (auto& [tcvrId, portIds] : tcvrToPortMap) {
    XLOG(DBG2) << "TransceiverID(" << tcvrId
               << ") has ports: " << folly::join(", ", portIds);
  }

  return tcvrToPortMap;
}

PortToTcvrMap getPortToTcvrMap(
    const std::map<int32_t, cfg::PlatformPortEntry>& platformPorts,
    const std::map<std::string, phy::DataPlanePhyChip>& chipsMap) {
  PortToTcvrMap portToTcvrMap;

  for (const auto& it : platformPorts) {
    auto port = it.second;
    // Get the transceiver id based on the port info from platform mapping.
    auto portIdInt = *port.mapping()->id();
    auto portId = PortID(portIdInt);
    auto transceiverIds = utility::getTransceiverIds(port, chipsMap);
    if (transceiverIds.empty()) {
      XLOG(INFO) << "Did not find corresponding TransceiverIDs for PortID: "
                 << portIdInt;
    }

    // Add all transceivers to the port-indexed transceiver group.
    portToTcvrMap[portId] = transceiverIds;

    XLOG(ERR) << "PortID(" << portId
              << ") has transceivers: " << folly::join(", ", transceiverIds);
  }

  return portToTcvrMap;
}

std::vector<PortID> getPortIdsWithTransceiverOrXphy(
    const std::map<int32_t, cfg::PlatformPortEntry>& platformPorts,
    const std::map<std::string, phy::DataPlanePhyChip>& chipsMap) {
  std::vector<PortID> portIds;

  for (const auto& [portId, portEntry] : platformPorts) {
    auto tcvrs = utility::getDataPlanePhyChips(
        portEntry, chipsMap, phy::DataPlanePhyChipType::TRANSCEIVER);
    if (!tcvrs.empty()) {
      portIds.emplace_back(portId);
      continue;
    }
    auto xphys = utility::getDataPlanePhyChips(
        portEntry, chipsMap, phy::DataPlanePhyChipType::XPHY);
    if (!xphys.empty()) {
      portIds.emplace_back(portId);
    }
  }

  return portIds;
}

bool hasTransceiver(
    const cfg::PlatformPortEntry& port,
    const std::map<std::string, phy::DataPlanePhyChip>& chipsMap) {
  auto transceiverChips = getDataPlanePhyChips(
      port, chipsMap, phy::DataPlanePhyChipType::TRANSCEIVER);
  return !transceiverChips.empty();
}
} // namespace facebook::fboss::utility
