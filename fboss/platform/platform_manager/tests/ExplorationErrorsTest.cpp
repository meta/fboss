// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include "fboss/platform/platform_manager/ExplorationErrors.h"

using namespace facebook::fboss::platform::platform_manager;

TEST(ExplorationErrorsTest, IsExpectedErrorPsuSlot) {
  PlatformConfig platformConfig;

  // Test PSU slot paths with SLOT_PM_UNIT_ABSENCE - should return true
  EXPECT_TRUE(isExpectedError(
      platformConfig,
      ExplorationErrorType::SLOT_PM_UNIT_ABSENCE,
      "/SMB_SLOT@0/PSU_SLOT@0/[PSU_EEPROM]"));
  EXPECT_TRUE(isExpectedError(
      platformConfig,
      ExplorationErrorType::SLOT_PM_UNIT_ABSENCE,
      "/SMB_SLOT@1/PSU_SLOT@1/[PSU_EEPROM]"));
  EXPECT_TRUE(isExpectedError(
      platformConfig,
      ExplorationErrorType::SLOT_PM_UNIT_ABSENCE,
      "/CHASSIS/PSU_SLOT@2/[PSU_DEVICE]"));
  EXPECT_TRUE(isExpectedError(
      platformConfig,
      ExplorationErrorType::SLOT_PM_UNIT_ABSENCE,
      "/PSU_SLOT@0/[PSU_EEPROM]"));
  EXPECT_TRUE(isExpectedError(
      platformConfig,
      ExplorationErrorType::SLOT_PM_UNIT_ABSENCE,
      "/PSU_SLOT@99/[PSU_DEVICE]"));

  // Test PSU slot paths with RUN_DEVMAP_SYMLINK - should also return true
  EXPECT_TRUE(isExpectedError(
      platformConfig,
      ExplorationErrorType::RUN_DEVMAP_SYMLINK,
      "/SMB_SLOT@0/PSU_SLOT@0/[PSU_EEPROM]"));

  // Test non-PSU slot paths - should return false
  EXPECT_FALSE(isExpectedError(
      platformConfig,
      ExplorationErrorType::SLOT_PM_UNIT_ABSENCE,
      "/SMB_SLOT@0/[SCM_EEPROM]"));
  EXPECT_FALSE(isExpectedError(
      platformConfig,
      ExplorationErrorType::SLOT_PM_UNIT_ABSENCE,
      "/[MCB_MUX_A]"));
  EXPECT_FALSE(isExpectedError(
      platformConfig,
      ExplorationErrorType::SLOT_PM_UNIT_ABSENCE,
      "/SMB_SLOT@1/[NVME]"));
  EXPECT_FALSE(isExpectedError(
      platformConfig,
      ExplorationErrorType::SLOT_PM_UNIT_ABSENCE,
      "/CHASSIS/[FAN_CONTROLLER]"));

  // Test edge cases - should return false (paths don't match expected format)
  EXPECT_FALSE(isExpectedError(
      platformConfig,
      ExplorationErrorType::SLOT_PM_UNIT_ABSENCE,
      "/PSU_SLOT@/[PSU_EEPROM]")); // Missing number
  EXPECT_FALSE(isExpectedError(
      platformConfig,
      ExplorationErrorType::SLOT_PM_UNIT_ABSENCE,
      "/PSU_SLOT@abc/[PSU_EEPROM]")); // Non-numeric
  EXPECT_FALSE(isExpectedError(
      platformConfig,
      ExplorationErrorType::SLOT_PM_UNIT_ABSENCE,
      "/SMB_SLOT@0/PSU_SLOT@0/EXTRA/[PSU_EEPROM]")); // PSU_SLOT not at end

  // Test PSU slot with unrelated error type - should return false
  EXPECT_FALSE(isExpectedError(
      platformConfig,
      ExplorationErrorType::I2C_DEVICE_EXPLORE,
      "/PSU_SLOT@0/[PSU_EEPROM]"));
}

TEST(ExplorationErrorsTest, IsExpectedErrorIdprom) {
  // Test IDPROM error on specific platforms
  PlatformConfig montblancConfig;
  montblancConfig.platformName() = "MONTBLANC";
  EXPECT_TRUE(isExpectedError(
      montblancConfig, ExplorationErrorType::IDPROM_READ, "/[IDPROM]"));

  PlatformConfig jangaConfig;
  jangaConfig.platformName() = "JANGA800BIC";
  EXPECT_TRUE(isExpectedError(
      jangaConfig, ExplorationErrorType::IDPROM_READ, "/[IDPROM]"));

  PlatformConfig tahanConfig;
  tahanConfig.platformName() = "TAHAN800BC";
  EXPECT_TRUE(isExpectedError(
      tahanConfig, ExplorationErrorType::IDPROM_READ, "/[IDPROM]"));

  // Test IDPROM error on other platforms - should return false
  PlatformConfig otherConfig;
  otherConfig.platformName() = "OTHER_PLATFORM";
  EXPECT_FALSE(isExpectedError(
      otherConfig, ExplorationErrorType::IDPROM_READ, "/[IDPROM]"));

  // Test IDPROM error on correct platform but wrong path - should return false
  EXPECT_FALSE(isExpectedError(
      montblancConfig, ExplorationErrorType::IDPROM_READ, "/[OTHER_DEVICE]"));

  // Test wrong error type on correct platform/path - should return false
  EXPECT_FALSE(isExpectedError(
      montblancConfig, ExplorationErrorType::I2C_DEVICE_EXPLORE, "/[IDPROM]"));
}

TEST(ExplorationErrorsTest, IsExpectedErrorDarwinIdprom) {
  // Test IDPROM error on DARWIN platform with specific device paths
  PlatformConfig darwinConfig;
  darwinConfig.platformName() = "DARWIN";

  // Test RACKMON_SLOT IDPROM - should return true
  EXPECT_TRUE(isExpectedError(
      darwinConfig,
      ExplorationErrorType::IDPROM_READ,
      "/RACKMON_SLOT@0/[IDPROM]"));

  // Test PEM_SLOT IDPROM - should return true
  EXPECT_TRUE(isExpectedError(
      darwinConfig, ExplorationErrorType::IDPROM_READ, "/PEM_SLOT@0/[IDPROM]"));

  // Test other IDPROM paths on DARWIN - should return false
  EXPECT_FALSE(isExpectedError(
      darwinConfig, ExplorationErrorType::IDPROM_READ, "/[IDPROM]"));

  // Test wrong error type on DARWIN with correct path - should return false
  EXPECT_FALSE(isExpectedError(
      darwinConfig,
      ExplorationErrorType::I2C_DEVICE_EXPLORE,
      "/RACKMON_SLOT@0/[IDPROM]"));
}

TEST(ExplorationErrorsTest, IsExpectedErrorRtmOutletTsensor) {
  // Test RTM outlet tsensor on LADAKH800BCLS platform
  PlatformConfig ladakhConfig;
  ladakhConfig.platformName() = "LADAKH800BCLS";

  // Test I2C_DEVICE_CREATE error type - should return true
  EXPECT_TRUE(isExpectedError(
      ladakhConfig,
      ExplorationErrorType::I2C_DEVICE_CREATE,
      "/RTM_L_SLOT@0/[RTM_L_OUTLET_TSENSOR]"));
  EXPECT_TRUE(isExpectedError(
      ladakhConfig,
      ExplorationErrorType::I2C_DEVICE_CREATE,
      "/RTM_R_SLOT@0/[RTM_R_OUTLET_TSENSOR]"));

  // Test I2C_DEVICE_REG_INIT error type - should return true
  EXPECT_TRUE(isExpectedError(
      ladakhConfig,
      ExplorationErrorType::I2C_DEVICE_REG_INIT,
      "/RTM_L_SLOT@0/[RTM_L_OUTLET_TSENSOR]"));
  EXPECT_TRUE(isExpectedError(
      ladakhConfig,
      ExplorationErrorType::I2C_DEVICE_REG_INIT,
      "/RTM_R_SLOT@0/[RTM_R_OUTLET_TSENSOR]"));

  // Test RUN_DEVMAP_SYMLINK error type - should return true
  EXPECT_TRUE(isExpectedError(
      ladakhConfig,
      ExplorationErrorType::RUN_DEVMAP_SYMLINK,
      "/RTM_L_SLOT@0/[RTM_L_OUTLET_TSENSOR]"));
  EXPECT_TRUE(isExpectedError(
      ladakhConfig,
      ExplorationErrorType::RUN_DEVMAP_SYMLINK,
      "/RTM_R_SLOT@0/[RTM_R_OUTLET_TSENSOR]"));

  // Test on other platforms - should return false
  PlatformConfig otherConfig;
  otherConfig.platformName() = "OTHER_PLATFORM";
  EXPECT_FALSE(isExpectedError(
      otherConfig,
      ExplorationErrorType::I2C_DEVICE_CREATE,
      "/RTM_L_SLOT@0/[RTM_L_OUTLET_TSENSOR]"));
  EXPECT_FALSE(isExpectedError(
      otherConfig,
      ExplorationErrorType::RUN_DEVMAP_SYMLINK,
      "/RTM_R_SLOT@0/[RTM_R_OUTLET_TSENSOR]"));

  // Test with unrelated error type on LADAKH800BCLS - should return false
  EXPECT_FALSE(isExpectedError(
      ladakhConfig,
      ExplorationErrorType::I2C_DEVICE_EXPLORE,
      "/RTM_L_SLOT@0/[RTM_L_OUTLET_TSENSOR]"));
  EXPECT_FALSE(isExpectedError(
      ladakhConfig,
      ExplorationErrorType::SLOT_PM_UNIT_ABSENCE,
      "/RTM_R_SLOT@0/[RTM_R_OUTLET_TSENSOR]"));

  // Test with wrong device path on LADAKH800BCLS - should return false
  EXPECT_FALSE(isExpectedError(
      ladakhConfig,
      ExplorationErrorType::I2C_DEVICE_CREATE,
      "/RTM_L_SLOT@0/[RTM_L_TSENSOR_1]"));
  EXPECT_FALSE(isExpectedError(
      ladakhConfig,
      ExplorationErrorType::RUN_DEVMAP_SYMLINK,
      "/RTM_R_SLOT@0/[RTM_R_TSENSOR_2]"));
}

TEST(ExplorationErrorsTest, IsExpectedErrorM4062nhpIdprom) {
  PlatformConfig m4062nhpConfig;
  m4062nhpConfig.platformName() = "M4062NHP";

  // Both mezzanine EEPROMs may fail to parse
  EXPECT_TRUE(isExpectedError(
      m4062nhpConfig,
      ExplorationErrorType::IDPROM_READ,
      "/SMB_SLOT@0/[MEZZ_BOTTOM_EEPROM]"));
  EXPECT_TRUE(isExpectedError(
      m4062nhpConfig,
      ExplorationErrorType::IDPROM_READ,
      "/SMB_SLOT@0/[MEZZ_TOP_EEPROM]"));

  // Other platforms do not get the mezzanine exemption
  PlatformConfig otherConfig;
  otherConfig.platformName() = "OTHER_PLATFORM";
  EXPECT_FALSE(isExpectedError(
      otherConfig,
      ExplorationErrorType::IDPROM_READ,
      "/SMB_SLOT@0/[MEZZ_BOTTOM_EEPROM]"));

  // Only IDPROM_READ is tolerated on the mezzanine paths
  EXPECT_FALSE(isExpectedError(
      m4062nhpConfig,
      ExplorationErrorType::I2C_DEVICE_CREATE,
      "/SMB_SLOT@0/[MEZZ_TOP_EEPROM]"));

  // Other devices on the same slot still count as real errors
  EXPECT_FALSE(isExpectedError(
      m4062nhpConfig,
      ExplorationErrorType::IDPROM_READ,
      "/SMB_SLOT@0/[DCDC1]"));
}
