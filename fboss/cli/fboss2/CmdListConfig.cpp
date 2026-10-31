/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <algorithm>
#include "fboss/cli/fboss2/CmdList.h"
#include "fboss/cli/fboss2/commands/config/CmdConfigAppliedInfo.h"
#include "fboss/cli/fboss2/commands/config/CmdConfigReload.h"
#include "fboss/cli/fboss2/commands/config/acl/CmdConfigAcl.h"
#include "fboss/cli/fboss2/commands/config/acl/rule/CmdConfigAclRule.h"
#include "fboss/cli/fboss2/commands/config/acl/table/CmdConfigAclTable.h"
#include "fboss/cli/fboss2/commands/config/acl/table_group/CmdConfigAclTableGroup.h"
#include "fboss/cli/fboss2/commands/config/arp/CmdConfigArp.h"
#include "fboss/cli/fboss2/commands/config/copp/CmdConfigCopp.h"
#include "fboss/cli/fboss2/commands/config/data_plane/CmdConfigDataPlane.h"
#include "fboss/cli/fboss2/commands/config/data_plane/traffic_policy/CmdConfigDataPlaneTrafficPolicy.h"
#include "fboss/cli/fboss2/commands/config/dhcp/CmdConfigDhcp.h"
#include "fboss/cli/fboss2/commands/config/dhcp/relay_source_override/CmdConfigDhcpRelaySourceOverride.h"
#include "fboss/cli/fboss2/commands/config/dhcp/reply_source_override/CmdConfigDhcpReplySourceOverride.h"
#include "fboss/cli/fboss2/commands/config/history/CmdConfigHistory.h"
#include "fboss/cli/fboss2/commands/config/interface/CmdConfigInterface.h"
#include "fboss/cli/fboss2/commands/config/interface/ipv6/CmdConfigInterfaceIpv6.h"
#include "fboss/cli/fboss2/commands/config/interface/ipv6/ndp/CmdConfigInterfaceIpv6Ndp.h"
#include "fboss/cli/fboss2/commands/config/interface/pfc_config/CmdConfigInterfacePfcConfig.h"
#include "fboss/cli/fboss2/commands/config/interface/sflow/CmdConfigInterfaceSflow.h"
#include "fboss/cli/fboss2/commands/config/interface/switchport/CmdConfigInterfaceSwitchport.h"
#include "fboss/cli/fboss2/commands/config/interface/switchport/access/CmdConfigInterfaceSwitchportAccess.h"
#include "fboss/cli/fboss2/commands/config/interface/switchport/access/vlan/CmdConfigInterfaceSwitchportAccessVlan.h"
#include "fboss/cli/fboss2/commands/config/l2/CmdConfigL2.h"
#include "fboss/cli/fboss2/commands/config/l2/learning_mode/CmdConfigL2LearningMode.h"
#include "fboss/cli/fboss2/commands/config/load_balancing/CmdConfigLoadBalancing.h"
#include "fboss/cli/fboss2/commands/config/mac/CmdConfigMac.h"
#include "fboss/cli/fboss2/commands/config/mac/aging_time/CmdConfigMacAgingTime.h"
#include "fboss/cli/fboss2/commands/config/protocol/CmdConfigProtocol.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/CmdConfigProtocolBgp.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/global/CmdConfigProtocolBgpGlobal.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/neighbor/CmdConfigProtocolBgpNeighbor.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/peer-group/CmdConfigProtocolBgpPeerGroup.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/CmdConfigProtocolBgpPolicy.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/as-path-list/CmdConfigProtocolBgpPolicyAsPathList.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/community-list/CmdConfigProtocolBgpPolicyCommunityList.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/prefix-list/CmdConfigProtocolBgpPolicyPrefixList.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/prefix-list/entry/CmdConfigProtocolBgpPolicyPrefixListEntry.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/CmdConfigProtocolBgpPolicyRoutingPolicy.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/term/CmdConfigProtocolBgpPolicyRoutingPolicyTerm.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/term/action/CmdConfigProtocolBgpPolicyRoutingPolicyTermAction.h"
#include "fboss/cli/fboss2/commands/config/protocol/static/CmdConfigProtocolStatic.h"
#include "fboss/cli/fboss2/commands/config/protocol/static/route/add/CmdConfigProtocolStaticRouteAdd.h"
#include "fboss/cli/fboss2/commands/config/ptp/CmdConfigPtp.h"
#include "fboss/cli/fboss2/commands/config/ptp/transparent_clock/CmdConfigPtpTransparentClock.h"
#include "fboss/cli/fboss2/commands/config/qos/CmdConfigQos.h"
#include "fboss/cli/fboss2/commands/config/qos/buffer_pool/CmdConfigQosBufferPool.h"
#include "fboss/cli/fboss2/commands/config/qos/default_policy/CmdConfigQosDefaultPolicy.h"
#include "fboss/cli/fboss2/commands/config/qos/policy/CmdConfigQosPolicy.h"
#include "fboss/cli/fboss2/commands/config/qos/policy/CmdConfigQosPolicyMap.h"
#include "fboss/cli/fboss2/commands/config/qos/priority_group_policy/CmdConfigQosPriorityGroupPolicy.h"
#include "fboss/cli/fboss2/commands/config/qos/priority_group_policy/CmdConfigQosPriorityGroupPolicyGroupId.h"
#include "fboss/cli/fboss2/commands/config/qos/queue_config/CmdConfigQosQueueConfig.h"
#include "fboss/cli/fboss2/commands/config/qos/queue_config/CmdConfigQosQueueConfigQueueId.h"
#include "fboss/cli/fboss2/commands/config/rollback/CmdConfigRollback.h"
#include "fboss/cli/fboss2/commands/config/session/CmdConfigSessionClear.h"
#include "fboss/cli/fboss2/commands/config/session/CmdConfigSessionCommit.h"
#include "fboss/cli/fboss2/commands/config/session/CmdConfigSessionDiff.h"
#include "fboss/cli/fboss2/commands/config/session/CmdConfigSessionRebase.h"
#include "fboss/cli/fboss2/commands/config/srv6/CmdConfigSrv6.h"
#include "fboss/cli/fboss2/commands/config/srv6/my_sid/CmdConfigSrv6MySid.h"
#include "fboss/cli/fboss2/commands/config/srv6/my_sid/entry/CmdConfigSrv6MySidEntry.h"
#include "fboss/cli/fboss2/commands/config/switch/CmdConfigSwitch.h"
#include "fboss/cli/fboss2/commands/config/switch/admin_distance/CmdConfigAdminDistance.h"
#include "fboss/cli/fboss2/commands/config/switch/hostname/CmdConfigHostname.h"
#include "fboss/cli/fboss2/commands/config/switch/icmpv4_unavailable_src_addr/CmdConfigIcmpV4UnavailableSrcAddr.h"
#include "fboss/cli/fboss2/commands/config/traffic_counter/CmdConfigTrafficCounter.h"
#include "fboss/cli/fboss2/commands/config/tunnel/CmdConfigTunnel.h"
#include "fboss/cli/fboss2/commands/config/tunnel/ip_in_ip/CmdConfigTunnelIpInIp.h"
#include "fboss/cli/fboss2/commands/config/tunnel/ip_in_ip/decap/CmdConfigTunnelIpInIpDecap.h"
#include "fboss/cli/fboss2/commands/config/tunnel/ip_in_ip/encap/CmdConfigTunnelIpInIpEncap.h"
#include "fboss/cli/fboss2/commands/config/vlan/CmdConfigVlan.h"
#include "fboss/cli/fboss2/commands/config/vlan/CmdConfigVlanDefault.h"
#include "fboss/cli/fboss2/commands/config/vlan/port/CmdConfigVlanPort.h"
#include "fboss/cli/fboss2/commands/config/vlan/port/tagging_mode/CmdConfigVlanPortTaggingMode.h"
#include "fboss/cli/fboss2/commands/config/vlan/static_mac/CmdConfigVlanStaticMac.h"
#include "fboss/cli/fboss2/commands/config/vlan/static_mac/add/CmdConfigVlanStaticMacAdd.h"
#include "fboss/cli/fboss2/commands/config/vlan/static_mac/delete/CmdConfigVlanStaticMacDelete.h"
#include "fboss/cli/fboss2/commands/delete/acl/CmdDeleteAcl.h"
#include "fboss/cli/fboss2/commands/delete/acl/rule/CmdDeleteAclRule.h"
#include "fboss/cli/fboss2/commands/delete/acl/table/CmdDeleteAclTable.h"
#include "fboss/cli/fboss2/commands/delete/arp/CmdDeleteArp.h"
#include "fboss/cli/fboss2/commands/delete/config/CmdDeleteConfig.h"
#include "fboss/cli/fboss2/commands/delete/copp/CmdDeleteCopp.h"
#include "fboss/cli/fboss2/commands/delete/copp/queue/CmdDeleteCoppQueue.h"
#include "fboss/cli/fboss2/commands/delete/copp/reason/CmdDeleteCoppReason.h"
#include "fboss/cli/fboss2/commands/delete/copp/traffic_policy/CmdDeleteCoppTrafficPolicy.h"
#include "fboss/cli/fboss2/commands/delete/copp/traffic_policy/match/CmdDeleteCoppTrafficPolicyMatch.h"
#include "fboss/cli/fboss2/commands/delete/copp/traffic_policy/match/action/CmdDeleteCoppTrafficPolicyMatchAction.h"
#include "fboss/cli/fboss2/commands/delete/data_plane/CmdDeleteDataPlane.h"
#include "fboss/cli/fboss2/commands/delete/data_plane/traffic_policy/CmdDeleteDataPlaneTrafficPolicy.h"
#include "fboss/cli/fboss2/commands/delete/data_plane/traffic_policy/match/CmdDeleteDataPlaneTrafficPolicyMatch.h"
#include "fboss/cli/fboss2/commands/delete/data_plane/traffic_policy/match/action/CmdDeleteDataPlaneTrafficPolicyMatchAction.h"
#include "fboss/cli/fboss2/commands/delete/dhcp/CmdDeleteDhcp.h"
#include "fboss/cli/fboss2/commands/delete/dhcp/relay_source_override/CmdDeleteDhcpRelaySourceOverride.h"
#include "fboss/cli/fboss2/commands/delete/dhcp/reply_source_override/CmdDeleteDhcpReplySourceOverride.h"
#include "fboss/cli/fboss2/commands/delete/interface/CmdDeleteInterface.h"
#include "fboss/cli/fboss2/commands/delete/interface/ipv6/CmdDeleteInterfaceIpv6.h"
#include "fboss/cli/fboss2/commands/delete/interface/ipv6/ndp/CmdDeleteInterfaceIpv6Ndp.h"
#include "fboss/cli/fboss2/commands/delete/interface/sflow/CmdDeleteInterfaceSflow.h"
#include "fboss/cli/fboss2/commands/delete/load_balancing/CmdDeleteLoadBalancing.h"
#include "fboss/cli/fboss2/commands/delete/protocol/CmdDeleteProtocol.h"
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/CmdDeleteProtocolBgp.h"
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/neighbor/CmdDeleteProtocolBgpNeighbor.h"
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/peer-group/CmdDeleteProtocolBgpPeerGroup.h"
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/policy/CmdDeleteProtocolBgpPolicy.h"
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/policy/as-path-list/CmdDeleteProtocolBgpPolicyAsPathList.h"
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/policy/community-list/CmdDeleteProtocolBgpPolicyCommunityList.h"
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/policy/prefix-list/CmdDeleteProtocolBgpPolicyPrefixList.h"
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/policy/routing-policy/CmdDeleteProtocolBgpPolicyRoutingPolicy.h"
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/policy/routing-policy/term/CmdDeleteProtocolBgpPolicyRoutingPolicyTerm.h"
#include "fboss/cli/fboss2/commands/delete/protocol/static/CmdDeleteProtocolStatic.h"
#include "fboss/cli/fboss2/commands/delete/protocol/static/route/CmdDeleteProtocolStaticRoute.h"
#include "fboss/cli/fboss2/commands/delete/qos/CmdDeleteQos.h"
#include "fboss/cli/fboss2/commands/delete/qos/default_policy/CmdDeleteQosDefaultPolicy.h"
#include "fboss/cli/fboss2/commands/delete/qos/policy/CmdDeleteQosPolicy.h"
#include "fboss/cli/fboss2/commands/delete/qos/policy/CmdDeleteQosPolicyMap.h"
#include "fboss/cli/fboss2/commands/delete/qos/queue_config/CmdDeleteQosQueueConfig.h"
#include "fboss/cli/fboss2/commands/delete/qos/queue_config/CmdDeleteQosQueueConfigQueueId.h"
#include "fboss/cli/fboss2/commands/delete/srv6/CmdDeleteSrv6.h"
#include "fboss/cli/fboss2/commands/delete/srv6/my_sid/CmdDeleteSrv6MySid.h"
#include "fboss/cli/fboss2/commands/delete/srv6/my_sid/entry/CmdDeleteSrv6MySidEntry.h"
#include "fboss/cli/fboss2/commands/delete/switch/CmdDeleteSwitch.h"
#include "fboss/cli/fboss2/commands/delete/switch/admin_distance/CmdDeleteAdminDistance.h"
#include "fboss/cli/fboss2/commands/delete/switch/icmpv4_unavailable_src_addr/CmdDeleteIcmpV4UnavailableSrcAddr.h"
#include "fboss/cli/fboss2/commands/delete/traffic_counter/CmdDeleteTrafficCounter.h"
#include "fboss/cli/fboss2/commands/delete/tunnel/CmdDeleteTunnel.h"
#include "fboss/cli/fboss2/commands/delete/tunnel/ip_in_ip/CmdDeleteTunnelIpInIp.h"
#include "fboss/cli/fboss2/commands/delete/tunnel/ip_in_ip/decap/CmdDeleteTunnelIpInIpDecap.h"
#include "fboss/cli/fboss2/commands/delete/tunnel/ip_in_ip/encap/CmdDeleteTunnelIpInIpEncap.h"
#include "fboss/cli/fboss2/commands/delete/vlan/CmdDeleteVlan.h"

namespace facebook::fboss {

const CommandTree& kConfigCommandTree() {
  static CommandTree root = {
      {
          "config",
          "switch",
          "Configure switch-level settings",
          commandHandler<CmdConfigSwitch>,
          argRegistrar<CmdConfigSwitchTraits>,
          {{
               "admin-distance",
               "Set administrative distance for a routing client",
               commandHandler<CmdConfigAdminDistance>,
               argRegistrar<CmdConfigAdminDistanceTraits>,
           },
           {
               "hostname",
               "Set switch hostname",
               commandHandler<CmdConfigHostname>,
               argRegistrar<CmdConfigHostnameTraits>,
           },
           {
               "icmpv4-unavailable-src-addr",
               "Set IPv4 source address for ICMP when no address is configured",
               commandHandler<CmdConfigIcmpV4UnavailableSrcAddr>,
               argRegistrar<CmdConfigIcmpV4UnavailableSrcAddrTraits>,
           }},
      },

      {"config",
       "applied-info",
       "Show config applied information",
       commandHandler<CmdConfigAppliedInfo>,
       argRegistrar<CmdConfigAppliedInfoTraits>},

      {
          "config",
          "acl",
          "Configure ACL settings",
          commandHandler<CmdConfigAcl>,
          argRegistrar<CmdConfigAclTraits>,
          {{
               "rule",
               "Configure an ACL rule (e.g. match fields on an AclEntry)",
               commandHandler<CmdConfigAclRule>,
               argRegistrar<CmdConfigAclRuleTraits>,
           },
           {
               "table",
               "Create or configure an ACL table within a table-group",
               commandHandler<CmdConfigAclTable>,
               argRegistrar<CmdConfigAclTableTraits>,
           },
           {
               "table-group",
               "Create or configure an ACL table-group and its stage",
               commandHandler<CmdConfigAclTableGroup>,
               argRegistrar<CmdConfigAclTableGroupTraits>,
           }},
      },

      {
          "config",
          "copp",
          "Configure CoPP (control-plane policing) settings",
          commandHandler<CmdConfigCopp>,
          argRegistrar<CmdConfigCoppTraits>,
          {{
               "queue",
               "Configure a CPU queue (bandwidth shaping)",
               commandHandler<CmdConfigCoppQueue>,
               argRegistrar<CmdConfigCoppQueueTraits>,
           },
           {
               "traffic-policy",
               "Set a CPU-plane action on a named ACL rule",
               commandHandler<CmdConfigCoppTrafficPolicy>,
               argRegistrar<CmdConfigCoppTrafficPolicyTraits>,
           },
           {
               "reason",
               "Map a packet-rx reason to a CPU queue",
               commandHandler<CmdConfigCoppReason>,
               argRegistrar<CmdConfigCoppReasonTraits>,
           }},
      },

      {
          "config",
          "data-plane",
          "Configure dataplane settings",
          commandHandler<CmdConfigDataPlane>,
          argRegistrar<CmdConfigDataPlaneTraits>,
          {{
              "traffic-policy",
              "Set a dataplane action on a named ACL rule",
              commandHandler<CmdConfigDataPlaneTrafficPolicy>,
              argRegistrar<CmdConfigDataPlaneTrafficPolicyTraits>,
          }},
      },

      {"config",
       "arp",
       "Configure ARP settings",
       commandHandler<CmdConfigArp>,
       argRegistrar<CmdConfigArpTraits>},

      {
          "config",
          "dhcp",
          "Configure DHCP settings",
          commandHandler<CmdConfigDhcp>,
          argRegistrar<CmdConfigDhcpTraits>,
          {{
               "relay-source-override",
               "Override source IP for DHCP relay packets to the DHCP server (ipv4|ipv6 <IP>)",
               commandHandler<CmdConfigDhcpRelaySourceOverride>,
               argRegistrar<CmdConfigDhcpRelaySourceOverrideTraits>,
           },
           {
               "reply-source-override",
               "Override source IP for DHCP reply packets to the client (ipv4|ipv6 <IP>)",
               commandHandler<CmdConfigDhcpReplySourceOverride>,
               argRegistrar<CmdConfigDhcpReplySourceOverrideTraits>,
           }},
      },

      {"config",
       "history",
       "Show history of committed config revisions",
       commandHandler<CmdConfigHistory>,
       argRegistrar<CmdConfigHistoryTraits>},

      {
          "config",
          "interface",
          "Configure interface settings",
          commandHandler<CmdConfigInterface>,
          argRegistrar<CmdConfigInterfaceTraits>,
          {{
               "pfc-config",
               "Configure PFC settings for interface",
               commandHandler<CmdConfigInterfacePfcConfig>,
               argRegistrar<CmdConfigInterfacePfcConfigTraits>,
           },
           {
               "ipv6",
               "Configure IPv6 settings for interface",
               commandHandler<CmdConfigInterfaceIpv6>,
               argTypeHandler<CmdConfigInterfaceIpv6Traits>,
               {{
                   "ndp",
                   "Configure IPv6 Neighbor Discovery (NDP/RA) settings",
                   commandHandler<CmdConfigInterfaceIpv6Ndp>,
                   argRegistrar<CmdConfigInterfaceIpv6NdpTraits>,
               }},
           },
           {
               "sflow",
               "Configure sFlow settings: sample-dest <cpu|mirror>, "
               "ingress-rate <N>, egress-rate <N>",
               commandHandler<CmdConfigInterfaceSflow>,
               argRegistrar<CmdConfigInterfaceSflowTraits>,
           },
           {
               "switchport",
               "Configure switchport settings",
               commandHandler<CmdConfigInterfaceSwitchport>,
               argRegistrar<CmdConfigInterfaceSwitchportTraits>,
               {{
                   "access",
                   "Configure access mode settings",
                   commandHandler<CmdConfigInterfaceSwitchportAccess>,
                   argRegistrar<CmdConfigInterfaceSwitchportAccessTraits>,
                   {{
                       "vlan",
                       "Set access VLAN (ingressVlan) for the interface",
                       commandHandler<CmdConfigInterfaceSwitchportAccessVlan>,
                       argRegistrar<
                           CmdConfigInterfaceSwitchportAccessVlanTraits>,
                   }},
               }},
           }},
      },

      {
          "config",
          "l2",
          "Configure L2 settings",
          commandHandler<CmdConfigL2>,
          argRegistrar<CmdConfigL2Traits>,
          {{
              "learning-mode",
              "Set L2 learning mode (hardware, software, or disabled)",
              commandHandler<CmdConfigL2LearningMode>,
              argRegistrar<CmdConfigL2LearningModeTraits>,
          }},
      },

      {
          "config",
          "mac",
          "Configure MAC settings",
          commandHandler<CmdConfigMac>,
          argRegistrar<CmdConfigMacTraits>,
          {{
              "aging-time",
              "Set MAC address aging time in seconds",
              commandHandler<CmdConfigMacAgingTime>,
              argRegistrar<CmdConfigMacAgingTimeTraits>,
          }},
      },

      {
          "config",
          "load-balancing",
          "Configure load-balancing (ECMP/LAG) settings",
          {{
               "ecmp",
               "Configure ECMP hash fields, algorithm, and seed",
               commandHandler<CmdConfigLoadBalancingEcmp>,
               argRegistrar<CmdConfigLoadBalancingEcmpTraits>,
           },
           {
               "lag",
               "Configure LAG hash fields, algorithm, and seed",
               commandHandler<CmdConfigLoadBalancingLag>,
               argRegistrar<CmdConfigLoadBalancingLagTraits>,
           }},
      },

      {
          "config",
          "protocol",
          "Configure protocol settings",
          commandHandler<CmdConfigProtocol>,
          argRegistrar<CmdConfigProtocolTraits>,
          {
              {
                  "bgp",
                  "Configure BGP protocol",
                  commandHandler<CmdConfigProtocolBgp>,
                  argRegistrar<CmdConfigProtocolBgpTraits>,
                  {
                      {
                          "global",
                          "Configure BGP global settings: <attribute> <value> "
                          "(router-id, local-asn, hold-time, confed-asn, "
                          "count-confeds-in-as-path-len, "
                          "graceful-restart-time, rib-allocated-path-ids, "
                          "network6, switch-limit[-total-path|"
                          "-max-golden-vips|-overload-protection-mode])",
                          commandHandler<CmdConfigProtocolBgpGlobal>,
                          argRegistrar<CmdConfigProtocolBgpGlobalTraits>,
                      },
                      {
                          "peer-group",
                          "Configure BGP peer-group: <name> "
                          "[<attribute> <value> ...] (remote-asn, local-asn, "
                          "description, peer-tag, ingress-policy, "
                          "egress-policy, rr-client, confed-peer, "
                          "redistribute-peer, enhanced-route-refresh, "
                          "route-refresh, remove-private-as, "
                          "enforce-first-as, passive, next-hop-self, "
                          "ttl-security-hops, link-bandwidth, "
                          "advertise-lbw, receive-lbw, add-path send|receive, "
                          "afi disable-ipv4-afi|disable-ipv6-afi|"
                          "ipv4-over-ipv6-nh, "
                          "graceful-restart restart-time|stateful-ha, "
                          "max-route pre-filter|post-filter|"
                          "pre-warning-threshold|post-warning-threshold|"
                          "pre-warning-only|post-warning-only, "
                          "timers hold-time|keepalive|out-delay|"
                          "withdraw-unprog-delay)",
                          commandHandler<CmdConfigProtocolBgpPeerGroup>,
                          argRegistrar<CmdConfigProtocolBgpPeerGroupTraits>,
                      },
                      {
                          "neighbor",
                          "Configure BGP neighbor: <ip-address> "
                          "[<attribute> <value> ...]; `neighbor --help` "
                          "lists the attributes",
                          commandHandler<CmdConfigProtocolBgpNeighbor>,
                          argRegistrar<CmdConfigProtocolBgpNeighborTraits>,
                      },
                      {
                          "policy",
                          "Configure BGP policy objects",
                          commandHandler<CmdConfigProtocolBgpPolicy>,
                          argRegistrar<CmdConfigProtocolBgpPolicyTraits>,
                          {{
                               "as-path-list",
                               "Configure BGP AS-path list: <name> "
                               "[<attribute> <value> ...] (description, "
                               "regex, boolean-operator)",
                               commandHandler<
                                   CmdConfigProtocolBgpPolicyAsPathList>,
                               argRegistrar<
                                   CmdConfigProtocolBgpPolicyAsPathListTraits>,
                           },
                           {
                               "community-list",
                               "Configure BGP community-list: <name> "
                               "[<attribute> <value> ...] "
                               "(boolean-operator, community, description, "
                               "exact-match)",
                               commandHandler<
                                   CmdConfigProtocolBgpPolicyCommunityList>,
                               argRegistrar<
                                   CmdConfigProtocolBgpPolicyCommunityListTraits>,
                           },
                           {
                               "prefix-list",
                               "Configure BGP prefix-list: <name> "
                               "[<attribute> <value> ...] "
                               "(boolean-operator, description, "
                               "ip-version)",
                               commandHandler<
                                   CmdConfigProtocolBgpPolicyPrefixList>,
                               argRegistrar<
                                   CmdConfigProtocolBgpPolicyPrefixListTraits>,
                               {{
                                   "prefix",
                                   "Configure a prefix-list entry: "
                                   "<prefix/len> [<attribute> <value> ...] "
                                   "(communities, description, match-logic, "
                                   "max-allowed-subnet-count, "
                                   "prefix-len-range, regex)",
                                   commandHandler<
                                       CmdConfigProtocolBgpPolicyPrefixListEntry>,
                                   argRegistrar<
                                       CmdConfigProtocolBgpPolicyPrefixListEntryTraits>,
                               }},
                           },
                           {
                               "routing-policy",
                               "Configure BGP routing-policy: <name> "
                               "[<attribute> <value> ...] "
                               "(description)",
                               commandHandler<
                                   CmdConfigProtocolBgpPolicyRoutingPolicy>,
                               argRegistrar<
                                   CmdConfigProtocolBgpPolicyRoutingPolicyTraits>,
                               {{
                                   "term",
                                   "Configure a routing-policy term: "
                                   "<seq-num> [<attribute> <value> ...] "
                                   "(description)",
                                   commandHandler<
                                       CmdConfigProtocolBgpPolicyRoutingPolicyTerm>,
                                   argRegistrar<
                                       CmdConfigProtocolBgpPolicyRoutingPolicyTermTraits>,
                                   {{
                                       "action",
                                       "Configure a term action",
                                       {{
                                            "result",
                                            "Set the term result: "
                                            "<ACCEPT|REJECT|CONTINUE>",
                                            commandHandler<
                                                CmdConfigProtocolBgpPolicyRoutingPolicyTermActionResult>,
                                            argRegistrar<
                                                CmdConfigProtocolBgpPolicyRoutingPolicyTermActionResultTraits>,
                                        },
                                        {
                                            "set",
                                            "Set a route attribute: "
                                            "as-path prepend|community|"
                                            "local-pref|med|next-hop|"
                                            "origin|weight <value> ...",
                                            commandHandler<
                                                CmdConfigProtocolBgpPolicyRoutingPolicyTermActionSet>,
                                            argRegistrar<
                                                CmdConfigProtocolBgpPolicyRoutingPolicyTermActionSetTraits>,
                                        }},
                                   }},
                               }},
                           }},
                      },
                  },
              },
              {
                  "static",
                  "Configure static routing",
                  commandHandler<CmdConfigProtocolStatic>,
                  argTypeHandler<CmdConfigProtocolStaticTraits>,
                  {{
                       "ip",
                       "Configure IPv4 static routes",
                       {{
                           "route",
                           "Add an IPv4 static route",
                           commandHandler<CmdConfigProtocolStaticIpRouteAdd>,
                           argRegistrar<
                               CmdConfigProtocolStaticIpRouteAddTraits>,
                       }},
                   },
                   {
                       "ipv6",
                       "Configure IPv6 static routes",
                       {{
                           "route",
                           "Add an IPv6 static route",
                           commandHandler<CmdConfigProtocolStaticIpv6RouteAdd>,
                           argRegistrar<
                               CmdConfigProtocolStaticIpv6RouteAddTraits>,
                       }},
                   }},
              },
          },
      },

      {
          "config",
          "ptp",
          "Configure PTP settings",
          commandHandler<CmdConfigPtp>,
          argRegistrar<CmdConfigPtpTraits>,
          {{
              "transparent-clock",
              "Enable or disable PTP transparent clock mode",
              commandHandler<CmdConfigPtpTransparentClock>,
              argRegistrar<CmdConfigPtpTransparentClockTraits>,
          }},
      },

      {
          "config",
          "qos",
          "Configure QoS settings",
          commandHandler<CmdConfigQos>,
          argRegistrar<CmdConfigQosTraits>,
          {{
               "buffer-pool",
               "Configure buffer pool settings",
               commandHandler<CmdConfigQosBufferPool>,
               argRegistrar<CmdConfigQosBufferPoolTraits>,
           },
           {
               "default-policy",
               "Set the default data-plane QoS policy for ports not in portIdToQosPolicy",
               commandHandler<CmdConfigQosDefaultPolicy>,
               argRegistrar<CmdConfigQosDefaultPolicyTraits>,
           },
           {
               "policy",
               "Configure QoS policy settings",
               commandHandler<CmdConfigQosPolicy>,
               argRegistrar<CmdConfigQosPolicyTraits>,
               {{
                   "map",
                   "Set QoS map entry (tc-to-queue, pfc-pri-to-queue, tc-to-pg, pfc-pri-to-pg)",
                   commandHandler<CmdConfigQosPolicyMap>,
                   argRegistrar<CmdConfigQosPolicyMapTraits>,
               }},
           },
           {
               "priority-group-policy",
               "Configure priority group policy settings",
               commandHandler<CmdConfigQosPriorityGroupPolicy>,
               argRegistrar<CmdConfigQosPriorityGroupPolicyTraits>,
               {{"group-id",
                 "Specify priority group ID (0-7)",
                 commandHandler<CmdConfigQosPriorityGroupPolicyGroupId>,
                 argRegistrar<CmdConfigQosPriorityGroupPolicyGroupIdTraits>}},
           },
           {
               "queue-config",
               "Configure port queue settings for a named queue config, or for 'default' (the switch-wide default queues)",
               commandHandler<CmdConfigQosQueueConfig>,
               argRegistrar<CmdConfigQosQueueConfigTraits>,
               {{
                   "queue-id",
                   "Specify queue ID and attributes",
                   commandHandler<CmdConfigQosQueueConfigQueueId>,
                   argRegistrar<CmdConfigQosQueueConfigQueueIdTraits>,
               }},
           }},
      },

      {
          "config",
          "session",
          "Manage config session",
          {{
               "clear",
               "Clear the current config session",
               commandHandler<CmdConfigSessionClear>,
               argRegistrar<CmdConfigSessionClearTraits>,
           },
           {
               "commit",
               "Commit the current config session",
               commandHandler<CmdConfigSessionCommit>,
               argRegistrar<CmdConfigSessionCommitTraits>,
           },
           {
               "diff",
               "Show diff between configs (session vs live, session vs revision, or revision vs revision)",
               commandHandler<CmdConfigSessionDiff>,
               argRegistrar<CmdConfigSessionDiffTraits>,
           },
           {
               "rebase",
               "Rebase session changes onto current HEAD",
               commandHandler<CmdConfigSessionRebase>,
               argRegistrar<CmdConfigSessionRebaseTraits>,
           }},
      },

      {"config",
       "reload",
       "Reload agent configuration",
       commandHandler<CmdConfigReload>,
       argRegistrar<CmdConfigReloadTraits>},

      {"config",
       "rollback",
       "Rollback to a previous config revision",
       commandHandler<CmdConfigRollback>,
       argRegistrar<CmdConfigRollbackTraits>},

      {
          "config",
          "srv6",
          "Configure SRv6 MySID settings",
          commandHandler<CmdConfigSrv6>,
          argRegistrar<CmdConfigSrv6Traits>,
          {{
              "my-sid",
              "Initialize or manage MySID entries under a locator prefix",
              commandHandler<CmdConfigSrv6MySid>,
              argRegistrar<CmdConfigSrv6MySidTraits>,
              {{
                  "entry",
                  "Configure uA/uN/uDT46 entry: <fn> type ...",
                  commandHandler<CmdConfigSrv6MySidEntry>,
                  argRegistrar<CmdConfigSrv6MySidEntryTraits>,
              }},
          }},
      },

      {"config",
       "tunnel",
       "Configure tunnel settings",
       commandHandler<CmdConfigTunnel>,
       argTypeHandler<CmdConfigTunnelTraits>,
       {{
           "ip-in-ip",
           "Configure IP-in-IP tunnel (use 'encap' or 'decap')",
           commandHandler<CmdConfigTunnelIpInIp>,
           argTypeHandler<CmdConfigTunnelIpInIpTraits>,
           {{
                "encap",
                "Configure IP-in-IP encap tunnel",
                commandHandler<CmdConfigTunnelIpInIpEncap>,
                argRegistrar<CmdConfigTunnelIpInIpEncapTraits>,
            },
            {
                "decap",
                "Configure IP-in-IP decap tunnel",
                commandHandler<CmdConfigTunnelIpInIpDecap>,
                argRegistrar<CmdConfigTunnelIpInIpDecapTraits>,
            }},
       }}},

      {"config",
       "traffic-counter",
       "Create or update a named traffic counter (PACKETS,BYTES)",
       commandHandler<CmdConfigTrafficCounter>,
       argRegistrar<CmdConfigTrafficCounterTraits>},

      {
          "config",
          "vlan",
          "Configure VLAN settings",
          commandHandler<CmdConfigVlan>,
          argRegistrar<CmdConfigVlanTraits>,
          {{
               "port",
               "Configure VLAN port settings",
               commandHandler<CmdConfigVlanPort>,
               argRegistrar<CmdConfigVlanPortTraits>,
               {{
                   "taggingMode",
                   "Set VLAN port tagging mode (tagged, untagged, priority-tagged)",
                   commandHandler<CmdConfigVlanPortTaggingMode>,
                   argRegistrar<CmdConfigVlanPortTaggingModeTraits>,
               }},
           },
           {
               "static-mac",
               "Manage static MAC entries for VLANs",
               commandHandler<CmdConfigVlanStaticMac>,
               argRegistrar<CmdConfigVlanStaticMacTraits>,
               {{
                    "add",
                    "Add a static MAC entry to a VLAN",
                    commandHandler<CmdConfigVlanStaticMacAdd>,
                    argRegistrar<CmdConfigVlanStaticMacAddTraits>,
                },
                {
                    "delete",
                    "Delete a static MAC entry from a VLAN",
                    commandHandler<CmdConfigVlanStaticMacDelete>,
                    argRegistrar<CmdConfigVlanStaticMacDeleteTraits>,
                }},
           }},
      },

      // Registered separately (merged into the existing "config vlan" by
      // CmdSubcommands::addCommandBranch) so "default" lands at depth 0 and
      // owns data_[0] for its own VLAN ID arg, independent of the
      // "config vlan <id>" context (whose arg also lives at data_[0] but is
      // not populated when the "default" subcommand is matched).
      {
          "config",
          "vlan",
          "Configure VLAN settings",
          {{
              "default",
              "Set global default VLAN ID for untagged traffic",
              commandHandler<CmdConfigVlanDefault>,
              argRegistrar<CmdConfigVlanDefaultTraits>,
          }},
      },

      {"delete",
       "arp",
       "Reset ARP/NDP timer settings to their defaults",
       commandHandler<CmdDeleteArp>,
       argRegistrar<CmdDeleteArpTraits>},

      {
          "delete",
          "interface",
          "Delete an interface/port, or reset interface settings (e.g. ipv6 ndp, ip-address)",
          commandHandler<CmdDeleteInterface>,
          argRegistrar<CmdDeleteInterfaceTraits>,
          {{
               "ipv6",
               "Delete (reset to default) IPv6 settings for interface",
               commandHandler<CmdDeleteInterfaceIpv6>,
               argTypeHandler<CmdDeleteInterfaceIpv6Traits>,
               {{
                   "ndp",
                   "Reset IPv6 Neighbor Discovery (NDP/RA) settings to defaults",
                   commandHandler<CmdDeleteInterfaceIpv6Ndp>,
                   argRegistrar<CmdDeleteInterfaceIpv6NdpTraits>,
               }},
           },
           {
               "sflow",
               "Delete (reset to default) sFlow settings for interface: "
               "sample-dest, ingress-rate, egress-rate",
               commandHandler<CmdDeleteInterfaceSflow>,
               argRegistrar<CmdDeleteInterfaceSflowTraits>,
           }},
      },

      {
          "delete",
          "qos",
          "Delete (reset to default) QoS settings",
          commandHandler<CmdDeleteQos>,
          argRegistrar<CmdDeleteQosTraits>,
          {{
               "default-policy",
               "Clear the default data-plane QoS policy",
               commandHandler<CmdDeleteQosDefaultPolicy>,
               argRegistrar<CmdDeleteQosDefaultPolicyTraits>,
           },
           {
               "queue-config",
               "Remove a queue config, or 'default' to clear the switch-wide default queues",
               commandHandler<CmdDeleteQosQueueConfig>,
               argRegistrar<CmdDeleteQosQueueConfigTraits>,
               {{
                   "queue-id",
                   "Remove only the given queue from the queue config",
                   commandHandler<CmdDeleteQosQueueConfigQueueId>,
                   argRegistrar<CmdDeleteQosQueueConfigQueueIdTraits>,
               }},
           },
           {
               "policy",
               "Delete a QoS policy or one of its map entries",
               commandHandler<CmdDeleteQosPolicy>,
               argRegistrar<CmdDeleteQosPolicyTraits>,
               {{
                   "map",
                   "Remove a QoS map entry (dscp, tc-to-queue)",
                   commandHandler<CmdDeleteQosPolicyMap>,
                   argRegistrar<CmdDeleteQosPolicyMapTraits>,
               }},
           }},
      },

      {
          "delete",
          "protocol",
          "Delete protocol objects",
          commandHandler<CmdDeleteProtocol>,
          argTypeHandler<CmdDeleteProtocolTraits>,
          {{
               "bgp",
               "Delete BGP configuration objects",
               commandHandler<CmdDeleteProtocolBgp>,
               argTypeHandler<CmdDeleteProtocolBgpTraits>,
               {{
                    "neighbor",
                    "Delete a BGP neighbor: <ip-address>",
                    commandHandler<CmdDeleteProtocolBgpNeighbor>,
                    argRegistrar<CmdDeleteProtocolBgpNeighborTraits>,
                },
                {
                    "peer-group",
                    "Delete a BGP peer-group: <name>",
                    commandHandler<CmdDeleteProtocolBgpPeerGroup>,
                    argRegistrar<CmdDeleteProtocolBgpPeerGroupTraits>,
                },
                {
                    "policy",
                    "Delete BGP policy objects",
                    commandHandler<CmdDeleteProtocolBgpPolicy>,
                    argTypeHandler<CmdDeleteProtocolBgpPolicyTraits>,
                    {{
                         "as-path-list",
                         "Delete a BGP AS-path list, or one of its regexes: "
                         "<name> [regex <regex>]",
                         commandHandler<CmdDeleteProtocolBgpPolicyAsPathList>,
                         argRegistrar<
                             CmdDeleteProtocolBgpPolicyAsPathListTraits>,
                     },
                     {
                         "community-list",
                         "Delete a BGP community-list: <name>",
                         commandHandler<
                             CmdDeleteProtocolBgpPolicyCommunityList>,
                         argRegistrar<
                             CmdDeleteProtocolBgpPolicyCommunityListTraits>,
                     },
                     {
                         "prefix-list",
                         "Delete a BGP prefix-list, or one of its "
                         "entries: <name> [prefix <prefix/len>]",
                         commandHandler<CmdDeleteProtocolBgpPolicyPrefixList>,
                         argRegistrar<
                             CmdDeleteProtocolBgpPolicyPrefixListTraits>,
                     },
                     {
                         "routing-policy",
                         "Delete a BGP routing-policy: <name>",
                         commandHandler<
                             CmdDeleteProtocolBgpPolicyRoutingPolicy>,
                         argRegistrar<
                             CmdDeleteProtocolBgpPolicyRoutingPolicyTraits>,
                         {{
                             "term",
                             "Delete a routing-policy term: <seq-num>",
                             commandHandler<
                                 CmdDeleteProtocolBgpPolicyRoutingPolicyTerm>,
                             argRegistrar<
                                 CmdDeleteProtocolBgpPolicyRoutingPolicyTermTraits>,
                         }},
                     }},
                }},
           },
           {
               "static",
               "Delete static routing configuration",
               commandHandler<CmdDeleteProtocolStatic>,
               argTypeHandler<CmdDeleteProtocolStaticTraits>,
               {{
                    "ip",
                    "Delete IPv4 static routes",
                    {{
                        "route",
                        "Delete IPv4 static route",
                        commandHandler<CmdDeleteProtocolStaticIpRoute>,
                        argRegistrar<CmdDeleteProtocolStaticIpRouteTraits>,
                    }},
                },
                {
                    "ipv6",
                    "Delete IPv6 static routes",
                    {{
                        "route",
                        "Delete IPv6 static route",
                        commandHandler<CmdDeleteProtocolStaticIpv6Route>,
                        argRegistrar<CmdDeleteProtocolStaticIpv6RouteTraits>,
                    }},
                }},
           }},
      },

      {
          "delete",
          "acl",
          "Delete (clear) ACL settings",
          commandHandler<CmdDeleteAcl>,
          argRegistrar<CmdDeleteAclTraits>,
          {{
               "rule",
               "Delete an ACL rule (and any associated MatchAction) from an AclTable",
               commandHandler<CmdDeleteAclRule>,
               argRegistrar<CmdDeleteAclRuleTraits>,
           },
           {
               "table",
               "Delete an ACL table, its rules, and their traffic-policy actions",
               commandHandler<CmdDeleteAclTable>,
               argRegistrar<CmdDeleteAclTableTraits>,
           }},
      },

      {"delete",
       "config",
       "Delete config objects",
       commandHandler<CmdDeleteConfig>,
       argRegistrar<CmdDeleteConfigTraits>},

      {
          "delete",
          "copp",
          "Delete COPP (Control Plane Policing) configuration",
          commandHandler<CmdDeleteCopp>,
          argRegistrar<CmdDeleteCoppTraits>,
          {{
               "queue",
               "Delete a CPU queue entry",
               commandHandler<CmdDeleteCoppQueue>,
               argRegistrar<CmdDeleteCoppQueueTraits>,
           },
           {
               "traffic-policy",
               "Delete CPU traffic policy configuration",
               commandHandler<CmdDeleteCoppTrafficPolicy>,
               argRegistrar<CmdDeleteCoppTrafficPolicyTraits>,
               {{
                   "match",
                   "Target a named ACL matcher entry in cpuTrafficPolicy.trafficPolicy.matchToAction",
                   commandHandler<CmdDeleteCoppTrafficPolicyMatch>,
                   argRegistrar<CmdDeleteCoppTrafficPolicyMatchTraits>,
                   {{
                       "action",
                       "Delete a CPU-plane action (send-to-queue, counter, set-tc, user-defined-trap) from the matcher",
                       commandHandler<CmdDeleteCoppTrafficPolicyMatchAction>,
                       argRegistrar<
                           CmdDeleteCoppTrafficPolicyMatchActionTraits>,
                   }},
               }},
           },
           {
               "reason",
               "Delete a packet-rx reason to CPU queue mapping",
               commandHandler<CmdDeleteCoppReason>,
               argRegistrar<CmdDeleteCoppReasonTraits>,
           }},
      },

      {
          "delete",
          "data-plane",
          "Delete dataplane configuration",
          commandHandler<CmdDeleteDataPlane>,
          argRegistrar<CmdDeleteDataPlaneTraits>,
          {{
              "traffic-policy",
              "Delete dataplane traffic policy configuration",
              commandHandler<CmdDeleteDataPlaneTrafficPolicy>,
              argRegistrar<CmdDeleteDataPlaneTrafficPolicyTraits>,
              {{
                  "match",
                  "Target a named ACL matcher entry in dataPlaneTrafficPolicy.matchToAction",
                  commandHandler<CmdDeleteDataPlaneTrafficPolicyMatch>,
                  argRegistrar<CmdDeleteDataPlaneTrafficPolicyMatchTraits>,
                  {{
                      "action",
                      "Delete a dataplane action (send-to-queue, set-dscp, set-tc, mirror-ingress, mirror-egress, counter, trap-to-cpu, copy-to-cpu, redirect, user-defined-trap, flowlet, ecmp-hash) from the matcher",
                      commandHandler<
                          CmdDeleteDataPlaneTrafficPolicyMatchAction>,
                      argRegistrar<
                          CmdDeleteDataPlaneTrafficPolicyMatchActionTraits>,
                  }},
              }},
          }},
      },

      {
          "delete",
          "dhcp",
          "Remove DHCP source-override settings",
          commandHandler<CmdDeleteDhcp>,
          argRegistrar<CmdDeleteDhcpTraits>,
          {{
               "relay-source-override",
               "Remove source IP override for DHCP relay packets (ipv4|ipv6)",
               commandHandler<CmdDeleteDhcpRelaySourceOverride>,
               argRegistrar<CmdDeleteDhcpRelaySourceOverrideTraits>,
           },
           {
               "reply-source-override",
               "Remove source IP override for DHCP reply packets (ipv4|ipv6)",
               commandHandler<CmdDeleteDhcpReplySourceOverride>,
               argRegistrar<CmdDeleteDhcpReplySourceOverrideTraits>,
           }},
      },

      {
          "delete",
          "load-balancing",
          "Delete load-balancing (ECMP/LAG) configuration",
          commandHandler<CmdDeleteLoadBalancing>,
          argRegistrar<CmdDeleteLoadBalancingTraits>,
          {{
               "ecmp",
               "Delete the ECMP load-balancer configuration",
               commandHandler<CmdDeleteLoadBalancingEcmp>,
               argRegistrar<CmdDeleteLoadBalancingEcmpTraits>,
           },
           {
               "lag",
               "Delete the LAG load-balancer configuration",
               commandHandler<CmdDeleteLoadBalancingLag>,
               argRegistrar<CmdDeleteLoadBalancingLagTraits>,
           }},
      },

      {
          "delete",
          "srv6",
          "Delete SRv6 MySID configuration",
          commandHandler<CmdDeleteSrv6>,
          argRegistrar<CmdDeleteSrv6Traits>,
          {{
              "my-sid",
              "Remove an entire MySID config or one entry",
              commandHandler<CmdDeleteSrv6MySid>,
              argRegistrar<CmdDeleteSrv6MySidTraits>,
              {{
                  "entry",
                  "Remove one MySID function entry",
                  commandHandler<CmdDeleteSrv6MySidEntry>,
                  argRegistrar<CmdDeleteSrv6MySidEntryTraits>,
              }},
          }},
      },

      {"delete",
       "tunnel",
       "Delete (reset to default) tunnel settings",
       commandHandler<CmdDeleteTunnel>,
       argTypeHandler<CmdDeleteTunnelTraits>,
       {{
           "ip-in-ip",
           "Delete IP-in-IP tunnel or reset its attributes (use 'encap' or "
           "'decap')",
           commandHandler<CmdDeleteTunnelIpInIp>,
           argTypeHandler<CmdDeleteTunnelIpInIpTraits>,
           {{
                "encap",
                "Delete IP-in-IP encap tunnel or reset its attributes",
                commandHandler<CmdDeleteTunnelIpInIpEncap>,
                argRegistrar<CmdDeleteTunnelIpInIpEncapTraits>,
            },
            {
                "decap",
                "Delete IP-in-IP decap tunnel or reset its attributes",
                commandHandler<CmdDeleteTunnelIpInIpDecap>,
                argRegistrar<CmdDeleteTunnelIpInIpDecapTraits>,
            }},
       }}},

      {"delete",
       "traffic-counter",
       "Delete a traffic counter (refuses while a traffic-policy match action "
       "references it): <name>",
       commandHandler<CmdDeleteTrafficCounter>,
       argRegistrar<CmdDeleteTrafficCounterTraits>},

      {"delete",
       "vlan",
       "Delete a VLAN and its interface (refuses while it is the default VLAN or a port's ingress VLAN)",
       commandHandler<CmdDeleteVlan>,
       argRegistrar<CmdDeleteVlanTraits>},

      {"delete",
       "switch",
       "Delete switch-level settings",
       commandHandler<CmdDeleteSwitch>,
       argTypeHandler<CmdDeleteSwitchTraits>,
       {{"admin-distance",
         "Remove the admin distance entry for a routing client: <client-id>",
         commandHandler<CmdDeleteAdminDistance>,
         argRegistrar<CmdDeleteAdminDistanceTraits>},
        {"icmpv4-unavailable-src-addr",
         "Remove the ICMPv4 unavailable source address (agent falls back to the RFC 7600 default)",
         commandHandler<CmdDeleteIcmpV4UnavailableSrcAddr>,
         argRegistrar<CmdDeleteIcmpV4UnavailableSrcAddrTraits>}}},
  };
  stable_sort(root.begin(), root.end());
  return root;
}

} // namespace facebook::fboss
