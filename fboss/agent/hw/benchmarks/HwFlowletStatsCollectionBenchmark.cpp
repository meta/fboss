/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/benchmarks/HwStatsCollectionBenchmarkHelper.h"
#include "fboss/agent/test/AgentEnsemble.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/ScaleTestUtils.h"
#include "fboss/agent/test/utils/UdfTestUtils.h"
#include "fboss/lib/CommonUtils.h"

#include <folly/Benchmark.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>

namespace facebook::fboss {

// Run against HwStatsCollection; the gap is what the ARS counter reads cost.
// Not an exact mirror: this also stands up the UDF ACL ARS needs, whose
// traffic counter is read every collection.
BENCHMARK(HwFlowletStatsCollectionAtScale) {
  folly::BenchmarkSuspender suspender;

  constexpr int kArsEcmpGroupWidth = 4;
  // Same as the plain stats collection benchmark, for comparability.
  constexpr int numPortsToCollectStats = 48;
  constexpr int numRouteCounters = 255;

  // @lint-ignore CLANGTIDY
  FLAGS_flowletSwitchingEnable = true;
  // @lint-ignore CLANGTIDY
  FLAGS_flowletStatsEnable = true;

  auto portsToUse = [](const AgentEnsemble& ensemble, int maxPorts) {
    auto ports = ensemble.masterLogicalPortIds();
    ports.resize(std::min(static_cast<int>(ports.size()), maxPorts));
    return ports;
  };

  AgentEnsembleSwitchConfigFn initialConfigFn =
      [portsToUse](const AgentEnsemble& ensemble) {
        // Disable stats collection thread, so the benchmark drives collection.
        // @lint-ignore CLANGTIDY
        FLAGS_enable_stats_update_thread = false;
        // @lint-ignore CLANGTIDY
        FLAGS_dlbResourceCheckEnable = false;

        auto portsNew = portsToUse(ensemble, numPortsToCollectStats);

        auto config =
            utility::onePortPerInterfaceConfig(ensemble.getSw(), portsNew);
        config.udfConfig() =
            utility::addUdfAclConfig(utility::kUdfOffsetBthReserved);
        utility::addFlowletConfigs(
            config,
            portsNew,
            ensemble.isSai(),
            cfg::SwitchingMode::PER_PACKET_QUALITY);
        utility::addFlowletAcl(
            config,
            ensemble.isSai(),
            utility::kFlowletAclName,
            utility::kFlowletAclCounterName);
        return config;
      };

  auto ensemble =
      createAgentEnsemble(initialConfigFn, false /*disableLinkStateToggler*/);
  const int iterations = statsCollectionBenchmarkIterations(ensemble.get());

  // Programming the max number of ARS groups possible
  auto asic = checkSameAndGetAsicForTesting(
      ensemble->getSw()->getHwAsicTable()->getL3Asics());
  auto maxArsGroups = asic->getMaxArsGroups();
  CHECK(maxArsGroups.has_value()) << "ASIC reports no ARS group ceiling";
  const int numArsEcmpGroups = maxArsGroups.value();

  std::vector<PortID> ports = portsToUse(*ensemble, numPortsToCollectStats);
  std::vector<PortDescriptor> portDescs;
  portDescs.reserve(ports.size());
  for (const auto& port : ports) {
    portDescs.emplace_back(port);
  }

  utility::EcmpSetupTargetedPorts6 ecmpHelper(
      ensemble->getSw()->getState(),
      ensemble->getSw()->needL2EntryForNeighbor());
  ensemble->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
    return ecmpHelper.resolveNextHops(
        in,
        boost::container::flat_set<PortDescriptor>(
            portDescs.begin(), portDescs.end()));
  });

  auto memberSets = utility::generateEcmpGroupScale(
      portDescs, numArsEcmpGroups, kArsEcmpGroupWidth, kArsEcmpGroupWidth);
  CHECK_EQ(memberSets.size(), static_cast<size_t>(numArsEcmpGroups));
  std::vector<boost::container::flat_set<PortDescriptor>> nhopSets;
  std::vector<RoutePrefixV6> arsPrefixes;
  for (int i = 0; i < numArsEcmpGroups; ++i) {
    nhopSets.emplace_back(memberSets[i].begin(), memberSets[i].end());
    arsPrefixes.emplace_back(
        folly::IPAddressV6(fmt::format("2401::{:x}", i)), 128);
  }
  {
    auto updater = ensemble->getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(&updater, nhopSets, arsPrefixes);
  }

  // As in the plain stats collection benchmark, so the runs stay comparable.
  programStatsCollectionRouteCounters(ensemble.get(), numRouteCounters);

  auto countArsAttachedGroups = [&]() {
    int attached = 0;
    for (const auto& prefix : arsPrefixes) {
      auto mode = ensemble->getFwdSwitchingMode(prefix);
      if (mode == cfg::SwitchingMode::PER_PACKET_QUALITY ||
          mode == cfg::SwitchingMode::FLOWLET_QUALITY) {
        ++attached;
      }
    }
    return attached;
  };

  // Benchmarking the max ARS group scale, so ensure all are programmed.
  // Attachment lands in hardware asynchronously, hence the retries.
  int arsAttachedGroups = 0;
  WITH_RETRIES({
    arsAttachedGroups = countArsAttachedGroups();
    EXPECT_EVENTUALLY_EQ(arsAttachedGroups, numArsEcmpGroups);
  });
  CHECK_EQ(arsAttachedGroups, numArsEcmpGroups);
  XLOG(INFO) << "ARS groups attached " << arsAttachedGroups;

  suspender.dismiss();
  for (auto i = 0; i < iterations; ++i) {
    ensemble->getSw()->updateStats();
  }
  suspender.rehire();
}

} // namespace facebook::fboss
