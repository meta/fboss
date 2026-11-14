// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/TxPacket.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/packet/EthHdr.h"
#include "fboss/agent/packet/IPv6Hdr.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/packet/UDPHeader.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AgentTestAddressConstants.h"
#include "fboss/agent/test/utils/AqmTestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"
#include "fboss/agent/test/utils/QosTestUtils.h"
#include "fboss/lib/CommonUtils.h"

#include <fb303/ServiceData.h>
#include <fmt/format.h>

DECLARE_bool(disable_neighbor_updates);

namespace facebook::fboss {

class AgentWatermarkTest : public AgentHwTest {
 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = AgentHwTest::initialConfig(ensemble);
    utility::addOlympicQueueConfig(&config, ensemble.getL3Asics());
    utility::addOlympicQosMaps(config, ensemble.getL3Asics());
    utility::setTTLZeroCpuConfig(ensemble.getL3Asics(), config);
    return config;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::L3_QOS, ProductionFeature::OLYMPIC_QOS};
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_disable_neighbor_updates = false;
    // Disabling because neighbor solicitation packets will cause device
    // watermarks to be non-zero
    FLAGS_disable_neighbor_solicitation = true;
  }

  // Only the switch under test has ports configured under the multi_switch
  // harness; scope watermark checks to it (no-op for single-switch agents).
  std::set<SwitchID> switchIdsUnderTest() const {
    return {getCurrentSwitchIdForTesting()};
  }

  folly::IPAddressV6 kDestIp2() const {
    return folly::IPAddressV6("2620:0:1cfe:face:b00c::5");
  }

  std::map<PortID, folly::IPAddressV6> getPort2DstIp() const {
    return {
        {getAgentEnsemble()->masterLogicalInterfacePortIds()[0],
         folly::IPAddressV6(kTestDstIpV6)},
        {getAgentEnsemble()->masterLogicalInterfacePortIds()[1], kDestIp2()},
    };
  }

  void stopTraffic(const PortID& portId, bool needTrafficLoop = false) {
    // Toggle the link to break traffic loop
    getAgentEnsemble()->getLinkToggler()->bringDownPorts({portId});
    getAgentEnsemble()->getLinkToggler()->bringUpPorts({portId});
    resolveNdpNeighbors(PortDescriptor(portId), needTrafficLoop);
  }

  void sendUdpPkt(
      uint8_t dscpVal,
      const folly::IPAddressV6& dst,
      int payloadSize,
      std::optional<PortID> port) {
    auto vlanId = getVlanIDForTx();
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);

    auto kECT1 = 0x01; // ECN capable transport ECT(1)
    auto txPacket = utility::makeUDPTxPacket(
        getSw(),
        vlanId,
        srcMac,
        intfMac,
        folly::IPAddressV6(kTestSrcIpV6),
        dst,
        kTestSrcPort,
        kTestDstPort,
        // Trailing 2 bits are for ECN, we do not want drops in
        // these queues due to any configured thresholds!
        static_cast<uint8_t>(dscpVal << 2 | kECT1),
        255,
        std::vector<uint8_t>(payloadSize, 0xff));
    if (port.has_value()) {
      getSw()->sendPacketOutOfPortAsync(std::move(txPacket), *port);
    } else {
      sendPacketSwitchedAsync(std::move(txPacket));
    }
  }

  /*
   * In practice, sending single packet usually (but not always) returned BST
   * value > 0 (usually 2, but  other times it was 0). Thus, send a large
   * number of packets to try to avoid the risk of flakiness.
   */
  void sendUdpPkts(
      uint8_t dscpVal,
      const folly::IPAddressV6& dstIp,
      int cnt = 100,
      int payloadSize = 6000,
      std::optional<PortID> port = std::nullopt) {
    for (int i = 0; i < cnt; i++) {
      sendUdpPkt(dscpVal, dstIp, payloadSize, port);
    }
  }

  void checkFb303BufferWatermarkUcast(
      const PortID& portId,
      const int queueId,
      bool isVoq) {
    std::string portName;
    if (!isVoq) {
      portName = getProgrammedState()->getPorts()->getNodeIf(portId)->getName();

    } else {
      auto systemPortId = getSystemPortID(
          portId,
          getProgrammedState(),
          scopeResolver().scope(portId).switchId());
      portName = getProgrammedState()
                     ->getSystemPorts()
                     ->getNodeIf(systemPortId)
                     ->getName();
    }

    if (!FLAGS_multi_switch) {
      auto counters = fb303::fbData->getRegexCounters({fmt::format(
          "buffer_watermark_ucast.{}.*.queue{}.*.p100.60", portName, queueId)});
      // Unfortunately since  we use quantile stats, which compute
      // a MAX over a period, we can't really assert on the exact
      // value, just on its presence
      WITH_RETRIES({ EXPECT_EVENTUALLY_EQ(1, counters.size()); });
    } else {
      // TODO: Check the fb303 counter on HwAgent
    }
  }

  const std::map<int16_t, int64_t> getQueueWatermarks(
      const PortID& portId,
      bool isVoq) {
    if (isVoq) {
      auto sysPortId = getSystemPortID(
          portId, getProgrammedState(), switchIdForPort(portId));
      return *getLatestSysPortStats(sysPortId).queueWatermarkBytes_();
    } else {
      return *getLatestPortStats(portId).queueWatermarkBytes_();
    }
  }

  uint64_t getMinDeviceWatermarkValue(SwitchID switchId) {
    uint64_t minDeviceWatermarkBytes{0};
    const auto asicType =
        getSw()->getHwAsicTable()->getHwAsic(switchId)->getAsicType();
    if (asicType == cfg::AsicType::ASIC_TYPE_EBRO ||
        asicType == cfg::AsicType::ASIC_TYPE_P200) {
      /*
       * Ebro will always have some internal buffer utilization even
       * when there is no traffic in the ASIC. The recommendation is
       * to consider atleast 100 buffers, translating to 100 x 384B
       * as steady state device watermark.
       */
      constexpr auto kEbroMinDeviceWatermarkBytes = 38400;
      minDeviceWatermarkBytes = kEbroMinDeviceWatermarkBytes;
    } else if (
        asicType == cfg::AsicType::ASIC_TYPE_G202X ||
        asicType == cfg::AsicType::ASIC_TYPE_YUBA) {
      /*
       * GR2 / Yuba / SAI: ingress buffer pool high-watermark may not read back
       * as exactly zero after traffic stops; allow residual up to 173 KiB for
       * the "idle" phase of the test.
       */
      constexpr auto kResidualDeviceWatermarkBytes = 173 * 1024;
      minDeviceWatermarkBytes = kResidualDeviceWatermarkBytes;
    }
    return minDeviceWatermarkBytes;
  }

  bool gotExpectedDeviceWatermark(bool expectZero, SwitchID switchId) {
    static constexpr char kFb303DeviceWatermarkCtr[] =
        "buffer_watermark_device.p100.60";
    XLOG(DBG0) << "Expect zero watermark: " << std::boolalpha << expectZero
               << " switchId=" << switchId;
    bool watermarkAsExpected = false;
    uint64_t lastDeviceWatermarkBytes{0};
    bool sawSwitchWatermarkStats{false};
    bool fb303DeviceCtrPresent{false};
    const auto switchIndex =
        getSw()->getSwitchInfoTable().getSwitchIndexFromSwitchId(switchId);
    WITH_RETRIES({
      auto switchWatermarkStats = getAllSwitchWatermarkStats();
      if (!switchWatermarkStats.contains(switchIndex)) {
        continue;
      }
      sawSwitchWatermarkStats = true;
      auto deviceWatermarkBytes =
          *switchWatermarkStats.at(switchIndex).deviceWatermarkBytes();
      lastDeviceWatermarkBytes = deviceWatermarkBytes;
      XLOG(DBG0) << "Device watermark bytes: " << deviceWatermarkBytes;
      const auto minWatermarkBytes = getMinDeviceWatermarkValue(switchId);
      watermarkAsExpected |=
          (expectZero && (deviceWatermarkBytes <= minWatermarkBytes)) ||
          (!expectZero && (deviceWatermarkBytes > minWatermarkBytes));
      EXPECT_EVENTUALLY_TRUE(watermarkAsExpected);
      if (!FLAGS_multi_switch) {
        auto allCtrs = facebook::fb303::fbData->getCounters();
        auto ctr = allCtrs.find(kFb303DeviceWatermarkCtr);
        // We cant look for the exact value of the watermark counter,
        // but make sure the device watermark counter is available.
        fb303DeviceCtrPresent = (ctr != allCtrs.end());
        EXPECT_EVENTUALLY_TRUE(ctr != allCtrs.end());
        if (ctr != allCtrs.end()) {
          XLOG(DBG0) << ctr->first << " : " << ctr->second;
        }
      }
    });
    if (!watermarkAsExpected) {
      XLOG(DBG2) << "Device watermark bytes check failed: switchId=" << switchId
                 << " expectZero=" << std::boolalpha << expectZero
                 << " minWatermarkBytes="
                 << getMinDeviceWatermarkValue(switchId)
                 << " lastDeviceWatermarkBytes=" << lastDeviceWatermarkBytes
                 << " sawSwitchWatermarkStats=" << std::boolalpha
                 << sawSwitchWatermarkStats;
      if (!FLAGS_multi_switch) {
        XLOG(DBG2) << "fb303 counter '" << kFb303DeviceWatermarkCtr
                   << "' present=" << std::boolalpha << fb303DeviceCtrPresent;
      }
    }
    return watermarkAsExpected;
  }

  bool gotExpectedWatermark(
      const PortID& port,
      int queueId,
      bool expectZero,
      bool isVoq) {
    std::map<int16_t, int64_t> queueWaterMarks;
    auto queueTypeStr = isVoq ? " voq queueId: " : " queueId: ";
    auto portName =
        getProgrammedState()->getPorts()->getNodeIf(port)->getName();
    bool statsConditionMet = false;
    std::string egressGvoqWatermarkBytesLog{};
    WITH_RETRIES({
      queueWaterMarks = getQueueWatermarks(port, isVoq);
      XLOG(DBG2) << "Port: " << portName << queueTypeStr << queueId
                 << " Watermark: " << queueWaterMarks[queueId];
      statsConditionMet = (expectZero && !queueWaterMarks[queueId]) ||
          (!expectZero && queueWaterMarks[queueId]);
      EXPECT_EVENTUALLY_TRUE(statsConditionMet);
      if (isSupportedOnAllAsics(HwAsic::Feature::EGRESS_GVOQ_WATERMARK_BYTES)) {
        uint64_t egressGvoqWatermarkBytes =
            (*getLatestPortStats(port).egressGvoqWatermarkBytes_())[queueId];
        statsConditionMet &=
            ((expectZero && !egressGvoqWatermarkBytes) ||
             (!expectZero && egressGvoqWatermarkBytes));
        EXPECT_EVENTUALLY_TRUE(statsConditionMet);
        egressGvoqWatermarkBytesLog = ", GVOQ watermark bytes: " +
            std::to_string(egressGvoqWatermarkBytes);
      }
    });

    if (!statsConditionMet) {
      auto expectation = expectZero ? "zero" : "non-zero";
      XLOG(ERR) << "Queue watermark check failed: port=" << portName
                << " queueId=" << queueId << " isVoq=" << std::boolalpha
                << isVoq << " expectZero=" << expectZero << " expected "
                << expectation
                << " queueWatermarkBytes=" << queueWaterMarks[queueId];
    } else {
      XLOG(DBG2) << "Port: " << portName << queueTypeStr << queueId
                 << " got expected queue watermarks of "
                 << queueWaterMarks[queueId] << egressGvoqWatermarkBytesLog;
    }
    // Check fb303
    checkFb303BufferWatermarkUcast(port, queueId, isVoq);
    return statsConditionMet;
  }

  void assertWatermark(
      PortID port,
      const HwAsic* asic,
      int queueId,
      bool expectZero) {
    EXPECT_TRUE(gotExpectedWatermark(
        port,
        queueId,
        expectZero,
        asic->getSwitchType() == cfg::SwitchType::VOQ));
  }

  // Add NDP neighbors to switch state and populate to neighbor cache
  void resolveNdpNeighbors(
      PortDescriptor portDesc,
      bool needTrafficLoop = false) {
    utility::EcmpSetupTargetedPorts6 ecmpHelper6{
        getProgrammedState(),
        getSw()->needL2EntryForNeighbor(),
        (needTrafficLoop ? std::make_optional<folly::MacAddress>(
                               getMacForFirstInterfaceWithPortsForTesting(
                                   getProgrammedState()))
                         : std::nullopt)};

    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return ecmpHelper6.resolveNextHops(in, {portDesc});
    });

    if (needTrafficLoop) {
      const auto& nextHop = ecmpHelper6.nhop(portDesc);
      utility::disablePortTTLDecrementIfSupported(
          getAgentEnsemble(), ecmpHelper6.getRouterId(), nextHop);
    }

    auto interfaceId = ecmpHelper6.getInterface(portDesc, getProgrammedState());
    if (interfaceId) {
      auto interface =
          getProgrammedState()->getInterfaces()->getNodeIf(interfaceId.value());
      populateNdpNeighborsToCache(interface);
    } else {
      XLOG(WARN) << "Interface ID not found in ECMP setup for port "
                 << portDesc.phyPortID()
                 << ". Skipping resolution of NDP neighbor";
    }
  }

  void _setup(bool needTrafficLoop = false) {
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    std::optional<folly::MacAddress> macAddr{};
    if (needTrafficLoop) {
      macAddr = intfMac;
    }
    utility::EcmpSetupTargetedPorts6 ecmpHelper6{
        getProgrammedState(), getSw()->needL2EntryForNeighbor(), macAddr};
    for (const auto& portAndIp : getPort2DstIp()) {
      auto portDesc = PortDescriptor(portAndIp.first);
      resolveNdpNeighbors(portDesc, needTrafficLoop);
      auto routeUpdater = getSw()->getRouteUpdater();
      ecmpHelper6.programRoutes(
          &routeUpdater,
          {portDesc},
          {Route<folly::IPAddressV6>::Prefix{portAndIp.second, 128}},
          {},
          std::nullopt,
          needTrafficLoop ? std::make_optional(true) : std::nullopt);
      if (needTrafficLoop) {
        const auto& nextHop = ecmpHelper6.nhop(portDesc);
        utility::disablePortTTLDecrementIfSupported(
            getAgentEnsemble(), ecmpHelper6.getRouterId(), nextHop);
      }
    }
  }

  void runTest(int queueId) {
    auto setup = [this]() {
      // Needs a traffic loop to accommodate some ASICs like Jericho
      _setup(true /*needTrafficLoop*/);
    };
    auto verify = [this, queueId]() {
      for (const auto& portAndIp : getPort2DstIp()) {
        const auto asic = getSw()->getHwAsicTable()->getHwAsic(
            scopeResolver().scope(portAndIp.first).switchId());
        auto dscpsForQueue =
            utility::kOlympicQueueToDscp().find(queueId)->second;
        // Assert zero watermark
        assertWatermark(portAndIp.first, asic, queueId, true /*expectZero*/);
        // Send traffic
        sendUdpPkts(dscpsForQueue[0], portAndIp.second);
        // Assert non zero watermark
        assertWatermark(portAndIp.first, asic, queueId, false /*expectZero*/);
        // Stop traffic
        stopTraffic(portAndIp.first, true);
        // Assert zero watermark
        assertWatermark(portAndIp.first, asic, queueId, true /*expectZero*/);
      }
    };
    verifyAcrossWarmBoots(setup, verify);
  }
};

TEST_F(AgentWatermarkTest, VerifyDefaultQueue) {
  runTest(getOlympicQueueId(utility::OlympicQueueType::SILVER));
}

TEST_F(AgentWatermarkTest, VerifyNonDefaultQueue) {
  runTest(getOlympicQueueId(utility::OlympicQueueType::GOLD));
}

// TODO - merge device watermark checking into the tests
// above once all platforms support device watermarks
TEST_F(AgentWatermarkTest, VerifyDeviceWatermark) {
  auto setup = [this]() { _setup(true); };
  auto verify = [this]() {
    for (const auto& switchId : switchIdsUnderTest()) {
      auto portToSendTraffic =
          getAgentEnsemble()->masterLogicalInterfacePortIds(switchId)[0];
      auto minPktsForLineRate =
          getAgentEnsemble()->getMinPktsForLineRate(portToSendTraffic);
      sendUdpPkts(0, folly::IPAddressV6(kTestDstIpV6), minPktsForLineRate);
      getAgentEnsemble()->waitForLineRateOnPort(portToSendTraffic);
      // Assert non zero watermark
      EXPECT_TRUE(gotExpectedDeviceWatermark(false, switchId));

      // Now, break the loop to make sure traffic goes to zero!
      getAgentEnsemble()->getLinkToggler()->bringDownPorts({portToSendTraffic});
      getAgentEnsemble()->getLinkToggler()->bringUpPorts({portToSendTraffic});
      resolveNdpNeighbors(PortDescriptor(portToSendTraffic), true);

      // Assert zero watermark
      EXPECT_TRUE(gotExpectedDeviceWatermark(true, switchId));
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentWatermarkTest, VerifyDeviceWatermarkHigherThanQueueWatermark) {
  auto setup = [this]() {
    _setup(true);
    for (const auto& switchId : switchIdsUnderTest()) {
      auto minPktsForLineRate = getAgentEnsemble()->getMinPktsForLineRate(
          getAgentEnsemble()->masterLogicalInterfacePortIds(switchId)[0]);
      // Sending traffic on 2 queues
      sendUdpPkts(
          utility::kOlympicQueueToDscp()
              .at(utility::getOlympicQueueId(utility::OlympicQueueType::SILVER))
              .front(),
          folly::IPAddressV6(kTestDstIpV6),
          minPktsForLineRate / 2);
      sendUdpPkts(
          utility::kOlympicQueueToDscp()
              .at(utility::getOlympicQueueId(utility::OlympicQueueType::GOLD))
              .front(),
          folly::IPAddressV6(kTestDstIpV6),
          minPktsForLineRate / 2);
      getAgentEnsemble()->waitForLineRateOnPort(
          getAgentEnsemble()->masterLogicalInterfacePortIds(switchId)[0]);
    }
  };
  auto verify = [this]() {
    for (const auto& switchId : switchIdsUnderTest()) {
      auto queueIdGold =
          utility::getOlympicQueueId(utility::OlympicQueueType::GOLD);
      auto queueIdSilver =
          utility::getOlympicQueueId(utility::OlympicQueueType::SILVER);
      auto queueWatermarkNonZero =
          [queueIdGold,
           queueIdSilver](std::map<int16_t, int64_t>& queueWaterMarks) {
            if (queueWaterMarks.at(queueIdSilver) ||
                queueWaterMarks.at(queueIdGold)) {
              return true;
            }
            return false;
          };

      /*
       * Now we are at line rate on port, make sure that queue watermark
       * is non-zero! As of now, the assumption is as below:
       *
       * 1. VoQ switches has device watermarks being reported from ingress
       *    buffers, hence compare it with VoQ watermarks
       * 2. Non-voq switches just has egress queue watermarks which is from
       *    the same buffer as the device watermark is reported.
       *
       * In case of new platforms, make sure that the assumption holds.
       */
      std::map<int16_t, int64_t> queueWaterMarks;
      WITH_RETRIES_N_TIMED(10, std::chrono::milliseconds(1000), {
        queueWaterMarks = getQueueWatermarks(
            getAgentEnsemble()->masterLogicalInterfacePortIds(switchId)[0],
            getSw()->getHwAsicTable()->getHwAsic(switchId)->getSwitchType() ==
                cfg::SwitchType::VOQ /*isVoq*/);
        EXPECT_EVENTUALLY_TRUE(queueWatermarkNonZero(queueWaterMarks));
      });

      // Get device watermark now, so that it is > highest queue watermark!
      const auto switchIndex =
          getSw()->getSwitchInfoTable().getSwitchIndexFromSwitchId(switchId);
      auto switchWatermarkStats = getAllSwitchWatermarkStats();
      auto deviceWatermark =
          *switchWatermarkStats.at(switchIndex).deviceWatermarkBytes();
      XLOG(DBG2) << "For port: "
                 << getAgentEnsemble()->masterLogicalInterfacePortIds(
                        switchId)[0]
                 << ", Queue" << queueIdSilver
                 << " watermark: " << queueWaterMarks.at(queueIdSilver)
                 << ", Queue" << queueIdGold
                 << " watermark: " << queueWaterMarks.at(queueIdGold)
                 << ", Device watermark: " << deviceWatermark;

      // Make sure that device watermark is > highest queue watermark
      EXPECT_GT(
          deviceWatermark,
          std::max(
              queueWaterMarks.at(queueIdSilver),
              queueWaterMarks.at(queueIdGold)));
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentWatermarkTest, VerifyQueueWatermarkAccuracy) {
  auto setup = [this]() { _setup(false); };
  auto verify = [this]() {
    for (const auto& switchId : switchIdsUnderTest()) {
      const auto asic = getSw()->getHwAsicTable()->getHwAsic(switchId);
      /*
       * This test ensures that the queue watermark reported is accurate.
       * For this, we start by clearing any watermark from previous tests.
       * We need to know the exact number of bytes a packet will need while
       * it is in the ASIC queue. This size includes the headers + overhead
       * used in the ASIC for each packet. Once we know the number of bytes
       * that will be used per packet, with TX disable function, we ensure
       * that all the packets to be TXed is queued up, resulting in a high
       * watermark which is predictable. Then compare and make sure that
       * the watermark reported is as expected by the computed watermark
       * based on the number of bytes we expect in the queue.
       */
      auto kQueueId =
          utility::getOlympicQueueId(utility::OlympicQueueType::SILVER);
      constexpr auto kTxPacketPayloadLen{200};
      constexpr auto kNumberOfPacketsToSend{100};
      const bool isVoq = asic->getSwitchType() == cfg::SwitchType::VOQ;
      auto txPacketLen = kTxPacketPayloadLen + EthHdr::SIZE + IPv6Hdr::size() +
          UDPHeader::size();
      // Clear any watermark stats
      getQueueWatermarks(
          getAgentEnsemble()->masterLogicalInterfacePortIds(switchId)[0],
          isVoq);

      auto sendPackets = [=, this](PortID /*port*/, int numPacketsToSend) {
        // Send packets out on port1, so that it gets looped back, and
        // forwarded in the pipeline to egress port0 where the watermark
        // will be validated.
        sendUdpPkts(
            utility::kOlympicQueueToDscp().at(kQueueId).front(),
            folly::IPAddressV6(kTestDstIpV6),
            numPacketsToSend,
            kTxPacketPayloadLen,
            getAgentEnsemble()->masterLogicalInterfacePortIds(switchId)[1]);
      };

      utility::sendPacketsWithQueueBuildup(
          sendPackets,
          getAgentEnsemble(),
          masterLogicalInterfacePortIds()[0],
          kNumberOfPacketsToSend);

      uint64_t expectedWatermarkBytes;
      uint64_t roundedWatermarkBytes;
      // Zero for every ASIC except Yuba and G202X, which may report one
      // extra buffer when a leaked packet is re-enqueued from MAC loopback.
      uint64_t extraBufferBytes = 0;
      if (asic->getAsicType() == cfg::AsicType::ASIC_TYPE_JERICHO2) {
        // For Jericho2, there is a limitation that one packet less than
        // expected watermark shows up in watermark.
        expectedWatermarkBytes =
            utility::getEffectiveBytesPerPacket(asic, txPacketLen) *
            (kNumberOfPacketsToSend - 1);
        // Watermarks read in are accurate, no rounding needed
        roundedWatermarkBytes = expectedWatermarkBytes;
      } else if (
          asic->getAsicType() == cfg::AsicType::ASIC_TYPE_YUBA ||
          asic->getAsicType() == cfg::AsicType::ASIC_TYPE_G202X) {
        // For Yuba, watermark counter is accurate to the number of buffers
        expectedWatermarkBytes =
            utility::getEffectiveBytesPerPacket(asic, txPacketLen) *
            kNumberOfPacketsToSend;
        roundedWatermarkBytes = expectedWatermarkBytes;
        extraBufferBytes = asic->getPacketBufferUnitSize();
      } else {
        expectedWatermarkBytes =
            utility::getEffectiveBytesPerPacket(asic, txPacketLen) *
            kNumberOfPacketsToSend;
        roundedWatermarkBytes =
            utility::getRoundedBufferThreshold(asic, expectedWatermarkBytes);
      }
      std::map<int16_t, int64_t> queueWaterMarks;
      int64_t maxWatermarks = 0;
      WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(1000), {
        queueWaterMarks =
            getQueueWatermarks(masterLogicalInterfacePortIds()[0], isVoq);
        if (queueWaterMarks.at(kQueueId) > maxWatermarks) {
          maxWatermarks = queueWaterMarks.at(kQueueId);
        }
        if (extraBufferBytes != 0) {
          EXPECT_EVENTUALLY_TRUE(
              maxWatermarks == static_cast<int64_t>(roundedWatermarkBytes) ||
              maxWatermarks ==
                  static_cast<int64_t>(
                      roundedWatermarkBytes + extraBufferBytes));
        } else {
          EXPECT_EVENTUALLY_EQ(maxWatermarks, roundedWatermarkBytes);
        }
      });

      XLOG(DBG0) << "Expected rounded watermark bytes: "
                 << roundedWatermarkBytes
                 << ", reported watermark bytes: " << maxWatermarks
                 << ", pkts TXed: " << kNumberOfPacketsToSend
                 << ", pktLen: " << txPacketLen << ", effectiveBytesPerPacket: "
                 << utility::getEffectiveBytesPerPacket(asic, txPacketLen);
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
