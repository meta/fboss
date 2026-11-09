/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/bgp/CmdShowBgpOriginatedRoutes.h"

#include <folly/IPAddress.h>

#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"

namespace facebook::fboss {
using facebook::fboss::utils::Table;
using facebook::neteng::fboss::bgp::thrift::TOriginatedRouteWithHost;

CmdShowBgpOriginatedRoutes::RetType CmdShowBgpOriginatedRoutes::queryClient(
    const HostInfo& hostInfo) {
  std::vector<TOriginatedRoute> routes;
  auto client = utils::createClient<apache::thrift::Client<
      facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);

  client->sync_getOriginatedRoutes(routes);
  TOriginatedRouteWithHost result;
  result.tOriginatedRoutes() = std::move(routes);
  result.host() = hostInfo.getName();
  result.oobName() = hostInfo.getOobName();
  result.ip() = hostInfo.getIpStr();
  return result;
}

void CmdShowBgpOriginatedRoutes::printOutput(
    const RetType& originatedRouteWithHost,
    std::ostream& out) {
  // Table provides a cleaner display and reduces the spacing needed
  // for the output to the minimum.
  Table table;
  table.setHeader(
      {"Prefix",
       "Communities",
       "Supporting Route Cnt",
       "Minimum supporting route",
       "Require Nexthop Resolution"});

  for (const auto& route : *originatedRouteWithHost.tOriginatedRoutes()) {
    std::string ip_version;
    const auto& route_prefix = *route.prefix();
    const auto ip_address = folly::IPAddress::fromBinary(
        folly::ByteRange(folly::StringPiece(*route_prefix.prefix_bin())));

    const auto prefix =
        fmt::format("{}/{}", ip_address.str(), *route_prefix.num_bits());
    const HostInfo hostInfo(
        *originatedRouteWithHost.host(),
        *originatedRouteWithHost.oobName(),
        folly::IPAddress(*originatedRouteWithHost.ip()));
    const auto communities = printCommunities(
        apache::thrift::can_throw(*route.communities()),
        hostInfo,
        true /* print community with multi-lines */);

    table.addRow(
        {prefix,
         communities,
         folly::to<std::string>(
             folly::copy(route.supporting_route_count().value())),
         folly::to<std::string>(
             folly::copy(route.minimum_supporting_routes().value())),
         route.require_nexthop_resolution()
             ? folly::to<std::string>(*route.require_nexthop_resolution())
             : "N/A"});
  }
  out << table << std::endl;
}

std::string_view CmdShowBgpOriginatedRoutesTraits::description() {
  return "Displays the prefixes this switch originates into BGP itself, rather than ones it learned from a peer: the prefix, the communities attached to it (one per line, resolved to their configured names where the switch knows them), how many supporting routes currently back it, the minimum number of supporting routes configured before it may be advertised, and whether advertising it requires the next hop to resolve - rendered as 0 or 1, or N/A when the switch does not report the flag at all. A supporting-route count of 0 against a minimum of 0 means the prefix is originated unconditionally, which is how a switch loopback is usually configured. When a minimum is configured and the supporting count falls below it, the prefix is withdrawn, so these two columns together explain why an expected origination is missing from 'show bgp table'. This lists intent, not outcome: check the prefix in 'show bgp table' to confirm it is actually in the RIB.";
}

CmdShowBgpOriginatedRoutes::RetType CmdShowBgpOriginatedRoutes::sampleModel() {
  using facebook::neteng::fboss::bgp::thrift::TOriginatedRoute;
  using neteng::fboss::bgp_attr::TBgpCommunity;

  auto originated = [&](const std::string& prefixCidr,
                        const std::vector<TBgpCommunity>& communities,
                        int64_t supportingRouteCount,
                        int64_t minimumSupportingRoutes,
                        bool requireNexthopResolution) {
    TOriginatedRoute route;
    route.prefix() = sampleIpPrefix(prefixCidr);
    route.communities() = communities;
    route.supporting_route_count() = supportingRouteCount;
    route.minimum_supporting_routes() = minimumSupportingRoutes;
    route.require_nexthop_resolution() = requireNexthopResolution;
    return route;
  };

  // The LIVE marker plus a rack-private / loopback marker - the shape of the
  // set an RSW attaches to its own prefixes. Sample ASNs (RFC 6996 private-use
  // range): these values ship in the open-source tree and must not be real
  // ones.
  const std::vector<TBgpCommunity> loopbackCommunities = {
      sampleCommunity(kSampleLiveCommunity),
      sampleCommunity(kSampleLoopbackCommunity)};
  const std::vector<TBgpCommunity> rackCommunities = {
      sampleCommunity(kSampleLiveCommunity),
      sampleCommunity(kSampleRackCommunity)};

  TOriginatedRouteWithHost result;
  /*
   * Three rows covering the cases the prose describes: two loopbacks
   * originated unconditionally (no minimum, so no supporting route needed and
   * no next-hop resolution required), and a rack prefix that does require
   * next-hop resolution and carries a configured minimum it currently meets -
   * the pair of columns that explains why an origination gets withdrawn if the
   * supporting count drops below the minimum.
   */
  result.tOriginatedRoutes() = {
      originated("192.0.2.1/32", loopbackCommunities, 0, 0, false),
      originated(
          "2001:db8:e111:f162:27::/128", loopbackCommunities, 0, 0, false),
      originated("2001:db8:111c:6227::/64", rackCommunities, 12, 8, true)};
  result.host() = "rsw001.p001.f01.abc1";
  result.oobName() = "rsw001.p001.f01.abc1.oob";
  result.ip() = "192.0.2.1";
  return result;
}

} // namespace facebook::fboss
