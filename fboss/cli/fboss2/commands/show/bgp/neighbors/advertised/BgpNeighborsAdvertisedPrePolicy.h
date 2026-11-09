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

struct BgpNeighborsAdvertisedPrePolicyTraits : public ReadCommandTraits {
  using ParentCmd = CmdShowBgpNeighbors;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_IP_LIST;
  using ObjectArgType = std::vector<std::string>;
  using RetType = NetworkPathWithHost;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Displays the routes this switch has selected to send to a peer, before the egress policy runs. Each entry shows the prefix, the next hop advertised with it, the originator or router ID and cluster list, the community and extended-community sets, the AS path, local preference, origin and MED. These are candidates, not what the peer actually receives: compare against 'show bgp neighbors <peer> advertised post-policy' to see what the egress policy let through and how it rewrote the attributes, and against 'advertised rejected' for what it dropped. Advertised routes carry no last-modified time, so that field reads 'Not set'. The peer address is required.";
  }
};

class BgpNeighborsAdvertisedPrePolicy
    : public CmdHandler<
          BgpNeighborsAdvertisedPrePolicy,
          BgpNeighborsAdvertisedPrePolicyTraits> {
 public:
  using RetType = BgpNeighborsAdvertisedPrePolicyTraits::RetType;
  using ObjectArgType = BgpNeighborsAdvertisedPrePolicy::ObjectArgType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const std::vector<std::string>& queriedIps,
      const ObjectArgType& prefixes) {
    NetworkPathWithHost result;
    if (queriedIps.empty()) {
      std::cout
          << "This command must have an IP address as argument: fboss2 show bgp neighbors [ADDR] pre-policy [PREFIX]"
          << std::endl;
      return result;
    }
    auto client = utils::createClient<apache::thrift::Client<
        facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);
    std::map<TIpPrefix, std::vector<TBgpPath>> advertisedNetworks;
    client->sync_getPrefilterAdvertisedNetworks2(
        advertisedNetworks, queriedIps[0]);

    if (!prefixes.empty()) {
      advertisedNetworks = filterNetworks(advertisedNetworks, prefixes);
    }
    result.networkPath() = std::move(advertisedNetworks);
    result.host() = hostInfo.getName();
    result.oobName() = hostInfo.getOobName();
    result.ip() = hostInfo.getIpStr();
    return result;
  }

  // Canned, synthetic model (no real switch data) used to render a
  // deterministic example for the CLI reference wiki. Shares one builder with
  // the other five advertised/received views so they document the same routes.
  static RetType sampleModel() {
    return sampleNetworkPaths(SampleRouteDirection::Advertised, "");
  }

  void printOutput(
      const RetType& routesWithHost,
      std::ostream& out = std::cout) {
    const HostInfo hostInfo(
        *routesWithHost.host(),
        *routesWithHost.oobName(),
        folly::IPAddress(*routesWithHost.ip()));
    std::vector<std::string> output = printRoutesInformation(
        *routesWithHost.networkPath(), hostInfo, /*showPolicy=*/false);
    out << folly::join('\n', output) << std::endl;
  }
};
} // namespace facebook::fboss
