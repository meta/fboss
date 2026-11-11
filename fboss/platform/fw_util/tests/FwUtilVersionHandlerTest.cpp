// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/fw_util/FwUtilVersionHandler.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sstream>

namespace facebook::fboss::platform::fw_util {

namespace {

// Helper to create a minimal FwUtilConfig for testing
fw_util_config::FwUtilConfig createTestConfig() {
  fw_util_config::FwUtilConfig config;

  // Create device1 config
  fw_util_config::FwConfig device1Config;
  fw_util_config::VersionConfig version1;
  version1.versionType() = "full_command";
  version1.getVersionCmd() = "echo 1.0.0";
  device1Config.version() = version1;

  // Create device2 config
  fw_util_config::FwConfig device2Config;
  fw_util_config::VersionConfig version2;
  version2.versionType() = "full_command";
  version2.getVersionCmd() = "echo 2.0.0";
  device2Config.version() = version2;

  // Add to config
  config.fwConfigs() = {{"device1", device1Config}, {"device2", device2Config}};

  return config;
}

// Helper to capture stdout
class StdoutCapture {
 public:
  StdoutCapture() : oldBuf_(std::cout.rdbuf(buffer_.rdbuf())) {}

  ~StdoutCapture() {
    std::cout.rdbuf(oldBuf_);
  }

  std::string getOutput() const {
    return buffer_.str();
  }

 private:
  std::stringstream buffer_;
  std::streambuf* oldBuf_;
};

} // namespace

class FwUtilVersionHandlerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    config_ = createTestConfig();
    devicesByPrio_ = {{"device1", 1}, {"device2", 2}};
  }

  fw_util_config::FwUtilConfig config_;
  std::vector<std::pair<std::string, int>> devicesByPrio_;
};

// Test printDarwinVersion with a single valid device
TEST_F(FwUtilVersionHandlerTest, PrintDarwinVersionSingleDevice) {
  FwUtilVersionHandler handler(devicesByPrio_, config_);

  StdoutCapture capture;
  handler.printDarwinVersion("device1");

  std::string output = capture.getOutput();
  EXPECT_THAT(output, ::testing::HasSubstr("device1"));
  EXPECT_THAT(output, ::testing::HasSubstr("1.0.0"));
}

// Test printDarwinVersion with device not in config
TEST_F(FwUtilVersionHandlerTest, PrintDarwinVersionDeviceNotFound) {
  FwUtilVersionHandler handler(devicesByPrio_, config_);

  StdoutCapture capture;
  // Should log info message but not throw
  EXPECT_NO_THROW(handler.printDarwinVersion("nonexistent_device"));

  // No output to stdout for non-existent device (only logs)
  std::string output = capture.getOutput();
  EXPECT_TRUE(output.empty());
}

// Test printDarwinVersion with "all" devices
TEST_F(FwUtilVersionHandlerTest, PrintDarwinVersionAllDevices) {
  FwUtilVersionHandler handler(devicesByPrio_, config_);

  StdoutCapture capture;
  handler.printDarwinVersion("all");

  std::string output = capture.getOutput();
  EXPECT_THAT(output, ::testing::HasSubstr("device1"));
  EXPECT_THAT(output, ::testing::HasSubstr("1.0.0"));
  EXPECT_THAT(output, ::testing::HasSubstr("device2"));
  EXPECT_THAT(output, ::testing::HasSubstr("2.0.0"));
}

// Test printDarwinVersion when command execution fails
TEST_F(FwUtilVersionHandlerTest, PrintDarwinVersionCommandFailure) {
  // Create config with a failing command
  fw_util_config::FwUtilConfig failConfig;
  fw_util_config::FwConfig deviceConfig;
  fw_util_config::VersionConfig version;
  version.versionType() = "full_command";
  version.getVersionCmd() = "false"; // Command that always fails
  deviceConfig.version() = version;
  failConfig.fwConfigs() = {{"failing_device", deviceConfig}};

  std::vector<std::pair<std::string, int>> devices = {{"failing_device", 1}};
  FwUtilVersionHandler handler(devices, failConfig);

  // Should throw when command fails
  EXPECT_THROW(
      handler.printDarwinVersion("failing_device"), std::runtime_error);
}

// ============================================================================
// getSingleVersion() Tests
// ============================================================================

TEST_F(FwUtilVersionHandlerTest, GetSingleVersionDeviceNotFound) {
  FwUtilVersionHandler handler(devicesByPrio_, config_);

  // Should throw for non-existent device
  EXPECT_THAT(
      [&] { handler.getSingleVersion("nonexistent_device"); },
      ::testing::ThrowsMessage<std::runtime_error>(
          ::testing::HasSubstr("--fw_action=list")));
}

TEST_F(FwUtilVersionHandlerTest, GetSingleVersionSysfsPathMissing) {
  fw_util_config::FwUtilConfig sysfsConfig;
  fw_util_config::FwConfig deviceConfig;
  fw_util_config::VersionConfig version;
  const std::string missingPath =
      "/run/devmap/inforoms/NON_EXISTENT_INFO_ROM/fw_ver";
  version.versionType() = "sysfs";
  version.path() = missingPath;
  deviceConfig.version() = version;
  sysfsConfig.fwConfigs() = {{"missing_sysfs", deviceConfig}};

  std::vector<std::pair<std::string, int>> devices = {{"missing_sysfs", 1}};
  FwUtilVersionHandler handler(devices, sysfsConfig);

  try {
    handler.getSingleVersion("missing_sysfs");
    FAIL() << "Expected std::runtime_error";
  } catch (const std::runtime_error& ex) {
    EXPECT_EQ(ex.what(), "Failed to read file: " + missingPath);
  }
}

} // namespace facebook::fboss::platform::fw_util
