// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end coverage for the `fboss2-dev` acl table commands, kept to the
 * one loop that needs a live agent: create a table, commit, delete it,
 * commit again (added by the delete commit on this branch). Everything
 * commit-less (validation, refusals, cascades) lives in the unit suites.
 *
 * Requirements:
 *   - FBOSS agent running with enable_acl_table_group=true and
 *     sw.aclTableGroups holding at least one group. With the flag off the
 *     agent never reads aclTableGroups and `config acl table` refuses, so the
 *     suite skips instead of reporting a failure that is not the CLI's.
 *   - Run as a non-root user with write access to /etc/coop.
 */

#include <folly/json/dynamic.h>
#include <folly/logging/xlog.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;
using ::testing::HasSubstr;

class ConfigAclTest : public Fboss2IntegrationTest {
 protected:
  // `config acl table` refuses when enable_acl_table_group is off
  // (acl_utils::requireAclTableGroupMode), because the agent would never read
  // what the command writes. Checking the same flag here keeps a single-table
  // DUT reporting "skipped" rather than a failure that is not the CLI's.
  void SetUp() override {
    Fboss2IntegrationTest::SetUp();
    const auto config = getRunningConfig();
    const auto* args = config.get_ptr("defaultCommandLineArgs");
    const auto* flag = args ? args->get_ptr("enable_acl_table_group") : nullptr;
    if (!flag || flag->asString() != "true") {
      GTEST_SKIP() << "enable_acl_table_group is off on this DUT; "
                      "`config acl table` is refused here";
    }
  }

  // Returns the first AclTableGroup object from the running config, or
  // std::nullopt when the DUT has no sw.aclTableGroups (field 56).
  std::optional<folly::dynamic> getFirstAclTableGroup() const {
    auto config = getRunningConfig();
    if (!config.isObject() || !config.count("sw")) {
      return std::nullopt;
    }
    const auto& sw = config["sw"];
    if (!sw.count("aclTableGroups") || !sw["aclTableGroups"].isArray() ||
        sw["aclTableGroups"].empty()) {
      return std::nullopt;
    }
    return sw["aclTableGroups"][0];
  }
};

// The basic delete loop on a live agent: create, commit, delete, commit.
// checkTrafficPolicyAclsExistInConfig walks both policies and rejects a
// matcher naming a rule that no longer exists, so the second commit is the
// assertion that the cascade worked; what exactly gets stripped from each
// policy is unit territory (deleteTableCascadesIntoBothPolicies).
TEST_F(ConfigAclTest, DeleteTableCascadesAndCommits) {
  auto group = getFirstAclTableGroup();
  if (!group.has_value()) {
    GTEST_SKIP() << "DUT config has no aclTableGroups to target";
  }
  const auto groupName = (*group)["name"].asString();
  const std::string kTable = "acl-cli-it-table";
  const std::string kRule = "acl-cli-it-rule";

  XLOG(INFO) << "[Step 1] Creating table '" << kTable << "' in " << groupName;
  auto result = runCli(
      {"config", "acl", "table", kTable, "group", groupName, "priority", "3"});
  ASSERT_EQ(result.exitCode, 0) << result.stderr;

  XLOG(INFO) << "[Step 2] Adding a rule with a dataplane action";
  result = runCli({"config", "acl", "rule", kTable, kRule, "dscp", "46"});
  ASSERT_EQ(result.exitCode, 0) << result.stderr;
  // The traffic-policy verb writes the dataPlaneTrafficPolicy matcher this
  // test needs the delete to cascade into.
  result = runCli(
      {"config",
       "data-plane",
       "traffic-policy",
       "match",
       kRule,
       "action",
       "set-dscp",
       "32"});
  ASSERT_EQ(result.exitCode, 0) << result.stderr;
  commitConfig();

  XLOG(INFO) << "[Step 3] Deleting the table";
  result = runCli({"delete", "acl", "table", kTable});
  ASSERT_EQ(result.exitCode, 0) << result.stderr;
  EXPECT_THAT(result.stdout, HasSubstr(kTable));

  // This commit is the real assertion: a leftover matcher would fail it with
  // "Invalid config: No acl named acl-cli-it-rule found".
  XLOG(INFO) << "[Step 4] Committing after the delete";
  commitConfig();

  auto config = getRunningConfig();
  const auto& sw = config["sw"];
  bool tableStillThere = false;
  for (const auto& g : sw["aclTableGroups"]) {
    if (!g.count("aclTables")) {
      continue;
    }
    for (const auto& t : g["aclTables"]) {
      if (t["name"].asString() == kTable) {
        tableStillThere = true;
      }
    }
  }
  EXPECT_FALSE(tableStillThere) << "table still present after delete";

  XLOG(INFO) << "  PASSED: acl table delete cascade";
}
