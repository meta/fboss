// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <folly/ScopeGuard.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/platform/platform_manager/DevicePathResolver.h"

namespace fs = std::filesystem;

using namespace ::testing;
using namespace facebook::fboss::platform::platform_manager;

class DevicePathResolverTest : public testing::Test {
 public:
  void SetUp() override {
    const auto* testInfo =
        ::testing::UnitTest::GetInstance()->current_test_info();
    std::string uniqueName =
        std::string("device_path_resolver_test_") +
        testInfo->test_suite_name() + "_" + testInfo->name() + "_" +
        std::to_string(
            std::chrono::system_clock::now().time_since_epoch().count());
    tmpDir_ = fs::temp_directory_path() / uniqueName;
    fs::create_directories(tmpDir_);

    config_ = PlatformConfig();
    dataStore_ = std::make_unique<DataStore>(config_);
    // Zero retry budget: negative cases resolve immediately instead of waiting
    // out the production sensor path timeout.
    resolver_ = std::make_unique<DevicePathResolver>(
        *dataStore_, std::chrono::seconds(0));
  }

  void TearDown() override {
    if (fs::exists(tmpDir_)) {
      fs::remove_all(tmpDir_);
    }
  }

  void createFile(const fs::path& path, const std::string& content = "") {
    fs::create_directories(path.parent_path());
    std::ofstream ofs(path);
    ofs << content;
  }

  fs::path createSubDir(const std::string& relativePath) {
    auto path = tmpDir_ / relativePath;
    fs::create_directories(path);
    return path;
  }

 protected:
  fs::path tmpDir_;
  PlatformConfig config_;
  std::unique_ptr<DataStore> dataStore_;
  std::unique_ptr<DevicePathResolver> resolver_;

  static constexpr auto kTestDevicePath = "/[TEST_DEVICE]";
  static constexpr auto kPciDevicePath = "/[PCI_DEVICE]";
  static constexpr auto kNonexistentDevicePath = "/[NONEXISTENT_DEVICE]";
};

TEST_F(DevicePathResolverTest, ResolveSensorPath) {
  auto sysfsPath = createSubDir("device/sysfs");
  dataStore_->updateSysfsPath(kTestDevicePath, sysfsPath.string());

  auto hwmon0 = createSubDir("device/sysfs/hwmon/hwmon0");
  EXPECT_EQ(resolver_->resolveSensorPath(kTestDevicePath), hwmon0.string());

  fs::remove_all(sysfsPath / "hwmon");
  auto iioDevice = createSubDir("device/sysfs/iio:device0");
  EXPECT_EQ(resolver_->resolveSensorPath(kTestDevicePath), iioDevice.string());

  fs::remove_all(iioDevice);
  EXPECT_THROW(
      resolver_->resolveSensorPath(kTestDevicePath), std::runtime_error);
}

TEST_F(DevicePathResolverTest, ResolveSensorPathWaitsForLateDriverProbe) {
  auto sysfsPath = createSubDir("device/sysfs");
  dataStore_->updateSysfsPath(kTestDevicePath, sysfsPath.string());

  auto hwmon0 = sysfsPath / "hwmon" / "hwmon0";
  std::thread lateProbe([&]() {
    /* sleep override */
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    fs::create_directories(hwmon0);
  });
  SCOPE_EXIT {
    lateProbe.join();
  };

  DevicePathResolver resolver(*dataStore_, std::chrono::seconds(5));
  EXPECT_EQ(resolver.resolveSensorPath(kTestDevicePath), hwmon0.string());
}

TEST_F(DevicePathResolverTest, ResolveSensorPathReportsFilesystemError) {
  dataStore_->updateSysfsPath(
      kTestDevicePath, (tmpDir_ / "missing/sysfs").string());

  EXPECT_THAT(
      [&]() { resolver_->resolveSensorPath(kTestDevicePath); },
      ThrowsMessage<std::runtime_error>(
          HasSubstr("A filesystem error was encountered while polling")));
}

TEST_F(DevicePathResolverTest, ResolveEepromPath) {
  auto sysfsPath = createSubDir("device/sysfs");
  dataStore_->updateSysfsPath(kTestDevicePath, sysfsPath.string());

  auto eepromPath = sysfsPath / "eeprom";
  createFile(eepromPath);
  EXPECT_EQ(resolver_->resolveEepromPath(kTestDevicePath), eepromPath.string());

  fs::remove(eepromPath);
  EXPECT_THROW(
      resolver_->resolveEepromPath(kTestDevicePath), std::runtime_error);
}

TEST_F(DevicePathResolverTest, ResolveI2cBusPath) {
  dataStore_->updateI2cBusNum("/", "TEST_DEVICE", 5);
  EXPECT_EQ(resolver_->resolveI2cBusPath(kTestDevicePath), "/dev/i2c-5");

  dataStore_->updateI2cBusNum("/SLOT@0", "TEST_DEVICE", 42);
  EXPECT_EQ(
      resolver_->resolveI2cBusPath("/SLOT@0/[TEST_DEVICE]"), "/dev/i2c-42");
}

TEST_F(DevicePathResolverTest, ResolvePciSubDevSysfsPath) {
  auto sysfsPath = createSubDir("pci/device");
  dataStore_->updateSysfsPath(kPciDevicePath, sysfsPath.string());
  EXPECT_EQ(
      resolver_->resolvePciSubDevSysfsPath(kPciDevicePath), sysfsPath.string());

  dataStore_->updateSysfsPath(
      kPciDevicePath, (tmpDir_ / "nonexistent").string());
  EXPECT_THROW(
      resolver_->resolvePciSubDevSysfsPath(kPciDevicePath), std::runtime_error);
}

TEST_F(DevicePathResolverTest, ResolvePciSubDevCharDevPath) {
  auto charDevPath = tmpDir_ / "dev/chardev0";
  createFile(charDevPath);
  dataStore_->updateCharDevPath(kPciDevicePath, charDevPath.string());
  EXPECT_EQ(
      resolver_->resolvePciSubDevCharDevPath(kPciDevicePath),
      charDevPath.string());

  dataStore_->updateCharDevPath(
      kPciDevicePath, (tmpDir_ / "nonexistent").string());
  EXPECT_THROW(
      resolver_->resolvePciSubDevCharDevPath(kPciDevicePath),
      std::runtime_error);
}

TEST_F(DevicePathResolverTest, TryResolveI2cDevicePath) {
  auto sysfsPath = createSubDir("i2c/device");
  dataStore_->updateSysfsPath(kTestDevicePath, sysfsPath.string());

  auto result = resolver_->tryResolveI2cDevicePath(kTestDevicePath);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, sysfsPath.string());

  EXPECT_FALSE(
      resolver_->tryResolveI2cDevicePath(kNonexistentDevicePath).has_value());
}

TEST_F(DevicePathResolverTest, ResolvePciDevicePath) {
  auto sysfsPath = createSubDir("pci/device");
  dataStore_->updateSysfsPath(kPciDevicePath, sysfsPath.string());
  EXPECT_EQ(
      resolver_->resolvePciDevicePath(kPciDevicePath), sysfsPath.string());

  EXPECT_THROW(
      resolver_->resolvePciDevicePath(kNonexistentDevicePath),
      std::runtime_error);
}

TEST_F(DevicePathResolverTest, ResolvePresencePath) {
  const std::string presenceFileName = "presence";
  auto sysfsPath = createSubDir("device/sysfs");
  dataStore_->updateSysfsPath(kTestDevicePath, sysfsPath.string());

  auto presencePath = sysfsPath / presenceFileName;
  createFile(presencePath, "1");
  auto result =
      resolver_->resolvePresencePath(kTestDevicePath, presenceFileName);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, presencePath.string());

  fs::remove(presencePath);
  auto hwmon0 = createSubDir("device/sysfs/hwmon/hwmon0");
  auto hwmonPresencePath = hwmon0 / presenceFileName;
  createFile(hwmonPresencePath, "1");
  result = resolver_->resolvePresencePath(kTestDevicePath, presenceFileName);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, hwmonPresencePath.string());

  fs::remove_all(sysfsPath / "hwmon");
  EXPECT_FALSE(resolver_->resolvePresencePath(kTestDevicePath, presenceFileName)
                   .has_value());

  EXPECT_FALSE(
      resolver_->resolvePresencePath(kNonexistentDevicePath, presenceFileName)
          .has_value());
}
