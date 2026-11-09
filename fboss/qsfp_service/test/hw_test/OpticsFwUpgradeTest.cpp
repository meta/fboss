// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <set>

#include <folly/logging/xlog.h>
#include <folly/testing/TestUtil.h>
#include "common/time/Time.h"
#include "fboss/lib/CommonUtils.h"
#include "fboss/qsfp_service/QsfpConfig.h"
#include "fboss/qsfp_service/test/hw_test/HwPortUtils.h"
#include "fboss/qsfp_service/test/hw_test/HwTest.h"

using namespace ::testing;

namespace facebook::fboss {

namespace {
std::vector<std::string> transceiverStatesToNames(
    const std::vector<TransceiverStateMachineState>& states) {
  std::vector<std::string> stateNames;
  stateNames.reserve(states.size());
  for (const auto& state : states) {
    stateNames.push_back(apache::thrift::util::enumNameSafe(state));
  }
  return stateNames;
}

// firmwareForUpgradeTest only overrides the versions. Its fwHandleMap is empty,
// so swapping it in wholesale would leave the modules under test with no
// firmware storage handles.
cfg::TransceiverFirmware upgradeTestFirmware(
    const cfg::QsfpServiceConfig& qsfpCfg) {
  auto firmware = *qsfpCfg.qsfpTestConfig()->firmwareForUpgradeTest();
  if (const auto& tcvrFw = qsfpCfg.transceiverFirmwareVersions()) {
    firmware.fwHandleMap() = *tcvrFw->fwHandleMap();
  }
  return firmware;
}
} // namespace

class OpticsFwUpgradeTest : public HwTest {
 public:
  OpticsFwUpgradeTest(bool setupOverrideTcvrToPortAndProfile = true)
      : HwTest(setupOverrideTcvrToPortAndProfile) {}

  void SetUp() override {
    // Enable the firmware_upgrade_supported flag for these tests. Without this,
    // firmware upgrade functionality will be skipped
    gflags::SetCommandLineOptionWithMode(
        "firmware_upgrade_supported", "1", gflags::SET_FLAGS_DEFAULT);
    // Set max concurrent evb fw upgrade to 8 for cold boot init before the test
    // starts. This ensures that all the transceivers have a chance to upgrade
    // to the latest version of the firmware from the config before the actual
    // test starts later
    gflags::SetCommandLineOptionWithMode(
        "max_concurrent_evb_fw_upgrade", "8", gflags::SET_FLAGS_DEFAULT);
    // Set the firmware upgrade on coldboot flag to true so that any optics
    // running old firmware from previous tests get upgraded at start up
    gflags::SetCommandLineOptionWithMode(
        "firmware_upgrade_on_coldboot", "1", gflags::SET_FLAGS_DEFAULT);
    HwTest::SetUp();
    gflags::SetCommandLineOptionWithMode(
        "firmware_upgrade_on_tcvr_insert", "1", gflags::SET_FLAGS_DEFAULT);
    // Revert the max_concurrent_evb_fw_upgrade back to 1 which is the default
    gflags::SetCommandLineOptionWithMode(
        "max_concurrent_evb_fw_upgrade", "1", gflags::SET_FLAGS_DEFAULT);
  }

  void printDebugInfo(
      TransceiverID tcvrID,
      bool upgradeExpected,
      long upgradeSinceMs,
      const TcvrStats& tcvrStats) const {
    XLOG(INFO) << "Transceiver " << tcvrID
               << " :upgradeExpected = " << upgradeExpected
               << " :lastFwUpgradeStartTime = "
               << *tcvrStats.lastFwUpgradeStartTime()
               << ", lastFwUpgradeEndTime = "
               << *tcvrStats.lastFwUpgradeEndTime()
               << " :upgradeSince = " << upgradeSinceMs;
  }

  // Helper function that returns a list of transceivers that are upgradeable.
  // The criteria is that the transceiver part number is listed in the
  // qsfp config as being upgradeable.
  std::vector<int32_t> transceiversToTest() {
    std::vector<int32_t> tcvrsToTest;
    auto allTransceivers = utility::legacyTransceiverIds(
        utility::getCabledPortTranceivers(getHwQsfpEnsemble()));
    auto& qsfpTestCfg =
        apache::thrift::can_throw(*getHwQsfpEnsemble()
                                       ->getWedgeManager()
                                       ->getQsfpConfig()
                                       ->thrift.qsfpTestConfig());

    // Gather all transceivers that we need to include as part of this test
    // The criteria is that the qsfp test config defines a firmware version for
    // the transceiver part number
    for (auto tcvrID : allTransceivers) {
      auto tcvrInfo =
          getHwQsfpEnsemble()->getWedgeManager()->getTransceiverInfo(
              TransceiverID(tcvrID));
      auto& vendorPN = *tcvrInfo.tcvrState()->vendor().ensure().partNumber();
      if (qsfpTestCfg.firmwareForUpgradeTest()->versionsMap()->find(vendorPN) !=
          qsfpTestCfg.firmwareForUpgradeTest()->versionsMap()->end()) {
        tcvrsToTest.push_back(tcvrID);
      }
    }

    CHECK(!tcvrsToTest.empty()) << "No upgradeable transceivers found";

    return tcvrsToTest;
  }

  // Returns the ports requiring optics firmware upgrade, filtered down to only
  // cabled transceivers. The wedge manager reports every transceiver, but in
  // the test environment we only want to act on the cabled ones.
  std::map<std::string, FirmwareUpgradeData>
  getCabledPortsRequiringOpticsFwUpgrade() {
    auto wedgeMgr = getHwQsfpEnsemble()->getWedgeManager();
    auto portsForFwUpgrade = wedgeMgr->getPortsRequiringOpticsFwUpgrade();

    // Build the set of cabled port names using the same getPortName() mapping
    // that the wedge manager uses to key the returned map.
    std::set<std::string> cabledPortNames;
    for (const auto& tcvrID :
         utility::getCabledPortTranceivers(getHwQsfpEnsemble())) {
      cabledPortNames.insert(wedgeMgr->getPortName(tcvrID));
    }

    std::erase_if(portsForFwUpgrade, [&](const auto& portAndData) {
      return !cabledPortNames.contains(portAndData.first);
    });
    return portsForFwUpgrade;
  }

  // Helper function that verifies if an upgrade was done or not
  // Returns true if upgrade was done and was expected to be done, false
  // otherwise
  // Helper function that verifies if an upgrade was done or not
  // Returns true if upgrade was done and was expected to be done, false
  // otherwise
  bool verifyUpgrade(
      bool upgradeExpected,
      long upgradeSinceTsSec,
      std::vector<int32_t> tcvrs) {
    if (tcvrs.empty()) {
      XLOG(INFO) << "Chassis has no upgradeable transceivers";
      return true;
    }
    bool result = true;
    for (auto tcvrID : tcvrs) {
      auto tcvrInfo =
          getHwQsfpEnsemble()->getWedgeManager()->getTransceiverInfo(
              TransceiverID(tcvrID));
      auto& tcvrStats = *tcvrInfo.tcvrStats();
      if (upgradeExpected &&
          (*tcvrStats.lastFwUpgradeStartTime() <= upgradeSinceTsSec ||
           *tcvrStats.lastFwUpgradeEndTime() <=
               *tcvrStats.lastFwUpgradeStartTime())) {
        printDebugInfo(
            TransceiverID(tcvrID),
            upgradeExpected,
            upgradeSinceTsSec,
            tcvrStats);
        result = false;
      } else if (
          !upgradeExpected &&
          (*tcvrStats.lastFwUpgradeStartTime() > upgradeSinceTsSec ||
           *tcvrStats.lastFwUpgradeEndTime() > upgradeSinceTsSec)) {
        printDebugInfo(
            TransceiverID(tcvrID),
            upgradeExpected,
            upgradeSinceTsSec,
            tcvrStats);
        result = false;
      }
    }
    return result;
  }

  // Helper function that sets the port status of all transceivers to the given
  // status. It does that by setting setOverrideAgentPortStatusForTesting to
  // true and then refreshing the state machines so that the state can be
  // transitioned to ACTIVE or INACTIVE states.
  // Only checks state machine states for the transceivers in tcvrsToCheck.
  // This avoids false failures from non-upgradeable transceivers that may
  // be slow to reach the expected state due to hardware issues (e.g. I2C
  // errors invalidating cache and causing missed PORT_UP events).
  void setPortStatus(bool status, const std::vector<int32_t>& tcvrsToCheck) {
    auto wedgeMgr = getHwQsfpEnsemble()->getWedgeManager();
    auto qsfpServiceHandler = getHwQsfpEnsemble()->getQsfpServiceHandler();
    qsfpServiceHandler->setOverrideAgentPortStatusForTesting(
        status, true /* enabled */);
    qsfpServiceHandler->refreshStateMachines();

    for (auto id : tcvrsToCheck) {
      auto curState = wedgeMgr->getCurrentState(TransceiverID(id));
      if (status) {
        auto expectedState = FLAGS_port_manager_mode
            ? TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED
            : TransceiverStateMachineState::ACTIVE;
        EXPECT_EQ(curState, expectedState)
            << "Transceiver:" << id
            << " Actual: " << apache::thrift::util::enumNameSafe(curState)
            << ", Expected: "
            << apache::thrift::util::enumNameSafe(expectedState);

        if (FLAGS_port_manager_mode) {
          auto portMgr = qsfpServiceHandler->getPortManager();
          const auto& portToPortInfo =
              wedgeMgr->getProgrammedIphyPortToPortInfo(TransceiverID(id));
          for (const auto& [portId, tcvrPortInfo] : portToPortInfo) {
            if (!tcvrPortInfo.status.has_value()) {
              continue;
            }
            auto portStatus = tcvrPortInfo.status.value();
            if (portStatus.portEnabled && portStatus.operState) {
              auto portState = portMgr->getPortState(portId);
              EXPECT_EQ(portState, PortStateMachineState::PORT_UP)
                  << "PortID(" << portId << ") for Transceiver " << id
                  << " is not UP";
            }
          }
        }
      } else {
        // When forcing link down in tests, we also end up triggering
        // remediation since initial_remediate_interval is set to 0 in HwTest
        // SetUp. Since not every module can be remediated, the expected states
        // when forcing link down are either INACTIVE or XPHY_PORTS_PROGRAMMED
        // In addition, when firmware upgrade happens, the state machine
        // transitions from INACTIVE to UPGRADING to NOT_PRESENT and then
        // eventually to IPHY_PORTS_PROGRAMMED state. Therefore expect that too
        // when forcing link down
        std::vector<TransceiverStateMachineState> expectedStates =
            FLAGS_port_manager_mode
            ? std::vector<
                  TransceiverStateMachineState>{TransceiverStateMachineState::TRANSCEIVER_READY, TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED}
            : std::vector<TransceiverStateMachineState>{
                  TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED,
                  TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED,
                  TransceiverStateMachineState::INACTIVE};

        EXPECT_TRUE(
            std::find(expectedStates.begin(), expectedStates.end(), curState) !=
            expectedStates.end())
            << "Transceiver:" << id
            << " Actual: " << apache::thrift::util::enumNameSafe(curState)
            << ", Expected one of the states in expectedStates: "
            << folly::join(",", transceiverStatesToNames(expectedStates));
      }
    }
  }
};

class OpticsFwUpgradeTestNoIPhySetup : public OpticsFwUpgradeTest {
 public:
  OpticsFwUpgradeTestNoIPhySetup() : OpticsFwUpgradeTest(false) {}
};

TEST_F(OpticsFwUpgradeTest, noUpgradeForSameVersion) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::FIRMWARE_UPGRADE});
  // In this test, firmware versions in qsfp config is not changed. Hence, the
  // firmware upgrade shouldn't be triggered under any circumstances.
  // 1. Coldboot init might still trigger firmware upgrade depending on what
  // the firmware version was before the test started and what the firmware
  // version is in the qsfp config the test is run with
  // 2. Warmboot init, verify there were no upgrades done during warm boot.
  // 3. Force links to go down and verify there are no upgrades done

  long initDoneTimestampSec = facebook::WallClockUtil::NowInSecFast();
  auto tcvrsToTest = transceiversToTest();
  addTestedTransceiverIds(tcvrsToTest);
  auto verify = [&, tcvrsToTest]() {
    if (didWarmBoot()) {
      CHECK(verifyUpgrade(
          false /* upgradeExpected */,
          0 /* upgradeSinceTsSec */,
          tcvrsToTest /* tcvrs */))
          << "No upgrades expected during warm boot";
    }
    // Force link up
    setPortStatus(true, tcvrsToTest);
    CHECK(verifyUpgrade(
        false /* upgradeExpected */,
        initDoneTimestampSec /* upgradeSinceTsSec */,
        tcvrsToTest /* tcvrs */))
        << "No upgrades expected on port up";
    // Force link down
    setPortStatus(false, tcvrsToTest);
    CHECK(verifyUpgrade(
        false /* upgradeExpected */,
        initDoneTimestampSec /* upgradeSinceTsSec */,
        tcvrsToTest /* tcvrs */))
        << "No upgrades expected on port down";
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(OpticsFwUpgradeTestNoIPhySetup, noUpgradeOnWarmboot) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::FIRMWARE_UPGRADE});
  /*
   * This test verifies that a warmboot does not trigger any firmware upgrade
   * Step 1: Coldboot
   * Step 2: After modules are discovered, reload config with different fw
   * version.
   * Step 3: Program ports, bring the ports up
   * Step 4: Bring the ports down, this should trigger firmware download
   * Step 5: Warmboot
   * Step 6: Verify that no upgrades happened during warmboot
   */

  auto tcvrsToTest = transceiversToTest();
  addTestedTransceiverIds(tcvrsToTest);

  auto wedgeMgr = getHwQsfpEnsemble()->getWedgeManager();
  auto qsfpServiceHandler = getHwQsfpEnsemble()->getQsfpServiceHandler();

  // Lambda to refresh state machine and return true if all transceivers are
  // in TRANSCEIVER_PROGRAMMED state. In port manager mode, also check that
  // all ports are in PORT_UP state since the transceiver and port state
  // machines are separate and may transition at different rates.
  auto refreshStateMachinesAndCheckTcvrProgrammed = [&]() {
    qsfpServiceHandler->refreshStateMachines();
    for (auto id : tcvrsToTest) {
      auto curState = wedgeMgr->getCurrentState(TransceiverID(id));
      if (curState != TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED) {
        return false;
      }
      // In port manager mode, also verify ports are in expected state
      if (FLAGS_port_manager_mode) {
        auto portMgr = qsfpServiceHandler->getPortManager();
        const auto& portToPortInfo =
            wedgeMgr->getProgrammedIphyPortToPortInfo(TransceiverID(id));
        for (const auto& [portId, tcvrPortInfo] : portToPortInfo) {
          if (!tcvrPortInfo.status.has_value()) {
            continue;
          }
          auto portStatus = tcvrPortInfo.status.value();
          // If port is enabled, expect PORT_DOWN state since we set ports down
          if (portStatus.portEnabled) {
            auto portState = portMgr->getPortState(portId);
            if (portState != PortStateMachineState::PORT_DOWN) {
              return false;
            }
          }
        }
      }
    }
    return true;
  };

  // Lambda to toggle port status to trigger firmware download. It waits till
  // firmware download is complete
  auto togglePortsAndWaitForFwDownload = [&, this]() {
    setPortStatus(true, tcvrsToTest);
    // Bring the port down, this should trigger firmware download
    setPortStatus(false, tcvrsToTest);
    // Wait for fwUpgradeInProgress to clear to confirm all
    // upgrades are done
    WITH_RETRIES_N_TIMED(
        10 /* retries */,
        std::chrono::milliseconds(10000) /* msBetweenRetry */,
        {
          qsfpServiceHandler->refreshStateMachines();
          for (auto tcvrID : tcvrsToTest) {
            auto tcvrInfo = wedgeMgr->getTransceiverInfo(TransceiverID(tcvrID));
            auto& tcvrState = *tcvrInfo.tcvrState();
            EXPECT_EVENTUALLY_FALSE(*tcvrState.fwUpgradeInProgress());
          }
        });
  };

  auto setup = [&]() {
    // Set everything up so that ports get programmed
    gflags::SetCommandLineOptionWithMode(
        "override_program_iphy_ports_for_test", "1", gflags::SET_FLAGS_DEFAULT);
    wedgeMgr->setOverrideTcvrToPortAndProfileForTesting();
    if (FLAGS_port_manager_mode) {
      getHwQsfpEnsemble()
          ->getQsfpServiceHandler()
          ->setOverrideAgentPortStatusForTesting(
              false /* up */, true /* enabled */);
    }
    WITH_RETRIES_N_TIMED(
        10 /* retries */,
        std::chrono::milliseconds(10000) /* msBetweenRetry */,
        {
          EXPECT_EVENTUALLY_TRUE(refreshStateMachinesAndCheckTcvrProgrammed());
        });

    // Update the firmware versions in the config
    auto qsfpCfg = wedgeMgr->getQsfpConfig()->thrift;
    qsfpCfg.transceiverFirmwareVersions() = upgradeTestFirmware(qsfpCfg);
    std::string newCfgStr =
        apache::thrift::SimpleJSONSerializer::serialize<std::string>(qsfpCfg);
    auto newQsfpCfg = QsfpConfig::fromRawConfig(newCfgStr);
    folly::test::TemporaryDirectory tmpDir = folly::test::TemporaryDirectory();
    std::string newCfgPath =
        tmpDir.path().string() + "/optics_upgrade_test_config";
    newQsfpCfg->dumpConfig(newCfgPath);
    FLAGS_qsfp_config = newCfgPath;
    wedgeMgr->loadConfig();

    // Now that the config has changed, toggling port status will re-trigger
    // firmware download
    togglePortsAndWaitForFwDownload();
  };

  auto verify = [&]() {
    // If we just did a warm boot, we expect no upgrades to have happened
    if (didWarmBoot()) {
      qsfpServiceHandler->refreshStateMachines();
      // Another one to ensure if the upgrade was incorrectly triggered by
      // previous refresh, this refresh will update the latest firmware
      // upgrade timestamps in transceiverInfo causing verifyUpgrade to fail
      qsfpServiceHandler->refreshStateMachines();
      CHECK(verifyUpgrade(
          false /* upgradeExpected */,
          0 /* upgradeSinceTsSec */,
          tcvrsToTest /* tcvrs */))
          << "No upgrades expected";
    }
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(OpticsFwUpgradeTest, triggerOpticsFwUpgradeTest) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::FIRMWARE_UPGRADE});
  /*
   * This test verifies that triggerOpticsFwUpgrade and
   * triggerAllOpticsFwUpgrade functions work correctly.
   * ------------------------------------------------------------------------
   * Coldboot Setup:
   * - Create a new QSFP config with qsfpConfig.transceiverFirmwareVersions =
   *   qsfpConfig.qsfpTestConfig.firmwareForUpgradeTest
   * - Load the new config to make optics eligible for firmware upgrade
   * - Use triggerOpticsFwUpgrade to upgrade all eligible interfaces and verify
   * they are upgraded
   * ------------------------------------------------------------------------
   * Warmboot Verify:
   * - Use triggerAllOpticsFwUpgrade to upgrade all transceivers
   * - Verify all transceivers are upgraded
   */

  // Allow all optics using the same EVB to be upgraded in this test
  gflags::SetCommandLineOptionWithMode(
      "max_concurrent_evb_fw_upgrade", "8", gflags::SET_FLAGS_DEFAULT);
  long initDoneTimestampSec = facebook::WallClockUtil::NowInSecFast();

  auto tcvrsToTest = transceiversToTest();
  addTestedTransceiverIds(tcvrsToTest);

  auto wedgeMgr = getHwQsfpEnsemble()->getWedgeManager();
  auto qsfpServiceHandler = getHwQsfpEnsemble()->getQsfpServiceHandler();

  // Ensure firmwares were upgraded correctly by confirming there are no more
  // ports requiring firmware upgrade
  auto expectNoPortsRequireUpgrade = [&]() {
    auto remaining = getCabledPortsRequiringOpticsFwUpgrade();
    if (!remaining.empty()) {
      std::vector<std::string> portsStillRequiringUpgrade;
      portsStillRequiringUpgrade.reserve(remaining.size());
      for (const auto& [portToUpgrade, _] : remaining) {
        portsStillRequiringUpgrade.push_back(portToUpgrade);
      }
      ADD_FAILURE()
          << "The following ports still require upgrade, prior upgrades didn't succeed completely: "
          << folly::join(",", portsStillRequiringUpgrade);
    }
  };

  auto setup = [&]() {
    qsfpServiceHandler->refreshStateMachines();

    auto qsfpCfg = wedgeMgr->getQsfpConfig()->thrift;
    qsfpCfg.transceiverFirmwareVersions() = upgradeTestFirmware(qsfpCfg);
    std::string newCfgStr =
        apache::thrift::SimpleJSONSerializer::serialize<std::string>(qsfpCfg);
    auto newQsfpCfg = QsfpConfig::fromRawConfig(newCfgStr);
    folly::test::TemporaryDirectory tmpDir = folly::test::TemporaryDirectory();
    std::string newCfgPath =
        tmpDir.path().string() + "/optics_upgrade_test_config";
    newQsfpCfg->dumpConfig(newCfgPath);
    FLAGS_qsfp_config = newCfgPath;
    wedgeMgr->loadConfig();

    qsfpServiceHandler->refreshStateMachines();
    auto portsForFwUpgrade = getCabledPortsRequiringOpticsFwUpgrade();

    EXPECT_FALSE(portsForFwUpgrade.empty())
        << "No modules requiring firmware upgrade";

    if (!portsForFwUpgrade.empty()) {
      std::vector<std::string> interfacesToUpgrade;
      interfacesToUpgrade.reserve(portsForFwUpgrade.size());
      for (const auto& [portToUpgrade, _] : portsForFwUpgrade) {
        interfacesToUpgrade.push_back(portToUpgrade);
      }

      XLOG(INFO) << "Triggering firmware upgrade for interfaces: "
                 << folly::join(",", interfacesToUpgrade);

      std::map<std::string, FirmwareUpgradeData> upgradedPorts;
      qsfpServiceHandler->triggerOpticsFwUpgrade(
          upgradedPorts,
          std::make_unique<std::vector<std::string>>(interfacesToUpgrade));

      const auto& portNameToModule = wedgeMgr->getPortNameToModuleMap();
      WITH_RETRIES_N_TIMED(
          10 /* retries */,
          std::chrono::milliseconds(10000) /* msBetweenRetry */,
          {
            qsfpServiceHandler->refreshStateMachines();
            for (const auto& portName : interfacesToUpgrade) {
              auto tcvrID = TransceiverID(portNameToModule.at(portName));
              auto tcvrInfo = wedgeMgr->getTransceiverInfo(tcvrID);
              auto& tcvrState = *tcvrInfo.tcvrState();
              EXPECT_EVENTUALLY_FALSE(*tcvrState.fwUpgradeInProgress());
            }
          });

      std::vector<int32_t> upgradedTcvrIds;
      upgradedTcvrIds.reserve(interfacesToUpgrade.size());
      for (const auto& portName : interfacesToUpgrade) {
        upgradedTcvrIds.push_back(portNameToModule.at(portName));
      }
      // One more refresh to ensure TransceiverInfo cache has the
      // latest lastFwUpgradeEndTime set after upgradeFirmwareLocked
      qsfpServiceHandler->refreshStateMachines();
      CHECK(verifyUpgrade(
          true /* upgradeExpected */,
          initDoneTimestampSec /* upgradeSinceTsSec */,
          upgradedTcvrIds /* tcvrs */))
          << "Upgrade expected for selected interfaces";
      expectNoPortsRequireUpgrade();
    }
  };

  auto verify = [&, tcvrsToTest]() {
    if (didWarmBoot()) {
      qsfpServiceHandler->refreshStateMachines();
      auto portsForFwUpgrade = getCabledPortsRequiringOpticsFwUpgrade();

      if (!portsForFwUpgrade.empty()) {
        XLOG(INFO)
            << "Triggering firmware upgrade for all transceivers via triggerAllOpticsFwUpgrade";

        long verifyStartTimestampSec = facebook::WallClockUtil::NowInSecFast();

        std::vector<std::string> interfacesToUpgrade;
        interfacesToUpgrade.reserve(portsForFwUpgrade.size());
        for (const auto& [portToUpgrade, _] : portsForFwUpgrade) {
          interfacesToUpgrade.push_back(portToUpgrade);
        }

        std::map<std::string, FirmwareUpgradeData> upgradedPorts;
        XLOG(INFO)
            << "Triggering firmware upgrade for transceivers via triggerOpticsFwUpgrade: "
            << folly::join(",", interfacesToUpgrade);
        qsfpServiceHandler->triggerOpticsFwUpgrade(
            upgradedPorts,
            std::make_unique<std::vector<std::string>>(interfacesToUpgrade));

        EXPECT_FALSE(upgradedPorts.empty())
            << "Expected some ports to be selected for upgrade";

        const auto& portNameToModule = wedgeMgr->getPortNameToModuleMap();
        WITH_RETRIES_N_TIMED(
            10 /* retries */,
            std::chrono::milliseconds(10000) /* msBetweenRetry */,
            {
              qsfpServiceHandler->refreshStateMachines();
              for (auto tcvrID : tcvrsToTest) {
                auto tcvrInfo =
                    wedgeMgr->getTransceiverInfo(TransceiverID(tcvrID));
                auto& tcvrState = *tcvrInfo.tcvrState();
                EXPECT_EVENTUALLY_FALSE(*tcvrState.fwUpgradeInProgress());
              }
            });

        std::vector<int32_t> allUpgradedTcvrIds;
        allUpgradedTcvrIds.reserve(upgradedPorts.size());
        for (const auto& [upgradePortName, _] : upgradedPorts) {
          allUpgradedTcvrIds.push_back(portNameToModule.at(upgradePortName));
        }
        // One more refresh to ensure TransceiverInfo cache has the
        // latest lastFwUpgradeEndTime set after upgradeFirmwareLocked
        qsfpServiceHandler->refreshStateMachines();
        CHECK(verifyUpgrade(
            true /* upgradeExpected */,
            verifyStartTimestampSec,
            allUpgradedTcvrIds /* tcvrs */))
            << "Upgrade expected for all transceivers via triggerAllOpticsFwUpgrade";
        expectNoPortsRequireUpgrade();
      }
    }
  };

  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
