// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <utility>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/SwSwitchMySidUpdater.h"
#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/AggregatePort.h"
#include "fboss/agent/state/RouteNextHop.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/TrunkUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/test/utils/Srv6TestUtils.h"
#include "fboss/agent/test/utils/TrapPacketUtils.h"
#include "fboss/lib/CommonUtils.h"

namespace facebook::fboss {

struct PhysicalPortBindingSid {
  static constexpr bool isTrunk = false;
};
struct AggregatePortBindingSid {
  static constexpr bool isTrunk = true;
};
using Srv6BindingSidPortTypes =
    ::testing::Types<PhysicalPortBindingSid, AggregatePortBindingSid>;

template <typename PortType>
class AgentSrv6BindingSidTest : public AgentHwTest {
 protected:
  static constexpr bool kIsTrunk = PortType::isTrunk;

  static inline const folly::IPAddressV6 kSid0{"3001:db8:1:2:3:4:5:6"};
  static inline const folly::IPAddressV6 kSid1{"3001:db8:4:5:6::"};
  static inline const folly::IPAddressV6 kMySidPrefix{"fc00:100:1::"};
  static constexpr uint8_t kMySidPrefixLen{48};
  static constexpr int kNumNextHops{4};

  static inline const folly::CIDRNetwork kEncapRoutePrefix{
      folly::IPAddress("2800:2::"),
      64};
  static inline const folly::CIDRNetwork kEncapV4RoutePrefix{
      folly::IPAddress("10.100.0.0"),
      16};
  static inline const std::string kEncapNhgName{"encapToBindingSid"};
  static inline const std::string kBindingSidNhgName{"bindingSidNHG"};

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    if constexpr (kIsTrunk) {
      return {ProductionFeature::SRV6_BINDING_SID, ProductionFeature::LAG};
    }
    return {ProductionFeature::SRV6_BINDING_SID};
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_enable_nexthop_id_manager = true;
    FLAGS_resolve_nexthops_from_id = true;
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto masterLogicalPorts = ensemble.masterLogicalPortIds();
    cfg::SwitchConfig cfg;
    if constexpr (kIsTrunk) {
      std::vector<utility::AggregatePortInfo> aggPorts;
      aggPorts.reserve(kNumNextHops);
      for (int i = 0; i < kNumNextHops; ++i) {
        aggPorts.push_back({AggregatePortID(i + 1), {masterLogicalPorts[i]}});
      }
      cfg = utility::oneAggregatePortPerInterfaceConfig(
          ensemble.getSw(),
          masterLogicalPorts,
          aggPorts,
          true /*interfaceHasSubnet*/);
    } else {
      cfg = utility::onePortPerInterfaceConfig(
          ensemble.getSw(), masterLogicalPorts, true /*interfaceHasSubnet*/);
    }
    cfg.loadBalancers() =
        utility::getEcmpFullWithFlowLabelTrunkFullWithFlowLabelHashConfig(
            ensemble.getL3Asics());
    addSrv6TunnelConfig(cfg);
    auto asic = checkSameAndGetAsicForTesting(ensemble.getL3Asics());
    std::set<folly::CIDRNetwork> trapPrefixes{
        folly::CIDRNetwork{kSid0, 128}, folly::CIDRNetwork{kSid1, 128}};
    utility::addTrapPacketAcl(asic, &cfg, trapPrefixes);
    utility::addOlympicQueueConfig(
        &cfg,
        ensemble.getL3Asics(),
        /*addWredConfig=*/false,
        /*addEcnConfig=*/true);
    utility::addOlympicQosMaps(cfg, ensemble.getL3Asics());
    return cfg;
  }

  void applyConfigAndEnableTrunks(const cfg::SwitchConfig& config) {
    this->applyNewConfig(config);
    this->applyNewState(
        [](const std::shared_ptr<SwitchState> state) {
          return utility::enableTrunkPorts(state);
        },
        "enable trunk ports");
  }

  template <typename IPAddrT = folly::IPAddressV6>
  utility::EcmpSetupAnyNPorts<IPAddrT> makeEcmpHelper() {
    return utility::EcmpSetupAnyNPorts<IPAddrT>(
        this->getProgrammedState(), this->getSw()->needL2EntryForNeighbor());
  }

  void resolveNextHops(int numNextHops) {
    auto ecmpHelper6 = makeEcmpHelper<folly::IPAddressV6>();
    this->resolveNeighbors(ecmpHelper6, numNextHops, true /* useLinkLocal */);
  }

  void setupHelper(bool resolveNeighbors = true) {
    if constexpr (kIsTrunk) {
      applyConfigAndEnableTrunks(
          this->initialConfig(*this->getAgentEnsemble()));
    }
    if (resolveNeighbors) {
      resolveNextHops(kNumNextHops);
    }
    programRoutes();
  }

  struct OpenrRouteInfo {
    folly::IPAddressV6 prefix;
    folly::IPAddressV6 bgpNhop;
    int nhopIdx;
  };

  std::vector<folly::IPAddressV6> getLoopbacks() const {
    return {
        folly::IPAddressV6("fdad::1:1"),
        folly::IPAddressV6("fdad::2:1"),
        folly::IPAddressV6("fdad::3:1"),
        folly::IPAddressV6("fdad::4:1")};
  }

  void programOpenrRoutes() {
    auto ecmpHelper = makeEcmpHelper();
    auto routeUpdater = this->getSw()->getRouteUpdater();
    auto loopbacks = getLoopbacks();
    for (auto i = 0; i < loopbacks.size(); ++i) {
      auto nhop = ecmpHelper.nhop(i);
      auto nhopIp = nhop.linkLocalNhopIp.has_value()
          ? folly::IPAddress(nhop.linkLocalNhopIp.value())
          : folly::IPAddress(nhop.ip);
      routeUpdater.addRoute(
          RouterID(0),
          loopbacks[i],
          loopbacks[i].bitCount(),
          ClientID::OPENR,
          RouteNextHopEntry(
              RouteNextHopSet{ResolvedNextHop(nhopIp, nhop.intf, ECMP_WEIGHT)},
              AdminDistance::OPENR));
    }
    routeUpdater.program();
  }

  void removeOpenrRoutes() {
    auto routeUpdater = this->getSw()->getRouteUpdater();
    for (const auto& pfx : getLoopbacks()) {
      routeUpdater.delRoute(RouterID(0), pfx, pfx.bitCount(), ClientID::OPENR);
    }
    routeUpdater.program();
  }

  void programRoutes() {
    programOpenrRoutes();
  }

  void unresolveAllNextHops() {
    auto ecmpHelper = makeEcmpHelper();
    std::vector<PortDescriptor> portVec;
    portVec.reserve(kNumNextHops);
    for (int i = 0; i < kNumNextHops; ++i) {
      portVec.push_back(ecmpHelper.nhop(i).portDesc);
    }
    boost::container::flat_set<PortDescriptor> portDescs(
        portVec.begin(), portVec.end());
    this->applyNewState(
        [&ecmpHelper, &portDescs](std::shared_ptr<SwitchState> in) {
          return ecmpHelper.unresolveNextHops(
              in, portDescs, /*useLinkLocal=*/true);
        },
        "unresolve all neighbors");
  }

  void addBindingSid(
      const folly::IPAddressV6& mySidAddr,
      const std::vector<NextHopThrift>& nhops) {
    MySidEntry bindingEntry;
    bindingEntry.type() = MySidType::BINDING_MICRO_SID;
    facebook::network::thrift::IPPrefix mySidPrefix;
    mySidPrefix.prefixAddress() = facebook::network::toBinaryAddress(mySidAddr);
    mySidPrefix.prefixLength() = kMySidPrefixLen;
    bindingEntry.mySid() = mySidPrefix;

    auto* rib = this->getSw()->getRib();
    CHECK(rib) << "RIB not initialized";
    rib->addOrUpdateNamedNextHopGroups(
        this->getSw()->getScopeResolver(),
        {{kBindingSidNhgName, util::toRouteNextHopSet(nhops, true)}},
        createRibToSwitchStateFunction(),
        this->getSw());

    NamedRouteDestination namedDest;
    namedDest.nextHopGroup_ref() = kBindingSidNhgName;
    bindingEntry.namedNextHops() = namedDest;

    auto ribMySidToSwitchStateFunc =
        createRibMySidToSwitchStateFunction(std::nullopt);
    rib->update(
        this->getSw()->getScopeResolver(),
        {bindingEntry},
        {} /* toDelete */,
        "addBindingSid",
        ribMySidToSwitchStateFunc,
        this->getSw());
  }

  void updateBindingSidNamedNextHopGroup(
      const std::vector<NextHopThrift>& nhops) {
    auto* rib = this->getSw()->getRib();
    CHECK(rib) << "RIB not initialized";
    rib->addOrUpdateNamedNextHopGroups(
        this->getSw()->getScopeResolver(),
        {{kBindingSidNhgName, util::toRouteNextHopSet(nhops, true)}},
        createRibToSwitchStateFunction(),
        this->getSw());
  }

  PortID getEgressPort(const PortDescriptor& portDesc) {
    if (portDesc.isPhysicalPort()) {
      return portDesc.phyPortID();
    }
    auto aggPort = this->getProgrammedState()->getAggregatePorts()->getNode(
        portDesc.aggPortID());
    return aggPort->sortedSubports().begin()->portID;
  }

  PortID findInjectPort(const std::vector<PortID>& egressPorts) {
    for (const auto& portMap :
         std::as_const(*this->getProgrammedState()->getPorts())) {
      for (const auto& [_, port] : std::as_const(*portMap.second)) {
        if (std::find(egressPorts.begin(), egressPorts.end(), port->getID()) ==
                egressPorts.end() &&
            port->isPortUp()) {
          return port->getID();
        }
      }
    }
    throw FbossError("No UP port found besides egress ports");
  }

  static constexpr uint8_t kDscp{42};
  static constexpr uint8_t kHopLimit{24};

  utility::EthFrame sendBindingSidPacket(
      bool ecnMarked,
      bool isV4,
      std::optional<PortID> injectPort = std::nullopt,
      std::optional<folly::IPAddressV6> outerDst = std::nullopt) {
    auto dstAddr = outerDst.value_or(kMySidPrefix);
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(this->getProgrammedState());

    auto outerTc = ecnMarked ? static_cast<uint8_t>((kDscp << 2) | 0x3)
                             : static_cast<uint8_t>(kDscp << 2);

    std::unique_ptr<TxPacket> txPacket;
    if (isV4) {
      txPacket = utility::makeIpInIpTxPacket(
          this->getSw(),
          this->getVlanIDForTx().value(),
          intfMac,
          intfMac,
          folly::IPAddressV6("100::1"),
          dstAddr,
          folly::IPAddressV4("10.0.0.1"),
          folly::IPAddressV4("10.0.0.2"),
          8000,
          8001,
          outerTc,
          0,
          kHopLimit,
          64);
    } else {
      txPacket = utility::makeIpInIpTxPacket(
          this->getSw(),
          this->getVlanIDForTx().value(),
          intfMac,
          intfMac,
          folly::IPAddressV6("100::1"),
          dstAddr,
          folly::IPAddressV6("2001:db8::1"),
          folly::IPAddressV6("2001:db8::2"),
          8000,
          8001,
          outerTc,
          0,
          kHopLimit,
          64);
    }

    auto originalFrame = utility::makeEthFrame(*txPacket);
    if (injectPort.has_value()) {
      XLOG(DBG2) << "sendBindingSidPacket: inner=" << (isV4 ? "v4" : "v6")
                 << " ecn=" << (ecnMarked ? "marked" : "unmarked")
                 << " front-panel inject on port " << injectPort.value();
      this->getSw()->sendPacketOutOfPortAsync(
          std::move(txPacket), injectPort.value());
    } else {
      XLOG(DBG2) << "sendBindingSidPacket: inner=" << (isV4 ? "v4" : "v6")
                 << " ecn=" << (ecnMarked ? "marked" : "unmarked")
                 << " CPU inject";
      this->sendPacketSwitchedAsync(std::move(txPacket));
    }
    return originalFrame;
  }

  void assertBindingSidForwarding(
      const std::vector<PortID>& egressPorts,
      const std::vector<folly::IPAddressV6>& expectedSids,
      bool ecnMarked,
      bool isV4,
      std::optional<PortID> injectPort = std::nullopt) {
    std::map<PortID, int64_t> bytesBefore;
    for (const auto& port : egressPorts) {
      bytesBefore[port] = *this->getLatestPortStats(port).outBytes_();
    }

    utility::SwSwitchPacketSnooper snooper(this->getSw(), "bindingSidSnooper");
    auto originalFrame =
        sendBindingSidPacket(ecnMarked, isV4, std::move(injectPort));

    auto frameRx = snooper.waitForPacket(1);
    WITH_RETRIES({
      bool anyPortGotBytes = false;
      for (auto port : egressPorts) {
        auto bytesAfter = *this->getLatestPortStats(port).outBytes_();
        if (bytesAfter > bytesBefore[port]) {
          anyPortGotBytes = true;
        }
      }
      EXPECT_EVENTUALLY_TRUE(anyPortGotBytes);
      if (!frameRx.has_value()) {
        frameRx = snooper.waitForPacket(1);
      }
      EXPECT_EVENTUALLY_TRUE(frameRx.has_value());
    });
    ASSERT_TRUE(frameRx.has_value());
    folly::io::Cursor cursor((*frameRx).get());
    utility::EthFrame frame(cursor);
    auto v6Payload = frame.v6PayLoad();
    ASSERT_TRUE(v6Payload.has_value());
    auto v6Hdr = v6Payload->header();

    bool sidMatch = false;
    for (const auto& sid : expectedSids) {
      if (v6Hdr.dstAddr == sid) {
        sidMatch = true;
        break;
      }
    }
    EXPECT_TRUE(sidMatch) << "Outer DA " << v6Hdr.dstAddr
                          << " does not match any expected SID";

    EXPECT_EQ(v6Hdr.hopLimit, kHopLimit - 1);
    EXPECT_EQ(v6Hdr.trafficClass >> 2, kDscp);
    EXPECT_EQ(v6Hdr.trafficClass & 0x3, ecnMarked ? 0x3 : 0);

    auto origOuterV6 = originalFrame.v6PayLoad();
    ASSERT_TRUE(origOuterV6.has_value());
    if (isV4) {
      auto expectedInnerV4 = origOuterV6->v4PayLoad();
      auto rxInnerV4 = v6Payload->v4PayLoad();
      ASSERT_NE(expectedInnerV4, nullptr);
      ASSERT_NE(rxInnerV4, nullptr);
      EXPECT_EQ(*rxInnerV4, *expectedInnerV4);
    } else {
      auto expectedInnerV6 = origOuterV6->v6PayLoad();
      auto rxInnerV6 = v6Payload->v6PayLoad();
      ASSERT_NE(expectedInnerV6, nullptr);
      ASSERT_NE(rxInnerV6, nullptr);
      EXPECT_EQ(*rxInnerV6, *expectedInnerV6);
    }
  }

  void verifyBindingSidCpuAndFrontPanel(
      const std::vector<PortID>& egressPorts,
      const std::vector<folly::IPAddressV6>& expectedSids) {
    auto injectPort = findInjectPort(egressPorts);
    for (bool isV4 : {false, true}) {
      // ECN not marked
      assertBindingSidForwarding(egressPorts, expectedSids, false, isV4);
      assertBindingSidForwarding(
          egressPorts, expectedSids, false, isV4, injectPort);
      // ECN marked
      assertBindingSidForwarding(egressPorts, expectedSids, true, isV4);
      assertBindingSidForwarding(
          egressPorts, expectedSids, true, isV4, injectPort);
    }
  }

  void assertBindingSidDrop(
      PortID injectPort,
      const std::vector<PortID>& egressPorts,
      bool isV4,
      std::optional<folly::IPAddressV6> outerDst = std::nullopt) {
    XLOG(DBG2) << "assertBindingSidDrop: inner=" << (isV4 ? "v4" : "v6")
               << " inject port " << injectPort << " outerDst="
               << (outerDst.has_value() ? outerDst->str() : "default");
    auto injectStatsBefore = this->getLatestPortStats(injectPort);
    std::map<PortID, int64_t> egressBytesBefore;
    for (const auto& port : egressPorts) {
      egressBytesBefore[port] = *this->getLatestPortStats(port).outBytes_();
    }

    sendBindingSidPacket(false, isV4, injectPort, outerDst);

    WITH_RETRIES({
      auto injectStatsAfter = this->getLatestPortStats(injectPort);
      EXPECT_EVENTUALLY_GT(
          *injectStatsAfter.inDiscards_(), *injectStatsBefore.inDiscards_());
      EXPECT_EVENTUALLY_TRUE(
          injectStatsAfter.inSrv6MySidDiscards_().has_value());
      EXPECT_EVENTUALLY_GT(
          injectStatsAfter.inSrv6MySidDiscards_().value_or(0),
          injectStatsBefore.inSrv6MySidDiscards_().value_or(0));
      for (auto port : egressPorts) {
        auto egressBytesAfter = *this->getLatestPortStats(port).outBytes_();
        EXPECT_EVENTUALLY_EQ(egressBytesAfter, egressBytesBefore[port]);
      }
    });
  }

  void verifyBindingSidDropFrontPanel(
      const std::vector<PortID>& egressPorts,
      std::optional<folly::IPAddressV6> outerDst = std::nullopt) {
    auto injectPort = findInjectPort(egressPorts);
    for (bool isV4 : {false, true}) {
      assertBindingSidDrop(injectPort, egressPorts, isV4, outerDst);
    }
  }

  void addEncapRouteToBindingSid(
      const folly::CIDRNetwork& prefix,
      const folly::IPAddressV6& bindingSidAddr) {
    static constexpr int kEncapNhopIdx = 4;
    auto ecmpHelper = utility::EcmpSetupAnyNPorts<folly::IPAddressV6>(
        this->getProgrammedState(),
        this->getSw()->needL2EntryForNeighbor(),
        utility::kLocalCpuMac());
    auto nhop = ecmpHelper.nhop(kEncapNhopIdx);
    CHECK(nhop.linkLocalNhopIp.has_value());

    this->applyNewState(
        [&ecmpHelper, &nhop](std::shared_ptr<SwitchState> in) {
          return ecmpHelper.resolveNextHops(
              in, {nhop.portDesc}, true /* useLinkLocal */);
        },
        "resolve encap route neighbor with kLocalCpuMac");

    RouteNextHopSet nhops{ResolvedNextHop(
        folly::IPAddress(*nhop.linkLocalNhopIp),
        nhop.intf,
        ECMP_WEIGHT,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::vector<folly::IPAddressV6>{bindingSidAddr},
        TunnelType::SRV6_ENCAP,
        std::string("srv6Tunnel0"))};

    auto rib = this->getSw()->getRib();
    std::vector<std::pair<std::string, RouteNextHopSet>> groups;
    groups.emplace_back(kEncapNhgName, nhops);
    rib->addOrUpdateNamedNextHopGroups(
        this->getSw()->getScopeResolver(),
        groups,
        createRibToSwitchStateFunction(),
        this->getSw());

    UnicastRoute route;
    route.dest()->ip() =
        facebook::network::toBinaryAddress(folly::IPAddress(prefix.first));
    route.dest()->prefixLength() = prefix.second;
    NamedRouteDestination namedDest;
    namedDest.nextHopGroup_ref() = kEncapNhgName;
    route.namedRouteDestination() = namedDest;

    auto routeUpdater = this->getSw()->getRouteUpdater();
    routeUpdater.addRoute(RouterID(0), ClientID::TE_AGENT, route);
    routeUpdater.program();
  }

  void pumpEncapTrafficAndVerifyLoadBalanced(
      const std::vector<PortID>& egressPorts,
      std::optional<PortID> injectPort = std::nullopt,
      int numPackets = 10000,
      int maxDeviationPct = 25) {
    auto vlanId = this->getVlanIDForTx().value();
    auto mac = utility::kLocalCpuMac();
    int sqrtN = static_cast<int>(std::sqrt(numPackets));

    auto pumpTraffic = [&]() {
      XLOG(DBG2) << "pumpEncapTrafficAndVerifyLoadBalanced: sending "
                 << sqrtN * sqrtN << " v6 packets, inject="
                 << (injectPort.has_value()
                         ? folly::to<std::string>(injectPort.value())
                         : "CPU");
      for (int i = 0; i < sqrtN; ++i) {
        for (int j = 0; j < sqrtN; ++j) {
          auto srcIp =
              folly::IPAddressV6(folly::to<std::string>("1001::1:", i + 1));
          auto dstIp =
              folly::IPAddressV6(folly::to<std::string>("2800:2::", j + 1));
          auto txPacket = utility::makeUDPTxPacket(
              this->getSw(),
              vlanId,
              mac,
              mac,
              srcIp,
              dstIp,
              10000 + i,
              20000 + j);
          if (injectPort.has_value()) {
            this->getSw()->sendPacketOutOfPortAsync(
                std::move(txPacket), injectPort.value());
          } else {
            this->sendPacketSwitchedAsync(std::move(txPacket));
          }
        }
      }
    };

    utility::pumpTrafficAndVerifyLoadBalanced(
        pumpTraffic,
        [&]() { /* clearPortStats - no-op, stats are delta-based */ },
        [&]() {
          return utility::isLoadBalanced(
              this->getLatestPortStats(egressPorts), maxDeviationPct);
        });
  }

  void addRouteToExistingEncapNhg(const folly::CIDRNetwork& prefix) {
    UnicastRoute route;
    route.dest()->ip() =
        facebook::network::toBinaryAddress(folly::IPAddress(prefix.first));
    route.dest()->prefixLength() = prefix.second;
    NamedRouteDestination namedDest;
    namedDest.nextHopGroup_ref() = kEncapNhgName;
    route.namedRouteDestination() = namedDest;

    auto routeUpdater = this->getSw()->getRouteUpdater();
    routeUpdater.addRoute(RouterID(0), ClientID::TE_AGENT, route);
    routeUpdater.program();
  }

  void pumpV4EncapTrafficAndVerifyLoadBalanced(
      const std::vector<PortID>& egressPorts,
      std::optional<PortID> injectPort = std::nullopt,
      int numPackets = 10000,
      int maxDeviationPct = 25) {
    auto vlanId = this->getVlanIDForTx().value();
    auto mac = utility::kLocalCpuMac();
    int sqrtN = static_cast<int>(std::sqrt(numPackets));

    auto pumpTraffic = [&]() {
      XLOG(DBG2) << "pumpV4EncapTrafficAndVerifyLoadBalanced: sending "
                 << sqrtN * sqrtN << " v4 packets, inject="
                 << (injectPort.has_value()
                         ? folly::to<std::string>(injectPort.value())
                         : "CPU");
      for (int i = 0; i < sqrtN; ++i) {
        for (int j = 0; j < sqrtN; ++j) {
          auto srcIp = folly::IPAddressV4(
              folly::to<std::string>("192.168.", i % 256, ".", (i / 256) + 1));
          auto dstIp = folly::IPAddressV4(
              folly::to<std::string>("10.100.", j % 256, ".", (j / 256) + 1));
          auto txPacket = utility::makeUDPTxPacket(
              this->getSw(),
              vlanId,
              mac,
              mac,
              srcIp,
              dstIp,
              10000 + i,
              20000 + j);
          if (injectPort.has_value()) {
            this->getSw()->sendPacketOutOfPortAsync(
                std::move(txPacket), injectPort.value());
          } else {
            this->sendPacketSwitchedAsync(std::move(txPacket));
          }
        }
      }
    };

    utility::pumpTrafficAndVerifyLoadBalanced(
        pumpTraffic,
        [&]() { /* clearPortStats - no-op, stats are delta-based */ },
        [&]() {
          return utility::isLoadBalanced(
              this->getLatestPortStats(egressPorts), maxDeviationPct);
        });
  }

 private:
  void addSrv6TunnelConfig(cfg::SwitchConfig& cfg) const {
    std::vector<cfg::Srv6Tunnel> tunnelList;
    tunnelList.push_back(
        utility::makeSrv6TunnelConfig(
            "srv6Tunnel0", InterfaceID(cfg.interfaces()[0].intfID().value())));
    cfg.srv6Tunnels() = tunnelList;
  }
};

TYPED_TEST_SUITE(AgentSrv6BindingSidTest, Srv6BindingSidPortTypes);

TYPED_TEST(AgentSrv6BindingSidTest, multipleNextHops) {
  auto setup = [this]() {
    this->setupHelper();
    utility::addBindingSidEntry(
        this->getSw(),
        this->kMySidPrefix,
        this->kMySidPrefixLen,
        {utility::makeSrv6NextHopThrift(this->getLoopbacks()[0], this->kSid0),
         utility::makeSrv6NextHopThrift(this->getLoopbacks()[1], this->kSid1)});
  };

  auto verify = [this]() {
    auto ecmpHelper = this->makeEcmpHelper();
    std::vector<PortID> egressPorts{
        this->getEgressPort(ecmpHelper.nhop(0).portDesc),
        this->getEgressPort(ecmpHelper.nhop(1).portDesc)};
    this->verifyBindingSidCpuAndFrontPanel(
        egressPorts, {this->kSid0, this->kSid1});
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentSrv6BindingSidTest, singleNextHop) {
  auto setup = [this]() {
    this->setupHelper();
    utility::addBindingSidEntry(
        this->getSw(),
        this->kMySidPrefix,
        this->kMySidPrefixLen,
        {utility::makeSrv6NextHopThrift(this->getLoopbacks()[0], this->kSid0)});
  };

  auto verify = [this]() {
    auto ecmpHelper = this->makeEcmpHelper();
    std::vector<PortID> egressPorts{
        this->getEgressPort(ecmpHelper.nhop(0).portDesc)};
    this->verifyBindingSidCpuAndFrontPanel(egressPorts, {this->kSid0});
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(
    AgentSrv6BindingSidTest,
    bindingSidNamedNextHopGroupUpdateUpdatesSidList) {
  auto setup = [this]() {
    this->setupHelper();
    this->addBindingSid(
        this->kMySidPrefix,
        {utility::makeSrv6NextHopThrift(this->getLoopbacks()[0], this->kSid0),
         utility::makeSrv6NextHopThrift(this->getLoopbacks()[1], this->kSid0)});
  };

  auto verify = [this]() {
    auto ecmpHelper = this->makeEcmpHelper();
    std::vector<PortID> egressPorts{
        this->getEgressPort(ecmpHelper.nhop(0).portDesc),
        this->getEgressPort(ecmpHelper.nhop(1).portDesc)};
    this->verifyBindingSidCpuAndFrontPanel(egressPorts, {this->kSid0});

    this->updateBindingSidNamedNextHopGroup(
        {utility::makeSrv6NextHopThrift(this->getLoopbacks()[0], this->kSid1),
         utility::makeSrv6NextHopThrift(this->getLoopbacks()[1], this->kSid1)});

    this->verifyBindingSidCpuAndFrontPanel(egressPorts, {this->kSid1});

    // Restore the named NHG to the state setup() left it in. On warm boot
    // setup() is skipped and verify() reruns, so the kSid1 update above must
    // not leak into the next run.
    this->updateBindingSidNamedNextHopGroup(
        {utility::makeSrv6NextHopThrift(this->getLoopbacks()[0], this->kSid0),
         utility::makeSrv6NextHopThrift(this->getLoopbacks()[1], this->kSid0)});
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentSrv6BindingSidTest, bindingSidTracksNeighborResolutionAndLink) {
  auto setup = [this]() {
    this->setupHelper(false /* resolveNeighbors */);
    utility::addBindingSidEntry(
        this->getSw(),
        this->kMySidPrefix,
        this->kMySidPrefixLen,
        {utility::makeSrv6NextHopThrift(this->getLoopbacks()[0], this->kSid0)});

    auto ecmpHelper = this->makeEcmpHelper();
    auto portDesc = ecmpHelper.nhop(0).portDesc;
    std::vector<PortID> egressPorts{this->getEgressPort(portDesc)};

    // Neighbors unresolved — packets should be dropped
    this->verifyBindingSidDropFrontPanel(egressPorts);

    // Resolve neighbors — forwarding should work
    this->resolveNextHops(this->kNumNextHops);
    this->verifyBindingSidCpuAndFrontPanel(egressPorts, {this->kSid0});
  };

  auto verify = [this]() {
    auto ecmpHelper = this->makeEcmpHelper();
    auto portDesc = ecmpHelper.nhop(0).portDesc;
    auto egressPort = this->getEgressPort(portDesc);
    std::vector<PortID> egressPorts{egressPort};

    // Bring down the link — packets should be dropped
    this->bringDownPort(egressPort);
    this->verifyBindingSidDropFrontPanel(egressPorts);

    // Unresolve neighbor, bring link back up, re-resolve
    this->applyNewState(
        [&ecmpHelper, portDesc](std::shared_ptr<SwitchState> in) {
          return ecmpHelper.unresolveNextHops(
              in, {portDesc}, /*useLinkLocal=*/true);
        },
        "unresolve binding sid neighbor");
    this->bringUpPort(egressPort);
    this->applyNewState(
        [&ecmpHelper, portDesc](std::shared_ptr<SwitchState> in) {
          return ecmpHelper.resolveNextHops(
              in, {portDesc}, /*useLinkLocal=*/true);
        },
        "re-resolve binding sid neighbor");

    // Forwarding should work again
    this->verifyBindingSidCpuAndFrontPanel(egressPorts, {this->kSid0});
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentSrv6BindingSidTest, dropPacketBindingSidIsNotLastSid) {
  auto setup = [this]() {
    this->setupHelper();
    utility::addBindingSidEntry(
        this->getSw(),
        this->kMySidPrefix,
        this->kMySidPrefixLen,
        {utility::makeSrv6NextHopThrift(this->getLoopbacks()[0], this->kSid0)});
  };

  auto verify = [this]() {
    auto ecmpHelper = this->makeEcmpHelper();
    std::vector<PortID> egressPorts{
        this->getEgressPort(ecmpHelper.nhop(0).portDesc)};
    this->verifyBindingSidDropFrontPanel(
        egressPorts, folly::IPAddressV6("fc00:100:1:2::"));
  };

  this->verifyAcrossWarmBoots(setup, verify);
}
/*
Note that we inject packets first through encap on nhop(4),
so that the flow label gets populated and then make them
hit binding sid. This is to mimic how things will happen in
actual production.
*/
TYPED_TEST(AgentSrv6BindingSidTest, bindingSidMultiHopIsLoadBalanced) {
  auto constexpr kBindingSidWidth = 4;
  auto setup = [this]() {
    this->setupHelper(false /* resolveNeighbors */);
    this->resolveNextHops(kBindingSidWidth);
    utility::addBindingSidEntry(
        this->getSw(),
        this->kMySidPrefix,
        this->kMySidPrefixLen,
        {utility::makeSrv6NextHopThrift(this->getLoopbacks()[0], this->kSid0),
         utility::makeSrv6NextHopThrift(this->getLoopbacks()[1], this->kSid1),
         utility::makeSrv6NextHopThrift(this->getLoopbacks()[2], this->kSid0),
         utility::makeSrv6NextHopThrift(this->getLoopbacks()[3], this->kSid1)});
    this->addEncapRouteToBindingSid(
        this->kEncapRoutePrefix, this->kMySidPrefix);
    this->addRouteToExistingEncapNhg(this->kEncapV4RoutePrefix);
  };

  auto verify = [this]() {
    auto ecmpHelper = this->makeEcmpHelper();
    std::vector<PortID> egressPorts;
    egressPorts.reserve(kBindingSidWidth);
    for (int i = 0; i < kBindingSidWidth; ++i) {
      egressPorts.push_back(this->getEgressPort(ecmpHelper.nhop(i).portDesc));
    }
    auto injectPort = this->findInjectPort(egressPorts);
    // IPv6 encap traffic
    this->pumpEncapTrafficAndVerifyLoadBalanced(egressPorts);
    this->pumpEncapTrafficAndVerifyLoadBalanced(egressPorts, injectPort);
    // IPv4 encap traffic
    this->pumpV4EncapTrafficAndVerifyLoadBalanced(egressPorts);
    this->pumpV4EncapTrafficAndVerifyLoadBalanced(egressPorts, injectPort);
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentSrv6BindingSidTest, multiHopUnresolvedToResolved) {
  auto setup = [this]() {
    // Start with neighbors unresolved and no OpenR routes.
    // Only BGP routes are programmed — binding SID next hops are fully
    // unresolved since the BGP routes have no underlying IGP resolution.
    this->setupHelper(false /* resolveNeighbors */);
    this->removeOpenrRoutes();
    utility::addBindingSidEntry(
        this->getSw(),
        this->kMySidPrefix,
        this->kMySidPrefixLen,
        {utility::makeSrv6NextHopThrift(this->getLoopbacks()[0], this->kSid0),
         utility::makeSrv6NextHopThrift(this->getLoopbacks()[1], this->kSid1)});
    auto ecmpHelper = this->makeEcmpHelper();
    std::vector<PortID> egressPorts{
        this->getEgressPort(ecmpHelper.nhop(0).portDesc),
        this->getEgressPort(ecmpHelper.nhop(1).portDesc)};

    // Phase 0: BGP routes are resolved to null route, so packets
    // matching it will get discarded
    this->verifyBindingSidDropFrontPanel(egressPorts);

    // Phase 1: Add OpenR routes (BGP routes now resolve recursively),
    // but neighbors are still unresolved — packets will now get trapped
    // to CPU.
    this->programOpenrRoutes();
    // Phase 2: Resolve neighbors — forwarding should work.
    this->resolveNextHops(this->kNumNextHops);
  };

  auto verify = [this]() {
    auto ecmpHelper = this->makeEcmpHelper();
    std::vector<PortID> egressPorts{
        this->getEgressPort(ecmpHelper.nhop(0).portDesc),
        this->getEgressPort(ecmpHelper.nhop(1).portDesc)};
    this->verifyBindingSidCpuAndFrontPanel(
        egressPorts, {this->kSid0, this->kSid1});

    // Iteration 1: unresolve neighbors first, then remove OpenR routes.
    this->unresolveAllNextHops();
    this->removeOpenrRoutes();

    // Re-add OpenR routes and re-resolve — verify forwarding recovers.
    this->programOpenrRoutes();
    this->resolveNextHops(this->kNumNextHops);
    this->verifyBindingSidCpuAndFrontPanel(
        egressPorts, {this->kSid0, this->kSid1});

    // Iteration 2: remove OpenR routes first, then unresolve neighbors.
    this->removeOpenrRoutes();
    this->unresolveAllNextHops();

    // Re-add OpenR routes and re-resolve — verify forwarding recovers.
    this->programOpenrRoutes();
    this->resolveNextHops(this->kNumNextHops);
    this->verifyBindingSidCpuAndFrontPanel(
        egressPorts, {this->kSid0, this->kSid1});
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
