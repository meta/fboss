/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "CmdShowMacDetails.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fboss/agent/if/gen-cpp2/ctrl_types.h>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "fboss/cli/fboss2/utils/CmdClientUtils.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fmt/format.h"

namespace facebook::fboss {

using RetType = CmdShowMacDetails::RetType;

RetType CmdShowMacDetails::queryClient(const HostInfo& hostInfo) {
  std::vector<facebook::fboss::L2EntryThrift> entries;
  std::map<int32_t, facebook::fboss::PortInfoThrift> portEntries;
  std::vector<facebook::fboss::AggregatePortThrift> aggPortentries;
  auto client =
      utils::createClient<facebook::fboss::FbossCtrlAsyncClient>(hostInfo);

  client->sync_getL2Table(entries);
  client->sync_getAllPortInfo(portEntries);
  client->sync_getAggregatePortTable(aggPortentries);
  return createModel(entries, portEntries, aggPortentries);
}

void CmdShowMacDetails::printOutput(const RetType& model, std::ostream& out) {
  constexpr auto fmtString = "{:<24}{:<19}{:<14}{:<19}{:<14}\n";
  out << fmt::format(
      fmtString, "MAC Address", "Port/Trunk", "VLAN", "TYPE", "CLASSID");

  for (const auto& entry : model.l2Entries().value()) {
    out << fmt::format(
        fmtString,
        entry.mac().value(),
        entry.ifName().value(),
        folly::copy(entry.vlanID().value()),
        entry.l2EntryType().value(),
        entry.classID().value());
  }
  out << std::endl;
}

RetType CmdShowMacDetails::createModel(
    std::vector<facebook::fboss::L2EntryThrift>& l2Entries,
    std::map<int32_t, facebook::fboss::PortInfoThrift>& portEntries,
    std::vector<facebook::fboss::AggregatePortThrift>& aggregatePortEntries) {
  RetType model;

  for (const auto& entry : l2Entries) {
    cli::L2Entry l2Details;

    l2Details.mac() = entry.mac().value();
    l2Details.port() = folly::copy(entry.port().value());
    l2Details.vlanID() = folly::copy(entry.vlanID().value());
    l2Details.l2EntryType() =
        utils::getl2EntryTypeStr(entry.l2EntryType().value());
    auto trunkPtr = apache::thrift::get_pointer(entry.trunk());
    if (trunkPtr != nullptr) {
      l2Details.trunk() = *trunkPtr;
      std::vector<facebook::fboss::AggregatePortThrift> aggPortEntries;
      for (const auto& agg_port : aggregatePortEntries) {
        if (agg_port.key().value() == *trunkPtr) {
          aggPortEntries.push_back(agg_port);
        }
      }
      if (aggPortEntries.size() == 1) {
        l2Details.ifName() = aggPortEntries[0].name().value();
      } else {
        l2Details.ifName() = std::to_string(*trunkPtr) + " (Trunk)";
      }
    } else {
      l2Details.ifName() =
          portEntries[folly::copy(entry.port().value())].name().value();
    }
    auto classIdPtr = apache::thrift::get_pointer(entry.classID());
    l2Details.classID() =
        (classIdPtr != nullptr) ? std::to_string(*classIdPtr) : "-";

    model.l2Entries()->push_back(l2Details);
  }
  return model;
}

std::string_view CmdShowMacDetailsTraits::description() {
  return "Displays the switch's L2 MAC address table: each learned MAC, the port or trunk and VLAN it was learned on, the entry type, and any class ID. Use it to verify L2 learning and locate where a host is connected.";
}

CmdShowMacDetails::RetType CmdShowMacDetails::sampleModel() {
  RetType model;

  cli::L2Entry entry1;
  entry1.mac() = "02:00:11:22:33:01";
  entry1.ifName() = "eth1/37/5";
  entry1.vlanID() = 2074;
  entry1.l2EntryType() = "Validated";
  entry1.classID() = "-";

  cli::L2Entry entry2;
  entry2.mac() = "02:00:11:22:33:02";
  entry2.ifName() = "eth1/37/1";
  entry2.vlanID() = 2073;
  entry2.l2EntryType() = "Validated";
  entry2.classID() = "-";

  cli::L2Entry entry3;
  entry3.mac() = "02:00:11:22:33:03";
  entry3.ifName() = "eth1/25/1";
  entry3.vlanID() = 2049;
  entry3.l2EntryType() = "Validated";
  entry3.classID() = "-";

  model.l2Entries() = {entry1, entry2, entry3};
  return model;
}

// Explicit template instantiation
template void CmdHandler<CmdShowMacDetails, CmdShowMacDetailsTraits>::run();

} // namespace facebook::fboss
