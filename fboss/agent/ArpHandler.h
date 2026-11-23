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

#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/NeighborEntry.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/types.h"

#include <memory>

#include <folly/IPAddressV4.h>
#include <folly/MacAddress.h>

namespace folly {
namespace io {
class Cursor;
}
} // namespace folly

namespace facebook::fboss {

class RxPacket;
class SwSwitch;
class SwitchState;
class Vlan;

enum ArpOpCode : uint16_t {
  ARP_OP_REQUEST = 1,
  ARP_OP_REPLY = 2,
};

class ArpHandler {
 public:
  enum : uint16_t { ETHERTYPE_ARP = 0x0806 };

  explicit ArpHandler(SwSwitch* sw);

  template <typename VlanOrIntfT>
  void handlePacket(
      std::unique_ptr<RxPacket> pkt,
      folly::MacAddress dst,
      folly::MacAddress src,
      folly::io::Cursor cursor,
      const std::shared_ptr<VlanOrIntfT>& vlanOrIntf);

  // Send an ARP request for targetIP out of intf. The second version picks
  // the intf address that reaches targetIP as the sender IP.
  static void sendArpRequest(
      SwSwitch* sw,
      const std::shared_ptr<Interface>& intf,
      const folly::IPAddressV4& senderIP,
      const folly::IPAddressV4& targetIP);
  static void sendArpRequest(
      SwSwitch* sw,
      const std::shared_ptr<Interface>& intf,
      const folly::IPAddressV4& targetIP);

  /*
   * Send gratuitous arp on all vlans
   * */
  void floodGratuituousArp();

 private:
  // Forbidden copy constructor and assignment operator
  ArpHandler(ArpHandler const&) = delete;
  ArpHandler& operator=(ArpHandler const&) = delete;

  void sendArpReply(
      std::optional<VlanID> vlan,
      PortID port,
      folly::MacAddress senderMac,
      folly::IPAddressV4 senderIP,
      folly::MacAddress targetMac,
      folly::IPAddressV4 targetIP);

  template <typename VlanOrIntfT>
  void receivedArpNotMine(
      const std::shared_ptr<VlanOrIntfT>& vlanOrIntf,
      folly::IPAddressV4 ip,
      folly::MacAddress mac,
      PortDescriptor port,
      ArpOpCode op);

  template <typename VlanOrIntfT>
  void receivedArpMine(
      const std::shared_ptr<VlanOrIntfT>& vlanOrIntf,
      folly::IPAddressV4 ip,
      folly::MacAddress mac,
      PortDescriptor port,
      ArpOpCode op);

  SwSwitch* sw_{nullptr};
};

} // namespace facebook::fboss
