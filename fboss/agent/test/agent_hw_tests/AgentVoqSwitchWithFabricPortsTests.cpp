// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.
//
#include "fboss/agent/test/agent_hw_tests/AgentVoqSwitchTests.h"

#include "fboss/agent/FabricConnectivityManager.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/test/utils/DsfConfigUtils.h"
#include "fboss/agent/test/utils/FabricTestUtils.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"

namespace facebook::fboss {

class AgentVoqSwitchWithFabricPortsTest : public AgentVoqSwitchTest {
 public:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    // Increase the query timeout to be 5sec
    FLAGS_hwswitch_query_timeout = 5000;
    auto config = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true, /*interfaceHasSubnet*/
        true, /*setInterfaceMac*/
        utility::kBaseVlanId,
        true /*enable fabric ports*/);
    utility::populatePortExpectedNeighborsToSelf(
        ensemble.masterLogicalPortIds(), config);
    return config;
  }

  std::optional<size_t> maxRequiredInterfacePorts() const override {
    // VoQ + fabric tests need every fabric and interface port.
    return std::nullopt;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::VOQ_SWITCH_WITH_FABRIC_PORTS};
  }

 protected:
  void assertPortAndDrainState(cfg::SwitchDrainState expectDrainState) const {
    bool expectDrained =
        expectDrainState == cfg::SwitchDrainState::DRAINED ? true : false;
    for (const auto& switchId : getSw()->getHwAsicTable()->getSwitchIDs()) {
      // reachability should always be there regardless of drain state
      utility::checkFabricConnectivity(getAgentEnsemble(), switchId);
      HwSwitchMatcher matcher(std::unordered_set<SwitchID>({switchId}));
      const auto& switchSettings =
          getProgrammedState()->getSwitchSettings()->getSwitchSettings(matcher);
      EXPECT_EQ(switchSettings->isSwitchDrained(), expectDrained);
    }
    // Drained - expect inactive
    // Undrained - expect active
    utility::checkFabricPortsActiveState(
        getAgentEnsemble(), masterLogicalFabricPortIds(), !expectDrained);
  }
  void verifyLocalForwarding() {
    // Setup neighbor entry
    utility::EcmpSetupAnyNPorts6 ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    const auto testPort = ecmpHelper.ecmpPortDescriptorAt(0);
    addRemoveNeighbor(testPort, NeighborOp::ADD);
    auto sendPktAndVerify = [&](bool isFrontPanel) {
      auto beforeOutPkts = folly::copy(
          getLatestPortStats(testPort.phyPortID()).outUnicastPkts_().value());
      std::optional<PortID> frontPanelPort;
      if (isFrontPanel) {
        frontPanelPort = ecmpHelper.ecmpPortDescriptorAt(1).phyPortID();
      }
      sendPacket(ecmpHelper.ip(testPort), frontPanelPort);
      WITH_RETRIES({
        auto afterOutPkts =
            getLatestPortStats(testPort.phyPortID()).outUnicastPkts_().value();
        XLOG(DBG2) << " Before out pkts: " << beforeOutPkts
                   << " After out pkts: " << afterOutPkts;
        EXPECT_EVENTUALLY_EQ(afterOutPkts, beforeOutPkts + 1);
      });
    };
    sendPktAndVerify(false /*isFrontPanel*/);
    sendPktAndVerify(true /*isFrontPanel*/);
  }

 private:
  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_hide_fabric_ports = false;
    // Allow disabling of looped ports. This should
    // be a noop for VOQ switches
    FLAGS_disable_looped_fabric_ports = true;
    // Fabric connectivity manager to expect single NPU
    if (!FLAGS_multi_switch) {
      FLAGS_janga_single_npu_for_testing = true;
    }
  }
};

class AgentVoqSwitchWithFabricPortsStartDrained
    : public AgentVoqSwitchWithFabricPortsTest {
 public:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = AgentVoqSwitchWithFabricPortsTest::initialConfig(ensemble);
    // Set threshold higher than existing fabric ports
    config.switchSettings()->minLinksToJoinVOQDomain() =
        ensemble.masterLogicalFabricPortIds().size() + 10;
    config.switchSettings()->minLinksToRemainInVOQDomain() =
        ensemble.masterLogicalFabricPortIds().size() + 5;
    return config;
  }
};

TEST_F(AgentVoqSwitchWithFabricPortsTest, collectStats) {
  auto verify = [this]() {
    EXPECT_GT(getProgrammedState()->getPorts()->numNodes(), 0);
    assertPortAndDrainState(cfg::SwitchDrainState::UNDRAINED);
    getSw()->updateStats();
    WITH_RETRIES({
      auto port2Stats = getSw()->getHwPortStats(masterLogicalFabricPortIds());
      for (auto portId : masterLogicalFabricPortIds()) {
        auto pitr = port2Stats.find(portId);
        ASSERT_EVENTUALLY_TRUE(pitr != port2Stats.end());
        EXPECT_EVENTUALLY_TRUE(pitr->second.cableLengthMeters().has_value());
      }
      auto state = getProgrammedState();
      for (auto& portMap : std::as_const(*state->getPorts())) {
        for (auto& [_, port] : std::as_const(*portMap.second)) {
          if (scopeResolver().scope(port).switchId() !=
              getCurrentSwitchIdForTesting()) {
            continue;
          }
          auto loadBearingInErrors = fb303::fbData->getCounterIfExists(
              port->getName() + ".load_bearing_in_errors.sum.60");
          auto loadBearingFecErrors = fb303::fbData->getCounterIfExists(
              port->getName() +
              ".load_bearing_fec_uncorrectable_errors.sum.60");
          auto loadBearingFlaps = fb303::fbData->getCounterIfExists(
              port->getName() + ".load_bearing_link_state.flap.sum.60");
          if (port->getPortType() == cfg::PortType::FABRIC_PORT) {
            EXPECT_EVENTUALLY_TRUE(loadBearingInErrors.has_value());
            EXPECT_EVENTUALLY_TRUE(loadBearingFecErrors.has_value());
            if (getAgentEnsemble()->getBootType() == BootType::COLD_BOOT) {
              EXPECT_EVENTUALLY_TRUE(loadBearingFlaps.has_value());
            } else {
              // No port flap after wb, hence there no stats being recorded
              EXPECT_FALSE(loadBearingFlaps.has_value());
            }
          } else {
            EXPECT_FALSE(loadBearingInErrors.has_value());
            EXPECT_FALSE(loadBearingFecErrors.has_value());
            EXPECT_FALSE(loadBearingFlaps.has_value());
          }
        }
      }
    });
  };
  verifyAcrossWarmBoots(verify);
}

TEST_F(AgentVoqSwitchWithFabricPortsTest, checkFabricConnectivity) {
  auto verify = [this]() {
    for (const auto& switchId : getSw()->getHwAsicTable()->getSwitchIDs()) {
      utility::checkFabricConnectivity(getAgentEnsemble(), switchId);
    }
  };
  verifyAcrossWarmBoots(verify);
}

TEST_F(AgentVoqSwitchWithFabricPortsTest, switchReachability) {
  auto verify = [=, this]() {
    EXPECT_GT(getProgrammedState()->getPorts()->numNodes(), 0);
    auto fabricPortId =
        PortID(masterLogicalPortIds({cfg::PortType::FABRIC_PORT})[0]);
    auto drainPort = [&](bool drain, PortID fabPortId) {
      applyNewState([&](const std::shared_ptr<SwitchState>& in) {
        auto out = in->clone();
        auto port = out->getPorts()->getNodeIf(fabPortId);
        auto newPort = port->modify(&out);
        newPort->setPortDrainState(
            drain ? cfg::PortDrainState::DRAINED
                  : cfg::PortDrainState::UNDRAINED);
        return out;
      });
    };
    auto switchReachableOverPort = [&](bool reachable,
                                       PortID portId,
                                       int expectedGroupSize) {
      auto switchId = getCurrentSwitchIdForTesting();
      WITH_RETRIES({
        const auto& reachability = getSw()->getSwitchReachability();
        const auto switchIter = reachability.find(switchId);
        ASSERT_EVENTUALLY_TRUE(switchIter != reachability.end());
        auto switchReachability = switchIter->second;
        const auto switchToPortGroupIter =
            switchReachability.switchIdToFabricPortGroupMap()->find(switchId);
        EXPECT_EVENTUALLY_TRUE(
            switchToPortGroupIter !=
            switchReachability.switchIdToFabricPortGroupMap()->end());

        const auto portGroupIter =
            switchReachability.fabricPortGroupMap()->find(
                switchToPortGroupIter->second);
        EXPECT_EVENTUALLY_TRUE(
            portGroupIter != switchIter->second.fabricPortGroupMap()->end());
        EXPECT_EVENTUALLY_EQ(portGroupIter->second.size(), expectedGroupSize);
        // If the size matches, then check for port membership
        auto portNameIter = std::find(
            portGroupIter->second.begin(),
            portGroupIter->second.end(),
            getProgrammedState()->getPorts()->getNodeIf(portId)->getName());
        EXPECT_EVENTUALLY_EQ(
            (portNameIter != portGroupIter->second.end()), reachable);
      });
    };
    drainPort(true /*drain*/, fabricPortId);
    switchReachableOverPort(
        false /*reachable*/,
        fabricPortId,
        masterLogicalFabricPortIds().size() - 1);
    drainPort(false /*drain*/, fabricPortId);
    switchReachableOverPort(
        true /*reachable*/, fabricPortId, masterLogicalFabricPortIds().size());
  };
  verifyAcrossWarmBoots(verify);
}

TEST_F(AgentVoqSwitchWithFabricPortsTest, fabricIsolate) {
  auto verify = [=, this]() {
    EXPECT_GT(getProgrammedState()->getPorts()->numNodes(), 0);
    auto fabricPortId =
        PortID(masterLogicalPortIds({cfg::PortType::FABRIC_PORT})[0]);
    auto switchId = getCurrentSwitchIdForTesting();
    utility::checkPortFabricReachability(
        getAgentEnsemble(), switchId, fabricPortId);
    auto drainPort = [&](bool drain) {
      applyNewState([&](const std::shared_ptr<SwitchState>& in) {
        auto out = in->clone();
        auto port = out->getPorts()->getNodeIf(fabricPortId);
        auto newPort = port->modify(&out);
        newPort->setPortDrainState(
            drain ? cfg::PortDrainState::DRAINED
                  : cfg::PortDrainState::UNDRAINED);
        return out;
      });
      // Drained port == inactive, undrained port == active
      auto expectActive = !drain;
      std::vector<PortID> fabricPortIds({static_cast<PortID>(fabricPortId)});
      utility::checkFabricPortsActiveState(
          getAgentEnsemble(), fabricPortIds, expectActive);
      // Fabric reachability should be unchanged regardless of drain
      utility::checkPortFabricReachability(
          getAgentEnsemble(), switchId, fabricPortId);
      // Flap port, active and reachability status should be unaffected
      // after flap
      bringDownPort(fabricPortId);
      bringUpPort(fabricPortId);
      utility::checkFabricPortsActiveState(
          getAgentEnsemble(), fabricPortIds, expectActive);
      utility::checkPortFabricReachability(
          getAgentEnsemble(), switchId, fabricPortId);
    };
    drainPort(true);
    drainPort(false);
  };
  verifyAcrossWarmBoots(verify);
}

TEST_F(AgentVoqSwitchWithFabricPortsTest, fabricConnectivityMismatch) {
  auto verify = [this]() {
    auto fabricPortId = masterLogicalFabricPortIds()[0];
    auto cfg = initialConfig(*getAgentEnsemble());
    cfg::PortNeighbor nbr;
    nbr.remoteSystem() = "RemoteA";
    nbr.remotePort() = "portA";
    auto portCfg = utility::findCfgPort(cfg, fabricPortId);
    portCfg->expectedNeighborReachability() = {nbr};
    applyNewConfig(cfg);

    WITH_RETRIES({
      auto port = getProgrammedState()->getPorts()->getNodeIf(fabricPortId);
      EXPECT_EVENTUALLY_TRUE(port->getLedPortExternalState().has_value());
      EXPECT_EVENTUALLY_EQ(
          port->getLedPortExternalState().value(),
          PortLedExternalState::CABLING_ERROR_LOOP_DETECTED);
    });
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentVoqSwitchWithFabricPortsTest, switchIsolate) {
  auto setup = [=, this]() {
    // Before drain all fabric ports should be active
    utility::checkFabricPortsActiveState(
        getAgentEnsemble(),
        masterLogicalFabricPortIds(),
        true /*expectActive*/);
    // Local forwarding works
    verifyLocalForwarding();
    auto newCfg = initialConfig(*getAgentEnsemble());
    *newCfg.switchSettings()->switchDrainState() =
        cfg::SwitchDrainState::DRAINED;
    applyNewConfig(newCfg);
  };

  auto verify = [this]() {
    assertPortAndDrainState(cfg::SwitchDrainState::DRAINED);
    // Local forwarding should continue to work even after drain
    verifyLocalForwarding();
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentVoqSwitchWithFabricPortsTest, minVoqThresholdDrainUndrain) {
  auto verify = [this]() {
    assertPortAndDrainState(cfg::SwitchDrainState::UNDRAINED);
    verifyLocalForwarding();
    auto updateMinLinksThreshold =
        [this](int minLinksToJoin, int minLinksToRemain) {
          auto newCfg = initialConfig(*getAgentEnsemble());
          // Set threshold higher than existing fabric ports
          newCfg.switchSettings()->minLinksToJoinVOQDomain() = minLinksToJoin;
          newCfg.switchSettings()->minLinksToRemainInVOQDomain() =
              minLinksToRemain;
          applyNewConfig(newCfg);
        };
    // Bump up threshold beyond existing fabric links, switch should drain
    updateMinLinksThreshold(
        masterLogicalFabricPortIds().size() + 10,
        masterLogicalFabricPortIds().size() + 5);
    assertPortAndDrainState(cfg::SwitchDrainState::DRAINED);
    // Verify local forwarding works post drain due to min links
    verifyLocalForwarding();
    // Bump up threshold beyond existing fabric links, switch should drain
    updateMinLinksThreshold(0, 0);
    assertPortAndDrainState(cfg::SwitchDrainState::UNDRAINED);
    // Verify local forwarding works post undrain once min links are satisfied
    verifyLocalForwarding();
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentVoqSwitchWithFabricPortsTest, verifyNifMulticastTrafficDropped) {
  constexpr static auto kNumPacketsToSend{1000};
  auto setup = []() {};

  auto verify = [=, this]() {
    auto beforePkts = folly::copy(
        getLatestPortStats(masterLogicalInterfaceOrHyperPortIds()[0])
            .outUnicastPkts_()
            .value());
    sendLocalServiceDiscoveryMulticastPacket(
        masterLogicalInterfaceOrHyperPortIds()[0], kNumPacketsToSend);
    WITH_RETRIES({
      auto afterPkts =
          getLatestPortStats(masterLogicalInterfaceOrHyperPortIds()[0])
              .outUnicastPkts_()
              .value();
      XLOG(DBG2) << "Before pkts: " << beforePkts
                 << " After pkts: " << afterPkts;
      EXPECT_EVENTUALLY_GE(afterPkts, beforePkts + kNumPacketsToSend);
    });

    // Wait for some time and make sure that fabric stats dont increment.
    sleep(5);
    auto fabricPortStats = getLatestPortStats(masterLogicalFabricPortIds());
    auto fabricBytes = 0;
    for (const auto& idAndStats : fabricPortStats) {
      fabricBytes += folly::copy(idAndStats.second.outBytes_().value());
    }
    // Even though NIF will see RX/TX bytes, fabric will always be zero
    // as these packets are expected to be dropped without being sent
    // out on fabric.
    EXPECT_EQ(fabricBytes, 0);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentVoqSwitchWithFabricPortsTest, overdrainPct) {
  auto setup = []() {};
  auto verify = [this]() {
    auto switchIndex = getCurrentSwitchIndexForTesting();
    auto counterName =
        folly::to<std::string>("switch.", switchIndex, ".fabric_overdrain_pct");
    WITH_RETRIES(
        { EXPECT_EVENTUALLY_EQ(0, fb303::fbData->getCounter(counterName)); });
    auto enableFabPorts = [this](bool enable) {
      auto cfg = initialConfig(*getAgentEnsemble());
      for (auto& port : *cfg.ports()) {
        if (*port.portType() == cfg::PortType::FABRIC_PORT) {
          port.state() =
              enable ? cfg::PortState::ENABLED : cfg::PortState::DISABLED;
        }
      }
      applyNewConfig(cfg);
    };
    // Disable all fabric port
    enableFabPorts(false);
    WITH_RETRIES(
        { EXPECT_EVENTUALLY_EQ(100, fb303::fbData->getCounter(counterName)); });
    // Enable all fabric port
    enableFabPorts(true);
    WITH_RETRIES(
        { EXPECT_EVENTUALLY_EQ(0, fb303::fbData->getCounter(counterName)); });
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(
    AgentVoqSwitchWithFabricPortsStartDrained,
    assertLocalForwardingAndCableLen) {
  auto verify = [this]() {
    assertPortAndDrainState(cfg::SwitchDrainState::DRAINED);
    // Local forwarding should work even when we come up drained
    verifyLocalForwarding();
    getSw()->updateStats();
    WITH_RETRIES({
      auto port2Stats = getSw()->getHwPortStats(masterLogicalFabricPortIds());
      for (auto portId : masterLogicalFabricPortIds()) {
        auto pitr = port2Stats.find(portId);
        EXPECT_EVENTUALLY_TRUE(pitr != port2Stats.end());
        EXPECT_EVENTUALLY_TRUE(pitr->second.cableLengthMeters().has_value());
      }
    });
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentVoqSwitchWithFabricPortsTest, checkFabricPortSprayWithIsolate) {
  utility::EcmpSetupAnyNPorts6 ecmpHelper(
      getProgrammedState(), getSw()->needL2EntryForNeighbor());
  const auto testPort = ecmpHelper.ecmpPortDescriptorAt(0);
  auto setup = [this, testPort, ecmpHelper]() {
    setForceTrafficOverFabric(true);
    addRemoveNeighbor(testPort, NeighborOp::ADD);
  };

  auto verify = [this, testPort, ecmpHelper]() {
    auto beforePkts = folly::copy(
        getLatestPortStats(testPort.phyPortID()).outUnicastPkts_().value());

    // Drain a fabric port
    auto fabricPortId =
        PortID(masterLogicalPortIds({cfg::PortType::FABRIC_PORT})[0]);
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto out = in->clone();
      auto port = out->getPorts()->getNodeIf(fabricPortId);
      auto newPort = port->modify(&out);
      newPort->setPortDrainState(cfg::PortDrainState::DRAINED);
      return out;
    });

    // Send 10K packets and spray on fabric ports
    for (auto i = 0; i < 10000; ++i) {
      sendPacket(
          ecmpHelper.ip(testPort),
          ecmpHelper.ecmpPortDescriptorAt(1).phyPortID());
    }
    WITH_RETRIES({
      auto afterPkts =
          getLatestPortStats(testPort.phyPortID()).outUnicastPkts_().value();
      XLOG(DBG2) << "Before pkts: " << beforePkts
                 << " After pkts: " << afterPkts;
      EXPECT_EVENTUALLY_GE(afterPkts, beforePkts + 10000);
      auto nifBytes =
          getLatestPortStats(testPort.phyPortID()).outBytes_().value();
      auto switchId =
          getSw()->getScopeResolver()->scope(testPort.phyPortID()).switchId();
      auto fabricPortStats =
          getLatestPortStats(masterLogicalFabricPortIds(switchId));
      auto fabricBytes = 0;
      for (const auto& idAndStats : fabricPortStats) {
        fabricBytes += idAndStats.second.outBytes_().value();
        if (idAndStats.first != fabricPortId) {
          EXPECT_EVENTUALLY_GT(idAndStats.second.outBytes_().value(), 0);
          EXPECT_EVENTUALLY_GT(idAndStats.second.inBytes_().value(), 0);
        }
      }
      XLOG(DBG2) << "NIF bytes: " << nifBytes
                 << " Fabric bytes: " << fabricBytes;
      EXPECT_EVENTUALLY_GE(fabricBytes, nifBytes);
      // Confirm load balance fails as the drained fabric port
      // should see close to 0 packets. We may see some control packtes.
      EXPECT_EVENTUALLY_FALSE(utility::isLoadBalanced(fabricPortStats, 15));

      // Confirm traffic is load balanced across all UNDRAINED fabric ports
      fabricPortStats.erase(fabricPortId);
      EXPECT_EVENTUALLY_TRUE(utility::isLoadBalanced(fabricPortStats, 15));
    });
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentVoqSwitchWithFabricPortsTest, checkFabricPortSpray) {
  utility::EcmpSetupAnyNPorts6 ecmpHelper(
      getProgrammedState(), getSw()->needL2EntryForNeighbor());
  const auto testPort = ecmpHelper.ecmpPortDescriptorAt(0);
  auto setup = [this, testPort, ecmpHelper]() {
    setForceTrafficOverFabric(true);
    addRemoveNeighbor(testPort, NeighborOp::ADD);
  };

  auto verify = [this, testPort, ecmpHelper]() {
    auto beforePkts = folly::copy(
        getLatestPortStats(testPort.phyPortID()).outUnicastPkts_().value());
    for (auto i = 0; i < 10000; ++i) {
      sendPacket(
          ecmpHelper.ip(testPort),
          ecmpHelper.ecmpPortDescriptorAt(1).phyPortID());
    }
    WITH_RETRIES({
      auto afterPkts =
          getLatestPortStats(testPort.phyPortID()).outUnicastPkts_().value();
      XLOG(DBG2) << "Before pkts: " << beforePkts
                 << " After pkts: " << afterPkts;
      EXPECT_EVENTUALLY_GE(afterPkts, beforePkts + 10000);
      auto nifBytes =
          getLatestPortStats(testPort.phyPortID()).outBytes_().value();
      auto switchId =
          getSw()->getScopeResolver()->scope(testPort.phyPortID()).switchId();
      auto fabricPortStats =
          getLatestPortStats(masterLogicalFabricPortIds(switchId));
      auto fabricBytes = 0;
      for (const auto& idAndStats : fabricPortStats) {
        fabricBytes += idAndStats.second.outBytes_().value();
      }
      XLOG(DBG2) << "NIF bytes: " << nifBytes
                 << " Fabric bytes: " << fabricBytes;
      EXPECT_EVENTUALLY_GE(fabricBytes, nifBytes);
      EXPECT_EVENTUALLY_TRUE(utility::isLoadBalanced(fabricPortStats, 15));
    });
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentVoqSwitchWithFabricPortsTest, fdrCellDrops) {
  utility::EcmpSetupAnyNPorts6 ecmpHelper(
      getProgrammedState(), getSw()->needL2EntryForNeighbor());
  const auto testPort = ecmpHelper.ecmpPortDescriptorAt(0);
  auto setup = [this, testPort]() {
    addRemoveNeighbor(testPort, NeighborOp::ADD);
    setForceTrafficOverFabric(true);
    std::string out;
    for (const auto& switchId : getSw()->getHwAsicTable()->getSwitchIDs()) {
      // Start with a blank command first and then set the FDA drop threshold
      // This is because, sometimes first diag command doesn't work in some SDK
      // versions and in FBOSS OSS
      getAgentEnsemble()->runDiagCommand("\n", out, switchId);
      getAgentEnsemble()->runDiagCommand(
          "setreg FDA_OFM_CLASS_DROP_TH_CORE 0x001001001001001001001001\n",
          out,
          switchId);
      getAgentEnsemble()->runDiagCommand("quit\n", out, switchId);
    }
  };

  auto verify = [this, testPort, &ecmpHelper]() {
    auto sendPkts = [this, testPort, &ecmpHelper]() {
      for (auto i = 0; i < 1000; ++i) {
        sendPacket(
            ecmpHelper.ip(testPort),
            std::nullopt,
            std::vector<uint8_t>(1024, 0xff));
      }
    };
    int64_t fdrFifoWatermark = 0;
    int64_t fdrCellDrops = 0;
    WITH_RETRIES({
      sendPkts();
      for (const auto& switchWatermarksIter : getAllSwitchWatermarkStats()) {
        if (switchWatermarksIter.second.fdrFifoWatermarkBytes().has_value()) {
          fdrFifoWatermark +=
              switchWatermarksIter.second.fdrFifoWatermarkBytes().value();
        }
      }
      EXPECT_EVENTUALLY_GT(fdrFifoWatermark, 0);
      fdrCellDrops = *getAggregatedSwitchDropStats().fdrCellDrops();
      // TLTimeseries value > 0
      EXPECT_EVENTUALLY_GT(fdrCellDrops, 0);
    });
    XLOG(DBG0) << "FDR fifo watermark: " << fdrFifoWatermark
               << ", FDR cell drops: " << fdrCellDrops;
    // Assert that we don't spuriously increment fdrCellDrops on every drop
    // stats. This would happen if we treated a stat as clear on read, while
    // in HW it was cumulative
    checkStatsStabilize(30 /*retries*/);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentVoqSwitchWithFabricPortsTest, fabricLinkDownCellDropCounter) {
  auto setup = [=, this]() {
    // Before drain all fabric ports should be active
    utility::checkFabricPortsActiveState(
        getAgentEnsemble(),
        masterLogicalFabricPortIds(),
        true /*expectActive*/);
  };

  auto verify = [&]() {
    auto fabPort = masterLogicalFabricPortIds()[0];
    uint64_t prevStats{0};

    // Trigger SAI_PORT_STAT_IF_IN_LINK_DOWN_CELL_DROP by following the
    // suggestion in CS00012385619. If the Bucket-Link-Down-Threshold
    // equals 63, the links will go down, followed by 61 that brings it
    // back up.
    WITH_RETRIES({
      std::string out;
      for (const auto& switchId : getSw()->getHwAsicTable()->getSwitchIDs()) {
        getAgentEnsemble()->runDiagCommand(
            "m FMAC_LEAKY_BUCKET_CONTROL_REGISTER(0) BKT_LINK_DN_TH_N=63\n",
            out,
            switchId);
        getAgentEnsemble()->runDiagCommand(
            "m FMAC_LEAKY_BUCKET_CONTROL_REGISTER(1) BKT_LINK_DN_TH_N=63\n",
            out,
            switchId);
        getAgentEnsemble()->runDiagCommand(
            "m FMAC_LEAKY_BUCKET_CONTROL_REGISTER(2) BKT_LINK_DN_TH_N=63\n",
            out,
            switchId);
        getAgentEnsemble()->runDiagCommand(
            "m FMAC_LEAKY_BUCKET_CONTROL_REGISTER(3) BKT_LINK_DN_TH_N=63\n",
            out,
            switchId);
        getAgentEnsemble()->runDiagCommand(
            "m FMAC_LEAKY_BUCKET_CONTROL_REGISTER(0) BKT_LINK_DN_TH_N=61\n",
            out,
            switchId);
        getAgentEnsemble()->runDiagCommand(
            "m FMAC_LEAKY_BUCKET_CONTROL_REGISTER(1) BKT_LINK_DN_TH_N=61\n",
            out,
            switchId);
        getAgentEnsemble()->runDiagCommand(
            "m FMAC_LEAKY_BUCKET_CONTROL_REGISTER(2) BKT_LINK_DN_TH_N=61\n",
            out,
            switchId);
        getAgentEnsemble()->runDiagCommand(
            "m FMAC_LEAKY_BUCKET_CONTROL_REGISTER(3) BKT_LINK_DN_TH_N=61\n",
            out,
            switchId);
      }
      uint64_t stats =
          getLatestPortStats(fabPort).fabricLinkDownDroppedCells_().value();
      XLOG(DBG2) << "Fabric link down cell drop counter : " << stats;
      // Ensure counter doesn't go backward!
      EXPECT_GE(stats, prevStats);
      prevStats = stats;
      // The fabric link down cell drop counter increments by max 1 per
      // iteration, ensure that we can see a value of 3 eventually, which
      // means the COR counter is being accumulated as expected in FBOSS.
      EXPECT_EVENTUALLY_GE(stats, 3);
    });
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentVoqSwitchWithFabricPortsTest, ValidateFecErrorDetect) {
  auto verify = [this]() {
    utility::setupFecErrorDetectEnable(
        getAgentEnsemble(), true /*fecErrorDetectEnable*/);
    utility::validateFecErrorDetectInState(
        getProgrammedState().get(), true /*fecErrorDetectEnable*/);
    utility::setupFecErrorDetectEnable(
        getAgentEnsemble(), false /*fecErrorDetectEnable*/);
    utility::validateFecErrorDetectInState(
        getProgrammedState().get(), false /*fecErrorDetectEnable*/);
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentVoqSwitchWithFabricPortsTest, verifyRxFifoStuckDetectedCallback) {
  auto verify = [this]() {
    std::string out;
    WITH_RETRIES({
      for (const auto& switchId : getSw()->getHwAsicTable()->getSwitchIDs()) {
        getAgentEnsemble()->runDiagCommand(
            "fabric link rx_fifo_monitor action=TRIGGER\n", out, switchId);
        auto multiSwitchStats = getSw()->getHwSwitchStatsExpensive();
        auto switchIndex =
            getSw()->getSwitchInfoTable().getSwitchIndexFromSwitchId(switchId);
        auto asicError = *multiSwitchStats[switchIndex].hwAsicErrors();
        auto rxFifoStuckDetected = asicError.rxFifoStuckDetected().value_or(0);
        XLOG(DBG2) << "Switch ID: " << switchId
                   << ", rxFifoStuckDetected: " << rxFifoStuckDetected;
        EXPECT_EVENTUALLY_GT(rxFifoStuckDetected, 0);
      }
    });
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

} // namespace facebook::fboss
