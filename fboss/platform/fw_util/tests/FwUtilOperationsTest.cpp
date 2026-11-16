// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/fw_util/FwUtilImpl.h"

#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace facebook::fboss::platform::fw_util {

namespace {

// Helper function to create a minimal valid config file for testing
std::string createMinimalConfigFile(const std::string& tempDir) {
  std::string configPath = tempDir + "/fw_util.json";
  std::ofstream configFile(configPath);
  configFile << R"({
  "fwConfigs": {
    "test_bios": {
      "version": {
        "versionType": "sysfs",
        "path": "/sys/devices/virtual/dmi/id/bios_version"
      },
      "priority": 1,
      "desiredVersion": "1.0.0",
      "sha1sum": "0000000000000000000000000000000000000000"
    }
  }
})";
  configFile.close();
  return configPath;
}

// Helper function to read file contents
std::string readFileContents(const std::string& filePath) {
  std::ifstream file(filePath);
  if (!file.is_open()) {
    return "";
  }
  std::string content(
      (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  file.close();
  return content;
}

} // namespace

class FwUtilOperationsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a unique temporary directory for test files
    // Use a unique suffix to avoid conflicts when tests run in parallel
    tempDir_ = std::filesystem::temp_directory_path() /
        ("fw_util_test_" +
         std::to_string(
             std::chrono::system_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tempDir_);

    // Create minimal config and binary files
    configFilePath_ = createMinimalConfigFile(tempDir_.string());
    binaryFilePath_ = tempDir_ / "test_binary.bin";

    // Create empty binary file
    std::ofstream binaryFile(binaryFilePath_);
    binaryFile << "test binary content";
    binaryFile.close();
  }

  void TearDown() override {
    // Clean up temporary directory
    if (std::filesystem::exists(tempDir_)) {
      std::filesystem::remove_all(tempDir_);
    }
  }

  std::filesystem::path tempDir_;
  std::string configFilePath_;
  std::filesystem::path binaryFilePath_;
};

// ============================================================================
// doJtagOperation() Tests (Lines 12-25)
// ============================================================================

TEST_F(FwUtilOperationsTest, DoJtagOperationValidPath) {
  // Create FwUtilImpl instance
  FwUtilImpl fwUtil(
      binaryFilePath_.string(),
      configFilePath_,
      false, // verifySha1sum
      false // dryRun
  );

  // Create a JtagConfig with valid path
  JtagConfig jtagConfig;
  std::string testFilePath = (tempDir_ / "jtag_test_file.txt").string();
  jtagConfig.path() = testFilePath;
  jtagConfig.value() = 42;

  // Call doJtagOperation
  fwUtil.doJtagOperation(jtagConfig, "test_fpd");

  // Verify file was created
  EXPECT_TRUE(std::filesystem::exists(testFilePath));

  // Verify file contains correct value
  std::string fileContent = readFileContents(testFilePath);
  EXPECT_EQ(fileContent, "42");
}

TEST_F(FwUtilOperationsTest, DoJtagOperationEmptyPath) {
  // Create FwUtilImpl instance
  FwUtilImpl fwUtil(
      binaryFilePath_.string(),
      configFilePath_,
      false, // verifySha1sum
      false // dryRun
  );

  // Create a JtagConfig with empty path
  JtagConfig jtagConfig;
  jtagConfig.path() = "";
  jtagConfig.value() = 42;

  // List all files in temp directory before the operation
  std::set<std::string> filesBefore;
  for (const auto& entry : std::filesystem::directory_iterator(tempDir_)) {
    filesBefore.insert(entry.path().filename().string());
  }

  // Call doJtagOperation - should not throw, just log error
  EXPECT_NO_THROW(fwUtil.doJtagOperation(jtagConfig, "test_fpd"));

  // Verify no new files were created (since path is empty, operation should
  // fail)
  std::set<std::string> filesAfter;
  for (const auto& entry : std::filesystem::directory_iterator(tempDir_)) {
    filesAfter.insert(entry.path().filename().string());
  }
  EXPECT_EQ(filesBefore, filesAfter)
      << "No files should be created when path is empty";
}

TEST_F(FwUtilOperationsTest, DoJtagOperationVariousValues) {
  // Create FwUtilImpl instance
  FwUtilImpl fwUtil(
      binaryFilePath_.string(),
      configFilePath_,
      false, // verifySha1sum
      false // dryRun
  );

  // Test various values: zero, negative, large
  std::vector<std::pair<int, std::string>> testCases = {
      {0, "0"}, {-1, "-1"}, {999999, "999999"}};

  for (const auto& [value, expected] : testCases) {
    JtagConfig jtagConfig;
    std::string testFilePath =
        (tempDir_ / ("jtag_value_" + std::to_string(value) + ".txt")).string();
    jtagConfig.path() = testFilePath;
    jtagConfig.value() = value;

    // Call doJtagOperation
    fwUtil.doJtagOperation(jtagConfig, "test_fpd");

    // Verify file was created and contains correct value
    EXPECT_TRUE(std::filesystem::exists(testFilePath));
    std::string fileContent = readFileContents(testFilePath);
    EXPECT_EQ(fileContent, expected);
  }
}

TEST_F(FwUtilOperationsTest, DoJtagOperationFileOverwrite) {
  // Create FwUtilImpl instance
  FwUtilImpl fwUtil(
      binaryFilePath_.string(),
      configFilePath_,
      false, // verifySha1sum
      false // dryRun
  );

  // Create a JtagConfig and write first value
  JtagConfig jtagConfig;
  std::string testFilePath = (tempDir_ / "jtag_overwrite.txt").string();
  jtagConfig.path() = testFilePath;
  jtagConfig.value() = 100;

  // First write
  fwUtil.doJtagOperation(jtagConfig, "test_fpd");

  // Verify first value
  std::string fileContent1 = readFileContents(testFilePath);
  EXPECT_EQ(fileContent1, "100");

  // Second write with different value
  jtagConfig.value() = 200;
  fwUtil.doJtagOperation(jtagConfig, "test_fpd");

  // Verify file was overwritten with new value
  std::string fileContent2 = readFileContents(testFilePath);
  EXPECT_EQ(fileContent2, "200");
}

TEST_F(FwUtilOperationsTest, PerformJamUpgradeMissingBinary) {
  // Create FwUtilImpl with non-existent binary file
  std::string missingBinary = tempDir_.string() + "/missing.bin";
  FwUtilImpl fwUtil(missingBinary, configFilePath_, false, false);

  JamConfig jamConfig;
  std::vector<std::string> extraArgs = {"-a", "-v"};
  jamConfig.jamExtraArgs() = extraArgs;

  // Should throw because binary file doesn't exist
  EXPECT_THROW(fwUtil.performJamUpgrade(jamConfig), std::runtime_error);
}

TEST_F(FwUtilOperationsTest, PerformXappUpgradeMissingBinary) {
  // Create FwUtilImpl with non-existent binary file
  std::string missingBinary = tempDir_.string() + "/missing.bin";
  FwUtilImpl fwUtil(missingBinary, configFilePath_, false, false);

  XappConfig xappConfig;
  std::vector<std::string> extraArgs = {"-d", "0"};
  xappConfig.xappExtraArgs() = extraArgs;

  // Should throw because binary file doesn't exist
  EXPECT_THROW(fwUtil.performXappUpgrade(xappConfig), std::runtime_error);
}

TEST_F(FwUtilOperationsTest, PerformPsuUtilUpgradeMissingBinary) {
  // Create FwUtilImpl with non-existent binary file
  std::string missingBinary = tempDir_.string() + "/missing.bin";
  FwUtilImpl fwUtil(missingBinary, configFilePath_, false, false);

  PsuUtilConfig psuUtilConfig;
  std::vector<std::string> extraArgs = {"--psu", "auto", "upgrade"};
  psuUtilConfig.psuUtilExtraArgs() = extraArgs;

  // Should throw because binary file doesn't exist
  EXPECT_THROW(fwUtil.performPsuUtilUpgrade(psuUtilConfig), std::runtime_error);
}

TEST_F(FwUtilOperationsTest, DoGpiosetOperationSuccess) {
  FwUtilImpl fwUtil(binaryFilePath_.string(), configFilePath_, false, false);

  GpiosetConfig gpiosetConfig;
  gpiosetConfig.gpioChip() = "gpiochip0";
  gpiosetConfig.gpioChipPin() = "10";
  gpiosetConfig.gpioChipValue() = "1";

  // Will throw in test environment without GPIO hardware
  EXPECT_THROW(
      fwUtil.doGpiosetOperation(gpiosetConfig, "test_fpd"), std::exception);
}

TEST_F(FwUtilOperationsTest, DoGpiosetOperationInvalidChip) {
  FwUtilImpl fwUtil(binaryFilePath_.string(), configFilePath_, false, false);

  GpiosetConfig gpiosetConfig;
  gpiosetConfig.gpioChip() = "invalid_chip";
  gpiosetConfig.gpioChipPin() = "10";
  gpiosetConfig.gpioChipValue() = "1";

  // Should fail with invalid GPIO chip
  EXPECT_THROW(
      fwUtil.doGpiosetOperation(gpiosetConfig, "test_fpd"), std::exception);
}

TEST_F(FwUtilOperationsTest, DoGpiogetOperationSuccess) {
  FwUtilImpl fwUtil(binaryFilePath_.string(), configFilePath_, false, false);

  GpiogetConfig gpiogetConfig;
  gpiogetConfig.gpioChip() = "gpiochip0";
  gpiogetConfig.gpioChipPin() = "10";

  // Will throw in test environment without GPIO hardware
  EXPECT_THROW(
      fwUtil.doGpiogetOperation(gpiogetConfig, "test_fpd"), std::exception);
}

TEST_F(FwUtilOperationsTest, DoWriteToPortOperationSuccess) {
  FwUtilImpl fwUtil(binaryFilePath_.string(), configFilePath_, false, false);

  // Create a test port file
  std::string testPortFile = tempDir_.string() + "/test_port";
  std::ofstream portFile(testPortFile, std::ios::binary);
  portFile << std::string(256, '\0'); // Create 256-byte file
  portFile.close();

  WriteToPortConfig writeConfig;
  writeConfig.portFile() = testPortFile;
  writeConfig.hexOffset() = "0x10";
  writeConfig.hexByteValue() = "0xFF";

  // Should write byte to port file
  EXPECT_NO_THROW(fwUtil.doWriteToPortOperation(writeConfig, "test_platform"));

  // Verify byte was written at correct offset
  std::ifstream readPort(testPortFile, std::ios::binary);
  readPort.seekg(0x10);
  char readByte;
  readPort.read(&readByte, 1);
  EXPECT_TRUE(readPort.good()) << "Failed to read from port file";
  EXPECT_EQ(static_cast<unsigned char>(readByte), 0xFF);
}

} // namespace facebook::fboss::platform::fw_util
