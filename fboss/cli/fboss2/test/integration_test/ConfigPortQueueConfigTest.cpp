// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end test for:
 *   fboss2-dev config qos buffer-pool <name> shared-bytes/headroom-bytes
 *   fboss2-dev config qos queue-config <name> queue-id <id> <attr> <value>...
 *   fboss2-dev config interface <name> queue-config <name>
 *   fboss2-dev delete qos queue-config <name>
 *
 * Builds a buffer pool + named queue config with five queues exercising:
 *   - WRR + SP scheduling
 *   - weight, shared-bytes, reserved-bytes
 *   - scaling-factor (MMUScalingFactor enum)
 *   - stream-type (MULTICAST vs default UNICAST)
 *   - buffer-pool-name back-reference
 *   - AQM with ECN behavior and linear detection profile
 * then assigns the queue config to an interface and verifies the running
 * config.
 *
 * A second config covers the create/shape/delete lifecycle: a shaped queue,
 * then removal of the whole named entry.
 */

#include <folly/json/dynamic.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;

namespace {
// Stable names (see matching rationale in ConfigPfcTest.cpp). Shares the
// buffer pool name with ConfigPfcTest because FBOSS only permits one
// bufferPool in the running config.
constexpr auto kBufferPoolName = "cli_e2e_test_buffer_pool";
constexpr auto kQueueConfigName = "cli_e2e_test_queue_config";
// Separate name so the delete case cannot disturb BuildAndAssignPolicy's
// config (tests may be sharded across workers and are not ordered).
constexpr auto kDeleteQueueConfigName = "cli_e2e_test_qc_delete";

// Enum values from switch_config.thrift
constexpr int kSchedWrr = 0;
constexpr int kSchedSp = 1;
constexpr int kScalingTwo = 9;
constexpr int kStreamUnicast = 0;
constexpr int kStreamMulticast = 1;
constexpr int kBehaviorEcn = 1;
} // namespace

class ConfigPortQueueConfigTest : public Fboss2IntegrationTest {
 protected:
  std::string bufferPoolName_ = kBufferPoolName;
  std::string queueConfigName_ = kQueueConfigName;

  void SetUp() override {
    Fboss2IntegrationTest::SetUp();
    XLOG(INFO) << "Using buffer-pool: " << bufferPoolName_;
    XLOG(INFO) << "Using queue-config: " << queueConfigName_;
  }

  void configureBufferPool(int sharedBytes, int headroomBytes) {
    ASSERT_EQ(
        runCli({"config",
                "qos",
                "buffer-pool",
                bufferPoolName_,
                "shared-bytes",
                std::to_string(sharedBytes)})
            .exitCode,
        0);
    ASSERT_EQ(
        runCli({"config",
                "qos",
                "buffer-pool",
                bufferPoolName_,
                "headroom-bytes",
                std::to_string(headroomBytes)})
            .exitCode,
        0);
  }

  // Thin wrapper — just builds the long argv and forwards to runCli.
  void configureQueue(int queueId, const std::vector<std::string>& attrs) {
    std::vector<std::string> cmd = {
        "config",
        "qos",
        "queue-config",
        queueConfigName_,
        "queue-id",
        std::to_string(queueId)};
    cmd.insert(cmd.end(), attrs.begin(), attrs.end());
    auto result = runCli(cmd);
    ASSERT_EQ(result.exitCode, 0)
        << "queue-id " << queueId << " failed: " << result.stderr;
  }

  const folly::dynamic* findQueue(const folly::dynamic& list, int queueId)
      const {
    for (const auto& q : list) {
      if (q.isObject() && q.count("id") && q["id"].asInt() == queueId) {
        return &q;
      }
    }
    return nullptr;
  }
};

TEST_F(ConfigPortQueueConfigTest, BuildAndAssignPolicy) {
  std::string ifName = getRandomInterfacePortName();
  XLOG(INFO) << "Using test interface " << ifName;

  XLOG(INFO) << "[Step 1] Configuring buffer pool...";
  configureBufferPool(/*sharedBytes=*/1048576, /*headroomBytes=*/0);

  XLOG(INFO) << "[Step 2] Configuring queues...";
  configureQueue(
      2, {"scheduling", "SP", "shared-bytes", "1048576", "weight", "10"});
  configureQueue(
      6,
      {"scheduling", "SP", "shared-bytes", "1048576", "scaling-factor", "TWO"});
  configureQueue(
      7,
      {"scheduling",
       "WRR",
       "weight",
       "20",
       "reserved-bytes",
       "5000",
       "buffer-pool-name",
       bufferPoolName_});
  configureQueue(
      3, {"scheduling", "WRR", "weight", "15", "stream-type", "MULTICAST"});
  configureQueue(
      4,
      {"scheduling",
       "SP",
       "shared-bytes",
       "1048576",
       "active-queue-management",
       "congestion-behavior",
       "ECN",
       "detection",
       "linear",
       "minimum-length",
       "120000",
       "maximum-length",
       "120000",
       "probability",
       "100"});

  XLOG(INFO) << "[Step 3] Assigning queue-config to " << ifName;
  ASSERT_EQ(
      runCli({"config", "interface", ifName, "queue-config", queueConfigName_})
          .exitCode,
      0);

  XLOG(INFO) << "[Step 4] Committing...";
  commitConfig();
  waitForAgentReady();

  XLOG(INFO) << "[Step 5] Verifying running config...";
  auto config = getRunningConfig();
  const auto& sw = config["sw"];

  // Buffer pool
  ASSERT_TRUE(sw.count("bufferPoolConfigs"));
  const auto& pools = sw["bufferPoolConfigs"];
  ASSERT_TRUE(pools.count(bufferPoolName_))
      << "buffer pool " << bufferPoolName_ << " missing";
  EXPECT_EQ(pools[bufferPoolName_]["sharedBytes"].asInt(), 1048576);
  EXPECT_EQ(pools[bufferPoolName_]["headroomBytes"].asInt(), 0);

  // Named queue config
  ASSERT_TRUE(sw.count("portQueueConfigs"));
  const auto& queueConfigs = sw["portQueueConfigs"];
  ASSERT_TRUE(queueConfigs.count(queueConfigName_))
      << "queue config " << queueConfigName_ << " missing";
  const auto& queues = queueConfigs[queueConfigName_];

  // Queue 2: SP + shared + weight
  {
    const auto* q = findQueue(queues, 2);
    ASSERT_NE(q, nullptr);
    EXPECT_EQ((*q)["scheduling"].asInt(), kSchedSp);
    EXPECT_EQ((*q)["sharedBytes"].asInt(), 1048576);
    EXPECT_EQ((*q)["weight"].asInt(), 10);
    EXPECT_EQ(
        (*q).getDefault("streamType", kStreamUnicast).asInt(), kStreamUnicast);
  }
  // Queue 6: SP + scaling factor TWO
  {
    const auto* q = findQueue(queues, 6);
    ASSERT_NE(q, nullptr);
    EXPECT_EQ((*q)["scheduling"].asInt(), kSchedSp);
    EXPECT_EQ((*q)["scalingFactor"].asInt(), kScalingTwo);
  }
  // Queue 7: WRR + reserved + buffer-pool ref
  {
    const auto* q = findQueue(queues, 7);
    ASSERT_NE(q, nullptr);
    EXPECT_EQ((*q)["scheduling"].asInt(), kSchedWrr);
    EXPECT_EQ((*q)["weight"].asInt(), 20);
    EXPECT_EQ((*q)["reservedBytes"].asInt(), 5000);
    EXPECT_EQ((*q)["bufferPoolName"].asString(), bufferPoolName_);
  }
  // Queue 3: streamType MULTICAST
  {
    const auto* q = findQueue(queues, 3);
    ASSERT_NE(q, nullptr);
    EXPECT_EQ((*q)["streamType"].asInt(), kStreamMulticast);
    EXPECT_EQ((*q)["scheduling"].asInt(), kSchedWrr);
    EXPECT_EQ((*q)["weight"].asInt(), 15);
  }
  // Queue 4: AQM ECN w/ linear detection
  {
    const auto* q = findQueue(queues, 4);
    ASSERT_NE(q, nullptr);
    ASSERT_TRUE(q->count("aqms"));
    const auto& aqms = (*q)["aqms"];
    ASSERT_TRUE(aqms.isArray());
    ASSERT_FALSE(aqms.empty());
    EXPECT_EQ(aqms[0]["behavior"].asInt(), kBehaviorEcn);
    const auto& linear = aqms[0]["detection"]["linear"];
    EXPECT_EQ(linear["minimumLength"].asInt(), 120000);
    EXPECT_EQ(linear["maximumLength"].asInt(), 120000);
    EXPECT_EQ(linear["probability"].asInt(), 100);
  }

  // Interface assignment
  bool sawPort = false;
  for (const auto& port : sw["ports"]) {
    if (port.count("name") && port["name"].asString() == ifName) {
      sawPort = true;
      EXPECT_EQ(
          port.getDefault("portQueueConfigName", "").asString(),
          queueConfigName_);
      break;
    }
  }
  EXPECT_TRUE(sawPort) << "interface " << ifName << " not in running config";

  XLOG(INFO) << "TEST PASSED";
}

// Removing a whole named config has to survive the round trip: the agent must
// accept a config with the entry gone, not just the CLI drop it locally.
//
// The queue carries a rate-limit on the way through, so the round trip also
// covers the agent accepting a shaped queue. Which grammar forms the CLI
// parses is a unit-test question (CmdConfigQosQueueConfigTest), so only one
// form runs here.
TEST_F(ConfigPortQueueConfigTest, QueueConfigCreateShapeAndDelete) {
  const std::string name = kDeleteQueueConfigName;

  XLOG(INFO) << "[Step 1] Creating " << name;
  ASSERT_EQ(
      runCli({"config",
              "qos",
              "queue-config",
              name,
              "queue-id",
              "1",
              "scheduling",
              "WRR",
              "weight",
              "3",
              "rate-limit",
              "kbps",
              "12500000",
              "12500000"})
          .exitCode,
      0);
  commitConfig();
  waitForAgentReady();

  XLOG(INFO) << "[Step 2] Verifying running config";
  // Hold the config in a named local: getRunningConfig() returns by value, and
  // binding a reference to a subobject of the temporary would dangle.
  auto afterCreate = getRunningConfig();
  const auto& swAfterCreate = afterCreate["sw"];
  // Check the key before indexing -- folly::dynamic::operator[] on a missing
  // key inserts a null, and count() on a null throws rather than failing the
  // assertion readably.
  ASSERT_TRUE(swAfterCreate.count("portQueueConfigs"))
      << "no portQueueConfigs in running config after creating " << name;
  ASSERT_TRUE(swAfterCreate["portQueueConfigs"].count(name))
      << name << " missing after create";

  const auto& queues = swAfterCreate["portQueueConfigs"][name];
  const auto* q1 = findQueue(queues, 1);
  ASSERT_NE(q1, nullptr) << "queue 1 missing from " << name;
  ASSERT_TRUE(q1->count("portQueueRate"));
  const auto& kbps = (*q1)["portQueueRate"]["kbitsPerSec"];
  EXPECT_EQ(kbps["minimum"].asInt(), 12500000);
  EXPECT_EQ(kbps["maximum"].asInt(), 12500000);

  XLOG(INFO) << "[Step 3] Deleting " << name;
  discardSession();
  auto del = runCli({"delete", "qos", "queue-config", name});
  ASSERT_EQ(del.exitCode, 0) << "CLI failed: " << del.stderr;
  commitConfig();
  waitForAgentReady();

  auto afterDelete = getRunningConfig();
  const auto& swAfterDelete = afterDelete["sw"];
  EXPECT_FALSE(
      swAfterDelete.count("portQueueConfigs") &&
      swAfterDelete["portQueueConfigs"].count(name))
      << name << " still present after delete";
  XLOG(INFO) << "TEST PASSED";
}

// The bound-config delete guard is intentionally NOT tested here: it rejects
// at the staged session and never reaches the agent, so it needs no hardware.
// See CmdDeleteQosQueueConfigTest.refusesToDeleteBoundNamedConfig.
