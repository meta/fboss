// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/agent_multinode_tests/AgentMultiNodeTest.h"
#include "fboss/agent/test/TestUtils.h"

#include "fboss/agent/test/agent_multinode_tests/AgentMultiNodeDsfUtils.h"

namespace facebook::fboss {

void AgentMultiNodeTest::SetUp() {
  AgentHwTest::SetUp();
  topologyInfo_ = utility::TopologyInfo::makeTopologyInfo(getProgrammedState());

  if (!topologyInfo_->isTestDriver(*getSw())) {
    throw FbossError(
        "Agent Multi Node test binary can be run on TestDriver only");
  }
}

cfg::SwitchConfig AgentMultiNodeTest::initialConfig(
    const AgentEnsemble& /*ensemble*/) const {
  XLOG(DBG0) << "initialConfig() loaded config from file " << FLAGS_config;

  // Trust the deployed config (/etc/coop/agent/current) for loopback mode.
  // Configerator already sets loopbackMode=PHY on the specific INTERFACE_PORTs
  // we want UP for multinode tests (the ones with IPs configured). Overriding
  // here brings ALL interface ports UP via loopback on the test driver, which
  // diverges from remote RDSWs and breaks tests that pick the first UP port
  // expecting it to have an IP.
  auto agentConfig = AgentConfig::fromFile(FLAGS_config);
  auto config = *agentConfig->thrift.sw();

  // Fail fast if configerator forgot to set loopbackMode on every
  // INTERFACE_PORT we expect UP. Otherwise the test driver brings nothing up
  // and downstream tests fail with confusing "link down" / "first UP port has
  // no IP" errors.
  //
  // Hyper port topologies are exempt: there the L3 ports are HYPER_PORT and
  // HYPER_PORT_MEMBER, LinkStateToggler skips both, and the two EDSWs are
  // cabled to each other, so no INTERFACE_PORT is ever looped back.
  if (!FLAGS_hyper_port) {
    size_t numLoopedInterfacePorts = 0;
    for (const auto& port : *config.ports()) {
      if (port.portType() == cfg::PortType::INTERFACE_PORT &&
          port.loopbackMode() != cfg::PortLoopbackMode::NONE) {
        ++numLoopedInterfacePorts;
      }
    }
    if (numLoopedInterfacePorts == 0) {
      throw FbossError(
          "Deployed config has no INTERFACE_PORT with loopbackMode set; ",
          "test driver cannot bring any port UP. Check configerator config.");
    }
  }

  return config;
}

// TODO: Factor out to VOQ specific Test subclass and implement there
std::vector<ProductionFeature>
AgentMultiNodeTest::getProductionFeaturesVerified() const {
  return {ProductionFeature::VOQ};
}

void AgentMultiNodeTest::setCmdLineFlagOverrides() const {
  AgentHwTest::setCmdLineFlagOverrides();
  FLAGS_hide_fabric_ports = false;
  FLAGS_hide_interface_ports = false;
  // Allow disabling of looped ports. This should
  // be a noop for VOQ switches
  FLAGS_disable_looped_fabric_ports = true;
  FLAGS_enable_lldp = true;
  FLAGS_tun_intf = true;
  FLAGS_disable_neighbor_updates = false;
  FLAGS_publish_stats_to_fsdb = true;
  FLAGS_publish_state_to_fsdb = true;
  FLAGS_dsf_subscribe = true;
}

void AgentMultiNodeTest::verifyCluster() const {
  switch (topologyInfo_->getTopologyType()) {
    case utility::TopologyInfo::TopologyType::DSF:
      utility::verifyDsfCluster(topologyInfo_);
      break;
  }
}

void AgentMultiNodeTest::runTestWithVerifyCluster(
    const std::function<bool(const std::unique_ptr<utility::TopologyInfo>&)>&
        testFn) const {
  // Verify cluster before running test
  verifyCluster();
  if (testing::Test::HasNonfatalFailure()) {
    // Some EXPECT_* asserts in verifyDsfClusterHelper() failed.
    FAIL()
        << "Sanity checks in DSF cluster verification failed, can't proceed with test";
  }

  // The testFn is expected to contain retries specific to the test logic,
  // thus, we can ASSERT here.
  ASSERT_TRUE(testFn(topologyInfo_));
  // Verify cluster is still healthy after test
  verifyCluster();
}

TEST_F(AgentMultiNodeTest, verifyCluster) {
  verifyCluster();
}

} // namespace facebook::fboss
