/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/qsfp_service/StatsPublisher.h"
#include "fboss/lib/phy/PhyManager.h"
#include "fboss/qsfp_service/PortManager.h"
#include "fboss/qsfp_service/StatsPublisherHelper.h"
#include "fboss/qsfp_service/TransceiverManager.h"

#include <fb303/ThreadCachedServiceData.h>

#include <folly/logging/xlog.h>
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"

namespace facebook {
namespace fboss {
static constexpr auto kPciLockHeld = "qsfp.pciLockHeldTotal";
static constexpr auto kQsfpReadErrors = "qsfp.readErrors";
static constexpr auto kQsfpWriteErrors = "qsfp.writeErrors";
static constexpr auto kMissingPortInfo = "missingPortInfo";
static constexpr auto kPortPrefix = "qsfp.port";
static constexpr auto kInterfacePrefix = "qsfp.interface";
static constexpr auto kQsfpModuleErrors = "qsfp.moduleErrors";
static constexpr auto kFwStorageHandleMissingFromConfig =
    "qsfp.fwStorageHandleMissingFromConfig";
static constexpr auto kAOIOverride = "qsfp.aoiOverride";
static constexpr auto kMaxTimeTakenForFirmwareUpgrade =
    "qsfp.optics_firmware_upgrade.upgrade_time.max";
static constexpr auto kOpticsRemediationCounterName = "opticsRemediationCount";
static constexpr auto kEepromInvalid = "eeprom.invalid";
static constexpr auto kStatusReady = "status.ready";
static constexpr auto kTcvrStateMachineState = "tcvrStateMachineState";
static constexpr auto kPortStateMachineState = "portStateMachineState";
static constexpr auto kHighTemp = "qsfp.highTempTransceiver";
static constexpr auto kHighVcc = "qsfp.highVccTransceiver";
static constexpr auto kInterfaceHighTemp = "temp.high";
static constexpr auto kInterfaceHighVcc = "vcc.high";
static constexpr auto kNonValidatedTransceiverConfigCount =
    "qsfp.non_validated_tcvr_configs.count";
static constexpr auto kTotalTransceiversEepromInvalid =
    "qsfp.totalTransceiversEepromInvalid";
static constexpr auto kNumModulesWithInvalidBankSelect =
    "qsfp.numModulesWithInvalidBankSelect";
static constexpr auto kColdBoot = "cold_boot";
static constexpr auto kWarmBoot = "warm_boot";

void StatsPublisher::init() {
  // Start monitoring aggregation thread
  facebook::fb303::ThreadCachedServiceData::get()->startPublishThread(
      std::chrono::milliseconds(1000));
  // Initialise stats that are not called at regular intervals
  tcData().addStatExportType(kPciLockHeld, facebook::fb303::SUM);
  tcData().addStatExportType(kQsfpReadErrors, facebook::fb303::SUM);
  tcData().addStatExportType(kQsfpWriteErrors, facebook::fb303::SUM);
  tcData().addStatExportType(kQsfpModuleErrors, facebook::fb303::SUM);
  tcData().addStatExportType(
      kFwStorageHandleMissingFromConfig, facebook::fb303::SUM);
  tcData().addStatExportType(kAOIOverride, facebook::fb303::SUM);
  tcData().addStatExportType(kHighTemp, facebook::fb303::SUM);
  tcData().addStatExportType(kHighVcc, facebook::fb303::SUM);
  tcData().addStatExportType(kColdBoot, facebook::fb303::SUM);
  tcData().addStatExportType(kWarmBoot, facebook::fb303::SUM);
  publishBootType();
}

void StatsPublisher::publishBootType() {
  if (!transceiverManager_) {
    return;
  }
  tcData().addStatValue(
      transceiverManager_->canWarmBoot() ? kWarmBoot : kColdBoot,
      1,
      facebook::fb303::SUM);
}

// static
void StatsPublisher::initPerPortFb303Stats(std::set<std::string>& portNames) {
  if (portNames.empty()) {
    return;
  }
  auto prefix =
      folly::to<std::string>(kInterfacePrefix, ".", *portNames.begin(), ".");
  tcData().addStatExportType(
      folly::to<std::string>(prefix, kInterfaceHighTemp), facebook::fb303::SUM);
  tcData().addStatExportType(
      folly::to<std::string>(prefix, kInterfaceHighVcc), facebook::fb303::SUM);
}

// static
void StatsPublisher::bumpPciLockHeld() {
  tcData().addStatValue(kPciLockHeld, 1, facebook::fb303::SUM);
}

// static
void StatsPublisher::bumpReadFailure() {
  tcData().addStatValue(kQsfpReadErrors, 1, facebook::fb303::SUM);
}

// static
void StatsPublisher::bumpWriteFailure() {
  tcData().addStatValue(kQsfpWriteErrors, 1, facebook::fb303::SUM);
}

// static
void StatsPublisher::bumpModuleErrors() {
  tcData().addStatValue(kQsfpModuleErrors, 1, facebook::fb303::SUM);
}

// static
void StatsPublisher::bumpFwStorageHandleMissingFromConfig() {
  tcData().addStatValue(
      kFwStorageHandleMissingFromConfig, 1, facebook::fb303::SUM);
}

// static
void StatsPublisher::bumpAOIOverride() {
  tcData().addStatValue(kAOIOverride, 1, facebook::fb303::SUM);
}

// static
void StatsPublisher::bumpHighTemp() {
  tcData().addStatValue(kHighTemp, 1, facebook::fb303::SUM);
}

// static
void StatsPublisher::bumpHighVcc() {
  tcData().addStatValue(kHighVcc, 1, facebook::fb303::SUM);
}

// static
void StatsPublisher::missingPorts(TransceiverID module) {
  auto stat = folly::to<std::string>(
      kPortPrefix, ".eth1/", module + 1, "/1.", kMissingPortInfo);
  tcData().addStatValue(stat, 1, facebook::fb303::SUM);
}

// static
void StatsPublisher::bumpHighTempPort(std::string& interfaceName) {
  auto key = folly::to<std::string>(
      kInterfacePrefix, ".", interfaceName, ".", kInterfaceHighTemp);
  tcData().addStatValue(key, 1, facebook::fb303::SUM);
}

// static
void StatsPublisher::bumpHighVccPort(std::string& interfaceName) {
  auto key = folly::to<std::string>(
      kInterfacePrefix, ".", interfaceName, ".", kInterfaceHighVcc);
  tcData().addStatValue(key, 1, facebook::fb303::SUM);
}

namespace qsfpstats { /* a namespace for helper functions */

void publishFb303Counters(
    const std::map<int32_t, TransceiverInfo>& infoMap,
    uint32_t stats_publish_interval,
    folly::EventBase* evb,
    const TransceiverManager* transceiverManager) {
  StatsPublisherHelper helper;
  helper.publishFb303Counters(
      infoMap, stats_publish_interval, transceiverManager);
}
} // namespace qsfpstats

void StatsPublisher::publishFbagentCounters(
    const std::map<int32_t, TransceiverInfo>& infoMap,
    const std::map<int32_t, SignalFlags>& signalFlagsMap,
    const std::map<int32_t, std::map<int, MediaLaneSignals>>& mediaSignalsMap) {
  int numTransceiversUp = 0;
  int numTransceiversEepromInvalid = 0;
  int numModulesWithInvalidBankSelect = 0;
  for (const auto& kv : infoMap) {
    const TransceiverInfo& info = kv.second;
    auto interfaceName =
        transceiverManager_->getPortName(TransceiverID(kv.first));
    /* prefix of the counter will be qsfp.port.<portname>.<field> */
    std::string prefix = folly::to<std::string>(
        kPortPrefix, ".eth1/", *info.tcvrState()->port(), "/1.");
    tcData().setCounter(
        prefix + "present", ((*info.tcvrState()->present()) ? 1 : 0));

    // Counter with the port/interface name
    std::string interfacePrefix;
    if (!interfaceName.empty()) {
      interfacePrefix = folly::to<std::string>(
          StatsPublisherHelper::kInterfacePrefix, interfaceName, ".");
      tcData().setCounter(
          interfacePrefix + "present",
          ((*info.tcvrState()->present()) ? 1 : 0));
      if (auto smState = info.tcvrState()->stateMachineState()) {
        tcData().setCounter(
            interfacePrefix + kTcvrStateMachineState,
            static_cast<int64_t>(*smState));
      }
    } else {
      XLOG(ERR) << "Port name empty for id : " << kv.first
                << ", skipping present counter";
    }

    if (!(*info.tcvrState()->present())) {
      continue;
    }
    numTransceiversUp++;
    tcData().setCounter(
        prefix + "qsfpTransceiver",
        ((*info.tcvrState()->transceiver() == TransceiverType::QSFP) ? 1 : 0));

    // Counter with the port/interface name
    if (!interfaceName.empty()) {
      tcData().setCounter(
          interfacePrefix + "qsfpTransceiver",
          ((*info.tcvrState()->transceiver() == TransceiverType::QSFP) ? 1
                                                                       : 0));
    } else {
      XLOG(ERR) << "Port name empty for id : " << kv.first
                << ", skipping qsfpTransceiver counter";
    }

    if (auto stats = info.tcvrStats()->stats()) {
      tcData().setCounter(
          folly::to<std::string>(interfacePrefix, "readDownTime"),
          *stats->readDownTime());
      tcData().setCounter(
          folly::to<std::string>(interfacePrefix, "writeDownTime"),
          *stats->writeDownTime());
    }

    // Eeprom checksum
    auto csumStatus = info.tcvrState()->eepromCsumValid();
    tcData().setCounter(
        folly::to<std::string>(interfacePrefix, kEepromInvalid),
        *csumStatus ? 0 : 1);

    if (!*csumStatus) {
      numTransceiversEepromInvalid++;
    }

    if (info.tcvrState()->errorStates()->count(
            TransceiverErrorState::INVALID_BANK_SELECT)) {
      numModulesWithInvalidBankSelect++;
    }

    // Remediation Counter
    if (auto remediationTimes = info.tcvrStats()->remediationCounter()) {
      tcData().setCounter(
          folly::to<std::string>(
              interfacePrefix, kOpticsRemediationCounterName),
          *remediationTimes);
    }

    // Status ready
    if (auto status = info.tcvrState()->status()) {
      if (status->cmisModuleState()) {
        tcData().setCounter(
            folly::to<std::string>(interfacePrefix, kStatusReady),
            *status->cmisModuleState() == CmisModuleState::READY ? 1 : 0);
      } else if (status->dataNotReady()) {
        tcData().setCounter(
            folly::to<std::string>(interfacePrefix, kStatusReady),
            *status->dataNotReady() == 0 ? 1 : 0);
      }
    }
  }

  tcData().setCounter(
      folly::to<std::string>(kPortPrefix, ".totalTransceiversPresent"),
      numTransceiversUp);
  tcData().setCounter(
      kTotalTransceiversEepromInvalid, numTransceiversEepromInvalid);
  tcData().setCounter(
      kNumModulesWithInvalidBankSelect, numModulesWithInvalidBankSelect);

  // Signal Flags
  for (const auto& kv : signalFlagsMap) {
    auto interfaceName =
        transceiverManager_->getPortName(TransceiverID(kv.first));
    if (!interfaceName.empty()) {
      std::string interfacePrefixPortName = folly::to<std::string>(
          StatsPublisherHelper::kInterfacePrefix, interfaceName, ".");
      tcData().setCounter(
          interfacePrefixPortName + "txLos", *kv.second.txLos());
      tcData().setCounter(
          interfacePrefixPortName + "rxLos", *kv.second.rxLos());
      tcData().setCounter(
          interfacePrefixPortName + "txLol", *kv.second.txLol());
      tcData().setCounter(
          interfacePrefixPortName + "rxLol", *kv.second.rxLol());
    }
  }

  // Media Signals
  for (const auto& kv : mediaSignalsMap) {
    uint32_t aggTxFault = 0;
    for (const auto& mediaSignals : kv.second) {
      if (auto txFault = mediaSignals.second.txFault()) {
        aggTxFault |= (*txFault << (*(mediaSignals.second.lane())));
      }
    }
    auto interfaceName =
        transceiverManager_->getPortName(TransceiverID(kv.first));
    if (!interfaceName.empty()) {
      std::string interfacePrefixPortName = folly::to<std::string>(
          StatsPublisherHelper::kInterfacePrefix, interfaceName, ".");
      tcData().setCounter(
          folly::to<std::string>(interfacePrefixPortName, "txFault"),
          aggTxFault);
    } else {
      XLOG(ERR) << "Couldn't find portName for transceiverId : " << kv.first
                << ". Skipping Publishing FB303 counters for this transceiver";
    }
  }
}

void StatsPublisher::publishStats(
    folly::EventBase* evb,
    int32_t stats_publish_interval) {
  XLOG(INFO) << "Publishing stats to Fb303";
  std::map<int32_t, TransceiverInfo> infoMap;
  std::map<int32_t, SignalFlags> signalFlagsMap;
  std::map<int32_t, std::map<int, MediaLaneSignals>> mediaSignalsMap;
  StatsPublisherHelper helper;

  transceiverManager_->getTransceiversInfo(
      infoMap, std::make_unique<std::vector<int32_t>>(std::vector<int32_t>()));
  transceiverManager_->getAndClearTransceiversSignalFlags(
      signalFlagsMap,
      std::make_unique<std::vector<int32_t>>(std::vector<int32_t>()));
  transceiverManager_->getAndClearTransceiversMediaSignals(
      mediaSignalsMap,
      std::make_unique<std::vector<int32_t>>(std::vector<int32_t>()));

  publishFbagentCounters(
      infoMap, signalFlagsMap, mediaSignalsMap); // fb303 counters
  qsfpstats::publishFb303Counters(
      infoMap,
      stats_publish_interval,
      evb,
      transceiverManager_); // non integral counters
  tcData().setCounter(
      folly::to<std::string>(kPortPrefix, ".totalTransceiversPresentNew"),
      transceiverManager_->scanTransceiverPresence(
          std::make_unique<std::vector<int32_t>>()));

  // Trigger module data capture and restart a new cycle afterwards
  helper.triggerVdmStatsCapture(infoMap, transceiverManager_);

  tcData().setCounter(
      kMaxTimeTakenForFirmwareUpgrade,
      transceiverManager_->getMaxTimeTakenForFwUpgrade());

  transceiverManager_->publishI2cTransactionStats();
  if (phyManager_) {
    phyManager_->publishPhyIOStatsToFb303();
  }

  tcData().setCounter(
      kNonValidatedTransceiverConfigCount,
      transceiverManager_->getNumNonValidatedTransceiverConfigs(infoMap));

  if (portManager_) {
    std::map<std::string, PortStateMachineState> portStates;
    portManager_->getPortStates(
        portStates, std::make_unique<std::vector<std::string>>());
    for (const auto& [interfaceName, portState] : portStates) {
      tcData().setCounter(
          folly::to<std::string>(
              StatsPublisherHelper::kInterfacePrefix,
              interfaceName,
              ".",
              kPortStateMachineState),
          static_cast<int64_t>(portState));
    }
  }
}
} // namespace fboss
} // namespace facebook
