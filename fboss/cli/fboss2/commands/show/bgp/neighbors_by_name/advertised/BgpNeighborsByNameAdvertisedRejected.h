/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <fmt/format.h>
#include <re2/re2.h>

#include <folly/IPAddress.h>

#include "configerator/structs/neteng/fboss/bgp/if/gen-cpp2/bgp_attr_types.h"
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors_by_name/CmdShowBgpNeighborsByName.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "folly/String.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace facebook::fboss {
using neteng::fboss::bgp::thrift::TBgpPath;
using neteng::fboss::bgp_attr::TIpPrefix;

namespace {
inline constexpr auto kCrfPolicyName = "Denied by CRF";
} // namespace

// RetType key: "prefix | policy_name" -> list of "peer_addr (description)".
// Using primitive containers so that CmdHandler's JSON serialization works.
struct BgpNeighborsByNameAdvertisedRejectedTraits : public ReadCommandTraits {
  using ParentCmd = CmdShowBgpNeighborsByName;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_IP_LIST;
  using ObjectArgType = std::vector<std::string>;
  using RetType = std::map<std::string, std::vector<std::string>>;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Displays the prefixes the egress policy refused to advertise, aggregated across every neighbor whose description matches the name pattern, rather than one peer at a time. Output is grouped by prefix and rejection reason - the policy term that denied it, or 'Unknown' when the switch did not record one - and each group lists the peers it applies to as 'address (description)'. That grouping is the point of the command: a term rejecting a prefix on one uplink but not its pair is a config asymmetry, and it shows here as a group with fewer peers than the pattern matched. An optional list of prefixes after the subcommand narrows the result to those prefixes. Empty output prints 'No rejected prefixes found.', which on a healthy switch is the expected result. The name pattern is required and is matched the same way 'show bgp neighbors-by-name' matches it. See 'show bgp neighbors <peer> advertised rejected' for the per-path detail behind any one of these rows.";
  }
};

class BgpNeighborsByNameAdvertisedRejected
    : public CmdHandler<
          BgpNeighborsByNameAdvertisedRejected,
          BgpNeighborsByNameAdvertisedRejectedTraits> {
 public:
  using RetType = BgpNeighborsByNameAdvertisedRejectedTraits::RetType;
  using ObjectArgType =
      BgpNeighborsByNameAdvertisedRejectedTraits::ObjectArgType;

  static std::string ipPrefixToString(const TIpPrefix& ipPrefix) {
    auto ipAddress = folly::IPAddress::fromBinary(
        folly::ByteRange(folly::StringPiece(ipPrefix.prefix_bin().value())));
    return fmt::format(
        "{}/{}", ipAddress.str(), folly::copy(ipPrefix.num_bits().value()));
  }

  static std::string getPolicyName(const TBgpPath& path) {
    auto* policyName = apache::thrift::get_pointer(path.policy_name());
    if (policyName && !policyName->empty()) {
      return *policyName;
    }
    return "Unknown";
  }

  static bool isCrfRejection(const std::string& policyName) {
    return policyName == kCrfPolicyName;
  }

  static std::string makeKey(
      const std::string& prefix,
      const std::string& policyName) {
    return fmt::format("{} | {}", prefix, policyName);
  }

  // Shared query logic used by both this command and the "crf" subcommand.
  static RetType queryRejectedPrefixes(
      const HostInfo& hostInfo,
      const std::string& regexPattern,
      const ObjectArgType& prefixes,
      bool crfOnly);

  RetType queryClient(
      const HostInfo& hostInfo,
      const std::vector<std::string>& regexPatterns,
      const ObjectArgType& prefixes);

  static void printRejectedResult(const RetType& result, std::ostream& out);

  void printOutput(const RetType& result, std::ostream& out = std::cout);

  // Canned, synthetic model (no real switch data) shared by this command, the
  // "crf" subcommand and both "received rejected" commands, which render the
  // same map through printRejectedResult(). crfOnly mirrors the query flag:
  // it keeps only the groups isCrfRejection() accepts.
  static RetType sampleRejectedPrefixes(
      SampleRouteDirection direction,
      bool crfOnly);

  static RetType sampleModel() {
    return sampleRejectedPrefixes(
        SampleRouteDirection::Advertised, /*crfOnly=*/false);
  }
};
} // namespace facebook::fboss
