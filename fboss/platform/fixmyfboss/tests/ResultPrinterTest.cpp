/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/fixmyfboss/ResultPrinter.h"

#include <sstream>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace facebook::fboss::platform;
using namespace facebook::fboss::platform::platform_checks;
using ::testing::HasSubstr;
using ::testing::Not;

namespace {

CheckResult makeResult(
    const std::string& name,
    CheckStatus status,
    std::optional<std::string> errorMessage = std::nullopt,
    std::optional<std::string> details = std::nullopt) {
  CheckResult result;
  result.checkName() = name;
  result.status() = status;
  if (errorMessage) {
    result.errorMessage() = *errorMessage;
  }
  if (details) {
    result.details() = *details;
  }
  return result;
}

} // namespace

TEST(ResultPrinterTest, SummaryListsSkipReasons) {
  std::ostringstream out;
  const std::vector<CheckResult> results{
      makeResult("A", CheckStatus::OK),
      makeResult("B", CheckStatus::SKIPPED, "no BMC host"),
  };

  fixmyfboss::ResultPrinter(out).printSummary(results);

  EXPECT_THAT(out.str(), HasSubstr("Passed: 1"));
  EXPECT_THAT(out.str(), HasSubstr("Skipped: 1"));
  EXPECT_THAT(out.str(), HasSubstr("B (no BMC host)"));
}

TEST(ResultPrinterTest, DetailsOnlyWhenRequested) {
  const std::vector<CheckResult> results{
      makeResult("A", CheckStatus::PROBLEM, "broken", "command output"),
  };
  std::ostringstream terse;
  std::ostringstream verbose;

  fixmyfboss::ResultPrinter(terse).printDetails(results);
  fixmyfboss::ResultPrinter(verbose, true).printDetails(results);

  EXPECT_THAT(terse.str(), HasSubstr("broken"));
  EXPECT_THAT(terse.str(), Not(HasSubstr("command output")));
  EXPECT_THAT(verbose.str(), HasSubstr("command output"));
}

TEST(ResultPrinterTest, NoColorPrintsPlainText) {
  const std::vector<CheckResult> results{
      makeResult("A", CheckStatus::PROBLEM, "broken"),
  };
  std::ostringstream out;

  fixmyfboss::ResultPrinter printer(out, false, /*color=*/false);
  printer.printSummary(results);
  printer.printDetails(results);

  EXPECT_THAT(out.str(), HasSubstr("[  PROBLEM  ] A"));
  EXPECT_THAT(out.str(), Not(HasSubstr("\033")));
}
