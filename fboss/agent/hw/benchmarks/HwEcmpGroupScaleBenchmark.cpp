/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/hw/test/HwTestEcmpUtils.h"
#include "fboss/agent/test/AgentEnsemble.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"
#include "fboss/agent/test/utils/ScaleTestUtils.h"

#include <folly/Benchmark.h>
#include <folly/IPAddress.h>

#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"

namespace facebook::fboss {

void unprogramRoutes(
    RouteUpdateWrapper* updater,
    const std::vector<RoutePrefixV6>& prefixes) {
  for (const auto& prefix : prefixes) {
    updater->delRoute(
        RouterID(0),
        folly::IPAddress(prefix.network()),
        prefix.mask(),
        ClientID::BGPD);
  }
}

void programRoutes(
    RouteUpdateWrapper* updater,
    const std::vector<std::vector<PortDescriptor>>& portDescs,
    const std::vector<RoutePrefixV6>& prefixes,
    const utility::EcmpSetupTargetedPorts<folly::IPAddressV6>* ecmpHelper) {
  auto constexpr kClientID(ClientID::BGPD);
  const AdminDistance kDefaultAdminDistance = AdminDistance::EBGP;
  for (auto i = 0; i < prefixes.size(); ++i) {
    RouteNextHopSet nhopSet;
    for (const auto& portDesc : portDescs[i]) {
      auto nhop = ecmpHelper->nhop(portDesc);
      nhopSet.emplace(ResolvedNextHop(nhop.ip, nhop.intf, UCMP_DEFAULT_WEIGHT));
    }
    RouteNextHopEntry nhopEntry(nhopSet, kDefaultAdminDistance);
    updater->addRoute(
        RouterID(0),
        prefixes[i].network(),
        prefixes[i].mask(),
        kClientID,
        nhopEntry);
  }
}

void ecmpGroupScaleBenchmark(
    bool add,
    uint32_t ecmpGroups,
    uint32_t ecmpGroupWidth) {
  folly::BenchmarkSuspender suspender;
  AgentEnsembleSwitchConfigFn initialConfigFn =
      [&](const AgentEnsemble& ensemble) {
        FLAGS_enable_ecmp_resource_manager = true;
        FLAGS_ecmp_resource_percentage = 100;
        FLAGS_flowletSwitchingEnable = true;
        FLAGS_dlbResourceCheckEnable = false;
        auto cfg = utility::onePortPerInterfaceConfig(
            ensemble.getSw(), ensemble.masterLogicalPortIds());

        XLOG(DBG2) << "EcmpGroupWidth: " << ecmpGroupWidth
                   << ", EcmpGroups: " << ecmpGroups;

        utility::addFlowletConfigs(
            cfg,
            ensemble.masterLogicalPortIds(),
            ensemble.isSai(),
            cfg::SwitchingMode::PER_PACKET_QUALITY);
        return cfg;
      };
  auto ensemble =
      createAgentEnsemble(initialConfigFn, false /*disableLinkStateToggler*/);
  auto ecmpHelper = utility::EcmpSetupTargetedPorts6(
      ensemble->getSw()->getState(),
      ensemble->getSw()->needL2EntryForNeighbor());
  boost::container::flat_set<PortDescriptor> portDescs;

  for (const auto& portId : ensemble->masterLogicalInterfacePortIds()) {
    portDescs.insert(PortDescriptor(portId));
  }
  ensemble->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
    return ecmpHelper.resolveNextHops(in, portDescs);
  });

  auto nhopSets = utility::generateEcmpGroupScale(
      std::vector<PortDescriptor>(portDescs.begin(), portDescs.end()),
      ecmpGroups,
      ecmpGroupWidth /*max ecmp width*/,
      ecmpGroupWidth /*min ecmp width*/);
  std::vector<RoutePrefixV6> prefixes;
  prefixes.reserve(ecmpGroups);
  for (auto i = 0; i < ecmpGroups; ++i) {
    prefixes.emplace_back(
        folly::IPAddressV6(folly::to<std::string>(2401, ":", i + 1, "::")), 64);
  }

  auto updater = ensemble->getSw()->getRouteUpdater();
  XLOG(DBG2) << "Operation: " << add << ", EcmpGroupWidth: " << ecmpGroupWidth
             << ", EcmpGroups: " << ecmpGroups;

  programRoutes(&updater, nhopSets, prefixes, &ecmpHelper);
  if (add) {
    suspender.dismiss();
    updater.program();
    suspender.rehire();
    unprogramRoutes(&updater, prefixes);
    updater.program();
  } else {
    updater.program();
    suspender.dismiss();
    unprogramRoutes(&updater, prefixes);
    updater.program();
    suspender.rehire();
  }
}

BENCHMARK(RouteAddHwEcmpGroupScale128x64Benchmark) {
  // Measure 128x64 ECMP route add
  ecmpGroupScaleBenchmark(
      true /* add */, 128 /* ecmpGroup */, 64 /* ecmpWidth */);
}
BENCHMARK(RouteAddHwEcmpGroupScale256x64Benchmark) {
  // Measure 256x64 ECMP route add
  ecmpGroupScaleBenchmark(
      true /* add */, 256 /* ecmpGroup */, 64 /* ecmpWidth */);
}

BENCHMARK(RouteDelHwEcmpGroupScale128x64Benchmark) {
  // Measure 128x64 ECMP route del
  ecmpGroupScaleBenchmark(
      false /* delete */, 128 /* ecmpGroup */, 64 /* ecmpWidth */);
}
BENCHMARK(RouteDelHwEcmpGroupScale256x64Benchmark) {
  // Measure 256x64 ECMP route del
  ecmpGroupScaleBenchmark(
      false /* delete */, 256 /* ecmpGroup */, 64 /* ecmpWidth */);
}

} // namespace facebook::fboss
