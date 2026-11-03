/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/checks/SolConsoleCheck.h"

#include <filesystem>

#include <folly/FileUtil.h>
#include <folly/testing/TestUtil.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/platform/platform_checks/tests/MockHost.h"

using namespace facebook::fboss::platform;
using namespace facebook::fboss::platform::platform_checks;
using ::testing::ByMove;
using ::testing::Return;

namespace {

// A BMC shell whose PATH has a fake sol.sh behaving like the real one: input
// sent before it is attached is lost, it prints its banner, the console
// answers the first key press with `consoleOutput`, and Ctrl-L x exits.
class FakeBmcShell {
 public:
  explicit FakeBmcShell(const std::string& consoleOutput) {
    const std::filesystem::path dir{tmpDir_.path().string()};
    auto solSh = dir / "sol.sh";
    folly::writeFile(
        "#!/bin/sh\n"
        "stty raw -echo\n"
        "timeout --foreground 0.3 cat >/dev/null\n"
        "printf '\\r\\n  CTRL-l x : Terminate the connection.\\r\\n'\n"
        "dd bs=1 count=1 >/dev/null 2>&1\n"
        "printf '" +
            consoleOutput +
            "'\n"
            "dd bs=1 count=2 >/dev/null 2>&1\n"
            "stty sane\n",
        solSh.c_str());
    std::filesystem::permissions(solSh, std::filesystem::perms::owner_all);
    command_ = "env PATH=" + dir.string() + ":/bin:/usr/bin PS1='# ' sh -i";
  }

  std::unique_ptr<ExpectSession> spawn() const {
    return std::make_unique<ExpectSession>(command_);
  }

 private:
  folly::test::TemporaryDirectory tmpDir_;
  std::string command_;
};

} // namespace

TEST(SolConsoleCheckTest, LoginPromptPasses) {
  FakeBmcShell shell("\\r\\nrsw1 login: ");
  auto bmc = std::make_shared<MockHost>("bmc");
  EXPECT_CALL(*bmc, spawnInteractive()).WillOnce(Return(ByMove(shell.spawn())));

  auto result = SolConsoleCheck(CheckTarget{.host = bmc}).run();

  EXPECT_EQ(*result.status(), CheckStatus::OK);
}

TEST(SolConsoleCheckTest, NoLoginPromptIsProblem) {
  FakeBmcShell shell("\\r\\n");
  auto bmc = std::make_shared<MockHost>("bmc");
  EXPECT_CALL(*bmc, spawnInteractive()).WillOnce(Return(ByMove(shell.spawn())));

  auto result =
      SolConsoleCheck(CheckTarget{.host = bmc}, std::chrono::seconds(1)).run();

  EXPECT_EQ(*result.status(), CheckStatus::PROBLEM);
}
