/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/bcm/BcmCosQueueManager.h"

#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include "fboss/agent/hw/CounterUtils.h"
#include "fboss/agent/hw/bcm/BcmCosQueueFBConvertors.h"
#include "fboss/agent/hw/bcm/BcmCosQueueManagerUtils.h"
#include "fboss/agent/hw/bcm/BcmEgressQueueFlexCounter.h"
#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmPlatform.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"

extern "C" {

#if (defined(IS_OPENNSA))
// TODO (skhare) refactor and replace with right OpenNSA API
#define NUM_COS(unit) 8
#else
#include <soc/drv.h>
#endif

#include <bcm/cosq.h>
}

namespace {
using namespace facebook::fboss;
// the corresponding bcm_cosq_control_t for uc and mc queues
using CosControlTypeAndDescMap =
    std::map<cfg::StreamType, std::pair<bcm_cosq_control_t, std::string>>;
static const std::map<BcmCosQueueControlType, CosControlTypeAndDescMap>
    kCosqControlTypeAndDescMap = {
        {BcmCosQueueControlType::ALPHA,
         {{cfg::StreamType::UNICAST,
           std::make_pair(bcmCosqControlDropLimitAlpha, "uc alpha")},
          {cfg::StreamType::MULTICAST,
           std::make_pair(bcmCosqControlDropLimitAlpha, "mc alpa")}}},
        {BcmCosQueueControlType::RESERVED_BYTES,
         {{cfg::StreamType::UNICAST,
           std::make_pair(
               bcmCosqControlEgressUCQueueMinLimitBytes,
               "uc reserved bytes")},
          {cfg::StreamType::MULTICAST,
           std::make_pair(
               bcmCosqControlEgressMCQueueMinLimitBytes,
               "mc reserved bytes")}}},
        {BcmCosQueueControlType::SHARED_BYTES,
         {{cfg::StreamType::UNICAST,
           std::make_pair(
               bcmCosqControlEgressUCQueueSharedLimitBytes,
               "uc shared bytes")},
          {cfg::StreamType::MULTICAST,
           std::make_pair(
               bcmCosqControlEgressMCQueueSharedLimitBytes,
               "mc shared bytes")}}}};

int cosqGportTraverseCallback(
    int unit,
    bcm_gport_t gport,
    int /*numq*/,
    uint32 flags,
    bcm_gport_t queueGport,
    void* userData) {
  auto cosQueueManager = static_cast<BcmCosQueueManager*>(userData);
  if (gport != cosQueueManager->getPortGport() &&
      !(BCM_GPORT_MODPORT_PORT_GET(gport) == 0 &&
        cosQueueManager->getPortGport() == BCM_GPORT_LOCAL_CPU)) {
    // no-op
    return 0l;
  }
  CosQueueGports* info = cosQueueManager->getCosQueueGports();
  if (flags & BCM_COSQ_GPORT_SCHEDULER) {
    info->scheduler = queueGport;
  } else if (flags & BCM_COSQ_GPORT_UCAST_QUEUE_GROUP) {
    bcm_cos_queue_t cosQ = BCM_GPORT_UCAST_QUEUE_GROUP_QID_GET(queueGport);
    // In Tomahawk, there're two more ucast queues created for each port but
    // they are not satisfied BCM_COSQ_QUEUE_VALID check. While in all the other
    // platforms, like TH3 and TH4, this callback returns only valid ucast
    // queues.
    auto asicType = cosQueueManager->getBcmSwitch()
                        ->getPlatform()
                        ->getAsic()
                        ->getAsicType();
    if ((asicType == cfg::AsicType::ASIC_TYPE_TOMAHAWK ||
         asicType == cfg::AsicType::ASIC_TYPE_FAKE) &&
        !BCM_COSQ_QUEUE_VALID(unit, cosQ)) {
      // skip this queue
    } else {
      info->unicast[cosQ] = queueGport;
    }
  } else if (flags & BCM_COSQ_GPORT_MCAST_QUEUE_GROUP) {
    bcm_cos_queue_t cosQ = BCM_GPORT_MCAST_QUEUE_GROUP_QID_GET(queueGport);
    info->multicast[cosQ] = queueGport;
  }
  return 0l;
};

int64_t getCumulativeCounter(
    facebook::fb303::ExportedStatMapImpl* statsMap,
    const std::string& statName) {
  auto statPtr = statsMap->getStatPtrNoExport(statName);
  auto lockedStatPtr = statPtr->wlock();
  lockedStatPtr->update(
      std::chrono::seconds(facebook::fb303::get_legacy_stats_time()));
  auto numLevels = lockedStatPtr->numLevels();
  // Cumulative (ALLTIME) counters are at (numLevels - 1)
  return lockedStatPtr->sum(numLevels - 1);
}

} // unnamed namespace

namespace facebook::fboss {

BcmCosQueueManager::BcmCosQueueManager(
    BcmSwitch* hw,
    const std::string& portName,
    bcm_gport_t portGport)
    : hw_(hw), portName_(portName), portGport_(portGport) {
  if (hw_->getPlatform()->getAsic()->isSupported(HwAsic::Feature::L3_QOS)) {
    // Make sure we get all the queue gports from hardware for portGport
    getCosQueueGportsFromHw();
  }
}

uint64_t BcmCosQueueManager::getQueueStatValue(
    int unit,
    bcm_gport_t queueGport,
    bcm_cos_queue_t queueID,
    const std::string& portName,
    int queueNum,
    const BcmCosQueueCounterType& type,
    const BcmEgressQueueTrafficCounterStats& flexCtrStats) {
  // If no flex counter stat is found, use the traditional way
  if (flexCtrStats.empty() ||
      !BcmEgressQueueFlexCounter::isSupported(type.statType)) {
    auto bcmStatType = utility::getBcmCosqStatType(
        type.statType, hw_->getPlatform()->getAsic()->getAsicType());
    uint64_t value;
    auto rv = bcm_cosq_stat_get(unit, queueGport, queueID, bcmStatType, &value);
    bcmCheckError(
        rv,
        "Unable to get cosq stat ",
        bcmStatType,
        " for ",
        portName,
        ", queue=",
        queueNum);
    return value;
  }

  // Check whether flexCtrStats has such counter
  if (auto streamTypeStatsItr = flexCtrStats.find(type.streamType);
      streamTypeStatsItr != flexCtrStats.end()) {
    if (auto queueStatsItr = streamTypeStatsItr->second.find(queueNum);
        queueStatsItr != streamTypeStatsItr->second.end()) {
      if (auto statItr = queueStatsItr->second.find(type.getCounterType());
          statItr != queueStatsItr->second.end()) {
        return statItr->second;
      } else {
        throw facebook::fboss::FbossError(
            "Failed to get Queue FlexCounter stat for port:",
            portName,
            ", queue:",
            queueNum,
            " because of unsupported counter type:",
            apache::thrift::util::enumNameSafe(type.getCounterType()));
      }
    } else {
      throw facebook::fboss::FbossError(
          "Failed to get Queue FlexCounter stat for port:",
          portName,
          ", queue:",
          queueNum,
          " because the queue isn't in BcmEgressQueueTrafficCounterStats");
    }
  } else {
    throw facebook::fboss::FbossError(
        "Failed to get Queue FlexCounter stat for port:",
        portName,
        ", queue:",
        queueNum,
        " because of unsupported stream type:",
        apache::thrift::util::enumNameSafe(type.streamType));
  }
}

void BcmCosQueueManager::fillOrReplaceCounter(
    const BcmCosQueueCounterType& type,
    QueueStatCounters& counters,
    const std::optional<QueueConfig>& queueConfig) {
  if (type.isScopeQueues()) {
    // TODO(joseph5wu) Right now, we create counters based on queue size, we
    // could have created counters only for the queues we set in the config.
    for (int queue = 0; queue < getNumQueues(type.streamType); queue++) {
      auto countersItr = counters.queues.find(queue);
      /*
       * if a queue name is specified in the config, use it in the statName,
       * else just use queue ID.
       */
      std::string name;

      if (queueConfig.has_value() && (queueConfig.value().size() > queue) &&
          queueConfig.value().at(queue)->getName().has_value()) {
        name = folly::to<std::string>(
            portName_,
            ".queue",
            queue,
            ".",
            queueConfig.value().at(queue)->getName().value(),
            ".",
            type.name);
      } else {
        name =
            folly::to<std::string>(portName_, ".queue", queue, ".", type.name);
      }

      // if counter already exists, delete it
      if (countersItr != counters.queues.end()) {
        auto oldName = countersItr->second->getName();
        utility::deleteCounter(oldName);
        counters.queues.erase(countersItr);
      }

      counters.queues.emplace(
          queue,
          std::make_unique<facebook::stats::MonotonicCounter>(
              name, fb303::SUM, fb303::RATE));

      // Prepare pre-allocate Queue FlexCounter stats so that we can avoid
      // allocating memory frequently when the getStats() function is called.
      if (hw_->getPlatform()->getAsic()->isSupported(
              HwAsic::Feature::EGRESS_QUEUE_FLEX_COUNTER) &&
          BcmEgressQueueFlexCounter::isSupported(type.statType)) {
        auto queueFlexCounterStatsLock = queueFlexCounterStats_.wlock();
        if (queueFlexCounterStatsLock->find(type.streamType) ==
            queueFlexCounterStatsLock->end()) {
          BcmTrafficCounterStats trafficCounterStats;
          trafficCounterStats.emplace(type.getCounterType(), 0);
          std::unordered_map<int, BcmTrafficCounterStats> queueStatsMap;
          queueStatsMap.emplace(queue, std::move(trafficCounterStats));
          queueFlexCounterStatsLock->emplace(
              type.streamType, std::move(queueStatsMap));
        } else {
          auto& queueStats = queueFlexCounterStatsLock->at(type.streamType);
          if (queueStats.find(queue) == queueStats.end()) {
            BcmTrafficCounterStats trafficCounterStats;
            trafficCounterStats.emplace(type.getCounterType(), 0);
            queueStats.emplace(queue, std::move(trafficCounterStats));
          } else {
            queueStats[queue].emplace(type.getCounterType(), 0);
          }
        }
      }
    }
  }
  if (type.isScopeAggregated()) {
    counters.aggregated = std::make_unique<facebook::stats::MonotonicCounter>(
        folly::to<std::string>(portName_, ".", type.name),
        fb303::SUM,
        fb303::RATE);
  }
}

void BcmCosQueueManager::setupQueueCounter(
    const BcmCosQueueCounterType& type,
    const std::optional<QueueConfig>& queueConfig) {
  // create counters for each type for each queue
  auto curCountersItr = queueCounters_.find(type);
  if (curCountersItr != queueCounters_.end()) {
    auto& counters = curCountersItr->second;
    fillOrReplaceCounter(type, counters, queueConfig);
  } else {
    QueueStatCounters counters;
    fillOrReplaceCounter(type, counters, queueConfig);
    queueCounters_.emplace(type, std::move(counters));
  }
}

void BcmCosQueueManager::setupQueueCounters(
    const std::optional<QueueConfig>& queueConfig) {
  for (const auto& type : getQueueCounterTypes()) {
    setupQueueCounter(type, queueConfig);
  }
}

void BcmCosQueueManager::destroyQueueCounters() {
  std::map<BcmCosQueueCounterType, QueueStatCounters> swapTo;
  queueCounters_.swap(swapTo);

  for (const auto& type : getQueueCounterTypes()) {
    auto it = swapTo.find(type);
    if (it != swapTo.end()) {
      for (auto& item : it->second.queues) {
        if (item.second) {
          utility::deleteCounter(item.second->getName());
        }
      }
      if (it->second.aggregated) {
        utility::deleteCounter(it->second.aggregated->getName());
      }
    }
  }
}

void BcmCosQueueManager::updateQueueStats(
    std::chrono::seconds now,
    HwPortStats* portStats) {
  // We only need one BcmEgressQueueFlexCounter for all the BcmPorts in
  // the same unit, and this FlexCounter is created in BcmSwitch::setupCos()
  // So this should be thread-safe to call here.
  // Besides, BcmCosQueueManager's lifetime totally depends on BcmPort, so
  // once we're thread-safe to call BcmPort::updateStats(), this function
  // here is also safe.
  auto* flexCounterMgr = hw_->getBcmEgressQueueFlexCounterManager();
  auto queueFlexCounterStatsLock = queueFlexCounterStats_.wlock();
  if (flexCounterMgr) {
    // Collect all queue stats for such port at once.
    flexCounterMgr->getStats(portGport_, *queueFlexCounterStatsLock);
  }

  for (const auto& cntr : queueCounters_) {
    if (cntr.first.isScopeQueues()) {
      for (auto& countersItr : cntr.second.queues) {
        auto queueStatIDPair =
            getQueueStatIDPair(countersItr.first, cntr.first.streamType);
        auto value = getQueueStatValue(
            hw_->getUnit(),
            queueStatIDPair.first,
            queueStatIDPair.second,
            portName_,
            countersItr.first,
            cntr.first,
            *queueFlexCounterStatsLock);
        updateQueueStat(
            countersItr.first,
            cntr.first.statType,
            value,
            countersItr.second.get(),
            now,
            portStats);
      }
    }
    if (cntr.first.isScopeAggregated()) {
      updateQueueAggregatedStat(
          cntr.first, cntr.second.aggregated.get(), now, portStats);
    }
  }
}

std::map<int32_t, int64_t> BcmCosQueueManager::getQueueStats(
    BcmCosQueueStatType statType) {
  std::map<int32_t, int64_t> queueOutPkts;
  auto queueFlexCounterStatsLock = queueFlexCounterStats_.rlock();
  if (queueFlexCounterStatsLock) {
    auto statMap = facebook::fb303::fbData->getStatMap();
    for (const auto& cntr : queueCounters_) {
      if (cntr.first.isScopeQueues()) {
        if (cntr.first.statType == statType) {
          for (const auto& queue : cntr.second.queues) {
            queueOutPkts[queue.first] =
                getCumulativeCounter(statMap, queue.second->getName());
          }
        }
      }
    }
  }
  return queueOutPkts;
}

void BcmCosQueueManager::updateQueueStat(
    bcm_cos_queue_t cosQ,
    BcmCosQueueStatType statType,
    uint64_t value,
    facebook::stats::MonotonicCounter* counter,
    std::chrono::seconds now,
    HwPortStats* portStats) {
  if (counter) {
    counter->updateValue(now, value);
  }

  if (portStats) {
    if (statType == BcmCosQueueStatType::DROPPED_BYTES) {
      portStats->queueOutDiscardBytes_()[cosQ] = value;
    } else if (statType == BcmCosQueueStatType::DROPPED_PACKETS) {
      portStats->queueOutDiscardPackets_()[cosQ] = value;
    } else if (statType == BcmCosQueueStatType::OUT_BYTES) {
      portStats->queueOutBytes_()[cosQ] = value;
    } else if (statType == BcmCosQueueStatType::OUT_PACKETS) {
      portStats->queueOutPackets_()[cosQ] = value;
    } else if (statType == BcmCosQueueStatType::WRED_DROPPED_PACKETS) {
      portStats->queueWredDroppedPackets_()[cosQ] = value;
    }
  }
}

bcm_cos_queue_t BcmCosQueueManager::getCosQueue(
    cfg::StreamType streamType,
    bcm_gport_t gport) const {
  if (streamType == cfg::StreamType::UNICAST) {
    return BCM_GPORT_UCAST_QUEUE_GROUP_QID_GET(gport);
  } else if (streamType == cfg::StreamType::MULTICAST) {
    return BCM_COSQ_GPORT_MCAST_EGRESS_QUEUE_GET(gport);
  }
  throw FbossError(
      "Failed to get cos queue because unknown StreamType ",
      apache::thrift::util::enumNameSafe(streamType));
  ;
}

int BcmCosQueueManager::getControlValue(
    cfg::StreamType streamType,
    bcm_gport_t gport,
    bcm_cos_queue_t cosQ,
    BcmCosQueueControlType ctrlType) const {
  bcm_cosq_control_t type;
  std::string description;
  std::tie(type, description) =
      kCosqControlTypeAndDescMap.at(ctrlType).at(streamType);

  int value;
  int rv = bcm_cosq_control_get(hw_->getUnit(), gport, cosQ, type, &value);
  bcmCheckError(
      rv, "Unable to get ", description, " for ", portName_, " cosQ ", cosQ);
  return value;
}

void BcmCosQueueManager::programControlValue(
    cfg::StreamType streamType,
    bcm_gport_t gport,
    bcm_cos_queue_t cosQ,
    BcmCosQueueControlType ctrlType,
    int value) {
  bcm_cosq_control_t type;
  std::string description;
  std::tie(type, description) =
      kCosqControlTypeAndDescMap.at(ctrlType).at(streamType);

  int rv = bcm_cosq_control_set(hw_->getUnit(), gport, cosQ, type, value);
  bcmCheckError(
      rv, "Unable to set ", description, " for ", portName_, " cosQ ", cosQ);
}

void BcmCosQueueManager::getSchedulingAndWeight(
    bcm_gport_t gport,
    bcm_cos_queue_t cosQ,
    PortQueue* queue) const {
  int mode, weight;
  int rv =
      bcm_cosq_gport_sched_get(hw_->getUnit(), gport, cosQ, &mode, &weight);
  bcmCheckError(
      rv,
      "Unable to get scheduling mode and weight for ",
      portName_,
      " cosQ ",
      cosQ);

  auto cfgPair =
      utility::bcmSchedulingAndWeightToCfg(std::make_pair(mode, weight));
  queue->setScheduling(cfgPair.first);
  queue->setWeight(cfgPair.second);
}

void BcmCosQueueManager::programSchedulingAndWeight(
    bcm_gport_t gport,
    bcm_cos_queue_t cosQ,
    const PortQueue& queue) {
  auto bcmPair = utility::cfgSchedulingAndWeightToBcm(
      std::make_pair(queue.getScheduling(), queue.getWeight()));
  int rv = bcm_cosq_gport_sched_set(
      hw_->getUnit(), gport, cosQ, bcmPair.first, bcmPair.second);
  bcmCheckError(
      rv,
      "Unable to set scheduling mode and weight for ",
      portName_,
      " cosQ ",
      cosQ);
}

void BcmCosQueueManager::getReservedBytes(
    bcm_gport_t gport,
    bcm_cos_queue_t cosQ,
    PortQueue* queue) const {
  int reservedBytes = getControlValue(
      queue->getStreamType(),
      gport,
      cosQ,
      BcmCosQueueControlType::RESERVED_BYTES);
  const auto& defaultQueueSettings =
      getDefaultQueueSettings(queue->getStreamType());
  if (reservedBytes &&
      reservedBytes != defaultQueueSettings.getReservedBytes().value()) {
    queue->setReservedBytes(reservedBytes);
  }
}

void BcmCosQueueManager::programReservedBytes(
    bcm_gport_t gport,
    bcm_cos_queue_t cosQ,
    const PortQueue& queue) {
  const auto& defaultQueueSettings =
      getDefaultQueueSettings(queue.getStreamType());
  int reservedBytes = defaultQueueSettings.getReservedBytes().value();
  if (queue.getReservedBytes()) {
    reservedBytes = queue.getReservedBytes().value();
  }
  // Based on CS00011976085, Broadcom recommends us *NOT* to change reserved
  // bytes if there's traffic running.
  // "Lets say, you are trying to configure
  // bcmCosqControlEgressUCQueueMinLimitBytes to a value X. To configure X, we
  // first need to adjust Shared buffer. If X is less than current value, shared
  // buffer needs to be increased. If X is greater than current value, then
  // shared buffer needs to be decreased. Now, in the case where Shared buffer
  // needs to be increased, we first need to make sure MIN_COUNT (current usage)
  // is less than the amount of bytes/cells required to increase shared buffer.
  // If MIN_COUNT does not come down, then we will not get any free cells to
  // allocate to Shared buffer. Hence, we have this check in place to make sure
  // you total Cell limit/usage is always in sync and within the available
  // buffer limits."
  // Therefore, we should just check whether the to-be-programmed value is the
  // same as hw value, if so we don't need to reprogram it again.
  int programmedReservedBytes = getControlValue(
      queue.getStreamType(),
      gport,
      cosQ,
      BcmCosQueueControlType::RESERVED_BYTES);
  if (programmedReservedBytes == reservedBytes) {
    XLOG(DBG2) << "Current programmed queue reserved bytes for gport " << gport
               << ", queue " << static_cast<int>(queue.getID())
               << " is the same as target value:" << reservedBytes
               << ", skip programming reserved bytes";
  } else {
    programControlValue(
        queue.getStreamType(),
        gport,
        cosQ,
        BcmCosQueueControlType::RESERVED_BYTES,
        reservedBytes);
  }
}

void BcmCosQueueManager::getSharedBytes(
    bcm_gport_t gport,
    bcm_cos_queue_t cosQ,
    PortQueue* queue) const {
  int sharedBytes = getControlValue(
      queue->getStreamType(),
      gport,
      cosQ,
      BcmCosQueueControlType::SHARED_BYTES);
  const auto& defaultQueueSettings =
      getDefaultQueueSettings(queue->getStreamType());
  if (sharedBytes &&
      sharedBytes != defaultQueueSettings.getSharedBytes().value()) {
    queue->setSharedBytes(sharedBytes);
  }
}

void BcmCosQueueManager::programSharedBytes(
    bcm_gport_t gport,
    bcm_cos_queue_t cosQ,
    const PortQueue& queue) {
  const auto& defaultQueueSettings =
      getDefaultQueueSettings(queue.getStreamType());
  int sharedBytes = defaultQueueSettings.getSharedBytes().value();
  if (queue.getSharedBytes()) {
    sharedBytes = queue.getSharedBytes().value();
  }
  // Only program shared bytes if it's different from the programmed one.
  // Same reason as programReservedBytes() above
  int programmedSharedBytes = getControlValue(
      queue.getStreamType(), gport, cosQ, BcmCosQueueControlType::SHARED_BYTES);
  if (programmedSharedBytes == sharedBytes) {
    XLOG(DBG2) << "Current programmed queue shared bytes for gport " << gport
               << ", queue " << static_cast<int>(queue.getID())
               << " is the same as target value:" << sharedBytes
               << ", skip programming shared bytes";
  } else {
    XLOG(DBG1) << "Setting gport " << gport << ", queue "
               << static_cast<int>(queue.getID()) << " shared bytes to "
               << sharedBytes << " bytes";
    programControlValue(
        queue.getStreamType(),
        gport,
        cosQ,
        BcmCosQueueControlType::SHARED_BYTES,
        sharedBytes);
  }
}

void BcmCosQueueManager::getBandwidth(
    bcm_gport_t gport,
    int queueIdx,
    PortQueue* queue) const {
  uint32_t ppsMin, ppsMax, flags;
  int rv = bcm_cosq_gport_bandwidth_get(
      hw_->getUnit(), gport, queueIdx, &ppsMin, &ppsMax, &flags);
  bcmCheckError(
      rv,
      "Unable to get cos queue bandwidth for ",
      portName_,
      " queue ",
      queueIdx);

  const auto& defaultQueueSettings =
      getDefaultQueueSettings(queue->getStreamType());

  const auto& portQueueRate = defaultQueueSettings.getPortQueueRate();
  // Check if default setting is pps and matches what is configued in
  // hardware
  if (flags == BCM_COSQ_BW_PACKET_MODE &&
      portQueueRate->type() == cfg::PortQueueRate::Type::pktsPerSec &&
      (portQueueRate->cref<switch_config_tags::pktsPerSec>()
           ->cref<switch_config_tags::minimum>()
           ->toThrift() == ppsMin) &&
      (portQueueRate->cref<switch_config_tags::pktsPerSec>()
           ->cref<switch_config_tags::maximum>()
           ->toThrift() == ppsMax)) {
    XLOG(DBG1) << "Configured pps equals default ppsMin: " << ppsMin
               << " and default ppsMax: " << ppsMax;
    return;
  }

  // Check if default setting is kbps and matches what is configued in hardware
  if (flags != BCM_COSQ_BW_PACKET_MODE &&
      portQueueRate->type() == cfg::PortQueueRate::Type::kbitsPerSec &&
      (portQueueRate->cref<switch_config_tags::kbitsPerSec>()
           ->cref<switch_config_tags::minimum>()
           ->toThrift() == ppsMin) &&
      (portQueueRate->cref<switch_config_tags::kbitsPerSec>()
           ->cref<switch_config_tags::maximum>()
           ->toThrift() == ppsMax)) {
    XLOG(DBG1) << "Configured kbps equals default ppsMin: " << ppsMin
               << " and default ppsMax: " << ppsMax;
    return;
  }

  cfg::PortQueueRate cfgPortQueueRate;

  cfg::Range range;
  *range.minimum() = ppsMin;
  *range.maximum() = ppsMax;

  flags != BCM_COSQ_BW_PACKET_MODE
      ? cfgPortQueueRate.kbitsPerSec().emplace(range)
      : cfgPortQueueRate.pktsPerSec().emplace(range);

  queue->setPortQueueRate(cfgPortQueueRate);
}

void BcmCosQueueManager::programBandwidth(
    bcm_gport_t gport,
    int queueIdx,
    const PortQueue& queue) {
  uint32_t rateMin, rateMax, flags;

  const auto& defaultQueueSettings =
      getDefaultQueueSettings(queue.getStreamType());
  // THRIFT_COPY
  auto portQueueRate = defaultQueueSettings.getPortQueueRate()->toThrift();

  if (queue.getPortQueueRate()) {
    portQueueRate = queue.getPortQueueRate()->toThrift();
  }

  if (portQueueRate.getType() == cfg::PortQueueRate::Type::pktsPerSec) {
    rateMin = *portQueueRate.pktsPerSec().as_const()->minimum();
    rateMax = *portQueueRate.pktsPerSec().as_const()->maximum();
    flags = BCM_COSQ_BW_PACKET_MODE;
  } else { // cfg::PortQueueRate::Type::pktsPerSec
    rateMin = *portQueueRate.kbitsPerSec().as_const()->minimum();
    rateMax = *portQueueRate.kbitsPerSec().as_const()->maximum();
    flags = 0;
  }

  XLOG(DBG1) << "Setting gport " << gport << ", queue "
             << static_cast<int>(queue.getID()) << " bandwidth "
             << ((flags == BCM_COSQ_BW_PACKET_MODE) ? "PacketsPerSec" : "Kbps")
             << " min: " << rateMin << " max: " << rateMax;

  int rv = bcm_cosq_gport_bandwidth_set(
      hw_->getUnit(), gport, queueIdx, rateMin, rateMax, flags);
  bcmCheckError(
      rv, "Unable to set pps/kbps for ", portName_, " queue ", queueIdx);
}

void BcmCosQueueManager::programEgressDynamicSharedEnabled(
    cfg::StreamType streamType,
    bcm_gport_t gport,
    bcm_cos_queue_t cosQ) const {
  bcm_cosq_control_t cosControl;
  std::string streamTypeStr;

  if (streamType == cfg::StreamType::MULTICAST) {
    cosControl = bcmCosqControlEgressMCSharedDynamicEnable;
    streamTypeStr = "mc";
  } else {
    cosControl = bcmCosqControlEgressUCSharedDynamicEnable;
    streamTypeStr = "uc";
  }

  auto errorDescr = folly::to<std::string>(
      "egress ", streamTypeStr, " shared dynamic enable value");
  int rv = bcm_cosq_control_set(hw_->getUnit(), gport, cosQ, cosControl, 1);

  bcmCheckError(
      rv, "Unable to set ", errorDescr, " for ", portName_, " cosQ ", cosQ);
}

bool BcmCosQueueManager::isEgressDynamicSharedEnabled(
    cfg::StreamType streamType,
    bcm_gport_t gport,
    bcm_cos_queue_t cosQ) const {
  int value = 0;
  std::string streamTypeStr;
  bcm_cosq_control_t cosControl;

  if (streamType == cfg::StreamType::MULTICAST) {
    cosControl = bcmCosqControlEgressMCSharedDynamicEnable;
    streamTypeStr = "mc";
  } else {
    cosControl = bcmCosqControlEgressUCSharedDynamicEnable;
    streamTypeStr = "uc";
  }

  auto errorDescr = folly::to<std::string>(
      "egress ", streamTypeStr, " shared dynamic enable");
  int rv =
      bcm_cosq_control_get(hw_->getUnit(), gport, cosQ, cosControl, &value);
  bcmCheckError(
      rv, "Unable to get ", errorDescr, " for ", portName_, " cosQ ", cosQ);
  return (value != 0);
}

void BcmCosQueueManager::getAlpha(
    bcm_gport_t gport,
    bcm_cos_queue_t cosQ,
    PortQueue* queue) const {
  // check if EgressMC(UC)SharedDynamicEnable is enabled on the HW
  // mostly its programmed as enabled by default during sdk init
  // It causes failures on SIM where default settings may be different
  cfg::MMUScalingFactor scalingFactor;
  const auto& defaultQueueSettings =
      getDefaultQueueSettings(queue->getStreamType());
  if (isEgressDynamicSharedEnabled(queue->getStreamType(), gport, cosQ)) {
    auto alpha = getControlValue(
        queue->getStreamType(), gport, cosQ, BcmCosQueueControlType::ALPHA);
    scalingFactor = utility::bcmAlphaToCfgAlpha(
        static_cast<bcm_cosq_control_drop_limit_alpha_value_e>(alpha));
  } else {
    // pick the default valaue if not programmed
    scalingFactor = defaultQueueSettings.getScalingFactor().value();
  }

  if (scalingFactor != defaultQueueSettings.getScalingFactor().value()) {
    queue->setScalingFactor(scalingFactor);
  }
}

void BcmCosQueueManager::programAlpha(
    bcm_gport_t gport,
    bcm_cos_queue_t cosQ,
    const PortQueue& queue) {
  const auto& defaultQueueSettings =
      getDefaultQueueSettings(queue.getStreamType());

  // EgressDynamicShared is mostly enabled by SDK during init
  // but in some cases its not. Explicitly enable it always when we set
  // alpha
  // (1) SIM
  // (2) mmu_lossless = 1
  programEgressDynamicSharedEnabled(queue.getStreamType(), gport, cosQ);
  auto alpha = utility::cfgAlphaToBcmAlpha(
      defaultQueueSettings.getScalingFactor().value());
  if (queue.getScalingFactor()) {
    alpha = utility::cfgAlphaToBcmAlpha(queue.getScalingFactor().value());
  }
  auto programmedAlpha = getControlValue(
      queue.getStreamType(), gport, cosQ, BcmCosQueueControlType::ALPHA);
  if (programmedAlpha == alpha) {
    XLOG(DBG2) << "Current programmed queue alpha for gport " << gport
               << ", queue " << static_cast<int>(queue.getID())
               << " is the same as target value:" << alpha
               << ", skip programming alpha";
  } else {
    XLOG(DBG1) << "Setting gport " << gport << ", queue "
               << static_cast<int>(queue.getID()) << " alpha to " << alpha;
    programControlValue(
        queue.getStreamType(),
        gport,
        cosQ,
        BcmCosQueueControlType::ALPHA,
        alpha);
  }
}

void BcmCosQueueManager::updateQueueAggregatedStat(
    const BcmCosQueueCounterType& type,
    facebook::stats::MonotonicCounter* counter,
    std::chrono::seconds now,
    HwPortStats* portStats) {
  // bcm support passing -1 as cosQ id and using port gport can get
  // all cosQ stats
  auto statType = type.statType;

  uint64_t value;
  auto rv = bcm_cosq_stat_get(
      hw_->getUnit(),
      portGport_,
      -1,
      utility::getBcmCosqStatType(
          statType, hw_->getPlatform()->getAsic()->getAsicType()),
      &value);
  bcmCheckError(
      rv,
      "Unable to get aggregated cosq stat ",
      utility::getBcmCosqStatType(
          statType, hw_->getPlatform()->getAsic()->getAsicType()),
      " for ",
      portName_);

  if (counter) {
    counter->updateValue(now, value);
  }

  // right now HwPortStats only supports returning pkt stat for aggregated
  if (statType == BcmCosQueueStatType::DROPPED_PACKETS && portStats) {
    *portStats->outCongestionDiscardPkts_() = value;
  }
}

void BcmCosQueueManager::getCosQueueGportsFromHw() {
  auto rv = bcm_cosq_gport_traverse(
      hw_->getUnit(), &cosqGportTraverseCallback, static_cast<void*>(this));
  bcmCheckError(rv, "Failed to traverse queue gports for ", portName_);
  XLOG(DBG2) << "getCosQueueGportsFromHw for port: " << portName_
             << ", port gport: " << portGport_
             << ", unicast: " << cosQueueGports_.unicast.size()
             << ", multicast: " << cosQueueGports_.multicast.size();
}

int BcmCosQueueManager::getReservedBytes(
    cfg::StreamType streamType,
    bcm_gport_t gport,
    const bcm_cos_queue_t cosQ) {
  return getControlValue(
      streamType, gport, cosQ, BcmCosQueueControlType::RESERVED_BYTES);
}

std::set<bcm_cos_queue_t> BcmCosQueueManager::getNamedQueues(
    cfg::StreamType streamType) const {
  std::set<bcm_cos_queue_t> named;
  auto namedQueue = namedQueues_.find(streamType);
  if (namedQueue != namedQueues_.end()) {
    named = namedQueue->second;
  }
  return named;
}

void BcmCosQueueManager::updateNamedQueue(const PortQueue& queue) {
  auto cosQ = queue.getID();
  auto streamType = queue.getStreamType();
  bool queueHasName = queue.getName().has_value();

  if (namedQueues_.find(streamType) != namedQueues_.end()) {
    std::set<bcm_cos_queue_t>& namedQueue = namedQueues_.at(streamType);
    // Add to named queues if the queue has a name, delete if not!
    if (queueHasName) {
      namedQueue.insert(cosQ);
    } else {
      namedQueue.erase(cosQ);
    }
  } else if (queueHasName) {
    namedQueues_.insert({streamType, std::set<bcm_cos_queue_t>{cosQ}});
  }
}

} // namespace facebook::fboss
