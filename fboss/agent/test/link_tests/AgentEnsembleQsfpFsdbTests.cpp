// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include "fboss/agent/FsdbSyncer.h"
#include "fboss/agent/test/link_tests/AgentEnsembleLinkTest.h"
#include "fboss/fsdb/if/FsdbModel.h"
#include "fboss/lib/CommonUtils.h"
#include "fboss/lib/thrift_service_client/ThriftServiceClient.h"
#include "fboss/qsfp_service/fsdb/test/QsfpFsdbTestUtils.h"

using namespace ::testing;
using namespace facebook::fboss;
using namespace facebook::fboss::fsdb;

namespace {

const thriftpath::RootThriftPath<FsdbOperStateRoot> stateRoot;
const thriftpath::RootThriftPath<FsdbOperStatsRoot> statsRoot;

} // anonymous namespace

class AgentEnsembleQsfpFsdbTest : public AgentEnsembleLinkTest {
 public:
  void SetUp() override {
    AgentEnsembleLinkTest::SetUp();
    if (!IsSkipped()) {
      pubSubMgr_ = std::make_unique<fsdb::FsdbPubSubManager>("qsfpFsdbTest");
    }
  }

 protected:
  template <typename Path>
  TestSubscription<Path> subscribe(const Path& path) {
    return TestSubscription<Path>(pubSubMgr(), path);
  }

  template <typename Path>
  TestSubscription<Path> subscribeStat(const Path& path) {
    return TestSubscription<Path>(pubSubMgr(), path, true);
  }

  static auto constexpr kRetries = 60;

  using PortStateMap = std::map<std::string, portstate::PortState>;

  // Gets the port state maps with before/after, with a reset of the transceiver
  // in the middle.
  void getPortStateMapsWithRestart(
      PortStateMap& portStateMapBefore,
      PortStateMap& portStateMapAfter,
      ResetAction resetAction) {
    auto statesIn = subscribe(stateRoot.qsfp_service().state().portStates());

    auto& ports = getCabledTransceiverPorts();
    std::vector<std::string> portNames;
    portNames.reserve(ports.size());
    for (const auto& port : ports) {
      portNames.push_back(getPortName(port));
    }

    auto checkStatesPopulated = [&](bool expectZeroStamps) {
      // Verify states
      WITH_RETRIES_N(120 /* 120seconds */, {
        statesIn.data().withRLock([&](auto& state) {
          ASSERT_EVENTUALLY_TRUE(state);
          // Expect same number of phystates as the number of xphy ports
          ASSERT_EVENTUALLY_TRUE(state->size() >= portNames.size());
          for (auto portName : portNames) {
            auto iter = (*state).find(portName);
            EXPECT_EVENTUALLY_TRUE(iter != (*state).end());
            if (expectZeroStamps) {
              EXPECT_EVENTUALLY_TRUE(
                  iter->second.tcvrProgrammingStartTs().value() == 0);
              EXPECT_EVENTUALLY_TRUE(
                  iter->second.tcvrProgrammingCompleteTs().value() == 0);
            } else {
              EXPECT_EVENTUALLY_TRUE(
                  iter->second.tcvrProgrammingStartTs().value() > 0);
              EXPECT_EVENTUALLY_TRUE(
                  iter->second.tcvrProgrammingCompleteTs().value() > 0);
            }
          }
        });
      });
    };

    auto getPortStateMap =
        [&](std::map<std::string, portstate::PortState>& portStateMap) {
          statesIn.data().withRLock([&](auto& state) {
            for (const auto& portName : portNames) {
              auto iter = (*state).find(portName);
              CHECK(iter != (*state).end());
              auto portStateMapItr = portStateMap.find(portName);
              // Check no duplicate port names
              CHECK(portStateMapItr == portStateMap.end());
              portStateMap[portName].tcvrProgrammingStartTs() =
                  iter->second.tcvrProgrammingStartTs().value();
              portStateMap[portName].tcvrProgrammingCompleteTs() =
                  iter->second.tcvrProgrammingCompleteTs().value();
            }
          });
        };

    checkStatesPopulated(false);
    // Get the Timestamps
    getPortStateMap(portStateMapBefore);

    auto qsfpServiceClient = utils::createQsfpServiceClient();
    auto rpcOptions = apache::thrift::RpcOptions();
    rpcOptions.setTimeout(std::chrono::seconds(60));

    if (resetAction != ResetAction::INVALID) {
      qsfpServiceClient->sync_resetTransceiver(
          rpcOptions, portNames, ResetType::HARD_RESET, resetAction);
      /* sleep override */
      std::this_thread::sleep_for(1s);
    }
    checkStatesPopulated(resetAction == ResetAction::INVALID ? false : true);
    // Get the Timestamps
    getPortStateMap(portStateMapAfter);

    CHECK_EQ(portStateMapAfter.size(), portStateMapBefore.size())
        << "Port States before and after dont have the same ports";

    if (resetAction != ResetAction::INVALID) {
      // Clear Reset
      qsfpServiceClient->sync_resetTransceiver(
          rpcOptions,
          portNames,
          ResetType::HARD_RESET,
          ResetAction::CLEAR_RESET);
      /* sleep override */
      std::this_thread::sleep_for(1s);
    }
  }

 private:
  fsdb::FsdbPubSubManager* pubSubMgr() {
    return pubSubMgr_.get();
  }

  void setCmdLineFlagOverrides() const override {
    // Set all FSDB flags even if we're not testing agent FSDB. Otherwise
    // fsdbSyncer, pubSubMgr, etc. are not created and we can't subscribe
    // anything in the test.
    FLAGS_publish_stats_to_fsdb = true;
    FLAGS_publish_state_to_fsdb = true;
    AgentEnsembleLinkTest::setCmdLineFlagOverrides();
  }
  std::vector<link_test_production_features::LinkTestProductionFeature>
  getProductionFeatures() const override {
    std::vector<link_test_production_features::LinkTestProductionFeature>
        features = {link_test_production_features::LinkTestProductionFeature::
                        L1_LINK_TEST};
    const std::string testName =
        testing::UnitTest::GetInstance()->current_test_info()->name();
    if (testName == "portStateWithResetHold") {
      // Holding the transceiver in reset needs BMC-Lite; on BMC-Classic the BMC
      // arbitrates transceiver reset and releases it under us.
      features.push_back(
          link_test_production_features::LinkTestProductionFeature::BMC_LITE);
    }
    return features;
  }
  std::unique_ptr<fsdb::FsdbPubSubManager> pubSubMgr_;
};

TEST_F(AgentEnsembleQsfpFsdbTest, tcvr) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::FSDB_PUBLISH});
  addTestedPorts(getCabledPorts());
  auto statesIn = subscribe(stateRoot.qsfp_service().state().tcvrStates());
  auto statsIn = subscribeStat(statsRoot.qsfp_service().tcvrStats());
  auto actualConfig = subscribe(stateRoot.qsfp_service().config());
  const auto& tcvrs = getCabledTranceivers();
  WITH_RETRIES_N(kRetries, {
    // Verify stats
    ASSERT_EVENTUALLY_TRUE(statsIn.data().rlock()->has_value());
    // Verify states
    statesIn.data().withRLock([&](auto& states) {
      ASSERT_EVENTUALLY_TRUE(states);
      ASSERT_EVENTUALLY_FALSE(states->empty());
      for (const auto& tcvr : tcvrs) {
        auto iter = (*states).find(tcvr);
        EXPECT_EVENTUALLY_TRUE(iter != (*states).end());
        if (iter != (*states).end()) {
          EXPECT_EVENTUALLY_TRUE(iter->second.present().value());
          EXPECT_EVENTUALLY_TRUE(
              iter->second.vendor() &&
              !iter->second.vendor()->partNumber()->empty());
        }
      }
    });
    // Verify config
    // Just wait for it to arrive. Qsfp doesn't export config path in Thrift
    // and most of the time, config in test servers is empty.
    EXPECT_EVENTUALLY_TRUE(actualConfig.data().rlock());
  });
}

TEST_F(AgentEnsembleQsfpFsdbTest, phy) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::FSDB_PUBLISH});
  addTestedPorts(getCabledPorts());
  auto statesIn = subscribe(stateRoot.qsfp_service().state().phyStates());
  auto statsIn = subscribeStat(statsRoot.qsfp_service().phyStats());
  auto xphyPorts = getXphyCabledPorts();

  WITH_RETRIES_N(120 /* 120seconds */, {
    // Verify states
    statesIn.data().withRLock([&](auto& states) {
      ASSERT_EVENTUALLY_TRUE(states);
      // Expect same number of phystates as the number of xphy ports
      ASSERT_EVENTUALLY_EQ(states->size(), xphyPorts.size());
      for (auto port : xphyPorts) {
        auto portName = getPortName(port);
        auto iter = (*states).find(portName);
        EXPECT_EVENTUALLY_TRUE(iter != (*states).end());
        if (iter != (*states).end()) {
          EXPECT_EVENTUALLY_EQ(iter->second.name(), portName);
        }
      }
    });
    // Verify stats
    WITH_RETRIES_N(120 /* 120seconds */, {
      statsIn.data().withRLock([&](auto& stats) {
        ASSERT_EVENTUALLY_TRUE(stats);
        // Expect same number of phystates as the number of xphy ports
        ASSERT_EVENTUALLY_EQ(stats->size(), xphyPorts.size());
        for (auto port : xphyPorts) {
          auto portName = getPortName(port);
          auto iter = (*stats).find(portName);
          EXPECT_EVENTUALLY_TRUE(iter != (*stats).end());
        }
      });
    });
  });
}

TEST_F(AgentEnsembleQsfpFsdbTest, portState) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::FSDB_PUBLISH});
  addTestedPorts(getCabledPorts());
  std::map<std::string, portstate::PortState> portStateMapBefore;
  std::map<std::string, portstate::PortState> portStateMapAfter;
  getPortStateMapsWithRestart(
      portStateMapBefore, portStateMapAfter, ResetAction::INVALID);
  CHECK_EQ(portStateMapAfter.size(), portStateMapBefore.size())
      << "Port States before and after dont have the same ports";

  for (auto& [port, portstate] : portStateMapAfter) {
    auto itr = portStateMapBefore.find(port);
    CHECK(itr != portStateMapBefore.end())
        << "Port State before not found for port " << port;
    CHECK_EQ(
        itr->second.tcvrProgrammingStartTs().value(),
        portstate.tcvrProgrammingStartTs().value())
        << " Time stamp did not match for programming start";
    CHECK_EQ(
        itr->second.tcvrProgrammingCompleteTs().value(),
        portstate.tcvrProgrammingCompleteTs().value())
        << " Time stamp did not match for programming complete";
  }
}

TEST_F(AgentEnsembleQsfpFsdbTest, portStateWithResetHold) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::FSDB_PUBLISH});
  addTestedPorts(getCabledPorts());
  std::map<std::string, portstate::PortState> portStateMapBefore;
  std::map<std::string, portstate::PortState> portStateMapAfter;
  getPortStateMapsWithRestart(
      portStateMapBefore, portStateMapAfter, ResetAction::RESET);
  CHECK_EQ(portStateMapAfter.size(), portStateMapBefore.size())
      << "Port States before and after dont have the same ports";

  for (auto& [port, portstate] : portStateMapBefore) {
    auto itr = portStateMapAfter.find(port);
    CHECK(itr != portStateMapAfter.end())
        << "Port State before not found for port " << port;
    CHECK_NE(portstate.tcvrProgrammingStartTs().value(), 0)
        << " Time stamp for start is 0, before reset";
    CHECK_NE(portstate.tcvrProgrammingCompleteTs().value(), 0)
        << " Time stamp for complete is 0, before reset";
  }

  for (auto& [port, portstate] : portStateMapAfter) {
    CHECK_EQ(portstate.tcvrProgrammingStartTs().value(), 0)
        << " Time stamp for start is not 0, after hold reset";
    CHECK_EQ(portstate.tcvrProgrammingCompleteTs().value(), 0)
        << " Time stamp for complete is not 0, after hold reset";
  }
}
