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
#ifndef IS_OSS
#include "common/thrift/thrift/gen-cpp2/MonitorAsyncClient.h"
#endif
#include "fboss/agent/if/gen-cpp2/FbossHwCtrl.h"
#include "fboss/cli/fboss2/CmdGlobalOptions.h"
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/interface/CmdShowInterface.h"
#include "fboss/cli/fboss2/commands/show/interface/traffic/gen-cpp2/model_types.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"
#include "fboss/cli/fboss2/utils/Table.h"

namespace facebook::fboss {

using utils::Table;

struct CmdShowInterfaceTrafficTraits : public ReadCommandTraits {
  using ParentCmd = CmdShowInterface;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = cli::InterfaceTrafficModel;
  static constexpr bool ALLOW_FILTERING = true;
  static constexpr bool ALLOW_AGGREGATION = true;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description();
};

class CmdShowInterfaceTraffic : public CmdHandler<
                                    CmdShowInterfaceTraffic,
                                    CmdShowInterfaceTrafficTraits> {
 public:
  using ObjectArgType = CmdShowInterfaceTrafficTraits::ObjectArgType;
  using RetType = CmdShowInterfaceTrafficTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const std::vector<std::string>& queriedIfs);
  RetType createModel(
      const std::map<int32_t, facebook::fboss::PortInfoThrift>& portCounters,
      std::map<std::string, int64_t> intCounters,
      const std::vector<std::string>& queriedIfs);
  double calculateUtilizationPercent(double speedMbps, int bandwidth);
  std::string extractExpectedPort(const std::string& portDescription);
  bool isInterestingTraffic(cli::TrafficCounters& tc);
  bool isNonZeroErrors(cli::TrafficErrorCounters& ec);
  std::vector<double> getTrafficTotals(
      std::vector<cli::TrafficCounters> trafficCounters);
  std::string getThresholdColor(double pct);
  std::vector<std::string> getRowColors(double inPct, double outPct);
  Table::StyledCell makeColorCell(
      const std::string& val,
      const std::string& level);
  void printOutput(const RetType& model, std::ostream& out = std::cout);

  // Canned, synthetic model (no real switch data) used to render a
  // deterministic example for the CLI reference wiki. No live switch.
  static RetType sampleModel();
};

} // namespace facebook::fboss
