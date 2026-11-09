// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end test for `fboss2-dev delete load-balancing ecmp`:
 *   capture the running ECMP load-balancer → delete → check gone →
 *   restore via `config load-balancing ecmp` for DUT hygiene.
 *
 * The restore step is not re-verified per-field — the config-side round-trip
 * is ConfigLoadBalancingTest's coverage; this test only checks the entry is
 * present again. The lag subcommand shares the removal implementation and is
 * covered by CmdDeleteLoadBalancing unit tests;
 *
 * Requirements:
 *   - FBOSS agent is running with a valid configuration that already contains
 *     an ECMP load balancer entry (true for every real DUT)
 */

#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <cstddef>
#include <map>
#include <string>
#include <vector>
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"
#include "fboss/cli/fboss2/test/integration_test/LoadBalancingTestUtils.h"
#include "folly/json/dynamic.h"

using namespace facebook::fboss;
using ::testing::AssertionFailure;
using ::testing::AssertionResult;
using ::testing::AssertionSuccess;

namespace {

constexpr int kEcmpIdValue = 1;

} // namespace

class DeleteLoadBalancingTest : public Fboss2IntegrationTest {
 protected:
  // Returns the load balancer entry with the given id, or nullptr.
  static const folly::dynamic* findLoadBalancer(
      const folly::dynamic& config,
      int idValue) {
    if (!config.isObject() || !config.count("sw") ||
        !config["sw"].count("loadBalancers")) {
      return nullptr;
    }
    for (const auto& lb : config["sw"]["loadBalancers"]) {
      if (lb.count("id") && lb["id"].asInt() == idValue) {
        return &lb;
      }
    }
    return nullptr;
  }

  // Comma-joined restore tokens for one fieldSelection list; "none" if empty.
  static std::string restoreTokens(
      const folly::dynamic& lb,
      const std::string& fieldKey) {
    std::vector<std::string> tokens;
    if (lb.count("fieldSelection") && lb["fieldSelection"].count(fieldKey)) {
      for (const auto& v : lb["fieldSelection"][fieldKey]) {
        tokens.push_back(fieldIntToToken(fieldKey, v.asInt()));
      }
    }
    if (tokens.empty()) {
      return "none";
    }
    std::string joined = tokens[0];
    for (size_t i = 1; i < tokens.size(); ++i) {
      joined += "," + tokens[i];
    }
    return joined;
  }

  // Asserted from the test body (ASSERT_TRUE) so a failed CLI step aborts
  // the test before the next commit.
  AssertionResult runCliOk(const std::vector<std::string>& args) {
    auto result = runCli(args);
    if (result.exitCode != 0) {
      discardSession();
      return AssertionFailure()
          << "runCli failed: args[0]=" << args[0] << " stdout=" << result.stdout
          << " stderr=" << result.stderr;
    }
    return AssertionSuccess();
  }

  // Stages the config CLI calls that rebuild the captured entry. Leaves the
  // session staged; the caller commits.
  AssertionResult stageEcmpRestore(const folly::dynamic& original) {
    auto algorithm = runCliOk(
        {"config",
         "load-balancing",
         "ecmp",
         "hash-algorithm",
         algorithmIntToToken(original["algorithm"].asInt())});
    if (!algorithm) {
      return algorithm;
    }
    static const std::map<std::string, std::string> kAttrByKey = {
        {"ipv4Fields", "hash-fields-ipv4"},
        {"ipv6Fields", "hash-fields-ipv6"},
        {"transportFields", "hash-fields-transport"},
        {"mplsFields", "hash-fields-mpls"},
    };
    for (const auto& [fieldKey, attr] : kAttrByKey) {
      auto fields = runCliOk(
          {"config",
           "load-balancing",
           "ecmp",
           attr,
           restoreTokens(original, fieldKey)});
      if (!fields) {
        return fields;
      }
    }
    if (original.count("seed")) {
      auto seed = runCliOk(
          {"config",
           "load-balancing",
           "ecmp",
           "hash-seed",
           std::to_string(original["seed"].asInt())});
      if (!seed) {
        return seed;
      }
    }
    return AssertionSuccess();
  }

  // The delete is committed before the restore runs, so a fatal assertion in
  // between would otherwise leave the DUT without its ECMP load-balancer.
  // armRestore() records the entry to put back; the test clears it once the
  // restore has been committed.
  void armRestore(const folly::dynamic& original) {
    pendingRestore_ = original;
  }

  void clearRestore() {
    pendingRestore_ = nullptr;
  }

  void TearDown() override {
    if (!pendingRestore_.isNull()) {
      if (stageEcmpRestore(pendingRestore_)) {
        commitConfig();
      } else {
        XLOG(ERR) << "TearDown failed to restore the ECMP load-balancer";
      }
    }
    Fboss2IntegrationTest::TearDown();
  }

 private:
  // The ECMP load-balancer captured before the delete was committed, restored
  // in TearDown if the test did not get far enough to commit the restore.
  folly::dynamic pendingRestore_{nullptr};
};

TEST_F(DeleteLoadBalancingTest, DeleteThenRestoreEcmp) {
  XLOG(INFO) << "[Step 1] Capture the running ECMP load-balancer";
  folly::dynamic original = folly::dynamic::object;
  {
    auto config = getRunningConfig();
    const auto* lb = findLoadBalancer(config, kEcmpIdValue);
    ASSERT_NE(lb, nullptr) << "no ECMP load balancer in running config";
    original = *lb;
  }
  // The config CLI cannot restore udfGroups; every supported DUT runs
  // without them (same constraint as ConfigLoadBalancingTest).
  if (original.count("fieldSelection") &&
      original["fieldSelection"].count("udfGroups")) {
    ASSERT_TRUE(original["fieldSelection"]["udfGroups"].empty())
        << "ECMP load balancer has udfGroups; restore would drop them";
  }

  XLOG(INFO) << "[Step 2] Delete the ECMP load-balancer and commit";
  ASSERT_TRUE(runCliOk({"delete", "load-balancing", "ecmp"}));
  armRestore(original);
  commitConfig();
  waitForAgentReady();

  XLOG(INFO) << "[Step 3] Check the ECMP load-balancer is gone";
  {
    auto config = waitForRunningConfig([&](const folly::dynamic& c) {
      return findLoadBalancer(c, kEcmpIdValue) == nullptr;
    });
    EXPECT_EQ(findLoadBalancer(config, kEcmpIdValue), nullptr)
        << "ECMP load balancer still in running config";
  }

  XLOG(INFO) << "[Step 4] Restore the original ECMP load-balancer";
  ASSERT_TRUE(stageEcmpRestore(original));
  commitConfig();
  clearRestore();
  waitForAgentReady();

  // Presence check only: per-field config round-trip correctness is
  // ConfigLoadBalancingTest's coverage.
  auto config = waitForRunningConfig([&](const folly::dynamic& c) {
    return findLoadBalancer(c, kEcmpIdValue) != nullptr;
  });
  EXPECT_NE(findLoadBalancer(config, kEcmpIdValue), nullptr)
      << "ECMP load balancer was not restored";
}
