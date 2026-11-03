/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AgentTestAddressConstants.h"
#include "fboss/agent/test/utils/AgentTestEcmpConstants.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"
#include "fboss/agent/test/utils/PortTestUtils.h"
#include "fboss/lib/CommonUtils.h"

namespace facebook::fboss {

class AgentMmuTuningTest : public AgentHwTest {
 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = utility::oneL3IntfConfig(
        ensemble.getSw()->getPlatformMapping(),
        ensemble.getL3Asics(),
        ensemble.masterLogicalPortIds()[0],
        ensemble.getSw()->getPlatformSupportsAddRemovePort());
    addQosMap(&cfg);
    addQueueConfig(&cfg, ensemble.getL3Asics());
    utility::addCpuQueueConfig(cfg, ensemble.getL3Asics(), ensemble.isSai());
    return cfg;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::L3_QOS,
        ProductionFeature::PORT_TX_DISABLE,
        ProductionFeature::MMU_TUNING};
  }

  void setup() {
    utility::EcmpSetupAnyNPorts6 helper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor(), dstMac());
    resolveNeighborAndProgramRoutes(helper, kDefaultEcmpWidth);
    utility::setCreditWatchdogAndPortTx(
        getAgentEnsemble(), masterLogicalPortIds()[0], false);
  }
  void verify(
      int16_t lowPriQueue,
      int16_t highPriQueue,
      uint8_t lowPriDscp,
      uint8_t highPriDscp) {
    // Send  MMU Size+ bytes. With port TX disabled, all these bytes will be
    // buffered in MMU. The higher pri queue should then endup using more of MMU
    // than lower pri queue.
    sendUdpPkts(lowPriDscp, highPriDscp);

    WITH_RETRIES({
      auto portStats = getLatestPortStats(masterLogicalPortIds()[0]);
      auto queueOutDiscardBytes = *portStats.queueOutDiscardBytes_();
      auto queueOutDiscardPackets = *portStats.queueOutDiscardPackets_();
      auto queueWaterMarks = *portStats.queueWatermarkBytes_();
      XLOG(DBG2) << " Port discards: " << *portStats.outDiscards_()
                 << " low pri queue discards, bytes: "
                 << queueOutDiscardBytes[lowPriQueue]
                 << " packets: " << queueOutDiscardPackets[lowPriQueue]
                 << " high pri queue discards, bytes: "
                 << queueOutDiscardBytes[highPriQueue]
                 << " packets: " << queueOutDiscardPackets[highPriQueue];
      auto lowPriWatermark = queueWaterMarks[lowPriQueue];
      auto highPriWatermark = queueWaterMarks[highPriQueue];
      XLOG(DBG2) << " Low pri queue ( " << lowPriQueue
                 << " ) watermark: " << lowPriWatermark << " High pri queue ( "
                 << highPriQueue << " ) watermark: " << highPriWatermark;
      EXPECT_EVENTUALLY_TRUE(
          highPriWatermark >= lowPriWatermark &&
          *portStats.outDiscards_() > 0 &&
          queueOutDiscardBytes[lowPriQueue] > 0 &&
          queueOutDiscardBytes[highPriQueue] > 0 &&
          queueOutDiscardPackets[lowPriQueue] > 0 &&
          queueOutDiscardPackets[highPriQueue] > 0);
    });
    // After verification, enable tx to let packets go through.
    // New SDK expects buffer to be empty during teardown.
    if (!FLAGS_setup_for_warmboot) {
      utility::setCreditWatchdogAndPortTx(
          getAgentEnsemble(), masterLogicalPortIds()[0], true);
    }
  }

 private:
  void sendUdpPkts(uint8_t lowPriDscp, uint8_t highPriDscp) {
    auto mmuSizeBytes =
        checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
            ->getMMUSizeBytes();
    auto bytesSent = 0;
    // Send high pri DSCP packet followed by low pri DSCP packet
    // since in cases where MMU is split evenly (viz. tuning reserved
    // bytes on platforms that use queue groups), at the edge of MMU fill
    // it matters which packet fills up the MMU last. In the worst case
    // the low pri queue may endup getting 1 more packet then high pri
    // queue and tripping up the test.
    std::vector<uint8_t> dscpsToSend{highPriDscp, lowPriDscp};
    // Fill entire MMU and then some
    while (bytesSent < mmuSizeBytes + 20000) {
      for (auto dscp : dscpsToSend) {
        auto pkt = createUdpPkt(dscp);
        bytesSent += pkt->buf()->computeChainDataLength();
        sendPacketSwitchedAsync(std::move(pkt));
      }
    }

    // Block until packets sent and stats updated
    WITH_RETRIES({
      auto portStats = getLatestPortStats(masterLogicalPortIds()[0]);
      auto outDiscard = *portStats.outDiscards_();
      XLOG(DBG0) << "Out discard : " << outDiscard;
      EXPECT_EVENTUALLY_GT(outDiscard, 0);
    });
  }
  MacAddress dstMac() const {
    return getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
  }
  std::unique_ptr<facebook::fboss::TxPacket> createUdpPkt(
      uint8_t dscpVal) const {
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
        std::vector<uint8_t>(7000, 0xff));
  }

  void addQosMap(cfg::SwitchConfig* cfg) const {
    cfg::QosMap qosMap;
    std::map<int, std::vector<uint8_t>> queue2Dscp = {
        {0, {0}}, {1, {1}}, {2, {2}}, {3, {3}}};

    for (auto dscp = 4; dscp < 64; ++dscp) {
      queue2Dscp[0].push_back(dscp);
    }
    qosMap.dscpMaps()->resize(queue2Dscp.size());
    ssize_t qosMapIdx = 0;
    for (const auto& q2dscps : queue2Dscp) {
      auto [q, dscps] = q2dscps;
      qosMap.dscpMaps()[qosMapIdx].internalTrafficClass() = q;
      for (auto dscp : dscps) {
        qosMap.dscpMaps()[qosMapIdx].fromDscpToTrafficClass()->push_back(dscp);
      }
      qosMap.trafficClassToQueueId()->emplace(q, q);
      ++qosMapIdx;
    }
    cfg->qosPolicies()->resize(1);
    cfg->qosPolicies()[0].name() = "qp";
    cfg->qosPolicies()[0].qosMap() = qosMap;

    cfg::TrafficPolicyConfig dataPlaneTrafficPolicy;
    dataPlaneTrafficPolicy.defaultQosPolicy() = "qp";
    cfg->dataPlaneTrafficPolicy() = dataPlaneTrafficPolicy;
    cfg::CPUTrafficPolicyConfig cpuConfig;
    cfg::TrafficPolicyConfig cpuTrafficPolicy;
    cpuTrafficPolicy.defaultQosPolicy() = "qp";
    cpuConfig.trafficPolicy() = cpuTrafficPolicy;
    cfg->cpuTrafficPolicy() = cpuConfig;
  }

  void addQueueConfig(
      cfg::SwitchConfig* config,
      const std::vector<const HwAsic*>& asics) const {
    auto asic = checkSameAndGetAsicForTesting(asics);
    auto streamType =
        *(utility::getStreamType(cfg::PortType::INTERFACE_PORT, asics).begin());
    bool scalingFactorSupported =
        asic->scalingFactorBasedDynamicThresholdSupported();
    std::vector<cfg::PortQueue> portQueues;

    // Queue 0 and 1 tune reserved bytes
    cfg::PortQueue queue0;
    queue0.id() = 0;
    queue0.name() = "queue0";
    queue0.streamType() = streamType;
    queue0.scheduling() = cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
    queue0.weight() = 1;
    portQueues.push_back(queue0);

    cfg::PortQueue queue1;
    queue1.id() = 1;
    queue1.name() = "queue1";
    queue1.streamType() = streamType;
    queue1.scheduling() = cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
    queue1.weight() = 1;
    if (!asic->mmuQgroupsEnabled()) {
      queue1.reservedBytes() = 9984;
    }
    portQueues.push_back(queue1);

    // Queue 2 and 3 tune scaling factor
    cfg::PortQueue queue2;
    queue2.id() = 2;
    queue2.name() = "queue2";
    queue2.streamType() = streamType;
    queue2.scheduling() = cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
    queue2.weight() = 1;
    if (scalingFactorSupported) {
      queue2.scalingFactor() = cfg::MMUScalingFactor::ONE;
    }
    portQueues.push_back(queue2);

    cfg::PortQueue queue3;
    queue3.id() = 3;
    queue3.name() = "queue3";
    queue3.streamType() = streamType;
    queue3.scheduling() = cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
    queue3.weight() = 1;
    if (scalingFactorSupported) {
      queue3.scalingFactor() = cfg::MMUScalingFactor::EIGHT;
    }
    portQueues.push_back(queue3);

    config->portQueueConfigs()["queue_config"] = portQueues;
    for (auto& port : *config->ports()) {
      port.portQueueConfigName() = "queue_config";
    }
  }
};

TEST_F(AgentMmuTuningTest, verifyReservedBytesTuning) {
  verifyAcrossWarmBoots(
      [this]() { setup(); }, [this]() { verify(0, 1, 0, 1); });
}

TEST_F(AgentMmuTuningTest, verifyScalingFactorTuning) {
  verifyAcrossWarmBoots(
      [this]() { setup(); }, [this]() { verify(2, 3, 2, 3); });
}
} // namespace facebook::fboss
