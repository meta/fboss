// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "fboss/agent/LldpManager.h"
#include "fboss/agent/Main.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/test/AgentTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/types.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"

#include <boost/container/flat_set.hpp>
#include "fboss/agent/test/link_tests/gen-cpp2/link_test_production_features_types.h"

// TODO Movng these to Linktestutils.h causes linker error. Resolve and move
// them
DECLARE_bool(setup_for_warmboot);
DECLARE_string(config);
DECLARE_string(volatile_state_dir);
DECLARE_bool(disable_neighbor_updates);
DECLARE_bool(enable_macsec);
DECLARE_bool(list_production_feature);

namespace facebook::fboss {

using namespace std::chrono_literals;

// When we switch to use qsfp_service to collect stats(PhyInfo), default stats
// collection frequency is 60s. Give the maximum check time 5x24=120s here.
constexpr auto kSecondsBetweenXphyInfoCollectionCheck = 5s;
constexpr auto kMaxNumXphyInfoCollectionCheck = 24;

class LinkTest : public AgentTest {
 public:
  bool sendAndCheckReachabilityOnAllCabledPorts() {
    sw()->getLldpMgr()->sendLldpOnAllPorts();
    return checkReachabilityOnAllCabledPorts();
  }

 protected:
  void SetUp() override;
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
   * Return plugged in Optical and Active Electrical Cable transceivers and
   * their names.
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
  const std::vector<PortID>& getCabledTransceiverPorts() const {
    return cabledTransceiverPorts_;
  }
  const std::set<TransceiverID>& getCabledTranceivers() const {
    return cabledTransceivers_;
  }
  boost::container::flat_set<PortDescriptor> getSingleVlanOrRoutedCabledPorts()
      const;
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

  void checkAgentMemoryInBounds() const;

  /*
   * Program default (v6) route over ports
   */

  void programDefaultRoute(
      const boost::container::flat_set<PortDescriptor>& ecmpPorts,
      std::optional<folly::MacAddress> dstMac = std::nullopt,
      bool disableTTLDecrement = false);

  /*
   * Create a L3 data plane loop and seed it with traffic
   */
  void createL3DataplaneFlood(
      const boost::container::flat_set<PortDescriptor>& inPorts);
  void createL3DataplaneFlood() {
    createL3DataplaneFlood(getSingleVlanOrRoutedCabledPorts());
  }
  std::string getPortName(PortID port) const;
  std::vector<std::string> getPortName(
      const std::vector<PortID>& portIDs) const;

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

  std::vector<std::pair<PortID, PortID>> getPortPairsForFecErrInj() const;

  void printProductionFeatures() const;

 private:
  void programDefaultRoute(
      const boost::container::flat_set<PortDescriptor>& ecmpPorts,
      utility::EcmpSetupTargetedPorts6& ecmp6,
      bool disableTTLDecrement = false);
  void initializeCabledPorts();
  void logLinkDbgMessage(std::vector<PortID>& portIDs) const override;

  virtual std::vector<link_test_production_features::LinkTestProductionFeature>
  getProductionFeatures() const;

  std::vector<PortID> cabledPorts_;
  std::vector<PortID> cabledFabricPorts_;
  std::vector<PortID> cabledTransceiverPorts_;
  std::set<TransceiverID> cabledTransceivers_;
  std::vector<PortID> qsfpServiceManagedPorts_;
};
int linkTestMain(
    int argc,
    char** argv,
    PlatformInitFn initPlatformFn,
    std::optional<cfg::StreamType> streamType = std::nullopt);
} // namespace facebook::fboss
