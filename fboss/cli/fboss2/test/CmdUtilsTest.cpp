// Copyright (c) Meta Platforms, Inc. and affiliates.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include <fboss/cli/fboss2/utils/CmdUtilsCommon.h>
#include <gtest/gtest.h>
#include <cstdlib>

using namespace ::testing;

namespace facebook::fboss {
namespace {

TEST(CmdUtilsTest, FormatUsageLine) {
  utils::CmdLogInfo info;
  info.CmdName = "show port";
  info.Duration = "1s";
  info.Arguments = "eth1/1/1";
  info.UserInfo = "alice";
  info.ExitStatus = "SUCCESS";
  const std::string expected =
      "user=\"alice\" cmd=\"show port\" args=\"eth1/1/1\" status=\"SUCCESS\" duration=\"1s\"";
  EXPECT_EQ(utils::formatUsageLine(info), expected);
}

TEST(CmdUtilsTest, FormatUsageLineEmpty) {
  utils::CmdLogInfo info;
  const std::string expected =
      "user=\"\" cmd=\"\" args=\"\" status=\"\" duration=\"\"";
  EXPECT_EQ(utils::formatUsageLine(info), expected);
}

TEST(CmdUtilsTest, FormatUsageLineEscapesHostileArgs) {
  utils::CmdLogInfo info;
  info.CmdName = "show port";
  info.Duration = "1s";
  info.Arguments = "x\"\nstatus=FORGED a\\b";
  info.UserInfo = "alice";
  info.ExitStatus = "SUCCESS";
  const std::string expected =
      "user=\"alice\" cmd=\"show port\" args=\"x\\\"\\nstatus=FORGED a\\\\b\" "
      "status=\"SUCCESS\" duration=\"1s\"";
  const std::string line = utils::formatUsageLine(info);
  EXPECT_EQ(line, expected);
  // The record must stay a single syslog line no matter the input.
  EXPECT_EQ(line.find('\n'), std::string::npos);
  EXPECT_EQ(line.find('\r'), std::string::npos);
}

#ifndef IS_OSS
TEST(CmdUtilsTest, InternalUserInfoKeepsEnvBehavior) {
  // The internal build still attests FB_UID + USER from the environment;
  // this pins the verbatim move out of CmdUtilsCommon.cpp. OSS builds map
  // the effective uid through passwd instead, so this runs internal-only.
  setenv("FB_UID", "424242", 1);
  setenv("USER", "bob", 1);
  EXPECT_EQ(utils::getUserInfo(), "424242 | bob");
  unsetenv("FB_UID");
  unsetenv("USER");
}
#endif

} // namespace
} // namespace facebook::fboss
