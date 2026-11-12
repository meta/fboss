/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/CounterUtils.h"
#include "fboss/agent/hw/HwCpuFb303Stats.h"
#include "fboss/agent/hw/HwFb303Stats.h"

#include <common/stats/MonotonicCounter.h>
#include <fb303/ServiceData.h>
#include <folly/logging/xlog.h>

#include <cstring>
#include <new>

#include <gtest/gtest.h>
using namespace facebook::fboss;
using namespace facebook::fb303;
using namespace std::chrono;

namespace {
HwCpuFb303Stats::QueueId2Name kQueue2Name = {
    {1, "high"},
    {2, "low"},
};

HwPortStats getInitedStats() {
  return {
      apache::thrift::FragileConstructor(),
      0, // inBytes
      0, // inUcastPackets
      0, // inMulticastPkts
      0, // inBroadcastPkts
      0, // inDiscards
      0, // inErrors
      0, // inPause
      0, // inIpv4HdrErrors
      0, // inIpv6HdrErrors
      0, // inDstNullDiscards
      0, // inDiscardsRaw
      0, // outBytes
      0, // outUnicastPkts
      0, // outMulticastPkts
      0, // outBroadcastPkts
      0, // outDiscards
      0, // outErrors
      0, // outPause
      0, // outCongestionDiscardPkts
      0, // wredDroppedPackets
      {{1, 0}, {2, 0}}, // queueOutDiscards
      {{1, 3}, {2, 3}}, // queueOutBytes
      0, // outEcnCounter
      {{1, 1}, {2, 1}}, // queueOutPackets
      {{1, 2}, {2, 2}}, // queueOutDiscardPackets
      {{0, 0}, {0, 0}}, // queueWatermarkBytes
      0, // fecCorrectableErrors
      0, // fecUncorrectableErrors
      0, // inPfcCtrl_
      0, // outPfcCtrl_
      {{0, 1}, {7, 1}}, // inPfc_
      {{0, 2}, {7, 2}}, // inPfcXon_
      {{0, 3}, {7, 3}}, // outPfc_
      {{1, 0}, {2, 0}}, // queueWredDroppedPackets
      {{1, 0}, {2, 0}}, // queueEcnMarkedPackets
      0, // fecCorrectedBits_
      {{0, 100}, {1, 10}, {2, 1}}, // fecCodewords
      0, // pqpErrorEgressDroppedPackets_
      0, // fabricLinkDownDroppedCells_
      0, // linkLayerFlowControlWatermark_
      0, // timestamp
      "test", // portName
      {}, // macsec stats,
      0, // inLabelMissDiscards_
      {}, // queueWatermarkLevel
      0, // inCongestionDiscards
      0, // inAclDiscards
      0, // inTrapDiscards
      0, // outForwardingDiscards
      0, // fabricConnectivityMismatch
      1, // logicalPortId
      2, // leakyBucketFlapCount_
      100, // cableLengthMeters
      true, // dataCellsFilterIsOn
      {{1, 0}, {2, 0}}, // egressGvoqWatermarkBytes_
      {{1, 0}}, // pgInCongestionDiscards_
      30, // pfcDeadlockDetection_
      30, // pfcDeadlockRecovery_
      {{1, false}}, // pgInCongestionDiscardsSeen_
      0, // macTransmitQueueMinWatermarkCells_
      0, // macTransmitQueueMaxWatermarkCells_
      false, // macTransmitQueueStuck_
      0, // fabricControlRxPackets_
      0, // fabricControlTxPackets_
      {{1, 0}}, // txPfcDurationUsec_
      {{1, 0}}, // rxPfcDurationUsec_
      0, // outDiscardsSll_
      0, // outDiscardsHll_
      0, // inSrv6MySidDiscards_
      0, // cableDelayNsec
      0, // llrTxOk
      0, // llrTxReplay
      0, // llrRxOk
      0, // llrRxMissingSeq
      0, // llrRxDuplicateSeq
      0, // llrRxAckNackSeqError
      0, // llrRxReplay
      0, // linkDownDebounceRetriggerCount_
      0, // linkUpDebounceRetriggerCount_
      0, // llrTxInitCtlOs
      0, // llrTxInitEchoCtlOs
      0, // llrTxAckCtlOs
      0, // llrTxNackCtlOs
      0, // llrRxInitCtlOs
      0, // llrRxInitEchoCtlOs
      0, // llrRxAckCtlOs
      0, // llrRxNackCtlOs
      0, // llrRxExpectedSeqGood
      0, // llrRxExpectedSeqPoisoned
      0, // llrRxExpectedSeqBad
      0, // linkFault_
      0, // fecCorrectedSymbols
      LlrTxStatus::OFF, // llrTxStatus
      LlrRxStatus::OFF, // llrRxStatus
      0, // llrTxEligiblePkts
      0, // llrTxIneligiblePkts
      0, // llrRxEligiblePkts
      0, // llrRxIneligiblePkts
      0, // llrTxNackReplayEvent
      0, // llrTxTimerReplayEvent
      0, // llrTxError
      0, // cbfcCcUpdateTx
      0, // cbfcCfUpdateTx
      0, // cbfcCfUpdateRx
      {}, // cbfcVcSenderCreditsConsumed
      {}, // cbfcVcSenderCreditsFreed
      {}, // cbfcVcReceiverCreditsConsumed
      {}, // cbfcVcReceiverCreditsFreed
      0, // hwProtectionSwitchoverEvents
      0, // hwProtectionSwitchoverDropPkts
      {}, // fabricLinkMonitoringTxPackets_
      {}, // fabricLinkMonitoringRxPackets_
      {}, // fabricLinkMonitoringDroppedPackets_
      {}, // fabricLinkMonitoringInvalidPayloadPackets_
      {}, // fabricLinkMonitoringNoPendingSeqNumPackets_
  };
}

void updateStats(HwCpuFb303Stats& cpuStats) {
  auto now = duration_cast<seconds>(system_clock::now().time_since_epoch());
  // To get last increment from monotonic counter we need to update it twice
  HwPortStats empty{};
  // Need to populate queue stats, since by default these
  // maps are empty
  *empty.queueOutDiscardPackets_() = *empty.queueOutPackets_() =
      *empty.queueOutBytes_() = {{1, 0}, {2, 0}};
  cpuStats.updateStats(empty, now);
  cpuStats.updateStats(getInitedStats(), now);
}

void verifyUpdatedStats(const HwCpuFb303Stats& cpuStats) {
  auto curValue{1};
  curValue = 1;
  for (auto counterName : HwCpuFb303Stats::kQueueMonotonicCounterStatKeys()) {
    for (const auto& queueIdAndName : kQueue2Name) {
      EXPECT_EQ(
          cpuStats.getCounterLastIncrement(
              HwCpuFb303Stats::statName(
                  counterName, queueIdAndName.first, queueIdAndName.second)),
          curValue);
      EXPECT_EQ(
          cpuStats.getCumulativeValueIf(
              HwCpuFb303Stats::statName(
                  counterName, queueIdAndName.first, queueIdAndName.second)),
          curValue);
    }
    ++curValue;
  }
}
} // namespace

TEST(HwCpuFb303StatsTest, StatName) {
  for (auto statKey : HwCpuFb303Stats::kQueueMonotonicCounterStatKeys()) {
    EXPECT_EQ(
        HwCpuFb303Stats::statName(statKey, 1, "high"),
        folly::to<std::string>("cpu.queue1.cpuQueue-high.", statKey));
  }
}
TEST(HwCpuFb303StatsTest, StatsInit) {
  HwCpuFb303Stats stats(kQueue2Name);
  for (auto statKey : HwCpuFb303Stats::kQueueMonotonicCounterStatKeys()) {
    for (const auto& queueIdAndName : kQueue2Name) {
      EXPECT_TRUE(fbData->getStatMap()->contains(
          HwCpuFb303Stats::statName(
              statKey, queueIdAndName.first, queueIdAndName.second)));
    }
  }
}

TEST(HwCpuFb303StatsTest, StatsDeInit) {
  {
    HwCpuFb303Stats stats(kQueue2Name);
  }
  for (auto statKey : HwCpuFb303Stats::kQueueMonotonicCounterStatKeys()) {
    for (const auto& queueIdAndName : kQueue2Name) {
      EXPECT_FALSE(fbData->getStatMap()->contains(
          HwCpuFb303Stats::statName(
              statKey, queueIdAndName.first, queueIdAndName.second)));
    }
  }
}

TEST(HwCpuFb303Stats, UpdateStats) {
  HwCpuFb303Stats cpuStats(kQueue2Name);
  updateStats(cpuStats);
  verifyUpdatedStats(cpuStats);
}

TEST(HwCpuFb303Stats, UpdateCpuFb303Stats) {
  HwCpuFb303Stats cpuStats(kQueue2Name);
  // To get last increment from monotonic counter we need to update it twice
  CpuPortStats empty{};
  *empty.queueDiscardPackets_() =
      *empty.queueInPackets_() = {{1, 0}, {2, 0}, {3, 0}};
  *empty.queueToName_() = {
      {1, "high"},
      {2, "low"},
      {3, "mid"},
  };
  *empty.portStats_()->queueOutBytes_() = {{1, 0}, {2, 0}, {3, 0}};
  CpuPortStats initedStats{};
  *initedStats.queueToName_() = {
      {1, "high"},
      {2, "low"},
  };
  *initedStats.queueDiscardPackets_() = {{1, 2}, {2, 2}};
  *initedStats.queueInPackets_() = {{1, 1}, {2, 1}};
  *initedStats.portStats_()->queueOutBytes_() = {{1, 3}, {2, 3}};
  cpuStats.updateStats(empty);
  cpuStats.updateStats(initedStats);
  verifyUpdatedStats(cpuStats);
  for (auto statKey : HwCpuFb303Stats::kQueueMonotonicCounterStatKeys()) {
    EXPECT_FALSE(fbData->getStatMap()->contains(
        HwCpuFb303Stats::statName(statKey, 3, "mid")));
  }
}

TEST(HwCpuFb303StatsTest, RenameQueue) {
  HwCpuFb303Stats stats(kQueue2Name);
  stats.queueChanged(1, "very_high");
  auto newQueueMapping = kQueue2Name;
  for (auto statKey : HwCpuFb303Stats::kQueueMonotonicCounterStatKeys()) {
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwCpuFb303Stats::statName(statKey, 1, "very_high")));
    EXPECT_FALSE(fbData->getStatMap()->contains(
        HwCpuFb303Stats::statName(statKey, 1, "high")));
    // No impact on low
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwCpuFb303Stats::statName(statKey, 2, "low")));
  }
}

TEST(HwCpuFb303StatsTest, AddQueue) {
  HwCpuFb303Stats stats(kQueue2Name);
  stats.queueChanged(3, "very_high");
  auto newQueueMapping = kQueue2Name;
  for (auto statKey : HwCpuFb303Stats::kQueueMonotonicCounterStatKeys()) {
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwCpuFb303Stats::statName(statKey, 1, "high")));
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwCpuFb303Stats::statName(statKey, 2, "low")));
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwCpuFb303Stats::statName(statKey, 3, "very_high")));
  }
}
TEST(HwCpuFb303StatsTest, RemoveQueue) {
  HwCpuFb303Stats stats(kQueue2Name);
  stats.queueRemoved(1);
  auto newQueueMapping = kQueue2Name;
  for (auto statKey : HwCpuFb303Stats::kQueueMonotonicCounterStatKeys()) {
    EXPECT_FALSE(fbData->getStatMap()->contains(
        HwCpuFb303Stats::statName(statKey, 1, "high")));
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwCpuFb303Stats::statName(statKey, 2, "low")));
  }
}

TEST(HwCpuFb303Stats, queueNameChangeResetsValue) {
  HwCpuFb303Stats cpuStats(kQueue2Name);
  updateStats(cpuStats);
  cpuStats.queueChanged(1, "very_high");
  cpuStats.queueChanged(2, "very_low");
  HwCpuFb303Stats::QueueId2Name newQueues = {{1, "very_high"}, {2, "very_low"}};
  for (auto counterName : HwCpuFb303Stats::kQueueMonotonicCounterStatKeys()) {
    for (const auto& queueIdAndName : newQueues) {
      EXPECT_TRUE(fbData->getStatMap()->contains(
          HwCpuFb303Stats::statName(
              counterName, queueIdAndName.first, queueIdAndName.second)));
      EXPECT_EQ(
          cpuStats.getCounterLastIncrement(
              HwCpuFb303Stats::statName(
                  counterName, queueIdAndName.first, queueIdAndName.second)),
          0);
    }
  }
  for (auto counterName : HwCpuFb303Stats::kQueueMonotonicCounterStatKeys()) {
    for (const auto& queueIdAndName : kQueue2Name) {
      EXPECT_FALSE(fbData->getStatMap()->contains(
          HwCpuFb303Stats::statName(
              counterName, queueIdAndName.first, queueIdAndName.second)));
    }
  }
}

TEST(HwCpuFb303Stats, queueNameWithSwitchId) {
  std::string switchIdPrefix("switch.0.");
  HwCpuFb303Stats cpuStats(kQueue2Name, switchIdPrefix);
  updateStats(cpuStats);
  for (auto counterName : HwCpuFb303Stats::kQueueMonotonicCounterStatKeys()) {
    for (const auto& queueIdAndName : kQueue2Name) {
      EXPECT_TRUE(fbData->getStatMap()->contains(
          switchIdPrefix +
          HwCpuFb303Stats::statName(
              counterName, queueIdAndName.first, queueIdAndName.second)));
    }
  }
}

TEST(HwFb303Stats, CumulativeValueInitializedToZero) {
  alignas(HwFb303Counter) unsigned char storage[sizeof(HwFb303Counter)];
  std::memset(storage, 0xA5, sizeof(storage));
  auto* counter = new (storage) HwFb303Counter(
      facebook::stats::MonotonicCounter(
          "test.cumulativeInitPoison", SUM, RATE));
  EXPECT_EQ(counter->cumulativeValue, 0);
  utility::deleteCounter("test.cumulativeInitPoison");
  counter->~HwFb303Counter();
}

TEST(HwFb303Stats, CumulativeValueZeroBeforeFirstUpdate) {
  HwFb303Stats stats(std::nullopt);
  const std::string kStat{"test.cumulativeInit"};
  auto now = duration_cast<seconds>(system_clock::now().time_since_epoch());
  stats.reinitStat(kStat, std::nullopt);
  EXPECT_EQ(stats.getCumulativeValueIf(kStat), 0);
  stats.updateStat(now, kStat, 0x123456789ABCDELL);
  EXPECT_EQ(stats.getCumulativeValueIf(kStat), 0x123456789ABCDEULL);
  stats.removeStat(kStat);
  stats.reinitStat(kStat, std::nullopt);
  EXPECT_EQ(stats.getCumulativeValueIf(kStat), 0);
}

TEST(HwCpuFb303Stats, getCpuStats) {
  HwCpuFb303Stats cpuStats(kQueue2Name);
  updateStats(cpuStats);

  const auto cpuPortStats = cpuStats.getCpuPortStats();
  EXPECT_EQ(cpuPortStats.queueToName_()->size(), 2);
  EXPECT_EQ(cpuPortStats.queueInPackets_()->size(), 2);
  EXPECT_EQ(cpuPortStats.queueDiscardPackets_()->size(), 2);

  for (const auto& queueIdAndName : *cpuPortStats.queueToName_()) {
    const auto& iter1 =
        cpuPortStats.queueDiscardPackets_()->find(queueIdAndName.first);
    EXPECT_TRUE(iter1 != cpuPortStats.queueDiscardPackets_()->end());
    EXPECT_EQ(iter1->second, 2);

    const auto& iter2 =
        cpuPortStats.queueInPackets_()->find(queueIdAndName.first);
    EXPECT_TRUE(iter2 != cpuPortStats.queueInPackets_()->end());
    EXPECT_EQ(iter2->second, 1);
  }
}
