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

#include "folly/json/dynamic.h"

#include "configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/bgp_policy_types.h"
#include "configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h"
#include "configerator/structs/neteng/fboss/bgp/if/gen-cpp2/bgp_attr_types.h"
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace {
inline constexpr auto kGar = "--decode-gar-lbw-ext-comm";
}

namespace facebook::fboss {

enum class BgpPolicySource { POLICY_ARTIFACT, BGP_CONFIG };

struct LoadedBgpPolicy {
  BgpPolicySource source;
  facebook::bgp::thrift::BgpPolicyConfig config;
};

LoadedBgpPolicy getLoadedBgpPolicy(const HostInfo& hostInfo);
std::optional<facebook::bgp::bgp_policy::BgpPolicies> getRunningBgpPolicies(
    const HostInfo& hostInfo);
std::optional<facebook::bgp::nsf_policy::NsfTeWeightEncoding>
getNsfTeWeightEncoding(const facebook::bgp::bgp_policy::BgpPolicies& policies);

// This header is included across the entire bgp show-command tree, whose
// command headers reference many neteng::fboss::bgp::thrift types unqualified
// (e.g. NetworkPathWithHost, TBgpStreamSession, TRibEntryWithHost). Keep the
// using-directive so those dependents compile; replacing it with explicit
// using-declarations would require qualifying types across ~20 imported PR
// command headers.
// NOLINTNEXTLINE(google-build-using-namespace)
using namespace neteng::fboss::bgp::thrift;
using neteng::fboss::bgp_attr::TAsPath;
using neteng::fboss::bgp_attr::TBgpCommunity;
using neteng::fboss::bgp_attr::TIpPrefix;

namespace {
// This function computes a computation by recursively looking for available
// samples ahead of an offset.
template <class T>
void computeCombinations(
    int offset,
    int k,
    std::vector<T>& currentCombination,
    std::vector<std::vector<T>>& combinations,
    const std::vector<T>& container) {
  if (k == 0) {
    combinations.push_back(currentCombination);
    return;
  }

  for (int i = offset; i <= container.size() - k; ++i) {
    currentCombination.push_back(container.at(i));
    computeCombinations(
        i + 1, k - 1, currentCombination, combinations, container);
    currentCombination.pop_back();
  }
}
} // namespace

struct CmdShowVersionTraits : public ReadCommandTraits, public CliDocsExempt {
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = std::map<std::string, std::string>;
};

void cmdShowVersion(CmdShowVersionTraits::RetType& binaryVersion);
std::string getPackageId(
    const std::string& packageVersion,
    const std::string& packageInfo);

std::map<std::set<std::string>, std::string> getCommunitySet(
    const HostInfo& host);

const std::string printCommunities(
    const std::vector<TBgpCommunity>& peer_communities,
    const HostInfo& hostInfo,
    bool isMultiLine = false);

const std::string printClusterList(const std::vector<long>& clusterList);
const std::string printExtCommunities(
    const std::vector<TBgpExtCommunity>& extCommunities);
const std::string printAsPath(const TAsPath& asPath);
const std::string printLocalPrefs(
    int localPref,
    const std::map<int, std::string>& mnemonics);
folly::dynamic getLocalBgpConfig(const HostInfo& host);
const std::map<int, std::string> getLocalPrefMnemonics(
    const HostInfo& hostInfo);
// Reset static caches for testing
void resetBgpMnemonicCaches();
const std::vector<std::string> printRoutesInformation(
    const std::map<TIpPrefix, std::vector<TBgpPath>>& networks,
    const HostInfo& hostInfo,
    bool showPolicy);
// BGP Neighbors printOutput
void printTimestamp(
    long timeToParse,
    const std::string& outputMessage,
    std::ostream& out);
timeval splitFractionalSecondsFromTimer(long timer);
void printAddPathCapability(
    const std::vector<TBgpAddPathNegotiated>& capabilities,
    std::ostream& out);
void printBgpCapabilities(const TBgpSessionDetail& details, std::ostream& out);
void printBgpPrefixTelemetry(
    const TBgpSession& neighbor,
    const TBgpSessionDetail& details,
    std::ostream& out);
void printBgpMessageCounters(
    const TBgpSessionDetail& details,
    std::ostream& out);
void printBgpNeighborsOutput(
    const std::vector<TBgpSession>& neighbors,
    std::ostream& out);
void printRouteHeader(std::string& out, bool detailed);
// void printAdjacencies(
//     std::map<std::string, std::vector<openr::thrift::AdjacencyDatabase>>&
//         neighbors,
//     std::ostream& out);
// void filterReceivedRoutes(
//     openr::thrift::ReceivedRouteFilter& filter,
//     const std::vector<std::string>& addrs,
//     std::vector<std::string> node,
//     std::vector<std::string> area);
// void filterAdvertisedRoutes(
//     openr::thrift::AdvertisedRouteFilter& filter,
//     const std::vector<std::string>& addrs,
//     const std::string& prefixType = "");
// void printReceivedRoutesDetail(
//     const std::vector<openr::thrift::ReceivedRouteDetail>& routes,
//     std::ostream& out);
// void printOpenrAdvertisedRoute(
//     const std::vector<openr::thrift::AdvertisedRoute>& routes,
//     std::ostream& out);
// void printAdvertisedRoutesDetail(
//     const std::vector<openr::thrift::AdvertisedRouteDetail>& routes,
//     std::ostream& out);
const std::string formatBytes(size_t n);
// void printOpenrReceivedRoutes(
//     const std::vector<openr::thrift::ReceivedRouteDetail>& routes,
//     const std::unordered_map<std::string, std::string>& tagNames,
//     std::ostream& out,
//     bool detailed,
//     bool tag2Name = false);
/*
 * Marker legend printed above every RIB-entry listing. Single source of truth
 * shared by the internal and OSS printRIBEntries copies and by the golden
 * tests, so the legend cannot drift from the markers it describes.
 *
 * "!" marks a path excluded from selection before comparison.
 */
inline constexpr auto kRibEntryMarkersLegend =
    "Markers: * - One of the best entries, @ - Best entry, "
    "% - Pending selection, ! - Inactive path";

/*
 * Build a TIpPrefix from CIDR text ("0.0.0.0/0", "2001:db8::/32") or a bare
 * address. Only used to construct the canned data behind the CLI
 * reference-wiki sampleModel() hooks; live paths get their prefixes from the
 * daemon already in this form.
 *
 * Throws on input it cannot parse (folly::IPAddressFormatException for a
 * malformed address, folly::ConversionError for a non-numeric prefix length).
 * Callers pass literals, so a throw here means the literal is wrong and the
 * unit tests will catch it; do not feed this untrusted input.
 */
TIpPrefix sampleIpPrefix(const std::string& cidr);

/*
 * A two-byte-ASN community for canned sample data. community() carries the
 * packed 32-bit form printCommunities() reads; asn()/value() carry the halves.
 * uint16_t inputs so the pack cannot silently overflow.
 */
TBgpCommunity sampleCommunity(uint16_t asn, uint16_t value);

/*
 * The community values every BGP wiki sample uses, in one place. The
 * 'show bgp config' sample defines mnemonics for these exact pairs
 * (AS65108.DEFAULT and AS65108.AGGREGATE.GLOBAL), so a reader can
 * cross-reference a rendered community against that config; keeping them here
 * is what stops the two samples drifting apart. Sample ASNs, RFC 6996
 * private-use range.
 */
struct SampleCommunity {
  uint16_t asn;
  uint16_t value;
};
inline constexpr SampleCommunity kSampleLiveCommunity{64873, 521};
inline constexpr SampleCommunity kSampleDefaultCommunity{65108, 337};
inline constexpr SampleCommunity kSampleAggregateCommunity{65108, 814};
inline constexpr SampleCommunity kSampleLoopbackCommunity{65221, 291};
inline constexpr SampleCommunity kSampleRackCommunity{65108, 725};
TBgpCommunity sampleCommunity(const SampleCommunity& community);

/*
 * Canned RIB data (no real switch data, addresses in documentation ranges)
 * backing the CLI reference-wiki sampleModel() hooks of the commands that
 * render a RIB listing. Shared so 'show bgp table' and 'show bgp table detail'
 * document the same rows, since detail only adds lines to the same paths.
 *
 * Two prefixes: an IPv4 default route with two ECMP paths (one selected as
 * best, one rejected on router-id) plus a third path outside the best group,
 * and an IPv6 prefix with a single best path.
 */
TRibEntryWithHost sampleRibEntriesWithHost();

/** Format a wire-format BGP address without aborting on malformed data. */
std::string formatBgpIpAddress(const TIpPrefix& prefix);

/** Print submitted FIB-out state attached to one exact RIB entry. */
void printFibOut(std::ostream& out, const TRibEntry& entry);

/** Return synthetic FIB-out data for CLI documentation and tests. */
TFibOutTable sampleFibOutTable();

// Which side of a peering the canned paths represent. Advertised routes have
// not been installed anywhere, so they carry no last-modified time; received
// ones do.
enum class SampleRouteDirection { Advertised, Received };

/*
 * Canned per-peer route data (no real switch data) backing the CLI
 * reference-wiki sampleModel() hooks of the six
 * 'show bgp neighbors <peer> advertised|received ...' views, which all render
 * through printRoutesInformation(). Shared so the six entries describe the
 * same routes rather than drifting apart.
 *
 * policyName is rendered only by the post-policy and rejected views (the
 * pre-policy views pass showPolicy=false), so pass the accept or deny string
 * the view is meant to illustrate.
 */
NetworkPathWithHost sampleNetworkPaths(
    SampleRouteDirection direction,
    const std::string& policyName);

// Prints entries for bgp table commands
void printRIBEntries(
    std::ostream& out,
    TRibEntryWithHost& entries,
    bool detail = false,
    const std::string& originator = "",
    bool fbnetsource = false);
std::map<TIpPrefix, std::vector<TBgpPath>> filterNetworks(
    const std::map<TIpPrefix, std::vector<TBgpPath>>& networks,
    const std::vector<std::string>& prefixes);
// Find the routes that where advertised/received but not accepted by a peer
const std::map<TIpPrefix, std::vector<TBgpPath>> getRejectedNetworks(
    const std::map<TIpPrefix, std::vector<TBgpPath>>& receivedNetworks,
    const std::map<TIpPrefix, std::vector<TBgpPath>>& acceptedNetworks);
// Searches for link bandwidth in a list of ext-communities.
float getLinkBandwidthFromExtCommunities(
    const std::vector<TBgpExtCommunity>& extCommunities);
// Shared helpers used by both the OSS (oss/CmdShowUtils.cpp) and internal
// (facebook/CmdShowUtils.cpp) implementations of the diverging show-util
// functions. Defined once in commands/show/bgp/CmdShowUtils.cpp.
float translateLinkBandwidthValue(uint32_t linkBandwidthValue);
std::string formatBgpOrigin(int32_t origin);
bool hasBestPath(const TRibEntry& entry);
// This functions maps from std::map<TIpPrefix, TBgpPath> to
// std::map<TIpPrefix, std::vector<TBgpPath>>
const std::map<TIpPrefix, std::vector<TBgpPath>> vectorizePaths(
    const std::map<TIpPrefix, TBgpPath>& networks);

// This function gets all the combinations of a given container
// in k number of samples. The return value is a list with all the
// combinations.
//
// Example: container = [1, 2, 3], k = 2
// The formula to compute combinations C(n, k):
//    C(n, k) = n! / (k! * (n - k)!) = 3! / (2! * (3 - 2)!) = 3 combinations
//
// Result = {[1, 2], [1, 3], [2, 3]}
template <class T>
std::vector<std::vector<T>> getCombinations(
    const std::vector<T>& container,
    int k) {
  const int size = container.size();
  if (k > size || k < 0) {
    throw std::out_of_range("Number of samples (k) out of combination range.");
  }
  if (k == size) {
    return {container};
  }
  if (k == 0) {
    return {};
  }

  std::vector<T> currentCombination;
  std::vector<std::vector<T>> combinations;
  computeCombinations(0, k, currentCombination, combinations, container);
  return combinations;
}

// Function to sort entries by prefix and next hop.
void sortEntries(std::vector<TRibEntry>& entries);
// Return communities for a given community mnemonic.
const std::vector<std::string> communityNameToValue(
    const std::string& communityName,
    const HostInfo& hostInfo);
// Returns asn:value from community
const std::string communityToString(const TBgpCommunity& community);
// Checks for a given community that the asn and value are integers
bool isValidCommunity(std::string_view communityName);

void stringFormatTimestamp(
    time_t timestamp,
    const char* format,
    std::string* output);

} // namespace facebook::fboss
