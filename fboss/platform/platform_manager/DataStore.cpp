// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/platform_manager/DataStore.h"

#include <fmt/format.h>
#include <folly/logging/xlog.h>

#include "fboss/platform/platform_manager/Utils.h"

namespace facebook::fboss::platform::platform_manager {

DataStore::DataStore(const PlatformConfig& config) : platformConfig_(config) {}

uint16_t DataStore::getI2cBusNum(
    const std::optional<std::string>& slotPath,
    const std::string& pmUnitScopeBusName) const {
  auto it = i2cBusNums_.find(std::make_pair(std::nullopt, pmUnitScopeBusName));
  if (it != i2cBusNums_.end()) {
    return it->second;
  }
  it = i2cBusNums_.find(std::make_pair(slotPath, pmUnitScopeBusName));
  if (it != i2cBusNums_.end()) {
    return it->second;
  }
  throw std::runtime_error(
      fmt::format(
          "Could not find bus number for {} at {}",
          pmUnitScopeBusName,
          slotPath.value_or("CPU Scope")));
}

void DataStore::updateI2cBusNum(
    const std::optional<std::string>& slotPath,
    const std::string& pmUnitScopeBusName,
    uint16_t busNum) {
  XLOG(INFO) << fmt::format(
      "Updating bus {} in {} to bus number {} (i2c-{})",
      pmUnitScopeBusName,
      slotPath ? fmt::format("SlotPath {}", *slotPath) : "Global Scope",
      busNum,
      busNum);
  i2cBusNums_[std::make_pair(slotPath, pmUnitScopeBusName)] = busNum;
}

PmUnitInfo DataStore::getPmUnitInfo(const std::string& slotPath) const {
  if (slotPathToPmUnitInfo.find(slotPath) != slotPathToPmUnitInfo.end()) {
    return slotPathToPmUnitInfo.at(slotPath);
  }
  throw std::runtime_error(
      fmt::format("Could not find PmUnit at {}", slotPath));
}

bool DataStore::hasPmUnit(const std::string& slotPath) const {
  // If we know nothing about the PmUnit at the slot, return false.
  if (slotPathToPmUnitInfo.find(slotPath) == slotPathToPmUnitInfo.end()) {
    return false;
  }
  // If the slot had a PresenceDetection logic, check whether we were able
  // to retrieve presence info.
  auto presenceInfo = slotPathToPmUnitInfo.at(slotPath).presenceInfo();
  if (presenceInfo && !*presenceInfo->isPresent()) {
    return false;
  }
  // If both the above conditions are false, then we know a PmUnit exists.
  return true;
}

std::string DataStore::getSysfsPath(const std::string& devicePath) const {
  auto itr = pciSubDevicePathToSysfsPath_.find(devicePath);
  if (itr != pciSubDevicePathToSysfsPath_.end()) {
    return itr->second;
  }
  throw std::runtime_error(
      fmt::format("Could not find SysfsPath for {}", devicePath));
}

void DataStore::updateSysfsPath(
    const std::string& devicePath,
    const std::string& sysfsPath) {
  XLOG(INFO) << fmt::format(
      "Updating SysfsPath for {} to {}", devicePath, sysfsPath);
  pciSubDevicePathToSysfsPath_[devicePath] = sysfsPath;
}

bool DataStore::hasSysfsPath(const std::string& devicePath) const {
  return pciSubDevicePathToSysfsPath_.find(devicePath) !=
      pciSubDevicePathToSysfsPath_.end();
}

std::string DataStore::getCharDevPath(const std::string& devicePath) const {
  auto itr = pciSubDevicePathToCharDevPath_.find(devicePath);
  if (itr != pciSubDevicePathToCharDevPath_.end()) {
    return itr->second;
  }
  throw std::runtime_error(
      fmt::format("Could not find CharDevPath for {}", devicePath));
}

void DataStore::updateCharDevPath(
    const std::string& devicePath,
    const std::string& charDevPath) {
  XLOG(INFO) << fmt::format(
      "Updating CharDevPath for {} to {}", devicePath, charDevPath);
  pciSubDevicePathToCharDevPath_[devicePath] = charDevPath;
}

void DataStore::updatePmUnitName(
    const std::string& slotPath,
    const std::string& name) {
  slotPathToPmUnitInfo[slotPath].name() = name;
  XLOG(INFO) << fmt::format(
      "At SlotPath {}, updating to PmUnit with name: {}", slotPath, name);
}

void DataStore::updatePmUnitVersion(
    const std::string& slotPath,
    const PmUnitVersion& version) {
  slotPathToPmUnitInfo[slotPath].version() = version;
  XLOG(INFO) << fmt::format(
      "At SlotPath {}, updating to PmUnit `{}` with {}",
      slotPath,
      *slotPathToPmUnitInfo[slotPath].name(),
      fmt::format(
          "ProductionState {}, ProductionSubState {}, RespinVariantIndicator {}",
          *version.productionState(),
          *version.productionSubState(),
          *version.respinVariantIndicator()));
}

void DataStore::updatePmUnitEepromProductName(
    const std::string& slotPath,
    const std::string& eepromProductName) {
  slotPathToPmUnitInfo[slotPath].eepromProductName() = eepromProductName;
  XLOG(INFO) << fmt::format(
      "At SlotPath {}, updating PmUnit eepromProductName: {}",
      slotPath,
      eepromProductName);
}

void DataStore::updatePmUnitSuccessfullyExplored(
    const std::string& slotPath,
    bool successfullyExplored) {
  slotPathToPmUnitInfo[slotPath].successfullyExplored() = successfullyExplored;
}

void DataStore::updatePmUnitPresenceInfo(
    const std::string& slotPath,
    const PresenceInfo& presenceInfo) {
  slotPathToPmUnitInfo[slotPath].presenceInfo() = presenceInfo;
}

PmUnitConfig DataStore::resolvePmUnitConfig(const std::string& slotPath) const {
  if (slotPathToPmUnitInfo.find(slotPath) == slotPathToPmUnitInfo.end()) {
    throw std::runtime_error(
        fmt::format("Unable to resolve PmUnitInfo for {}", slotPath));
  }
  const auto& pmUnitInfo = slotPathToPmUnitInfo.at(slotPath);
  const auto& pmUnitName = *pmUnitInfo.name();
  XLOG(INFO) << fmt::format(
      "Resolving PmUnitConfig of {} at {}", pmUnitName, slotPath);
  return Utils::resolvePmUnitConfig(
      platformConfig_, pmUnitName, pmUnitInfo.version().to_optional());
}

void DataStore::updateEepromContents(
    const std::string& devicePath,
    const FbossEepromInterface& contents) {
  XLOG(INFO) << fmt::format(
      "Updating EepromContents for DevicePath ({})", devicePath);
  eepromContents_.insert(std::make_pair(devicePath, contents));
}

FbossEepromInterface DataStore::getEepromContents(
    const std::string& devicePath) const {
  if (!hasEepromContents(devicePath)) {
    throw std::runtime_error(
        fmt::format(
            "Couldn't find EepromContents at DevicePath ({})", devicePath));
  }
  return eepromContents_.at(devicePath);
}

bool DataStore::hasEepromContents(const std::string& devicePath) const {
  return eepromContents_.contains(devicePath);
}

std::map<std::string, PmUnitInfo> DataStore::getSlotPathToPmUnitInfo() const {
  return slotPathToPmUnitInfo;
}

const std::map<std::pair<std::optional<std::string>, std::string>, uint16_t>
DataStore::getI2cBusNums() const {
  return i2cBusNums_;
}

void DataStore::updateFirmwareVersion(
    const std::string& deviceName,
    const std::string& firmwareVersion) {
  XLOG(INFO) << fmt::format(
      "Updating firmware version for {} to {}", deviceName, firmwareVersion);
  firmwareVersions_[deviceName] = firmwareVersion;
}

std::unordered_map<std::string, std::string> DataStore::getFirmwareVersions()
    const {
  return firmwareVersions_;
}

void DataStore::updateHardwareVersion(
    const std::string& fieldName,
    const std::string& hardwareVersion) {
  XLOG(INFO) << fmt::format(
      "Updating hardware version for {} to {}", fieldName, hardwareVersion);
  hardwareVersions_[fieldName] = hardwareVersion;
}

std::unordered_map<std::string, std::string> DataStore::getHardwareVersions()
    const {
  return hardwareVersions_;
}

} // namespace facebook::fboss::platform::platform_manager
