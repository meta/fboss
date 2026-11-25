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

#include "fboss/agent/AgentConfig.h"

#include <folly/logging/xlog.h>
#include "fboss/lib/CommonUtils.h"
#include "fboss/qsfp_service/module/QsfpModule.h"
#include "fboss/qsfp_service/module/cmis/CmisFieldInfo.h"
#include "fboss/qsfp_service/test/hw_test/HwPortUtils.h"
#include "fboss/qsfp_service/test/hw_test/HwQsfpEnsemble.h"
#include "fboss/qsfp_service/test/hw_test/HwTransceiverTest.h"
#include "thrift/lib/cpp/util/EnumUtils.h"

constexpr static auto kMaxRefreshesForReadyState = 5;

namespace facebook::fboss {

class HwTransceiverResetTest : public HwTransceiverTest {
 public:
  // Mark port down to allow tcvr removal later when issuing hard reset
  HwTransceiverResetTest() : HwTransceiverTest(false) {}

  void resetAllTransceiversAndWaitForStable(
      const std::map<int32_t, TransceiverInfo>& transceivers) {
    auto wedgeManager = getHwQsfpEnsemble()->getWedgeManager();
    std::vector<int32_t> hardresetTcvrs;
    // Reset all transceivers
    for (auto idAndTransceiver : transceivers) {
      wedgeManager->triggerQsfpHardReset(idAndTransceiver.first);
      hardresetTcvrs.push_back(idAndTransceiver.first);
    }
    // Clear reset on all transceivers
    wedgeManager->clearAllTransceiverReset();
    XLOG(INFO) << "Trigger QSFP Hard reset for tranceivers: ["
               << folly::join(",", hardresetTcvrs) << "]";

    if (FLAGS_port_manager_mode) {
      getHwQsfpEnsemble()
          ->getQsfpServiceHandler()
          ->setOverrideAgentPortStatusForTesting(
              isPortUp_ /* up */, true /* enabled */);
    }

    // Tunable optics take ~100s for datapath activation. Thus wait sufficient
    // time for the transceivers to complete transceiver programming state
    waitTillCabledTcvrProgrammed(30 /* numRetries */, isPortUp_ /* portUp */);
  }

  // Verify that the transceiver's module state is correct based on the
  // expectInReset flag.
  // If expectInReset == true, verifies that module state is
  // UNKNOWN
  // If expectInReset == false, verifies that module state is NOT
  // UNKNOWN
  bool verifyCmisModuleState(int32_t cmisTcvrID, bool expectInReset) {
    auto wedgeManager = getHwQsfpEnsemble()->getWedgeManager();
    ReadRequest request;
    request.ids() = std::vector<int32_t>{cmisTcvrID};
    TransceiverIOParameters params;
    params.offset() = 3; // ModuleStatus register in CMIS
    request.parameter() = params;
    std::map<int32_t, ReadResponse> currentResponse;
    std::unique_ptr<ReadRequest> readRequest =
        std::make_unique<ReadRequest>(request);
    wedgeManager->readTransceiverRegister(
        currentResponse, std::move(readRequest));
    if (currentResponse.find(cmisTcvrID) == currentResponse.end()) {
      return false;
    }
    auto curr = currentResponse[cmisTcvrID];
    if (!folly::copy(curr.valid().value())) {
      return false;
    }
    auto moduleState = (CmisModuleState)((curr.data().value().data()[0] &
                                          MODULE_STATUS_MASK) >>
                                         MODULE_STATUS_BITSHIFT);

    if (expectInReset && moduleState != CmisModuleState::UNKNOWN) {
      return false;
    }
    if (!expectInReset && moduleState == CmisModuleState::UNKNOWN) {
      return false;
    }

    return true;
  }
};

TEST_F(HwTransceiverResetTest, resetTranscieverAndDetectPresence) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::TRANSCEIVER_RESET});
  addTestedTransceivers(getExpectedTransceivers());
  // Validate that the power control register has been correctly set before we
  // begin resetting the modules
  auto wedgeManager = getHwQsfpEnsemble()->getWedgeManager();
  std::map<int32_t, TransceiverInfo> transceivers;
  wedgeManager->getTransceiversInfo(
      transceivers, getExpectedLegacyTransceiverIds());
  for (auto idAndTransceiver : transceivers) {
    auto& tcvrState =
        apache::thrift::can_throw(*idAndTransceiver.second.tcvrState());
    if (*tcvrState.present()) {
      XLOG(INFO) << "Transceiver:" << idAndTransceiver.first
                 << " before hard reset, power control="
                 << apache::thrift::util::enumNameSafe(
                        *tcvrState.settings()->powerControl());
      auto transmitterTech = *tcvrState.cable().value_or({}).transmitterTech();
      auto mgmtInterface = apache::thrift::can_throw(
          *tcvrState.transceiverManagementInterface());
      if (transmitterTech == TransmitterTechnology::COPPER) {
        if (mgmtInterface == TransceiverManagementInterface::CMIS) {
          EXPECT_TRUE(
              *tcvrState.settings()->powerControl() ==
              PowerControlState::HIGH_POWER_OVERRIDE);
        } else {
          EXPECT_TRUE(
              *tcvrState.settings()->powerControl() ==
              PowerControlState::POWER_SET_BY_HW);
        }
      } else if (transmitterTech == TransmitterTechnology::OPTICAL) {
        // Only optical modules get a power override (customizationSupported).
        EXPECT_TRUE(
            *tcvrState.settings()->powerControl() ==
                PowerControlState::POWER_OVERRIDE ||
            *tcvrState.settings()->powerControl() ==
                PowerControlState::HIGH_POWER_OVERRIDE);
      }
    }
  }

  resetAllTransceiversAndWaitForStable(transceivers);

  std::map<int32_t, TransceiverInfo> transceiversAfterReset;
  wedgeManager->getTransceiversInfo(
      transceiversAfterReset, getExpectedLegacyTransceiverIds());
  // Assert that we can detect all transceivers again
  for (auto idAndTransceiver : transceivers) {
    auto& tcvrState =
        apache::thrift::can_throw(*idAndTransceiver.second.tcvrState());
    if (*tcvrState.present()) {
      XLOG(DBG2) << "Checking that transceiver : " << idAndTransceiver.first
                 << " was detected after reset";
      auto titr = transceiversAfterReset.find(idAndTransceiver.first);
      EXPECT_TRUE(titr != transceiversAfterReset.end());
      EXPECT_TRUE(*titr->second.tcvrState()->present());

      XLOG(INFO) << "Transceiver:" << idAndTransceiver.first
                 << " before hard reset, power control="
                 << apache::thrift::util::enumNameSafe(
                        *tcvrState.settings()->powerControl())
                 << ", after hard reset, power control="
                 << apache::thrift::util::enumNameSafe(
                        *titr->second.tcvrState()->settings()->powerControl());
      EXPECT_EQ(
          *tcvrState.settings()->powerControl(),
          *titr->second.tcvrState()->settings()->powerControl());
    }
  }
}

TEST_F(HwTransceiverResetTest, resetTranscieverAndDetectStateChanged) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::TRANSCEIVER_RESET});
  addTestedTransceivers(getExpectedTransceivers());
  std::map<int32_t, TransceiverInfo> transceivers;
  std::map<int32_t, ModuleStatus> moduleStatuses;

  auto wedgeManager = getHwQsfpEnsemble()->getWedgeManager();
  WITH_RETRIES_N(kMaxRefreshesForReadyState, {
    refreshTransceiversWithRetry();
    transceivers.clear();
    wedgeManager->getTransceiversInfo(
        transceivers, getExpectedLegacyTransceiverIds());
    EXPECT_EVENTUALLY_GT(transceivers.size(), 0);

    for (auto idAndTransceiver : transceivers) {
      auto& tcvrState =
          apache::thrift::can_throw(*idAndTransceiver.second.tcvrState());
      if (*tcvrState.present()) {
        auto mgmtInterface = tcvrState.transceiverManagementInterface();
        CHECK(mgmtInterface);
        if (mgmtInterface == TransceiverManagementInterface::CMIS) {
          auto state = tcvrState.status()->cmisModuleState();
          CHECK(state);
          EXPECT_EVENTUALLY_EQ(state, CmisModuleState::READY)
              << "Cmis module not ready on tcvr " << idAndTransceiver.first;
        }
      }
    }
  });

  // clear existing module status flags from previous refreshes
  wedgeManager->getAndClearTransceiversModuleStatus(
      moduleStatuses, getExpectedLegacyTransceiverIds());
  auto transceiverIds = refreshTransceiversWithRetry();
  EXPECT_TRUE(
      utility::containsSubset(transceiverIds, getExpectedTransceivers()));

  transceivers.clear();
  wedgeManager->getTransceiversInfo(
      transceivers, getExpectedLegacyTransceiverIds());
  // get the module status flags from the previous refresh
  moduleStatuses.clear();
  wedgeManager->getAndClearTransceiversModuleStatus(
      moduleStatuses, getExpectedLegacyTransceiverIds());

  // Validate that cmisStateChanged is 0 before running the test
  // TODO: also add check for SFF modules' PowerControl field
  for (auto idAndTransceiver : transceivers) {
    auto& tcvrState =
        apache::thrift::can_throw(*idAndTransceiver.second.tcvrState());
    if (*tcvrState.present()) {
      auto mgmtInterface = tcvrState.transceiverManagementInterface();
      CHECK(mgmtInterface);
      if (*mgmtInterface == TransceiverManagementInterface::SFF ||
          *mgmtInterface == TransceiverManagementInterface::SFF8472) {
      } else if (*mgmtInterface == TransceiverManagementInterface::CMIS) {
        auto status = moduleStatuses[idAndTransceiver.first];
        auto stateChanged = status.cmisStateChanged();
        CHECK(stateChanged);
        EXPECT_FALSE(*stateChanged)
            << "Wrong stateChanged on module " << idAndTransceiver.first;
      } else {
        throw FbossError(
            "Invalid transceiver type: ",
            apache::thrift::util::enumNameSafe(*mgmtInterface));
      }
    }
  }

  resetAllTransceiversAndWaitForStable(transceivers);

  std::map<int32_t, TransceiverInfo> transceiversAfterReset;
  wedgeManager->getTransceiversInfo(
      transceiversAfterReset, getExpectedLegacyTransceiverIds());
  moduleStatuses.clear();
  wedgeManager->getAndClearTransceiversModuleStatus(
      moduleStatuses, getExpectedLegacyTransceiverIds());

  // Check that CMIS modules have cmisStateChanged flag set after reset
  // TODO: also add check for SFF modules' PowerControl field
  for (auto idAndTransceiver : transceivers) {
    auto& tcvrState =
        apache::thrift::can_throw(*idAndTransceiver.second.tcvrState());
    if (*tcvrState.present()) {
      auto titr = transceiversAfterReset.find(idAndTransceiver.first);
      EXPECT_TRUE(titr != transceiversAfterReset.end());
      auto& tcvrStateAfterReset =
          apache::thrift::can_throw(*titr->second.tcvrState());
      auto mgmtInterface = tcvrStateAfterReset.transceiverManagementInterface();
      // TODO: T236126124 figure out why the test is flakey for AEC cables. see
      // D78320942
      auto transmitterTech =
          *tcvrStateAfterReset.cable().value_or({}).transmitterTech();
      CHECK(mgmtInterface);
      if (*mgmtInterface == TransceiverManagementInterface::SFF ||
          *mgmtInterface == TransceiverManagementInterface::SFF8472) {
      } else if (*mgmtInterface == TransceiverManagementInterface::CMIS) {
        auto status = moduleStatuses[idAndTransceiver.first];
        auto stateChanged = status.cmisStateChanged();
        CHECK(stateChanged);
        // Non Active copper cables don't set the state changed flag
        if (!TransceiverManager::activeCable(tcvrState)) {
          EXPECT_TRUE(
              *stateChanged ==
              (transmitterTech != TransmitterTechnology::COPPER))
              << " Failed comparison for transceiver "
              << idAndTransceiver.first;
        }
      } else {
        throw FbossError(
            "Invalid transceiver type: ",
            apache::thrift::util::enumNameSafe(*mgmtInterface));
      }
    }
  }
}

class HwTransceiverResetBmcLiteTest : public HwTransceiverResetTest {
  std::vector<qsfp_production_features::QsfpProductionFeature>
  getProductionFeatures() const override {
    return {qsfp_production_features::QsfpProductionFeature::BMC_LITE};
  }
};

TEST_F(HwTransceiverResetBmcLiteTest, verifyResetControl) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::TRANSCEIVER_RESET});
  // 1. Put the transceivers in reset one at a time.
  // 2. Verify absence of Transceiver. Read byte 0 10 times and ensure
  //    - for sff, all reads fail
  //    - for cmis, moduleStatus is unknown.
  // 3. Verify read to all other transceivers is successful
  // 4. Release reset, check presence of transceiver.
  // 5. Verify all other transceivers still respond to IO
  // 6. Wait up to 10 seconds for the transceiver in step 4 to start responding
  // to IO
  // 7. Repeat 1-6 for all transceivers

  auto wedgeManager = getHwQsfpEnsemble()->getWedgeManager();
  // Only work with optical transceivers
  auto opticalTransceivers = getCabledOpticalAndActiveTransceiverIDs();
  addTestedTransceiverIds(opticalTransceivers);

  EXPECT_TRUE(!opticalTransceivers.empty());

  std::map<int32_t, TransceiverInfo> transceivers;
  wedgeManager->getTransceiversInfo(
      transceivers,
      std::make_unique<std::vector<int32_t>>(opticalTransceivers));

  std::vector<int32_t> cmisTransceivers;
  for (auto idAndTransceiver : transceivers) {
    if (*idAndTransceiver.second.tcvrState()
             ->transceiverManagementInterface() ==
        TransceiverManagementInterface::CMIS) {
      cmisTransceivers.push_back(idAndTransceiver.first);
    }
  }

  auto readAndVerifyByte0 = [&, wedgeManager](int tcvrID, bool valid) {
    ReadRequest request;
    request.ids() = {tcvrID};
    TransceiverIOParameters params;
    params.offset() = 0;
    request.parameter() = params;
    std::map<int32_t, ReadResponse> currentResponse;
    std::unique_ptr<ReadRequest> readRequest =
        std::make_unique<ReadRequest>(request);
    wedgeManager->readTransceiverRegister(
        currentResponse, std::move(readRequest));
    EXPECT_TRUE(currentResponse.find(tcvrID) != currentResponse.end());
    auto curr = currentResponse[tcvrID];

    return *curr.valid() == valid;
  };

  auto isCmis = [&cmisTransceivers](int32_t tcvrID) {
    return std::find(
               cmisTransceivers.begin(), cmisTransceivers.end(), tcvrID) !=
        cmisTransceivers.end();
  };

  // Lambda to test a reset function and a clear reset function
  // for WedgeManager.
  auto verifyResetAndClearFunctions =
      [&](void (TransceiverManager::*resetFunc)(int),
          void (TransceiverManager::*clearResetFunc)(int)) {
        for (auto tcvrID : opticalTransceivers) {
          XLOG(INFO) << "Testing transceiver (0-indexed): " << tcvrID;

          // Expect presence before reset.
          auto info = wedgeManager->getTransceiverInfo(TransceiverID(tcvrID));
          EXPECT_TRUE(*info.tcvrState().value().present());

          // 1. Put transceiver in reset
          (wedgeManager->*resetFunc)(tcvrID);
          XLOG(INFO) << "Transceiver (0-indexed): " << tcvrID
                     << " should be in reset";

          // 2.Refresh transceivers and check that the presence is false.
          // Do IO and verify it fails on above transceiver
          // For SFF, failure means IO fails when transceiver is in reset
          // For CMIS, failure means module state is unknown when transceiver is
          // in reset or the IO fails

          refreshTransceiversWithRetry();
          info = wedgeManager->getTransceiverInfo(TransceiverID(tcvrID));
          EXPECT_FALSE(*info.tcvrState().value().present());

          WITH_RETRIES_N_TIMED(
              10 /* retries */,
              std::chrono::milliseconds(1000) /* msBetweenRetry */,
              {
                if (isCmis(tcvrID)) {
                  EXPECT_EVENTUALLY_TRUE(
                      readAndVerifyByte0(tcvrID, false /* valid */) ||
                      verifyCmisModuleState(tcvrID, true /* expectInReset */));
                } else {
                  EXPECT_EVENTUALLY_TRUE(
                      readAndVerifyByte0(tcvrID, false /* valid */));
                }
              });

          // 3. Verify presence and read to all other transceivers is successful
          for (auto otherTcvrID : opticalTransceivers) {
            if (otherTcvrID == tcvrID) {
              // Skip the tcvrID in test
              continue;
            }
            EXPECT_TRUE(readAndVerifyByte0(otherTcvrID, true /* valid */));
            if (isCmis(otherTcvrID)) {
              verifyCmisModuleState(otherTcvrID, false /* expectInReset */);
            }
            info = wedgeManager->getTransceiverInfo(TransceiverID(otherTcvrID));
            EXPECT_TRUE(*info.tcvrState().value().present());
          }

          // 4. Undo reset to put transceiver back in normal operation
          // Refresh transceivers to update the presence map.
          (wedgeManager->*clearResetFunc)(tcvrID);
          XLOG(INFO) << "Transceiver (0-indexed): " << tcvrID
                     << " should be out of reset";

          refreshTransceiversWithRetry();
          info = wedgeManager->getTransceiverInfo(TransceiverID(tcvrID));
          EXPECT_TRUE(*info.tcvrState().value().present());

          // 5. Verify all other transceivers are present and respond to IO.
          for (auto otherTcvrID : opticalTransceivers) {
            if (otherTcvrID == tcvrID) {
              // Skip the tcvrID in test
              continue;
            }
            EXPECT_TRUE(readAndVerifyByte0(otherTcvrID, true /* valid */));
            if (isCmis(otherTcvrID)) {
              verifyCmisModuleState(otherTcvrID, false /* expectInReset */);
            }
            info = wedgeManager->getTransceiverInfo(TransceiverID(otherTcvrID));
            EXPECT_TRUE(*info.tcvrState().value().present());
          }

          // 6. Wait up to 10 seconds for transceiver to start responding
          WITH_RETRIES_N_TIMED(
              10 /* retries */,
              std::chrono::milliseconds(1000) /* msBetweenRetry */,
              {
                if (isCmis(tcvrID)) {
                  EXPECT_EVENTUALLY_TRUE(
                      readAndVerifyByte0(tcvrID, true /* valid */) &&
                      verifyCmisModuleState(tcvrID, false /* expectInReset */));
                } else {
                  EXPECT_EVENTUALLY_TRUE(
                      readAndVerifyByte0(tcvrID, true /* valid */));
                }
              });
        }
      };

  // Test holding Transceiver reset then releasing reset works.
  verifyResetAndClearFunctions(
      &TransceiverManager::holdTransceiverReset,
      &TransceiverManager::releaseTransceiverReset);

  // Test holding Transceiver reset then triggering hard reset works.
  verifyResetAndClearFunctions(
      &TransceiverManager::holdTransceiverReset,
      &TransceiverManager::triggerQsfpHardReset);
}

TEST_F(HwTransceiverResetTest, verifyHardResetAction) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::TRANSCEIVER_RESET});
  // Bring the ports UP, resetTransceiver with a reset_and_clear action, and
  // ensure transceivers come back to ACTIVE state
  addTestedTransceivers(getExpectedTransceivers());

  auto wedgeManager = getHwQsfpEnsemble()->getWedgeManager();
  auto qsfpServiceHandler = getHwQsfpEnsemble()->getQsfpServiceHandler();

  XLOG(INFO) << "Step 1. Bring the ports Up";
  // By default the modules are in TransceiverProgrammed state. Bring the ports
  // up now
  qsfpServiceHandler->setOverrideAgentPortStatusForTesting(
      true /* up */, true /* enabled */);
  qsfpServiceHandler->refreshStateMachines();

  XLOG(INFO) << "Step 2. Confirm transceivers are in ACTIVE state";
  for (const auto& id : getExpectedTransceivers()) {
    auto curState = wedgeManager->getCurrentState(id);
    auto expectedState = FLAGS_port_manager_mode
        ? TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED
        : TransceiverStateMachineState::ACTIVE;
    ASSERT_EQ(curState, expectedState)
        << "Transceiver:" << id
        << " Actual: " << apache::thrift::util::enumNameSafe(curState)
        << ", Expected: " << apache::thrift::util::enumNameSafe(expectedState);

    if (FLAGS_port_manager_mode) {
      const auto programmedPortToPortInfo =
          wedgeManager->getProgrammedIphyPortToPortInfo(id);
      for (const auto& [portId, portInfo] : programmedPortToPortInfo) {
        auto curPortState =
            qsfpServiceHandler->getPortManager()->getPortState(portId);
        auto expectedPortState = PortStateMachineState::PORT_UP;
        ASSERT_EQ(curPortState, expectedPortState)
            << "Port:" << portId
            << " Actual: " << apache::thrift::util::enumNameSafe(curPortState)
            << ", Expected: "
            << apache::thrift::util::enumNameSafe(expectedPortState);
      }
    }
  }

  // Trigger Hard Reset
  XLOG(INFO) << "Step 3. Hard reset the ports associated with transceivers";

  std::vector<std::string> portNames;
  for (auto tcvrId : getExpectedTransceivers()) {
    auto portsSet = wedgeManager->getPortNames(tcvrId);
    std::copy(portsSet.begin(), portsSet.end(), std::back_inserter(portNames));
  }
  wedgeManager->resetTransceiver(
      std::make_unique<std::vector<std::string>>(portNames),
      ResetType::HARD_RESET,
      ResetAction::RESET_THEN_CLEAR);

  // Transceivers should be in NOT_PRESENT state because they were just reset
  XLOG(INFO) << "Step 4. Confirm transceivers are in not_present state";
  for (auto id : getExpectedTransceivers()) {
    auto curState = wedgeManager->getCurrentState(id);
    auto expectedState = TransceiverStateMachineState::NOT_PRESENT;
    ASSERT_EQ(curState, expectedState)
        << "Transceiver:" << id
        << " Actual: " << apache::thrift::util::enumNameSafe(curState)
        << ", Expected: " << apache::thrift::util::enumNameSafe(expectedState);
  }

  // In PortManager mode, we need to bring ports down in order for ports to be
  // able to reset. This is to make sure we don't trigger programming while the
  // port is still up according to agent (this is useful in the case of i2c read
  // errors for present transceivers).
  if (FLAGS_port_manager_mode) {
    // Mocking agent bringing ports down to enable programming sequence.
    qsfpServiceHandler->setOverrideAgentPortStatusForTesting(
        false /* up */, true /* enabled */);

    // Refreshing to ensure ports reset to initialized based on transceiver
    // NOT_PRESENT status from hard reset.
    qsfpServiceHandler->refreshStateMachines();
    qsfpServiceHandler->refreshStateMachines();
  }

  XLOG(INFO)
      << "Step 5. Wait till the transceivers go back to programmed state";
  // Clear port status to stop till we only reach programming state. Otherwise
  // when there are i2c errors on any transceiver, that transceiver will lag
  // behind but the refresh will take rest of them to active and
  // waitTillCabledTcvrProgrammed will fail
  qsfpServiceHandler->setOverrideAgentPortStatusForTesting(
      true /* up */, true /* enabled */, true /* clearOnly */);
  if (FLAGS_port_manager_mode) {
    qsfpServiceHandler->setOverrideAgentPortStatusForTesting(
        true /* up */, true /* enabled */, false /* clearOnly */);
  }

  waitTillCabledTcvrProgrammed();
}
} // namespace facebook::fboss
