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

struct BgpNeighborsAdvertisedRejectedTraits : public ReadCommandTraits {
  using ParentCmd = CmdShowBgpNeighbors;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_IP_LIST;
  using ObjectArgType = std::vector<std::string>;
  using RetType = NetworkPathWithHost;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Displays the routes this switch chose not to send to a peer, each with the egress policy and term that denied it. This is the direct answer to 'why is my peer not seeing this prefix' when the prefix is present in 'show bgp table' and in 'advertised pre-policy' but missing from 'advertised post-policy'. Empty output means the egress policy dropped nothing for that peer, which is the normal state on a healthy session. The peer address is required.";
  }
};

class BgpNeighborsAdvertisedRejected
    : public CmdHandler<
          BgpNeighborsAdvertisedRejected,
          BgpNeighborsAdvertisedRejectedTraits> {
 public:
  using RetType = BgpNeighborsAdvertisedRejectedTraits::RetType;
  using ObjectArgType = BgpNeighborsAdvertisedRejected::ObjectArgType;

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

    client->sync_getPrefilterAdvertisedNetworks2(
        receivedNetworks, queriedIps[0]);
    client->sync_getPostfilterAdvertisedNetworks2(
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
        SampleRouteDirection::Advertised,
        "Denied by SAMPLE_UPLINK_OUT term SAMPLE_DENY_LONG_PATH");
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
