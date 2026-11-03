// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.
#include <folly/Subprocess.h>

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/agent/test/link_tests/LinkTestUtils.h"
#include "fboss/lib/CommonFileUtils.h"
#include "fboss/lib/CommonUtils.h"
#include "fboss/lib/config/PlatformConfigUtils.h"
#include "fboss/lib/thrift_service_client/ThriftServiceClient.h"

namespace facebook::fboss::utility {
const std::vector<std::string> kRestartQsfpService = {
    "/bin/systemctl",
    "restart",
    "qsfp_service_for_testing"};

const std::vector<std::string> kRestartQsfpServiceOss = {
    "/bin/systemctl",
    "restart",
    "qsfp_service_oss"};

const std::string kForceColdbootQsfpSvcFileName =
    "/dev/shm/fboss/qsfp_service/cold_boot_once_qsfp_service";

// This file stores all non-validated transceiver configurations found while
// running getAllTransceiverConfigValidationStatuses() in EmptyLinkTest. Each
// configuration will be in JSON format. This file is then downloaded by the
// Netcastle test runner to parse it and log these configurations to Scuba.
// Matching file definition is located in
// fbcode/neteng/netcastle/teams/fboss/constants.py.
const std::string kTransceiverConfigJsonsForScuba =
    "/tmp/transceiver_config_jsons_for_scuba.log";

// Per-port info (transceiver info, FEC, media interface) dumped by link/qsfp
// test TearDown for Scuba ingestion. Overwritten per test; Netcastle downloads
// after each test run. Matching definition will be in
// fbcode/neteng/netcastle/teams/fboss/constants.py.
const std::string kLinkQsfpTestPortInfoForScuba =
    "/tmp/link_qsfp_test_port_info_for_scuba.log";

void restartQsfpService(bool coldboot) {
  if (coldboot) {
    createFile(kForceColdbootQsfpSvcFileName);
    XLOG(DBG2) << "Restarting QSFP Service in coldboot mode";
  } else {
    XLOG(DBG2) << "Restarting QSFP Service in warmboot mode";
  }
#ifdef IS_OSS
  folly::Subprocess(kRestartQsfpServiceOss).waitChecked();
#else
  folly::Subprocess(kRestartQsfpService).waitChecked();

#endif

  checkWithRetry(
      [] {
        return utils::createQsfpServiceClient()
                   ->sync_getQsfpServiceRunState() ==
            QsfpServiceRunState::ACTIVE;
      },
      300 /* retries */,
      std::chrono::milliseconds(1000),
      "QSFP Service did not become ACTIVE after restart",
      true /* retryOnException */);
}

// Retrieves the config validation status for all active transceivers. Strings
// are retrieved instead of booleans because these contain the stringified JSONs
// of each non-validated transceiver config, which will be printed to stdout and
// ingested by Netcastle.
void getAllTransceiverConfigValidationStatuses(
    const std::set<TransceiverID>& cabledTransceivers) {
  std::vector<int32_t> expectedTransceivers(
      cabledTransceivers.begin(), cabledTransceivers.end());
  std::map<int32_t, std::string> responses;

  try {
    auto qsfpServiceClient = utils::createQsfpServiceClient();
    qsfpServiceClient->sync_getTransceiverConfigValidationInfo(
        responses, expectedTransceivers, true);
  } catch (const std::exception& ex) {
    XLOG(WARN)
        << "Failed to call qsfp_service getTransceiverConfigValidationInfo(). "
        << folly::exceptionStr(ex);
  }

  createFile(kTransceiverConfigJsonsForScuba);

  std::vector<int32_t> missingTransceivers, invalidTransceivers;
  std::vector<std::string> nonValidatedConfigs;
  for (auto transceiverID : expectedTransceivers) {
    if (auto transceiverStatusIt = responses.find(transceiverID);
        transceiverStatusIt != responses.end()) {
      if (transceiverStatusIt->second != "") {
        invalidTransceivers.push_back(transceiverID);
        nonValidatedConfigs.push_back(transceiverStatusIt->second);
      }
      continue;
    }
    missingTransceivers.push_back(transceiverID);
  }
  if (nonValidatedConfigs.size()) {
    writeSysfs(
        kTransceiverConfigJsonsForScuba,
        folly::join("\n", nonValidatedConfigs));
  }
  if (!missingTransceivers.empty()) {
    XLOG(DBG2) << "Transceivers [" << folly::join(",", missingTransceivers)
               << "] are missing config validation status.";
  }
  if (!invalidTransceivers.empty()) {
    XLOG(DBG2) << "Transceivers [" << folly::join(",", invalidTransceivers)
               << "] have non-validated configurations.";
  }
  if (missingTransceivers.empty() && invalidTransceivers.empty()) {
    XLOG(DBG2) << "Transceivers [" << folly::join(",", expectedTransceivers)
               << "] all have validated configurations.";
  }
}

void waitForAllTransceiverStates(
    bool enabled,
    const std::set<TransceiverID>& cabledTransceivers,
    uint32_t retries,
    std::chrono::duration<uint32_t, std::milli> msBetweenRetry) {
  auto expectedTcvrState = enabled ? TransceiverStateMachineState::ACTIVE
                                   : TransceiverStateMachineState::INACTIVE;
  if (FLAGS_qsfp_port_manager_mode) {
    expectedTcvrState = enabled
        ? TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED
        : TransceiverStateMachineState::TRANSCEIVER_READY;
  }
  waitForStateMachineState(
      cabledTransceivers, expectedTcvrState, retries, msBetweenRetry);
}

void waitForStateMachineState(
    const std::set<TransceiverID>& transceiversToCheck,
    TransceiverStateMachineState stateMachineState,
    uint32_t retries,
    std::chrono::duration<uint32_t, std::milli> msBetweenRetry) {
  XLOG(DBG2) << "Checking qsfp TransceiverStateMachineState on "
             << folly::join(",", transceiversToCheck);

  std::vector<int32_t> expectedTransceiver;
  for (auto transceiverID : transceiversToCheck) {
    expectedTransceiver.push_back(transceiverID);
  }

  std::vector<int32_t> badTransceivers;
  while (retries--) {
    badTransceivers.clear();
    std::map<int32_t, TransceiverInfo> info;
    try {
      auto qsfpServiceClient = utils::createQsfpServiceClient();
      qsfpServiceClient->sync_getTransceiverInfo(info, expectedTransceiver);
    } catch (const std::exception& ex) {
      // We have retry mechanism to handle failure. No crash here
      XLOG(WARN) << "Failed to call qsfp_service getTransceiverInfo(). "
                 << folly::exceptionStr(ex);
    }
    // Check whether all expected transceivers have expected state
    for (auto transceiverID : expectedTransceiver) {
      // Only continue if the transceiver state machine matches
      if (auto transceiverInfoIt = info.find(transceiverID);
          transceiverInfoIt != info.end()) {
        if (auto state =
                transceiverInfoIt->second.tcvrState()->stateMachineState();
            state.has_value() && *state == stateMachineState) {
          continue;
        }
      }
      // Otherwise such transceiver is considered to be in a bad state
      badTransceivers.push_back(transceiverID);
    }

    if (badTransceivers.empty()) {
      XLOG(DBG2) << "All qsfp TransceiverStateMachineState on "
                 << folly::join(",", expectedTransceiver) << " match "
                 << apache::thrift::util::enumNameSafe(stateMachineState);
      return;
    } else {
      /* sleep override */
      std::this_thread::sleep_for(msBetweenRetry);
    }
  }

  throw FbossError(
      "Transceivers:[",
      folly::join(",", badTransceivers),
      "] don't have expected TransceiverStateMachineState:",
      apache::thrift::util::enumNameSafe(stateMachineState));
}

void includeLpoTransceivers(
    std::map<int32_t, TransceiverInfo>& infos,
    bool includeLpo) {
  if (includeLpo) {
    return;
  }
  // Remove LPO modules from the map
  auto itr = infos.begin();
  while (itr != infos.end()) {
    if (itr->second.tcvrState()->moduleTechnology().value() ==
        ModuleTechnology::LPO) {
      itr = infos.erase(itr);
    } else {
      itr++;
    }
  }
}

void waitForPortStateMachineState(
    bool enabled,
    const std::vector<PortID>& portsToCheck,
    uint32_t retries,
    std::chrono::duration<uint32_t, std::milli> msBetweenRetry) {
  if (!FLAGS_qsfp_port_manager_mode) {
    return;
  }
  XLOG(DBG2) << "Checking qsfp PortStateMachineState on "
             << folly::join(",", portsToCheck);

  auto stateMachineState = enabled ? PortStateMachineState::PORT_UP
                                   : PortStateMachineState::UNINITIALIZED;
  std::vector<int32_t> expectedPorts;
  expectedPorts.reserve(portsToCheck.size());
  for (auto portID : portsToCheck) {
    expectedPorts.push_back(static_cast<int32_t>(portID));
  }

  std::vector<int32_t> badPorts;
  while (retries--) {
    badPorts.clear();
    std::map<int32_t, PortStateMachineState> info;
    try {
      auto qsfpServiceClient = utils::createQsfpServiceClient();
      qsfpServiceClient->sync_getPortStateMachineState(info, expectedPorts);
    } catch (const std::exception& ex) {
      // We have retry mechanism to handle failure. No crash here
      XLOG(WARN) << "Failed to call qsfp_service getPortStateMachineState(). "
                 << folly::exceptionStr(ex);
    }
    // Check whether all expected ports have expected state
    for (auto portID : expectedPorts) {
      // Only continue if the port state machine matches
      if (auto portInfoIt = info.find(portID); portInfoIt != info.end()) {
        if (portInfoIt->second == stateMachineState) {
          continue;
        }
      }
      // Otherwise such port is considered to be in a bad state
      badPorts.push_back(portID);
    }

    if (badPorts.empty()) {
      XLOG(DBG2) << "All qsfp PortStateMachineState on "
                 << folly::join(",", expectedPorts) << " match "
                 << apache::thrift::util::enumNameSafe(stateMachineState);
      return;
    } else {
      /* sleep override */
      std::this_thread::sleep_for(msBetweenRetry);
    }
  }

  throw FbossError(
      "Ports:[",
      folly::join(",", badPorts),
      "] don't have expected PortStateMachineState:",
      apache::thrift::util::enumNameSafe(stateMachineState));
}

// Wait until we have successfully fetched transceiver info (and thus know
// which transceivers are available for testing)
std::map<int32_t, TransceiverInfo> waitForTransceiverInfo(
    std::vector<int32_t> transceiverIds,
    bool includeLpo,
    uint32_t retries,
    std::chrono::duration<uint32_t, std::milli> msBetweenRetry) {
  std::map<int32_t, TransceiverInfo> info;
  while (retries--) {
    try {
      auto qsfpServiceClient = utils::createQsfpServiceClient();
      qsfpServiceClient->sync_getTransceiverInfo(info, transceiverIds);
    } catch (const std::exception& ex) {
      XLOG(WARN) << "Failed to call qsfp_service getTransceiverInfo(). "
                 << folly::exceptionStr(ex);
    }
    // Make sure we have at least one present transceiver
    for (const auto& it : info) {
      if (*it.second.tcvrState()->present()) {
        includeLpoTransceivers(info, includeLpo);
        return info;
      }
    }
    XLOG(DBG2) << "TransceiverInfo was empty";
    if (retries) {
      /* sleep override */
      std::this_thread::sleep_for(msBetweenRetry);
    }
  }

  throw FbossError("TransceiverInfo was never populated.");
}

bool waitForFreshTransceiverInfo(
    const std::vector<int32_t>& transceiverIds,
    const std::map<int32_t, TransceiverInfo>& prevInfo,
    std::map<int32_t, TransceiverInfo>& freshInfo,
    bool includeLpo,
    uint32_t retries,
    std::chrono::duration<uint32_t, std::milli> msBetweenRetry) {
  // A transceiver missing from prevInfo has no baseline to advance past, so
  // hold it to the time this wait started rather than accepting whatever
  // qsfp_service already had cached.
  const int64_t waitStartTime = std::time(nullptr);
  std::map<int32_t, int64_t> baseline;
  for (auto tcvrId : transceiverIds) {
    auto prevItr = prevInfo.find(tcvrId);
    baseline[tcvrId] = prevItr != prevInfo.end()
        ? prevItr->second.tcvrStats()->timeCollected().value()
        : waitStartTime;
  }

  while (retries--) {
    freshInfo.clear();
    try {
      freshInfo = waitForTransceiverInfo(transceiverIds, includeLpo);
    } catch (const std::exception& ex) {
      XLOG(WARN) << "Failed to fetch transceiverInfo. "
                 << folly::exceptionStr(ex);
    }
    std::vector<int32_t> staleTcvrs;
    for (auto tcvrId : transceiverIds) {
      auto tcvrInfoItr = freshInfo.find(tcvrId);
      if (tcvrInfoItr == freshInfo.end() ||
          tcvrInfoItr->second.tcvrStats()->timeCollected().value() <=
              baseline[tcvrId]) {
        staleTcvrs.push_back(tcvrId);
      }
    }
    if (staleTcvrs.empty()) {
      return true;
    }
    XLOG(DBG2) << "Transceivers:[" << folly::join(",", staleTcvrs)
               << "] have no transceiverInfo update newer than their baseline, "
               << retries << " retries left";
    if (retries) {
      /* sleep override */
      std::this_thread::sleep_for(msBetweenRetry);
    }
  }
  return false;
}

std::map<std::string, MediaInterfaceCode> getPortToMediaInterface() {
  std::map<std::string, MediaInterfaceCode> portToMedia;
  try {
    auto qsfpServiceClient = utils::createQsfpServiceClient();
    qsfpServiceClient->sync_getPortMediaInterface(portToMedia);
  } catch (const std::exception& ex) {
    XLOG(WARN) << "Failed to call qsfp_service getPortMediaInterface(). "
               << folly::exceptionStr(ex);
    return {};
  }
  return portToMedia;
}

const TransceiverSpec* getTransceiverSpec(const SwSwitch* sw, PortID portId) {
  auto& platformPort = sw->getPlatformMapping()->getPlatformPort(portId);
  const auto& chips = sw->getPlatformMapping()->getChips();
  if (auto tcvrIds = utility::getTransceiverIds(platformPort, chips);
      !tcvrIds.empty()) {
    auto transceiver = sw->getState()->getTransceivers()->getNodeIf(tcvrIds[0]);
    return transceiver.get();
  }
  return nullptr;
}

// This should only be called by platforms that actually have
// an external phy
std::optional<int32_t> getPortExternalPhyID(const SwSwitch* sw, PortID port) {
  auto platformMapping = sw->getPlatformMapping();
  const auto& platformPortEntry = platformMapping->getPlatformPort(port);
  const auto& chips = platformMapping->getChips();
  if (chips.empty()) {
    throw FbossError("Not platform data plane phy chips");
  }

  const auto& xphy = utility::getDataPlanePhyChips(
      platformPortEntry, chips, phy::DataPlanePhyChipType::XPHY);
  if (xphy.empty()) {
    return std::nullopt;
  } else {
    // One port should only has one xphy id
    CHECK_EQ(xphy.size(), 1);
    return *xphy.begin()->second.physicalID();
  }
}
} // namespace facebook::fboss::utility
