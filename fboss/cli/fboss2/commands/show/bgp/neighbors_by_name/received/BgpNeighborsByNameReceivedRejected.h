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
#include "fboss/cli/fboss2/commands/show/bgp/neighbors_by_name/advertised/BgpNeighborsByNameAdvertisedRejected.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "folly/String.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace facebook::fboss {
using neteng::fboss::bgp::thrift::TBgpPath;
using neteng::fboss::bgp_attr::TIpPrefix;

// RetType key: "prefix | policy_name" -> list of "peer_addr (description)".
// Using primitive containers so that CmdHandler's JSON serialization works.
struct BgpNeighborsByNameReceivedRejectedTraits : public ReadCommandTraits {
  using ParentCmd = CmdShowBgpNeighborsByName;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_IP_LIST;
  using ObjectArgType = std::vector<std::string>;
  using RetType = std::map<std::string, std::vector<std::string>>;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Displays the prefixes the ingress policy discarded, aggregated across every neighbor whose description matches the name pattern, rather than one peer at a time. Output is grouped by prefix and rejection reason - the policy term that denied it, or 'Unknown' when the switch did not record one - and each group lists the peers it applies to as 'address (description)'. This is the fleet-wide version of the 'a peer swears it is advertising this prefix but it never reaches the RIB' question: if the prefix appears here for every matched peer the drop is policy-wide, and if it appears for only some of them the drop is peer-specific. An optional list of prefixes after the subcommand narrows the result to those prefixes. Empty output prints 'No rejected prefixes found.'. The name pattern is required. See 'show bgp neighbors <peer> received rejected' for the per-path detail behind any one of these rows.";
  }
};

class BgpNeighborsByNameReceivedRejected
    : public CmdHandler<
          BgpNeighborsByNameReceivedRejected,
          BgpNeighborsByNameReceivedRejectedTraits> {
 public:
  using RetType = BgpNeighborsByNameReceivedRejectedTraits::RetType;
  using ObjectArgType = BgpNeighborsByNameReceivedRejectedTraits::ObjectArgType;

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

  void printOutput(const RetType& result, std::ostream& out = std::cout);

  // Canned, synthetic model (no real switch data). Received and advertised
  // rejections share a RetType and a renderer, so they share one sample.
  static RetType sampleModel() {
    return BgpNeighborsByNameAdvertisedRejected::sampleRejectedPrefixes(
        SampleRouteDirection::Received, /*crfOnly=*/false);
  }
};
} // namespace facebook::fboss
