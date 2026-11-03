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

#include <string_view>
#include "configerator/structs/neteng/fboss/bgp/if/gen-cpp2/bgp_attr_types.h"

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/CmdShowBgpNeighbors.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "folly/String.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace facebook::fboss {
using neteng::fboss::bgp::thrift::TBgpPath;
using neteng::fboss::bgp_attr::TIpPrefix;

struct BgpNeighborsReceivedPostPolicyTraits : public ReadCommandTraits {
  using ParentCmd = CmdShowBgpNeighbors;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_IP_LIST;
  using ObjectArgType = std::vector<std::string>;
  using RetType = NetworkPathWithHost;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Displays the routes from a peer that survived the ingress policy, with a Policy line on each naming the policy and term that accepted it and any attributes that term rewrote. These are the paths that go on to compete in best-path selection, so this is the view to trust when 'show bgp table' shows an attribute the peer did not send - the rewrite happened here. Prefixes present in 'received pre-policy' but absent from this view were dropped, and 'received rejected' names the term responsible. The peer address is required.";
  }
};

class BgpNeighborsReceivedPostPolicy
    : public CmdHandler<
          BgpNeighborsReceivedPostPolicy,
          BgpNeighborsReceivedPostPolicyTraits> {
 public:
  using RetType = BgpNeighborsReceivedPostPolicyTraits::RetType;
  using ObjectArgType = BgpNeighborsReceivedPostPolicy::ObjectArgType;

  // - The first argument will always be considered as the neighbor ip
  // - The second argument will always be consdiered as the session id
  // - Every other address after the session id will be ignored.
  //
  // Note: Currently '--session_id' flag is no supported on FBOSS2.
  // In the mean time, the way to query over a neighbor with a session id,
  // the id must be passed right after the peer IP like the following:
  //    fboss2 show bgp neighbors 1.2.3.4 5.6.7.8
  RetType queryClient(
      const HostInfo& hostInfo,
      const std::vector<std::string>& queriedIps,
      const ObjectArgType& prefixes) {
    NetworkPathWithHost result;
    if (queriedIps.empty()) {
      std::cout
          << "This command must have an IP address as argument: fboss2 show bgp neighbors [ADDR] post-policy [PREFIX]"
          << std::endl;
      return result;
    }
    auto client = utils::createClient<apache::thrift::Client<
        facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);
    std::map<TIpPrefix, std::vector<TBgpPath>> receivedNetworks;

    if (queriedIps.size() > 1) {
      std::cout << "Displaying information from neighbor: " << queriedIps[0]
                << ", session id: " << queriedIps[1] << std::endl;
      client->sync_getPostfilterReceivedNetworksFromSession2(
          receivedNetworks, queriedIps[0], queriedIps[1]);
    } else {
      client->sync_getPostfilterReceivedNetworks2(
          receivedNetworks, queriedIps[0]);
    }

    if (!prefixes.empty()) {
      receivedNetworks = filterNetworks(receivedNetworks, prefixes);
    }

    result.networkPath() = std::move(receivedNetworks);
    result.host() = hostInfo.getName();
    result.oobName() = hostInfo.getOobName();
    result.ip() = hostInfo.getIpStr();
    return result;
  }

  // Canned, synthetic model (no real switch data) used to render a
  // deterministic example for the CLI reference wiki. Shares one builder with
  // the other five advertised/received views so they document the same routes.
  static RetType sampleModel() {
    return sampleNetworkPaths(
        SampleRouteDirection::Received,
        "Accepted/Modified by SAMPLE_UPLINK_IN term SAMPLE_SET_LOCALPREF");
  }

  void printOutput(
      const RetType& routesWithHost,
      std::ostream& out = std::cout) {
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
