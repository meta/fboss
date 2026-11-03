// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end test for:
 *   fboss2-dev config switch icmpv4-unavailable-src-addr <ipv4-address>
 *
 * icmpV4UnavailableSrcAddress is optional in the agent config: a device that
 * has never configured it has no entry, and the agent computes the RFC 7600
 * default (192.0.0.8) on the fly. Because the field may be absent, this test
 * never reads it before writing it (single-arg getSwConfigField throws on an
 * absent field). It sets a known value, verifies the round-trip, then sets the
 * value back to the well-known default — behaviorally identical to absence.
 * The change is HITLESS so no agent restart is needed between steps.
 */

#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <string>
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;

namespace {
// RFC7600 IANA dummy IPv4 address — agent default when field is absent
constexpr std::string_view kDefaultIcmpV4SrcAddr = "192.0.0.8";
} // namespace

class ConfigIcmpV4UnavailableSrcAddrTest : public Fboss2IntegrationTest {
 protected:
  // Safe to call only after the field has been set at least once this test:
  // single-arg getSwConfigField throws if the field is absent from config.
  std::string getIcmpV4SrcAddr() const {
    return getSwConfigField<std::string>("icmpV4UnavailableSrcAddress");
  }

  void setIcmpV4SrcAddr(const std::string& addr) {
    auto result =
        runCli({"config", "switch", "icmpv4-unavailable-src-addr", addr});
    ASSERT_EQ(result.exitCode, 0)
        << "icmpv4-unavailable-src-addr CLI failed: " << result.stderr;
    commitConfig();
  }
};

TEST_F(ConfigIcmpV4UnavailableSrcAddrTest, SetAndRestoreAddress) {
  // Write first, then read: the field is optional and a single-arg read throws
  // when it is absent, so we never inspect it before the initial set.
  const std::string testAddr = "10.0.0.1";

  XLOG(INFO) << "[Step 1] Setting address to " << testAddr << "...";
  setIcmpV4SrcAddr(testAddr);
  EXPECT_EQ(getIcmpV4SrcAddr(), testAddr);
  XLOG(INFO) << "  Verified: address is " << testAddr;

  // Restore to the well-known RFC 7600 default. Behaviorally identical to the
  // field being absent (the agent synthesizes 192.0.0.8 when unset), so there
  // is no true original to recover.
  const std::string defaultAddr{kDefaultIcmpV4SrcAddr};
  XLOG(INFO) << "[Step 2] Restoring address to default " << defaultAddr
             << "...";
  setIcmpV4SrcAddr(defaultAddr);
  EXPECT_EQ(getIcmpV4SrcAddr(), defaultAddr);

  XLOG(INFO) << "TEST PASSED";
}

TEST_F(ConfigIcmpV4UnavailableSrcAddrTest, SetThenDelete) {
  const std::string testAddr = "10.0.0.2";

  XLOG(INFO) << "[Step 1] Setting address to " << testAddr << "...";
  setIcmpV4SrcAddr(testAddr);
  EXPECT_EQ(getIcmpV4SrcAddr(), testAddr);

  XLOG(INFO) << "[Step 2] delete switch icmpv4-unavailable-src-addr";
  auto result = runCli({"delete", "switch", "icmpv4-unavailable-src-addr"});
  ASSERT_EQ(result.exitCode, 0)
      << "delete icmpv4-unavailable-src-addr CLI failed: " << result.stderr;
  commitConfig();

  // Deleting removes the field entirely, so the config ends in the true
  // pristine state (the agent synthesizes 192.0.0.8 when the field is absent).
  XLOG(INFO) << "[Step 3] Verifying the field is absent...";
  EXPECT_EQ(
      getSwConfigField<std::string>("icmpV4UnavailableSrcAddress", "<absent>"),
      "<absent>");

  XLOG(INFO) << "TEST PASSED";
}
