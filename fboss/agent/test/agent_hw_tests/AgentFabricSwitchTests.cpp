// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <algorithm>
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/hw/HwSwitchFb303Stats.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/DsfConfigUtils.h"
#include "fboss/agent/test/utils/FabricTestUtils.h"
#include "fboss/lib/CommonUtils.h"

#include <fmt/format.h>

#include "folly/testing/TestUtil.h"

namespace {
constexpr auto kNumRemoteFabricNodes = 8;
constexpr auto kNumParallelLinks = 5;
} // namespace

namespace facebook::fboss {
class AgentFabricSwitchTest : public AgentHwTest {
 public:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = AgentHwTest::initialConfig(ensemble);
    utility::populatePortExpectedNeighborsToSelf(
        ensemble.masterLogicalPortIds(), config);
    return config;
  }

  void SetUp() override {
    AgentHwTest::SetUp();
    if (!IsSkipped()) {
      ASSERT_TRUE(
          std::any_of(getAsics().begin(), getAsics().end(), [](auto& iter) {
            return iter.second->getSwitchType() == cfg::SwitchType::FABRIC;
          }));
      ASSERT_FALSE(
          getAgentEnsemble()
              ->masterLogicalFabricPortIds(getCurrentSwitchIdForTesting())
              .empty())
          << "No fabric ports for switchId: "
          << static_cast<int>(getCurrentSwitchIdForTesting());
    }
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::FABRIC};
  }
  /*
   * Get switchIds of type fabric which have non zero fabric ports
   */
  std::unordered_set<SwitchID> getFabricSwitchIdsWithPorts() const {
    auto fabSwitchIds = getSw()->getSwitchInfoTable().getSwitchIdsOfType(
        cfg::SwitchType::FABRIC);
    // Return fabric switches with ports
    std::erase_if(fabSwitchIds, [this](auto switchId) {
      return getAgentEnsemble()->masterLogicalFabricPortIds(switchId).empty();
    });
    CHECK_GT(fabSwitchIds.size(), 0) << " No fab switch ids found";
    return fabSwitchIds;
  }

 protected:
  std::vector<PortID> fabricPortIdsForTesting() const {
    return getAgentEnsemble()->masterLogicalFabricPortIds(
        getCurrentSwitchIdForTesting());
  }

  std::map<SwitchID, std::vector<PortID>> switch2FabricPortIds() const {
    std::map<SwitchID, std::vector<PortID>> switch2FabricPortIds;
    for (auto switchId : getFabricSwitchIdsWithPorts()) {
      auto fabricPortIds =
          getAgentEnsemble()->masterLogicalFabricPortIds(switchId);
      CHECK(!fabricPortIds.empty());
      switch2FabricPortIds[switchId] = std::move(fabricPortIds);
    }
    return switch2FabricPortIds;
  }
  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_hide_fabric_ports = false;
  }
};

TEST_F(AgentFabricSwitchTest, init) {
  auto setup = []() {};
  auto verify = [this]() {
    auto switchId = getCurrentSwitchIdForTesting();
    auto portAsic = getSw()->getHwAsicTable()->getHwAsic(switchId);
    for (auto portId : fabricPortIdsForTesting()) {
      auto port = getProgrammedState()->getPorts()->getNode(portId);
      EXPECT_EQ(port->getAdminState(), cfg::PortState::ENABLED);
      EXPECT_EQ(
          port->getLoopbackMode(),
          portAsic->getDesiredLoopbackMode(port->getPortType()));
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentFabricSwitchTest, checkFabricConnectivityStats) {
  auto setup = [=, this]() {
    auto newCfg = getSw()->getConfig();
    // reset the neighbor reachability information
    for (const auto& portID : fabricPortIdsForTesting()) {
      auto portCfg = utility::findCfgPort(newCfg, portID);
      portCfg->expectedNeighborReachability() = {};
    }
    applyNewConfig(newCfg);
  };
  auto verify = [this]() {
    EXPECT_GT(getProgrammedState()->getPorts()->numNodes(), 0);
    WITH_RETRIES({
      auto reachabilityStats = getAgentEnsemble()->getFabricReachabilityStats();
      EXPECT_EVENTUALLY_EQ(
          *reachabilityStats.missingCount(), fabricPortIdsForTesting().size());
      EXPECT_EVENTUALLY_EQ(
          *reachabilityStats.mismatchCount(), fabricPortIdsForTesting().size());
    });
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentFabricSwitchTest, collectStats) {
  auto verify = [this]() {
    EXPECT_GT(getProgrammedState()->getPorts()->numNodes(), 0);
    WITH_RETRIES({
      getSw()->updateStats();
      for (auto portId : fabricPortIdsForTesting()) {
        auto port = getProgrammedState()->getPorts()->getNode(portId);
        auto loadBearingInErrors = fb303::fbData->getCounterIfExists(
            port->getName() + ".load_bearing_in_errors.sum.60");
        auto loadBearingFecErrors = fb303::fbData->getCounterIfExists(
            port->getName() + ".load_bearing_fec_uncorrectable_errors.sum.60");
        auto loadBearingFlaps = fb303::fbData->getCounterIfExists(
            port->getName() + ".load_bearing_link_state.flap.sum.60");
        EXPECT_EVENTUALLY_TRUE(loadBearingInErrors.has_value());
        EXPECT_EVENTUALLY_TRUE(loadBearingFecErrors.has_value());
        if (getAgentEnsemble()->getBootType() == BootType::COLD_BOOT) {
          EXPECT_EVENTUALLY_TRUE(loadBearingFlaps.has_value());
        } else {
          // No port flaps after wb, hence no port flap stats being recorded
          EXPECT_FALSE(loadBearingFlaps.has_value());
        }
      }
    });
  };
  verifyAcrossWarmBoots(verify);
}

TEST_F(AgentFabricSwitchTest, checkFabricConnectivity) {
  auto verify = [this]() {
    EXPECT_GT(getProgrammedState()->getPorts()->numNodes(), 0);
    auto switchId = getCurrentSwitchIdForTesting();
    utility::checkFabricConnectivity(getAgentEnsemble(), switchId);
  };
  verifyAcrossWarmBoots(verify);
}

TEST_F(AgentFabricSwitchTest, fabricPortIsolate) {
  auto switchId = getCurrentSwitchIdForTesting();
  auto allFabricPortIds = fabricPortIdsForTesting();
  auto drainedPortId = allFabricPortIds[0];
  auto setup = [=, this]() {
    auto newCfg = getSw()->getConfig();
    for (auto& portCfg : *newCfg.ports()) {
      if (PortID(*portCfg.logicalID()) == drainedPortId) {
        *portCfg.drainState() = cfg::PortDrainState::DRAINED;
      }
    }
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    EXPECT_GT(getProgrammedState()->getPorts()->numNodes(), 0);
    utility::checkPortFabricReachability(
        getAgentEnsemble(), switchId, drainedPortId);
    std::vector<PortID> undrainedPortIds;
    std::vector<PortID> drainedPortIds = {drainedPortId};
    for (auto portId : allFabricPortIds) {
      if (portId != drainedPortId) {
        undrainedPortIds.push_back(portId);
      }
    }
    auto checkActiveInactiveState = [&]() {
      // Only drained port will be inactive
      utility::checkFabricPortsActiveState(
          getAgentEnsemble(), drainedPortIds, false /*expectActive*/);
      utility::checkFabricPortsActiveState(
          getAgentEnsemble(), undrainedPortIds, true /*expectActive*/);
    };
    checkActiveInactiveState();
    auto portsToFlap = drainedPortIds;
    portsToFlap.push_back(*undrainedPortIds.begin());
    // Flap ports. Active/inactive state should restore after
    bringDownPorts(portsToFlap);
    bringUpPorts(portsToFlap);
    checkActiveInactiveState();
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentFabricSwitchTest, fabricSwitchIsolate) {
  auto setup = [=, this]() {
    setSwitchDrainState(getSw()->getConfig(), cfg::SwitchDrainState::DRAINED);
  };

  auto verify = [=, this]() {
    EXPECT_GT(getProgrammedState()->getPorts()->numNodes(), 0);
    auto switchId = getCurrentSwitchIdForTesting();
    utility::checkFabricConnectivity(getAgentEnsemble(), switchId);
    // All ports should go to inactive state when switch is drained and
    // ports are in loopback!
    utility::checkFabricPortsActiveState(
        getAgentEnsemble(), fabricPortIdsForTesting(), false /*expectActive*/);
  };
  verifyAcrossWarmBoots(setup, verify);
}

class AgentFabricSwitchSelfLoopTest : public AgentFabricSwitchTest {
 public:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = AgentFabricSwitchTest::initialConfig(ensemble);
    // Start off as drained else all looped ports will get disabled
    config.switchSettings()->switchDrainState() =
        cfg::SwitchDrainState::DRAINED;
    return config;
  }

 protected:
  void verifyState(cfg::PortState desiredState, const PortMap& ports) const {
    std::vector<PortID> portIds;
    for (const auto& [portId, _] : ports) {
      portIds.emplace_back(portId);
    }
    verifyState(desiredState, portIds);
  }
  void verifyState(cfg::PortState desiredState, std::vector<PortID>& ports)
      const {
    WITH_RETRIES_N(180, {
      if (desiredState == cfg::PortState::DISABLED) {
        auto numPorts = ports.size();
        auto switchId = getCurrentSwitchIdForTesting();
        auto switchIndex =
            getSw()->getSwitchInfoTable().getSwitchIndexFromSwitchId(switchId);
        auto missingConnectivity = *getHwSwitchStats(switchIndex)
                                        .fabricReachabilityStats()
                                        ->missingCount();
        // When disabled all ports should lose connectivity info
        EXPECT_EVENTUALLY_EQ(missingConnectivity, numPorts);
      }
      std::vector<PortError> expectedErrors;
      if (desiredState == cfg::PortState::DISABLED) {
        expectedErrors.push_back(PortError::ERROR_DISABLE_LOOP_DETECTED);
      }
      for (const auto& portId : ports) {
        auto port = getProgrammedState()->getPorts()->getNode(portId);
        EXPECT_EVENTUALLY_EQ(port->getAdminState(), desiredState);
        EXPECT_EVENTUALLY_EQ(port->getActiveErrors(), expectedErrors);

        auto ledExternalState = port->getLedPortExternalState();
        EXPECT_EVENTUALLY_TRUE(ledExternalState.has_value());

        // If port is disabled, connectivity info would disappear
        // so cabling error should clear out.
        auto desiredLedState =
            (desiredState == cfg::PortState::DISABLED
                 ? PortLedExternalState::NONE
                 : PortLedExternalState::CABLING_ERROR_LOOP_DETECTED);
        EXPECT_EVENTUALLY_EQ(*ledExternalState, desiredLedState)
            << " LED State mismatch for port: " << port->getName();
      }
    });
  }

 protected:
  void checkDataCellFilter(bool expectFilterOn) {
    for (const auto& [_, portIds] : switch2FabricPortIds()) {
      checkDataCellFilter(expectFilterOn, portIds);
    }
  }
  void checkDataCellFilter(
      bool expectFilterOn,
      const std::vector<PortID>& portIds) {
    WITH_RETRIES({
      for (const auto& [_, portStats] : getNextUpdatedPortStats(portIds)) {
        EXPECT_EVENTUALLY_TRUE(portStats.dataCellsFilterOn().has_value());
        EXPECT_EVENTUALLY_EQ(*portStats.dataCellsFilterOn(), expectFilterOn);
      }
    });
  }

 private:
  void setCmdLineFlagOverrides() const override {
    AgentFabricSwitchTest::setCmdLineFlagOverrides();
    FLAGS_disable_looped_fabric_ports = true;
    /*
     * Ideally we would have liked to test with
     * FLAGS_detect_wrong_fabric_connections = true;
     * as well. However, with soc property that this flag
     * turns on cannot set internal loopback mode on
     * fabric ports.  Hence the need to keep this disabled
     */
  }
};

TEST_F(AgentFabricSwitchSelfLoopTest, selfLoopDetection) {
  auto setup = [this]() {
    auto ports = fabricPortIdsForTesting();
    // Since switch is drained, ports should stay enabled
    verifyState(cfg::PortState::ENABLED, ports);
    // Data filter should be turned off since we never enabled
    // wrong_fabric_connections
    checkDataCellFilter(false /*expectFilterOn*/, fabricPortIdsForTesting());
    // Undrain
    setSwitchDrainState(getSw()->getConfig(), cfg::SwitchDrainState::UNDRAINED);
  };
  auto verify = [this]() {
    auto ports = fabricPortIdsForTesting();
    // Ports should now get disabled
    verifyState(cfg::PortState::DISABLED, ports);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentFabricSwitchSelfLoopTest, portDrained) {
  auto drainedPortId = fabricPortIdsForTesting()[0];
  auto setup = [this, drainedPortId]() {
    // Drain port
    auto newCfg = getSw()->getConfig();
    for (auto& port : *newCfg.ports()) {
      if (PortID(*port.logicalID()) == drainedPortId) {
        port.drainState() = cfg::PortDrainState::DRAINED;
      }
    }
    applyNewConfig(newCfg);
    auto ports = fabricPortIdsForTesting();
    // Since switch is drained, ports should stay enabled
    verifyState(cfg::PortState::ENABLED, ports);
    // Data filter should be turned off since we never enabled
    // wrong_fabric_connections
    checkDataCellFilter(false /*expectFilterOn*/, fabricPortIdsForTesting());
    // Undrain
    setSwitchDrainState(newCfg, cfg::SwitchDrainState::UNDRAINED);
  };
  auto verify = [this, drainedPortId]() {
    auto ports = fabricPortIdsForTesting();
    // All but the drained ports should now get disabled
    std::erase(ports, drainedPortId);
    verifyState(cfg::PortState::DISABLED, ports);
    // Data filter should be turned off since we never enabled
    // wrong_fabric_connections
    checkDataCellFilter(false /*expectFilterOn*/, fabricPortIdsForTesting());
    // Verify that global drops are zero
    auto switchId = getCurrentSwitchIdForTesting();
    auto switchIndex =
        getSw()->getSwitchInfoTable().getSwitchIndexFromSwitchId(switchId);
    auto hwSwitchStats = getHwSwitchStats(switchIndex);
    const auto& dropStats = *hwSwitchStats.switchDropStats();
    EXPECT_EQ(*dropStats.globalDrops(), 0);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentFabricSwitchTest, reachDiscard) {
  auto verify = [this]() {
    auto switchId = getCurrentSwitchIdForTesting();
    auto switchIndex =
        getSw()->getSwitchInfoTable().getSwitchIndexFromSwitchId(switchId);
    auto beforeSwitchDrops = *getHwSwitchStats(switchIndex).switchDropStats();
    // Two packets needed starting from SDK 14.x
    std::string out;
    getAgentEnsemble()->runDiagCommand(
        "TX 1 destination=-1 destinationModid=-1 flags=0x8000\n",
        out,
        switchId);
    getAgentEnsemble()->runDiagCommand(
        "TX 1 destination=-1 destinationModid=-1 flags=0x8000\n",
        out,
        switchId);
    WITH_RETRIES({
      auto afterSwitchDrops = *getHwSwitchStats(switchIndex).switchDropStats();
      XLOG(INFO) << " Before reach drops: "
                 << *beforeSwitchDrops.globalReachabilityDrops()
                 << " After reach drops: "
                 << *afterSwitchDrops.globalReachabilityDrops()
                 << " Before global drops: " << *beforeSwitchDrops.globalDrops()
                 << " After global drops: : "
                 << *afterSwitchDrops.globalDrops();
      EXPECT_EVENTUALLY_GT(
          *afterSwitchDrops.globalReachabilityDrops(),
          *beforeSwitchDrops.globalReachabilityDrops());
      // Global drops are in bytes
      EXPECT_EVENTUALLY_GT(
          *afterSwitchDrops.globalDrops(), *beforeSwitchDrops.globalDrops());
    });
    checkStatsStabilize();
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentFabricSwitchTest, dtlQueueWatermarks) {
  auto verify = [this]() {
    auto switchId = getCurrentSwitchIdForTesting();
    auto switchIndex =
        getSw()->getSwitchInfoTable().getSwitchIndexFromSwitchId(switchId);
    std::string out;
    utility::checkFabricPortsActiveState(
        getAgentEnsemble(), fabricPortIdsForTesting(), true /*expectActive*/);
    WITH_RETRIES({
      auto beforeWatermarks = getAllSwitchWatermarkStats()[switchIndex];
      ASSERT_EVENTUALLY_TRUE(
          beforeWatermarks.dtlQueueWatermarkBytes().has_value());
      EXPECT_EVENTUALLY_EQ(*beforeWatermarks.dtlQueueWatermarkBytes(), 0);
    });
    WITH_RETRIES({
      getAgentEnsemble()->runDiagCommand(
          "modify RTP_RMHMT 5 1 LINK_BIT_MAP=1\ntx 1000 DeSTination=13 DeSTinationModid=5 flags=0x8000\n",
          out,
          switchId);
      auto afterWatermarks = getAllSwitchWatermarkStats()[switchIndex];
      ASSERT_EVENTUALLY_TRUE(
          afterWatermarks.dtlQueueWatermarkBytes().has_value());
      EXPECT_EVENTUALLY_GT(*afterWatermarks.dtlQueueWatermarkBytes(), 0);
      XLOG(INFO) << "SwitchId: " << switchId << " After DTL queue watermarks: "
                 << *afterWatermarks.dtlQueueWatermarkBytes();
    });
  };

  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentFabricSwitchTest, switchReachability) {
  auto verify = [this]() {
    auto switchId = getCurrentSwitchIdForTesting();
    utility::checkFabricPortsActiveState(
        getAgentEnsemble(), fabricPortIdsForTesting(), true /*expectActive*/);
    bool switchReachabilityWorking = false;
    WITH_RETRIES({
      auto switchReachability = getSw()->getSwitchReachability();
      ASSERT_EVENTUALLY_TRUE(switchReachability.contains(switchId));
      auto switchIter = switchReachability.find(switchId);
      for (auto& [destinationSwitchId, portGroupId] :
           *switchIter->second.switchIdToFabricPortGroupMap()) {
        auto portGroupIter =
            switchIter->second.fabricPortGroupMap()->find(portGroupId);
        if (portGroupIter != switchIter->second.fabricPortGroupMap()->end()) {
          XLOG(DBG0) << "On local switch id " << switchId
                     << ", destination switch id " << destinationSwitchId
                     << " reachability available over port group ID "
                     << portGroupId << ", with group size "
                     << portGroupIter->second.size();
          // Fabric device does not have physical rechability to
          // any other device, so no ports expected in port group.
          EXPECT_EQ(portGroupIter->second.size(), 0);
          switchReachabilityWorking = true;
        }
      }
      EXPECT_EVENTUALLY_TRUE(switchReachabilityWorking);
    });
  };

  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentFabricSwitchTest, ValidateFecErrorDetect) {
  auto verify = [this]() {
    utility::setupFecErrorDetectEnable(
        getAgentEnsemble(), true /*fecErrorDetectEnable*/);
    for (auto portId : fabricPortIdsForTesting()) {
      auto port = getProgrammedState()->getPorts()->getNodeIf(portId);
      EXPECT_EQ(*port->getFecErrorDetectEnable(), true);
    }
    utility::setupFecErrorDetectEnable(
        getAgentEnsemble(), false /*fecErrorDetectEnable*/);
    for (auto portId : fabricPortIdsForTesting()) {
      auto port = getProgrammedState()->getPorts()->getNodeIf(portId);
      EXPECT_EQ(*port->getFecErrorDetectEnable(), false);
    }
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

class AgentBalancedInputModeTest : public AgentFabricSwitchTest {
 public:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = AgentFabricSwitchTest::initialConfig(ensemble);
    // Initialize local switch as level 2 (SDSW)
    const auto selfFabricLevel = 2;
    const auto remoteFabricLevel = 1;
    for (auto& [_, dsfNode] : *config.dsfNodes()) {
      dsfNode.fabricLevel() = selfFabricLevel;
    }
    std::vector<int> fabricSwitchIds;
    // Add fabric nodes to DsfConfig with different clusterIDs
    for (int i = 0; i < kNumRemoteFabricNodes; i++) {
      auto [fabricSwitchId, fabricNode] = utility::getRemoteFabricNodeCfg(
          *config.dsfNodes(),
          remoteFabricLevel,
          i /* clusterId */,
          checkSameAndGetAsicForTesting(
              ensemble.getHwAsicTable()->getFabricAsics())
              ->getAsicType(),
          ensemble.getSw()->getPlatformType());
      config.dsfNodes()->insert({fabricSwitchId, fabricNode});
      fabricSwitchIds.push_back(fabricSwitchId);
    }
    // Populate expected neighbor reachability to all remote fabric nodes
    utility::populatePortExpectedNeighborsToRemote(
        ensemble.masterLogicalFabricPortIds(),
        config,
        fabricSwitchIds,
        kNumParallelLinks);
    return config;
  }

  /*
   * Override init info for HwAsicTable creation with fabric level 2.
   * This ensures that HwAsic is created with DUAL_STAGE_L2 fabric node role
   * instead of the default SINGLE_STAGE_L1.
   */
  void overrideTestEnsembleInitInfo(
      TestEnsembleInitInfo& initInfo) const override {
    // Read dsfNodes from input config and set fabric level to 2 for all nodes
    // so that HwAsicTable derives the correct FabricNodeRole (DUAL_STAGE_L2).
    auto inputAgentConfig = AgentConfig::fromFile(FLAGS_config)->thrift;
    auto dsfNodes = *inputAgentConfig.sw()->dsfNodes();
    CHECK(!dsfNodes.empty()) << "dsfNodes must not be empty";
    for (auto& [_, dsfNode] : dsfNodes) {
      dsfNode.fabricLevel() = 2; // Set to L2 (SDSW)
    }
    initInfo.overrideDsfNodes = std::move(dsfNodes);
  }

 private:
  void setCmdLineFlagOverrides() const override {
    AgentFabricSwitchTest::setCmdLineFlagOverrides();
    FLAGS_disable_looped_fabric_ports = false;
    FLAGS_detect_wrong_fabric_connections = false;
    FLAGS_enable_balanced_input_mode = true;
  }
};

TEST_F(AgentBalancedInputModeTest, init) {
  auto verify = [this]() {
    auto fabricPortIds = masterLogicalFabricPortIds();
    auto fabricPortIter = fabricPortIds.begin();
    std::optional<int> currReachabilityGroupId;

    for (int i = 0; i < kNumRemoteFabricNodes; i++) {
      currReachabilityGroupId = std::nullopt;

      for (int j = 0; j < kNumParallelLinks - 1; j++) {
        CHECK(fabricPortIter != fabricPortIds.end());
        auto port = getProgrammedState()->getPorts()->getNode(*fabricPortIter);
        EXPECT_TRUE(port->getReachabilityGroupId().has_value());

        // Check ports to the same remote neighbor has same reachabilityGroupId
        if (!currReachabilityGroupId.has_value()) {
          currReachabilityGroupId = port->getReachabilityGroupId();
        }
        EXPECT_EQ(currReachabilityGroupId, port->getReachabilityGroupId());
      }
    }
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentFabricSwitchTest, verifyRtpGpdAlwaysDisabled) {
  auto verify = [&]() {
    setSwitchDrainState(getSw()->getConfig(), cfg::SwitchDrainState::DRAINED);
    setSwitchDrainState(getSw()->getConfig(), cfg::SwitchDrainState::UNDRAINED);
    // Once drain/undrain is done, make sure that RTP GPD is set to 0
    auto expectedValues = {
        "RTP_GRACEFUL_POWER_DOWN_CONFIGURATION.RTP0[0x135]=0:",
        "RTP_GRACEFUL_POWER_DOWN_CONFIGURATION.RTP1[0x135]=0:"};
    auto switchId = getCurrentSwitchIdForTesting();
    std::string out;
    WITH_RETRIES({
      getAgentEnsemble()->runDiagCommand(
          "g RTP_GRACEFUL_POWER_DOWN_CONFIGURATION\n", out, switchId);
      XLOG(DBG0) << "SwitchId: " << static_cast<int>(switchId)
                 << ", output: " << out;
      for (auto& entry : expectedValues) {
        EXPECT_EVENTUALLY_TRUE(out.find(entry) != std::string::npos);
      }
    });
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentFabricSwitchTest, verifySourceRoutedCellHandling) {
  auto verify = [&]() {
    auto fabricSwitchId = getCurrentSwitchIdForTesting();
    auto fabricPorts = fabricPortIdsForTesting();
    std::string sourceRoutedCellInjectCintStr = R"(
      cint_reset();
      int i;
      uint32 flags = 0;
      uint32 MAX_SEND_WORDS = 16;
      uint32 data_set[MAX_SEND_WORDS];

      bcm_fabric_route_t route;
      bcm_fabric_route_t_init(&route);

      int link_id[2];
      )" +
        folly::to<std::string>("link_id[0] =",
                               static_cast<int>(fabricPorts[0]),
                               ";\nlink_id[1] =",
                               static_cast<int>(fabricPorts[1]),
                               ";") +
        R"(
      route.number_of_hops = 2;
      route.hop_ids = link_id;
      for (i = 0; i < MAX_SEND_WORDS; i++) {
        data_set[i] = -1;
      }
      bcm_fabric_route_tx(0, flags, route, MAX_SEND_WORDS, data_set);
    )";
    folly::test::TemporaryFile file;
    folly::writeFull(
        file.fd(),
        sourceRoutedCellInjectCintStr.c_str(),
        sourceRoutedCellInjectCintStr.size());
    auto cmd = fmt::format("cint {}\n", file.path().c_str());
    std::string out;
    getAgentEnsemble()->runDiagCommand(cmd, out, fabricSwitchId);
    WITH_RETRIES({
      auto stats = getNextUpdatedPortStats({fabricPorts[0], fabricPorts[1]});
      uint64_t outBytesFab0 = *stats[fabricPorts[0]].outBytes_();
      uint64_t outBytesFab1 = *stats[fabricPorts[1]].outBytes_();
      // We expect the source routed cell to be dropped at fabricPorts[0]
      EXPECT_EVENTUALLY_LT(outBytesFab0, 200);
      EXPECT_EVENTUALLY_EQ(outBytesFab1, 0);
      XLOG(DBG2) << "Out bytes for fabric port ID(" << fabricPorts[0]
                 << "): " << outBytesFab0 << ", fabric port ID("
                 << fabricPorts[1] << "): " << outBytesFab1;
      for (auto portId : fabricPorts) {
        auto portName =
            getProgrammedState()->getPorts()->getNodeIf(portId)->getName();
        auto rciWatermarkStr = fmt::format(
            "buffer_watermark_ucast.{}.queue0.fabric_q0.p100.60", portName);
        auto counters = getAgentEnsemble()->getFb303CountersByRegex(
            portId, rciWatermarkStr);
        ASSERT_EVENTUALLY_EQ(counters.size(), 1);
        for (const auto& ctr : counters) {
          // No RCI asserted on any port
          XLOG(DBG4) << ctr.first << " : " << ctr.second;
          EXPECT_EQ(ctr.second, 0);
        }
      }
    });
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentFabricSwitchTest, verifyFabricInterCellJitterWatermark) {
  auto verify = [&]() {
    auto switchId = getCurrentSwitchIdForTesting();
    auto switchIndex =
        getSw()->getSwitchInfoTable().getSwitchIndexFromSwitchId(switchId);
    std::string out;
    WITH_RETRIES({
      // Mark module ID 1 as reachable over some fabric ports
      getAgentEnsemble()->runDiagCommand(
          "mod RTP_RMHMT 1 1 LINK_BIT_MAP=1\n", out, switchId);
      // Send packets to module ID 1, such that length is > 512B.
      // These will be split into multiple cells and sent over the
      // fabric links and will be reassembled back into packets,
      // resulting in some value in inter cell jitter watermark.
      getAgentEnsemble()->runDiagCommand(
          "TX 100 DeSTination=0 DeSTinationModid=1 LENgth=1400\n",
          out,
          switchId);
      auto watermarkStats = getAllSwitchWatermarkStats();
      auto switchWatermarksIter = watermarkStats.find(switchIndex);
      ASSERT_EVENTUALLY_TRUE(switchWatermarksIter != watermarkStats.end());
      auto jitterWatermark =
          switchWatermarksIter->second.fabricInterCellJitterWatermarkUsec();
      ASSERT_EVENTUALLY_TRUE(jitterWatermark.has_value());
      EXPECT_EVENTUALLY_GT(jitterWatermark.value(), 0);
      XLOG(DBG0) << "Fabric Inter Cell Jitter Watermark usec : "
                 << jitterWatermark.value();
    });
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

} // namespace facebook::fboss
