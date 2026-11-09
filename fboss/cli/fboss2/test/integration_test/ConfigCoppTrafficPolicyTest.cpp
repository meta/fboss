// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * Integration test for:
 *   fboss2-dev config copp traffic-policy match <matcher> action
 *       <type> <value>
 *   fboss2-dev delete copp traffic-policy match <matcher> action <type>
 *
 * Exercises the full add -> delete cycle on a CPU-plane action field in
 * sw.cpuTrafficPolicy.trafficPolicy.matchToAction:
 *   1. inject a dedicated test ACL through the config session (matchToAction
 *      matchers must reference a configured ACL),
 *   2. set a send-to-queue action on it and commit,
 *   3. verify the action appears in the committed config,
 *   4. delete the action and commit,
 *   5. verify the action is gone.
 *
 * Restore strategy: SetUp snapshots the running config before any mutation;
 * TearDown compares the current committed config against the snapshot and,
 * only if they differ, writes the snapshot back through the config session
 * and commits hitlessly. This restores the pre-test config regardless of
 * where the test body failed and regardless of whether the matcher was
 * pre-existing or injected.
 */

#include <folly/json/dynamic.h>
#include <folly/json/json.h>
#include <folly/logging/xlog.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

namespace fs = std::filesystem;

using namespace facebook::fboss;

namespace {
constexpr std::string_view kActionType = "send-to-queue";
constexpr std::string_view kActionJsonField = "sendToQueue";
constexpr int kQueueId = 2;
// Matcher ACL injected when the running config has no usable ACL. The name
// marks it as test-owned so a leaked entry is easy to attribute and remove.
constexpr std::string_view kInjectedAclName = "fboss2-copp-test-acl";
} // namespace

class ConfigCoppTrafficPolicyTest : public Fboss2IntegrationTest {
 protected:
  // Running config captured before any mutation; TearDown restores to this.
  folly::dynamic snapshot_;

  void SetUp() override {
    Fboss2IntegrationTest::SetUp();
    snapshot_ = getRunningConfig();
    if (!snapshot_.isObject() || !snapshot_.count("sw")) {
      GTEST_SKIP() << "No sw config; skipping";
    }
    injectTestAcl();
  }

  void TearDown() override {
    restoreSnapshot();
    Fboss2IntegrationTest::TearDown();
  }

  static std::optional<folly::dynamic> findCpuMatchToAction(
      const folly::dynamic& config,
      const std::string& matcher) {
    if (!config.isObject() || !config.count("sw")) {
      return std::nullopt;
    }
    const auto& sw = config["sw"];
    if (!sw.count("cpuTrafficPolicy") ||
        !sw["cpuTrafficPolicy"].count("trafficPolicy") ||
        !sw["cpuTrafficPolicy"]["trafficPolicy"].count("matchToAction")) {
      return std::nullopt;
    }
    for (const auto& mta :
         sw["cpuTrafficPolicy"]["trafficPolicy"]["matchToAction"]) {
      if (mta.isObject() && mta.count("matcher") &&
          mta["matcher"].asString() == matcher) {
        return mta;
      }
    }
    return std::nullopt;
  }

  /**
   * ACL entry injected as the matcher when the running config has none.
   * dstIp-only qualifier; PERMIT on link-local is a behavioral no-op.
   */
  static folly::dynamic buildInjectedAclEntry() {
    return folly::dynamic::object("name", std::string(kInjectedAclName))(
        "dstIp", "fe80::/10")("actionType", 1 /* PERMIT */);
  }

  fs::path sessionConfigPath() const {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): Used only in test setup
    const char* home = std::getenv("HOME");
    EXPECT_NE(home, nullptr) << "HOME env var not set";
    return fs::path(home ? home : "") / ".fboss2" / "agent.conf";
  }

  /**
   * Initialize the config session and return its parsed JSON.
   *
   * The session file only materializes after a successful config command, so
   * run the copp CLI itself (it validates only that the matcher name is
   * non-empty); the dangling matchToAction it stages is stripped by the
   * caller before committing.
   */
  folly::dynamic initSessionAndRead() {
    auto result = runCli(
        {"config",
         "copp",
         "traffic-policy",
         "match",
         std::string(kInjectedAclName),
         "action",
         std::string(kActionType),
         std::to_string(kQueueId)});
    EXPECT_EQ(result.exitCode, 0)
        << "Session init via copp CLI failed: " << result.stderr;

    auto path = sessionConfigPath();
    EXPECT_TRUE(fs::exists(path))
        << "Session config not found after init: " << path;

    std::ifstream ifs(path);
    EXPECT_TRUE(ifs.is_open()) << "Cannot read session config";
    std::string content(
        std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>{});
    return folly::parseJson(content);
  }

  void writeSessionConfig(const folly::dynamic& cfg) {
    std::ofstream ofs(sessionConfigPath());
    ASSERT_TRUE(ofs.is_open()) << "Cannot write session config";
    ofs << folly::toJson(cfg);
  }

  static void stripInjectedMatchToAction(folly::dynamic& sw) {
    if (!sw.count("cpuTrafficPolicy") ||
        !sw["cpuTrafficPolicy"].count("trafficPolicy") ||
        !sw["cpuTrafficPolicy"]["trafficPolicy"].count("matchToAction")) {
      return;
    }
    auto& mtaList = sw["cpuTrafficPolicy"]["trafficPolicy"]["matchToAction"];
    folly::dynamic filtered = folly::dynamic::array;
    for (const auto& mta : mtaList) {
      if (!mta.isObject() || !mta.count("matcher") ||
          mta["matcher"].asString() != kInjectedAclName) {
        filtered.push_back(mta);
      }
    }
    mtaList = filtered;
  }

  /**
   * Add the test ACL to the session config so it can serve as a
   * matchToAction matcher, then commit hitlessly.
   *
   * Covers both agent modes: legacy (sw.acls, e.g. fboss-sim) and
   * multi-ACL-table (aclTableGroups[].aclTables[].aclEntries, e.g. DUTs
   * bootstrapped with enable_acl_table_group=true).
   */
  void injectTestAcl() {
    XLOG(INFO) << "No usable ACL in running config; injecting '"
               << kInjectedAclName << "'";
    auto cfg = initSessionAndRead();
    auto& sw = cfg["sw"];

    // The session-init CLI call staged a matchToAction for the (not yet
    // existing) test ACL; drop it so the commit only introduces the ACL.
    stripInjectedMatchToAction(sw);

    auto entry = buildInjectedAclEntry();
    bool injectedIntoTableGroup = false;
    if (sw.count("aclTableGroups") && sw["aclTableGroups"].isArray()) {
      for (auto& group : sw["aclTableGroups"]) {
        if (!group.isObject() || !group.count("aclTables")) {
          continue;
        }
        for (auto& table : group["aclTables"]) {
          if (table.isObject() && table.count("aclEntries")) {
            table["aclEntries"].push_back(entry);
            injectedIntoTableGroup = true;
            break;
          }
        }
        if (injectedIntoTableGroup) {
          break;
        }
      }
    }
    if (!injectedIntoTableGroup) {
      if (!sw.count("acls") || !sw["acls"].isArray()) {
        sw["acls"] = folly::dynamic::array;
      }
      sw["acls"].push_back(entry);
    }

    writeSessionConfig(cfg);
    commitConfig();

    auto after = getRunningConfig();
    ASSERT_TRUE(after.isObject() && after.count("sw"))
        << "Running config unavailable after ACL injection";
  }

  /**
   * Write the SetUp snapshot into the config session and commit hitlessly,
   * reverting every config change made since the snapshot was taken.
   */
  void commitSnapshot() {
    XLOG(INFO) << "Restoring pre-test config snapshot";
    initSessionAndRead(); // materialize the session; staged content is
                          // replaced wholesale by the snapshot below
    writeSessionConfig(snapshot_);
    commitConfig();
  }

  /**
   * TearDown hook: decide whether the committed config drifted from the
   * SetUp snapshot and restore it if so. Must never throw (a throwing
   * TearDown aborts cleanup and leaks test state onto the switch).
   */
  void restoreSnapshot() {
    if (!snapshot_.isObject() || !snapshot_.count("sw")) {
      return; // nothing usable was captured; restoring would corrupt config
    }
    try {
      if (getRunningConfig() == snapshot_) {
        return;
      }
      commitSnapshot();
    } catch (const std::exception& ex) {
      ADD_FAILURE() << "Config snapshot restore failed; switch may be left "
                    << "with test config: " << ex.what();
    }
  }
};

TEST_F(ConfigCoppTrafficPolicyTest, addAndDeleteAction) {
  const std::string matcher{kInjectedAclName};
  XLOG(INFO) << "Using injected matcher '" << matcher << "'";

  // 1. Add the action and commit.
  auto setResult = runCli(
      {"config",
       "copp",
       "traffic-policy",
       "match",
       matcher,
       "action",
       std::string(kActionType),
       std::to_string(kQueueId)});
  ASSERT_EQ(setResult.exitCode, 0) << "CLI failed: " << setResult.stderr;
  EXPECT_THAT(
      setResult.stdout, ::testing::HasSubstr("Set copp traffic-policy action"));

  commitConfig();

  auto afterSet = findCpuMatchToAction(getRunningConfig(), matcher);
  ASSERT_TRUE(afterSet.has_value())
      << "matchToAction entry for '" << matcher << "' missing after set";
  ASSERT_TRUE(
      (*afterSet).count("action") &&
      (*afterSet)["action"].count(std::string(kActionJsonField)))
      << "Action field missing after set + commit";
  EXPECT_EQ(
      (*afterSet)["action"][std::string(kActionJsonField)]["queueId"].asInt(),
      kQueueId);

  // 2. Delete the action and commit.
  auto deleteResult = runCli(
      {"delete",
       "copp",
       "traffic-policy",
       "match",
       matcher,
       "action",
       std::string(kActionType)});
  ASSERT_EQ(deleteResult.exitCode, 0) << "CLI failed: " << deleteResult.stderr;
  EXPECT_THAT(
      deleteResult.stdout, ::testing::HasSubstr("Successfully deleted"));

  commitConfig();

  auto afterDelete = findCpuMatchToAction(getRunningConfig(), matcher);
  if (afterDelete.has_value()) {
    EXPECT_FALSE(
        (*afterDelete).count("action") &&
        (*afterDelete)["action"].count(std::string(kActionJsonField)))
        << "Action field still present after delete + commit";
  }
}
