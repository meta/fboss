// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/fw_util/FwUtilImpl.h"

#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace facebook::fboss::platform::fw_util {

namespace {

// Helper function to create a test config file with multiple FPDs
std::string createTestConfigFile(const std::string& tempDir) {
  std::string configPath = tempDir + "/fw_util_test.json";
  std::ofstream configFile(configPath);
  configFile << R"({
  "fwConfigs": {
    "BIOS": {
      "version": {
        "versionType": "Not Applicable"
      },
      "priority": 1,
      "desiredVersion": "1.0.0"
    },
    "BMC": {
      "version": {
        "versionType": "Not Applicable"
      },
      "priority": 2,
      "desiredVersion": "2.0.0"
    },
    "FPGA": {
      "version": {
        "versionType": "Not Applicable"
      },
      "priority": 3
    }
  }
})";
  configFile.close();
  return configPath;
}

} // namespace

class FwUtilImplTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a unique temporary directory for test files
    tempDir_ = std::filesystem::temp_directory_path() /
        ("fw_util_impl_test_" +
         std::to_string(
             std::chrono::system_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tempDir_);

    // Try to create platform name cache file for testing
    try {
      std::filesystem::create_directories("/var/facebook/fboss");
      std::ofstream platformFile("/var/facebook/fboss/platform_name");
      if (platformFile.is_open()) {
        platformFile << "TEST_PLATFORM\n";
        platformFile.close();
        createdPlatformFile_ = true;
      }
    } catch (...) {
      createdPlatformFile_ = false;
    }

    // Create test config and binary files
    configFilePath_ = createTestConfigFile(tempDir_.string());
    binaryFilePath_ = tempDir_ / "test_binary.bin";

    // Create a dummy binary file
    std::ofstream binFile(binaryFilePath_);
    binFile << "test binary content\n";
    binFile.close();
  }

  void TearDown() override {
    // Clean up temporary directory
    if (std::filesystem::exists(tempDir_)) {
      std::filesystem::remove_all(tempDir_);
    }

    // Clean up platform file if we created it
    if (createdPlatformFile_) {
      try {
        std::filesystem::remove("/var/facebook/fboss/platform_name");
      } catch (...) {
        // Ignore cleanup errors
      }
    }
  }

  std::filesystem::path tempDir_;
  std::filesystem::path binaryFilePath_;
  std::string configFilePath_;
  bool createdPlatformFile_ = false;
};

// ============================================================================
// getFpdNameList() Tests
// ============================================================================

TEST_F(FwUtilImplTest, GetFpdNameListReturnsAll) {
  if (!createdPlatformFile_) {
    GTEST_SKIP() << "Skipping test: unable to create platform name file";
  }

  FwUtilImpl fwUtil(binaryFilePath_.string(), configFilePath_, false, false);

  std::vector<std::string> fpdList = fwUtil.getFpdNameList();

  // Should return all 3 FPDs from config
  EXPECT_EQ(fpdList.size(), 3);

  // Verify all expected FPDs are present (order may vary by priority)
  EXPECT_TRUE(
      std::find(fpdList.begin(), fpdList.end(), "BIOS") != fpdList.end());
  EXPECT_TRUE(
      std::find(fpdList.begin(), fpdList.end(), "BMC") != fpdList.end());
  EXPECT_TRUE(
      std::find(fpdList.begin(), fpdList.end(), "FPGA") != fpdList.end());
}

// ============================================================================
// getFpd() Tests
// ============================================================================

TEST_F(FwUtilImplTest, GetFpdCaseInsensitive) {
  if (!createdPlatformFile_) {
    GTEST_SKIP() << "Skipping test: unable to create platform name file";
  }

  FwUtilImpl fwUtil(binaryFilePath_.string(), configFilePath_, false, false);

  // Test case-insensitive matching
  auto [fpdName1, config1] = fwUtil.getFpd("bios");
  EXPECT_EQ(fpdName1, "BIOS");

  auto [fpdName2, config2] = fwUtil.getFpd("BIOS");
  EXPECT_EQ(fpdName2, "BIOS");

  auto [fpdName3, config3] = fwUtil.getFpd("BiOs");
  EXPECT_EQ(fpdName3, "BIOS");
}

TEST_F(FwUtilImplTest, GetFpdReturnsAll) {
  if (!createdPlatformFile_) {
    GTEST_SKIP() << "Skipping test: unable to create platform name file";
  }

  FwUtilImpl fwUtil(binaryFilePath_.string(), configFilePath_, false, false);

  // Test "all" returns special tuple
  auto [fpdName, config] = fwUtil.getFpd("all");
  EXPECT_EQ(fpdName, "all");

  // Test case-insensitive "all"
  auto [fpdName2, config2] = fwUtil.getFpd("ALL");
  EXPECT_EQ(fpdName2, "all");
}

TEST_F(FwUtilImplTest, GetFpdInvalidThrows) {
  if (!createdPlatformFile_) {
    GTEST_SKIP() << "Skipping test: unable to create platform name file";
  }

  FwUtilImpl fwUtil(binaryFilePath_.string(), configFilePath_, false, false);

  // Test invalid FPD name throws exception
  EXPECT_THROW(fwUtil.getFpd("INVALID_FPD"), std::runtime_error);
  EXPECT_THROW(fwUtil.getFpd("nonexistent"), std::runtime_error);
}

// ============================================================================
// doVersionAudit() Tests
// ============================================================================

TEST_F(FwUtilImplTest, DoVersionAuditVersionMismatch) {
  if (!createdPlatformFile_) {
    GTEST_SKIP() << "Skipping test: unable to create platform name file";
  }

  // doVersionAudit calls exit(1) when version mismatch is found
  // BIOS and BMC have desiredVersion that won't match actual version
  // FPGA has no desiredVersion and will be skipped
  EXPECT_EXIT(
      {
        FwUtilImpl fwUtil(
            binaryFilePath_.string(), configFilePath_, false, false);
        fwUtil.doVersionAudit();
      },
      ::testing::ExitedWithCode(1),
      "Firmware version mismatch found");
}

TEST_F(FwUtilImplTest, DoVersionAuditNoDesiredVersion) {
  // No target has a desiredVersion. The audit compares nothing, thus it must
  // not pass.
  std::string noDesiredConfig = tempDir_.string() + "/no_desired.json";
  std::ofstream configFile(noDesiredConfig);
  configFile << R"({
  "fwConfigs": {
    "DeviceA": { "version": { "versionType": "Not Applicable" }, "priority": 1 },
    "DeviceB": { "version": { "versionType": "Not Applicable" }, "priority": 2 }
  }
})";
  configFile.close();

  EXPECT_EXIT(
      {
        FwUtilImpl fwUtil(
            binaryFilePath_.string(), noDesiredConfig, false, false);
        fwUtil.doVersionAudit();
      },
      ::testing::ExitedWithCode(2),
      "No firmware target has a desired version");
}

TEST_F(FwUtilImplTest, DoVersionAuditVersionReadFailure) {
  // The version file does not exist. A version that cannot be read must fail
  // the audit.
  std::string unreadableConfig = tempDir_.string() + "/unreadable.json";
  std::ofstream configFile(unreadableConfig);
  configFile << R"({
  "fwConfigs": {
    "DeviceA": {
      "version": { "versionType": "sysfs", "path": "/run/devmap/none/fw_ver" },
      "priority": 1,
      "desiredVersion": "1.0.0"
    }
  }
})";
  configFile.close();

  EXPECT_EXIT(
      {
        FwUtilImpl fwUtil(
            binaryFilePath_.string(), unreadableConfig, false, false);
        fwUtil.doVersionAudit();
      },
      ::testing::ExitedWithCode(1),
      "Firmware version mismatch found");
}

// ============================================================================
// doFirmwareAction() Tests
// ============================================================================

TEST_F(FwUtilImplTest, DoFirmwareActionInvalidAction) {
  if (!createdPlatformFile_) {
    GTEST_SKIP() << "Skipping test: unable to create platform name file";
  }

  FwUtilImpl fwUtil(binaryFilePath_.string(), configFilePath_, false, false);

  // Invalid action should log error and exit(1)
  EXPECT_EXIT(
      fwUtil.doFirmwareAction("BIOS", "invalid_action"),
      ::testing::ExitedWithCode(1),
      "Invalid action");
}

// ============================================================================
// printVersion() Tests
// ============================================================================

TEST_F(FwUtilImplTest, PrintVersionInvalidFpd) {
  if (!createdPlatformFile_) {
    GTEST_SKIP() << "Skipping test: unable to create platform name file";
  }

  FwUtilImpl fwUtil(binaryFilePath_.string(), configFilePath_, false, false);

  // printVersion for invalid FPD should throw
  EXPECT_THROW(fwUtil.printVersion("INVALID_FPD"), std::runtime_error);
}

// ============================================================================
// Priority Sorting Tests
// ============================================================================

TEST_F(FwUtilImplTest, FpdListSortedByPriority) {
  if (!createdPlatformFile_) {
    GTEST_SKIP() << "Skipping test: unable to create platform name file";
  }

  // Create a config with known priorities to test sorting behavior
  std::string sortTestConfig = tempDir_.string() + "/sort_test.json";
  std::ofstream configFile(sortTestConfig);
  configFile << R"({
  "fwConfigs": {
    "DeviceC": { "version": { "versionType": "Not Applicable" }, "priority": 3 },
    "DeviceA": { "version": { "versionType": "Not Applicable" }, "priority": 1 },
    "DeviceB": { "version": { "versionType": "Not Applicable" }, "priority": 2 }
  }
})";
  configFile.close();

  FwUtilImpl fwUtil(binaryFilePath_.string(), sortTestConfig, false, false);
  std::vector<std::string> fpdList = fwUtil.getFpdNameList();

  // Verify sorting behavior: list should be ordered by ascending priority
  const std::vector<std::string> expected{"DeviceA", "DeviceB", "DeviceC"};
  EXPECT_EQ(fpdList, expected);
}

} // namespace facebook::fboss::platform::fw_util
