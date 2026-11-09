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
#include "fboss/cli/fboss2/commands/show/bgp/neighbors_by_name/received/BgpNeighborsByNameReceivedRejected.h"

namespace facebook::fboss {

struct BgpNeighborsByNameReceivedRejectedCrfTraits : public ReadCommandTraits {
  using ParentCmd = BgpNeighborsByNameReceivedRejected;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = std::map<std::string, std::vector<std::string>>;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Narrows 'show bgp neighbors-by-name <pattern> received rejected' to the prefixes dropped by Cluster Route Filtering, excluding every rejection attributable to a named policy term. CRF drops a received prefix because accepting it would violate the cluster's route-scoping rules rather than because a configured term matched, so these are the drops that are invisible in the policy config. Everything else - the grouping by prefix and reason, the peer list per group, the optional prefix filter (which is the parent command's argument, so it is typed before 'crf'), the required name pattern, and 'No rejected prefixes found.' on an empty result - behaves exactly as it does without 'crf'.";
  }
};

class BgpNeighborsByNameReceivedRejectedCrf
    : public CmdHandler<
          BgpNeighborsByNameReceivedRejectedCrf,
          BgpNeighborsByNameReceivedRejectedCrfTraits> {
 public:
  using RetType = BgpNeighborsByNameReceivedRejectedCrfTraits::RetType;

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
        SampleRouteDirection::Received, /*crfOnly=*/true);
  }
};
} // namespace facebook::fboss
