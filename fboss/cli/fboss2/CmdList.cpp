/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/CmdList.h"

#include <fboss/cli/fboss2/utils/CmdUtils.h> // NOLINT(misc-include-cleaner)
#include "fboss/cli/fboss2/CmdHandler.h" // NOLINT(misc-include-cleaner)
#include "fboss/cli/fboss2/commands/bounce/interface/CmdBounceInterface.h"
#include "fboss/cli/fboss2/commands/clear/CmdClearArp.h"
#include "fboss/cli/fboss2/commands/clear/CmdClearInterfaceCounters.h"
#include "fboss/cli/fboss2/commands/clear/CmdClearNdp.h"
#include "fboss/cli/fboss2/commands/clear/interface/CmdClearInterface.h"
#include "fboss/cli/fboss2/commands/clear/interface/counters/phy/CmdClearInterfaceCountersPhy.h"
#include "fboss/cli/fboss2/commands/clear/interface/prbs/CmdClearInterfacePrbs.h"
#include "fboss/cli/fboss2/commands/clear/interface/prbs/stats/CmdClearInterfacePrbsStats.h"
#include "fboss/cli/fboss2/commands/config/gen/agent/CmdConfigGenAgent.h"
#include "fboss/cli/fboss2/commands/get/pcap/CmdGetPcap.h"
#include "fboss/cli/fboss2/commands/reload/CmdReloadConfigAgent.h"
#include "fboss/cli/fboss2/commands/set/interface/CmdSetInterface.h"
#include "fboss/cli/fboss2/commands/set/interface/admin_state/CmdSetInterfaceAdminState.h"
#include "fboss/cli/fboss2/commands/set/interface/loopback/CmdSetInterfaceLoopback.h"
#include "fboss/cli/fboss2/commands/set/interface/prbs/CmdSetInterfacePrbs.h"
#include "fboss/cli/fboss2/commands/set/interface/prbs/state/CmdSetInterfacePrbsState.h"
#include "fboss/cli/fboss2/commands/set/port/CmdSetPort.h"
#include "fboss/cli/fboss2/commands/set/port/state/CmdSetPortState.h"
#include "fboss/cli/fboss2/commands/set/sdk/CmdSetSdk.h"
#include "fboss/cli/fboss2/commands/set/sdk/reg_dump/CmdSetSdkRegDump.h"
#include "fboss/cli/fboss2/commands/set/transceiver/CmdSetTransceiver.h"
#include "fboss/cli/fboss2/commands/set/transceiver/loopback/CmdSetTransceiverLoopback.h"
#include "fboss/cli/fboss2/commands/show/acl/CmdShowAcl.h"
#include "fboss/cli/fboss2/commands/show/agent/CmdShowAgentBootType.h"
#include "fboss/cli/fboss2/commands/show/agent/CmdShowAgentFirmware.h"
#include "fboss/cli/fboss2/commands/show/agent/CmdShowAgentSsl.h"
#include "fboss/cli/fboss2/commands/show/aggregateport/CmdShowAggregatePort.h"
#include "fboss/cli/fboss2/commands/show/arp/CmdShowArp.h"
#include "fboss/cli/fboss2/commands/show/cpuport/CmdShowCpuPort.h"
#include "fboss/cli/fboss2/commands/show/dsf/CmdShowDsf.h"
#include "fboss/cli/fboss2/commands/show/dsf/subscription/CmdShowDsfSubscription.h"
#include "fboss/cli/fboss2/commands/show/dsfnodes/CmdShowDsfNodes.h"
#include "fboss/cli/fboss2/commands/show/environment/fan/CmdShowEnvironmentFan.h"
#include "fboss/cli/fboss2/commands/show/example/CmdShowExample.h"
#include "fboss/cli/fboss2/commands/show/fabric/CmdShowFabric.h"
#include "fboss/cli/fboss2/commands/show/fabric/inputbalance/CmdShowFabricInputBalance.h"
#include "fboss/cli/fboss2/commands/show/fabric/monitoring/CmdShowFabricMonitoringCounters.h"
#include "fboss/cli/fboss2/commands/show/fabric/monitoring/CmdShowFabricMonitoringDetails.h"
#include "fboss/cli/fboss2/commands/show/fabric/reachability/CmdShowFabricReachability.h"
#include "fboss/cli/fboss2/commands/show/fabric/reachability/uncached/CmdShowFabricReachabilityUncached.h"
#include "fboss/cli/fboss2/commands/show/fabric/topology/CmdShowFabricTopology.h"
#include "fboss/cli/fboss2/commands/show/fb303counters/CmdShowFb303Counters.h"
#include "fboss/cli/fboss2/commands/show/flowlet/CmdShowFlowlet.h"
#include "fboss/cli/fboss2/commands/show/fsdb/CmdShowFsdbOperState.h"
#include "fboss/cli/fboss2/commands/show/fsdb/CmdShowFsdbOperStats.h"
#include "fboss/cli/fboss2/commands/show/fsdb/CmdShowFsdbPublishers.h"
#include "fboss/cli/fboss2/commands/show/fsdb/CmdShowFsdbSubscribers.h"
#include "fboss/cli/fboss2/commands/show/host/CmdShowHost.h"
#include "fboss/cli/fboss2/commands/show/hwagent/CmdShowHwAgentStatus.h"
#include "fboss/cli/fboss2/commands/show/hwobject/CmdShowHwObject.h"
#include "fboss/cli/fboss2/commands/show/hwobject/uncached/CmdShowHwObjectUncached.h"
#include "fboss/cli/fboss2/commands/show/interface/CmdShowInterface.h"
#include "fboss/cli/fboss2/commands/show/interface/capabilities/CmdShowInterfaceCapabilities.h"
#include "fboss/cli/fboss2/commands/show/interface/counters/CmdShowInterfaceCounters.h"
#include "fboss/cli/fboss2/commands/show/interface/counters/fec/CmdShowInterfaceCountersFec.h"
#include "fboss/cli/fboss2/commands/show/interface/counters/fec/ber/CmdShowInterfaceCountersFecBer.h"
#include "fboss/cli/fboss2/commands/show/interface/counters/fec/histogram/CmdShowInterfaceCountersFecHistogram.h"
#include "fboss/cli/fboss2/commands/show/interface/counters/fec/tail/CmdShowInterfaceCountersFecTail.h"
#include "fboss/cli/fboss2/commands/show/interface/counters/fec/uncorrectable/CmdShowInterfaceCountersFecUncorrectable.h"
#include "fboss/cli/fboss2/commands/show/interface/counters/mka/CmdShowInterfaceCountersMKA.h"
#include "fboss/cli/fboss2/commands/show/interface/errors/CmdShowInterfaceErrors.h"
#include "fboss/cli/fboss2/commands/show/interface/flaps/CmdShowInterfaceFlaps.h"
#include "fboss/cli/fboss2/commands/show/interface/phy/CmdShowInterfacePhy.h"
#include "fboss/cli/fboss2/commands/show/interface/phymap/CmdShowInterfacePhymap.h"
#include "fboss/cli/fboss2/commands/show/interface/prbs/CmdShowInterfacePrbs.h"
#include "fboss/cli/fboss2/commands/show/interface/prbs/capabilities/CmdShowInterfacePrbsCapabilities.h"
#include "fboss/cli/fboss2/commands/show/interface/prbs/state/CmdShowInterfacePrbsState.h"
#include "fboss/cli/fboss2/commands/show/interface/prbs/stats/CmdShowInterfacePrbsStats.h"
#include "fboss/cli/fboss2/commands/show/interface/status/CmdShowInterfaceStatus.h"
#include "fboss/cli/fboss2/commands/show/interface/traffic/CmdShowInterfaceTraffic.h"
#include "fboss/cli/fboss2/commands/show/interface/transceiver/CmdShowInterfaceTransceiver.h"
#include "fboss/cli/fboss2/commands/show/interface/transceiver/eeprom/CmdShowInterfaceTransceiverEeprom.h"
#include "fboss/cli/fboss2/commands/show/interface/transceiver/eeprom/CmdShowInterfaceTransceiverEepromDump.h"
#include "fboss/cli/fboss2/commands/show/interface/transceiver/performancemonitoring/CmdShowInterfaceTransceiverPerformanceMonitoring.h"
#include "fboss/cli/fboss2/commands/show/l2/CmdShowL2.h"
#include "fboss/cli/fboss2/commands/show/lldp/CmdShowLldp.h"
#include "fboss/cli/fboss2/commands/show/mac/CmdShowMacAddrToBlock.h"
#include "fboss/cli/fboss2/commands/show/mac/CmdShowMacDetails.h"
#include "fboss/cli/fboss2/commands/show/mirror/CmdShowMirror.h"
#include "fboss/cli/fboss2/commands/show/mpls/CmdShowMplsRoute.h"
#include "fboss/cli/fboss2/commands/show/mymac/CmdShowMyMac.h"
#include "fboss/cli/fboss2/commands/show/mysid/CmdShowMySid.h"
#include "fboss/cli/fboss2/commands/show/ndp/CmdShowNdp.h"
#include "fboss/cli/fboss2/commands/show/nexthopgroups/CmdShowNextHopGroups.h"
#include "fboss/cli/fboss2/commands/show/port/CmdShowPort.h"
#include "fboss/cli/fboss2/commands/show/port/CmdShowPortQueue.h"
#ifndef IS_OSS
#include "fboss/cli/fboss2/commands/show/port/facebook/cablelength/CmdShowPortCableLength.h"
#endif
#include "fboss/cli/fboss2/commands/show/product/CmdShowProduct.h"
#include "fboss/cli/fboss2/commands/show/product/CmdShowProductDetails.h"
#include "fboss/cli/fboss2/commands/show/reboot_history/CmdShowRebootHistory.h"
#include "fboss/cli/fboss2/commands/show/rif/CmdShowRif.h"
#include "fboss/cli/fboss2/commands/show/route/CmdShowRoute.h"
#include "fboss/cli/fboss2/commands/show/route/CmdShowRouteCounters.h"
#include "fboss/cli/fboss2/commands/show/route/CmdShowRouteDetails.h"
#include "fboss/cli/fboss2/commands/show/route/CmdShowRouteSummary.h"
#include "fboss/cli/fboss2/commands/show/sdk/dump/CmdShowSdkDump.h"
#include "fboss/cli/fboss2/commands/show/systemport/CmdShowSystemPort.h"
#include "fboss/cli/fboss2/commands/show/teflow/CmdShowTeFlow.h"
#include "fboss/cli/fboss2/commands/show/transceiver/CmdShowTransceiver.h"
#include "fboss/cli/fboss2/commands/show/transceiver/loopback/CmdShowTransceiverLoopback.h"
#include "fboss/cli/fboss2/commands/start/pcap/CmdStartPcap.h"
#include "fboss/cli/fboss2/commands/start/port/CmdStartPort.h"
#include "fboss/cli/fboss2/commands/start/port/cable_length_measurement/CmdStartPortCableLengthMeasurement.h"
#include "fboss/cli/fboss2/commands/stop/pcap/CmdStopPcap.h"
#include "fboss/cli/fboss2/commands/stream/fsdb/CmdStreamSubFsdbOperState.h"
#include "fboss/cli/fboss2/commands/stream/fsdb/CmdStreamSubFsdbOperStats.h"

namespace facebook::fboss {

const CommandTree& kCommandTree() {
  static CommandTree root = {
      {"config",
       "gen",
       "Generate FBOSS service configuration files",
       {{"agent",
         "Generate an Agent configuration file",
         commandHandler<CmdConfigGenAgent>,
         argTypeHandler<CmdConfigGenAgentTraits>,
         localOptionsHandler<CmdConfigGenAgentTraits>}}},

      {"reload",
       "config",
       "Reload binary configuration file",
       {{"agent",
         "Reload the agent config",
         commandHandler<CmdReloadConfigAgent>,
         argTypeHandler<CmdReloadConfigAgentTraits>}}},

      {"show",
       "acl",
       "Show ACL information",
       commandHandler<CmdShowAcl>,
       validFilterHandler<CmdShowAcl>,
       argTypeHandler<CmdShowAclTraits>},

      {"show",
       "agent",
       "Show Agent state",
       {
           {"ssl",
            "Show Agent SSL information",
            commandHandler<CmdShowAgentSsl>,
            argTypeHandler<CmdShowAgentSslTraits>},
           {"boot-type",
            "Show Agent boot type",
            commandHandler<CmdShowAgentBootType>,
            argTypeHandler<CmdShowAgentBootTypeTraits>},
           {"firmware",
            "Show Agent Firmware information",
            commandHandler<CmdShowAgentFirmware>,
            argTypeHandler<CmdShowAgentFirmwareTraits>},
       }},

      {"show",
       "aggregate-port",
       "Show aggregate port information",
       commandHandler<CmdShowAggregatePort>,
       validFilterHandler<CmdShowAggregatePort>,
       argTypeHandler<CmdShowAggregatePortTraits>},

      {"show",
       "fb303-counters",
       "Show raw fb303 counters from a FBOSS service",
       commandHandler<CmdShowFb303Counters>,
       validFilterHandler<CmdShowFb303Counters>,
       argTypeHandler<CmdShowFb303CountersTraits>,
       localOptionsHandler<CmdShowFb303CountersTraits>},

      {"show",
       "arp",
       "Show ARP information",
       commandHandler<CmdShowArp>,
       validFilterHandler<CmdShowArp>,
       argTypeHandler<CmdShowArpTraits>},

      {"show",
       "environment",
       "Show platform environment information",
       {

           {"fan",
            "Show fan status and speed information",
            commandHandler<CmdShowEnvironmentFan>,
            argTypeHandler<CmdShowEnvironmentFanTraits>}}},

      {"show",
       "fabric",
       "Show Fabric connectivity",
       commandHandler<CmdShowFabric>,
       argTypeHandler<CmdShowFabricTraits>,
       {

           {"reachability",
            "Show Fabric ports that can reach the given switch name. Returns reachability data from SW cache by default.",
            commandHandler<CmdShowFabricReachability>,
            argTypeHandler<CmdShowFabricReachabilityTraits>,
            {{"uncached",
              "Get reachability information directly from hardware",
              commandHandler<CmdShowFabricReachabilityUncached>,
              argTypeHandler<CmdShowFabricReachabilityUncachedTraits>}}},
           {"topology",
            "Show Fabric topology per virtual device",
            commandHandler<CmdShowFabricTopology>,
            argTypeHandler<CmdShowFabricTopologyTraits>},
           {"inputBalance",
            "Show Fabric input balanced given destination switch name(s)",
            commandHandler<CmdShowFabricInputBalance>,
            argTypeHandler<CmdShowFabricInputBalanceTraits>},
           {"monitoring",
            "Show Fabric monitoring information",
            {{"counters",
              "Show Fabric link monitoring counters",
              commandHandler<CmdShowFabricMonitoringCounters>,
              argTypeHandler<CmdShowFabricMonitoringCountersTraits>},
             {"details",
              "Show Fabric link monitoring details",
              commandHandler<CmdShowFabricMonitoringDetails>,
              argTypeHandler<CmdShowFabricMonitoringDetailsTraits>}}}}},

      {"show",
       "flowlet",
       "Show Flowlet information",
       commandHandler<CmdShowFlowlet>,
       argTypeHandler<CmdShowFlowletTraits>},

      {"show",
       "fsdb",
       "Show operational information from fsdb",
       {
           {"stats",
            "Show fsdb operational stats",
            commandHandler<CmdShowFsdbOperStats>,
            argTypeHandler<CmdShowFsdbDataCommonTraits>},
           {"state",
            "Show fsdb operational state",
            commandHandler<CmdShowFsdbOperState>,
            argTypeHandler<CmdShowFsdbDataCommonTraits>},
           {"subscribers",
            "Show fsdb subscribers",
            commandHandler<CmdShowFsdbSubscribers>,
            argTypeHandler<CmdShowFsdbSubscriberTraits>},
           {"publishers",
            "Show fsdb publishers",
            commandHandler<CmdShowFsdbPublishers>,
            argTypeHandler<CmdShowFsdbPublisherTraits>},
       }},

      {"show",
       "example",
       "Example command help message",
       commandHandler<CmdShowExample>,
       argTypeHandler<CmdShowExampleTraits>},

      {"show",
       "dsf",
       "Show DSF device information",
       commandHandler<CmdShowDsf>,
       argTypeHandler<CmdShowDsfTraits>,
       {

           {"subscription",
            "Show DSF subscription status/path other other devices",
            commandHandler<CmdShowDsfSubscription>,
            argTypeHandler<CmdShowDsfSubscriptionTraits>}}},

      {"show",
       "dsfnodes",
       "Show DsfNodes cluster",
       commandHandler<CmdShowDsfNodes>,
       argTypeHandler<CmdShowDsfNodesTraits>},

      {"show",
       "lldp",
       "Show LLDP information",
       commandHandler<CmdShowLldp>,
       argTypeHandler<CmdShowLldpTraits>},

      {"show",
       "ndp",
       "Show NDP information",
       commandHandler<CmdShowNdp>,
       validFilterHandler<CmdShowNdp>,
       argTypeHandler<CmdShowNdpTraits>},

      {"show",
       "port",
       "Show Port information",
       commandHandler<CmdShowPort>,
       validFilterHandler<CmdShowPort>,
       argTypeHandler<CmdShowPortTraits>,
       {

           {"queue",
            "Show Port queue information",
            commandHandler<CmdShowPortQueue>,
            argTypeHandler<CmdShowPortQueueTraits>},
#ifndef IS_OSS
           {"cable-length",
            "Show Port cable length information",
            commandHandler<CmdShowPortCableLength>,
            argTypeHandler<CmdShowPortCableLengthTraits>},
#endif
       }},

      {"show",
       "rif",
       "Show RIF information",
       commandHandler<CmdShowRif>,
       validFilterHandler<CmdShowRif>,
       argTypeHandler<CmdShowRifTraits>},

      {"show",
       "interface",
       "Show Interface information",
       commandHandler<CmdShowInterface>,
       argTypeHandler<CmdShowInterfaceTraits>,
       {
           {"counters",
            "Show Interface Counters",
            commandHandler<CmdShowInterfaceCounters>,
            argTypeHandler<CmdShowInterfaceCountersTraits>,
            {
                {"mka",
                 "Show Interface MKA counters",
                 commandHandler<CmdShowInterfaceCountersMKA>,
                 argTypeHandler<CmdShowInterfaceCountersMKATraits>},
                {"fec",
                 "Show Interface counters fec",
                 commandHandler<CmdShowInterfaceCountersFec>,
                 argTypeHandler<CmdShowInterfaceCountersFecTraits>,
                 {
                     {"ber",
                      "Show Interface counters fec ber",
                      commandHandler<CmdShowInterfaceCountersFecBer>,
                      argTypeHandler<CmdShowInterfaceCountersFecBerTraits>},
                     {"uncorrectable",
                      "Show Interface counters fec uncorrectable",
                      commandHandler<CmdShowInterfaceCountersFecUncorrectable>,
                      argTypeHandler<
                          CmdShowInterfaceCountersFecUncorrectableTraits>},
                     {"histogram",
                      "Show Interface counters fec histogram",
                      commandHandler<CmdShowInterfaceCountersFecHistogram>,
                      argTypeHandler<
                          CmdShowInterfaceCountersFecHistogramTraits>},
                     {"tail",
                      "Show Interface counters fec tail",
                      commandHandler<CmdShowInterfaceCountersFecTail>,
                      argTypeHandler<CmdShowInterfaceCountersFecTailTraits>},
                 }},
            }},

           {"errors",
            "Show Interface Error Counters",
            commandHandler<CmdShowInterfaceErrors>,
            argTypeHandler<CmdShowInterfaceErrorsTraits>},

           {"flaps",
            "Show Interface Flap Counters",
            commandHandler<CmdShowInterfaceFlaps>,
            argTypeHandler<CmdShowInterfaceFlapsTraits>},

           {"traffic",
            "Show Interface Traffic",
            commandHandler<CmdShowInterfaceTraffic>,
            argTypeHandler<CmdShowInterfaceTrafficTraits>},

           {"status",
            "Show Interface Status",
            commandHandler<CmdShowInterfaceStatus>,
            argTypeHandler<CmdShowInterfaceStatusTraits>},
           {"phy",
            "Show Phy info for asic and/or xphy",
            commandHandler<CmdShowInterfacePhy>,
            argTypeHandler<CmdShowInterfacePhyTraits>},
           {"phymap",
            "Show External Phy Port Map",
            commandHandler<CmdShowInterfacePhymap>,
            argTypeHandler<CmdShowInterfacePhymapTraits>},
           {"capabilities",
            "Show Supported Port capabilities on the ports",
            commandHandler<CmdShowInterfaceCapabilities>,
            argTypeHandler<CmdShowInterfaceCapabilitiesTraits>},
           {"prbs",
            "Show PRBS information",
            commandHandler<CmdShowInterfacePrbs>,
            argTypeHandler<CmdShowInterfacePrbsTraits>,
            {
                {"capabilities",
                 "Show PRBS capabilities",
                 commandHandler<CmdShowInterfacePrbsCapabilities>,
                 argTypeHandler<CmdShowInterfacePrbsCapabilitiesTraits>},
                {"state",
                 "Show PRBS state",
                 commandHandler<CmdShowInterfacePrbsState>,
                 argTypeHandler<CmdShowInterfacePrbsStateTraits>},
                {"stats",
                 "Show PRBS stats",
                 commandHandler<CmdShowInterfacePrbsStats>,
                 argTypeHandler<CmdShowInterfacePrbsStatsTraits>},
            }},
           {"transceiver",
            "Show Transceiver information for the interface",
            commandHandler<CmdShowInterfaceTransceiver>,
            argTypeHandler<CmdShowInterfaceTransceiverTraits>,
            {
                {"eeprom",
                 "Read transceiver EEPROM registers",
                 commandHandler<CmdShowInterfaceTransceiverEeprom>,
                 argTypeHandler<CmdShowInterfaceTransceiverEepromTraits>,
                 {
                     {"dump",
                      "Dump all diagnostic EEPROM pages",
                      commandHandler<CmdShowInterfaceTransceiverEepromDump>,
                      argTypeHandler<
                          CmdShowInterfaceTransceiverEepromDumpTraits>},
                 }},
                {"performance-monitoring",
                 "Show transceiver VDM performance monitoring stats",
                 commandHandler<
                     CmdShowInterfaceTransceiverPerformanceMonitoring>,
                 argTypeHandler<
                     CmdShowInterfaceTransceiverPerformanceMonitoringTraits>},
            }},
       }},
      {"show",
       "transceiver",
       "Show Transceiver information",
       commandHandler<CmdShowTransceiver>,
       argTypeHandler<CmdShowTransceiverTraits>,
       {{"loopback",
         "Show transceiver loopback capability and state",
         commandHandler<CmdShowTransceiverLoopback>,
         argTypeHandler<CmdShowTransceiverLoopbackTraits>}}},

      {"show",
       "route",
       "Show Route information",
       commandHandler<CmdShowRoute>,
       argTypeHandler<CmdShowRouteTraits>,
       localOptionsHandler<CmdShowRouteTraits>,
       {{"counters",
         "Show route counters",
         commandHandler<CmdShowRouteCounters>,
         argTypeHandler<CmdShowRouteCountersTraits>},
        {"details",
         "Show details of the route table",
         commandHandler<CmdShowRouteDetails>,
         argTypeHandler<CmdShowRouteDetailsTraits>},
        {"summary",
         "Print a summary of routing tables",
         commandHandler<CmdShowRouteSummary>,
         argTypeHandler<CmdShowRouteSummaryTraits>}}},

      {"show",
       "mac",
       "Show MAC information",
       {{"blocked",
         "Show details of blocked MAC addresses",
         commandHandler<CmdShowMacAddrToBlock>,
         argTypeHandler<CmdShowMacAddrToBlockTraits>},
        {"details",
         "Show details of the MAC(L2) table",
         commandHandler<CmdShowMacDetails>,
         argTypeHandler<CmdShowMacDetailsTraits>}}},

      {"show",
       "mirror",
       "Show mirror",
       commandHandler<CmdShowMirror>,
       argTypeHandler<CmdShowMirrorTraits>},

      {"show",
       "my-mac",
       "Show MY_MAC entries",
       commandHandler<CmdShowMyMac>,
       argTypeHandler<CmdShowMyMacTraits>},

      {"show",
       "mysid",
       "Show MySid entries",
       commandHandler<CmdShowMySid>,
       argTypeHandler<CmdShowMySidTraits>},

      {"show",
       "mpls",
       "Show MPLS information",
       {{"route",
         "Show details of MPLS routes",
         commandHandler<CmdShowMplsRoute>,
         argTypeHandler<CmdShowMplsRouteTraits>}}},

      {"show",
       "nexthopgroups",
       "Show next hop groups",
       commandHandler<CmdShowNextHopGroups>,
       argTypeHandler<CmdShowNextHopGroupsTraits>},

      {"show",
       "namednexthopgroups",
       "Show named next hop groups",
       commandHandler<CmdShowNamedNextHopGroups>,
       argTypeHandler<CmdShowNamedNextHopGroupsTraits>},

      {
          "show",
          "sdk",
          "Show SDK state",
          {
              {"dump", // rename this to "qsfp", "dump" is too generic
               "Get QSFP SDK state (NOTE: this only dumps qsfp sdk state)",
               commandHandler<CmdShowQsfpSdkDump>,
               argTypeHandler<CmdShowQsfpSdkDumpTraits>},
              {"agent",
               "Get Agent SDK state",
               commandHandler<CmdShowAgentSdkDump>,
               argTypeHandler<CmdShowAgentSdkDumpTraits>},
          },
      },

      {"show",
       "cpuport",
       "Show cpu port information",
       commandHandler<CmdShowCpuPort>,
       validFilterHandler<CmdShowCpuPort>,
       argTypeHandler<CmdShowCpuPortTraits>},

      {"show",
       "systemport",
       "Show system port information",
       commandHandler<CmdShowSystemPort>,
       validFilterHandler<CmdShowSystemPort>,
       argTypeHandler<CmdShowSystemPortTraits>},

      {"show",
       "teflow",
       "Show teflow information",
       commandHandler<CmdShowTeFlow>,
       argTypeHandler<CmdShowTeFlowTraits>},

      {"show",
       "host",
       "Show Host Information",
       commandHandler<CmdShowHost>,
       argTypeHandler<CmdShowHostTraits>},

      {"show",
       "hw-object",
       "Show HW Objects",
       commandHandler<CmdShowHwObject>,
       argTypeHandler<CmdShowHwObjectTraits>,
       {{"uncached",
         "Query hardware directly instead of cache",
         commandHandler<CmdShowHwObjectUncached>,
         argTypeHandler<CmdShowHwObjectUncachedTraits>}}},

      {"show",
       "hw-agent",
       "Show HwAgent Status",
       commandHandler<CmdShowHwAgentStatus>,
       argTypeHandler<CmdShowHwAgentStatusTraits>},

      {"show",
       "l2",
       "Show L2 Table",
       commandHandler<CmdShowL2>,
       argTypeHandler<CmdShowL2Traits>},

      {"clear",
       "arp",
       "Clear ARP information",
       commandHandler<CmdClearArp>,
       argTypeHandler<CmdClearArpTraits>},

      {"clear",
       "ndp",
       "Clear NDP information",
       commandHandler<CmdClearNdp>,
       argTypeHandler<CmdClearNdpTraits>},

      {
          "clear",
          "interface",
          "Clear Interface Information",
          commandHandler<CmdClearInterface>,
          argTypeHandler<CmdClearInterfaceTraits>,
          {
              {"counters",
               "Clear Interface Counters",
               commandHandler<CmdClearInterfaceCounters>,
               argTypeHandler<CmdClearInterfaceCountersTraits>,
               {{"phy",
                 "Clear Interface Phy Counters",
                 commandHandler<CmdClearInterfaceCountersPhy>,
                 argTypeHandler<CmdClearInterfaceCountersPhyTraits>}}},
              {
                  "prbs",
                  "Clear PRBS Information",
                  commandHandler<CmdClearInterfacePrbs>,
                  argTypeHandler<CmdClearInterfacePrbsTraits>,
                  {{"stats",
                    "Clear PRBS stats",
                    commandHandler<CmdClearInterfacePrbsStats>,
                    argTypeHandler<CmdClearInterfacePrbsStatsTraits>}},
              },
          },
      },
      {"bounce",
       "interface",
       "Shut/No-Shut Interface",
       commandHandler<CmdBounceInterface>,
       argTypeHandler<CmdBounceInterfaceTraits>,
       localOptionsHandler<CmdBounceInterfaceTraits>},
      {
          "set",
          "interface",
          "Set Interface information",
          commandHandler<CmdSetInterface>,
          argTypeHandler<CmdSetInterfaceTraits>,
          {{
               "prbs",
               "Set PRBS properties",
               commandHandler<CmdSetInterfacePrbs>,
               argTypeHandler<CmdSetInterfacePrbsTraits>,
               {{"state",
                 "Set PRBS state",
                 commandHandler<CmdSetInterfacePrbsState>,
                 argTypeHandler<CmdSetInterfacePrbsStateTraits>}},
           },
           {
               "loopback",
               "Set loopback mode: <component> <enable|disable>",
               commandHandler<CmdSetInterfaceLoopback>,
               argRegistrar<CmdSetInterfaceLoopbackTraits>,
           },
           {
               "admin-state",
               "Set XPHY admin state: <xphy_system|xphy_line> <enable|disable>",
               commandHandler<CmdSetInterfaceAdminState>,
               argRegistrar<CmdSetInterfaceAdminStateTraits>,
               localOptionsHandler<CmdSetInterfaceAdminStateTraits>,
           }},
      },
      {"set",
       "port",
       "Set Port information",
       commandHandler<CmdSetPort>,
       argTypeHandler<CmdSetPortTraits>,
       {{"state",
         "Set Port state",
         commandHandler<CmdSetPortState>,
         argTypeHandler<CmdSetPortStateTraits>,
         localOptionsHandler<CmdSetPortStateTraits>}}},
      {"set",
       "sdk",
       "Set SDK information",
       commandHandler<CmdSetSdk>,
       argRegistrar<CmdSetSdkTraits>,
       {{"reg-dump",
         "Enable or disable the SDK dumping register/state logs to disk",
         commandHandler<CmdSetSdkRegDump>,
         argRegistrar<CmdSetSdkRegDumpTraits>}}},
      {"set",
       "transceiver",
       "Set Transceiver properties",
       commandHandler<CmdSetTransceiver>,
       argTypeHandler<CmdSetTransceiverTraits>,
       {{"loopback",
         "Set transceiver loopback mode",
         commandHandler<CmdSetTransceiverLoopback>,
         argRegistrar<CmdSetTransceiverLoopbackTraits>}}},

      {"start",
       "pcap",
       "Start Packet Capture",
       commandHandler<CmdStartPcap>,
       argTypeHandler<CmdStartPcapTraits>,
       localOptionsHandler<CmdStartPcapTraits>},

      {"start",
       "port",
       "Start Port event",
       commandHandler<CmdStartPort>,
       argTypeHandler<CmdStartPortTraits>,
       {{"cable-length-measurement",
         "Trigger cable length measurement",
         commandHandler<CmdStartPortCableLengthMeasurement>,
         argTypeHandler<CmdStartPortCableLengthMeasurementTraits>}}},

      {"stop",
       "pcap",
       "Stop Packet Capture",
       commandHandler<CmdStopPcap>,
       argTypeHandler<CmdStopPcapTraits>,
       localOptionsHandler<CmdStopPcapTraits>},

      {"get",
       "pcap",
       "Get Packet Capture",
       commandHandler<CmdGetPcap>,
       argTypeHandler<CmdGetPcapTraits>,
       localOptionsHandler<CmdGetPcapTraits>},

      {"show",
       "product",
       "Show Product Information",
       commandHandler<CmdShowProduct>,
       argTypeHandler<CmdShowProductTraits>,
       {{"details",
         "Show Product Detail Information",
         commandHandler<CmdShowProductDetails>,
         argTypeHandler<CmdShowProductDetailsTraits>}}},

      {"show",
       "reboot-history",
       "Show the switch's reboot history and causes",
       commandHandler<CmdShowRebootHistory>,
       argTypeHandler<CmdShowRebootHistoryTraits>},

      {"stream",
       "fsdb",
       "Stream fsdb operational information",
       {
           {"stats",
            "Stream fsdb operational stats",
            commandHandler<CmdStreamSubFsdbOperStats>,
            argTypeHandler<CmdStreamSubFsdbOperStatsTraits>},
           {"state",
            "Stream fsdb operational state",
            commandHandler<CmdStreamSubFsdbOperState>,
            argTypeHandler<CmdStreamSubFsdbOperStateTraits>},
       }},
  };
  stable_sort(root.begin(), root.end());
  return root;
}

} // namespace facebook::fboss
