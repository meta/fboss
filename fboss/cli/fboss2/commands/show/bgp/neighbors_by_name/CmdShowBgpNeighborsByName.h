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

#include <re2/re2.h>

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/CmdShowBgpNeighbors.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace facebook::fboss {

using facebook::neteng::fboss::bgp::thrift::TBgpSession;

struct CmdShowBgpNeighborsByNameTraits : public ReadCommandTraits {
  using ParentCmd = void;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_IP_LIST;
  using ObjectArgType = std::vector<std::string>;
  using RetType = std::vector<TBgpSession>;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Displays the same full per-session detail as 'show bgp neighbors', but selects the sessions by peer description rather than by peer address. The argument is a regular expression, matched as a substring against each session's description - the peer hostname the switch was configured with - so 'fsw' selects every uplink and 'fsw001.p001.f01.abc1' selects exactly one. This is the command to reach for when you know which box you care about but not which address the session runs over, which is the usual case on a switch peering over IPv6 link-local or over several parallel sessions to the same neighbor. Sessions whose description is empty, notably configured listen ranges that nobody has connected on, are excluded by any pattern that requires at least one character - matching is a partial match, so a pattern that can match the empty string (such as '.*') does select them. An unmatched pattern prints 'No neighbors matched the given pattern.' rather than empty output, so an empty result is distinguishable from a broken query. The pattern is required; an invalid regex is reported on stderr and yields no rows.";
  }
};

class CmdShowBgpNeighborsByName : public CmdHandler<
                                      CmdShowBgpNeighborsByName,
                                      CmdShowBgpNeighborsByNameTraits> {
 public:
  using RetType = CmdShowBgpNeighborsByNameTraits::RetType;

  static std::vector<TBgpSession> filterNeighborsByNameRegex(
      const std::vector<TBgpSession>& allSessions,
      const std::string& pattern);

  RetType queryClient(
      const HostInfo& hostInfo,
      const ObjectArgType& regexPatterns);

  void printOutput(const RetType& neighbors, std::ostream& out = std::cout);

  // Canned, synthetic model (no real switch data) used to render a
  // deterministic example for the CLI reference wiki. Built by running the
  // command's own filter over the 'show bgp neighbors' sample, so the example
  // stays in step with that command's render and demonstrates the filter
  // dropping the description-less listen range.
  static RetType sampleModel() {
    return filterNeighborsByNameRegex(
        CmdShowBgpNeighbors::sampleModel(), "fsw");
  }
};
} // namespace facebook::fboss
