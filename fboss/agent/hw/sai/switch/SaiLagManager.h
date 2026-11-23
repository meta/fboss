// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/agent/hw/HwTrunkCounters.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/hw/sai/api/LagApi.h"
#include "fboss/agent/hw/sai/store/SaiObjectWithCounters.h"
#include "fboss/agent/hw/sai/switch/SaiBridgeManager.h"
#include "fboss/agent/state/AggregatePort.h"

namespace facebook::fboss {

struct ConcurrentIndices;

using SaiLag = SaiObject<SaiLagTraits>;
using SaiLagMember = SaiObject<SaiLagMemberTraits>;

struct SaiLagHandle {
  std::shared_ptr<SaiLag> lag{};
  std::map<PortSaiId, std::shared_ptr<SaiLagMember>> members{};
  std::shared_ptr<SaiBridgePort> bridgePort{};
  uint32_t minimumLinkCount{0};
  // A LAG that carries a router interface of its own need not be a member of
  // any VLAN, in which case it has no port vlan id and no bridge port.
  std::optional<VlanID> vlanId{};
  std::unique_ptr<utility::HwTrunkCounters> counters;
};

class SaiManagerTable;
class SaiPlatform;
class SaiStore;

class SaiLagManager {
 public:
  using Handles =
      folly::F14FastMap<AggregatePortID, std::unique_ptr<SaiLagHandle>>;
  SaiLagManager(
      SaiStore* saiStore,
      SaiManagerTable* managerTable,
      SaiPlatform* platform,
      ConcurrentIndices* concurrentIndices)
      : saiStore_(saiStore),
        managerTable_(managerTable),
        platform_(platform),
        concurrentIndices_(concurrentIndices) {}
  ~SaiLagManager();
  SaiLagManager(const SaiLagManager&) = delete;
  SaiLagManager& operator=(const SaiLagManager&) = delete;
  SaiLagManager(SaiLagManager&&) = delete;
  SaiLagManager& operator=(SaiLagManager&&) = delete;
  LagSaiId addLag(const std::shared_ptr<AggregatePort>& aggregatePort);
  void removeLag(const std::shared_ptr<AggregatePort>& aggregatePort);
  void changeLag(
      const std::shared_ptr<AggregatePort>& oldAggregatePort,
      const std::shared_ptr<AggregatePort>& newAggregatePort);

  SaiLagHandle* getLagHandleIf(AggregatePortID aggregatePortID) const;
  SaiLagHandle* getLagHandle(AggregatePortID aggregatePortID) const;
  bool isLagUp(const AggregatePortID& aggregatePortID) const;

  uint8_t getLagCount() const {
    return handles_.size();
  }
  size_t getLagMemberCount(AggregatePortID aggPort) const;
  size_t getActiveMemberCount(AggregatePortID aggPort) const;

  void disableMember(AggregatePortID aggPort, PortID subPort);

  bool isLagMember(PortID port);

  void addBridgePort(const std::shared_ptr<AggregatePort>& aggPort);
  void changeBridgePort(
      const std::shared_ptr<AggregatePort>& oldPort,
      const std::shared_ptr<AggregatePort>& newPort);

  void updateStats(AggregatePortID aggPort);

  std::optional<HwTrunkStats> getHwTrunkStats(AggregatePortID aggPort) const;
  std::map<std::string, HwTrunkStats> getAllHwTrunkStats() const;

 private:
  void programTtlDecrement(const AggregatePort& aggregatePort);

  std::pair<PortSaiId, std::shared_ptr<SaiLagMember>> addMember(
      const std::shared_ptr<SaiLag>& lag,
      AggregatePortID aggPort,
      PortID subPort,
      AggregatePort::Forwarding fwdState = AggregatePort::Forwarding::DISABLED);
  void removeMember(AggregatePortID aggPort, PortID subPort);

  void removeLagHandle(AggregatePortID aggPort, SaiLagHandle* handle);

  void setMemberState(SaiLagMember* member, AggregatePort::Forwarding fwdState);
  SaiLagMember* getMember(SaiLagHandle* handle, PortID port);

  SaiStore* saiStore_;
  SaiManagerTable* managerTable_;
  SaiPlatform* platform_;
  ConcurrentIndices* concurrentIndices_;
  Handles handles_;
};

} // namespace facebook::fboss
