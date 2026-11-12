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

#include "fboss/agent/NeighborCache.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/packet/ICMPHdr.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/types.h"

#include <folly/IPAddressV6.h>
#include <folly/MacAddress.h>
#include <list>
#include <string>

namespace facebook::fboss {

enum class ICMPv6Type : uint8_t;

class NdpCache : public NeighborCache<NdpTable> {
 public:
  NdpCache(
      SwSwitch* sw,
      const SwitchState* state,
      VlanID vlanID,
      std::string vlanName,
      InterfaceID intfID);

  void sentNeighborSolicitation(folly::IPAddressV6 ip);
  void resolve(folly::IPAddressV6 ip);
  void receivedNdpMine(
      folly::IPAddressV6 ip,
      folly::MacAddress mac,
      PortDescriptor port,
      ICMPv6Type type,
      uint32_t flags);
  void receivedNdpNotMine(
      folly::IPAddressV6 ip,
      folly::MacAddress mac,
      PortDescriptor port,
      ICMPv6Type type,
      uint32_t flags);

  void checkReachability(
      folly::IPAddressV6 targetIP,
      folly::MacAddress targetMac,
      PortDescriptor port) const override;

  void probeFor(folly::IPAddressV6 ip) const override;

  std::list<NdpEntryThrift> getNdpCacheData();

  std::optional<NdpEntryThrift> getNdpCacheData(folly::IPAddressV6 ip) const;

 private:
  void receivedNeighborAdvertisementMine(
      folly::IPAddressV6 ip,
      folly::MacAddress mac,
      PortDescriptor port,
      ICMPv6Type type,
      uint32_t flags);
  void receivedNeighborSolicitationMine(
      folly::IPAddressV6 ip,
      folly::MacAddress mac,
      PortDescriptor port,
      ICMPv6Type type);
};

} // namespace facebook::fboss
