/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <string_view>
#include <unordered_set>
#include <vector>

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/bgp/summary/gen-cpp2/bgp_summary_types.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace facebook::fboss {
using apache::thrift::util::enumNameSafe;
using facebook::fboss::utils::Table;
using facebook::neteng::fboss::bgp::thrift::TBgpPeerState;
using facebook::neteng::fboss::bgp::thrift::TBgpSession;
using facebook::neteng::fboss::bgp::thrift::TPeerEgressStats;

enum PeerMetric {
  PrefixesRcvd = 0,
  PrefixesSent = 1,
  UpdatesRcvd = 2,
  UpdatesSent = 3,
  SuppressedUpdates = 4,
  TotalAdjRibOutQueueBlocks = 5,
  TotalAdjRibOutQueueWait = 6,
  LastAdjRibOutQueueBlock = 7,
  TotalSendQueueBlocks = 8,
  TotalSendQueueWait = 9,
  LastSendQueueBlock = 10,
  TotalSocketBuffered = 11,
  LastSocketBuffered = 12,
  MAX_VALUE,
};

inline const std::string kNoGroupName = "NONE";

inline const std::unordered_set<PeerMetric> kTimeMetrics = {
    LastAdjRibOutQueueBlock,
    LastSendQueueBlock,
    LastSocketBuffered,
};

struct CmdShowBgpSummaryEgressTraits : public ReadCommandTraits {
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = cli::ShowBgpSummaryModel;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description();
};

class CmdShowBgpSummaryEgress : public CmdHandler<
                                    CmdShowBgpSummaryEgress,
                                    CmdShowBgpSummaryEgressTraits> {
 public:
  using RetType = CmdShowBgpSummaryEgressTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo);
  void printOutput(const RetType& model, std::ostream& out = std::cout);

  // Canned, synthetic model (no real switch data) used to render a
  // deterministic example for the CLI reference wiki. No live switch.
  static RetType sampleModel();
  Table makePeerTable(const std::vector<TPeerEgressStats>& peerEgressStats);
  void printPeerSummary(
      const std::vector<TPeerEgressStats>& peerEgressStats,
      std::ostream& out);
  Table makeGroupTable(const std::vector<TPeerEgressStats>& peerEgressStats);
  void printGroupSummary(
      const std::vector<TPeerEgressStats>& peerEgressStats,
      std::ostream& out);
  RetType createModel(std::vector<TPeerEgressStats>& peerEgressStats);
};

} // namespace facebook::fboss
