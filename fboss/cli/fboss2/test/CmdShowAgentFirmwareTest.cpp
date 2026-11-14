// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>
#include <sstream>

#include "fboss/cli/fboss2/commands/show/agent/CmdShowAgentFirmware.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

namespace facebook::fboss {

class CmdShowAgentFirmwareTestFixture : public CmdHandlerTestBase {};

TEST_F(CmdShowAgentFirmwareTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowAgentFirmwareTraits::description().empty());
  EXPECT_FALSE(CmdShowAgentFirmware::sampleModel().firmwareEntries()->empty());
}

TEST_F(CmdShowAgentFirmwareTestFixture, printOutputEmptyList) {
  CmdShowAgentFirmware::RetType model;
  std::stringstream ss;
  CmdShowAgentFirmware().printOutput(model, ss);
  EXPECT_EQ(ss.str(), "No firmware information available\n");
}

TEST_F(CmdShowAgentFirmwareTestFixture, printOutputRendersSample) {
  std::stringstream ss;
  CmdShowAgentFirmware().printOutput(CmdShowAgentFirmware::sampleModel(), ss);
  EXPECT_EQ(
      ss.str(),
      "Version: 1.0.0-EA1\n"
      "Operational Status: RUNNING\n"
      "Functional Status: MONITORING\n");
}

} // namespace facebook::fboss
