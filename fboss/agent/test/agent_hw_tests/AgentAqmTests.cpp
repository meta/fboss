// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/packet/EthHdr.h"
#include "fboss/agent/packet/IPv6Hdr.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/packet/TCPHeader.h"
#include "fboss/agent/test/AgentEnsemble.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/ResourceLibUtil.h"
#include "fboss/agent/test/TestEnsembleIf.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AgentTestAddressConstants.h"
#include "fboss/agent/test/utils/AqmTestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/NetworkAITestUtils.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"
#include "fboss/agent/test/utils/PortStatsTestUtils.h"
#include "fboss/agent/test/utils/QosTestUtils.h"
#include "fboss/agent/types.h"
#include "fboss/lib/CommonUtils.h"

namespace {
constexpr uint8_t kECT1{1}; // ECN capable transport(1), ECT(1)
constexpr uint8_t kECT0{2}; // ECN capable transport(0), ECT(0)
constexpr uint8_t kNotECT{0}; // Not ECN-Capable Transport, Not-ECT
constexpr auto kDefaultTxPayloadBytes{7000};

struct AqmTestStats {
  uint64_t wredDroppedPackets;
  uint64_t outEcnCounter;
  uint64_t outPackets;
};

struct AqmThresholdsConfig {
  bool enableEcn{false};
  bool enableWred{false};
};

/*
 * Ensure that the number of dropped packets is as expected. Allow for
 * an error to account for more / less drops while its worked out.
 */
void verifyWredDroppedPacketCount(
    const AqmTestStats& after,
    const AqmTestStats& before,
    int expectedDroppedPkts) {
  const int acceptableErrorPct{10};
  int64_t deltaWredDroppedPackets =
      static_cast<int64_t>(after.wredDroppedPackets) -
      before.wredDroppedPackets;
  XLOG(DBG0) << "Delta WRED dropped pkts: " << deltaWredDroppedPackets;

  int allowedDeviation = acceptableErrorPct * expectedDroppedPkts / 100;
  EXPECT_NEAR(deltaWredDroppedPackets, expectedDroppedPkts, allowedDeviation);
}

/*
 * Due to the way packet marking happens, we might not have an accurate count,
 * but the number of packets marked will be >= to the expected marked packet
 * count.
 */
void verifyEcnMarkedPacketCount(
    const AqmTestStats& after,
    const AqmTestStats& before,
    int expectedMarkedPkts) {
  uint64_t deltaEcnMarkedPackets = after.outEcnCounter - before.outEcnCounter;
  uint64_t deltaOutPackets = after.outPackets - before.outPackets;
  XLOG(DBG0) << "Delta ECN marked pkts: " << deltaEcnMarkedPackets
             << ", delta out packets: " << deltaOutPackets;
  EXPECT_GE(after.outEcnCounter, before.outEcnCounter + expectedMarkedPkts);
  EXPECT_GT(after.outPackets, before.outPackets + deltaEcnMarkedPackets);
}

} // namespace

namespace facebook::fboss {
class AgentAqmTest : public AgentHwTest {
 public:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    cfg::SwitchConfig config = utility::onePortPerInterfaceConfig(
        ensemble.getSw(), ensemble.masterLogicalPortIds());
    if (ensemble.getHwAsicTable()->isFeatureSupportedOnAllAsic(
            HwAsic::Feature::L3_QOS)) {
      if (isDualStage3Q2QQos()) {
        auto hwAsic = checkSameAndGetAsicForTesting(ensemble.getL3Asics());
        cfg::StreamType streamType =
            *hwAsic->getQueueStreamTypes(cfg::PortType::INTERFACE_PORT).begin();
        utility::addNetworkAIQueueConfig(
            &config,
            streamType,
            cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN,
            hwAsic);
      } else {
        utility::addOlympicQueueConfig(&config, ensemble.getL3Asics());
      }
      utility::addOlympicQosMaps(config, ensemble.getL3Asics());
    }
    utility::setTTLZeroCpuConfig(ensemble.getL3Asics(), config);
    return config;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::ECN, ProductionFeature::WRED};
  }

 protected:
  uint8_t kSilverDscp() const {
    return 0;
  }

  uint8_t kDscp() const {
    return 5;
  }

  bool isEct(const uint8_t ecnVal) {
    return ecnVal != kNotECT;
  }

  folly::MacAddress getIntfMac() const {
    return getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
  }

  void sendPkt(
      uint8_t dscpVal,
      const uint8_t ecnVal,
      int payloadLen = kDefaultTxPayloadBytes,
      int ttl = 255,
      std::optional<PortID> outPort = std::nullopt) {
    dscpVal = static_cast<uint8_t>(dscpVal << 2);
    dscpVal |= ecnVal;

    auto vlanId = getVlanIDForTx();
    auto intfMac = getIntfMac();
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    auto txPacket = utility::makeTCPTxPacket(
        getSw(),
        vlanId,
        srcMac,
        intfMac,
        folly::IPAddressV6(kTestSrcIpV6),
        folly::IPAddressV6(kTestDstIpV6),
        kTestSrcPort,
        kTestDstPort,
        dscpVal,
        ttl,
        std::vector<uint8_t>(payloadLen, 0xff));

    if (outPort) {
      getSw()->sendPacketOutOfPortAsync(std::move(txPacket), *outPort);
    } else {
      sendPacketSwitchedAsync(std::move(txPacket));
    }
  }

  /*
   * For congestion detection queue length of minLength = 128, and maxLength =
   * 128, a packet count of 128 has been enough to cause ECN marking. Inject
   * 128 * 2 packets to avoid test noise.
   */
  void sendPkts(
      uint8_t dscpVal,
      const uint8_t ecnVal,
      int cnt = 256,
      int payloadLen = kDefaultTxPayloadBytes,
      int ttl = 255,
      std::optional<PortID> outPort = std::nullopt) {
    for (int i = 0; i < cnt; i++) {
      sendPkt(dscpVal, ecnVal, payloadLen, ttl, outPort);
    }
  }

  void queueShaperAndBurstSetup(
      const std::vector<int>& queueIds,
      cfg::SwitchConfig& config,
      uint32_t minKbps,
      uint32_t maxKbps,
      uint32_t minBurstKb,
      uint32_t maxBurstKb) {
    for (auto queueId : queueIds) {
      utility::addQueueShaperConfig(&config, queueId, minKbps, maxKbps);
      utility::addQueueBurstSizeConfig(
          &config, queueId, minBurstKb, maxBurstKb);
    }
  }

  virtual cfg::SwitchConfig configureQueue2WithAqmThreshold(
      bool enableWred,
      bool enableEcn) const {
    auto config = utility::onePortPerInterfaceConfig(
        getSw(), masterLogicalPortIds(), true /*interfaceHasSubnet*/);
    if (getAgentEnsemble()->getHwAsicTable()->isFeatureSupportedOnAllAsic(
            HwAsic::Feature::L3_QOS)) {
      auto hwAsic =
          checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
      auto streamType =
          *hwAsic->getQueueStreamTypes(cfg::PortType::INTERFACE_PORT).begin();
      if (isDualStage3Q2QQos()) {
        utility::addNetworkAIQueueConfig(
            &config,
            streamType,
            cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN,
            hwAsic,
            enableWred,
            enableEcn);
      } else {
        utility::addOlympicQueueConfig(
            &config, getAgentEnsemble()->getL3Asics(), enableWred, enableEcn);
        if (hwAsic->getSwitchType() == cfg::SwitchType::VOQ) {
          utility::addVoqAqmConfig(
              &config, streamType, hwAsic, enableWred, enableEcn);
        }
      }
      utility::addOlympicQosMaps(config, getAgentEnsemble()->getL3Asics());
    }
    utility::setTTLZeroCpuConfig(getAgentEnsemble()->getL3Asics(), config);
    return config;
  }

  void setupEcmpTraffic() {
    utility::EcmpSetupTargetedPorts6 ecmpHelper{
        getProgrammedState(), getSw()->needL2EntryForNeighbor(), getIntfMac()};
    const auto& portDesc = PortDescriptor(masterLogicalInterfacePortIds()[0]);
    applyNewState([&ecmpHelper, &portDesc](std::shared_ptr<SwitchState> in) {
      return ecmpHelper.resolveNextHops(in, {portDesc});
    });
    RoutePrefixV6 route{folly::IPAddressV6(kTestDstIpV6), 128};
    auto wrapper = getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(
        &wrapper, {portDesc}, {route}, {}, std::nullopt, true);
    utility::disablePortTTLDecrementIfSupported(
        getAgentEnsemble(),
        ecmpHelper.getRouterId(),
        ecmpHelper.getNextHops()[0]);
  }

  /*
   * ECN/WRED traffic is always sent on specific queues, identified by queueId.
   * However, AQM stats are collected either from queue or from port depending
   * on test requirement, specified using useQueueStatsForAqm.
   */
  void extractAqmTestStats(
      const HwPortStats& portStats,
      const uint8_t queueId,
      bool useQueueStatsForAqm,
      AqmTestStats& stats) {
    if (useQueueStatsForAqm) {
      if (isSupportedOnAllAsics(HwAsic::Feature::QUEUE_ECN_COUNTER)) {
        stats.outEcnCounter +=
            portStats.queueEcnMarkedPackets_().value().find(queueId)->second;
      }
      stats.wredDroppedPackets +=
          portStats.queueWredDroppedPackets_().value().find(queueId)->second;
    } else {
      stats.outEcnCounter += folly::copy(portStats.outEcnCounter_().value());
      stats.wredDroppedPackets +=
          folly::copy(portStats.wredDroppedPackets_().value());
    }
    // Always populate outPackets
    stats.outPackets += utility::getPortOutPkts(portStats);
  }

  // For VoQ systems, WRED stat is collected from sysPorts and
  // outpackets is from egress.
  void extractAqmTestStats(
      const HwSysPortStats& sysPortStats,
      const HwPortStats& portStats,
      const uint8_t& queueId,
      AqmTestStats& stats) const {
    stats.wredDroppedPackets +=
        sysPortStats.queueWredDroppedPackets_().value().find(queueId)->second;
    stats.outPackets += portStats.queueOutPackets_().value().at(queueId);
  }

  template <typename StatsT>
  uint64_t extractQueueWatermarkStats(
      const StatsT& stats,
      const uint8_t& queueId) {
    return stats.get_queueWatermarkBytes_().find(queueId)->second;
  }

  /*
   * Collect stats which are needed for AQM tests. AQM specific stats
   * collected are as below:
   * WRED: Per queue available for all platforms, per port available for
   *       non-voq switches,
   * ECN : Per egress queue available for VoQ and TAJO platforms, per
   *       port available for non-voq switches.
   *
   * For non-voq switches, egress queue watermarks is a good indication
   * of peak queue usage, which can tell us if ECN marking / WRED should
   * have happened, however, for VoQ switches, queue watermarks are needed
   * from VoQs instead.
   */
  AqmTestStats getAqmTestStats(
      const uint8_t ecnVal,
      const PortID& portId,
      const uint8_t& queueId,
      const bool useQueueStatsForAqm) {
    AqmTestStats stats{};
    uint64_t queueWatermark{};
    const auto switchType =
        checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
            ->getSwitchType();
    // Always collect port stats!
    auto portStats = getLatestPortStats(portId);
    if (isEct(ecnVal) || switchType != cfg::SwitchType::VOQ) {
      // Get ECNs marked packet stats for VoQ/non-voq switches and
      // watermarks for non-voq switches.
      extractAqmTestStats(portStats, queueId, useQueueStatsForAqm, stats);
      if (switchType != cfg::SwitchType::VOQ) {
        queueWatermark = extractQueueWatermarkStats(portStats, queueId);
      }
    }
    if (switchType == cfg::SwitchType::VOQ) {
      // Gets watermarks + WRED drops in case of non-ECN traffic and
      // watermarks for ECN traffic for VoQ switches.
      auto sysPortId = getSystemPortID(
          portId,
          getProgrammedState(),
          scopeResolver().scope(portId).switchId());
      auto sysPortStats = getLatestSysPortStats(sysPortId);
      extractAqmTestStats(sysPortStats, portStats, queueId, stats);
      queueWatermark = extractQueueWatermarkStats(sysPortStats, queueId);
    }
    XLOG(DBG0) << "Queue " << static_cast<int>(queueId)
               << ", watermark: " << queueWatermark
               << ", Out packets: " << stats.outPackets
               << ", WRED drops: " << stats.wredDroppedPackets
               << ", ECN marked: " << stats.outEcnCounter;
    return stats;
  }

  // The enableWred/ enableEcn params are used to specify the test should
  // use WRED/ECN config or not. This helps validate functionality in AI
  // network which uses ECN alone, the case with both ECN and WRED as in
  // front end network or with just WRED.
  void runTest(
      const uint8_t ecnVal,
      bool enableWred,
      bool enableEcn,
      bool warmbootTest = false) {
    auto kQueueId = utility::getOlympicQueueId(utility::OlympicQueueType::ECN1);
    // For VoQ switch, AQM stats are collected from queue!
    auto useQueueStatsForAqm =
        checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
            ->getSwitchType() == cfg::SwitchType::VOQ;
    auto statsIncremented = [this](
                                const AqmTestStats& aqmStats, uint8_t ecnVal) {
      auto increment =
          isEct(ecnVal) ? aqmStats.outEcnCounter : aqmStats.wredDroppedPackets;
      return increment > 0;
    };

    auto setup = [&]() {
      applyNewConfig(configureQueue2WithAqmThreshold(enableWred, enableEcn));
      setupEcmpTraffic();
      if (isEct(ecnVal)) {
        sendPkt(kDscp(), ecnVal);
        WITH_RETRIES({
          auto aqmStats = getAqmTestStats(
              ecnVal,
              masterLogicalInterfacePortIds()[0],
              kQueueId,
              useQueueStatsForAqm);
          // Assert that ECT capable packets are not counted by port ECN
          // counter when there is no congestion!
          EXPECT_EVENTUALLY_FALSE(statsIncremented(aqmStats, ecnVal));
        });
      }
    };

    auto verify = [&]() {
      const int kNumPacketsToSend = getAgentEnsemble()->getMinPktsForLineRate(
          masterLogicalInterfacePortIds()[0]);
      sendPkts(kDscp(), ecnVal, kNumPacketsToSend);
      /*
       * Need traffic loop to build up for ECN/WRED to show up for some
       * platforms. However, we cannot expect traffic to reach line rate
       * in WRED config cases. There can also be a delay before stats are
       * synced. So, add enough retries to avoid flakiness.
       */
      WITH_RETRIES_N_TIMED(10, std::chrono::milliseconds(1000), {
        auto aqmStats = getAqmTestStats(
            ecnVal,
            masterLogicalInterfacePortIds()[0],
            kQueueId,
            useQueueStatsForAqm);
        EXPECT_EVENTUALLY_TRUE(statsIncremented(aqmStats, ecnVal));
      });
    };

    if (warmbootTest) {
      // for warmboot test, we invoke the setup and verify post warmboot
      verifyAcrossWarmBoots([] {}, [] {}, setup, verify);
    } else {
      verifyAcrossWarmBoots(setup, verify);
    }
  }

  void queueWredThresholdSetup(
      cfg::SwitchConfig& config,
      std::span<const int> queueIds) {
    auto asic = checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
    bool isVoq = asic->getSwitchType() == cfg::SwitchType::VOQ;
    for (int queueId : queueIds) {
      utility::addQueueWredConfig(
          config,
          getAgentEnsemble()->getL3Asics(),
          queueId,
          utility::kQueueConfigAqmsWredThresholdMinMax,
          utility::kQueueConfigAqmsWredThresholdMinMax,
          utility::kQueueConfigAqmsWredDropProbability,
          isVoq);
    }
  }

  void queueEcnThresholdSetup(
      cfg::SwitchConfig& config,
      std::span<const int> queueIds) {
    auto asic = checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
    bool isVoq = asic->getSwitchType() == cfg::SwitchType::VOQ;
    for (int queueId : queueIds) {
      utility::addQueueEcnConfig(
          config,
          getAgentEnsemble()->getL3Asics(),
          queueId,
          utility::kQueueConfigAqmsEcnThresholdMinMax,
          utility::kQueueConfigAqmsEcnThresholdMinMax,
          100, // probability
          isVoq);
    }
  }

  /*
   * This function validates the ECN/WRED thresholds configured on a queue.
   * If more AQM configurations are added, this function needs to reassess
   * whether thresholds are supported and the logic is applicable.
   */
  void validateAqmThresholds(
      const uint8_t ecnCodePoint,
      int thresholdBytes,
      int expectedMarkedOrDroppedPacketCount,
      AqmThresholdsConfig thresholdsConfig = AqmThresholdsConfig(),
      const std::optional<
          std::function<void(AqmTestStats&, AqmTestStats&, int)>>&
          verifyPacketCountFn = std::nullopt,
      const std::optional<std::function<
          void(cfg::SwitchConfig&, std::vector<int>, const int txPacketLen)>>&
          setupFn = std::nullopt,
      int maxQueueFillLevel = 0) {
    const int kQueueId =
        utility::getOlympicQueueId(utility::OlympicQueueType::SILVER);
    // Good to keep the payload size such that the whole packet with headers
    // can fit in a single buffer in ASIC to keep computation simple and
    // accurate.
    constexpr int kPayloadLength{30};
    const int kTxPacketLen =
        kPayloadLength + EthHdr::SIZE + IPv6Hdr::size() + TCPHeader::size();
    const std::vector<const HwAsic*> asics{getAgentEnsemble()->getL3Asics()};
    auto asic = checkSameAndGetAsicForTesting(asics);
    // The ECN/WRED threshold are rounded down for TAJO as opposed to being
    // rounded up to the next cell size for Broadcom.
    bool roundUp = asic->getAsicType() != cfg::AsicType::ASIC_TYPE_EBRO &&
        asic->getAsicType() != cfg::AsicType::ASIC_TYPE_P200;
    int roundedBufferThreshold{
        utility::getRoundedBufferThreshold(asic, thresholdBytes, roundUp)};
    int effectiveBytesPerPacket{static_cast<int>(
        utility::getEffectiveBytesPerPacket(asic, kTxPacketLen))};

    if (expectedMarkedOrDroppedPacketCount == 0 && maxQueueFillLevel > 0) {
      // The expectedMarkedOrDroppedPacketCount is not set, instead, it needs
      // to be computed based on the maxQueueFillLevel specified as param!
      expectedMarkedOrDroppedPacketCount =
          (maxQueueFillLevel - roundedBufferThreshold) /
          effectiveBytesPerPacket;
    }

    // HW programs the marking threshold in getThresholdGranularity() steps
    // (Chenab: 12288B; 1B on Broadcom), so it can start marking up to one step
    // above roundedBufferThreshold, leaving that gap's worth of packets
    // unmarked. Allow that shortfall, converted from bytes to packets via
    // effectiveBytesPerPacket. Only the queue-fill (no-drop) case needs it.
    const int markedCountBoundaryTolerance = maxQueueFillLevel > 0
        ? static_cast<int>(asic->getThresholdGranularity()) /
            effectiveBytesPerPacket
        : 0;

    // Send enough packets such that the queue gets filled up to the
    // configured ECN/WRED threshold, then send a fixed number of
    // additional packets to get marked / dropped.
    auto ceilFn = [](int a, int b) -> int { return a / b + (a % b != 0); };
    int numPacketsToSend =
        ceilFn(roundedBufferThreshold, effectiveBytesPerPacket) +
        expectedMarkedOrDroppedPacketCount;

    auto setup = [&]() {
      cfg::SwitchConfig config{getSw()->getConfig()};
      // Configure both WRED and ECN thresholds
      if (thresholdsConfig.enableEcn) {
        queueEcnThresholdSetup(config, std::array{kQueueId});
      }
      if (thresholdsConfig.enableWred) {
        queueWredThresholdSetup(config, std::array{kQueueId});
      }
      // Include any config setup needed per test case
      if (setupFn.has_value()) {
        (*setupFn)(config, {kQueueId}, kTxPacketLen);
      }
      applyNewConfig(config);

      // No traffic loop needed, so send traffic to a different MAC
      int kEcmpWidthForTest{1};
      utility::EcmpSetupAnyNPorts6 ecmpHelper6{
          getProgrammedState(),
          getSw()->needL2EntryForNeighbor(),
          utility::MacAddressGenerator().get(getIntfMac().u64HBO() + 10)};
      resolveNeighborAndProgramRoutes(ecmpHelper6, kEcmpWidthForTest);
    };

    auto verify = [&]() {
      XLOG(DBG3) << "Rounded threshold: " << roundedBufferThreshold
                 << ", effective bytes per pkt: " << effectiveBytesPerPacket
                 << ", kTxPacketLen: " << kTxPacketLen
                 << ", pkts to send: " << numPacketsToSend
                 << ", expected marked/dropped pkts: "
                 << expectedMarkedOrDroppedPacketCount;

      auto sendPackets = [&](const PortID& /* port */, int numPacketsToSend) {
        // Single port config, traffic gets forwarded out of the same!
        PortID kLoopbackPort{masterLogicalInterfacePortIds()[1]};
        HwPortStats initialStats{getLatestPortStats(kLoopbackPort)};
        sendPkts(
            utility::kOlympicQueueToDscp().at(kQueueId).front(),
            ecnCodePoint,
            numPacketsToSend,
            kPayloadLength,
            255 /*ttl*/,
            kLoopbackPort);
        WITH_RETRIES({
          HwPortStats currentStats{getLatestPortStats(kLoopbackPort)};
          EXPECT_EVENTUALLY_GE(
              currentStats.inUnicastPkts_().value(),
              initialStats.inUnicastPkts_().value() + numPacketsToSend);
        })
      };

      learnL2EntryIfPending(
          utility::MacAddressGenerator().get(getIntfMac().u64HBO() + 1),
          masterLogicalInterfacePortIds()[1]);
      // Send traffic with queue buildup and get the stats at the start.
      // Update the stats to initialize them before sending packets to build up
      // the queue.
      getAgentEnsemble()->getSw()->updateStats();
      HwPortStats beforePortStats = utility::sendPacketsWithQueueBuildup(
          sendPackets,
          getAgentEnsemble(),
          masterLogicalInterfacePortIds()[0],
          numPacketsToSend);

      AqmTestStats before{};
      extractAqmTestStats(
          beforePortStats, kQueueId, false /*useQueueStatsForAqm*/, before);

      // For ECN all packets are sent out, for WRED, account for drops!
      const uint64_t kExpectedOutPackets = isEct(ecnCodePoint)
          ? numPacketsToSend
          : numPacketsToSend - expectedMarkedOrDroppedPacketCount;
      // Because of the test setup, Ebro, Yuba, and G202X might forward
      // one packet fewer and drop one packet more. Accept that as well
      // as the exact expected forward count.
      uint64_t kMinOutPackets = kExpectedOutPackets;
      switch (asic->getAsicType()) {
        case cfg::AsicType::ASIC_TYPE_EBRO:
        case cfg::AsicType::ASIC_TYPE_YUBA:
        case cfg::AsicType::ASIC_TYPE_G202X:
          if (!isEct(ecnCodePoint) && kExpectedOutPackets > 0) {
            kMinOutPackets = kExpectedOutPackets - 1;
          }
          break;
        default:
          break;
      }

      bool useQueueStatsForAqm = asic->getSwitchType() == cfg::SwitchType::VOQ;
      WITH_RETRIES_N_TIMED(10, std::chrono::milliseconds(1000), {
        uint64_t outPackets{0}, wredDrops{0}, ecnMarking{0};
        // For VoQ switch, AQM stats are collected from queue!
        AqmTestStats aqmStats = getAqmTestStats(
            ecnCodePoint,
            masterLogicalInterfacePortIds()[0],
            kQueueId,
            useQueueStatsForAqm);

        outPackets = aqmStats.outPackets - before.outPackets;
        if (isEct(ecnCodePoint)) {
          ecnMarking = aqmStats.outEcnCounter - before.outEcnCounter;
        } else {
          wredDrops = aqmStats.wredDroppedPackets - before.wredDroppedPackets;
        }

        // Check for outpackets as expected and ensure that ECN or WRED
        // counters are incrementing too!
        // - In case of WRED, make sure that dropped packets + out packets
        //   >= expected drop + out packets.
        // - In case of ECN, ensure that ECN marked packet count is >= the
        //   expected marked packet count, this will ensure test case
        //   waiting long enough to for all marked packets to be seen.
        EXPECT_EVENTUALLY_GE(outPackets, kMinOutPackets);
        if (isEct(ecnCodePoint)) {
          EXPECT_EVENTUALLY_GE(
              ecnMarking + markedCountBoundaryTolerance,
              expectedMarkedOrDroppedPacketCount);
        } else {
          EXPECT_EVENTUALLY_GE(
              wredDrops + outPackets,
              kExpectedOutPackets + expectedMarkedOrDroppedPacketCount);
        }
      });
      AqmTestStats after = getAqmTestStats(
          ecnCodePoint,
          masterLogicalInterfacePortIds()[0],
          kQueueId,
          useQueueStatsForAqm);
      int64_t deltaOutPackets =
          static_cast<int64_t>(after.outPackets) - before.outPackets;

      // Might see more outPackets than expected due to
      // utility::sendPacketsWithQueueBuildup()
      EXPECT_GE(deltaOutPackets, kMinOutPackets);
      XLOG(DBG0) << "Delta out pkts: " << deltaOutPackets;

      if (verifyPacketCountFn.has_value()) {
        (*verifyPacketCountFn)(
            after, before, expectedMarkedOrDroppedPacketCount);
      }
    };

    verifyAcrossWarmBoots(setup, verify);
  }

  void runPerQueueEcnMarkedStatsTest() {
    const PortID portId = masterLogicalInterfacePortIds()[0];
    const int silverQueueId =
        utility::getOlympicQueueId(utility::OlympicQueueType::SILVER);

    auto setup = [=, this]() {
      auto config{getSw()->getConfig()};
      queueEcnThresholdSetup(config, std::array{silverQueueId});
      applyNewConfig(config);

      // Setup traffic loop
      setupEcmpTraffic();
    };

    auto verify = [=, this]() {
      // Get stats to verify if additional packets are getting ECN marked
      HwPortStats beforePortStats =
          getAgentEnsemble()->getLatestPortStats(portId);
      AqmTestStats beforeAqmQueueStats{};
      extractAqmTestStats(
          beforePortStats,
          silverQueueId,
          true /*useQueueStatsForAqm*/,
          beforeAqmQueueStats);

      // Send traffic
      const int kNumPacketsToSend =
          getAgentEnsemble()->getMinPktsForLineRate(portId);
      sendPkts(
          utility::kOlympicQueueToDscp().at(silverQueueId).front(),
          kECT1,
          kNumPacketsToSend);

      getAgentEnsemble()->waitForLineRateOnPort(portId);

      WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(200), {
        HwPortStats afterPortStats =
            getAgentEnsemble()->getLatestPortStats(portId);
        AqmTestStats afterAqmQueueStats{};
        extractAqmTestStats(
            afterPortStats,
            silverQueueId,
            true /*useQueueStatsForAqm*/,
            afterAqmQueueStats);

        uint64_t deltaQueueEcnMarkedPackets = afterAqmQueueStats.outEcnCounter -
            beforeAqmQueueStats.outEcnCounter;

        // Details for debugging
        uint64_t deltaOutPackets =
            afterAqmQueueStats.outPackets - beforeAqmQueueStats.outPackets;
        uint64_t deltaPortEcnMarkedPackets = *afterPortStats.outEcnCounter_() -
            *beforePortStats.outEcnCounter_();
        XLOG(DBG3) << "queue(" << silverQueueId << "): delta/total"
                   << " EcnMarked: " << deltaQueueEcnMarkedPackets << "/"
                   << afterAqmQueueStats.outEcnCounter
                   << " outPackets: " << deltaOutPackets << "/"
                   << afterAqmQueueStats.outPackets
                   << " Port.EcnMarked: " << deltaPortEcnMarkedPackets << "/"
                   << *afterPortStats.outEcnCounter_();

        EXPECT_EVENTUALLY_GT(
            afterAqmQueueStats.outEcnCounter, beforeAqmQueueStats.outEcnCounter)
            << "Queue(" << silverQueueId << ") ECN marked packets not seen!";

        // ECN marked packets seen for the queue
        XLOG(DBG0) << "queue(" << silverQueueId
                   << "): " << deltaQueueEcnMarkedPackets
                   << " ECN marked packets seen!";
        // Make sure that port ECN counters are working
        EXPECT_EVENTUALLY_GT(
            afterPortStats.outEcnCounter_().value(),
            beforePortStats.outEcnCounter_().value());
      });
    };
    verifyAcrossWarmBoots(setup, verify);
  }

  void runWredThresholdTest() {
    validateAqmThresholds(
        kNotECT,
        utility::kQueueConfigAqmsWredThresholdMinMax /*thresholdBytes*/,
        50 /*expectedMarkedOrDroppedPacketCount*/,
        AqmThresholdsConfig{.enableWred = true},
        std::move(verifyWredDroppedPacketCount));
  }

  void runEcnThresholdTest() {
    constexpr auto kMarkedPackets{50};
    constexpr auto kThresholdBytes{utility::kQueueConfigAqmsEcnThresholdMinMax};
    /*
     * Broadcom platforms does ECN marking at the egress and not in
     * MMU. ECN mark/no-mark decision is refreshed periodically, like
     * for TH3/TH4 once in 0.5usec, TH in 1usec etc. This means, once
     * ECN threshold is exceeded, packets will continue to be marked
     * until the next refresh, which will result in more ECN marking
     * during this test. Hence, apply a shaper on the queue to ensure
     * packets are sent out one per 2 usec (500K pps), which is enough
     * spacing of packets egressing to have ECN accounting done
     * with minimal error. However, in prod devices, we should expect
     * to see this +/- error with ECN marking.
     */
    auto shaperSetup = [&](cfg::SwitchConfig& config,
                           const std::vector<int>& queueIds,
                           const int txPacketLen) {
      auto maxQueueShaperKbps = ceil(500000 * txPacketLen / 1000);
      queueShaperAndBurstSetup(
          queueIds,
          config,
          0,
          maxQueueShaperKbps,
          utility::kQueueConfigBurstSizeMinKb,
          utility::kQueueConfigBurstSizeMaxKb);
    };
    validateAqmThresholds(
        kECT0,
        kThresholdBytes,
        kMarkedPackets,
        AqmThresholdsConfig{.enableEcn = true},
        verifyEcnMarkedPacketCount,
        std::move(shaperSetup));
  }

  void runPerQueueWredDropStatsTest() {
    const std::array<int, 3> wredQueueIds = {
        utility::getOlympicQueueId(utility::OlympicQueueType::SILVER),
        utility::getOlympicQueueId(utility::OlympicQueueType::GOLD),
        utility::getOlympicQueueId(utility::OlympicQueueType::ECN1)};

    auto setup = [&]() {
      const AgentEnsemble& ensemble = *getAgentEnsemble();
      const std::vector<const HwAsic*> asics = ensemble.getL3Asics();
      cfg::SwitchConfig config = initialConfig(ensemble);
      if (ensemble.getHwAsicTable()->isFeatureSupportedOnAllAsic(
              HwAsic::Feature::L3_QOS)) {
        utility::addOlympicQueueConfig(&config, asics);
      }
      bool isVoq = checkSameAndGetAsicForTesting(asics)->getSwitchType() ==
          cfg::SwitchType::VOQ;
      for (int queueId : wredQueueIds) {
        utility::addQueueWredConfig(
            config,
            asics,
            queueId,
            utility::kQueueConfigAqmsWredThresholdMinMax,
            utility::kQueueConfigAqmsWredThresholdMinMax,
            utility::kQueueConfigAqmsWredDropProbability,
            isVoq);
      }
      applyNewConfig(config);
      setupEcmpTraffic();
    };

    auto verify = [&]() {
      // Using delta stats in this function, so get the stats before starting
      const HwPortStats beforeStats =
          getLatestPortStats(masterLogicalInterfacePortIds()[0]);
      const std::map<int16_t, int64_t>& beforeDroppedPacketsMap =
          *beforeStats.queueWredDroppedPackets_();

      // Send traffic to all queues
      constexpr int kNumPacketsToSend{1000};
      for (int queueId : wredQueueIds) {
        sendPkts(
            utility::kOlympicQueueToDscp().at(queueId).front(),
            false,
            kNumPacketsToSend);
      }

      WITH_RETRIES({
        // Current map of queue to WRED dropped packets
        const std::map<std::int16_t, std::int64_t> droppedPacketsMap =
            *getLatestPortStats(masterLogicalPortIds())
                 .find(masterLogicalInterfacePortIds()[0])
                 ->second.queueWredDroppedPackets_();
        for (int queueId : wredQueueIds) {
          int64_t wredDrops = droppedPacketsMap.find(queueId)->second -
              beforeDroppedPacketsMap.find(queueId)->second;
          XLOG(DBG3) << "Queue : " << queueId << ", wredDrops : " << wredDrops;
          EXPECT_EVENTUALLY_GT(wredDrops, 0);
        }
      });
    };

    verifyAcrossWarmBoots(setup, verify);
  }

  void runEcnTrafficNoDropTest() {
    constexpr int kThresholdBytes{utility::kQueueConfigAqmsEcnThresholdMinMax};
    std::optional<cfg::MMUScalingFactor> scalingFactor{std::nullopt};
    checkSameAsicType(getAgentEnsemble()->getL3Asics());
    SwitchID switchId =
        scopeResolver().scope(masterLogicalInterfacePortIds()[0]).switchId();
    const HwAsic* asic = hwAsicForSwitch(switchId);
    if (asic->scalingFactorBasedDynamicThresholdSupported()) {
      scalingFactor = cfg::MMUScalingFactor::ONE_16TH;
    }

    auto setupScalingFactor = [&](cfg::SwitchConfig& config,
                                  const std::vector<int>& /* queueIds */,
                                  const int /* txPktLen */) {
      if (scalingFactor.has_value()) {
        std::vector<cfg::PortQueue>& queues =
            config.portQueueConfigs()["queue_config"];
        for (auto& queue : queues) {
          queue.scalingFactor() = scalingFactor.value();
        }
      }
    };

    uint32_t queueFillMaxBytes = utility::getQueueLimitBytes(
        asic,
        getAgentEnsemble()->getHwAgentTestClient(switchId),
        scalingFactor);
    if (scalingFactor.has_value()) {
      /*
       * For platforms with dynamic alpha based buffer limits, account for
       * possible usage outside of the test and need to relax the limits being
       * checked for no drops. Hence checking for queue build up to 99.9% of
       * the possible depth.
       */
      queueFillMaxBytes = queueFillMaxBytes * 0.999;
    }
    validateAqmThresholds(
        kECT0,
        kThresholdBytes,
        0,
        AqmThresholdsConfig{.enableEcn = true},
        std::nullopt /* verifyPacketCountFn */,
        setupScalingFactor,
        queueFillMaxBytes);
  }
};

class AgentAqmWredDropTest : public AgentAqmTest {
 public:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    cfg::SwitchConfig config = AgentAqmTest::initialConfig(ensemble);
    if (ensemble.getHwAsicTable()->isFeatureSupportedOnAllAsic(
            HwAsic::Feature::L3_QOS)) {
      cfg::StreamType streamType =
          *checkSameAndGetAsicForTesting(ensemble.getL3Asics())
               ->getQueueStreamTypes(cfg::PortType::INTERFACE_PORT)
               .begin();
      utility::addQueueWredDropConfig(
          &config, streamType, ensemble.getL3Asics());
      // For VoQ switches, add AQM config to VoQ as well.
      auto asic = checkSameAndGetAsicForTesting(ensemble.getL3Asics());
      if (asic->getSwitchType() == cfg::SwitchType::VOQ) {
        utility::addVoqAqmConfig(
            &config,
            streamType,
            asic,
            true /*addWredConfig*/,
            false /*addEcnConfig*/);
      }
    }
    return config;
  }

  void runWredDropTest() {
    auto setup = [=, this]() { setupEcmpTraffic(); };
    auto verify = [=, this]() {
      // Send packets to queue0 and queue2 (both configured to the same
      // weight). Queue0 is configured with 0% drop probability and queue2 is
      // configured with 5% drop probability.
      sendPkts(kDscp(), false);
      sendPkts(kSilverDscp(), false);

      auto getQueueWatermarkBytes = [&](const auto& portStats, int queueId) {
        auto queueWatermark =
            portStats.get_queueWatermarkBytes_().find(queueId)->second;
        XLOG(DBG0) << "Queue " << queueId << " watermark : " << queueWatermark;
        return queueWatermark;
      };

      constexpr auto queue2Id = 2;
      constexpr auto queue0Id = 0;
      uint64_t queue0Watermark{0};
      uint64_t queue2Watermark{0};
      WITH_RETRIES({
        auto portStats = getLatestPortStats(masterLogicalInterfacePortIds()[0]);
        queue0Watermark = getQueueWatermarkBytes(portStats, queue0Id);
        queue2Watermark = getQueueWatermarkBytes(portStats, queue2Id);
        EXPECT_EVENTUALLY_GT(queue0Watermark, 0);
        EXPECT_EVENTUALLY_GT(queue2Watermark, 0);
      });

      // Queue0 watermark should be higher than queue2 since it drops less
      // packets.
      XLOG(DBG0) << "Expecting queue 0 watermark " << queue0Watermark
                 << " larger than queue 2 watermark : " << queue2Watermark;
      EXPECT_TRUE(queue0Watermark > queue2Watermark);
    };

    verifyAcrossWarmBoots(setup, verify);
  }
};

class AgentAqmEcnOnlyTest : public AgentAqmTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::ECN};
  }

  std::optional<size_t> maxRequiredInterfacePorts() const override {
    // AQM tests need full port set for health check
    return std::nullopt;
  }
};

class AgentAqmEcnProbabilisticMarkingTest : public AgentAqmTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::ECN, ProductionFeature::ECN_PROBABILISTIC_MARKING};
  }

 protected:
  static constexpr int kMinThresh = 10 * 1024;
  static constexpr int kMaxThresh = 30 * 1024 * 1024;
  static constexpr int kProbability = 50;

  cfg::SwitchConfig configureQueue2WithAqmThreshold(
      bool enableWred,
      bool enableEcn) const override {
    auto config = utility::onePortPerInterfaceConfig(
        getSw(), masterLogicalPortIds(), true /*interfaceHasSubnet*/);
    if (getAgentEnsemble()->getHwAsicTable()->isFeatureSupportedOnAllAsic(
            HwAsic::Feature::L3_QOS)) {
      auto hwAsic =
          checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
      auto streamType =
          *hwAsic->getQueueStreamTypes(cfg::PortType::INTERFACE_PORT).begin();
      bool isVoq = hwAsic->getSwitchType() == cfg::SwitchType::VOQ;

      if (isDualStage3Q2QQos()) {
        utility::addNetworkAIQueueConfig(
            &config,
            streamType,
            cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN,
            hwAsic,
            enableWred,
            false /* enableEcn - override below */);
      } else {
        utility::addOlympicQueueConfig(
            &config,
            getAgentEnsemble()->getL3Asics(),
            enableWred,
            false /* enableEcn - we'll add our own */);
        if (isVoq) {
          utility::addVoqAqmConfig(
              &config,
              streamType,
              hwAsic,
              enableWred,
              false /* enableEcn, override below */);
        }
      }

      if (enableEcn) {
        auto kQueueId =
            utility::getOlympicQueueId(utility::OlympicQueueType::ECN1);
        if (isVoq) {
          utility::addVoqEcnProbabilisticMarkingConfig(
              &config,
              streamType,
              hwAsic,
              kQueueId,
              kProbability,
              kMinThresh,
              kMaxThresh);
        } else {
          utility::addQueueEcnProbabilisticMarkingConfig(
              &config,
              streamType,
              getAgentEnsemble()->getL3Asics(),
              kQueueId,
              kProbability,
              kMinThresh,
              kMaxThresh);
        }
      }
      utility::addOlympicQosMaps(config, getAgentEnsemble()->getL3Asics());
    }
    utility::setTTLZeroCpuConfig(getAgentEnsemble()->getL3Asics(), config);
    return config;
  }
};

TEST_F(AgentAqmTest, verifyEct0) {
  runTest(kECT0, true /* enableWred */, true /* enableEcn */);
}

TEST_F(AgentAqmTest, verifyEct1) {
  runTest(kECT1, true /* enableWred */, true /* enableEcn */);
}

TEST_F(AgentAqmEcnOnlyTest, verifyEcnEct0WithoutWredConfig) {
  runTest(kECT0, false /* enableWred */, true /* enableEcn */);
}

TEST_F(AgentAqmEcnOnlyTest, verifyEcnEct1WithoutWredConfig) {
  runTest(kECT1, false /* enableWred */, true /* enableEcn */);
}

TEST_F(AgentAqmEcnOnlyTest, verifyEcnWithoutWredPostWarmboot) {
  runTest(
      kECT1,
      false /* enableWred */,
      true /* enableEcn */,
      true /* warmbootTest */);
}

TEST_F(AgentAqmTest, verifyWredWithoutEcnConfig) {
  runTest(kNotECT, true /* enableWred */, false /* enableEcn */);
}

TEST_F(AgentAqmTest, verifyWred) {
  runTest(kNotECT, true /* enableWred */, true /* enableEcn */);
}

TEST_F(AgentAqmWredDropTest, verifyWredDrop) {
  runWredDropTest();
}

TEST_F(AgentAqmTest, verifyWredThreshold) {
  runWredThresholdTest();
}

TEST_F(AgentAqmTest, verifyPerQueueWredDropStats) {
  runPerQueueWredDropStatsTest();
}

TEST_F(AgentAqmEcnOnlyTest, verifyEcnTrafficNoDrop) {
  runEcnTrafficNoDropTest();
}

TEST_F(AgentAqmEcnOnlyTest, verifyEcnThreshold) {
  runEcnThresholdTest();
}

TEST_F(AgentAqmEcnOnlyTest, verifyPerQueueEcnMarkedStats) {
  runPerQueueEcnMarkedStatsTest();
}

TEST_F(AgentAqmEcnProbabilisticMarkingTest, verifyEcnProbabilisticMarking) {
  runTest(kECT0, false /* enableWred */, true /* enableEcn */);
}

} // namespace facebook::fboss
