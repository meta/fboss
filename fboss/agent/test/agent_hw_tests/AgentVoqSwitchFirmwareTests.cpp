// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.
//
#include "fboss/agent/test/agent_hw_tests/AgentVoqSwitchTests.h"

#include <unistd.h>

#include "fboss/agent/FabricConnectivityManager.h"
#include "fboss/agent/test/utils/DsfConfigUtils.h"
#include "fboss/agent/test/utils/FabricTestUtils.h"
#include "fboss/agent/test/utils/PortTestUtils.h"
#include "fboss/lib/CommonUtils.h"

// For Netcastle runs, netcastle sets up db directory under
// /tmp and the isolation firmware is placed under that dir
// When running by hand, we can either mimic this or override the
// location via a command line argument
DEFINE_string(
    isolation_firmware_path,
    "/tmp/db/jericho3ai_a0/fi-2.4.0.1-GA.elf",
    "Path where isolation FW is placed");

namespace facebook::fboss {

constexpr auto kLinkDisableFirmwarePath =
    "/tmp/db/jericho3ai_a0/fi-2.4.13-GA.elf";
constexpr auto kLinkDisableFirmwarePathSdk14 =
    "/tmp/db/jericho3ai_a0/fi-2.6.6-GA.elf";

// For SDK >= 14.0, use fi-2.6.6-GA.elf instead of fi-2.4.13-GA.elf as the
// link disable isolation firmware. Since this test file is compiled once for
// all SDK versions (agent_voq_test_src is a shared library), compile-time
// SDK version checks via SaiVersion.h are not possible. Instead, use a
// filesystem-based approach: netcastle copies firmware files from the SDK
// directory to /tmp/db/jericho3ai_a0/ before launching the test binary, so
// file presence correctly indicates the SDK version. For SDK >= 14
// (6.5.34_dnx, 6.5.35_dnx), fi-2.6.6-GA.elf is present and gets selected.
// For older SDKs, only fi-2.4.13-GA.elf exists and is used as fallback.
std::string getLinkDisableFirmwarePath() {
  if (access(kLinkDisableFirmwarePathSdk14, F_OK) == 0) {
    return kLinkDisableFirmwarePathSdk14;
  }
  return kLinkDisableFirmwarePath;
}

template <bool enableLinkDisableFirmware>
struct EnableLinkDisableFirmware {
  static constexpr auto linkDisableFirmware = enableLinkDisableFirmware;
};
using FirmwareTypes = ::testing::
    Types<EnableLinkDisableFirmware<false>, EnableLinkDisableFirmware<true>>;

template <typename EnableLinkDisableFirmwareT>
class AgentVoqSwitchIsolationFirmwareTest : public AgentVoqSwitchTest {
  static auto constexpr linkDisableFirmware =
      EnableLinkDisableFirmwareT::linkDisableFirmware;

 public:
  bool isLinkDisableFirmware() const {
    return linkDisableFirmware == true;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::VOQ_SWITCH_WITH_FABRIC_PORTS};
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true, /*interfaceHasSubnet*/
        true, /*setInterfaceMac*/
        utility::kBaseVlanId,
        true /*enable fabric ports*/);
    utility::populatePortExpectedNeighborsToSelf(
        ensemble.masterLogicalPortIds(), config);
    /*
     * Clear FW config since only one FW config is supported today. We
     * started adding the default FW config in the config template so
     * all tests load the FW. However for these FW tests, we add more
     * custom config. So clear out the default config so we can prepare
     * afresh.
     */
    config = clearFWConfig(config);
    config = addFwConfig(config);
    return config;
  }

 protected:
  cfg::SwitchConfig clearFWConfig(cfg::SwitchConfig config) const {
    auto fwCapableSwitchIndices = getFWCapableSwitchIndices(config);
    auto& switchSettings = *config.switchSettings();
    for (auto& [_, switchInfo] : *switchSettings.switchIdToSwitchInfo()) {
      if (fwCapableSwitchIndices.find(*switchInfo.switchIndex()) !=
          fwCapableSwitchIndices.end()) {
        switchInfo.firmwareNameToFirmwareInfo()->clear();
      }
    }
    return config;
  }
  cfg::SwitchConfig addFwConfig(
      cfg::SwitchConfig config,
      const std::string firmwarePath = FLAGS_isolation_firmware_path) const {
    cfg::FirmwareInfo j3FwInfo;
    j3FwInfo.coreToUse() = 5;
    j3FwInfo.path() = firmwarePath;
    j3FwInfo.logPath() = "/tmp/edk.log";
    j3FwInfo.firmwareLoadType() =
        cfg::FirmwareLoadType::FIRMWARE_LOAD_TYPE_START;
    auto fwCapableSwitchIndices = getFWCapableSwitchIndices(config);
    auto& switchSettings = *config.switchSettings();
    for (auto& [_, switchInfo] : *switchSettings.switchIdToSwitchInfo()) {
      if (fwCapableSwitchIndices.find(*switchInfo.switchIndex()) !=
          fwCapableSwitchIndices.end()) {
        switchInfo.firmwareNameToFirmwareInfo()->insert(
            std::make_pair("isolationFirmware", j3FwInfo));
      }
    }
    return config;
  }
  std::set<uint16_t> getFWCapableSwitchIndices(
      const cfg::SwitchConfig& config) const {
    std::set<uint16_t> switchIndices;
    auto& switchSettings = *config.switchSettings();
    for (auto& [id, switchInfo] : *switchSettings.switchIdToSwitchInfo()) {
      if (switchInfo.asicType() == cfg::AsicType::ASIC_TYPE_JERICHO3) {
        switchIndices.insert(*switchInfo.switchIndex());
      }
    }
    return switchIndices;
  }
  void setMinLinksConfig() {
    auto config = getSw()->getConfig();
    auto& switchSettings = *config.switchSettings();
    switchSettings.minLinksToJoinVOQDomain() =
        ceil(masterLogicalFabricPortIds().size() * 0.66);
    switchSettings.minLinksToRemainInVOQDomain() =
        *switchSettings.minLinksToJoinVOQDomain() -
        masterLogicalFabricPortIds().size() / 8;
    applyNewConfig(config);
  }
  void assertSwitchDrainState(bool expectDrained) const {
    auto expectDrainState = expectDrained
        ? cfg::SwitchDrainState::DRAINED_DUE_TO_ASIC_ERROR
        : cfg::SwitchDrainState::UNDRAINED;
    auto switchId = this->getCurrentSwitchIdForTesting();
    // reachability should always be there regardless of drain state
    utility::checkFabricConnectivity(getAgentEnsemble(), switchId);
    HwSwitchMatcher matcher(std::unordered_set<SwitchID>({switchId}));
    WITH_RETRIES({
      const auto& switchSettings =
          getProgrammedState()->getSwitchSettings()->getSwitchSettings(matcher);
      EXPECT_EVENTUALLY_EQ(switchSettings->isSwitchDrained(), expectDrained);
      EXPECT_EVENTUALLY_EQ(
          switchSettings->getActualSwitchDrainState(), expectDrainState);
    });
  }
  void assertPortAndDrainState(bool expectDrained) const {
    assertSwitchDrainState(expectDrained);
    // Drained - expect inactive
    // Undrained - expect active
    utility::checkFabricPortsActiveState(
        getAgentEnsemble(), masterLogicalFabricPortIds(), !expectDrained);
  }
  void forceIsolate(int delay = 1) {
    auto switchId = this->getCurrentSwitchIdForTesting();
    std::stringstream ss;
    if (std::stoi(getSdkMajorVersion(switchId)) >= 14) {
      ss << "fabric firmware auto_isolate force enable=1 delay=" << delay
         << std::endl;
    } else {
      ss << "edk -c fi force_isolate 0 5 1 " << delay << std::endl;
    }
    XLOG(INFO) << "Running force_isolate command: ";
    std::string out;
    getAgentEnsemble()->runDiagCommand(ss.str(), out, switchId);
    XLOG(INFO) << "force_isolate output: " << out;
  }
  void getIsolate() {
    auto switchId = this->getCurrentSwitchIdForTesting();
    std::stringstream ss;
    if (std::stoi(getSdkMajorVersion(switchId)) >= 14) {
      ss << "fabric firmware auto_isolate dump" << std::endl;
    } else {
      ss << "edk -c fi dump 0 5" << std::endl;
    }
    XLOG(INFO) << "Running get_isolate command: ";
    std::string out;
    getAgentEnsemble()->runDiagCommand(ss.str(), out, switchId);
    XLOG(INFO) << "get_isolate output: " << out;
  }
  void forceCrash(int delay = 1) {
    auto switchId = this->getCurrentSwitchIdForTesting();
    std::stringstream ss;
    if (std::stoi(getSdkMajorVersion(switchId)) >= 14) {
      ss << "fabric firmware auto_isolate fw_crash delay=" << delay
         << std::endl;
    } else {
      ss << "edk -c fi crash 0 5 " << delay << std::endl;
    }
    std::string out;
    getAgentEnsemble()->runDiagCommand(ss.str(), out, switchId);
  }
  void forceLinkAdminDisable(int portId) {
    auto platformMapping = getAgentEnsemble()->getPlatformMapping();
    const auto& portEntry =
        platformMapping->getPlatformPort(static_cast<int32_t>(portId));
    auto iphyLane = portEntry.mapping()->pins()[0].a()->lane().value();

    auto iphyChip = platformMapping->getPortIphyChip(PortID(portId));
    auto coreId = iphyChip.physicalID().value();

    auto virtualDeviceId = portEntry.mapping()->virtualDeviceId().value();
    auto firstFabricPort =
        platformMapping->getFirstFabricPortForVirtualDevice(virtualDeviceId);
    auto baseCoreId =
        platformMapping->getPortIphyChip(firstFabricPort).physicalID().value();
    auto coreIndex = coreId - baseCoreId;

    constexpr int kFabricLanesPerCore = 8;
    auto portToDisable = coreIndex * kFabricLanesPerCore + iphyLane;

    auto switchId = this->getCurrentSwitchIdForTesting();
    std::stringstream ss;
    if (std::stoi(getSdkMajorVersion(switchId)) >= 14) {
      ss << "fabric firmware auto_link_disable force " << (1024 + portToDisable)
         << " enable=1" << std::endl;
    } else {
      ss << "edk -c fi force_link_down 0 5 " << portToDisable << " 1"
         << std::endl;
    }
    XLOG(INFO) << "Running force link Admin Disable command to disable port: "
               << portId << " asic port index: " << portToDisable;
    std::string out;
    getAgentEnsemble()->runDiagCommand(ss.str(), out, switchId);
    XLOG(INFO) << "force link admin disable output: " << out;
  }
  /*
   * Invoked post FW crash to
   * - Induce a isolate event through FW
   * - Verify that event takes effect
   *   This is a concrete way of verifying that
   *   FW is functional post a crash
   */
  void forceIsolatePostCrashAndVerify() {
    WITH_RETRIES({
      // We issue force isolation with retries, since the FW
      // just crashed. So our command to force isolate may
      // race with FW restarting
      forceIsolate();
      assertSwitchDrainState(true /* drained */);
    });
  }

  std::map<int32_t, std::string> getSdkRegDumpFiles() const {
    std::map<int32_t, std::string> switchIdToSdkRegDumpFile;

    auto config = initialConfig(*getAgentEnsemble());
    auto fwCapableSwitchIndices = getFWCapableSwitchIndices(config);
    for (auto& [switchId, switchInfo] :
         *config.switchSettings()->switchIdToSwitchInfo()) {
      if (fwCapableSwitchIndices.find(*switchInfo.switchIndex()) !=
          fwCapableSwitchIndices.end()) {
        switchIdToSdkRegDumpFile[switchId] = folly::to<std::string>(
            sdkRegDumpPathPrefix_, "_", switchId, ".log");
      }
    }

    return switchIdToSdkRegDumpFile;
  }

  bool expectFabricPortsActivePostIsolate() {
    auto switchId = this->getCurrentSwitchIdForTesting();
    auto sdkMajorVersionStr = getSdkMajorVersion(switchId);
    auto sdkMajorVersionNum = std::stoi(sdkMajorVersionStr);
    XLOG(DBG2) << "SDK major version number is " << sdkMajorVersionNum
               << " for switch ID " << switchId;
    if (sdkMajorVersionNum >= 14) {
      // For SDKs starting from 14.x, fabric ports would be in INACTIVE
      // state after isolation (this is the correct behavior)
      return false;
    }
    return true;
  }

  void assertFirmwareInfo(
      const FirmwareOpStatus& expectedOpStatus,
      const FirmwareFuncStatus& expectedFuncStatus) {
    auto switchId = this->getCurrentSwitchIdForTesting();
    auto firmwareInfoList = getAgentEnsemble()->getAllFirmwareInfo(switchId);

    if (firmwareInfoList.empty()) {
      // 11.7 Does not support querying firmwareInfo yet.
      // After that support is added, remove this if check.
      // This is better than marking this test known bad for 11.7 as we want
      // to continue to validate forceIsolate which is supported on 11.7
      return;
    }

    CHECK_EQ(firmwareInfoList.size(), 1);
    auto firmwareInfo = firmwareInfoList[0];

    XLOG(DBG2) << "FirmwareInfo:: version: " << *firmwareInfo.version()
               << " opStatus: "
               << apache::thrift::util::enumNameSafe(
                      firmwareInfo.opStatus().value())
               << " funcStatus: "
               << apache::thrift::util::enumNameSafe(
                      firmwareInfo.funcStatus().value());

    EXPECT_FALSE(firmwareInfo.version()->empty());
    EXPECT_TRUE(firmwareInfo.opStatus().value() == expectedOpStatus);
    EXPECT_TRUE(firmwareInfo.funcStatus().value() == expectedFuncStatus);
  }

 protected:
  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_hide_fabric_ports = false;
    // Fabric connectivity manager to expect single NPU
    if (!FLAGS_multi_switch) {
      FLAGS_janga_single_npu_for_testing = true;
    }
    FLAGS_sdk_reg_dump_path_prefix = sdkRegDumpPathPrefix_;

    if (isLinkDisableFirmware()) {
      FLAGS_isolation_firmware_path = getLinkDisableFirmwarePath();
    } else {
      FLAGS_isolation_firmware_path = "/tmp/db/jericho3ai_a0/fi-2.4.0.1-GA.elf";
    }
  }

 private:
  std::string sdkRegDumpPathPrefix_{"/tmp/sdk_reg_dump"};
};

TYPED_TEST_SUITE(AgentVoqSwitchIsolationFirmwareTest, FirmwareTypes);

template <typename EnableLinkDisableFirmwareT>
class AgentVoqSwitchIsolationFirmwareWBEventsTest
    : public AgentVoqSwitchIsolationFirmwareTest<EnableLinkDisableFirmwareT> {
  static bool isColdBoot;
  static uint16_t fwCapableSwitchIndex;

 public:
  static constexpr auto kEventDelay = 30;

 private:
  void setCmdLineFlagOverrides() const override {
    AgentVoqSwitchIsolationFirmwareTest<
        EnableLinkDisableFirmwareT>::setCmdLineFlagOverrides();
    FLAGS_agent_exit_delay_s = 60;
  }
  void tearDownAgentEnsemble(bool warmboot = false) override {
    // We check for agentEnsemble not being NULL since the tests
    // are also invoked for just listing their prod features.
    // Then in such case, agentEnsemble is never setup
    isColdBoot = this->getAgentEnsemble() &&
        this->getSw()->getBootType() == BootType::COLD_BOOT;
    if (isColdBoot) {
      auto switchId = this->getCurrentSwitchIdForTesting();
      auto config = this->getSw()->getConfig();
      auto it = config.switchSettings()->switchIdToSwitchInfo()->find(
          static_cast<int64_t>(switchId));
      CHECK(it != config.switchSettings()->switchIdToSwitchInfo()->end());
      fwCapableSwitchIndex = *it->second.switchIndex();
    }
    if (isColdBoot) {
      WITH_RETRIES({
        auto crashCounterRegex = std::string("(switch.") +
            folly::to<std::string>(fwCapableSwitchIndex) + ".)?" +
            "bcm.isolationFirmwareCrash.sum";
        auto fwCrashedCounters =
            this->getAgentEnsemble()->getFb303RegexCounters(
                crashCounterRegex, this->getCurrentSwitchIdForTesting());
        EXPECT_EVENTUALLY_TRUE(
            fwCrashedCounters.size() == 1 &&
            fwCrashedCounters.begin()->second == 0);
      });
    }

    std::atexit([]() {
      if (isColdBoot) {
        WITH_RETRIES({
          auto fwIsolated = fb303::fbData->getCounterIfExists(
              "fw_drained_with_high_num_active_fabric_links.sum");
          EXPECT_EVENTUALLY_TRUE(
              fwIsolated.has_value() && fwIsolated.value() == 0);
        });
      }
    });
    AgentHwTest::tearDownAgentEnsemble(warmboot);
  }
};

TYPED_TEST_SUITE(AgentVoqSwitchIsolationFirmwareWBEventsTest, FirmwareTypes);

template <typename EnableLinkDisableFirmwareT>
bool AgentVoqSwitchIsolationFirmwareWBEventsTest<
    EnableLinkDisableFirmwareT>::isColdBoot = false;

template <typename EnableLinkDisableFirmwareT>
uint16_t AgentVoqSwitchIsolationFirmwareWBEventsTest<
    EnableLinkDisableFirmwareT>::fwCapableSwitchIndex = 0;

template <typename EnableLinkDisableFirmwareT>
class AgentVoqSwitchIsolationFirmwareUpdateTest
    : public AgentVoqSwitchIsolationFirmwareTest<EnableLinkDisableFirmwareT> {
 public:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true, /*interfaceHasSubnet*/
        true, /*setInterfaceMac*/
        utility::kBaseVlanId,
        true /*enable fabric ports*/);
    utility::populatePortExpectedNeighborsToSelf(
        ensemble.masterLogicalPortIds(), config);
    config = this->clearFWConfig(config);
    return config;
  }
  void tearDownAgentEnsemble(bool warmboot = false) override {
    // We check for agentEnsemble not being NULL since the tests
    // are also invoked for just listing their prod features.
    // Then in such case, agentEnsemble is never setup
    if (this->getAgentEnsemble() &&
        this->getSw()->getBootType() == BootType::COLD_BOOT) {
      auto agentConfig = this->getSw()->getAgentConfig();
      const auto& configFileName = this->getAgentEnsemble()->configFileName();
      agentConfig.sw() = this->addFwConfig(*agentConfig.sw());
      AgentEnsemble::writeConfig(agentConfig, configFileName);
    }
    AgentHwTest::tearDownAgentEnsemble(warmboot);
  }
};

TYPED_TEST_SUITE(AgentVoqSwitchIsolationFirmwareUpdateTest, FirmwareTypes);

TYPED_TEST(AgentVoqSwitchIsolationFirmwareTest, forceIsolate) {
  auto setup = [this]() {
    this->assertPortAndDrainState(false /* not drained*/);
    this->setMinLinksConfig();
    this->assertFirmwareInfo(
        FirmwareOpStatus::RUNNING, FirmwareFuncStatus::MONITORING);
    this->forceIsolate();
  };

  auto verify = [this]() {
    this->assertSwitchDrainState(true /* drained */);
    utility::checkFabricPortsActiveState(
        this->getAgentEnsemble(),
        this->masterLogicalFabricPortIds(),
        this->expectFabricPortsActivePostIsolate());
    this->assertFirmwareInfo(
        FirmwareOpStatus::STOPPED, FirmwareFuncStatus::ISOLATED);
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentVoqSwitchIsolationFirmwareTest, forceCrash) {
  auto setup = [this]() {
    this->assertPortAndDrainState(false /* not drained*/);
    this->setMinLinksConfig();
  };

  auto verify = [this]() {
    this->forceCrash();
    auto switchId = this->getCurrentSwitchIdForTesting();
    auto config = this->getSw()->getConfig();
    auto& switchIdToSwitchInfo =
        *config.switchSettings()->switchIdToSwitchInfo();
    auto switchIdx =
        *switchIdToSwitchInfo.at(static_cast<int64_t>(switchId)).switchIndex();
    WITH_RETRIES({
      auto switchStats = this->getHwSwitchStats(switchIdx);
      auto asicErrors = switchStats.hwAsicErrors();
      EXPECT_EVENTUALLY_GT(asicErrors->isolationFirmwareCrashes(), 0);
    });
    this->forceIsolatePostCrashAndVerify();
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentVoqSwitchIsolationFirmwareTest, forceLinkAdminDisable) {
  if (!this->isLinkDisableFirmware()) {
#if defined(GTEST_SKIP)
    GTEST_SKIP();
#endif
    return;
  }

  auto setup = [this]() {
    this->assertPortAndDrainState(false /* not drained */);
    this->setMinLinksConfig();
    this->assertFirmwareInfo(
        FirmwareOpStatus::RUNNING, FirmwareFuncStatus::MONITORING);

    utility::checkFabricPortsActiveState(
        this->getAgentEnsemble(),
        this->masterLogicalFabricPortIds(),
        true /* expect ACTIVE */);
    ASSERT_TRUE(!this->masterLogicalFabricPortIds().empty());
    this->forceLinkAdminDisable(this->masterLogicalFabricPortIds()[0]);
  };

  auto verify = [this]() {
    // setup() induces Firmware link disable.
    // On Firmware link disable, SAI implementation issues a callback.
    // Agent processes this callback to Admin Disable the link.
    // However, since the config is not modified, the link is Admin
    // re-enabled post-warmboot.
    // Thus, verify() DISABLED for coldboot runs, but ENABLED for warmboot runs.
    auto portId = this->masterLogicalFabricPortIds()[0];
    auto expectedAdminState =
        this->getSw()->getBootType() == BootType::COLD_BOOT
        ? cfg::PortState::DISABLED
        : cfg::PortState::ENABLED;

    utility::checkPortsAdminState(
        this->getAgentEnsemble(), {portId}, expectedAdminState);

    WITH_RETRIES({
      if (this->getSw()->getBootType() == BootType::COLD_BOOT) {
        auto port = this->getProgrammedState()->getPorts()->getNode(portId);
        std::vector<PortError> expectedErrors = {
            PortError::LINK_DISABLED_BY_FIRMWARE};
        EXPECT_EVENTUALLY_EQ(port->getActiveErrors(), expectedErrors);
      }
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(
    AgentVoqSwitchIsolationFirmwareWBEventsTest,
    forceIsolateDuringWarmBoot) {
  auto setup = [this]() {
    this->assertPortAndDrainState(false /* not drained*/);
    this->setMinLinksConfig();
    XLOG(ERR)
        << "Schedule a delayed isolate in the firmware. Agent would have unregistered SDK callbacks by that time.";
    this->forceIsolate(this->kEventDelay);
    /*
    Issue: The force_isolate command sometimes doesn't get flushed through the
    diagnostic shell before binaries exit, despite the SDK call being made.

    Mitigation: We add a second command (get_isolate) after force_isolate. Even
    if get_isolate doesn't get flushed either, it's not critical since the test
    doesn't depend on its execution - it only ensures force_isolate gets
    processed.

    TODO: Think of a long term solution to this.
    */
    this->getIsolate();
  };

  auto verifyPostWarmboot = [this]() {
    this->getIsolate();
    this->assertSwitchDrainState(true /* drained */);
    utility::checkFabricPortsActiveState(
        this->getAgentEnsemble(),
        this->masterLogicalFabricPortIds(),
        this->expectFabricPortsActivePostIsolate());
  };
  this->verifyAcrossWarmBoots(setup, []() {}, []() {}, verifyPostWarmboot);
}

TYPED_TEST(
    AgentVoqSwitchIsolationFirmwareWBEventsTest,
    forceCrashDuringWarmBoot) {
  auto setup = [this]() {
    this->assertPortAndDrainState(false /* not drained*/);
    this->setMinLinksConfig();
    // Force delayed crash - which most likely
    // will now happen while switch is warm booting
    this->forceCrash(this->kEventDelay);
  };

  auto verifyPostWarmboot = [this]() {
    this->forceIsolatePostCrashAndVerify();
  };
  this->verifyAcrossWarmBoots(setup, []() {}, []() {}, verifyPostWarmboot);
}

TYPED_TEST(AgentVoqSwitchIsolationFirmwareUpdateTest, loadOnWarmboot) {
  auto setup = [this]() {
    this->assertPortAndDrainState(false /* not drained*/);
    this->setMinLinksConfig();
  };

  auto verifyPostWarmboot = [this]() {
    this->forceIsolate();
    this->assertSwitchDrainState(true /* drained */);
  };
  this->verifyAcrossWarmBoots(setup, []() {}, []() {}, verifyPostWarmboot);
}

template <typename EnableLinkDisableFirmwareT>
class AgentVoqSwitchIsolationFirmwareUpgradeDownGrade
    : public AgentVoqSwitchIsolationFirmwareUpdateTest<
          EnableLinkDisableFirmwareT> {
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true, /*interfaceHasSubnet*/
        true, /*setInterfaceMac*/
        utility::kBaseVlanId,
        true /*enable fabric ports*/);
    utility::populatePortExpectedNeighborsToSelf(
        ensemble.masterLogicalPortIds(), config);
    config = this->clearFWConfig(config);

    // AgentVoqSwitchIsolationFirmwareUpgradeDownGrade/0.* run tests warmboot
    // transition from fw version 2.4.0.1 to link disable fw.
    // AgentVoqSwitchIsolationFirmwareUpgradeDownGrade/1.* tests the reverse
    // transition.
    std::string fwPath;
    if (this->isLinkDisableFirmware()) {
      fwPath = getLinkDisableFirmwarePath();
    } else {
      fwPath = "/tmp/db/jericho3ai_a0/fi-2.4.0.1-GA.elf";
    }
    config = this->addFwConfig(config, fwPath);
    return config;
  }

  void tearDownAgentEnsemble(bool warmboot = false) override {
    // We check for agentEnsemble not being NULL since the tests
    // are also invoked for just listing their prod features.
    // Then in such case, agentEnsemble is never setup
    if (this->getAgentEnsemble() &&
        this->getSw()->getBootType() == BootType::COLD_BOOT) {
      auto agentConfig = this->getSw()->getAgentConfig();
      const auto& configFileName = this->getAgentEnsemble()->configFileName();
      agentConfig.sw() = this->clearFWConfig(*agentConfig.sw());

      // To test firmware transition on warmboot, if we chose link disable fw
      // during cb, configure 2.4.0.1 post wb and vice-versa.
      std::string fwPath;
      if (!this->isLinkDisableFirmware()) {
        fwPath = getLinkDisableFirmwarePath();
      } else {
        fwPath = "/tmp/db/jericho3ai_a0/fi-2.4.0.1-GA.elf";
      }
      agentConfig.sw() =
          this->addFwConfig(*agentConfig.sw(), std::move(fwPath));
      AgentEnsemble::writeConfig(agentConfig, configFileName);
    }
    AgentHwTest::tearDownAgentEnsemble(warmboot);
  }
};

TYPED_TEST_SUITE(
    AgentVoqSwitchIsolationFirmwareUpgradeDownGrade,
    FirmwareTypes);

TYPED_TEST(AgentVoqSwitchIsolationFirmwareUpgradeDownGrade, firmwareChange) {
  auto setup = [this]() {
    this->assertPortAndDrainState(false /* not drained*/);
    this->setMinLinksConfig();
  };

  auto verify = [this]() {
    this->assertFirmwareInfo(
        FirmwareOpStatus::RUNNING, FirmwareFuncStatus::MONITORING);

    // With one firmware running, warmboot upgrade to a different firmware,
    // then force firmware isolate and verify that the newly loaded firmware can
    // isolate as expected.
    // Thus, force isolate during warmboot only.
    if (this->getSw()->getBootType() == BootType::WARM_BOOT) {
      this->forceIsolate();
      this->assertSwitchDrainState(true /* drained */);
      utility::checkFabricPortsActiveState(
          this->getAgentEnsemble(),
          this->masterLogicalFabricPortIds(),
          this->expectFabricPortsActivePostIsolate());
    }
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
