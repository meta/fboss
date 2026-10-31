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

#include <gmock/gmock.h>
#include <map>
#include <string>

#include "configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/bgp_policy_types.h" // NOLINT(misc-include-cleaner)
#include "configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h"
#include "configerator/structs/neteng/fboss/bgp/if/gen-cpp2/bgp_attr_types.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_route_types_types.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"
#include "neteng/fboss/bgp/if/gen-cpp2/policy_thrift_types.h"

namespace facebook::fboss {
using namespace facebook::neteng::fboss::bgp::thrift;
using namespace facebook::neteng::fboss::bgp_attr;
using namespace facebook::bgp::bgp_policy;
using facebook::neteng::routing::policy::thrift::TPolicyStats;

class MockBgpClient : public apache::thrift::ServiceHandler<TBgpService> {
 public:
  MOCK_METHOD(void, getBuildInfo, ((std::map<std::string, std::string>&)));
  MOCK_METHOD(void, getBgpSessions, (std::vector<TBgpSession>&));
  MOCK_METHOD(void, getBgpLocalConfig, (TBgpLocalConfig&));
  MOCK_METHOD(void, getDrainState, (TBgpDrainState&));
  MOCK_METHOD(void, getAttributeStats, (TAttributeStats&));
  MOCK_METHOD(
      void,
      getDeduplicatorStats,
      (TGetDeduplicatorStatsResponse&,
       std::unique_ptr<TGetDeduplicatorStatsRequest>));
  MOCK_METHOD(void, getEntryStats, (TEntryStats&));
  MOCK_METHOD(void, getPolicyStats, (TPolicyStats&));
  MOCK_METHOD(void, getOriginatedRoutes, (std::vector<TOriginatedRoute>&));
  MOCK_METHOD(
      void,
      getBgpNeighbors,
      (std::vector<TBgpSession>&, std::unique_ptr<std::vector<std::string>>));
  MOCK_METHOD(void, getPeerEgressStats, (std::vector<TPeerEgressStats>&));

  using networks = std::map<TIpPrefix, std::vector<TBgpPath>>&;
  MOCK_METHOD(
      void,
      getPrefilterReceivedNetworks2,
      (networks, std::unique_ptr<std::string>));
  MOCK_METHOD(
      void,
      getPostfilterReceivedNetworks2,
      (networks, std::unique_ptr<std::string>));
  MOCK_METHOD(
      void,
      getPrefilterAdvertisedNetworks2,
      (networks, std::unique_ptr<std::string>));
  MOCK_METHOD(
      void,
      getPostfilterAdvertisedNetworks2,
      (networks, std::unique_ptr<std::string>));
  MOCK_METHOD(void, getRibEntries, (std::vector<TRibEntry>&, TBgpAfi));
  /** Mock the compact exact-prefix FIB-out RPC. */
  MOCK_METHOD(
      void,
      getFibOutPrefix,
      (TFibOutTable&, std::unique_ptr<TFibOutPrefixRequest>));

  using apache::thrift::ServiceHandler<TBgpService>::sync_getFibOutPrefix;

  /** Adapt the generated client signature to this handler-based mock. */
  void sync_getFibOutPrefix(
      TFibOutTable& table,
      const TFibOutPrefixRequest& request) {
    getFibOutPrefix(table, std::make_unique<TFibOutPrefixRequest>(request));
  }
  MOCK_METHOD(void, getRibSummary, (TRibSummary&, TBgpAfi));
  MOCK_METHOD(void, getChangeListEntries, (std::vector<TRibEntry>&, TBgpAfi));
  MOCK_METHOD(void, getShadowRibEntries, (std::vector<TRibEntry>&, TBgpAfi));
  MOCK_METHOD(void, getRunningConfig, (std::string&));
  MOCK_METHOD(
      void,
      getRunningConfigStruct,
      (facebook::bgp::thrift::BgpConfig&));
  MOCK_METHOD(void, getPolicyConfig, (std::string&));
  MOCK_METHOD(bool, hasPolicySymlink, ());
  MOCK_METHOD(
      void,
      getRibPrefix,
      (std::vector<TRibEntry>&, std::unique_ptr<std::string>));
  MOCK_METHOD(
      void,
      getRibSubprefixes,
      (std::vector<TRibEntry>&, std::unique_ptr<std::string>));
  MOCK_METHOD(
      void,
      getRibEntriesForCommunity,
      (std::vector<TRibEntry>&, TBgpAfi, std::unique_ptr<std::string>));
  // Canonical (deduplicated) RIB getters -- mirror the legacy getters above but
  // return a single TCanonicalRibState.
  MOCK_METHOD(void, getRibEntriesCanonical, (TCanonicalRibState&, TBgpAfi));
  MOCK_METHOD(
      void,
      getRibPrefixCanonical,
      (TCanonicalRibState&, std::unique_ptr<std::string>));
  MOCK_METHOD(
      void,
      getRibSubprefixesCanonical,
      (TCanonicalRibState&, std::unique_ptr<std::string>));
  MOCK_METHOD(
      void,
      getRibEntriesForCommunityCanonical,
      (TCanonicalRibState&, TBgpAfi, std::unique_ptr<std::string>));
  MOCK_METHOD(
      void,
      getShadowRibEntriesCanonical,
      (TCanonicalRibState&, TBgpAfi));
  MOCK_METHOD(
      void,
      getChangeListEntriesCanonical,
      (TCanonicalRibState&, TBgpAfi));
  MOCK_METHOD(void, getBgpStreamSessions, (std::vector<TBgpStreamSession>&));
  MOCK_METHOD(int64_t, getRibVersion, ());
  MOCK_METHOD(int64_t, getNumPrefixes, ());
  MOCK_METHOD(int64_t, getProcessUptimeSeconds, ());
  MOCK_METHOD(
      void,
      getSubscriberNetworkInfo,
      (networks, int32_t, std::unique_ptr<std::string>));
  MOCK_METHOD(void, setDebugLevel, (TBgpDebugLevel));
  MOCK_METHOD(void, getProfilerStats, (std::vector<TBgpProfilerStat>&));
};
} // namespace facebook::fboss
