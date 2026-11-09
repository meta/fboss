// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <fmt/format.h>

#include <algorithm>

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/PacketTestUtils.h"

DECLARE_bool(enable_acl_table_group);

namespace facebook::fboss {

class AgentPortBoundIngressAclTest : public AgentHwTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::L3_FORWARDING,
        ProductionFeature::PORT_BOUND_INGRESS_ACL};
  }

  std::optional<size_t> maxRequiredInterfacePorts() const override {
    return 4;
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_enable_acl_table_group = true;
  }

 protected:
  enum class L4Protocol { UDP, TCP };

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    return utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /* interfaceHasSubnet */);
  }

  void setupL3Forwarding() {
    utility::EcmpSetupAnyNPorts6 ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return ecmpHelper.resolveNextHops(in, 1);
    });
    auto updater = getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(&updater, 1);
  }

  void addRestrictAclTable(cfg::AclTableGroup* aclTableGroup) {
    // TODO: Add the remaining production restrict-* ACL entries to this table.
    cfg::AclEntry permitAcl;
    permitAcl.name() = kRestrictPermitAclName;
    permitAcl.actionType() = cfg::AclActionType::PERMIT;
    permitAcl.l4DstPort() = kRestrictPermitL4DstPort;

    cfg::AclEntry denyAcl;
    denyAcl.name() = kRestrictDenyAclName;
    denyAcl.actionType() = cfg::AclActionType::DENY;

    cfg::AclTable aclTable;
    aclTable.name() = kRestrictAclTableName;
    aclTable.priority() = 1;
    aclTable.actionTypes() = {
        cfg::AclTableActionType::PACKET_ACTION,
        cfg::AclTableActionType::COUNTER};
    aclTable.qualifiers() = {cfg::AclTableQualifier::L4_DST_PORT};
    aclTable.aclEntries() = {std::move(permitAcl), std::move(denyAcl)};
    aclTableGroup->aclTables()->push_back(std::move(aclTable));
  }

  void addBlockAclTable(cfg::AclTableGroup* aclTableGroup) {
    // TODO: Add the remaining production block-* ACL entries to this table.
    cfg::AclEntry permitAcl;
    permitAcl.name() = kBlockPermitAclName;
    permitAcl.actionType() = cfg::AclActionType::PERMIT;
    permitAcl.proto() = 17;
    permitAcl.l4DstPort() = kBlockPermitL4DstPort;

    cfg::AclEntry denyAcl;
    denyAcl.name() = kBlockDenyAclName;
    denyAcl.actionType() = cfg::AclActionType::DENY;

    cfg::AclTable aclTable;
    aclTable.name() = kBlockAclTableName;
    aclTable.priority() = 2;
    aclTable.actionTypes() = {
        cfg::AclTableActionType::PACKET_ACTION,
        cfg::AclTableActionType::COUNTER};
    aclTable.qualifiers() = {
        cfg::AclTableQualifier::IP_PROTOCOL_NUMBER,
        cfg::AclTableQualifier::L4_DST_PORT};
    aclTable.aclEntries() = {std::move(permitAcl), std::move(denyAcl)};
    aclTableGroup->aclTables()->push_back(std::move(aclTable));
  }

  void addPortBoundAclTableGroup(cfg::SwitchConfig* config) {
    cfg::AclTableGroup aclTableGroup;
    aclTableGroup.name() = kAclTableGroupName;
    aclTableGroup.stage() = cfg::AclStage::INGRESS;
    aclTableGroup.bindPoint() = cfg::AclTableGroupBindPoint::PORT;
    addRestrictAclTable(&aclTableGroup);
    addBlockAclTable(&aclTableGroup);
    config->aclTableGroups()->push_back(std::move(aclTableGroup));

    for (const auto& [aclName, counterName] :
         std::initializer_list<std::pair<const char*, const char*>>{
             {kRestrictPermitAclName, kRestrictPermitCounterName},
             {kRestrictDenyAclName, kRestrictDenyCounterName},
             {kBlockPermitAclName, kBlockPermitCounterName},
             {kBlockDenyAclName, kBlockDenyCounterName}}) {
      utility::addAclStat(
          config,
          aclName,
          counterName,
          utility::getAclCounterTypes(getAgentEnsemble()->getL3Asics()));
    }
  }

  void bindPortToAclTable(
      cfg::SwitchConfig* config,
      PortID port,
      folly::StringPiece aclTableName) {
    utility::findCfgPort(*config, port)->ingressAclTableName() =
        aclTableName.str();
  }

  void configurePortBoundAcl(
      const std::vector<PortID>& restrictPorts,
      PortID blockPort) {
    auto config = initialConfig(*getAgentEnsemble());
    addPortBoundAclTableGroup(&config);
    for (const auto& restrictPort : restrictPorts) {
      bindPortToAclTable(&config, restrictPort, kRestrictAclTableName);
    }
    bindPortToAclTable(&config, blockPort, kBlockAclTableName);

    XLOG(INFO) << "Configuring port-bound ingress ACL group "
               << kAclTableGroupName << ": " << restrictPorts.size()
               << " port(s) -> table " << kRestrictAclTableName << ", port "
               << blockPort << " -> table " << kBlockAclTableName;
    applyNewConfig(config);
  }

  void sendPacket(
      PortID ingressPort,
      uint16_t l4DstPort,
      L4Protocol protocol = L4Protocol::UDP) {
    auto vlanId = getVlanIDForTx();
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    auto packet = protocol == L4Protocol::UDP ? utility::makeUDPTxPacket(
                                                    getSw(),
                                                    vlanId,
                                                    srcMac,
                                                    intfMac,
                                                    folly::IPAddressV6(kSrcIp),
                                                    folly::IPAddressV6(kDstIp),
                                                    kL4SrcPort,
                                                    l4DstPort,
                                                    0,
                                                    255)
                                              : utility::makeTCPTxPacket(
                                                    getSw(),
                                                    vlanId,
                                                    srcMac,
                                                    intfMac,
                                                    folly::IPAddressV6(kSrcIp),
                                                    folly::IPAddressV6(kDstIp),
                                                    kL4SrcPort,
                                                    l4DstPort,
                                                    0,
                                                    255);
    getSw()->sendPacketOutOfPortAsync(std::move(packet), ingressPort);
  }

  int64_t getPortCounter(PortID portId, folly::StringPiece counterName) const {
    auto port = getProgrammedState()->getPorts()->getNode(portId);
    auto key = fmt::format("{}.{}.sum", port->getName(), counterName);
    return getAgentEnsemble()->getFb303Counter(
        key, scopeResolver().scope(portId).switchId());
  }

  void verifyAclPacket(
      folly::StringPiece caseName,
      PortID ingressPort,
      uint16_t l4DstPort,
      const std::string& permitCounterName,
      const std::string& denyCounterName,
      bool expectPermit,
      L4Protocol protocol = L4Protocol::UDP) {
    const auto egressPort = masterLogicalPortIds()[0];
    const auto permitCounterBefore =
        utility::getAclInOutPackets(getSw(), permitCounterName);
    const auto denyCounterBefore =
        utility::getAclInOutPackets(getSw(), denyCounterName);
    const auto egressPacketsBefore =
        getPortCounter(egressPort, kOutUnicastPktsCounterName);

    XLOG(INFO) << "[PortBoundIngressAcl][" << caseName << "] Sending IPv6 "
               << (protocol == L4Protocol::UDP ? "UDP" : "TCP") << " packet "
               << kSrcIp << ":" << kL4SrcPort << " -> " << kDstIp << ":"
               << l4DstPort << " on ingress port " << ingressPort
               << "; expected " << (expectPermit ? "PERMIT" : "DROP");
    sendPacket(ingressPort, l4DstPort, protocol);
    WITH_RETRIES({
      if (expectPermit) {
        EXPECT_EVENTUALLY_GE(
            utility::getAclInOutPackets(getSw(), permitCounterName),
            permitCounterBefore + 1);
        EXPECT_EVENTUALLY_EQ(
            utility::getAclInOutPackets(getSw(), denyCounterName),
            denyCounterBefore);
        EXPECT_EVENTUALLY_GT(
            getPortCounter(egressPort, kOutUnicastPktsCounterName),
            egressPacketsBefore);
      } else {
        EXPECT_EVENTUALLY_EQ(
            utility::getAclInOutPackets(getSw(), permitCounterName),
            permitCounterBefore);
        EXPECT_EVENTUALLY_GE(
            utility::getAclInOutPackets(getSw(), denyCounterName),
            denyCounterBefore + 1);
        EXPECT_EVENTUALLY_EQ(
            getPortCounter(egressPort, kOutUnicastPktsCounterName),
            egressPacketsBefore);
      }
    });

    const auto permitCounterAfter =
        utility::getAclInOutPackets(getSw(), permitCounterName);
    const auto denyCounterAfter =
        utility::getAclInOutPackets(getSw(), denyCounterName);
    const auto egressPacketsAfter =
        getPortCounter(egressPort, kOutUnicastPktsCounterName);
    XLOG(INFO) << "[PortBoundIngressAcl][" << caseName << "] Observed "
               << (egressPacketsAfter > egressPacketsBefore ? "PERMITTED"
                                                            : "DROPPED")
               << "; permit counter " << permitCounterBefore << " -> "
               << permitCounterAfter << "; deny counter " << denyCounterBefore
               << " -> " << denyCounterAfter << "; egress packets "
               << egressPacketsBefore << " -> " << egressPacketsAfter;
  }

  void verifyUnboundPacket(PortID ingressPort, uint16_t l4DstPort) {
    const auto egressPort = masterLogicalPortIds()[0];
    const auto restrictPermitCounterBefore =
        utility::getAclInOutPackets(getSw(), kRestrictPermitCounterName);
    const auto restrictDenyCounterBefore =
        utility::getAclInOutPackets(getSw(), kRestrictDenyCounterName);
    const auto blockPermitCounterBefore =
        utility::getAclInOutPackets(getSw(), kBlockPermitCounterName);
    const auto blockDenyCounterBefore =
        utility::getAclInOutPackets(getSw(), kBlockDenyCounterName);
    const auto egressPacketsBefore =
        getPortCounter(egressPort, kOutUnicastPktsCounterName);

    XLOG(INFO) << "[PortBoundIngressAcl][Unbound] Sending IPv6 UDP packet "
               << kSrcIp << ":" << kL4SrcPort << " -> " << kDstIp << ":"
               << l4DstPort << " on unbound ingress port " << ingressPort
               << "; expected PERMIT without matching either table";
    sendPacket(ingressPort, l4DstPort);
    WITH_RETRIES({
      EXPECT_EVENTUALLY_EQ(
          utility::getAclInOutPackets(getSw(), kRestrictPermitCounterName),
          restrictPermitCounterBefore);
      EXPECT_EVENTUALLY_EQ(
          utility::getAclInOutPackets(getSw(), kRestrictDenyCounterName),
          restrictDenyCounterBefore);
      EXPECT_EVENTUALLY_EQ(
          utility::getAclInOutPackets(getSw(), kBlockPermitCounterName),
          blockPermitCounterBefore);
      EXPECT_EVENTUALLY_EQ(
          utility::getAclInOutPackets(getSw(), kBlockDenyCounterName),
          blockDenyCounterBefore);
      EXPECT_EVENTUALLY_GT(
          getPortCounter(egressPort, kOutUnicastPktsCounterName),
          egressPacketsBefore);
    });
  }

  void verifyPacketForwarded(
      folly::StringPiece caseName,
      PortID ingressPort,
      uint16_t l4DstPort) {
    const auto egressPort = masterLogicalPortIds()[0];
    const auto egressPacketsBefore =
        getPortCounter(egressPort, kOutUnicastPktsCounterName);
    XLOG(INFO) << "[PortBoundIngressAclWarmboot][" << caseName
               << "] Sending IPv6 UDP packet " << kSrcIp << ":" << kL4SrcPort
               << " -> " << kDstIp << ":" << l4DstPort << " on ingress port "
               << ingressPort << "; expected PERMIT";
    sendPacket(ingressPort, l4DstPort);
    WITH_RETRIES({
      EXPECT_EVENTUALLY_GT(
          getPortCounter(egressPort, kOutUnicastPktsCounterName),
          egressPacketsBefore);
    });
  }

  void configureDefaultSwitchAcl() {
    auto config = initialConfig(*getAgentEnsemble());
    utility::addAclTableGroup(
        &config, cfg::AclStage::INGRESS, utility::kDefaultAclTableGroupName());
    utility::addDefaultAclTable(config);
    applyNewConfig(config);
    XLOG(INFO)
        << "[PortBoundIngressAclWarmboot][Cold setup] Applied only the default "
        << "switch-bound ingress ACL table; no port has an ACL binding";
  }

  void configurePortBoundAclAfterWarmboot(PortID restrictPort) {
    auto config = getAgentEnsemble()->getCurrentConfig();
    addPortBoundAclTableGroup(&config);
    bindPortToAclTable(&config, restrictPort, kRestrictAclTableName);

    applyNewConfig(config);
    XLOG(INFO) << "[PortBoundIngressAclWarmboot][Warm setup] Applied group "
               << kAclTableGroupName << " with restrict table "
               << kRestrictAclTableName << " on port " << restrictPort;
  }

  void verifyDefaultSwitchAclTraffic() {
    const auto& ports = masterLogicalPortIds();
    verifyPacketForwarded(
        "Default table, port A, UDP/53", ports[1], kRestrictPermitL4DstPort);
    verifyPacketForwarded(
        "Default table, port B, UDP/54", ports[2], kDeniedL4DstPort);
  }

  void verifyPortBoundAclAddedAfterWarmboot() {
    const auto& ports = masterLogicalPortIds();
    const auto restrictPort = ports[1];
    const auto unboundPort = ports[2];

    verifyAclPacket(
        "Bound UDP/53",
        restrictPort,
        kRestrictPermitL4DstPort,
        kRestrictPermitCounterName,
        kRestrictDenyCounterName,
        true);
    verifyAclPacket(
        "Bound UDP/54",
        restrictPort,
        kDeniedL4DstPort,
        kRestrictPermitCounterName,
        kRestrictDenyCounterName,
        false);
    verifyUnboundPacket(unboundPort, kRestrictPermitL4DstPort);
    verifyUnboundPacket(unboundPort, kDeniedL4DstPort);
  }

  void replaceRestrictAclTableWithBlockAclTable() {
    auto config = getAgentEnsemble()->getCurrentConfig();
    const auto& ports = masterLogicalPortIds();

    for (const auto& restrictPort : {ports[1], ports[3]}) {
      bindPortToAclTable(&config, restrictPort, kBlockAclTableName);
    }
    utility::delAclStat(
        &config, kRestrictPermitAclName, kRestrictPermitCounterName);
    utility::delAclStat(
        &config, kRestrictDenyAclName, kRestrictDenyCounterName);
    utility::delAclTable(&config, kRestrictAclTableName);

    XLOG(INFO) << "[PortBoundIngressAclReplace] Rebinding ports " << ports[1]
               << " and " << ports[3] << " from " << kRestrictAclTableName
               << " to " << kBlockAclTableName << " and removing "
               << kRestrictAclTableName << " in the same config update";
    applyNewConfig(config);
  }

  void addProtocolQualifierToRestrictAclTable() {
    auto config = getAgentEnsemble()->getCurrentConfig();
    cfg::AclTable* restrictAclTable{nullptr};
    for (auto& aclTableGroup : *config.aclTableGroups()) {
      if (*aclTableGroup.name() != kAclTableGroupName) {
        continue;
      }
      for (auto& aclTable : *aclTableGroup.aclTables()) {
        if (*aclTable.name() == kRestrictAclTableName) {
          restrictAclTable = &aclTable;
          break;
        }
      }
    }
    CHECK(restrictAclTable);
    restrictAclTable->qualifiers() = {
        cfg::AclTableQualifier::IP_PROTOCOL_NUMBER,
        cfg::AclTableQualifier::L4_DST_PORT};

    auto permitAcl = std::find_if(
        restrictAclTable->aclEntries()->begin(),
        restrictAclTable->aclEntries()->end(),
        [](const auto& acl) { return *acl.name() == kRestrictPermitAclName; });
    CHECK(permitAcl != restrictAclTable->aclEntries()->end());
    permitAcl->proto() = 17;

    XLOG(INFO) << "[PortBoundIngressAclQualifierChange] Adding the IP protocol "
                  "qualifier to "
               << kRestrictAclTableName << " while it remains bound to port "
               << masterLogicalPortIds()[1];
    applyNewConfig(config);

    // Wait for stats from the replacement table before measuring traffic.
    WITH_RETRIES({
      EXPECT_EVENTUALLY_EQ(
          utility::getAclInOutPackets(getSw(), kRestrictPermitCounterName), 0);
      EXPECT_EVENTUALLY_EQ(
          utility::getAclInOutPackets(getSw(), kRestrictDenyCounterName), 0);
    });
  }

  void verifyPortUsesBlockAclTable(PortID ingressPort) {
    ASSERT_EQ(
        getProgrammedState()
            ->getPorts()
            ->getNode(ingressPort)
            ->getIngressAclTableName(),
        kBlockAclTableName);
    verifyAclPacket(
        "Rebound port block permit",
        ingressPort,
        kBlockPermitL4DstPort,
        kBlockPermitCounterName,
        kBlockDenyCounterName,
        true);
    verifyAclPacket(
        "Rebound port block deny",
        ingressPort,
        kRestrictPermitL4DstPort,
        kBlockPermitCounterName,
        kBlockDenyCounterName,
        false);
  }

  void verifyReplacementSetup() {
    const auto& ports = masterLogicalPortIds();
    for (const auto& restrictPort : {ports[1], ports[3]}) {
      ASSERT_EQ(
          getProgrammedState()
              ->getPorts()
              ->getNode(restrictPort)
              ->getIngressAclTableName(),
          kRestrictAclTableName);
    }
    ASSERT_EQ(
        getProgrammedState()
            ->getPorts()
            ->getNode(ports[2])
            ->getIngressAclTableName(),
        kBlockAclTableName);
  }

  void verifyReplacement() {
    const auto& ports = masterLogicalPortIds();
    const auto aclTableGroup =
        getProgrammedState()->getAclTableGroups()->getNodeIf(
            cfg::AclStage::INGRESS);
    ASSERT_NE(aclTableGroup, nullptr);
    ASSERT_EQ(
        aclTableGroup->getAclTableMap()->getTableIf(kRestrictAclTableName),
        nullptr);
    for (const auto& port : {ports[1], ports[2], ports[3]}) {
      verifyPortUsesBlockAclTable(port);
    }
  }

  void verifyPortBoundAcl() {
    const auto& ports = masterLogicalPortIds();
    const auto restrictPort = ports[1];
    const auto blockPort = ports[2];
    const auto unboundPort = ports[3];

    verifyAclPacket(
        "Restrict permit",
        restrictPort,
        kRestrictPermitL4DstPort,
        kRestrictPermitCounterName,
        kRestrictDenyCounterName,
        true);
    verifyAclPacket(
        "Restrict deny",
        restrictPort,
        kDeniedL4DstPort,
        kRestrictPermitCounterName,
        kRestrictDenyCounterName,
        false);
    verifyAclPacket(
        "Block permit",
        blockPort,
        kBlockPermitL4DstPort,
        kBlockPermitCounterName,
        kBlockDenyCounterName,
        true);
    verifyAclPacket(
        "Block deny",
        blockPort,
        kRestrictPermitL4DstPort,
        kBlockPermitCounterName,
        kBlockDenyCounterName,
        false);
    verifyUnboundPacket(unboundPort, kDeniedL4DstPort);
  }

  static constexpr auto kAclTableGroupName = "port-ingress-acl-group";
  static constexpr auto kRestrictAclTableName =
      "port-ingress-restrict-acl-table";
  static constexpr auto kBlockAclTableName = "port-ingress-block-acl-table";
  static constexpr auto kRestrictPermitAclName = "restrict-permit-53";
  static constexpr auto kRestrictDenyAclName = "restrict-deny";
  static constexpr auto kBlockPermitAclName = "block-permit-dhcp-67";
  static constexpr auto kBlockDenyAclName = "block-deny";
  static constexpr auto kRestrictPermitCounterName = "restrict-permit-53";
  static constexpr auto kRestrictDenyCounterName = "restrict-deny";
  static constexpr auto kBlockPermitCounterName = "block-permit-dhcp-67";
  static constexpr auto kBlockDenyCounterName = "block-deny";
  static constexpr auto kOutUnicastPktsCounterName = "out_unicast_pkts";
  static constexpr auto kSrcIp = "2620:0:1cfe:face:b00c::1";
  static constexpr auto kDstIp = "2620:0:1cfe:face:b00c::10";
  static constexpr uint16_t kL4SrcPort = 8000;
  static constexpr uint16_t kRestrictPermitL4DstPort = 53;
  static constexpr uint16_t kBlockPermitL4DstPort = 67;
  static constexpr uint16_t kDeniedL4DstPort = 54;
};

// Two ports select different tables from the same port-bound ingress group.
// The restrict table permits UDP/53, the block table permits UDP/67, and both
// tables deny unmatched traffic. A third, unbound port remains unrestricted.
TEST_F(AgentPortBoundIngressAclTest, VerifyPortBoundAclTraffic) {
  auto setup = [this]() {
    setupL3Forwarding();
    configurePortBoundAcl(
        {masterLogicalPortIds()[1]}, masterLogicalPortIds()[2]);
  };
  auto verify = [this]() { verifyPortBoundAcl(); };

  verifyAcrossWarmBoots(setup, verify);
}

// Cold boot uses only the switch-bound default table. After warm boot, the
// port-bound group is added and its restrict table is attached to one port.
TEST_F(
    AgentPortBoundIngressAclTest,
    VerifyAddPortBoundIngressAclAfterWarmboot) {
  auto setup = [this]() {
    setupL3Forwarding();
    configureDefaultSwitchAcl();
  };
  auto verify = [this]() { verifyDefaultSwitchAclTraffic(); };
  auto setupPostWarmboot = [this]() {
    configurePortBoundAclAfterWarmboot(masterLogicalPortIds()[1]);
  };
  auto verifyPostWarmboot = [this]() {
    verifyPortBoundAclAddedAfterWarmboot();
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

// Removing a port binding updates the SAI port before ACL processing. The
// hardware attribute must be explicitly cleared while the table remains.
TEST_F(AgentPortBoundIngressAclTest, VerifyUnbindPortBoundIngressAcl) {
  const auto restrictPort = masterLogicalPortIds()[1];
  auto setup = [=, this]() {
    setupL3Forwarding();
    configurePortBoundAcl({restrictPort}, masterLogicalPortIds()[2]);
  };
  auto verify = [=, this]() {
    verifyAclPacket(
        "Before unbind deny",
        restrictPort,
        kDeniedL4DstPort,
        kRestrictPermitCounterName,
        kRestrictDenyCounterName,
        false);
  };
  auto setupPostWarmboot = [=, this]() {
    auto config = getAgentEnsemble()->getCurrentConfig();
    auto port = utility::findCfgPort(config, restrictPort);
    ASSERT_TRUE(port->ingressAclTableName().has_value());
    port->ingressAclTableName().reset();
    applyNewConfig(config);
  };
  auto verifyPostWarmboot = [=, this]() {
    EXPECT_FALSE(
        getProgrammedState()
            ->getPorts()
            ->getNode(restrictPort)
            ->getIngressAclTableName()
            .has_value());
    verifyUnboundPacket(restrictPort, kDeniedL4DstPort);
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

// A create-only profile change recreates the SAI ports. Their restrict and
// block table bindings must be restored on the replacement port objects.
TEST_F(AgentPortBoundIngressAclTest, VerifyPortBoundAclAfterPortRecreate) {
  const auto& ports = masterLogicalPortIds();
  ASSERT_GE(ports.size(), 4);
  const auto restrictPort = ports[1];
  const auto blockPort = ports[2];
  // This test is enabled only on Cisco Q200 and reproduces the original
  // 100G-to-200G port recreation that lost the ACL binding.
  constexpr auto kRecreateProfile =
      cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_COPPER;

  if (!getSw()->getHwAsicTable()->isFeatureSupportedOnAllAsic(
          HwAsic::Feature::SAI_PORT_SPEED_CHANGE)) {
    GTEST_SKIP() << "Port speed changes do not recreate SAI ports";
  }

  auto* platformMapping = getAgentEnsemble()->getPlatformMapping();
  for (const auto& port : {restrictPort, blockPort}) {
    const auto& platformPort = platformMapping->getPlatformPort(port);
    const auto profileMatcher =
        PlatformPortProfileConfigMatcher(kRecreateProfile, port);
    if (platformPort.supportedProfiles()->find(kRecreateProfile) ==
            platformPort.supportedProfiles()->end() ||
        !platformMapping->getPortProfileConfig(profileMatcher)) {
      GTEST_SKIP() << "Port " << port << " does not support profile "
                   << apache::thrift::util::enumNameSafe(kRecreateProfile);
    }
    const auto portState = getProgrammedState()->getPorts()->getNode(port);
    if (portState->getProfileID() == kRecreateProfile) {
      GTEST_SKIP() << "Port " << port << " already uses the recreate profile";
    }
    if (portState->getSpeed() == cfg::PortSpeed::TWOHUNDREDG) {
      GTEST_SKIP() << "Port " << port
                   << " already uses a 200G profile, so the test cannot "
                      "force a create-only speed change";
    }
  }

  for (size_t i = 0; i < 4; ++i) {
    const auto portGroup =
        utility::getAllPortsInGroup(platformMapping, ports[i]);
    for (size_t j = i + 1; j < 4; ++j) {
      if (std::find(portGroup.begin(), portGroup.end(), ports[j]) !=
          portGroup.end()) {
        GTEST_SKIP() << "Selected traffic ports " << ports[i] << " and "
                     << ports[j] << " share one port group";
      }
    }
  }

  auto setup = [=, this]() {
    setupL3Forwarding();
    configurePortBoundAcl({restrictPort}, blockPort);
  };
  auto verify = [this]() {
    XLOG(INFO) << "[PortBoundIngressAclRecreate][Before recreate] Verifying "
                  "both port ACL bindings";
    verifyPortBoundAcl();
  };
  auto setupPostWarmboot = [=, this]() {
    auto config = getAgentEnsemble()->getCurrentConfig();
    for (const auto& port : {restrictPort, blockPort}) {
      const auto portConfig = utility::findCfgPort(config, port);
      ASSERT_TRUE(
          apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
              portConfig->profileID()));
      ASSERT_TRUE(portConfig->ingressAclTableName().has_value());
      XLOG(INFO) << "[PortBoundIngressAclRecreate] Changing port " << port
                 << " from profile "
                 << apache::thrift::util::enumNameSafe(*portConfig->profileID())
                 << " to "
                 << apache::thrift::util::enumNameSafe(kRecreateProfile)
                 << " while keeping ingress ACL table "
                 << *portConfig->ingressAclTableName();
      utility::configurePortProfile(
          platformMapping,
          getAgentEnsemble()->supportsAddRemovePort(),
          config,
          kRecreateProfile,
          utility::getAllPortsInGroup(platformMapping, port),
          port);
    }
    applyNewConfig(config);
    for (const auto& port : {restrictPort, blockPort}) {
      EXPECT_EQ(
          getProgrammedState()->getPorts()->getNode(port)->getProfileID(),
          kRecreateProfile);
    }
  };
  auto verifyPostWarmboot = [this]() {
    XLOG(INFO) << "[PortBoundIngressAclRecreate][After recreate] Verifying "
                  "both recreated ports remain restricted";
    verifyPortBoundAcl();
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

// Rebind two ports from the restrict table to the existing block table before
// removing the obsolete restrict table in the same update.
TEST_F(
    AgentPortBoundIngressAclTest,
    VerifyReplaceAndRemovePortBoundAclInOneUpdate) {
  auto setup = [this]() {
    setupL3Forwarding();
    const auto& ports = masterLogicalPortIds();
    configurePortBoundAcl({ports[1], ports[3]}, ports[2]);
  };
  auto verify = [this]() { verifyReplacementSetup(); };
  auto setupPostWarmboot = [this]() {
    replaceRestrictAclTableWithBlockAclTable();
  };
  auto verifyPostWarmboot = [this]() { verifyReplacement(); };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

// Adding a qualifier recreates the bound SAI ACL table. UDP/53 remains
// permitted, while TCP/53 stops matching the now UDP-specific permit entry.
TEST_F(AgentPortBoundIngressAclTest, VerifyPortBoundAclTableQualifierChange) {
  const auto restrictPort = masterLogicalPortIds()[1];
  auto setup = [=, this]() {
    setupL3Forwarding();
    configurePortBoundAcl({restrictPort}, masterLogicalPortIds()[2]);
  };
  auto verify = [=, this]() {
    verifyAclPacket(
        "Before qualifier change TCP/53",
        restrictPort,
        kRestrictPermitL4DstPort,
        kRestrictPermitCounterName,
        kRestrictDenyCounterName,
        true,
        L4Protocol::TCP);
  };
  auto setupPostWarmboot = [this]() {
    addProtocolQualifierToRestrictAclTable();
  };
  auto verifyPostWarmboot = [=, this]() {
    verifyAclPacket(
        "After qualifier change UDP/53",
        restrictPort,
        kRestrictPermitL4DstPort,
        kRestrictPermitCounterName,
        kRestrictDenyCounterName,
        true);
    verifyAclPacket(
        "After qualifier change TCP/53",
        restrictPort,
        kRestrictPermitL4DstPort,
        kRestrictPermitCounterName,
        kRestrictDenyCounterName,
        false,
        L4Protocol::TCP);
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

} // namespace facebook::fboss
