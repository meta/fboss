// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/FsdbSyncer.h"
#include "fboss/agent/AgentFsdbSyncManager.h"
#ifndef IS_OSS
#include "fboss/facebook/bitsflow/BitsflowHelper.h"
#endif

#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/fsdb/client/FsdbPubSubManager.h"
#include "fboss/fsdb/common/Flags.h"
#include "fboss/fsdb/if/gen-cpp2/fsdb_common_types.h"
#include "fboss/fsdb/if/gen-cpp2/fsdb_oper_types.h"

#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <optional>

namespace {

const thriftpath::RootThriftPath<facebook::fboss::fsdb::FsdbOperStateRoot>
    stateRoot;
const thriftpath::RootThriftPath<facebook::fboss::fsdb::FsdbOperStatsRoot>
    statsRoot;

} // anonymous namespace

namespace facebook::fboss {

uint64_t FsdbSyncer::getPendingUpdatesQueueLength() const {
  return agentFsdbSyncManager_->getPendingUpdatesQueueLength();
}

std::vector<std::string> FsdbSyncer::getAgentStatePath() {
  return stateRoot.agent().tokens();
}

std::vector<std::string> FsdbSyncer::getAgentSwitchStatePath() {
  return stateRoot.agent().switchState().tokens();
}

std::vector<std::string> FsdbSyncer::getAgentSwitchConfigPath() {
  return stateRoot.agent().config().sw().tokens();
}

std::vector<std::string> FsdbSyncer::getAgentStatsPath() {
  return statsRoot.agent().tokens();
}

std::optional<std::string> FsdbSyncer::getBitsflowLockdownLevel() {
#ifndef IS_OSS
  return bitsflow::BitsflowHelper::getCurrentBitsflowLockdownLevel();
#else
  return std::nullopt;
#endif
}

FsdbSyncer::FsdbSyncer(SwSwitch* sw)
    : sw_(sw),
      fsdbPubSubMgr_(std::make_shared<fsdb::FsdbPubSubManager>("agent")) {
  agentFsdbSyncManager_ =
      std::make_unique<AgentFsdbSyncManager>(fsdbPubSubMgr_);

  if (FLAGS_publish_stats_to_fsdb) {
    fsdbPubSubMgr_->createStatPathPublisher(
        getAgentStatsPath(), [this](auto oldState, auto newState) {
          fsdbStatPublisherStateChanged(oldState, newState);
        });
  }
}

void FsdbSyncer::start() {
  // full sync of agent info
  agentFsdbSyncManager_->agentInfoChanged(sw_->getAgentInfo());
  // full sync of initial state
  agentFsdbSyncManager_->stateUpdated(
      StateDelta(std::make_shared<SwitchState>(), sw_->getState()));
  // full sync of configuration
  agentFsdbSyncManager_->cfgUpdated(cfg::SwitchConfig(), sw_->getConfig());
  // sync of bitsflow lock down level
#ifndef IS_OSS
  agentFsdbSyncManager_->bitsflowLockdownLevelUpdated(
      std::optional<cfgr_bitsflow::BitsflowLockdownLevel>(),
      bitsflow::BitsflowHelper::getBitsflowLockdownLevel(
          bitsflow::BitsflowHelper::getCurrentBitsflowLockdownLevel()));
#endif
  agentFsdbSyncManager_->start();
}

FsdbSyncer::~FsdbSyncer() {
  CHECK(!readyForStatPublishing_.load());
}

void FsdbSyncer::stop(bool gracefulStop) {
  // Disable state updates in updateEvb, so this synchronizes
  // with any inflight updates happening in updateEvb
  agentFsdbSyncManager_->stop(gracefulStop);
  agentFsdbSyncManager_.reset();
  readyForStatPublishing_.store(false);
  fsdbPubSubMgr_.reset();
}

void FsdbSyncer::stateUpdated(const StateDelta& stateDelta) {
  agentFsdbSyncManager_->stateUpdated(stateDelta);
}

void FsdbSyncer::cfgUpdated(
    const cfg::SwitchConfig& oldConfig,
    const cfg::SwitchConfig& newConfig) {
  agentFsdbSyncManager_->cfgUpdated(oldConfig, newConfig);
}

void FsdbSyncer::updateDsfSubscriberState(
    const std::string& nodeName,
    fsdb::FsdbSubscriptionState oldState,
    fsdb::FsdbSubscriptionState newState) {
  agentFsdbSyncManager_->updateDsfSubscriberState(nodeName, oldState, newState);
}

void FsdbSyncer::switchReachabilityChanged(
    int64_t switchId,
    switch_reachability::SwitchReachability newReachability) {
  agentFsdbSyncManager_->switchReachabilityChanged(
      switchId, std::move(newReachability));
}

void FsdbSyncer::agentInfoChanged(agent_info::AgentInfo newAgentInfo) {
  agentFsdbSyncManager_->agentInfoChanged(std::move(newAgentInfo));
}

void FsdbSyncer::iPhyStatesUpdated(
    std::map<std::string, phy::PhyState>&& iPhyStates) {
  agentFsdbSyncManager_->updateIPhyStates(std::move(iPhyStates));
}

void FsdbSyncer::statsUpdated(const AgentStats& stats) {
  if (!readyForStatPublishing_.load()) {
    return;
  }
  fsdb::OperState stateUnit;
  stateUnit.contents() =
      apache::thrift::BinarySerializer::serialize<std::string>(stats);
  stateUnit.protocol() = fsdb::OperProtocol::BINARY;
  fsdbPubSubMgr_->publishStat(std::move(stateUnit));
}

void FsdbSyncer::fsdbStatPublisherStateChanged(
    fsdb::FsdbStreamClient::State oldState,
    fsdb::FsdbStreamClient::State newState) {
  CHECK(oldState != newState);
  if (newState == fsdb::FsdbStreamClient::State::CONNECTED) {
    // Stats sync at regular intervals, so let the sync
    // happen in that sequence after a connection.
    readyForStatPublishing_.store(true);
  } else {
    readyForStatPublishing_.store(false);
  }
}

} // namespace facebook::fboss
