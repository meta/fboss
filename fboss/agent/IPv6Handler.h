/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "fboss/agent/StateObserver.h"
#include "fboss/agent/ndp/IPv6RouteAdvertiser.h"
#include "fboss/agent/packet/ICMPHdr.h"
#include "fboss/agent/packet/NDP.h"
#include "fboss/agent/types.h"
#include "fboss/lib/RadixTree.h"

#include <boost/container/flat_map.hpp>
#include <folly/IPAddressV6.h>
#include <folly/MacAddress.h>
#include <folly/Synchronized.h>
#include <memory>
#include <unordered_set>

DECLARE_bool(send_icmp_time_exceeded);

namespace folly {
namespace io {
class Cursor;
}
} // namespace folly

namespace facebook::fboss {

class IPv6Hdr;
class Interface;
class RxPacket;
class StateDelta;
class SwitchState;
class Vlan;
class SwSwitch;

class IPv6Handler : public StateObserver {
 public:
  enum : uint16_t { ETHERTYPE_IPV6 = 0x86dd };
  enum : uint32_t { IPV6_MIN_MTU = 1280 };

  explicit IPv6Handler(SwSwitch* sw);
  ~IPv6Handler() override;

  void stateUpdated(const StateDelta& delta) override;

  template <typename VlanOrIntfT>
  void handlePacket(
      std::unique_ptr<RxPacket> pkt,
      folly::MacAddress dst,
      folly::MacAddress src,
      folly::io::Cursor cursor,
      const std::shared_ptr<VlanOrIntfT>& vlanOrIntf);

  void floodNeighborAdvertisements();

  /*
   * TODO(aeckert): 17949183 unify packet handling pipeline and then
   * make this private again.
   */
  void sendMulticastNeighborSolicitations(
      PortID ingressPort,
      const folly::IPAddressV6& targetIP,
      RouterID vrf);

  /*
   * Only tests use this API directly, otherwise should only be invoked
   * internally in response to incoming packets
   */
  void sendNeighborAdvertisement(
      std::optional<VlanID> vlan,
      folly::MacAddress srcMac,
      folly::IPAddressV6 srcIP,
      folly::MacAddress dstMac,
      folly::IPAddressV6 dstIP,
      const std::optional<PortDescriptor>& portDescriptor =
          std::optional<PortDescriptor>());

  // Send a multicast NDP solicitation for targetIP out of intf, if intf
  // reaches it. Every interface reaches fe80::/64, so a link-local target is
  // solicited only on the neighbor's interface, not on every interface. The
  // second version uses the intf MAC as the source MAC.
  static void sendMulticastNeighborSolicitation(
      SwSwitch* sw,
      const std::shared_ptr<Interface>& intf,
      const folly::IPAddressV6& targetIP,
      const folly::MacAddress& srcMac);
  static void sendMulticastNeighborSolicitation(
      SwSwitch* sw,
      const std::shared_ptr<Interface>& intf,
      const folly::IPAddressV6& targetIP);

  static void sendUnicastNeighborSolicitation(
      SwSwitch* sw,
      const folly::IPAddressV6& targetIP,
      const folly::MacAddress& targetMac,
      const folly::IPAddressV6& srcIP,
      const folly::MacAddress& srcMac,
      const std::optional<VlanID>& vlanID,
      const PortDescriptor& portDescriptor);

 private:
  struct ICMPHeaders;
  using RAMap = boost::container::flat_map<InterfaceID, IPv6RouteAdvertiser>;

  // Forbidden copy constructor and assignment operator
  IPv6Handler(IPv6Handler const&) = delete;
  IPv6Handler& operator=(IPv6Handler const&) = delete;

  bool raEnabled(const Interface* intf) const;
  void intfAdded(const SwitchState* state, const Interface* intf);
  void intfDeleted(const Interface* intf);

  void sendICMPv6TimeExceeded(
      PortID srcPort,
      std::optional<VlanID> srcVlan,
      folly::MacAddress dst,
      folly::MacAddress src,
      IPv6Hdr& v6Hdr,
      folly::io::Cursor cursor);

  void sendICMPv6PacketTooBig(
      PortID srcPort,
      std::optional<VlanID> srcVlan,
      folly::MacAddress dst,
      folly::MacAddress src,
      IPv6Hdr& v6Hdr,
      int expectedMtu,
      folly::io::Cursor cursor);
  /**
   * Function to handle ICMPv6
   *
   * For now, the controller just handle ND related ICMPv6 packets. For
   * other types, the packet is returned back to the caller through the return
   * value.
   *
   * @return nullptr The packet has been handled by this function
   *         others The function does not handle the packet. It is up to the
   *                caller to decide what to do with the packet (i.e. forward
   *                the packet to host)
   */
  template <typename VlanOrIntfT>
  std::unique_ptr<RxPacket> handleICMPv6Packet(
      std::unique_ptr<RxPacket> pkt,
      folly::MacAddress dst,
      folly::MacAddress src,
      const IPv6Hdr& ipv6,
      folly::io::Cursor cursor,
      const std::shared_ptr<VlanOrIntfT>& vlanOrIntf);
  void handleRouterSolicitation(
      std::unique_ptr<RxPacket> pkt,
      const ICMPHeaders& hdr,
      folly::io::Cursor cursor);
  void handleRouterAdvertisement(
      std::unique_ptr<RxPacket> pkt,
      const ICMPHeaders& hdr,
      folly::io::Cursor cursor);
  template <typename VlanOrIntfT>
  void handleNeighborSolicitation(
      std::unique_ptr<RxPacket> pkt,
      const ICMPHeaders& hdr,
      folly::io::Cursor cursor,
      const std::shared_ptr<VlanOrIntfT>& vlanOrIntf);
  template <typename VlanOrIntfT>
  void handleNeighborAdvertisement(
      std::unique_ptr<RxPacket> pkt,
      const ICMPHeaders& hdr,
      folly::io::Cursor cursor,
      const std::shared_ptr<VlanOrIntfT>& vlanOrIntf);

  bool checkNdpPacket(const ICMPHeaders& hdr, const RxPacket* pkt) const;

  void resolveDestAndHandlePacket(
      IPv6Hdr hdr,
      std::unique_ptr<RxPacket> pkt,
      folly::MacAddress dst,
      folly::MacAddress src,
      folly::io::Cursor cursor,
      RouterID vrf);

  static void sendNeighborSolicitation(
      SwSwitch* sw,
      const folly::IPAddressV6& dstIP,
      const folly::MacAddress& dstMac,
      const folly::IPAddressV6& srcIP,
      const folly::MacAddress& srcMac,
      const folly::IPAddressV6& neighborIP,
      const std::optional<VlanID>& vlanID,
      const std::optional<PortDescriptor>& portDescriptor =
          std::optional<PortDescriptor>(),
      const NDPOptions& options = NDPOptions());

  template <typename VlanOrIntfT>
  void receivedNdpNotMine(
      const std::shared_ptr<VlanOrIntfT>& vlanOrIntf,
      folly::IPAddressV6 ip,
      folly::MacAddress macAddr,
      PortDescriptor port,
      ICMPv6Type type,
      uint32_t flags);

  template <typename VlanOrIntfT>
  void receivedNdpMine(
      const std::shared_ptr<VlanOrIntfT>& vlanOrIntf,
      folly::IPAddressV6 ip,
      folly::MacAddress macAddr,
      PortDescriptor port,
      ICMPv6Type type,
      uint32_t flags);

  void rebuildDecapMySidCache(const std::shared_ptr<SwitchState>& state);

  struct DecapMySidCache {
    std::unordered_set<folly::IPAddressV6> decapMySids;
    facebook::network::RadixTree<folly::IPAddressV6, bool> decapMySidSubnets;
  };

  SwSwitch* sw_{nullptr};
  RAMap routeAdvertisers_;
  folly::Synchronized<DecapMySidCache> decapMySidCache_;
};

} // namespace facebook::fboss
