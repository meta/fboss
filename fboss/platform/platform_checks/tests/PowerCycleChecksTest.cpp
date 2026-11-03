/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/checks/PowerCycleChecks.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/platform/platform_checks/tests/MockHost.h"

using namespace facebook::fboss::platform::platform_checks;
using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;

namespace {

constexpr RebootTiming kNoWait{
    .settle = std::chrono::seconds(0),
    .pollInterval = std::chrono::seconds(0),
    .timeout = std::chrono::seconds(5)};

CommandResult ok(const std::string& out = "") {
  return CommandResult{.exitCode = 0, .standardOut = out};
}

CommandResult bootSource(const std::string& flash) {
  return ok("Current Boot Code Source: " + flash + "\n");
}

std::shared_ptr<MockHost> self(const std::string& machineId = "self-id") {
  auto self = std::make_shared<MockHost>("localhost", true);
  ON_CALL(*self, readFile(std::filesystem::path("/etc/machine-id")))
      .WillByDefault(Return(machineId + "\n"));
  return self;
}

} // namespace

TEST(SecondaryBmcBootCheckTest, BootsFromBothFlashes) {
  auto bmc = std::make_shared<MockHost>("bmc");
  {
    InSequence seq;
    EXPECT_CALL(*bmc, run("boot_info.sh bmc", _))
        .WillOnce(Return(bootSource("Master Flash")));
    EXPECT_CALL(*bmc, run("boot_info.sh bmc reset slave", _))
        .WillOnce(Return(CommandResult{.exitCode = 255}));
    EXPECT_CALL(*bmc, run("true", _)).WillOnce(Return(ok()));
    EXPECT_CALL(*bmc, run("boot_info.sh bmc", _))
        .WillOnce(Return(bootSource("Slave Flash")));
    EXPECT_CALL(*bmc, run("boot_info.sh bmc reset master", _))
        .WillOnce(Return(CommandResult{.exitCode = 255}));
    EXPECT_CALL(*bmc, run("true", _)).WillOnce(Return(ok()));
    EXPECT_CALL(*bmc, run("boot_info.sh bmc", _))
        .WillOnce(Return(bootSource("Master Flash")));
  }

  auto result = SecondaryBmcBootCheck(CheckTarget{.host = bmc}, kNoWait).run();

  EXPECT_EQ(*result.status(), CheckStatus::OK);
}

TEST(SecondaryBmcBootCheckTest, NotStartingOnMasterIsProblem) {
  auto bmc = std::make_shared<MockHost>("bmc");
  EXPECT_CALL(*bmc, run("boot_info.sh bmc", _))
      .WillOnce(Return(bootSource("Slave Flash")));

  auto result = SecondaryBmcBootCheck(CheckTarget{.host = bmc}, kNoWait).run();

  EXPECT_EQ(*result.status(), CheckStatus::PROBLEM);
}

TEST(WedgePowerCheckTest, StaleUptimeMeansNoReboot) {
  auto x86 = std::make_shared<MockHost>("x86");
  auto bmc = std::make_shared<MockHost>("bmc");
  EXPECT_CALL(*bmc, run("wedge_power.sh on", _)).WillRepeatedly(Return(ok()));
  EXPECT_CALL(*bmc, run("wedge_power.sh off", _)).WillOnce(Return(ok()));
  EXPECT_CALL(*bmc, run("wedge_power.sh status", _))
      .WillOnce(Return(ok("Microserver power is on\n")))
      .WillOnce(Return(ok("Microserver power is off\n")))
      .WillOnce(Return(ok("Microserver power is on\n")));
  EXPECT_CALL(*x86, run("true", _)).WillOnce(Return(ok()));
  EXPECT_CALL(*x86, run("cat /proc/uptime", _))
      .WillOnce(Return(ok("86400.00 1000.00\n")));

  auto result =
      WedgePowerCheck(
          CheckTarget{.host = x86}, CheckTarget{.host = bmc}, self(), kNoWait)
          .run();

  EXPECT_EQ(*result.status(), CheckStatus::PROBLEM);
  EXPECT_THAT(
      *result.errorMessage(),
      ::testing::StartsWith("x86 did not reboot: uptime is 86400s, "));
}

TEST(WedgePowerCheckTest, IsDisruptiveAndNeedsBmc) {
  WedgePowerCheck check(
      CheckTarget{.host = std::make_shared<MockHost>()},
      CheckTarget{.host = nullptr, .unavailableReason = "no BMC"},
      self());

  EXPECT_TRUE(check.isDisruptive());
  EXPECT_EQ(check.getSkipReason(), "no BMC");
}

TEST(SecondaryBmcBootCheckTest, RestoresMasterWhenSlaveBootFails) {
  auto bmc = std::make_shared<MockHost>("bmc");
  {
    InSequence seq;
    EXPECT_CALL(*bmc, run("boot_info.sh bmc", _))
        .WillOnce(Return(bootSource("Master Flash")));
    EXPECT_CALL(*bmc, run("boot_info.sh bmc reset slave", _))
        .WillOnce(Return(ok()));
    EXPECT_CALL(*bmc, run("true", _)).WillOnce(Return(ok()));
    EXPECT_CALL(*bmc, run("boot_info.sh bmc", _))
        .WillOnce(Return(bootSource("Master Flash")));
    EXPECT_CALL(*bmc, run("boot_info.sh bmc reset master", _))
        .WillOnce(Return(ok()));
    EXPECT_CALL(*bmc, run("true", _)).WillOnce(Return(ok()));
    EXPECT_CALL(*bmc, run("boot_info.sh bmc", _))
        .WillOnce(Return(bootSource("Master Flash")));
  }

  auto result = SecondaryBmcBootCheck(CheckTarget{.host = bmc}, kNoWait).run();

  EXPECT_EQ(*result.status(), CheckStatus::PROBLEM);
  EXPECT_EQ(
      *result.remediationMessage(),
      "Check the BMC flash state with boot_info.sh bmc");
}

TEST(WedgePowerCheckTest, SkippedWhenX86IsLocal) {
  WedgePowerCheck check(
      CheckTarget{.host = std::make_shared<MockHost>("localhost", true)},
      CheckTarget{.host = std::make_shared<MockHost>("bmc")},
      self());

  EXPECT_TRUE(check.getSkipReason().has_value());
}

TEST(WedgePowerCheckTest, AlreadyOnIsJudgedByStatus) {
  auto x86 = std::make_shared<MockHost>("x86");
  auto bmc = std::make_shared<MockHost>("bmc");
  // OpenBMC's wedge_power.sh exits 1 with "Already on. Skip!".
  EXPECT_CALL(*bmc, run("wedge_power.sh on", _))
      .WillOnce(Return(
          CommandResult{.exitCode = 1, .standardOut = "Already on. Skip!\n"}))
      .WillRepeatedly(Return(ok()));
  EXPECT_CALL(*bmc, run("wedge_power.sh off", _)).WillOnce(Return(ok()));
  EXPECT_CALL(*bmc, run("wedge_power.sh status", _))
      .WillOnce(Return(ok("Microserver power is on\n")))
      .WillOnce(Return(ok("Microserver power is off\n")))
      .WillOnce(Return(ok("Microserver power is on\n")));
  EXPECT_CALL(*x86, run("true", _)).WillOnce(Return(ok()));
  EXPECT_CALL(*x86, run("cat /proc/uptime", _))
      .WillOnce(Return(ok("86400.00 1000.00\n")));

  auto result =
      WedgePowerCheck(
          CheckTarget{.host = x86}, CheckTarget{.host = bmc}, self(), kNoWait)
          .run();

  // Gets past the first "on" to the fresh-boot check.
  EXPECT_THAT(
      *result.errorMessage(),
      ::testing::StartsWith("x86 did not reboot: uptime is 86400s, "));
}

TEST(WedgePowerCheckTest, FailedPowerOnIsRetriedAndReported) {
  auto x86 = std::make_shared<MockHost>("x86");
  auto bmc = std::make_shared<MockHost>("bmc");
  EXPECT_CALL(*bmc, run("wedge_power.sh on", _)).WillRepeatedly(Return(ok()));
  EXPECT_CALL(*bmc, run("wedge_power.sh off", _)).WillOnce(Return(ok()));
  // Powering back on never takes effect.
  EXPECT_CALL(*bmc, run("wedge_power.sh status", _))
      .WillOnce(Return(ok("Microserver power is on\n")))
      .WillRepeatedly(Return(ok("Microserver power is off\n")));

  auto result =
      WedgePowerCheck(
          CheckTarget{.host = x86}, CheckTarget{.host = bmc}, self(), kNoWait)
          .run();

  EXPECT_EQ(*result.status(), CheckStatus::PROBLEM);
  EXPECT_THAT(
      *result.remediationMessage(), ::testing::HasSubstr("powered off"));
  EXPECT_THAT(
      *result.details(),
      ::testing::HasSubstr("Trying to power the x86 back on"));
}

TEST(WedgePowerCheckTest, RecoveredPowerOnStillReported) {
  auto x86 = std::make_shared<MockHost>("x86");
  auto bmc = std::make_shared<MockHost>("bmc");
  EXPECT_CALL(*bmc, run("wedge_power.sh on", _)).WillRepeatedly(Return(ok()));
  EXPECT_CALL(*bmc, run("wedge_power.sh off", _)).WillOnce(Return(ok()));
  // The second "on" is not reflected in time; the retry is.
  EXPECT_CALL(*bmc, run("wedge_power.sh status", _))
      .WillOnce(Return(ok("Microserver power is on\n")))
      .WillOnce(Return(ok("Microserver power is off\n")))
      .WillOnce(Return(ok("Microserver power is off\n")))
      .WillOnce(Return(ok("Microserver power is on\n")));

  auto result =
      WedgePowerCheck(
          CheckTarget{.host = x86}, CheckTarget{.host = bmc}, self(), kNoWait)
          .run();

  EXPECT_EQ(*result.status(), CheckStatus::PROBLEM);
  EXPECT_THAT(
      *result.remediationMessage(),
      ::testing::Not(::testing::HasSubstr("powered off")));
}

TEST(SecondaryBmcBootCheckTest, FailedRestoreIsReported) {
  auto bmc = std::make_shared<MockHost>("bmc");
  {
    InSequence seq;
    EXPECT_CALL(*bmc, run("boot_info.sh bmc", _))
        .WillOnce(Return(bootSource("Master Flash")));
    EXPECT_CALL(*bmc, run("boot_info.sh bmc reset slave", _))
        .WillOnce(Return(ok()));
    EXPECT_CALL(*bmc, run("true", _)).WillOnce(Return(ok()));
    EXPECT_CALL(*bmc, run("boot_info.sh bmc", _))
        .WillOnce(Return(bootSource("Master Flash")));
    EXPECT_CALL(*bmc, run("boot_info.sh bmc reset master", _))
        .WillOnce(Return(ok()));
    EXPECT_CALL(*bmc, run("true", _)).WillOnce(Return(ok()));
    // The BMC came back, but still on its slave flash.
    EXPECT_CALL(*bmc, run("boot_info.sh bmc", _))
        .WillOnce(Return(bootSource("Slave Flash")));
  }

  auto result = SecondaryBmcBootCheck(CheckTarget{.host = bmc}, kNoWait).run();

  EXPECT_EQ(*result.status(), CheckStatus::PROBLEM);
  EXPECT_THAT(
      *result.remediationMessage(), ::testing::HasSubstr("slave flash"));
}

TEST(WedgePowerCheckTest, SkippedWhenX86IsThisMachine) {
  auto x86 = std::make_shared<MockHost>("x86");
  EXPECT_CALL(*x86, readFile(std::filesystem::path("/etc/machine-id")))
      .WillOnce(Return("self-id\n"));
  WedgePowerCheck check(
      CheckTarget{.host = x86},
      CheckTarget{.host = std::make_shared<MockHost>("bmc")},
      self("self-id"));

  EXPECT_TRUE(check.getSkipReason().has_value());
}

TEST(WedgePowerCheckTest, SkippedWhenX86IdentityIsUnknown) {
  auto x86 = std::make_shared<MockHost>("x86");
  EXPECT_CALL(*x86, readFile(std::filesystem::path("/etc/machine-id")))
      .WillOnce(Return(std::nullopt));
  WedgePowerCheck check(
      CheckTarget{.host = x86},
      CheckTarget{.host = std::make_shared<MockHost>("bmc")},
      self());

  EXPECT_TRUE(check.getSkipReason().has_value());
}

TEST(WedgePowerCheckTest, SkippedWhenOwnIdentityIsEmpty) {
  auto x86 = std::make_shared<MockHost>("x86");
  EXPECT_CALL(*x86, readFile(std::filesystem::path("/etc/machine-id")))
      .WillOnce(Return("x86-id\n"));
  WedgePowerCheck check(
      CheckTarget{.host = x86},
      CheckTarget{.host = std::make_shared<MockHost>("bmc")},
      self(""));

  EXPECT_TRUE(check.getSkipReason().has_value());
}

TEST(WedgePowerCheckTest, RunsWhenX86IsAnotherMachine) {
  auto x86 = std::make_shared<MockHost>("x86");
  EXPECT_CALL(*x86, readFile(std::filesystem::path("/etc/machine-id")))
      .WillOnce(Return("x86-id\n"));
  WedgePowerCheck check(
      CheckTarget{.host = x86},
      CheckTarget{.host = std::make_shared<MockHost>("bmc")},
      self());

  EXPECT_EQ(check.getSkipReason(), std::nullopt);
}

TEST(WedgePowerCheckTest, SlowBmcDoesNotFailX86UptimeCheck) {
  auto x86 = std::make_shared<MockHost>("x86");
  auto bmc = std::make_shared<MockHost>("bmc");
  EXPECT_CALL(*bmc, run("wedge_power.sh on", _)).WillRepeatedly(Return(ok()));
  EXPECT_CALL(*bmc, run("wedge_power.sh off", _)).WillOnce(Return(ok()));
  EXPECT_CALL(*bmc, run("wedge_power.sh status", _))
      .WillOnce(Return(ok("Microserver power is on\n")))
      .WillOnce(Return(ok("Microserver power is off\n")))
      .WillOnce(Return(ok("Microserver power is on\n")));
  EXPECT_CALL(*bmc, run("wedge_power.sh reset -s", _))
      .WillOnce(Return(CommandResult{.exitCode = 255}));
  // The BMC is unreachable for a while after the reset.
  EXPECT_CALL(*bmc, run("true", _))
      .WillOnce(Return(CommandResult{.exitCode = 255}))
      .WillOnce(Return(CommandResult{.exitCode = 255}))
      .WillOnce(Return(ok()));
  EXPECT_CALL(*bmc, run("cat /proc/uptime", _))
      .WillOnce(Return(ok("1.00 1.00\n")));
  EXPECT_CALL(*x86, run("true", _)).WillRepeatedly(Return(ok()));
  // By then the x86 has been up for longer than the BMC.
  EXPECT_CALL(*x86, run("cat /proc/uptime", _))
      .WillOnce(Return(ok("0.00 0.00\n")))
      .WillOnce(Return(ok("1.50 1.50\n")));
  RebootTiming timing = kNoWait;
  timing.pollInterval = std::chrono::seconds(1);

  auto result =
      WedgePowerCheck(
          CheckTarget{.host = x86}, CheckTarget{.host = bmc}, self(), timing)
          .run();

  EXPECT_EQ(*result.status(), CheckStatus::OK);
}

TEST(WedgePowerCheckTest, UptimeFromBeforeThePowerCycleIsNoReboot) {
  auto x86 = std::make_shared<MockHost>("x86");
  auto bmc = std::make_shared<MockHost>("bmc");
  EXPECT_CALL(*bmc, run("wedge_power.sh on", _)).WillRepeatedly(Return(ok()));
  EXPECT_CALL(*bmc, run("wedge_power.sh off", _)).WillOnce(Return(ok()));
  EXPECT_CALL(*bmc, run("wedge_power.sh status", _))
      .WillOnce(Return(ok("Microserver power is on\n")))
      .WillOnce(Return(ok("Microserver power is off\n")))
      .WillOnce(Return(ok("Microserver power is on\n")));
  EXPECT_CALL(*x86, run("true", _)).WillOnce(Return(ok()));
  // Well below a fixed cap, but longer than since the power cycle.
  EXPECT_CALL(*x86, run("cat /proc/uptime", _))
      .WillOnce(Return(ok("30.00 30.00\n")));

  auto result =
      WedgePowerCheck(
          CheckTarget{.host = x86}, CheckTarget{.host = bmc}, self(), kNoWait)
          .run();

  EXPECT_EQ(*result.status(), CheckStatus::PROBLEM);
}
