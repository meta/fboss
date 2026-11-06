// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/NeighborUpdater.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_constants.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/hw/test/HwTestCoppUtils.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/state/AggregatePortMap.h"
#include "fboss/agent/state/MacTable.h"
#include "fboss/agent/state/StateUtils.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/state/VlanMap.h"
#include "fboss/agent/test/AgentEnsemble.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/ResourceLibUtil.h"
#include "fboss/agent/test/utils/StatsTestUtils.h"
#include "fboss/lib/CommonUtils.h"

DEFINE_bool(
    list_production_feature,
    false,
    "list production feature needed for every single test");

DEFINE_bool(
    disable_link_toggler,
    false,
    "Used by certain tests where we don't want to bring up ports by toggler");

DEFINE_bool(
    enable_sdk_dump_on_fail,
    false,
    "generate sdk debug dump on failure");

namespace {
int kArgc;
char** kArgv;
} // namespace

namespace facebook::fboss {
namespace {
bool haveL3Asics(const std::map<SwitchID, const HwAsic*>& asics) {
  return std::any_of(asics.begin(), asics.end(), [](auto& iter) {
    auto switchType = iter.second->getSwitchType();
    return switchType == cfg::SwitchType::NPU ||
        switchType == cfg::SwitchType::VOQ;
  });
}
} // namespace

void AgentHwTest::SetUp() {
  gflags::ParseCommandLineFlags(&kArgc, &kArgv, false);
  if (FLAGS_list_production_feature) {
    printProductionFeatures();
    return;
  }
  auto config = fbossCommonInit(kArgc, kArgv);
  // Reset any global state being tracked in singletons
  // Each test then sets up its own state as needed.
  folly::SingletonVault::singleton()->destroyInstances();
  folly::SingletonVault::singleton()->reenableInstances();

  setCmdLineFlagOverrides();

  AgentEnsembleSwitchConfigFn initialConfigFn =
      [this](const AgentEnsemble& ensemble) {
        try {
          return initialConfig(ensemble);

        } catch (const std::exception& e) {
          XLOG(FATAL) << "Failed to create initial config: " << e.what();
        }
      };

  // Create init info with initial values - tests can override via
  // overrideTestEnsembleInitInfo
  TestEnsembleInitInfo initInfo;
  initInfo.failHwCallsOnWarmboot = failHwCallsOnWarmboot();
  initInfo.maxRequiredInterfacePorts = maxRequiredInterfacePorts();
  initInfo.maxRequiredFabricPorts = maxRequiredFabricPorts();
  overrideTestEnsembleInitInfo(initInfo);

  agentEnsemble_ = createAgentEnsemble(
      initialConfigFn,
      FLAGS_disable_link_toggler /*disableLinkStateToggler*/,
      [this](const cfg::SwitchConfig& sw, cfg::PlatformConfig& cfg) {
        applyPlatformConfigOverrides(sw, cfg);
      },
      (HwSwitch::FeaturesDesired::PACKET_RX_DESIRED |
       HwSwitch::FeaturesDesired::LINKSCAN_DESIRED |
       HwSwitch::FeaturesDesired::TAM_EVENT_NOTIFY_DESIRED),
      initInfo);
}

void AgentHwTest::setCmdLineFlagOverrides() const {
  // Default values for flags which maybe overridden by derived class tests
  FLAGS_enable_snapshot_debugs = false;
  FLAGS_verify_apply_oper_delta = true;
  FLAGS_hide_fabric_ports = true;
  // hide all interface ports to ensure hyper port is used in test run
  FLAGS_hide_interface_ports = FLAGS_hyper_port;
  // Don't send/receive periodic lldp packets. They will
  // interfere with tests.
  FLAGS_enable_lldp = false;
  // Disable tun intf, else pkts from host will interfere
  // with tests
  FLAGS_tun_intf = false;
  // Set watermark stats update interval to 0 so we always refresh BST stats
  // in each updateStats call (same for VOQ stats)
  FLAGS_update_watermark_stats_interval_s = 0;
  FLAGS_update_voq_stats_interval_s = 0;
  // Always collect cable lengthhs in each iteration of
  // stats collection loop.
  FLAGS_update_cable_length_stats_s = 0;
  // disable neighbor updates
  FLAGS_disable_neighbor_updates = true;
  // disable icmp error response
  FLAGS_disable_icmp_error_response = true;
  // Disable FSDB publishing on single-box test
  FLAGS_publish_stats_to_fsdb = false;
  FLAGS_publish_state_to_fsdb = false;
  // Looped ports are the common case in tests
  FLAGS_disable_looped_fabric_ports = false;
  // Looped fabric ports show up as wrong fabric connection.
  // disable this detection
  FLAGS_detect_wrong_fabric_connections = false;
  // Disable DSF subscription on single-box test
  FLAGS_dsf_subscribe = false;
  // Set HW agent connection timeout to 130 seconds
  FLAGS_hw_agent_connection_timeout_ms = 130000;
  FLAGS_update_stats_interval_s = 1;
}

void AgentHwTest::TearDown() {
  if (FLAGS_run_forever ||
      (::testing::Test::HasFailure() && FLAGS_run_forever_on_failure)) {
    runForever();
  }
  if (FLAGS_enable_sdk_dump_on_fail &&
      (testing::Test::HasFatalFailure() || testing::Test::HasFailure())) {
    agentEnsemble_->getHwDebugDump();
  }
  tearDownAgentEnsemble();
}

void AgentHwTest::tearDownAgentEnsemble(bool doWarmboot) {
  if (!agentEnsemble_) {
    return;
  }

  if (::testing::Test::HasFailure()) {
    // TODO: Collect Info and dump counters
  }
  if (doWarmboot) {
    agentEnsemble_->gracefulExit();
  }
  agentEnsemble_.reset();
}

void AgentHwTest::runForever() const {
  XLOG(DBG2) << "AgentHwTest run forever...";
  while (true) {
    sleep(1);
    XLOG_EVERY_MS(DBG2, 5000) << "AgentHwTest running forever";
  }
}

std::shared_ptr<SwitchState> AgentHwTest::applyNewConfig(
    const cfg::SwitchConfig& config) {
  return agentEnsemble_->applyNewConfig(config);
}

SwSwitch* AgentHwTest::getSw() const {
  return agentEnsemble_->getSw();
}

SwitchID AgentHwTest::getCurrentSwitchIdForTesting() const {
  return SwitchID(FLAGS_switch_id_for_testing);
}

int16_t AgentHwTest::getCurrentSwitchIndexForTesting() const {
  auto switchId = getCurrentSwitchIdForTesting();
  auto switchSettings =
      utility::getFirstNodeIf(getProgrammedState()->getSwitchSettings());
  auto switchIdToSwitchInfo = switchSettings->getSwitchIdToSwitchInfo();
  auto it = switchIdToSwitchInfo.find(static_cast<int64_t>(switchId));
  if (it == switchIdToSwitchInfo.end()) {
    throw FbossError(
        "SwitchId ",
        static_cast<int64_t>(switchId),
        " not found in SwitchIdToSwitchInfo");
  }
  return *it->second.switchIndex();
}

SwitchID AgentHwTest::getSwitchIdUnderTest(const AgentEnsemble& ensemble) {
  return ensemble.getSw()
      ->getScopeResolver()
      ->scope(ensemble.masterLogicalPortIds()[0])
      .switchId();
}

bool AgentHwTest::sendPacketSwitchedAsync(std::unique_ptr<TxPacket> pkt) {
  return getSw()->sendPacketSwitchedAsync(
      std::move(pkt), {getSwitchIdUnderTest(*getAgentEnsemble())});
}

void AgentHwTest::learnL2EntryIfPending(
    folly::MacAddress mac,
    PortID port,
    const std::function<void()>& sendPkt) {
  // Learn only on BCM: Cisco leaba 24.8 never learns a router-MAC SA, so the
  // wait below would time out. Drop the BCM-only check once the fleet moves
  // off leaba 24.8.
  auto asic = hwAsicForPort(port);
  if (asic->getAsicVendor() != HwAsic::AsicVendor::ASIC_VENDOR_BCM ||
      !asic->isSupported(HwAsic::Feature::PENDING_L2_ENTRY) ||
      utility::getFirstNodeIf(getProgrammedState()->getSwitchSettings())
              ->getL2LearningMode() != cfg::L2LearningMode::SOFTWARE) {
    return;
  }
  auto learnt = [&]() {
    auto state = getProgrammedState();
    auto vlan = state->getPorts()->getNodeIf(port)->getIngressVlan();
    auto entry = state->getVlans()->getNode(vlan)->getMacTable()->getMacIf(mac);
    auto aggPort = state->getAggregatePorts()->getAggregatePortForPort(port);
    return entry &&
        (entry->getPort() == PortDescriptor(port) ||
         (aggPort && entry->getPort() == PortDescriptor(aggPort->getID())));
  };
  if (learnt()) {
    return;
  }
  sendPkt();
  WITH_RETRIES({ EXPECT_EVENTUALLY_TRUE(learnt()); });
  // Refresh stats so callers' baselines include this frame.
  getNextUpdatedPortStats(port);
}

void AgentHwTest::learnL2EntryIfPending(folly::MacAddress mac, PortID port) {
  learnL2EntryIfPending(mac, port, [&]() {
    auto state = getProgrammedState();
    // Router MAC as DA so the frame is not flooded back in on other ports.
    getSw()->sendPacketOutOfPortAsync(
        utility::makeEthTxPacket(
            getSw(),
            state->getPorts()->getNodeIf(port)->getIngressVlan(),
            mac,
            getMacForFirstInterfaceWithPorts(state),
            ETHERTYPE::ETHERTYPE_WOL),
        port);
  });
}

void AgentHwTest::learnSrcMacOnEcmpTxPortIfPending(int ecmpWidth) {
  auto state = getProgrammedState();
  auto intfMac = getMacForFirstInterfaceWithPorts(state);
  utility::EcmpSetupAnyNPorts6 ecmpHelper(
      state, getSw()->needL2EntryForNeighbor());
  learnL2EntryIfPending(
      utility::MacAddressGenerator().get(intfMac.u64HBO() + 1),
      ecmpHelper.ecmpPortDescriptorAt(ecmpWidth).phyPortID());
}

folly::MacAddress AgentHwTest::getMacForFirstInterfaceWithPorts(
    const std::shared_ptr<SwitchState>& state) {
  return utility::getMacForFirstInterfaceWithPorts(
      state, getSwitchIdUnderTest(*getAgentEnsemble()));
}

InterfaceID AgentHwTest::firstInterfaceIDWithPorts(
    const std::shared_ptr<SwitchState>& state) {
  return utility::firstInterfaceIDWithPorts(
      state, getSwitchIdUnderTest(*getAgentEnsemble()));
}

const std::map<SwitchID, const HwAsic*> AgentHwTest::getAsics() const {
  return agentEnsemble_->getSw()->getHwAsicTable()->getHwAsics();
}

bool AgentHwTest::isSupportedOnAllAsics(HwAsic::Feature feature) const {
  // All Asics supporting the feature
  return agentEnsemble_->getSw()->getHwAsicTable()->isFeatureSupportedOnAllAsic(
      feature);
}

AgentEnsemble* AgentHwTest::getAgentEnsemble() const {
  return agentEnsemble_.get();
}

const std::shared_ptr<SwitchState> AgentHwTest::getProgrammedState() const {
  return getAgentEnsemble()->getProgrammedState();
}

std::vector<PortID> AgentHwTest::masterLogicalPortIds(
    std::optional<SwitchID> switchId) const {
  // If no switchIndex provided, return default filtered ports
  if (!switchId.has_value()) {
    return getAgentEnsemble()->masterLogicalPortIds();
  }
  return getAgentEnsemble()->masterLogicalPortIds(switchId.value());
}

std::vector<PortID> AgentHwTest::masterLogicalPortIds(
    const std::set<cfg::PortType>& portTypes) const {
  return getAgentEnsemble()->masterLogicalPortIds(portTypes);
}

std::vector<PortID> AgentHwTest::masterLogicalPortIds(
    const std::set<cfg::PortType>& portTypes,
    SwitchID switchId) const {
  return getAgentEnsemble()->masterLogicalPortIds(portTypes, switchId);
}

void AgentHwTest::setSwitchDrainState(
    const cfg::SwitchConfig& curConfig,
    cfg::SwitchDrainState drainState) {
  auto newCfg = curConfig;
  *newCfg.switchSettings()->switchDrainState() = drainState;
  applyNewConfig(newCfg);
}

void AgentHwTest::applySwitchDrainState(cfg::SwitchDrainState drainState) {
  applyNewState([&](const std::shared_ptr<SwitchState>& in) {
    auto out = in->clone();
    for (const auto& [_, switchSetting] :
         std::as_const(*out->getSwitchSettings())) {
      auto newSwitchSettings = switchSetting->modify(&out);
      newSwitchSettings->setActualSwitchDrainState(drainState);
    }
    return out;
  });
}

cfg::SwitchConfig AgentHwTest::initialConfig(
    const AgentEnsemble& ensemble) const {
  auto anyL3Asics =
      haveL3Asics(ensemble.getSw()->getHwAsicTable()->getHwAsics());
  // Build the initial config from the capped port view, so the config is
  // trimmed to maxRequired*Ports. Tests that need every port opt out via
  // maxRequiredInterfacePorts()/maxRequiredFabricPorts() returning nullopt.
  auto config = utility::onePortPerInterfaceConfig(
      ensemble.getSw(),
      ensemble.masterLogicalPortIds(),
      anyL3Asics /*interfaceHasSubnet*/,
      anyL3Asics /*setInterfaceMac*/,
      utility::kBaseVlanId,
      !FLAGS_hide_fabric_ports /*enable fabric ports*/);
  return config;
}

void AgentHwTest::printProductionFeatures() const {
  std::vector<std::string> asicFeatures;
  for (const auto& feature : getProductionFeaturesVerified()) {
    asicFeatures.push_back(apache::thrift::util::enumNameSafe(feature));
  }
  std::cout << "Feature List: " << folly::join(",", asicFeatures) << "\n";
  GTEST_SKIP();
}

std::map<PortID, HwPortStats> AgentHwTest::getLatestPortStats(
    const std::vector<PortID>& ports) {
  return getAgentEnsemble()->getLatestPortStats(ports);
}

HwPortStats AgentHwTest::getLatestPortStats(const PortID& port) {
  return getLatestPortStats(std::vector<PortID>({port})).begin()->second;
}

std::map<PortID, HwPortStats> AgentHwTest::extractPortStats(
    const std::vector<PortID>& ports,
    const std::map<uint16_t, multiswitch::HwSwitchStats>& switch2Stats) {
  std::map<PortID, HwPortStats> portStats;
  for (auto portId : ports) {
    auto portSwitchId = scopeResolver().scope(portId).switchId();
    auto portSwitchIndex =
        getSw()->getSwitchInfoTable().getSwitchIndexFromSwitchId(portSwitchId);
    auto port = getProgrammedState()->getPort(portId);
    const auto& switchStats =
        switch2Stats.find(static_cast<uint16_t>(portSwitchIndex))->second;
    portStats[portId] =
        switchStats.hwPortStats()->find(port->getName())->second;
  }
  return portStats;
}

HwPortStats AgentHwTest::extractPortStats(
    PortID port,
    const std::map<uint16_t, multiswitch::HwSwitchStats>& switch2Stats) {
  return extractPortStats(std::vector<PortID>({port}), switch2Stats)
      .begin()
      ->second;
}

std::map<PortID, HwPortStats> AgentHwTest::getNextUpdatedPortStats(
    const std::vector<PortID>& ports) {
  const auto lastPortStats = getLatestPortStats(ports);
  std::map<PortID, HwPortStats> nextPortStats;
  checkWithRetry(
      [&lastPortStats, &nextPortStats, &ports, this]() {
        nextPortStats = getSw()->getHwPortStats(ports);
        // Check collect timestamp is different from last port stats
        for (const auto& [portId, portStats] : nextPortStats) {
          if (*portStats.timestamp_() ==
              *lastPortStats.at(portId).timestamp_()) {
            return false;
          }
        }
        return !nextPortStats.empty();
      },
      120,
      std::chrono::milliseconds(1000),
      " fetch next port stats");
  return nextPortStats;
}

HwPortStats AgentHwTest::getNextUpdatedPortStats(const PortID& port) {
  return getNextUpdatedPortStats(std::vector<PortID>({port})).begin()->second;
}

// return last incremented port stats. the port stats contains a timer
// which callers can use to determine when traffic stopped by checking the out
// bytes
HwPortStats AgentHwTest::getLastIncrementedPortStats(const PortID& port) {
  HwPortStats lastPortStats = getLatestPortStats(port);
  // wait till port stats starts incrementing
  WITH_RETRIES({
    auto currentPortStats = getLatestPortStats(port);
    EXPECT_EVENTUALLY_TRUE(
        *currentPortStats.outBytes_() > *lastPortStats.outBytes_());
    lastPortStats = currentPortStats;
  });
  // wait till port stats stops incrementing
  WITH_RETRIES({
    auto currentPortStats = getLatestPortStats(port);
    if ((*currentPortStats.timestamp_() != *lastPortStats.timestamp_()) &&
        (*currentPortStats.outBytes_() == *lastPortStats.outBytes_())) {
      return lastPortStats;
    }
    lastPortStats = currentPortStats;
    EXPECT_EVENTUALLY_TRUE(false);
  });
  return lastPortStats;
}

// returns a pair of ports stats corresponding to
// start and end of traffic measurement duration
std::map<PortID, std::pair<HwPortStats, HwPortStats>>
AgentHwTest::sendTrafficAndCollectStats(
    const std::vector<PortID>& ports,
    int timeIntervalInSec,
    const std::function<void()>& startSendFn,
    const std::function<void()>& stopSendFn,
    bool keepTrafficRunning) {
  std::map<PortID, std::pair<HwPortStats, HwPortStats>> portStats;
  std::vector<HwPortStats> portStatsBefore;
  startSendFn();
  // In tests like QoS scheduler, smaller number of low priority packets might
  // go through initially but dopped soon in subsequent looping. So, wait
  // some time for traffic stablizing before collecting portStatsBefore
  sleep(timeIntervalInSec);
  for (const auto& port : ports) {
    portStatsBefore.push_back(getLatestPortStats(port));
  }
  sleep(timeIntervalInSec);
  if (!keepTrafficRunning) {
    stopSendFn();
  }
  auto index = 0;
  for (const auto& port : ports) {
    portStats.insert(
        {port,
         {portStatsBefore[index++],
          keepTrafficRunning ? getLatestPortStats(port)
                             : getLastIncrementedPortStats(port)}});
  }
  return portStats;
}

std::map<SystemPortID, HwSysPortStats> AgentHwTest::getLatestSysPortStats(
    const std::vector<SystemPortID>& ports) {
  std::map<std::string, HwSysPortStats> systemPortStats;
  std::map<SystemPortID, HwSysPortStats> portIdStatsMap;
  const auto statsNamePrefix =
      "switch." + std::to_string(getCurrentSwitchIndexForTesting()) + ".";
  checkWithRetry(
      [&systemPortStats, &portIdStatsMap, &ports, statsNamePrefix, this]() {
        portIdStatsMap.clear();

        getSw()->getAllHwSysPortStats(systemPortStats);
        for (auto [portStatName, stats] : systemPortStats) {
          if (portStatName.rfind(statsNamePrefix, 0) != 0) {
            continue;
          }
          auto portName = portStatName.substr(statsNamePrefix.size());
          SystemPortID portId;
          try {
            if (portName.find("cpu") != std::string::npos) {
              portId = 0;
            } else {
              portId = getProgrammedState()
                           ->getSystemPorts()
                           ->getSystemPort(portName)
                           ->getID();
            }
          } catch (const FbossError&) {
            // Look in remote sys ports if we couldn't find in local sys ports
            portId = getProgrammedState()
                         ->getRemoteSystemPorts()
                         ->getSystemPort(portName)
                         ->getID();
          }
          if (std::find(ports.begin(), ports.end(), portId) != ports.end()) {
            if (*stats.timestamp_() !=
                hardware_stats_constants::STAT_UNINITIALIZED()) {
              portIdStatsMap.emplace(portId, stats);
            }
          }
        }
        return ports.size() == portIdStatsMap.size();
      },
      120,
      std::chrono::milliseconds(1000),
      " fetch system port stats");

  return portIdStatsMap;
}

HwSysPortStats AgentHwTest::getLatestSysPortStats(const SystemPortID& port) {
  return getLatestSysPortStats(std::vector<SystemPortID>({port}))
      .begin()
      ->second;
}

std::optional<HwSysPortStats> AgentHwTest::getLatestCpuSysPortStats() {
  std::map<std::string, HwSysPortStats> systemPortStats;
  std::optional<HwSysPortStats> portStats;
  const auto statsNamePrefix =
      "switch." + std::to_string(getCurrentSwitchIndexForTesting()) + ".";
  checkWithRetry(
      [&systemPortStats, &portStats, statsNamePrefix, this]() {
        bool found = false;
        getSw()->getAllHwSysPortStats(systemPortStats);
        for (auto [portStatName, stats] : systemPortStats) {
          if (portStatName.rfind(statsNamePrefix, 0) != 0) {
            continue;
          }
          auto portName = portStatName.substr(statsNamePrefix.size());
          if (portName.find("cpu") != std::string::npos) {
            XLOG(DBG2) << "found cpu port stats for " << portStatName;
            portStats = stats;
            found = true;
          }
        }
        return found;
      },
      120,
      std::chrono::milliseconds(1000),
      " fetch cpu system port stats");

  return portStats;
}

multiswitch::HwSwitchStats AgentHwTest::getHwSwitchStats(
    std::optional<uint16_t> switchIndex) const {
  auto idx = switchIndex.value_or(getCurrentSwitchIndexForTesting());
  multiswitch::HwSwitchStats switchStats;
  checkWithRetry(
      [idx, &switchStats, this]() {
        auto switchIndex2Stats = getAllHwSwitchStats();
        auto sitr = switchIndex2Stats.find(idx);
        if (sitr != switchIndex2Stats.end()) {
          switchStats = std::move(sitr->second);
          return true;
        }
        return false;
      },
      120,
      std::chrono::milliseconds(1000),
      " fetch multiswitch::HwSwitchStats");
  return switchStats;
}

std::map<uint16_t, multiswitch::HwSwitchStats>
AgentHwTest::getAllHwSwitchStats() const {
  return getSw()->getHwSwitchStatsExpensive();
}

HwSwitchDropStats AgentHwTest::getAggregatedSwitchDropStats() {
  HwSwitchDropStats hwSwitchDropStats;
  checkWithRetry([&hwSwitchDropStats, this]() {
    HwSwitchDropStats aggHwSwitchDropStats;
    auto switchIndex2Stats = getAllHwSwitchStats();

    for (const auto& switchIndex :
         getSw()->getSwitchInfoTable().getSwitchIndices()) {
      auto switchStatsIter = switchIndex2Stats.find(switchIndex);
      if (switchStatsIter == switchIndex2Stats.end()) {
        return false;
      }
      const auto& switchStats = switchStatsIter->second;
      const auto& dropStats = *switchStats.switchDropStats();

#define FILL_DROP_COUNTERS(stat)                       \
  aggHwSwitchDropStats.stat##Drops() =                 \
      aggHwSwitchDropStats.stat##Drops().value_or(0) + \
      dropStats.stat##Drops().value_or(0);

      FILL_DROP_COUNTERS(global);
      FILL_DROP_COUNTERS(globalReachability);
      FILL_DROP_COUNTERS(packetIntegrity);
      FILL_DROP_COUNTERS(fdrCell);
      FILL_DROP_COUNTERS(voqResourceExhaustion);
      FILL_DROP_COUNTERS(globalResourceExhaustion);
      FILL_DROP_COUNTERS(sramResourceExhaustion);
      FILL_DROP_COUNTERS(vsqResourceExhaustion);
      FILL_DROP_COUNTERS(dropPrecedence);
      FILL_DROP_COUNTERS(queueResolution);
      FILL_DROP_COUNTERS(ingressPacketPipelineReject);
      FILL_DROP_COUNTERS(corruptedCellPacketIntegrity);
      FILL_DROP_COUNTERS(tc0RateLimit);
    }
    hwSwitchDropStats = aggHwSwitchDropStats;
    return true;
  });
  return hwSwitchDropStats;
}

std::map<uint16_t, HwSwitchWatermarkStats>
AgentHwTest::getAllSwitchWatermarkStats() {
  std::map<uint16_t, HwSwitchWatermarkStats> watermarkStats;
  const auto& hwSwitchStatsMap = getSw()->getHwSwitchStatsExpensive();
  for (const auto& [switchIdx, hwSwitchStats] : hwSwitchStatsMap) {
    watermarkStats.emplace(switchIdx, *hwSwitchStats.switchWatermarkStats());
  }
  return watermarkStats;
}

void AgentHwTest::applyNewStateImpl(
    StateUpdateFn fn,
    const std::string& name,
    bool transaction) {
  agentEnsemble_->applyNewState(fn, name, transaction);
}

cfg::SwitchConfig AgentHwTest::addCoppConfig(
    const AgentEnsemble& ensemble,
    const cfg::SwitchConfig& in) const {
  auto config = in;
  // Before m-mpu agent test, use first Asic for initialization.
  auto switchIds = ensemble.getSw()->getHwAsicTable()->getSwitchIDs();
  CHECK_GE(switchIds.size(), 1);
  auto asic =
      ensemble.getSw()->getHwAsicTable()->getHwAsic(*switchIds.cbegin());
  const auto& cpuStreamTypes =
      asic->getQueueStreamTypes(cfg::PortType::CPU_PORT);
  utility::setDefaultCpuTrafficPolicyConfig(
      config, ensemble.getL3Asics(), ensemble.isSai());
  utility::addCpuQueueConfig(config, ensemble.getL3Asics(), ensemble.isSai());
  return config;
}

void AgentHwTest::checkStatsStabilize(
    int trys,
    const std::function<
        void(const std::map<uint16_t, multiswitch::HwSwitchStats>&)>&
        assertForNoChange) {
  auto resetDontCareValues = [](auto& statsMap) {
    for (auto& [_, stats] : statsMap) {
      stats.timestamp() = 0;
      for (auto& [_, portStats] : *stats.hwPortStats()) {
        portStats.timestamp_() = 0;
      }
      for (auto& [_, sysPortStats] : *stats.sysPortStats()) {
        sysPortStats.timestamp_() = 0;
      }
      stats.teFlowStats()->timestamp() = 0;
      // PhyInfo can be noisy and dependent on external
      // physical params. Don't compare these
      stats.phyInfo()->clear();
      // temperature stats can be noisy
      (*stats.switchTemperatureStats()).value().value().clear();
      (*stats.switchTemperatureStats()).timeStamp().value().clear();
    }
  };
  auto timestampChanged = [](const auto& before, const auto& after) {
    for (auto& [switchId, stats] : before) {
      if (stats.timestamp() == after.at(switchId).timestamp()) {
        return false;
      }
    }
    return true;
  };
  WITH_RETRIES_N(trys, ({
                   auto before = getSw()->getHwSwitchStatsExpensive();
                   std::map<uint16_t, multiswitch::HwSwitchStats> after;
                   checkWithRetry(
                       [&before, &after, this, &timestampChanged]() {
                         after = getSw()->getHwSwitchStatsExpensive();
                         return timestampChanged(before, after);
                       },
                       20,
                       std::chrono::milliseconds(100),
                       " fetch port stats");
                   EXPECT_EVENTUALLY_TRUE(timestampChanged(before, after));
                   resetDontCareValues(before);
                   resetDontCareValues(after);
                   assertForNoChange(after);
                   EXPECT_EVENTUALLY_EQ(before, after)
                       << statsDelta(before, after);
                 }));
}

SwitchID AgentHwTest::switchIdForPort(PortID port) const {
  return scopeResolver().scope(port).switchId();
}

const HwAsic* AgentHwTest::hwAsicForPort(PortID port) const {
  return hwAsicForSwitch(switchIdForPort(port));
}

const HwAsic* AgentHwTest::hwAsicForSwitch(SwitchID switchID) const {
  return getSw()->getHwAsicTable()->getHwAsic(switchID);
}

void AgentHwTest::populateArpNeighborsToCache(
    const std::shared_ptr<Interface>& interface) {
  auto arpCache = getAgentEnsemble()
                      ->getSw()
                      ->getNeighborUpdater()
                      ->getArpCacheForIntf(interface->getID())
                      .get();
  getAgentEnsemble()->getSw()->getNeighborCacheEvb()->runInFbossEventBaseThread(
      [interface, arpCache] {
        arpCache->repopulate(interface->getArpTable());
      });
}

void AgentHwTest::populateNdpNeighborsToCache(
    const std::shared_ptr<Interface>& interface) {
  auto ndpCache = getAgentEnsemble()
                      ->getSw()
                      ->getNeighborUpdater()
                      ->getNdpCacheForIntf(interface->getID())
                      .get();
  getAgentEnsemble()->getSw()->getNeighborCacheEvb()->runInFbossEventBaseThread(
      [interface, ndpCache] {
        ndpCache->repopulate(interface->getNdpTable());
      });
}

bool isWarmbootSetupRequested() {
  // Parse gflags to read the setup_for_warmboot flag
  // The false parameter means don't remove parsed flags from argc/argv
  gflags::ParseCommandLineFlags(&kArgc, &kArgv, false);
  return FLAGS_setup_for_warmboot;
}

void initAgentHwTest(
    int argc,
    char* argv[],
    PlatformInitFn initPlatform,
    std::optional<cfg::StreamType> streamType) {
  initEnsemble(initPlatform, streamType);
  kArgc = argc;
  kArgv = argv;
}

} // namespace facebook::fboss
