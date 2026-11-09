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

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/CmdLocalOptions.h"
#include "fboss/cli/fboss2/commands/show/fb303counters/gen-cpp2/model_types.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace facebook::fboss {

inline const std::string kFb303CountersServiceOpt = "--service";
inline const std::string kFb303CountersRegexOpt = "--regex";

struct CmdShowFb303CountersTraits : public ReadCommandTraits {
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = cli::ShowFb303CountersModel;
  static constexpr bool ALLOW_FILTERING = true;
  static constexpr bool ALLOW_AGGREGATION = true;

  std::vector<utils::LocalOption> LocalOptions = {
      {kFb303CountersServiceOpt,
       "Service to query. One of agent, fsdb, qsfp, led, sensor, data_corral, "
       "fan, rackmon, platform_manager, bgp. The platform services expose "
       "counters only when built internally; agent, qsfp, fsdb and bgp work "
       "in either build.",
       "agent"},
      {kFb303CountersRegexOpt,
       "Return only counters whose name contains a match for this regex, "
       "filtered server side. Searches rather than full-matches, so a bare "
       "substring like 'link_state\\.flap' works; anchor with ^ and $ for an "
       "exact name. Empty returns every counter.",
       ""},
  };

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description();
};

class CmdShowFb303Counters
    : public CmdHandler<CmdShowFb303Counters, CmdShowFb303CountersTraits> {
 public:
  using RetType = CmdShowFb303CountersTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo);
  void printOutput(const RetType& model, std::ostream& out = std::cout);

  // Canned, synthetic model (no real switch data) used to render a
  // deterministic example for the CLI reference wiki. No live switch.
  static RetType sampleModel();

  // Exposed for testing: turns per-source counter maps into the flat model.
  static RetType createModel(
      const std::vector<std::pair<std::string, std::map<std::string, int64_t>>>&
          bySource);
};

} // namespace facebook::fboss
