/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/test/agent_hw_tests/AgentQosSchedulerTestBase.h"

#include "fboss/agent/test/utils/AgentTestAddressConstants.h"
#include "fboss/agent/test/utils/AgentTestEcmpConstants.h"
#include "fboss/agent/test/utils/QosTestUtils.h"

namespace {
auto constexpr kRateSamplingInterval = 25;
} // namespace

namespace facebook::fboss {
std::unique_ptr<facebook::fboss::TxPacket>
AgentQosSchedulerTestBase::createUdpPkt(uint8_t dscpVal) const {
  auto srcMac = utility::MacAddressGenerator().get(dstMac().u64HBO() + 1);

  return utility::makeUDPTxPacket(
      getSw(),
      getVlanIDForTx(),
      srcMac,
      dstMac(),
      folly::IPAddressV6(kTestSrcIpV6),
      folly::IPAddressV6(kTestDstIpV6),
      kTestSrcPort,
      kTestDstPort,
      // Trailing 2 bits are for ECN
      static_cast<uint8_t>(dscpVal << 2),
      // Hop limit
      255,
      // Payload
      std::vector<uint8_t>(1200, 0xff));
}

void AgentQosSchedulerTestBase::sendUdpPkt(uint8_t dscpVal, bool frontPanel) {
  utility::EcmpSetupAnyNPorts6 ecmpHelper6(
      getProgrammedState(), getSw()->needL2EntryForNeighbor());
  if (frontPanel) {
    getSw()->sendPacketOutOfPortAsync(
        createUdpPkt(dscpVal),
        ecmpHelper6.ecmpPortDescriptorAt(kDefaultEcmpWidth).phyPortID());
  } else {
    sendPacketSwitchedAsync(createUdpPkt(dscpVal));
  }
}

void AgentQosSchedulerTestBase::sendUdpPkts(
    uint8_t dscpVal,
    int cnt,
    bool frontPanel) {
  for (int i = 0; i < cnt; i++) {
    sendUdpPkt(dscpVal, frontPanel);
  }
}

void AgentQosSchedulerTestBase::_setup(
    const utility::EcmpSetupAnyNPorts6& ecmpHelper6) {
  resolveNeighborAndProgramRoutes(ecmpHelper6, kDefaultEcmpWidth, true);
  utility::disablePortTTLDecrementIfSupported(
      getAgentEnsemble(), ecmpHelper6.getRouterId(), ecmpHelper6.nhop(0));
}

void AgentQosSchedulerTestBase::sendUdpPktsForAllQueues(
    const std::vector<int>& queueIds,
    const std::map<int, std::vector<uint8_t>>& queueToDscp,
    bool frontPanel) {
  auto port = getProgrammedState()->getPort(outPort());
  auto queues = port->getPortQueues()->impl();
  std::set<int> wrrQueues;
  std::set<int> spQueues;
  for (const auto& queue : std::as_const(queues)) {
    if (std::find(queueIds.begin(), queueIds.end(), queue->getID()) !=
        queueIds.end()) {
      if (queue->getScheduling() ==
          cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN) {
        wrrQueues.insert(queue->getID());
      } else if (
          queue->getScheduling() == cfg::QueueScheduling::STRICT_PRIORITY) {
        spQueues.insert(queue->getID());
      }
    }
  }
  auto pktsToSend = getAgentEnsemble()->getMinPktsForLineRate(outPort());
  // send traffic to wrr queues first
  for (auto queue : wrrQueues) {
    XLOG(DBG2) << "send traffic to wrr queue " << queue << " with "
               << pktsToSend << " packets";
    sendUdpPkts(queueToDscp.at(queue).front(), pktsToSend, frontPanel);
  }
  // send traffic to sp queues from low priority to high priority
  for (auto queue : spQueues) {
    XLOG(DBG2) << "send traffic to sp queue " << queue << " with " << pktsToSend
               << " packets";
    sendUdpPkts(queueToDscp.at(queue).front(), pktsToSend, frontPanel);
  }
}

void AgentQosSchedulerTestBase::_verifyDscpQueueMappingHelper(
    const std::map<int, std::vector<uint8_t>>& queueToDscp) {
  auto portId = outPort();
  auto portStatsBefore = getLatestPortStats(portId);
  for (const auto& q2dscps : queueToDscp) {
    for (auto dscp : q2dscps.second) {
      sendUdpPkt(dscp);
    }
  }
  WITH_RETRIES({
    EXPECT_EVENTUALLY_TRUE(
        utility::verifyQueueMappings(
            portStatsBefore, queueToDscp, getAgentEnsemble(), portId));
  });
}

// Packets processed by WRR queues should be in proportion to their weights
bool AgentQosSchedulerTestBase::verifyWRRHelper(
    int maxWeightQueueId,
    const std::map<int, uint8_t>& wrrQueueToWeight,
    const std::vector<int>& queueIds,
    const std::map<int, std::vector<uint8_t>>& queueToDscp) {
  {
    /*
     * The normalized stats (stats/weight) for every queue should be identical
     * (within certain percentage variance).
     *
     * We compare normalized stats for every queue against normalized stats
     * for queue with max weight (or better precesion).
     */
    const double kVariance = 0.10; // i.e. + or -10%
    auto portId = outPort();
    auto startTrafficFun = [this, portId, queueIds, queueToDscp]() {
      utility::EcmpSetupAnyNPorts6 ecmpHelper6(
          getProgrammedState(), getSw()->needL2EntryForNeighbor(), dstMac());
      _setup(ecmpHelper6);
      sendUdpPktsForAllQueues(queueIds, queueToDscp);
      getAgentEnsemble()->waitForLineRateOnPort(portId);
    };
    WITH_RETRIES_N(
        10, ({
          auto [portStatsBefore, portStatsAfter] =
              sendTrafficAndCollectStats(
                  {portId},
                  kRateSamplingInterval,
                  startTrafficFun,
                  []() {},
                  true /*keepTrafficRunning*/)
                  .cbegin()
                  ->second;
          auto queueStatsBefore = portStatsBefore.queueOutPackets_();
          auto queueStatsAfter = portStatsAfter.queueOutPackets_();
          auto maxWeightQueueBytes =
              (queueStatsAfter->find(maxWeightQueueId)->second -
               queueStatsBefore->find(maxWeightQueueId)->second) /
              (*portStatsAfter.timestamp_() - *portStatsBefore.timestamp_());
          auto maxWeightQueueWeight = wrrQueueToWeight.at(maxWeightQueueId);
          auto maxWeightQueueNormalizedBytes =
              maxWeightQueueBytes / maxWeightQueueWeight;
          auto lowMaxWeightQueueNormalizedBytes =
              maxWeightQueueNormalizedBytes * (1 - kVariance);
          auto highMaxWeightQueueNormalizedBytes =
              maxWeightQueueNormalizedBytes * (1 + kVariance);

          XLOG(DBG0) << "Max weight:: QueueId: " << maxWeightQueueId
                     << " stat: " << maxWeightQueueBytes
                     << " weight: " << static_cast<int>(maxWeightQueueWeight)
                     << " normalized (stat/weight): "
                     << maxWeightQueueNormalizedBytes
                     << " low normalized: " << lowMaxWeightQueueNormalizedBytes
                     << " high normalized: "
                     << highMaxWeightQueueNormalizedBytes;
          for (const auto& queueStat : *queueStatsAfter) {
            auto currQueueId = queueStat.first;
            auto currQueueBytes =
                (queueStat.second - queueStatsBefore[currQueueId]) /
                (*portStatsAfter.timestamp_() - *portStatsBefore.timestamp_());
            if (wrrQueueToWeight.find(currQueueId) == wrrQueueToWeight.end()) {
              EXPECT_FALSE(currQueueBytes);
              continue;
            }
            auto currQueueWeight = wrrQueueToWeight.at(currQueueId);
            auto currQueueNormalizedBytes = currQueueBytes / currQueueWeight;

            XLOG(DBG0) << "Curr queue :: QueueId: " << currQueueId
                       << " stat: " << currQueueBytes
                       << " weight: " << static_cast<int>(currQueueWeight)
                       << " normalized (stat/weight): "
                       << currQueueNormalizedBytes;

            EXPECT_EVENTUALLY_TRUE(
                lowMaxWeightQueueNormalizedBytes < currQueueNormalizedBytes &&
                currQueueNormalizedBytes < highMaxWeightQueueNormalizedBytes);
          }
        }));
  }
  return true;
}

// Only trafficQueueId should have traffic
bool AgentQosSchedulerTestBase::verifySPHelper(
    int trafficQueueId,
    const std::vector<int>& queueIds,
    const std::map<int, std::vector<uint8_t>>& queueToDscp,
    bool fromFrontPanel) {
  XLOG(DBG0) << "trafficQueueId: " << trafficQueueId;
  auto portId = outPort();
  auto startTrafficFun =
      [this, portId, queueIds, queueToDscp, fromFrontPanel]() {
        utility::EcmpSetupAnyNPorts6 ecmpHelper6(
            getProgrammedState(), getSw()->needL2EntryForNeighbor(), dstMac());
        _setup(ecmpHelper6);
        sendUdpPktsForAllQueues(queueIds, queueToDscp, fromFrontPanel);
        getAgentEnsemble()->waitForLineRateOnPort(portId);
      };
  WITH_RETRIES_N(10, {
    auto [portStatsBefore, portStatsAfter] = sendTrafficAndCollectStats(
                                                 {portId},
                                                 kRateSamplingInterval,
                                                 startTrafficFun,
                                                 []() {},
                                                 true /*keepTrafficRunning*/)
                                                 .cbegin()
                                                 ->second;
    auto queueStatsBefore = portStatsBefore.queueOutPackets_();
    auto queueStatsAfter = portStatsAfter.queueOutPackets_();
    for (const auto& queueStat : *queueStatsAfter) {
      auto queueId = queueStat.first;
      auto statVal = (queueStat.second - queueStatsBefore[queueId]) /
          (*portStatsAfter.timestamp_() - *portStatsBefore.timestamp_());
      XLOG(DBG0) << "QueueId: " << queueId << " stats: " << statVal;
      EXPECT_EVENTUALLY_TRUE(
          queueId != trafficQueueId ? statVal == 0 : statVal != 0);
    }
  });
  return true;
}

} // namespace facebook::fboss
