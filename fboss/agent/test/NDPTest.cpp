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

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/LinkAggregationManager.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/ThriftHandler.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/ndp/IPv6RouteAdvertiser.h"
#include "fboss/agent/packet/EthHdr.h"
#include "fboss/agent/packet/Ethertype.h"
#include "fboss/agent/packet/ICMPHdr.h"
#include "fboss/agent/packet/IPv6Hdr.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/state/AggregatePort.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/NdpTable.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/CounterCache.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/NeighborEntryTest.h"
#include "fboss/agent/test/TestUtils.h"

#include <folly/IPAddressV6.h>
#include <folly/MacAddress.h>
#include <folly/io/Cursor.h>
#include <netinet/icmp6.h>
#include <future>
#include <thread>

using namespace facebook::fboss;
using facebook::network::toBinaryAddress;
using facebook::network::toIPAddress;
using facebook::network::thrift::BinaryAddress;
using folly::IOBuf;
using folly::IPAddress;
using folly::IPAddressV6;
using folly::MacAddress;
using folly::StringPiece;
using folly::io::Cursor;
using std::make_unique;
using std::shared_ptr;
using std::unique_ptr;
using std::chrono::seconds;

using ::testing::_;

namespace {
// TODO(joseph5wu) Network control strict priority queue
const uint8_t kNCStrictPriorityQueue = 7;
const AggregatePortID kAggregatePortID = AggregatePortID(300);
const int kSubportCount = 2;

cfg::SwitchConfig createSwitchConfig(
    seconds raInterval,
    seconds ndpTimeout,
    bool createAggPort = false,
    std::optional<std::string> routerAddress = std::nullopt,
    int numIntfs = 2) {
  // Create a thrift config to use
  cfg::SwitchConfig config;
  config.switchSettings()->switchIdToSwitchInfo() = {
      std::make_pair(0, createSwitchInfo(cfg::SwitchType::NPU))};
  config.vlans()->resize(numIntfs);
  *config.vlans()[0].name() = "PrimaryVlan";
  *config.vlans()[0].id() = 5;
  *config.vlans()[0].routable() = true;
  config.vlans()[0].intfID() = 5;
  *config.vlans()[1].name() = "DefaultHWVlan";
  *config.vlans()[1].id() = 1;
  *config.vlans()[1].routable() = true;
  config.vlans()[1].intfID() = 1;

  config.vlanPorts()->resize(10);
  config.ports()->resize(10);
  for (int n = 0; n < 10; ++n) {
    preparedMockPortConfig(config.ports()[n], n + 1);
    *config.ports()[n].minFrameSize() = 64;
    *config.ports()[n].maxFrameSize() = 9000;
    *config.ports()[n].routable() = true;
    *config.ports()[n].ingressVlan() = 5;

    *config.vlanPorts()[n].vlanID() = 5;
    *config.vlanPorts()[n].logicalPort() = n + 1;
    *config.vlanPorts()[n].spanningTreeState() =
        cfg::SpanningTreeState::FORWARDING;
    *config.vlanPorts()[n].emitTags() = 0;
  }

  config.interfaces()->resize(numIntfs);
  *config.interfaces()[0].intfID() = 5;
  *config.interfaces()[0].vlanID() = 5;
  config.interfaces()[0].name() = "PrimaryInterface";
  config.interfaces()[0].mtu() = 9000;
  config.interfaces()[0].ipAddresses()->resize(6);
  config.interfaces()[0].ipAddresses()[0] = "10.164.4.10/24";
  config.interfaces()[0].ipAddresses()[1] = "10.164.4.1/24";
  config.interfaces()[0].ipAddresses()[2] = "10.164.4.2/24";
  config.interfaces()[0].ipAddresses()[3] = "2401:db00:2110:3004::/64";
  config.interfaces()[0].ipAddresses()[4] = "2401:db00:2110:3004::000a/64";
  config.interfaces()[0].ipAddresses()[5] = "fe80::face:b00c/64";
  config.interfaces()[0].ndp() = cfg::NdpConfig();
  *config.interfaces()[0].ndp()->routerAdvertisementSeconds() =
      raInterval.count();
  if (routerAddress) {
    config.interfaces()[0].ndp()->routerAddress() = *routerAddress;
  }
  *config.interfaces()[1].intfID() = 1;
  *config.interfaces()[1].vlanID() = 1;
  config.interfaces()[1].name() = "DefaultHWInterface";
  config.interfaces()[1].mtu() = 9000;
  config.interfaces()[1].ipAddresses()->resize(3);
  config.interfaces()[1].ipAddresses()[0] = "20.164.4.10/24";
  config.interfaces()[1].ipAddresses()[1] = "3401:db00:2110:3004::a/64";
  config.interfaces()[1].ipAddresses()[2] = "fe80::face:b00c/64";

  for (int i = 2; i < numIntfs; i++) {
    int idx = i;
    int intfId = 100 + i;
    *config.interfaces()[idx].intfID() = intfId;
    *config.interfaces()[idx].vlanID() = intfId;
    config.interfaces()[idx].name() = "DefaultHWInterface";
    config.interfaces()[idx].mtu() = 9000;
    config.interfaces()[idx].ipAddresses()->resize(1);
    config.interfaces()[idx].ipAddresses()[0] = "fe80::face:b00c/64";
    *config.vlans()[idx].name() = "DefaultHWVlanTest";
    *config.vlans()[idx].id() = intfId;
    *config.vlans()[idx].routable() = true;
    config.vlans()[idx].intfID() = intfId;
  }
  if (ndpTimeout.count() > 0) {
    *config.arpTimeoutSeconds() = ndpTimeout.count();
  }

  if (createAggPort) {
    config.aggregatePorts()->resize(1);
    config.aggregatePorts()[0].key() = static_cast<uint16_t>(kAggregatePortID);
    config.aggregatePorts()[0].name() = "AggPort";
    config.aggregatePorts()[0].description() = "Test Aggport";
    config.aggregatePorts()[0].memberPorts()->resize(kSubportCount);
    for (auto i = 0; i < kSubportCount; i++) {
      config.aggregatePorts()[0].memberPorts()[i].memberPortID() = i + 1;
    }
  }

  return config;
}

cfg::SwitchConfig createSwitchConfigPortRif(
    seconds raInterval,
    seconds ndpTimeout,
    bool createAggPort = false,
    std::optional<std::string> routerAddress = std::nullopt,
    int numIntfs = 2) {
  // Create a thrift config to use
  cfg::SwitchConfig config = testConfigAWithPortInterfaces();

  config.interfaces()->resize(numIntfs);
  *config.interfaces()[0].intfID() = 5;
  *config.interfaces()[0].vlanID() = 0;
  config.interfaces()[0].name() = "PrimaryInterface";
  config.interfaces()[0].mtu() = 9000;
  config.interfaces()[0].ipAddresses()->resize(6);
  config.interfaces()[0].ipAddresses()[0] = "10.164.4.10/24";
  config.interfaces()[0].ipAddresses()[1] = "10.164.4.1/24";
  config.interfaces()[0].ipAddresses()[2] = "10.164.4.2/24";
  config.interfaces()[0].ipAddresses()[3] = "2401:db00:2110:3004::/64";
  config.interfaces()[0].ipAddresses()[4] = "2401:db00:2110:3004::000a/64";
  config.interfaces()[0].ipAddresses()[5] = "fe80::face:b00c/64";
  config.interfaces()[0].ndp() = cfg::NdpConfig();
  *config.interfaces()[0].ndp()->routerAdvertisementSeconds() =
      raInterval.count();
  if (routerAddress) {
    config.interfaces()[0].ndp()->routerAddress() = *routerAddress;
  }
  *config.interfaces()[1].intfID() = 1;
  *config.interfaces()[1].vlanID() = 0;
  config.interfaces()[1].name() = "DefaultHWInterface";
  config.interfaces()[1].mtu() = 9000;
  config.interfaces()[1].ipAddresses()->resize(3);
  config.interfaces()[1].ipAddresses()[0] = "20.164.4.10/24";
  config.interfaces()[1].ipAddresses()[1] = "3401:db00:2110:3004::a/64";
  config.interfaces()[1].ipAddresses()[2] = "fe80::face:b00c/64";

  for (int i = 2; i < numIntfs; i++) {
    int idx = i;
    int intfId = 100 + i;
    *config.interfaces()[idx].intfID() = intfId;
    *config.interfaces()[idx].vlanID() = 0;
    config.interfaces()[idx].name() = "DefaultHWInterface";
    config.interfaces()[idx].mtu() = 9000;
    config.interfaces()[idx].ipAddresses()->resize(1);
    config.interfaces()[idx].ipAddresses()[0] = "fe80::face:b00c/64";
    *config.vlans()[idx].name() = "DefaultHWVlanTest";
    *config.vlans()[idx].id() = intfId;
    *config.vlans()[idx].routable() = true;
    config.vlans()[idx].intfID() = intfId;
  }
  if (ndpTimeout.count() > 0) {
    *config.arpTimeoutSeconds() = ndpTimeout.count();
  }

  return config;
}

using PayloadCheckFn = std::function<void(Cursor* cursor, uint32_t length)>;

TxMatchFn checkICMPv6Pkt(
    MacAddress srcMac,
    IPAddressV6 srcIP,
    MacAddress dstMac,
    IPAddressV6 dstIP,
    VlanID vlan,
    ICMPv6Type type,
    const PayloadCheckFn& checkPayload) {
  return [=](const TxPacket* pkt) {
    Cursor c(pkt->buf());
    auto parsedDstMac = PktUtil::readMac(&c);
    checkField(dstMac, parsedDstMac, "dst mac");
    auto parsedSrcMac = PktUtil::readMac(&c);
    checkField(srcMac, parsedSrcMac, "src mac");
    auto vlanType = c.readBE<uint16_t>();
    checkField(
        static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_VLAN),
        vlanType,
        "VLAN ethertype");
    auto vlanTag = c.readBE<uint16_t>();
    checkField(static_cast<uint16_t>(vlan), vlanTag, "VLAN tag");
    auto ethertype = c.readBE<uint16_t>();
    checkField(
        static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6),
        ethertype,
        "ethertype");
    IPv6Hdr ipv6(c);
    checkField(
        static_cast<uint8_t>(IP_PROTO::IP_PROTO_IPV6_ICMP),
        ipv6.nextHeader,
        "IPv6 protocol");
    checkField(srcIP, ipv6.srcAddr, "src IP");
    checkField(dstIP, ipv6.dstAddr, "dst IP");

    Cursor ipv6PayloadStart(c);
    ICMPHdr icmp6(c);
    checkField(icmp6.computeChecksum(ipv6, c), icmp6.csum, "ICMPv6 checksum");
    checkField(static_cast<uint8_t>(type), icmp6.type, "ICMPv6 type");
    checkField(0, icmp6.code, "ICMPv6 code");

    checkPayload(&c, ipv6.payloadLength - ICMPHdr::SIZE);

    if (ipv6.payloadLength != (c - ipv6PayloadStart)) {
      throw FbossError(
          "IPv6 payload length mismatch: header says ",
          ipv6.payloadLength,
          " but we used ",
          c - ipv6PayloadStart);
    }

    // This is a match
    return;
  };
}

TxMatchFn checkNeighborAdvert(
    MacAddress srcMac,
    IPAddressV6 srcIP,
    MacAddress dstMac,
    IPAddressV6 dstIP,
    VlanID vlan,
    uint8_t flags) {
  auto checkPayload = [=](Cursor* cursor, uint32_t /*length*/) {
    auto parsedFlags = cursor->read<uint8_t>();
    checkField(flags, parsedFlags, "NA flags");
    checkField(0, cursor->read<uint8_t>(), "reserved1");
    checkField(0, cursor->read<uint8_t>(), "reserved2");
    checkField(0, cursor->read<uint8_t>(), "reserved3");
    auto targetIP = PktUtil::readIPv6(cursor);
    checkField(srcIP, targetIP, "target IP");

    auto optionType = cursor->read<uint8_t>();
    checkField(2, optionType, "target MAC option type");
    auto optionLength = cursor->read<uint8_t>();
    checkField(1, optionLength, "target MAC option length");
    auto targetMac = PktUtil::readMac(cursor);
    checkField(srcMac, targetMac, "target MAC");

    // This is a match
    return;
  };
  return checkICMPv6Pkt(
      srcMac,
      srcIP,
      dstMac,
      dstIP,
      vlan,
      ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_ADVERTISEMENT,
      checkPayload);
}

TxMatchFn checkNeighborSolicitation(
    MacAddress srcMac,
    IPAddressV6 srcIP,
    MacAddress dstMac,
    IPAddressV6 dstIP,
    IPAddressV6 targetIP,
    VlanID vlan,
    bool hasOption = true) {
  auto checkPayload = [=](Cursor* cursor, uint32_t /*length*/) {
    auto reserved = cursor->read<uint32_t>();
    checkField(0, reserved, "NS reserved field");
    auto parsedTargetIP = PktUtil::readIPv6(cursor);
    checkField(targetIP, parsedTargetIP, "target IP");

    if (hasOption) {
      auto optionType = cursor->read<uint8_t>();
      checkField(1, optionType, "source MAC option type");
      auto optionLength = cursor->read<uint8_t>();
      checkField(1, optionLength, "source MAC option length");
      auto srcMacOption = PktUtil::readMac(cursor);
      checkField(srcMac, srcMacOption, "source MAC option value");
    }
  };
  return checkICMPv6Pkt(
      srcMac,
      srcIP,
      dstMac,
      dstIP,
      vlan,
      ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_SOLICITATION,
      checkPayload);
}

using PrefixVector = std::vector<std::pair<IPAddressV6, uint8_t>>;
TxMatchFn checkRouterAdvert(
    MacAddress srcMac,
    IPAddressV6 srcIP,
    MacAddress dstMac,
    IPAddressV6 dstIP,
    VlanID vlan,
    const cfg::NdpConfig& ndp,
    uint32_t mtu,
    PrefixVector expectedPrefixes) {
  auto checkPayload = [=](Cursor* cursor, uint32_t length) {
    Cursor start(*cursor);
    Cursor end = start + length;
    auto parsedHopLimit = cursor->read<uint8_t>();
    checkField(*ndp.curHopLimit(), parsedHopLimit, "cur hop limit");
    auto parsedFlags = cursor->read<uint8_t>();
    checkField(0, parsedFlags, "NDP RA flags");
    auto parsedLifetime = cursor->readBE<uint16_t>();
    checkField(*ndp.routerLifetime(), parsedLifetime, "router lifetime");
    auto parsedReachableTime = cursor->readBE<uint32_t>();
    checkField(0, parsedReachableTime, "reachable time");
    auto parsedRetransTimer = cursor->readBE<uint32_t>();
    checkField(0, parsedRetransTimer, "retransmit timer");

    bool srcMacSeen = false;
    bool mtuSeen = false;
    PrefixVector prefixes;
    while (*cursor != end) {
      auto optionType = cursor->read<uint8_t>();
      auto optionLength = cursor->read<uint8_t>();
      // TODO: define constants for the option types
      switch (optionType) {
        case 1: {
          // src link-layer address
          checkField(1, optionLength, "src MAC option length");
          if (srcMacSeen) {
            throw FbossError("duplicate src MAC option found");
          }
          srcMacSeen = true;
          auto mac = PktUtil::readMac(cursor);
          checkField(srcMac, mac, "src MAC option");
          break;
        }
        case 3: {
          // prefix
          checkField(4, optionLength, "prefix option length");
          auto prefixLength = cursor->read<uint8_t>();
          auto prefixFlags = cursor->read<uint8_t>();
          checkField(0xc0, prefixFlags, "prefix flags");
          auto prefixValidLifetime = cursor->readBE<uint32_t>();
          checkField(
              *ndp.prefixValidLifetimeSeconds(),
              prefixValidLifetime,
              "prefix valid lifetime");
          auto prefixPreferredLifetime = cursor->readBE<uint32_t>();
          checkField(
              *ndp.prefixPreferredLifetimeSeconds(),
              prefixPreferredLifetime,
              "prefix preferred lifetime");
          auto reserved2 = cursor->readBE<uint32_t>();
          checkField(0, reserved2, "prefix option reserved2");
          auto prefix = PktUtil::readIPv6(cursor);
          prefixes.emplace_back(prefix, prefixLength);
          break;
        }
        case 5: {
          // MTU
          checkField(1, optionLength, "MTU option length");
          if (mtuSeen) {
            throw FbossError("duplicate MTU option found");
          }
          mtuSeen = true;
          auto reserved = cursor->readBE<uint16_t>();
          checkField(0, reserved, "MTU option reserved bytes");
          auto parsedMTU = cursor->readBE<uint32_t>();
          checkField(mtu, parsedMTU, "MTU option value");
          break;
        }
        default:
          // unexpected option type
          throw FbossError(
              "unexpected NDP option type ",
              optionType,
              " at payload offset ",
              (*cursor - start) - 2,
              "\n",
              PktUtil::hexDump(start));
      }
    }

    if (!srcMacSeen) {
      throw FbossError("no src MAC option found");
    }
    if (!mtuSeen) {
      throw FbossError("no MTU option found");
    }
    if (prefixes != expectedPrefixes) {
      // TODO: Print out useful info about the differences
      throw FbossError("mismatching advertised prefixes");
    }
  };
  return checkICMPv6Pkt(
      srcMac,
      srcIP,
      dstMac,
      dstIP,
      vlan,
      ICMPv6Type::ICMPV6_TYPE_NDP_ROUTER_ADVERTISEMENT,
      checkPayload);
}

void sendNeighborAdvertisement(
    HwTestHandle* handle,
    StringPiece ipStr,
    StringPiece macStr,
    const PortDescriptor& port,
    int vlanID,
    bool solicited = true) {
  IPAddressV6 srcIP(ipStr);
  MacAddress srcMac(macStr);
  VlanID vlan(vlanID);

  // The destination MAC and IP are specific to the mock switch we setup
  MacAddress dstMac(MockPlatform::getMockLocalMac());
  IPAddressV6 dstIP("2401:db00:2110:3004::a");
  size_t plen = 20;

  IPv6Hdr ipv6(srcIP, dstIP);
  ipv6.trafficClass = 0xe0;
  ipv6.payloadLength = ICMPHdr::SIZE + plen;
  ipv6.nextHeader = static_cast<uint8_t>(IP_PROTO::IP_PROTO_IPV6_ICMP);
  ipv6.hopLimit = 255;

  size_t totalLen = EthHdr::SIZE + IPv6Hdr::SIZE + ipv6.payloadLength;
  auto buf = IOBuf::create(totalLen);
  buf->append(totalLen);
  folly::io::RWPrivateCursor cursor(buf.get());

  auto bodyFn = [&](folly::io::RWPrivateCursor* c) {
    c->write<uint32_t>(
        ND_NA_FLAG_OVERRIDE | (solicited ? ND_NA_FLAG_SOLICITED : 0));
    c->push(srcIP.bytes(), IPAddressV6::byteCount());
  };

  ICMPHdr icmp6(
      static_cast<uint8_t>(ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_ADVERTISEMENT),
      0,
      0);
  icmp6.serializeFullPacket(&cursor, dstMac, srcMac, vlan, ipv6, plen, bodyFn);

  // Send the packet to the switch
  PktUtil::padToLength(buf.get(), totalLen);
  handle->rxPacket(std::move(buf), port, vlan);
}

} // unnamed namespace

class NdpTest : public ::testing::Test {
 public:
  unique_ptr<HwTestHandle> setupTestHandle(
      seconds raInterval = seconds(0),
      seconds ndpInterval = seconds(0),
      std::optional<std::string> routerAddress = std::nullopt,
      int numIntfs = 2) {
    auto config = createSwitchConfig(
        raInterval, ndpInterval, false, routerAddress, numIntfs);

    *config.maxNeighborProbes() = 1;
    *config.staleEntryInterval() = 1;
    auto handle = createTestHandle(&config);
    sw_ = handle->getSw();
    sw_->initialConfigApplied(std::chrono::steady_clock::now());
    return handle;
  }

  unique_ptr<HwTestHandle> setupTestHandleWithPortRif(
      seconds raInterval = seconds(0),
      seconds ndpInterval = seconds(0),
      std::optional<std::string> routerAddress = std::nullopt,
      int numIntfs = 2) {
    auto config = createSwitchConfigPortRif(
        raInterval, ndpInterval, false, routerAddress, numIntfs);

    *config.maxNeighborProbes() = 1;
    *config.staleEntryInterval() = 1;
    auto handle = createTestHandle(&config);
    sw_ = handle->getSw();
    sw_->initialConfigApplied(std::chrono::steady_clock::now());
    return handle;
  }
  unique_ptr<HwTestHandle> setupTestHandleWithNdpTimeout(
      seconds ndpTimeout,
      int numIntfs = 2) {
    return setupTestHandle(seconds(0), ndpTimeout, std::nullopt, numIntfs);
  }
  void addRoutes() {
    RouteNextHopSet nexthops;
    // resolved by intf 1
    nexthops.emplace(UnresolvedNextHop(
        IPAddress("2401:db00:2110:3004::1"), UCMP_DEFAULT_WEIGHT));
    // resolved by intf 1
    nexthops.emplace(UnresolvedNextHop(
        IPAddress("2401:db00:2110:3004::2"), UCMP_DEFAULT_WEIGHT));
    // un-resolvable
    nexthops.emplace(UnresolvedNextHop(
        IPAddress("5555:db00:2110:3004::1"), UCMP_DEFAULT_WEIGHT));

    auto updater = sw_->getRouteUpdater();
    updater.addRoute(
        RouterID(0),
        IPAddressV6("1111:1111:1:1::1"),
        64,
        ClientID(1001),
        RouteNextHopEntry(nexthops, AdminDistance::MAX_ADMIN_DISTANCE));

    updater.program();
  }

 protected:
  void validateRouterAdv(std::optional<std::string> configuredRouterIp);
  void validateRouterAdvForPortRif(
      std::optional<std::string> configuredRouterIp);
  void validateRouterAdvForAggregatePortRif();
  SwSwitch* sw_;
};

TEST_F(NdpTest, UnsolicitedRequest) {
  auto handle = this->setupTestHandle();
  auto sw = handle->getSw();

  // Create an neighbor solicitation request
  auto pkt = PktUtil::parseHexData(
      // dst mac, src mac
      "33 33 ff 00 00 0a  02 05 73 f9 46 fc"
      // 802.1q, VLAN 5
      "81 00 00 05"
      // IPv6
      "86 dd"
      // Version 6, traffic class, flow label
      "6e 00 00 00"
      // Payload length: 24
      "00 18"
      // Next Header: 58 (ICMPv6), Hop Limit (255)
      "3a ff"
      // src addr (::0)
      "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
      // dst addr (2401:db00:2110:3004::a)
      "24 01 db 00 21 10 30 04 00 00 00 00 00 00 00 0a"
      // type: neighbor solicitation
      "87"
      // code
      "00"
      // checksum
      "d8 6c"
      // reserved
      "00 00 00 00"
      // target address (2401:db00:2110:3004::a)
      "24 01 db 00 21 10 30 04 00 00 00 00 00 00 00 0a");

  // Cache the current stats
  CounterCache counters(sw);

  // We should get a neighbor advertisement back
  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "neighbor advertisement",
      checkNeighborAdvert(
          MockPlatform::getMockLocalMac(),
          IPAddressV6("2401:db00:2110:3004::a"),
          MacAddress("02:05:73:f9:46:fc"),
          IPAddressV6("ff01::1"),
          VlanID(5),
          0xa0),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  // Send the packet to the SwSwitch
  handle->rxPacket(
      make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), VlanID(5));

  // Check the new stats
  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.ndp.sum", 1);
}

TEST_F(NdpTest, UnsolicitedRequestPortRif) {
  auto handle = this->setupTestHandleWithPortRif();
  auto sw = handle->getSw();

  // Create an neighbor solicitation request
  auto pkt = PktUtil::parseHexData(
      // dst mac, src mac
      "33 33 ff 00 00 0a  02 05 73 f9 46 fc"
      // IPv6
      "86 dd"
      // Version 6, traffic class, flow label
      "6e 00 00 00"
      // Payload length: 24
      "00 18"
      // Next Header: 58 (ICMPv6), Hop Limit (255)
      "3a ff"
      // src addr (::0)
      "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
      // dst addr (2401:db00:2110:3004::a)
      "24 01 db 00 21 10 30 04 00 00 00 00 00 00 00 0a"
      // type: neighbor solicitation
      "87"
      // code
      "00"
      // checksum
      "d8 6c"
      // reserved
      "00 00 00 00"
      // target address (2401:db00:2110:3004::a)
      "24 01 db 00 21 10 30 04 00 00 00 00 00 00 00 0a");

  // Cache the current stats
  CounterCache counters(sw);

  // We should get a neighbor advertisement back
  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "neighbor advertisement",
      checkNeighborAdvert(
          MacAddress("00:02:00:00:00:55"),
          IPAddressV6("2401:db00:2110:3004::a"),
          MacAddress("02:05:73:f9:46:fc"),
          IPAddressV6("ff01::1"),
          VlanID(1),
          0xa0),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  // Send the packet to the SwSwitch
  handle->rxPacket(
      make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), std::nullopt);

  // Check the new stats
  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.ndp.sum", 1);
}

TEST_F(NdpTest, NeighborSoliciationNotMine) {
  auto handle = this->setupTestHandle();
  auto sw = handle->getSw();

  // Create an neighbor solicitation request
  auto pkt = PktUtil::parseHexData(
      // dst mac, src mac
      "33 33 ff 00 00 0a  02 05 73 f9 46 fc"
      // 802.1q, VLAN 5
      "81 00 00 05"
      // IPv6
      "86 dd"
      // Version 6, traffic class, flow label
      "6e 00 00 00"
      // Payload length: 32
      "00 20"
      // Next Header: 58 (ICMPv6), Hop Limit (255)
      "3a ff"
      // src addr (::0)
      "34 01 00 00 00 00 00 00 00 00 00 00 00 00 00 0b"
      // dst addr (3401:db00:2110:3004::a)
      "34 01 db 00 21 10 30 04 00 00 00 00 00 00 00 0a"
      // type: neighbor solicitation
      "87"
      // code
      "00"
      // checksum
      "C6 5C"
      // reserved
      "00 00 00 00"
      // target address (3401:db00:2110:3004::a)
      "34 01 db 00 21 10 30 04 00 00 00 00 00 00 00 0a"
      // Src link layer (mac) option
      "01"
      // Option len
      "01"
      // Src link layer address (mac)
      "02 05 73 f9 46 fc");

  // Cache the current stats
  CounterCache counters(sw);

  // Send the packet to the SwSwitch
  handle->rxPacket(
      make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), VlanID(5));

  // Check the new stats
  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.drops.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.ndp.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "ipv6.ndp.not_mine.sum", 1);
}

TEST_F(NdpTest, NeighborSoliciationNotMinePortRif) {
  auto handle = this->setupTestHandleWithPortRif();
  auto sw = handle->getSw();

  // Create an neighbor solicitation request
  auto pkt = PktUtil::parseHexData(
      // dst mac, src mac
      "33 33 ff 00 00 0a  02 05 73 f9 46 fc"
      // IPv6
      "86 dd"
      // Version 6, traffic class, flow label
      "6e 00 00 00"
      // Payload length: 32
      "00 20"
      // Next Header: 58 (ICMPv6), Hop Limit (255)
      "3a ff"
      // src addr (::0)
      "34 01 00 00 00 00 00 00 00 00 00 00 00 00 00 0b"
      // dst addr (3401:db00:2110:3004::a)
      "34 01 db 00 21 10 30 04 00 00 00 00 00 00 00 0a"
      // type: neighbor solicitation
      "87"
      // code
      "00"
      // checksum
      "C6 5C"
      // reserved
      "00 00 00 00"
      // target address (3401:db00:2110:3004::a)
      "34 01 db 00 21 10 30 04 00 00 00 00 00 00 00 0a"
      // Src link layer (mac) option
      "01"
      // Option len
      "01"
      // Src link layer address (mac)
      "02 05 73 f9 46 fc");

  // Cache the current stats
  CounterCache counters(sw);

  // Send the packet to the SwSwitch
  handle->rxPacket(
      make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), std::nullopt);

  // Check the new stats
  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.drops.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.ndp.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "ipv6.ndp.not_mine.sum", 1);
}

TEST_F(NdpTest, TriggerSolicitation) {
  auto handle = this->setupTestHandleWithPortRif();
  auto sw = handle->getSw();
  auto intfID =
      sw->getState()->getInterfaceIDForPort(PortDescriptor(PortID(1)));
  this->addRoutes();

  // Create a packet to a node in the attached IPv6 subnet
  auto pkt = PktUtil::parseHexData(
      // dst mac, src mac
      "00 02 00 00 00 55  02 05 73 f9 46 fc"
      // IPv6
      "86 dd"
      // Version 6, traffic class, flow label
      "6e 00 00 00"
      // Payload length: 8
      "00 08"
      // Next Header: 17 (UDP), Hop Limit (255)
      "11 ff"
      // src addr (2401:db00:2110:1234::1:0)
      "24 01 db 00 21 10 12 34 00 00 00 00 00 01 00 00"
      // dst addr (2401:db00:2110:3004::1:0)
      "24 01 db 00 21 10 30 04 00 00 00 00 00 01 00 00"
      // source port (53 - DNS)
      "00 35"
      // destination port (53 - DNS)
      "00 35"
      // length
      "00 00"
      // checksum (not valid)
      "2a 7e");

  // Cache the current stats
  CounterCache counters(sw);

  // We should get a neighbor solicitation back
  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "neighbor solicitation",
      checkNeighborSolicitation(
          MacAddress("00:02:00:00:00:55"),
          MockPlatform::getLinkLocalIp6(MacAddress("00:02:00:00:00:55")),
          MacAddress("33:33:ff:01:00:00"),
          IPAddressV6("ff02::1:ff01:0"),
          IPAddressV6("2401:db00:2110:3004::1:0"),
          VlanID(1)),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  // Send the packet to the SwSwitch
  handle->rxPacket(
      make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), std::nullopt);
  waitForStateUpdates(sw);

  // Check the new stats
  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);

  // Resolve the pending entry by injecting a Neighbor Advertisement
  WaitForNdpEntryReachable neighborEntryReachable(
      sw, IPAddressV6("2401:db00:2110:3004::1:0"), intfID);
  sendNeighborAdvertisement(
      handle.get(),
      "2401:db00:2110:3004::1:0",
      "02:10:20:30:40:22",
      PortDescriptor(PortID(1)),
      1);
  EXPECT_TRUE(neighborEntryReachable.wait());

  // Reset counter baseline after NA injection (sendNeighborAdvertisement
  // internally calls rxPacket which increments the trapped counter)
  counters.update();

  // Create a packet to a node not in attached subnet, but in route table
  pkt = PktUtil::parseHexData(
      // dst mac, src mac
      "00 02 00 00 00 55  02 05 73 f9 46 fc"
      // IPv6
      "86 dd"
      // Version 6, traffic class, flow label
      "6e 00 00 00"
      // Payload length: 8
      "00 08"
      // Next Header: 17 (UDP), Hop Limit (255)
      "11 ff"
      // src addr (2401:db00:2110:1234::1:0)
      "24 01 db 00 21 10 12 34 00 00 00 00 00 01 00 00"
      // dst addr (1111:1111:1:1:2:3:4:5)
      "11 11 11 11 00 01 00 01 00 02 00 03 00 04 00 05"
      // source port (53 - DNS)
      "00 35"
      // destination port (53 - DNS)
      "00 35"
      // length
      "00 00"
      // checksum (not valid)
      "2a 7e");

  // We should get a neighbor solicitation back
  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "neighbor solicitation",
      checkNeighborSolicitation(
          MacAddress("00:02:00:00:00:55"),
          MockPlatform::getLinkLocalIp6(MacAddress("00:02:00:00:00:55")),
          MacAddress("33:33:ff:00:00:01"),
          IPAddressV6("ff02::1:ff00:1"),
          IPAddressV6("2401:db00:2110:3004::1"),
          VlanID(1)),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "neighbor solicitation",
      checkNeighborSolicitation(
          MacAddress("00:02:00:00:00:55"),
          MockPlatform::getLinkLocalIp6(MacAddress("00:02:00:00:00:55")),
          MacAddress("33:33:ff:00:00:02"),
          IPAddressV6("ff02::1:ff00:2"),
          IPAddressV6("2401:db00:2110:3004::2"),
          VlanID(1)),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  /* expect neighbor solicitations to nexthops */
  // Send the packet to the SwSwitch
  handle->rxPacket(
      make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), std::nullopt);
  waitForStateUpdates(sw);

  // Check the new stats
  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);

  // Resolve nexthop entries by injecting Neighbor Advertisements
  WaitForNdpEntryReachable nextHop1Reachable(
      sw, IPAddressV6("2401:db00:2110:3004::1"), intfID);
  WaitForNdpEntryReachable nextHop2Reachable(
      sw, IPAddressV6("2401:db00:2110:3004::2"), intfID);
  sendNeighborAdvertisement(
      handle.get(),
      "2401:db00:2110:3004::1",
      "02:10:20:30:40:23",
      PortDescriptor(PortID(1)),
      1);
  sendNeighborAdvertisement(
      handle.get(),
      "2401:db00:2110:3004::2",
      "02:10:20:30:40:24",
      PortDescriptor(PortID(1)),
      1);
  EXPECT_TRUE(nextHop1Reachable.wait());
  EXPECT_TRUE(nextHop2Reachable.wait());

  // Wait for entries to expire so we don't trigger updates
  // while the test is exiting
  WaitForNdpEntryExpiration neighborEntryExpire(
      sw, IPAddressV6("2401:db00:2110:3004::1:0"), intfID);
  WaitForNdpEntryExpiration nextHop1Expire(
      sw, IPAddressV6("2401:db00:2110:3004::1"), intfID);
  WaitForNdpEntryExpiration nextHop2Expire(
      sw, IPAddressV6("2401:db00:2110:3004::2"), intfID);
}

void NdpTest::validateRouterAdv(std::optional<std::string> configuredRouterIp) {
  seconds raInterval(1);
  auto config =
      createSwitchConfig(raInterval, seconds(0), false, configuredRouterIp);
  auto routerAdvSrcIp = configuredRouterIp
      ? folly::IPAddressV6(*configuredRouterIp)
      : MockPlatform::getMockLinkLocalIp6();
  // Add an interface with a /128 mask, to make sure it isn't included
  // in the generated RA packets.
  config.interfaces()[0].ipAddresses()->emplace_back(
      "2401:db00:2000:1234:1::/128");
  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();
  sw->initialConfigApplied(std::chrono::steady_clock::now());

  auto state = sw->getState();
  auto intfConfig = state->getInterfaces()->getNode(InterfaceID(5));
  PrefixVector expectedPrefixes{
      {IPAddressV6("2401:db00:2110:3004::"), 64},
      {IPAddressV6("fe80::"), 64},
  };
  CounterCache counters(sw);

  // Send the router solicitation packet
  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "router advertisement",
      checkRouterAdvert(
          MockPlatform::getMockLocalMac(),
          routerAdvSrcIp,
          MacAddress("02:05:73:f9:46:fc"),
          IPAddressV6("2401:db00:2110:1234::1:0"),
          VlanID(5),
          intfConfig->getNdpConfig()->toThrift(),
          9000,
          expectedPrefixes),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  auto pkt = PktUtil::parseHexData(
      // dst mac, src mac
      "33 33 00 00 00 02  02 05 73 f9 46 fc"
      // 802.1q, VLAN 5
      "81 00 00 05"
      // IPv6
      "86 dd"
      // Version 6, traffic class, flow label
      "6e 00 00 00"
      // Payload length: 8
      "00 08"
      // Next Header: 58 (ICMPv6), Hop Limit (255)
      "3a ff"
      // src addr (2401:db00:2110:1234::1:0)
      "24 01 db 00 21 10 12 34 00 00 00 00 00 01 00 00"
      // dst addr (ff02::2)
      "ff 02 00 00 00 00 00 00 00 00 00 00 00 00 00 02"
      // type: router solicitation
      "85"
      // code
      "00"
      // checksum
      "49 71"
      // reserved
      "00 00 00 00");
  handle->rxPacket(
      make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), VlanID(5));

  // Now send the packet with specified source MAC address as ICMPv6 option
  // which differs from MAC address in ethernet header. The switch should use
  // the MAC address in options field.

  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "router advertisement",
      checkRouterAdvert(
          MockPlatform::getMockLocalMac(),
          routerAdvSrcIp,
          MacAddress("02:ab:73:f9:46:fc"),
          IPAddressV6("2401:db00:2110:1234::1:0"),
          VlanID(5),
          intfConfig->getNdpConfig()->toThrift(),
          9000,
          expectedPrefixes),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  auto pkt2 = PktUtil::parseHexData(
      // dst mac, src mac
      "33 33 00 00 00 02  02 05 73 f9 46 fc"
      // 802.1q, VLAN 5
      "81 00 00 05"
      // IPv6
      "86 dd"
      // Version 6, traffic class, flow label
      "6e 00 00 00"
      // Payload length: 16
      "00 10"
      // Next Header: 58 (ICMPv6), Hop Limit (255)
      "3a ff"
      // src addr (2401:db00:2110:1234::1:0)
      "24 01 db 00 21 10 12 34 00 00 00 00 00 01 00 00"
      // dst addr (ff02::2)
      "ff 02 00 00 00 00 00 00 00 00 00 00 00 00 00 02"
      // type: router solicitation
      "85"
      // code
      "00"
      // checksum
      "8a c7"
      // reserved
      "00 00 00 00"
      // option type: Source Link-Layer Address, length
      "01 01"
      // source mac
      "02 ab 73 f9 46 fc");
  handle->rxPacket(
      make_unique<IOBuf>(pkt2), PortDescriptor(PortID(1)), VlanID(5));

  // The RA packet will be sent in the background even thread after the RA
  // interval.  Schedule a timeout to wake us up after the interval has
  // expired.  Using the background EventBase to run the timeout ensures that
  // it will always run after the RA timeout has fired.
  // We also send RA packets just before switch controller shutdown,
  // so expect RA packet.

  // Multicast router advertisement use switched api
  EXPECT_MANY_SWITCHED_PKTS(
      sw,
      "router advertisement",
      checkRouterAdvert(
          MockPlatform::getMockLocalMac(),
          routerAdvSrcIp,
          MacAddress("33:33:00:00:00:01"),
          IPAddressV6("ff02::1"),
          VlanID(5),
          intfConfig->getNdpConfig()->toThrift(),
          9000,
          expectedPrefixes));
  std::promise<bool> done;
  auto* evb = sw->getBackgroundEvb();
  evb->runInFbossEventBaseThread([&]() {
    evb->tryRunAfterDelay([&]() { done.set_value(true); }, 1010 /*ms*/);
  });
  done.get_future().wait();
  counters.update();
  EXPECT_GT(counters.value("PrimaryInterface.router_advertisements.sum"), 0);
}

void NdpTest::validateRouterAdvForPortRif(
    std::optional<std::string> configuredRouterIp) {
  seconds raInterval(1);
  auto config = testConfigAWithPortInterfaces();
  config.interfaces()[0].ndp() = cfg::NdpConfig();
  *config.interfaces()[0].ndp()->routerAdvertisementSeconds() =
      raInterval.count();
  if (configuredRouterIp) {
    config.interfaces()[0].ipAddresses()->emplace_back(*configuredRouterIp);
    config.interfaces()[0].ndp()->routerAddress() = *configuredRouterIp;
  }
  seconds ndpTimeout(1);
  if (ndpTimeout.count() > 0) {
    *config.arpTimeoutSeconds() = ndpTimeout.count();
  }
  // createSwitchConfig(raInterval, seconds(0), false, configuredRouterIp);
  auto routerAdvSrcIp = configuredRouterIp
      ? folly::IPAddressV6(*configuredRouterIp)
      : MockPlatform::getLinkLocalIp6(folly::MacAddress("00:02:00:00:00:55"));
  // Add an interface with a /128 mask, to make sure it isn't included
  // in the generated RA packets.
  config.interfaces()[0].ipAddresses()->emplace_back(
      "2401:db00:2000:1234:1::/128");
  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();
  sw->initialConfigApplied(std::chrono::steady_clock::now());

  auto state = sw->getState();
  auto intfConfig = state->getInterfaces()->getNode(
      InterfaceID(config.interfaces()[0].intfID().value()));
  PrefixVector expectedPrefixes{
      {IPAddressV6("2601:db00:2110:3001::"), 64},
      {IPAddressV6("fe80::"), 64},
  };
  CounterCache counters(sw);

  // Send the router solicitation packet
  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "router advertisement",
      checkRouterAdvert(
          folly::MacAddress("00:02:00:00:00:55"),
          routerAdvSrcIp,
          MacAddress("02:05:73:f9:46:fc"),
          IPAddressV6("2401:db00:2110:1234::1:0"),
          VlanID(1),
          intfConfig->getNdpConfig()->toThrift(),
          9000,
          expectedPrefixes),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  auto pkt = PktUtil::parseHexData(
      // dst mac, src mac
      "33 33 00 00 00 02  02 05 73 f9 46 fc"
      // IPv6
      "86 dd"
      // Version 6, traffic class, flow label
      "6e 00 00 00"
      // Payload length: 8
      "00 08"
      // Next Header: 58 (ICMPv6), Hop Limit (255)
      "3a ff"
      // src addr (2401:db00:2110:1234::1:0)
      "24 01 db 00 21 10 12 34 00 00 00 00 00 01 00 00"
      // dst addr (ff02::2)
      "ff 02 00 00 00 00 00 00 00 00 00 00 00 00 00 02"
      // type: router solicitation
      "85"
      // code
      "00"
      // checksum
      "49 71"
      // reserved
      "00 00 00 00");
  handle->rxPacket(
      make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), std::nullopt);

  // Now send the packet with specified source MAC address as ICMPv6 option
  // which differs from MAC address in ethernet header. The switch should use
  // the MAC address in options field.

  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "router advertisement",
      checkRouterAdvert(
          folly::MacAddress("00:02:00:00:00:55"),
          routerAdvSrcIp,
          MacAddress("02:ab:73:f9:46:fc"),
          IPAddressV6("2401:db00:2110:1234::1:0"),
          VlanID(1),
          intfConfig->getNdpConfig()->toThrift(),
          9000,
          expectedPrefixes),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  auto pkt2 = PktUtil::parseHexData(
      // dst mac, src mac
      "33 33 00 00 00 02  02 05 73 f9 46 fc"
      // IPv6
      "86 dd"
      // Version 6, traffic class, flow label
      "6e 00 00 00"
      // Payload length: 16
      "00 10"
      // Next Header: 58 (ICMPv6), Hop Limit (255)
      "3a ff"
      // src addr (2401:db00:2110:1234::1:0)
      "24 01 db 00 21 10 12 34 00 00 00 00 00 01 00 00"
      // dst addr (ff02::2)
      "ff 02 00 00 00 00 00 00 00 00 00 00 00 00 00 02"
      // type: router solicitation
      "85"
      // code
      "00"
      // checksum
      "8a c7"
      // reserved
      "00 00 00 00"
      // option type: Source Link-Layer Address, length
      "01 01"
      // source mac
      "02 ab 73 f9 46 fc");
  handle->rxPacket(
      make_unique<IOBuf>(pkt2), PortDescriptor(PortID(1)), std::nullopt);

  // The RA packet will be sent in the background even thread after the RA
  // interval.  Schedule a timeout to wake us up after the interval has
  // expired.  Using the background EventBase to run the timeout ensures that
  // it will always run after the RA timeout has fired.
  // We also send RA packets just before switch controller shutdown,
  // so expect RA packet.

  // Multicast router advertisement use switched api
  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "router advertisement",
      checkRouterAdvert(
          folly::MacAddress("00:02:00:00:00:55"),
          routerAdvSrcIp,
          MacAddress("33:33:00:00:00:01"),
          IPAddressV6("ff02::1"),
          VlanID(1),
          intfConfig->getNdpConfig()->toThrift(),
          9000,
          expectedPrefixes),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue))
      .Times(::testing::AtLeast(1));

  std::promise<bool> done;
  auto* evb = sw->getBackgroundEvb();
  evb->runInFbossEventBaseThread([&]() {
    evb->tryRunAfterDelay([&]() { done.set_value(true); }, 1010 /*ms*/);
  });
  done.get_future().wait();
  counters.update();
  EXPECT_GT(counters.value("fboss6001.router_advertisements.sum"), 0);
}

void NdpTest::validateRouterAdvForAggregatePortRif() {
  seconds raInterval(1);
  auto config = testConfigAWithAggregatePortInterface();

  // The aggregate port interface is the one the helper appends last.
  auto& aggIntf = config.interfaces()->back();
  ASSERT_EQ(kAggregatePortInterfaceID, *aggIntf.intfID());
  aggIntf.ndp() = cfg::NdpConfig();
  *aggIntf.ndp()->routerAdvertisementSeconds() = raInterval.count();

  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();
  sw->initialConfigApplied(std::chrono::steady_clock::now());

  CounterCache counters(sw);

  // Wait for the advertiser to fire. An interface bound to an aggregate port
  // has no physical port of its own, which used to be fatal on this path.
  std::promise<bool> done;
  auto* evb = sw->getBackgroundEvb();
  evb->runInFbossEventBaseThread([&]() {
    evb->tryRunAfterDelay([&]() { done.set_value(true); }, 1010 /*ms*/);
  });
  done.get_future().wait();
  counters.update();
  EXPECT_GT(counters.value("fbossAgg.router_advertisements.sum"), 0);
}

TEST_F(NdpTest, RouterAdvertisementAggregatePortRif) {
  this->validateRouterAdvForAggregatePortRif();
}

TEST_F(NdpTest, FloodNeighborAdvertisementsAggregatePortRif) {
  auto config = testConfigAWithAggregatePortInterface();
  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();
  sw->initialConfigApplied(std::chrono::steady_clock::now());

  // Flooding walks every interface and every address on it, so the egress port
  // is not worth pinning down here. What matters is that an interface bound to
  // an aggregate port, which has no physical port of its own, is advertised
  // over the aggregate rather than aborting the flood.
  EXPECT_HW_CALL(sw, sendPacketOutOfPortAsync_(_, _, _))
      .Times(::testing::AnyNumber());
  EXPECT_HW_CALL(sw, sendPacketSwitchedAsync_(_)).Times(::testing::AnyNumber());

  sw->getIPv6Handler()->floodNeighborAdvertisements();
}

TEST_F(NdpTest, RouterAdvertisement) {
  this->validateRouterAdv(std::nullopt);
}

TEST_F(NdpTest, RouterAdvertisementPortRif) {
  this->validateRouterAdvForPortRif(std::nullopt);
}

TEST_F(NdpTest, BrokenRouterAdvConfig) {
  seconds raInterval(1);
  auto config = createSwitchConfig(raInterval, seconds(0), false, "2::2");
  EXPECT_THROW(createTestHandle(&config), FbossError);
}

TEST_F(NdpTest, RouterAdvConfigWithRouterAddress) {
  this->validateRouterAdv("fe80::face:b00c");
}

TEST_F(NdpTest, RouterAdvConfigWithRouterAddressPortRif) {
  this->validateRouterAdvForPortRif("fe80::face:b00c");
}

TEST_F(NdpTest, RouterAdvertiserMoveAssignStopsReplacedAdvertiser) {
  // A long RA interval keeps periodic advertisements from firing, so the only
  // RAs counted are the final ones sent when an advertiser is stopped.
  auto config = createSwitchConfig(seconds(3600), seconds(0));
  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();
  sw->initialConfigApplied(std::chrono::steady_clock::now());
  EXPECT_MANY_SWITCHED_PKTS(sw, "router advertisement", [](const TxPacket*) {});

  auto state = sw->getState();
  auto intf = state->getInterfaces()->getNode(InterfaceID(5));
  const std::string kRaCounter = "PrimaryInterface.router_advertisements.sum";

  IPv6RouteAdvertiser replaced(sw, state.get(), intf.get());
  IPv6RouteAdvertiser replacement(sw, state.get(), intf.get());
  waitForBackgroundThread(sw);
  CounterCache counters(sw);

  // flat_map::erase() shifts entries with move assignment, so the advertiser
  // being overwritten must be stopped rather than left running.
  replaced = std::move(replacement);
  waitForBackgroundThread(sw);
  counters.update();
  counters.checkDelta(kRaCounter, 1);
}

TEST_F(
    NdpTest,
    RouterAdvertiserTimerDoesNotContinueAfterInterfaceNdpEntryChange) {
  constexpr auto kOriginalRaIntervalSeconds = 1;
  constexpr auto kReplacementRaIntervalSeconds = 3600;
  auto config = createSwitchConfig(
      seconds(kOriginalRaIntervalSeconds), seconds(0), false, std::nullopt, 3);
  config.interfaces()[2].ndp() = cfg::NdpConfig();
  *config.interfaces()[2].ndp()->routerAdvertisementSeconds() =
      kReplacementRaIntervalSeconds;

  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();
  sw->initialConfigApplied(std::chrono::steady_clock::now());
  EXPECT_MANY_SWITCHED_PKTS(sw, "router advertisement", [](const TxPacket*) {});
  waitForBackgroundThread(sw);

  const InterfaceID intfID(5);
  const IPAddressV6 neighborIP("2401:db00:2110:3004::b");
  // On production VOQ switches, local NDP entries are children of Interface.
  // Updating this table makes IPv6Handler::stateUpdated() erase and recreate
  // interface 5's advertiser. The later RA-enabled interface forces erase()
  // to shift a flat_map entry over the advertiser being torn down. Give the
  // replacement a long interval so any subsequent RA during this test can only
  // come from the leaked pre-update advertiser, which retains its 1s interval.
  sw->updateStateBlocking(
      "Add NDP neighbor under interface",
      [intfID, neighborIP, kReplacementRaIntervalSeconds](
          const std::shared_ptr<SwitchState> state) {
        auto newState = state->clone();
        auto ndpTable = std::make_shared<NdpTable>();
        ndpTable->addEntry(
            neighborIP,
            MacAddress("02:05:73:f9:46:fb"),
            PortDescriptor(PortID(1)),
            intfID);
        auto oldIntf = state->getInterfaces()->getNode(intfID);
        auto newIntf = oldIntf->modify(&newState);
        auto ndpConfig = oldIntf->getNdpConfig()->toThrift();
        *ndpConfig.routerAdvertisementSeconds() = kReplacementRaIntervalSeconds;
        newIntf->setNdpConfig(std::move(ndpConfig));
        newIntf->setNdpTable(std::move(ndpTable));
        return newState;
      });
  waitForBackgroundThread(sw);

  ASSERT_NE(
      sw->getState()
          ->getInterfaces()
          ->getNode(intfID)
          ->getNdpTable()
          ->getEntryIf(neighborIP),
      nullptr);

  // Take the baseline after the state update has completed. The replacement
  // and the later interface both have long intervals. With the move-assignment
  // fix, no RA should follow; without it, the leaked advertiser keeps firing.
  CounterCache counters(sw);
  std::this_thread::sleep_for(seconds(kOriginalRaIntervalSeconds + 1));
  waitForBackgroundThread(sw);
  counters.update();
  counters.checkDelta("PrimaryInterface.router_advertisements.sum", 0);
}

TEST_F(NdpTest, receiveNeighborAdvertisementUnsolicited) {
  auto handle = this->setupTestHandle();
  auto sw = handle->getSw();

  // Send two unsolicited neighbor advertisements, state should update at
  // least once
  auto intfID =
      sw->getState()->getInterfaceIDForPort(PortDescriptor(PortID(1)));
  WaitForNdpEntryCreation neighbor1Create(
      sw, IPAddressV6("2401:db00:2110:3004::b"), intfID, false);
  sendNeighborAdvertisement(
      handle.get(),
      "2401:db00:2110:3004::b",
      "02:05:73:f9:46:fb",
      PortDescriptor(PortID(1)),
      5,
      false);
  EXPECT_TRUE(neighbor1Create.wait());

  ThriftHandler thriftHandler(sw);
  auto binAddr = toBinaryAddress(IPAddressV6("2401:db00:2110:3004::b"));
  auto numFlushed =
      thriftHandler.flushNeighborEntry(make_unique<BinaryAddress>(binAddr), 5);
  EXPECT_EQ(numFlushed, 1);
}

TEST_F(NdpTest, FlushEntry) {
  auto handle = this->setupTestHandle();
  auto sw = handle->getSw();

  ThriftHandler thriftHandler(sw);

  // Send two unsolicited neighbor advertisements, state should update at
  // least once
  // Helper for checking entries in NDP table
  auto getNDPTableEntry = [&](IPAddressV6 ip, InterfaceID intf) {
    return sw->getState()
        ->getInterfaces()
        ->getNodeIf(intf)
        ->getNdpTable()
        ->getEntryIf(ip);
  };
  auto intfID =
      sw->getState()->getInterfaceIDForPort(PortDescriptor(PortID(1)));
  WaitForNdpEntryCreation neighbor1Create(
      sw, IPAddressV6("2401:db00:2110:3004::b"), intfID, false);
  WaitForNdpEntryCreation neighbor2Create(
      sw, IPAddressV6("2401:db00:2110:3004::c"), intfID, false);
  sendNeighborAdvertisement(
      handle.get(),
      "2401:db00:2110:3004::b",
      "02:05:73:f9:46:fb",
      PortDescriptor(PortID(1)),
      5);
  sendNeighborAdvertisement(
      handle.get(),
      "2401:db00:2110:3004::c",
      "02:05:73:f9:46:fc",
      PortDescriptor(PortID(1)),
      5);

  EXPECT_TRUE(neighbor1Create.wait());
  EXPECT_TRUE(neighbor2Create.wait());

  // Flush 2401:db00:2110:3004::b
  WaitForNdpEntryExpiration neighbor1Expire(
      sw, IPAddressV6("2401:db00:2110:3004::b"), intfID);
  auto binAddr = toBinaryAddress(IPAddressV6("2401:db00:2110:3004::b"));
  auto numFlushed =
      thriftHandler.flushNeighborEntry(make_unique<BinaryAddress>(binAddr), 5);
  EXPECT_EQ(numFlushed, 1);
  EXPECT_TRUE(neighbor1Expire.wait());

  // Still one entry
  // Only one entry now
  auto entry0 = getNDPTableEntry(IPAddressV6("2401:db00:2110:3004::b"), intfID);
  EXPECT_EQ(entry0, nullptr);

  auto entry1 = getNDPTableEntry(IPAddressV6("2401:db00:2110:3004::c"), intfID);
  EXPECT_NE(entry1, nullptr);
  EXPECT_EQ(entry1->getMac(), MacAddress("02:05:73:f9:46:fc"));

  // Now flush 2401:db00:2110:3004::c, but using special Vlan0
  WaitForNdpEntryExpiration neighbor2Expire(
      sw, IPAddressV6("2401:db00:2110:3004::c"), intfID);
  binAddr = toBinaryAddress(IPAddressV6("2401:db00:2110:3004::c"));
  // NDP removal should trigger a static MAC entry removal
  EXPECT_STATE_UPDATE_TIMES(sw, 1);
  numFlushed =
      thriftHandler.flushNeighborEntry(make_unique<BinaryAddress>(binAddr), 0);
  EXPECT_EQ(numFlushed, 1);
  EXPECT_TRUE(neighbor2Expire.wait());
  waitForStateUpdates(sw);

  // Try flushing 2401:db00:2110:3004::c again (should be a no-op)
  EXPECT_STATE_UPDATE_TIMES(sw, 0);
  binAddr = toBinaryAddress(IPAddressV6("2401:db00:2110:3004::c"));
  numFlushed =
      thriftHandler.flushNeighborEntry(make_unique<BinaryAddress>(binAddr), 5);
  EXPECT_EQ(numFlushed, 0);
}

// Ensure that NDP entries learned against a port are
// flushed when the port become part of Aggregate
TEST_F(NdpTest, FlushOnAggPortTransition) {
  auto handle = this->setupTestHandle();
  auto sw = handle->getSw();

  ThriftHandler thriftHandler(sw);
  // Helper for checking entries in NDP table
  auto getNDPTableEntry = [&](IPAddressV6 ip, InterfaceID intf) {
    return sw->getState()
        ->getInterfaces()
        ->getNodeIf(intf)
        ->getNdpTable()
        ->getEntryIf(ip);
  };
  auto neighborAddr = IPAddressV6("2401:db00:2110:3004::b");
  auto intfID =
      sw->getState()->getInterfaceIDForPort(PortDescriptor(PortID(1)));

  // Create NDP entry against a port
  WaitForNdpEntryCreation neighbor1Create(sw, neighborAddr, intfID, false);

  sendNeighborAdvertisement(
      handle.get(),
      neighborAddr.str(),
      "02:05:73:f9:46:fb",
      PortDescriptor(PortID(1)),
      5);

  EXPECT_TRUE(neighbor1Create.wait());
  auto entry = getNDPTableEntry(neighborAddr, intfID);
  EXPECT_NE(entry, nullptr);

  // Create an Aggregate port with a subport that has
  // NDP entries already present
  seconds raInterval = seconds(0);
  seconds ndpInterval = seconds(0);
  auto config = createSwitchConfig(raInterval, ndpInterval, true);
  sw->applyConfig("Add aggports", config);

  // Enable the subPorts
  AggregatePort::PartnerState pState{};
  for (int i = 0; i < kSubportCount; i++) {
    ProgramForwardingAndPartnerState addPort1ToAggregatePort(
        PortID(i + 1),
        kAggregatePortID,
        AggregatePort::Forwarding::ENABLED,
        pState);
    sw->updateStateNoCoalescing(
        "Adding member port to AggregatePort", addPort1ToAggregatePort);
  }

  WITH_RETRIES_N_TIMED(5, std::chrono::milliseconds(1000), {
    // Old entry should be flushed
    auto flushedEntry = getNDPTableEntry(neighborAddr, intfID);
    EXPECT_EVENTUALLY_EQ(flushedEntry, nullptr);
  });

  // Send neighbor advertisement on Aggregate
  WaitForNdpEntryCreation neighbor2Create(sw, neighborAddr, intfID, false);

  sendNeighborAdvertisement(
      handle.get(),
      neighborAddr.str(),
      "02:05:73:f9:46:fb",
      PortDescriptor(AggregatePortID(kAggregatePortID)),
      5,
      true);

  EXPECT_TRUE(neighbor2Create.wait());
  entry = getNDPTableEntry(neighborAddr, intfID);
  EXPECT_NE(entry, nullptr);
  EXPECT_EQ(
      entry->getPort(), PortDescriptor(AggregatePortID(kAggregatePortID)));
}

TEST_F(NdpTest, UpdateNeighborEntryToAggregatePort) {
  auto handle = this->setupTestHandle();
  auto sw = handle->getSw();
  const auto neighborAddr = IPAddressV6("2401:db00:2110:3004::b");
  const auto neighborMac = MacAddress("02:05:73:f9:46:fb");
  const auto intfID =
      sw->getState()->getInterfaceIDForPort(PortDescriptor(PortID(1)));

  // Configure the aggregate descriptor. The L3 traffic-loop setup seeds this
  // neighbor with a physical descriptor, but NDP learning can update it with
  // an aggregate descriptor.
  auto config = createSwitchConfig(seconds(0), seconds(0), true);
  sw->applyConfig("Add aggports", config);

  sw->getNeighborUpdater()->receivedNdpMineForIntf(
      intfID,
      neighborAddr,
      neighborMac,
      PortDescriptor(PortID(1)),
      ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_ADVERTISEMENT,
      0);
  sw->getNeighborUpdater()->waitForPendingUpdates();
  waitForStateUpdates(sw);
  auto entry = sw->getState()
                   ->getInterfaces()
                   ->getNodeIf(intfID)
                   ->getNdpTable()
                   ->getEntryIf(neighborAddr);
  ASSERT_NE(entry, nullptr);
  ASSERT_EQ(entry->getPort(), PortDescriptor(PortID(1)));

  sw->getNeighborUpdater()->receivedNdpMineForIntf(
      intfID,
      neighborAddr,
      neighborMac,
      PortDescriptor(AggregatePortID(kAggregatePortID)),
      ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_ADVERTISEMENT,
      ND_NA_FLAG_OVERRIDE);
  sw->getNeighborUpdater()->waitForPendingUpdates();
  waitForStateUpdates(sw);

  entry = sw->getState()
              ->getInterfaces()
              ->getNodeIf(intfID)
              ->getNdpTable()
              ->getEntryIf(neighborAddr);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(
      entry->getPort(), PortDescriptor(AggregatePortID(kAggregatePortID)));
}

TEST_F(NdpTest, PendingNdp) {
  auto handle = this->setupTestHandleWithPortRif();
  auto sw = handle->getSw();
  auto intfID =
      sw->getState()->getInterfaceIDForPort(PortDescriptor(PortID(1)));
  // Create a packet to a node in the attached IPv6 subnet
  auto pkt = PktUtil::parseHexData(
      // dst mac, src mac
      "00 02 00 00 00 55  02 05 73 f9 46 fc"
      // IPv6
      "86 dd"
      // Version 6, traffic class, flow label
      "6e 00 00 00"
      // Payload length: 8
      "00 08"
      // Next Header: 17 (UDP), Hop Limit (255)
      "11 ff"
      // src addr (2401:db00:2110:1234::1:0)
      "24 01 db 00 21 10 12 34 00 00 00 00 00 01 00 00"
      // dst addr (2401:db00:2110:3004::1:0)
      "24 01 db 00 21 10 30 04 00 00 00 00 00 01 00 00"
      // source port (53 - DNS)
      "00 35"
      // destination port (53 - DNS)
      "00 35"
      // length
      "00 00"
      // checksum (not valid)
      "2a 7e");

  // Cache the current stats
  CounterCache counters(sw);

  // We should get a neighbor solicitation back
  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "neighbor solicitation",
      checkNeighborSolicitation(
          MacAddress("00:02:00:00:00:55"),
          MockPlatform::getLinkLocalIp6(MacAddress("00:02:00:00:00:55")),
          MacAddress("33:33:ff:01:00:00"),
          IPAddressV6("ff02::1:ff01:0"),
          IPAddressV6("2401:db00:2110:3004::1:0"),
          VlanID(1)),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  // Send the packet to the SwSwitch
  handle->rxPacket(
      make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), std::nullopt);
  waitForStateUpdates(sw);

  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.ndp.sum", 0);

  // Resolve the pending entry by injecting a Neighbor Advertisement
  WaitForNdpEntryReachable neighborEntryReachable(
      sw, IPAddressV6("2401:db00:2110:3004::1:0"), intfID);
  sendNeighborAdvertisement(
      handle.get(),
      "2401:db00:2110:3004::1:0",
      "02:10:20:30:40:22",
      PortDescriptor(PortID(1)),
      1);

  // The entry should now be valid instead of pending
  EXPECT_TRUE(neighborEntryReachable.wait());
  waitForStateUpdates(sw);
  auto entry = sw->getState()
                   ->getInterfaces()
                   ->getNodeIf(intfID)
                   ->getNdpTable()
                   ->getEntryIf(IPAddressV6("2401:db00:2110:3004::1:0"));
  EXPECT_NE(entry, nullptr);
  EXPECT_EQ(entry->isPending(), false);

  // Verify that we don't ever overwrite a valid entry with a pending one.
  // Receive the same packet again, entry should still be valid
  EXPECT_STATE_UPDATE_TIMES(sw, 0);

  // Send the packet to the SwSwitch
  handle->rxPacket(
      make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), std::nullopt);
  waitForStateUpdates(sw);
  entry = sw->getState()
              ->getInterfaces()
              ->getNodeIf(intfID)
              ->getNdpTable()
              ->getEntryIf(IPAddressV6("2401:db00:2110:3004::1:0"));
  EXPECT_NE(entry, nullptr);
  EXPECT_EQ(entry->isPending(), false);
};

// Note: With interface-based neighbor tables, pending entries are not in
// SwitchState (they exist only in the internal neighbor cache). Therefore,
// this test creates pending entries but must resolve them to REACHABLE state
// before verifying cleanup, as state observers cannot observe pending entries.
TEST_F(NdpTest, PendingNdpCleanup) {
  seconds ndpTimeout(1);
  auto handle = this->setupTestHandleWithPortRif(seconds(0), ndpTimeout);
  auto sw = handle->getSw();

  this->addRoutes();

  auto intfID =
      sw->getState()->getInterfaceIDForPort(PortDescriptor(PortID(1)));

  // Create a packet to a node in the attached IPv6 subnet
  auto pkt = PktUtil::parseHexData(
      // dst mac, src mac
      "00 02 00 00 00 55  02 05 73 f9 46 fc"
      // IPv6
      "86 dd"
      // Version 6, traffic class, flow label
      "6e 00 00 00"
      // Payload length: 8
      "00 08"
      // Next Header: 17 (UDP), Hop Limit (255)
      "11 ff"
      // src addr (2401:db00:2110:1234::1:0)
      "24 01 db 00 21 10 12 34 00 00 00 00 00 01 00 00"
      // dst addr (2401:db00:2110:3004::1:0)
      "24 01 db 00 21 10 30 04 00 00 00 00 00 01 00 00"
      // source port (53 - DNS)
      "00 35"
      // destination port (53 - DNS)
      "00 35"
      // length
      "00 00"
      // checksum (not valid)
      "2a 7e");

  // Cache the current stats
  CounterCache counters(sw);

  // We should get a neighbor solicitation back
  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "neighbor solicitation",
      checkNeighborSolicitation(
          MacAddress("00:02:00:00:00:55"),
          MockPlatform::getLinkLocalIp6(MacAddress("00:02:00:00:00:55")),
          MacAddress("33:33:ff:01:00:00"),
          IPAddressV6("ff02::1:ff01:0"),
          IPAddressV6("2401:db00:2110:3004::1:0"),
          VlanID(1)),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  // Send the packet to the SwSwitch
  handle->rxPacket(
      make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), std::nullopt);
  waitForStateUpdates(sw);

  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.ndp.sum", 0);

  // ndpTimeout=1 lets this entry reach PROBE and send a unicast probe while
  // the rest of the test is still running, so expect it before resolving.
  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "neighbor solicitation",
      checkNeighborSolicitation(
          MacAddress("00:02:00:00:00:55"),
          IPAddressV6("2401:db00:2110:3004::"),
          MacAddress("02:10:20:30:40:22"),
          IPAddressV6("2401:db00:2110:3004::1:0"),
          IPAddressV6("2401:db00:2110:3004::1:0"),
          VlanID(1),
          false),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  // Resolve the pending entry by injecting a Neighbor Advertisement
  WaitForNdpEntryReachable neighborEntryReachable(
      sw, IPAddressV6("2401:db00:2110:3004::1:0"), intfID);
  sendNeighborAdvertisement(
      handle.get(),
      "2401:db00:2110:3004::1:0",
      "02:10:20:30:40:22",
      PortDescriptor(PortID(1)),
      1);
  EXPECT_TRUE(neighborEntryReachable.wait());

  // Reset counter baseline after NA injection
  counters.update();

  // Create a second packet to a node not in attached subnet, but in route table
  pkt = PktUtil::parseHexData(
      // dst mac, src mac
      "00 02 00 00 00 55  02 05 73 f9 46 fc"
      // IPv6
      "86 dd"
      // Version 6, traffic class, flow label
      "6e 00 00 00"
      // Payload length: 8
      "00 08"
      // Next Header: 17 (UDP), Hop Limit (255)
      "11 ff"
      // src addr (2401:db00:2110:1234::1:0)
      "24 01 db 00 21 10 12 34 00 00 00 00 00 01 00 00"
      // dst addr (1111:1111:1:1:2:3:4:5)
      "11 11 11 11 00 01 00 01 00 02 00 03 00 04 00 05"
      // source port (53 - DNS)
      "00 35"
      // destination port (53 - DNS)
      "00 35"
      // length
      "00 00"
      // checksum (not valid)
      "2a 7e");

  // We should send two more neighbor solicitations
  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "neighbor solicitation",
      checkNeighborSolicitation(
          MacAddress("00:02:00:00:00:55"),
          MockPlatform::getLinkLocalIp6(MacAddress("00:02:00:00:00:55")),
          MacAddress("33:33:ff:00:00:01"),
          IPAddressV6("ff02::1:ff00:1"),
          IPAddressV6("2401:db00:2110:3004::1"),
          VlanID(1)),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "neighbor solicitation",
      checkNeighborSolicitation(
          MacAddress("00:02:00:00:00:55"),
          MockPlatform::getLinkLocalIp6(MacAddress("00:02:00:00:00:55")),
          MacAddress("33:33:ff:00:00:02"),
          IPAddressV6("ff02::1:ff00:2"),
          IPAddressV6("2401:db00:2110:3004::2"),
          VlanID(1)),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  // Send the packet to the SwSwitch
  handle->rxPacket(
      make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), std::nullopt);
  waitForStateUpdates(sw);

  // Check the new stats
  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);

  // Set up unicast probe expectations BEFORE resolving entries, since with
  // ndpTimeout=1 entries can advance through REACHABLE -> STALE -> PROBE
  // before we get a chance to register expectations after NA injection.
  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "neighbor solicitation",
      checkNeighborSolicitation(
          MacAddress("00:02:00:00:00:55"),
          IPAddressV6("2401:db00:2110:3004::"),
          MacAddress("02:10:20:30:40:23"),
          IPAddressV6("2401:db00:2110:3004::1"),
          IPAddressV6("2401:db00:2110:3004::1"),
          VlanID(1),
          false),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "neighbor solicitation",
      checkNeighborSolicitation(
          MacAddress("00:02:00:00:00:55"),
          IPAddressV6("2401:db00:2110:3004::"),
          MacAddress("02:10:20:30:40:24"),
          IPAddressV6("2401:db00:2110:3004::2"),
          IPAddressV6("2401:db00:2110:3004::2"),
          VlanID(1),
          false),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  // Resolve nexthop entries by injecting Neighbor Advertisements
  WaitForNdpEntryReachable nexthop1Reachable(
      sw, IPAddressV6("2401:db00:2110:3004::1"), intfID);
  WaitForNdpEntryReachable nexthop2Reachable(
      sw, IPAddressV6("2401:db00:2110:3004::2"), intfID);
  sendNeighborAdvertisement(
      handle.get(),
      "2401:db00:2110:3004::1",
      "02:10:20:30:40:23",
      PortDescriptor(PortID(1)),
      1);
  sendNeighborAdvertisement(
      handle.get(),
      "2401:db00:2110:3004::2",
      "02:10:20:30:40:24",
      PortDescriptor(PortID(1)),
      1);
  EXPECT_TRUE(nexthop1Reachable.wait());
  EXPECT_TRUE(nexthop2Reachable.wait());

  // Wait for resolved entries to expire
  WaitForNdpEntryExpiration neighborExpire(
      sw, IPAddressV6("2401:db00:2110:3004::1:0"), intfID);
  WaitForNdpEntryExpiration nexthop1Expire(
      sw, IPAddressV6("2401:db00:2110:3004::1"), intfID);
  WaitForNdpEntryExpiration nexthop2Expire(
      sw, IPAddressV6("2401:db00:2110:3004::2"), intfID);

  std::promise<bool> done;
  auto* evb = sw->getBackgroundEvb();
  evb->runInFbossEventBaseThread(
      [&]() { evb->tryRunAfterDelay([&]() { done.set_value(true); }, 2550); });
  done.get_future().wait();

  EXPECT_TRUE(neighborExpire.wait());
  EXPECT_TRUE(nexthop1Expire.wait());
  EXPECT_TRUE(nexthop2Expire.wait());
  // Entries should be removed
  auto ndpTable =
      sw->getState()->getInterfaces()->getNodeIf(intfID)->getNdpTable();
  auto entry = ndpTable->getEntryIf(IPAddressV6("2401:db00:2110:3004::1:0"));
  auto entry2 = ndpTable->getEntryIf(IPAddressV6("2401:db00:2110:3004::1"));
  auto entry3 = ndpTable->getEntryIf(IPAddressV6("2401:db00:2110:3004::2"));
  EXPECT_EQ(entry, nullptr);
  EXPECT_EQ(entry2, nullptr);
  EXPECT_EQ(entry3, nullptr);
  EXPECT_NE(sw, nullptr);
};

TEST_F(NdpTest, NdpExpiration) {
  seconds ndpTimeout(1);
  auto handle = this->setupTestHandleWithPortRif(seconds(0), ndpTimeout);
  auto sw = handle->getSw();

  this->addRoutes();

  auto intfID =
      sw->getState()->getInterfaceIDForPort(PortDescriptor(PortID(1)));
  auto targetIP = IPAddressV6("2401:db00:2110:3004::1:0");
  auto targetIP2 = IPAddressV6("2401:db00:2110:3004::1");
  auto targetIP3 = IPAddressV6("2401:db00:2110:3004::2");

  // Create a packet to a node in the attached IPv6 subnet
  auto pkt = PktUtil::parseHexData(
      // dst mac, src mac
      "00 02 00 00 00 55  02 05 73 f9 46 fc"
      // IPv6
      "86 dd"
      // Version 6, traffic class, flow label
      "6e 00 00 00"
      // Payload length: 8
      "00 08"
      // Next Header: 17 (UDP), Hop Limit (255)
      "11 ff"
      // src addr (2401:db00:2110:1234::1:0)
      "24 01 db 00 21 10 12 34 00 00 00 00 00 01 00 00"
      // dst addr (2401:db00:2110:3004::1:0)
      "24 01 db 00 21 10 30 04 00 00 00 00 00 01 00 00"
      // source port (53 - DNS)
      "00 35"
      // destination port (53 - DNS)
      "00 35"
      // length
      "00 00"
      // checksum (not valid)
      "2a 7e");

  // Cache the current stats
  CounterCache counters(sw);

  // We should get a neighbor solicitation back
  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "neighbor solicitation",
      checkNeighborSolicitation(
          MacAddress("00:02:00:00:00:55"),
          MockPlatform::getLinkLocalIp6(MacAddress("00:02:00:00:00:55")),
          MacAddress("33:33:ff:01:00:00"),
          IPAddressV6("ff02::1:ff01:0"),
          targetIP,
          VlanID(1)),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  // Send the packet to the SwSwitch
  handle->rxPacket(
      make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), std::nullopt);
  waitForStateUpdates(sw);

  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.ndp.sum", 0);

  // Resolve the pending entry by injecting a Neighbor Advertisement
  WaitForNdpEntryReachable neighbor0Reachable(sw, targetIP, intfID);
  sendNeighborAdvertisement(
      handle.get(),
      targetIP.str(),
      "02:10:20:30:40:22",
      PortDescriptor(PortID(1)),
      1);
  EXPECT_TRUE(neighbor0Reachable.wait());

  // Reset counter baseline after NA injection
  counters.update();

  // Create a second packet to a node not in attached subnet, but in route table
  pkt = PktUtil::parseHexData(
      // dst mac, src mac
      "00 02 00 00 00 55  02 05 73 f9 46 fc"
      // IPv6
      "86 dd"
      // Version 6, traffic class, flow label
      "6e 00 00 00"
      // Payload length: 8
      "00 08"
      // Next Header: 17 (UDP), Hop Limit (255)
      "11 ff"
      // src addr (2401:db00:2110:1234::1:0)
      "24 01 db 00 21 10 12 34 00 00 00 00 00 01 00 00"
      // dst addr (1111:1111:1:1:2:3:4:5)
      "11 11 11 11 00 01 00 01 00 02 00 03 00 04 00 05"
      // source port (53 - DNS)
      "00 35"
      // destination port (53 - DNS)
      "00 35"
      // length
      "00 00"
      // checksum (not valid)
      "2a 7e");

  // We should send two more neighbor solicitations
  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "neighbor solicitation",
      checkNeighborSolicitation(
          MacAddress("00:02:00:00:00:55"),
          MockPlatform::getLinkLocalIp6(MacAddress("00:02:00:00:00:55")),
          MacAddress("33:33:ff:00:00:01"),
          IPAddressV6("ff02::1:ff00:1"),
          targetIP2,
          VlanID(1)),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "neighbor solicitation",
      checkNeighborSolicitation(
          MacAddress("00:02:00:00:00:55"),
          MockPlatform::getLinkLocalIp6(MacAddress("00:02:00:00:00:55")),
          MacAddress("33:33:ff:00:00:02"),
          IPAddressV6("ff02::1:ff00:2"),
          targetIP3,
          VlanID(1)),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  // Send the packet to the SwSwitch
  handle->rxPacket(
      make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), std::nullopt);
  waitForStateUpdates(sw);

  // Check the new stats
  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);

  // Set up unicast probe NS expectations BEFORE resolving entries.
  // With ndpTimeout=1s, entries quickly go REACHABLE -> STALE -> PROBE,
  // generating unicast NS probes. Expectations must be ready before NAs.
  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "neighbor solicitation",
      checkNeighborSolicitation(
          MacAddress("00:02:00:00:00:55"),
          IPAddressV6("2401:db00:2110:3004::"),
          MacAddress("02:10:20:30:40:22"),
          targetIP,
          targetIP,
          VlanID(1),
          false),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "neighbor solicitation",
      checkNeighborSolicitation(
          MacAddress("00:02:00:00:00:55"),
          IPAddressV6("2401:db00:2110:3004::"),
          MacAddress("02:10:20:30:40:23"),
          targetIP2,
          targetIP2,
          VlanID(1),
          false),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "neighbor solicitation",
      checkNeighborSolicitation(
          MacAddress("00:02:00:00:00:55"),
          IPAddressV6("2401:db00:2110:3004::"),
          MacAddress("02:10:20:30:40:24"),
          targetIP3,
          targetIP3,
          VlanID(1),
          false),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  // Resolve nexthop entries by injecting Neighbor Advertisements
  WaitForNdpEntryReachable neighbor1Reachable(sw, targetIP2, intfID);
  WaitForNdpEntryReachable neighbor2Reachable(sw, targetIP3, intfID);
  sendNeighborAdvertisement(
      handle.get(),
      targetIP2.str(),
      "02:10:20:30:40:23",
      PortDescriptor(PortID(1)),
      1);
  sendNeighborAdvertisement(
      handle.get(),
      targetIP3.str(),
      "02:10:20:30:40:24",
      PortDescriptor(PortID(1)),
      1);
  EXPECT_TRUE(neighbor1Reachable.wait());
  EXPECT_TRUE(neighbor2Reachable.wait());

  auto ndpTable =
      sw->getState()->getInterfaces()->getNodeIf(intfID)->getNdpTable();
  auto entry = ndpTable->getEntryIf(targetIP);
  auto entry2 = ndpTable->getEntryIf(targetIP2);
  auto entry3 = ndpTable->getEntryIf(targetIP3);
  EXPECT_NE(entry, nullptr);
  EXPECT_EQ(entry->isPending(), false);
  EXPECT_NE(entry2, nullptr);
  EXPECT_EQ(entry2->isPending(), false);
  EXPECT_NE(entry3, nullptr);
  EXPECT_EQ(entry3->isPending(), false);

  // Wait for the entries to expire.
  // We wait 2.5 seconds(plus change):
  // Up to 1.5 seconds for lifetime.
  // 1 more second for probe
  WaitForNdpEntryExpiration expire0(sw, targetIP, intfID);
  WaitForNdpEntryExpiration expire1(sw, targetIP2, intfID);
  WaitForNdpEntryExpiration expire2(sw, targetIP3, intfID);
  std::promise<bool> done;
  auto* evb = sw->getBackgroundEvb();
  evb->runInFbossEventBaseThread(
      [&]() { evb->tryRunAfterDelay([&]() { done.set_value(true); }, 2550); });
  done.get_future().wait();
  EXPECT_TRUE(expire0.wait());
  EXPECT_TRUE(expire1.wait());
  EXPECT_TRUE(expire2.wait());

  // The entries should be expired
  ndpTable = sw->getState()->getInterfaces()->getNodeIf(intfID)->getNdpTable();
  entry = ndpTable->getEntryIf(targetIP);
  entry2 = ndpTable->getEntryIf(targetIP2);
  entry3 = ndpTable->getEntryIf(targetIP3);
  EXPECT_EQ(entry, nullptr);
  EXPECT_EQ(entry2, nullptr);
  EXPECT_EQ(entry3, nullptr);
}

TEST_F(NdpTest, FlushEntryWithConcurrentUpdate) {
  auto handle = this->setupTestHandle();
  auto sw = handle->getSw();
  ThriftHandler thriftHandler(sw);

  PortID portID(1);
  // ensure port is up
  sw->linkStateChanged(portID, true, cfg::PortType::INTERFACE_PORT);

  VlanID vlanID(5);
  std::vector<IPAddressV6> targetIPs;
  for (uint32_t i = 1; i <= 255; i++) {
    targetIPs.emplace_back("2401:db00:2110:3004::" + std::to_string(i));
  }
  // populate ndp entries first before flush
  {
    auto intfID = sw->getState()->getInterfaceIDForPort(PortDescriptor(portID));
    std::array<std::unique_ptr<WaitForNdpEntryReachable>, 255> ndpReachables;
    std::transform(
        targetIPs.begin(),
        targetIPs.end(),
        ndpReachables.begin(),
        [&](const IPAddressV6& ip) {
          return make_unique<WaitForNdpEntryReachable>(sw, ip, intfID);
        });
    for (auto& ip : targetIPs) {
      sendNeighborAdvertisement(
          handle.get(),
          ip.str(),
          "02:05:73:f9:46:fb",
          PortDescriptor(PortID(portID)),
          vlanID,
          false);
      waitForStateUpdates(sw);
    }
    for (auto& ndpReachable : ndpReachables) {
      EXPECT_TRUE(ndpReachable->wait());
    }
  }

  std::atomic<bool> done{false};
  std::thread ndpReplies([&handle, &targetIPs, &portID, &vlanID, &done]() {
    int index = 0;
    while (!done) {
      sendNeighborAdvertisement(
          handle.get(),
          targetIPs[index].str(),
          "02:05:73:f9:46:fb",
          PortDescriptor(PortID(portID)),
          vlanID,
          false);
      index = (index + 1) % targetIPs.size();
      usleep(1000);
    }
  });

  // flush all ndp entries for 10 times
  for (uint32_t i = 0; i < 10; i++) {
    int numFlushed = 0;
    for (auto& ip : targetIPs) {
      numFlushed += thriftHandler.flushNeighborEntry(
          make_unique<BinaryAddress>(toBinaryAddress(ip)), vlanID);
    }
    XLOG(DBG) << "iter" << i << " flushed " << numFlushed << " entries";
  }
  // let TSAN/ASAN catch any racing issues
  done = true;
  ndpReplies.join();
}

TEST_F(NdpTest, PortFlapRecover) {
  auto handle = this->setupTestHandleWithPortRif(seconds(0), seconds(0));
  auto sw = handle->getSw();

  this->addRoutes();

  // ensure port is up
  sw->linkStateChanged(PortID(1), true, cfg::PortType::INTERFACE_PORT);

  auto intfID =
      sw->getState()->getInterfaceIDForPort(PortDescriptor(PortID(1)));

  auto targetIP = IPAddressV6("2401:db00:2110:3004::1:0");
  auto targetIP2 = IPAddressV6("2401:db00:2110:3004::1");
  auto targetIP3 = IPAddressV6("2401:db00:2110:3004::2");

  // Create a packet to a node in the attached IPv6 subnet
  auto pkt = PktUtil::parseHexData(
      // dst mac, src mac
      "00 02 00 00 00 55  02 05 73 f9 46 fc"
      // IPv6
      "86 dd"
      // Version 6, traffic class, flow label
      "6e 00 00 00"
      // Payload length: 8
      "00 08"
      // Next Header: 17 (UDP), Hop Limit (255)
      "11 ff"
      // src addr (2401:db00:2110:1234::1:0)
      "24 01 db 00 21 10 12 34 00 00 00 00 00 01 00 00"
      // dst addr (2401:db00:2110:3004::1:0)
      "24 01 db 00 21 10 30 04 00 00 00 00 00 01 00 00"
      // source port (53 - DNS)
      "00 35"
      // destination port (53 - DNS)
      "00 35"
      // length
      "00 00"
      // checksum (not valid)
      "2a 7e");

  // Cache the current stats
  CounterCache counters(sw);

  // We should get a neighbor solicitation back
  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "neighbor solicitation",
      checkNeighborSolicitation(
          MacAddress("00:02:00:00:00:55"),
          MockPlatform::getLinkLocalIp6(MacAddress("00:02:00:00:00:55")),
          MacAddress("33:33:ff:01:00:00"),
          IPAddressV6("ff02::1:ff01:0"),
          targetIP,
          VlanID(1)),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  // Send the packet to the SwSwitch
  handle->rxPacket(
      make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), std::nullopt);
  waitForStateUpdates(sw);

  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.ndp.sum", 0);

  // Resolve the pending entry by injecting a Neighbor Advertisement
  WaitForNdpEntryReachable neighbor0Reachable(sw, targetIP, intfID);
  sendNeighborAdvertisement(
      handle.get(),
      targetIP.str(),
      "02:10:20:30:40:22",
      PortDescriptor(PortID(1)),
      1);
  EXPECT_TRUE(neighbor0Reachable.wait());

  // Reset counter baseline after NA injection
  counters.update();

  // Create a second packet to a node not in attached subnet, but in route table
  pkt = PktUtil::parseHexData(
      // dst mac, src mac
      "00 02 00 00 00 55  02 05 73 f9 46 fc"
      // IPv6
      "86 dd"
      // Version 6, traffic class, flow label
      "6e 00 00 00"
      // Payload length: 8
      "00 08"
      // Next Header: 17 (UDP), Hop Limit (255)
      "11 ff"
      // src addr (2401:db00:2110:1234::1:0)
      "24 01 db 00 21 10 12 34 00 00 00 00 00 01 00 00"
      // dst addr (1111:1111:1:1:2:3:4:5)
      "11 11 11 11 00 01 00 01 00 02 00 03 00 04 00 05"
      // source port (53 - DNS)
      "00 35"
      // destination port (53 - DNS)
      "00 35"
      // length
      "00 00"
      // checksum (not valid)
      "2a 7e");

  // We should send two more neighbor solicitations
  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "neighbor solicitation",
      checkNeighborSolicitation(
          MacAddress("00:02:00:00:00:55"),
          MockPlatform::getLinkLocalIp6(MacAddress("00:02:00:00:00:55")),
          MacAddress("33:33:ff:00:00:01"),
          IPAddressV6("ff02::1:ff00:1"),
          targetIP2,
          VlanID(1)),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  EXPECT_OUT_OF_PORT_PKT(
      sw,
      "neighbor solicitation",
      checkNeighborSolicitation(
          MacAddress("00:02:00:00:00:55"),
          MockPlatform::getLinkLocalIp6(MacAddress("00:02:00:00:00:55")),
          MacAddress("33:33:ff:00:00:02"),
          IPAddressV6("ff02::1:ff00:2"),
          targetIP3,
          VlanID(1)),
      PortID(1),
      std::optional<uint8_t>(kNCStrictPriorityQueue));

  // Send the packet to the SwSwitch
  handle->rxPacket(
      make_unique<IOBuf>(pkt), PortDescriptor(PortID(1)), std::nullopt);
  waitForStateUpdates(sw);

  // Check the new stats
  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);

  // Resolve nexthop entries by injecting Neighbor Advertisements
  // In port-RIF mode, all target IPs are in PortID(1)'s subnet
  // (2401:db00:2110:3004::/64), so all NAs must be sent on PortID(1).
  WaitForNdpEntryReachable neighbor1Reachable(sw, targetIP2, intfID);
  WaitForNdpEntryReachable neighbor2Reachable(sw, targetIP3, intfID);
  sendNeighborAdvertisement(
      handle.get(),
      targetIP2.str(),
      "02:10:20:30:40:22",
      PortDescriptor(PortID(1)),
      1);
  sendNeighborAdvertisement(
      handle.get(),
      targetIP3.str(),
      "02:10:20:30:40:23",
      PortDescriptor(PortID(1)),
      1);
  EXPECT_TRUE(neighbor1Reachable.wait());
  EXPECT_TRUE(neighbor2Reachable.wait());

  // The entries should now be valid instead of pending
  auto ndpTable =
      sw->getState()->getInterfaces()->getNodeIf(intfID)->getNdpTable();
  auto entry = ndpTable->getEntryIf(targetIP);
  auto entry2 = ndpTable->getEntryIf(targetIP2);
  auto entry3 = ndpTable->getEntryIf(targetIP3);
  EXPECT_NE(entry, nullptr);
  EXPECT_EQ(entry->isPending(), false);
  EXPECT_NE(entry2, nullptr);
  EXPECT_EQ(entry2->isPending(), false);
  EXPECT_NE(entry3, nullptr);
  EXPECT_EQ(entry3->isPending(), false);

  // send a port down event to the switch for port 1
  // All three entries are on PortID(1), so all should go pending
  EXPECT_STATE_UPDATE_TIMES_ATLEAST(sw, 1);
  WaitForNdpEntryPending neigbor0Pending(sw, targetIP, intfID);
  WaitForNdpEntryPending neigbor1Pending(sw, targetIP2, intfID);
  WaitForNdpEntryPending neigbor2Pending(sw, targetIP3, intfID);

  // Update port oper state to DOWN and directly invoke portDown on the
  // NeighborUpdater. Calling portDown directly (instead of relying on
  // stateUpdated -> portChanged dispatch) ensures the neighbor cache
  // processes the port-down event deterministically.
  sw->linkStateChanged(PortID(1), false, cfg::PortType::INTERFACE_PORT);
  waitForStateUpdates(sw);
  sw->getNeighborUpdater()->portDown(PortDescriptor(PortID(1)));
  sw->getNeighborUpdater()->waitForPendingUpdates();
  waitForBackgroundThread(sw);
  waitForStateUpdates(sw);

  // All three entries should be pending now
  EXPECT_TRUE(neigbor0Pending.wait());
  EXPECT_TRUE(neigbor1Pending.wait());
  EXPECT_TRUE(neigbor2Pending.wait());

  ndpTable = sw->getState()->getInterfaces()->getNodeIf(intfID)->getNdpTable();
  entry = ndpTable->getEntryIf(targetIP);
  entry2 = ndpTable->getEntryIf(targetIP2);
  entry3 = ndpTable->getEntryIf(targetIP3);
  EXPECT_NE(entry, nullptr);
  EXPECT_EQ(entry->isPending(), true);
  EXPECT_EQ(entry->getPort(), PortDescriptor(PortID(0)));
  EXPECT_NE(entry2, nullptr);
  EXPECT_EQ(entry2->isPending(), true);
  EXPECT_EQ(entry2->getPort(), PortDescriptor(PortID(0)));
  EXPECT_NE(entry3, nullptr);
  EXPECT_EQ(entry3->isPending(), true);
  EXPECT_EQ(entry3->getPort(), PortDescriptor(PortID(0)));

  // send a port up event to the switch for port 1
  EXPECT_STATE_UPDATE_TIMES_ATLEAST(sw, 1);
  sw->linkStateChanged(PortID(1), true, cfg::PortType::INTERFACE_PORT);

  sendNeighborAdvertisement(
      handle.get(),
      targetIP.str(),
      "02:10:20:30:40:22",
      PortDescriptor(PortID(1)),
      1);
  sendNeighborAdvertisement(
      handle.get(),
      targetIP2.str(),
      "02:10:20:30:40:22",
      PortDescriptor(PortID(1)),
      1);
  sendNeighborAdvertisement(
      handle.get(),
      targetIP3.str(),
      "02:10:20:30:40:23",
      PortDescriptor(PortID(1)),
      1);

  EXPECT_TRUE(neighbor0Reachable.wait());
  EXPECT_TRUE(neighbor1Reachable.wait());
  EXPECT_TRUE(neighbor2Reachable.wait());

  // All entries should be valid again
  ndpTable = sw->getState()->getInterfaces()->getNodeIf(intfID)->getNdpTable();
  entry = ndpTable->getEntryIf(targetIP);
  entry2 = ndpTable->getEntryIf(targetIP2);
  entry3 = ndpTable->getEntryIf(targetIP3);
  EXPECT_NE(entry, nullptr);
  EXPECT_EQ(entry->isPending(), false);
  EXPECT_NE(entry2, nullptr);
  EXPECT_EQ(entry2->isPending(), false);
  EXPECT_NE(entry3, nullptr);
  EXPECT_EQ(entry3->isPending(), false);
}

TEST_F(NdpTest, stressSendMulticastNeighborSoclicitsDuringStateUpdate) {
  // create 100 L3 interfaces
  auto handle = this->setupTestHandleWithNdpTimeout(seconds(0), 100);
  auto sw = handle->getSw();

  this->addRoutes();

  // Step 1: resolve one non-linklocal IP
  sw->linkStateChanged(PortID(1), true, cfg::PortType::INTERFACE_PORT);
  auto vlanID = VlanID(5);
  auto intfID =
      sw->getState()->getInterfaceIDForPort(PortDescriptor(PortID(1)));
  auto targetIP = IPAddressV6("2401:db00:2110:3004::1:0");

  std::optional<WaitForNdpEntryReachable> neighborReachable;
  neighborReachable.emplace(sw, targetIP, intfID);

  sendNeighborAdvertisement(
      handle.get(),
      targetIP.str(),
      "02:10:20:30:40:22",
      PortDescriptor(PortID(1)),
      vlanID);
  EXPECT_TRUE(neighborReachable.value().wait());

  // Step 2: send multicast neighbor solicit packets from all 100 interfaces in
  // another thread
  auto* evb = sw->getBackgroundEvb();
  evb->runInFbossEventBaseThread([&]() {
    sw->getIPv6Handler()->sendMulticastNeighborSolicitation(
        sw, IPAddressV6("fe80::face:b000"));
  });

  // Step 3: trigger switch state update by flushing NDP entries
  ThriftHandler thriftHandler(sw);
  auto binAddr = toBinaryAddress(IPAddressV6("2401:db00:2110:3004::1:0"));
  thriftHandler.flushNeighborEntry(make_unique<BinaryAddress>(binAddr), 5);
  waitForStateUpdates(sw);

  // Step 4: verified no crash and non-linklocal IP can still be resolved
  sendNeighborAdvertisement(
      handle.get(),
      targetIP.str(),
      "02:10:20:30:40:22",
      PortDescriptor(PortID(1)),
      vlanID);

  EXPECT_TRUE(neighborReachable.value().wait());
}
