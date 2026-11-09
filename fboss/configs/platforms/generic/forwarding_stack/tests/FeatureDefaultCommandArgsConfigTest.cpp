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
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "fboss/configs/platforms/generic/forwarding_stack/utils/FeatureDefaultCommandArgsUtils.h"

namespace facebook::fboss::configgen {
namespace {

TEST(FeatureDefaultCommandArgsConfigTest, ParsesAndResolvesConditions) {
  const auto config = parseFeatureDefaultCommandArgsConfig(
      R"({"profileDefaultArgs":{"default":{"profile_arg":"profile_value"}},"features":{"matching_feature":{"args":{"feature_arg":"feature_value"},"autoEnableWhen":[{"configProfiles":{"included":["default"]},"asicTypes":{"included":["ASIC_TYPE_TOMAHAWK5"]},"platforms":{"included":["PLATFORM_WEDGE800BACT"]}},{"configProfiles":{"included":["link_test"]},"platforms":{"included":["PLATFORM_MONTBLANC"]}}]},"excluded_feature":{"args":{"excluded_arg":"excluded_value"},"autoEnableWhen":[{"platforms":{"excluded":["PLATFORM_WEDGE800BACT"]}}]},"explicit_only":{"args":{"explicit_arg":"explicit_value"}}}})");

  EXPECT_EQ(
      config.profileDefaultArgs()->at("default").at("profile_arg"),
      "profile_value");
  EXPECT_TRUE(config.features()->contains("matching_feature"));

  const auto resolved = resolveFeatureDefaultCommandArgs(
      config,
      "default",
      cfg::AsicType::ASIC_TYPE_TOMAHAWK5,
      PlatformType::PLATFORM_WEDGE800BACT);
  const std::map<std::string, std::string> expected{
      {"feature_arg", "feature_value"},
      {"profile_arg", "profile_value"},
  };
  EXPECT_EQ(resolved, expected);

  const std::map<std::string, std::string> alternativeExpected{
      {"excluded_arg", "excluded_value"},
      {"feature_arg", "feature_value"},
  };
  EXPECT_EQ(
      resolveFeatureDefaultCommandArgs(
          config, "link_test", std::nullopt, PlatformType::PLATFORM_MONTBLANC),
      alternativeExpected);
}

TEST(FeatureDefaultCommandArgsConfigTest, CommentDoesNotAffectResolution) {
  const auto config = parseFeatureDefaultCommandArgsConfig(
      R"({"features":{"documented_feature":{"args":{"feature_arg":"feature_value"},"autoEnableWhen":[{"configProfiles":{"included":["link_test"]},"comment":"Explain this enablement decision."}]}}})");

  EXPECT_EQ(
      config.features()
          ->at("documented_feature")
          .autoEnableWhen()
          ->at(0)
          .comment()
          .value(),
      "Explain this enablement decision.");
  const std::map<std::string, std::string> expected{
      {"feature_arg", "feature_value"},
  };
  EXPECT_EQ(
      resolveFeatureDefaultCommandArgs(
          config, "link_test", std::nullopt, PlatformType::PLATFORM_MONTBLANC),
      expected);
}

TEST(FeatureDefaultCommandArgsConfigTest, RejectsInvalidConfigs) {
  const std::vector<std::string_view> invalidConfigs{
      R"({"profileDefaultArgs":[],"features":{}})",
      R"({"profileDefaultArgs":{},"features":{"feature":{"args":{"foo":"true"},"autoEnabledWhen":[{"configProfiles":{"included":["hw_test"]}}]}}})",
      R"({"profileDefaultArgs":{},"features":{"feature":{"args":{"foo":"true"},"autoEnableWhen":[]}}})",
      R"({"profileDefaultArgs":{},"features":{"feature":{"args":{"foo":"true"},"autoEnableWhen":[{}]}}})",
      R"({"features":{"feature":{"args":{"foo":"true"},"autoEnableWhen":[{"asicTypes":{"included":["TOMAHAWK5"]}}]}}})",
      R"({"features":{"feature":{"args":{"foo":"true"},"autoEnableWhen":[{"platforms":{"included":["wedge800bact"]}}]}}})",
  };

  for (const auto config : invalidConfigs) {
    EXPECT_THROW(parseFeatureDefaultCommandArgsConfig(config), std::exception)
        << config;
  }
}

} // namespace
} // namespace facebook::fboss::configgen
