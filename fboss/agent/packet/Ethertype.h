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
#include <cinttypes>

/*
 * EtherType
 *
 * References:
 *   http://www.networksorcery.com/enp/protocol/802/ethertypes.htm
 *   https://en.wikipedia.org/wiki/EtherType
 */

namespace facebook::fboss {

enum class ETHERTYPE : uint16_t {

  // Internet Protocol (version 4)
  ETHERTYPE_IPV4 = 0x0800,

  // Address Resolution Protocol
  ETHERTYPE_ARP = 0x0806,

  // Wake-on-LAN
  ETHERTYPE_WOL = 0x0842,

  // Reverse Address Resolution Protocol
  ETHERTYPE_RARP = 0x8035,

  // IEEE 802.1Q
  ETHERTYPE_VLAN = 0x8100,

  // Internet Protocol (version 6)
  ETHERTYPE_IPV6 = 0x86DD,

  // MPLS Label Switched Packet
  ETHERTYPE_MPLS = 0x8847,

  // Jumbo Frame (instead of actual length)
  ETHERTYPE_JUMBO = 0x8870,

  // Extensible Authentication Protocol (802.1X)
  ETHERTYPE_EAPOL = 0x888E,

  // IEEE 802.1ad (Q-in-Q)
  ETHERTYPE_QINQ = 0x88A8,

  // Link Layer Discovery Protocol
  ETHERTYPE_LLDP = 0x88CC,

  // Link Aggregation Control Protocol and Marker Protocol
  ETHERTYPE_SLOW_PROTOCOLS = 0x8809,

  // Ethernet passive optical n/ws. Used for pause frames
  ETHERTYPE_EPON = 0x8808,

  // Congestion Signaling (CSIG) tag
  ETHERTYPE_CSIG = 0x9900,
};
} // namespace facebook::fboss
