// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <chrono>

#include <gtest/gtest.h>

#include "fboss/platform/platform_manager/Utils.h"

using namespace facebook::fboss::platform::platform_manager;

namespace {
std::pair<std::string, std::string> makeDevicePathPair(
    std::string slotPath,
    std::string deviceName) {
  return std::make_pair(slotPath, deviceName);
}
} // namespace

TEST(UtilsTest, ParseDevicePath) {
  EXPECT_EQ(
      makeDevicePathPair("/", "IDPROM"), Utils().parseDevicePath("/[IDPROM]"));
  EXPECT_EQ(
      makeDevicePathPair("/MCB_SLOT@0", "sensor"),
      Utils().parseDevicePath("/MCB_SLOT@0/[sensor]"));
  EXPECT_EQ(
      makeDevicePathPair("/MCB_SLOT@0/SMB_SLOT@11", "SMB_IOB_I2C_1"),
      Utils().parseDevicePath("/MCB_SLOT@0/SMB_SLOT@11/[SMB_IOB_I2C_1]"));
  EXPECT_NO_THROW(Utils().parseDevicePath("ABCDE/[abc]"));
  EXPECT_NO_THROW(Utils().parseDevicePath("/MCB_SLOT/[abc]"));
  EXPECT_NO_THROW(Utils().parseDevicePath("/MCB_SLOT@1/[]"));
}

TEST(UtilsTest, CreateDevicePath) {
  Utils utils;
  EXPECT_EQ("/[IDPROM]", utils.createDevicePath("/", "IDPROM"));
  EXPECT_EQ(
      "/MCB_SLOT@0/[sensor]", utils.createDevicePath("/MCB_SLOT@0", "sensor"));
  EXPECT_EQ(
      "/MCB_SLOT@0/SMB_SLOT@11/[SMB_IOB_I2C_1]",
      utils.createDevicePath("/MCB_SLOT@0/SMB_SLOT@11", "SMB_IOB_I2C_1"));
}

TEST(UtilsTest, ComputeHexExpression) {
  Utils utils;

  // Test simple arithmetic expressions
  EXPECT_EQ("0xa", utils.computeHexExpression("5 + 5", 1, 2));
  EXPECT_EQ("0x14", utils.computeHexExpression("10 * 2", 1, 2));
  EXPECT_EQ("0x0", utils.computeHexExpression("10 - 10", 1, 2));
  EXPECT_EQ("0x5", utils.computeHexExpression("15 / 3", 1, 2));

  // Test with hex literals that get converted to decimal
  EXPECT_EQ("0x20", utils.computeHexExpression("0x10 + 0x10", 1, 2));
  EXPECT_EQ("0xff", utils.computeHexExpression("0xf0 + 0xf", 1, 2));
  EXPECT_EQ("0x100", utils.computeHexExpression("0xff + 1", 1, 2));

  // Test with port and led parameters, no startPort
  EXPECT_EQ("0x3", utils.computeHexExpression("{portNum} + {ledNum}", 1, 0, 2));
  EXPECT_EQ(
      "0x14", utils.computeHexExpression("{portNum} * {ledNum}", 4, 0, 5));
  EXPECT_EQ(
      "0x1a",
      utils.computeHexExpression("0x10 + {portNum} * {ledNum}", 2, 0, 5));

  // Test complex expressions
  EXPECT_EQ(
      "0x6c",
      utils.computeHexExpression("0x64 + {portNum} + {ledNum}", 4, 0, 4));

  // Test with zero values
  EXPECT_EQ("0x0", utils.computeHexExpression("{portNum} + {ledNum}", 0, 0, 0));

  // Test with multiple operations
  EXPECT_EQ(
      "0x40418",
      utils.computeHexExpression(
          "0x40410 + ({portNum} - {startPort})*0x8 + ({ledNum} - 1)*0x4",
          2,
          1,
          1));

  // Test with multiple operations
  EXPECT_EQ(
      "0x48474",
      utils.computeHexExpression(
          "0x48410 + ({portNum} - {startPort})*0x8 + ({ledNum} - 1)*0x4",
          45,
          33,
          2));

  // Test with multiple operations
  EXPECT_EQ(
      "0x65e0",
      utils.computeHexExpression(
          "0x65c0 + ({portNum} - {startPort})*0x10 + ({ledNum} - 1)*0x10",
          39,
          39,
          3));

  // Test with invalid expression - should throw
  EXPECT_THROW(
      utils.computeHexExpression("invalid_expression", 1, 2, 1),
      std::runtime_error);

  // Test with optional led parameter (led = std::nullopt)
  // Expression without {ledNum} placeholder
  EXPECT_EQ("0x64", utils.computeHexExpression("0x64", 1));
  EXPECT_EQ("0x69", utils.computeHexExpression("0x64 + {portNum}", 5));
  EXPECT_EQ("0x6e", utils.computeHexExpression("0x64 + {portNum} * 2", 5));

  // Test with startPort but no led
  EXPECT_EQ(
      "0x10",
      utils.computeHexExpression("0x10 + ({portNum} - {startPort})", 5, 5));
  EXPECT_EQ(
      "0x18",
      utils.computeHexExpression(
          "0x10 + ({portNum} - {startPort}) * 0x8", 2, 1));
  EXPECT_EQ(
      "0x40420",
      utils.computeHexExpression(
          "0x40410 + ({portNum} - {startPort}) * 0x8", 3, 1));

  // Test expressions that work with both led present and absent
  // With led
  EXPECT_EQ(
      "0x40418",
      utils.computeHexExpression(
          "0x40410 + ({portNum} - {startPort})*0x8 + ({ledNum} - 1)*0x4",
          2,
          1,
          1));
  // Without led (different expression)
  EXPECT_EQ(
      "0x40418",
      utils.computeHexExpression(
          "0x40410 + ({portNum} - {startPort})*0x8", 2, 1));

  // Test with zero port value and no led
  EXPECT_EQ("0x0", utils.computeHexExpression("{portNum}", 0));
  EXPECT_EQ("0xa", utils.computeHexExpression("{portNum}", 10));

  // Test complex expression without led
  EXPECT_EQ(
      "0x65d0",
      utils.computeHexExpression(
          "0x65c0 + ({portNum} - {startPort})*0x10", 40, 39));
}

TEST(UtilsTest, ConvertHexLiteralsToDecimal) {
  Utils utils;

  // Test basic hex to decimal conversion
  EXPECT_EQ("255", utils.convertHexLiteralsToDecimal("0xff"));
  EXPECT_EQ("16", utils.convertHexLiteralsToDecimal("0x10"));
  EXPECT_EQ("0", utils.convertHexLiteralsToDecimal("0x0"));
  EXPECT_EQ("4095", utils.convertHexLiteralsToDecimal("0xfff"));

  // Test multiple hex literals in expression
  EXPECT_EQ("255 + 16", utils.convertHexLiteralsToDecimal("0xff + 0x10"));
  EXPECT_EQ(
      "255 * 16 - 1", utils.convertHexLiteralsToDecimal("0xff * 0x10 - 0x1"));

  // Test mixed case hex literals
  EXPECT_EQ("255", utils.convertHexLiteralsToDecimal("0xFF"));
  EXPECT_EQ("255", utils.convertHexLiteralsToDecimal("0xFf"));
  EXPECT_EQ("171", utils.convertHexLiteralsToDecimal("0xAb"));

  // Test expressions without hex literals
  EXPECT_EQ("5 + 3", utils.convertHexLiteralsToDecimal("5 + 3"));
  EXPECT_EQ("test_string", utils.convertHexLiteralsToDecimal("test_string"));
  EXPECT_EQ("", utils.convertHexLiteralsToDecimal(""));

  // Test hex literals in complex expressions
  EXPECT_EQ(
      "255 + @port * 16",
      utils.convertHexLiteralsToDecimal("0xff + @port * 0x10"));
}

TEST(UtilsTest, FormatExpression) {
  Utils utils;
  EXPECT_EQ(
      "0x1000 + (1 - 1)*0x4",
      utils.formatExpression(
          "0x1000 + ({portNum} - {startPort})*0x4", 1, 1, std::nullopt));
  EXPECT_EQ(
      "0x1000 + (2 - 1)*0x8 + (2 - 1)*0x4",
      utils.formatExpression(
          "0x1000 + ({portNum} - {startPort})*0x8 + ({ledNum} - 1)*0x4",
          2,
          1,
          2));
  EXPECT_EQ("5", utils.formatExpression("{portNum}", 5, 0, std::nullopt));
  EXPECT_EQ("3", utils.formatExpression("{ledNum}", 0, 0, 3));
}

TEST(UtilsTest, CheckDeviceReadiness) {
  Utils utils;

  EXPECT_TRUE(utils.checkDeviceReadiness(
      []() { return true; }, "ready", std::chrono::seconds(1)));
  EXPECT_FALSE(utils.checkDeviceReadiness(
      []() { return false; }, "not ready", std::chrono::seconds(0)));

  int callCount = 0;
  EXPECT_TRUE(utils.checkDeviceReadiness(
      [&callCount]() { return ++callCount >= 3; },
      "waiting",
      std::chrono::seconds(5)));
  EXPECT_GE(callCount, 3);
}

TEST(UtilsTest, CreateXcvrCtrlConfigs) {
  PciDeviceConfig pciDeviceConfig;
  pciDeviceConfig.xcvrCtrlBlockConfigs() = {};
  EXPECT_TRUE(Utils::createXcvrCtrlConfigs(pciDeviceConfig).empty());

  XcvrCtrlBlockConfig xcvrBlock;
  xcvrBlock.pmUnitScopedNamePrefix() = "SMB_XCVR";
  xcvrBlock.deviceName() = "fbiob-xcvr";
  xcvrBlock.csrOffsetCalc() = "0x1000 + ({portNum} - {startPort})*0x4";
  xcvrBlock.numPorts() = 2;
  xcvrBlock.startPort() = 1;
  xcvrBlock.iobufOffsetCalc() = "0x2000 + ({portNum} - {startPort})*0x4";
  pciDeviceConfig.xcvrCtrlBlockConfigs() = {xcvrBlock};

  auto configs = Utils::createXcvrCtrlConfigs(pciDeviceConfig);
  EXPECT_EQ(2, configs.size());
  EXPECT_EQ(
      "SMB_XCVR_XCVR_CTRL_PORT_1",
      *configs[0].fpgaIpBlockConfig()->pmUnitScopedName());
  EXPECT_EQ("0x1000", *configs[0].fpgaIpBlockConfig()->csrOffset());
  EXPECT_EQ("0x2000", *configs[0].fpgaIpBlockConfig()->iobufOffset());
  EXPECT_EQ(1, *configs[0].portNumber());
  EXPECT_EQ("0x1004", *configs[1].fpgaIpBlockConfig()->csrOffset());
  EXPECT_EQ(2, *configs[1].portNumber());

  xcvrBlock.iobufOffsetCalc() = "";
  pciDeviceConfig.xcvrCtrlBlockConfigs() = {xcvrBlock};
  configs = Utils::createXcvrCtrlConfigs(pciDeviceConfig);
  EXPECT_TRUE(configs[0].fpgaIpBlockConfig()->iobufOffset()->empty());
}

TEST(UtilsTest, CreateLedCtrlConfigs) {
  PciDeviceConfig pciDeviceConfig;
  pciDeviceConfig.ledCtrlBlockConfigs() = {};
  EXPECT_TRUE(Utils::createLedCtrlConfigs(pciDeviceConfig).empty());

  LedCtrlBlockConfig ledBlock;
  ledBlock.pmUnitScopedNamePrefix() = "LED_CTRL";
  ledBlock.deviceName() = "fbiob-led";
  ledBlock.csrOffsetCalc() =
      "0x1000 + ({portNum} - {startPort})*0x8 + ({ledNum} - 1)*0x4";
  ledBlock.numPorts() = 2;
  ledBlock.ledPerPort() = 2;
  ledBlock.startPort() = 1;
  ledBlock.iobufOffsetCalc() =
      "0x2000 + ({portNum} - {startPort})*0x8 + ({ledNum} - 1)*0x4";
  pciDeviceConfig.ledCtrlBlockConfigs() = {ledBlock};

  auto configs = Utils::createLedCtrlConfigs(pciDeviceConfig);
  EXPECT_EQ(4, configs.size());
  EXPECT_EQ(
      "LED_CTRL_PORT_1_LED_1",
      *configs[0].fpgaIpBlockConfig()->pmUnitScopedName());
  EXPECT_EQ("0x1000", *configs[0].fpgaIpBlockConfig()->csrOffset());
  EXPECT_EQ("0x2000", *configs[0].fpgaIpBlockConfig()->iobufOffset());
  EXPECT_EQ(1, *configs[0].portNumber());
  EXPECT_EQ(1, *configs[0].ledId());
  EXPECT_EQ("0x100c", *configs[3].fpgaIpBlockConfig()->csrOffset());
  EXPECT_EQ(2, *configs[3].portNumber());
  EXPECT_EQ(2, *configs[3].ledId());

  ledBlock.iobufOffsetCalc() = "";
  pciDeviceConfig.ledCtrlBlockConfigs() = {ledBlock};
  configs = Utils::createLedCtrlConfigs(pciDeviceConfig);
  EXPECT_TRUE(configs[0].fpgaIpBlockConfig()->iobufOffset()->empty());
}

TEST(UtilsTest, CreateMdioBusConfigs) {
  PciDeviceConfig pciDeviceConfig;
  pciDeviceConfig.mdioBusBlockConfigs() = {};
  EXPECT_TRUE(Utils::createMdioBusConfigs(pciDeviceConfig).empty());

  MdioBusBlockConfig mdioBlock;
  mdioBlock.pmUnitScopedNamePrefix() = "MDIO_BUS";
  mdioBlock.deviceName() = "fbiob-mdio";
  mdioBlock.csrOffsetCalc() = "0x200 + {busIndex}*0x20";
  mdioBlock.numBuses() = 2;
  pciDeviceConfig.mdioBusBlockConfigs() = {mdioBlock};

  auto configs = Utils::createMdioBusConfigs(pciDeviceConfig);
  EXPECT_EQ(2, configs.size());
  EXPECT_EQ("MDIO_BUS_1", *configs[0].pmUnitScopedName());
  EXPECT_EQ("fbiob-mdio", *configs[0].deviceName());
  EXPECT_EQ("0x200", *configs[0].csrOffset());
  EXPECT_EQ("MDIO_BUS_2", *configs[1].pmUnitScopedName());
  EXPECT_EQ("0x220", *configs[1].csrOffset());
}

TEST(UtilsTest, CreateRtmCtrlConfigs) {
  PciDeviceConfig pciDeviceConfig;
  pciDeviceConfig.rtmCtrlBlockConfigs() = {};
  EXPECT_TRUE(Utils::createRtmCtrlConfigs(pciDeviceConfig).empty());

  RtmCtrlBlockConfig rtmBlock;
  rtmBlock.pmUnitScopedNamePrefix() = "RTM_L_MDIO_1";
  rtmBlock.deviceName() = "fbiob-rtm";
  rtmBlock.csrOffsetCalc() = "0x1000 + ({portNum} - {startPort})*0x4";
  rtmBlock.numPorts() = 2;
  rtmBlock.startPort() = 1;
  rtmBlock.iobufOffsetCalc() = "0x2000 + ({portNum} - {startPort})*0x4";
  pciDeviceConfig.rtmCtrlBlockConfigs() = {rtmBlock};

  auto configs = Utils::createRtmCtrlConfigs(pciDeviceConfig);
  EXPECT_EQ(2, configs.size());
  EXPECT_EQ(
      "RTM_L_MDIO_1_RTM_CTRL_PORT_1",
      *configs[0].fpgaIpBlockConfig()->pmUnitScopedName());
  EXPECT_EQ(
      "RTM_L_MDIO_1_RTM_CTRL_PORT_2",
      *configs[1].fpgaIpBlockConfig()->pmUnitScopedName());
  EXPECT_EQ("0x1000", *configs[0].fpgaIpBlockConfig()->csrOffset());
  EXPECT_EQ("0x1004", *configs[1].fpgaIpBlockConfig()->csrOffset());
  EXPECT_EQ(1, *configs[0].portNumber());
  EXPECT_EQ(2, *configs[1].portNumber());
  EXPECT_EQ("0x2000", *configs[0].fpgaIpBlockConfig()->iobufOffset());
  EXPECT_EQ("0x2004", *configs[1].fpgaIpBlockConfig()->iobufOffset());

  rtmBlock.iobufOffsetCalc() = "";
  pciDeviceConfig.rtmCtrlBlockConfigs() = {rtmBlock};
  configs = Utils::createRtmCtrlConfigs(pciDeviceConfig);
  EXPECT_TRUE(configs[0].fpgaIpBlockConfig()->iobufOffset()->empty());
  EXPECT_TRUE(configs[1].fpgaIpBlockConfig()->iobufOffset()->empty());
}

namespace {
PmUnitVersion makeVersion(
    int16_t productionState,
    int16_t productionSubState,
    int16_t respinVariantIndicator) {
  PmUnitVersion version;
  version.productionState() = productionState;
  version.productionSubState() = productionSubState;
  version.respinVariantIndicator() = respinVariantIndicator;
  return version;
}

// A PlatformConfig with one PmUnit "SMB" whose default config has no I2C
// devices and whose versioned config has one.
PlatformConfig makeVersionedPlatformConfig(
    const VersionedPmUnitConfig& versionedPmUnitConfig) {
  PlatformConfig config;
  config.pmUnitConfigs() = {{"SMB", PmUnitConfig()}};
  config.versionedPmUnitConfigs() = {{"SMB", {versionedPmUnitConfig}}};
  return config;
}

VersionedPmUnitConfig makeVersionedPmUnitConfig() {
  VersionedPmUnitConfig versionedPmUnitConfig;
  versionedPmUnitConfig.pmUnitConfig()->i2cDeviceConfigs() = {
      I2cDeviceConfig()};
  return versionedPmUnitConfig;
}

bool isVersionedConfig(const PmUnitConfig& pmUnitConfig) {
  return pmUnitConfig.i2cDeviceConfigs()->size() == 1;
}
} // namespace

TEST(UtilsTest, ResolvePmUnitConfigMatchesProductSubVersion) {
  auto versioned = makeVersionedPmUnitConfig();
  versioned.productSubVersion() = 10;
  auto config = makeVersionedPlatformConfig(versioned);

  // productSubVersion is matched against RespinVariantIndicator alone.
  EXPECT_TRUE(isVersionedConfig(
      Utils::resolvePmUnitConfig(config, "SMB", makeVersion(3, 1, 10))));
  EXPECT_TRUE(isVersionedConfig(
      Utils::resolvePmUnitConfig(config, "SMB", makeVersion(9, 9, 10))));
  EXPECT_FALSE(isVersionedConfig(
      Utils::resolvePmUnitConfig(config, "SMB", makeVersion(3, 1, 11))));
}

TEST(UtilsTest, ResolvePmUnitConfigMatchesPmUnitVersions) {
  auto versioned = makeVersionedPmUnitConfig();
  // productSubVersion is ignored when pmUnitVersions is present.
  versioned.productSubVersion() = 10;
  versioned.pmUnitVersions() = {makeVersion(4, 1, 10)};
  auto config = makeVersionedPlatformConfig(versioned);

  EXPECT_TRUE(isVersionedConfig(
      Utils::resolvePmUnitConfig(config, "SMB", makeVersion(4, 1, 10))));
  EXPECT_FALSE(isVersionedConfig(
      Utils::resolvePmUnitConfig(config, "SMB", makeVersion(3, 1, 10))));
}

TEST(UtilsTest, ResolvePmUnitConfigWithoutVersion) {
  auto versioned = makeVersionedPmUnitConfig();
  versioned.productSubVersion() = 10;
  auto config = makeVersionedPlatformConfig(versioned);

  EXPECT_FALSE(isVersionedConfig(
      Utils::resolvePmUnitConfig(config, "SMB", std::nullopt)));
}

// A version supplied for a PmUnit that does not exist -- eg. a typo in
// --pm_unit_version -- must fail loudly rather than silently resolving that
// PmUnit to its default config.
TEST(UtilsTest, ResolvePmUnitConfigsRejectsUnknownPmUnit) {
  auto versioned = makeVersionedPmUnitConfig();
  versioned.productSubVersion() = 10;
  auto config = makeVersionedPlatformConfig(versioned);
  config.platformName() = "sample";

  EXPECT_THROW(
      Utils::resolvePmUnitConfigs(config, {{"SBM", makeVersion(3, 1, 10)}}),
      std::invalid_argument);
}

TEST(UtilsTest, ResolvePmUnitConfigsAppliesVersionsPerPmUnit) {
  auto versioned = makeVersionedPmUnitConfig();
  versioned.productSubVersion() = 10;
  auto config = makeVersionedPlatformConfig(versioned);
  config.pmUnitConfigs()["SCM"] = PmUnitConfig();

  auto resolved =
      Utils::resolvePmUnitConfigs(config, {{"SMB", makeVersion(3, 1, 10)}});

  EXPECT_EQ(resolved.size(), config.pmUnitConfigs()->size());
  EXPECT_TRUE(isVersionedConfig(resolved.at("SMB")));
  // SCM has no detected version, so it keeps its default config.
  EXPECT_FALSE(isVersionedConfig(resolved.at("SCM")));
}

// Callers that detect no versions must observe exactly the pre-versioning
// behaviour, so that adding version resolution cannot alter any existing
// caller or platform.
TEST(UtilsTest, ResolvePmUnitConfigsWithoutVersionsReturnsDefaults) {
  auto versioned = makeVersionedPmUnitConfig();
  versioned.productSubVersion() = 10;
  auto config = makeVersionedPlatformConfig(versioned);
  config.pmUnitConfigs()["SCM"] = PmUnitConfig();

  const std::map<std::string, PmUnitConfig> expected(
      config.pmUnitConfigs()->begin(), config.pmUnitConfigs()->end());
  EXPECT_EQ(Utils::resolvePmUnitConfigs(config, {}), expected);
}

TEST(UtilsTest, ResolvePmUnitConfigSelectsMatchingVersionedEntry) {
  auto unmatched = makeVersionedPmUnitConfig();
  unmatched.productSubVersion() = 9;
  unmatched.pmUnitConfig()->i2cDeviceConfigs() = {};
  auto matched = makeVersionedPmUnitConfig();
  matched.productSubVersion() = 11;

  auto config = makeVersionedPlatformConfig(unmatched);
  config.versionedPmUnitConfigs()->at("SMB").push_back(matched);

  EXPECT_TRUE(isVersionedConfig(
      Utils::resolvePmUnitConfig(config, "SMB", makeVersion(3, 1, 11))));
}

TEST(UtilsTest, ResolvePmUnitConfigMatchesAnyDeclaredPmUnitVersion) {
  auto versioned = makeVersionedPmUnitConfig();
  versioned.pmUnitVersions() = {makeVersion(4, 1, 10), makeVersion(5, 0, 2)};
  auto config = makeVersionedPlatformConfig(versioned);

  EXPECT_TRUE(isVersionedConfig(
      Utils::resolvePmUnitConfig(config, "SMB", makeVersion(5, 0, 2))));
  EXPECT_FALSE(isVersionedConfig(
      Utils::resolvePmUnitConfig(config, "SMB", makeVersion(5, 0, 3))));
}

// An empty pmUnitVersions list is not a declaration of "matches nothing"; the
// entry falls back to the legacy productSubVersion form.
TEST(UtilsTest, ResolvePmUnitConfigEmptyPmUnitVersionsUsesProductSubVersion) {
  auto versioned = makeVersionedPmUnitConfig();
  versioned.productSubVersion() = 10;
  versioned.pmUnitVersions() = {};
  auto config = makeVersionedPlatformConfig(versioned);

  EXPECT_TRUE(isVersionedConfig(
      Utils::resolvePmUnitConfig(config, "SMB", makeVersion(3, 1, 10))));
}
