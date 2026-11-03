/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/ndp/IPv6RouteAdvertiser.h"

#include <folly/IPAddressV6.h>
#include <folly/MacAddress.h>
#include <folly/logging/xlog.h>
#include <netinet/icmp6.h>
#include <utility>
#include "fboss/agent/FbossError.h"
#include "fboss/agent/InterfaceStats.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/ICMPHdr.h"
#include "fboss/agent/packet/IPv6Hdr.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/SwitchState.h"

using folly::IOBuf;
using folly::IPAddressV6;
using folly::MacAddress;
using folly::io::Cursor;
using folly::io::RWPrivateCursor;

namespace {

uint32_t getAdvertisementPacketBodySize(uint32_t items_count) {
  uint32_t bodyLength = 4 + // hop limit, flags, lifetime
      4 + // reachable timer
      4 + // retrans timer
      8 + // src MAC option
      8 + // MTU option
      (32 * items_count); // prefix options

  return bodyLength;
}

using Prefix = std::pair<IPAddressV6, uint8_t>;

std::set<Prefix> getPrefixesToAdvertise(
    const facebook::fboss::Interface* intf) {
  std::set<Prefix> prefixes;
  for (auto iter : std::as_const(*intf->getAddresses())) {
    auto addr = folly::IPAddress(iter.first);
    if (!addr.isV6()) {
      continue;
    }
    uint8_t mask = iter.second->cref();
    // For IPs that are configured with a 128-bit mask, don't bother including
    // them in the advertisement, since no-one else besides us can belong to
    // this subnet.
    if (mask == 128) {
      continue;
    }
    prefixes.emplace(addr.asV6().mask(mask), mask);
  }
  return prefixes;
}
} // namespace

namespace facebook::fboss {

/*
 * IPv6RAImpl is the class that actually handles sending out the RA packets.
 *
 * This uses AsyncTimeout to receive timeout notifications in the SwSwitch's
 * background event thread, and send out RA packets every time the timer fires.
 */
class IPv6RAImpl : private folly::AsyncTimeout {
 public:
  IPv6RAImpl(SwSwitch* sw, const SwitchState* state, const Interface* intf);

  SwSwitch* getSw() const {
    return sw_;
  }

  static void start(IPv6RAImpl* ra);
  static void stop(IPv6RAImpl* ra);

 private:
  // Forbidden copy constructor and assignment operator
  IPv6RAImpl(IPv6RAImpl const&) = delete;
  IPv6RAImpl& operator=(IPv6RAImpl const&) = delete;

  void timeoutExpired() noexcept override {
    sendRouteAdvertisement();
    scheduleTimeout(interval_);
  }

  void initPacket(const Interface* intf);
  void sendRouteAdvertisement();

  std::chrono::milliseconds interval_;
  folly::IOBuf buf_;
  SwSwitch* sw_{nullptr};
  InterfaceID intfID_;
};

IPv6RAImpl::IPv6RAImpl(
    SwSwitch* sw,
    const SwitchState* /*state*/,
    const Interface* intf)
    : AsyncTimeout(sw->getBackgroundEvb()), sw_(sw), intfID_(intf->getID()) {
  std::chrono::seconds raInterval(
      intf->getNdpConfig()
          ->get<switch_config_tags::routerAdvertisementSeconds>()
          ->cref());
  interval_ = raInterval;
  initPacket(intf);
}

void IPv6RAImpl::start(IPv6RAImpl* ra) {
  ra->scheduleTimeout(ra->interval_);
}

void IPv6RAImpl::stop(IPv6RAImpl* ra) {
  /*
   * Just before going down we send one last route
   * advertisement to avoid RA's timing out while
   * controller is restarted
   */
  ra->cancelTimeout();
  ra->sendRouteAdvertisement();
  delete ra;
}

void IPv6RAImpl::initPacket(const Interface* intf) {
  auto totalLength = IPv6RouteAdvertiser::getPacketSize(intf);
  buf_ = IOBuf(IOBuf::CREATE, totalLength);
  buf_.append(totalLength);
  RWPrivateCursor cursor(&buf_);
  IPv6RouteAdvertiser::createAdvertisementPacket(
      sw_,
      intf,
      &cursor,
      MacAddress("33:33:00:00:00:01"),
      IPAddressV6("ff02::1"));
}

void IPv6RAImpl::sendRouteAdvertisement() {
  XLOG(DBG5) << "sending route advertisement:\n"
             << PktUtil::hexDump(Cursor(&buf_));

  // Allocate a new packet, and copy our data into it.
  //
  // Note that we unfortunately do need to perform a buffer copy here.
  // The TxPacket is required to use DMA memory for its buffer, so we can't do
  // a simple clone.
  //
  // TODO: In the future it would be nice to support allocating buf_ in a DMA
  // buffer so that we really can just clone a reference to it here, rather
  // than doing a copy.
  uint32_t pktLen = buf_.length();
  auto pkt = sw_->allocatePacket(pktLen);
  RWPrivateCursor cursor(pkt->buf());
  cursor.push(buf_.data(), buf_.length());

  std::optional<PortDescriptor> portDescriptor{std::nullopt};
  auto intf = sw_->getState()->getInterfaces()->getNodeIf(intfID_);
  if (intf) {
    if (intf->getType() == cfg::InterfaceType::SYSTEM_PORT) {
      // VOQ switches don't use VLANs (no broadcast domain).
      // Find the port to send out the pkt with pipeline bypass on.
      portDescriptor =
          PortDescriptor(getPortID(*intf->getSystemPortID(), sw_->getState()));
    } else if (intf->getType() == cfg::InterfaceType::PORT) {
      // A port router interface is bound to either a physical port or an
      // aggregate port.
      if (auto aggregatePortID = intf->getAggregatePortIDf()) {
        portDescriptor = PortDescriptor(*aggregatePortID);
      } else {
        portDescriptor = PortDescriptor(intf->getPortID());
      }
    }
  } else {
    XLOG(ERR) << " Skip sending router advertisement for non "
              << "existent interface: " << intfID_;
    return;
  }
  sw_->sendNetworkControlPacketAsync(std::move(pkt), portDescriptor);
  sw_->interfaceStats(intfID_)->sentRouterAdvertisement();
}

IPv6RouteAdvertiser::IPv6RouteAdvertiser(
    SwSwitch* sw,
    const SwitchState* state,
    const Interface* intf) {
  adv_ = new IPv6RAImpl(sw, state, intf);
  sw->getBackgroundEvb()->runInFbossEventBaseThread(IPv6RAImpl::start, adv_);
}

IPv6RouteAdvertiser::IPv6RouteAdvertiser(IPv6RouteAdvertiser&& other) noexcept
    : adv_(other.adv_) {
  other.adv_ = nullptr;
}

IPv6RouteAdvertiser::~IPv6RouteAdvertiser() {
  stop();
}

IPv6RouteAdvertiser& IPv6RouteAdvertiser::operator=(
    IPv6RouteAdvertiser&& other) noexcept {
  if (this != &other) {
    stop();
    adv_ = std::exchange(other.adv_, nullptr);
  }
  return *this;
}

void IPv6RouteAdvertiser::stop() {
  if (!adv_) {
    return;
  }
  adv_->getSw()->getBackgroundEvb()->runInFbossEventBaseThread(
      IPv6RAImpl::stop, adv_);
  adv_ = nullptr;
}

/* static */ uint32_t IPv6RouteAdvertiser::getPacketSize(
    const Interface* intf) {
  auto prefixCount = getPrefixesToAdvertise(intf).size();
  auto bodyLength = getAdvertisementPacketBodySize(prefixCount);

  return ICMPHdr::computeTotalLengthV6(bodyLength);
}

/* static */ void IPv6RouteAdvertiser::createAdvertisementPacket(
    SwSwitch* sw,
    const Interface* intf,
    folly::io::RWPrivateCursor* cursor,
    folly::MacAddress dstMac,
    const folly::IPAddressV6& dstIP) {
  const auto ndpConfig = intf->getNdpConfig();

  // Settings
  uint8_t hopLimit = ndpConfig->get<switch_config_tags::curHopLimit>()->cref();
  // Set managed and other bits in router advertisements.
  // These bits control whether address and other information
  // (e.g. where to grab the image) is available via DHCP.
  uint8_t flags = 0;
  if (ndpConfig->get<switch_config_tags::routerAdvertisementManagedBit>()
          ->cref()) {
    flags |= ND_RA_FLAG_MANAGED;
  }
  if (ndpConfig->get<switch_config_tags::routerAdvertisementOtherBit>()
          ->cref()) {
    flags |= ND_RA_FLAG_OTHER;
  }
  std::chrono::seconds lifetime(
      ndpConfig->get<switch_config_tags::routerLifetime>()->cref());
  std::chrono::seconds reachableTimer(0);
  std::chrono::seconds retransTimer(0);
  uint32_t prefixValidLifetime =
      ndpConfig->get<switch_config_tags::prefixValidLifetimeSeconds>()->cref();
  uint32_t prefixPreferredLifetime =
      ndpConfig->get<switch_config_tags::prefixPreferredLifetimeSeconds>()
          ->cref();
  uint32_t mtu = intf->getMtu();
  auto prefixes = getPrefixesToAdvertise(intf);

  auto serializeBody = [&](RWPrivateCursor* cur) {
    cur->writeBE<uint8_t>(hopLimit);
    cur->writeBE<uint8_t>(flags);
    cur->writeBE<uint16_t>(static_cast<uint16_t>(lifetime.count()));
    cur->writeBE<uint32_t>(static_cast<uint32_t>(reachableTimer.count()));
    cur->writeBE<uint32_t>(static_cast<uint32_t>(retransTimer.count()));

    // Source MAC option
    cur->writeBE<uint8_t>(
        static_cast<uint8_t>(1)); // Option type (src link-layer address)
    cur->writeBE<uint8_t>(static_cast<uint8_t>(1)); // Option length = 1 (x8)
    cur->push(intf->getMac().bytes(), MacAddress::SIZE);

    // Prefix options
    for (const auto& prefix : prefixes) {
      cur->writeBE<uint8_t>(
          static_cast<uint8_t>(3)); // Option type (prefix information)
      cur->writeBE<uint8_t>(static_cast<uint8_t>(4)); // Option length = 4 (x8)
      cur->writeBE<uint8_t>(prefix.second);
      uint8_t prefixFlags = 0xc0; // on link, autonomous address configuration
      cur->writeBE<uint8_t>(prefixFlags);
      cur->writeBE<uint32_t>(prefixValidLifetime);
      cur->writeBE<uint32_t>(prefixPreferredLifetime);
      cur->writeBE<uint32_t>(static_cast<uint32_t>(0)); // reserved
      cur->push(prefix.first.bytes(), IPAddressV6::byteCount());
    }

    // MTU option
    cur->writeBE<uint8_t>(static_cast<uint8_t>(5)); // Option type (MTU)
    cur->writeBE<uint8_t>(static_cast<uint8_t>(1)); // Option length = 1 (x8)
    cur->writeBE<uint16_t>(static_cast<uint16_t>(0)); // Reserved
    cur->writeBE<uint32_t>(mtu);
  };

  auto bodyLength = getAdvertisementPacketBodySize(prefixes.size());

  auto routerAddress = ndpConfig->get<switch_config_tags::routerAddress>();
  IPAddressV6 srcIP = routerAddress
      ? folly::IPAddressV6(routerAddress->cref())
      : folly::IPAddressV6(IPAddressV6::LINK_LOCAL, intf->getMac());
  IPv6Hdr ipv6(srcIP, dstIP);
  ipv6.trafficClass = kGetNetworkControlTrafficClass();
  ipv6.payloadLength = ICMPHdr::SIZE + bodyLength;
  ipv6.nextHeader = static_cast<uint8_t>(IP_PROTO::IP_PROTO_IPV6_ICMP);
  ipv6.hopLimit = 255;

  ICMPHdr icmp6(
      static_cast<uint8_t>(ICMPv6Type::ICMPV6_TYPE_NDP_ROUTER_ADVERTISEMENT),
      0,
      0);

  icmp6.serializeFullPacket(
      cursor,
      dstMac,
      intf->getMac(),
      sw->getVlanIDForTx(intf->getID()),
      ipv6,
      bodyLength,
      serializeBody);
}

} // namespace facebook::fboss
