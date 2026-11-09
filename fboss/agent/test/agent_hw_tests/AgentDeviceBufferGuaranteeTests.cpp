// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/utils/MultiPortTrafficTestUtils.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"
#include "fboss/agent/test/utils/PortTestUtils.h"
#include "fboss/agent/test/utils/QosTestUtils.h"
#include "fboss/lib/CommonUtils.h"

#include <folly/String.h>

/*
 * S416694: heavy congestion on a few ports consumed the entire device buffer,
 * so traffic to ports that were not themselves congested was dropped due to
 * buffer unavailability. The fix caps overall device buffer utilization and
 * reserves a minimum per queue.
 *
 * Scenario under test: congest a few output queues hard enough to put the
 * device under sustained buffer pressure, and check that buffer usage does not
 * cross a specified threshold at the heaviest load, which guarantees that
 * enough buffer is left for traffic on queues that are not congested.
 *
 * Buffer model, thresholds and the values below:
 * https://docs.google.com/document/d/1e6STyTlR5M_M9GVYJh08H5We612hPObEU8ijJF6QBEI/edit?usp=sharing
 */

namespace facebook::fboss {

namespace {

constexpr uint64_t kMaxUsableBufferSizeBytes = 64 * 1024 * 1024;

constexpr uint64_t kMaxHealthyDeviceWatermarkBytes = 55 * 1024 * 1024;

constexpr uint64_t kCongestionAchievedThresholdBytes = 50 * 1024 * 1024;

constexpr int kNumCongestedPorts = 2;

constexpr int kNumLoopPorts = 1;

} // namespace

class AgentDeviceBufferGuaranteeTest : public AgentHwTest {
 protected:
  // Ports whose queues the test fills, and the ports that fill them.
  struct CongestionTargets {
    std::vector<PortID> congested;
    std::vector<folly::IPAddressV6> congestedIps;
    std::vector<PortID> injection;
  };

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = AgentHwTest::initialConfig(ensemble);
    utility::addOlympicQueueConfig(&config, ensemble.getL3Asics());
    utility::addOlympicQosMaps(config, ensemble.getL3Asics());
    // Only ECN-capable traffic is allowed to grow a queue far enough to drive
    // device utilization to the cap; non-ECN traffic is clamped well below it.
    utility::addQueueEcnConfig(
        config,
        ensemble.getL3Asics(),
        utility::kOlympicSilverQueueId,
        utility::kQueueConfigAqmsEcnThresholdMinMax,
        utility::kQueueConfigAqmsEcnThresholdMinMax,
        100 /* probability */,
        ensemble.getSw()->getSwitchInfoTable().l3SwitchType() ==
            cfg::SwitchType::VOQ);
    return config;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::L3_QOS,
        ProductionFeature::OLYMPIC_QOS,
        ProductionFeature::ECN,
        ProductionFeature::BUFFER_MIN_GUARANTEE_WITH_DELAY_DROPS};
  }

  // Opt out of the 8 port default: eight senders cannot drive utilization far
  // enough to reach the cap.
  std::optional<size_t> maxRequiredInterfacePorts() const override {
    return std::nullopt;
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    // Keep control traffic from interfering with the test.
    FLAGS_disable_neighbor_solicitation = true;
  }

  CongestionTargets congestionTargets() const {
    const auto ports = getAgentEnsemble()->masterLogicalInterfacePortIds();
    const auto ips =
        utility::getOneRemoteHostIpPerInterfacePort(getAgentEnsemble());
    CHECK_GT(ports.size(), kNumCongestedPorts + kNumLoopPorts)
        << "need more interface ports to congest some and keep one draining";
    CongestionTargets targets;
    // Route and adjacency are programmed for the loop port first, then for
    // the ports where congestion is created. Rest of the ports get neither.
    for (int i = 0; i < kNumCongestedPorts; i++) {
      targets.congested.push_back(ports[i]);
      targets.congestedIps.push_back(ips[kNumLoopPorts + i]);
    }
    targets.injection = {
        ports.begin() + kNumCongestedPorts + kNumLoopPorts, ports.end()};
    return targets;
  }

  // Program adjacency and route per congestion port, we expect traffic to
  // build up on these queues.
  void resolveCongestedPorts(const CongestionTargets& targets) {
    auto ensemble = getAgentEnsemble();
    utility::EcmpSetupTargetedPorts6 ecmpHelper(
        ensemble->getProgrammedState(),
        ensemble->getSw()->needL2EntryForNeighbor(),
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState()));
    std::vector<PortDescriptor> descriptors;
    descriptors.reserve(targets.congested.size());
    for (const auto& port : targets.congested) {
      descriptors.emplace_back(port);
    }
    const boost::container::flat_set<PortDescriptor> nhops(
        descriptors.begin(), descriptors.end());
    ensemble->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return ecmpHelper.resolveNextHops(in, nhops);
    });
    auto updater = ensemble->getSw()->getRouteUpdater();
    for (int i = 0; i < kNumCongestedPorts; i++) {
      ecmpHelper.programRoutes(
          &updater,
          boost::container::flat_set<PortDescriptor>{
              PortDescriptor(targets.congested[i])},
          {RoutePrefixV6(targets.congestedIps[i], 128)});
    }
  }

  uint64_t currentDeviceWatermarkBytes() {
    const auto switchIndex =
        getSw()->getSwitchInfoTable().getSwitchIndexFromSwitchId(
            getCurrentSwitchIdForTesting());
    auto watermarkStats = getAllSwitchWatermarkStats();
    const auto watermarkIt = watermarkStats.find(switchIndex);
    if (watermarkIt == watermarkStats.end()) {
      // Reads as no congestion, so say why rather than let the caller retry
      // against a silent zero.
      XLOG(WARNING) << "no device watermark for switch index " << switchIndex;
      return 0;
    }
    return static_cast<uint64_t>(*watermarkIt->second.deviceWatermarkBytes());
  }

  // Cycle every sender over the congested queues until the device is under
  // buffer pressure and has stopped building: four consecutive rounds whose
  // watermark drifts no more than a few percent means it has settled at
  // whatever level the cap allows. One round per stats interval, since the
  // watermark only advances as the background thread publishes it. Returns the
  // settled watermark.
  uint64_t injectCongestingTraffic(const CongestionTargets& targets) {
    const auto macs = txMacs();
    constexpr int kMaxFillRounds = 30;
    constexpr int kPacketsPerRound = 200;
    constexpr int kStableRoundsRequired = 4;
    constexpr double kMaxDriftRatio = 0.05;
    uint64_t previous = 0;
    uint64_t watermark = 0;
    int stableRounds = 0;
    WITH_RETRIES_N_TIMED(kMaxFillRounds, std::chrono::seconds(1), {
      for (const auto& port : targets.injection) {
        for (const auto& dstIp : targets.congestedIps) {
          for (int i = 0; i < kPacketsPerRound; i++) {
            sendUdpPkt(macs, dstIp, port);
          }
        }
      }
      watermark = currentDeviceWatermarkBytes();
      EXPECT_EVENTUALLY_GE(watermark, kCongestionAchievedThresholdBytes);

      // Holding steady only counts once both ends of the round are past the
      // threshold, so the rounds spent climbing to it are never counted.
      const auto drift =
          watermark > previous ? watermark - previous : previous - watermark;
      stableRounds = (previous >= kCongestionAchievedThresholdBytes &&
                      watermark >= kCongestionAchievedThresholdBytes &&
                      drift <= previous * kMaxDriftRatio)
          ? stableRounds + 1
          : 0;
      XLOG(DBG0) << "DeviceBuffer fill: deviceWatermark=" << watermark
                 << " bytes, stableRounds=" << stableRounds;
      previous = watermark;
      EXPECT_EVENTUALLY_EQ(stableRounds, kStableRoundsRequired);
    });
    return watermark;
  }

  // Resolved once and passed in: the injection loop sends hundreds of
  // thousands of packets and walking the switch state per packet is waste.
  struct TxMacs {
    folly::MacAddress src;
    folly::MacAddress intf;
  };

  TxMacs txMacs() const {
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    return TxMacs{
        utility::MacAddressGenerator().get(intfMac.u64HBO() + 1), intfMac};
  }

  void sendUdpPkt(
      const TxMacs& macs,
      const folly::IPAddressV6& dstIp,
      std::optional<PortID> outPort = std::nullopt) {
    // Payload sized so the frame with L2, IPv6 and UDP headers is ~4600B.
    constexpr int kPayloadBytes = 4538;
    constexpr uint8_t kEct1 = 0x01;
    // kOlympicQueueToDscp() rebuilds its whole map on every call, so resolve
    // the one value we need once.
    static const uint8_t kDscp = utility::kOlympicQueueToDscp()
                                     .at(utility::kOlympicSilverQueueId)
                                     .front();
    static const folly::IPAddressV6 kSrcIp("2620:0:1cfe:face:b00c::1");
    auto txPacket = utility::makeUDPTxPacket(
        getSw(),
        getVlanIDForTx(),
        macs.src,
        macs.intf,
        kSrcIp,
        dstIp,
        8000,
        8001,
        static_cast<uint8_t>(kDscp << 2 | kEct1),
        255 /* ttl */,
        std::vector<uint8_t>(kPayloadBytes, 0xff));
    if (outPort.has_value()) {
      getSw()->sendPacketOutOfPortAsync(std::move(txPacket), *outPort);
    } else {
      getSw()->sendPacketSwitchedAsync(std::move(txPacket));
    }
  }
};

TEST_F(
    AgentDeviceBufferGuaranteeTest,
    VerifyDeviceBufferCappedUnderCongestion) {
  auto loopPort =
      getAgentEnsemble()->masterLogicalInterfacePortIds()[kNumCongestedPorts];

  auto setup = [this, loopPort]() {
    resolveCongestedPorts(congestionTargets());
    utility::setupEcmpDataplaneLoopOnPorts(getAgentEnsemble(), {loopPort});
  };

  auto verify = [this, loopPort]() {
    const auto targets = congestionTargets();
    auto portName = [this](PortID port) {
      return getProgrammedState()->getPorts()->getNodeIf(port)->getName();
    };
    std::vector<std::string> congestedNames;
    congestedNames.reserve(targets.congested.size());
    for (const auto& port : targets.congested) {
      congestedNames.push_back(portName(port));
    }

    // Bring the loop port to line rate
    const auto macs = txMacs();
    auto seedPackets = getAgentEnsemble()->getMinPktsForLineRate(loopPort);
    auto loopIp =
        utility::getOneRemoteHostIpPerInterfacePort(getAgentEnsemble())[0];
    for (size_t pkt = 0; pkt < seedPackets; pkt++) {
      sendUdpPkt(macs, loopIp, loopPort);
    }
    utility::waitForLineRateOnPorts(getAgentEnsemble(), {loopPort});

    // Disable transmit on target ports, so queue builds up.
    for (const auto& port : targets.congested) {
      utility::setCreditWatchdogAndPortTx(getAgentEnsemble(), port, false);
    }
    // Fill the device buffer from every sender at once.
    const auto deviceWatermark = injectCongestingTraffic(targets);
    XLOG(DBG0) << "DeviceBuffer congestion: congested=["
               << folly::join(",", congestedNames)
               << "] (tx disabled), senders=" << targets.injection.size()
               << ", deviceWatermark=" << deviceWatermark
               << " bytes, max usable size=" << kMaxUsableBufferSizeBytes
               << " bytes";

    // Utilization has reached the max and stays there.
    EXPECT_GT(deviceWatermark, kCongestionAchievedThresholdBytes);
    EXPECT_LT(deviceWatermark, kMaxHealthyDeviceWatermarkBytes);

    // Verify that line rate traffic on an uncongested port is not dropped.
    constexpr int kCleanReadsRequired = 4;
    const auto discardsBefore = *getLatestPortStats(loopPort).outDiscards_();
    int cleanReads = 0;
    uint64_t discards{0};
    WITH_RETRIES({
      discards = *getLatestPortStats(loopPort).outDiscards_();
      EXPECT_EQ(discards, discardsBefore);
      EXPECT_EVENTUALLY_EQ(cleanReads++, kCleanReadsRequired);
    });
    XLOG(DBG0) << "DeviceBuffer loop port " << portName(loopPort)
               << " discardsBefore=" << discardsBefore
               << ", discardsAfter=" << discards
               << ", delta=" << discards - discardsBefore;

    // Uncongest the device
    for (const auto& port : targets.congested) {
      utility::setCreditWatchdogAndPortTx(getAgentEnsemble(), port, true);
    }
  };

  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
