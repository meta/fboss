// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/qos/queue_config/CmdConfigQosQueueConfigQueueId.h"
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
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

namespace {
// Most cases below drive the reserved `default` name, i.e. the
// SwitchConfig::defaultPortQueues branch of queueConfigListForWrite. The named
// portQueueConfigs branch is covered by the NamedQueueConfig tests at the end.
utils::QueueConfigName kDefaultName() {
  return utils::QueueConfigName({utils::kDefaultQueueConfigName});
}
} // namespace

// Seed JSON mirrors a typical RSW defaultPortQueues shape with 9 unicast
// queues (ids 0-8) at weighted round-robin scheduling with varying weights.
static const std::string kSeedConfig = R"({
  "sw": {
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
    ]
  }
})";

class CmdConfigQosQueueConfigTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigQosQueueConfigTestFixture()
      : CmdConfigTestBase("fboss_dqc_test_%%%%-%%%%-%%%%-%%%%", kSeedConfig) {}

 protected:
  const std::string cmdPrefix_ = "config qos queue-config default queue-id";

  static const cfg::PortQueue* findQueue(int16_t queueId) {
    const auto& queues = *ConfigSession::getInstance()
                              .getAgentConfig()
                              .sw()
                              ->defaultPortQueues();
    for (const auto& queue : queues) {
      if (*queue.id() == queueId) {
        return &queue;
      }
    }
    return nullptr;
  }

  // Run the command with the given argument string and return the target queue.
  const cfg::PortQueue* runAndFind(const std::string& args, int16_t queueId) {
    setupTestableConfigSession(cmdPrefix_, args);
    auto cmd = CmdConfigQosQueueConfigQueueId();
    utils::QueueIdAndAttributes config(getCmdArgsList());
    cmd.queryClient(localhost(), kDefaultName(), config);
    return findQueue(queueId);
  }
};

// Arg validation: minimum valid input
TEST_F(CmdConfigQosQueueConfigTestFixture, validMinimalArgs) {
  EXPECT_NO_THROW(utils::QueueIdAndAttributes({"0", "weight", "5"}));
  EXPECT_NO_THROW(utils::QueueIdAndAttributes({"7", "reserved-bytes", "1024"}));
  EXPECT_NO_THROW(utils::QueueIdAndAttributes({"3", "scheduling", "WRR"}));
}

// Arg validation: multiple attributes
TEST_F(CmdConfigQosQueueConfigTestFixture, validMultipleAttrs) {
  EXPECT_NO_THROW(
      utils::QueueIdAndAttributes(
          {"1", "weight", "10", "reserved-bytes", "2048"}));
  EXPECT_NO_THROW(
      utils::QueueIdAndAttributes(
          {"2", "scheduling", "SP", "shared-bytes", "4096"}));
}

// Arg validation: empty args
TEST_F(CmdConfigQosQueueConfigTestFixture, emptyArgsFails) {
  EXPECT_THROW(utils::QueueIdAndAttributes({}), std::invalid_argument);
}

// Arg validation: attribute missing value
TEST_F(CmdConfigQosQueueConfigTestFixture, attrMissingValueFails) {
  EXPECT_THROW(
      utils::QueueIdAndAttributes({"0", "weight"}), std::invalid_argument);
}

// Arg validation: negative queue id
TEST_F(CmdConfigQosQueueConfigTestFixture, negativeQueueIdFails) {
  EXPECT_THROW(
      utils::QueueIdAndAttributes({"-1", "weight", "5"}),
      std::invalid_argument);
}

// Modify weight on an existing queue in defaultPortQueues
// The SAI scheduler stores WRR weight as a uint8; the bound lives in the
// shared applyPortQueueConfig so the qos path rejects it too, not just copp.
TEST_F(CmdConfigQosQueueConfigTestFixture, setWeightAboveUint8Throws) {
  setupTestableConfigSession(cmdPrefix_, "1 weight 300");

  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());

  EXPECT_THROW(
      cmd.queryClient(localhost(), kDefaultName(), config),
      std::invalid_argument);
}

// SAI documents weight 0 as invalid, and the agent crashes on it.
TEST_F(CmdConfigQosQueueConfigTestFixture, setWeightZeroThrows) {
  setupTestableConfigSession(cmdPrefix_, "1 weight 0");

  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());

  EXPECT_THROW(
      cmd.queryClient(localhost(), kDefaultName(), config),
      std::invalid_argument);
}

TEST_F(CmdConfigQosQueueConfigTestFixture, setWeightExistingQueue) {
  setupTestableConfigSession(cmdPrefix_, "1 weight 20");

  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());

  auto result = cmd.queryClient(localhost(), kDefaultName(), config);

  EXPECT_THAT(result, ::testing::HasSubstr("Successfully configured"));
  EXPECT_THAT(result, ::testing::HasSubstr("1"));

  auto& agentConfig = ConfigSession::getInstance().getAgentConfig();
  auto& switchConfig = *agentConfig.sw();
  const auto& queues = *switchConfig.defaultPortQueues();

  const cfg::PortQueue* q = nullptr;
  for (const auto& queue : queues) {
    if (*queue.id() == 1) {
      q = &queue;
      break;
    }
  }
  ASSERT_NE(q, nullptr);
  ASSERT_TRUE(q->weight().has_value());
  EXPECT_EQ(*q->weight(), 20);
}

// Set reserved-bytes on an existing queue
TEST_F(CmdConfigQosQueueConfigTestFixture, setReservedBytesExistingQueue) {
  setupTestableConfigSession(cmdPrefix_, "0 reserved-bytes 1024");

  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());

  cmd.queryClient(localhost(), kDefaultName(), config);

  auto& agentConfig = ConfigSession::getInstance().getAgentConfig();
  const auto& queues = *agentConfig.sw()->defaultPortQueues();

  const cfg::PortQueue* q = nullptr;
  for (const auto& queue : queues) {
    if (*queue.id() == 0) {
      q = &queue;
      break;
    }
  }
  ASSERT_NE(q, nullptr);
  ASSERT_TRUE(q->reservedBytes().has_value());
  EXPECT_EQ(*q->reservedBytes(), 1024);
}

// Create a new queue entry when queue-id is not in defaultPortQueues
TEST_F(CmdConfigQosQueueConfigTestFixture, createsNewQueueEntry) {
  setupTestableConfigSession(cmdPrefix_, "15 weight 3");

  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());

  cmd.queryClient(localhost(), kDefaultName(), config);

  const auto& queues =
      *ConfigSession::getInstance().getAgentConfig().sw()->defaultPortQueues();

  const cfg::PortQueue* q = nullptr;
  for (const auto& queue : queues) {
    if (*queue.id() == 15) {
      q = &queue;
      break;
    }
  }
  ASSERT_NE(q, nullptr);
  ASSERT_TRUE(q->weight().has_value());
  EXPECT_EQ(*q->weight(), 3);
}

// Set scheduling to STRICT_PRIORITY via short name
TEST_F(CmdConfigQosQueueConfigTestFixture, setSchedulingShortName) {
  setupTestableConfigSession(cmdPrefix_, "8 scheduling SP");

  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());

  cmd.queryClient(localhost(), kDefaultName(), config);

  const auto& queues =
      *ConfigSession::getInstance().getAgentConfig().sw()->defaultPortQueues();

  const cfg::PortQueue* q = nullptr;
  for (const auto& queue : queues) {
    if (*queue.id() == 8) {
      q = &queue;
      break;
    }
  }
  ASSERT_NE(q, nullptr);
  EXPECT_EQ(*q->scheduling(), cfg::QueueScheduling::STRICT_PRIORITY);
}

// Full AQM grammar: ECN behavior + linear detection thresholds
TEST_F(CmdConfigQosQueueConfigTestFixture, setAqmEcnWithLinearDetection) {
  setupTestableConfigSession(
      cmdPrefix_,
      "6 active-queue-management detection linear minimum-length 40000 "
      "maximum-length 40000 congestion-behavior ECN");

  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());

  cmd.queryClient(localhost(), kDefaultName(), config);

  const auto& queues =
      *ConfigSession::getInstance().getAgentConfig().sw()->defaultPortQueues();
  const cfg::PortQueue* q = nullptr;
  for (const auto& queue : queues) {
    if (*queue.id() == 6) {
      q = &queue;
      break;
    }
  }
  ASSERT_NE(q, nullptr);
  ASSERT_TRUE(q->aqms().has_value());
  ASSERT_EQ(q->aqms()->size(), 1);
  const auto& aqm = q->aqms()->front();
  EXPECT_EQ(*aqm.behavior(), cfg::QueueCongestionBehavior::ECN);
  ASSERT_TRUE(aqm.detection()->linear().has_value());
  EXPECT_EQ(*aqm.detection()->linear()->minimumLength(), 40000);
  EXPECT_EQ(*aqm.detection()->linear()->maximumLength(), 40000);
}

// Unknown linear detection attribute error enumerates the valid attributes
TEST_F(CmdConfigQosQueueConfigTestFixture, unknownLinearAttrFails) {
  setupTestableConfigSession(
      cmdPrefix_,
      "6 active-queue-management congestion-behavior ECN "
      "detection linear bogus-attr 5");

  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());

  try {
    cmd.queryClient(localhost(), kDefaultName(), config);
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), ::testing::HasSubstr("bogus-attr"));
    EXPECT_THAT(e.what(), ::testing::HasSubstr("minimum-length"));
    EXPECT_THAT(e.what(), ::testing::HasSubstr("maximum-length"));
    EXPECT_THAT(e.what(), ::testing::HasSubstr("probability"));
  }
}

// Linear detection attribute with a missing trailing value fails
TEST_F(CmdConfigQosQueueConfigTestFixture, linearAttrMissingValueFails) {
  setupTestableConfigSession(
      cmdPrefix_, "6 active-queue-management detection linear minimum-length");

  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());

  EXPECT_THROW(
      cmd.queryClient(localhost(), kDefaultName(), config),
      std::invalid_argument);
}

// Invalid congestion-behavior error enumerates the thrift enum values
TEST_F(CmdConfigQosQueueConfigTestFixture, invalidCongestionBehaviorFails) {
  setupTestableConfigSession(
      cmdPrefix_, "6 active-queue-management congestion-behavior BOGUS");

  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());

  try {
    cmd.queryClient(localhost(), kDefaultName(), config);
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), ::testing::HasSubstr("EARLY_DROP"));
    EXPECT_THAT(e.what(), ::testing::HasSubstr("ECN"));
  }
}

// Non-numeric values for integer attributes fail with a clean CLI error
TEST_F(CmdConfigQosQueueConfigTestFixture, nonNumericValueFails) {
  setupTestableConfigSession(cmdPrefix_, "0 weight abc");

  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());

  try {
    cmd.queryClient(localhost(), kDefaultName(), config);
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), ::testing::HasSubstr("must be an integer"));
  }
}

// Unknown attribute fails in queryClient
TEST_F(CmdConfigQosQueueConfigTestFixture, unknownAttrFails) {
  setupTestableConfigSession(cmdPrefix_, "0 unknown-attr 99");

  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());

  EXPECT_THROW(
      cmd.queryClient(localhost(), kDefaultName(), config),
      std::invalid_argument);
}

// Negative integer value fails with the non-negative error (distinct from the
// non-numeric branch covered above)
TEST_F(CmdConfigQosQueueConfigTestFixture, negativeValueFails) {
  setupTestableConfigSession(cmdPrefix_, "0 weight -5");

  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());

  try {
    cmd.queryClient(localhost(), kDefaultName(), config);
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), ::testing::HasSubstr("must be non-negative"));
  }
}

// Set shared-bytes through queryClient and verify the stored field
TEST_F(CmdConfigQosQueueConfigTestFixture, setSharedBytesExistingQueue) {
  const auto* q = runAndFind("0 shared-bytes 4096", 0);
  ASSERT_NE(q, nullptr);
  ASSERT_TRUE(q->sharedBytes().has_value());
  EXPECT_EQ(*q->sharedBytes(), 4096);
}

// Set scaling-factor (enum attribute) and verify the stored field
TEST_F(CmdConfigQosQueueConfigTestFixture, setScalingFactor) {
  const auto* q = runAndFind("0 scaling-factor ONE_HALF", 0);
  ASSERT_NE(q, nullptr);
  ASSERT_TRUE(q->scalingFactor().has_value());
  EXPECT_EQ(*q->scalingFactor(), cfg::MMUScalingFactor::ONE_HALF);
}

// Set stream-type (enum attribute) and verify the stored field
TEST_F(CmdConfigQosQueueConfigTestFixture, setStreamType) {
  const auto* q = runAndFind("0 stream-type UNICAST", 0);
  ASSERT_NE(q, nullptr);
  EXPECT_EQ(*q->streamType(), cfg::StreamType::UNICAST);
}

// Set buffer-pool-name (string attribute) and verify the stored field
TEST_F(CmdConfigQosQueueConfigTestFixture, setBufferPoolName) {
  const auto* q = runAndFind("0 buffer-pool-name egress_pool", 0);
  ASSERT_NE(q, nullptr);
  ASSERT_TRUE(q->bufferPoolName().has_value());
  EXPECT_EQ(*q->bufferPoolName(), "egress_pool");
}

// Set scheduling via full thrift enum name (not just the short SP alias)
TEST_F(CmdConfigQosQueueConfigTestFixture, setSchedulingFullEnumName) {
  const auto* q = runAndFind("0 scheduling DEFICIT_ROUND_ROBIN", 0);
  ASSERT_NE(q, nullptr);
  EXPECT_EQ(*q->scheduling(), cfg::QueueScheduling::DEFICIT_ROUND_ROBIN);
}

// Set active-queue-management with EARLY_DROP behavior and probability, and
// verify all three linear fields plus behavior are stored
TEST_F(CmdConfigQosQueueConfigTestFixture, setAqmEarlyDropWithProbability) {
  const auto* q = runAndFind(
      "6 active-queue-management detection linear minimum-length 1000 "
      "maximum-length 5000 probability 80 congestion-behavior EARLY_DROP",
      6);
  ASSERT_NE(q, nullptr);
  ASSERT_TRUE(q->aqms().has_value());
  ASSERT_EQ(q->aqms()->size(), 1);
  const auto& aqm = q->aqms()->front();
  EXPECT_EQ(*aqm.behavior(), cfg::QueueCongestionBehavior::EARLY_DROP);
  ASSERT_TRUE(aqm.detection()->linear().has_value());
  EXPECT_EQ(*aqm.detection()->linear()->minimumLength(), 1000);
  EXPECT_EQ(*aqm.detection()->linear()->maximumLength(), 5000);
  EXPECT_EQ(*aqm.detection()->linear()->probability(), 80);
}

// printOutput emits the message
TEST_F(CmdConfigQosQueueConfigTestFixture, printOutput) {
  auto cmd = CmdConfigQosQueueConfigQueueId();
  std::string msg = "Successfully configured queue-config 'default' queue-id 3";

  std::stringstream buf;
  std::streambuf* old = std::cout.rdbuf(buf.rdbuf());
  cmd.printOutput(msg);
  std::cout.rdbuf(old);

  EXPECT_EQ(buf.str(), msg + "\n");
}

// Seed with queue 6 already carrying a fully-populated AQM entry (ECN behavior
// + linear detection), to exercise the merge-from-existing path: editing one
// linear field must preserve behavior and the other linear fields.
static const std::string kSeedConfigWithAqm = R"({
  "sw": {
    "defaultPortQueues": [
      {
        "id": 6, "streamType": 1, "weight": 9, "scheduling": 5,
        "aqms": [
          {
            "behavior": 1,
            "detection": {"linear": {
              "minimumLength": 100, "maximumLength": 200, "probability": 50
            }}
          }
        ]
      }
    ]
  }
})";

class CmdConfigQosQueueConfigAqmSeedFixture : public CmdConfigTestBase {
 public:
  CmdConfigQosQueueConfigAqmSeedFixture()
      : CmdConfigTestBase(
            "fboss_dqc_aqm_test_%%%%-%%%%-%%%%-%%%%",
            kSeedConfigWithAqm) {}
};

// Editing one linear threshold of an existing AQM entry (selected by its
// behavior) preserves that entry's behavior and its untouched linear fields.
TEST_F(CmdConfigQosQueueConfigAqmSeedFixture, aqmEditPreservesExistingFields) {
  setupTestableConfigSession(
      "config qos queue-config default queue-id",
      "6 active-queue-management congestion-behavior ECN "
      "detection linear minimum-length 1234");

  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());
  cmd.queryClient(localhost(), kDefaultName(), config);

  const auto& queues =
      *ConfigSession::getInstance().getAgentConfig().sw()->defaultPortQueues();
  const cfg::PortQueue* q = nullptr;
  for (const auto& queue : queues) {
    if (*queue.id() == 6) {
      q = &queue;
      break;
    }
  }
  ASSERT_NE(q, nullptr);
  ASSERT_TRUE(q->aqms().has_value());
  ASSERT_EQ(q->aqms()->size(), 1);
  const auto& aqm = q->aqms()->front();
  // behavior preserved from the seed
  EXPECT_EQ(*aqm.behavior(), cfg::QueueCongestionBehavior::ECN);
  ASSERT_TRUE(aqm.detection()->linear().has_value());
  // edited field updated
  EXPECT_EQ(*aqm.detection()->linear()->minimumLength(), 1234);
  // untouched fields preserved from the seed
  EXPECT_EQ(*aqm.detection()->linear()->maximumLength(), 200);
  EXPECT_EQ(*aqm.detection()->linear()->probability(), 50);
}

// Look up a defaultPortQueues entry in the live ConfigSession by id (the
// AqmSeed fixture is not derived from the main fixture, so it can't use that
// fixture's protected findQueue helper).
static const cfg::PortQueue* queueInSession(int16_t queueId) {
  const auto& queues =
      *ConfigSession::getInstance().getAgentConfig().sw()->defaultPortQueues();
  for (const auto& queue : queues) {
    if (*queue.id() == queueId) {
      return &queue;
    }
  }
  return nullptr;
}

// Returns the aqms entry with the given behavior, or nullptr. Lets coexistence
// tests assert per-behavior instead of relying on positional aqms[0]/[1].
static const cfg::ActiveQueueManagement* findAqmByBehavior(
    const cfg::PortQueue& q,
    cfg::QueueCongestionBehavior behavior) {
  if (!q.aqms().has_value()) {
    return nullptr;
  }
  for (const auto& aqm : *q.aqms()) {
    if (*aqm.behavior() == behavior) {
      return &aqm;
    }
  }
  return nullptr;
}

// Adding an EARLY_DROP policy to a queue that already carries an ECN entry must
// APPEND (yielding two entries) rather than overwrite the ECN one — this is the
// core coexistence behavior of selectOrCreateAqm.
TEST_F(CmdConfigQosQueueConfigAqmSeedFixture, aqmCoexistEcnAndEarlyDrop) {
  setupTestableConfigSession(
      "config qos queue-config default queue-id",
      "6 active-queue-management congestion-behavior EARLY_DROP "
      "detection linear minimum-length 300 maximum-length 400 probability 25");

  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());
  cmd.queryClient(localhost(), kDefaultName(), config);

  const cfg::PortQueue* q = queueInSession(6);
  ASSERT_NE(q, nullptr);
  ASSERT_TRUE(q->aqms().has_value());
  ASSERT_EQ(q->aqms()->size(), 2)
      << "EARLY_DROP should append, not clobber ECN";

  // The pre-existing ECN entry is untouched.
  const auto* ecn = findAqmByBehavior(*q, cfg::QueueCongestionBehavior::ECN);
  ASSERT_NE(ecn, nullptr) << "seeded ECN entry was lost";
  ASSERT_TRUE(ecn->detection()->linear().has_value());
  EXPECT_EQ(*ecn->detection()->linear()->minimumLength(), 100);
  EXPECT_EQ(*ecn->detection()->linear()->maximumLength(), 200);
  EXPECT_EQ(*ecn->detection()->linear()->probability(), 50);

  // The new EARLY_DROP entry carries its own detection.
  const auto* ed =
      findAqmByBehavior(*q, cfg::QueueCongestionBehavior::EARLY_DROP);
  ASSERT_NE(ed, nullptr) << "EARLY_DROP entry was not created";
  ASSERT_TRUE(ed->detection()->linear().has_value());
  EXPECT_EQ(*ed->detection()->linear()->minimumLength(), 300);
  EXPECT_EQ(*ed->detection()->linear()->maximumLength(), 400);
  EXPECT_EQ(*ed->detection()->linear()->probability(), 25);
}

// A bare `congestion-behavior <x>` (no detection args) must leave the selected
// entry's existing detection intact — the sawDetectionArgs no-clobber guard.
TEST_F(
    CmdConfigQosQueueConfigAqmSeedFixture,
    aqmBareBehaviorPreservesDetection) {
  setupTestableConfigSession(
      "config qos queue-config default queue-id",
      "6 active-queue-management congestion-behavior ECN");

  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());
  cmd.queryClient(localhost(), kDefaultName(), config);

  const cfg::PortQueue* q = queueInSession(6);
  ASSERT_NE(q, nullptr);
  ASSERT_TRUE(q->aqms().has_value());
  ASSERT_EQ(q->aqms()->size(), 1);
  const auto& aqm = q->aqms()->front();
  EXPECT_EQ(*aqm.behavior(), cfg::QueueCongestionBehavior::ECN);
  ASSERT_TRUE(aqm.detection()->linear().has_value())
      << "existing detection was clobbered by a bare congestion-behavior";
  EXPECT_EQ(*aqm.detection()->linear()->minimumLength(), 100);
  EXPECT_EQ(*aqm.detection()->linear()->maximumLength(), 200);
  EXPECT_EQ(*aqm.detection()->linear()->probability(), 50);
}

// An AQM edit that names no congestion-behavior is ambiguous (a queue can hold
// both an ECN and an EARLY_DROP entry) and must be rejected, not silently
// applied to aqms.front() or committed as a phantom EARLY_DROP entry.
TEST_F(CmdConfigQosQueueConfigTestFixture, aqmRequiresCongestionBehavior) {
  setupTestableConfigSession(
      cmdPrefix_,
      "6 active-queue-management detection linear minimum-length 100");

  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());

  try {
    cmd.queryClient(localhost(), kDefaultName(), config);
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), ::testing::HasSubstr("congestion-behavior"));
  }
}

// Two congestion-behavior tokens in one edit are ambiguous (selection keys on
// the first, assignment on the last) and must be rejected.
TEST_F(
    CmdConfigQosQueueConfigTestFixture,
    aqmDuplicateCongestionBehaviorFails) {
  setupTestableConfigSession(
      cmdPrefix_,
      "6 active-queue-management congestion-behavior ECN "
      "congestion-behavior EARLY_DROP");

  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());
  EXPECT_THROW(
      cmd.queryClient(localhost(), kDefaultName(), config),
      std::invalid_argument);
}

// A queue-id with no attributes at all is rejected by queryClient rather than
// staging an empty edit.
TEST_F(CmdConfigQosQueueConfigTestFixture, noAttributesFails) {
  setupTestableConfigSession(cmdPrefix_, "0");
  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());
  try {
    cmd.queryClient(localhost(), kDefaultName(), config);
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), ::testing::HasSubstr("At least one attribute"));
  }
}

// ---------------------------------------------------------------------------
// Named queue configs: the portQueueConfigs branch of queueConfigListForWrite.
// Everything above drives the reserved `default` name.
// ---------------------------------------------------------------------------

namespace {
constexpr auto kNamedConfigName = "rsw_queues";

utils::QueueConfigName kNamedName() {
  return utils::QueueConfigName({kNamedConfigName});
}

const std::vector<cfg::PortQueue>* namedQueuesInSession(
    const std::string& name) {
  const auto& configs =
      *ConfigSession::getInstance().getAgentConfig().sw()->portQueueConfigs();
  auto it = configs.find(name);
  return it == configs.end() ? nullptr : &it->second;
}

size_t defaultQueueCount() {
  return ConfigSession::getInstance()
      .getAgentConfig()
      .sw()
      ->defaultPortQueues()
      ->size();
}
} // namespace

// A named config lands in portQueueConfigs, creating the entry on demand, and
// leaves defaultPortQueues alone.
TEST_F(CmdConfigQosQueueConfigTestFixture, namedConfigCreatesEntry) {
  // Must come before any ConfigSession::getInstance() call: until the session
  // is set up, getInstance() resolves against the real /etc/coop/cli.
  setupTestableConfigSession(
      "config qos queue-config rsw_queues queue-id", "3 weight 7");
  const auto defaultCountBefore = defaultQueueCount();

  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());
  auto result = cmd.queryClient(localhost(), kNamedName(), config);

  EXPECT_THAT(result, ::testing::HasSubstr(kNamedConfigName));

  const auto* queues = namedQueuesInSession(kNamedConfigName);
  ASSERT_NE(queues, nullptr) << "named entry was not created";
  ASSERT_EQ(queues->size(), 1);
  EXPECT_EQ(*queues->front().id(), 3);
  ASSERT_TRUE(queues->front().weight().has_value());
  EXPECT_EQ(*queues->front().weight(), 7);

  EXPECT_EQ(defaultQueueCount(), defaultCountBefore)
      << "a named edit must not touch defaultPortQueues";
}

// `default` is reserved: it routes to defaultPortQueues and must never
// materialize a portQueueConfigs["default"] key, which
// Port::portQueueConfigName could otherwise be pointed at.
TEST_F(CmdConfigQosQueueConfigTestFixture, defaultNameNeverCreatesNamedEntry) {
  setupTestableConfigSession(cmdPrefix_, "1 weight 20");
  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());
  cmd.queryClient(localhost(), kDefaultName(), config);

  EXPECT_EQ(namedQueuesInSession(utils::kDefaultQueueConfigName), nullptr)
      << "'default' must route to defaultPortQueues, not portQueueConfigs";
}

// rate-limit <kbps|pps> [<min>] <max> sets the matching branch of the
// PortQueueRate union -- e.g. pinning a queue-per-host queue to 12.5 Gbps.
TEST_F(CmdConfigQosQueueConfigTestFixture, setRateLimitKbps) {
  const auto* q = runAndFind("0 rate-limit kbps 12500000 12500000", 0);
  ASSERT_NE(q, nullptr);
  ASSERT_TRUE(q->portQueueRate().has_value());
  ASSERT_TRUE(q->portQueueRate()->kbitsPerSec().has_value());
  EXPECT_EQ(*q->portQueueRate()->kbitsPerSec()->minimum(), 12500000);
  EXPECT_EQ(*q->portQueueRate()->kbitsPerSec()->maximum(), 12500000);
  // The union holds one unit at a time.
  EXPECT_FALSE(q->portQueueRate()->pktsPerSec().has_value());
}

TEST_F(CmdConfigQosQueueConfigTestFixture, setRateLimitPps) {
  const auto* q = runAndFind("1 rate-limit pps 1000 50000", 1);
  ASSERT_NE(q, nullptr);
  ASSERT_TRUE(q->portQueueRate()->pktsPerSec().has_value());
  EXPECT_EQ(*q->portQueueRate()->pktsPerSec()->minimum(), 1000);
  EXPECT_EQ(*q->portQueueRate()->pktsPerSec()->maximum(), 50000);
}

// <min> is optional: the two-token form is what `config copp queue` has always
// accepted, and it means a zero floor rather than an error.
TEST_F(CmdConfigQosQueueConfigTestFixture, setRateLimitMaxOnly) {
  const auto* q = runAndFind("1 rate-limit pps 50000", 1);
  ASSERT_NE(q, nullptr);
  ASSERT_TRUE(q->portQueueRate()->pktsPerSec().has_value());
  EXPECT_EQ(*q->portQueueRate()->pktsPerSec()->minimum(), 0);
  EXPECT_EQ(*q->portQueueRate()->pktsPerSec()->maximum(), 50000);
}

// An attribute name is never a bare integer, so the optional <min> lookahead
// must not swallow the next attribute's keyword.
TEST_F(CmdConfigQosQueueConfigTestFixture, rateLimitMaxOnlyFollowedByAttr) {
  const auto* q = runAndFind("1 rate-limit pps 50000 weight 7", 1);
  ASSERT_NE(q, nullptr);
  EXPECT_EQ(*q->portQueueRate()->pktsPerSec()->maximum(), 50000);
  EXPECT_EQ(*q->weight(), 7);
}

// bandwidth-burst is intentionally not offered: PortQueue carries the two
// burst fields, but SaiSchedulerTraits::AdapterHostKey has no burst-rate
// member, so they would never be programmed. Reject them like any unknown
// attribute rather than accepting a setting that does nothing.
TEST_F(CmdConfigQosQueueConfigTestFixture, bandwidthBurstIsNotAnAttribute) {
  // The two-value form a user would reach for parses as `bandwidth-burst 1`
  // followed by a stray `1`, which the walk rejects for want of a value.
  EXPECT_THROW(
      utils::QueueIdAndAttributes({"0", "bandwidth-burst", "1", "1"}),
      std::invalid_argument);

  // With a single value it parses as an ordinary pair and is rejected one
  // layer later, by applyPortQueueConfig's unknown-attribute branch.
  setupTestableConfigSession(cmdPrefix_, "0 bandwidth-burst 1");
  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());
  EXPECT_THROW(
      cmd.queryClient(localhost(), kDefaultName(), config),
      std::invalid_argument);
}

// Nothing downstream re-checks the range, so an inverted one is refused here.
TEST_F(CmdConfigQosQueueConfigTestFixture, invertedRangeFails) {
  setupTestableConfigSession(cmdPrefix_, "0 rate-limit kbps 200 100");
  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());
  EXPECT_THROW(
      cmd.queryClient(localhost(), kDefaultName(), config),
      std::invalid_argument);
}

TEST_F(CmdConfigQosQueueConfigTestFixture, multiTokenAttrArityFails) {
  // rate-limit needs a unit and at least a max.
  EXPECT_THROW(
      utils::QueueIdAndAttributes({"0", "rate-limit"}), std::invalid_argument);
  EXPECT_THROW(
      utils::QueueIdAndAttributes({"0", "rate-limit", "kbps"}),
      std::invalid_argument);
}

// A bad unit is rejected at apply time, naming the valid units.
TEST_F(CmdConfigQosQueueConfigTestFixture, badRateLimitUnitFails) {
  setupTestableConfigSession(cmdPrefix_, "0 rate-limit mbps 100");
  auto cmd = CmdConfigQosQueueConfigQueueId();
  utils::QueueIdAndAttributes config(getCmdArgsList());
  EXPECT_THROW(
      cmd.queryClient(localhost(), kDefaultName(), config),
      std::invalid_argument);
}

// The duplicate guard lives in the shared walk, so the qos family gets it too
// -- it never had one of its own.
TEST_F(CmdConfigQosQueueConfigTestFixture, duplicateAttrFails) {
  EXPECT_THROW(
      utils::QueueIdAndAttributes({"0", "weight", "4", "weight", "5"}),
      std::invalid_argument);
  EXPECT_THROW(
      utils::QueueIdAndAttributes(
          {"0", "rate-limit", "kbps", "100", "rate-limit", "pps", "50"}),
      std::invalid_argument);
}

TEST_F(CmdConfigQosQueueConfigTestFixture, setNameAndMaxDynamicSharedBytes) {
  const auto* q =
      runAndFind("0 name queue_per_host max-dynamic-shared-bytes 20971520", 0);
  ASSERT_NE(q, nullptr);
  ASSERT_TRUE(q->name().has_value());
  EXPECT_EQ(*q->name(), "queue_per_host");
  ASSERT_TRUE(q->maxDynamicSharedBytes().has_value());
  EXPECT_EQ(*q->maxDynamicSharedBytes(), 20971520);
}

TEST_F(CmdConfigQosQueueConfigTestFixture, emptyNameFails) {
  setupTestableConfigSession(cmdPrefix_, "0 name \"\"");
  auto cmd = CmdConfigQosQueueConfigQueueId();
  EXPECT_THROW(
      cmd.queryClient(
          localhost(),
          kDefaultName(),
          utils::QueueIdAndAttributes({"0", "name", ""})),
      std::invalid_argument);
}

TEST_F(CmdConfigQosQueueConfigTestFixture, queueConfigNameValidation) {
  EXPECT_TRUE(
      utils::QueueConfigName({utils::kDefaultQueueConfigName}).isDefault());
  EXPECT_FALSE(utils::QueueConfigName({kNamedConfigName}).isDefault());

  EXPECT_THROW(utils::QueueConfigName({}), std::invalid_argument);
  EXPECT_THROW(utils::QueueConfigName({"a", "b"}), std::invalid_argument);
  // Must start with a letter, and may not contain spaces.
  EXPECT_THROW(utils::QueueConfigName({"9queues"}), std::invalid_argument);
  EXPECT_THROW(utils::QueueConfigName({"bad name"}), std::invalid_argument);
}

} // namespace facebook::fboss
