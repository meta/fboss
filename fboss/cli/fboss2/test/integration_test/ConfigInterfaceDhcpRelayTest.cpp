// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end tests for 'fboss2-dev config interface <name> dhcp relay ...'
 * and 'fboss2-dev delete interface <name> dhcp relay ...'
 *
 * These tests:
 *  1. Pick an interface from the running system
 *  2. Set the DHCP relay destination via the CLI
 *  3. Verify the config landed in the agent's running config (fetched via
 *     thrift getRunningConfig()), using wait-retry polling so the
 *     verification is robust to commit-triggered agent restarts.
 *
 * Requirements:
 *  - FBOSS agent must be running with a valid configuration
 */

#include <fmt/format.h>
#include <folly/dynamic.h>
#include <folly/json/dynamic.h>
#include <folly/json/json.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <chrono>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;

class ConfigInterfaceDhcpRelayTest : public Fboss2IntegrationTest {
 protected:
  // Run `config interface <name> dhcp relay <attr> <address>` and commit.
  void setRelay(
      const std::string& interfaceName,
      const std::string& attr,
      const std::string& address) {
    touched_.emplace_back(interfaceName, attr);
    auto result = runCli(
        {"config", "interface", interfaceName, "dhcp", "relay", attr, address});
    ASSERT_EQ(result.exitCode, 0)
        << "Failed to set dhcp relay " << attr << ": " << result.stderr;
    commitConfig();
  }

  // Clear every relay attribute this test set and commit once, so the
  // shared DUT is left clean even when a test fails midway.
  void TearDown() override {
    if (!touched_.empty()) {
      bool reset = false;
      for (const auto& [interfaceName, attr] : touched_) {
        auto result = runCli(
            {"delete", "interface", interfaceName, "dhcp", "relay", attr});
        if (result.exitCode != 0) {
          XLOG(WARN) << "TearDown failed to clear dhcp relay " << attr << " on "
                     << interfaceName << ": " << result.stderr;
        } else {
          reset = true;
        }
      }
      if (reset) {
        commitConfig();
      }
    }
    Fboss2IntegrationTest::TearDown();
  }

  // Return the interface config object with the given intfID from a running
  // config, or an empty object when absent.
  static folly::dynamic getInterfaceConfig(
      const folly::dynamic& cfg,
      int intfID) {
    if (cfg.count("sw") && cfg["sw"].count("interfaces")) {
      for (const auto& iface : cfg["sw"]["interfaces"]) {
        if (iface.count("intfID") && iface["intfID"].asInt() == intfID) {
          return iface;
        }
      }
    }
    return folly::dynamic::object();
  }

  // Poll the agent's running config until the relay field for the L3
  // interface with the given intfID satisfies `predicate`.
  void expectRelayField(
      int intfID,
      const std::string& fieldName,
      const std::function<bool(const folly::dynamic*)>& predicate,
      const std::string& description) {
    auto cfg = waitForRunningConfig(
        [&](const folly::dynamic& c) {
          auto iface = getInterfaceConfig(c, intfID);
          const folly::dynamic* v =
              iface.count(fieldName) ? &iface[fieldName] : nullptr;
          return predicate(v);
        },
        std::chrono::seconds(30));
    auto iface = getInterfaceConfig(cfg, intfID);
    const folly::dynamic* v =
        iface.count(fieldName) ? &iface[fieldName] : nullptr;
    EXPECT_TRUE(predicate(v))
        << "intfID " << intfID << " " << fieldName << " = "
        << (v ? folly::toJson(*v) : "<unset>") << ", expected " << description;
  }

 private:
  // (interfaceName, attr) pairs set by this test, cleared in TearDown
  std::vector<std::pair<std::string, std::string>> touched_;
};

// ---------------------------------------------------------------------------
// Test: set the IPv4 relay destination and verify via running config
// ---------------------------------------------------------------------------

TEST_F(ConfigInterfaceDhcpRelayTest, SetAndVerifyIpv4Relay) {
  XLOG(INFO) << "[Step 1] Finding an interface to test...";
  std::string ifName = getRandomInterfacePortName();
  XLOG(INFO) << "  Using interface: " << ifName;

  const std::string relayAddr = "10.127.255.67";

  XLOG(INFO) << "[Step 2] Setting dhcp relay ip-address to " << relayAddr;
  setRelay(ifName, "ip-address", relayAddr);

  XLOG(INFO) << "[Step 3] Verifying running config via thrift...";
  expectRelayField(
      getInterfaceIdForPort(ifName),
      "dhcpRelayAddressV4",
      [&](const folly::dynamic* v) { return v && v->asString() == relayAddr; },
      fmt::format("== {}", relayAddr));
  XLOG(INFO) << "TEST PASSED";
}

// ---------------------------------------------------------------------------
// Test: set the IPv6 relay destination and verify via running config
// ---------------------------------------------------------------------------

TEST_F(ConfigInterfaceDhcpRelayTest, SetAndVerifyIpv6Relay) {
  XLOG(INFO) << "[Step 1] Finding an interface to test...";
  std::string ifName = getRandomInterfacePortName();
  XLOG(INFO) << "  Using interface: " << ifName;

  const std::string relayAddr = "2401:db00:eef0:a67::";

  XLOG(INFO) << "[Step 2] Setting dhcp relay ipv6-address to " << relayAddr;
  setRelay(ifName, "ipv6-address", relayAddr);

  XLOG(INFO) << "[Step 3] Verifying running config via thrift...";
  expectRelayField(
      getInterfaceIdForPort(ifName),
      "dhcpRelayAddressV6",
      [&](const folly::dynamic* v) { return v && v->asString() == relayAddr; },
      fmt::format("== {}", relayAddr));
  XLOG(INFO) << "TEST PASSED";
}

// ---------------------------------------------------------------------------
// Test: set both, then delete each and verify they are gone
// ---------------------------------------------------------------------------

TEST_F(ConfigInterfaceDhcpRelayTest, SetAndDeleteRelay) {
  XLOG(INFO) << "[Step 1] Finding an interface to test...";
  std::string ifName = getRandomInterfacePortName();
  XLOG(INFO) << "  Using interface: " << ifName;
  int intfID = getInterfaceIdForPort(ifName);

  const std::string v4Addr = "10.127.255.67";
  const std::string v6Addr = "2401:db00:eef0:a67::";

  XLOG(INFO) << "[Step 2] Setting both relay destinations...";
  setRelay(ifName, "ip-address", v4Addr);
  setRelay(ifName, "ipv6-address", v6Addr);

  expectRelayField(
      intfID,
      "dhcpRelayAddressV4",
      [&](const folly::dynamic* v) { return v && v->asString() == v4Addr; },
      fmt::format("== {}", v4Addr));
  expectRelayField(
      intfID,
      "dhcpRelayAddressV6",
      [&](const folly::dynamic* v) { return v && v->asString() == v6Addr; },
      fmt::format("== {}", v6Addr));

  XLOG(INFO) << "[Step 3] Deleting the IPv4 relay (with matching address)...";
  auto result = runCli(
      {"delete", "interface", ifName, "dhcp", "relay", "ip-address", v4Addr});
  ASSERT_EQ(result.exitCode, 0) << result.stderr;
  commitConfig();

  expectRelayField(
      intfID,
      "dhcpRelayAddressV4",
      [](const folly::dynamic* v) { return v == nullptr; },
      "<unset>");

  XLOG(INFO) << "[Step 4] Deleting the IPv6 relay...";
  result =
      runCli({"delete", "interface", ifName, "dhcp", "relay", "ipv6-address"});
  ASSERT_EQ(result.exitCode, 0) << result.stderr;
  commitConfig();

  expectRelayField(
      intfID,
      "dhcpRelayAddressV6",
      [](const folly::dynamic* v) { return v == nullptr; },
      "<unset>");
  XLOG(INFO) << "TEST PASSED";
}

// Note: the mismatched-address refusal on delete is CLI-side validation
// with no agent interaction; it is covered by unit tests
// (CmdDeleteInterfaceDhcpRelayTest queryClientMismatchedAddress).
