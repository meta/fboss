// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/agent/gen-cpp2/agent_info_types.h"
#include "fboss/agent/gen-cpp2/agent_stats_types.h"
#include "fboss/agent/gen-cpp2/switch_reachability_types.h"
#include "fboss/fsdb/client/FsdbPubSubManager.h"
#include "fboss/fsdb/client/FsdbStreamClient.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"

#include <memory>

namespace facebook::fboss {
class SwSwitch;
class AgentFsdbSyncManager;
class StateDelta;

namespace fsdb {
class FsdbPubSubManager;
enum class FsdbSubscriptionState;
} // namespace fsdb
namespace cfg {
class SwitchConfig;
}
class FsdbSyncer {
 public:
  explicit FsdbSyncer(SwSwitch* sw);
  ~FsdbSyncer();
  void stateUpdated(const StateDelta& stateDelta);
  void statsUpdated(const AgentStats& stats);

  // TODO - change to AgentConfig once SwSwitch can pass us that
  void cfgUpdated(
      const cfg::SwitchConfig& oldConfig,
      const cfg::SwitchConfig& newConfig);

  void updateDsfSubscriberState(
      const std::string& nodeName,
      fsdb::FsdbSubscriptionState oldState,
      fsdb::FsdbSubscriptionState newState);

  void switchReachabilityChanged(
      int64_t switchId,
      switch_reachability::SwitchReachability newReachability);

  void agentInfoChanged(agent_info::AgentInfo newAgentInfo);

  void iPhyStatesUpdated(std::map<std::string, phy::PhyState>&& iPhyStates);

  void start();
  void stop(bool gracefulStop = false);

  bool isReadyForStatePublishing() const {
    return readyForStatePublishing_.load();
  }

  bool isReadyForStatPublishing() const {
    return readyForStatPublishing_.load();
  }

  fsdb::FsdbPubSubManager* pubSubMgr() {
    return fsdbPubSubMgr_.get();
  }

  uint64_t getPendingUpdatesQueueLength() const;

 private:
  // Paths
  static std::vector<std::string> getAgentStatePath();
  static std::vector<std::string> getAgentStatsPath();
  static std::vector<std::string> getAgentSwitchStatePath();
  static std::vector<std::string> getAgentSwitchConfigPath();

  void fsdbStatPublisherStateChanged(
      fsdb::FsdbStreamClient::State oldState,
      fsdb::FsdbStreamClient::State newState);
  std::optional<std::string> getBitsflowLockdownLevel();

  SwSwitch* sw_;
  std::shared_ptr<fsdb::FsdbPubSubManager> fsdbPubSubMgr_;
  std::atomic<bool> readyForStatePublishing_{false};
  std::atomic<bool> readyForStatPublishing_{false};
  std::unique_ptr<AgentFsdbSyncManager> agentFsdbSyncManager_;
};

} // namespace facebook::fboss
