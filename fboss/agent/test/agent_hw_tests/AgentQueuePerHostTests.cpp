/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/ResourceLibUtil.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/AgentTestEcmpConstants.h"

#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/NeighborTestUtils.h"
#include "fboss/agent/test/utils/PacketTestUtils.h"
#include "fboss/agent/test/utils/QosTestUtils.h"
#include "fboss/agent/test/utils/QueuePerHostTestUtils.h"

namespace facebook::fboss {

class AgentQueuePerHostTest : public AgentHwTest {
  template <typename AddrT>
  using NeighborTableT = std::conditional_t<
      std::is_same_v<AddrT, folly::IPAddressV4>,
      ArpTable,
      NdpTable>;

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
  }

 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);
    utility::addQueuePerHostQueueConfig(&cfg);
    utility::addQueuePerHostAcls(&cfg, ensemble.isSai());
    return cfg;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::QUEUE_PER_HOST};
  }

  template <typename AddrT>
  const std::map<AddrT, std::pair<folly::MacAddress, cfg::AclLookupClass>>&
  getIpToMacAndClassID() {
    // TODO (skhare) Use ResourceGenerator to create this map, where the number
    // of entries equals kQueuePerhostQueueIds()

    if constexpr (std::is_same<AddrT, folly::IPAddressV4>::value) {
      static const std::map<
          folly::IPAddressV4,
          std::pair<folly::MacAddress, cfg::AclLookupClass>>
          ipToMacAndClassID = {
              {folly::IPAddressV4("1.0.0.10"),
               std::make_pair(
                   folly::MacAddress("0:2:3:4:5:10"),
                   cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0)},
              {folly::IPAddressV4("1.0.0.11"),
               std::make_pair(
                   folly::MacAddress("0:2:3:4:5:11"),
                   cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1)},
              {folly::IPAddressV4("1.0.0.12"),
               std::make_pair(
                   folly::MacAddress("0:2:3:4:5:12"),
                   cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2)},
              {folly::IPAddressV4("1.0.0.13"),
               std::make_pair(
                   folly::MacAddress("0:2:3:4:5:13"),
                   cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3)},
              {folly::IPAddressV4("1.0.0.14"),
               std::make_pair(
                   folly::MacAddress("0:2:3:4:5:14"),
                   cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_4)},
          };

      return ipToMacAndClassID;
    } else {
      static const std::map<
          folly::IPAddressV6,
          std::pair<folly::MacAddress, cfg::AclLookupClass>>
          ipToMacAndClassID = {
              {folly::IPAddressV6("1::10"),
               std::make_pair(
                   folly::MacAddress("0:2:3:4:5:10"),
                   cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0)},
              {folly::IPAddressV6("1::11"),
               std::make_pair(
                   folly::MacAddress("0:2:3:4:5:11"),
                   cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1)},
              {folly::IPAddressV6("1::12"),
               std::make_pair(
                   folly::MacAddress("0:2:3:4:5:12"),
                   cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2)},
              {folly::IPAddressV6("1::13"),
               std::make_pair(
                   folly::MacAddress("0:2:3:4:5:13"),
                   cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3)},
              {folly::IPAddressV6("1::14"),
               std::make_pair(
                   folly::MacAddress("0:2:3:4:5:14"),
                   cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_4)},
          };
      return ipToMacAndClassID;
    }
  }

  template <typename AddrT>
  std::shared_ptr<SwitchState> addNeighbors(
      const std::shared_ptr<SwitchState>& inState) {
    auto outState{inState->clone()};

    for (const auto& ipToMacAndClassID : getIpToMacAndClassID<AddrT>()) {
      auto ip = ipToMacAndClassID.first;

      NeighborTableT<AddrT>* neighborTable;
      neighborTable = outState->getInterfaces()
                          ->getNode(kIntfID)
                          ->template getNeighborTable<NeighborTableT<AddrT>>()
                          ->modify(kIntfID, &outState);

      neighborTable->addPendingEntry(ip, kIntfID);
    }

    return outState;
  }

  template <typename AddrT>
  std::shared_ptr<typename NeighborTableT<AddrT>::Entry> getNeighborEntry(
      AddrT ip) {
    return getProgrammedState()
        ->getInterfaces()
        ->getNode(kIntfID)
        ->template getNeighborTable<NeighborTableT<AddrT>>()
        ->getEntryIf(ip);
  }

  template <typename AddrT>
  std::shared_ptr<SwitchState> removeNeighbors(
      const std::shared_ptr<SwitchState>& inState) {
    auto outState{inState->clone()};
    for (const auto& ipToMacAndClassID : getIpToMacAndClassID<AddrT>()) {
      auto ip = ipToMacAndClassID.first;
      NeighborTableT<AddrT>* neighborTable;
      neighborTable = outState->getInterfaces()
                          ->getNode(kIntfID)
                          ->template getNeighborTable<NeighborTableT<AddrT>>()
                          ->modify(kIntfID, &outState);
      neighborTable->removeEntry(ip);
    }
    return outState;
  }

  template <typename AddrT>
  std::shared_ptr<SwitchState> updateNeighbors(
      const std::shared_ptr<SwitchState>& inState,
      bool setClassIDs,
      bool blockNeighbor) {
    auto outState{inState->clone()};
    for (const auto& ipToMacAndClassID : getIpToMacAndClassID<AddrT>()) {
      auto ip = ipToMacAndClassID.first;
      auto macAndClassID = ipToMacAndClassID.second;
      auto neighborMac = macAndClassID.first;
      auto classID = blockNeighbor ? cfg::AclLookupClass::CLASS_DROP
                                   : macAndClassID.second;

      NeighborTableT<AddrT>* neighborTable;
      neighborTable = outState->getInterfaces()
                          ->getNode(kIntfID)
                          ->template getNeighborTable<NeighborTableT<AddrT>>()
                          ->modify(kIntfID, &outState);

      auto existingEntry = neighborTable->getEntryIf(ip);

      if (setClassIDs) {
        neighborTable->updateEntry(
            ip,
            neighborMac,
            PortDescriptor(masterLogicalPortIds()[0]),
            kIntfID,
            NeighborState::REACHABLE,
            classID);

      } else {
        neighborTable->updateEntry(
            ip,
            neighborMac,
            PortDescriptor(masterLogicalPortIds()[0]),
            kIntfID,
            NeighborState::REACHABLE);
      }

      if (getSw()->needL2EntryForNeighbor()) {
        outState = utility::NeighborTestUtils::updateMacEntryForUpdatedNbrEntry(
            outState, kVlanID, existingEntry, neighborTable->getEntryIf(ip));
      }
    }

    return outState;
  }

  template <typename AddrT>
  void verifyNeighborClassId(bool blockNeighbor) {
    WITH_RETRIES({
      auto state = getProgrammedState();
      for (const auto& ipToMacAndClassID : getIpToMacAndClassID<AddrT>()) {
        auto ip = ipToMacAndClassID.first;
        auto macAndClassID = ipToMacAndClassID.second;
        auto classID = blockNeighbor ? cfg::AclLookupClass::CLASS_DROP
                                     : macAndClassID.second;

        std::shared_ptr<NeighborTableT<AddrT>> neighborTable;
        neighborTable =
            state->getInterfaces()
                ->getNode(kIntfID)
                ->template getNeighborTable<NeighborTableT<AddrT>>();

        auto entry = neighborTable->getEntryIf(ip);
        XLOG(DBG2) << "Verify class id for " << ip
                   << " expected classID: " << static_cast<int>(classID)
                   << " found " << static_cast<int>(*entry->getClassID());
        EXPECT_EVENTUALLY_EQ(entry->getClassID(), classID);
      }
    });
  }

  template <typename AddrT>
  std::shared_ptr<SwitchState> resolveNeighbors(
      const std::shared_ptr<SwitchState>& inState) {
    return updateNeighbors<AddrT>(
        inState, false /* setClassIDs */, false /* blockNeighbors */);
  }

  template <typename AddrT>
  std::shared_ptr<SwitchState> updateClassID(
      const std::shared_ptr<SwitchState>& inState,
      bool blockNeighbor) {
    return updateNeighbors<AddrT>(
        inState, true /* setClassIDs */, blockNeighbor);
  }

  // v4 and v6 neighbors share the same MAC addresses; block both families.
  void setMacAddrsToBlock() {
    auto cfgMacAddrsToBlock = std::make_unique<std::vector<cfg::MacAndVlan>>();
    auto addMacs = [&](const auto& ipToMacAndClassIDs) {
      for (const auto& ipToMacAndClassID : ipToMacAndClassIDs) {
        auto macAndClassID = ipToMacAndClassID.second;
        auto macAddress = macAndClassID.first;
        cfg::MacAndVlan macAndVlan;
        macAndVlan.vlanID() = kVlanID;
        macAndVlan.macAddress() = macAddress.toString();
        cfgMacAddrsToBlock->emplace_back(macAndVlan);
      }
    };
    addMacs(getIpToMacAndClassID<folly::IPAddressV4>());
    addMacs(getIpToMacAndClassID<folly::IPAddressV6>());
    ThriftHandler handler(getSw());
    handler.setMacAddrsToBlock(std::move(cfgMacAddrsToBlock));
  }

  template <typename AddrT>
  void _verifyHelper(bool frontPanel, bool blockNeighbor) {
    XLOG(DBG2) << "verify send packets "
               << (frontPanel ? "out of port" : "switched");
    waitForStateUpdates(getSw());
    verifyNeighborClassId<AddrT>(blockNeighbor);
    auto ttlAclName = utility::getQueuePerHostTtlAclName();
    auto ttlCounterName = utility::getQueuePerHostTtlCounterName();

    if (frontPanel) {
      learnSrcMacOnEcmpTxPortIfPending(kDefaultEcmpWidth);
    }
    getNextUpdatedPortStats(masterLogicalPortIds()[0]);
    auto statBefore = utility::getAclInOutPackets(getSw(), ttlCounterName);

    std::map<int, int64_t> beforeQueueOutPkts;
    for (const auto& queueId : utility::kQueuePerhostQueueIds()) {
      beforeQueueOutPkts[queueId] =
          folly::copy(this->getLatestPortStats(this->masterLogicalPortIds()[0])
                          .queueOutPackets_()
                          .value())
              .at(queueId);
    }

    for (const auto& ipToMacAndClassID : getIpToMacAndClassID<AddrT>()) {
      auto dstIP = ipToMacAndClassID.first;
      sendPacket(dstIP, frontPanel, 64 /* ttl < 128 */);
      sendPacket(dstIP, frontPanel, 128 /* ttl >= 128 */);
    }

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

      /*
       *  Consider ACL with action to egress pkts through queue 2.
       *
       *  CPU originated packets:
       *     - Hits ACL (queue2Cnt = 1), egress through queue 2 of port0.
       *     - port0 is in loopback mode, so the packet gets looped back.
       *     - When packet is routed, its dstMAC gets overwritten. Thus, the
       *       looped back packet is not routed, and thus does not hit the ACL.
       *     - On some platforms, looped back packets for unknown MACs are
       *       flooded and counted on queue *before* the split horizon check
       *       (drop when srcPort == dstPort). This flooding always happens on
       *       queue 0, so expect one or more packets on queue 0.
       *
       *  Front panel packets (injected with pipeline bypass):
       *     - Egress out of port1 queue0 (pipeline bypass).
       *     - port1 is in loopback mode, so the packet gets looped back.
       *     - Rest of the workflow is same as above when CPU originated packet
       *       gets injected for switching.
       */
      for (auto [qid, beforePkts] : beforeQueueOutPkts) {
        auto pktsOnQueue = afterQueueOutPkts[qid] - beforePkts;
        XLOG(DBG2) << " Pkts on queue : " << qid << " pkts: " << pktsOnQueue;

        if (blockNeighbor) {
          // if the neighbor is blocked, all pkts are dropped
          EXPECT_EVENTUALLY_EQ(pktsOnQueue, 0);
        } else {
          if (qid == 0) {
            EXPECT_EVENTUALLY_GE(pktsOnQueue, 1);
          } else {
            EXPECT_EVENTUALLY_EQ(
                pktsOnQueue, 2 /* 1 pkt each for ttl < 128 and ttl >= 128 */);
          }
        }
      }
    });

    auto aclStatsMatch = [&]() {
      auto statAfter = utility::getAclInOutPackets(getSw(), ttlCounterName);
      XLOG(DBG2) << " Acl stats : " << statAfter;
      if (blockNeighbor) {
        // if the neighbor is blocked, all pkts are dropped
        return statAfter - statBefore == 0;
      } else {
        // counts ttl >= 128 packet only
        return statAfter - statBefore == getIpToMacAndClassID<AddrT>().size();
      }
    };
    WITH_RETRIES({ EXPECT_EVENTUALLY_TRUE(aclStatsMatch()); });
  }

  // Verify both address families in a single warm-boot cycle. v4 and v6 are
  // exercised sequentially with per-family before/after counter snapshots, so
  // the per-queue and ACL-counter assertions remain per-family.
  void verifyAllFamilies(bool blockNeighbor) {
    {
      SCOPED_TRACE("v4");
      _verifyHelper<folly::IPAddressV4>(false, blockNeighbor);
      _verifyHelper<folly::IPAddressV4>(true, blockNeighbor);
    }
    {
      SCOPED_TRACE("v6");
      _verifyHelper<folly::IPAddressV6>(false, blockNeighbor);
      _verifyHelper<folly::IPAddressV6>(true, blockNeighbor);
    }
  }

  void classIDAfterNeighborResolveHelper(bool blockNeighbor) {
    /*
     * Resolve neighbors, then apply classID
     * Prod will typically follow this sequence as LookupClassUpdater is
     * implemented as a state observer which would update resolved neighbors
     * with classIDs.
     */
    applyNewState([this](std::shared_ptr<SwitchState> /*in*/) {
      auto state = this->addNeighbors<folly::IPAddressV4>(getProgrammedState());
      state = this->addNeighbors<folly::IPAddressV6>(state);
      state = this->resolveNeighbors<folly::IPAddressV4>(state);
      state = this->resolveNeighbors<folly::IPAddressV6>(state);
      return state;
    });

    if (blockNeighbor) {
      setMacAddrsToBlock();
    }
    applyNewState([this, blockNeighbor](std::shared_ptr<SwitchState> /*in*/) {
      auto state = this->updateClassID<folly::IPAddressV4>(
          getProgrammedState(), blockNeighbor);
      state = this->updateClassID<folly::IPAddressV6>(state, blockNeighbor);
      return state;
    });
  }

  void classIDWithResolveHelper(bool blockNeighbor) {
    if (blockNeighbor) {
      setMacAddrsToBlock();
    }
    applyNewState([this, blockNeighbor](std::shared_ptr<SwitchState> /*in*/) {
      auto state = this->addNeighbors<folly::IPAddressV4>(getProgrammedState());
      state = this->addNeighbors<folly::IPAddressV6>(state);
      state = this->resolveNeighbors<folly::IPAddressV4>(state);
      state = this->resolveNeighbors<folly::IPAddressV6>(state);
      state = this->updateClassID<folly::IPAddressV4>(state, blockNeighbor);
      state = this->updateClassID<folly::IPAddressV6>(state, blockNeighbor);
      return state;
    });
  }

  void verifyHostToQueueMappingClassIDsAfterResolveHelper(bool blockNeighbor) {
    auto setup = [this, blockNeighbor]() {
      this->classIDAfterNeighborResolveHelper(blockNeighbor);
    };
    auto verify = [this, blockNeighbor]() {
      this->verifyAllFamilies(blockNeighbor);
    };

    verifyAcrossWarmBoots(setup, verify);
  }

  void verifyHostToQueueMappingClassIDsWithResolveHelper(bool blockNeighbor) {
    auto setup = [this, blockNeighbor]() {
      this->classIDWithResolveHelper(blockNeighbor);
    };
    auto verify = [this, blockNeighbor]() {
      this->verifyAllFamilies(blockNeighbor);
    };

    verifyAcrossWarmBoots(setup, verify);
  }

  template <typename AddrT>
  void verifyTtldCounterForFamily() {
    auto ttlAclName = utility::getQueuePerHostTtlAclName();
    auto ttlCounterName = utility::getQueuePerHostTtlCounterName();

    const auto aclByteCounterSupported =
        isSupportedOnAllAsics(HwAsic::Feature::ACL_BYTE_COUNTER);
    for (bool frontPanel : {false, true}) {
      getNextUpdatedPortStats(masterLogicalPortIds()[0]);
      auto packetsBefore = utility::getAclInOutPackets(getSw(), ttlCounterName);
      auto bytesBefore = aclByteCounterSupported
          ? utility::getAclInOutPackets(getSw(), ttlCounterName, true)
          : 0;

      auto dstIP = getIpToMacAndClassID<AddrT>().begin()->first;
      sendPacket(dstIP, frontPanel, 64 /* ttl < 128 */);
      size_t packetSize = sendPacket(dstIP, frontPanel, 128 /* ttl >= 128 */);

      WITH_RETRIES({
        auto packetsAfter =
            utility::getAclInOutPackets(getSw(), ttlCounterName);
        auto bytesAfter = aclByteCounterSupported
            ? utility::getAclInOutPackets(getSw(), ttlCounterName, true)
            : 0;

        XLOG(DBG2) << "verify send packets "
                   << (frontPanel ? "out of port" : "switched") << "\n"
                   << "ttlAclPacketCounter: " << std::to_string(packetsBefore)
                   << " -> " << std::to_string(packetsAfter) << "\n"
                   << "ttlAclBytesCounter: " << std::to_string(bytesBefore)
                   << " -> " << std::to_string(bytesAfter);

        // counts ttl >= 128 packet only
        EXPECT_EVENTUALLY_EQ(packetsAfter - packetsBefore, 1);
        if (aclByteCounterSupported) {
          if (frontPanel) {
            EXPECT_EVENTUALLY_EQ(bytesAfter - bytesBefore, packetSize);
          }
          // TODO: Still need to debug why we get extra 4 bytes for CPU port
          EXPECT_EVENTUALLY_TRUE(bytesAfter - bytesBefore >= packetSize);
        }
      });
    }
  }

  void verifyTtldCounter() {
    auto setup = [this]() {
      applyNewState([this](std::shared_ptr<SwitchState> /*in*/) {
        auto state =
            this->addNeighbors<folly::IPAddressV4>(getProgrammedState());
        state = this->addNeighbors<folly::IPAddressV6>(state);
        state = this->resolveNeighbors<folly::IPAddressV4>(state);
        state = this->resolveNeighbors<folly::IPAddressV6>(state);
        return state;
      });
    };

    auto verify = [this]() {
      {
        SCOPED_TRACE("v4");
        this->verifyTtldCounterForFamily<folly::IPAddressV4>();
      }
      {
        SCOPED_TRACE("v6");
        this->verifyTtldCounterForFamily<folly::IPAddressV6>();
      }
    };

    verifyAcrossWarmBoots(setup, verify);
  }

  template <typename AddrT>
  AddrT kSrcIP() {
    if constexpr (std::is_same<AddrT, folly::IPAddressV4>::value) {
      return folly::IPAddressV4("1.0.0.1");
    } else {
      return folly::IPAddressV6("1::1");
    }
  }

 private:
  template <typename AddrT>
  size_t sendPacket(AddrT dstIP, bool frontPanel, uint8_t ttl) {
    auto vlanId =
        VlanID(*initialConfig(*getAgentEnsemble()).vlanPorts()[0].vlanID());
    auto intfMac = utility::getInterfaceMac(getProgrammedState(), vlanId);
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    auto txPacket = utility::makeUDPTxPacket(
        getSw(),
        vlanId,
        srcMac, // src mac
        intfMac, // dst mac
        kSrcIP<AddrT>(),
        dstIP,
        8000, // l4 src port
        8001, // l4 dst port
        48 << 2, // DSCP
        ttl);

    size_t txPacketSize = txPacket->buf()->length();
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

    return txPacketSize;
  }

  const VlanID kVlanID{utility::kBaseVlanId};
  const InterfaceID kIntfID{utility::kBaseVlanId};
};

// Verify that traffic arriving on a front panel port gets right queue-per-host
// queue.
TEST_F(AgentQueuePerHostTest, VerifyHostToQueueMappingClassIDsAfterResolve) {
  this->verifyHostToQueueMappingClassIDsAfterResolveHelper(
      false /* block neighbor */);
}

// Verify that traffic arriving on a front panel port to a blocked neighbor gets
// dropped.
TEST_F(
    AgentQueuePerHostTest,
    VerifyHostToQueueMappingClassIDsAfterResolveBlock) {
  this->verifyHostToQueueMappingClassIDsAfterResolveHelper(
      true /* block neighbor */);
}

// Verify that traffic arriving on a front panel port gets right queue-per-host
// queue.
TEST_F(AgentQueuePerHostTest, VerifyHostToQueueMappingClassIDsWithResolve) {
  this->verifyHostToQueueMappingClassIDsWithResolveHelper(
      false /* block neighbor */);
}

// Verify that traffic arriving on a front panel port to a blocked neighbor gets
// dropped.
TEST_F(
    AgentQueuePerHostTest,
    VerifyHostToQueueMappingClassIDsWithResolveBlock) {
  this->verifyHostToQueueMappingClassIDsWithResolveHelper(
      true /* block neighbor */);
}

// Verify that TTLd traffic not going to queue-per-host has TTLd counter
// incremented.
TEST_F(AgentQueuePerHostTest, VerifyTtldCounter) {
  this->verifyTtldCounter();
}

// Verify that removing a Pending NDP/ARP entry that was never Reachable
// is handled cleanly by LookupClassUpdater on a QPH-enabled port. Unlike
// the other tests in this file, this one does not resolve the entries
// between add and remove.
TEST_F(AgentQueuePerHostTest, RemovePendingNeighborDoesNotCrash) {
  auto setup = [this]() {
    this->applyNewState(
        [this](const std::shared_ptr<SwitchState>& /*in*/) {
          auto state =
              this->addNeighbors<folly::IPAddressV4>(getProgrammedState());
          state = this->addNeighbors<folly::IPAddressV6>(state);
          return state;
        },
        "inject Pending neighbors");

    this->applyNewState(
        [this](const std::shared_ptr<SwitchState>& /*in*/) {
          auto state =
              this->removeNeighbors<folly::IPAddressV4>(getProgrammedState());
          state = this->removeNeighbors<folly::IPAddressV6>(state);
          return state;
        },
        "remove Pending neighbors");
  };

  auto verify = [this]() {
    {
      SCOPED_TRACE("v4");
      for (const auto& ipToMacAndClassID :
           this->getIpToMacAndClassID<folly::IPAddressV4>()) {
        EXPECT_EQ(
            this->getNeighborEntry<folly::IPAddressV4>(ipToMacAndClassID.first),
            nullptr);
      }
    }
    {
      SCOPED_TRACE("v6");
      for (const auto& ipToMacAndClassID :
           this->getIpToMacAndClassID<folly::IPAddressV6>()) {
        EXPECT_EQ(
            this->getNeighborEntry<folly::IPAddressV6>(ipToMacAndClassID.first),
            nullptr);
      }
    }
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
