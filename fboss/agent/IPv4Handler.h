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

#include "fboss/agent/types.h"

#include <memory>

#include <folly/IPAddressV4.h>
#include <folly/MacAddress.h>
#include "fboss/agent/packet/IPv4Hdr.h"

namespace folly {
namespace io {
class Cursor;
}
} // namespace folly

DECLARE_bool(ipv4_ext_headers_enabled);

namespace facebook::fboss {

class RxPacket;
class SwitchState;
class SwSwitch;
class Vlan;

class IPv4Handler {
 public:
  enum : uint16_t { ETHERTYPE_IPV4 = 0x0800 };

  explicit IPv4Handler(SwSwitch* sw);

  template <typename VlanOrIntfT>
  void handlePacket(
      std::unique_ptr<RxPacket> pkt,
      folly::MacAddress dst,
      folly::MacAddress src,
      folly::io::Cursor cursor,
      const std::shared_ptr<VlanOrIntfT>& vlanOrIntf);

  /*
   * TODO(aeckert): t17949183 unify packet handling pipeline and then
   * make this private again.
   */
  bool resolveMac(
      std::shared_ptr<SwitchState> state,
      PortID ingressPort,
      folly::IPAddressV4 dest,
      RouterID vrf);

 private:
  void sendICMPTimeExceeded(
      PortID port,
      std::optional<VlanID> srcVlan,
      folly::MacAddress dst,
      folly::MacAddress src,
      IPv4Hdr& v4Hdr,
      folly::io::Cursor cursor);

  // Forbidden copy constructor and assignment operator
  IPv4Handler(IPv4Handler const&) = delete;
  IPv4Handler& operator=(IPv4Handler const&) = delete;

  SwSwitch* sw_{nullptr};
};

} // namespace facebook::fboss
