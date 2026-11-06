/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/checks/MacAddressCheck.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/platform/platform_checks/HostEeprom.h"
#include "fboss/platform/platform_checks/tests/MockHost.h"

using namespace ::testing;
using namespace facebook::fboss::platform::platform_checks;
using MacAddressMap = std::unordered_map<std::string, folly::MacAddress>;

class MockMacAddressCheck : public MacAddressCheck {
 public:
  MOCK_METHOD(
      folly::MacAddress,
      getMacAddress,
      (const std::string& interface),
      ());

  MOCK_METHOD(MacAddressMap, getEepromMacAddressList, (), ());
};

class MacAddressCheckTest : public ::testing::Test {
 protected:
  void SetUp() override {
    check_ = std::make_unique<MockMacAddressCheck>();
  }

  std::unique_ptr<MockMacAddressCheck> check_;
};

TEST_F(MacAddressCheckTest, MatchingAddresses) {
  folly::MacAddress eth0Mac("01:02:03:04:05:06");
  std::unordered_map<std::string, folly::MacAddress> eepromMacList;
  eepromMacList["eth0"] = eth0Mac;
  EXPECT_CALL(*check_, getMacAddress("eth0")).WillRepeatedly(Return(eth0Mac));
  EXPECT_CALL(*check_, getEepromMacAddressList())
      .WillRepeatedly(Return(eepromMacList));

  auto result = check_->run();

  EXPECT_EQ(result.checkType(), CheckType::MAC_ADDRESS_CHECK);
  EXPECT_EQ(result.status(), CheckStatus::OK);
}

TEST_F(MacAddressCheckTest, MismatchedAddresses) {
  folly::MacAddress eth0Mac("01:02:03:04:05:06");
  folly::MacAddress eth0Mac2("00:02:03:04:05:06");
  std::unordered_map<std::string, folly::MacAddress> eepromMacList;
  eepromMacList["eth0"] = eth0Mac2;
  EXPECT_CALL(*check_, getMacAddress("eth0")).WillRepeatedly(Return(eth0Mac));
  EXPECT_CALL(*check_, getEepromMacAddressList())
      .WillRepeatedly(Return(eepromMacList));

  auto result = check_->run();

  EXPECT_EQ(result.checkType(), CheckType::MAC_ADDRESS_CHECK);
  EXPECT_EQ(result.status(), CheckStatus::PROBLEM);
}

TEST_F(MacAddressCheckTest, ZeroAddress) {
  folly::MacAddress eth0Mac("01:02:03:04:05:06");
  std::unordered_map<std::string, folly::MacAddress> eepromMacList;
  eepromMacList["eth0"] = eth0Mac;
  EXPECT_CALL(*check_, getMacAddress("eth0"))
      .WillRepeatedly(Return(folly::MacAddress::ZERO));
  EXPECT_CALL(*check_, getEepromMacAddressList())
      .WillRepeatedly(Return(eepromMacList));

  auto result = check_->run();

  EXPECT_EQ(result.checkType(), CheckType::MAC_ADDRESS_CHECK);
  EXPECT_EQ(result.status(), CheckStatus::ERROR);
}

class EepromMockedMacAddressCheck : public MacAddressCheck {
 public:
  using MacAddressCheck::MacAddressCheck;

  MOCK_METHOD(MacAddressMap, getEepromMacAddressList, (), (override));
};

TEST(MacAddressCheckHostTest, ReadsInterfaceMacFromHostSysfs) {
  auto host = std::make_shared<MockHost>();
  EXPECT_CALL(
      *host, readFile(std::filesystem::path("/sys/class/net/eth1/address")))
      .WillOnce(Return("01:02:03:04:05:06\n"));
  EepromMockedMacAddressCheck check(CheckTarget{.host = host}, "eth1");
  EXPECT_CALL(check, getEepromMacAddressList())
      .WillOnce(Return(
          MacAddressMap{
              {"COME_EEPROM", folly::MacAddress("01:02:03:04:05:06")}}));

  auto result = check.run();

  EXPECT_EQ(result.status(), CheckStatus::OK);
}

TEST(MacAddressCheckHostTest, MissingInterfaceIsError) {
  auto host = std::make_shared<MockHost>();
  EXPECT_CALL(*host, readFile(_)).WillOnce(Return(std::nullopt));
  EepromMockedMacAddressCheck check(CheckTarget{.host = host});

  auto result = check.run();

  EXPECT_EQ(result.status(), CheckStatus::ERROR);
}

TEST(MacAddressCheckHostTest, EepromMacComesFromWeutil) {
  auto host = std::make_shared<MockHost>();
  EXPECT_CALL(*host, run(StartsWith("weutil --json --eeprom "), _))
      .WillRepeatedly(Return(
          CommandResult{
              .exitCode = 0,
              .standardOut = R"({"X86 CPU MAC Base": "01:02:03:04:05:06"})"}));
  EXPECT_CALL(
      *host, readFile(std::filesystem::path("/sys/class/net/eth0/address")))
      .WillOnce(Return("01:02:03:04:05:07\n"));
  MacAddressCheck check(CheckTarget{.host = host, .platformName = "MONTBLANC"});

  auto result = check.run();

  EXPECT_EQ(result.status(), CheckStatus::PROBLEM);
}

TEST(HostEepromTest, ParseWeutilJson) {
  const EepromFields expected{
      {"Local MAC", "aa:bb:cc:dd:ee:ff"}, {"Product Name", "DARWIN"}};

  EXPECT_EQ(
      parseWeutilJson(
          R"({"Local MAC": "aa:bb:cc:dd:ee:ff", "Product Name": "DARWIN"})"),
      expected);
}

TEST(MacAddressCheckHostTest, MalformedEepromMacIsSkipped) {
  auto host = std::make_shared<MockHost>();
  EXPECT_CALL(*host, run(StartsWith("weutil --json --eeprom "), _))
      .WillOnce(Return(
          CommandResult{
              .exitCode = 0,
              .standardOut = R"({"X86 CPU MAC Base": "not-a-mac"})"}))
      .WillRepeatedly(Return(
          CommandResult{
              .exitCode = 0,
              .standardOut = R"({"X86 CPU MAC Base": "01:02:03:04:05:06"})"}));
  EXPECT_CALL(
      *host, readFile(std::filesystem::path("/sys/class/net/eth0/address")))
      .WillOnce(Return("01:02:03:04:05:06\n"));
  MacAddressCheck check(CheckTarget{.host = host, .platformName = "MONTBLANC"});

  auto result = check.run();

  EXPECT_EQ(result.status(), CheckStatus::OK);
}

TEST(MacAddressCheckHostTest, OnlyMalformedEepromMacsIsError) {
  auto host = std::make_shared<MockHost>();
  EXPECT_CALL(*host, run(StartsWith("weutil --json --eeprom "), _))
      .WillRepeatedly(Return(
          CommandResult{
              .exitCode = 0,
              .standardOut = R"({"X86 CPU MAC Base": "not-a-mac"})"}));
  EXPECT_CALL(
      *host, readFile(std::filesystem::path("/sys/class/net/eth0/address")))
      .WillOnce(Return("01:02:03:04:05:06\n"));
  MacAddressCheck check(CheckTarget{.host = host, .platformName = "MONTBLANC"});

  auto result = check.run();

  EXPECT_EQ(result.status(), CheckStatus::ERROR);
  EXPECT_EQ(*result.errorMessage(), "No EEPROM MAC address found");
}
