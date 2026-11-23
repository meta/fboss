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
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AgentTestAddressConstants.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/PortStatsTestUtils.h"
#include "fboss/lib/CommonUtils.h"

#include <folly/IPAddress.h>

namespace facebook::fboss {

// This test simply checks if the loop back mode is working correctly.
// Some of our hw tests rely on the looped back packets (e.g. Ecn and Wred)
// When srcMac == dstMac, brcm-sai could drop the packets, thus causing
// confusions in these tests (whether it's implementation issue or packets being
// dropped).
class AgentLoopBackTest : public AgentHwTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::CPU_RX_TX};
  }

 private:
  PortID portIdToTest() {
    if (FLAGS_hyper_port) {
      return masterLogicalHyperPortIds()[0];
    }
    return masterLogicalInterfacePortIds()[0];
  }

  void sendPkt(
      bool frontPanel,
      uint8_t ttl,
      bool srcEqualDstMac,
      bool srcEqualDstIp = false) {
    auto vlanId = getVlanIDForTx();
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    auto dstIp = folly::IPAddressV6(kTestDstIpV6);
    auto txPacket = utility::makeUDPTxPacket(
        getSw(),
        vlanId,
        srcEqualDstMac ? intfMac : srcMac,
        intfMac,
        srcEqualDstIp ? dstIp : folly::IPAddressV6(kTestSrcIpV6),
        dstIp,
        kTestSrcPort,
        kTestDstPort,
        0,
        ttl);

    if (frontPanel) {
      getSw()->sendPacketOutOfPortAsync(
          std::move(txPacket), this->portIdToTest());
    } else {
      sendPacketSwitchedAsync(std::move(txPacket));
    }
  }

  static inline constexpr auto pktTtl = 255;

 protected:
  void runTest(bool srcEqualDstMac, bool srcEqualDstIp = false) {
    auto setup = [=, this]() {
      auto kEcmpWidthForTest = 1;
      utility::EcmpSetupAnyNPorts6 ecmpHelper6{
          getProgrammedState(),
          getSw()->needL2EntryForNeighbor(),
          getMacForFirstInterfaceWithPortsForTesting(getProgrammedState())};
      resolveNeighborAndProgramRoutes(ecmpHelper6, kEcmpWidthForTest);
    };
    auto verify = [=, this]() {
      const auto switchType =
          checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
              ->getSwitchType();
      auto intfMac =
          getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
      learnL2EntryIfPending(
          srcEqualDstMac
              ? intfMac
              : utility::MacAddressGenerator().get(intfMac.u64HBO() + 1),
          portIdToTest(),
          [&]() { sendPkt(true, 1, srcEqualDstMac, srcEqualDstIp); });
      for (auto frontPanel : {true, false}) {
        auto beforePortStats = getLatestPortStats(this->portIdToTest());
        sendPkt(frontPanel, pktTtl, srcEqualDstMac, srcEqualDstIp);
        WITH_RETRIES({
          auto afterPortStats = getLatestPortStats(this->portIdToTest());
          // For packets going out to front panel, they would not go through the
          // routing logic the very first time (but directly looped back).
          // Therefore, the counter would plus one compared to the cpu port.
          // For VoQ switches, TTL 0 packets don't get dropped at Egress and
          // hence it will be forwarded at Egress and looped back in and then
          // get dropped at Ingress. So, there would be one extra packet for VoQ
          // switches.
          int expectedPkts;
          if (frontPanel && switchType == cfg::SwitchType::VOQ) {
            // VoQ switch front panel
            expectedPkts = pktTtl + 1;
          } else if (frontPanel || switchType == cfg::SwitchType::VOQ) {
            // VoQ switch CPU port and Non-VoQ switch front panel
            expectedPkts = pktTtl;
          } else {
            // Non-VoQ switch CPU port
            expectedPkts = pktTtl - 1;
          }
          EXPECT_EVENTUALLY_EQ(
              utility::getPortOutPkts(afterPortStats) -
                  utility::getPortOutPkts(beforePortStats),
              expectedPkts);
        });
      }
    };

    verifyAcrossWarmBoots(setup, verify);
  }
};

TEST_F(AgentLoopBackTest, VerifyLoopBack) {
  runTest(false /* srcEqualDstMac */);
}

TEST_F(AgentLoopBackTest, VerifyLoopBackSrcEqualDstMac) {
  runTest(true /* srcEqualDstMac */);
}

// T269303598: Verify L3 hairpin forwarding when SIP==DIP.
// Requires SAI_KEY_NOT_DROP_SIP_DIP_EQUAL to be enabled.
TEST_F(AgentLoopBackTest, VerifyLoopBackSrcEqualDstIp) {
  runTest(false /* srcEqualDstMac */, true /* srcEqualDstIp */);
}

} // namespace facebook::fboss
