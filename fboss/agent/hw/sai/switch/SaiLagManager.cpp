// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/switch/SaiLagManager.h"

#include "fboss/agent/hw/HwPortFb303Stats.h"
#include "fboss/agent/hw/HwTrunkCounters.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/hw/sai/api/SaiApiTable.h"
#include "fboss/agent/hw/sai/switch/ConcurrentIndices.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiVlanManager.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"

#include "fboss/agent/hw/sai/store/SaiStore.h"

#include <folly/container/Enumerate.h>

namespace facebook::fboss {

LagSaiId SaiLagManager::addLag(
    const std::shared_ptr<AggregatePort>& aggregatePort) {
  if (aggregatePort->getAggregatePortType() ==
      cfg::AggregatePortType::HYPER_PORT) {
    // TODO(daiuweix): update HYPER port through SaiPortManager
    return LagSaiId(0);
  }
  XLOG(DBG2) << "adding aggregate port : " << aggregatePort->getID();

  auto name = aggregatePort->getName();
  std::array<char, 32> labelValue{};
  for (auto i = 0; i < 32 && i < name.length(); i++) {
    labelValue[i] = name[i];
  }
  // TODO(pshaikh): support LAG without ports?
  auto subports = aggregatePort->sortedSubports();
  CHECK_GT(subports.size(), 0);
  auto& subPort = subports.front();
  auto portSaiIdsIter = concurrentIndices_->portSaiIds.find(subPort.portID);
  // port must exist before LAG
  CHECK(portSaiIdsIter != concurrentIndices_->portSaiIds.end());
  auto portSaiId = portSaiIdsIter->second;
  // All members of the same LAG are part of the same VLAN, so the first
  // member's VLAN is the LAG's. A routed LAG, one carrying a router interface
  // of its own, is in no VLAN: it gets no port vlan id, no vlan membership and
  // no bridge port.
  auto vlanSaiIdsIter =
      concurrentIndices_->vlanIds.find(PortDescriptorSaiId(portSaiId));
  std::optional<VlanID> vlanID{};
  if (vlanSaiIdsIter != concurrentIndices_->vlanIds.end()) {
    vlanID = vlanSaiIdsIter->second;
    // VlanID(0) is the "no vlan" sentinel a port carries when its router
    // interface is bound to the port rather than to a vlan. SaiPortManager
    // indexes every port it adds, sentinel included, so this reset is what
    // actually puts the LAG on the routed path; the lookup above only misses
    // if the member port was never indexed at all.
    if (vlanID == VlanID(0)) {
      vlanID.reset();
    }
  }

  SaiLagTraits::CreateAttributes createAttributes{labelValue, vlanID};

  auto& lagStore = saiStore_->get<SaiLagTraits>();
  auto lag = lagStore.setObject(
      SaiLagTraits::Attributes::Label{labelValue}, createAttributes);
  LagSaiId lagSaiId = lag->adapterKey();
  std::map<PortSaiId, std::shared_ptr<SaiLagMember>> members;
  for (auto iter : folly::enumerate(aggregatePort->subportAndFwdState())) {
    auto [currentSubPort, fwdState] = *iter;
    auto member =
        addMember(lag, aggregatePort->getID(), currentSubPort, fwdState);
    members.emplace(std::move(member));
  }
  if (vlanID) {
    concurrentIndices_->vlanIds.emplace(
        PortDescriptorSaiId(lag->adapterKey()), *vlanID);
  }
  concurrentIndices_->aggregatePortIds.emplace(
      lag->adapterKey(), aggregatePort->getID());
  auto handle = std::make_unique<SaiLagHandle>();

  handle->members = std::move(members);
  handle->lag = std::move(lag);
  handle->minimumLinkCount = aggregatePort->getMinimumLinkCount();
  handle->vlanId = vlanID;
  handle->counters = std::make_unique<utility::HwTrunkCounters>(
      aggregatePort->getID(), aggregatePort->getName());
  handles_.emplace(aggregatePort->getID(), std::move(handle));
  if (vlanID) {
    managerTable_->vlanManager().createVlanMember(
        *vlanID, SaiPortDescriptor(aggregatePort->getID()), false, false);
  }
  if (aggregatePort->getTTLDisableDecrement().has_value()) {
    programTtlDecrement(*aggregatePort);
  }

  return lagSaiId;
}

void SaiLagManager::removeLag(
    const std::shared_ptr<AggregatePort>& aggregatePort) {
  if (aggregatePort->getAggregatePortType() ==
      cfg::AggregatePortType::HYPER_PORT) {
    return;
  }
  XLOG(DBG2) << "removing aggregate port : " << aggregatePort->getID();
  auto iter = handles_.find(aggregatePort->getID());
  if (iter == handles_.end()) {
    throw FbossError(
        "attempting to non-existing remove LAG ", aggregatePort->getID());
  }
  removeLagHandle(iter->first, iter->second.get());
  handles_.erase(iter->first);
}

void SaiLagManager::changeLag(
    const std::shared_ptr<AggregatePort>& oldAggregatePort,
    const std::shared_ptr<AggregatePort>& newAggregatePort) {
  if (newAggregatePort->getAggregatePortType() ==
      cfg::AggregatePortType::HYPER_PORT) {
    // TODO(daiuweix): update HYPER port through SaiPortManager
    return;
  }
  auto handleIter = handles_.find(oldAggregatePort->getID());
  CHECK(handleIter != handles_.end());
  auto& saiLagHandle = handleIter->second;

  saiLagHandle->minimumLinkCount = newAggregatePort->getMinimumLinkCount();

  auto oldPortAndFwdState = oldAggregatePort->subportAndFwdState();
  auto newPortAndFwdState = newAggregatePort->subportAndFwdState();
  auto oldIter = oldPortAndFwdState.begin();
  auto newIter = newPortAndFwdState.begin();

  while (oldIter != oldPortAndFwdState.end() &&
         newIter != newPortAndFwdState.end()) {
    if (oldIter->first < newIter->first) {
      removeMember(oldAggregatePort->getID(), oldIter->first);
      oldIter++;
    } else if (newIter->first < oldIter->first) {
      // add member
      auto member = addMember(
          saiLagHandle->lag, newAggregatePort->getID(), newIter->first);
      setMemberState(member.second.get(), newIter->second);
      saiLagHandle->members.emplace(std::move(member));
      newIter++;
    } else {
      if (oldIter->second != newIter->second) {
        // forwarding state changed
        auto member = getMember(saiLagHandle.get(), newIter->first);
        setMemberState(member, newIter->second);
      }
      newIter++;
      oldIter++;
    }
  }

  while (oldIter != oldPortAndFwdState.end()) {
    removeMember(oldAggregatePort->getID(), oldIter->first);
    oldIter++;
  }
  while (newIter != newPortAndFwdState.end()) {
    auto member =
        addMember(saiLagHandle->lag, newAggregatePort->getID(), newIter->first);
    setMemberState(member.second.get(), newIter->second);
    saiLagHandle->members.emplace(std::move(member));
    newIter++;
  }
  if (newAggregatePort->getName() != oldAggregatePort->getName()) {
    saiLagHandle->counters->reinitialize(
        newAggregatePort->getID(), newAggregatePort->getName());
  }
  if (newAggregatePort->getTTLDisableDecrement() !=
      oldAggregatePort->getTTLDisableDecrement()) {
    programTtlDecrement(*newAggregatePort);
  }
}

void SaiLagManager::programTtlDecrement(const AggregatePort& aggregatePort) {
  const bool disable = aggregatePort.getTTLDisableDecrement().value_or(false);
  using Attribute = SaiLagTraits::Attributes::DisableTtlDecrement;
  if (!platform_->getAsic()->isSupported(
          HwAsic::Feature::LAG_TTL_DECREMENT_DISABLE) ||
      !Attribute::optionalExtensionAttributeId()) {
    if (disable) {
      throw FbossError(
          "LAG TTL decrement control is unavailable on this ASIC/SDK");
    }
    return; // Explicit false is harmless on platforms without this feature.
  }
  // Disable TTL decrement on LAG ports.
  auto handle = getLagHandle(aggregatePort.getID());
  SaiApiTable::getInstance()->lagApi().setAttribute(
      handle->lag->adapterKey(), Attribute{disable});
}

std::pair<PortSaiId, std::shared_ptr<SaiLagMember>> SaiLagManager::addMember(
    const std::shared_ptr<SaiLag>& lag,
    AggregatePortID aggregatePortID,
    PortID subPort,
    AggregatePort::Forwarding fwdState) {
  auto portHandle = managerTable_->portManager().getPortHandle(subPort);
  CHECK(portHandle);
  portHandle->bridgePort.reset();
  auto saiPortId = portHandle->port->adapterKey();
  auto saiLagId = lag->adapterKey();

  SaiLagMemberTraits::AdapterHostKey adapterHostKey{saiLagId, saiPortId};
  auto egressDisabled = (fwdState == AggregatePort::Forwarding::ENABLED)
      ? SaiLagMemberTraits::Attributes::EgressDisable{false}
      : SaiLagMemberTraits::Attributes::EgressDisable{true};
  SaiLagMemberTraits::CreateAttributes attrs{
      saiLagId, saiPortId, egressDisabled};
  auto& lagMemberStore = saiStore_->get<SaiLagMemberTraits>();
  auto member = lagMemberStore.setObject(adapterHostKey, attrs);
  concurrentIndices_->memberPort2AggregatePortIds.emplace(
      saiPortId, aggregatePortID);
  return {saiPortId, member};
}

void SaiLagManager::removeMember(AggregatePortID aggPort, PortID subPort) {
  auto handlesIter = handles_.find(aggPort);
  CHECK(handlesIter != handles_.end());
  auto portHandle = managerTable_->portManager().getPortHandle(subPort);
  CHECK(portHandle);
  auto saiPortId = portHandle->port->adapterKey();
  auto membersIter = handlesIter->second->members.find(saiPortId);
  if (membersIter == handlesIter->second->members.end()) {
    // link down will remove lag member, resulting in LACP machine processing
    // lag shrink. this  will also cause LACP machine to issue state delta to
    // remove  lag member. so  ignore the lag member  removal which  could be
    // issued second time by sw switch.
    XLOG(DBG6) << "member " << subPort << " of aggregate port " << aggPort
               << " was already removed.";
    return;
  }
  membersIter->second.reset();
  handlesIter->second->members.erase(membersIter);
  concurrentIndices_->memberPort2AggregatePortIds.erase(saiPortId);
  // During rollback, all hw calls are skipped except creation will fail.
  // For lags that are already created, rollback will not remove any of
  // those in hardware. Lag and lag members will be reclaim when the
  // lastGoodState is applied. Therefore, there's no need to create bridge port
  // in this case.
  if (getHwWriteBehavior() != HwWriteBehavior::SKIP) {
    portHandle->bridgePort = managerTable_->bridgeManager().addBridgePort(
        SaiPortDescriptor(subPort),
        PortDescriptorSaiId(portHandle->port->adapterKey()));
  }
}

SaiLagHandle* FOLLY_NULLABLE
SaiLagManager::getLagHandleIf(AggregatePortID aggregatePortID) const {
  auto iter = handles_.find(aggregatePortID);
  if (iter == handles_.end()) {
    return nullptr;
  }
  return iter->second.get();
}

SaiLagHandle* SaiLagManager::getLagHandle(
    AggregatePortID aggregatePortID) const {
  auto* handle = getLagHandleIf(aggregatePortID);
  if (handle) {
    return handle;
  }
  throw FbossError("handle for aggregate port ", aggregatePortID, " not found");
}

bool SaiLagManager::isLagUp(const AggregatePortID& aggregatePortID) const {
  const auto* handle = getLagHandle(aggregatePortID);
  return handle->minimumLinkCount <= getActiveMemberCount(aggregatePortID);
}

void SaiLagManager::removeLagHandle(
    AggregatePortID aggPort,
    SaiLagHandle* handle) {
  // remove members
  while (!handle->members.empty()) {
    auto membersIter = handle->members.begin();
    auto portSaiId = membersIter->first;
    auto iter = concurrentIndices_->portSaiId2PortInfo.find(portSaiId);
    CHECK(iter != concurrentIndices_->portSaiId2PortInfo.end());
    removeMember(aggPort, iter->second.portID);
  }
  // remove bridge port
  handle->bridgePort.reset();
  // A routed LAG has neither vlan membership nor a vlan id index entry.
  if (handle->vlanId) {
    managerTable_->vlanManager().removeVlanMember(
        *handle->vlanId, SaiPortDescriptor(aggPort));
    concurrentIndices_->vlanIds.erase(
        PortDescriptorSaiId(handle->lag->adapterKey()));
  }
  concurrentIndices_->aggregatePortIds.erase(handle->lag->adapterKey());
  // remove lag
  handle->lag.reset();
}

SaiLagManager::~SaiLagManager() {
  for (auto& handlesIter : handles_) {
    auto& [aggPortID, handle] = handlesIter;
    removeLagHandle(aggPortID, handle.get());
  }
}

size_t SaiLagManager::getLagMemberCount(AggregatePortID aggPort) const {
  auto handle = getLagHandle(aggPort);
  return handle->members.size();
}

size_t SaiLagManager::getActiveMemberCount(AggregatePortID aggPort) const {
  const auto* handle = getLagHandle(aggPort);
  return std::count_if(
      std::begin(handle->members),
      std::end(handle->members),
      [](const auto& entry) {
        auto member = entry.second;
        auto attributes = member->attributes();
        auto isEgressDisabled =
            std::get<SaiLagMemberTraits::Attributes::EgressDisable>(attributes)
                .value();
        return !isEgressDisabled;
      });
}

void SaiLagManager::setMemberState(
    SaiLagMember* member,
    AggregatePort::Forwarding fwdState) {
  switch (fwdState) {
    case AggregatePort::Forwarding::DISABLED:
      member->setAttribute(SaiLagMemberTraits::Attributes::EgressDisable{true});
      break;
    case AggregatePort::Forwarding::ENABLED:
      member->setAttribute(
          SaiLagMemberTraits::Attributes::EgressDisable{false});
      break;
  }
}

SaiLagMember* SaiLagManager::getMember(SaiLagHandle* handle, PortID port) {
  auto portsIter = concurrentIndices_->portSaiIds.find(port);
  if (portsIter == concurrentIndices_->portSaiIds.end()) {
    throw FbossError("port sai id not found for lag member port ", port);
  }
  auto membersIter = handle->members.find(portsIter->second);
  if (membersIter == handle->members.end()) {
    throw FbossError("member not found for lag member port ", port);
  }
  return membersIter->second.get();
}

void SaiLagManager::disableMember(AggregatePortID aggPort, PortID subPort) {
  auto handleIter = handles_.find(aggPort);
  CHECK(handleIter != handles_.end());
  auto& saiLagHandle = handleIter->second;
  auto member = getMember(saiLagHandle.get(), subPort);
  setMemberState(member, AggregatePort::Forwarding::DISABLED);
}

bool SaiLagManager::isLagMember(PortID port) {
  auto handle = managerTable_->portManager().getPortHandle(port);
  if (!handle || !handle->port) {
    throw FbossError("lag membership checked for unknown port ", port);
  }
  return concurrentIndices_->memberPort2AggregatePortIds.find(
             handle->port->adapterKey()) !=
      concurrentIndices_->memberPort2AggregatePortIds.end();
}

void SaiLagManager::addBridgePort(
    const std::shared_ptr<AggregatePort>& aggPort) {
  auto handle = getLagHandle(aggPort->getID());
  if (!handle->vlanId) {
    // A routed LAG does no l2 forwarding, so it gets no bridge port.
    return;
  }
  auto& lag = handle->lag;
  handle->bridgePort = managerTable_->bridgeManager().addBridgePort(
      SaiPortDescriptor(aggPort->getID()),
      PortDescriptorSaiId(lag->adapterKey()));
}

void SaiLagManager::changeBridgePort(
    const std::shared_ptr<AggregatePort>& /*oldPort*/,
    const std::shared_ptr<AggregatePort>& newPort) {
  return addBridgePort(newPort);
}

void SaiLagManager::updateStats(AggregatePortID aggPort) {
  HwTrunkStats stats{};
  utility::clearHwTrunkStats(stats);
  *stats.capacity_() = 0;

  std::optional<std::chrono::seconds> timeRetrieved{};

  auto* handle = getLagHandle(aggPort);
  for (auto member : handle->members) {
    auto portSaiId = member.first;
    auto portIdsIter = concurrentIndices_->portSaiId2PortInfo.find(portSaiId);
    if (portIdsIter == concurrentIndices_->portSaiId2PortInfo.end()) {
      continue;
    }
    auto fb303Stats = managerTable_->portManager().getLastPortStat(
        portIdsIter->second.portID);
    if (!fb303Stats) {
      continue;
    }
    utility::accumulateHwTrunkMemberStats(stats, fb303Stats->portStats());
    auto attributes = member.second->attributes();
    auto isEgressDisabled =
        std::get<SaiLagMemberTraits::Attributes::EgressDisable>(attributes)
            .value();
    if (!isEgressDisabled) {
      auto portSpeed =
          managerTable_->portManager().getSpeed(portIdsIter->second.portID);
      *stats.capacity_() += static_cast<int64_t>(portSpeed);
    }
    if (!timeRetrieved) {
      timeRetrieved = fb303Stats->timeRetrieved();
    }
  }
  if (timeRetrieved) {
    // at least one member exists in lag
    handle->counters->updateCounters(timeRetrieved.value(), stats);
  }
}

std::optional<HwTrunkStats> SaiLagManager::getHwTrunkStats(
    AggregatePortID aggPort) const {
  auto* handle = getLagHandleIf(aggPort);
  if (!handle) {
    return std::nullopt;
  }
  return handle->counters->getHwTrunkStats();
}

std::map<std::string, HwTrunkStats> SaiLagManager::getAllHwTrunkStats() const {
  std::map<std::string, HwTrunkStats> trunkStats;
  for (const auto& [aggPortId, handle] : handles_) {
    if (handle->counters) {
      trunkStats.emplace(
          handle->counters->trunkName(), handle->counters->getHwTrunkStats());
    }
  }
  return trunkStats;
}
} // namespace facebook::fboss
