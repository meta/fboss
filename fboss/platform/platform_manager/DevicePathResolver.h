// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <chrono>

#include "fboss/platform/platform_manager/DataStore.h"
#include "fboss/platform/platform_manager/I2cExplorer.h"
#include "fboss/platform/platform_manager/gen-cpp2/platform_manager_config_types.h"

namespace facebook::fboss::platform::platform_manager {

class DevicePathResolver {
 public:
  virtual ~DevicePathResolver() = default;
  explicit DevicePathResolver(const DataStore& dataStore);
  // sensorPathWaitSecs bounds how long resolveSensorPath() waits for a sensor
  // endpoint to appear. Tests override it to keep negative cases fast.
  DevicePathResolver(
      const DataStore& dataStore,
      std::chrono::seconds sensorPathWaitSecs);

  // Resolves a given DevicePath to the sensor sysfs path in the system.
  // Throws a runtime exception if devicePath fails to resolve to I2cDevicePath
  // or fails to resolve to sensor sysfs path.
  std::string resolveSensorPath(const std::string& devicePath) const;

  // Resolves a given DevicePath to the eeprom sysfs path in the system.
  // Throws a runtime exception if devicePath fails to resolve to I2cDevicePath
  // or fails to resolve to eeprom sysfs path.
  std::string resolveEepromPath(const std::string& devicePath);

  // Resolves a given DevicePath to I2cBusPath in the system.
  // Throws a runtime exception if the DevicePath cannot be resolved.
  std::string resolveI2cBusPath(const std::string& devicePath);

  // Resolves a given PciSubDevicePath to CharDevPath in the system.
  // Throws a runtime exception if the DevicePath cannot be resolved.
  std::string resolvePciSubDevSysfsPath(const std::string& devicePath);

  // Resolves a given PciSubDevicePath to CharDevPath in the system.
  // Throws a runtime exception if the DevicePath cannot be resolved.
  virtual std::string resolvePciSubDevCharDevPath(
      const std::string& devicePath) const;

  // Try resolving a given DevicePath to i2c sysfs path in the system.
  std::optional<std::string> tryResolveI2cDevicePath(
      const std::string& devicePath);

  // Resolves a given DevicePath to pci sysfs path in the system.
  // Throws a runtime exception if the DevicePath cannot be resolved.
  std::string resolvePciDevicePath(const std::string& devicePath);

  // Resolves the presenceFileName present at the given devicePath.
  virtual std::optional<std::string> resolvePresencePath(
      const std::string& devicePath,
      const std::string& presenceFileName) const;

 private:
  // Get PciDeviceConfig for a given SlotPath and DeviceName.
  // Throws a runtime exception if the PciDeviceConfig is not found.
  PciDeviceConfig getPciDeviceConfig(
      const std::string& slotPath,
      const std::string& deviceName);

  const DataStore& dataStore_;
  std::chrono::seconds sensorPathWaitSecs_;
};

} // namespace facebook::fboss::platform::platform_manager
