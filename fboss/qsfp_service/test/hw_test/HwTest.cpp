/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/qsfp_service/test/hw_test/HwTest.h"

#include <fmt/format.h>
#include <folly/String.h>
#include <folly/gen/Base.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <algorithm>
#include <iterator>
#include "fboss/agent/FbossError.h"
#include "fboss/lib/CommonFileUtils.h"
#include "fboss/lib/CommonUtils.h"
#include "fboss/lib/config/PlatformConfigUtils.h"
#include "fboss/lib/fpga/MultiPimPlatformSystemContainer.h"
#include "fboss/lib/if/LinkQsfpTestMachineInfo.h"
#include "fboss/lib/if/LinkQsfpTestPortInfoUtils.h"
#include "fboss/lib/if/gen-cpp2/link_qsfp_test_port_info_types.h"
#include "fboss/lib/phy/PhyManager.h"
#include "fboss/qsfp_service/QsfpServer.h"
#include "fboss/qsfp_service/module/CdbCommandBlock.h"
#include "fboss/qsfp_service/platforms/wedge/WedgeManager.h"
#include "fboss/qsfp_service/test/hw_test/HwPortUtils.h"
#include "fboss/qsfp_service/test/hw_test/HwQsfpEnsemble.h"

#ifndef IS_OSS
#include "common/base/Proc.h"
#endif

DEFINE_bool(
    setup_for_warmboot,
    false,
    "Set up test for QSFP warmboot. Useful for testing individual "
    "tests doing a full process warmboot and verifying expectations");

DEFINE_bool(
    list_production_feature,
    false,
    "List production feature needed for every single test.");

namespace facebook::fboss {

using namespace std::chrono_literals;

namespace {
// Per-transceiver info dumped at TearDown for Scuba ingestion. Netcastle
// downloads this file after each test run and logs it to the
// fboss_link_qsfp_test_port_info Scuba table. Must match
// FBOSS_LINK_QSFP_TEST_PORT_INFO_FILE in
// fbcode/neteng/netcastle/teams/fboss/constants.py and the link test path
// (kLinkQsfpTestPortInfoForScuba) in
// fbcode/fboss/agent/test/link_tests/LinkTestUtils.cpp.
const std::string kLinkQsfpTestPortInfoForScuba =
    "/tmp/link_qsfp_test_port_info_for_scuba.log";

// Per-port errors dumped alongside the port info at TearDown. Netcastle
// downloads this file after each test and logs it to the
// fboss_link_qsfp_test_port_errors Scuba table. Must match
// FBOSS_LINK_QSFP_TEST_PORT_ERROR_FILE in
// fbcode/neteng/netcastle/teams/fboss/constants.py.
const std::string kLinkQsfpTestPortErrorForScuba =
    "/tmp/link_qsfp_test_port_error_for_scuba.log";

constexpr auto kPresentButNotInConfig = "EXTRA";

// Builds a single Scuba row for a tested transceiver from its TransceiverInfo
// (vendor, firmware, module/per-port media interface). Returns a row with just
// the test/transceiver/port identity if the transceiver has no info entry.
LinkQsfpTestPortInfo buildPortInfoRow(
    WedgeManager* wedgeManager,
    const TransceiverID& id,
    const std::string& testName,
    const std::map<int32_t, TransceiverInfo>& tcvrInfos) {
  LinkQsfpTestPortInfo portInfo;
  portInfo.testName() = testName;
  portInfo.transceiverId() = static_cast<int32_t>(id);

  std::string tcvrPortName;
  try {
    tcvrPortName = wedgeManager->getPortName(id);
    portInfo.portName() = tcvrPortName;
    if (auto portId = wedgeManager->getPortIDByPortName(tcvrPortName)) {
      portInfo.portId() = static_cast<int32_t>(*portId);
    }
  } catch (const std::exception&) {
    // Leave port name/id unset if the transceiver has no mapped port.
  }

  auto tcvrIt = tcvrInfos.find(static_cast<int32_t>(id));
  if (tcvrIt == tcvrInfos.end()) {
    return portInfo;
  }
  const auto& tcvrState = *tcvrIt->second.tcvrState();
  utility::populateTransceiverInfoFields(portInfo, tcvrState, tcvrPortName);
  return portInfo;
}

// Transceivers qsfp_service sees as physically present that the config does not
// cable up. Note this is scoped to the platform mapping: WedgeManager only
// creates Transceiver objects for slots the mapping knows about, so a module in
// an unmapped slot can never appear in getPresentTransceivers() and is not
// reported here.
std::vector<TransceiverID> getPresentTransceiversNotInConfig(
    WedgeManager* wedgeManager,
    const std::vector<TransceiverID>& cabledTcvrs) {
  const auto present = wedgeManager->getPresentTransceivers();
  // getCabledPortTranceivers() builds its vector from an unordered_set, so it
  // arrives unsorted; set_difference requires both ranges ordered.
  const std::set<TransceiverID> expected(
      cabledTcvrs.begin(), cabledTcvrs.end());
  std::vector<TransceiverID> unexpected;
  std::set_difference(
      present.begin(),
      present.end(),
      expected.begin(),
      expected.end(),
      std::back_inserter(unexpected));
  return unexpected;
}

// Builds one Scuba row per present-but-unconfigured transceiver. pool is left
// unset: it is Basset lab inventory state, unknown on the switch, and Netcastle
// stamps it at upload time.
LinkQsfpTestPortError buildPortErrorRow(
    WedgeManager* wedgeManager,
    const TransceiverID& id,
    const std::string& machineName,
    const std::string& machineType,
    const std::map<int32_t, TransceiverInfo>& tcvrInfos) {
  LinkQsfpTestPortError portError;
  portError.machineName() = machineName;
  portError.machineType() = machineType;
  portError.error() = kPresentButNotInConfig;

  // Non-empty for every id reachable here (presence implies a mapped port), but
  // fall back to the transceiver name so a row is never anonymous. Guarded like
  // buildPortInfoRow: these transceivers are by definition in unusual mapping
  // state, and one that cannot be named must not abort the whole dump.
  std::string portName;
  try {
    portName = wedgeManager->getPortName(id);
    if (portName.empty()) {
      portName = wedgeManager->getTransceiverName(id);
    }
  } catch (const std::exception&) {
    // Leave the port name empty if the transceiver has no mapped port.
  }
  portError.portName() = std::move(portName);

  auto tcvrIt = tcvrInfos.find(static_cast<int32_t>(id));
  if (tcvrIt != tcvrInfos.end()) {
    utility::populateTransceiverErrorFields(
        portError, *tcvrIt->second.tcvrState());
  }
  return portError;
}
} // namespace

HwTest::HwTest(bool setupOverrideTcvrToPortAndProfile)
    : setupOverrideTcvrToPortAndProfile_(setupOverrideTcvrToPortAndProfile) {}

void HwTest::SetUp() {
  if (FLAGS_list_production_feature) {
    printProductionFeatures();
    return;
  }

  // First use QsfpConfig to init default command line arguments
  initFlagDefaultsFromQsfpConfig();
  // Change the default remediation interval to 0 to avoid waiting
  gflags::SetCommandLineOptionWithMode(
      "remediate_interval", "0", gflags::SET_FLAGS_DEFAULT);
  gflags::SetCommandLineOptionWithMode(
      "initial_remediate_interval", "0", gflags::SET_FLAGS_DEFAULT);

  // This is used to set up the override TransceiveToPortAndProfile so that we
  // don't have to rely on wedge_agent::programInternalPhyPorts() in our
  // qsfp_hw_test
  if (setupOverrideTcvrToPortAndProfile_) {
    gflags::SetCommandLineOptionWithMode(
        "override_program_iphy_ports_for_test", "1", gflags::SET_FLAGS_DEFAULT);
  }

  ensemble_ = std::make_unique<HwQsfpEnsemble>();
  ensemble_->init();

  if (FLAGS_port_manager_mode) {
    // In PortManager mode, we need ports to be enabled to bring ports /
    // transceivers to programmed state.
    ensemble_->getQsfpServiceHandler()->setOverrideAgentPortStatusForTesting(
        true /* up */, true /* enabled */);
  }

  // Setting remediation paused as a default for 10 minutes
  ensemble_->getWedgeManager()->setPauseRemediation(600, nullptr);

  // Allow back to back refresh and customizations in test
  gflags::SetCommandLineOptionWithMode(
      "qsfp_data_refresh_interval", "0", gflags::SET_FLAGS_DEFAULT);
  gflags::SetCommandLineOptionWithMode(
      "customize_interval", "0", gflags::SET_FLAGS_DEFAULT);

  waitTillCabledTcvrProgrammed();

  // It also takes ~5 seconds sometimes for the CMIS modules to be
  // functional after a data path deinit, that can happen in the customize call
  if (!didWarmBoot()) {
    // Warmboot shouldn't configure the transceiver again so we shouldn't
    // require a wait here
    sleep(5);
  }

  XLOG(INFO) << "HwTest::SetUp() complete";
}

void HwTest::TearDown() {
  // Dump per-transceiver metadata for any transceivers the test registered.
  // Runs before the ensemble is torn down so transceiver info is still
  // available. Best effort; never fails the test. gtest TearDown() runs on both
  // the cold-boot (--setup_for_warmboot) and warm-boot phases for qsfp hw
  // tests, so both phases emit rows.
  dumpTestMetadata();

  // At the end of the test, expect the watchdog fired count to be 0 because we
  // don't expect any deadlocked threads

  // ensemble_ is nullptr when --list_production_feature feature is enabled,
  // because SetUp() code returns early after printing production features –
  // skipping over ensemble initialization.
  if (ensemble_ != nullptr) {
    EXPECT_EQ(
        ensemble_->getWedgeManager()
            ->getStateMachineThreadHeartbeatMissedCount(),
        0);
    EXPECT_NO_THROW(checkQsfpMemoryInBounds());
    ensemble_.reset();
  }
  // We expect that any coldboot flag set during the test should be cleared
  EXPECT_FALSE(checkFileExists(TransceiverManager::forceColdBootFileName()));
}

void HwTest::checkQsfpMemoryInBounds() const {
#ifndef IS_OSS
  const auto* qsfpConfig = ensemble_->getWedgeManager()->getQsfpConfig();
  if (qsfpConfig == nullptr) {
    throw FbossError("QSFP config is not loaded, cannot check memory bounds");
  }
  auto qsfpTestConfig = qsfpConfig->thrift.qsfpTestConfig();
  if (!qsfpTestConfig.has_value() ||
      !qsfpTestConfig->qsfpServiceMemLimitBytes().has_value()) {
    XLOG(INFO) << "qsfpServiceMemLimitBytes not set in qsfp test config, "
               << "skipping memory check";
    return;
  }
  int64_t memUsage = facebook::Proc::getMemoryUsage();
  auto memUsageThreshold = *qsfpTestConfig->qsfpServiceMemLimitBytes();
  if (memUsage > memUsageThreshold) {
    throw FbossError(
        "QSFP service RSS memory ",
        memUsage,
        " above expected ",
        memUsageThreshold);
  }
#endif
}

bool HwTest::didWarmBoot() const {
  return ensemble_->didWarmBoot();
}

MultiPimPlatformPimContainer* HwTest::getPimContainer(int pimID) {
  return ensemble_->getPhyManager()->getSystemContainer()->getPimContainer(
      pimID);
}

void HwTest::setupForWarmboot() const {
  ensemble_->setupForWarmboot();
}

// Refresh transceivers and retry after some unsuccessful attempts
std::vector<TransceiverID> HwTest::refreshTransceiversWithRetry(
    int numRetries) {
  auto expectedIds = utility::getCabledPortTranceivers(getHwQsfpEnsemble());
  std::map<int32_t, TransceiverInfo> transceiversBeforeRefresh;
  std::vector<TransceiverID> transceiverIds;
  ensemble_->getWedgeManager()->getTransceiversInfo(
      transceiversBeforeRefresh,
      std::make_unique<std::vector<int32_t>>(
          utility::legacyTransceiverIds(expectedIds)));

  // Lambda to do a refresh and confirm refresh actually happened
  auto refresh =
      [this, &expectedIds, &transceiversBeforeRefresh, &transceiverIds]() {
        transceiverIds = ensemble_->getWedgeManager()->refreshTransceivers();
        std::map<int32_t, TransceiverInfo> transceiversAfterRefresh;
        ensemble_->getWedgeManager()->getTransceiversInfo(
            transceiversAfterRefresh,
            std::make_unique<std::vector<int32_t>>(
                utility::legacyTransceiverIds(expectedIds)));
        for (auto id : expectedIds) {
          // Confirm all the cabled transceivers were returned by
          // getTransceiversInfo
          if (transceiversAfterRefresh.find(id) ==
              transceiversAfterRefresh.end()) {
            XLOG(WARN) << "TransceiverID : " << id
                       << ": transceiverInfo not returned after refresh";
            return false;
          }
          // It's possible that there were no transceivers to return before a
          // refresh
          if (transceiversBeforeRefresh.find(id) ==
              transceiversBeforeRefresh.end()) {
            continue;
          }
          auto timeAfter =
              *transceiversAfterRefresh[id].tcvrState()->timeCollected();
          auto timeBefore =
              *transceiversBeforeRefresh[id].tcvrState()->timeCollected();
          // Confirm that the timestamp advanced which will only happen when
          // refresh is successful
          if (timeAfter <= timeBefore) {
            XLOG(WARN) << "TransceiverID : " << id
                       << ": timestamp in the TransceiverInfo after refresh = "
                       << timeAfter
                       << ", timestamp in the TransceiverInfo before refresh = "
                       << timeBefore;
            return false;
          }
        }
        return true;
      };
  checkWithRetry(
      refresh, numRetries, 1s, "Never refreshed all expected transceivers");

  return transceiverIds;
}

// This function is only used if new port programming feature is enabled
// We will wait till all the cabled transceivers reach the
// TRANSCEIVER_PROGRAMMED state by retrying `numRetries` times of
// TransceiverManager::refreshStateMachines()
void HwTest::waitTillCabledTcvrProgrammed(int numRetries, bool portUp) {
  // First get all expected transceivers based on cabled ports from agent.conf
  const auto& expectedIds =
      utility::getCabledPortTranceivers(getHwQsfpEnsemble());
  const auto& allCabledPorts = utility::getCabledPorts(getHwQsfpEnsemble());
  const PortStateMachineState expectedPortState = portUp
      ? PortStateMachineState::PORT_UP
      : PortStateMachineState::PORT_DOWN;

  // Filter cabled ports to only include those managed by qsfp_service
  // (i.e., ports with TRANSCEIVER or XPHY chips). Ports connected directly
  // to backplane without XPHY are not managed by qsfp_service.
  const auto& platformMapping = getHwQsfpEnsemble()->getPlatformMapping();
  auto managedPortIds = utility::getPortIdsWithTransceiverOrXphy(
      platformMapping->getPlatformPorts(), platformMapping->getChips());
  std::set<PortID> managedPortSet(managedPortIds.begin(), managedPortIds.end());

  std::set<PortID> expectedPorts;
  for (const auto& portId : allCabledPorts) {
    if (managedPortSet.count(portId) > 0) {
      expectedPorts.insert(portId);
    } else {
      XLOG(INFO) << "Skipping port " << portId
                 << " - not managed by qsfp_service (no TRANSCEIVER or XPHY)";
    }
  }
  // Due to some platforms are easy to have i2c issue which causes the current
  // refresh not work as expected. Adding enough retries to make sure that we
  // at least can secure TRANSCEIVER_PROGRAMMED after `numRetries` times.
  auto refreshStateMachinesTillTcvrProgrammed =
      [this, &expectedIds, &expectedPorts, expectedPortState]() {
        auto wedgeMgr = getHwQsfpEnsemble()->getWedgeManager();
        getHwQsfpEnsemble()->getQsfpServiceHandler()->refreshStateMachines();
        std::vector<TransceiverID> notProgrammedTcvrs;
        std::vector<PortID> notProgrammedPorts;
        bool retval{true};

        for (auto id : expectedIds) {
          auto curState = wedgeMgr->getCurrentState(id);
          // Statemachine can support transceiver programming (iphy/xphy/tcvr)
          // when `setupOverrideTcvrToPortAndProfile_` is true.
          if (setupOverrideTcvrToPortAndProfile_ &&
              curState !=
                  TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED) {
            notProgrammedTcvrs.push_back(id);
            retval = false;
          }
        }

        if (!notProgrammedTcvrs.empty()) {
          XLOG(WARN) << "Transceivers that should be programmed but are not: "
                     << folly::join(",", notProgrammedTcvrs);
        }

        if (FLAGS_port_manager_mode) {
          auto portManager =
              getHwQsfpEnsemble()->getQsfpServiceHandler()->getPortManager();
          for (const auto& portId : expectedPorts) {
            auto curState = portManager->getPortState(portId);
            if (setupOverrideTcvrToPortAndProfile_ &&
                curState != expectedPortState) {
              notProgrammedPorts.push_back(portId);
              retval = false;
            }
          }
        }

        if (!notProgrammedPorts.empty()) {
          XLOG(ERR) << "Ports that did not reach "
                    << apache::thrift::util::enumNameSafe(expectedPortState)
                    << ": " << folly::join(",", notProgrammedPorts);
        }

        return retval;
      };

  // Retry until all state machines reach TRANSCEIVER_PROGRAMMED
  checkWithRetry(
      refreshStateMachinesTillTcvrProgrammed,
      numRetries /* retries */,
      std::chrono::milliseconds(5000) /* msBetweenRetry */,
      "Never got all transceivers programmed");
}

std::vector<int> HwTest::getCabledOpticalAndActiveTransceiverIDs() {
  auto transceivers = utility::legacyTransceiverIds(
      utility::getCabledPortTranceivers(getHwQsfpEnsemble()));
  std::map<int32_t, TransceiverInfo> transceiversInfo;
  getHwQsfpEnsemble()->getWedgeManager()->getTransceiversInfo(
      transceiversInfo, std::make_unique<std::vector<int32_t>>(transceivers));

  return folly::gen::from(transceivers) |
      folly::gen::filter([&transceiversInfo](int32_t tcvrId) {
           auto& tcvrInfo = transceiversInfo[tcvrId];
           return TransceiverManager::opticalOrActiveCable(
               *tcvrInfo.tcvrState());
         }) |
      folly::gen::as<std::vector>();
}

void HwTest::addTestedTransceiver(const TransceiverID& id) {
  testedTransceivers_.insert(id);
}

void HwTest::addTestedTransceivers(const std::vector<TransceiverID>& ids) {
  testedTransceivers_.insert(ids.begin(), ids.end());
}

void HwTest::addTestedTransceiverIds(const std::vector<int32_t>& ids) {
  for (auto id : ids) {
    testedTransceivers_.insert(TransceiverID(id));
  }
}

void HwTest::addTestedPorts(const std::vector<PortID>& portIds) {
  auto* platformMapping = getHwQsfpEnsemble()->getPlatformMapping();
  for (const auto& portId : portIds) {
    try {
      testedTransceivers_.insert(
          TransceiverID(platformMapping->getTransceiverIdFromSwPort(portId)));
    } catch (const std::exception& ex) {
      XLOG(WARN) << "Could not map port " << portId
                 << " to a transceiver for test metadata: "
                 << folly::exceptionStr(ex);
    }
  }
}

void HwTest::addTestMetadata(
    const TransceiverID& id,
    const std::string& key,
    const std::string& value) {
  perTransceiverMetadata_[id][key] = value;
}

void HwTest::addVerifiedProductionFeatures(
    const std::vector<qsfp_production_features::QsfpProductionFeature>&
        features) {
  verifiedProductionFeatures_.insert(
      verifiedProductionFeatures_.end(), features.begin(), features.end());
}

void HwTest::dumpTestMetadata() {
  // ensemble_ is nullptr in --list_production_feature mode; nothing to dump.
  if (ensemble_ == nullptr) {
    return;
  }

  std::vector<std::string> jsonLines;
  try {
    if (!testedTransceivers_.empty()) {
      auto testInfo = testing::UnitTest::GetInstance()->current_test_info();
      auto testName =
          fmt::format("{}.{}", testInfo->test_suite_name(), testInfo->name());

      std::vector<std::string> featureNames;
      featureNames.reserve(verifiedProductionFeatures_.size());
      for (const auto& feature : verifiedProductionFeatures_) {
        featureNames.push_back(apache::thrift::util::enumNameSafe(feature));
      }

      auto* wedgeManager = getHwQsfpEnsemble()->getWedgeManager();

      std::vector<int32_t> tcvrIds;
      tcvrIds.reserve(testedTransceivers_.size());
      for (const auto& id : testedTransceivers_) {
        tcvrIds.push_back(static_cast<int32_t>(id));
      }

      std::map<int32_t, TransceiverInfo> tcvrInfos;
      wedgeManager->getTransceiversInfo(
          tcvrInfos, std::make_unique<std::vector<int32_t>>(tcvrIds));

      for (const auto& id : testedTransceivers_) {
        auto portInfo = buildPortInfoRow(wedgeManager, id, testName, tcvrInfos);
        portInfo.verifiedProductionFeatures() = featureNames;
        if (auto it = perTransceiverMetadata_.find(id);
            it != perTransceiverMetadata_.end()) {
          portInfo.extraMetadata() = it->second;
        }
        jsonLines.push_back(
            apache::thrift::SimpleJSONSerializer::serialize<std::string>(
                portInfo));
      }
    }
  } catch (const std::exception& ex) {
    XLOG(WARN) << "Failed to build qsfp hw test port info: "
               << folly::exceptionStr(ex);
  }

  // Always (over)write the file, even when there are no rows, so that a test
  // which registered no transceivers clears any file left by a previous test.
  // The dump path is shared across tests (and with link tests), so without this
  // Netcastle would re-upload a stale file and mis-attribute its rows to the
  // current test.
  auto content = folly::join("\n", jsonLines);
  if (writeSysfs(kLinkQsfpTestPortInfoForScuba, content)) {
    XLOG(DBG2) << "Dumped qsfp hw test port info for " << jsonLines.size()
               << " transceivers to " << kLinkQsfpTestPortInfoForScuba;
  } else {
    XLOG(ERR) << "Failed to write qsfp hw test port info to "
              << kLinkQsfpTestPortInfoForScuba;
  }
  dumpPortErrors();

  testedTransceivers_.clear();
  perTransceiverMetadata_.clear();
  verifiedProductionFeatures_.clear();
}

void HwTest::dumpPortErrors() {
  std::vector<std::string> jsonLines;
  try {
    auto* wedgeManager = getHwQsfpEnsemble()->getWedgeManager();
    auto unexpected = getPresentTransceiversNotInConfig(
        wedgeManager, utility::getCabledPortTranceivers(getHwQsfpEnsemble()));

    if (!unexpected.empty()) {
      std::vector<int32_t> tcvrIds;
      tcvrIds.reserve(unexpected.size());
      for (const auto& id : unexpected) {
        tcvrIds.push_back(static_cast<int32_t>(id));
      }
      std::map<int32_t, TransceiverInfo> tcvrInfos;
      wedgeManager->getTransceiversInfo(
          tcvrInfos, std::make_unique<std::vector<int32_t>>(tcvrIds));

      auto machineName = utility::getMachineName();
      auto machineType = utility::getMachineType();
      for (const auto& id : unexpected) {
        auto portError = buildPortErrorRow(
            wedgeManager, id, machineName, machineType, tcvrInfos);
        jsonLines.push_back(
            apache::thrift::SimpleJSONSerializer::serialize<std::string>(
                portError));
        XLOG(WARN) << "Transceiver " << static_cast<int32_t>(id) << " ("
                   << *portError.portName()
                   << ") is present but not in the config";
      }
    }
  } catch (const std::exception& ex) {
    // Drop anything already collected: a partially built set is
    // indistinguishable in Scuba from a complete one, so publishing it would
    // silently under-report. Better to emit nothing than a truncated set.
    jsonLines.clear();
    XLOG(WARN) << "Failed to build qsfp hw test port errors: "
               << folly::exceptionStr(ex);
  }

  // Always (over)write, even with no rows, so a file left by a previous test is
  // never re-uploaded and mis-attributed to this one.
  auto content = folly::join("\n", jsonLines);
  if (writeSysfs(kLinkQsfpTestPortErrorForScuba, content)) {
    XLOG(DBG2) << "Dumped " << jsonLines.size()
               << " qsfp hw test port errors to "
               << kLinkQsfpTestPortErrorForScuba;
  } else {
    XLOG(ERR) << "Failed to write qsfp hw test port errors to "
              << kLinkQsfpTestPortErrorForScuba;
  }
}

std::vector<qsfp_production_features::QsfpProductionFeature>
HwTest::getProductionFeatures() const {
  return {};
}

void HwTest::printProductionFeatures() const {
  std::vector<std::string> supportedFeatures;
  for (const auto& feature : getProductionFeatures()) {
    supportedFeatures.push_back(apache::thrift::util::enumNameSafe(feature));
  }
  std::cout << "Feature List: " << folly::join(",", supportedFeatures) << "\n";
  GTEST_SKIP();
}

TEST_F(HwTest, CheckTcvrNameAndInterfaces) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::TRANSCEIVER_DETECTION});
  addTestedTransceivers(utility::getCabledPortTranceivers(getHwQsfpEnsemble()));
  auto wedgeManager = getHwQsfpEnsemble()->getWedgeManager();
  std::map<int32_t, TransceiverInfo> insertedTransceiversMap;
  wedgeManager->getTransceiversInfo(
      insertedTransceiversMap, std::make_unique<std::vector<int32_t>>());
  CHECK(!insertedTransceiversMap.empty());
  auto platformMappingTransceiverMap =
      getHwQsfpEnsemble()->getWedgeManager()->getTcvrIdToTcvrNameMap();
  for (auto& [id, tcvr] : insertedTransceiversMap) {
    if (platformMappingTransceiverMap.find(static_cast<TransceiverID>(id)) ==
        platformMappingTransceiverMap.end()) {
      // It's possible that there are more transceivers on the system than the
      // one used by current config. For example, the BSP mapping may have more
      // transceiver slots corresponding to multiple NPUs, but in reality we are
      // using the single NPU mode and hence should not test the unused
      // transceivers in this test
      continue;
    }
    auto tcvrStateName = *tcvr.tcvrState()->tcvrName();
    auto tcvrStatsName = *tcvr.tcvrState()->tcvrName();
    XLOG(INFO) << "Checking transceiver name and interfaces for id: " << id
               << " " << tcvrStateName;
    EXPECT_EQ(tcvrStateName, tcvrStatsName);
    EXPECT_TRUE(
        tcvrStateName.starts_with("eth") || tcvrStateName.starts_with("fab"));
    auto tcvrPortName = wedgeManager->getPortName(TransceiverID(id));
    EXPECT_EQ(tcvrStateName + "/1", tcvrPortName);

    auto ports = wedgeManager->getPortNames(TransceiverID(id));
    EXPECT_FALSE(ports.empty());
    EXPECT_EQ(*tcvr.tcvrState()->interfaces(), ports);
    EXPECT_EQ(*tcvr.tcvrStats()->interfaces(), ports);
  }
}
TEST_F(HwTest, checkCmisModuleFirmwareUpgradeCdbTimeout) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::
           TRANSCEIVER_ADVERTISEMENTS});
  auto wedgeManager = getHwQsfpEnsemble()->getWedgeManager();
  refreshTransceiversWithRetry();

  auto cabledTransceiverIds =
      utility::getCabledPortTranceivers(getHwQsfpEnsemble());
  addTestedTransceivers(cabledTransceiverIds);
  auto cabledTransceivers = utility::legacyTransceiverIds(cabledTransceiverIds);
  std::map<int32_t, TransceiverInfo> transceiversInfo;
  wedgeManager->getTransceiversInfo(
      transceiversInfo,
      std::make_unique<std::vector<int32_t>>(cabledTransceivers));

  int cmisCount = 0;
  for (auto tcvrId : cabledTransceivers) {
    auto it = transceiversInfo.find(tcvrId);
    if (it == transceiversInfo.end()) {
      continue;
    }
    auto& tcvrState = *it->second.tcvrState();
    auto mgmtInterface = tcvrState.transceiverManagementInterface().value_or(
        TransceiverManagementInterface::NONE);
    if (mgmtInterface != TransceiverManagementInterface::CMIS) {
      continue;
    }
    if (!TransceiverManager::opticalOrActiveCable(tcvrState)) {
      XLOG(INFO) << "Skipping flat memory transceiver " << tcvrId;
      continue;
    }

    XLOG(INFO) << "Checking CDB MaxDurationWrite for transceiver " << tcvrId;

    auto* transceiverImpl = wedgeManager->getTransceiverImplForTesting(tcvrId);
    ASSERT_NE(transceiverImpl, nullptr)
        << "TransceiverImpl is null for transceiver " << tcvrId;

    CdbCommandBlock cdb;
    cdb.selectCdbPage(transceiverImpl);
    cdb.createCdbCmdGetFwFeatureInfo();
    ASSERT_TRUE(cdb.cmisRunCdbCommand(transceiverImpl))
        << "CDB command 0x0041 failed for transceiver " << tcvrId;

    auto maxDurationWriteUsec = cdb.getMaxDurationWriteUsec();
    XLOG(INFO) << "Transceiver " << tcvrId
               << ": MaxDurationWrite = " << maxDurationWriteUsec << " usec";

    EXPECT_GT(maxDurationWriteUsec, 0)
        << "Transceiver " << tcvrId
        << " MaxDurationWrite is 0 (not advertised or insufficient response)";

    EXPECT_LE(maxDurationWriteUsec, kMaxCdbTimeoutUsec)
        << "Transceiver " << tcvrId << " MaxDurationWrite ("
        << maxDurationWriteUsec << " usec) exceeds kMaxCdbTimeoutUsec ("
        << kMaxCdbTimeoutUsec << " usec = 120s)";
    cmisCount++;
  }
  XLOG(INFO) << "Checked " << cmisCount << " CMIS transceivers";
}

} // namespace facebook::fboss
