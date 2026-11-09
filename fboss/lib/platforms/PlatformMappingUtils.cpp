// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/lib/platforms/PlatformMappingUtils.h"

#include "fboss/agent/FbossError.h"

#include <folly/FileUtil.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <filesystem>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace facebook::fboss {
namespace {

using PortNameToId = std::unordered_map<std::string_view, int32_t>;

template <typename ThriftType>
ThriftType readThriftJson(const std::string& path) {
  std::string json;
  if (!folly::readFile(path.c_str(), json)) {
    throw FbossError("Unable to read Thrift JSON file ", path);
  }

  try {
    return apache::thrift::SimpleJSONSerializer::deserialize<ThriftType>(json);
  } catch (const std::exception& ex) {
    throw FbossError(
        "Unable to parse Thrift JSON file ", path, ": ", ex.what());
  }
}

PortNameToId buildPortNameToId(const PortIdToPortAssignment& assignments) {
  PortNameToId nameToId;
  nameToId.reserve(assignments.size());
  for (const auto& [portId, assignment] : assignments) {
    const auto& portName = *assignment.portName();
    if (!nameToId.emplace(portName, portId).second) {
      throw FbossError("Duplicate port assignment for port name ", portName);
    }
  }
  return nameToId;
}

void translateSubsumedPortNames(
    cfg::PlatformPortEntry& entry,
    const PortNameToId& nameToId) {
  for (auto& [_, portConfig] : *entry.supportedProfiles()) {
    if (!portConfig.subsumedPortNames()) {
      continue;
    }
    std::vector<int32_t> subsumedPorts;
    for (const auto& portName : *portConfig.subsumedPortNames()) {
      if (auto it = nameToId.find(portName); it != nameToId.end()) {
        subsumedPorts.push_back(it->second);
      }
    }
    if (!subsumedPorts.empty()) {
      portConfig.subsumedPorts() = std::move(subsumedPorts);
    }
    portConfig.subsumedPortNames().reset();
  }
}

void translatePortConfigOverrides(
    cfg::PlatformMapping& rawPlatformMapping,
    const PortNameToId& nameToId) {
  if (!rawPlatformMapping.portConfigOverrides()) {
    return;
  }

  auto& overrides = *rawPlatformMapping.portConfigOverrides();
  for (auto overrideIt = overrides.begin(); overrideIt != overrides.end();) {
    auto& factor = *overrideIt->factor();
    if (factor.portNames()) {
      std::vector<int32_t> portIds;
      for (const auto& portName : *factor.portNames()) {
        if (auto it = nameToId.find(portName); it != nameToId.end()) {
          portIds.push_back(it->second);
        }
      }
      if (portIds.empty()) {
        overrideIt = overrides.erase(overrideIt);
        continue;
      }
      factor.ports() = std::move(portIds);
      factor.portNames().reset();
    }
    ++overrideIt;
  }
}

} // namespace

std::string resolveRawPlatformMappingPath(const std::string& path) {
  namespace fs = std::filesystem;
  const fs::path variantRawPath(path);
  if (fs::exists(variantRawPath)) {
    return variantRawPath.string();
  }

  // Variants can share the base platform's raw mapping when they have no
  // variant-specific raw mapping file. Walk upward to find the variant root.
  auto variantDir = variantRawPath.parent_path();
  while (!variantDir.empty()) {
    if (variantDir.parent_path().filename() == "variants") {
      const auto baseDir = variantDir.parent_path().parent_path();
      // Preserve the path below the variant directory under the base platform.
      const auto baseRawPath =
          baseDir / variantRawPath.lexically_relative(variantDir);
      if (fs::exists(baseRawPath)) {
        return baseRawPath.string();
      }
      break;
    }

    const auto parent = variantDir.parent_path();
    if (parent == variantDir) {
      break;
    }
    variantDir = parent;
  }

  // Keep the original path so callers retain the usual missing-file behavior.
  return variantRawPath.string();
}

cfg::PlatformMapping readRawPlatformMapping(const std::string& path) {
  auto mapping = readThriftJson<cfg::PlatformMapping>(path);
  if (!mapping.rawPlatformPorts()) {
    throw FbossError("Raw platform mapping has no rawPlatformPorts: ", path);
  }
  return mapping;
}

PortIdToPortAssignment readPortIdToPortAssignment(const std::string& path) {
  auto platformConfig = readThriftJson<cfg::PlatformConfig>(path);
  if (!platformConfig.portIdToPortAssignment()) {
    throw FbossError("Platform config has no portIdToPortAssignment: ", path);
  }
  return std::move(*platformConfig.portIdToPortAssignment());
}

cfg::PlatformMapping reconstructPlatformMapping(
    cfg::PlatformMapping rawPlatformMapping,
    const PortIdToPortAssignment& portIdToPortAssignment) {
  if (!rawPlatformMapping.rawPlatformPorts()) {
    throw FbossError("Raw platform mapping has no rawPlatformPorts");
  }

  const auto nameToId = buildPortNameToId(portIdToPortAssignment);
  std::map<int32_t, cfg::PlatformPortEntry> ports;
  for (const auto& [portId, assignment] : portIdToPortAssignment) {
    auto rawEntry =
        rawPlatformMapping.rawPlatformPorts()->find(*assignment.portName());
    if (rawEntry == rawPlatformMapping.rawPlatformPorts()->end()) {
      throw FbossError(
          "No raw platform port for assigned port ", *assignment.portName());
    }

    auto& entry = rawEntry->second;
    if (!entry.mapping()->controllingPortName()) {
      throw FbossError(
          "Raw platform port has no controllingPortName: ",
          *assignment.portName());
    }
    auto controllingPort =
        nameToId.find(*entry.mapping()->controllingPortName());
    if (controllingPort == nameToId.end()) {
      throw FbossError(
          "No port assignment for controlling port ",
          *entry.mapping()->controllingPortName());
    }
    entry.mapping()->id() = portId;
    entry.mapping()->name() = *assignment.portName();
    entry.mapping()->controllingPort() = controllingPort->second;
    entry.mapping()->controllingPortName().reset();
    entry.mapping()->portType() = *assignment.portType();
    entry.mapping()->scope() = *assignment.scope();
    if (assignment.attachedCorePortIndex()) {
      entry.mapping()->attachedCorePortIndex() =
          *assignment.attachedCorePortIndex();
    } else {
      entry.mapping()->attachedCorePortIndex().reset();
    }
    translateSubsumedPortNames(entry, nameToId);
    ports.emplace(portId, std::move(entry));
  }

  rawPlatformMapping.ports() = std::move(ports);
  translatePortConfigOverrides(rawPlatformMapping, nameToId);
  rawPlatformMapping.rawPlatformPorts().reset();
  return rawPlatformMapping;
}

cfg::PlatformMapping loadPlatformMappingFromSplitFiles(
    const std::string& rawPlatformMappingPath,
    const std::string& portIdToPortAssignmentPath) {
  return reconstructPlatformMapping(
      readRawPlatformMapping(rawPlatformMappingPath),
      readPortIdToPortAssignment(portIdToPortAssignmentPath));
}

} // namespace facebook::fboss
