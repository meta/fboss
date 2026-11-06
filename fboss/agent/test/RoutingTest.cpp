/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gtest/gtest.h>

#include <fmt/core.h>
#include <folly/IPAddress.h>
#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include <folly/MacAddress.h>
#include <folly/MoveWrapper.h>
#include <folly/String.h>
#include <folly/io/IOBuf.h>
#include <folly/logging/xlog.h>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/IPv4Handler.h"
#include "fboss/agent/IPv6Handler.h"
#include "fboss/agent/RxPacket.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/TunManager.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/packet/EthHdr.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/CounterCache.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/MockTunManager.h"
#include "fboss/agent/test/TestPacketFactory.h"
#include "fboss/agent/test/TestUtils.h"

using namespace facebook::fboss;

using ::testing::_;

using folly::IOBuf;
using folly::IPAddressV4;
using folly::IPAddressV6;

namespace {

const folly::MacAddress kEmptyMac("00:00:00:00:00:00");

// Link-local addresses automatically assigned on all interfaces.
const folly::IPAddressV6 kIPv6LinkLocalAddr(
    IPAddressV6::LINK_LOCAL,
    MockPlatform::getMockLocalMac());

// Interface addresses
const folly::IPAddressV4 kIPv4IntfAddr1("10.0.0.11");
const folly::IPAddressV4 kIPv4IntfAddr2("10.0.0.21");
const folly::IPAddressV6 kIPv6IntfAddr1("face:b00c::11");
const folly::IPAddressV6 kIPv6IntfAddr2("face:b00c::21");

// Link local interface addresses
const folly::IPAddressV4 kllIPv4IntfAddr1("169.254.1.1");
const folly::IPAddressV4 kllIPv4IntfAddr2("169.254.2.2");
const folly::IPAddressV6 kllIPv6IntfAddr1("fe80::1");
const folly::IPAddressV6 kllIPv6IntfAddr2("fe80::2");

// Neighbor addresses
const folly::IPAddressV4 kIPv4NbhAddr1("10.0.0.10");
const folly::IPAddressV4 kIPv4NbhAddr2("10.0.0.20");
const folly::IPAddressV6 kIPv6NbhAddr1("face:b00c::10");
const folly::IPAddressV6 kIPv6NbhAddr2("face:b00c::20");
const folly::IPAddressV4 kllIPv4NbhAddr1("169.254.1.55");
const folly::IPAddressV4 kllIPv4NbhAddr2("169.254.2.55");
const folly::IPAddressV6 kllIPv6NbhAddr("fe80::7efe:90ff:fe2e:6e47");
const folly::MacAddress kNbhMacAddr1("00:02:c9:55:b7:a4");
const folly::MacAddress kNbhMacAddr2("6c:40:08:99:8d:d8");

// Multicast addresses and their corresponding mac addresses
const folly::IPAddressV4 kIPv4McastAddr("224.0.0.1");
const folly::IPAddressV6 kIPv6McastAddr("ff02::1");
const folly::MacAddress kIPv4MacAddr("01:00:5E:00:00:01");
const folly::MacAddress kIPv6MacAddr("33:33:00:00:00:01");

/**
 * Utility function to create V4 packet based on src/dst addresses.
 */
const IOBuf createV4UnicastPacket(
    const folly::IPAddressV4& srcAddr,
    const folly::IPAddressV4& dstAddr,
    const folly::MacAddress& srcMac = MockPlatform::getMockLocalMac(),
    const folly::MacAddress& dstMac = MockPlatform::getMockLocalMac()) {
  return createV4Packet(srcAddr, dstAddr, srcMac, dstMac);
}

/**
 * Utility function to create V6 packet based on src/dst addresses.
 */
const IOBuf createV6UnicastPacket(
    const folly::IPAddressV6& srcAddr,
    const folly::IPAddressV6& dstAddr,
    const folly::MacAddress& srcMac = MockPlatform::getMockLocalMac(),
    const folly::MacAddress& dstMac = MockPlatform::getMockLocalMac()) {
  return createV6Packet(srcAddr, dstAddr, srcMac, dstMac);
}

/**
 * Utility function to create multicast packets with fixed destination
 * address.
 */
const IOBuf createV4MulticastPacket(const folly::IPAddressV4& srcAddr) {
  return createV4Packet(
      srcAddr, kIPv4McastAddr, MockPlatform::getMockLocalMac(), kIPv4MacAddr);
}
const IOBuf createV6MulticastPacket(const folly::IPAddressV6& srcAddr) {
  return createV6Packet(
      srcAddr, kIPv6McastAddr, MockPlatform::getMockLocalMac(), kIPv6MacAddr);
}

/**
 * Expectation comparison for RxPacket being sent to Host.
 */
RxMatchFn matchRxPacket(const IOBuf& expBuf) {
  return [expBuf](const RxPacket* rcvdPkt) {
    if (!folly::IOBufEqualTo()(expBuf, *rcvdPkt->buf())) {
      throw FbossError("Expected rx-packet is not same as received packet");
    }

    // All good
    return;
  };
}

/**
 * Expectation comparison for TxPacket being sent to Switch
 */
TxMatchFn matchTxPacket(
    const folly::MacAddress& srcMac,
    const folly::MacAddress& dstMac,
    VlanID vlanID,
    uint16_t protocol,
    std::unique_ptr<IOBuf> expBuf) {
  // Write these into header. NOTE expPkt is being modified for comparison.
  expBuf->prepend(EthHdr::SIZE);
  folly::io::RWPrivateCursor rwCursor(expBuf.get());
  TxPacket::writeEthHeader(&rwCursor, dstMac, srcMac, vlanID, protocol);

  auto wrappedExpBuf = folly::makeMoveWrapper(std::move(expBuf));
  return [wrappedExpBuf](const TxPacket* rcvdPkt) {
    auto buf = rcvdPkt->buf();
    XLOG(DBG2) << "-------------";
    XLOG(DBG2) << "\n" << folly::hexDump(buf->data(), buf->length());
    XLOG(DBG2) << "\n"
               << folly::hexDump(
                      wrappedExpBuf->get(), (*wrappedExpBuf)->length());

    if (!folly::IOBufEqualTo()(**wrappedExpBuf, *rcvdPkt->buf())) {
      throw FbossError("Expected tx-packet is not same as received packet");
    }

    // All good
    return;
  };
}

class RoutingFixture : public ::testing::Test {
 public:
  void SetUp() override {
    auto config = getSwitchConfig();
    config.switchSettings()->switchIdToSwitchInfo() = {
        {0,
         createSwitchInfo(
             cfg::SwitchType::NPU,
             cfg::AsicType::ASIC_TYPE_MOCK,
             cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN(),
             cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX(),
             0, /* switchIndex */
             std::nullopt, /* sysPortMin */
             std::nullopt, /* sysPortMax */
             MockPlatform::getMockLocalMac().toString())}};
    this->handle = createTestHandle(&config, SwitchFlags::ENABLE_TUN);
    sw = this->handle->getSw();

    // Get TunManager pointer
    this->tunMgr = dynamic_cast<MockTunManager*>(sw->getTunManager());
    ASSERT_NE(this->tunMgr, nullptr);

    // Add some ARP/NDP entries so that link-local addresses can be routed
    // as well.
    auto updateFn = [=](const std::shared_ptr<SwitchState>& state) {
      std::shared_ptr<SwitchState> newState{state};

      ArpTable *arpTable1, *arpTable2;
      NdpTable *ndpTable1, *ndpTable2;
      auto* intf1 = newState->getInterfaces()->getNodeIf(InterfaceID(1)).get();
      auto* intf2 = newState->getInterfaces()->getNodeIf(InterfaceID(2)).get();
      arpTable1 = intf1->getArpTable().get()->modify(&intf1, &newState);
      ndpTable1 = intf1->getNdpTable().get()->modify(&intf1, &newState);
      arpTable2 = intf2->getArpTable().get()->modify(&intf2, &newState);
      ndpTable2 = intf2->getNdpTable().get()->modify(&intf2, &newState);

      arpTable1->addEntry(
          kIPv4NbhAddr1,
          kNbhMacAddr1,
          PortDescriptor(PortID(1)),
          InterfaceID(1));
      arpTable1->addEntry(
          kllIPv4NbhAddr1,
          kNbhMacAddr1,
          PortDescriptor(PortID(1)),
          InterfaceID(1));
      arpTable2->addEntry(
          kIPv4NbhAddr2,
          kNbhMacAddr2,
          PortDescriptor(PortID(2)),
          InterfaceID(2));
      arpTable2->addEntry(
          kllIPv4NbhAddr2,
          kNbhMacAddr2,
          PortDescriptor(PortID(2)),
          InterfaceID(2));
      ndpTable1->addEntry(
          kIPv6NbhAddr1,
          kNbhMacAddr1,
          PortDescriptor(PortID(1)),
          InterfaceID(1));
      ndpTable1->addEntry(
          kllIPv6NbhAddr,
          kNbhMacAddr1,
          PortDescriptor(PortID(1)),
          InterfaceID(1));
      ndpTable2->addEntry(
          kIPv6NbhAddr2,
          kNbhMacAddr2,
          PortDescriptor(PortID(2)),
          InterfaceID(2));
      ndpTable2->addEntry(
          kllIPv6NbhAddr,
          kNbhMacAddr2,
          PortDescriptor(PortID(2)),
          InterfaceID(2));

      return newState;
    };
    sw->updateState("ARP/NDP Entries Update", std::move(updateFn));

    // Apply initial config and verify basic initialization call sequence
    EXPECT_CALL(*this->tunMgr, sync(_)).Times(1);
    EXPECT_CALL(*this->tunMgr, startObservingUpdates()).Times(1);
    sw->initialConfigApplied(std::chrono::steady_clock::now());
  }

  void TearDown() override {
    this->tunMgr = nullptr;
    sw = nullptr;
    this->handle.reset();
  }

  /**
  * Utility function to initialize interface configuration. We assign
  * - 1 globally routable v6 address
  * - 1 globally routable v4 address
  * - 1 link-local v4 address (different on all interfaces)
  - - 1 link-local v6 address (different on all interfaces)
  * NOTE: v6 link-local address will be assigned automatically based on mac
  */
  static void initializeInterfaceConfig(cfg::Interface& intf, int32_t id) {
    *intf.intfID() = id;
    *intf.vlanID() = id;
    intf.name() = fmt::format("Interface-{}", id);
    intf.mtu() = 9000;
    intf.ipAddresses()->resize(4);
    intf.ipAddresses()[0] = fmt::format("169.254.{}.{}/32", id, id);
    intf.ipAddresses()[1] = fmt::format("10.0.0.{}1/31", id);
    intf.ipAddresses()[2] = fmt::format("face:b00c::{}1/127", id);
    intf.ipAddresses()[3] = fmt::format("fe80::{}/64", id);
  }

  /**
   * VLANs => 1, 2
   * Interfaces => 1, 2 (corresponding to their vlans)
   * Ports => 1, 2 (corresponding to their vlans and interfaces)
   */
  static cfg::SwitchConfig getSwitchConfig() {
    cfg::SwitchConfig config;

    // Add two VLANs with ID 1 and 2
    config.vlans()->resize(2);
    *config.vlans()[0].name() = "Vlan-1";
    *config.vlans()[0].id() = 1;
    *config.vlans()[0].routable() = true;
    config.vlans()[0].intfID() = 1;
    *config.vlans()[1].name() = "Vlan-2";
    *config.vlans()[1].id() = 2;
    *config.vlans()[1].routable() = true;
    config.vlans()[1].intfID() = 2;

    // Add two ports with ID 1 and 2 associated with VLAN 1 and 2 respectively
    config.ports()->resize(2);
    config.vlanPorts()->resize(2);
    for (int i = 0; i < 2; ++i) {
      auto& port = config.ports()[i];
      preparedMockPortConfig(port, i + 1);
      *port.minFrameSize() = 64;
      *port.maxFrameSize() = 9000;
      *port.routable() = true;
      *port.ingressVlan() = i + 1;

      auto& vlanPort = config.vlanPorts()[i];
      *vlanPort.vlanID() = i + 1;
      *vlanPort.logicalPort() = i + 1;
      *vlanPort.spanningTreeState() = cfg::SpanningTreeState::FORWARDING;
      *vlanPort.emitTags() = 0;
    }

    config.interfaces()->resize(2);
    initializeInterfaceConfig(config.interfaces()[0], 1);
    initializeInterfaceConfig(config.interfaces()[1], 2);

    return config;
  }

  // Member variables. Kept public for each access.
  SwSwitch* sw{nullptr};
  std::unique_ptr<HwTestHandle> handle{nullptr};
  MockTunManager* tunMgr{nullptr};

  const InterfaceID ifID1{1};
  const InterfaceID ifID2{2};
};

/**
 * Verify flow of unicast packets from switch to host for both v4 and v6.
 * Interface is identified based on the dstAddr on packet among all switch
 * interfaces.
 */
TEST_F(RoutingFixture, SwitchToHostUnicast) {
  // Cache the current stats
  CounterCache counters(this->sw);

  // v4 packet destined to intf1 address from any address. Destination Linux
  // Interface is identified based on srcVlan.
  // NOTE: the srcVlan and destAddr belong to different address. But packet will
  // still be forwarded to host because dest is one of interface's address.
  {
    auto pkt = createV4UnicastPacket(kIPv4NbhAddr2, kIPv4IntfAddr1);

    EXPECT_TUN_PKT(this->tunMgr, "V4 UcastPkt", this->ifID1, matchRxPacket(pkt))
        .Times(1);

    this->handle->rxPacket(
        std::make_unique<IOBuf>(pkt), PortDescriptor(PortID(2)), VlanID(2));

    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.rx.sum", 1);
    counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.drops.sum", 0);
  }

  // v4 packet destined to non-interface address should be dropped.
  {
    const folly::IPAddressV4 unknownDest("10.152.35.65");
    auto pkt = createV4UnicastPacket(kIPv4NbhAddr1, unknownDest);

    EXPECT_TUN_PKT(this->tunMgr, "V4 UcastPkt", this->ifID1, matchRxPacket(pkt))
        .Times(0);

    this->handle->rxPacket(
        std::make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), VlanID(1));

    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.rx.sum", 0);
    counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.drops.sum", 1);
  }

  // v6 packet destined to intf1 address from any address. Destination Linux
  // Interface is identified based on srcVlan (not verified here).
  {
    auto pkt = createV6UnicastPacket(kIPv6NbhAddr1, kIPv6IntfAddr1);

    EXPECT_TUN_PKT(this->tunMgr, "V6 UcastPkt", this->ifID1, matchRxPacket(pkt))
        .Times(1);

    this->handle->rxPacket(
        std::make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), VlanID(1));

    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.rx.sum", 1);
    counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.drops.sum", 0);
  }

  // v6 packet destined to non-interface address should be dropped.
  {
    const folly::IPAddressV6 unknownDest("2803:6082:990:8c2d::1");
    auto pkt = createV6UnicastPacket(kIPv6NbhAddr2, unknownDest);

    EXPECT_TUN_PKT(this->tunMgr, "V6 UcastPkt", this->ifID2, matchRxPacket(pkt))
        .Times(0);

    this->handle->rxPacket(
        std::make_unique<IOBuf>(pkt), PortDescriptor(PortID(2)), VlanID(2));

    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.rx.sum", 0);
    counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.drops.sum", 1);
  }
}

/**
 * Verify flow of link-local packets from switch to host for both v4 and v6.
 * Interface is identified based on the dstAddr on packet within scope of a
 * VLAN on which packet is received.
 *
 * NOTE: All interfaces in our UTs has same v4 and v6 link local address. It is
 * possible to have different link-local addresses on difference interfaces. It
 * doesn't matter from routing perspective.
 *
 */
TEST_F(RoutingFixture, SwitchToHostLinkLocalUnicast) {
  // Cache the current stats
  CounterCache counters(this->sw);

  auto verifyV4LLUcastPkt = [&](PortID portID) {
    auto pkt = createV4UnicastPacket(kllIPv4NbhAddr2, kllIPv4IntfAddr2);

    EXPECT_TUN_PKT(
        this->tunMgr, "V4 llUcastPkt", this->ifID2, matchRxPacket(pkt))
        .Times(1);

    this->handle->rxPacket(
        std::make_unique<IOBuf>(pkt), PortDescriptor(portID), VlanID(2));

    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.rx.sum", 1);
    counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.drops.sum", 0);
  };

  // v4 packet destined to intf2 link-local address from any address but from
  // same VLAN as of intf2
  verifyV4LLUcastPkt(PortID(2));

  // v4 link local packet destined to non-interface address should be dropped.
  {
    const folly::IPAddressV4 llDstAddrUnknown("169.254.3.4");
    auto pkt = createV4UnicastPacket(kllIPv4NbhAddr1, llDstAddrUnknown);

    EXPECT_TUN_PKT(
        this->tunMgr, "V4 llUcastPkt", this->ifID1, matchRxPacket(pkt))
        .Times(0);

    this->handle->rxPacket(
        std::make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), VlanID(1));

    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.rx.sum", 0);
    counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.drops.sum", 1);
  }

  // v4 link local packet destined to interface address outside of srcVlan
  // should be routed like normal v4 IP packet. E.g. packet received on Vlan(2)
  // for ll-addr of Interface(1)
  // NOTE: Scope of link-local packet is purely within single VLAN.
  {
    auto pkt = createV4UnicastPacket(kllIPv4NbhAddr2, kllIPv4IntfAddr1);

    EXPECT_TUN_PKT(
        this->tunMgr, "V4 llUcastPkt", this->ifID1, matchRxPacket(pkt))
        .Times(1);
    this->handle->rxPacket(
        std::make_unique<IOBuf>(pkt), PortDescriptor(PortID(2)), VlanID(2));
    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.rx.sum", 1);
    counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.drops.sum", 0);
  }

  // v6 packet destined to intf1 link-local address from any address.
  // Destination Linux interface is identified based on srcVlan
  // NOTE: We have two ipv6 link-local addresses
  {
    auto pkt = createV6UnicastPacket(kllIPv6NbhAddr, kIPv6LinkLocalAddr);

    EXPECT_TUN_PKT(
        this->tunMgr, "V6 llUcastPkt", this->ifID1, matchRxPacket(pkt))
        .Times(1);

    this->handle->rxPacket(
        std::make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), VlanID(1));

    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.rx.sum", 1);
    counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.drops.sum", 0);
  }

  // v6 packet destined to unknwon link-local address should be dropped.
  {
    const folly::IPAddressV6 llDstAddrUnknown("fe80::202:c9ff:fe55:b7a4");
    auto pkt = createV6UnicastPacket(kllIPv6NbhAddr, llDstAddrUnknown);

    EXPECT_TUN_PKT(
        this->tunMgr, "V6 llUcastPkt", this->ifID2, matchRxPacket(pkt))
        .Times(0);

    this->handle->rxPacket(
        std::make_unique<IOBuf>(pkt), PortDescriptor(PortID(2)), VlanID(2));

    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.rx.sum", 0);
    counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.drops.sum", 1);
  }

  // v6 packet destined to intf1 link-local address but received on vlan(2)
  // (outside of vlan), then it should be dropped.
  {
    auto pkt = createV6UnicastPacket(kllIPv6NbhAddr, kllIPv6IntfAddr1);

    EXPECT_TUN_PKT(
        this->tunMgr, "V6 llUcastPkt", this->ifID2, matchRxPacket(pkt))
        .Times(0);

    this->handle->rxPacket(
        std::make_unique<IOBuf>(pkt), PortDescriptor(PortID(2)), VlanID(2));

    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.rx.sum", 0);
    counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.drops.sum", 1);
  }
}

/**
 * Verif flow of multicast packets from switch to host for both v4 and v6.
 * Interface is directly derived from VLAN.
 */
TEST_F(RoutingFixture, SwitchToHostMulticast) {
  // Cache the current stats
  CounterCache counters(this->sw);

  // Packet destined to v4 multicast address should always be forwarded to
  // Host regarless of it's dest/src IP
  {
    auto pkt = createV4MulticastPacket(kIPv4IntfAddr1);

    EXPECT_TUN_PKT(
        this->tunMgr, "V4 llMcastPkt", this->ifID1, matchRxPacket(pkt))
        .Times(1);
    this->handle->rxPacket(
        std::make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), VlanID(1));

    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.rx.sum", 1);
    counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.drops.sum", 0);
  }

  // Packet destined to v6 multicast address should always be forwarded to
  // Host regarless of it's dest/src IP
  {
    auto pkt = createV6MulticastPacket(kIPv6IntfAddr1);

    EXPECT_TUN_PKT(
        this->tunMgr, "V6 llMcastPkt", this->ifID1, matchRxPacket(pkt))
        .Times(1);

    this->handle->rxPacket(
        std::make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), VlanID(1));

    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.rx.sum", 1);
    counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.drops.sum", 0);
  }
}

namespace {

// IPv6 multicast packet from fe80::7 to ff02::1 behind an 8 octet Hop-by-Hop
// header. upperLayer is the Hop-by-Hop Next Header followed by 16 octets of
// upper-layer payload.
folly::IOBuf createV6HopByHopMulticastPacket(const std::string& upperLayer) {
  return PktUtil::parseHexData(
      // dst mac, src mac
      "33 33 00 00 00 01  00 02 c9 55 b7 a4"
      // 802.1q, VLAN 1
      "81 00 00 01"
      // IPv6
      "86 dd"
      // Version 6, traffic class, flow label
      "60 00 00 00"
      // Payload length: 8 (Hop-by-Hop) + 16
      "00 18"
      // Next Header: 0 (Hop-by-Hop), Hop Limit (255)
      "00 ff"
      // src addr (fe80::7)
      "fe 80 00 00 00 00 00 00 00 00 00 00 00 00 00 07"
      // dst addr (ff02::1)
      "ff 02 00 00 00 00 00 00 00 00 00 00 00 00 00 01" +
      upperLayer);
}

} // namespace

/**
 * Neighbor Discovery behind IPv6 extension headers, e.g. a Router
 * Advertisement after a Hop-by-Hop header (T296568668), is dropped instead of
 * forwarded to host. Other ICMPv6 behind extension headers, e.g. MLD, is still
 * forwarded.
 */
TEST_F(RoutingFixture, SwitchToHostNdpWithExtensionHeaders) {
  CounterCache counters(this->sw);

  {
    auto pkt = createV6HopByHopMulticastPacket(
        // Hop-by-Hop: Next Header 58 (ICMPv6), Hdr Ext Len 0, PadN
        "3a 00 01 04 00 00 00 00"
        // Router Advertisement: type, code, checksum, cur hop limit, flags,
        // router lifetime, reachable time, retrans timer
        "86 00 00 00 40 00 00 00 00 00 00 00 00 00 00 00");

    EXPECT_CALL(*this->tunMgr, sendPacketToHost_(_)).Times(0);
    this->handle->rxPacket(
        std::make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), VlanID(1));

    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.rx.sum", 0);
    counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.drops.sum", 1);
  }

  {
    auto pkt = createV6HopByHopMulticastPacket(
        // Hop-by-Hop: Next Header 58 (ICMPv6), Hdr Ext Len 0, Router Alert
        // (MLD), PadN
        "3a 00 05 02 00 00 01 00"
        // MLD Query: type, code, checksum, max response delay, reserved,
        // multicast address (first 8 octets)
        "82 00 00 00 27 10 00 00 00 00 00 00 00 00 00 00");

    EXPECT_TUN_PKT(
        this->tunMgr, "V6 HopByHopMldPkt", this->ifID1, matchRxPacket(pkt))
        .Times(1);
    this->handle->rxPacket(
        std::make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), VlanID(1));

    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.rx.sum", 1);
    counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.drops.sum", 0);
  }
}

/**
 * Verify flow of unicast packets from host to switch for both v4 and v6.
 * All outgoing L2 packets to switch must have platform MAC as the destination
 * address as it will trigger L3 lookup in hardware.
 */
TEST_F(RoutingFixture, HostToSwitchUnicast) {
  // Cache the current stats
  CounterCache counters(this->sw);

  // Any unicast packet (except link-local) is forwarded to CPU at L3 lookup
  // state by setting src/dst mac address to be the platform mac address.
  {
    auto pkt = createTxPacket(
        this->sw,
        createV4UnicastPacket(
            kIPv4IntfAddr1, kIPv4NbhAddr1, kEmptyMac, kEmptyMac));

    auto bufCopy = IOBuf::copyBuffer(
        pkt->buf()->data(), pkt->buf()->length(), pkt->buf()->headroom(), 0);
    EXPECT_SWITCHED_PKT(
        this->sw,
        "V4 UcastPkt",
        matchTxPacket(
            MockPlatform::getMockLocalMac(),
            MockPlatform::getMockLocalMac(),
            VlanID(1),
            IPv4Handler::ETHERTYPE_IPV4,
            std::move(bufCopy)))
        .Times(1);
    this->sw->sendL3Packet(std::move(pkt), InterfaceID(1));

    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.tx.sum", 1);
  }

  // Same as above test but for v6
  {
    auto pkt = createTxPacket(
        this->sw,
        createV6UnicastPacket(
            kIPv6IntfAddr2, kIPv6NbhAddr2, kEmptyMac, kEmptyMac));

    auto bufCopy = IOBuf::copyBuffer(
        pkt->buf()->data(), pkt->buf()->length(), pkt->buf()->headroom(), 0);
    EXPECT_SWITCHED_PKT(
        this->sw,
        "V6 UcastPkt",
        matchTxPacket(
            MockPlatform::getMockLocalMac(),
            MockPlatform::getMockLocalMac(),
            VlanID(2),
            IPv6Handler::ETHERTYPE_IPV6,
            std::move(bufCopy)))
        .Times(1);
    this->sw->sendL3Packet(std::move(pkt), InterfaceID(2));

    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.tx.sum", 1);
  }
}

/**
 * Verify flow of link local packets from host to switch for both v4 and v6.
 * All outgoing L2 packets must have their MAC resolved in software.
 */
TEST_F(RoutingFixture, HostToSwitchLinkLocal) {
  // Cache the current stats
  CounterCache counters(this->sw);

  // v4 link-local
  {
    auto pkt = createTxPacket(
        this->sw,
        createV4UnicastPacket(
            kllIPv4IntfAddr1, kllIPv4NbhAddr1, kEmptyMac, kEmptyMac));

    auto bufCopy = IOBuf::copyBuffer(
        pkt->buf()->data(), pkt->buf()->length(), pkt->buf()->headroom(), 0);
    EXPECT_SWITCHED_PKT(
        this->sw,
        "V4 UcastPkt",
        matchTxPacket(
            MockPlatform::getMockLocalMac(),
            MockPlatform::getMockLocalMac(), // NOTE: no neighbor mac address
                                             // resolution like v6
            VlanID(1),
            IPv4Handler::ETHERTYPE_IPV4,
            std::move(bufCopy)))
        .Times(1);
    this->sw->sendL3Packet(std::move(pkt), InterfaceID(1));

    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.tx.sum", 1);
  }

  // Same as above test but for v6
  {
    auto pkt = createTxPacket(
        this->sw,
        createV6UnicastPacket(
            kllIPv6IntfAddr2, kllIPv6NbhAddr, kEmptyMac, kEmptyMac));

    auto bufCopy = IOBuf::copyBuffer(
        pkt->buf()->data(), pkt->buf()->length(), pkt->buf()->headroom(), 0);
    EXPECT_SWITCHED_PKT(
        this->sw,
        "V6 UcastPkt",
        matchTxPacket(
            MockPlatform::getMockLocalMac(),
            kNbhMacAddr2, // because vlan-id = 2
            VlanID(2),
            IPv6Handler::ETHERTYPE_IPV6,
            std::move(bufCopy)))
        .Times(1);
    this->sw->sendL3Packet(std::move(pkt), InterfaceID(2));

    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.tx.sum", 1);
  }

  // v6 packet with unknown link-local destination address (NDP not resolvable)
  // should trigger NDP request and packet gets dropped
  {
    const folly::IPAddressV6 llNbhAddrUnknown("fe80::face");
    auto pkt = createTxPacket(
        this->sw,
        createV6UnicastPacket(
            kllIPv6IntfAddr2, llNbhAddrUnknown, kEmptyMac, kEmptyMac));

    // NDP Neighbor solicitation request will be sent and the actual packet
    // will be dropped.
    EXPECT_HW_CALL(this->sw, sendPacketSwitchedAsync_(_)).Times(1);
    this->sw->sendL3Packet(std::move(pkt), InterfaceID(1));

    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.tx.sum", 0);
  }

  // v4 packet with unknown link-local destination address (ARP not resolvable)
  // should be just routed out of the box like other v4 IP packets. (unlike v6)
  {
    const folly::IPAddressV4 llNbhAddrUnknown("169.254.13.95");
    auto pkt = createTxPacket(
        this->sw,
        createV4UnicastPacket(
            kllIPv4IntfAddr2, llNbhAddrUnknown, kEmptyMac, kEmptyMac));

    auto bufCopy = IOBuf::copyBuffer(
        pkt->buf()->data(), pkt->buf()->length(), pkt->buf()->headroom(), 0);
    EXPECT_SWITCHED_PKT(
        this->sw,
        "V4 UcastPkt",
        matchTxPacket(
            MockPlatform::getMockLocalMac(),
            MockPlatform::getMockLocalMac(), // NOTE: no neighbor mac address
                                             // resolution like v6
            VlanID(1),
            IPv4Handler::ETHERTYPE_IPV4,
            std::move(bufCopy)))
        .Times(1);
    this->sw->sendL3Packet(std::move(pkt), InterfaceID(1));

    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.tx.sum", 1);
  }
}

/**
 * Verify flow of multicast packets from host to switch for both v4 and v6.
 * All outgoing multicast packets must have their destination mac address
 * based on their IPAddress.
 */
TEST_F(RoutingFixture, HostToSwitchMulticast) {
  // Cache the current stats
  CounterCache counters(this->sw);

  // Any multicast packet sent to switch must have it's source mac address
  // set appropriately as per it's multicast IP address.
  // This test is for v4
  {
    auto pkt = createTxPacket(
        this->sw,
        createV4UnicastPacket(
            kIPv4IntfAddr1, kIPv4McastAddr, kEmptyMac, kEmptyMac));

    auto bufCopy = IOBuf::copyBuffer(
        pkt->buf()->data(), pkt->buf()->length(), pkt->buf()->headroom(), 0);
    EXPECT_SWITCHED_PKT(
        this->sw,
        "V4 McastPkt",
        matchTxPacket(
            MockPlatform::getMockLocalMac(),
            kIPv4MacAddr,
            VlanID(1),
            IPv4Handler::ETHERTYPE_IPV4,
            std::move(bufCopy)))
        .Times(1);
    this->sw->sendL3Packet(std::move(pkt), InterfaceID(1));

    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.tx.sum", 1);
  }

  // Same as above test but for v6
  {
    auto pkt = createTxPacket(
        this->sw,
        createV6UnicastPacket(
            kIPv6IntfAddr2, kIPv6McastAddr, kEmptyMac, kEmptyMac));

    auto bufCopy = IOBuf::copyBuffer(
        pkt->buf()->data(), pkt->buf()->length(), pkt->buf()->headroom(), 0);
    EXPECT_SWITCHED_PKT(
        this->sw,
        "V6 McastPkt",
        matchTxPacket(
            MockPlatform::getMockLocalMac(),
            kIPv6MacAddr,
            VlanID(2),
            IPv6Handler::ETHERTYPE_IPV6,
            std::move(bufCopy)))
        .Times(1);
    this->sw->sendL3Packet(std::move(pkt), InterfaceID(2));

    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "host.tx.sum", 1);
  }
}

} // anonymous namespace
