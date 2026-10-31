// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/AgentEnsemble.h"

#include <chrono>
#include <map>
#include <vector>

#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/CommonInit.h"
#include "fboss/agent/EncapIndexAllocator.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/ThriftHandler.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_constants.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/PacketSendUtils.h"
#include "fboss/agent/types.h"
#include "fboss/lib/CommonFileUtils.h"
#include "fboss/lib/CommonUtils.h"
#include "fboss/lib/config/PlatformConfigUtils.h"

#include <fmt/format.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <folly/testing/TestUtil.h>
#include <thrift/lib/cpp2/async/PooledRequestChannel.h>
#include <thrift/lib/cpp2/async/ReconnectingRequestChannel.h>
#include <thrift/lib/cpp2/async/RetryingRequestChannel.h>
#include <thrift/lib/cpp2/async/RocketClientChannel.h>
#ifndef IS_OSS
#include "common/thrift/thrift/gen-cpp2/MonitorAsyncClient.h"
#endif
#include <gtest/gtest.h>

DEFINE_bool(
    setup_for_warmboot,
    false,
    "Set up test for SDK warmboot. Useful for testing individual "
    "tests doing a full process warmboot and verifying expectations");

namespace {
facebook::fboss::PlatformInitFn kPlatformInitFn;
static std::string kInputConfigFile;
std::optional<facebook::fboss::cfg::StreamType> kStreamTypeOpt{std::nullopt};
static const int kMsWaitForStatsRetry = 2000;
constexpr auto kConfig = "config";
constexpr auto kMultiSwitch = "multi_switch";
constexpr auto kOverriddenAgentConfigFile = "overridden_agent.conf";
} // namespace

namespace facebook::fboss {
AgentEnsemble::AgentEnsemble(const std::string& configFileName) {
  setConfigFiles(configFileName);
  setBootType();
}

void AgentEnsemble::setupEnsemble(
    AgentEnsembleSwitchConfigFn initialConfigFn,
    bool disableLinkStateToggler,
    AgentEnsemblePlatformConfigFn platformConfigFn,
    uint32_t hwFeaturesDesired,
    const TestEnsembleInitInfo& initInfo) {
  FLAGS_verify_apply_oper_delta = true;
  FLAGS_enable_bulk_create_ecmp_members = true;

  if (bootType_ == BootType::COLD_BOOT || FLAGS_prod_invariant_config_test) {
    auto inputAgentConfig =
        AgentConfig::fromFile(AgentEnsemble::getInputConfigFile())->thrift;
    // If overrideDsfNodes is provided in initInfo, use it to update the
    // dsfNodes in the config. This is useful for tests that need specific
    // DSF configuration (e.g., L2 fabric level) before the HwSwitch is
    // created.
    if (initInfo.overrideDsfNodes.has_value()) {
      inputAgentConfig.sw()->dsfNodes() = *initInfo.overrideDsfNodes;
    }

    if (initInfo.overridePortIdRange.has_value() ||
        initInfo.overrideLocalSystemPortOffset.has_value()) {
      for (auto& [_, switchInfo] :
           *inputAgentConfig.sw()->switchSettings()->switchIdToSwitchInfo()) {
        if (initInfo.overridePortIdRange.has_value()) {
          switchInfo.portIdRange() = *initInfo.overridePortIdRange;
        }
        if (initInfo.overrideLocalSystemPortOffset.has_value()) {
          switchInfo.localSystemPortOffset() =
              *initInfo.overrideLocalSystemPortOffset;
        }
      }
    }

    if (platformConfigFn) {
      platformConfigFn(
          *(inputAgentConfig.sw()), *(inputAgentConfig.platform()));
    }
    // some platform config may need cold boots. so overwrite the config before
    // creating a switch
    writeConfig(inputAgentConfig, configFile_);
  }

  auto agentConf = AgentConfig::fromFile(configFile_);

  overrideConfigFlag(configFile_);
  createSwitch(std::move(agentConf), hwFeaturesDesired, kPlatformInitFn);

  for (auto switchId : getSw()->getHwAsicTable()->getSwitchIDs()) {
    HwAsic* asic = getHwAsicTable()->getHwAsicIf(switchId);
    if (kStreamTypeOpt.has_value()) {
      asic->setDefaultStreamType(kStreamTypeOpt.value());
    }
    switchId2PortIds_[switchId] = std::vector<PortID>();
  }

  auto portsByControllingPort = utility::getSubsidiaryPortIDs(
      getSw()->getPlatformMapping()->getPlatformPorts());
  const auto& platformPorts = getSw()->getPlatformMapping()->getPlatformPorts();
  for (const auto& port : portsByControllingPort) {
    if (*platformPorts.find(static_cast<int32_t>(port.first))
                ->second.mapping()
                ->portType() == cfg::PortType::FABRIC_PORT &&
        FLAGS_hide_fabric_ports) {
      continue;
    }
    if (*platformPorts.find(static_cast<int32_t>(port.first))
                ->second.mapping()
                ->portType() == cfg::PortType::MANAGEMENT_PORT &&
        FLAGS_hide_management_ports) {
      continue;
    }
    if (*platformPorts.find(static_cast<int32_t>(port.first))
                ->second.mapping()
                ->portType() == cfg::PortType::INTERFACE_PORT &&
        FLAGS_hide_interface_ports) {
      continue;
    }
    auto switchId = getSw()->getScopeResolver()->scope(port.first).switchId();
    switchId2PortIds_[switchId].push_back(port.first);
  }

  // For multi-die ASICs (e.g. Q4D), interleave ports across dies so that
  // tests using 2+ ports automatically exercise both dies.
  interleavePortsAcrossDies(platformPorts);

  // The per-type caps are applied dynamically in the masterLogicalPortIds()
  // views (see getMaxRequiredPorts / masterLogicalPortIdsImpl);
  // switchId2PortIds_ stays the full master-port store. Those capped views are
  // what tests and the initial config consume, so the config is trimmed to
  // maxRequired*Ports.
  maxRequiredInterfacePorts_ = initInfo.maxRequiredInterfacePorts;
  maxRequiredFabricPorts_ = initInfo.maxRequiredFabricPorts;

  for (auto switchId : getSw()->getHwAsicTable()->getSwitchIDs()) {
    HwAsic* asic = getHwAsicTable()->getHwAsicIf(switchId);
    utility::setPortToDefaultProfileIDMap(
        std::make_shared<MultiSwitchPortMap>(), /* unused */
        getSw()->getPlatformMapping(),
        asic,
        getSw()->getPlatformSupportsAddRemovePort(),
        switchId2PortIds_[switchId]);
  }

  // during config invariant test, we want to use the input config file during
  // warm boot
  if (bootType_ == BootType::COLD_BOOT || FLAGS_prod_invariant_config_test) {
    initialConfig_ = initialConfigFn(*this);
    applyInitialConfig(initialConfig_);
    // reload the new config
    reloadPlatformConfig();
  } else {
    initialConfig_ = *(AgentConfig::fromFile(configFile_)->thrift.sw());
  }

  createAndDumpOverriddenAgentConfig();

  // Setup LinkStateToggler and start agent
  if (hwFeaturesDesired & HwSwitch::FeaturesDesired::LINKSCAN_DESIRED &&
      disableLinkStateToggler == false) {
    setupLinkStateToggler();
  }
  startAgent(initInfo.failHwCallsOnWarmboot);

  for (const auto& switchId : getSw()->getSwitchInfoTable().getL3SwitchIDs()) {
    auto switchIndex =
        getSw()->getSwitchInfoTable().getSwitchIndexFromSwitchId(switchId);
    if (switchIndex >= FLAGS_num_npus_for_testing) {
      XLOG(DBG2) << "Skipping hw switch connection wait for switchId: "
                 << static_cast<int64_t>(switchId)
                 << " switchIndex: " << switchIndex
                 << " numNpusForTesting: " << FLAGS_num_npus_for_testing;
      continue;
    }
    ensureHwSwitchConnected(switchId);
  }
}

void AgentEnsemble::interleavePortsAcrossDies(
    const std::map<int32_t, cfg::PlatformPortEntry>& platformPorts) {
  for (auto& [switchId, portIds] : switchId2PortIds_) {
    auto* asic = getHwAsicTable()->getHwAsicIf(switchId);
    if (!asic || asic->getNumDies() <= 1) {
      continue;
    }
    auto numDies = asic->getNumDies();
    // Separate non-interface ports (keep original order) from interface ports
    // (interleave across dies). Non-interface ports (recycle, management) may
    // have unequal counts per die which skews the round-robin if included.
    std::vector<PortID> nonInterfacePorts;
    std::vector<std::vector<PortID>> interfacePortsByDie(numDies);
    for (auto portId : portIds) {
      auto platformPortIter = platformPorts.find(static_cast<int32_t>(portId));
      if (platformPortIter == platformPorts.end()) {
        continue;
      }
      auto portType = *platformPortIter->second.mapping()->portType();
      if (portType != cfg::PortType::INTERFACE_PORT) {
        nonInterfacePorts.push_back(portId);
      } else if (platformPortIter->second.mapping()
                     ->attachedCoreId()
                     .has_value()) {
        auto coreId = *platformPortIter->second.mapping()->attachedCoreId();
        auto dieId = asic->getDieIdForCore(coreId);
        interfacePortsByDie[dieId < numDies ? dieId : 0].push_back(portId);
      } else {
        // No core info available, default to die 0
        interfacePortsByDie[0].push_back(portId);
      }
    }
    // Build reordered list: non-interface ports first, then round-robin
    // interface ports across dies: die0[0], die1[0], die0[1], die1[1], ...
    std::vector<PortID> reordered;
    reordered.reserve(portIds.size());
    reordered.insert(
        reordered.end(), nonInterfacePorts.begin(), nonInterfacePorts.end());
    size_t maxSize = 0;
    for (const auto& diePorts : interfacePortsByDie) {
      maxSize = std::max(maxSize, diePorts.size());
    }
    for (size_t i = 0; i < maxSize; ++i) {
      for (uint32_t die = 0; die < numDies; ++die) {
        if (i < interfacePortsByDie[die].size()) {
          reordered.push_back(interfacePortsByDie[die][i]);
        }
      }
    }
    portIds = std::move(reordered);
  }
}

void AgentEnsemble::startAgent(bool failHwCallsOnWarmboot) {
  auto* initializer = agentInitializer();
  auto hwWriteBehavior = HwWriteBehavior::WRITE;
  if (getSw()->getWarmBootHelper()->canWarmBoot(
          getSw()->isRunModeMultiSwitch(),
          getSw()->getHwSwitchThriftClientTable())) {
    hwWriteBehavior = HwWriteBehavior::LOG_FAIL;
    if (getSw()->getHwAsicTable()->isFeatureSupportedOnAllAsic(
            HwAsic::Feature::ZERO_SDK_WRITE_WARMBOOT)) {
      if (failHwCallsOnWarmboot) {
        hwWriteBehavior = HwWriteBehavior::FAIL;
      } else {
        // If failHwCallsOnWarmboot = false, skip logging as the write is
        // expected
        hwWriteBehavior = HwWriteBehavior::WRITE;
      }
    }
  }
  asyncInitThread_.reset(new std::thread([this, initializer, hwWriteBehavior] {
    // hardware switch events will be dispatched to agent ensemble
    // agent ensemble is responsible to dispatch them to SwSwitch
    initializer->initAgent(this, hwWriteBehavior);
  }));
  initializer->initializer()->waitForInitDone();

  if (FLAGS_verify_recover_from_hw_switch) {
    CHECK_EQ(getSw()->getBootType(), BootType::WARM_BOOT)
        << "verify_recover_from_hw_switch is set but boot type is not WARM_BOOT";
    CHECK(getSw()->getWarmBootHelper()->isWarmBootFromHwSwitch())
        << "verify_recover_from_hw_switch is set but did not warmboot from HW switch";
  }

  if (getSw()->getBootType() == BootType::COLD_BOOT) {
    if (linkToggler_ != nullptr) {
      linkToggler_->applyInitialConfig(initialConfig_);
    }
    // With link state toggler, initial config is applied with ports down to
    // later bring up the ports. This causes the config to be written as
    // loopback mode = None. Write the init config again to have the proper
    // config.
    applyNewConfig(initialConfig_);
  } else {
    if (FLAGS_prod_invariant_config_test) {
      // During warmboot, the ports are already up.
      applyNewConfig(initialConfig_);
    }
  }
}

void AgentEnsemble::writeConfig(const cfg::SwitchConfig& config) {
  auto* initializer = agentInitializer();
  auto isSwConfigured =
      initializer->sw() && initializer->sw()->isFullyConfigured();
  auto agentConfig = isSwConfigured
      ? initializer->sw()->getAgentConfig()
      : AgentConfig::fromFile(configFile_)->thrift;

  agentConfig.sw() = config;
  // Inherit SDK version from previous config
  auto inputConfig = AgentConfig::fromFile(FLAGS_config)->thrift;
  if (inputConfig.sw()->sdkVersion().has_value()) {
    agentConfig.sw()->sdkVersion() = inputConfig.sw()->sdkVersion().value();
  }
  writeConfig(agentConfig);
}

void AgentEnsemble::writeConfig(const cfg::AgentConfig& agentConfig) {
  writeConfig(agentConfig, configFile_);
}

void AgentEnsemble::writeConfig(
    const cfg::AgentConfig& agentConfig,
    const std::string& fileName) {
  auto newAgentConfig = AgentConfig(
      agentConfig,
      apache::thrift::SimpleJSONSerializer::serialize<std::string>(
          agentConfig));
  newAgentConfig.dumpConfig(fileName);
}

void AgentEnsemble::overrideConfigFlag(const std::string& fileName) {
  FLAGS_config = fileName;
  initFlagDefaults(
      *(AgentConfig::fromFile(fileName)->thrift.defaultCommandLineArgs()));
}

AgentEnsemble::~AgentEnsemble() {
  joinAsyncInitThread();
}

void AgentEnsemble::applyNewConfig(
    const cfg::SwitchConfig& config,
    bool activate) {
  writeConfig(config);
  if (activate) {
    getSw()->applyConfig("applying new config", config);
  }
}

std::vector<PortID> AgentEnsemble::getAllMasterLogicalPortIds() const {
  std::vector<PortID> all;
  for (const auto& [switchId, portIds] : switchId2PortIds_) {
    all.insert(all.end(), portIds.begin(), portIds.end());
  }
  return all;
}

std::optional<size_t> AgentEnsemble::getMaxRequiredPorts(
    cfg::PortType portType) const {
  // Enumerate every PortType explicitly (no default) so that adding a new
  // port type forces a deliberate decision here about whether it should be
  // capped.
  switch (portType) {
    case cfg::PortType::INTERFACE_PORT:
      return maxRequiredInterfacePorts_;
    case cfg::PortType::FABRIC_PORT:
      return maxRequiredFabricPorts_;
    case cfg::PortType::CPU_PORT:
    case cfg::PortType::RECYCLE_PORT:
    case cfg::PortType::MANAGEMENT_PORT:
    case cfg::PortType::EVENTOR_PORT:
    case cfg::PortType::HYPER_PORT:
    case cfg::PortType::HYPER_PORT_MEMBER:
      return std::nullopt;
  }
  return std::nullopt;
}

void AgentEnsemble::switchRunStateChanged(SwitchRunState runState) {}

void AgentEnsemble::programRoutes(
    const RouterID& rid,
    const ClientID& client,
    const utility::RouteDistributionGenerator::ThriftRouteChunks& routeChunks) {
  auto updater = getSw()->getRouteUpdater();
  for (const auto& routeChunk : routeChunks) {
    std::for_each(
        routeChunk.begin(),
        routeChunk.end(),
        [&updater, client, rid](const auto& route) {
          updater.addRoute(rid, client, route);
        });
    updater.program();
  }
}

void AgentEnsemble::unprogramRoutes(
    const RouterID& rid,
    const ClientID& client,
    const utility::RouteDistributionGenerator::ThriftRouteChunks& routeChunks) {
  auto updater = getSw()->getRouteUpdater();
  for (const auto& routeChunk : routeChunks) {
    std::for_each(
        routeChunk.begin(),
        routeChunk.end(),
        [&updater, client, rid](const auto& route) {
          updater.delRoute(rid, *route.dest(), client);
        });
    updater.program();
  }
}

void AgentEnsemble::stopStatsThread() {
  agentInitializer()->stopStatsThread();
}

void AgentEnsemble::gracefulExit() {
  auto* initializer = agentInitializer();
  // exit for warm boot
  bool gracefulExit = !::testing::Test::HasFailure();
  initializer->stopAgent(
      true /* setupWarmboot */, gracefulExit /* gracefulExit */);
}

void AgentEnsemble::applyNewState(
    StateUpdateFn fn,
    const std::string& name,
    bool transaction) {
  // TODO: Handle multiple Asics
  auto asic = getSw()->getHwAsicTable()->getHwAsics().cbegin()->second;
  CHECK(asic);
  auto applyUpdate = [&](const std::shared_ptr<SwitchState>& in) {
    auto newState = fn(in);
    if (!newState) {
      return newState;
    }
    return EncapIndexAllocator::updateEncapIndices(
        StateDelta(in, newState), *asic);
  };
  transaction ? getSw()->updateStateWithHwFailureProtection(name, applyUpdate)
              : getSw()->updateStateBlocking(name, applyUpdate);
}

void AgentEnsemble::enableExactMatch(bcm::BcmConfig& config) {
  if (auto yamlCfg = config.yamlConfig()) {
    // use common func
    facebook::fboss::enableExactMatch(*yamlCfg);
  } else {
    auto& cfg = *(config.config());
    cfg["fpem_mem_entries"] = "0x10000";
  }
}

void AgentEnsemble::setupLinkStateToggler() {
  if (linkToggler_) {
    return;
  }
  linkToggler_ = std::make_unique<LinkStateToggler>(this);
}

std::string AgentEnsemble::getInputConfigFile() {
  if (kInputConfigFile.empty()) {
    kInputConfigFile = FLAGS_config;
  }
  return kInputConfigFile;
}

void AgentEnsemble::setConfigFiles(const std::string& fileName) {
  if (kInputConfigFile.empty()) {
    kInputConfigFile = FLAGS_config;
  }
  utilCreateDir(AgentDirectoryUtil().agentEnsembleConfigDir());
  configFile_ = AgentDirectoryUtil().agentEnsembleConfigDir() + fileName;
}

void AgentEnsemble::setBootType() {
  auto dirUtil = AgentDirectoryUtil();
  bootType_ = (checkFileExists(dirUtil.getSwSwitchCanWarmBootFile()))
      ? BootType::WARM_BOOT
      : BootType::COLD_BOOT;
}

BootType AgentEnsemble::getBootType() const {
  return bootType_;
}

void initEnsemble(
    PlatformInitFn initPlatform,
    std::optional<cfg::StreamType> streamType) {
  kPlatformInitFn = std::move(initPlatform);
  kStreamTypeOpt = streamType;
}

std::map<PortID, HwPortStats> AgentEnsemble::getLatestPortStats(
    const std::vector<PortID>& ports) {
  // Stats collection from SwSwitch is async, wait for stats
  // being available before returning here.
  std::map<PortID, HwPortStats> portIdStatsMap;
  checkWithRetry(
      [&portIdStatsMap, &ports, this]() {
        portIdStatsMap = getSw()->getHwPortStats(ports);
        // Check collect timestamp is valid
        for (const auto& [_, portStats] : portIdStatsMap) {
          if (*portStats.timestamp_() ==
              hardware_stats_constants::STAT_UNINITIALIZED()) {
            return false;
          }
        }
        return !portIdStatsMap.empty();
      },
      120,
      std::chrono::milliseconds(1000),
      " fetch port stats");
  return portIdStatsMap;
}

std::map<InterfaceID, HwRouterInterfaceStats>
AgentEnsemble::getLatestInterfaceStats(
    const std::vector<InterfaceID>& interfaces) {
  std::map<InterfaceID, HwRouterInterfaceStats> intfIdStatsMap;
  checkWithRetry(
      [&intfIdStatsMap, &interfaces, this]() {
        intfIdStatsMap = getSw()->getHwRouterInterfaceStats(interfaces);
        return !intfIdStatsMap.empty();
      },
      120,
      std::chrono::milliseconds(1000),
      " fetch interface stats");
  return intfIdStatsMap;
}

std::map<SystemPortID, HwSysPortStats> AgentEnsemble::getLatestSysPortStats(
    const std::vector<SystemPortID>& ports) {
  return getSw()->getHwSysPortStats(ports);
}

void AgentEnsemble::registerStateObserver(
    StateObserver* observer,
    const std::string& name) {
  getSw()->registerStateObserver(observer, name);
}

void AgentEnsemble::unregisterStateObserver(StateObserver* observer) {
  getSw()->unregisterStateObserver(observer);
}

std::map<PortID, FabricEndpoint> AgentEnsemble::getFabricConnectivity(
    SwitchID switchId) const {
  std::map<PortID, FabricEndpoint> connectivity;
  auto gotConnectivity =
      getSw()->getHwSwitchThriftClientTable()->getFabricConnectivity(switchId);

  for (const auto& [portName, fabricEndpoint] : gotConnectivity) {
    auto portID = getSw()->getPlatformMapping()->getPortID(portName);

    connectivity.insert({portID, fabricEndpoint});
  }
  return connectivity;
}

void AgentEnsemble::runDiagCommand(
    const std::string& input,
    std::string& output,
    std::optional<SwitchID> switchId) {
  ClientInformation clientInfo;
  clientInfo.username() = "agent_ensemble";
  clientInfo.hostname() = "agent_ensemble";
  if (FLAGS_multi_switch) {
    CHECK(switchId.has_value());
    output = getSw()->getHwSwitchThriftClientTable()->diagCmd(
        switchId.value(), input, clientInfo);
  } else {
    auto client = createFbossHwClient(
        5909, std::make_shared<folly::ScopedEventBaseThread>());
    fbstring out;
    client->sync_diagCmd(
        out,
        input,
        clientInfo,
        0 /* serverTimeoutMsecs */,
        false /* bypassFilter */);
    output = out;
  }
}

void AgentEnsemble::runCint(
    const std::string& cintData,
    std::string& output,
    const SwitchID& switchId) {
  folly::test::TemporaryFile file;
  folly::writeFull(file.fd(), cintData.c_str(), cintData.size());
  auto cmd = fmt::format("cint {}\n", file.path().c_str());
  runDiagCommand(cmd, output, switchId);
}

LinkStateToggler* AgentEnsemble::getLinkToggler() {
  return linkToggler_.get();
}

uint64_t AgentEnsemble::getTrafficRate(
    const HwPortStats& prevPortStats,
    const HwPortStats& curPortStats,
    const int secondsBetweenStatsCollection) {
  auto prevPortBytes = *prevPortStats.outBytes_();
  auto prevPortPackets =
      (*prevPortStats.outUnicastPkts_() + *prevPortStats.outMulticastPkts_() +
       *prevPortStats.outBroadcastPkts_());

  auto curPortPackets =
      (*curPortStats.outUnicastPkts_() + *curPortStats.outMulticastPkts_() +
       *curPortStats.outBroadcastPkts_());

  // 20 bytes are consumed by ethernet preamble, start of frame and
  // interpacket gap. Account for that in linerate.
  auto packetPaddingBytes = (curPortPackets - prevPortPackets) * 20;
  auto curPortBytes = *curPortStats.outBytes_() + packetPaddingBytes;
  // Stats are sampled asynchronously by the stats thread, so the interval
  // between the two samples can differ from how long the caller waited.
  const bool timestampsValid = *prevPortStats.timestamp_() !=
          hardware_stats_constants::STAT_UNINITIALIZED() &&
      *curPortStats.timestamp_() !=
          hardware_stats_constants::STAT_UNINITIALIZED();
  const int64_t elapsedSec = timestampsValid
      ? *curPortStats.timestamp_() - *prevPortStats.timestamp_()
      : 0;
  const int64_t durationSec =
      elapsedSec > 0 ? elapsedSec : std::max(1, secondsBetweenStatsCollection);
  auto rate =
      static_cast<uint64_t>((curPortBytes - prevPortBytes) * 8) / durationSec;
  XLOG(DBG2) << "Current rate " << rate << " bps over " << durationSec
             << " seconds (caller expected " << secondsBetweenStatsCollection
             << "), curPortBytes " << curPortBytes << " prevPortBytes "
             << prevPortBytes << " curPortPackets " << curPortPackets
             << " prevPortPackets " << prevPortPackets;
  return rate;
}

/*
 * Wait for traffic on port to reach specified rate. If the
 * specified rate is reached, return true, else false.
 */
bool AgentEnsemble::waitForRateOnPort(
    PortID port,
    const uint64_t desiredBps,
    int secondsToWaitPerIteration) {
  // Need to wait for atleast one second
  if (secondsToWaitPerIteration < 1) {
    secondsToWaitPerIteration = 1;
    XLOG(WARNING) << "Setting wait time to 1 second for tests!";
  }

  if (FLAGS_hyper_port) {
    XLOG(DBG2)
        << "enable SRAM only through diag to achieve 3.2Tbps linerate for hyper port";
    std::string out;
    this->runDiagCommand(
        "w CGM_VOQ_SRAM_DRAM_MODE 0 128 1\n", out, SwitchID(0));
    XLOG(DBG2) << "diag output: " << out;
  }

  const auto portSpeedBps =
      static_cast<uint64_t>(
          getProgrammedState()->getPorts()->getNodeIf(port)->getSpeed()) *
      1000 * 1000;
  if (desiredBps > portSpeedBps) {
    // Cannot achieve higher than line rate
    XLOG(ERR) << "Desired rate " << desiredBps << " bps is > port rate "
              << portSpeedBps << " bps!!";
    return false;
  }

  // The first iteration in the below loop will not be successful
  // given the prev/curr stats collections are back to back!
  auto prevPortStats = getLatestPortStats(port);
  XLOG(DBG0) << "PortID: " << port << ", Desired rate " << desiredBps;
  bool metDesiredRate = false;
  WITH_RETRIES_N_TIMED(
      10, std::chrono::milliseconds(1000 * secondsToWaitPerIteration), {
        auto curPortStats = getLatestPortStats(port);
        auto rate = getTrafficRate(
            prevPortStats, curPortStats, secondsToWaitPerIteration);
        // Update prev stats for the next iteration if needed!
        prevPortStats = curPortStats;
        if (desiredBps == 0) {
          metDesiredRate = rate == desiredBps;
        } else {
          metDesiredRate = rate >= desiredBps;
        }
        EXPECT_EVENTUALLY_TRUE(metDesiredRate);
      });
  return metDesiredRate;
}

void AgentEnsemble::waitForLineRateOnPort(PortID port) {
  const auto portSpeedBps =
      static_cast<uint64_t>(
          getProgrammedState()->getPorts()->getNodeIf(port)->getSpeed()) *
      1000 * 1000;
  if (waitForRateOnPort(port, portSpeedBps)) {
    // Traffic on port reached line rate!
    return;
  }
  throw FbossError("Line rate was never reached");
}

void AgentEnsemble::waitForSpecificRateOnPort(
    PortID port,
    const uint64_t desiredBps,
    int secondsToWaitPerIteration) {
  if (waitForRateOnPort(port, desiredBps, secondsToWaitPerIteration)) {
    // Traffic on port reached desired rate!
    return;
  }

  throw FbossError("Desired rate ", desiredBps, " bps was never reached");
}

void AgentEnsemble::sendPacketAsync(
    std::unique_ptr<TxPacket> pkt,
    std::optional<PortDescriptor> portDescriptor,
    std::optional<uint8_t> queueId,
    std::optional<SwitchID> switchId) {
  if (!portDescriptor.has_value()) {
    // For a switched send, target the requested NPU; default to the switch
    // owning the first master logical port (switch 0 on multi-NPU platforms).
    // Resolve the default lazily so masterLogicalPortIds()[0] is only accessed
    // when the caller did not supply a switchId.
    SwitchID resolvedSwitchId = switchId.has_value()
        ? *switchId
        : getSw()
              ->getScopeResolver()
              ->scope(masterLogicalPortIds()[0])
              .switchId();
    getSw()->sendPacketSwitchedAsync(std::move(pkt), {resolvedSwitchId});
    return;
  }
  getSw()->sendPacketOutOfPortAsync(
      std::move(pkt), portDescriptor->phyPortID(), queueId);
}

void AgentEnsemble::sendPacketSwitchedAsync(
    std::unique_ptr<TxPacket> pkt,
    const std::optional<SwitchID>& switchId) {
  if (switchId.has_value()) {
    getSw()->sendPacketSwitchedAsync(std::move(pkt), {*switchId});
  } else {
    // Preserve the prior untargeted behavior: route to the switch owning the
    // first master logical port (switch 0 on multi-NPU platforms).
    sendPacketAsync(std::move(pkt), std::nullopt, std::nullopt);
  }
}

std::unique_ptr<TxPacket> AgentEnsemble::allocatePacket(uint32_t size) {
  return getSw()->allocatePacket(size);
}

void AgentEnsemble::bringUpPorts(const std::vector<PortID>& ports) {
  CHECK(linkToggler_);
  linkToggler_->bringUpPorts(ports);
}

void AgentEnsemble::bringDownPorts(const std::vector<PortID>& ports) {
  CHECK(linkToggler_);
  linkToggler_->bringDownPorts(ports);
}

void AgentEnsemble::clearPortStats() {
  auto portsVec = std::make_unique<std::vector<int32_t>>();
  for (const auto& [key, ports] :
       std::as_const(*getProgrammedState()->getPorts())) {
    for (const auto& [id, port] : std::as_const(*ports)) {
      portsVec->push_back(id);
    }
  }
  clearPortStats(std::move(portsVec));
}

void AgentEnsemble::clearPortStats(
    const std::unique_ptr<std::vector<int32_t>>& ports) {
  ThriftHandler(getSw()).clearPortStats(
      std::make_unique<std::vector<int32_t>>(std::move(*ports)));
}

bool AgentEnsemble::ensureSendPacketSwitched(
    std::unique_ptr<TxPacket> pkt,
    const std::optional<SwitchID>& switchId) {
  // lambda that returns HwPortStats for the given port(s)
  auto getPortStats =
      [&](const std::vector<PortID>& portIds) -> std::map<PortID, HwPortStats> {
    std::map<PortID, HwPortStats> portStats;
    WITH_RETRIES({
      portStats = getLatestPortStats(portIds);
      EXPECT_EVENTUALLY_TRUE(portStats.size());
    });
    return portStats;
  };
  auto getSysPortStats = [&](const std::vector<SystemPortID>& portIds)
      -> std::map<SystemPortID, HwSysPortStats> {
    return getLatestSysPortStats(portIds);
  };

  // On multi-NPU platforms, scope the verified ports to the switch under test
  // so stats are checked on the same NPU the packet is routed to.
  auto portIds = switchId.has_value()
      ? masterLogicalInterfaceOrHyperPortIds(*switchId)
      : masterLogicalPortIds(
            std::set<cfg::PortType>{
                cfg::PortType::INTERFACE_PORT, cfg::PortType::HYPER_PORT});

  return utility::ensureSendPacketSwitched(
      this,
      std::move(pkt),
      portIds,
      getPortStats,
      masterLogicalSysPortIds(),
      getSysPortStats,
      kMsWaitForStatsRetry,
      switchId);
}

bool AgentEnsemble::ensureSendPacketOutOfPort(
    std::unique_ptr<TxPacket> pkt,
    PortID portID,
    std::optional<uint8_t> queue) {
  // lambda that returns HwPortStats for the given port(s)
  auto getPortStats =
      [&](const std::vector<PortID>& portIds) -> std::map<PortID, HwPortStats> {
    return getLatestPortStats(portIds);
  };
  return utility::ensureSendPacketOutOfPort(
      this,
      std::move(pkt),
      portID,
      masterLogicalPortIds(
          std::set<cfg::PortType>{
              cfg::PortType::INTERFACE_PORT,
              cfg::PortType::HYPER_PORT,
              cfg::PortType::HYPER_PORT_MEMBER}),
      getPortStats,
      queue,
      kMsWaitForStatsRetry);
}

std::unique_ptr<apache::thrift::Client<utility::AgentHwTestCtrl>>
AgentEnsemble::getHwAgentTestClient(SwitchID switchId) {
  auto switchIndex =
      getSw()->getSwitchInfoTable().getSwitchIndexFromSwitchId(switchId);
  uint16_t port = FLAGS_hwagent_port_base + switchIndex;
  auto evbThread = std::make_shared<folly::ScopedEventBaseThread>();

  auto reconnectingChannel =
      apache::thrift::PooledRequestChannel::newSyncChannel(
          evbThread, [port, evbThread](folly::EventBase& evb) {
            return apache::thrift::RetryingRequestChannel::newChannel(
                evb,
                2, /*retries before error*/
                apache::thrift::ReconnectingRequestChannel::newChannel(
                    *evbThread->getEventBase(), [port](folly::EventBase& evb) {
                      auto socket = folly::AsyncSocket::UniquePtr(
                          new folly::AsyncSocket(&evb));
                      socket->connect(
                          nullptr, folly::SocketAddress("::1", port));
                      auto channel =
                          apache::thrift::RocketClientChannel::newChannel(
                              std::move(socket));
                      channel->setTimeout(FLAGS_hwswitch_query_timeout * 1000);
                      return channel;
                    }));
          });
  return std::make_unique<apache::thrift::Client<utility::AgentHwTestCtrl>>(
      std::move(reconnectingChannel));
}

/**
 * Creates an overridden AgentConfig object by incorporating the overridden
 * initial configuration  and command line args, with the platform config from
 * the test configuration in configerator. This config is dumped for hw-agents
 * and for some warmboot tests.
 */
void AgentEnsemble::createAndDumpOverriddenAgentConfig() {
  XLOG(DBG2) << "Creating overridden agent config";
  CHECK(initialConfig_ != cfg::SwitchConfig());
  auto testConfig = AgentConfig::fromFile(configFile_);

  // Create base agent config with command line args
  std::map<std::string, std::string> defaultCommandLineArgs;
  std::vector<gflags::CommandLineFlagInfo> flags;
  gflags::GetAllFlags(&flags);
  for (const auto& flag : flags) {
    // Skip writing flags if config is itself.
    if (flag.name != kConfig) {
      defaultCommandLineArgs.emplace(flag.name, flag.current_value);
    }
  }

  // Build the new agent config
  cfg::AgentConfig newAgentConf;
  newAgentConf.defaultCommandLineArgs() = defaultCommandLineArgs;
  newAgentConf.sw() = initialConfig_;
  newAgentConf.platform() = *testConfig->thrift.platform();
  auto inputConfig = AgentConfig::fromFile(FLAGS_config)->thrift;
  if (inputConfig.sw()->sdkVersion().has_value()) {
    newAgentConf.sw()->sdkVersion() = inputConfig.sw()->sdkVersion().value();
  }

  auto agentConfig = AgentConfig(newAgentConf);

  // Create directory and dump ensemble config
  utilCreateDir(AgentDirectoryUtil().agentEnsembleConfigDir());
  auto ensembleConfigPath = AgentDirectoryUtil().agentEnsembleConfigDir() +
      kOverriddenAgentConfigFile;
  agentConfig.dumpConfig(ensembleConfigPath);
  XLOG(DBG2) << "Dumped ensemble config to " << ensembleConfigPath;

  // Handle hardware agent config for multi-switch setups
  if (FLAGS_multi_switch ||
      folly::get_default(defaultCommandLineArgs, kMultiSwitch, "") == "true") {
    for (const auto& [_, switchInfo] :
         *newAgentConf.sw()->switchSettings()->switchIdToSwitchInfo()) {
      auto hwAgentConfigPath = AgentDirectoryUtil().getTestHwAgentConfigFile(
          *switchInfo.switchIndex());
      agentConfig.dumpConfig(hwAgentConfigPath);
      XLOG(DBG2) << "Dumped hw_agent config for switch index "
                 << *switchInfo.switchIndex() << " to " << hwAgentConfigPath;
    }
  }
}

std::optional<VlanID> AgentEnsemble::getVlanIDForTx() const {
  auto intf = firstInterfaceWithPortsForTesting(getProgrammedState());
  return getSw()->getVlanIDForTx(intf);
}

std::vector<FirmwareInfo> AgentEnsemble::getAllFirmwareInfo(
    SwitchID switchId) const {
  return getSw()->getHwSwitchThriftClientTable()->getAllFirmwareInfo(switchId);
}

/**
 * Retrieves monitoring counters that match a given regex pattern for a specific
 * port.
 *
 * @details
 * Works in both mono-switch and multi-switch environments.
 *
 * @param portId The ID of the port for which to retrieve counters.
 * @param regex The regex pattern to match against the counter names.
 *
 * @return A map of counter names to their respective values that match the
 * regex pattern.
 */
std::map<std::string, int64_t> AgentEnsemble::getFb303CountersByRegex(
    const PortID& portId,
    const std::string& regex) {
  std::map<std::string, int64_t> counters;
  auto switchID = scopeResolver().scope(portId).switchId();
#ifndef IS_OSS
  auto client = getSw()->getHwSwitchThriftClientTable()->getClient(switchID);
  apache::thrift::Client<facebook::thrift::Monitor> monitoringClient{
      client->getChannelShared()};
  monitoringClient.sync_getRegexCounters(counters, regex);
#else
  counters = queryHwAgentFb303RegexCounters(switchID, regex);
#endif
  return counters;
}

/**
 * Retrieves the value of a specific fb303 counter for a given switch.
 *
 * @details
 * Works in both mono-switch and multi-switch environments.
 *
 * @param key The name of the counter to retrieve.
 * @param switchID The ID of the switch for which to retrieve the counter.
 *
 * @return The value of the specified counter.
 */
int64_t AgentEnsemble::getFb303Counter(
    const std::string& key,
    const SwitchID& switchID) {
  int64_t counter{0};
#ifndef IS_OSS
  auto client = getSw()->getHwSwitchThriftClientTable()->getClient(switchID);
  apache::thrift::Client<facebook::thrift::Monitor> monitoringClient{
      client->getChannelShared()};
  counter = monitoringClient.sync_getCounter(key);
#else
  counter = queryHwAgentFb303Counter(switchID, key);
#endif
  return counter;
}

/**
 * Retrieves the value of the first counter that matches a given regex pattern
 * for a specific port.
 *
 * @details
 * Works in both mono-switch and multi-switch environments.
 *
 * @param portId The ID of the port for which to retrieve the counter.
 * @param regex The regex pattern to match against counter names.
 *
 * @return The value of the first matching counter if one exists, otherwise
 * nullopt.
 */
std::optional<int64_t> AgentEnsemble::getFb303CounterIfExists(
    const PortID& portId,
    const std::string& regex) {
  auto counters = getFb303CountersByRegex(portId, regex);
  if (!counters.empty()) {
    return counters.begin()->second;
  }
  return std::nullopt;
}

/**
 * Retrieves monitoring counters that match a given regex pattern for a specific
 * switch.
 *
 * @details
 * Works in both mono-switch and multi-switch environments.
 * @param regex The regex pattern to match against the counter names.
 * @param switchID The ID of the switch for which to retrieve counters.
 *
 * @return A map of counter names to their respective values that match the
 * regex pattern.
 */
std::map<std::string, int64_t> AgentEnsemble::getFb303RegexCounters(
    const std::string& regex,
    const SwitchID& switchID) {
  std::map<std::string, int64_t> counters;
#ifndef IS_OSS
  auto client = getSw()->getHwSwitchThriftClientTable()->getClient(switchID);
  apache::thrift::Client<facebook::thrift::Monitor> monitoringClient{
      client->getChannelShared()};
  monitoringClient.sync_getRegexCounters(counters, regex);
#else
  counters = queryHwAgentFb303RegexCounters(switchID, regex);
#endif
  return counters;
}

std::map<std::string, int64_t> AgentEnsemble::queryHwAgentFb303RegexCounters(
    const SwitchID& switchID,
    const std::string& regex) {
  std::map<std::string, int64_t> counters;
  if (!getSw()->isRunModeMultiSwitch()) {
    counters = facebook::fb303::fbData->getRegexCounters(regex);
  } else {
    auto hwTestClient = getHwAgentTestClient(switchID);
    hwTestClient->sync_getFb303RegexCounters(counters, regex);
  }
  return counters;
}

int64_t AgentEnsemble::queryHwAgentFb303Counter(
    const SwitchID& switchID,
    const std::string& key) {
  int64_t counter{0};
  if (!getSw()->isRunModeMultiSwitch()) {
    // getCounterIfExists (not getCounter) so a missing key yields 0 instead of
    // throwing, matching the multi-switch handler.
    counter = facebook::fb303::fbData->getCounterIfExists(key).value_or(0);
  } else {
    auto hwTestClient = getHwAgentTestClient(switchID);
    counter = hwTestClient->sync_getFb303Counter(key);
  }
  return counter;
}

std::string AgentEnsemble::getHwDebugDump() {
  std::string out{};
  ThriftHandler(getSw()).getHwDebugDump(out);
  return out;
}

cfg::SwitchingMode AgentEnsemble::getFwdSwitchingMode(
    const RoutePrefixV6& prefix) {
  auto resolvedRoute = findRoute<folly::IPAddressV6>(
      RouterID(0), {prefix.network(), prefix.mask()}, getProgrammedState());
  return getFwdSwitchingMode(resolvedRoute->getForwardInfo());
}

} // namespace facebook::fboss
