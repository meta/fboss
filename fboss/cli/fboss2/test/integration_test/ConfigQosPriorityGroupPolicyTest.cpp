// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * End-to-end test for the static shared-buffer threshold of a priority group:
 *
 *   config qos priority-group-policy <policy> group-id <id> \
 *       static-limit-bytes <bytes>
 *
 * staticLimitBytes is field 15 of cfg::PortPgConfig and is optional, so the
 * interesting behavior is what happens around it: a priority group configured
 * without it must not acquire one, a second command must overwrite the first
 * rather than adding a second entry for the same group id, and neither the
 * other attributes of the same group nor the other groups in the policy may
 * change.
 *
 * Names are the stable ones ConfigPfcTest uses: the agent
 * accepts only one buffer pool, so a unique-per-run name would collide with a
 * leftover instead of overwriting it. The group ids are ones no other suite
 * configures, so their state here is this test's own.
 */

#include <folly/json/dynamic.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;

class ConfigQosPriorityGroupPolicyTest : public Fboss2IntegrationTest {
 protected:
  static constexpr auto kPool = "cli_e2e_test_buffer_pool";
  static constexpr auto kPolicy = "cli_e2e_test_pg_policy";
  // MMUScalingFactor::ONE_HALF, from switch_config.thrift.
  static constexpr int kOneHalf = 8;

  void runOk(const std::vector<std::string>& args) {
    auto r = runCli(args);
    ASSERT_EQ(r.exitCode, 0) << "stdout=" << r.stdout << " stderr=" << r.stderr;
  }

  // All entries of kPolicy with the given group id. A list, not a single
  // entry, because "how many" is one of the things the test asserts.
  static std::vector<const folly::dynamic*> findGroups(
      const folly::dynamic& cfg,
      int id) {
    std::vector<const folly::dynamic*> out;
    if (!cfg.isObject() || !cfg.count("sw") ||
        !cfg["sw"].count("portPgConfigs") ||
        !cfg["sw"]["portPgConfigs"].count(kPolicy)) {
      return out;
    }
    for (const auto& pg : cfg["sw"]["portPgConfigs"][kPolicy]) {
      if (pg.isObject() && pg.count("id") && pg["id"].asInt() == id) {
        out.push_back(&pg);
      }
    }
    return out;
  }
};

TEST_F(ConfigQosPriorityGroupPolicyTest, StaticLimitBytes) {
  // Two groups: one the test sets a static limit on, one it never touches
  // after setup, which is how the test tells "wrote the right entry" from
  // "wrote the policy".
  constexpr int kTargetGroup = 3;
  constexpr int kOtherGroup = 4;
  constexpr int kMinLimitBytes = 14478;
  constexpr int kFirstStaticLimitBytes = 1048576; // 1 MiB
  constexpr int kFinalStaticLimitBytes = 7340032; // 7 MiB

  // There is no command to unset staticLimitBytes, and the test does not
  // remove the policy, so an earlier run can leave one behind. Only if the
  // groups start without one does the absence check below mean anything.
  bool staticAbsentAtStart = true;
  {
    auto before = getRunningConfig();
    for (int id : {kTargetGroup, kOtherGroup}) {
      for (const auto* pg : findGroups(before, id)) {
        if (pg->count("staticLimitBytes")) {
          staticAbsentAtStart = false;
        }
      }
    }
  }

  XLOG(INFO) << "[Step 1] Creating the pool and two groups, no static limit";
  // headroom-bytes is optional in the schema but not to the hw agent:
  // SaiBufferManager::setupIngressBufferPool dereferences it unguarded once a
  // priority group binds the pool.
  runOk(
      {"config",
       "qos",
       "buffer-pool",
       kPool,
       "shared-bytes",
       "78773528",
       "headroom-bytes",
       "4405376"});
  for (int id : {kTargetGroup, kOtherGroup}) {
    runOk(
        {"config",
         "qos",
         "priority-group-policy",
         kPolicy,
         "group-id",
         std::to_string(id),
         "min-limit-bytes",
         std::to_string(kMinLimitBytes),
         "scaling-factor",
         "ONE_HALF",
         "buffer-pool-name",
         kPool});
  }
  commitConfig();
  waitForAgentReady();

  XLOG(INFO) << "[Step 2] Neither group may have a static limit yet";
  {
    auto config = getRunningConfig();
    for (int id : {kTargetGroup, kOtherGroup}) {
      auto groups = findGroups(config, id);
      ASSERT_EQ(groups.size(), 1u)
          << "expected exactly one entry for group id " << id;
      EXPECT_EQ((*groups[0])["minLimitBytes"].asInt(), kMinLimitBytes);
      EXPECT_EQ((*groups[0])["sramScalingFactor"].asInt(), kOneHalf);
      if (staticAbsentAtStart) {
        EXPECT_EQ(groups[0]->count("staticLimitBytes"), 0)
            << "staticLimitBytes is optional and nothing asked for it on "
               "group id "
            << id;
      }
    }
    if (!staticAbsentAtStart) {
      XLOG(WARN) << "a group already carried a staticLimitBytes before the "
                    "test ran; skipping the absence check";
    }
  }

  XLOG(INFO) << "[Step 3] Setting static-limit-bytes twice on group "
             << kTargetGroup;
  // The group already exists in the committed config, so these commands take
  // the handler's "scan the list for this id" path. The second value must
  // replace the first in the same entry.
  for (int bytes : {kFirstStaticLimitBytes, kFinalStaticLimitBytes}) {
    runOk(
        {"config",
         "qos",
         "priority-group-policy",
         kPolicy,
         "group-id",
         std::to_string(kTargetGroup),
         "static-limit-bytes",
         std::to_string(bytes)});
  }
  commitConfig();
  waitForAgentReady();

  XLOG(INFO) << "[Step 4] Verifying running config...";
  auto config = getRunningConfig();

  auto target = findGroups(config, kTargetGroup);
  ASSERT_EQ(target.size(), 1u)
      << "repeated commands must update the existing entry for group id "
      << kTargetGroup << ", not append another one";
  const auto& pg = *target[0];

  ASSERT_TRUE(pg.count("staticLimitBytes"))
      << "static-limit-bytes did not reach the running config";
  EXPECT_EQ(pg["staticLimitBytes"].asInt(), kFinalStaticLimitBytes)
      << "the second static-limit-bytes command must overwrite the first";

  // The hardware ignores the scaling factor once a static limit is set, but
  // the CLI writes one field at a time: everything configured in step 1 has
  // to survive untouched.
  EXPECT_EQ(pg["minLimitBytes"].asInt(), kMinLimitBytes);
  EXPECT_EQ(pg["sramScalingFactor"].asInt(), kOneHalf);
  EXPECT_EQ(pg["bufferPoolName"].asString(), kPool);

  auto other = findGroups(config, kOtherGroup);
  ASSERT_EQ(other.size(), 1u);
  EXPECT_EQ(other[0]->count("staticLimitBytes"), 0)
      << "group id " << kOtherGroup
      << " was never given a static limit and must not have one";

  XLOG(INFO) << "TEST PASSED";
}
