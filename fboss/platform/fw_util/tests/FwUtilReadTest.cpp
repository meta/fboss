// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/fw_util/FwUtilImpl.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace facebook::fboss::platform::fw_util {

class FwUtilReadTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create unique temp directory for this test
    tempDir_ = std::filesystem::temp_directory_path() /
        ("fw_util_read_test_" +
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

  void createConfigWithFlashrom(const std::string& configFile) {
    std::ofstream file(configFile);
    file << R"({
      "fwConfigs": {
        "test_device": {
          "version": {
            "versionType": "sysfs",
            "path": "/run/devmap/sensors/test_device/version"
          },
          "priority": 1,
          "upgrade": {
            "flashrom": {
              "programmer_type": "dummy",
              "chip": "dummy_chip"
            }
          }
        }
      }
    })";
    file.close();
  }

  std::filesystem::path tempDir_;
  std::string binaryFile_;
};

// Test performRead with valid flashrom args
TEST_F(FwUtilReadTest, PerformReadFlashromWithArgs) {
  std::string configFile = (tempDir_ / "test_config.json").string();
  createConfigWithFlashrom(configFile);

  FwUtilImpl fwUtil(binaryFile_, configFile, false, false);

  ReadFirmwareOperationConfig readConfig;
  readConfig.commandType() = "flashrom";

  FlashromConfig flashromConfig;
  flashromConfig.programmer_type() = "dummy";
  flashromConfig.chip() = std::vector<std::string>{"dummy_chip"};
  readConfig.flashromArgs() = flashromConfig;

  // Should call performFlashromRead (will fail with dummy config, but that's
  // expected)
  EXPECT_THROW(
      fwUtil.performRead(readConfig, "test_device"), std::runtime_error);
}

// Test performRead with unsupported command type
TEST_F(FwUtilReadTest, PerformReadUnsupportedCommandType) {
  std::string configFile = (tempDir_ / "test_config.json").string();
  createConfigWithFlashrom(configFile);

  FwUtilImpl fwUtil(binaryFile_, configFile, false, false);

  ReadFirmwareOperationConfig readConfig;
  readConfig.commandType() = "unsupported_type";

  // Should throw exception for unsupported command type
  EXPECT_THROW(
      fwUtil.performRead(readConfig, "test_device"), std::runtime_error);
}

} // namespace facebook::fboss::platform::fw_util
