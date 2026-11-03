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

struct BgpNeighborsReceivedRejectedTraits : public ReadCommandTraits {
  using ParentCmd = CmdShowBgpNeighbors;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_IP_LIST;
  using ObjectArgType = std::vector<std::string>;
  using RetType = NetworkPathWithHost;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Displays the routes from a peer that the ingress policy discarded, each with the policy and term that denied it. This is where to look when a peer insists it is advertising a prefix that never appears in 'show bgp table': if the prefix is in 'received pre-policy' and here, the switch dropped it deliberately and the named term says why. Empty output means the ingress policy dropped nothing from that peer, which is the normal state. The peer address is required.";
  }
};

class BgpNeighborsReceivedRejected : public CmdHandler<
                                         BgpNeighborsReceivedRejected,
                                         BgpNeighborsReceivedRejectedTraits> {
 public:
  using RetType = BgpNeighborsReceivedRejectedTraits::RetType;
  using ObjectArgType = BgpNeighborsReceivedRejected::ObjectArgType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const std::vector<std::string>& queriedIps,
      const ObjectArgType& prefixes) {
    if (queriedIps.empty()) {
      std::cout
          << "This command must have an IP address as argument: fboss2 show bgp neighbors [ADDR] rejected [PREFIX]"
          << std::endl;
      return {};
    }
    auto client = utils::createClient<apache::thrift::Client<
        facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);
    std::map<TIpPrefix, std::vector<TBgpPath>> receivedNetworks;
    std::map<TIpPrefix, std::vector<TBgpPath>> acceptedNetworks;

    client->sync_getPrefilterReceivedNetworks2(receivedNetworks, queriedIps[0]);
    client->sync_getPostfilterReceivedNetworks2(
        acceptedNetworks, queriedIps[0]);

    std::map<TIpPrefix, std::vector<TBgpPath>> rejectedNetworks =
        getRejectedNetworks(receivedNetworks, acceptedNetworks);

    if (!prefixes.empty()) {
      rejectedNetworks = filterNetworks(rejectedNetworks, prefixes);
    }

    NetworkPathWithHost result;
    result.networkPath() = std::move(rejectedNetworks);
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
        "Denied by SAMPLE_UPLINK_IN term SAMPLE_DENY_MARTIAN");
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
