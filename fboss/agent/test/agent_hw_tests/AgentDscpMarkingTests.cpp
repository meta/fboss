/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/packet/EthFrame.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/utils/AgentTestAddressConstants.h"
#include "fboss/agent/test/utils/AgentTestEcmpConstants.h"

#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/DscpMarkingUtils.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"

#include "fboss/agent/test/utils/QosTestUtils.h"
#include "fboss/agent/types.h"
#include "fboss/lib/CommonUtils.h"

#include <folly/IPAddress.h>
#include <gtest/gtest.h>

namespace facebook::fboss {

class AgentDscpMarkingTest : public AgentHwTest {
 protected:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::DSCP_REMARKING};
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);
    auto l3Asics = ensemble.getL3Asics();
    auto asic = checkSameAndGetAsicForTesting(l3Asics);
    utility::addOlympicQosMaps(cfg, l3Asics);
    // drop packets are match to avoid packets matching multiple times in L3
    // loop case
    utility::addDscpCounterAcl(asic, &cfg, cfg::AclActionType::DENY);
    utility::addDscpMarkingAcls(asic, &cfg, ensemble.isSai());
    return cfg;
  }

  void verifyDscpReclassification() {
    XLOG(DBG2) << "verify DSCP reclassification Acls";
    learnSrcMacOnEcmpTxPortIfPending(kDefaultEcmpWidth);
    auto beforeAclInOutPkts =
        utility::getAclInOutPackets(getSw(), utility::kCounterName());
    sendPacket(
        utility::kNcnfDscp(),
        true /* frontPanel */,
        IP_PROTO::IP_PROTO_UDP,
        std::nullopt,
        std::nullopt);
    sendPacket(
        utility::kNcnfDscp(),
        true /* frontPanel */,
        IP_PROTO::IP_PROTO_TCP,
        std::nullopt,
        std::nullopt);
    int64_t afterAclInOutPkts = 0;
    WITH_RETRIES_N(60, {
      afterAclInOutPkts =
          utility::getAclInOutPackets(getSw(), utility::kCounterName());

      // See detailed comment block at the beginning of this function
      EXPECT_EVENTUALLY_EQ(afterAclInOutPkts - beforeAclInOutPkts, 2);
    });
  }

  void verifyDscpMarking() {
    /*
     * ACL1:: Matcher: ICP DSCP. Action: Increment stat.
     * ACL2:: Matcher: L4SrcPort/L4DstPort. Action: SET_DSCP, SET_TC.
     * ACL3:: Matcher: NCNF DSCP, SrcPort. Action: SET_DSCP.
     *
     * ACL1 precedes ACL2.
     * ACL2 mimics prod ACL, ACL1 is used only for test verification.
     * ACL3 mimics prod ACL, we use ACL1 to verify if prod ACL is hit.
     *
     * Inject a pkt with dscp = 0, l4SrcPort matching ACL2:
     *   - The packet will match ACL2, thus SET_DSCP, SET_TC.
     *   - Verify ICP queue counters (verifies SET_TC action).
     *   - Packet egress via front panel port which is in loopback mode.
     *   - Thus, packet gets looped back.
     *   - Verify ACL1 counter (verifies SET_DSCP action).
     *
     * Inject a pkt with dscp = ICP DSCP, l4SrcPort matching ACL2:
     *   - The packet will match ACL1, thus counter incremented.
     *   - Packet dropped after hitting ACL1
     *
     * Inject 2 packets(1 TCP and 1 UDP) with dscp = NCNF DSCP and srcPort
     * matching front panel port
     *   - The packet bypasses to front panel port and loops back as if
     * ingressing on that port.
     *   - The packet will match ACL3, thus SET_DSCP to ICP DSCP. This again
     * goes out the port.
     *   - On the second loop, the packet will match ACL1, thus counter
     * incremented.
     */

    auto setup = [=, this]() {
      utility::EcmpSetupAnyNPorts6 ecmpHelper(
          getProgrammedState(),
          getSw()->needL2EntryForNeighbor(),
          getMacForFirstInterfaceWithPortsForTesting(getProgrammedState()),
          RouterID(0));
      resolveNeighborAndProgramRoutes(ecmpHelper, kDefaultEcmpWidth);
      // Add the DSCP remarking ACLs
      auto newCfg{initialConfig(*getAgentEnsemble())};
      auto l3Asics = getAgentEnsemble()->getL3Asics();
      auto asic = checkSameAndGetAsicForTesting(l3Asics);
      utility::addDscpReclassificationAcls(
          asic, &newCfg, ecmpHelper.ecmpPortDescriptorAt(0).phyPortID());
      applyNewConfig(newCfg);
    };

    auto verify = [=, this]() {
      utility::EcmpSetupAnyNPorts6 ecmpHelper(
          getProgrammedState(),
          getSw()->needL2EntryForNeighbor(),
          getMacForFirstInterfaceWithPortsForTesting(getProgrammedState()),
          RouterID(0));
      auto portId = ecmpHelper.ecmpPortDescriptorAt(0).phyPortID();
      auto portStatsBefore = getLatestPortStats(portId);

      for (bool frontPanel : {false, true}) {
        XLOG(DBG2) << "verify send packets "
                   << (frontPanel ? "out of port" : "switched");
        if (frontPanel) {
          learnSrcMacOnEcmpTxPortIfPending(kDefaultEcmpWidth);
        }
        auto beforeAclInOutPkts =
            utility::getAclInOutPackets(getSw(), utility::kCounterName());

        sendAllPackets(
            0 /* No Dscp */,
            frontPanel,
            IP_PROTO::IP_PROTO_UDP,
            utility::kUdpPorts());
        sendAllPackets(
            0 /* No Dscp */,
            frontPanel,
            IP_PROTO::IP_PROTO_TCP,
            utility::kTcpPorts());

        int64_t afterAclInOutPkts = 0;
        WITH_RETRIES_N(60, {
          afterAclInOutPkts =
              utility::getAclInOutPackets(getSw(), utility::kCounterName());

          // See detailed comment block at the beginning of this function
          EXPECT_EVENTUALLY_EQ(
              afterAclInOutPkts - beforeAclInOutPkts,
              (1 /* ACL hit once */ * 2 /* l4SrcPort, l4DstPort */ *
               utility::kUdpPorts().size()) +
                  (1 /* ACL hit once */ * 2 /* l4SrcPort, l4DstPort */ *
                   utility::kTcpPorts().size()));
        });
        utility::verifyQueueHit(
            portStatsBefore, utility::kOlympicICPQueueId, getSw(), portId);

        sendAllPackets(
            utility::kIcpDscp(),
            frontPanel,
            IP_PROTO::IP_PROTO_UDP,
            utility::kUdpPorts());
        sendAllPackets(
            utility::kIcpDscp(),
            frontPanel,
            IP_PROTO::IP_PROTO_TCP,
            utility::kTcpPorts());

        WITH_RETRIES_N(60, {
          auto afterAclInOutPkts2 =
              utility::getAclInOutPackets(getSw(), utility::kCounterName());

          // See detailed comment block at the beginning of this function
          EXPECT_EVENTUALLY_EQ(
              afterAclInOutPkts2 - afterAclInOutPkts,
              (1 /* ACL hit once */ * 2 /* l4SrcPort, l4DstPort */ *
               utility::kUdpPorts().size()) +
                  (1 /* ACL hit once */ * 2 /* l4SrcPort, l4DstPort */ *
                   utility::kTcpPorts().size()));
        });
      }
      verifyDscpReclassification();
    };

    verifyAcrossWarmBoots(setup, verify);
  }

 private:
  void sendAllPackets(
      uint8_t dscp,
      bool frontPanel,
      IP_PROTO proto,
      const std::vector<uint32_t>& ports) {
    for (auto port : ports) {
      sendPacket(
          dscp,
          frontPanel,
          proto,
          port /* l4SrcPort */,
          std::nullopt /* l4DstPort */);
      sendPacket(
          dscp,
          frontPanel,
          proto,
          std::nullopt /* l4SrcPort */,
          port /* l4DstPort */);
    }
  }

  void sendPacket(
      uint8_t dscp,
      bool frontPanel,
      IP_PROTO proto,
      std::optional<uint16_t> l4SrcPort,
      std::optional<uint16_t> l4DstPort) {
    auto vlanId = getVlanIDForTx();
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);

    std::unique_ptr<facebook::fboss::TxPacket> txPacket;
    if (proto == IP_PROTO::IP_PROTO_UDP) {
      txPacket = utility::makeUDPTxPacket(
          getSw(),
          vlanId,
          srcMac, // src mac
          intfMac, // dst mac
          folly::IPAddressV6(kTestSrcIpV6), // src ip
          folly::IPAddressV6(kTestDstIpV6), // dst ip
          l4SrcPort.has_value() ? l4SrcPort.value() : kTestSrcPort,
          l4DstPort.has_value() ? l4DstPort.value() : kTestDstPort,
          dscp << 2, // shifted by 2 bits for ECN
          255 // ttl
      );
    } else if (proto == IP_PROTO::IP_PROTO_TCP) {
      txPacket = utility::makeTCPTxPacket(
          getSw(),
          vlanId,
          srcMac, // src mac
          intfMac, // dst mac
          folly::IPAddressV6(kTestSrcIpV6), // src ip
          folly::IPAddressV6(kTestDstIpV6), // dst ip
          l4SrcPort.has_value() ? l4SrcPort.value() : kTestSrcPort,
          l4DstPort.has_value() ? l4DstPort.value() : kTestDstPort,
          dscp << 2, // shifted by 2 bits for ECN
          255 // ttl
      );
    } else {
      CHECK(false);
    }

    // port is in LB mode, so it will egress and immediately loop back.
    // Since it is not re-written, it should hit the pipeline as if it
    // ingressed on the port, and be properly queued.
    if (frontPanel) {
      utility::EcmpSetupAnyNPorts6 ecmpHelper(
          getProgrammedState(),
          getSw()->needL2EntryForNeighbor(),
          getMacForFirstInterfaceWithPortsForTesting(getProgrammedState()),
          RouterID(0));
      auto outPort =
          ecmpHelper.ecmpPortDescriptorAt(kDefaultEcmpWidth).phyPortID();
      getSw()->sendPacketOutOfPortAsync(std::move(txPacket), outPort);
    } else {
      sendPacketSwitchedAsync(std::move(txPacket));
    }
  }
};

// Verify that the DSCP unmarked traffic to specific L4 src/dst ports that
// arrives on a forntl panel/cpu port is DSCP marked correctly as well as
// egresses via the right QoS queue.
TEST_F(AgentDscpMarkingTest, VerifyDscpMarking) {
  verifyDscpMarking();
}

} // namespace facebook::fboss
