// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/test/utils/TrapPacketUtils.h"
#include "fboss/lib/CommonUtils.h"

namespace {
static folly::IPAddressV6 kSrcIp() {
  return folly::IPAddressV6("1::1");
}
} // namespace
namespace facebook::fboss {

class AgentDeepPacketInspectionTest : public AgentHwTest {
 public:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = AgentHwTest::initialConfig(ensemble);
    auto port = FLAGS_hyper_port ? ensemble.masterLogicalHyperPortIds()[0]
                                 : ensemble.masterLogicalInterfacePortIds()[0];
    utility::addOlympicQosMaps(config, ensemble.getL3Asics());
    auto asic = checkSameAndGetAsicForTesting(ensemble.getL3Asics());
    utility::addTrapPacketAcl(asic, &config, port);
    return config;
  }

 protected:
  utility::EcmpSetupTargetedPorts6 ecmpHelper() const {
    return utility::EcmpSetupTargetedPorts6(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
  }
  PortDescriptor kPort() const {
    return PortDescriptor(getTestPortId(0));
  }
  PortID getTestPortId(int idx = 0) const {
    if (FLAGS_hyper_port) {
      return masterLogicalHyperPortIds()[idx];
    }
    return masterLogicalInterfacePortIds()[idx];
  }

  std::unique_ptr<TxPacket> makePacket(
      bool tcp,
      const folly::IPAddressV6& dstIp,
      std::optional<std::vector<uint8_t>> payload =
          std::optional<std::vector<uint8_t>>()) {
    return tcp ? utility::makeTCPTxPacket(
                     getSw(),
                     getVlanIDForTx(),
                     utility::kLocalCpuMac(),
                     utility::kLocalCpuMac(),
                     kSrcIp(),
                     dstIp,
                     8000, // l4 src port
                     8001, // l4 dst port
                     0x24 << 2, // dscp
                     255, // hopLimit
                     payload)
               : utility::makeUDPTxPacket(
                     getSw(),
                     getVlanIDForTx(),
                     utility::kLocalCpuMac(),
                     utility::kLocalCpuMac(),
                     kSrcIp(),
                     dstIp,
                     8000, // l4 src port
                     8001, // l4 dst port
                     0x24 << 2, // dscp
                     255, // hopLimit
                     payload);
  }

  void sendPacketAndVerify(
      std::unique_ptr<TxPacket> txPacket,
      std::optional<PortID> frontPanelPort) {
    XLOG(DBG5) << "\n"
               << folly::hexDump(
                      txPacket->buf()->data(), txPacket->buf()->length());
    auto nhopMac = ecmpHelper().nhop(kPort()).mac;
    auto switchType = getSw()->getSwitchInfoTable().l3SwitchType();

    auto ethFrame =
        (switchType == cfg::SwitchType::VOQ || FLAGS_rx_vlan_untagged_packets)
        ? utility::makeEthFrame(*txPacket, nhopMac)
        : utility::makeEthFrame(
              *txPacket,
              nhopMac,
              getProgrammedState()
                  ->getPorts()
                  ->getNode(kPort().phyPortID())
                  ->getIngressVlan());

    utility::SwSwitchPacketSnooper snooper(
        getSw(), "snoop", std::nullopt, ethFrame);
    if (frontPanelPort.has_value()) {
      getSw()->sendPacketOutOfPortAsync(std::move(txPacket), *frontPanelPort);
    } else {
      sendPacketSwitchedAsync(std::move(txPacket));
    }
    WITH_RETRIES({
      auto frameRx = snooper.waitForPacket(1);
      EXPECT_EVENTUALLY_TRUE(frameRx.has_value());
    });
  }

 private:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::L3_FORWARDING};
  }
};
TEST_F(AgentDeepPacketInspectionTest, l3ForwardedPkt) {
  auto setup = [this]() {
    applyNewState([this](const std::shared_ptr<SwitchState>& in) {
      return ecmpHelper().resolveNextHops(in, {kPort()});
    });
  };
  auto verify = [this]() {
    std::optional<PortID> frontPanelPort = getTestPortId(1);
    learnL2EntryIfPending(utility::kLocalCpuMac(), *frontPanelPort);
    for (bool isTcp : {true, false}) {
      for (bool isFrontPanel : {true, false}) {
        std::optional<PortID> outOfPort =
            isFrontPanel ? frontPanelPort : std::nullopt;
        auto txPacket = makePacket(isTcp, ecmpHelper().ip(kPort()));
        sendPacketAndVerify(std::move(txPacket), outOfPort);
      }
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}
} // namespace facebook::fboss
