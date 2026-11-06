/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/CmdHandler.cpp"

// NOLINTBEGIN(misc-include-cleaner)
// IWYU pragma: begin_keep
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowBgpOriginatedRoutes.h"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowVersionBgp.h"
#include "fboss/cli/fboss2/commands/show/bgp/changelist/CmdShowBgpChangelist.h"
#include "fboss/cli/fboss2/commands/show/bgp/config/CmdShowConfigRunningBgp.h"
#include "fboss/cli/fboss2/commands/show/bgp/fibnexthops/CmdShowBgpFibNexthops.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/CmdShowBgpNeighbors.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/advertised/BgpNeighborsAdvertisedPostPolicy.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/advertised/BgpNeighborsAdvertisedPrePolicy.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/advertised/BgpNeighborsAdvertisedRejected.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/received/BgpNeighborsReceivedPostPolicy.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/received/BgpNeighborsReceivedPrePolicy.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/received/BgpNeighborsReceivedRejected.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/session_id/CmdBgpNeighborsSessionId.h"
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
// IWYU pragma: end_keep
// NOLINTEND(misc-include-cleaner)

namespace facebook::fboss {

// Explicit template instantiations for BGP commands
template void
CmdHandler<CmdShowBgpChangelist, CmdShowBgpChangelistTraits>::run();
template void
CmdHandler<CmdShowConfigRunningBgp, CmdShowConfigRunningBgpTraits>::run();
template void CmdHandler<CmdShowBgpNeighbors, CmdShowBgpNeighborsTraits>::run();
template void CmdHandler<CmdShowVersionBgp, CmdShowVersionTraits>::run();
template void CmdHandler<
    BgpNeighborsAdvertisedPostPolicy,
    BgpNeighborsAdvertisedPostPolicyTraits>::run();
template void CmdHandler<
    BgpNeighborsAdvertisedPrePolicy,
    BgpNeighborsAdvertisedPrePolicyTraits>::run();
template void CmdHandler<
    BgpNeighborsAdvertisedRejected,
    BgpNeighborsAdvertisedRejectedTraits>::run();
template void CmdHandler<
    BgpNeighborsReceivedPostPolicy,
    BgpNeighborsReceivedPostPolicyTraits>::run();
template void CmdHandler<
    BgpNeighborsReceivedPrePolicy,
    BgpNeighborsReceivedPrePolicyTraits>::run();
template void CmdHandler<
    BgpNeighborsReceivedRejected,
    BgpNeighborsReceivedRejectedTraits>::run();
template void
CmdHandler<CmdBgpNeighborsSessionId, CmdBgpNeighborsSessionIdTraits>::run();
template void
CmdHandler<CmdShowBgpOriginatedRoutes, CmdShowBgpOriginatedRoutesTraits>::run();
template void CmdHandler<CmdShowBgpShadowRib, CmdShowBgpShadowRibTraits>::run();
template void
CmdHandler<CmdShowBgpStatsAttrs, CmdShowBgpStatsAttrsTraits>::run();
template void
CmdHandler<CmdShowBgpStatsEntries, CmdShowBgpStatsEntriesTraits>::run();
template void
CmdHandler<CmdShowBgpStatsPolicy, CmdShowBgpStatsPolicyTraits>::run();
template void
CmdHandler<CmdShowBgpStreamSubscriber, CmdShowBgpStreamSubscriberTraits>::run();
template void CmdHandler<
    CmdShowBgpStreamSubscriberPostPolicy,
    CmdShowBgpStreamSubscriberPostPolicyTraits>::run();
template void CmdHandler<
    CmdShowBgpStreamSubscriberPrePolicy,
    CmdShowBgpStreamSubscriberPrePolicyTraits>::run();
template void
CmdHandler<CmdShowBgpStreamSummary, CmdShowBgpStreamSummaryTraits>::run();
template void CmdHandler<CmdShowBgpSummary, CmdShowBgpSummaryTraits>::run();
template void
CmdHandler<CmdShowBgpSummaryEgress, CmdShowBgpSummaryEgressTraits>::run();
template void CmdHandler<CmdShowBgpTable, CmdShowBgpTableTraits>::run();
template void
CmdHandler<CmdShowBgpTableCommunity, CmdShowBgpTableCommunityTraits>::run();
template void
CmdHandler<CmdShowBgpTableDetail, CmdShowBgpTableDetailTraits>::run();
template void
CmdHandler<CmdShowBgpFibNexthops, CmdShowBgpFibNexthopsTraits>::run();
template void
CmdHandler<CmdShowBgpTableSummary, CmdShowBgpTableSummaryTraits>::run();
template void CmdHandler<
    CmdShowBgpTableMoreSpecifics,
    CmdShowBgpTableMoreSpecificsTraits>::run();
template void
CmdHandler<CmdShowBgpTablePrefix, CmdShowBgpTablePrefixTraits>::run();

} // namespace facebook::fboss
