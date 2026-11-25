/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <folly/Benchmark.h>
#include <folly/IPAddress.h>
#include <folly/logging/xlog.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <map>
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/test/AgentEnsemble.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/ScaleTestUtils.h"

namespace facebook::fboss {

// These weights are chosen so we can test FBOSS's weight normalization as well.
//  The aggregate weight sum this way will exceed the ecmpWidth leading to FBOSS
//  normalizing the weights to fit the ecmpWidth.
const auto oddWeight = 203;
const auto evenWeight = 312;

inline void ucmpScaleBenchmark(int ecmpWidth) {
  folly::BenchmarkSuspender suspender;

  AgentEnsembleSwitchConfigFn initialConfigFn =
      [ecmpWidth](const AgentEnsemble& ensemble) {
        FLAGS_ecmp_resource_percentage = 100;
        FLAGS_ecmp_width = ecmpWidth;
        FLAGS_wide_ecmp = false;
        // Get ASIC type from the ensemble configuration
        auto l3Asics = ensemble.getL3Asics();
        if (!l3Asics.empty()) {
          auto detectedAsicType = l3Asics[0]->getAsicType();
          if (detectedAsicType == cfg::AsicType::ASIC_TYPE_TOMAHAWK3 &&
              ecmpWidth == 512) {
            FLAGS_wide_ecmp = true;
          }
        }
        auto cfg = utility::onePortPerInterfaceConfig(
            ensemble.getSw(), ensemble.masterLogicalPortIds());
        return cfg;
      };

  auto ensemble =
      createAgentEnsemble(initialConfigFn, false /*disableLinkStateToggler*/);

  auto ecmpHelper = utility::EcmpSetupTargetedPorts6(
      ensemble->getSw()->getState(),
      ensemble->getSw()->needL2EntryForNeighbor());

  const auto kMaxEcmpMembers =
      utility::getMaxUcmpMembers(ensemble->getL3Asics());
  // Each route has unique next hop groups.
  // EBRO has no native UCMP: SAI adds each next hop weight-many times, so a
  // group costs its normalized weight sum (up to ecmpWidth) member entries
  // instead of its next hop count. Cap the routes so all groups fit.
  constexpr int kDefaultMaxRoutes = 120;
  const bool ucmpReplicatesWeights =
      ensemble->getL3Asics().front()->getAsicType() ==
      cfg::AsicType::ASIC_TYPE_EBRO;
  const int kMaxRoutes = ucmpReplicatesWeights
      ? std::min(
            kDefaultMaxRoutes, static_cast<int>(kMaxEcmpMembers) / ecmpWidth)
      : kDefaultMaxRoutes;

  boost::container::flat_set<PortDescriptor> portDescs;
  std::vector<std::vector<NextHopWeight>> swWeights;

  for (const auto& portId : ensemble->masterLogicalInterfacePortIds()) {
    portDescs.insert(PortDescriptor(portId));
  }

  ensemble->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
    return ecmpHelper.resolveNextHops(in, portDescs);
  });

  std::vector<RoutePrefixV6> prefixes;
  prefixes.reserve(kMaxRoutes);
  for (auto i = 0; i < kMaxRoutes; ++i) {
    auto prefix =
        folly::IPAddressV6(folly::to<std::string>(2401, ":", i + 1, "::"));
    prefixes.emplace_back(prefix, 64);
  }

  // (total ucmp groups * number of ports per group) should not exceed
  // maxEcmpMember count. Therefore we calculate the maxNextHops per ECMP group
  // that we can accommodate for kMaxRoutes.
  auto maxNextHops = ((kMaxEcmpMembers / kMaxRoutes) - 1);

  // Assuming maximum ports that we can get from test config is N. We N - 4 as
  // the nexthop count for each group.  This gives us enough combinations
  // for distinct groups.

  auto numPortsToUse = portDescs.size() - 4;

  // We take the minimum of maxNextHops and numPortsToUse to use for the test so
  // that we dont exceed the maxEcmpMember count.

  auto nhopSets = utility::generateEcmpGroupScale(
      std::vector<PortDescriptor>(portDescs.begin(), portDescs.end()),
      kMaxRoutes,
      portDescs.size(),
      std::min(static_cast<size_t>(maxNextHops), numPortsToUse));

  CHECK_EQ(nhopSets.size(), kMaxRoutes);
  utility::assignUcmpWeights(nhopSets, swWeights, oddWeight, evenWeight);

  auto updater = ensemble->getSw()->getRouteUpdater();
  auto constexpr kClientID(ClientID::BGPD);
  const AdminDistance kDefaultAdminDistance = AdminDistance::EBGP;

  for (auto i = 0; i < kMaxRoutes; ++i) {
    RouteNextHopSet nhopSet;
    for (auto j = 0; j < nhopSets[i].size(); ++j) {
      auto nhop = ecmpHelper.nhop(nhopSets[i][j]);
      nhopSet.emplace(ResolvedNextHop(nhop.ip, nhop.intf, swWeights[i][j]));
    }
    RouteNextHopEntry nhopEntry(nhopSet, kDefaultAdminDistance);
    updater.addRoute(
        RouterID(0),
        prefixes[i].network(),
        prefixes[i].mask(),
        kClientID,
        nhopEntry);
  }

  suspender.dismiss();
  updater.program();
  suspender.rehire();
}

#define UCMP_SCALE_BENCHMARK(name, ecmpWidth) \
  BENCHMARK(name) {                           \
    ucmpScaleBenchmark(ecmpWidth);            \
  }

} // namespace facebook::fboss
