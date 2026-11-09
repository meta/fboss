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

#include "fboss/lib/CommonUtils.h"
#include "fboss/qsfp_service/module/QsfpModule.h"
#include "fboss/qsfp_service/platforms/wedge/WedgeManager.h"
#include "fboss/qsfp_service/test/hw_test/HwPortUtils.h"
#include "fboss/qsfp_service/test/hw_test/HwQsfpEnsemble.h"
#include "fboss/qsfp_service/test/hw_test/HwTransceiverUtils.h"

namespace facebook::fboss {

namespace {

std::queue<TransceiverStateMachineState> getExpectedTransceiverStates(
    bool hasXphy,
    bool isPresent) {
  std::queue<TransceiverStateMachineState> tcvrExpectedStates;
  if (!FLAGS_port_manager_mode) {
    tcvrExpectedStates.push(
        TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED);
  }
  if (!FLAGS_port_manager_mode && hasXphy) {
    tcvrExpectedStates.push(
        TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED);
  }
  tcvrExpectedStates.push(TransceiverStateMachineState::TRANSCEIVER_READY);
  if (!FLAGS_port_manager_mode || isPresent) {
    tcvrExpectedStates.push(
        TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED);
  }
  return tcvrExpectedStates;
}

std::queue<PortStateMachineState> getExpectedPortStates(
    bool hasXphy,
    bool isUp = false) {
  std::queue<PortStateMachineState> portExpectedStates;
  portExpectedStates.push(PortStateMachineState::IPHY_PORTS_PROGRAMMED);
  if (hasXphy) {
    portExpectedStates.push(PortStateMachineState::XPHY_PORTS_PROGRAMMED);
  }
  portExpectedStates.push(PortStateMachineState::TRANSCEIVERS_PROGRAMMED);
  portExpectedStates.push(
      isUp ? PortStateMachineState::PORT_UP : PortStateMachineState::PORT_DOWN);
  return portExpectedStates;
}

} // namespace

class HwStateMachineTest : public HwTest {
 public:
  HwStateMachineTest(bool setupOverrideTcvrToPortAndProfile = true)
      : HwTest(setupOverrideTcvrToPortAndProfile) {}

  void SetUp() override {
    HwTest::SetUp();
    if (!IsSkipped()) {
      std::map<int32_t, TransceiverInfo> presentTcvrs;
      getHwQsfpEnsemble()->getWedgeManager()->getTransceiversInfo(
          presentTcvrs, std::make_unique<std::vector<int32_t>>());

      auto cabledTransceivers = utility::legacyTransceiverIds(
          utility::getCabledPortTranceivers(getHwQsfpEnsemble()));

      // Get all transceivers from platform mapping
      const auto& chips = getHwQsfpEnsemble()->getPlatformMapping()->getChips();
      for (const auto& chip : chips) {
        if (*chip.second.type() != phy::DataPlanePhyChipType::TRANSCEIVER) {
          continue;
        }
        auto id = *chip.second.physicalID();
        if (auto tcvrIt = presentTcvrs.find(id); tcvrIt != presentTcvrs.end() &&
            *tcvrIt->second.tcvrState()->present()) {
          if (std::find(
                  cabledTransceivers.begin(), cabledTransceivers.end(), id) !=
              cabledTransceivers.end()) {
            presentTransceivers_.emplace_back(id);
          }
        } else {
          absentTransceivers_.emplace_back(id);
        }
      }
      XLOG(DBG2) << "Present transceivers: "
                 << folly::join(",", presentTransceivers_);
      XLOG(DBG2) << "Absent transceivers: "
                 << folly::join(",", absentTransceivers_);

      // Set pause remdiation so it won't trigger remediation
      setPauseRemediation(true);
    }
  }

  const std::vector<TransceiverID>& getPresentTransceivers() const {
    return presentTransceivers_;
  }
  const std::vector<TransceiverID>& getAbsentTransceivers() const {
    return absentTransceivers_;
  }
  void setPauseRemediation(bool paused) {
    // Pausing remediation for 30 minutes. This is reset after every test.
    getHwQsfpEnsemble()->getWedgeManager()->setPauseRemediation(
        paused ? 1800 : 0, nullptr);
  }

  bool meetAllExpectedState(
      TransceiverID id,
      TransceiverStateMachineState curState,
      std::queue<TransceiverStateMachineState>& tcvrExpectedStates,
      bool isRemediated,
      bool isAgentColdboot) {
    auto wedgeMgr = getHwQsfpEnsemble()->getWedgeManager();
    // Check whether current state matches the head of the expected state
    // queue
    if (tcvrExpectedStates.empty()) {
      // Already meet all expected states.
      return true;
    } else if (curState == tcvrExpectedStates.front()) {
      tcvrExpectedStates.pop();
      const auto programmedPortToPortInfo =
          wedgeMgr->getProgrammedIphyPortToPortInfo(id);
      std::vector<PortID> xphyPorts;
      if (auto phyManager = getHwQsfpEnsemble()->getPhyManager()) {
        xphyPorts = phyManager->getXphyPorts();
      }

      if (curState == TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED) {
        EXPECT_EQ(wedgeMgr->getNeedResetDataPath(id), isAgentColdboot);
      } else if (
          curState == TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED) {
        if (isRemediated) {
          // TODO(joseph5wu) Add check to ensure the module remediation did
          // happen
          const auto& transceiver = wedgeMgr->getTransceiverInfo(id);
          auto mgmtInterface = apache::thrift::can_throw(
              *transceiver.tcvrState()->transceiverManagementInterface());
          if (mgmtInterface == TransceiverManagementInterface::CMIS) {
            // CMIS will hard reset the module in
            // remediateFlakyTransceiver() Without clear hard reset, we
            // won't be able to detect such transceiver
          } else if (mgmtInterface == TransceiverManagementInterface::SFF) {
            // SFF will trigger tx enable and then reset low power mode.
          }
        }
        // Check xphy programmed correctly
        const auto& transceiver = wedgeMgr->getTransceiverInfo(id);
        for (const auto& [portId, portInfo] : programmedPortToPortInfo) {
          if (std::find(xphyPorts.begin(), xphyPorts.end(), portId) !=
              xphyPorts.end()) {
            utility::verifyXphyPort(
                portId, portInfo.profile, transceiver, getHwQsfpEnsemble());
          }
        }
        EXPECT_EQ(wedgeMgr->getNeedResetDataPath(id), isAgentColdboot);
      } else if (
          curState == TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED) {
        // Just finished transceiver programming
        // Only care enabled ports
        if (!programmedPortToPortInfo.empty()) {
          for (const auto& [portId, portInfo] : programmedPortToPortInfo) {
            const auto tcvrInfo = wedgeMgr->getTransceiverInfo(id);
            auto portNameStr = getHwQsfpEnsemble()
                                   ->getQsfpServiceHandler()
                                   ->getPortNameByPortId(portId);
            CHECK(portNameStr.has_value());
            utility::HwTransceiverUtils::verifyTransceiverSettings(
                *tcvrInfo.tcvrState(), *portNameStr, portInfo.profile);
            utility::HwTransceiverUtils::verifyDiagsCapability(
                *tcvrInfo.tcvrState(), wedgeMgr->getDiagsCapability(id));
          }
        }
        // After transceiver is programmed, needResetDataPath should be false
        EXPECT_FALSE(wedgeMgr->getNeedResetDataPath(id));
      }
      return tcvrExpectedStates.empty();
    }
    XLOG(WARN) << "Transceiver:" << id << " doesn't have expected state="
               << apache::thrift::util::enumNameSafe(tcvrExpectedStates.front())
               << " but actual state="
               << apache::thrift::util::enumNameSafe(curState);
    return false;
  }

  bool meetAllExpectedPortState(
      PortID portId,
      PortStateMachineState curState,
      std::queue<PortStateMachineState>& portExpectedStates,
      bool isAgentColdboot) {
    auto wedgeMgr = getHwQsfpEnsemble()->getWedgeManager();
    auto portMgr =
        getHwQsfpEnsemble()->getQsfpServiceHandler()->getPortManager();
    // Check whether current state matches the head of the expected state
    // queue
    if (portExpectedStates.empty()) {
      // Already meet all expected states.
      return true;
    } else if (curState == portExpectedStates.front()) {
      portExpectedStates.pop();
      std::vector<PortID> xphyPorts;
      if (auto phyManager = getHwQsfpEnsemble()->getPhyManager()) {
        xphyPorts = phyManager->getXphyPorts();
      }

      if (curState == PortStateMachineState::IPHY_PORTS_PROGRAMMED) {
        // Check port is up
        EXPECT_EQ(portMgr->getXphyNeedResetDataPath(portId), isAgentColdboot);
      } else if (curState == PortStateMachineState::XPHY_PORTS_PROGRAMMED) {
        // Check xphy programmed correctly
        auto tcvrIdOpt =
            portMgr->getLowestIndexedStaticTransceiverForPort(portId);
        EXPECT_TRUE(tcvrIdOpt.has_value());
        if (!tcvrIdOpt) {
          return false;
        }
        auto tcvrId = *tcvrIdOpt;
        const auto programmedPortToPortInfo =
            wedgeMgr->getProgrammedIphyPortToPortInfo(tcvrId);
        const auto& transceiver = wedgeMgr->getTransceiverInfo(tcvrId);
        for (const auto& [currPortId, portInfo] : programmedPortToPortInfo) {
          if (currPortId != portId) {
            continue;
          }
          if (std::find(xphyPorts.begin(), xphyPorts.end(), portId) ==
              xphyPorts.end()) {
            continue;
          }

          utility::verifyXphyPort(
              portId, portInfo.profile, transceiver, getHwQsfpEnsemble());
        }

        EXPECT_EQ(portMgr->getXphyNeedResetDataPath(portId), isAgentColdboot);
      } else if (curState == PortStateMachineState::TRANSCEIVERS_PROGRAMMED) {
        // After transceiver is programmed, needResetDataPath should be false
        EXPECT_FALSE(portMgr->getXphyNeedResetDataPath(portId));
      }
      return portExpectedStates.empty();
    }
    XLOG(WARN) << "Port:" << portId << " doesn't have expected state="
               << apache::thrift::util::enumNameSafe(portExpectedStates.front())
               << " but actual state="
               << apache::thrift::util::enumNameSafe(curState);
    return false;
  }

  bool refreshStateMachinesTillMeetAllStates(
      std::unordered_map<
          TransceiverID,
          std::queue<TransceiverStateMachineState>>& expectedStates,
      std::unordered_map<PortID, std::queue<PortStateMachineState>>&
          expectedPortStates,
      bool isRemediated,
      bool isAgentColdboot) {
    auto wedgeMgr = getHwQsfpEnsemble()->getWedgeManager();
    auto portMgr =
        getHwQsfpEnsemble()->getQsfpServiceHandler()->getPortManager();
    getHwQsfpEnsemble()->getQsfpServiceHandler()->refreshStateMachines();
    int numFailedTransceivers = 0;
    for (auto& idToExpectStates : expectedStates) {
      auto id = idToExpectStates.first;
      auto curState = wedgeMgr->getCurrentState(id);

      // Verify remediation state matches expectation. This check must happen
      // during the refresh cycle (before TRANSCEIVER_PROGRAMMED clears the
      // flag) to catch unexpected remediation.
      if (!isRemediated) {
        EXPECT_FALSE(wedgeMgr->transceiverJustRemediated(id))
            << "Transceiver:" << id
            << " should NOT be remediated but transceiverJustRemediated is "
               "true. Current state: "
            << apache::thrift::util::enumNameSafe(curState);
      }

      if (!meetAllExpectedState(
              id,
              curState,
              idToExpectStates.second,
              isRemediated,
              isAgentColdboot)) {
        ++numFailedTransceivers;
      }
    }

    int numFailedPorts = 0;
    for (auto& portToExpectStates : expectedPortStates) {
      auto portId = portToExpectStates.first;
      auto curState = portMgr->getPortState(portId);
      if (!meetAllExpectedPortState(
              portId, curState, portToExpectStates.second, isAgentColdboot)) {
        ++numFailedPorts;
      }
    }

    XLOG_IF(WARN, numFailedTransceivers)
        << numFailedTransceivers
        << " transceivers don't meet the expected state";
    XLOG_IF(WARN, numFailedPorts)
        << numFailedPorts << " ports don't meet the expected state";
    return numFailedTransceivers == 0 && numFailedPorts == 0;
  }

 private:
  // Forbidden copy constructor and assignment operator
  HwStateMachineTest(HwStateMachineTest const&) = delete;
  HwStateMachineTest& operator=(HwStateMachineTest const&) = delete;

  std::vector<TransceiverID> presentTransceivers_;
  std::vector<TransceiverID> absentTransceivers_;
};

// With HwTest::waitTillCabledTcvrProgrammed() being called in HwTest::SetUp(),
// all transceivers' state machine will reach programmed state eventually.
// But we also want to verify the early state if qsfp_service can't call
// iphy programming from wedge_agent.
class HwStateMachineTestWithoutIphyProgramming : public HwStateMachineTest {
 public:
  HwStateMachineTestWithoutIphyProgramming() : HwStateMachineTest(false) {}

 private:
  // Forbidden copy constructor and assignment operator
  HwStateMachineTestWithoutIphyProgramming(
      HwStateMachineTestWithoutIphyProgramming const&) = delete;
  HwStateMachineTestWithoutIphyProgramming& operator=(
      HwStateMachineTestWithoutIphyProgramming const&) = delete;
};

TEST_F(HwStateMachineTestWithoutIphyProgramming, CheckOpticsDetection) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::TRANSCEIVER_DETECTION});
  addTestedTransceivers(getPresentTransceivers());
  addTestedTransceivers(getAbsentTransceivers());
  auto verify = [&]() {
    auto wedgeMgr = getHwQsfpEnsemble()->getWedgeManager();
    // Default HwTest::Setup() already has a refresh, so all present
    // transceivers should be in DISCOVERED state; while
    // not present front panel ports should still be NOT_PRESENT

    // In PortManager mode, we need to execute some additional refreshes to
    // ensure transceivers / ports reach their intended states.
    getHwQsfpEnsemble()->getQsfpServiceHandler()->refreshStateMachines();
    getHwQsfpEnsemble()->getQsfpServiceHandler()->refreshStateMachines();

    // In PortManager mode, transceivers can proceed to TRANSCEIVER_READY
    // directly, without requiring iphy programming - this logic is covered by
    // PortStateMachines.
    TransceiverStateMachineState presentExpectedState = FLAGS_port_manager_mode
        ? TransceiverStateMachineState::TRANSCEIVER_READY
        : TransceiverStateMachineState::DISCOVERED;
    for (auto id : getPresentTransceivers()) {
      auto curState = wedgeMgr->getCurrentState(id);
      EXPECT_EQ(curState, presentExpectedState)
          << "Transceiver:" << id
          << " Actual: " << apache::thrift::util::enumNameSafe(curState)
          << ", Expected: "
          << apache::thrift::util::enumNameSafe(presentExpectedState);
    }

    TransceiverStateMachineState absentExpectedState = FLAGS_port_manager_mode
        ? TransceiverStateMachineState::TRANSCEIVER_READY
        : TransceiverStateMachineState::NOT_PRESENT;
    for (auto id : getAbsentTransceivers()) {
      auto curState = wedgeMgr->getCurrentState(id);
      EXPECT_EQ(curState, absentExpectedState)
          << "Transceiver:" << id
          << " Actual: " << apache::thrift::util::enumNameSafe(curState)
          << ", Expected: "
          << apache::thrift::util::enumNameSafe(absentExpectedState);
    }
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(HwStateMachineTest, CheckPortsProgrammed) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::DATA_PATH_PROGRAMMING});
  addTestedTransceivers(getPresentTransceivers());
  addTestedTransceivers(getAbsentTransceivers());
  auto verify = [this]() {
    auto checkTransceiverProgrammed = [this](
                                          const std::vector<TransceiverID>&
                                              tcvrs) {
      auto wedgeMgr = getHwQsfpEnsemble()->getWedgeManager();
      std::vector<PortID> xphyPorts;
      if (auto phyManager = getHwQsfpEnsemble()->getPhyManager()) {
        xphyPorts = phyManager->getXphyPorts();
      }

      for (auto id : tcvrs) {
        // Verify IPHY/ XPHY/ TCVR programmed as expected
        const auto& portAndProfile =
            wedgeMgr->getOverrideProgrammedIphyPortAndProfileForTest(id);
        // Check programmed iphy port and profile
        const auto programmedPortToPortInfo =
            wedgeMgr->getProgrammedIphyPortToPortInfo(id);
        const auto& transceiver = wedgeMgr->getTransceiverInfo(id);
        if (portAndProfile.empty()) {
          // If iphy port and profile is empty, it means the ports are
          // disabled. We don't need to program such transceivers
          EXPECT_TRUE(programmedPortToPortInfo.empty());
        } else {
          EXPECT_EQ(programmedPortToPortInfo.size(), portAndProfile.size());
          for (const auto& [portId, portInfo] : programmedPortToPortInfo) {
            auto expectedPortAndProfileIt = portAndProfile.find(portId);
            EXPECT_TRUE(expectedPortAndProfileIt != portAndProfile.end());
            EXPECT_EQ(portInfo.profile, expectedPortAndProfileIt->second);
            if (std::find(xphyPorts.begin(), xphyPorts.end(), portId) !=
                xphyPorts.end()) {
              utility::verifyXphyPort(
                  portId, portInfo.profile, transceiver, getHwQsfpEnsemble());
            }
          }
          if (!programmedPortToPortInfo.empty()) {
            for (const auto& [portId, portInfo] : programmedPortToPortInfo) {
              auto portNameStr = getHwQsfpEnsemble()
                                     ->getQsfpServiceHandler()
                                     ->getPortNameByPortId(portId);
              CHECK(portNameStr.has_value());
              utility::HwTransceiverUtils::verifyTransceiverSettings(
                  *transceiver.tcvrState(), *portNameStr, portInfo.profile);
              utility::HwTransceiverUtils::verifyDiagsCapability(
                  *transceiver.tcvrState(), wedgeMgr->getDiagsCapability(id));
            }
          }
        }
      }
    };

    checkTransceiverProgrammed(getPresentTransceivers());
    checkTransceiverProgrammed(getAbsentTransceivers());

    // Also verify backplane XPHY ports (XPHY ports without transceivers)
    // These ports are not associated with any transceiver, so they won't be
    // covered by the transceiver-based verification above.
    auto* phyManager = getHwQsfpEnsemble()->getPhyManager();
    if (phyManager) {
      auto xphyPorts = phyManager->getXphyPorts();

      for (const auto& portId : xphyPorts) {
        // Check if this port has a transceiver - if so, it was already
        // verified in the transceiver loop above
        auto tcvrId = utility::getTranscieverIdx(portId, getHwQsfpEnsemble());
        if (tcvrId.has_value()) {
          continue;
        }
        auto programmedProfile = phyManager->getProgrammedProfile(portId);
        if (programmedProfile.has_value()) {
          utility::verifyXphyPort(
              portId, *programmedProfile, std::nullopt, getHwQsfpEnsemble());
        }
      }
    }
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(HwStateMachineTest, CheckPortStatusUpdated) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::DATA_PATH_PROGRAMMING});
  addTestedTransceivers(getPresentTransceivers());
  auto verify = [this]() {
    std::unordered_map<TransceiverID, time_t> lastDownTimes;
    auto checkTransceiverActiveState =
        [this, &lastDownTimes](
            bool up, TransceiverStateMachineState expectedState) {
          XLOG(DBG2) << "Check transceiver active state for up=" << up;
          auto qsfpServiceHandler =
              getHwQsfpEnsemble()->getQsfpServiceHandler();
          auto wedgeMgr = getHwQsfpEnsemble()->getWedgeManager();
          qsfpServiceHandler->setOverrideAgentPortStatusForTesting(
              up, true /* enabled */);

          qsfpServiceHandler->refreshStateMachines();
          // Adding extra refresh cycle to account for PortManager differences.
          if (FLAGS_port_manager_mode) {
            qsfpServiceHandler->refreshStateMachines();
          }

          for (auto id : getPresentTransceivers()) {
            auto curState = wedgeMgr->getCurrentState(id);
            auto programmedPortToPortInfo =
                wedgeMgr->getProgrammedIphyPortToPortInfo(id);
            if (programmedPortToPortInfo.empty()) {
              throw FbossError(
                  "Unexpectedly no programmed ports for transceiver:", id);
            } else {
              // In PortManager mode, the furthest state that transceivers can
              // reach is TRANSCEIVER_PROGRAMMED
              auto expectedTcvrState = FLAGS_port_manager_mode
                  ? TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED
                  : expectedState;
              EXPECT_EQ(curState, expectedTcvrState)
                  << "Transceiver:" << id << " doesn't have expected state="
                  << apache::thrift::util::enumNameSafe(expectedTcvrState)
                  << " but actual state="
                  << apache::thrift::util::enumNameSafe(curState);

              // Check PortManagerState
              if (FLAGS_port_manager_mode) {
                for (const auto& [portId, portInfo] :
                     programmedPortToPortInfo) {
                  auto portState =
                      qsfpServiceHandler->getPortManager()->getPortState(
                          portId);
                  auto expectedPortState = up
                      ? PortStateMachineState::PORT_UP
                      : PortStateMachineState::PORT_DOWN;
                  EXPECT_EQ(portState, expectedPortState)
                      << "Port:" << portId << " doesn't have expected state="
                      << apache::thrift::util::enumNameSafe(expectedPortState)
                      << " but actual state="
                      << apache::thrift::util::enumNameSafe(portState);
                }
              }

              // Make sure if lastDownTimes has such transceiver, we compare the
              // new lastDownTime for such Transceiver with the value in the map
              // to make sure we'll get new lastDownTime
              if (curState == TransceiverStateMachineState::INACTIVE) {
                auto newDownTime = wedgeMgr->getLastDownTime(id);
                if (lastDownTimes.find(id) != lastDownTimes.end()) {
                  EXPECT_GT(newDownTime, lastDownTimes[id]);
                }
                lastDownTimes[id] = newDownTime;
              }
            }
          }

          // Also verify backplane XPHY ports (no transceiver): on retimer-only
          // platforms (Ladakh/Leh) these are most ports, so the loop above
          // would pass vacuously. PortManager mode only; mirrors
          // CheckPortsProgrammed.
          if (FLAGS_port_manager_mode) {
            auto* phyManager = getHwQsfpEnsemble()->getPhyManager();
            if (phyManager) {
              auto expectedPortState = up ? PortStateMachineState::PORT_UP
                                          : PortStateMachineState::PORT_DOWN;
              for (const auto& portId : phyManager->getXphyPorts()) {
                // Already checked in the transceiver loop above.
                if (utility::getTranscieverIdx(portId, getHwQsfpEnsemble())
                        .has_value()) {
                  continue;
                }
                if (!phyManager->getProgrammedProfile(portId).has_value()) {
                  continue;
                }
                auto portState =
                    qsfpServiceHandler->getPortManager()->getPortState(portId);
                EXPECT_EQ(portState, expectedPortState)
                    << "Backplane XPHY port:" << portId
                    << " doesn't have expected state="
                    << apache::thrift::util::enumNameSafe(expectedPortState)
                    << " but actual state="
                    << apache::thrift::util::enumNameSafe(portState);
              }
            }
          }
        };

    // First set all ports up
    checkTransceiverActiveState(true, TransceiverStateMachineState::ACTIVE);
    // Then set all ports down
    checkTransceiverActiveState(false, TransceiverStateMachineState::INACTIVE);
    // Set all ports up again
    checkTransceiverActiveState(true, TransceiverStateMachineState::ACTIVE);
    // Finally set all ports down to see whether lastDownTime got updated
    checkTransceiverActiveState(false, TransceiverStateMachineState::INACTIVE);
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(HwStateMachineTest, CheckTransceiverRemoved) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::TRANSCEIVER_REMOVAL});
  addTestedTransceivers(getPresentTransceivers());
  auto verify = [this]() {
    auto wedgeMgr = getHwQsfpEnsemble()->getWedgeManager();
    auto qsfpServiceHandler = getHwQsfpEnsemble()->getQsfpServiceHandler();
    auto portMgr =
        getHwQsfpEnsemble()->getQsfpServiceHandler()->getPortManager();
    bool hasXphy = (getHwQsfpEnsemble()->getPhyManager() != nullptr);
    qsfpServiceHandler->setOverrideAgentPortStatusForTesting(
        false /* up */, true /* enabled */);
    qsfpServiceHandler->refreshStateMachines();

    // Reset all present transceivers
    for (const auto& tcvrId : getPresentTransceivers()) {
      wedgeMgr->triggerQsfpHardReset(tcvrId);
      auto curState = wedgeMgr->getCurrentState(tcvrId);
      EXPECT_EQ(curState, TransceiverStateMachineState::NOT_PRESENT)
          << "Transceiver:" << tcvrId
          << " doesn't have expected state=NOT_PRESENT but actual state="
          << apache::thrift::util::enumNameSafe(curState);

      // In PortManager mode, we only expect ports to come back to initialized
      // state once their transceivers get re-discovered, to avoid any
      // pre-mature port programming.
      const auto programmedPortToPortInfo =
          wedgeMgr->getProgrammedIphyPortToPortInfo(tcvrId);
      if (FLAGS_port_manager_mode) {
        for (const auto& [portId, portInfo] : programmedPortToPortInfo) {
          auto curPortState = portMgr->getPortState(portId);
          EXPECT_EQ(curPortState, PortStateMachineState::PORT_DOWN)
              << "Port:" << portId
              << " doesn't have expected state=PORT_DOWN but actual state="
              << apache::thrift::util::enumNameSafe(curPortState);
        }
      }
    }

    // Refresh and ensure transceivers / ports go through expected programming
    // sequence
    std::unordered_map<TransceiverID, std::queue<TransceiverStateMachineState>>
        expectedStates;
    std::unordered_map<PortID, std::queue<PortStateMachineState>>
        expectedPortStates;

    for (const auto& id : getPresentTransceivers()) {
      expectedStates.emplace(
          id, getExpectedTransceiverStates(hasXphy, true /* isPresent */));

      if (FLAGS_port_manager_mode) {
        const auto programmedPortToPortInfo =
            wedgeMgr->getProgrammedIphyPortToPortInfo(id);
        for (const auto& [portId, portInfo] : programmedPortToPortInfo) {
          expectedPortStates.emplace(
              portId, getExpectedPortStates(hasXphy, false /* isUp */));
        }
      }
    }

    // Tunable optics take ~100s for datapath activation. Thus wait sufficient
    // time for the transceivers to complete transceiver programming state
    WITH_RETRIES_N_TIMED(
        30 /* retries */,
        std::chrono::milliseconds(10000) /* msBetweenRetry */,
        EXPECT_EVENTUALLY_TRUE(refreshStateMachinesTillMeetAllStates(
            expectedStates,
            expectedPortStates,
            false /* isRemediated */,
            false /* isAgentColdboot */)));
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(HwStateMachineTest, CheckTransceiverRemediated) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::REMEDIATION});
  addTestedTransceivers(getPresentTransceivers());
  auto verify = [this]() {
    std::set<TransceiverID> enabledTcvrs;
    auto wedgeMgr = getHwQsfpEnsemble()->getWedgeManager();
    auto qsfpServiceHandler = getHwQsfpEnsemble()->getQsfpServiceHandler();
    qsfpServiceHandler->setOverrideAgentPortStatusForTesting(
        true /* up */, true /* enabled */);
    setPauseRemediation(false);
    // Ensuring remediation doesn't get triggered for transceivers with all
    // active ports.
    qsfpServiceHandler->refreshStateMachines();
    for (auto id : getPresentTransceivers()) {
      auto curState = wedgeMgr->getCurrentState(id);
      bool isEnabled = !wedgeMgr->getProgrammedIphyPortToPortInfo(id).empty();
      auto expectedState = isEnabled ? TransceiverStateMachineState::ACTIVE
                                     : TransceiverStateMachineState::INACTIVE;
      if (FLAGS_port_manager_mode) {
        expectedState = isEnabled
            ? TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED
            : TransceiverStateMachineState::TRANSCEIVER_READY;
      }

      if (isEnabled) {
        enabledTcvrs.insert(id);
      }
      EXPECT_EQ(curState, expectedState)
          << "Transceiver:" << id << " doesn't have expected state="
          << apache::thrift::util::enumNameSafe(expectedState)
          << " but actual state="
          << apache::thrift::util::enumNameSafe(curState);
    }

    // Now set all ports down to trigger remediation
    qsfpServiceHandler->setOverrideAgentPortStatusForTesting(
        false /* up */, true /* enabled */);
    // Make sure all enabled transceiver should go through:
    // XPHY_PORTS_PROGRAMMED -> TRANSCEIVER_READY -> TRANSCEIVER_PROGRAMMED
    std::unordered_map<TransceiverID, std::queue<TransceiverStateMachineState>>
        expectedStates;
    std::unordered_map<PortID, std::queue<PortStateMachineState>>
        expectedPortStates;

    for (auto id : getPresentTransceivers()) {
      if (enabledTcvrs.find(id) == enabledTcvrs.end() ||
          !wedgeMgr->supportRemediateTransceiver(id)) {
        continue;
      }

      std::queue<TransceiverStateMachineState> tcvrExpectedStates;
      if (!FLAGS_port_manager_mode) {
        tcvrExpectedStates.push(
            TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED);
        tcvrExpectedStates.push(
            TransceiverStateMachineState::TRANSCEIVER_READY);
        tcvrExpectedStates.push(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED);
      } else {
        tcvrExpectedStates.push(TransceiverStateMachineState::DISCOVERED);
        tcvrExpectedStates.push(
            TransceiverStateMachineState::TRANSCEIVER_READY);
        tcvrExpectedStates.push(
            TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED);
      }

      expectedStates.emplace(id, tcvrExpectedStates);
      if (FLAGS_port_manager_mode) {
        const auto programmedPortToPortInfo =
            wedgeMgr->getProgrammedIphyPortToPortInfo(id);
        for (const auto& [portId, portInfo] : programmedPortToPortInfo) {
          expectedPortStates.emplace(portId, []() {
            std::queue<PortStateMachineState> q;
            q.push(PortStateMachineState::PORT_DOWN);
            return q;
          }());
        }
      }
    }

    // Tunable optics take ~100s for datapath activation. Thus wait sufficient
    // time for the transceivers to complete transceiver programming state
    WITH_RETRIES_N_TIMED(
        30 /* retries */,
        std::chrono::milliseconds(10000) /* msBetweenRetry */,
        EXPECT_EVENTUALLY_TRUE(refreshStateMachinesTillMeetAllStates(
            expectedStates,
            expectedPortStates,
            true /* isRemediated */,
            false /* isAgentColdboot */)));
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(HwStateMachineTest, CheckAgentConfigChanged) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::DATA_PATH_PROGRAMMING});
  addTestedTransceivers(getPresentTransceivers());
  addTestedTransceivers(getAbsentTransceivers());
  auto verify = [this]() {
    auto wedgeMgr = getHwQsfpEnsemble()->getWedgeManager();

    // Enable remediation to verify it doesn't incorrectly trigger during
    // config changes when ports are up
    setPauseRemediation(false);

    auto verifyConfigChanged = [this, &wedgeMgr](bool isAgentColdboot) {
      std::time_t testStartTime = std::time(nullptr);
      // Prepare expected states
      std::
          unordered_map<TransceiverID, std::queue<TransceiverStateMachineState>>
              expectedStates;
      std::unordered_map<PortID, std::queue<PortStateMachineState>>
          expectedPortStates;
      bool hasXphy = (getHwQsfpEnsemble()->getPhyManager() != nullptr);

      for (auto id : getPresentTransceivers()) {
        expectedStates.emplace(
            id, getExpectedTransceiverStates(hasXphy, true /* isPresent */));

        if (FLAGS_port_manager_mode) {
          const auto programmedPortToPortInfo =
              wedgeMgr->getProgrammedIphyPortToPortInfo(id);
          for (const auto& [portId, portInfo] : programmedPortToPortInfo) {
            expectedPortStates.emplace(
                portId, getExpectedPortStates(hasXphy, true /* isUp */));
          }
        }
      }

      for (auto id : getAbsentTransceivers()) {
        expectedStates.emplace(
            id, getExpectedTransceiverStates(hasXphy, false /* isPresent */));
      }

      // Override ConfigAppliedInfo
      ConfigAppliedInfo configAppliedInfo;
      auto currentInMs = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch());
      configAppliedInfo.lastAppliedInMs() = currentInMs.count();
      if (isAgentColdboot) {
        configAppliedInfo.lastColdbootAppliedInMs() = currentInMs.count();
      }

      getHwQsfpEnsemble()
          ->getQsfpServiceHandler()
          ->setOverrideAgentConfigAppliedInfoForTesting(configAppliedInfo);

      // Due to some platforms are easy to have i2c issue which causes the
      // current refresh not work as expected. Adding enough retries to make
      // sure that we at least can meet all `expectedStates` after 10 times.
      // ZR optics need more retries as datapath programming takes longer.
      bool hasTunableOptics = false;
      for (auto id : getPresentTransceivers()) {
        auto tcvrInfo = wedgeMgr->getTransceiverInfo(id);
        auto modTech = tcvrInfo.tcvrState()->moduleTechnology().value();
        if (modTech == ModuleTechnology::TUNABLE_C_BAND ||
            modTech == ModuleTechnology::TUNABLE_L_BAND) {
          hasTunableOptics = true;
          break;
        }
      }
      int numRetries = hasTunableOptics ? 30 : 10;

      WITH_RETRIES_N_TIMED(
          numRetries,
          std::chrono::milliseconds(10000) /* msBetweenRetry */,
          EXPECT_EVENTUALLY_TRUE(refreshStateMachinesTillMeetAllStates(
              expectedStates,
              expectedPortStates,
              false /* isRemediated */,
              isAgentColdboot)));

      // Verify datapath reset happened on a config change that involved agent
      // cold boot and didn't happen for warmboot
      for (auto id : getPresentTransceivers()) {
        auto tcvrInfo = wedgeMgr->getTransceiverInfo(id);
        auto& tcvrState = tcvrInfo.tcvrState().value();
        auto& tcvrStats = tcvrInfo.tcvrStats().value();
        for (auto& [portNameStr, _] : tcvrStats.portNameToHostLanes().value()) {
          XLOG(INFO) << "Verify datapath reset timestamp for port "
                     << portNameStr;
          utility::HwTransceiverUtils::verifyDatapathResetTimestamp(
              portNameStr,
              tcvrState,
              tcvrStats,
              testStartTime,
              isAgentColdboot);
        }

        // Verify remediation was NOT triggered during config change.
        // With ports up, remediation should never be triggered even though
        // it's enabled.
        EXPECT_FALSE(wedgeMgr->transceiverJustRemediated(id))
            << "Transceiver:" << id
            << " should NOT be remediated during config change (coldboot="
            << isAgentColdboot << ")";
      }
    };

    // First verify warmboot config change
    verifyConfigChanged(false /* isAgentColdboot */);
    // And then coldboot
    verifyConfigChanged(true /* isAgentColdboot */);
  };
  verifyAcrossWarmBoots([]() {}, verify);
}
} // namespace facebook::fboss
