// (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/CmdList.h"

#include "fboss/cli/fboss2/commands/show/bgp/config/CmdShowConfigRunningBgp.h"
#include "fboss/cli/fboss2/commands/show/bgp/policy/CmdShowBgpPolicy.h"
#include "fboss/cli/fboss2/commands/show/bgp/profiler/CmdShowBgpProfiler.h"
#include "fboss/cli/fboss2/commands/show/config/CmdShowConfigHistoryAgent.h"
#include "fboss/cli/fboss2/commands/show/config/CmdShowConfigTraits.h"
#include "fboss/cli/fboss2/commands/show/version/CmdShowVersionDistro.h"

namespace facebook::fboss {

const CommandTree& kBaseAdditionalCommandTree() {
  static CommandTree tree = {
      {"show",
       "version",
       "Show the local FBOSS distro image version summary",
       commandHandler<CmdShowVersionDistro>,
       validFilterHandler<CmdShowVersionDistro>,
       argTypeHandler<CmdShowVersionTraits>,
       {{"detail",
         "Show the complete FBOSS distro image version record",
         commandHandler<CmdShowVersionDistroDetail>,
         validFilterHandler<CmdShowVersionDistroDetail>,
         argTypeHandler<CmdShowVersionTraits>}}},
      {"show",
       "bgp",
       "Show BGP information",
       {{"config",
         "Show BGP configuration",
         {{"running",
           "Show running BGP configuration",
           commandHandler<CmdShowConfigRunningBgp>,
           argTypeHandler<CmdShowConfigRunningBgpTraits>}}},

        {"policy",
         "Show BGP policy by name",
         commandHandler<CmdShowBgpPolicy>,
         argTypeHandler<CmdShowBgpPolicyTraits>},

        {"profiler",
         "Show BGP profiler stats",
         commandHandler<CmdShowBgpProfiler>,
         argTypeHandler<CmdShowBgpProfilerTraits>}}},

      {"show",
       "config",
       "Show config info for various binaries",
       {{"history",
         "Show history of changes to the current config",
         {{"agent",
           "Show history of changes to the current Agent config",
           commandHandler<CmdShowConfigHistoryAgent>,
           argRegistrar<CmdShowConfigTraits>}}}}},
  };
  return tree;
}

} // namespace facebook::fboss
