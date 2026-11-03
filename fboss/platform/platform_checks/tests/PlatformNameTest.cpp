/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/PlatformName.h"

#include <gtest/gtest.h>

#include "fboss/platform/platform_checks/tests/MockHost.h"

using namespace facebook::fboss::platform::platform_checks;
using ::testing::_;
using ::testing::Return;

TEST(PlatformNameTest, PrefersCachedName) {
  MockHost host;
  EXPECT_CALL(
      host,
      readFile(std::filesystem::path("/var/facebook/fboss/platform_name")))
      .WillOnce(Return("MONTBLANC\n"));
  EXPECT_CALL(host, run(_, _)).Times(0);

  EXPECT_EQ(getPlatformName(host), "MONTBLANC");
}

TEST(PlatformNameTest, FallsBackToSanitizedDmidecode) {
  MockHost host;
  EXPECT_CALL(host, readFile(_)).WillOnce(Return(std::nullopt));
  EXPECT_CALL(host, run("dmidecode -s system-product-name", _))
      .WillOnce(
          Return(CommandResult{.exitCode = 0, .standardOut = "minipack3\n"}));

  EXPECT_EQ(getPlatformName(host), "MONTBLANC");
}

TEST(PlatformNameTest, DmidecodeFailure) {
  MockHost host;
  EXPECT_CALL(host, readFile(_)).WillOnce(Return(std::nullopt));
  EXPECT_CALL(host, run(_, _)).WillOnce(Return(CommandResult{.exitCode = 1}));

  EXPECT_EQ(getPlatformName(host), std::nullopt);
}

TEST(PlatformNameTest, EmptyDmidecodeOutputIsFailure) {
  MockHost host;
  EXPECT_CALL(host, readFile(_)).WillOnce(Return(std::nullopt));
  EXPECT_CALL(host, run(_, _))
      .WillOnce(Return(CommandResult{.exitCode = 0, .standardOut = " \n"}));

  EXPECT_EQ(getPlatformName(host), std::nullopt);
}
