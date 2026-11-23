/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "CmdShowDsfNodes.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fboss/agent/if/gen-cpp2/ctrl_types.h>
#include <fmt/format.h>
#include <folly/String.h>
#include <re2/re2.h>
#include <map>
#include <string>
#include <vector>
#include "fboss/cli/fboss2/utils/CmdClientUtils.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "folly/Conv.h"

namespace facebook::fboss {

using utils::Table;
using RetType = CmdShowDsfNodesTraits::RetType;

RetType CmdShowDsfNodes::queryClient(const HostInfo& hostInfo) {
  auto client =
      utils::createClient<facebook::fboss::FbossCtrlAsyncClient>(hostInfo);
  std::map<int64_t, cfg::DsfNode> entries;
  client->sync_getDsfNodes(entries);
  return createModel(entries);
}

void CmdShowDsfNodes::printOutput(const RetType& model, std::ostream& out) {
  Table table;
  table.setHeader({
      "Name",
      "Switch Id",
      "Type",
      "System port ranges",
  });

  for (auto const& entry : model.dsfNodes().value()) {
    table.addRow({
        *entry.name(),
        folly::to<std::string>(*entry.switchId()),
        *entry.type(),
        *entry.systemPortRanges(),
    });
  }
  out << table << std::endl;
}

RetType CmdShowDsfNodes::createModel(
    const std::map<int64_t, cfg::DsfNode>& dsfNodes) {
  RetType model;
  const std::string kUnavail;
  for (const auto& idAndNode : dsfNodes) {
    const auto& node = idAndNode.second;
    cli::DsfNodeEntry entry;
    entry.name() = *node.name();
    entry.switchId() = *node.switchId();
    entry.type() =
        (node.type() == cfg::DsfNodeType::INTERFACE_NODE ? "Intf Node"
                                                         : "Fabric Node");
    std::vector<std::string> ranges;
    if (node.systemPortRanges()->systemPortRanges()->size()) {
      for (const auto& range : *node.systemPortRanges()->systemPortRanges()) {
        ranges.push_back(
            fmt::format("({}, {})", *range.minimum(), *range.maximum()));
      }
      entry.systemPortRanges() = folly::join(", ", ranges);
    } else {
      entry.systemPortRanges() = "--";
    }
    model.dsfNodes()->push_back(entry);
  }
  return model;
}

std::string_view CmdShowDsfNodesTraits::description() {
  return "Displays the DSF nodes known to the switch: each node's name, switch ID, type (interface node or fabric node), and system port ID ranges (interface nodes only). DSF-only — applies to DSF switches and returns data only in a DSF topology.";
}

RetType CmdShowDsfNodes::sampleModel() {
  RetType model;

  cli::DsfNodeEntry entry1;
  entry1.name() = "rdsw001";
  entry1.switchId() = 0;
  entry1.type() = "Intf Node";
  entry1.systemPortRanges() = "(185, 212), (16569, 16596)";
  model.dsfNodes()->push_back(entry1);

  cli::DsfNodeEntry entry2;
  entry2.name() = "rdsw002";
  entry2.switchId() = 4;
  entry2.type() = "Intf Node";
  entry2.systemPortRanges() = "(213, 240), (16597, 16624)";
  model.dsfNodes()->push_back(entry2);

  cli::DsfNodeEntry entry3;
  entry3.name() = "fdsw001";
  entry3.switchId() = 2560;
  entry3.type() = "Fabric Node";
  entry3.systemPortRanges() = "--";
  model.dsfNodes()->push_back(entry3);

  cli::DsfNodeEntry entry4;
  entry4.name() = "fdsw002";
  entry4.switchId() = 2564;
  entry4.type() = "Fabric Node";
  entry4.systemPortRanges() = "--";
  model.dsfNodes()->push_back(entry4);

  return model;
}

// Explicit template instantiation
template void CmdHandler<CmdShowDsfNodes, CmdShowDsfNodesTraits>::run();
template const ValidFilterMapType
CmdHandler<CmdShowDsfNodes, CmdShowDsfNodesTraits>::getValidFilters();
} // namespace facebook::fboss
