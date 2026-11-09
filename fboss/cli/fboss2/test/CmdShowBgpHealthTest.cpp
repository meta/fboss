/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sstream>

#include "fboss/cli/fboss2/commands/show/bgp/health/CmdShowBgpHealth.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdShowBgpHealthTestFixture : public CmdHandlerTestBase {};

TEST_F(CmdShowBgpHealthTestFixture, wikiDocHooks) {
  const auto description = CmdShowBgpHealthTraits::description();
  EXPECT_FALSE(description.empty());
  // The four statuses are the thing the prose has to explain; a description
  // that has lost them is not doing its job.
  for (const auto& status : {"PASS", "WARN", "FAIL", "SKIP"}) {
    EXPECT_THAT(std::string(description), HasSubstr(status));
  }
  EXPECT_FALSE(CmdShowBgpHealth::sampleModel().modules()->empty());
}

// The header tally is derived from the checks, so this pins that the derivation
// stays right rather than that two hardcoded numbers were kept in step.
// UNKNOWN has no counter in THealthReport, so a check in that state would be
// listed in a module table but missing from the header total.
TEST_F(CmdShowBgpHealthTestFixture, sampleModelHasNoUncountableChecks) {
  const auto model = CmdShowBgpHealth::sampleModel();
  for (const auto& mod : *model.modules()) {
    for (const auto& result : *mod.checks()) {
      EXPECT_NE(
          *result.status(),
          facebook::neteng::fboss::bgp::thrift::HealthCheckStatus::UNKNOWN);
    }
  }
}

// Each module header must reflect the worst status among its own rows.
TEST_F(CmdShowBgpHealthTestFixture, moduleStatusMatchesItsChecks) {
  using facebook::neteng::fboss::bgp::thrift::HealthCheckStatus;
  const auto model = CmdShowBgpHealth::sampleModel();
  for (const auto& mod : *model.modules()) {
    bool hasWarn = false;
    bool hasFail = false;
    for (const auto& result : *mod.checks()) {
      hasWarn |= *result.status() == HealthCheckStatus::WARN;
      hasFail |= *result.status() == HealthCheckStatus::FAIL;
    }
    const auto expected = hasFail ? HealthCheckStatus::FAIL
        : hasWarn                 ? HealthCheckStatus::WARN
                                  : HealthCheckStatus::PASS;
    EXPECT_EQ(*mod.overallStatus(), expected);
  }
}

TEST_F(CmdShowBgpHealthTestFixture, sampleModelCountsMatchListedChecks) {
  const auto model = CmdShowBgpHealth::sampleModel();
  size_t listedChecks = 0;
  for (const auto& mod : *model.modules()) {
    listedChecks += mod.checks()->size();
  }
  const size_t tally = *model.passCount() + *model.warnCount() +
      *model.failCount() + *model.skipCount();
  EXPECT_EQ(listedChecks, tally);
}

TEST_F(CmdShowBgpHealthTestFixture, printOutputRendersSample) {
  std::stringstream ss;
  CmdShowBgpHealth().printOutput(CmdShowBgpHealth::sampleModel(), ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("BGP++ Health Report"));
  EXPECT_THAT(
      output, HasSubstr("Checks: 9 total | 7 PASS | 1 WARN | 0 FAIL | 1 SKIP"));
  EXPECT_THAT(output, HasSubstr("--- Global: System (PASS) ---"));
  // The example covers three of the four status renders, and a module whose
  // own status is degraded by one of its checks.
  EXPECT_THAT(output, HasSubstr("[PASS]"));
  EXPECT_THAT(output, HasSubstr("[SKIPPED]"));
  EXPECT_THAT(output, HasSubstr("[WARN]"));
  EXPECT_THAT(output, HasSubstr("--- Peer Manager (WARN) ---"));
  EXPECT_THAT(output, HasSubstr("Overall: WARN"));
}

} // namespace facebook::fboss
