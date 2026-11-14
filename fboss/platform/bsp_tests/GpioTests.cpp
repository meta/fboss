#include <string>

#include <folly/String.h>
#include <vector>

#include <fmt/format.h>
#include <gtest/gtest.h>

#include "fboss/platform/bsp_tests/BspTest.h"
#include "fboss/platform/bsp_tests/gen-cpp2/bsp_tests_config_types.h"
#include "fboss/platform/bsp_tests/utils/GpioUtils.h"
#include "fboss/platform/bsp_tests/utils/I2CUtils.h"
#include "fboss/platform/bsp_tests/utils/KmodUtils.h"

namespace facebook::fboss::platform::bsp_tests {

// Test fixture for GPIO tests
class GpioTest : public BspTest {
 protected:
  std::vector<I2CAdapter> getAllAdaptersWithGpios() {
    std::vector<I2CAdapter> adapters;

    const auto& runtimeConfig = this->GetRuntimeConfig();
    // Get I2C adapters from the runtime config
    for (const auto& [pmName, adapter] : *runtimeConfig.i2cAdapters()) {
      bool hasGpioDevice = false;
      for (const auto& i2cDevice : *adapter.i2cDevices()) {
        if (*i2cDevice.isGpioChip()) {
          hasGpioDevice = true;
          break;
        }
      }
      if (hasGpioDevice) {
        adapters.emplace_back(adapter);
      }
    }
    return adapters;
  }

  std::optional<GpioTestData> getGpioTestData(const I2CDevice& device) {
    auto testDataOpt = getDeviceTestData(device);
    if (testDataOpt.has_value() && testDataOpt->gpioTestData().has_value()) {
      return testDataOpt->gpioTestData().value();
    }
    return std::nullopt;
  }

  // Format GPIO label: busNum-address (with address zero-padded to 4 digits)
  std::string formatGpioLabel(int busNum, const std::string& address) {
    std::string formattedAddress = address.substr(2); // Remove "0x" prefix
    formattedAddress.insert(
        0, 4 - formattedAddress.length(), '0'); // Zero-pad to 4 digits
    return fmt::format("{}-{}", busNum, formattedAddress);
  }

  // Helper struct to hold GPIO device information
  struct GpioDeviceInfo {
    std::string gpioName;
    std::optional<GpioTestData> testData;
    int busNum;
  };

  // Setup GPIO devices, returning a vector of GPIO device info
  std::vector<GpioDeviceInfo> setupGpioDevices(
      const I2CAdapter& adapter,
      int adapterBaseBusNum) {
    std::vector<GpioDeviceInfo> gpioDevices;

    for (const auto& i2cDevice : *adapter.i2cDevices()) {
      // Skip devices that are not GPIO chips
      if (!*i2cDevice.isGpioChip()) {
        continue;
      }

      // Get GPIO test data if available
      auto testDataOpt = getGpioTestData(i2cDevice);

      int busNum = adapterBaseBusNum + *i2cDevice.channel();
      EXPECT_TRUE(I2CUtils::createI2CDevice(i2cDevice, busNum))
          << "Failed to create I2C device " << *i2cDevice.deviceName();

      std::string expectedLabel = formatGpioLabel(busNum, *i2cDevice.address());

      // Check that the GPIO device is detected
      auto gpios = GpioUtils::gpiodetect(expectedLabel);
      EXPECT_EQ(gpios.size(), 1)
          << "Expected GPIO not detected: " << expectedLabel;

      gpioDevices.push_back({expectedLabel, testDataOpt, busNum});
    }

    return gpioDevices;
  }

  // Helper function to run a common GPIO test pattern
  void runGpioTest(
      const std::string& testName,
      const std::function<void(const std::string&, GpioTestData)>&
          deviceTestFn) {
    const auto adapters = getAllAdaptersWithGpios();
    if (adapters.empty()) {
      GTEST_SKIP() << "No I2C GPIOs found in test config";
    }

    int id = 1;
    for (const auto& adapter : adapters) {
      try {
        auto result = I2CUtils::createI2CAdapter(adapter, id);
        registerAdaptersForCleanup(result.createdAdapters);
        id++;

        auto gpioDevices =
            setupGpioDevices(adapter, result.buses.begin()->second.busNum);

        // Run the test-specific function for each GPIO device
        for (const auto& gpioDevice : gpioDevices) {
          if (!gpioDevice.testData.has_value()) {
            XLOG(INFO) << "Skipping " << testName << " test for "
                       << gpioDevice.gpioName
                       << " because no test data is provided";
            continue;
          }
          deviceTestFn(gpioDevice.gpioName, gpioDevice.testData.value());
        }
      } catch (const std::exception& e) {
        FAIL() << "Exception during " << testName << " test: " << e.what();
      }
    }
  }
};

// For top-level GPIO created from userspace, e.g pci.gpiochip
TEST_F(GpioTest, GpioCreated) {
  std::vector<std::string> errorMessages;

  int id = 0;
  for (const auto& device : *GetRuntimeConfig().devices()) {
    for (const auto& auxDevice : *device.auxDevices()) {
      if (*auxDevice.type() != fbiob::AuxDeviceType::GPIO) {
        continue;
      }

      try {
        id++;
        auto initialDetectedGpios = GpioUtils::gpiodetect("");
        CdevUtils::createNewDevice(*device.pciInfo(), auxDevice, id);
        registerDeviceForCleanup(*device.pciInfo(), auxDevice, id);

        // GPIO detection, check that the GPIO device is detected and verify the
        // number of lines
        auto detectedGpios = GpioUtils::gpiodetect("");
        ASSERT_EQ(detectedGpios.size() - initialDetectedGpios.size(), 1)
            << "Expected 1 new GPIO to be detected for " << *auxDevice.name();

        // Find the newly added GPIO by comparing the two vectors
        size_t newGpioIndex = 0;
        std::string gpioName;
        for (size_t i = 0; i < detectedGpios.size(); ++i) {
          bool found = false;
          for (const auto& initial : initialDetectedGpios) {
            if (detectedGpios[i].name == initial.name) {
              found = true;
              break;
            }
          }
          if (!found) {
            newGpioIndex = i;
            gpioName = detectedGpios[i].name;
            break;
          }
        }
        ASSERT_FALSE(gpioName.empty())
            << "Could not find newly added GPIO for " << *auxDevice.name();

        if (detectedGpios[newGpioIndex].lines <=
            0) { // Assuming > 0 is required
          errorMessages.emplace_back(
              fmt::format(
                  "For {}: expected > 0 lines, but got {}",
                  gpioName,
                  detectedGpios[newGpioIndex].lines));
          continue; // Continue to the next auxDevice
        }
        XLOG(INFO) << "GPIO detection: " << gpioName
                   << " size: " << detectedGpios.size()
                   << " lines: " << detectedGpios[newGpioIndex].lines;

        // GPIO info, verify the number of lines
        auto info = GpioUtils::gpioinfo(gpioName);
        EXPECT_EQ(info.size(), detectedGpios[newGpioIndex].lines)
            << "Line count mismatch for " << gpioName << ": gpiodetect reports "
            << detectedGpios[newGpioIndex].lines
            << " lines, but gpioinfo reports " << info.size() << " lines.";
        XLOG(INFO) << "GPIO info: " << gpioName << " lines: " << info.size();

        // GPIO get, show each line's value
        for (size_t i = 0; i < info.size(); ++i) {
          if (info[i].direction != "input") {
            continue;
          }
          int val = GpioUtils::gpioget(gpioName, i);
          XLOG(INFO) << "GPIO get: " << gpioName << " Line index: " << i
                     << " Line value: " << val;
        }
      } catch (const std::exception& e) {
        errorMessages.emplace_back(
            fmt::format(
                "Exception during GPIO creation for {}: {}",
                *auxDevice.name(),
                e.what()));
      }
    }
  }

  if (!errorMessages.empty()) {
    FAIL() << "Found " << errorMessages.size() << " errors:\n"
           << folly::join("\n", errorMessages);
  }
}

// Test that GPIO devices are detectable
TEST_F(GpioTest, GpioIsDetectable) {
  runGpioTest(
      "GPIO detection", [](const std::string& gpioName, GpioTestData testData) {
        // Verify the number of lines
        auto detectedGpios = GpioUtils::gpiodetect(gpioName);
        ASSERT_EQ(detectedGpios.size(), 1);
        ASSERT_EQ(detectedGpios[0].lines, *testData.numLines())
            << "Expected " << *testData.numLines()
            << " lines, got: " << GpioUtils::gpiodetect(gpioName)[0].lines;
      });
}

// Test GPIO info
TEST_F(GpioTest, GpioInfo) {
  runGpioTest(
      "GPIO info", [](const std::string& gpioName, GpioTestData testData) {
        // Get GPIO info
        auto info = GpioUtils::gpioinfo(gpioName);
        ASSERT_EQ(info.size(), *testData.numLines());

        // Check each line's info
        for (const auto& expectedLine : *testData.lines()) {
          int idx = *expectedLine.index();
          EXPECT_EQ(info[idx].name, *expectedLine.name())
              << "Line " << idx
              << " name mismatch: expected=" << *expectedLine.name()
              << ", got=" << info[idx].name;

          EXPECT_EQ(info[idx].direction, *expectedLine.direction())
              << "Line " << idx
              << " direction mismatch: expected=" << *expectedLine.direction()
              << ", got=" << info[idx].direction;
        }
      });
}

// Test GPIO get
TEST_F(GpioTest, GpioGet) {
  runGpioTest(
      "GPIO get", [](const std::string& gpioName, GpioTestData testData) {
        // Check each line's value
        for (size_t i = 0; i < testData.lines()->size(); ++i) {
          const auto& line = (*testData.lines())[i];
          if (!line.getValue().has_value()) {
            continue;
          }

          try {
            int val = GpioUtils::gpioget(gpioName, *line.index());
            int expectedValue = *line.getValue();
            EXPECT_EQ(val, expectedValue)
                << "Expected " << expectedValue << " for " << gpioName
                << " on line " << i << ", got: " << val;
          } catch (const std::exception& e) {
            FAIL() << "Failed to get GPIO value for " << gpioName << ": "
                   << e.what();
          }
        }
      });
}

// Test driver unloads while GPIO devices are created
TEST_F(GpioTest, DriverUnload) {
  int id = 1;
  for (const auto& adapter : getAllAdaptersWithGpios()) {
    try {
      auto result = I2CUtils::createI2CAdapter(adapter, id);
      id++;

      for (const auto& i2cDevice : *adapter.i2cDevices()) {
        // Only create devices that are GPIO chips
        if (*i2cDevice.isGpioChip()) {
          int busNum = result.buses.at(*i2cDevice.channel()).busNum;
          ASSERT_TRUE(I2CUtils::createI2CDevice(i2cDevice, busNum))
              << "Failed to create I2C device " << *i2cDevice.deviceName();
        }
      }
    } catch (const std::exception& e) {
      FAIL() << "Failed to create GPIO devices: " << e.what();
    }
  }

  // Unload kernel modules
  try {
    KmodUtils::unloadKmods(*GetRuntimeConfig().kmods());
  } catch (const std::exception& e) {
    FAIL() << "Failed to unload kmods with GPIO devices open: " << e.what();
  }
}
} // namespace facebook::fboss::platform::bsp_tests
