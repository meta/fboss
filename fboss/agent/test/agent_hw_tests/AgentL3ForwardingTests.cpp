// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/ThriftHandler.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/state/StateUtils.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/HyperPortTestUtils.h"
#include "fboss/lib/CommonUtils.h"

#include "fboss/agent/test/gen-cpp2/production_features_types.h"
namespace facebook::fboss {

class AgentL3ForwardingTest : public AgentHwTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::L3_FORWARDING};
  }

 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = AgentHwTest::initialConfig(ensemble);
    return addCoppConfig(ensemble, config);
  }
  std::optional<VlanID> kVlanID() const {
    return getVlanIDForTx();
  }
  InterfaceID kIntfID() const {
    if (getSw()->getSwitchInfoTable().haveVoqSwitches()) {
      return firstInterfaceIDWithPortsForTesting(
          getProgrammedState(), cfg::Scope::GLOBAL);
    }
    return firstInterfaceIDWithPortsForTesting(getProgrammedState());
  }

  folly::MacAddress kNeighborMac() const {
    // Random mac address
    return folly::MacAddress("0c:42:a1:66:1d:9e");
  }
  template <typename IPAddrT>
  auto getNeighborTable(const std::shared_ptr<SwitchState>& in) {
    auto state = in;
    return state->getInterfaces()
        ->getNode(kIntfID())
        ->template getNeighborEntryTable<IPAddrT>()
        ->modify(kIntfID(), &state);
  }
  template <typename IPAddrT>
  std::shared_ptr<SwitchState> addResolvedNeighbor(
      const std::shared_ptr<SwitchState>& in,
      const IPAddrT& ip) {
    auto outState{in->clone()};
    auto neighborTable = getNeighborTable<IPAddrT>(outState);
    neighborTable->addPendingEntry(ip, kIntfID());
    auto ports = getPortsForInterface(kIntfID(), outState);
    CHECK(ports.size());
    neighborTable->updateEntry(
        ip,
        kNeighborMac(),
        PortDescriptor(*ports.begin()),
        kIntfID(),
        NeighborState::REACHABLE);
    return outState;
  }
  template <typename IPAddrT>
  std::shared_ptr<SwitchState> removeNeighbor(
      const std::shared_ptr<SwitchState>& inState,
      const IPAddrT& ip) {
    auto outState{inState->clone()};
    auto neighborTable = getNeighborTable<IPAddrT>(outState);
    neighborTable->removeEntry(ip);
    return outState;
  }

  void verifyHwAgentConnectionState(ThriftHandler& handler) {
    std::map<int16_t, HwAgentEventSyncStatus> statusMap;
    if (!getSw()->isRunModeMultiSwitch()) {
      return;
    }
    handler.getHwAgentConnectionStatus(statusMap);
    WITH_RETRIES({
      statusMap.clear();
      handler.getHwAgentConnectionStatus(statusMap);
      EXPECT_EVENTUALLY_GE(statusMap.size(), 1);
      EXPECT_EVENTUALLY_EQ(statusMap[0].statsEventSyncActive().value(), 1);
      EXPECT_EVENTUALLY_EQ(statusMap[0].fdbEventSyncActive().value(), 1);
      EXPECT_EVENTUALLY_EQ(statusMap[0].linkEventSyncActive().value(), 1);
      EXPECT_EVENTUALLY_EQ(statusMap[0].rxPktEventSyncActive().value(), 1);
      EXPECT_EVENTUALLY_EQ(statusMap[0].txPktEventSyncActive().value(), 1);
      EXPECT_EVENTUALLY_EQ(
          statusMap[0].switchReachabilityChangeEventSyncActive().value(), 1);
    });
  }
};

TEST_F(AgentL3ForwardingTest, linkLocalNeighborAndNextHop) {
  auto setup = [=, this]() {
    // Random LL IPs
    // linkLocalNhop - used as both LL nbr and nhop
    // linkLocalNbr  - used as both LL nbr and
    folly::IPAddress linkLocalNhop("fe80::e42:a1ff:fe66:1d9e");
    folly::IPAddress linkLocalNbr("fe80::e43:a1ff:fe66:1d9e");
    applyNewState(
        [&](const std::shared_ptr<SwitchState>& in) {
          return addResolvedNeighbor(in, linkLocalNhop.asV6());
        },
        "add link local nbr, nhop");
    applyNewState(
        [&](const std::shared_ptr<SwitchState>& in) {
          return addResolvedNeighbor(in, linkLocalNbr.asV6());
        },
        "add link local nbr, nhop");
    RouteNextHopSet nhops;
    nhops.emplace(ResolvedNextHop(linkLocalNhop, kIntfID(), ECMP_WEIGHT));
    auto routeUpdater = getSw()->getRouteUpdater();
    routeUpdater.addRoute(
        RouterID(0),
        folly::IPAddressV6("2803:1::"),
        64,
        ClientID::BGPD,
        RouteNextHopEntry(nhops, AdminDistance::EBGP));
    routeUpdater.program();
  };
  // No verify here. We are just testing safe warm boots
  // with LL neighbors and next hops
  verifyAcrossWarmBoots(setup, []() {});
}

TEST_F(AgentL3ForwardingTest, ttl255) {
  utility::EcmpSetupAnyNPorts6 ecmpHelper6(
      getSw()->getState(), getSw()->needL2EntryForNeighbor());
  utility::EcmpSetupAnyNPorts4 ecmpHelper4(
      getSw()->getState(), getSw()->needL2EntryForNeighbor());
  auto setup = [=, this]() {
    auto programDefaultRoutes = [this](auto& ecmpHelper) {
      auto wrapper = getSw()->getRouteUpdater();
      ecmpHelper.programRoutes(&wrapper, 1);
    };
    programDefaultRoutes(ecmpHelper6);
    programDefaultRoutes(ecmpHelper4);

    auto checkNhop = [this](const auto& nhop) {
      auto port = getProgrammedState()->getPort(nhop.portDesc.phyPortID());
      bool isHyperPort = (port->getPortType() == cfg::PortType::HYPER_PORT);
      ASSERT_EQ(isHyperPort, FLAGS_hyper_port);
    };

    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      checkNhop(ecmpHelper6.nhop(0));
      return ecmpHelper6.resolveNextHops(in, {ecmpHelper6.nhop(0).portDesc});
    });
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      checkNhop(ecmpHelper4.nhop(0));
      return ecmpHelper4.resolveNextHops(in, {ecmpHelper4.nhop(0).portDesc});
    });
    CHECK_EQ(ecmpHelper4.nhop(0).portDesc, ecmpHelper6.nhop(0).portDesc);
  };
  auto verify = [=, this]() {
    ThriftHandler handler(getSw());
    verifyHwAgentConnectionState(handler);
    auto memberPorts = utility::getHyperPortMembers(
        getProgrammedState(), ecmpHelper6.nhop(0).portDesc.phyPortID());
    if (memberPorts.empty()) {
      memberPorts.push_back(ecmpHelper6.nhop(0).portDesc.phyPortID());
    }
    auto constexpr kBytesPerPort = 1000;
    // Use a distinct src MAC so the test packet's SMAC != DMAC; TU1 drops
    // L3 packets whose SMAC equals DMAC.
    auto srcMac = folly::MacAddress("02:01:02:03:04:05");
    auto inPort = ecmpHelper6.nhop(1).portDesc.phyPortID();
    auto sendPkt = [=, this](bool isV6) {
      auto srcIp = folly::IPAddress(isV6 ? "1001::1" : "10.0.0.1");
      auto dstIp = folly::IPAddress(isV6 ? "100:100:100::1" : "100.100.100.1");
      constexpr uint8_t kTtl = 255;
      auto pkt = utility::makeUDPTxPacket(
          getSw(),
          getVlanIDForTx(),
          srcMac,
          getMacForFirstInterfaceWithPortsForTesting(getProgrammedState()),
          srcIp,
          dstIp,
          10000,
          10001,
          0,
          kTtl,
          std::vector<uint8_t>(kBytesPerPort, 0xff));
      getSw()->sendPacketOutOfPortAsync(std::move(pkt), inPort);
    };
    auto pumpTraffic = [=]() {
      for (auto isV6 : {true, false}) {
        for (auto i = 0; i < memberPorts.size(); ++i) {
          sendPkt(isV6);
        }
      }
    };
    learnL2EntryIfPending(srcMac, inPort, [&]() { sendPkt(true); });
    auto port = ecmpHelper6.nhop(0).portDesc.phyPortID();
    auto allPorts = memberPorts;
    allPorts.push_back(port);
    auto portStatsBefore = getLatestPortStats(allPorts);
    pumpTraffic();
    WITH_RETRIES({
      auto portStatsAfter = getLatestPortStats(allPorts);
      XLOG(DBG2) << " Out pkts, before:"
                 << *portStatsBefore[port].outUnicastPkts_()
                 << " after:" << *portStatsAfter[port].outUnicastPkts_()
                 << " Out bytes before: " << *portStatsBefore[port].outBytes_()
                 << " after: " << *portStatsAfter[port].outBytes_();
      EXPECT_EVENTUALLY_EQ(
          *portStatsAfter[port].outUnicastPkts_(),
          *portStatsBefore[port].outUnicastPkts_() + memberPorts.size() * 2);
      EXPECT_EVENTUALLY_GE(
          *portStatsAfter[port].outBytes_(),
          *portStatsBefore[port].outBytes_() +
              memberPorts.size() * 2 * kBytesPerPort);
      if (memberPorts.size() > 1) {
        for (auto memberPort : memberPorts) {
          XLOG(DBG2) << " Member port: " << memberPort << ". Out pkts, before:"
                     << *portStatsBefore[memberPort].outUnicastPkts_()
                     << " after:"
                     << *portStatsAfter[memberPort].outUnicastPkts_()
                     << " Out bytes before: "
                     << *portStatsBefore[memberPort].outBytes_()
                     << " after: " << *portStatsAfter[memberPort].outBytes_();
          EXPECT_EVENTUALLY_GE(
              *portStatsAfter[memberPort].outBytes_(),
              *portStatsBefore[memberPort].outBytes_() + 2 * kBytesPerPort);
        }
      }
    });
    handler.clearAllPortStats();
    WITH_RETRIES({
      auto portStatsAfter = getLatestPortStats(allPorts);
      for (auto aPort : allPorts) {
        XLOG(INFO) << " Out pkts on port: " << aPort
                   << " after:" << *portStatsAfter[aPort].outUnicastPkts_()
                   << std::endl;
        EXPECT_EVENTUALLY_EQ(*portStatsAfter[aPort].outUnicastPkts_(), 0);
      }
    });
  };
  verifyAcrossWarmBoots(setup, verify);
}
} // namespace facebook::fboss
