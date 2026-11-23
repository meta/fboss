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

#include <string>
#include <string_view>
#include <vector>

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/CmdLocalOptions.h"

namespace facebook::fboss {

inline constexpr std::string_view kConfigGenAgentCommand = "config_gen_agent";
#ifdef IS_OSS
inline constexpr std::string_view kConfigGenerationCommand =
    "python3 fboss/lib/oss/run-helper.py --target fboss2 config gen agent --fboss-root ";
#else
inline constexpr std::string_view kConfigGenerationCommand =
    "buck2 run fbcode//fboss/cli/fboss2:fboss2 -- config gen agent --fboss-root ";
#endif
inline const std::string kConfigGenAgentPlatform = "--platform";
inline const std::string kConfigGenAgentProfile = "--profile";
inline const std::string kConfigGenAgentFbossRoot = "--fboss-root";
inline const std::string kConfigGenAgentAsicConfigVariant =
    "--asic-config-variant";
inline const std::string kConfigGenAgentAsicConfigFile = "--asic-config-file";
inline const std::string kConfigGenAgentAsicConfigType = "--asic-config-type";
inline const std::string kConfigGenAgentReferenceConfigFile =
    "--reference-config-file";
inline const std::string kConfigGenAgentOutputDirectory = "--output-dir";
inline const std::string kConfigGenAgentVerify = "--verify";

struct CmdConfigGenAgentTraits : public WriteCommandTraits {
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  static constexpr bool IS_LOCAL_COMMAND = true;
  using ObjectArgType = std::monostate;
  using RetType = std::string;
  std::vector<utils::LocalOption> LocalOptions = {
      {kConfigGenAgentPlatform,
       "Target platform; omit to generate every platform declaration"},
      {kConfigGenAgentProfile,
       "Generate one profile; omit to generate the platform declaration"},
      {kConfigGenAgentFbossRoot, "Path to the fboss source root [required]"},
      {kConfigGenAgentAsicConfigVariant,
       "ASIC configuration variant; defaults to --profile"},
      {kConfigGenAgentAsicConfigFile,
       "ASIC configuration file overriding the generated profile artifact"},
      {kConfigGenAgentAsicConfigType,
       "Type of --asic-config-file: key_value, json, or yaml"},
      {kConfigGenAgentReferenceConfigFile,
       "Agent configuration to compare with the generated output"},
      {kConfigGenAgentOutputDirectory,
       "Directory in which to create agent.conf"},
      {kConfigGenAgentVerify,
       "Verify declaration-generated configs without modifying them",
       std::nullopt,
       /*isFlag=*/true},
  };
};

/*
 * Thin fboss2 adapter for Agent config generation. The command layer owns CLI
 * option parsing and presentation only; config construction and filesystem
 * behavior live in reusable libraries that do not depend on the CLI framework.
 */
class CmdConfigGenAgent
    : public CmdHandler<CmdConfigGenAgent, CmdConfigGenAgentTraits> {
 public:
  using RetType = CmdConfigGenAgentTraits::RetType;

  // Reads the command options and returns the generated configuration paths.
  RetType queryClient(const HostInfo& hostInfo);

  // Prints the generated configuration path for the caller.
  void printOutput(const RetType& outputPath);
};

} // namespace facebook::fboss
