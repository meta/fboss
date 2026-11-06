/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/cli/fboss2/commands/config/gen/ConfigGenerationUtils.h"
#include "fboss/configs/platforms/generic/forwarding_stack/utils/LinkTopologyUtils.h"
#include "fboss/lib/platforms/gen-cpp2/platform_descriptor_types.h"

namespace facebook::fboss::configgen {

struct ResolvedAgentConfigInputs {
  ConfigProfileType profile;
  // PlatformMapping inputs
  PlatformDescriptor platformDescriptor;
  std::map<int32_t, cfg::PortAssignment> portAssignments;
  std::unique_ptr<PlatformMapping> platformMapping;
  std::optional<LinkTopology> linkTopology;
  // Asic config inputs
  cfg::ChipConfig chipConfig;
};

/*
 * Agent config generation is kept separate from the fboss2 command adapter so
 * other binaries can reuse it. The design follows AgentConfig's top-level
 * ownership boundaries: platform, sw, and defaultCommandLineArgs. Generation
 * logic for each field should be added only as that capability is supported,
 * rather than introducing speculative configuration models up front.
 */

// Combines independently constructed AgentConfig sections. Fields not listed
// here retain their Thrift defaults.
cfg::AgentConfig assembleAgentConfig(
    std::map<std::string, std::string> defaultCommandLineArgs,
    cfg::SwitchConfig sw,
    cfg::PlatformConfig platform);

// Parses a loadable ASIC config type accepted by --asic-config-type.
cfg::AsicConfigType parseAsicConfigType(std::string_view configType);

// Combines the ASIC configuration and deployment-specific port assignments
// into the platform section of an AgentConfig.
cfg::PlatformConfig assemblePlatformConfig(
    cfg::ChipConfig chipConfig,
    std::map<int32_t, cfg::PortAssignment> portAssignments);

// Resolves the generated ASIC configuration selected by platform and variant.
// An empty variant selects "default". The file extension is derived from
// config_type in asic_config.json.
std::filesystem::path findGeneratedAsicConfig(
    const std::filesystem::path& fbossRoot,
    std::string_view platform,
    std::string_view asicConfigVariant);

// Resolves the generated port-assignment artifact selected with the platform
// descriptor. Colocated and legacy centralized layouts are both supported.
std::filesystem::path findPortIdToPortAssignmentConfig(
    const std::filesystem::path& fbossRoot,
    std::string_view platform);

// Resolves and loads the generated platform descriptor used to derive
// platform-level switch properties such as ASIC type and switch ASIC count.
// Returning both values lets callers reuse descriptors parsed during variant
// resolution instead of reading the selected file again.
std::pair<std::filesystem::path, PlatformDescriptor>
findPlatformDescriptorConfigWithDescriptor(
    const std::filesystem::path& fbossRoot,
    std::string_view platform);

// Resolves and loads one coherent set of inputs for agent config generation.
// The profile must use a canonical config-generation profile name. Raw mapping
// and port assignment artifacts are loaded from the selected descriptor's
// directory.
ResolvedAgentConfigInputs resolveAgentConfigInputs(
    const std::filesystem::path& fbossRoot,
    std::string_view platform,
    std::string_view profile,
    const std::optional<std::filesystem::path>& asicConfigFile = std::nullopt,
    const std::optional<cfg::AsicConfigType>& asicConfigType = std::nullopt,
    std::optional<std::string_view> asicConfigVariant = std::nullopt,
    std::optional<std::string_view> linkTopologyVariant = std::nullopt);

// Generates SwitchSettings for a single NPU platform. Multi-ASIC platforms are
// rejected until their switch IDs and indexes can be supplied explicitly.
cfg::SwitchSettings generateSwitchSettings(
    const PlatformDescriptor& platformDescriptor);

// Constructs the switch section of an AgentConfig from resolved inputs and
// command-line arguments that affect the generated switch configuration.
cfg::SwitchConfig generateSwitchConfig(
    const ResolvedAgentConfigInputs& inputs,
    const std::map<std::string, std::string>& defaultCommandLineArgs = {});

// Applies requested profiles and modeled LLDP peers, removes ports subsumed by
// those profiles, and disables ports outside the topology.
void applyLinkTopology(
    cfg::SwitchConfig& switchConfig,
    const PlatformMapping& platformMapping,
    const LinkTopology& topology);

// Assigns deterministic IPv4 and IPv6 subnets to link-test interfaces.
void assignLinkTestInterfaceIpAddresses(cfg::SwitchConfig& switchConfig);

// Constructs the platform section of an AgentConfig from resolved inputs.
cfg::PlatformConfig generatePlatformConfig(
    const ResolvedAgentConfigInputs& inputs);

// Generates a new agent.conf and returns its path. The output is written to a
// unique temporary directory by default and never overwrites an existing file.
std::filesystem::path generateAgentConfig(
    std::string_view platform,
    std::string_view profile,
    const std::filesystem::path& fbossRoot,
    const std::optional<std::filesystem::path>& outputDirectory = std::nullopt,
    const std::optional<std::filesystem::path>& asicConfigFile = std::nullopt,
    const std::optional<cfg::AsicConfigType>& asicConfigType = std::nullopt,
    std::optional<std::string_view> asicConfigVariant = std::nullopt);

// Produces serialized Agent config contents for a generic declaration request.
std::string generateAgentConfigForManifest(
    const ConfigGenerationRequest& request);

} // namespace facebook::fboss::configgen
