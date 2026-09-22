// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end test for PFC-related CLI commands:
 *   fboss2-dev config qos buffer-pool <name> shared-bytes/headroom-bytes
 *   fboss2-dev config qos priority-group-policy <name> group-id <id> <attrs>
 *   fboss2-dev config interface <port> pfc-config <attrs>
 *
 * Exercises both the single-attribute and multi-attribute forms of the
 * priority-group-policy and pfc-config commands, then verifies the agent's
 * running config.
 */

#include <fmt/format.h>
#include <folly/json/dynamic.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;

namespace {
// Use stable (non-unique) names because the agent only supports a single
// bufferPool in the running config ("Only one bufferPool supported!").
// Unique-per-run names would accumulate pools across runs and wedge the
// agent on next restart. With stable names each run overwrites the prior
// definition. Shared with ConfigPortQueueConfigTest for the same reason.
constexpr auto kBufferPoolName = "cli_e2e_test_buffer_pool";
constexpr auto kPolicyName = "cli_e2e_test_pg_policy";

// MMUScalingFactor enum values from switch_config.thrift
constexpr int kOneHalf = 8;
constexpr int kTwo = 9;
// PfcWatchdogRecoveryAction enum: NO_DROP=0, DROP=1
constexpr int kRecoveryNoDrop = 0;

struct PgSpec {
  int id;
  const char* scalingFactorName; // "ONE_HALF" / "TWO"
  int scalingFactorInt;
  int64_t minLimitBytes;
  int64_t headroomLimitBytes;
  int64_t resumeOffsetBytes;
};
} // namespace

class ConfigPfcTest : public Fboss2IntegrationTest {
 protected:
  std::string bufferPoolName_ = kBufferPoolName;
  std::string policyName_ = kPolicyName;

  void SetUp() override {
    Fboss2IntegrationTest::SetUp();
    XLOG(INFO) << "Using buffer-pool: " << bufferPoolName_;
    XLOG(INFO) << "Using pg-policy:  " << policyName_;
  }

  void TearDown() override {
    if (pfcIntfName_.has_value()) {
      // Disable PFC so subsequent tests (e.g. flow-control/PAUSE tests) can
      // use the same port without hitting the "PAUSE and PFC cannot be enabled
      // on the same port" rejection from the agent.
      XLOG(INFO) << "TearDown: disabling PFC on " << *pfcIntfName_
                 << " (port used by this test)";
      bool anyFailed = false;
      for (const auto* dir : {"tx", "rx"}) {
        auto result = runCli(
            {"config",
             "interface",
             *pfcIntfName_,
             "pfc-config",
             dir,
             "disabled"});
        if (result.exitCode != 0) {
          XLOG(WARN) << "TearDown: failed to disable pfc-config " << dir
                     << " on " << *pfcIntfName_ << ": " << result.stderr;
          anyFailed = true;
        } else {
          XLOG(INFO) << "TearDown: pfc-config " << dir << " disabled on "
                     << *pfcIntfName_;
        }
      }
      auto commitResult = runCli({"config", "session", "commit"});
      if (commitResult.exitCode != 0) {
        XLOG(WARN) << "TearDown: commit after PFC disable failed: "
                   << commitResult.stderr;
      } else if (!anyFailed) {
        XLOG(INFO) << "TearDown: PFC cleanup committed successfully on "
                   << *pfcIntfName_;
      }
    }
    Fboss2IntegrationTest::TearDown();
  }

  // Recorded by the test body so TearDown knows which interface to clean up.
  std::optional<std::string> pfcIntfName_;

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

  // Configure a priority group by issuing one attribute per CLI call.
  void configurePgSingleAttr(const PgSpec& pg) {
    std::vector<std::string> base = {
        "config",
        "qos",
        "priority-group-policy",
        policyName_,
        "group-id",
        std::to_string(pg.id)};
    auto run = [&](std::initializer_list<std::string> extras) {
      std::vector<std::string> cmd = base;
      cmd.insert(cmd.end(), extras);
      auto r = runCli(cmd);
      ASSERT_EQ(r.exitCode, 0) << r.stderr;
    };
    run({"min-limit-bytes", std::to_string(pg.minLimitBytes)});
    run({"headroom-limit-bytes", std::to_string(pg.headroomLimitBytes)});
    run({"resume-offset-bytes", std::to_string(pg.resumeOffsetBytes)});
    run({"scaling-factor", pg.scalingFactorName});
    run({"buffer-pool-name", bufferPoolName_});
  }

  // Configure a priority group in one CLI call with all attributes.
  void configurePgMultiAttr(const PgSpec& pg) {
    auto r = runCli(
        {"config",
         "qos",
         "priority-group-policy",
         policyName_,
         "group-id",
         std::to_string(pg.id),
         "min-limit-bytes",
         std::to_string(pg.minLimitBytes),
         "headroom-limit-bytes",
         std::to_string(pg.headroomLimitBytes),
         "resume-offset-bytes",
         std::to_string(pg.resumeOffsetBytes),
         "scaling-factor",
         pg.scalingFactorName,
         "buffer-pool-name",
         bufferPoolName_});
    ASSERT_EQ(r.exitCode, 0) << r.stderr;
  }

  const folly::dynamic* findPg(const folly::dynamic& list, int id) const {
    for (const auto& pg : list) {
      if (pg.isObject() && pg.count("id") && pg["id"].asInt() == id) {
        return &pg;
      }
    }
    return nullptr;
  }

  void expectPgMatch(const folly::dynamic& actual, const PgSpec& expected) {
    EXPECT_EQ(actual["sramScalingFactor"].asInt(), expected.scalingFactorInt);
    EXPECT_EQ(actual["minLimitBytes"].asInt(), expected.minLimitBytes);
    EXPECT_EQ(
        actual["headroomLimitBytes"].asInt(), expected.headroomLimitBytes);
    EXPECT_EQ(actual["resumeOffsetBytes"].asInt(), expected.resumeOffsetBytes);
    EXPECT_EQ(actual["bufferPoolName"].asString(), bufferPoolName_);
    EXPECT_EQ(actual["name"].asString(), fmt::format("pg{}", expected.id));
  }
};

TEST_F(ConfigPfcTest, BufferPoolPriorityGroupAndPortPfc) {
  std::string ifName = getRandomInterfacePortName();
  pfcIntfName_ = ifName;
  XLOG(INFO) << "Using test interface " << ifName;

  XLOG(INFO) << "[Step 1] Configuring buffer pool...";
  configureBufferPool(/*sharedBytes=*/78773528, /*headroomBytes=*/4405376);

  const std::vector<PgSpec> pgs = {
      {2, "ONE_HALF", kOneHalf, 14478, 726440, 9652},
      {6, "ONE_HALF", kOneHalf, 4826, 726440, 9652},
      {0, "TWO", kTwo, 4826, 0, 9652},
      {7, "TWO", kTwo, 4826, 0, 9652},
  };

  XLOG(INFO) << "[Step 2a] Configuring PGs with single-attribute commands...";
  configurePgSingleAttr(pgs[0]);
  configurePgSingleAttr(pgs[1]);
  XLOG(INFO) << "[Step 2b] Configuring PGs with multi-attribute commands...";
  configurePgMultiAttr(pgs[2]);
  configurePgMultiAttr(pgs[3]);

  XLOG(INFO) << "[Step 3] Configuring port PFC on " << ifName;
  // Single-attribute form for tx / rx / pg-policy binding.
  ASSERT_EQ(
      runCli({"config", "interface", ifName, "pfc-config", "tx", "enabled"})
          .exitCode,
      0);
  ASSERT_EQ(
      runCli({"config", "interface", ifName, "pfc-config", "rx", "enabled"})
          .exitCode,
      0);
  ASSERT_EQ(
      runCli({"config",
              "interface",
              ifName,
              "pfc-config",
              "priority-group-policy",
              policyName_})
          .exitCode,
      0);
  // Multi-attribute form for the watchdog bundle.
  ASSERT_EQ(
      runCli({"config",
              "interface",
              ifName,
              "pfc-config",
              "watchdog-detection-time",
              "150",
              "watchdog-recovery-time",
              "1000",
              "watchdog-recovery-action",
              "no-drop"})
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
  ASSERT_TRUE(pools.count(bufferPoolName_));
  EXPECT_EQ(pools[bufferPoolName_]["sharedBytes"].asInt(), 78773528);
  EXPECT_EQ(pools[bufferPoolName_]["headroomBytes"].asInt(), 4405376);

  // Priority group policy
  ASSERT_TRUE(sw.count("portPgConfigs"));
  const auto& pgConfigs = sw["portPgConfigs"];
  ASSERT_TRUE(pgConfigs.count(policyName_));
  const auto& pgList = pgConfigs[policyName_];
  for (const auto& spec : pgs) {
    const auto* actual = findPg(pgList, spec.id);
    ASSERT_NE(actual, nullptr)
        << "PG id " << spec.id << " missing in running config";
    expectPgMatch(*actual, spec);
  }

  // Per-port PFC
  bool sawPort = false;
  for (const auto& port : sw["ports"]) {
    if (!port.count("name") || port["name"].asString() != ifName) {
      continue;
    }
    sawPort = true;
    ASSERT_TRUE(port.count("pfc")) << "pfc missing on " << ifName;
    const auto& pfc = port["pfc"];
    EXPECT_TRUE(pfc["tx"].asBool());
    EXPECT_TRUE(pfc["rx"].asBool());
    EXPECT_EQ(pfc["portPgConfigName"].asString(), policyName_);
    ASSERT_TRUE(pfc.count("watchdog"));
    const auto& wd = pfc["watchdog"];
    EXPECT_EQ(wd["detectionTimeMsecs"].asInt(), 150);
    EXPECT_EQ(wd["recoveryTimeMsecs"].asInt(), 1000);
    EXPECT_EQ(wd["recoveryAction"].asInt(), kRecoveryNoDrop);
    break;
  }
  EXPECT_TRUE(sawPort) << "interface " << ifName << " not in running config";

  XLOG(INFO) << "TEST PASSED";
}

TEST_F(ConfigPfcTest, BufferPoolSharedBytes) {
  // Focused coverage for BufferPoolConfig.sharedBytes:
  //   fboss2-dev config qos buffer-pool <name> shared-bytes <bytes>
  //
  // BufferPoolPriorityGroupAndPortPfc already proves a single-attribute
  // command reaches the running config, so this test covers what that one
  // does not: that a later command overwrites an earlier value rather than
  // being ignored, and that the multi-attribute form of the command sets
  // sharedBytes just as the single-attribute form does.
  //
  // The pool name is deliberately the shared, stable kBufferPoolName -- the
  // agent rejects a second pool ("Only one bufferPool supported!"), so a
  // unique-per-run name is not an option here. The values below are distinct
  // from the ones BufferPoolPriorityGroupAndPortPfc writes, so a pool left
  // behind by that test cannot satisfy these assertions by accident.
  constexpr int kFirstSharedBytes = 12582912; // 12 MiB
  constexpr int kFinalSharedBytes = 67108864; // 64 MiB
  constexpr int kHeadroomBytes = 2097152; // 2 MiB

  XLOG(INFO) << "[Step 1] Setting shared-bytes with the single-attribute form";
  ASSERT_EQ(
      runCli({"config",
              "qos",
              "buffer-pool",
              bufferPoolName_,
              "shared-bytes",
              std::to_string(kFirstSharedBytes)})
          .exitCode,
      0);

  XLOG(INFO) << "[Step 2] Overwriting via the multi-attribute form";
  // Two things at once: the multi-attribute form is accepted, and the
  // sharedBytes it carries replaces the value set in step 1.
  ASSERT_EQ(
      runCli({"config",
              "qos",
              "buffer-pool",
              bufferPoolName_,
              "shared-bytes",
              std::to_string(kFinalSharedBytes),
              "headroom-bytes",
              std::to_string(kHeadroomBytes)})
          .exitCode,
      0);

  XLOG(INFO) << "[Step 3] Committing (buffer-pool changes restart the agent)";
  commitConfig();
  waitForAgentReady();

  XLOG(INFO) << "[Step 4] Verifying running config...";
  auto config = getRunningConfig();
  const auto& sw = config["sw"];
  ASSERT_TRUE(sw.count("bufferPoolConfigs"))
      << "bufferPoolConfigs missing from running config -- was the config "
         "committed?";
  const auto& pools = sw["bufferPoolConfigs"];
  ASSERT_TRUE(pools.count(bufferPoolName_))
      << "buffer pool '" << bufferPoolName_ << "' not in running config";
  const auto& pool = pools[bufferPoolName_];

  ASSERT_TRUE(pool.count("sharedBytes"))
      << "sharedBytes missing -- it is a non-optional field and must always "
         "be present";
  EXPECT_EQ(pool["sharedBytes"].asInt(), kFinalSharedBytes)
      << "the later command must overwrite the earlier shared-bytes value";
  EXPECT_NE(pool["sharedBytes"].asInt(), kFirstSharedBytes)
      << "running config still holds the value from step 1";

  // The headroom set alongside it in the same command must have landed too,
  // confirming the multi-attribute form applied every pair and not just the
  // first.
  ASSERT_TRUE(pool.count("headroomBytes"));
  EXPECT_EQ(pool["headroomBytes"].asInt(), kHeadroomBytes);

  XLOG(INFO) << "TEST PASSED";
}
