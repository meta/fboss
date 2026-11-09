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

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors_by_name/advertised/BgpNeighborsByNameAdvertisedRejected.h"

namespace facebook::fboss {

struct BgpNeighborsByNameAdvertisedRejectedCrfTraits
    : public ReadCommandTraits {
  using ParentCmd = BgpNeighborsByNameAdvertisedRejected;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = std::map<std::string, std::vector<std::string>>;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Narrows 'show bgp neighbors-by-name <pattern> advertised rejected' to the prefixes withheld by Cluster Route Filtering, dropping every rejection attributable to a named policy term. CRF withholds a prefix from a peer because advertising it would violate the cluster's route-scoping rules, not because a configured term matched, so these rejections are the ones an operator did not write and would not find by reading the policy. Everything else - the grouping by prefix and reason, the peer list per group, the optional prefix filter (which is the parent command's argument, so it is typed before 'crf'), the required name pattern, and 'No rejected prefixes found.' on an empty result - behaves exactly as it does without 'crf'. Run the parent command first: if a prefix is missing from both, nothing rejected it and the absence is elsewhere.";
  }
};

class BgpNeighborsByNameAdvertisedRejectedCrf
    : public CmdHandler<
          BgpNeighborsByNameAdvertisedRejectedCrf,
          BgpNeighborsByNameAdvertisedRejectedCrfTraits> {
 public:
  using RetType = BgpNeighborsByNameAdvertisedRejectedCrfTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const std::vector<std::string>& regexPatterns,
      const std::vector<std::string>& prefixes);

  void printOutput(const RetType& result, std::ostream& out = std::cout);

  // Canned, synthetic model (no real switch data): the CRF subset of the
  // parent command's sample, taken through the same crfOnly filter the query
  // applies.
  static RetType sampleModel() {
    return BgpNeighborsByNameAdvertisedRejected::sampleRejectedPrefixes(
        SampleRouteDirection::Advertised, /*crfOnly=*/true);
  }
};
} // namespace facebook::fboss
