/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 */

#include "fboss/platform/platform_checks/checks/i801SmbusTimeoutCheck.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/platform/platform_checks/tests/MockHost.h"

using namespace ::testing;
using namespace facebook::fboss::platform;
using namespace facebook::fboss::platform::platform_checks;

class i801SmbusTimeoutCheckTest : public ::testing::Test {
 protected:
  void SetUp() override {
    host_ = std::make_shared<MockHost>();
    check_ =
        std::make_unique<i801SmbusTimeoutCheck>(CheckTarget{.host = host_});
  }

  void expectEepromReadFailure() {
    EXPECT_CALL(
        *host_, run(AllOf(StartsWith("weutil "), HasSubstr("MCB_EEPROM")), _))
        .WillOnce(Return(CommandResult{.exitCode = 1}));
  }

  void expectHexdump(const CommandResult& result) {
    EXPECT_CALL(*host_, run(HasSubstr("hexdump"), _)).WillOnce(Return(result));
  }

  std::shared_ptr<MockHost> host_;
  std::unique_ptr<i801SmbusTimeoutCheck> check_;
};

TEST_F(i801SmbusTimeoutCheckTest, McbEepromNotFound) {
  // MCB EEPROM doesn't exist - check not applicable
  EXPECT_CALL(*host_, exists(_)).WillOnce(Return(false));

  auto result = check_->run();

  EXPECT_EQ(result.checkType(), CheckType::I801_SMBUS_TIMEOUT_CHECK);
  EXPECT_EQ(result.status(), CheckStatus::OK);
}

TEST_F(i801SmbusTimeoutCheckTest, DriverNotFound) {
  // MCB EEPROM exists
  EXPECT_CALL(*host_, exists(_))
      .WillOnce(Return(true)) // MCB EEPROM path
      .WillOnce(Return(false)); // Driver path

  // EEPROM read fails
  expectEepromReadFailure();

  auto result = check_->run();

  EXPECT_EQ(result.checkType(), CheckType::I801_SMBUS_TIMEOUT_CHECK);
  EXPECT_EQ(result.status(), CheckStatus::OK);
}

TEST_F(i801SmbusTimeoutCheckTest, PciDeviceNotBound) {
  // MCB EEPROM exists
  EXPECT_CALL(*host_, exists(_))
      .WillOnce(Return(true)) // MCB EEPROM path
      .WillOnce(Return(true)) // Driver path
      .WillOnce(Return(false)); // PCI device path

  // EEPROM read fails
  expectEepromReadFailure();

  auto result = check_->run();

  EXPECT_EQ(result.checkType(), CheckType::I801_SMBUS_TIMEOUT_CHECK);
  EXPECT_EQ(result.status(), CheckStatus::OK);
}

TEST_F(i801SmbusTimeoutCheckTest, TimeoutDetected) {
  // MCB EEPROM exists
  EXPECT_CALL(*host_, exists(_))
      .WillOnce(Return(true)) // MCB EEPROM path
      .WillOnce(Return(true)) // Driver path
      .WillOnce(Return(true)); // PCI device path

  // EEPROM read fails
  expectEepromReadFailure();

  // hexdump fails with timeout error
  expectHexdump(
      {.exitCode = 1,
       .standardErr =
           "hexdump: /run/devmap/eeproms/MCB_EEPROM: Connection timed out"});

  auto result = check_->run();

  EXPECT_EQ(result.checkType(), CheckType::I801_SMBUS_TIMEOUT_CHECK);
  EXPECT_EQ(result.status(), CheckStatus::PROBLEM);
  EXPECT_TRUE(result.errorMessage().has_value());
  EXPECT_TRUE(
      result.errorMessage()->find("MCB EEPROM read timed out") !=
      std::string::npos);
  EXPECT_EQ(result.remediation().value(), RemediationType::MANUAL_REMEDIATION);
}

TEST_F(i801SmbusTimeoutCheckTest, OtherHexdumpError) {
  // MCB EEPROM exists
  EXPECT_CALL(*host_, exists(_))
      .WillOnce(Return(true)) // MCB EEPROM path
      .WillOnce(Return(true)) // Driver path
      .WillOnce(Return(true)); // PCI device path

  // EEPROM read fails
  expectEepromReadFailure();

  // hexdump fails with a different error (not timeout)
  expectHexdump({.exitCode = 1, .standardErr = "hexdump: permission denied"});

  auto result = check_->run();

  EXPECT_EQ(result.checkType(), CheckType::I801_SMBUS_TIMEOUT_CHECK);
  EXPECT_EQ(result.status(), CheckStatus::PROBLEM);
  EXPECT_TRUE(result.errorMessage().has_value());
  EXPECT_TRUE(
      result.errorMessage()->find("i801_smbus timeout not detected") !=
      std::string::npos);
}

TEST_F(i801SmbusTimeoutCheckTest, HexdumpSucceedsButEepromFailed) {
  // MCB EEPROM exists
  EXPECT_CALL(*host_, exists(_))
      .WillOnce(Return(true)) // MCB EEPROM path
      .WillOnce(Return(true)) // Driver path
      .WillOnce(Return(true)); // PCI device path

  // EEPROM read fails
  expectEepromReadFailure();

  // hexdump succeeds
  expectHexdump({.exitCode = 0, .standardOut = "0000000 1234 5678\n"});

  auto result = check_->run();

  EXPECT_EQ(result.checkType(), CheckType::I801_SMBUS_TIMEOUT_CHECK);
  EXPECT_EQ(result.status(), CheckStatus::PROBLEM);
  EXPECT_TRUE(result.errorMessage().has_value());
  EXPECT_TRUE(
      result.errorMessage()->find("i801_smbus timeout not detected") !=
      std::string::npos);
}
