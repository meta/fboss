/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/clear/CmdClearInterfaceCounters.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <algorithm>

#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/cli/fboss2/utils/CmdClientUtils.h"

namespace facebook::fboss {

namespace {
void clearCounters(
    apache::thrift::Client<FbossCtrl>& client,
    std::vector<int32_t> ports) {
  if (ports.size()) {
    client.sync_clearPortStats(ports);
  } else {
    client.sync_clearAllPortStats();
  }
}
} // namespace

std::string CmdClearInterfaceCounters::queryClient(
    const HostInfo& hostInfo,
    std::vector<std::string> queriedIfs) {
  std::map<int32_t, facebook::fboss::PortInfoThrift> portEntries;
  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);

  std::vector<int32_t> portsToClear;
  /*
    if the user has provided specific interfaces to clear then find the
    corresponding port names, otherwise empty vector means clear all counters
  */
  if (queriedIfs.size()) {
    client->sync_getAllPortInfo(portEntries);
    for (auto queriedIf : queriedIfs) {
      auto it = std::find_if(
          portEntries.begin(),
          portEntries.end(),
          [&queriedIf](const auto& port) {
            return port.second.name().value() == queriedIf;
          });
      if (it != portEntries.end()) {
        portsToClear.push_back(it->first);
      } else {
        throw std::runtime_error(
            fmt::format(
                "{} is not a valid interface name on this device", queriedIf));
      }
    }
  }
  clearCounters(*client, portsToClear);

  return "Counters cleared.";
}

void CmdClearInterfaceCounters::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdClearInterfaceCounters, CmdClearInterfaceCountersTraits>::run();
template const ValidFilterMapType CmdHandler<
    CmdClearInterfaceCounters,
    CmdClearInterfaceCountersTraits>::getValidFilters();

} // namespace facebook::fboss
