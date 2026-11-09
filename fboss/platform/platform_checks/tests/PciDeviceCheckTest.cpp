/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/checks/PciDeviceCheck.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <filesystem>

#include "fboss/platform/platform_checks/tests/MockHost.h"

using namespace ::testing;
using namespace facebook::fboss::platform;
using namespace facebook::fboss::platform::platform_checks;

namespace {

class MockPciDeviceCheck : public PciDeviceCheck {
 public:
  explicit MockPciDeviceCheck(std::shared_ptr<MockHost> host)
      : PciDeviceCheck(CheckTarget{.host = std::move(host)}) {}

  MOCK_METHOD(
      std::vector<std::filesystem::path>,
      getPciDevicePaths,
      (),
      (const));

  MOCK_METHOD(platform_manager::PlatformConfig, getPlatformConfig, (), (const));
};

// Helper to create a test platform config with known PCI devices
platform_manager::PlatformConfig createTestPlatformConfig(
    const std::vector<
        std::tuple<std::string, std::string, std::string, std::string>>&
        devices) {
  platform_manager::PlatformConfig config;
  std::map<std::string, platform_manager::PmUnitConfig> pmUnitConfigs;

  platform_manager::PmUnitConfig pmUnitConfig;
  std::vector<platform_manager::PciDeviceConfig> pciDeviceConfigs;

  for (const auto& [vendor, device, subsystemVendor, subsystemDevice] :
       devices) {
    platform_manager::PciDeviceConfig pciDeviceConfig;
    pciDeviceConfig.pmUnitScopedName() = "TEST_DEVICE";
    pciDeviceConfig.vendorId() = vendor;
    pciDeviceConfig.deviceId() = device;
    pciDeviceConfig.subSystemVendorId() = subsystemVendor;
    pciDeviceConfig.subSystemDeviceId() = subsystemDevice;
    pciDeviceConfigs.push_back(pciDeviceConfig);
  }

  pmUnitConfig.pciDeviceConfigs() = pciDeviceConfigs;
  pmUnitConfigs["TEST_PM_UNIT"] = pmUnitConfig;
  config.pmUnitConfigs() = pmUnitConfigs;

  return config;
}

} // namespace

class PciDeviceCheckTest : public ::testing::Test {
 protected:
  void SetUp() override {
    host_ = std::make_shared<MockHost>();
    check_ = std::make_unique<MockPciDeviceCheck>(host_);
  }

  std::shared_ptr<MockHost> host_;
  std::unique_ptr<MockPciDeviceCheck> check_;
};

TEST_F(PciDeviceCheckTest, AllDevicesPresent) {
  // Create a test config expecting two specific PCI devices
  auto testConfig = createTestPlatformConfig({
      {"0x1234", "0x5678", "0xabcd", "0xef01"},
      {"0x8086", "0x1234", "0x1111", "0x2222"},
  });

  // Mock config to return our test config
  EXPECT_CALL(*check_, getPlatformConfig()).WillRepeatedly(Return(testConfig));

  // Mock filesystem to show both devices are present
  std::vector<std::filesystem::path> mockPaths = {
      "/sys/bus/pci/devices/0000:01:00.0",
      "/sys/bus/pci/devices/0000:02:00.0",
  };
  EXPECT_CALL(*check_, getPciDevicePaths()).WillRepeatedly(Return(mockPaths));

  // Mock file reads for first device (matches first config device)
  EXPECT_CALL(
      *host_,
      readFile(
          std::filesystem::path("/sys/bus/pci/devices/0000:01:00.0/vendor")))
      .WillRepeatedly(Return(std::make_optional<std::string>("0x1234\n")));
  EXPECT_CALL(
      *host_,
      readFile(
          std::filesystem::path("/sys/bus/pci/devices/0000:01:00.0/device")))
      .WillRepeatedly(Return(std::make_optional<std::string>("0x5678\n")));
  EXPECT_CALL(
      *host_,
      readFile(
          std::filesystem::path(
              "/sys/bus/pci/devices/0000:01:00.0/subsystem_vendor")))
      .WillRepeatedly(Return(std::make_optional<std::string>("0xabcd\n")));
  EXPECT_CALL(
      *host_,
      readFile(
          std::filesystem::path(
              "/sys/bus/pci/devices/0000:01:00.0/subsystem_device")))
      .WillRepeatedly(Return(std::make_optional<std::string>("0xef01\n")));

  // Mock file reads for second device (matches second config device)
  EXPECT_CALL(
      *host_,
      readFile(
          std::filesystem::path("/sys/bus/pci/devices/0000:02:00.0/vendor")))
      .WillRepeatedly(Return(std::make_optional<std::string>("0x8086\n")));
  EXPECT_CALL(
      *host_,
      readFile(
          std::filesystem::path("/sys/bus/pci/devices/0000:02:00.0/device")))
      .WillRepeatedly(Return(std::make_optional<std::string>("0x1234\n")));
  EXPECT_CALL(
      *host_,
      readFile(
          std::filesystem::path(
              "/sys/bus/pci/devices/0000:02:00.0/subsystem_vendor")))
      .WillRepeatedly(Return(std::make_optional<std::string>("0x1111\n")));
  EXPECT_CALL(
      *host_,
      readFile(
          std::filesystem::path(
              "/sys/bus/pci/devices/0000:02:00.0/subsystem_device")))
      .WillRepeatedly(Return(std::make_optional<std::string>("0x2222\n")));

  // Run check - should pass since all expected devices are present
  auto result = check_->run();

  EXPECT_EQ(result.checkType(), CheckType::PCI_DEVICE_CHECK);
  EXPECT_EQ(result.status(), CheckStatus::OK);
}

TEST_F(PciDeviceCheckTest, MissingDevice) {
  auto testConfig = createTestPlatformConfig({
      {"0x1234", "0x5678", "0xabcd", "0xef01"},
      {"0x8086", "0x1234", "0x1111", "0x2222"},
  });

  EXPECT_CALL(*check_, getPlatformConfig()).WillRepeatedly(Return(testConfig));

  // ONE device present (missing one)
  std::vector<std::filesystem::path> mockPaths = {
      "/sys/bus/pci/devices/0000:01:00.0",
  };
  EXPECT_CALL(*check_, getPciDevicePaths()).WillRepeatedly(Return(mockPaths));

  EXPECT_CALL(
      *host_,
      readFile(
          std::filesystem::path("/sys/bus/pci/devices/0000:01:00.0/vendor")))
      .WillRepeatedly(Return(std::make_optional<std::string>("0x1234\n")));
  EXPECT_CALL(
      *host_,
      readFile(
          std::filesystem::path("/sys/bus/pci/devices/0000:01:00.0/device")))
      .WillRepeatedly(Return(std::make_optional<std::string>("0x5678\n")));
  EXPECT_CALL(
      *host_,
      readFile(
          std::filesystem::path(
              "/sys/bus/pci/devices/0000:01:00.0/subsystem_vendor")))
      .WillRepeatedly(Return(std::make_optional<std::string>("0xabcd\n")));
  EXPECT_CALL(
      *host_,
      readFile(
          std::filesystem::path(
              "/sys/bus/pci/devices/0000:01:00.0/subsystem_device")))
      .WillRepeatedly(Return(std::make_optional<std::string>("0xef01\n")));

  // Run check - should fail with PROBLEM status (one device missing)
  auto result = check_->run();

  EXPECT_EQ(result.checkType(), CheckType::PCI_DEVICE_CHECK);
  EXPECT_EQ(result.status(), CheckStatus::PROBLEM);
  EXPECT_TRUE(result.errorMessage().has_value());
  EXPECT_TRUE(
      result.errorMessage()->find("Missing 1 PCI device(s)") !=
      std::string::npos);
  EXPECT_TRUE(
      result.errorMessage()->find("0x8086:0x1234:0x1111:0x2222") !=
      std::string::npos);
  EXPECT_TRUE(result.remediationMessage().has_value());
  EXPECT_EQ(result.remediation().value(), RemediationType::REBOOT_REQUIRED);
}

TEST_F(PciDeviceCheckTest, EmptyAttributeIsNotAPresentDevice) {
  auto testConfig = createTestPlatformConfig({
      {"0x1234", "0x5678", "0xabcd", "0xef01"},
  });
  EXPECT_CALL(*check_, getPlatformConfig()).WillRepeatedly(Return(testConfig));
  const std::filesystem::path device("/sys/bus/pci/devices/0000:01:00.0");
  EXPECT_CALL(*check_, getPciDevicePaths())
      .WillRepeatedly(Return(std::vector<std::filesystem::path>{device}));
  EXPECT_CALL(*host_, readFile(device / "vendor"))
      .WillRepeatedly(Return(std::make_optional<std::string>("\n")));
  EXPECT_CALL(*host_, readFile(device / "device"))
      .WillRepeatedly(Return(std::make_optional<std::string>("0x5678\n")));
  EXPECT_CALL(*host_, readFile(device / "subsystem_vendor"))
      .WillRepeatedly(Return(std::make_optional<std::string>("0xabcd\n")));
  EXPECT_CALL(*host_, readFile(device / "subsystem_device"))
      .WillRepeatedly(Return(std::make_optional<std::string>("0xef01\n")));

  auto result = check_->run();

  EXPECT_EQ(result.status(), CheckStatus::PROBLEM);
  EXPECT_NE(
      result.errorMessage()->find("Missing 1 PCI device(s)"),
      std::string::npos);
}
