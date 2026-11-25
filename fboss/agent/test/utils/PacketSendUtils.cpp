/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/test/utils/PacketSendUtils.h"

#include "fboss/agent/TxPacket.h"
#include "fboss/agent/test/TestEnsembleIf.h"

#include <folly/logging/xlog.h>

#include <string>

namespace facebook::fboss::utility {

namespace {

template <typename ConditionFN, typename PortT, typename StatsGetFn>
bool waitPortStatsConditionImpl(
    const ConditionFN& conditionFn,
    const std::vector<PortT>& portIds,
    uint32_t retries,
    std::chrono::duration<uint32_t, std::milli> msBetweenRetry,
    const StatsGetFn& getHwPortStats) {
  auto newPortStats = getHwPortStats(portIds);
  while (retries--) {
    // TODO(borisb): exponential backoff!
    if (conditionFn(newPortStats)) {
      return true;
    }
    std::this_thread::sleep_for(msBetweenRetry);
    newPortStats = getHwPortStats(portIds);
  }
  XLOG(DBG3) << "Awaited port stats condition was never satisfied";
  return false;
}

template <typename ConditionFn, typename UpdateStatsFn>
bool waitStatsConditionImpl(
    const ConditionFn& conditionFn,
    const UpdateStatsFn& updateStatsFn,
    uint32_t retries,
    const std::chrono::duration<uint32_t, std::milli>& msBetweenRetry) {
  while (retries--) {
    updateStatsFn();
    // TODO: exponential backoff!
    if (conditionFn()) {
      return true;
    }
    std::this_thread::sleep_for(msBetweenRetry);
  }
  XLOG(DBG3) << "Awaited stats condition was never satisfied!";
  return false;
}

template <typename PortStatT>
bool anyQueueBytesIncremented(
    const PortStatT& origPortStat,
    const PortStatT& newPortStat) {
  return std::any_of(
      origPortStat.queueOutBytes_()->begin(),
      origPortStat.queueOutBytes_()->end(),
      [&newPortStat](auto queueAndBytes) {
        auto [qid, oldQbytes] = queueAndBytes;
        const auto& newQueueStats = newPortStat.queueOutBytes_();
        return newQueueStats->find(qid)->second > oldQbytes;
      });
}

bool featureSupported(TestEnsembleIf* ensemble, HwAsic::Feature feature) {
  return ensemble->getHwAsicTable()->isFeatureSupportedOnAnyAsic(feature);
}

bool waitForAnyPorAndQueutOutBytesIncrement(
    TestEnsembleIf* ensemble,
    const std::map<PortID, HwPortStats>& originalPortStats,
    const std::vector<PortID>& portIds,
    const HwPortStatsFunc& getHwPortStats,
    const int msBetweenRetry) {
  bool queueStatsSupported =
      featureSupported(ensemble, HwAsic::Feature::L3_QOS);
  auto conditionFn = [&originalPortStats,
                      queueStatsSupported](const auto& newPortStats) {
    for (const auto& [portId, portStat] : originalPortStats) {
      auto newPortStatItr = newPortStats.find(portId);
      if (newPortStatItr != newPortStats.end()) {
        if (*newPortStatItr->second.outBytes_() > portStat.outBytes_()) {
          // Wait for queue stat increment if queues are supported
          // on this platform
          if (!queueStatsSupported ||
              anyQueueBytesIncremented(portStat, newPortStatItr->second)) {
            return true;
          }
        }
      }
    }
    XLOG(DBG3) << "No port stats increased yet";
    return false;
  };
  return waitPortStatsCondition(
      conditionFn,
      portIds,
      20,
      std::chrono::milliseconds(msBetweenRetry),
      getHwPortStats);
}

bool waitForAnyVoQOutBytesIncrement(
    TestEnsembleIf* ensemble,
    const std::map<SystemPortID, HwSysPortStats>& originalPortStats,
    const std::vector<SystemPortID>& portIds,
    const HwSysPortStatsFunc& getHwPortStats,
    const int msBetweenRetry) {
  if (!featureSupported(ensemble, HwAsic::Feature::VOQ)) {
    throw FbossError("VOQs are unsupported on platform");
  }
  auto conditionFn = [&originalPortStats](const auto& newPortStats) {
    for (const auto& [portId, portStat] : originalPortStats) {
      auto newPortStatItr = newPortStats.find(portId);
      if (newPortStatItr != newPortStats.end() &&
          anyQueueBytesIncremented(portStat, newPortStatItr->second)) {
        return true;
      }
    }
    XLOG(DBG3) << "No port stats increased yet";
    return false;
  };
  return waitSysPortStatsCondition(
      conditionFn,
      portIds,
      20,
      std::chrono::milliseconds(msBetweenRetry),
      getHwPortStats);
}
} // namespace

bool waitPortStatsCondition(
    std::function<bool(const std::map<PortID, HwPortStats>&)> conditionFn,
    const std::vector<PortID>& portIds,
    uint32_t retries,
    std::chrono::duration<uint32_t, std::milli> msBetweenRetry,
    const HwPortStatsFunc& getHwPortStats) {
  return waitPortStatsConditionImpl(
      conditionFn, portIds, retries, msBetweenRetry, getHwPortStats);
}

bool waitSysPortStatsCondition(
    std::function<bool(const std::map<SystemPortID, HwSysPortStats>&)>
        conditionFn,
    const std::vector<SystemPortID>& portIds,
    uint32_t retries,
    std::chrono::duration<uint32_t, std::milli> msBetweenRetry,
    const HwSysPortStatsFunc& getHwPortStats) {
  return waitPortStatsConditionImpl(
      conditionFn, portIds, retries, msBetweenRetry, getHwPortStats);
}

bool waitStatsCondition(
    const std::function<bool()>& conditionFn,
    const std::function<void()>& updateStatsFn,
    uint32_t retries,
    const std::chrono::duration<uint32_t, std::milli>& msBetweenRetry) {
  return waitStatsConditionImpl(
      conditionFn, updateStatsFn, retries, msBetweenRetry);
}

bool ensureSendPacketSwitched(
    TestEnsembleIf* ensemble,
    std::unique_ptr<TxPacket> pkt,
    const std::vector<PortID>& portIds,
    const HwPortStatsFunc& getHwPortStats,
    const std::vector<SystemPortID>& sysPortIds,
    const HwSysPortStatsFunc& getHwSysPortStats,
    const int msBetweenRetry,
    const std::optional<SwitchID>& switchId) {
  auto originalPortStats = getHwPortStats(portIds);
  auto originalSysPortStats = getHwSysPortStats(sysPortIds);
  ensemble->sendPacketSwitchedAsync(std::move(pkt), switchId);
  bool waitForVoqs =
      sysPortIds.size() && featureSupported(ensemble, HwAsic::Feature::VOQ);
  return waitForAnyPorAndQueutOutBytesIncrement(
             ensemble,
             originalPortStats,
             portIds,
             getHwPortStats,
             msBetweenRetry) &&
      (!waitForVoqs ||
       waitForAnyVoQOutBytesIncrement(
           ensemble,
           originalSysPortStats,
           sysPortIds,
           getHwSysPortStats,
           msBetweenRetry));
}

bool ensureSendPacketSwitched(
    TestEnsembleIf* ensemble,
    std::unique_ptr<TxPacket> pkt,
    const std::vector<PortID>& portIds,
    const HwPortStatsFunc& getHwPortStats,
    const int msBetweenRetry,
    const std::optional<SwitchID>& switchId) {
  auto noopGetSysPortStats = [](const std::vector<SystemPortID>&)
      -> std::map<SystemPortID, HwSysPortStats> { return {}; };
  return ensureSendPacketSwitched(
      ensemble,
      std::move(pkt),
      portIds,
      getHwPortStats,
      {},
      noopGetSysPortStats,
      msBetweenRetry,
      switchId);
}

bool ensureSendPacketOutOfPort(
    TestEnsembleIf* ensemble,
    std::unique_ptr<TxPacket> pkt,
    PortID portID,
    const std::vector<PortID>& ports,
    const HwPortStatsFunc& getHwPortStats,
    std::optional<uint8_t> queue,
    const int msBetweenRetry) {
  auto originalPortStats = getHwPortStats(ports);
  ensemble->sendPacketAsync(std::move(pkt), PortDescriptor(portID), queue);
  return waitForAnyPorAndQueutOutBytesIncrement(
      ensemble, originalPortStats, ports, getHwPortStats, msBetweenRetry);
}

} // namespace facebook::fboss::utility
