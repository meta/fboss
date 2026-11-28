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
#include "fboss/agent/MacTableUtils.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/PacketTestUtils.h"
#include "fboss/agent/test/utils/QueuePerHostTestUtils.h"
#include "fboss/lib/CommonUtils.h"

namespace facebook::fboss {

class AgentQueuePerHostL2Test : public AgentHwTest {
 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto switchId = getCurrentSwitchIdForTesting();
    auto asic = ensemble.getSw()->getHwAsicTable()->getHwAsic(switchId);
    auto cfg = utility::oneL3IntfTwoPortConfig(
        ensemble.getSw()->getPlatformMapping(),
        asic,
        ensemble.masterLogicalInterfacePortIds()[0],
        ensemble.masterLogicalInterfacePortIds()[1],
        ensemble.getSw()->getPlatformSupportsAddRemovePort(),
        asic->desiredLoopbackModes(),
        ensemble.getSw()->getPlatformType());
    cfg.switchSettings()->l2LearningMode() = cfg::L2LearningMode::SOFTWARE;
    utility::addQueuePerHostQueueConfig(&cfg);
    utility::addQueuePerHostAcls(&cfg, ensemble.isSai());
    return cfg;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::QUEUE_PER_HOST};
  }

  void verifyHelper(bool useFrontPanel) {
    auto ttlAclName = utility::getQueuePerHostTtlAclName();
    auto ttlCounterName = utility::getQueuePerHostTtlCounterName();

    auto statBefore =
        utility::waitForAndGetAclInOutPackets(getSw(), ttlCounterName);

    std::map<int, int64_t> beforeQueueOutPkts;
    for (const auto& queueId : utility::kQueuePerhostQueueIds()) {
      beforeQueueOutPkts[queueId] =
          folly::copy(this->getLatestPortStats(this->masterLogicalPortIds()[0])
                          .queueOutPackets_()
                          .value())
              .at(queueId);
    }

    auto txPacket = createL3Pkt(64 /* ttl < 128 */, useFrontPanel);
    auto txPacket2 = createL3Pkt(128 /* ttl >= 128 */, useFrontPanel);

    if (useFrontPanel) {
      XLOG(DBG2) << "verify send packets out of port";
      getSw()->sendPacketOutOfPortAsync(
          std::move(txPacket), PortID(masterLogicalPortIds()[1]));
      getSw()->sendPacketOutOfPortAsync(
          std::move(txPacket2), PortID(masterLogicalPortIds()[1]));
    } else {
      XLOG(DBG2) << "verify send packets switched";
      sendPacketSwitchedAsync(std::move(txPacket));
      sendPacketSwitchedAsync(std::move(txPacket2));
    }

    /*
     *  CPU originated packets:
     *     - Hits ACL (queue2Cnt = 1), egress through queue 2 of port0.
     *     - port0 is in loopback mode, so the packet gets looped back.
     *     - the looped back packet hits ACL (queue2Cnt = 2).
     *     - the packet gets dropped at egress as srcPort == dstPort, thus
     *       breaking the loop.
     *
     *  Front panel packets (injected with pipeline bypass):
     *     - Egress out of port1 queue0 (pipeline bypass).
     *     - port1 is in loopback mode, so the packet gets looped back.
     *     - Rest of the workflow is same as above when CPU originated packet
     *       gets injected for switching.
     *
     * Note: these are bridged packets, thus the looped back packets carry same
     * MAC as before and thus hit ACL.
     *
     * On some platforms, split horizon check is after ACL matching.
     */
    WITH_RETRIES({
      std::map<int, int64_t> afterQueueOutPkts;
      for (const auto& queueId : utility::kQueuePerhostQueueIds()) {
        afterQueueOutPkts[queueId] =
            folly::copy(
                this->getLatestPortStats(this->masterLogicalPortIds()[0])
                    .queueOutPackets_()
                    .value())
                .at(queueId);
      }

      for (auto [qid, beforePkts] : beforeQueueOutPkts) {
        auto pktsOnQueue = afterQueueOutPkts[qid] - beforePkts;

        XLOG(DBG0) << "queueId: " << qid << " pktsOnQueue: " << pktsOnQueue;

        if (qid == kQueueID()) {
          /*
           * On some platforms, split horizon check is after ACL matching.
           * Thus, the counter get increment one additional time for the looped
           * back packet.
           */
          if (checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
                      ->getAsicType() == cfg::AsicType::ASIC_TYPE_EBRO ||
              checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
                      ->getAsicType() == cfg::AsicType::ASIC_TYPE_P200 ||
              checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
                      ->getAsicType() == cfg::AsicType::ASIC_TYPE_YUBA ||
              checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
                      ->getAsicType() == cfg::AsicType::ASIC_TYPE_G202X) {
            /* 1 pkt each for ttl < 128 and ttl >= 128 */
            EXPECT_EVENTUALLY_EQ(pktsOnQueue, 4);
          } else {
            /* 1 pkt each for ttl < 128 and ttl >= 128 */
            EXPECT_EVENTUALLY_EQ(pktsOnQueue, 2);
          }
        } else {
          EXPECT_EVENTUALLY_EQ(pktsOnQueue, 0);
        }
      }

      auto aclStatsMatch = [&] {
        auto statAfter =
            utility::getAclInOutPackets(this->getSw(), ttlCounterName);

        /*
         * counts ttl >= 128 packet only
         * but L2 traffic (so TTL is not decremented, and thus looped back
         * packet also has ttl >= 128, thus the count is 2.
         */
        return statAfter - statBefore == 2;
      };
      EXPECT_EVENTUALLY_TRUE(aclStatsMatch());
    });
  }

  std::unique_ptr<facebook::fboss::TxPacket> createL3Pkt(
      uint8_t ttl,
      bool useFrontPanel) {
    auto srcMac = useFrontPanel ? kMac1() : utility::kLocalCpuMac();
    return utility::makeUDPTxPacket(
        getSw(),
        VlanID(*initialConfig(*getAgentEnsemble()).vlanPorts()[0].vlanID()),
        srcMac,
        kMac0(), // dstMac: packet to port0 (from CPU/port1)
        folly::IPAddressV6("1::1"), // srcIPv6
        folly::IPAddressV6("1::10"), // dstIPv6
        8000, // l4 src port
        8001, // l4 dst port
        48 << 2, // DSCP
        ttl);
  }

  PortDescriptor physPortDescr() const {
    return PortDescriptor(PortID(masterLogicalPortIds()[0]));
  }

  VlanID kVlanID() {
    return VlanID(*initialConfig(*getAgentEnsemble()).vlanPorts()[0].vlanID());
  }

  int kQueueID() {
    return 2;
  }

  static MacAddress kMac0() {
    return MacAddress("02:00:00:00:00:05");
  }

  static MacAddress kMac1() {
    return MacAddress("02:00:00:00:00:06");
  }

  static MacAddress kMac2() {
    return MacAddress("02:00:00:00:00:07");
  }

  static MacAddress kMac3() {
    return MacAddress("02:00:00:00:00:02");
  }

  PortDescriptor kPhysPortDescr0() const {
    return PortDescriptor(PortID(masterLogicalPortIds()[0]));
  }

  PortDescriptor kPhysPortDescr1() const {
    return PortDescriptor(PortID(masterLogicalPortIds()[1]));
  }

  cfg::AclLookupClass kClassID1() {
    return cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1;
  }

  cfg::AclLookupClass kClassID0() {
    return cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2;
  }

  cfg::AclLookupClass kClassID2() {
    return cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3;
  }

  cfg::AclLookupClass kClassID3() {
    return cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_4;
  }

  void addOrUpdateMacEntry(
      folly::MacAddress macAddr,
      PortDescriptor portDescr,
      std::optional<cfg::AclLookupClass> classID,
      MacEntryType type) {
    applyNewState(
        [&](const std::shared_ptr<SwitchState>& in) {
          auto newState = in->clone();
          auto vlan = newState->getVlans()->getNodeIf(kVlanID()).get();
          auto macTable = vlan->getMacTable().get();
          macTable = macTable->modify(&vlan, &newState);
          if (macTable->getMacIf(macAddr)) {
            macTable->updateEntry(macAddr, portDescr, classID, type);
          } else {
            auto macEntry =
                std::make_shared<MacEntry>(macAddr, portDescr, classID, type);
            macTable->addEntry(macEntry);
          }
          return newState;
        },
        "addOrUpdateMacEntry");
  }
};

TEST_F(AgentQueuePerHostL2Test, VerifyHostToQueueMappingClassID) {
  auto setup = [this]() {
    /*
     * The first two mac entries exist so that the LookupClassUpdater
     * selects the correct class id for the test mac - kMac1()
     * TODO - reorganize the test to use packet injection to learn
     * mac similar to production flow.
     */
    addOrUpdateMacEntry(
        kMac2(), kPhysPortDescr0(), kClassID2(), MacEntryType::STATIC_ENTRY);
    addOrUpdateMacEntry(
        kMac3(), kPhysPortDescr0(), kClassID3(), MacEntryType::STATIC_ENTRY);
    addOrUpdateMacEntry(
        kMac0(), kPhysPortDescr0(), kClassID0(), MacEntryType::STATIC_ENTRY);
    addOrUpdateMacEntry(
        kMac1(), kPhysPortDescr1(), kClassID1(), MacEntryType::STATIC_ENTRY);
  };

  auto verify = [this]() {
    verifyHelper(false /* cpu port */);
    verifyHelper(true /* front panel port */);
  };

  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
