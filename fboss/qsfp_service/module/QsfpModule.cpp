/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/qsfp_service/module/QsfpModule.h"

#include <string>

#include <re2/re2.h>

#include <boost/assign.hpp>

#include <fmt/core.h>
#include <folly/String.h>
#include <folly/io/IOBuf.h>
#include <folly/io/async/EventBase.h>
#include <folly/logging/xlog.h>

#include "fboss/agent/FbossError.h"
#include "fboss/lib/AlertLogger.h"
#include "fboss/lib/link_snapshots/AsyncFileWriterFactory.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"
#include "fboss/qsfp_service/StatsPublisher.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"
#include "fboss/qsfp_service/module/TransceiverImpl.h"
#include "fboss/qsfp_service/module/cmis/gen-cpp2/cmis_types.h"
#include "fboss/qsfp_service/module/properties/TransceiverPropertiesManager.h"

DEFINE_int32(
    qsfp_data_refresh_interval,
    10,
    "how often to refetch qsfp data that changes frequently");
DEFINE_int32(
    customize_interval,
    30,
    "minimum interval between customizing the same down port twice");
DEFINE_int32(
    remediate_interval,
    360,
    "seconds between running more destructive remediations on down ports");
DEFINE_int32(
    initial_remediate_interval,
    120,
    "seconds to wait before running first destructive remediations on down ports after bootup");
DEFINE_int32(
    time_for_tcvr_ready_after_fw_upgrade_s,
    60,
    "max time after firmware upgrade sequence when the the tcvr is expected to be ready for link up");
DEFINE_bool(remediation_enabled, true, "Flag to disable/enable remediation.");
DEFINE_int32(
    refresh_all_pages_cycles,
    100,
    "Number of cycles to kick off a full refresh of all pages");
DEFINE_int32(
    firmware_upgrade_attempts,
    3,
    "Number of attempts to upgrade transceiver firmware before giving up. "
    "Set to 1 to disable retries (e.g. for interrupt tests).");

using folly::IOBuf;
using std::lock_guard;
using std::memcpy;
using std::mutex;

namespace facebook {
namespace fboss {

TransceiverID QsfpModule::getID() const {
  return TransceiverID(qsfpImpl_->getNum());
}

std::string QsfpModule::getNameString() const {
  return qsfpImpl_->getName().toString();
}

// Converts power from milliwatts to decibel-milliwatts
double QsfpModule::mwToDb(double value) {
  if (value <= 0.01) {
    return -40;
  }
  return 10 * log10(value);
};

/*
 * Given a byte, extract bit fields for various alarm flags;
 * note the we might want to use the lower or the upper nibble,
 * so offset is the number of the bit to start at;  this is usually
 * 0 or 4.
 */

FlagLevels QsfpModule::getQsfpFlags(const uint8_t* data, int offset) {
  FlagLevels flags;

  CHECK_GE(offset, 0);
  CHECK_LE(offset, 4);
  flags.warn()->low() = (*data & (1 << offset));
  flags.warn()->high() = (*data & (1 << ++offset));
  flags.alarm()->low() = (*data & (1 << ++offset));
  flags.alarm()->high() = (*data & (1 << ++offset));

  return flags;
}

QsfpModule::QsfpModule(
    std::set<std::string> portNames,
    TransceiverImpl* qsfpImpl,
    std::string tcvrName,
    std::shared_ptr<const TransceiverConfig> tcvrConfig)
    : Transceiver(),
      qsfpImpl_(qsfpImpl),
      tcvrConfig_(std::move(tcvrConfig)),
      portNames_(portNames),
      tcvrName_(std::move(tcvrName)) {
  CHECK(!portNames.empty())
      << "No portNames attached to this transceiver in platform mapping";
  StatsPublisher::initPerPortFb303Stats(portNames);
  primaryPortName_ = *portNames.begin();
  markLastDownTime();
}

QsfpModule::~QsfpModule() {}

void QsfpModule::removeTransceiver() {
  lock_guard<std::mutex> g(qsfpModuleMutex_);
  removeTransceiverLocked();
}

void QsfpModule::removeTransceiverLocked() {
  qsfpImpl_->updateTransceiverState(
      TransceiverStateMachineEvent::TCVR_EV_REMOVE_TRANSCEIVER);
}

void QsfpModule::getQsfpValue(
    int dataAddress,
    int offset,
    int length,
    uint8_t* data) const {
  const uint8_t* ptr = getQsfpValuePtr(dataAddress, offset, length);

  memcpy(data, ptr, length);
}

bool QsfpModule::upgradeFirmware(
    std::vector<std::unique_ptr<FbossFirmware>>& fwList) {
  // Always use i2cEvb to program transceivers if there's an i2cEvb
  auto i2cEvb = qsfpImpl_->getI2cEventBase();
  auto upgradeFwFn = [&]() -> bool {
    lock_guard<std::mutex> g(qsfpModuleMutex_);
    return upgradeFirmwareLocked(fwList);
  };

  if (!i2cEvb) {
    try {
      return upgradeFwFn();
    } catch (const std::exception& ex) {
      QSFP_LOG(ERR, this) << "Error calling upgradeFirmwareLocked(): "
                          << ex.what();
    }
    return false;
  }

  bool fwUpgradeStatus = false;
  via(i2cEvb)
      .thenValue([&, upgradeFwFn](auto&&) mutable {
        try {
          fwUpgradeStatus = upgradeFwFn();
        } catch (const std::exception& ex) {
          QSFP_LOG(ERR, this)
              << "Error calling upgradeFirmwareLocked(): " << ex.what();
        }
      })
      .get();
  return fwUpgradeStatus;
}

std::string QsfpModule::getFwStorageHandle() const {
  auto cachedTcvrInfo = getTransceiverInfo();
  auto vendor = cachedTcvrInfo.tcvrState()->vendor();
  if (!vendor.has_value()) {
    QSFP_LOG(ERR, this)
        << "Vendor not set. Can't find the partnumber to upgrade";
    return std::string();
  }

  const auto& tcvrPartNumber = vendor->partNumber().value();
  if (tcvrConfig_) {
    const auto& handleFromConfig = tcvrConfig_->partNumberToFwHandle_;
    auto fwHandle = handleFromConfig.find(tcvrPartNumber);
    if (fwHandle != handleFromConfig.end()) {
      return fwHandle->second;
    }
    StatsPublisher::bumpFwStorageHandleMissingFromConfig();
    QSFP_LOG(INFO, this)
        << "No firmware storage handle in qsfp config for part number: "
        << tcvrPartNumber << ". Falling back to the built in map";
  }

  return getFwStorageHandle(tcvrPartNumber);
}

bool QsfpModule::upgradeFirmwareLocked(
    std::vector<std::unique_ptr<FbossFirmware>>& fwList) {
  QSFP_LOG(INFO, this) << "Upgrading firmware";

  lastFwUpgradeStartTime_ = std::time(nullptr);
  auto fwUpgradeFn = [this, &fwList]() {
    bool fwUpgradeResult = true;
    // Start of firmware upgrade
    // Step 1: Disable TX before upgrading the firmware. This helps keeps the
    // link down during upgrade and avoid noise
    try {
      std::set<uint8_t> allTcvrLineLanes;
      for (uint8_t lane = 0; lane < numMediaLanes(); lane++) {
        allTcvrLineLanes.insert(lane);
      }
      auto txDisableStatus = setTransceiverTxImplLocked(
          allTcvrLineLanes /* tcvrLanes */,
          phy::Side::LINE /* side */,
          std::nullopt /* channelMask */,
          false /* enable */);
      if (!txDisableStatus) {
        QSFP_LOG(ERR, this)
            << "Failed to disable tx on port. Still continuing with firmware upgrade";
      } else {
        QSFP_LOG(INFO, this) << "TX Disabled successfully";
      }
    } catch (const FbossError& e) {
      QSFP_LOG(ERR, this) << "Failed to disable tx on port : " << e.what()
                          << " : Still continuing with firmware upgrade";
    }

    for (auto& fw : fwList) {
      // Step 2: First ensure the module is out of lower mode
      TransceiverSettings settings = getTransceiverSettingsInfo();
      setPowerOverrideIfSupportedLocked(*settings.powerControl());

      // Step 2b: Wait for the module to be ready after exiting low power before
      // starting the firmware download; CDB operations are unreliable
      // otherwise.
      if (!moduleReadyStatePoll()) {
        QSFP_LOG(ERR, this)
            << "Module not in ready state before firmware upgrade. Continuing anyway";
      }

      // Step 3: Mark the module dirty so that we can refresh the entire cache
      // later
      dirty_ = true;

      // Step 4: Upgrade Firmware
      fwUpgradeResult &= upgradeFirmwareLockedImpl(fw.get());

      // Step 5: Trigger a hard reset of the transceiver to kick start the new
      // firmware
      triggerModuleReset();
      // If there are more than 1 firmware to update on the optic (for modules
      // that have separate MCU and DSP firmwares), then update the cache in
      // preparation for the next upgrade.
      // Adding additional 3s beyond the 2s already in triggerModuleReset, to
      // keep it consistent with the prior delay. We should check and remove it
      // if its not needed
      // @lint-ignore CLANGTIDY facebook-hte-BadCall-sleep
      sleep(3);
      updateQsfpData(true);
      updateCachedTransceiverInfoLocked({});
    }

    // End of Firmware Upgrade
    return fwUpgradeResult;
  };

  int upgradeAttempts = 1;
  bool finalFwUpgradeResult = false;
  while (upgradeAttempts <= FLAGS_firmware_upgrade_attempts) {
    finalFwUpgradeResult = fwUpgradeFn();
    if (!finalFwUpgradeResult &&
        upgradeAttempts < FLAGS_firmware_upgrade_attempts) {
      // fwUpgradeFn will wait 5 seconds internally when re-trying.
      upgradeAttempts++;
    } else {
      break;
    }
  }
  lastFwUpgradeEndTime_ = std::time(nullptr);
  auto elapsedSeconds = lastFwUpgradeEndTime_ - lastFwUpgradeStartTime_;

  QSFP_LOG(INFO, this) << "Firmware upgrade completed "
                       << (finalFwUpgradeResult ? "successfully"
                                                : "unsuccessfully")
                       << " in " << elapsedSeconds
                       << " seconds, attempts = " << upgradeAttempts;
  return finalFwUpgradeResult;
}

void QsfpModule::triggerModuleReset() {
  qsfpImpl_->triggerQsfpHardReset();
  // The hardware is back at its defaults now, so whatever we were tracking
  // about datapath programming describes a datapath that no longer exists.
  resetDatapathProgrammingStateLocked();
  // Required delay time between a transceiver getting out of reset and fully
  // functional.
  //
  // This blocks with qsfpModuleMutex_ held, which is acceptable given the two
  // callers: remediation, which is being removed fleetwide, and the reset that
  // follows a firmware download, which already holds the lock far longer than
  // 2s.
  // @lint-ignore CLANGTIDY facebook-hte-BadCall-sleep
  sleep(kSecAfterModuleOutOfReset);
}

// Note that this needs to be called while holding the
// qsfpModuleMutex_
bool QsfpModule::cacheIsValid() const {
  return present_ && !dirty_;
}

TransceiverInfo QsfpModule::getTransceiverInfo() const {
  auto cachedInfo = info_.rlock();
  if (!cachedInfo->has_value()) {
    throw QsfpModuleError("Still populating data...");
  }
  return **cachedInfo;
}

std::string QsfpModule::getPartNumber() const {
  std::string partNumber = "UNKNOWN";
  try {
    auto transceiverInfo = getTransceiverInfo();
    const auto& cachedTcvrState = transceiverInfo.tcvrState();
    const auto& vendor = cachedTcvrState.value().vendor();
    if (vendor.has_value()) {
      partNumber = vendor.value().partNumber().value();
    }
  } catch (const std::exception& ex) {
    QSFP_LOG(ERR, this) << "Error calling getTransceiverInfo(): " << ex.what();
  }
  return partNumber;
}

Transceiver::TransceiverPresenceDetectionStatus QsfpModule::detectPresence() {
  lock_guard<std::mutex> g(qsfpModuleMutex_);
  return detectPresenceLocked();
}

Transceiver::TransceiverPresenceDetectionStatus
QsfpModule::detectPresenceLocked() {
  auto currentQsfpStatus = qsfpImpl_->detectTransceiver();
  bool statusChanged{false};
  if (currentQsfpStatus != present_) {
    QSFP_LOG(DBG1, this) << "QSFP status changed from "
                         << (present_ ? "PRESENT" : "NOT PRESENT") << " to "
                         << (currentQsfpStatus ? "PRESENT" : "NOT PRESENT");
    dirty_ = true;
    present_ = currentQsfpStatus;
    statusChanged = true;

    // If a transceiver went from present to missing, clear the cached data.
    if (!present_) {
      info_.wlock()->reset();
    }

    // In the case of an OBO module or an inaccessable present module,
    // we need to fill in the essential info before parsing the DOM data
    // which may not be available.
    TransceiverInfo info;

    auto& tcvrState = info.tcvrState().ensure();
    tcvrState.present() = present_;
    tcvrState.transceiver() = type();
    tcvrState.port() = qsfpImpl_->getNum();

    *info_.wlock() = info;
  }
  return {currentQsfpStatus, statusChanged};
}

const folly::EventBase* QsfpModule::getEvb() const {
  return qsfpImpl_->getI2cEventBase();
}

unsigned int QsfpModule::numHostLanes() const {
  auto code = getModuleMediaInterface();
  if (TransceiverPropertiesManager::isKnown(code)) {
    return TransceiverPropertiesManager::getNumHostLanes(code);
  }
  switch (code) {
    case MediaInterfaceCode::LR_10G:
    case MediaInterfaceCode::SR_10G:
    case MediaInterfaceCode::BASE_T_10G:
    case MediaInterfaceCode::CR_10G:
    case MediaInterfaceCode::CR1_100G:
    case MediaInterfaceCode::CR1_200G:
      return 1;
    case MediaInterfaceCode::CR2_400G:
      return 2;
    case MediaInterfaceCode::CWDM4_100G:
    case MediaInterfaceCode::CR4_100G:
    case MediaInterfaceCode::FR1_100G:
    case MediaInterfaceCode::CR4_200G:
    case MediaInterfaceCode::CR4_400G:
    case MediaInterfaceCode::CR4_800G:
      return 4;
    case MediaInterfaceCode::CR8_400G:
    case MediaInterfaceCode::CR8_800G:
    case MediaInterfaceCode::CR8_1600G:
      return 8;
    case MediaInterfaceCode::UNKNOWN:
      return 0;
    default:
      throw QsfpModuleError("invalid module media interface");
  }
}

unsigned int QsfpModule::numMediaLanes() const {
  auto code = getModuleMediaInterface();
  if (TransceiverPropertiesManager::isKnown(code)) {
    return TransceiverPropertiesManager::getNumMediaLanes(code);
  }
  switch (code) {
    case MediaInterfaceCode::LR_10G:
    case MediaInterfaceCode::SR_10G:
    case MediaInterfaceCode::FR1_100G:
    case MediaInterfaceCode::BASE_T_10G:
    case MediaInterfaceCode::CR_10G:
    case MediaInterfaceCode::CR1_100G:
    case MediaInterfaceCode::CR1_200G:
      return 1;
    case MediaInterfaceCode::CR2_400G:
      return 2;
    case MediaInterfaceCode::CWDM4_100G:
    case MediaInterfaceCode::CR4_100G:
    case MediaInterfaceCode::CR4_200G:
    case MediaInterfaceCode::CR4_400G:
    case MediaInterfaceCode::CR4_800G:
      return 4;
    case MediaInterfaceCode::CR8_400G:
    case MediaInterfaceCode::CR8_800G:
    case MediaInterfaceCode::CR8_1600G:
      return 8;
    case MediaInterfaceCode::UNKNOWN:
      return 0;
    default:
      throw QsfpModuleError("invalid module media interface");
  }
}

void QsfpModule::updateCachedTransceiverInfoLocked(ModuleStatus moduleStatus) {
  // Migration plan from fields in TransceiverInfo to being inside state/states:
  // 1. Populate data to old fields then populate state/states from old fields
  // 2. Migrate users to new fields.
  // 3. Populate data to new fields then populate old fields from new fields
  // guarded by a just knob.
  // 4. Use the knob to slowly turn off old fields, check that nobody dies.
  // 5. Remove old fields
  //
  // We're currently at step 1.

  TransceiverInfo info;

  auto& tcvrState = *info.tcvrState();
  auto& tcvrStats = *info.tcvrStats();

  tcvrState.present() = present_;
  tcvrState.transceiver() = type();
  tcvrState.port() = qsfpImpl_->getNum();

  if (present_) {
    auto nMediaLanes = numMediaLanes();
    tcvrState.mediaLaneSignals() = std::vector<MediaLaneSignals>(nMediaLanes);
    if (!getSignalsPerMediaLane(*tcvrState.mediaLaneSignals())) {
      tcvrState.mediaLaneSignals()->clear();
    } else {
      cacheMediaLaneSignals(*tcvrState.mediaLaneSignals());
    }

    auto sensorInfo = getSensorInfo();
    if (auto tempFlags = sensorInfo.temp()->flags()) {
      if (*tempFlags->alarm()->high()) {
        StatsPublisher::bumpHighTemp();
        StatsPublisher::bumpHighTempPort(primaryPortName_);
      }
    }
    if (auto vccFlags = sensorInfo.vcc()->flags()) {
      if (*vccFlags->alarm()->high() || *vccFlags->warn()->high()) {
        StatsPublisher::bumpHighVcc();
        StatsPublisher::bumpHighVccPort(primaryPortName_);
      }
    }
    tcvrStats.sensor() = sensorInfo;
    tcvrState.vendor() = getVendorInfo();
    if (tcvrConfig_ && tcvrConfig_->vendorOverrides_) {
      tcvrState.vendorOverrides() = *tcvrConfig_->vendorOverrides_;
    }
    tcvrState.cable() = getCableInfo();
    if (auto threshold = getThresholdInfo()) {
      tcvrState.thresholds() = *threshold;
    }
    tcvrState.settings() = getTransceiverSettingsInfo();

    for (int i = 0; i < nMediaLanes; i++) {
      Channel chan;
      chan.channel() = i;
      tcvrStats.channels()->push_back(chan);
    }
    if (!getSensorsPerChanInfo(*tcvrStats.channels())) {
      tcvrStats.channels()->clear();
    }

    tcvrState.hostLaneSignals() = std::vector<HostLaneSignals>(numHostLanes());
    if (!getSignalsPerHostLane(*tcvrState.hostLaneSignals())) {
      tcvrState.hostLaneSignals()->clear();
    }
    if (auto hostLaneSignals = tcvrState.hostLaneSignals()) {
      for (auto& hostLaneSignal : *hostLaneSignals) {
        if (hostLaneSignal.cmisLaneState() &&
            !apache::thrift::util::tryGetEnumName(
                *hostLaneSignal.cmisLaneState())) {
          tcvrState.errorStates()->insert(
              TransceiverErrorState::INVALID_DATA_PATH_LANE_STATE);
          QSFP_LOG(ERR, this) << "Invalid data path lane state";
          break;
        }
      }
    }

    if (auto transceiverStats = getTransceiverStats()) {
      tcvrStats.stats() = *transceiverStats;
    }

    tcvrState.signalFlag() = getSignalFlagInfo();
    cacheSignalFlags(*tcvrState.signalFlag());
    logLatchedLinkFaultsLocked(
        *tcvrState.signalFlag(), *tcvrState.mediaLaneSignals());

    if (auto extSpecCompliance = getExtendedSpecificationComplianceCode()) {
      tcvrState.extendedSpecificationComplianceCode() = *extSpecCompliance;
    }
    tcvrState.transceiverManagementInterface() = managementInterface();

    tcvrState.identifier() = getIdentifier();
    if (!apache::thrift::util::tryGetEnumName(*tcvrState.identifier())) {
      tcvrState.errorStates()->insert(
          TransceiverErrorState::INVALID_IDENTIFIER);
      QSFP_LOG(ERR, this) << "Invalid module identifier";
    }

    if (hasInvalidBankSelect()) {
      tcvrState.errorStates()->insert(
          TransceiverErrorState::INVALID_BANK_SELECT);
    }
    auto currentStatus = getModuleStatus();
    // Use the input `moduleStatus` as the reference to update the
    // `cmisStateChanged` for currentStatus, which will be used in the
    // TransceiverInfo
    updateCmisStateChanged(currentStatus, moduleStatus);
    tcvrState.status() = currentStatus;
    cacheStatusFlags(currentStatus);

    // Tunable optics parameter
    auto tunableLaserstatus = getTunableLaserStatus();
    if (tunableLaserstatus) {
      tcvrState.tunableLaserStatus() = tunableLaserstatus.value();
    }

    // Drive the non-blocking VDM ForOds freeze/read handshake. On the
    // StatsPublisher trigger this writes FreezeRequest (no wait); one refresh
    // later it reads the frozen snapshot and returns true. Never blocks the
    // refresh thread.
    const bool vdmFrozenReadThisCycle = driveVdmCaptureLocked();

    auto vdmStats = getVdmDiagsStatsInfo();
    auto vdmPerfMonStats = getVdmPerfMonitorStats();

    if (vdmStats.has_value() && vdmPerfMonStats.has_value()) {
      tcvrStats.vdmDiagsStats() = *vdmStats;
      tcvrStats.vdmPerfMonitorStats() = *vdmPerfMonStats;

      // Only refresh the ForOds snapshot on the cycle we read frozen data;
      // otherwise carry forward the previous values.
      if (vdmFrozenReadThisCycle) {
        tcvrStats.vdmDiagsStatsForOds() = *vdmStats;
        tcvrStats.vdmPerfMonitorStatsForOds() =
            getVdmPerfMonitorStatsForOds(*vdmPerfMonStats);
      } else if (info_.rlock()->has_value()) {
        // If the VDM is not updated in this cycle then retain older values.
        // Skip while the info cache is not yet populated (e.g. the first
        // refresh, or right after an info_ reset) -- there is nothing to carry
        // forward yet and getTransceiverInfo() would otherwise throw.
        auto cachedTcvrInfo = getTransceiverInfo();
        if (cachedTcvrInfo.tcvrStats()->vdmDiagsStatsForOds()) {
          tcvrStats.vdmDiagsStatsForOds() =
              cachedTcvrInfo.tcvrStats()->vdmDiagsStatsForOds().value();
        }
        if (cachedTcvrInfo.tcvrStats()->vdmPerfMonitorStatsForOds()) {
          tcvrStats.vdmPerfMonitorStatsForOds() =
              cachedTcvrInfo.tcvrStats()->vdmPerfMonitorStatsForOds().value();
        }
      }
    }

    tcvrState.timeCollected() = lastQsfpDataUpdateTime_;
    tcvrStats.timeCollected() = lastQsfpDataUpdateTime_;

    const auto thermalMargins = getThermalMargins();
    if (thermalMargins.dspTempMargin) {
      tcvrStats.dspTempMargin() = *thermalMargins.dspTempMargin;
    }
    if (thermalMargins.laserTempMargin) {
      tcvrStats.laserTempMargin() = *thermalMargins.laserTempMargin;
    }

    tcvrStats.remediationCounter() = numRemediation_;
    tcvrState.eepromCsumValid() = verifyEepromChecksums();
    tcvrState.moduleMediaInterface() = getModuleMediaInterface();

    for (auto it : getPortNameToHostLanes()) {
      tcvrState.portNameToHostLanes()[it.first] =
          std::vector<int>(it.second.begin(), it.second.end());
    }
    tcvrStats.portNameToHostLanes() = *tcvrState.portNameToHostLanes();

    for (auto it : getPortNameToMediaLanes()) {
      tcvrState.portNameToMediaLanes()[it.first] =
          std::vector<int>(it.second.begin(), it.second.end());
    }
    tcvrStats.portNameToMediaLanes() = *tcvrState.portNameToMediaLanes();

    for (const auto& [portName, lanes] : *tcvrStats.portNameToHostLanes()) {
      int lastDataPathResetTime = 0;
      for (const auto& lane : lanes) {
        // Get the most recent reset time (max) for any of the port lanes.
        lastDataPathResetTime = std::max<long>(
            lastDataPathResetTime, getLastDatapathResetTime(lane));
      }
      tcvrStats.lastDatapathResetTime()[portName] = lastDataPathResetTime;
    }

    auto diagCapability = getDiagsCapability();
    if (diagCapability.has_value()) {
      tcvrState.diagCapability() = diagCapability.value();
    }
    // lpoModule() Deprecated. Use ModuleTechnolgy instead.
    tcvrState.lpoModule() = isLpoModule();

    if (isLpoModule()) {
      tcvrState.moduleTechnology() = ModuleTechnology::LPO;
    } else if (isAecModule()) {
      tcvrState.moduleTechnology() = ModuleTechnology::AEC;
    } else if (isCBandTunable()) {
      tcvrState.moduleTechnology() = ModuleTechnology::TUNABLE_C_BAND;
    } else if (isLBandTunable()) {
      tcvrState.moduleTechnology() = ModuleTechnology::TUNABLE_L_BAND;
    } else {
      tcvrState.moduleTechnology() = ModuleTechnology::GREY;
    }

    tcvrState.pagingSupport() =
        flatMem_ ? PagingSupport::FLAT_MEM : PagingSupport::PAGED;
  }

  tcvrStats.lastFwUpgradeStartTime() = lastFwUpgradeStartTime_;
  tcvrStats.lastFwUpgradeEndTime() = lastFwUpgradeEndTime_;

  // If it's over the estimated time that a tcvr takes to be ready for link up
  // after fw upgrade, then set the fwUpgradeInProgress status to false
  if ((std::time(nullptr) - lastFwUpgradeEndTime_) >
      FLAGS_time_for_tcvr_ready_after_fw_upgrade_s) {
    tcvrState.fwUpgradeInProgress() = false;
  } else {
    tcvrState.fwUpgradeInProgress() = true;
  }
  auto tcvrName = getTcvrName();
  tcvrState.tcvrName() = tcvrName;
  tcvrStats.tcvrName() = tcvrName;
  tcvrState.interfaces() = getInterfaces();
  tcvrStats.interfaces() = getInterfaces();

  *info_.wlock() = info;
  lastTcvrInfoUpdateTime_ = std::time(nullptr);
}

bool QsfpModule::customizationSupported() const {
  // Customization is allowed on present Optical modules only. We should skip
  // other types. Overridden in derived classes if needed.
  auto tech = getQsfpTransmitterTechnology();
  return present_ && tech == TransmitterTechnology::OPTICAL;
}

bool QsfpModule::shouldRefresh(time_t cooldown) const {
  return std::time(nullptr) - lastTcvrInfoUpdateTime_ >= cooldown;
}

void QsfpModule::ensureOutOfReset() const {
  qsfpImpl_->ensureOutOfReset();
  QSFP_LOG(DBG3, this) << "Cleared the reset register of QSFP.";
}

void QsfpModule::cacheSignalFlags(const SignalFlags& signalflag) {
  signalFlagCache_.txLos() = *signalflag.txLos() | *signalFlagCache_.txLos();
  signalFlagCache_.rxLos() = *signalflag.rxLos() | *signalFlagCache_.rxLos();
  signalFlagCache_.txLol() = *signalflag.txLol() | *signalFlagCache_.txLol();
  signalFlagCache_.rxLol() = *signalflag.rxLol() | *signalFlagCache_.rxLol();
}

void QsfpModule::logLatchedLinkFaultsLocked(
    const SignalFlags& signalFlags,
    const std::vector<MediaLaneSignals>& mediaLaneSignals) {
  const int txLos = *signalFlags.txLos();
  const int rxLos = *signalFlags.rxLos();
  const int txLol = *signalFlags.txLol();
  const int rxLol = *signalFlags.rxLol();

  // txFault is only reported per media lane; collapse it into a per-lane
  // bitmask so it reads uniformly with the aggregated flags above.
  int txFault = 0;
  for (const auto& laneSignal : mediaLaneSignals) {
    if (laneSignal.txFault().value_or(false)) {
      txFault |= (1 << *laneSignal.lane());
    }
  }

  // Edge-trigger: a link that stays down keeps these flags latched every
  // refresh, so only act when the flag set changes from what we last observed.
  if (signalFlags == lastLoggedSignalFlags_ && txFault == lastLoggedTxFault_) {
    return;
  }
  lastLoggedSignalFlags_ = signalFlags;
  lastLoggedTxFault_ = txFault;

  const auto portNames = folly::join(", ", getInterfaces());

  // Reaching here means the flag set changed. All-clear is therefore a recovery
  // from a previously latched fault; log it at INFO so downtime is visible in
  // the event log without being treated as an error.
  if (!txLos && !rxLos && !txLol && !rxLol && !txFault) {
    QSFP_LOG(INFO, this) << LinkAlert()
                         << fmt::format(
                                "latched link fault flags "
                                "(txLos/rxLos/txLol/rxLol/txFault) cleared on "
                                "ports [{}]",
                                portNames);
    return;
  }

  QSFP_LOG(ERR, this) << LinkAlert()
                      << fmt::format(
                             "reported latched link fault flags on ports [{}]: "
                             "txLos=0x{:x} rxLos=0x{:x} txLol=0x{:x} "
                             "rxLol=0x{:x} txFault=0x{:x}",
                             portNames,
                             txLos,
                             rxLos,
                             txLol,
                             rxLol,
                             txFault);
}

void QsfpModule::cacheStatusFlags(const ModuleStatus& status) {
  if (moduleStatusCache_.cmisStateChanged() && status.cmisStateChanged()) {
    moduleStatusCache_.cmisStateChanged() =
        *status.cmisStateChanged() || *moduleStatusCache_.cmisStateChanged();
  } else {
    moduleStatusCache_.cmisStateChanged().copy_from(status.cmisStateChanged());
  }
}

void QsfpModule::cacheMediaLaneSignals(
    const std::vector<MediaLaneSignals>& mediaSignals) {
  for (const auto& signal : mediaSignals) {
    if (mediaSignalsCache_.find(*signal.lane()) == mediaSignalsCache_.end()) {
      // Initialize all lanes to false if an entry in the cache doesn't exist
      // yet
      mediaSignalsCache_[*signal.lane()].lane() = *signal.lane();
      mediaSignalsCache_[*signal.lane()].txFault() = false;
    }
    if (auto txFault = signal.txFault()) {
      if (*txFault) {
        mediaSignalsCache_[*signal.lane()].txFault() = true;
      }
    }
  }
}

bool QsfpModule::setPortPrbs(
    const std::string& portName,
    phy::Side side,
    const prbs::InterfacePrbsState& prbs) {
  bool status = false;
  auto setPrbsLambda = [&status, portName, side, prbs, this]() {
    lock_guard<std::mutex> g(qsfpModuleMutex_);
    status = setPortPrbsLocked(portName, side, prbs);
  };
  auto i2cEvb = qsfpImpl_->getI2cEventBase();
  if (!i2cEvb) {
    // Certain platforms cannot execute multiple I2C transactions in parallel
    // and therefore don't have an I2C evb thread
    setPrbsLambda();
  } else {
    via(i2cEvb)
        .thenValue([setPrbsLambda](auto&&) mutable { setPrbsLambda(); })
        .get();
  }
  return status;
}

bool QsfpModule::setTransceiverTx(
    const std::string& portName,
    phy::Side side,
    std::optional<uint8_t> userChannelMask,
    bool enable) {
  // Lambda to call Locked function
  auto setTcvrFn = [&]() -> bool {
    lock_guard<std::mutex> g(qsfpModuleMutex_);
    return setTransceiverTxLocked(portName, side, userChannelMask, enable);
  };

  auto i2cEvb = qsfpImpl_->getI2cEventBase();
  if (!i2cEvb) {
    // In non-multithreaded environment, run the function in current thread
    return setTcvrFn();
  } else {
    // In mulththreaded environment, run the function in event base thread
    return via(i2cEvb)
        .thenValue([setTcvrFn](auto&&) mutable { return setTcvrFn(); })
        .get();
  }
}

void QsfpModule::setTransceiverLoopback(
    const std::string& portName,
    phy::Side side,
    bool setLoopback,
    phy::LoopbackMode mode) {
  // Lambda to call Locked function
  auto setTcvrFn = [&]() {
    lock_guard<std::mutex> g(qsfpModuleMutex_);
    setTransceiverLoopbackLocked(portName, side, setLoopback, mode);
  };

  auto i2cEvb = qsfpImpl_->getI2cEventBase();
  if (!i2cEvb) {
    // In non-multithreaded environment, run the function in current thread
    setTcvrFn();
  } else {
    // In mulththreaded environment, run the function in event base thread
    via(i2cEvb).thenValue([setTcvrFn](auto&&) mutable { setTcvrFn(); }).get();
  }
}

std::map<std::string, CdbDatapathSymErrHistogram>
QsfpModule::getSymbolErrorHistogram() {
  std::map<std::string, CdbDatapathSymErrHistogram> symErr;

  // Lambda to call Qsfp function
  auto getSymErrFn = [&]() {
    lock_guard<std::mutex> g(qsfpModuleMutex_);
    symErr = getCdbSymbolErrorHistogramLocked();
  };

  auto i2cEvb = qsfpImpl_->getI2cEventBase();
  if (!i2cEvb) {
    // In non-multithreaded environment, run the function in current thread
    getSymErrFn();
  } else {
    // In mulththreaded environment, run the function in event base thread
    via(i2cEvb)
        .thenValue([getSymErrFn](auto&&) mutable { getSymErrFn(); })
        .get();
  }
  return symErr;
}

/*
 * isTransceiverFeatureSupported
 *
 * This function returns the supported status of transceiver features
 */
bool QsfpModule::isTransceiverFeatureSupported(
    TransceiverFeature feature) const {
  auto lockedDiagsCapability = diagsCapability_.rlock();
  if (auto diagsCapability = *lockedDiagsCapability) {
    switch (feature) {
      case TransceiverFeature::NONE:
        return false;
      case TransceiverFeature::VDM:
        return diagsCapability->vdm().value();
      case TransceiverFeature::CDB:
        return diagsCapability->cdb().value();
      case TransceiverFeature::PRBS:
      case TransceiverFeature::LOOPBACK:
      case TransceiverFeature::TX_DISABLE:
        throw FbossError(
            "Line/System side info is needed to check feature support in Transceiver");
      default:
        return false;
    }
  }
  return false;
}

/*
 * isTransceiverFeatureSupported
 *
 * This function returns the supported status of transceiver features for line
 * or system side
 */
bool QsfpModule::isTransceiverFeatureSupported(
    TransceiverFeature feature,
    phy::Side side) const {
  auto lockedDiagsCapability = diagsCapability_.rlock();
  if (auto diagsCapability = *lockedDiagsCapability) {
    switch (feature) {
      case TransceiverFeature::NONE:
        return false;
      case TransceiverFeature::VDM:
      case TransceiverFeature::CDB:
        throw FbossError(
            "Line/System side info is not needed to check Feature support in Transceiver");
      case TransceiverFeature::PRBS:
        return (side == phy::Side::LINE)
            ? diagsCapability->prbsLine().value()
            : diagsCapability->prbsSystem().value();
      case TransceiverFeature::LOOPBACK:
        return (side == phy::Side::LINE)
            ? diagsCapability->loopbackLine().value()
            : diagsCapability->loopbackSystem().value();
      case TransceiverFeature::TX_DISABLE:
        return (side == phy::Side::LINE)
            ? diagsCapability->txOutputControl().value()
            : diagsCapability->rxOutputControl().value();
      default:
        return false;
    }
  }
  return false;
}

bool QsfpModule::isVdmSupported(uint8_t maxGroupRequested) const {
  if (!isTransceiverFeatureSupported(TransceiverFeature::VDM)) {
    return false;
  }

  // Intel SPTSHP3CLCKS / SPTSHP3CLCK2 has a bug and needs FW Update.
  // Tracked by T209278325
  auto cachedTcvrInfo = getTransceiverInfo();
  auto vendor = cachedTcvrInfo.tcvrState()->vendor();
  if (vendor.has_value()) {
    re2::RE2 portNameRe("Intel");
    if (re2::RE2::PartialMatch(vendor->name().value(), portNameRe) &&
        ((vendor->partNumber().value() == "SPTSHP3CLCKS") ||
         (vendor->partNumber().value() == "SPTSHP3CLCK2"))) {
      QSFP_LOG(WARN, this)
          << "Found Intel SPTSHP3CLCKS / SPTSHP3CLCK2. VDM is not supported";
      return false;
    }
  }
  if (!maxGroupRequested) {
    return true;
  }
  return (vdmSupportedGroupsMax_ >= maxGroupRequested);
}

prbs::InterfacePrbsState QsfpModule::getPortPrbsState(
    std::optional<const std::string> portName,
    phy::Side side) {
  prbs::InterfacePrbsState state;
  auto getPrbsStateLambda = [&state, portName, side, this]() {
    lock_guard<std::mutex> g(qsfpModuleMutex_);
    state = getPortPrbsStateLocked(portName, side);
  };
  auto i2cEvb = qsfpImpl_->getI2cEventBase();
  if (!i2cEvb) {
    // Certain platforms cannot execute multiple I2C transactions in parallel
    // and therefore don't have an I2C evb thread
    getPrbsStateLambda();
  } else {
    via(i2cEvb)
        .thenValue(
            [getPrbsStateLambda](auto&&) mutable { getPrbsStateLambda(); })
        .get();
  }
  return state;
}

void QsfpModule::periodicUpdateQsfpData() {
  bool updatedAllPages = false;
  refreshCycleCount_++;
  if (refreshCycleCount_ >= FLAGS_refresh_all_pages_cycles) {
    updatedAllPages = true;
    // reset cycle count
    refreshCycleCount_ = 0;
    QSFP_LOG(DBG2, this) << "Refreshing all pages";
  }
  updateQsfpData(updatedAllPages);
}

void QsfpModule::refresh() {
  lock_guard<std::mutex> g(qsfpModuleMutex_);
  refreshLocked();
}

// call refresh() on the module and return whether or not it was successful
folly::Future<bool> QsfpModule::futureRefresh() {
  // Always use i2cEvb to program transceivers if there's an i2cEvb
  auto i2cEvb = qsfpImpl_->getI2cEventBase();
  if (!i2cEvb) {
    try {
      refresh();
      return folly::makeFuture(true);
    } catch (const std::exception& ex) {
      QSFP_LOG(DBG2, this) << "Error calling refresh(): " << ex.what();
      return folly::makeFuture(false);
    }
  }

  return via(i2cEvb).thenValue([this](auto&&) mutable {
    try {
      this->refresh();
      return true;
    } catch (const std::exception& ex) {
      QSFP_LOG(DBG2, this) << "Error calling refresh(): " << ex.what();
      return false;
    }
  });
}

void QsfpModule::refreshLocked() {
  auto detectionStatus = detectPresenceLocked();

  auto willRefresh = !dirty_ && shouldRefresh(FLAGS_qsfp_data_refresh_interval);
  QSFP_LOG_IF(DBG3, dirty_, this)
      << "dirty_ = " << dirty_ << ", willRefresh = " << willRefresh
      << ", present = " << detectionStatus.present
      << ", statusChanged = " << detectionStatus.statusChanged;
  if (!dirty_ && !willRefresh) {
    return;
  }

  if (detectionStatus.statusChanged && detectionStatus.present) {
    // A new transceiver has been detected
    qsfpImpl_->updateTransceiverState(
        TransceiverStateMachineEvent::TCVR_EV_EVENT_DETECT_TRANSCEIVER);
  } else if (detectionStatus.statusChanged && !detectionStatus.present) {
    // The transceiver has been removed
    qsfpImpl_->updateTransceiverState(
        TransceiverStateMachineEvent::TCVR_EV_REMOVE_TRANSCEIVER);
  }

  ModuleStatus moduleStatus;
  // Each of the reset functions need to check whether the transceiver is
  // present or not, and then handle its own logic differently. Even though
  // the transceiver might be absent here, we'll still go through all of the
  // rest functions
  if (dirty_) {
    // make sure data is up to date before trying to customize.
    ensureOutOfReset();
    updateQsfpData(true);
    updateCmisStateChanged(moduleStatus);
    if (present_) {
      // Data has been read for the new optics
      qsfpImpl_->updateTransceiverState(
          TransceiverStateMachineEvent::TCVR_EV_READ_EEPROM);
      // Issue an allPages=false update to pick up the new qsfp data after we
      // trigger READ_EEPROM event. Some Transceiver might pick up all the diag
      // capabilities and we can use this to make sure the current QsfpData has
      // updated pages without waiting for the next refresh
      // TODO: updateQsfpData here could be unnecessary if the read_eeprom event
      // above is a no-op. Need to figure out a way to avoid this call in that
      // case
      updateQsfpData(false);
    }
  }

  // If it's just regular refresh
  if (willRefresh) {
    periodicUpdateQsfpData();
    updateCmisStateChanged(moduleStatus);
  }

  updateCachedTransceiverInfoLocked(moduleStatus);
  // Only update prbs stats if the transceiver is present.
  // Should have this check inside of updatePrbsStats().
  // However updatePrbsStats() is a public function and not lock safe as
  // refresh() to get the qsfpModuleMutex_ first.
  // TODO: Need to rethink whether all the following prbs stats functions should
  // get the lock of qsfpModuleMutex_ first.
  if (detectionStatus.present) {
    updatePrbsStats();
  }
}

void QsfpModule::clearTransceiverPrbsStats(
    const std::string& portName,
    phy::Side side) {
  QSFP_LOG(INFO, this) << " Clearing prbs stats for port " << portName << " "
                       << ((side == phy::Side::LINE) ? "Line" : "Host");
  auto systemPrbs = systemPrbsStats_.wlock();
  auto linePrbs = linePrbsStats_.wlock();
  auto tcvrLanes = getTcvrLanesForPort(portName, side);

  auto clearLaneStats =
      [this, &tcvrLanes](std::vector<phy::PrbsLaneStats>& laneStats) {
        for (auto& laneStat : laneStats) {
          auto laneId = *laneStat.laneId();
          if (tcvrLanes.find(laneId) == tcvrLanes.end()) {
            continue;
          }
          laneStat = phy::PrbsLaneStats();
          laneStat.laneId() = laneId;

          QSFP_LOG(INFO, this)
              << " Lane " << *laneStat.laneId() << " ber and maxBer cleared";
        }
      };
  if (side == phy::Side::SYSTEM) {
    clearLaneStats(*systemPrbs->laneStats());
  } else {
    clearLaneStats(*linePrbs->laneStats());
  }
}

void QsfpModule::updatePrbsStats() {
  auto systemPrbs = systemPrbsStats_.wlock();
  auto linePrbs = linePrbsStats_.wlock();

  auto updatePrbsStatEntry =
      [this](const phy::PrbsStats& oldStat, phy::PrbsStats& newStat) {
        for (const auto& oldLane : *oldStat.laneStats()) {
          for (auto& newLane : *newStat.laneStats()) {
            if (*newLane.laneId() != *oldLane.laneId()) {
              continue;
            }
            // Update numLossOfLock
            if (!(*newLane.locked()) && *oldLane.locked()) {
              newLane.numLossOfLock() = *oldLane.numLossOfLock() + 1;
            } else {
              newLane.numLossOfLock() = *oldLane.numLossOfLock();
            }
            // Update maxBer only if there is a lock
            if (*newLane.locked() && *newLane.ber() > *oldLane.maxBer()) {
              newLane.maxBer() = *newLane.ber();
            } else {
              newLane.maxBer() = *oldLane.maxBer();
            }
            // Update maxSnr only if there is a lock
            if (*newLane.locked()) {
              if (newLane.snr().has_value() &&
                  (!oldLane.maxSnr().has_value() ||
                   *newLane.snr() > *oldLane.maxSnr())) {
                newLane.maxSnr() = *newLane.snr();
              }
            } else if (oldLane.maxSnr().has_value()) {
              newLane.maxSnr() = *oldLane.maxSnr();
            }

            QSFP_LOG(DBG5, this)
                << " Lane " << *newLane.laneId()
                << " Lock=" << (*newLane.locked() ? "Y" : "N")
                << " ber=" << *newLane.ber() << " maxBer=" << *newLane.maxBer()
                << " snr=" << (newLane.snr().has_value() ? *newLane.snr() : 0)
                << " maxSnr="
                << (newLane.maxSnr().has_value() ? *newLane.maxSnr() : 0);

            // Update timeSinceLastLocked
            // If previously there was no lock and now there is, update
            // timeSinceLastLocked to now
            if (!(*oldLane.locked()) && *newLane.locked()) {
              newLane.timeSinceLastLocked() = 0;
            } else {
              newLane.timeSinceLastLocked() = *oldLane.timeSinceLastLocked() +
                  (*newStat.timeCollected()) - (*oldStat.timeCollected());
            }
            if (!(*oldStat.timeCollected())) {
              // Initially timeCollected will be 0, so initialize
              // timeSinceLastClear to be 0 also
              newLane.timeSinceLastClear() = 0;
            } else {
              newLane.timeSinceLastClear() = *oldLane.timeSinceLastClear() +
                  (*newStat.timeCollected()) - (*oldStat.timeCollected());
            }
          }
        }
      };

  phy::PrbsStats stats;
  if (isPrbsSupported(phy::Side::SYSTEM)) {
    auto sysPrbsState = getPortPrbsStateLocked(std::nullopt, phy::Side::SYSTEM);
    stats = getPortPrbsStatsSideLocked(
        phy::Side::SYSTEM,
        sysPrbsState.checkerEnabled().has_value() &&
            sysPrbsState.checkerEnabled().value(),
        *systemPrbs);
    updatePrbsStatEntry(*systemPrbs, stats);
    *systemPrbs = stats;
  }

  if (isPrbsSupported(phy::Side::LINE)) {
    auto linePrbsState = getPortPrbsStateLocked(std::nullopt, phy::Side::LINE);
    stats = getPortPrbsStatsSideLocked(
        phy::Side::LINE,
        linePrbsState.checkerEnabled().has_value() &&
            linePrbsState.checkerEnabled().value(),
        *linePrbs);
    updatePrbsStatEntry(*linePrbs, stats);
    *linePrbs = stats;
  }
}

phy::PrbsStats QsfpModule::getPortPrbsStats(
    const std::string& portName,
    phy::Side side) {
  phy::PrbsStats portPrbs;
  auto tcvrLanes = getTcvrLanesForPort(portName, side);

  auto sidePrbs = (side == phy::Side::LINE) ? linePrbsStats_.rlock()
                                            : systemPrbsStats_.rlock();
  portPrbs.portId() = sidePrbs->portId().value();
  portPrbs.timeCollected() = sidePrbs->timeCollected().value();
  portPrbs.component() = sidePrbs->component().value();
  for (auto& laneStat : sidePrbs->laneStats().value()) {
    if (tcvrLanes.find(*laneStat.laneId()) != tcvrLanes.end()) {
      portPrbs.laneStats()->push_back(laneStat);
    }
  }

  return portPrbs;
}

bool QsfpModule::shouldRemediate(time_t pauseRemidiation) {
  // Always use i2cEvb to program transceivers if there's an i2cEvb
  auto shouldRemediateFunc = [&, this]() {
    lock_guard<std::mutex> g(qsfpModuleMutex_);
    return shouldRemediateLocked(pauseRemidiation);
  };
  auto i2cEvb = qsfpImpl_->getI2cEventBase();
  if (!i2cEvb) {
    // Certain platforms cannot execute multiple I2C transactions in parallel
    // and therefore don't have an I2C evb thread
    return shouldRemediateFunc();
  } else {
    bool shouldRemediateResult = false;
    via(i2cEvb)
        .thenValue(
            [&shouldRemediateResult, shouldRemediateFunc](auto&&) mutable {
              shouldRemediateResult = shouldRemediateFunc();
            })
        .get();
    return shouldRemediateResult;
  }
}

bool QsfpModule::shouldRemediateLocked(time_t pauseRemidiation) {
  if (!FLAGS_remediation_enabled || !supportRemediate()) {
    return false;
  }

  auto sysPrbsState = getPortPrbsStateLocked(std::nullopt, phy::Side::SYSTEM);
  auto linePrbsState = getPortPrbsStateLocked(std::nullopt, phy::Side::LINE);

  auto linePrbsEnabled =
      ((linePrbsState.generatorEnabled().has_value() &&
        linePrbsState.generatorEnabled().value()) ||
       (linePrbsState.checkerEnabled().has_value() &&
        linePrbsState.checkerEnabled().value()));
  auto sysPrbsEnabled =
      ((sysPrbsState.generatorEnabled().has_value() &&
        sysPrbsState.generatorEnabled().value()) ||
       (sysPrbsState.checkerEnabled().has_value() &&
        sysPrbsState.checkerEnabled().value()));

  if (linePrbsEnabled || sysPrbsEnabled) {
    QSFP_LOG(INFO, this)
        << "Skipping remediation because PRBS is enabled. System: "
        << sysPrbsEnabled << ", Line: " << linePrbsEnabled;
    return false;
  }

  auto now = std::time(nullptr);
  bool remediationEnabled =
      (now > pauseRemidiation) && (now > getModulePauseRemediationUntil());
  // Rather than immediately attempting to remediate a module,
  // we would like to introduce a bit delay to de-couple the consequences
  // of a remediation with the root cause that brought down the link.
  // This is an effort to help with debugging.
  // And for the first remediation, we don't want to wait for
  // `FLAGS_remediate_interval`, instead we just need to wait for
  // `FLAGS_initial_remediate_interval`. (D26014510)
  bool remediationCooled = false;
  auto lastDownTime = lastDownTime_.load();
  if (lastDownTime > lastRemediateTime_) {
    // New lastDownTime means the port just recently went down
    remediationCooled = (now - lastDownTime) > FLAGS_initial_remediate_interval;
  } else {
    remediationCooled = (now - lastRemediateTime_) > FLAGS_remediate_interval;
  }
  return remediationEnabled && remediationCooled;
}

void QsfpModule::customizeTransceiver(TransceiverPortState& portState) {
  lock_guard<std::mutex> g(qsfpModuleMutex_);
  if (present_) {
    customizeTransceiverLocked(portState);
  }
}

void QsfpModule::customizeTransceiverLocked(
    const TransceiverPortState& portState) {
  auto& portName = portState.portName;
  auto speed = portState.speed;
  auto startHostLane = portState.startHostLane;
  QSFP_LOG(INFO, this) << fmt::format(
      "customizeTransceiverLocked: PortName {}, Speed {}, StartHostLane {}",
      portName,
      apache::thrift::util::enumNameSafe(speed),
      startHostLane);
  /*
   * This must be called with a lock held on qsfpModuleMutex_
   */
  if (customizationSupported()) {
    TransceiverSettings settings = getTransceiverSettingsInfo();

    // We want this on regardless of speed
    setPowerOverrideIfSupportedLocked(*settings.powerControl());

    if (speed != cfg::PortSpeed::DEFAULT) {
      setCdrIfSupported(speed, *settings.cdrTx(), *settings.cdrRx());
      setRateSelectIfSupported(
          speed, *settings.rateSelect(), *settings.rateSelectSetting());
    }
  } else {
    QSFP_LOG(DBG1, this) << "Customization not supported";
  }
}

std::optional<TransceiverStats> QsfpModule::getTransceiverStats() {
  auto transceiverStats = qsfpImpl_->getTransceiverStats();
  if (!transceiverStats.has_value()) {
    return {};
  }
  return transceiverStats.value();
}

folly::Future<std::pair<int32_t, std::unique_ptr<IOBuf>>>
QsfpModule::futureReadTransceiver(TransceiverIOParameters param) {
  // Always use i2cEvb to program transceivers if there's an i2cEvb
  auto i2cEvb = qsfpImpl_->getI2cEventBase();
  auto id = getID();
  if (!i2cEvb) {
    // Certain platforms cannot execute multiple I2C transactions in parallel
    // and therefore don't have an I2C evb thread
    return std::make_pair(id, readTransceiver(param));
  }
  // As with all the other i2c transactions, run in the i2c event base thread
  return via(i2cEvb).thenValue([this, param, id](auto&&) mutable {
    return std::make_pair(id, this->readTransceiver(param));
  });
}

std::unique_ptr<IOBuf> QsfpModule::readTransceiver(
    TransceiverIOParameters param) {
  lock_guard<std::mutex> g(qsfpModuleMutex_);
  return readTransceiverLocked(param);
}

std::unique_ptr<IOBuf> QsfpModule::readTransceiverLocked(
    TransceiverIOParameters param) {
  /*
   * This must be called with a lock held on qsfpModuleMutex_
   */
  auto length = param.length().has_value() ? *(param.length()) : 1;
  auto iobuf = folly::IOBuf::createCombined(length);
  if (!present_) {
    return iobuf;
  }
  try {
    auto offset = *(param.offset());
    if (param.page().has_value()) {
      uint8_t page = *(param.page());
      // When the page is specified, first update byte 127 with the speciied
      // pageId
      qsfpImpl_->writeTransceiver(
          {TransceiverAccessParameter::ADDR_QSFP, 127, sizeof(page)},
          &page,
          POST_I2C_WRITE_DELAY_US,
          CAST_TO_INT(CmisField::PAGE_CHANGE)); // common enum to all tcvr types
    }
    qsfpImpl_->readTransceiver(
        {TransceiverAccessParameter::ADDR_QSFP, offset, length},
        iobuf->writableData(),
        CAST_TO_INT(CmisField::RAW)); // common enum to all tcvr types
    // Mark the valid data in the buffer
    iobuf->append(length);
  } catch (const std::exception& ex) {
    QSFP_LOG(ERR, this) << "Error reading data: " << ex.what();
    throw;
  }
  return iobuf;
}

folly::Future<std::pair<int32_t, bool>> QsfpModule::futureWriteTransceiver(
    TransceiverIOParameters param,
    const std::vector<uint8_t>& data) {
  // Always use i2cEvb to program transceivers if there's an i2cEvb
  auto i2cEvb = qsfpImpl_->getI2cEventBase();
  auto id = getID();
  if (!i2cEvb) {
    // Certain platforms cannot execute multiple I2C transactions in parallel
    // and therefore don't have an I2C evb thread
    return std::make_pair(id, writeTransceiver(param, data.data()));
  }
  // As with all the other i2c transactions, run in the i2c event base thread
  return via(i2cEvb).thenValue([this, param, id, data](auto&&) mutable {
    return std::make_pair(id, this->writeTransceiver(param, data.data()));
  });
}

bool QsfpModule::writeTransceiver(
    TransceiverIOParameters param,
    const uint8_t* data) {
  lock_guard<std::mutex> g(qsfpModuleMutex_);
  return writeTransceiverLocked(param, data);
}

bool QsfpModule::writeTransceiverLocked(
    TransceiverIOParameters param,
    const uint8_t* data) {
  /*
   * This must be called with a lock held on qsfpModuleMutex_
   */
  if (!present_) {
    return false;
  }
  try {
    auto offset = *(param.offset());
    if (param.page().has_value()) {
      uint8_t page = *(param.page());
      // When the page is specified, first update byte 127 with the speciied
      // pageId
      qsfpImpl_->writeTransceiver(
          {TransceiverAccessParameter::ADDR_QSFP, 127, sizeof(page)},
          &page,
          POST_I2C_WRITE_DELAY_US,
          CAST_TO_INT(CmisField::PAGE_CHANGE)); // common enum to all tcvr types
    }
    int numBytes = param.length().has_value() ? *(param.length()) : 1;
    qsfpImpl_->writeTransceiver(
        {TransceiverAccessParameter::ADDR_QSFP, offset, numBytes},
        data,
        POST_I2C_WRITE_DELAY_US,
        CAST_TO_INT(CmisField::RAW)); // common enum to all tcvr types
  } catch (const std::exception& ex) {
    QSFP_LOG(ERR, this) << "Error writing data: " << ex.what();
    throw;
  }
  return true;
}

SignalFlags QsfpModule::readAndClearCachedSignalFlags() {
  lock_guard<std::mutex> g(qsfpModuleMutex_);
  SignalFlags signalFlag;
  // Store the cached data before clearing it.
  signalFlag.txLos() = *signalFlagCache_.txLos();
  signalFlag.rxLos() = *signalFlagCache_.rxLos();
  signalFlag.txLol() = *signalFlagCache_.txLol();
  signalFlag.rxLol() = *signalFlagCache_.rxLol();

  // Clear the cached data after read.
  signalFlagCache_.txLos() = 0;
  signalFlagCache_.rxLos() = 0;
  signalFlagCache_.txLol() = 0;
  signalFlagCache_.rxLol() = 0;
  return signalFlag;
}

std::map<int, MediaLaneSignals>
QsfpModule::readAndClearCachedMediaLaneSignals() {
  lock_guard<std::mutex> g(qsfpModuleMutex_);
  // Store the cached data before clearing it.
  std::map<int, MediaLaneSignals> mediaSignals(mediaSignalsCache_);

  // Clear the cached data after read.
  for (auto& signal : mediaSignalsCache_) {
    signal.second.txFault() = false;
  }
  return mediaSignals;
}

ModuleStatus QsfpModule::readAndClearCachedModuleStatus() {
  lock_guard<std::mutex> g(qsfpModuleMutex_);
  // Store the cached data before clearing it.
  ModuleStatus moduleStatus(moduleStatusCache_);

  // Clear the cached data after read.
  moduleStatusCache_ = ModuleStatus();

  return moduleStatus;
}

TransceiverManagementInterface QsfpModule::getTransceiverManagementInterface(
    const uint8_t moduleId,
    const unsigned int oneBasedPort) {
  if (moduleId ==
          static_cast<uint8_t>(TransceiverModuleIdentifier::QSFP_PLUS_CMIS) ||
      moduleId == static_cast<uint8_t>(TransceiverModuleIdentifier::QSFP_DD) ||
      moduleId == static_cast<uint8_t>(TransceiverModuleIdentifier::OSFP) ||
      moduleId == static_cast<uint8_t>(TransceiverModuleIdentifier::CPO)) {
    return TransceiverManagementInterface::CMIS;
  } else if (
      moduleId ==
          static_cast<uint8_t>(TransceiverModuleIdentifier::QSFP_PLUS) ||
      moduleId == static_cast<uint8_t>(TransceiverModuleIdentifier::QSFP) ||
      moduleId ==
          static_cast<uint8_t>(TransceiverModuleIdentifier::MINIPHOTON_OBO) ||
      moduleId == static_cast<uint8_t>(TransceiverModuleIdentifier::QSFP28)) {
    return TransceiverManagementInterface::SFF;
  } else if (
      moduleId == static_cast<uint8_t>(TransceiverModuleIdentifier::SFP_PLUS)) {
    return TransceiverManagementInterface::SFF8472;
  } else if (
      moduleId != static_cast<uint8_t>(TransceiverModuleIdentifier::UNKNOWN)) {
    XLOG(ERR) << fmt::format(
        "QSFP {:d}: Unrecognized non zero module Id = {:d}",
        oneBasedPort,
        moduleId);
    return TransceiverManagementInterface::UNKNOWN;
  }

  XLOG(ERR) << fmt::format(
      "QSFP {:d}: Bad module Id = {:d}", oneBasedPort, moduleId);
  return TransceiverManagementInterface::NONE;
}

void QsfpModule::programTransceiver(
    ProgramTransceiverState& programTcvrState,
    bool needResetDataPath) {
  // Always use i2cEvb to program transceivers if there's an i2cEvb
  auto programTcvrFunc = [this, &programTcvrState, needResetDataPath]() {
    lock_guard<std::mutex> g(qsfpModuleMutex_);
    if (present_) {
      // Don't consider ports for programming if they have a startHostLane >=
      // the number of lanes on the plugged in transceiver.
      auto hostLaneCount = numHostLanes();
      std::erase_if(programTcvrState.ports, [hostLaneCount](const auto& port) {
        // startHostLane is 0-indexed hence the >= comparison
        return port.second.startHostLane >= hostLaneCount;
      });

      if (!cacheIsValid()) {
        throw FbossError(
            "Transceiver: ",
            getNameString(),
            " - Cache is not valid, so cannot program the transceiver");
      }
      datapathInitDoneInProgramMask_.clear();
      // Make sure customize xcvr first so that we can set the application code
      // correctly and then call configureModule() later to program serdes like
      // Rx equalizer setting based on QSFP config
      for (auto portIt : programTcvrState.ports) {
        customizeTransceiverLocked(portIt.second);
        // updateQsfpData so that we can make sure the new application code in
        // cache or the new host settings ges updated before calling
        // configureModule() or programming other datapaths
        updateQsfpData(true);
      }
      // Current configureModule() actually assumes the locked is obtained.
      // See CmisModule::configureModule(). Need to clean it up in the future.
      for (auto portIt : programTcvrState.ports) {
        auto startHostLane = portIt.second.startHostLane;
        configureModule(startHostLane);
      }

      TransceiverSettings settings = getTransceiverSettingsInfo();
      // We found that some module did not enable Rx output squelch by default,
      // which introduced some difficulty to bring link back up when flapped.
      // Here we ensure that Rx output squelch is always enabled.
      // Skip doing this for 200G-FR4 modules configured in 2x50G mode. For this
      // mode, we need all 4 lanes to operate independently.
      // Also skip for tunable optics modules that advertise rxConsActImpl
      // (Page 45h, Byte 129, Bit 1) where squelch is intentionally disabled
      // and Rx Consequent Action (LF insertion) is programmed instead.
      for (auto portIt : programTcvrState.ports) {
        auto speed = portIt.second.speed;
        bool skipSquelchEnforce =
            (getModuleMediaInterface() == MediaInterfaceCode::FR4_200G &&
             speed == cfg::PortSpeed::FIFTYTHREEPOINTONETWOFIVEG) ||
            (isTunableOptics() && isRxConsActImplSupported());
        if (!skipSquelchEnforce) {
          if (auto hostLaneSettings = settings.hostLaneSettings()) {
            ensureRxOutputSquelchEnabled(*hostLaneSettings);
          }
        }
      }

      if (needResetDataPath) {
        XLOG(INFO) << fmt::format(
            "Transceiver {:s}: Resetting data path", getNameString());
        resetDataPath(getNameString());
      }

      // Since we're touching the transceiver, we need to update the cached
      // transceiver info
      updateQsfpData(false);

      // Cache has been updated, populate the host/mediaLane to port name
      // mapping
      // First clear the existing mappings
      hostLaneToPortName_.clear();
      mediaLaneToPortName_.clear();
      portNameToHostLanes_.clear();
      portNameToMediaLanes_.clear();
      portNameToBankId_.clear();
      for (const auto& [portName, portState] : programTcvrState.ports) {
        updateLaneToPortNameMapping(portName, portState.startHostLane);
        if (portState.bankId.has_value()) {
          portNameToBankId_[portName] = *portState.bankId;
        }
      }
      updateCachedTransceiverInfoLocked({});

      // Set the programming in port state.
      setPortStateLocked(true /* programEnd */);
    }

    // We are done programming the transceivers. Clear the pending datapath mask
    // (all banks) and start fresh for the next programTransceiver call
    datapathResetPendingMask_.clear();
  };

  auto i2cEvb = qsfpImpl_->getI2cEventBase();
  if (!i2cEvb) {
    // Certain platforms cannot execute multiple I2C transactions in parallel
    // and therefore don't have an I2C evb thread
    programTcvrFunc();
  } else {
    via(i2cEvb)
        .thenValue([programTcvrFunc](auto&&) mutable { programTcvrFunc(); })
        .get();
  }
}

void QsfpModule::updateLaneToPortNameMapping(
    const std::string& portName,
    uint8_t startHostLane) {
  auto hostLanes = configuredHostLanes(startHostLane);
  auto mediaLanes = configuredMediaLanes(
      startHostLane); // assumption: startMediaLane = startHostLane

  portNameToHostLanes_[portName] = {};
  for (auto lane : hostLanes) {
    hostLaneToPortName_[lane] = portName;
    portNameToHostLanes_[portName].insert(lane);
  }

  portNameToMediaLanes_[portName] = {};
  for (auto lane : mediaLanes) {
    mediaLaneToPortName_[lane] = portName;
    portNameToMediaLanes_[portName].insert(lane);
  }
}

/*
 * readyTransceiver
 *
 * Runs a function in the i2c controller's event base thread to check if the
 * module's power control configuration is same as the desired one. If it is
 * same then return true or false based on whether module is in ready state or
 * not. If the power controll config value is not same as desired one then
 * configure it correctly and return false
 *
 * @param hasTunableOpticsConfig - indicates if tunable optics config is
 *        present in qsfp_service_config. For tunable optics modules without
 *        config, high power mode transition is skipped.
 */
bool QsfpModule::readyTransceiver(bool hasTunableOpticsConfig) {
  // Always use i2cEvb to program transceivers if there's an i2cEvb
  auto powerStateCheckFn = [this, hasTunableOpticsConfig]() -> bool {
    lock_guard<std::mutex> g(qsfpModuleMutex_);
    if (present_) {
      if (!cacheIsValid()) {
        QSFP_LOG(DBG1, this) << fmt::format(
            "Transceiver {:s} - Cache is not valid, so cannot check the transceiver state",
            getNameString());
        return false;
      }
      // Check the transceiver power configuration state and then return
      // accordingly. This function's implementation is dependent on optics
      // type (Cmis, Sff etc)
      if (ensureTransceiverReadyLocked(hasTunableOpticsConfig)) {
        // After the transceiver is ready, update the cache with the latest
        // data. Some modules report inconsistent data while the module is not
        // ready, which fails the subsequent calls to program transceiver. Thus
        // ensure that the cache is updated for all the subsequent operations
        QSFP_LOG(INFO, this) << "Transceiver is ready, updating cache";
        updateQsfpData(false);
        // Update the programming start of port state
        setPortStateLocked(false /* programEnd */);
        return true;
      } else {
        return false;
      }
    } else {
      // If module is not present then don't block state machine transition
      // and return true
      return true;
    }
  };

  auto i2cEvb = qsfpImpl_->getI2cEventBase();
  if (!i2cEvb) {
    // Certain platforms cannot execute multiple I2C transactions in parallel
    // and therefore don't have an I2C evb thread. For them, call the function
    // directly from current thread
    return powerStateCheckFn();
  } else {
    // Call the function in I2c controller's event base thread
    return via(i2cEvb)
        .thenValue(
            [powerStateCheckFn](auto&&) mutable { return powerStateCheckFn(); })
        .get();
  }
}

void QsfpModule::setPortStateLocked(bool programEnd) {
  auto steadyTime = std::chrono::steady_clock::now().time_since_epoch();
  auto ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(steadyTime).count();
  if (programEnd) {
    portState_.tcvrProgrammingCompleteTs() = static_cast<int64_t>(ns);
  } else {
    portState_.tcvrProgrammingStartTs() = static_cast<int64_t>(ns);
  }
}

bool QsfpModule::tryRemediate(
    bool allPortsDown,
    time_t pauseRemdiation,
    const std::vector<std::string>& ports) {
  // Always use i2cEvb to program transceivers if there's an i2cEvb
  auto remediateTcvrFunc = [this, allPortsDown, &ports, pauseRemdiation]() {
    lock_guard<std::mutex> g(qsfpModuleMutex_);
    return tryRemediateLocked(allPortsDown, pauseRemdiation, ports);
  };
  auto i2cEvb = qsfpImpl_->getI2cEventBase();
  if (!i2cEvb) {
    // Certain platforms cannot execute multiple I2C transactions in parallel
    // and therefore don't have an I2C evb thread
    return remediateTcvrFunc();
  } else {
    bool didRemediate = false;
    via(i2cEvb)
        .thenValue([&didRemediate, remediateTcvrFunc](auto&&) mutable {
          didRemediate = remediateTcvrFunc();
        })
        .get();
    return didRemediate;
  }
}

bool QsfpModule::tryRemediateLocked(
    bool allPortsDown,
    time_t pauseRemidiation,
    const std::vector<std::string>& ports) {
  // Only update numRemediation_ iff this transceiver should remediate and
  // remediation actually happens
  if (shouldRemediateLocked(pauseRemidiation)) {
    remediateFlakyTransceiver(allPortsDown, ports);
    ++numRemediation_;
    // Remediation touches the hardware, hard resetting the optics in Cmis case,
    // so set dirty so that we always do a refresh in the next cycle and update
    // the cache with the recent data
    dirty_ = true;
    return true;
  }
  return false;
}

void QsfpModule::markLastDownTime() {
  lastDownTime_.store(std::time(nullptr));
}

/*
 * getBerFloatValue
 *
 * A utility function to convert the 16 bit BER value from module register to
 * the double value. This function is applicable to SFF as well as CMIS
 */
double QsfpModule::getBerFloatValue(uint8_t lsb, uint8_t msb) {
  int exponent = (lsb >> 3) & 0x1f;
  int mantissa = ((lsb & 0x7) << 8) | msb;

  exponent -= 24;
  double berVal = mantissa * exp10(exponent);
  return berVal;
}

void QsfpModule::setModulePauseRemediation(int32_t timeout) {
  modulePauseRemediationUntil_ = std::time(nullptr) + timeout;
}

time_t QsfpModule::getModulePauseRemediationUntil() {
  return modulePauseRemediationUntil_;
}

std::set<uint8_t> QsfpModule::getTcvrLanesForPort(
    const std::string& portName,
    phy::Side side) const {
  std::set<uint8_t> tcvrLanes;

  if (side == phy::Side::LINE) {
    auto portNameToMediaLanes = getPortNameToMediaLanes();
    if (portNameToMediaLanes.find(portName) == portNameToMediaLanes.end()) {
      XLOG(ERR) << fmt::format(
          "Port name to line side lanes not available for {:s}", portName);
    } else {
      tcvrLanes = portNameToMediaLanes.at(portName);
    }
  } else {
    auto portNameToHostLanes = getPortNameToHostLanes();
    if (portNameToHostLanes.find(portName) == portNameToHostLanes.end()) {
      XLOG(ERR) << fmt::format(
          "Port name to lanes not available for {:s}", portName);
    } else {
      tcvrLanes = portNameToHostLanes.at(portName);
    }
  }

  return tcvrLanes;
}

TransceiverInfo QsfpModule::updateFwUpgradeStatusInTcvrInfoLocked(
    bool upgradeInProgress) {
  (*(info_.wlock()))->tcvrState()->fwUpgradeInProgress() = upgradeInProgress;
  return getTransceiverInfo();
}

} // namespace fboss
} // namespace facebook
