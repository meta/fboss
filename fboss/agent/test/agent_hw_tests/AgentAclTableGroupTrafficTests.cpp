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

  bool portRifOneLoop() const {
    return checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
               ->getAsicType() == cfg::AsicType::ASIC_TYPE_P200;
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

  // Host byte on the port RIF prefix. The virtual default VLAN RIF is
  // created first on P200 and takes 1::/64, so interface 2000 is 2::/64.
  // An SVI still has 1::/64 on this interface. Read the programmed prefix.
  template <typename AddrT>
  AddrT hostAddress(uint8_t host) {
    auto intf = getProgrammedState()->getInterfaces()->getNode(kIntfID);
    for (const auto& addrAndMask : intf->getAddressesCopy()) {
      const auto& addr = addrAndMask.first;
      if constexpr (std::is_same<AddrT, folly::IPAddressV4>::value) {
        if (!addr.isV4()) {
          continue;
        }
        auto bytes = addr.asV4().toByteArray();
        bytes[3] = host;
        return folly::IPAddressV4(bytes);
      } else {
        if (!addr.isV6()) {
          continue;
        }
        auto bytes = addr.asV6().toByteArray();
        bytes[15] = host;
        return folly::IPAddressV6(bytes);
      }
    }
    XLOG(FATAL) << "Interface has no address for the test traffic";
    return AddrT();
  }

  template <typename AddrT>
  std::map<AddrT, std::pair<folly::MacAddress, cfg::AclLookupClass>>
  getIpToMacAndClassID() {
    return {
        {hostAddress<AddrT>(10),
         std::make_pair(
             folly::MacAddress("0:2:3:4:5:10"),
             cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0)},
        {hostAddress<AddrT>(11),
         std::make_pair(
             folly::MacAddress("0:2:3:4:5:11"),
             cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1)},
        {hostAddress<AddrT>(12),
         std::make_pair(
             folly::MacAddress("0:2:3:4:5:12"),
             cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2)},
        {hostAddress<AddrT>(13),
         std::make_pair(
             folly::MacAddress("0:2:3:4:5:13"),
             cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3)},
        {hostAddress<AddrT>(14),
         std::make_pair(
             folly::MacAddress("0:2:3:4:5:14"),
             cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_4)},
    };
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
      bool setClassIDs,
      std::optional<folly::MacAddress> macOverride = std::nullopt) {
    using NeighborTableT = typename std::conditional_t<
        std::is_same<AddrT, folly::IPAddressV4>::value,
        ArpTable,
        NdpTable>;

    auto outState{inState->clone()};

    for (const auto& ipToMacAndClassID : getIpToMacAndClassID<AddrT>()) {
      auto ip = ipToMacAndClassID.first;
      auto [neighborMac, classID] = ipToMacAndClassID.second;
      if (macOverride.has_value()) {
        neighborMac = *macOverride;
      }

      auto intf = outState->getInterfaces()->getNode(kIntfID);
      // Port RIF stores MAC and port on the interface neighbor. The VLAN FDB
      // path looks up kVlanID, which a port RIF does not create.
      auto portRif = intf->getType() == cfg::InterfaceType::PORT;
      NeighborTableT* neighborTable;
      neighborTable = intf->template getNeighborTable<NeighborTableT>()->modify(
          kIntfID, &outState);
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

      if (getSw()->needL2EntryForNeighbor() && !portRif) {
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

  // The looped frame's DMAC is the neighbor MAC. On a port RIF that has to be
  // the router MAC, or ETHERNET_NO_TERMINATION_ON_L3_PORT drops it before the
  // DSCP 26 lookup. Restored before the DSCP 26 send.
  template <typename AddrT>
  void programDscpNeighborMac(std::optional<folly::MacAddress> mac) {
    applyNewState([this, mac](std::shared_ptr<SwitchState> /*in*/) {
      return this->updateNeighbors<AddrT>(
          this->getProgrammedState(), true /*setClassIDs*/, mac);
    });
  }

  template <typename AddrT>
  void addResolvedNeighborWithClassID() {
    applyNewState([this](std::shared_ptr<SwitchState> /*in*/) {
      auto state1 = addNeighbors<AddrT>(this->getProgrammedState());
      auto state2 = resolveNeighbors<AddrT>(state1);
      return state2;
    });
    applyNewState([this](std::shared_ptr<SwitchState> /*in*/) {
      return updateClassID<AddrT>(this->getProgrammedState());
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
    } else if constexpr (std::is_same<AddrT, folly::IPAddressV6>::value) {
      testType = "IPv6 Traffic";
    }
    dstIP = hostAddress<AddrT>(10);

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

    auto oneLookup =
        1 * 2 * 2 * (utility::kUdpPorts().size() + utility::kTcpPorts().size());
    auto ttlDelta =
        2 * (utility::kUdpPorts().size() + utility::kTcpPorts().size());
    bool oneLoop = portRifOneLoop();

    if (frontPanel) {
      learnSrcMacOnEcmpTxPortIfPending(kEcmpWidth);
    }
    auto beforeAclPkts = pktCounterHelper();
    if (oneLoop) {
      auto intfMac =
          getProgrammedState()->getInterfaces()->getNode(kIntfID)->getMac();
      programDscpNeighborMac<AddrT>(intfMac);
    }
    // Hop limit 2 returns once as hop limit 1 with DSCP 26. That lookup is
    // the DSCP counter. The following transmit is taken by
    // L3_TTL_OR_HOP_LIMIT_IS_ONE. Both TTL iterations are overridden, so
    // the count stays oneLookup.
    sendAllPacketshelper<AddrT>(
        dstIP,
        frontPanel,
        0,
        oneLoop ? std::optional<uint8_t>(2) : std::nullopt,
        oneLoop);

    WITH_RETRIES({
      auto [dscpAclPkts, ttlAclPkts] = pktCounterHelper();
      XLOG(DBG2) << "Before ICP pkts: " << beforeAclPkts.first
                 << " Intermediate ICP pkts: " << dscpAclPkts;
      XLOG(DBG2) << "Before Ttl pkts: " << beforeAclPkts.second
                 << " Intermediate Ttl pkts: " << ttlAclPkts;
      // Hop limit 2 misses the 0x80 TTL entry. The DSCP 26 count is the return.
      EXPECT_EVENTUALLY_EQ(
          ttlAclPkts - beforeAclPkts.second, oneLoop ? 0 : ttlDelta);
      EXPECT_EVENTUALLY_EQ(dscpAclPkts - beforeAclPkts.first, oneLookup);
    });
    if (oneLoop) {
      programDscpNeighborMac<AddrT>(std::nullopt);
    }

    auto intermediateAclPkts = pktCounterHelper();
    // Host neighbor, hop limits 64 and 128. On a port RIF the routed copy is
    // dropped, so the DSCP 26 packet is counted once. Ebro still loops it.
    sendAllPacketshelper<AddrT>(
        dstIP, frontPanel, utility::kIcpDscp(), std::nullopt, oneLoop);

    WITH_RETRIES({
      auto [dscpAclPkts, ttlAclPkts] = pktCounterHelper();
      XLOG(DBG2) << "Intermediate ICP pkts: " << intermediateAclPkts.first
                 << " After ICP pkts: " << dscpAclPkts;
      XLOG(DBG2) << "Intermediate Ttl pkts: " << intermediateAclPkts.second
                 << " After Ttl pkts: " << ttlAclPkts;
      EXPECT_EVENTUALLY_EQ(ttlAclPkts - intermediateAclPkts.second, ttlDelta);
      EXPECT_EVENTUALLY_EQ(
          dscpAclPkts - intermediateAclPkts.first,
          oneLoop ? oneLookup : oneLookup * 2);
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
    // Host 1 on the port RIF subnet. Not the interface address itself
    // when that address uses host 0 (IPv6).
    return hostAddress<AddrT>(1);
  }

 private:
  template <typename AddrT>
  void sendAllPacketshelper(
      AddrT dstIP,
      bool frontPanel,
      uint8_t dscp,
      std::optional<uint8_t> ttlOverride = std::nullopt,
      bool portInject = false) {
    sendAllPackets(
        dstIP,
        frontPanel,
        dscp,
        IP_PROTO::IP_PROTO_UDP,
        utility::kUdpPorts(),
        ttlOverride,
        portInject);
    sendAllPackets(
        dstIP,
        frontPanel,
        dscp,
        IP_PROTO::IP_PROTO_TCP,
        utility::kTcpPorts(),
        ttlOverride,
        portInject);
  }

  template <typename AddrT>
  void sendAllPackets(
      AddrT dstIP,
      bool frontPanel,
      uint8_t dscp,
      IP_PROTO proto,
      const std::vector<uint32_t>& ports,
      std::optional<uint8_t> ttlOverride = std::nullopt,
      bool portInject = false) {
    for (auto ttl : ttlfields) {
      if (ttlOverride.has_value()) {
        ttl = *ttlOverride;
      }
      for (auto port : ports) {
        sendPacket(
            dstIP,
            frontPanel,
            ttl,
            dscp,
            proto,
            port,
            std::nullopt,
            portInject);
        sendPacket(
            dstIP,
            frontPanel,
            ttl,
            dscp,
            proto,
            std::nullopt,
            port,
            portInject);
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
      std::optional<uint16_t> l4DstPort,
      bool portInject = false) {
    auto intf = getProgrammedState()->getInterfaces()->getNode(kIntfID);
    // SVI MAC is keyed by VLAN. A port RIF has no such VLAN.
    // Do not tag the default VLAN: that is the virtual CPU RIF, and the
    // punt is reported on port 0, which is not a configured port.
    auto portRif = intf->getType() == cfg::InterfaceType::PORT;
    folly::MacAddress intfMac = portRif
        ? intf->getMac()
        : utility::getInterfaceMac(getProgrammedState(), kVlanID);
    std::optional<VlanID> txVlan =
        portRif ? std::nullopt : std::optional<VlanID>(kVlanID);
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    std::unique_ptr<facebook::fboss::TxPacket> txPacket;
    CHECK(proto == IP_PROTO::IP_PROTO_UDP || proto == IP_PROTO::IP_PROTO_TCP);
    if (proto == IP_PROTO::IP_PROTO_UDP) {
      txPacket = utility::makeUDPTxPacket(
          getSw(),
          txVlan,
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
          txVlan,
          srcMac,
          intfMac,
          kSrcIP<AddrT>(),
          dstIP,
          l4SrcPort.has_value() ? l4SrcPort.value() : 8000,
          l4DstPort.has_value() ? l4DstPort.value() : 8001,
          dscp << 2,
          ttl);
    }
    // Port inject is the DSCP 0 port-RIF return. It is pipeline bypass.
    // The DSCP 26 send keeps the original choice: switched when frontPanel
    // is false, and out of the ECMP port when frontPanel is true.
    if (portInject) {
      getSw()->sendPacketOutOfPortAsync(
          std::move(txPacket), masterLogicalPortIds()[1]);
    } else if (frontPanel) {
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
