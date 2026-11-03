/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <stdexcept>
#include <utility>

#include "configerator/structs/neteng/fboss/bgp/if/gen-cpp2/bgp_attr_types.h"
#ifdef IS_OSS
#include "fboss/cli/fboss2/oss/NetTools.h"
#else
#include "neteng/fboss/bgp/cpp/lib/BgpStructs.h"
#include "neteng/fboss/bgp/cpp/lib/BgpUtil.h"
#endif

#include "configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/bgp_policy_types.h" // NOLINT(misc-include-cleaner)
#include "configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h"
#include "fboss/agent/AddressUtil.h"
#include "fboss/cli/fboss2/CmdLocalOptions.h"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "folly/IPAddress.h"
#include "folly/Range.h"
#include "folly/String.h"
#include "folly/json.h"
#include "folly/json/dynamic.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"
#include "thrift/lib/cpp/util/EnumUtils.h"
#include "thrift/lib/cpp2/protocol/Serializer.h"

namespace facebook::fboss {
using namespace neteng::fboss::bgp::thrift;
using folly::IPAddress;
using neteng::fboss::bgp_attr::TAsPath;
using neteng::fboss::bgp_attr::TAsPathSegType;
using neteng::fboss::bgp_attr::TBgpAfi;

std::string formatBgpOrigin(int32_t origin) {
  switch (origin) {
    case 0:
      return "BGP_ORIGIN_IGP";
    case 1:
      return "BGP_ORIGIN_EGP";
    case 2:
      return "BGP_ORIGIN_INCOMPLETE";
    default:
      return fmt::format("BGP_ORIGIN_{}", origin);
  }
}

using nettools::bgplib::BgpAttrCommunityC;

using apache::thrift::Client;
using apache::thrift::util::enumNameSafe;
using facebook::fboss::utils::Table;
using facebook::neteng::fboss::bgp::thrift::TBgpAddPathNegotiated;
using facebook::neteng::fboss::bgp::thrift::TBgpService;
using facebook::neteng::fboss::bgp::thrift::TBgpSessionDetail;
using neteng::fboss::bgp::thrift::TBgpPeerState;

LoadedBgpPolicy getLoadedBgpPolicy(const HostInfo& hostInfo) {
  auto client = utils::createClient<Client<TBgpService>>(hostInfo);
  if (client->sync_hasPolicySymlink()) {
    std::string policyConfigJson;
    client->sync_getPolicyConfig(policyConfigJson);
    if (policyConfigJson.empty()) {
      throw std::runtime_error(
          "BGP reports a standalone policy artifact, but getPolicyConfig returned empty");
    }
    return {
        BgpPolicySource::POLICY_ARTIFACT,
        apache::thrift::SimpleJSONSerializer::deserialize<
            facebook::bgp::thrift::BgpPolicyConfig>(policyConfigJson)};
  }

  facebook::bgp::thrift::BgpConfig bgpConfig;
  client->sync_getRunningConfigStruct(bgpConfig);
  facebook::bgp::thrift::BgpPolicyConfig policyConfig;
  policyConfig.communities().copy_from(bgpConfig.communities());
  policyConfig.localprefs().copy_from(bgpConfig.localprefs());
  policyConfig.policies().copy_from(bgpConfig.policies());
  return {BgpPolicySource::BGP_CONFIG, std::move(policyConfig)};
}

// Route-detail views remain best-effort when the artifact RPC is unavailable.
std::optional<facebook::bgp::bgp_policy::BgpPolicies> getRunningBgpPolicies(
    const HostInfo& hostInfo) {
  auto client = utils::createClient<Client<TBgpService>>(hostInfo);
  std::string policyConfigJson;
  try {
    client->sync_getPolicyConfig(policyConfigJson);
    if (!policyConfigJson.empty()) {
      auto policyConfig = apache::thrift::SimpleJSONSerializer::deserialize<
          facebook::bgp::thrift::BgpPolicyConfig>(policyConfigJson);
      return policyConfig.policies().to_optional();
    }
  } catch (const std::exception& ex) {
    XLOGF(DBG2, "Standalone BGP policy config unavailable: {}", ex.what());
  }

  try {
    facebook::bgp::thrift::BgpConfig bgpConfig;
    client->sync_getRunningConfigStruct(bgpConfig);
    return bgpConfig.policies().to_optional();
  } catch (const std::exception& ex) {
    XLOGF(
        WARN, "BGP policy config unavailable from both sources: {}", ex.what());
    return std::nullopt;
  }
}

std::optional<facebook::bgp::nsf_policy::NsfTeWeightEncoding>
getNsfTeWeightEncoding(const facebook::bgp::bgp_policy::BgpPolicies& policies) {
  // A switch's running BGP policy is guaranteed to carry at most one GAR NSF TE
  // weight encoding scheme, so the first supported encoding we encounter is the
  // definitive switch-wide scheme; we can safely return on the first match.
  for (const auto& statement : *policies.bgp_policy_statements()) {
    for (const auto& term : *statement.policy_entries()) {
      for (const auto& action : *term.policy_action_entries()) {
        if (!action.lbw_ext_community_action().has_value() ||
            !action.lbw_ext_community_action()->encoding_scheme().has_value()) {
          continue;
        }
        const auto& encoding =
            *action.lbw_ext_community_action()->encoding_scheme();
        switch (encoding.getType()) {
          case facebook::bgp::nsf_policy::NsfTeWeightEncoding::Type::
              fpf_l2_encoding:
          case facebook::bgp::nsf_policy::NsfTeWeightEncoding::Type::
              l2_encoding:
            return encoding;
          // Intentionally no default: listing every variant makes -Wswitch-enum
          // flag any newly added encoding scheme so this switch is revisited.
          case facebook::bgp::nsf_policy::NsfTeWeightEncoding::Type::__EMPTY__:
            break;
        }
      }
    }
  }
  return std::nullopt;
}

const std::string printCommunities(
    const std::vector<TBgpCommunity>& peer_communities,
    const HostInfo& hostInfo,
    bool isMultiLine) {
  std::vector<std::string> communities;
  communities.reserve(peer_communities.size());
  for (const auto& comm : peer_communities) {
    communities.emplace_back(
        BgpAttrCommunityC::createBgpAttrCommunity(
            folly::to<std::string>(*comm.community()))
            ->to_string());
  }

  auto communitySet = getCommunitySet(hostInfo);
  const auto& communityNames =
      nettools::bgplib::findCommunities(communities, communitySet);

  std::vector<std::string> output;
  for (const auto& [set, alias] : communityNames) {
    if (alias != nettools::bgplib::kNullMessage &&
        folly::StringPiece(alias).startsWith("COMM_")) {
      const std::string name = alias.substr(5); // Trim 'COMM_' from alias name
      output.emplace_back(name + "/" + folly::join(',', set));
    } else {
      const std::string name = alias;
      output.emplace_back(name + "/" + folly::join(',', set));
    }
  }

  return isMultiLine ? folly::join("\n", output) : folly::join(", ", output);
}

float translateLinkBandwidthValue(const uint32_t linkBandwidthValue) {
  // helper function to translate unsigned int value to float
  // background: for two-byte ASN ext community with LINK_BANDWIDTH_SUB_TYPE
  // the value of the community is an unsigned int encoding a float number
  // and this function helps translate that back to float,
  // assuming machine has single prevision binary32 float architecture.
  // Ref: IEEE-754-1985 specification and
  // BgpExtCommunityLinkBandWidthTypeC::getLBW()
  union {
    uint32_t intVal;
    float floatVal;
  } tmp;
  tmp.floatVal = 0.0f; // To avoid lint error
  tmp.intVal = linkBandwidthValue;
  return tmp.floatVal;
}

const std::string printAsPath(const TAsPath& asPath) {
  const std::map<TAsPathSegType, std::string> segFormat = {
      {TAsPathSegType::AS_SEQUENCE, "{}"},
      {TAsPathSegType::AS_CONFED_SEQUENCE, "({})"},
      {TAsPathSegType::AS_SET, "{{{}}}"},
      {TAsPathSegType::AS_CONFED_SET, "({{{}}})"}};

  std::vector<std::string> segments;
  segments.reserve(asPath.size());
  for (const auto& path : asPath) {
    // TODO(michaeluw): fully deprecate asns T113736668
    // Use asns_4_byte if set, otherwise use asns
    std::string asnsStr;
    if (!path.asns_4_byte().value().empty()) {
      asnsStr = folly::join('_', path.asns_4_byte().value());
    } else {
      asnsStr = folly::join('_', path.asns().value());
    }
    segments.emplace_back(
        fmt::format(
            fmt::runtime(segFormat.at(folly::copy(path.seg_type().value()))),
            std::move(asnsStr)));
  }
  return folly::join('_', segments);
}

const std::string printLocalPrefs(
    int localPref,
    const std::map<int, std::string>& mnemonics) {
  if (nettools::bgplib::isAristaDevice()) {
    return folly::to<std::string>(localPref);
  }

  std::string prefName = nettools::bgplib::kNullMessage;
  const auto& prefMnemonic = mnemonics.find(localPref);
  if (prefMnemonic != mnemonics.end()) {
    // Strip away LOCALPREF_ from the mnemonic name
    static const std::string kLocalPref = "LOCALPREF_";
    if (folly::StringPiece(prefMnemonic->second).startsWith(kLocalPref)) {
      prefName = prefMnemonic->second.substr(kLocalPref.size());
    } else {
      prefName = prefMnemonic->second;
    }
  }
  return fmt::format("{}/{}", prefName, localPref);
}

const std::string printClusterList(const std::vector<long>& clusterList) {
  std::vector<std::string> clusters;
  clusters.reserve(clusterList.size());
  for (const auto& cluster : clusterList) {
    clusters.emplace_back(IPAddress::fromLongHBO(cluster).str());
  }
  return folly::join(',', clusters);
}

namespace {
// Static state for mnemonic caching - defined here to be accessible by both
// functions
struct MnemonicCache {
  bool firstTimeRetreivingMnemonics = true;
  std::map<int, std::string> prefsMnemonics;
  bool firstTimeGettingCommunities = true;
  std::map<std::set<std::string>, std::string> communitySetMap;
};

MnemonicCache& getMnemonicCache() {
  static MnemonicCache cache;
  return cache;
}
} // namespace

std::map<std::set<std::string>, std::string> getCommunitySet(
    const HostInfo& hostInfo) {
  auto& cache = getMnemonicCache();

  if (!cache.firstTimeGettingCommunities) {
    return cache.communitySetMap;
  }

  cache.firstTimeGettingCommunities = false;
  const auto bgp_mnemonics = getLocalBgpConfig(hostInfo);
  if (bgp_mnemonics.find("communities") == bgp_mnemonics.items().end()) {
    return cache.communitySetMap;
  }
  for (const auto& mnemonic : bgp_mnemonics.at("communities")) {
    std::set<std::string> communities;
    if (mnemonic.find("communities") == mnemonic.items().end()) {
      continue;
    }
    for (const auto& community : mnemonic.at("communities")) {
      communities.insert(community.asString());
    }
    if (mnemonic.find("name") == mnemonic.items().end()) {
      cache.communitySetMap[communities] = nettools::bgplib::kNullMessage;
    } else {
      cache.communitySetMap[communities] = mnemonic.at("name").asString();
    }
  }
  return cache.communitySetMap;
}

folly::dynamic getLocalBgpConfig(const HostInfo& hostInfo) {
  auto client =
      utils::createClient<apache::thrift::Client<TBgpService>>(hostInfo);
  std::string config;
  client->sync_getRunningConfig(config);
  return folly::parseJson(config);
}

const std::map<int, std::string> getLocalPrefMnemonics(
    const HostInfo& hostInfo) {
  auto& cache = getMnemonicCache();

  if (!cache.firstTimeRetreivingMnemonics) {
    return cache.prefsMnemonics;
  }

  cache.firstTimeRetreivingMnemonics = false;
  const auto bgp_mnemonics = getLocalBgpConfig(hostInfo);

  if (bgp_mnemonics.empty()) {
    return cache.prefsMnemonics;
  }

  if (bgp_mnemonics.find("localprefs") == bgp_mnemonics.items().end()) {
    return cache.prefsMnemonics;
  }
  for (const auto& mnemonic : bgp_mnemonics.at("localprefs")) {
    if (mnemonic.find("localpref") == mnemonic.items().end()) {
      continue;
    }
    cache.prefsMnemonics[mnemonic.at("localpref").asInt()] =
        mnemonic.at("name").asString();
  }
  return cache.prefsMnemonics;
}

void resetBgpMnemonicCaches() {
  auto& cache = getMnemonicCache();
  cache.firstTimeRetreivingMnemonics = true;
  cache.prefsMnemonics.clear();
  cache.firstTimeGettingCommunities = true;
  cache.communitySetMap.clear();
}

TIpPrefix sampleIpPrefix(const std::string& cidr) {
  const auto slash = cidr.find_last_of('/');
  const auto address = (slash == std::string::npos)
      ? IPAddress(cidr)
      : IPAddress(cidr.substr(0, slash));

  TIpPrefix prefix;
  prefix.prefix_bin() =
      facebook::network::toBinaryAddress(address).addr().value().toStdString();
  prefix.num_bits() = (slash != std::string::npos)
      ? folly::to<int>(cidr.substr(slash + 1))
      : (address.isV4() ? 32 : 128);
  prefix.afi() = address.isV4() ? TBgpAfi::AFI_IPV4 : TBgpAfi::AFI_IPV6;
  return prefix;
}

TBgpCommunity sampleCommunity(const SampleCommunity& community) {
  return sampleCommunity(community.asn, community.value);
}

TBgpCommunity sampleCommunity(uint16_t asn, uint16_t value) {
  TBgpCommunity community;
  community.asn() = asn;
  community.value() = value;
  community.community() =
      static_cast<int64_t>((static_cast<uint32_t>(asn) << 16) | value);
  return community;
}

std::string formatBgpIpAddress(const TIpPrefix& prefix) {
  const auto address = IPAddress::tryFromBinary(
      folly::ByteRange(folly::StringPiece(*prefix.prefix_bin())));
  return address.hasValue() ? address->str() : "<invalid>";
}

void printFibOut(std::ostream& out, const TRibEntry& entry) {
  if (!entry.fib_out().has_value()) {
    return;
  }

  const auto& fibOut = *entry.fib_out();
  out << fmt::format(
      "  FIB-out: {}", apache::thrift::util::enumNameSafe(*fibOut.operation()));
  if (fibOut.nexthop_set_ref_count().has_value()) {
    out << fmt::format(
        " | Route References: {}", *fibOut.nexthop_set_ref_count());
  }
  if (fibOut.admin_distance().has_value()) {
    out << fmt::format(" | Admin Distance: {}", *fibOut.admin_distance());
  }
  if (fibOut.class_id().has_value()) {
    out << fmt::format(" | Class ID: {}", *fibOut.class_id());
  }
  out << '\n';

  for (const auto& nextHop : *fibOut.next_hops()) {
    out << fmt::format(
        "    {} | Weight: {} | Role: {}",
        formatBgpIpAddress(*nextHop.next_hop()),
        *nextHop.weight(),
        apache::thrift::util::enumNameSafe(*nextHop.role()));
    if (nextHop.is_connected().has_value()) {
      out << fmt::format(
          " | Connected: {}", *nextHop.is_connected() ? "true" : "false");
    }
    if (nextHop.interface_name().has_value()) {
      out << fmt::format(" | Interface: {}", *nextHop.interface_name());
    }
    if (nextHop.topology_info().has_value()) {
      std::vector<std::pair<std::string, int64_t>> topology(
          nextHop.topology_info()->begin(), nextHop.topology_info()->end());
      std::sort(topology.begin(), topology.end());
      out << " | Topology:";
      for (const auto& [name, value] : topology) {
        out << fmt::format(" {}={}", name, value);
      }
    }
    out << '\n';
  }
}

namespace {

struct SamplePathSpec {
  std::string nextHop;
  std::string peer;
  std::string peerDescription;
  int32_t localPref;
  // The advertising router, rendered as the Router/Originator line by
  // 'table detail'. Must be IPv4: router IDs are 32-bit, and sampleBgpPath
  // throws on anything else.
  std::string routerAddress;
  bool isBestPath;
  // Only the rejected ECMP path carries one; 'table detail' prints it as the
  // BestPath Rejection Reason.
  std::string bestPathFilterDescription;
};

TBgpPath sampleBgpPath(const SamplePathSpec& spec) {
  TBgpPath path;
  path.next_hop() = sampleIpPrefix(spec.nextHop);
  path.peer_id() = sampleIpPrefix(spec.peer);
  path.peer_description() = spec.peerDescription;
  path.local_pref() = spec.localPref;
  // printRIBEntries reads router_id back with IPAddress::fromLongHBO, so store
  // host byte order.
  path.router_id() =
      static_cast<int32_t>(IPAddress(spec.routerAddress).asV4().toLongHBO());
  // BGP_ORIGIN_IGP; printRIBEntries trims the enum prefix and renders "IGP".
  path.origin() = 0;
  path.is_best_path() = spec.isBestPath;
  if (!spec.bestPathFilterDescription.empty()) {
    path.bestpath_filter_descr() = spec.bestPathFilterDescription;
  }
  // Microseconds; printRIBEntries renders LM as the time elapsed since then,
  // so this drifts with wall clock rather than being a fixed string.
  path.last_modified_time() = 1788455780000000;
  path.next_hop_weight() = 0;
  path.med() = 0;
  path.path_id() = 0;
  path.path_id_to_send() = 4;
  path.weight() = 0;

  neteng::fboss::bgp_attr::TAsPathSeg segment;
  segment.seg_type() = TAsPathSegType::AS_SEQUENCE;
  // Sample ASNs (RFC 6996 private-use range). These samples are published to
  // the CLI reference wiki and ship in the open-source tree, so they must not
  // carry real AS numbers or community assignments.
  segment.asns() = {64712, 65017, 64961, 65108};
  path.as_path() = TAsPath{segment};

  // The community a default route carries; the 'show bgp config' sample
  // defines the matching AS65108.DEFAULT mnemonic for it.
  path.communities() = {sampleCommunity(kSampleDefaultCommunity)};

  return path;
}

} // namespace

TRibEntryWithHost sampleRibEntriesWithHost() {
  TRibEntry defaultRoute;
  defaultRoute.prefix() = sampleIpPrefix("0.0.0.0/0");
  defaultRoute.best_group() = "best";
  defaultRoute.best_next_hop() = sampleIpPrefix("192.0.2.11");
  defaultRoute.paths() = {
      {"best",
       {sampleBgpPath(
            {.nextHop = "192.0.2.11",
             .peer = "192.0.2.11",
             .peerDescription = "fsw001.p001.f01.abc1",
             .localPref = 100,
             .routerAddress = "192.0.2.101",
             .isBestPath = true,
             .bestPathFilterDescription = ""}),
        sampleBgpPath(
            {.nextHop = "192.0.2.12",
             .peer = "192.0.2.12",
             .peerDescription = "fsw002.p001.f01.abc1",
             .localPref = 100,
             .routerAddress = "192.0.2.102",
             .isBestPath = false,
             .bestPathFilterDescription =
                 "Router-Id, Filter Criterion: Choose Lowest Value"})}},
      // A drained peer advertises the same prefix at a lower local-pref, so it
      // lands outside the best group and renders without the ECMP marker.
      {"warm",
       {sampleBgpPath(
           {.nextHop = "192.0.2.13",
            .peer = "192.0.2.13",
            .peerDescription = "fsw003.p001.f01.abc1",
            .localPref = 20,
            .routerAddress = "192.0.2.103",
            .isBestPath = false,
            .bestPathFilterDescription = ""})}}};
  TRibEntry v6Prefix;
  v6Prefix.prefix() = sampleIpPrefix("2001:db8:1c00::/40");
  v6Prefix.best_group() = "best";
  v6Prefix.best_next_hop() = sampleIpPrefix("2001:db8:e11e:1062::4e");
  v6Prefix.paths() = {
      {"best",
       {sampleBgpPath(
           {.nextHop = "2001:db8:e11e:1062::4e",
            .peer = "2001:db8:e11e:1062::4e",
            .peerDescription = "fsw001.p001.f01.abc1",
            .localPref = 100,
            .routerAddress = "192.0.2.101",
            .isBestPath = true,
            .bestPathFilterDescription = ""})}}};
  TRibEntryWithHost data;
  data.tRibEntries() = {defaultRoute, v6Prefix};
  data.host() = "rsw001.p001.f01.abc1";
  data.oobName() = "rsw001.p001.f01.abc1.oob";
  data.ip() = "192.0.2.1";
  return data;
}

TFibOutTable sampleFibOutTable() {
  TFibOutRoute defaultRoute;
  defaultRoute.operation() = TFibOutOperation::PROGRAM;
  defaultRoute.admin_distance() = 20;
  for (const auto& address : {"192.0.2.11", "192.0.2.12"}) {
    TFibOutNextHop nextHop;
    nextHop.next_hop() = sampleIpPrefix(address);
    nextHop.weight() = 0;
    nextHop.role() = TFibOutNextHopRole::PRIMARY;
    defaultRoute.next_hops()->emplace_back(std::move(nextHop));
  }
  defaultRoute.nexthop_set_ref_count() = 1;
  TFibOutEntry defaultEntry;
  defaultEntry.prefix() = sampleIpPrefix("0.0.0.0/0");
  defaultEntry.fib_out() = std::move(defaultRoute);

  TFibOutNextHop v6NextHop;
  v6NextHop.next_hop() = sampleIpPrefix("2001:db8:e11e:1062::4e");
  v6NextHop.weight() = 0;
  v6NextHop.role() = TFibOutNextHopRole::PRIMARY;
  TFibOutRoute v6Route;
  v6Route.operation() = TFibOutOperation::PROGRAM;
  v6Route.next_hops() = {std::move(v6NextHop)};
  v6Route.admin_distance() = 20;
  v6Route.nexthop_set_ref_count() = 1;
  TFibOutEntry v6Entry;
  v6Entry.prefix() = sampleIpPrefix("2001:db8:1c00::/40");
  v6Entry.fib_out() = std::move(v6Route);

  TFibOutTable table;
  table.enabled() = true;
  table.entries() = {std::move(defaultEntry), std::move(v6Entry)};
  return table;
}

NetworkPathWithHost sampleNetworkPaths(
    SampleRouteDirection direction,
    const std::string& policyName) {
  const bool received = direction == SampleRouteDirection::Received;

  auto path = [&](const std::string& nextHop,
                  int64_t peerAsn,
                  int32_t localPref,
                  const std::vector<TBgpCommunity>& communities) {
    TBgpPath bgpPath;
    bgpPath.next_hop() = sampleIpPrefix(nextHop);
    bgpPath.local_pref() = localPref;
    // BGP_ORIGIN_IGP; the renderer trims the enum prefix and prints "IGP".
    bgpPath.origin() = 0;
    bgpPath.med() = 0;
    bgpPath.communities() = communities;
    if (!policyName.empty()) {
      bgpPath.policy_name() = policyName;
    }
    /*
     * Microseconds. Advertised routes are rendered straight out of the
     * AdjRibOut and carry no last-modified time, so the view prints
     * "LastModified: Not set"; received ones carry the time the path arrived.
     */
    if (received) {
      bgpPath.last_modified_time() = 1788455780000000;
    }

    neteng::fboss::bgp_attr::TAsPathSeg segment;
    segment.seg_type() = TAsPathSegType::AS_CONFED_SEQUENCE;
    // printAsPath() prefers asns_4_byte and only falls back to the legacy
    // asns field, so populate the one the renderer actually reads.
    segment.asns_4_byte() = {peerAsn};
    bgpPath.as_path() = TAsPath{segment};
    return bgpPath;
  };

  /*
   * The confederation ASNs either side of the session: the peer we receive
   * from and the peer we advertise to are in different sub-ASes, which is
   * what makes the two directions distinguishable in the rendered AS path.
   */
  constexpr int64_t kUpstreamAsn = 65221;
  constexpr int64_t kDownstreamAsn = 64650;

  // LIVE, plus the community that identifies the prefix class.
  const std::vector<TBgpCommunity> aggregateCommunities = {
      sampleCommunity(kSampleLiveCommunity),
      sampleCommunity(kSampleAggregateCommunity)};
  const std::vector<TBgpCommunity> defaultRouteCommunities = {
      sampleCommunity(kSampleLiveCommunity),
      sampleCommunity(kSampleDefaultCommunity)};

  NetworkPathWithHost result;
  if (received) {
    // What the peer sends us: a default route and a more specific.
    result.networkPath() = {
        {sampleIpPrefix("0.0.0.0/0"),
         {path("192.0.2.11", kUpstreamAsn, 100, defaultRouteCommunities)}},
        {sampleIpPrefix("203.0.113.7/32"),
         {path("192.0.2.11", kUpstreamAsn, 90, aggregateCommunities)}}};
  } else {
    // What we send the peer: two rack aggregates.
    result.networkPath() = {
        {sampleIpPrefix("198.51.100.0/24"),
         {path("192.0.2.12", kDownstreamAsn, 100, aggregateCommunities)}},
        {sampleIpPrefix("203.0.113.0/24"),
         {path("192.0.2.12", kDownstreamAsn, 100, aggregateCommunities)}}};
  }
  result.host() = "rsw001.p001.f01.abc1";
  result.oobName() = "rsw001.p001.f01.abc1.oob";
  result.ip() = "192.0.2.1";
  return result;
}

const std::vector<std::string> printRoutesInformation(
    const std::map<TIpPrefix, std::vector<TBgpPath>>& networks,
    const HostInfo& hostInfo,
    bool showPolicy) {
  std::vector<std::string> routes;
  const auto& prefMnemonics = getLocalPrefMnemonics(hostInfo);
  for (const auto& [ipPrefix, bgpPaths] : networks) {
    for (const auto& path : bgpPaths) {
      const auto ip_address = IPAddress::fromBinary(
          folly::ByteRange(folly::StringPiece(ipPrefix.prefix_bin().value())));
      const auto prefix = fmt::format(
          "{}/{}", ip_address.str(), folly::copy(ipPrefix.num_bits().value()));

      std::string nextHop;
      if (path.next_hop().value().prefix_bin().value().empty()) {
        nextHop = (folly::copy(ipPrefix.afi().value()) == TBgpAfi::AFI_IPV4)
            ? "0.0.0.0"
            : "::";
      } else {
        nextHop = IPAddress::fromBinary(
                      folly::ByteRange(
                          folly::StringPiece(
                              path.next_hop().value().prefix_bin().value())))
                      .str();
      }

      std::string routerOriginatorId = "  --  ";
      if (const auto& originatorId =
              apache::thrift::get_pointer(path.originator_id())) {
        routerOriginatorId = IPAddress::fromLongHBO(*originatorId).str();
      } else if (
          const auto& routerId =
              apache::thrift::get_pointer(path.router_id())) {
        routerOriginatorId = IPAddress::fromLongHBO(*routerId).str();
      }

      const auto& clusterList =
          apache::thrift::get_pointer(path.cluster_list());
      std::string clusterListStr = (clusterList && !clusterList->empty())
          ? fmt::format("[{}]", printClusterList(*clusterList))
          : "[]";

      const std::string communitiesStr =
          (apache::thrift::get_pointer(path.communities()))
          ? printCommunities(
                *apache::thrift::get_pointer(path.communities()), hostInfo)
          : "";
      const std::string extCommunitiesStr =
          (apache::thrift::get_pointer(path.extCommunities()))
          ? printExtCommunities(
                *apache::thrift::get_pointer(path.extCommunities()))
          : "";
      const std::string localPrefsStr =
          (apache::thrift::get_pointer(path.local_pref()))
          ? printLocalPrefs(
                *apache::thrift::get_pointer(path.local_pref()), prefMnemonics)
          : "";
      const std::string originStr = (apache::thrift::get_pointer(path.origin()))
          ? formatBgpOrigin(*apache::thrift::get_pointer(path.origin()))
          : nettools::bgplib::kNullMessage;

      std::string routeInformation = fmt::format(
          "---\n"
          "Network: {}\n"
          "Nexthop: {}\n"
          "Router/OriginatorId: {}\n"
          "ClusterList: {}\n"
          "Communities: {}\n"
          "ExtCommunities: {}\n"
          "AsPath: {}\n"
          "LocalPref: {}\n"
          "Origin: {}\n"
          "MED: {}\n"
          "LastModified: {}",
          prefix,
          nextHop,
          routerOriginatorId.data(),
          clusterListStr,
          communitiesStr,
          extCommunitiesStr,
          printAsPath(path.as_path().value()),
          localPrefsStr,
          (originStr != nettools::bgplib::kNullMessage)
              ? originStr.substr(11) // Trim BGP_ORIGIN_ from origin string
              : originStr,
          path.med().has_value()
              ? std::to_string(*path.med())
              : (path.multi_exit_disc().has_value()
                     ? std::to_string(*path.multi_exit_disc())
                     : "Not set"),
          (folly::copy(path.last_modified_time().value()))
              ? utils::parseTimeToTimeStamp(
                    folly::copy(path.last_modified_time().value()) / 1000)
              : "Not set");

      if (showPolicy) {
        std::string policyStr = "Not set";
        if (apache::thrift::get_pointer(path.policy_name()) &&
            !apache::thrift::get_pointer(path.policy_name())->empty()) {
          policyStr = *apache::thrift::get_pointer(path.policy_name());
        }
        routeInformation += fmt::format("\nPolicy: {}", policyStr);
      }
      routes.emplace_back(routeInformation);
    }
  }
  return routes;
}

void printBgpCapabilities(const TBgpSessionDetail& details, std::ostream& out) {
  out << "  Neighbor Capabilities:" << std::endl;
  if (folly::copy(details.ipv4_unicast().value())) {
    out << "    Multiprotocol IPv4 Unicast: negotiated" << std::endl;
  }
  if (folly::copy(details.ipv6_unicast().value())) {
    out << "    Multiprotocol IPv6 Unicast: negotiated" << std::endl;
  }
  if (folly::copy(details.legacy_v4_nlri_encoding().value())) {
    out << "    Legacy v4 NLRI encoding (RFC 4271 classic NLRI + NEXT_HOP): yes"
        << std::endl;
  }
  if (folly::copy(details.rr_client().value())) {
    out << "    Route Refresh: advertised" << std::endl;
  }
  if (const auto remoteRestartTime =
          folly::copy(details.gr_remote_restart_time().value())) {
    out << "    Graceful Restart: received, peer restart-time is "
        << remoteRestartTime << "s" << std::endl;
  } else {
    out << "    Graceful Restart: not received" << std::endl;
  }
  if (const auto restartTime = folly::copy(details.gr_restart_time().value())) {
    out << "    Graceful Restart: sent, local restart-time is " << restartTime
        << "s" << std::endl;
  } else {
    out << "    Graceful Restart: NOT sent" << std::endl;
  }
}

void printBgpPrefixTelemetry(
    const TBgpSession& neighbor,
    const TBgpSessionDetail& details,
    std::ostream& out) {
  // Prefix-level telemetry: the current advertised/received prefix gauges plus
  // the cumulative announcement/withdrawal counts (per AFI where tracked).
  // "Accepted" applies only to the current-gauge row; cumulative rows show "-".
  const std::string kNa = "-";
  Table table;
  table.setHeader({"Prefix Telemetry", "Sent", "Rcvd", "Accepted"});
  table.addRow(
      {"Current prefixes",
       folly::to<std::string>(*neighbor.postpolicy_sent_prefix_count()),
       folly::to<std::string>(*neighbor.prepolicy_rcvd_prefix_count()),
       folly::to<std::string>(*neighbor.postpolicy_rcvd_prefix_count())});
  table.addRow(
      {"Announcements IPv4",
       folly::to<std::string>(*details.sent_update_announcements_ipv4()),
       folly::to<std::string>(*details.recv_update_announcements_ipv4()),
       kNa});
  table.addRow(
      {"Announcements IPv6",
       folly::to<std::string>(*details.sent_update_announcements_ipv6()),
       folly::to<std::string>(*details.recv_update_announcements_ipv6()),
       kNa});
  table.addRow(
      {"Withdrawals",
       folly::to<std::string>(*details.sent_update_withdrawals()),
       folly::to<std::string>(*details.recv_update_withdrawals()),
       kNa});
  out << table << std::endl;
}

void printBgpMessageCounters(
    const TBgpSessionDetail& details,
    std::ostream& out) {
  // Per-message-type PDU counters for both layers: data-plane (socket, the
  // SessionManager I/O thread) and control-plane (AdjRib / PeerManager). They
  // converge for cross-module validation, and `clear bgp egress-counters` /
  // `clear bgp ingress-counters` zero them for debugging. AdjRib only
  // produces/consumes UPDATE and EoR PDUs, so the other types show "-" there.

  // Only render when there is something to show, so idle peers stay
  // uncluttered. A clear zeroes one direction at a time, so the other
  // direction's non-zero counters keep the table visible for verifying the
  // clear.
  const bool anyCounter = *details.socket_tx_open_msgs() ||
      *details.socket_tx_update_msgs() || *details.socket_tx_keepalive_msgs() ||
      *details.socket_tx_notification_msgs() ||
      *details.socket_tx_route_refresh_msgs() ||
      *details.socket_tx_eor_msgs() || *details.socket_rx_open_msgs() ||
      *details.socket_rx_update_msgs() || *details.socket_rx_keepalive_msgs() ||
      *details.socket_rx_notification_msgs() ||
      *details.socket_rx_route_refresh_msgs() ||
      *details.socket_rx_eor_msgs() || *details.adjrib_sent_update_msgs() ||
      *details.adjrib_sent_eor_msgs() || *details.adjrib_recv_update_msgs() ||
      *details.adjrib_recv_eor_msgs();
  if (!anyCounter) {
    return;
  }

  const std::string kNa = "-";
  Table table;
  table.setHeader(
      {"Message Type", "Socket Tx", "Socket Rx", "AdjRib Sent", "AdjRib Recv"});
  table.addRow(
      {"open",
       folly::to<std::string>(*details.socket_tx_open_msgs()),
       folly::to<std::string>(*details.socket_rx_open_msgs()),
       kNa,
       kNa});
  table.addRow(
      {"update",
       folly::to<std::string>(*details.socket_tx_update_msgs()),
       folly::to<std::string>(*details.socket_rx_update_msgs()),
       folly::to<std::string>(*details.adjrib_sent_update_msgs()),
       folly::to<std::string>(*details.adjrib_recv_update_msgs())});
  table.addRow(
      {"keepalive",
       folly::to<std::string>(*details.socket_tx_keepalive_msgs()),
       folly::to<std::string>(*details.socket_rx_keepalive_msgs()),
       kNa,
       kNa});
  table.addRow(
      {"notification",
       folly::to<std::string>(*details.socket_tx_notification_msgs()),
       folly::to<std::string>(*details.socket_rx_notification_msgs()),
       kNa,
       kNa});
  table.addRow(
      {"route-refresh",
       folly::to<std::string>(*details.socket_tx_route_refresh_msgs()),
       folly::to<std::string>(*details.socket_rx_route_refresh_msgs()),
       kNa,
       kNa});
  table.addRow(
      {"end-of-rib",
       folly::to<std::string>(*details.socket_tx_eor_msgs()),
       folly::to<std::string>(*details.socket_rx_eor_msgs()),
       folly::to<std::string>(*details.adjrib_sent_eor_msgs()),
       folly::to<std::string>(*details.adjrib_recv_eor_msgs())});
  out << table << std::endl;
}

void printAddPathCapability(
    const std::vector<TBgpAddPathNegotiated>& capabilities,
    std::ostream& out) {
  std::vector<std::string> negotiations;
  negotiations.reserve(capabilities.size());
  for (const auto& capability : capabilities) {
    const auto afi = enumNameSafe(folly::copy(capability.afi().value()));
    const auto addPathCapability =
        enumNameSafe(folly::copy(capability.add_path().value()));
    negotiations.emplace_back(
        fmt::format("{} {}", afi, addPathCapability).data());
  }
  if (!negotiations.empty()) {
    out << ", Negotiated - " << folly::join(", ", negotiations) << std::endl;
  }
}

void printRouteHeader(std::string& out, bool detailed) {
  // Helper function to construct print lines of header for advertised and
  // received rooutes. Add marker information
  out =
      "Markers: * - Best entries (used for forwarding), @ - Entry used to advertise across area\n";
  if (!detailed) {
    out +=
        "Acronyms: SP - Source Preference, PP - Path Preference, D - Distance\n"
        "          MN - Min-Nexthops\n";
  }
}

const std::string formatBytes(size_t n) {
  const std::vector<std::string> suffix{"B", "KB", "MB", "GB"};
  size_t i = 0;
  double out = n;
  for (i = 0; i < 4 && n >= 1024; i++) {
    out = n / 1024.0;
    n /= 1024;
  }
  out = (static_cast<int>(out * 100.0)) / 100.0;
  return fmt::format("{} {}", out, suffix[i]);
}

timeval splitFractionalSecondsFromTimer(const long timer) {
  constexpr int kOneSecond = 1000;
  struct timeval tv;
  tv.tv_sec = timer / kOneSecond; // get total amount of seconds
  tv.tv_usec = (timer % kOneSecond); // get fractional seconds

  tv.tv_usec = lrint(tv.tv_usec); // round fs to nearest decimal
  return tv;
}

void printTimestamp(
    const long timeToParse,
    const std::string& outputMessage,
    std::ostream& out) {
  constexpr std::string_view kTimeFormat = "%Y-%m-%d %H:%M:%S %Z";
  const auto splitTime = splitFractionalSecondsFromTimer(timeToParse);
  std::string formattedTime; // Timestamp storage
  stringFormatTimestamp(splitTime.tv_sec, kTimeFormat.data(), &formattedTime);

  // stringFormatTimestamp returns a formatted time like the following:
  //      2021-10-27 00:00:06 PDT
  //
  // In order to add the fractional seconds, we find the last space before the
  // timestamp's timezone and concatenate the fractional seconds.
  //      2021-10-27 00:00:06 PDT -> 2021-10-27 00:00:06.475 PDT
  int index = formattedTime.find_last_of(' ');
  out << outputMessage
      << fmt::format(
             " {}.{} {}",
             formattedTime.substr(0, index), // Date and time
             splitTime.tv_usec, // fractional seconds
             formattedTime.substr(index + 1)) // timezone
      << std::endl;
}

std::map<TIpPrefix, std::vector<TBgpPath>> filterNetworks(
    const std::map<TIpPrefix, std::vector<TBgpPath>>& networks,
    const std::vector<std::string>& prefixes) {
  std::vector<TIpPrefix> prefixList;
  for (const auto& ip : prefixes) {
    // Split the Ip Address and subnet mask
    std::size_t maskIndex = ip.find_last_of('/');
    const auto address = folly::IPAddress(ip.substr(0, maskIndex));
    const auto binaryAddress = facebook::network::toBinaryAddress(address);

    TIpPrefix ipPrefix;
    ipPrefix.prefix_bin() = binaryAddress.addr().value().toStdString();
    ipPrefix.num_bits() = folly::to<int>(ip.substr(maskIndex + 1));
    ipPrefix.afi() = (address.isV4()) ? TBgpAfi::AFI_IPV4 : TBgpAfi::AFI_IPV6;

    prefixList.emplace_back(ipPrefix);
  }

  std::map<TIpPrefix, std::vector<TBgpPath>> filteredNetworks;
  for (const auto& prefix : prefixList) {
    if (const auto& it = networks.find(prefix); it != networks.end()) {
      filteredNetworks.try_emplace(it->first, it->second);
    }
  }
  return filteredNetworks;
}

const std::map<TIpPrefix, std::vector<TBgpPath>> getRejectedNetworks(
    const std::map<TIpPrefix, std::vector<TBgpPath>>& receivedNetworks,
    const std::map<TIpPrefix, std::vector<TBgpPath>>& acceptedNetworks) {
  std::map<TIpPrefix, std::vector<TBgpPath>> rejectedNetworks;
  for (const auto& [route, path] : receivedNetworks) {
    if (acceptedNetworks.find(route) == acceptedNetworks.end()) {
      rejectedNetworks.try_emplace(route, path);
    }
  }
  return rejectedNetworks;
}

const std::map<TIpPrefix, std::vector<TBgpPath>> vectorizePaths(
    const std::map<TIpPrefix, TBgpPath>& networks) {
  std::map<TIpPrefix, std::vector<TBgpPath>> vectorizedNetworks;
  for (const auto& [route, path] : networks) {
    std::vector<TBgpPath> vectorizedPath = {path};
    vectorizedNetworks.try_emplace(route, std::move(vectorizedPath));
  }
  return vectorizedNetworks;
}

void sortEntries(std::vector<TRibEntry>& entries) {
  // Sort the entries by their prefix
  std::sort(
      entries.begin(),
      entries.end(),
      [](const TRibEntry& entryA, const TRibEntry& entryB) {
        return entryA.prefix().value() < entryB.prefix().value();
      });

  for (auto& entry : entries) {
    for (auto& [group, path] : *entry.paths()) {
      // Sort RIB entries by nexthop
      std::sort(
          path.begin(),
          path.end(),
          [](const TBgpPath& pathA, const TBgpPath& pathB) {
            return pathA.next_hop().value() < pathB.next_hop().value();
          });
    }
  }
}

float getLinkBandwidthFromExtCommunities(
    const std::vector<TBgpExtCommunity>& extCommunities) {
  for (const auto& comm : extCommunities) {
    const auto& commUnion = comm.u().value();

    if (commUnion.getType() != TBgpExtCommUnion::Type::two_byte_asn) {
      // not a two-byte ASN
      continue;
    }
    const auto& asn = commUnion.two_byte_asn().value();

    if (folly::copy(asn.type().value()) !=
        static_cast<int>(TBgpTwoByteAsnExtCommType::LINK_BANDWIDTH_TYPE)) {
      // not a link bandwidth value
      continue;
    }

    if ((folly::copy(asn.sub_type().value()) ==
         static_cast<int>(
             TBgpTwoByteAsnExtCommSubType::LINK_BANDWIDTH_SUB_TYPE))) {
      return translateLinkBandwidthValue(folly::copy(asn.value().value()));
    }
  }
  return -1.0f;
}

bool hasBestPath(const TRibEntry& entry) {
  for (const auto& [group, paths] : entry.paths().value()) {
    for (const auto& path : paths) {
      if (path.is_best_path().value_or(false /* default */)) {
        return true;
      }
    }
  }
  return false;
}

bool isValidCommunity(std::string_view communityName) {
  const size_t delimiter = communityName.find(':');
  if (delimiter != std::string::npos) {
    const auto& it = communityName.begin();
    return std::all_of(it, it + delimiter - 1, ::isdigit) &&
        std::all_of(it + delimiter + 1, communityName.end(), ::isdigit);
  }
  return false;
}

const std::vector<std::string> communityNameToValue(
    const std::string& communityName,
    const HostInfo& hostInfo) {
  static const auto& bgpMnemonics = getLocalBgpConfig(hostInfo);

  // First Case: The community name comes in format 'asn:value'.
  // if so, we need to make sure the values are valid (integers).
  if (isValidCommunity(communityName)) {
    return {communityName};
  } else {
    // Second Case: The community name is sometimes prefixed with
    // COMM_.
    // example: COMM_ADMIT_CONTROLLER or
    //          ADMIT_CONTROLLER
    // If so we need to make sure that the name is present on bgp mnemonics
    // to return the right community set values.
    std::vector<std::string> communitySet;
    const auto& communities = bgpMnemonics.at("communities");
    const std::string commNamePrefix = "COMM_";
    for (const auto& community : communities) {
      if (community["name"] == communityName ||
          community["name"] ==
              fmt::format("{}{}", commNamePrefix, communityName)) {
        for (const auto& curCommunity : community["communities"]) {
          communitySet.emplace_back(curCommunity.asString());
        }
        return communitySet;
      }
    }
  }
  throw std::invalid_argument(
      fmt::format("Unknown Community Name: {}", communityName));
}

const std::string communityToString(const TBgpCommunity& community) {
  return fmt::format(
      "{}:{}",
      folly::copy(community.asn().value()),
      folly::copy(community.value().value()));
}

void stringFormatTimestamp(
    time_t timestamp,
    const char* format,
    std::string* output) {
  std::tm tm_info;
  localtime_r(&timestamp, &tm_info);
  std::ostringstream oss;
  oss << std::put_time(&tm_info, format);
  *output = oss.str();
}

} // namespace facebook::fboss
