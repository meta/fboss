// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end tests for `fboss2-dev config protocol bgp policy routing-policy
 * <name> term <seq-num> match from <attribute> <value>`.
 *
 * Scope: the match level of a routing-policy term
 * (bgp_policy.BgpPolicyAtomicMatch entries under the term's
 * .policy_entries[].policy_match_entries). Like `action`, `match` is a real
 * CLI11 subcommand, so these tests exercise the two-deep subcommand dispatch
 * through the full CLI parse. Every test stages the change AND commits it,
 * then asserts the values landed at the correct thrift field paths inside
 * bgpd's running config (via getRunningConfig RPC).
 *
 * These assertions are load-bearing in a way the staged-config ones are not:
 * bgpd CONSTRUCTS a Policy from every statement in its config, rejecting an
 * atomic match whose type it does not support, whose payload field is unset,
 * or whose by-name reference does not resolve — so a wrong encoding
 * crash-loops the daemon and fails the test here rather than sitting inert in
 * the config. That is also why this test creates the as-path-list and
 * prefix-list it matches on.
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
const std::string kPolicy = "FBOSS2-TEST-RP-MATCH";
} // namespace

class ConfigBgpPolicyRoutingPolicyTermMatchTest : public ConfigBgpTestBase {
 protected:
  // Stage `... routing-policy <kPolicy> term 10 match <tokens...>` WITHOUT
  // clearing the staged session, so matches can accumulate across
  // invocations. Returns the staged session JSON.
  folly::dynamic stageMatch(const std::vector<std::string>& tokens) {
    std::vector<std::string> args = {
        "config",
        "protocol",
        "bgp",
        "policy",
        "routing-policy",
        kPolicy,
        "term",
        "10",
        "match"};
    args.insert(args.end(), tokens.begin(), tokens.end());
    auto result = runCli(args);
    EXPECT_EQ(result.exitCode, 0)
        << "stdout=" << result.stdout << " stderr=" << result.stderr;
    EXPECT_THAT(result.stdout, Not(HasSubstr("Error:")));
    return readBgpSessionConfig();
  }

  // Stage any `config ...` command, failing the test on a non-zero exit.
  void stageCli(const std::vector<std::string>& args) {
    auto result = runCli(args);
    EXPECT_EQ(result.exitCode, 0)
        << "stdout=" << result.stdout << " stderr=" << result.stderr;
    EXPECT_THAT(result.stdout, Not(HasSubstr("Error:")));
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

  // The atomic match of `type` in the term's policy_match_entries — the only
  // match container bgpd reads. BgpPolicyAtomicMatchType rides the SimpleJSON
  // wire as an int.
  static const folly::dynamic* findAtomic(
      const folly::dynamic& term,
      int64_t type) {
    if (term.count("policy_match_entries") == 0) {
      return nullptr;
    }
    const auto& match = term["policy_match_entries"];
    if (match.count("match_entries") == 0) {
      return nullptr;
    }
    for (const auto& atomic : match["match_entries"]) {
      if (atomic.count("type") && atomic["type"].asInt() == type) {
        return &atomic;
      }
    }
    return nullptr;
  }
};

TEST_F(ConfigBgpPolicyRoutingPolicyTermMatchTest, SetMatchesAndCommit) {
  discardSession();
  clearBgpSession();
  // A match reference must resolve: PolicyManager::PopulateReferences throws
  // "Could not find <T> reference" for a name absent from its by-name map,
  // which crash-loops bgpd. So create the lists this term will reference
  // before referencing them.
  stageCli(
      {"config",
       "protocol",
       "bgp",
       "policy",
       "as-path-list",
       "FBOSS2-TEST-ASPL",
       "entry",
       "10",
       "asn-regexp",
       "^65000$"});
  // Deliberately entry-less. bgpd's
  // PrefixTreeMatch::validateAndCreatePrefixTree throws "Unsupported Prefix
  // configuration: seq_num" for any prefix entry carrying a seq_num — which is
  // exactly how `prefix-list <name> entry <seq-num>` keys its entries — so a
  // referenced prefix-list with entries crash-loops the daemon today. An empty
  // prefixes[] skips that validation loop, which still proves this term's
  // reference resolves and is encoded correctly.
  stageCli(
      {"config", "protocol", "bgp", "policy", "prefix-list", "FBOSS2-TEST-PL"});
  stageMatch({"from", "as-path-list", "FBOSS2-TEST-ASPL"});
  stageMatch({"from", "origin", "IGP"});
  stageMatch({"from", "prefix-list", "FBOSS2-TEST-PL"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  // bgpd's getRunningConfig RPC proves it parsed, adopted AND constructed the
  // term's atomic matches: an unsupported type or an unset payload field
  // throws during Policy construction and crash-loops the daemon, so simply
  // reaching these assertions is itself a check on the encoding.
  // BgpPolicyAtomicMatchType wire values: AS_PATH=2, ORIGIN=4,
  // PREFIX_LIST=5.
  auto running = readRunningBgpConfigViaRpc();
  const auto* term = findTerm(running);
  ASSERT_NE(term, nullptr) << "bgpd's running config has no routing-policy "
                           << kPolicy << " term 10";

  // A reference is carried in *_list_names, which
  // PolicyManager::PopulateReferences resolves against its by-name maps.
  const auto* asPath = findAtomic(*term, 2);
  ASSERT_NE(asPath, nullptr);
  EXPECT_EQ(
      (*asPath)["as_path_filters"]["as_path_list_names"][0].asString(),
      "FBOSS2-TEST-ASPL");

  const auto* origin = findAtomic(*term, 4);
  ASSERT_NE(origin, nullptr);
  // Origin.IGP = 1.
  EXPECT_EQ((*origin)["origin"].asInt(), 1);

  const auto* prefixList = findAtomic(*term, 5);
  ASSERT_NE(prefixList, nullptr);
  EXPECT_EQ(
      (*prefixList)["prefix_filters"]["prefix_list_names"][0].asString(),
      "FBOSS2-TEST-PL");
}
