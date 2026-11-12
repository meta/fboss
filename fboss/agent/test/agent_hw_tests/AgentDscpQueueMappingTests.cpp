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

#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/ResourceLibUtil.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/AgentTestAddressConstants.h"
#include "fboss/agent/test/utils/AgentTestEcmpConstants.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"
#include "fboss/agent/test/utils/PacketTestUtils.h"
#include "fboss/agent/test/utils/TrafficPolicyTestUtils.h"
#include "fboss/lib/CommonUtils.h"

DECLARE_bool(enable_acl_table_group);

namespace facebook::fboss {

class AgentDscpQueueMappingTestBase : public AgentHwTest {
 protected:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::L3_QOS};
  }

  void setupHelper() {
    utility::EcmpSetupAnyNPorts6 ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    resolveNeighborAndProgramRoutes(ecmpHelper, kDefaultEcmpWidth);
  }

  void sendPacket(bool frontPanel, int16_t dscp, uint8_t ttl = 64) {
    auto vlanId = getVlanIDForTx();
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    auto txPacket = utility::makeUDPTxPacket(
        getSw(),
        vlanId,
        srcMac, // src mac
        intfMac, // dst mac
        folly::IPAddressV6("2620:0:1cfe:face:b00c::1"),
        kDstIP(),
        kTestSrcPort, // l4 src port
        kTestDstPort, // l4 dst port
        dscp << 2, // Trailing 2 bits are for ECN
        ttl);

    // port is in LB mode, so it will egress and immediately loop back.
    // Since it is not re-written, it should hit the pipeline as if it
    // ingressed on the port, and be properly queued.
    if (frontPanel) {
      utility::EcmpSetupAnyNPorts6 ecmpHelper(
          getProgrammedState(), getSw()->needL2EntryForNeighbor());
      auto outPort =
          ecmpHelper.ecmpPortDescriptorAt(kDefaultEcmpWidth).phyPortID();
      getSw()->sendPacketOutOfPortAsync(std::move(txPacket), outPort);
    } else {
      sendPacketSwitchedAsync(std::move(txPacket));
    }
  }

  folly::IPAddressV6 kDstIP() {
    utility::EcmpSetupAnyNPorts6 ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    return ecmpHelper.ip(0);
  }

  folly::MacAddress kMacAddress() {
    return folly::MacAddress("0:2:3:4:5:10");
  }

  int16_t kDscp() const {
    return 48;
  }

  int kQueueId() const {
    return 7;
  }

  std::string kCounterName() const {
    return folly::to<std::string>("dscp", kQueueId(), "_counter");
  }

  int kQueueIdQosMap() const {
    return 7;
  }

  int kQueueIdAcl() const {
    return 2;
  }

  PortID portIdToTest() {
    if (FLAGS_hyper_port) {
      return masterLogicalHyperPortIds()[0];
    }
    return masterLogicalInterfacePortIds()[0];
  }

  const VlanID kVlanID{utility::kBaseVlanId};
  const InterfaceID kIntfID{utility::kBaseVlanId};
};

class AgentDscpQueueMappingTest : public AgentDscpQueueMappingTestBase {
 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);
    // QosMap
    auto l3Asics = ensemble.getL3Asics();
    utility::addOlympicV2QosMaps(cfg, l3Asics);
    return cfg;
  }

  void dscpMappingSetupHelper(bool newDscpSchema) {
    auto newCfg{initialConfig(*getAgentEnsemble())};
    auto l3Asics = getAgentEnsemble()->getL3Asics();
    /* update Dscp 46 to gold queue*/
    utility::addOlympicV2QosMaps(newCfg, l3Asics, newDscpSchema);
    applyNewConfig(newCfg);
  }

  void dscpMappingVerifyHelper(int kQueueId, int16_t kDscp) {
    for (bool frontPanel : {false, true}) {
      if (frontPanel) {
        learnSrcMacOnEcmpTxPortIfPending(kDefaultEcmpWidth);
      }
      auto beforeQueueOutPkts =
          folly::copy(getLatestPortStats(this->portIdToTest())
                          .queueOutPackets_()
                          .value())
              .at(kQueueId);

      sendPacket(frontPanel, kDscp);

      WITH_RETRIES({
        auto afterQueueOutPkts =
            folly::copy(getLatestPortStats(this->portIdToTest())
                            .queueOutPackets_()
                            .value())
                .at(kQueueId);

        XLOG(DBG2) << "verify send packets "
                   << (frontPanel ? "out of port" : "switched")
                   << " beforeQueueOutPkts = " << beforeQueueOutPkts
                   << " afterQueueOutPkti = " << afterQueueOutPkts;

        /*
         * Packet from CPU / looped back from front panel port (with
         * pipeline bypass), hits ACL and increments counter (queue2Count
         * = 1). On some platforms, looped back packets for unknown MACs
         * are flooded and counted on queue *before* the split horizon
         * check. This packet will match the DSCP based ACL and thus
         * increment the queue2Count = 2.
         */
        EXPECT_EVENTUALLY_GE(afterQueueOutPkts - beforeQueueOutPkts, 1);
      });
    }
  }

  void verifyDscpQueueMappingHelper(
      int kQueueId,
      int16_t kDscp,
      bool dscpTcChangePostWarmboot,
      int kQueueIdPostWarmboot) {
    auto setup = [this]() { setupHelper(); };

    auto verify = [this, kQueueId, kDscp]() {
      dscpMappingVerifyHelper(kQueueId, kDscp);
    };

    if (dscpTcChangePostWarmboot) {
      auto setupPostWarmboot = [this]() {
        dscpMappingSetupHelper(true /*newDscpSchema*/);
      };
      auto verifyPostWarmboot = [this, kQueueIdPostWarmboot, kDscp]() {
        dscpMappingVerifyHelper(kQueueIdPostWarmboot, kDscp);
      };
      verifyAcrossWarmBoots(
          setup, verify, setupPostWarmboot, verifyPostWarmboot);
    } else {
      verifyAcrossWarmBoots(setup, verify);
    }
  }

  void verifyDscpQueueMappingCanaryOff(
      int kQueueId,
      int16_t kDscp,
      int kQueueIdPostWarmboot) {
    auto setup = [this]() {
      dscpMappingSetupHelper(true /*newDscpSchema*/);
      setupHelper();
    };

    auto verify = [this, kQueueId, kDscp]() {
      dscpMappingVerifyHelper(kQueueId, kDscp);
    };
    auto setupPostWarmboot = [this]() {
      dscpMappingSetupHelper(false /*newDscpSchema*/);
    };
    auto verifyPostWarmboot = [this, kQueueIdPostWarmboot, kDscp]() {
      dscpMappingVerifyHelper(kQueueIdPostWarmboot, kDscp);
    };
    verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
  }
};

class AgentAclAndDscpQueueMappingTest : public AgentDscpQueueMappingTestBase {
 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);
    // QosMap
    utility::addOlympicQosMaps(cfg, ensemble.getL3Asics());

    // ACL
    auto l3Asics = ensemble.getL3Asics();
    auto asic = checkSameAndGetAsicForTesting(l3Asics);
    // acl0 qualifies on TTL + IPv6 etherType only; on Q4D/J4 those shared
    // qualifiers would route it to the IPv4 default table, so target the IPv6
    // table explicitly there.
    std::optional<std::string> aclTableName;
    if (FLAGS_enable_acl_table_group &&
        (asic->getAsicType() == cfg::AsicType::ASIC_TYPE_QUMRAN4D ||
         asic->getAsicType() == cfg::AsicType::ASIC_TYPE_JERICHO4)) {
      aclTableName = utility::kIpv6AclTable();
    }
    auto* acl = utility::addAcl_DEPRECATED(
        &cfg, "acl0", cfg::AclActionType::PERMIT, aclTableName);
    if (asic->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1) {
      // TU1 does not support the TTL ACL qualifier; qualify on dstIp instead.
      // ::/0 matches any IPv6 destination, so the test packet still hits acl0.
      acl->dstIp() = "::/0";
    } else {
      cfg::Ttl ttl; // Match packets with hop limit > 127
      std::tie(*ttl.value(), *ttl.mask()) = std::make_tuple(0x80, 0x80);
      acl->ttl() = ttl;
    }
    utility::addEtherTypeToAcl(asic, acl, cfg::EtherType::IPv6);
    utility::addAclStat(
        &cfg,
        "acl0",
        kCounterName(),
        utility::getAclCounterTypes(ensemble.getL3Asics()));
    return cfg;
  }

  void verifyAclAndQosMapHelper() {
    auto setup = [this]() { setupHelper(); };

    auto verify = [this]() {
      for (bool frontPanel : {false, true}) {
        XLOG(DBG2) << "verify send packets "
                   << (frontPanel ? "out of port" : "switched");
        if (frontPanel) {
          learnSrcMacOnEcmpTxPortIfPending(kDefaultEcmpWidth);
        }
        auto beforeQueueOutPkts =
            folly::copy(getLatestPortStats(this->portIdToTest())
                            .queueOutPackets_()
                            .value())
                .at(kQueueId());
        auto beforeAclInOutPkts =
            utility::getAclInOutPackets(getSw(), kCounterName());

        XLOG(DBG2) << "beforeQueueOutPkts = " << beforeQueueOutPkts
                   << " beforeAclInOutPkts = " << beforeAclInOutPkts;

        sendPacket(frontPanel, kDscp(), 255 /* ttl, > 127 to match ACL */);

        WITH_RETRIES({
          auto afterQueueOutPkts =
              folly::copy(getLatestPortStats(this->portIdToTest())
                              .queueOutPackets_()
                              .value())
                  .at(kQueueId());
          auto afterAclInOutPkts =
              utility::getAclInOutPackets(getSw(), kCounterName());

          XLOG(DBG2) << "afterQueueOutPkts = " << afterQueueOutPkts
                     << " afterAclInOutPkts = " << afterAclInOutPkts;

          EXPECT_EVENTUALLY_EQ(1, afterQueueOutPkts - beforeQueueOutPkts);
          // On some ASICs looped back pkt hits the ACL before being
          // dropped in the ingress pipeline, hence GE
          EXPECT_EVENTUALLY_GE(afterAclInOutPkts, beforeAclInOutPkts + 1);
          // At most we should get a pkt bump of 2
          EXPECT_EVENTUALLY_LE(afterAclInOutPkts, beforeAclInOutPkts + 2);
        });
      }
    };

    verifyAcrossWarmBoots(setup, verify);
  }
};

class AgentAclConflictAndDscpQueueMappingTest
    : public AgentDscpQueueMappingTestBase {
 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);

    auto l3Asics = ensemble.getL3Asics();
    auto asic = checkSameAndGetAsicForTesting(l3Asics);
    // The QoS map sends packets to queue kQueueIdQosMap() i.e. 7,
    // The ACL sends them to queue kQueueIdAcl() i.e. 2.
    // QosMap
    utility::addOlympicQosMaps(cfg, l3Asics);

    // ACL
    utility::addDscpAclToCfg(asic, &cfg, "acl0", kDscp());
    utility::addTrafficCounter(
        &cfg, kCounterName(), utility::getAclCounterTypes(l3Asics));
    utility::addQueueMatcher(
        &cfg, "acl0", kQueueIdAcl(), ensemble.isSai(), kCounterName());
    return cfg;
  }

  void verifyAclAndQosMapConflictHelper() {
    auto setup = [this]() { setupHelper(); };

    auto verify = [this]() {
      for (bool frontPanel : {false, true}) {
        XLOG(DBG2) << "verify send packets "
                   << (frontPanel ? "out of port" : "switched");
        if (frontPanel) {
          learnSrcMacOnEcmpTxPortIfPending(kDefaultEcmpWidth);
        }
        auto beforeQueueOutPktsAcl =
            folly::copy(getLatestPortStats(this->portIdToTest())
                            .queueOutPackets_()
                            .value())
                .at(kQueueIdAcl());
        auto beforeQueueOutPktsQosMap =
            folly::copy(getLatestPortStats(this->portIdToTest())
                            .queueOutPackets_()
                            .value())
                .at(kQueueIdQosMap());
        auto beforeAclInOutPkts =
            utility::getAclInOutPackets(getSw(), kCounterName());

        XLOG(DBG2) << "beforeQueueOutPktsAcl = " << beforeQueueOutPktsAcl
                   << " beforeQueueOutPktsQosMap = " << beforeQueueOutPktsQosMap
                   << " beforeAclInOutPkts = " << beforeAclInOutPkts;

        sendPacket(frontPanel, kDscp());

        WITH_RETRIES({
          auto afterQueueOutPktsAcl =
              folly::copy(getLatestPortStats(this->portIdToTest())
                              .queueOutPackets_()
                              .value())
                  .at(kQueueIdAcl());
          auto afterQueueOutPktsQosMap =
              folly::copy(getLatestPortStats(this->portIdToTest())
                              .queueOutPackets_()
                              .value())
                  .at(kQueueIdQosMap());

          auto afterAclInOutPkts =
              utility::getAclInOutPackets(getSw(), kCounterName());

          XLOG(DBG2) << "afterQueueOutPktsAcl = " << afterQueueOutPktsAcl
                     << " afterQueueOutPktsQosMap = " << afterQueueOutPktsQosMap
                     << " afterAclInOutPkts = " << afterAclInOutPkts;

          // The ACL overrides the decision of the QoS map
          EXPECT_EVENTUALLY_EQ(
              0, afterQueueOutPktsQosMap - beforeQueueOutPktsQosMap);

          /*
           * Packet from CPU / looped back from front panel port (with
           * pipeline bypass), hits ACL and increments counter
           * (queue2Count = 1). On some platforms, looped back packets for
           * unknown MACs are flooded and counted on queue *before* the
           * split horizon check. This packet will match the DSCP based
           * ACL and thus increment the queue2Count = 2.
           */
          EXPECT_EVENTUALLY_GE(afterQueueOutPktsAcl - beforeQueueOutPktsAcl, 1);

          // On some ASICs looped back pkt hits the ACL before being
          // dropped in the ingress pipeline, hence GE
          EXPECT_EVENTUALLY_GE(afterAclInOutPkts, beforeAclInOutPkts + 1);
          // At most we should get a pkt bump of 2
          EXPECT_EVENTUALLY_LE(afterAclInOutPkts, beforeAclInOutPkts + 2);
        });
      }
    };

    verifyAcrossWarmBoots(setup, verify);
  }
};

// Verify that traffic arriving on front panel/cpu port egresses via queue
// based on DSCP
TEST_F(AgentDscpQueueMappingTest, VerifyDscpQueueMapping) {
  verifyDscpQueueMappingHelper(
      kQueueId(),
      kDscp(),
      false /* dscpTcChangePostWarmboot */,
      kQueueId() /* kQueueIdPostWarmboot */);
}

TEST_F(AgentDscpQueueMappingTest, VerifyDscpQueueMappingChangePostWarmboot) {
  /*
   * Test to verify that changing DSCP TC mapping over warmboot works as
   * intended. Prewarmboot, dscp 46 is mapped to silver traffic. So set dscptc
   * map accordingly and send packet and ensure that its received in queue 2
   * (corresponding to silver). post warmboot, set the dscp mapping to gold and
   * ensure that the packet is received in gold queue (queue 3)
   */
  verifyDscpQueueMappingHelper(
      utility::kOlympicAllSPSilverQueueId,
      utility::kDscpToRemap,
      true /* dscpTcChangePostWarmboot */,
      utility::kOlympicAllSPGoldQueueId);
}

TEST_F(AgentDscpQueueMappingTest, VerifyDscpQueueMappingCanaryOff) {
  /*
   * Same as VerifyDscpQueueMappingChangePostWarmboot but this tests the
   * canary off scenario where dscp 46 is GOLD prewarmboot and changes to
   * SILVER post warmboot (to test rollback)
   */
  verifyDscpQueueMappingCanaryOff(
      utility::kOlympicAllSPGoldQueueId,
      utility::kDscpToRemap,
      utility::kOlympicAllSPSilverQueueId);
}

// Verify that traffic arriving on front panel/cpu port with non-conflicting
// ACL (counter based on ttl) and QoS Map (DSCP to Queue)
TEST_F(AgentAclAndDscpQueueMappingTest, VerifyAclAndQosMap) {
  verifyAclAndQosMapHelper();
}

// Verify that traffic arriving on front panel/cpu port with conflicting ACL
// (DSCP to queue) and QoS Map (DSCP to Queue)
TEST_F(AgentAclConflictAndDscpQueueMappingTest, VerifyAclAndQosMapConflict) {
  verifyAclAndQosMapConflictHelper();
}

} // namespace facebook::fboss
