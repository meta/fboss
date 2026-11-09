// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/fsdb/client/FsdbSyncManager.h"
#ifndef IS_OSS
#include "configerator/structs/neteng/fboss/bitsflow/gen-cpp2/bitsflow_types.h"
#endif
#include "fboss/fsdb/if/FsdbModel.h"

#include "fboss/agent/gen-cpp2/agent_info_types.h"
#include "fboss/agent/gen-cpp2/switch_reachability_types.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"

DECLARE_bool(fsdb_sync_full_state);
DECLARE_bool(agent_fsdb_sync);

namespace facebook::fboss {

namespace fsdb_model_tags = apache::thrift::ident;
namespace agent_config_tags = apache::thrift::ident;
#ifndef IS_OSS
namespace cfgr_bitsflow = ::configerator::structs::neteng::fboss::bitsflow;
#endif

template <>
struct thrift_cow::ResolveMemberType<
    thrift_cow::ThriftStructNode<fsdb::AgentData>,
    fsdb_model_tags::switchState> : std::true_type {
  using type = SwitchState;
};

class AgentFsdbSyncManager
    : public fsdb::
          FsdbSyncManager<fsdb::AgentData, true /* EnablePatchAPIs */> {
  /* list of maps which are subscribed to by external consumers */
  using SubscribedMaps = std::tuple<
      std::type_identity<switch_state_tags::portMaps>,
      std::type_identity<switch_state_tags::transceiverMaps>,
      std::type_identity<switch_state_tags::qosPolicyMaps>,
      std::type_identity<switch_state_tags::sflowCollectorMaps>,
      std::type_identity<switch_state_tags::aggregatePortMaps>,
      std::type_identity<switch_state_tags::systemPortMaps>,
      std::type_identity<switch_state_tags::vlanMaps>,
      std::type_identity<switch_state_tags::interfaceMaps>>;

 public:
  using Base = fsdb::FsdbSyncManager<fsdb::AgentData>;
  using AgentData = Base::CowState;
  using AgentDataSwitchState = typename AgentData::Fields::TypeFor<
      fsdb_model_tags::switchState>::element_type;

  static_assert(
      std::is_same_v<AgentDataSwitchState, SwitchState>,
      "Switch state not represented in agent data");

  explicit AgentFsdbSyncManager(
      const std::shared_ptr<fsdb::FsdbPubSubManager>& pubSubMgr);
  AgentFsdbSyncManager();

  // invoke with a new state to send
  void stateUpdated(const StateDelta& delta);
  void cfgUpdated(
      const cfg::SwitchConfig& oldConfig,
      const cfg::SwitchConfig& newConfig);
#ifndef IS_OSS
  void bitsflowLockdownLevelUpdated(
      std::optional<cfgr_bitsflow::BitsflowLockdownLevel> oldLevel,
      std::optional<cfgr_bitsflow::BitsflowLockdownLevel> newLevel);
#endif
  void updateDsfSubscriberState(
      const std::string& nodeName,
      fsdb::FsdbSubscriptionState oldState,
      fsdb::FsdbSubscriptionState newState);
  void switchReachabilityChanged(
      int64_t switchId,
      switch_reachability::SwitchReachability newReachability);
  void agentInfoChanged(agent_info::AgentInfo newAgentInfo);
  void updateIPhyStates(std::map<std::string, phy::PhyState>&& iPhyStates);

 private:
  void stateUpdatedDelta(const StateDelta& delta);

  static bool modify(
      std::shared_ptr<facebook::fboss::SwitchState>& oldState,
      const std::shared_ptr<facebook::fboss::SwitchState>& newState);
};

} // namespace facebook::fboss
