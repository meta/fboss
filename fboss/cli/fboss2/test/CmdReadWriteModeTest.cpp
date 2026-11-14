// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <fmt/format.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "fboss/cli/fboss2/CmdList.h"

namespace facebook::fboss {

namespace {

void collectViolations(
    const Command& cmd,
    const std::string& verb,
    const std::string& parentPath,
    std::vector<std::string>& violations) {
  const auto path = fmt::format("{} {}", parentPath, cmd.name);
  const auto expected = verb == "show" ? CliReadWriteMode::CLI_MODE_READ
                                       : CliReadWriteMode::CLI_MODE_WRITE;
  if (cmd.commandHandler && cmd.commandHandler->mode() != expected) {
    violations.push_back(path);
  }
  for (const auto& child : cmd.subcommands) {
    collectViolations(child, verb, path, violations);
  }
}

std::vector<std::string> readWriteModeViolations(const CommandTree& tree) {
  std::vector<std::string> violations;
  for (const auto& root : tree) {
    collectViolations(root, root.verb, root.verb, violations);
  }
  return violations;
}

} // namespace

TEST(CmdReadWriteModeTest, ShowCommandsAreReadOtherVerbsAreWrite) {
  EXPECT_EQ(
      readWriteModeViolations(kCommandTree()), std::vector<std::string>{});
  EXPECT_EQ(
      readWriteModeViolations(kAdditionalCommandTree()),
      std::vector<std::string>{});
}

} // namespace facebook::fboss
