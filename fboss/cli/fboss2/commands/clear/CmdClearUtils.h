// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"

namespace facebook::fboss::utils {

template <typename NeighborEntry>
std::string flushNeighborEntries(
    std::unique_ptr<apache::thrift::Client<FbossCtrl>>& agent,
    const std::vector<NeighborEntry>& allNeighbors,
    const utils::CIDRNetwork& networkFilter) {
  static_assert(
      std::is_same_v<NeighborEntry, NdpEntryThrift> ||
      std::is_same_v<NeighborEntry, ArpEntryThrift>);

  std::string nbrTypeStr;
  if constexpr (std::is_same_v<NeighborEntry, NdpEntryThrift>) {
    nbrTypeStr = "Ndp";
  } else {
    nbrTypeStr = "Arp";
  }

  std::vector<IfAndIP> entriesToFlush;

  for (const auto& entry : allNeighbors) {
    // STATIC or DYNAMIC entries cannot be flushed.
    if (entry.get_state() == "STATIC" || entry.get_state() == "DYNAMIC") {
      continue;
    }

    auto ip = folly::IPAddress::fromBinary(
        folly::ByteRange(folly::StringPiece(entry.get_ip().addr().value())));
    auto shouldFlush = networkFilter.empty() ||
        std::find_if(
            networkFilter.begin(),
            networkFilter.end(),
            [&ip](const auto& network) {
              return ip.inSubnet(network.first, network.second);
            }) != networkFilter.end();
    if (shouldFlush) {
      std::cout << fmt::format(
          "Deleting {} entry ip: {} interfaceID: {}\n",
          nbrTypeStr,
          ip.str(),
          entry.get_interfaceID());

      IfAndIP flushEntry;
      flushEntry.ip() = entry.get_ip();
      flushEntry.interfaceID() = entry.get_interfaceID();
      entriesToFlush.emplace_back(std::move(flushEntry));
    }
  }

  auto numFlushed = agent->sync_flushNeighborEntries(entriesToFlush);
  return folly::to<std::string>("Flushed ", numFlushed, " entries");
}

} // namespace facebook::fboss::utils
