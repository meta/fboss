// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "fboss/platform/platform_manager/gen-cpp2/platform_manager_config_types.h"

namespace facebook::fboss::platform::platform_manager {

struct LogicalEeprom {
  std::string pmUnitScopedName;
  int16_t offset;
  std::string kernelDeviceName;
};

class ConfigValidator {
 public:
  virtual ~ConfigValidator() = default;
  bool isValid(const PlatformConfig& platformConfig);
  bool isValidPmUnitConfig(
      const std::map<std::string, SlotTypeConfig>& slotTypeConfigs,
      const PmUnitConfig& pmUnitConfig);
  bool isValidSlotTypeConfig(const SlotTypeConfig& slotTypeConfig);
  bool isValidSlotConfig(
      const SlotConfig& slotConfig,
      const std::string& slotName,
      const std::map<std::string, SlotTypeConfig>& slotTypeConfigs);
  bool isValidFpgaIpBlockConfig(const FpgaIpBlockConfig& fpgaIpBlockConfig);
  bool isValidLedCtrlBlockConfig(const LedCtrlBlockConfig& ledCtrlBlockConfig);
  bool isValidXcvrCtrlBlockConfig(
      const XcvrCtrlBlockConfig& xcvrCtrlBlockConfig);
  bool isValidI2cAdapterBlockConfig(
      const I2cAdapterBlockConfig& i2cAdapterBlockConfig);
  bool isValidI2cAdaptersFromCpu(
      const std::vector<std::string>& i2cAdaptersFromCpu);
  bool isValidPciDeviceConfig(const PciDeviceConfig& pciDeviceConfig);
  bool isValidI2cDeviceConfig(const I2cDeviceConfig& i2cDeviceConfig);
  bool isValidCpldSysfsAttrs(const std::vector<CpldSysfsAttr>& cpldSysfsAttrs);
  bool isValidFanCpldConfig(const FanCpldConfig& fanCpldConfig);
  bool isValidDevicePath(
      const PlatformConfig& platformConfig,
      const std::string& devicePath);
  bool isValidSymlink(const std::string& symlink);
  bool isValidPresenceDetection(const PresenceDetection& presenceDetection);
  bool isValidSpiDeviceConfigs(
      const std::vector<SpiDeviceConfig>& spiDeviceConfig);
  bool isValidBspKmodsRpmVersion(const std::string& bspKmodsRpmVersion);
  bool isValidBspKmodsRpmName(const std::string& bspKmodsRpmName);
  bool isValidVersionedPmUnitConfig(
      const std::string& pmUnitName,
      const std::vector<VersionedPmUnitConfig>& versionedPmUnitConfigs,
      const PmUnitConfig& defaultPmUnitConfig,
      const std::map<std::string, SlotTypeConfig>& slotTypeConfigs);
  bool isValidXcvrSymlinks(
      int16_t numXcvrs,
      const std::vector<std::string>& symlinks);
  bool isValidCsrOffsetCalc(
      const std::string& csrOffsetCalc,
      const int16_t& portNum,
      const int16_t& startPort,
      std::optional<int16_t> ledNum = std::nullopt);
  bool isValidIobufOffsetCalc(
      const std::string& iobufOffsetCalc,
      const int16_t& portNum,
      const int16_t& startPort,
      std::optional<int16_t> ledNum = std::nullopt);
  bool isValidPortRanges(
      const std::vector<std::pair<int16_t, int16_t>>& startPortAndNumPorts);
  bool isValidLedCtrlBlockXcvrCoverage(const PlatformConfig& config);
  bool isValidXcvrCtrlBlockXcvrCoverage(const PlatformConfig& config);
  bool isValidVersionedPciDeviceCoverage(
      const PmUnitConfig& defaultPmUnitConfig,
      const VersionedPmUnitConfig& versionedPmUnitConfig);
  bool isValidChassisEepromDevicePath(
      const PlatformConfig& platformConfig,
      const std::string& chassisEepromDevicePath);
  bool isValidLogicalEeprom(
      const PlatformConfig& config,
      const std::string& pmUnitName,
      const PmUnitConfig& pmUnitConfig);
  bool isValidPlatformWithoutPmOptics(const PlatformConfig& config);
  std::map<std::pair<std::string, std::string>, std::vector<LogicalEeprom>>
  getLogicalEeproms(
      const std::map<std::string, SlotTypeConfig>& slotTypeConfigs,
      const std::string& slotType,
      const PmUnitConfig& pmUnitConfig);
  bool isValidRtmCtrlBlockConfig(const RtmCtrlBlockConfig& rtmCtrlBlockConfig);
  bool isValidMdioBusBlockConfig(const MdioBusBlockConfig& mdioBusBlockConfig);

  // Used by other platform services config validation.
  virtual bool isValidSlotPath(
      const PlatformConfig& platformConfig,
      const std::string& slotPath);
  virtual bool isValidDeviceName(
      const PlatformConfig& platformConfig,
      const std::string& slotPath,
      const std::string& deviceName);
  virtual bool isValidPmUnitName(
      const PlatformConfig& platformConfig,
      const std::string& slotPath,
      const std::string& pmUnitName);
  int16_t numXcvrs_ = 0;
  int16_t numRtms_ = 0;

 private:
  void buildDeviceNameCache(const PlatformConfig& platformConfig);

  // Cache: SlotType -> set of valid device names for that slot type
  std::optional<
      std::unordered_map<std::string, std::unordered_set<std::string>>>
      deviceNamesBySlotType_;
};

} // namespace facebook::fboss::platform::platform_manager
