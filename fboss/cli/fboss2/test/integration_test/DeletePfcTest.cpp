// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * End-to-end test for the PFC delete commands:
 *
 *   delete interface <port> pfc-config [watchdog|rx-duration|tx-duration]
 *   delete qos priority-group-policy <name>
 *   delete qos buffer-pool <name>
 *
 * The test builds the same buffer-pool -> priority-group -> port PFC chain
 * ConfigPfcTest builds, then dismantles it from the leaf inward. Along the way
 * it checks the two refusals: a policy still bound to a port, and a pool still
 * named by a priority group. Deleting in dependency order leaves the DUT with
 * none of the scratch objects, so no snapshot/restore is needed.
 *
 * Names are the stable ones ConfigPfcTest and ConfigPortQueueConfigTest use:
 * the agent accepts only one buffer pool, so a unique-per-run name would
 * collide with a leftover instead of overwriting it. The flip side is that
 * those suites' leftovers (a queue config naming the pool, a port still bound
 * to the policy) would make the final deletes refuse for reasons unrelated to
 * this test, so the test unbinds them first.
 */

#include <folly/json/dynamic.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;

class DeletePfcTest : public Fboss2IntegrationTest {
 protected:
  static constexpr auto kPool = "cli_e2e_test_buffer_pool";
  static constexpr auto kPolicy = "cli_e2e_test_pg_policy";

  static const folly::dynamic* findPort(
      const folly::dynamic& cfg,
      const std::string& name) {
    if (!cfg.isObject() || !cfg.count("sw") || !cfg["sw"].count("ports")) {
      return nullptr;
    }
    for (const auto& port : cfg["sw"]["ports"]) {
      if (port.count("name") && port["name"].asString() == name) {
        return &port;
      }
    }
    return nullptr;
  }

  static bool hasPfc(const folly::dynamic& cfg, const std::string& port) {
    const auto* p = findPort(cfg, port);
    return p != nullptr && p->count("pfc");
  }

  static bool hasWatchdog(const folly::dynamic& cfg, const std::string& port) {
    const auto* p = findPort(cfg, port);
    return p != nullptr && p->count("pfc") && (*p)["pfc"].count("watchdog");
  }

  static bool hasPolicy(const folly::dynamic& cfg) {
    return cfg.isObject() && cfg.count("sw") &&
        cfg["sw"].count("portPgConfigs") &&
        cfg["sw"]["portPgConfigs"].count(kPolicy);
  }

  static bool hasPool(const folly::dynamic& cfg) {
    return cfg.isObject() && cfg.count("sw") &&
        cfg["sw"].count("bufferPoolConfigs") &&
        cfg["sw"]["bufferPoolConfigs"].count(kPool);
  }

  // PFC edits commit with an agent restart. Wait for the agents to come back
  // before polling the running config, or the 30s config poll gives up first.
  void commitAndWait() {
    commitConfig();
    waitForAgentReady();
  }

  void runOk(const std::vector<std::string>& args) {
    auto r = runCli(args);
    ASSERT_EQ(r.exitCode, 0) << "stdout=" << r.stdout << " stderr=" << r.stderr;
  }

  // Remove whatever an earlier suite (or an aborted run of this one) left
  // pointing at kPolicy or kPool, so the deletes at the end refuse only for
  // the reasons this test sets up. Handles: ports bound to kPolicy, and named
  // queue configs with a queue on kPool plus the ports bound to them.
  void unbindStaleReferrers() {
    auto cfg = getRunningConfig();
    if (!cfg.isObject() || !cfg.count("sw")) {
      return;
    }
    const auto& sw = cfg["sw"];

    std::vector<std::string> staleQueueConfigs;
    if (sw.count("portQueueConfigs")) {
      for (const auto& [name, queues] : sw["portQueueConfigs"].items()) {
        for (const auto& q : queues) {
          if (q.count("bufferPoolName") &&
              q["bufferPoolName"].asString() == kPool) {
            staleQueueConfigs.push_back(name.asString());
            break;
          }
        }
      }
    }

    bool touched = false;
    if (sw.count("ports")) {
      for (const auto& p : sw["ports"]) {
        const auto name = p["name"].asString();
        if (p.count("pfc") && p["pfc"].count("portPgConfigName") &&
            p["pfc"]["portPgConfigName"].asString() == kPolicy) {
          XLOG(INFO) << "Unbinding stale pfc-config on " << name;
          runOk({"delete", "interface", name, "pfc-config"});
          touched = true;
        }
        if (p.count("portQueueConfigName") &&
            std::find(
                staleQueueConfigs.begin(),
                staleQueueConfigs.end(),
                p["portQueueConfigName"].asString()) !=
                staleQueueConfigs.end()) {
          XLOG(INFO) << "Unbinding stale queue-config on " << name;
          runOk({"delete", "interface", name, "queue-config"});
          touched = true;
        }
      }
    }
    for (const auto& qc : staleQueueConfigs) {
      XLOG(INFO) << "Removing stale queue config " << qc;
      runOk({"delete", "qos", "queue-config", qc});
      touched = true;
    }
    if (touched) {
      commitAndWait();
    }
  }
};

TEST_F(DeletePfcTest, DeleteChainLeafInward) {
  const std::string port = getRandomInterfacePortName();
  XLOG(INFO) << "Using test interface " << port;

  unbindStaleReferrers();

  // 1. Build the chain: pool, one PG naming the pool, port PFC binding the
  //    policy with a watchdog.
  // headroom-bytes is optional in the config schema but not to the hw agent:
  // SaiBufferManager::setupIngressBufferPool dereferences it unguarded once a
  // priority group binds the pool, and the agent crash-loops without it.
  // Keep the pool small: on TH5 a near-ITM-sized pool on top of the agent's
  // default egress pool oversubscribes the MMU and SAI rejects the PG bind.
  runOk(
      {"config",
       "qos",
       "buffer-pool",
       kPool,
       "shared-bytes",
       "1048576",
       "headroom-bytes",
       "65536"});
  runOk(
      {"config",
       "qos",
       "priority-group-policy",
       kPolicy,
       "group-id",
       "2",
       "min-limit-bytes",
       "14478",
       "buffer-pool-name",
       kPool});
  runOk(
      {"config",
       "interface",
       port,
       "pfc-config",
       "tx",
       "enabled",
       "rx",
       "enabled",
       "priority-group-policy",
       kPolicy,
       "watchdog-detection-time",
       "150",
       "watchdog-recovery-time",
       "1000",
       "watchdog-recovery-action",
       "no-drop"});
  commitAndWait();
  auto cfg = waitForRunningConfig([&](const folly::dynamic& c) {
    return hasPool(c) && hasPolicy(c) && hasWatchdog(c, port);
  });
  ASSERT_TRUE(hasWatchdog(cfg, port)) << "setup did not land";

  // 2. Policy still bound to the port: delete must refuse and leave it.
  XLOG(INFO) << "Expect refusal: policy bound to " << port;
  auto r = runCli({"delete", "qos", "priority-group-policy", kPolicy});
  EXPECT_NE(r.exitCode, 0);
  EXPECT_NE(
      (r.stdout + r.stderr).find("still bound to interface(s)"),
      std::string::npos)
      << r.stdout << r.stderr;

  // 3. Reset just the watchdog; the rest of the pfc struct stays.
  XLOG(INFO) << "Deleting watchdog on " << port;
  runOk({"delete", "interface", port, "pfc-config", "watchdog"});
  commitAndWait();
  cfg = waitForRunningConfig(
      [&](const folly::dynamic& c) { return !hasWatchdog(c, port); });
  EXPECT_FALSE(hasWatchdog(cfg, port));
  EXPECT_TRUE(hasPfc(cfg, port)) << "watchdog reset must not drop pfc";

  // 4. Remove the whole pfc struct.
  XLOG(INFO) << "Deleting pfc-config on " << port;
  runOk({"delete", "interface", port, "pfc-config"});
  commitAndWait();
  cfg = waitForRunningConfig(
      [&](const folly::dynamic& c) { return !hasPfc(c, port); });
  EXPECT_FALSE(hasPfc(cfg, port));

  // 5. Pool still named by the PG: delete must refuse.
  XLOG(INFO) << "Expect refusal: pool referenced by " << kPolicy;
  r = runCli({"delete", "qos", "buffer-pool", kPool});
  EXPECT_NE(r.exitCode, 0);
  EXPECT_NE(
      (r.stdout + r.stderr).find("still referenced by"), std::string::npos)
      << r.stdout << r.stderr;

  // 6. Policy is unbound now; delete it, then the pool.
  XLOG(INFO) << "Deleting policy " << kPolicy << " and pool " << kPool;
  runOk({"delete", "qos", "priority-group-policy", kPolicy});
  runOk({"delete", "qos", "buffer-pool", kPool});
  commitAndWait();
  cfg = waitForRunningConfig(
      [&](const folly::dynamic& c) { return !hasPolicy(c) && !hasPool(c); });
  EXPECT_FALSE(hasPolicy(cfg));
  EXPECT_FALSE(hasPool(cfg));
}
