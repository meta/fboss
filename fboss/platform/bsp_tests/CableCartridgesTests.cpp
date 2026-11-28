// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <folly/String.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <re2/re2.h>

#include "fboss/platform/bsp_tests/BspTest.h"
#include "fboss/platform/bsp_tests/utils/CdevUtils.h"
#include "fboss/platform/bsp_tests/utils/I2CUtils.h"
#include "fboss/platform/helpers/PlatformFsUtils.h"

namespace facebook::fboss::platform::bsp_tests {

const re2::RE2 kSlotIdPattern("^cc_slot_id_\\d+$");

const std::vector<std::string> expectedSlotIdFiles = {
    "cc_slot_id_0",
    "cc_slot_id_1",
};

// Helper function to validate slot ID file names with expected pattern
void validateSlotIdFileName(
    const std::string& slotIdName,
    std::vector<std::string>& errorMessages) {
  if (!re2::RE2::FullMatch(slotIdName, kSlotIdPattern)) {
    errorMessages.emplace_back(
        fmt::format(
            "Slot ID file name {} does not match specification", slotIdName));
  }
}

std::map<std::string, std::string> checkCableCartridgeCorrectness(
    const std::string& deviceName,
    const std::string& ccPath,
    std::vector<std::string>& errorMessages) {
  std::map<std::string, std::string> slotIdMap;

  // Check that the Cable Cartridge is a directory
  if (!std::filesystem::is_directory(ccPath)) {
    errorMessages.emplace_back(
        fmt::format(
            "Cable Cartridge path {} for {} is not a directory",
            ccPath,
            deviceName));
    return slotIdMap;
  }

  // Check expected slot ID files: cc_slot_id_0 and cc_slot_id_1
  for (const auto& fileName : expectedSlotIdFiles) {
    validateSlotIdFileName(fileName, errorMessages);

    std::string filePath = (std::filesystem::path(ccPath) / fileName).string();
    if (!std::filesystem::exists(filePath)) {
      errorMessages.emplace_back(
          fmt::format(
              "Expected file {} not found in {} for {}",
              fileName,
              ccPath,
              deviceName));
      continue;
    }

    std::optional<std::string> content =
        PlatformFsUtils().getStringFileContent(filePath);
    if (!content.has_value()) {
      errorMessages.emplace_back(
          fmt::format(
              "Failed to read {} in {} for {}", fileName, ccPath, deviceName));
    } else {
      auto trimmed = folly::trimWhitespace(*content).str();
      if (trimmed.empty()) {
        errorMessages.emplace_back(
            fmt::format(
                "{} is empty in {} for {}", fileName, ccPath, deviceName));
      } else {
        XLOG(INFO) << fmt::format(
            "Read {} for {} (at {}): {}",
            fileName,
            deviceName,
            ccPath,
            trimmed);
        slotIdMap[fileName] = trimmed;
      }
    }
  }

  // Check driver link if present (as shown in reference directory listing)
  std::string driverPath = (std::filesystem::path(ccPath) / "driver").string();
  if (std::filesystem::exists(driverPath)) {
    std::error_code ec;
    auto target = std::filesystem::read_symlink(driverPath, ec);
    if (!ec) {
      XLOG(INFO) << fmt::format(
          "Driver for {} (at {}): {}",
          deviceName,
          ccPath,
          target.filename().string());
    }
  }

  return slotIdMap;
}

class CableCartridgesTest : public BspTest {
 public:
  bool isCableCartridgeDevice(const I2CDevice& device) const {
    const std::string& devName = *device.deviceName();
    const std::string& pmName = *device.pmName();

    if (devName.starts_with("cc_slot") ||
        devName.find("slot_id") != std::string::npos ||
        devName == "anacapa_slot_id") {
      return true;
    }
    if (pmName.find("CABLE_CARTRIDGE") != std::string::npos) {
      return true;
    }

    const auto& symlinks =
        *GetPlatformManagerConfig().symbolicLinkToDevicePath();
    for (const auto& [linkPath, target] : symlinks) {
      if (linkPath.find("cable-cartridge") != std::string::npos) {
        auto openBracket = target.find('[');
        auto closeBracket = target.find(']', openBracket);
        if (openBracket != std::string::npos &&
            closeBracket != std::string::npos &&
            closeBracket > openBracket + 1) {
          auto targetScopedName =
              target.substr(openBracket + 1, closeBracket - openBracket - 1);
          if (pmName == targetScopedName ||
              pmName.ends_with("." + targetScopedName)) {
            return true;
          }
        }
      }
    }
    return false;
  }

  std::vector<std::pair<I2CAdapter, I2CDevice>> getCableCartridgeDevices() {
    std::vector<std::pair<I2CAdapter, I2CDevice>> ccDevices;
    std::set<std::string> seenPmNames;

    // Scan all I2C devices across all I2C adapters in the platform config
    for (const auto& [_, adapter] : *GetRuntimeConfig().i2cAdapters()) {
      for (const auto& i2cDevice : *adapter.i2cDevices()) {
        if (isCableCartridgeDevice(i2cDevice)) {
          if (seenPmNames.insert(*i2cDevice.pmName()).second) {
            ccDevices.emplace_back(adapter, i2cDevice);
          }
        }
      }
    }
    return ccDevices;
  }

  void verifySlotIdsEqual(
      const std::map<std::string, std::map<std::string, std::string>>&
          allDeviceSlotIds,
      std::vector<std::string>& errorMessages) const {
    // 1. Verify that within each Cable Cartridge device, cc_slot_id_0 ==
    // cc_slot_id_1
    for (const auto& [devName, slotIds] : allDeviceSlotIds) {
      auto it0 = slotIds.find("cc_slot_id_0");
      auto it1 = slotIds.find("cc_slot_id_1");
      if (it0 != slotIds.end() && it1 != slotIds.end()) {
        if (it0->second != it1->second) {
          errorMessages.emplace_back(
              fmt::format(
                  "Slot ID mismatch within {}: cc_slot_id_0 ({}) != cc_slot_id_1 ({})",
                  devName,
                  it0->second,
                  it1->second));
        } else {
          XLOG(INFO) << fmt::format(
              "Slot IDs match within {}: cc_slot_id_0 == cc_slot_id_1 == {}",
              devName,
              it0->second);
        }
      }
    }

    // 2. Verify that across all Cable Cartridge devices, cc_slot_id values are
    // all equal
    if (allDeviceSlotIds.size() > 1) {
      std::string firstDevName;
      std::string expectedSlotIdVal;
      bool hasExpected = false;

      for (const auto& [devName, slotIds] : allDeviceSlotIds) {
        for (const auto& [fileName, val] : slotIds) {
          if (!hasExpected) {
            firstDevName = devName;
            expectedSlotIdVal = val;
            hasExpected = true;
          } else if (val != expectedSlotIdVal) {
            errorMessages.emplace_back(
                fmt::format(
                    "Slot ID mismatch across Cable Cartridge devices: {} has {} ({}) != {} slot ID ({})",
                    devName,
                    fileName,
                    val,
                    firstDevName,
                    expectedSlotIdVal));
          }
        }
      }

      if (hasExpected && errorMessages.empty()) {
        XLOG(INFO) << fmt::format(
            "Verified all {} Cable Cartridge device(s) have matching slot ID: {}",
            allDeviceSlotIds.size(),
            expectedSlotIdVal);
      }
    }
  }

  void runDeviceCableCartridges(
      const std::vector<std::pair<I2CAdapter, I2CDevice>>& ccDevices,
      std::vector<std::string>& errorMessages) {
    XLOG(INFO) << fmt::format(
        "Found {} Cable Cartridge device(s) in platform config to test:",
        ccDevices.size());
    for (const auto& [_, dev] : ccDevices) {
      XLOG(INFO) << "  - " << *dev.pmName();
    }

    std::map<std::string, std::map<std::string, std::string>> allDeviceSlotIds;
    int id = 1;
    for (const auto& [adapter, i2cDevice] : ccDevices) {
      try {
        XLOG(INFO) << fmt::format(
            "Checking Cable Cartridge device: {} (adapter: {}, address: {})",
            *i2cDevice.pmName(),
            *adapter.pmName(),
            *i2cDevice.address());

        const auto result = I2CUtils::createI2CAdapter(adapter, id);
        registerAdaptersForCleanup(result.createdAdapters);
        id += result.createdAdapters.size();

        int busNum = result.buses.at(*i2cDevice.channel()).busNum;
        if (!I2CUtils::createI2CDevice(i2cDevice, busNum)) {
          errorMessages.emplace_back(
              fmt::format(
                  "Failed to create I2C device {} on bus {}",
                  *i2cDevice.pmName(),
                  busNum));
          continue;
        }

        std::string path = I2CUtils::getI2CDir(busNum, *i2cDevice.address());
        auto slotIds = checkCableCartridgeCorrectness(
            *i2cDevice.pmName(), path, errorMessages);
        if (!slotIds.empty()) {
          allDeviceSlotIds[*i2cDevice.pmName()] = std::move(slotIds);
        }
      } catch (const std::exception& e) {
        errorMessages.emplace_back(
            fmt::format(
                "Exception during Cable Cartridge test for {}: {}",
                *i2cDevice.pmName(),
                e.what()));
      }
    }
    cleanupDevices();

    // Verify slot IDs equality (within device and across all Cable Cartridges)
    verifySlotIdsEqual(allDeviceSlotIds, errorMessages);
  }
};

// Main test: verify Cable Cartridges are created and check cc_slot_id_0 and
// cc_slot_id_1
TEST_F(CableCartridgesTest, CableCartridgesCreated) {
  auto ccDevices = getCableCartridgeDevices();
  if (ccDevices.empty()) {
    GTEST_SKIP()
        << "No Cable Cartridge devices found in platform I2C configuration";
  }

  std::vector<std::string> errorMessages;
  runDeviceCableCartridges(ccDevices, errorMessages);

  if (!errorMessages.empty()) {
    FAIL() << "Found " << errorMessages.size() << " errors:\n"
           << folly::join("\n", errorMessages);
  }
}

// Simple test to verify validateSlotIdFileName behavior
TEST_F(CableCartridgesTest, ValidateSlotIdFileName) {
  if (getCableCartridgeDevices().empty()) {
    GTEST_SKIP()
        << "No Cable Cartridge devices found in platform I2C configuration";
  }

  std::vector<std::string> errorMessages;
  validateSlotIdFileName("cc_slot_id_0", errorMessages);
  validateSlotIdFileName("cc_slot_id_1", errorMessages);

  XLOG(INFO) << "Errors for valid names: " << errorMessages.size();
  for (const auto& error : errorMessages) {
    XLOG(INFO) << "  Error: " << error;
  }
  EXPECT_TRUE(errorMessages.empty());
}

} // namespace facebook::fboss::platform::bsp_tests
