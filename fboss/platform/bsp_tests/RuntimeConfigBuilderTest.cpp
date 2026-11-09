// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <ranges>

#include "fboss/platform/bsp_tests/BspTestEnvironment.h"
#include "fboss/platform/bsp_tests/RuntimeConfigBuilder.h"
#include "fboss/platform/config_lib/ConfigLib.h"
#include "fboss/platform/platform_manager/Utils.h"

namespace facebook::fboss::platform::bsp_tests {

// Test subclass that exposes protected methods for testing
class TestableRuntimeConfigBuilder : public RuntimeConfigBuilder {
 public:
  using RuntimeConfigBuilder::getActualAdapter;
};

// Test fixture for RuntimeConfigBuilder tests
class RuntimeConfigBuilderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a test RuntimeConfigBuilder instance
    builder_ = std::make_unique<TestableRuntimeConfigBuilder>();

    setupMockPlatformConfig();
  }

  void setupMockPlatformConfig() {
    // Load the platform configuration using ConfigLib
    try {
      std::string configJson = ConfigLib().getPlatformManagerConfig("sample");
      apache::thrift::SimpleJSONSerializer::deserialize<
          platform_manager::PlatformConfig>(configJson, pmConfig_);
    } catch (const std::exception& e) {
      FAIL() << "Failed to load platform config: " << e.what();
    }
  }

  // Helper method to test getActualAdapter
  void testGetActualAdapter(
      const std::string& sourceUnitName,
      const std::string& sourceBusName,
      const std::string& slotType,
      const std::string& expectedUnitName,
      const std::string& expectedBusName,
      int expectedChannel) {
    std::string actualUnitName, actualBusName;
    int actualChannel;

    std::tie(actualUnitName, actualBusName, actualChannel) =
        builder_->getActualAdapter(
            pmConfig_,
            platform_manager::Utils::resolvePmUnitConfigs(pmConfig_, {}),
            sourceUnitName,
            sourceBusName,
            slotType);

    EXPECT_EQ(actualUnitName, expectedUnitName)
        << "Expected unit name: " << expectedUnitName
        << ", got: " << actualUnitName;

    EXPECT_EQ(actualBusName, expectedBusName)
        << "Expected bus name: " << expectedBusName
        << ", got: " << actualBusName;

    EXPECT_EQ(actualChannel, expectedChannel)
        << "Expected channel: " << expectedChannel
        << ", got: " << actualChannel;
  }

  // Makes the versioned config distinguishable from the default one.
  void makeVersionedPsuConfigDistinct() {
    auto& versioned = pmConfig_.versionedPmUnitConfigs()->at(kPsuPmUnit).at(0);
    auto& device = versioned.pmUnitConfig()->i2cDeviceConfigs()->at(0);
    device.address() = kVersionedAddress;
    device.kernelDeviceName() = kVersionedDriver;
  }

  RuntimeConfig buildWithVersions(const PmUnitVersionMap& pmUnitVersions) {
    bsp_tests::BspTestsConfig testConfig;
    testConfig.testData() = std::map<std::string, DeviceTestData>();
    BspKmodsFile kmods;
    return builder_->buildRuntimeConfig(
        testConfig, pmConfig_, kmods, "sample", pmUnitVersions);
  }

  // The device named `pmName` and the adapter it ended up attached to, after
  // INCOMING bus resolution.
  std::optional<std::pair<std::string, I2CDevice>> findAttachedI2cDevice(
      const RuntimeConfig& runtimeConfig,
      const std::string& pmName) {
    for (const auto& [adapterName, adapter] : *runtimeConfig.i2cAdapters()) {
      for (const auto& device : *adapter.i2cDevices()) {
        if (*device.pmName() == pmName) {
          return std::make_pair(adapterName, device);
        }
      }
    }
    return std::nullopt;
  }

  std::optional<I2CDevice> findI2cDeviceIf(
      const RuntimeConfig& runtimeConfig,
      const std::string& pmName) {
    auto attached = findAttachedI2cDevice(runtimeConfig, pmName);
    if (!attached) {
      return std::nullopt;
    }
    return attached->second;
  }

  I2CDevice findI2cDevice(
      const RuntimeConfig& runtimeConfig,
      const std::string& pmName) {
    auto attached = findAttachedI2cDevice(runtimeConfig, pmName);
    if (!attached) {
      throw std::runtime_error("No I2CDevice named " + pmName);
    }
    return attached->second;
  }

  std::string findAdapterOf(
      const RuntimeConfig& runtimeConfig,
      const std::string& pmName) {
    auto attached = findAttachedI2cDevice(runtimeConfig, pmName);
    if (!attached) {
      throw std::runtime_error("No I2CDevice named " + pmName);
    }
    return attached->first;
  }

  // Respin of `pmUnitName` = its default config with `mutate` applied.
  void addRespin(
      const std::string& pmUnitName,
      int16_t subVersion,
      const std::function<void(platform_manager::PmUnitConfig&)>& mutate) {
    platform_manager::VersionedPmUnitConfig versioned;
    versioned.productSubVersion() = subVersion;
    versioned.pmUnitConfig() = pmConfig_.pmUnitConfigs()->at(pmUnitName);
    mutate(*versioned.pmUnitConfig());
    pmConfig_.versionedPmUnitConfigs()[pmUnitName] = {versioned};
  }

  static platform_manager::PmUnitVersion respin(int16_t subVersion) {
    platform_manager::PmUnitVersion version;
    version.productionState() = 3;
    version.productionSubState() = 1;
    version.respinVariantIndicator() = subVersion;
    return version;
  }

  // Versions that would select `versionedConfig`.
  static std::vector<platform_manager::PmUnitVersion> declaredVersions(
      const platform_manager::VersionedPmUnitConfig& versionedConfig) {
    if (auto pmUnitVersions = versionedConfig.pmUnitVersions();
        pmUnitVersions && !pmUnitVersions->empty()) {
      return *pmUnitVersions;
    }
    if (!versionedConfig.productSubVersion()) {
      return {};
    }
    platform_manager::PmUnitVersion version;
    version.productionState() = 0;
    version.productionSubState() = 0;
    version.respinVariantIndicator() = *versionedConfig.productSubVersion();
    return {version};
  }

  static constexpr auto kPsuPmUnit = "PSU_2GH";
  static constexpr auto kPsuSensor = "PSU_2GH.PSU_2GH_SENSOR";
  static constexpr auto kDefaultAddress = "0x12";
  static constexpr auto kDefaultDriver = "lm75";
  static constexpr auto kVersionedAddress = "0x21";
  static constexpr auto kVersionedDriver = "mp2891";
  static constexpr int16_t kVersionedProductSubVersion = 4;

  std::unique_ptr<TestableRuntimeConfigBuilder> builder_;
  platform_manager::PlatformConfig pmConfig_;
};

TEST_F(RuntimeConfigBuilderTest, ProductSubVersionMatchAppliesVersionedConfig) {
  makeVersionedPsuConfigDistinct();

  platform_manager::PmUnitVersion version;
  version.productionState() = 3;
  version.productionSubState() = 1;
  version.respinVariantIndicator() = kVersionedProductSubVersion;

  auto device =
      findI2cDevice(buildWithVersions({{kPsuPmUnit, version}}), kPsuSensor);

  EXPECT_EQ(*device.address(), kVersionedAddress);
  EXPECT_EQ(*device.deviceName(), kVersionedDriver);
}

// productSubVersion constrains RespinVariantIndicator alone.
TEST_F(RuntimeConfigBuilderTest, ProductSubVersionIgnoresProductionStates) {
  makeVersionedPsuConfigDistinct();

  platform_manager::PmUnitVersion version;
  version.productionState() = 9;
  version.productionSubState() = 9;
  version.respinVariantIndicator() = kVersionedProductSubVersion;

  auto device =
      findI2cDevice(buildWithVersions({{kPsuPmUnit, version}}), kPsuSensor);

  EXPECT_EQ(*device.address(), kVersionedAddress);
}

// pmUnitVersions wins over productSubVersion and needs all three components.
TEST_F(RuntimeConfigBuilderTest, PmUnitVersionsMatchAppliesVersionedConfig) {
  makeVersionedPsuConfigDistinct();

  platform_manager::PmUnitVersion version;
  version.productionState() = 4;
  version.productionSubState() = 1;
  version.respinVariantIndicator() = 10;
  pmConfig_.versionedPmUnitConfigs()->at(kPsuPmUnit).at(0).pmUnitVersions() = {
      version};

  auto device =
      findI2cDevice(buildWithVersions({{kPsuPmUnit, version}}), kPsuSensor);

  EXPECT_EQ(*device.address(), kVersionedAddress);
  EXPECT_EQ(*device.deviceName(), kVersionedDriver);
}

TEST_F(RuntimeConfigBuilderTest, PmUnitVersionsRequiresFullTripleMatch) {
  makeVersionedPsuConfigDistinct();

  platform_manager::PmUnitVersion configured;
  configured.productionState() = 4;
  configured.productionSubState() = 1;
  configured.respinVariantIndicator() = 10;
  pmConfig_.versionedPmUnitConfigs()->at(kPsuPmUnit).at(0).pmUnitVersions() = {
      configured};

  platform_manager::PmUnitVersion detected = configured;
  detected.productionSubState() = 2;

  auto device =
      findI2cDevice(buildWithVersions({{kPsuPmUnit, detected}}), kPsuSensor);

  EXPECT_EQ(*device.address(), kDefaultAddress);
  EXPECT_EQ(*device.deviceName(), kDefaultDriver);
}

TEST_F(RuntimeConfigBuilderTest, UnmatchedVersionUsesDefaultConfig) {
  makeVersionedPsuConfigDistinct();

  platform_manager::PmUnitVersion version;
  version.productionState() = 3;
  version.productionSubState() = 1;
  version.respinVariantIndicator() = kVersionedProductSubVersion + 1;

  auto device =
      findI2cDevice(buildWithVersions({{kPsuPmUnit, version}}), kPsuSensor);

  EXPECT_EQ(*device.address(), kDefaultAddress);
  EXPECT_EQ(*device.deviceName(), kDefaultDriver);
}

// An unprogrammed IDPROM reports no version at all.
TEST_F(RuntimeConfigBuilderTest, NoVersionUsesDefaultConfig) {
  makeVersionedPsuConfigDistinct();

  auto device = findI2cDevice(buildWithVersions({}), kPsuSensor);

  EXPECT_EQ(*device.address(), kDefaultAddress);
  EXPECT_EQ(*device.deviceName(), kDefaultDriver);
}

// Dropping it silently would validate that PmUnit against its default config.
TEST_F(RuntimeConfigBuilderTest, RejectsVersionForUnknownPmUnit) {
  platform_manager::PmUnitVersion version;
  version.productionState() = 3;
  version.productionSubState() = 1;
  version.respinVariantIndicator() = 10;

  EXPECT_THROW(
      buildWithVersions({{"NOT_A_PM_UNIT", version}}), std::invalid_argument);
}

// A respin can move a child slot onto a different upstream bus, so bus
// resolution has to walk the resolved slot tree, not the default one.
TEST_F(RuntimeConfigBuilderTest, VersionedOutgoingSlotConfigRetargetsAdapter) {
  EXPECT_EQ(
      findAdapterOf(buildWithVersions({}), kPsuSensor),
      "YOLO_MAX.YOLO_DOM_I2C_0");

  addRespin("YOLO_MAX", 7, [](platform_manager::PmUnitConfig& pmUnitConfig) {
    auto& busNames = *pmUnitConfig.outgoingSlotConfigs()
                          ->at("PSU_SLOT@0")
                          .outgoingI2cBusNames();
    std::swap(busNames.at(0), busNames.at(1));
  });

  EXPECT_EQ(
      findAdapterOf(buildWithVersions({{"YOLO_MAX", respin(7)}}), kPsuSensor),
      "YOLO_MAX.YOLO_MUX1");
}

TEST_F(RuntimeConfigBuilderTest, VersionedMuxIsHonored) {
  addRespin("YOLO_MAX", 8, [](platform_manager::PmUnitConfig& pmUnitConfig) {
    for (auto& i2cDevice : *pmUnitConfig.i2cDeviceConfigs()) {
      if (*i2cDevice.pmUnitScopedName() == "YOLO_MUX1") {
        i2cDevice.numOutgoingChannels() = 8;
      }
    }
  });

  auto runtimeConfig = buildWithVersions({{"YOLO_MAX", respin(8)}});
  const auto& mux = runtimeConfig.i2cAdapters()->at("YOLO_MAX.YOLO_MUX1");

  EXPECT_EQ(*mux.muxAdapterInfo()->numOutgoingChannels(), 8);
}

TEST_F(RuntimeConfigBuilderTest, VersionedPciDeviceIsHonored) {
  addRespin("SMB", 6, [](platform_manager::PmUnitConfig& pmUnitConfig) {
    pmUnitConfig.pciDeviceConfigs()->at(0).deviceId() = "0xbeef";
  });

  auto runtimeConfig = buildWithVersions({{"SMB", respin(6)}});
  auto smbIob = std::ranges::find_if(
      *runtimeConfig.devices(), [](const PciDevice& device) {
        return *device.pmName() == "SMB.SMB_IOB";
      });

  ASSERT_NE(smbIob, runtimeConfig.devices()->end());
  EXPECT_EQ(*smbIob->pciInfo()->deviceId(), "0xbeef");
}

// Test that getActualAdapter correctly resolves a direct bus (non-INCOMING)
TEST_F(RuntimeConfigBuilderTest, DirectBusResolution) {
  testGetActualAdapter("SCM", "CPU@0", "SCM_SLOT", "SCM", "CPU@0", 0);
}

// Test that getActualAdapter correctly resolves a single INCOMING step
TEST_F(RuntimeConfigBuilderTest, SingleIncomingStep) {
  testGetActualAdapter(
      "SMB", "INCOMING@0", "SMB_SLOT", "SCM", "SCM_IOB_I2C_0", 0);
}

// Test handling of invalid INCOMING bus
TEST_F(RuntimeConfigBuilderTest, InvalidIncomingBus) {
  // The getActualAdapter method should throw an exception for an invalid
  // INCOMING bus
  EXPECT_THROW(
      builder_->getActualAdapter(
          pmConfig_,
          platform_manager::Utils::resolvePmUnitConfigs(pmConfig_, {}),
          "SMB",
          "INCOMING@99",
          "SMB_SLOT"),
      std::runtime_error)
      << "Expected exception when resolving invalid INCOMING bus";
}

TEST_F(RuntimeConfigBuilderTest, CpuAdaptersAdded) {
  // Create a minimal BspTestsConfig
  bsp_tests::BspTestsConfig testConfig;
  testConfig.testData() = std::map<std::string, DeviceTestData>();
  BspKmodsFile kmods;

  // Build the runtime config using the sample platform config
  auto runtimeConfig =
      builder_->buildRuntimeConfig(testConfig, pmConfig_, kmods, "sample");

  // get all cpu adapters
  std::vector<I2CAdapter> cpuAdapters;
  for (const auto& [pmName, adapter] : *runtimeConfig.i2cAdapters()) {
    if (*adapter.isCpuAdapter()) {
      cpuAdapters.push_back(adapter);
    }
  }

  // Verify that the runtime config contains cpu adapters
  ASSERT_EQ(cpuAdapters.size(), 2);

  // Verify that the cpu adapters have the correct properties
  for (const auto& adapter : cpuAdapters) {
    EXPECT_TRUE(
        *adapter.pmName() == "SCM.CPU@0" || *adapter.pmName() == "SCM.CPU@1");
    EXPECT_TRUE(*adapter.busName() == "CPU@0" || *adapter.busName() == "CPU@1");
  }
}

TEST_F(RuntimeConfigBuilderTest, MuxAdaptersAdded) {
  bsp_tests::BspTestsConfig testConfig;
  testConfig.testData() = std::map<std::string, DeviceTestData>();

  // Create empty kmods
  BspKmodsFile kmods;
  auto runtimeConfig =
      builder_->buildRuntimeConfig(testConfig, pmConfig_, kmods, "sample");

  // get all mux adapters
  std::vector<I2CAdapter> muxAdapters;
  for (const auto& [pmName, adapter] : *runtimeConfig.i2cAdapters()) {
    if (adapter.muxAdapterInfo().has_value()) {
      muxAdapters.push_back(adapter);
    }
  }

  EXPECT_EQ(muxAdapters.size(), 4);

  for (const auto& adapter : muxAdapters) {
    if (adapter.pmName() == "YOLO_MAX.YOLO_MUX1") {
      EXPECT_EQ(*adapter.isCpuAdapter(), false);
      EXPECT_EQ(*adapter.busName(), "YOLO_MUX1");
      EXPECT_EQ(*adapter.muxAdapterInfo()->deviceName(), "pca9x44");
      EXPECT_EQ(*adapter.muxAdapterInfo()->parentAdapterChannel(), 0);
      EXPECT_EQ(*adapter.muxAdapterInfo()->numOutgoingChannels(), 4);
      EXPECT_EQ(*adapter.muxAdapterInfo()->address(), "0x55");
      auto parent_adapter = *adapter.muxAdapterInfo()->parentAdapter();
      EXPECT_EQ(*parent_adapter.isCpuAdapter(), true);
      EXPECT_EQ(*parent_adapter.pmName(), "SCM.CPU@1");
    }
  }
}

// Test that i2cAdapters are added at the top level of RuntimeConfig correctly
TEST_F(RuntimeConfigBuilderTest, I2cAdaptersTopLevel) {
  // Create a minimal BspTestsConfig
  bsp_tests::BspTestsConfig testConfig;
  testConfig.testData() = std::map<std::string, DeviceTestData>();

  // Create empty kmods
  BspKmodsFile kmods;

  // Build the runtime config using the sample platform config
  auto runtimeConfig =
      builder_->buildRuntimeConfig(testConfig, pmConfig_, kmods, "sample");

  // Verify that the runtime config contains i2cAdapters at the top level
  ASSERT_GT(runtimeConfig.i2cAdapters()->size(), 0);

  // Look for a specific i2c adapter from the sample config
  bool foundAdapter = false;
  for (const auto& [pmName, adapter] : *runtimeConfig.i2cAdapters()) {
    if (*adapter.pmName() == "SCM.SCM_IOB_I2C_0") {
      foundAdapter = true;

      // Check that the adapter has PCI adapter info
      ASSERT_TRUE(adapter.pciAdapterInfo().has_value());
      const auto& pciInfo = *adapter.pciAdapterInfo();

      EXPECT_EQ(*pciInfo.pciInfo()->vendorId(), "0x83bf");
      EXPECT_EQ(*pciInfo.pciInfo()->deviceId(), "0xab87");

      const auto& auxData = pciInfo.auxData();
      EXPECT_EQ(*auxData->name(), "SCM_IOB_I2C_0");
      EXPECT_EQ(*auxData->id()->deviceName(), "i2c-smb");
      EXPECT_EQ(*auxData->iobufOffset(), "0x0023");

      break;
    }
  }

  EXPECT_TRUE(foundAdapter) << "Expected I2C adapter not found at top level";
}

// Test that PCI devices are added correctly with auxDevices
TEST_F(RuntimeConfigBuilderTest, PciDevicesWithAuxDevices) {
  // Create a minimal BspTestsConfig
  bsp_tests::BspTestsConfig testConfig;
  testConfig.testData() = std::map<std::string, DeviceTestData>();

  // Create empty kmods
  BspKmodsFile kmods;

  // Build the runtime config
  auto runtimeConfig =
      builder_->buildRuntimeConfig(testConfig, pmConfig_, kmods, "sample");

  // Verify that the runtime config contains PCI devices with auxDevices
  bool foundPciDevice = false;

  for (const auto& testDevice : *runtimeConfig.devices()) {
    if (*testDevice.pmName() == "SCM.SCM_IOB") {
      foundPciDevice = true;

      // Check that the PCI device has the correct properties
      EXPECT_EQ(*testDevice.pciInfo()->vendorId(), "0x83bf");
      EXPECT_EQ(*testDevice.pciInfo()->deviceId(), "0xab87");

      ASSERT_GT(testDevice.auxDevices()->size(), 0);

      // Look for the SPI aux device
      bool foundSpiAuxDevice = false;
      for (const auto& auxDevice : *testDevice.auxDevices()) {
        if (*auxDevice.name() == "SCM_IOB_SPI_0") {
          foundSpiAuxDevice = true;
          EXPECT_EQ(*auxDevice.id()->deviceName(), "spi");
          EXPECT_EQ(*auxDevice.iobufOffset(), "0x0034");
          break;
        }
      }
      EXPECT_TRUE(foundSpiAuxDevice)
          << "SPI aux device not found in PCI device";

      break;
    }
  }

  EXPECT_TRUE(foundPciDevice)
      << "Expected PCI device not found in runtime config";
}

// Test that RuntimeConfigBuilder can build configs for all real platforms
TEST_F(RuntimeConfigBuilderTest, BuildConfigsForAllRealPlatforms) {
  std::vector<std::string> realPlatforms = {
      "sample",
      "meru800bia",
      "meru800bfa",
      "montblanc",
      "morgan800cc",
      "janga800bic",
      "tahan800bc",
      "darwin",
      "minipack3n",
      "minipack3ba",
      "minipack3bam",
      "icecube",
      "darwin48v",
  };

  for (const auto& platformName : realPlatforms) {
    SCOPED_TRACE("Testing platform: " + platformName);

    // Load platform configuration for this platform
    platform_manager::PlatformConfig platformConfig;
    try {
      std::string configJson =
          ConfigLib().getPlatformManagerConfig(platformName);
      apache::thrift::SimpleJSONSerializer::deserialize<
          platform_manager::PlatformConfig>(configJson, platformConfig);
    } catch (const std::exception& e) {
      FAIL() << "Failed to load platform config for " << platformName << ": "
             << e.what();
    }

    // Create a minimal BspTestsConfig for this platform
    bsp_tests::BspTestsConfig testConfig;
    testConfig.testData() = std::map<std::string, DeviceTestData>();

    BspKmodsFile kmods;

    // Attempt to build the runtime config
    RuntimeConfig runtimeConfig;
    EXPECT_NO_THROW({
      runtimeConfig = builder_->buildRuntimeConfig(
          testConfig, platformConfig, kmods, platformName);
    }) << "Failed to build runtime config for platform: "
       << platformName;

    // Resolution must be inert when no versions are detected.
    const std::map<std::string, platform_manager::PmUnitConfig> defaults(
        platformConfig.pmUnitConfigs()->begin(),
        platformConfig.pmUnitConfigs()->end());
    EXPECT_EQ(
        platform_manager::Utils::resolvePmUnitConfigs(platformConfig, {}),
        defaults);

    // Expectations come from the config, so this keeps covering platforms as
    // they add respins and survives a config author changing an address.
    for (const auto& [pmUnitName, versionedConfigs] :
         *platformConfig.versionedPmUnitConfigs()) {
      for (const auto& versionedConfig : versionedConfigs) {
        const auto versions = declaredVersions(versionedConfig);
        // An entry with neither pmUnitVersions nor productSubVersion can never
        // be selected, so it would be silently skipped below and counted as
        // covered.
        ASSERT_FALSE(versions.empty())
            << platformName << " PmUnit " << pmUnitName
            << " declares a versionedPmUnitConfig with no selectable version";
        for (const auto& version : versions) {
          SCOPED_TRACE(
              fmt::format(
                  "PmUnit {} version {}.{}.{}",
                  pmUnitName,
                  *version.productionState(),
                  *version.productionSubState(),
                  *version.respinVariantIndicator()));

          RuntimeConfig respinConfig;
          ASSERT_NO_THROW({
            respinConfig = builder_->buildRuntimeConfig(
                testConfig,
                platformConfig,
                kmods,
                platformName,
                {{pmUnitName, version}});
          });

          for (const auto& i2cDevice :
               *versionedConfig.pmUnitConfig()->i2cDeviceConfigs()) {
            // Muxes become I2CAdapters, not I2CDevices.
            if (i2cDevice.numOutgoingChannels().has_value()) {
              continue;
            }
            const auto pmName =
                fmt::format("{}.{}", pmUnitName, *i2cDevice.pmUnitScopedName());
            SCOPED_TRACE(pmName);
            auto resolved = findI2cDeviceIf(respinConfig, pmName);
            ASSERT_TRUE(resolved.has_value());
            EXPECT_EQ(*resolved->address(), *i2cDevice.address());
            EXPECT_EQ(*resolved->deviceName(), *i2cDevice.kernelDeviceName());
          }
        }
      }
    }
  }
}

// Test that hyphens in kmod names are replaced with underscores
TEST_F(RuntimeConfigBuilderTest, KmodNamesHyphenReplacement) {
  // Create a minimal BspTestsConfig
  bsp_tests::BspTestsConfig testConfig;
  testConfig.testData() = std::map<std::string, DeviceTestData>();

  // Create kmods with hyphenated names
  BspKmodsFile kmods;
  std::vector<std::string> kmodNames = {
      "test-kmod-1", "another-hyphenated-kmod", "no_hyphens_here"};
  kmods.bspKmods() = kmodNames;

  // Store original kmod names for verification
  std::vector<std::string> originalKmodNames = kmodNames;

  // Build the runtime config
  auto runtimeConfig =
      builder_->buildRuntimeConfig(testConfig, pmConfig_, kmods, "sample");

  // Verify that hyphens in kmod names have been replaced with underscores
  // We need to check the kmods in the returned RuntimeConfig object
  const auto& modifiedKmods = *runtimeConfig.kmods()->bspKmods();
  ASSERT_EQ(modifiedKmods.size(), originalKmodNames.size());

  for (size_t i = 0; i < modifiedKmods.size(); i++) {
    std::string expected = originalKmodNames[i];
    std::replace(expected.begin(), expected.end(), '-', '_');
    EXPECT_EQ(modifiedKmods[i], expected)
        << "Expected kmod name " << expected << ", got: " << modifiedKmods[i];
  }
}

} // namespace facebook::fboss::platform::bsp_tests
