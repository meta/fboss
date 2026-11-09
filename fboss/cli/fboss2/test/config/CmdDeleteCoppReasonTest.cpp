/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <cstddef>
#include <stdexcept>
#include <string>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/delete/copp/reason/CmdDeleteCoppReason.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

// Seed config mirrors the cpuTrafficPolicy.rxReasonToQueueOrderedList shape
// observed in a real production FBOSS agent.conf (rsw001_p001_m002_qzr1
// near line 11076): NDP(8), ARP(1), BGP(11) -> high-priority queue,
// LACP(13) -> mid, UNMATCHED(0) -> default.
class CmdDeleteCoppReasonTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteCoppReasonTestFixture()
      : CmdConfigTestBase(
            "fboss_delete_copp_reason_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "cpuTrafficPolicy": {
      "rxReasonToQueueOrderedList": [
        { "rxReason": 8,  "queueId": 9 },
        { "rxReason": 1,  "queueId": 9 },
        { "rxReason": 11, "queueId": 9 },
        { "rxReason": 13, "queueId": 2 },
        { "rxReason": 0,  "queueId": 1 }
      ]
    }
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "delete copp reason";

  // Helper: find the rxReasonToQueueOrderedList entry for a reason, or
  // nullptr.
  const cfg::PacketRxReasonToQueue* findReason(cfg::PacketRxReason r) const {
    auto& config = ConfigSession::getInstance().getAgentConfig();
    const auto& policy = *config.sw()->cpuTrafficPolicy();
    for (const auto& entry : *policy.rxReasonToQueueOrderedList()) {
      if (*entry.rxReason() == r) {
        return &entry;
      }
    }
    return nullptr;
  }

  size_t reasonListSize() const {
    auto& config = ConfigSession::getInstance().getAgentConfig();
    return config.sw()
        ->cpuTrafficPolicy()
        ->rxReasonToQueueOrderedList()
        ->size();
  }
};

// =============================================================
// CoppReasonDeleteArgs validation tests
// =============================================================

TEST_F(CmdDeleteCoppReasonTestFixture, argsValid) {
  EXPECT_EQ(
      CoppReasonDeleteArgs({"arp"}).getReason(), cfg::PacketRxReason::ARP);
  EXPECT_EQ(
      CoppReasonDeleteArgs({"ARP"}).getReason(), cfg::PacketRxReason::ARP);
  EXPECT_EQ(
      CoppReasonDeleteArgs({"bgpv6"}).getReason(), cfg::PacketRxReason::BGPV6);
  EXPECT_EQ(
      CoppReasonDeleteArgs({"ttl_1"}).getReason(), cfg::PacketRxReason::TTL_1);
}

TEST_F(CmdDeleteCoppReasonTestFixture, argsInvalid) {
  // Wrong arity
  EXPECT_THROW(CoppReasonDeleteArgs({}), std::invalid_argument);
  EXPECT_THROW(CoppReasonDeleteArgs({"arp", "queue"}), std::invalid_argument);

  // Unknown reason name
  EXPECT_THROW(CoppReasonDeleteArgs({"not-a-reason"}), std::invalid_argument);
  EXPECT_THROW(CoppReasonDeleteArgs({""}), std::invalid_argument);
}

// =============================================================
// Reason mapping delete
// =============================================================

TEST_F(CmdDeleteCoppReasonTestFixture, deleteMappedReason) {
  setupTestableConfigSession(cmdPrefix_, "arp");
  CmdDeleteCoppReason cmd;
  HostInfo hostInfo("testhost");
  CoppReasonDeleteArgs args({"arp"});

  ASSERT_NE(findReason(cfg::PacketRxReason::ARP), nullptr);
  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("Deleted reason ARP -> queue 9 mapping"));

  EXPECT_EQ(findReason(cfg::PacketRxReason::ARP), nullptr);
  EXPECT_EQ(reasonListSize(), 4);
  // Other entries untouched.
  EXPECT_NE(findReason(cfg::PacketRxReason::NDP), nullptr);
  EXPECT_NE(findReason(cfg::PacketRxReason::LACP), nullptr);
}

TEST_F(CmdDeleteCoppReasonTestFixture, deleteUnmappedReasonIsNoop) {
  setupTestableConfigSession(cmdPrefix_, "lldp");
  const auto sessionBefore = sessionConfigText();
  CmdDeleteCoppReason cmd;
  HostInfo hostInfo("testhost");
  CoppReasonDeleteArgs args({"lldp"});

  EXPECT_THAT(cmd.queryClient(hostInfo, args), HasSubstr("nothing to delete"));
  EXPECT_EQ(reasonListSize(), 5);
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

// Deleting when the device has no cpuTrafficPolicy at all must be a no-op
// rather than dereference an empty optional.
class CmdDeleteCoppReasonNoPolicyFixture : public CmdConfigTestBase {
 public:
  CmdDeleteCoppReasonNoPolicyFixture()
      : CmdConfigTestBase(
            "fboss_delete_copp_reason_no_policy_test_%%%%-%%%%-%%%%-%%%%",
            R"({"sw": {}})") {}

 protected:
  const std::string cmdPrefix_ = "delete copp reason";
};

TEST_F(CmdDeleteCoppReasonNoPolicyFixture, noCpuTrafficPolicyIsNoop) {
  setupTestableConfigSession(cmdPrefix_, "arp");
  const auto sessionBefore = sessionConfigText();
  CmdDeleteCoppReason cmd;
  HostInfo hostInfo("testhost");
  CoppReasonDeleteArgs args({"arp"});

  EXPECT_THAT(cmd.queryClient(hostInfo, args), HasSubstr("nothing to delete"));
  // The no-op must not create the policy.
  EXPECT_FALSE(
      ConfigSession::getInstance()
          .getAgentConfig()
          .sw()
          ->cpuTrafficPolicy()
          .has_value());
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

// cpuTrafficPolicy present but rxReasonToQueueOrderedList absent is also a
// no-op.
class CmdDeleteCoppReasonNoListFixture : public CmdConfigTestBase {
 public:
  CmdDeleteCoppReasonNoListFixture()
      : CmdConfigTestBase(
            "fboss_delete_copp_reason_no_list_test_%%%%-%%%%-%%%%-%%%%",
            R"({"sw": {"cpuTrafficPolicy": {}}})") {}

 protected:
  const std::string cmdPrefix_ = "delete copp reason";
};

TEST_F(CmdDeleteCoppReasonNoListFixture, noReasonListIsNoop) {
  setupTestableConfigSession(cmdPrefix_, "arp");
  const auto sessionBefore = sessionConfigText();
  CmdDeleteCoppReason cmd;
  HostInfo hostInfo("testhost");
  CoppReasonDeleteArgs args({"arp"});

  EXPECT_THAT(cmd.queryClient(hostInfo, args), HasSubstr("nothing to delete"));
  // The no-op must not create the reason list.
  EXPECT_FALSE(
      ConfigSession::getInstance()
          .getAgentConfig()
          .sw()
          ->cpuTrafficPolicy()
          ->rxReasonToQueueOrderedList()
          .has_value());
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

} // namespace facebook::fboss
