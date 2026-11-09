// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end tests for `fboss2-dev config protocol bgp neighbor <ip>
 * [<attribute> <value> ...]`.
 *
 * Scope: the per-neighbor attributes. Every test stages the change AND
 * commits it, then asserts the value landed at the correct thrift field path
 * inside the matching .peers[] entry of the promoted system config
 * (/etc/coop/bgpcpp/bgpcpp.conf) and of bgpd's own running config. Stage-only
 * behavior (attribute parsing, validation, rejection) is covered by the unit
 * tests; an integration test that never commits exercises no daemon.
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
// Test-only neighbor addresses, from the documentation range (RFC 3849) so
// they never collide with a real peer in the device's running BGP config.
const std::string kNeighbor = "2001:db8::1234";
} // namespace

class ConfigBgpNeighborTest : public ConfigBgpTestBase {
 protected:
  // Stage `config protocol bgp neighbor <tokens...>` WITHOUT clearing the
  // staged session, so attributes can accumulate across invocations. Returns
  // the staged session JSON.
  folly::dynamic stageNeighbor(const std::vector<std::string>& tokens) {
    std::vector<std::string> args = {"config", "protocol", "bgp", "neighbor"};
    args.insert(args.end(), tokens.begin(), tokens.end());
    auto result = runCli(args);
    EXPECT_EQ(result.exitCode, 0)
        << "stdout=" << result.stdout << " stderr=" << result.stderr;
    EXPECT_THAT(result.stdout, Not(HasSubstr("Error:")));
    return readBgpSessionConfig();
  }

  // The .peers[] entry for addr, or nullptr.
  static const folly::dynamic* findPeer(
      const folly::dynamic& config,
      const std::string& addr) {
    if (config.count("peers") == 0) {
      return nullptr;
    }
    for (const auto& peer : config["peers"]) {
      if (peer.count("peer_addr") && peer["peer_addr"].asString() == addr) {
        return &peer;
      }
    }
    return nullptr;
  }
};

TEST_F(ConfigBgpNeighborTest, SetRemoteAsnAndCommit) {
  discardSession();
  clearBgpSession();
  stageNeighbor({kNeighbor, "remote-asn", "65000"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  // Two layers: the promoted file proves what the commit wrote; the daemon's
  // getRunningConfig RPC proves bgpd parsed and adopted it after the restart.
  auto config = readSystemBgpConfig();
  const auto* peer = findPeer(config, kNeighbor);
  ASSERT_NE(peer, nullptr) << "committed config has no peer " << kNeighbor;
  ASSERT_TRUE(peer->count("remote_as_4_byte"));
  EXPECT_EQ((*peer)["remote_as_4_byte"].asInt(), 65000);

  auto running = readRunningBgpConfigViaRpc();
  const auto* runningPeer = findPeer(running, kNeighbor);
  ASSERT_NE(runningPeer, nullptr)
      << "bgpd's running config has no peer " << kNeighbor;
  EXPECT_EQ((*runningPeer)["remote_as_4_byte"].asInt(), 65000);
}

TEST_F(ConfigBgpNeighborTest, SetTimersHoldTimeAndCommit) {
  discardSession();
  clearBgpSession();
  stageNeighbor({kNeighbor, "timers", "hold-time", "90"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  // Verify nested timer fields through the daemon's own view of its config.
  auto running = readRunningBgpConfigViaRpc();
  const auto* peer = findPeer(running, kNeighbor);
  ASSERT_NE(peer, nullptr) << "bgpd's running config has no peer " << kNeighbor;
  ASSERT_TRUE(peer->count("bgp_peer_timers"));
  EXPECT_EQ((*peer)["bgp_peer_timers"]["hold_time_seconds"].asInt(), 90);
}

TEST_F(ConfigBgpNeighborTest, GracefulRestartKeepsInheritedHoldTime) {
  // bgpd takes a peer's whole timers struct once any field of it is set, so
  // the first per-neighbor timer must carry the inherited hold time along
  // instead of the thrift default (0 = no keepalives on the session).
  discardSession();
  clearBgpSession();
  auto global =
      runCli({"config", "protocol", "bgp", "global", "hold-time", "180"});
  ASSERT_EQ(global.exitCode, 0) << global.stdout << global.stderr;
  stageNeighbor({kNeighbor, "graceful-restart", "restart-time", "60"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  auto running = readRunningBgpConfigViaRpc();
  const auto* peer = findPeer(running, kNeighbor);
  ASSERT_NE(peer, nullptr) << "bgpd's running config has no peer " << kNeighbor;
  ASSERT_TRUE(peer->count("bgp_peer_timers"));
  EXPECT_EQ((*peer)["bgp_peer_timers"]["hold_time_seconds"].asInt(), 180);
  EXPECT_EQ((*peer)["bgp_peer_timers"]["keep_alive_seconds"].asInt(), 60);
  EXPECT_EQ((*peer)["bgp_peer_timers"]["graceful_restart_seconds"].asInt(), 60);
}

TEST_F(ConfigBgpNeighborTest, SessionKnobsAndCommit) {
  // The per-session knobs bgpd resolves peer > group (remove-private-as,
  // ttl-security-hops) reach the daemon's running config via the same path.
  discardSession();
  clearBgpSession();
  stageNeighbor({kNeighbor, "remove-private-as", "true"});
  stageNeighbor({kNeighbor, "ttl-security-hops", "2"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  auto running = readRunningBgpConfigViaRpc();
  const auto* peer = findPeer(running, kNeighbor);
  ASSERT_NE(peer, nullptr) << "bgpd's running config has no peer " << kNeighbor;
  ASSERT_TRUE(peer->count("remove_private_as"));
  EXPECT_TRUE((*peer)["remove_private_as"].asBool());
  ASSERT_TRUE(peer->count("ttl_security_hops"));
  EXPECT_EQ((*peer)["ttl_security_hops"].asInt(), 2);
}

TEST_F(ConfigBgpNeighborTest, RouteLimitWarningKeepsUnlimitedCap) {
  // A warning threshold on a peer with no route limit above it must not
  // introduce the 12000-route thrift default: bgpd reads max_routes 0 as
  // unlimited, which is what the peer had before the edit.
  discardSession();
  clearBgpSession();
  stageNeighbor({kNeighbor, "max-route", "pre-warning-threshold", "30000"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  auto running = readRunningBgpConfigViaRpc();
  const auto* peer = findPeer(running, kNeighbor);
  ASSERT_NE(peer, nullptr) << "bgpd's running config has no peer " << kNeighbor;
  ASSERT_TRUE(peer->count("pre_filter"));
  EXPECT_EQ((*peer)["pre_filter"]["max_routes"].asInt(), 0);
  EXPECT_EQ((*peer)["pre_filter"]["warning_limit"].asInt(), 30000);
}

TEST_F(ConfigBgpNeighborTest, DeleteNeighborAndCommit) {
  // Land a neighbor in the system config, then delete it through a second
  // commit and verify it is gone from the promoted config.
  discardSession();
  clearBgpSession();
  stageNeighbor({kNeighbor, "remote-asn", "65000"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  ASSERT_NE(findPeer(readRunningBgpConfigViaRpc(), kNeighbor), nullptr)
      << "setup commit did not land the neighbor in bgpd's running config";

  clearBgpSession();
  auto result = runCli({"delete", "protocol", "bgp", "neighbor", kNeighbor});
  EXPECT_THAT(result.stdout, HasSubstr("Successfully deleted"));
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after delete commit; state="
      << bgpDaemonActiveState();
  EXPECT_EQ(findPeer(readRunningBgpConfigViaRpc(), kNeighbor), nullptr)
      << "deleted neighbor still present in bgpd's running config";
}
