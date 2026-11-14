// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AgentTestAddressConstants.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/MultiPortTrafficTestUtils.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"
#include "fboss/agent/test/utils/QosTestUtils.h"

namespace facebook::fboss {

class AgentTrafficPauseTest : public AgentHwTest {
 public:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);
    utility::setTTLZeroCpuConfig(ensemble.getL3Asics(), config);
    return config;
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    // Turn on Leaba SDK shadow cache to avoid test case timeout
    FLAGS_counter_refresh_interval = 1;
  }

  void sendPauseFrames(const PortID& portId, const int count) {
    // PAUSE frame to have the highest quanta of 0xffff
    std::vector<uint8_t> payload{0x00, 0x01, 0xff, 0xff};
    std::vector<uint8_t> padding(42, 0);
    payload.insert(payload.end(), padding.begin(), padding.end());
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    for (int idx = 0; idx < count; idx++) {
      auto pkt = utility::makeEthTxPacket(
          getSw(),
          getVlanIDForTx(),
          utility::MacAddressGenerator().get(intfMac.u64HBO() + 1),
          folly::MacAddress("01:80:C2:00:00:01"),
          ETHERTYPE::ETHERTYPE_EPON,
          payload);
      // Inject pause frames on highest priority queue to avoid drops
      getSw()->sendPacketOutOfPortAsync(
          std::move(pkt),
          portId,
          utility::getOlympicQueueId(utility::OlympicQueueType::NC));
    }
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::PAUSE};
  }

  static void sendPacket(
      AgentEnsemble* ensemble,
      const folly::IPAddressV6& destIpv6Addr) {
    auto vlanId = ensemble->getVlanIDForTx();
    auto intfMac = getMacForFirstInterfaceWithPortsForTesting(
        ensemble->getProgrammedState());
    auto dscp = utility::kOlympicQueueToDscp().at(0).front();
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    auto txPacket = utility::makeUDPTxPacket(
        ensemble->getSw(),
        vlanId,
        srcMac,
        intfMac,
        folly::IPAddressV6(kTestSrcIpV6),
        destIpv6Addr,
        kTestSrcPort,
        kTestDstPort,
        dscp << 2,
        255,
        std::vector<uint8_t>(2000, 0xff));
    ensemble->getSw()->sendPacketSwitchedAsync(
        std::move(txPacket), {getSwitchIdUnderTest(*ensemble)});
  }

  // Ensure that ports which dont have pause received can
  // still operate at line rate.
  void validateLineRateOnNonPausePorts(
      const std::vector<PortID>& portsWithLineRateTraffic) {
    // Stats timestamps are whole seconds, so a wider window is needed to keep
    // the resulting error below the margin asserted on below.
    constexpr int kRateCheckWindowSec{5};
    for (const auto& portId : portsWithLineRateTraffic) {
      std::optional<HwPortStats> beforePortStats;
      uint64_t ninetySevenPctLineRate =
          static_cast<uint64_t>(
              getProgrammedState()->getPorts()->getNodeIf(portId)->getSpeed()) *
          1000 * 1000 * 0.97;
      int iteration{0};
      WITH_RETRIES_N_TIMED(
          6, std::chrono::milliseconds(1000 * kRateCheckWindowSec), {
            auto afterPortStats = getNextUpdatedPortStats(portId);
            if (!beforePortStats.has_value()) {
              // Skip first iteration as rate computation wont be accurate!
              beforePortStats = afterPortStats;
              continue;
            }
            auto trafficRate = getAgentEnsemble()->getTrafficRate(
                *beforePortStats, afterPortStats, kRateCheckWindowSec);
            XLOG(DBG0) << "Iteration: " << iteration++
                       << ", port ID: " << portId
                       << ", expected 97% line rate : "
                       << ninetySevenPctLineRate
                       << " bps, observed traffic rate: " << trafficRate
                       << " bps";
            EXPECT_EVENTUALLY_GT(trafficRate, ninetySevenPctLineRate);
            beforePortStats = afterPortStats;
          });
    }
  }

  void validateTrafficWithPause(
      const cfg::PortPause& pauseCfg,
      std::function<bool(uint64_t, PortID)> rateChecker) {
    const PortID kPausedPortId{masterLogicalInterfacePortIds()[0]};
    auto setup = [&]() {
      auto cfg = getAgentEnsemble()->getCurrentConfig();
      // Enable the same Pause configs on all ports
      for (auto& port : *cfg.ports()) {
        if (port.portType() == cfg::PortType::INTERFACE_PORT) {
          port.pause() = pauseCfg;
        }
      }

      applyNewConfig(cfg);
      utility::setupEcmpDataplaneLoopOnAllPorts(getAgentEnsemble());
    };
    auto verify = [&]() {
      const int kNumPortsWithLineRateTraffic{5};
      utility::createTrafficOnMultiplePorts(
          getAgentEnsemble(),
          kNumPortsWithLineRateTraffic,
          sendPacket,
          99 /*desiredPctLineRate*/);
      // Now that we have line rate traffic, send pause which
      // should reduce the traffic rate below line rate.
      XLOG(DBG0)
          << "Traffic on ports reached line rate, now send back to back PAUSE!";
      std::atomic<bool> keepTxingPauseFrames{true};
      std::unique_ptr<std::thread> pauseTxThread =
          std::make_unique<std::thread>(
              [this, &keepTxingPauseFrames, &kPausedPortId]() {
                initThread("PauseFramesTransmitThread");
                while (keepTxingPauseFrames) {
                  this->sendPauseFrames(kPausedPortId, 1000);
                }
              });
      std::optional<HwPortStats> prevPortStats;
      HwPortStats curPortStats{};
      constexpr int kPausedPortRateCheckWindowSec{5};
      WITH_RETRIES_N_TIMED(
          15, std::chrono::milliseconds(1000 * kPausedPortRateCheckWindowSec), {
            curPortStats = getNextUpdatedPortStats(kPausedPortId);
            if (!prevPortStats.has_value()) {
              // Rate calculation wont be accurate
              prevPortStats = curPortStats;
              continue;
            }
            auto rate = getAgentEnsemble()->getTrafficRate(
                *prevPortStats, curPortStats, kPausedPortRateCheckWindowSec);
            XLOG(DBG0) << "Port " << kPausedPortId
                       << ", current rate is : " << rate
                       << " bps, pause frames received: "
                       << curPortStats.inPause_().value();
            // Update prev stats for the next iteration
            prevPortStats = curPortStats;
            EXPECT_EVENTUALLY_TRUE(rateChecker(rate, kPausedPortId));
          });
      // Make sure that ports without pause sees line rate traffic always
      auto allPorts{masterLogicalInterfacePortIds()};
      std::vector<PortID> lineRateTrafficPorts;
      std::copy_if(
          allPorts.begin(),
          allPorts.begin() + kNumPortsWithLineRateTraffic,
          std::back_inserter(lineRateTrafficPorts),
          [kPausedPortId](const PortID& portId) {
            return portId != kPausedPortId;
          });
      validateLineRateOnNonPausePorts(lineRateTrafficPorts);
      keepTxingPauseFrames = false;
      pauseTxThread->join();
      pauseTxThread.reset();
    };
    verifyAcrossWarmBoots(setup, verify);
  }
};
TEST_F(AgentTrafficPauseTest, verifyPauseRxOnly) {
  // Enable pause RX alone
  cfg::PortPause pauseCfg;
  pauseCfg.tx() = false;
  pauseCfg.rx() = true;
  // Pause should slow down traffic significantly, lets say eventual
  // rate should be less than 80% line rate!
  auto rateChecker = [this](uint64_t rate, const PortID& portId) {
    auto eightyPctLineRate =
        static_cast<uint64_t>(
            getProgrammedState()->getPorts()->getNodeIf(portId)->getSpeed()) *
        1000 * 1000 * 0.8;
    return rate && rate < eightyPctLineRate;
  };
  validateTrafficWithPause(pauseCfg, rateChecker);
}
TEST_F(AgentTrafficPauseTest, verifyPauseTxOnly) {
  // Enable pause TX alone
  cfg::PortPause pauseCfg;
  pauseCfg.tx() = true;
  pauseCfg.rx() = false;
  // Pause should have no impact on traffic given only TX is enabled
  auto rateChecker = [this](uint64_t rate, const PortID& portId) {
    auto ninetySevenPctLineRate =
        static_cast<uint64_t>(
            getProgrammedState()->getPorts()->getNodeIf(portId)->getSpeed()) *
        1000 * 1000 * 0.97;
    return rate >= ninetySevenPctLineRate;
  };
  validateTrafficWithPause(pauseCfg, rateChecker);
}
TEST_F(AgentTrafficPauseTest, verifyPauseRxTx) {
  // Enable both RX and TX pause
  cfg::PortPause pauseCfg;
  pauseCfg.tx() = true;
  pauseCfg.rx() = true;
  // Pause should slow down traffic significantly, lets say eventual
  // rate should be less than 80% line rate!
  auto rateChecker = [this](uint64_t rate, const PortID& portId) {
    auto eightyPctLineRate =
        static_cast<uint64_t>(
            getProgrammedState()->getPorts()->getNodeIf(portId)->getSpeed()) *
        1000 * 1000 * 0.8;
    return rate && rate < eightyPctLineRate;
  };
  validateTrafficWithPause(pauseCfg, rateChecker);
}
} // namespace facebook::fboss
