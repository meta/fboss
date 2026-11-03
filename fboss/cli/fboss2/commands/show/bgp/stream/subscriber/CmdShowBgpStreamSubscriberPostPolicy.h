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

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/commands/show/bgp/stream/CmdShowBgpStreamSubscriber.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace facebook::fboss {
using namespace neteng::fboss::bgp::thrift;
using facebook::fboss::utils::Table;

struct CmdShowBgpStreamSubscriberPostPolicyTraits : public ReadCommandTraits {
  using ParentCmd = CmdShowBgpStreamSubscriber;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_IP_LIST;
  using ObjectArgType = std::vector<std::string>;
  using RetType = NetworkPathWithHost;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Displays the routes the switch actually streams to a subscriber, after the export policy has run, with a Policy line on each naming the policy and term that accepted it and any attributes that term rewrote. A subscriber is an external client - a controller or collector - subscribed to the switch's route stream rather than peered over BGP, and its numeric id comes from 'show bgp stream summary'. Diffing this against 'pre-policy' is how you see what the export policy changed; anything present there and absent here was dropped by it. An optional list of prefixes after the subcommand narrows the result to those prefixes. The subscriber id is required; without one the command fails with a usage hint.";
  }
};

class CmdShowBgpStreamSubscriberPostPolicy
    : public CmdHandler<
          CmdShowBgpStreamSubscriberPostPolicy,
          CmdShowBgpStreamSubscriberPostPolicyTraits> {
 public:
  using RetType = CmdShowBgpStreamSubscriberPostPolicyTraits::RetType;
  using ObjectArgType = CmdShowBgpStreamSubscriberPostPolicy::ObjectArgType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const std::vector<std::string>& peerIds,
      const std::vector<std::string>& prefixes) {
    if (peerIds.empty()) {
      throw std::invalid_argument(
          "No subscriber ID provided. Hint: use `fboss2 show bgp stream summary` to see the subscribers.\n"
          "Usage: `fboss2 show bgp stream subscriber <subscriber id> post-policy`\n"
          "Example: `fboss2 show bgp stream subscriber 1 post-policy`");
    }

    std::map<TIpPrefix, std::vector<TBgpPath>> routes;
    auto client = utils::createClient<apache::thrift::Client<
        facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);
    client->sync_getSubscriberNetworkInfo(
        routes, folly::to<int32_t>(peerIds[0]), "post-policy");

    if (!prefixes.empty()) {
      routes = filterNetworks(routes, prefixes);
    }

    NetworkPathWithHost result;
    result.networkPath() = std::move(routes);
    result.host() = hostInfo.getName();
    result.oobName() = hostInfo.getOobName();
    result.ip() = hostInfo.getIpStr();

    return result;
  }

  // Canned, synthetic model (no real switch data). Streamed routes are the
  // switch's own advertisements, so this reuses the shared advertised-route
  // sample; post-policy renders it with the Policy line.
  static RetType sampleModel() {
    return sampleNetworkPaths(
        SampleRouteDirection::Advertised,
        "Accepted/Modified by STREAM_EXPORT term SAMPLE_ACCEPT_TERM");
  }

  void printOutput(RetType& routesWithHost, std::ostream& out = std::cout) {
    const HostInfo hostInfo(
        *routesWithHost.host(),
        *routesWithHost.oobName(),
        folly::IPAddress(*routesWithHost.ip()));
    std::vector<std::string> output = printRoutesInformation(
        *routesWithHost.networkPath(), hostInfo, /*showPolicy=*/true);
    out << folly::join('\n', output) << std::endl;
  }
};

} // namespace facebook::fboss
