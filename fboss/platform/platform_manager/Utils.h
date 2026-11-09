// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include "fboss/platform/platform_manager/gen-cpp2/platform_manager_config_types.h"

namespace facebook::fboss::platform::platform_manager {

class Utils {
 public:
  virtual ~Utils() = default;
  // Extract (SlotPath, DeviceName) from DevicePath.
  // Returns a pair of (SlotPath, DeviceName). Throws if DevicePath is invalid.
  // Eg: /MCB_SLOT@0/[IDPROM] will return std::pair("/MCB_SLOT@0", "IDPROM")
  std::pair<std::string, std::string> parseDevicePath(
      const std::string& devicePath);

  // Construct and Return a DevicePath from given SlotPath and DeviceName
  // Eg: SlotPath:"/MCB_SLOT@0", DeviceName:"IDPROM" will return
  // /MCB_SLOT@0/[IDPROM]
  std::string createDevicePath(
      const std::string& slotPath,
      const std::string& deviceName);

  // Explore and resolve GpioChip's CharDevicePath for given SysfsPath.
  // Throws an exception when it fails to resolve CharDevicePath
  std::string resolveGpioChipCharDevPath(const std::string& sysfsPath);

  // Explore and resolve Watchdogs's CharDevicePath for given SysfsPath.
  // Throws an exception when it fails to resolve CharDevicePath
  std::string resolveWatchdogCharDevPath(const std::string& sysfsPath);

  // Explore and resolve MdioBus's CharDevicePath for given SysfsPath.
  // Throws an exception when it fails to resolve CharDevicePath
  std::string resolveMdioBusCharDevPath(uint32_t instanceId);

  bool checkDeviceReadiness(
      std::function<bool()>&& isDeviceReadyFunc,
      const std::string& onWaitMsg,
      std::chrono::seconds maxWaitSecs);

  virtual int getGpioLineValue(const std::string& charDevPath, int lineIndex)
      const;

  // Format the expression by substituting port, startPort, and led parameters
  std::string formatExpression(
      const std::string& expression,
      int port,
      int startPort,
      std::optional<int> led);

  // Evaluate a mathematical expression and return the result as a hex string
  std::string evaluateExpression(const std::string& expression);

  // Compute the expression and return the result as a string.
  std::string computeHexExpression(
      const std::string& expression,
      int port,
      int startPort = 1,
      std::optional<int> led = std::nullopt);

  // Replace hex literals with decimal values in expression string
  std::string convertHexLiteralsToDecimal(const std::string& expression);

  // Create the I2C Adapter Config block based on the given I2cAdapterConfig
  // residing at the given PciDevice. Throw std::runtime_error on failure.
  static std::vector<I2cAdapterConfig> createI2cAdapterConfigs(
      const PciDeviceConfig& pciDeviceConfig);

  // Create the XCVR Controller Config block based on the given xcvrCtrlConfig
  // residing at the given PciDevice. Throw std::runtime_error on failure.
  static std::vector<XcvrCtrlConfig> createXcvrCtrlConfigs(
      const PciDeviceConfig& pciDeviceConfig);

  // Create the LED Controller Config block based on the given ledCtrlConfig
  // residing at the given PciDevice. Throw std::runtime_error on failure.
  static std::vector<LedCtrlConfig> createLedCtrlConfigs(
      const PciDeviceConfig& pciDeviceConfig);

  // Create the MDIO BUS Controller Config block residing at the given
  // PciDevice. Throw std::runtime_error on failure.
  static std::vector<FpgaIpBlockConfig> createMdioBusConfigs(
      const PciDeviceConfig& pciDeviceConfig);

  // Create the RTM Controller Config block based on the given
  // fpgaIpBlockConfig residing at the given PciDevice. Throw std::runtime_error
  // on failure.
  static std::vector<RtmCtrlConfig> createRtmCtrlConfigs(
      const PciDeviceConfig& pciDeviceConfig);

  // Resolve the PmUnitConfig to use for `pmUnitName` on a PmUnit reporting
  // `version`. Returns the matching entry from `versionedPmUnitConfigs` when
  // one applies, otherwise the default entry from `pmUnitConfigs`. Throws
  // std::out_of_range if `pmUnitName` has no default PmUnitConfig.
  static PmUnitConfig resolvePmUnitConfig(
      const PlatformConfig& platformConfig,
      const std::string& pmUnitName,
      const std::optional<PmUnitVersion>& version);

  // Resolve every PmUnit in `platformConfig` against `pmUnitVersions`, a map
  // of PmUnit name to the version detected on this system. PmUnits absent from
  // the map resolve to their default PmUnitConfig.
  static std::map<std::string, PmUnitConfig> resolvePmUnitConfigs(
      const PlatformConfig& platformConfig,
      const std::map<std::string, PmUnitVersion>& pmUnitVersions);
};

} // namespace facebook::fboss::platform::platform_manager
