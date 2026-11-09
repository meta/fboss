// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <fmt/format.h>
#include <folly/ScopeGuard.h>
#include <folly/String.h>
#include <folly/logging/xlog.h>

#include <thrift/lib/cpp/util/EnumUtils.h>

#include "fboss/agent/FabricConnectivityManager.h"
#include "fboss/agent/LldpManager.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/lldp/LinkNeighbor.h"
#include "fboss/agent/lldp/LinkNeighborDB.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortMap.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/link_tests/AgentEnsembleLinkTest.h"
#include "fboss/agent/test/link_tests/LinkTestUtils.h"
#include "fboss/lib/CommonUtils.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"
#include "fboss/lib/thrift_service_client/ThriftServiceClient.h"

using namespace ::testing;

// Same "Transceiver:<id>" prefix qsfp_service's QSFP_LOG emits, so optic logs
// stay greppable across the two services. Spelled out here rather than
// including TransceiverLogging.h, which would drag the whole
// //fboss/qsfp_service:transceiver-manager target into the link tests.
#define TCVR_TEST_LOG(level, tcvrId) \
  XLOG(level) << "Transceiver:" << (tcvrId) << " "

namespace facebook::fboss {

namespace {

// LldpManager transmits every LLDP_INTERVAL (5s), and a fabric port has to
// rediscover its endpoint once the link is disturbed. Give both 180s to
// converge after the datapath is looped or restored.
constexpr uint32_t kConvergenceRetries = 180;
constexpr auto kConvergenceRetryInterval = std::chrono::seconds(1);

struct LoopbackMode {
  phy::Side side;
  phy::PortComponent component;
};

constexpr LoopbackMode kSystemLoopback{
    phy::Side::SYSTEM,
    phy::PortComponent::TRANSCEIVER_SYSTEM};

constexpr LoopbackMode kLineLoopback{
    phy::Side::LINE,
    phy::PortComponent::TRANSCEIVER_LINE};

// One (A, Z) link plus which of its two transceivers can be looped back.
struct PairUnderTest {
  PortID portA;
  PortID portZ;
  int32_t tcvrA{};
  int32_t tcvrZ{};
  bool loopbackA{false};
  bool loopbackZ{false};
};

/*
 * What every port under test must look like at one point in the sequence.
 *
 * An interface port is checked over LLDP, where the expectation is a specific
 * neighbor: the configured peer in steady state, or the port itself while it is
 * hearing its own frames.
 *
 * A fabric port carries no LLDP, so it is checked over the ASIC's fabric
 * endpoint discovery instead: the configured peer in steady state, and its own
 * switch while it is hearing its own cells.
 */
struct PortExpectations {
  std::map<PortID, PortID> lldpNeighbors;
  std::set<PortID> fabricReachable;
  std::set<PortID> fabricUnreachable;

  bool empty() const {
    return lldpNeighbors.empty() && fabricReachable.empty() &&
        fabricUnreachable.empty();
  }
};

} // namespace

class AgentEnsembleLoopbackTest : public AgentEnsembleLinkTest {
 private:
  std::vector<link_test_production_features::LinkTestProductionFeature>
  getProductionFeatures() const override {
    return {
        link_test_production_features::LinkTestProductionFeature::L1_LINK_TEST,
        link_test_production_features::LinkTestProductionFeature::
            ACTIVE_TRANSCEIVER};
  }

 protected:
  std::map<PortID, PortID> getConfiguredNeighbors() const;

  std::optional<PortID> getFreshestLldpNeighbor(PortID port) const;

  std::vector<std::string> checkLldpNeighbors(
      const std::map<PortID, PortID>& expectedNeighbors) const;

  bool isSelfLoop(PortID port, const FabricEndpoint& endpoint) const;

  std::vector<std::string> checkFabricReachability(
      const std::set<PortID>& ports,
      bool expectReachable) const;

  void verifyExpectations(
      const PortExpectations& expectations,
      const std::string& context,
      std::vector<std::string>& failures) const;

  bool expectReachableToPeer(PortID port, PortExpectations& expectations) const;

  void expectLoopedBack(PortID port, PortExpectations& expectations) const;

  std::string getPortNames(const std::vector<PortID>& ports) const;

  bool isFabricPort(PortID port) const;

  std::optional<int32_t> getTransceiverId(PortID port) const;

  std::vector<PairUnderTest> getPairsUnderTest(const LoopbackMode& mode) const;

  void verifyBaseline(const std::vector<PairUnderTest>& pairs);

  void setLoopback(const LoopbackMode& mode, PortID port, bool enable);

  std::vector<std::string> clearLoopback(
      const LoopbackMode& mode,
      const std::vector<PortID>& ports);

  void loopbackAndVerify(
      const LoopbackMode& mode,
      const std::vector<PortID>& loopbackPorts,
      const PortExpectations& looped,
      const PortExpectations& restored,
      std::vector<std::string>& failures);

  void runLoopbacksByEnd(
      const LoopbackMode& mode,
      const std::vector<PairUnderTest>& pairs,
      std::vector<std::string>& failures);

  void runLoopbackTest(const LoopbackMode& mode);

  std::map<PortID, PortID> configuredNeighbors_;
  std::unique_ptr<apache::thrift::Client<QsfpService>> qsfpServiceClient_;
};

/*
 * The agent config carries the expected remote port for every cabled interface
 * port in expectedLLDPValues[PORT]. That is the ground truth the live LLDP
 * neighbor table is compared against, both before the test starts and after
 * every loopback is cleared.
 *
 * Fabric ports are absent from the result by construction: they carry no LLDP
 * and their expected peer arrives in expectedNeighborReachability instead,
 * which the agent folds into the FabricEndpoint that checkFabricReachability()
 * reads.
 */
std::map<PortID, PortID> AgentEnsembleLoopbackTest::getConfiguredNeighbors()
    const {
  std::map<PortID, PortID> configuredNeighbors;
  const auto& config = getSw()->getConfig();
  const auto& ports = *config.ports();
  for (const auto& port : ports) {
    if (*port.state() == cfg::PortState::DISABLED) {
      continue;
    }
    const auto& lldpValues = *port.expectedLLDPValues();
    auto portTagItr = lldpValues.find(cfg::LLDPTag::PORT);
    if (portTagItr == lldpValues.end()) {
      continue;
    }
    try {
      configuredNeighbors.emplace(
          PortID(*port.logicalID()), getPortID(portTagItr->second));
    } catch (const FbossError& ex) {
      // The neighbor lives on this same switch for a link test setup, so a
      // name we can't resolve means the config and the platform mapping
      // disagree. Surface it rather than silently dropping the port.
      XLOG(WARN) << "Could not resolve expected LLDP neighbor '"
                 << portTagItr->second << "' of port " << *port.logicalID()
                 << ": " << ex.what();
    }
  }
  return configuredNeighbors;
}

/*
 * A neighbor entry stays in LinkNeighborDB until the TTL advertised in the
 * frame (120s for FBOSS) elapses, so the pre-loopback entry is still present
 * while the loopback is up. Every FBOSS neighbor advertises the same TTL, so
 * the entry with the latest expiration is the one refreshed most recently,
 * i.e. the one describing what the port is receiving right now.
 */
std::optional<PortID> AgentEnsembleLoopbackTest::getFreshestLldpNeighbor(
    PortID port) const {
  std::shared_ptr<LinkNeighbor> freshest;
  auto lldpDb = getSw()->getLldpMgr()->getDB();
  // getNeighbors() snapshots under the DB's lock and returns by value.
  const auto& neighbors = lldpDb->getNeighbors(port);
  for (const auto& neighbor : neighbors) {
    if (!freshest ||
        neighbor->getExpirationTime() > freshest->getExpirationTime()) {
      freshest = neighbor;
    }
  }
  if (!freshest) {
    return std::nullopt;
  }
  try {
    return getPortID(freshest->getPortId());
  } catch (const FbossError&) {
    return std::nullopt;
  }
}

// Returns one human readable reason per port whose freshest LLDP neighbor is
// not the expected one, so an empty result means every expectation holds.
std::vector<std::string> AgentEnsembleLoopbackTest::checkLldpNeighbors(
    const std::map<PortID, PortID>& expectedNeighbors) const {
  std::vector<std::string> errors;
  for (const auto& [port, expectedNeighbor] : expectedNeighbors) {
    auto neighbor = getFreshestLldpNeighbor(port);
    if (!neighbor.has_value()) {
      errors.push_back(fmt::format("{}: no LLDP neighbor", getPortName(port)));
    } else if (*neighbor != expectedNeighbor) {
      errors.push_back(
          fmt::format(
              "{}: LLDP neighbor is {}, expected {}",
              getPortName(port),
              getPortName(*neighbor),
              getPortName(expectedNeighbor)));
    }
  }
  return errors;
}

/*
 * Whether a fabric port is hearing its own switch rather than a remote one.
 *
 * This is the same range check the agent uses to raise
 * CABLING_ERROR_LOOP_DETECTED (see LinkConnectivityProcessor): one ASIC covers
 * getVirtualDevices() consecutive SwitchIDs starting at the local base, so a
 * self loop is an endpoint anywhere in that range, not just an exact SwitchID
 * match.
 */
bool AgentEnsembleLoopbackTest::isSelfLoop(
    PortID port,
    const FabricEndpoint& endpoint) const {
  if (!*endpoint.isAttached()) {
    return false;
  }
  const auto localBaseSwitchId =
      getSw()->getScopeResolver()->scope(port).switchId();
  const auto* localAsic =
      getSw()->getHwAsicTable()->getHwAsic(localBaseSwitchId);
  const auto connectedSwitchId = SwitchID(*endpoint.switchId());
  return connectedSwitchId >= localBaseSwitchId &&
      connectedSwitchId <
      SwitchID(
          static_cast<int>(localBaseSwitchId) + localAsic->getVirtualDevices());
}

/*
 * A fabric port has no LLDP, so whether it is still connected to the peer the
 * config expects comes from the ASIC's fabric endpoint discovery instead.
 * isConnectivityInfoMismatch() is the same comparison the agent uses to flag a
 * mis-cabled fabric link, i.e. observed switch/port against the
 * expectedNeighborReachability the port was configured with.
 *
 * The looped expectation is the fabric twin of "the port sees itself as its own
 * LLDP neighbor": the endpoint must be this switch. A port that has gone down
 * instead also satisfies it, because whether a transceiver loopback makes the
 * ASIC rediscover itself or drop the link is ASIC dependent. What must not
 * satisfy it is a live endpoint that is some *other* switch -- that is
 * mis-cabling, and merely asking for "not the configured peer" would let it
 * pass as a working loopback. The endpoint actually observed is logged either
 * way so the real behaviour is visible in the run.
 */
std::vector<std::string> AgentEnsembleLoopbackTest::checkFabricReachability(
    const std::set<PortID>& ports,
    bool expectReachable) const {
  auto describe = [this](PortID port, const FabricEndpoint& endpoint) {
    if (!*endpoint.isAttached()) {
      return std::string("not attached");
    }
    return fmt::format(
        "attached to switch {} port {}{}",
        endpoint.switchName().value_or(fmt::to_string(*endpoint.switchId())),
        endpoint.portName().value_or(fmt::to_string(*endpoint.portId())),
        isSelfLoop(port, endpoint) ? " (this switch, i.e. looped back)" : "");
  };

  std::vector<std::string> errors;
  // getFabricConnectivity() is a thrift round trip per switch, so fetch each
  // switch's table once rather than once per port.
  std::map<SwitchID, std::map<PortID, FabricEndpoint>> connectivityBySwitch;
  for (const auto& port : ports) {
    auto switchId = getSw()->getScopeResolver()->scope(port).switchId();
    auto switchItr = connectivityBySwitch.find(switchId);
    if (switchItr == connectivityBySwitch.end()) {
      switchItr = connectivityBySwitch
                      .emplace(switchId, getFabricConnectivity(switchId))
                      .first;
    }
    const auto& connectivity = switchItr->second;

    auto endpointItr = connectivity.find(port);
    if (endpointItr == connectivity.end()) {
      // No entry at all is indistinguishable from a detached endpoint, so it
      // only fails the reachable expectation.
      if (expectReachable) {
        errors.push_back(
            fmt::format("{}: no fabric endpoint reported", getPortName(port)));
      }
      continue;
    }

    const auto& endpoint = endpointItr->second;
    if (expectReachable) {
      if (!*endpoint.isAttached() ||
          FabricConnectivityManager::isConnectivityInfoMismatch(endpoint)) {
        errors.push_back(
            fmt::format(
                "{}: fabric endpoint is {}, expected the configured peer",
                getPortName(port),
                describe(port, endpoint)));
      }
      continue;
    }
    if (*endpoint.isAttached() && !isSelfLoop(port, endpoint)) {
      errors.push_back(
          fmt::format(
              "{}: fabric endpoint is {}, expected this switch (looped back) or a down port",
              getPortName(port),
              describe(port, endpoint)));
    } else {
      XLOG(INFO) << getPortName(port) << ": fabric endpoint under loopback is "
                 << describe(port, endpoint);
    }
  }
  return errors;
}

/*
 * All the ports touched in one step converge at the same time, so they share a
 * single retry window instead of serializing one timeout per port.
 */
void AgentEnsembleLoopbackTest::verifyExpectations(
    const PortExpectations& expectations,
    const std::string& context,
    std::vector<std::string>& failures) const {
  if (expectations.empty()) {
    return;
  }
  std::vector<std::string> errors;
  // EXPECT_EVENTUALLY (not ASSERT_EVENTUALLY) on purpose: a hard ASSERT
  // returns out of the caller, which would skip clearing the loopback we just
  // programmed into the optic.
  WITH_RETRIES_N_TIMED(kConvergenceRetries, kConvergenceRetryInterval, {
    errors = checkLldpNeighbors(expectations.lldpNeighbors);
    for (const auto expectReachable : {true, false}) {
      const auto& fabricPorts = expectReachable
          ? expectations.fabricReachable
          : expectations.fabricUnreachable;
      auto fabricErrors = checkFabricReachability(fabricPorts, expectReachable);
      errors.insert(errors.end(), fabricErrors.begin(), fabricErrors.end());
    }
    EXPECT_EVENTUALLY_TRUE(errors.empty())
        << context << " - " << folly::join("; ", errors);
  });
  for (const auto& error : errors) {
    failures.push_back(fmt::format("{} - {}", context, error));
  }
}

/*
 * A port is verified one way or the other, never both: a fabric port carries no
 * LLDP, and an interface port has no fabric endpoint. Every expectation in this
 * test is routed through these two helpers so the two checks can never get
 * crossed on a port.
 *
 * Returns false for an interface port with no expectedLLDPValues[PORT] in the
 * config, which leaves the port unverifiable -- the callers decide whether that
 * is fatal.
 */
bool AgentEnsembleLoopbackTest::expectReachableToPeer(
    PortID port,
    PortExpectations& expectations) const {
  if (isFabricPort(port)) {
    expectations.fabricReachable.insert(port);
    return true;
  }
  auto neighborItr = configuredNeighbors_.find(port);
  if (neighborItr == configuredNeighbors_.end()) {
    return false;
  }
  expectations.lldpNeighbors.emplace(port, neighborItr->second);
  return true;
}

// The looped counterpart of expectReachableToPeer(): an interface port must
// start seeing itself as its own LLDP neighbor, a fabric port must start seeing
// its own switch as its endpoint (or lose the endpoint entirely).
void AgentEnsembleLoopbackTest::expectLoopedBack(
    PortID port,
    PortExpectations& expectations) const {
  if (isFabricPort(port)) {
    expectations.fabricUnreachable.insert(port);
  } else {
    expectations.lldpNeighbors.emplace(port, port);
  }
}

std::string AgentEnsembleLoopbackTest::getPortNames(
    const std::vector<PortID>& ports) const {
  std::vector<std::string> names;
  names.reserve(ports.size());
  for (const auto& port : ports) {
    names.push_back(getPortName(port));
  }
  return folly::join(", ", names);
}

// Decides which of the two verification paths a port takes; see
// expectReachableToPeer().
bool AgentEnsembleLoopbackTest::isFabricPort(PortID port) const {
  const auto swPort = getProgrammedState()->getPorts()->getNodeIf(port);
  return swPort != nullptr &&
      swPort->getPortType() == cfg::PortType::FABRIC_PORT;
}

// getTransceiverIdFromSwPort() throws on a port with no optic, which is a
// legitimate state here rather than an error -- see getPairsUnderTest().
std::optional<int32_t> AgentEnsembleLoopbackTest::getTransceiverId(
    PortID port) const {
  try {
    return getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(port);
  } catch (const FbossError&) {
    return std::nullopt;
  }
}

/*
 * Every connected pair, interface or fabric, annotated with whether each end's
 * transceiver advertises the loopback under test in its diagsCapability.
 *
 * A pair with no optic on either end is dropped -- there is nothing to loop
 * back. For LINE mode, pairs whose two ends land on the same transceiver are
 * dropped too -- see the self-cabled check below.
 */
std::vector<PairUnderTest> AgentEnsembleLoopbackTest::getPairsUnderTest(
    const LoopbackMode& mode) const {
  // TransceiverFeature::NONE returns every optical pair. The per-side
  // capability is resolved below because the feature filter only inspects the
  // A end of each pair.
  auto opticalPairs = getConnectedOpticalAndActivePortPairWithFeature(
      TransceiverFeature::NONE,
      mode.side,
      true /* skipLoopback */,
      true /*opticalModulesOnly*/);

  // getConnectedOpticalAndActivePortPairWithFeature() only matches the A end
  // against the optical port list, so the Z end can still be a port with no
  // transceiver (e.g. a backplane fabric port). Resolve both ends up front and
  // drop the pair if either one has no optic to program.
  struct Candidate {
    PortID portA;
    PortID portZ;
    int32_t tcvrA;
    int32_t tcvrZ;
  };
  std::vector<Candidate> candidatePairs;
  std::vector<int32_t> tcvrIds;
  for (const auto& [portA, portZ] : opticalPairs) {
    auto tcvrA = getTransceiverId(portA);
    auto tcvrZ = getTransceiverId(portZ);
    if (!tcvrA.has_value() || !tcvrZ.has_value()) {
      XLOG(INFO) << "Skipping " << getPortName(portA) << " <-> "
                 << getPortName(portZ) << ": no transceiver on "
                 << getPortName(tcvrA.has_value() ? portZ : portA);
      continue;
    }
    candidatePairs.push_back(Candidate{portA, portZ, *tcvrA, *tcvrZ});
    tcvrIds.push_back(*tcvrA);
    tcvrIds.push_back(*tcvrZ);
  }
  if (tcvrIds.empty()) {
    return {};
  }
  auto tcvrInfos = utility::waitForTransceiverInfo(tcvrIds);

  auto supportsLoopback = [&mode, &tcvrInfos](int32_t tcvrId) {
    auto tcvrInfoItr = tcvrInfos.find(tcvrId);
    if (tcvrInfoItr == tcvrInfos.end()) {
      return false;
    }
    const auto& diagCapability =
        tcvrInfoItr->second.tcvrState()->diagCapability();
    if (!apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
            diagCapability) ||
        !*diagCapability->diagnostics()) {
      return false;
    }
    return mode.side == phy::Side::SYSTEM ? *diagCapability->loopbackSystem()
                                          : *diagCapability->loopbackLine();
  };

  std::vector<PairUnderTest> pairs;
  for (const auto& [portA, portZ, tcvrA, tcvrZ] : candidatePairs) {
    PairUnderTest pair;
    pair.portA = portA;
    pair.portZ = portZ;
    pair.tcvrA = tcvrA;
    pair.tcvrZ = tcvrZ;
    // A fiber cabled from a transceiver back to itself -- either a port
    // straight back to itself, or between two ports on the same optic (e.g.
    // eth1/26/1 to eth1/26/5) -- cannot be line looped and have valid LLDP.
    // Comparing transceivers covers both, since a port looped to itself is on
    // one optic by definition. The line loopback
    // re-transmits the peer's optical signal back at the peer, so the check
    // expects the *peer* port to start seeing itself; when the peer is a port
    // on the very optic being looped, enabling the loopback perturbs both ends
    // of the link at once and "peer hears itself" no longer distinguishes a
    // working loopback from a broken link. System loopback is unaffected: it
    // loops the host side for the lanes of one SW port, regardless of how the
    // fiber is cabled.
    if (mode.side == phy::Side::LINE && pair.tcvrA == pair.tcvrZ) {
      TCVR_TEST_LOG(INFO, pair.tcvrA)
          << "Skipping line loopback test for " << getPortName(portA) << " <-> "
          << getPortName(portZ) << ": transceiver is cabled back to itself";
      continue;
    }
    pair.loopbackA = supportsLoopback(pair.tcvrA);
    pair.loopbackZ = supportsLoopback(pair.tcvrZ);
    if (pair.loopbackA || pair.loopbackZ) {
      pairs.push_back(pair);
    }
  }
  return pairs;
}

/*
 * Every link must already reach the peer the config expects before any loopback
 * is applied, otherwise a mis-cabled setup would look like a loopback failure.
 */
void AgentEnsembleLoopbackTest::verifyBaseline(
    const std::vector<PairUnderTest>& pairs) {
  waitForLldpOnCabledPorts();
  PortExpectations baseline;
  for (const auto& pair : pairs) {
    for (const auto& port : {pair.portA, pair.portZ}) {
      ASSERT_TRUE(expectReachableToPeer(port, baseline))
          << "No expectedLLDPValues[PORT] configured for " << getPortName(port);
    }
  }
  std::vector<std::string> failures;
  verifyExpectations(baseline, "baseline reachability check", failures);
  ASSERT_TRUE(failures.empty())
      << "Links are not in their expected steady state before the test:\n"
      << folly::join("\n", failures);
}

/*
 * CmisModule::setTransceiverLoopbackLocked programs the loopback enable
 * register only for the lanes of the given SW port, so the effect is scoped to
 * that port and not to sibling ports sharing the transceiver.
 */
void AgentEnsembleLoopbackTest::setLoopback(
    const LoopbackMode& mode,
    PortID port,
    bool enable) {
  TCVR_TEST_LOG(
      INFO, getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(port))
      << (enable ? "Enabling " : "Clearing ")
      << apache::thrift::util::enumNameSafe(mode.side) << " loopback on "
      << getPortName(port);
  qsfpServiceClient_->sync_setPortLoopbackState(
      getPortName(port), mode.component, enable, phy::LoopbackMode::INPUT);
}

// Best effort: keep clearing the remaining optics even if one of them fails,
// so a single unresponsive transceiver does not leave the rest looped.
std::vector<std::string> AgentEnsembleLoopbackTest::clearLoopback(
    const LoopbackMode& mode,
    const std::vector<PortID>& ports) {
  std::vector<std::string> errors;
  for (const auto& port : ports) {
    try {
      setLoopback(mode, port, false);
    } catch (const std::exception& ex) {
      errors.push_back(
          fmt::format(
              "Failed to clear {} loopback on {}: {}",
              apache::thrift::util::enumNameSafe(mode.side),
              getPortName(port),
              ex.what()));
      XLOG(ERR) << errors.back();
    }
  }
  return errors;
}

/*
 * Loop back every port in loopbackPorts at once, check that each port whose
 * datapath the loopback diverts stops reaching its peer, then clear the
 * loopbacks and check that every link returns to its configured neighbor.
 */
void AgentEnsembleLoopbackTest::loopbackAndVerify(
    const LoopbackMode& mode,
    const std::vector<PortID>& loopbackPorts,
    const PortExpectations& looped,
    const PortExpectations& restored,
    std::vector<std::string>& failures) {
  if (loopbackPorts.empty()) {
    return;
  }
  const auto sideName = apache::thrift::util::enumNameSafe(mode.side);
  const auto portNames = getPortNames(loopbackPorts);

  // Armed before the first optic is programmed so a mid-way failure still
  // leaves every optic in its original state.
  auto clearLoopbackGuard =
      folly::makeGuard([&]() { clearLoopback(mode, loopbackPorts); });
  for (const auto& port : loopbackPorts) {
    setLoopback(mode, port, true);
  }

  verifyExpectations(
      looped,
      fmt::format("{} loopback enabled on {}", sideName, portNames),
      failures);

  auto clearErrors = clearLoopback(mode, loopbackPorts);
  clearLoopbackGuard.dismiss();
  failures.insert(failures.end(), clearErrors.begin(), clearErrors.end());

  verifyExpectations(
      restored,
      fmt::format("{} loopback cleared on {}", sideName, portNames),
      failures);
}

/*
 * One end of every link at a time: every A end that supports the loopback is
 * programmed in a single step, verified and cleared, then every Z end. Batching
 * a whole end means the entire set converges within one LLDP window instead of
 * paying a separate timeout per link.
 *
 * Which port's datapath the loopback diverts depends on the side. A system
 * loopback feeds a port its own frames, so the looped port is the one that
 * stops reaching its peer. A line loopback re-transmits the peer's optical
 * signal back at it, so it is the peer that does -- which is also why the two
 * ends of one link are never looped together.
 */
void AgentEnsembleLoopbackTest::runLoopbacksByEnd(
    const LoopbackMode& mode,
    const std::vector<PairUnderTest>& pairs,
    std::vector<std::string>& failures) {
  for (bool loopbackAEnd : {true, false}) {
    std::vector<PortID> loopbackPorts;
    PortExpectations looped;
    PortExpectations restored;
    for (const auto& pair : pairs) {
      if (!(loopbackAEnd ? pair.loopbackA : pair.loopbackZ)) {
        continue;
      }
      const auto loopbackPort = loopbackAEnd ? pair.portA : pair.portZ;
      const auto peerPort = loopbackAEnd ? pair.portZ : pair.portA;
      const auto observedPort =
          mode.side == phy::Side::SYSTEM ? loopbackPort : peerPort;
      loopbackPorts.push_back(loopbackPort);
      expectLoopedBack(observedPort, looped);
      for (const auto& port : {loopbackPort, peerPort}) {
        // verifyBaseline() has already asserted every port under test is
        // verifiable, so this is unreachable. Record it rather than aborting:
        // this loop runs between the A-end and Z-end rounds, and dying here
        // would skip the guard that clears whatever is still looped.
        if (!expectReachableToPeer(port, restored)) {
          failures.push_back(
              fmt::format(
                  "No expectReachableToPeer[PORT] configured for {}",
                  getPortName(port)));
        }
      }
    }
    try {
      loopbackAndVerify(mode, loopbackPorts, looped, restored, failures);
    } catch (const std::exception& ex) {
      failures.push_back(
          fmt::format(
              "{} loopback on {} threw: {}",
              apache::thrift::util::enumNameSafe(mode.side),
              getPortNames(loopbackPorts),
              ex.what()));
    }
  }
}

void AgentEnsembleLoopbackTest::runLoopbackTest(const LoopbackMode& mode) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::
           TRANSCEIVER_LOOPBACK});
  const auto sideName = apache::thrift::util::enumNameSafe(mode.side);

  // 1. All cabled ports must be up. AgentEnsembleLinkTest::SetUp() already
  //    waits for this; re-assert it so a flap between setup and here is
  //    attributed to this test.
  waitForAllCabledPorts(true);

  // 2. Collect the transceivers that advertise this loopback.
  auto pairs = getPairsUnderTest(mode);
  std::vector<PortID> testedPorts;
  for (const auto& pair : pairs) {
    if (pair.loopbackA) {
      TCVR_TEST_LOG(INFO, pair.tcvrA)
          << getPortName(pair.portA) << " supports " << sideName << " loopback";
      testedPorts.push_back(pair.portA);
    }
    if (pair.loopbackZ) {
      TCVR_TEST_LOG(INFO, pair.tcvrZ)
          << getPortName(pair.portZ) << " supports " << sideName << " loopback";
      testedPorts.push_back(pair.portZ);
    }
  }
  if (testedPorts.empty()) {
    GTEST_SKIP() << "No cabled transceiver supports " << sideName
                 << " loopback";
  }
  addTestedPorts(testedPorts);

  // 3. Every link must reach its configured peer before the first loopback is
  //    applied.
  configuredNeighbors_ = getConfiguredNeighbors();
  ASSERT_NO_FATAL_FAILURE(verifyBaseline(pairs));

  // 4. Loop every A end at once, then every Z end, checking reachability with
  //    the loopbacks up and again once they are cleared.
  qsfpServiceClient_ = utils::createQsfpServiceClient();
  std::vector<std::string> failures;
  runLoopbacksByEnd(mode, pairs, failures);

  if (!failures.empty()) {
    auto summary = fmt::format(
        "{} {} loopback check(s) failed across the {} transceiver(s) under test:\n{}",
        failures.size(),
        sideName,
        testedPorts.size(),
        folly::join("\n", failures));
    XLOG(ERR) << summary;
    FAIL() << summary;
  }
}

/*
 * Enable the transceiver system (host) side loopback on one end of every link
 * at a time -- all the A ends, then all the Z ends. The ASIC's transmit is
 * looped back into its own receive, so every looped port must start hearing
 * itself (an interface port by seeing itself as its LLDP neighbor, a fabric
 * port by discovering its own switch as its endpoint) and go back to its
 * configured neighbor once the loopback is cleared.
 */
TEST_F(AgentEnsembleLoopbackTest, systemLoopbackTest) {
  runLoopbackTest(kSystemLoopback);
}

/*
 * Enable the transceiver line (media) side loopback on one end of every link at
 * a time -- all the A ends, then all the Z ends. The optical signal arriving
 * from the peer is re-transmitted back at it, so it is the peer port that must
 * start hearing itself, and both ends must go back to their configured
 * neighbors once the loopback is cleared.
 */
TEST_F(AgentEnsembleLoopbackTest, lineLoopbackTest) {
  runLoopbackTest(kLineLoopback);
}

} // namespace facebook::fboss
