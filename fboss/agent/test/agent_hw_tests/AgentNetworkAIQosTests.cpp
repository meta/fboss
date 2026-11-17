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
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/agent_hw_tests/AgentQosTestBase.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/NetworkAITestUtils.h"
#include "fboss/agent/test/utils/PfcTestUtils.h"
#include "fboss/agent/test/utils/QosTestUtils.h"
#include "fboss/agent/test/utils/QueueTestUtils.h"

constexpr int kPayloadSize = 4500;
DEFINE_int32(lossy_queue_test_qmin, 5000, "Queue min guarantee in bytes");
DEFINE_int32(lossy_queue_test_pkts, 100, "Number of packets to send");

namespace facebook::fboss {

class AgentNetworkAIQosTests : public AgentQosTestBase {
 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);
    auto hwAsic = checkSameAndGetAsicForTesting(ensemble.getL3Asics());
    auto streamType =
        *hwAsic->getQueueStreamTypes(cfg::PortType::INTERFACE_PORT).begin();
    utility::addNetworkAIQueueConfig(
        &cfg, streamType, cfg::QueueScheduling::STRICT_PRIORITY, hwAsic);
    utility::addNetworkAIQosMaps(cfg, ensemble.getL3Asics());
    if (hwAsic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
      utility::setDefaultCpuTrafficPolicyConfig(
          cfg, ensemble.getL3Asics(), ensemble.isSai());
      utility::addCpuQueueConfig(cfg, ensemble.getL3Asics(), ensemble.isSai());
      utility::setTTLZeroCpuConfig(ensemble.getL3Asics(), cfg);
    }
    return cfg;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::L3_QOS, ProductionFeature::NETWORKAI_QOS};
  }
};

// Verify that traffic arriving on a front panel/cpu port is qos mapped to the
// correct queue for each olympic dscp value.
TEST_F(AgentNetworkAIQosTests, VerifyDscpQueueMapping) {
  verifyDscpQueueMapping(utility::kNetworkAIQueueToDscp());
}

class AgentNetworkAILossyQueueTests : public AgentQosTestBase {
  void setCmdLineFlagOverrides() const override {
    AgentQosTestBase::setCmdLineFlagOverrides();
    FLAGS_egress_buffer_pool_size = kBufferPoolSize;
  }

  void applyPlatformConfigOverrides(
      const cfg::SwitchConfig& sw,
      cfg::PlatformConfig& config) const override {
    utility::applyBackendAsicConfig(sw, config);
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);
    auto hwAsic = checkSameAndGetAsicForTesting(ensemble.getL3Asics());
    auto portIds = ensemble.masterLogicalInterfacePortIds();
    std::vector<PortID> testPortIds{portIds[0]};

    // Program a high PG limit so that drops won't happen on ingress.
    auto bufferParams =
        utility::PfcBufferParams::getPfcBufferParams(hwAsic->getAsicType());
    bufferParams.globalShared = kBufferPoolSize;
    bufferParams.globalHeadroom = 0;
    bufferParams.pgShared = kBufferPoolSize;
    bufferParams.minLimit = 0;
    utility::setupPfcBuffers(
        &ensemble,
        cfg,
        testPortIds,
        {} /*losslessPgIds*/,
        {0} /*lossyPgIds*/,
        bufferParams);

    // Set a zero shared limit in order to exercise Qmin.
    auto streamType =
        *hwAsic->getQueueStreamTypes(cfg::PortType::INTERFACE_PORT).begin();
    utility::addNetworkAIQueueConfig(
        &cfg,
        streamType,
        cfg::QueueScheduling::STRICT_PRIORITY,
        hwAsic,
        false,
        false,
        {},
        testPortIds);
    for (auto& portQueue :
         cfg.portQueueConfigs()->at(utility::kNetworkAIQueueConfigName)) {
      if (*portQueue.id() == utility::kNetworkAIDefaultQueueId) {
        portQueue.reservedBytes() = FLAGS_lossy_queue_test_qmin;
        portQueue.sharedBytes() = 0;
      }
    }
    utility::addEgressTestPoolIfRequired(
        &cfg,
        cfg.portQueueConfigs()->at(utility::kNetworkAIQueueConfigName),
        hwAsic);

    return cfg;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::L3_QOS,
        ProductionFeature::NETWORKAI_QOS,
        ProductionFeature::STATIC_PG_SHARED_LIMIT};
  }

 protected:
  constexpr static auto kBufferPoolSize = 2000000;
  const folly::IPAddressV6 kIp{"2401::3333"};

  void setupEcmpTraffic(const PortID& portId, const folly::IPAddressV6& ip) {
    utility::EcmpSetupTargetedPorts6 ecmpHelper{
        getProgrammedState(),
        getSw()->needL2EntryForNeighbor(),
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState())};

    const PortDescriptor port(portId);
    RoutePrefixV6 route{ip, 128};

    applyNewState([&](const std::shared_ptr<SwitchState>& state) {
      return ecmpHelper.resolveNextHops(state, {port});
    });

    auto routeUpdater = getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(
        &routeUpdater, {port}, {route}, {}, std::nullopt, true);
    utility::disablePortTTLDecrementIfSupported(
        getAgentEnsemble(), ecmpHelper.getRouterId(), ecmpHelper.nhop(port));
  }

  std::string portDesc(const PortID& portId) {
    return fmt::format(
        "portId={} name={}",
        portId,
        getAgentEnsemble()
            ->getProgrammedState()
            ->getPorts()
            ->getNodeIf(portId)
            ->getName());
  }

  std::pair<int, int> getWatermarks(PortID portId, int priority = 0) {
    auto portStats = getLatestPortStats(portId);
    const auto& portName = getAgentEnsemble()
                               ->getProgrammedState()
                               ->getPorts()
                               ->getNodeIf(portId)
                               ->getName();
    auto regex = fmt::format(
        "buffer_watermark_pg_shared.{}.pg{}.p100.60", portName, priority);
    auto counter = getAgentEnsemble()->getFb303CounterIfExists(portId, regex);
    return std::make_pair(
        folly::get_default(*portStats.queueWatermarkBytes_(), priority, 0),
        counter.value_or(0));
  }

  void logPortStats(PortID portId) {
    auto portStats = getLatestPortStats(portId);
    auto [pgWatermark, queueWatermark] = getWatermarks(portId);
    auto outDiscards = folly::get_default(
        *portStats.queueOutDiscardPackets_(),
        utility::kNetworkAIDefaultQueueId,
        0);
    XLOG(INFO) << "Port " << portId << " pgWatermark: " << pgWatermark
               << " queueWatermark: " << queueWatermark
               << " inDiscards: " << *portStats.inDiscards_()
               << " outDiscards: " << outDiscards;
  }
};

// Verifies that congestion on a lossy priority results in queue drops instead
// of PG drops. To do so, we program queue 0 with a small queue min and zero
// shared buffer limit (to simulate congestion). We expect to see some amount
// of queue drops and no in discards, as well as some traffic flowing.
TEST_F(AgentNetworkAILossyQueueTests, VerifyEgressQueueDrop) {
  PortID testPortId = masterLogicalInterfacePortIds()[0];
  XLOG(DBG3) << "Test port: " << portDesc(testPortId);

  const auto sendPacket = [&](AgentEnsemble* ensemble,
                              const folly::IPAddressV6& dstIp,
                              std::optional<PortID> portId = std::nullopt) {
    folly::IPAddressV6 kSrcIp("2402::1");
    const auto dstMac = getMacForFirstInterfaceWithPortsForTesting(
        ensemble->getProgrammedState());
    const auto srcMac = utility::MacAddressGenerator().get(dstMac.u64HBO() + 1);
    auto txPacket = utility::makeUDPTxPacket(
        ensemble->getSw(),
        getVlanIDForTx(),
        srcMac,
        dstMac,
        kSrcIp,
        dstIp,
        8000, // l4 src port
        8001, // l4 dst port
        0 << 2, // dscp for lossy traffic
        255, // hopLimit
        std::vector<uint8_t>(kPayloadSize));
    if (portId.has_value()) {
      ensemble->getSw()->sendPacketOutOfPortAsync(std::move(txPacket), *portId);
    } else {
      sendPacketSwitchedAsync(std::move(txPacket));
    }
  };

  auto setup = [=, this]() { setupEcmpTraffic(testPortId, kIp); };

  auto verify = [=, this]() {
    auto portStatsBefore = getLatestPortStats(testPortId);
    auto queueDropsBefore = folly::get_default(
        *portStatsBefore.queueOutDiscardPackets_(),
        utility::kNetworkAIDefaultQueueId,
        0);

    for (int i = 0; i < FLAGS_lossy_queue_test_pkts; ++i) {
      sendPacket(getAgentEnsemble(), kIp, testPortId);
    }

    WITH_RETRIES({
      logPortStats(testPortId);
      auto portStatsAfter = getLatestPortStats(testPortId);
      auto queueDropsAfter = folly::get_default(
          *portStatsAfter.queueOutDiscardPackets_(),
          utility::kNetworkAIDefaultQueueId,
          0);

      // We expect to see:
      // 1. No in discards or in congestion discards
      // 2. Some packets should be looping
      // 3. Some packets should be queue dropped
      EXPECT_EQ(*portStatsAfter.inDiscards_(), *portStatsBefore.inDiscards_());
      EXPECT_EQ(
          *portStatsAfter.inCongestionDiscards_(),
          *portStatsBefore.inCongestionDiscards_());
      EXPECT_EVENTUALLY_GT(
          *portStatsAfter.outBytes_() - *portStatsBefore.outBytes_(),
          kPayloadSize * 100000ll); // packets have looped many times
      EXPECT_EVENTUALLY_GT(queueDropsAfter, queueDropsBefore);
    });
  };

  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
