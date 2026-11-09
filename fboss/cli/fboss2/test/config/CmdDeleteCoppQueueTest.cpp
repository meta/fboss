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
#include <cstdint>
#include <stdexcept>
#include <string>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/delete/copp/queue/CmdDeleteCoppQueue.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

// Seed config mirrors the cpuQueues + cpuTrafficPolicy shape of a real
// production FBOSS agent.conf: several queues with per-queue weight, name
// and rate caps. Queue 0 is deliberately unreferenced (deletable), queue 9
// is referenced by reason mappings, queue 4 only by the matchToAction
// send-to-queue action, and queue 6 only by the matchToAction
// user-defined-trap action — covering each side of the referenced-queue
// guard.
class CmdDeleteCoppQueueTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteCoppQueueTestFixture()
      : CmdConfigTestBase(
            "fboss_delete_copp_queue_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "cpuQueues": [
      { "id": 9, "streamType": 1, "weight": 4, "scheduling": 0,
        "name": "cpuQueue-high" },
      { "id": 2, "streamType": 1, "weight": 2, "scheduling": 0,
        "name": "cpuQueue-mid",
        "portQueueRate": { "kbitsPerSec": { "minimum": 0, "maximum": 2000 } } },
      { "id": 1, "streamType": 1, "weight": 1, "scheduling": 0,
        "name": "cpuQueue-default",
        "portQueueRate": { "pktsPerSec": { "minimum": 0, "maximum": 1000 } } },
      { "id": 4, "streamType": 1, "weight": 1, "scheduling": 0,
        "name": "cpuQueue-hostif" },
      { "id": 6, "streamType": 1, "weight": 1, "scheduling": 0,
        "name": "cpuQueue-trap" },
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
            "action": { "sendToQueue": { "queueId": 4 },
                        "counter": "cpuPolicy-mid-counter" } },
          { "matcher": "cpuPolicy-trap",
            "action": { "userDefinedTrap": { "queueId": 6 } } }
        ]
      }
    }
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "delete copp queue";

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
};

// =============================================================
// CoppQueueDeleteArgs validation tests
// =============================================================

TEST_F(CmdDeleteCoppQueueTestFixture, argsIdOnly) {
  CoppQueueDeleteArgs a({"9"});
  EXPECT_EQ(a.getQueueId(), 9);
}

TEST_F(CmdDeleteCoppQueueTestFixture, argsInvalid) {
  // Empty
  EXPECT_THROW(CoppQueueDeleteArgs({}), std::invalid_argument);

  // Bad queue id
  EXPECT_THROW(CoppQueueDeleteArgs({"abc"}), std::invalid_argument);
  EXPECT_THROW(CoppQueueDeleteArgs({"-1"}), std::invalid_argument);
  EXPECT_THROW(CoppQueueDeleteArgs({"999"}), std::invalid_argument);

  // No per-attribute sub-commands are accepted
  EXPECT_THROW(CoppQueueDeleteArgs({"9", "name"}), std::invalid_argument);
}

// =============================================================
// Whole-queue delete
// =============================================================

TEST_F(CmdDeleteCoppQueueTestFixture, deleteWholeQueue) {
  setupTestableConfigSession(cmdPrefix_, "0");
  CmdDeleteCoppQueue cmd;
  HostInfo hostInfo("testhost");
  CoppQueueDeleteArgs args({"0"});

  ASSERT_NE(findQueue(0), nullptr);
  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("Deleted queue 0"));

  EXPECT_EQ(findQueue(0), nullptr);
  // Siblings untouched.
  EXPECT_NE(findQueue(9), nullptr);
  EXPECT_NE(findQueue(2), nullptr);
  EXPECT_NE(findQueue(1), nullptr);
}

TEST_F(CmdDeleteCoppQueueTestFixture, deleteQueueReferencedByReasonThrows) {
  setupTestableConfigSession(cmdPrefix_, "9");
  CmdDeleteCoppQueue cmd;
  HostInfo hostInfo("testhost");
  // Queue 9 is referenced by the NDP, ARP, and BGP reason mappings.
  CoppQueueDeleteArgs args({"9"});

  try {
    cmd.queryClient(hostInfo, args);
    FAIL() << "Expected std::runtime_error";
  } catch (const std::runtime_error& e) {
    EXPECT_THAT(e.what(), HasSubstr("Cannot delete queue 9"));
    EXPECT_THAT(e.what(), HasSubstr("reason NDP"));
    EXPECT_THAT(e.what(), HasSubstr("reason ARP"));
    EXPECT_THAT(e.what(), HasSubstr("reason BGP"));
  }
  // Config untouched on error.
  EXPECT_NE(findQueue(9), nullptr);
}

TEST_F(CmdDeleteCoppQueueTestFixture, deleteQueueReferencedByMatcherThrows) {
  setupTestableConfigSession(cmdPrefix_, "4");
  CmdDeleteCoppQueue cmd;
  HostInfo hostInfo("testhost");
  // Queue 4 is referenced only by the cpuPolicy-mid send-to-queue action.
  CoppQueueDeleteArgs args({"4"});

  try {
    cmd.queryClient(hostInfo, args);
    FAIL() << "Expected std::runtime_error";
  } catch (const std::runtime_error& e) {
    EXPECT_THAT(e.what(), HasSubstr("Cannot delete queue 4"));
    EXPECT_THAT(e.what(), HasSubstr("matcher 'cpuPolicy-mid' send-to-queue"));
  }
  EXPECT_NE(findQueue(4), nullptr);
}

TEST_F(
    CmdDeleteCoppQueueTestFixture,
    deleteQueueReferencedByUserDefinedTrapThrows) {
  setupTestableConfigSession(cmdPrefix_, "6");
  CmdDeleteCoppQueue cmd;
  HostInfo hostInfo("testhost");
  // Queue 6 is referenced only by the cpuPolicy-trap user-defined-trap
  // action.
  CoppQueueDeleteArgs args({"6"});

  try {
    cmd.queryClient(hostInfo, args);
    FAIL() << "Expected std::runtime_error";
  } catch (const std::runtime_error& e) {
    EXPECT_THAT(e.what(), HasSubstr("Cannot delete queue 6"));
    EXPECT_THAT(
        e.what(), HasSubstr("matcher 'cpuPolicy-trap' user-defined-trap"));
  }
  EXPECT_NE(findQueue(6), nullptr);
}

TEST_F(CmdDeleteCoppQueueTestFixture, deleteQueueNotFoundIsNoop) {
  setupTestableConfigSession(cmdPrefix_, "5");
  const auto sessionBefore = sessionConfigText();
  CmdDeleteCoppQueue cmd;
  HostInfo hostInfo("testhost");
  CoppQueueDeleteArgs args({"5"});

  EXPECT_THAT(cmd.queryClient(hostInfo, args), HasSubstr("nothing to delete"));
  // The seeded queues are untouched.
  EXPECT_NE(findQueue(9), nullptr);
  EXPECT_NE(findQueue(0), nullptr);
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

} // namespace facebook::fboss
