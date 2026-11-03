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

#include <fmt/core.h>
#include <iostream>

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace facebook::fboss {

using facebook::neteng::fboss::bgp::thrift::TBgpProfilerStat;

struct CmdShowBgpProfilerTraits : public ReadCommandTraits {
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = std::vector<TBgpProfilerStat>;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Displays per-function latency statistics collected by the BGP coroutine profiler: for each profiled function, how many times it ran and the p50, p90, p99, maximum and cumulative wall time in milliseconds. Read the columns together rather than one at a time - a high total against a small p99 is a function called often and cheaply, whereas a p99 far above the p50 is a function that is usually fast but occasionally stalls, which is the shape that causes convergence hiccups. The profiler is off unless it was turned on, so an unprofiled switch prints a message saying so along with how to enable it: 'start bgp profiler' at runtime, or '--bgp_coro_profiler_enabled=true' at startup. The same message appears when the profiler is on but nothing profiled has run yet, so seeing it does not by itself mean the flag failed to take. Counters accumulate from the moment profiling starts; use 'clear bgp profiler' to reset them before timing a specific event. Takes no arguments.";
  }
};

class CmdShowBgpProfiler
    : public CmdHandler<CmdShowBgpProfiler, CmdShowBgpProfilerTraits> {
 public:
  using RetType = CmdShowBgpProfilerTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo);

  void printOutput(const RetType& stats, std::ostream& out = std::cout);

  /*
   * Canned, synthetic model (no real switch data) used to render a
   * deterministic example for the CLI reference wiki. Defined inline rather
   * than in the .cpp: that TU lives only in the fboss2-routing-protocol
   * target, which the test targets do not link, so an out-of-line definition
   * is invisible to the wiki tests.
   */
  static RetType sampleModel() {
    /*
     * Three rows chosen to show the reading the prose describes: a hot, cheap
     * function whose cost is in its call count; one with a p99 an order of
     * magnitude above its p50, the occasional-stall shape; and a rare,
     * uniformly expensive one. Rows render in the order given, not sorted.
     */
    auto stat = [](const std::string& name,
                   int64_t count,
                   int64_t p50,
                   int64_t p90,
                   int64_t p99,
                   int64_t max,
                   int64_t total) {
      TBgpProfilerStat profilerStat;
      profilerStat.name() = name;
      profilerStat.count() = count;
      profilerStat.p50_ms() = p50;
      profilerStat.p90_ms() = p90;
      profilerStat.p99_ms() = p99;
      profilerStat.max_ms() = max;
      profilerStat.total_ms() = total;
      return profilerStat;
    };

    return {
        stat("BgpRib::processUpdate", 148302, 0, 1, 2, 14, 62418),
        stat("BgpPeer::sendUpdateBatch", 9471, 1, 3, 47, 214, 38820),
        stat("BgpRib::recomputeBestPaths", 62, 118, 143, 166, 171, 7394)};
  }
};

} // namespace facebook::fboss
