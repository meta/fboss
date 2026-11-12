// Copyright (c) Meta Platforms, Inc. and affiliates.

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

// Create a loopback that no bootstrap config ships, address it, verify the
// interface + backing VLAN shape, strip one address, then delete the whole
// loopback and verify interface and VLAN are both gone — restoring the DUT
// exactly. A stable index is used so a re-run after a crash converges
// (creation tolerates the interface already existing from an aborted run).
TEST_F(ConfigLoopbackInterfaceTest, CreateAddressAndDeleteLoopback) {
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

  XLOG(INFO) << "[Step 3] Removing the IPv4 address...";
  discardSession();
  auto delAddr =
      runCli({"delete", "interface", kCreatedLoopback, "ip-address", kV4Addr});
  ASSERT_EQ(delAddr.exitCode, 0) << delAddr.stderr;
  commitConfig();
  waitForAgentReady();
  {
    auto config = getRunningConfig();
    const auto* intf = findInterfaceById(config["sw"], kCreatedIntfId);
    ASSERT_NE(intf, nullptr);
    EXPECT_FALSE(hasAddress(*intf, kV4Addr)) << kV4Addr << " not removed";
    EXPECT_TRUE(hasAddress(*intf, kV6Addr)) << kV6Addr << " lost";
  }

  XLOG(INFO) << "[Step 4] Deleting " << kCreatedLoopback << "...";
  discardSession();
  auto del = runCli({"delete", "interface", kCreatedLoopback});
  ASSERT_EQ(del.exitCode, 0) << del.stderr;
  commitConfig();
  waitForAgentReady();

  XLOG(INFO) << "[Step 5] Verifying interface and VLAN are gone...";
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
