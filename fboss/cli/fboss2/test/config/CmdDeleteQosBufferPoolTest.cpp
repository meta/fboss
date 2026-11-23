// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/commands/delete/qos/buffer_pool/CmdDeleteQosBufferPool.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

// One free pool, and one held by each kind of referrer a pool can have (port
// queue config, default port queue, cpu queue, default VOQ, cpu VOQ, priority
// group).
class CmdDeleteQosBufferPoolTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteQosBufferPoolTestFixture()
      : CmdConfigTestBase(
            "fboss_delete_qos_buffer_pool_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "ports": [],
    "bufferPoolConfigs": {
      "free": {"sharedBytes": 1000},
      "queue-held": {"sharedBytes": 1000},
      "default-held": {"sharedBytes": 1000},
      "cpu-held": {"sharedBytes": 1000},
      "voq-held": {"sharedBytes": 1000},
      "cpu-voq-held": {"sharedBytes": 1000},
      "pg-held": {"sharedBytes": 1000}
    },
    "portQueueConfigs": {
      "uplink": [{"id": 0, "streamType": 0, "scheduling": 0, "bufferPoolName": "queue-held"}]
    },
    "defaultPortQueues": [{"id": 0, "streamType": 0, "scheduling": 0, "bufferPoolName": "default-held"}],
    "cpuQueues": [{"id": 0, "streamType": 0, "scheduling": 0, "bufferPoolName": "cpu-held"}],
    "defaultVoqConfig": [{"id": 1, "streamType": 0, "scheduling": 0, "bufferPoolName": "voq-held"}],
    "cpuVoqs": [{"id": 2, "streamType": 0, "scheduling": 0, "bufferPoolName": "cpu-voq-held"}],
    "portPgConfigs": {
      "pg-a": [{"id": 0, "bufferPoolName": "pg-held"}]
    }
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "delete qos";

  static BufferPoolName name(const std::string& n) {
    return BufferPoolName(std::vector<std::string>{n});
  }

  bool hasPool(const std::string& n) {
    auto pools =
        ConfigSession::getInstance().getAgentConfig().sw()->bufferPoolConfigs();
    return pools.has_value() && pools->count(n) > 0;
  }

  void expectRefused(const std::string& pool, const std::string& refSubstr) {
    setupTestableConfigSession(cmdPrefix_, "buffer-pool " + pool);
    auto cmd = CmdDeleteQosBufferPool();
    try {
      cmd.queryClient(localhost(), name(pool));
      FAIL() << "expected refusal for " << pool;
    } catch (const std::runtime_error& e) {
      EXPECT_THAT(e.what(), HasSubstr("still referenced by"));
      EXPECT_THAT(e.what(), HasSubstr(refSubstr));
    }
    EXPECT_TRUE(hasPool(pool));
  }
};

TEST_F(CmdDeleteQosBufferPoolTestFixture, nameArgValidation) {
  EXPECT_THROW(
      BufferPoolName(std::vector<std::string>{}), std::invalid_argument);
  EXPECT_THROW(
      BufferPoolName(std::vector<std::string>{"a", "b"}),
      std::invalid_argument);
  EXPECT_EQ(name("p").getName(), "p");
}

TEST_F(CmdDeleteQosBufferPoolTestFixture, deletesUnreferencedPool) {
  setupTestableConfigSession(cmdPrefix_, "buffer-pool free");
  auto cmd = CmdDeleteQosBufferPool();

  auto result = cmd.queryClient(localhost(), name("free"));

  EXPECT_THAT(result, HasSubstr("deleted buffer pool 'free'"));
  EXPECT_FALSE(hasPool("free"));
  EXPECT_TRUE(hasPool("queue-held"));
  // Same action level as `config qos buffer-pool`.
  EXPECT_EQ(
      ConfigSession::getInstance().getRequiredAction(cli::ServiceType::AGENT),
      cli::ConfigActionLevel::SERVICE_RESTART);
}

TEST_F(CmdDeleteQosBufferPoolTestFixture, missingPoolFails) {
  setupTestableConfigSession(cmdPrefix_, "buffer-pool nope");
  auto cmd = CmdDeleteQosBufferPool();

  EXPECT_THROW(cmd.queryClient(localhost(), name("nope")), std::runtime_error);
}

TEST_F(CmdDeleteQosBufferPoolTestFixture, refusedWhenPortQueueRefers) {
  expectRefused("queue-held", "portQueueConfigs[uplink][queue 0]");
}

TEST_F(CmdDeleteQosBufferPoolTestFixture, refusedWhenDefaultQueueRefers) {
  expectRefused("default-held", "defaultPortQueues[queue 0]");
}

TEST_F(CmdDeleteQosBufferPoolTestFixture, refusedWhenCpuQueueRefers) {
  expectRefused("cpu-held", "cpuQueues[queue 0]");
}

TEST_F(CmdDeleteQosBufferPoolTestFixture, refusedWhenDefaultVoqRefers) {
  expectRefused("voq-held", "defaultVoqConfig[queue 1]");
}

TEST_F(CmdDeleteQosBufferPoolTestFixture, refusedWhenCpuVoqRefers) {
  expectRefused("cpu-voq-held", "cpuVoqs[queue 2]");
}

TEST_F(CmdDeleteQosBufferPoolTestFixture, refusedWhenPriorityGroupRefers) {
  expectRefused("pg-held", "portPgConfigs[pg-a][pg 0]");
}

} // namespace facebook::fboss
