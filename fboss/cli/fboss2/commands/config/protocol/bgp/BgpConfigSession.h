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

#include <folly/json/dynamic.h>
#include <cstdint>
#include <optional>
#include <string>

namespace facebook::fboss {

/**
 * BgpConfigSession manages BGP configuration editing sessions for the fboss2
 * CLI.
 *
 * OVERVIEW:
 * BgpConfigSession provides a session-based workflow for editing BGP
 * configuration. It maintains a JSON file that stores the BGP configuration
 * in BGP++ thrift-compatible format (SimpleJSON).
 *
 * The output JSON can be directly used by BGP++ daemon via:
 *   apache::thrift::SimpleJSONSerializer().deserialize(json, bgpConfig);
 *
 * STORAGE:
 * - Session file: ~/.fboss2/bgp_config.json (per-user, temporary edits)
 * - The configuration is stored as JSON matching BgpConfig thrift schema
 *
 * CONFIGURATION HIERARCHY (from BGP++ research):
 * - Global Level (G): Device-wide settings in BgpGlobalConfig
 * - Peer Group Level (PG): Shared settings in BgpCommonPeerGroupConfig
 * - Peer Level (P): Individual peer settings (can override PG)
 *
 * BGP++ THRIFT FIELD NAMES (from bgp_config.thrift):
 *   - router_id: string - Router identifier (loopback IP)
 *   - local_as_4_byte: i64 - Local AS number (RFC 6793)
 *   - hold_time: i32 - Hold time in seconds (default 30)
 *   - local_confed_as_4_byte: i64 - Confederation AS number
 *   - peers: list<BgpPeer> - List of BGP peers
 *   - peer_groups: list<PeerGroup> - List of peer groups
 *
 * USAGE:
 *   1. Get the singleton: auto& session = BgpConfigSession::getInstance();
 *   2. Modify config: session.setRouterId("10.0.0.1");
 *   3. Save changes: session.saveConfig();
 *   4. Output is directly BGP++ compatible JSON
 *
 * STUB MODE:
 * When FBOSS_BGP_STUB_MODE=1, commands print config without writing files.
 */
class BgpConfigSession {
 public:
  BgpConfigSession();
  ~BgpConfigSession() = default;

  // Get or create the current BGP config session
  static BgpConfigSession& getInstance();

  // Get the path to the session config file (~/.fboss2/bgp_config.json)
  std::string getSessionConfigPath() const;

  // Check if a session exists
  bool sessionExists() const;

  // Get the parsed BGP configuration as a mutable reference
  // This is the raw BgpConfig thrift-compatible JSON
  folly::dynamic& getBgpConfig();
  const folly::dynamic& getBgpConfig() const;

  // Save the configuration back to the session file
  void saveConfig();

  // Clear the session (delete the session file)
  void clearSession();

  // Enable/disable stub mode (for testing without actual file writes)
  void setStubMode(bool enabled);
  bool isStubMode() const;

  // ==================== Global Configuration (Level: G) ====================
  // These map directly to BgpConfig thrift fields

  // router_id: string - Router identifier (normally loopback IP)
  void setRouterId(const std::string& routerId);
  std::optional<std::string> getRouterId() const;

  // local_as_4_byte: i64 - Local ASN (RFC 6793 4-byte ASN)
  void setLocalAsn(uint64_t asn);
  std::optional<uint64_t> getLocalAsn() const;

  // hold_time: i32 - Hold time for all BGP sessions (default 30)
  void setHoldTime(int32_t seconds);
  std::optional<int32_t> getHoldTime() const;

  // local_confed_as_4_byte: i64 - Confederation ASN
  void setConfedAsn(uint64_t asn);
  std::optional<uint64_t> getConfedAsn() const;

  // listen_addr: string - Listen address for BGP sessions
  void setListenAddress(const std::string& listenAddr);
  std::optional<std::string> getListenAddress() const;

  // listen_port: i32 - Listen port (default 179)
  void setListenPort(int32_t port);
  std::optional<int32_t> getListenPort() const;

  // dynamic_peer_limit: i32 - Maximum dynamic peers
  void setDynamicPeerLimit(int32_t limit);
  std::optional<int32_t> getDynamicPeerLimit() const;

  // count_confeds_in_as_path_len: bool - Count confeds in AS path length
  void setCountConfedsInAsPathLen(bool count);
  std::optional<bool> getCountConfedsInAsPathLen() const;

  // allow_loopback_reflection: bool - Allow loopback reflection
  void setLoopbackReflection(bool allow);
  std::optional<bool> getLoopbackReflection() const;

  // enable_rib_allocated_path_id: bool - Use RIB-allocated path IDs
  void setRibAllocatedPathIds(bool enable);
  std::optional<bool> getRibAllocatedPathIds() const;

  // enable_dynamic_policy_evaluation: bool - Dynamic policy evaluation
  void setDynamicPolicyEval(bool enable);
  std::optional<bool> getDynamicPolicyEval() const;

  // enable_update_group: bool - Enable BGP update groups
  void setEnableUpdateGroups(bool enable);
  std::optional<bool> getEnableUpdateGroups() const;

  // enable_serialize_group_pdu: bool - Serialize group PDU generation
  void setSerializeGroupPdus(bool enable);
  std::optional<bool> getSerializeGroupPdus() const;

  // ==================== Global Graceful Restart ====================
  // support_stateful_gr: bool - Enable stateful GR
  void setGracefulRestartStateful(bool enable);
  std::optional<bool> getGracefulRestartStateful() const;

  // graceful_restart_seconds: i32 - GR restart time
  void setGracefulRestartTime(int32_t seconds);
  std::optional<int32_t> getGracefulRestartTime() const;

  // enable_optimized_gr: bool - Use optimized GR logic
  void setGracefulRestartOptimized(bool enable);
  std::optional<bool> getGracefulRestartOptimized() const;

  // ==================== Global Best Path Selection ====================
  // enable_weight_comparison: bool
  void setBestPathEnableWeight(bool enable);
  std::optional<bool> getBestPathEnableWeight() const;

  // enable_med_comparison: bool
  void setBestPathEnableMed(bool enable);
  std::optional<bool> getBestPathEnableMed() const;

  // enable_med_missing_as_worst: bool
  void setBestPathMedMissingAsWorst(bool enable);
  std::optional<bool> getBestPathMedMissingAsWorst() const;

  // enable_next_hop_tracking: bool
  void setNextHopTracking(bool enable);
  std::optional<bool> getNextHopTracking() const;

  // ==================== Global Link Bandwidth / UCMP ====================
  // compute_ucmp_from_link_bandwidth_community: bool
  void setUseLbwCommunity(bool enable);
  std::optional<bool> getUseLbwCommunity() const;

  // ucmp_width: i32
  void setUcmpWidth(int32_t width);
  std::optional<int32_t> getUcmpWidth() const;

  // ==================== Peer Group Configuration (Level: PG)
  // ==================== These map to PeerGroup thrift struct fields

  // Create peer group by name
  void createPeerGroup(const std::string& groupName);

  // remote_as_4_byte: i64 - Default remote ASN for group
  void setPeerGroupRemoteAsn(const std::string& groupName, uint64_t remoteAsn);

  // local_as: i64 - Local ASN override for this peer group
  void setPeerGroupLocalAsn(const std::string& groupName, uint64_t localAsn);

  // description: string - Group description
  void setPeerGroupDescription(
      const std::string& groupName,
      const std::string& description);

  // local_addr: string - Default local address
  void setPeerGroupLocalAddr(
      const std::string& groupName,
      const std::string& localAddr);

  // next_hop4: string - Default IPv4 next-hop
  void setPeerGroupNextHop4(
      const std::string& groupName,
      const std::string& nextHop4);

  // next_hop6: string - Default IPv6 next-hop
  void setPeerGroupNextHop6(
      const std::string& groupName,
      const std::string& nextHop6);

  // is_passive: bool - Passive mode for group
  void setPeerGroupPassive(const std::string& groupName, bool isPassive);

  // is_rr_client: bool - Route reflector client
  void setPeerGroupRrClient(const std::string& groupName, bool isRrClient);

  // next_hop_self: bool - Set next-hop to self
  void setPeerGroupNextHopSelf(const std::string& groupName, bool nextHopSelf);

  // is_confed_peer: bool - Confederation peer
  void setPeerGroupConfedPeer(const std::string& groupName, bool isConfedPeer);

  // peer_port: i32 - Remote peer port
  void setPeerGroupPort(const std::string& groupName, int32_t port);

  // local_port: i32 - Local source port
  void setPeerGroupLocalPort(const std::string& groupName, int32_t port);

  // connect_mode: string - PASSIVE_ACTIVE, PASSIVE_ONLY, ACTIVE_ONLY
  void setPeerGroupConnectMode(
      const std::string& groupName,
      const std::string& connectMode);

  // bgp_peer_timers.hold_time_seconds: i32 - Hold time
  void setPeerGroupHoldTime(const std::string& groupName, int32_t holdTime);

  // bgp_peer_timers.keep_alive_seconds: i32 - Keepalive interval
  void setPeerGroupKeepalive(const std::string& groupName, int32_t keepalive);

  // bgp_peer_timers.out_delay_seconds: i32 - Output delay
  void setPeerGroupOutDelay(const std::string& groupName, int32_t outDelay);

  // Peer group graceful restart
  void setPeerGroupGracefulRestartTime(
      const std::string& groupName,
      int32_t seconds);
  void setPeerGroupStatefulHa(const std::string& groupName, bool enable);

  // Peer group AFI/SAFI
  void setPeerGroupDisableIpv4Afi(const std::string& groupName, bool disable);
  void setPeerGroupDisableIpv6Afi(const std::string& groupName, bool disable);
  void setPeerGroupIpv4LabeledUnicast(
      const std::string& groupName,
      bool enable);
  void setPeerGroupIpv6LabeledUnicast(
      const std::string& groupName,
      bool enable);
  void setPeerGroupIpv4OverIpv6Nh(const std::string& groupName, bool enable);

  // Peer group add-path
  void setPeerGroupAddPathSend(const std::string& groupName, bool enable);
  void setPeerGroupAddPathReceive(const std::string& groupName, bool enable);

  // Peer group AS-path settings
  void setPeerGroupRemovePrivateAs(const std::string& groupName, bool remove);
  void setPeerGroupEnforceFirstAs(const std::string& groupName, bool enforce);

  // ingress_policy_name: string - Ingress policy name
  void setPeerGroupIngressPolicy(
      const std::string& groupName,
      const std::string& policy);

  // egress_policy_name: string - Egress policy name
  void setPeerGroupEgressPolicy(
      const std::string& groupName,
      const std::string& policy);

  // Peer group route limits
  void setPeerGroupPreFilterMaxRoutes(
      const std::string& groupName,
      int64_t maxRoutes);
  void setPeerGroupPostFilterMaxRoutes(
      const std::string& groupName,
      int64_t maxRoutes);

  // Peer group link bandwidth
  void setPeerGroupAdvertiseLbw(const std::string& groupName, bool advertise);
  void setPeerGroupReceiveLbw(const std::string& groupName, bool receive);
  void setPeerGroupLbwValue(const std::string& groupName, int64_t bps);

  // Peer group identification (for RSW config compatibility)
  void setPeerGroupPeerTag(
      const std::string& groupName,
      const std::string& tag);

  // Peer group pre_filter warning settings
  void setPeerGroupPreFilterWarningLimit(
      const std::string& groupName,
      int64_t limit);
  void setPeerGroupPreFilterWarningOnly(
      const std::string& groupName,
      bool warningOnly);

  // Peer group timer: withdraw_unprog_delay_seconds
  void setPeerGroupWithdrawUnprogDelay(
      const std::string& groupName,
      int32_t seconds);

  // ==================== Networks Configuration ====================

  // Add a network6 entry (IPv6 prefix to advertise)
  void addNetwork6(
      const std::string& prefix,
      const std::string& policyName,
      bool installToFib,
      int32_t minSupportingRoutes);

  // ==================== Switch Limit Configuration ====================

  // prefix_limit: i32 - Maximum number of prefixes
  void setSwitchLimitPrefixLimit(int64_t limit);

  // total_path_limit: i32 - Maximum total paths
  void setSwitchLimitTotalPathLimit(int64_t limit);

  // max_golden_vips: i32 - Maximum golden VIPs
  void setSwitchLimitMaxGoldenVips(int64_t limit);

  // overload_protection_mode: i32 - Overload protection mode
  void setSwitchLimitOverloadProtectionMode(int32_t mode);

  // ==================== Export Functions ====================

  // Export configuration as pretty JSON string (BGP++ thrift format)
  std::string exportConfig() const;

 private:
  std::string sessionConfigPath_;
  folly::dynamic bgpConfig_;
  bool configLoaded_ = false;
  bool stubMode_ = false;

  void loadConfig();
  void ensureConfigLoaded();

  // Helper to find or create a peer group in the peer_groups list
  folly::dynamic& findOrCreatePeerGroup(const std::string& groupName);

  // Helper to ensure peer group timers exist
  folly::dynamic& ensurePeerGroupTimers(const std::string& groupName);

  // Helper to ensure add_path config exists
  folly::dynamic& ensurePeerGroupAddPath(const std::string& groupName);

  // Helper to ensure pre_filter/post_filter config exists
  folly::dynamic& ensurePeerGroupPreFilter(const std::string& groupName);
  folly::dynamic& ensurePeerGroupPostFilter(const std::string& groupName);
};

} // namespace facebook::fboss
