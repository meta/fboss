// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/agent_hw_tests/AgentFabricSwitchFabricLinkMonitoringTests.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/test/utils/DsfConfigUtils.h"
#include "fboss/agent/test/utils/FabricTestUtils.h"
#include "fboss/agent/test/utils/PortTestUtils.h"

#include <folly/testing/TestUtil.h>

namespace facebook::fboss {

namespace {

// Number of RDSW (VOQ) switches connected to this FDSW (L1 fabric switch)
// Each RDSW has one switch ID visible from this FDSW
// Based on actual fabric connectivity: 128 RDSW devices
constexpr int kNumRdswSwitches = 128;

// Number of SDSW (L2 fabric) switches connected to this FDSW
// Based on actual fabric connectivity: 128 SDSW devices with 4 ASICs each
constexpr int kNumSdswSwitches = 128;
constexpr int kNumSdswAsicsPerSwitch = 4;

// Number of FDSW (L1 fabric) switches in the same cluster
// Based on actual config: fdsw001-fdsw040 with n001.c083.nao5
constexpr int kNumFdswSwitches = 40;
constexpr int kNumFdswAsicsPerSwitch = 2;
constexpr int kNumSwitchIdsPerFdsw = 4;

// Number of fabric ports per ASIC (RAMON3 has 256 fabric ports per chip)
constexpr int kNumFabricPortsPerAsic = 256;

// Switch ID ranges - FDSW starts at 0 so the default test switch IDs
// fall within the FDSW range without requiring switch ID overrides.
// RDSW and SDSW ranges follow after the FDSW range.
// FDSW: 40 switches * 4 IDs = 160 (0-159)
// RDSW: 128 switches * 4 IDs = 512 (160-671)
// SDSW: 128 switches * 4 IDs = 512 (672-1183)
constexpr int64_t kFdswSwitchIdStart = 0;
constexpr int64_t kRdswSwitchIdStart =
    kFdswSwitchIdStart + kNumFdswSwitches * kNumSwitchIdsPerFdsw;
constexpr int64_t kSdswSwitchIdStart =
    kRdswSwitchIdStart + kNumRdswSwitches * kNumSwitchIdsPerFdsw;

constexpr auto kFabricLinkMonPuntPathCintStr = R"(
cint_reset();
int cint_instru_cell_profile_action_set(
    int unit,
    int flags,
    uint32 cell_profile,
    int forward,
    int mirror,
    int mirror_stat,
    int global_capture,
    int sop)
{
    int is_dnxf;
    uint32 action_profile_map = 0;

    BCM_IF_ERROR_RETURN_MSG(bcm_device_member_get(unit, 0, bcmDeviceMemberDNXF, &is_dnxf), " bcm_device_member_get Failed");
    if (!is_dnxf)
    {
        printf("Function is only for FE devices. Unit %d provided. Is DNXF %d.\n", unit,is_dnxf);
        return BCM_E_FAIL;
    }

    if (forward)
    {
        printf("Configure cell profile %d to FORWARD CELLs\n", cell_profile);
        BCM_FABRIC_CELL_PROFILE_MAP_ACTION_FORWARDING_SET(action_profile_map);
    }

    if (mirror)
    {
        printf("Configure cell profile %d to MIRROR CELLs\n", cell_profile);
        BCM_FABRIC_CELL_PROFILE_MAP_ACTION_MIRROR_SET(action_profile_map);
    }

    if (mirror_stat)
    {
        printf("Configure cell profile %d to MIRROR CELLs and take STATISTICS\n", cell_profile);
        BCM_FABRIC_CELL_PROFILE_MAP_ACTION_MIRROR_WITH_STATISTICS_SET(action_profile_map);
    }

    if (global_capture)
    {
        printf("Configure cell profile %d to trigger Global Capture\n", cell_profile);
        BCM_FABRIC_CELL_PROFILE_MAP_ACTION_GLOBAL_CAPTURE_SET(action_profile_map);
    }

    if (sop)
    {
        printf("Configure cell profile %d to trigger configured actions only on start of packet cell\n", cell_profile);
        BCM_FABRIC_CELL_PROFILE_MAP_ACTION_SOP_ONLY_SET(action_profile_map);
    }

    BCM_IF_ERROR_RETURN_MSG(bcm_fabric_profile_map_set(unit, flags, bcmFabricProfileMapCellProfileToActionProfile,
          cell_profile, action_profile_map), " bcm_fabric_profile_map_set Failed");

    return BCM_E_NONE;
}

int cint_cell_profile_forward_mirror_action_set(int unit, uint32 cell_profile)
{
    int forward = 1;
    int mirror = 1;
    int mirror_stat = 0;
    int global_capture = 0;
    int sop = 0;
    int flags = 0;
    BCM_IF_ERROR_RETURN_MSG(cint_instru_cell_profile_action_set(unit, flags, cell_profile,
        forward, mirror, mirror_stat, global_capture, sop), "cint_cell_profile_forward_mirror_action_set failed");

    return BCM_E_NONE;
}

int fe13_standalone_link_monitoring_packet_path_en(int unit)
{
    uint32 value;

    value = 1;
    BCM_IF_ERROR_RETURN_MSG(diag_reg_field_set(unit, "DCH_AUTO_DOC_NAME_0" , "SNAKE_NO_SORT", &value), " DCH_AUTO_DOC_NAME_0 Failed");

    value = 0;
    BCM_IF_ERROR_RETURN_MSG(diag_reg_field_set(unit, "DCH_AUTO_DOC_NAME_13" , "AUTO_DOC_NAME_14", &value), " DCH_AUTO_DOC_NAME_13 Failed");

    value = 0;
    BCM_IF_ERROR_RETURN_MSG(diag_reg_field_set(unit, "DCH_AUTO_DOC_NAME_36" , "FIELD_0_0", &value), " DCH_AUTO_DOC_NAME_36 Failed");

    value = 0;
    BCM_IF_ERROR_RETURN_MSG(diag_reg_field_set(unit, "DCH_DCH_ENABLERS_REGISTER_2_P" , "AUTO_DOC_NAME_10", &value), " DCH_DCH_ENABLERS_REGISTER_2_P Failed");

    value = 0xFF;
    BCM_IF_ERROR_RETURN_MSG(diag_reg_field_set(unit, "DCH_DCH_ENABLERS_REGISTER_3_P" , "AUTO_DOC_NAME_12", &value), " DCH_DCH_ENABLERS_REGISTER_3_P Failed");

    value = 1;
    BCM_IF_ERROR_RETURN_MSG(diag_reg_field_set(unit, "DTM_DTML_ENABLERS" , "AUTO_DOC_NAME_8", &value), " DTM_DTML_ENABLERS Failed");

    value = 0xFF;
    BCM_IF_ERROR_RETURN_MSG(diag_reg_field_set(unit, "DTM_DTM_LINK_ACTIVE_MASK_CFG" , "LINK_ACTIVE_MASK_EN_P_N", &value), " DTM_DTM_LINK_ACTIVE_MASK_CFG Failed");

    printf("fe13_standalone_link_monitoring_packet_path_en: PASS\n\n");
    return BCM_E_NONE;
}

fe13_standalone_link_monitoring_packet_path_en(0);
cint_cell_profile_forward_mirror_action_set(0, 2);
)";

// Get the simplified device name
std::string getRdswName(int rdswIndex) {
  return fmt::format("rdsw{:03d}", rdswIndex);
}

std::string getFdswName(int fdswIndex) {
  return fmt::format("fdsw{:03d}", fdswIndex);
}

std::string getSdswName(int sdswIndex) {
  return fmt::format("sdsw{:03d}", sdswIndex);
}

} // namespace

void AgentFabricSwitchFabricLinkMonitoringTest::SetUp() {
  AgentHwTest::SetUp();
  if (!IsSkipped()) {
    // Verify we are running on a fabric switch
    ASSERT_TRUE(
        std::any_of(getAsics().begin(), getAsics().end(), [](auto& iter) {
          return iter.second->getSwitchType() == cfg::SwitchType::FABRIC;
        }));
    // Verify we have fabric ports for at least one fabric switch
    // Use getSwitchInfoTable().getSwitchIdsOfType() to get switch IDs that
    // match the scope resolver's mapping (from config), not HwAsicTable (from
    // hardware)
    auto fabricSwitchIds = getSw()->getSwitchInfoTable().getSwitchIdsOfType(
        cfg::SwitchType::FABRIC);
    ASSERT_FALSE(fabricSwitchIds.empty()) << "No fabric switch IDs found";
    bool hasFabricPorts = false;
    for (const auto& switchId : fabricSwitchIds) {
      if (!getAgentEnsemble()->masterLogicalFabricPortIds(switchId).empty()) {
        hasFabricPorts = true;
        break;
      }
    }
    ASSERT_TRUE(hasFabricPorts)
        << "No fabric ports found for any of the fabric switch IDs";
  }
}

cfg::SwitchConfig AgentFabricSwitchFabricLinkMonitoringTest::initialConfig(
    const AgentEnsemble& ensemble) const {
  cfg::SwitchConfig config;

  // Switch settings with hardcoded switch IDs to ensure consistency
  // between cold boot and warm boot.
  config.switchSettings()->switchType() = cfg::SwitchType::FABRIC;
  config.switchSettings()->switchId() = 0;

  std::map<int64_t, cfg::SwitchInfo> switchIdToSwitchInfo;

  cfg::SwitchInfo switchInfo0;
  switchInfo0.switchType() = cfg::SwitchType::FABRIC;
  switchInfo0.asicType() = cfg::AsicType::ASIC_TYPE_RAMON3;
  switchInfo0.switchIndex() = 0;
  cfg::Range64 portIdRange0;
  portIdRange0.minimum() = 0;
  portIdRange0.maximum() = 2047;
  switchInfo0.portIdRange() = portIdRange0;
  switchInfo0.connectionHandle() = "15:00";
  cfg::SystemPortRanges sysPortRanges0;
  switchInfo0.systemPortRanges() = sysPortRanges0;
  switchIdToSwitchInfo[0] = switchInfo0;

  cfg::SwitchInfo switchInfo1;
  switchInfo1.switchType() = cfg::SwitchType::FABRIC;
  switchInfo1.asicType() = cfg::AsicType::ASIC_TYPE_RAMON3;
  switchInfo1.switchIndex() = 1;
  cfg::Range64 portIdRange1;
  portIdRange1.minimum() = 2048;
  portIdRange1.maximum() = 4095;
  switchInfo1.portIdRange() = portIdRange1;
  switchInfo1.connectionHandle() = "16:00";
  cfg::SystemPortRanges sysPortRanges1;
  switchInfo1.systemPortRanges() = sysPortRanges1;
  switchIdToSwitchInfo[2] = switchInfo1;

  config.switchSettings()->switchIdToSwitchInfo() = switchIdToSwitchInfo;

  addFabricPorts(config, ensemble);
  addDsfNodes(config, switchIdToSwitchInfo, ensemble);
  configureExpectedNeighborReachability(config);

  XLOG(DBG2) << "Configured DSF nodes: " << kNumRdswSwitches << " RDSWs, "
             << kNumFdswSwitches << " FDSWs, " << kNumSdswSwitches << " SDSWs";

  return config;
}

void AgentFabricSwitchFabricLinkMonitoringTest::addFabricPorts(
    cfg::SwitchConfig& config,
    const AgentEnsemble& ensemble) const {
  const auto* platformMapping = ensemble.getPlatformMapping();
  auto switchIds = ensemble.getHwAsicTable()->getSwitchIDs();
  CHECK(!switchIds.empty()) << "No switch IDs found";
  auto asic = ensemble.getHwAsicTable()->getHwAsic(*switchIds.begin());

  const auto& platformPorts = platformMapping->getPlatformPorts();
  const auto& portToDefaultProfileID = utility::getPortToDefaultProfileIDMap();
  auto desiredLoopbackMode = cfg::PortLoopbackMode::NONE;
  if (auto iter = asic->desiredLoopbackModes().find(cfg::PortType::FABRIC_PORT);
      iter != asic->desiredLoopbackModes().end()) {
    desiredLoopbackMode = iter->second;
  }

  for (const auto& [portIdInt, platformPort] : platformPorts) {
    if (*platformPort.mapping()->portType() != cfg::PortType::FABRIC_PORT) {
      continue;
    }
    auto profileIter = portToDefaultProfileID.find(PortID(portIdInt));
    if (profileIter == portToDefaultProfileID.end()) {
      continue;
    }
    auto profileId = profileIter->second;

    cfg::Port portCfg;
    portCfg.logicalID() = portIdInt;
    portCfg.name() = *platformPort.mapping()->name();
    portCfg.portType() = cfg::PortType::FABRIC_PORT;
    portCfg.state() = cfg::PortState::ENABLED;
    portCfg.loopbackMode() = desiredLoopbackMode;
    portCfg.profileID() = profileId;
    portCfg.speed() = utility::getSpeed(profileId);
    portCfg.ingressVlan() = 0;
    portCfg.maxFrameSize() = 0;
    portCfg.routable() = true;
    portCfg.parserType() = cfg::ParserType::L3;

    config.ports()->push_back(portCfg);
  }
}

void AgentFabricSwitchFabricLinkMonitoringTest::addDsfNodes(
    cfg::SwitchConfig& config,
    const std::map<int64_t, cfg::SwitchInfo>& switchIdToSwitchInfo,
    const AgentEnsemble& ensemble) const {
  auto fdswAsicType = cfg::AsicType::ASIC_TYPE_RAMON3;
  auto fdswPlatformType = PlatformType::PLATFORM_MERU800BFA;
  auto sdswAsicType = cfg::AsicType::ASIC_TYPE_RAMON3;
  auto sdswPlatformType = PlatformType::PLATFORM_MERU800BFA;
  auto rdswAsicType = cfg::AsicType::ASIC_TYPE_JERICHO3;
  auto rdswPlatformType = PlatformType::PLATFORM_MERU800BIA;

  config.dsfNodes()->clear();

  // Add local FDSW (L1 fabric switch) DSF nodes - one for each ASIC
  auto fabricAsics = ensemble.getHwAsicTable()->getFabricAsics();
  auto localAsicType = fabricAsics.empty()
      ? cfg::AsicType::ASIC_TYPE_RAMON3
      : (*fabricAsics.begin())->getAsicType();
  for (const auto& [localSwitchId, switchInfo] : switchIdToSwitchInfo) {
    utility::addFabricDsfNode(
        config,
        localSwitchId,
        getFdswName(1), // Local switch is fdsw001
        utility::kL1FabricLevel,
        fdswPlatformType,
        localAsicType);
  }

  // Add RDSW (VOQ) nodes
  for (int i = 0; i < kNumRdswSwitches; i++) {
    int64_t switchId = kRdswSwitchIdStart + (i * kNumSwitchIdsPerFdsw);
    cfg::DsfNode node;
    node.switchId() = switchId;
    node.name() = getRdswName(i + 1);
    node.type() = cfg::DsfNodeType::INTERFACE_NODE;
    node.asicType() = rdswAsicType;
    node.platformType() = rdswPlatformType;
    int64_t baseOffset = i * 1024;
    node.localSystemPortOffset() = baseOffset;
    node.globalSystemPortOffset() = baseOffset;
    cfg::SystemPortRanges sysPortRanges;
    cfg::Range64 range;
    range.minimum() = baseOffset;
    range.maximum() = baseOffset + 1023;
    sysPortRanges.systemPortRanges()->push_back(range);
    node.systemPortRanges() = sysPortRanges;
    node.inbandPortId() = 1;
    (*config.dsfNodes())[switchId] = node;
  }

  // Add remote FDSW (L1 fabric) nodes — skip i=0 (local switch already added
  // above with hardware-detected localAsicType)
  for (int i = 1; i < kNumFdswSwitches; i++) {
    std::string fdswName = getFdswName(i + 1);
    for (int asicIdx = 0; asicIdx < kNumFdswAsicsPerSwitch; asicIdx++) {
      int64_t switchId =
          kFdswSwitchIdStart + (i * kNumSwitchIdsPerFdsw) + (asicIdx * 2);
      utility::addFabricDsfNode(
          config,
          switchId,
          fdswName,
          utility::kL1FabricLevel,
          fdswPlatformType,
          fdswAsicType);
    }
  }

  // Add SDSW (L2 fabric) nodes
  for (int i = 0; i < kNumSdswSwitches; i++) {
    std::string sdswName = getSdswName(i + 1);
    for (int asicIdx = 0; asicIdx < kNumSdswAsicsPerSwitch; asicIdx++) {
      int64_t switchId =
          kSdswSwitchIdStart + (i * kNumSdswAsicsPerSwitch) + asicIdx;
      utility::addFabricDsfNode(
          config,
          switchId,
          sdswName,
          utility::kL2FabricLevel,
          sdswPlatformType,
          sdswAsicType);
    }
  }
}

void AgentFabricSwitchFabricLinkMonitoringTest::
    configureExpectedNeighborReachability(cfg::SwitchConfig& config) const {
  // Assign expected neighbors sequentially across fabric ports.
  // Each ASIC has 256 fabric ports split into:
  //   - 128 ports → RDSWs (one per RDSW, not monitored by L1)
  //   - 64 ports → SDSWs (one per SDSW, monitored by L1)
  //   - 64 ports → unassigned (no expected neighbor)
  // The SDSW count per ASIC is limited to 64 to match the hardware's
  // cell profile capacity for fabric link monitoring.
  constexpr int kSdswPortsPerAsic = 64;
  int portIndex = 0;

  for (auto& portCfg : *config.ports()) {
    if (*portCfg.portType() != cfg::PortType::FABRIC_PORT) {
      continue;
    }

    // Position within the current ASIC's 256-port block
    int asicPortIndex = portIndex % kNumFabricPortsPerAsic;
    cfg::PortNeighbor neighbor;
    std::string remotePort = fmt::format("fab1/1/{}", (portIndex % 4) + 1);

    if (asicPortIndex < kNumRdswSwitches) {
      // First 128 ports per ASIC → one RDSW each
      neighbor.remoteSystem() = getRdswName(asicPortIndex + 1);
      neighbor.remotePort() = remotePort;
      portCfg.expectedNeighborReachability() = {neighbor};
    } else if (asicPortIndex < kNumRdswSwitches + kSdswPortsPerAsic) {
      // Next 64 ports per ASIC → one SDSW each
      int sdswIndex = asicPortIndex - kNumRdswSwitches;
      neighbor.remoteSystem() = getSdswName(sdswIndex + 1);
      neighbor.remotePort() = remotePort;
      portCfg.expectedNeighborReachability() = {neighbor};
    }
    portIndex++;
  }
}

void AgentFabricSwitchFabricLinkMonitoringTest::setCmdLineFlagOverrides()
    const {
  AgentHwTest::setCmdLineFlagOverrides();
  FLAGS_hide_fabric_ports = false;
  FLAGS_enable_fabric_link_monitoring = true;
}

void AgentFabricSwitchFabricLinkMonitoringTest::overrideTestEnsembleInitInfo(
    TestEnsembleInitInfo& initInfo) const {
  // Override DSF nodes to ensure the system is detected as a dual stage FDSW.
  // The platform config provides the correct switch IDs (0 and 2), but does
  // not include DSF nodes with fabricLevel=2 needed for isDualStage() to
  // return true during platform init. We add a minimal SDSW dummy node here
  // along with local FDSW nodes with fabricLevel=1 for DUAL_STAGE_L1 detection.
  std::map<int64_t, cfg::DsfNode> dsfNodes;
  dsfNodes[0] =
      utility::makeFabricDsfNode(0, "fdsw001", utility::kL1FabricLevel);
  dsfNodes[2] =
      utility::makeFabricDsfNode(2, "fdsw001", utility::kL1FabricLevel);
  // Dummy SDSW node with fabricLevel=2 to make isDualStage() return true
  dsfNodes[kSdswSwitchIdStart] = utility::makeFabricDsfNode(
      kSdswSwitchIdStart, "sdsw001", utility::kL2FabricLevel);
  initInfo.overrideDsfNodes = std::move(dsfNodes);
}

void AgentFabricSwitchFabricLinkMonitoringTest::runCmd(const std::string& cmd) {
  for (auto fabricSwitchId : getSw()->getSwitchInfoTable().getSwitchIdsOfType(
           cfg::SwitchType::FABRIC)) {
    std::string out;
    getAgentEnsemble()->runDiagCommand(cmd, out, fabricSwitchId);
  }
}

void AgentFabricSwitchFabricLinkMonitoringTest::runCint(
    const std::string& cintStr) {
  folly::test::TemporaryFile file;
  XLOG(INFO) << " Cint file " << file.path().c_str();
  auto written = folly::writeFull(file.fd(), cintStr.c_str(), cintStr.size());
  CHECK_EQ(written, cintStr.size()) << "Failed to write cint file";
  auto cmd = fmt::format("cint {}\n", file.path().c_str());
  runCmd(cmd);
}

TEST_F(
    AgentFabricSwitchFabricLinkMonitoringTest,
    validateFabricLinkMonitoring) {
  auto setup = [this]() { runCint(kFabricLinkMonPuntPathCintStr); };
  auto verify = [this]() {
    auto allFabricPorts = masterLogicalFabricPortIds();
    ASSERT_FALSE(allFabricPorts.empty()) << "No fabric ports found";

    // Get only the L2-connected ports for monitoring verification
    // FabricLinkMonitoringManager only monitors ports connected to L2 switches
    auto l2ConnectedPorts = getL2ConnectedL1FabricPorts(getSw()->getState());
    ASSERT_FALSE(l2ConnectedPorts.empty())
        << "No L2-connected fabric ports found";

    // Convert set to vector for the utility functions
    std::vector<PortID> l2Ports(
        l2ConnectedPorts.begin(), l2ConnectedPorts.end());

    XLOG(DBG2) << "Verifying fabric link monitoring on " << l2Ports.size()
               << " L2-connected ports out of " << allFabricPorts.size()
               << " total fabric ports";

    auto* fabricLinkMonMgr = getSw()->getFabricLinkMonitoringManager();
    auto initialStats = utility::collectFabricLinkMonitoringStats(
        fabricLinkMonMgr, l2Ports, l2Ports.size());

    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(
          utility::allPortsHaveFabricLinkMonitoringCounterIncrements(
              fabricLinkMonMgr, l2Ports, initialStats, l2Ports.size()))
          << "Waiting for fabric_link_monitoring_tx_packets/rx_packets "
          << "counters to increment by at least "
          << utility::kFabricLinkMonitoringMinCounterIncrement << " on all "
          << l2Ports.size() << " L2-connected fabric ports";
      EXPECT_EVENTUALLY_TRUE(
          utility::allPortsHaveFabricLinkMonitoringFsdbStats(
              getSw(), l2Ports, initialStats, l2Ports.size()))
          << "Waiting for all fabric link monitoring stats in hwPortStats and TX/RX progress";
    });
  };
  verifyAcrossWarmBoots(setup, verify);
}
} // namespace facebook::fboss
