// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <fboss/agent/if/gen-cpp2/ctrl_types.h>
#include <fboss/cli/fboss2/utils/CmdUtils.h>
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/cli/fboss2/commands/show/route/gen-cpp2/model_types.h"

namespace facebook::fboss::show::route::utils {

bool isFpfEncoding(
    const std::optional<facebook::bgp::nsf_policy::NsfTeWeightEncoding>&
        encoding);

// Parse a --clientID option value: a ClientID enum name (case-insensitive,
// e.g. "BGPD", "static_route") or its numeric value. Throws
// std::invalid_argument listing the accepted names otherwise.
ClientID parseClientId(const std::string& str);

// "ipv4" or "ipv6", from the route's destination prefix.
std::string getAddressFamilyStr(const IpPrefix& dest);

std::string getMplsActionCodeStr(MplsActionCode mplsActionCode);

std::string getMplsActionInfoStr(const cli::MplsActionInfo& mplsActionInfo);

void getNextHopInfoAddr(
    const network::thrift::BinaryAddress& addr,
    cli::NextHopInfo& nextHopInfo);

void getNextHopInfoThrift(
    const NextHopThrift& nextHop,
    cli::NextHopInfo& nextHopInfo);

// " SRv6 SID List [sid,sid]", or empty when the nexthop carries no list.
// Shared so SRv6 nexthops render the same in every command that shows them.
std::string getSrv6SidListStr(const cli::NextHopInfo& nextHopInfo);

std::string getNextHopInfoStr(
    const cli::NextHopInfo& nextHopInfo,
    const std::optional<facebook::bgp::nsf_policy::NsfTeWeightEncoding>&
        encoding = std::nullopt);
std::string getNextHopInfoStr(
    const cli::NextHopInfo& nextHopInfo,
    const std::map<std::string, std::string>& vlanAggregatePortMap,
    const std::map<
        std::string,
        std::map<std::string, std::vector<std::string>>>& vlanPortMap,
    const std::optional<facebook::bgp::nsf_policy::NsfTeWeightEncoding>&
        encoding = std::nullopt);

} // namespace facebook::fboss::show::route::utils
