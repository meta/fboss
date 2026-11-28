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

#include "fboss/agent/hw/sai/api/SystemPortApi.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/switch/SaiQosMapManager.h"
#include "fboss/agent/hw/sai/switch/SaiQueueManager.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SystemPort.h"
#include "fboss/agent/types.h"

#include "folly/container/F14Map.h"
#include "folly/container/F14Set.h"

namespace facebook::fboss {

class SaiManagerTable;
class SaiPlatform;
class SaiStore;
struct ConcurrentIndices;
class HwSysPortFb303Stats;

using SaiSystemPort = SaiObject<SaiSystemPortTraits>;

struct SaiSystemPortHandle {
  std::shared_ptr<SaiSystemPort> systemPort;
  std::shared_ptr<SaiQosMap> tcToQueueQosMap;
  std::optional<std::string> qosPolicy;
  SaiQueueHandles queues;
  std::vector<SaiQueueHandle*> configuredQueues;
  bool qosMapsSupported{};

  void resetQueues();
};

class SaiSystemPortManager {
  using Handles =
      folly::F14FastMap<SystemPortID, std::unique_ptr<SaiSystemPortHandle>>;
  using Stats =
      folly::F14FastMap<SystemPortID, std::unique_ptr<HwSysPortFb303Stats>>;

 public:
  SaiSystemPortManager(
      SaiStore* saiStore,
      SaiManagerTable* managerTable,
      SaiPlatform* platform,
      ConcurrentIndices* concurrentIndices);
  ~SaiSystemPortManager();
  void resetQueues();
  SystemPortSaiId addSystemPort(
      const std::shared_ptr<SystemPort>& swSystemPort);
  void removeSystemPort(const std::shared_ptr<SystemPort>& swSystemPort);
  void changeSystemPort(
      const std::shared_ptr<SystemPort>& oldSystemPort,
      const std::shared_ptr<SystemPort>& newSystemPort);
  std::shared_ptr<SystemPortMap> constructSystemPorts(
      const std::shared_ptr<MultiSwitchPortMap>& ports,
      const std::map<int64_t, cfg::SwitchInfo>& switchIdToSwitchInfo,
      int64_t switchId);
  SystemPortSaiId addFabricLinkMonitoringSystemPort(
      const std::shared_ptr<SystemPort>& swSystemPort,
      const std::shared_ptr<SwitchState>& state);
  void changeFabricLinkMonitoringSystemPort(
      const std::shared_ptr<SystemPort>& oldSystemPort,
      const std::shared_ptr<SystemPort>& newSystemPort);
  void removeFabricLinkMonitoringSystemPort(
      const std::shared_ptr<SystemPort>& swSystemPort,
      const std::shared_ptr<SwitchState>& state);

  const Stats& getLastPortStats() const {
    return portStats_;
  }
  const HwSysPortFb303Stats* getLastPortStats(SystemPortID port) const;

  const SaiSystemPortHandle* getSystemPortHandle(SystemPortID swId) const {
    return getSystemPortHandleImpl(swId);
  }
  SaiSystemPortHandle* getSystemPortHandle(SystemPortID swId) {
    return getSystemPortHandleImpl(swId);
  }
  Handles::const_iterator begin() const {
    return handles_.begin();
  }
  Handles::const_iterator end() const {
    return handles_.end();
  }
  void
  updateStats(SystemPortID portId, bool updateWatermarks, bool updateVoqStats);

  void setQosPolicy(
      SystemPortID portId,
      const std::optional<std::string>& qosPolicy);
  void setQosPolicy(const std::shared_ptr<QosPolicy>& qosPolicy);
  void clearQosPolicy(SystemPortID portId);
  void clearQosPolicy(const std::shared_ptr<QosPolicy>& qosPolicy);
  void changeQosPolicy(
      const std::shared_ptr<SystemPort>& oldSystemPort,
      const std::shared_ptr<SystemPort>& newSystemPort);

  void resetQosMaps();
  void addSystemPortShelPktDstEnable(
      const std::shared_ptr<SystemPort>& swSystemPort) const;
  void changeSystemPortShelPktDstEnable(
      const std::shared_ptr<SystemPort>& oldSystemPort,
      const std::shared_ptr<SystemPort>& newSystemPort) const;
  void setFabricLinkMonitoringSystemPortOffset(std::optional<int32_t> offset) {
    fabricLinkMonitoringSystemPortOffset_ = offset;
  }

 private:
  PortID getFabricLinkMonitoringPortId(
      const std::shared_ptr<SystemPort>& swSystemPort,
      const std::shared_ptr<SwitchState>& state) const;
  void loadQueues(
      SaiSystemPortHandle& sysPortHandle,
      const std::shared_ptr<SystemPort>& swSystemPort);
  std::optional<std::pair<uint32_t, uint32_t>> coreAndPortIndexFromPort(
      const std::shared_ptr<SystemPort>& swSystemPort) const;
  SaiSystemPortTraits::CreateAttributes attributesFromSwSystemPort(
      const std::shared_ptr<SystemPort>& swSystemPort) const;
  SaiQueueHandle* FOLLY_NULLABLE
  getQueueHandle(SystemPortID swId, const SaiQueueConfig& saiQueueConfig) const;
  void changeQueue(
      const std::shared_ptr<SystemPort>& systemPort,
      const QueueConfig& oldQueueConfig,
      const QueueConfig& newQueueConfig);
  void configureQueues(
      const std::shared_ptr<SystemPort>& systemPort,
      SaiSystemPortHandle* sysPortHandle,
      const QueueConfig& newQueueConfig);

  SaiSystemPortHandle* getSystemPortHandleImpl(SystemPortID swId) const;
  void setQosMapOnSystemPort(
      const SaiQosMapHandle* qosMapHandle,
      SystemPortID swId);
  SaiStore* saiStore_;
  SaiManagerTable* managerTable_;
  SaiPlatform* platform_;
  Handles handles_;
  ConcurrentIndices* concurrentIndices_;
  Stats portStats_;
  bool tcToQueueMapAllowedOnSystemPort_;
  // The offset from PortID for system ports in fabric link monitoring. Used
  // only on the legacy (non-relocated) path; see getFabricLinkMonitoringPortId.
  std::optional<int32_t> fabricLinkMonitoringSystemPortOffset_;
};

} // namespace facebook::fboss
