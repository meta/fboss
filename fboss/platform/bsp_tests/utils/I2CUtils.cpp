// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/platform/bsp_tests/utils/I2CUtils.h"
#include "fboss/platform/platform_manager/I2cAddr.h"
#include "fboss/platform/platform_manager/I2cExplorer.h"

#include <re2/re2.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <folly/FileUtil.h>
#include <folly/String.h>
#include <folly/logging/xlog.h>

#include "fboss/platform/bsp_tests/utils/CdevUtils.h"
#include "fboss/platform/helpers/PlatformUtils.h"
#include "fboss/platform/platform_manager/FanCpldManager.h"

namespace fs = std::filesystem;
namespace facebook::fboss::platform::bsp_tests {

std::string I2CUtils::findPciDirectory(PciDeviceInfo pci) {
  for (const auto& dirEntry : fs::directory_iterator("/sys/bus/pci/devices")) {
    std::string vendor, device, subSystemVendor, subSystemDevice;
    auto deviceFilePath = dirEntry.path() / "device";
    auto vendorFilePath = dirEntry.path() / "vendor";
    auto subSystemVendorFilePath = dirEntry.path() / "subsystem_vendor";
    auto subSystemDeviceFilePath = dirEntry.path() / "subsystem_device";
    if (!folly::readFile(vendorFilePath.c_str(), vendor)) {
      XLOG(ERR) << "Failed to read vendor file from " << dirEntry.path();
    }
    if (!folly::readFile(deviceFilePath.c_str(), device)) {
      XLOG(ERR) << "Failed to read device file from " << dirEntry.path();
    }
    if (!folly::readFile(subSystemVendorFilePath.c_str(), subSystemVendor)) {
      XLOG(ERR) << "Failed to read subsystem_vendor file from "
                << dirEntry.path();
    }
    if (!folly::readFile(subSystemDeviceFilePath.c_str(), subSystemDevice)) {
      XLOG(ERR) << "Failed to read subsystem_device file from "
                << dirEntry.path();
    }
    if (folly::trimWhitespace(vendor).str() == *pci.vendorId() &&
        folly::trimWhitespace(device).str() == *pci.deviceId() &&
        folly::trimWhitespace(subSystemVendor).str() ==
            *pci.subSystemVendorId() &&
        folly::trimWhitespace(subSystemDevice).str() ==
            *pci.subSystemDeviceId()) {
      return dirEntry.path().string();
    }
  }
  throw std::runtime_error(
      fmt::format(
          "No sysfs path found for vendorId: {}, deviceId: {}, "
          "subSystemVendorId: {}, subSystemDeviceId: {}",
          *pci.vendorId(),
          *pci.deviceId(),
          *pci.subSystemVendorId(),
          *pci.subSystemDeviceId()));
}

// Creates an adapter AND its parents e.g. a mux adapter that
// depends on a parent PCI adapter
I2CAdapterCreationResult I2CUtils::createI2CAdapter(
    const I2CAdapter& adapter,
    int32_t id) {
  I2CAdapterCreationResult result;

  // TODO: Check if it already exists
  if (*adapter.isCpuAdapter()) {
    // No need to create, just return the bus
    std::vector<std::string> i2cCpuAdapter = {*adapter.busName()};
    for (const auto& [busName, busNum] :
         platform_manager::I2cExplorer().getBusNums(i2cCpuAdapter)) {
      if (busName == *adapter.busName()) {
        I2CBus i2cBus;
        i2cBus.busNum = busNum;
        i2cBus.name = *adapter.busName();
        result.buses = {{0, i2cBus}};
        // CPU adapters are not created, so don't add to createdAdapters
        return result;
      }
    }
    throw std::runtime_error(
        fmt::format("CPU adapter {} not found", *adapter.busName()));
  } else if (adapter.pciAdapterInfo().has_value()) {
    auto pci = *adapter.pciAdapterInfo();
    // Ensure the adapter has I2C info
    if (!adapter.pciAdapterInfo()->auxData()->i2cData()) {
      throw std::runtime_error("I2C adapter does not have I2C info");
    }
    int numBuses = *pci.auxData()->i2cData()->numChannels();
    CdevUtils::createNewDevice(*pci.pciInfo(), *pci.auxData(), id);

    // find new buses from pci device path
    std::string pciDir = findPciDirectory(*pci.pciInfo());
    std::string i2cDir = findI2cDir(pciDir, adapter, id);
    std::map<int, I2CBus> newBuses;
    if (numBuses > 1) {
      for (int i = 0; i < numBuses; i++) {
        std::string channelFile = fmt::format("{}/channel-{}", i2cDir, i);
        if (!fs::exists(channelFile) || !fs::is_symlink(channelFile)) {
          throw std::runtime_error(
              fmt::format("{} does not exist or is not symlink", channelFile));
        }
        I2CBus bus;
        bus.busNum = platform_manager::I2cExplorer().extractBusNumFromPath(
            fs::read_symlink(channelFile));
        bus.name = getBusNameFromNum(bus.busNum);
        newBuses.insert({i, bus});
      }
    } else {
      std::string i2cBusDir;
      for (const auto& dirEntry : fs::directory_iterator(i2cDir)) {
        if (dirEntry.path().filename().string().find("i2c-") !=
            std::string::npos) {
          i2cBusDir = dirEntry.path().string();
          break;
        }
      }

      I2CBus bus;
      bus.busNum =
          platform_manager::I2cExplorer().extractBusNumFromPath(i2cBusDir);
      bus.name = getBusNameFromNum(bus.busNum);
      newBuses.insert({0, bus});
    }

    result.buses = newBuses;
    result.createdAdapters.push_back({adapter, id, newBuses});
    return result;
  } else if (adapter.muxAdapterInfo().has_value()) {
    // create parent adapter recursively and track all created adapters
    int parentId = id + 1;
    auto parentResult =
        createI2CAdapter(*adapter.muxAdapterInfo()->parentAdapter(), parentId);

    // Add all parent adapters to our result
    result.createdAdapters.insert(
        result.createdAdapters.end(),
        parentResult.createdAdapters.begin(),
        parentResult.createdAdapters.end());

    I2CDevice device;
    device.pmName() = *adapter.pmName();
    device.channel() = *adapter.muxAdapterInfo()->parentAdapterChannel();
    device.deviceName() = *adapter.muxAdapterInfo()->deviceName();
    device.address() = *adapter.muxAdapterInfo()->address();
    int busNum = parentResult.buses.at(*device.channel()).busNum;
    createI2CDevice(device, busNum);

    std::map<uint16_t, uint16_t> createdBuses =
        platform_manager::I2cExplorer().getMuxChannelI2CBuses(
            busNum, platform_manager::I2cAddr(*device.address()));
    std::map<int, I2CBus> newMuxBuses;
    for (const auto& [channel, bus] : createdBuses) {
      I2CBus newBus;
      newBus.busNum = bus;
      newBus.name = getBusNameFromNum(bus);
      newMuxBuses.insert({channel, newBus});
    }

    result.buses = newMuxBuses;
    result.createdAdapters.push_back({adapter, id, newMuxBuses});
    return result;
  } else {
    throw std::runtime_error("Unknown adapter type");
  }
}

std::string I2CUtils::getBusNameFromNum(int busNum) {
  auto busName = PlatformFsUtils().getStringFileContent(
      fmt::format("/sys/bus/i2c/devices/i2c-{}/name", busNum));
  if (busName->empty()) {
    throw std::runtime_error(
        fmt::format("Failed to open bus name file for bus {}", busNum));
  }
  return *busName;
}

std::string I2CUtils::findI2cDir(
    const std::string& pciDir,
    const I2CAdapter& adapter,
    int id) {
  std::string expectedEnding = fmt::format(
      ".{}.{}", *adapter.pciAdapterInfo()->auxData()->id()->deviceName(), id);
  for (const auto& dirEntry : std::filesystem::directory_iterator(pciDir)) {
    if ((dirEntry.path().string().ends_with(expectedEnding))) {
      return dirEntry.path().string();
    }
  }
  return "";
}

std::set<I2CBus> I2CUtils::findI2CBuses() {
  auto [exitCode, output] = PlatformUtils().execCommand("i2cdetect -l");

  if (exitCode != 0) {
    throw std::runtime_error(
        fmt::format(
            "Command failed with exit code {}: i2cdetect -l", exitCode));
  }

  std::set<I2CBus> buses;
  std::istringstream iss(output);
  std::string line;
  while (std::getline(iss, line)) {
    if (!line.empty()) {
      buses.insert(parseI2CDetectLine(line));
    }
  }

  return buses;
}

I2CBus I2CUtils::parseI2CDetectLine(const std::string& line) {
  std::vector<std::string> parts;
  std::istringstream iss(line);
  std::string part;

  while (std::getline(iss, part, '\t')) {
    parts.push_back(part);
  }

  if (parts.size() < 4) {
    throw std::runtime_error(
        fmt::format("Invalid i2cdetect line format: {}", line));
  }

  // Parse bus number from the first part (e.g., "i2c-1" -> 1)
  std::string busNumStr = parts[0].substr(parts[0].find('-') + 1);
  int busNum = std::stoi(busNumStr);

  return {busNum, parts[1], parts[2], parts[3]};
}

bool I2CUtils::detectI2CDevice(int bus, const std::string& hexAddr) {
  // Try different options for i2cdetect
  std::vector<std::vector<std::string>> options = {
      {"-y"}, {"-y", "-r"}, {"-y", "-q"}};

  for (const auto& opt : options) {
    std::vector<std::string> cmd = {"i2cdetect"};
    cmd.insert(cmd.end(), opt.begin(), opt.end());
    cmd.push_back(std::to_string(bus));
    cmd.push_back(hexAddr);
    cmd.push_back(hexAddr);

    try {
      auto [exitCode, output] =
          PlatformUtils().execCommand(folly::join(" ", cmd));
      if (exitCode != 0) {
        XLOG(WARN) << "i2cdetect command failed with exit code " << exitCode;
        continue; // Try next option if command failed
      }

      std::istringstream iss(output);
      std::string line;

      while (std::getline(iss, line)) {
        if (!line.empty()) {
          std::istringstream lineStream(line);
          std::string part;
          while (lineStream >> part) {
            // Remove "0x" prefix from hexAddr for comparison
            std::string addrSuffix = hexAddr.substr(2);
            if (part == addrSuffix) {
              return true;
            }
          }
        }
      }
    } catch (const std::exception& e) {
      XLOG(WARN) << "Error running i2cdetect with " << folly::join(" ", cmd)
                 << ": " << e.what();
      // Continue to the next option
    }
  }

  return false;
}

std::vector<std::string> I2CUtils::parseI2CDumpData(const std::string& data) {
  std::vector<std::string> dataBytes;
  std::istringstream iss(data);
  std::string line;

  // Skip the first line (header)
  std::getline(iss, line);

  // Process remaining lines
  while (std::getline(iss, line)) {
    std::istringstream lineStream(line);
    std::string part;

    // Skip the first column (address)
    lineStream >> part;

    // Process remaining parts
    while (lineStream >> part) {
      // Check if the part contains only hex characters
      bool isHex = true;
      for (char c : part) {
        if (!std::isxdigit(c)) {
          isHex = false;
          break;
        }
      }

      if (isHex) {
        dataBytes.push_back(part);
      }
    }
  }

  return dataBytes;
}

std::string I2CUtils::i2cDump(
    const int bus,
    const std::string& addr,
    const std::string& start,
    const std::string& end) {
  auto [exitCode, output] = PlatformUtils().execCommand(
      fmt::format(
          "i2cdump -y -r {}-{} {} {}", start, end, std::to_string(bus), addr));

  if (exitCode != 0) {
    throw std::runtime_error(
        fmt::format(
            "i2cdump command failed with exit code {}: bus={}, addr={}, start={}, end={}",
            exitCode,
            bus,
            addr,
            start,
            end));
  }

  return output;
}

std::string I2CUtils::i2cGet(
    const int bus,
    const std::string& addr,
    const std::string& reg) {
  auto [exitCode, output] = PlatformUtils().execCommand(
      fmt::format("i2cget -y {} {} {}", std::to_string(bus), addr, reg));

  if (exitCode != 0) {
    throw std::runtime_error(
        fmt::format(
            "i2cget command failed with exit code {}: bus={}, addr={}, reg={}",
            exitCode,
            bus,
            addr,
            reg));
  }

  return output.erase(output.find_last_not_of("\n\r") + 1);
}

void I2CUtils::i2cSet(
    const int bus,
    const std::string& addr,
    const std::string& reg,
    const std::string& data) {
  auto [exitCode, output] = PlatformUtils().execCommand(
      fmt::format(
          "i2cset -y {} {} {} {}", std::to_string(bus), addr, reg, data));

  if (exitCode != 0) {
    throw std::runtime_error(
        fmt::format(
            "i2cset command failed with exit code {}: bus={}, addr={}, reg={}, data={}",
            exitCode,
            bus,
            addr,
            reg,
            data));
  }
}

bool I2CUtils::createI2CDevice(const I2CDevice& device, int bus) {
  try {
    // Create the command to create the I2C device
    std::string cmd = fmt::format(
        "echo {} {} > /sys/bus/i2c/devices/i2c-{}/new_device",
        *device.deviceName(),
        *device.address(),
        bus);

    auto [exitCode, output] = PlatformUtils().execCommand(cmd);
    if (exitCode != 0) {
      XLOG(ERR) << "Failed to create I2C device: " << cmd
                << ", error code: " << exitCode;
      return false;
    }

    // Verify the device exists
    std::string devDir = "/sys/bus/i2c/devices";
    std::string busPath = fmt::format("{}/i2c-{}", devDir, bus);

    // Check if the bus directory exists
    if (!std::filesystem::exists(busPath)) {
      XLOG(ERR) << "Device " << *device.address() << " on bus " << bus
                << " not found: bus directory does not exist";
      return false;
    }

    // Get the address suffix (remove "0x" prefix and pad with zeros)
    std::string addrSuffix = (*device.address()).substr(2);
    while (addrSuffix.length() < 4) {
      addrSuffix = "0" + addrSuffix;
    }

    // Check if the device directory exists
    std::string devicePath = fmt::format("{}/{}-{}", devDir, bus, addrSuffix);
    if (!std::filesystem::exists(devicePath)) {
      XLOG(ERR) << "Device " << *device.address() << " on bus " << bus
                << " not found: device directory does not exist";
      return false;
    }

    // Read the name file and verify it is correct
    std::string namePath = fmt::format("{}/name", devicePath);
    std::ifstream nameFile(namePath);
    if (!nameFile.is_open()) {
      XLOG(ERR) << "Failed to open name file for device " << *device.address()
                << " on bus " << bus;
      return false;
    }

    std::string name;
    std::getline(nameFile, name);
    name.erase(name.find_last_not_of("\n\r") + 1); // Trim trailing newlines

    if (name != *device.deviceName()) {
      XLOG(ERR) << "Device " << *device.address() << " on bus " << bus
                << ": wrong device name: expected '" << *device.deviceName()
                << "' got '" << name << "'";
      return false;
    }

    if (device.fanCpldConfig().has_value()) {
      platform_manager::configureFanCpld(
          bus,
          platform_manager::I2cAddr(*device.address()),
          *device.fanCpldConfig());
    }

    return true;
  } catch (const std::exception& e) {
    XLOG(ERR) << "Failed to create device " << *device.deviceName()
              << ", error: " << e.what();
    return false;
  }
}

std::string I2CUtils::getI2CDir(int busNum, const std::string& address) {
  // Get the address suffix (remove "0x" prefix and pad with zeros)
  std::string addrSuffix = address.substr(2);
  while (addrSuffix.length() < 4) {
    addrSuffix = fmt::format("0{}", addrSuffix);
  }
  return fmt::format("/sys/bus/i2c/devices/{}-{}/", busNum, addrSuffix);
}

} // namespace facebook::fboss::platform::bsp_tests
