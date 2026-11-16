// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/fw_util/FwUtilImpl.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace facebook::fboss::platform::fw_util {

class FwUtilVerifyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create unique temp directory for this test
    tempDir_ = std::filesystem::temp_directory_path() /
        ("fw_util_verify_test_" +
         std::to_string(
             std::chrono::system_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tempDir_);

    // Create test config file
    configFile_ = (tempDir_ / "test_config.json").string();
    createTestConfig();
  }

  void TearDown() override {
    // Clean up temp directory
    if (std::filesystem::exists(tempDir_)) {
      std::filesystem::remove_all(tempDir_);
    }
  }

  void createTestConfig() {
    std::ofstream configFile(configFile_);
    configFile << R"({
      "name": "test_device",
      "version": {
        "type": "sysfs",
        "path": "/run/devmap/sensors/test_device/version"
      }
    })";
    configFile.close();
  }

  std::filesystem::path tempDir_;
  std::string configFile_;
};

// Test performVerify with flashrom command type and flashromArgs present
TEST_F(FwUtilVerifyTest, PerformVerifyFlashromWithArgs) {
  FwUtilImpl fwUtil(
      "", // fwBinaryFile
      configFile_,
      false, // verifySha1sum
      false // dryRun
  );

  VerifyFirmwareOperationConfig verifyConfig;
  verifyConfig.commandType() = "flashrom";

  FlashromConfig flashromConfig;
  flashromConfig.programmer() = "dummy";
  flashromConfig.chip() = std::vector<std::string>{"dummy"};
  verifyConfig.flashromArgs() = flashromConfig;

  // Should call performFlashromVerify (expected to fail with dummy config)
  EXPECT_THROW(fwUtil.performVerify(verifyConfig, "test_fpd"), std::exception);
}

// Test performVerify with flashrom command type but no flashromArgs
TEST_F(FwUtilVerifyTest, PerformVerifyFlashromWithoutArgs) {
  FwUtilImpl fwUtil(
      "", // fwBinaryFile
      configFile_,
      false, // verifySha1sum
      false // dryRun
  );

  VerifyFirmwareOperationConfig verifyConfig;
  verifyConfig.commandType() = "flashrom";
  // flashromArgs not set - should return without error

  EXPECT_NO_THROW(fwUtil.performVerify(verifyConfig, "test_fpd"));
}

// Test performVerify with unsupported command type
TEST_F(FwUtilVerifyTest, PerformVerifyUnsupportedCommandType) {
  FwUtilImpl fwUtil(
      "", // fwBinaryFile
      configFile_,
      false, // verifySha1sum
      false // dryRun
  );

  VerifyFirmwareOperationConfig verifyConfig;
  verifyConfig.commandType() = "unsupported_type";

  EXPECT_THROW(
      {
        try {
          fwUtil.performVerify(verifyConfig, "test_fpd");
        } catch (const std::runtime_error& e) {
          EXPECT_THAT(
              e.what(),
              ::testing::HasSubstr("Unsupported verify command type"));
          throw;
        }
      },
      std::runtime_error);
}

} // namespace facebook::fboss::platform::fw_util
