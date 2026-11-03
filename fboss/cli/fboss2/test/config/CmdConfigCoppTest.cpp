/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <folly/String.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/copp/CmdConfigCopp.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

// Seed config mirrors the cpuQueues + cpuTrafficPolicy shape observed in a
// real production FBOSS agent.conf (rsw001_p001_m002_qzr1 near line 11076):
//   - cpuQueues[] holds 4 entries with per-queue weight, name, and an
//     optional portQueueRate.pktsPerSec cap on the low-priority queues.
//   - cpuTrafficPolicy.rxReasonToQueueOrderedList[] maps protocol reasons
//     to queue ids in priority order.
class CmdConfigCoppTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigCoppTestFixture()
      : CmdConfigTestBase(
            "fboss_copp_config_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "cpuQueues": [
      { "id": 9, "streamType": 1, "weight": 4, "scheduling": 0,
        "name": "cpuQueue-high" },
      { "id": 2, "streamType": 1, "weight": 2, "scheduling": 0,
        "name": "cpuQueue-mid" },
      { "id": 1, "streamType": 1, "weight": 1, "scheduling": 0,
        "name": "cpuQueue-default",
        "portQueueRate": { "pktsPerSec": { "minimum": 0, "maximum": 1000 } } },
      { "id": 0, "streamType": 1, "weight": 1, "scheduling": 0,
        "name": "cpuQueue-low",
        "portQueueRate": { "pktsPerSec": { "minimum": 0, "maximum": 500 } } }
    ],
    "cpuTrafficPolicy": {
      "rxReasonToQueueOrderedList": [
        { "rxReason": 8,  "queueId": 9 },
        { "rxReason": 1,  "queueId": 9 },
        { "rxReason": 11, "queueId": 9 },
        { "rxReason": 13, "queueId": 2 },
        { "rxReason": 0,  "queueId": 1 }
      ],
      "trafficPolicy": {
        "matchToAction": [
          { "matcher": "cpuPolicy-mid",
            "action": { "counter": "cpuPolicy-mid-counter" } }
        ]
      }
    }
  }
})") {}

 protected:
  const std::string cpuQueueCmdPrefix_ = "config copp queue";
  const std::string reasonCmdPrefix_ = "config copp reason";
  const std::string cpuTrafficPolicyCmdPrefix_ = "config copp traffic-policy";

  // Helper: find the cpuQueues entry with the given id, or nullptr.
  const cfg::PortQueue* findQueue(int16_t id) const {
    auto& config = ConfigSession::getInstance().getAgentConfig();
    for (const auto& q : *config.sw()->cpuQueues()) {
      if (*q.id() == id) {
        return &q;
      }
    }
    return nullptr;
  }

  // Helper: find the rxReasonToQueueOrderedList entry for a reason, or nullptr.
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

  // Helper: find the matchToAction entry with the given matcher, or nullptr.
  const cfg::MatchToAction* findMatchToAction(const std::string& name) const {
    auto& config = ConfigSession::getInstance().getAgentConfig();
    const auto& policy = *config.sw()->cpuTrafficPolicy();
    if (!policy.trafficPolicy().has_value()) {
      return nullptr;
    }
    for (const auto& mta : *policy.trafficPolicy()->matchToAction()) {
      if (*mta.matcher() == name) {
        return &mta;
      }
    }
    return nullptr;
  }
};

// =============================================================
// CoppQueueArgs validation tests
// =============================================================

TEST_F(CmdConfigCoppTestFixture, cpuQueueArgs_idOnly) {
  CoppQueueArgs a({"0"});
  EXPECT_EQ(a.getQueueId(), 0);
  EXPECT_FALSE(a.hasEdits());

  CoppQueueArgs b({"9"});
  EXPECT_EQ(b.getQueueId(), 9);
  EXPECT_FALSE(b.hasEdits());
}

TEST_F(CmdConfigCoppTestFixture, cpuQueueArgs_name) {
  CoppQueueArgs a({"2", "name", "cpuQueue-mid"});
  EXPECT_EQ(a.getQueueId(), 2);
  ASSERT_EQ(a.getAttributes().size(), 1);
  EXPECT_EQ(a.getAttributes()[0].first, "name");
  EXPECT_THAT(a.getAttributes()[0].second, ElementsAre("cpuQueue-mid"));
}

// rate-limit arrives as value tokens for the shared applier. The two-token
// <unit> <max> form must keep working alongside the three-token one.
TEST_F(CmdConfigCoppTestFixture, cpuQueueArgs_rateLimit) {
  CoppQueueArgs a({"0", "rate-limit", "kbps", "1500"});
  EXPECT_EQ(a.getQueueId(), 0);
  ASSERT_EQ(a.getAttributes().size(), 1);
  EXPECT_EQ(a.getAttributes()[0].first, "rate-limit");
  EXPECT_THAT(a.getAttributes()[0].second, ElementsAre("kbps", "1500"));

  CoppQueueArgs b({"1", "rate-limit", "pps", "750"});
  ASSERT_EQ(b.getAttributes().size(), 1);
  EXPECT_THAT(b.getAttributes()[0].second, ElementsAre("pps", "750"));

  // Three-token form: <unit> <min> <max>.
  CoppQueueArgs c({"1", "rate-limit", "kbps", "500", "1500"});
  ASSERT_EQ(c.getAttributes().size(), 1);
  EXPECT_THAT(c.getAttributes()[0].second, ElementsAre("kbps", "500", "1500"));

  // A following attribute name is not a bare integer, so the two-token form
  // is still recognized when another attribute trails it.
  CoppQueueArgs d({"1", "rate-limit", "pps", "750", "weight", "4"});
  ASSERT_EQ(d.getAttributes().size(), 2);
  EXPECT_THAT(d.getAttributes()[0].second, ElementsAre("pps", "750"));
  EXPECT_EQ(d.getAttributes()[1].first, "weight");
}

// Generic attributes flow through as <attr, value> pairs for
// utils::applyPortQueueConfig; the aqm keyword captures the tail.
TEST_F(CmdConfigCoppTestFixture, cpuQueueArgs_genericAttributes) {
  CoppQueueArgs a({"2", "scheduling", "wrr", "weight", "4"});
  EXPECT_EQ(a.getQueueId(), 2);
  ASSERT_EQ(a.getAttributes().size(), 2);
  EXPECT_EQ(a.getAttributes()[0].first, "scheduling");
  EXPECT_THAT(a.getAttributes()[0].second, ElementsAre("wrr"));
  EXPECT_EQ(a.getAttributes()[1].first, "weight");
  EXPECT_THAT(a.getAttributes()[1].second, ElementsAre("4"));

  // Attributes combine in one invocation, name included.
  CoppQueueArgs b(
      {"2", "name", "q2", "rate-limit", "pps", "100", "weight", "8"});
  ASSERT_EQ(b.getAttributes().size(), 3);
  EXPECT_EQ(b.getAttributes()[0].first, "name");
  EXPECT_EQ(b.getAttributes()[1].first, "rate-limit");
  EXPECT_EQ(b.getAttributes()[2].first, "weight");

  CoppQueueArgs c(
      {"2", "aqm", "congestion-behavior", "ECN", "detection", "linear"});
  EXPECT_TRUE(c.getAttributes().empty());
  EXPECT_EQ(c.getAqmAttributes().size(), 4);
}

TEST_F(CmdConfigCoppTestFixture, cpuQueueArgs_badArity) {
  EXPECT_THROW(CoppQueueArgs({}), std::invalid_argument);
  EXPECT_THROW(CoppQueueArgs({"0", "name"}), std::invalid_argument);
  EXPECT_THROW(
      CoppQueueArgs({"0", "name", "foo", "extra"}), std::invalid_argument);
  EXPECT_THROW(
      CoppQueueArgs({"0", "rate-limit", "kbps"}), std::invalid_argument);
  EXPECT_THROW(CoppQueueArgs({"0", "rate-limit"}), std::invalid_argument);
  EXPECT_THROW(CoppQueueArgs({"0", "weight"}), std::invalid_argument);
}

TEST_F(CmdConfigCoppTestFixture, cpuQueueArgs_badValues) {
  EXPECT_THROW(CoppQueueArgs({"abc"}), std::invalid_argument);
  EXPECT_THROW(CoppQueueArgs({"-1"}), std::invalid_argument);
  EXPECT_THROW(CoppQueueArgs({"999"}), std::invalid_argument);
}

// rate-limit values are checked in utils::applyPortQueueConfig, so a bad one
// is rejected when the edit is applied rather than when the args are parsed.
TEST_F(CmdConfigCoppTestFixture, cpuQueue_badRateLimitValues) {
  setupTestableConfigSession(cpuQueueCmdPrefix_, "0 rate-limit kbps 1");
  CmdConfigCoppQueue cmd;
  HostInfo hostInfo("testhost");

  // Non-numeric, negative, unknown unit, inverted range, empty name.
  for (const std::vector<std::string>& args :
       {std::vector<std::string>{"0", "rate-limit", "kbps", "abc"},
        std::vector<std::string>{"0", "rate-limit", "pps", "-5"},
        std::vector<std::string>{"0", "rate-limit", "mbps", "100"},
        std::vector<std::string>{"0", "rate-limit", "kbps", "200", "100"},
        std::vector<std::string>{"0", "name", ""}}) {
    EXPECT_THROW(
        cmd.queryClient(hostInfo, CoppQueueArgs(args)), std::invalid_argument)
        << folly::join(" ", args);
  }
}

// =============================================================
// CoppReasonArgs validation tests
// =============================================================

TEST_F(CmdConfigCoppTestFixture, reasonArgs_valid) {
  CoppReasonArgs a({"arp", "queue", "9"});
  EXPECT_EQ(a.getReason(), cfg::PacketRxReason::ARP);
  EXPECT_EQ(a.getQueueId(), 9);

  // Case-insensitive + dash-to-underscore normalization
  CoppReasonArgs b({"BGP", "queue", "2"});
  EXPECT_EQ(b.getReason(), cfg::PacketRxReason::BGP);
  EXPECT_EQ(b.getQueueId(), 2);

  CoppReasonArgs c({"ttl-1", "queue", "0"});
  EXPECT_EQ(c.getReason(), cfg::PacketRxReason::TTL_1);
  EXPECT_EQ(c.getQueueId(), 0);

  CoppReasonArgs d({"dhcpv6", "queue", "2"});
  EXPECT_EQ(d.getReason(), cfg::PacketRxReason::DHCPV6);
  EXPECT_EQ(d.getQueueId(), 2);
}

TEST_F(CmdConfigCoppTestFixture, reasonArgs_badArity) {
  EXPECT_THROW(CoppReasonArgs({}), std::invalid_argument);
  EXPECT_THROW(CoppReasonArgs({"arp"}), std::invalid_argument);
  EXPECT_THROW(CoppReasonArgs({"arp", "queue"}), std::invalid_argument);
  EXPECT_THROW(
      CoppReasonArgs({"arp", "queue", "1", "extra"}), std::invalid_argument);
}

TEST_F(CmdConfigCoppTestFixture, reasonArgs_unknownReason) {
  EXPECT_THROW(
      CoppReasonArgs({"not-a-reason", "queue", "0"}), std::invalid_argument);
  EXPECT_THROW(CoppReasonArgs({"", "queue", "0"}), std::invalid_argument);
}

TEST_F(CmdConfigCoppTestFixture, reasonArgs_missingQueueKeyword) {
  EXPECT_THROW(CoppReasonArgs({"arp", "que", "9"}), std::invalid_argument);
}

TEST_F(CmdConfigCoppTestFixture, reasonArgs_badQueueId) {
  EXPECT_THROW(CoppReasonArgs({"arp", "queue", "abc"}), std::invalid_argument);
  EXPECT_THROW(CoppReasonArgs({"arp", "queue", "-1"}), std::invalid_argument);
  EXPECT_THROW(CoppReasonArgs({"arp", "queue", "999"}), std::invalid_argument);
}

// =============================================================
// queryClient() tests — queue
// =============================================================

TEST_F(CmdConfigCoppTestFixture, cpuQueue_ensureExists) {
  setupTestableConfigSession(cpuQueueCmdPrefix_, "9");
  CmdConfigCoppQueue cmd;
  HostInfo hostInfo("testhost");
  CoppQueueArgs args({"9"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("9"));

  const auto* q = findQueue(9);
  ASSERT_NE(q, nullptr);
  // Existing queue's name must be preserved when no sub-command was given.
  EXPECT_EQ(q->name(), "cpuQueue-high");
}

TEST_F(CmdConfigCoppTestFixture, cpuQueue_createRequiresStreamType) {
  setupTestableConfigSession(cpuQueueCmdPrefix_, "5");
  CmdConfigCoppQueue cmd;
  HostInfo hostInfo("testhost");
  ASSERT_EQ(findQueue(5), nullptr);

  // A cpu queue is identified by (streamType, queueId); creating one without
  // naming the stream type must be refused, not defaulted.
  EXPECT_THROW(
      cmd.queryClient(hostInfo, CoppQueueArgs({"5"})), std::invalid_argument);
  EXPECT_EQ(findQueue(5), nullptr);

  cmd.queryClient(
      hostInfo,
      CoppQueueArgs({"5", "stream-type", "multicast", "weight", "3"}));
  const auto* q = findQueue(5);
  ASSERT_NE(q, nullptr);
  EXPECT_EQ(*q->id(), 5);
  EXPECT_EQ(*q->streamType(), cfg::StreamType::MULTICAST);
  EXPECT_EQ(*q->weight(), 3);
}

TEST_F(CmdConfigCoppTestFixture, cpuQueue_setName) {
  setupTestableConfigSession(cpuQueueCmdPrefix_, "2 name cpuQueue-renamed");
  CmdConfigCoppQueue cmd;
  HostInfo hostInfo("testhost");
  CoppQueueArgs args({"2", "name", "cpuQueue-renamed"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("cpuQueue-renamed"));

  const auto* q = findQueue(2);
  ASSERT_NE(q, nullptr);
  EXPECT_EQ(q->name(), "cpuQueue-renamed");
}

TEST_F(CmdConfigCoppTestFixture, cpuQueue_setRateLimitKbps) {
  setupTestableConfigSession(cpuQueueCmdPrefix_, "0 rate-limit kbps 1500");
  CmdConfigCoppQueue cmd;
  HostInfo hostInfo("testhost");
  CoppQueueArgs args({"0", "rate-limit", "kbps", "1500"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("1500"));

  const auto* q = findQueue(0);
  ASSERT_NE(q, nullptr);
  ASSERT_TRUE(q->portQueueRate().has_value());
  // Switching from pps to kbps must clear the pps side of the union.
  ASSERT_TRUE(q->portQueueRate()->kbitsPerSec().has_value());
  EXPECT_EQ(*q->portQueueRate()->kbitsPerSec()->maximum(), 1500);
  EXPECT_EQ(*q->portQueueRate()->kbitsPerSec()->minimum(), 0);
  EXPECT_FALSE(q->portQueueRate()->pktsPerSec().has_value());
}

TEST_F(CmdConfigCoppTestFixture, cpuQueue_setRateLimitPps) {
  setupTestableConfigSession(cpuQueueCmdPrefix_, "1 rate-limit pps 2000");
  CmdConfigCoppQueue cmd;
  HostInfo hostInfo("testhost");
  CoppQueueArgs args({"1", "rate-limit", "pps", "2000"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("2000"));

  const auto* q = findQueue(1);
  ASSERT_NE(q, nullptr);
  ASSERT_TRUE(q->portQueueRate().has_value());
  ASSERT_TRUE(q->portQueueRate()->pktsPerSec().has_value());
  EXPECT_EQ(*q->portQueueRate()->pktsPerSec()->maximum(), 2000);
}

TEST_F(CmdConfigCoppTestFixture, cpuQueue_setSchedulingSp) {
  setupTestableConfigSession(cpuQueueCmdPrefix_, "9 scheduling sp");
  CmdConfigCoppQueue cmd;
  HostInfo hostInfo("testhost");
  CoppQueueArgs args({"9", "scheduling", "sp"});

  cmd.queryClient(hostInfo, args);

  const auto* q = findQueue(9);
  ASSERT_NE(q, nullptr);
  EXPECT_EQ(*q->scheduling(), cfg::QueueScheduling::STRICT_PRIORITY);
  // Switching to SP must not clear the existing weight; the agent ignores
  // it for SP queues and it is restored if the user switches back to WRR.
  ASSERT_TRUE(q->weight().has_value());
  EXPECT_EQ(*q->weight(), 4);
}

TEST_F(CmdConfigCoppTestFixture, cpuQueue_setSchedulingAndWeight) {
  setupTestableConfigSession(cpuQueueCmdPrefix_, "2 scheduling wrr weight 8");
  CmdConfigCoppQueue cmd;
  HostInfo hostInfo("testhost");
  CoppQueueArgs args({"2", "scheduling", "wrr", "weight", "8"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("weight 8"));

  const auto* q = findQueue(2);
  ASSERT_NE(q, nullptr);
  EXPECT_EQ(*q->scheduling(), cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN);
  ASSERT_TRUE(q->weight().has_value());
  EXPECT_EQ(*q->weight(), 8);
  // Other fields on the queue must be untouched.
  EXPECT_EQ(q->name(), "cpuQueue-mid");
}

TEST_F(CmdConfigCoppTestFixture, cpuQueue_setRateLimitMinMax) {
  setupTestableConfigSession(cpuQueueCmdPrefix_, "2 rate-limit kbps 1000 5000");
  CmdConfigCoppQueue cmd;
  HostInfo hostInfo("testhost");
  CoppQueueArgs args({"2", "rate-limit", "kbps", "1000", "5000"});

  cmd.queryClient(hostInfo, args);

  const auto* q = findQueue(2);
  ASSERT_NE(q, nullptr);
  ASSERT_TRUE(q->portQueueRate()->kbitsPerSec().has_value());
  EXPECT_EQ(*q->portQueueRate()->kbitsPerSec()->minimum(), 1000);
  EXPECT_EQ(*q->portQueueRate()->kbitsPerSec()->maximum(), 5000);
}

TEST_F(CmdConfigCoppTestFixture, cpuQueue_setMaxDynamicSharedBytes) {
  setupTestableConfigSession(
      cpuQueueCmdPrefix_, "2 max-dynamic-shared-bytes 20971520");
  CmdConfigCoppQueue cmd;
  HostInfo hostInfo("testhost");

  cmd.queryClient(
      hostInfo, CoppQueueArgs({"2", "max-dynamic-shared-bytes", "20971520"}));

  const auto* q = findQueue(2);
  ASSERT_NE(q, nullptr);
  ASSERT_TRUE(q->maxDynamicSharedBytes().has_value());
  EXPECT_EQ(*q->maxDynamicSharedBytes(), 20971520);
}

TEST_F(CmdConfigCoppTestFixture, cpuQueue_setReservedBytes) {
  setupTestableConfigSession(cpuQueueCmdPrefix_, "2 reserved-bytes 3000");
  CmdConfigCoppQueue cmd;
  HostInfo hostInfo("testhost");
  CoppQueueArgs args({"2", "reserved-bytes", "3000"});

  cmd.queryClient(hostInfo, args);

  const auto* q = findQueue(2);
  ASSERT_NE(q, nullptr);
  ASSERT_TRUE(q->reservedBytes().has_value());
  EXPECT_EQ(*q->reservedBytes(), 3000);
}

// Weight above the SAI uint8 range is refused inside the shared helper, so
// every caller gets the bound. Weight 0 is legal: SAI coerces it to 1 and
// shipped configs may carry it, so a set/restore round-trip must accept it.
TEST_F(CmdConfigCoppTestFixture, cpuQueue_weightOutOfRange) {
  setupTestableConfigSession(cpuQueueCmdPrefix_, "2 weight 256");
  CmdConfigCoppQueue cmd;
  HostInfo hostInfo("testhost");

  EXPECT_THROW(
      cmd.queryClient(hostInfo, CoppQueueArgs({"2", "weight", "256"})),
      std::invalid_argument);

  cmd.queryClient(hostInfo, CoppQueueArgs({"2", "weight", "0"}));
  const auto* q = findQueue(2);
  ASSERT_NE(q, nullptr);
  EXPECT_EQ(*q->weight(), 0);
}

// A queue created by the CLI must be programmable: the agent filters
// A rejected attribute must not leave a half-built queue behind: the create
// and the edit are one transaction.
TEST_F(CmdConfigCoppTestFixture, cpuQueue_rejectedCreateLeavesNoPhantom) {
  setupTestableConfigSession(
      cpuQueueCmdPrefix_, "12 stream-type multicast weight 999");
  CmdConfigCoppQueue cmd;
  HostInfo hostInfo("testhost");

  EXPECT_THROW(
      cmd.queryClient(
          hostInfo,
          CoppQueueArgs({"12", "stream-type", "multicast", "weight", "999"})),
      std::invalid_argument);
  EXPECT_EQ(findQueue(12), nullptr);
}

// A trailing bare aqm keyword must be an error, not a silent success that
// saves the session having applied nothing.
TEST_F(CmdConfigCoppTestFixture, cpuQueue_bareAqmThrows) {
  EXPECT_THROW(CoppQueueArgs({"2", "aqm"}), std::invalid_argument);
  EXPECT_THROW(
      CoppQueueArgs({"2", "name", "q2", "active-queue-management"}),
      std::invalid_argument);
}

// Repeated attributes would be last-wins while the echo message claims both
// applied, so utils::walkQueueAttributes refuses them.
TEST_F(CmdConfigCoppTestFixture, cpuQueue_duplicateNameOrRateLimitThrows) {
  EXPECT_THROW(
      CoppQueueArgs({"2", "name", "a", "name", "b"}), std::invalid_argument);
  EXPECT_THROW(
      CoppQueueArgs(
          {"2", "rate-limit", "kbps", "100", "rate-limit", "pps", "50"}),
      std::invalid_argument);
  EXPECT_THROW(
      CoppQueueArgs({"2", "weight", "4", "weight", "5"}),
      std::invalid_argument);
}

// Unknown attributes parse as generic pairs and are rejected by
// utils::applyPortQueueConfig with its valid-attribute list.
TEST_F(CmdConfigCoppTestFixture, cpuQueue_unknownAttribute) {
  setupTestableConfigSession(cpuQueueCmdPrefix_, "2 wait 4");
  CmdConfigCoppQueue cmd;
  HostInfo hostInfo("testhost");

  EXPECT_THROW(
      cmd.queryClient(hostInfo, CoppQueueArgs({"2", "wait", "4"})),
      std::invalid_argument);
}

// =============================================================
// queryClient() tests — reason
// =============================================================

TEST_F(CmdConfigCoppTestFixture, reason_updateExisting) {
  setupTestableConfigSession(reasonCmdPrefix_, "arp queue 2");
  CmdConfigCoppReason cmd;
  HostInfo hostInfo("testhost");

  // Seed maps ARP -> 9; update it to 2.
  CoppReasonArgs args({"arp", "queue", "2"});
  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("ARP"));

  const auto* entry = findReason(cfg::PacketRxReason::ARP);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(*entry->queueId(), 2);
}

TEST_F(CmdConfigCoppTestFixture, reason_appendNew) {
  setupTestableConfigSession(reasonCmdPrefix_, "dhcp queue 2");
  CmdConfigCoppReason cmd;
  HostInfo hostInfo("testhost");

  // DHCP is not in the seed — must be appended.
  ASSERT_EQ(findReason(cfg::PacketRxReason::DHCP), nullptr);
  CoppReasonArgs args({"dhcp", "queue", "2"});
  cmd.queryClient(hostInfo, args);

  const auto* entry = findReason(cfg::PacketRxReason::DHCP);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(*entry->queueId(), 2);
}

TEST_F(CmdConfigCoppTestFixture, reason_preservesOrderingOnUpdate) {
  setupTestableConfigSession(reasonCmdPrefix_, "bgp queue 0");
  CmdConfigCoppReason cmd;
  HostInfo hostInfo("testhost");

  auto& config = ConfigSession::getInstance().getAgentConfig();
  const auto origSize =
      config.sw()->cpuTrafficPolicy()->rxReasonToQueueOrderedList()->size();

  CoppReasonArgs args({"bgp", "queue", "0"});
  cmd.queryClient(hostInfo, args);

  // Updating an existing entry must not grow the list.
  EXPECT_EQ(
      config.sw()->cpuTrafficPolicy()->rxReasonToQueueOrderedList()->size(),
      origSize);
  // And BGP's position in the list must be unchanged (still at index 2).
  const auto& list =
      *config.sw()->cpuTrafficPolicy()->rxReasonToQueueOrderedList();
  EXPECT_EQ(*list[2].rxReason(), cfg::PacketRxReason::BGP);
  EXPECT_EQ(*list[2].queueId(), 0);
}

TEST_F(CmdConfigCoppTestFixture, reasonArgs_order) {
  CoppReasonArgs a({"arp", "queue", "9", "order", "0"});
  ASSERT_TRUE(a.getOrder().has_value());
  EXPECT_EQ(*a.getOrder(), 0);

  EXPECT_FALSE(CoppReasonArgs({"arp", "queue", "9"}).getOrder().has_value());

  EXPECT_THROW(
      CoppReasonArgs({"arp", "queue", "9", "order"}), std::invalid_argument);
  EXPECT_THROW(
      CoppReasonArgs({"arp", "queue", "9", "position", "0"}),
      std::invalid_argument);
  EXPECT_THROW(
      CoppReasonArgs({"arp", "queue", "9", "order", "abc"}),
      std::invalid_argument);
  EXPECT_THROW(
      CoppReasonArgs({"arp", "queue", "9", "order", "-1"}),
      std::invalid_argument);
}

// Seed list order: NDP(8)->9, ARP(1)->9, BGP(11)->9, LACP(13)->2,
// UNMATCHED(0)->1. `order <n>` places the entry at 0-based index n of the
// final list.
TEST_F(CmdConfigCoppTestFixture, reason_insertNewAtFront) {
  setupTestableConfigSession(reasonCmdPrefix_, "dhcp queue 2 order 0");
  CmdConfigCoppReason cmd;
  HostInfo hostInfo("testhost");

  CoppReasonArgs args({"dhcp", "queue", "2", "order", "0"});
  cmd.queryClient(hostInfo, args);

  const auto& list = *ConfigSession::getInstance()
                          .getAgentConfig()
                          .sw()
                          ->cpuTrafficPolicy()
                          ->rxReasonToQueueOrderedList();
  ASSERT_EQ(list.size(), 6);
  EXPECT_EQ(*list[0].rxReason(), cfg::PacketRxReason::DHCP);
  EXPECT_EQ(*list[0].queueId(), 2);
  // Previous head shifted down.
  EXPECT_EQ(*list[1].rxReason(), cfg::PacketRxReason::NDP);
}

TEST_F(CmdConfigCoppTestFixture, reason_moveExisting) {
  setupTestableConfigSession(reasonCmdPrefix_, "bgp queue 2 order 0");
  CmdConfigCoppReason cmd;
  HostInfo hostInfo("testhost");

  // BGP sits at index 2 in the seed; move it to the front.
  CoppReasonArgs args({"bgp", "queue", "2", "order", "0"});
  cmd.queryClient(hostInfo, args);

  const auto& list = *ConfigSession::getInstance()
                          .getAgentConfig()
                          .sw()
                          ->cpuTrafficPolicy()
                          ->rxReasonToQueueOrderedList();
  // Moving must not grow the list.
  ASSERT_EQ(list.size(), 5);
  EXPECT_EQ(*list[0].rxReason(), cfg::PacketRxReason::BGP);
  EXPECT_EQ(*list[0].queueId(), 2);
  EXPECT_EQ(*list[1].rxReason(), cfg::PacketRxReason::NDP);
}

TEST_F(CmdConfigCoppTestFixture, reason_orderOutOfRange) {
  setupTestableConfigSession(reasonCmdPrefix_, "dhcp queue 2 order 6");
  CmdConfigCoppReason cmd;
  HostInfo hostInfo("testhost");

  // 5 entries in the seed; a new entry may go at positions 0..5, an existing
  // one only at 0..4.
  EXPECT_THROW(
      cmd.queryClient(
          hostInfo, CoppReasonArgs({"dhcp", "queue", "2", "order", "6"})),
      std::invalid_argument);
  EXPECT_THROW(
      cmd.queryClient(
          hostInfo, CoppReasonArgs({"bgp", "queue", "2", "order", "5"})),
      std::invalid_argument);
}

// =============================================================
// TrafficPolicyArgs validation tests
//
// The arg class validates only shape (arity, the match/action literals, a
// non-empty rule name). The action keyword and its value are checked by
// traffic_policy::applyAction against the shared action table, so those cases
// live in the apply tests below.
// =============================================================

TEST_F(CmdConfigCoppTestFixture, trafficPolicyArgs_valid) {
  traffic_policy::TrafficPolicyArgs a(
      {"match", "acl-x", "action", "send-to-queue", "2"});
  EXPECT_EQ(a.getMatcherName(), "acl-x");
  EXPECT_EQ(
      a.getActionTokens(), (std::vector<std::string>{"send-to-queue", "2"}));

  traffic_policy::TrafficPolicyArgs b(
      {"match", "acl-x", "action", "counter", "cnt"});
  EXPECT_EQ(b.getActionTokens(), (std::vector<std::string>{"counter", "cnt"}));

  // Valueless actions are a legal 4-token form.
  traffic_policy::TrafficPolicyArgs c(
      {"match", "acl-x", "action", "trap-to-cpu"});
  EXPECT_EQ(c.getActionTokens(), (std::vector<std::string>{"trap-to-cpu"}));

  // redirect carries two value tokens.
  traffic_policy::TrafficPolicyArgs d(
      {"match", "acl-x", "action", "redirect", "nexthop", "1.1.1.1"});
  EXPECT_EQ(
      d.getActionTokens(),
      (std::vector<std::string>{"redirect", "nexthop", "1.1.1.1"}));
}

TEST_F(CmdConfigCoppTestFixture, trafficPolicyArgs_badArity) {
  EXPECT_THROW(traffic_policy::TrafficPolicyArgs({}), std::invalid_argument);
  EXPECT_THROW(
      traffic_policy::TrafficPolicyArgs({"match", "acl-x", "action"}),
      std::invalid_argument);
}

TEST_F(CmdConfigCoppTestFixture, trafficPolicyArgs_badLiterals) {
  EXPECT_THROW(
      traffic_policy::TrafficPolicyArgs(
          {"matcher", "acl-x", "action", "counter", "cnt"}),
      std::invalid_argument);
  EXPECT_THROW(
      traffic_policy::TrafficPolicyArgs(
          {"match", "acl-x", "actions", "counter", "cnt"}),
      std::invalid_argument);
  EXPECT_THROW(
      traffic_policy::TrafficPolicyArgs(
          {"match", "", "action", "counter", "cnt"}),
      std::invalid_argument);
}

// =============================================================
// queryClient() tests — traffic-policy
// =============================================================

TEST_F(CmdConfigCoppTestFixture, cpuTrafficPolicy_addActionToExistingMatcher) {
  setupTestableConfigSession(
      cpuTrafficPolicyCmdPrefix_, "match cpuPolicy-mid action send-to-queue 2");
  CmdConfigCoppTrafficPolicy cmd;
  HostInfo hostInfo("testhost");
  traffic_policy::TrafficPolicyArgs args(
      {"match", "cpuPolicy-mid", "action", "send-to-queue", "2"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("cpuPolicy-mid"));

  const auto* mta = findMatchToAction("cpuPolicy-mid");
  ASSERT_NE(mta, nullptr);
  ASSERT_TRUE(mta->action()->sendToQueue().has_value());
  EXPECT_EQ(*mta->action()->sendToQueue()->queueId(), 2);
  // The pre-existing counter action on the same matcher must be preserved.
  ASSERT_TRUE(mta->action()->counter().has_value());
  EXPECT_EQ(*mta->action()->counter(), "cpuPolicy-mid-counter");
}

TEST_F(CmdConfigCoppTestFixture, cpuTrafficPolicy_createsNewMatcherEntry) {
  setupTestableConfigSession(
      cpuTrafficPolicyCmdPrefix_, "match acl-new action set-tc 3");
  CmdConfigCoppTrafficPolicy cmd;
  HostInfo hostInfo("testhost");
  traffic_policy::TrafficPolicyArgs args(
      {"match", "acl-new", "action", "set-tc", "3"});

  cmd.queryClient(hostInfo, args);

  const auto* mta = findMatchToAction("acl-new");
  ASSERT_NE(mta, nullptr);
  ASSERT_TRUE(mta->action()->setTc().has_value());
  EXPECT_EQ(*mta->action()->setTc()->tcValue(), 3);
}

TEST_F(CmdConfigCoppTestFixture, cpuTrafficPolicy_userDefinedTrap) {
  setupTestableConfigSession(
      cpuTrafficPolicyCmdPrefix_,
      "match cpuPolicy-mid action user-defined-trap 9");
  CmdConfigCoppTrafficPolicy cmd;
  HostInfo hostInfo("testhost");
  traffic_policy::TrafficPolicyArgs args(
      {"match", "cpuPolicy-mid", "action", "user-defined-trap", "9"});

  cmd.queryClient(hostInfo, args);

  const auto* mta = findMatchToAction("cpuPolicy-mid");
  ASSERT_NE(mta, nullptr);
  ASSERT_TRUE(mta->action()->userDefinedTrap().has_value());
  EXPECT_EQ(*mta->action()->userDefinedTrap()->queueId(), 9);
}

TEST_F(CmdConfigCoppTestFixture, cpuTrafficPolicy_overwritesSameActionType) {
  setupTestableConfigSession(
      cpuTrafficPolicyCmdPrefix_, "match cpuPolicy-mid action counter cnt-new");
  CmdConfigCoppTrafficPolicy cmd;
  HostInfo hostInfo("testhost");
  traffic_policy::TrafficPolicyArgs args(
      {"match", "cpuPolicy-mid", "action", "counter", "cnt-new"});

  cmd.queryClient(hostInfo, args);

  const auto* mta = findMatchToAction("cpuPolicy-mid");
  ASSERT_NE(mta, nullptr);
  ASSERT_TRUE(mta->action()->counter().has_value());
  EXPECT_EQ(*mta->action()->counter(), "cnt-new");
}

// Adding an action when the device has neither cpuTrafficPolicy nor
// trafficPolicy must create both containers, not throw.
class CmdConfigCoppNoCpuTrafficPolicyFixture : public CmdConfigTestBase {
 public:
  CmdConfigCoppNoCpuTrafficPolicyFixture()
      : CmdConfigTestBase(
            "fboss_copp_config_no_policy_test_%%%%-%%%%-%%%%-%%%%",
            R"({"sw": {}})") {}

 protected:
  const std::string cpuTrafficPolicyCmdPrefix_ = "config copp traffic-policy";
};

TEST_F(
    CmdConfigCoppNoCpuTrafficPolicyFixture,
    cpuTrafficPolicy_createsPolicyFromAbsent) {
  setupTestableConfigSession(
      cpuTrafficPolicyCmdPrefix_, "match acl-new action send-to-queue 2");
  CmdConfigCoppTrafficPolicy cmd;
  HostInfo hostInfo("testhost");
  traffic_policy::TrafficPolicyArgs args(
      {"match", "acl-new", "action", "send-to-queue", "2"});

  cmd.queryClient(hostInfo, args);

  auto& config = ConfigSession::getInstance().getAgentConfig();
  ASSERT_TRUE(config.sw()->cpuTrafficPolicy().has_value());
  const auto& policy = *config.sw()->cpuTrafficPolicy();
  ASSERT_TRUE(policy.trafficPolicy().has_value());
  const auto& matchToActions = *policy.trafficPolicy()->matchToAction();
  ASSERT_EQ(matchToActions.size(), 1);
  EXPECT_EQ(*matchToActions[0].matcher(), "acl-new");
  ASSERT_TRUE(matchToActions[0].action()->sendToQueue().has_value());
  EXPECT_EQ(*matchToActions[0].action()->sendToQueue()->queueId(), 2);
}

} // namespace facebook::fboss
