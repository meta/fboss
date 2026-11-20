// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <folly/String.h>
#include <gtest/gtest.h>
#include <chrono>
#include <thread>

#include "fboss/agent/test/link_tests/AgentEnsembleLinkTest.h"
#include "fboss/lib/thrift_service_client/ThriftServiceClient.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"

using namespace ::testing;
using namespace facebook::fboss;
using namespace std::chrono_literals;

static constexpr int kThriftIterations = 50;

// QsfpServiceThriftDeadlockTests are meant to reduce the scope of deadlock
// issues to routine qsfp service behavior. This file will host tests that
// repeatedly call multiple QsfpServiceHandler thrift APIs to ensure
// deadlocks don't occur.

// If a Thrift function either requires multiple locks or acquires 1+ write
// locks, we want to add a test here.

TEST_F(AgentEnsembleLinkTest, resetTransceiverDeadlockTest) {
  const auto& cabledPorts = getCabledPorts();
  auto cabledPortsNames = getPortNames(cabledPorts);
  XLOG(INFO) << "Resetting transceivers for: "
             << folly::join(",", cabledPortsNames);

  auto qsfpServiceClient = utils::createQsfpServiceClient();
  auto rpcOptions = apache::thrift::RpcOptions();
  rpcOptions.setTimeout(std::chrono::seconds(60));

  for (int i = 0; i < kThriftIterations; ++i) {
    XLOG(INFO) << "Reset Iteration: " << i;
    qsfpServiceClient->sync_resetTransceiver(
        rpcOptions,
        cabledPortsNames,
        ResetType::HARD_RESET,
        ResetAction::RESET_THEN_CLEAR);
    /* sleep override */
    std::this_thread::sleep_for(1s);
  }

  ASSERT_NO_THROW(waitForAllCabledPorts(true, 60, 5s));
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::LINK_BRINGUP});
}
