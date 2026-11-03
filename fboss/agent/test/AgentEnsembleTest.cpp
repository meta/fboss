// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "fboss/agent/test/AgentEnsembleTest.h"
#include <fmt/core.h>
#include <folly/gen/Base.h>
#include <optional>
#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/CommonInit.h"
#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/Main.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_constants.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/utils/QosTestUtils.h"
#include "fboss/qsfp_service/lib/QsfpClient.h"

namespace {
int argCount{0};
char** argVec{nullptr};
} // unnamed namespace

DECLARE_string(config);
DECLARE_bool(disable_looped_fabric_ports);
DEFINE_bool(
    enable_sdk_dump,
    false,
    "Generate sdk debug dump when exiting test");

namespace facebook::fboss {

void AgentEnsembleTest::setupAgentEnsemble(bool disableLinkStateToggler) {
  setVersionInfo();
  gflags::ParseCommandLineFlags(&argCount, &argVec, false);
  // TODO(Elangovan) add support for production features

  auto config = fbossCommonInit(argCount, argVec);
  // Reset any global state being tracked in singletons
  // Each test then sets up its own state as needed.
  folly::SingletonVault::singleton()->destroyInstances();
  folly::SingletonVault::singleton()->reenableInstances();

  setCmdLineFlagOverrides();
  preInitSetup();
  AgentEnsembleSwitchConfigFn initialConfigFn =
      [this](const AgentEnsemble& ensemble) { return initialConfig(ensemble); };

  agentEnsemble_ = createAgentEnsemble(
      initialConfigFn,
      disableLinkStateToggler,
      platformConfigFn_,
      (HwSwitch::FeaturesDesired::PACKET_RX_DESIRED |
       HwSwitch::FeaturesDesired::LINKSCAN_DESIRED),
      TestEnsembleInitInfo{.failHwCallsOnWarmboot = false});
  XLOG(DBG2) << "Agent has been setup and ready for the test";
}

void AgentEnsembleTest::setCmdLineFlagOverrides() const {
  // Looped ports are the common case in tests
  FLAGS_disable_looped_fabric_ports = false;
  // Set HW agent connection timeout to 130 seconds
  FLAGS_hw_agent_connection_timeout_ms = 130000;
  // Link snapshots use a huge amount of logspace and aren't human-readable
  FLAGS_enable_snapshot_debugs = false;
}

void AgentEnsembleTest::TearDown() {
  if (FLAGS_run_forever ||
      (::testing::Test::HasFailure() && FLAGS_run_forever_on_failure)) {
    runForever();
  }
  // If the test is skipped, don't do warmboot shutdown, as it will make the
  // test report as 'passing' instead of 'skipped'
  if (FLAGS_setup_for_warmboot &&
      isSupportedOnAllAsics(HwAsic::Feature::WARMBOOT) &&
      !::testing::Test::IsSkipped()) {
    XLOG(DBG2) << "tearDownAgentEnsemble() for warmboot";
    tearDownAgentEnsemble(true);
  } else {
    tearDownAgentEnsemble();
  }
}

void AgentEnsembleTest::tearDownAgentEnsemble(bool doWarmboot) {
  if (!agentEnsemble_) {
    return;
  }
  // Dump end-of-test metadata before any graceful exit. This runs on both the
  // cold-boot (setup_for_warmboot) and warm-boot phases, since gracefulExit()
  // below hard-exits the process on the setup_for_warmboot phase and gtest
  // TearDown() would otherwise be skipped.
  dumpTestMetadata();

  if (FLAGS_enable_sdk_dump) {
    agentEnsemble_->getHwDebugDump();
  }

  if (::testing::Test::HasFailure()) {
    // TODO: Collect Info and dump counters
  }
  if (doWarmboot) {
    agentEnsemble_->gracefulExit();
  }
  agentEnsemble_.reset();
}

cfg::SwitchConfig AgentEnsembleTest::initialConfig(
    [[maybe_unused]] const AgentEnsemble& ensemble) {
  auto agentConf = AgentConfig::fromDefaultFile();
  return agentConf->thrift.sw().value();
}

void AgentEnsembleTest::preInitSetup() {
  utilCreateDir(getAgentTestDir());
}

// We are now returning a map of portId to port stats. Need to modify callers
// accordingly
std::map<PortID, HwPortStats> AgentEnsembleTest::getPortStats(
    const std::vector<PortID>& ports) const {
  std::map<PortID, HwPortStats> portStats;
  checkWithRetry(
      [&portStats, &ports, this]() {
        portStats = getSw()->getHwPortStats(ports);
        // Check collect timestamp is valid
        for (const auto& [portId, portStat] : portStats) {
          if (*portStat.timestamp_() ==
              hardware_stats_constants::STAT_UNINITIALIZED()) {
            return false;
          }
        }
        return !portStats.empty();
      },
      120,
      std::chrono::milliseconds(1000),
      " fetch port stats");
  return portStats;
}

SwSwitch* AgentEnsembleTest::getSw() const {
  return agentEnsemble_->getSw();
}

bool AgentEnsembleTest::isSupportedOnAllAsics(HwAsic::Feature feature) const {
  // All Asics supporting the feature
  return agentEnsemble_->getSw()->getHwAsicTable()->isFeatureSupportedOnAllAsic(
      feature);
}

AgentEnsemble* AgentEnsembleTest::getAgentEnsemble() const {
  return agentEnsemble_.get();
}

const std::shared_ptr<SwitchState> AgentEnsembleTest::getProgrammedState()
    const {
  return getAgentEnsemble()->getProgrammedState();
}

std::map<PortID, FabricEndpoint> AgentEnsembleTest::getFabricConnectivity(
    SwitchID switchId) const {
  return getAgentEnsemble()->getFabricConnectivity(switchId);
}

void AgentEnsembleTest::resolveNeighbor(
    PortDescriptor portDesc,
    const folly::IPAddress& ip,
    folly::MacAddress mac) {
  // TODO support agg ports as well.
  CHECK(portDesc.isPhysicalPort());
  auto port = getProgrammedState()->getPorts()->getNodeIf(portDesc.phyPortID());
  auto vlan = port->getVlans().begin()->first;
  if (ip.isV4()) {
    resolveNeighbor(portDesc, ip.asV4(), vlan, mac);
  } else {
    resolveNeighbor(portDesc, ip.asV6(), vlan, mac);
  }
}

template <typename AddrT>
void AgentEnsembleTest::resolveNeighbor(
    const PortDescriptor& port,
    const AddrT& ip,
    const VlanID& vlanId,
    folly::MacAddress mac) {
  using NeighborTableT = typename std::conditional_t<
      std::is_same<AddrT, folly::IPAddressV4>::value,
      ArpTable,
      NdpTable>;

  auto resolveNeighborFn = [=,
                            this](const std::shared_ptr<SwitchState>& state) {
    auto outputState{state->clone()};
    auto vlan = outputState->getVlans()->getNode(vlanId);
    auto intfId = vlan->getInterfaceID();
    NeighborTableT* nbrTable;
    auto intf = outputState->getInterfaces()->getNode(intfId);
    nbrTable = intf->template getNeighborEntryTable<AddrT>()->modify(
        intfId, &outputState);

    if (nbrTable->getEntryIf(ip)) {
      nbrTable->updateEntry(
          ip, mac, port, vlan->getInterfaceID(), NeighborState::REACHABLE);
    } else {
      nbrTable->addEntry(ip, mac, port, vlan->getInterfaceID());
    }
    return outputState;
  };
  getSw()->updateStateBlocking("resolve nbr", resolveNeighborFn);
}

void AgentEnsembleTest::runForever() const {
  XLOG(DBG2) << "AgentEnsembleTest run forever...";
  while (true) {
    sleep(1);
    XLOG_EVERY_MS(DBG2, 5000) << "AgentEnsembleTest running forever";
  }
}

bool AgentEnsembleTest::waitForSwitchStateCondition(
    std::function<bool(const std::shared_ptr<SwitchState>&)> conditionFn,
    uint32_t retries,
    std::chrono::duration<uint32_t, std::milli> msBetweenRetry) const {
  auto newState = getSw()->getState();
  while (retries--) {
    if (conditionFn(newState)) {
      return true;
    }
    std::this_thread::sleep_for(msBetweenRetry);
    newState = getProgrammedState();
  }
  XLOG(DBG3) << "Awaited state condition was never satisfied";
  return false;
}

void AgentEnsembleTest::setPortStatus(PortID portId, bool up) {
  auto configFnLinkDown = [=, this](const std::shared_ptr<SwitchState>& state) {
    auto newState = state->clone();
    auto ports = newState->getPorts()->modify(&newState);
    auto port = ports->getNodeIf(portId)->clone();
    port->setAdminState(
        up ? cfg::PortState::ENABLED : cfg::PortState::DISABLED);
    ports->updateNode(port, getSw()->getScopeResolver()->scope(port));
    return newState;
  };
  getSw()->updateStateBlocking("set port state", configFnLinkDown);
}

void AgentEnsembleTest::setPortLoopbackMode(
    PortID portId,
    cfg::PortLoopbackMode mode) {
  auto setLbMode = [=, this](const std::shared_ptr<SwitchState>& state) {
    auto newState = state->clone();
    auto ports = newState->getPorts()->modify(&newState);
    auto port = ports->getNodeIf(portId)->clone();
    port->setLoopbackMode(mode);
    return newState;
  };
  getSw()->updateStateBlocking("set port loopback mode", setLbMode);
}

std::string AgentEnsembleTest::getPortName(PortID portId) const {
  for (auto portMap : std::as_const(*getSw()->getState()->getPorts())) {
    for (auto port : std::as_const(*portMap.second)) {
      if (port.second->getID() == portId) {
        return port.second->getName();
      }
    }
  }
  throw FbossError("No port with ID: ", portId);
}

// Returns the port names for a given list of portIDs
std::vector<std::string> AgentEnsembleTest::getPortNames(
    const std::vector<PortID>& ports) const {
  std::vector<std::string> portNames;
  for (auto port : ports) {
    portNames.push_back(getPortName(port));
  }
  return portNames;
}

// Waits till the link status of the passed in ports reaches
// the expected state
void AgentEnsembleTest::waitForLinkStatus(
    const std::vector<PortID>& portsToCheck,
    bool up,
    uint32_t retries,
    std::chrono::duration<uint32_t, std::milli> msBetweenRetry) const {
  XLOG(DBG2) << "Checking link status on "
             << folly::join(",", getPortNames(portsToCheck));
  auto portStatus = getSw()->getPortStatus();
  std::vector<PortID> badPorts;
  while (retries--) {
    badPorts.clear();
    for (const auto& port : portsToCheck) {
      if (*portStatus[port].up() != up) {
        portStatus = getSw()->getPortStatus();
        badPorts.push_back(port);
      }
    }
    if (badPorts.empty()) {
      return;
    } else {
      /* sleep override */
      std::this_thread::sleep_for(msBetweenRetry);
    }
  }

  auto msg = fmt::format(
      "Unexpected Link status {:d} for {:s}",
      !up,
      folly::join(",", getPortNames(badPorts)));
  logLinkDbgMessage(badPorts);
  throw FbossError(msg);
}

void AgentEnsembleTest::getAllHwPortStats(
    std::map<std::string, HwPortStats>& hwPortStats) const {
  checkWithRetry(
      [&hwPortStats, this]() {
        hwPortStats.clear();
        getSw()->getAllHwPortStats(hwPortStats);
        for (const auto& [port, portStats] : hwPortStats) {
          if (*portStats.timestamp_() ==
              hardware_stats_constants::STAT_UNINITIALIZED()) {
            return false;
          }
        }
        return !hwPortStats.empty();
      },
      120,
      std::chrono::milliseconds(1000),
      " fetch all port stats");
}

// Provided the timestamp of the last port stats collection, get another unique
// set of valid port stats
std::map<std::string, HwPortStats> AgentEnsembleTest::getNextUpdatedHwPortStats(
    int64_t timestamp) {
  std::map<std::string, HwPortStats> portStats;
  // TODO(Elangovan) do we need 120 retries?
  checkWithRetry(
      [&portStats, timestamp, this]() {
        // clear the port stats between each retry
        portStats.clear();
        getSw()->getAllHwPortStats(portStats);
        // Stats can be briefly empty right after warmboot
        if (portStats.empty()) {
          return false;
        }
        // Since each port can have a unique timestamp, compare with the first
        // port
        if (*portStats.begin()->second.timestamp_() == timestamp) {
          return false;
        }
        // Make sure that the other ports have valid stats
        for (const auto& [port, portStat] : portStats) {
          if (*portStat.timestamp_() ==
              hardware_stats_constants::STAT_UNINITIALIZED()) {
            return false;
          }
        }
        return true;
      },
      120,
      std::chrono::milliseconds(1000),
      " fetch port stats");

  return portStats;
}

void AgentEnsembleTest::assertNoInDiscards(int maxNumDiscards) {
  // When port stat is not updated yet post warmboot (collect timestamp will be
  // -1), retry another round on all ports.
  int numRounds = 0;
  auto lastStatRefTime = hardware_stats_constants::STAT_UNINITIALIZED();

  // Gather 2 round of valid port stats and ensure the discards are within
  // maxNumDiscards
  for (numRounds = 0; numRounds < 2; numRounds++) {
    auto portStats = getNextUpdatedHwPortStats(lastStatRefTime);
    lastStatRefTime = *portStats.begin()->second.timestamp_();

    for (const auto& [port, stats] : portStats) {
      auto inDiscards = *stats.inDiscards_();
      XLOG(DBG2) << "Port: " << port << " in discards: " << inDiscards
                 << " in bytes: " << *stats.inBytes_()
                 << " out bytes: " << *stats.outBytes_() << " at timestamp "
                 << *stats.timestamp_();
      EXPECT_LE(inDiscards, maxNumDiscards);
    }
  }
}

void AgentEnsembleTest::assertNoInErrors(int maxNumDiscards) {
  // When port stat is not updated yet post warmboot (collect timestamp will be
  // -1), retry another round on all ports.
  int numRounds = 0;
  auto lastStatRefTime = hardware_stats_constants::STAT_UNINITIALIZED();

  // Gather 2 round of valid port stats and ensure the discards are within
  // maxNumDiscards
  for (numRounds = 0; numRounds < 2; numRounds++) {
    auto portStats = getNextUpdatedHwPortStats(lastStatRefTime);
    lastStatRefTime = *portStats.begin()->second.timestamp_();

    for (const auto& [port, stats] : portStats) {
      auto inErrors = *stats.inErrors_();
      XLOG(DBG2) << "Port: " << port << " in errors: " << inErrors
                 << " in bytes: " << *stats.inBytes_()
                 << " out bytes: " << *stats.outBytes_() << " at timestamp "
                 << *stats.timestamp_();
      EXPECT_LE(inErrors, maxNumDiscards);
    }
  }
}

void AgentEnsembleTest::dumpRunningConfig(const std::string& targetDir) {
  auto testConfig = getSw()->getAgentConfig();
  auto newcfg = AgentConfig(
      testConfig,
      apache::thrift::SimpleJSONSerializer::serialize<std::string>(testConfig));
  newcfg.dumpConfig(targetDir);
}

void AgentEnsembleTest::setupConfigFile(
    const cfg::SwitchConfig& cfg,
    const std::string& testConfigDir,
    const std::string& configFile) const {
  // TODO(Elangovan) evaluate if this is needed with the move to Agent ensemble
}

std::shared_ptr<SwitchState> AgentEnsembleTest::applyNewConfig(
    const cfg::SwitchConfig& config) {
  return agentEnsemble_->applyNewConfig(config);
}

void AgentEnsembleTest::reloadConfig(std::string reason) const {
  // reload config so that test config is loaded
  getSw()->applyConfig(reason, true);
}

void AgentEnsembleTest::reloadPlatformConfig() {
  getAgentEnsemble()->reloadPlatformConfig();
}

SwitchID AgentEnsembleTest::scope(
    const boost::container::flat_set<PortDescriptor>& ports) {
  return scope(getSw()->getState(), ports);
}

SwitchID AgentEnsembleTest::scope(
    const std::shared_ptr<SwitchState>& state,
    const boost::container::flat_set<PortDescriptor>& ports) {
  auto matcher = getSw()->getScopeResolver()->scope(state, ports);
  return matcher.switchId();
}

PortID AgentEnsembleTest::getPortID(const std::string& portName) const {
  for (auto portMap : std::as_const(*getSw()->getState()->getPorts())) {
    for (auto port : std::as_const(*portMap.second)) {
      if (port.second->getName() == portName) {
        return port.second->getID();
      }
    }
  }
  throw FbossError("No port named: ", portName);
}

void initAgentEnsembleTest(
    int argc,
    char** argv,
    PlatformInitFn initPlatformFn,
    std::optional<cfg::StreamType> streamType) {
  argCount = argc;
  argVec = argv;
  initEnsemble(initPlatformFn, streamType);
}

} // namespace facebook::fboss
