/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/checks/BmcChecks.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/platform/platform_checks/tests/MockHost.h"

using namespace facebook::fboss::platform::platform_checks;
using ::testing::_;
using ::testing::Return;

namespace {

CommandResult ok(const std::string& out) {
  return CommandResult{.exitCode = 0, .standardOut = out};
}

constexpr auto kWeutilAll =
    "Product Name: MINIPACK3_BMC\n"
    "BMC MAC Base: AA:BB:CC:00:00:01\n"
    "X86 CPU MAC Base: aa:bb:cc:00:00:10\n";

} // namespace

TEST(BmcChecksTest, FindWeutilField) {
  EXPECT_EQ(
      findWeutilField(kWeutilAll, "X86 CPU MAC Base"), "aa:bb:cc:00:00:10");
  EXPECT_EQ(findWeutilField(kWeutilAll, "Missing"), std::nullopt);
}

TEST(BmcChecksTest, BmcMacMatchesIgnoringCase) {
  auto bmc = std::make_shared<MockHost>("bmc");
  EXPECT_CALL(*bmc, run("weutil --all", _)).WillOnce(Return(ok(kWeutilAll)));
  EXPECT_CALL(*bmc, run("cat /sys/class/net/eth0/address", _))
      .WillOnce(Return(ok("aa:bb:cc:00:00:01\n")));

  auto result = BmcMacAddressCheck(CheckTarget{.host = bmc}).run();

  EXPECT_EQ(*result.status(), CheckStatus::OK);
}

TEST(BmcChecksTest, BmcMacMismatch) {
  auto bmc = std::make_shared<MockHost>("bmc");
  EXPECT_CALL(*bmc, run("weutil --all", _)).WillOnce(Return(ok(kWeutilAll)));
  EXPECT_CALL(*bmc, run("cat /sys/class/net/eth0/address", _))
      .WillOnce(Return(ok("aa:bb:cc:00:00:02\n")));

  auto result = BmcMacAddressCheck(CheckTarget{.host = bmc}).run();

  EXPECT_EQ(*result.status(), CheckStatus::PROBLEM);
  EXPECT_EQ(*result.remediation(), RemediationType::RMA_REQUIRED);
}

TEST(BmcChecksTest, BmcEepromReportsFailedEeproms) {
  auto bmc = std::make_shared<MockHost>("bmc");
  EXPECT_CALL(*bmc, run("weutil --list", _))
      .WillOnce(Return(ok("CHASSIS  /run/devmap/eeproms/CHASSIS\nSCM  x\n")));
  EXPECT_CALL(*bmc, run("weutil --eeprom CHASSIS", _)).WillOnce(Return(ok("")));
  EXPECT_CALL(*bmc, run("weutil --eeprom SCM", _))
      .WillOnce(Return(CommandResult{.exitCode = 1}));

  auto result = BmcEepromCheck(CheckTarget{.host = bmc}).run();

  EXPECT_EQ(*result.status(), CheckStatus::PROBLEM);
  EXPECT_EQ(*result.errorMessage(), "weutil failed to read: SCM");
}

TEST(BmcChecksTest, X86MacConsistent) {
  auto x86 = std::make_shared<MockHost>("x86");
  auto bmc = std::make_shared<MockHost>("bmc");
  EXPECT_CALL(*bmc, run("wedge_us_mac.sh", _))
      .WillOnce(Return(ok("AA:BB:CC:00:00:10\n")));
  EXPECT_CALL(*bmc, run("weutil --all", _)).WillOnce(Return(ok(kWeutilAll)));
  EXPECT_CALL(*x86, run("cat /sys/class/net/eth0/address", _))
      .WillOnce(Return(ok("aa:bb:cc:00:00:10\n")));

  auto result =
      X86MacConsistencyCheck(CheckTarget{.host = x86}, CheckTarget{.host = bmc})
          .run();

  EXPECT_EQ(*result.status(), CheckStatus::OK);
}

TEST(BmcChecksTest, X86MacConsistencySkippedWithoutBmc) {
  X86MacConsistencyCheck check(
      CheckTarget{.host = std::make_shared<MockHost>()},
      CheckTarget{.host = nullptr, .unavailableReason = "no BMC"});

  EXPECT_EQ(check.getSkipReason(), "no BMC");
}
