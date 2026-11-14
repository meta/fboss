// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "fboss/agent/InstrumentationHooks.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/MultiSwitchAgentEnsemble.h"

#include <folly/logging/Init.h>
#include <gtest/gtest.h>

FOLLY_INIT_LOGGING_CONFIG("fboss=DBG4; default:async=true");

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  facebook::fboss::startCoverageCollection();
  facebook::fboss::initAgentHwTest(argc, argv, nullptr);
  const int rc = RUN_ALL_TESTS();
  facebook::fboss::stopCoverageCollection();
  return rc;
}
