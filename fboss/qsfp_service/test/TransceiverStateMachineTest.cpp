/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <string>
#include "fboss/qsfp_service/test/TransceiverManagerTestHelper.h"

#include "fboss/qsfp_service/TransceiverStateMachine.h"
#include "fboss/qsfp_service/module/cmis/CmisModule.h"
#include "fboss/qsfp_service/module/tests/FakeTransceiverImpl.h"
#include "fboss/qsfp_service/module/tests/MockSffModule.h"
#include "fboss/qsfp_service/test/MockTransceivers.h"
#include "fboss/qsfp_service/test/hw_test/HwTransceiverUtils.h"

namespace facebook::fboss {

namespace {
const auto kWarmBootAgentConfigChangedFnStr =
    "Triggering Agent config changed w/ warm boot";
const auto kColdBootAgentConfigChangedFnStr =
    "Triggering Agent config changed w/ cold boot";
const auto kRemediateTransceiverFnStr = "Remediating transceiver";
const auto kUpgradeTransceiverFnStr = "Upgrading transceiver firmware";
} // namespace

/*
 * The recommended way to use TransceiverStateMachineTest to verify the event:
 * 1) Call getAllStates() to get all the states of state machine
 * 2) Call verifyStateMachine() with only the supported states to check the
 *    logic of processing a specified event. NOTE: this function will also
 *    erase the supported states from the input `states`
 * 3) Finally call verifyStateUnchanged() to check the rest of states still in
 *    the original state set to make sure even the code tries to process the
 *    specified event, these states will still stay the same.
 */
class TransceiverStateMachineTest : public TransceiverManagerTestHelper {
 public:
  void SetUp() override {
    gflags::SetCommandLineOptionWithMode(
        "enable_snapshot_debugs", "0", gflags::SET_FLAGS_DEFAULT);
    gflags::SetCommandLineOptionWithMode(
        "cdb_command_timeout_usec", "1", gflags::SET_FLAGS_DEFAULT);
    TransceiverManagerTestHelper::SetUp();
  }

  enum class TransceiverType {
    CMIS,
    MOCK_CMIS,
    MOCK_SFF,
    MOCK_SFF8472,
  };
  std::string kPortName1 = "eth1/1/1";
  std::string kPortName5 = "eth1/1/5";
  QsfpModule* overrideTransceiver(
      bool multiPort,
      TransceiverType type = TransceiverType::CMIS) {
    // Set port status to DOWN so that we can remove the transceiver correctly
    updateTransceiverActiveState(false /* up */, true /* enabled */, portId1_);
    if (multiPort) {
      updateTransceiverActiveState(
          false /* up */, true /* enabled */, portId5_);
    }

    auto overrideCmisModule = [this, multiPort](bool isMock) {
      // This override function use ids starting from 1
      transceiverManager_->overrideMgmtInterface(
          static_cast<int>(id_) + 1,
          uint8_t(TransceiverModuleIdentifier::QSFP_PLUS_CMIS));
      if (isMock) {
        XLOG(INFO) << "Making Mock CMIS QSFP for " << id_;
        cmisQsfpImpls_.push_back(
            std::make_unique<MockCmisTransceiverImpl>(
                id_, transceiverManager_.get()));
        EXPECT_CALL(*cmisQsfpImpls_.back().get(), detectTransceiver())
            .WillRepeatedly(::testing::Return(true));
        return transceiverManager_->overrideTransceiverForTesting(
            id_,
            std::make_unique<MockCmisModule>(
                transceiverManager_->getPortNames(id_),
                cmisQsfpImpls_.back().get(),
                tcvrConfig_,
                transceiverManager_->getTransceiverName(id_)));
      } else {
        XLOG(INFO) << "Making CMIS QSFP for " << id_;
        std::unique_ptr<FakeTransceiverImpl> xcvrImpl;
        if (multiPort) {
          qsfpImpls_.push_back(
              std::make_unique<Cmis2x400GFr4Transceiver>(
                  id_, transceiverManager_.get()));
        } else {
          qsfpImpls_.push_back(
              std::make_unique<Cmis200GTransceiver>(
                  id_, transceiverManager_.get()));
        }
        return transceiverManager_->overrideTransceiverForTesting(
            id_,
            std::make_unique<CmisModule>(
                transceiverManager_->getPortNames(id_),
                qsfpImpls_.back().get(),
                tcvrConfig_,
                true /*supportRemediate*/,
                transceiverManager_->getTransceiverName(id_)));
      }
    };

    auto overrideSffModule = [this]() {
      qsfpImpls_.push_back(
          std::make_unique<SffCwdm4Transceiver>(
              id_, transceiverManager_.get()));
      // This override function use ids starting from 1
      transceiverManager_->overrideMgmtInterface(
          static_cast<int>(id_) + 1,
          uint8_t(TransceiverModuleIdentifier::QSFP));
      XLOG(INFO) << "Making Mock SFF QSFP for " << id_;
      auto xcvr = transceiverManager_->overrideTransceiverForTesting(
          id_,
          std::make_unique<MockSffModule>(
              transceiverManager_->getPortNames(id_),
              qsfpImpls_.back().get(),
              tcvrConfig_,
              transceiverManager_->getTransceiverName(id_)));
      return xcvr;
    };

    auto overrideSff8472Module = [this]() {
      transceiverManager_->overrideMgmtInterface(
          static_cast<int>(id_) + 1,
          uint8_t(TransceiverModuleIdentifier::SFP_PLUS));
      XLOG(INFO) << "Making Mock SFF8472 SFP for " << id_;
      sff8472QsfpImpls_.push_back(
          std::make_unique<MockSff8472TransceiverImpl>(
              id_, transceiverManager_.get()));
      EXPECT_CALL(*sff8472QsfpImpls_.back().get(), detectTransceiver())
          .WillRepeatedly(::testing::Return(true));
      return transceiverManager_->overrideTransceiverForTesting(
          id_,
          std::make_unique<MockSff8472Module>(
              transceiverManager_->getPortNames(id_),
              sff8472QsfpImpls_.back().get(),
              transceiverManager_->getTransceiverName(id_)));
    };

    Transceiver* xcvr;
    switch (type) {
      case TransceiverType::CMIS:
        xcvr = overrideCmisModule(false);
        break;
      case TransceiverType::MOCK_CMIS:
        xcvr = overrideCmisModule(true);
        break;
      case TransceiverType::MOCK_SFF:
        xcvr = overrideSffModule();
        break;
      case TransceiverType::MOCK_SFF8472:
        xcvr = overrideSff8472Module();
        break;
    }
    EXPECT_TRUE(xcvr->getLastDownTime() != 0);

    // Remove the override config we set before
    transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
        emptyOverrideTcvrToPortAndProfile_);

    auto qsfpModuleXcvr = static_cast<QsfpModule*>(xcvr);
    // Assign a default transceiver info so that we can set expectations based
    // on it even when transceiverInfo hasn't been updated for the first time
    *(qsfpModuleXcvr->info_.wlock()) = TransceiverInfo{};
    return qsfpModuleXcvr;
  }

  void setState(TransceiverStateMachineState state, bool multiPort) {
    // Pause remediation
    transceiverManager_->setPauseRemediation(60, nullptr);
    enableTransceiverFirmwareUpgradeTesting(false);

    auto discoverTransceiverFn = [this]() {
      // One refresh can finish discoverring xcvr after detectPresence
      transceiverManager_->refreshStateMachines();
    };
    auto programIphy = [this, multiPort]() {
      if (multiPort) {
        transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
            overrideMultiPortTcvrToPortAndProfile_);
      } else {
        transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
            overrideTcvrToPortAndProfile_);
      }

      // refreshStateMachines() will first refresh all transceivers to bring
      // them to DISCOVERED state, and then program the iphy ports
      transceiverManager_->refreshStateMachines();
    };
    auto programIphyAndXcvr = [this, multiPort]() {
      if (multiPort) {
        transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
            overrideMultiPortTcvrToPortAndProfile_);
      } else {
        transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
            overrideTcvrToPortAndProfile_);
      }
      transceiverManager_->refreshStateMachines();
      // Because MockWedgeManager doesn't have PhyManager, so this second
      // refreshStateMachines() will actually trigger xcvr ready for prog
      transceiverManager_->refreshStateMachines();
    };
    auto setXcvtActiveState = [this, &programIphyAndXcvr](bool portUp) {
      programIphyAndXcvr();
      // Extra refreshes may be needed if the module requires a power mode
      // transition (e.g. 2x400G FR4 going from LOW to HIGH power)
      transceiverManager_->refreshStateMachines();
      if (transceiverManager_->getCurrentState(id_) !=
          TransceiverStateMachineState::TRANSCEIVER_READY) {
        transceiverManager_->refreshStateMachines();
      }
      transceiverManager_->refreshStateMachines();
      transceiverManager_->setOverrideAgentPortStatusForTesting(
          portUp /* up */, true /* enabled */, false /* clearOnly */);
      transceiverManager_->refreshStateMachines();
    };

    switch (state) {
      case TransceiverStateMachineState::NOT_PRESENT:
        // default state is always NOT_PRESENT.
        break;
      case TransceiverStateMachineState::PRESENT:
        // Because we want to verify two events: DETECT_TRANSCEIVER and
        // READ_EEPROM separately, we have to make sure we updateQsfpData with
        // allPages=True after `DETECT_TRANSCEIVER` but before `READ_EEPROM`
        // to match the same behavior as QsfpModule::refreshLocked()
        transceiverManager_->updateStateBlocking(
            id_,
            TransceiverStateMachineEvent::TCVR_EV_EVENT_DETECT_TRANSCEIVER);
        xcvr_->updateQsfpData(true);
        break;
      case TransceiverStateMachineState::DISCOVERED:
        discoverTransceiverFn();
        break;
      case TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED:
        programIphy();
        break;
      case TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED:
        programIphy();
        // Use updateStateBlocking() to skip PhyManager check
        transceiverManager_->updateStateBlocking(
            id_, TransceiverStateMachineEvent::TCVR_EV_PROGRAM_XPHY);
        break;
      case TransceiverStateMachineState::TRANSCEIVER_READY:
        programIphyAndXcvr();
        // An extra refresh may be needed if the module requires a power mode
        // transition (e.g. 2x400G FR4 going from LOW to HIGH power)
        if (transceiverManager_->getCurrentState(id_) !=
            TransceiverStateMachineState::TRANSCEIVER_READY) {
          transceiverManager_->refreshStateMachines();
        }
        break;
      case TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED:
        programIphyAndXcvr();
        if (transceiverManager_->getCurrentState(id_) !=
            TransceiverStateMachineState::TRANSCEIVER_READY) {
          transceiverManager_->refreshStateMachines();
        }
        transceiverManager_->updateStateBlocking(
            id_, TransceiverStateMachineEvent::TCVR_EV_PROGRAM_TRANSCEIVER);
        break;
      case TransceiverStateMachineState::ACTIVE:
        setXcvtActiveState(true);
        break;
      case TransceiverStateMachineState::INACTIVE:
        setXcvtActiveState(false);
        break;
      case TransceiverStateMachineState::UPGRADING:
        setXcvtActiveState(false);
        enableTransceiverFirmwareUpgradeTesting(true);
        transceiverManager_->updateStateBlocking(
            id_, TransceiverStateMachineEvent::TCVR_EV_UPGRADE_FIRMWARE);
        break;
    }
    auto curState = transceiverManager_->getCurrentState(id_);
    EXPECT_EQ(curState, state)
        << "Transceiver=0 state doesn't match state expected="
        << apache::thrift::util::enumNameSafe(state)
        << ", actual=" << apache::thrift::util::enumNameSafe(curState);
  }

  std::set<TransceiverStateMachineState> getAllStates() const {
    return {
        TransceiverStateMachineState::NOT_PRESENT,
        TransceiverStateMachineState::PRESENT,
        TransceiverStateMachineState::DISCOVERED,
        TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED,
        TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED,
        TransceiverStateMachineState::TRANSCEIVER_READY,
        TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
        TransceiverStateMachineState::ACTIVE,
        TransceiverStateMachineState::INACTIVE,
        TransceiverStateMachineState::UPGRADING,
    };
  }

  template <
      typename PRE_UPDATE_FN,
      typename STATE_UPDATE_FN,
      typename VERIFY_FN>
  void verifyStateMachine(
      const std::set<TransceiverStateMachineState>& supportedStates,
      TransceiverStateMachineState expectedState,
      std::set<TransceiverStateMachineState>& states,
      PRE_UPDATE_FN preUpdate,
      STATE_UPDATE_FN stateUpdate,
      VERIFY_FN verify,
      bool multiPort,
      TransceiverType type,
      const std::string& updateStr) {
    for (auto preState : supportedStates) {
      if (states.find(preState) == states.end()) {
        // Current state is no longer in the state set, skip checking it
        continue;
      }
      XLOG(INFO) << "Verifying Transceiver=0 state CHANGED by " << updateStr
                 << " from preState="
                 << apache::thrift::util::enumNameSafe(preState)
                 << " to newState="
                 << apache::thrift::util::enumNameSafe(expectedState)
                 << ", multiPort=" << multiPort;
      // Always create a new transceiver so that we can make sure the state
      // can go back to the beginning state
      xcvr_ = overrideTransceiver(multiPort, type);
      setState(preState, multiPort);

      // Call preUpdate() before actual stateUpdate()
      preUpdate();

      // Trigger state update
      stateUpdate();

      // Check current state matches expected state
      auto curState = transceiverManager_->getCurrentState(id_);
      EXPECT_EQ(curState, expectedState)
          << "Transceiver=0 state doesn't match after " << updateStr
          << ", preState=" << apache::thrift::util::enumNameSafe(preState)
          << ", expected new state="
          << apache::thrift::util::enumNameSafe(expectedState)
          << ", actual=" << apache::thrift::util::enumNameSafe(curState);

      // Verify the result after update finishes
      verify();

      // Remove from the state set
      states.erase(preState);
      ::testing::Mock::VerifyAndClearExpectations(transceiverManager_.get());
      ::testing::Mock::VerifyAndClearExpectations(xcvr_);
    }
  }

  template <typename PRE_UPDATE_FN, typename VERIFY_FN>
  void verifyStateMachine(
      const std::set<TransceiverStateMachineState>& supportedStates,
      TransceiverStateMachineEvent event,
      TransceiverStateMachineState expectedState,
      std::set<TransceiverStateMachineState>& states,
      PRE_UPDATE_FN preUpdate,
      VERIFY_FN verify,
      bool multiPort,
      TransceiverType type = TransceiverType::CMIS,
      bool holdTransceiversLockWhileUpdating = false) {
    auto stateUpdateFn = [this, event, holdTransceiversLockWhileUpdating]() {
      if (holdTransceiversLockWhileUpdating) {
        auto lockedTransceivers =
            transceiverManager_->getSynchronizedTransceivers().rlock();
        transceiverManager_->updateStateBlocking(id_, event);
      } else {
        transceiverManager_->updateStateBlocking(id_, event);
      }
    };
    auto stateUpdateFnStr = folly::to<std::string>(
        "Event=", apache::thrift::util::enumNameSafe(event));
    verifyStateMachine(
        supportedStates,
        expectedState,
        states,
        preUpdate,
        stateUpdateFn,
        verify,
        multiPort,
        type,
        stateUpdateFnStr);
  }

  template <
      typename PRE_UPDATE_FN,
      typename STATE_UPDATE_FN,
      typename VERIFY_FN>
  void verifyStateUnchanged(
      std::set<TransceiverStateMachineState>& states,
      PRE_UPDATE_FN preUpdate,
      STATE_UPDATE_FN stateUpdate,
      VERIFY_FN verify,
      bool multiPort,
      TransceiverType type,
      const std::string& updateStr) {
    for (auto state : states) {
      XLOG(INFO) << "Verifying Transceiver=0 State="
                 << apache::thrift::util::enumNameSafe(state)
                 << " UNCHANGED by " << updateStr
                 << ", multiPort=" << multiPort;
      // Always create a new transceiver so that we can make sure the state
      // can go back to the beginning state
      xcvr_ = overrideTransceiver(multiPort, type);
      setState(state, multiPort);

      // Call preUpdate() before actual stateUpdate()
      preUpdate();

      // Trigger state update
      stateUpdate();

      // Check new state doesn't change
      auto newState = transceiverManager_->getCurrentState(id_);
      EXPECT_EQ(newState, state)
          << "Transceiver=0 unchanged state doesn't match after " << updateStr
          << ", preState=" << apache::thrift::util::enumNameSafe(state)
          << ", newState=" << apache::thrift::util::enumNameSafe(newState);

      // Verify the result after update finishes
      verify();

      ::testing::Mock::VerifyAndClearExpectations(transceiverManager_.get());
      ::testing::Mock::VerifyAndClearExpectations(xcvr_);
    }
  }

  template <typename PRE_UPDATE_FN, typename VERIFY_FN>
  void verifyStateUnchanged(
      TransceiverStateMachineEvent event,
      std::set<TransceiverStateMachineState>& states,
      PRE_UPDATE_FN preUpdate,
      VERIFY_FN verify,
      bool multiPort,
      TransceiverType type = TransceiverType::CMIS) {
    auto stateUpdateFn = [this, event]() {
      transceiverManager_->updateStateBlocking(id_, event);
    };
    auto stateUpdateFnStr = folly::to<std::string>(
        "Event=", apache::thrift::util::enumNameSafe(event));
    verifyStateUnchanged(
        states,
        preUpdate,
        stateUpdateFn,
        verify,
        multiPort,
        type,
        stateUpdateFnStr);
  }

  void verifyResetProgrammingAttributes() {
    // Check state machine attributes should be reset to default values
    const auto& stateMachine =
        transceiverManager_->getStateMachineForTesting(id_);
    EXPECT_EQ(
        stateMachine.get_attribute(transceiverMgrPtr),
        transceiverManager_.get());
    EXPECT_EQ(stateMachine.get_attribute(transceiverID), id_);
    EXPECT_FALSE(stateMachine.get_attribute(isIphyProgrammed));
    EXPECT_FALSE(stateMachine.get_attribute(isXphyProgrammed));
    EXPECT_FALSE(stateMachine.get_attribute(isTransceiverProgrammed));
    EXPECT_TRUE(stateMachine.get_attribute(needMarkLastDownTime));
    EXPECT_TRUE(
        transceiverManager_->getProgrammedIphyPortToPortInfo(id_).empty());
  }

  void updateTransceiverActiveState(bool up, bool enabled, PortID portId) {
    PortStatus status;
    status.enabled() = enabled;
    status.up() = up;
    transceiverManager_->updateTransceiverActiveState(
        {id_}, {{portId, status}});
    // Sleep 1s to avoid the state machine handling the event too fast
    /* sleep override */
    sleep(1);
  }

  void setProgramCmisModuleExpectation(bool isProgrammed) {
    ::testing::Sequence s;
    setProgramCmisModuleExpectation(isProgrammed, s);
  }

  void setProgramCmisModuleExpectation(
      bool isProgrammed,
      ::testing::Sequence& s,
      bool multiPort = false) {
    int callTimes = isProgrammed ? 1 : 0;
    MockCmisModule* mockXcvr = static_cast<MockCmisModule*>(xcvr_);
    auto portSpeed = cfg::PortSpeed::HUNDREDG;
    TransceiverPortState state{kPortName1, 0 /* startHostLane */, portSpeed, 4};
    EXPECT_CALL(*mockXcvr, customizeTransceiverLocked(state))
        .Times(callTimes)
        .InSequence(s);
    EXPECT_CALL(*mockXcvr, updateQsfpData(true)).Times(callTimes).InSequence(s);
    if (multiPort) {
      TransceiverPortState state2{
          kPortName5, 4 /* startHostLane */, portSpeed, 4};
      EXPECT_CALL(*mockXcvr, customizeTransceiverLocked(state2))
          .Times(callTimes)
          .InSequence(s);
      EXPECT_CALL(*mockXcvr, updateQsfpData(true))
          .Times(callTimes)
          .InSequence(s);
    }
    EXPECT_CALL(*mockXcvr, configureModule(0 /* startHostLane */))
        .Times(callTimes)
        .InSequence(s);
    if (multiPort) {
      EXPECT_CALL(*mockXcvr, configureModule(4 /* startHostLane */))
          .Times(callTimes)
          .InSequence(s);
    }

    const auto& info = transceiverManager_->getTransceiverInfo(id_);
    if (auto settings = info.tcvrState()->settings()) {
      if (auto hostLaneSettings = settings->hostLaneSettings()) {
        EXPECT_CALL(*mockXcvr, ensureRxOutputSquelchEnabled(*hostLaneSettings))
            .Times(callTimes)
            .InSequence(s);
      }
    }

    // Normal transceiver programming shouldn't trigger resetDataPath()
    EXPECT_CALL(*mockXcvr, resetDataPath(testing::_)).Times(0).InSequence(s);

    EXPECT_CALL(*mockXcvr, updateQsfpData(false))
        .Times(callTimes)
        .InSequence(s);
    ModuleStatus moduleStatus;
    EXPECT_CALL(*mockXcvr, updateCachedTransceiverInfoLocked(moduleStatus))
        .Times(callTimes)
        .InSequence(s);
  }

  void enableRemediationTesting() {
    gflags::SetCommandLineOptionWithMode(
        "initial_remediate_interval", "0", gflags::SET_FLAGS_DEFAULT);
    gflags::SetCommandLineOptionWithMode(
        "remediate_interval", "0", gflags::SET_FLAGS_DEFAULT);
  }

  void enableTransceiverFirmwareUpgradeTesting(bool enable) {
    gflags::SetCommandLineOptionWithMode(
        "firmware_upgrade_supported",
        enable ? "1" : "0",
        gflags::SET_FLAGS_DEFAULT);
    transceiverManager_->setForceFirmwareUpgradeForTesting(enable);
  }

  void triggerAgentConfigChanged(bool isAgentColdBoot) {
    // Override ConfigAppliedInfo
    ConfigAppliedInfo configAppliedInfo;
    auto currentInMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch());
    configAppliedInfo.lastAppliedInMs() = currentInMs.count();
    if (isAgentColdBoot) {
      configAppliedInfo.lastColdbootAppliedInMs() = currentInMs.count();
    }
    transceiverManager_->setOverrideAgentConfigAppliedInfoForTesting(
        configAppliedInfo);

    transceiverManager_->triggerAgentConfigChangeEvent();
  }

  void triggerRemediateEvents() {
    transceiverManager_->triggerRemediateEvents(stableXcvrIds_);
  }

  void triggerFirmwareUpgradeEvents() {
    std::unordered_set<TransceiverID> tcvrs = {id_};
    transceiverManager_->triggerFirmwareUpgradeEvents(tcvrs);
  }

  void setMockCmisPresence(bool isPresent) {
    MockCmisModule* mockXcvr = static_cast<MockCmisModule*>(xcvr_);
    auto xcvrImpl = mockXcvr->getTransceiverImpl();
    EXPECT_CALL(*xcvrImpl, detectTransceiver())
        .WillRepeatedly(::testing::Return(isPresent));
  }

  void cleanup() {
    resetTransceiverManager();
    transceiverManager_->init();
  }

  QsfpModule* xcvr_;
  const TransceiverID id_ = TransceiverID(0);
  const std::vector<TransceiverID> stableXcvrIds_ = {id_};
  const PortID portId1_ = PortID(1);
  const PortID portId5_ = PortID(5);
  const cfg::PortProfileID profile_ =
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_OPTICAL;
  const TransceiverManager::OverrideTcvrToPortAndProfile
      overrideTcvrToPortAndProfile_ = {
          {id_,
           {
               {portId1_, profile_},
           }}};
  const TransceiverManager::OverrideTcvrToPortAndProfile
      overrideMultiPortTcvrToPortAndProfile_ = {
          {id_,
           {
               {portId1_, profile_},
               {portId5_, profile_},
           }}};
  const TransceiverManager::OverrideTcvrToPortAndProfile
      emptyOverrideTcvrToPortAndProfile_ = {};
  std::vector<std::unique_ptr<MockCmisTransceiverImpl>> cmisQsfpImpls_;
  std::vector<std::unique_ptr<MockSff8472TransceiverImpl>> sff8472QsfpImpls_;
};

TEST_F(TransceiverStateMachineTest, defaultState) {
  for (auto multiPort : {false, true}) {
    XLOG(INFO) << "Verifying defaultState for multiPort = " << multiPort;
    overrideTransceiver(false /* multiPort */);
    EXPECT_EQ(
        transceiverManager_->getCurrentState(id_),
        TransceiverStateMachineState::NOT_PRESENT);
    verifyResetProgrammingAttributes();
    // Prepare for testing with next multiPort value
    cleanup();
  }
}

TEST_F(TransceiverStateMachineTest, detectTransceiver) {
  for (auto multiPort : {false, true}) {
    XLOG(INFO) << "Verifying detectTransceiver for multiPort = " << multiPort;
    auto allStates = getAllStates();
    verifyStateMachine(
        {TransceiverStateMachineState::NOT_PRESENT,
         TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED,
         TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED,
         TransceiverStateMachineState::TRANSCEIVER_READY,
         TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
         TransceiverStateMachineState::INACTIVE},
        TransceiverStateMachineEvent::TCVR_EV_EVENT_DETECT_TRANSCEIVER,
        TransceiverStateMachineState::PRESENT /* expected state */,
        allStates,
        []() {} /* preUpdate */,
        []() {} /* verify */,
        multiPort);
    // Other states should not change even though we try to process the event
    verifyStateUnchanged(
        TransceiverStateMachineEvent::TCVR_EV_EVENT_DETECT_TRANSCEIVER,
        allStates,
        []() {} /* preUpdate */,
        []() {} /* verify */,
        multiPort);
    // Prepare for testing with next multiPort value
    cleanup();
  }
}

TEST_F(TransceiverStateMachineTest, readEeprom) {
  for (auto multiPort : {false, true}) {
    XLOG(INFO) << "Verifying readEeprom for multiPort = " << multiPort;
    auto allStates = getAllStates();
    // Only PRESENT can accept READ_EEPROM event
    verifyStateMachine(
        {TransceiverStateMachineState::PRESENT},
        TransceiverStateMachineEvent::TCVR_EV_READ_EEPROM,
        TransceiverStateMachineState::DISCOVERED /* expected state */,
        allStates,
        [this]() {
          // Make sure `discoverTransceiver` has been called
          EXPECT_CALL(*transceiverManager_, verifyEepromChecksumsLocked(id_))
              .Times(1);
        },
        [this]() {
          // Enter DISCOVERED will also call `resetProgrammingAttributes`
          const auto& stateMachine =
              transceiverManager_->getStateMachineForTesting(id_);
          EXPECT_FALSE(stateMachine.get_attribute(isIphyProgrammed));
          EXPECT_FALSE(stateMachine.get_attribute(isXphyProgrammed));
          EXPECT_FALSE(stateMachine.get_attribute(isTransceiverProgrammed));
          EXPECT_TRUE(stateMachine.get_attribute(needMarkLastDownTime));

          // Before fetching TransceiverInfo, make sure we call refresh()
          // to update the cached TransceiverInfo
          xcvr_->refresh();
          const auto& info = transceiverManager_->getTransceiverInfo(id_);
          utility::HwTransceiverUtils::verifyDiagsCapability(
              *info.tcvrState(),
              transceiverManager_->getDiagsCapability(id_),
              false /* skipCheckingIndividualCapability */);
        },
        multiPort,
        TransceiverType::CMIS /* type */,
        true /* holdTransceiversLockWhileUpdating */);
    // Other states should not change even though we try to process the event
    verifyStateUnchanged(
        TransceiverStateMachineEvent::TCVR_EV_READ_EEPROM,
        allStates,
        []() {} /* preUpdate */,
        []() {} /* verify */,
        multiPort);

    // Prepare for testing with next multiPort value
    cleanup();
  }
}

TEST_F(TransceiverStateMachineTest, programIphy) {
  auto allStates = getAllStates();
  // Both NOT_PRESENT and DISCOVERED can accept PROGRAM_IPHY event
  verifyStateMachine(
      {TransceiverStateMachineState::NOT_PRESENT,
       TransceiverStateMachineState::DISCOVERED},
      TransceiverStateMachineEvent::TCVR_EV_PROGRAM_IPHY,
      TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED /* expected state */,
      allStates,
      [this]() {
        transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
            overrideTcvrToPortAndProfile_);
      },
      [this]() {
        const auto& stateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        // Now isIphyProgrammed should be true
        EXPECT_TRUE(stateMachine.get_attribute(isIphyProgrammed));
        EXPECT_FALSE(stateMachine.get_attribute(isXphyProgrammed));
        EXPECT_FALSE(stateMachine.get_attribute(isTransceiverProgrammed));
        EXPECT_TRUE(stateMachine.get_attribute(needMarkLastDownTime));

        // checked programmed iphy ports match overrideTcvrToPortAndProfile_
        for (const auto& xcvrToPortAndProfile : overrideTcvrToPortAndProfile_) {
          const auto& programmedIphyPorts =
              transceiverManager_->getProgrammedIphyPortToPortInfo(
                  xcvrToPortAndProfile.first);
          EXPECT_EQ(
              xcvrToPortAndProfile.second.size(), programmedIphyPorts.size());
          for (auto portToProfile : xcvrToPortAndProfile.second) {
            const auto& it = programmedIphyPorts.find(portToProfile.first);
            EXPECT_TRUE(it != programmedIphyPorts.end());
            EXPECT_EQ(portToProfile.second, it->second.profile);
          }
        }
      },
      false /* multiPort */);
  // Other states should not change even though we try to process the event
  verifyStateUnchanged(
      TransceiverStateMachineEvent::TCVR_EV_PROGRAM_IPHY,
      allStates,
      []() {} /* preUpdate */,
      []() {} /* verify */,
      false /* multiPort */);
}

TEST_F(TransceiverStateMachineTest, programIphyFailed) {
  std::set<TransceiverStateMachineState> stateSet = {
      TransceiverStateMachineState::NOT_PRESENT,
      TransceiverStateMachineState::DISCOVERED};
  // If we never set overrideTcvrToPortAndProfile_, programming iphy won't work
  verifyStateUnchanged(
      TransceiverStateMachineEvent::TCVR_EV_PROGRAM_IPHY,
      stateSet,
      []() {} /* preUpdate */,
      [this]() {
        const auto& stateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        // Now isIphyProgrammed should still be false
        EXPECT_FALSE(stateMachine.get_attribute(isIphyProgrammed));
        EXPECT_FALSE(stateMachine.get_attribute(isXphyProgrammed));
        EXPECT_FALSE(stateMachine.get_attribute(isTransceiverProgrammed));
        EXPECT_TRUE(stateMachine.get_attribute(needMarkLastDownTime));

        // checked programmed iphy ports
        const auto& programmedIphyPorts =
            transceiverManager_->getProgrammedIphyPortToPortInfo(id_);
        EXPECT_TRUE(programmedIphyPorts.empty());

        // Now set the override transceiver to port and profile to make
        // programming iphy ports work
        transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
            overrideTcvrToPortAndProfile_);
        // Then try again, it should succeed
        transceiverManager_->updateStateBlocking(
            id_, TransceiverStateMachineEvent::TCVR_EV_PROGRAM_IPHY);

        EXPECT_EQ(
            transceiverManager_->getCurrentState(id_),
            TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED);
        const auto& newStateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        // Now isIphyProgrammed should be true
        EXPECT_TRUE(newStateMachine.get_attribute(isIphyProgrammed));
        EXPECT_FALSE(newStateMachine.get_attribute(isXphyProgrammed));
        EXPECT_FALSE(newStateMachine.get_attribute(isTransceiverProgrammed));
        // checked programmed iphy ports
        const auto& newProgrammedIphyPorts =
            transceiverManager_->getProgrammedIphyPortToPortInfo(id_);
        EXPECT_EQ(newProgrammedIphyPorts.size(), 1);
      },
      false /* multiPort */);
}

TEST_F(TransceiverStateMachineTest, programXphy) {
  auto allStates = getAllStates();
  // Only IPHY_PORTS_PROGRAMMED can accept PROGRAM_XPHY event
  verifyStateMachine(
      {TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED},
      TransceiverStateMachineEvent::TCVR_EV_PROGRAM_XPHY,
      TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED /* expected state */,
      allStates,
      [this]() {
        // Make sure `programExternalPhyPorts` has been called
        EXPECT_CALL(*transceiverManager_, programExternalPhyPorts(id_, false))
            .Times(1);
      },
      [this]() {
        const auto& stateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        // Now isXphyProgrammed should be true
        EXPECT_TRUE(stateMachine.get_attribute(isIphyProgrammed));
        EXPECT_TRUE(stateMachine.get_attribute(isXphyProgrammed));
        EXPECT_FALSE(stateMachine.get_attribute(isTransceiverProgrammed));
        EXPECT_TRUE(stateMachine.get_attribute(needMarkLastDownTime));
      },
      false /* multiPort */);
  // Other states should not change even though we try to process the event
  verifyStateUnchanged(
      TransceiverStateMachineEvent::TCVR_EV_PROGRAM_XPHY,
      allStates,
      [this]() {
        // Make sure `programExternalPhyPorts` has never been called
        EXPECT_CALL(*transceiverManager_, programExternalPhyPorts(id_, false))
            .Times(0);
      } /* preUpdate */,
      []() {} /* verify */,
      false /* multiPort */);
}

ACTION(ThrowFbossError) {
  throw FbossError("Mock FbossError");
}
TEST_F(TransceiverStateMachineTest, programXphyFailed) {
  std::set<TransceiverStateMachineState> stateSet = {
      TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED};
  // If programExternalPhyPorts() failed, state shouldn't change
  verifyStateUnchanged(
      TransceiverStateMachineEvent::TCVR_EV_PROGRAM_XPHY,
      stateSet,
      [this]() {
        EXPECT_CALL(*transceiverManager_, programExternalPhyPorts(id_, false))
            .Times(2)
            .WillOnce(ThrowFbossError());
      },
      [this]() {
        const auto& stateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        EXPECT_TRUE(stateMachine.get_attribute(isIphyProgrammed));
        // Now isXphyProgrammed should still be false
        EXPECT_FALSE(stateMachine.get_attribute(isXphyProgrammed));
        EXPECT_FALSE(stateMachine.get_attribute(isTransceiverProgrammed));

        // Then try again, it should succeed
        transceiverManager_->updateStateBlocking(
            id_, TransceiverStateMachineEvent::TCVR_EV_PROGRAM_XPHY);

        EXPECT_EQ(
            transceiverManager_->getCurrentState(id_),
            TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED);
        const auto& newStateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        EXPECT_TRUE(newStateMachine.get_attribute(isIphyProgrammed));
        // Now isXphyProgrammed should be true
        EXPECT_TRUE(stateMachine.get_attribute(isXphyProgrammed));
        EXPECT_FALSE(newStateMachine.get_attribute(isTransceiverProgrammed));
      },
      false /* multiPort */);
}

TEST_F(TransceiverStateMachineTest, readyTransceiver) {
  auto allStates = getAllStates();
  // Both IPHY_PORTS_PROGRAMMED and XPHY_PORTS_PROGRAMMED
  // can accept PREPARE_TRANSCEIVER event
  verifyStateMachine(
      {TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED,
       TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED},
      TransceiverStateMachineEvent::TCVR_EV_PREPARE_TRANSCEIVER,
      TransceiverStateMachineState::TRANSCEIVER_READY /* expected state */,
      allStates,
      [this]() {},
      [this]() {
        const auto& stateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        // Now isTransceiverProgrammed should be true
        EXPECT_TRUE(stateMachine.get_attribute(isIphyProgrammed));
        EXPECT_FALSE(stateMachine.get_attribute(isTransceiverProgrammed));
        EXPECT_TRUE(stateMachine.get_attribute(needMarkLastDownTime));
        EXPECT_FALSE(stateMachine.get_attribute(needResetDataPath));
      },
      false /* multiPort */,
      TransceiverType::MOCK_CMIS);
  // Other states should not change even though we try to process the event
  verifyStateUnchanged(
      TransceiverStateMachineEvent::TCVR_EV_PREPARE_TRANSCEIVER,
      allStates,
      []() {} /* preUpdate */,
      []() {} /* verify */,
      false /* multiPort */,
      TransceiverType::MOCK_CMIS);
}

TEST_F(TransceiverStateMachineTest, programTransceiver) {
  auto allStates = getAllStates();
  // TRANSCEIVER_READY state can accept PROGRAM_TRANSCEIVER event
  verifyStateMachine(
      {TransceiverStateMachineState::TRANSCEIVER_READY},
      TransceiverStateMachineEvent::TCVR_EV_PROGRAM_TRANSCEIVER,
      TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED /* expected state */,
      allStates,
      [this]() { setProgramCmisModuleExpectation(true); },
      [this]() {
        const auto& stateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        // Now isTransceiverProgrammed should be true
        EXPECT_TRUE(stateMachine.get_attribute(isIphyProgrammed));
        EXPECT_TRUE(stateMachine.get_attribute(isTransceiverProgrammed));
        EXPECT_TRUE(stateMachine.get_attribute(needMarkLastDownTime));
        EXPECT_FALSE(stateMachine.get_attribute(needResetDataPath));
      },
      false /* multiPort */,
      TransceiverType::MOCK_CMIS);
  // Other states should not change even though we try to process the event
  verifyStateUnchanged(
      TransceiverStateMachineEvent::TCVR_EV_PROGRAM_TRANSCEIVER,
      allStates,
      [this]() { setProgramCmisModuleExpectation(false); },
      []() {} /* verify */,
      false /* multiPort */,
      TransceiverType::MOCK_CMIS);
}

TEST_F(TransceiverStateMachineTest, programMultiPortTransceiver) {
  auto allStates = getAllStates();
  // TRANSCEIVER_READY state can accept PROGRAM_TRANSCEIVER event
  verifyStateMachine(
      {TransceiverStateMachineState::TRANSCEIVER_READY},
      TransceiverStateMachineEvent::TCVR_EV_PROGRAM_TRANSCEIVER,
      TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED /* expected state */,
      allStates,
      [this]() {
        ::testing::Sequence s;
        setProgramCmisModuleExpectation(true, s, true /* multiPort */);
      },
      [this]() {
        const auto& stateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        // Now isTransceiverProgrammed should be true
        EXPECT_TRUE(stateMachine.get_attribute(isIphyProgrammed));
        EXPECT_TRUE(stateMachine.get_attribute(isTransceiverProgrammed));
        EXPECT_TRUE(stateMachine.get_attribute(needMarkLastDownTime));
        EXPECT_FALSE(stateMachine.get_attribute(needResetDataPath));
      },
      true /* multiPort */,
      TransceiverType::MOCK_CMIS);

  // Other states should not change even though we try to process the event
  verifyStateUnchanged(
      TransceiverStateMachineEvent::TCVR_EV_PROGRAM_TRANSCEIVER,
      allStates,
      [this]() {
        ::testing::Sequence s;
        setProgramCmisModuleExpectation(false, s, true /* multiPort */);
      },
      []() {} /* verify */,
      true /* multiPort */,
      TransceiverType::MOCK_CMIS);
}

TEST_F(TransceiverStateMachineTest, programTransceiverFailed) {
  std::set<TransceiverStateMachineState> stateSet = {
      TransceiverStateMachineState::TRANSCEIVER_READY};
  // If any function inside QsfpModule::programTransceiver() failed,
  // state shouldn't change
  verifyStateUnchanged(
      TransceiverStateMachineEvent::TCVR_EV_PROGRAM_TRANSCEIVER,
      stateSet,
      [this]() {
        MockCmisModule* mockXcvr = static_cast<MockCmisModule*>(xcvr_);

        // Mock throw exception on one of the functions in
        // QsfpModule::programTransceiver()
        TransceiverPortState state{kPortName1, 0, cfg::PortSpeed::HUNDREDG, 4};
        EXPECT_CALL(*mockXcvr, customizeTransceiverLocked(state))
            .Times(2)
            .WillOnce(ThrowFbossError());
        EXPECT_CALL(*mockXcvr, configureModule(0)).Times(1);

        // The rest functions are after customizeTransceiverLocked() and they
        // should only be called once at the second time
        const auto& info = transceiverManager_->getTransceiverInfo(id_);
        if (auto settings = info.tcvrState()->settings()) {
          if (auto hostLaneSettings = settings->hostLaneSettings()) {
            EXPECT_CALL(
                *mockXcvr, ensureRxOutputSquelchEnabled(*hostLaneSettings))
                .Times(1);
          }
        }
        // 2 update qsfp data: one immediately after the successful
        // customizeTransceiverLocked(), the second one is before
        // updateCachedTransceiverInfoLocked() at the end
        EXPECT_CALL(*mockXcvr, updateQsfpData(true)).Times(1);
        ModuleStatus moduleStatus;
        EXPECT_CALL(*mockXcvr, updateCachedTransceiverInfoLocked(moduleStatus))
            .Times(1);
        EXPECT_CALL(*mockXcvr, updateQsfpData(false)).Times(1);
        // Normal transceiver programming shouldn't trigger resetDataPath()
        EXPECT_CALL(*mockXcvr, resetDataPath(testing::_)).Times(0);
      },
      [this]() {
        const auto& stateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        EXPECT_TRUE(stateMachine.get_attribute(isIphyProgrammed));
        // Now isTransceiverProgrammed should still be false
        EXPECT_FALSE(stateMachine.get_attribute(isTransceiverProgrammed));

        // Then try again, it should succeed
        transceiverManager_->updateStateBlocking(
            id_, TransceiverStateMachineEvent::TCVR_EV_PROGRAM_TRANSCEIVER);

        EXPECT_EQ(
            transceiverManager_->getCurrentState(id_),
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED);
        const auto& newStateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        EXPECT_TRUE(newStateMachine.get_attribute(isIphyProgrammed));
        // Now isTransceiverProgrammed should be true
        EXPECT_TRUE(newStateMachine.get_attribute(isTransceiverProgrammed));
        EXPECT_FALSE(stateMachine.get_attribute(needResetDataPath));
      },
      false /* multiPort */,
      TransceiverType::MOCK_CMIS);
}

TEST_F(TransceiverStateMachineTest, portUp) {
  for (auto multiPort : {false, true}) {
    XLOG(INFO) << "Verifying portUp for multiPort = " << multiPort;
    auto allStates = getAllStates();
    // Only TRANSCEIVER_PROGRAMMED and INACTIVE can accept PORT_UP event
    verifyStateMachine(
        {TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
         TransceiverStateMachineState::INACTIVE},
        TransceiverStateMachineEvent::TCVR_EV_PORT_UP,
        TransceiverStateMachineState::ACTIVE /* expected state */,
        allStates,
        []() {},
        [this]() {
          // Enter ACTIVE will also set `needMarkLastDownTime` to true
          const auto& stateMachine =
              transceiverManager_->getStateMachineForTesting(id_);
          EXPECT_TRUE(stateMachine.get_attribute(needMarkLastDownTime));
        },
        multiPort);
    // Other states should not change even though we try to process the event
    verifyStateUnchanged(
        TransceiverStateMachineEvent::TCVR_EV_PORT_UP,
        allStates,
        []() {} /* preUpdate */,
        []() {} /* verify */,
        multiPort);

    // Prepare for testing with next multiPort value
    cleanup();
  }
}

TEST_F(TransceiverStateMachineTest, portDown) {
  for (auto multiPort : {false, true}) {
    XLOG(INFO) << "Verifying portDown for multiPort = " << multiPort;
    auto allStates = getAllStates();
    time_t beforeStateChangedTime{0};
    // Only TRANSCEIVER_PROGRAMMED and ACTIVE can accept ALL_PORTS_DOWN event
    verifyStateMachine(
        {TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
         TransceiverStateMachineState::ACTIVE},
        TransceiverStateMachineEvent::TCVR_EV_ALL_PORTS_DOWN,
        TransceiverStateMachineState::INACTIVE /* expected state */,
        allStates,
        [&beforeStateChangedTime]() {
          beforeStateChangedTime = std::time(nullptr);
          // Sleep 1s to avoid the state machine handling the event too fast
          /* sleep override */
          sleep(1);
        },
        [this, &beforeStateChangedTime]() {
          // Enter INACTIVE will also mark last down time
          const auto& stateMachine =
              transceiverManager_->getStateMachineForTesting(id_);
          EXPECT_FALSE(stateMachine.get_attribute(needMarkLastDownTime));
          // Check the lastDownTime should be updated
          EXPECT_GT(
              transceiverManager_->getLastDownTime(id_),
              beforeStateChangedTime);
        },
        multiPort);
    // Other states should not change even though we try to process the event
    verifyStateUnchanged(
        TransceiverStateMachineEvent::TCVR_EV_ALL_PORTS_DOWN,
        allStates,
        []() {} /* preUpdate */,
        []() {} /* verify */,
        multiPort);
    // Prepare for testing with next multiPort value
    cleanup();
  }
}

TEST_F(TransceiverStateMachineTest, removeTransceiver) {
  for (auto multiPort : {false, true}) {
    XLOG(INFO) << "Verifying removeTransceiver for multiPort = " << multiPort;
    auto allStates = getAllStates();
    // All states besides NOT_PRESENT can accept REMOVE_TRANSCEIVER event
    // Only PRESENT and DISCOVERED don't need to check all ports down, since
    // nothing is actually programmed yet.
    // And INACTIVE also means ports are down so it should pass
    verifyStateMachine(
        {TransceiverStateMachineState::PRESENT,
         TransceiverStateMachineState::DISCOVERED,
         TransceiverStateMachineState::INACTIVE},
        TransceiverStateMachineEvent::TCVR_EV_REMOVE_TRANSCEIVER,
        TransceiverStateMachineState::NOT_PRESENT /* expected state */,
        allStates,
        []() {} /* preUpdate */,
        [this]() { verifyResetProgrammingAttributes(); },
        multiPort);
    // Other after programming states need to make sure all ports down before
    // removing such transceiver in case some transient i2c issue causes we
    // can't detect a transceiver and accidentally remove it
    verifyStateMachine(
        {TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED,
         TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED,
         TransceiverStateMachineState::TRANSCEIVER_READY,
         TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
         TransceiverStateMachineState::ACTIVE,
         TransceiverStateMachineState::UPGRADING},
        TransceiverStateMachineEvent::TCVR_EV_REMOVE_TRANSCEIVER,
        TransceiverStateMachineState::NOT_PRESENT /* expected state */,
        allStates,
        [this, multiPort]() {
          // Set port status to DOWN so that we can remove the transceiver
          // correctly
          updateTransceiverActiveState(
              false /* up */, true /* enabled */, portId1_);
          if (multiPort) {
            updateTransceiverActiveState(
                false /* up */, true /* enabled */, portId5_);
          }
        },
        [this]() { verifyResetProgrammingAttributes(); },
        multiPort);

    // Only NOT_PRESENT left
    EXPECT_EQ(allStates.size(), 1);
    EXPECT_TRUE(
        allStates.find(TransceiverStateMachineState::NOT_PRESENT) !=
        allStates.end());
    verifyStateUnchanged(
        TransceiverStateMachineEvent::TCVR_EV_REMOVE_TRANSCEIVER,
        allStates,
        []() {} /* preUpdate */,
        []() {} /* verify */,
        multiPort);

    // Prepare for testing with next multiPort value
    cleanup();
  }
}

TEST_F(TransceiverStateMachineTest, removeTransceiverFailed) {
  for (auto multiPort : {false, true}) {
    XLOG(INFO) << "Verifying removeTransceiverFailed for multiPort = "
               << multiPort;
    std::set<TransceiverStateMachineState> stateSet = {
        TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED,
        TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED,
        TransceiverStateMachineState::TRANSCEIVER_READY,
        TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
        TransceiverStateMachineState::ACTIVE};
    // If never set port state to down, we can't change the state to NOT_PRESENT
    verifyStateUnchanged(
        TransceiverStateMachineEvent::TCVR_EV_REMOVE_TRANSCEIVER,
        stateSet,
        []() {} /* preUpdate */,
        [this, multiPort]() {
          const auto& stateMachine =
              transceiverManager_->getStateMachineForTesting(id_);
          // Now isIphyProgrammed should still be true
          EXPECT_TRUE(stateMachine.get_attribute(isIphyProgrammed));
          const auto& programmedIphyPorts =
              transceiverManager_->getProgrammedIphyPortToPortInfo(id_);
          if (multiPort) {
            EXPECT_EQ(programmedIphyPorts.size(), 2);
          } else {
            EXPECT_EQ(programmedIphyPorts.size(), 1);
          }

          // Set port status to DOWN so that we can remove the transceiver
          // correctly
          updateTransceiverActiveState(
              false /* up */, true /* enabled */, portId1_);
          if (multiPort) {
            updateTransceiverActiveState(
                false /* up */, true /* enabled */, portId5_);
          }
          // Then try again, it should succeed
          transceiverManager_->updateStateBlocking(
              id_, TransceiverStateMachineEvent::TCVR_EV_REMOVE_TRANSCEIVER);

          EXPECT_EQ(
              transceiverManager_->getCurrentState(id_),
              TransceiverStateMachineState::NOT_PRESENT);
          verifyResetProgrammingAttributes();
        },
        multiPort);

    // Prepare for testing with next multiPort value
    cleanup();
  }
}

TEST_F(TransceiverStateMachineTest, remediateCmisTransceiver) {
  enableRemediationTesting();
  auto allStates = getAllStates();
  // Only INACTIVE can accept REMEDIATE_TRANSCEIVER event
  verifyStateMachine(
      {TransceiverStateMachineState::INACTIVE},
      TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED /* expected state */,
      allStates,
      [this]() {
        // Before trigger remediation, remove remediation pause
        transceiverManager_->setPauseRemediation(0, nullptr);
        // Give 1s buffer when comparing lastDownTime_ in shouldRemediate()
        /* sleep override */
        sleep(1);

        MockCmisTransceiverImpl* xcvrImpl =
            static_cast<MockCmisTransceiverImpl*>(cmisQsfpImpls_[id_].get());
        EXPECT_CALL(*xcvrImpl, triggerQsfpHardReset()).Times(1);
      },
      [this]() { triggerRemediateEvents(); },
      [this]() {
        // Just finished remediation, so the dirty flag should be set
        EXPECT_TRUE(xcvr_->getDirty_());
        // state machine goes back to XPHY_PORTS_PROGRAMMED so that we can
        // reprogram the xcvr again
        const auto& stateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        EXPECT_FALSE(stateMachine.get_attribute(isTransceiverProgrammed));
        // Trigger a prepare transceiver which should fail because the cache is
        // still dirty
        transceiverManager_->updateStateBlocking(
            id_, TransceiverStateMachineEvent::TCVR_EV_PREPARE_TRANSCEIVER);
        // Refresh the state machine again which will first do a successful
        // refresh and clear the dirty_ flag and then trigger
        // PREPARE_TRANSCEIVER
        transceiverManager_->refreshStateMachines();
        EXPECT_EQ(
            transceiverManager_->getCurrentState(id_),
            TransceiverStateMachineState::TRANSCEIVER_READY);
        EXPECT_FALSE(stateMachine.get_attribute(isTransceiverProgrammed));

        // Next the program transceiver event will move it to xcvr programmed
        // state
        transceiverManager_->updateStateBlocking(
            id_, TransceiverStateMachineEvent::TCVR_EV_PROGRAM_TRANSCEIVER);
        EXPECT_FALSE(xcvr_->getDirty_());
        EXPECT_EQ(
            transceiverManager_->getCurrentState(id_),
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED);
        const auto& newStateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        // Now isTransceiverProgrammed should be true
        EXPECT_TRUE(newStateMachine.get_attribute(isTransceiverProgrammed));
      },
      false /* multiPort */,
      TransceiverType::MOCK_CMIS,
      kRemediateTransceiverFnStr);
  // Other states should not change even though we try to process the event
  verifyStateUnchanged(
      allStates,
      [this]() {
        // Before trigger remediation, remove remediation pause
        transceiverManager_->setPauseRemediation(0, nullptr);
        // Give 1s buffer when comparing lastDownTime_ in shouldRemediate()
        /* sleep override */
        sleep(1);

        // Make sure no triggerQsfpHardReset() has been called
        MockTransceiverPlatformApi* xcvrApi =
            static_cast<MockTransceiverPlatformApi*>(
                transceiverManager_->getQsfpPlatformApi());
        EXPECT_CALL(*xcvrApi, triggerQsfpHardReset(id_ + 1)).Times(0);
      } /* preUpdate */,
      [this]() { triggerRemediateEvents(); },
      []() {} /* verify */,
      false /* multiPort */,
      TransceiverType::MOCK_CMIS,
      kRemediateTransceiverFnStr);
}

TEST_F(TransceiverStateMachineTest, remediateCmisMultiPortTransceiver) {
  enableRemediationTesting();
  auto allStates = getAllStates();
  verifyStateMachine(
      // We can only trigger remediation from active state when some ports are
      // down. If all ports are down, we'll trigger remediation from inactive
      // state
      {TransceiverStateMachineState::INACTIVE,
       TransceiverStateMachineState::ACTIVE},
      TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED /* expected state */,
      allStates,
      [this]() {
        // Before trigger remediation, remove remediation pause
        transceiverManager_->setPauseRemediation(0, nullptr);
        // Give 1s buffer when comparing lastDownTime_ in shouldRemediate()
        /* sleep override */
        sleep(1);

        // If we are remediating from inactive state, only then expect a hard
        // reset of transceiver because then we do a full remediation
        // We trigger a hard reset as remediation
      },
      [this]() {
        // When testing remediation from active state, let port1 remain UP but
        // port2 by down
        if (transceiverManager_->getCurrentState(id_) ==
            TransceiverStateMachineState::ACTIVE) {
          updateTransceiverActiveState(
              true /* up */, true /* enabled */, portId1_);
          updateTransceiverActiveState(
              false /* up */, true /* enabled */, portId5_);
        }
        triggerRemediateEvents();
        MockCmisTransceiverImpl* xcvrImpl =
            static_cast<MockCmisTransceiverImpl*>(cmisQsfpImpls_[id_].get());
        EXPECT_CALL(*xcvrImpl, triggerQsfpHardReset())
            .Times(
                transceiverManager_->getCurrentState(id_) ==
                TransceiverStateMachineState::INACTIVE);
      },
      [this]() {
        // Just finished remediation, so the dirty flag should be set
        EXPECT_TRUE(xcvr_->getDirty_());
        // state machine goes back to XPHY_PORTS_PROGRAMMED so that we can
        // reprogram the xcvr again
        const auto& stateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        EXPECT_FALSE(stateMachine.get_attribute(isTransceiverProgrammed));
        // Trigger a prepare transceiver which should fail because the cache is
        // still dirty
        transceiverManager_->updateStateBlocking(
            id_, TransceiverStateMachineEvent::TCVR_EV_PREPARE_TRANSCEIVER);
        // Refresh the state machine again which will first do a successful
        // refresh and clear the dirty_ flag and then trigger
        // PREPARE_TRANSCEIVER
        transceiverManager_->refreshStateMachines();
        EXPECT_EQ(
            transceiverManager_->getCurrentState(id_),
            TransceiverStateMachineState::TRANSCEIVER_READY);
        EXPECT_FALSE(stateMachine.get_attribute(isTransceiverProgrammed));

        // Next the program transceiver event will move it to xcvr programmed
        // state
        transceiverManager_->updateStateBlocking(
            id_, TransceiverStateMachineEvent::TCVR_EV_PROGRAM_TRANSCEIVER);
        EXPECT_FALSE(xcvr_->getDirty_());
        EXPECT_EQ(
            transceiverManager_->getCurrentState(id_),
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED);
        const auto& newStateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        // Now isTransceiverProgrammed should be true
        EXPECT_TRUE(newStateMachine.get_attribute(isTransceiverProgrammed));
      },
      true /* multiPort */,
      TransceiverType::MOCK_CMIS,
      kRemediateTransceiverFnStr);

  // Other states should not change even though we try to process the event
  verifyStateUnchanged(
      allStates,
      [this]() {
        // Before trigger remediation, remove remediation pause
        transceiverManager_->setPauseRemediation(0, nullptr);
        // Give 1s buffer when comparing lastDownTime_ in shouldRemediate()
        /* sleep override */
        sleep(1);

        // Make sure no triggerQsfpHardReset() has been called
        MockTransceiverPlatformApi* xcvrApi =
            static_cast<MockTransceiverPlatformApi*>(
                transceiverManager_->getQsfpPlatformApi());
        EXPECT_CALL(*xcvrApi, triggerQsfpHardReset(id_ + 1)).Times(0);
      } /* preUpdate */,
      [this]() { triggerRemediateEvents(); },
      []() {} /* verify */,
      true /* multiPort */,
      TransceiverType::MOCK_CMIS,
      kRemediateTransceiverFnStr);
}

TEST_F(TransceiverStateMachineTest, remediateSff8472Transceiver) {
  enableRemediationTesting();
  auto allStates = getAllStates();
  // Only INACTIVE can accept REMEDIATE_TRANSCEIVER event
  verifyStateMachine(
      {TransceiverStateMachineState::INACTIVE},
      TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED /* expected state */,
      allStates,
      [this]() {
        // Before trigger remediation, remove remediation pause
        transceiverManager_->setPauseRemediation(0, nullptr);
        // Give 1s buffer when comparing lastDownTime_ in shouldRemediate()
        /* sleep override */
        sleep(1);

        // We trigger a hard reset as remediation
        MockSff8472TransceiverImpl* xcvrImpl =
            static_cast<MockSff8472TransceiverImpl*>(
                sff8472QsfpImpls_[id_].get());
        EXPECT_CALL(*xcvrImpl, triggerQsfpHardReset()).Times(1);
      },
      [this]() { triggerRemediateEvents(); },
      [this]() {
        // Just finished remediation, so the dirty flag should be set
        EXPECT_TRUE(xcvr_->getDirty_());
        // state machine goes back to XPHY_PORTS_PROGRAMMED so that we can
        // reprogram the xcvr again
        const auto& stateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        EXPECT_FALSE(stateMachine.get_attribute(isTransceiverProgrammed));
        // Trigger a prepare transceiver which should fail because the cache is
        // still dirty
        transceiverManager_->updateStateBlocking(
            id_, TransceiverStateMachineEvent::TCVR_EV_PREPARE_TRANSCEIVER);

        // Refresh the state machine again which will first do a successful
        // refresh and clear the dirty_ flag and then trigger
        // PREPARE_TRANSCEIVER
        transceiverManager_->refreshStateMachines();
        EXPECT_EQ(
            transceiverManager_->getCurrentState(id_),
            TransceiverStateMachineState::TRANSCEIVER_READY);
        EXPECT_FALSE(stateMachine.get_attribute(isTransceiverProgrammed));

        // Next trigger a program transceiver event which will move it to
        // xcvr programmed state
        transceiverManager_->updateStateBlocking(
            id_, TransceiverStateMachineEvent::TCVR_EV_PROGRAM_TRANSCEIVER);
        EXPECT_FALSE(xcvr_->getDirty_());
        EXPECT_EQ(
            transceiverManager_->getCurrentState(id_),
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED);
        const auto& newStateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        // Now isTransceiverProgrammed should be true
        EXPECT_TRUE(newStateMachine.get_attribute(isTransceiverProgrammed));
      },
      false /* multiPort */,
      TransceiverType::MOCK_SFF8472,
      kRemediateTransceiverFnStr);
  // Other states should not change even though we try to process the event
  verifyStateUnchanged(
      allStates,
      [this]() {
        // Before trigger remediation, remove remediation pause
        transceiverManager_->setPauseRemediation(0, nullptr);
        // Give 1s buffer when comparing lastDownTime_ in shouldRemediate()
        /* sleep override */
        sleep(1);

        // Make sure no triggerQsfpHardReset() has been called
        MockTransceiverPlatformApi* xcvrApi =
            static_cast<MockTransceiverPlatformApi*>(
                transceiverManager_->getQsfpPlatformApi());
        EXPECT_CALL(*xcvrApi, triggerQsfpHardReset(id_ + 1)).Times(0);
      } /* preUpdate */,
      [this]() { triggerRemediateEvents(); },
      []() {} /* verify */,
      false /* multiPort */,
      TransceiverType::MOCK_SFF8472,
      kRemediateTransceiverFnStr);
}

TEST_F(TransceiverStateMachineTest, remediateCmisTransceiverFailed) {
  enableRemediationTesting();
  std::set<TransceiverStateMachineState> stateSet = {
      TransceiverStateMachineState::INACTIVE};
  // If triggerQsfpHardReset() failed, state shouldn't change
  verifyStateUnchanged(
      stateSet,
      [this]() {
        // Before trigger remediation, remove remediation pause
        transceiverManager_->setPauseRemediation(0, nullptr);
        // Give 1s buffer when comparing lastDownTime_ in shouldRemediate()
        /* sleep override */
        sleep(1);

        MockCmisTransceiverImpl* xcvrImpl =
            static_cast<MockCmisTransceiverImpl*>(cmisQsfpImpls_[id_].get());
        EXPECT_CALL(*xcvrImpl, triggerQsfpHardReset())
            .Times(2)
            .WillOnce(ThrowFbossError());

        MockCmisModule* mockXcvr = static_cast<MockCmisModule*>(xcvr_);
        ::testing::Sequence s;
        // Expect updateQsfpData and updateCachedTransceiverInfoLocked to be
        // called from refreshStateMachines() we do in verify() below
        EXPECT_CALL(*mockXcvr, updateQsfpData(true)).Times(1);
        EXPECT_CALL(*mockXcvr, updateQsfpData(false)).Times(3);
        EXPECT_CALL(*mockXcvr, updateCachedTransceiverInfoLocked(::testing::_))
            .Times(2)
            .InSequence(s);
        setProgramCmisModuleExpectation(true, s);
      },
      [this]() { triggerRemediateEvents(); },
      [this]() {
        // The earlier remediation failed, so the dirty flag shouldn't be set
        EXPECT_FALSE(xcvr_->getDirty_());
        const auto& stateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        EXPECT_TRUE(stateMachine.get_attribute(isIphyProgrammed));
        EXPECT_TRUE(stateMachine.get_attribute(isTransceiverProgrammed));

        // Then try again, it should succeed
        transceiverManager_->updateStateBlocking(
            id_, TransceiverStateMachineEvent::TCVR_EV_REMEDIATE_TRANSCEIVER);
        // Since the remediation failed, the dirty flag should be set now
        EXPECT_TRUE(xcvr_->getDirty_());
        EXPECT_EQ(
            transceiverManager_->getCurrentState(id_),
            TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED);
        const auto& afterRemediationStateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        EXPECT_FALSE(afterRemediationStateMachine.get_attribute(
            isTransceiverProgrammed));

        // Refresh the state machine again which will first do a successful
        // refresh and clear the dirty_ flag and then trigger
        // PREPARE_TRANSCEIVER
        transceiverManager_->refreshStateMachines();
        EXPECT_EQ(
            transceiverManager_->getCurrentState(id_),
            TransceiverStateMachineState::TRANSCEIVER_READY);
        EXPECT_FALSE(xcvr_->getDirty_());

        // Next refresh() will trigger PROGRAM_TRANSCEIVER
        transceiverManager_->refreshStateMachines();
        EXPECT_EQ(
            transceiverManager_->getCurrentState(id_),
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED);
        const auto& afterProgrammingStateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        EXPECT_TRUE(afterProgrammingStateMachine.get_attribute(
            isTransceiverProgrammed));
      },
      false /* multiPort */,
      TransceiverType::MOCK_CMIS,
      kRemediateTransceiverFnStr);
}

TEST_F(TransceiverStateMachineTest, remediateSffTransceiver) {
  enableRemediationTesting();
  auto allStates = getAllStates();
  // Only INACTIVE can accept REMEDIATE_TRANSCEIVER event
  verifyStateMachine(
      {TransceiverStateMachineState::INACTIVE},
      TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED /* expected state */,
      allStates,
      [this]() {
        // Before trigger remediation, remove remediation pause
        transceiverManager_->setPauseRemediation(0, nullptr);
        // Give 1s buffer when comparing lastDownTime_ in shouldRemediate()
        /* sleep override */
        sleep(1);

        MockSffModule* mockXcvr = static_cast<MockSffModule*>(xcvr_);
        ::testing::InSequence s;
        EXPECT_CALL(*mockXcvr, resetLowPowerMode()).Times(1);
        EXPECT_CALL(*mockXcvr, ensureTxEnabled()).Times(1);
      },
      [this]() { triggerRemediateEvents(); },
      [this]() {
        // Just finished remediation, so the dirty flag should be set
        EXPECT_TRUE(xcvr_->getDirty_());
        // state machine goes back to XPHY_PORTS_PROGRAMMED so that we can
        // reprogram the xcvr again
        const auto& stateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        EXPECT_FALSE(stateMachine.get_attribute(isTransceiverProgrammed));
        // Trigger a prepare transceiver which should fail because the cache is
        // still dirty
        transceiverManager_->updateStateBlocking(
            id_, TransceiverStateMachineEvent::TCVR_EV_PREPARE_TRANSCEIVER);
        // Refresh the state machine again which will first do a successful
        // refresh and clear the dirty_ flag and then trigger
        // PREPARE_TRANSCEIVER
        transceiverManager_->refreshStateMachines();
        EXPECT_EQ(
            transceiverManager_->getCurrentState(id_),
            TransceiverStateMachineState::TRANSCEIVER_READY);
        EXPECT_FALSE(stateMachine.get_attribute(isTransceiverProgrammed));

        // Next trigger PROGRAM_TRANSCEIVER to move it to xcvr programmed
        transceiverManager_->updateStateBlocking(
            id_, TransceiverStateMachineEvent::TCVR_EV_PROGRAM_TRANSCEIVER);
        EXPECT_FALSE(xcvr_->getDirty_());
        EXPECT_EQ(
            transceiverManager_->getCurrentState(id_),
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED);
        const auto& newStateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        // Now isTransceiverProgrammed should be true
        EXPECT_TRUE(newStateMachine.get_attribute(isTransceiverProgrammed));
      },
      false /* multiPort */,
      TransceiverType::MOCK_SFF,
      kRemediateTransceiverFnStr);
  // Other states should not change even though we try to process the event
  verifyStateUnchanged(
      allStates,
      [this]() {
        // Before trigger remediation, remove remediation pause
        transceiverManager_->setPauseRemediation(0, nullptr);
        // Give 1s buffer when comparing lastDownTime_ in shouldRemediate()
        /* sleep override */
        sleep(1);

        MockSffModule* mockXcvr = static_cast<MockSffModule*>(xcvr_);
        ::testing::InSequence s;
        EXPECT_CALL(*mockXcvr, resetLowPowerMode()).Times(0);
        EXPECT_CALL(*mockXcvr, ensureTxEnabled()).Times(0);
      } /* preUpdate */,
      [this]() { triggerRemediateEvents(); },
      []() {} /* verify */,
      false /* multiPort */,
      TransceiverType::MOCK_SFF,
      kRemediateTransceiverFnStr);
}

TEST_F(TransceiverStateMachineTest, remediateSffTransceiverFailed) {
  enableRemediationTesting();
  std::set<TransceiverStateMachineState> stateSet = {
      TransceiverStateMachineState::INACTIVE};
  // If resetLowPowerMode() failed, state shouldn't change
  verifyStateUnchanged(
      stateSet,
      [this]() {
        // Before trigger remediation, remove remediation pause
        transceiverManager_->setPauseRemediation(0, nullptr);
        // Give 1s buffer when comparing lastDownTime_ in shouldRemediate()
        /* sleep override */
        sleep(1);

        MockSffModule* mockXcvr = static_cast<MockSffModule*>(xcvr_);
        ::testing::InSequence s;
        EXPECT_CALL(*mockXcvr, resetLowPowerMode())
            .Times(2)
            .WillOnce(ThrowFbossError());
        EXPECT_CALL(*mockXcvr, ensureTxEnabled()).Times(1);
      },
      [this]() { triggerRemediateEvents(); },
      [this]() {
        // The earlier remediation failed, so the dirty flag should not be set
        EXPECT_FALSE(xcvr_->getDirty_());
        // state machine goes back to XPHY_PORTS_PROGRAMMED so that we can
        // reprogram the xcvr again
        const auto& stateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        EXPECT_TRUE(stateMachine.get_attribute(isIphyProgrammed));
        EXPECT_TRUE(stateMachine.get_attribute(isTransceiverProgrammed));

        // Then try again, it should succeed
        transceiverManager_->updateStateBlocking(
            id_, TransceiverStateMachineEvent::TCVR_EV_REMEDIATE_TRANSCEIVER);
        EXPECT_TRUE(xcvr_->getDirty_());
        EXPECT_EQ(
            transceiverManager_->getCurrentState(id_),
            TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED);
        const auto& afterRemediationStateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        EXPECT_FALSE(afterRemediationStateMachine.get_attribute(
            isTransceiverProgrammed));

        // Refresh the state machine again which will first do a successful
        // refresh and clear the dirty_ flag and then trigger
        // PREPARE_TRANSCEIVER. Next refresh will trigger PROGRAM_TRANSCEIVER
        transceiverManager_->refreshStateMachines();
        EXPECT_FALSE(xcvr_->getDirty_());
        transceiverManager_->refreshStateMachines();
        const auto& afterProgrammingStateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        EXPECT_TRUE(afterProgrammingStateMachine.get_attribute(
            isTransceiverProgrammed));
      },
      false /* multiPort */,
      TransceiverType::MOCK_SFF,
      kRemediateTransceiverFnStr);
}

TEST_F(TransceiverStateMachineTest, agentConfigChangedWarmBoot) {
  auto allStates = getAllStates();
  // Instead of using transceiverManager_->updateStateBlocking(), use
  // triggerAgentConfigChangeEvent() so that we can verify the logic inside this
  // function too
  // After programmed state can accept RESET_TO_DISCOVERED event
  verifyStateMachine(
      {TransceiverStateMachineState::ACTIVE,
       TransceiverStateMachineState::INACTIVE,
       TransceiverStateMachineState::TRANSCEIVER_READY,
       TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
       TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED,
       TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED,
       TransceiverStateMachineState::UPGRADING},
      TransceiverStateMachineState::DISCOVERED /* expected state */,
      allStates,
      []() {} /* preUpdate */,
      [this]() { triggerAgentConfigChanged(false); } /* stateUpdate */,
      [this]() {
        // Enter DISCOVERED will also call `resetProgrammingAttributes`
        const auto& stateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        EXPECT_FALSE(stateMachine.get_attribute(isIphyProgrammed));
        EXPECT_FALSE(stateMachine.get_attribute(isXphyProgrammed));
        EXPECT_FALSE(stateMachine.get_attribute(isTransceiverProgrammed));
        EXPECT_TRUE(stateMachine.get_attribute(needMarkLastDownTime));
      } /* verify */,
      false /* multiPort */,
      TransceiverType::CMIS,
      kWarmBootAgentConfigChangedFnStr);

  // Other states should not change even though we try to process the event
  verifyStateUnchanged(
      allStates,
      []() {} /* preUpdate */,
      [this]() { triggerAgentConfigChanged(false); } /* stateUpdate */,
      []() {} /* verify */,
      false /* multiPort */,
      TransceiverType::CMIS,
      kWarmBootAgentConfigChangedFnStr);
}

TEST_F(TransceiverStateMachineTest, agentConfigChangedColdBoot) {
  auto allStates = getAllStates();
  // Instead of using transceiverManager_->updateStateBlocking(), use
  // triggerAgentConfigChangeEvent() so that we can verify the logic inside this
  // function too
  // After programmed state can accept RESET_TO_DISCOVERED event
  verifyStateMachine(
      {TransceiverStateMachineState::ACTIVE,
       TransceiverStateMachineState::INACTIVE,
       TransceiverStateMachineState::TRANSCEIVER_READY,
       TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
       TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED,
       TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED,
       TransceiverStateMachineState::UPGRADING},
      TransceiverStateMachineState::DISCOVERED /* expected state */,
      allStates,
      [this]() {
        auto* mockXcvr = static_cast<MockCmisModule*>(xcvr_);
        int expectedResets = transceiverManager_->getCurrentState(id_) ==
                TransceiverStateMachineState::ACTIVE
            ? 0
            : 1;
        EXPECT_CALL(*mockXcvr, resetDataPath(testing::_)).Times(expectedResets);
      } /* preUpdate */,
      [this]() { triggerAgentConfigChanged(true); } /* stateUpdate */,
      [this]() {
        // Enter DISCOVERED will also call `resetProgrammingAttributes`
        const auto& stateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        EXPECT_FALSE(stateMachine.get_attribute(isIphyProgrammed));
        EXPECT_FALSE(stateMachine.get_attribute(isXphyProgrammed));
        EXPECT_FALSE(stateMachine.get_attribute(isTransceiverProgrammed));
        EXPECT_TRUE(stateMachine.get_attribute(needMarkLastDownTime));
        // Cold boot agent will need a reset data path for CMIS module
        EXPECT_TRUE(stateMachine.get_attribute(needResetDataPath));

        // refresh twice state machine so that we can finish programming xcvr
        transceiverManager_->refreshStateMachines();
        transceiverManager_->refreshStateMachines();
        transceiverManager_->refreshStateMachines();
        EXPECT_FALSE(stateMachine.get_attribute(needResetDataPath));
      } /* verify */,
      false /* multiPort */,
      TransceiverType::MOCK_CMIS,
      kColdBootAgentConfigChangedFnStr);

  // Other states should not change even though we try to process the event
  verifyStateUnchanged(
      allStates,
      []() {} /* preUpdate */,
      [this]() { triggerAgentConfigChanged(true); } /* stateUpdate */,
      []() {} /* verify */,
      false /* multiPort */,
      TransceiverType::MOCK_CMIS,
      kColdBootAgentConfigChangedFnStr);
}

TEST_F(TransceiverStateMachineTest, agentConfigChangedColdBootFsdbStatusAge) {
  gflags::FlagSaver flagSaver;
  xcvr_ =
      overrideTransceiver(false /* multiPort */, TransceiverType::MOCK_CMIS);
  setState(TransceiverStateMachineState::ACTIVE, false /* multiPort */);
  auto* mockXcvr = static_cast<MockCmisModule*>(xcvr_);

  gflags::SetCommandLineOption("subscribe_to_state_from_fsdb", "true");
  transceiverManager_->setOverrideAgentPortStatusForTesting(
      false /* up */, false /* enabled */, true /* clearOnly */);
  auto publishPortUp = [this]() {
    NpuPortStatus status;
    status.portEnabled = true;
    status.operState = true;
    std::map<int, NpuPortStatus> portStatus{{portId1_, status}};
    transceiverManager_->syncNpuPortStatusUpdate(portStatus);
  };
  auto reprogram = [this]() {
    for (int i = 0; i < 3; i++) {
      transceiverManager_->refreshStateMachines();
    }
  };

  publishPortUp();
  /* sleep override */
  sleep(1);
  EXPECT_CALL(*mockXcvr, resetDataPath(testing::_)).Times(1);
  triggerAgentConfigChanged(true);
  reprogram();
  testing::Mock::VerifyAndClearExpectations(mockXcvr);

  /* sleep override */
  sleep(1);
  EXPECT_CALL(*mockXcvr, resetDataPath(testing::_)).Times(0);
  triggerAgentConfigChanged(true);
  publishPortUp();
  reprogram();
  EXPECT_FALSE(
      transceiverManager_->getStateMachineForTesting(id_).get_attribute(
          needResetDataPath));
}

TEST_F(TransceiverStateMachineTest, agentConfigChangedWarmBootOnAbsentXcvr) {
  auto allStates = getAllStates();
  verifyStateMachine(
      {TransceiverStateMachineState::ACTIVE,
       TransceiverStateMachineState::INACTIVE,
       TransceiverStateMachineState::TRANSCEIVER_READY,
       TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
       TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED,
       TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED,
       TransceiverStateMachineState::UPGRADING},
      TransceiverStateMachineState::NOT_PRESENT /* expected state */,
      allStates,
      [this]() {
        setMockCmisPresence(false);
        xcvr_->detectPresence();
      } /* preUpdate */,
      [this]() { triggerAgentConfigChanged(false); } /* stateUpdate */,
      [this]() {
        // Enter DISCOVERED will also call `resetProgrammingAttributes`
        const auto& stateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        EXPECT_FALSE(stateMachine.get_attribute(isIphyProgrammed));
        EXPECT_FALSE(stateMachine.get_attribute(isXphyProgrammed));
        EXPECT_FALSE(stateMachine.get_attribute(isTransceiverProgrammed));
        EXPECT_TRUE(stateMachine.get_attribute(needMarkLastDownTime));
      } /* verify */,
      false /* multiPort */,
      TransceiverType::MOCK_CMIS,
      kWarmBootAgentConfigChangedFnStr);

  // Other states should not change even though we try to process the event
  verifyStateUnchanged(
      allStates,
      [this]() {
        setMockCmisPresence(false);
        xcvr_->detectPresence();
      } /* preUpdate */,
      [this]() { triggerAgentConfigChanged(false); } /* stateUpdate */,
      []() {} /* verify */,
      false /* multiPort */,
      TransceiverType::MOCK_CMIS,
      kWarmBootAgentConfigChangedFnStr);
}

TEST_F(TransceiverStateMachineTest, agentConfigChangedColdBootOnAbsentXcvr) {
  auto allStates = getAllStates();
  // Use ABSENT_XCVR for such transceiver to make sure state can go back to
  // NOT_PRESENT
  verifyStateMachine(
      {TransceiverStateMachineState::ACTIVE,
       TransceiverStateMachineState::INACTIVE,
       TransceiverStateMachineState::TRANSCEIVER_READY,
       TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
       TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED,
       TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED,
       TransceiverStateMachineState::UPGRADING},
      TransceiverStateMachineState::NOT_PRESENT /* expected state */,
      allStates,
      [this]() {
        setMockCmisPresence(false);
        xcvr_->detectPresence();

        // Even though agent cold boot, because transceiver is absent,
        // resetDataPath() won't be called
        MockCmisModule* mockXcvr = static_cast<MockCmisModule*>(xcvr_);
        EXPECT_CALL(*mockXcvr, resetDataPath(testing::_)).Times(0);
      } /* preUpdate */,
      [this]() { triggerAgentConfigChanged(true); } /* stateUpdate */,
      [this]() {
        // Enter DISCOVERED will also call `resetProgrammingAttributes`
        const auto& stateMachine =
            transceiverManager_->getStateMachineForTesting(id_);
        EXPECT_FALSE(stateMachine.get_attribute(isIphyProgrammed));
        EXPECT_FALSE(stateMachine.get_attribute(isXphyProgrammed));
        EXPECT_FALSE(stateMachine.get_attribute(isTransceiverProgrammed));
        EXPECT_TRUE(stateMachine.get_attribute(needMarkLastDownTime));
        // Cold boot agent will need a reset data path for CMIS module
        EXPECT_TRUE(stateMachine.get_attribute(needResetDataPath));

        // refresh thrice state machine so that we can finish programming xcvr
        transceiverManager_->refreshStateMachines();
        EXPECT_TRUE(stateMachine.get_attribute(isIphyProgrammed));
        EXPECT_EQ(
            transceiverManager_->getCurrentState(id_),
            TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED);

        transceiverManager_->refreshStateMachines();
        EXPECT_EQ(
            transceiverManager_->getCurrentState(id_),
            TransceiverStateMachineState::TRANSCEIVER_READY);

        transceiverManager_->refreshStateMachines();
        EXPECT_TRUE(stateMachine.get_attribute(isTransceiverProgrammed));
        EXPECT_EQ(
            transceiverManager_->getCurrentState(id_),
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED);

        EXPECT_FALSE(stateMachine.get_attribute(needResetDataPath));
      } /* verify */,
      false /* multiPort */,
      TransceiverType::MOCK_CMIS,
      kColdBootAgentConfigChangedFnStr);

  // Other states should not change even though we try to process the event
  verifyStateUnchanged(
      allStates,
      [this]() {
        setMockCmisPresence(false);
        xcvr_->detectPresence();
      } /* preUpdate */,
      [this]() { triggerAgentConfigChanged(true); } /* stateUpdate */,
      []() {} /* verify */,
      false /* multiPort */,
      TransceiverType::MOCK_CMIS,
      kColdBootAgentConfigChangedFnStr);
}

TEST_F(TransceiverStateMachineTest, syncPortsOnRemovedTransceiver) {
  std::set<TransceiverStateMachineState> stateSet = {
      TransceiverStateMachineState::ACTIVE};
  verifyStateMachine(
      {TransceiverStateMachineState::ACTIVE},
      TransceiverStateMachineState::NOT_PRESENT,
      stateSet,
      [this]() {
        setMockCmisPresence(false);

        MockCmisModule* mockXcvr = static_cast<MockCmisModule*>(xcvr_);
        ::testing::Sequence s;
        // The first refreshLocked() should detect transceiver is removed
        // and dirty, need to updateQsfpData fully
        EXPECT_CALL(*mockXcvr, ensureOutOfReset());
        EXPECT_CALL(*mockXcvr, updateQsfpData(true));
        // Because transceiver is absent, we should use default ModuleStatus
        // to update cached transceiver info
        ModuleStatus moduleStatus;
        EXPECT_CALL(*mockXcvr, updateCachedTransceiverInfoLocked(moduleStatus));
      } /* preUpdate */,
      [this]() {
        // Trigger active state change function just like wedge_agent calls
        // qsfp_service syncPorts(). Bring down the ports
        updateTransceiverActiveState(
            false /* up */, true /* enabled */, portId1_);
        // The refresh() will let TransceiverStateMachine trigger next event
        xcvr_->refresh();
      } /* stateUpdate */,
      [this]() {
        // The TransceiverStateMachine should trigger a remove event during
        // refresh(). Now check all programming attributes being removed.
        verifyResetProgrammingAttributes();
      } /* verify */,
      false /* multiPort */,
      TransceiverType::MOCK_CMIS,
      "Triggering syncPorts with port down on a removed transceiver");
}

TEST_F(TransceiverStateMachineTest, reseatTransceiver) {
  auto removeCmisTransceiver = [this](bool isRemoval) {
    if (isRemoval) {
      XLOG(INFO) << "Verifying Removing Transceiver: " << id_;
      transceiverManager_->overrideMgmtInterface(
          static_cast<int>(id_) + 1,
          uint8_t(TransceiverModuleIdentifier::UNKNOWN));
      transceiverManager_->overrideTransceiverForTesting(id_, nullptr);
    } else {
      XLOG(INFO) << "Verifying Inserting new CMIS Transceiver: " << id_;
      // Mimic a SFF module -> CMIS module replacement
      transceiverManager_->overrideMgmtInterface(
          static_cast<int>(id_) + 1,
          uint8_t(TransceiverModuleIdentifier::QSFP_PLUS_CMIS));
      cmisQsfpImpls_.push_back(
          std::make_unique<MockCmisTransceiverImpl>(
              id_, transceiverManager_.get()));
      EXPECT_CALL(*cmisQsfpImpls_.back().get(), detectTransceiver())
          .WillRepeatedly(::testing::Return(true));
      xcvr_ = static_cast<QsfpModule*>(
          transceiverManager_->overrideTransceiverForTesting(
              id_,
              std::make_unique<MockCmisModule>(
                  transceiverManager_->getPortNames(id_),
                  cmisQsfpImpls_.back().get(),
                  tcvrConfig_,
                  transceiverManager_->getTransceiverName(id_))));
    }

    // Check this refreshStateMachines will:
    // 1) If it's a removal operation, such transceiver will be removed and the
    // state machine will be set back to NOT_PRESENT. Example log: P501158293
    // 2) If it's a new transceiver inserted, a new transceiver will be detected
    transceiverManager_->refreshStateMachines();
    const auto& xcvrInfo = transceiverManager_->getTransceiverInfo(id_);
    EXPECT_EQ(*xcvrInfo.tcvrState()->present(), !isRemoval);
    // verify that getTransceiverInfo properly returns a timestamp when
    // the transceiver isn't present
    if (isRemoval) {
      EXPECT_GT(*xcvrInfo.tcvrState()->timeCollected(), 0);
      EXPECT_GT(*xcvrInfo.tcvrStats()->timeCollected(), 0);
    }

    // Both cases will kick off a PROGRAM_IPHY event
    EXPECT_EQ(
        transceiverManager_->getCurrentState(id_),
        TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED);
    // Now only isIphyProgrammed should be true
    const auto& stateMachine =
        transceiverManager_->getStateMachineForTesting(id_);
    EXPECT_TRUE(stateMachine.get_attribute(isIphyProgrammed));
    EXPECT_FALSE(stateMachine.get_attribute(isXphyProgrammed));
    EXPECT_FALSE(stateMachine.get_attribute(isTransceiverProgrammed));

    // Use updateStateBlocking() to skip PhyManager check
    // Make sure `programExternalPhyPorts` has been called
    EXPECT_CALL(*transceiverManager_, programExternalPhyPorts(id_, false))
        .Times(1);
    transceiverManager_->updateStateBlocking(
        id_, TransceiverStateMachineEvent::TCVR_EV_PROGRAM_XPHY);
    EXPECT_EQ(
        transceiverManager_->getCurrentState(id_),
        TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED);
    // Now isXphyProgrammed should also be true
    const auto& stateMachine2 =
        transceiverManager_->getStateMachineForTesting(id_);
    EXPECT_TRUE(stateMachine2.get_attribute(isIphyProgrammed));
    EXPECT_TRUE(stateMachine2.get_attribute(isXphyProgrammed));
    EXPECT_FALSE(stateMachine2.get_attribute(isTransceiverProgrammed));

    // Because the transceiver is not present, the PROGRAM_TRANSCEIVER should be
    // no-op
    if (!isRemoval) {
      // When transceiver is present, do a successful refresh to make the cache
      // valid
      MockCmisModule* mockXcvr = static_cast<MockCmisModule*>(xcvr_);
      ::testing::Sequence s;
      EXPECT_CALL(*mockXcvr, updateQsfpData(false)).Times(3);
      EXPECT_CALL(*mockXcvr, updateCachedTransceiverInfoLocked(::testing::_))
          .Times(2);
      setProgramCmisModuleExpectation(true);
    }
    if (!isRemoval) {
      EXPECT_TRUE(stateMachine2.get_attribute(newTransceiverInsertedAfterInit));
    }
    // Refresh the state machine again which will first do a successful
    // refresh and clear the dirty_ flag and then trigger
    // PREPARE_TRANSCEIVER. Next refresh will trigger PROGRAM_TRANSCEIVER
    EXPECT_EQ(
        transceiverManager_->getCurrentState(id_),
        TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED);
    transceiverManager_->refreshStateMachines();
    EXPECT_EQ(
        transceiverManager_->getCurrentState(id_),
        TransceiverStateMachineState::TRANSCEIVER_READY);
    transceiverManager_->refreshStateMachines();
    if (!isRemoval) {
      // Only check when xcvr is there
      EXPECT_FALSE(xcvr_->getDirty_());
    }
    EXPECT_EQ(
        transceiverManager_->getCurrentState(id_),
        TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED);
    // Now all programming attributes should be true
    const auto& stateMachine3 =
        transceiverManager_->getStateMachineForTesting(id_);
    EXPECT_TRUE(stateMachine3.get_attribute(isIphyProgrammed));
    EXPECT_TRUE(stateMachine3.get_attribute(isXphyProgrammed));
    EXPECT_TRUE(stateMachine3.get_attribute(isTransceiverProgrammed));

    // Now that transceiver is programmed, newTransceiverInsertedAfterInit
    // should be false
    if (!isRemoval) {
      EXPECT_FALSE(
          stateMachine2.get_attribute(newTransceiverInsertedAfterInit));
    }
  };

  // Due to this test might need several updates, which are first removing the
  // transceiver and then inserting back, instead of using the general
  // verifyStateMachine(), we decided to break the whole tests into multiple
  // steps.
  // Step1: Mimic agent side ports down
  xcvr_ = overrideTransceiver(false /* multiPort */, TransceiverType::MOCK_SFF);
  setState(TransceiverStateMachineState::INACTIVE, false /* multiPort */);

  // Step2: Remove the transceiver and check the state machine updates
  removeCmisTransceiver(true);

  // Step3: Insert the transceiver back and call refreshStateMachine()
  removeCmisTransceiver(false);
}

TEST_F(TransceiverStateMachineTest, programMultiPortTransceiverSequentially) {
  xcvr_ = overrideTransceiver(true /* multiPort */, TransceiverType::CMIS);
  auto verifyPortToLaneMap = [this](bool bothPorts) {
    std::map<std::string, std::vector<int>> expectedHostLaneMap = {
        {"eth1/1/1", {0, 1, 2, 3}}};
    std::map<std::string, std::vector<int>> expectedMediaLaneMap = {
        {"eth1/1/1", {0, 1, 2, 3}}};
    if (bothPorts) {
      std::vector<int> hostLanes = {4, 5, 6, 7};
      std::vector<int> mediaLanes = {4, 5, 6, 7};
      expectedHostLaneMap.emplace("eth1/1/5", hostLanes);
      expectedMediaLaneMap.emplace("eth1/1/5", mediaLanes);
    }
    const auto& info = transceiverManager_->getTransceiverInfo(id_);
    EXPECT_EQ(info.tcvrState()->portNameToHostLanes(), expectedHostLaneMap);
    EXPECT_EQ(info.tcvrState()->portNameToMediaLanes(), expectedMediaLaneMap);
    EXPECT_EQ(info.tcvrStats()->portNameToHostLanes(), expectedHostLaneMap);
    EXPECT_EQ(info.tcvrStats()->portNameToMediaLanes(), expectedMediaLaneMap);
  };

  // Step1: Start with discovered state
  setState(TransceiverStateMachineState::DISCOVERED, true /* multiPort */);

  // Step2: Add one of the two ports for port programming
  TransceiverManager::OverrideTcvrToPortAndProfile
      onePortInMultiPortTcvrToPortAndProfile_ = {
          {id_,
           {
               {portId1_, profile_},
           }}};
  transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
      onePortInMultiPortTcvrToPortAndProfile_);

  // Step3: Refresh multiple times to allow transition from discovered to
  // tcvr_programmed states
  for (auto statesToVerify :
       {TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED,
        TransceiverStateMachineState::TRANSCEIVER_READY,
        TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED}) {
    transceiverManager_->refreshStateMachines();
    EXPECT_EQ(transceiverManager_->getCurrentState(id_), statesToVerify);
  }

  const auto& stateMachine =
      transceiverManager_->getStateMachineForTesting(id_);
  EXPECT_TRUE(stateMachine.get_attribute(isIphyProgrammed));
  EXPECT_TRUE(stateMachine.get_attribute(isTransceiverProgrammed));

  // Step4: Set port status to up to allow transition to active state for port1
  // in next refresh
  transceiverManager_->setOverrideAgentPortStatusForTesting(
      true /* up */, true /* enabled */, false /* clearOnly */);
  transceiverManager_->refreshStateMachines(); // active
  EXPECT_EQ(
      transceiverManager_->getCurrentState(id_),
      TransceiverStateMachineState::ACTIVE);
  verifyPortToLaneMap(false /* bothPorts */);

  // Step5: Now add the 2nd port for programming
  transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
      overrideMultiPortTcvrToPortAndProfile_);
  transceiverManager_->setOverrideAgentPortStatusForTesting(
      true /* up */, true /* enabled */, false /* clearOnly */);

  // Step6: Refresh and make sure we go back from active to active states
  // through various steps
  for (auto statesToVerify :
       {TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED,
        TransceiverStateMachineState::TRANSCEIVER_READY,
        TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
        TransceiverStateMachineState::ACTIVE}) {
    transceiverManager_->refreshStateMachines();
    EXPECT_EQ(transceiverManager_->getCurrentState(id_), statesToVerify);
  }
  EXPECT_TRUE(stateMachine.get_attribute(isIphyProgrammed));
  EXPECT_TRUE(stateMachine.get_attribute(isTransceiverProgrammed));
  verifyPortToLaneMap(true /* bothPorts */);

  // Step7: Back to one port in the transceiver. We should again cycle from
  // active to active state through various transitions
  transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
      onePortInMultiPortTcvrToPortAndProfile_);
  transceiverManager_->setOverrideAgentPortStatusForTesting(
      true /* up */, true /* enabled */, false /* clearOnly */);
  for (auto statesToVerify :
       {TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED,
        TransceiverStateMachineState::TRANSCEIVER_READY,
        TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
        TransceiverStateMachineState::ACTIVE}) {
    transceiverManager_->refreshStateMachines();
    EXPECT_EQ(transceiverManager_->getCurrentState(id_), statesToVerify);
  }
  EXPECT_TRUE(stateMachine.get_attribute(isIphyProgrammed));
  EXPECT_TRUE(stateMachine.get_attribute(isTransceiverProgrammed));
  verifyPortToLaneMap(false /* bothPorts */);
}

TEST_F(TransceiverStateMachineTest, upgradeFirmware) {
  for (auto multiPort : {false, true}) {
    XLOG(INFO) << "Verifying upgradeFirmware for multiPort = " << multiPort;
    auto allStates = getAllStates();
    verifyStateMachine(
        {TransceiverStateMachineState::UPGRADING,
         TransceiverStateMachineState::INACTIVE,
         TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED,
         TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED,
         TransceiverStateMachineState::TRANSCEIVER_READY,
         TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
         TransceiverStateMachineState::ACTIVE},
        TransceiverStateMachineState::DISCOVERED /* expected state */,
        allStates,
        []() {} /* preUpdate */,
        [this]() {
          enableTransceiverFirmwareUpgradeTesting(true);
          triggerFirmwareUpgradeEvents();
        },
        [this]() {
          const auto& stateMachine =
              transceiverManager_->getStateMachineForTesting(id_);
          // We should set needToResetToDiscovered in the entry to UPGRADING
          // state. However, we revert it to false on entry into the NOT_PRESENT
          // state. In the verify function, the state should be NOT_PRESENT.
          // Hence expect needToResetToDiscovered to be false
          EXPECT_FALSE(stateMachine.get_attribute(needToResetToDiscovered));
          enableTransceiverFirmwareUpgradeTesting(false);
        } /* verify */,
        multiPort,
        TransceiverType::MOCK_CMIS,
        kUpgradeTransceiverFnStr);

    // Other states should not change even though we try to process the event
    verifyStateUnchanged(
        allStates,
        [this]() {
          enableTransceiverFirmwareUpgradeTesting(true);
          triggerFirmwareUpgradeEvents();
        } /* preUpdate */,
        []() {} /* stateUpdate */,
        [this]() {
          enableTransceiverFirmwareUpgradeTesting(false);
        } /* verify */,
        multiPort,
        TransceiverType::MOCK_CMIS,
        kUpgradeTransceiverFnStr);
    // Prepare for testing with next multiPort value
    cleanup();
  }
}
} // namespace facebook::fboss
