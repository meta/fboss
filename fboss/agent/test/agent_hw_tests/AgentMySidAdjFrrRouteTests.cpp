// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/ThriftHandler.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/packet/Ethertype.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/state/AggregatePort.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/TrunkUtils.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/test/utils/Srv6TestUtils.h"
#include "fboss/agent/test/utils/TrapPacketUtils.h"
#include "fboss/lib/CommonUtils.h"

#include <fmt/format.h>

#include <set>
#include <utility>

namespace facebook::fboss {

class AgentMySidAdjFrrRouteTest : public AgentHwTest {
 protected:
  static constexpr int kNumLags{4};
  // Lag the protected SID's own adjacency is wired to; the rest are backups.
  static constexpr int kPrimaryLag{0};
  static constexpr uint8_t kMySidPrefixLen{48};
  static constexpr uint8_t kTrafficClass{0xa8};
  static constexpr auto kLocatorPrefix{"fdad:ffff::/32"};
  static constexpr auto kSrv6TunnelId{"srv6Tunnel0"};
  static constexpr auto kShiftedProtectedSidPktDst{"fdad:ffff:f::"};

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::SRV6_MIDPOINT,
        ProductionFeature::SRV6_ENCAP,
        ProductionFeature::MYSID_ADJACENCY_FRR,
        ProductionFeature::LAG};
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_enable_nexthop_id_manager = true;
    FLAGS_resolve_nexthops_from_id = true;
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /* interfaceHasSubnet */);
    for (int i = 0; i < kNumLags; ++i) {
      utility::addAggPort(
          i + 1,
          {static_cast<int32_t>(ensemble.masterLogicalPortIds()[i])},
          &config);
    }
    config.loadBalancers() =
        utility::getEcmpFullWithFlowLabelTrunkFullWithFlowLabelHashConfig(
            ensemble.getL3Asics());
    config.mySidConfig() = makeAdjacencyMySidConfig();
    config.srv6Tunnels() = {utility::makeSrv6TunnelConfig(
        kSrv6TunnelId, InterfaceID(config.interfaces()[0].intfID().value()))};
    std::set<folly::CIDRNetwork> trapPrefixes{
        {folly::IPAddressV6(kShiftedProtectedSidPktDst), 128}};
    for (int i = kPrimaryLag + 1; i < kNumLags; ++i) {
      trapPrefixes.emplace(repairSid(i), 128);
    }
    utility::addTrapPacketAcl(
        checkSameAndGetAsicForTesting(ensemble.getL3Asics()),
        &config,
        trapPrefixes);
    return config;
  }

  void SetUp() override {
    AgentHwTest::SetUp();
    if (FLAGS_list_production_feature) {
      return;
    }

    applyNewState(
        [](const std::shared_ptr<SwitchState> state) {
          return utility::enableTrunkPorts(state);
        },
        "enable trunk ports");

    auto ecmpHelper = makeEcmpHelper();
    boost::container::flat_set<PortDescriptor> lagPortDescs;
    for (int i = kPrimaryLag + 1; i < kNumLags; ++i) {
      lagPortDescs.emplace(lagPortDesc(i));
    }
    applyNewState(
        [&ecmpHelper,
         &lagPortDescs](const std::shared_ptr<SwitchState>& state) {
          return ecmpHelper.resolveNextHops(
              state, lagPortDescs, true /* useLinkLocal */);
        },
        "resolve adjacency mysid neighbors");

    for (int i = kPrimaryLag + 1; i < kNumLags; ++i) {
      utility::waitForMySidResolveOrUnresolve(
          [this]() { return getProgrammedState(); },
          mySidPrefix(i),
          kMySidPrefixLen,
          true /* resolved */);
    }
  }

  cfg::MySidConfig makeAdjacencyMySidConfig() const {
    cfg::MySidConfig mySidConfig;
    mySidConfig.locatorPrefix() = kLocatorPrefix;
    mySidConfig.locatorPrefixes() = {
        kLocatorPrefix, utility::kSrv6PlaneLocatorPrefix};
    for (int i = 0; i < kNumLags; ++i) {
      cfg::MySidEntryConfig entry;
      cfg::AdjacencyMySidConfig adjacency;
      adjacency.portName() = fmt::format("AGG-{}", i + 1);
      adjacency.isV6() = true;
      entry.adjacency() = std::move(adjacency);
      mySidConfig.entries()[i + 1] = std::move(entry);
    }
    return mySidConfig;
  }

  utility::EcmpSetupTargetedPorts<folly::IPAddressV6> makeEcmpHelper() {
    return utility::EcmpSetupTargetedPorts<folly::IPAddressV6>(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
  }

  PortDescriptor lagPortDesc(int index) const {
    return PortDescriptor(AggregatePortID(index + 1));
  }

  folly::IPAddressV6 mySidPrefix(int index) const {
    return folly::IPAddressV6(fmt::format("fdad:ffff:{:x}::", index + 1));
  }

  folly::IPAddressV6 backupNextHopAddress(int index) const {
    return folly::IPAddressV6(fmt::format("fdad::{:x}:1", index + 1));
  }

  folly::IPAddressV6 repairSid(int index) const {
    return folly::IPAddressV6(fmt::format("3001:db8:{:x}::", index + 1));
  }

  // uSID packet aimed at the protected SID: [locator fdad:ffff:]
  // [active uSID 1][next uSID f]. Function id 0xf is outside the configured
  // 1..kNumLags, so once uSID 1 is shifted out the packet is not another
  // local SID on this box.
  folly::IPAddressV6 protectedSidPktDst() const {
    return folly::IPAddressV6("fdad:ffff:1:f::");
  }

  PortID getEgressPort(const PortDescriptor& portDesc) const {
    if (portDesc.isPhysicalPort()) {
      return portDesc.phyPortID();
    }
    auto aggPort = getProgrammedState()->getAggregatePorts()->getNodeIf(
        portDesc.aggPortID());
    return aggPort->sortedSubports().front().portID;
  }

  PortID findInjectPort(const std::vector<PortID>& egressPorts) {
    for (const auto& portMap :
         std::as_const(*getProgrammedState()->getPorts())) {
      for (const auto& [_, port] : std::as_const(*portMap.second)) {
        if (port->isPortUp() &&
            std::find(egressPorts.begin(), egressPorts.end(), port->getID()) ==
                egressPorts.end()) {
          return port->getID();
        }
      }
    }
    throw FbossError("No UP port found besides the mysid lag ports");
  }

  std::unique_ptr<TxPacket> makePacketToProtectedSid(
      uint32_t outerFlowLabel = 0) {
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    return utility::makeIpInIpTxPacket(
        getSw(),
        getVlanIDForTx().value(),
        intfMac,
        intfMac,
        folly::IPAddressV6("100::1") /* outerSrc */,
        protectedSidPktDst() /* outerDst */,
        folly::IPAddressV6("2001:db8::1") /* innerSrc */,
        folly::IPAddressV6("2001:db8::2") /* innerDst */,
        8000 /* srcPort */,
        8001 /* dstPort */,
        kTrafficClass,
        0 /* innerTrafficClass */,
        64 /* hopLimit */,
        64 /* innerHopLimit */,
        outerFlowLabel);
  }

  void sendPacketToProtectedSid(
      PortID injectPort,
      uint32_t outerFlowLabel = 0) {
    auto txPacket = makePacketToProtectedSid(outerFlowLabel);
    getSw()->sendPacketOutOfPortAsync(std::move(txPacket), injectPort);
  }

  std::vector<PortID> allLagPorts() const {
    std::vector<PortID> ports;
    ports.reserve(kNumLags);
    for (int i = 0; i < kNumLags; ++i) {
      ports.push_back(getEgressPort(lagPortDesc(i)));
    }
    return ports;
  }

  std::vector<int> backupLags() const {
    std::vector<int> lags;
    for (int i = kPrimaryLag + 1; i < kNumLags; ++i) {
      lags.push_back(i);
    }
    return lags;
  }

  // Sends one packet at the protected SID and asserts exactly one of
  // `liveLags` carried it while every lag in `downLags` stayed flat.
  void verifyForwardedViaOneOfLags(
      const std::vector<int>& liveLags,
      const std::vector<int>& downLags) {
    auto lagPorts = allLagPorts();
    auto injectPort = findInjectPort(lagPorts);

    std::vector<int64_t> bytesBefore(kNumLags);
    for (int i = 0; i < kNumLags; ++i) {
      bytesBefore[i] = *getLatestPortStats(lagPorts[i]).outBytes_();
    }

    sendPacketToProtectedSid(injectPort);

    WITH_RETRIES({
      int carried = 0;
      for (auto lag : liveLags) {
        if (*getLatestPortStats(lagPorts[lag]).outBytes_() > bytesBefore[lag]) {
          ++carried;
        }
      }
      EXPECT_EVENTUALLY_EQ(carried, 1);
    });

    for (auto lag : downLags) {
      EXPECT_EQ(
          *getLatestPortStats(lagPorts[lag]).outBytes_(), bytesBefore[lag])
          << "traffic egressed lag " << lag << " on port " << lagPorts[lag]
          << ", which should not be carrying";
    }
  }

  void verifyShiftedPacket(
      const utility::EthFrame& originalFrame,
      const utility::IPv6Packet& shiftedPacket) {
    // End.X consumes the active uSID and performs one routing hop. Everything
    // below that outer IPv6 header must remain unchanged.
    const auto originalV6 = originalFrame.v6PayLoad();
    ASSERT_TRUE(originalV6.has_value());
    auto expectedHeader = originalV6->header();
    expectedHeader.decrementTTL();
    expectedHeader.dstAddr = folly::IPAddressV6(kShiftedProtectedSidPktDst);
    EXPECT_EQ(shiftedPacket.header(), expectedHeader);

    const auto* originalInnerV6 = originalV6->v6PayLoad();
    const auto* shiftedInnerV6 = shiftedPacket.v6PayLoad();
    ASSERT_NE(originalInnerV6, nullptr);
    ASSERT_NE(shiftedInnerV6, nullptr);
    EXPECT_EQ(*shiftedInnerV6, *originalInnerV6);
  }

  void verifyShiftedPacketOnLag(int lag) {
    // The primary and plain-IP backup paths emit the End.X result directly,
    // so capture the packet on the selected LAG and verify it as-is.
    const auto egressPort = getEgressPort(lagPortDesc(lag));
    const auto injectPort = findInjectPort(allLagPorts());
    const auto bytesBefore = *getLatestPortStats(egressPort).outBytes_();
    utility::SwSwitchPacketSnooper snooper(
        getSw(), "mySidShiftedPacketSnooper", egressPort);

    auto txPacket = makePacketToProtectedSid();
    const auto originalFrame =
        utility::makeEthFrame(*txPacket, true /* skipTtlDecrement */);
    getSw()->sendPacketOutOfPortAsync(std::move(txPacket), injectPort);

    auto capturedFrame = snooper.waitForPacket(1);
    WITH_RETRIES({
      EXPECT_EVENTUALLY_GT(
          *getLatestPortStats(egressPort).outBytes_(), bytesBefore);
      if (!capturedFrame.has_value()) {
        capturedFrame = snooper.waitForPacket(1);
      }
      EXPECT_EVENTUALLY_TRUE(capturedFrame.has_value());
    });

    ASSERT_TRUE(capturedFrame.has_value());
    folly::io::Cursor cursor(capturedFrame->get());
    utility::EthFrame frame(cursor);
    EXPECT_EQ(
        frame.header().etherType,
        static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6));

    const auto capturedV6 = frame.v6PayLoad();
    ASSERT_TRUE(capturedV6.has_value());
    verifyShiftedPacket(originalFrame, *capturedV6);
  }

  void verifyForwardedViaSrv6Backup(
      const std::vector<int>& liveLags,
      const std::vector<int>& downLags) {
    // A repair path wraps the End.X result in a new IPv6 header whose
    // destination is the selected backup's repair SID, whose hop limit and
    // traffic class are copied from the shifted packet, and whose flow label
    // carries nonzero entropy. Strip that header and apply the same
    // verification used for direct primary forwarding.
    verifyForwardedViaOneOfLags(liveLags, downLags);

    utility::SwSwitchPacketSnooper snooper(getSw(), "mySidSrv6BackupSnooper");
    std::map<PortID, int64_t> bytesBefore;
    for (const auto lag : liveLags) {
      const auto port = getEgressPort(lagPortDesc(lag));
      bytesBefore[port] = *getLatestPortStats(port).outBytes_();
    }
    auto txPacket = makePacketToProtectedSid();
    const auto originalFrame =
        utility::makeEthFrame(*txPacket, true /* skipTtlDecrement */);
    getSw()->sendPacketOutOfPortAsync(
        std::move(txPacket), findInjectPort(allLagPorts()));

    auto capturedFrame = snooper.waitForPacket(1);
    WITH_RETRIES({
      bool forwarded{false};
      for (const auto& [port, bytes] : bytesBefore) {
        if (*getLatestPortStats(port).outBytes_() > bytes) {
          forwarded = true;
          break;
        }
      }
      EXPECT_EVENTUALLY_TRUE(forwarded);
      if (!capturedFrame.has_value()) {
        capturedFrame = snooper.waitForPacket(1);
      }
      EXPECT_EVENTUALLY_TRUE(capturedFrame.has_value());
    });
    ASSERT_TRUE(capturedFrame.has_value());
    folly::io::Cursor cursor(capturedFrame->get());
    utility::EthFrame frame(cursor);
    const auto encapsulatingV6 = frame.v6PayLoad();
    ASSERT_TRUE(encapsulatingV6.has_value());

    std::vector<folly::IPAddressV6> expectedRepairSids;
    expectedRepairSids.reserve(liveLags.size());
    for (const auto lag : liveLags) {
      expectedRepairSids.push_back(repairSid(lag));
    }
    EXPECT_NE(
        std::find(
            expectedRepairSids.begin(),
            expectedRepairSids.end(),
            encapsulatingV6->header().dstAddr),
        expectedRepairSids.end());

    const auto* shiftedPacket = encapsulatingV6->v6PayLoad();
    ASSERT_NE(shiftedPacket, nullptr);
    EXPECT_EQ(
        encapsulatingV6->header().hopLimit, shiftedPacket->header().hopLimit);
    EXPECT_EQ(
        encapsulatingV6->header().trafficClass,
        shiftedPacket->header().trafficClass);
    EXPECT_NE(encapsulatingV6->header().flowLabel, 0);
    verifyShiftedPacket(originalFrame, *shiftedPacket);
  }

  // With the primary adjacency up, FRR backups are programmed but must not
  // carry traffic: the packet has to leave via the protected SID's own
  // adjacency (AGG-1) and none of the backup lags.
  void verifyForwardedViaPrimary() {
    verifyForwardedViaOneOfLags({kPrimaryLag} /* liveLags */, backupLags());
    verifyShiftedPacketOnLag(kPrimaryLag);
  }

  void pumpTrafficAndVerifyLoadBalancedAcrossLags(
      const std::vector<int>& liveLags,
      const std::vector<int>& downLags) {
    constexpr int kNumPackets{10000};
    constexpr int kMaxDeviationPct{25};
    auto lagPorts = allLagPorts();
    auto injectPort = findInjectPort(lagPorts);

    std::vector<PortID> livePorts;
    livePorts.reserve(liveLags.size());
    for (const auto lag : liveLags) {
      livePorts.push_back(lagPorts[lag]);
    }
    const auto liveStatsBefore = getLatestPortStats(livePorts);
    std::vector<int64_t> downBytesBefore;
    downBytesBefore.reserve(downLags.size());
    for (const auto lag : downLags) {
      downBytesBefore.push_back(*getLatestPortStats(lagPorts[lag]).outBytes_());
    }

    for (uint32_t flowLabel = 1; flowLabel <= kNumPackets; ++flowLabel) {
      sendPacketToProtectedSid(injectPort, flowLabel);
    }
    WITH_RETRIES({
      const auto liveStatsAfter = getLatestPortStats(livePorts);
      const auto [highest, lowest] = utility::getHighestAndLowestBytesIncrement(
          liveStatsBefore, liveStatsAfter);
      int64_t forwardedPackets{0};
      for (const auto& [port, statsBefore] : liveStatsBefore) {
        forwardedPackets += *liveStatsAfter.at(port).outUnicastPkts__ref() -
            *statsBefore.outUnicastPkts__ref();
      }
      EXPECT_EVENTUALLY_EQ(forwardedPackets, kNumPackets);
      EXPECT_EVENTUALLY_TRUE(
          utility::isDeviationWithinThreshold(
              lowest, highest, kMaxDeviationPct));
    });

    for (size_t i = 0; i < downLags.size(); ++i) {
      const auto lag = downLags[i];
      EXPECT_EQ(
          *getLatestPortStats(lagPorts[lag]).outBytes_(), downBytesBefore[i])
          << "traffic egressed lag " << lag << " on port " << lagPorts[lag]
          << ", which should not be carrying";
    }
  }

  // Link down only, leaving the neighbor in place: FRR switchover is driven by
  // the hardware protection group off port state, so traffic has to move
  // before the control plane has withdrawn anything.
  void bringDownLagLink(int lag) {
    auto port = getEgressPort(lagPortDesc(lag));
    bringDownPort(port);
    XLOG(DBG2) << "Brought down lag " << lag << " link (port " << port << ")";
  }

  void unresolveLagNeighbor(int lag) {
    auto ecmpHelper = makeEcmpHelper();
    const boost::container::flat_set<PortDescriptor> lagPortDescs{
        lagPortDesc(lag)};
    applyNewState(
        [&ecmpHelper,
         &lagPortDescs](const std::shared_ptr<SwitchState>& state) {
          return ecmpHelper.unresolveNextHops(
              state, lagPortDescs, true /* useLinkLocal */);
        },
        "unresolve adjacency mysid neighbor");
    utility::waitForMySidResolveOrUnresolve(
        [this]() { return getProgrammedState(); },
        mySidPrefix(lag),
        kMySidPrefixLen,
        false /* resolved */);
  }

  void bringUpLagLink(int lag) {
    auto port = getEgressPort(lagPortDesc(lag));
    bringUpPort(port);
    // The SAI fast-path link-down handler disables the LAG member. Since LACP
    // is not running in tests, toggle forwarding state to generate the delta
    // that re-enables it before restoring the neighbor.
    const auto aggPortId = lagPortDesc(lag).aggPortID();
    auto setLagForwardingState = [aggPortId, port](
                                     const std::shared_ptr<SwitchState>& state,
                                     AggregatePort::Forwarding forwarding) {
      auto newState = state;
      auto aggPort =
          newState->getAggregatePorts()->getNode(aggPortId)->modify(&newState);
      aggPort->setForwardingState(port, forwarding);
      return newState;
    };
    applyNewState(
        [&setLagForwardingState](const std::shared_ptr<SwitchState>& state) {
          return setLagForwardingState(
              state, AggregatePort::Forwarding::DISABLED);
        },
        "disable lag member to sync with SAI state");
    applyNewState(
        [&setLagForwardingState](const std::shared_ptr<SwitchState>& state) {
          return setLagForwardingState(
              state, AggregatePort::Forwarding::ENABLED);
        },
        "re-enable lag member after link flap");
  }

  void resolveLagNeighbor(int lag) {
    auto ecmpHelper = makeEcmpHelper();
    const boost::container::flat_set<PortDescriptor> lagPortDescs{
        lagPortDesc(lag)};
    applyNewState(
        [&ecmpHelper,
         &lagPortDescs](const std::shared_ptr<SwitchState>& state) {
          return ecmpHelper.resolveNextHops(
              state, lagPortDescs, true /* useLinkLocal */);
        },
        "resolve adjacency mysid neighbor");
    utility::waitForMySidResolveOrUnresolve(
        [this]() { return getProgrammedState(); },
        mySidPrefix(lag),
        kMySidPrefixLen,
        true /* resolved */);
  }

  void bringUpLag(int lag) {
    unresolveLagNeighbor(lag);
    bringUpLagLink(lag);
    resolveLagNeighbor(lag);
    auto port = getEgressPort(lagPortDesc(lag));
    XLOG(DBG2) << "Brought up lag " << lag << " link (port " << port << ")";
  }

  void programBackupRoutes() {
    auto ecmpHelper = makeEcmpHelper();
    auto routeUpdater = getSw()->getRouteUpdater();
    for (int i = 1; i < kNumLags; ++i) {
      const auto nextHop = ecmpHelper.nhop(lagPortDesc(i));
      const auto nextHopIp = nextHop.linkLocalNhopIp.has_value()
          ? folly::IPAddress(*nextHop.linkLocalNhopIp)
          : folly::IPAddress(nextHop.ip);
      routeUpdater.addRoute(
          RouterID(0),
          backupNextHopAddress(i),
          backupNextHopAddress(i).bitCount(),
          ClientID::OPENR,
          RouteNextHopEntry(
              RouteNextHopSet{
                  ResolvedNextHop(nextHopIp, nextHop.intf, ECMP_WEIGHT)},
              AdminDistance::OPENR));
    }
    routeUpdater.program();
  }

  std::unique_ptr<FrrProtectedObject> makeFrrProtectedObject() const {
    auto protectedObject = std::make_unique<FrrProtectedObject>();
    facebook::network::thrift::IPPrefix prefix;
    prefix.prefixAddress() = facebook::network::toBinaryAddress(mySidPrefix(0));
    prefix.prefixLength() = kMySidPrefixLen;
    protectedObject->mySid() = std::move(prefix);
    return protectedObject;
  }

  void addSrv6BackupProtection(const std::vector<int>& backupLagIndexes) {
    programBackupRoutes();

    auto backupNextHops = std::make_unique<std::vector<NextHopThrift>>();
    for (const auto i : backupLagIndexes) {
      backupNextHops->push_back(
          utility::makeSrv6NextHopThrift(
              backupNextHopAddress(i), repairSid(i), kSrv6TunnelId));
    }

    ThriftHandler(getSw()).addAdjacencyFrr(
        makeFrrProtectedObject(), std::move(backupNextHops));
  }

  void addSrv6BackupProtection() {
    addSrv6BackupProtection(backupLags());
  }

  void addIpBackupProtection(int backupLag) {
    const auto nextHop = makeEcmpHelper().nhop(lagPortDesc(backupLag));
    const auto nextHopIp = nextHop.linkLocalNhopIp.has_value()
        ? folly::IPAddress(*nextHop.linkLocalNhopIp)
        : folly::IPAddress(nextHop.ip);
    NextHopThrift backupNextHop;
    backupNextHop.address() = facebook::network::toBinaryAddress(nextHopIp);
    backupNextHop.address()->ifName() =
        utility::createTunIntfName(nextHop.intf);
    auto backupNextHops = std::make_unique<std::vector<NextHopThrift>>();
    backupNextHops->push_back(std::move(backupNextHop));
    ThriftHandler(getSw()).addAdjacencyFrr(
        makeFrrProtectedObject(), std::move(backupNextHops));
  }

  void deleteSrv6BackupProtection() {
    ThriftHandler(getSw()).deleteAdjacencyFrr(makeFrrProtectedObject());
  }
};

TEST_F(AgentMySidAdjFrrRouteTest, addSrv6BackupProtection) {
  auto setup = [this]() {
    resolveLagNeighbor(kPrimaryLag);
    addSrv6BackupProtection();
  };
  auto verify = [this]() {
    // Primary up: traffic takes the protected SID's own adjacency.
    verifyForwardedViaPrimary();

    // Fail the primary link and an FRR backup has to pick the traffic up.
    bringDownLagLink(kPrimaryLag);
    verifyForwardedViaSrv6Backup(backupLags(), {kPrimaryLag} /* downLags */);

    // Restore the primary adjacency and traffic must fail back to it.
    bringUpLag(kPrimaryLag);
    verifyForwardedViaPrimary();
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentMySidAdjFrrRouteTest, addProtectionToPrimaryOnlyMySid) {
  auto setup = [this]() { resolveLagNeighbor(kPrimaryLag); };
  auto verify = [this]() {
    verifyForwardedViaPrimary();

    addSrv6BackupProtection();
    verifyForwardedViaPrimary();

    bringDownLagLink(kPrimaryLag);
    verifyForwardedViaSrv6Backup(backupLags(), {kPrimaryLag} /* downLags */);

    bringUpLag(kPrimaryLag);
    deleteSrv6BackupProtection();
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentMySidAdjFrrRouteTest, addPrimaryToBackupOnlyMySid) {
  auto setup = [this]() {
    unresolveLagNeighbor(kPrimaryLag);
    addSrv6BackupProtection();
  };
  auto verify = [this]() {
    // Reestablish the backup-only starting state after warm boot, when the
    // primary link is left down by the prior run.
    unresolveLagNeighbor(kPrimaryLag);
    bringUpLagLink(kPrimaryLag);
    verifyForwardedViaSrv6Backup(backupLags(), {kPrimaryLag} /* downLags */);

    // Resolving the primary adds it to the protection group and takes traffic.
    resolveLagNeighbor(kPrimaryLag);
    verifyForwardedViaPrimary();

    // Hardware protection returns traffic to a backup when the primary fails.
    bringDownLagLink(kPrimaryLag);
    verifyForwardedViaSrv6Backup(backupLags(), {kPrimaryLag} /* downLags */);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentMySidAdjFrrRouteTest, addBackupAndPrimaryToBackupOnlyMySid) {
  const std::vector<int> oneBackup{1};
  const std::vector<int> twoBackups{1, 2};
  auto setup = [this]() { unresolveLagNeighbor(kPrimaryLag); };
  auto verify = [this, &oneBackup, &twoBackups]() {
    addSrv6BackupProtection(oneBackup);
    verifyForwardedViaSrv6Backup(oneBackup, {kPrimaryLag, 2, 3});

    addSrv6BackupProtection(twoBackups);
    verifyForwardedViaSrv6Backup(twoBackups, {kPrimaryLag, 3});

    resolveLagNeighbor(kPrimaryLag);
    verifyForwardedViaPrimary();

    unresolveLagNeighbor(kPrimaryLag);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentMySidAdjFrrRouteTest, allNextHopsUnavailableThenRecover) {
  auto setup = [this]() { unresolveLagNeighbor(kPrimaryLag); };
  auto verify = [this]() {
    addSrv6BackupProtection();
    verifyForwardedViaSrv6Backup(backupLags(), {kPrimaryLag} /* downLags */);

    for (const auto lag : backupLags()) {
      unresolveLagNeighbor(lag);
    }

    constexpr int kRecoveredBackupLag{1};
    resolveLagNeighbor(kRecoveredBackupLag);
    verifyForwardedViaSrv6Backup({kRecoveredBackupLag}, {kPrimaryLag, 2, 3});

    resolveLagNeighbor(kPrimaryLag);
    verifyForwardedViaPrimary();
    unresolveLagNeighbor(kPrimaryLag);

    for (const auto lag : {2, 3}) {
      resolveLagNeighbor(lag);
    }
    deleteSrv6BackupProtection();
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentMySidAdjFrrRouteTest, backupNextHopFlap) {
  auto setup = [this]() {
    unresolveLagNeighbor(kPrimaryLag);
    addSrv6BackupProtection();
  };
  auto verify = [this]() {
    pumpTrafficAndVerifyLoadBalancedAcrossLags(
        backupLags(), {kPrimaryLag} /* downLags */);

    constexpr int kFlappedBackupLag{1};
    bringDownLagLink(kFlappedBackupLag);
    pumpTrafficAndVerifyLoadBalancedAcrossLags(
        {2, 3}, {kPrimaryLag, kFlappedBackupLag});

    bringUpLag(kFlappedBackupLag);
    pumpTrafficAndVerifyLoadBalancedAcrossLags(
        backupLags(), {kPrimaryLag} /* downLags */);

    resolveLagNeighbor(kPrimaryLag);
    verifyForwardedViaPrimary();
    bringDownLagLink(kPrimaryLag);
    pumpTrafficAndVerifyLoadBalancedAcrossLags(
        backupLags(), {kPrimaryLag} /* downLags */);

    unresolveLagNeighbor(kPrimaryLag);
    bringUpLagLink(kPrimaryLag);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentMySidAdjFrrRouteTest, ipNextHopsAsBackup) {
  constexpr int kBackupLag{1};
  auto setup = [this]() {
    unresolveLagNeighbor(kPrimaryLag);
    addIpBackupProtection(kBackupLag);
  };
  auto verify = [this]() { verifyShiftedPacketOnLag(kBackupLag); };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
