// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

#include "fboss/cli/fboss2/commands/delete/qos/policy/CmdDeleteQosPolicy.h"
#include "fboss/cli/fboss2/commands/delete/qos/policy/CmdDeleteQosPolicyMap.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

// Two policies: "unreferenced" is free to delete, "in-use" is named by
// dataPlaneTrafficPolicy.defaultQosPolicy so the delete must refuse it.
// dscpMaps groups codepoints under an internalTrafficClass, matching the
// shape CmdConfigQosPolicyMap writes.
static const std::string kSeedConfig = R"({
  "sw": {
    "qosPolicies": [
      {
        "name": "unreferenced",
        "rules": [],
        "qosMap": {
          "dscpMaps": [
            {"internalTrafficClass": 0, "fromDscpToTrafficClass": [0, 1, 2]},
            {"internalTrafficClass": 1, "fromDscpToTrafficClass": [8], "fromTrafficClassToDscp": 8}
          ],
          "expMaps": [
            {"internalTrafficClass": 2, "fromExpToTrafficClass": [5]}
          ],
          "pcpMaps": [
            {"internalTrafficClass": 3, "fromPcpToTrafficClass": [6]}
          ],
          "trafficClassToQueueId": {"0": 0, "1": 1, "2": 2},
          "pfcPriorityToQueueId": {"3": 3},
          "trafficClassToPgId": {"4": 4, "5": 5},
          "pfcPriorityToPgId": {"6": 6}
        }
      },
      {
        "name": "in-use",
        "rules": [],
        "qosMap": {
          "dscpMaps": [],
          "expMaps": [],
          "trafficClassToQueueId": {"0": 0}
        }
      },
      {"name": "port-ref", "rules": []},
      {"name": "cpu-ref", "rules": []},
      {"name": "no-map", "rules": []}
    ],
    "dataPlaneTrafficPolicy": {
      "defaultQosPolicy": "in-use",
      "portIdToQosPolicy": {"1": "port-ref"}
    },
    "cpuTrafficPolicy": {
      "trafficPolicy": {
        "defaultQosPolicy": "cpu-ref"
      }
    }
  }
})";

class CmdDeleteQosPolicyTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteQosPolicyTestFixture()
      : CmdConfigTestBase(
            "fboss_del_qos_policy_test_%%%%-%%%%-%%%%-%%%%",
            kSeedConfig) {}

 protected:
  static const cfg::QosPolicy* findPolicy(const std::string& name) {
    const auto& policies =
        *ConfigSession::getInstance().getAgentConfig().sw()->qosPolicies();
    for (const auto& policy : policies) {
      if (*policy.name() == name) {
        return &policy;
      }
    }
    return nullptr;
  }

  static const cfg::DscpQosMap* findDscpEntry(
      const std::string& policyName,
      int16_t trafficClass) {
    const auto* policy = findPolicy(policyName);
    if (policy == nullptr || !policy->qosMap().has_value()) {
      return nullptr;
    }
    for (const auto& entry : *policy->qosMap()->dscpMaps()) {
      if (*entry.internalTrafficClass() == trafficClass) {
        return &entry;
      }
    }
    return nullptr;
  }
};

// ---------------------------------------------------------------- arg parsing

// The name type is shared with `config qos policy`; arity is enforced by
// CLI11 (required/expected(1)), so only the empty-name guard is ours.
TEST_F(CmdDeleteQosPolicyTestFixture, policyNameArgValidation) {
  EXPECT_EQ(QosPolicyName({"p1"}).getName(), "p1");
  EXPECT_EQ(QosPolicyName({}).getName(), "");

  setupTestableConfigSession("delete qos policy", "");
  auto cmd = CmdDeleteQosPolicy();
  EXPECT_THROW(
      cmd.queryClient(localhost(), QosPolicyName({})), std::runtime_error);
}

TEST_F(CmdDeleteQosPolicyTestFixture, mapEntryArgValidation) {
  EXPECT_EQ(DeleteQosMapEntry({"dscp", "40"}).getKey(), 40);
  EXPECT_EQ(
      DeleteQosMapEntry({"dscp", "40"}).getMapType(), DeleteQosMapType::DSCP);
  EXPECT_EQ(
      DeleteQosMapEntry({"tc-to-queue", "3"}).getMapType(),
      DeleteQosMapType::TC_TO_QUEUE);

  // wrong arity
  EXPECT_THROW(DeleteQosMapEntry({"dscp"}), std::invalid_argument);
  EXPECT_THROW(DeleteQosMapEntry({"dscp", "1", "2"}), std::invalid_argument);
  // unknown map type
  EXPECT_THROW(DeleteQosMapEntry({"no-such-map", "1"}), std::invalid_argument);
  // non-integer value
  EXPECT_THROW(DeleteQosMapEntry({"dscp", "abc"}), std::invalid_argument);
  // dscp out of range
  EXPECT_THROW(DeleteQosMapEntry({"dscp", "64"}), std::invalid_argument);
  EXPECT_THROW(DeleteQosMapEntry({"dscp", "-1"}), std::invalid_argument);
  // negative traffic class
  EXPECT_THROW(DeleteQosMapEntry({"tc-to-queue", "-1"}), std::invalid_argument);
  // boundary values accepted
  EXPECT_NO_THROW(DeleteQosMapEntry({"dscp", "0"}));
  EXPECT_NO_THROW(DeleteQosMapEntry({"dscp", "63"}));
}

// -------------------------------------------------------- delete whole policy

TEST_F(CmdDeleteQosPolicyTestFixture, deleteUnreferencedPolicy) {
  setupTestableConfigSession("delete qos policy", "unreferenced");

  auto cmd = CmdDeleteQosPolicy();
  auto result = cmd.queryClient(localhost(), QosPolicyName({"unreferenced"}));

  EXPECT_THAT(result, ::testing::HasSubstr("Successfully deleted"));
  EXPECT_EQ(findPolicy("unreferenced"), nullptr);
  // the other policy survives
  EXPECT_NE(findPolicy("in-use"), nullptr);
}

TEST_F(CmdDeleteQosPolicyTestFixture, deleteReferencedPolicyRefused) {
  setupTestableConfigSession("delete qos policy", "in-use");

  auto cmd = CmdDeleteQosPolicy();
  try {
    cmd.queryClient(localhost(), QosPolicyName({"in-use"}));
    FAIL() << "expected delete of referenced policy to throw";
  } catch (const std::runtime_error& e) {
    EXPECT_THAT(
        e.what(),
        ::testing::AllOf(
            ::testing::HasSubstr("dataPlaneTrafficPolicy.defaultQosPolicy"),
            ::testing::HasSubstr("Unset those fields first")));
  }

  // refusal leaves the policy in place
  EXPECT_NE(findPolicy("in-use"), nullptr);
}

TEST_F(CmdDeleteQosPolicyTestFixture, deleteMissingPolicyIsNoop) {
  setupTestableConfigSession("delete qos policy", "nope");
  const auto sessionBefore = sessionConfigText();

  const auto before =
      *ConfigSession::getInstance().getAgentConfig().sw()->qosPolicies();
  auto cmd = CmdDeleteQosPolicy();
  EXPECT_THAT(
      cmd.queryClient(localhost(), QosPolicyName({"nope"})),
      ::testing::HasSubstr("nothing to delete"));
  EXPECT_EQ(
      *ConfigSession::getInstance().getAgentConfig().sw()->qosPolicies(),
      before);
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

// ------------------------------------------------------------ delete map dscp

TEST_F(CmdDeleteQosPolicyTestFixture, deleteDscpFromSharedEntry) {
  setupTestableConfigSession("delete qos policy map", "dscp 1");

  auto cmd = CmdDeleteQosPolicyMap();
  auto result = cmd.queryClient(
      localhost(),
      QosPolicyName({"unreferenced"}),
      DeleteQosMapEntry({"dscp", "1"}));

  EXPECT_THAT(result, ::testing::HasSubstr("Successfully deleted"));

  // tc 0 keeps its other codepoints; the entry itself survives
  const auto* entry = findDscpEntry("unreferenced", 0);
  ASSERT_NE(entry, nullptr);
  EXPECT_THAT(*entry->fromDscpToTrafficClass(), ::testing::ElementsAre(0, 2));
}

TEST_F(CmdDeleteQosPolicyTestFixture, deleteLastDscpDropsEntry) {
  setupTestableConfigSession("delete qos policy map", "dscp 0");

  auto cmd = CmdDeleteQosPolicyMap();
  // remove every codepoint mapped to tc 0
  for (const auto& dscp : {"0", "1", "2"}) {
    cmd.queryClient(
        localhost(),
        QosPolicyName({"unreferenced"}),
        DeleteQosMapEntry({"dscp", dscp}));
  }

  // entry carried no egress rewrite, so it is dropped rather than left empty
  EXPECT_EQ(findDscpEntry("unreferenced", 0), nullptr);
}

TEST_F(CmdDeleteQosPolicyTestFixture, deleteDscpKeepsEntryWithEgressRewrite) {
  setupTestableConfigSession("delete qos policy map", "dscp 8");

  auto cmd = CmdDeleteQosPolicyMap();
  cmd.queryClient(
      localhost(),
      QosPolicyName({"unreferenced"}),
      DeleteQosMapEntry({"dscp", "8"}));

  // fromTrafficClassToDscp is still set, so the entry must stay
  const auto* entry = findDscpEntry("unreferenced", 1);
  ASSERT_NE(entry, nullptr);
  EXPECT_TRUE(entry->fromDscpToTrafficClass()->empty());
  EXPECT_TRUE(entry->fromTrafficClassToDscp().has_value());
}

TEST_F(CmdDeleteQosPolicyTestFixture, deleteMissingDscpIsNoop) {
  setupTestableConfigSession("delete qos policy map", "dscp 63");
  const auto sessionBefore = sessionConfigText();

  const auto before = *findPolicy("unreferenced")->qosMap();
  auto cmd = CmdDeleteQosPolicyMap();
  EXPECT_THAT(
      cmd.queryClient(
          localhost(),
          QosPolicyName({"unreferenced"}),
          DeleteQosMapEntry({"dscp", "63"})),
      ::testing::HasSubstr("nothing to delete"));
  EXPECT_EQ(*findPolicy("unreferenced")->qosMap(), before);
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

// ----------------------------------------------------- delete map tc-to-queue

TEST_F(CmdDeleteQosPolicyTestFixture, deleteTcToQueue) {
  setupTestableConfigSession("delete qos policy map", "tc-to-queue 1");

  auto cmd = CmdDeleteQosPolicyMap();
  auto result = cmd.queryClient(
      localhost(),
      QosPolicyName({"unreferenced"}),
      DeleteQosMapEntry({"tc-to-queue", "1"}));

  EXPECT_THAT(result, ::testing::HasSubstr("Successfully deleted"));

  const auto* policy = findPolicy("unreferenced");
  ASSERT_NE(policy, nullptr);
  const auto& tcToQueue = *policy->qosMap()->trafficClassToQueueId();
  EXPECT_EQ(tcToQueue.count(1), 0);
  // neighbouring keys untouched
  EXPECT_EQ(tcToQueue.at(0), 0);
  EXPECT_EQ(tcToQueue.at(2), 2);
}

TEST_F(CmdDeleteQosPolicyTestFixture, deleteMissingTcToQueueIsNoop) {
  setupTestableConfigSession("delete qos policy map", "tc-to-queue 7");
  const auto sessionBefore = sessionConfigText();

  const auto before = *findPolicy("unreferenced")->qosMap();
  auto cmd = CmdDeleteQosPolicyMap();
  EXPECT_THAT(
      cmd.queryClient(
          localhost(),
          QosPolicyName({"unreferenced"}),
          DeleteQosMapEntry({"tc-to-queue", "7"})),
      ::testing::HasSubstr("nothing to delete"));
  EXPECT_EQ(*findPolicy("unreferenced")->qosMap(), before);
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

// ------------------------------------------------- delete map remaining kinds

TEST_F(CmdDeleteQosPolicyTestFixture, deleteMplsExpDropsEntry) {
  setupTestableConfigSession("delete qos policy map", "mpls-exp 5");

  auto cmd = CmdDeleteQosPolicyMap();
  auto result = cmd.queryClient(
      localhost(),
      QosPolicyName({"unreferenced"}),
      DeleteQosMapEntry({"mpls-exp", "5"}));

  EXPECT_THAT(result, ::testing::HasSubstr("Successfully deleted"));
  // 5 was the entry's only codepoint and it has no egress rewrite, so the
  // whole ExpQosMap entry is dropped.
  const auto* policy = findPolicy("unreferenced");
  ASSERT_NE(policy, nullptr);
  EXPECT_TRUE(policy->qosMap()->expMaps()->empty());
}

TEST_F(CmdDeleteQosPolicyTestFixture, deleteMissingMplsExpIsNoop) {
  setupTestableConfigSession("delete qos policy map", "mpls-exp 0");
  const auto sessionBefore = sessionConfigText();

  const auto before = *findPolicy("unreferenced")->qosMap();
  auto cmd = CmdDeleteQosPolicyMap();
  EXPECT_THAT(
      cmd.queryClient(
          localhost(),
          QosPolicyName({"unreferenced"}),
          DeleteQosMapEntry({"mpls-exp", "0"})),
      ::testing::HasSubstr("nothing to delete"));
  EXPECT_EQ(*findPolicy("unreferenced")->qosMap(), before);
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

TEST_F(CmdDeleteQosPolicyTestFixture, deleteLastDot1pResetsPcpMaps) {
  setupTestableConfigSession("delete qos policy map", "dot1p 6");

  auto cmd = CmdDeleteQosPolicyMap();
  auto result = cmd.queryClient(
      localhost(),
      QosPolicyName({"unreferenced"}),
      DeleteQosMapEntry({"dot1p", "6"}));

  EXPECT_THAT(result, ::testing::HasSubstr("Successfully deleted"));
  // 6 was the only PCP codepoint; the emptied optional pcpMaps is reset.
  const auto* policy = findPolicy("unreferenced");
  ASSERT_NE(policy, nullptr);
  EXPECT_FALSE(policy->qosMap()->pcpMaps().has_value());
}

TEST_F(CmdDeleteQosPolicyTestFixture, deleteMissingDot1pIsNoop) {
  setupTestableConfigSession("delete qos policy map", "dot1p 0");
  const auto sessionBefore = sessionConfigText();

  const auto before = *findPolicy("unreferenced")->qosMap();
  auto cmd = CmdDeleteQosPolicyMap();
  EXPECT_THAT(
      cmd.queryClient(
          localhost(),
          QosPolicyName({"unreferenced"}),
          DeleteQosMapEntry({"dot1p", "0"})),
      ::testing::HasSubstr("nothing to delete"));
  EXPECT_EQ(*findPolicy("unreferenced")->qosMap(), before);
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

TEST_F(CmdDeleteQosPolicyTestFixture, deleteLastPfcPriToQueueResetsMap) {
  setupTestableConfigSession("delete qos policy map", "pfc-pri-to-queue 3");

  auto cmd = CmdDeleteQosPolicyMap();
  auto result = cmd.queryClient(
      localhost(),
      QosPolicyName({"unreferenced"}),
      DeleteQosMapEntry({"pfc-pri-to-queue", "3"}));

  EXPECT_THAT(result, ::testing::HasSubstr("Successfully deleted"));
  // 3 was the only key; the emptied optional map is reset to unset.
  const auto* policy = findPolicy("unreferenced");
  ASSERT_NE(policy, nullptr);
  EXPECT_FALSE(policy->qosMap()->pfcPriorityToQueueId().has_value());
}

TEST_F(CmdDeleteQosPolicyTestFixture, deleteTcToPgKeepsRemainingKeys) {
  setupTestableConfigSession("delete qos policy map", "tc-to-pg 4");

  auto cmd = CmdDeleteQosPolicyMap();
  auto result = cmd.queryClient(
      localhost(),
      QosPolicyName({"unreferenced"}),
      DeleteQosMapEntry({"tc-to-pg", "4"}));

  EXPECT_THAT(result, ::testing::HasSubstr("Successfully deleted"));
  const auto* policy = findPolicy("unreferenced");
  ASSERT_NE(policy, nullptr);
  const auto& tcToPg = policy->qosMap()->trafficClassToPgId();
  ASSERT_TRUE(tcToPg.has_value());
  EXPECT_EQ(tcToPg->count(4), 0);
  EXPECT_EQ(tcToPg->at(5), 5);
}

TEST_F(CmdDeleteQosPolicyTestFixture, deletePfcPriToPgOnUnsetKeyIsNoop) {
  setupTestableConfigSession("delete qos policy map", "pfc-pri-to-pg 0");
  const auto sessionBefore = sessionConfigText();

  const auto before = *findPolicy("unreferenced")->qosMap();
  auto cmd = CmdDeleteQosPolicyMap();
  EXPECT_THAT(
      cmd.queryClient(
          localhost(),
          QosPolicyName({"unreferenced"}),
          DeleteQosMapEntry({"pfc-pri-to-pg", "0"})),
      ::testing::HasSubstr("nothing to delete"));
  EXPECT_EQ(*findPolicy("unreferenced")->qosMap(), before);
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

TEST_F(CmdDeleteQosPolicyTestFixture, deletePfcPriToPg) {
  setupTestableConfigSession("delete qos policy map", "pfc-pri-to-pg 6");

  auto cmd = CmdDeleteQosPolicyMap();
  auto result = cmd.queryClient(
      localhost(),
      QosPolicyName({"unreferenced"}),
      DeleteQosMapEntry({"pfc-pri-to-pg", "6"}));

  EXPECT_THAT(result, ::testing::HasSubstr("Successfully deleted"));
  const auto* policy = findPolicy("unreferenced");
  ASSERT_NE(policy, nullptr);
  EXPECT_FALSE(policy->qosMap()->pfcPriorityToPgId().has_value());
}

TEST_F(CmdDeleteQosPolicyTestFixture, mapEntryValueRangeValidation) {
  // Non-dscp kinds are 3-bit: 8 is out of range for all of them.
  EXPECT_THROW(DeleteQosMapEntry({"dot1p", "8"}), std::invalid_argument);
  EXPECT_THROW(
      DeleteQosMapEntry({"pfc-pri-to-queue", "8"}), std::invalid_argument);
  EXPECT_THROW(DeleteQosMapEntry({"tc-to-pg", "-1"}), std::invalid_argument);
  EXPECT_THROW(DeleteQosMapEntry({"mpls-exp", "8"}), std::invalid_argument);
}

TEST_F(CmdDeleteQosPolicyTestFixture, mapDeleteOnMissingPolicyFails) {
  setupTestableConfigSession("delete qos policy map", "tc-to-queue 0");

  auto cmd = CmdDeleteQosPolicyMap();
  EXPECT_THROW(
      cmd.queryClient(
          localhost(),
          QosPolicyName({"nope"}),
          DeleteQosMapEntry({"tc-to-queue", "0"})),
      std::runtime_error);
}

// A policy named only through portIdToQosPolicy (not defaultQosPolicy) is still
// refused, and the error points at the per-port field so the operator knows
// which port binding to unset.
TEST_F(CmdDeleteQosPolicyTestFixture, deletePortReferencedPolicyRefused) {
  setupTestableConfigSession("delete qos policy", "port-ref");

  auto cmd = CmdDeleteQosPolicy();
  try {
    cmd.queryClient(localhost(), QosPolicyName({"port-ref"}));
    FAIL() << "expected delete of port-referenced policy to throw";
  } catch (const std::runtime_error& e) {
    EXPECT_THAT(
        std::string(e.what()),
        ::testing::HasSubstr("dataPlaneTrafficPolicy.portIdToQosPolicy"));
  }

  EXPECT_NE(findPolicy("port-ref"), nullptr);
}

// A policy named by cpuTrafficPolicy.trafficPolicy is refused, exercising the
// second TrafficPolicyConfig findReferences scans.
TEST_F(CmdDeleteQosPolicyTestFixture, deleteCpuReferencedPolicyRefused) {
  setupTestableConfigSession("delete qos policy", "cpu-ref");

  auto cmd = CmdDeleteQosPolicy();
  try {
    cmd.queryClient(localhost(), QosPolicyName({"cpu-ref"}));
    FAIL() << "expected delete of cpu-referenced policy to throw";
  } catch (const std::runtime_error& e) {
    EXPECT_THAT(
        std::string(e.what()),
        ::testing::HasSubstr("cpuTrafficPolicy.trafficPolicy"));
  }

  EXPECT_NE(findPolicy("cpu-ref"), nullptr);
}

// Deleting a map entry on a policy that has no qosMap is a no-op rather than
// dereferencing an unset optional.
TEST_F(CmdDeleteQosPolicyTestFixture, mapDeleteOnPolicyWithNoQosMapIsNoop) {
  setupTestableConfigSession("delete qos policy map", "dscp 1");
  const auto sessionBefore = sessionConfigText();

  auto cmd = CmdDeleteQosPolicyMap();
  auto result = cmd.queryClient(
      localhost(), QosPolicyName({"no-map"}), DeleteQosMapEntry({"dscp", "1"}));
  EXPECT_THAT(result, ::testing::HasSubstr("no qosMap configured"));
  EXPECT_THAT(result, ::testing::HasSubstr("nothing to delete"));
  EXPECT_FALSE(findPolicy("no-map")->qosMap().has_value());
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

} // namespace facebook::fboss
