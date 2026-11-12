// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <filesystem>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <folly/String.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>

#include "fboss/mdio/BspDeviceMdio.h"
#include "fboss/platform/bsp_tests/BspTest.h"
#include "fboss/platform/bsp_tests/utils/CdevUtils.h"

namespace facebook::fboss::platform::bsp_tests {

constexpr auto kMdioBusCtrlPathPrefix = "/sys/class/mdio_bus/mdio_controller.";
constexpr auto kMdioBusDevPathPrefix = "/dev/fb-mdio-";

class MdioTest : public BspTest {
 protected:
  std::optional<MdioTestData> getMdioTestData(const std::string& pmName) {
    auto testDataOpt = getDeviceTestData(pmName);
    if (testDataOpt.has_value() && testDataOpt->mdioTestData().has_value()) {
      return testDataOpt->mdioTestData().value();
    }
    return std::nullopt;
  }
};

class MdioAuxDevice {
 public:
  MdioAuxDevice(
      const PciDeviceInfo& pciInfo,
      const fbiob::AuxData& auxDevice,
      int32_t id)
      : pciInfo_(pciInfo), auxDevice_(auxDevice), id_(id) {
    CdevUtils::createNewDevice(pciInfo_, auxDevice_, id_);
  }

  ~MdioAuxDevice() {
    try {
      CdevUtils::deleteDevice(pciInfo_, auxDevice_, id_);
    } catch (const std::exception& e) {
      // Destructors should not throw. Just log the error.
      XLOG(ERR) << "Failed to delete MDIO_BUS device in destructor: "
                << e.what();
    }
  }

  // Prevent copying
  MdioAuxDevice(const MdioAuxDevice&) = delete;
  MdioAuxDevice& operator=(const MdioAuxDevice&) = delete;

 private:
  const PciDeviceInfo& pciInfo_;
  const fbiob::AuxData& auxDevice_;
  int32_t id_;
};

// Test create MDIO bus
TEST_F(MdioTest, CreateMdioBus) {
  const auto& devices = *GetRuntimeConfig().devices();
  if (devices.empty()) {
    GTEST_SKIP() << "No devices found in runtime config";
    return;
  }

  for (const auto& device : devices) {
    // Skip devices without aux devices
    if (device.auxDevices()->empty()) {
      continue;
    }

    int32_t id = 0;
    for (const auto& auxDevice : *device.auxDevices()) {
      // Skip non-MDIO devices
      if (*auxDevice.type() != fbiob::AuxDeviceType::MDIO_BUS) {
        continue;
      }

      id++;

      try {
        std::string mdioDevPath =
            fmt::format("{}{}", kMdioBusDevPathPrefix, id);
        {
          MdioAuxDevice mdioAuxDevice(*device.pciInfo(), auxDevice, id);

          XLOG(DBG4) << "Created MDIO_BUS device for PM: " << *auxDevice.name();

          // Check that the dev files exist
          ASSERT_TRUE(std::filesystem::exists(mdioDevPath))
              << "MDIO_BUS device file does not exist: " << mdioDevPath;

          // Expected sysfs files in MDIO_BUS Ctrl directory
          std::vector<std::string> expectedFiles = {"reset_bus"};
          // Check that the expected sysfs files exist
          std::vector<std::string> missingFiles;
          for (const auto& file : expectedFiles) {
            if (!std::filesystem::exists(
                    fmt::format("{}{}/{}", kMdioBusCtrlPathPrefix, id, file))) {
              missingFiles.push_back(file);
            }
          }

          ASSERT_TRUE(missingFiles.empty())
              << "Expected sysfs files do not exist: "
              << folly::join(", ", missingFiles);
        }

        ASSERT_FALSE(std::filesystem::exists(mdioDevPath))
            << "MDIO_BUS device file was not deleted: " << mdioDevPath;

      } catch (const std::exception& e) {
        FAIL() << "Exception during MDIO_BUS device creation: " << e.what();
      }
    }
  }
}

// Test MDIO_BUS read and write
TEST_F(MdioTest, MdioBusReadAndWrite) {
  const auto& devices = *GetRuntimeConfig().devices();
  if (devices.empty()) {
    GTEST_SKIP() << "No devices found in runtime config";
    return;
  }

  for (const auto& device : devices) {
    // Skip devices without aux devices
    if (device.auxDevices()->empty()) {
      continue;
    }

    int32_t id = 0;
    for (const auto& auxDevice : *device.auxDevices()) {
      // Skip non-MDIO devices
      if (*auxDevice.type() != fbiob::AuxDeviceType::MDIO_BUS) {
        continue;
      }

      id++;

      try {
        std::string mdioDevPath =
            fmt::format("{}{}", kMdioBusDevPathPrefix, id);
        {
          MdioAuxDevice mdioAuxDevice(*device.pciInfo(), auxDevice, id);

          XLOG(DBG4) << "Created MDIO_BUS device for PM: " << *auxDevice.name();

          // Create MdioController instance,

          MdioController<BspDeviceMdio> mdioController(id, 1, id, mdioDevPath);
          mdioController.init();

          auto mdioTestDataOpt = getMdioTestData(*auxDevice.name());
          if (mdioTestDataOpt) {
            const auto& mdioTestData = *mdioTestDataOpt;

            auto devAddr = 1;

            for (const auto& test : *mdioTestData.mdioWriteData()) {
              auto phyAddr = static_cast<uint8_t>(test.phyAddress().value());
              auto regAddr = static_cast<uint16_t>(test.regAddress().value());
              auto setValue = static_cast<uint16_t>(test.setValue().value());
              // Read and write to the specified register

              XLOG(DBG4) << fmt::format(
                  "Writing to MDIO_BUS device, PM: {}, phyAddr: 0x{:x}, devAddr: 0x{:x}, regAddr: 0x{:x}, setValue: 0x{:x}",
                  *auxDevice.name(),
                  phyAddr,
                  devAddr,
                  regAddr,
                  setValue);

              mdioController.writeCl45(phyAddr, devAddr, regAddr, setValue);
              auto readValue =
                  mdioController.readCl45(phyAddr, devAddr, regAddr);
              EXPECT_EQ(readValue, setValue) << fmt::format(
                  "MDIO_BUS write/read value mismatch for phyAddr: 0x{:x}, regAddr: 0x{:x}",
                  phyAddr,
                  regAddr);
            }

            for (const auto& test : *mdioTestData.mdioReadData()) {
              // Read from the specified register
              auto phyAddr = static_cast<uint8_t>(test.phyAddress().value());
              auto regAddr = static_cast<uint16_t>(test.regAddress().value());
              auto expectedValue =
                  static_cast<uint16_t>(test.expected().value());

              XLOG(DBG4) << fmt::format(
                  "Reading from MDIO_BUS device, PM: {}, phyAddr: 0x{:x}, devAddr: 0x{:x}, regAddr: 0x{:x}",
                  *auxDevice.name(),
                  phyAddr,
                  devAddr,
                  regAddr);

              auto readValue =
                  mdioController.readCl45(phyAddr, devAddr, regAddr);
              EXPECT_EQ(readValue, expectedValue) << fmt::format(
                  "MDIO_BUS read value mismatch for phyAddr: 0x{:x}, regAddr: 0x{:x}",
                  phyAddr,
                  regAddr);
            }
          }
        }

        ASSERT_FALSE(std::filesystem::exists(mdioDevPath))
            << "MDIO_BUS device file was not deleted: " << mdioDevPath;
      } catch (const std::exception& e) {
        FAIL() << "Exception during MDIO_BUS read test: " << e.what();
      }
    }
  }
}

} // namespace facebook::fboss::platform::bsp_tests
