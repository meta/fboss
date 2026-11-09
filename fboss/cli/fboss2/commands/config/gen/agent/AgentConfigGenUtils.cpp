/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/gen/agent/AgentConfigGenUtils.h"

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include <folly/FileUtil.h>
#include <folly/json/json.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/cli/fboss2/commands/config/gen/PlatformConfigPathUtils.h"
#include "fboss/cli/fboss2/utils/ConfigFileUtils.h"
#include "fboss/configs/platforms/generic/forwarding_stack/utils/FeatureDefaultCommandArgsUtils.h" // @manual=//fboss/configs/platforms/generic/forwarding_stack/utils:config_generation_utils
#include "fboss/configs/platforms/generic/forwarding_stack/utils/LinkTopologyUtils.h"
#include "fboss/lib/config/PlatformConfigUtils.h"
#include "fboss/lib/config/agent/AclConfigUtils.h"
#include "fboss/lib/config/agent/CoppConfigUtils.h"
#include "fboss/lib/config/agent/InterfaceConfigUtils.h"
#include "fboss/lib/config/agent/PortConfigUtils.h"
#include "fboss/lib/config/agent/VlanConfigUtils.h"
#include "fboss/lib/platforms/PlatformDescriptor.h"
#include "fboss/lib/platforms/PlatformMappingUtils.h"

namespace facebook::fboss::configgen {
namespace fs = std::filesystem;
namespace {

constexpr std::string_view kAgentConfigFileName = "agent.conf";
constexpr std::string_view kDefaultProfileName = "default";
constexpr std::string_view kPortAssignmentFileName =
    "port_id_to_port_assignment.json";
constexpr std::string_view kPlatformDescriptorFileName =
    "platform_descriptor.json";
constexpr std::string_view kRawPlatformMappingFileName =
    "raw_platform_mapping.json";
constexpr std::string_view kSaiUserDefinedTrapArg = "sai_user_defined_trap";

struct GeneratedAsicConfigFile {
  fs::path path;
  cfg::AsicConfigType configType;
};

struct GeneratedPlatformMappingArtifactPaths {
  fs::path colocated;
  fs::path legacy;
};

bool isCommandLineArgEnabled(
    const std::map<std::string, std::string>& defaultCommandLineArgs,
    std::string_view name) {
  const auto it = defaultCommandLineArgs.find(std::string(name));
  return it != defaultCommandLineArgs.end() && it->second == "true";
}

std::string normalizeAsicConfigVariant(
    std::string_view profile,
    std::optional<std::string_view> asicConfigVariant) {
  const auto variant = std::string(asicConfigVariant.value_or(profile));
  const fs::path component{variant};
  if (component.empty() || component.has_parent_path() || component == "." ||
      component == "..") {
    throw FbossError("Invalid ASIC config variant: ", variant);
  }
  return variant;
}

std::string readFile(const fs::path& path) {
  std::string contents;
  if (!folly::readFile(path.c_str(), contents)) {
    throw FbossError("Unable to read config-generation input ", path.string());
  }
  return contents;
}

std::optional<std::string> getConfigTypeName(
    const folly::dynamic& variant,
    const fs::path& metadataPath) {
  if (!variant.isObject()) {
    throw FbossError(
        "ASIC config variant in ", metadataPath.string(), " is not an object");
  }
  if (!variant.count("asic_config_params")) {
    return std::nullopt;
  }

  const auto& params = variant["asic_config_params"];
  if (!params.isObject()) {
    throw FbossError(
        "asic_config_params in ", metadataPath.string(), " is not an object");
  }
  if (!params.count("config_type")) {
    return std::nullopt;
  }
  if (!params["config_type"].isString()) {
    throw FbossError(
        "config_type in ", metadataPath.string(), " is not a string");
  }
  return params["config_type"].asString();
}

std::string_view getGeneratedFileExtension(cfg::AsicConfigType configType) {
  switch (configType) {
    case cfg::AsicConfigType::KEY_VALUE_CONFIG:
    case cfg::AsicConfigType::JSON_CONFIG:
      return ".json";
    case cfg::AsicConfigType::YAML_CONFIG:
      return ".yml";
    case cfg::AsicConfigType::NONE:
      throw FbossError("ASIC config type NONE does not have a generated file");
  }
  throw FbossError("Unknown ASIC config type");
}

GeneratedAsicConfigFile resolveGeneratedAsicConfig(
    const PlatformConfigDirectory& platformDirectory,
    std::string_view platform,
    std::string_view asicConfigVariant,
    const std::optional<fs::path>& asicConfigFile = std::nullopt,
    const std::optional<cfg::AsicConfigType>& asicConfigType = std::nullopt) {
  if (asicConfigType && !asicConfigFile) {
    throw FbossError("ASIC config type requires an ASIC config file override");
  }
  if (asicConfigFile && asicConfigType) {
    if (!fs::is_regular_file(*asicConfigFile)) {
      throw FbossError(
          "ASIC config override does not exist: ", asicConfigFile->string());
    }
    return {.path = *asicConfigFile, .configType = *asicConfigType};
  }

  const auto asicConfigDirectory =
      findPlatformConfigComponentDirectory(platformDirectory, "asic_config");
  const auto metadataPath = asicConfigDirectory / "asic_config.json";
  if (!fs::is_regular_file(metadataPath)) {
    throw FbossError(
        "ASIC config metadata does not exist: ", metadataPath.string());
  }

  const auto metadata = folly::parseJson(readFile(metadataPath));
  const auto platformName = std::string(platform);
  const auto variantName = std::string(asicConfigVariant);
  if (!metadata.isObject() || !metadata.count("platform_name") ||
      !metadata["platform_name"].isString() ||
      metadata["platform_name"].asString() != platformName) {
    throw FbossError(
        "ASIC config metadata ",
        metadataPath.string(),
        " does not describe platform '",
        platform,
        "'");
  }
  if (!metadata.count("variants") || !metadata["variants"].isObject() ||
      !metadata["variants"].count(variantName)) {
    throw FbossError(
        "ASIC config variant '",
        asicConfigVariant,
        "' does not exist for platform '",
        platform,
        "'");
  }

  std::optional<std::string> configTypeName;
  if (metadata.count("defaults")) {
    configTypeName = getConfigTypeName(metadata["defaults"], metadataPath);
  }
  if (const auto variantConfigType =
          getConfigTypeName(metadata["variants"][variantName], metadataPath)) {
    configTypeName = variantConfigType;
  }
  if (!configTypeName) {
    throw FbossError(
        "ASIC config metadata ",
        metadataPath.string(),
        " does not define config_type for variant '",
        asicConfigVariant,
        "'");
  }
  cfg::AsicConfigType configType;
  if (!apache::thrift::util::tryParseEnum(*configTypeName, &configType)) {
    throw FbossError(
        "Unsupported ASIC config type '",
        *configTypeName,
        "' in ",
        metadataPath.string());
  }

  fs::path selectedPath;
  if (asicConfigFile) {
    selectedPath = *asicConfigFile;
  } else {
    auto fileName = variantName;
    fileName += getGeneratedFileExtension(configType);
    selectedPath = asicConfigDirectory / "generated" / fileName;
  }
  if (!fs::is_regular_file(selectedPath)) {
    throw FbossError(
        asicConfigFile ? "ASIC config override does not exist: "
                       : "Generated ASIC config does not exist: ",
        selectedPath.string());
  }
  return {.path = std::move(selectedPath), .configType = configType};
}

std::map<std::string, std::string> loadKeyValueConfig(
    const std::string& contents,
    const fs::path& path) {
  const auto json = folly::parseJson(contents);
  if (!json.isObject()) {
    throw FbossError(
        "Key-value ASIC config is not a JSON object: ", path.string());
  }

  std::map<std::string, std::string> config;
  for (const auto& [key, value] : json.items()) {
    if (!key.isString() || !value.isString()) {
      throw FbossError(
          "Key-value ASIC config must contain only string values: ",
          path.string());
    }
    config.emplace(key.asString(), value.asString());
  }
  return config;
}

cfg::ChipConfig loadAsicConfig(const GeneratedAsicConfigFile& generatedFile) {
  const auto contents = readFile(generatedFile.path);
  cfg::AsicConfigEntry common;
  switch (generatedFile.configType) {
    case cfg::AsicConfigType::KEY_VALUE_CONFIG:
      common.set_config(loadKeyValueConfig(contents, generatedFile.path));
      break;
    case cfg::AsicConfigType::JSON_CONFIG:
      folly::parseJson(contents);
      common.set_jsonConfig(contents);
      break;
    case cfg::AsicConfigType::YAML_CONFIG:
      common.set_yamlConfig(contents);
      break;
    case cfg::AsicConfigType::NONE:
      throw FbossError("ASIC config type NONE cannot be loaded");
  }

  cfg::AsicConfig asicConfig;
  asicConfig.common() = std::move(common);

  cfg::ChipConfig chipConfig;
  chipConfig.set_asicConfig(std::move(asicConfig));
  return chipConfig;
}

GeneratedPlatformMappingArtifactPaths getGeneratedPlatformMappingArtifactPaths(
    const fs::path& fbossRoot,
    const PlatformConfigDirectory& platformDirectory,
    std::string_view platform,
    std::string_view fileName) {
  return {
      .colocated =
          platformDirectory.path / "platform_mapping" / "generated" / fileName,
      .legacy = fbossRoot / "lib" / "platform_mapping_v2" /
          "generated_platform_mappings" / platformDirectory.systemVendor /
          std::string(platform) / fileName,
  };
}

std::optional<fs::path> findExistingGeneratedPlatformMappingArtifact(
    const GeneratedPlatformMappingArtifactPaths& paths) {
  if (fs::is_regular_file(paths.colocated)) {
    return paths.colocated;
  }
  if (fs::is_regular_file(paths.legacy)) {
    return paths.legacy;
  }
  return std::nullopt;
}

void addMatchingPlatformDescriptors(
    const fs::path& directory,
    std::string_view platform,
    std::set<fs::path>& loadedDescriptorPaths,
    std::vector<std::pair<fs::path, PlatformDescriptor>>& matches) {
  if (!fs::is_directory(directory)) {
    return;
  }

  for (const auto& entry : fs::directory_iterator(directory)) {
    const auto descriptorPath = entry.path() / kPlatformDescriptorFileName;
    if (!entry.is_directory() || !fs::is_regular_file(descriptorPath)) {
      continue;
    }
    if (!loadedDescriptorPaths.insert(descriptorPath.lexically_normal())
             .second) {
      continue;
    }
    auto descriptor =
        PlatformDescriptorRegistry::loadPlatformDescriptorFromFile(
            descriptorPath.string());
    const auto& modeNames = *descriptor.modeNames();
    if (std::find(modeNames.begin(), modeNames.end(), platform) !=
        modeNames.end()) {
      matches.emplace_back(descriptorPath, std::move(descriptor));
    }
  }
}

std::pair<fs::path, PlatformDescriptor> findPlatformDescriptorByVariantScan(
    const GeneratedPlatformMappingArtifactPaths& paths,
    std::string_view platform) {
  std::vector<std::pair<fs::path, PlatformDescriptor>> matches;
  std::set<fs::path> loadedDescriptorPaths;
  addMatchingPlatformDescriptors(
      paths.colocated.parent_path(), platform, loadedDescriptorPaths, matches);
  addMatchingPlatformDescriptors(
      paths.legacy.parent_path().parent_path(),
      platform,
      loadedDescriptorPaths,
      matches);
  if (matches.empty()) {
    throw FbossError(
        "Generated platform descriptor does not exist for platform '",
        platform,
        "'");
  }

  std::sort(
      matches.begin(), matches.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
      });
  const auto expectedAsicType = *matches.front().second.asicType();
  const auto expectedNumSwitchAsics = *matches.front().second.numSwitchAsics();
  for (const auto& [path, descriptor] : matches) {
    if (*descriptor.asicType() != expectedAsicType ||
        *descriptor.numSwitchAsics() != expectedNumSwitchAsics) {
      throw FbossError(
          "Platform descriptor variants disagree on switch properties for '",
          platform,
          "'; conflicting descriptor: ",
          path.string());
    }
  }
  return std::move(matches.front());
}

std::pair<fs::path, PlatformDescriptor> resolvePlatformDescriptor(
    const fs::path& fbossRoot,
    const PlatformConfigDirectory& platformDirectory,
    std::string_view platform) {
  const auto paths = getGeneratedPlatformMappingArtifactPaths(
      fbossRoot, platformDirectory, platform, kPlatformDescriptorFileName);
  if (auto path = findExistingGeneratedPlatformMappingArtifact(paths)) {
    auto descriptor =
        PlatformDescriptorRegistry::loadPlatformDescriptorFromFile(
            path->string());
    return {*path, std::move(descriptor)};
  }
  return findPlatformDescriptorByVariantScan(paths, platform);
}

std::unique_ptr<HwAsic> generateHwAsic(
    const cfg::SwitchSettings& switchSettings) {
  if (switchSettings.switchIdToSwitchInfo()->empty()) {
    throw FbossError("Switch settings do not contain switch information");
  }
  // Even for multiple NPU platforms, we should only have one AsicType
  const auto& [switchId, switchInfo] =
      *switchSettings.switchIdToSwitchInfo()->begin();
  auto asicSwitchInfo = switchInfo;
  // HwAsic stores a per-switch MAC because runtime packet handling uses it and
  // the constructor enfoces it. For config generation, we only need
  // HwAsic::isSupported() and ASIC type/vendor methods. The MAC is not relevant
  // to config generation.
  constexpr std::string_view kMockAsicMac = "02:00:00:00:00:01";
  asicSwitchInfo.switchMac() = kMockAsicMac;
  return HwAsic::makeAsic(switchId, asicSwitchInfo, std::nullopt, std::nullopt);
}

void addDefaultProfilePortGraph(
    cfg::SwitchConfig& switchConfig,
    const PlatformMapping& platformMapping,
    const HwAsic& asic) {
  const auto& platformPorts = platformMapping.getPlatformPorts();

  const auto portGroups = utility::getSubsidiaryPortIDs(platformPorts);
  std::map<PortID, std::vector<PortID>> supportedPortGroups;
  std::set<PortID> controllingPorts;
  for (const auto& group : portGroups) {
    const auto& portEntryMapping =
        platformMapping.getPlatformPort(group.first).mapping();
    const auto portType = *portEntryMapping->portType();
    if (portType != cfg::PortType::INTERFACE_PORT &&
        portType != cfg::PortType::MANAGEMENT_PORT) {
      XLOG(WARN) << "Skipping unsupported port " << group.first << " ('"
                 << *portEntryMapping->name() << "') with type "
                 << apache::thrift::util::enumNameSafe(portType)
                 << " during default profile config generation";
      continue;
    }
    supportedPortGroups.emplace(group.first, group.second);
    controllingPorts.insert(group.first);
  }
  // A standalone default config intentionally programs only controlling
  // ports, so safe profiles may subsume unrequired subsidiary ports.
  const utility::SafeProfileSelectionOptions options{
      .asicType = asic.getAsicType(),
      .supportsAddRemovePort = true,
      .preferOpticalProfiles = true,
      .requiredPorts = std::move(controllingPorts),
  };
  const auto portProfiles =
      utility::getSafeProfileIDs(platformMapping, supportedPortGroups, options);

  auto interfaceVlanID = utility::kInterfaceVlanIdMin;
  auto managementVlanID = utility::kManagementVlanIdMax;
  for (const auto& [portID, profileID] : portProfiles) {
    if (interfaceVlanID > managementVlanID) {
      throw FbossError("No free VLAN ID available for default port config");
    }
    const auto portType =
        *platformMapping.getPlatformPort(portID).mapping()->portType();
    int32_t vlanID;
    if (portType == cfg::PortType::MANAGEMENT_PORT) {
      // Allocate management VLANs downward to keep them separate from regular
      // interface VLANs and later low-to-high port additions.
      vlanID = managementVlanID--;
    } else {
      // Allocate regular interface VLANs upward from the low end.
      vlanID = interfaceVlanID++;
    }
    utility::addRoutedPortToConfig(
        switchConfig, &platformMapping, portID, profileID, VlanID(vlanID));
    if (portType == cfg::PortType::MANAGEMENT_PORT) {
      // Keep the management L3 interface logically up independently of the
      // physical port state, matching the established COOP configuration.
      auto& managementInterface = switchConfig.interfaces()->back();
      managementInterface.isVirtual() = true;
      managementInterface.isStateSyncDisabled() = true;
    }
    auto& port = switchConfig.ports()->back();
    port.state() = cfg::PortState::ENABLED;
  }

  utility::addDefaultVlan(switchConfig, asic);
  utility::addDefaultLoopbackInterface(switchConfig, asic);
}

struct LinkTopologyEndpoint {
  std::string portName;
  std::optional<std::string> peer;
  cfg::PortProfileID profile;
};

std::map<PortID, LinkTopologyEndpoint> resolveLinkTopologyEndpoints(
    const LinkTopology& topology,
    const PlatformMapping& platformMapping) {
  std::map<PortID, LinkTopologyEndpoint> endpoints;
  const auto addEndpoint = [&](const std::string& portName,
                               std::optional<std::string> peerName,
                               cfg::PortProfileID profile) {
    const auto portID = platformMapping.getPortID(portName);
    if (!endpoints
             .emplace(
                 portID,
                 LinkTopologyEndpoint{
                     .portName = portName,
                     .peer = std::move(peerName),
                     .profile = profile})
             .second) {
      throw FbossError(
          "Link topology resolves more than one endpoint to port '",
          portName,
          "'");
    }
  };

  for (const auto& link : topology) {
    addEndpoint(link.portA, link.portB, link.profile);
    if (link.portB && *link.portB != link.portA) {
      addEndpoint(*link.portB, link.portA, link.profile);
    }
  }
  return endpoints;
}

void enableTopologyPort(
    cfg::SwitchConfig& switchConfig,
    PortID portID,
    const LinkTopologyEndpoint& endpoint) {
  auto portIt = std::find_if(
      switchConfig.ports()->begin(),
      switchConfig.ports()->end(),
      [portID](const auto& port) {
        return *port.logicalID() == static_cast<int32_t>(portID);
      });
  if (portIt == switchConfig.ports()->end()) {
    throw FbossError(
        "Link topology profile application did not create port '",
        endpoint.portName,
        "'");
  }
  if (*portIt->portType() == cfg::PortType::MANAGEMENT_PORT) {
    // Routed management interfaces remain logically up independently of the
    // physical port state, matching the default port graph behavior.
    const auto managementInterface = std::find_if(
        switchConfig.interfaces()->begin(),
        switchConfig.interfaces()->end(),
        [&](const auto& interface) {
          return *interface.vlanID() == *portIt->ingressVlan();
        });
    if (managementInterface == switchConfig.interfaces()->end()) {
      throw FbossError(
          "No routed interface exists for management port '",
          endpoint.portName,
          "'");
    }
    managementInterface->isVirtual() = true;
    managementInterface->isStateSyncDisabled() = true;
  }
  portIt->state() = cfg::PortState::ENABLED;
  if (endpoint.peer) {
    portIt->expectedLLDPValues()[cfg::LLDPTag::PORT] = *endpoint.peer;
  }
}

} // namespace

void applyLinkTopology(
    cfg::SwitchConfig& switchConfig,
    const PlatformMapping& platformMapping,
    const LinkTopology& topology) {
  // Resolve every named endpoint before mutating the config so invalid or
  // ambiguous platform mappings fail without partially applying the topology.
  const auto endpoints =
      resolveLinkTopologyEndpoints(topology, platformMapping);
  std::vector<utility::PortProfileRequest> requests;
  requests.reserve(endpoints.size());
  for (const auto& [_, endpoint] : endpoints) {
    requests.push_back(
        {.portName = endpoint.portName, .profileID = endpoint.profile});
  }
  const auto result =
      utility::applyPortProfileChanges(switchConfig, platformMapping, requests);
  // Profile changes can collapse a breakout group. Remove the subsumed ports
  // and their dependent VLAN/interface state before enabling the final graph.
  if (!result.subsumedPorts.empty()) {
    std::set<PortID> subsumedPortIDs;
    for (const auto& [portID, _] : result.subsumedPorts) {
      subsumedPortIDs.insert(portID);
    }
    utility::removePortsFromConfig(switchConfig, subsumedPortIDs);
  }

  // Only topology endpoints remain active. Clear any default-graph LLDP state
  // before adding peer expectations for explicitly paired endpoints.
  for (auto& port : *switchConfig.ports()) {
    port.state() = cfg::PortState::DISABLED;
    port.expectedLLDPValues()->clear();
  }
  for (const auto& [portID, endpoint] : endpoints) {
    enableTopologyPort(switchConfig, portID, endpoint);
  }
}

void assignLinkTestInterfaceIpAddresses(cfg::SwitchConfig& switchConfig) {
  // Preserve the legacy 10.0.0.0/24, 11.0.0.0/24, ... sequence until the
  // first octet reaches 224. Then wrap it to 10 and increment the second
  // octet, producing 10.1.0.0/24, 11.1.0.0/24, ... so configurations can
  // assign unique IPv4 subnets to more than 250 interfaces.
  constexpr size_t kIpv4FirstOctetStart = 10;
  constexpr size_t kIpv4OctetLimit = 224;
  constexpr size_t kIpv4FirstOctetCount =
      kIpv4OctetLimit - kIpv4FirstOctetStart;
  // IPv6 hextets are emitted as decimal digits to preserve the legacy output.
  // 9999 is the largest such value that still fits in one hexadecimal hextet.
  constexpr size_t kIpv6FirstHextetStart = 2400;
  constexpr size_t kIpv6FirstHextetLimit = 10000;
  constexpr size_t kMaxInterfaces =
      kIpv6FirstHextetLimit - kIpv6FirstHextetStart;
  if (switchConfig.interfaces()->size() > kMaxInterfaces) {
    throw FbossError(
        "Link-test interface address assignment supports at most ",
        kMaxInterfaces,
        " interfaces");
  }
  for (size_t index = 0; index < switchConfig.interfaces()->size(); ++index) {
    const auto ipv4FirstOctet =
        kIpv4FirstOctetStart + index % kIpv4FirstOctetCount;
    const auto ipv4SecondOctet = index / kIpv4FirstOctetCount;
    switchConfig.interfaces()[index].ipAddresses() = {
        std::to_string(kIpv6FirstHextetStart + index) + "::/64",
        std::to_string(ipv4FirstOctet) + "." + std::to_string(ipv4SecondOctet) +
            ".0.0/24",
    };
  }
}

cfg::AsicConfigType parseAsicConfigType(std::string_view configType) {
  if (configType == "key_value") {
    return cfg::AsicConfigType::KEY_VALUE_CONFIG;
  }
  if (configType == "json") {
    return cfg::AsicConfigType::JSON_CONFIG;
  }
  if (configType == "yaml") {
    return cfg::AsicConfigType::YAML_CONFIG;
  }
  throw FbossError(
      "Unsupported ASIC config type '",
      configType,
      "'; expected key_value, json, or yaml");
}

cfg::AgentConfig assembleAgentConfig(
    std::map<std::string, std::string> defaultCommandLineArgs,
    cfg::SwitchConfig sw,
    cfg::PlatformConfig platform) {
  cfg::AgentConfig config;
  config.defaultCommandLineArgs() = std::move(defaultCommandLineArgs);
  config.sw() = std::move(sw);
  config.platform() = std::move(platform);
  return config;
}

cfg::PlatformConfig assemblePlatformConfig(
    cfg::ChipConfig chipConfig,
    std::map<int32_t, cfg::PortAssignment> portAssignments) {
  cfg::PlatformConfig platformConfig;
  platformConfig.chip() = std::move(chipConfig);
  platformConfig.portIdToPortAssignment() = std::move(portAssignments);
  return platformConfig;
}

fs::path findGeneratedAsicConfig(
    const fs::path& fbossRoot,
    std::string_view platform,
    std::string_view asicConfigVariant) {
  const auto platformDirectory =
      findPlatformConfigDirectory(fbossRoot, platform);
  return resolveGeneratedAsicConfig(
             platformDirectory,
             platform,
             normalizeAsicConfigVariant(
                 kDefaultProfileName,
                 asicConfigVariant.empty()
                     ? std::nullopt
                     : std::optional<std::string_view>(asicConfigVariant)))
      .path;
}

fs::path findPortIdToPortAssignmentConfig(
    const fs::path& fbossRoot,
    std::string_view platform) {
  const auto descriptorPath =
      findPlatformDescriptorConfigWithDescriptor(fbossRoot, platform).first;
  const auto assignmentPath =
      descriptorPath.parent_path() / kPortAssignmentFileName;
  if (!fs::is_regular_file(assignmentPath)) {
    throw FbossError(
        "Generated ",
        kPortAssignmentFileName,
        " does not exist next to selected platform descriptor ",
        descriptorPath.string());
  }
  return assignmentPath;
}

std::pair<fs::path, PlatformDescriptor>
findPlatformDescriptorConfigWithDescriptor(
    const fs::path& fbossRoot,
    std::string_view platform) {
  const auto platformDirectory =
      findPlatformConfigDirectory(fbossRoot, platform);
  return resolvePlatformDescriptor(fbossRoot, platformDirectory, platform);
}

ResolvedAgentConfigInputs resolveAgentConfigInputs(
    const fs::path& fbossRoot,
    std::string_view platform,
    std::string_view profile,
    const std::optional<fs::path>& asicConfigFile,
    const std::optional<cfg::AsicConfigType>& asicConfigType,
    std::optional<std::string_view> asicConfigVariant,
    std::optional<std::string_view> linkTopologyVariant) {
  const auto normalizedProfile = normalizeProfile(profile);
  const auto profileName = getProfileName(normalizedProfile);
  const auto normalizedAsicConfigVariant =
      normalizeAsicConfigVariant(profileName, asicConfigVariant);
  const auto platformDirectory =
      findPlatformConfigDirectory(fbossRoot, platform);
  auto [descriptorPath, descriptor] =
      resolvePlatformDescriptor(fbossRoot, platformDirectory, platform);
  const auto mappingDirectory = descriptorPath.parent_path();
  const auto rawMappingPath = resolveRawPlatformMappingPath(
      (mappingDirectory / kRawPlatformMappingFileName).string());
  const auto assignmentPath = mappingDirectory / kPortAssignmentFileName;
  auto portAssignments = readPortIdToPortAssignment(assignmentPath.string());
  auto platformMapping =
      std::make_unique<PlatformMapping>(reconstructPlatformMapping(
          readRawPlatformMapping(rawMappingPath), portAssignments));
  std::optional<LinkTopology> linkTopology;
  if (linkTopologyVariant) {
    linkTopology =
        loadLinkTopologyVariant(platformDirectory.path, *linkTopologyVariant);
  }

  return {
      .profile = normalizedProfile,
      .platformDescriptor = std::move(descriptor),
      .portAssignments = std::move(portAssignments),
      .platformMapping = std::move(platformMapping),
      .linkTopology = std::move(linkTopology),
      .chipConfig = loadAsicConfig(resolveGeneratedAsicConfig(
          platformDirectory,
          platform,
          normalizedAsicConfigVariant,
          asicConfigFile,
          asicConfigType)),
  };
}

cfg::SwitchSettings generateSwitchSettings(
    const PlatformDescriptor& platformDescriptor) {
  // TODO(@joseph5wu): Support multi-NPU platforms and non-NPU switch types.
  // For now, only single-NPU platforms with SwitchType::NPU are supported.
  const auto numSwitchAsics = *platformDescriptor.numSwitchAsics();
  if (numSwitchAsics <= 0) {
    throw FbossError(
        "Platform descriptor has invalid numSwitchAsics ", numSwitchAsics);
  }
  if (numSwitchAsics > 1) {
    throw FbossError(
        "SwitchSettings generation does not support multi-ASIC platforms; "
        "numSwitchAsics is ",
        numSwitchAsics);
  }

  cfg::Range64 portIdRange;
  portIdRange.minimum() =
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN();
  portIdRange.maximum() =
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX();

  cfg::SwitchInfo switchInfo;
  switchInfo.switchType() = cfg::SwitchType::NPU;
  switchInfo.asicType() = *platformDescriptor.asicType();
  switchInfo.switchIndex() = 0;
  switchInfo.portIdRange() = std::move(portIdRange);

  cfg::SwitchSettings switchSettings;
  switchSettings.switchType() = cfg::SwitchType::NPU;
  switchSettings.switchIdToSwitchInfo() = {{0, std::move(switchInfo)}};
  // TODO(@joseph5wu): Remove this legacy field through T286888882. Most SAI
  // HW-test configs and production COOP configs set it to true; D48978980
  // derives the production value from the presence of saiSdk.
  switchSettings.needL2EntryForNeighbor() = true;
  return switchSettings;
}

cfg::SwitchConfig generateSwitchConfig(
    const ResolvedAgentConfigInputs& inputs,
    const std::map<std::string, std::string>& defaultCommandLineArgs) {
  auto switchSettings = generateSwitchSettings(inputs.platformDescriptor);
  auto asic = generateHwAsic(switchSettings);
  if (!asic) {
    throw FbossError("Unable to construct HwAsic from switch settings");
  }
  // TODO(@joseph5wu) Only update the l2LearningMode for `default` profile. Once
  // FBOSS Agent team finishes migrating test config to use Software mode for
  // TH4/5/6, we can remove the profile limitation here
  if (inputs.profile == ConfigProfileType::DEFAULT) {
    switchSettings.l2LearningMode() = asic->getDefaultL2LearningMode();
  }

  cfg::SwitchConfig switchConfig;
  switchConfig.switchSettings() = std::move(switchSettings);
  utility::setupDefaultAclTableGroups(switchConfig, *asic);

  // DEFAULT and LINK_TEST are standalone service configurations, so they need
  // a usable port graph and control-plane policy. HW_TEST and BENCHMARK remain
  // intentionally minimal because their test fixtures construct the required
  // switch state.
  if (inputs.profile == ConfigProfileType::DEFAULT ||
      inputs.profile == ConfigProfileType::LINK_TEST) {
    utility::addDefaultCpuQueueConfig(switchConfig, *asic);
    utility::addDefaultCpuTrafficPolicyConfig(
        switchConfig,
        *asic,
        isCommandLineArgEnabled(
            defaultCommandLineArgs, kSaiUserDefinedTrapArg));
    addDefaultProfilePortGraph(switchConfig, *inputs.platformMapping, *asic);

    if (inputs.linkTopology) {
      applyLinkTopology(
          switchConfig, *inputs.platformMapping, *inputs.linkTopology);
    }
    // Link tests derive ECMP next hops from non-link-local interface addresses.
    if (inputs.profile == ConfigProfileType::LINK_TEST) {
      assignLinkTestInterfaceIpAddresses(switchConfig);
    }
  }
  return switchConfig;
}

cfg::PlatformConfig generatePlatformConfig(
    const ResolvedAgentConfigInputs& inputs) {
  return assemblePlatformConfig(inputs.chipConfig, inputs.portAssignments);
}

namespace {

std::string generateAgentConfigContents(
    std::string_view platform,
    std::string_view profile,
    const fs::path& fbossRoot,
    const std::optional<fs::path>& asicConfigFile,
    const std::optional<cfg::AsicConfigType>& asicConfigType,
    std::optional<std::string_view> asicConfigVariant,
    std::optional<std::string_view> linkTopologyVariant) {
  auto inputs = resolveAgentConfigInputs(
      fbossRoot,
      platform,
      profile,
      asicConfigFile,
      asicConfigType,
      asicConfigVariant,
      linkTopologyVariant);
  auto defaultCommandLineArgs = generateFeatureDefaultCommandArgs(
      fbossRoot,
      ServiceType::AGENT,
      getProfileName(inputs.profile),
      *inputs.platformDescriptor.asicType(),
      *inputs.platformDescriptor.platformType());
  auto switchConfig = generateSwitchConfig(inputs, defaultCommandLineArgs);
  auto config = assembleAgentConfig(
      std::move(defaultCommandLineArgs),
      std::move(switchConfig),
      generatePlatformConfig(inputs));
  return utils::serializeToPrettyJson(config) + "\n";
}

} // namespace

fs::path generateAgentConfig(
    std::string_view platform,
    std::string_view profile,
    const fs::path& fbossRoot,
    const std::optional<fs::path>& outputDirectory,
    const std::optional<fs::path>& asicConfigFile,
    const std::optional<cfg::AsicConfigType>& asicConfigType,
    std::optional<std::string_view> asicConfigVariant) {
  auto directory = utils::prepareOutputDirectory(outputDirectory);
  auto outputPath = directory / kAgentConfigFileName;
  utils::writeFileWithoutOverwrite(
      outputPath,
      generateAgentConfigContents(
          platform,
          profile,
          fbossRoot,
          asicConfigFile,
          asicConfigType,
          asicConfigVariant,
          std::nullopt));
  return outputPath;
}

std::string generateAgentConfigForManifest(
    const ConfigGenerationRequest& request) {
  if (request.service != ServiceType::AGENT) {
    throw FbossError("Agent config generator received a non-Agent request");
  }
  return generateAgentConfigContents(
      request.platform,
      request.profile,
      request.fbossRoot,
      std::nullopt,
      std::nullopt,
      request.asicConfigVariant
          ? std::optional<std::string_view>(*request.asicConfigVariant)
          : std::nullopt,
      request.linkTopologyVariant
          ? std::optional<std::string_view>(*request.linkTopologyVariant)
          : std::nullopt);
}

} // namespace facebook::fboss::configgen
