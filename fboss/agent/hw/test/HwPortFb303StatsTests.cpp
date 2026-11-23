/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/hw/HwPortFb303Stats.h"
#include "fboss/agent/hw/StatsConstants.h"

#include <fb303/ServiceData.h>
#include <fb303/ThreadCachedServiceData.h>
#include <folly/logging/xlog.h>

#include <gtest/gtest.h>
using namespace facebook::fboss;
using namespace facebook::fb303;
using namespace std::chrono;

namespace {
constexpr auto kPortName = "eth1/1/1";
HwPortFb303Stats::QueueId2Name kQueue2Name = {
    {1, "gold"},
    {2, "silver"},
};

std::vector<PfcPriority> kEnabledPfcPriorities(
    {static_cast<PfcPriority>(2), static_cast<PfcPriority>(3)});

HwPortStats getInitedStats() {
  MacsecStats macsecStats{
      apache::thrift::FragileConstructor(),
      // ingress macsec port stats
      mka::MacsecPortStats{
          apache::thrift::FragileConstructor(),
          1, // preMacsecDropPkts
          2, // controlPkts
          3, // dataPkts
          4, // octetsEncrypted
          5, // inBadOrNoMacsecTagDroppedPkts
          6, // inNoSciDroppedPkts
          7, // inUnknownSciPkts
          8, // inOverrunDroppedPkts
          9, // inDelayedPkts
          10, // inLateDroppedPkts
          11, // inNotValidDroppedPkts
          12, // inInvalidPkts
          13, // inNoSaDroppedPkts
          14, // inUnusedSaPkts
          16, // inCurrentXpn
          0, // outTooLongDroppedPkts
          0, // outCurrentXpn
          15 // noMacsecTagPkts
      },
      // egress macsec port stats
      mka::MacsecPortStats{
          apache::thrift::FragileConstructor(),
          1, // preMacsecDropPkts
          2, // controlPkts
          3, // dataPkts
          4, // octetsEncrypted
          0, // inBadOrNoMacsecTagDroppedPkts
          0, // inNoSciDroppedPkts
          0, // inUnknownSciPkts
          0, // inOverrunDroppedPkts
          0, // inDelayedPkts
          0, // inLateDroppedPkts
          0, // inNotValidDroppedPkts
          0, // inInvalidPkts
          0, // inNoSaDroppedPkts
          0, // inUnusedSaPkts
          0, // inCurrentXpn
          5, // outTooLongDroppedPkts
          7, // outCurrentXpn
          6 // noMacsecTagPkts
      },
      {}, // ingress flow stats
      {}, // egress flow stats
      {{}}, // rx SA stats
      {{}}, // tx SA stats
      // Macsec Acl stats ingress
      mka::MacsecAclStats{
          apache::thrift::FragileConstructor(),
          0, // defaultAclStats
          0, // controlAclStats
          0, // flowAclStats
      },
      // Macsec Acl stats egress
      mka::MacsecAclStats{
          apache::thrift::FragileConstructor(),
          0, // defaultAclStats
          0, // controlAclStats
          0, // flowAclStats
      },
  };
  return {
      apache::thrift::FragileConstructor(),
      1, // inBytes
      2, // inUcastPackets
      3, // inMulticastPkts
      4, // inBroadcastPkts
      5, // inDiscards
      6, // inErrors
      7, // inPause
      8, // inIpv4HdrErrors
      9, // inIpv6HdrErrors
      10, // inDstNullDiscards
      11, // inDiscardsRaw
      12, // outBytes
      13, // outUnicastPkts
      14, // outMulticastPkts
      15, // outBroadcastPkts
      16, // outDiscards
      17, // outErrors
      18, // outPause
      19, // outCongestionDiscardPkts
      20, // wredDroppedPackets
      {{1, 1}, {2, 1}}, // queueOutDiscards
      {{1, 3}, {2, 3}}, // queueOutBytes
      21, // outEcnCounter
      {{1, 4}, {2, 4}}, // queueOutPackets
      {{1, 2}, {2, 2}}, // queueOutDiscardPackets
      {{1, 0}, {2, 10}}, // queueWatermarkBytes
      22, // fecCorrectableErrors
      23, // fecUncorrectableErrors
      24, // inPfcCtrl_
      25, // outPfcCtrl_
      {{0, 1}, {7, 1}}, // inPfc_
      {{0, 2}, {7, 2}}, // inPfcXon_
      {{0, 3}, {7, 3}}, // outPfc_
      {{1, 5}, {2, 5}}, // queueWredDroppedPackets
      {{1, 6}, {2, 6}}, // queueEcnMarkedPackets
      0, // fecCorrectedBits_
      {{0, 100}, {1, 10}, {2, 1}}, // fecCodewords
      26, // pqpErrorEgressDroppedPackets_
      27, // fabricLinkDownDroppedCells_
      28, // linkLayerFlowControlWatermark_
      0, // timestamp
      "test", // portName
      macsecStats,
      24, // inLabelMissDiscards_
      {}, // queueWatermarkLevel
      24, // inCongestionDiscards_
      26, // inAclDiscards
      27, // inTrapDiscards
      29, // outForwardingDiscards
      0, // fabricConnectivityMismatch
      1, // logicalPortId
      2, // leakyBucketFlapCount_
      1, // cableLengthMeters
      true, // dataCellsFilterIsOn
      {{1, 0}, {2, 0}}, // egressGvoqWatermarkBytes_
      {{2, 3}, {3, 4}}, // pgInCongestionDiscards_
      29, // pfcDeadlockDetection_
      30, // pfcDeadlockRecovery_
      {{2, true}, {3, false}}, // pgInCongestionDiscardSeen_
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
      1, // cableDelayNsec
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
      // Both statuses must have the numeric value 1 -- verifyUpdatedStats
      // expects every kPortFb303CounterStatKeys() counter to read 1.
      LlrTxStatus::INIT, // llrTxStatus
      LlrRxStatus::SEND_ACKS, // llrRxStatus
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

HwPortStats resetOptionals(HwPortStats stats) {
  stats.macsecStats().reset();
  stats.inAclDiscards_().reset();
  stats.inTrapDiscards_().reset();
  stats.outForwardingDiscards_().reset();
  stats.pfcDeadlockDetection_().reset();
  stats.pfcDeadlockRecovery_().reset();
  stats.pgInCongestionDiscards_()->clear();
  stats.pgInCongestionDiscardSeen_()->clear();
  stats.outDiscardsSll_().reset();
  stats.outDiscardsHll_().reset();
  stats.inSrv6MySidDiscards_().reset();
  return stats;
}

HwPortStats getInitedStatsSansOptionals() {
  return resetOptionals(getInitedStats());
}

HwPortStats getEmptyStats() {
  // To get last increment from monotonic counter we need to update it twice
  HwPortStats empty{};
  // Initialize the optionals, so we can see a delta when we update these with
  // actual values
  empty.inAclDiscards_() = 0;
  empty.inTrapDiscards_() = 0;
  MacsecStats emptyMacsecStats{
      apache::thrift::FragileConstructor(),
      mka::MacsecPortStats{
          apache::thrift::FragileConstructor(),
          0, // preMacsecDropPkts
          0, // controlPkts
          0, // dataPkts
          0, // octetsEncrypted
          0, // inBadOrNoMacsecTagDroppedPkts
          0, // inNoSciDroppedPkts
          0, // inUnknownSciPkts
          0, // inOverrunDroppedPkts
          0, // inDelayedPkts
          0, // inLateDroppedPkts
          0, // inNotValidDroppedPkts
          0, // inInvalidPkts
          0, // inNoSaDroppedPkts
          0, // inUnusedSaPkts
          0, // inCurrentXpn
          0, // outTooLongDroppedPkts
          0, // outCurrentXpn
          0 // noMacsecTagPkts
      }, // ingress  macsec port stats
      mka::MacsecPortStats{
          apache::thrift::FragileConstructor(),
          0, // preMacsecDropPkts
          0, // controlPkts
          0, // dataPkts
          0, // octetsEncrypted
          0, // inBadOrNoMacsecTagDroppedPkts
          0, // inNoSciDroppedPkts
          0, // inUnknownSciPkts
          0, // inOverrunDroppedPkts
          0, // inDelayedPkts
          0, // inLateDroppedPkts
          0, // inNotValidDroppedPkts
          0, // inInvalidPkts
          0, // inNoSaDroppedPkts
          0, // inUnusedSaPkts
          0, // inCurrentXpn
          0, // outTooLongDroppedPkts
          0, // outCurrentXpn
          0 // noMacsecTagPkts
      }, // egress  macsec port stats
      {}, // ingress flow stats
      {}, // egress flow stats
      {{}}, // rx SA stats
      {{}}, // tx SA stats
      mka::MacsecAclStats{
          apache::thrift::FragileConstructor(),
          0,
          0,
          0,
      }, // macsec ingress Acl stats
      mka::MacsecAclStats{
          apache::thrift::FragileConstructor(),
          0,
          0,
          0,
      }, // macsec egress Acl stats
  };
  empty.macsecStats() = emptyMacsecStats;
  // Need to populate queue stats, since by default these
  // maps are empty
  *empty.queueOutDiscardPackets_() = *empty.queueOutDiscardBytes_() =
      *empty.queueOutBytes_() = *empty.queueOutPackets_() =
          *empty.queueWatermarkBytes_() = *empty.queueEcnMarkedPackets_() =
              *empty.queueWredDroppedPackets_() = {{1, 0}, {2, 0}};
  // Populate priority group stats only for enabled PFC priorities
  for (const auto& priority : kEnabledPfcPriorities) {
    empty.pgInCongestionDiscards_()[priority] = 0;
    empty.pgInCongestionDiscardSeen_()[priority] = false;
  }
  empty.pfcDeadlockDetection_() = 0;
  empty.pfcDeadlockRecovery_() = 0;
  return empty;
}

HwPortStats getEmptyStatsSansOptionals() {
  return resetOptionals(getEmptyStats());
}

void updateStats(HwPortFb303Stats& portStats) {
  auto now = duration_cast<seconds>(system_clock::now().time_since_epoch());
  portStats.updateStats(getEmptyStats(), now);
  portStats.updateStats(getInitedStats(), now);
}

void verifyUpdatedStats(const HwPortFb303Stats& portStats) {
  auto curValue{1};
  for (auto counterName : portStats.kPortMonotonicCounterStatKeys()) {
    // +1 because first initialization is to -1
    auto actualVal = portStats.getCounterLastIncrement(
        HwPortFb303Stats::statName(counterName, kPortName), 0);
    if (actualVal == 0) {
      continue;
    }
    auto expectedVal = (curValue++) + 1;
    EXPECT_EQ(actualVal, expectedVal) << "failed for " << counterName;
    XLOG(DBG2) << counterName << ": " << actualVal << " " << expectedVal;
  }
  curValue = 1;
  for (auto counterName : portStats.kPortFb303CounterStatKeys()) {
    auto value = facebook::fb303::fbData->getCounter(
        HwPortFb303Stats::statName(counterName, kPortName));
    EXPECT_EQ(value, curValue);
  }

  curValue = 1;
  for (auto counterName : portStats.kQueueMonotonicCounterStatKeys()) {
    for (const auto& queueIdAndName : kQueue2Name) {
      EXPECT_EQ(
          portStats.getCounterLastIncrement(
              HwPortFb303Stats::statName(
                  counterName,
                  kPortName,
                  queueIdAndName.first,
                  queueIdAndName.second)),
          curValue);
    }
    ++curValue;
  }
  curValue = 1;
  if (portStats.portStats().macsecStats().has_value()) {
    for (auto counterName : portStats.kInMacsecPortMonotonicCounterStatKeys()) {
      EXPECT_EQ(
          portStats.getCounterLastIncrement(
              HwPortFb303Stats::statName(counterName, kPortName)),
          curValue++);
    }
    curValue = 1;
    for (auto counterName :
         portStats.kOutMacsecPortMonotonicCounterStatKeys()) {
      EXPECT_EQ(
          portStats.getCounterLastIncrement(
              HwPortFb303Stats::statName(counterName, kPortName)),
          curValue++);
    }
  }
  // Only verify priority group stats if they were populated
  if (apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          portStats.portStats().pgInCongestionDiscards_()) &&
      !portStats.portStats().pgInCongestionDiscards_()->empty()) {
    curValue = 3;
    for (auto counterName :
         portStats.kPriorityGroupMonotonicCounterStatKeys()) {
      for (const auto& priority : kEnabledPfcPriorities) {
        EXPECT_EQ(
            portStats.getCounterLastIncrement(
                HwPortFb303Stats::pgStatName(counterName, kPortName, priority)),
            curValue++);
      }
    }

    // Keep the pgInCongestionDiscardsSeen_ also under the empty check
    // for pgInCongestionDiscards_ as its set together for these tests.
    for (auto counterName : portStats.kPriorityGroupCounterStatKeys()) {
      for (const auto& priority : kEnabledPfcPriorities) {
        int expectedVal = static_cast<int>(priority) == 2
            ? 1
            : 0; // PG2 is true, others false
        auto value = facebook::fb303::fbData->getCounter(
            HwPortFb303Stats::pgStatName(counterName, kPortName, priority));
        EXPECT_EQ(value, expectedVal);
      }
    }
  }
}
} // namespace

// Per-PG counters are keyed by PG id, so a PG id outside the enabled PFC
// priorities ({2,3}) is still registered and published.
TEST(HwPortFb303StatsTest, PgCongestionCountersKeyedByPgIdNotPriority) {
  HwPortFb303Stats stats(
      kPortName,
      {},
      kEnabledPfcPriorities, // {2, 3}
      std::nullopt,
      true /* inCongestionDiscardCountSupported */,
      true /* inCongestionDiscardSeenSupported */);
  // Registration covers all PG ids, e.g. PG 5 and PG 0 (not in {2,3}).
  for (auto statKey : stats.kPriorityGroupMonotonicCounterStatKeys()) {
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwPortFb303Stats::pgStatName(statKey, kPortName, 5)));
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwPortFb303Stats::pgStatName(statKey, kPortName, 0)));
  }

  // Update path: congestion on PG 5 (outside {2,3}) is published under .pg5.
  auto now = duration_cast<seconds>(system_clock::now().time_since_epoch());
  // Baseline first (monotonic counters need a prior data point).
  HwPortStats baseline = getInitedStats();
  baseline.pgInCongestionDiscards_() = {{5, 0}};
  baseline.pgInCongestionDiscardSeen_() = {{5, false}};
  HwPortStats inited = getInitedStats();
  inited.pgInCongestionDiscards_() = {{5, 42}};
  inited.pgInCongestionDiscardSeen_() = {{5, true}};
  stats.updateStats(baseline, now);
  stats.updateStats(inited, now);

  for (auto counterName : stats.kPriorityGroupMonotonicCounterStatKeys()) {
    EXPECT_EQ(
        stats.getCounterLastIncrement(
            HwPortFb303Stats::pgStatName(counterName, kPortName, 5)),
        42);
  }
  for (auto counterName : stats.kPriorityGroupCounterStatKeys()) {
    EXPECT_EQ(
        fbData->getCounter(
            HwPortFb303Stats::pgStatName(counterName, kPortName, 5)),
        1);
  }
}

namespace {
// Each test uses its own port name: fb303 timeseries live in a process wide
// store, so reusing a name would let one test's sums leak into the next.
int64_t publishedSum(folly::StringPiece statKey, const std::string& portName) {
  facebook::tcData().publishStats();
  return fbData
      ->getCounterIfExists(
          HwPortFb303Stats::statName(statKey, portName) + ".sum")
      .value_or(-1);
}
} // namespace

// A changed sample publishes 1 into the SUM timeseries and an unchanged one
// publishes 0, so ODS sees .sum/.sum.60/... rather than a level that a scrape
// slower than the collection interval could step over.
TEST(HwPortFb303StatsTest, PhyChangedPublishesOnePerChangedInterval) {
  const std::string portName = "eth1/1/2";
  HwPortFb303Stats portStats(portName);

  portStats.updatePhyChanged(kLineRxSignalDetectChanged(), true);
  portStats.updatePhyChanged(kLineRxSignalDetectChanged(), false);
  portStats.updatePhyChanged(kLineRxSignalDetectChanged(), true);
  portStats.updatePhyChanged(kLineRxSignalDetectChanged(), true);

  EXPECT_EQ(publishedSum(kLineRxSignalDetectChanged(), portName), 3);
}

// An unchanged interval must still publish, so the counter exists on a healthy
// port and reads 0 instead of being absent from ODS.
TEST(HwPortFb303StatsTest, PhyUnchangedPublishesZeroRatherThanNothing) {
  const std::string portName = "eth1/1/3";
  HwPortFb303Stats portStats(portName);

  portStats.updatePhyChanged(kLineRxCdrLockChanged(), false);
  portStats.updatePhyChanged(kLineRxCdrLockChanged(), false);

  // value_or(-1) distinguishes "published 0" from "never published"
  EXPECT_EQ(publishedSum(kLineRxCdrLockChanged(), portName), 0);
}

// Every key the phy path can publish is independent: updatePmdInfo runs once
// per side on XPHY, and fault publishing is line only, so a shared key would
// conflate them.
TEST(HwPortFb303StatsTest, PhyChangedCountersAreIndependentPerKey) {
  const std::string portName = "eth1/1/4";
  HwPortFb303Stats portStats(portName);

  portStats.updatePhyChanged(kLineRxSignalDetectChanged(), true);
  portStats.updatePhyChanged(kSystemRxSignalDetectChanged(), false);
  portStats.updatePhyChanged(kLineRxCdrLockChanged(), true);
  portStats.updatePhyChanged(kSystemRxCdrLockChanged(), true);
  portStats.updatePhyChanged(kLineLocalFaultChanged(), false);
  portStats.updatePhyChanged(kLineRemoteFaultChanged(), true);

  EXPECT_EQ(publishedSum(kLineRxSignalDetectChanged(), portName), 1);
  EXPECT_EQ(publishedSum(kSystemRxSignalDetectChanged(), portName), 0);
  EXPECT_EQ(publishedSum(kLineRxCdrLockChanged(), portName), 1);
  EXPECT_EQ(publishedSum(kSystemRxCdrLockChanged(), portName), 1);
  EXPECT_EQ(publishedSum(kLineLocalFaultChanged(), portName), 0);
  EXPECT_EQ(publishedSum(kLineRemoteFaultChanged(), portName), 1);
}

// These are not in kPortMonotonicCounterStatKeys or kPortFb303CounterStatKeys,
// so reinitStats has to clear the old name explicitly or a renamed port would
// leave its timeseries behind forever.
TEST(HwPortFb303StatsTest, PhyChangedCountersClearedOnPortRename) {
  const std::string portName = "eth1/1/5";
  const std::string newPortName = "eth1/1/6";
  HwPortFb303Stats portStats(portName);

  portStats.updatePhyChanged(kLineRxSignalDetectChanged(), true);
  portStats.updatePhyChanged(kLineRemoteFaultChanged(), true);
  ASSERT_EQ(publishedSum(kLineRxSignalDetectChanged(), portName), 1);

  portStats.portNameChanged(newPortName);
  facebook::tcData().publishStats();
  for (const auto& statKey :
       {kLineRxSignalDetectChanged(),
        kSystemRxSignalDetectChanged(),
        kLineRxCdrLockChanged(),
        kSystemRxCdrLockChanged(),
        kLineLocalFaultChanged(),
        kLineRemoteFaultChanged()}) {
    SCOPED_TRACE(statKey.str());
    EXPECT_FALSE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(statKey, portName)));
  }

  // The new name starts clean and picks up subsequent samples
  portStats.updatePhyChanged(kLineRxSignalDetectChanged(), true);
  EXPECT_EQ(publishedSum(kLineRxSignalDetectChanged(), newPortName), 1);
}

// HW protection switchover counters are optional: only ports that report them
// (TH5/TH6 on XGS 13.3+) update the timeseries.
TEST(HwPortFb303StatsTest, HwProtectionSwitchoverCounters) {
  const std::string portName = "eth1/1/7";
  HwPortFb303Stats portStats(portName);
  auto now = duration_cast<seconds>(system_clock::now().time_since_epoch());

  HwPortStats baseline{};
  baseline.hwProtectionSwitchoverEvents_() = 0;
  baseline.hwProtectionSwitchoverDropPkts_() = 0;
  HwPortStats inited = baseline;
  inited.hwProtectionSwitchoverEvents_() = 5;
  inited.hwProtectionSwitchoverDropPkts_() = 7;
  portStats.updateStats(baseline, now);
  portStats.updateStats(inited, now);

  EXPECT_EQ(
      portStats.getCounterLastIncrement(
          HwPortFb303Stats::statName(
              kHwProtectionSwitchoverEvents(), portName)),
      5);
  EXPECT_EQ(
      portStats.getCounterLastIncrement(
          HwPortFb303Stats::statName(
              kHwProtectionSwitchoverDropPkts(), portName)),
      7);
}

TEST(
    HwPortFb303StatsTest,
    LinkDownDuringWarmBootCounterPublishesBooleanWhenSupported) {
  const std::string portName = "eth1/1/10";
  HwPortFb303Stats portStats(
      portName,
      {} /*queueId2Name*/,
      {} /*enabledPfcPriorities*/,
      std::nullopt /*pfcCfg*/,
      false /*inCongestionDiscardCountSupported*/,
      false /*inCongestionDiscardSeenSupported*/,
      false /*srv6MysidDiscardCounterSupported*/,
      false /*mplsLabelLookupFailCounterSupported*/,
      false /*linkDebounceRetriggerCounterSupported*/,
      false /*sllHllDiscardCounterSupported*/,
      true /*portLatchStatusSupported*/);

  portStats.updateLinkDownDuringWarmBootCounter(true);
  EXPECT_EQ(
      fbData
          ->getCounterIfExists(
              HwPortFb303Stats::statName(kLinkDownDuringWarmBoot(), portName))
          .value_or(-1),
      1);

  portStats.updateLinkDownDuringWarmBootCounter(false);
  EXPECT_EQ(
      fbData
          ->getCounterIfExists(
              HwPortFb303Stats::statName(kLinkDownDuringWarmBoot(), portName))
          .value_or(-1),
      0);
}

TEST(
    HwPortFb303StatsTest,
    LinkDownDuringWarmBootCounterInitializedOnlyWhenSupported) {
  const std::string supportedPortName = "latch_supported_init";
  HwPortFb303Stats supportedPortStats(
      supportedPortName,
      {} /*queueId2Name*/,
      {} /*enabledPfcPriorities*/,
      std::nullopt /*pfcCfg*/,
      false /*inCongestionDiscardCountSupported*/,
      false /*inCongestionDiscardSeenSupported*/,
      false /*srv6MysidDiscardCounterSupported*/,
      false /*mplsLabelLookupFailCounterSupported*/,
      false /*linkDebounceRetriggerCounterSupported*/,
      false /*sllHllDiscardCounterSupported*/,
      true /*portLatchStatusSupported*/);
  const auto supportedCounter = fbData->getCounterIfExists(
      HwPortFb303Stats::statName(kLinkDownDuringWarmBoot(), supportedPortName));
  ASSERT_TRUE(supportedCounter.has_value());
  EXPECT_EQ(*supportedCounter, 0);

  const std::string unsupportedPortName = "latch_unsupported_init";
  HwPortFb303Stats unsupportedPortStats(unsupportedPortName);
  EXPECT_FALSE(fbData
                   ->getCounterIfExists(
                       HwPortFb303Stats::statName(
                           kLinkDownDuringWarmBoot(), unsupportedPortName))
                   .has_value());
}

TEST(
    HwPortFb303StatsTest,
    LinkDownDuringWarmBootCounterNotCreatedWhenUnsupported) {
  const std::string portName = "eth1/1/10";
  HwPortFb303Stats portStats(portName);

  portStats.updateLinkDownDuringWarmBootCounter(true);
  EXPECT_FALSE(
      fbData
          ->getCounterIfExists(
              HwPortFb303Stats::statName(kLinkDownDuringWarmBoot(), portName))
          .has_value());
}

TEST(HwPortFb303StatsTest, StatName) {
  EXPECT_EQ(
      HwPortFb303Stats::statName(kOutBytes(), kPortName),
      folly::to<std::string>(kPortName, '.', kOutBytes()));
  EXPECT_EQ(kLinkDownDuringWarmBoot().str(), "link_down_during_warmboot");
  EXPECT_EQ(
      HwPortFb303Stats::statName(kLinkDownDuringWarmBoot(), "eth1/1/10"),
      "eth1/1/10.link_down_during_warmboot");
  EXPECT_EQ(
      HwPortFb303Stats::statName(kOutBytes(), kPortName, 1, "gold"),
      folly::to<std::string>(kPortName, '.', "queue1.gold.", kOutBytes()));
}

TEST(HwPortFb303StatsTest, StatsInit) {
  HwPortFb303Stats stats(
      kPortName,
      kQueue2Name,
      kEnabledPfcPriorities,
      std::nullopt,
      true /* inCongestionDiscardCountSupported */,
      true /* inCongestionDiscardSeenSupported */);
  for (auto statKey : stats.kPortMonotonicCounterStatKeys()) {
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(statKey, kPortName)));
  }
  for (auto statKey : stats.kQueueMonotonicCounterStatKeys()) {
    for (const auto& queueIdAndName : kQueue2Name) {
      EXPECT_TRUE(fbData->getStatMap()->contains(
          HwPortFb303Stats::statName(
              statKey,
              kPortName,
              queueIdAndName.first,
              queueIdAndName.second)));
    }
  }
  for (auto statKey : stats.kPfcMonotonicCounterStatKeys()) {
    for (const auto& pfcPriority : kEnabledPfcPriorities) {
      EXPECT_TRUE(fbData->getStatMap()->contains(
          HwPortFb303Stats::statName(statKey, kPortName, pfcPriority)));
    }
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(statKey, kPortName)));
  }
  for (auto statKey : stats.kPfcDeadlockMonotonicCounterStatKeys()) {
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(statKey, kPortName)));
  }
  for (auto statKey : stats.kPriorityGroupMonotonicCounterStatKeys()) {
    for (const auto& pfcPriority : kEnabledPfcPriorities) {
      EXPECT_TRUE(fbData->getStatMap()->contains(
          HwPortFb303Stats::pgStatName(statKey, kPortName, pfcPriority)));
    }
  }
  // kPriorityGroupCounterStatKeys() are not initialized on construction,
  // they will be initialized on the first set/update. Hence, we don't
  // check for their existence in this test.
}

TEST(HwPortFb303StatsTest, StatsDeInit) {
  {
    HwPortFb303Stats stats(kPortName);
  }
  HwPortFb303Stats dummy("dummy");
  for (auto statKey : dummy.kPortMonotonicCounterStatKeys()) {
    EXPECT_FALSE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(statKey, kPortName)));
  }
  for (auto statKey : dummy.kPortFb303CounterStatKeys()) {
    EXPECT_FALSE(
        fbData
            ->getCounterIfExists(HwPortFb303Stats::statName(statKey, kPortName))
            .has_value());
  }
  for (auto statKey : dummy.kQueueMonotonicCounterStatKeys()) {
    for (const auto& queueIdAndName : kQueue2Name) {
      EXPECT_FALSE(fbData->getStatMap()->contains(
          HwPortFb303Stats::statName(
              statKey,
              kPortName,
              queueIdAndName.first,
              queueIdAndName.second)));
    }
  }
}

TEST(HwPortFb303StatsTest, ReInit) {
  constexpr auto kNewPortName = "eth1/2/1";

  HwPortFb303Stats stats(
      kPortName,
      kQueue2Name,
      kEnabledPfcPriorities,
      std::nullopt,
      true /* inCongestionDiscardCountSupported */,
      true /* inCongestionDiscardSeenSupported */);
  stats.portNameChanged(kNewPortName);
  for (const auto& sName : stats.kPortMonotonicCounterStatKeys()) {
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(sName, kNewPortName)));
    EXPECT_FALSE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(sName, kPortName)));
  }
  for (auto statKey : stats.kPortFb303CounterStatKeys()) {
    EXPECT_FALSE(
        fbData
            ->getCounterIfExists(HwPortFb303Stats::statName(statKey, kPortName))
            .has_value());
    // For fb303 stats - the new counter will be updated on the next
    // update a not on name change
    EXPECT_FALSE(fbData
                     ->getCounterIfExists(
                         HwPortFb303Stats::statName(statKey, kNewPortName))
                     .has_value());
  }
  updateStats(stats);
  for (auto statKey : stats.kPortFb303CounterStatKeys()) {
    EXPECT_FALSE(
        fbData
            ->getCounterIfExists(HwPortFb303Stats::statName(statKey, kPortName))
            .has_value());
    // New stat counters show up after update
    EXPECT_TRUE(fbData
                    ->getCounterIfExists(
                        HwPortFb303Stats::statName(statKey, kNewPortName))
                    .has_value());
  }

  for (auto statKey : stats.kQueueMonotonicCounterStatKeys()) {
    for (const auto& queueIdAndName : kQueue2Name) {
      EXPECT_TRUE(fbData->getStatMap()->contains(
          HwPortFb303Stats::statName(
              statKey,
              kNewPortName,
              queueIdAndName.first,
              queueIdAndName.second)));
      EXPECT_FALSE(fbData->getStatMap()->contains(
          HwPortFb303Stats::statName(
              statKey,
              kPortName,
              queueIdAndName.first,
              queueIdAndName.second)));
    }
  }
  for (auto statKey : stats.kPfcMonotonicCounterStatKeys()) {
    for (auto pfcPriority : kEnabledPfcPriorities) {
      EXPECT_TRUE(fbData->getStatMap()->contains(
          HwPortFb303Stats::statName(statKey, kNewPortName, pfcPriority)));
      EXPECT_FALSE(fbData->getStatMap()->contains(
          HwPortFb303Stats::statName(statKey, kPortName, pfcPriority)));
    }
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(statKey, kNewPortName)));
    EXPECT_FALSE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(statKey, kPortName)));
  }
  for (auto statKey : stats.kPfcDeadlockMonotonicCounterStatKeys()) {
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(statKey, kNewPortName)));
    EXPECT_FALSE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(statKey, kPortName)));
  }
  for (auto statKey : stats.kPriorityGroupMonotonicCounterStatKeys()) {
    for (const auto& pfcPriority : kEnabledPfcPriorities) {
      EXPECT_TRUE(fbData->getStatMap()->contains(
          HwPortFb303Stats::pgStatName(statKey, kNewPortName, pfcPriority)));
      EXPECT_FALSE(fbData->getStatMap()->contains(
          HwPortFb303Stats::pgStatName(statKey, kPortName, pfcPriority)));
    }
  }
  // kPriorityGroupCounterStatKeys() are not initialized on construction or
  // reinit. They will be initialized only on the first set (via setPgCounter).
  // Hence, we don't check for their existence in this test.
}

TEST(HwPortFb303Stats, UpdateStats) {
  // Initialize with PFC priorities to ensure priority group counters are
  // created
  HwPortFb303Stats portStats(
      kPortName,
      kQueue2Name,
      kEnabledPfcPriorities,
      std::nullopt,
      true /* inCongestionDiscardCountSupported */,
      true /* inCongestionDiscardSeenSupported */);
  updateStats(portStats);
  verifyUpdatedStats(portStats);
}

TEST(HwPortFb303Stats, UpdateStatsSansOptionals) {
  HwPortFb303Stats portStats(kPortName, kQueue2Name);
  auto now = duration_cast<seconds>(system_clock::now().time_since_epoch());
  portStats.updateStats(getEmptyStatsSansOptionals(), now);
  portStats.updateStats(getInitedStatsSansOptionals(), now);
  verifyUpdatedStats(portStats);
}

TEST(HwPortFb303StatsTest, PortName) {
  constexpr auto kNewPortName = "eth1/2/1";
  HwPortFb303Stats stats(kPortName, kQueue2Name);
  EXPECT_EQ(*stats.portStats().portName_(), kPortName);
  stats.portNameChanged(kNewPortName);
  EXPECT_EQ(*stats.portStats().portName_(), kNewPortName);
}

TEST(HwPortFb303StatsTest, RenameQueue) {
  HwPortFb303Stats stats(kPortName, kQueue2Name);
  stats.queueChanged(1, "platinum");
  auto newQueueMapping = kQueue2Name;
  for (auto statKey : stats.kQueueMonotonicCounterStatKeys()) {
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(statKey, kPortName, 1, "platinum")));
    EXPECT_FALSE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(statKey, kPortName, 1, "gold")));
    // No impact on silver
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(statKey, kPortName, 2, "silver")));
  }
}

TEST(HwPortFb303StatsTest, AddQueue) {
  HwPortFb303Stats stats(kPortName, kQueue2Name);
  stats.queueChanged(3, "platinum");
  auto newQueueMapping = kQueue2Name;
  for (auto statKey : stats.kQueueMonotonicCounterStatKeys()) {
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(statKey, kPortName, 1, "gold")));
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(statKey, kPortName, 2, "silver")));
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(statKey, kPortName, 3, "platinum")));
  }
}

TEST(HwPortFb303StatsTest, RemoveQueue) {
  HwPortFb303Stats stats(kPortName, kQueue2Name);
  stats.queueRemoved(1);
  auto newQueueMapping = kQueue2Name;
  for (auto statKey : stats.kQueueMonotonicCounterStatKeys()) {
    EXPECT_FALSE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(statKey, kPortName, 1, "gold")));
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(statKey, kPortName, 2, "silver")));
  }
}

TEST(HwPortFb303Stats, portNameChangeResetsValue) {
  HwPortFb303Stats portStats(kPortName, kQueue2Name);
  updateStats(portStats);
  auto kNewPortName = "fab1/1/1";
  portStats.portNameChanged(kNewPortName);
  for (auto counterName : portStats.kPortMonotonicCounterStatKeys()) {
    EXPECT_EQ(
        portStats.getCounterLastIncrement(
            HwPortFb303Stats::statName(counterName, kNewPortName)),
        0);
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(counterName, kNewPortName)));
    EXPECT_FALSE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(counterName, kPortName)));
  }
  // fb303 counters simply get deleted on name change. Only to be added
  // back on next update
  for (auto statKey : portStats.kPortFb303CounterStatKeys()) {
    EXPECT_FALSE(
        fbData
            ->getCounterIfExists(HwPortFb303Stats::statName(statKey, kPortName))
            .has_value());
    // For fb303 stats - the new counter will be updated on the next
    // update a not on name change
    EXPECT_FALSE(fbData
                     ->getCounterIfExists(
                         HwPortFb303Stats::statName(statKey, kNewPortName))
                     .has_value());
  }
  for (auto counterName : portStats.kQueueMonotonicCounterStatKeys()) {
    for (const auto& queueIdAndName : kQueue2Name) {
      EXPECT_TRUE(fbData->getStatMap()->contains(
          HwPortFb303Stats::statName(
              counterName,
              kNewPortName,
              queueIdAndName.first,
              queueIdAndName.second)));
      EXPECT_EQ(
          portStats.getCounterLastIncrement(
              HwPortFb303Stats::statName(
                  counterName,
                  kNewPortName,
                  queueIdAndName.first,
                  queueIdAndName.second)),
          0);
      EXPECT_FALSE(fbData->getStatMap()->contains(
          HwPortFb303Stats::statName(
              counterName,
              kPortName,
              queueIdAndName.first,
              queueIdAndName.second)));
    }
  }
}

TEST(HwPortFb303Stats, queueNameChangeResetsValue) {
  HwPortFb303Stats portStats(kPortName, kQueue2Name);
  updateStats(portStats);
  portStats.queueChanged(1, "platinum");
  portStats.queueChanged(2, "bronze");
  HwPortFb303Stats::QueueId2Name newQueues = {{1, "platinum"}, {2, "bronze"}};
  for (auto counterName : portStats.kQueueMonotonicCounterStatKeys()) {
    for (const auto& queueIdAndName : newQueues) {
      EXPECT_TRUE(fbData->getStatMap()->contains(
          HwPortFb303Stats::statName(
              counterName,
              kPortName,
              queueIdAndName.first,
              queueIdAndName.second)));
      EXPECT_EQ(
          portStats.getCounterLastIncrement(
              HwPortFb303Stats::statName(
                  counterName,
                  kPortName,
                  queueIdAndName.first,
                  queueIdAndName.second)),
          0);
    }
  }
  for (auto counterName : portStats.kQueueMonotonicCounterStatKeys()) {
    for (const auto& queueIdAndName : kQueue2Name) {
      EXPECT_FALSE(fbData->getStatMap()->contains(
          HwPortFb303Stats::statName(
              counterName,
              kPortName,
              queueIdAndName.first,
              queueIdAndName.second)));
    }
  }
}

TEST(HwPortFb303StatsTest, Srv6CounterNotCreatedWhenDisabled) {
  HwPortFb303Stats stats(kPortName);
  EXPECT_FALSE(fbData->getStatMap()->contains(
      HwPortFb303Stats::statName(kInSrv6MySidDiscards(), kPortName)));
}

TEST(HwPortFb303StatsTest, Srv6CounterCreatedWhenEnabled) {
  HwPortFb303Stats stats(
      kPortName,
      {} /*queueId2Name*/,
      {} /*enabledPfcPriorities*/,
      std::nullopt /*pfcCfg*/,
      false /*inCongestionDiscardCountSupported*/,
      false /*inCongestionDiscardSeenSupported*/,
      true /*srv6MysidDiscardCounterSupported*/);
  EXPECT_TRUE(fbData->getStatMap()->contains(
      HwPortFb303Stats::statName(kInSrv6MySidDiscards(), kPortName)));
}

TEST(HwPortFb303StatsTest, MplsCounterNotCreatedWhenDisabled) {
  HwPortFb303Stats stats(kPortName);
  EXPECT_FALSE(fbData->getStatMap()->contains(
      HwPortFb303Stats::statName(kInLabelMissDiscards(), kPortName)));
}

TEST(HwPortFb303StatsTest, MplsCounterCreatedWhenEnabled) {
  HwPortFb303Stats stats(
      kPortName,
      {} /*queueId2Name*/,
      {} /*enabledPfcPriorities*/,
      std::nullopt /*pfcCfg*/,
      false /*inCongestionDiscardCountSupported*/,
      false /*inCongestionDiscardSeenSupported*/,
      false /*srv6MysidDiscardCounterSupported*/,
      true /*mplsLabelLookupFailCounterSupported*/);
  EXPECT_TRUE(fbData->getStatMap()->contains(
      HwPortFb303Stats::statName(kInLabelMissDiscards(), kPortName)));
}

TEST(HwPortFb303StatsTest, Srv6CounterReinitOnPortNameChange) {
  constexpr auto kNewPortName = "eth1/2/1";
  HwPortFb303Stats stats(
      kPortName,
      {} /*queueId2Name*/,
      {} /*enabledPfcPriorities*/,
      std::nullopt /*pfcCfg*/,
      false /*inCongestionDiscardCountSupported*/,
      false /*inCongestionDiscardSeenSupported*/,
      true /*srv6MysidDiscardCounterSupported*/);
  stats.portNameChanged(kNewPortName);
  EXPECT_TRUE(fbData->getStatMap()->contains(
      HwPortFb303Stats::statName(kInSrv6MySidDiscards(), kNewPortName)));
  EXPECT_FALSE(fbData->getStatMap()->contains(
      HwPortFb303Stats::statName(kInSrv6MySidDiscards(), kPortName)));
}

TEST(HwPortFb303StatsTest, SllHllCountersNotCreatedWhenDisabled) {
  HwPortFb303Stats stats(kPortName);
  EXPECT_FALSE(fbData->getStatMap()->contains(
      HwPortFb303Stats::statName(kOutDiscardsSll(), kPortName)));
  EXPECT_FALSE(fbData->getStatMap()->contains(
      HwPortFb303Stats::statName(kOutDiscardsHll(), kPortName)));
}

TEST(HwPortFb303StatsTest, SllHllCountersCreatedWhenEnabled) {
  HwPortFb303Stats stats(
      kPortName,
      {} /*queueId2Name*/,
      {} /*enabledPfcPriorities*/,
      std::nullopt /*pfcCfg*/,
      false /*inCongestionDiscardCountSupported*/,
      false /*inCongestionDiscardSeenSupported*/,
      false /*srv6MysidDiscardCounterSupported*/,
      false /*mplsLabelLookupFailCounterSupported*/,
      false /*linkDebounceRetriggerCounterSupported*/,
      true /*sllHllDiscardCounterSupported*/);
  EXPECT_TRUE(fbData->getStatMap()->contains(
      HwPortFb303Stats::statName(kOutDiscardsSll(), kPortName)));
  EXPECT_TRUE(fbData->getStatMap()->contains(
      HwPortFb303Stats::statName(kOutDiscardsHll(), kPortName)));
}

TEST(HwPortFb303StatsTest, SllHllCountersReinitOnPortNameChange) {
  constexpr auto kNewPortName = "eth1/2/1";
  HwPortFb303Stats stats(
      kPortName,
      {} /*queueId2Name*/,
      {} /*enabledPfcPriorities*/,
      std::nullopt /*pfcCfg*/,
      false /*inCongestionDiscardCountSupported*/,
      false /*inCongestionDiscardSeenSupported*/,
      false /*srv6MysidDiscardCounterSupported*/,
      false /*mplsLabelLookupFailCounterSupported*/,
      false /*linkDebounceRetriggerCounterSupported*/,
      true /*sllHllDiscardCounterSupported*/);
  stats.portNameChanged(kNewPortName);
  EXPECT_TRUE(fbData->getStatMap()->contains(
      HwPortFb303Stats::statName(kOutDiscardsSll(), kNewPortName)));
  EXPECT_FALSE(fbData->getStatMap()->contains(
      HwPortFb303Stats::statName(kOutDiscardsSll(), kPortName)));
}

TEST(HwPortFb303StatsTest, SllHllCountersNotUpdatedWhenDisabled) {
  HwPortFb303Stats stats(kPortName);
  // Stats carrying SLL/HLL values must be a no-op on an ASIC that does not
  // support them - the counters were never created.
  updateStats(stats);
  EXPECT_FALSE(fbData->getStatMap()->contains(
      HwPortFb303Stats::statName(kOutDiscardsSll(), kPortName)));
  EXPECT_FALSE(fbData->getStatMap()->contains(
      HwPortFb303Stats::statName(kOutDiscardsHll(), kPortName)));
}

TEST(HwPortFb303StatsTest, ChangePfcConfig) {
  HwPortFb303Stats stats(kPortName, kQueue2Name, kEnabledPfcPriorities);
  std::vector<PfcPriority> newPriorities(
      {static_cast<PfcPriority>(5), static_cast<PfcPriority>(6)});
  stats.pfcConfigChanged(newPriorities, std::nullopt);
  for (auto statKey : stats.kPfcMonotonicCounterStatKeys()) {
    for (auto pfcPriority : kEnabledPfcPriorities) {
      EXPECT_FALSE(fbData->getStatMap()->contains(
          HwPortFb303Stats::statName(statKey, kPortName, pfcPriority)));
    }
    for (auto pfcPriority : newPriorities) {
      EXPECT_TRUE(fbData->getStatMap()->contains(
          HwPortFb303Stats::statName(statKey, kPortName, pfcPriority)));
    }
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(statKey, kPortName)));
  }
  for (auto statKey : stats.kPfcDeadlockMonotonicCounterStatKeys()) {
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(statKey, kPortName)));
  }
}

TEST(HwPortFb303StatsTest, DisablePfcDurationWithStaleStats) {
  cfg::PortPfc pfc;
  pfc.txPfcDurationEnable() = true;
  pfc.rxPfcDurationEnable() = true;
  HwPortFb303Stats stats(
      kPortName,
      kQueue2Name,
      kEnabledPfcPriorities,
      pfc,
      true /* inCongestionDiscardCountSupported */,
      true /* inCongestionDiscardSeenSupported */);
  auto portStats = getInitedStats();
  for (const auto& priority : kEnabledPfcPriorities) {
    portStats.txPfcDurationUsec_()[priority] = 10;
    portStats.rxPfcDurationUsec_()[priority] = 20;
  }
  stats.updateStats(portStats, seconds(1));
  for (const auto& priority : kEnabledPfcPriorities) {
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(kTxPfcDurationUsec(), kPortName, priority)));
    EXPECT_TRUE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(kRxPfcDurationUsec(), kPortName, priority)));
  }

  // Disabling removes the counters, but stats collected before the disable
  // still carry duration values: updating them must not abort.
  pfc.txPfcDurationEnable() = false;
  pfc.rxPfcDurationEnable() = false;
  stats.pfcConfigChanged(kEnabledPfcPriorities, pfc);
  stats.updateStats(portStats, seconds(2));
  for (const auto& priority : kEnabledPfcPriorities) {
    EXPECT_FALSE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(kTxPfcDurationUsec(), kPortName, priority)));
    EXPECT_FALSE(fbData->getStatMap()->contains(
        HwPortFb303Stats::statName(kRxPfcDurationUsec(), kPortName, priority)));
  }
}

TEST(HwPortFb303StatsTest, ChangePfcPrioritiesWithDurationEnabled) {
  cfg::PortPfc pfc;
  pfc.txPfcDurationEnable() = true;
  pfc.rxPfcDurationEnable() = true;
  HwPortFb303Stats stats(
      kPortName,
      kQueue2Name,
      kEnabledPfcPriorities,
      pfc,
      true /* inCongestionDiscardCountSupported */,
      true /* inCongestionDiscardSeenSupported */);
  std::vector<PfcPriority> newPriorities(
      {static_cast<PfcPriority>(5), static_cast<PfcPriority>(6)});
  stats.pfcConfigChanged(newPriorities, pfc);
  for (auto statKey : {kTxPfcDurationUsec(), kRxPfcDurationUsec()}) {
    for (const auto& priority : kEnabledPfcPriorities) {
      EXPECT_FALSE(fbData->getStatMap()->contains(
          HwPortFb303Stats::statName(statKey, kPortName, priority)));
    }
    for (const auto& priority : newPriorities) {
      EXPECT_TRUE(fbData->getStatMap()->contains(
          HwPortFb303Stats::statName(statKey, kPortName, priority)));
    }
  }

  // Duration stats are collected for every priority, so publishing them for
  // the newly enabled priorities must find their counters.
  auto portStats = getInitedStats();
  for (int priority = 0; priority <= 7; ++priority) {
    portStats.txPfcDurationUsec_()[priority] = 10;
    portStats.rxPfcDurationUsec_()[priority] = 20;
  }
  stats.updateStats(portStats, seconds(1));
}
