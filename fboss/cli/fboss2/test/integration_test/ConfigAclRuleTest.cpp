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

#include <folly/json/dynamic.h>
#include <folly/logging/xlog.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>
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

TEST_F(ConfigAclRuleTest, SetDestinationIp) {
  runSet("destination-ip", {"fc00::/64"}, [](auto& e) {
    EXPECT_EQ(e["dstIp"].asString(), "fc00::/64");
  });
}

TEST_F(ConfigAclRuleTest, SetProtocol) {
  runSet(
      "protocol", {"udp"}, [](auto& e) { EXPECT_EQ(e["proto"].asInt(), 17); });
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

TEST_F(ConfigAclRuleTest, SetPacketLookupResult) {
  runSet("packet-lookup-result", {"mpls-no-match"}, [](auto& e) {
    EXPECT_EQ(e["packetLookupResult"].asInt(), 1);
  });
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
