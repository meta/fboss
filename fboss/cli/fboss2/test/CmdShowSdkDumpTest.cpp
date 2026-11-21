// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "fboss/cli/fboss2/commands/show/sdk/dump/CmdShowSdkDump.h"

using namespace ::testing;

namespace facebook::fboss {

// getHwDebugDump can succeed but return no SDK state (e.g. Broadcom SAI may
// produce an empty dump). These tests verify CmdShowAgentSdkDump::printOutput
// surfaces the empty case to the user instead of silently printing nothing

TEST(CmdShowAgentSdkDumpTest, emptyPayloadWarns) {
  CmdShowAgentSdkDump cmd;
  std::stringstream ss;
  cmd.printOutput(std::string(""), ss);
  auto out = ss.str();
  EXPECT_THAT(out, HasSubstr("Printing Agent SDK state:"));
  EXPECT_THAT(out, HasSubstr("Warning: no SDK state was captured."));
  EXPECT_THAT(out, Not(HasSubstr("for switch")));
}

TEST(CmdShowAgentSdkDumpTest, multiSwitchAllEmptyWarns) {
  CmdShowAgentSdkDump cmd;
  std::stringstream ss;
  cmd.printOutput(std::string(R"({"0": ""})"), ss);
  auto out = ss.str();
  EXPECT_THAT(out, HasSubstr("no SDK state was captured for switch 0."));
  EXPECT_THAT(out, HasSubstr("no SDK state was captured for any switch."));
}

TEST(CmdShowAgentSdkDumpTest, multiSwitchMixedWarnsOnlyEmpty) {
  CmdShowAgentSdkDump cmd;
  std::stringstream ss;
  cmd.printOutput(std::string(R"({"0": "somedata", "1": ""})"), ss);
  auto out = ss.str();
  EXPECT_THAT(out, HasSubstr("no SDK state was captured for switch 1."));
  EXPECT_THAT(out, Not(HasSubstr("for switch 0.")));
  EXPECT_THAT(out, Not(HasSubstr("for any switch.")));
}

TEST(CmdShowAgentSdkDumpTest, nonEmptyPayloadNoWarning) {
  CmdShowAgentSdkDump cmd;
  std::stringstream ss;
  cmd.printOutput(std::string(R"({"0": "real dump"})"), ss);
  auto out = ss.str();
  EXPECT_THAT(out, HasSubstr("real dump"));
  EXPECT_THAT(out, Not(HasSubstr("Warning:")));
}

TEST(CmdShowAgentSdkDumpTest, monolithicNonJsonNoWarning) {
  CmdShowAgentSdkDump cmd;
  std::stringstream ss;
  cmd.printOutput(std::string("raw non-json dump text"), ss);
  auto out = ss.str();
  EXPECT_THAT(out, HasSubstr("raw non-json dump text"));
  EXPECT_THAT(out, Not(HasSubstr("Warning:")));
}

} // namespace facebook::fboss
