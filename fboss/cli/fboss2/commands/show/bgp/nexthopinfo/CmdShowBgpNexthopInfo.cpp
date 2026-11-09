/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/bgp/nexthopinfo/CmdShowBgpNexthopInfo.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <optional>

#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"

namespace facebook::fboss {

namespace {
// Render an age in seconds as a compact "…ago" string.
std::string humanizeAge(int64_t seconds) {
  if (seconds < 0) {
    seconds = 0;
  }
  const int64_t days = seconds / 86400;
  seconds %= 86400;
  const int64_t hours = seconds / 3600;
  seconds %= 3600;
  const int64_t minutes = seconds / 60;
  const int64_t secs = seconds % 60;

  std::string out;
  if (days > 0) {
    out += std::to_string(days) + "d";
  }
  if (hours > 0) {
    out += std::to_string(hours) + "h";
  }
  if (minutes > 0) {
    out += std::to_string(minutes) + "m";
  }
  if (out.empty()) {
    out += std::to_string(secs) + "s";
  }
  return out + " ago";
}

// Unset age => "-"; otherwise return a compact "…ago" string.
std::string ageOrDash(const std::optional<int64_t>& ageSeconds) {
  return ageSeconds.has_value() ? humanizeAge(*ageSeconds) : "-";
}

std::string nexthopIpStr(const TNexthopInfo& entry) {
  const auto& prefixBin = *entry.next_hop()->prefix_bin();
  return folly::IPAddress::fromBinary(
             folly::ByteRange(
                 reinterpret_cast<const unsigned char*>(prefixBin.data()),
                 prefixBin.size()))
      .str();
}

// Detailed, per-nexthop "zoom" view shown when a specific IP is queried.
// Callers must handle cache misses (empty next_hop) before calling this.
void printDetailEntry(const TNexthopInfo& entry, std::ostream& out) {
  utils::Table table;
  table.setHeader({"Field", "Value"});
  table.addRow({"Nexthop", nexthopIpStr(entry)});
  table.addRow({"Reachable", *entry.is_reachable() ? "Yes" : "No"});
  table.addRow(
      {"IGP Cost",
       entry.igp_cost().has_value() ? folly::to<std::string>(*entry.igp_cost())
                                    : "N/A"});
  table.addRow(
      {"Directly Connected",
       entry.is_connected().has_value() ? (*entry.is_connected() ? "Yes" : "No")
                                        : "Unknown"});
  table.addRow(
      {"Resolved For Selection",
       *entry.is_resolved_for_selection() ? "Yes" : "No"});
  table.addRow(
      {"Dependent Routes", folly::to<std::string>(*entry.route_count())});
  table.addRow(
      {"Last Reachability Change",
       ageOrDash(entry.last_reachability_change_age_s().to_optional())});
  table.addRow(
      {"Last IGP Cost Change",
       ageOrDash(entry.last_igp_cost_change_age_s().to_optional())});
  out << table << std::endl;
}
} // namespace

CmdShowBgpNexthopInfo::RetType CmdShowBgpNexthopInfo::queryClient(
    const HostInfo& hostInfo,
    const ObjectArgType& queriedIps) {
  RetType result;

  // Specific IP(s) => detailed per-nexthop view; no arg => compact list of all.
  result.detailed() = !queriedIps.empty();

  auto client = utils::createClient<apache::thrift::Client<
      facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);

  if (queriedIps.empty()) {
    // No IP specified: list every entry in the nexthop cache.
    std::vector<TNexthopInfo> entries;
    client->sync_getNexthopInfos(entries, std::vector<std::string>{});
    result.entries() = std::move(entries);
    return result;
  }

  // Specific IP(s): keep the existing per-nexthop detailed lookup. Record the
  // queried address index-parallel to each entry so a cache miss can be named.
  for (const auto& ip : queriedIps) {
    TNexthopInfo nexthopInfo;
    client->sync_getNexthopInfoForNexthop(nexthopInfo, ip);
    result.entries()->push_back(std::move(nexthopInfo));
    result.queried_nexthops()->push_back(ip);
  }
  return result;
}

void CmdShowBgpNexthopInfo::printOutput(
    const RetType& data,
    std::ostream& out) {
  if (*data.detailed()) {
    const auto& entries = *data.entries();
    const auto& queried = *data.queried_nexthops();
    for (size_t i = 0; i < entries.size(); ++i) {
      const auto& prefixBin = *entries[i].next_hop()->prefix_bin();
      if (prefixBin.empty()) {
        // Miss: name the exact address the operator queried.
        const std::string ip = i < queried.size() ? queried[i] : std::string{};
        out << "Nexthop " << ip << " not found in the nexthop cache"
            << std::endl;
        continue;
      }
      printDetailEntry(entries[i], out);
    }
    return;
  }

  utils::Table table;
  table.setHeader(
      {"Nexthop",
       "Reachable",
       "IGP Cost",
       "Resolved For Selection",
       "Dependent Routes"});

  size_t rows = 0;
  for (const auto& entry : *data.entries()) {
    const auto& prefixBin = *entry.next_hop()->prefix_bin();
    if (prefixBin.empty()) {
      continue;
    }

    const auto reachable = *entry.is_reachable() ? "Yes" : "No";
    const auto igpCost = entry.igp_cost().has_value()
        ? folly::to<std::string>(*entry.igp_cost())
        : "N/A";

    table.addRow(
        {nexthopIpStr(entry),
         reachable,
         igpCost,
         *entry.is_resolved_for_selection() ? "Yes" : "No",
         folly::to<std::string>(*entry.route_count())});
    ++rows;
  }

  if (rows == 0) {
    out << "No nexthop cache entries found" << std::endl;
    return;
  }

  out << table << std::endl;
}

std::string_view CmdShowBgpNexthopInfoTraits::description() {
  return "Displays the next hops tracked by the BGP RIB. Reachable is the status reported by the resolver. Resolved for selection also requires an IGP cost unless the platform allows selection without one. With no argument it lists every tracked next hop with its reachability, IGP cost, selection state, and number of dependent BGP paths. Pass one or more addresses for a per-next-hop view that adds whether the next hop is directly connected and how long ago its reachability and IGP cost last changed; an address that is not tracked is reported by name rather than silently skipped. A next hop that is reachable but not resolved for selection is the case to look for when a path is present in 'show bgp table' yet never becomes best. If the RIB is tracking no next hops, the command prints 'No nexthop cache entries found', which is a normal state rather than an error.";
}

CmdShowBgpNexthopInfo::RetType CmdShowBgpNexthopInfo::sampleModel() {
  /*
   * Reachability, direct connection and resolved-for-selection are distinct
   * properties. igpCost and igpCostChangeAge are optional so an unresolved next
   * hop can render "N/A" without also claiming a cost changed.
   */
  auto nexthop = [](const std::string& address,
                    bool reachable,
                    bool isConnected,
                    std::optional<int32_t> igpCost,
                    bool isResolvedForSelection,
                    int64_t routeCount) {
    TNexthopInfo info;
    info.next_hop() = sampleIpPrefix(address);
    info.is_reachable() = reachable;
    if (igpCost.has_value()) {
      info.igp_cost() = *igpCost;
      info.last_igp_cost_change_age_s() = 39654;
    }
    info.is_connected() = isConnected;
    info.is_resolved_for_selection() = isResolvedForSelection;
    info.route_count() = routeCount;
    info.last_reachability_change_age_s() = 39654;
    return info;
  };

  RetType result;
  result.detailed() = false;
  result.entries() = {
      // Healthy multi-hop next hop: reachable, resolved, carrying the routes
      // that depend on it. Not directly connected, hence the non-zero cost.
      nexthop(
          "192.0.2.11",
          /*reachable=*/true,
          /*isConnected=*/false,
          /*igpCost=*/10,
          /*isResolvedForSelection=*/true,
          171),
      // Reachable without a cost, but not resolved for selection.
      nexthop(
          "192.0.2.12",
          /*reachable=*/true,
          /*isConnected=*/false,
          /*igpCost=*/std::nullopt,
          /*isResolvedForSelection=*/false,
          171),
      // Directly connected and unreachable: no IGP cost, so the cost column
      // renders "N/A" and no cost-change age is claimed.
      nexthop(
          "192.0.2.13",
          /*reachable=*/false,
          /*isConnected=*/true,
          /*igpCost=*/std::nullopt,
          /*isResolvedForSelection=*/false,
          0)};
  return result;
}

template void
CmdHandler<CmdShowBgpNexthopInfo, CmdShowBgpNexthopInfoTraits>::run();

} // namespace facebook::fboss
