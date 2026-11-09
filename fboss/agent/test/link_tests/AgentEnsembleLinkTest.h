// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <boost/container/flat_set.hpp>
#include "fboss/agent/LldpManager.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/test/AgentEnsembleTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/link_tests/LinkTestUtils.h"
#include "fboss/agent/test/link_tests/gen-cpp2/link_test_production_features_types.h"
#include "fboss/agent/types.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/qsfp_clients.h"

DECLARE_string(config);
DECLARE_bool(disable_neighbor_updates);
DECLARE_bool(list_production_feature);

namespace facebook::fboss {

using namespace std::chrono_literals;

// When we switch to use qsfp_service to collect stats(PhyInfo), default stats
// collection frequency is 60s. Give the maximum check time 5x24=120s here.
constexpr auto kSecondsBetweenXphyInfoCollectionCheck = 5s;
constexpr auto kMaxNumXphyInfoCollectionCheck = 24;

class AgentEnsembleLinkTest : public AgentEnsembleTest {
 public:
  bool sendAndCheckReachabilityOnAllCabledPorts() {
    getSw()->getLldpMgr()->sendLldpOnAllPorts();
    return checkReachabilityOnAllCabledPorts();
  }

 protected:
  void SetUp() override;
  // Recompute the cabled port set from the currently applied config. Tests that
  // reprogram ports after SetUp() (e.g. speed change, which can subsume/disable
  // or add ports) must call this so the cabled port set no longer references
  // ports that were removed from the SwitchState.
  void reinitializeCabledPorts();
  void overrideL2LearningConfig(bool swLearning = false, int ageTimer = 300);
  void setupTtl0ForwardingEnable();
  void waitForAllCabledPorts(
      bool up,
      uint32_t retries = 60,
      std::chrono::duration<uint32_t, std::milli> msBetweenRetry =
          std::chrono::milliseconds(1000)) const;
  void waitForPortStateMachineState(
      bool up,
      uint32_t retries = 60,
      std::chrono::duration<uint32_t, std::milli> msBetweenRetry =
          std::chrono::milliseconds(1000)) const;
  bool checkReachabilityOnAllCabledPorts() const;
  /*
   * Get pairs of ports connected to each other
   */
  std::set<std::pair<PortID, PortID>> getConnectedPairs() const;

  /*
   * Return plugged in optical transceivers and their names. Active electrical
   * cables are included too, unless opticalOnly is set, which restricts the
   * result to transceivers reporting TransmitterTechnology::OPTICAL.
   */
  std::tuple<std::vector<PortID>, std::string>
  getOpticalAndActiveCabledPortsAndNames(
      bool pluggableOnly = false,
      bool opticalOnly = false) const;

  /*
   * Ports where we expect optics to be plugged in.
   * In link tests this information is conveyed in config via non
   * null LLDP neighbors. We pick that up here to extract cabled ports
   */
  const std::vector<PortID>& getCabledPorts() const;
  const std::vector<PortID>& getCabledTransceiverPorts() const;
  const std::set<TransceiverID>& getCabledTranceivers() const {
    return cabledTransceivers_;
  }
  boost::container::flat_set<PortDescriptor> getSingleVlanOrRoutedCabledPorts(
      std::optional<SwitchID> switchId = std::nullopt) const;
  const std::vector<PortID>& getCabledFabricPorts() const {
    return cabledFabricPorts_;
  }
  /*
   * Get cabled ports that are managed by qsfp_service (i.e., ports with
   * TRANSCEIVER or XPHY chips). Ports connected directly to backplane
   * without XPHY are not managed by qsfp_service.
   */
  const std::vector<PortID>& getQsfpServiceManagedPorts() const {
    return qsfpServiceManagedPorts_;
  }

  std::vector<PortID> getXphyCabledPorts() const {
    std::vector<PortID> xphyPorts;
    auto& ports = getCabledPorts();
    for (const auto& port : ports) {
      if (utility::getPortExternalPhyID(getSw(), port).has_value()) {
        xphyPorts.push_back(port);
      }
    }
    return xphyPorts;
  }

  /*
   * Get Primary port names (e.g. eth1/1/1) from list of cabled ports
   */
  std::vector<std::string> getPrimaryPortNamesCabledPorts();

  /*
   * Dumps The transceiver I2c Logs at the end of a test run.
   */
  void dumpTransceiverI2cLogs(
      apache::thrift::Client<facebook::fboss::QsfpService>& qsfpClient);

  void checkAgentMemoryInBounds() const;

  /*
   * Program default (v6) route over ports
   */

  void programDefaultRoute(
      const boost::container::flat_set<PortDescriptor>& ecmpPorts,
      utility::EcmpSetupTargetedPorts6& ecmp6);
  void programDefaultRouteWithDisableTTLDecrement(
      const boost::container::flat_set<PortDescriptor>& ecmpPorts,
      utility::EcmpSetupTargetedPorts6& ecmp6);
  void programDefaultRoute(
      const boost::container::flat_set<PortDescriptor>& ecmpPorts,
      std::optional<folly::MacAddress> dstMac = std::nullopt);

  /*
   * Create a L3 data plane loop and seed it with traffic
   */
  void createL3DataplaneFlood(
      const boost::container::flat_set<PortDescriptor>& inPorts);
  void createL3DataplaneFlood() {
    createL3DataplaneFlood(getSingleVlanOrRoutedCabledPorts());
  }

  std::optional<PortID> getPeerPortID(
      PortID portId,
      const std::set<std::pair<PortID, PortID>>& connectedPairs) const;

  std::set<std::pair<PortID, PortID>>
  getConnectedOpticalAndActivePortPairWithFeature(
      TransceiverFeature feature,
      phy::Side side,
      bool skipLoopback = false,
      bool opticalModulesOnly = false) const;

  void waitForLldpOnCabledPorts(
      uint32_t retries = 60,
      std::chrono::duration<uint32_t, std::milli> msBetweenRetry =
          std::chrono::milliseconds(1000)) const;

  void setCmdLineFlagOverrides() const override;

  void TearDown() override;

  void setLinkState(bool enable, std::vector<PortID>& portIds);

  phy::FecMode getPortFECMode(PortID portId) const;

  std::vector<std::pair<PortID, PortID>> getPortPairsForFecErrInj() const;

  void setForceTrafficOverFabric(bool force);

  void printProductionFeatures() const;

  void addTestedPort(const PortID& portId);
  void addTestedPorts(const std::vector<PortID>& portIds);
  void addTestMetadata(
      const PortID& portId,
      const std::string& key,
      const std::string& value);

  // Declare the production feature(s) this test verifies (e.g. LINK_BRINGUP,
  // FEC). Call from the test body. The feature names are recorded on every
  // dumped Scuba row for the test. This is independent of
  // getProductionFeatures() (the L1/L2 gating method consumed by
  // --list_production_feature) and is used only for metadata/Scuba reporting.
  void addVerifiedProductionFeatures(
      const std::vector<
          link_test_production_features::LinkTestProductionFeature>& features);

 private:
  void initializeCabledPorts();
  void logLinkDbgMessage(std::vector<PortID>& portIDs) const override;

  virtual std::vector<link_test_production_features::LinkTestProductionFeature>
  getProductionFeatures() const;

  void dumpTestMetadata() override;

  std::vector<PortID> cabledPorts_;
  std::vector<PortID> cabledFabricPorts_;
  std::set<TransceiverID> cabledTransceivers_;
  std::vector<PortID> cabledTransceiverPorts_;
  std::vector<PortID> qsfpServiceManagedPorts_;
  std::set<PortID> testedPorts_;
  std::map<PortID, std::map<std::string, std::string>> perPortMetadata_;
  std::vector<link_test_production_features::LinkTestProductionFeature>
      verifiedProductionFeatures_;
};
int agentEnsembleLinkTestMain(
    int argc,
    char** argv,
    PlatformInitFn initPlatformFn,
    std::optional<cfg::StreamType> streamType = std::nullopt);
} // namespace facebook::fboss
