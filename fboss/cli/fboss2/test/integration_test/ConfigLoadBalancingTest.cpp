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
 *     the load balancer targeted by an enabled test
 */

#include <fmt/format.h>
#include <folly/ScopeGuard.h>
#include <folly/json/dynamic.h>
#include <folly/json/json.h>
#include <folly/logging/xlog.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <initializer_list>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrlAsyncClient.h"
#include "fboss/cli/fboss2/commands/config/load_balancing/CmdConfigLoadBalancing.h"
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"
#include "fboss/cli/fboss2/test/integration_test/LoadBalancingTestUtils.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace facebook::fboss;
using ::testing::HasSubstr;

namespace {

constexpr int loadBalancerIdValue(cfg::LoadBalancerID id) {
  return static_cast<int>(id);
}

template <typename Enum>
std::set<int> enumValues(std::initializer_list<Enum> values) {
  std::set<int> out;
  for (auto value : values) {
    out.insert(static_cast<int>(value));
  }
  return out;
}

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

  // Return the requested load balancer from the sw config, throwing if absent.
  folly::dynamic getLoadBalancer(cfg::LoadBalancerID id) const {
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
      if (lb.count("id") && lb["id"].asInt() == loadBalancerIdValue(id)) {
        return lb;
      }
    }
    throw std::runtime_error(
        fmt::format(
            "Running config has no {} load balancer", lbIdToString(id)));
  }

  cfg::HashingAlgorithm getAlgorithm(cfg::LoadBalancerID id) const {
    auto lb = getLoadBalancer(id);
    return static_cast<cfg::HashingAlgorithm>(lb["algorithm"].asInt());
  }

  std::optional<int> getSeed(cfg::LoadBalancerID id) const {
    auto lb = getLoadBalancer(id);
    if (!lb.count("seed")) {
      return std::nullopt;
    }
    return lb["seed"].asInt();
  }

  std::set<int> getFieldSet(cfg::LoadBalancerID id, const std::string& fieldKey)
      const {
    auto lb = getLoadBalancer(id);
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

  void testAlgorithm(cfg::LoadBalancerID id) {
    const auto area = lbIdToString(id);
    const auto original = getAlgorithm(id);
    const auto expected = original == cfg::HashingAlgorithm::CRC
        ? cfg::HashingAlgorithm::CRC16_CCITT
        : cfg::HashingAlgorithm::CRC;
    const auto algoToken = algorithmIntToToken(static_cast<int>(expected));
    XLOG(INFO) << "--- " << area << " hash-algorithm " << algoToken << " ---";

    auto stdout = runAndCommit(
        {"config", "load-balancing", area, "hash-algorithm", algoToken});
    EXPECT_THAT(stdout, HasSubstr("hash-algorithm"));
    EXPECT_EQ(getAlgorithm(id), expected);

    // Restore.
    const auto originalToken = algorithmIntToToken(static_cast<int>(original));
    runAndCommit(
        {"config", "load-balancing", area, "hash-algorithm", originalToken});
    EXPECT_EQ(getAlgorithm(id), original);
  }

  // Running-config load balancers with the given id.
  int countConfigLoadBalancers(cfg::LoadBalancerID id) const {
    auto config = getRunningConfig();
    int count = 0;
    for (const auto& lb : config["sw"]["loadBalancers"]) {
      if (lb.count("id") && lb["id"].asInt() == loadBalancerIdValue(id)) {
        ++count;
      }
    }
    return count;
  }

  // Switch-state load balancers stored under the requested id, one per switch.
  // loadBalancerMaps: SwitchIdList -> LoadBalancerID -> LoadBalancerFields.
  std::vector<folly::dynamic> getStateLoadBalancers(
      cfg::LoadBalancerID id) const {
    std::vector<folly::dynamic> out;
    auto maps = getSwitchState("loadBalancerMaps");
    for (const auto& [switchIds, lbs] : maps.items()) {
      for (const auto& [key, lb] : lbs.items()) {
        if (key.asInt() == loadBalancerIdValue(id)) {
          out.push_back(lb);
        }
      }
    }
    return out;
  }

  // Verify the command updates the existing load-balancer entry without
  // creating a duplicate, and that the committed seed appears in switch state
  // under the matching LoadBalancerID.
  void testSeed(cfg::LoadBalancerID id) {
    const auto area = lbIdToString(id);
    const auto original = getSeed(id);
    // Flip the low bit of the current seed, so the test value always differs
    // from whatever the DUT already uses.
    const int32_t newSeed = original.has_value() ? (*original ^ 1) : 1;
    XLOG(INFO) << "--- " << area << " hash-seed " << newSeed << " ---";

    ASSERT_EQ(countConfigLoadBalancers(id), 1)
        << "expected exactly one " << area << " load balancer";

    // Put the original seed back even if a check below fails.
    const std::string restoreValue =
        original.has_value() ? std::to_string(*original) : "default";
    auto restoreGuard = folly::makeGuard([&] {
      runAndCommit(
          {"config", "load-balancing", area, "hash-seed", restoreValue});
    });

    runAndCommit(
        {"config",
         "load-balancing",
         area,
         "hash-seed",
         std::to_string(newSeed)});

    EXPECT_EQ(countConfigLoadBalancers(id), 1)
        << "the command appended a load balancer instead of updating " << area;
    EXPECT_EQ(getSeed(id), newSeed);

    const auto stateLbs = getStateLoadBalancers(id);
    EXPECT_FALSE(stateLbs.empty())
        << "no " << area << " load balancer in switch state";
    for (const auto& lb : stateLbs) {
      EXPECT_EQ(lb["id"].asInt(), loadBalancerIdValue(id))
          << "loadBalancerMaps key does not match the entry's id";
      EXPECT_EQ(lb["seed"].asInt(), newSeed);
    }

    restoreGuard.dismiss();
    runAndCommit({"config", "load-balancing", area, "hash-seed", restoreValue});
    EXPECT_EQ(getSeed(id), original);
  }

  // Generic hash-fields-* test. Sets `newTokens`, verifies the running
  // config's field-set matches `expectedEnumValues`, then restores the
  // original comma-separated token list.
  void testFields(
      cfg::LoadBalancerID id,
      const std::string& attrName, // e.g. "hash-fields-ipv4"
      const std::string& fieldKey, // e.g. "ipv4Fields"
      const std::string& newTokens,
      const std::set<int>& expectedEnumValues,
      const std::string& restoreTokens) {
    const auto area = lbIdToString(id);
    XLOG(INFO) << "--- " << area << " " << attrName << " " << newTokens
               << " ---";
    auto originalFields = getFieldSet(id, fieldKey);

    runAndCommit({"config", "load-balancing", area, attrName, newTokens});
    EXPECT_EQ(getFieldSet(id, fieldKey), expectedEnumValues);

    runAndCommit({"config", "load-balancing", area, attrName, restoreTokens});
    EXPECT_EQ(getFieldSet(id, fieldKey), originalFields);
  }
};

// ============================================================
// ECMP
// ============================================================

TEST_F(ConfigLoadBalancingTest, EcmpHashAlgorithm) {
  testAlgorithm(cfg::LoadBalancerID::ECMP);
}

TEST_F(ConfigLoadBalancingTest, EcmpHashSeed) {
  testSeed(cfg::LoadBalancerID::ECMP);
}

TEST_F(ConfigLoadBalancingTest, EcmpHashFieldsIpv4) {
  testFields(
      cfg::LoadBalancerID::ECMP,
      "hash-fields-ipv4",
      "ipv4Fields",
      "src-ip",
      enumValues({cfg::IPv4Field::SOURCE_ADDRESS}),
      "src-ip,dst-ip");
}

TEST_F(ConfigLoadBalancingTest, EcmpHashFieldsIpv6) {
  testFields(
      cfg::LoadBalancerID::ECMP,
      "hash-fields-ipv6",
      "ipv6Fields",
      "src-ip,dst-ip,flow-label",
      enumValues(
          {cfg::IPv6Field::SOURCE_ADDRESS,
           cfg::IPv6Field::DESTINATION_ADDRESS,
           cfg::IPv6Field::FLOW_LABEL}),
      "src-ip,dst-ip");
}

TEST_F(ConfigLoadBalancingTest, EcmpHashFieldsTransport) {
  testFields(
      cfg::LoadBalancerID::ECMP,
      "hash-fields-transport",
      "transportFields",
      "src-port",
      enumValues({cfg::TransportField::SOURCE_PORT}),
      "src-port,dst-port");
}

TEST_F(ConfigLoadBalancingTest, EcmpHashFieldsMpls) {
  testFields(
      cfg::LoadBalancerID::ECMP,
      "hash-fields-mpls",
      "mplsFields",
      "top,second",
      enumValues({cfg::MPLSField::TOP_LABEL, cfg::MPLSField::SECOND_LABEL}),
      "none");
}

// ============================================================
// LAG
// ============================================================

// LAG tests disabled pending the UDF-group-ID fix in SaiSwitchManager:
// addOrUpdateLagLoadBalancer did not pass udfGroupIds to
// hashManager().getOrCreate(), so any LAG hash update on a DUT with UDF groups
// would silently drop them.
TEST_F(ConfigLoadBalancingTest, DISABLED_LagHashAlgorithm) {
  testAlgorithm(cfg::LoadBalancerID::AGGREGATE_PORT);
}

TEST_F(ConfigLoadBalancingTest, DISABLED_LagHashSeed) {
  testSeed(cfg::LoadBalancerID::AGGREGATE_PORT);
}

TEST_F(ConfigLoadBalancingTest, DISABLED_LagHashFieldsIpv4) {
  testFields(
      cfg::LoadBalancerID::AGGREGATE_PORT,
      "hash-fields-ipv4",
      "ipv4Fields",
      "dst-ip",
      enumValues({cfg::IPv4Field::DESTINATION_ADDRESS}),
      "src-ip,dst-ip");
}
