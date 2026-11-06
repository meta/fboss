#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>
#include <gtest/gtest.h>

#include "fboss/platform/bsp_tests/BspTest.h"
#include "fboss/platform/bsp_tests/utils/CdevUtils.h"

namespace facebook::fboss::platform::bsp_tests {

// Helper function to find a subdevice in the filesystem
std::string findSubdevice(
    const PciDeviceInfo& pciInfo,
    const fbiob::AuxData& dev,
    int32_t id,
    const std::unordered_map<std::string, std::string>& fpgaDirMap) {
  std::string pciDir = fpgaDirMap.at(CdevUtils::makePciName(pciInfo));
  std::string deviceNameDotId =
      fmt::format("{}.{}", *dev.id()->deviceName(), id);

  std::vector<std::string> matchingFiles;
  for (const auto& entry : std::filesystem::directory_iterator(pciDir)) {
    std::string path = entry.path().string();
    std::string filename = entry.path().filename().string();

    // Check if the filename contains the pattern (deviceName.id)
    if (filename.find(deviceNameDotId) != std::string::npos) {
      matchingFiles.push_back(path);
    }
  }

  return matchingFiles.size() == 1 ? matchingFiles[0] : "";
}

class XcvrTest : public BspTest {};

// Test that all XCVR controllers have the device name "xcvr_ctrl"
TEST_F(XcvrTest, XcvrNames) {
  const auto& devices = *GetRuntimeConfig().devices();
  if (devices.empty()) {
    GTEST_SKIP() << "No devices found in runtime config";
  }

  for (const auto& device : devices) {
    // Skip devices without aux devices
    if (device.auxDevices()->empty()) {
      continue;
    }

    // Find XCVR controllers in auxDevices
    for (const auto& auxDevice : *device.auxDevices()) {
      // Skip non-XCVR devices
      if (*auxDevice.type() != fbiob::AuxDeviceType::XCVR) {
        continue;
      }

      ASSERT_EQ(*auxDevice.id()->deviceName(), "xcvr_ctrl")
          << "XCVR controller name should be 'xcvr_ctrl'";
    }
  }
}

// Test that creating an XCVR device creates the expected sysfs files
TEST_F(XcvrTest, XcvrCreatesSysfsFiles) {
  const auto& devices = *GetRuntimeConfig().devices();
  if (devices.empty()) {
    GTEST_SKIP() << "No devices found in runtime config";
  }

  // Convert devices to PciDeviceInfo for CdevUtils::findPciDirs
  std::vector<PciDeviceInfo> pciDevices(devices.size());
  std::transform(
      devices.begin(),
      devices.end(),
      pciDevices.begin(),
      [](const auto& device) { return *device.pciInfo(); });
  auto fpgaDirMap = CdevUtils::findPciDirs(pciDevices);

  for (const auto& device : devices) {
    // Skip devices without aux devices
    if (device.auxDevices()->empty()) {
      continue;
    }

    // Find XCVR controllers in auxDevices
    for (const auto& auxDevice : *device.auxDevices()) {
      // Skip non-XCVR devices
      if (*auxDevice.type() != fbiob::AuxDeviceType::XCVR ||
          !auxDevice.xcvrData().has_value()) {
        continue;
      }

      const auto& xcvrData = *auxDevice.xcvrData();
      int32_t port = *xcvrData.portNumber();

      std::vector<std::string> expectedFiles = {
          fmt::format("xcvr_reset_{}", port),
          fmt::format("xcvr_low_power_{}", port),
          fmt::format("xcvr_present_{}", port)};

      // Check that the device doesn't exist before creation
      ASSERT_TRUE(
          findSubdevice(*device.pciInfo(), auxDevice, port, fpgaDirMap).empty())
          << "XCVR device exists before creation";

      try {
        CdevUtils::createNewDevice(*device.pciInfo(), auxDevice, port);

        // Check that the device was created
        std::string devPath =
            findSubdevice(*device.pciInfo(), auxDevice, port, fpgaDirMap);
        ASSERT_FALSE(devPath.empty()) << "Failed to create XCVR device";

        // Check that the expected sysfs files exist
        std::vector<std::string> missingFiles;
        for (const auto& file : expectedFiles) {
          if (!std::filesystem::exists(fmt::format("{}/{}", devPath, file))) {
            missingFiles.push_back(file);
          }
        }

        ASSERT_TRUE(missingFiles.empty())
            << "Expected sysfs files do not exist: "
            << folly::join(", ", missingFiles);
      } catch (const std::exception& e) {
        FAIL() << "Exception during XCVR device creation: " << e.what();
      }

      try {
        CdevUtils::deleteDevice(*device.pciInfo(), auxDevice, port);

        // Check that the device was deleted
        ASSERT_TRUE(
            findSubdevice(*device.pciInfo(), auxDevice, port, fpgaDirMap)
                .empty())
            << "Failed to delete XCVR device";
      } catch (const std::exception& e) {
        FAIL() << "Exception during XCVR device deletion: " << e.what();
      }
    }
  }
}

} // namespace facebook::fboss::platform::bsp_tests
