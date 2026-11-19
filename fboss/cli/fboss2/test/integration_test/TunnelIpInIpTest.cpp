// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end test for the 'fboss2-dev config tunnel ip-in-ip {encap,decap}' and
 * 'fboss2-dev delete tunnel ip-in-ip {encap,decap}' commands.
 *
 * Positive tests create encap and decap tunnels, commit, and verify the result
 * in the agent's *running config* (fetched over thrift via getRunningConfig),
 * not just the staged session config — this proves the agent accepted and
 * applied the delta without crashing.
 *
 * Negative tests verify that the CLI rejects direction-invalid input
 * (termination-type on encap, deleting with the wrong direction) with a
 * non-zero exit code.
 *
 * Requirements:
 * - FBOSS agent must be running with a valid configuration
 */

#include <folly/json/dynamic.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;

class TunnelIpInIpTest : public Fboss2IntegrationTest {
 protected:
  void TearDown() override {
    // Stage all deletes first, then commit once: fewer failure points, and a
    // single failed commit can't leave later tunnels undeleted on the shared
    // DUT.
    bool anyDeleted = false;
    for (const auto& [id, direction] : createdTunnels_) {
      auto result = runCli({"delete", "tunnel", "ip-in-ip", direction, id});
      anyDeleted |= (result.exitCode == 0);
    }
    if (anyDeleted) {
      commitConfig();
    }
    createdTunnels_.clear();
    Fboss2IntegrationTest::TearDown();
  }

  // Return the tunnel object with the given id from the agent's running config,
  // or std::nullopt if absent.
  std::optional<folly::dynamic> findTunnelInRunningConfig(
      const std::string& id) const {
    auto config = getRunningConfig();
    if (!config.count("sw")) {
      return std::nullopt;
    }
    const auto& sw = config["sw"];
    if (!sw.count("ipInIpTunnels")) {
      return std::nullopt;
    }
    for (const auto& t : sw["ipInIpTunnels"]) {
      if (t.count("ipInIpTunnelId") && t["ipInIpTunnelId"].asString() == id) {
        return t;
      }
    }
    return std::nullopt;
  }

  // Poll the running config until the tunnel reaches the desired presence, to
  // absorb the agent's config-reload window after a commit.
  std::optional<folly::dynamic> waitForTunnelInRunningConfig(
      const std::string& id,
      bool shouldExist,
      std::chrono::seconds timeout = std::chrono::seconds(60)) const {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::optional<folly::dynamic> last;
    while (std::chrono::steady_clock::now() < deadline) {
      last = findTunnelInRunningConfig(id);
      if (last.has_value() == shouldExist) {
        return last;
      }
      std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    return last;
  }

  void trackTunnel(const std::string& id, const std::string& direction) {
    createdTunnels_.emplace_back(id, direction);
  }

 private:
  // (tunnelId, direction) pairs to clean up in TearDown.
  std::vector<std::pair<std::string, std::string>> createdTunnels_;
};

class TunnelIpInIpDecapTest : public TunnelIpInIpTest {
 protected:
  TunnelIpInIpDecapTest() {
    addProductionFeatures({ProductionFeature::IP_IN_IP_DECAP});
  }
};

// Declares no feature: these pass on ASICs asic_production_features does not
// list IP_IN_IP_ENCAP for (e.g. Tomahawk5).
class TunnelIpInIpEncapTest : public TunnelIpInIpTest {};

// Commands the CLI rejects, or treats as a no-op, before anything reaches the
// agent.
class TunnelIpInIpValidationTest : public TunnelIpInIpTest {};

// ---------------------------------------------------------------------------
// Positive: create a complete DECAP tunnel and verify every field landed in
// the agent's running config, including tunnelType == IP_IN_IP_DECAP.
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpDecapTest, CreateDecapTunnelAndVerifyRunningConfig) {
  int intfId = ensureUnderlayIntfId();
  const std::string dstIp = findIpv6OnIntf(intfId);
  const std::string srcIp = "2001:db8:cafe::1";
  const std::string tunnelId = "integration-test-decap";

  XLOG(INFO) << "[Step 1] Creating decap tunnel " << tunnelId
             << " (underlayIntf=" << intfId << " dst=" << dstIp << ")...";
  auto result = runCli(
      {"config",
       "tunnel",
       "ip-in-ip",
       "decap",
       tunnelId,
       "destination",
       dstIp,
       "source",
       srcIp,
       "ttl-mode",
       "pipe",
       "dscp-mode",
       "pipe",
       "ecn-mode",
       "pipe",
       "termination-type",
       "p2mp",
       "underlay-intf-id",
       std::to_string(intfId)});
  ASSERT_EQ(result.exitCode, 0) << "Create failed: " << result.stderr;
  commitConfig();
  trackTunnel(tunnelId, "decap");

  XLOG(INFO) << "[Step 2] Verifying tunnel in agent running config...";
  auto tunnel = waitForTunnelInRunningConfig(tunnelId, /*shouldExist=*/true);
  ASSERT_TRUE(tunnel.has_value())
      << "Tunnel '" << tunnelId << "' not found in running config";
  EXPECT_EQ((*tunnel)["dstIp"].asString(), dstIp);
  EXPECT_EQ((*tunnel)["srcIp"].asString(), srcIp);
  EXPECT_EQ((*tunnel)["underlayIntfID"].asInt(), intfId);
  // tunnelType enum serializes as its integer value: IP_IN_IP_DECAP == 0.
  ASSERT_TRUE(tunnel->count("tunnelType"));
  EXPECT_EQ(
      (*tunnel)["tunnelType"].asInt(),
      static_cast<int>(TunnelType::IP_IN_IP_DECAP));
  // tunnelTermType: P2MP == 2.
  ASSERT_TRUE(tunnel->count("tunnelTermType"));
  EXPECT_EQ(
      (*tunnel)["tunnelTermType"].asInt(),
      static_cast<int>(cfg::TunnelTerminationType::P2MP));
}

// ---------------------------------------------------------------------------
// Positive: create an ENCAP tunnel and verify tunnelType == IP_IN_IP_ENCAP in
// the agent's running config.
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpEncapTest, CreateEncapTunnelAndVerifyRunningConfig) {
  int intfId = ensureUnderlayIntfId();
  const std::string dstIp = findIpv6OnIntf(intfId);
  const std::string srcIp = "2001:db8:cafe::2";
  const std::string tunnelId = "integration-test-encap";

  XLOG(INFO) << "[Step 1] Creating encap tunnel " << tunnelId << "...";
  auto result = runCli(
      {"config",
       "tunnel",
       "ip-in-ip",
       "encap",
       tunnelId,
       "destination",
       dstIp,
       "source",
       srcIp,
       "ttl-mode",
       "uniform",
       "underlay-intf-id",
       std::to_string(intfId)});
  ASSERT_EQ(result.exitCode, 0) << "Create failed: " << result.stderr;
  commitConfig();
  trackTunnel(tunnelId, "encap");

  XLOG(INFO) << "[Step 2] Verifying encap tunnel in agent running config...";
  auto tunnel = waitForTunnelInRunningConfig(tunnelId, /*shouldExist=*/true);
  ASSERT_TRUE(tunnel.has_value())
      << "Tunnel '" << tunnelId << "' not found in running config";
  EXPECT_EQ((*tunnel)["dstIp"].asString(), dstIp);
  EXPECT_EQ((*tunnel)["srcIp"].asString(), srcIp);
  ASSERT_TRUE(tunnel->count("tunnelType"));
  EXPECT_EQ(
      (*tunnel)["tunnelType"].asInt(),
      static_cast<int>(TunnelType::IP_IN_IP_ENCAP));
}

// ---------------------------------------------------------------------------
// Positive: create then delete a decap tunnel; verify it disappears from the
// running config.
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpDecapTest, DeleteDecapTunnel) {
  int intfId = ensureUnderlayIntfId();
  const std::string dstIp = findIpv6OnIntf(intfId);
  const std::string tunnelId = "integration-test-decap-delete";

  auto result = runCli(
      {"config",
       "tunnel",
       "ip-in-ip",
       "decap",
       tunnelId,
       "destination",
       dstIp,
       "underlay-intf-id",
       std::to_string(intfId)});
  ASSERT_EQ(result.exitCode, 0) << "Create failed: " << result.stderr;
  commitConfig();
  trackTunnel(tunnelId, "decap");
  ASSERT_TRUE(waitForTunnelInRunningConfig(tunnelId, true).has_value());

  XLOG(INFO) << "[Step 2] Deleting tunnel...";
  result = runCli({"delete", "tunnel", "ip-in-ip", "decap", tunnelId});
  ASSERT_EQ(result.exitCode, 0) << "Delete failed: " << result.stderr;
  commitConfig();

  auto tunnel = waitForTunnelInRunningConfig(tunnelId, /*shouldExist=*/false);
  EXPECT_FALSE(tunnel.has_value())
      << "Tunnel '" << tunnelId << "' still present after delete";
}

// ---------------------------------------------------------------------------
// Positive: reset an optional attr (ttl-mode) on a decap tunnel and confirm it
// is gone from the running config while destination survives.
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpDecapTest, ResetOptionalAttrOnDecapTunnel) {
  int intfId = ensureUnderlayIntfId();
  const std::string dstIp = findIpv6OnIntf(intfId);
  const std::string tunnelId = "integration-test-decap-reset";

  auto result = runCli(
      {"config",
       "tunnel",
       "ip-in-ip",
       "decap",
       tunnelId,
       "destination",
       dstIp,
       "ttl-mode",
       "pipe",
       "underlay-intf-id",
       std::to_string(intfId)});
  ASSERT_EQ(result.exitCode, 0) << "Create failed: " << result.stderr;
  commitConfig();
  trackTunnel(tunnelId, "decap");
  auto created = waitForTunnelInRunningConfig(tunnelId, true);
  ASSERT_TRUE(created.has_value());
  // Guard against a vacuous post-reset check: ttlMode must actually be
  // serialized in the running config before we reset it.
  ASSERT_TRUE(created->count("ttlMode"))
      << "ttl-mode not present in running config after create";

  XLOG(INFO) << "[Step 2] Resetting ttl-mode...";
  result =
      runCli({"delete", "tunnel", "ip-in-ip", "decap", tunnelId, "ttl-mode"});
  ASSERT_EQ(result.exitCode, 0) << "Reset failed: " << result.stderr;
  commitConfig();

  // ttlMode should be gone; dstIp should remain.
  std::optional<folly::dynamic> tunnel;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (std::chrono::steady_clock::now() < deadline) {
    tunnel = findTunnelInRunningConfig(tunnelId);
    if (tunnel.has_value() && !tunnel->count("ttlMode")) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
  ASSERT_TRUE(tunnel.has_value());
  EXPECT_FALSE(tunnel->count("ttlMode"))
      << "ttl-mode should be unset after reset";
  EXPECT_EQ((*tunnel)["dstIp"].asString(), dstIp);
}

// ---------------------------------------------------------------------------
// Negative: termination-type is decap-only — the encap command must reject it.
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpValidationTest, EncapRejectsTerminationType) {
  int intfId = ensureUnderlayIntfId();
  const std::string dstIp = findIpv6OnIntf(intfId);
  const std::string tunnelId = "integration-test-encap-badattr";

  auto result = runCli(
      {"config",
       "tunnel",
       "ip-in-ip",
       "encap",
       tunnelId,
       "destination",
       dstIp,
       "termination-type",
       "p2p",
       "underlay-intf-id",
       std::to_string(intfId)});
  EXPECT_NE(result.exitCode, 0)
      << "Expected non-zero exit: termination-type is not valid for encap";
  discardSession();
}

// ---------------------------------------------------------------------------
// Negative: mp2p/mp2mp terminations are not implemented by the agent's
// SaiTunnelManager (it aborts on them at commit), so the CLI must reject them.
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpValidationTest, DecapRejectsUnsupportedTerminationType) {
  int intfId = ensureUnderlayIntfId();
  const std::string dstIp = findIpv6OnIntf(intfId);
  const std::string tunnelId = "integration-test-decap-mp2mp";

  auto result = runCli(
      {"config",
       "tunnel",
       "ip-in-ip",
       "decap",
       tunnelId,
       "destination",
       dstIp,
       "source",
       "2001:db8:cafe::4",
       "termination-type",
       "mp2mp",
       "underlay-intf-id",
       std::to_string(intfId)});
  EXPECT_NE(result.exitCode, 0)
      << "Expected non-zero exit: mp2mp termination is not agent-supported";
  discardSession();
}

// ---------------------------------------------------------------------------
// Negative: deleting an encap tunnel with the decap subcommand (wrong
// direction) must be rejected.
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpEncapTest, DeleteWrongDirectionRejected) {
  int intfId = ensureUnderlayIntfId();
  const std::string dstIp = findIpv6OnIntf(intfId);
  const std::string tunnelId = "integration-test-encap-wrongdir";

  auto result = runCli(
      {"config",
       "tunnel",
       "ip-in-ip",
       "encap",
       tunnelId,
       "destination",
       dstIp,
       "source",
       "2001:db8:cafe::3",
       "underlay-intf-id",
       std::to_string(intfId)});
  ASSERT_EQ(result.exitCode, 0) << "Create failed: " << result.stderr;
  commitConfig();
  trackTunnel(tunnelId, "encap");
  ASSERT_TRUE(waitForTunnelInRunningConfig(tunnelId, true).has_value());

  XLOG(INFO) << "[Test] Deleting encap tunnel via 'decap' (must fail)...";
  result = runCli({"delete", "tunnel", "ip-in-ip", "decap", tunnelId});
  EXPECT_NE(result.exitCode, 0)
      << "Expected non-zero exit deleting encap tunnel with decap subcommand";
  discardSession();
}

// ---------------------------------------------------------------------------
// Negative: an encap tunnel needs an outer source IP. Creating one without
// 'source' must be rejected by the CLI (no commit) — the agent's
// SaiTunnelManager would otherwise abort dereferencing the unset srcIp.
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpValidationTest, EncapWithoutSourceRejected) {
  int intfId = ensureUnderlayIntfId();
  const std::string dstIp = findIpv6OnIntf(intfId);
  const std::string tunnelId = "integration-test-encap-nosrc";

  auto result = runCli(
      {"config",
       "tunnel",
       "ip-in-ip",
       "encap",
       tunnelId,
       "destination",
       dstIp,
       "underlay-intf-id",
       std::to_string(intfId)});
  EXPECT_NE(result.exitCode, 0)
      << "Expected non-zero exit creating an encap tunnel without a source";
  discardSession();
}

// ---------------------------------------------------------------------------
// Positive: deleting a non-existent tunnel is idempotent (exit 0).
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpValidationTest, DeleteNonExistentIsIdempotent) {
  const std::string ghostId = "tunnel-that-does-not-exist-XYZ-12345";
  auto result = runCli({"delete", "tunnel", "ip-in-ip", "decap", ghostId});
  EXPECT_EQ(result.exitCode, 0)
      << "Idempotent delete of non-existent tunnel should succeed: "
      << result.stderr;
}
