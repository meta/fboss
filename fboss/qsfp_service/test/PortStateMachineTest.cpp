/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

/*
 * PortStateMachineTest will exist alongside TransceiverStateMachineTest until
 * the PortManager migration is complete. The tests here will be used to
 * validate state machine transitions in Port Manager mode only. Any
 * transceiver-specific (e.g. SFF / CMIS) testing will be kept in
 * TransceiverStateMachineTest.cpp until we consolidate the files.
 */

#include "fboss/qsfp_service/test/MockPhyManager.h"
#include "fboss/qsfp_service/test/MockPortManager.h"

#include "fboss/qsfp_service/test/MockManagerConstructorArgs.h"
#include "fboss/qsfp_service/test/MockTransceivers.h"
#include "fboss/qsfp_service/test/TransceiverManagerTestHelper.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss {

ACTION(ThrowFbossError) {
  throw FbossError("Mock FbossError");
}

class PortStateMachineTest : public TransceiverManagerTestHelper {
 public:
  struct StateMachineStates {
    std::unordered_map<TransceiverID, TransceiverStateMachineState> tcvrStates;
    std::unordered_map<PortID, PortStateMachineState> portStates;

    bool operator==(const StateMachineStates& other) const {
      return tcvrStates == other.tcvrStates && portStates == other.portStates;
    }

    bool operator!=(const StateMachineStates& other) const {
      return !(*this == other);
    }

    bool hasPort(PortID portId) const {
      return portStates.find(portId) != portStates.end();
    }

    bool hasTcvr(TransceiverID tcvrId) const {
      return tcvrStates.find(tcvrId) != tcvrStates.end();
    }

    bool isMultiPort() const {
      return portStates.size() > 1;
    }

    bool isMultiTcvr() const {
      return tcvrStates.size() > 1;
    }

    bool hasTcvr() const {
      return !tcvrStates.empty();
    }
  };

  void SetUp() override {
    TransceiverManagerTestHelper::SetUp();
    // Setup GFlags for Full Port Manager Functionality
    gflags::SetCommandLineOptionWithMode(
        "port_manager_mode", "t", gflags::SET_FLAGS_DEFAULT);
    gflags::SetCommandLineOptionWithMode(
        "remediate_interval", "0", gflags::SET_FLAGS_DEFAULT);
    gflags::SetCommandLineOptionWithMode(
        "initial_remediate_interval", "0", gflags::SET_FLAGS_DEFAULT);
  }

  std::vector<QsfpModule*> getQsfpModules(bool multiTcvr) {
    return multiTcvr ? std::vector<QsfpModule*>{xcvr1_, xcvr2_}
                     : std::vector<QsfpModule*>{xcvr1_};
  }

  void setMockCmisPresence(bool isPresent, bool multiTcvr) {
    std::vector<QsfpModule*> qsfpModules = getQsfpModules(multiTcvr);
    for (auto qsfpModule : qsfpModules) {
      MockCmisModule* mockXcvr = static_cast<MockCmisModule*>(qsfpModule);
      auto xcvrImpl = mockXcvr->getTransceiverImpl();
      EXPECT_CALL(*xcvrImpl, detectTransceiver())
          .WillRepeatedly(::testing::Return(isPresent));
    }
  }

  void setMockCmisTransceiverReady(bool isReady, bool multiTcvr) {
    std::vector<QsfpModule*> qsfpModules = getQsfpModules(multiTcvr);
    for (auto qsfpModule : qsfpModules) {
      MockCmisModule* mockXcvr = static_cast<MockCmisModule*>(qsfpModule);
      EXPECT_CALL(*mockXcvr, ensureTransceiverReadyLocked(testing::_))
          .WillRepeatedly(::testing::Return(isReady));
    }
  }

  void detectPresence(bool multiTcvr) {
    xcvr1_->detectPresence();
    if (multiTcvr) {
      xcvr2_->detectPresence();
    }
  }

  std::vector<StateMachineStates> statePairsToStructs(
      const std::vector<
          std::pair<TransceiverStateMachineState, PortStateMachineState>>&
          statePairs,
      bool isMultiTcvr,
      bool isMultiPort,
      bool tcvrExists) {
    std::vector<StateMachineStates> statesVec;
    statesVec.reserve(statePairs.size());
    for (const auto& [tcvrState, portState] : statePairs) {
      statesVec.push_back(makeStates(
          tcvrState,
          portState,
          isMultiTcvr /* isMultiTcvr */,
          isMultiPort /* isMultiPort */,
          tcvrExists));
    }
    return statesVec;
  }

  // We default to CMIS module for testing.
  QsfpModule*
  overrideTransceiver(bool multiPort, bool isMock, TransceiverID tcvrId) {
    // Set port status to DOWN so that we can remove the transceiver correctly
    auto overrideCmisModule = [this, multiPort, isMock, tcvrId]() {
      // This override function use ids starting from 1
      transceiverManager_->overrideMgmtInterface(
          static_cast<int>(tcvrId) + 1,
          uint8_t(TransceiverModuleIdentifier::QSFP_PLUS_CMIS));
      if (isMock) {
        XLOG(INFO) << "Making Mock CMIS QSFP for " << tcvrId;
        cmisQsfpImpls_.push_back(
            std::make_unique<MockCmisTransceiverImpl>(
                tcvrId, transceiverManager_.get()));
        EXPECT_CALL(*cmisQsfpImpls_.back().get(), detectTransceiver())
            .WillRepeatedly(::testing::Return(true));
        return transceiverManager_->overrideTransceiverForTesting(
            tcvrId,
            std::make_unique<MockCmisModule>(
                transceiverManager_->getPortNames(tcvrId),
                cmisQsfpImpls_.back().get(),
                tcvrConfig_,
                transceiverManager_->getTransceiverName(tcvrId)));
      } else {
        XLOG(INFO) << "Making CMIS QSFP for " << tcvrId;
        std::unique_ptr<FakeTransceiverImpl> xcvrImpl;
        if (multiPort) {
          qsfpImpls_.push_back(
              std::make_unique<Cmis2x400GFr4Transceiver>(
                  tcvrId, transceiverManager_.get()));
        } else {
          qsfpImpls_.push_back(
              std::make_unique<Cmis200GTransceiver>(
                  tcvrId, transceiverManager_.get()));
        }
        return transceiverManager_->overrideTransceiverForTesting(
            tcvrId,
            std::make_unique<CmisModule>(
                transceiverManager_->getPortNames(tcvrId),
                qsfpImpls_.back().get(),
                tcvrConfig_,
                true /*supportRemediate*/,
                transceiverManager_->getTransceiverName(tcvrId)));
      }
    };

    Transceiver* xcvr = overrideCmisModule();
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

  void initManagers(
      FakeTestPlatformMappingType mappingType =
          FakeTestPlatformMappingType::STANDARD) {
    // Clear mock implementations from previous test iterations
    cmisQsfpImpls_.clear();
    qsfpImpls_.clear();

    // Determine numTransceivers and numPortsPerModule based on mapping type
    int numTransceivers = 1;
    int numPortsPerModule = 8;
    bool isMultiTcvr = false;

    switch (mappingType) {
      case FakeTestPlatformMappingType::STANDARD:
        numTransceivers = 1;
        numPortsPerModule = 8;
        isMultiTcvr = false;
        break;
      case FakeTestPlatformMappingType::DUAL_TRANSCEIVER:
        numTransceivers = 2;
        numPortsPerModule = 4;
        isMultiTcvr = true;
        break;
      case FakeTestPlatformMappingType::BACKPLANE:
      case FakeTestPlatformMappingType::XPHY_BACKPLANE:
        // Backplane mappings create a single port with specific chip configs
        numTransceivers = 1;
        numPortsPerModule = 1;
        isMultiTcvr = false;
        break;
    }

    // Create Platform Mapping Object
    const auto platformMapping = makeFakePlatformMapping(
        numTransceivers, numPortsPerModule, mappingType);
    const std::shared_ptr<const PlatformMapping> castedPlatformMapping =
        platformMapping;

    std::vector<PortID> portIds;
    for (auto& [portId, _] : platformMapping->getPlatformPorts()) {
      portIds.emplace_back(portId);
    }

    XLOG(ERR) << "Platform mapping has " << portIds.size()
              << " ports and mappingType is " << static_cast<int>(mappingType);

    // Create Threads Object
    const auto qsfpServiceThreads = makeQsfpServiceThreads(platformMapping);

    // Create PhyManager Object
    std::unique_ptr<MockPhyManager> phyManager =
        std::make_unique<MockPhyManager>(platformMapping.get());
    phyManager_ = phyManager.get();

    // Create Transceiver Manager
    transceiverManager_ = std::make_unique<MockWedgeManager>(
        numTransceivers,
        numPortsPerModule,
        platformMapping,
        qsfpServiceThreads);

    // Create Port Manager
    portManager_ = std::make_unique<MockPortManager>(
        transceiverManager_.get(),
        std::move(phyManager),
        castedPlatformMapping,
        qsfpServiceThreads);
    transceiverManager_->setPauseRemediation(600, nullptr /* evb */);

    // Set appropriate override for multi-tcvr ports
    if (isMultiTcvr) {
      transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
          overrideMultiTransceiverTcvrToPortAndProfile_);
    }

    // Set xphy no-tcvr port to profile override for backplane ports
    if (mappingType == FakeTestPlatformMappingType::XPHY_BACKPLANE) {
      portManager_->setOverrideXphyNoTcvrPortToProfileForTesting(
          overrideXphyNoTcvrPortToProfile_);
    }

    // Initialize managers
    transceiverManager_->init();
    portManager_->init();
  }

  void initializePortsThroughRefresh() {
    transceiverManager_->refreshTransceivers();
    portManager_->updateTransceiverPortStatus();
  }

  void disablePortsThroughRefresh() {
    portManager_->setOverrideAgentPortStatusForTesting({}, {});
    transceiverManager_->refreshTransceivers();
    portManager_->updateTransceiverPortStatus();
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
    portManager_->setOverrideAgentConfigAppliedInfoForTesting(
        configAppliedInfo);

    portManager_->triggerAgentConfigChangeEvent();
  }

  void setOverrideAgentStatusPortUp(bool multiTcvr, bool multiPort) {
    if (multiPort && !multiTcvr) {
      portManager_->setOverrideAgentPortStatusForTesting(
          {portId1_, portId3_}, {portId1_, portId3_});
    } else {
      portManager_->setOverrideAgentPortStatusForTesting(
          {portId1_}, {portId1_});
    }
  }

  void refreshAndTriggerProgramming() {
    transceiverManager_->refreshTransceivers();
    portManager_->updateTransceiverPortStatus();
    portManager_->updatePortActiveStatusInTransceiverManager();
    portManager_->detectTransceiverResetAndReinitializeCorrespondingDownPorts();
    portManager_->triggerProgrammingEvents();
    transceiverManager_->triggerProgrammingEvents();
  }

  // MultiPort optics requires two refresh cycles because leading ports are
  // responsible for calling programInternalPhyPorts. In the next refresh loop,
  // depending on the order of async events, the non-leading ports need to check
  // in with the leading ports status.
  void triggerOnlyIphyPortProgramming(bool isMultiTcvr, bool isMultiPort) {
    setupProgramExternalPhyPortFailMock(isMultiTcvr);
    portManager_->triggerProgrammingEvents();
    if (isMultiPort) {
      portManager_->triggerProgrammingEvents();
    }
    removeProgramExternalPhyPortFailMock(isMultiTcvr);
  }

  void
  setState(const StateMachineStates& states, bool multiPort, bool multiTcvr) {
    // Set tcvr & port overrides - assumes port up is desired, which can be
    // overridden below.
    if (multiPort && !multiTcvr) {
      transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
          overrideMultiPortTcvrToPortAndProfile_);
      portManager_->setOverrideAgentPortStatusForTesting(
          {portId1_, portId3_}, {portId1_, portId3_});
    } else if (!multiPort && multiTcvr) {
      transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
          overrideMultiTransceiverTcvrToPortAndProfile_);
      portManager_->setOverrideAgentPortStatusForTesting(
          {portId1_}, {portId1_});
    } else {
      transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
          overrideTcvrToPortAndProfile_);
      portManager_->setOverrideAgentPortStatusForTesting(
          {portId1_}, {portId1_});
    }

    // Current use case requires both ports to be in the same state.
    std::optional<TransceiverStateMachineState> tcvrState = std::nullopt;
    if (states.hasTcvr(tcvrId1_)) {
      tcvrState = states.tcvrStates.at(tcvrId1_);
    }
    auto portState = states.portStates.at(portId1_);

    // Handle cases where there is no transceiver (e.g., XPHY_BACKPLANE)
    if (!tcvrState.has_value()) {
      if (portState == PortStateMachineState::UNINITIALIZED) {
        // Default state - no action needed.
      } else if (portState == PortStateMachineState::INITIALIZED) {
        portManager_->updateTransceiverPortStatus();
      } else if (portState == PortStateMachineState::IPHY_PORTS_PROGRAMMED) {
        portManager_->updateTransceiverPortStatus();
        triggerOnlyIphyPortProgramming(multiTcvr, multiPort);
      } else if (portState == PortStateMachineState::XPHY_PORTS_PROGRAMMED) {
        portManager_->updateTransceiverPortStatus();
        for (int i = 0; i < 2; i++) {
          portManager_->triggerProgrammingEvents();
        }
      } else if (portState == PortStateMachineState::TRANSCEIVERS_PROGRAMMED) {
        portManager_->updateTransceiverPortStatus();
        for (int i = 0; i < 3; i++) {
          portManager_->triggerProgrammingEvents();
        }
      } else if (portState == PortStateMachineState::PORT_UP) {
        for (int i = 0; i < 5; ++i) {
          refreshAndTriggerProgramming();
        }
      } else if (portState == PortStateMachineState::PORT_DOWN) {
        portManager_->setOverrideAgentPortStatusForTesting(
            {}, {portId1_, portId3_});
        for (int i = 0; i < 5; ++i) {
          refreshAndTriggerProgramming();
        }
      } else {
        throw FbossError("Unsupported port state for no-transceiver mapping.");
      }
    } else if (
        tcvrState == TransceiverStateMachineState::NOT_PRESENT &&
        portState == PortStateMachineState::UNINITIALIZED) {
      // Default state - no action needed.
    } else if (
        tcvrState == TransceiverStateMachineState::NOT_PRESENT &&
        portState == PortStateMachineState::INITIALIZED) {
      setMockCmisPresence(false /* isPresent */, multiTcvr);
      detectPresence(multiTcvr);

      transceiverManager_->refreshTransceivers();
      portManager_->updateTransceiverPortStatus();
      portManager_->updatePortActiveStatusInTransceiverManager();
    } else if (
        tcvrState == TransceiverStateMachineState::NOT_PRESENT &&
        portState == PortStateMachineState::IPHY_PORTS_PROGRAMMED) {
      setMockCmisPresence(false /* isPresent */, multiTcvr);
      detectPresence(multiTcvr);

      transceiverManager_->refreshTransceivers();
      portManager_->updateTransceiverPortStatus();
      portManager_->updatePortActiveStatusInTransceiverManager();
      triggerOnlyIphyPortProgramming(multiTcvr, multiPort);
    } else if (
        tcvrState == TransceiverStateMachineState::NOT_PRESENT &&
        portState == PortStateMachineState::XPHY_PORTS_PROGRAMMED) {
      portManager_->updateTransceiverPortStatus();
      portManager_->updatePortActiveStatusInTransceiverManager();
      for (int i = 0; i < 3; i++) {
        portManager_->triggerProgrammingEvents();
      }
    } else if (
        tcvrState == TransceiverStateMachineState::DISCOVERED &&
        portState == PortStateMachineState::INITIALIZED) {
      initializePortsThroughRefresh();
    } else if (
        tcvrState == TransceiverStateMachineState::DISCOVERED &&
        portState == PortStateMachineState::IPHY_PORTS_PROGRAMMED) {
      initializePortsThroughRefresh();
      triggerOnlyIphyPortProgramming(multiTcvr, multiPort);
    } else if (
        tcvrState == TransceiverStateMachineState::DISCOVERED &&
        portState == PortStateMachineState::XPHY_PORTS_PROGRAMMED) {
      initializePortsThroughRefresh();
      for (int i = 0; i < 3; i++) {
        portManager_->triggerProgrammingEvents();
      }
    } else if (
        tcvrState == TransceiverStateMachineState::TRANSCEIVER_READY &&
        portState == PortStateMachineState::UNINITIALIZED) {
      transceiverManager_->refreshTransceivers();
      transceiverManager_->triggerProgrammingEvents();
    } else if (
        tcvrState == TransceiverStateMachineState::TRANSCEIVER_READY &&
        portState == PortStateMachineState::INITIALIZED) {
      initializePortsThroughRefresh();
      transceiverManager_->triggerProgrammingEvents();
    } else if (
        tcvrState == TransceiverStateMachineState::TRANSCEIVER_READY &&
        portState == PortStateMachineState::IPHY_PORTS_PROGRAMMED) {
      initializePortsThroughRefresh();
      transceiverManager_->triggerProgrammingEvents();
      triggerOnlyIphyPortProgramming(multiTcvr, multiPort);
    } else if (
        tcvrState == TransceiverStateMachineState::TRANSCEIVER_READY &&
        portState == PortStateMachineState::XPHY_PORTS_PROGRAMMED) {
      initializePortsThroughRefresh();
      transceiverManager_->triggerProgrammingEvents();
      for (int i = 0; i < 3; i++) {
        portManager_->triggerProgrammingEvents();
      }
    } else if (
        tcvrState == TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED &&
        portState == PortStateMachineState::XPHY_PORTS_PROGRAMMED) {
      initializePortsThroughRefresh();
      transceiverManager_->triggerProgrammingEvents();
      for (int i = 0; i < 3; i++) {
        portManager_->triggerProgrammingEvents();
      }
      transceiverManager_->triggerProgrammingEvents();
      if (multiTcvr) {
        transceiverManager_->triggerProgrammingEvents();
        transceiverManager_->triggerProgrammingEvents();
        transceiverManager_->triggerProgrammingEvents();
      }
    } else if (
        tcvrState == TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED &&
        portState == PortStateMachineState::TRANSCEIVERS_PROGRAMMED) {
      initializePortsThroughRefresh();
      transceiverManager_->triggerProgrammingEvents();
      portManager_->triggerProgrammingEvents();
      portManager_->triggerProgrammingEvents();
      transceiverManager_->triggerProgrammingEvents();
      portManager_->triggerProgrammingEvents();
      transceiverManager_->triggerProgrammingEvents();
      portManager_->triggerProgrammingEvents();
    } else if (
        tcvrState == TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED &&
        portState == PortStateMachineState::PORT_UP) {
      for (int i = 0; i < 5; ++i) {
        refreshAndTriggerProgramming();
      }
    } else if (
        tcvrState == TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED &&
        portState == PortStateMachineState::PORT_DOWN) {
      portManager_->setOverrideAgentPortStatusForTesting(
          {}, {portId1_, portId3_});
      for (int i = 0; i < 5; ++i) {
        refreshAndTriggerProgramming();
      }
    } else {
      // Ensure test setup is as expected.
      throw FbossError("Unsupported initial state.");
    }

    XLOG(INFO) << "setState changed state to "
               << logState(getCurrentState(multiPort, multiTcvr));
  }

  bool isMultiPort(const StateMachineStates& states) {
    return states.isMultiPort();
  }

  bool isMultiTcvr(const StateMachineStates& states) {
    return states.isMultiTcvr();
  }

  std::string logState(const StateMachineStates& states) {
    std::string result;
    if (states.hasTcvr(tcvrId1_)) {
      result = "tcvr1State=" +
          apache::thrift::util::enumNameSafe(states.tcvrStates.at(tcvrId1_));
    }

    if (states.hasTcvr(tcvrId2_)) {
      result += ", tcvr2State=" +
          apache::thrift::util::enumNameSafe(states.tcvrStates.at(tcvrId2_));
    }

    if (states.hasPort(portId1_)) {
      result += ", port1State=" +
          apache::thrift::util::enumNameSafe(states.portStates.at(portId1_));
    }

    if (states.hasPort(portId3_)) {
      result += ", port3State=" +
          apache::thrift::util::enumNameSafe(states.portStates.at(portId3_));
    }

    return result;
  }

  StateMachineStates getCurrentState(bool multiPort, bool multiTcvr) {
    StateMachineStates states;
    // Only get transceiver state if the port has a transceiver
    if (auto tcvrIdOpt =
            portManager_->getLowestIndexedStaticTransceiverForPort(portId1_)) {
      states.tcvrStates[*tcvrIdOpt] =
          transceiverManager_->getCurrentState(*tcvrIdOpt);
    }
    states.portStates[portId1_] = portManager_->getPortState(portId1_);
    if (multiPort) {
      states.portStates[portId3_] = portManager_->getPortState(portId3_);
    }
    if (multiTcvr) {
      states.tcvrStates[tcvrId2_] =
          transceiverManager_->getCurrentState(tcvrId2_);
    }
    return states;
  }

  std::optional<PortStateMachineState> optionalPortState(
      bool condition,
      PortStateMachineState state) {
    return condition ? std::optional{state} : std::nullopt;
  }

  std::optional<TransceiverStateMachineState> optionalTcvrState(
      bool condition,
      TransceiverStateMachineState state) {
    return condition ? std::optional{state} : std::nullopt;
  }

  // Helper function to validate current state matches expected state
  void assertCurrentStateEquals(const StateMachineStates& expectedState) {
    // Use hasTcvr() to determine multiTcvr, since isMultiTcvr() checks for > 1
    // and we may have 0 transceivers for backplane-only ports
    auto currentState = getCurrentState(
        expectedState.isMultiPort(),
        expectedState.hasTcvr() && expectedState.isMultiTcvr());
    ASSERT_EQ(currentState, expectedState)
        << "Intermediate state doesn't match expected, "
        << "expected=" << logState(expectedState)
        << ", actual=" << logState(currentState);
  }

  StateMachineStates makeStates(
      TransceiverStateMachineState tcvr1State,
      PortStateMachineState port1State,
      std::optional<PortStateMachineState> port3State = std::nullopt,
      std::optional<TransceiverStateMachineState> tcvr2State = std::nullopt) {
    StateMachineStates states;
    states.tcvrStates[tcvrId1_] = tcvr1State;
    states.portStates[portId1_] = port1State;
    if (port3State.has_value()) {
      states.portStates[portId3_] = port3State.value();
    }
    if (tcvr2State.has_value()) {
      states.tcvrStates[tcvrId2_] = tcvr2State.value();
    }
    return states;
  }

  StateMachineStates makeStates(
      TransceiverStateMachineState tcvr1State,
      PortStateMachineState port1State,
      bool isMultiTcvr,
      bool isMultiPort,
      bool tcvrExists = true) {
    StateMachineStates states;
    if (tcvrExists) {
      states.tcvrStates[tcvrId1_] = tcvr1State;
      if (isMultiTcvr) {
        states.tcvrStates[tcvrId2_] = tcvr1State;
      }
    }
    states.portStates[portId1_] = port1State;
    if (isMultiPort) {
      states.portStates[portId3_] = port1State;
    }
    return states;
  }

  // Helper function to determine if a mapping type has transceivers
  bool mappingTypeHasTcvr(FakeTestPlatformMappingType mappingType) const {
    return mappingType != FakeTestPlatformMappingType::XPHY_BACKPLANE &&
        mappingType != FakeTestPlatformMappingType::BACKPLANE;
  }

  void updateTransceiverActiveState(bool up, bool enabled, PortID portId) {
    PortStatus status;
    status.enabled() = enabled;
    status.up() = up;
    portManager_->updatePortActiveState({{portId, status}});
    // Sleep 1s to avoid the state machine handling the event too fast
    /* sleep override */
    sleep(1);
  }

  bool isTransceiverPresent(const TransceiverID& tcvrId) {
    const auto& presentTcvrs = transceiverManager_->getPresentTransceivers();
    return presentTcvrs.find(tcvrId) != presentTcvrs.end();
  }

  template <
      typename PRE_UPDATE_FN,
      typename STATE_UPDATE_FN,
      typename VERIFY_FN>
  void verifyStateMachine(
      const std::vector<StateMachineStates>& supportedStates,
      const StateMachineStates& expectedState,
      PRE_UPDATE_FN preUpdate,
      STATE_UPDATE_FN stateUpdate,
      VERIFY_FN verify,
      const std::string& updateStr,
      bool isMock = false,
      std::optional<FakeTestPlatformMappingType> mappingTypeOverride =
          std::nullopt) {
    for (const auto& states : supportedStates) {
      auto multiPort = isMultiPort(states);
      auto multiTcvr = isMultiTcvr(states);

      // Use override if provided, otherwise derive from isMultiTcvr
      auto mappingType = mappingTypeOverride.value_or(
          multiTcvr ? FakeTestPlatformMappingType::DUAL_TRANSCEIVER
                    : FakeTestPlatformMappingType::STANDARD);
      initManagers(mappingType);

      XLOG(INFO) << "Verifying Transceiver=0 state CHANGED by " << updateStr
                 << " from " << logState(states) << " to "
                 << logState(expectedState);

      // Always create a new transceiver so that we can make sure the state
      // can go back to the beginning state

      bool tcvrExists = mappingTypeHasTcvr(mappingType);
      if (tcvrExists) {
        xcvr1_ = overrideTransceiver(multiPort, isMock, tcvrId1_);
      }
      if (multiTcvr) {
        xcvr2_ = overrideTransceiver(multiPort, isMock, tcvrId2_);
      }
      setState(states, multiPort, multiTcvr);

      // Call preUpdate() before actual stateUpdate()
      preUpdate();

      // Trigger state update
      stateUpdate();

      // Check current state matches expected state
      auto curState = getCurrentState(multiPort, multiTcvr);
      EXPECT_EQ(curState, expectedState)
          << "Transceiver=0 state doesn't match after " << updateStr
          << ", preState=" << logState(states)
          << ", expected new state=" << logState(expectedState)
          << ", actual=" << logState(curState);

      // Verify the result after update finishes
      verify();

      ::testing::Mock::VerifyAndClearExpectations(transceiverManager_.get());
      ::testing::Mock::VerifyAndClearExpectations(xcvr1_);
      if (multiTcvr) {
        ::testing::Mock::VerifyAndClearExpectations(xcvr2_);
      }
    }
  }

  void setProgramCmisModuleExpectation(
      bool isProgrammed,
      ::testing::Sequence& s,
      bool multiPort = false) {
    int callTimes = isProgrammed ? 1 : 0;
    MockCmisModule* mockXcvr = static_cast<MockCmisModule*>(xcvr1_);
    auto portSpeed = cfg::PortSpeed::HUNDREDG;
    TransceiverPortState state{kPortName1, 0 /* startHostLane */, portSpeed, 4};
    EXPECT_CALL(*mockXcvr, customizeTransceiverLocked(state))
        .Times(callTimes)
        .InSequence(s);
    EXPECT_CALL(*mockXcvr, updateQsfpData(true)).Times(callTimes).InSequence(s);
    if (multiPort) {
      TransceiverPortState state2{
          kPortName3, 2 /* startHostLane */, portSpeed, 4};
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
      EXPECT_CALL(*mockXcvr, configureModule(2 /* startHostLane */))
          .Times(callTimes)
          .InSequence(s);
    }

    const auto& info = transceiverManager_->getTransceiverInfo(tcvrId1_);
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

  template <typename PRE_UPDATE_FN, typename VERIFY_FN>
  void verifyStateMachine(
      const std::vector<StateMachineStates>& supportedStates,
      StateMachineStates expectedState,
      TransceiverStateMachineEvent event,
      PortStateMachineEvent portEvent1,
      std::optional<PortStateMachineEvent> portEvent3,
      PRE_UPDATE_FN preUpdate,
      VERIFY_FN verify,
      bool holdTransceiversLockWhileUpdating = false,
      bool isMock = false) {
    auto stateUpdateFn = [this,
                          event,
                          portEvent1,
                          portEvent3,
                          holdTransceiversLockWhileUpdating]() {
      if (holdTransceiversLockWhileUpdating) {
        auto lockedTransceivers =
            transceiverManager_->getSynchronizedTransceivers().rlock();
        transceiverManager_->updateStateBlocking(tcvrId1_, event);
      } else {
        transceiverManager_->updateStateBlocking(tcvrId1_, event);
      }
      portManager_->updateStateBlocking(portId1_, portEvent1);
      if (portEvent3.has_value()) {
        portManager_->updateStateBlocking(portId3_, portEvent3.value());
      }
    };
    auto stateUpdateFnStr = folly::to<std::string>(
        "Event=", apache::thrift::util::enumNameSafe(event));
    verifyStateMachine(
        supportedStates,
        expectedState,
        preUpdate,
        stateUpdateFn,
        verify,
        stateUpdateFnStr,
        isMock);
  }

  template <
      typename PRE_UPDATE_FN,
      typename STATE_UPDATE_FN,
      typename VERIFY_FN>
  void verifyStateUnchanged(
      const std::vector<StateMachineStates>& supportedStates,
      PRE_UPDATE_FN preUpdate,
      STATE_UPDATE_FN stateUpdate,
      VERIFY_FN verify,
      const std::string& updateStr,
      FakeTestPlatformMappingType mappingType,
      bool isMock = false) {
    for (const auto& states : supportedStates) {
      auto multiPort = isMultiPort(states);
      auto multiTcvr = isMultiTcvr(states);

      initManagers(mappingType);

      XLOG(INFO) << "Verifying Transceiver=0 state UNCHANGED by " << updateStr
                 << " for " << logState(states) << ", multiPort=" << multiPort;
      // Always create a new transceiver so that we can make sure the state
      // can go back to the beginning state
      bool tcvrExists = mappingTypeHasTcvr(mappingType);
      if (tcvrExists) {
        xcvr1_ = overrideTransceiver(multiPort, isMock, tcvrId1_);
      }
      if (multiTcvr) {
        xcvr2_ = overrideTransceiver(multiPort, isMock, tcvrId2_);
      }
      setState(states, multiPort, multiTcvr);

      // Call preUpdate() before actual stateUpdate()
      preUpdate();

      // Trigger state update
      stateUpdate();

      // Check current state matches original state (unchanged)
      auto curState = getCurrentState(multiPort, multiTcvr);
      EXPECT_EQ(curState, states)
          << "Transceiver=0 state changed unexpectedly after " << updateStr
          << ", expected unchanged state=" << logState(states)
          << ", actual=" << logState(curState);

      // Verify the result after update finishes
      verify();

      ::testing::Mock::VerifyAndClearExpectations(transceiverManager_.get());
      ::testing::Mock::VerifyAndClearExpectations(xcvr1_);

      if (multiTcvr) {
        ::testing::Mock::VerifyAndClearExpectations(xcvr2_);
      }
    }
  }

  template <typename PRE_UPDATE_FN, typename VERIFY_FN>
  void verifyStateUnchanged(
      TransceiverStateMachineEvent tcvrEvent,
      PortStateMachineEvent portEvent1,
      std::optional<PortStateMachineEvent> portEvent3,
      const std::vector<StateMachineStates>& supportedStates,
      PRE_UPDATE_FN preUpdate,
      VERIFY_FN verify,
      bool isMock = false) {
    if (supportedStates.empty()) {
      return;
    }
    auto stateUpdateFn = [this, tcvrEvent, portEvent1, portEvent3]() {
      transceiverManager_->updateStateBlocking(tcvrId1_, tcvrEvent);
      portManager_->updateStateBlocking(portId1_, portEvent1);
      if (portEvent3.has_value()) {
        portManager_->updateStateBlocking(portId3_, portEvent3.value());
      }
    };
    auto stateUpdateFnStr = folly::to<std::string>(
        "Event=", apache::thrift::util::enumNameSafe(tcvrEvent));
    verifyStateUnchanged(
        supportedStates,
        preUpdate,
        stateUpdateFn,
        verify,
        stateUpdateFnStr,
        isMock);
  }

  void verifyXphyNeedResetDataPath(bool multiPort, bool expected) {
    EXPECT_EQ(portManager_->getXphyNeedResetDataPath(portId1_), expected);
    if (multiPort) {
      EXPECT_EQ(portManager_->getXphyNeedResetDataPath(portId3_), expected);
    }
  }

  void enableTransceiverFirmwareUpgradeTesting(bool enable) {
    gflags::SetCommandLineOptionWithMode(
        "firmware_upgrade_supported",
        enable ? "1" : "0",
        gflags::SET_FLAGS_DEFAULT);
    gflags::SetCommandLineOptionWithMode(
        "firmware_upgrade_on_tcvr_insert",
        enable ? "1" : "0",
        gflags::SET_FLAGS_DEFAULT);
    transceiverManager_->setForceFirmwareUpgradeForTesting(enable);
  }

  void verifyStateMachineAttributes(
      TransceiverID tcvrId,
      const std::map<std::string, bool>& expectedAttributes) {
    const auto& stateMachine =
        transceiverManager_->getStateMachineForTesting(tcvrId);

    for (const auto& [attrName, expectedValue] : expectedAttributes) {
      bool actualValue;
      if (attrName == "isTransceiverProgrammed") {
        actualValue = stateMachine.get_attribute(isTransceiverProgrammed);
      } else if (attrName == "needMarkLastDownTime") {
        actualValue = stateMachine.get_attribute(needMarkLastDownTime);
      } else if (attrName == "isIphyProgrammed") {
        actualValue = stateMachine.get_attribute(isIphyProgrammed);
      } else if (attrName == "isXphyProgrammed") {
        actualValue = stateMachine.get_attribute(isXphyProgrammed);
      } else if (attrName == "isTransceiverJustRemediated") {
        actualValue = stateMachine.get_attribute(isTransceiverJustRemediated);
      } else if (attrName == "needToResetToDiscovered") {
        actualValue = stateMachine.get_attribute(needToResetToDiscovered);
      } else if (attrName == "newTransceiverInsertedAfterInit") {
        actualValue =
            stateMachine.get_attribute(newTransceiverInsertedAfterInit);
      } else {
        FAIL() << "Unknown attribute name: " << attrName;
        continue;
      }

      EXPECT_EQ(actualValue, expectedValue)
          << "Attribute '" << attrName << "' mismatch for transceiver "
          << tcvrId << ". Expected: " << expectedValue
          << ", Actual: " << actualValue;
    }
  }

  void verifyStateMachineAttributes(
      bool multiTcvr,
      const std::map<std::string, bool>& expectedAttributes) {
    verifyStateMachineAttributes(tcvrId1_, expectedAttributes);
    if (multiTcvr) {
      verifyStateMachineAttributes(tcvrId2_, expectedAttributes);
    }
  }

  void triggerRemediateEvents(bool isMultiTcvr = false) {
    std::vector<TransceiverID> tcvrIds = {tcvrId1_};
    if (isMultiTcvr) {
      tcvrIds.push_back(tcvrId2_);
    }
    transceiverManager_->triggerRemediateEvents(tcvrIds);
  }

  struct TestModeConfig {
    FakeTestPlatformMappingType mappingType;
    bool isMultiTcvr;
    bool isMultiPort;
  };

  // Test mode combinations for most tests (excludes XPHY_BACKPLANE):
  // - STANDARD, single port per transceiver
  // - STANDARD, multi-port per transceiver
  // - DUAL_TRANSCEIVER, single port per transceiver (multi-tcvr)
  std::vector<TestModeConfig> getTestModeCombinations() {
    return {
        {FakeTestPlatformMappingType::STANDARD, false, false},
        {FakeTestPlatformMappingType::STANDARD, false, true},
        {FakeTestPlatformMappingType::DUAL_TRANSCEIVER, true, false},
    };
  }

  // Test mode combinations including XPHY_BACKPLANE (for specific tests):
  // - STANDARD, single port per transceiver
  // - STANDARD, multi-port per transceiver
  // - DUAL_TRANSCEIVER, single port per transceiver (multi-tcvr)
  // - XPHY_BACKPLANE, single port per transceiver
  std::vector<TestModeConfig> getTestModeCombinationsWithBackplane() {
    return {
        {FakeTestPlatformMappingType::STANDARD, false, false},
        {FakeTestPlatformMappingType::STANDARD, false, true},
        {FakeTestPlatformMappingType::DUAL_TRANSCEIVER, true, false},
        {FakeTestPlatformMappingType::XPHY_BACKPLANE, false, false},
    };
  }

  void logTestExecution(
      const std::string& testName,
      bool multiTcvrPort,
      bool multiPort) {
    XLOG(INFO) << "Verifying " << testName << " for multiPort = " << multiPort
               << ", multiTcvrPort = " << multiTcvrPort;
  }

  void setupProgramInternalPhyPortsFailMock(bool isMultiTcvr) {
    EXPECT_CALL(*portManager_, programInternalPhyPorts(tcvrId1_))
        .WillRepeatedly(ThrowFbossError());
    if (isMultiTcvr) {
      EXPECT_CALL(*portManager_, programInternalPhyPorts(tcvrId2_))
          .WillRepeatedly(ThrowFbossError());
    }
  }

  void removeProgramInternalPhyPortsFailMock(bool isMultiTcvr) {
    EXPECT_CALL(*portManager_, programInternalPhyPorts(tcvrId1_))
        .WillRepeatedly(::testing::Return());
    if (isMultiTcvr) {
      EXPECT_CALL(*portManager_, programInternalPhyPorts(tcvrId2_))
          .WillRepeatedly(::testing::Return());
    }
  }

  void setupProgramExternalPhyPortFailMock(bool isMultiTcvr) {
    EXPECT_CALL(
        *portManager_,
        programExternalPhyPort(
            ::testing::_, std::optional<TransceiverID>(tcvrId1_), false))
        .WillRepeatedly(ThrowFbossError());
    if (isMultiTcvr) {
      EXPECT_CALL(
          *portManager_,
          programExternalPhyPort(
              ::testing::_, std::optional<TransceiverID>(tcvrId2_), false))
          .WillRepeatedly(ThrowFbossError());
    }
  }

  void removeProgramExternalPhyPortFailMock(bool isMultiTcvr) {
    // Add new expectations that override the throwing ones (matched first due
    // to reverse order)
    EXPECT_CALL(
        *portManager_,
        programExternalPhyPort(
            ::testing::_, std::optional<TransceiverID>(tcvrId1_), false))
        .WillRepeatedly(::testing::Return());
    if (isMultiTcvr) {
      EXPECT_CALL(
          *portManager_,
          programExternalPhyPort(
              ::testing::_, std::optional<TransceiverID>(tcvrId2_), false))
          .WillRepeatedly(::testing::Return());
    }
  }

  // Port Manager
  MockPhyManager* phyManager_{};
  std::unique_ptr<MockPortManager> portManager_;

  // Tcvr Data
  QsfpModule* xcvr1_{};
  const TransceiverID tcvrId1_ = TransceiverID(0);
  QsfpModule* xcvr2_{};
  const TransceiverID tcvrId2_ = TransceiverID(1);
  const std::vector<TransceiverID> stableXcvrIds_ = {tcvrId1_};
  std::vector<std::unique_ptr<MockCmisTransceiverImpl>> cmisQsfpImpls_;

  // Port Data
  std::string kPortName1 = "eth1/1/1";
  std::string kPortName3 = "eth1/1/3";
  const PortID portId1_ = PortID(1);
  const PortID portId3_ = PortID(3);

  // Port Profiles (these depend on what's present in
  // FakeTestPlatformMapping.cpp)
  const cfg::PortProfileID profile_ =
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_OPTICAL;
  const cfg::PortProfileID multiPortProfile_ =
      cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_OPTICAL;
  const cfg::PortProfileID multiTcvrProfile_ =
      cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_COPPER;

  // TcvrToPortAndProfileOverrides
  const TransceiverManager::OverrideTcvrToPortAndProfile
      overrideTcvrToPortAndProfile_ = {
          {tcvrId1_,
           {
               {portId1_, profile_},
           }}};
  const TransceiverManager::OverrideTcvrToPortAndProfile
      overrideMultiPortTcvrToPortAndProfile_ = {
          {tcvrId1_,
           {
               {portId1_, multiPortProfile_},
               {portId3_, multiPortProfile_},
           }}};
  // TODO(smenta) – I also want to test on this platform mapping, but using
  // a normal profile.
  const TransceiverManager::OverrideTcvrToPortAndProfile
      overrideMultiTransceiverTcvrToPortAndProfile_ = {
          {tcvrId1_,
           {
               {portId1_, multiTcvrProfile_},
           }}};
  const TransceiverManager::OverrideTcvrToPortAndProfile
      emptyOverrideTcvrToPortAndProfile_ = {};

  // XphyNoTcvrPortToProfileOverride (for backplane ports with XPHY)
  const PortManager::OverrideXphyNoTcvrPortToProfile
      overrideXphyNoTcvrPortToProfile_ = {
          {portId1_, profile_},
      };
};

TEST_F(PortStateMachineTest, verifyStateSetupMacro) {
  // Unit test to guarantee setState works as expected in verifyStateChanged /
  // verifyStateUnchanged
  const std::string kTestName = "verifyStateSetupMacro";
  std::vector<std::pair<TransceiverStateMachineState, PortStateMachineState>>
      statePairs = {
          {TransceiverStateMachineState::NOT_PRESENT,
           PortStateMachineState::INITIALIZED},
          {TransceiverStateMachineState::NOT_PRESENT,
           PortStateMachineState::INITIALIZED},
          {TransceiverStateMachineState::NOT_PRESENT,
           PortStateMachineState::IPHY_PORTS_PROGRAMMED},
          {TransceiverStateMachineState::NOT_PRESENT,
           PortStateMachineState::XPHY_PORTS_PROGRAMMED},
          {TransceiverStateMachineState::DISCOVERED,
           PortStateMachineState::INITIALIZED},
          {TransceiverStateMachineState::DISCOVERED,
           PortStateMachineState::IPHY_PORTS_PROGRAMMED},
          {TransceiverStateMachineState::DISCOVERED,
           PortStateMachineState::XPHY_PORTS_PROGRAMMED},
          {TransceiverStateMachineState::TRANSCEIVER_READY,
           PortStateMachineState::UNINITIALIZED},
          {TransceiverStateMachineState::TRANSCEIVER_READY,
           PortStateMachineState::INITIALIZED},
          {TransceiverStateMachineState::TRANSCEIVER_READY,
           PortStateMachineState::IPHY_PORTS_PROGRAMMED},
          {TransceiverStateMachineState::TRANSCEIVER_READY,
           PortStateMachineState::XPHY_PORTS_PROGRAMMED},
          {TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
           PortStateMachineState::XPHY_PORTS_PROGRAMMED},
          {TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
           PortStateMachineState::TRANSCEIVERS_PROGRAMMED},
          {TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
           PortStateMachineState::PORT_UP},
          {TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
           PortStateMachineState::PORT_DOWN},
      };
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinationsWithBackplane()) {
    bool tcvrExists = mappingTypeHasTcvr(mappingType);
    for (auto [tcvrState, portState] : statePairs) {
      verifyStateUnchanged(
          {makeStates(
              tcvrState, portState, isMultiTcvr, isMultiPort, tcvrExists)},
          []() {} /* preUpdate */,
          []() {} /* stateUpdate */,
          []() {} /* verify */,
          kTestName,
          mappingType,
          true /* isMock */);
    }
  }
}

TEST_F(PortStateMachineTest, defaultStateAfterInit) {
  const std::string kTestName = "defaultStateAfterInit";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinationsWithBackplane()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    initManagers(mappingType);

    bool tcvrExists = mappingTypeHasTcvr(mappingType);

    // At this point, refresh cycle hasn't been run yet, so all ports should
    // be UNINITIALIZED. However, TransceiverManager creation triggers
    // refreshTransceivers(), which will discover present transceivers.
    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::DISCOVERED /* tcvr1State */,
        PortStateMachineState::UNINITIALIZED /* port1State */,
        isMultiTcvr /* isMultiTcvr */,
        isMultiPort /* isMultiPort */,
        tcvrExists /* tcvrExists */));
  }
}

TEST_F(PortStateMachineTest, agentEnablePorts) {
  const std::string kTestName = "agentEnablePorts";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinationsWithBackplane()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    bool tcvrExists = mappingTypeHasTcvr(mappingType);
    verifyStateMachine(
        {makeStates(
            TransceiverStateMachineState::NOT_PRESENT /* tcvr1State */,
            PortStateMachineState::UNINITIALIZED /* port1State */,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */,
            tcvrExists /* tcvrExists */)},
        makeStates(
            TransceiverStateMachineState::DISCOVERED /* tcvr1State */,
            PortStateMachineState::INITIALIZED /* port1State */,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */,
            tcvrExists /* tcvrExists */) /* expected state */,
        []() {} /* preUpdate */,
        [this]() { initializePortsThroughRefresh(); } /* stateUpdate */,
        [this, isMultiPort]() {
          verifyXphyNeedResetDataPath(isMultiPort, false /* expected */);
        } /* verify */,
        kTestName,
        false /* isMock */,
        mappingType);
  }
}

TEST_F(PortStateMachineTest, agentDisablePorts) {
  const std::string kTestName = "agentDisablePorts";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinationsWithBackplane()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    bool tcvrExists = mappingTypeHasTcvr(mappingType);

    std::vector<std::pair<TransceiverStateMachineState, PortStateMachineState>>
        statePairs = {
            {TransceiverStateMachineState::DISCOVERED,
             PortStateMachineState::INITIALIZED},
            {TransceiverStateMachineState::DISCOVERED,
             PortStateMachineState::IPHY_PORTS_PROGRAMMED},
            {TransceiverStateMachineState::DISCOVERED,
             PortStateMachineState::XPHY_PORTS_PROGRAMMED},
            {TransceiverStateMachineState::TRANSCEIVER_READY,
             PortStateMachineState::INITIALIZED},
            {TransceiverStateMachineState::TRANSCEIVER_READY,
             PortStateMachineState::IPHY_PORTS_PROGRAMMED},
            {TransceiverStateMachineState::TRANSCEIVER_READY,
             PortStateMachineState::XPHY_PORTS_PROGRAMMED},
            {TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
             PortStateMachineState::XPHY_PORTS_PROGRAMMED},
            {TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
             PortStateMachineState::TRANSCEIVERS_PROGRAMMED},
            {TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
             PortStateMachineState::PORT_UP},
            {TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
             PortStateMachineState::PORT_DOWN},
        };

    verifyStateMachine(
        statePairsToStructs(statePairs, isMultiTcvr, isMultiPort, tcvrExists),
        makeStates(
            TransceiverStateMachineState::TRANSCEIVER_READY /* tcvr1State */,
            PortStateMachineState::UNINITIALIZED /* port1State */,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */,
            tcvrExists)
        /* expected
      state */
        ,
        []() {} /* preUpdate */,
        [this]() {
          disablePortsThroughRefresh();
          portManager_->refreshStateMachines();
          portManager_->refreshStateMachines();
        } /* stateUpdate */,
        []() {} /* verify */,
        kTestName,
        true /* isMock */,
        mappingType);
  }
}

TEST_F(
    PortStateMachineTest,
    tryProgrammingTransceiverWithoutPhyProgrammingComplete) {
  const std::string kTestName =
      "tryProgrammingTransceiverWithoutPhyProgrammingComplete";

  std::vector<std::pair<TransceiverStateMachineState, PortStateMachineState>>
      statePairs = {
          {TransceiverStateMachineState::NOT_PRESENT,
           PortStateMachineState::INITIALIZED},
          {TransceiverStateMachineState::DISCOVERED,
           PortStateMachineState::INITIALIZED},
          {TransceiverStateMachineState::TRANSCEIVER_READY,
           PortStateMachineState::INITIALIZED},
      };

  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    verifyStateMachine(
        statePairsToStructs(
            statePairs, isMultiTcvr, isMultiPort, true /* tcvrExists */),
        makeStates(
            TransceiverStateMachineState::TRANSCEIVER_READY,
            PortStateMachineState::INITIALIZED,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */) /* expected
                                                   state */
        ,
        []() {} /* preUpdate */,
        [this]() {
          for (auto i = 0; i < 10; ++i) {
            portManager_->updateTransceiverPortStatus();
            transceiverManager_->triggerProgrammingEvents();
          }
        } /* stateUpdate */,
        []() {} /* verify */,
        kTestName,
        true /* isMock */,
        mappingType);
  }
}

TEST_F(PortStateMachineTest, furthestStatesWhenTransceiverNotPresent) {
  const std::string kTestName = "furthestStatesWhenTransceiverNotPresent";

  // Qsfp Service model still allows transceiver to progress to PROGRAMMED if
  // not present, due to i2c errors.
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    verifyStateMachine(
        {makeStates(
            TransceiverStateMachineState::NOT_PRESENT,
            PortStateMachineState::UNINITIALIZED,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */)},
        makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */) /* expected state */,
        [this, isMultiTcvr]() {
          setMockCmisPresence(false, isMultiTcvr);
          detectPresence(isMultiTcvr);
        } /* preUpdate */,
        [this]() {
          for (int i = 0; i < 5; ++i) {
            portManager_->refreshStateMachines();
          }
        } /* stateUpdate */,
        []() {} /* verify */,
        kTestName,
        true /* isMock */,
        mappingType);
  }
}

TEST_F(PortStateMachineTest, furthestStatesWhenTransceiverNotReady) {
  const std::string kTestName = "furthestStatesWhenTransceiverNotReady";

  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    verifyStateMachine(
        {makeStates(
            TransceiverStateMachineState::NOT_PRESENT,
            PortStateMachineState::UNINITIALIZED,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */)},
        makeStates(
            TransceiverStateMachineState::DISCOVERED,
            PortStateMachineState::XPHY_PORTS_PROGRAMMED,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */) /* expected state */,
        [this, isMultiTcvr]() {
          setMockCmisPresence(true, isMultiTcvr);
          setMockCmisTransceiverReady(false, isMultiTcvr);
        } /* preUpdate */,
        [this]() {
          for (int i = 0; i < 5; ++i) {
            portManager_->refreshStateMachines();
          }
        } /* stateUpdate */,
        []() {} /* verify */,
        kTestName,
        true /* isMock */,
        mappingType);
  }
}

TEST_F(PortStateMachineTest, fullSimpleRefreshCycle) {
  const std::string kTestName = "fullSimpleRefreshCycle";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinationsWithBackplane()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    bool tcvrExists = mappingTypeHasTcvr(mappingType);
    verifyStateMachine(
        {makeStates(
            TransceiverStateMachineState::NOT_PRESENT,
            PortStateMachineState::UNINITIALIZED,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */,
            tcvrExists)},
        makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */,
            tcvrExists) /* expected state */,
        []() {} /* preUpdate */,
        [this]() {
          for (int i = 0; i < 5; ++i) {
            refreshAndTriggerProgramming();
          }
        } /* stateUpdate */,
        [this, isMultiPort]() {
          verifyXphyNeedResetDataPath(isMultiPort, false /* expected */);
        } /* verify */,
        kTestName,
        true /* isMock */,
        mappingType);
  }
}

TEST_F(
    PortStateMachineTest,
    transceiverDownReinitializesPortThenTransceiverUp) {
  const std::string kTestName =
      "transceiverDownReinitializesPortThenTransceiverUp";

  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    initManagers(mappingType);

    // Set the original state.
    xcvr1_ = overrideTransceiver(isMultiPort, true, tcvrId1_);
    if (isMultiTcvr) {
      xcvr2_ = overrideTransceiver(isMultiPort, true, tcvrId2_);
    }
    setState(
        makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr,
            isMultiPort),
        isMultiPort,
        isMultiTcvr);

    // Mark transceiver as not present.
    setMockCmisPresence(false, isMultiTcvr);
    setMockCmisTransceiverReady(false, isMultiTcvr);
    transceiverManager_->refreshTransceivers();
    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::NOT_PRESENT,
        PortStateMachineState::PORT_UP,
        isMultiTcvr,
        isMultiPort));

    // Try re-initializing ports, but port should stay up until agent brings
    // port down.
    portManager_->detectTransceiverResetAndReinitializeCorrespondingDownPorts();
    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::NOT_PRESENT,
        PortStateMachineState::PORT_UP,
        isMultiTcvr,
        isMultiPort));

    // Agent brings port down.
    portManager_->setOverrideAllAgentPortStatusForTesting(
        false /* up */, true /* enabled */);
    portManager_->updateTransceiverPortStatus();
    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::NOT_PRESENT,
        PortStateMachineState::PORT_DOWN,
        isMultiTcvr,
        isMultiPort));

    // Not present workflow brings transceiver to TRANSCEIVER_READY even
    // though transceiver isn't present.
    portManager_->triggerProgrammingEvents();
    transceiverManager_->triggerProgrammingEvents();
    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::TRANSCEIVER_READY,
        PortStateMachineState::PORT_DOWN,
        isMultiTcvr,
        isMultiPort));

    // Transceiver is now present, gets discovered and progressed to the next
    // state.
    setMockCmisPresence(true, isMultiTcvr);
    setMockCmisTransceiverReady(true, isMultiTcvr);
    transceiverManager_->refreshTransceivers();
    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::DISCOVERED,
        PortStateMachineState::PORT_DOWN,
        isMultiTcvr,
        isMultiPort));

    // Port is detected as needing to re-initialize.
    portManager_->detectTransceiverResetAndReinitializeCorrespondingDownPorts();
    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::DISCOVERED,
        PortStateMachineState::INITIALIZED,
        isMultiTcvr,
        isMultiPort));

    // Ensure port status from agent doesn't progress any states.
    portManager_->updateTransceiverPortStatus();
    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::DISCOVERED,
        PortStateMachineState::INITIALIZED,
        isMultiTcvr,
        isMultiPort));

    // First round of programming.
    triggerOnlyIphyPortProgramming(isMultiTcvr, isMultiPort);
    transceiverManager_->triggerProgrammingEvents();
    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::TRANSCEIVER_READY,
        PortStateMachineState::IPHY_PORTS_PROGRAMMED,
        isMultiTcvr,
        isMultiPort));

    // Second round of programming.
    for (int i = 0; i < 3; i++) {
      refreshAndTriggerProgramming();
    }

    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
        PortStateMachineState::TRANSCEIVERS_PROGRAMMED,
        isMultiTcvr,
        isMultiPort));

    // Agent brings port down based on latest status.
    refreshAndTriggerProgramming();
    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
        PortStateMachineState::PORT_DOWN,
        isMultiTcvr,
        isMultiPort));

    // Agent sets port up.
    setOverrideAgentStatusPortUp(isMultiTcvr, isMultiPort);
    refreshAndTriggerProgramming();
    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
        PortStateMachineState::PORT_UP,
        isMultiTcvr,
        isMultiPort));
  }
}

TEST_F(PortStateMachineTest, agentConfigChangedColdBootOnPresentTcvr) {
  const std::string kTestName = "agentConfigChangedColdBootOnPresentTcvr";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);

    verifyStateMachine(
        {makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */)},
        makeStates(
            TransceiverStateMachineState::DISCOVERED,
            PortStateMachineState::UNINITIALIZED,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */) /* expected state */,
        []() {} /* preUpdate */,
        [this]() { triggerAgentConfigChanged(true); } /* stateUpdate */,
        [this, isMultiPort, isMultiTcvr]() {
          std::map<std::string, bool> expectedAttrValues = {
              {"isTransceiverProgrammed", false},
              {"needMarkLastDownTime", true}};
          verifyStateMachineAttributes(isMultiTcvr, expectedAttrValues);

          verifyXphyNeedResetDataPath(isMultiPort, true /* expected */);

          for (int i = 0; i < 4; i++) {
            portManager_->refreshStateMachines();
          }

          verifyXphyNeedResetDataPath(isMultiPort, false /* expected */);
        } /* verify */,
        kTestName,
        true /* isMock */,
        mappingType);
  }
}

TEST_F(PortStateMachineTest, agentConfigChangedWarmBootOnPresentTcvr) {
  const std::string kTestName = "agentConfigChangedWarmBootOnPresentTcvr";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);

    verifyStateMachine(
        {makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */)},
        makeStates(
            TransceiverStateMachineState::DISCOVERED,
            PortStateMachineState::UNINITIALIZED,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */)
        /* expected state
         */
        ,
        []() {} /* preUpdate */,
        [this]() { triggerAgentConfigChanged(false); } /* stateUpdate */,
        [this, isMultiTcvr, isMultiPort]() {
          // Enter DISCOVERED will also call `resetProgrammingAttributes`
          std::map<std::string, bool> expectedAttrValues = {
              {"isIphyProgrammed", false}, {"needMarkLastDownTime", true}};
          verifyStateMachineAttributes(isMultiTcvr, expectedAttrValues);

          verifyXphyNeedResetDataPath(isMultiPort, false /* expected */);
        } /* verify */,
        kTestName,
        true /* isMock */,
        mappingType);
  }
}

TEST_F(PortStateMachineTest, agentConfigChangedColdBootOnAbsentXcvr) {
  const std::string kTestName = "agentConfigChangedColdBootOnAbsentXcvr";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    verifyStateMachine(
        {makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */)},
        makeStates(
            TransceiverStateMachineState::NOT_PRESENT,
            PortStateMachineState::UNINITIALIZED,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */)
        /* expected state
         */
        ,
        [this, isMultiTcvr]() {
          setMockCmisPresence(false, isMultiTcvr);
          detectPresence(isMultiTcvr);
        } /* preUpdate */,
        [this]() { triggerAgentConfigChanged(true); } /* stateUpdate */,
        [this, isMultiTcvr, isMultiPort]() {
          std::map<std::string, bool> expectedAttrValues = {
              {"isTransceiverProgrammed", false},
              {"needMarkLastDownTime", true}};
          verifyStateMachineAttributes(isMultiTcvr, expectedAttrValues);

          verifyXphyNeedResetDataPath(isMultiPort, true /* expected */);

          for (int i = 0; i < 4; i++) {
            portManager_->refreshStateMachines();
          }

          verifyXphyNeedResetDataPath(isMultiPort, false /* expected */);
        } /* verify */,
        kTestName,
        true /* isMock */,
        mappingType);
  }
}

TEST_F(PortStateMachineTest, agentConfigChangedWarmBootOnAbsentXcvr) {
  const std::string kTestName = "agentConfigChangedWarmBootOnAbsentXcvr";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);

    verifyStateMachine(
        {makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */)},
        makeStates(
            TransceiverStateMachineState::NOT_PRESENT,
            PortStateMachineState::UNINITIALIZED,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */)
        /* expected state
         */
        ,
        [this, isMultiTcvr]() {
          setMockCmisPresence(false, isMultiTcvr);
          detectPresence(isMultiTcvr);
        } /* preUpdate */,
        [this]() { triggerAgentConfigChanged(false); } /* stateUpdate */,
        [this, isMultiTcvr, isMultiPort]() {
          // Enter DISCOVERED will also call `resetProgrammingAttributes`
          std::map<std::string, bool> expectedAttrValues = {
              {"isTransceiverProgrammed", false},
              {"needMarkLastDownTime", true}};
          verifyStateMachineAttributes(isMultiTcvr, expectedAttrValues);

          verifyXphyNeedResetDataPath(
              isMultiPort /* multiPort */, false /* expected */);
        } /* verify */,
        kTestName,
        true /* isMock */,
        mappingType);
  }
}

TEST_F(PortStateMachineTest, syncPortsOnRemovedTransceiver) {
  const std::string kTestName = "syncPortsOnRemovedTransceiver";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    verifyStateMachine(
        {makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */)},
        makeStates(
            TransceiverStateMachineState::NOT_PRESENT,
            PortStateMachineState::PORT_DOWN,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */),
        [this, isMultiTcvr]() {
          setMockCmisPresence(false, isMultiTcvr);

          for (auto module : getQsfpModules(isMultiTcvr)) {
            MockCmisModule* mockXcvr = static_cast<MockCmisModule*>(module);
            ::testing::Sequence s;
            // The first refreshLocked() should detect transceiver is removed
            // and dirty, need to updateQsfpData fully
            EXPECT_CALL(*mockXcvr, ensureOutOfReset());
            EXPECT_CALL(*mockXcvr, updateQsfpData(true));
            // Because transceiver is absent, we should use default ModuleStatus
            // to update cached transceiver info
            ModuleStatus moduleStatus;
            EXPECT_CALL(
                *mockXcvr, updateCachedTransceiverInfoLocked(moduleStatus));
          }
        } /* preUpdate */,
        [this, isMultiPort, isMultiTcvr]() {
          // Trigger active state change function just like wedge_agent calls
          // qsfp_service syncPorts(). Bring down the ports
          updateTransceiverActiveState(
              false /* up */, true /* enabled */, portId1_);
          if (isMultiPort) {
            updateTransceiverActiveState(
                false /* up */, true /* enabled */, portId3_);
          }
          // The refresh() will let TransceiverStateMachine trigger next event
          xcvr1_->refresh();
          if (isMultiTcvr) {
            xcvr2_->refresh();
          }
        } /* stateUpdate */,
        [this, isMultiTcvr]() {
          std::map<std::string, bool> expectedAttrValues = {
              {"isTransceiverProgrammed", false},
              {"needMarkLastDownTime", true}};
          verifyStateMachineAttributes(isMultiTcvr, expectedAttrValues);
        } /* verify */,
        kTestName,
        true /* isMock */,
        mappingType);
  }
}

TEST_F(PortStateMachineTest, verifyAllDownPortStatusSharedWithTransceiver) {
  std::string kTestName = "verifyAllDownPortStatusSharedWithTransceiver";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);

    verifyStateMachine(
        {makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */)},
        makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_DOWN,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */) /* expected state */,
        []() {} /* preUpdate */,
        [this]() {
          portManager_->setOverrideAllAgentPortStatusForTesting(false, true);
          for (int i = 0; i < 5; ++i) {
            refreshAndTriggerProgramming();
          }
        } /* stateUpdate */,
        [this, isMultiTcvr]() {
          std::vector<TransceiverID> tcvrIds = isMultiTcvr
              ? std::vector<TransceiverID>{tcvrId1_, tcvrId2_}
              : std::vector<TransceiverID>{tcvrId1_};
          for (const auto& tcvrId : tcvrIds) {
            auto [allPortsDown, _] =
                transceiverManager_->areAllPortsDown(tcvrId);
            ASSERT_TRUE(allPortsDown);
          }
        } /* verify */,
        kTestName,
        true /* isMock */,
        mappingType);
  }
}

TEST_F(
    PortStateMachineTest,
    verifyOneDownPortMultiPortStatusSharedWithTransceiver) {
  std::string kTestName =
      "verifyOneDownPortMultiPortStatusSharedWithTransceiver";
  XLOG(INFO) << "Verifying " << kTestName;
  initManagers();

  verifyStateMachine(
      {makeStates(
          TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
          PortStateMachineState::PORT_UP,
          PortStateMachineState::PORT_UP)},
      makeStates(
          TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
          PortStateMachineState::PORT_DOWN,
          PortStateMachineState::PORT_UP) /* expected state */,
      []() {} /* preUpdate */,
      [this]() {
        portManager_->setOverrideAgentPortStatusForTesting(
            {portId3_}, {portId1_, portId3_});
        for (int i = 0; i < 5; ++i) {
          refreshAndTriggerProgramming();
        }
      } /* stateUpdate */,
      [this]() {
        auto [allPortsDown, downPorts] =
            transceiverManager_->areAllPortsDown(tcvrId1_);
        ASSERT_FALSE(allPortsDown);
        ASSERT_EQ(downPorts.size(), 1);
        ASSERT_EQ(downPorts[0], kPortName1);
      } /* verify */,
      kTestName,
      true /* isMock */);
}

TEST_F(PortStateMachineTest, verifyDetectingPortStatusOnResetTransceiver) {
  const std::string kTestName = "verifyDetectingPortStatusOnResetTransceiver";

  auto verifyTcvrPortStatus =
      [this](
          PortStateMachineState portState, bool isMultiTcvr, bool isMultiPort) {
        // Verify port status for tcvrId1_
        bool allPortsDown = false;
        std::vector<std::string> downPorts;
        std::tie(allPortsDown, downPorts) =
            transceiverManager_->areAllPortsDown(tcvrId1_);

        if (portState == PortStateMachineState::PORT_UP) {
          // Port is up, so allPortsDown should be false
          ASSERT_FALSE(allPortsDown);
          ASSERT_EQ(downPorts.size(), 0);
        } else {
          // Port is down, so allPortsDown should be true
          ASSERT_TRUE(allPortsDown);
          int expectedDownPorts = isMultiPort ? 2 : 1;
          ASSERT_EQ(downPorts.size(), expectedDownPorts);
          ASSERT_NE(
              std::find(downPorts.begin(), downPorts.end(), kPortName1),
              downPorts.end());
          if (isMultiPort) {
            ASSERT_NE(
                std::find(downPorts.begin(), downPorts.end(), kPortName3),
                downPorts.end());
          }
        }

        // Verify port status for tcvrId2_ if multi-transceiver
        // this doeesn't look correct
        if (isMultiTcvr) {
          std::tie(allPortsDown, downPorts) =
              transceiverManager_->areAllPortsDown(tcvrId2_);

          if (portState == PortStateMachineState::PORT_UP) {
            //
            EXPECT_FALSE(allPortsDown);
            ASSERT_EQ(downPorts.size(), 0);
          } else {
            ASSERT_TRUE(allPortsDown);
            ASSERT_EQ(downPorts.size(), 1);
            // TcvrToPortInfo stores mapped information from
            // getDualTransceiverPortProfileIDs
            ASSERT_EQ(downPorts[0], "eth1/2/1");
          }
        }
      };

  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    // Test both PORT_UP and PORT_DOWN scenarios
    for (auto portState :
         {PortStateMachineState::PORT_UP, PortStateMachineState::PORT_DOWN}) {
      std::string portStateName =
          portState == PortStateMachineState::PORT_UP ? "PortUp" : "PortDown";
      logTestExecution(
          kTestName + "_" + portStateName, isMultiTcvr, isMultiPort);
      initManagers(mappingType);

      // Set initial states.
      xcvr1_ = overrideTransceiver(isMultiPort, true, tcvrId1_);
      if (isMultiTcvr) {
        xcvr2_ = overrideTransceiver(isMultiPort, true, tcvrId2_);
      }
      setState(
          makeStates(
              TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
              portState,
              isMultiTcvr,
              isMultiPort),
          isMultiPort,
          isMultiTcvr);

      // Mark transceiver as not present.
      setMockCmisPresence(false, isMultiTcvr);
      setMockCmisTransceiverReady(false, isMultiTcvr);

      // Refresh cycle a few more times and ensure that port stays in expected
      // state, transceiver reaches transceiver ready, but transceiver isn't
      // present.
      refreshAndTriggerProgramming();
      assertCurrentStateEquals(makeStates(
          TransceiverStateMachineState::TRANSCEIVER_READY,
          portState,
          isMultiTcvr,
          isMultiPort));
      ASSERT_FALSE(isTransceiverPresent(tcvrId1_));
      if (isMultiTcvr) {
        ASSERT_FALSE(isTransceiverPresent(tcvrId2_));
      }

      // Ensure transceiver manager returns updated port status data.
      verifyTcvrPortStatus(portState, isMultiTcvr, isMultiPort);

      // Mark transceiver as present.
      setMockCmisPresence(true, isMultiTcvr);
      setMockCmisTransceiverReady(true, isMultiTcvr);

      for (int i = 0; i < 5; i++) {
        refreshAndTriggerProgramming();
      }

      auto expectedTcvrState = portState == PortStateMachineState::PORT_UP
          ? TransceiverStateMachineState::TRANSCEIVER_READY
          : TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED;
      assertCurrentStateEquals(
          makeStates(expectedTcvrState, portState, isMultiTcvr, isMultiPort));
      ASSERT_TRUE(isTransceiverPresent(tcvrId1_));
      if (isMultiTcvr) {
        ASSERT_TRUE(isTransceiverPresent(tcvrId2_));
      }

      // Ensure transceiver manager returns updated port status data.
      verifyTcvrPortStatus(portState, isMultiTcvr, isMultiPort);
    }
  }
}

TEST_F(PortStateMachineTest, ensureNoFwUpgradeOnPortUpAndI2cConnectionIssues) {
  /*
   * This test is meant to mimic the case where an inserted transceiver with
   * ports up starts suddenly having i2c connection issues which makes
   * TransceiverManager think it's not present. We need to ensure we don't
   * reprogram or trigger firmware upgrade on accident.
   */
  const std::string kTestName =
      "ensureNoFwUpgradeOnPortUpAndI2cConnectionIssues";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    initManagers(mappingType);

    // Execute refresh loop to get to TRANSCEIVER_PROGRAMMED / PORT_UP.
    xcvr1_ = overrideTransceiver(isMultiPort, true, tcvrId1_);
    if (isMultiTcvr) {
      xcvr2_ = overrideTransceiver(isMultiPort, true, tcvrId2_);
    }
    setState(
        makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr,
            isMultiPort),
        isMultiPort,
        isMultiTcvr);

    gflags::SetCommandLineOptionWithMode(
        "firmware_upgrade_on_tcvr_insert", "t", gflags::SET_FLAGS_DEFAULT);
    gflags::SetCommandLineOptionWithMode(
        "firmware_upgrade_supported", "t", gflags::SET_FLAGS_DEFAULT);

    // Mark transceiver as not present.
    setMockCmisPresence(false, isMultiTcvr);
    setMockCmisTransceiverReady(false, isMultiTcvr);

    // Refresh cycle a few more times and ensure that port stays up,
    // transceiver reaches transceiver ready, but transceiver isn't present.
    for (int i = 0; i < 5; i++) {
      refreshAndTriggerProgramming();
    }
    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::TRANSCEIVER_READY,
        PortStateMachineState::PORT_UP,
        isMultiTcvr,
        isMultiPort));
    ASSERT_FALSE(isTransceiverPresent(tcvrId1_));
    if (isMultiTcvr) {
      ASSERT_FALSE(isTransceiverPresent(tcvrId2_));
    }

    // Mark transceiver as present.
    setMockCmisPresence(true, isMultiTcvr);
    setMockCmisTransceiverReady(true, isMultiTcvr);

    // Mocking aggregate refreshTransceivers() call to determine which
    // transceivers are candidates for fw upgrade.
    std::unordered_set<TransceiverID> emptyTcvrs{};
    const auto& refreshedTcvrs =
        static_cast<TransceiverManager*>(transceiverManager_.get())
            ->refreshTransceivers(emptyTcvrs);
    const auto& tcvrsForFwUpgrade =
        transceiverManager_->findPotentialTcvrsForFirmwareUpgrade(
            refreshedTcvrs);
    ASSERT_FALSE(tcvrsForFwUpgrade.contains(tcvrId1_));
    if (isMultiTcvr) {
      ASSERT_FALSE(tcvrsForFwUpgrade.contains(tcvrId2_));
    }

    // Ensure the state is as expected.
    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::DISCOVERED,
        PortStateMachineState::PORT_UP,
        isMultiTcvr,
        isMultiPort));

    // Ensure all ports are still up. If allPortsDown is false while
    // TransceiverManager was refreshingTransceivers, the
    // FLAGS_firmware_upgrade_on_tcvr_insert case should have been hit.
    auto [allPortsDown, _] = transceiverManager_->areAllPortsDown(tcvrId1_);
    ASSERT_FALSE(allPortsDown);
    if (isMultiTcvr) {
      auto [allPortsDown2, _] = transceiverManager_->areAllPortsDown(tcvrId2_);
      ASSERT_FALSE(allPortsDown2);
    }

    // Refresh a few more times and guarantee transceiver reaches
    // TRANSCEIVER_READY
    for (int i = 0; i < 5; i++) {
      refreshAndTriggerProgramming();
    }
    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::TRANSCEIVER_READY,
        PortStateMachineState::PORT_UP,
        isMultiTcvr,
        isMultiPort));
    ASSERT_TRUE(isTransceiverPresent(tcvrId1_));
    if (isMultiTcvr) {
      ASSERT_TRUE(isTransceiverPresent(tcvrId2_));
    }

    gflags::SetCommandLineOptionWithMode(
        "firmware_upgrade_on_tcvr_insert", "f", gflags::SET_FLAGS_DEFAULT);
    gflags::SetCommandLineOptionWithMode(
        "firmware_upgrade_supported", "f", gflags::SET_FLAGS_DEFAULT);
  }
}

TEST_F(PortStateMachineTest, ensureFwUpgradeOnTcvrInsertWithPortsDown) {
  /*
   * This test verifies that when firmware_upgrade_on_tcvr_insert and
   * firmware_upgrade_supported flags are enabled, and a transceiver is
   * newly inserted (after init) with all ports down, it is correctly
   * selected as a candidate for firmware upgrade.
   */
  const std::string kTestName = "ensureFwUpgradeOnTcvrInsertWithPortsDown";

  // We need to capture the firmware upgrade candidates during stateUpdate
  std::unordered_set<TransceiverID> tcvrsForFwUpgrade;

  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);

    verifyStateMachine(
        {makeStates(
            TransceiverStateMachineState::NOT_PRESENT,
            PortStateMachineState::UNINITIALIZED,
            isMultiTcvr,
            isMultiPort)},
        makeStates(
            TransceiverStateMachineState::DISCOVERED /* tcvr1State */,
            PortStateMachineState::INITIALIZED /* port1State */,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */) /* expected state */,
        [this, isMultiTcvr, isMultiPort]() {
          // Enable firmware upgrade flags
          gflags::SetCommandLineOptionWithMode(
              "firmware_upgrade_on_tcvr_insert",
              "t",
              gflags::SET_FLAGS_DEFAULT);
          gflags::SetCommandLineOptionWithMode(
              "firmware_upgrade_supported", "t", gflags::SET_FLAGS_DEFAULT);
          // To avoid state expectation on i2c failure case.
          setupProgramInternalPhyPortsFailMock(isMultiTcvr);
          setupProgramExternalPhyPortFailMock(isMultiTcvr);

          // Mark transceiver as not present
          setMockCmisPresence(false, isMultiTcvr);
          setMockCmisTransceiverReady(false, isMultiTcvr);
          detectPresence(isMultiTcvr);

          // Refresh state machines a few times to let state settle
          for (int i = 0; i < 3; i++) {
            portManager_->refreshStateMachines();
          }
          assertCurrentStateEquals(makeStates(
              TransceiverStateMachineState::TRANSCEIVER_READY /* tcvr1State */,
              PortStateMachineState::INITIALIZED /* port1State */,
              isMultiTcvr /* isMultiTcvr */,
              isMultiPort /* isMultiPort */));

          // Verify TransceiverManager is fully initialized
          ASSERT_TRUE(transceiverManager_->isFullyInitialized());
        } /* preUpdate */,
        [this, &tcvrsForFwUpgrade, isMultiTcvr]() {
          // Mark transceiver as present (simulating insertion after init)
          setMockCmisPresence(true, isMultiTcvr);
          setMockCmisTransceiverReady(true, isMultiTcvr);

          removeProgramInternalPhyPortsFailMock(isMultiTcvr);
          removeProgramExternalPhyPortFailMock(isMultiTcvr);

          // Run refreshTransceivers to detect the newly inserted transceiver
          transceiverManager_->refreshTransceivers();

          // Check newTransceiverInsertedAfterInit attribute is set
          std::map<std::string, bool> finalAttr = {
              {"newTransceiverInsertedAfterInit", true}};
          verifyStateMachineAttributes(isMultiTcvr, finalAttr);

          // Check if transceivers are candidates for firmware upgrade
          std::vector<TransceiverID> refreshedTcvrs{tcvrId1_};
          if (isMultiTcvr) {
            refreshedTcvrs.push_back(tcvrId2_);
          }
          tcvrsForFwUpgrade =
              transceiverManager_->findPotentialTcvrsForFirmwareUpgrade(
                  refreshedTcvrs);
          // Verify that the transceivers are selected for firmware upgrade
          ASSERT_TRUE(tcvrsForFwUpgrade.contains(tcvrId1_))
              << "Transceiver " << tcvrId1_
              << " should be selected for fw upgrade on insert with ports down";
          if (isMultiTcvr) {
            ASSERT_TRUE(tcvrsForFwUpgrade.contains(tcvrId2_))
                << "Transceiver " << tcvrId2_
                << " should be selected for fw upgrade on insert with ports "
                   "down";
          }
        } /* stateUpdate */,
        [this, isMultiTcvr, &tcvrsForFwUpgrade]() {
          // Reset flags
          gflags::SetCommandLineOptionWithMode(
              "firmware_upgrade_on_tcvr_insert",
              "f",
              gflags::SET_FLAGS_DEFAULT);
          gflags::SetCommandLineOptionWithMode(
              "firmware_upgrade_supported", "f", gflags::SET_FLAGS_DEFAULT);
        } /* verify */,
        kTestName,
        true /* isMock */,
        mappingType);
  }
}

// Basic Coverage of TransceiverRemediation logic coordinated by
// PortStateMachine - per-tcvrType testing is covered in
// TransceiverStateMachineTest
// TODO(smenta) – Refactor for multiPort
TEST_F(PortStateMachineTest, CheckCmisTransceiverRemediatedSuccess) {
  const std::string kTestName = "CheckTransceiverRemediatedSuccess";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    verifyStateMachine(
        {makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */)},
        makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_DOWN,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */) /* expected state */,
        [this, isMultiTcvr]() {
          transceiverManager_->setPauseRemediation(0, nullptr /* evb */);

          MockCmisTransceiverImpl* xcvrImpl1 =
              static_cast<MockCmisTransceiverImpl*>(
                  cmisQsfpImpls_[tcvrId1_].get());
          EXPECT_CALL(*xcvrImpl1, triggerQsfpHardReset()).Times(1);
          if (isMultiTcvr) {
            MockCmisTransceiverImpl* xcvrImpl2 =
                static_cast<MockCmisTransceiverImpl*>(
                    cmisQsfpImpls_[tcvrId2_].get());
            EXPECT_CALL(*xcvrImpl2, triggerQsfpHardReset()).Times(1);
          }
        } /* preUpdate */,
        [this, isMultiTcvr, isMultiPort]() {
          for (int i = 0; i < 5; i++) {
            refreshAndTriggerProgramming();
          }
          assertCurrentStateEquals(makeStates(
              TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
              PortStateMachineState::PORT_UP,
              isMultiTcvr,
              isMultiPort));

          portManager_->setOverrideAllAgentPortStatusForTesting(
              false /* isUp */, true /* isEnabled */);

          // First, trigger port status update to mark lastDownTime.
          // This calls updateTransceiverPortStatus which updates port states
          // and marks lastDownTime via updateTcvrLastDownTime.
          portManager_->updateTransceiverPortStatus();
          portManager_->updatePortActiveStatusInTransceiverManager();

          // Give 1s buffer when comparing lastDownTime_ in shouldRemediate()
          // The sleep must be AFTER the port status update which marks
          // lastDownTime, but BEFORE remediation is triggered.
          /* sleep override */
          sleep(1);

          // Now call refreshStateMachines which will trigger remediation.
          // The port status is already updated from the call above.
          portManager_->refreshStateMachines();
          EXPECT_TRUE(xcvr1_->getDirty_());
          if (isMultiTcvr) {
            EXPECT_TRUE(xcvr2_->getDirty_());
          }

          std::map<std::string, bool> expectedAttrValues = {
              {"isTransceiverProgrammed", false},
              {"isTransceiverJustRemediated", true},
              {"needMarkLastDownTime", false}};
          verifyStateMachineAttributes(isMultiTcvr, expectedAttrValues);

          assertCurrentStateEquals(makeStates(
              TransceiverStateMachineState::DISCOVERED,
              PortStateMachineState::PORT_DOWN,
              isMultiTcvr,
              isMultiPort));

          refreshAndTriggerProgramming();
          assertCurrentStateEquals(makeStates(
              TransceiverStateMachineState::TRANSCEIVER_READY,
              PortStateMachineState::PORT_DOWN,
              isMultiTcvr,
              isMultiPort));

          refreshAndTriggerProgramming();
          assertCurrentStateEquals(makeStates(
              TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
              PortStateMachineState::PORT_DOWN,
              isMultiTcvr,
              isMultiPort));
        } /* stateUpdate */,
        [this, isMultiTcvr]() {
          EXPECT_FALSE(xcvr1_->getDirty_());
          if (isMultiTcvr) {
            EXPECT_FALSE(xcvr2_->getDirty_());
          }
          std::map<std::string, bool> expectedAttrValues = {
              {"isTransceiverProgrammed", true},
              {"isTransceiverJustRemediated", false}};
          verifyStateMachineAttributes(isMultiTcvr, expectedAttrValues);
        } /* verify */,
        kTestName,
        true /* isMock */,
        mappingType);
  }
}

TEST_F(PortStateMachineTest, CheckCmisTransceiverRemediatedFailed) {
  const std::string kTestName = "CheckTransceiverRemediatedFailed";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    verifyStateUnchanged(
        {makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_DOWN,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */)},
        [this, isMultiTcvr, isMultiPort]() {
          transceiverManager_->setPauseRemediation(0, nullptr /* evb */);
          sleep(1);

          MockCmisTransceiverImpl* xcvrImpl1 =
              static_cast<MockCmisTransceiverImpl*>(
                  cmisQsfpImpls_[tcvrId1_].get());
          // throw FbossError on first call, then succeed on the next
          EXPECT_CALL(*xcvrImpl1, triggerQsfpHardReset())
              .Times(2)
              .WillOnce(ThrowFbossError());
          if (isMultiTcvr) {
            MockCmisTransceiverImpl* xcvrImpl2 =
                static_cast<MockCmisTransceiverImpl*>(
                    cmisQsfpImpls_[tcvrId2_].get());
            EXPECT_CALL(*xcvrImpl2, triggerQsfpHardReset())
                .Times(2)
                .WillOnce(ThrowFbossError());
          }

          // TODO(smenta) – Re-enable expectations in multiTcvr mode (avoiding
          // because it's a messy).
          if (!isMultiTcvr) {
            MockCmisModule* mockXcvr1 = static_cast<MockCmisModule*>(xcvr1_);
            ::testing::Sequence s1;

            // Expect updateQsfpData and updateCachedTransceiverInfoLocked to be
            // called from refreshStateMachines() we do in verify() below
            EXPECT_CALL(*mockXcvr1, updateQsfpData(true)).Times(1);
            EXPECT_CALL(*mockXcvr1, updateQsfpData(false)).Times(3);
            EXPECT_CALL(
                *mockXcvr1, updateCachedTransceiverInfoLocked(::testing::_))
                .Times(2)
                .InSequence(s1);
            setProgramCmisModuleExpectation(true, s1, isMultiPort);
          }

          portManager_->setOverrideAllAgentPortStatusForTesting(
              false /* isUp */, true /* isEnabled */);
          portManager_->updateTransceiverPortStatus();
          portManager_->updatePortActiveStatusInTransceiverManager();
        } /* preUpdate */,
        [this, isMultiTcvr]() {
          triggerRemediateEvents(isMultiTcvr);
        } /* stateUpdate */,
        [this, isMultiTcvr, isMultiPort]() {
          EXPECT_FALSE(xcvr1_->getDirty_());
          if (isMultiTcvr) {
            EXPECT_FALSE(xcvr2_->getDirty_());
          }

          std::map<std::string, bool> expectedAttrValues = {
              {"isTransceiverProgrammed", true},
              {"isTransceiverJustRemediated", false}};
          verifyStateMachineAttributes(isMultiTcvr, expectedAttrValues);

          assertCurrentStateEquals(makeStates(
              TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
              PortStateMachineState::PORT_DOWN,
              isMultiTcvr,
              isMultiPort));

          // Try again, it should succeed.
          triggerRemediateEvents(isMultiTcvr);
          EXPECT_TRUE(xcvr1_->getDirty_());
          if (isMultiTcvr) {
            EXPECT_TRUE(xcvr2_->getDirty_());
          }

          assertCurrentStateEquals(makeStates(
              TransceiverStateMachineState::DISCOVERED,
              PortStateMachineState::PORT_DOWN,
              isMultiTcvr,
              isMultiPort));

          std::map<std::string, bool> afterRemediationAttrValues = {
              {"isTransceiverProgrammed", false},
              {"isTransceiverJustRemediated", true},
              {"needMarkLastDownTime", false}};
          verifyStateMachineAttributes(isMultiTcvr, afterRemediationAttrValues);

          portManager_->refreshStateMachines();
          assertCurrentStateEquals(makeStates(
              TransceiverStateMachineState::TRANSCEIVER_READY,
              PortStateMachineState::PORT_DOWN,
              isMultiTcvr,
              isMultiPort));
          EXPECT_FALSE(xcvr1_->getDirty_());
          if (isMultiTcvr) {
            EXPECT_FALSE(xcvr2_->getDirty_());
          }

          portManager_->refreshStateMachines();
          assertCurrentStateEquals(makeStates(
              TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
              PortStateMachineState::PORT_DOWN,
              isMultiTcvr,
              isMultiPort));

          std::map<std::string, bool> afterProgrammingAttrValues = {
              {"isTransceiverProgrammed", true},
              {"isTransceiverJustRemediated", false}};
          verifyStateMachineAttributes(isMultiTcvr, afterProgrammingAttrValues);

          // Clear expectations for MockCmisTransceiverImpl objects
          MockCmisTransceiverImpl* xcvrImpl1 =
              static_cast<MockCmisTransceiverImpl*>(
                  cmisQsfpImpls_[tcvrId1_].get());
          ::testing::Mock::VerifyAndClearExpectations(xcvrImpl1);
          if (isMultiTcvr) {
            MockCmisTransceiverImpl* xcvrImpl2 =
                static_cast<MockCmisTransceiverImpl*>(
                    cmisQsfpImpls_[tcvrId2_].get());
            ::testing::Mock::VerifyAndClearExpectations(xcvrImpl2);
          }
        } /* verify */,
        kTestName,
        mappingType,
        true /* isMock */);
  }
}

TEST_F(PortStateMachineTest, programIphyFails) {
  const std::string kTestName = "programIphyFails";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    verifyStateUnchanged(
        {makeStates(
            TransceiverStateMachineState::TRANSCEIVER_READY,
            PortStateMachineState::INITIALIZED,
            isMultiTcvr,
            isMultiPort)},
        [this, isMultiTcvr]() {
          setupProgramInternalPhyPortsFailMock(isMultiTcvr);
          setupProgramExternalPhyPortFailMock(isMultiTcvr);
        },
        [this]() {
          portManager_->triggerProgrammingEvents();
          portManager_->triggerProgrammingEvents();
        },
        [this, isMultiTcvr, isMultiPort]() {
          removeProgramInternalPhyPortsFailMock(isMultiTcvr);
          portManager_->triggerProgrammingEvents();
          portManager_->triggerProgrammingEvents();
          assertCurrentStateEquals(makeStates(
              TransceiverStateMachineState::TRANSCEIVER_READY,
              PortStateMachineState::IPHY_PORTS_PROGRAMMED,
              isMultiTcvr,
              isMultiPort));
          removeProgramExternalPhyPortFailMock(isMultiTcvr);
        },
        kTestName,
        mappingType,
        true /* isMock */);
  }
}

TEST_F(PortStateMachineTest, programXphyFails) {
  const std::string kTestName = "programXphyFails";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    verifyStateUnchanged(
        {makeStates(
            TransceiverStateMachineState::TRANSCEIVER_READY,
            PortStateMachineState::IPHY_PORTS_PROGRAMMED,
            isMultiTcvr,
            isMultiPort)},
        [this, isMultiTcvr]() {
          setupProgramExternalPhyPortFailMock(isMultiTcvr);
        },
        [this]() {
          portManager_->triggerProgrammingEvents();
          portManager_->triggerProgrammingEvents();
        },
        [this, isMultiTcvr, isMultiPort]() {
          removeProgramExternalPhyPortFailMock(isMultiTcvr);
          portManager_->triggerProgrammingEvents();
          portManager_->triggerProgrammingEvents();

          assertCurrentStateEquals(makeStates(
              TransceiverStateMachineState::TRANSCEIVER_READY,
              PortStateMachineState::XPHY_PORTS_PROGRAMMED,
              isMultiTcvr,
              isMultiPort));
        } /* verify */,
        kTestName,
        mappingType,
        true /* isMock */);
  }
}

TEST_F(
    PortStateMachineTest,
    furthestStatesWhenSingleTransceiverNotPresentMultiTcvr) {
  const std::string kTestName =
      "furthestStatesWhenSingleTransceiverNotPresentMultiTcvr";

  // Only test multiTcvr mode (which doesn't support multiPort).
  // When one transceiver is not present in multi-tcvr mode, the system
  // should match the behavior of furthestStatesWhenTransceiverNotPresent
  // where the present transceiver can progress to TRANSCEIVER_PROGRAMMED,
  // and the port can progress to PORT_UP.
  const bool isMultiTcvr = true;
  const bool isMultiPort = false;
  logTestExecution(kTestName, isMultiTcvr, isMultiPort);
  verifyStateMachine(
      {makeStates(
          TransceiverStateMachineState::NOT_PRESENT,
          PortStateMachineState::UNINITIALIZED,
          isMultiTcvr,
          isMultiPort)},
      makeStates(
          TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
          PortStateMachineState::PORT_UP,
          isMultiTcvr,
          isMultiPort) /* expected state */,
      [this]() {
        // Make only tcvrId2_ not present, tcvrId1_ remains present (default)
        MockCmisModule* mockXcvr2 = static_cast<MockCmisModule*>(xcvr2_);
        auto xcvrImpl2 = mockXcvr2->getTransceiverImpl();
        EXPECT_CALL(*xcvrImpl2, detectTransceiver())
            .WillRepeatedly(::testing::Return(false));
        xcvr2_->detectPresence();
      } /* preUpdate */,
      [this]() {
        for (int i = 0; i < 5; ++i) {
          portManager_->refreshStateMachines();
        }
      } /* stateUpdate */,
      [this]() {
        ASSERT_TRUE(isTransceiverPresent(tcvrId1_));
        ASSERT_FALSE(isTransceiverPresent(tcvrId2_));
      } /* verify */,
      kTestName,
      true /* isMock */);
}

TEST_F(
    PortStateMachineTest,
    furthestStatesWhenSingleTransceiverNotReadyMultiTcvr) {
  const std::string kTestName =
      "furthestStatesWhenSingleTransceiverNotReadyMultiTcvr";

  // Only test multiTcvr mode (which doesn't support multiPort).
  // In multiTcvr mode, if one transceiver is not ready, the system can
  // progress to XPHY_PORTS_PROGRAMMED but cannot fully program the
  // transceivers. This is because all transceivers must be ready for the
  // system to be fully programmed.
  const bool isMultiTcvr = true;
  const bool isMultiPort = false;
  logTestExecution(kTestName, isMultiTcvr, isMultiPort);
  verifyStateMachine(
      {makeStates(
          TransceiverStateMachineState::NOT_PRESENT,
          PortStateMachineState::UNINITIALIZED,
          isMultiTcvr,
          isMultiPort)},
      makeStates(
          TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
          PortStateMachineState::XPHY_PORTS_PROGRAMMED,
          std::nullopt,
          TransceiverStateMachineState::DISCOVERED) /* expected state */,
      [this]() {
        // Make only tcvrId2_ present but not ready
        MockCmisModule* mockXcvr = static_cast<MockCmisModule*>(xcvr2_);
        EXPECT_CALL(*mockXcvr, ensureTransceiverReadyLocked(testing::_))
            .WillRepeatedly(::testing::Return(false));
      } /* preUpdate */,
      [this]() {
        for (int i = 0; i < 5; ++i) {
          portManager_->refreshStateMachines();
        }
      } /* stateUpdate */,
      []() {} /* verify */,
      kTestName,
      true /* isMock */);
}

TEST_F(PortStateMachineTest, upgradeFirmware) {
  const std::string kTestName = "upgradeFirmware";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    verifyStateMachine(
        {makeStates(
            TransceiverStateMachineState::NOT_PRESENT,
            PortStateMachineState::UNINITIALIZED,
            isMultiTcvr,
            isMultiPort)},
        makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr,
            isMultiPort) /* expected state */,
        [this]() {
          enableTransceiverFirmwareUpgradeTesting(true);
        } /* preUpdate */,
        [this]() {
          for (int i = 0; i < 5; ++i) {
            portManager_->refreshStateMachines();
          }
        } /* stateUpdate */,
        [this, isMultiTcvr]() {
          std::map<std::string, bool> expectedAttrValues = {
              {"needToResetToDiscovered", false}};
          verifyStateMachineAttributes(isMultiTcvr, expectedAttrValues);
          enableTransceiverFirmwareUpgradeTesting(false);
        } /* verify */,
        kTestName,
        true /* isMock */);
  }
}

TEST_F(PortStateMachineTest, reseatTransceiver) {
  const std::string kTestName = "reseatTransceiver";

  // This test verifies that removing and reinserting a transceiver properly
  // resets the state machine and allows reprogramming
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    initManagers(mappingType);

    // Lambda to handle transceiver removal and insertion
    auto removeCmisTransceiver = [this, isMultiPort](
                                     TransceiverID tcvrId, bool isRemoval) {
      if (isRemoval) {
        XLOG(INFO) << "Verifying Removing Transceiver: " << tcvrId;
        // Set mgmt interface to UNKNOWN to simulate removal
        transceiverManager_->overrideMgmtInterface(
            static_cast<int>(tcvrId) + 1,
            uint8_t(TransceiverModuleIdentifier::UNKNOWN));
        // Actually remove the transceiver object
        transceiverManager_->overrideTransceiverForTesting(tcvrId, nullptr);
      } else {
        XLOG(INFO) << "Verifying Inserting new CMIS Transceiver: " << tcvrId;
        // Mimic a module replacement by setting to CMIS
        transceiverManager_->overrideMgmtInterface(
            static_cast<int>(tcvrId) + 1,
            uint8_t(TransceiverModuleIdentifier::QSFP_PLUS_CMIS));

        // Create new mock CMIS transceiver implementation
        cmisQsfpImpls_.push_back(
            std::make_unique<MockCmisTransceiverImpl>(
                tcvrId, transceiverManager_.get()));
        EXPECT_CALL(*cmisQsfpImpls_.back().get(), detectTransceiver())
            .WillRepeatedly(::testing::Return(true));

        // Create and override with new MockCmisModule
        auto* xcvr = static_cast<QsfpModule*>(
            transceiverManager_->overrideTransceiverForTesting(
                tcvrId,
                std::make_unique<MockCmisModule>(
                    transceiverManager_->getPortNames(tcvrId),
                    cmisQsfpImpls_.back().get(),
                    tcvrConfig_,
                    transceiverManager_->getTransceiverName(tcvrId))));

        // Update the test's transceiver pointers
        if (tcvrId == tcvrId1_) {
          xcvr1_ = xcvr;
        } else if (tcvrId == tcvrId2_) {
          xcvr2_ = xcvr;
        }
      }
    };

    // Step 1: Start from TRANSCEIVER_PROGRAMMED / PORT_DOWN state
    xcvr1_ = overrideTransceiver(isMultiPort, true, tcvrId1_);
    if (isMultiTcvr) {
      xcvr2_ = overrideTransceiver(isMultiPort, true, tcvrId2_);
    }
    setState(
        makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_DOWN,
            isMultiTcvr,
            isMultiPort),
        isMultiPort,
        isMultiTcvr);

    // Step 2: Remove the transceiver(s) by actually setting them to nullptr
    XLOG(INFO) << "Removing transceiver(s)";
    removeCmisTransceiver(tcvrId1_, true /* isRemoval */);
    if (isMultiTcvr) {
      removeCmisTransceiver(tcvrId2_, true /* isRemoval */);
    }

    // Refresh to detect removal
    portManager_->refreshStateMachines();

    // Verify transceiver is not present
    const auto& xcvrInfo1 = transceiverManager_->getTransceiverInfo(tcvrId1_);
    EXPECT_FALSE(*xcvrInfo1.tcvrState()->present());
    // Verify timestamp is set even when not present
    EXPECT_GT(*xcvrInfo1.tcvrState()->timeCollected(), 0);
    EXPECT_GT(*xcvrInfo1.tcvrStats()->timeCollected(), 0);

    if (isMultiTcvr) {
      const auto& xcvrInfo2 = transceiverManager_->getTransceiverInfo(tcvrId2_);
      EXPECT_FALSE(*xcvrInfo2.tcvrState()->present());
      EXPECT_GT(*xcvrInfo2.tcvrState()->timeCollected(), 0);
      EXPECT_GT(*xcvrInfo2.tcvrStats()->timeCollected(), 0);
    }

    // After removal, port should still be DOWN, transceiver back to NOT_PRESENT
    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::TRANSCEIVER_READY,
        PortStateMachineState::PORT_DOWN,
        isMultiTcvr,
        isMultiPort));
    ASSERT_FALSE(isTransceiverPresent(tcvrId1_));
    if (isMultiTcvr) {
      ASSERT_FALSE(isTransceiverPresent(tcvrId2_));
    }

    // Step 3: Reinitialize ports which should trigger IPHY programming
    setupProgramExternalPhyPortFailMock(isMultiTcvr);
    portManager_->detectTransceiverResetAndReinitializeCorrespondingDownPorts();
    portManager_->triggerProgrammingEvents();
    portManager_->triggerProgrammingEvents();
    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::TRANSCEIVER_READY,
        PortStateMachineState::IPHY_PORTS_PROGRAMMED,
        isMultiTcvr,
        isMultiPort));

    // Verify programming attributes
    std::map<std::string, bool> afterIphyAttr = {
        {"isTransceiverProgrammed", false}};
    verifyStateMachineAttributes(isMultiTcvr, afterIphyAttr);

    // Step 4: Trigger XPHY programming
    removeProgramExternalPhyPortFailMock(isMultiTcvr);
    portManager_->triggerProgrammingEvents();
    portManager_->triggerProgrammingEvents();
    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::TRANSCEIVER_READY,
        PortStateMachineState::XPHY_PORTS_PROGRAMMED,
        isMultiTcvr,
        isMultiPort));

    std::map<std::string, bool> afterXphyAttr = {
        {"isTransceiverProgrammed", false}};
    verifyStateMachineAttributes(isMultiTcvr, afterXphyAttr);

    // Step 5: Insert new transceiver(s) using the lambda
    XLOG(INFO) << "Inserting new transceiver(s)";
    removeCmisTransceiver(tcvrId1_, false /* isRemoval */);
    if (isMultiTcvr) {
      removeCmisTransceiver(tcvrId2_, false /* isRemoval */);
    }

    transceiverManager_->refreshTransceivers();

    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::DISCOVERED,
        PortStateMachineState::XPHY_PORTS_PROGRAMMED,
        isMultiTcvr,
        isMultiPort));

    // Verify transceiver is now present
    ASSERT_TRUE(isTransceiverPresent(tcvrId1_));
    if (isMultiTcvr) {
      ASSERT_TRUE(isTransceiverPresent(tcvrId2_));
    }

    // Should progress to TRANSCEIVER_READY after detection
    portManager_->triggerProgrammingEvents();
    transceiverManager_->triggerProgrammingEvents();
    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::TRANSCEIVER_READY,
        PortStateMachineState::XPHY_PORTS_PROGRAMMED,
        isMultiTcvr,
        isMultiPort));

    // Step 6: Complete programming to TRANSCEIVER_PROGRAMMED
    portManager_->refreshStateMachines();
    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
        PortStateMachineState::XPHY_PORTS_PROGRAMMED,
        isMultiTcvr,
        isMultiPort));

    portManager_->refreshStateMachines();
    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
        PortStateMachineState::TRANSCEIVERS_PROGRAMMED,
        isMultiTcvr,
        isMultiPort));

    // Verify all programming attributes are now true
    std::map<std::string, bool> finalAttr = {{"isTransceiverProgrammed", true}};
    verifyStateMachineAttributes(isMultiTcvr, finalAttr);

    EXPECT_FALSE(xcvr1_->getDirty_());
    if (isMultiTcvr) {
      EXPECT_FALSE(xcvr2_->getDirty_());
    }

    portManager_->refreshStateMachines();
    assertCurrentStateEquals(makeStates(
        TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
        PortStateMachineState::PORT_DOWN,
        isMultiTcvr,
        isMultiPort));
  }
}

TEST_F(PortStateMachineTest, verifyOnlyPortDownIsUnsafeForRemediation) {
  // This test verifies that updatePortActiveStatusInTransceiverManager()
  // correctly marks ports as inactive (operState = false) ONLY when the port
  // is in PORT_DOWN state. All other states should be considered as unsafe for
  // remediation / fw upgrade.
  const std::string kTestName = "verifyOnlyPortDownIsUnsafeForRemediation";

  // Test port states that should be considered SAFE (operState = true)
  // using state combinations supported by setState()
  std::vector<std::pair<TransceiverStateMachineState, PortStateMachineState>>
      safeStatePairs = {
          {TransceiverStateMachineState::TRANSCEIVER_READY,
           PortStateMachineState::UNINITIALIZED},
          {TransceiverStateMachineState::DISCOVERED,
           PortStateMachineState::INITIALIZED},
          {TransceiverStateMachineState::DISCOVERED,
           PortStateMachineState::IPHY_PORTS_PROGRAMMED},
          {TransceiverStateMachineState::DISCOVERED,
           PortStateMachineState::XPHY_PORTS_PROGRAMMED},
          {TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
           PortStateMachineState::XPHY_PORTS_PROGRAMMED},
          {TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
           PortStateMachineState::TRANSCEIVERS_PROGRAMMED},
          {TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
           PortStateMachineState::PORT_UP},
      };

  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    bool tcvrExists = mappingTypeHasTcvr(mappingType);
    verifyStateUnchanged(
        statePairsToStructs(
            safeStatePairs, isMultiTcvr, isMultiPort, tcvrExists),
        []() {} /* preUpdate */,
        [this]() {
          portManager_->updatePortActiveStatusInTransceiverManager();
        } /* stateUpdate */,
        [this, isMultiTcvr]() {
          auto [allPortsDown, downPorts] =
              transceiverManager_->areAllPortsDown(tcvrId1_);
          EXPECT_FALSE(allPortsDown)
              << "Non-PORT_DOWN states should NOT be considered as port down.";
          EXPECT_TRUE(downPorts.empty())
              << "Non-PORT_DOWN states should NOT have any down ports.";

          if (isMultiTcvr) {
            auto [allPortsDown2, downPorts2] =
                transceiverManager_->areAllPortsDown(tcvrId2_);
            EXPECT_FALSE(allPortsDown2)
                << "Non-PORT_DOWN states should NOT be considered as port down for tcvr2.";
            EXPECT_TRUE(downPorts2.empty())
                << "Non-PORT_DOWN states should NOT have any down ports for tcvr2.";
          }
        } /* verify */,
        kTestName + "_safe_states",
        mappingType,
        true /* isMock */
    );
  }

  // Test PORT_DOWN which should be the ONLY state considered unsafe
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    verifyStateUnchanged(
        {makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_DOWN,
            isMultiTcvr,
            isMultiPort)},
        []() {} /* preUpdate */,
        [this]() {
          portManager_->updatePortActiveStatusInTransceiverManager();
        } /* stateUpdate */,
        [this, isMultiTcvr, isMultiPort]() {
          auto [allPortsDown, downPorts] =
              transceiverManager_->areAllPortsDown(tcvrId1_);
          EXPECT_TRUE(allPortsDown)
              << "PORT_DOWN should be considered as port down.";
          int expectedDownPorts = isMultiPort ? 2 : 1;
          EXPECT_EQ(downPorts.size(), expectedDownPorts)
              << "PORT_DOWN should report all ports as down.";

          if (isMultiTcvr) {
            auto [allPortsDown2, downPorts2] =
                transceiverManager_->areAllPortsDown(tcvrId2_);
            EXPECT_TRUE(allPortsDown2)
                << "PORT_DOWN should be considered as port down for tcvr2.";
            EXPECT_EQ(downPorts2.size(), 1)
                << "PORT_DOWN should report port as down for tcvr2.";
          }
        } /* verify */,
        kTestName + "_PORT_DOWN_is_unsafe",
        mappingType,
        true /* isMock */
    );
  }
}

TEST_F(PortStateMachineTest, agentDisableEnablePort) {
  std::string kTestName = "agentDisableEnablePort";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinationsWithBackplane()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    bool tcvrExists = mappingTypeHasTcvr(mappingType);
    verifyStateMachine(
        {makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr,
            isMultiPort,
            tcvrExists)},
        makeStates(
            TransceiverStateMachineState::TRANSCEIVER_READY,
            PortStateMachineState::UNINITIALIZED,
            isMultiTcvr,
            isMultiPort,
            tcvrExists) /* expected state */,
        []() {} /* preUpdate */,
        [this]() {
          portManager_->setOverrideAllAgentPortStatusForTesting(false, false);
          for (int i = 0; i < 5; ++i) {
            refreshAndTriggerProgramming();
          }
        } /* stateUpdate */,
        []() {} /* verify */,
        kTestName + "-phase-disable",
        true /* isMock */,
        mappingType);

    verifyStateMachine(
        {makeStates(
            TransceiverStateMachineState::TRANSCEIVER_READY,
            PortStateMachineState::UNINITIALIZED,
            isMultiTcvr,
            isMultiPort,
            tcvrExists)},
        makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr,
            isMultiPort,
            tcvrExists) /* expected state */,
        []() {} /* preUpdate */,
        [this]() {
          portManager_->setOverrideAllAgentPortStatusForTesting(true, true);
          for (int i = 0; i < 5; ++i) {
            refreshAndTriggerProgramming();
          }
        } /* stateUpdate */,
        []() {} /* verify */,
        kTestName + "-phase-enable",
        true /* isMock */,
        mappingType);
  }
}

// This test verifies that when remediation is ENABLED, coldboot config changes
// do NOT trigger remediation on transceivers that are already programmed with
// ports up. The transceiver should remain in TRANSCEIVER_PROGRAMMED / PORT_UP
// state after the config change and refresh cycles.
TEST_F(PortStateMachineTest, agentConfigChangedColdBootNoRemediationOnPortUp) {
  const std::string kTestName =
      "agentConfigChangedColdBootNoRemediationOnPortUp";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);

    verifyStateUnchanged(
        {makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */)},
        [this]() {
          // Enable remediation (remove pause) - this is key to the test
          transceiverManager_->setPauseRemediation(0, nullptr /* evb */);
          sleep(1);
        } /* preUpdate */,
        [this, isMultiTcvr]() {
          triggerAgentConfigChanged(true /* isAgentColdBoot */);
          for (int i = 0; i < 5; i++) {
            portManager_->refreshStateMachines();
            std::map<std::string, bool> expectedAttrValues = {
                {"isTransceiverJustRemediated", false}};
            verifyStateMachineAttributes(isMultiTcvr, expectedAttrValues);
          }
        } /* stateUpdate */,
        [this, isMultiTcvr]() {
          // Verify that remediation was NOT triggered even though it's enabled
          std::map<std::string, bool> expectedAttrValues = {
              {"isTransceiverProgrammed", true},
              {"isTransceiverJustRemediated", false}};
          verifyStateMachineAttributes(isMultiTcvr, expectedAttrValues);
        } /* verify */,
        kTestName,
        mappingType,
        true /* isMock */
    );
  }
}

// This test verifies that when remediation is ENABLED, warmboot config changes
// do NOT trigger remediation on transceivers that are already programmed with
// ports up. The transceiver should remain in TRANSCEIVER_PROGRAMMED / PORT_UP
// state after the config change and refresh cycles.
TEST_F(PortStateMachineTest, agentConfigChangedWarmBootNoRemediationOnPortUp) {
  const std::string kTestName =
      "agentConfigChangedWarmBootNoRemediationOnPortUp";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);

    verifyStateUnchanged(
        {makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr /* isMultiTcvr */,
            isMultiPort /* isMultiPort */)},
        [this]() {
          // Enable remediation (remove pause) - this is key to the test
          transceiverManager_->setPauseRemediation(0, nullptr /* evb */);
          sleep(1);
        } /* preUpdate */,
        [this, isMultiTcvr]() {
          triggerAgentConfigChanged(false /* isAgentColdBoot */);
          for (int i = 0; i < 5; i++) {
            portManager_->refreshStateMachines();
            std::map<std::string, bool> expectedAttrValues = {
                {"isTransceiverJustRemediated", false}};
            verifyStateMachineAttributes(isMultiTcvr, expectedAttrValues);
          }
        } /* stateUpdate */,
        [this, isMultiTcvr]() {
          // Verify that remediation was NOT triggered even though it's enabled
          std::map<std::string, bool> expectedAttrValues = {
              {"isTransceiverProgrammed", true},
              {"isTransceiverJustRemediated", false}};
          verifyStateMachineAttributes(isMultiTcvr, expectedAttrValues);
        } /* verify */,
        kTestName,
        mappingType,
        true /* isMock */
    );
  }
}

TEST_F(PortStateMachineTest, LastDownTimeUpdatedOnPortDown) {
  const std::string kTestName = "LastDownTimeUpdatedOnPortDown";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    initManagers(mappingType);
    xcvr1_ = overrideTransceiver(isMultiPort, true /*isMock*/, tcvrId1_);
    if (isMultiTcvr) {
      xcvr2_ = overrideTransceiver(isMultiPort, true /*isMock*/, tcvrId2_);
    }

    // Drive to TRANSCEIVER_PROGRAMMED / PORT_UP via setState
    setState(
        makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr,
            isMultiPort),
        isMultiPort,
        isMultiTcvr);

    // Record lastDownTime before transition
    auto initialDownTime = xcvr1_->getLastDownTime();

    // Sleep 1s so the new timestamp is clearly different
    /* sleep override */
    sleep(1);

    // Set all ports DOWN, trigger the updateTransceiverPortStatus path
    portManager_->setOverrideAllAgentPortStatusForTesting(
        false /* isUp */, true /* isEnabled */);
    portManager_->updateTransceiverPortStatus();
    portManager_->updatePortActiveStatusInTransceiverManager();

    // Verify lastDownTime was updated for tcvr1
    EXPECT_GT(xcvr1_->getLastDownTime(), initialDownTime);

    // Verify needMarkLastDownTime is now false for tcvr1 (was set true on UP,
    // cleared on DOWN). Note: tcvr2 in DUAL_TRANSCEIVER mode doesn't have
    // ports in the test override, so its needMarkLastDownTime is not managed
    // by updateTcvrLastDownTime.
    std::map<std::string, bool> expectedAttrValues = {
        {"needMarkLastDownTime", false}};
    verifyStateMachineAttributes(tcvrId1_, expectedAttrValues);
  }
}

TEST_F(PortStateMachineTest, LastDownTimeNotUpdatedWhenPortsStayUp) {
  const std::string kTestName = "LastDownTimeNotUpdatedWhenPortsStayUp";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    initManagers(mappingType);
    xcvr1_ = overrideTransceiver(isMultiPort, true /*isMock*/, tcvrId1_);
    if (isMultiTcvr) {
      xcvr2_ = overrideTransceiver(isMultiPort, true /*isMock*/, tcvrId2_);
    }

    // Drive to PORT_UP via setState
    setState(
        makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr,
            isMultiPort),
        isMultiPort,
        isMultiTcvr);

    auto downTimeBefore = xcvr1_->getLastDownTime();
    /* sleep override */
    sleep(1);

    // Refresh again without changing port status (ports stay UP)
    portManager_->updateTransceiverPortStatus();
    portManager_->updatePortActiveStatusInTransceiverManager();

    // lastDownTime should NOT have changed
    EXPECT_EQ(xcvr1_->getLastDownTime(), downTimeBefore);
  }
}

TEST_F(PortStateMachineTest, LastDownTimeNotUpdatedOnRepeatedDown) {
  const std::string kTestName = "LastDownTimeNotUpdatedOnRepeatedDown";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    initManagers(mappingType);
    xcvr1_ = overrideTransceiver(isMultiPort, true /*isMock*/, tcvrId1_);
    if (isMultiTcvr) {
      xcvr2_ = overrideTransceiver(isMultiPort, true /*isMock*/, tcvrId2_);
    }

    // Drive to PORT_UP then DOWN
    setState(
        makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr,
            isMultiPort),
        isMultiPort,
        isMultiTcvr);

    portManager_->setOverrideAllAgentPortStatusForTesting(
        false /* isUp */, true /* isEnabled */);
    portManager_->updateTransceiverPortStatus();
    portManager_->updatePortActiveStatusInTransceiverManager();

    // Record lastDownTime after first DOWN transition
    auto downTimeAfterFirstDown = xcvr1_->getLastDownTime();

    // needMarkLastDownTime should be false now for tcvr1
    std::map<std::string, bool> expectedAttrValues = {
        {"needMarkLastDownTime", false}};
    verifyStateMachineAttributes(tcvrId1_, expectedAttrValues);

    /* sleep override */
    sleep(1);

    // Trigger another update cycle with ports still DOWN
    portManager_->updateTransceiverPortStatus();
    portManager_->updatePortActiveStatusInTransceiverManager();

    // lastDownTime should NOT have changed (guard prevents re-marking)
    EXPECT_EQ(xcvr1_->getLastDownTime(), downTimeAfterFirstDown);
  }
}

TEST_F(PortStateMachineTest, LastDownTimeUpdatedOnUpDownCycle) {
  const std::string kTestName = "LastDownTimeUpdatedOnUpDownCycle";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    initManagers(mappingType);
    xcvr1_ = overrideTransceiver(isMultiPort, true /*isMock*/, tcvrId1_);
    if (isMultiTcvr) {
      xcvr2_ = overrideTransceiver(isMultiPort, true /*isMock*/, tcvrId2_);
    }

    // Cycle 1: UP -> DOWN
    setState(
        makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr,
            isMultiPort),
        isMultiPort,
        isMultiTcvr);

    portManager_->setOverrideAllAgentPortStatusForTesting(
        false /* isUp */, true /* isEnabled */);
    portManager_->updateTransceiverPortStatus();
    portManager_->updatePortActiveStatusInTransceiverManager();
    auto firstDownTime = xcvr1_->getLastDownTime();

    /* sleep override */
    sleep(1);

    // Cycle 2: UP again -> DOWN again
    setOverrideAgentStatusPortUp(isMultiTcvr, isMultiPort);
    portManager_->updateTransceiverPortStatus();
    portManager_->updatePortActiveStatusInTransceiverManager();

    // needMarkLastDownTime should now be true for tcvr1 (port came back up)
    std::map<std::string, bool> expectedAttrValues = {
        {"needMarkLastDownTime", true}};
    verifyStateMachineAttributes(tcvrId1_, expectedAttrValues);

    portManager_->setOverrideAllAgentPortStatusForTesting(
        false /* isUp */, true /* isEnabled */);
    portManager_->updateTransceiverPortStatus();
    portManager_->updatePortActiveStatusInTransceiverManager();

    // Second DOWN time should be strictly greater than first
    EXPECT_GT(xcvr1_->getLastDownTime(), firstDownTime);
    expectedAttrValues = {{"needMarkLastDownTime", false}};
    verifyStateMachineAttributes(tcvrId1_, expectedAttrValues);
  }
}

TEST_F(PortStateMachineTest, LastDownTimeViaUpdatePortActiveState) {
  const std::string kTestName = "LastDownTimeViaUpdatePortActiveState";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    initManagers(mappingType);
    xcvr1_ = overrideTransceiver(isMultiPort, true /*isMock*/, tcvrId1_);
    if (isMultiTcvr) {
      xcvr2_ = overrideTransceiver(isMultiPort, true /*isMock*/, tcvrId2_);
    }

    // Drive to PORT_UP via setState
    setState(
        makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr,
            isMultiPort),
        isMultiPort,
        isMultiTcvr);

    auto initialDownTime = xcvr1_->getLastDownTime();
    /* sleep override */
    sleep(1);

    // Use updatePortActiveState (agent callback path) to set ports DOWN
    std::map<int32_t, PortStatus> portStatusMap;
    PortStatus downStatus;
    downStatus.enabled() = true;
    downStatus.up() = false;
    portStatusMap[static_cast<int32_t>(portId1_)] = downStatus;
    if (isMultiPort) {
      portStatusMap[static_cast<int32_t>(portId3_)] = downStatus;
    }
    portManager_->updatePortActiveState(portStatusMap);

    // Verify lastDownTime updated via this path
    EXPECT_GT(xcvr1_->getLastDownTime(), initialDownTime);
  }
}

TEST_F(PortStateMachineTest, PrematureRemediationBlockedByLastDownTime) {
  const std::string kTestName = "PrematureRemediationBlockedByLastDownTime";
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    initManagers(mappingType);
    xcvr1_ = overrideTransceiver(isMultiPort, true /*isMock*/, tcvrId1_);
    if (isMultiTcvr) {
      xcvr2_ = overrideTransceiver(isMultiPort, true /*isMock*/, tcvrId2_);
    }

    // Drive to PORT_UP via setState
    setState(
        makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr,
            isMultiPort),
        isMultiPort,
        isMultiTcvr);

    // Set a longer initial_remediate_interval to block premature remediation
    gflags::SetCommandLineOptionWithMode(
        "initial_remediate_interval", "300", gflags::SET_FLAGS_DEFAULT);
    transceiverManager_->setPauseRemediation(0, nullptr /* evb */);

    // Set ports DOWN and update
    portManager_->setOverrideAllAgentPortStatusForTesting(
        false /* isUp */, true /* isEnabled */);
    portManager_->updateTransceiverPortStatus();
    portManager_->updatePortActiveStatusInTransceiverManager();

    // Immediately try remediation (no sleep - NOT enough time has passed)
    portManager_->refreshStateMachines();

    // Remediation should NOT have triggered (xcvr should not be dirty)
    EXPECT_FALSE(xcvr1_->getDirty_());
    if (isMultiTcvr) {
      EXPECT_FALSE(xcvr2_->getDirty_());
    }

    // State should still be TRANSCEIVER_PROGRAMMED (not reset to DISCOVERED)
    std::map<std::string, bool> expectedAttrValues = {
        {"isTransceiverProgrammed", true},
        {"isTransceiverJustRemediated", false}};
    verifyStateMachineAttributes(isMultiTcvr, expectedAttrValues);

    // Reset flag
    gflags::SetCommandLineOptionWithMode(
        "initial_remediate_interval", "0", gflags::SET_FLAGS_DEFAULT);
  }
}

TEST_F(PortStateMachineTest, NoRemediationWhileAsicPrbsEnabled) {
  const std::string kTestName = "NoRemediationWhileAsicPrbsEnabled";
  gflags::FlagSaver flagSaver;
  for (const auto& [mappingType, isMultiTcvr, isMultiPort] :
       getTestModeCombinations()) {
    logTestExecution(kTestName, isMultiTcvr, isMultiPort);
    initManagers(mappingType);
    xcvr1_ = overrideTransceiver(isMultiPort, true /*isMock*/, tcvrId1_);
    if (isMultiTcvr) {
      xcvr2_ = overrideTransceiver(isMultiPort, true /*isMock*/, tcvrId2_);
    }
    setState(
        makeStates(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED,
            PortStateMachineState::PORT_UP,
            isMultiTcvr,
            isMultiPort),
        isMultiPort,
        isMultiTcvr);
    gflags::SetCommandLineOptionWithMode(
        "initial_remediate_interval", "0", gflags::SET_FLAGS_DEFAULT);
    transceiverManager_->setPauseRemediation(0, nullptr /* evb */);

    // Ports are down because they're running ASIC PRBS
    portManager_->setOverrideAllAgentPortStatusForTesting(
        false /* up */,
        true /* enabled */,
        false /* clearOnly */,
        true /* asicPrbsEnabled */);
    portManager_->updateTransceiverPortStatus();
    portManager_->updatePortActiveStatusInTransceiverManager();
    // Give 1s buffer when comparing lastDownTime_ in shouldRemediate()
    /* sleep override */
    sleep(1);
    portManager_->refreshStateMachines();
    EXPECT_FALSE(xcvr1_->getDirty_());
    if (isMultiTcvr) {
      EXPECT_FALSE(xcvr2_->getDirty_());
    }

    // Once PRBS is off the same down ports get remediated
    portManager_->setOverrideAllAgentPortStatusForTesting(
        false /* up */, true /* enabled */);
    portManager_->refreshStateMachines();
    EXPECT_TRUE(xcvr1_->getDirty_());
    if (isMultiTcvr) {
      EXPECT_TRUE(xcvr2_->getDirty_());
    }
  }
}

/*
 * Test for race condition where a non-lowest-indexed port becomes the
 * orchestrator for IPHY programming because the lowest-indexed port hasn't
 * been initialized yet. When the lowest-indexed port later initializes and
 * tries to orchestrate, programInternalPhyPorts() throws because the
 * transceiver is already in the initialized cache.
 *
 * This mimics a production failure where during cold boot:
 * 1. Port 19 (higher-indexed) initializes before Port 17 (lowest-indexed)
 * 2. Port 19 becomes orchestrator, calls programInternalPhyPorts() for the
 *    transceiver, populating the cache for ALL ports on that transceiver
 * 3. Port 17 initializes later, tries to orchestrate (being lowest-indexed)
 * 4. programInternalPhyPorts() throws "already exists in initialized
 *    transceivers cache" because the cache was already populated by Port 19
 * 5. Port 17 is permanently stuck in INITIALIZED state, blocking ports 21/23
 *    that depend on it as their orchestrator
 */
TEST_F(PortStateMachineTest, iphyProgrammingRaceWhenNonLowestPortInitFirst) {
  initManagers(FakeTestPlatformMappingType::STANDARD);

  // Set up multi-port transceiver override so both portId1_ and portId3_
  // are mapped to tcvrId1_
  transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
      overrideMultiPortTcvrToPortAndProfile_);

  xcvr1_ =
      overrideTransceiver(true /* multiPort */, false /* isMock */, tcvrId1_);

  // overrideTransceiver() clears the override config, so re-set it
  transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
      overrideMultiPortTcvrToPortAndProfile_);

  // Step 1: Initialize ONLY portId3_ (the higher-indexed port).
  // This mimics the race condition where the lowest-indexed port (portId1_)
  // hasn't been initialized yet.
  portManager_->setOverrideAgentPortStatusForTesting(
      {portId3_} /* upPortIds */, {portId3_} /* enabledPortIds */);
  transceiverManager_->refreshTransceivers();
  portManager_->updateTransceiverPortStatus();

  // Verify: portId3_ is INITIALIZED, portId1_ is still UNINITIALIZED
  EXPECT_EQ(
      portManager_->getPortState(portId3_), PortStateMachineState::INITIALIZED);
  EXPECT_EQ(
      portManager_->getPortState(portId1_),
      PortStateMachineState::UNINITIALIZED);

  // Step 2: Trigger IPHY programming. Since portId3_ is the only initialized
  // port, it becomes the orchestrator and calls programInternalPhyPorts().
  // This populates the initialized transceivers cache for ALL ports on tcvr.
  portManager_->triggerProgrammingEvents();

  // Verify: portId3_ should reach IPHY_PORTS_PROGRAMMED
  EXPECT_EQ(
      portManager_->getPortState(portId3_),
      PortStateMachineState::IPHY_PORTS_PROGRAMMED);

  // Step 3: Now initialize portId1_ (the lowest-indexed port).
  // This mimics the delayed initialization in production.
  portManager_->setOverrideAgentPortStatusForTesting(
      {portId1_, portId3_} /* upPortIds */,
      {portId1_, portId3_} /* enabledPortIds */);
  portManager_->updateTransceiverPortStatus();

  // Verify: portId1_ is now INITIALIZED
  EXPECT_EQ(
      portManager_->getPortState(portId1_), PortStateMachineState::INITIALIZED);

  // Step 4: Trigger IPHY programming again. portId1_ is now the lowest
  // initialized port, so it tries to orchestrate. The cache already has
  // entries from portId3_'s earlier call, but
  // setTransceiverEnabledStatusInCache is now idempotent (skips duplicates
  // instead of throwing), so programInternalPhyPorts succeeds and portId1_
  // progresses.
  portManager_->triggerProgrammingEvents();

  EXPECT_EQ(
      portManager_->getPortState(portId1_),
      PortStateMachineState::IPHY_PORTS_PROGRAMMED)
      << "portId1_ should progress to IPHY_PORTS_PROGRAMMED";
}

} // namespace facebook::fboss
