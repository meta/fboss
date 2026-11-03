/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/platforms/common/PlatformMappingUtils.h"

#include <array>
#include <filesystem>

#include <folly/Conv.h>
#include <folly/FileUtil.h>
#include <folly/ScopeGuard.h>
#include <folly/testing/TestUtil.h>
#include <gtest/gtest.h>

#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/FbossError.h"
#include "fboss/lib/platforms/PlatformDescriptor.h"

namespace fs = std::filesystem;

using namespace facebook::fboss;

namespace {

std::string createDescriptorJson() {
  return R"({
        "platformType": )" +
      folly::to<std::string>(
             static_cast<int>(PlatformType::PLATFORM_WEDGE800BACT)) +
      R"(,
        "productNamePrefixes": ["Wedge800BACT"],
        "modeNames": ["wedge800bact"],
        "asicType": )" +
      folly::to<std::string>(
             static_cast<int>(cfg::AsicType::ASIC_TYPE_TOMAHAWK5)) +
      R"(
      })";
}

std::string createPlatformMappingJson() {
  return R"({
        "ports": {},
        "chips": [],
        "platformSupportedProfiles": []
      })";
}

} // namespace

TEST(TestPlatformMappingUtils, initPlatformMappingUsesDescriptorMapping) {
  auto tmpDir = folly::test::TemporaryDirectory();
  auto platformDir = fs::path(tmpDir.path().string()) / "platforms" / "accton" /
      "wedge800bact";
  fs::create_directories(platformDir);
  ASSERT_TRUE(
      folly::writeFile(
          createDescriptorJson(),
          (platformDir / "platform_descriptor.json").string().c_str()));
  ASSERT_TRUE(
      folly::writeFile(
          createPlatformMappingJson(),
          (platformDir / "platform_mapping.json").string().c_str()));

  FLAGS_platform_mapping_override_path = "";
  FLAGS_platform_descriptor_config_path =
      (fs::path(tmpDir.path().string()) / "platforms").string();

  auto platformMapping =
      utility::initPlatformMapping(PlatformType::PLATFORM_WEDGE800BACT);

  EXPECT_TRUE(platformMapping->getPlatformPorts().empty());
  EXPECT_TRUE(platformMapping->getChips().empty());
}

TEST(TestPlatformMappingUtils, initPlatformMappingSupportsFakeWedge) {
  FLAGS_platform_mapping_override_path = "";
  FLAGS_platform_descriptor_config_path = "";

  const auto platformMapping =
      utility::initPlatformMapping(PlatformType::PLATFORM_FAKE_WEDGE);

  EXPECT_FALSE(platformMapping->getPlatformPorts().empty());
}

TEST(TestPlatformMappingUtils, initPlatformMappingUsesOverrideForNewPlatforms) {
  const auto savedMappingOverridePath = FLAGS_platform_mapping_override_path;
  const auto savedDescriptorConfigPath = FLAGS_platform_descriptor_config_path;
  SCOPE_EXIT {
    FLAGS_platform_mapping_override_path = savedMappingOverridePath;
    FLAGS_platform_descriptor_config_path = savedDescriptorConfigPath;
  };

  auto tmpDir = folly::test::TemporaryDirectory();
  auto mappingPath = fs::path(tmpDir.path().string()) / "platform_mapping.json";
  ASSERT_TRUE(
      folly::writeFile(createPlatformMappingJson(), mappingPath.c_str()));
  FLAGS_platform_mapping_override_path = mappingPath.string();
  FLAGS_platform_descriptor_config_path = "";

  const auto platformMapping =
      utility::initPlatformMapping(PlatformType::PLATFORM_WEDGE800CACT);

  EXPECT_TRUE(platformMapping->getPlatformPorts().empty());
  EXPECT_TRUE(platformMapping->getChips().empty());
}

TEST(
    TestPlatformMappingUtils,
    initPlatformMappingRequiresExternalMappingForNewPlatforms) {
  const auto savedMappingOverridePath = FLAGS_platform_mapping_override_path;
  const auto savedDescriptorConfigPath = FLAGS_platform_descriptor_config_path;
  SCOPE_EXIT {
    FLAGS_platform_mapping_override_path = savedMappingOverridePath;
    FLAGS_platform_descriptor_config_path = savedDescriptorConfigPath;
  };

  FLAGS_platform_mapping_override_path = "";
  FLAGS_platform_descriptor_config_path = "";

  constexpr auto expectedError =
      "all newer platforms than wedge800 no longer ship a compiled-in "
      "platform mapping. The mapping must be provided externally via "
      "--platform_descriptor_config_path or "
      "--platform_mapping_override_path.";
  constexpr auto futurePlatformType = static_cast<PlatformType>(9999);
  constexpr std::array platformTypes{
      PlatformType::PLATFORM_WEDGE800BACT,
      PlatformType::PLATFORM_WEDGE800BNHP,
      PlatformType::PLATFORM_WEDGE800CACT,
      PlatformType::PLATFORM_WEDGE800CNHP,
      PlatformType::PLATFORM_ICECUBE800BANW,
      PlatformType::PLATFORM_M4062NHP,
      PlatformType::PLATFORM_M4061CLSC,
      PlatformType::PLATFORM_M5120CSC,
      PlatformType::PLATFORM_M4340NHP,
      PlatformType::PLATFORM_M4061NHP,
      futurePlatformType,
  };
  for (const auto type : platformTypes) {
    try {
      utility::initPlatformMapping(type);
      FAIL() << "Expected platform mapping initialization to fail";
    } catch (const FbossError& error) {
      EXPECT_STREQ(error.what(), expectedError);
    }
  }
}

TEST(
    TestPlatformMappingUtils,
    initPlatformMapping_RawFlagDisabled_UsesLegacyMapping) {
  const auto savedUseRawPlatformMapping = FLAGS_use_raw_platform_mapping;
  SCOPE_EXIT {
    FLAGS_use_raw_platform_mapping = savedUseRawPlatformMapping;
  };
  FLAGS_use_raw_platform_mapping = false;

  const auto platformMapping = utility::initPlatformMapping(
      PlatformType::PLATFORM_FAKE_WEDGE, cfg::PlatformConfig());

  EXPECT_FALSE(platformMapping->getPlatformPorts().empty());
}

TEST(
    TestPlatformMappingUtils,
    initPlatformMapping_RawFlagEnabledWithoutDescriptorPath_Fails) {
  const auto savedUseRawPlatformMapping = FLAGS_use_raw_platform_mapping;
  const auto savedPlatformDescriptorConfigPath =
      FLAGS_platform_descriptor_config_path;
  SCOPE_EXIT {
    FLAGS_use_raw_platform_mapping = savedUseRawPlatformMapping;
    FLAGS_platform_descriptor_config_path = savedPlatformDescriptorConfigPath;
  };
  FLAGS_use_raw_platform_mapping = true;
  FLAGS_platform_descriptor_config_path = "";

  EXPECT_THROW(
      utility::initPlatformMapping(
          PlatformType::PLATFORM_FAKE_WEDGE, cfg::PlatformConfig()),
      FbossError);
}

TEST(
    TestPlatformMappingUtils,
    initPlatformMapping_RawFlagEnabledWithoutAssignments_Fails) {
  const auto savedUseRawPlatformMapping = FLAGS_use_raw_platform_mapping;
  const auto savedPlatformDescriptorConfigPath =
      FLAGS_platform_descriptor_config_path;
  SCOPE_EXIT {
    FLAGS_use_raw_platform_mapping = savedUseRawPlatformMapping;
    FLAGS_platform_descriptor_config_path = savedPlatformDescriptorConfigPath;
  };
  FLAGS_use_raw_platform_mapping = true;
  auto tmpDir = folly::test::TemporaryDirectory();
  auto platformDir = fs::path(tmpDir.path().string()) / "platforms" / "accton" /
      "wedge800bact";
  fs::create_directories(platformDir);
  ASSERT_TRUE(
      folly::writeFile(
          createDescriptorJson(),
          (platformDir / "platform_descriptor.json").string().c_str()));
  ASSERT_TRUE(
      folly::writeFile(
          createPlatformMappingJson(),
          (platformDir / "platform_mapping.json").string().c_str()));
  FLAGS_platform_descriptor_config_path =
      (fs::path(tmpDir.path().string()) / "platforms").string();

  EXPECT_THROW(
      utility::initPlatformMapping(
          PlatformType::PLATFORM_FAKE_WEDGE, cfg::PlatformConfig()),
      FbossError);
}
