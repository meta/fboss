#include <fcntl.h>

#include <folly/String.h>
#include <unistd.h>
#include <cerrno>

#include <cstring>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fboss/platform/bsp_tests/BspTest.h"
#include "fboss/platform/bsp_tests/gen-cpp2/bsp_tests_config_types.h"
#include "fboss/platform/bsp_tests/utils/I2CUtils.h"
#include "fboss/platform/bsp_tests/utils/KmodUtils.h"
#include "fboss/platform/bsp_tests/utils/WatchdogUtils.h"

namespace facebook::fboss::platform::bsp_tests {

class WatchdogTest : public BspTest {
 protected:
  std::vector<I2CAdapter> getAllAdaptersWithWatchdogs() {
    std::vector<I2CAdapter> adapters;

    for (const auto& [adapterName, adapter] :
         *this->GetRuntimeConfig().i2cAdapters()) {
      bool hasWatchdogDevice = false;
      for (const auto& i2cDevice : *adapter.i2cDevices()) {
        auto watchdogDataOpt = getWatchdogTestData(i2cDevice);
        if (watchdogDataOpt.has_value()) {
          hasWatchdogDevice = true;
          break;
        }
      }
      if (hasWatchdogDevice) {
        adapters.push_back(adapter);
      }
    }
    return adapters;
  }

  std::vector<I2CDevice> getDevicesWithWatchdogs(const I2CAdapter& adapter) {
    std::vector<I2CDevice> devices;

    for (const auto& i2cDevice : *adapter.i2cDevices()) {
      auto watchdogDataOpt = getWatchdogTestData(i2cDevice);
      if (watchdogDataOpt.has_value()) {
        devices.push_back(i2cDevice);
      }
    }

    return devices;
  }

  // Helper function to perform operations on a watchdog device with proper
  // cleanup
  void withWatchdog(
      const std::string& path,
      const std::function<void(int fd)>& operation) {
    int fd = open(path.c_str(), O_WRONLY);
    bool success = true;
    std::vector<std::string> errMsgs;
    ASSERT_GE(fd, 0) << "Failed to open watchdog device " << path;
    try {
      operation(fd);
    } catch (const std::exception& e) {
      success = false;
      errMsgs.push_back(
          fmt::format("Failed operation on {} : {}", path, e.what()));
    }
    try {
      WatchdogUtils::magicCloseWatchdog(fd);
      close(fd);
    } catch (const std::exception& e) {
      success = false;
      errMsgs.push_back(
          fmt::format("Failed to close watchdog on {} : {}", path, e.what()));
    }
    if (!success) {
      FAIL() << folly::join("; ", errMsgs);
    }
  }

  // Helper function to find new watchdogs using set difference
  std::set<std::string> getNewWatchdogs(
      const std::set<std::string>& existingWdts) {
    auto totalWdts = WatchdogUtils::getWatchdogs();
    std::set<std::string> newWdts;
    std::set_difference(
        totalWdts.begin(),
        totalWdts.end(),
        existingWdts.begin(),
        existingWdts.end(),
        std::inserter(newWdts, newWdts.begin()));
    return newWdts;
  }

  std::optional<WatchdogTestData> getWatchdogTestData(const I2CDevice& device) {
    auto testDataOpt = getDeviceTestData(device);
    if (testDataOpt.has_value() &&
        testDataOpt->watchdogTestData().has_value()) {
      return testDataOpt->watchdogTestData().value();
    }
    return std::nullopt;
  }

  std::set<std::string> setupDeviceAndGetWatchdogs(
      const I2CDevice& device,
      int busNum) {
    auto existingWdts = WatchdogUtils::getWatchdogs();
    EXPECT_TRUE(I2CUtils::createI2CDevice(device, busNum))
        << "Failed to create I2C device " << *device.deviceName() << " on bus "
        << busNum;
    return getNewWatchdogs(existingWdts);
  }

  // Helper function to run a common watchdog test pattern
  void runWatchdogTest(
      const std::string& testName,
      const std::function<void(const std::string&)>& deviceTestFn) {
    const auto adapters = getAllAdaptersWithWatchdogs();
    if (adapters.empty()) {
      GTEST_SKIP() << "No watchdogs found in test config";
    }

    int id = 0;
    for (const auto& adapter : adapters) {
      int adapterId = id;
      try {
        auto result = I2CUtils::createI2CAdapter(adapter, id);
        registerAdaptersForCleanup(result.createdAdapters);
        int adapterBaseBusNum = result.buses.begin()->second.busNum;
        id++;

        for (const auto& device : getDevicesWithWatchdogs(adapter)) {
          int busNum = adapterBaseBusNum + *device.channel();
          auto watchdogDataOpt = getWatchdogTestData(device);
          const auto& testData = watchdogDataOpt.value();

          auto newWdts = setupDeviceAndGetWatchdogs(device, busNum);

          ASSERT_EQ(newWdts.size(), *testData.numWatchdogs())
              << "Expected " << *testData.numWatchdogs() << " watchdogs, got "
              << newWdts.size();

          // Run the test-specific function for each watchdog
          for (const auto& watchdog : newWdts) {
            deviceTestFn(watchdog);
          }
        }
      } catch (const std::exception& e) {
        if (adapter.pciAdapterInfo().has_value()) {
          CdevUtils::deleteDevice(
              *adapter.pciAdapterInfo()->pciInfo(),
              *adapter.pciAdapterInfo()->auxData(),
              adapterId);
        }
        FAIL() << "Exception during " << testName << " test: " << e.what();
      }
    }
  }
};

TEST_F(WatchdogTest, WatchdogStart) {
  this->runWatchdogTest("watchdog start", [this](const std::string& watchdog) {
    withWatchdog(watchdog, [&watchdog](int fd) {
      int timeout = WatchdogUtils::getWatchdogTimeout(fd);
      EXPECT_GT(timeout, 0) << "Timeout expected to be greater than 0";

      // Opening an already-active watchdog must fail with EBUSY
      int fd2 = open(watchdog.c_str(), O_WRONLY);
      EXPECT_EQ(fd2, -1) << "Second open of active watchdog should fail";
      EXPECT_EQ(errno, EBUSY) << "Expected EBUSY, got " << strerror(errno);
      if (fd2 >= 0) {
        close(fd2);
      }
    });
  });
}

TEST_F(WatchdogTest, WatchdogPing) {
  this->runWatchdogTest("watchdog ping", [this](const std::string& watchdog) {
    withWatchdog(watchdog, [](int fd) { WatchdogUtils::pingWatchdog(fd); });
  });
}

TEST_F(WatchdogTest, WatchdogSetTimeout) {
  this->runWatchdogTest(
      "watchdog set timeout", [this](const std::string& watchdog) {
        withWatchdog(watchdog, [](int fd) {
          WatchdogUtils::setWatchdogTimeout(fd, 10);
          int t = WatchdogUtils::getWatchdogTimeout(fd);
          EXPECT_EQ(t, 10) << "Timeout expected to be 10, got " << t;

          WatchdogUtils::setWatchdogTimeout(fd, 100);
          t = WatchdogUtils::getWatchdogTimeout(fd);
          EXPECT_EQ(t, 100) << "Timeout expected to be 100, got " << t;
        });
      });
}

TEST_F(WatchdogTest, WatchdogMagicClose) {
  this->runWatchdogTest(
      "watchdog magic close", [this](const std::string& watchdog) {
        withWatchdog(watchdog, [](int) {
          // withWatchdog closes the wd
        });
      });
}

TEST_F(WatchdogTest, WatchdogDriverUnload) {
  const auto adapters = getAllAdaptersWithWatchdogs();
  if (adapters.empty()) {
    GTEST_SKIP() << "No watchdogs found in test config";
  }

  const auto& runtimeConfig = this->GetRuntimeConfig();

  auto existingWdts = WatchdogUtils::getWatchdogs();
  int expectedWdts = 0;
  int id = 0;

  // Create all adapters and devices
  for (const auto& adapter : adapters) {
    try {
      id++;
      auto result = I2CUtils::createI2CAdapter(adapter, id);
      int adapterBaseBusNum = result.buses.begin()->second.busNum;

      for (const auto& device : getDevicesWithWatchdogs(adapter)) {
        auto watchdogDataOpt = getWatchdogTestData(device);
        const auto& testData = watchdogDataOpt.value();
        expectedWdts += *testData.numWatchdogs();

        int busNum = adapterBaseBusNum + *device.channel();
        EXPECT_TRUE(I2CUtils::createI2CDevice(device, busNum))
            << "Failed to create I2C device " << *device.deviceName()
            << " on bus " << busNum;
      }
    } catch (const std::exception& e) {
      FAIL() << "Exception during watchdog driver unload test setup: "
             << e.what();
    }
  }

  // Verify new watchdogs were created
  auto newWdts = getNewWatchdogs(existingWdts);

  for (const auto& watchdog : newWdts) {
    int fd = open(watchdog.c_str(), O_WRONLY);
    WatchdogUtils::magicCloseWatchdog(fd);
    close(fd);
  }

  KmodUtils::unloadKmods(*runtimeConfig.kmods());

  ASSERT_EQ(newWdts.size(), expectedWdts)
      << "Expected " << expectedWdts << " new watchdogs, got "
      << newWdts.size();

  // Verify watchdogs are gone
  auto remainingWdts = WatchdogUtils::getWatchdogs();
  EXPECT_EQ(remainingWdts.size(), existingWdts.size())
      << "Expected " << existingWdts.size() << " watchdogs after unload, got "
      << remainingWdts.size();
}
} // namespace facebook::fboss::platform::bsp_tests
