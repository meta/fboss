// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <folly/String.h>
#include <folly/Subprocess.h>
#include <gflags/gflags.h>

#include <thrift/lib/cpp2/protocol/DebugProtocol.h>
#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/LldpManager.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/hw/test/LoadBalancerUtils.h"
#include "fboss/agent/hw/test/dataplane_tests/HwTestQosUtils.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortMap.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/link_tests/LinkTest.h"
#include "fboss/agent/test/link_tests/LinkTestUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/QosTestUtils.h"
#include "fboss/lib/CommonUtils.h"
#include "fboss/lib/config/PlatformConfigUtils.h"
#include "fboss/lib/phy/gen-cpp2/phy_types_custom_protocol.h"
#include "fboss/lib/thrift_service_client/ThriftServiceClient.h"

#ifndef IS_OSS
#include "common/base/Proc.h"
#endif

DEFINE_bool(
    list_production_feature,
    false,
    "List production feature needed for every single test.");

namespace {
int kArgc;
char** kArgv;

const std::vector<std::string> l1LinkTestNames = {
    "asicLinkFlap",
    "getTransceivers",
    "opticsTxDisableRandomPorts",
    "opticsTxDisableEnable",
    "testOpticsRemediation",
    "qsfpColdbootAfterAgentUp",
    "fabricLinkHealth",
    "opticsVdmPerformanceMonitoring",
    "iPhyInfoTest",
    "xPhyInfoTest",
    "verifyIphyFecCounters",
    "verifyIphyFecBerCounters",
    "clearIphyInterfaceCounters",
    "resetTransceiverDeadlockTest"};

const std::vector<std::string> l2LinkTestNames = {"trafficRxTx", "ecmpShrink"};

#ifndef IS_OSS
static auto kAgentMemLimit = 9 * 1000 * 1000 * 1000L; // 9GB
#endif
} // namespace

namespace facebook::fboss {

void LinkTest::SetUp() {
  gflags::ParseCommandLineFlags(&kArgc, &kArgv, false);
  if (FLAGS_list_production_feature) {
    printProductionFeatures();
    GTEST_SKIP() << "Skipping this test because list_production_feature is set";
    return;
  }

  AgentTest::SetUp();
  initializeCabledPorts();
  // Wait for all the cabled ports to link up before finishing the setup
  // TODO(joseph5wu) Temporarily increase the timeout to 3mins because current
  // TransceiverStateMachine can only allow handle updates sequentially and for
  // Minipack xphy programming, it will take about 68s to program all 128 ports.
  // Will lower the timeout back to 1min once we can support parallel
  // programming
  waitForAllCabledPorts(true, 60, 5s);
  utility::waitForAllTransceiverStates(true, getCabledTranceivers(), 60, 5s);
  waitForPortStateMachineState(true, 60, 5s);
  XLOG(DBG2) << "Link Test setup ready";
}

void LinkTest::TearDown() {
  if (!FLAGS_list_production_feature) {
    // Expect the qsfp service to be running at the end of the tests
    auto qsfpServiceClient = utils::createQsfpServiceClient();
    EXPECT_EQ(
        facebook::fb303::cpp2::fb_status::ALIVE,
        qsfpServiceClient.get()->sync_getStatus())
        << "QSFP Service no longer alive after the test";
    EXPECT_EQ(
        QsfpServiceRunState::ACTIVE,
        qsfpServiceClient.get()->sync_getQsfpServiceRunState())
        << "QSFP Service run state no longer active after the test";
#ifndef IS_OSS
    // FSDB is not fully supported in OSS
    auto fsdbClient = utils::createFsdbClient();
    EXPECT_EQ(
        facebook::fb303::cpp2::fb_status::ALIVE,
        fsdbClient.get()->sync_getStatus())
        << "FSDB no longer alive after the test";
#endif
    AgentTest::TearDown();
  }
}

void LinkTest::checkAgentMemoryInBounds() const {
#ifndef IS_OSS
  int64_t memUsage = facebook::Proc::getMemoryUsage();
  if (memUsage > kAgentMemLimit) {
    throw FbossError("Agent RSS memory ", memUsage, " above 9GB");
  }
#endif
}

void LinkTest::setCmdLineFlagOverrides() const {
  FLAGS_enable_macsec = true;
  AgentTest::setCmdLineFlagOverrides();
}

void LinkTest::overrideL2LearningConfig(bool swLearning, int ageTimer) {
  auto agentConfig = AgentConfig::fromFile(FLAGS_config);
  cfg::AgentConfig testConfig = agentConfig->thrift;
  if (swLearning) {
    testConfig.sw()->switchSettings()->l2LearningMode() =
        cfg::L2LearningMode::SOFTWARE;
  } else {
    testConfig.sw()->switchSettings()->l2LearningMode() =
        cfg::L2LearningMode::HARDWARE;
  }
  testConfig.sw()->switchSettings()->l2AgeTimerSeconds() = ageTimer;
  auto newAgentConfig = AgentConfig(
      testConfig,
      apache::thrift::SimpleJSONSerializer::serialize<std::string>(testConfig));
  newAgentConfig.dumpConfig(getTestConfigPath());
  FLAGS_config = getTestConfigPath();
  platform()->reloadConfig();
}

void LinkTest::setupTtl0ForwardingEnable() {
  if (!sw()->getHwAsicTable()->isFeatureSupportedOnAnyAsic(
          HwAsic::Feature::SAI_TTL0_PACKET_FORWARD_ENABLE)) {
    // don't configure if not supported
    return;
  }
  auto agentConfig = AgentConfig::fromFile(FLAGS_config);
  auto newAgentConfig =
      utility::setTTL0PacketForwardingEnableConfig(*agentConfig);
  newAgentConfig.dumpConfig(getTestConfigPath());
  FLAGS_config = getTestConfigPath();
  platform()->reloadConfig();
}

// Waits till the link status of the ports in cabledPorts vector reaches
// the expected state
void LinkTest::waitForAllCabledPorts(
    bool up,
    uint32_t retries,
    std::chrono::duration<uint32_t, std::milli> msBetweenRetry) const {
  waitForLinkStatus(getCabledPorts(), up, retries, msBetweenRetry);
}

void LinkTest::waitForPortStateMachineState(
    bool up,
    uint32_t retries,
    std::chrono::duration<uint32_t, std::milli> msBetweenRetry) const {
  utility::waitForPortStateMachineState(
      up, getQsfpServiceManagedPorts(), retries, msBetweenRetry);
}

// Initializes the vector that holds the ports that are expected to be cabled.
// If the expectedLLDPValues in the switch config has an entry, we expect
// that port to take part in the test
void LinkTest::initializeCabledPorts() {
  const auto& platformPorts = sw()->getPlatformMapping()->getPlatformPorts();

  auto swConfig = sw()->getConfig();
  const auto& platformMapping = sw()->getPlatformMapping();
  const auto& chips = platformMapping->getChips();

  // Specifically for ports that qsfp_service should track in Port Manager mode.
  auto managedPortIds =
      utility::getPortIdsWithTransceiverOrXphy(platformPorts, chips);
  std::set<PortID> managedPortSet(managedPortIds.begin(), managedPortIds.end());

  for (const auto& port : *swConfig.ports()) {
    if (!(*port.expectedLLDPValues()).empty()) {
      auto portID = *port.logicalID();
      cabledPorts_.emplace_back(portID);
      if (managedPortSet.count(PortID(portID))) {
        qsfpServiceManagedPorts_.emplace_back(portID);
      }
      if (*port.portType() == cfg::PortType::FABRIC_PORT) {
        cabledFabricPorts_.emplace_back(portID);
      }
      const auto platformPortEntry = platformPorts.find(portID);
      EXPECT_TRUE(platformPortEntry != platformPorts.end())
          << "Can't find port:" << portID << " in PlatformMapping";

      const auto tcvrIds = utility::getTransceiverIds(
          platformPortEntry->second, chips, *port.profileID());
      for (const auto& tcvrId : tcvrIds) {
        cabledTransceivers_.insert(tcvrId);
      }
      if (!tcvrIds.empty()) {
        cabledTransceiverPorts_.emplace_back(portID);
      }
    }
  }
}

std::tuple<std::vector<PortID>, std::string>
LinkTest::getOpticalAndActiveCabledPortsAndNames(
    bool pluggableOnly,
    bool opticalOnly) const {
  std::string portNames;
  std::vector<PortID> ports;
  std::vector<int32_t> transceiverIds;
  for (const auto& port : getCabledTransceiverPorts()) {
    auto portName = getPortName(port);
    auto tcvrId = platform()->getPlatformPort(port)->getTransceiverID().value();
    transceiverIds.push_back(tcvrId);
  }

  auto transceiverInfos = utility::waitForTransceiverInfo(transceiverIds);
  for (const auto& port : getCabledTransceiverPorts()) {
    auto portName = getPortName(port);
    auto tcvrId = platform()->getPlatformPort(port)->getTransceiverID().value();
    auto tcvrInfo = transceiverInfos.find(tcvrId);

    if (tcvrInfo != transceiverInfos.end()) {
      auto tcvrState = *tcvrInfo->second.tcvrState();
      if (TransmitterTechnology::OPTICAL ==
          tcvrState.cable().value_or({}).transmitterTech()) {
        if (!pluggableOnly ||
            (pluggableOnly &&
             (tcvrState.identifier().value_or({}) !=
              TransceiverModuleIdentifier::MINIPHOTON_OBO))) {
          ports.push_back(port);
          portNames += portName + " ";
        } else {
          XLOG(DBG2) << "Transceiver: " << tcvrId + 1 << ", " << portName
                     << ", is on-board optics, skip it";
        }
      } else if (
          tcvrState.cable().value_or({}).mediaTypeEncoding() ==
          MediaTypeEncodings::ACTIVE_CABLES) {
        if (opticalOnly) {
          XLOG(DBG2) << "Transceiver: " << tcvrId + 1 << ", " << portName
                     << ", is an active cable, skip it (opticalOnly)";
        } else {
          ports.push_back(port);
          portNames += portName + " ";
        }
      } else {
        XLOG(DBG2) << "Transceiver: " << tcvrId + 1 << ", " << portName
                   << ", is not optics, skip it";
      }
    } else {
      XLOG(DBG2) << "TransceiverInfo of transceiver: " << tcvrId + 1 << ", "
                 << portName << ", is not present, skip it";
    }
  }

  return {ports, portNames};
}

const std::vector<PortID>& LinkTest::getCabledPorts() const {
  return cabledPorts_;
}

boost::container::flat_set<PortDescriptor>
LinkTest::getSingleVlanOrRoutedCabledPorts() const {
  boost::container::flat_set<PortDescriptor> ecmpPorts;
  auto singleVlanOrRoutedPorts =
      utility::getSingleVlanOrRoutedCabledPorts(sw());
  for (auto port : getCabledPorts()) {
    if (singleVlanOrRoutedPorts.contains(PortDescriptor(port))) {
      ecmpPorts.insert(PortDescriptor(port));
    }
  }
  return ecmpPorts;
}

void LinkTest::programDefaultRoute(
    const boost::container::flat_set<PortDescriptor>& ecmpPorts,
    utility::EcmpSetupTargetedPorts6& ecmp6,
    bool disableTTLDecrement) {
  ASSERT_GT(ecmpPorts.size(), 0);
  sw()->updateStateBlocking("Resolve nhops", [ecmpPorts, &ecmp6](auto state) {
    return ecmp6.resolveNextHops(state, ecmpPorts);
  });
  ecmp6.programRoutes(
      std::make_unique<SwSwitchRouteUpdateWrapper>(sw()->getRouteUpdater()),
      ecmpPorts,
      {Route<folly::IPAddressV6>::Prefix{folly::IPAddressV6(), 0}},
      std::vector<NextHopWeight>(),
      std::nullopt,
      disableTTLDecrement);
}

void LinkTest::programDefaultRoute(
    const boost::container::flat_set<PortDescriptor>& ecmpPorts,
    std::optional<folly::MacAddress> dstMac,
    bool disableTTLDecrement) {
  utility::EcmpSetupTargetedPorts6 ecmp6(
      sw()->getState(),
      sw()->needL2EntryForNeighbor(),
      dstMac,
      RouterID(0),
      false,
      {cfg::PortType::INTERFACE_PORT, cfg::PortType::MANAGEMENT_PORT});
  programDefaultRoute(ecmpPorts, ecmp6, disableTTLDecrement);
}

void LinkTest::createL3DataplaneFlood(
    const boost::container::flat_set<PortDescriptor>& ecmpPorts) {
  auto switchId = scope(ecmpPorts);
  utility::EcmpSetupTargetedPorts6 ecmp6(
      sw()->getState(),
      sw()->needL2EntryForNeighbor(),
      sw()->getLocalMac(switchId),
      RouterID(0),
      false,
      {cfg::PortType::INTERFACE_PORT, cfg::PortType::MANAGEMENT_PORT});
  programDefaultRoute(ecmpPorts, ecmp6, true /* disableTTLDecrement */);
  if (sw()->getHwAsicTable()->isFeatureSupportedOnAnyAsic(
          HwAsic::Feature::PORT_TTL_DECREMENT_DISABLE)) {
    utility::disableTTLDecrementOnPorts(sw(), ecmpPorts);
  }
  auto vlanID = getVlanIDForTx();
  utility::pumpTraffic(
      true,
      utility::getAllocatePktFn(sw()),
      utility::getSendPktFunc(sw()),
      sw()->getLocalMac(switchId),
      vlanID);
  // TODO: Assert that traffic reached a certain rate
  XLOG(DBG2) << "Created L3 Data Plane Flood";
}

bool LinkTest::checkReachabilityOnAllCabledPorts() const {
  auto lldpDb = sw()->getLldpMgr()->getDB();
  for (const auto& port : getCabledPorts()) {
    auto portType = platform()->getPlatformPort(port)->getPortType();
    if (portType == cfg::PortType::INTERFACE_PORT &&
        !lldpDb->getNeighbors(port).size()) {
      XLOG(DBG2) << " No lldp neighbors on : " << getPortName(port);
      return false;
    }
    if (portType == cfg::PortType::FABRIC_PORT) {
      auto fabricReachabilityEntries =
          platform()->getHwSwitch()->getFabricConnectivity();
      auto fabricPortEndPoint = fabricReachabilityEntries.find(port);
      if (fabricPortEndPoint == fabricReachabilityEntries.end() ||
          !*fabricPortEndPoint->second.isAttached()) {
        XLOG(DBG2) << " No fabric end points on : " << getPortName(port);
        return false;
      }
    }
  }
  return true;
}

std::string LinkTest::getPortName(PortID portId) const {
  for (auto portMap : std::as_const(*sw()->getState()->getPorts())) {
    for (auto port : std::as_const(*portMap.second)) {
      if (port.second->getID() == portId) {
        return port.second->getName();
      }
    }
  }
  throw FbossError("No port with ID: ", portId);
}

std::vector<std::string> LinkTest::getPortName(
    const std::vector<PortID>& portIDs) const {
  std::vector<std::string> portNames;
  for (auto port : portIDs) {
    portNames.push_back(getPortName(port));
  }
  return portNames;
}

std::optional<PortID> LinkTest::getPeerPortID(
    PortID portId,
    const std::set<std::pair<PortID, PortID>>& connectedPairs) const {
  for (auto portPair : connectedPairs) {
    if (portPair.first == portId) {
      return portPair.second;
    } else if (portPair.second == portId) {
      return portPair.first;
    }
  }
  return std::nullopt;
}

std::set<std::pair<PortID, PortID>> LinkTest::getConnectedPairs() const {
  waitForLldpOnCabledPorts();
  std::set<std::pair<PortID, PortID>> connectedPairs;
  for (auto cabledPort : cabledPorts_) {
    PortID neighborPort;
    auto portType = platform()->getPlatformPort(cabledPort)->getPortType();
    if (portType == cfg::PortType::FABRIC_PORT) {
      auto fabricReachabilityEntries =
          platform()->getHwSwitch()->getFabricConnectivity();
      auto fabricPortEndpoint = fabricReachabilityEntries.find(cabledPort);
      if (fabricPortEndpoint == fabricReachabilityEntries.end() ||
          !*fabricPortEndpoint->second.isAttached()) {
        XLOG(DBG2) << " No fabric end points on : " << getPortName(cabledPort);
        continue;
      }
      neighborPort =
          PortID(folly::copy(fabricPortEndpoint->second.portId().value())) +
          getRemotePortOffset(platform()->getType());
    } else {
      auto lldpNeighbors =
          sw()->getLldpMgr()->getDB()->getNeighbors(cabledPort);
      if (lldpNeighbors.size() != 1) {
        XLOG(WARN) << "Wrong lldp neighbor size for port "
                   << getPortName(cabledPort) << ", should be 1 but got "
                   << lldpNeighbors.size();
        continue;
      }
      neighborPort = getPortID((*lldpNeighbors.begin())->getPortId());
    }
    // Insert sorted pairs, so that the same pair does not show up twice in
    // the set
    auto connectedPair = cabledPort < neighborPort
        ? std::make_pair(cabledPort, neighborPort)
        : std::make_pair(neighborPort, cabledPort);
    connectedPairs.insert(connectedPair);
  }
  return connectedPairs;
}

/*
 * getConnectedOpticalAndActivePortPairWithFeature
 *
 * Returns the set of connected port pairs with optical link and the optics
 * supporting the given feature. For feature==None, this will return set of
 * connected port pairs using optical links
 *
 * opticalModulesOnly restricts the candidate ports to optical transceivers,
 * excluding active electrical cables.
 */
std::set<std::pair<PortID, PortID>>
LinkTest::getConnectedOpticalAndActivePortPairWithFeature(
    TransceiverFeature feature,
    phy::Side side,
    bool skipLoopback,
    bool opticalModulesOnly) const {
  auto connectedPairs = getConnectedPairs();
  auto ports = std::get<0>(
      getOpticalAndActiveCabledPortsAndNames(false, opticalModulesOnly));

  std::set<std::pair<PortID, PortID>> connectedOpticalPortPairs;
  for (auto connectedPair : connectedPairs) {
    if (std::find(ports.begin(), ports.end(), connectedPair.first) !=
        ports.end()) {
      if (connectedPair.first == connectedPair.second && skipLoopback) {
        continue;
      }
      connectedOpticalPortPairs.insert(connectedPair);
    }
  }

  if (feature == TransceiverFeature::NONE) {
    return connectedOpticalPortPairs;
  }

  std::vector<int32_t> transceiverIds;
  for (auto portPair : connectedOpticalPortPairs) {
    auto tcvrId =
        platform()->getPlatformPort(portPair.first)->getTransceiverID().value();
    transceiverIds.push_back(tcvrId);
    tcvrId = platform()
                 ->getPlatformPort(portPair.second)
                 ->getTransceiverID()
                 .value();
    transceiverIds.push_back(tcvrId);
  }

  auto transceiverInfos = utility::waitForTransceiverInfo(transceiverIds);
  std::set<std::pair<PortID, PortID>> connectedOpticalFeaturedPorts;
  for (auto portPair : connectedOpticalPortPairs) {
    auto tcvrId =
        platform()->getPlatformPort(portPair.first)->getTransceiverID().value();

    auto tcvrInfo = transceiverInfos.find(tcvrId);
    if (tcvrInfo != transceiverInfos.end()) {
      auto tcvrState = *tcvrInfo->second.tcvrState();
      if (tcvrState.diagCapability().value().diagnostics().value()) {
        if (feature == TransceiverFeature::TX_DISABLE &&
            side == phy::Side::LINE) {
          bool txDisCapable =
              tcvrState.diagCapability().value().txOutputControl().value();
          if (txDisCapable) {
            connectedOpticalFeaturedPorts.insert(portPair);
          }
        } else if (
            feature == TransceiverFeature::TX_DISABLE &&
            side == phy::Side::SYSTEM) {
          bool rxDisCapable =
              tcvrState.diagCapability().value().rxOutputControl().value();
          if (rxDisCapable) {
            connectedOpticalFeaturedPorts.insert(portPair);
          }
        } else if (
            feature == TransceiverFeature::LOOPBACK &&
            side == phy::Side::LINE) {
          bool lineLoopCapable =
              tcvrState.diagCapability().value().loopbackLine().value();
          if (lineLoopCapable) {
            connectedOpticalFeaturedPorts.insert(portPair);
          }
        } else if (
            feature == TransceiverFeature::LOOPBACK &&
            side == phy::Side::SYSTEM) {
          bool sysLoopCapable =
              tcvrState.diagCapability().value().loopbackSystem().value();
          if (sysLoopCapable) {
            connectedOpticalFeaturedPorts.insert(portPair);
          }
        } else if (feature == TransceiverFeature::VDM) {
          bool vdmCapable = tcvrState.diagCapability().value().vdm().value();
          if (vdmCapable) {
            connectedOpticalFeaturedPorts.insert(portPair);
          }
        }
      }
    }
  }

  return connectedOpticalFeaturedPorts;
}

void LinkTest::waitForLldpOnCabledPorts(
    uint32_t retries,
    std::chrono::duration<uint32_t, std::milli> msBetweenRetry) const {
  WITH_RETRIES_N_TIMED(retries, msBetweenRetry, {
    ASSERT_EVENTUALLY_TRUE(checkReachabilityOnAllCabledPorts());
  });
}

// Log debug information from IPHY, XPHY and optics
void LinkTest::logLinkDbgMessage(std::vector<PortID>& portIDs) const {
  auto iPhyInfos = sw()->getIPhyInfo(portIDs);
  auto qsfpServiceClient = utils::createQsfpServiceClient();
  auto portNames = getPortNames(portIDs);
  std::map<std::string, phy::PhyInfo> xPhyInfos;
  std::map<int32_t, TransceiverInfo> tcvrInfos;

  try {
    qsfpServiceClient->sync_getInterfacePhyInfo(xPhyInfos, portNames);
  } catch (const std::exception& ex) {
    XLOG(WARN) << "Failed to call qsfp_service getInterfacePhyInfo(). "
               << folly::exceptionStr(ex);
  }

  std::vector<int32_t> tcvrIds;
  for (auto portID : portIDs) {
    tcvrIds.push_back(
        platform()->getPlatformPort(portID)->getTransceiverID().value());
  }

  try {
    qsfpServiceClient->sync_getTransceiverInfo(tcvrInfos, tcvrIds);
  } catch (const std::exception& ex) {
    XLOG(WARN) << "Failed to call qsfp_service getTransceiverInfo(). "
               << folly::exceptionStr(ex);
  }

  for (auto portID : portIDs) {
    auto portName = getPortName(portID);
    XLOG(ERR) << "Debug information for " << portName;
    if (iPhyInfos.contains(portID)) {
      XLOG(ERR) << "IPHY INFO: "
                << apache::thrift::debugString(iPhyInfos[portID]);
    } else {
      XLOG(ERR) << "IPHY info missing for " << portName;
    }

    if (xPhyInfos.contains(portName)) {
      XLOG(ERR) << "XPHY INFO: "
                << apache::thrift::debugString(xPhyInfos[portName]);
    } else {
      XLOG(ERR) << "XPHY info missing for " << portName;
    }

    auto tcvrId =
        platform()->getPlatformPort(portID)->getTransceiverID().value();
    if (tcvrInfos.contains(tcvrId)) {
      XLOG(ERR) << "Transceiver INFO: "
                << apache::thrift::debugString(tcvrInfos[tcvrId]);
    } else {
      XLOG(ERR) << "Transceiver info missing for " << portName;
    }
  }
}

void LinkTest::setLinkState(bool enable, std::vector<PortID>& portIds) {
  for (const auto& port : portIds) {
    setPortStatus(port, enable);
  }
  EXPECT_NO_THROW(
      waitForLinkStatus(portIds, enable, 60, std::chrono::milliseconds(1000)););
}

std::vector<std::pair<PortID, PortID>> LinkTest::getPortPairsForFecErrInj()
    const {
  auto connectedPairs = getConnectedPairs();
  std::unordered_set<phy::FecMode> supportedFecs = {
      phy::FecMode::RS528, phy::FecMode::RS544, phy::FecMode::RS544_2N};
  std::vector<std::pair<PortID, PortID>> supportedPorts;
  for (const auto& [port1, port2] : connectedPairs) {
    auto fecPort1 = platform()->getHwSwitch()->getPortFECMode(port1);
    auto fecPort2 = platform()->getHwSwitch()->getPortFECMode(port2);
    if (fecPort1 != fecPort2) {
      throw FbossError(
          "FEC different on both ends of the link: ", fecPort1, fecPort2);
    }
    if (supportedFecs.contains(fecPort1)) {
      supportedPorts.emplace_back(port1, port2);
    }
  }
  return supportedPorts;
}

std::vector<link_test_production_features::LinkTestProductionFeature>
LinkTest::getProductionFeatures() const {
  const std::string testName =
      testing::UnitTest::GetInstance()->current_test_info()->name();

  if (std::find(l1LinkTestNames.begin(), l1LinkTestNames.end(), testName) !=
      l1LinkTestNames.end()) {
    return {
        link_test_production_features::LinkTestProductionFeature::L1_LINK_TEST};
  } else if (
      std::find(l2LinkTestNames.begin(), l2LinkTestNames.end(), testName) !=
      l2LinkTestNames.end()) {
    return {
        link_test_production_features::LinkTestProductionFeature::L2_LINK_TEST};
  } else {
    throw std::runtime_error(
        "Test type (L1/L2) not specified for this test case");
  }
}

void LinkTest::printProductionFeatures() const {
  std::vector<std::string> supportedFeatures;
  for (const auto& feature : getProductionFeatures()) {
    supportedFeatures.push_back(apache::thrift::util::enumNameSafe(feature));
  }
  if (supportedFeatures.size() == 0) {
    throw std::runtime_error("No production features found for this Link Test");
  }
  std::cout << "Feature List: " << folly::join(",", supportedFeatures) << "\n";
}

int linkTestMain(
    int argc,
    char** argv,
    PlatformInitFn initPlatformFn,
    std::optional<cfg::StreamType> streamType) {
  ::testing::InitGoogleTest(&argc, argv);

  kArgc = argc;
  kArgv = argv;
  initAgentTest(argc, argv, initPlatformFn, streamType);
  return RUN_ALL_TESTS();
}

} // namespace facebook::fboss
