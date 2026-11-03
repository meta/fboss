/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/gen/agent/CmdConfigGenAgent.h"
#include "fboss/cli/fboss2/commands/config/gen/agent/AgentConfigComparisonUtils.h"
#include "fboss/cli/fboss2/commands/config/gen/agent/AgentConfigGenUtils.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <CLI/App.hpp>
#include <folly/FileUtil.h>
#include <folly/testing/TestUtil.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "fboss/agent/FbossError.h"
#include "fboss/cli/fboss2/CmdList.h"
#include "fboss/cli/fboss2/CmdLocalOptions.h"
#include "fboss/cli/fboss2/CmdSubcommands.h"
#include "fboss/cli/fboss2/commands/config/gen/ConfigGenerationUtils.h"
#include "fboss/cli/fboss2/commands/config/gen/PlatformConfigPathUtils.h"
#include "fboss/cli/fboss2/utils/CLIParserUtils.h"
#include "fboss/configs/platforms/generic/forwarding_stack/utils/FeatureDefaultCommandArgsUtils.h" // @manual=//fboss/configs/platforms/generic/forwarding_stack/utils:config_generation_utils
#include "fboss/lib/config/agent/InterfaceConfigUtils.h"
#include "fboss/lib/config/agent/PortConfigUtils.h"
#include "fboss/lib/config/agent/VlanConfigUtils.h"

namespace facebook::fboss::configgen {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kPlatform = "test_platform";
constexpr std::string_view kProfile = "hw_test";
constexpr std::string_view kBenchmarkProfile = "benchmark";
constexpr std::string_view kAsicYaml = "ASIC_CONFIG: test\n";
constexpr std::string_view kAsicJson = "{\"ASIC_CONFIG\":\"test\"}\n";
constexpr std::string_view kKeyValueConfig =
    "{\"foo\":\"bar\",\"answer\":\"42\"}\n";
constexpr std::string_view kPortName = "eth1/1/1";
constexpr std::string_view kManagementPortName = "management0";
constexpr std::string_view kSecondManagementPortName = "management1";
constexpr int32_t kManagementPortId = 100;
constexpr int32_t kSecondManagementPortId = 101;
constexpr auto kPortProfile = cfg::PortProfileID::PROFILE_100G_4_NRZ_NOFEC;
constexpr auto kWidePortProfile =
    cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N;

cfg::Vlan makeLoopbackVlan() {
  auto vlan = utility::createVlanConfig(VlanID(utility::kFbossLoopbackVlanId));
  vlan.name() = "fbossLoopback0";
  return vlan;
}

cfg::Vlan makeDefaultVlan() {
  auto vlan = utility::createVlanConfig(VlanID(utility::kDefaultVlanId4094));
  vlan.name() = "default";
  vlan.routable() = false;
  return vlan;
}

cfg::Interface makeLoopbackInterface() {
  auto intf = utility::createVlanInterfaceConfig(
      InterfaceID(utility::kFbossLoopbackVlanId),
      VlanID(utility::kFbossLoopbackVlanId));
  intf.name().reset();
  intf.isVirtual() = true;
  intf.isStateSyncDisabled() = true;
  return intf;
}

void writeTestFile(const fs::path& path, std::string_view contents) {
  fs::create_directories(path.parent_path());
  if (!folly::writeFile(contents, path.c_str())) {
    throw std::runtime_error("Unable to write test file " + path.string());
  }
}

void writePortAssignments(
    const fs::path& path,
    int32_t portId,
    std::string_view portName,
    cfg::PortType portType = cfg::PortType::INTERFACE_PORT) {
  writeTestFile(
      path,
      "{\"portIdToPortAssignment\":{\"" + std::to_string(portId) +
          "\":{\"portName\":\"" + std::string(portName) + "\",\"portType\":" +
          std::to_string(static_cast<int>(portType)) + ",\"scope\":0}}}\n");
}

void writePlatformDescriptor(
    const fs::path& path,
    int16_t numSwitchAsics,
    std::string_view platform = kPlatform) {
  PlatformDescriptor descriptor;
  descriptor.platformType() = PlatformType::PLATFORM_WEDGE800BACT;
  descriptor.productNamePrefixes() = {"TestPlatform"};
  descriptor.modeNames() = {std::string(platform)};
  descriptor.asicType() = cfg::AsicType::ASIC_TYPE_TOMAHAWK5;
  descriptor.numSwitchAsics() = numSwitchAsics;
  writeTestFile(
      path,
      apache::thrift::SimpleJSONSerializer::serialize<std::string>(descriptor));
}

void writeRawPlatformMapping(const fs::path& path, std::string_view portName) {
  cfg::PlatformPortEntry port;
  port.mapping()->id() = 0;
  port.mapping()->name() = portName;
  port.mapping()->controllingPort() = 0;
  port.mapping()->pins() = {};
  port.mapping()->controllingPortName() = portName;
  port.supportedProfiles()[kPortProfile] = cfg::PlatformPortConfig{};

  cfg::PlatformPortProfileConfigEntry profile;
  profile.factor()->profileID() = kPortProfile;
  profile.profile()->speed() = cfg::PortSpeed::HUNDREDG;

  cfg::PlatformMapping mapping;
  mapping.ports() = {};
  mapping.chips() = {};
  mapping.platformSupportedProfiles() = {std::move(profile)};
  mapping.rawPlatformPorts() = {};
  (*mapping.rawPlatformPorts())[std::string(portName)] = std::move(port);
  writeTestFile(
      path,
      apache::thrift::SimpleJSONSerializer::serialize<std::string>(mapping));
}

void writeTwoPortGroupMapping(const fs::path& mappingDirectory) {
  writeTestFile(
      mappingDirectory / "port_id_to_port_assignment.json",
      R"({"portIdToPortAssignment":{"1":{"portName":"eth1/1/1","portType":0,"scope":0},"2":{"portName":"eth1/1/2","portType":0,"scope":0}}})");

  cfg::PlatformPortEntry controllingPort;
  controllingPort.mapping()->id() = 0;
  controllingPort.mapping()->name() = kPortName;
  controllingPort.mapping()->controllingPort() = 0;
  controllingPort.mapping()->pins() = {};
  controllingPort.mapping()->controllingPortName() = kPortName;
  controllingPort.supportedProfiles()[kPortProfile] = cfg::PlatformPortConfig{};
  controllingPort.supportedProfiles()[kWidePortProfile].subsumedPortNames() = {
      "eth1/1/2"};

  cfg::PlatformPortEntry subsidiaryPort;
  subsidiaryPort.mapping()->id() = 0;
  subsidiaryPort.mapping()->name() = "eth1/1/2";
  subsidiaryPort.mapping()->controllingPort() = 0;
  subsidiaryPort.mapping()->pins() = {};
  subsidiaryPort.mapping()->controllingPortName() = kPortName;
  subsidiaryPort.supportedProfiles()[kPortProfile] = cfg::PlatformPortConfig{};

  cfg::PlatformPortProfileConfigEntry narrowProfile;
  narrowProfile.factor()->profileID() = kPortProfile;
  narrowProfile.profile()->speed() = cfg::PortSpeed::HUNDREDG;
  cfg::PlatformPortProfileConfigEntry wideProfile;
  wideProfile.factor()->profileID() = kWidePortProfile;
  wideProfile.profile()->speed() = cfg::PortSpeed::FOURHUNDREDG;

  cfg::PlatformMapping mapping;
  mapping.ports() = {};
  mapping.chips() = {};
  mapping.platformSupportedProfiles() = {
      std::move(narrowProfile), std::move(wideProfile)};
  mapping.rawPlatformPorts() = {};
  (*mapping.rawPlatformPorts())[std::string(kPortName)] =
      std::move(controllingPort);
  (*mapping.rawPlatformPorts())["eth1/1/2"] = std::move(subsidiaryPort);
  writeTestFile(
      mappingDirectory / "raw_platform_mapping.json",
      apache::thrift::SimpleJSONSerializer::serialize<std::string>(mapping));
}

void writeInterfaceAndManagementPortMapping(const fs::path& mappingDirectory) {
  writeTestFile(
      mappingDirectory / "port_id_to_port_assignment.json",
      "{\"portIdToPortAssignment\":{\"1\":{\"portName\":\"" +
          std::string(kPortName) + "\",\"portType\":" +
          std::to_string(static_cast<int>(cfg::PortType::INTERFACE_PORT)) +
          ",\"scope\":0},\"" + std::to_string(kManagementPortId) +
          "\":{\"portName\":\"" + std::string(kManagementPortName) +
          "\",\"portType\":" +
          std::to_string(static_cast<int>(cfg::PortType::MANAGEMENT_PORT)) +
          ",\"scope\":0},\"" + std::to_string(kSecondManagementPortId) +
          "\":{\"portName\":\"" + std::string(kSecondManagementPortName) +
          "\",\"portType\":" +
          std::to_string(static_cast<int>(cfg::PortType::MANAGEMENT_PORT)) +
          ",\"scope\":0}}}\n");

  cfg::PlatformPortEntry interfacePort;
  interfacePort.mapping()->id() = 0;
  interfacePort.mapping()->name() = kPortName;
  interfacePort.mapping()->controllingPort() = 0;
  interfacePort.mapping()->pins() = {};
  interfacePort.mapping()->controllingPortName() = kPortName;
  interfacePort.supportedProfiles()[kPortProfile] = cfg::PlatformPortConfig{};

  cfg::PlatformPortEntry managementPort;
  managementPort.mapping()->id() = 0;
  managementPort.mapping()->name() = kManagementPortName;
  managementPort.mapping()->controllingPort() = 0;
  managementPort.mapping()->pins() = {};
  managementPort.mapping()->controllingPortName() = kManagementPortName;
  managementPort.supportedProfiles()[kPortProfile] = cfg::PlatformPortConfig{};

  cfg::PlatformPortEntry secondManagementPort;
  secondManagementPort.mapping()->id() = 0;
  secondManagementPort.mapping()->name() = kSecondManagementPortName;
  secondManagementPort.mapping()->controllingPort() = 0;
  secondManagementPort.mapping()->pins() = {};
  secondManagementPort.mapping()->controllingPortName() =
      kSecondManagementPortName;
  secondManagementPort.supportedProfiles()[kPortProfile] =
      cfg::PlatformPortConfig{};

  cfg::PlatformPortProfileConfigEntry profile;
  profile.factor()->profileID() = kPortProfile;
  profile.profile()->speed() = cfg::PortSpeed::HUNDREDG;

  cfg::PlatformMapping mapping;
  mapping.ports() = {};
  mapping.chips() = {};
  mapping.platformSupportedProfiles() = {std::move(profile)};
  mapping.rawPlatformPorts() = {};
  (*mapping.rawPlatformPorts())[std::string(kPortName)] =
      std::move(interfacePort);
  (*mapping.rawPlatformPorts())[std::string(kManagementPortName)] =
      std::move(managementPort);
  (*mapping.rawPlatformPorts())[std::string(kSecondManagementPortName)] =
      std::move(secondManagementPort);
  writeTestFile(
      mappingDirectory / "raw_platform_mapping.json",
      apache::thrift::SimpleJSONSerializer::serialize<std::string>(mapping));
}

FeatureDefaultCommandArgs makeAutoFeature(
    std::map<std::string, std::string> args,
    std::set<std::string> profiles = {},
    std::set<std::string> asicTypes = {},
    std::set<std::string> excludedAsicTypes = {},
    std::set<std::string> platforms = {},
    std::set<std::string> excludedPlatforms = {}) {
  FeatureEnableConditions conditions;
  if (!profiles.empty()) {
    FeatureConditionValues values;
    values.included() = std::move(profiles);
    conditions.configProfiles() = std::move(values);
  }
  if (!asicTypes.empty() || !excludedAsicTypes.empty()) {
    FeatureConditionValues values;
    values.included() = std::move(asicTypes);
    values.excluded() = std::move(excludedAsicTypes);
    conditions.asicTypes() = std::move(values);
  }
  if (!platforms.empty() || !excludedPlatforms.empty()) {
    FeatureConditionValues values;
    values.included() = std::move(platforms);
    values.excluded() = std::move(excludedPlatforms);
    conditions.platforms() = std::move(values);
  }

  FeatureDefaultCommandArgs feature;
  feature.args() = std::move(args);
  feature.autoEnableWhen() =
      std::vector<FeatureEnableConditions>{std::move(conditions)};
  return feature;
}

void writeFeatureDefaultCommandArgsConfig(const fs::path& fbossRoot) {
  FeatureDefaultCommandArgsConfig config;
  config.profileDefaultArgs()[std::string(kProfile)] = {
      {"check_wb_handles", "true"},
      {"counter_refresh_interval", "0"},
      {"enable_nexthop_id_manager", "true"},
      {"intf_nbr_tables", "true"},
      {"log_variable_name", "true"},
      {"resolve_nexthops_from_id", "true"},
      {"skip_transceiver_programming", "true"},
      {"use_full_dlb_scale", "true"},
      {"verify_fib_nexthop_id_consistency", "true"},
  };
  config.profileDefaultArgs()[std::string(kBenchmarkProfile)] = {
      {"check_wb_handles", "true"},
      {"counter_refresh_interval", "1"},
      {"enable_nexthop_id_manager", "true"},
      {"enable_route_resource_protection", "false"},
      {"log_variable_name", "true"},
      {"resolve_nexthops_from_id", "true"},
      {"skip_transceiver_programming", "true"},
      {"use_full_dlb_scale", "true"},
  };
  const std::set<std::string> testProfiles{
      std::string(kProfile), std::string(kBenchmarkProfile)};
  config.features()["sai_configure_six_tap"] = makeAutoFeature(
      {{"sai_configure_six_tap", "true"}},
      testProfiles,
      {"ASIC_TYPE_TOMAHAWK5"});
  config.features()["enable_acl_table_group"] = makeAutoFeature(
      {{"enable_acl_table_group", "true"}},
      testProfiles,
      {},
      {"ASIC_TYPE_EBRO", "ASIC_TYPE_RAMON3"});
  config.features()["platform_descriptor_registry"] = makeAutoFeature(
      {{"platform_descriptor_config_path", "/tmp/platform_descriptors/"}},
      testProfiles,
      {},
      {},
      {"PLATFORM_WEDGE800BACT"});
  config.features()["use_raw_platform_mapping"] = makeAutoFeature(
      {{"use_raw_platform_mapping", "true"}},
      testProfiles,
      {},
      {},
      {"PLATFORM_WEDGE800BACT"});

  writeTestFile(
      fbossRoot / "configs" / "platforms" / "generic" / "forwarding_stack" /
          "agent" / "feature_default_command_args_config.json",
      apache::thrift::SimpleJSONSerializer::serialize<std::string>(config));
}

std::map<int32_t, cfg::PortAssignment> makePortAssignments(
    int32_t portId,
    std::string_view portName) {
  cfg::PortAssignment assignment;
  assignment.portName() = portName;
  assignment.portType() = cfg::PortType::INTERFACE_PORT;
  assignment.scope() = cfg::Scope::LOCAL;
  return {{portId, std::move(assignment)}};
}

fs::path createTestPlatform(
    const fs::path& fbossRoot,
    std::string_view vendor,
    std::string_view platform = kPlatform,
    std::string_view configType = "YAML_CONFIG",
    std::string_view extension = ".yml",
    std::string_view generatedConfig = kAsicYaml) {
  writeFeatureDefaultCommandArgsConfig(fbossRoot);
  const auto asicConfigDirectory =
      fbossRoot / "configs" / "platforms" / vendor / platform / "asic_config";
  writeTestFile(
      asicConfigDirectory / "asic_config.json",
      "{\"platform_name\":\"" + std::string(platform) +
          "\",\"defaults\":{\"asic_config_params\":{\"config_type\":\"" +
          std::string(configType) +
          "\"}},\"variants\":{\"default\":{},\"hw_test\":{}}}\n");
  writeTestFile(
      asicConfigDirectory / "generated" / ("default" + std::string(extension)),
      generatedConfig);
  writeTestFile(
      asicConfigDirectory / "generated" / ("hw_test" + std::string(extension)),
      generatedConfig);
  const auto generatedMappingDirectory = fbossRoot / "lib" /
      "platform_mapping_v2" / "generated_platform_mappings" / vendor / platform;
  writePortAssignments(
      generatedMappingDirectory / "port_id_to_port_assignment.json",
      1,
      kPortName);
  writeRawPlatformMapping(
      generatedMappingDirectory / "raw_platform_mapping.json", kPortName);
  writePlatformDescriptor(
      generatedMappingDirectory / "platform_descriptor.json", 1, platform);
  return asicConfigDirectory.parent_path();
}

void writeConfigGenerationTarget(
    const fs::path& platformDirectory,
    std::string_view contents) {
  writeTestFile(
      platformDirectory / "forwarding_stack" / "config_generation.json",
      contents);
}

std::string readFile(const fs::path& path) {
  std::string contents;
  if (!folly::readFile(path.c_str(), contents)) {
    throw std::runtime_error("Unable to read test file " + path.string());
  }
  return contents;
}

std::string serializeAgentConfig(
    std::string_view yamlConfig,
    bool includeSdkVersion = false,
    std::optional<std::string_view> testArgument = std::nullopt,
    std::optional<int16_t> switchIndex = std::nullopt) {
  cfg::AsicConfigEntry common;
  common.set_yamlConfig(std::string(yamlConfig));
  cfg::AsicConfig asicConfig;
  asicConfig.common() = std::move(common);
  cfg::ChipConfig chipConfig;
  chipConfig.set_asicConfig(std::move(asicConfig));

  cfg::AgentConfig config;
  config.platform()->chip() = std::move(chipConfig);
  if (testArgument) {
    config.defaultCommandLineArgs()["test_argument"] = *testArgument;
  }
  if (includeSdkVersion) {
    cfg::SdkVersion sdkVersion;
    sdkVersion.asicSdk() = "sdk";
    sdkVersion.saiSdk() = "sai";
    config.sw()->sdkVersion() = std::move(sdkVersion);
  }
  if (switchIndex) {
    cfg::SwitchInfo switchInfo;
    switchInfo.switchIndex() = *switchIndex;
    config.sw()->switchSettings()->switchIdToSwitchInfo()[0] =
        std::move(switchInfo);
  }
  return apache::thrift::SimpleJSONSerializer::serialize<std::string>(config);
}

std::map<std::string, std::string> expectedHwTestCommandLineArgs() {
  return {
      {"check_wb_handles", "true"},
      {"counter_refresh_interval", "0"},
      {"enable_acl_table_group", "true"},
      {"enable_nexthop_id_manager", "true"},
      {"intf_nbr_tables", "true"},
      {"log_variable_name", "true"},
      {"platform_descriptor_config_path", "/tmp/platform_descriptors/"},
      {"resolve_nexthops_from_id", "true"},
      {"sai_configure_six_tap", "true"},
      {"skip_transceiver_programming", "true"},
      {"use_full_dlb_scale", "true"},
      {"use_raw_platform_mapping", "true"},
      {"verify_fib_nexthop_id_consistency", "true"},
  };
}

std::map<std::string, std::string> expectedBenchmarkCommandLineArgs() {
  return {
      {"check_wb_handles", "true"},
      {"counter_refresh_interval", "1"},
      {"enable_acl_table_group", "true"},
      {"enable_nexthop_id_manager", "true"},
      {"enable_route_resource_protection", "false"},
      {"log_variable_name", "true"},
      {"platform_descriptor_config_path", "/tmp/platform_descriptors/"},
      {"resolve_nexthops_from_id", "true"},
      {"sai_configure_six_tap", "true"},
      {"skip_transceiver_programming", "true"},
      {"use_full_dlb_scale", "true"},
      {"use_raw_platform_mapping", "true"},
  };
}

TEST(PlatformConfigPathUtilsTest, FindsPlatformAndComponentDirectories) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  const auto expectedPlatformDirectory =
      createTestPlatform(fbossRoot, "test_vendor");
  fs::create_directories(expectedPlatformDirectory / "platform_mapping");

  const auto platformDirectory =
      findPlatformConfigDirectory(fbossRoot, kPlatform);

  EXPECT_EQ(platformDirectory.systemVendor, "test_vendor");
  EXPECT_EQ(platformDirectory.path, expectedPlatformDirectory);
  EXPECT_EQ(
      findPlatformConfigComponentDirectory(
          platformDirectory, "platform_mapping"),
      expectedPlatformDirectory / "platform_mapping");
}

TEST(PlatformConfigPathUtilsTest, RejectsDuplicatePlatformNames) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  createTestPlatform(fbossRoot, "first_vendor");
  createTestPlatform(fbossRoot, "second_vendor");

  EXPECT_THROW(findPlatformConfigDirectory(fbossRoot, kPlatform), FbossError);
}

TEST(PlatformConfigPathUtilsTest, DoesNotTreatGenericDataAsAPlatform) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  fs::create_directories(
      fbossRoot / "configs" / "platforms" / "generic" / "forwarding_stack");

  EXPECT_THROW(
      findPlatformConfigDirectory(fbossRoot, "forwarding_stack"), FbossError);
}

TEST(FeatureDefaultCommandArgsTest, ResolvesMatchingAutomaticFeatures) {
  FeatureDefaultCommandArgsConfig config;
  config.profileDefaultArgs()["hw_test"] = {{"base", "true"}};
  config.features()["matching"] = makeAutoFeature(
      {{"matching", "true"}},
      {"hw_test"},
      {"ASIC_TYPE_TOMAHAWK5"},
      {},
      {"PLATFORM_WEDGE800BACT"});
  config.features()["excluded_platform"] = makeAutoFeature(
      {{"excluded_platform", "true"}},
      {"hw_test"},
      {},
      {},
      {},
      {"PLATFORM_WEDGE800BACT"});
  config.features()["excluded"] = makeAutoFeature(
      {{"excluded", "true"}}, {"hw_test"}, {}, {"ASIC_TYPE_TOMAHAWK5"});
  config.features()["other_profile"] =
      makeAutoFeature({{"other_profile", "true"}}, {"default"});
  FeatureDefaultCommandArgs explicitOnly;
  explicitOnly.args() = {{"explicit_only", "true"}};
  config.features()["explicit_only"] = std::move(explicitOnly);

  const std::map<std::string, std::string> expected{
      {"base", "true"}, {"matching", "true"}};
  EXPECT_EQ(
      resolveFeatureDefaultCommandArgs(
          config,
          "hw_test",
          cfg::AsicType::ASIC_TYPE_TOMAHAWK5,
          PlatformType::PLATFORM_WEDGE800BACT),
      expected);
}

TEST(FeatureDefaultCommandArgsTest, SkipsAsicConditionsWithoutAsicType) {
  FeatureDefaultCommandArgsConfig config;
  config.profileDefaultArgs()["hw_test"] = {{"base", "true"}};
  config.features()["platform_feature"] = makeAutoFeature(
      {{"platform_feature", "true"}},
      {"hw_test"},
      {},
      {},
      {"PLATFORM_WEDGE800BACT"});
  config.features()["asic_feature"] = makeAutoFeature(
      {{"asic_feature", "true"}}, {"hw_test"}, {"ASIC_TYPE_TOMAHAWK5"});

  const std::map<std::string, std::string> expected{
      {"base", "true"}, {"platform_feature", "true"}};
  EXPECT_EQ(
      resolveFeatureDefaultCommandArgs(
          config, "hw_test", std::nullopt, PlatformType::PLATFORM_WEDGE800BACT),
      expected);
}

TEST(FeatureDefaultCommandArgsTest, RejectsEmptyAutomaticConditions) {
  FeatureDefaultCommandArgsConfig config;
  FeatureDefaultCommandArgs feature;
  feature.args() = {{"unexpected", "true"}};
  feature.autoEnableWhen() =
      std::vector<FeatureEnableConditions>{FeatureEnableConditions{}};
  config.features()["empty_conditions"] = std::move(feature);

  EXPECT_THROW(
      resolveFeatureDefaultCommandArgs(
          config, "hw_test", std::nullopt, PlatformType::PLATFORM_WEDGE800BACT),
      FbossError);
}

TEST(FeatureDefaultCommandArgsTest, RejectsConflictingArguments) {
  FeatureDefaultCommandArgsConfig config;
  config.features()["first"] =
      makeAutoFeature({{"conflict", "first"}}, {"hw_test"});
  config.features()["second"] =
      makeAutoFeature({{"conflict", "second"}}, {"hw_test"});

  EXPECT_THROW(
      resolveFeatureDefaultCommandArgs(
          config,
          "hw_test",
          cfg::AsicType::ASIC_TYPE_TOMAHAWK5,
          PlatformType::PLATFORM_WEDGE800BACT),
      FbossError);
}

TEST(AgentConfigGenTest, AssemblesEmptyAgentConfig) {
  auto config = assembleAgentConfig({}, {}, {});

  EXPECT_TRUE(config.defaultCommandLineArgs()->empty());
  EXPECT_EQ(*config.sw(), cfg::SwitchConfig());
  EXPECT_EQ(*config.platform(), cfg::PlatformConfig());
  EXPECT_TRUE(config.thriftApiToRateLimitInQps()->empty());
}

TEST(AgentConfigGenTest, AssemblesPlatformConfig) {
  cfg::AsicConfigEntry common;
  common.set_yamlConfig("asic config");
  cfg::AsicConfig asicConfig;
  asicConfig.common() = std::move(common);
  cfg::ChipConfig chipConfig;
  chipConfig.set_asicConfig(std::move(asicConfig));

  cfg::PortAssignment assignment;
  assignment.portName() = "eth1/1/1";
  assignment.portType() = cfg::PortType::INTERFACE_PORT;
  assignment.scope() = cfg::Scope::LOCAL;
  std::map<int32_t, cfg::PortAssignment> assignments{
      {1, std::move(assignment)}};

  const auto platformConfig = assemblePlatformConfig(chipConfig, assignments);

  EXPECT_EQ(*platformConfig.chip(), chipConfig);
  EXPECT_EQ(*platformConfig.portIdToPortAssignment(), assignments);
}

TEST(AgentConfigGenTest, SelectsGeneratedPlatformArtifacts) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  const auto platformDirectory = createTestPlatform(fbossRoot, "test_vendor");

  EXPECT_EQ(
      findGeneratedAsicConfig(fbossRoot, kPlatform, kProfile),
      platformDirectory / "asic_config" / "generated" / "hw_test.yml");
  EXPECT_EQ(
      findGeneratedAsicConfig(fbossRoot, kPlatform, ""),
      platformDirectory / "asic_config" / "generated" / "default.yml");
  EXPECT_EQ(
      findGeneratedAsicConfig(fbossRoot, kPlatform, "default"),
      platformDirectory / "asic_config" / "generated" / "default.yml");
  EXPECT_EQ(
      findPortIdToPortAssignmentConfig(fbossRoot, kPlatform),
      fbossRoot / "lib" / "platform_mapping_v2" /
          "generated_platform_mappings" / "test_vendor" / kPlatform /
          "port_id_to_port_assignment.json");
  const auto [descriptorPath, descriptor] =
      findPlatformDescriptorConfigWithDescriptor(fbossRoot, kPlatform);
  EXPECT_EQ(
      descriptorPath,
      fbossRoot / "lib" / "platform_mapping_v2" /
          "generated_platform_mappings" / "test_vendor" / kPlatform /
          "platform_descriptor.json");
  EXPECT_EQ(*descriptor.asicType(), cfg::AsicType::ASIC_TYPE_TOMAHAWK5);
}

TEST(AgentConfigGenTest, GeneratesSingleNpuSwitchSettings) {
  PlatformDescriptor descriptor;
  descriptor.asicType() = cfg::AsicType::ASIC_TYPE_TOMAHAWK5;
  descriptor.numSwitchAsics() = 1;

  const auto switchSettings = generateSwitchSettings(descriptor);

  EXPECT_EQ(*switchSettings.switchType(), cfg::SwitchType::NPU);
  EXPECT_TRUE(*switchSettings.needL2EntryForNeighbor());
  ASSERT_EQ(switchSettings.switchIdToSwitchInfo()->size(), 1);
  const auto& switchInfo = switchSettings.switchIdToSwitchInfo()->at(0);
  EXPECT_EQ(*switchInfo.switchType(), cfg::SwitchType::NPU);
  EXPECT_EQ(*switchInfo.asicType(), cfg::AsicType::ASIC_TYPE_TOMAHAWK5);
  EXPECT_EQ(*switchInfo.switchIndex(), 0);
  EXPECT_EQ(
      *switchInfo.portIdRange()->minimum(),
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN());
  EXPECT_EQ(
      *switchInfo.portIdRange()->maximum(),
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX());
}

TEST(AgentConfigGenTest, GeneratesDefaultAclTableGroup) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  createTestPlatform(fbossRoot, "test_vendor");

  const auto inputs = resolveAgentConfigInputs(fbossRoot, kPlatform, kProfile);
  const auto switchConfig = generateSwitchConfig(inputs);

  cfg::AclTable table;
  table.name() = cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE();
  table.priority() = 0;
  table.aclEntries() = {};
  table.actionTypes() = {};
  table.qualifiers() = {};
  table.udfGroups() = {};
  cfg::AclTableGroup group;
  group.name() = "acl-table-group-ingress";
  group.aclTables() = {table};
  group.stage() = cfg::AclStage::INGRESS;
  const std::vector<cfg::AclTableGroup> expected{group};

  EXPECT_EQ(*switchConfig.aclTableGroups(), expected);
  EXPECT_FALSE(switchConfig.aclTableGroup());
  EXPECT_TRUE(switchConfig.ports()->empty());
  EXPECT_TRUE(switchConfig.vlans()->empty());
  EXPECT_TRUE(switchConfig.vlanPorts()->empty());
  EXPECT_TRUE(switchConfig.interfaces()->empty());
}

TEST(AgentConfigGenTest, UsesSaiUserDefinedTrapCommandLineArgumentForCopp) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  createTestPlatform(fbossRoot, "test_vendor");
  const auto inputs = resolveAgentConfigInputs(fbossRoot, kPlatform, "default");

  const auto enabledConfig =
      generateSwitchConfig(inputs, {{"sai_user_defined_trap", "true"}});
  const auto disabledConfig =
      generateSwitchConfig(inputs, {{"sai_user_defined_trap", "false"}});
  const auto unsetConfig = generateSwitchConfig(inputs);

  const auto& enabledAction = *enabledConfig.cpuTrafficPolicy()
                                   ->trafficPolicy()
                                   ->matchToAction()
                                   ->at(2)
                                   .action();
  EXPECT_FALSE(enabledAction.sendToQueue().has_value());
  EXPECT_TRUE(enabledAction.setTc().has_value());
  EXPECT_TRUE(enabledAction.userDefinedTrap().has_value());
  EXPECT_EQ(*enabledAction.toCpuAction(), cfg::ToCpuAction::COPY);

  const auto& disabledAction = *disabledConfig.cpuTrafficPolicy()
                                    ->trafficPolicy()
                                    ->matchToAction()
                                    ->at(2)
                                    .action();
  ASSERT_TRUE(disabledAction.sendToQueue().has_value());
  EXPECT_FALSE(disabledAction.setTc().has_value());
  EXPECT_FALSE(disabledAction.userDefinedTrap().has_value());
  EXPECT_EQ(*disabledAction.toCpuAction(), cfg::ToCpuAction::COPY);

  EXPECT_EQ(
      *unsetConfig.cpuTrafficPolicy()
           ->trafficPolicy()
           ->matchToAction()
           ->at(2)
           .action(),
      disabledAction);
}

TEST(AgentConfigGenTest, GeneratesDefaultProfilePortGraph) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  createTestPlatform(fbossRoot, "test_vendor");
  writeTwoPortGroupMapping(
      fbossRoot / "lib" / "platform_mapping_v2" /
      "generated_platform_mappings" / "test_vendor" / kPlatform);
  const auto inputs = resolveAgentConfigInputs(fbossRoot, kPlatform, "default");

  const auto switchConfig = generateSwitchConfig(inputs);

  auto expectedPort = utility::createInterfacePortConfig(
      *inputs.platformMapping,
      PortID(1),
      kWidePortProfile,
      VlanID(utility::kInterfaceVlanIdMin));
  expectedPort.state() = cfg::PortState::ENABLED;
  expectedPort.loopbackMode() = cfg::PortLoopbackMode::NONE;
  expectedPort.maxFrameSize() =
      cfg::switch_config_constants::DEFAULT_PORT_MTU();
  const std::vector<cfg::Port> expectedPorts{expectedPort};
  EXPECT_EQ(*switchConfig.ports(), expectedPorts);

  const std::vector<cfg::VlanPort> expectedVlanPorts{
      utility::createVlanPortConfig(
          PortID(1), VlanID(utility::kInterfaceVlanIdMin))};
  EXPECT_EQ(*switchConfig.vlanPorts(), expectedVlanPorts);

  const std::vector<cfg::Interface> expectedInterfaces{
      utility::createVlanInterfaceConfig(
          InterfaceID(utility::kInterfaceVlanIdMin),
          VlanID(utility::kInterfaceVlanIdMin)),
      makeLoopbackInterface()};
  EXPECT_EQ(*switchConfig.interfaces(), expectedInterfaces);

  const std::vector<cfg::Vlan> expectedVlans{
      makeLoopbackVlan(),
      makeDefaultVlan(),
      utility::createVlanConfig(VlanID(utility::kInterfaceVlanIdMin)),
  };
  EXPECT_EQ(*switchConfig.vlans(), expectedVlans);
  EXPECT_EQ(*switchConfig.defaultVlan(), utility::kDefaultVlanId4094);
}

TEST(AgentConfigGenTest, AllocatesManagementPortsDownward) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  createTestPlatform(fbossRoot, "test_vendor");
  writeInterfaceAndManagementPortMapping(
      fbossRoot / "lib" / "platform_mapping_v2" /
      "generated_platform_mappings" / "test_vendor" / kPlatform);
  const auto inputs = resolveAgentConfigInputs(fbossRoot, kPlatform, "default");

  const auto switchConfig = generateSwitchConfig(inputs);
  constexpr int32_t kExpectedFirstManagementVlan = 2252;
  constexpr int32_t kExpectedSecondManagementVlan = 2251;

  auto expectedInterfacePort = utility::createInterfacePortConfig(
      *inputs.platformMapping,
      PortID(1),
      kPortProfile,
      VlanID(utility::kInterfaceVlanIdMin));
  expectedInterfacePort.state() = cfg::PortState::ENABLED;
  expectedInterfacePort.loopbackMode() = cfg::PortLoopbackMode::NONE;
  expectedInterfacePort.maxFrameSize() =
      cfg::switch_config_constants::DEFAULT_PORT_MTU();
  auto expectedManagementPort = utility::createRoutedPortConfig(
      *inputs.platformMapping,
      PortID(kManagementPortId),
      kPortProfile,
      VlanID(kExpectedFirstManagementVlan));
  expectedManagementPort.state() = cfg::PortState::ENABLED;
  expectedManagementPort.loopbackMode() = cfg::PortLoopbackMode::NONE;
  expectedManagementPort.maxFrameSize() =
      cfg::switch_config_constants::DEFAULT_PORT_MTU();
  auto expectedSecondManagementPort = utility::createRoutedPortConfig(
      *inputs.platformMapping,
      PortID(kSecondManagementPortId),
      kPortProfile,
      VlanID(kExpectedSecondManagementVlan));
  expectedSecondManagementPort.state() = cfg::PortState::ENABLED;
  expectedSecondManagementPort.loopbackMode() = cfg::PortLoopbackMode::NONE;
  expectedSecondManagementPort.maxFrameSize() =
      cfg::switch_config_constants::DEFAULT_PORT_MTU();
  const std::vector<cfg::Port> expectedPorts{
      expectedInterfacePort,
      expectedManagementPort,
      expectedSecondManagementPort};
  EXPECT_EQ(*switchConfig.ports(), expectedPorts);

  const std::vector<cfg::VlanPort> expectedVlanPorts{
      utility::createVlanPortConfig(
          PortID(1), VlanID(utility::kInterfaceVlanIdMin)),
      utility::createVlanPortConfig(
          PortID(kManagementPortId), VlanID(kExpectedFirstManagementVlan)),
      utility::createVlanPortConfig(
          PortID(kSecondManagementPortId),
          VlanID(kExpectedSecondManagementVlan))};
  EXPECT_EQ(*switchConfig.vlanPorts(), expectedVlanPorts);

  auto expectedManagementInterface = utility::createVlanInterfaceConfig(
      InterfaceID(kExpectedFirstManagementVlan),
      VlanID(kExpectedFirstManagementVlan));
  expectedManagementInterface.isVirtual() = true;
  expectedManagementInterface.isStateSyncDisabled() = true;
  auto expectedSecondManagementInterface = utility::createVlanInterfaceConfig(
      InterfaceID(kExpectedSecondManagementVlan),
      VlanID(kExpectedSecondManagementVlan));
  expectedSecondManagementInterface.isVirtual() = true;
  expectedSecondManagementInterface.isStateSyncDisabled() = true;
  const std::vector<cfg::Interface> expectedInterfaces{
      utility::createVlanInterfaceConfig(
          InterfaceID(utility::kInterfaceVlanIdMin),
          VlanID(utility::kInterfaceVlanIdMin)),
      expectedManagementInterface,
      expectedSecondManagementInterface,
      makeLoopbackInterface()};
  EXPECT_EQ(*switchConfig.interfaces(), expectedInterfaces);

  const std::vector<cfg::Vlan> expectedVlans{
      makeLoopbackVlan(),
      makeDefaultVlan(),
      utility::createVlanConfig(VlanID(utility::kInterfaceVlanIdMin)),
      utility::createVlanConfig(VlanID(kExpectedFirstManagementVlan)),
      utility::createVlanConfig(VlanID(kExpectedSecondManagementVlan)),
  };
  EXPECT_EQ(*switchConfig.vlans(), expectedVlans);
}

TEST(AgentConfigGenTest, SkipsNonInterfacePortForDefaultProfile) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  createTestPlatform(fbossRoot, "test_vendor");
  const auto assignmentPath = fbossRoot / "lib" / "platform_mapping_v2" /
      "generated_platform_mappings" / "test_vendor" / kPlatform /
      "port_id_to_port_assignment.json";
  writePortAssignments(
      assignmentPath, 1, kPortName, cfg::PortType::FABRIC_PORT);
  const auto inputs = resolveAgentConfigInputs(fbossRoot, kPlatform, "default");

  const auto switchConfig = generateSwitchConfig(inputs);

  EXPECT_TRUE(switchConfig.ports()->empty());
  EXPECT_TRUE(switchConfig.vlanPorts()->empty());
  const std::vector<cfg::Interface> expectedInterfaces{makeLoopbackInterface()};
  EXPECT_EQ(*switchConfig.interfaces(), expectedInterfaces);
  const std::vector<cfg::Vlan> expectedVlans{
      makeLoopbackVlan(),
      makeDefaultVlan(),
  };
  EXPECT_EQ(*switchConfig.vlans(), expectedVlans);
}

TEST(AgentConfigGenTest, ResolvesVariantDescriptorAndRejectsMultiAsicPlatform) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  createTestPlatform(fbossRoot, "test_vendor");
  const auto generatedMappingDirectory = fbossRoot / "lib" /
      "platform_mapping_v2" / "generated_platform_mappings" / "test_vendor";
  fs::remove(
      generatedMappingDirectory / kPlatform / "platform_descriptor.json");
  const auto variantDescriptor = generatedMappingDirectory /
      "test_platform_variant" / "platform_descriptor.json";
  writePlatformDescriptor(variantDescriptor, 2);
  writePortAssignments(
      variantDescriptor.parent_path() / "port_id_to_port_assignment.json",
      2,
      "eth1/1/2");
  writeRawPlatformMapping(
      variantDescriptor.parent_path() / "raw_platform_mapping.json",
      "eth1/1/2");

  const auto [descriptorPath, descriptor] =
      findPlatformDescriptorConfigWithDescriptor(fbossRoot, kPlatform);
  EXPECT_EQ(descriptorPath, variantDescriptor);
  EXPECT_EQ(*descriptor.numSwitchAsics(), 2);
  const auto inputs = resolveAgentConfigInputs(fbossRoot, kPlatform, kProfile);
  ASSERT_EQ(inputs.platformMapping->getPlatformPorts().size(), 1);
  EXPECT_EQ(
      *inputs.platformMapping->getPlatformPort(2).mapping()->name(),
      "eth1/1/2");
  EXPECT_THROW(generateSwitchConfig(inputs), FbossError);
}

TEST(AgentConfigGenTest, GeneratesPlatformConfig) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  createTestPlatform(fbossRoot, "test_vendor");

  const auto inputs = resolveAgentConfigInputs(fbossRoot, kPlatform, kProfile);
  const auto platformConfig = generatePlatformConfig(inputs);

  ASSERT_EQ(
      platformConfig.chip()->getType(), cfg::ChipConfig::Type::asicConfig);
  EXPECT_EQ(
      platformConfig.chip()
          ->asicConfig()
          .value()
          .common()
          ->yamlConfig()
          .value(),
      kAsicYaml);
  EXPECT_EQ(
      *platformConfig.portIdToPortAssignment(),
      makePortAssignments(1, kPortName));
  EXPECT_FALSE(platformConfig.platformSettings());
  ASSERT_EQ(inputs.platformMapping->getPlatformPorts().size(), 1);
  const auto& port = inputs.platformMapping->getPlatformPort(1);
  EXPECT_EQ(*port.mapping()->name(), kPortName);
  EXPECT_EQ(*port.mapping()->controllingPort(), 1);
  EXPECT_EQ(*port.mapping()->portType(), cfg::PortType::INTERFACE_PORT);
  EXPECT_TRUE(port.supportedProfiles()->contains(kPortProfile));
  EXPECT_FALSE(inputs.platformMapping->toThrift().rawPlatformPorts());
}

TEST(AgentConfigGenTest, RejectsEmptyProfile) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  createTestPlatform(fbossRoot, "test_vendor");

  EXPECT_THROW(resolveAgentConfigInputs(fbossRoot, kPlatform, ""), FbossError);
}

TEST(AgentConfigGenTest, UsesExplicitAsicConfigFile) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  const auto platformDirectory = createTestPlatform(fbossRoot, "test_vendor");
  const auto generatedConfig =
      platformDirectory / "asic_config" / "generated" / "hw_test.yml";
  fs::remove(generatedConfig);
  const auto overrideConfig =
      fs::path(temporaryDirectory.path().string()) / "vendor_config.yml";
  constexpr std::string_view kVendorAsicYaml = "ASIC_CONFIG: vendor\n";
  writeTestFile(overrideConfig, kVendorAsicYaml);

  const auto inputs =
      resolveAgentConfigInputs(fbossRoot, kPlatform, kProfile, overrideConfig);
  const auto platformConfig = generatePlatformConfig(inputs);

  EXPECT_EQ(
      platformConfig.chip()
          ->asicConfig()
          .value()
          .common()
          ->yamlConfig()
          .value(),
      kVendorAsicYaml);
}

TEST(AgentConfigGenTest, UsesExplicitAsicConfigTypeWithoutMetadata) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  const auto platformDirectory = createTestPlatform(fbossRoot, "test_vendor");
  fs::remove(platformDirectory / "asic_config" / "asic_config.json");
  const auto overrideConfig =
      fs::path(temporaryDirectory.path().string()) / "vendor_config.yml";
  constexpr std::string_view kVendorAsicYaml = "ASIC_CONFIG: vendor\n";
  writeTestFile(overrideConfig, kVendorAsicYaml);

  const auto inputs = resolveAgentConfigInputs(
      fbossRoot,
      kPlatform,
      kProfile,
      overrideConfig,
      cfg::AsicConfigType::YAML_CONFIG);
  const auto platformConfig = generatePlatformConfig(inputs);

  EXPECT_EQ(
      platformConfig.chip()
          ->asicConfig()
          .value()
          .common()
          ->yamlConfig()
          .value(),
      kVendorAsicYaml);
}

TEST(AgentConfigGenTest, RejectsExplicitAsicConfigTypeWithoutFile) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  createTestPlatform(fbossRoot, "test_vendor");

  EXPECT_THROW(
      resolveAgentConfigInputs(
          fbossRoot,
          kPlatform,
          kProfile,
          std::nullopt,
          cfg::AsicConfigType::YAML_CONFIG),
      FbossError);
}

TEST(AgentConfigGenTest, ParsesExplicitAsicConfigType) {
  EXPECT_EQ(
      parseAsicConfigType("key_value"), cfg::AsicConfigType::KEY_VALUE_CONFIG);
  EXPECT_EQ(parseAsicConfigType("json"), cfg::AsicConfigType::JSON_CONFIG);
  EXPECT_EQ(parseAsicConfigType("yaml"), cfg::AsicConfigType::YAML_CONFIG);
  EXPECT_THROW(parseAsicConfigType("NONE"), FbossError);
  EXPECT_THROW(parseAsicConfigType("YAML_CONFIG"), FbossError);
}

TEST(AgentConfigGenTest, RejectsMissingExplicitAsicConfigFile) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  createTestPlatform(fbossRoot, "test_vendor");
  const auto missingConfig =
      fs::path(temporaryDirectory.path().string()) / "missing.yml";

  EXPECT_THROW(
      resolveAgentConfigInputs(fbossRoot, kPlatform, kProfile, missingConfig),
      FbossError);
}

TEST(AgentConfigGenTest, PrefersColocatedPlatformMappingBundle) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  const auto platformDirectory = createTestPlatform(fbossRoot, "test_vendor");
  const auto colocatedPath = platformDirectory / "platform_mapping" /
      "generated" / "port_id_to_port_assignment.json";
  writePortAssignments(colocatedPath, 2, "eth1/1/2");
  writeRawPlatformMapping(
      colocatedPath.parent_path() / "raw_platform_mapping.json", "eth1/1/2");
  writePlatformDescriptor(
      colocatedPath.parent_path() / "platform_descriptor.json", 1);

  EXPECT_EQ(
      findPortIdToPortAssignmentConfig(fbossRoot, kPlatform), colocatedPath);
  const auto inputs = resolveAgentConfigInputs(fbossRoot, kPlatform, kProfile);
  EXPECT_EQ(
      *generatePlatformConfig(inputs).portIdToPortAssignment(),
      makePortAssignments(2, "eth1/1/2"));
  ASSERT_EQ(inputs.platformMapping->getPlatformPorts().size(), 1);
  EXPECT_EQ(
      *inputs.platformMapping->getPlatformPort(2).mapping()->name(),
      "eth1/1/2");
}

TEST(AgentConfigGenTest, ReportsMissingRawPlatformMappingPath) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  createTestPlatform(fbossRoot, "test_vendor");
  const auto rawMappingPath = fbossRoot / "lib" / "platform_mapping_v2" /
      "generated_platform_mappings" / "test_vendor" / kPlatform /
      "raw_platform_mapping.json";
  fs::remove(rawMappingPath);

  try {
    resolveAgentConfigInputs(fbossRoot, kPlatform, kProfile);
    FAIL() << "Expected missing raw platform mapping to fail";
  } catch (const FbossError& error) {
    EXPECT_NE(
        std::string(error.what()).find(rawMappingPath.string()),
        std::string::npos);
  }
}

TEST(AgentConfigGenTest, ReportsMalformedPortAssignmentPath) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  createTestPlatform(fbossRoot, "test_vendor");
  const auto assignmentPath = fbossRoot / "lib" / "platform_mapping_v2" /
      "generated_platform_mappings" / "test_vendor" / kPlatform /
      "port_id_to_port_assignment.json";
  writeTestFile(assignmentPath, "not JSON");

  try {
    resolveAgentConfigInputs(fbossRoot, kPlatform, kProfile);
    FAIL() << "Expected malformed port assignments to fail";
  } catch (const FbossError& error) {
    EXPECT_NE(
        std::string(error.what()).find(assignmentPath.string()),
        std::string::npos);
  }
}

TEST(AgentConfigGenTest, GeneratesJsonAsicConfig) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  createTestPlatform(
      fbossRoot, "test_vendor", kPlatform, "JSON_CONFIG", ".json", kAsicJson);

  const auto inputs = resolveAgentConfigInputs(fbossRoot, kPlatform, kProfile);
  const auto platformConfig = generatePlatformConfig(inputs);
  const auto& common = *platformConfig.chip()->asicConfig()->common();

  ASSERT_EQ(common.getType(), cfg::AsicConfigEntry::Type::jsonConfig);
  EXPECT_EQ(common.jsonConfig().value(), kAsicJson);
}

TEST(AgentConfigGenTest, GeneratesKeyValueAsicConfig) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  createTestPlatform(
      fbossRoot,
      "test_vendor",
      kPlatform,
      "KEY_VALUE_CONFIG",
      ".json",
      kKeyValueConfig);

  const auto inputs = resolveAgentConfigInputs(fbossRoot, kPlatform, kProfile);
  const auto platformConfig = generatePlatformConfig(inputs);
  const auto& common = *platformConfig.chip()->asicConfig()->common();
  const std::map<std::string, std::string> expected{
      {"answer", "42"}, {"foo", "bar"}};

  ASSERT_EQ(common.getType(), cfg::AsicConfigEntry::Type::config);
  EXPECT_EQ(common.config().value(), expected);
}

TEST(AgentConfigGenTest, RejectsUnsupportedAsicConfigType) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const auto fbossRoot = fs::path(temporaryDirectory.path().string()) / "fboss";
  createTestPlatform(
      fbossRoot, "test_vendor", kPlatform, "UNSUPPORTED_CONFIG", ".json", "{}");

  EXPECT_THROW(
      resolveAgentConfigInputs(fbossRoot, kPlatform, kProfile), FbossError);
}

TEST(AgentConfigGenTest, SerializesAndWritesAgentConfig) {
  folly::test::TemporaryDirectory sourceDirectory;
  folly::test::TemporaryDirectory outputDirectory;
  const auto fbossRoot = fs::path(sourceDirectory.path().string()) / "fboss";
  createTestPlatform(fbossRoot, "test_vendor");

  auto outputPath = generateAgentConfig(
      kPlatform,
      kProfile,
      fbossRoot,
      fs::path(outputDirectory.path().string()));

  EXPECT_EQ(outputPath.filename(), "agent.conf");
  cfg::AgentConfig config;
  apache::thrift::SimpleJSONSerializer::deserialize(
      readFile(outputPath), config);
  EXPECT_EQ(*config.defaultCommandLineArgs(), expectedHwTestCommandLineArgs());
  const auto inputs = resolveAgentConfigInputs(fbossRoot, kPlatform, kProfile);
  EXPECT_EQ(*config.sw(), generateSwitchConfig(inputs));
  EXPECT_EQ(*config.platform(), generatePlatformConfig(inputs));
  EXPECT_TRUE(config.thriftApiToRateLimitInQps()->empty());
}

TEST(AgentConfigGenTest, GeneratesBenchmarkWithDefaultAsicConfigVariant) {
  folly::test::TemporaryDirectory sourceDirectory;
  folly::test::TemporaryDirectory outputDirectory;
  const auto fbossRoot = fs::path(sourceDirectory.path().string()) / "fboss";
  const auto platformDirectory = createTestPlatform(fbossRoot, "test_vendor");
  constexpr std::string_view kDefaultAsicYaml = "ASIC_CONFIG: default\n";
  writeTestFile(
      platformDirectory / "asic_config" / "generated" / "default.yml",
      kDefaultAsicYaml);

  const auto outputPath = generateAgentConfig(
      kPlatform,
      kBenchmarkProfile,
      fbossRoot,
      fs::path(outputDirectory.path().string()),
      std::nullopt,
      std::nullopt,
      "default");

  cfg::AgentConfig config;
  apache::thrift::SimpleJSONSerializer::deserialize(
      readFile(outputPath), config);
  EXPECT_EQ(
      *config.defaultCommandLineArgs(), expectedBenchmarkCommandLineArgs());
  EXPECT_EQ(
      config.platform()
          ->chip()
          ->asicConfig()
          .as_const()
          .value()
          .common()
          ->yamlConfig()
          .value(),
      kDefaultAsicYaml);
  EXPECT_EQ(
      *config.platform()->portIdToPortAssignment(),
      makePortAssignments(1, kPortName));
  EXPECT_EQ(config.sw()->aclTableGroups()->size(), 1);
  EXPECT_TRUE(config.sw()->ports()->empty());
  EXPECT_TRUE(config.sw()->vlans()->empty());
  EXPECT_TRUE(config.sw()->vlanPorts()->empty());
  EXPECT_TRUE(config.sw()->interfaces()->empty());
  EXPECT_TRUE(config.sw()->cpuQueues()->empty());
  EXPECT_FALSE(config.sw()->cpuTrafficPolicy());
  EXPECT_FALSE(config.sw()->sdkVersion());
}

TEST(AgentConfigComparisonTest, ReportsByteMatch) {
  const auto config = serializeAgentConfig("first: 1\nsecond: 2\n");

  const auto result = compareAgentConfigContents(config, config);

  EXPECT_EQ(result.status, AgentConfigComparisonStatus::BYTE_MATCH);
  EXPECT_EQ(result.ignoredPaths, std::vector<std::string>{"sw.sdkVersion"});
  EXPECT_TRUE(result.semanticDifferences.empty());
}

TEST(AgentConfigComparisonTest, IgnoresYamlMapOrder) {
  const auto generated = serializeAgentConfig("first: 1\nsecond: 2\n");
  const auto reference = serializeAgentConfig("second: 2\nfirst: 1\n");

  const auto result = compareAgentConfigContents(generated, reference);

  EXPECT_EQ(result.status, AgentConfigComparisonStatus::SEMANTIC_MATCH);
  EXPECT_EQ(
      result.semanticDifferences,
      std::vector<std::string>{
          ".platform.chip.asicConfig.common.yamlConfig: YAML formatting or map ordering"});
}

TEST(AgentConfigComparisonTest, ReportsSemanticContentMismatch) {
  const auto generated = serializeAgentConfig("value: generated\n");
  const auto reference = serializeAgentConfig("value: reference\n");

  const auto result = compareAgentConfigContents(generated, reference);

  EXPECT_EQ(result.status, AgentConfigComparisonStatus::CONTENT_MISMATCH);
  EXPECT_NE(result.normalizedGenerated, result.normalizedReference);
  EXPECT_EQ(
      result.differingPaths,
      std::vector<std::string>{".platform.chip.asicConfig.common.yamlConfig"});
}

TEST(AgentConfigComparisonTest, FormatsMapKeysAsJqExpressions) {
  const auto generated =
      serializeAgentConfig("value: same\n", false, std::nullopt, 0);
  const auto reference =
      serializeAgentConfig("value: same\n", false, std::nullopt, 1);

  const auto result = compareAgentConfigContents(generated, reference);

  EXPECT_EQ(result.status, AgentConfigComparisonStatus::CONTENT_MISMATCH);
  EXPECT_EQ(
      result.differingPaths,
      std::vector<std::string>{
          ".sw.switchSettings.switchIdToSwitchInfo[\"0\"].switchIndex"});
}

TEST(AgentConfigComparisonTest, AppliesUniversalIgnoredPaths) {
  const auto generated = serializeAgentConfig("value: same\n");
  const auto reference = serializeAgentConfig("value: same\n", true);

  const auto result = compareAgentConfigContents(generated, reference);

  EXPECT_EQ(result.status, AgentConfigComparisonStatus::SEMANTIC_MATCH);
  EXPECT_EQ(result.ignoredPaths, std::vector<std::string>{"sw.sdkVersion"});
  EXPECT_EQ(
      result.semanticDifferences,
      std::vector<std::string>{".sw.sdkVersion: ignored by comparison policy"});
}

TEST(AgentConfigComparisonTest, AppliesAdditionalIgnoredPaths) {
  const auto generated =
      serializeAgentConfig("value: same\n", false, "generated");
  const auto reference =
      serializeAgentConfig("value: same\n", false, "reference");
  AgentConfigComparisonOptions options;
  options.ignoredPaths = {"defaultCommandLineArgs.test_argument"};

  const auto result = compareAgentConfigContents(generated, reference, options);

  EXPECT_EQ(result.status, AgentConfigComparisonStatus::SEMANTIC_MATCH);
  const std::vector<std::string> expectedIgnoredPaths{
      "defaultCommandLineArgs.test_argument", "sw.sdkVersion"};
  EXPECT_EQ(result.ignoredPaths, expectedIgnoredPaths);
}

TEST(AgentConfigComparisonTest, RejectsUnknownIgnoredPath) {
  const auto config = serializeAgentConfig("value: same\n");
  AgentConfigComparisonOptions options;
  options.ignoredPaths = {"sw.notAField"};

  EXPECT_THROW(
      compareAgentConfigContents(config, config, options),
      std::invalid_argument);
}

TEST(AgentConfigGenTest, UsesUniqueDefaultOutputDirectory) {
  folly::test::TemporaryDirectory sourceDirectory;
  const auto fbossRoot = fs::path(sourceDirectory.path().string()) / "fboss";
  createTestPlatform(fbossRoot, "test_vendor");

  auto outputPath = generateAgentConfig(kPlatform, kProfile, fbossRoot);

  EXPECT_EQ(
      outputPath.parent_path().parent_path(),
      fs::path("/tmp/fboss2/config-gen"));
  EXPECT_EQ(outputPath.filename(), "agent.conf");
  EXPECT_TRUE(fs::is_regular_file(outputPath));

  std::error_code error;
  fs::remove_all(outputPath.parent_path(), error);
  EXPECT_FALSE(error);
}

TEST(AgentConfigGenTest, DoesNotOverwriteExistingConfig) {
  folly::test::TemporaryDirectory sourceDirectory;
  folly::test::TemporaryDirectory outputDirectory;
  const auto fbossRoot = fs::path(sourceDirectory.path().string()) / "fboss";
  const auto outputRoot = fs::path(outputDirectory.path().string());
  createTestPlatform(fbossRoot, "test_vendor");
  auto outputPath =
      generateAgentConfig(kPlatform, kProfile, fbossRoot, outputRoot);

  EXPECT_THROW(
      generateAgentConfig(kPlatform, kProfile, fbossRoot, outputRoot),
      std::system_error);

  cfg::AgentConfig config;
  EXPECT_NO_THROW(
      apache::thrift::SimpleJSONSerializer::deserialize(
          readFile(outputPath), config));
}

TEST(AgentConfigGenTest, GeneratesAndRefreshesManifestOutputs) {
  folly::test::TemporaryDirectory sourceDirectory;
  const auto fbossRoot = fs::path(sourceDirectory.path().string()) / "fboss";
  constexpr std::string_view kManifestPlatform = "wedge800bact";
  const auto platformDirectory =
      createTestPlatform(fbossRoot, "test_vendor", kManifestPlatform);
  writeConfigGenerationTarget(
      platformDirectory,
      R"({"serviceToOptions":{"AGENT":[{"profile":"DEFAULT"},{"profile":"HW_TEST"},{"profile":"BENCHMARK","asicConfigVariant":"default"}]}})");

  const std::vector<fs::path> expectedPaths{
      platformDirectory / "forwarding_stack" / "generated" / "agent" /
          "default.conf",
      platformDirectory / "forwarding_stack" / "generated" / "agent" /
          "hw_test.conf",
      platformDirectory / "forwarding_stack" / "generated" / "agent" /
          "benchmark.conf"};
  EXPECT_EQ(
      generateConfigsFromManifest(
          fbossRoot, ServiceType::AGENT, generateAgentConfigForManifest),
      expectedPaths);
  for (const auto& path : expectedPaths) {
    cfg::AgentConfig config;
    EXPECT_NO_THROW(
        apache::thrift::SimpleJSONSerializer::deserialize(
            readFile(path), config));
  }

  writeTestFile(expectedPaths.front(), "stale\n");
  const auto obsoletePath =
      expectedPaths.front().parent_path() / "removed.conf";
  writeTestFile(obsoletePath, "obsolete\n");
  EXPECT_EQ(
      generateConfigsFromManifest(
          fbossRoot, ServiceType::AGENT, generateAgentConfigForManifest),
      expectedPaths);
  EXPECT_NE(readFile(expectedPaths.front()), "stale\n");
  EXPECT_FALSE(fs::exists(obsoletePath));
}

TEST(AgentConfigGenTest, GeneratesAddressedLinkTestVariants) {
  folly::test::TemporaryDirectory sourceDirectory;
  const auto fbossRoot = fs::path(sourceDirectory.path().string()) / "fboss";
  const auto platformDirectory =
      createTestPlatform(fbossRoot, "test_vendor", "wedge800bact");
  const auto mappingDirectory = fbossRoot / "lib" / "platform_mapping_v2" /
      "generated_platform_mappings" / "test_vendor" / "wedge800bact";
  writeTwoPortGroupMapping(mappingDirectory);
  writeTestFile(
      platformDirectory / "forwarding_stack" / "link_topology" /
          "paired_ports.csv",
      "portA,portB,profile,hasTransceiver\n"
      "eth1/1/1,eth1/1/2,PROFILE_100G_4_NRZ_NOFEC,true\n");
  writeTestFile(
      platformDirectory / "forwarding_stack" / "link_topology" /
          "single_port.csv",
      "portA,portB,profile,hasTransceiver\n"
      "eth1/1/1,,PROFILE_100G_4_NRZ_NOFEC,true\n");
  writeConfigGenerationTarget(
      platformDirectory,
      R"({"serviceToOptions":{"AGENT":[{"profile":"LINK_TEST","asicConfigVariant":"default","linkTopologyVariant":"paired_ports"},{"profile":"LINK_TEST","variant":"stress","asicConfigVariant":"default","linkTopologyVariant":"single_port"}]}})");

  const auto outputPath = platformDirectory / "forwarding_stack" / "generated" /
      "agent" / "link_test.conf";
  const auto stressOutputPath = platformDirectory / "forwarding_stack" /
      "generated" / "agent" / "link_test_stress.conf";
  EXPECT_EQ(
      generateConfigsFromManifest(
          fbossRoot,
          ServiceType::AGENT,
          generateAgentConfigForManifest,
          "wedge800bact"),
      (std::vector<fs::path>{outputPath, stressOutputPath}));

  cfg::AgentConfig config;
  apache::thrift::SimpleJSONSerializer::deserialize(
      readFile(outputPath), config);
  ASSERT_EQ(config.sw()->ports()->size(), 2);
  const auto& controllingPort = config.sw()->ports()->at(0);
  EXPECT_EQ(*controllingPort.name(), kPortName);
  EXPECT_EQ(*controllingPort.profileID(), kPortProfile);
  EXPECT_EQ(*controllingPort.state(), cfg::PortState::ENABLED);
  EXPECT_EQ(
      controllingPort.expectedLLDPValues()->at(cfg::LLDPTag::PORT), "eth1/1/2");
  const auto& topologyPort = config.sw()->ports()->at(1);
  EXPECT_EQ(*topologyPort.name(), "eth1/1/2");
  EXPECT_EQ(*topologyPort.profileID(), kPortProfile);
  EXPECT_EQ(*topologyPort.state(), cfg::PortState::ENABLED);
  EXPECT_EQ(
      topologyPort.expectedLLDPValues()->at(cfg::LLDPTag::PORT), kPortName);
  EXPECT_FALSE(config.sw()->cpuQueues()->empty());
  EXPECT_TRUE(config.sw()->cpuTrafficPolicy());
  ASSERT_EQ(config.sw()->interfaces()->size(), 3);
  for (size_t index = 0; index < config.sw()->interfaces()->size(); ++index) {
    EXPECT_EQ(
        *config.sw()->interfaces()[index].ipAddresses(),
        (std::vector<std::string>{
            std::to_string(2400 + index) + "::/64",
            std::to_string(10 + index) + ".0.0.0/24"}));
  }

  cfg::AgentConfig stressConfig;
  apache::thrift::SimpleJSONSerializer::deserialize(
      readFile(stressOutputPath), stressConfig);
  ASSERT_EQ(stressConfig.sw()->ports()->size(), 1);
  EXPECT_EQ(*stressConfig.sw()->ports()->front().name(), kPortName);
  EXPECT_TRUE(
      stressConfig.sw()->ports()->front().expectedLLDPValues()->empty());
}

TEST(AgentConfigGenTest, AppliesTopologyWithoutModeledPeer) {
  folly::test::TemporaryDirectory sourceDirectory;
  const auto fbossRoot = fs::path(sourceDirectory.path().string()) / "fboss";
  const auto platformDirectory =
      createTestPlatform(fbossRoot, "test_vendor", "wedge800bact");
  const auto mappingDirectory = fbossRoot / "lib" / "platform_mapping_v2" /
      "generated_platform_mappings" / "test_vendor" / "wedge800bact";
  writeTwoPortGroupMapping(mappingDirectory);
  writeTestFile(
      platformDirectory / "forwarding_stack" / "link_topology" /
          "external_peer.csv",
      "portA,portB,profile,hasTransceiver\n"
      "eth1/1/1,,PROFILE_100G_4_NRZ_NOFEC,true\n");
  writeConfigGenerationTarget(
      platformDirectory,
      R"({"serviceToOptions":{"AGENT":[{"profile":"DEFAULT","linkTopologyVariant":"external_peer"}]}})");

  const auto outputPath = platformDirectory / "forwarding_stack" / "generated" /
      "agent" / "default.conf";
  EXPECT_EQ(
      generateConfigsFromManifest(
          fbossRoot,
          ServiceType::AGENT,
          generateAgentConfigForManifest,
          "wedge800bact"),
      std::vector<fs::path>{outputPath});

  cfg::AgentConfig config;
  apache::thrift::SimpleJSONSerializer::deserialize(
      readFile(outputPath), config);
  ASSERT_EQ(config.sw()->ports()->size(), 1);
  const auto& topologyPort = config.sw()->ports()->at(0);
  EXPECT_EQ(*topologyPort.name(), kPortName);
  EXPECT_EQ(*topologyPort.profileID(), kPortProfile);
  EXPECT_EQ(*topologyPort.state(), cfg::PortState::ENABLED);
  EXPECT_TRUE(topologyPort.expectedLLDPValues()->empty());
  for (const auto& interface : *config.sw()->interfaces()) {
    EXPECT_TRUE(interface.ipAddresses()->empty());
  }
}

TEST(AgentConfigGenTest, AssignsLinkTestAddressesBeyondFirstIpv4Octet) {
  cfg::SwitchConfig switchConfig;
  switchConfig.interfaces()->resize(251);

  assignLinkTestInterfaceIpAddresses(switchConfig);

  EXPECT_EQ(
      *switchConfig.interfaces()[213].ipAddresses(),
      (std::vector<std::string>{"2613::/64", "223.0.0.0/24"}));
  EXPECT_EQ(
      *switchConfig.interfaces()[214].ipAddresses(),
      (std::vector<std::string>{"2614::/64", "10.1.0.0/24"}));
  EXPECT_EQ(
      *switchConfig.interfaces()[250].ipAddresses(),
      (std::vector<std::string>{"2650::/64", "46.1.0.0/24"}));
}

TEST(AgentConfigGenTest, DisablesPortsOutsideLinkTopology) {
  folly::test::TemporaryDirectory sourceDirectory;
  const auto fbossRoot = fs::path(sourceDirectory.path().string()) / "fboss";
  createTestPlatform(fbossRoot, "test_vendor");
  writeTwoPortGroupMapping(
      fbossRoot / "lib" / "platform_mapping_v2" /
      "generated_platform_mappings" / "test_vendor" / kPlatform);
  const auto inputs = resolveAgentConfigInputs(fbossRoot, kPlatform, "default");

  cfg::SwitchConfig switchConfig;
  utility::addInterfacePortToConfig(
      switchConfig,
      inputs.platformMapping.get(),
      PortID(1),
      kPortProfile,
      VlanID(2001));
  utility::addInterfacePortToConfig(
      switchConfig,
      inputs.platformMapping.get(),
      PortID(2),
      kPortProfile,
      VlanID(2002));
  for (auto& port : *switchConfig.ports()) {
    port.state() = cfg::PortState::ENABLED;
    port.expectedLLDPValues()[cfg::LLDPTag::PORT] = "stale-peer";
  }
  const LinkTopology topology{
      {.portA = std::string(kPortName),
       .portB = std::nullopt,
       .profile = kPortProfile,
       .hasTransceiver = true}};

  applyLinkTopology(switchConfig, *inputs.platformMapping, topology);

  std::map<std::string, cfg::PortState> portStates;
  for (const auto& port : *switchConfig.ports()) {
    portStates.emplace(*port.name(), *port.state());
    EXPECT_TRUE(port.expectedLLDPValues()->empty());
  }
  const std::map<std::string, cfg::PortState> expectedPortStates{
      {std::string(kPortName), cfg::PortState::ENABLED},
      {"eth1/1/2", cfg::PortState::DISABLED}};
  EXPECT_EQ(portStates, expectedPortStates);
}

TEST(AgentConfigGenTest, RemovesPortsSubsumedByLinkTopologyProfile) {
  folly::test::TemporaryDirectory sourceDirectory;
  const auto fbossRoot = fs::path(sourceDirectory.path().string()) / "fboss";
  createTestPlatform(fbossRoot, "test_vendor");
  writeTwoPortGroupMapping(
      fbossRoot / "lib" / "platform_mapping_v2" /
      "generated_platform_mappings" / "test_vendor" / kPlatform);
  const auto inputs = resolveAgentConfigInputs(fbossRoot, kPlatform, "default");

  cfg::SwitchConfig switchConfig;
  utility::addInterfacePortToConfig(
      switchConfig,
      inputs.platformMapping.get(),
      PortID(1),
      kPortProfile,
      VlanID(2001));
  utility::addInterfacePortToConfig(
      switchConfig,
      inputs.platformMapping.get(),
      PortID(2),
      kPortProfile,
      VlanID(2002));
  const LinkTopology topology{
      {.portA = std::string(kPortName),
       .portB = std::string(kPortName),
       .profile = kWidePortProfile,
       .hasTransceiver = true}};

  applyLinkTopology(switchConfig, *inputs.platformMapping, topology);

  ASSERT_EQ(switchConfig.ports()->size(), 1);
  EXPECT_EQ(*switchConfig.ports()->front().name(), kPortName);
  EXPECT_EQ(*switchConfig.ports()->front().profileID(), kWidePortProfile);
  EXPECT_EQ(*switchConfig.ports()->front().state(), cfg::PortState::ENABLED);
  EXPECT_EQ(
      *switchConfig.vlanPorts(),
      std::vector<cfg::VlanPort>{
          utility::createVlanPortConfig(PortID(1), VlanID(2001))});
  ASSERT_EQ(switchConfig.vlans()->size(), 1);
  EXPECT_EQ(*switchConfig.vlans()->front().id(), 2001);
  ASSERT_EQ(switchConfig.interfaces()->size(), 1);
  EXPECT_EQ(*switchConfig.interfaces()->front().intfID(), 2001);
}

TEST(AgentConfigGenTest, GeneratesOnlySelectedPlatformDeclaration) {
  folly::test::TemporaryDirectory sourceDirectory;
  const auto fbossRoot = fs::path(sourceDirectory.path().string()) / "fboss";
  const auto selectedPlatformDirectory =
      createTestPlatform(fbossRoot, "a_vendor", "wedge800bact");
  writeConfigGenerationTarget(
      selectedPlatformDirectory,
      R"({"serviceToOptions":{"AGENT":[{"profile":"DEFAULT"},{"profile":"HW_TEST"}]}})");
  const auto otherPlatformDirectory =
      createTestPlatform(fbossRoot, "z_vendor", "montblanc");
  writeConfigGenerationTarget(
      otherPlatformDirectory,
      R"({"serviceToOptions":{"AGENT":[{"profile":"DEFAULT"}]}})");

  const auto selectedObsoletePath = selectedPlatformDirectory /
      "forwarding_stack" / "generated" / "agent" / "removed.conf";
  const auto otherObsoletePath = otherPlatformDirectory / "forwarding_stack" /
      "generated" / "agent" / "removed.conf";
  writeTestFile(selectedObsoletePath, "obsolete\n");
  writeTestFile(otherObsoletePath, "obsolete\n");
  const std::vector<fs::path> expectedPaths{
      selectedPlatformDirectory / "forwarding_stack" / "generated" / "agent" /
          "default.conf",
      selectedPlatformDirectory / "forwarding_stack" / "generated" / "agent" /
          "hw_test.conf"};

  EXPECT_EQ(
      generateConfigsFromManifest(
          fbossRoot,
          ServiceType::AGENT,
          generateAgentConfigForManifest,
          "wedge800bact"),
      expectedPaths);
  EXPECT_FALSE(fs::exists(selectedObsoletePath));
  EXPECT_TRUE(fs::exists(otherObsoletePath));
  EXPECT_FALSE(
      fs::exists(
          otherPlatformDirectory / "forwarding_stack" / "generated" / "agent" /
          "default.conf"));
}

TEST(AgentConfigGenTest, VerifiesMissingStaleAndObsoleteManifestOutputs) {
  folly::test::TemporaryDirectory sourceDirectory;
  const auto fbossRoot = fs::path(sourceDirectory.path().string()) / "fboss";
  constexpr std::string_view kManifestPlatform = "wedge800bact";
  const auto platformDirectory =
      createTestPlatform(fbossRoot, "test_vendor", kManifestPlatform);
  writeConfigGenerationTarget(
      platformDirectory,
      R"({"serviceToOptions":{"AGENT":[{"profile":"DEFAULT"},{"profile":"HW_TEST"}]}})");

  const auto generatedPaths = generateConfigsFromManifest(
      fbossRoot, ServiceType::AGENT, generateAgentConfigForManifest);
  EXPECT_TRUE(verifyConfigsFromManifest(
                  fbossRoot, ServiceType::AGENT, generateAgentConfigForManifest)
                  .isFresh());

  const auto& missingPath = generatedPaths.front();
  const auto& stalePath = generatedPaths.back();
  const auto obsoletePath = platformDirectory / "forwarding_stack" /
      "generated" / "agent" / "removed.conf";
  fs::remove(missingPath);
  writeTestFile(stalePath, "stale\n");
  writeTestFile(obsoletePath, "obsolete\n");

  const auto result = verifyConfigsFromManifest(
      fbossRoot, ServiceType::AGENT, generateAgentConfigForManifest);
  EXPECT_EQ(result.missing, std::vector<fs::path>{missingPath});
  EXPECT_EQ(result.stale, std::vector<fs::path>{stalePath});
  EXPECT_EQ(result.obsolete, std::vector<fs::path>{obsoletePath});
  EXPECT_FALSE(fs::exists(missingPath));
  EXPECT_EQ(readFile(stalePath), "stale\n");
  EXPECT_EQ(readFile(obsoletePath), "obsolete\n");
}

TEST(AgentConfigGenTest, DoesNotPartiallyWriteManifestOutputs) {
  folly::test::TemporaryDirectory sourceDirectory;
  const auto fbossRoot = fs::path(sourceDirectory.path().string()) / "fboss";
  constexpr std::string_view kManifestPlatform = "wedge800bact";
  const auto platformDirectory =
      createTestPlatform(fbossRoot, "a_vendor", kManifestPlatform);
  writeConfigGenerationTarget(
      platformDirectory,
      R"({"serviceToOptions":{"AGENT":[{"profile":"HW_TEST"}]}})");
  const auto failingPlatformDirectory =
      createTestPlatform(fbossRoot, "z_vendor", "montblanc");
  writeConfigGenerationTarget(
      failingPlatformDirectory,
      R"({"serviceToOptions":{"AGENT":[{"profile":"DEFAULT"}]}})");
  fs::remove(
      failingPlatformDirectory / "asic_config" / "generated" / "default.yml");

  EXPECT_THROW(
      generateConfigsFromManifest(
          fbossRoot, ServiceType::AGENT, generateAgentConfigForManifest),
      FbossError);
  EXPECT_FALSE(
      fs::exists(platformDirectory / "forwarding_stack" / "generated"));
}

TEST(AgentConfigGenTest, RejectsUnknownPlatformOrProfile) {
  folly::test::TemporaryDirectory sourceDirectory;
  folly::test::TemporaryDirectory outputDirectory;
  const auto fbossRoot = fs::path(sourceDirectory.path().string()) / "fboss";
  const auto outputRoot = fs::path(outputDirectory.path().string());
  createTestPlatform(fbossRoot, "test_vendor");

  EXPECT_THROW(
      generateAgentConfig("unknown", kProfile, fbossRoot, outputRoot),
      FbossError);
  EXPECT_THROW(
      generateAgentConfig(kPlatform, "unknown", fbossRoot, outputRoot),
      FbossError);
  EXPECT_TRUE(fs::is_empty(outputRoot));
}

TEST(AgentConfigGenTest, RejectsActiveConfigDirectory) {
  folly::test::TemporaryDirectory sourceDirectory;
  const auto fbossRoot = fs::path(sourceDirectory.path().string()) / "fboss";
  createTestPlatform(fbossRoot, "test_vendor");

  EXPECT_THROW(
      generateAgentConfig(
          kPlatform,
          kProfile,
          fbossRoot,
          fs::path("/etc/coop/config-gen-test")),
      std::invalid_argument);
}

TEST(CmdConfigGenAgentTest, RegistersAgentCommand) {
  CLI::App app{"Test CLI"};
  EXPECT_NO_THROW(CmdSubcommands().init(app, kCommandTree(), {}, {}));

  auto* config = utils::getSubcommandIf(app, "config");
  ASSERT_NE(config, nullptr);
  auto* gen = utils::getSubcommandIf(*config, "gen");
  ASSERT_NE(gen, nullptr);
  EXPECT_NE(utils::getSubcommandIf(*gen, "agent"), nullptr);
}

TEST(CmdConfigGenAgentTest, RejectsProfileWithoutPlatform) {
  auto localOptions = CmdLocalOptions::getInstance();
  localOptions->clear();
  localOptions->setLocalOption(
      std::string(kConfigGenAgentCommand),
      kConfigGenAgentFbossRoot,
      "/tmp/fboss");
  localOptions->setLocalOption(
      std::string(kConfigGenAgentCommand),
      kConfigGenAgentProfile,
      std::string(kProfile));

  testing::internal::CaptureStderr();
  EXPECT_THROW(CmdConfigGenAgent().run(), std::runtime_error);
  const auto error = testing::internal::GetCapturedStderr();
  localOptions->clear();

  EXPECT_NE(error.find("--profile"), std::string::npos);
  EXPECT_NE(error.find("require --platform"), std::string::npos);
}

TEST(CmdConfigGenAgentTest, UsesExplicitAsicConfigVariant) {
  folly::test::TemporaryDirectory sourceDirectory;
  folly::test::TemporaryDirectory outputDirectory;
  const auto fbossRoot = fs::path(sourceDirectory.path().string()) / "fboss";
  const auto platformDirectory = createTestPlatform(fbossRoot, "test_vendor");
  constexpr std::string_view kDefaultAsicYaml = "ASIC_CONFIG: default\n";
  writeTestFile(
      platformDirectory / "asic_config" / "generated" / "default.yml",
      kDefaultAsicYaml);

  auto localOptions = CmdLocalOptions::getInstance();
  localOptions->clear();
  localOptions->setLocalOption(
      std::string(kConfigGenAgentCommand),
      kConfigGenAgentPlatform,
      std::string(kPlatform));
  localOptions->setLocalOption(
      std::string(kConfigGenAgentCommand),
      kConfigGenAgentProfile,
      std::string(kBenchmarkProfile));
  localOptions->setLocalOption(
      std::string(kConfigGenAgentCommand),
      kConfigGenAgentFbossRoot,
      fbossRoot.string());
  localOptions->setLocalOption(
      std::string(kConfigGenAgentCommand),
      kConfigGenAgentAsicConfigVariant,
      "default");
  localOptions->setLocalOption(
      std::string(kConfigGenAgentCommand),
      kConfigGenAgentOutputDirectory,
      outputDirectory.path().string());

  testing::internal::CaptureStdout();
  EXPECT_NO_THROW(CmdConfigGenAgent().run());
  testing::internal::GetCapturedStdout();
  localOptions->clear();

  cfg::AgentConfig config;
  apache::thrift::SimpleJSONSerializer::deserialize(
      readFile(fs::path(outputDirectory.path().string()) / "agent.conf"),
      config);
  EXPECT_EQ(
      config.platform()
          ->chip()
          ->asicConfig()
          .as_const()
          .value()
          .common()
          ->yamlConfig()
          .value(),
      kDefaultAsicYaml);
}

TEST(CmdConfigGenAgentTest, VerifiesDeclaredOutputsGloballyAndForPlatform) {
  folly::test::TemporaryDirectory sourceDirectory;
  const auto fbossRoot = fs::path(sourceDirectory.path().string()) / "fboss";
  constexpr std::string_view kManifestPlatform = "wedge800bact";
  createTestPlatform(fbossRoot, "test_vendor", kManifestPlatform);
  const auto platformDirectory =
      findPlatformConfigDirectory(fbossRoot, kManifestPlatform);
  writeConfigGenerationTarget(
      platformDirectory.path,
      R"({"serviceToOptions":{"AGENT":[{"profile":"DEFAULT"}]}})");
  const auto generatedPaths = generateConfigsFromManifest(
      fbossRoot, ServiceType::AGENT, generateAgentConfigForManifest);
  writeTestFile(generatedPaths.front(), "stale\n");

  auto localOptions = CmdLocalOptions::getInstance();
  localOptions->clear();
  localOptions->setLocalOption(
      std::string(kConfigGenAgentCommand),
      kConfigGenAgentFbossRoot,
      fbossRoot.string());
  localOptions->setLocalOption(
      std::string(kConfigGenAgentCommand), kConfigGenAgentVerify, "true");

  testing::internal::CaptureStderr();
  EXPECT_THROW(CmdConfigGenAgent().run(), std::runtime_error);
  auto error = testing::internal::GetCapturedStderr();
  EXPECT_NE(error.find("Stale:"), std::string::npos);
  EXPECT_NE(error.find(generatedPaths.front().string()), std::string::npos);
  EXPECT_NE(
      error.find(std::string(kConfigGenerationCommand) + fbossRoot.string()),
      std::string::npos);

  localOptions->setLocalOption(
      std::string(kConfigGenAgentCommand),
      kConfigGenAgentPlatform,
      std::string(kManifestPlatform));
  testing::internal::CaptureStderr();
  EXPECT_THROW(CmdConfigGenAgent().run(), std::runtime_error);
  error = testing::internal::GetCapturedStderr();
  localOptions->clear();
  EXPECT_NE(error.find("Stale:"), std::string::npos);
  EXPECT_NE(
      error.find(
          "Agent generated configurations for PLATFORM_WEDGE800BACT are out "
          "of date:"),
      std::string::npos);
  EXPECT_NE(
      error.find(
          std::string(kConfigGenerationCommand) + fbossRoot.string() +
          " --platform " + std::string(kManifestPlatform)),
      std::string::npos);
}

TEST(CmdConfigGenAgentTest, ReportsContentMismatchAsLocalError) {
  folly::test::TemporaryDirectory sourceDirectory;
  folly::test::TemporaryDirectory referenceDirectory;
  folly::test::TemporaryDirectory outputDirectory;
  const auto fbossRoot = fs::path(sourceDirectory.path().string()) / "fboss";
  createTestPlatform(fbossRoot, "test_vendor");

  const auto referencePath = generateAgentConfig(
      kPlatform,
      kProfile,
      fbossRoot,
      fs::path(referenceDirectory.path().string()));
  cfg::AgentConfig referenceConfig;
  apache::thrift::SimpleJSONSerializer::deserialize(
      readFile(referencePath), referenceConfig);
  cfg::AsicConfigEntry wrongCommon;
  wrongCommon.set_yamlConfig("wrong: config\n");
  cfg::AsicConfig wrongAsicConfig;
  wrongAsicConfig.common() = std::move(wrongCommon);
  referenceConfig.platform()->chip()->set_asicConfig(
      std::move(wrongAsicConfig));
  writeTestFile(
      referencePath,
      apache::thrift::SimpleJSONSerializer::serialize<std::string>(
          referenceConfig));

  auto localOptions = CmdLocalOptions::getInstance();
  localOptions->clear();
  localOptions->setLocalOption(
      std::string(kConfigGenAgentCommand),
      kConfigGenAgentPlatform,
      std::string(kPlatform));
  localOptions->setLocalOption(
      std::string(kConfigGenAgentCommand),
      kConfigGenAgentProfile,
      std::string(kProfile));
  localOptions->setLocalOption(
      std::string(kConfigGenAgentCommand),
      kConfigGenAgentFbossRoot,
      fbossRoot.string());
  localOptions->setLocalOption(
      std::string(kConfigGenAgentCommand),
      kConfigGenAgentReferenceConfigFile,
      referencePath.string());
  localOptions->setLocalOption(
      std::string(kConfigGenAgentCommand),
      kConfigGenAgentOutputDirectory,
      outputDirectory.path().string());

  testing::internal::CaptureStderr();
  EXPECT_THROW(CmdConfigGenAgent().run(), std::runtime_error);
  const auto error = testing::internal::GetCapturedStderr();
  localOptions->clear();

  EXPECT_NE(error.find("Result: CONTENT_MISMATCH"), std::string::npos);
  EXPECT_NE(
      error.find(".platform.chip.asicConfig.common.yamlConfig"),
      std::string::npos);
  EXPECT_EQ(error.find("Thrift call failed"), std::string::npos);
  EXPECT_EQ(error.find("localhost"), std::string::npos);
}

} // namespace
} // namespace facebook::fboss::configgen
