// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <fboss/agent/gen-cpp2/agent_config_types.h>
#include <fboss/agent/gen-cpp2/platform_config_types.h>
#include <fboss/agent/gen-cpp2/switch_config_types.h>
#include <fboss/agent/hw/bcm/gen-cpp2/bcm_config_types.h>
#include <unistd.h>
#include <chrono>
#include <functional>
#include <optional>
#include "fboss/agent/L2Entry.h"
#include "fboss/agent/Main.h"

#include "fboss/agent/FbossInit.h"
#include "fboss/agent/HwSwitchThriftClientTable.h"
#include "fboss/agent/SwAgentInitializer.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwSwitchWarmBootHelper.h"
#include "fboss/agent/test/LinkStateToggler.h"
#include "fboss/agent/test/RouteDistributionGenerator.h"
#include "fboss/agent/test/TestEnsembleIf.h"

#include "fboss/agent/if/gen-cpp2/AgentHwTestCtrl.h"

DECLARE_string(config);
DECLARE_bool(setup_for_warmboot);

namespace facebook::fboss {
class StateObserver;
class AgentEnsemble;
class HwSwitch;

using AgentEnsembleSwitchConfigFn =
    std::function<cfg::SwitchConfig(const AgentEnsemble&)>;
using AgentEnsemblePlatformConfigFn =
    std::function<void(const cfg::SwitchConfig& sw, cfg::PlatformConfig&)>;

class AgentEnsemble : public TestEnsembleIf {
 public:
  AgentEnsemble() : AgentEnsemble("agent.conf") {}
  explicit AgentEnsemble(const std::string& configFileName);
  virtual ~AgentEnsemble() override;
  using StateUpdateFn = SwSwitch::StateUpdateFn;
  using TestEnsembleIf::getLatestInterfaceStats;
  using TestEnsembleIf::getLatestPortStats;
  using TestEnsembleIf::getLatestSysPortStats;

  void setupEnsemble(
      AgentEnsembleSwitchConfigFn initConfig,
      bool disableLinkStateToggler,
      AgentEnsemblePlatformConfigFn platformConfig =
          AgentEnsemblePlatformConfigFn(),
      uint32_t featuresDesired =
          (HwSwitch::FeaturesDesired::PACKET_RX_DESIRED |
           HwSwitch::FeaturesDesired::LINKSCAN_DESIRED |
           HwSwitch::FeaturesDesired::TAM_EVENT_NOTIFY_DESIRED),
      const TestEnsembleInitInfo& initInfo = TestEnsembleInitInfo{
          .failHwCallsOnWarmboot = false});

  void startAgent(bool failHwCallsOnWarmboot = false);

  void applyNewConfig(const cfg::SwitchConfig& config, bool activate);

  void setupLinkStateToggler();

  SwSwitch* getSw() const {
    return agentInitializer()->sw();
  }

  std::shared_ptr<SwitchState> getProgrammedState() const override {
    return getSw()->getState();
  }

  const std::map<int32_t, cfg::PlatformPortEntry>& getPlatformPorts()
      const override {
    return getSw()->getPlatformMapping()->getPlatformPorts();
  }

  void applyInitialConfig(const cfg::SwitchConfig& config) override {
    // agent will start after writing an initial config.
    applyNewConfig(config, false);
  }

  std::shared_ptr<SwitchState> applyNewConfig(
      const cfg::SwitchConfig& config) override {
    applyNewConfig(config, true);
    return getProgrammedState();
  }

  std::unique_ptr<RouteUpdateWrapper> getRouteUpdaterWrapper() override {
    return std::make_unique<SwSwitchRouteUpdateWrapper>(
        getSw()->getRouteUpdater());
  }

  virtual const SwAgentInitializer* agentInitializer() const = 0;
  virtual SwAgentInitializer* agentInitializer() = 0;
  virtual void createSwitch(
      std::unique_ptr<AgentConfig> config,
      uint32_t hwFeaturesDesired,
      PlatformInitFn initPlatform) = 0;
  virtual void reloadPlatformConfig() = 0;

  void applyNewState(
      StateUpdateFn fn,
      const std::string& name = "test-update",
      bool transaction = false) override;

  std::vector<PortID> getAllMasterLogicalPortIds() const override;

  std::optional<size_t> getMaxRequiredPorts(
      cfg::PortType portType) const override;

  void switchRunStateChanged(SwitchRunState runState) override;

  void programRoutes(
      const RouterID& rid,
      const ClientID& client,
      const utility::RouteDistributionGenerator::ThriftRouteChunks&
          routeChunks);

  void unprogramRoutes(
      const RouterID& rid,
      const ClientID& client,
      const utility::RouteDistributionGenerator::ThriftRouteChunks&
          routeChunks);

  void stopStatsThread();
  void gracefulExit();

  static void enableExactMatch(bcm::BcmConfig& config);

  std::string getInputConfigFile();

  void packetReceived(std::unique_ptr<RxPacket> pkt) noexcept override {
    getSw()->packetReceived(std::move(pkt));
  }

  void linkStateChanged(
      PortID port,
      bool up,
      cfg::PortType portType,
      std::optional<phy::LinkFaultStatus> iPhyFaultStatus = std::nullopt,
      std::optional<AggregatePortID> aggPortId = std::nullopt) override {
    if (linkToggler_ &&
        getSw()->getSwitchRunState() >= SwitchRunState::CONFIGURED) {
      linkToggler_->linkStateChanged(port, up);
      getSw()->linkStateChanged(port, up, portType);
    } else if (
        !linkToggler_ &&
        getSw()->getSwitchRunState() >= SwitchRunState::INITIALIZED) {
      getSw()->linkStateChanged(port, up, portType);
    } else {
      XLOG(DBG2) << "Agent Ensemble dropping link state change for port "
                 << port << " UP:" << up;
    }
  }

  void linkActiveStateChangedOrFwIsolated(
      const std::map<PortID, bool>& port2IsActive,
      bool fwIsolated,
      const std::optional<uint32_t>& numActiveFabricPortsAtFwIsolate) override {
    getSw()->linkActiveStateChangedOrFwIsolated(
        port2IsActive, fwIsolated, numActiveFabricPortsAtFwIsolate);
  }
  void linkAdminStateChangedByFw(
      const std::vector<int32_t>& fwDisabledPortIds) override {
    getSw()->linkAdminStateChangedByFw(fwDisabledPortIds);
  }
  void linkConnectivityChanged(
      const std::map<PortID, multiswitch::FabricConnectivityDelta>&
          port2OldAndNewConnectivity) override {
    getSw()->linkConnectivityChanged(port2OldAndNewConnectivity);
  }

  void switchReachabilityChanged(
      const SwitchID switchId,
      const std::map<SwitchID, std::set<PortID>>& switchReachabilityInfo)
      override {
    getSw()->switchReachabilityChanged(switchId, switchReachabilityInfo);
  }

  void l2LearningUpdateReceived(
      L2Entry l2Entry,
      L2EntryUpdateType l2EntryUpdateType) override {
    getSw()->l2LearningUpdateReceived(l2Entry, l2EntryUpdateType);
  }

  void exitFatal() const noexcept override {
    getSw()->exitFatal();
  }

  void pfcWatchdogStateChanged(const PortID& port, const bool deadlock)
      override {
    getSw()->pfcWatchdogStateChanged(port, deadlock);
  }

  std::map<PortID, HwPortStats> getLatestPortStats(
      const std::vector<PortID>& ports) override;

  std::map<InterfaceID, HwRouterInterfaceStats> getLatestInterfaceStats(
      const std::vector<InterfaceID>& interfaces) override;

  std::map<SystemPortID, HwSysPortStats> getLatestSysPortStats(
      const std::vector<SystemPortID>& ports) override;

  void setLoopbackMode(cfg::PortLoopbackMode mode) {
    mode_ = mode;
  }

  const SwitchIdScopeResolver& scopeResolver() const override {
    CHECK(getSw()->getScopeResolver());
    return *(getSw()->getScopeResolver());
  }

  HwAsicTable* getHwAsicTable() override {
    return getSw()->getHwAsicTable();
  }

  const HwAsicTable* getHwAsicTable() const override {
    return getSw()->getHwAsicTable();
  }

  std::map<PortID, FabricEndpoint> getFabricConnectivity(
      SwitchID switchId) const override;

  FabricReachabilityStats getFabricReachabilityStats() const override {
    return getSw()->getFabricReachabilityStats();
  }

  void updateStats() override {
    getSw()->updateStats();
  }

  void registerStateObserver(StateObserver* observer, const std::string& name)
      override;
  void unregisterStateObserver(StateObserver* observer) override;

  void runDiagCommand(
      const std::string& input,
      std::string& output,
      std::optional<SwitchID> switchId = std::nullopt) override;

  void runCint(
      const std::string& cintData,
      std::string& output,
      const SwitchID& switchId);

  void clearPortStats(
      const std::unique_ptr<std::vector<int32_t>>& ports) override;

  void clearPortStats();

  LinkStateToggler* getLinkToggler() override;

  folly::MacAddress getLocalMac(SwitchID id) const override {
    return getSw()->getLocalMac(id);
  }
  void waitForLineRateOnPort(PortID port);
  void waitForSpecificRateOnPort(
      PortID port,
      const uint64_t desiredBps,
      int secondsToWaitPerIteration = 1);

  void sendPacketAsync(
      std::unique_ptr<TxPacket> pkt,
      std::optional<PortDescriptor> portDescriptor,
      std::optional<uint8_t> queueId,
      std::optional<SwitchID> switchId = std::nullopt) override;

  void sendPacketSwitchedAsync(
      std::unique_ptr<TxPacket> pkt,
      const std::optional<SwitchID>& switchId) override;

  std::unique_ptr<TxPacket> allocatePacket(uint32_t size) override;

  bool supportsAddRemovePort() const override {
    return getSw()->getPlatformSupportsAddRemovePort();
  }

  const PlatformMapping* getPlatformMapping() const override {
    return getSw()->getPlatformMapping();
  }

  void bringUpPort(PortID port) {
    bringUpPorts({port});
  }
  void bringDownPort(PortID port) {
    bringDownPorts({port});
  }
  void bringUpPorts(const std::vector<PortID>& ports);
  void bringDownPorts(const std::vector<PortID>& ports);

  cfg::SwitchConfig getCurrentConfig() const override {
    return getSw()->getConfig();
  }
  bool ensureSendPacketSwitched(
      std::unique_ptr<TxPacket> pkt,
      const std::optional<SwitchID>& switchId = std::nullopt);
  bool ensureSendPacketOutOfPort(
      std::unique_ptr<TxPacket> pkt,
      PortID portID,
      std::optional<uint8_t> queue = std::nullopt);

  virtual void ensureHwSwitchConnected(SwitchID switchId) = 0;
  uint64_t getTrafficRate(
      const HwPortStats& prevPortStats,
      const HwPortStats& curPortStats,
      const int secondsBetweenStatsCollection);

  // The timeout defaults to --hwswitch_query_timeout seconds.
  std::unique_ptr<apache::thrift::Client<utility::AgentHwTestCtrl>>
  getHwAgentTestClient(
      SwitchID switchId,
      int retries = 2,
      std::optional<std::chrono::milliseconds> timeout = std::nullopt);
  // Delivers the event to the test-lifecycle observers of the switch under test
  // (--switch_id_for_testing): in process in mono mode, and over thrift to its
  // hardware agent in multi-switch mode. Returns whether they accepted it.
  bool notifyTestLifecycleEvent(
      const std::string& attemptUuid,
      utility::TestLifecycleEvent event);
  BootType getBootType() const;
  static void writeConfig(
      const cfg::AgentConfig& config,
      const std::string& file);

  const std::string& configFileName() const {
    return configFile_;
  }

  bool needL2EntryForNeighbor() const override {
    return getSw()->needL2EntryForNeighbor();
  }

  std::optional<VlanID> getVlanIDForTx() const override;

  std::vector<FirmwareInfo> getAllFirmwareInfo(
      SwitchID switchId) const override;

  /**
   * Retrieves monitoring counters that match a given regex pattern for a
   * specific port.
   *
   * * @details
   * Works in both mono-switch and multi-switch environments:
   *
   * @param portId - The identifier of the port for which to retrieve counters.
   * @param regex - The regex pattern to match against the monitoring counters.
   * @return A map of counter names to their respective values that match the
   * regex.
   */
  std::map<std::string, int64_t> getFb303CountersByRegex(
      const PortID& portId,
      const std::string& regex);

  /**
   * Retrieves monitoring counters that match a given regex pattern for a
   * specific switch.
   *
   * @details
   * Works in both mono-switch and multi-switch environments.
   * @param regex The regex pattern to match against the counter names.
   * @param switchID The ID of the switch for which to retrieve counters.
   *
   * @return A map of counter names to their respective values that match the
   * regex pattern.
   */
  std::map<std::string, int64_t> getFb303RegexCounters(
      const std::string& regex,
      const SwitchID& switchID);

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
  int64_t getFb303Counter(const std::string& key, const SwitchID& switchID);

  /**
   * Retrieves the value of the first counter that matches a given regex
   * pattern for a specific port.
   *
   * @details
   * Works in both mono-switch and multi-switch environments.
   *
   * @param portId The ID of the port for which to retrieve the counter.
   * @param regex The regex pattern to match against counter names.
   *
   * @return The value of the first matching counter if one exists,
   * otherwise nullopt.
   */
  std::optional<int64_t> getFb303CounterIfExists(
      const PortID& portId,
      const std::string& regex);

  std::string getHwDebugDump();

  virtual cfg::SwitchingMode getFwdSwitchingMode(
      const RouteNextHopEntry& entry) = 0;

  cfg::SwitchingMode getFwdSwitchingMode(const RoutePrefixV6& prefix);

 protected:
  void joinAsyncInitThread() {
    if (asyncInitThread_) {
      asyncInitThread_->join();
      asyncInitThread_.reset();
    }
  }

 private:
  void interleavePortsAcrossDies(
      const std::map<int32_t, cfg::PlatformPortEntry>& platformPorts);
  void setConfigFiles(const std::string& fileName);
  void setBootType();
  void overrideConfigFlag(const std::string& fileName);
  void dumpConfigForHwAgent(AgentConfig* agentConf);

  std::map<std::string, int64_t> queryHwAgentFb303RegexCounters(
      const SwitchID& switchId,
      const std::string& regex);
  int64_t queryHwAgentFb303Counter(
      const SwitchID& switchId,
      const std::string& key);

  void writeConfig(const cfg::SwitchConfig& config);
  void writeConfig(const cfg::AgentConfig& config);
  bool waitForRateOnPort(
      PortID port,
      uint64_t desiredBps,
      int secondsToWaitPerIteration = 2);

  /**
   * Creates an overridden AgentConfig object by incorporating the overridden
   * initial configuration  and command line args, with the platform config from
   * the test configuration in configerator. This config is dumped for hw-agents
   * and for some warmboot tests.
   */
  void createAndDumpOverriddenAgentConfig();

  cfg::SwitchConfig initialConfig_;
  std::unique_ptr<std::thread> asyncInitThread_{nullptr};
  std::map<SwitchID, std::vector<PortID>>
      switchId2PortIds_; /* per switch ports; the sole master-port store */
  // Per-switch caps applied dynamically by the typed masterLogical*PortIds()
  // accessors (see getMaxRequiredPorts). nullopt means no cap.
  std::optional<size_t> maxRequiredInterfacePorts_;
  std::optional<size_t> maxRequiredFabricPorts_;
  std::string configFile_{};
  std::unique_ptr<LinkStateToggler> linkToggler_;
  cfg::PortLoopbackMode mode_{cfg::PortLoopbackMode::MAC};
  BootType bootType_{BootType::COLD_BOOT};
};

void initEnsemble(
    PlatformInitFn initPlatform,
    std::optional<cfg::StreamType> streamType = std::nullopt);

std::unique_ptr<AgentEnsemble> createAgentEnsemble(
    AgentEnsembleSwitchConfigFn initialConfigFn,
    bool disableLinkStateToggler,
    AgentEnsemblePlatformConfigFn platformConfigFn =
        AgentEnsemblePlatformConfigFn(),
    uint32_t featuresDesired =
        (HwSwitch::FeaturesDesired::PACKET_RX_DESIRED |
         HwSwitch::FeaturesDesired::LINKSCAN_DESIRED |
         HwSwitch::FeaturesDesired::TAM_EVENT_NOTIFY_DESIRED),
    const TestEnsembleInitInfo& initInfo = TestEnsembleInitInfo{
        .failHwCallsOnWarmboot = false});

} // namespace facebook::fboss
