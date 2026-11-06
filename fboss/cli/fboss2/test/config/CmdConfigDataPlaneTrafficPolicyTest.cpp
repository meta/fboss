/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <CLI/App.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/TrafficPolicyUtils.h"
#include "fboss/cli/fboss2/commands/config/data_plane/traffic_policy/CmdConfigDataPlaneTrafficPolicy.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

// One ACL table holding the rule the actions attach to, and nothing under
// either traffic policy, so each test starts from an empty matchToAction.
static constexpr auto kSeedConfig = R"({
  "sw": {
    "aclTableGroups": [{
      "name": "ingress-ACL-Table-Group",
      "stage": 0,
      "aclTables": [{
        "name": "AclTable1",
        "priority": 0,
        "aclEntries": [
          {"name": "queue-per-host-queue-0-l2", "actionType": 1}
        ],
        "actionTypes": [],
        "qualifiers": [],
        "udfGroups": []
      }]
    }]
  }
})";

class CmdConfigDataPlaneTrafficPolicyTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigDataPlaneTrafficPolicyTestFixture()
      : CmdConfigTestBase(
            "fboss_dp_traffic_policy_test_%%%%-%%%%-%%%%-%%%%",
            kSeedConfig) {}

 protected:
  const std::string cmdPrefix_ = "config data-plane traffic-policy";
  const std::string kRule = "queue-per-host-queue-0-l2";

  cfg::MatchAction& dataPlaneAction(const std::string& matcher) {
    auto& cfg = ConfigSession::getInstance().getAgentConfig();
    auto policy = cfg.sw()->dataPlaneTrafficPolicy();
    if (!policy) {
      throw std::runtime_error("dataPlaneTrafficPolicy not set");
    }
    for (auto& mta : *policy->matchToAction()) {
      if (*mta.matcher() == matcher) {
        return *mta.action();
      }
    }
    throw std::runtime_error("no matchToAction for " + matcher);
  }

  std::string apply(const std::vector<std::string>& actionTokens) {
    std::vector<std::string> argv{"match", kRule, "action"};
    argv.insert(argv.end(), actionTokens.begin(), actionTokens.end());
    setupTestableConfigSession(cmdPrefix_, "");
    CmdConfigDataPlaneTrafficPolicy cmd;
    HostInfo host("testhost");
    traffic_policy::TrafficPolicyArgs args(argv);
    return cmd.queryClient(host, args);
  }
};

// Real-world queue-per-host example: a lookup-class match paired with a
// send-to-queue action on the dataplane policy.
TEST_F(CmdConfigDataPlaneTrafficPolicyTestFixture, setSendToQueue) {
  apply({"send-to-queue", "0"});
  EXPECT_EQ(*dataPlaneAction(kRule).sendToQueue()->queueId(), 0);
}

TEST_F(CmdConfigDataPlaneTrafficPolicyTestFixture, setCounter) {
  apply({"counter", "queue-per-host-queue-0-l2"});
  EXPECT_EQ(*dataPlaneAction(kRule).counter(), "queue-per-host-queue-0-l2");
}

TEST_F(CmdConfigDataPlaneTrafficPolicyTestFixture, setTcAcceptsNine) {
  // Shipped CPU policies use tc 9, so the acl-rule side's 0-7 cap would be
  // wrong here; the thrift byte range is what applies.
  apply({"set-tc", "9"});
  EXPECT_EQ(*dataPlaneAction(kRule).setTc()->tcValue(), 9);
}

TEST_F(CmdConfigDataPlaneTrafficPolicyTestFixture, setValuelessAction) {
  apply({"trap-to-cpu"});
  EXPECT_EQ(*dataPlaneAction(kRule).toCpuAction(), cfg::ToCpuAction::TRAP);
}

TEST_F(CmdConfigDataPlaneTrafficPolicyTestFixture, setRedirectNexthop) {
  apply({"redirect", "nexthop", "2401:db00::1"});
  const auto& hops =
      *dataPlaneAction(kRule).redirectToNextHop()->redirectNextHops();
  ASSERT_EQ(hops.size(), 1u);
  EXPECT_EQ(*hops[0].ip(), "2401:db00::1");
}

// The tests above cover the generic int/string/valueless shapes. Every row
// below carries its own keyword->enumerator or keyword->field mapping, which
// is where a copy/paste slip lands the wrong field or the wrong enumerator --
// copy-to-cpu vs trap-to-cpu being the obvious pair, since both write
// toCpuAction.
TEST_F(CmdConfigDataPlaneTrafficPolicyTestFixture, setMappedActionRows) {
  struct Case {
    std::vector<std::string> tokens;
    void (*check)(const cfg::MatchAction&);
  };
  const std::vector<Case> kCases = {
      {{"set-dscp", "46"},
       [](const cfg::MatchAction& a) {
         EXPECT_EQ(*a.setDscp()->dscpValue(), 46);
       }},
      {{"mirror-ingress", "mirror-in"},
       [](const cfg::MatchAction& a) {
         EXPECT_EQ(*a.ingressMirror(), "mirror-in");
       }},
      {{"mirror-egress", "mirror-out"},
       [](const cfg::MatchAction& a) {
         EXPECT_EQ(*a.egressMirror(), "mirror-out");
       }},
      {{"copy-to-cpu"},
       [](const cfg::MatchAction& a) {
         EXPECT_EQ(*a.toCpuAction(), cfg::ToCpuAction::COPY);
       }},
      {{"flowlet", "forward"},
       [](const cfg::MatchAction& a) {
         EXPECT_EQ(*a.flowletAction(), cfg::FlowletAction::FORWARD);
       }},
      {{"flowlet", "disable"},
       [](const cfg::MatchAction& a) {
         EXPECT_EQ(*a.flowletAction(), cfg::FlowletAction::DISABLE);
       }},
      {{"ecmp-hash", "flowlet-quality"},
       [](const cfg::MatchAction& a) {
         EXPECT_EQ(
             *a.ecmpHashAction()->switchingMode(),
             cfg::SwitchingMode::FLOWLET_QUALITY);
       }},
      {{"ecmp-hash", "per-packet-quality"},
       [](const cfg::MatchAction& a) {
         EXPECT_EQ(
             *a.ecmpHashAction()->switchingMode(),
             cfg::SwitchingMode::PER_PACKET_QUALITY);
       }},
      {{"ecmp-hash", "fixed-assignment"},
       [](const cfg::MatchAction& a) {
         EXPECT_EQ(
             *a.ecmpHashAction()->switchingMode(),
             cfg::SwitchingMode::FIXED_ASSIGNMENT);
       }},
      {{"ecmp-hash", "per-packet-random"},
       [](const cfg::MatchAction& a) {
         EXPECT_EQ(
             *a.ecmpHashAction()->switchingMode(),
             cfg::SwitchingMode::PER_PACKET_RANDOM);
       }},
  };

  for (const auto& testCase : kCases) {
    SCOPED_TRACE(
        testCase.tokens.size() > 1
            ? testCase.tokens[0] + " " + testCase.tokens[1]
            : testCase.tokens[0]);
    apply(testCase.tokens);
    testCase.check(dataPlaneAction(kRule));
  }
}

// A keyword outside a mapped row's own table has to throw; the alternative is
// the setter falling through and leaving the action quietly unset.
TEST_F(CmdConfigDataPlaneTrafficPolicyTestFixture, rejectsUnmappedActionValue) {
  EXPECT_THROW(apply({"flowlet", "enable"}), std::invalid_argument);
  EXPECT_THROW(apply({"ecmp-hash", "per-packet"}), std::invalid_argument);
}

TEST_F(
    CmdConfigDataPlaneTrafficPolicyTestFixture,
    actionsAccumulateOnOneMatcher) {
  apply({"send-to-queue", "0"});
  apply({"counter", "c1"});
  auto& action = dataPlaneAction(kRule);
  EXPECT_EQ(*action.sendToQueue()->queueId(), 0);
  EXPECT_EQ(*action.counter(), "c1");

  auto& policy = *ConfigSession::getInstance()
                      .getAgentConfig()
                      .sw()
                      ->dataPlaneTrafficPolicy();
  EXPECT_EQ(policy.matchToAction()->size(), 1u);
}

TEST_F(CmdConfigDataPlaneTrafficPolicyTestFixture, rejectsUnknownAction) {
  EXPECT_THROW(apply({"drop", "1"}), std::invalid_argument);
}

// The delete command tree has a literal "action" node between the matcher
// name and the action type, so a rule named "action" would be configurable
// here but impossible to target for delete.
TEST_F(CmdConfigDataPlaneTrafficPolicyTestFixture, rejectsRuleNamedAction) {
  EXPECT_THROW(
      traffic_policy::TrafficPolicyArgs(
          {"match", "action", "action", "counter", "c1"}),
      std::invalid_argument);
}

TEST_F(CmdConfigDataPlaneTrafficPolicyTestFixture, rejectsWrongArity) {
  EXPECT_THROW(apply({"send-to-queue"}), std::invalid_argument);
  EXPECT_THROW(apply({"trap-to-cpu", "1"}), std::invalid_argument);
  EXPECT_THROW(apply({"redirect", "nexthop"}), std::invalid_argument);
}

TEST_F(
    CmdConfigDataPlaneTrafficPolicyTestFixture,
    rejectsRedirectWithoutKeyword) {
  EXPECT_THROW(
      apply({"redirect", "gateway", "1.1.1.1"}), std::invalid_argument);
}

// updateAclsImpl resolves a rule to the CPU policy first and only falls back
// to the dataplane one, so a rule in both would have its dataplane actions
// silently ignored.
TEST_F(
    CmdConfigDataPlaneTrafficPolicyTestFixture,
    refusesRuleAlreadyInCoppPolicy) {
  setupTestableConfigSession(cmdPrefix_, "");
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
  traffic_policy::applyAction(
      swConfig, traffic_policy::PolicyKind::Cpu, kRule, {"send-to-queue", "9"});

  EXPECT_THROW(
      traffic_policy::applyAction(
          swConfig,
          traffic_policy::PolicyKind::DataPlane,
          kRule,
          {"send-to-queue", "0"}),
      std::runtime_error);
}

TEST_F(
    CmdConfigDataPlaneTrafficPolicyTestFixture,
    deleteRemovesOneActionThenTheEntry) {
  setupTestableConfigSession(cmdPrefix_, "");
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
  const auto kind = traffic_policy::PolicyKind::DataPlane;
  traffic_policy::applyAction(swConfig, kind, kRule, {"send-to-queue", "0"});
  traffic_policy::applyAction(swConfig, kind, kRule, {"counter", "c1"});

  traffic_policy::deleteAction(swConfig, kind, kRule, "counter");
  EXPECT_FALSE(dataPlaneAction(kRule).counter().has_value());
  EXPECT_TRUE(dataPlaneAction(kRule).sendToQueue().has_value());

  // Dropping the last action drops the matchToAction entry with it, so no
  // empty entry is left holding checkTrafficPolicyAclsExistInConfig hostage.
  traffic_policy::deleteAction(swConfig, kind, kRule, "send-to-queue");
  EXPECT_TRUE(swConfig.dataPlaneTrafficPolicy()->matchToAction()->empty());
}

// Delete-side twin of setMappedActionRows. Every row's reset has to clear the
// same field its setter wrote; a row whose set and reset disagree would pass
// the set test above and only show up here.
TEST_F(CmdConfigDataPlaneTrafficPolicyTestFixture, deleteClearsMappedRows) {
  struct Case {
    std::vector<std::string> tokens;
    const char* actionType;
    void (*stillSet)(const cfg::MatchAction&);
  };
  const std::vector<Case> kCases = {
      {{"set-dscp", "46"},
       "set-dscp",
       [](const cfg::MatchAction& a) {
         EXPECT_FALSE(a.setDscp().has_value());
       }},
      {{"mirror-ingress", "mirror-in"},
       "mirror-ingress",
       [](const cfg::MatchAction& a) {
         EXPECT_FALSE(a.ingressMirror().has_value());
       }},
      {{"mirror-egress", "mirror-out"},
       "mirror-egress",
       [](const cfg::MatchAction& a) {
         EXPECT_FALSE(a.egressMirror().has_value());
       }},
      {{"copy-to-cpu"},
       "copy-to-cpu",
       [](const cfg::MatchAction& a) {
         EXPECT_FALSE(a.toCpuAction().has_value());
       }},
      {{"trap-to-cpu"},
       "trap-to-cpu",
       [](const cfg::MatchAction& a) {
         EXPECT_FALSE(a.toCpuAction().has_value());
       }},
      {{"flowlet", "forward"},
       "flowlet",
       [](const cfg::MatchAction& a) {
         EXPECT_FALSE(a.flowletAction().has_value());
       }},
      {{"ecmp-hash", "flowlet-quality"},
       "ecmp-hash",
       [](const cfg::MatchAction& a) {
         EXPECT_FALSE(a.ecmpHashAction().has_value());
       }},
      {{"redirect", "nexthop", "2401:db00::1"},
       "redirect",
       [](const cfg::MatchAction& a) {
         EXPECT_FALSE(a.redirectToNextHop().has_value());
       }},
  };

  setupTestableConfigSession(cmdPrefix_, "");
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
  const auto kind = traffic_policy::PolicyKind::DataPlane;
  for (const auto& testCase : kCases) {
    SCOPED_TRACE(testCase.actionType);
    // A sibling action keeps the entry alive so the delete under test is the
    // only thing that can clear the field being checked.
    traffic_policy::applyAction(swConfig, kind, kRule, {"counter", "c-keep"});
    traffic_policy::applyAction(swConfig, kind, kRule, testCase.tokens);

    auto msg = traffic_policy::deleteAction(
        swConfig, kind, kRule, testCase.actionType);
    EXPECT_THAT(msg, HasSubstr("Successfully deleted"));
    testCase.stillSet(dataPlaneAction(kRule));

    traffic_policy::deleteAction(swConfig, kind, kRule, "counter");
  }
}

// trap-to-cpu and copy-to-cpu are two keywords over the single toCpuAction
// field, so a reset that only checked has_value() would clear whichever one
// happened to be set and report the other as deleted.
TEST_F(
    CmdConfigDataPlaneTrafficPolicyTestFixture,
    deleteToCpuActionOnlyClearsItsOwnKeyword) {
  setupTestableConfigSession(cmdPrefix_, "");
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
  const auto kind = traffic_policy::PolicyKind::DataPlane;
  traffic_policy::applyAction(swConfig, kind, kRule, {"trap-to-cpu"});

  auto msg = traffic_policy::deleteAction(swConfig, kind, kRule, "copy-to-cpu");
  EXPECT_THAT(msg, HasSubstr("already absent"));
  ASSERT_TRUE(dataPlaneAction(kRule).toCpuAction().has_value());
  EXPECT_EQ(*dataPlaneAction(kRule).toCpuAction(), cfg::ToCpuAction::TRAP);

  // The owning keyword still clears it.
  msg = traffic_policy::deleteAction(swConfig, kind, kRule, "trap-to-cpu");
  EXPECT_THAT(msg, HasSubstr("Successfully deleted"));
  EXPECT_TRUE(swConfig.dataPlaneTrafficPolicy()->matchToAction()->empty());
}

// redirect writes RedirectNextHop.ip(), which the agent later feeds to
// folly::IPAddress mid-apply, so a non-address has to be refused here.
TEST_F(CmdConfigDataPlaneTrafficPolicyTestFixture, rejectsNonIpRedirect) {
  EXPECT_THROW(
      apply({"redirect", "nexthop", "not-an-ip"}), std::invalid_argument);
  EXPECT_THROW(apply({"redirect", "nexthop", ""}), std::invalid_argument);
}

// The advertised form string is what an arity error shows the user, so it has
// to agree with the range the setter enforces.
TEST_F(
    CmdConfigDataPlaneTrafficPolicyTestFixture,
    setTcFormMatchesAcceptedMax) {
  apply({"set-tc", "127"});
  EXPECT_EQ(*dataPlaneAction(kRule).setTc()->tcValue(), 127);
  EXPECT_THROW(apply({"set-tc", "128"}), std::invalid_argument);

  // form is only surfaced in the arity error, so provoke one to read it back.
  try {
    apply({"set-tc"});
    FAIL() << "set-tc with no value should throw";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("<0-127>"));
  }
}

// The 0-value forms (trap-to-cpu, copy-to-cpu) and the
// 2-value one (redirect) only work because addCliArg says expected(4, 6) with
// allow_extra_args(). Every other test here builds TrafficPolicyArgs directly,
// so this is the one that runs the real CLI11 parser over that wiring. It
// parses the option on a bare App, so it covers the arities, not the sibling
// subcommand reclassification the addCliArg comment warns about.
TEST_F(
    CmdConfigDataPlaneTrafficPolicyTestFixture,
    cli11ParsesEveryActionArity) {
  const std::vector<std::string> kForms = {
      "match r1 action trap-to-cpu",
      "match r1 action set-tc 9",
      "match r1 action redirect nexthop 2401:db00::1",
  };
  for (const auto& form : kForms) {
    SCOPED_TRACE(form);
    CLI::App app;
    std::vector<std::string> parsed;
    CmdConfigDataPlaneTrafficPolicyTraits::addCliArg(app, parsed);
    ASSERT_NO_THROW(app.parse(form, false));
    // Round-trips to the same tokens TrafficPolicyArgs expects.
    EXPECT_NO_THROW(traffic_policy::TrafficPolicyArgs{parsed});
  }
}

TEST_F(
    CmdConfigDataPlaneTrafficPolicyTestFixture,
    deleteAbsentActionIsReported) {
  setupTestableConfigSession(cmdPrefix_, "");
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
  const auto kind = traffic_policy::PolicyKind::DataPlane;
  traffic_policy::applyAction(swConfig, kind, kRule, {"send-to-queue", "0"});

  auto msg = traffic_policy::deleteAction(swConfig, kind, kRule, "counter");
  EXPECT_THAT(msg, HasSubstr("already absent"));
}

// A matchToAction entry can pre-exist with no actions set at all (e.g. from a
// hand-edited config). Deleting an already-absent action from it should still
// prune the empty entry, not just report the action as absent.
TEST_F(
    CmdConfigDataPlaneTrafficPolicyTestFixture,
    deleteAbsentActionPrunesPreExistingEmptyEntry) {
  setupTestableConfigSession(cmdPrefix_, "");
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
  const auto kind = traffic_policy::PolicyKind::DataPlane;
  traffic_policy::upsertMatcher(
      traffic_policy::policyFor(swConfig, kind), kRule);
  ASSERT_FALSE(swConfig.dataPlaneTrafficPolicy()->matchToAction()->empty());

  auto msg = traffic_policy::deleteAction(swConfig, kind, kRule, "counter");
  EXPECT_THAT(msg, HasSubstr("already absent"));
  EXPECT_TRUE(swConfig.dataPlaneTrafficPolicy()->matchToAction()->empty());
}

} // namespace facebook::fboss
