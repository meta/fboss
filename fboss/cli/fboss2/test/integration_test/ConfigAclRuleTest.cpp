// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end tests for the `acl rule` CLI surface — both
 *   `fboss2-dev config acl rule <table> <rule> <attr> <value>` (setter), and
 *   `fboss2-dev delete acl rule <table> <rule>`               (whole rule).
 *
 * Set tests cover all 16 AclEntry match-field attributes:
 *   source-ip, destination-ip, protocol, source-port, destination-port,
 *   dscp, tcp-flags, icmp-type, icmp-code, ip-fragment, ttl,
 *   destination-mac, ip-type, packet-lookup-result.
 *
 * For each attribute a `Set` test:
 *   `config acl rule` upserts an entry with the attribute set, commit, then
 *   verify the entry's running-config representation contains the field.
 *
 * The `DeleteRule` test exercises whole-entry deletion: create an entry via
 * `config acl rule`, commit, verify present, then `delete acl rule` and
 * verify the entry is gone from the table.
 *
 * The suite relies on the ACL table group shipped in the DUT's agent config
 * as-is. Mainstream platforms (e.g. montblanc/TH5) ship AclTable1 with an
 * empty `qualifiers` list, which SAI expands to the full ASIC-supported
 * qualifier set at table-create time — so entries matching
 * srcIp/dstIp/dscp/ttl/L4-ports/tcp-flags/etc. are accepted without declaring
 * qualifiers or restarting the agent. `config acl rule` edits only ACL
 * entries, which apply hitlessly (no table recreate). Tests skip cleanly on a
 * DUT whose config has no aclTableGroups AclTable to target.
 *
 * Per-test TearDown best-effort deletes the test rule so each test starts
 * from a clean table. The rule itself is created on demand by `config acl
 * rule` (which upserts), so no per-test pre-injection is needed.
 */

#include <fmt/format.h>
#include <folly/ScopeGuard.h>
#include <folly/json/dynamic.h>
#include <folly/logging/xlog.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <chrono>
#include <optional>
#include <string>
#include <vector>
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;
using ::testing::HasSubstr;

namespace {
constexpr auto kTestRuleName = "test-rule-acl-cli";
} // namespace

class ConfigAclRuleTest : public Fboss2IntegrationTest {
 protected:
  // These tests target the ACL table group shipped in the DUT's agent config.
  // We assume the DUT config already contains a multi-ACL-table group (its
  // AclTable1). Until fboss2 gains a CLI to create a default ACL table group
  // and table on the fly (mirroring the SAI agent tests'
  // utility::setupDefaultAclTableGroups), the suite depends on the DUT config
  // providing it, and skips cleanly if it is absent.
  //
  // No qualifier injection or agent restart is needed: mainstream platforms
  // (e.g. montblanc/TH5) ship AclTable1 with an empty `qualifiers` list, which
  // SAI expands to the full ASIC-supported qualifier set at table-create time.
  void SetUp() override {
    Fboss2IntegrationTest::SetUp();
    auto config = getRunningConfig();
    const auto* sw = config.get_ptr("sw");
    const auto* groups = sw ? sw->get_ptr("aclTableGroups") : nullptr;
    if (!groups || !groups->isArray() || groups->empty() ||
        !(*groups)[0].count("aclTables") ||
        !(*groups)[0]["aclTables"].isArray() ||
        (*groups)[0]["aclTables"].empty()) {
      GTEST_SKIP() << "DUT config has no aclTableGroups AclTable — "
                      "nothing for ConfigAclRuleTest to target";
    }
  }

  void TearDown() override {
    // Best-effort wipe so each test starts from a clean table. runCli reports
    // failure via exitCode (it does not throw), so inspect it explicitly.
    // targetTable() can still throw if the running config has no ACL tables;
    // swallow that so the framework's TearDown still runs.
    try {
      auto tableName = targetTable();
      auto del = runCli({"delete", "acl", "rule", tableName, kTestRuleName});
      if (del.exitCode == 0) {
        // The rule existed and is staged for deletion; the commit must land,
        // otherwise the next test (which reuses kTestRuleName) would see a
        // stale rule. Surface a commit failure rather than swallowing it.
        auto commit = runCli({"config", "session", "commit"});
        EXPECT_EQ(commit.exitCode, 0)
            << "TearDown commit of rule deletion failed: " << commit.stderr;
      } else {
        // Rule absent (e.g. the test errored before creating it) — nothing to
        // commit. The base SetUp discards any stale session before the next
        // test, so no cleanup is needed here.
        XLOG(INFO) << "TearDown: no '" << kTestRuleName
                   << "' rule to delete (exit " << del.exitCode << ")";
      }
    } catch (const std::exception& e) {
      XLOG(WARN) << "TearDown cleanup skipped: " << e.what();
    }
    Fboss2IntegrationTest::TearDown();
  }

  // The first AclTable of the first AclTableGroup — the table the CLI
  // targets. The CLI scans every group looking for a table with this name,
  // so the group index does not need to be conveyed.
  std::string targetTable() const {
    auto config = getRunningConfig();
    const auto& groups = config["sw"]["aclTableGroups"];
    if (groups.empty()) {
      throw std::runtime_error("no aclTableGroups in running config");
    }
    const auto& tables = groups[0]["aclTables"];
    if (tables.empty()) {
      throw std::runtime_error("no aclTables in aclTableGroups[0]");
    }
    return tables[0]["name"].asString();
  }

  // Find the MatchAction attached to a rule via
  // dataPlaneTrafficPolicy.matchToAction. Returns nullopt if none exists.
  std::optional<folly::dynamic> getMatchAction(
      const std::string& ruleName) const {
    auto config = getRunningConfig();
    const auto& sw = config["sw"];
    if (!sw.count("dataPlaneTrafficPolicy")) {
      return std::nullopt;
    }
    const auto& policy = sw["dataPlaneTrafficPolicy"];
    if (!policy.isObject() || !policy.count("matchToAction")) {
      return std::nullopt;
    }
    for (const auto& mta : policy["matchToAction"]) {
      if (mta["matcher"].asString() == ruleName) {
        return mta["action"];
      }
    }
    return std::nullopt;
  }

  std::optional<folly::dynamic> getRule(
      const std::string& tableName,
      const std::string& ruleName) const {
    auto config = getRunningConfig();
    const auto& groups = config["sw"]["aclTableGroups"];
    for (const auto& group : groups) {
      if (!group.count("aclTables")) {
        continue;
      }
      for (const auto& table : group["aclTables"]) {
        if (table["name"].asString() != tableName) {
          continue;
        }
        for (const auto& entry : table["aclEntries"]) {
          if (entry["name"].asString() == ruleName) {
            return entry;
          }
        }
      }
    }
    return std::nullopt;
  }

  void runConfigCli(
      const std::string& table,
      const std::string& rule,
      const std::string& attr,
      const std::vector<std::string>& values) {
    std::vector<std::string> argv{"config", "acl", "rule", table, rule, attr};
    for (const auto& v : values) {
      argv.push_back(v);
    }
    auto result = runCli(argv);
    ASSERT_EQ(result.exitCode, 0)
        << "stdout=" << result.stdout << " stderr=" << result.stderr;
    EXPECT_THAT(result.stdout, HasSubstr(attr));
  }

  // Attach an action to `rule` through the data-plane traffic policy. The acl
  // rule command no longer writes MatchAction fields; the policy verb names
  // the policy the action belongs to.
  void runTrafficPolicyActionCli(
      const std::string& rule,
      const std::vector<std::string>& action) {
    std::vector<std::string> argv{
        "config", "data-plane", "traffic-policy", "match", rule, "action"};
    for (const auto& a : action) {
      argv.push_back(a);
    }
    auto result = runCli(argv);
    ASSERT_EQ(result.exitCode, 0)
        << "stdout=" << result.stdout << " stderr=" << result.stderr;
    EXPECT_THAT(result.stdout, HasSubstr(action[0]));
  }

  void runDeleteCli(const std::string& table, const std::string& rule) {
    auto result = runCli({"delete", "acl", "rule", table, rule});
    ASSERT_EQ(result.exitCode, 0)
        << "stdout=" << result.stdout << " stderr=" << result.stderr;
    EXPECT_THAT(result.stdout, HasSubstr(rule));
  }

  // `prefix` followed by the current epoch in milliseconds, so a name never
  // collides with one left behind by an earlier run.
  static std::string uniqueName(const std::string& prefix) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return fmt::format(
        "{}{}",
        prefix,
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
  }

  // aclTableGroupMaps: SwitchIdList -> AclStage -> AclTableGroupFields.
  // Returns the rule's switch-state entry on every switch that has it.
  std::vector<folly::dynamic> getStateEntries(
      const std::string& tableName,
      const std::string& ruleName) const {
    std::vector<folly::dynamic> entries;
    auto groupMaps = getSwitchState("aclTableGroupMaps");
    for (const auto& [switchIds, stages] : groupMaps.items()) {
      for (const auto& [stage, group] : stages.items()) {
        const auto* tables = group.get_ptr("aclTableMap");
        const auto* table = tables ? tables->get_ptr(tableName) : nullptr;
        const auto* acls = table ? table->get_ptr("aclMap") : nullptr;
        const auto* entry = acls ? acls->get_ptr(ruleName) : nullptr;
        if (entry != nullptr) {
          entries.push_back(*entry);
        }
      }
    }
    return entries;
  }

  // Number of dataPlaneTrafficPolicy.matchToAction entries for the rule.
  int countMatchToActions(const std::string& ruleName) const {
    auto config = getRunningConfig();
    const auto* policy = config["sw"].get_ptr("dataPlaneTrafficPolicy");
    const auto* mtas = policy ? policy->get_ptr("matchToAction") : nullptr;
    if (mtas == nullptr) {
      return 0;
    }
    int count = 0;
    for (const auto& mta : *mtas) {
      if (mta["matcher"].asString() == ruleName) {
        ++count;
      }
    }
    return count;
  }

  // Deletes the test rule's only traffic-policy action with
  //   delete data-plane traffic-policy match <rule> action <keyword>
  // and commits. The rule must stay, its matchToAction must be gone, and its
  // switch-state aclAction must no longer carry `stateField`.
  void deleteActionAndVerify(
      const std::string& tableName,
      const std::string& keyword,
      const std::string& stateField) {
    auto del = runCli(
        {"delete",
         "data-plane",
         "traffic-policy",
         "match",
         kTestRuleName,
         "action",
         keyword});
    ASSERT_EQ(del.exitCode, 0)
        << "stdout=" << del.stdout << " stderr=" << del.stderr;
    commitConfig();

    EXPECT_FALSE(getMatchAction(kTestRuleName).has_value())
        << "matchToAction still present after deleting its only action";
    EXPECT_TRUE(getRule(tableName, kTestRuleName).has_value())
        << "rule removed along with its action";
    auto remaining = getStateEntries(tableName, kTestRuleName);
    ASSERT_FALSE(remaining.empty())
        << "rule not found in switch state after deleting its action";
    for (const auto& entry : remaining) {
      const auto* aclAction = entry.get_ptr("aclAction");
      EXPECT_TRUE(aclAction == nullptr || !aclAction->count(stateField))
          << stateField << " still in switch state after delete";
    }
  }

  // The agent rejects an ACL whose mirror is undefined and fboss2 has no
  // command that creates a mirror, so mirror tests add one to the session
  // config directly: a SPAN mirror to the first enabled port that is not
  // already mirrored. Saved to the session, not committed. Returns false if
  // there is no such port.
  bool stageSpanMirror(const std::string& mirrorName) {
    auto& session = ConfigSession::getInstance();
    auto& sw = *session.getAgentConfig().sw();
    for (const auto& port : *sw.ports()) {
      if (port.name().has_value() && *port.state() == cfg::PortState::ENABLED &&
          !port.ingressMirror().has_value() &&
          !port.egressMirror().has_value()) {
        cfg::Mirror mirror;
        mirror.name() = mirrorName;
        mirror.destination()->egressPort().emplace().name() = *port.name();
        sw.mirrors()->push_back(mirror);
        session.saveConfig();
        return true;
      }
    }
    return false;
  }

  // Cleanup for a mirror test: removes the test rule (which also drops its
  // matchToAction) and the mirror in one commit. Never throws, so it is safe
  // in a scope guard.
  void deleteRuleAndMirror(
      const std::string& tableName,
      const std::string& mirrorName) {
    try {
      discardSession();
      auto del = runCli({"delete", "acl", "rule", tableName, kTestRuleName});
      auto& session = ConfigSession::getInstance();
      auto& mirrors = *session.getAgentConfig().sw()->mirrors();
      auto before = mirrors.size();
      std::erase_if(mirrors, [&mirrorName](const cfg::Mirror& m) {
        return *m.name() == mirrorName;
      });
      bool mirrorRemoved = mirrors.size() != before;
      if (mirrorRemoved) {
        session.saveConfig();
      }
      if (del.exitCode != 0 && !mirrorRemoved) {
        return; // nothing was committed, nothing to undo
      }
      auto commit = runCli({"config", "session", "commit"});
      EXPECT_EQ(commit.exitCode, 0)
          << "cleanup commit failed: " << commit.stderr;
    } catch (const std::exception& e) {
      ADD_FAILURE() << "cleanup failed: " << e.what();
    }
  }

  // A single (attribute, values) baseline pair the test sets up before
  // exercising the attribute under test. Some attributes are only valid
  // alongside a matching `protocol` (e.g. icmp-type requires
  // protocol=icmp; tcp-flags requires protocol=tcp), and SAI rejects
  // entries with an inconsistent combination — so the test has to
  // populate the right baseline before mutating the attribute being
  // tested. The baseline is staged via `config acl rule` and committed
  // as a single hitless transaction together with the test attribute.
  struct Preset {
    std::string attr;
    std::vector<std::string> values{};
  };

  void stagePresets(
      const std::string& tableName,
      const std::string& ruleName,
      const std::vector<Preset>& presets) {
    for (const auto& p : presets) {
      runConfigCli(tableName, ruleName, p.attr, p.values);
    }
  }

  // Common driver for a single-attribute `set` test: stage any required
  // baseline, set the value, commit once, verify the running config
  // reflects it.
  template <typename Verify>
  void runSet(
      const std::string& attr,
      const std::vector<std::string>& values,
      Verify verify,
      const std::vector<Preset>& presets = {}) {
    auto tableName = targetTable();
    XLOG(INFO) << "[set] " << attr << " on " << tableName << "/"
               << kTestRuleName;
    stagePresets(tableName, kTestRuleName, presets);
    runConfigCli(tableName, kTestRuleName, attr, values);
    commitConfig();
    auto updated = getRule(tableName, kTestRuleName);
    ASSERT_TRUE(updated.has_value());
    verify(*updated);
  }

  // Driver for `action <subattr> [extras]` tests. The rule is created on
  // demand by setting a benign match-field (dscp) first, then the action
  // is layered on. Both stage operations and the final commit travel as
  // a single hitless transaction.
  template <typename Verify>
  void runActionSet(
      const std::string& subattr,
      const std::vector<std::string>& extras,
      Verify verify) {
    auto tableName = targetTable();
    XLOG(INFO) << "[set-action] " << subattr << " on " << tableName << "/"
               << kTestRuleName;
    runConfigCli(tableName, kTestRuleName, "dscp", {"10"});
    std::vector<std::string> values{subattr};
    for (const auto& e : extras) {
      values.push_back(e);
    }
    runConfigCli(tableName, kTestRuleName, "action", values);
    commitConfig();
    verify(tableName);
  }
};

// =============================================================
// Set tests — `config acl rule <table> <rule> <attr> <value>`
// =============================================================

TEST_F(ConfigAclRuleTest, SetSourceIp) {
  runSet("source-ip", {"10.250.0.0/24"}, [](auto& e) {
    EXPECT_EQ(e["srcIp"].asString(), "10.250.0.0/24");
  });
}

// RedirectNextHop.ip is not settable through `config acl rule ... action`; it
// lives on the rule's MatchAction in the data-plane traffic policy:
//   config data-plane traffic-policy match <rule> action redirect nexthop <ip>
// The agent keeps the configured nexthop in switch state and resolves it
// through the RIB into resolvedNexthops; a rule whose nexthop does not resolve
// is kept but disabled (ApplyThriftConfig.cpp, AclNexthopHandler.cpp). Whether
// a documentation address resolves depends on the DUT's routes, so the test
// checks that the agent's enabled flag agrees with its resolution rather than
// expecting one outcome.
TEST_F(ConfigAclRuleTest, SetActionRedirectNexthop) {
  auto tableName = targetTable();
  XLOG(INFO) << "[set-action-redirect-nexthop] " << tableName << "/"
             << kTestRuleName;

  // The running config holds exactly one matchToAction for the rule, whose
  // redirect carries the single nexthop `expected` and no interface. Switch
  // state carries the same nexthop, and the entry is enabled exactly when the
  // agent resolved it to at least one nexthop.
  auto expectRedirect = [&](const std::string& expected) {
    EXPECT_EQ(countMatchToActions(kTestRuleName), 1)
        << "expected one matchToAction for the rule";
    auto action = getMatchAction(kTestRuleName);
    if (!action.has_value() || !action->count("redirectToNextHop")) {
      ADD_FAILURE() << "redirectToNextHop missing from running config";
      return;
    }
    const auto& nhops = (*action)["redirectToNextHop"]["redirectNextHops"];
    if (nhops.size() != 1) {
      ADD_FAILURE() << "expected one redirect nexthop, got " << nhops.size();
      return;
    }
    EXPECT_EQ(nhops[0]["ip"].asString(), expected);
    EXPECT_FALSE(nhops[0].count("intfID")) << "intfID set without being given";

    auto entries = getStateEntries(tableName, kTestRuleName);
    if (entries.empty()) {
      ADD_FAILURE() << "rule not found in switch state";
      return;
    }
    for (const auto& entry : entries) {
      const auto* aclAction = entry.get_ptr("aclAction");
      const auto* redirect =
          aclAction ? aclAction->get_ptr("redirectToNextHop") : nullptr;
      if (redirect == nullptr) {
        ADD_FAILURE() << "redirectToNextHop missing from switch state";
        return;
      }
      const auto& stateNhops = (*redirect)["action"]["redirectNextHops"];
      if (stateNhops.size() != 1) {
        ADD_FAILURE() << "expected one nexthop in switch state, got "
                      << stateNhops.size();
        return;
      }
      EXPECT_EQ(stateNhops[0]["ip"].asString(), expected);
      const bool resolved = !(*redirect)["resolvedNexthops"].empty();
      const auto* enabled = entry.get_ptr("enabled");
      if (enabled == nullptr) {
        ADD_FAILURE() << "enabled not set on a redirect rule in switch state";
        return;
      }
      XLOG(INFO) << "redirect " << expected << ": resolved=" << resolved
                 << " enabled=" << enabled->asBool();
      EXPECT_EQ(enabled->asBool(), resolved)
          << "enabled does not match nexthop resolution";
    }
  };

  // 1. The rule matches a documentation prefix, so no real traffic is
  //    redirected.
  runConfigCli(tableName, kTestRuleName, "destination-ip", {"2001:db8::/32"});
  commitConfig();

  // 2. An IPv6 nexthop.
  runTrafficPolicyActionCli(
      kTestRuleName, {"redirect", "nexthop", "2001:db8::1"});
  commitConfig();
  expectRedirect("2001:db8::1");

  // 3. An IPv4 nexthop on the same rule replaces the first one rather than
  //    being added next to it.
  runTrafficPolicyActionCli(
      kTestRuleName, {"redirect", "nexthop", "192.0.2.1"});
  commitConfig();
  expectRedirect("192.0.2.1");

  // 4. Deleting the action removes it: the rule stays, the matchToAction and
  //    the redirect in switch state are gone, and the rule is enabled again.
  ASSERT_NO_FATAL_FAILURE(
      deleteActionAndVerify(tableName, "redirect", "redirectToNextHop"));
  for (const auto& entry : getStateEntries(tableName, kTestRuleName)) {
    const auto* enabled = entry.get_ptr("enabled");
    EXPECT_TRUE(enabled == nullptr || enabled->asBool())
        << "rule still disabled after its redirect was deleted";
  }
}

TEST_F(ConfigAclRuleTest, SetDestinationIp) {
  runSet("destination-ip", {"fc00::/64"}, [](auto& e) {
    EXPECT_EQ(e["dstIp"].asString(), "fc00::/64");
  });
}

TEST_F(ConfigAclRuleTest, SetProtocol) {
  runSet(
      "protocol", {"udp"}, [](auto& e) { EXPECT_EQ(e["proto"].asInt(), 17); });
}

// QueueMatchAction.queueId is not settable through `config acl rule ...
// action`; it lives on the rule's MatchAction in the data-plane traffic
// policy:
//   config data-plane traffic-policy match <rule> action send-to-queue <id>
// On a data-plane rule the id names a port egress queue (sendToCPU stays
// false). The CLI accepts 0-32767 but the ASIC only has a handful of port
// queues, so only small ids are committed.
TEST_F(ConfigAclRuleTest, SetActionSendToQueue) {
  auto tableName = targetTable();
  XLOG(INFO) << "[set-action-send-to-queue] " << tableName << "/"
             << kTestRuleName;

  // The running config holds exactly one matchToAction for the rule, with
  // sendToQueue.queueId == expected, and the switch state carries the same
  // queue as a port (not CPU) queue.
  auto expectQueue = [&](int expected) {
    EXPECT_EQ(countMatchToActions(kTestRuleName), 1)
        << "expected one matchToAction for the rule";
    auto action = getMatchAction(kTestRuleName);
    if (!action.has_value() || !action->count("sendToQueue")) {
      ADD_FAILURE() << "sendToQueue missing from running config";
      return;
    }
    EXPECT_EQ((*action)["sendToQueue"]["queueId"].asInt(), expected);

    auto entries = getStateEntries(tableName, kTestRuleName);
    if (entries.empty()) {
      ADD_FAILURE() << "rule not found in switch state";
      return;
    }
    for (const auto& entry : entries) {
      const auto* aclAction = entry.get_ptr("aclAction");
      const auto* queue =
          aclAction ? aclAction->get_ptr("sendToQueue") : nullptr;
      if (queue == nullptr) {
        ADD_FAILURE() << "sendToQueue missing from switch state";
        return;
      }
      EXPECT_EQ((*queue)["action"]["queueId"].asInt(), expected);
      EXPECT_FALSE((*queue)["sendToCPU"].asBool())
          << "data-plane send-to-queue must target a port queue";
    }
  };

  // 1. The rule matches a documentation prefix, so no real traffic is
  //    re-queued.
  runConfigCli(tableName, kTestRuleName, "destination-ip", {"2001:db8::/32"});
  commitConfig();

  // 2. send-to-queue 2.
  runTrafficPolicyActionCli(kTestRuleName, {"send-to-queue", "2"});
  commitConfig();
  expectQueue(2);

  // 3. A new id on the same rule overwrites it.
  runTrafficPolicyActionCli(kTestRuleName, {"send-to-queue", "5"});
  commitConfig();
  expectQueue(5);

  // 4. Deleting the action removes it: the rule stays, the matchToAction and
  //    the programmed queue are gone.
  deleteActionAndVerify(tableName, "send-to-queue", "sendToQueue");
}

TEST_F(ConfigAclRuleTest, SetSourcePort) {
  runSet(
      "source-port",
      {"12345"},
      [](auto& e) { EXPECT_EQ(e["l4SrcPort"].asInt(), 12345); },
      {{"protocol", {"tcp"}}});
}

TEST_F(ConfigAclRuleTest, SetDestinationPort) {
  runSet(
      "destination-port",
      {"443"},
      [](auto& e) { EXPECT_EQ(e["l4DstPort"].asInt(), 443); },
      {{"protocol", {"tcp"}}});
}

TEST_F(ConfigAclRuleTest, SetDscp) {
  runSet("dscp", {"46"}, [](auto& e) { EXPECT_EQ(e["dscp"].asInt(), 46); });
}

// SetDscpMatchAction.dscpValue is not settable through `config acl rule ...
// action`; it lives on the rule's MatchAction in the data-plane traffic
// policy:
//   config data-plane traffic-policy match <rule> action set-dscp <v>
// The CLI range 0-63 is the whole 6-bit DSCP space, so both ends are
// committed.
TEST_F(ConfigAclRuleTest, SetActionSetDscp) {
  auto tableName = targetTable();
  XLOG(INFO) << "[set-action-set-dscp] " << tableName << "/" << kTestRuleName;

  // The running config holds exactly one matchToAction for the rule, with
  // setDscp.dscpValue == expected, and the switch state carries the same
  // value.
  auto expectDscp = [&](int expected) {
    EXPECT_EQ(countMatchToActions(kTestRuleName), 1)
        << "expected one matchToAction for the rule";
    auto action = getMatchAction(kTestRuleName);
    if (!action.has_value() || !action->count("setDscp")) {
      ADD_FAILURE() << "setDscp missing from running config";
      return;
    }
    EXPECT_EQ((*action)["setDscp"]["dscpValue"].asInt(), expected);

    auto entries = getStateEntries(tableName, kTestRuleName);
    if (entries.empty()) {
      ADD_FAILURE() << "rule not found in switch state";
      return;
    }
    for (const auto& entry : entries) {
      const auto* aclAction = entry.get_ptr("aclAction");
      const auto* dscp = aclAction ? aclAction->get_ptr("setDscp") : nullptr;
      if (dscp == nullptr) {
        ADD_FAILURE() << "setDscp missing from switch state";
        return;
      }
      EXPECT_EQ((*dscp)["dscpValue"].asInt(), expected);
    }
  };

  // 1. The rule matches a documentation prefix, so no real traffic is
  //    re-marked.
  runConfigCli(tableName, kTestRuleName, "destination-ip", {"2001:db8::/32"});
  commitConfig();

  // 2. Lowest codepoint.
  runTrafficPolicyActionCli(kTestRuleName, {"set-dscp", "0"});
  commitConfig();
  expectDscp(0);

  // 3. Highest codepoint on the same rule overwrites it.
  runTrafficPolicyActionCli(kTestRuleName, {"set-dscp", "63"});
  commitConfig();
  expectDscp(63);

  // 4. Deleting the action removes it: the rule stays, the matchToAction and
  //    the programmed re-mark are gone.
  deleteActionAndVerify(tableName, "set-dscp", "setDscp");
}

TEST_F(ConfigAclRuleTest, SetTcpFlags) {
  runSet(
      "tcp-flags",
      {"0x12"},
      [](auto& e) { EXPECT_EQ(e["tcpFlagsBitMap"].asInt(), 0x12); },
      {{"protocol", {"tcp"}}});
}

TEST_F(ConfigAclRuleTest, SetIcmpType) {
  runSet(
      "icmp-type",
      {"8"},
      [](auto& e) { EXPECT_EQ(e["icmpType"].asInt(), 8); },
      {{"protocol", {"icmp"}}});
}

TEST_F(ConfigAclRuleTest, SetIcmpCode) {
  // icmp-code only makes sense alongside icmp-type (echo-reply / etc.) —
  // SAI rejects an entry that has icmp-code without icmp-type.
  runSet(
      "icmp-code",
      {"0"},
      [](auto& e) { EXPECT_EQ(e["icmpCode"].asInt(), 0); },
      {{"protocol", {"icmp"}}, {"icmp-type", {"8"}}});
}

// Ttl.mask is written together with Ttl.value on every `ttl` command: an
// explicit mask (decimal or hex) overwrites the previous one, and a `ttl`
// without a mask resets it to 0xFF. The mask is also checked in the agent's
// switch state (aclTableGroupMaps), not just in the running config.
TEST_F(ConfigAclRuleTest, SetTtlMask) {
  auto tableName = targetTable();
  XLOG(INFO) << "[set-ttl-mask] " << tableName << "/" << kTestRuleName;

  auto expectConfigTtl = [this, &tableName](int value, int mask) {
    auto rule = getRule(tableName, kTestRuleName);
    if (!rule.has_value()) {
      ADD_FAILURE() << "rule " << kTestRuleName << " not in running config";
      return;
    }
    EXPECT_EQ((*rule)["ttl"]["value"].asInt(), value);
    EXPECT_EQ((*rule)["ttl"]["mask"].asInt(), mask);
  };

  auto expectStateTtl = [this, &tableName](int value, int mask) {
    auto entries = getStateEntries(tableName, kTestRuleName);
    if (entries.empty()) {
      ADD_FAILURE() << "rule " << kTestRuleName << " not found in switch state";
      return;
    }
    for (const auto& entry : entries) {
      const auto* ttl = entry.get_ptr("ttl");
      if (ttl == nullptr) {
        ADD_FAILURE() << "switch state entry has no ttl";
        return;
      }
      EXPECT_EQ((*ttl)["value"].asInt(), value);
      EXPECT_EQ((*ttl)["mask"].asInt(), mask);
    }
  };

  // Hex mask: match TTLs 64-79 (upper nibble 0x4).
  runConfigCli(tableName, kTestRuleName, "ttl", {"64", "0xF0"});
  commitConfig();
  expectConfigTtl(64, 0xF0);
  expectStateTtl(64, 0xF0);

  // New decimal mask on the same rule overwrites the old one.
  runConfigCli(tableName, kTestRuleName, "ttl", {"64", "192"});
  commitConfig();
  expectConfigTtl(64, 192);
  expectStateTtl(64, 192);

  // Omitting the mask resets it to the exact-match default.
  runConfigCli(tableName, kTestRuleName, "ttl", {"64"});
  commitConfig();
  expectConfigTtl(64, 0xFF);
  expectStateTtl(64, 0xFF);
}

TEST_F(ConfigAclRuleTest, SetIpFragment) {
  // cfg::IpFragMatch::MATCH_ANY_FRAGMENT = 4
  runSet("ip-fragment", {"any-fragment"}, [](auto& e) {
    EXPECT_EQ(e["ipFrag"].asInt(), 4);
  });
}

TEST_F(ConfigAclRuleTest, SetTtlDefaultMask) {
  runSet("ttl", {"64"}, [](auto& e) {
    EXPECT_EQ(e["ttl"]["value"].asInt(), 64);
    EXPECT_EQ(e["ttl"]["mask"].asInt(), 0xFF);
  });
}

TEST_F(ConfigAclRuleTest, SetTtlExplicitMask) {
  runSet("ttl", {"64", "240"}, [](auto& e) {
    EXPECT_EQ(e["ttl"]["value"].asInt(), 64);
    EXPECT_EQ(e["ttl"]["mask"].asInt(), 240);
  });
}

// Ttl.value is a scalar: a second `ttl` on the same rule overwrites the first
// in place. The value is also checked in the agent's switch state
// (aclTableGroupMaps), not just in the running config.
TEST_F(ConfigAclRuleTest, SetTtlValue) {
  auto tableName = targetTable();
  XLOG(INFO) << "[set-ttl-value] " << tableName << "/" << kTestRuleName;

  auto countRules = [this, &tableName]() {
    int count = 0;
    auto config = getRunningConfig();
    for (const auto& group : config["sw"]["aclTableGroups"]) {
      if (!group.count("aclTables")) {
        continue;
      }
      for (const auto& table : group["aclTables"]) {
        if (table["name"].asString() != tableName) {
          continue;
        }
        for (const auto& entry : table["aclEntries"]) {
          if (entry["name"].asString() == kTestRuleName) {
            ++count;
          }
        }
      }
    }
    return count;
  };

  auto expectStateTtl = [this, &tableName](int expectedValue) {
    auto entries = getStateEntries(tableName, kTestRuleName);
    if (entries.empty()) {
      ADD_FAILURE() << "rule " << kTestRuleName << " not found in switch state";
      return;
    }
    for (const auto& entry : entries) {
      const auto* ttl = entry.get_ptr("ttl");
      if (ttl == nullptr) {
        ADD_FAILURE() << "switch state entry has no ttl";
        return;
      }
      EXPECT_EQ((*ttl)["value"].asInt(), expectedValue);
      EXPECT_EQ((*ttl)["mask"].asInt(), 0xFF);
    }
  };

  runConfigCli(tableName, kTestRuleName, "ttl", {"1"});
  commitConfig();
  auto rule = getRule(tableName, kTestRuleName);
  ASSERT_TRUE(rule.has_value());
  EXPECT_EQ((*rule)["ttl"]["value"].asInt(), 1);
  EXPECT_EQ((*rule)["ttl"]["mask"].asInt(), 0xFF);
  expectStateTtl(1);

  // Overwrite on the same rule: the value changes, no second rule appears.
  runConfigCli(tableName, kTestRuleName, "ttl", {"255"});
  commitConfig();
  rule = getRule(tableName, kTestRuleName);
  ASSERT_TRUE(rule.has_value());
  EXPECT_EQ((*rule)["ttl"]["value"].asInt(), 255);
  EXPECT_EQ((*rule)["ttl"]["mask"].asInt(), 0xFF);
  EXPECT_EQ(countRules(), 1);
  expectStateTtl(255);
}

TEST_F(ConfigAclRuleTest, SetDestinationMac) {
  runSet("destination-mac", {"aa:bb:cc:dd:ee:ff"}, [](auto& e) {
    EXPECT_EQ(e["dstMac"].asString(), "aa:bb:cc:dd:ee:ff");
  });
}

TEST_F(ConfigAclRuleTest, SetIpType) {
  // cfg::IpType::IP6 = 3
  runSet(
      "ip-type", {"ipv6"}, [](auto& e) { EXPECT_EQ(e["ipType"].asInt(), 3); });
}

// SetTcAction.tcValue is not settable through `config acl rule ... action`;
// it lives on the rule's MatchAction in the data-plane traffic policy:
//   config data-plane traffic-policy match <rule> action set-tc <v>
// The CLI accepts 0-127 (a thrift byte) and leaves the per-ASIC cap to the
// agent, so the test commits 0 and 7, which every ASIC supports. The agent
// copies setTc into the ACL entry only when it runs with
// --sai_user_defined_trap (ApplyThriftConfig.cpp), so the test reads that flag
// from the agent and checks the switch state for whichever way it runs.
TEST_F(ConfigAclRuleTest, SetActionSetTc) {
  auto tableName = targetTable();
  XLOG(INFO) << "[set-action-set-tc] " << tableName << "/" << kTestRuleName;

  std::string trapFlag;
  try {
    trapFlag = getAgentOption("sai_user_defined_trap");
  } catch (const std::exception& e) {
    FAIL() << "could not read --sai_user_defined_trap from the agent: "
           << e.what();
  }
  const bool userDefinedTrap = trapFlag == "true";
  XLOG(INFO) << "agent --sai_user_defined_trap=" << trapFlag;

  // The running config holds exactly one matchToAction for the rule, with
  // setTc.tcValue == expected. In switch state the rule carries setTc with the
  // same value when --sai_user_defined_trap is on, and no setTc when it is off.
  auto expectTc = [&](int expected) {
    EXPECT_EQ(countMatchToActions(kTestRuleName), 1)
        << "expected one matchToAction for the rule";
    auto action = getMatchAction(kTestRuleName);
    if (!action.has_value() || !action->count("setTc")) {
      ADD_FAILURE() << "setTc missing from running config";
      return;
    }
    EXPECT_EQ((*action)["setTc"]["tcValue"].asInt(), expected);

    auto entries = getStateEntries(tableName, kTestRuleName);
    if (entries.empty()) {
      ADD_FAILURE() << "rule not found in switch state";
      return;
    }
    for (const auto& entry : entries) {
      const auto* aclAction = entry.get_ptr("aclAction");
      const auto* setTc = aclAction ? aclAction->get_ptr("setTc") : nullptr;
      if (!userDefinedTrap) {
        EXPECT_EQ(setTc, nullptr)
            << "setTc in switch state with --sai_user_defined_trap off";
        continue;
      }
      if (setTc == nullptr) {
        ADD_FAILURE() << "setTc missing from switch state";
        return;
      }
      EXPECT_EQ((*setTc)["action"]["tcValue"].asInt(), expected);
      EXPECT_FALSE((*setTc)["sendToCPU"].asBool())
          << "data-plane rule marked as a CPU policy";
    }
  };

  // 1. The rule matches a documentation prefix, so no real traffic is
  //    reclassified.
  runConfigCli(tableName, kTestRuleName, "destination-ip", {"2001:db8::/32"});
  commitConfig();

  // 2. Lowest traffic class.
  runTrafficPolicyActionCli(kTestRuleName, {"set-tc", "0"});
  commitConfig();
  expectTc(0);

  // 3. Highest traffic class common to all ASICs, on the same rule,
  //    overwrites it.
  runTrafficPolicyActionCli(kTestRuleName, {"set-tc", "7"});
  commitConfig();
  expectTc(7);

  // 4. Deleting the action removes it: the rule stays, the matchToAction and
  //    the programmed traffic class are gone.
  deleteActionAndVerify(tableName, "set-tc", "setTc");
}

TEST_F(ConfigAclRuleTest, SetPacketLookupResult) {
  runSet("packet-lookup-result", {"mpls-no-match"}, [](auto& e) {
    EXPECT_EQ(e["packetLookupResult"].asInt(), 1);
  });
}

// MatchAction.counter is not settable through `config acl rule ... action`;
// it lives on the rule's MatchAction in the data-plane traffic policy:
//   config data-plane traffic-policy match <rule> action counter <n>
// The agent rejects a counter name that has no sw.trafficCounters entry, so
// the test creates one with `config traffic-counter` and deletes it again at
// the end.
TEST_F(ConfigAclRuleTest, SetActionCounter) {
  auto tableName = targetTable();
  // The SAI ACL counter label holds at most 31 characters, and the hw agent
  // aborts on a longer name, so keep this well under that (21 characters).
  const std::string counterName = uniqueName("e2e_ctr_");
  XLOG(INFO) << "[set-action-counter] " << tableName << "/" << kTestRuleName
             << " -> " << counterName;

  // Removes the rule (which also drops its matchToAction) and the counter in
  // one commit. Runs even when an assertion below fails, so the counter never
  // outlives the test.
  auto cleanup = folly::makeGuard([this, &tableName, &counterName] {
    discardSession();
    auto delRule = runCli({"delete", "acl", "rule", tableName, kTestRuleName});
    auto delCounter = runCli({"delete", "traffic-counter", counterName});
    if (delRule.exitCode != 0 && delCounter.exitCode != 0) {
      return; // nothing was committed, nothing to undo
    }
    auto commit = runCli({"config", "session", "commit"});
    EXPECT_EQ(commit.exitCode, 0) << "cleanup commit failed: " << commit.stderr;
  });

  // 1. Create the counter and the rule (documentation prefix, so no real
  //    traffic matches).
  auto create =
      runCli({"config", "traffic-counter", counterName, "PACKETS,BYTES"});
  ASSERT_EQ(create.exitCode, 0)
      << "config traffic-counter failed: " << create.stderr;
  runConfigCli(tableName, kTestRuleName, "destination-ip", {"2001:db8::/32"});
  commitConfig();

  // 2. Attach the counter to the rule.
  runTrafficPolicyActionCli(kTestRuleName, {"counter", counterName});
  commitConfig();

  auto action = getMatchAction(kTestRuleName);
  ASSERT_TRUE(action.has_value()) << "matchToAction missing after set";
  ASSERT_TRUE(action->count("counter"));
  EXPECT_EQ((*action)["counter"].asString(), counterName);
  for (const auto* other :
       {"sendToQueue", "setDscp", "ingressMirror", "egressMirror"}) {
    EXPECT_FALSE(action->count(other))
        << "action counter must not set " << other;
  }

  // The agent resolves the name to the full TrafficCounter (name + types).
  auto entries = getStateEntries(tableName, kTestRuleName);
  ASSERT_FALSE(entries.empty()) << "rule not found in switch state";
  for (const auto& entry : entries) {
    const auto* aclAction = entry.get_ptr("aclAction");
    ASSERT_NE(aclAction, nullptr) << "switch state entry has no aclAction";
    const auto* counter = aclAction->get_ptr("trafficCounter");
    ASSERT_NE(counter, nullptr) << "switch state aclAction has no counter";
    EXPECT_EQ((*counter)["name"].asString(), counterName);
    std::vector<int64_t> types;
    for (const auto& t : (*counter)["types"]) {
      types.push_back(t.asInt());
    }
    EXPECT_THAT(
        types, ::testing::UnorderedElementsAre(0 /* PACKETS */, 1 /* BYTES */));
  }

  // 3. Delete the action: the rule stays, the counter action is gone.
  deleteActionAndVerify(tableName, "counter", "trafficCounter");
}

// Class ids are given the same way as to `config interface <intf>
// lookup-class`: a numeric id or a class name.
TEST_F(ConfigAclRuleTest, SetLookupClassL2) {
  runSet("lookup-class-l2", {"10"}, [](auto& e) {
    EXPECT_EQ(e["lookupClassL2"].asInt(), 10);
  });
}

TEST_F(ConfigAclRuleTest, SetLookupClassNeighbor) {
  runSet(
      "lookup-class-neighbor", {"CLASS_QUEUE_PER_HOST_QUEUE_5"}, [](auto& e) {
        EXPECT_EQ(e["lookupClassNeighbor"].asInt(), 15);
      });
}

TEST_F(ConfigAclRuleTest, SetLookupClassRoute) {
  runSet("lookup-class-route", {"13"}, [](auto& e) {
    EXPECT_EQ(e["lookupClassRoute"].asInt(), 13);
  });
}

// MatchAction.egressMirror is not settable through `config acl rule ...
// action`; it lives on the rule's MatchAction in the data-plane traffic
// policy:
//   config data-plane traffic-policy match <rule> action mirror-egress <n>
// The test adds its own SPAN mirror (stageSpanMirror) and removes it again at
// the end.
TEST_F(ConfigAclRuleTest, SetActionMirrorEgress) {
  auto tableName = targetTable();
  const std::string mirrorName = uniqueName("cli_e2e_test_mirror_");
  XLOG(INFO) << "[set-action-mirror-egress] " << tableName << "/"
             << kTestRuleName << " -> " << mirrorName;

  // Runs even when an assertion below fails, so the mirror never outlives the
  // test.
  auto cleanup = folly::makeGuard([this, &tableName, &mirrorName] {
    deleteRuleAndMirror(tableName, mirrorName);
  });

  // 1. Stage the rule (documentation prefix, so no real traffic matches) and
  //    the mirror.
  runConfigCli(tableName, kTestRuleName, "destination-ip", {"2001:db8::/32"});
  if (!stageSpanMirror(mirrorName)) {
    GTEST_SKIP() << "no enabled, unmirrored port to use as mirror destination";
  }
  commitConfig();

  // 2. Attach the mirror to the rule.
  runTrafficPolicyActionCli(kTestRuleName, {"mirror-egress", mirrorName});
  commitConfig();

  auto action = getMatchAction(kTestRuleName);
  ASSERT_TRUE(action.has_value()) << "matchToAction missing after set";
  ASSERT_TRUE(action->count("egressMirror"));
  EXPECT_EQ((*action)["egressMirror"].asString(), mirrorName);
  EXPECT_FALSE(action->count("ingressMirror"))
      << "mirror-egress must not set ingressMirror";

  auto entries = getStateEntries(tableName, kTestRuleName);
  ASSERT_FALSE(entries.empty()) << "rule not found in switch state";
  for (const auto& entry : entries) {
    const auto* aclAction = entry.get_ptr("aclAction");
    ASSERT_NE(aclAction, nullptr) << "switch state entry has no aclAction";
    ASSERT_TRUE(aclAction->count("egressMirror"));
    EXPECT_EQ((*aclAction)["egressMirror"].asString(), mirrorName);
    EXPECT_FALSE(aclAction->count("ingressMirror"));
  }

  // 3. Delete the action: the rule stays, the mirror action is gone.
  deleteActionAndVerify(tableName, "mirror-egress", "egressMirror");
}

// =============================================================
// Action tests — `config acl rule <table> <rule> action <subattr>`
//
// The only actions this command takes are the three that mutate
// AclEntry.actionType in place. Each test creates the entry on demand by
// setting a neutral match field (dscp), then exercises the action. Richer
// actions belong to a traffic policy and are covered by
// ConfigCoppTrafficPolicyTest / the data-plane traffic-policy unit tests.
// =============================================================

TEST_F(ConfigAclRuleTest, SetActionPermit) {
  runActionSet("permit", {}, [this](const std::string& table) {
    auto e = getRule(table, kTestRuleName);
    ASSERT_TRUE(e.has_value());
    // PERMIT == 1
    EXPECT_EQ((*e)["actionType"].asInt(), 1);
  });
}

TEST_F(ConfigAclRuleTest, SetActionDeny) {
  runActionSet("deny", {}, [this](const std::string& table) {
    auto e = getRule(table, kTestRuleName);
    ASSERT_TRUE(e.has_value());
    // DENY == 0
    EXPECT_EQ((*e)["actionType"].asInt(), 0);
  });
}

// MatchAction.ingressMirror is not settable through `config acl rule ...
// action`; it lives on the rule's MatchAction in the data-plane traffic
// policy:
//   config data-plane traffic-policy match <rule> action mirror-ingress <n>
// The test adds its own SPAN mirror (stageSpanMirror) and removes it again at
// the end.
TEST_F(ConfigAclRuleTest, SetActionMirrorIngress) {
  auto tableName = targetTable();
  const std::string mirrorName = uniqueName("cli_e2e_test_mirror_");
  XLOG(INFO) << "[set-action-mirror-ingress] " << tableName << "/"
             << kTestRuleName << " -> " << mirrorName;

  // Runs even when an assertion below fails, so the mirror never outlives the
  // test.
  auto cleanup = folly::makeGuard([this, &tableName, &mirrorName] {
    deleteRuleAndMirror(tableName, mirrorName);
  });

  // 1. Stage the rule (documentation prefix, so no real traffic matches) and
  //    the mirror.
  runConfigCli(tableName, kTestRuleName, "destination-ip", {"2001:db8::/32"});
  if (!stageSpanMirror(mirrorName)) {
    GTEST_SKIP() << "no enabled, unmirrored port to use as mirror destination";
  }
  commitConfig();

  // 2. Attach the mirror to the rule.
  runTrafficPolicyActionCli(kTestRuleName, {"mirror-ingress", mirrorName});
  commitConfig();

  auto action = getMatchAction(kTestRuleName);
  ASSERT_TRUE(action.has_value()) << "matchToAction missing after set";
  ASSERT_TRUE(action->count("ingressMirror"));
  EXPECT_EQ((*action)["ingressMirror"].asString(), mirrorName);
  EXPECT_FALSE(action->count("egressMirror"))
      << "mirror-ingress must not set egressMirror";

  auto entries = getStateEntries(tableName, kTestRuleName);
  ASSERT_FALSE(entries.empty()) << "rule not found in switch state";
  for (const auto& entry : entries) {
    const auto* aclAction = entry.get_ptr("aclAction");
    ASSERT_NE(aclAction, nullptr) << "switch state entry has no aclAction";
    ASSERT_TRUE(aclAction->count("ingressMirror"));
    EXPECT_EQ((*aclAction)["ingressMirror"].asString(), mirrorName);
    EXPECT_FALSE(aclAction->count("egressMirror"));
  }

  // 3. Delete the action: the rule stays, the mirror action is gone.
  deleteActionAndVerify(tableName, "mirror-ingress", "ingressMirror");
}

// =============================================================
// Delete test — `delete acl rule <table> <rule>`
// =============================================================

TEST_F(ConfigAclRuleTest, DeleteRule) {
  auto tableName = targetTable();
  XLOG(INFO) << "[delete-rule] " << tableName << "/" << kTestRuleName;

  runConfigCli(tableName, kTestRuleName, "dscp", {"46"});
  commitConfig();
  ASSERT_TRUE(getRule(tableName, kTestRuleName).has_value())
      << "rule missing after upsert";

  runDeleteCli(tableName, kTestRuleName);
  commitConfig();
  EXPECT_FALSE(getRule(tableName, kTestRuleName).has_value())
      << "rule still present after delete";
}

// Delete must also drop the MatchToAction attached via
// dataPlaneTrafficPolicy.matchToAction. Without that cleanup the second
// commit below would fail with "Invalid config: No acl named X found"
// (ApplyThriftConfig.checkTrafficPolicyAclsExistInConfig).
TEST_F(ConfigAclRuleTest, DeleteRuleWithAction) {
  auto tableName = targetTable();
  XLOG(INFO) << "[delete-rule-with-action] " << tableName << "/"
             << kTestRuleName;

  runConfigCli(tableName, kTestRuleName, "dscp", {"46"});
  runTrafficPolicyActionCli(kTestRuleName, {"set-dscp", "32"});
  commitConfig();
  ASSERT_TRUE(getRule(tableName, kTestRuleName).has_value())
      << "rule missing after upsert";
  ASSERT_TRUE(getMatchAction(kTestRuleName).has_value())
      << "matchToAction missing after action set";

  runDeleteCli(tableName, kTestRuleName);
  commitConfig();
  EXPECT_FALSE(getRule(tableName, kTestRuleName).has_value())
      << "rule still present after delete";
  EXPECT_FALSE(getMatchAction(kTestRuleName).has_value())
      << "matchToAction still present after rule delete";
}

// MatchAction.toCpuAction is not settable through `config acl rule ...
// action`; it lives on the rule's MatchAction in the data-plane traffic
// policy:
//   config data-plane traffic-policy match <rule> action trap-to-cpu
//   config data-plane traffic-policy match <rule> action copy-to-cpu
// Both keywords write the same scalar field (TRAP = 1, COPY = 0), so the
// second overwrites the first.
TEST_F(ConfigAclRuleTest, SetActionToCpu) {
  auto tableName = targetTable();
  XLOG(INFO) << "[set-action-to-cpu] " << tableName << "/" << kTestRuleName;

  constexpr int kCopy = 0;
  constexpr int kTrap = 1;

  // The running config holds exactly one matchToAction for the rule, with
  // toCpuAction == expected, and the switch state carries the same value.
  auto expectToCpu = [&](int expected) {
    EXPECT_EQ(countMatchToActions(kTestRuleName), 1)
        << "expected one matchToAction for the rule";
    auto action = getMatchAction(kTestRuleName);
    if (!action.has_value() || !action->count("toCpuAction")) {
      ADD_FAILURE() << "toCpuAction missing from running config";
      return;
    }
    EXPECT_EQ((*action)["toCpuAction"].asInt(), expected);

    auto entries = getStateEntries(tableName, kTestRuleName);
    if (entries.empty()) {
      ADD_FAILURE() << "rule not found in switch state";
      return;
    }
    for (const auto& entry : entries) {
      const auto* aclAction = entry.get_ptr("aclAction");
      if (aclAction == nullptr || !aclAction->count("toCpuAction")) {
        ADD_FAILURE() << "toCpuAction missing from switch state";
        return;
      }
      EXPECT_EQ((*aclAction)["toCpuAction"].asInt(), expected);
    }
  };

  // 1. The rule matches a documentation prefix, so no real traffic is sent to
  //    the CPU.
  runConfigCli(tableName, kTestRuleName, "destination-ip", {"2001:db8::/32"});
  commitConfig();

  // 2. trap-to-cpu.
  runTrafficPolicyActionCli(kTestRuleName, {"trap-to-cpu"});
  commitConfig();
  expectToCpu(kTrap);

  // 3. copy-to-cpu on the same rule overwrites it.
  runTrafficPolicyActionCli(kTestRuleName, {"copy-to-cpu"});
  commitConfig();
  expectToCpu(kCopy);

  // 4. Deleting copy-to-cpu removes the action: the rule stays, the
  //    matchToAction and the programmed toCpuAction are gone.
  deleteActionAndVerify(tableName, "copy-to-cpu", "toCpuAction");
}
