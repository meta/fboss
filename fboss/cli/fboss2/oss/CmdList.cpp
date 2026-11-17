// (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/CmdList.h"

#include "fboss/cli/fboss2/commands/show/bgp/config/CmdShowConfigRunningBgp.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/CmdShowBgpNeighbors.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/advertised/BgpNeighborsAdvertisedPostPolicy.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/advertised/BgpNeighborsAdvertisedPrePolicy.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/advertised/BgpNeighborsAdvertisedRejected.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/received/BgpNeighborsReceivedPostPolicy.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/received/BgpNeighborsReceivedPrePolicy.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/received/BgpNeighborsReceivedRejected.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/session_id/CmdBgpNeighborsSessionId.h"
#include "fboss/cli/fboss2/commands/show/bgp/policy/CmdShowBgpPolicy.h"
#include "fboss/cli/fboss2/commands/show/bgp/profiler/CmdShowBgpProfiler.h"
#include "fboss/cli/fboss2/commands/show/bgp/table/CmdShowBgpTable.h"
#include "fboss/cli/fboss2/commands/show/bgp/table/CmdShowBgpTableCommunity.h"
#include "fboss/cli/fboss2/commands/show/bgp/table/CmdShowBgpTableDetail.h"
#include "fboss/cli/fboss2/commands/show/bgp/table/CmdShowBgpTableMoreSpecifics.h"
#include "fboss/cli/fboss2/commands/show/bgp/table/CmdShowBgpTablePrefix.h"
#include "fboss/cli/fboss2/commands/show/bgp/table/CmdShowBgpTableSummary.h"
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

        {"neighbors",
         "Show BGP neighbors",
         commandHandler<CmdShowBgpNeighbors>,
         argTypeHandler<CmdShowBgpNeighborsTraits>,
         {{"advertised",
           "Show BGP advertised routes",
           {{"post-policy",
             "Show BGP advertised routes (post-policy)",
             commandHandler<BgpNeighborsAdvertisedPostPolicy>,
             argTypeHandler<BgpNeighborsAdvertisedPostPolicyTraits>},
            {"pre-policy",
             "Show BGP advertised routes (pre-policy)",
             commandHandler<BgpNeighborsAdvertisedPrePolicy>,
             argTypeHandler<BgpNeighborsAdvertisedPrePolicyTraits>},
            {"rejected",
             "Show BGP advertised rejected routes",
             commandHandler<BgpNeighborsAdvertisedRejected>,
             argTypeHandler<BgpNeighborsAdvertisedRejectedTraits>}}},
          {"received",
           "Show BGP received routes",
           {{"post-policy",
             "Show BGP received routes (post-policy)",
             commandHandler<BgpNeighborsReceivedPostPolicy>,
             argTypeHandler<BgpNeighborsReceivedPostPolicyTraits>},
            {"pre-policy",
             "Show BGP received routes (pre-policy)",
             commandHandler<BgpNeighborsReceivedPrePolicy>,
             argTypeHandler<BgpNeighborsReceivedPrePolicyTraits>},
            {"rejected",
             "Show BGP received rejected routes",
             commandHandler<BgpNeighborsReceivedRejected>,
             argTypeHandler<BgpNeighborsReceivedRejectedTraits>}}},
          {"session-id",
           "Show BGP neighbor by session ID",
           commandHandler<CmdBgpNeighborsSessionId>,
           argTypeHandler<CmdBgpNeighborsSessionIdTraits>}}},

        {"policy",
         "Show BGP policy by name",
         commandHandler<CmdShowBgpPolicy>,
         argTypeHandler<CmdShowBgpPolicyTraits>},

        {"profiler",
         "Show BGP profiler stats",
         commandHandler<CmdShowBgpProfiler>,
         argTypeHandler<CmdShowBgpProfilerTraits>},

        {"table",
         "Show BGP routing table",
         commandHandler<CmdShowBgpTable>,
         argTypeHandler<CmdShowBgpTableTraits>,
         {{"community",
           "Show BGP routes by community",
           commandHandler<CmdShowBgpTableCommunity>,
           argTypeHandler<CmdShowBgpTableCommunityTraits>},
          {"detail",
           "Show BGP table details",
           commandHandler<CmdShowBgpTableDetail>,
           argTypeHandler<CmdShowBgpTableDetailTraits>},
          {"more-specifics",
           "Show BGP more specific routes",
           commandHandler<CmdShowBgpTableMoreSpecifics>,
           argTypeHandler<CmdShowBgpTableMoreSpecificsTraits>},
          {"prefix",
           "Show BGP routes by prefix",
           commandHandler<CmdShowBgpTablePrefix>,
           argTypeHandler<CmdShowBgpTablePrefixTraits>},
          {"summary",
           "Show BGP RIB summary (per-afi aggregate and per-masklength counts)",
           commandHandler<CmdShowBgpTableSummary>,
           argTypeHandler<CmdShowBgpTableSummaryTraits>}}}}},

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
