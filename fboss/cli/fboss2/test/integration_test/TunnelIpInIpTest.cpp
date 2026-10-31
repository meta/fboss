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

#include <fmt/format.h>
#include <folly/ScopeGuard.h>
#include <folly/json/dynamic.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <chrono>
#include <optional>
#include <set>
#include <sstream>
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

  // Shared body of the dscp-mode and ecn-mode tests. Creates a `direction`
  // ("encap" or "decap") tunnel with `attr` ("dscp-mode" or "ecn-mode") set to
  // pipe (the agent defaults to uniform), checks the mode in the running
  // config, the switch state and the SAI tunnel object, then removes the
  // tunnel and the underlay VLAN it created.
  //
  // What SaiTunnelManager programs differs per mode and direction:
  // - dscp-mode goes to the direction's own attribute (EncapDscpMode or
  //   DecapDscpMode, PIPE_MODEL = 1); the other direction stays uniform (0).
  // - decap ecn-mode goes to DecapEcnMode (pipe -> COPY_FROM_OUTER = 1).
  // - encap ecn-mode is not programmed at all: the IP-in-IP SAI tunnel has no
  //   encap ECN attribute, so only DecapEcnMode is checked to stay STANDARD
  //   (0).
  void checkTunnelMode(const std::string& direction, const std::string& attr) {
    const bool encap = direction == "encap";
    const bool dscp = attr == "dscp-mode";
    const std::string cfgField = dscp ? "dscpMode" : "ecnMode";
    const int pipe = static_cast<int>(cfg::TunnelMode::PIPE);
    // Substrings of the new tunnel's 'show hw-object IPTUNNEL' line; an empty
    // one is not checked.
    std::string saiSet;
    std::string saiUnset;
    if (dscp) {
      saiSet = encap ? "EncapDscpMode: 1" : "DecapDscpMode: 1";
      saiUnset = encap ? "DecapDscpMode: 0" : "EncapDscpMode: 0";
    } else if (encap) {
      saiUnset = "DecapEcnMode: 0";
    } else {
      saiSet = "DecapEcnMode: 1";
    }

    int intfId = ensureUnderlayIntfId();
    const std::string dstIp = findIpv6OnIntf(intfId);
    const std::string tunnelId =
        fmt::format("integration-test-{}-{}", direction, dscp ? "dscp" : "ecn");
    std::vector<std::string> cmd{
        "config",
        "tunnel",
        "ip-in-ip",
        direction,
        tunnelId,
        "destination",
        dstIp,
        attr,
        "pipe"};
    if (encap) {
      cmd.insert(cmd.end(), {"source", "2001:db8:cafe::6"});
    } else {
      // ttl-mode pipe: see DeleteDecapTunnel.
      cmd.insert(cmd.end(), {"ttl-mode", "pipe"});
    }
    cmd.insert(cmd.end(), {"underlay-intf-id", std::to_string(intfId)});

    // Armed as a guard so a failed check still leaves the DUT as found; runs
    // explicitly in Step 3.
    auto cleanup = [&]() {
      deleteTunnelAndUnderlay(direction, tunnelId, intfId);
    };
    auto cleanupGuard = folly::makeGuard(cleanup);

    const auto saiBefore = saiTunnelObjects("TunnelSaiId(");
    XLOG(INFO) << "[Step 1] Creating " << direction << " tunnel " << tunnelId
               << " with " << attr << " pipe...";
    auto result = runCli(cmd);
    ASSERT_EQ(result.exitCode, 0) << "Create failed: " << result.stderr;
    commitConfig();
    trackTunnel(tunnelId, direction);

    XLOG(INFO) << "[Step 2] Verifying " << attr
               << " in config, state and SAI...";
    auto tunnel = waitForTunnelInRunningConfig(tunnelId, /*shouldExist=*/true);
    ASSERT_TRUE(tunnel.has_value())
        << "Tunnel '" << tunnelId << "' not found in running config";
    ASSERT_TRUE(tunnel->count(cfgField))
        << cfgField << " missing from running config";
    EXPECT_EQ((*tunnel)[cfgField].asInt(), pipe);

    // ipTunnelMaps: SwitchIdList -> tunnel id -> IpTunnelFields.
    auto tunnelMaps = getSwitchState("ipTunnelMaps");
    int found = 0;
    for (const auto& [switchIds, tunnels] : tunnelMaps.items()) {
      const auto* state = tunnels.get_ptr(tunnelId);
      if (state == nullptr) {
        continue;
      }
      ++found;
      const auto* mode = state->get_ptr(cfgField);
      ASSERT_NE(mode, nullptr) << cfgField << " missing from switch state";
      EXPECT_EQ(mode->asInt(), pipe);
    }
    EXPECT_GT(found, 0) << "tunnel '" << tunnelId << "' not in switch state";

    std::vector<std::string> added;
    for (const auto& line : saiTunnelObjects("TunnelSaiId(")) {
      if (!saiBefore.count(line)) {
        added.push_back(line);
      }
    }
    ASSERT_EQ(added.size(), 1) << "expected one new SAI tunnel object";
    XLOG(INFO) << "New SAI tunnel: " << added[0];
    if (!saiSet.empty()) {
      EXPECT_NE(added[0].find(saiSet), std::string::npos)
          << "SAI tunnel lacks '" << saiSet << "'";
    }
    if (!saiUnset.empty()) {
      EXPECT_NE(added[0].find(saiUnset), std::string::npos)
          << "SAI tunnel lacks '" << saiUnset << "'";
    }

    // Leave the DUT as found: drop the tunnel, then the underlay VLAN and its
    // interface that ensureUnderlayIntfId created.
    XLOG(INFO) << "[Step 3] Deleting tunnel and underlay VLAN...";
    cleanupGuard.dismiss();
    cleanup();
    EXPECT_EQ(saiTunnelObjects("TunnelSaiId("), saiBefore)
        << "SAI tunnel left after delete";
  }

  // The SAI tunnel objects whose 'show hw-object IPTUNNEL' line contains
  // `prefix` ("TunnelSaiId(" for tunnels, "TunnelTermSaiId(" for tunnel
  // terminations), one line per object. The JSON output maps each host to
  // that hw agent's text dump.
  std::set<std::string> saiTunnelObjects(const std::string& prefix) {
    std::set<std::string> lines;
    auto dump = runCliJson({"show", "hw-object", "IPTUNNEL"});
    for (const auto& [host, text] : dump.items()) {
      std::istringstream out(text.asString());
      std::string line;
      while (std::getline(out, line)) {
        if (line.find(prefix) != std::string::npos) {
          lines.insert(line);
        }
      }
    }
    return lines;
  }

  // Deletes the tunnel, then the underlay VLAN and its interface that
  // ensureUnderlayIntfId created for `intfId`, and checks both are gone. Safe
  // to call when either is already absent, so tests also arm it as a guard.
  void deleteTunnelAndUnderlay(
      const std::string& direction,
      const std::string& tunnelId,
      int intfId) {
    try {
      if (runCli({"delete", "tunnel", "ip-in-ip", direction, tunnelId})
              .exitCode == 0) {
        commitConfig();
      }
      auto config = getRunningConfig();
      for (const auto& intf : config["sw"]["interfaces"]) {
        const auto* id = intf.get_ptr("intfID");
        const auto* vlan = intf.get_ptr("vlanID");
        if (id != nullptr && vlan != nullptr && id->asInt() == intfId) {
          deleteVlanIfPresent(static_cast<int>(vlan->asInt()));
        }
      }
      EXPECT_FALSE(findTunnelInRunningConfig(tunnelId).has_value())
          << "Tunnel '" << tunnelId << "' still present after delete";
      auto after = getRunningConfig();
      for (const auto& intf : after["sw"]["interfaces"]) {
        const auto* id = intf.get_ptr("intfID");
        EXPECT_TRUE(id == nullptr || id->asInt() != intfId)
            << "underlay interface " << intfId << " left behind";
      }
    } catch (const std::exception& e) {
      ADD_FAILURE() << "cleanup failed: " << e.what();
    }
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

  // Shared body of the destination/source mask tests. Creates a `direction`
  // ("encap" or "decap") tunnel whose `attr` ("destination" or "source")
  // carries `mask /64`, then checks where the agent put the mask, and removes
  // the tunnel and the underlay VLAN it created.
  //
  // The CLI stores the prefix length as an IPv6 mask address in dstIpMask or
  // srcIpMask. For encap the agent copies both fields straight into switch
  // state; for decap it swaps them, because config dstIp is the termination
  // match and is kept as srcIp in switch state. The other mask must stay unset
  // in both places, so a mask written to the wrong field fails the test.
  void checkTunnelMask(const std::string& direction, const std::string& attr) {
    const bool encap = direction == "encap";
    const bool onDst = attr == "destination";
    const std::string cfgMask = onDst ? "dstIpMask" : "srcIpMask";
    const std::string cfgOther = onDst ? "srcIpMask" : "dstIpMask";
    // Decap swaps directions in switch state.
    const bool stateOnDst = encap == onDst;
    const std::string stateMask = stateOnDst ? "dstIpMask" : "srcIpMask";
    const std::string stateOther = stateOnDst ? "srcIpMask" : "dstIpMask";
    const std::string stateIp = stateOnDst ? "dstIp" : "srcIp";
    const std::string mask = "ffff:ffff:ffff:ffff::"; // /64

    int intfId = ensureUnderlayIntfId();
    const std::string dstIp = findIpv6OnIntf(intfId);
    const std::string srcIp = "2001:db8:cafe::5";
    const std::string tunnelId = fmt::format(
        "integration-test-{}-{}mask", direction, onDst ? "dst" : "src");
    const std::string& maskedIp = onDst ? dstIp : srcIp;

    // Drop the tunnel, then the underlay VLAN and its interface that
    // ensureUnderlayIntfId created. Armed as a guard so a failed check still
    // leaves the DUT as found; runs explicitly in Step 3.
    auto cleanup = [&]() {
      try {
        if (runCli({"delete", "tunnel", "ip-in-ip", direction, tunnelId})
                .exitCode == 0) {
          commitConfig();
        }
        auto config = getRunningConfig();
        for (const auto& intf : config["sw"]["interfaces"]) {
          const auto* id = intf.get_ptr("intfID");
          const auto* vlan = intf.get_ptr("vlanID");
          if (id != nullptr && vlan != nullptr && id->asInt() == intfId) {
            deleteVlanIfPresent(static_cast<int>(vlan->asInt()));
          }
        }
        EXPECT_FALSE(findTunnelInRunningConfig(tunnelId).has_value())
            << "Tunnel '" << tunnelId << "' still present after delete";
        auto after = getRunningConfig();
        for (const auto& intf : after["sw"]["interfaces"]) {
          const auto* id = intf.get_ptr("intfID");
          EXPECT_TRUE(id == nullptr || id->asInt() != intfId)
              << "underlay interface " << intfId << " left behind";
        }
      } catch (const std::exception& e) {
        ADD_FAILURE() << "cleanup failed: " << e.what();
      }
    };
    auto cleanupGuard = folly::makeGuard(cleanup);

    std::vector<std::string> cmd{
        "config",
        "tunnel",
        "ip-in-ip",
        direction,
        tunnelId,
        "destination",
        dstIp};
    if (onDst) {
      cmd.insert(cmd.end(), {"mask", "64"});
    }
    if (encap || !onDst) {
      cmd.insert(cmd.end(), {"source", srcIp});
    }
    if (!onDst) {
      cmd.insert(cmd.end(), {"mask", "64"});
    }
    if (!encap) {
      // ttl-mode pipe: see DeleteDecapTunnel.
      cmd.insert(cmd.end(), {"ttl-mode", "pipe"});
    }
    cmd.insert(cmd.end(), {"underlay-intf-id", std::to_string(intfId)});

    XLOG(INFO) << "[Step 1] Creating " << direction << " tunnel " << tunnelId
               << " with " << attr << " mask /64...";
    auto result = runCli(cmd);
    ASSERT_EQ(result.exitCode, 0) << "Create failed: " << result.stderr;
    commitConfig();
    trackTunnel(tunnelId, direction);

    XLOG(INFO) << "[Step 2] Verifying the mask in running config and state...";
    auto tunnel = waitForTunnelInRunningConfig(tunnelId, /*shouldExist=*/true);
    ASSERT_TRUE(tunnel.has_value())
        << "Tunnel '" << tunnelId << "' not found in running config";
    ASSERT_TRUE(tunnel->count(cfgMask))
        << cfgMask << " missing from running config";
    EXPECT_EQ((*tunnel)[cfgMask].asString(), mask);
    EXPECT_FALSE(tunnel->count(cfgOther))
        << cfgOther << " set without being configured";

    // ipTunnelMaps: SwitchIdList -> tunnel id -> IpTunnelFields.
    auto tunnelMaps = getSwitchState("ipTunnelMaps");
    int found = 0;
    for (const auto& [switchIds, tunnels] : tunnelMaps.items()) {
      const auto* state = tunnels.get_ptr(tunnelId);
      if (state == nullptr) {
        continue;
      }
      ++found;
      EXPECT_EQ((*state)[stateIp].asString(), maskedIp);
      const auto* stateMaskVal = state->get_ptr(stateMask);
      ASSERT_NE(stateMaskVal, nullptr)
          << stateMask << " missing from " << direction << " switch state";
      EXPECT_EQ(stateMaskVal->asString(), mask);
      EXPECT_EQ(state->get_ptr(stateOther), nullptr)
          << direction << " " << attr << " mask landed in switch state "
          << stateOther;
    }
    EXPECT_GT(found, 0) << "tunnel '" << tunnelId << "' not in switch state";

    XLOG(INFO) << "[Step 3] Deleting tunnel and underlay VLAN...";
    cleanupGuard.dismiss();
    cleanup();
  }

 private:
  // (tunnelId, direction) pairs to clean up in TearDown.
  std::vector<std::pair<std::string, std::string>> createdTunnels_;
};

// ---------------------------------------------------------------------------
// Positive: create a complete DECAP tunnel and verify every field landed in
// the agent's running config, including tunnelType == IP_IN_IP_DECAP.
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpTest, CreateDecapTunnelAndVerifyRunningConfig) {
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
// Positive: 'destination <ip> mask <len>' on a DECAP tunnel lands in the
// switch state's srcIpMask (decap swaps directions).
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpTest, DecapDestinationMask) {
  checkTunnelMask("decap", "destination");
}

// ---------------------------------------------------------------------------
// Positive: 'source <ip> mask <len>' on a DECAP tunnel lands in the switch
// state's dstIpMask (decap swaps directions).
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpTest, DecapSourceMask) {
  checkTunnelMask("decap", "source");
}

// ---------------------------------------------------------------------------
// Positive: create an ENCAP tunnel and verify tunnelType == IP_IN_IP_ENCAP in
// the agent's running config.
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpTest, CreateEncapTunnelAndVerifyRunningConfig) {
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
// Positive: 'destination <ip> mask <len>' on an ENCAP tunnel lands in the
// switch state's dstIpMask.
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpTest, EncapDestinationMask) {
  checkTunnelMask("encap", "destination");
}

// ---------------------------------------------------------------------------
// Positive: 'source <ip> mask <len>' on an ENCAP tunnel lands in the switch
// state's srcIpMask.
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpTest, EncapSourceMask) {
  checkTunnelMask("encap", "source");
}

// ---------------------------------------------------------------------------
// Positive: create then delete a decap tunnel; verify it disappears from the
// running config.
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpTest, DeleteDecapTunnel) {
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
// Positive: 'dscp-mode pipe' on a DECAP tunnel is programmed as the SAI
// tunnel's DecapDscpMode.
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpTest, DecapDscpMode) {
  checkTunnelMode("decap", "dscp-mode");
}

// ---------------------------------------------------------------------------
// Positive: 'ecn-mode pipe' on a DECAP tunnel is programmed as the SAI
// tunnel's DecapEcnMode (COPY_FROM_OUTER).
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpTest, DecapEcnMode) {
  checkTunnelMode("decap", "ecn-mode");
}

// ---------------------------------------------------------------------------
// Positive: 'termination-type p2p' on a DECAP tunnel (the agent defaults to
// p2mp) is programmed as a P2P SAI tunnel term, which also matches on the
// outer source address; a P2MP term has no source.
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpTest, DecapTerminationType) {
  // Fake SAI only implements P2MP tunnel terms and rejects a P2P one with
  // ATTR_NOT_SUPPORTED, which aborts the hw agent (fboss-sim runs fake SAI).
  const auto config = getRunningConfig();
  for (const auto& info :
       config["sw"]["switchSettings"]["switchIdToSwitchInfo"].values()) {
    switch (static_cast<cfg::AsicType>(info["asicType"].asInt())) {
      case cfg::AsicType::ASIC_TYPE_FAKE:
      case cfg::AsicType::ASIC_TYPE_FAKE_NO_WARMBOOT:
        GTEST_SKIP() << "fake SAI does not support P2P tunnel terms";
      default:
        break;
    }
  }

  int intfId = ensureUnderlayIntfId();
  const std::string dstIp = findIpv6OnIntf(intfId);
  const std::string srcIp = "2001:db8:cafe::7";
  const std::string tunnelId = "integration-test-decap-p2p";
  const int p2p = static_cast<int>(cfg::TunnelTerminationType::P2P);

  // Armed as a guard so a failed check still leaves the DUT as found; runs
  // explicitly in Step 3.
  auto cleanup = [&]() { deleteTunnelAndUnderlay("decap", tunnelId, intfId); };
  auto cleanupGuard = folly::makeGuard(cleanup);
  const auto termsBefore = saiTunnelObjects("TunnelTermSaiId(");

  // ttl-mode pipe: see DeleteDecapTunnel.
  XLOG(INFO) << "[Step 1] Creating decap tunnel " << tunnelId
             << " with termination-type p2p...";
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
       "termination-type",
       "p2p",
       "ttl-mode",
       "pipe",
       "underlay-intf-id",
       std::to_string(intfId)});
  ASSERT_EQ(result.exitCode, 0) << "Create failed: " << result.stderr;
  commitConfig();
  trackTunnel(tunnelId, "decap");

  XLOG(INFO) << "[Step 2] Verifying termination-type in config, state, SAI...";
  auto tunnel = waitForTunnelInRunningConfig(tunnelId, /*shouldExist=*/true);
  ASSERT_TRUE(tunnel.has_value())
      << "Tunnel '" << tunnelId << "' not found in running config";
  ASSERT_TRUE(tunnel->count("tunnelTermType"))
      << "tunnelTermType missing from running config";
  EXPECT_EQ((*tunnel)["tunnelTermType"].asInt(), p2p);

  // ipTunnelMaps: SwitchIdList -> tunnel id -> IpTunnelFields.
  auto tunnelMaps = getSwitchState("ipTunnelMaps");
  int found = 0;
  for (const auto& [switchIds, tunnels] : tunnelMaps.items()) {
    const auto* state = tunnels.get_ptr(tunnelId);
    if (state == nullptr) {
      continue;
    }
    ++found;
    const auto* termType = state->get_ptr("tunnelTermType");
    ASSERT_NE(termType, nullptr) << "tunnelTermType missing from switch state";
    EXPECT_EQ(termType->asInt(), p2p);
  }
  EXPECT_GT(found, 0) << "tunnel '" << tunnelId << "' not in switch state";

  // SAI_TUNNEL_TERM_TABLE_ENTRY_TYPE_P2P = 0 (P2MP = 1, and has no SrcIp).
  std::vector<std::string> added;
  for (const auto& line : saiTunnelObjects("TunnelTermSaiId(")) {
    if (!termsBefore.count(line)) {
      added.push_back(line);
    }
  }
  ASSERT_EQ(added.size(), 1) << "expected one new SAI tunnel term";
  XLOG(INFO) << "New SAI tunnel term: " << added[0];
  EXPECT_NE(added[0].find("(Type: 0,"), std::string::npos)
      << "SAI tunnel term is not P2P";
  EXPECT_NE(added[0].find(fmt::format("DstIp: {},", dstIp)), std::string::npos)
      << "SAI tunnel term does not match the destination";
  EXPECT_NE(added[0].find(fmt::format("SrcIp: {},", srcIp)), std::string::npos)
      << "SAI tunnel term does not match the outer source";

  XLOG(INFO) << "[Step 3] Deleting tunnel and underlay VLAN...";
  cleanupGuard.dismiss();
  cleanup();
  EXPECT_EQ(saiTunnelObjects("TunnelTermSaiId("), termsBefore)
      << "SAI tunnel term left after delete";
}

// ---------------------------------------------------------------------------
// Positive: reset an optional attr (ttl-mode) on a decap tunnel and confirm it
// is gone from the running config while destination survives.
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpTest, ResetOptionalAttrOnDecapTunnel) {
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
// Positive: 'dscp-mode pipe' on an ENCAP tunnel is programmed as the SAI
// tunnel's EncapDscpMode.
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpTest, EncapDscpMode) {
  checkTunnelMode("encap", "dscp-mode");
}

// ---------------------------------------------------------------------------
// Positive: 'ecn-mode pipe' on an ENCAP tunnel reaches the running config and
// switch state. The IP-in-IP SAI tunnel has no encap ECN attribute, so the
// hardware check is only that it does not leak into DecapEcnMode.
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpTest, EncapEcnMode) {
  checkTunnelMode("encap", "ecn-mode");
}

// ---------------------------------------------------------------------------
// Negative: termination-type is decap-only — the encap command must reject it.
// ---------------------------------------------------------------------------

TEST_F(TunnelIpInIpTest, EncapRejectsTerminationType) {
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

TEST_F(TunnelIpInIpTest, DecapRejectsUnsupportedTerminationType) {
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

TEST_F(TunnelIpInIpTest, DeleteWrongDirectionRejected) {
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

TEST_F(TunnelIpInIpTest, EncapWithoutSourceRejected) {
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

TEST_F(TunnelIpInIpTest, DeleteNonExistentIsIdempotent) {
  const std::string ghostId = "tunnel-that-does-not-exist-XYZ-12345";
  auto result = runCli({"delete", "tunnel", "ip-in-ip", "decap", ghostId});
  EXPECT_EQ(result.exitCode, 0)
      << "Idempotent delete of non-existent tunnel should succeed: "
      << result.stderr;
}
