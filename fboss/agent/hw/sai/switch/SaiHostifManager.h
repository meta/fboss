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

#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"

#include "fboss/agent/hw/HwCpuFb303Stats.h"
#include "fboss/agent/hw/HwSysPortFb303Stats.h"
#include "fboss/agent/hw/sai/api/CounterApi.h"
#include "fboss/agent/hw/sai/api/HostifApi.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/state/ControlPlane.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/types.h"
#include "fboss/lib/RefMap.h"

#include "folly/container/F14Map.h"

#include <memory>
#include <mutex>

namespace facebook::fboss {

struct ConcurrentIndices;
class SaiManagerTable;
class SaiPlatform;
class SaiStore;

using SaiHostifTrapGroup = SaiObject<SaiHostifTrapGroupTraits>;
using SaiHostifTrap = SaiObject<SaiHostifTrapTraits>;
using SaiHostifTrapCounter = SaiObject<SaiCounterTraits>;
using SaiHostifUserDefinedTrap = SaiObject<SaiHostifUserDefinedTrapTraits>;

struct SaiCpuPortHandle {
  PortSaiId cpuPortId;
  std::optional<SystemPortSaiId> cpuSystemPortId;
  SaiQueueHandles queues;
  std::vector<SaiQueueHandle*> configuredQueues;
  SaiQueueHandles voqs;
  std::vector<SaiQueueHandle*> configuredVoqs;
};

struct SaiHostifTrapHandle {
  std::shared_ptr<SaiHostifTrapGroup> trapGroup;
  std::shared_ptr<SaiHostifTrap> trap;
  std::shared_ptr<SaiHostifTrapCounter> counter;
};

struct SaiHostifUserDefinedTrapHandle {
  std::shared_ptr<SaiHostifTrapGroup> trapGroup;
  std::shared_ptr<SaiHostifUserDefinedTrap> trap;
};

class SaiHostifManager {
 public:
  explicit SaiHostifManager(
      SaiStore* saiStore,
      SaiManagerTable* managerTable,
      const SaiPlatform* platform,
      ConcurrentIndices* concurrentIndices);
  ~SaiHostifManager();
  SaiHostifManager(const SaiHostifManager&) = delete;
  SaiHostifManager& operator=(const SaiHostifManager&) = delete;
  SaiHostifManager(SaiHostifManager&&) = delete;
  SaiHostifManager& operator=(SaiHostifManager&&) = delete;
  std::optional<HostifTrapSaiId> addHostifTrap(
      cfg::PacketRxReason trapId,
      uint32_t queueId,
      uint16_t priority);
  void removeHostifTrap(cfg::PacketRxReason trapId);
  void changeHostifTrap(
      cfg::PacketRxReason trapId,
      uint32_t queueId,
      uint16_t priority);
  static std::optional<std::pair<sai_int32_t, sai_packet_action_t>>
  packetReasonToHostifTrap(
      cfg::PacketRxReason reason,
      const SaiPlatform* platform);
  static std::optional<SaiHostifTrapTraits::CreateAttributes>
  makeHostifTrapAttributes(
      cfg::PacketRxReason trapId,
      HostifTrapGroupSaiId trapGroupId,
      uint16_t priority,
      const SaiPlatform* platform);
  static SaiHostifUserDefinedTrapTraits::CreateAttributes
  makeHostifUserDefinedTrapAttributes(
      HostifTrapGroupSaiId trapGroupId,
      std::optional<uint16_t> priority,
      std::optional<uint16_t> trapType);
  std::shared_ptr<SaiHostifUserDefinedTrapHandle> ensureHostifUserDefinedTrap(
      uint32_t queueId);
  void processHostifDelta(const ThriftMapDelta<MultiControlPlane>& delta);
  SaiQueueHandle* getQueueHandle(const SaiQueueConfig& saiQueueConfig);
  const SaiQueueHandle* getQueueHandle(
      const SaiQueueConfig& saiQueueConfig) const;
  SaiQueueHandle* getVoqHandle(const SaiQueueConfig& saiQueueConfig);
  const SaiQueueHandle* getVoqHandle(
      const SaiQueueConfig& saiQueueConfig) const;
  void updateStats(bool updateWatermarks = false);
  HwPortStats getCpuPortStats() const;
  QueueConfig getQueueSettings() const;
  QueueConfig getVoqSettings() const;
  const HwCpuFb303Stats& getCpuFb303Stats() const {
    return cpuStats_;
  }
  const HwSysPortFb303Stats& getCpuSysPortFb303Stats() const {
    return cpuSysPortStats_;
  }
  const SaiHostifTrapHandle* getHostifTrapHandle(
      cfg::PacketRxReason rxReason) const;
  SaiHostifTrapHandle* getHostifTrapHandle(cfg::PacketRxReason rxReason);
  std::shared_ptr<SaiHostifTrapCounter> createHostifTrapCounter(
      cfg::PacketRxReason rxReason);
  void qosPolicyUpdated(const std::string& qosPolicy);

 private:
  uint32_t getMaxCpuQueues() const;
  void setCpuQosPolicy(const std::optional<std::string>& qosPolicy);
  void clearQosPolicy();
  void setCpuPortQosPolicy(QosMapSaiId dscpToTc, QosMapSaiId tcToQueue);
#if defined(SAI_CBFC_SUPPORTED)
  void setCpuPortTcToVcMap(const std::shared_ptr<SaiQosMap>& tcToVc);
#endif
  void setCpuSystemPortQosPolicy(QosMapSaiId tcToQueue);
  std::shared_ptr<SaiHostifTrapGroup> ensureHostifTrapGroup(uint32_t queueId);
  void processQueueDelta(const DeltaValue<ControlPlane>& delta);
  void processVoqDelta(const DeltaValue<ControlPlane>& delta);
  void processRxReasonToQueueDelta(const DeltaValue<ControlPlane>& delta);
  void processQosDelta(const DeltaValue<ControlPlane>& delta);

  void loadCpuPort();
  void loadCpuPortQueues();
  void loadCpuSystemPortVoqs();
  void changeCpuQueue(
      const ControlPlane::PortQueues& oldQueueConfig,
      const ControlPlane::PortQueues& newQueueConfig);
  void changeCpuVoq(
      const ControlPlane::PortQueues& oldQueueConfig,
      const ControlPlane::PortQueues& newQueueConfig);
  SaiQueueHandle* getQueueHandleImpl(
      const SaiQueueConfig& saiQueueConfig) const;
  SaiQueueHandle* getVoqHandleImpl(const SaiQueueConfig& saiQueueConfig) const;
  SaiHostifTrapHandle* getHostifTrapHandleImpl(
      cfg::PacketRxReason rxReason) const;
  void publishCpuQueueWatermark(int cosq, uint64_t peakBytes) const;
  void processHostifEntryDelta(const DeltaValue<ControlPlane>& delta);

  SaiStore* saiStore_;
  SaiManagerTable* managerTable_;
  const SaiPlatform* platform_;
  ConcurrentIndices* concurrentIndices_;
  folly::F14FastMap<cfg::PacketRxReason, std::unique_ptr<SaiHostifTrapHandle>>
      handles_;

  // single QoS policy applied accross the front panel ports and cpu port
  // on all platforms except for J3 right now.
  std::shared_ptr<SaiQosMap> dscpToTcQosMap_;
  std::shared_ptr<SaiQosMap> tcToQueueQosMap_;
  std::shared_ptr<SaiQosMap> tcToVoqMap_;
#if defined(SAI_CBFC_SUPPORTED)
  std::shared_ptr<SaiQosMap> tcToVcQosMap_;
#endif
  std::optional<std::string> qosPolicy_;
  std::unique_ptr<SaiCpuPortHandle> cpuPortHandle_;
  HwCpuFb303Stats cpuStats_;
  HwSysPortFb303Stats cpuSysPortStats_;
  HwRxReasonStats rxReasonStats_;
};

} // namespace facebook::fboss
