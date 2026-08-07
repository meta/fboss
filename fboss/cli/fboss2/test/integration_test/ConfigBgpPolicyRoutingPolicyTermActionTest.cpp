// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end tests for `fboss2-dev config protocol bgp policy routing-policy
 * <name> term <seq-num> action result|set ...`.
 *
 * Scope: the action level of a routing-policy term (term_miss_action and
 * bgp_policy.BgpPolicyAction entries in .policy_entries[]
 * .policy_action_entries[]). Like `term`, `action` is a real CLI11
 * subcommand, so these tests exercise the two-deep subcommand dispatch
 * through the full CLI parse. Every test stages the change AND commits it,
 * then asserts the values landed at the correct thrift field paths inside
 * bgpd's running config (via getRunningConfig RPC).
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
// Test-only routing-policy name, unlikely to collide with a real policy in
// the device's running BGP config.
const std::string kPolicy = "FBOSS2-TEST-RP-ACTION";
} // namespace

class ConfigBgpPolicyRoutingPolicyTermActionTest : public ConfigBgpTestBase {
 protected:
  // Stage `... routing-policy <kPolicy> term 10 action <tokens...>` WITHOUT
  // clearing the staged session, so actions can accumulate across
  // invocations. Returns the staged session JSON.
  folly::dynamic stageAction(const std::vector<std::string>& tokens) {
    std::vector<std::string> args = {
        "config",
        "protocol",
        "bgp",
        "policy",
        "routing-policy",
        kPolicy,
        "term",
        "10",
        "action"};
    args.insert(args.end(), tokens.begin(), tokens.end());
    auto result = runCli(args);
    EXPECT_EQ(result.exitCode, 0)
        << "stdout=" << result.stdout << " stderr=" << result.stderr;
    EXPECT_THAT(result.stdout, Not(HasSubstr("Error:")));
    return readBgpSessionConfig();
  }

  // The .policy_entries[] term with sequence_number 10 of the policy named
  // kPolicy, or nullptr.
  static const folly::dynamic* findTerm(const folly::dynamic& config) {
    if (config.count("policies") == 0 ||
        config["policies"].count("bgp_policy_statements") == 0) {
      return nullptr;
    }
    for (const auto& policy : config["policies"]["bgp_policy_statements"]) {
      if (policy.count("name") && policy["name"].asString() == kPolicy &&
          policy.count("policy_entries")) {
        for (const auto& term : policy["policy_entries"]) {
          if (term.count("sequence_number") &&
              term["sequence_number"].asInt() == 10) {
            return &term;
          }
        }
      }
    }
    return nullptr;
  }

  // The action entry containing `field`, or nullptr.
  static const folly::dynamic* findAction(
      const folly::dynamic& term,
      const std::string& field) {
    if (term.count("policy_action_entries") == 0) {
      return nullptr;
    }
    for (const auto& action : term["policy_action_entries"]) {
      if (action.count(field)) {
        return &action;
      }
    }
    return nullptr;
  }
};

TEST_F(ConfigBgpPolicyRoutingPolicyTermActionTest, SetActionsAndCommit) {
  discardSession();
  clearBgpSession();
  stageAction({"result", "CONTINUE"});
  stageAction({"set", "as-path", "prepend", "65000", "65000"});
  stageAction({"set", "community", "65000:100", "additive"});
  stageAction({"set", "local-pref", "200"});
  stageAction({"set", "med", "50"});
  stageAction({"set", "next-hop", "2001:db8::99"});
  stageAction({"set", "origin", "IGP"});
  stageAction({"set", "weight", "100"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  // bgpd's getRunningConfig RPC proves it parsed and adopted the term's
  // action entries, one entry per action kind.
  auto running = readRunningBgpConfigViaRpc();
  const auto* term = findTerm(running);
  ASSERT_NE(term, nullptr) << "bgpd's running config has no routing-policy "
                           << kPolicy << " term 10";

  // result CONTINUE → FlowControlAction.NEXT_TERM = 3 (integer on the
  // SimpleJSON wire).
  EXPECT_EQ((*term)["term_miss_action"].asInt(), 3);

  const auto* prepend = findAction(*term, "set_as_path_prepend");
  ASSERT_NE(prepend, nullptr);
  EXPECT_EQ((*prepend)["set_as_path_prepend"]["asn"].asInt(), 65000);
  EXPECT_EQ((*prepend)["set_as_path_prepend"]["repeat_times"].asInt(), 2);

  const auto* community = findAction(*term, "community_action");
  ASSERT_NE(community, nullptr);
  // additive → CommunityActionType.ADD = 1 (bgpd reads the deprecated
  // community_action struct; the type field rides the wire too:
  // BgpPolicyActionType.COMMUNITY_LIST = 2).
  EXPECT_EQ((*community)["type"].asInt(), 2);
  EXPECT_EQ((*community)["community_action"]["action_type"].asInt(), 1);
  EXPECT_EQ(
      (*community)["community_action"]["communities"][0].asString(),
      "65000:100");

  const auto* localPref = findAction(*term, "set_local_pref");
  ASSERT_NE(localPref, nullptr);
  EXPECT_EQ((*localPref)["set_local_pref"]["local_pref"].asInt(), 200);

  const auto* med = findAction(*term, "med_action");
  ASSERT_NE(med, nullptr);
  EXPECT_EQ((*med)["med_action"]["med_value"].asInt(), 50);
  // MedActionType.SET = 1.
  EXPECT_EQ((*med)["med_action"]["med_action_type"].asInt(), 1);

  const auto* nexthop = findAction(*term, "set_nexthop");
  ASSERT_NE(nexthop, nullptr);
  EXPECT_EQ(
      (*nexthop)["set_nexthop"]["next_hop"]["next_hop_prefix"].asString(),
      "2001:db8::99");

  const auto* origin = findAction(*term, "set_origin");
  ASSERT_NE(origin, nullptr);
  // Origin.IGP = 1.
  EXPECT_EQ((*origin)["set_origin"].asInt(), 1);

  const auto* weight = findAction(*term, "weight_action");
  ASSERT_NE(weight, nullptr);
  EXPECT_EQ((*weight)["weight_action"]["weight_value"].asInt(), 100);
}
