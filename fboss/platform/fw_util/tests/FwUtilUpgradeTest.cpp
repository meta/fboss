// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/fw_util/FwUtilImpl.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace facebook::fboss::platform::fw_util {

class FwUtilUpgradeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create unique temp directory for this test
    tempDir_ = std::filesystem::temp_directory_path() /
        ("fw_util_upgrade_test_" +
         std::to_string(
             std::chrono::system_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tempDir_);

    // Create test binary file
    binaryFile_ = (tempDir_ / "test_binary.bin").string();
    std::ofstream binFile(binaryFile_);
    binFile << "test binary content";
    binFile.close();

    // Create platform name file if it doesn't exist
    std::filesystem::path platformPath = "/var/facebook/fboss/platform_name";
    if (!std::filesystem::exists(platformPath)) {
      std::ofstream platformFile(platformPath);
      if (platformFile.is_open()) {
        platformFile << "test_platform";
        platformFile.close();
        createdPlatformFile_ = true;
      }
    }
  }

  void TearDown() override {
    // Clean up temp directory
    if (std::filesystem::exists(tempDir_)) {
      std::filesystem::remove_all(tempDir_);
    }

    // Clean up platform file if we created it
    if (createdPlatformFile_) {
      std::filesystem::remove("/var/facebook/fboss/platform_name");
    }
  }

  void createConfigWithUpgrade(const std::string& configFile) {
    std::ofstream file(configFile);
    file << R"({
      "fwConfigs": {
        "test_device": {
          "version": {
            "versionType": "sysfs",
            "path": "/run/devmap/sensors/test_device/version"
          },
          "priority": 1,
          "sha1sum": "0000000000000000000000000000000000000000",
          "upgrade": [
            {
              "commandType": "flashrom",
              "flashromArgs": {
                "programmer_type": "internal",
                "chip": ["MX25L12805D"]
              }
            }
          ]
        }
      }
    })";
    file.close();
  }

  void createConfigWithPreAndPostUpgrade(
      const std::string& configFile,
      const std::string& portFile) {
    std::ofstream file(configFile);
    file << R"({
      "fwConfigs": {
        "test_device": {
          "version": {
            "versionType": "sysfs",
            "path": "/run/devmap/sensors/test_device/version"
          },
          "priority": 1,
          "preUpgrade": [
            {
              "commandType": "writeToPort",
              "writeToPortArgs": {
                "hexByteValue": "0x1",
                "portFile": ")"
         << portFile << R"(",
                "hexOffset": "0x0"
              }
            }
          ],
          "postUpgrade": [
            {
              "commandType": "writeToPort",
              "writeToPortArgs": {
                "hexByteValue": "0x2",
                "portFile": ")"
         << portFile << R"(",
                "hexOffset": "0x0"
              }
            }
          ]
        }
      }
    })";
    file.close();
  }

  void createConfigWithoutUpgrade(const std::string& configFile) {
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
  bool createdPlatformFile_ = false;
};

// Test doUpgrade with dry run mode enabled
TEST_F(FwUtilUpgradeTest, DoUpgradeDryRunMode) {
  std::string configFile = (tempDir_ / "test_config.json").string();
  createConfigWithUpgrade(configFile);

  FwUtilImpl fwUtil(
      binaryFile_,
      configFile,
      false, // verifySha1sum
      true // dryRun - ENABLED
  );

  // Should return early without throwing
  EXPECT_NO_THROW(fwUtil.doUpgrade("test_device"));
}

TEST_F(FwUtilUpgradeTest, DoFirmwareActionDryRunSkipsPreAndPostUpgrade) {
  std::string configFile = (tempDir_ / "test_config.json").string();
  std::string portFile = (tempDir_ / "upgrade_port").string();
  createConfigWithPreAndPostUpgrade(configFile, portFile);

  FwUtilImpl fwUtil(
      binaryFile_,
      configFile,
      false, // verifySha1sum
      true // dryRun - ENABLED
  );

  EXPECT_NO_THROW(fwUtil.doFirmwareAction("test_device", "program"));
  EXPECT_FALSE(std::filesystem::exists(portFile));
}

// Test doUpgrade with valid upgrade configuration
TEST_F(FwUtilUpgradeTest, DoUpgradeWithValidConfig) {
  std::string configFile = (tempDir_ / "test_config.json").string();
  createConfigWithUpgrade(configFile);

  FwUtilImpl fwUtil(
      binaryFile_,
      configFile,
      false, // verifySha1sum
      false // dryRun
  );

  // Should attempt to call doUpgradeOperation, which will fail with flashrom
  EXPECT_THROW(fwUtil.doUpgrade("test_device"), std::exception);
}

// Test doUpgrade with missing upgrade configuration
TEST_F(FwUtilUpgradeTest, DoUpgradeNoUpgradeConfig) {
  std::string configFile = (tempDir_ / "test_config.json").string();
  createConfigWithoutUpgrade(configFile);

  FwUtilImpl fwUtil(
      binaryFile_,
      configFile,
      false, // verifySha1sum
      false // dryRun
  );

  // Should throw runtime_error for missing upgrade config
  EXPECT_THROW(
      {
        try {
          fwUtil.doUpgrade("test_device");
        } catch (const std::runtime_error& e) {
          EXPECT_THAT(
              e.what(), ::testing::HasSubstr("No upgrade operation found"));
          throw;
        }
      },
      std::runtime_error);
}

} // namespace facebook::fboss::platform::fw_util
