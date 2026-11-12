// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "fboss/agent/InstrumentationHooks.h"
#include "fboss/agent/platforms/sai/SaiPlatformInit.h"
#include "fboss/agent/test/AgentHwTest.h"

#include <folly/logging/Init.h>
#include <gtest/gtest.h>

FOLLY_INIT_LOGGING_CONFIG("fboss=DBG4; default:async=true");

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  facebook::fboss::startCoverageCollection();
  std::optional<facebook::fboss::cfg::StreamType> streamType = std::nullopt;
#if defined(TAJO_SDK_GTE_24_8_3001)
  streamType = facebook::fboss::cfg::StreamType::UNICAST;
#endif
  facebook::fboss::initAgentHwTest(
      argc, argv, facebook::fboss::initSaiPlatform, streamType);
  const int rc = RUN_ALL_TESTS();
  facebook::fboss::stopCoverageCollection();
  return rc;
}
