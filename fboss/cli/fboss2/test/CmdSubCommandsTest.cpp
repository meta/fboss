// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <CLI/CLI.hpp>

#include "fboss/cli/fboss2/CmdArgsLists.h"
#include "fboss/cli/fboss2/CmdList.h"
#include "fboss/cli/fboss2/CmdSubcommands.h"
#include "fboss/cli/fboss2/utils/CLIParserUtils.h"

namespace facebook::fboss {
namespace {

class CmdSubCommandsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CmdArgsLists::getInstance()->clear();
    app_ = std::make_unique<CLI::App>("Test CLI");
  }

  void TearDown() override {
    CmdArgsLists::getInstance()->clear();
  }

  std::unique_ptr<CLI::App> app_;
};

TEST_F(CmdSubCommandsTest, InitWithEmptyCommandTrees) {
  CommandTree emptyTree;
  std::vector<Command> emptySpecialCmds;

  // Should not throw when initialized with empty trees
  EXPECT_NO_THROW(
      CmdSubcommands::getInstance()->init(
          *app_, emptyTree, emptyTree, emptySpecialCmds));

  // Verify that supported verbs are registered
  EXPECT_NE(utils::getSubcommandIf(*app_, "show"), nullptr);
  EXPECT_NE(utils::getSubcommandIf(*app_, "clear"), nullptr);
  EXPECT_NE(utils::getSubcommandIf(*app_, "create"), nullptr);
  EXPECT_NE(utils::getSubcommandIf(*app_, "delete"), nullptr);
  EXPECT_NE(utils::getSubcommandIf(*app_, "set"), nullptr);
}

TEST_F(CmdSubCommandsTest, InitWithValidCommandTree) {
  CommandTree cmdTree = {
      RootCommand(
          "show",
          "testObject",
          "Test object help",
          []() {},
          []() { return utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE; }),
  };
  CommandTree emptyTree;
  std::vector<Command> emptySpecialCmds;

  EXPECT_NO_THROW(
      CmdSubcommands::getInstance()->init(
          *app_, cmdTree, emptyTree, emptySpecialCmds));

  // Verify that the command was added under the verb
  auto* showCmd = utils::getSubcommandIf(*app_, "show");
  ASSERT_NE(showCmd, nullptr);
  EXPECT_NE(utils::getSubcommandIf(*showCmd, "testObject"), nullptr);
}

TEST_F(CmdSubCommandsTest, InitWithNestedCommands) {
  CommandTree cmdTree = {
      RootCommand(
          "show",
          "parent",
          "Parent command help",
          {Command(
              "child",
              "Child command help",
              []() {},
              []() {
                return utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
              })}),
  };
  CommandTree emptyTree;
  std::vector<Command> emptySpecialCmds;

  EXPECT_NO_THROW(
      CmdSubcommands::getInstance()->init(
          *app_, cmdTree, emptyTree, emptySpecialCmds));

  // Verify nested command structure
  auto* showCmd = utils::getSubcommandIf(*app_, "show");
  ASSERT_NE(showCmd, nullptr);
  auto* parentCmd = utils::getSubcommandIf(*showCmd, "parent");
  ASSERT_NE(parentCmd, nullptr);
  EXPECT_NE(utils::getSubcommandIf(*parentCmd, "child"), nullptr);
}

TEST_F(CmdSubCommandsTest, ThrowsOnUnsupportedVerb) {
  CommandTree cmdTree = {
      RootCommand(
          "unsupported_verb",
          "testObject",
          "Test object help",
          []() {},
          []() { return utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE; }),
  };
  CommandTree emptyTree;
  std::vector<Command> emptySpecialCmds;

  EXPECT_THROW(
      CmdSubcommands::getInstance()->init(
          *app_, cmdTree, emptyTree, emptySpecialCmds),
      std::runtime_error);
}

TEST_F(CmdSubCommandsTest, MergesSubcommandsFromDuplicateCommand) {
  // When the same object name appears in both trees, subcommands from the
  // second tree are merged into the existing command branch. This allows
  // different trees to contribute leaf commands under the same parent
  // (e.g., "delete config" can have OSS and facebook-only subcommands).
  CommandTree cmdTree = {
      RootCommand(
          "delete",
          "config",
          "Delete config objects",
          []() {},
          []() { return utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE; },
          {Command(
              "child1",
              "First child help",
              []() {},
              []() {
                return utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
              })}),
  };

  CommandTree additionalTree = {
      RootCommand(
          "delete",
          "config",
          "Delete config objects",
          []() {},
          []() { return utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE; },
          {Command(
              "child2",
              "Second child help",
              []() {},
              []() {
                return utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
              })}),
  };

  std::vector<Command> emptySpecialCmds;

  EXPECT_NO_THROW(
      CmdSubcommands::getInstance()->init(
          *app_, cmdTree, additionalTree, emptySpecialCmds));

  // Verify both children exist under the merged "config" command
  auto* deleteCmd = utils::getSubcommandIf(*app_, "delete");
  ASSERT_NE(deleteCmd, nullptr);
  auto* configCmd = utils::getSubcommandIf(*deleteCmd, "config");
  ASSERT_NE(configCmd, nullptr);
  EXPECT_NE(utils::getSubcommandIf(*configCmd, "child1"), nullptr);
  EXPECT_NE(utils::getSubcommandIf(*configCmd, "child2"), nullptr);
}

TEST_F(CmdSubCommandsTest, MergeThrowsOnDuplicateLeafCommand) {
  // When two trees contribute the same leaf command (no children) under a
  // merged parent, it means the exact same path exists in both trees.
  // This is a conflict and must be rejected.
  CommandTree cmdTree = {
      RootCommand(
          "delete",
          "config",
          "Delete config objects",
          []() {},
          []() { return utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE; },
          {Command(
              "samechild",
              "First help",
              []() {},
              []() {
                return utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
              })}),
  };

  CommandTree additionalTree = {
      RootCommand(
          "delete",
          "config",
          "Delete config objects",
          []() {},
          []() { return utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE; },
          {Command(
              "samechild",
              "Duplicate help",
              []() {},
              []() {
                return utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
              })}),
  };

  std::vector<Command> emptySpecialCmds;

  try {
    CmdSubcommands::getInstance()->init(
        *app_, cmdTree, additionalTree, emptySpecialCmds);
    FAIL() << "Expected std::runtime_error to be thrown";
  } catch (const std::runtime_error& e) {
    EXPECT_THAT(
        std::string(e.what()),
        ::testing::HasSubstr("Command `samechild` already exists"));
  }
}

TEST_F(CmdSubCommandsTest, MergesNestedSubcommands) {
  // Verify merging works at deeper nesting levels — two trees contribute
  // different grandchildren under the same parent>child path.
  CommandTree cmdTree = {
      RootCommand(
          "show",
          "parent",
          "Parent help",
          {Command(
              "child",
              "Child help",
              {Command(
                  "grandchild1",
                  "Grandchild1 help",
                  []() {},
                  []() {
                    return utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
                  })})}),
  };

  CommandTree additionalTree = {
      RootCommand(
          "show",
          "parent",
          "Parent help",
          {Command(
              "child",
              "Child help",
              {Command(
                  "grandchild2",
                  "Grandchild2 help",
                  []() {},
                  []() {
                    return utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
                  })})}),
  };

  std::vector<Command> emptySpecialCmds;

  EXPECT_NO_THROW(
      CmdSubcommands::getInstance()->init(
          *app_, cmdTree, additionalTree, emptySpecialCmds));

  auto* showCmd = utils::getSubcommandIf(*app_, "show");
  ASSERT_NE(showCmd, nullptr);
  auto* parentCmd = utils::getSubcommandIf(*showCmd, "parent");
  ASSERT_NE(parentCmd, nullptr);
  auto* childCmd = utils::getSubcommandIf(*parentCmd, "child");
  ASSERT_NE(childCmd, nullptr);
  EXPECT_NE(utils::getSubcommandIf(*childCmd, "grandchild1"), nullptr);
  EXPECT_NE(utils::getSubcommandIf(*childCmd, "grandchild2"), nullptr);
}

TEST_F(CmdSubCommandsTest, ThrowsOnDuplicateNestedCommand) {
  // Duplicate leaf commands within the same tree are rejected — the exact
  // same path from root to leaf exists twice, which is a conflict.
  CommandTree cmdTree = {
      RootCommand(
          "show",
          "parent",
          "Parent command help",
          {Command(
               "child",
               "First child help",
               []() {},
               []() {
                 return utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
               }),
           Command(
               "child",
               "Duplicate child help",
               []() {},
               []() {
                 return utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
               })}),
  };
  CommandTree emptyTree;
  std::vector<Command> emptySpecialCmds;

  try {
    CmdSubcommands::getInstance()->init(
        *app_, cmdTree, emptyTree, emptySpecialCmds);
    FAIL() << "Expected std::runtime_error to be thrown";
  } catch (const std::runtime_error& e) {
    EXPECT_THAT(
        std::string(e.what()),
        ::testing::HasSubstr("Command `child` already exists"));
  }
}

TEST_F(CmdSubCommandsTest, InitWithSpecialCommands) {
  // This test covers lines 285-288 in CmdSubcommands.cpp:init()
  // Special commands are added directly to the root CLI app, bypassing the
  // verb structure (e.g., "show", "clear"). This allows commands like "help"
  // that don't follow the typical "<verb> <object>" pattern.
  //
  // Regular commands: app -> verb -> object (e.g., "fboss2 show port")
  // Special commands: app -> command directly (e.g., "fboss2 help")
  //
  // See kSpecialCommands() in CmdList.cpp for actual special commands.
  CommandTree emptyTree;
  std::vector<Command> specialCmds = {
      Command(
          "special",
          "Special command help",
          []() {},
          []() { return utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE; }),
  };

  EXPECT_NO_THROW(
      CmdSubcommands::getInstance()->init(
          *app_, emptyTree, emptyTree, specialCmds));

  // Verify that special command was added directly to app (not under a verb)
  EXPECT_NE(utils::getSubcommandIf(*app_, "special"), nullptr);
}

TEST_F(CmdSubCommandsTest, InitWithMultipleVerbs) {
  CommandTree cmdTree = {
      RootCommand(
          "show",
          "object1",
          "Show object1 help",
          []() {},
          []() { return utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE; }),
      RootCommand(
          "clear",
          "object2",
          "Clear object2 help",
          []() {},
          []() { return utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE; }),
  };
  CommandTree emptyTree;
  std::vector<Command> emptySpecialCmds;

  EXPECT_NO_THROW(
      CmdSubcommands::getInstance()->init(
          *app_, cmdTree, emptyTree, emptySpecialCmds));

  // Verify commands under different verbs
  auto* showCmd = utils::getSubcommandIf(*app_, "show");
  ASSERT_NE(showCmd, nullptr);
  EXPECT_NE(utils::getSubcommandIf(*showCmd, "object1"), nullptr);

  auto* clearCmd = utils::getSubcommandIf(*app_, "clear");
  ASSERT_NE(clearCmd, nullptr);
  EXPECT_NE(utils::getSubcommandIf(*clearCmd, "object2"), nullptr);
}

TEST_F(CmdSubCommandsTest, ValidatesFsdbPathDuringParsing) {
  CommandTree cmdTree = {RootCommand(
      "show",
      "fsdb",
      "Show FSDB information",
      {Command(
          "state",
          "Show FSDB operational state",
          []() {},
          []() {
            return utils::ObjectArgTypeId::OBJECT_ARG_TYPE_FSDB_PATH;
          })})};
  CommandTree emptyTree;
  std::vector<Command> emptySpecialCmds;

  auto parse = [&](std::vector<std::string> args) {
    CmdArgsLists::getInstance()->clear();
    app_ = std::make_unique<CLI::App>("Test CLI");
    CmdSubcommands::getInstance()->init(
        *app_, cmdTree, emptyTree, emptySpecialCmds);

    std::vector<char*> argv;
    for (auto& arg : args) {
      argv.push_back(arg.data());
    }
    app_->parse(static_cast<int>(argv.size()), argv.data());
  };

  EXPECT_THROW(parse({"fboss2", "show", "fsdb", "state"}), CLI::RequiredError);
  EXPECT_THROW(
      parse({"fboss2", "show", "fsdb", "state", "/"}), CLI::ValidationError);
  EXPECT_THROW(
      parse({"fboss2", "show", "fsdb", "state", "///"}), CLI::ValidationError);
  EXPECT_THROW(
      parse({"fboss2", "show", "fsdb", "state", "agent"}),
      CLI::ValidationError);
  EXPECT_NO_THROW(parse({"fboss2", "show", "fsdb", "state", "/agent"}));
}

TEST_F(CmdSubCommandsTest, RegistersLocalOptionsFromRealCommandTree) {
  /*
   * A command's LocalOptions only reach CLI11 if its entry in the command
   * tree also passes localOptionsHandler<Traits>. Declaring them in the
   * traits struct alone leaves the flag unparseable at the CLI, which no
   * per-command test catches because those set the option value directly on
   * CmdLocalOptions. Guard one real command that carries a local option.
   */
  std::vector<Command> emptySpecialCmds;

  CmdSubcommands::getInstance()->init(
      *app_, kCommandTree(), kAdditionalCommandTree(), emptySpecialCmds);

  auto* showCmd = utils::getSubcommandIf(*app_, "show");
  ASSERT_NE(showCmd, nullptr);
  auto* bgpCmd = utils::getSubcommandIf(*showCmd, "bgp");
  ASSERT_NE(bgpCmd, nullptr);
  auto* summaryCmd = utils::getSubcommandIf(*bgpCmd, "summary");
  ASSERT_NE(summaryCmd, nullptr);
  EXPECT_NE(summaryCmd->get_option_no_throw("--sort-by"), nullptr);
}

} // namespace
} // namespace facebook::fboss
