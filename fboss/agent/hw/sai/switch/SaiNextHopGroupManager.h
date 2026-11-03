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

#include "fboss/agent/hw/sai/api/NextHopGroupApi.h"

#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/hw/sai/api/ArsApi.h"
#include "fboss/agent/hw/sai/api/NextHopApi.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/switch/SaiArsManager.h"
#include "fboss/agent/hw/sai/switch/SaiNextHopManager.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/state/FlowletSwitchingConfig.h"
#include "fboss/agent/state/RouteNextHop.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/types.h"
#include "fboss/lib/RefMap.h"

#include <memory>
#include <tuple>
#include <utility>
#include "folly/container/F14Map.h"
#include "folly/container/F14Set.h"

#include <boost/container/flat_set.hpp>

#include "fboss/agent/hw/sai/store/SaiObjectEventSubscriber-defs.h"
#include "fboss/agent/hw/sai/store/SaiObjectEventSubscriber.h"

namespace facebook::fboss {

class SaiArsManager;
class SaiManagerTable;
class SaiPlatform;
class SaiNextHopGroupManager;
class SaiStore;
struct SaiNextHopGroupHandle;

using SaiNextHopGroup = SaiObject<SaiNextHopGroupTraits>;
using SaiNextHopGroupMember = SaiObject<SaiNextHopGroupMemberTraits>;
using SaiNextHop = ConditionSaiObjectType<SaiNextHopTraits>::type;
using SaiNextHopGroupMemberInfo = std::pair<
    SaiNextHopGroupMemberTraits::AdapterHostKey,
    SaiNextHopGroupMemberTraits::Attributes::Weight>;

sai_next_hop_group_type_t getNextHopGroupType(
    const RouteNextHopEntry::NextHopSet& nextHops);

bool isProtectionNextHopGroupType(sai_next_hop_group_type_t nextHopGroupType);

struct SaiNextHopGroupKey {
  SaiNextHopGroupKey(
      RouteNextHopEntry::NextHopSet nextHops,
      std::optional<cfg::SwitchingMode> switchingMode,
      sai_next_hop_group_type_t groupType = SAI_NEXT_HOP_GROUP_TYPE_ECMP)
      : nextHops(std::move(nextHops)),
        switchingMode(switchingMode),
        groupType(groupType) {}

  bool operator==(const SaiNextHopGroupKey& other) const {
    return nextHops == other.nextHops && switchingMode == other.switchingMode &&
        groupType == other.groupType;
  }

  bool operator<(const SaiNextHopGroupKey& other) const {
    return std::tie(nextHops, switchingMode, groupType) <
        std::tie(other.nextHops, other.switchingMode, other.groupType);
  }

  RouteNextHopEntry::NextHopSet nextHops;
  std::optional<cfg::SwitchingMode> switchingMode;
  sai_next_hop_group_type_t groupType;
};

template <typename T>
class ManagedNextHop;

template <typename NextHopTraits>
class ManagedSaiNextHopGroupNextHopMember
    : public SaiObjectEventAggregateSubscriber<
          ManagedSaiNextHopGroupNextHopMember<NextHopTraits>,
          SaiNextHopGroupMemberTraits,
          NextHopTraits> {
 public:
  using Base = SaiObjectEventAggregateSubscriber<
      ManagedSaiNextHopGroupNextHopMember<NextHopTraits>,
      SaiNextHopGroupMemberTraits,
      NextHopTraits>;

  using NextHopWeakPtr = std::weak_ptr<const SaiObject<NextHopTraits>>;
  using PublisherObjects = std::tuple<NextHopWeakPtr>;
  using NextHopWeight =
      std::optional<typename SaiNextHopGroupMemberTraits::Attributes::Weight>;
  ManagedSaiNextHopGroupNextHopMember(
      SaiNextHopGroupManager* manager,
      SaiNextHopGroupHandle* nhgroup,
      std::shared_ptr<ManagedNextHop<NextHopTraits>> managedNextHop,
      SaiNextHopGroupTraits::AdapterKey nexthopGroupId,
      sai_next_hop_group_type_t nextHopGroupType,
      NextHopWeight weight,
      bool fixedWidthMode)
      : Base(managedNextHop->adapterHostKey()),
        manager_(manager),
        nhgroup_(nhgroup),
        managedNextHop_(managedNextHop),
        nexthopGroupId_(nexthopGroupId),
        nextHopGroupType_(nextHopGroupType),
        weight_(weight),
        fixedWidthMode_(fixedWidthMode) {}

  ~ManagedSaiNextHopGroupNextHopMember() {
    this->resetObject();
  }

  std::pair<
      std::optional<SaiNextHopGroupMemberTraits::AdapterHostKey>,
      std::optional<SaiNextHopGroupMemberTraits::CreateAttributes>>
  getAdapterHostKeyAndCreateAttributes();

  void setObjectOwnership(
      const std::shared_ptr<SaiNextHopGroupMember>& object) {
    this->setObject(object);
  }

  std::shared_ptr<SaiObject<SaiNextHopGroupMemberTraits>>
  getNhopGroupMemberObject() {
    return this->getObject();
  }

  void createObject(PublisherObjects added);

  void removeObject(size_t index, PublisherObjects removed);

  std::string toString() const;

 private:
  SaiNextHopGroupManager* manager_;
  SaiNextHopGroupHandle* nhgroup_;
  std::shared_ptr<ManagedNextHop<NextHopTraits>> managedNextHop_;
  SaiNextHopGroupTraits::AdapterKey nexthopGroupId_;
  sai_next_hop_group_type_t nextHopGroupType_;
  NextHopWeight weight_;
  bool fixedWidthMode_;
  std::optional<SaiNextHopGroupMemberTraits::AdapterHostKey> adapterHostKey_;
  std::optional<SaiNextHopGroupMemberTraits::CreateAttributes>
      createAttributes_;
};

class SaiNextHopGroupChildGroupMember {
 public:
  SaiNextHopGroupChildGroupMember(
      SaiNextHopGroupManager* manager,
      std::shared_ptr<SaiNextHopGroupHandle> childNextHopGroup,
      const SaiNextHopGroupTraits::AdapterKey& parentNextHopGroupId);

  std::pair<
      std::optional<SaiNextHopGroupMemberTraits::AdapterHostKey>,
      std::optional<SaiNextHopGroupMemberTraits::CreateAttributes>>
  getAdapterHostKeyAndCreateAttributes();

  std::shared_ptr<SaiObject<SaiNextHopGroupMemberTraits>>
  getNhopGroupMemberObject() {
    return nextHopGroupMember_;
  }

  std::string toString() const;

 private:
  std::shared_ptr<SaiNextHopGroupHandle> childNextHopGroup_;
  SaiNextHopGroupTraits::AdapterKey parentNextHopGroupId_;
  std::optional<SaiNextHopGroupMemberTraits::AdapterHostKey> adapterHostKey_;
  std::optional<SaiNextHopGroupMemberTraits::CreateAttributes>
      createAttributes_;
  std::shared_ptr<SaiNextHopGroupMember> nextHopGroupMember_;
};

class NextHopGroupMember {
 public:
  using NextHopWeight =
      std::optional<SaiNextHopGroupMemberTraits::Attributes::Weight>;
  using ManagedIpNextHopGroupMember =
      ManagedSaiNextHopGroupNextHopMember<SaiIpNextHopTraits>;
  using ManagedMplsNextHopGroupMember =
      ManagedSaiNextHopGroupNextHopMember<SaiMplsNextHopTraits>;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  using ManagedSrv6NextHopGroupMember =
      ManagedSaiNextHopGroupNextHopMember<SaiSrv6SidlistNextHopTraits>;
#endif

  NextHopGroupMember(
      SaiNextHopGroupManager* manager,
      SaiNextHopGroupHandle* nhgroup,
      SaiNextHopGroupTraits::AdapterKey nexthopGroupId,
      sai_next_hop_group_type_t nextHopGroupType,
      ManagedSaiNextHop managedSaiNextHop,
      NextHopWeight nextHopWeight,
      bool fixedWidthMode);

  bool isProgrammed() const {
    return std::visit(
        [](auto arg) { return arg && arg->isAlive(); },
        managedNextHopGroupMember_);
  }

  std::string toString() {
    return std::visit(
        [](auto arg) {
          if (!arg) {
            return std::string("");
          }
          return arg->toString();
        },
        managedNextHopGroupMember_);
  }

  std::pair<
      std::optional<SaiNextHopGroupMemberTraits::AdapterHostKey>,
      std::optional<SaiNextHopGroupMemberTraits::CreateAttributes>>
  getAdapterHostKeyAndCreateAttributes() {
    return std::visit(
        [](auto arg) {
          std::pair<
              std::optional<SaiNextHopGroupMemberTraits::AdapterHostKey>,
              std::optional<SaiNextHopGroupMemberTraits::CreateAttributes>>
              ret;
          if (arg) {
            ret = arg->getAdapterHostKeyAndCreateAttributes();
          }
          return ret;
        },
        managedNextHopGroupMember_);
  }

  void setObject(const std::shared_ptr<SaiNextHopGroupMember>& object) {
    return std::visit(
        [&](auto arg) {
          CHECK(arg);
          return arg->setObjectOwnership(object);
        },
        managedNextHopGroupMember_);
  }

  std::shared_ptr<SaiObject<SaiNextHopGroupMemberTraits>> getObject() {
    return std::visit(
        [&](auto arg) {
          std::shared_ptr<SaiObject<SaiNextHopGroupMemberTraits>> object;
          if (arg) {
            object = arg->getNhopGroupMemberObject();
          }
          return object;
        },
        managedNextHopGroupMember_);
  }

 private:
  std::variant<
      std::shared_ptr<ManagedIpNextHopGroupMember>,
      std::shared_ptr<ManagedMplsNextHopGroupMember>
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
      ,
      std::shared_ptr<ManagedSrv6NextHopGroupMember>
#endif
      >
      managedNextHopGroupMember_;
};

struct ArsHwCounter {
  uint64_t failPackets{0};
  uint64_t portReassignments{0};
};

// What a group counted between two sweeps, as opposed to what its counters
// read. Same shape, different meaning, so the distinction is in the name.
using ArsCounterDelta = ArsHwCounter;

struct SaiNextHopGroupHandle {
  std::shared_ptr<SaiNextHopGroup> nextHopGroup;
  std::vector<std::shared_ptr<NextHopGroupMember>> members_;
  // Currently only Adj FRR with  1:N protection is supported,
  // so a group can have only one child next hop group member.
  std::shared_ptr<SaiNextHopGroupChildGroupMember> childGroupMember_;
  bool fixedWidthMode{false};
  bool bulkCreate{false};
  std::set<SaiNextHopGroupMemberInfo> fixedWidthNextHopGroupMembers_;
  uint32_t maxVariableWidthEcmpSize{0};
  std::optional<cfg::SwitchingMode> desiredEcmpSwitchingMode_;
  ArsHwCounter arsHwCounter_;
  SaiStore* saiStore_{nullptr};
  const SaiPlatform* platform_{nullptr};
  sai_object_id_t adapterKey() const {
    if (!nextHopGroup) {
      return SAI_NULL_OBJECT_ID;
    }
    return nextHopGroup->adapterKey();
  }
  size_t nextHopGroupSize() const;
  // Reads this group's ARS counters off the hardware, stores them in
  // arsHwCounter_ for the next sweep to compare against, and returns how much
  // they moved since the previous sweep. Zero for a group with no ARS object
  // attached, whose counters cannot move.
  ArsCounterDelta updateStats();
  void memberAdded(
      SaiNextHopGroupMemberInfo memberInfo,
      bool updateHardware = true);
  void memberRemoved(
      SaiNextHopGroupMemberInfo memberInfo,
      bool updateHardware = true);
  void bulkProgramMembers(
      SaiNextHopGroupMemberInfo modifiedMemberInfo,
      bool added,
      bool updateHardware);
  ~SaiNextHopGroupHandle();
};

class SaiNextHopGroupManager {
 public:
  SaiNextHopGroupManager(
      SaiStore* saiStore,
      SaiManagerTable* managerTable,
      const SaiPlatform* platform);

  std::shared_ptr<SaiNextHopGroupHandle> incRefOrAddNextHopGroup(
      const SaiNextHopGroupKey& key);

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  // MONITORED_OBJECT for a PROTECTION group's PRIMARY member: the SAI id of
  // the egress port/LAG its neighbor resolves out of. nullopt when the ASIC
  // infers it itself. Throws when the ASIC needs the attribute but the egress
  // object cannot be derived -- without it the ASIC would never fail over.
  std::optional<SaiNextHopGroupMemberTraits::Attributes::MonitoredObject>
  getMonitoredObjectIf(
      const SaiNeighborTraits::NeighborEntry& neighborEntry) const;
#endif

  const SaiNextHopGroupHandle* getNextHopGroup(
      const SaiNextHopGroupKey& key) const;

  std::shared_ptr<SaiNextHopGroupMember> createSaiObject(
      const typename SaiNextHopGroupMemberTraits::AdapterHostKey& key,
      const typename SaiNextHopGroupMemberTraits::CreateAttributes& attributes);

  std::shared_ptr<SaiNextHopGroupMember> getSaiObject(
      const typename SaiNextHopGroupMemberTraits::AdapterHostKey& key);

  std::shared_ptr<SaiNextHopGroupMember> getSaiObjectFromWBCache(
      const typename SaiNextHopGroupMemberTraits::AdapterHostKey& key);

  std::string listManagedObjects() const;

  void updateArsModeAll(
      const std::shared_ptr<FlowletSwitchingConfig>& newFlowletConfig);

  void setPrimaryArsSwitchingMode(
      std::optional<cfg::SwitchingMode> switchingMode);

  void setMinWidthForArsVirtualGroup(
      std::optional<int32_t> minWidthForArsVirtualGroup);

  void setEcmpGroupSettings(const EcmpGroupSettingsMap& ecmpGroupSettings);

  // Absent key means the feature was not configured for that group type, which
  // is not the same as configured off. Callers outside this manager want the
  // combined answer, so the lookup is not exposed raw.
  bool isSplitHorizonEnabled(cfg::EcmpGroupType groupType) const;

  cfg::SwitchingMode getNextHopGroupSwitchingMode(
      const RouteNextHopEntry::NextHopSet& swNextHops);

  std::vector<EcmpDetails> getAllEcmpDetails() const;
  void updateStats();
  HwFlowletStats getHwFlowletStats() const;

 private:
  bool isFixedWidthNextHopGroup(
      const RouteNextHopEntry::NextHopSet& swNextHops) const;
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  std::optional<SaiNextHopGroupTraits::Attributes::ArsObjectId> getArsObjectId(
      std::optional<cfg::SwitchingMode> switchingMode,
      size_t nextHopGroupSize) const;
#endif
  SaiStore* saiStore_;
  SaiManagerTable* managerTable_;
  const SaiPlatform* platform_;
  // TODO(borisb): improve SaiObject/SaiStore to the point where they
  // support the next hop group use case correctly, rather than this
  // abomination of multiple levels of RefMaps :(
  FlatRefMap<SaiNextHopGroupKey, SaiNextHopGroupHandle> handles_;
  UnorderedRefMap<
      std::pair<typename SaiNextHopGroupTraits::AdapterKey, ResolvedNextHop>,
      NextHopGroupMember>
      nextHopGroupMembers_;
  std::optional<cfg::SwitchingMode> primaryArsMode_;
  std::optional<int32_t> minWidthForArsVirtualGroup_;
  EcmpGroupSettingsMap ecmpGroupSettings_;
  HwFlowletStats arsStats_;
};

} // namespace facebook::fboss
