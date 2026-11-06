// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <folly/IPAddress.h>
#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AgentTestAddressConstants.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/MultiPortTrafficTestUtils.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"
#include "fboss/agent/test/utils/QosTestUtils.h"
#include "fboss/lib/CommonUtils.h"

namespace facebook::fboss {

namespace {
constexpr int kReservedBytesPerQueue = 10240; // 10KB
constexpr int kMaxTestPorts = 8;
constexpr int kPktsPerPort = 1000;
constexpr int kLineRateMaxRetries = 60;
} // namespace

class AgentEgressQueueScalingFactorTest : public AgentHwTest {
 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);
    utility::addOlympicQueueConfig(&cfg, ensemble.getL3Asics());
    utility::addOlympicQosMaps(cfg, ensemble.getL3Asics());
    utility::setTTLZeroCpuConfig(ensemble.getL3Asics(), cfg);
    // Override reservedBytes only on queues that already have it set
    // by addOlympicQueueConfig. Setting it on all queues across all
    // ports can exceed the MMU budget on large port-count platforms.
    auto& portQueues = cfg.portQueueConfigs()["queue_config"];
    for (auto& queue : portQueues) {
      if (queue.reservedBytes().has_value()) {
        queue.reservedBytes() = kReservedBytesPerQueue;
      }
    }
    return cfg;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::L3_QOS, ProductionFeature::OLYMPIC_QOS};
  }

  MacAddress dstMac() const {
    return getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
  }

  void sendUdpPktToDst(uint8_t dscpVal, const folly::IPAddressV6& dstIp) {
    auto intfMac = dstMac();
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    auto pkt = utility::makeUDPTxPacket(
        getSw(),
        getVlanIDForTx(),
        srcMac,
        intfMac,
        folly::IPAddressV6(kTestSrcIpV6),
        dstIp,
        kTestSrcPort,
        kTestDstPort,
        static_cast<uint8_t>(dscpVal << 2),
        255,
        std::vector<uint8_t>(1200, 0xff));
    getSw()->sendPacketSwitchedAsync(std::move(pkt));
  }

  // Generate destination IPs matching setupEcmpDataplaneLoopOnPorts:
  // ports[i] gets IP 2401::(i+1). This ensures correct port-to-IP
  // mapping regardless of which physical ports are passed in.
  std::vector<folly::IPAddressV6> getHostIpsForPorts(
      const std::vector<PortID>& portIds) const {
    std::vector<folly::IPAddressV6> ips;
    ips.reserve(portIds.size());
    for (size_t i = 0; i < portIds.size(); i++) {
      ips.emplace_back(folly::to<std::string>("2401::", i + 1));
    }
    return ips;
  }

  // Start looping traffic on the given ports by sending enough packets
  // to reach line rate, then verify all ports reach >= 90% line rate.
  void startTrafficOnPorts(const std::vector<PortID>& portIds) {
    auto hostIps = getHostIpsForPorts(portIds);

    // Send packets on every port to start traffic loops
    for (size_t i = 0; i < portIds.size(); i++) {
      for (int pkt = 0; pkt < kPktsPerPort; pkt++) {
        sendUdpPktToDst(0 /*dscp*/, hostIps[i]);
      }
    }
    XLOG(DBG2) << "Sent " << kPktsPerPort << " packets on each of "
               << portIds.size() << " ports";

    utility::waitForLineRateOnPorts(
        getAgentEnsemble(),
        portIds,
        90 /*desiredPctLineRate*/,
        kLineRateMaxRetries);
  }

  void verifyTrafficFlowingOnPorts(const std::vector<PortID>& portIds) {
    auto hostIps = getHostIpsForPorts(portIds);
    auto queueToDscp = utility::kOlympicQueueToDscp();
    for (size_t i = 0; i < portIds.size(); i++) {
      auto portStatsBefore = getLatestPortStats(portIds[i]);
      for (const auto& q2dscps : queueToDscp) {
        for (auto dscp : q2dscps.second) {
          sendUdpPktToDst(dscp, hostIps[i]);
        }
      }
      WITH_RETRIES({
        EXPECT_EVENTUALLY_TRUE(
            utility::verifyQueueMappings(
                portStatsBefore, queueToDscp, getAgentEnsemble(), portIds[i]));
      });
    }
  }

  cfg::SwitchConfig getConfigWithScalingFactor(
      int queueId,
      cfg::MMUScalingFactor scalingFactor) {
    auto config = initialConfig(*getAgentEnsemble());
    auto asic = checkSameAndGetAsic(getAgentEnsemble()->getL3Asics());
    if (asic->scalingFactorBasedDynamicThresholdSupported()) {
      auto& portQueues = config.portQueueConfigs()["queue_config"];
      for (auto& queue : portQueues) {
        if (*queue.id() == queueId) {
          queue.scalingFactor() = scalingFactor;
          break;
        }
      }
    }
    return config;
  }
};

// Validates that changing the MMU scaling factor on a queue works
// both before and after warm boot. Traffic loops at line rate on
// all ports. The verify phase changes scaling factor from ONE to
// EIGHT and back to ONE on the silver queue, checking traffic
// continues to flow on all ports. The same verify runs pre and
// post warm boot.
TEST_F(
    AgentEgressQueueScalingFactorTest,
    VerifyScalingFactorChangeWithTraffic) {
  auto allPortIds = masterLogicalInterfacePortIds();
  auto numPorts = std::min(static_cast<int>(allPortIds.size()), kMaxTestPorts);
  std::vector<PortID> portIds(
      allPortIds.begin(), allPortIds.begin() + numPorts);

  auto setup = [portIds, this]() {
    utility::setupEcmpDataplaneLoopOnPorts(getAgentEnsemble(), portIds);
    startTrafficOnPorts(portIds);
  };

  auto verify = [portIds, this]() {
    auto queueId =
        utility::getOlympicQueueId(utility::OlympicQueueType::SILVER);

    // Verify traffic with initial scaling factor (ONE)
    verifyTrafficFlowingOnPorts(portIds);

    // Change scaling factor from ONE to EIGHT
    applyNewConfig(
        getConfigWithScalingFactor(queueId, cfg::MMUScalingFactor::EIGHT));
    verifyTrafficFlowingOnPorts(portIds);

    // Change scaling factor back from EIGHT to ONE
    applyNewConfig(
        getConfigWithScalingFactor(queueId, cfg::MMUScalingFactor::ONE));
    verifyTrafficFlowingOnPorts(portIds);
  };

  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
