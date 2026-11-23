// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/agent_multinode_tests/AgentMultiNodeVoqSwitchTrafficTests.h"

#include "fboss/agent/test/agent_multinode_tests/AgentMultiNodeDsfUtils.h"
#include "fboss/agent/test/agent_multinode_tests/AgentMultiNodeUtils.h"
#include "fboss/agent/test/thrift_client_utils/ThriftClientUtils.h"

namespace facebook::fboss {

class AgentMultiNodeVoqSwitchNoTrafficDropTest
    : public AgentMultiNodeVoqSwitchTrafficTest {
 protected:
  struct Scenario {
    std::string name;
    std::function<bool()> setup;
  };

  // Return true only if all scenarios are successful
  bool runScenariosAndVerifyNoDrops(
      const std::unique_ptr<utility::TopologyInfo>& topologyInfo,
      const std::vector<Scenario>& scenarios) const {
    for (const auto& scenario : scenarios) {
      XLOG(DBG2) << "Running scenario: " << scenario.name;
      if (!scenario.setup()) {
        XLOG(DBG2) << "Scenario: " << scenario.name << " failed";
        return false;
      }

      if (!utility::verifyNoReassemblyErrorsForAllSwitches(topologyInfo)) {
        // TODO query drop counters to root cause reason for reassembly errors
        XLOG(DBG2) << "Scenario: " << scenario.name
                   << " unexpected reassembly errors";
        return false;
      }
    }

    return true;
  }

 public:
  bool verifyNoTrafficDropOnProcessRestarts(
      const std::unique_ptr<utility::TopologyInfo>& topologyInfo) {
    XLOG(DBG2) << "Verifying No Traffic Drops on process restarts";

    if (!setupTrafficLoop(topologyInfo)) {
      XLOG(DBG2) << "Traffic loop setup failed";
      return false;
    }

    if (!utility::verifyNoReassemblyErrorsForAllSwitches(topologyInfo)) {
      XLOG(DBG2) << "Unexpected reassembly errors";
      // TODO query drop counters to root cause reason for reassembly errors
      return false;
    }

    const auto& switches = topologyInfo->getAllSwitches();
    const auto& myHostname = topologyInfo->getMyHostname();

    auto allQsfpRestartHelper = [switches](bool gracefulRestart) {
      {
        // Restart QSFP on all switches
        utility::forEachExcluding(
            {}, // exclude none
            switches,
            gracefulRestart ? utility::triggerGracefulQsfpRestart
                            : utility::triggerUngracefulQsfpRestart);

        // Wait for QSFP to come up on all switches
        auto restart = utility::checkForEachExcluding(
            switches,
            {}, // exclude none
            [](const std::string& switchName,
               const QsfpServiceRunState& state) {
              return utility::verifyQsfpServiceRunState(switchName, state);
            },
            QsfpServiceRunState::ACTIVE);

        return restart;
      }
    };

    auto allFsdbRestartHelper = [switches](bool gracefulRestart) {
      {
        // Restart FSDB on all switches
        utility::forEachExcluding(
            switches,
            {}, // exclude none
            gracefulRestart ? utility::triggerGracefulFsdbRestart
                            : utility::triggerUngracefulFsdbRestart);

        // Wait for FSDB to come up on all switches
        auto restart = utility::checkForEachExcluding(
            switches,
            {}, // exclude none
            [](const std::string& switchName,
               const QsfpServiceRunState& state) {
              return utility::verifyFsdbIsUp(switchName);
            },
            QsfpServiceRunState::ACTIVE);

        return restart;
      }
    };

    auto allAgentRestartHelper = [switches, myHostname](bool gracefulRestart) {
      {
        // Restart Agent on all switches
        utility::forEachExcluding(
            switches,
            {myHostname}, // exclude self, as running this test binary
            [gracefulRestart](const std::string& switchName) {
              if (gracefulRestart) {
                utility::triggerGracefulAgentRestart(switchName);
              } else {
                utility::triggerUngracefulAgentRestart(switchName);
              }
            });

        // Wait for Agent to come up on all switches
        auto restart = utility::checkForEachExcluding(
            switches,
            {myHostname}, // exclude self, as running this test binary
            [](const std::string& switchName, const SwitchRunState& state) {
              return utility::verifySwSwitchRunState(switchName, state);
            },
            SwitchRunState::CONFIGURED);

        return restart;
      }
    };

    // With traffic loop running, execute a variety of scenarios.
    // For each scenario, expect no drops on Fabric ports.
    Scenario gracefullyRestartQsfpAllSwitches = {
        "gracefullyRestartQsfpAllSwitches",
        [&] { return allQsfpRestartHelper(true /* gracefulRestart */); }};

    Scenario unGracefullyRestartQsfpAllSwitches = {
        "unGracefullyRestartQsfpAllSwitches",
        [&] { return allQsfpRestartHelper(false /* ungracefulRestart */); }};

    Scenario gracefullyRestartFsdbAllSwitches = {
        "gracefullyRestartFsdbAllSwitches",
        [&] { return allFsdbRestartHelper(true /* gracefulRestart */); }};

    Scenario unGracefullyRestartFsdbAllSwitches = {
        "unGracefullyRestartFsdbAllSwitches",
        [&] { return allFsdbRestartHelper(false /* ungracefulRestart */); }};

    Scenario gracefullyRestartAgentAllSwitches = {
        "gracefullyRestartFsdbAllSwitches",
        [&] { return allAgentRestartHelper(true /* gracefulRestart */); }};

    std::vector<Scenario> scenarios = {
        std::move(gracefullyRestartQsfpAllSwitches),
        std::move(unGracefullyRestartQsfpAllSwitches),
        std::move(gracefullyRestartFsdbAllSwitches),
        std::move(unGracefullyRestartFsdbAllSwitches),
        std::move(gracefullyRestartAgentAllSwitches),
    };

    return runScenariosAndVerifyNoDrops(topologyInfo, scenarios);
  }

  bool verifyNoTrafficDropOnPortStateChanges(
      const std::unique_ptr<utility::TopologyInfo>& topologyInfo) {
    XLOG(DBG2) << "Verifying No Traffic Drops on port state changes";

    if (!setupTrafficLoop(topologyInfo)) {
      XLOG(DBG2) << "Traffic loop setup failed";
      return false;
    }

    if (!utility::verifyNoReassemblyErrorsForAllSwitches(topologyInfo)) {
      XLOG(DBG2) << "Unexpected reassembly errors";
      // TODO query drop counters to root cause reason for reassembly errors
      return false;
    }

    auto fabricSwitches = getOneFabricSwitchForEachCluster(topologyInfo);
    auto drainUndrainHelper = [fabricSwitches]() {
      {
        // Drain and then Undrain one Active fabric port for one FDSW for every
        // cluster. Drain/Undrain is a synchronous call, so we don't check
        // status. Other tests verify if Drain/Undrain changes Active/Inactive
        // state.
        auto switchToActivePort = utility::forEachWithRetVal(
            fabricSwitches, utility::getFirstActiveFabricPort);

        for (const auto& [switchName, port] : switchToActivePort) {
          utility::drainPort(switchName, port);
        }
        for (const auto& [switchName, port] : switchToActivePort) {
          utility::undrainPort(switchName, port);
        }

        return true;
      }
    };

    Scenario drainUndrainFabricLinkOnePerFabric = {
        "drainUndrainFabricLinkOnePerFabric",
        [&] { return drainUndrainHelper(); }};

    const std::vector<Scenario> scenarios = {
        std::move(drainUndrainFabricLinkOnePerFabric),
    };

    return runScenariosAndVerifyNoDrops(topologyInfo, scenarios);
  }
};

TEST_F(
    AgentMultiNodeVoqSwitchNoTrafficDropTest,
    verifyNoTrafficDropOnProcessRestarts) {
  runTestWithVerifyCluster([this](const auto& topologyInfo) {
    switch (topologyInfo->getTopologyType()) {
      case utility::TopologyInfo::TopologyType::DSF:
        return this->verifyNoTrafficDropOnProcessRestarts(topologyInfo);
    }
  });
}

TEST_F(
    AgentMultiNodeVoqSwitchNoTrafficDropTest,
    verifyNoTrafficDropOnPortStateChanges) {
  runTestWithVerifyCluster([this](const auto& topologyInfo) {
    switch (topologyInfo->getTopologyType()) {
      case utility::TopologyInfo::TopologyType::DSF:
        return this->verifyNoTrafficDropOnPortStateChanges(topologyInfo);
    }
  });
}

} // namespace facebook::fboss
