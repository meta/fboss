// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <cstddef>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/commands/config/QueueConfigUtils.h"
#include "fboss/cli/fboss2/commands/delete/qos/queue_config/CmdDeleteQosQueueConfig.h"
#include "fboss/cli/fboss2/commands/delete/qos/queue_config/CmdDeleteQosQueueConfigQueueId.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

namespace {
constexpr auto kBoundConfig = "bound_qc";
constexpr auto kUnboundConfig = "unbound_qc";
constexpr auto kBoundPort = "eth1/1/1";

utils::QueueConfigName defaultName() {
  return utils::QueueConfigName({utils::kDefaultQueueConfigName});
}
} // namespace

// defaultPortQueues mirrors a typical RSW shape (9 unicast WRR queues). Two
// named configs sit alongside it: one bound to a port, one not, so the
// referential-integrity guard has both a positive and a negative case.
static const std::string kSeedConfig = R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1,
        "name": "eth1/1/1",
        "state": 2,
        "speed": 100000,
        "ingressVlan": 1,
        "portQueueConfigName": "bound_qc"
      },
      {
        "logicalID": 2,
        "name": "eth1/2/1",
        "state": 2,
        "speed": 100000,
        "ingressVlan": 1
      }
    ],
    "defaultPortQueues": [
      {"id": 0, "streamType": 1, "weight": 1, "scheduling": 5},
      {"id": 1, "streamType": 1, "weight": 9, "scheduling": 5},
      {"id": 2, "streamType": 1, "weight": 9, "scheduling": 5},
      {"id": 3, "streamType": 1, "weight": 9, "scheduling": 5},
      {"id": 4, "streamType": 1, "weight": 9, "scheduling": 5},
      {"id": 5, "streamType": 1, "weight": 9, "scheduling": 5},
      {"id": 6, "streamType": 1, "weight": 9, "scheduling": 5},
      {"id": 7, "streamType": 1, "weight": 9, "scheduling": 5},
      {"id": 8, "streamType": 1, "weight": 9, "scheduling": 0}
    ],
    "portQueueConfigs": {
      "bound_qc": [
        {"id": 0, "streamType": 1, "weight": 1, "scheduling": 5}
      ],
      "unbound_qc": [
        {"id": 0, "streamType": 1, "weight": 1, "scheduling": 5},
        {"id": 1, "streamType": 1, "weight": 2, "scheduling": 5}
      ]
    }
  }
})";

class CmdDeleteQosQueueConfigTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteQosQueueConfigTestFixture()
      : CmdConfigTestBase(
            "fboss_del_qc_test_%%%%-%%%%-%%%%-%%%%",
            kSeedConfig) {}

 protected:
  static cfg::SwitchConfig& switchConfig() {
    return *ConfigSession::getInstance().getAgentConfig().sw();
  }

  static const std::vector<cfg::PortQueue>* namedQueues(
      const std::string& name) {
    const auto& configs = *switchConfig().portQueueConfigs();
    auto it = configs.find(name);
    return it == configs.end() ? nullptr : &it->second;
  }

  static const cfg::PortQueue* findDefaultQueue(int16_t queueId) {
    for (const auto& queue : *switchConfig().defaultPortQueues()) {
      if (*queue.id() == queueId) {
        return &queue;
      }
    }
    return nullptr;
  }
};

// ---------------------------------------------------------------------------
// DeleteQueueId argument validation
// ---------------------------------------------------------------------------

TEST_F(CmdDeleteQosQueueConfigTestFixture, validQueueId) {
  EXPECT_NO_THROW(DeleteQueueId({"0"}));
  EXPECT_NO_THROW(DeleteQueueId({"8"}));
  EXPECT_EQ(DeleteQueueId({"5"}).getQueueId(), 5);
}

TEST_F(CmdDeleteQosQueueConfigTestFixture, emptyArgsFails) {
  EXPECT_THROW(DeleteQueueId({}), std::invalid_argument);
}

TEST_F(CmdDeleteQosQueueConfigTestFixture, multipleArgsFails) {
  EXPECT_THROW(DeleteQueueId({"0", "1"}), std::invalid_argument);
}

TEST_F(CmdDeleteQosQueueConfigTestFixture, nonIntegerQueueIdFails) {
  EXPECT_THROW(DeleteQueueId({"abc"}), std::invalid_argument);
  EXPECT_THROW(DeleteQueueId({"1.5"}), std::invalid_argument);
}

// Mirrors the config command's parser bound (utils::kMaxQueueId).
TEST_F(CmdDeleteQosQueueConfigTestFixture, queueIdAboveMaxFails) {
  EXPECT_THROW(DeleteQueueId({"129"}), std::invalid_argument);
  EXPECT_THROW(DeleteQueueId({"200"}), std::invalid_argument);
}

TEST_F(CmdDeleteQosQueueConfigTestFixture, maxQueueIdAccepted) {
  EXPECT_NO_THROW(DeleteQueueId({"128"}));
  EXPECT_EQ(DeleteQueueId({"128"}).getQueueId(), 128);
}

TEST_F(CmdDeleteQosQueueConfigTestFixture, negativeQueueIdFails) {
  EXPECT_THROW(DeleteQueueId({"-1"}), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Single queue removal -- `queue-config <name|default> queue-id <id>`
// ---------------------------------------------------------------------------

TEST_F(CmdDeleteQosQueueConfigTestFixture, deleteDefaultQueue) {
  setupTestableConfigSession("delete qos queue-config default queue-id", "8");

  auto cmd = CmdDeleteQosQueueConfigQueueId();
  auto result = cmd.queryClient(
      localhost(), defaultName(), DeleteQueueId(getCmdArgsList()));

  EXPECT_THAT(result, ::testing::HasSubstr("Successfully deleted"));
  EXPECT_THAT(result, ::testing::HasSubstr("8"));

  EXPECT_EQ(findDefaultQueue(8), nullptr);
  EXPECT_EQ(switchConfig().defaultPortQueues()->size(), 8);
  for (int16_t id = 0; id <= 7; ++id) {
    EXPECT_NE(findDefaultQueue(id), nullptr);
  }
}

TEST_F(CmdDeleteQosQueueConfigTestFixture, deleteMiddleDefaultQueue) {
  setupTestableConfigSession("delete qos queue-config default queue-id", "4");

  auto cmd = CmdDeleteQosQueueConfigQueueId();
  cmd.queryClient(localhost(), defaultName(), DeleteQueueId(getCmdArgsList()));

  EXPECT_EQ(findDefaultQueue(4), nullptr);
  EXPECT_NE(findDefaultQueue(3), nullptr);
  EXPECT_NE(findDefaultQueue(5), nullptr);
}

TEST_F(CmdDeleteQosQueueConfigTestFixture, deleteNonExistentQueueIsNoop) {
  setupTestableConfigSession("delete qos queue-config default queue-id", "15");
  const auto sessionBefore = sessionConfigText();

  auto cmd = CmdDeleteQosQueueConfigQueueId();
  EXPECT_THAT(
      cmd.queryClient(
          localhost(), defaultName(), DeleteQueueId(getCmdArgsList())),
      ::testing::HasSubstr("nothing to delete"));

  EXPECT_EQ(switchConfig().defaultPortQueues()->size(), 9);
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

TEST_F(CmdDeleteQosQueueConfigTestFixture, doubleDeleteIsNoop) {
  setupTestableConfigSession("delete qos queue-config default queue-id", "7");

  auto cmd = CmdDeleteQosQueueConfigQueueId();
  DeleteQueueId queueId(getCmdArgsList());

  cmd.queryClient(localhost(), defaultName(), queueId);
  EXPECT_EQ(findDefaultQueue(7), nullptr);

  const auto sessionBefore = sessionConfigText();
  EXPECT_THAT(
      cmd.queryClient(localhost(), defaultName(), queueId),
      ::testing::HasSubstr("nothing to delete"));
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

// The same command reaches a named config, leaving defaultPortQueues alone.
TEST_F(CmdDeleteQosQueueConfigTestFixture, deleteNamedQueue) {
  setupTestableConfigSession(
      "delete qos queue-config unbound_qc queue-id", "1");

  auto cmd = CmdDeleteQosQueueConfigQueueId();
  auto result = cmd.queryClient(
      localhost(),
      utils::QueueConfigName({kUnboundConfig}),
      DeleteQueueId(getCmdArgsList()));

  EXPECT_THAT(result, ::testing::HasSubstr(kUnboundConfig));

  const auto* queues = namedQueues(kUnboundConfig);
  ASSERT_NE(queues, nullptr);
  ASSERT_EQ(queues->size(), 1);
  EXPECT_EQ(*queues->front().id(), 0);
  EXPECT_EQ(switchConfig().defaultPortQueues()->size(), 9);
}

// A missing config name is a no-op that must not default-construct an entry --
// this is why the command resolves through findQueueConfigList().
TEST_F(CmdDeleteQosQueueConfigTestFixture, deleteQueueInUnknownConfigIsNoop) {
  setupTestableConfigSession(
      "delete qos queue-config no_such_qc queue-id", "0");
  const auto sessionBefore = sessionConfigText();

  auto cmd = CmdDeleteQosQueueConfigQueueId();
  auto result = cmd.queryClient(
      localhost(),
      utils::QueueConfigName({"no_such_qc"}),
      DeleteQueueId(getCmdArgsList()));
  EXPECT_THAT(result, ::testing::HasSubstr("no_such_qc"));
  EXPECT_THAT(result, ::testing::HasSubstr("nothing to delete"));
  EXPECT_EQ(namedQueues("no_such_qc"), nullptr)
      << "a no-op delete must not create the entry";
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

// Removing the last queue leaves the entry in place (empty), so per-queue
// settings can be added back to it later.
TEST_F(CmdDeleteQosQueueConfigTestFixture, emptyingNamedConfigKeepsEntry) {
  setupTestableConfigSession(
      "delete qos queue-config unbound_qc queue-id", "0");

  auto cmd = CmdDeleteQosQueueConfigQueueId();
  cmd.queryClient(
      localhost(),
      utils::QueueConfigName({kUnboundConfig}),
      DeleteQueueId(getCmdArgsList()));
  cmd.queryClient(
      localhost(),
      utils::QueueConfigName({kUnboundConfig}),
      DeleteQueueId({"1"}));

  const auto* queues = namedQueues(kUnboundConfig);
  ASSERT_NE(queues, nullptr) << "entry was removed when its last queue went";
  EXPECT_TRUE(queues->empty());
}

// Deleting one queue from a bound config is allowed: the port's
// portQueueConfigName still resolves, just to a smaller list.
TEST_F(CmdDeleteQosQueueConfigTestFixture, deleteQueueFromBoundConfigAllowed) {
  setupTestableConfigSession("delete qos queue-config bound_qc queue-id", "0");

  auto cmd = CmdDeleteQosQueueConfigQueueId();
  EXPECT_NO_THROW(cmd.queryClient(
      localhost(),
      utils::QueueConfigName({kBoundConfig}),
      DeleteQueueId(getCmdArgsList())));

  const auto* queues = namedQueues(kBoundConfig);
  ASSERT_NE(queues, nullptr);
  EXPECT_TRUE(queues->empty());
}

// ---------------------------------------------------------------------------
// Whole-config removal -- `queue-config <name|default>`
// ---------------------------------------------------------------------------

TEST_F(CmdDeleteQosQueueConfigTestFixture, clearsWholeDefaultList) {
  setupTestableConfigSession("delete qos queue-config", "default");

  auto cmd = CmdDeleteQosQueueConfig();
  auto result =
      cmd.queryClient(localhost(), utils::QueueConfigName(getCmdArgsList()));

  EXPECT_THAT(result, ::testing::HasSubstr("Successfully deleted"));
  EXPECT_TRUE(switchConfig().defaultPortQueues()->empty());
}

TEST_F(CmdDeleteQosQueueConfigTestFixture, clearingEmptyDefaultListIsNoop) {
  setupTestableConfigSession("delete qos queue-config", "default");

  auto cmd = CmdDeleteQosQueueConfig();
  cmd.queryClient(localhost(), defaultName());
  ASSERT_TRUE(switchConfig().defaultPortQueues()->empty());

  const auto sessionBefore = sessionConfigText();
  EXPECT_THAT(
      cmd.queryClient(localhost(), defaultName()),
      ::testing::HasSubstr("nothing to delete"));
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

TEST_F(CmdDeleteQosQueueConfigTestFixture, deletesWholeUnboundNamedConfig) {
  setupTestableConfigSession("delete qos queue-config", kUnboundConfig);

  auto cmd = CmdDeleteQosQueueConfig();
  auto result =
      cmd.queryClient(localhost(), utils::QueueConfigName(getCmdArgsList()));

  EXPECT_THAT(result, ::testing::HasSubstr(kUnboundConfig));
  EXPECT_EQ(namedQueues(kUnboundConfig), nullptr);
  // Untouched neighbours.
  EXPECT_NE(namedQueues(kBoundConfig), nullptr);
  EXPECT_EQ(switchConfig().defaultPortQueues()->size(), 9);
}

// The referential-integrity guard: removing a config a port still names would
// leave that port's portQueueConfigName resolving to nothing.
TEST_F(CmdDeleteQosQueueConfigTestFixture, refusesToDeleteBoundNamedConfig) {
  setupTestableConfigSession("delete qos queue-config", kBoundConfig);

  auto cmd = CmdDeleteQosQueueConfig();
  try {
    cmd.queryClient(localhost(), utils::QueueConfigName(getCmdArgsList()));
    FAIL() << "expected std::runtime_error";
  } catch (const std::runtime_error& e) {
    // The message has to name the offending interface and both remedies,
    // otherwise the user cannot tell which port is blocking them.
    EXPECT_THAT(e.what(), ::testing::HasSubstr(kBoundConfig));
    EXPECT_THAT(e.what(), ::testing::HasSubstr(kBoundPort));
    EXPECT_THAT(e.what(), ::testing::HasSubstr("queue-config"));
  }

  EXPECT_NE(namedQueues(kBoundConfig), nullptr)
      << "a refused delete must leave the config in place";
}

TEST_F(CmdDeleteQosQueueConfigTestFixture, deleteUnknownNamedConfigIsNoop) {
  setupTestableConfigSession("delete qos queue-config", "no_such_qc");
  const auto sessionBefore = sessionConfigText();

  auto cmd = CmdDeleteQosQueueConfig();
  auto result =
      cmd.queryClient(localhost(), utils::QueueConfigName(getCmdArgsList()));
  EXPECT_THAT(result, ::testing::HasSubstr("no_such_qc"));
  EXPECT_THAT(result, ::testing::HasSubstr("nothing to delete"));
  EXPECT_EQ(namedQueues("no_such_qc"), nullptr)
      << "a no-op delete must not create the entry";
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

// ---------------------------------------------------------------------------

TEST_F(CmdDeleteQosQueueConfigTestFixture, printOutput) {
  auto cmd = CmdDeleteQosQueueConfigQueueId();
  std::string msg = "Successfully deleted queue config 'default' queue-id 3";

  std::stringstream buf;
  std::streambuf* old = std::cout.rdbuf(buf.rdbuf());
  cmd.printOutput(msg);
  std::cout.rdbuf(old);

  EXPECT_EQ(buf.str(), msg + "\n");
}

} // namespace facebook::fboss
