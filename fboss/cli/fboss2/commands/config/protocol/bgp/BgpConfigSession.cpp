/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/protocol/bgp/BgpConfigSession.h"

#include <folly/FileUtil.h>
#include <folly/json/json.h>
#include <folly/logging/xlog.h>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include "folly/json/dynamic.h"

namespace fs = std::filesystem;

namespace facebook::fboss {

namespace {
// Default BGP config structure matching BgpConfig thrift schema
// This can be directly deserialized by BGP++ using SimpleJSONSerializer
folly::dynamic getDefaultBgpConfig() {
  return folly::dynamic::object
      // Global settings (BgpConfig fields)
      ("router_id", "")("hold_time", 30)("listen_port", 179)(
          "listen_addr", "::")
      // peers: list<BgpPeer>
      ("peers", folly::dynamic::array())
      // peer_groups: list<PeerGroup>
      ("peer_groups", folly::dynamic::array())
      // communities: list<BgpCommunity>
      ("communities", folly::dynamic::array())
      // networks4/networks6: list<BgpNetwork>
      ("networks4", folly::dynamic::array())(
          "networks6", folly::dynamic::array());
}
} // namespace

BgpConfigSession::BgpConfigSession() {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded CLI
  const char* homeDir = std::getenv("HOME");
  if (homeDir == nullptr) {
    throw std::runtime_error("HOME environment variable not set");
  }

  fs::path configDir = fs::path(homeDir) / ".fboss2";
  if (!fs::exists(configDir)) {
    fs::create_directories(configDir);
  }

  sessionConfigPath_ = (configDir / "bgp_config.json").string();
  bgpConfig_ = getDefaultBgpConfig();

  // Check for stub mode via environment variable
  // NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded CLI
  const char* stubEnv = std::getenv("FBOSS_BGP_STUB_MODE");
  if (stubEnv != nullptr && std::string(stubEnv) == "1") {
    stubMode_ = true;
    XLOG(INFO)
        << "BGP Config Session running in STUB MODE (FBOSS_BGP_STUB_MODE=1)";
  }
}

BgpConfigSession& BgpConfigSession::getInstance() {
  static BgpConfigSession instance;
  return instance;
}

std::string BgpConfigSession::getSessionConfigPath() const {
  return sessionConfigPath_;
}

bool BgpConfigSession::sessionExists() const {
  return fs::exists(sessionConfigPath_);
}

void BgpConfigSession::loadConfig() {
  if (configLoaded_) {
    return;
  }

  if (sessionExists()) {
    std::string content;
    if (folly::readFile(sessionConfigPath_.c_str(), content)) {
      try {
        bgpConfig_ = folly::parseJson(content);
      } catch (const std::exception& ex) {
        XLOG(WARN) << "Failed to parse BGP config: " << ex.what()
                   << ", using default config";
        bgpConfig_ = getDefaultBgpConfig();
      }
    }
  } else {
    bgpConfig_ = getDefaultBgpConfig();
  }

  configLoaded_ = true;
}

void BgpConfigSession::ensureConfigLoaded() {
  if (!configLoaded_) {
    loadConfig();
  }
}

folly::dynamic& BgpConfigSession::getBgpConfig() {
  ensureConfigLoaded();
  return bgpConfig_;
}

const folly::dynamic& BgpConfigSession::getBgpConfig() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  return bgpConfig_;
}

void BgpConfigSession::saveConfig() {
  if (stubMode_) {
    std::cout << "[STUB MODE] Would save config to: " << sessionConfigPath_
              << std::endl;
    std::cout << "[STUB MODE] Config content:\n" << exportConfig() << std::endl;
    return;
  }

  std::string prettyJson = folly::toPrettyJson(bgpConfig_);
  folly::writeFileAtomic(sessionConfigPath_, prettyJson, 0644);
  XLOG(INFO) << "Saved BGP config to: " << sessionConfigPath_;
}

void BgpConfigSession::clearSession() {
  if (stubMode_) {
    std::cout << "[STUB MODE] Would clear session file: " << sessionConfigPath_
              << std::endl;
    bgpConfig_ = getDefaultBgpConfig();
    configLoaded_ = false;
    return;
  }

  if (fs::exists(sessionConfigPath_)) {
    fs::remove(sessionConfigPath_);
  }
  bgpConfig_ = getDefaultBgpConfig();
  configLoaded_ = false;
}

void BgpConfigSession::setStubMode(bool enabled) {
  stubMode_ = enabled;
}

bool BgpConfigSession::isStubMode() const {
  return stubMode_;
}

// ==================== Global Configuration ====================

void BgpConfigSession::setRouterId(const std::string& routerId) {
  ensureConfigLoaded();
  bgpConfig_["router_id"] = routerId;
}

std::optional<std::string> BgpConfigSession::getRouterId() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("router_id") &&
      !bgpConfig_["router_id"].asString().empty()) {
    return bgpConfig_["router_id"].asString();
  }
  return std::nullopt;
}

void BgpConfigSession::setLocalAsn(uint64_t asn) {
  ensureConfigLoaded();
  bgpConfig_["local_as_4_byte"] = static_cast<int64_t>(asn);
}

std::optional<uint64_t> BgpConfigSession::getLocalAsn() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("local_as_4_byte")) {
    return static_cast<uint64_t>(bgpConfig_["local_as_4_byte"].asInt());
  }
  return std::nullopt;
}

void BgpConfigSession::setHoldTime(int32_t seconds) {
  ensureConfigLoaded();
  bgpConfig_["hold_time"] = seconds;
}

std::optional<int32_t> BgpConfigSession::getHoldTime() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("hold_time")) {
    return static_cast<int32_t>(bgpConfig_["hold_time"].asInt());
  }
  return std::nullopt;
}

void BgpConfigSession::setConfedAsn(uint64_t asn) {
  ensureConfigLoaded();
  bgpConfig_["local_confed_as_4_byte"] = static_cast<int64_t>(asn);
}

std::optional<uint64_t> BgpConfigSession::getConfedAsn() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("local_confed_as_4_byte")) {
    return static_cast<uint64_t>(bgpConfig_["local_confed_as_4_byte"].asInt());
  }
  return std::nullopt;
}

void BgpConfigSession::setListenAddress(const std::string& listenAddr) {
  ensureConfigLoaded();
  bgpConfig_["listen_addr"] = listenAddr;
}

std::optional<std::string> BgpConfigSession::getListenAddress() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("listen_addr")) {
    return bgpConfig_["listen_addr"].asString();
  }
  return std::nullopt;
}

void BgpConfigSession::setListenPort(int32_t port) {
  ensureConfigLoaded();
  bgpConfig_["listen_port"] = port;
}

std::optional<int32_t> BgpConfigSession::getListenPort() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("listen_port")) {
    return static_cast<int32_t>(bgpConfig_["listen_port"].asInt());
  }
  return std::nullopt;
}

void BgpConfigSession::setDynamicPeerLimit(int32_t limit) {
  ensureConfigLoaded();
  bgpConfig_["dynamic_peer_limit"] = limit;
}

std::optional<int32_t> BgpConfigSession::getDynamicPeerLimit() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("dynamic_peer_limit")) {
    return static_cast<int32_t>(bgpConfig_["dynamic_peer_limit"].asInt());
  }
  return std::nullopt;
}

void BgpConfigSession::setCountConfedsInAsPathLen(bool count) {
  ensureConfigLoaded();
  bgpConfig_["count_confeds_in_as_path_len"] = count;
}

std::optional<bool> BgpConfigSession::getCountConfedsInAsPathLen() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("count_confeds_in_as_path_len")) {
    return bgpConfig_["count_confeds_in_as_path_len"].asBool();
  }
  return std::nullopt;
}

void BgpConfigSession::setLoopbackReflection(bool allow) {
  ensureConfigLoaded();
  bgpConfig_["allow_loopback_reflection"] = allow;
}

std::optional<bool> BgpConfigSession::getLoopbackReflection() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("allow_loopback_reflection")) {
    return bgpConfig_["allow_loopback_reflection"].asBool();
  }
  return std::nullopt;
}

void BgpConfigSession::setRibAllocatedPathIds(bool enable) {
  ensureConfigLoaded();
  bgpConfig_["enable_rib_allocated_path_id"] = enable;
}

std::optional<bool> BgpConfigSession::getRibAllocatedPathIds() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("enable_rib_allocated_path_id")) {
    return bgpConfig_["enable_rib_allocated_path_id"].asBool();
  }
  return std::nullopt;
}

void BgpConfigSession::setDynamicPolicyEval(bool enable) {
  ensureConfigLoaded();
  bgpConfig_["enable_dynamic_policy_evaluation"] = enable;
}

std::optional<bool> BgpConfigSession::getDynamicPolicyEval() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("enable_dynamic_policy_evaluation")) {
    return bgpConfig_["enable_dynamic_policy_evaluation"].asBool();
  }
  return std::nullopt;
}

void BgpConfigSession::setEnableUpdateGroups(bool enable) {
  ensureConfigLoaded();
  bgpConfig_["enable_update_group"] = enable;
}

std::optional<bool> BgpConfigSession::getEnableUpdateGroups() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("enable_update_group")) {
    return bgpConfig_["enable_update_group"].asBool();
  }
  return std::nullopt;
}

void BgpConfigSession::setSerializeGroupPdus(bool enable) {
  ensureConfigLoaded();
  bgpConfig_["enable_serialize_group_pdu"] = enable;
}

std::optional<bool> BgpConfigSession::getSerializeGroupPdus() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("enable_serialize_group_pdu")) {
    return bgpConfig_["enable_serialize_group_pdu"].asBool();
  }
  return std::nullopt;
}

// ==================== Global Graceful Restart ====================

void BgpConfigSession::setGracefulRestartStateful(bool enable) {
  ensureConfigLoaded();
  bgpConfig_["support_stateful_gr"] = enable;
}

std::optional<bool> BgpConfigSession::getGracefulRestartStateful() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("support_stateful_gr")) {
    return bgpConfig_["support_stateful_gr"].asBool();
  }
  return std::nullopt;
}

void BgpConfigSession::setGracefulRestartTime(int32_t seconds) {
  ensureConfigLoaded();
  bgpConfig_["graceful_restart_seconds"] = seconds;
}

std::optional<int32_t> BgpConfigSession::getGracefulRestartTime() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("graceful_restart_seconds")) {
    return static_cast<int32_t>(bgpConfig_["graceful_restart_seconds"].asInt());
  }
  return std::nullopt;
}

void BgpConfigSession::setGracefulRestartOptimized(bool enable) {
  ensureConfigLoaded();
  bgpConfig_["enable_optimized_gr"] = enable;
}

std::optional<bool> BgpConfigSession::getGracefulRestartOptimized() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("enable_optimized_gr")) {
    return bgpConfig_["enable_optimized_gr"].asBool();
  }
  return std::nullopt;
}

// ==================== Global Best Path Selection ====================

void BgpConfigSession::setBestPathEnableWeight(bool enable) {
  ensureConfigLoaded();
  bgpConfig_["enable_weight_comparison"] = enable;
}

std::optional<bool> BgpConfigSession::getBestPathEnableWeight() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("enable_weight_comparison")) {
    return bgpConfig_["enable_weight_comparison"].asBool();
  }
  return std::nullopt;
}

void BgpConfigSession::setBestPathEnableMed(bool enable) {
  ensureConfigLoaded();
  bgpConfig_["enable_med_comparison"] = enable;
}

std::optional<bool> BgpConfigSession::getBestPathEnableMed() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("enable_med_comparison")) {
    return bgpConfig_["enable_med_comparison"].asBool();
  }
  return std::nullopt;
}

void BgpConfigSession::setBestPathMedMissingAsWorst(bool enable) {
  ensureConfigLoaded();
  bgpConfig_["enable_med_missing_as_worst"] = enable;
}

std::optional<bool> BgpConfigSession::getBestPathMedMissingAsWorst() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("enable_med_missing_as_worst")) {
    return bgpConfig_["enable_med_missing_as_worst"].asBool();
  }
  return std::nullopt;
}

void BgpConfigSession::setNextHopTracking(bool enable) {
  ensureConfigLoaded();
  bgpConfig_["enable_next_hop_tracking"] = enable;
}

std::optional<bool> BgpConfigSession::getNextHopTracking() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("enable_next_hop_tracking")) {
    return bgpConfig_["enable_next_hop_tracking"].asBool();
  }
  return std::nullopt;
}

// ==================== Global Link Bandwidth / UCMP ====================

void BgpConfigSession::setUseLbwCommunity(bool enable) {
  ensureConfigLoaded();
  bgpConfig_["compute_ucmp_from_link_bandwidth_community"] = enable;
}

std::optional<bool> BgpConfigSession::getUseLbwCommunity() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("compute_ucmp_from_link_bandwidth_community")) {
    return bgpConfig_["compute_ucmp_from_link_bandwidth_community"].asBool();
  }
  return std::nullopt;
}

void BgpConfigSession::setUcmpWidth(int32_t width) {
  ensureConfigLoaded();
  bgpConfig_["ucmp_width"] = width;
}

std::optional<int32_t> BgpConfigSession::getUcmpWidth() const {
  const_cast<BgpConfigSession*>(this)->ensureConfigLoaded();
  if (bgpConfig_.count("ucmp_width")) {
    return static_cast<int32_t>(bgpConfig_["ucmp_width"].asInt());
  }
  return std::nullopt;
}

// ==================== Peer Group Configuration ====================

folly::dynamic& BgpConfigSession::findOrCreatePeerGroup(
    const std::string& groupName) {
  ensureConfigLoaded();
  auto& peerGroups = bgpConfig_["peer_groups"];

  // Find existing peer group
  for (auto& group : peerGroups) {
    if (group["name"].asString() == groupName) {
      return group;
    }
  }

  // Create new peer group with required fields
  folly::dynamic newGroup = folly::dynamic::object("name", groupName);

  peerGroups.push_back(std::move(newGroup));
  return peerGroups[peerGroups.size() - 1];
}

void BgpConfigSession::createPeerGroup(const std::string& groupName) {
  findOrCreatePeerGroup(groupName);
}

void BgpConfigSession::setPeerGroupRemoteAsn(
    const std::string& groupName,
    uint64_t remoteAsn) {
  findOrCreatePeerGroup(groupName)["remote_as_4_byte"] =
      static_cast<int64_t>(remoteAsn);
}

void BgpConfigSession::setPeerGroupLocalAsn(
    const std::string& groupName,
    uint64_t localAsn) {
  findOrCreatePeerGroup(groupName)["local_as"] = static_cast<int64_t>(localAsn);
}

void BgpConfigSession::setPeerGroupDescription(
    const std::string& groupName,
    const std::string& description) {
  findOrCreatePeerGroup(groupName)["description"] = description;
}

void BgpConfigSession::setPeerGroupLocalAddr(
    const std::string& groupName,
    const std::string& localAddr) {
  findOrCreatePeerGroup(groupName)["local_addr"] = localAddr;
}

void BgpConfigSession::setPeerGroupNextHop4(
    const std::string& groupName,
    const std::string& nextHop4) {
  findOrCreatePeerGroup(groupName)["next_hop4"] = nextHop4;
}

void BgpConfigSession::setPeerGroupNextHop6(
    const std::string& groupName,
    const std::string& nextHop6) {
  findOrCreatePeerGroup(groupName)["next_hop6"] = nextHop6;
}

void BgpConfigSession::setPeerGroupPassive(
    const std::string& groupName,
    bool isPassive) {
  findOrCreatePeerGroup(groupName)["is_passive"] = isPassive;
}

void BgpConfigSession::setPeerGroupRrClient(
    const std::string& groupName,
    bool isRrClient) {
  findOrCreatePeerGroup(groupName)["is_rr_client"] = isRrClient;
}

void BgpConfigSession::setPeerGroupNextHopSelf(
    const std::string& groupName,
    bool nextHopSelf) {
  findOrCreatePeerGroup(groupName)["next_hop_self"] = nextHopSelf;
}

void BgpConfigSession::setPeerGroupConfedPeer(
    const std::string& groupName,
    bool isConfedPeer) {
  findOrCreatePeerGroup(groupName)["is_confed_peer"] = isConfedPeer;
}

void BgpConfigSession::setPeerGroupPort(
    const std::string& groupName,
    int32_t port) {
  findOrCreatePeerGroup(groupName)["peer_port"] = port;
}

void BgpConfigSession::setPeerGroupLocalPort(
    const std::string& groupName,
    int32_t port) {
  findOrCreatePeerGroup(groupName)["local_port"] = port;
}

void BgpConfigSession::setPeerGroupConnectMode(
    const std::string& groupName,
    const std::string& connectMode) {
  findOrCreatePeerGroup(groupName)["connect_mode"] = connectMode;
}

folly::dynamic& BgpConfigSession::ensurePeerGroupTimers(
    const std::string& groupName) {
  auto& group = findOrCreatePeerGroup(groupName);
  if (!group.count("bgp_peer_timers")) {
    group["bgp_peer_timers"] = folly::dynamic::object;
  }
  return group["bgp_peer_timers"];
}

void BgpConfigSession::setPeerGroupHoldTime(
    const std::string& groupName,
    int32_t holdTime) {
  ensurePeerGroupTimers(groupName)["hold_time_seconds"] = holdTime;
}

void BgpConfigSession::setPeerGroupKeepalive(
    const std::string& groupName,
    int32_t keepalive) {
  ensurePeerGroupTimers(groupName)["keep_alive_seconds"] = keepalive;
}

void BgpConfigSession::setPeerGroupOutDelay(
    const std::string& groupName,
    int32_t outDelay) {
  ensurePeerGroupTimers(groupName)["out_delay_seconds"] = outDelay;
}

void BgpConfigSession::setPeerGroupGracefulRestartTime(
    const std::string& groupName,
    int32_t seconds) {
  findOrCreatePeerGroup(groupName)["graceful_restart_seconds"] = seconds;
}

void BgpConfigSession::setPeerGroupStatefulHa(
    const std::string& groupName,
    bool enable) {
  findOrCreatePeerGroup(groupName)["enable_stateful_ha"] = enable;
}

// Peer group AFI/SAFI
void BgpConfigSession::setPeerGroupDisableIpv4Afi(
    const std::string& groupName,
    bool disable) {
  findOrCreatePeerGroup(groupName)["disable_ipv4_afi"] = disable;
}

void BgpConfigSession::setPeerGroupDisableIpv6Afi(
    const std::string& groupName,
    bool disable) {
  findOrCreatePeerGroup(groupName)["disable_ipv6_afi"] = disable;
}

void BgpConfigSession::setPeerGroupIpv4LabeledUnicast(
    const std::string& groupName,
    bool enable) {
  findOrCreatePeerGroup(groupName)["is_afi_ipv4_lu_configured"] = enable;
}

void BgpConfigSession::setPeerGroupIpv6LabeledUnicast(
    const std::string& groupName,
    bool enable) {
  findOrCreatePeerGroup(groupName)["is_afi_ipv6_lu_configured"] = enable;
}

void BgpConfigSession::setPeerGroupIpv4OverIpv6Nh(
    const std::string& groupName,
    bool enable) {
  findOrCreatePeerGroup(groupName)["v4_over_v6_nexthop"] = enable;
}

// Peer group add-path helper
folly::dynamic& BgpConfigSession::ensurePeerGroupAddPath(
    const std::string& groupName) {
  auto& group = findOrCreatePeerGroup(groupName);
  if (!group.count("add_path")) {
    group["add_path"] = folly::dynamic::object;
  }
  return group["add_path"];
}

void BgpConfigSession::setPeerGroupAddPathSend(
    const std::string& groupName,
    bool enable) {
  ensurePeerGroupAddPath(groupName)["send"] = enable;
}

void BgpConfigSession::setPeerGroupAddPathReceive(
    const std::string& groupName,
    bool enable) {
  ensurePeerGroupAddPath(groupName)["receive"] = enable;
}

// Peer group AS-path settings
void BgpConfigSession::setPeerGroupRemovePrivateAs(
    const std::string& groupName,
    bool remove) {
  findOrCreatePeerGroup(groupName)["remove_private_as"] = remove;
}

void BgpConfigSession::setPeerGroupEnforceFirstAs(
    const std::string& groupName,
    bool enforce) {
  findOrCreatePeerGroup(groupName)["enforce_first_as"] = enforce;
}

void BgpConfigSession::setPeerGroupIngressPolicy(
    const std::string& groupName,
    const std::string& policy) {
  findOrCreatePeerGroup(groupName)["ingress_policy_name"] = policy;
}

void BgpConfigSession::setPeerGroupEgressPolicy(
    const std::string& groupName,
    const std::string& policy) {
  findOrCreatePeerGroup(groupName)["egress_policy_name"] = policy;
}

// Peer group route limits helpers
folly::dynamic& BgpConfigSession::ensurePeerGroupPreFilter(
    const std::string& groupName) {
  auto& group = findOrCreatePeerGroup(groupName);
  if (!group.count("pre_filter")) {
    group["pre_filter"] = folly::dynamic::object;
  }
  return group["pre_filter"];
}

folly::dynamic& BgpConfigSession::ensurePeerGroupPostFilter(
    const std::string& groupName) {
  auto& group = findOrCreatePeerGroup(groupName);
  if (!group.count("post_filter")) {
    group["post_filter"] = folly::dynamic::object;
  }
  return group["post_filter"];
}

void BgpConfigSession::setPeerGroupPreFilterMaxRoutes(
    const std::string& groupName,
    int64_t maxRoutes) {
  ensurePeerGroupPreFilter(groupName)["max_routes"] = maxRoutes;
}

void BgpConfigSession::setPeerGroupPostFilterMaxRoutes(
    const std::string& groupName,
    int64_t maxRoutes) {
  ensurePeerGroupPostFilter(groupName)["max_routes"] = maxRoutes;
}

// Peer group link bandwidth
void BgpConfigSession::setPeerGroupAdvertiseLbw(
    const std::string& groupName,
    bool advertise) {
  findOrCreatePeerGroup(groupName)["advertise_link_bandwidth"] = advertise;
}

void BgpConfigSession::setPeerGroupReceiveLbw(
    const std::string& groupName,
    bool receive) {
  findOrCreatePeerGroup(groupName)["receive_link_bandwidth"] = receive;
}

void BgpConfigSession::setPeerGroupLbwValue(
    const std::string& groupName,
    int64_t bps) {
  findOrCreatePeerGroup(groupName)["link_bandwidth_bps"] = bps;
}

// Peer group identification (for RSW config compatibility)
void BgpConfigSession::setPeerGroupPeerTag(
    const std::string& groupName,
    const std::string& tag) {
  findOrCreatePeerGroup(groupName)["peer_tag"] = tag;
}

// Peer group pre_filter warning settings
void BgpConfigSession::setPeerGroupPreFilterWarningLimit(
    const std::string& groupName,
    int64_t limit) {
  ensurePeerGroupPreFilter(groupName)["warning_limit"] = limit;
}

void BgpConfigSession::setPeerGroupPreFilterWarningOnly(
    const std::string& groupName,
    bool warningOnly) {
  ensurePeerGroupPreFilter(groupName)["warning_only"] = warningOnly;
}

// Peer group timer: withdraw_unprog_delay_seconds
void BgpConfigSession::setPeerGroupWithdrawUnprogDelay(
    const std::string& groupName,
    int32_t seconds) {
  ensurePeerGroupTimers(groupName)["withdraw_unprog_delay_seconds"] = seconds;
}

// ==================== Networks Configuration ====================

void BgpConfigSession::addNetwork6(
    const std::string& prefix,
    const std::string& policyName,
    bool installToFib,
    int32_t minSupportingRoutes) {
  ensureConfigLoaded();

  // Ensure networks6 array exists
  if (!bgpConfig_.count("networks6")) {
    bgpConfig_["networks6"] = folly::dynamic::array();
  }

  // Check if network already exists
  auto& networks = bgpConfig_["networks6"];
  for (auto& network : networks) {
    if (network["prefix"].asString() == prefix) {
      // Update existing network
      network["policy_name"] = policyName;
      network["install_to_fib"] = installToFib;
      network["minimum_supporting_routes"] = minSupportingRoutes;
      return;
    }
  }

  // Add new network entry
  folly::dynamic network = folly::dynamic::object();
  network["prefix"] = prefix;
  network["policy_name"] = policyName;
  network["install_to_fib"] = installToFib;
  network["minimum_supporting_routes"] = minSupportingRoutes;
  networks.push_back(std::move(network));
}

// ==================== Switch Limit Configuration ====================

void BgpConfigSession::setSwitchLimitPrefixLimit(int64_t limit) {
  ensureConfigLoaded();
  if (!bgpConfig_.count("switch_limit_config")) {
    bgpConfig_["switch_limit_config"] = folly::dynamic::object();
  }
  bgpConfig_["switch_limit_config"]["prefix_limit"] = limit;
}

void BgpConfigSession::setSwitchLimitTotalPathLimit(int64_t limit) {
  ensureConfigLoaded();
  if (!bgpConfig_.count("switch_limit_config")) {
    bgpConfig_["switch_limit_config"] = folly::dynamic::object();
  }
  bgpConfig_["switch_limit_config"]["total_path_limit"] = limit;
}

void BgpConfigSession::setSwitchLimitMaxGoldenVips(int64_t limit) {
  ensureConfigLoaded();
  if (!bgpConfig_.count("switch_limit_config")) {
    bgpConfig_["switch_limit_config"] = folly::dynamic::object();
  }
  bgpConfig_["switch_limit_config"]["max_golden_vips"] = limit;
}

void BgpConfigSession::setSwitchLimitOverloadProtectionMode(int32_t mode) {
  ensureConfigLoaded();
  if (!bgpConfig_.count("switch_limit_config")) {
    bgpConfig_["switch_limit_config"] = folly::dynamic::object();
  }
  bgpConfig_["switch_limit_config"]["overload_protection_mode"] = mode;
}

// ==================== Export Functions ====================

std::string BgpConfigSession::exportConfig() const {
  return folly::toPrettyJson(bgpConfig_);
}

} // namespace facebook::fboss
