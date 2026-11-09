#include "fboss/platform/bsp_tests/utils/CdevUtils.h"

#include <fcntl.h>
#include <linux/limits.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>
#include <folly/logging/xlog.h>

#include "fboss/platform/bsp_tests/gen-cpp2/bsp_tests_runtime_config_types.h"
#include "fboss/platform/bsp_tests/gen-cpp2/fbiob_device_config_types.h"
#include "fboss/platform/platform_manager/uapi/fbiob-ioctl.h"

namespace facebook::fboss::platform::bsp_tests {

using namespace facebook::fboss::platform::bsp_tests::fbiob;
using PciDevice = facebook::fboss::platform::bsp_tests::PciDevice;
using AuxData = facebook::fboss::platform::bsp_tests::fbiob::AuxData;
using AuxDeviceType =
    facebook::fboss::platform::bsp_tests::fbiob::AuxDeviceType;

namespace {

// Convert AuxData to fbiob_aux_data
struct fbiob_aux_data getFbiobAuxData(const AuxData& auxDevice, int32_t id) {
  struct fbiob_aux_data auxData = {};

  // Set device name and ID
  strncpy(auxData.id.name, auxDevice.id()->deviceName()->c_str(), NAME_MAX - 1);
  auxData.id.id = id;

  // Set CSR and IOBUF offsets if provided
  if (auxDevice.csrOffset() && !auxDevice.csrOffset()->empty()) {
    auxData.csr_offset = std::stoi(*auxDevice.csrOffset(), nullptr, 16);
  }
  if (auxDevice.iobufOffset() && !auxDevice.iobufOffset()->empty()) {
    auxData.iobuf_offset = std::stoi(*auxDevice.iobufOffset(), nullptr, 16);
  }

  // Set device-specific data based on device type
  switch (*auxDevice.type()) {
    case AuxDeviceType::FAN:
      if (auxDevice.fanData()) {
        auxData.fan_data.num_fans = *auxDevice.fanData()->numFans();
      }
      break;

    case AuxDeviceType::I2C:
      if (auxDevice.i2cData()) {
        if (auxDevice.i2cData()->busFreqHz()) {
          auxData.i2c_data.bus_freq_hz = *auxDevice.i2cData()->busFreqHz();
        }
        if (auxDevice.i2cData()->numChannels()) {
          auxData.i2c_data.num_channels = *auxDevice.i2cData()->numChannels();
        }
      }
      break;

    case AuxDeviceType::SPI:
      if (auxDevice.spiData()) {
        auxData.spi_data.num_spidevs = *auxDevice.spiData()->numSpidevs();

        // Copy SPI device info for each device (up to FBIOB_SPIDEV_MAX)
        for (std::size_t i = 0; i <
             std::min(static_cast<std::size_t>(
                          *auxDevice.spiData()->numSpidevs()),
                      static_cast<std::size_t>(FBIOB_SPIDEV_MAX));
             i++) {
          const auto& spiDev = auxDevice.spiData()->spiDevices()[i];
          strncpy(
              auxData.spi_data.spidevs[i].modalias,
              spiDev.modalias()->c_str(),
              NAME_MAX - 1);
          auxData.spi_data.spidevs[i].max_speed_hz = *spiDev.maxSpeedHz();
          auxData.spi_data.spidevs[i].chip_select = *spiDev.chipSelect();
        }
      }
      break;

    case AuxDeviceType::LED:
      if (auxDevice.ledData()) {
        auxData.led_data.port_num = *auxDevice.ledData()->portNumber();
        auxData.led_data.led_idx = *auxDevice.ledData()->ledId();
      }
      break;

    case AuxDeviceType::XCVR:
      if (auxDevice.xcvrData()) {
        auxData.xcvr_data.port_num = *auxDevice.xcvrData()->portNumber();
      }
      break;
    case AuxDeviceType::GPIO:
      // Do nothing
      break;
    case AuxDeviceType::SYSLED:
      // Do nothing
      break;
    case AuxDeviceType::MDIO_BUS:
      // Do nothing
      break;
  }

  return auxData;
}

} // anonymous namespace

void CdevUtils::createNewDevice(
    const PciDeviceInfo& pciDevice,
    const AuxData& auxDevice,
    int32_t id) {
  struct fbiob_aux_data auxData = getFbiobAuxData(auxDevice, id);
  std::string path = makePciPath(pciDevice);

  int fd = open(path.c_str(), O_RDWR);
  if (fd < 0) {
    auto errorMsg =
        fmt::format("Failed to open device {}: {}", path, strerror(errno));
    XLOG(ERR) << errorMsg;
    throw std::runtime_error(errorMsg);
  }

  try {
    if (ioctl(fd, FBIOB_IOC_NEW_DEVICE, &auxData) < 0) {
      auto errorMsg = fmt::format(
          "Failed to create device {}: {}", *auxDevice.name(), strerror(errno));
      XLOG(ERR) << errorMsg;
      throw std::runtime_error(errorMsg);
    }
  } catch (const std::exception& e) {
    close(fd);
    XLOG(ERR) << "Exception: " << e.what();
    throw;
  }

  close(fd);
}

void CdevUtils::deleteDevice(
    const PciDeviceInfo& pciDevice,
    const AuxData& auxDevice,
    int id) {
  struct fbiob_aux_data auxData = getFbiobAuxData(auxDevice, id);
  std::string path = makePciPath(pciDevice);

  int fd = open(path.c_str(), O_RDWR);
  if (fd < 0) {
    auto errorMsg =
        fmt::format("Failed to open device {}: {}", path, strerror(errno));
    XLOG(ERR) << errorMsg;
    throw std::runtime_error(errorMsg);
  }

  try {
    if (ioctl(fd, FBIOB_IOC_DEL_DEVICE, &auxData) < 0) {
      auto errorMsg = fmt::format(
          "Failed to delete device {} at {}: {}",
          *auxDevice.name(),
          path,
          strerror(errno));
      XLOG(ERR) << errorMsg;
      throw std::runtime_error(errorMsg);
    }
  } catch (const std::exception& e) {
    close(fd);
    XLOG(ERR) << "Error deleting device: " << e.what();
    throw;
  }

  close(fd);
}

std::string CdevUtils::makePciPath(const PciDeviceInfo& pciDevice) {
  // Strip the leading "0x" from each ID
  std::string vendorId = pciDevice.vendorId()->substr(2);
  std::string deviceId = pciDevice.deviceId()->substr(2);
  std::string subsystemVendorId = pciDevice.subSystemVendorId()->substr(2);
  std::string subsystemDeviceId = pciDevice.subSystemDeviceId()->substr(2);

  return fmt::format(
      "/dev/fbiob_{}.{}.{}.{}",
      vendorId,
      deviceId,
      subsystemVendorId,
      subsystemDeviceId);
}

std::string CdevUtils::makePciName(const PciDeviceInfo& pciDevice) {
  return fmt::format(
      "{}.{}.{}.{}",
      *pciDevice.vendorId(),
      *pciDevice.deviceId(),
      *pciDevice.subSystemVendorId(),
      *pciDevice.subSystemDeviceId());
}

std::unordered_map<std::string, std::string> CdevUtils::findPciDirs(
    const std::vector<PciDeviceInfo>& pciDevices) {
  std::unordered_map<std::string, std::string> result;

  for (const auto& pciDevice : pciDevices) {
    bool found = false;
    for (const auto& entry :
         std::filesystem::directory_iterator("/sys/bus/pci/devices/")) {
      if (!std::filesystem::is_directory(entry.path())) {
        continue;
      }

      if (checkFilesForPci(pciDevice, entry.path().string())) {
        found = true;
        result[makePciName(pciDevice)] = entry.path().string();
        break;
      }
    }

    if (!found) {
      auto errorMsg = fmt::format(
          "Could not find dir for PCI device {}", makePciName(pciDevice));
      throw std::runtime_error(errorMsg);
    }
  }

  return result;
}

bool CdevUtils::checkFilesForPci(
    const PciDeviceInfo& pciDevice,
    const std::string& dirPath) {
  std::unordered_map<std::string, std::string> fileValues = {
      {"vendor", *pciDevice.vendorId()},
      {"device", *pciDevice.deviceId()},
      {"subsystem_vendor", *pciDevice.subSystemVendorId()},
      {"subsystem_device", *pciDevice.subSystemDeviceId()},
  };

  for (const auto& [filename, expectedValue] : fileValues) {
    std::string filePath = fmt::format("{}/{}", dirPath, filename);

    if (!std::filesystem::exists(filePath)) {
      return false;
    }

    std::ifstream file(filePath);
    if (!file.is_open()) {
      return false;
    }

    std::string content;
    std::getline(file, content);
    content.erase(
        content.find_last_not_of("\n\r") + 1); // Trim trailing newlines

    if (content != expectedValue) {
      return false;
    }
  }

  return true;
}

} // namespace facebook::fboss::platform::bsp_tests
