// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end tests for the switch-wide default queue config:
 *   fboss2-dev config qos queue-config default queue-id <id> <attr> <value> ...
 *   fboss2-dev delete qos queue-config default [queue-id <id>]
 *
 * `default` is the reserved queue-config name that targets
 * SwitchConfig::defaultPortQueues rather than a portQueueConfigs entry; the
 * named-config path is covered by ConfigPortQueueConfigTest.
 *
 * Each case configures one realistic queue shape rather than one queue with
 * every attribute at once, so a failure names the scheduling discipline that
 * broke instead of pointing at a single kitchen-sink queue. Only what cannot be
 * checked off-device is covered here: that a committed config session survives
 * the agent restart and lands in the running config.
 *
 * Everything else already has coverage a layer down. Argument parsing,
 * attribute handling and rejection of malformed input are unit tested in
 * fboss/cli/fboss2/test/config/CmdConfigQosQueueConfigTest.cpp against a
 * seeded ConfigSession; whether the agent accepts a given cfg::PortQueue is
 * covered by fboss/agent/state/tests/PortQueueTests.cpp against the real
 * ThriftConfigApplier. Neither needs a device.
 *
 * Every commit restarts the agent, so each case commits once to apply and once
 * to clean up, and no case programs more than one queue.
 */

#include <folly/json/dynamic.h>
#include <folly/logging/xlog.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;

namespace {
// Enum values from switch_config.thrift
constexpr int kSchedWrr = 0;
constexpr int kSchedSp = 1;
constexpr int kScalingTwo = 9;
constexpr int kBehaviorEarlyDrop = 0;
constexpr int kBehaviorEcn = 1;

// defaultPortQueues applies to every port with no portQueueConfigName
// override, so these cases deliberately stay away from the large MMU numbers
// ConfigPortQueueConfigTest uses on a single bound interface -- the point is to
// prove each attribute survives the CLI -> agent.conf -> ThriftConfigApplier
// round trip, not to exercise buffer capacity fleet-wide. buffer-pool-name is
// skipped because it would require creating a pool first.
constexpr int kReservedBytes = 1024;
constexpr int kWeight = 5;
constexpr int kAqmMinLength = 40000;
constexpr int kAqmMaxLength = 60000;
constexpr int kAqmProbability = 100;
} // namespace

class ConfigQosDefaultQueueConfigTest : public Fboss2IntegrationTest {
 protected:
  void SetUp() override {
    Fboss2IntegrationTest::SetUp();
    discardSession();
    baselineIds_ = defaultQueueIds(getRunningConfig());
  }

  void TearDown() override {
    // Safety net: an assertion that fires mid-case skips the case's own
    // cleanup, and the queue is already committed to the running config of a
    // shared lab device. Remove it here so the next case (and the next run)
    // starts from the baseline.
    if (createdQueueId_.has_value()) {
      XLOG(INFO) << "TearDown: removing leftover queue-id " << *createdQueueId_;
      deleteQueueAndCommit(*createdQueueId_);
    }
    discardSession();
    Fboss2IntegrationTest::TearDown();
  }

  /**
   * Lowest queue id not already present in defaultPortQueues. Keeps each case
   * independent of how many queues the ASIC already has configured, including
   * none at all.
   */
  int pickUnusedQueueId() const {
    int candidate = 0;
    while (baselineIds_.count(candidate)) {
      candidate++;
    }
    return candidate;
  }

  /**
   * Configure `queueId` with `attrs` and commit. Records the id so TearDown can
   * remove it even if the caller's assertions fail. Returns the queue as it
   * appears in the agent's running config.
   */
  const folly::dynamic* configureAndCommit(
      int queueId,
      const std::vector<std::string>& attrs) {
    std::vector<std::string> cmd = {
        "config",
        "qos",
        "queue-config",
        "default",
        "queue-id",
        std::to_string(queueId)};
    cmd.insert(cmd.end(), attrs.begin(), attrs.end());

    auto result = runCli(cmd);
    EXPECT_EQ(result.exitCode, 0)
        << "queue-id " << queueId << " failed: " << result.stderr;
    if (result.exitCode != 0) {
      return nullptr;
    }
    createdQueueId_ = queueId;

    XLOG(INFO) << "Committing queue-id " << queueId << "...";
    commitConfig();
    waitForAgentReady();

    runningConfig_ = getRunningConfig();
    return findQueueById(runningConfig_, queueId);
  }

  void deleteQueueAndCommit(int queueId) {
    discardSession();
    auto del = runCli(
        {"delete",
         "qos",
         "queue-config",
         "default",
         "queue-id",
         std::to_string(queueId)});
    EXPECT_EQ(del.exitCode, 0) << "CLI failed: " << del.stderr;
    createdQueueId_.reset();

    XLOG(INFO) << "Committing delete of queue-id " << queueId << "...";
    commitConfig();
    waitForAgentReady();
  }

  /**
   * Return the defaultPortQueues entry with the given queue id as a
   * folly::dynamic object, or nullptr if no such entry exists.
   */
  const folly::dynamic* findQueueById(const folly::dynamic& config, int queueId)
      const {
    if (!config.isObject() || !config.count("sw") ||
        !config["sw"].count("defaultPortQueues")) {
      return nullptr;
    }
    const auto& queues = config["sw"]["defaultPortQueues"];
    if (!queues.isArray()) {
      return nullptr;
    }
    for (const auto& q : queues) {
      if (q.count("id") && q["id"].asInt() == queueId) {
        return &q;
      }
    }
    return nullptr;
  }

  /**
   * Return the set of queue ids present in the running config's
   * defaultPortQueues, or an empty set if the list is absent.
   */
  std::set<int> defaultQueueIds(const folly::dynamic& config) const {
    std::set<int> ids;
    if (!config.isObject() || !config.count("sw") ||
        !config["sw"].count("defaultPortQueues")) {
      return ids;
    }
    const auto& queues = config["sw"]["defaultPortQueues"];
    if (!queues.isArray()) {
      return ids;
    }
    for (const auto& q : queues) {
      if (q.count("id")) {
        ids.insert(q["id"].asInt());
      }
    }
    return ids;
  }

  std::set<int> baselineIds_;
  std::optional<int> createdQueueId_;
  // Owns the storage the folly::dynamic* returned by configureAndCommit()
  // points into.
  folly::dynamic runningConfig_;
};

// The linear detection profile is written one command at a time: the CLI seeds
// the linear struct from the entry already on the queue, applies only the
// attributes named, and writes it back. This case pins minimumLength -- that a
// later command moves it, and that moving it leaves the rest of the profile
// alone.
TEST_F(ConfigQosDefaultQueueConfigTest, AqmDetectionMinimumLength) {
  constexpr int kFirstMinLength = 20000;
  constexpr int kFinalMinLength = 35000;
  constexpr int kMaxLength = 60000;
  // ASICs without ECN_PROBABILISTIC_MARKING reject any ECN probability other
  // than 100, so use 100 to keep this test runnable on every platform.
  constexpr int kProbability = kAqmProbability;

  int queueId = pickUnusedQueueId();
  const auto* q = configureAndCommit(
      queueId,
      {"active-queue-management",
       "congestion-behavior",
       "ECN",
       "detection",
       "linear",
       "minimum-length",
       std::to_string(kFirstMinLength),
       "maximum-length",
       std::to_string(kMaxLength),
       "probability",
       std::to_string(kProbability)});
  ASSERT_NE(q, nullptr) << "queue-id " << queueId << " not in running config";
  ASSERT_TRUE(q->count("aqms"));
  ASSERT_FALSE((*q)["aqms"].empty());
  EXPECT_EQ(
      (*q)["aqms"][0]["detection"]["linear"]["minimumLength"].asInt(),
      kFirstMinLength);

  // Move minimum-length on its own. congestion-behavior still has to be named
  // -- it is the key that picks which AQM entry the edit lands on.
  discardSession();
  q = configureAndCommit(
      queueId,
      {"active-queue-management",
       "congestion-behavior",
       "ECN",
       "detection",
       "linear",
       "minimum-length",
       std::to_string(kFinalMinLength)});
  ASSERT_NE(q, nullptr) << "queue-id " << queueId << " not in running config";
  ASSERT_TRUE(q->count("aqms"));
  ASSERT_FALSE((*q)["aqms"].empty());
  const auto& linear = (*q)["aqms"][0]["detection"]["linear"];

  EXPECT_EQ(linear["minimumLength"].asInt(), kFinalMinLength)
      << "the second command must move minimumLength";
  EXPECT_EQ(linear["maximumLength"].asInt(), kMaxLength)
      << "a minimum-length edit must not disturb maximumLength";
  EXPECT_EQ(
      linear.getDefault("probability", kAqmProbability).asInt(), kProbability)
      << "a minimum-length edit must not reset probability to its default";

  deleteQueueAndCommit(queueId);
}

// Bulk traffic queue: shares bandwidth with its peers via weighted round robin.
// `WRR` also exercises the short-name scheduling alias.
TEST_F(ConfigQosDefaultQueueConfigTest, BulkTrafficQueueUsesWrrWeight) {
  int queueId = pickUnusedQueueId();
  const auto* q = configureAndCommit(
      queueId, {"scheduling", "WRR", "weight", std::to_string(kWeight)});

  ASSERT_NE(q, nullptr) << "queue-id " << queueId << " not in running config";
  ASSERT_TRUE(q->count("scheduling"));
  EXPECT_EQ((*q)["scheduling"].asInt(), kSchedWrr);
  ASSERT_TRUE(q->count("weight"));
  EXPECT_EQ((*q)["weight"].asInt(), kWeight);

  deleteQueueAndCommit(queueId);
}

// Latency-sensitive traffic: drains ahead of every lower-priority queue. No
// weight -- thrift ignores it for STRICT_PRIORITY.
TEST_F(
    ConfigQosDefaultQueueConfigTest,
    PriorityTrafficQueueUsesStrictPriority) {
  int queueId = pickUnusedQueueId();
  const auto* q = configureAndCommit(queueId, {"scheduling", "SP"});

  ASSERT_NE(q, nullptr) << "queue-id " << queueId << " not in running config";
  ASSERT_TRUE(q->count("scheduling"));
  EXPECT_EQ((*q)["scheduling"].asInt(), kSchedSp);

  deleteQueueAndCommit(queueId);
}

// maximumLength is the top of the drop ramp: at that queue length the
// probability applies in full. It is written by the same one-attribute-at-a-
// time path as the rest of the linear profile, so this case checks that an
// edit naming only maximum-length moves it and leaves minimumLength and
// probability as they were.
TEST_F(ConfigQosDefaultQueueConfigTest, AqmDetectionMaximumLength) {
  constexpr int kMinLength = 15000;
  constexpr int kFirstMaxLength = 50000;
  constexpr int kFinalMaxLength = 90000;
  // ASICs without ECN_PROBABILISTIC_MARKING reject any ECN probability other
  // than 100, so use 100 to keep this test runnable on every platform.
  constexpr int kProbability = kAqmProbability;

  int queueId = pickUnusedQueueId();
  const auto* q = configureAndCommit(
      queueId,
      {"active-queue-management",
       "congestion-behavior",
       "ECN",
       "detection",
       "linear",
       "minimum-length",
       std::to_string(kMinLength),
       "maximum-length",
       std::to_string(kFirstMaxLength),
       "probability",
       std::to_string(kProbability)});
  ASSERT_NE(q, nullptr) << "queue-id " << queueId << " not in running config";
  ASSERT_TRUE(q->count("aqms"));
  ASSERT_FALSE((*q)["aqms"].empty());
  EXPECT_EQ(
      (*q)["aqms"][0]["detection"]["linear"]["maximumLength"].asInt(),
      kFirstMaxLength);

  discardSession();
  q = configureAndCommit(
      queueId,
      {"active-queue-management",
       "congestion-behavior",
       "ECN",
       "detection",
       "linear",
       "maximum-length",
       std::to_string(kFinalMaxLength)});
  ASSERT_NE(q, nullptr) << "queue-id " << queueId << " not in running config";
  ASSERT_TRUE(q->count("aqms"));
  ASSERT_FALSE((*q)["aqms"].empty());
  const auto& linear = (*q)["aqms"][0]["detection"]["linear"];

  EXPECT_EQ(linear["maximumLength"].asInt(), kFinalMaxLength)
      << "the second command must move maximumLength";
  EXPECT_EQ(linear["minimumLength"].asInt(), kMinLength)
      << "a maximum-length edit must not disturb minimumLength";
  EXPECT_EQ(
      linear.getDefault("probability", kAqmProbability).asInt(), kProbability)
      << "a maximum-length edit must not reset probability to its default";

  deleteQueueAndCommit(queueId);
}

// Loss-sensitive traffic: guaranteed buffer plus a dynamic shared threshold, so
// a burst is absorbed rather than tail-dropped.
TEST_F(ConfigQosDefaultQueueConfigTest, LossSensitiveQueueCarvesBuffer) {
  int queueId = pickUnusedQueueId();
  const auto* q = configureAndCommit(
      queueId,
      {"reserved-bytes",
       std::to_string(kReservedBytes),
       "scaling-factor",
       "TWO"});

  ASSERT_NE(q, nullptr) << "queue-id " << queueId << " not in running config";
  ASSERT_TRUE(q->count("reservedBytes"));
  EXPECT_EQ((*q)["reservedBytes"].asInt(), kReservedBytes);
  ASSERT_TRUE(q->count("scalingFactor"));
  EXPECT_EQ((*q)["scalingFactor"].asInt(), kScalingTwo);

  deleteQueueAndCommit(queueId);
}

// ECN-capable traffic: mark Congestion Experienced on the way up the ramp
// instead of dropping. active-queue-management must come last in the argv --
// it consumes all remaining arguments.
TEST_F(ConfigQosDefaultQueueConfigTest, CongestionSignallingQueueMarksEcn) {
  int queueId = pickUnusedQueueId();
  const auto* q = configureAndCommit(
      queueId,
      {"active-queue-management",
       "congestion-behavior",
       "ECN",
       "detection",
       "linear",
       "minimum-length",
       std::to_string(kAqmMinLength),
       "maximum-length",
       std::to_string(kAqmMaxLength),
       "probability",
       std::to_string(kAqmProbability)});

  ASSERT_NE(q, nullptr) << "queue-id " << queueId << " not in running config";
  ASSERT_TRUE(q->count("aqms"));
  const auto& aqms = (*q)["aqms"];
  ASSERT_TRUE(aqms.isArray());
  ASSERT_FALSE(aqms.empty());
  EXPECT_EQ(aqms[0]["behavior"].asInt(), kBehaviorEcn);
  const auto& linear = aqms[0]["detection"]["linear"];
  EXPECT_EQ(linear["minimumLength"].asInt(), kAqmMinLength);
  EXPECT_EQ(linear["maximumLength"].asInt(), kAqmMaxLength);
  EXPECT_EQ(linear["probability"].asInt(), kAqmProbability);

  deleteQueueAndCommit(queueId);
}

// probability is the one field of the linear profile thrift gives a default
// to (100), which makes "never set" and "set to 100" the same running config
// and 0 a real failure. So this case starts without the attribute, checks the
// default came through, then sets it and checks the lengths configured
// earlier survived the edit. It uses EARLY_DROP rather than ECN: the agent
// rejects an ECN probability other than 100 on ASICs without
// ECN_PROBABILISTIC_MARKING, while EARLY_DROP takes any probability everywhere.
TEST_F(ConfigQosDefaultQueueConfigTest, AqmDetectionProbability) {
  constexpr int kMinLength = 25000;
  constexpr int kMaxLength = 45000;
  constexpr int kDefaultProbability = 100;
  constexpr int kProbability = 25;

  int queueId = pickUnusedQueueId();
  const auto* q = configureAndCommit(
      queueId,
      {"active-queue-management",
       "congestion-behavior",
       "EARLY_DROP",
       "detection",
       "linear",
       "minimum-length",
       std::to_string(kMinLength),
       "maximum-length",
       std::to_string(kMaxLength)});
  ASSERT_NE(q, nullptr) << "queue-id " << queueId << " not in running config";
  ASSERT_TRUE(q->count("aqms"));
  ASSERT_FALSE((*q)["aqms"].empty());
  EXPECT_EQ((*q)["aqms"][0]["behavior"].asInt(), kBehaviorEarlyDrop);
  // Absent in the JSON means the thrift default, which is why this reads
  // through getDefault rather than requiring the key. What it rules out is a
  // profile that came back with probability 0, i.e. a ramp that never drops.
  EXPECT_EQ(
      (*q)["aqms"][0]["detection"]["linear"]
          .getDefault("probability", kDefaultProbability)
          .asInt(),
      kDefaultProbability)
      << "probability was never configured, so it must read as its default";

  discardSession();
  q = configureAndCommit(
      queueId,
      {"active-queue-management",
       "congestion-behavior",
       "EARLY_DROP",
       "detection",
       "linear",
       "probability",
       std::to_string(kProbability)});
  ASSERT_NE(q, nullptr) << "queue-id " << queueId << " not in running config";
  ASSERT_TRUE(q->count("aqms"));
  ASSERT_FALSE((*q)["aqms"].empty());
  const auto& linear = (*q)["aqms"][0]["detection"]["linear"];

  EXPECT_EQ(linear["probability"].asInt(), kProbability)
      << "an explicit probability must replace the default";
  EXPECT_EQ(linear["minimumLength"].asInt(), kMinLength)
      << "a probability edit must not disturb minimumLength";
  EXPECT_EQ(linear["maximumLength"].asInt(), kMaxLength)
      << "a probability edit must not disturb maximumLength";

  deleteQueueAndCommit(queueId);
}

// Delete takes the entry back out and leaves defaultPortQueues exactly as it
// was found -- the one case that asserts on the post-delete state rather than
// just cleaning up after itself.
TEST_F(ConfigQosDefaultQueueConfigTest, DeleteRemovesQueueEntry) {
  int queueId = pickUnusedQueueId();
  ASSERT_NE(
      configureAndCommit(queueId, {"weight", std::to_string(kWeight)}), nullptr)
      << "queue-id " << queueId << " not in running config";

  discardSession();
  auto del = runCli(
      {"delete",
       "qos",
       "queue-config",
       "default",
       "queue-id",
       std::to_string(queueId)});
  ASSERT_EQ(del.exitCode, 0) << "CLI failed: " << del.stderr;
  EXPECT_THAT(del.stdout, ::testing::HasSubstr("Successfully deleted"));
  createdQueueId_.reset();

  XLOG(INFO) << "Committing delete...";
  commitConfig();
  waitForAgentReady();

  EXPECT_EQ(defaultQueueIds(getRunningConfig()), baselineIds_)
      << "defaultPortQueues ids differ from original after delete";
}
