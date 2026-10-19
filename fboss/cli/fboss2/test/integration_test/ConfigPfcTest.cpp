// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end test for PFC-related CLI commands:
 *   fboss2-dev config qos buffer-pool <name> <attr> <bytes>
 *       (<attr>: shared-bytes | headroom-bytes | reserved-bytes)
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
constexpr int kRecoveryDrop = 1;

// PFC watchdog timer values every supported ASIC accepts (see
// AgentPfcConfigTest).
constexpr int kWatchdogDetectionMsecs = 100;
constexpr int kWatchdogRecoveryMsecs = 1000;

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

  // What bindPortToPgPolicy() found already configured, so that
  // removePortPfc() deletes only what the test itself created.
  struct PfcChainState {
    bool hadPool{false};
    bool hadPolicy{false};
  };

  static bool hasNamedEntry(
      const folly::dynamic& config,
      const char* section,
      const std::string& name) {
    const auto& sw = config["sw"];
    return sw.count(section) && sw[section].isObject() &&
        sw[section].count(name);
  }

  // The pfc block of port `ifName` in `config`, or null when it has none.
  static folly::dynamic portPfc(
      const folly::dynamic& config,
      const std::string& ifName) {
    for (const auto& port : config["sw"]["ports"]) {
      if (port.count("name") && port["name"].asString() == ifName) {
        return port.getDefault("pfc", nullptr);
      }
    }
    return nullptr;
  }

  // The PFC watchdog of port `ifName` in `config`, or null when it has none.
  static folly::dynamic portWatchdog(
      const folly::dynamic& config,
      const std::string& ifName) {
    auto pfc = portPfc(config, ifName);
    return pfc.isObject() ? pfc.getDefault("watchdog", nullptr)
                          : folly::dynamic(nullptr);
  }

  // Ports other than `ifName` that already have a PFC watchdog.
  static std::vector<std::string> otherWatchdogPorts(
      const folly::dynamic& config,
      const std::string& ifName) {
    std::vector<std::string> ports;
    for (const auto& port : config["sw"]["ports"]) {
      if (port.count("name") && port["name"].asString() != ifName &&
          port.count("pfc") && port["pfc"].count("watchdog")) {
        ports.push_back(port["name"].asString());
      }
    }
    return ports;
  }

  // The agent requires every port with a watchdog to use the same recovery
  // action, so a test watchdog must match whatever the rest of the DUT
  // already uses. NO_DROP (the thrift default) when no other port has one.
  static int otherPortsRecoveryAction(
      const folly::dynamic& config,
      const std::string& ifName) {
    const auto others = otherWatchdogPorts(config, ifName);
    if (others.empty()) {
      return kRecoveryNoDrop;
    }
    return portWatchdog(config, others.front())
        .getDefault("recoveryAction", kRecoveryNoDrop)
        .asInt();
  }

  static std::string recoveryActionArg(int action) {
    return action == kRecoveryDrop ? "drop" : "no-drop";
  }

  // Puts `ifName` into a known PFC state: bound to this test's PG policy, on
  // this test's buffer pool, with PFC rx/tx enabled. A watchdog is only
  // accepted on a port bound to an existing priority-group policy, and some
  // ASICs (e.g. G202X) reject deadlock detection while PFC is off. The changes
  // are staged, not committed. `state` records what already existed.
  void bindPortToPgPolicy(const std::string& ifName, PfcChainState& state) {
    const auto config = getRunningConfig();
    state.hadPool = hasNamedEntry(config, "bufferPoolConfigs", bufferPoolName_);
    state.hadPolicy = hasNamedEntry(config, "portPgConfigs", policyName_);

    XLOG(INFO) << "Binding " << ifName << " to PG policy " << policyName_;
    // Default buffer sizes of the agent's PFC hardware tests (PfcTestUtils).
    // Larger per-PG headroom fails to allocate on some ASICs (e.g. TH5) and
    // the hw agent aborts.
    configureBufferPool(/*sharedBytes=*/1500000, /*headroomBytes=*/5000);
    const std::vector<PgSpec> pgs = {
        {0, "ONE_HALF", kOneHalf, 2200, 2200, 1800},
        {7, "ONE_HALF", kOneHalf, 2200, 2200, 1800},
    };
    for (const auto& pg : pgs) {
      configurePgMultiAttr(pg);
    }
    auto r = runCli(
        {"config",
         "interface",
         ifName,
         "pfc-config",
         "priority-group-policy",
         policyName_,
         "rx",
         "enabled",
         "tx",
         "enabled"});
    ASSERT_EQ(r.exitCode, 0) << r.stderr;
  }

  // Stages and commits a complete watchdog on `ifName`.
  void configureWatchdog(
      const std::string& ifName,
      int detectionMsecs,
      int recoveryMsecs,
      const std::string& actionArg) {
    XLOG(INFO) << "Watchdog on " << ifName << ": detection " << detectionMsecs
               << "ms, recovery " << recoveryMsecs << "ms, " << actionArg;
    auto r = runCli(
        {"config",
         "interface",
         ifName,
         "pfc-config",
         "watchdog-detection-time",
         std::to_string(detectionMsecs),
         "watchdog-recovery-time",
         std::to_string(recoveryMsecs),
         "watchdog-recovery-action",
         actionArg});
    ASSERT_EQ(r.exitCode, 0) << r.stderr;
    commitConfig();
    waitForAgentReady();
  }

  // Leaves the DUT as bindPortToPgPolicy() found it: removes the port's PFC
  // config, then the PG policy and buffer pool if the test created them, and
  // checks they are gone. Leaf first: each delete refuses while something
  // still references what it removes.
  void removePortPfc(const std::string& ifName, const PfcChainState& state) {
    XLOG(INFO) << "Removing the PFC config of " << ifName
               << ", the PG policy and the buffer pool";
    ASSERT_EQ(
        runCli({"delete", "interface", ifName, "pfc-config"}).exitCode, 0);
    if (!state.hadPolicy) {
      ASSERT_EQ(
          runCli({"delete", "qos", "priority-group-policy", policyName_})
              .exitCode,
          0);
    }
    if (!state.hadPool) {
      ASSERT_EQ(
          runCli({"delete", "qos", "buffer-pool", bufferPoolName_}).exitCode,
          0);
    }
    commitConfig();
    waitForAgentReady();

    const auto config = getRunningConfig();
    EXPECT_FALSE(portPfc(config, ifName).isObject())
        << "pfc still configured on " << ifName;
    EXPECT_EQ(
        hasNamedEntry(config, "portPgConfigs", policyName_), state.hadPolicy)
        << "PG policy " << policyName_ << " not restored to its original state";
    EXPECT_EQ(
        hasNamedEntry(config, "bufferPoolConfigs", bufferPoolName_),
        state.hadPool)
        << "buffer pool " << bufferPoolName_
        << " not restored to its original state";
  }

  // Shared body of the rx-duration and tx-duration tests. `attr` is the CLI
  // attribute, `flag` the PortPfc field it sets, and `otherFlag` the field of
  // the other direction. The flag is independent of PFC itself, so setting it
  // must not touch rx/tx or the other direction's flag. "disabled" stores an
  // explicit false.
  void checkPfcDurationFlag(
      const std::string& attr,
      const char* flag,
      const char* otherFlag) {
    std::string ifName = getRandomInterfacePortName();
    XLOG(INFO) << "Using test interface " << ifName;
    const std::string otherAttr =
        attr == "rx-duration" ? "tx-duration" : "rx-duration";

    // Known starting state: PFC on, bound to this test's PG policy, and the
    // other direction explicitly off. The hardware monitors one direction per
    // port (SaiPortManager programs a single PfcMonitorDirection), so with the
    // other flag on, this one would be accepted in config but not programmed.
    XLOG(INFO) << "[Step 1] Binding " << ifName << ", " << otherAttr
               << " disabled";
    PfcChainState chain;
    ASSERT_NO_FATAL_FAILURE(bindPortToPgPolicy(ifName, chain));
    auto r = runCli(
        {"config", "interface", ifName, "pfc-config", otherAttr, "disabled"});
    ASSERT_EQ(r.exitCode, 0) << r.stderr;
    commitConfig();
    waitForAgentReady();

    for (const bool enabled : {true, false}) {
      const std::string value = enabled ? "enabled" : "disabled";
      XLOG(INFO) << "[Step " << (enabled ? 2 : 3) << "] " << attr << " "
                 << value;
      r = runCli({"config", "interface", ifName, "pfc-config", attr, value});
      ASSERT_EQ(r.exitCode, 0) << r.stderr;
      commitConfig();
      waitForAgentReady();

      auto pfc = portPfc(getRunningConfig(), ifName);
      ASSERT_TRUE(pfc.isObject()) << "pfc missing on " << ifName;
      ASSERT_TRUE(pfc.count(flag))
          << flag << " not set to " << enabled << ": " << folly::toJson(pfc);
      EXPECT_EQ(pfc[flag].asBool(), enabled);
      EXPECT_FALSE(pfc.getDefault(otherFlag, false).asBool())
          << otherFlag << " changed by a " << attr << " command";
      EXPECT_TRUE(pfc.getDefault("rx", false).asBool())
          << "rx changed by a " << attr << " command";
      EXPECT_TRUE(pfc.getDefault("tx", false).asBool())
          << "tx changed by a " << attr << " command";
    }

    XLOG(INFO) << "[Step 4] Leaving the DUT as found";
    ASSERT_NO_FATAL_FAILURE(removePortPfc(ifName, chain));
  }
};

// watchdog-detection-time is how long a PFC queue may stay paused before the
// watchdog declares a deadlock. Changing it alone must edit the existing
// watchdog in place: recovery time and recovery action stay as they were.
TEST_F(ConfigPfcTest, WatchdogDetectionTimeMsecs) {
  std::string ifName = getRandomInterfacePortName();
  XLOG(INFO) << "Using test interface " << ifName;
  constexpr int kNewDetectionMsecs = 150;

  const int action = otherPortsRecoveryAction(getRunningConfig(), ifName);

  XLOG(INFO) << "[Step 1] Full watchdog on a port bound to a PG policy";
  PfcChainState chain;
  ASSERT_NO_FATAL_FAILURE(bindPortToPgPolicy(ifName, chain));
  ASSERT_NO_FATAL_FAILURE(configureWatchdog(
      ifName,
      kWatchdogDetectionMsecs,
      kWatchdogRecoveryMsecs,
      recoveryActionArg(action)));

  auto wd = portWatchdog(getRunningConfig(), ifName);
  ASSERT_TRUE(wd.isObject()) << "watchdog missing on " << ifName;
  EXPECT_EQ(wd["detectionTimeMsecs"].asInt(), kWatchdogDetectionMsecs);

  XLOG(INFO) << "[Step 2] Only watchdog-detection-time " << kNewDetectionMsecs;
  ASSERT_EQ(
      runCli({"config",
              "interface",
              ifName,
              "pfc-config",
              "watchdog-detection-time",
              std::to_string(kNewDetectionMsecs)})
          .exitCode,
      0);
  commitConfig();
  waitForAgentReady();

  wd = portWatchdog(getRunningConfig(), ifName);
  ASSERT_TRUE(wd.isObject()) << "watchdog missing on " << ifName;
  EXPECT_EQ(wd["detectionTimeMsecs"].asInt(), kNewDetectionMsecs);
  EXPECT_EQ(wd["recoveryTimeMsecs"].asInt(), kWatchdogRecoveryMsecs)
      << "recovery time reset by a detection-time command";
  EXPECT_EQ(wd["recoveryAction"].asInt(), action)
      << "recovery action reset by a detection-time command";

  XLOG(INFO) << "[Step 3] Leaving the DUT as found";
  ASSERT_NO_FATAL_FAILURE(removePortPfc(ifName, chain));

  XLOG(INFO) << "TEST PASSED";
}

// watchdog-recovery-time is how long the watchdog keeps a deadlocked queue in
// recovery before it goes back to honouring PFC. Changing it alone must edit
// the existing watchdog in place: detection time and recovery action stay as
// they were.
TEST_F(ConfigPfcTest, WatchdogRecoveryTimeMsecs) {
  std::string ifName = getRandomInterfacePortName();
  XLOG(INFO) << "Using test interface " << ifName;
  constexpr int kNewRecoveryMsecs = 500;

  const int action = otherPortsRecoveryAction(getRunningConfig(), ifName);

  XLOG(INFO) << "[Step 1] Full watchdog on a port bound to a PG policy";
  PfcChainState chain;
  ASSERT_NO_FATAL_FAILURE(bindPortToPgPolicy(ifName, chain));
  ASSERT_NO_FATAL_FAILURE(configureWatchdog(
      ifName,
      kWatchdogDetectionMsecs,
      kWatchdogRecoveryMsecs,
      recoveryActionArg(action)));

  auto wd = portWatchdog(getRunningConfig(), ifName);
  ASSERT_TRUE(wd.isObject()) << "watchdog missing on " << ifName;
  EXPECT_EQ(wd["recoveryTimeMsecs"].asInt(), kWatchdogRecoveryMsecs);

  XLOG(INFO) << "[Step 2] Only watchdog-recovery-time " << kNewRecoveryMsecs;
  ASSERT_EQ(
      runCli({"config",
              "interface",
              ifName,
              "pfc-config",
              "watchdog-recovery-time",
              std::to_string(kNewRecoveryMsecs)})
          .exitCode,
      0);
  commitConfig();
  waitForAgentReady();

  wd = portWatchdog(getRunningConfig(), ifName);
  ASSERT_TRUE(wd.isObject()) << "watchdog missing on " << ifName;
  EXPECT_EQ(wd["recoveryTimeMsecs"].asInt(), kNewRecoveryMsecs);
  EXPECT_EQ(wd["detectionTimeMsecs"].asInt(), kWatchdogDetectionMsecs)
      << "detection time reset by a recovery-time command";
  EXPECT_EQ(wd["recoveryAction"].asInt(), action)
      << "recovery action reset by a recovery-time command";

  XLOG(INFO) << "[Step 3] Leaving the DUT as found";
  ASSERT_NO_FATAL_FAILURE(removePortPfc(ifName, chain));

  XLOG(INFO) << "TEST PASSED";
}

TEST_F(ConfigPfcTest, BufferPoolReservedBytes) {
  // Focused coverage for BufferPoolConfig.reservedBytes:
  //   fboss2-dev config qos buffer-pool <name> reserved-bytes <bytes>
  //
  // reservedBytes is an `optional i32` in cfg::BufferPoolConfig, and that is
  // what separates this test from the sharedBytes / headroomBytes coverage
  // in BufferPoolPriorityGroupAndPortPfc:
  //   * a pool the CLI creates without a reserved-bytes attribute must not
  //     acquire one -- the handler default-initializes sharedBytes only,
  //   * the field is a scalar, so a second reserved-bytes command overwrites
  //     the first rather than accumulating,
  //   * a reserved-bytes command must leave sharedBytes and headroomBytes
  //     exactly as they were; it writes that one field and nothing else.
  //
  // The pool name is deliberately the shared, stable kBufferPoolName -- the
  // agent rejects a second pool ("Only one bufferPool supported!"), so a
  // unique-per-run name is not an option here. The values below are distinct
  // from the ones the other buffer-pool tests write, so a pool left behind by
  // one of them cannot satisfy these assertions by accident.
  constexpr int kSharedBytes = 33554432; // 32 MiB
  constexpr int kHeadroomBytes = 1048576; // 1 MiB
  constexpr int kFirstReservedBytes = 524288; // 512 KiB
  constexpr int kFinalReservedBytes = 3145728; // 3 MiB

  // The absence check in step 2 is only meaningful if the pool does not
  // already carry a reservedBytes: there is no CLI to unset the field, so a
  // run that died before TearDown can leave one behind.
  bool reservedAbsentAtStart = true;
  {
    auto before = getRunningConfig();
    const auto& sw = before["sw"];
    if (sw.count("bufferPoolConfigs") &&
        sw["bufferPoolConfigs"].count(bufferPoolName_)) {
      reservedAbsentAtStart =
          sw["bufferPoolConfigs"][bufferPoolName_].count("reservedBytes") == 0;
    }
  }

  XLOG(INFO) << "[Step 1] Configuring the pool without any reserved-bytes";
  ASSERT_EQ(
      runCli({"config",
              "qos",
              "buffer-pool",
              bufferPoolName_,
              "shared-bytes",
              std::to_string(kSharedBytes)})
          .exitCode,
      0);
  ASSERT_EQ(
      runCli({"config",
              "qos",
              "buffer-pool",
              bufferPoolName_,
              "headroom-bytes",
              std::to_string(kHeadroomBytes)})
          .exitCode,
      0);
  commitConfig();
  waitForAgentReady();

  XLOG(INFO) << "[Step 2] reservedBytes must still be absent";
  {
    auto config = getRunningConfig();
    const auto& sw = config["sw"];
    ASSERT_TRUE(sw.count("bufferPoolConfigs"))
        << "bufferPoolConfigs missing from running config -- was the config "
           "committed?";
    const auto& pools = sw["bufferPoolConfigs"];
    ASSERT_TRUE(pools.count(bufferPoolName_))
        << "buffer pool '" << bufferPoolName_ << "' not in running config";
    const auto& pool = pools[bufferPoolName_];
    EXPECT_EQ(pool["sharedBytes"].asInt(), kSharedBytes);
    EXPECT_EQ(pool["headroomBytes"].asInt(), kHeadroomBytes);
    if (reservedAbsentAtStart) {
      EXPECT_EQ(pool.count("reservedBytes"), 0)
          << "reservedBytes is optional and no command asked for it, so the "
             "pool must not carry one";
    } else {
      XLOG(WARN) << "pool already had a reservedBytes before the test ran; "
                    "skipping the absence check";
    }
  }

  XLOG(INFO) << "[Step 3] Setting reserved-bytes twice in one session";
  // The pool now exists in the committed config, so these commands take the
  // handler's update-in-place path rather than the create path. The second
  // command must overwrite the first: reservedBytes is a scalar, not a list.
  ASSERT_EQ(
      runCli({"config",
              "qos",
              "buffer-pool",
              bufferPoolName_,
              "reserved-bytes",
              std::to_string(kFirstReservedBytes)})
          .exitCode,
      0);
  ASSERT_EQ(
      runCli({"config",
              "qos",
              "buffer-pool",
              bufferPoolName_,
              "reserved-bytes",
              std::to_string(kFinalReservedBytes)})
          .exitCode,
      0);
  commitConfig();
  waitForAgentReady();

  XLOG(INFO) << "[Step 4] Verifying running config...";
  auto config = getRunningConfig();
  const auto& sw = config["sw"];
  ASSERT_TRUE(sw.count("bufferPoolConfigs"));
  const auto& pools = sw["bufferPoolConfigs"];
  ASSERT_TRUE(pools.count(bufferPoolName_))
      << "buffer pool '" << bufferPoolName_ << "' not in running config";
  const auto& pool = pools[bufferPoolName_];

  ASSERT_TRUE(pool.count("reservedBytes"))
      << "reserved-bytes did not reach the running config";
  EXPECT_EQ(pool["reservedBytes"].asInt(), kFinalReservedBytes)
      << "the second reserved-bytes command must overwrite the first";

  // reserved-bytes must not have disturbed its neighbours in the struct --
  // this is what catches an attribute dispatched to the wrong field.
  EXPECT_EQ(pool["sharedBytes"].asInt(), kSharedBytes)
      << "reserved-bytes must leave sharedBytes untouched";
  EXPECT_EQ(pool["headroomBytes"].asInt(), kHeadroomBytes)
      << "reserved-bytes must leave headroomBytes untouched";

  XLOG(INFO) << "TEST PASSED";
}

// rx-duration turns on the per-port counter of how long the port was paused by
// PFC frames it received.
TEST_F(ConfigPfcTest, RxPfcDurationEnable) {
  checkPfcDurationFlag(
      "rx-duration", "rxPfcDurationEnable", "txPfcDurationEnable");
  if (!HasFailure()) {
    XLOG(INFO) << "TEST PASSED";
  }
}

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

// tx-duration turns on the per-port counter of how long the port spent
// sending PFC pause frames to its neighbor.
TEST_F(ConfigPfcTest, TxPfcDurationEnable) {
  checkPfcDurationFlag(
      "tx-duration", "txPfcDurationEnable", "rxPfcDurationEnable");
  if (!HasFailure()) {
    XLOG(INFO) << "TEST PASSED";
  }
}

// watchdog-recovery-action is what the switch does while it breaks a PFC
// deadlock: "drop" discards the stuck queue's packets, "no-drop" ignores pause
// frames and keeps sending. NO_DROP is the thrift default, so the test writes
// DROP first and then NO_DROP to prove both values land. Changing only the
// action must leave the two watchdog timers alone.
TEST_F(ConfigPfcTest, WatchdogRecoveryAction) {
  std::string ifName = getRandomInterfacePortName();
  XLOG(INFO) << "Using test interface " << ifName;

  // The agent requires every port with a watchdog to use the same recovery
  // action, so ports that already have one are switched along with the test
  // port and put back at the end.
  const auto startConfig = getRunningConfig();
  const auto otherPorts = otherWatchdogPorts(startConfig, ifName);
  const int startAction = otherPortsRecoveryAction(startConfig, ifName);
  auto setOtherPortsAction = [&](const std::string& actionArg) {
    for (const auto& port : otherPorts) {
      auto r = runCli(
          {"config",
           "interface",
           port,
           "pfc-config",
           "watchdog-recovery-action",
           actionArg});
      ASSERT_EQ(r.exitCode, 0) << r.stderr;
    }
  };

  XLOG(INFO) << "[Step 1] Full watchdog with drop, on a port bound to a PG "
             << "policy (" << otherPorts.size() << " other watchdog ports)";
  PfcChainState chain;
  ASSERT_NO_FATAL_FAILURE(bindPortToPgPolicy(ifName, chain));
  ASSERT_NO_FATAL_FAILURE(setOtherPortsAction("drop"));
  ASSERT_NO_FATAL_FAILURE(configureWatchdog(
      ifName, kWatchdogDetectionMsecs, kWatchdogRecoveryMsecs, "drop"));

  auto wd = portWatchdog(getRunningConfig(), ifName);
  ASSERT_TRUE(wd.isObject()) << "watchdog missing on " << ifName;
  EXPECT_EQ(wd["recoveryAction"].asInt(), kRecoveryDrop);

  XLOG(INFO) << "[Step 2] Only watchdog-recovery-action no-drop";
  ASSERT_NO_FATAL_FAILURE(setOtherPortsAction("no-drop"));
  ASSERT_EQ(
      runCli({"config",
              "interface",
              ifName,
              "pfc-config",
              "watchdog-recovery-action",
              "no-drop"})
          .exitCode,
      0);
  commitConfig();
  waitForAgentReady();

  wd = portWatchdog(getRunningConfig(), ifName);
  ASSERT_TRUE(wd.isObject()) << "watchdog missing on " << ifName;
  EXPECT_EQ(wd["recoveryAction"].asInt(), kRecoveryNoDrop);
  EXPECT_EQ(wd["detectionTimeMsecs"].asInt(), kWatchdogDetectionMsecs)
      << "detection time reset by a recovery-action command";
  EXPECT_EQ(wd["recoveryTimeMsecs"].asInt(), kWatchdogRecoveryMsecs)
      << "recovery time reset by a recovery-action command";

  XLOG(INFO) << "[Step 3] Leaving the DUT as found";
  ASSERT_NO_FATAL_FAILURE(setOtherPortsAction(recoveryActionArg(startAction)));
  ASSERT_NO_FATAL_FAILURE(removePortPfc(ifName, chain));
  const auto endConfig = getRunningConfig();
  for (const auto& port : otherPorts) {
    EXPECT_EQ(
        portWatchdog(endConfig, port)
            .getDefault("recoveryAction", kRecoveryNoDrop)
            .asInt(),
        startAction)
        << "recovery action of " << port << " not restored";
  }

  XLOG(INFO) << "TEST PASSED";
}
