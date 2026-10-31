/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/hw/test/HwSwitchEnsemble.h"
#include "fboss/agent/hw/test/HwTestPacketUtils.h"
#include "fboss/agent/test/EcmpSetupHelper.h"

#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/benchmarks/AgentBenchmarks.h"

#include <folly/IPAddressV6.h>
#include <folly/init/Init.h>
#include <folly/json/dynamic.h>
#include <folly/json/json.h>

#include <folly/Benchmark.h>
#include <folly/logging/xlog.h>
#include <chrono>
#include <iostream>
#include <thread>

namespace facebook::fboss {

std::pair<uint64_t, uint64_t> getOutPktsAndBytes(
    AgentEnsemble* ensemble,
    const PortID& port) {
  auto stats = ensemble->getLatestPortStats(port);
  return {*stats.outUnicastPkts_(), *stats.outBytes_()};
}

BENCHMARK(runTxSlowPathBenchmark) {
  constexpr int kEcmpWidth = 1;

  std::unique_ptr<AgentEnsemble> ensemble{};

  AgentEnsembleSwitchConfigFn initialConfigFn =
      [](const AgentEnsemble& ensemble) {
        auto ports = ensemble.masterLogicalPortIds();
        CHECK_GT(ports.size(), 0);
        return utility::onePortPerInterfaceConfig(ensemble.getSw(), ports);
      };
  ensemble =
      createAgentEnsemble(initialConfigFn, false /*disableLinkStateToggler*/);

  auto swSwitch = ensemble->getSw();
  auto ecmpHelper = utility::EcmpSetupAnyNPorts6(
      ensemble->getSw()->getState(),
      ensemble->getSw()->needL2EntryForNeighbor());
  auto portUsed = ecmpHelper.ecmpPortDescriptorAt(0).phyPortID();

  ensemble->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
    return ecmpHelper.resolveNextHops(in, kEcmpWidth);
  });
  ecmpHelper.programRoutes(
      std::make_unique<SwSwitchRouteUpdateWrapper>(
          ensemble->getSw(), ensemble->getSw()->getRib()),
      kEcmpWidth);
  auto cpuMac = ensemble->getSw()->getLocalMac(SwitchID(0));
  auto vlanId = ensemble->getVlanIDForTx();
  std::atomic<bool> packetTxDone{false};
  std::thread t([cpuMac, vlanId, swSwitch, &packetTxDone]() {
    const auto kSrcIp = folly::IPAddressV6("2620:0:1cfe:face:b00c::3");
    const auto kDstIp = folly::IPAddressV6("2620:0:1cfe:face:b00c::4");
    const auto kSrcMac = folly::MacAddress{"fa:ce:b0:00:00:0c"};
    while (!packetTxDone) {
      for (auto i = 0; i < 1'000; ++i) {
        // Send packet
        auto txPacket = utility::makeIpTxPacket(
            [swSwitch](uint32_t size) {
              return swSwitch->allocatePacket(size);
            },
            vlanId,
            kSrcMac,
            cpuMac,
            kSrcIp,
            kDstIp);
        swSwitch->sendPacketSwitchedAsync(std::move(txPacket));
      }
    }
  });

  auto [pktsBefore, bytesBefore] =
      getOutPktsAndBytes(ensemble.get(), PortID(portUsed));
  auto timeBefore = std::chrono::steady_clock::now();
  // Let the packet flood warm up
  std::this_thread::sleep_for(std::chrono::seconds(30));
  packetTxDone = true;
  t.join();
  auto timeAfter = std::chrono::steady_clock::now();
  std::chrono::duration<double, std::milli> durationMillseconds =
      timeAfter - timeBefore;
  auto pktsAfter = pktsBefore;
  auto bytesAfter = bytesBefore;
  constexpr auto kMaxIterations = 30;
  // Number of back to back unchanged readings that count as "drained".
  // Port stats are served from a counter cache refreshed on the same ~1s
  // cadence as this poll loop, so a single repeated reading only means we hit
  // the cache, not that the drain finished. Breaking on it truncates the
  // packet count while the denominator stays pinned to the TX window, which
  // reports a fraction of the real pps.
  constexpr auto kRequiredStableReads = 3;
  auto stableReads = 0;
  bool drained = false;
  // Wait for stats increment to stop. On some platforms it
  // takes longer for port stats to reflect the sent bytes.
  for (auto i = 0; i < kMaxIterations; ++i) {
    auto pktsPrior = pktsAfter;
    auto bytesPrior = bytesAfter;
    std::tie(pktsAfter, bytesAfter) =
        getOutPktsAndBytes(ensemble.get(), PortID(portUsed));
    if (pktsPrior == pktsAfter && bytesPrior == bytesAfter) {
      if (++stableReads >= kRequiredStableReads) {
        drained = true;
        break;
      }
    } else {
      stableReads = 0;
      XLOG(INFO) << " Stats still incrementing after iteration: " << i + 1;
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  if (!drained) {
    XLOG(WARN) << " Stats still incrementing after " << kMaxIterations
               << " iterations. Reported TX pps maybe lower than actual pps";
  }
  uint32_t pps = (static_cast<double>(pktsAfter - pktsBefore) /
                  durationMillseconds.count()) *
      1000;
  uint32_t bytesPerSec = (static_cast<double>(bytesAfter - bytesBefore) /
                          durationMillseconds.count()) *
      1000;

  if (FLAGS_json) {
    folly::dynamic cpuTxRateJson = folly::dynamic::object;
    cpuTxRateJson["cpu_tx_pps"] = pps;
    cpuTxRateJson["cpu_tx_bytes_per_sec"] = bytesPerSec;
    std::cout << toPrettyJson(cpuTxRateJson) << std::endl;
  } else {
    XLOG(DBG2) << " Pkts before: " << pktsBefore << " Pkts after: " << pktsAfter
               << " interval ms: " << durationMillseconds.count()
               << " pps: " << pps << " bytes per sec: " << bytesPerSec;
  }
}
} // namespace facebook::fboss
