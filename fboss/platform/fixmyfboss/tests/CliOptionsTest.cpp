/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/fixmyfboss/CliOptions.h"

#include <gtest/gtest.h>

using namespace facebook::fboss::platform::fixmyfboss;

namespace {

CliOptions parse(std::vector<std::string> args) {
  CLI::App app;
  CliOptions options;
  addOptions(app, options);
  std::reverse(args.begin(), args.end());
  app.parse(args);
  return options;
}

} // namespace

TEST(CliOptionsTest, DefaultsDiagnoseLocalMachine) {
  auto options = parse({});

  EXPECT_EQ(options.connect.hostname, std::nullopt);
  EXPECT_EQ(options.run.nameFilter, std::nullopt);
  EXPECT_FALSE(options.run.includeDisruptive);
  EXPECT_FALSE(options.connect.noBmc);
}

TEST(CliOptionsTest, RemoteOptions) {
  auto options = parse(
      {"--hostname",
       "rsw1",
       "--bmc-hostname",
       "rsw1-bmc",
       "--checks",
       "bmc",
       "--include-disruptive"});

  EXPECT_EQ(options.connect.hostname, "rsw1");
  EXPECT_EQ(options.connect.bmcHostname, "rsw1-bmc");
  EXPECT_EQ(options.run.nameFilter, "bmc");
  EXPECT_TRUE(options.run.includeDisruptive);
}

TEST(CliOptionsTest, NoBmcExcludesBmcHostname) {
  EXPECT_THROW(
      parse({"--no-bmc", "--bmc-hostname", "rsw1-bmc"}), CLI::ParseError);
}

TEST(CliOptionsTest, BmcPlatformDefaultsToLowercasePlatform) {
  CheckEnvironment env{.platformName = "MONTBLANC"};

  applyToEnvironment(parse({}), env);

  EXPECT_EQ(env.bmcPlatform, "montblanc");
}

TEST(CliOptionsTest, EmptyBmcPlatformDefaultsToLowercasePlatform) {
  CheckEnvironment env{.platformName = "MONTBLANC"};

  applyToEnvironment(parse({"--bmc-platform", ""}), env);

  EXPECT_EQ(env.bmcPlatform, "montblanc");
}

TEST(CliOptionsTest, ExplicitBmcPlatformIsUsed) {
  CheckEnvironment env{.platformName = "MONTBLANC"};

  applyToEnvironment(parse({"--bmc-platform", "tahan"}), env);

  EXPECT_EQ(env.bmcPlatform, "tahan");
}

TEST(CliOptionsTest, UnsetHwTestsDirKeepsEnvironmentValue) {
  CheckEnvironment env{
      .platformName = "MONTBLANC", .hwTestsDir = "/opt/hw_tests"};

  applyToEnvironment(parse({}), env);

  EXPECT_EQ(env.hwTestsDir, "/opt/hw_tests");
}
