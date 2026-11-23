// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/fw_util/FwUtilImpl.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace facebook::fboss::platform::fw_util {

class FwUtilPostUpgradeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create unique temp directory for this test
    tempDir_ = std::filesystem::temp_directory_path() /
        ("fw_util_postupgrade_test_" +
         std::to_string(
             std::chrono::system_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tempDir_);

    // Create test binary file
    binaryFile_ = (tempDir_ / "test_binary.bin").string();
    std::ofstream binFile(binaryFile_);
    binFile << "test binary content";
    binFile.close();
  }

  void TearDown() override {
    // Clean up temp directory
    if (std::filesystem::exists(tempDir_)) {
      std::filesystem::remove_all(tempDir_);
    }
  }

  void createMinimalConfig(const std::string& configFile) {
    std::ofstream file(configFile);
    file << R"({
      "fwConfigs": {
        "test_device": {
          "version": {
            "versionType": "sysfs",
            "path": "/run/devmap/sensors/test_device/version"
          },
          "priority": 1
        }
      }
    })";
    file.close();
  }

  std::filesystem::path tempDir_;
  std::string binaryFile_;
};

// Test doPostUpgradeOperation with valid args
TEST_F(FwUtilPostUpgradeTest, DoPostUpgradeOperationWithValidArgs) {
  std::string configFile = (tempDir_ / "test_config.json").string();
  createMinimalConfig(configFile);

  FwUtilImpl fwUtil(binaryFile_, configFile, false, false);

  PostFirmwareOperationConfig operation;
  operation.commandType() = "jtag";

  JtagConfig jtagConfig;
  jtagConfig.path() = (tempDir_ / "jtag_test.txt").string();
  jtagConfig.value() = 1;
  operation.jtagArgs() = jtagConfig;

  // Should call doJtagOperation and create file
  EXPECT_NO_THROW(fwUtil.doPostUpgradeOperation(operation, "test_device"));

  // Verify file was created
  EXPECT_TRUE(std::filesystem::exists(jtagConfig.path().value()));
}

// Test doPostUpgradeOperation with invalid configs does nothing
TEST_F(FwUtilPostUpgradeTest, DoPostUpgradeOperationInvalidConfigs) {
  std::string configFile = (tempDir_ / "test_config.json").string();
  createMinimalConfig(configFile);

  FwUtilImpl fwUtil(binaryFile_, configFile, false, false);

  // Test 1: Missing args (command type matches but no args)
  PostFirmwareOperationConfig operation1;
  operation1.commandType() = "jtag";
  EXPECT_NO_THROW(fwUtil.doPostUpgradeOperation(operation1, "test_device"));

  // Test 2: Unsupported command type
  PostFirmwareOperationConfig operation2;
  operation2.commandType() = "unsupported_type";
  EXPECT_NO_THROW(fwUtil.doPostUpgradeOperation(operation2, "test_device"));
}

} // namespace facebook::fboss::platform::fw_util
