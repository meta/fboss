// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end test for loopback interface addressing:
 *   fboss2-dev config interface loopback<N> [ip-address <A.B.C.D/32>]
 *                                           [ipv6-address <A:B::C/128>]
 *   fboss2-dev delete interface loopback<N> ip-address/ipv6-address <addr>
 *   fboss2-dev delete interface loopback<N>
 *
 * Loopbacks follow the bootstrap-config convention: the virtual interface at
 * intfID == vlanID == 10 + N, backed by the same-numbered VLAN
 * ("fbossLoopback<N>"). The bootstrap config ships loopback0 (usually
 * unnamed); other indices are created by the CLI on first use.
 */

#include <folly/json/dynamic.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;

namespace {
// Test-owned host addresses, shaped like the production loopback addressing
// (IPv4 /32 + IPv6 /128 host routes). Stable across runs so a re-run after a
// crash converges instead of leaking new state.
constexpr auto kV4Addr = "10.254.113.31/32";
constexpr auto kV6Addr = "fc00:0:0:31::/128";
// Index 9 (intfID/vlanID 19) is not shipped by any bootstrap config, so this
// exercises CLI-side creation.
constexpr auto kCreatedLoopback = "loopback9";
constexpr int kCreatedIntfId = 19;

const folly::dynamic* findInterfaceById(const folly::dynamic& sw, int intfId) {
  if (!sw.count("interfaces")) {
    return nullptr;
  }
  for (const auto& intf : sw["interfaces"]) {
    if (intf.isObject() && intf.count("intfID") &&
        intf["intfID"].asInt() == intfId) {
      return &intf;
    }
  }
  return nullptr;
}

bool hasAddress(const folly::dynamic& intf, const std::string& addr) {
  if (!intf.count("ipAddresses")) {
    return false;
  }
  for (const auto& a : intf["ipAddresses"]) {
    if (a.asString() == addr) {
      return true;
    }
  }
  return false;
}
} // namespace

class ConfigLoopbackInterfaceTest : public Fboss2IntegrationTest {};

// Add v4 + v6 host addresses to the bootstrap loopback0, verify they land on
// the virtual interface, then remove them again — restoring the DUT exactly.
TEST_F(ConfigLoopbackInterfaceTest, AddressRoundTripOnLoopback0) {
  XLOG(INFO) << "[Step 1] Adding addresses to loopback0...";
  auto add = runCli(
      {"config",
       "interface",
       "loopback0",
       "ip-address",
       kV4Addr,
       "ipv6-address",
       kV6Addr});
  ASSERT_EQ(add.exitCode, 0) << add.stderr;
  commitConfig();
  waitForAgentReady();

  XLOG(INFO) << "[Step 2] Verifying addresses in running config...";
  {
    auto config = getRunningConfig();
    const auto& sw = config["sw"];
    // The bootstrap loopback0 lives at the conventional intfID 10.
    const auto* intf = findInterfaceById(sw, 10);
    ASSERT_NE(intf, nullptr) << "no loopback0 interface (intfID 10)";
    EXPECT_TRUE((*intf)["isVirtual"].asBool());
    EXPECT_TRUE(hasAddress(*intf, kV4Addr)) << kV4Addr << " missing";
    EXPECT_TRUE(hasAddress(*intf, kV6Addr)) << kV6Addr << " missing";
  }

  XLOG(INFO) << "[Step 3] Removing the addresses again...";
  discardSession();
  ASSERT_EQ(
      runCli({"delete", "interface", "loopback0", "ip-address", kV4Addr})
          .exitCode,
      0);
  ASSERT_EQ(
      runCli({"delete", "interface", "loopback0", "ipv6-address", kV6Addr})
          .exitCode,
      0);
  commitConfig();
  waitForAgentReady();

  XLOG(INFO) << "[Step 4] Verifying restore...";
  {
    auto config = getRunningConfig();
    const auto* intf = findInterfaceById(config["sw"], 10);
    ASSERT_NE(intf, nullptr);
    EXPECT_FALSE(hasAddress(*intf, kV4Addr)) << kV4Addr << " not removed";
    EXPECT_FALSE(hasAddress(*intf, kV6Addr)) << kV6Addr << " not removed";
  }
  XLOG(INFO) << "TEST PASSED";
}

// Create a loopback that no bootstrap config ships, address it, verify the
// interface + backing VLAN shape, then delete the whole loopback and verify
// both are gone — restoring the DUT exactly. A stable index is used so a
// re-run after a crash converges (creation tolerates the interface already
// existing from an aborted prior run).
TEST_F(ConfigLoopbackInterfaceTest, CreateLoopbackWithAddresses) {
  XLOG(INFO) << "[Step 1] Creating " << kCreatedLoopback << " with addresses";
  auto create = runCli(
      {"config",
       "interface",
       kCreatedLoopback,
       "ip-address",
       kV4Addr,
       "ipv6-address",
       kV6Addr});
  ASSERT_EQ(create.exitCode, 0) << create.stderr;
  commitConfig();
  waitForAgentReady();

  XLOG(INFO) << "[Step 2] Verifying interface and VLAN shape...";
  {
    auto config = getRunningConfig();
    const auto& sw = config["sw"];
    const auto* intf = findInterfaceById(sw, kCreatedIntfId);
    ASSERT_NE(intf, nullptr)
        << kCreatedLoopback << " (intfID " << kCreatedIntfId << ") missing";
    EXPECT_TRUE((*intf)["isVirtual"].asBool());
    EXPECT_EQ((*intf)["vlanID"].asInt(), kCreatedIntfId);
    EXPECT_TRUE(hasAddress(*intf, kV4Addr));
    EXPECT_TRUE(hasAddress(*intf, kV6Addr));

    bool sawVlan = false;
    for (const auto& vlan : sw["vlans"]) {
      if (vlan["id"].asInt() == kCreatedIntfId) {
        sawVlan = true;
        EXPECT_TRUE(vlan["routable"].asBool());
        break;
      }
    }
    EXPECT_TRUE(sawVlan) << "backing VLAN " << kCreatedIntfId << " missing";
  }

  XLOG(INFO) << "[Step 3] Deleting " << kCreatedLoopback << "...";
  discardSession();
  auto del = runCli({"delete", "interface", kCreatedLoopback});
  ASSERT_EQ(del.exitCode, 0) << del.stderr;
  commitConfig();
  waitForAgentReady();

  XLOG(INFO) << "[Step 4] Verifying interface and VLAN are gone...";
  {
    auto config = getRunningConfig();
    const auto& sw = config["sw"];
    EXPECT_EQ(findInterfaceById(sw, kCreatedIntfId), nullptr)
        << kCreatedLoopback << " still present";
    for (const auto& vlan : sw["vlans"]) {
      EXPECT_NE(vlan["id"].asInt(), kCreatedIntfId)
          << "backing VLAN " << kCreatedIntfId << " still present";
    }
  }
  XLOG(INFO) << "TEST PASSED";
}
