// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include "fboss/platform/fw_util/ConfigValidator.h"

using namespace ::testing;
using namespace facebook::fboss::platform::fw_util;
using namespace facebook::fboss::platform::fw_util_config;

namespace {
FwConfig createValidNewFwConfig() {
  FwConfig fwConfig;

  VersionConfig versionConfig;
  versionConfig.versionType() = "sysfs";
  versionConfig.path() = "/sys/devices/virtual/dmi/id/bios_version";
  fwConfig.version() = versionConfig;

  fwConfig.priority() = 1;
  fwConfig.desiredVersion() = "2.12.g8aeaf1b4";
  fwConfig.sha1sum() = "182f3c1d2d2609f9f0b139aa36fe172193fe491d";

  return fwConfig;
}

} // namespace

TEST(ConfigValidatorTest, ValidConfig) {
  auto config = FwUtilConfig();

  // Create a valid config similar to real configs (darwin, morgan800cc, etc.)
  std::map<std::string, FwConfig> fwConfigs;
  fwConfigs["bios"] = createValidNewFwConfig();

  config.fwConfigs() = fwConfigs;

  EXPECT_TRUE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, EmptyConfig) {
  auto config = FwUtilConfig();

  // Create config with empty fwConfigs map - this should still be valid (eg
  // SEV)
  std::map<std::string, FwConfig> emptyFwConfigs;
  config.fwConfigs() = emptyFwConfigs;

  EXPECT_TRUE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, InvalidConfigEmptyDeviceName) {
  auto config = FwUtilConfig();

  // Create config with empty device name - this should be invalid
  std::map<std::string, FwConfig> fwConfigs;
  fwConfigs[""] = createValidNewFwConfig(); // Empty device name

  config.fwConfigs() = fwConfigs;

  EXPECT_FALSE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, InvalidConfigNegativePriority) {
  auto config = FwUtilConfig();

  // Create config with invalid priority (-1) - this should be invalid
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();
  fwConfig.priority() = -1; // Invalid priority (must be >= 0)
  fwConfigs["bios"] = fwConfig;

  config.fwConfigs() = fwConfigs;

  EXPECT_FALSE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, ValidDelayAfterUpgradeSec) {
  auto config = FwUtilConfig();
  auto fwConfig = createValidNewFwConfig();
  fwConfig.delayAfterUpgradeSec() = 30;
  config.fwConfigs() = {{"bios", fwConfig}};

  EXPECT_TRUE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, InvalidNegativeDelayAfterUpgradeSec) {
  auto config = FwUtilConfig();
  auto fwConfig = createValidNewFwConfig();
  fwConfig.delayAfterUpgradeSec() = -1;
  config.fwConfigs() = {{"bios", fwConfig}};

  EXPECT_FALSE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, InvalidConfigInvalidVersionType) {
  auto config = FwUtilConfig();

  // Create config with invalid version type - this should be invalid
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();
  auto versionConfig = *fwConfig.version();
  versionConfig.versionType() = "invalid_type"; // Invalid version type
  fwConfig.version() = versionConfig;
  fwConfigs["bios"] = fwConfig;

  config.fwConfigs() = fwConfigs;

  EXPECT_FALSE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, InvalidConfigSysfsWithoutPath) {
  auto config = FwUtilConfig();

  // Create config with sysfs version type but no path - this should be invalid
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();
  auto versionConfig = *fwConfig.version();
  versionConfig.versionType() = "sysfs";
  versionConfig.path().reset(); // Remove path (required for sysfs)
  fwConfig.version() = versionConfig;
  fwConfigs["bios"] = fwConfig;

  config.fwConfigs() = fwConfigs;

  EXPECT_FALSE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, InvalidConfigEmptyDesiredVersion) {
  auto config = FwUtilConfig();

  // Create config with empty desiredVersion when provided - this should be
  // invalid
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();
  fwConfig.desiredVersion() = ""; // Empty when provided (invalid)
  fwConfigs["bios"] = fwConfig;

  config.fwConfigs() = fwConfigs;

  EXPECT_FALSE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, InvalidConfigInvalidSha1Sum) {
  auto config = FwUtilConfig();

  // Create config with invalid sha1sum when provided - this should be invalid
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();
  fwConfig.sha1sum() = "invalid_sha1_format"; // Invalid SHA1 format
  fwConfigs["bios"] = fwConfig;

  config.fwConfigs() = fwConfigs;

  EXPECT_FALSE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, ValidPreUpgradeFlashrom) {
  auto config = FwUtilConfig();

  // Create config with valid flashrom preUpgrade (using upgrade section example
  // since flashrom not used in preUpgrade in real configs)
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  std::vector<PreFirmwareOperationConfig> preUpgrade;
  PreFirmwareOperationConfig preUpgradeConfig;
  preUpgradeConfig.commandType() = "flashrom";

  // Based on real config upgrade sections since flashrom not used in preUpgrade
  FlashromConfig flashromConfig;
  flashromConfig.programmer_type() = "linux_spi:dev=";
  flashromConfig.programmer() = "/run/devmap/flashes/SMB_SPI_MASTER_2_DEVICE_1";
  preUpgradeConfig.flashromArgs() = flashromConfig;

  preUpgrade.push_back(preUpgradeConfig);
  fwConfig.preUpgrade() = preUpgrade;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_TRUE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, ValidPreUpgradeJtag) {
  auto config = FwUtilConfig();

  // Create config with valid jtag preUpgrade based on real darwin config
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  std::vector<PreFirmwareOperationConfig> preUpgrade;
  PreFirmwareOperationConfig preUpgradeConfig;
  preUpgradeConfig.commandType() = "jtag";

  // Based on real darwin/darwin48v config
  JtagConfig jtagConfig;
  jtagConfig.path() = "/run/devmap/cplds/BLACKHAWK_CPLD/scd_jtag_sel";
  jtagConfig.value() = 0;
  preUpgradeConfig.jtagArgs() = jtagConfig;

  preUpgrade.push_back(preUpgradeConfig);
  fwConfig.preUpgrade() = preUpgrade;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_TRUE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, ValidPreUpgradeGpioset) {
  auto config = FwUtilConfig();

  // Create config with valid gpioset preUpgrade based on real icecube config
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  std::vector<PreFirmwareOperationConfig> preUpgrade;
  PreFirmwareOperationConfig preUpgradeConfig;
  preUpgradeConfig.commandType() = "gpioset";

  // Based on real icecube config
  GpiosetConfig gpiosetConfig;
  gpiosetConfig.gpioChip() = "fboss_iob_pci.gpiochip.*";
  gpiosetConfig.gpioChipPin() = "66";
  gpiosetConfig.gpioChipValue() = "1";
  preUpgradeConfig.gpiosetArgs() = gpiosetConfig;

  preUpgrade.push_back(preUpgradeConfig);
  fwConfig.preUpgrade() = preUpgrade;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_TRUE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, ValidPreUpgradeWriteToPort) {
  auto config = FwUtilConfig();

  // Create config with valid writeToPort preUpgrade
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  std::vector<PreFirmwareOperationConfig> preUpgrade;
  PreFirmwareOperationConfig preUpgradeConfig;
  preUpgradeConfig.commandType() = "writeToPort";

  WriteToPortConfig writeToPortConfig;
  writeToPortConfig.portFile() = "/sys/kernel/debug/test_port";
  writeToPortConfig.hexByteValue() = "0x12";
  writeToPortConfig.hexOffset() = "0x100";
  preUpgradeConfig.writeToPortArgs() = writeToPortConfig;

  preUpgrade.push_back(preUpgradeConfig);
  fwConfig.preUpgrade() = preUpgrade;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_TRUE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, ValidUpgradeFlashrom) {
  auto config = FwUtilConfig();

  // Create config with valid flashrom upgrade based on real platform configs
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  std::vector<UpgradeConfig> upgrade;
  UpgradeConfig upgradeConfig;
  upgradeConfig.commandType() = "flashrom";

  // Based on real platform flashrom upgrade configurations
  FlashromConfig flashromConfig;
  flashromConfig.programmer_type() = "linux_spi:dev=";
  flashromConfig.programmer() = "/run/devmap/flashes/MCB_SPI_MASTER_4_DEVICE_1";
  upgradeConfig.flashromArgs() = flashromConfig;

  upgrade.push_back(upgradeConfig);
  fwConfig.upgrade() = upgrade;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_TRUE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, ValidPostUpgradeGpioget) {
  auto config = FwUtilConfig();

  // Create config with valid gpioget postUpgrade
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  std::vector<PostFirmwareOperationConfig> postUpgrade;
  PostFirmwareOperationConfig postUpgradeConfig;
  postUpgradeConfig.commandType() = "gpioget";

  // Based on typical GPIO configuration pattern
  GpiogetConfig gpiogetConfig;
  gpiogetConfig.gpioChip() = "fboss_iob_pci.gpiochip.*";
  gpiogetConfig.gpioChipPin() = "66";
  postUpgradeConfig.gpiogetArgs() = gpiogetConfig;

  postUpgrade.push_back(postUpgradeConfig);
  fwConfig.postUpgrade() = postUpgrade;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_TRUE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, ValidVerifyFlashrom) {
  auto config = FwUtilConfig();

  // Create config with valid flashrom verify based on real platform configs
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  VerifyFirmwareOperationConfig verifyConfig;
  verifyConfig.commandType() = "flashrom";

  // Based on real platform flashrom verify configurations
  FlashromConfig flashromConfig;
  flashromConfig.programmer_type() = "internal";
  verifyConfig.flashromArgs() = flashromConfig;

  fwConfig.verify() = verifyConfig;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_TRUE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, ValidReadFlashrom) {
  auto config = FwUtilConfig();

  // Create config with valid flashrom read based on real platform configs
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  ReadFirmwareOperationConfig readConfig;
  readConfig.commandType() = "flashrom";

  // Based on real platform flashrom read configurations
  FlashromConfig flashromConfig;
  flashromConfig.programmer_type() = "linux_spi:dev=";
  flashromConfig.programmer() = "/run/devmap/flashes/MCB_SPI_MASTER_4_DEVICE_1";
  readConfig.flashromArgs() = flashromConfig;

  fwConfig.read() = readConfig;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_TRUE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, OobEepromSkipped) {
  auto config = FwUtilConfig();

  FwConfig oobEepromConfig;
  VersionConfig versionConfig;
  versionConfig.versionType() = "sysfs";
  versionConfig.path() = "/sys/bus/i2c/devices/some_bus/eeprom";
  oobEepromConfig.version() = versionConfig;
  oobEepromConfig.priority() = 1;

  std::vector<UpgradeConfig> upgradeConfigs;
  UpgradeConfig upgradeConfig;
  upgradeConfig.commandType() = "ddDynamicBus";
  upgradeConfigs.push_back(upgradeConfig);
  oobEepromConfig.upgrade() = upgradeConfigs;

  std::map<std::string, FwConfig> fwConfigs;
  fwConfigs["oob_eeprom"] = oobEepromConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_TRUE(ConfigValidator().isValid(config));
}

// ============================================================================
// Missing Args Validation Tests (Parameterized)
// ============================================================================
// These parameterized tests validate that ConfigValidator correctly rejects
// configs where a command type is specified but the required args are missing.

struct PreUpgradeMissingArgsTestCase {
  std::string commandType;
  std::string testName;
};

class PreUpgradeMissingArgsTest
    : public ::testing::TestWithParam<PreUpgradeMissingArgsTestCase> {};

TEST_P(PreUpgradeMissingArgsTest, RejectsConfigWithMissingArgs) {
  auto config = FwUtilConfig();
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  std::vector<PreFirmwareOperationConfig> preUpgrade;
  PreFirmwareOperationConfig preUpgradeConfig;
  preUpgradeConfig.commandType() = GetParam().commandType;
  // No args set for the command type
  preUpgrade.push_back(preUpgradeConfig);
  fwConfig.preUpgrade() = preUpgrade;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_FALSE(ConfigValidator().isValid(config));
}

INSTANTIATE_TEST_SUITE_P(
    ConfigValidatorTest,
    PreUpgradeMissingArgsTest,
    ::testing::Values(
        PreUpgradeMissingArgsTestCase{"flashrom", "Flashrom"},
        PreUpgradeMissingArgsTestCase{"jtag", "Jtag"},
        PreUpgradeMissingArgsTestCase{"gpioset", "Gpioset"},
        PreUpgradeMissingArgsTestCase{"writeToPort", "WriteToPort"}),
    [](const ::testing::TestParamInfo<PreUpgradeMissingArgsTestCase>& info) {
      return info.param.testName;
    });

struct UpgradeMissingArgsTestCase {
  std::string commandType;
  std::string testName;
};

class UpgradeMissingArgsTest
    : public ::testing::TestWithParam<UpgradeMissingArgsTestCase> {};

TEST_P(UpgradeMissingArgsTest, RejectsConfigWithMissingArgs) {
  auto config = FwUtilConfig();
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  std::vector<UpgradeConfig> upgrade;
  UpgradeConfig upgradeConfig;
  upgradeConfig.commandType() = GetParam().commandType;
  // No args set for the command type
  upgrade.push_back(upgradeConfig);
  fwConfig.upgrade() = upgrade;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_FALSE(ConfigValidator().isValid(config));
}

INSTANTIATE_TEST_SUITE_P(
    ConfigValidatorTest,
    UpgradeMissingArgsTest,
    ::testing::Values(
        UpgradeMissingArgsTestCase{"jam", "Jam"},
        UpgradeMissingArgsTestCase{"xapp", "Xapp"},
        UpgradeMissingArgsTestCase{"flashrom", "Flashrom"},
        UpgradeMissingArgsTestCase{"psu_util", "PsuUtil"}),
    [](const ::testing::TestParamInfo<UpgradeMissingArgsTestCase>& info) {
      return info.param.testName;
    });

TEST(ConfigValidatorTest, ValidUpgradeJam) {
  auto config = FwUtilConfig();
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  std::vector<UpgradeConfig> upgrade;
  UpgradeConfig upgradeConfigJam;
  upgradeConfigJam.commandType() = "jam";

  JamConfig jamConfig;
  std::vector<std::string> extraArgs = {"-arg1", "-arg2"};
  jamConfig.jamExtraArgs() = extraArgs;
  upgradeConfigJam.jamArgs() = jamConfig;

  upgrade.push_back(upgradeConfigJam);
  fwConfig.upgrade() = upgrade;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_TRUE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, ValidUpgradeXapp) {
  auto config = FwUtilConfig();
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  std::vector<UpgradeConfig> upgrade;
  UpgradeConfig upgradeConfigXapp;
  upgradeConfigXapp.commandType() = "xapp";

  XappConfig xappConfig;
  std::vector<std::string> extraArgs = {"-arg1", "-arg2"};
  xappConfig.xappExtraArgs() = extraArgs;
  upgradeConfigXapp.xappArgs() = xappConfig;

  upgrade.push_back(upgradeConfigXapp);
  fwConfig.upgrade() = upgrade;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_TRUE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, ValidUpgradePsuUtil) {
  auto config = FwUtilConfig();
  std::map<std::string, FwConfig> fwConfigs;

  FwConfig fwConfig;
  VersionConfig versionConfig;
  versionConfig.versionType() = "full_command";
  versionConfig.getVersionCmd() = "echo 0.4.6";
  fwConfig.version() = versionConfig;
  fwConfig.priority() = 8;

  std::vector<UpgradeConfig> upgrade;
  UpgradeConfig upgradeConfigPsuUtil;
  upgradeConfigPsuUtil.commandType() = "psu_util";

  PsuUtilConfig psuUtilConfig;
  std::vector<std::string> extraArgs = {"--psu", "auto", "upgrade"};
  psuUtilConfig.psuUtilExtraArgs() = extraArgs;
  upgradeConfigPsuUtil.psuUtilArgs() = psuUtilConfig;

  upgrade.push_back(upgradeConfigPsuUtil);
  fwConfig.upgrade() = upgrade;

  fwConfigs["psu"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_TRUE(ConfigValidator().isValid(config));
}

// PostUpgrade missing args tests (parameterized)
struct PostUpgradeMissingArgsTestCase {
  std::string commandType;
  std::string testName;
};

class PostUpgradeMissingArgsTest
    : public ::testing::TestWithParam<PostUpgradeMissingArgsTestCase> {};

TEST_P(PostUpgradeMissingArgsTest, RejectsConfigWithMissingArgs) {
  auto config = FwUtilConfig();
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  std::vector<PostFirmwareOperationConfig> postUpgrade;
  PostFirmwareOperationConfig postUpgradeConfig;
  postUpgradeConfig.commandType() = GetParam().commandType;
  // No args set for the command type
  postUpgrade.push_back(postUpgradeConfig);
  fwConfig.postUpgrade() = postUpgrade;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_FALSE(ConfigValidator().isValid(config));
}

INSTANTIATE_TEST_SUITE_P(
    ConfigValidatorTest,
    PostUpgradeMissingArgsTest,
    ::testing::Values(
        PostUpgradeMissingArgsTestCase{"gpioget", "Gpioget"},
        PostUpgradeMissingArgsTestCase{"jtag", "Jtag"},
        PostUpgradeMissingArgsTestCase{"writeToPort", "WriteToPort"}),
    [](const ::testing::TestParamInfo<PostUpgradeMissingArgsTestCase>& info) {
      return info.param.testName;
    });

// ============================================================================
// Invalid Field Value Tests
// ============================================================================
// The following tests validate that ConfigValidator correctly rejects configs
// where required fields have empty or invalid values. Each test covers a
// different field type (programmer_type, path, chip, pin, file, hex values).
// These tests ensure the validator catches malformed configurations that would
// cause runtime failures if not caught during validation.

TEST(ConfigValidatorTest, InvalidFlashromEmptyProgrammerType) {
  auto config = FwUtilConfig();
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  std::vector<PreFirmwareOperationConfig> preUpgrade;
  PreFirmwareOperationConfig preUpgradeConfigFlashrom;
  preUpgradeConfigFlashrom.commandType() = "flashrom";

  FlashromConfig flashromConfigEmpty;
  flashromConfigEmpty.programmer_type() = ""; // Empty programmer type
  preUpgradeConfigFlashrom.flashromArgs() = flashromConfigEmpty;

  preUpgrade.push_back(preUpgradeConfigFlashrom);
  fwConfig.preUpgrade() = preUpgrade;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_FALSE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, InvalidJtagEmptyPath) {
  auto config = FwUtilConfig();
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  std::vector<PreFirmwareOperationConfig> preUpgrade;
  PreFirmwareOperationConfig preUpgradeConfigJtag;
  preUpgradeConfigJtag.commandType() = "jtag";

  JtagConfig jtagConfigEmpty;
  jtagConfigEmpty.path() = ""; // Empty path
  jtagConfigEmpty.value() = 0;
  preUpgradeConfigJtag.jtagArgs() = jtagConfigEmpty;

  preUpgrade.push_back(preUpgradeConfigJtag);
  fwConfig.preUpgrade() = preUpgrade;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_FALSE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, InvalidGpiosetEmptyChip) {
  auto config = FwUtilConfig();
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  std::vector<PreFirmwareOperationConfig> preUpgrade;
  PreFirmwareOperationConfig preUpgradeConfigGpioset;
  preUpgradeConfigGpioset.commandType() = "gpioset";

  GpiosetConfig gpiosetConfigEmpty;
  gpiosetConfigEmpty.gpioChip() = ""; // Empty chip
  gpiosetConfigEmpty.gpioChipPin() = "66";
  gpiosetConfigEmpty.gpioChipValue() = "1";
  preUpgradeConfigGpioset.gpiosetArgs() = gpiosetConfigEmpty;

  preUpgrade.push_back(preUpgradeConfigGpioset);
  fwConfig.preUpgrade() = preUpgrade;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_FALSE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, InvalidGpiogetEmptyPin) {
  auto config = FwUtilConfig();
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  std::vector<PostFirmwareOperationConfig> postUpgrade;
  PostFirmwareOperationConfig postUpgradeConfigGpioget;
  postUpgradeConfigGpioget.commandType() = "gpioget";

  GpiogetConfig gpiogetConfigEmpty;
  gpiogetConfigEmpty.gpioChip() = "gpiochip0";
  gpiogetConfigEmpty.gpioChipPin() = ""; // Empty pin
  postUpgradeConfigGpioget.gpiogetArgs() = gpiogetConfigEmpty;

  postUpgrade.push_back(postUpgradeConfigGpioget);
  fwConfig.postUpgrade() = postUpgrade;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_FALSE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, InvalidWriteToPortEmptyFile) {
  auto config = FwUtilConfig();
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  std::vector<PreFirmwareOperationConfig> preUpgrade;
  PreFirmwareOperationConfig preUpgradeConfigWriteToPort;
  preUpgradeConfigWriteToPort.commandType() = "writeToPort";

  WriteToPortConfig writeToPortConfigEmpty;
  writeToPortConfigEmpty.portFile() = ""; // Empty port file
  writeToPortConfigEmpty.hexByteValue() = "0x12";
  writeToPortConfigEmpty.hexOffset() = "0x100";
  preUpgradeConfigWriteToPort.writeToPortArgs() = writeToPortConfigEmpty;

  preUpgrade.push_back(preUpgradeConfigWriteToPort);
  fwConfig.preUpgrade() = preUpgrade;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_FALSE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, InvalidWriteToPortInvalidHexValue) {
  auto config = FwUtilConfig();
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  std::vector<PreFirmwareOperationConfig> preUpgrade;
  PreFirmwareOperationConfig preUpgradeConfigWriteToPort;
  preUpgradeConfigWriteToPort.commandType() = "writeToPort";

  WriteToPortConfig writeToPortConfigInvalid;
  writeToPortConfigInvalid.portFile() = "/sys/kernel/debug/test_port";
  writeToPortConfigInvalid.hexByteValue() = "invalid"; // Invalid hex
  writeToPortConfigInvalid.hexOffset() = "0x100";
  preUpgradeConfigWriteToPort.writeToPortArgs() = writeToPortConfigInvalid;

  preUpgrade.push_back(preUpgradeConfigWriteToPort);
  fwConfig.preUpgrade() = preUpgrade;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_FALSE(ConfigValidator().isValid(config));
}

// Note: InvalidUpgradeFlashromMissingArgs is covered by UpgradeMissingArgsTest

TEST(ConfigValidatorTest, InvalidVerifyFlashromMissingArgs) {
  auto config = FwUtilConfig();
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  VerifyFirmwareOperationConfig verifyConfigFlashrom;
  verifyConfigFlashrom.commandType() = "flashrom";
  // No flashromArgs set
  fwConfig.verify() = verifyConfigFlashrom;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_FALSE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, InvalidReadFlashromMissingArgs) {
  auto config = FwUtilConfig();
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  ReadFirmwareOperationConfig readConfigFlashrom;
  readConfigFlashrom.commandType() = "flashrom";
  // No flashromArgs set
  fwConfig.read() = readConfigFlashrom;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_FALSE(ConfigValidator().isValid(config));
}

// Empty extra args tests (parameterized)
struct EmptyExtraArgsTestCase {
  std::string commandType;
  std::string testName;
};

class EmptyExtraArgsTest
    : public ::testing::TestWithParam<EmptyExtraArgsTestCase> {};

TEST_P(EmptyExtraArgsTest, RejectsConfigWithEmptyExtraArgs) {
  auto config = FwUtilConfig();
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  std::vector<UpgradeConfig> upgrade;
  UpgradeConfig upgradeConfig;
  upgradeConfig.commandType() = GetParam().commandType;

  std::vector<std::string> emptyArgs;
  if (GetParam().commandType == "jam") {
    JamConfig jamConfigEmpty;
    jamConfigEmpty.jamExtraArgs() = emptyArgs;
    upgradeConfig.jamArgs() = jamConfigEmpty;
  } else if (GetParam().commandType == "xapp") {
    XappConfig xappConfigEmpty;
    xappConfigEmpty.xappExtraArgs() = emptyArgs;
    upgradeConfig.xappArgs() = xappConfigEmpty;
  }

  upgrade.push_back(upgradeConfig);
  fwConfig.upgrade() = upgrade;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_FALSE(ConfigValidator().isValid(config));
}

INSTANTIATE_TEST_SUITE_P(
    ConfigValidatorTest,
    EmptyExtraArgsTest,
    ::testing::Values(
        EmptyExtraArgsTestCase{"jam", "Jam"},
        EmptyExtraArgsTestCase{"xapp", "Xapp"}),
    [](const ::testing::TestParamInfo<EmptyExtraArgsTestCase>& info) {
      return info.param.testName;
    });

TEST(ConfigValidatorTest, InvalidVersionTypeFullCommandMissingCmd) {
  auto config = FwUtilConfig();
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  auto versionConfigFullCmd = *fwConfig.version();
  versionConfigFullCmd.versionType() = "full_command";
  versionConfigFullCmd.getVersionCmd().reset(); // No getVersionCmd
  fwConfig.version() = versionConfigFullCmd;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_FALSE(ConfigValidator().isValid(config));
}

TEST(ConfigValidatorTest, ValidVersionTypeNotApplicable) {
  auto config = FwUtilConfig();
  std::map<std::string, FwConfig> fwConfigs;
  auto fwConfig = createValidNewFwConfig();

  auto versionConfigNotApplicable = *fwConfig.version();
  versionConfigNotApplicable.versionType() = "Not Applicable";
  versionConfigNotApplicable.path().reset(); // No path needed
  fwConfig.version() = versionConfigNotApplicable;

  fwConfigs["bios"] = fwConfig;
  config.fwConfigs() = fwConfigs;

  EXPECT_TRUE(ConfigValidator().isValid(config));
}
