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
  std::string deletingLines;
  // INCOMPLETE entries are flushed too, but the agent only counts an entry it
  // removes from switch state, and an unresolved entry is usually only in the
  // neighbor cache. List them separately so the output doesn't announce
  // deletions that the flushed count won't include.
  std::string unresolvedLines;
  size_t numUnresolved = 0;

  for (const auto& entry : allNeighbors) {
    // STATIC or DYNAMIC entries cannot be flushed.
    if (entry.state().value() == "STATIC" ||
        entry.state().value() == "DYNAMIC") {
      continue;
    }

    auto ip = folly::IPAddress::fromBinary(
        folly::ByteRange(folly::StringPiece(entry.ip()->addr().value())));
    auto shouldFlush = networkFilter.empty() ||
        std::find_if(
            networkFilter.begin(),
            networkFilter.end(),
            [&ip](const auto& network) {
              return ip.inSubnet(network.first, network.second);
            }) != networkFilter.end();
    if (shouldFlush) {
      if (entry.state().value() == "INCOMPLETE") {
        unresolvedLines += fmt::format(
            "  ip: {} interfaceID: {}\n",
            ip.str(),
            folly::copy(entry.interfaceID().value()));
        ++numUnresolved;
      } else {
        deletingLines += fmt::format(
            "Deleting {} entry ip: {} interfaceID: {}\n",
            nbrTypeStr,
            ip.str(),
            folly::copy(entry.interfaceID().value()));
      }

      IfAndIP flushEntry;
      flushEntry.ip() = entry.ip().value();
      flushEntry.interfaceID() = folly::copy(entry.interfaceID().value());
      entriesToFlush.emplace_back(std::move(flushEntry));
    }
  }

  auto numFlushed = agent->sync_flushNeighborEntries(entriesToFlush);
  auto result = fmt::format(
      "{}Flushed {} {}",
      deletingLines,
      numFlushed,
      numFlushed == 1 ? "entry" : "entries");
  if (numUnresolved > 0) {
    result += fmt::format(
        "\nNote: {} unresolved (INCOMPLETE) {} {} also sent for flush; "
        "counted above only if programmed in hardware:\n{}",
        numUnresolved,
        nbrTypeStr,
        numUnresolved == 1 ? "entry was" : "entries were",
        unresolvedLines);
    result.pop_back(); // printOutput adds the final newline
  }
  return result;
}

} // namespace facebook::fboss::utils
