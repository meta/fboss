/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <folly/FileUtil.h>
#include <folly/testing/TestUtil.h>
#include <gtest/gtest.h>

#include "fboss/configs/platforms/generic/forwarding_stack/utils/ConfigGenerationSpecUtils.h"

namespace facebook::fboss::configgen {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kConfig =
    R"({"serviceToOptions":{"AGENT":[{"profile":"BENCHMARK","variant":"example","asicConfigVariant":"default","linkTopologyVariant":"link_test_default"}]}})";

void writeTestFile(const fs::path& path, std::string_view contents) {
  fs::create_directories(path.parent_path());
  if (!folly::writeFile(contents, path.c_str())) {
    throw std::runtime_error("Unable to write test file " + path.string());
  }
}

TEST(ConfigGenerationSpecUtilsTest, ParsesEnumNamesIntoTypedConfig) {
  const auto spec = parseConfigGenerationSpec(kConfig);

  const auto& option = spec.serviceToOptions()->at(ServiceType::AGENT).at(0);
  EXPECT_EQ(*option.profile(), ConfigProfileType::BENCHMARK);
  EXPECT_EQ(*option.variant(), "example");
  EXPECT_EQ(*option.asicConfigVariant(), "default");
  EXPECT_EQ(*option.linkTopologyVariant(), "link_test_default");
}

TEST(ConfigGenerationSpecUtilsTest, LoadsPlatformFromOwningDirectory) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const fs::path platformDirectory =
      fs::path(temporaryDirectory.path().string()) / "montblanc";
  writeTestFile(
      platformDirectory / "forwarding_stack" / "config_generation.json",
      kConfig);

  const auto target = loadConfigGenerationTarget(platformDirectory);

  EXPECT_EQ(target.platform, PlatformType::PLATFORM_MONTBLANC);
  EXPECT_EQ(target.platformDirectory, platformDirectory);
  EXPECT_EQ(target.spec, parseConfigGenerationSpec(kConfig));
}

TEST(ConfigGenerationSpecUtilsTest, RejectsInvalidConfigs) {
  const std::vector<std::string_view> invalidConfigs{
      R"({"serviceToOptions":{}})",
      R"({"serviceToOptions":{"AGENT":[]}})",
      R"({"serviceToOptions":{"AGENT":[{"profile":"DEFAULT","variant":""}]}})",
      R"({"serviceToOptions":{"AGENT":[{"profile":"DEFAULT","asicConfigVariant":""}]}})",
      R"({"serviceToOptions":{"AGENT":[{"profile":"DEFAULT","linkTopologyVariant":""}]}})",
      R"({"serviceToOptions":{"AGENT":[{"profile":"DEFAULT"},{"profile":"DEFAULT"}]}})",
      R"({"serviceToOptions":{"AGENT":[{"profile":"DEFAULT","unknown":true}]}})",
      R"({"platform":"PLATFORM_MONTBLANC","serviceToOptions":{"AGENT":[{"profile":"DEFAULT"}]}})",
      R"({"serviceToOptions":{"NOT_A_SERVICE":[{"profile":"DEFAULT"}]}})",
      R"({"serviceToOptions":{"AGENT":[{"profile":"NOT_A_PROFILE"}]}})",
  };

  for (const auto config : invalidConfigs) {
    EXPECT_THROW(parseConfigGenerationSpec(config), std::exception) << config;
  }
}

TEST(ConfigGenerationSpecUtilsTest, RejectsUnknownPlatformDirectory) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const fs::path platformDirectory =
      fs::path(temporaryDirectory.path().string()) / "not_a_platform";
  writeTestFile(
      platformDirectory / "forwarding_stack" / "config_generation.json",
      kConfig);

  EXPECT_THROW(loadConfigGenerationTarget(platformDirectory), std::exception);
}

} // namespace
} // namespace facebook::fboss::configgen
