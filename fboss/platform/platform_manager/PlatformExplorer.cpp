// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/platform_manager/PlatformExplorer.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

#include <fb303/ServiceData.h>
#include <folly/FileUtil.h>
#include <folly/logging/xlog.h>
#include <re2/re2.h>

#include "fboss/platform/helpers/PlatformFsUtils.h"
#include "fboss/platform/helpers/PlatformUtils.h"
#include "fboss/platform/platform_manager/CpldManager.h"
#include "fboss/platform/platform_manager/FanCpldManager.h"
#include "fboss/platform/platform_manager/Utils.h"
#include "fboss/platform/weutil/FbossEepromInterface.h"
#include "fboss/platform/weutil/IoctlSmbusEepromReader.h"
#include "fboss/platform/weutil/ParserUtils.h"

namespace facebook::fboss::platform::platform_manager {
namespace {
constexpr auto kRootSlotPath = "/";
const re2::RE2 kLegacyXcvrName = "xcvr_\\d+";

std::string getSlotPath(
    const std::string& parentSlotPath,
    const std::string& slotName) {
  if (parentSlotPath == kRootSlotPath) {
    return fmt::format("{}{}", kRootSlotPath, slotName);
  } else {
    return fmt::format("{}/{}", parentSlotPath, slotName);
  }
}

// Return the first path passing file existence, or std::nullopt if neither
// exists. Conceptually (p1 || p2) where "truthiness" is file existence.
std::optional<std::filesystem::path> pathExistsOr(
    const std::filesystem::path& p1,
    const std::filesystem::path& p2,
    const facebook::fboss::platform::PlatformFsUtils* platformFsUtils) {
  if (platformFsUtils->exists(p1)) {
    return p1;
  }
  if (platformFsUtils->exists(p2)) {
    return p2;
  }
  return std::nullopt;
}

// Read an arbitrary version string from the file at given path.
//
// The file is expected to only contain the version string itself. If we fail to
// read a non-empty version string, or if the string contains whitespace or
// control characters, the string returned corresponds to an error.
//
// The version string is NOT parsed into integers, however, we will attempt to
// verify it matches XXX.YYY(.ZZZ) format and log if it does not.
std::string readVersionString(
    const std::string& path,
    const facebook::fboss::platform::PlatformFsUtils* platformFsUtils) {
  static const re2::RE2 kFwVerXYPattern =
      re2::RE2(PlatformExplorer::kFwVerXYPatternStr);
  static const re2::RE2 kFwVerXYZPattern =
      re2::RE2(PlatformExplorer::kFwVerXYZPatternStr);
  static const re2::RE2 kFwVerValidCharsPattern =
      re2::RE2(PlatformExplorer::kFwVerValidCharsPatternStr);
  const auto versionFileContent = platformFsUtils->getStringFileContent(path);
  if (!versionFileContent) {
    XLOGF(
        ERR,
        "Failed to open firmware version file {}: {}",
        path,
        folly::errnoStr(errno));
    return fmt::format("ERROR_FILE_{}", errno);
  }
  const auto& versionString = versionFileContent.value();
  if (versionString.empty()) {
    XLOGF(ERR, "Empty firmware version file {}", path);
    return PlatformExplorer::kFwVerErrorEmptyFile;
  }
  if (versionString.length() > 64) {
    XLOGF(
        ERR,
        "Firmware version \"{}\" from file {} is longer than 64 characters.",
        versionString,
        path);
    return PlatformExplorer::kFwVerErrorInvalidString;
  }
  if (!re2::RE2::FullMatch(versionString, kFwVerValidCharsPattern)) {
    XLOGF(
        ERR,
        "Firmware version \"{}\" from file {} contains invalid characters.",
        versionString,
        path);
    return PlatformExplorer::kFwVerErrorInvalidString;
  }
  // These names are for ease of reference. Neither semver nor any other
  // interpretation is enforced in fw_ver.
  // TODO: Revisit this when expanding coverage of fw_ver standardization,
  // e.g. if we include EEPROMs or similar without XXX.YYY.ZZZ versions.
  int major = 0;
  int minor = 0;
  int patch = 0;
  bool match = re2::RE2::FullMatch(
                   versionString, kFwVerXYZPattern, &major, &minor, &patch) ||
      re2::RE2::FullMatch(versionString, kFwVerXYPattern, &major, &minor);
  if (!match) {
    XLOGF(
        ERR,
        "Firmware version \"{}\" from file {} is not in XXX.YYY.ZZZ format.",
        versionString,
        path);
  }
  return versionString;
}

PlatformManagerStatus createPmStatus(
    const ExplorationStatus& explorationStatus,
    int64_t lastExplorationTime) {
  PlatformManagerStatus status;
  status.explorationStatus() = explorationStatus;
  status.lastExplorationTime() = lastExplorationTime;
  return status;
}

PlatformManagerStatus createPmStatus(
    const ExplorationStatus& explorationStatus) {
  return createPmStatus(explorationStatus, 0);
}
} // namespace

PlatformExplorer::PlatformExplorer(
    const PlatformConfig& config,
    DataStore& dataStore,
    std::shared_ptr<PlatformFsUtils> platformFsUtils)
    : platformConfig_(config),
      dataStore_(dataStore),
      explorationSummary_(platformConfig_),
      pciExplorer_(platformFsUtils),
      devicePathResolver_(dataStore_),
      presenceChecker_(devicePathResolver_),
      platformFsUtils_(std::move(platformFsUtils)) {
  updatePmStatus(createPmStatus(ExplorationStatus::UNSTARTED));
}

void PlatformExplorer::explore() {
  XLOG(INFO) << "Exploring the platform";
  updatePmStatus(createPmStatus(ExplorationStatus::IN_PROGRESS));
  for (const auto& [busName, busNum] :
       i2cExplorer_.getBusNums(*platformConfig_.i2cAdaptersFromCpu())) {
    dataStore_.updateI2cBusNum(std::nullopt, busName, busNum);
  }
  auto pmUnitName =
      getPmUnitNameFromSlot(*platformConfig_.rootSlotType(), kRootSlotPath);
  CHECK(pmUnitName == *platformConfig_.rootPmUnitName());
  explorePmUnit(kRootSlotPath, *platformConfig_.rootPmUnitName());
  XLOG(INFO) << "Creating symbolic links ...";
  for (const auto& [linkPath, devicePath] :
       *platformConfig_.symbolicLinkToDevicePath()) {
    createDeviceSymLink(linkPath, devicePath);
  }
  updateFirmwareVersions();
  XLOG(INFO) << "Publishing firmware versions ...";
  publishFirmwareVersions();
  XLOG(INFO) << "Generating human readable EEPROM contents ...";
  genHumanReadableEeproms();
  XLOG(INFO) << "Publishing hardware version of the unit ...";
  publishHardwareVersions();

  auto explorationStatus = explorationSummary_.summarize(
      dataStore_.getFirmwareVersions(), dataStore_.getHardwareVersions());
  updatePmStatus(createPmStatus(
      explorationStatus,
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count()));
}

void PlatformExplorer::explorePmUnit(
    const std::string& slotPath,
    const std::string& pmUnitName) {
  auto pmUnitConfig = dataStore_.resolvePmUnitConfig(slotPath);
  XLOG(INFO) << fmt::format("Exploring PmUnit {} at {}", pmUnitName, slotPath);
  auto pmUnitExploreStart = std::chrono::steady_clock::now();
  dataStore_.updatePmUnitSuccessfullyExplored(slotPath, false);
  XLOG(INFO) << fmt::format(
      "Exploring PCI Devices for PmUnit {} at SlotPath {}. Count {}",
      pmUnitName,
      slotPath,
      pmUnitConfig.pciDeviceConfigs()->size());
  explorePciDevices(slotPath, *pmUnitConfig.pciDeviceConfigs());

  XLOG(INFO) << fmt::format(
      "Exploring I2C Devices for PmUnit {} at SlotPath {}. Count {}",
      pmUnitName,
      slotPath,
      pmUnitConfig.i2cDeviceConfigs()->size());
  exploreI2cDevices(slotPath, *pmUnitConfig.i2cDeviceConfigs());

  if (!pmUnitConfig.embeddedSensorConfigs()->empty()) {
    XLOG(INFO) << fmt::format(
        "Processing Embedded Sensors for PmUnit {} at SlotPath {}. Count {}",
        pmUnitName,
        slotPath,
        pmUnitConfig.embeddedSensorConfigs()->size());
    for (const auto& embeddedSensorConfig :
         *pmUnitConfig.embeddedSensorConfigs()) {
      dataStore_.updateSysfsPath(
          Utils().createDevicePath(
              slotPath, *embeddedSensorConfig.pmUnitScopedName()),
          *embeddedSensorConfig.sysfsPath());
    }
  }
  auto pmUnitElapsedSeconds =
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::steady_clock::now() - pmUnitExploreStart)
          .count();
  auto pmUnitTimeCounterKey =
      fmt::format(kExplorePmUnitTime, slotPath + "." + pmUnitName);
  try {
    // Catch exception for this counter only since we included non-typical
    // characters such as "/", in case they are ever unsupported by fb303
    fb303::fbData->setCounter(pmUnitTimeCounterKey, pmUnitElapsedSeconds);
  } catch (const std::exception& ex) {
    XLOG(ERR) << fmt::format(
        "Error setting counter {}: {}", pmUnitTimeCounterKey, ex.what());
  }
  XLOG(INFO) << fmt::format(
      "Explored PmUnit {} at {} in {}s",
      pmUnitName,
      slotPath,
      pmUnitElapsedSeconds);

  dataStore_.updatePmUnitSuccessfullyExplored(slotPath, true);

  XLOG(INFO) << fmt::format(
      "Exploring Slots for PmUnit {} at SlotPath {}. Count {}",
      pmUnitName,
      slotPath,
      pmUnitConfig.outgoingSlotConfigs()->size());
  for (const auto& [slotName, slotConfig] :
       *pmUnitConfig.outgoingSlotConfigs()) {
    exploreSlot(slotPath, slotName, slotConfig);
  }
}

void PlatformExplorer::exploreSlot(
    const std::string& parentSlotPath,
    const std::string& slotName,
    const SlotConfig& slotConfig) {
  std::string childSlotPath = getSlotPath(parentSlotPath, slotName);
  XLOG(INFO) << fmt::format("Exploring SlotPath {}", childSlotPath);

  // If PresenceDetection is specified, proceed further only if the presence
  // condition is satisfied
  if (auto presenceDetection = slotConfig.presenceDetection()) {
    PresenceInfo presenceInfo;
    presenceInfo.presenceDetection() = *presenceDetection;
    presenceInfo.isPresent() = false;
    dataStore_.updatePmUnitPresenceInfo(childSlotPath, presenceInfo);
    try {
      auto isPmUnitPresent =
          presenceChecker_.isPresent(*presenceDetection, childSlotPath);
      presenceInfo.isPresent() = isPmUnitPresent;
      presenceInfo.actualValue() = presenceChecker_.getPresenceValue(
          presenceDetection.value(), childSlotPath);
      dataStore_.updatePmUnitPresenceInfo(childSlotPath, presenceInfo);
      if (!isPmUnitPresent) {
        auto errMsg = fmt::format(
            "Skipping exploring Slot {} at {}. No PmUnit in the Slot",
            slotName,
            childSlotPath);
        XLOG(ERR) << errMsg;
        explorationSummary_.addError(
            ExplorationErrorType::SLOT_PM_UNIT_ABSENCE,
            Utils().createDevicePath(childSlotPath, "<ABSENT>"),
            errMsg);
        return;
      }
    } catch (const std::exception& ex) {
      auto errMsg = fmt::format(
          "Error checking for presence in SlotPath {}: {}",
          slotName,
          ex.what());
      XLOG(ERR) << errMsg;
      explorationSummary_.addError(
          ExplorationErrorType::SLOT_PRESENCE_CHECK,
          Utils().createDevicePath(childSlotPath, "<ABSENT>"),
          errMsg);
      return;
    }
  }

  // Either no PresenceDetection is specified, or the presence conditions in
  // PresenceDetection are satisfied. Setup the PmUnit in the slot.
  int i = 0;
  for (const auto& busName : *slotConfig.outgoingI2cBusNames()) {
    auto busNum = dataStore_.getI2cBusNum(parentSlotPath, busName);
    dataStore_.updateI2cBusNum(
        childSlotPath, fmt::format("INCOMING@{}", i++), busNum);
  }
  auto childPmUnitName =
      getPmUnitNameFromSlot(*slotConfig.slotType(), childSlotPath);

  if (!childPmUnitName) {
    XLOG(INFO) << fmt::format(
        "No device could be read in Slot {}", childSlotPath);
    return;
  }

  explorePmUnit(childSlotPath, *childPmUnitName);
}

std::optional<std::string> PlatformExplorer::getPmUnitNameFromSlot(
    const std::string& slotType,
    const std::string& slotPath) {
  auto slotTypeConfig = platformConfig_.slotTypeConfigs()->at(slotType);
  CHECK(slotTypeConfig.idpromConfig() || slotTypeConfig.pmUnitName());
  std::optional<std::string> pmUnitNameInEeprom{std::nullopt};
  std::optional<int> productionStateInEeprom{std::nullopt};
  std::optional<int> productionSubStateInEeprom{std::nullopt};
  std::optional<int> respinVariantIndicatorInEeprom{std::nullopt};
  if (slotTypeConfig.idpromConfig()) {
    auto idpromConfig = *slotTypeConfig.idpromConfig();
    auto eepromI2cBusNum =
        dataStore_.getI2cBusNum(slotPath, *idpromConfig.busName());
    std::string eepromPath;

    /*
    Because of upstream kernel issues, we have to manually read the
    SCM EEPROM for the Meru800BFA/BIA & Icecube800banw platforms. It is read
    directly with ioctl and written to the /run/devmap file. See:
    https://github.com/facebookexternal/fboss.bsp.arista/pull/31/files
    */
    uint16_t eepromOffset = *idpromConfig.offset();
    if ((platformConfig_.platformName().value() == "MERU800BFA" ||
         platformConfig_.platformName().value() == "MERU800BIA" ||
         platformConfig_.platformName().value() == "ICECUBE800BANW" ||
         platformConfig_.platformName().value() == "BLACKWOLF800BANW") &&
        (!(idpromConfig.busName()->starts_with("INCOMING")) &&
         *idpromConfig.address() == "0x50")) {
      // ICECUBE800BANW has SMB at root level, others have SCM
      std::string eepromName =
          (platformConfig_.platformName().value() == "ICECUBE800BANW")
          ? "SMB_EEPROM"
          : "MERU_SCM_EEPROM";
      try {
        std::string eepromDir = "/run/devmap/eeproms/";
        eepromPath = eepromDir + eepromName;
        IoctlSmbusEepromReader::readEeprom(
            eepromDir,
            eepromName,
            eepromOffset,
            std::stoi(*idpromConfig.address(), nullptr, 16),
            dataStore_.getI2cBusNum(slotPath, *idpromConfig.busName()),
            kMaxEepromDataRegionSize);
        // The parsed file contains data starting at offset 0
        eepromOffset = 0;
      } catch (const std::exception& e) {
        auto errMsg = fmt::format(
            "Could not read EEPROM {} for {}: {}",
            eepromName,
            *idpromConfig.address(),
            e.what());
        XLOG(ERR) << errMsg;
        explorationSummary_.addError(
            ExplorationErrorType::IDPROM_READ, slotPath, "IDPROM", errMsg);
      }
    } else {
      createI2cDevice(
          Utils().createDevicePath(slotPath, "IDPROM"),
          *idpromConfig.kernelDeviceName(),
          eepromI2cBusNum,
          I2cAddr(*idpromConfig.address()));
      eepromPath = i2cExplorer_.getDeviceI2cPath(
          eepromI2cBusNum, I2cAddr(*idpromConfig.address()));
      eepromPath = eepromPath + "/eeprom";
    }
    try {
      auto idpromDevicePath = Utils().createDevicePath(slotPath, "IDPROM");
      dataStore_.updateEepromContents(
          idpromDevicePath, FbossEepromInterface(eepromPath, eepromOffset));
      const auto& eepromContents =
          dataStore_.getEepromContents(idpromDevicePath);
      if (idpromDevicePath == *platformConfig_.chassisEepromDevicePath()) {
        updateHardwareVersions(eepromContents);
      }
      pmUnitNameInEeprom = eepromContents.getProductName();
      if (pmUnitNameInEeprom) {
        dataStore_.updatePmUnitEepromProductName(slotPath, *pmUnitNameInEeprom);
      }
      productionStateInEeprom = std::stoi(eepromContents.getProductionState());
      productionSubStateInEeprom =
          std::stoi(eepromContents.getProductionSubState());
      respinVariantIndicatorInEeprom =
          std::stoi(eepromContents.getVariantVersion());
      XLOG(INFO) << fmt::format(
          "Found ProductionState `{}` ProductionSubState `{}` RespinVariantIndicator `{}` in IDPROM {} at {}",
          productionStateInEeprom ? std::to_string(*productionStateInEeprom)
                                  : "<ABSENT>",
          productionSubStateInEeprom
              ? std::to_string(*productionSubStateInEeprom)
              : "<ABSENT>",
          respinVariantIndicatorInEeprom
              ? std::to_string(*respinVariantIndicatorInEeprom)
              : "<ABSENT>",
          eepromPath,
          slotPath);

      if (productionStateInEeprom.has_value() &&
          productionSubStateInEeprom.has_value() &&
          respinVariantIndicatorInEeprom.has_value()) {
        PmUnitVersion version;
        version.productionState() = *productionStateInEeprom;
        version.productionSubState() = *productionSubStateInEeprom;
        version.respinVariantIndicator() = *respinVariantIndicatorInEeprom;
        dataStore_.updatePmUnitVersion(slotPath, version);
      } else {
        XLOG(WARNING) << fmt::format(
            "At SlotPath {}, unexpected partial versions: ProductionState `{}` "
            "ProductionSubState `{}` RespinVariantIndicator `{}`. Skipping updating PmUnit {}",
            slotPath,
            productionStateInEeprom ? std::to_string(*productionStateInEeprom)
                                    : "<ABSENT>",
            productionSubStateInEeprom
                ? std::to_string(*productionSubStateInEeprom)
                : "<ABSENT>",
            respinVariantIndicatorInEeprom
                ? std::to_string(*respinVariantIndicatorInEeprom)
                : "<ABSENT>",
            *pmUnitNameInEeprom);
      }
    } catch (const std::exception& e) {
      auto errMsg = fmt::format(
          "Could not fetch contents of IDPROM {} in {}. {}",
          eepromPath,
          slotPath,
          e.what());
      XLOG(ERR) << errMsg;
      explorationSummary_.addError(
          ExplorationErrorType::IDPROM_READ, slotPath, "IDPROM", errMsg);
    }
    if (pmUnitNameInEeprom) {
      XLOG(INFO) << fmt::format(
          "Found PmUnit name `{}` in IDPROM {} at {}",
          *pmUnitNameInEeprom,
          eepromPath,
          slotPath);
    }
  }

  auto pmUnitName = pmUnitNameInEeprom;
  if (slotTypeConfig.pmUnitName()) {
    if (pmUnitNameInEeprom &&
        *pmUnitNameInEeprom != *slotTypeConfig.pmUnitName()) {
      XLOG(WARNING) << fmt::format(
          "The PmUnit name in IDPROM -`{}` is different from the one "
          "in config - `{}`. NEEDS FIX.",
          *pmUnitNameInEeprom,
          *slotTypeConfig.pmUnitName());
    }
    XLOG(INFO) << fmt::format(
        "Going with PmUnit name `{}` defined in config for {}",
        *slotTypeConfig.pmUnitName(),
        slotPath);
    pmUnitName = *slotTypeConfig.pmUnitName();
  }
  if (!pmUnitName) {
    throw std::runtime_error(
        fmt::format(
            "PmUnitName must be configured in SlotTypeConfig::pmUnitName "
            "or SlotTypeConfig::idpromConfig at {}",
            slotPath));
  }
  dataStore_.updatePmUnitName(slotPath, *pmUnitName);
  return pmUnitName;
}

void PlatformExplorer::exploreI2cDevices(
    const std::string& slotPath,
    const std::vector<I2cDeviceConfig>& i2cDeviceConfigs) {
  for (const auto& i2cDeviceConfig : i2cDeviceConfigs) {
    try {
      auto busNum =
          dataStore_.getI2cBusNum(slotPath, *i2cDeviceConfig.busName());
      auto devAddr = I2cAddr(*i2cDeviceConfig.address());
      auto devicePath = Utils().createDevicePath(
          slotPath, *i2cDeviceConfig.pmUnitScopedName());
      if (i2cDeviceConfig.initRegSettings()) {
        setupI2cDevice(
            devicePath, busNum, devAddr, *i2cDeviceConfig.initRegSettings());
      }
      createI2cDevice(
          devicePath, *i2cDeviceConfig.kernelDeviceName(), busNum, devAddr);
      if (i2cDeviceConfig.fanCpldConfig()) {
        setupFanCpld(
            devicePath, busNum, devAddr, *i2cDeviceConfig.fanCpldConfig());
      }
      if (i2cDeviceConfig.numOutgoingChannels()) {
        auto channelToBusNums =
            i2cExplorer_.getMuxChannelI2CBuses(busNum, devAddr);
        if (channelToBusNums.size() != *i2cDeviceConfig.numOutgoingChannels()) {
          throw std::runtime_error(
              fmt::format(
                  "Unexpected number mux channels for {}. Expected: {}. Actual: {}",
                  *i2cDeviceConfig.pmUnitScopedName(),
                  *i2cDeviceConfig.numOutgoingChannels(),
                  channelToBusNums.size()));
        }
        for (const auto& [channelNum, channelBusNum] : channelToBusNums) {
          dataStore_.updateI2cBusNum(
              slotPath,
              fmt::format(
                  "{}@{}", *i2cDeviceConfig.pmUnitScopedName(), channelNum),
              channelBusNum);
        }
      }
      if (*i2cDeviceConfig.isGpioChip()) {
        auto i2cDevicePath = i2cExplorer_.getDeviceI2cPath(busNum, devAddr);
        dataStore_.updateCharDevPath(
            Utils().createDevicePath(
                slotPath, *i2cDeviceConfig.pmUnitScopedName()),
            Utils().resolveGpioChipCharDevPath(i2cDevicePath));
      }
      if (*i2cDeviceConfig.isWatchdog()) {
        auto i2cDevicePath = i2cExplorer_.getDeviceI2cPath(busNum, devAddr);
        dataStore_.updateCharDevPath(
            Utils().createDevicePath(
                slotPath, *i2cDeviceConfig.pmUnitScopedName()),
            Utils().resolveWatchdogCharDevPath(i2cDevicePath));
      }
      if (*i2cDeviceConfig.isEeprom()) {
        auto i2cDevicePath = i2cExplorer_.getDeviceI2cPath(busNum, devAddr);
        try {
          auto eepromPath = i2cDevicePath + "/eeprom";
          auto eepromOffset = i2cDeviceConfig.eepromOffset().value_or(0);
          dataStore_.updateEepromContents(
              Utils().createDevicePath(
                  slotPath, *i2cDeviceConfig.pmUnitScopedName()),
              FbossEepromInterface(eepromPath, eepromOffset));
          if (devicePath == *platformConfig_.chassisEepromDevicePath()) {
            const auto& eepromContents =
                dataStore_.getEepromContents(devicePath);
            updateHardwareVersions(eepromContents);
          }
        } catch (const std::exception& e) {
          auto errMsg = fmt::format(
              "Could not fetch contents of EEPROM device {} in {}. {}",
              *i2cDeviceConfig.pmUnitScopedName(),
              slotPath,
              e.what());
          XLOG(ERR) << errMsg;
          explorationSummary_.addError(
              ExplorationErrorType::IDPROM_READ,
              slotPath,
              *i2cDeviceConfig.pmUnitScopedName(),
              errMsg);
        }
      }
      if (i2cDeviceConfig.postCreateSysfsValues()) {
        for (const auto& sv : *i2cDeviceConfig.postCreateSysfsValues()) {
          auto path =
              i2cExplorer_.getDeviceI2cPath(busNum, devAddr) + "/" + *sv.attr();
          if (!platformFsUtils_->writeStringToSysfs(*sv.value(), path)) {
            explorationSummary_.addError(
                ExplorationErrorType::I2C_CONFIG_FAILED,
                slotPath,
                *i2cDeviceConfig.pmUnitScopedName(),
                fmt::format("failed writing {} to {}", *sv.value(), path));
          }
        }
      }
      if (i2cDeviceConfig.cpldSysfsAttrs() &&
          !i2cDeviceConfig.cpldSysfsAttrs()->empty()) {
        setupCpldSysfsAttrs(
            devicePath, busNum, devAddr, *i2cDeviceConfig.cpldSysfsAttrs());
        dataStore_.updateCharDevPath(
            devicePath, getCpldCharDevPath(busNum, devAddr));
      }
    } catch (const std::exception& ex) {
      auto errMsg = fmt::format(
          "Failed to explore I2C device {} at {}. {}",
          *i2cDeviceConfig.pmUnitScopedName(),
          slotPath,
          ex.what());
      XLOG(ERR) << errMsg;
      explorationSummary_.addError(
          ExplorationErrorType::I2C_DEVICE_EXPLORE,
          slotPath,
          *i2cDeviceConfig.pmUnitScopedName(),
          errMsg);
    }
  }
}

void PlatformExplorer::explorePciDevices(
    const std::string& slotPath,
    const std::vector<PciDeviceConfig>& pciDeviceConfigs) {
  for (const auto& pciDeviceConfig : pciDeviceConfigs) {
    auto pciDevice = PciDevice(pciDeviceConfig, platformFsUtils_);
    auto instId =
        getFpgaInstanceId(slotPath, *pciDeviceConfig.pmUnitScopedName());
    auto pciDevicePath = Utils().createDevicePath(slotPath, pciDevice.name());
    dataStore_.updateSysfsPath(pciDevicePath, pciDevice.sysfsPath());
    dataStore_.updateCharDevPath(pciDevicePath, pciDevice.charDevPath());

    auto createI2cAdapter = [&](const I2cAdapterConfig& i2cAdapterConfig) {
      auto busNums =
          pciExplorer_.createI2cAdapter(pciDevice, i2cAdapterConfig, instId++);
      if (*i2cAdapterConfig.numberOfAdapters() > 1) {
        CHECK_EQ(busNums.size(), *i2cAdapterConfig.numberOfAdapters());
        for (auto i = 0; i < busNums.size(); i++) {
          dataStore_.updateI2cBusNum(
              slotPath,
              fmt::format(
                  "{}@{}",
                  *i2cAdapterConfig.fpgaIpBlockConfig()->pmUnitScopedName(),
                  i),
              busNums[i]);
        }
      } else {
        CHECK_EQ(busNums.size(), 1);
        dataStore_.updateI2cBusNum(
            slotPath,
            *i2cAdapterConfig.fpgaIpBlockConfig()->pmUnitScopedName(),
            busNums[0]);
      }
    };
    auto i2cAdapterConfigs = Utils::createI2cAdapterConfigs(pciDeviceConfig);
    if (i2cAdapterConfigs.size() == 0) {
      i2cAdapterConfigs = *pciDeviceConfig.i2cAdapterConfigs();
    }
    createPciSubDevices(
        slotPath,
        i2cAdapterConfigs,
        ExplorationErrorType::PCI_SUB_DEVICE_CREATE_I2C_ADAPTER,
        createI2cAdapter);
    createPciSubDevices(
        slotPath,
        *pciDeviceConfig.spiMasterConfigs(),
        ExplorationErrorType::PCI_SUB_DEVICE_CREATE_SPI_MASTER,
        [&](const auto& spiMasterConfig) {
          auto spiCharDevPaths = pciExplorer_.createSpiMaster(
              pciDevice, spiMasterConfig, instId++);
          for (const auto& [pmUnitScopedName, spiCharDevPath] :
               spiCharDevPaths) {
            dataStore_.updateCharDevPath(
                Utils().createDevicePath(slotPath, pmUnitScopedName),
                spiCharDevPath);
          }
        });
    createPciSubDevices(
        slotPath,
        *pciDeviceConfig.gpioChipConfigs(),
        ExplorationErrorType::PCI_SUB_DEVICE_CREATE_GPIO_CHIP,
        [&](const auto& gpioChipConfig) {
          auto gpioCharDevPath =
              pciExplorer_.createGpioChip(pciDevice, gpioChipConfig, instId++);
          dataStore_.updateCharDevPath(
              Utils().createDevicePath(
                  slotPath, *gpioChipConfig.pmUnitScopedName()),
              gpioCharDevPath);
        });
    createPciSubDevices(
        slotPath,
        *pciDeviceConfig.watchdogConfigs(),
        ExplorationErrorType::PCI_SUB_DEVICE_CREATE_WATCH_DOG,
        [&](const auto& watchdogConfig) {
          auto watchdogCharDevPath =
              pciExplorer_.createWatchdog(pciDevice, watchdogConfig, instId++);
          dataStore_.updateCharDevPath(
              Utils().createDevicePath(
                  slotPath, *watchdogConfig.pmUnitScopedName()),
              watchdogCharDevPath);
        });
    createPciSubDevices(
        slotPath,
        *pciDeviceConfig.fanTachoPwmConfigs(),
        ExplorationErrorType::PCI_SUB_DEVICE_CREATE_FAN_CTRL,
        [&](const auto& fanPwmCtrlConfig) {
          auto fanCtrlSysfsPath = pciExplorer_.createFanPwmCtrl(
              pciDevice, fanPwmCtrlConfig, instId++);
          dataStore_.updateSysfsPath(
              Utils().createDevicePath(
                  slotPath,
                  *fanPwmCtrlConfig.fpgaIpBlockConfig()->pmUnitScopedName()),
              fanCtrlSysfsPath);
        });
    createPciSubDevices(
        slotPath,
        Utils::createLedCtrlConfigs(pciDeviceConfig),
        ExplorationErrorType::PCI_SUB_DEVICE_CREATE_LED_CTRL,
        [&](const auto& ledCtrlConfig) {
          pciExplorer_.createLedCtrl(pciDevice, ledCtrlConfig, instId++);
        });
    createPciSubDevices(
        slotPath,
        *pciDeviceConfig.ledCtrlConfigs(),
        ExplorationErrorType::PCI_SUB_DEVICE_CREATE_LED_CTRL,
        [&](const auto& ledCtrlConfig) {
          pciExplorer_.createLedCtrl(pciDevice, ledCtrlConfig, instId++);
        });
    createPciSubDevices(
        slotPath,
        *pciDeviceConfig.sysLedCtrlConfigs(),
        ExplorationErrorType::PCI_SUB_DEVICE_CREATE_LED_CTRL,
        [&](const auto& sysLedCtrlConfig) {
          pciExplorer_.createFpgaIpBlock(pciDevice, sysLedCtrlConfig, instId++);
        });
    createPciSubDevices(
        slotPath,
        Utils::createXcvrCtrlConfigs(pciDeviceConfig),
        ExplorationErrorType::PCI_SUB_DEVICE_CREATE_XCVR_CTRL,
        [&](const auto& xcvrCtrlConfig) {
          auto devicePath = Utils().createDevicePath(
              slotPath,
              *xcvrCtrlConfig.fpgaIpBlockConfig()->pmUnitScopedName());
          auto xcvrCtrlSysfsPath =
              pciExplorer_.createXcvrCtrl(pciDevice, xcvrCtrlConfig, instId++);
          dataStore_.updateSysfsPath(devicePath, xcvrCtrlSysfsPath);
        });
    createPciSubDevices(
        slotPath,
        *pciDeviceConfig.xcvrCtrlConfigs(),
        ExplorationErrorType::PCI_SUB_DEVICE_CREATE_XCVR_CTRL,
        [&](const auto& xcvrCtrlConfig) {
          auto devicePath = Utils().createDevicePath(
              slotPath,
              *xcvrCtrlConfig.fpgaIpBlockConfig()->pmUnitScopedName());
          auto xcvrCtrlSysfsPath =
              pciExplorer_.createXcvrCtrl(pciDevice, xcvrCtrlConfig, instId++);
          dataStore_.updateSysfsPath(devicePath, xcvrCtrlSysfsPath);
        });
    createPciSubDevices(
        slotPath,
        *pciDeviceConfig.infoRomConfigs(),
        ExplorationErrorType::PCI_SUB_DEVICE_CREATE_INFO_ROM,
        [&](const auto& infoRomConfig) {
          auto infoRomSysfsPath =
              pciExplorer_.createInfoRom(pciDevice, infoRomConfig, instId++);
          dataStore_.updateSysfsPath(
              Utils().createDevicePath(
                  slotPath, *infoRomConfig.pmUnitScopedName()),
              infoRomSysfsPath);
        });
    createPciSubDevices(
        slotPath,
        *pciDeviceConfig.miscCtrlConfigs(),
        ExplorationErrorType::PCI_SUB_DEVICE_CREATE_MISC_CTRL,
        [&](const auto& miscCtrlConfig) {
          auto miscCtrlSysfsPath = pciExplorer_.createFpgaIpBlock(
              pciDevice, miscCtrlConfig, instId++);
          dataStore_.updateSysfsPath(
              Utils().createDevicePath(
                  slotPath, *miscCtrlConfig.pmUnitScopedName()),
              miscCtrlSysfsPath);
        });
    createPciSubDevices(
        slotPath,
        Utils::createMdioBusConfigs(pciDeviceConfig),
        ExplorationErrorType::PCI_SUB_DEVICE_CREATE_MDIO_BUS,
        [&](const auto& mdioBusConfig) {
          auto instanceId = instId;
          auto mdioBusCharDevPath =
              pciExplorer_.createMdioBus(pciDevice, mdioBusConfig, instId++);
          dataStore_.updateCharDevPath(
              Utils().createDevicePath(
                  slotPath, *mdioBusConfig.pmUnitScopedName()),
              mdioBusCharDevPath);

          auto mdioBusSysfsPath = pciExplorer_.getMdioBusSysfsPath(
              pciDevice, mdioBusConfig, instanceId);
          dataStore_.updateSysfsPath(
              Utils().createDevicePath(
                  slotPath, *mdioBusConfig.pmUnitScopedName()),
              mdioBusSysfsPath);
        });
    createPciSubDevices(
        slotPath,
        Utils::createRtmCtrlConfigs(pciDeviceConfig),
        ExplorationErrorType::PCI_SUB_DEVICE_CREATE_RTM_CTRL,
        [&](const auto& rtmCtrlConfig) {
          auto devicePath = Utils().createDevicePath(
              slotPath, *rtmCtrlConfig.fpgaIpBlockConfig()->pmUnitScopedName());
          auto rtmCtrlSysfsPath =
              pciExplorer_.createRtmCtrl(pciDevice, rtmCtrlConfig, instId++);
          dataStore_.updateSysfsPath(devicePath, rtmCtrlSysfsPath);
        });
  }
}

uint32_t PlatformExplorer::getFpgaInstanceId(
    const std::string& slotPath,
    const std::string& fpgaName) {
  auto key = std::make_pair(slotPath, fpgaName);
  auto it = fpgaInstanceIds_.find(key);
  if (it == fpgaInstanceIds_.end()) {
    fpgaInstanceIds_[key] = 1000 * (fpgaInstanceIds_.size() + 1);
  }
  return fpgaInstanceIds_[key];
}

void PlatformExplorer::createDeviceSymLink(
    const std::string& linkPath,
    const std::string& devicePath) {
  auto linkParentPath = std::filesystem::path(linkPath).parent_path();
  if (!platformFsUtils_->createDirectories(linkParentPath.string())) {
    XLOG(ERR) << fmt::format(
        "Failed to create the parent path ({})", linkParentPath.string());
    return;
  }

  std::string targetPath;
  try {
    if (linkParentPath.string() == "/run/devmap/eeproms") {
      targetPath = devicePathResolver_.resolveEepromPath(devicePath);
    } else if (linkParentPath.string() == "/run/devmap/sensors") {
      targetPath = devicePathResolver_.resolveSensorPath(devicePath);
    } else if (linkParentPath.string() == "/run/devmap/cplds") {
      if (auto i2cDevicePath =
              devicePathResolver_.tryResolveI2cDevicePath(devicePath)) {
        targetPath = *i2cDevicePath;
      } else {
        targetPath = devicePathResolver_.resolvePciDevicePath(devicePath);
      }
    } else if (
        linkParentPath.string() == "/run/devmap/fpgas" ||
        linkParentPath.string() == "/run/devmap/inforoms") {
      targetPath = devicePathResolver_.resolvePciDevicePath(devicePath);
    } else if (linkParentPath.string() == "/run/devmap/i2c-busses") {
      targetPath = devicePathResolver_.resolveI2cBusPath(devicePath);
    } else if (
        linkParentPath.string() == "/run/devmap/gpiochips" ||
        linkParentPath.string() == "/run/devmap/flashes" ||
        linkParentPath.string() == "/run/devmap/watchdogs") {
      targetPath = devicePathResolver_.resolvePciSubDevCharDevPath(devicePath);
    } else if (linkParentPath.string() == "/run/devmap/xcvrs") {
      auto xcvrName = linkPath.substr(linkParentPath.string().length() + 1);
      // New XCVR paths
      if (xcvrName.starts_with("xcvr_ctrl")) {
        targetPath = devicePathResolver_.resolvePciSubDevSysfsPath(devicePath);
      }
      if (xcvrName.starts_with("xcvr_io")) {
        targetPath = devicePathResolver_.resolveI2cBusPath(devicePath);
      }
      // Legacy XCVR path
      if (re2::RE2::FullMatch(xcvrName, kLegacyXcvrName)) {
        targetPath = devicePathResolver_.resolvePciSubDevSysfsPath(devicePath);
      }
    } else if (linkParentPath.string() == "/run/devmap/mdio-busses") {
      auto mdioBusName = linkPath.substr(linkParentPath.string().length() + 1);
      if (mdioBusName.starts_with("mdio_bus_io")) {
        targetPath =
            devicePathResolver_.resolvePciSubDevCharDevPath(devicePath);
      }
      if (mdioBusName.starts_with("mdio_bus_ctrl")) {
        targetPath = devicePathResolver_.resolvePciSubDevSysfsPath(devicePath);
      }
    } else if (linkParentPath.string() == "/run/devmap/rtms") {
      auto rtmName = linkPath.substr(linkParentPath.string().length() + 1);
      if (rtmName.starts_with("rtm_ctrl")) {
        targetPath = devicePathResolver_.resolvePciSubDevSysfsPath(devicePath);
      }
    } else {
      throw std::runtime_error(
          fmt::format("Symbolic link {} is not supported.", linkPath));
    }
  } catch (const std::exception& ex) {
    auto errMsg = fmt::format(
        "Failed to create symlink {} for DevicePath {}. Reason: {}",
        linkPath,
        devicePath,
        ex.what());
    XLOG(ERR) << errMsg;
    explorationSummary_.addError(
        ExplorationErrorType::RUN_DEVMAP_SYMLINK, devicePath, errMsg);
    return;
  }
  XLOG(INFO) << fmt::format(
      "Creating symlink from {} to {}. DevicePath: {}",
      linkPath,
      targetPath,
      devicePath);
  auto cmd = fmt::format("ln -sfnv {} {}", targetPath, linkPath);
  auto [exitStatus, standardOut] = PlatformUtils().execCommand(cmd);
  if (exitStatus != 0) {
    XLOG(ERR) << fmt::format("Failed to run command ({})", cmd);
    return;
  }
}

void PlatformExplorer::updateFirmwareVersions() {
  for (const auto& [linkPath, _] :
       *platformConfig_.symbolicLinkToDevicePath()) {
    if (!linkPath.starts_with("/run/devmap/cplds") &&
        !linkPath.starts_with("/run/devmap/inforoms")) {
      continue;
    }
    std::vector<folly::StringPiece> linkPathParts;
    folly::split('/', linkPath, linkPathParts, true);
    // Note: The vector is guaranteed to be non-empty due to the prefix check.
    CHECK(!linkPathParts.empty());
    const auto deviceName = linkPathParts.back();
    std::string versionString;
    std::string linkPathHwmon = linkPath;
    // Check for and handle hwmon case. e.g.
    // /run/devmap/cplds/FAN0_CPLD/hwmon/hwmon20/
    auto hwmonSubdirPath = std::filesystem::path(linkPath) / "hwmon";
    if (platformFsUtils_->exists(hwmonSubdirPath)) {
      for (const auto& entry : platformFsUtils_->ls(hwmonSubdirPath)) {
        if (entry.is_directory() &&
            entry.path().filename().string().starts_with("hwmon")) {
          linkPathHwmon = hwmonSubdirPath / entry.path().filename();
          break;
        }
      }
    }
    std::optional<std::filesystem::path> fwVerFilePath = pathExistsOr(
        std::filesystem::path(linkPath) / "fw_ver",
        std::filesystem::path(linkPathHwmon) / "fw_ver",
        platformFsUtils_.get());
    if (fwVerFilePath.has_value()) {
      versionString =
          readVersionString(fwVerFilePath.value(), platformFsUtils_.get());
    } else {
      versionString = PlatformExplorer::kFwVerErrorFileNotFound;
    }

    dataStore_.updateFirmwareVersion(
        std::string(deviceName.data(), deviceName.size()), versionString);
  }
}

void PlatformExplorer::publishFirmwareVersions() {
  auto firmwareVersions = dataStore_.getFirmwareVersions();
  for (const auto& [deviceName, versionString] : firmwareVersions) {
    XLOGF(
        INFO,
        "Reporting firmware version for {} - version string:{}",
        deviceName,
        versionString);
    fb303::fbData->setCounter(
        fmt::format(kGroupedFirmwareVersion, deviceName, versionString), 1);
  }
}

void PlatformExplorer::updateHardwareVersions(
    const FbossEepromInterface& chassisEepromContent) {
  auto prodState = chassisEepromContent.getProductionState();
  auto prodSubState = chassisEepromContent.getProductionSubState();
  auto variantVersion = chassisEepromContent.getVariantVersion();

  dataStore_.updateHardwareVersion(
      kChassisEepromVersion, std::to_string(chassisEepromContent.getVersion()));
  if (!prodState.empty()) {
    dataStore_.updateHardwareVersion(kProductionState, prodState);
  }
  if (!prodSubState.empty()) {
    dataStore_.updateHardwareVersion(kProductionSubState, prodSubState);
  }
  if (!variantVersion.empty()) {
    dataStore_.updateHardwareVersion(kVariantVersion, variantVersion);
  }
}

void PlatformExplorer::publishHardwareVersions() {
  auto hardwareVersions = dataStore_.getHardwareVersions();

  if (hardwareVersions.empty()) {
    XLOG(ERR) << "Failed to report hardware versions. No versions available.";
    return;
  }
  // Report chassis EEPROM version
  auto versionIt = hardwareVersions.find(kChassisEepromVersion);
  if (versionIt != hardwareVersions.end()) {
    fb303::fbData->setCounter(
        fmt::format(kChassisEepromVersionODS, versionIt->second), 1);
  }
  // Report production state
  auto prodStateIt = hardwareVersions.find(kProductionState);
  if (prodStateIt != hardwareVersions.end()) {
    XLOG(INFO) << fmt::format(
        "Reporting Production State: {}", prodStateIt->second);
    fb303::fbData->setCounter(
        fmt::format(kProductionStateODS, prodStateIt->second), 1);
  } else {
    XLOG(ERR) << "Production State not set";
  }
  // Report production sub-state
  auto prodSubStateIt = hardwareVersions.find(kProductionSubState);
  if (prodSubStateIt != hardwareVersions.end()) {
    XLOG(INFO) << fmt::format(
        "Reporting Production Sub-State: {}", prodSubStateIt->second);
    fb303::fbData->setCounter(
        fmt::format(kProductionSubStateODS, prodSubStateIt->second), 1);
  } else {
    XLOG(ERR) << "Production Sub-State not set";
  }
  // Report variant version
  auto variantIt = hardwareVersions.find(kVariantVersion);
  if (variantIt != hardwareVersions.end()) {
    XLOG(INFO) << fmt::format(
        "Reporting Variant Indicator: {}", variantIt->second);
    fb303::fbData->setCounter(
        fmt::format(kVariantVersionODS, variantIt->second), 1);
  } else {
    XLOG(ERR) << "Variant Indicator not set";
  }
  // Report combined chassis variant: <production_state>.<sub_state>.<variant>
  if (prodStateIt != hardwareVersions.end() &&
      prodSubStateIt != hardwareVersions.end() &&
      variantIt != hardwareVersions.end()) {
    auto combined = fmt::format(
        "{}.{}.{}",
        prodStateIt->second,
        prodSubStateIt->second,
        variantIt->second);
    fb303::fbData->setCounter(fmt::format(kFullVersionODS, combined), 1);
  }
  // Report per-PmUnit versions. Keyed by name+version, so duplicate PmUnit
  // names (e.g. multiple PSUs) collapse into one counter per unique version.
  for (const auto& [slotPath, pmUnitInfo] :
       dataStore_.getSlotPathToPmUnitInfo()) {
    auto version = pmUnitInfo.version();
    if (!version) {
      fb303::fbData->setCounter(
          fmt::format(kPmUnitVersionODS, *pmUnitInfo.name(), "unspecified"), 1);
      continue;
    }
    auto versionStr = fmt::format(
        "{}.{}.{}",
        *version->productionState(),
        *version->productionSubState(),
        *version->respinVariantIndicator());
    fb303::fbData->setCounter(
        fmt::format(kPmUnitVersionODS, *pmUnitInfo.name(), versionStr), 1);
  }
}

PlatformManagerStatus PlatformExplorer::getPMStatus() const {
  return platformManagerStatus_.copy();
}

PmUnitInfo PlatformExplorer::getPmUnitInfo(const std::string& slotPath) const {
  return dataStore_.getPmUnitInfo(slotPath);
}

void PlatformExplorer::updatePmStatus(const PlatformManagerStatus& newStatus) {
  platformManagerStatus_.withWLock([&](PlatformManagerStatus& status) {
    status = newStatus;
    if (status.explorationStatus() == ExplorationStatus::FAILED) {
      status.failedDevices() = explorationSummary_.getFailedDevices();
    }
  });
}

std::optional<DataStore> PlatformExplorer::getDataStore() const {
  bool ready = false;
  platformManagerStatus_.withRLock([&](const PlatformManagerStatus& status) {
    ready =
        (status.explorationStatus() != ExplorationStatus::IN_PROGRESS &&
         status.explorationStatus() != ExplorationStatus::UNSTARTED);
  });
  if (ready) {
    return dataStore_;
  }
  return std::nullopt;
}

void PlatformExplorer::setupI2cDevice(
    const std::string& devicePath,
    uint16_t busNum,
    const I2cAddr& addr,
    const std::vector<I2cRegData>& initRegSettings) {
  try {
    i2cExplorer_.setupI2cDevice(busNum, addr, initRegSettings);
  } catch (const std::exception& ex) {
    XLOG(ERR) << ex.what();
    explorationSummary_.addError(
        ExplorationErrorType::I2C_DEVICE_REG_INIT, devicePath, ex.what());
  }
}

void PlatformExplorer::setupCpldSysfsAttrs(
    const std::string& devicePath,
    uint16_t busNum,
    const I2cAddr& addr,
    const std::vector<CpldSysfsAttr>& cpldSysfsAttrs) {
  try {
    createCpldSysfsAttrs(busNum, addr, cpldSysfsAttrs);
  } catch (const std::exception& ex) {
    XLOG(ERR) << ex.what();
    explorationSummary_.addError(
        ExplorationErrorType::CPLD_SYSFS_ATTR_CREATE, devicePath, ex.what());
  }
}

void PlatformExplorer::setupFanCpld(
    const std::string& devicePath,
    uint16_t busNum,
    const I2cAddr& addr,
    const FanCpldConfig& fanCpldConfig) {
  try {
    configureFanCpld(busNum, addr, fanCpldConfig);
  } catch (const std::exception& ex) {
    XLOG(ERR) << ex.what();
    explorationSummary_.addError(
        ExplorationErrorType::FAN_CPLD_CONFIGURE, devicePath, ex.what());
  }
}

void PlatformExplorer::createI2cDevice(
    const std::string& devicePath,
    const std::string& deviceName,
    uint16_t busNum,
    const I2cAddr& addr) {
  const auto& [slotPath, pmUnitScopedName] =
      Utils().parseDevicePath(devicePath);
  try {
    i2cExplorer_.createI2cDevice(pmUnitScopedName, deviceName, busNum, addr);
    dataStore_.updateSysfsPath(
        devicePath, i2cExplorer_.getDeviceI2cPath(busNum, addr));
  } catch (const std::exception& ex) {
    XLOG(ERR) << ex.what();
    explorationSummary_.addError(
        ExplorationErrorType::I2C_DEVICE_CREATE, devicePath, ex.what());
  }
}

template <typename T>
void PlatformExplorer::createPciSubDevices(
    const std::string& slotPath,
    const std::vector<T>& pciSubDeviceConfigs,
    ExplorationErrorType errorType,
    auto&& deviceCreationLambda) {
  for (const auto& pciSubDeviceConfig : pciSubDeviceConfigs) {
    try {
      deviceCreationLambda(pciSubDeviceConfig);
    } catch (PciSubDeviceRuntimeError& ex) {
      auto errMsg = fmt::format(
          "Failed to explore PCISubDevice {} at {}. Details: {}",
          ex.getPmUnitScopedName(),
          slotPath,
          ex.what());
      XLOG(ERR) << errMsg;
      explorationSummary_.addError(
          errorType, slotPath, ex.getPmUnitScopedName(), errMsg);
    }
  }
}

void PlatformExplorer::genHumanReadableEeproms() {
  const auto writeEepromContent = [&](const auto& devicePath,
                                      const auto& eepromRuntimePath) {
    // Ignore if eeprom content wasn't populated, something went wrong in
    // exploration.
    if (!dataStore_.hasEepromContents(devicePath)) {
      XLOG(ERR) << fmt::format(
          "{} has empty eeprom contents. Skip generating eeprom content",
          devicePath);
      return;
    }
    auto contents = dataStore_.getEepromContents(devicePath).getContents();
    std::ostringstream os;
    for (const auto& [key, value] : contents) {
      os << fmt::format("{}: {}\n", key, value);
    }
    platformFsUtils_->writeStringToFile(
        os.str(), fmt::format("{}_PARSED", eepromRuntimePath));
  };

  for (const auto& [linkPath, devicePath] :
       *platformConfig_.symbolicLinkToDevicePath()) {
    if (!linkPath.starts_with("/run/devmap/eeproms")) {
      continue;
    }

    const auto [slotPath, deviceName] = Utils().parseDevicePath(devicePath);

    if (!dataStore_.hasPmUnit(slotPath)) {
      XLOG(ERR) << fmt::format(
          "No device at {}. Skipping creating parsed eeprom content for {}",
          slotPath,
          devicePath);
      continue;
    }

    auto pmUnitConfig = dataStore_.resolvePmUnitConfig(slotPath);
    std::optional<I2cDeviceConfig> matchingConfig;

    // Search through i2cDeviceConfigs to find one with matching
    // pmUnitScopedName
    for (const auto& i2cDeviceConfig : *pmUnitConfig.i2cDeviceConfigs()) {
      if (*i2cDeviceConfig.pmUnitScopedName() == deviceName) {
        matchingConfig = i2cDeviceConfig;
        break;
      }
    }

    bool isEeprom = matchingConfig && *matchingConfig->isEeprom();
    if (deviceName != "IDPROM" && !isEeprom) {
      XLOG(WARNING) << fmt::format(
          "{} is not IDPROM or EEPROM. Skip generating eeprom content.",
          linkPath);
      continue;
    }
    writeEepromContent(devicePath, linkPath);
  }

  // In DSF, there's the kernel driver binding issue at the eeproms' addresses,
  // Hence, the eeproms were created through custom utility and not listed under
  // `symbolicLinkToDevicePath()`
  // See: https://github.com/facebookexternal/fboss.bsp.arista/pull/31/files
  if ((platformConfig_.platformName().value() == "MERU800BFA" ||
       platformConfig_.platformName().value() == "MERU800BIA" ||
       platformConfig_.platformName().value() == "BLACKWOLF800BANW") &&
      std::filesystem::exists("/run/devmap/eeproms/MERU_SCM_EEPROM")) {
    writeEepromContent("/[IDPROM]", "/run/devmap/eeproms/MERU_SCM_EEPROM");
  }
  if (platformConfig_.platformName().value() == "ICECUBE800BANW" &&
      std::filesystem::exists("/run/devmap/eeproms/SMB_EEPROM")) {
    writeEepromContent("/[IDPROM]", "/run/devmap/eeproms/SMB_EEPROM");
  }
}

} // namespace facebook::fboss::platform::platform_manager
