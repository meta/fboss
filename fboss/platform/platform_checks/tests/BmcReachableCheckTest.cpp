/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/checks/BmcReachableCheck.h"

#include <gtest/gtest.h>

#include "fboss/platform/platform_checks/tests/MockHost.h"

using namespace facebook::fboss::platform::platform_checks;

TEST(BmcReachableCheckTest, ConnectedBmcPasses) {
  BmcReachableCheck check(
      CheckTarget{.host = std::make_shared<MockHost>("bmc")}, std::nullopt);

  EXPECT_EQ(check.getSkipReason(), std::nullopt);
  EXPECT_EQ(*check.run().status(), CheckStatus::OK);
}

TEST(BmcReachableCheckTest, UnreachableBmcIsProblem) {
  BmcReachableCheck check(
      CheckTarget{.host = nullptr, .unavailableReason = "see BMC Reachable"},
      "Cannot connect to rsw1-oob: Connection refused");

  auto result = check.run();

  EXPECT_EQ(check.getSkipReason(), std::nullopt);
  EXPECT_EQ(*result.status(), CheckStatus::PROBLEM);
  EXPECT_EQ(
      *result.errorMessage(), "Cannot connect to rsw1-oob: Connection refused");
}

TEST(BmcReachableCheckTest, NoBmcIsSkipped) {
  BmcReachableCheck check(
      CheckTarget{
          .host = nullptr,
          .unavailableReason = "BMC checks disabled with --no-bmc"},
      std::nullopt);

  EXPECT_EQ(check.getSkipReason(), "BMC checks disabled with --no-bmc");
}
