// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <folly/String.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <re2/re2.h>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "fboss/platform/bsp_tests/BspTest.h"
#include "fboss/platform/bsp_tests/utils/I2CUtils.h"
#include "fboss/platform/bsp_tests/utils/KmodUtils.h"

namespace facebook::fboss::platform::bsp_tests {

void runI2CDumpTest(
    const I2CDevice& device,
    int busNum,
    const I2CTestData& testData) {
  for (const auto& tc : *testData.i2cDumpData()) {
    std::string output =
        I2CUtils::i2cDump(busNum, *device.address(), *tc.start(), *tc.end());

    std::vector<std::string> result_data = I2CUtils::parseI2CDumpData(output);

    ASSERT_EQ(result_data.size(), tc.expected()->size())
        << "i2cdump output size does not match expected size";

    for (size_t i = 0; i < result_data.size(); ++i) {
      EXPECT_EQ(result_data[i], (*tc.expected())[i])
          << "i2cdump output at index " << i
          << " does not match expected: " << "got " << result_data[i]
          << ", expected " << (*tc.expected())[i];
    }
  }
}

void runI2CGetTest(
    const I2CDevice& device,
    int busNum,
    const I2CTestData& testData) {
  for (const auto& tc : *testData.i2cGetData()) {
    std::string output = I2CUtils::i2cGet(busNum, *device.address(), *tc.reg());

    EXPECT_EQ(output, *tc.expected())
        << "i2cget output does not match expected: " << "got " << output
        << ", expected " << *tc.expected() << " at register " << *tc.reg()
        << " on device " << *device.address();
  }
}

// Helper function to run I2C set test
void runI2CSetTest(
    const I2CDevice& device,
    int busNum,
    const I2CTestData& testData) {
  for (const auto& tc : *testData.i2cSetData()) {
    std::string original =
        I2CUtils::i2cGet(busNum, *device.address(), *tc.reg());

    // Set new value
    I2CUtils::i2cSet(busNum, *device.address(), *tc.reg(), *tc.value());

    // Verify new value
    std::string output = I2CUtils::i2cGet(busNum, *device.address(), *tc.reg());

    EXPECT_EQ(output, *tc.value())
        << "i2cset failed: output " << output
        << " does not match expected value " << *tc.value() << " at register "
        << *tc.reg() << " on device " << *device.address();

    // Set back to original value
    I2CUtils::i2cSet(busNum, *device.address(), *tc.reg(), original);

    // Verify original value restored
    output = I2CUtils::i2cGet(busNum, *device.address(), *tc.reg());

    EXPECT_EQ(output, original) << "Failed to restore original value: got "
                                << output << ", expected " << original;
  }
}

void runI2CTestTransactions(
    const I2CDevice& device,
    int busNum,
    int repeat,
    std::optional<DeviceTestData> testDataOpt) {
  if (!testDataOpt.has_value() || !testDataOpt->i2cTestData().has_value()) {
    return;
  }

  const auto& testData = testDataOpt->i2cTestData().value();

  // Some devices need a single get before returning
  // expected values
  try {
    I2CUtils::i2cGet(busNum, *device.address(), "0x00");
  } catch (std::exception& e) {
    XLOG(WARN) << "initial i2cget failed: " << e.what();
  }
  for (int i = 0; i < repeat; ++i) {
    runI2CDumpTest(device, busNum, testData);
  }
  for (int i = 0; i < repeat; ++i) {
    runI2CGetTest(device, busNum, testData);
  }
  for (int i = 0; i < repeat; ++i) {
    runI2CSetTest(device, busNum, testData);
  }
}

// Test fixture for I2C tests
class I2CTest : public BspTest {
 protected:
  std::vector<I2CAdapter> getAllTestableAdapters() {
    // Don't include any cpu adapters
    std::vector<I2CAdapter> adapters;
    for (const auto& [pmName, adapter] : *GetRuntimeConfig().i2cAdapters()) {
      if (!*adapter.isCpuAdapter()) {
        adapters.push_back(adapter);
      }
    }
    return adapters;
  }
};

TEST_F(I2CTest, I2CAdapterNames) {
  const auto adapters = getAllTestableAdapters();
  ASSERT_FALSE(adapters.empty()) << "No I2C adapters found in runtime config";

  // Pattern to match: "i2c_master" optionally followed by underscore and
  // additional characters
  const re2::RE2 pattern = "(.+_)?i2c_master(_.+)?";

  std::set<std::string> invalidNames;

  for (const auto& adapter : adapters) {
    std::string deviceName;
    // mux adapter names can be anything, skip them
    if (!adapter.pciAdapterInfo().has_value()) {
      continue;
    }
    deviceName = *adapter.pciAdapterInfo()->auxData()->id()->deviceName();
    if (!RE2::FullMatch(deviceName, pattern)) {
      invalidNames.insert(deviceName);
    }
  }
  EXPECT_TRUE(invalidNames.empty())
      << "Invalid I2C adapter names: " << folly::join(", ", invalidNames);
}

TEST_F(I2CTest, I2CAdapterCreatesBusses) {
  const auto adapters = getAllTestableAdapters();
  ASSERT_FALSE(adapters.empty()) << "No I2C adapters found in runtime config";

  int id = 1;
  for (const auto& adapter : adapters) {
    try {
      auto result = I2CUtils::createI2CAdapter(adapter, id);
      registerAdaptersForCleanup(result.createdAdapters);
      id += 10;

      // Check that each bus has a unique name
      std::set<std::string> names;
      for (const auto& [channel, bus] : result.buses) {
        names.insert(bus.name);
      }

      EXPECT_EQ(names.size(), result.buses.size())
          << "Not all I2C buses on " << *adapter.pmName()
          << "have unique names: " << folly::join(", ", names);
    } catch (const std::exception& e) {
      FAIL() << "Exception during I2C adapter creation: " << e.what();
    }
    cleanupDevices();
  }
}

TEST_F(I2CTest, I2CAdapterDevicesExist) {
  const auto adapters = getAllTestableAdapters();
  ASSERT_FALSE(adapters.empty()) << "No I2C adapters found in runtime config";

  int id = 1;
  for (const auto& adapter : adapters) {
    try {
      auto result = I2CUtils::createI2CAdapter(adapter, id);
      // Register all created adapters for cleanup
      registerAdaptersForCleanup(result.createdAdapters);
      id += result.createdAdapters.size();

      // Check each device is detectable
      for (const auto& device : *adapter.i2cDevices()) {
        auto reason = getExpectedErrorReason(
            *device.pmName(), ExpectedErrorType::I2C_NOT_DETECTABLE);
        if (reason) {
          recordExpectedError(*device.pmName(), *reason);
          XLOG(INFO) << "Skipping I2C device " << *device.pmName() << " ";
          continue;
        }

        int busNum = result.buses.at(*device.channel()).busNum;

        EXPECT_TRUE(I2CUtils::detectI2CDevice(busNum, *device.address()))
            << "I2C device " << *device.pmName() << " " << *device.address()
            << " not detected on bus " << busNum << " " << *adapter.pmName();
      }
      cleanupDevices();
    } catch (const std::exception& e) {
      FAIL() << "Exception during I2C adapter device detection: " << e.what();
    }
  }
}

TEST_F(I2CTest, I2CBusWithDevicesCanBeUnloaded) {
  auto runtimeConfig = GetRuntimeConfig();
  auto adapters = getAllTestableAdapters();

  // Just test the first PCI adapter with devices
  for (const auto& adapter : adapters) {
    if (adapter.i2cDevices()->size() == 0) {
      continue;
    }
    if (!adapter.pciAdapterInfo().has_value()) {
      continue;
    }

    KmodUtils::loadKmods(*runtimeConfig.kmods());

    try {
      auto result = I2CUtils::createI2CAdapter(adapter);

      for (const auto& device : *adapter.i2cDevices()) {
        int busNum = result.buses.at(*device.channel()).busNum;

        // Check that the device is detectable
        ASSERT_TRUE(I2CUtils::detectI2CDevice(busNum, *device.address()))
            << "I2C device " << *device.address() << " not detected on bus "
            << busNum;

        ASSERT_TRUE(I2CUtils::createI2CDevice(device, busNum))
            << "I2C device " << *device.address() << " not created on bus "
            << busNum;
      }

      // Unload kernel modules - this is the main test, to ensure they can be
      // unloaded
      KmodUtils::unloadKmods(*runtimeConfig.kmods());
      return;
    } catch (const std::exception& e) {
      FAIL() << "Exception during I2C bus unload test: " << e.what();
    }
  }
}

TEST_F(I2CTest, I2CTransactions) {
  const auto& adapters = getAllTestableAdapters();

  int id = 1;
  for (const auto& adapter : adapters) {
    bool hasTestData = std::any_of(
        adapter.i2cDevices()->begin(),
        adapter.i2cDevices()->end(),
        [&](const auto& i2cDevice) {
          return getDeviceTestData(i2cDevice).has_value();
        });
    if (!hasTestData) {
      continue;
    }
    try {
      auto result = I2CUtils::createI2CAdapter(adapter, id);
      registerAdaptersForCleanup(result.createdAdapters);
      id += result.createdAdapters.size();

      for (const auto& i2cDevice : *adapter.i2cDevices()) {
        auto testData = getDeviceTestData(i2cDevice);
        int busNum = result.buses.at(*i2cDevice.channel()).busNum;
        runI2CTestTransactions(i2cDevice, busNum, 1, testData);
      }
    } catch (const std::exception& e) {
      FAIL() << "Exception during I2C transactions test: " << e.what();
    }
  }
}

} // namespace facebook::fboss::platform::bsp_tests
