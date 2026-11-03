// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end tests for `fboss2-dev config protocol bgp peer-group <name>
 * [<attribute> <value> ...]`.
 *
 * Scope: the per-peer-group attributes. Every test stages the change AND
 * commits it, then asserts the value landed at the correct thrift field path
 * inside the matching .peer_groups[] entry of bgpd's running config (via
 * getRunningConfig RPC). Stage-only behavior (attribute parsing, validation,
 * rejection) is covered by the unit tests; an integration test that never
 * commits exercises no daemon.
 *
 * Requirements:
 *   - The fboss2-dev binary under test (config subcommand tree).
 *   - HOME is set (the session file lives under $HOME/.fboss2).
 *   - bgpd is installed/active (commit restarts it).
 */

#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/test/integration_test/ConfigBgpTestBase.h"
#include "folly/json/dynamic.h"
#include "gmock/gmock.h"

using namespace facebook::fboss;
using ::testing::HasSubstr;
using ::testing::Not;

namespace {
// Test-only peer-group names, unlikely to collide with a real group in the
// device's running BGP config.
const std::string kGroup = "FBOSS2-TEST-GROUP";
} // namespace

class ConfigBgpPeerGroupTest : public ConfigBgpTestBase {
 protected:
  // Stage `config protocol bgp peer-group <tokens...>` WITHOUT clearing the
  // staged session, so attributes can accumulate across invocations. Returns
  // the staged session JSON.
  folly::dynamic stagePeerGroup(const std::vector<std::string>& tokens) {
    std::vector<std::string> args = {"config", "protocol", "bgp", "peer-group"};
    args.insert(args.end(), tokens.begin(), tokens.end());
    auto result = runCli(args);
    EXPECT_EQ(result.exitCode, 0)
        << "stdout=" << result.stdout << " stderr=" << result.stderr;
    EXPECT_THAT(result.stdout, Not(HasSubstr("Error:")));
    return readBgpSessionConfig();
  }

  // The .peer_groups[] entry named `name`, or nullptr.
  static const folly::dynamic* findGroup(
      const folly::dynamic& config,
      const std::string& name) {
    if (config.count("peer_groups") == 0) {
      return nullptr;
    }
    for (const auto& group : config["peer_groups"]) {
      if (group.count("name") && group["name"].asString() == name) {
        return &group;
      }
    }
    return nullptr;
  }
};

TEST_F(ConfigBgpPeerGroupTest, SetRemoteAsnAndCommit) {
  discardSession();
  clearBgpSession();
  stagePeerGroup({kGroup, "remote-asn", "65000"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  // Two layers: the promoted file proves what the commit wrote; the daemon's
  // getRunningConfig RPC proves bgpd parsed and adopted it after the restart.
  auto config = readSystemBgpConfig();
  const auto* group = findGroup(config, kGroup);
  ASSERT_NE(group, nullptr) << "committed config has no peer-group " << kGroup;
  ASSERT_TRUE(group->count("remote_as_4_byte"));
  EXPECT_EQ((*group)["remote_as_4_byte"].asInt(), 65000);

  auto running = readRunningBgpConfigViaRpc();
  const auto* runningGroup = findGroup(running, kGroup);
  ASSERT_NE(runningGroup, nullptr)
      << "bgpd's running config has no peer-group " << kGroup;
  EXPECT_EQ((*runningGroup)["remote_as_4_byte"].asInt(), 65000);
}

TEST_F(ConfigBgpPeerGroupTest, SetTimersHoldTimeAndCommit) {
  discardSession();
  clearBgpSession();
  stagePeerGroup({kGroup, "timers", "hold-time", "90"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  // Verify nested timer fields through the daemon's own view of its config.
  auto running = readRunningBgpConfigViaRpc();
  const auto* group = findGroup(running, kGroup);
  ASSERT_NE(group, nullptr)
      << "bgpd's running config has no peer-group " << kGroup;
  ASSERT_TRUE(group->count("bgp_peer_timers"));
  EXPECT_EQ((*group)["bgp_peer_timers"]["hold_time_seconds"].asInt(), 90);
  // bgpd applies the group's timers struct to its members as a whole, so the
  // fields the user did not set must carry the global defaults (hold/3
  // keepalive), not the thrift default of 0 that would disable keepalives.
  ASSERT_TRUE(running.count("hold_time"));
  EXPECT_EQ(
      (*group)["bgp_peer_timers"]["keep_alive_seconds"].asInt(),
      running["hold_time"].asInt() / 3);
}

TEST_F(ConfigBgpPeerGroupTest, SetPassiveAndCommit) {
  discardSession();
  clearBgpSession();
  stagePeerGroup({kGroup, "passive", "true"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  auto running = readRunningBgpConfigViaRpc();
  const auto* group = findGroup(running, kGroup);
  ASSERT_NE(group, nullptr)
      << "bgpd's running config has no peer-group " << kGroup;
  ASSERT_TRUE(group->count("is_passive"));
  EXPECT_TRUE((*group)["is_passive"].asBool());
}

TEST_F(ConfigBgpPeerGroupTest, SessionKnobsAndCommit) {
  // The per-session knobs bgpd resolves peer > group (remove-private-as,
  // ttl-security-hops) reach the daemon's running config via the same path.
  discardSession();
  clearBgpSession();
  stagePeerGroup({kGroup, "remove-private-as", "true"});
  stagePeerGroup({kGroup, "ttl-security-hops", "2"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  auto running = readRunningBgpConfigViaRpc();
  const auto* group = findGroup(running, kGroup);
  ASSERT_NE(group, nullptr)
      << "bgpd's running config has no peer-group " << kGroup;
  ASSERT_TRUE(group->count("remove_private_as"));
  EXPECT_TRUE((*group)["remove_private_as"].asBool());
  ASSERT_TRUE(group->count("ttl_security_hops"));
  EXPECT_EQ((*group)["ttl_security_hops"].asInt(), 2);
}

TEST_F(ConfigBgpPeerGroupTest, RouteLimitWarningKeepsUnlimitedCap) {
  // A warning threshold on a group with no route limit must not introduce the
  // 12000-route thrift default: bgpd reads max_routes 0 as unlimited, which
  // is what the group's members had before the edit.
  discardSession();
  clearBgpSession();
  stagePeerGroup({kGroup, "max-route", "pre-warning-threshold", "30000"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  auto running = readRunningBgpConfigViaRpc();
  const auto* group = findGroup(running, kGroup);
  ASSERT_NE(group, nullptr)
      << "bgpd's running config has no peer-group " << kGroup;
  ASSERT_TRUE(group->count("pre_filter"));
  EXPECT_EQ((*group)["pre_filter"]["max_routes"].asInt(), 0);
  EXPECT_EQ((*group)["pre_filter"]["warning_limit"].asInt(), 30000);
}

TEST_F(ConfigBgpPeerGroupTest, DeleteGroupAndCommit) {
  // Land a peer-group in the system config, then delete it through a second
  // commit and verify it is gone from bgpd's running config.
  discardSession();
  clearBgpSession();
  stagePeerGroup({kGroup, "remote-asn", "65000"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  ASSERT_NE(findGroup(readRunningBgpConfigViaRpc(), kGroup), nullptr)
      << "setup commit did not land the peer-group in bgpd's running config";

  clearBgpSession();
  auto result = runCli({"delete", "protocol", "bgp", "peer-group", kGroup});
  EXPECT_THAT(result.stdout, HasSubstr("Successfully deleted"));
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after delete commit; state="
      << bgpDaemonActiveState();
  EXPECT_EQ(findGroup(readRunningBgpConfigViaRpc(), kGroup), nullptr)
      << "deleted peer-group still present in bgpd's running config";
}
