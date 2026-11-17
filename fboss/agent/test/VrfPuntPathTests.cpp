// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <folly/io/Cursor.h>

#include "fboss/agent/ArpHandler.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/hw/mock/MockHwSwitch.h"
#include "fboss/agent/packet/ArpHdr.h"
#include "fboss/agent/packet/EthHdr.h"
#include "fboss/agent/packet/Ethertype.h"
#include "fboss/agent/packet/ICMPHdr.h"
#include "fboss/agent/packet/IPProto.h"
#include "fboss/agent/packet/IPv6Hdr.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/MockTunManager.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/lib/CommonUtils.h"

using namespace facebook::fboss;
using ::testing::_;

namespace {
const RouterID kTestVrf{1001};
// testConfigA: intf 1 on VLAN 1, intf 55 on VLAN 55
const InterfaceID kDefaultVrfIntf{1};
const PortID kDefaultVrfPort{1};
const VlanID kDefaultVrfVlan{1};
const InterfaceID kVrfIntf{55};
const VlanID kVrfVlan{55};
// Target and VLAN of an ARP request or NDP solicitation sent by the switch
struct NeighborRequest {
  folly::IPAddress target;
  std::optional<VlanID> vlan;
};

std::optional<NeighborRequest> parseNeighborRequest(const TxPacket* pkt) {
  folly::io::Cursor c(pkt->buf());
  EthHdr eth(c);
  std::optional<VlanID> vlan;
  if (!eth.getVlanTags().empty()) {
    vlan = VlanID(eth.getVlanTags()[0].vid());
  }
  if (eth.getEtherType() == static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_ARP)) {
    ArpHdr arp(c);
    if (arp.oper == ARP_OP_REQUEST) {
      return NeighborRequest{arp.tpa, vlan};
    }
  } else if (
      eth.getEtherType() == static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6)) {
    IPv6Hdr ipv6(c);
    if (ipv6.nextHeader == static_cast<uint8_t>(IP_PROTO::IP_PROTO_IPV6_ICMP)) {
      ICMPHdr icmp(c);
      if (icmp.type ==
          static_cast<uint8_t>(
              ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_SOLICITATION)) {
        c.skip(4); // reserved
        return NeighborRequest{PktUtil::readIPv6(&c), vlan};
      }
    }
  }
  return std::nullopt;
}

class VrfPuntPathTest : public ::testing::Test {
 protected:
  void SetUp() override {
    config_ = testConfigA();
    handle_ = createTestHandle(&config_, SwitchFlags::ENABLE_TUN);
    sw_ = handle_->getSw();
    tunMgr_ = dynamic_cast<MockTunManager*>(sw_->getTunManager());
    ASSERT_NE(tunMgr_, nullptr);
    EXPECT_CALL(*tunMgr_, sendPacketToHost_(_))
        .WillRepeatedly([this](auto args) {
          toHost_.wlock()->push_back(std::get<0>(args));
          return true;
        });
    VrfEntry vrf;
    vrf.vrfId() = kTestVrf;
    sw_->addVrfs({vrf});
    sw_->updateInterfaceVrfs({{kVrfIntf, kTestVrf}});
    for (const auto& [_, portMap] :
         std::as_const(*sw_->getState()->getPorts())) {
      for (const auto& [_, port] : std::as_const(*portMap)) {
        auto vlans = port->getVlans();
        if (vlans.find(kVrfVlan) != vlans.end()) {
          vrfPort_ = port->getID();
        }
      }
    }
    EXPECT_HW_CALL(sw_, sendPacketSwitchedAsync_(_))
        .WillRepeatedly([this](TxPacket* pkt) {
          std::unique_ptr<TxPacket> owned(pkt);
          if (auto req = parseNeighborRequest(owned.get())) {
            requests_.wlock()->push_back(*req);
          }
          return true;
        });
  }

  // Receive a UDP packet on port and return the host interfaces it was sent to
  std::vector<InterfaceID> receive(
      PortID port,
      VlanID vlan,
      const folly::IPAddress& src,
      const folly::IPAddress& dst) {
    toHost_.wlock()->clear();
    auto mac = sw_->getState()->getInterfaces()->getNode(kVrfIntf)->getMac();
    auto pkt = utility::makeUDPTxPacket(
        [](uint32_t size) { return TxPacket::allocateTxPacket(size); },
        vlan,
        folly::MacAddress("02:00:00:00:00:99"),
        mac,
        src,
        dst,
        10000,
        20000);
    handle_->rxPacket(pkt->buf()->clone(), PortDescriptor(port), vlan);
    return *toHost_.rlock();
  }

  std::vector<InterfaceID> receiveInVrf(const char* src, const char* dst) {
    return receive(
        vrfPort_, kVrfVlan, folly::IPAddress(src), folly::IPAddress(dst));
  }

  std::vector<InterfaceID> receiveInDefaultVrf(
      const char* src,
      const char* dst) {
    return receive(
        kDefaultVrfPort,
        kDefaultVrfVlan,
        folly::IPAddress(src),
        folly::IPAddress(dst));
  }

  std::vector<NeighborRequest> requestsFor(const char* target) {
    auto ip = folly::IPAddress(target);
    std::vector<NeighborRequest> out;
    auto requests = requests_.rlock();
    for (const auto& req : *requests) {
      if (req.target == ip) {
        out.push_back(req);
      }
    }
    return out;
  }

  // Wait for at least count requests for target, all sent out of vlan
  void expectRequests(const char* target, VlanID vlan, size_t count = 1) {
    WITH_RETRIES_N_TIMED(100, std::chrono::milliseconds(50), {
      EXPECT_EVENTUALLY_GE(requestsFor(target).size(), count) << target;
    });
    for (const auto& req : requestsFor(target)) {
      EXPECT_EQ(req.vlan, vlan) << target;
    }
  }

  // Send an L3 packet from the host out of intf, as the TUN path does
  void sendFromHost(InterfaceID intf, const char* dst) {
    auto dstIp = folly::IPAddress(dst);
    auto src = dstIp.isV4() ? folly::IPAddress("10.0.55.1")
                            : folly::IPAddress("2401:db00:2110:3055::1");
    auto pkt = utility::makeUDPTxPacket(
        [](uint32_t size) { return TxPacket::allocateTxPacket(size); },
        kVrfVlan,
        folly::MacAddress("02:00:00:00:00:99"),
        folly::MacAddress("02:00:00:00:00:98"),
        src,
        dstIp,
        10000,
        20000);
    // Strip the L2 header (with VLAN tag), keeping it as headroom
    pkt->buf()->trimStart(18);
    sw_->sendL3Packet(std::move(pkt), intf);
  }

  void addRoute(RouterID vrf, const std::string& prefix, const char* nhop) {
    auto cidr = folly::IPAddress::createNetwork(prefix);
    RouteNextHopSet nhops{
        UnresolvedNextHop(folly::IPAddress(nhop), UCMP_DEFAULT_WEIGHT)};
    auto updater = sw_->getRouteUpdater();
    updater.addRoute(
        vrf,
        cidr.first,
        cidr.second,
        ClientID::BGPD,
        RouteNextHopEntry(nhops, AdminDistance::EBGP));
    updater.program();
  }

  cfg::SwitchConfig config_;
  std::unique_ptr<HwTestHandle> handle_;
  SwSwitch* sw_{nullptr};
  MockTunManager* tunMgr_{nullptr};
  PortID vrfPort_{0};
  folly::Synchronized<std::vector<InterfaceID>> toHost_;
  folly::Synchronized<std::vector<NeighborRequest>> requests_;
};

using IntfIDs = std::vector<InterfaceID>;
} // namespace

// After SetUp (testConfigA):
//   intf 1:  VLAN 1, port 1, VRF 0
//            10.0.0.1/24, 192.168.0.1/24, 2401:db00:2110:3001::1/64, fe80::/64
//   intf 55: VLAN 55, vrfPort_, VRF 1001
//            10.0.55.1/24, 192.168.55.1/24, 2401:db00:2110:3055::1/64,
//            169.254.0.0/16

// A packet to a switch address reaches the host only when it is received in
// that address's VRF, and delivery follows the interface across VRF moves
TEST_F(VrfPuntPathTest, LocalDelivery) {
  const IntfIDs toVrfIntf{kVrfIntf};
  const IntfIDs toDefaultIntf{kDefaultVrfIntf};
  EXPECT_EQ(receiveInVrf("10.0.55.10", "10.0.55.1"), toVrfIntf);
  EXPECT_EQ(
      receiveInVrf("2401:db00:2110:3055::10", "2401:db00:2110:3055::1"),
      toVrfIntf);
  // v4 link-local address of intf 55
  EXPECT_EQ(receiveInVrf("169.254.0.10", "169.254.0.0"), toVrfIntf);
  EXPECT_EQ(receiveInDefaultVrf("10.0.0.10", "10.0.0.1"), toDefaultIntf);
  EXPECT_EQ(
      receiveInDefaultVrf("2401:db00:2110:3001::10", "2401:db00:2110:3001::1"),
      toDefaultIntf);

  EXPECT_TRUE(receiveInVrf("10.0.55.10", "10.0.0.1").empty());
  EXPECT_TRUE(receiveInVrf("2401:db00:2110:3055::10", "2401:db00:2110:3001::1")
                  .empty());
  EXPECT_TRUE(receiveInDefaultVrf("10.0.0.10", "10.0.55.1").empty());
  EXPECT_TRUE(
      receiveInDefaultVrf("2401:db00:2110:3001::10", "2401:db00:2110:3055::1")
          .empty());
  EXPECT_TRUE(receiveInDefaultVrf("169.254.0.10", "169.254.0.0").empty());

  sw_->updateInterfaceVrfs({{kVrfIntf, RouterID(0)}});
  EXPECT_EQ(receiveInVrf("10.0.55.10", "10.0.55.1"), toVrfIntf);
  EXPECT_EQ(receiveInDefaultVrf("10.0.0.10", "10.0.55.1"), toVrfIntf);
}

// A transit packet to an unresolved destination resolves its host or next hop
// in the ingress VRF, never through routes of another VRF
TEST_F(VrfPuntPathTest, GleanInVrf) {
  addRoute(kTestVrf, "100.1.1.0/24", "10.0.55.30");
  addRoute(kTestVrf, "2001:db8:300::/64", "2401:db00:2110:3055::30");
  receiveInVrf("10.0.55.10", "10.0.55.20");
  receiveInVrf("2401:db00:2110:3055::10", "2401:db00:2110:3055::20");
  receiveInVrf("10.0.55.10", "100.1.1.1");
  receiveInVrf("2401:db00:2110:3055::10", "2001:db8:300::1");
  expectRequests("10.0.55.20", kVrfVlan);
  expectRequests("2401:db00:2110:3055::20", kVrfVlan);
  expectRequests("10.0.55.30", kVrfVlan);
  expectRequests("2401:db00:2110:3055::30", kVrfVlan);

  // Hosts only VRF 0 reaches: no request. VRF 0 punts queued after them are a
  // barrier showing they were handled.
  receiveInVrf("10.0.55.10", "10.0.0.20");
  receiveInVrf("2401:db00:2110:3055::10", "2401:db00:2110:3001::20");
  receiveInDefaultVrf("10.0.0.10", "10.0.0.21");
  receiveInDefaultVrf("2401:db00:2110:3001::10", "2401:db00:2110:3001::21");
  expectRequests("10.0.0.21", kDefaultVrfVlan);
  expectRequests("2401:db00:2110:3001::21", kDefaultVrfVlan);
  EXPECT_TRUE(requestsFor("10.0.0.20").empty());
  EXPECT_TRUE(requestsFor("2401:db00:2110:3001::20").empty());
}

// A packet the host sends out of a VRF interface resolves its destination or
// next hop in that VRF
TEST_F(VrfPuntPathTest, HostPacketResolvesInVrf) {
  addRoute(kTestVrf, "2001:db8:400::/64", "2401:db00:2110:3055::40");
  sendFromHost(kVrfIntf, "10.0.55.40");
  sendFromHost(kVrfIntf, "2001:db8:400::1");
  expectRequests("10.0.55.40", kVrfVlan);
  expectRequests("2401:db00:2110:3055::40", kVrfVlan);
}
