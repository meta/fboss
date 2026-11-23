// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/fw_util/FwUtilImpl.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace facebook::fboss::platform::fw_util {

class FwUtilPreUpgradeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create unique temp directory for this test
    tempDir_ = std::filesystem::temp_directory_path() /
        ("fw_util_preupgrade_test_" +
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

// ============================================================================
// doPreUpgradeOperation() Tests
// ============================================================================

// Test empty command type returns early without error
TEST_F(FwUtilPreUpgradeTest, DoPreUpgradeOperationEmptyCommandType) {
  std::string configFile = (tempDir_ / "test_config.json").string();
  createMinimalConfig(configFile);

  FwUtilImpl fwUtil(binaryFile_, configFile, false, false);

  PreFirmwareOperationConfig operation;
  operation.commandType() = "";

  // Empty command type should return early without throwing
  EXPECT_NO_THROW(fwUtil.doPreUpgradeOperation(operation, "test_device"));
}

// Test jtag command type with valid args creates file with correct value
TEST_F(FwUtilPreUpgradeTest, DoPreUpgradeOperationJtagWithArgs) {
  std::string configFile = (tempDir_ / "test_config.json").string();
  createMinimalConfig(configFile);

  FwUtilImpl fwUtil(binaryFile_, configFile, false, false);

  PreFirmwareOperationConfig operation;
  operation.commandType() = "jtag";

  std::string jtagFilePath = (tempDir_ / "jtag_test.txt").string();
  JtagConfig jtagConfig;
  jtagConfig.path() = jtagFilePath;
  jtagConfig.value() = 1;
  operation.jtagArgs() = jtagConfig;

  // Should call doJtagOperation and create file
  EXPECT_NO_THROW(fwUtil.doPreUpgradeOperation(operation, "test_device"));

  // Verify file was created with correct content
  EXPECT_TRUE(std::filesystem::exists(jtagFilePath));
  std::ifstream resultFile(jtagFilePath);
  std::string content;
  std::getline(resultFile, content);
  resultFile.close();
  EXPECT_EQ(content, "1");
}

} // namespace facebook::fboss::platform::fw_util
