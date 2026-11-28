// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/IPProto.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/ResourceLibUtil.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/DscpMarkingUtils.h"
#include "fboss/agent/test/utils/NeighborTestUtils.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"
#include "fboss/agent/test/utils/PacketTestUtils.h"
#include "fboss/agent/test/utils/QueuePerHostTestUtils.h"

DECLARE_bool(enable_acl_table_group);

namespace facebook::fboss {

class AgentAclTableGroupTrafficTest : public AgentHwTest {
 protected:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::MULTI_ACL_TABLE,
        ProductionFeature::L3_FORWARDING,
        ProductionFeature::L3_QOS};
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_enable_acl_table_group = true;
  }

  // Ebro (wedge400c) and P200 derive the shared ACL key profile from the first
  // table, so every table's qualifiers must be added up front.
  bool isSingleStageAclTableAsic() const {
    return checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
               ->getAsicType() == cfg::AsicType::ASIC_TYPE_EBRO ||
        checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
            ->getAsicType() == cfg::AsicType::ASIC_TYPE_P200;
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        {ensemble.masterLogicalPortIds()[0],
         ensemble.masterLogicalPortIds()[1]},
        true /*interfaceHasSubnet*/);
    return cfg;
  }

  template <typename AddrT>
  const std::map<AddrT, std::pair<folly::MacAddress, cfg::AclLookupClass>>&
  getIpToMacAndClassID() {
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

  const std::vector<uint8_t> ttlfields = {64, 128};
  const uint8_t dscpDefault = 48;

  template <typename AddrT>
  std::shared_ptr<SwitchState> addNeighbors(
      const std::shared_ptr<SwitchState>& inState) {
    using NeighborTableT = typename std::conditional_t<
        std::is_same<AddrT, folly::IPAddressV4>::value,
        ArpTable,
        NdpTable>;

    auto outState{inState->clone()};

    for (const auto& ipToMacAndClassID : getIpToMacAndClassID<AddrT>()) {
      auto ip = ipToMacAndClassID.first;

      NeighborTableT* neighborTable;
      neighborTable = outState->getInterfaces()
                          ->getNode(kIntfID)
                          ->template getNeighborTable<NeighborTableT>()
                          ->modify(kIntfID, &outState);

      neighborTable->addPendingEntry(ip, kIntfID);
    }

    return outState;
  }

  template <typename AddrT>
  std::shared_ptr<SwitchState> updateNeighbors(
      const std::shared_ptr<SwitchState>& inState,
      bool setClassIDs) {
    using NeighborTableT = typename std::conditional_t<
        std::is_same<AddrT, folly::IPAddressV4>::value,
        ArpTable,
        NdpTable>;

    auto outState{inState->clone()};

    for (const auto& ipToMacAndClassID : getIpToMacAndClassID<AddrT>()) {
      auto ip = ipToMacAndClassID.first;
      auto [neighborMac, classID] = ipToMacAndClassID.second;

      NeighborTableT* neighborTable;
      neighborTable = outState->getInterfaces()
                          ->getNode(kIntfID)
                          ->template getNeighborTable<NeighborTableT>()
                          ->modify(kIntfID, &outState);
      auto neighborPort = PortDescriptor(masterLogicalPortIds()[0]);
      auto existingEntry = neighborTable->getEntryIf(ip);

      if (setClassIDs) {
        neighborTable->updateEntry(
            ip,
            neighborMac,
            neighborPort,
            kIntfID,
            NeighborState::REACHABLE,
            classID);
      } else {
        neighborTable->updateEntry(
            ip, neighborMac, neighborPort, kIntfID, NeighborState::REACHABLE);
      }

      if (getSw()->needL2EntryForNeighbor()) {
        outState = utility::NeighborTestUtils::updateMacEntryForUpdatedNbrEntry(
            outState, kVlanID, existingEntry, neighborTable->getEntryIf(ip));
      }
    }

    return outState;
  }

  template <typename AddrT>
  std::shared_ptr<SwitchState> resolveNeighbors(
      const std::shared_ptr<SwitchState>& inState) {
    return updateNeighbors<AddrT>(inState, false);
  }

  template <typename AddrT>
  std::shared_ptr<SwitchState> updateClassID(
      const std::shared_ptr<SwitchState>& inState) {
    return updateNeighbors<AddrT>(inState, true);
  }

  template <typename AddrT>
  void addResolvedNeighborWithClassID() {
    applyNewState([this](const std::shared_ptr<SwitchState>& in) {
      auto state1 = addNeighbors<AddrT>(in);
      auto state2 = resolveNeighbors<AddrT>(state1);
      return state2;
    });
    // Build on `in`: getProgrammedState() is still the pre batch state, so
    // rebuilding from it drops the MAC entry classIDs LookupClassUpdater
    // queued for the neighbors resolved above if they coalesce into this batch.
    applyNewState([this](const std::shared_ptr<SwitchState>& in) {
      return updateClassID<AddrT>(in);
    });
  }

  template <typename AddrT>
  void _verifyHelperMultipleAclTables(bool frontPanel) {
    std::string testType;

    if constexpr (std::is_same<AddrT, folly::IPAddressV4>::value) {
      testType = "IPv4 Traffic";
    } else if constexpr (std::is_same<AddrT, folly::IPAddressV6>::value) {
      testType = "IPv6 Traffic";
    }

    auto ttlCounterName = utility::getQueuePerHostTtlCounterName();

    if (frontPanel) {
      learnSrcMacOnEcmpTxPortIfPending(kEcmpWidth);
    }
    auto statBefore = utility::getAclInOutPackets(getSw(), ttlCounterName);

    const auto beforePortStats =
        this->getLatestPortStats(this->masterLogicalPortIds()[0]);
    const auto& beforeQueueOutPktsAll =
        beforePortStats.queueOutPackets_().value();
    std::map<int, int64_t> beforeQueueOutPkts;
    for (const auto& queueId : utility::kQueuePerhostQueueIds()) {
      beforeQueueOutPkts[queueId] = beforeQueueOutPktsAll.at(queueId);
    }

    for (const auto& ipToMacAndClassID : getIpToMacAndClassID<AddrT>()) {
      auto dstIP = ipToMacAndClassID.first;
      sendPacket<AddrT>(
          dstIP,
          frontPanel,
          ttlfields[0],
          dscpDefault,
          IP_PROTO::IP_PROTO_UDP,
          std::nullopt,
          std::nullopt);
      sendPacket<AddrT>(
          dstIP,
          frontPanel,
          ttlfields[1],
          dscpDefault,
          IP_PROTO::IP_PROTO_UDP,
          std::nullopt,
          std::nullopt);
    }

    WITH_RETRIES({
      const auto afterPortStats =
          this->getLatestPortStats(this->masterLogicalPortIds()[0]);
      const auto& queueOutPkts = afterPortStats.queueOutPackets_().value();
      std::map<int, int64_t> afterQueueOutPkts;
      for (const auto& queueId : utility::kQueuePerhostQueueIds()) {
        afterQueueOutPkts[queueId] = queueOutPkts.at(queueId);
      }

      for (auto [qid, beforePkts] : beforeQueueOutPkts) {
        auto pktsOnQueue = afterQueueOutPkts[qid] - beforePkts;
        XLOG(DBG2) << "TestType: " << testType << " Pkts on queue : " << qid
                   << " pkts: " << pktsOnQueue;
        if (qid == 0) {
          EXPECT_EVENTUALLY_GE(pktsOnQueue, 1);
        } else {
          EXPECT_EVENTUALLY_EQ(pktsOnQueue, 2);
        }
      }

      auto statAfter = utility::getAclInOutPackets(getSw(), ttlCounterName);
      XLOG(DBG2) << "statBefore: " << statBefore << " statAfter: " << statAfter
                 << " expected: " << getIpToMacAndClassID<AddrT>().size();
      EXPECT_EVENTUALLY_EQ(
          statAfter - statBefore, getIpToMacAndClassID<AddrT>().size());
    });
  }

  void verifyMultipleAclTablesHelper() {
    ASSERT_TRUE(isSupportedOnAllAsics(HwAsic::Feature::MULTIPLE_ACL_TABLES));

    auto setup = [this]() {
      bool addAllQualifiers = isSingleStageAclTableAsic();

      auto newCfg{initialConfig(*getAgentEnsemble())};
      utility::addQueuePerHostQueueConfig(&newCfg);
      utility::addQueuePerHostAclTables(
          &newCfg,
          1 /*priority*/,
          addAllQualifiers,
          getAgentEnsemble()->isSai());
      utility::addTtlAclTable(&newCfg, 2 /*priority*/);
      applyNewConfig(newCfg);

      utility::EcmpSetupAnyNPorts6 ecmpHelper(
          getProgrammedState(), getSw()->needL2EntryForNeighbor());
      resolveNeighborAndProgramRoutes(ecmpHelper, kEcmpWidth);

      addResolvedNeighborWithClassID<folly::IPAddressV4>();
      addResolvedNeighborWithClassID<folly::IPAddressV6>();
    };

    auto verify = [this]() {
      XLOG(DBG2) << "verify send packets switched";
      _verifyHelperMultipleAclTables<folly::IPAddressV4>(false);
      _verifyHelperMultipleAclTables<folly::IPAddressV6>(false);
      XLOG(DBG2) << "verify send packets out of port";
      _verifyHelperMultipleAclTables<folly::IPAddressV4>(true);
      _verifyHelperMultipleAclTables<folly::IPAddressV6>(true);
    };

    verifyAcrossWarmBoots(setup, verify);
  }

  template <typename AddrT>
  std::pair<std::string, AddrT> testTypeAndIpHelper() {
    std::string testType;
    AddrT dstIP;

    if constexpr (std::is_same<AddrT, folly::IPAddressV4>::value) {
      testType = "IPv4 Traffic";
      dstIP = folly::IPAddressV4("1.0.0.10");
    } else if constexpr (std::is_same<AddrT, folly::IPAddressV6>::value) {
      testType = "IPv6 Traffic";
      dstIP = folly::IPAddressV6("1::10");
    }

    return std::make_pair(testType, dstIP);
  }

  std::pair<uint64_t, uint64_t> pktCounterHelper() {
    auto kCounterName = utility::kCounterName();
    auto dscpAclPkts = utility::getAclInOutPackets(getSw(), kCounterName);

    auto ttlCounterName = utility::getQueuePerHostTtlCounterName();
    auto ttlAclPkts = utility::getAclInOutPackets(getSw(), ttlCounterName);

    return std::make_pair(dscpAclPkts, ttlAclPkts);
  }

  template <typename AddrT>
  void _verifyHelperDscpTtlAclTables(bool frontPanel) {
    auto [testType, dstIP] = testTypeAndIpHelper<AddrT>();
    XLOG(DBG2) << "TestType: " << testType;

    if (frontPanel) {
      learnSrcMacOnEcmpTxPortIfPending(kEcmpWidth);
    }
    auto beforeAclPkts = pktCounterHelper();
    sendAllPacketshelper<AddrT>(dstIP, frontPanel, 0);

    WITH_RETRIES({
      auto [dscpAclPkts, ttlAclPkts] = pktCounterHelper();
      XLOG(DBG2) << "Before ICP pkts: " << beforeAclPkts.first
                 << " Intermediate ICP pkts: " << dscpAclPkts;
      XLOG(DBG2) << "Before Ttl pkts: " << beforeAclPkts.second
                 << " Intermediate Ttl pkts: " << ttlAclPkts;
      EXPECT_EVENTUALLY_EQ(
          dscpAclPkts - beforeAclPkts.first,
          (1 * 2 * 2 *
           (utility::kUdpPorts().size() + utility::kTcpPorts().size())));
      EXPECT_EVENTUALLY_EQ(
          ttlAclPkts - beforeAclPkts.second,
          (2 * (utility::kUdpPorts().size() + utility::kTcpPorts().size())));
    });

    auto intermediateAclPkts = pktCounterHelper();
    sendAllPacketshelper<AddrT>(dstIP, frontPanel, utility::kIcpDscp());

    WITH_RETRIES({
      auto [dscpAclPkts, ttlAclPkts] = pktCounterHelper();
      XLOG(DBG2) << "Intermediate ICP pkts: " << intermediateAclPkts.first
                 << " After ICP pkts: " << dscpAclPkts;
      XLOG(DBG2) << "Intermediate Ttl pkts: " << intermediateAclPkts.second
                 << " After Ttl pkts: " << ttlAclPkts;
      EXPECT_EVENTUALLY_EQ(
          ttlAclPkts - intermediateAclPkts.second,
          (2 * (utility::kUdpPorts().size() + utility::kTcpPorts().size())));
      EXPECT_EVENTUALLY_EQ(
          dscpAclPkts - intermediateAclPkts.first,
          (2 * 2 * 2 *
           (utility::kUdpPorts().size() + utility::kTcpPorts().size())));
    });
  }

  void verifyDscpTtlAclTablesHelper() {
    ASSERT_TRUE(isSupportedOnAllAsics(HwAsic::Feature::MULTIPLE_ACL_TABLES));

    auto setup = [this]() {
      bool addAllQualifiers = isSingleStageAclTableAsic();

      auto newCfg{initialConfig(*getAgentEnsemble())};
      utility::addOlympicQosMaps(newCfg, getAgentEnsemble()->getL3Asics());
      auto hwAsic =
          checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
      utility::addDscpAclTable(
          &newCfg,
          hwAsic,
          1 /*priority*/,
          addAllQualifiers,
          getAgentEnsemble()->isSai());
      utility::addTtlAclTable(&newCfg, 2);
      applyNewConfig(newCfg);

      utility::EcmpSetupAnyNPorts6 ecmpHelper(
          getProgrammedState(), getSw()->needL2EntryForNeighbor());
      resolveNeighborAndProgramRoutes(ecmpHelper, kEcmpWidth);

      addResolvedNeighborWithClassID<folly::IPAddressV4>();
      addResolvedNeighborWithClassID<folly::IPAddressV6>();
    };

    auto verify = [this]() {
      // TODO: IPV4 not working. It needs to be triaged and fixed
      // _verifyHelperDscpTtlAclTables<folly::IPAddressV4>(false);
      // _verifyHelperDscpTtlAclTables<folly::IPAddressV4>(true);
      _verifyHelperDscpTtlAclTables<folly::IPAddressV6>(false);
      _verifyHelperDscpTtlAclTables<folly::IPAddressV6>(true);
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
  void sendAllPacketshelper(AddrT dstIP, bool frontPanel, uint8_t dscp) {
    sendAllPackets(
        dstIP, frontPanel, dscp, IP_PROTO::IP_PROTO_UDP, utility::kUdpPorts());
    sendAllPackets(
        dstIP, frontPanel, dscp, IP_PROTO::IP_PROTO_TCP, utility::kTcpPorts());
  }

  template <typename AddrT>
  void sendAllPackets(
      AddrT dstIP,
      bool frontPanel,
      uint8_t dscp,
      IP_PROTO proto,
      const std::vector<uint32_t>& ports) {
    for (auto ttl : ttlfields) {
      for (auto port : ports) {
        sendPacket(dstIP, frontPanel, ttl, dscp, proto, port, std::nullopt);
        sendPacket(dstIP, frontPanel, ttl, dscp, proto, std::nullopt, port);
      }
    }
  }

  template <typename AddrT>
  void sendPacket(
      AddrT dstIP,
      bool frontPanel,
      uint8_t ttl,
      uint8_t dscp,
      IP_PROTO proto,
      std::optional<uint16_t> l4SrcPort,
      std::optional<uint16_t> l4DstPort) {
    auto intfMac = utility::getInterfaceMac(getProgrammedState(), kVlanID);
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    std::unique_ptr<facebook::fboss::TxPacket> txPacket;
    CHECK(proto == IP_PROTO::IP_PROTO_UDP || proto == IP_PROTO::IP_PROTO_TCP);
    if (proto == IP_PROTO::IP_PROTO_UDP) {
      txPacket = utility::makeUDPTxPacket(
          getSw(),
          kVlanID,
          srcMac,
          intfMac,
          kSrcIP<AddrT>(),
          dstIP,
          l4SrcPort.has_value() ? l4SrcPort.value() : 8000,
          l4DstPort.has_value() ? l4DstPort.value() : 8001,
          dscp << 2,
          ttl);
    } else if (proto == IP_PROTO::IP_PROTO_TCP) {
      txPacket = utility::makeTCPTxPacket(
          getSw(),
          kVlanID,
          srcMac,
          intfMac,
          kSrcIP<AddrT>(),
          dstIP,
          l4SrcPort.has_value() ? l4SrcPort.value() : 8000,
          l4DstPort.has_value() ? l4DstPort.value() : 8001,
          dscp << 2,
          ttl);
    }
    if (frontPanel) {
      utility::EcmpSetupAnyNPorts6 ecmpHelper(
          getProgrammedState(), getSw()->needL2EntryForNeighbor());
      auto outPort = ecmpHelper.ecmpPortDescriptorAt(kEcmpWidth).phyPortID();
      getSw()->sendPacketOutOfPortAsync(std::move(txPacket), outPort);
    } else {
      sendPacketSwitchedAsync(std::move(txPacket));
    }
  }

  static inline constexpr auto kEcmpWidth = 1;
  const VlanID kVlanID{utility::kBaseVlanId};
  const InterfaceID kIntfID{utility::kBaseVlanId};
};

TEST_F(
    AgentAclTableGroupTrafficTest,
    VerifyQueuePerHostAclTableAndTtlAclTable) {
  this->verifyMultipleAclTablesHelper();
}

TEST_F(AgentAclTableGroupTrafficTest, VerifyDscpMarkingAndTtlAclTable) {
  this->verifyDscpTtlAclTablesHelper();
}

} // namespace facebook::fboss
