/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/bgp/neighbors_by_name/advertised/BgpNeighborsByNameAdvertisedRejected.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"

namespace facebook::fboss {

BgpNeighborsByNameAdvertisedRejected::RetType
BgpNeighborsByNameAdvertisedRejected::queryRejectedPrefixes(
    const HostInfo& hostInfo,
    const std::string& regexPattern,
    const ObjectArgType& prefixes,
    bool crfOnly) {
  RetType result;

  auto client = utils::createClient<apache::thrift::Client<
      facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);

  std::vector<facebook::neteng::fboss::bgp::thrift::TBgpSession> allSessions;
  client->sync_getBgpNeighbors(allSessions, {});

  auto matchingSessions = CmdShowBgpNeighborsByName::filterNeighborsByNameRegex(
      allSessions, regexPattern);

  if (matchingSessions.empty()) {
    std::cout << "No neighbors matched pattern: " << regexPattern << std::endl;
    return result;
  }

  std::cout << "Querying rejected advertised prefixes for "
            << matchingSessions.size() << " matching neighbor(s)..."
            << std::endl;

  for (const auto& session : matchingSessions) {
    const auto& peerAddr = *session.peer_addr();
    const auto& description = *session.description();

    std::map<TIpPrefix, std::vector<TBgpPath>> preFilterNetworks;
    std::map<TIpPrefix, std::vector<TBgpPath>> postFilterNetworks;

    client->sync_getPrefilterAdvertisedNetworks2(preFilterNetworks, peerAddr);
    client->sync_getPostfilterAdvertisedNetworks2(postFilterNetworks, peerAddr);

    auto rejectedNetworks =
        getRejectedNetworks(preFilterNetworks, postFilterNetworks);

    if (!prefixes.empty()) {
      rejectedNetworks = filterNetworks(rejectedNetworks, prefixes);
    }

    for (const auto& [ipPrefix, paths] : rejectedNetworks) {
      auto prefixStr = ipPrefixToString(ipPrefix);
      for (const auto& path : paths) {
        auto policyName = getPolicyName(path);
        if (crfOnly && !isCrfRejection(policyName)) {
          continue;
        }
        auto key = makeKey(prefixStr, policyName);
        result[key].push_back(fmt::format("{} ({})", peerAddr, description));
      }
    }
  }

  return result;
}

BgpNeighborsByNameAdvertisedRejected::RetType
BgpNeighborsByNameAdvertisedRejected::queryClient(
    const HostInfo& hostInfo,
    const std::vector<std::string>& regexPatterns,
    const ObjectArgType& prefixes) {
  if (regexPatterns.empty()) {
    std::cout
        << "This command requires a regex pattern as argument: "
        << "fboss2 show bgp neighbors-by-name <REGEX_PATTERN> advertised rejected"
        << std::endl;
    return {};
  }
  return queryRejectedPrefixes(
      hostInfo, regexPatterns[0], prefixes, /*crfOnly=*/false);
}

void BgpNeighborsByNameAdvertisedRejected::printRejectedResult(
    const RetType& result,
    std::ostream& out) {
  if (result.empty()) {
    out << "No rejected prefixes found." << std::endl;
    return;
  }

  out << fmt::format("{} rejected prefix/policy group(s)", result.size())
      << std::endl;
  out << std::endl;

  for (const auto& [key, neighbors] : result) {
    auto sepPos = key.find(" | ");
    if (sepPos == std::string::npos) {
      out << fmt::format("Prefix: {}", key) << std::endl;
      out << fmt::format("  Neighbor(s) ({}):", neighbors.size()) << std::endl;
      for (const auto& neighbor : neighbors) {
        out << fmt::format("    {}", neighbor) << std::endl;
      }
      out << std::endl;
      continue;
    }
    auto prefix = key.substr(0, sepPos);
    auto policy = key.substr(sepPos + 3);
    out << fmt::format("Prefix: {}", prefix) << std::endl;
    out << fmt::format("  Rejected reason: {}", policy) << std::endl;
    out << fmt::format("  Neighbor(s) ({}):", neighbors.size()) << std::endl;
    for (const auto& neighbor : neighbors) {
      out << fmt::format("    {}", neighbor) << std::endl;
    }
    out << std::endl;
  }
}

void BgpNeighborsByNameAdvertisedRejected::printOutput(
    const RetType& result,
    std::ostream& out) {
  printRejectedResult(result, out);
}

BgpNeighborsByNameAdvertisedRejected::RetType
BgpNeighborsByNameAdvertisedRejected::sampleRejectedPrefixes(
    SampleRouteDirection direction,
    bool crfOnly) {
  const std::string fsw001 = "192.0.2.11 (fsw001.p001.f01.abc1)";
  const std::string fsw002 = "192.0.2.12 (fsw002.p001.f01.abc1)";

  // Real policy_name values carry the "Denied by " prefix, and the egress and
  // ingress paths name different policies, so the two directions must not
  // share one string.
  const std::string namedTerm = direction == SampleRouteDirection::Advertised
      ? "Denied by SAMPLE_UPLINK_OUT term DENY_RFC1918"
      : "Denied by SAMPLE_UPLINK_IN term DENY_RFC1918";

  /*
   * Three groups covering what the prose describes: one named-term rejection
   * that both matched uplinks share, one CRF rejection they also share, and
   * one CRF rejection only fsw001 makes - the asymmetric case a reader is
   * looking for. Keys are built with makeKey() and screened with
   * isCrfRejection() so the example cannot drift from the real encoding.
   */
  const std::vector<
      std::tuple<std::string, std::string, std::vector<std::string>>>
      groups = {
          {"10.0.0.0/8", namedTerm, {fsw001, fsw002}},
          {"203.0.113.0/24", kCrfPolicyName, {fsw001, fsw002}},
          {"2001:db8:1c00::/40", kCrfPolicyName, {fsw001}}};

  RetType result;
  for (const auto& [prefix, policyName, neighbors] : groups) {
    if (crfOnly && !isCrfRejection(policyName)) {
      continue;
    }
    result[makeKey(prefix, policyName)] = neighbors;
  }
  return result;
}

template void CmdHandler<
    BgpNeighborsByNameAdvertisedRejected,
    BgpNeighborsByNameAdvertisedRejectedTraits>::run();

} // namespace facebook::fboss
