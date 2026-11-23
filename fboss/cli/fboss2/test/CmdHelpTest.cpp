// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/help/CmdHelp.h"
#include <fboss/cli/fboss2/CmdList.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/cli/fboss2/utils/CmdClientUtils.h"

#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;

namespace facebook::fboss {
class CmdHelpTestFixture : public CmdHandlerTestBase {
 public:
  const CommandTree& cmdTree = kCommandTree();
  const CommandTree& addCmdTree = kAdditionalCommandTree();
  std::vector<CommandTree> cmdTrees = {cmdTree, addCmdTree};

  void SetUp() override {
    CmdHandlerTestBase::SetUp();
  }
};

TEST_F(CmdHelpTestFixture, OrdersSubcommandsAcrossCommandTrees) {
  const CommandTree base = {
      {"show", "object", "Show object", {{"zulu", "Zulu command", {}}}}};
  const CommandTree additional = {
      {"show", "object", "Show object", {{"alpha", "Alpha command", {}}}}};
  std::stringstream ss;

  CmdHelp({base, additional}).printHelpInfo("object", ss);

  const auto help = ss.str();
  ASSERT_NE(help.find("|---alpha:"), std::string::npos);
  ASSERT_NE(help.find("|---zulu:"), std::string::npos);
  EXPECT_LT(help.find("|---alpha:"), help.find("|---zulu:"));
}

TEST_F(CmdHelpTestFixture, MergesNestedHelpBranches) {
  const CommandTree base = {
      {"show",
       "object",
       "Show object",
       {{"parent",
         "Parent help",
         [] {},
         [] { return utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE; },
         [] { return std::vector<utils::LocalOption>{{"--mode", "Mode"}}; },
         {{"zulu", "Zulu command", {}}}}}}};
  const CommandTree additional = {
      {"show",
       "object",
       "Show object",
       {{"parent", "Parent help", {{"alpha", "Alpha command", {}}}}}}};
  std::stringstream ss;

  CmdHelp({base, additional}).printHelpInfo("object", ss);

  const auto help = ss.str();
  const auto parent = help.find("|---parent:");
  ASSERT_NE(parent, std::string::npos);
  EXPECT_EQ(help.find("|---parent:", parent + 1), std::string::npos);
  EXPECT_THAT(help, HasSubstr("--mode"));
  ASSERT_NE(help.find("|---alpha:"), std::string::npos);
  ASSERT_NE(help.find("|---zulu:"), std::string::npos);
  EXPECT_LT(help.find("|---alpha:"), help.find("|---zulu:"));
}

// Performs test for all commands and all cases.
TEST_F(CmdHelpTestFixture, CmdHelpTest) {
  std::stringstream ss;

  for (const auto& cmdTree : cmdTrees) {
    for (const auto& rootCmd : cmdTree) {
      const auto& objectName = rootCmd.name;
      CmdHelp(cmdTrees).printHelpInfo(objectName, ss);
      std::string output = ss.str();
      const auto& applicableVerb = rootCmd.verb;
      /* testing the first level help separately because the
      first level in the command tree is a rootCommand, but the
      rest of the levels have Command.
      */
      EXPECT_THAT(output, HasSubstr(applicableVerb));

      // testing all nested commands' help info
      /* testing logic for nested sub-commands:
        I am performing an iterative in-order traversal of the command tree
        to check that the help info for each subcommand is displayed.
        This is using a stack.
      */
      for (const auto& subCommand : rootCmd.subcommands) {
        std::stack<Command> subCmdStack;
        subCmdStack.push(subCommand);
        while (!subCmdStack.empty()) {
          auto subCmd = subCmdStack.top();
          subCmdStack.pop();
          const auto& name = subCmd.name;
          EXPECT_THAT(output, HasSubstr(name));
          for (const auto& nestedSubCmds : subCmd.subcommands) {
            subCmdStack.push(nestedSubCmds);
          }
        }
      }
    }
  }
}

} // namespace facebook::fboss
