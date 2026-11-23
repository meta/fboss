// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/rib/MySidConfigUtils.h"

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/state/RouteNextHop.h"

#include <folly/Conv.h>

#include <algorithm>

namespace facebook::fboss {

namespace {

std::vector<folly::CIDRNetwork> parseLocatorPrefixes(
    const cfg::MySidConfig& config) {
  auto prefixes = *config.locatorPrefixes();
  if (prefixes.empty() && !config.locatorPrefix()->empty()) {
    prefixes.push_back(*config.locatorPrefix());
  }
  if (prefixes.empty()) {
    throw FbossError(
        "MySidConfig has neither locatorPrefixes nor locatorPrefix");
  }
  std::vector<folly::CIDRNetwork> locators;
  for (const auto& prefix : prefixes) {
    auto locator = folly::IPAddress::createNetwork(prefix);
    if (!locators.empty() && locator.second != locators.front().second) {
      throw FbossError(
          "MySID locator prefix ",
          prefix,
          " length differs from ",
          folly::IPAddress::networkToString(locators.front()));
    }
    if (std::find(locators.begin(), locators.end(), locator) !=
        locators.end()) {
      throw FbossError("Duplicate MySID locator prefix ", prefix);
    }
    locators.push_back(std::move(locator));
  }
  return locators;
}

MySidWithNextHops convertMySidEntry(
    const folly::IPAddressV6& locatorAddr,
    uint8_t locatorPrefixLen,
    int16_t functionId,
    const cfg::MySidEntryConfig& entryConfig,
    const std::unordered_map<std::string, InterfaceID>& portNameToInterfaceId) {
  auto funcIdStr = fmt::format("{:x}", functionId);
  auto sidAddr = buildSidAddress(locatorAddr, locatorPrefixLen, funcIdStr);

  state::MySidFields fields;
  facebook::network::thrift::IPPrefix prefix;
  prefix.prefixAddress() =
      facebook::network::toBinaryAddress(folly::IPAddress(sidAddr));
  prefix.prefixLength() = locatorPrefixLen + 16;
  fields.mySid() = prefix;
  fields.clientId() = ClientID::STATIC_ROUTE;

  RouteNextHopSet nhops;

  auto entryType = entryConfig.getType();
  switch (entryType) {
    case cfg::MySidEntryConfig::Type::adjacency: {
      fields.type() = MySidType::ADJACENCY_MICRO_SID;
      const auto& adjConfig = entryConfig.adjacency().value();
      auto it = portNameToInterfaceId.find(*adjConfig.portName());
      if (it == portNameToInterfaceId.end()) {
        throw FbossError(
            "Port '", *adjConfig.portName(), "' not found in switch config");
      }
      fields.adjacencyInterfaceId() = static_cast<int32_t>(it->second);
      fields.isV6() = *adjConfig.isV6();
      break;
    }
    case cfg::MySidEntryConfig::Type::node: {
      fields.type() = MySidType::NODE_MICRO_SID;
      const auto& nodeConfig = entryConfig.node().value();
      nhops.insert(UnresolvedNextHop(
          folly::IPAddress(*nodeConfig.nodeAddress()), ECMP_WEIGHT));
      break;
    }
    case cfg::MySidEntryConfig::Type::decap: {
      fields.type() = MySidType::DECAPSULATE_AND_LOOKUP;
      break;
    }
    default:
      throw FbossError(
          "Unknown MySidEntryConfig type for function ID ", functionId);
  }

  auto mySid = std::make_shared<MySid>(fields);
  mySid->setUnresolveNextHopsId(std::nullopt);
  mySid->setResolvedNextHopsId(std::nullopt);
  return MySidWithNextHops{std::move(mySid), std::move(nhops), std::nullopt};
}

} // namespace

folly::IPAddressV6 buildSidAddress(
    const folly::IPAddressV6& locatorAddr,
    uint8_t locatorPrefixLen,
    const std::string& functionId) {
  // Parse function ID from hex string
  uint16_t funcVal;
  try {
    funcVal = folly::to<uint16_t>(std::stoul(functionId, nullptr, 16));
  } catch (const std::exception& e) {
    throw FbossError(
        "Invalid MySID function ID '", functionId, "': ", e.what());
  }

  // The function ID occupies the 16 bits immediately after the locator block.
  // For a /32 locator, that's bits 32-47 (the 3rd group of the IPv6 address).
  // Locator prefix length must be byte-aligned (multiple of 8).
  if (locatorPrefixLen % 8 != 0) {
    throw FbossError(
        "Locator prefix length ",
        static_cast<int>(locatorPrefixLen),
        " must be a multiple of 8");
  }
  auto bytes = locatorAddr.toByteArray();
  uint8_t funcByteOffset = locatorPrefixLen / 8;
  if (funcByteOffset + 2 > bytes.size()) {
    throw FbossError(
        "Locator prefix length ",
        locatorPrefixLen,
        " too large for function ID placement");
  }
  bytes[funcByteOffset] = static_cast<uint8_t>(funcVal >> 8);
  bytes[funcByteOffset + 1] = static_cast<uint8_t>(funcVal & 0xFF);
  return folly::IPAddressV6(bytes);
}

std::unordered_map<std::string, InterfaceID> buildPortNameToInterfaceIdMap(
    const cfg::SwitchConfig& config) {
  // Build vlanID → intfID and portID → intfID lookups from the configured
  // routed Interfaces.
  std::unordered_map<int32_t, InterfaceID> vlanIdToIntfId;
  std::unordered_map<int32_t, InterfaceID> portIdToIntfId;
  for (const auto& intf : *config.interfaces()) {
    auto intfId = InterfaceID(static_cast<int>(*intf.intfID()));
    if (*intf.type() == cfg::InterfaceType::VLAN) {
      vlanIdToIntfId[*intf.vlanID()] = intfId;
    } else if (
        *intf.type() == cfg::InterfaceType::PORT && intf.portID().has_value()) {
      portIdToIntfId[*intf.portID()] = intfId;
    }
  }

  // Resolve every physical port's L3 InterfaceID once so aggregate ports
  // can inherit their member ports' interfaces. Prefer PORT-typed Interface
  // matching logicalID (the more specific binding), else VLAN-typed
  // Interface matching ingressVlan.
  std::unordered_map<int32_t, InterfaceID> portIdToResolvedIntfId;
  for (const auto& port : *config.ports()) {
    if (auto pit = portIdToIntfId.find(*port.logicalID());
        pit != portIdToIntfId.end()) {
      portIdToResolvedIntfId[*port.logicalID()] = pit->second;
    } else if (auto vit = vlanIdToIntfId.find(*port.ingressVlan());
               vit != vlanIdToIntfId.end()) {
      portIdToResolvedIntfId[*port.logicalID()] = vit->second;
    }
  }

  std::unordered_map<std::string, InterfaceID> portNameToIntfId;
  for (const auto& port : *config.ports()) {
    auto it = portIdToResolvedIntfId.find(*port.logicalID());
    if (it == portIdToResolvedIntfId.end()) {
      continue;
    }
    if (!port.name().has_value() || port.name()->empty()) {
      throw FbossError(
          "Port logicalID=", *port.logicalID(), " has no name set");
    }
    portNameToIntfId[*port.name()] = it->second;
  }
  for (const auto& aggPort : *config.aggregatePorts()) {
    std::optional<InterfaceID> sharedIntf;
    for (const auto& member : *aggPort.memberPorts()) {
      auto rit = portIdToResolvedIntfId.find(*member.memberPortID());
      if (rit == portIdToResolvedIntfId.end()) {
        continue;
      }
      if (!sharedIntf.has_value()) {
        sharedIntf = rit->second;
      } else if (*sharedIntf != rit->second) {
        throw FbossError(
            "Aggregate port '",
            *aggPort.name(),
            "' has members on different L3 interfaces: ",
            static_cast<int32_t>(*sharedIntf),
            " vs ",
            static_cast<int32_t>(rit->second));
      }
    }
    if (sharedIntf.has_value()) {
      portNameToIntfId[*aggPort.name()] = *sharedIntf;
    }
  }
  return portNameToIntfId;
}

std::vector<MySidWithNextHops> convertMySidConfig(
    const cfg::MySidConfig& config,
    const std::unordered_map<std::string, InterfaceID>& portNameToInterfaceId) {
  std::vector<MySidWithNextHops> result;
  for (const auto& [locatorIp, locatorPrefixLen] :
       parseLocatorPrefixes(config)) {
    for (const auto& [functionId, entryConfig] : *config.entries()) {
      result.push_back(convertMySidEntry(
          locatorIp.asV6(),
          locatorPrefixLen,
          functionId,
          entryConfig,
          portNameToInterfaceId));
    }
  }
  return result;
}

} // namespace facebook::fboss
