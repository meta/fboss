// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <fmt/format.h>
#include <folly/String.h>
#include <folly/Subprocess.h>
#include <folly/system/EnvUtil.h>
#include <gflags/gflags.h>

#include <thrift/lib/cpp/util/EnumUtils.h>
#include <thrift/lib/cpp2/protocol/DebugProtocol.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/LldpManager.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortMap.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/link_tests/AgentEnsembleLinkTest.h"
#include "fboss/agent/test/link_tests/LinkTestUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/HyperPortTestUtils.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"
#include "fboss/agent/test/utils/QosTestUtils.h"
#include "fboss/lib/CommonFileUtils.h"
#include "fboss/lib/CommonUtils.h"
#include "fboss/lib/config/PlatformConfigUtils.h"
#include "fboss/lib/if/LinkQsfpTestPortInfoUtils.h"
#include "fboss/lib/if/gen-cpp2/link_qsfp_test_port_info_types.h"
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
    "opticsRxOutputDisableEnable",
    "testOpticsRemediation",
    "qsfpColdbootAfterAgentUp",
    "fabricLinkHealth",
    "opticsVdmPerformanceMonitoring",
    "iPhyInfoTest",
    "xPhyInfoTest",
    "verifyIphyFecCounters",
    "verifyIphyFecBerCounters",
    "clearIphyInterfaceCounters"};

const std::vector<std::string> l2LinkTestNames = {"trafficRxTx", "ecmpShrink"};

#ifndef IS_OSS
long swAgentMemThreshold(facebook::fboss::PlatformType platform) {
  switch (platform) {
    case facebook::fboss::PlatformType::PLATFORM_WEDGE100:
    case facebook::fboss::PlatformType::PLATFORM_WEDGE400:
    case facebook::fboss::PlatformType::PLATFORM_DARWIN:
    case facebook::fboss::PlatformType::PLATFORM_DARWIN48V:
    case facebook::fboss::PlatformType::PLATFORM_MINIPACK:
    case facebook::fboss::PlatformType::PLATFORM_YAMP:
      return 3 * 1000 * 1000 * 1000L; // 3GB
    case facebook::fboss::PlatformType::PLATFORM_MORGAN800CC:
      return 5 * 1000 * 1000 * 1000L; // 5GB
    case facebook::fboss::PlatformType::PLATFORM_MERU800BIA:
    case facebook::fboss::PlatformType::PLATFORM_ICECUBE800BC:
    case facebook::fboss::PlatformType::PLATFORM_ICECUBE800BANW:
      return 6 * 1000 * 1000 * 1000L; // 6GB
    default:
      return 4 * 1000 * 1000 * 1000L; // 4GB
  }
}
#endif
} // namespace

namespace facebook::fboss {

void AgentEnsembleLinkTest::SetUp() {
  gflags::ParseCommandLineFlags(&kArgc, &kArgv, false);
  if (FLAGS_list_production_feature) {
    printProductionFeatures();
    GTEST_SKIP() << "Skipping this test because list_production_feature is set";
  }
  AgentEnsembleTest::SetUp();
  initializeCabledPorts();
  // Wait for all the cabled ports to link up before finishing the setup
  waitForAllCabledPorts(true, 60, 5s);
  utility::waitForAllTransceiverStates(true, getCabledTranceivers(), 60, 5s);
  waitForPortStateMachineState(true, 60, 5s);

  XLOG(DBG2) << "Multi Switch Link Test setup ready";
}

void AgentEnsembleLinkTest::TearDown() {
  if (!FLAGS_list_production_feature) {
    // Expect the qsfp service to be running at the end of the tests
    try {
      auto qsfpServiceClient = utils::createQsfpServiceClient();
      EXPECT_EQ(
          facebook::fb303::cpp2::fb_status::ALIVE,
          qsfpServiceClient.get()->sync_getStatus())
          << "QSFP Service no longer alive after the test";
      EXPECT_EQ(
          QsfpServiceRunState::ACTIVE,
          qsfpServiceClient.get()->sync_getQsfpServiceRunState())
          << "QSFP Service run state no longer active after the test";

      // Dump the I2C Logs at the end of the test.
      dumpTransceiverI2cLogs(*qsfpServiceClient);
    } catch (const std::exception& ex) {
      XLOG(ERR) << "Failed to call qsfp_service getStatus(). " << ex.what();
    }

    // NOTE: per-port metadata is dumped via the dumpTestMetadata() override,
    // invoked from tearDownAgentEnsemble() so it runs on both the cold-boot
    // (setup_for_warmboot) and warm-boot phases. TODO: Add a hard check that
    // every test registers tested ports once all tests are instrumented.

#ifndef IS_OSS
    try {
      // FSDB is not fully supported in OSS
      auto fsdbClient = utils::createFsdbClient();
      EXPECT_EQ(
          facebook::fb303::cpp2::fb_status::ALIVE,
          fsdbClient.get()->sync_getStatus())
          << "FSDB no longer alive after the test";
    } catch (const std::exception& ex) {
      XLOG(ERR) << "Failed to call fsdb getStatus(). " << ex.what();
    }
#endif
  }
  AgentEnsembleTest::TearDown();
}

std::vector<std::string>
AgentEnsembleLinkTest::getPrimaryPortNamesCabledPorts() {
  const auto& ports = getCabledPorts();
  std::unordered_set<std::string> uniqueSet;
  std::vector<std::string> result_list;
  for (const auto& port : ports) {
    auto portName =
        getSw()->getPlatformMapping()->getPortNameByPortId(port).value_or("");
    if (portName.empty()) {
      continue;
    }
    size_t firstSlash = portName.find('/');
    size_t secondSlash = portName.find('/', firstSlash + 1);
    if (firstSlash == std::string::npos || secondSlash == std::string::npos) {
      // Skip wrong input
      continue;
    }
    std::string firstTwoParams = portName.substr(0, secondSlash);
    if (uniqueSet.find(firstTwoParams) == uniqueSet.end()) {
      uniqueSet.insert(firstTwoParams);
      result_list.push_back(portName);
    }
  }
  return result_list;
}

void AgentEnsembleLinkTest::dumpTransceiverI2cLogs(
    apache::thrift::Client<facebook::fboss::QsfpService>& qsfpServiceClient) {
  auto ports = getPrimaryPortNamesCabledPorts();
  for (auto& port : ports) {
    try {
      qsfpServiceClient.sync_dumpTransceiverI2cLog(port);
    } catch (const std::exception& ex) {
      // Passive cables may not have EEPROM, ignore the error if the i2c log
      // does not exist.
      XLOG(ERR) << "Failed to dump i2c log for port " << port << " Exception "
                << ex.what();
    }
  }
}

void AgentEnsembleLinkTest::checkAgentMemoryInBounds() const {
#ifndef IS_OSS
  int64_t memUsage = facebook::Proc::getMemoryUsage();
  auto memUsageThreshold = swAgentMemThreshold(getSw()->getPlatformType());
  if (memUsage > memUsageThreshold) {
    throw FbossError(
        "SW Agent RSS memory ",
        memUsage,
        " above expected ",
        memUsageThreshold);
  }
#endif
}

void AgentEnsembleLinkTest::setCmdLineFlagOverrides() const {
  FLAGS_enable_macsec = true;
  AgentEnsembleTest::setCmdLineFlagOverrides();
}

void AgentEnsembleLinkTest::overrideL2LearningConfig(
    bool swLearning,
    int ageTimer) {
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
}

void AgentEnsembleLinkTest::setupTtl0ForwardingEnable() {
  auto agentConfig = AgentConfig::fromFile(FLAGS_config);
  auto newAgentConfig =
      utility::setTTL0PacketForwardingEnableConfig(*agentConfig);
  newAgentConfig.dumpConfig(getTestConfigPath());
  FLAGS_config = getTestConfigPath();
}

// Waits till the link status of the ports in cabledPorts vector reaches
// the expected state
void AgentEnsembleLinkTest::waitForAllCabledPorts(
    bool up,
    uint32_t retries,
    std::chrono::duration<uint32_t, std::milli> msBetweenRetry) const {
  waitForLinkStatus(getCabledPorts(), up, retries, msBetweenRetry);
}

void AgentEnsembleLinkTest::waitForPortStateMachineState(
    bool up,
    uint32_t retries,
    std::chrono::duration<uint32_t, std::milli> msBetweenRetry) const {
  utility::waitForPortStateMachineState(
      up, getQsfpServiceManagedPorts(), retries, msBetweenRetry);
}

// Initializes the vector that holds the ports that are expected to be cabled.
// If the expectedLLDPValues in the switch config has an entry, we expect
// that port to take part in the test
void AgentEnsembleLinkTest::initializeCabledPorts() {
  // Reset first so this can be re-run after a config change (e.g. a speed
  // change that subsumes or adds ports) to recompute the cabled port set from
  // the currently applied config.
  cabledPorts_.clear();
  cabledFabricPorts_.clear();
  cabledTransceivers_.clear();
  cabledTransceiverPorts_.clear();
  qsfpServiceManagedPorts_.clear();

  const auto& platformPorts = getSw()->getPlatformMapping()->getPlatformPorts();

  auto swConfig = getSw()->getConfig();
  const auto& platformMapping = getSw()->getPlatformMapping();
  const auto& chips = platformMapping->getChips();

  // Specifically for ports that qsfp_service should track in Port Manager mode.
  auto managedPortIds =
      utility::getPortIdsWithTransceiverOrXphy(platformPorts, chips);
  std::set<PortID> managedPortSet(managedPortIds.begin(), managedPortIds.end());

  for (const auto& port : *swConfig.ports()) {
    // Skip disabled ports. A speed change can subsume ports (the merged
    // higher-speed port disables its subsidiary ports); such ports are no
    // longer present in the SwitchState, so they must not be treated as cabled.
    if (*port.state() == cfg::PortState::DISABLED) {
      continue;
    }
    if (!(*port.expectedLLDPValues()).empty() ||
        !(*port.expectedNeighborReachability()).empty()) {
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

void AgentEnsembleLinkTest::reinitializeCabledPorts() {
  initializeCabledPorts();
}

std::tuple<std::vector<PortID>, std::string>
AgentEnsembleLinkTest::getOpticalAndActiveCabledPortsAndNames(
    bool pluggableOnly,
    bool opticalOnly) const {
  std::string portNames;
  std::vector<PortID> ports;
  std::vector<int32_t> transceiverIds;
  for (const auto& port : getCabledTransceiverPorts()) {
    auto portName = getPortName(port);
    // TODO to find equivalent to getPlatformPort
    auto tcvrId =
        getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(port);
    transceiverIds.push_back(tcvrId);
  }

  auto transceiverInfos = utility::waitForTransceiverInfo(transceiverIds);
  for (const auto& port : getCabledTransceiverPorts()) {
    auto portName = getPortName(port);
    auto tcvrId =
        getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(port);
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

const std::vector<PortID>& AgentEnsembleLinkTest::getCabledPorts() const {
  return cabledPorts_;
}

const std::vector<PortID>& AgentEnsembleLinkTest::getCabledTransceiverPorts()
    const {
  return cabledTransceiverPorts_;
}

boost::container::flat_set<PortDescriptor>
AgentEnsembleLinkTest::getSingleVlanOrRoutedCabledPorts(
    std::optional<SwitchID> switchId) const {
  boost::container::flat_set<PortDescriptor> ecmpPorts;
  auto singleVlanOrRoutedPorts =
      utility::getSingleVlanOrRoutedCabledPorts(getSw());
  for (auto port : getCabledPorts()) {
    // Only L3 interface ports can carry the ECMP group. The VLAN-interface
    // branch of utility::getSingleVlanOrRoutedCabledPorts() does not filter on
    // port type, so a management port that is the sole member of its VLAN
    // otherwise lands here with no interface to resolve a nexthop against.
    auto portType = getSw()->getState()->getPort(port)->getPortType();
    if (portType != cfg::PortType::INTERFACE_PORT &&
        portType != cfg::PortType::HYPER_PORT) {
      continue;
    }
    auto matcher = getSw()->getScopeResolver()->scope(
        getSw()->getState(), PortDescriptor(port));
    SwitchID portSwitchId = matcher.switchId();
    if (singleVlanOrRoutedPorts.find(PortDescriptor(port)) !=
            singleVlanOrRoutedPorts.end() &&
        (switchId == std::nullopt || portSwitchId == switchId.value())) {
      ecmpPorts.insert(PortDescriptor(port));
    }
  }
  std::vector<PortDescriptor> hyperPortDescriptors;
  for (auto hyperPortId : utility::getHyperPorts(getSw()->getState())) {
    if (getSw()->getState()->getPort(hyperPortId)->isPortUp()) {
      hyperPortDescriptors.emplace_back(hyperPortId);
      XLOG(DBG2) << "add hyper port " << hyperPortId << " to ecmp ports";
    }
  }
  ecmpPorts.insert(hyperPortDescriptors.begin(), hyperPortDescriptors.end());
  return ecmpPorts;
}

void AgentEnsembleLinkTest::programDefaultRoute(
    const boost::container::flat_set<PortDescriptor>& ecmpPorts,
    utility::EcmpSetupTargetedPorts6& ecmp6) {
  ASSERT_GT(ecmpPorts.size(), 0);
  getSw()->updateStateBlocking(
      "Resolve nhops", [ecmpPorts, &ecmp6](auto state) {
        return ecmp6.resolveNextHops(state, ecmpPorts);
      });
  ecmp6.programRoutes(
      std::make_unique<SwSwitchRouteUpdateWrapper>(getSw()->getRouteUpdater()),
      ecmpPorts);
}

void AgentEnsembleLinkTest::programDefaultRoute(
    const boost::container::flat_set<PortDescriptor>& ecmpPorts,
    std::optional<folly::MacAddress> dstMac) {
  utility::EcmpSetupTargetedPorts6 ecmp6(
      getSw()->getState(),
      getSw()->needL2EntryForNeighbor(),
      dstMac,
      RouterID(0),
      false,
      {cfg::PortType::INTERFACE_PORT, cfg::PortType::HYPER_PORT});
  programDefaultRoute(ecmpPorts, ecmp6);
}

// only use when NEXTHOP_TTL_DECREMENT_DISABLE is supported
// This allows a single state update to both program routes and configure nhops
void AgentEnsembleLinkTest::programDefaultRouteWithDisableTTLDecrement(
    const boost::container::flat_set<PortDescriptor>& ecmpPorts,
    utility::EcmpSetupTargetedPorts6& ecmp6) {
  ASSERT_GT(ecmpPorts.size(), 0);
  getSw()->updateStateBlocking(
      "Resolve nhops", [ecmpPorts, &ecmp6](auto state) {
        return ecmp6.resolveNextHops(state, ecmpPorts);
      });
  ecmp6.programRoutes(
      std::make_unique<SwSwitchRouteUpdateWrapper>(getSw()->getRouteUpdater()),
      ecmpPorts,
      {Route<folly::IPAddressV6>::Prefix{folly::IPAddressV6(), 0}},
      std::vector<NextHopWeight>(),
      std::nullopt,
      true /* disableTTLDecrement */);
}

void AgentEnsembleLinkTest::createL3DataplaneFlood(
    const boost::container::flat_set<PortDescriptor>& ecmpPorts) {
  auto switchId = scope(ecmpPorts);
  utility::EcmpSetupTargetedPorts6 ecmp6(
      getSw()->getState(),
      getSw()->needL2EntryForNeighbor(),
      getSw()->getLocalMac(switchId),
      RouterID(0),
      false,
      {cfg::PortType::INTERFACE_PORT, cfg::PortType::HYPER_PORT});
  if (getSw()->getHwAsicTable()->isFeatureSupported(
          switchId, HwAsic::Feature::NEXTHOP_TTL_DECREMENT_DISABLE)) {
    programDefaultRouteWithDisableTTLDecrement(ecmpPorts, ecmp6);
  } else {
    programDefaultRoute(ecmpPorts, ecmp6);
    utility::disableTTLDecrements(getSw(), ecmpPorts);
  }
  auto vlanID = getAgentEnsemble()->getVlanIDForTx();
  utility::pumpTraffic(
      true,
      utility::getAllocatePktFn(getSw()),
      utility::getSendPktFunc(getSw(), switchId),
      getSw()->getLocalMac(switchId),
      vlanID);
  // TODO: Assert that traffic reached a certain rate
  XLOG(DBG2) << "Created L3 Data Plane Flood";
}

bool AgentEnsembleLinkTest::checkReachabilityOnAllCabledPorts() const {
  auto lldpDb = getSw()->getLldpMgr()->getDB();
  for (const auto& port : getCabledPorts()) {
    auto portType =
        getProgrammedState()->getPorts()->getNodeIf(port)->getPortType();
    if (portType == cfg::PortType::INTERFACE_PORT &&
        !lldpDb->getNeighbors(port).size()) {
      XLOG(DBG2) << " No lldp neighbors on : " << getPortName(port);
      return false;
    }
    if (portType == cfg::PortType::FABRIC_PORT) {
      auto switchId = getSw()->getScopeResolver()->scope(port).switchId();
      // TODO(Elangovan) does the reachability have to be retried?
      auto fabricReachabilityEntries = getFabricConnectivity(switchId);
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

std::optional<PortID> AgentEnsembleLinkTest::getPeerPortID(
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

std::set<std::pair<PortID, PortID>> AgentEnsembleLinkTest::getConnectedPairs()
    const {
  waitForLldpOnCabledPorts();
  std::set<std::pair<PortID, PortID>> connectedPairs;
  for (auto cabledPort : cabledPorts_) {
    PortID neighborPort;
    auto portType =
        getProgrammedState()->getPorts()->getNodeIf(cabledPort)->getPortType();
    if (portType == cfg::PortType::FABRIC_PORT) {
      auto switchId = getSw()->getScopeResolver()->scope(cabledPort).switchId();
      auto fabricReachabilityEntries = getFabricConnectivity(switchId);
      auto fabricPortEndpoint = fabricReachabilityEntries.find(cabledPort);
      if (fabricPortEndpoint == fabricReachabilityEntries.end() ||
          !*fabricPortEndpoint->second.isAttached()) {
        XLOG(DBG2) << " No fabric end points on : " << getPortName(cabledPort);
        continue;
      }
      auto portName = fabricPortEndpoint->second.portName();
      if (!portName) {
        XLOG(DBG2) << " No neighbor port name found on : "
                   << getPortName(cabledPort);
        continue;
      }
      neighborPort = getPortID(portName.value());
      XLOG(DBG2) << "Get neighbor port " << portName.value()
                 << " and neighbor port id " << neighborPort
                 << " on : " << getPortName(cabledPort);
    } else {
      auto lldpNeighbors =
          getSw()->getLldpMgr()->getDB()->getNeighbors(cabledPort);
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
AgentEnsembleLinkTest::getConnectedOpticalAndActivePortPairWithFeature(
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
    auto tcvrId = getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(
        portPair.first);

    transceiverIds.push_back(tcvrId);
    tcvrId = getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(
        portPair.second);
    transceiverIds.push_back(tcvrId);
  }

  auto transceiverInfos = utility::waitForTransceiverInfo(transceiverIds);
  std::set<std::pair<PortID, PortID>> connectedOpticalFeaturedPorts;
  for (auto portPair : connectedOpticalPortPairs) {
    auto tcvrId = getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(
        portPair.first);

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

void AgentEnsembleLinkTest::waitForLldpOnCabledPorts(
    uint32_t retries,
    std::chrono::duration<uint32_t, std::milli> msBetweenRetry) const {
  WITH_RETRIES_N_TIMED(retries, msBetweenRetry, {
    ASSERT_EVENTUALLY_TRUE(checkReachabilityOnAllCabledPorts());
  });
}

// Log debug information from IPHY, XPHY and optics
void AgentEnsembleLinkTest::logLinkDbgMessage(
    std::vector<PortID>& portIDs) const {
  auto iPhyInfos = getSw()->getIPhyInfo(portIDs);
  auto qsfpServiceClient = utils::createQsfpServiceClient();
  auto portNames = getPortNames(portIDs);
  std::map<std::string, phy::PhyInfo> xPhyInfos;
  std::map<int32_t, TransceiverInfo> tcvrInfos;

  auto cabledTcvrPorts = getCabledTransceiverPorts();
  std::unordered_set<PortID> cabledTcvrPortSet(
      cabledTcvrPorts.begin(), cabledTcvrPorts.end());

  try {
    qsfpServiceClient->sync_getInterfacePhyInfo(xPhyInfos, portNames);
  } catch (const std::exception& ex) {
    XLOG(WARN) << "Failed to call qsfp_service getInterfacePhyInfo(). "
               << folly::exceptionStr(ex);
  }

  std::vector<int32_t> tcvrIds;
  for (auto portID : portIDs) {
    if (!cabledTcvrPortSet.contains(portID)) {
      continue;
    }
    tcvrIds.push_back(
        getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(portID));
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
    if (iPhyInfos.find(portID) != iPhyInfos.end()) {
      XLOG(ERR) << "IPHY INFO: "
                << apache::thrift::debugString(iPhyInfos[portID]);
    } else {
      XLOG(ERR) << "IPHY info missing for " << portName;
    }

    if (xPhyInfos.find(portName) != xPhyInfos.end()) {
      XLOG(ERR) << "XPHY INFO: "
                << apache::thrift::debugString(xPhyInfos[portName]);
    } else {
      XLOG(ERR) << "XPHY info missing for " << portName;
    }

    if (!cabledTcvrPortSet.contains(portID)) {
      continue;
    }
    auto tcvrId =
        getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(portID);
    if (tcvrInfos.find(tcvrId) != tcvrInfos.end()) {
      XLOG(ERR) << "Transceiver INFO: "
                << apache::thrift::debugString(tcvrInfos[tcvrId]);
    } else {
      XLOG(ERR) << "Transceiver info missing for " << portName;
    }
  }
}

void AgentEnsembleLinkTest::setLinkState(
    bool enable,
    std::vector<PortID>& portIds) {
  for (const auto& port : portIds) {
    setPortStatus(port, enable);
  }
  EXPECT_NO_THROW(
      waitForLinkStatus(portIds, enable, 60, std::chrono::milliseconds(1000)););
}

phy::FecMode AgentEnsembleLinkTest::getPortFECMode(PortID portId) const {
  auto port = getSw()->getState()->getPorts()->getNodeIf(portId);
  auto matcher =
      PlatformPortProfileConfigMatcher(port->getProfileID(), port->getID());
  auto profile = getSw()->getPlatformMapping()->getPortProfileConfig(matcher);

  if (profile.has_value()) {
    return profile->iphy()->fec().value();
  } else {
    throw FbossError("No profile found for port: ", portId);
  }
}

std::vector<std::pair<PortID, PortID>>
AgentEnsembleLinkTest::getPortPairsForFecErrInj() const {
  auto connectedPairs = getConnectedPairs();
  std::unordered_set<phy::FecMode> supportedFecs = {
      phy::FecMode::RS528, phy::FecMode::RS544, phy::FecMode::RS544_2N};
  std::vector<std::pair<PortID, PortID>> supportedPorts;
  for (const auto& [port1, port2] : connectedPairs) {
    if (getProgrammedState()->getPorts()->getNodeIf(port1)->getPortType() ==
            cfg::PortType::MANAGEMENT_PORT ||
        getProgrammedState()->getPorts()->getNodeIf(port2)->getPortType() ==
            cfg::PortType::MANAGEMENT_PORT) {
      continue;
    }
    auto fecPort1 = getPortFECMode(port1);
    auto fecPort2 = getPortFECMode(port2);
    if (fecPort1 != fecPort2) {
      throw FbossError(
          "FEC different on both ends of the link: ", fecPort1, fecPort2);
    }
    if (supportedFecs.find(fecPort1) != supportedFecs.end()) {
      supportedPorts.emplace_back(port1, port2);
    }
  }
  return supportedPorts;
}

void AgentEnsembleLinkTest::setForceTrafficOverFabric(bool force) {
  applyNewState([&](const std::shared_ptr<SwitchState>& in) {
    auto out = in->clone();
    for (const auto& [_, switchSetting] :
         std::as_const(*out->getSwitchSettings())) {
      auto newSwitchSettings = switchSetting->modify(&out);
      newSwitchSettings->setForceTrafficOverFabric(force);
    }
    return out;
  });
}

namespace {
// Platform features individual tests on this fixture require. A test absent
// from this map declares none, so it runs on every platform.
const std::map<
    std::string,
    std::vector<link_test_production_features::LinkTestProductionFeature>>
    kTestPlatformFeatures = {
        {"xPhyInfoTest",
         {link_test_production_features::LinkTestProductionFeature::XPHY}},
        {"fabricLinkHealth",
         {link_test_production_features::LinkTestProductionFeature::
              DSF_INTERFACE_NODE}},
        {"opticsTxDisableRandomPorts",
         {link_test_production_features::LinkTestProductionFeature::
              ACTIVE_TRANSCEIVER}},
        {"opticsTxDisableEnable",
         {link_test_production_features::LinkTestProductionFeature::
              ACTIVE_TRANSCEIVER}},
        {"opticsRxOutputDisableEnable",
         {link_test_production_features::LinkTestProductionFeature::
              ACTIVE_TRANSCEIVER}},
        {"testOpticsRemediation",
         {link_test_production_features::LinkTestProductionFeature::
              ACTIVE_TRANSCEIVER,
          link_test_production_features::LinkTestProductionFeature::
              REMEDIATION}},
        {"opticsVdmPerformanceMonitoring",
         {link_test_production_features::LinkTestProductionFeature::
              ACTIVE_TRANSCEIVER,
          link_test_production_features::LinkTestProductionFeature::VDM}},
};
} // namespace

std::vector<link_test_production_features::LinkTestProductionFeature>
AgentEnsembleLinkTest::getProductionFeatures() const {
  const std::string testName =
      testing::UnitTest::GetInstance()->current_test_info()->name();

  std::vector<link_test_production_features::LinkTestProductionFeature>
      features;
  if (std::find(l1LinkTestNames.begin(), l1LinkTestNames.end(), testName) !=
      l1LinkTestNames.end()) {
    features.push_back(
        link_test_production_features::LinkTestProductionFeature::L1_LINK_TEST);
  } else if (
      std::find(l2LinkTestNames.begin(), l2LinkTestNames.end(), testName) !=
      l2LinkTestNames.end()) {
    features.push_back(
        link_test_production_features::LinkTestProductionFeature::L2_LINK_TEST);
  } else {
    throw std::runtime_error(
        "Test type (L1/L2) not specified for this test case");
  }

  if (auto it = kTestPlatformFeatures.find(testName);
      it != kTestPlatformFeatures.end()) {
    features.insert(features.end(), it->second.begin(), it->second.end());
  }
  return features;
}

void AgentEnsembleLinkTest::printProductionFeatures() const {
  std::vector<std::string> supportedFeatures;
  for (const auto& feature : getProductionFeatures()) {
    supportedFeatures.push_back(apache::thrift::util::enumNameSafe(feature));
  }
  if (supportedFeatures.size() == 0) {
    throw std::runtime_error("No production features found for this Link Test");
  }
  std::cout << "Feature List: " << folly::join(",", supportedFeatures) << "\n";
}

void AgentEnsembleLinkTest::addTestedPort(const PortID& portId) {
  testedPorts_.insert(portId);
}

void AgentEnsembleLinkTest::addTestedPorts(const std::vector<PortID>& portIds) {
  testedPorts_.insert(portIds.begin(), portIds.end());
}

void AgentEnsembleLinkTest::addTestMetadata(
    const PortID& portId,
    const std::string& key,
    const std::string& value) {
  perPortMetadata_[portId][key] = value;
}

void AgentEnsembleLinkTest::addVerifiedProductionFeatures(
    const std::vector<link_test_production_features::LinkTestProductionFeature>&
        features) {
  verifiedProductionFeatures_.insert(
      verifiedProductionFeatures_.end(), features.begin(), features.end());
}

void AgentEnsembleLinkTest::dumpTestMetadata() {
  if (testedPorts_.empty()) {
    return;
  }
  try {
    auto testInfo = testing::UnitTest::GetInstance()->current_test_info();
    auto testName =
        fmt::format("{}.{}", testInfo->test_suite_name(), testInfo->name());

    std::vector<std::string> featureNames;
    featureNames.reserve(verifiedProductionFeatures_.size());
    for (const auto& feature : verifiedProductionFeatures_) {
      featureNames.push_back(apache::thrift::util::enumNameSafe(feature));
    }

    auto cabledTcvrPorts = getCabledTransceiverPorts();
    std::unordered_set<PortID> cabledTcvrPortSet(
        cabledTcvrPorts.begin(), cabledTcvrPorts.end());

    std::vector<int32_t> tcvrIds;
    std::map<PortID, int32_t> portToTcvrId;
    for (const auto& portId : testedPorts_) {
      if (!cabledTcvrPortSet.contains(portId)) {
        continue;
      }
      auto tcvrId =
          getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(portId);
      tcvrIds.push_back(tcvrId);
      portToTcvrId[portId] = tcvrId;
    }

    std::map<int32_t, TransceiverInfo> tcvrInfos;
    if (!tcvrIds.empty()) {
      // Best effort: if qsfp_service is unreachable we still dump port level
      // info (test name, port name, FEC mode) below.
      try {
        auto qsfpServiceClient = utils::createQsfpServiceClient();
        qsfpServiceClient->sync_getTransceiverInfo(tcvrInfos, tcvrIds);
      } catch (const std::exception& ex) {
        XLOG(WARN) << "Failed to get transceiver info for link test port info: "
                   << folly::exceptionStr(ex);
      }
    }

    std::vector<std::string> jsonLines;
    for (auto portId : testedPorts_) {
      LinkQsfpTestPortInfo portInfo;
      auto portName = getPortName(portId);
      portInfo.testName() = testName;
      portInfo.portId() = static_cast<int32_t>(portId);
      portInfo.portName() = portName;

      try {
        portInfo.fecMode() = getPortFECMode(portId);
      } catch (const std::exception&) {
        // Leave fecMode unset if no profile found for this port.
      }

      if (auto it = portToTcvrId.find(portId); it != portToTcvrId.end()) {
        portInfo.transceiverId() = it->second;
        if (auto tcvrIt = tcvrInfos.find(it->second);
            tcvrIt != tcvrInfos.end()) {
          const auto& tcvrState = *tcvrIt->second.tcvrState();
          utility::populateTransceiverInfoFields(portInfo, tcvrState, portName);
        }
      }

      portInfo.verifiedProductionFeatures() = featureNames;

      if (auto it = perPortMetadata_.find(portId);
          it != perPortMetadata_.end()) {
        portInfo.extraMetadata() = it->second;
      }

      jsonLines.push_back(
          apache::thrift::SimpleJSONSerializer::serialize<std::string>(
              portInfo));
    }

    auto content = folly::join("\n", jsonLines);
    if (writeSysfs(utility::kLinkQsfpTestPortInfoForScuba, content)) {
      XLOG(DBG2) << "Dumped link test port info for " << testedPorts_.size()
                 << " ports to " << utility::kLinkQsfpTestPortInfoForScuba;
    } else {
      XLOG(ERR) << "Failed to write link test port info for "
                << testedPorts_.size() << " ports to "
                << utility::kLinkQsfpTestPortInfoForScuba;
    }
  } catch (const std::exception& ex) {
    XLOG(WARN) << "Failed to dump link test port info: "
               << folly::exceptionStr(ex);
  }
  testedPorts_.clear();
  perPortMetadata_.clear();
  verifiedProductionFeatures_.clear();
}

int agentEnsembleLinkTestMain(
    int argc,
    char** argv,
    PlatformInitFn initPlatformFn,
    std::optional<cfg::StreamType> streamType) {
  ::testing::InitGoogleTest(&argc, argv);
  kArgc = argc;
  kArgv = argv;
  initAgentEnsembleTest(argc, argv, initPlatformFn, streamType);
  auto env = folly::experimental::EnvironmentState::fromCurrentEnvironment();
  std::vector<std::string> environment;
  for (const auto& [key, value] : *env) {
    environment.emplace_back(folly::to<std::string>(key, "=", value));
  }
  XLOG(INFO) << "Environment Variables: " << folly::join(" ", environment);
  return RUN_ALL_TESTS();
}

} // namespace facebook::fboss
