// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/commands/config/qos/policy/CmdConfigQosPolicy.h"
#include "fboss/cli/fboss2/commands/config/qos/policy/CmdConfigQosPolicyMap.h"
#include "fboss/cli/fboss2/commands/delete/qos/policy/CmdDeleteQosPolicyMap.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

// "p" carries a DSCP map where tc 0 holds several codepoints, tc 1 holds a
// single codepoint plus an egress rewrite, and tc 3 holds a single codepoint
// with no rewrite. "no-pcp" has no pcpMaps field at all.
static const std::string kSeedConfig = R"({
  "sw": {
    "qosPolicies": [
      {
        "name": "p",
        "rules": [],
        "qosMap": {
          "dscpMaps": [
            {"internalTrafficClass": 0, "fromDscpToTrafficClass": [0, 1, 2, 9]},
            {"internalTrafficClass": 1, "fromDscpToTrafficClass": [8], "fromTrafficClassToDscp": 8},
            {"internalTrafficClass": 3, "fromDscpToTrafficClass": [18]},
            {"internalTrafficClass": 5, "fromDscpToTrafficClass": [40, 18]}
          ],
          "expMaps": [
            {"internalTrafficClass": 0, "fromExpToTrafficClass": [0, 5]}
          ],
          "pcpMaps": [
            {"internalTrafficClass": 0, "fromPcpToTrafficClass": [0, 6]}
          ],
          "trafficClassToQueueId": {"0": 0}
        }
      },
      {
        "name": "no-pcp",
        "rules": [],
        "qosMap": {
          "dscpMaps": [],
          "expMaps": [],
          "trafficClassToQueueId": {"0": 0}
        }
      }
    ]
  }
})";

class CmdConfigQosPolicyMapTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigQosPolicyMapTestFixture()
      : CmdConfigTestBase(
            "fboss_cfg_qos_policy_map_test_%%%%-%%%%-%%%%-%%%%",
            kSeedConfig) {}

 protected:
  std::string configureMap(
      const std::string& policy,
      const std::vector<std::string>& args) {
    setupTestableConfigSession("config qos policy map", "");
    auto cmd = CmdConfigQosPolicyMap();
    return cmd.queryClient(
        localhost(), QosPolicyName({policy}), QosMapConfig(args));
  }

  static const cfg::QosMap& qosMap(const std::string& name) {
    auto& policies =
        *ConfigSession::getInstance().getAgentConfig().sw()->qosPolicies();
    for (const auto& policy : policies) {
      if (*policy.name() == name) {
        return *policy.qosMap();
      }
    }
    throw std::runtime_error("policy not found: " + name);
  }

  // Traffic classes whose ingress list carries `dscp`, in entry order.
  static std::vector<int16_t> dscpOwners(const std::string& name, int8_t dscp) {
    std::vector<int16_t> owners;
    for (const auto& e : *qosMap(name).dscpMaps()) {
      const auto& from = *e.fromDscpToTrafficClass();
      if (std::find(from.begin(), from.end(), dscp) != from.end()) {
        owners.push_back(*e.internalTrafficClass());
      }
    }
    return owners;
  }

  std::vector<int16_t> dscpOwnersAfterSetup(
      const std::string& name,
      int8_t dscp) {
    setupTestableConfigSession("config qos policy map", "");
    return dscpOwners(name, dscp);
  }

  static const cfg::DscpQosMap* dscpEntry(
      const std::string& name,
      int16_t trafficClass) {
    for (const auto& e : *qosMap(name).dscpMaps()) {
      if (*e.internalTrafficClass() == trafficClass) {
        return &e;
      }
    }
    return nullptr;
  }
};

using ::testing::ElementsAre;

TEST_F(CmdConfigQosPolicyMapTestFixture, moveDscpBetweenTrafficClasses) {
  configureMap("p", {"dscp", "9", "traffic-class", "2"});

  EXPECT_THAT(dscpOwners("p", 9), ElementsAre(2));
  // the source class keeps its other codepoints, in order
  EXPECT_THAT(
      *dscpEntry("p", 0)->fromDscpToTrafficClass(), ElementsAre(0, 1, 2));
}

TEST_F(CmdConfigQosPolicyMapTestFixture, moveDscpTwiceLeavesOneOwner) {
  // The field scenario: tc 0 -> tc 2 -> tc 4.
  configureMap("p", {"dscp", "9", "traffic-class", "2"});
  configureMap("p", {"dscp", "9", "traffic-class", "4"});

  EXPECT_THAT(dscpOwners("p", 9), ElementsAre(4));
  // tc 2 held nothing but the moved codepoint, so it is gone
  EXPECT_EQ(dscpEntry("p", 2), nullptr);
}

TEST_F(CmdConfigQosPolicyMapTestFixture, readdDscpToSameClassIsNoop) {
  setupTestableConfigSession("config qos policy map", "");
  auto before = qosMap("p");
  CmdConfigQosPolicyMap().queryClient(
      localhost(),
      QosPolicyName({"p"}),
      QosMapConfig({"dscp", "9", "traffic-class", "0"}));
  EXPECT_EQ(qosMap("p"), before);
}

TEST_F(CmdConfigQosPolicyMapTestFixture, moveLastDscpDropsEmptyEntry) {
  configureMap("p", {"dscp", "18", "traffic-class", "0"});

  EXPECT_THAT(dscpOwners("p", 18), ElementsAre(0));
  EXPECT_EQ(dscpEntry("p", 3), nullptr);
  EXPECT_THAT(*dscpEntry("p", 5)->fromDscpToTrafficClass(), ElementsAre(40));
}

TEST_F(CmdConfigQosPolicyMapTestFixture, readdHealsPreexistingDuplicate) {
  // The seed carries 18 under tc 3 and tc 5, as the pre-fix CLI could
  // produce. Asserting one of the two mappings must drop the other.
  ASSERT_THAT(dscpOwnersAfterSetup("p", 18), ElementsAre(3, 5));
  configureMap("p", {"dscp", "18", "traffic-class", "5"});

  EXPECT_THAT(dscpOwners("p", 18), ElementsAre(5));
  EXPECT_EQ(dscpEntry("p", 3), nullptr);
}

TEST_F(CmdConfigQosPolicyMapTestFixture, moveLastDscpKeepsEntryWithEgress) {
  configureMap("p", {"dscp", "8", "traffic-class", "0"});

  EXPECT_THAT(dscpOwners("p", 8), ElementsAre(0));
  const auto* tc1 = dscpEntry("p", 1);
  ASSERT_NE(tc1, nullptr);
  EXPECT_TRUE(tc1->fromDscpToTrafficClass()->empty());
  EXPECT_EQ(tc1->fromTrafficClassToDscp().value_or(-1), 8);
}

TEST_F(CmdConfigQosPolicyMapTestFixture, egressRewriteIsNotMoved) {
  // Two classes may legitimately rewrite to the same DSCP.
  configureMap("p", {"traffic-class", "0", "dscp", "8"});

  EXPECT_EQ(dscpEntry("p", 0)->fromTrafficClassToDscp().value_or(-1), 8);
  EXPECT_EQ(dscpEntry("p", 1)->fromTrafficClassToDscp().value_or(-1), 8);
  // and the ingress side is untouched
  EXPECT_THAT(dscpOwners("p", 8), ElementsAre(1));
}

TEST_F(CmdConfigQosPolicyMapTestFixture, moveMplsExpBetweenTrafficClasses) {
  configureMap("p", {"mpls-exp", "5", "traffic-class", "7"});

  const auto& expMaps = *qosMap("p").expMaps();
  ASSERT_EQ(expMaps.size(), 2);
  EXPECT_THAT(*expMaps[0].fromExpToTrafficClass(), ElementsAre(0));
  EXPECT_EQ(*expMaps[1].internalTrafficClass(), 7);
  EXPECT_THAT(*expMaps[1].fromExpToTrafficClass(), ElementsAre(5));
}

TEST_F(CmdConfigQosPolicyMapTestFixture, moveDot1pBetweenTrafficClasses) {
  configureMap("p", {"dot1p", "6", "traffic-class", "7"});

  const auto& pcpMaps = *qosMap("p").pcpMaps();
  ASSERT_EQ(pcpMaps.size(), 2);
  EXPECT_THAT(*pcpMaps[0].fromPcpToTrafficClass(), ElementsAre(0));
  EXPECT_EQ(*pcpMaps[1].internalTrafficClass(), 7);
  EXPECT_THAT(*pcpMaps[1].fromPcpToTrafficClass(), ElementsAre(6));
}

TEST_F(CmdConfigQosPolicyMapTestFixture, dot1pCreatesPcpMapsWhenUnset) {
  configureMap("no-pcp", {"dot1p", "3", "traffic-class", "1"});

  const auto& pcpMaps = qosMap("no-pcp").pcpMaps();
  ASSERT_TRUE(pcpMaps.has_value());
  ASSERT_EQ(pcpMaps->size(), 1);
  EXPECT_THAT(*(*pcpMaps)[0].fromPcpToTrafficClass(), ElementsAre(3));
}

TEST_F(CmdConfigQosPolicyMapTestFixture, addThenDeleteAgreeOnOwnership) {
  // add and delete share the detach helper; moving then deleting must leave
  // the codepoint nowhere, without touching the other classes.
  configureMap("p", {"dscp", "9", "traffic-class", "2"});

  setupTestableConfigSession("delete qos policy map", "dscp 9");
  CmdDeleteQosPolicyMap().queryClient(
      localhost(), QosPolicyName({"p"}), DeleteQosMapEntry({"dscp", "9"}));

  EXPECT_TRUE(dscpOwners("p", 9).empty());
  EXPECT_EQ(dscpEntry("p", 2), nullptr);
  EXPECT_THAT(
      *dscpEntry("p", 0)->fromDscpToTrafficClass(), ElementsAre(0, 1, 2));
}

} // namespace facebook::fboss
