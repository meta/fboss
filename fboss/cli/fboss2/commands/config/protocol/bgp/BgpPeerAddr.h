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

#include <folly/IPAddress.h>
#include <optional>
#include <string>
#include <vector>

#ifndef IS_OSS
#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#else
#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#endif

/**
 * Peer-address helpers shared by the BGP neighbor commands (`config protocol
 * bgp neighbor` / `delete protocol bgp neighbor`).
 *
 * peer_addr is the identity of a BgpPeer, but the same address can be spelled
 * many ways (2001:DB8::1 vs 2001:db8::1), and passive listening sessions may
 * use prefix notation (2001:db8::/64, per bgp_config.thrift). Both commands
 * normalize the CLI-supplied address once and compare stored entries in
 * normalized form so every spelling addresses the same peer.
 *
 * Prefixes are masked (10.0.0.1/24 -> 10.0.0.0/24) because bgpd keys dynamic
 * peers by the masked network (IPAddress::createNetwork in its config loader),
 * so two spellings that differ only in host bits are the same neighbor to it.
 */
namespace facebook::fboss::bgpcli {

// Normalize an address (or prefix, for passive listening sessions) to its
// canonical string form; a prefix has its host bits cleared. nullopt if the
// input parses as neither.
inline std::optional<std::string> normalizeBgpPeerAddr(const std::string& s) {
  if (auto addr = folly::IPAddress::tryFromString(s)) {
    return addr->str();
  }
  if (auto network = folly::IPAddress::tryCreateNetwork(
          s, /* defaultCidr */ -1, /* applyMask */ true)) {
    return folly::IPAddress::networkToString(*network);
  }
  return std::nullopt;
}

// Find the peer whose (normalized) peer_addr matches the given normalized
// address; peers().end() if absent. Stored entries may predate normalization,
// so their normalized form is compared when possible.
inline std::vector<bgp::thrift::BgpPeer>::iterator findBgpPeer(
    bgp::thrift::BgpConfig& cfg,
    const std::string& normalizedAddr) {
  auto& peers = *cfg.peers();
  for (auto it = peers.begin(); it != peers.end(); ++it) {
    const std::string key =
        normalizeBgpPeerAddr(*it->peer_addr()).value_or(*it->peer_addr());
    if (key == normalizedAddr) {
      return it;
    }
  }
  return peers.end();
}

} // namespace facebook::fboss::bgpcli
