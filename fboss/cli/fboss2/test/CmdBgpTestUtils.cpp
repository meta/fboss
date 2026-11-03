/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/test/CmdBgpTestUtils.h"
#ifndef IS_OSS
#include "configerator/distribution/api/ScopedConfigeratorFake.h"
#endif
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

#include <boost/algorithm/string.hpp> // NOLINT(misc-include-cleaner)
#include <netinet/in.h>
#include <chrono> // NOLINT(misc-include-cleaner)
#include <initializer_list>
#include <map>
#include <optional>

#include <fboss/agent/AddressUtil.h>
#include <folly/IPAddress.h>
#include <string>
#include <utility>
#include <vector>

namespace facebook::fboss {
using namespace std::chrono;
using namespace neteng::fboss::bgp::thrift;
using namespace neteng::fboss::bgp_attr;
#ifndef IS_OSS
using configerator::ScopedConfigeratorFake;
#endif

const std::string kBestGroupName = "bestGroupName";
const std::string kDefaultGroupName = "defaultGroupName";
const std::string kIpAddress = "8.0.0.0/32";
const std::string kPeerId = "1.2.3.4";
const std::string kPeerDescription = "one.two.three.four";
const std::string kNextHop = "8.0.0.1";
// Mirrors kRibEntryMarkersLegend in commands/show/bgp/CmdShowUtils.h.
// Kept as a literal so this small test-utils target need not depend on the
// full CLI command library; the exact-match golden tests catch any drift.
const std::string kRibEntryMarkersHeader =
    "Markers: * - One of the best entries, @ - Best entry, % - Pending selection, ! - Inactive path\n"
    "Acronyms: ASP - AS Path, LP - Local Preference, LBW - Link Bandwidth, LM - Last Modified\n";

// Common BGP peer state constants
const TBgpPeerState kEstablishedPeerState = TBgpPeerState::ESTABLISHED;
const TBgpPeerState kIdlePeerState = TBgpPeerState::IDLE;

// Common established session constants
const int kEstablishedRemoteAS = 1;
const std::string_view kEstablishedPeerAddress = "1.2.3.4";
const std::string_view kEstablishedRemoteBgpId = "4.3.2.1";
const std::string_view kEstablishedDescription = "leaf01.p099.f01.lab0";
const int kUptime = 1000;
const int kEstablishedDowntime = 2000;
const int kEstablishedNumResets = 9;
const bool kEstablishedIsGraceful = true;
const int kEstablishedPrePolicyRcvdPrefixCount = 2;
const int kEstablishedPostPolicySentPrefixCount = 2;
const int kEstablishedRecvUpdateMsgs = 100;
const int kEstablishedSentUpdateMsgs = 150;
const int kEstablishedBackpressureEvents = 10;
const int kEstablishedSuppressedUpdates = 5;

// Common idle session constants
const int kIdleRemoteAS = 4651;
const std::string_view kIdlePeerAddress = "2.3.4.5";
const std::string_view kIdleRemoteBgpId = "6.7.8.9";
const std::string_view kIdleDescription = "leaf07.p099.f01.lab0";
const bool kIdleIsGraceful = false;
const int kIdlePrePolicyRcvdPrefixCount = 0;
const int kIdlePostPolicySentPrefixCount = 0;
const int kIdleDowntime = 2500;
const int kIdleNumResets = 33;
const int kIdleRecvUpdateMsgs = 50;
const int kIdleSentUpdateMsgs = 75;
const int kIdleBackpressureEvents = 20;
const int kIdleSuppressedUpdates = 15;

// Common description constants
const std::string_view kIbgpDescription = "IBGP v4 Peers";

// Common group name constants
const std::string_view kEstablishedGroupName = "ESTABLISHED_GROUP";
const std::string_view kIdleGroupName = "IDLE_GROUP";
const std::string_view kIbgpGroupName = "IBGP_GROUP";

// Common socket write constants
const int64_t kEstablishedLastSocketWrite = 0;
const int64_t kIdleLastSocketWrite = 0;

TIpPrefix getPrefix(const std::string& ipAddress) {
  auto maskIndex = ipAddress.find_last_of('/');
  const auto address = (maskIndex == std::string::npos)
      ? folly::IPAddress(ipAddress)
      : folly::IPAddress(ipAddress.substr(0, maskIndex));
  const auto kBinaryAddress = facebook::network::toBinaryAddress(address);

  TIpPrefix ipPrefix;
  ipPrefix.prefix_bin() = kBinaryAddress.addr().value().toStdString();
  ipPrefix.num_bits() = (maskIndex != std::string::npos)
      ? folly::to<int>(ipAddress.substr(maskIndex + 1))
      : (address.isV4()) ? 32
                         : 128;
  ipPrefix.afi() = TBgpAfi::AFI_IPV4;
  return ipPrefix;
}

TBgpPath buildPath(
    const std::string& prefixAddress,
    const std::string& nextHopAddress,
    const std::string& peerId,
    const std::string& peerDescription,
    const bool setCommunity,
    const bool setAsPath,
    const bool setExtCommunity,
    const std::optional<uint32_t>& clusterList,
    const std::optional<uint32_t>& originatorId,
    const bool setPolicy,
    const std::optional<uint32_t>& nextHopWeight,
    const std::optional<bool> isBestPath,
    const bool setMed,
    const bool setReceivedPathId,
    const bool setPathIdToSend,
    const bool setWeight,
    const bool setIgpCost) {
  TBgpPath path;
  path.next_hop() = getPrefix(nextHopAddress);
  path.peer_id() = getPrefix(peerId);
  path.peer_description() = peerDescription;
  path.local_pref() = 25;
  path.origin() = 2;
  path.last_modified_time() = 1635278860724 * 1000;
  path.bestpath_filter_descr() =
      "Router-Id, Filter Criterion: Choose Lowest Value";
  if (nextHopWeight.has_value()) {
    path.next_hop_weight() = nextHopWeight.value();
  }

  if (setCommunity) {
    TBgpCommunity community;
    community.community() = 4274352190;
    community.asn() = 65221;
    community.value() = 28734;
    path.communities() = std::initializer_list<TBgpCommunity>({community});
  }
  if (setExtCommunity) {
    TBgpTwoByteAsnExtComm twoByteAsn;
    twoByteAsn.type() = 64;
    twoByteAsn.sub_type() = 2;
    twoByteAsn.asn() = 3;
    twoByteAsn.value() = 4;

    TBgpExtCommUnion communityUnion;
    communityUnion.two_byte_asn() = twoByteAsn;

    TBgpExtCommunity extCommunity;
    extCommunity.u() = communityUnion;

    path.extCommunities() =
        std::initializer_list<TBgpExtCommunity>({extCommunity});
  }
  if (setAsPath) {
    TAsPathSeg pathSegment;
    pathSegment.seg_type() = TAsPathSegType::AS_SEQUENCE;
    pathSegment.asns() = {64712};

    path.as_path() = std::initializer_list<TAsPathSeg>({pathSegment});
  }
  if (clusterList) {
    path.cluster_list() = {htonl(*clusterList)};
  }
  if (originatorId) {
    path.originator_id() = htonl(*originatorId);
  }
  if (setPolicy) {
    path.policy_name() = "Accepted/Modified by SAMPLE_UPLINK_POLICY term N/A";
  }
  if (isBestPath) {
    path.is_best_path() = isBestPath.value_or(false /* default */);
  }
  if (setMed) {
    path.med() = 10;
  }
  if (setReceivedPathId) {
    path.path_id() = 5;
  }
  if (setPathIdToSend) {
    path.path_id_to_send() = 6;
  }
  if (setWeight) {
    path.weight() = 20;
  }
  if (setIgpCost) {
    path.igp_cost() = 100;
  }
  return path;
}

std::map<TIpPrefix, std::vector<TBgpPath>> getReceivedNetworks(
    const std::string& prefixAddress,
    const std::string& nextHopAddress,
    const bool setCommunity,
    const bool setAsPath,
    const bool setExtCommunity,
    const std::optional<uint32_t>& clusterList,
    const std::optional<uint32_t>& originatorId,
    const bool setPolicy,
    const bool setMed) {
  std::map<TIpPrefix, std::vector<TBgpPath>> queriedNetworks = {
      {getPrefix(prefixAddress),
       {buildPath(
           prefixAddress,
           nextHopAddress,
           kPeerId,
           kPeerDescription,
           setCommunity,
           setAsPath,
           setExtCommunity,
           clusterList,
           originatorId,
           setPolicy,
           std::nullopt,
           std::nullopt,
           setMed)}}};
  return queriedNetworks;
}

TRibEntry buildEntry(
    const std::string& ipAddress,
    const std::string& nextHop,
    const std::map<std::string, std::vector<TBgpPath>>& paths,
    const bool omitBestPath) {
  TRibEntry entry;
  entry.best_group() = kBestGroupName;
  if (!omitBestPath) {
    entry.best_next_hop() = getPrefix(nextHop);
  }
  entry.prefix() = getPrefix(ipAddress);
  entry.paths() = paths;
  return entry;
}

TRibEntry buildEntry(
    const std::string& ipAddress,
    const std::string& nextHop,
    const std::string& peerId,
    const std::string& peerDescription,
    const std::optional<uint32_t>& pathNextHopWeight,
    const bool omitBestPath,
    const bool omitBestPaths) {
  std::map<std::string, std::vector<TBgpPath>> paths = {
      {omitBestPaths ? kDefaultGroupName : kBestGroupName,
       {buildPath(
           ipAddress,
           nextHop,
           peerId,
           peerDescription,
           true, // setCommunity
           true, // setAsPath
           true, // setExtCommunity
           kClusterList,
           kOriginatorId,
           false, // setPolicy
           pathNextHopWeight)}}};
  return buildEntry(ipAddress, nextHop, paths, omitBestPath);
}

TCanonicalRibState buildCanonicalRibState(
    const std::string& ipAddress,
    const std::string& nextHop,
    const std::string& peerId,
    const std::string& peerDescription) {
  TCanonicalRibState state;

  TBgpDedupedPath deduped;
  deduped.next_hop() = getPrefix(nextHop);
  deduped.med() = 10;
  state.deduped_paths()->emplace(0, std::move(deduped));

  TCanonicalPeer peer;
  peer.peer_id() = getPrefix(peerId);
  peer.peer_description() = peerDescription;
  state.peers()->emplace(0, std::move(peer));

  TBgpPathCanonical path;
  path.path_idx() = 0;
  path.peer_idx() = 0;
  path.is_best_path() = true;

  TRibEntryCanonical entry;
  entry.prefix() = getPrefix(ipAddress);
  // "best" mirrors facebook::bgp::kBestPathGroup (the group key real bgpd
  // uses).
  entry.paths() = {{"best", {std::move(path)}}};
  state.rib_entries() = {{ipAddress, std::move(entry)}};
  return state;
}

void maskDateInOutput(std::string& output) {
  std::size_t dateEndIndex = 0;
  // get the next "LM: "
  std::size_t dateStartIndex = output.find("LM: ", dateEndIndex);
  // newOutput is essentially output with all the date masked by "#"
  std::string newOutput;

  while (dateStartIndex != std::string::npos) {
    // point to the end of the "LM: " instance
    dateStartIndex += 4;

    // add the message between the end of the last date and the start of the
    // next date, followed by "#"
    newOutput +=
        output.substr(dateEndIndex, dateStartIndex - dateEndIndex) + "#";

    // we should be able to find the end of the date
    // output.find is never std::string::npos
    dateEndIndex = output.find('s', dateStartIndex) + 1;

    // get the next "LM: "
    dateStartIndex = output.find("LM: ", dateEndIndex);
  }
  newOutput += output.substr(dateEndIndex);

  output = newOutput;
}

TRibEntry buildEntryByCommunity(const std::string& name, int asn, int value) {
  TRibEntry entry = buildEntry();
  entry.best_group() = name;
  auto paths = entry.paths();
  // Update group name from path
  auto node = paths->extract("bestGroupName");
  node.key() = name;
  paths->insert(std::move(node));

  // Set new community values
  if (auto communities = paths->at(name).at(0).communities()) {
    auto& community = communities->at(0);
    community.asn() = asn;
    community.value() = value;
  }

  return entry;
}

TBgpStreamSession buildStreamSession(
    std::string_view name,
    int uptime,
    int32_t peerId,
    std::string_view peerAddr,
    int16_t prefixCount) {
  TBgpStreamSession session;
  session.subscriber_name() = name.data();
  session.uptime() = uptime;
  session.peer_id() = peerId;
  session.peer_addr() = peerAddr.data();
  session.sent_prefix_count() = prefixCount;
  return session;
}
} // namespace facebook::fboss
