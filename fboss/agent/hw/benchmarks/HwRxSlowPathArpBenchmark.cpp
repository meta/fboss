/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/IPv4Handler.h"
#include "fboss/agent/IPv6Handler.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/benchmarks/AgentBenchmarks.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"

#include <folly/Benchmark.h>
#include <folly/IPAddress.h>
#include <folly/json/dynamic.h>
#include <folly/json/json.h>

#include <iostream>
#include <thread>

namespace {
const std::string kSrcIp = "8.8.8.7";
const std::string kDstIp = "8.8.8.8";
} // namespace

namespace facebook::fboss {

// How to generate linerate arp request packets for NPU with Vlan Rif
// 1. Put all ports under same vlan
// 2. Install CPU trap acl
// 3. Send arp request packets with Broadcast mac.
// It should be broadcast to all the ports to the
// same vlan and hence amplify the traffic.
// How to generate linerate arp request packets for NPU with Port Rif
// 1. Create one port rif per port
// 2. Install CPU trap acl
// 3. Send arp request packets with Broadcast mac on that port.

BENCHMARK(RxSlowPathArpBenchmark) {
  AgentEnsembleSwitchConfigFn initialConfigFn = [](const AgentEnsemble&
                                                       ensemble) {
    FLAGS_sai_user_defined_trap = true;
    CHECK_GE(
        ensemble.masterLogicalPortIds({cfg::PortType::INTERFACE_PORT}).size(),
        1);
    std::vector<PortID> ports = {ensemble.masterLogicalInterfacePortIds()[0]};

    auto l3Asics = ensemble.getSw()->getHwAsicTable()->getL3Asics();
    auto asic = checkSameAndGetAsicForTesting(l3Asics);

    auto config =
        (asic->getAsicVendor() != HwAsic::AsicVendor::ASIC_VENDOR_CHENAB)
        ? utility::oneL3IntfNPortConfig(
              ensemble.getSw()->getPlatformMapping(),
              asic,
              ensemble.masterLogicalPortIds(),
              ensemble.getSw()->getPlatformSupportsAddRemovePort(),
              asic->desiredLoopbackModes(),
              true, /* interfaceHasSubnet */
              utility::kBaseVlanId, /* baseVlanId */
              true, /* optimizePortProfile */
              true, /* setInterfaceMac */
              ensemble.getSw()->getPlatformType())
        : utility::onePortPerInterfaceConfig(
              ensemble.getSw()->getPlatformMapping(),
              asic,
              ensemble.masterLogicalPortIds(),
              ensemble.getSw()->getPlatformSupportsAddRemovePort(),
              asic->desiredLoopbackModes());

    utility::setupDefaultAclTableGroups(config);
    // We don't want to set queue rate that limits the number of rx pkts
    utility::addCpuQueueConfig(
        config,
        ensemble.getL3Asics(),
        ensemble.isSai(),
        /* setQueueRate */ false);
    utility::setDefaultCpuTrafficPolicyConfig(
        config, ensemble.getL3Asics(), ensemble.isSai());
    config.switchSettings()->l2LearningMode() = cfg::L2LearningMode::HARDWARE;
    return config;
  };

  auto ensemble =
      createAgentEnsemble(initialConfigFn, false /*disableLinkStateToggler*/);

  auto state = ensemble->getSw()->getState();
  auto intf = firstInterfaceWithPortsForTesting(state);
  const auto kSrcMac = intf->getMac();
  auto broadcastMac = folly::MacAddress("FF:FF:FF:FF:FF:FF");
  //  Send packet
  std::optional<PortDescriptor> portDescriptor{};
  std::optional<VlanID> vlanId{};
  if (intf->getType() == cfg::InterfaceType::VLAN) {
    vlanId = intf->getVlanID();
    portDescriptor =
        PortDescriptor(ensemble->masterLogicalInterfacePortIds()[0]);
  } else if (intf->getType() == cfg::InterfaceType::PORT) {
    portDescriptor = PortDescriptor(intf->getPortID());
  }
  auto constexpr kPacketToSend = 10;
  for (int i = 0; i < kPacketToSend; i++) {
    auto txPacket = utility::makeARPTxPacket(
        ensemble->getSw(),
        vlanId,
        kSrcMac,
        broadcastMac,
        folly::IPAddressV4(kSrcIp),
        folly::IPAddressV4(kDstIp),
        ARP_OPER::ARP_OPER_REQUEST);
    ensemble->getSw()->sendPacketAsync(std::move(txPacket), portDescriptor);
  }

  constexpr auto kBurnIntevalInSeconds = 10;
  // Let the packet flood warm up
  std::this_thread::sleep_for(std::chrono::seconds(kBurnIntevalInSeconds));
  std::map<int, CpuPortStats> cpuStatsBefore;
  ensemble->getSw()->getAllCpuPortStats(cpuStatsBefore);
  auto statsBefore = cpuStatsBefore[FLAGS_switch_id_for_testing];
  auto hwAsic = checkSameAndGetAsicForTesting(ensemble->getL3Asics());
  auto [pktsBefore, bytesBefore] = utility::getCpuQueueOutPacketsAndBytes(
      *statsBefore.portStats_(), utility::getCoppHighPriQueueId(hwAsic));
  auto timeBefore = std::chrono::steady_clock::now();
  CHECK_NE(pktsBefore, 0);
  std::this_thread::sleep_for(std::chrono::seconds(kBurnIntevalInSeconds));
  std::map<int, CpuPortStats> cpuStatsAfter;
  ensemble->getSw()->getAllCpuPortStats(cpuStatsAfter);
  auto statsAfter = cpuStatsAfter[FLAGS_switch_id_for_testing];
  auto [pktsAfter, bytesAfter] = utility::getCpuQueueOutPacketsAndBytes(
      *statsAfter.portStats_(), utility::getCoppHighPriQueueId(hwAsic));
  auto timeAfter = std::chrono::steady_clock::now();
  std::chrono::duration<double, std::milli> durationMillseconds =
      timeAfter - timeBefore;
  uint32_t pps = (static_cast<double>(pktsAfter - pktsBefore) /
                  durationMillseconds.count()) *
      1000;
  uint32_t bytesPerSec = (static_cast<double>(bytesAfter - bytesBefore) /
                          durationMillseconds.count()) *
      1000;

  if (FLAGS_json) {
    folly::dynamic cpuRxRateJson = folly::dynamic::object;
    cpuRxRateJson["cpu_rx_pps"] = pps;
    cpuRxRateJson["cpu_rx_bytes_per_sec"] = bytesPerSec;
    std::cout << toPrettyJson(cpuRxRateJson) << std::endl;
  } else {
    XLOG(DBG2) << " Pkts before: " << pktsBefore << " Pkts after: " << pktsAfter
               << " interval ms: " << durationMillseconds.count()
               << " pps: " << pps << " bytes per sec: " << bytesPerSec;
  }
}
} // namespace facebook::fboss
