/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/protocol/bgp/neighbor/CmdConfigProtocolBgpNeighbor.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/core.h>
#include <folly/CppAttributes.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "configerator/structs/neteng/fboss/bgp/if/gen-cpp2/bgp_attr_types.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/BgpCliAttrHandlers.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/BgpPeerAddr.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fmt/format.h"
#include "thrift/lib/cpp/Thrift.h"

#ifndef IS_OSS
#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#else
#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#endif

namespace facebook::fboss {

namespace {

// CLI attribute names, exactly as documented (two-token attributes keep their
// space). Kept here (not as raw string literals at the dispatch sites) so the
// valid-attribute set and the handler table stay in sync.
constexpr std::string_view kRemoteAsn = "remote-asn";
constexpr std::string_view kLocalAsn = "local-asn";
constexpr std::string_view kDescription = "description";
constexpr std::string_view kPeerTag = "peer-tag";
constexpr std::string_view kPeerGroup = "peer-group";
constexpr std::string_view kIngressPolicy = "ingress-policy";
constexpr std::string_view kEgressPolicy = "egress-policy";
constexpr std::string_view kRrClient = "rr-client";
constexpr std::string_view kConfedPeer = "confed-peer";
constexpr std::string_view kRedistributePeer = "redistribute-peer";
constexpr std::string_view kEnhancedRouteRefresh = "enhanced-route-refresh";
constexpr std::string_view kRouteRefresh = "route-refresh";
constexpr std::string_view kRemovePrivateAs = "remove-private-as";
constexpr std::string_view kEnforceFirstAs = "enforce-first-as";
constexpr std::string_view kTtlSecurityHops = "ttl-security-hops";
constexpr std::string_view kPassive = "passive";
constexpr std::string_view kPeerPort = "peer-port";
constexpr std::string_view kAddPathSend = "add-path send";
constexpr std::string_view kAddPathReceive = "add-path receive";
constexpr std::string_view kAfiDisableIpv4Afi = "afi disable-ipv4-afi";
constexpr std::string_view kAfiDisableIpv6Afi = "afi disable-ipv6-afi";
constexpr std::string_view kAfiIpv4OverIpv6Nh = "afi ipv4-over-ipv6-nh";
constexpr std::string_view kAfiIpv4LabeledUnicast = "afi ipv4-labeled-unicast";
constexpr std::string_view kAfiIpv6LabeledUnicast = "afi ipv6-labeled-unicast";
constexpr std::string_view kBindAddrAddress = "bind-addr address";
constexpr std::string_view kBindAddrPort = "bind-addr port";
constexpr std::string_view kGracefulRestartTime =
    "graceful-restart restart-time";
constexpr std::string_view kGracefulRestartStatefulHa =
    "graceful-restart stateful-ha";
constexpr std::string_view kMaxRoutePreFilter = "max-route pre-filter";
constexpr std::string_view kMaxRoutePostFilter = "max-route post-filter";
constexpr std::string_view kMaxRoutePostWarningThreshold =
    "max-route post-warning-threshold";
constexpr std::string_view kTimersHoldTime = "timers hold-time";
constexpr std::string_view kTimersKeepalive = "timers keepalive";
constexpr std::string_view kTimersOutDelay = "timers out-delay";
// Parity attributes: not in the documented neighbor grammar, but supported by
// the per-attribute peer commands this dispatcher replaces (and by real
// BgpPeer thrift fields), so dropping them would regress existing usage.
constexpr std::string_view kNextHop4 = "next-hop4";
constexpr std::string_view kNextHop6 = "next-hop6";
constexpr std::string_view kNextHopSelf = "next-hop-self";
constexpr std::string_view kPeerId = "peer-id";
constexpr std::string_view kPeerType = "type";
constexpr std::string_view kLinkBandwidth = "link-bandwidth";
constexpr std::string_view kAdvertiseLbw = "advertise-lbw";
constexpr std::string_view kReceiveLbw = "receive-lbw";
constexpr std::string_view kTimersWithdrawUnprogDelay =
    "timers withdraw-unprog-delay";
constexpr std::string_view kMaxRoutePreWarningThreshold =
    "max-route pre-warning-threshold";
constexpr std::string_view kMaxRoutePreWarningOnly =
    "max-route pre-warning-only";
constexpr std::string_view kMaxRoutePostWarningOnly =
    "max-route post-warning-only";
// BgpPeer fields deliberately NOT exposed: `enabled` and `router_port_id`
// (nothing in bgpd reads them) and
// bgp_peer_timers.graceful_restart_end_of_rib_seconds (same). Staging them
// would persist config the daemon ignores.

// Protocol ranges bgpd enforces (or the wire format silently truncates to):
// the OPEN hold time is 16 bits and bgpd refuses 1-2s; the graceful-restart
// restart time is the 12-bit field of RFC 4724; TTL security hops are
// bgpd's kMin/kMaxTtlSecurityHops (it throws on anything else at load).
constexpr int32_t kHoldTimeMinSeconds = 3;
constexpr int32_t kHoldTimeMaxSeconds = 65535;
constexpr int32_t kGracefulRestartMaxSeconds = 4095;
constexpr int32_t kTtlSecurityHopsMin = 1;
constexpr int32_t kTtlSecurityHopsMax = 255;

// Value placeholders for the generated help. One per value shape, so the
// wording matches the factory that validates that shape.
constexpr std::string_view kUsageBool = "<true|false>";
constexpr std::string_view kUsageAsn = "<asn>";
constexpr std::string_view kUsageName = "<name>";
constexpr std::string_view kUsageString = "<string>";
constexpr std::string_view kUsageText = "<text ...>";
constexpr std::string_view kUsagePolicy = "<policy-name>";
constexpr std::string_view kUsageIp = "<ip-address>";
constexpr std::string_view kUsageIpv4 = "<ipv4-address>";
constexpr std::string_view kUsageIpv6 = "<ipv6-address>";
constexpr std::string_view kUsageSeconds = "<seconds>";
constexpr std::string_view kUsageHoldTime = "<seconds: 0 or 3-65535>";
constexpr std::string_view kUsageGracefulRestart = "<seconds: 0-4095>";
constexpr std::string_view kUsageRouteCount = "<route-count>";
constexpr std::string_view kUsageBitRate = "<bits-per-second>[K|M|G] | auto";
constexpr std::string_view kUsageTtlHops = "<hops: 1-255>";

using bgpcli::Tokens;
using BgpConfig = bgp::thrift::BgpConfig;
using BgpPeer = bgp::thrift::BgpPeer;
using PeerGroup = bgp::thrift::PeerGroup;
using bgpcli::asnAttr;
using bgpcli::AttrHandler;
using bgpcli::bitRateAttr;
using bgpcli::boolAttr;
using bgpcli::err;
using bgpcli::intAttr;
using bgpcli::ipAttr;
using bgpcli::joinedStringAttr;
using bgpcli::ok;
using bgpcli::parseBool;
using bgpcli::rejectedAttr;
using bgpcli::Result;
using bgpcli::routeCountAttr;
using bgpcli::secondsAttr;
using bgpcli::SecondsRange;
using bgpcli::stringAttr;
using bgpcli::thriftEnumAttr;
using facebook::neteng::fboss::bgp_attr::AddPath;
using facebook::neteng::fboss::bgp_attr::AdvertiseLinkBandwidth;
using facebook::neteng::fboss::bgp_attr::ReceiveLinkBandwidth;

// ---- per-attribute setters -------------------------------------------------
// Pure thrift assignment: no validation, no messages. Parsing, bounds and the
// user-facing text all live in the shared factories in BgpCliAttrHandlers.h,
// so a setter is just the field this attribute writes. Field names map 1:1 to
// bgp_config.thrift, so the compiler validates them.

void setRemoteAsn(BgpPeer& p, int64_t v) {
  p.remote_as_4_byte() = v;
}

void setLocalAsn(BgpPeer& p, int64_t v) {
  p.local_as_4_byte() = v;
}

void setDescription(BgpPeer& p, const std::string& v) {
  p.description() = v;
}

void setPeerTag(BgpPeer& p, const std::string& v) {
  p.peer_tag() = v;
}

void setPeerGroup(BgpPeer& p, const std::string& v) {
  p.peer_group_name() = v;
}

void setIngressPolicy(BgpPeer& p, const std::string& v) {
  p.ingress_policy_name() = v;
}

void setEgressPolicy(BgpPeer& p, const std::string& v) {
  p.egress_policy_name() = v;
}

void setRrClient(BgpPeer& p, bool v) {
  p.is_rr_client() = v;
}

void setConfedPeer(BgpPeer& p, bool v) {
  p.is_confed_peer() = v;
}

void setRedistributePeer(BgpPeer& p, bool v) {
  p.is_redistribute_peer() = v;
}

void setEnhancedRouteRefresh(BgpPeer& p, bool v) {
  p.enhanced_route_refresh() = v;
}

void setRouteRefresh(BgpPeer& p, bool v) {
  p.route_refresh() = v;
}

void setRemovePrivateAs(BgpPeer& p, bool v) {
  p.remove_private_as() = v;
}

void setEnforceFirstAs(BgpPeer& p, bool v) {
  p.enforce_first_as() = v;
}

void setTtlSecurityHops(BgpPeer& p, int32_t v) {
  p.ttl_security_hops() = v;
}

// bgpd reads is_passive=true as PASSIVE_ONLY (listen) and false as
// PASSIVE_ACTIVE (listen and connect). Its session code also knows an
// ACTIVE_ONLY mode, but the config model has no way to request it (the mode
// is derived from is_passive alone), so the CLI exposes the flag itself
// rather than inventing connect-mode names.
void setPassive(BgpPeer& p, bool v) {
  p.is_passive() = v;
}

void setDisableIpv4Afi(BgpPeer& p, bool v) {
  p.disable_ipv4_afi() = v;
}

void setDisableIpv6Afi(BgpPeer& p, bool v) {
  p.disable_ipv6_afi() = v;
}

void setIpv4OverIpv6Nh(BgpPeer& p, bool v) {
  p.v4_over_v6_nexthop() = v;
}

void setBindAddrAddress(BgpPeer& p, const std::string& v) {
  p.local_addr() = v;
}

void setGracefulRestartTime(BgpPeer& p, int32_t v) {
  p.bgp_peer_timers().ensure().graceful_restart_seconds() = v;
}

void setGracefulRestartStatefulHa(BgpPeer& p, bool v) {
  p.enable_stateful_ha() = v;
}

void setMaxRoutePreFilter(BgpPeer& p, int64_t v) {
  p.pre_filter().ensure().max_routes() = v;
}

void setMaxRoutePostFilter(BgpPeer& p, int64_t v) {
  p.post_filter().ensure().max_routes() = v;
}

// RouteLimit.warning_limit is an absolute route count in the thrift schema,
// not a percentage.
void setMaxRoutePreWarningThreshold(BgpPeer& p, int64_t v) {
  p.pre_filter().ensure().warning_limit() = v;
}

void setMaxRoutePostWarningThreshold(BgpPeer& p, int64_t v) {
  p.post_filter().ensure().warning_limit() = v;
}

void setMaxRoutePreWarningOnly(BgpPeer& p, bool v) {
  p.pre_filter().ensure().warning_only() = v;
}

void setMaxRoutePostWarningOnly(BgpPeer& p, bool v) {
  p.post_filter().ensure().warning_only() = v;
}

void setTimersHoldTime(BgpPeer& p, int32_t v) {
  p.bgp_peer_timers().ensure().hold_time_seconds() = v;
}

void setTimersKeepalive(BgpPeer& p, int32_t v) {
  p.bgp_peer_timers().ensure().keep_alive_seconds() = v;
}

void setTimersOutDelay(BgpPeer& p, int32_t v) {
  p.bgp_peer_timers().ensure().out_delay_seconds() = v;
}

void setTimersWithdrawUnprogDelay(BgpPeer& p, int32_t v) {
  p.bgp_peer_timers().ensure().withdraw_unprog_delay_seconds() = v;
}

void setNextHop4(BgpPeer& p, const std::string& v) {
  p.next_hop4() = v;
}

void setNextHop6(BgpPeer& p, const std::string& v) {
  p.next_hop6() = v;
}

void setNextHopSelf(BgpPeer& p, bool v) {
  p.next_hop_self() = v;
}

void setPeerId(BgpPeer& p, const std::string& v) {
  p.peer_id() = v;
}

void setPeerType(BgpPeer& p, const std::string& v) {
  p.type() = v;
}

void setLinkBandwidth(BgpPeer& p, const std::string& v) {
  p.link_bandwidth_bps() = v;
}

void setAdvertiseLbw(BgpPeer& p, AdvertiseLinkBandwidth v) {
  p.advertise_link_bandwidth() = v;
}

void setReceiveLbw(BgpPeer& p, ReceiveLinkBandwidth v) {
  p.receive_link_bandwidth() = v;
}

// ---- inherited values ------------------------------------------------------
// bgpd resolves a peer against its peer group at STRUCT granularity: if the
// peer carries bgp_peer_timers (or pre_filter / post_filter) at all, every
// field of that struct comes from the peer and the group's copy is ignored;
// a timers struct whose other fields are thrift defaults therefore runs the
// session with hold time 0 (no keepalives). So before the first attribute of
// such a struct is set on a peer, the struct is seeded from what the peer
// inherits today -- the group's struct, else the global defaults -- and the
// one requested field is then changed on top of it.

const PeerGroup* FOLLY_NULLABLE
peerGroupOf(const BgpConfig& cfg, const BgpPeer& peer) {
  if (!peer.peer_group_name() || !cfg.peer_groups()) {
    return nullptr;
  }
  for (const auto& group : *cfg.peer_groups()) {
    if (*group.name() == *peer.peer_group_name()) {
      return &group;
    }
  }
  return nullptr;
}

void seedTimers(const BgpConfig& cfg, BgpPeer& peer) {
  if (peer.bgp_peer_timers()) {
    return;
  }
  if (const auto* group = peerGroupOf(cfg, peer);
      group && group->bgp_peer_timers()) {
    peer.bgp_peer_timers() = *group->bgp_peer_timers();
    return;
  }
  bgp::thrift::BgpPeerTimers timers;
  timers.hold_time_seconds() = *cfg.hold_time();
  timers.keep_alive_seconds() = *cfg.hold_time() / 3;
  peer.bgp_peer_timers() = std::move(timers);
}

void seedRouteLimit(const BgpConfig& cfg, BgpPeer& peer, bool pre) {
  auto field = [pre](auto& p) -> decltype(auto) {
    return pre ? p.pre_filter() : p.post_filter();
  };
  if (field(peer)) {
    return;
  }
  if (const auto* group = peerGroupOf(cfg, peer); group != nullptr) {
    if (auto groupLimit = field(*group)) {
      field(peer) = *groupLimit;
      return;
    }
  }
  // No limit anywhere above: bgpd applies none to a peer without a struct
  // (capRoutesPerPeer returns early) and reads max_routes == 0 as unlimited,
  // so seed that. The thrift default the setter's ensure() would leave behind
  // is a 12000-route hard cap with session teardown -- a warning-only or
  // threshold edit must not introduce one.
  bgp::thrift::RouteLimit unlimited;
  unlimited.max_routes() = 0;
  unlimited.warning_only() = false;
  unlimited.warning_limit() = 0;
  field(peer) = std::move(unlimited);
}

// A neighbor handler sees the whole config (for the peer group lookup) plus
// the peer being edited. The shared factories only know the peer, so they are
// adapted; the adapters for struct-valued attributes seed first.
using NeighborHandler =
    std::function<Result(const BgpConfig&, BgpPeer&, const Tokens&)>;

// One dispatch-table row: how the value is spelled in help, and the handler.
struct NeighborAttr {
  std::string usage;
  NeighborHandler handle;
};

// "<A|B|C>" from a thrift enum's value names, for the generated help.
template <typename EnumT>
std::string enumUsage() {
  std::string out = "<";
  for (auto n : apache::thrift::TEnumTraits<EnumT>::names) {
    out += out.size() == 1 ? "" : "|";
    out += std::string(n);
  }
  return out + ">";
}

NeighborHandler peerOnly(AttrHandler<BgpPeer> handler) {
  return [handler = std::move(handler)](
             const BgpConfig&, BgpPeer& peer, const Tokens& values) {
    return handler(peer, values);
  };
}

NeighborHandler withTimers(AttrHandler<BgpPeer> handler) {
  return [handler = std::move(handler)](
             const BgpConfig& cfg, BgpPeer& peer, const Tokens& values) {
    seedTimers(cfg, peer);
    return handler(peer, values);
  };
}

NeighborHandler withRouteLimit(bool pre, AttrHandler<BgpPeer> handler) {
  return [pre, handler = std::move(handler)](
             const BgpConfig& cfg, BgpPeer& peer, const Tokens& values) {
    seedRouteLimit(cfg, peer, pre);
    return handler(peer, values);
  };
}

// ---- hand-written handlers -------------------------------------------------
// Only for value shapes no factory covers, because the shape is unique to this
// attribute rather than reusable.

// add-path send/receive: a boolean at the CLI, but the two attributes share
// one thrift field -- add_path is a single enum whose values form a bitmask by
// design (RECEIVE=1, SEND=2, BOTH=3). Each takes the boolAttr text but merges
// its direction into the value in effect for the peer (its own, else the peer
// group's) instead of overwriting it. The enum has no "disabled" member and
// an unset peer field means "inherit", so a direction the group enables
// cannot be switched off per peer; that is refused rather than staged as a
// no-op the CLI would report as success.
Result applyAddPath(
    const BgpConfig& cfg,
    BgpPeer& peer,
    bool send,
    const Tokens& values) {
  const std::string_view name = send ? kAddPathSend : kAddPathReceive;
  if (values.size() != 1) {
    return err(fmt::format("Error: {} requires <true|false>", name));
  }
  auto enable = parseBool(values[0]);
  if (!enable) {
    return err(
        fmt::format(
            "Error: Invalid {} value '{}'; expected true or false",
            name,
            values[0]));
  }
  const auto* group = peerGroupOf(cfg, peer);
  const std::optional<AddPath> inherited = group && group->add_path()
      ? std::optional<AddPath>(*group->add_path())
      : std::nullopt;
  const std::optional<AddPath> effective =
      peer.add_path() ? std::optional<AddPath>(*peer.add_path()) : inherited;
  int bits = effective ? static_cast<int>(*effective) : 0;
  const int bit = send ? static_cast<int>(AddPath::SEND)
                       : static_cast<int>(AddPath::RECEIVE);
  bits = *enable ? (bits | bit) : (bits & ~bit);
  if (bits == 0) {
    if (inherited && group != nullptr) {
      return err(
          fmt::format(
              "Error: cannot disable {} on this neighbor: peer group {} "
              "enables add-path {} and BgpPeer.add_path has no disabled "
              "value to override it (an unset field inherits the group); "
              "change it on the peer group instead",
              name,
              *group->name(),
              apache::thrift::TEnumTraits<AddPath>::findName(*inherited)));
    }
    peer.add_path().reset();
  } else {
    peer.add_path() = static_cast<AddPath>(bits);
  }
  return ok(
      fmt::format(
          "Successfully {} {}", *enable ? "enabled" : "disabled", name));
}

Result addPathSend(const BgpConfig& cfg, BgpPeer& peer, const Tokens& values) {
  return applyAddPath(cfg, peer, /* send */ true, values);
}

Result
addPathReceive(const BgpConfig& cfg, BgpPeer& peer, const Tokens& values) {
  return applyAddPath(cfg, peer, /* send */ false, values);
}

// ---- the dispatch table ----------------------------------------------------
// One line per attribute: dispatch key, value shape, setter. There are no
// handler bodies here by design — if an attribute appears to need one, its
// value shape is missing a factory and the fix is to add the factory.

constexpr std::string_view kNoPeerPortField =
    "BgpPeer has no per-neighbor port field; the daemon listens on the global "
    "listen_port";
constexpr std::string_view kNoLabeledUnicastField =
    "bgp_config.thrift has no labeled-unicast field on BgpPeer";

const std::map<std::string, NeighborAttr, std::less<>>& attrHandlers() {
  auto plain = [](std::string_view usage, AttrHandler<BgpPeer> h) {
    return NeighborAttr{std::string(usage), peerOnly(std::move(h))};
  };
  auto timers = [](std::string_view usage, AttrHandler<BgpPeer> h) {
    return NeighborAttr{std::string(usage), withTimers(std::move(h))};
  };
  auto preLimit = [](std::string_view usage, AttrHandler<BgpPeer> h) {
    return NeighborAttr{
        std::string(usage), withRouteLimit(/* pre */ true, std::move(h))};
  };
  auto postLimit = [](std::string_view usage, AttrHandler<BgpPeer> h) {
    return NeighborAttr{
        std::string(usage), withRouteLimit(/* pre */ false, std::move(h))};
  };
  auto rejected = [](std::string_view name, std::string_view reason) {
    return NeighborAttr{
        fmt::format("(not supported: {})", reason),
        peerOnly(rejectedAttr<BgpPeer>(name, reason))};
  };
  static const std::map<std::string, NeighborAttr, std::less<>> kHandlers = {
      {std::string(kRemoteAsn),
       plain(kUsageAsn, asnAttr<BgpPeer>(kRemoteAsn, setRemoteAsn))},
      {std::string(kLocalAsn),
       plain(kUsageAsn, asnAttr<BgpPeer>(kLocalAsn, setLocalAsn))},
      {std::string(kDescription),
       plain(
           kUsageText,
           joinedStringAttr<BgpPeer>(kDescription, setDescription))},
      {std::string(kPeerTag),
       plain(
           kUsageString, stringAttr<BgpPeer>(kPeerTag, "string", setPeerTag))},
      {std::string(kPeerGroup),
       plain(
           kUsageName, stringAttr<BgpPeer>(kPeerGroup, "name", setPeerGroup))},
      {std::string(kIngressPolicy),
       plain(
           kUsagePolicy,
           stringAttr<BgpPeer>(
               kIngressPolicy, "policy-name", setIngressPolicy))},
      {std::string(kEgressPolicy),
       plain(
           kUsagePolicy,
           stringAttr<BgpPeer>(kEgressPolicy, "policy-name", setEgressPolicy))},
      {std::string(kRrClient),
       plain(kUsageBool, boolAttr<BgpPeer>(kRrClient, setRrClient))},
      {std::string(kConfedPeer),
       plain(kUsageBool, boolAttr<BgpPeer>(kConfedPeer, setConfedPeer))},
      {std::string(kRedistributePeer),
       plain(
           kUsageBool,
           boolAttr<BgpPeer>(kRedistributePeer, setRedistributePeer))},
      {std::string(kEnhancedRouteRefresh),
       plain(
           kUsageBool,
           boolAttr<BgpPeer>(kEnhancedRouteRefresh, setEnhancedRouteRefresh))},
      {std::string(kRouteRefresh),
       plain(kUsageBool, boolAttr<BgpPeer>(kRouteRefresh, setRouteRefresh))},
      {std::string(kRemovePrivateAs),
       plain(
           kUsageBool,
           boolAttr<BgpPeer>(kRemovePrivateAs, setRemovePrivateAs))},
      {std::string(kEnforceFirstAs),
       plain(
           kUsageBool, boolAttr<BgpPeer>(kEnforceFirstAs, setEnforceFirstAs))},
      {std::string(kTtlSecurityHops),
       plain(
           kUsageTtlHops,
           intAttr<BgpPeer>(
               kTtlSecurityHops,
               "1-255",
               kTtlSecurityHopsMin,
               kTtlSecurityHopsMax,
               setTtlSecurityHops))},
      {std::string(kPassive),
       plain(kUsageBool, boolAttr<BgpPeer>(kPassive, setPassive))},
      {std::string(kPeerPort), rejected(kPeerPort, kNoPeerPortField)},
      {std::string(kAddPathSend),
       NeighborAttr{std::string(kUsageBool), addPathSend}},
      {std::string(kAddPathReceive),
       NeighborAttr{std::string(kUsageBool), addPathReceive}},
      {std::string(kAfiDisableIpv4Afi),
       plain(
           kUsageBool,
           boolAttr<BgpPeer>(kAfiDisableIpv4Afi, setDisableIpv4Afi))},
      {std::string(kAfiDisableIpv6Afi),
       plain(
           kUsageBool,
           boolAttr<BgpPeer>(kAfiDisableIpv6Afi, setDisableIpv6Afi))},
      {std::string(kAfiIpv4OverIpv6Nh),
       plain(
           kUsageBool,
           boolAttr<BgpPeer>(kAfiIpv4OverIpv6Nh, setIpv4OverIpv6Nh))},
      {std::string(kAfiIpv4LabeledUnicast),
       rejected(kAfiIpv4LabeledUnicast, kNoLabeledUnicastField)},
      {std::string(kAfiIpv6LabeledUnicast),
       rejected(kAfiIpv6LabeledUnicast, kNoLabeledUnicastField)},
      {std::string(kBindAddrAddress),
       plain(kUsageIp, ipAttr<BgpPeer>(kBindAddrAddress, setBindAddrAddress))},
      {std::string(kBindAddrPort), rejected(kBindAddrPort, kNoPeerPortField)},
      {std::string(kGracefulRestartTime),
       timers(
           kUsageGracefulRestart,
           secondsAttr<BgpPeer>(
               kGracefulRestartTime,
               setGracefulRestartTime,
               SecondsRange{0, kGracefulRestartMaxSeconds}))},
      {std::string(kGracefulRestartStatefulHa),
       plain(
           kUsageBool,
           boolAttr<BgpPeer>(
               kGracefulRestartStatefulHa, setGracefulRestartStatefulHa))},
      {std::string(kMaxRoutePreFilter),
       preLimit(
           kUsageRouteCount,
           routeCountAttr<BgpPeer>(kMaxRoutePreFilter, setMaxRoutePreFilter))},
      {std::string(kMaxRoutePostFilter),
       postLimit(
           kUsageRouteCount,
           routeCountAttr<BgpPeer>(
               kMaxRoutePostFilter, setMaxRoutePostFilter))},
      {std::string(kMaxRoutePreWarningThreshold),
       preLimit(
           kUsageRouteCount,
           routeCountAttr<BgpPeer>(
               kMaxRoutePreWarningThreshold, setMaxRoutePreWarningThreshold))},
      {std::string(kMaxRoutePostWarningThreshold),
       postLimit(
           kUsageRouteCount,
           routeCountAttr<BgpPeer>(
               kMaxRoutePostWarningThreshold,
               setMaxRoutePostWarningThreshold))},
      {std::string(kMaxRoutePreWarningOnly),
       preLimit(
           kUsageBool,
           boolAttr<BgpPeer>(
               kMaxRoutePreWarningOnly, setMaxRoutePreWarningOnly))},
      {std::string(kMaxRoutePostWarningOnly),
       postLimit(
           kUsageBool,
           boolAttr<BgpPeer>(
               kMaxRoutePostWarningOnly, setMaxRoutePostWarningOnly))},
      {std::string(kTimersHoldTime),
       timers(
           kUsageHoldTime,
           secondsAttr<BgpPeer>(
               kTimersHoldTime,
               setTimersHoldTime,
               SecondsRange{kHoldTimeMinSeconds, kHoldTimeMaxSeconds}))},
      {std::string(kTimersKeepalive),
       timers(
           kUsageSeconds,
           secondsAttr<BgpPeer>(kTimersKeepalive, setTimersKeepalive))},
      {std::string(kTimersOutDelay),
       timers(
           kUsageSeconds,
           secondsAttr<BgpPeer>(kTimersOutDelay, setTimersOutDelay))},
      {std::string(kTimersWithdrawUnprogDelay),
       timers(
           kUsageSeconds,
           secondsAttr<BgpPeer>(
               kTimersWithdrawUnprogDelay, setTimersWithdrawUnprogDelay))},
      {std::string(kNextHop4),
       plain(
           kUsageIpv4,
           ipAttr<BgpPeer>(kNextHop4, setNextHop4, /* requireV6 */ false))},
      {std::string(kNextHop6),
       plain(
           kUsageIpv6,
           ipAttr<BgpPeer>(kNextHop6, setNextHop6, /* requireV6 */ true))},
      {std::string(kNextHopSelf),
       plain(kUsageBool, boolAttr<BgpPeer>(kNextHopSelf, setNextHopSelf))},
      {std::string(kPeerId),
       plain(kUsageString, stringAttr<BgpPeer>(kPeerId, "string", setPeerId))},
      {std::string(kPeerType),
       plain(
           kUsageString,
           stringAttr<BgpPeer>(kPeerType, "string", setPeerType))},
      {std::string(kLinkBandwidth),
       plain(
           kUsageBitRate,
           bitRateAttr<BgpPeer>(kLinkBandwidth, setLinkBandwidth))},
      {std::string(kAdvertiseLbw),
       plain(
           enumUsage<AdvertiseLinkBandwidth>(),
           thriftEnumAttr<BgpPeer, AdvertiseLinkBandwidth>(
               kAdvertiseLbw, setAdvertiseLbw))},
      {std::string(kReceiveLbw),
       plain(
           enumUsage<ReceiveLinkBandwidth>(),
           thriftEnumAttr<BgpPeer, ReceiveLinkBandwidth>(
               kReceiveLbw, setReceiveLbw))},
  };
  return kHandlers;
}

std::string validAttrList() {
  std::string out;
  for (const auto& [name, _] : attrHandlers()) {
    if (!out.empty()) {
      out += ", ";
    }
    out += name;
  }
  return out;
}

// A fresh peer for peerAddr. Setting an attribute on a not-yet-created
// neighbor implicitly creates it, so command ordering stays forgiving; a bare
// `neighbor <ip>` creates one explicitly.
BgpPeer newPeer(const std::string& peerAddr) {
  BgpPeer peer;
  peer.peer_addr() = peerAddr;
  // local_addr / next_hop4 / next_hop6 are non-optional thrift fields that
  // bgpd parses with folly::IPAddress at config load -- an empty string makes
  // the daemon abort (IPAddressFormatException) before serving any config.
  // Seed them with the unspecified address of the right family; the user
  // overrides them via `bind-addr address` / `next-hop4` / `next-hop6`.
  const bool isV6 = peerAddr.find(':') != std::string::npos;
  peer.local_addr() = isV6 ? "::" : "0.0.0.0";
  peer.next_hop4() = "0.0.0.0";
  peer.next_hop6() = "::";
  return peer;
}

} // namespace

std::vector<std::string> bgpNeighborAttributeNames() {
  std::vector<std::string> names;
  for (const auto& [name, _] : attrHandlers()) {
    names.push_back(name);
  }
  return names;
}

std::string bgpNeighborAttributeHelp() {
  size_t width = 0;
  for (const auto& [name, _] : attrHandlers()) {
    width = std::max(width, name.size());
  }
  std::string out = "Attributes (<attribute> <value>):\n";
  for (const auto& [name, attr] : attrHandlers()) {
    out += fmt::format("  {:<{}}  {}\n", name, width, attr.usage);
  }
  return out;
}

// Parse + validate at construction so queryClient stays a thin dispatch.
// Throwing std::invalid_argument is how the framework surfaces arg parse
// errors (same mechanism as BgpGlobalConfig).
BgpNeighborConfig::BgpNeighborConfig(std::vector<std::string> v)
    : utils::BaseObjectArgType<std::string>(v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "Error: neighbor <ip-address> is required, followed by an optional "
        "<attribute> <value>");
  }
  auto normalized = bgpcli::normalizeBgpPeerAddr(v[0]);
  if (!normalized) {
    throw std::invalid_argument(
        fmt::format("Error: Invalid neighbor address '{}'", v[0]));
  }
  peerAddr_ = std::move(*normalized);

  if (v.size() == 1) {
    return; // bare `neighbor <ip>`: create the peer
  }

  // Attributes may span two tokens (e.g. `timers hold-time`); match the
  // longest prefix of the remaining tokens against the dispatch table.
  const auto& handlers = attrHandlers();
  if (v.size() >= 3) {
    std::string twoToken = v[1] + " " + v[2];
    if (handlers.find(twoToken) != handlers.end()) {
      attr_ = std::move(twoToken);
      values_.assign(v.begin() + 3, v.end());
      return;
    }
  }
  if (handlers.find(v[1]) != handlers.end()) {
    attr_ = v[1];
    values_.assign(v.begin() + 2, v.end());
    return;
  }
  throw std::invalid_argument(
      fmt::format(
          "Error: unknown neighbor attribute '{}'. Valid attributes: {}",
          v[1],
          validAttrList()));
}

CmdConfigProtocolBgpNeighborTraits::RetType
CmdConfigProtocolBgpNeighbor::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto& cfg = session.getBgpConfig();
  auto& peers = *cfg.peers();
  auto existing = bgpcli::findBgpPeer(cfg, args.peerAddr());
  if (args.attr().empty() && existing != peers.end()) {
    return fmt::format("BGP neighbor {} already exists", args.peerAddr());
  }

  // Edit a copy: a rejected value (or a struct seeded for it) must leave the
  // in-memory config exactly as it was, since later lookups in the same
  // process see it whether or not it was saved.
  BgpPeer peer = existing != peers.end() ? *existing : newPeer(args.peerAddr());
  Result result = args.attr().empty()
      ? ok(fmt::format("Successfully created BGP neighbor {}", args.peerAddr()))
      // The attribute is guaranteed valid: BgpNeighborConfig's constructor
      // rejects an unknown attribute before we get here.
      : attrHandlers()
            .find(args.attr())
            ->second.handle(cfg, peer, args.values());
  if (!result.ok) {
    return result.message;
  }
  if (existing != peers.end()) {
    *existing = std::move(peer);
  } else {
    peers.push_back(std::move(peer));
  }
  if (!args.attr().empty()) {
    result.message += fmt::format(" for neighbor {}", args.peerAddr());
  }
  session.saveBgpConfig();
  result.message +=
      fmt::format("\nConfig saved to: {}", session.getBgpSessionConfigPath());
  return result.message;
}

void CmdConfigProtocolBgpNeighbor::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdConfigProtocolBgpNeighbor,
    CmdConfigProtocolBgpNeighborTraits>::run();

} // namespace facebook::fboss
