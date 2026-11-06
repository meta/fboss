// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end tests for:
 *   - `fboss2-dev config load-balancing ecmp <attr> <value>`
 *   - `fboss2-dev config load-balancing lag <attr> <value>`
 *
 * For each attribute, the test:
 *   1. Reads the current load-balancer state from the running config
 *   2. Sets a new value via the CLI
 *   3. Commits the session (HITLESS — no agent restart)
 *   4. Verifies the running config reflects the new value
 *   5. Restores the original value
 *
 * Requirements:
 *   - FBOSS agent is running with a valid configuration that already contains
 *     ECMP and AGGREGATE_PORT load balancer entries (true for every real DUT)
 */

#include <fmt/format.h>
#include <folly/json/dynamic.h>
#include <folly/json/json.h>
#include <folly/logging/xlog.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "fboss/agent/if/gen-cpp2/FbossCtrlAsyncClient.h"
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"
#include "fboss/cli/fboss2/test/integration_test/LoadBalancingTestUtils.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace facebook::fboss;
using ::testing::HasSubstr;

namespace {

constexpr int kEcmpIdValue = 1;
constexpr int kLagIdValue = 2;

} // namespace

class ConfigLoadBalancingTest : public Fboss2IntegrationTest {
 protected:
  folly::dynamic getRunningConfig() const {
    HostInfo hostInfo("localhost");
    auto client =
        utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
    std::string configStr;
    client->sync_getRunningConfig(configStr);
    return folly::parseJson(configStr);
  }

  // Return the load balancer object for the given numeric LoadBalancerID
  // (1=ECMP, 2=AGGREGATE_PORT) from the sw config, throwing if not present.
  folly::dynamic getLoadBalancer(int idValue) const {
    auto config = getRunningConfig();
    if (!config.isObject() || !config.count("sw")) {
      throw std::runtime_error("Running config missing 'sw' object");
    }
    const auto& sw = config["sw"];
    if (!sw.count("loadBalancers") || !sw["loadBalancers"].isArray()) {
      throw std::runtime_error(
          "Running config missing 'sw.loadBalancers' array");
    }
    for (const auto& lb : sw["loadBalancers"]) {
      if (lb.count("id") && lb["id"].asInt() == idValue) {
        return lb;
      }
    }
    throw std::runtime_error(
        fmt::format("Running config has no load balancer with id={}", idValue));
  }

  int getAlgorithm(int idValue) const {
    auto lb = getLoadBalancer(idValue);
    return lb["algorithm"].asInt();
  }

  std::optional<int> getSeed(int idValue) const {
    auto lb = getLoadBalancer(idValue);
    if (!lb.count("seed")) {
      return std::nullopt;
    }
    return lb["seed"].asInt();
  }

  std::set<int> getFieldSet(int idValue, const std::string& fieldKey) const {
    auto lb = getLoadBalancer(idValue);
    std::set<int> out;
    if (!lb.count("fieldSelection")) {
      return out;
    }
    const auto& fs = lb["fieldSelection"];
    if (!fs.count(fieldKey) || !fs[fieldKey].isArray()) {
      return out;
    }
    for (const auto& v : fs[fieldKey]) {
      out.insert(v.asInt());
    }
    return out;
  }

  // Run the CLI and commit. Returns CLI stdout for assertion on log text.
  std::string runAndCommit(const std::vector<std::string>& args) {
    auto result = runCli(args);
    if (result.exitCode != 0) {
      ADD_FAILURE() << "runCli failed: stdout=" << result.stdout
                    << " stderr=" << result.stderr;
      return result.stdout;
    }
    commitConfig();
    return result.stdout;
  }

  // Drive `config load-balancing <area> hash-algorithm <algoToken>`, verify
  // the running config's algorithm int matches `expectedEnumValue`, then
  // restore the original algorithm.
  void testAlgorithm(
      const std::string& area,
      int idValue,
      const std::string& algoToken,
      int expectedEnumValue) {
    XLOG(INFO) << "--- " << area << " hash-algorithm " << algoToken << " ---";
    int original = getAlgorithm(idValue);
    ASSERT_NE(original, expectedEnumValue)
        << "Test value matches original — pick a different one";

    auto stdout = runAndCommit(
        {"config", "load-balancing", area, "hash-algorithm", algoToken});
    EXPECT_THAT(stdout, HasSubstr("hash-algorithm"));
    EXPECT_EQ(getAlgorithm(idValue), expectedEnumValue);

    // Restore.
    std::string originalToken = algorithmIntToToken(original);
    runAndCommit(
        {"config", "load-balancing", area, "hash-algorithm", originalToken});
    EXPECT_EQ(getAlgorithm(idValue), original);
  }

  // Drive `config load-balancing <area> hash-seed <seed>`, verify running
  // config matches, then restore. Uses the *numeric* seed verification
  // unconditionally; the restore path handles both "had a seed" and "seed
  // unset" original states.
  void testSeed(const std::string& area, int idValue, int newSeed) {
    XLOG(INFO) << "--- " << area << " hash-seed " << newSeed << " ---";
    auto original = getSeed(idValue);
    ASSERT_TRUE(!original.has_value() || *original != newSeed);

    runAndCommit(
        {"config",
         "load-balancing",
         area,
         "hash-seed",
         std::to_string(newSeed)});
    auto observed = getSeed(idValue);
    ASSERT_TRUE(observed.has_value());
    EXPECT_EQ(*observed, newSeed);

    // Restore: either original had a value or was unset (use "default").
    if (original.has_value()) {
      runAndCommit(
          {"config",
           "load-balancing",
           area,
           "hash-seed",
           std::to_string(*original)});
      auto restored = getSeed(idValue);
      ASSERT_TRUE(restored.has_value());
      EXPECT_EQ(*restored, *original);
    } else {
      runAndCommit({"config", "load-balancing", area, "hash-seed", "default"});
      EXPECT_FALSE(getSeed(idValue).has_value());
    }
  }

  // Generic hash-fields-* test. Sets `newTokens`, verifies the running
  // config's field-set matches `expectedEnumValues`, then restores the
  // original comma-separated token list.
  void testFields(
      const std::string& area,
      int idValue,
      const std::string& attrName, // e.g. "hash-fields-ipv4"
      const std::string& fieldKey, // e.g. "ipv4Fields"
      const std::string& newTokens,
      const std::set<int>& expectedEnumValues,
      const std::string& restoreTokens) {
    XLOG(INFO) << "--- " << area << " " << attrName << " " << newTokens
               << " ---";
    auto originalFields = getFieldSet(idValue, fieldKey);

    runAndCommit({"config", "load-balancing", area, attrName, newTokens});
    EXPECT_EQ(getFieldSet(idValue, fieldKey), expectedEnumValues);

    runAndCommit({"config", "load-balancing", area, attrName, restoreTokens});
    EXPECT_EQ(getFieldSet(idValue, fieldKey), originalFields);
  }
};

// ============================================================
// ECMP (LoadBalancerID = 1)
// ============================================================

TEST_F(ConfigLoadBalancingTest, EcmpHashAlgorithm) {
  testAlgorithm("ecmp", kEcmpIdValue, "crc32-hi", 4);
}

TEST_F(ConfigLoadBalancingTest, EcmpHashSeed) {
  testSeed("ecmp", kEcmpIdValue, 314159);
}

TEST_F(ConfigLoadBalancingTest, EcmpHashFieldsIpv4) {
  testFields(
      "ecmp",
      kEcmpIdValue,
      "hash-fields-ipv4",
      "ipv4Fields",
      "src-ip",
      {1 /* SOURCE_ADDRESS */},
      "src-ip,dst-ip");
}

TEST_F(ConfigLoadBalancingTest, EcmpHashFieldsIpv6) {
  testFields(
      "ecmp",
      kEcmpIdValue,
      "hash-fields-ipv6",
      "ipv6Fields",
      "src-ip,dst-ip,flow-label",
      {1, 2, 3},
      "src-ip,dst-ip");
}

TEST_F(ConfigLoadBalancingTest, EcmpHashFieldsTransport) {
  testFields(
      "ecmp",
      kEcmpIdValue,
      "hash-fields-transport",
      "transportFields",
      "src-port",
      {1 /* SOURCE_PORT */},
      "src-port,dst-port");
}

TEST_F(ConfigLoadBalancingTest, EcmpHashFieldsMpls) {
  testFields(
      "ecmp",
      kEcmpIdValue,
      "hash-fields-mpls",
      "mplsFields",
      "top,second",
      {1, 2},
      "none");
}

// ============================================================
// LAG (LoadBalancerID = 2 / AGGREGATE_PORT)
// ============================================================

// LAG tests disabled pending the UDF-group-ID fix in SaiSwitchManager:
// addOrUpdateLagLoadBalancer did not pass udfGroupIds to
// hashManager().getOrCreate(), so any LAG hash update on a DUT with UDF groups
// would silently drop them.
TEST_F(ConfigLoadBalancingTest, DISABLED_LagHashAlgorithm) {
  testAlgorithm("lag", kLagIdValue, "crc32-lo", 3);
}

TEST_F(ConfigLoadBalancingTest, DISABLED_LagHashSeed) {
  testSeed("lag", kLagIdValue, 271828);
}

TEST_F(ConfigLoadBalancingTest, DISABLED_LagHashFieldsIpv4) {
  testFields(
      "lag",
      kLagIdValue,
      "hash-fields-ipv4",
      "ipv4Fields",
      "dst-ip",
      {2 /* DESTINATION_ADDRESS */},
      "src-ip,dst-ip");
}
