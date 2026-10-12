// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/CmdList.h"

#include "fboss/cli/fboss2/commands/show/bgp/CmdShowBgpInitializationEvents.h"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowBgpOriginatedRoutes.h"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowVersionBgp.h"
#include "fboss/cli/fboss2/commands/show/bgp/changelist/CmdShowBgpChangelist.h"
#include "fboss/cli/fboss2/commands/show/bgp/config/CmdShowConfigRunningBgp.h"
#include "fboss/cli/fboss2/commands/show/bgp/fibnexthops/CmdShowBgpFibNexthops.h"
#include "fboss/cli/fboss2/commands/show/bgp/health/CmdShowBgpHealth.h"
#include "fboss/cli/fboss2/commands/show/bgp/holdtimers/CmdShowBgpHoldTimers.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/CmdShowBgpNeighbors.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/advertised/BgpNeighborsAdvertisedPostPolicy.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/advertised/BgpNeighborsAdvertisedPrePolicy.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/advertised/BgpNeighborsAdvertisedRejected.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/received/BgpNeighborsReceivedPostPolicy.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/received/BgpNeighborsReceivedPrePolicy.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/received/BgpNeighborsReceivedRejected.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/session_id/CmdBgpNeighborsSessionId.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors_by_name/CmdShowBgpNeighborsByName.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors_by_name/advertised/BgpNeighborsByNameAdvertisedRejected.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors_by_name/advertised/BgpNeighborsByNameAdvertisedRejectedCrf.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors_by_name/received/BgpNeighborsByNameReceivedRejected.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors_by_name/received/BgpNeighborsByNameReceivedRejectedCrf.h"
#include "fboss/cli/fboss2/commands/show/bgp/nexthopinfo/CmdShowBgpNexthopInfo.h"
#include "fboss/cli/fboss2/commands/show/bgp/policy/CmdShowBgpPolicy.h"
#include "fboss/cli/fboss2/commands/show/bgp/profiler/CmdShowBgpProfiler.h"
#include "fboss/cli/fboss2/commands/show/bgp/shadowrib/CmdShowBgpShadowRib.h"
#include "fboss/cli/fboss2/commands/show/bgp/stats/CmdShowBgpStatsAttrs.h"
#include "fboss/cli/fboss2/commands/show/bgp/stats/CmdShowBgpStatsEntries.h"
#include "fboss/cli/fboss2/commands/show/bgp/stats/CmdShowBgpStatsPolicy.h"
#include "fboss/cli/fboss2/commands/show/bgp/stream/CmdShowBgpStreamSubscriber.h"
#include "fboss/cli/fboss2/commands/show/bgp/stream/CmdShowBgpStreamSummary.h"
#include "fboss/cli/fboss2/commands/show/bgp/stream/subscriber/CmdShowBgpStreamSubscriberPostPolicy.h"
#include "fboss/cli/fboss2/commands/show/bgp/stream/subscriber/CmdShowBgpStreamSubscriberPrePolicy.h"
#include "fboss/cli/fboss2/commands/show/bgp/summary/CmdShowBgpSummary.h"
#include "fboss/cli/fboss2/commands/show/bgp/summary/egress/CmdShowBgpSummaryEgress.h"
#include "fboss/cli/fboss2/commands/show/bgp/table/CmdShowBgpTable.h"
#include "fboss/cli/fboss2/commands/show/bgp/table/CmdShowBgpTableCommunity.h"
#include "fboss/cli/fboss2/commands/show/bgp/table/CmdShowBgpTableDetail.h"
#include "fboss/cli/fboss2/commands/show/bgp/table/CmdShowBgpTableMoreSpecifics.h"
#include "fboss/cli/fboss2/commands/show/bgp/table/CmdShowBgpTablePrefix.h"
#include "fboss/cli/fboss2/commands/show/bgp/table/CmdShowBgpTableSummary.h"
#include "fboss/cli/fboss2/commands/show/bgp/updategroup/CmdShowBgpUpdateGroup.h"
#include "fboss/cli/fboss2/commands/show/config/CmdShowConfigHistoryAgent.h"
#include "fboss/cli/fboss2/commands/show/config/CmdShowConfigRunningAgent.h"
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
       {{"changelist",
         "Show BGP changelist",
         commandHandler<CmdShowBgpChangelist>,
         argTypeHandler<CmdShowBgpChangelistTraits>},

        {"fib-nexthops",
         "Show unique nexthop sets in submitted BGP FIB-out state",
         commandHandler<CmdShowBgpFibNexthops>,
         argTypeHandler<CmdShowBgpFibNexthopsTraits>},

        {"config",
         "Show BGP configuration",
         {{"running",
           "Show running BGP configuration",
           commandHandler<CmdShowConfigRunningBgp>,
           argTypeHandler<CmdShowConfigRunningBgpTraits>}}},

        {"health",
         "Show BGP health report",
         commandHandler<CmdShowBgpHealth>,
         argTypeHandler<CmdShowBgpHealthTraits>},

        {"holdtimers",
         "Show BGP peer hold timer information",
         commandHandler<CmdShowBgpHoldTimers>,
         argTypeHandler<CmdShowBgpHoldTimersTraits>},

        {"initialization-events",
         "Show BGP initialization events",
         commandHandler<CmdShowBgpInitializationEvents>,
         argTypeHandler<CmdShowBgpInitializationEventsTraits>},

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

        {"neighbors-by-name",
         "Show BGP neighbor information by name regex match",
         commandHandler<CmdShowBgpNeighborsByName>,
         argTypeHandler<CmdShowBgpNeighborsByNameTraits>,
         {{"advertised",
           "Routes advertised to matching peers",
           {{"rejected",
             "Rejected routes grouped by prefix",
             commandHandler<BgpNeighborsByNameAdvertisedRejected>,
             argTypeHandler<BgpNeighborsByNameAdvertisedRejectedTraits>,
             {{"crf",
               "Show only CRF-rejected routes",
               commandHandler<BgpNeighborsByNameAdvertisedRejectedCrf>,
               argTypeHandler<
                   BgpNeighborsByNameAdvertisedRejectedCrfTraits>}}}}},
          {"received",
           "Routes received from matching peers",
           {{"rejected",
             "Rejected routes grouped by prefix",
             commandHandler<BgpNeighborsByNameReceivedRejected>,
             argTypeHandler<BgpNeighborsByNameReceivedRejectedTraits>,
             {{"crf",
               "Show only CRF-rejected routes",
               commandHandler<BgpNeighborsByNameReceivedRejectedCrf>,
               argTypeHandler<
                   BgpNeighborsByNameReceivedRejectedCrfTraits>}}}}}}},

        {"nexthopinfo",
         "Show nexthop information for a given IP",
         commandHandler<CmdShowBgpNexthopInfo>,
         argTypeHandler<CmdShowBgpNexthopInfoTraits>},

        {"originated-routes",
         "Show BGP originated routes",
         commandHandler<CmdShowBgpOriginatedRoutes>,
         argTypeHandler<CmdShowBgpOriginatedRoutesTraits>},

        {"policy",
         "Show BGP policy by name",
         commandHandler<CmdShowBgpPolicy>,
         argTypeHandler<CmdShowBgpPolicyTraits>},

        {"profiler",
         "Show BGP profiler stats",
         commandHandler<CmdShowBgpProfiler>,
         argTypeHandler<CmdShowBgpProfilerTraits>},

        {"shadowrib",
         "Show BGP shadow RIB",
         commandHandler<CmdShowBgpShadowRib>,
         argTypeHandler<CmdShowBgpShadowRibTraits>},

        {"stats",
         "Show BGP statistics",
         {{"attrs",
           "Show BGP attribute deduplicator statistics",
           commandHandler<CmdShowBgpStatsAttrs>,
           argTypeHandler<CmdShowBgpStatsAttrsTraits>},
          {"entries",
           "Show BGP entry statistics",
           commandHandler<CmdShowBgpStatsEntries>,
           argTypeHandler<CmdShowBgpStatsEntriesTraits>},
          {"policy",
           "Show BGP policy statistics",
           commandHandler<CmdShowBgpStatsPolicy>,
           argTypeHandler<CmdShowBgpStatsPolicyTraits>}}},

        {"stream",
         "Show BGP stream information",
         {{"subscriber",
           "Show BGP stream subscribers",
           commandHandler<CmdShowBgpStreamSubscriber>,
           argTypeHandler<CmdShowBgpStreamSubscriberTraits>,
           {{"post-policy",
             "Show BGP stream subscribers (post-policy)",
             commandHandler<CmdShowBgpStreamSubscriberPostPolicy>,
             argTypeHandler<CmdShowBgpStreamSubscriberPostPolicyTraits>},
            {"pre-policy",
             "Show BGP stream subscribers (pre-policy)",
             commandHandler<CmdShowBgpStreamSubscriberPrePolicy>,
             argTypeHandler<CmdShowBgpStreamSubscriberPrePolicyTraits>}}},
          {"summary",
           "Show BGP stream summary",
           commandHandler<CmdShowBgpStreamSummary>,
           argTypeHandler<CmdShowBgpStreamSummaryTraits>}}},

        {"summary",
         "Show BGP summary",
         commandHandler<CmdShowBgpSummary>,
         argTypeHandler<CmdShowBgpSummaryTraits>,
         localOptionsHandler<CmdShowBgpSummaryTraits>,
         {{"egress",
           "Show BGP summary egress",
           commandHandler<CmdShowBgpSummaryEgress>,
           argTypeHandler<CmdShowBgpSummaryEgressTraits>}}},

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
           argTypeHandler<CmdShowBgpTableSummaryTraits>}}},

        {"update-group",
         "Show update-group details",
         commandHandler<CmdShowBgpUpdateGroup>,
         argTypeHandler<CmdShowBgpUpdateGroupTraits>},

        {"version",
         "Show BGP version",
         commandHandler<CmdShowVersionBgp>,
         argTypeHandler<CmdShowVersionTraits>}}},

      {"show",
       "config",
       "Show config info for various binaries",
       {{"running",
         "Show running config for various binaries",
         {
             {"agent",
              "Show Agent running config",
              commandHandler<CmdShowConfigRunningAgent>,
              argRegistrar<CmdShowConfigDynamicTraits>},
             {"bgp",
              "Show BGP running config",
              commandHandler<CmdShowConfigRunningBgp>,
              argTypeHandler<CmdShowConfigRunningBgpTraits>},
         }},
        {"history",
         "Show history of changes to the current config",
         {{"agent",
           "Show history of changes to the current Agent config",
           commandHandler<CmdShowConfigHistoryAgent>,
           argRegistrar<CmdShowConfigTraits>}}}}},
  };
  return tree;
}

} // namespace facebook::fboss
