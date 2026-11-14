/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/HwBasePortFb303Stats.h"

#include <fb303/ThreadCachedServiceData.h>

#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/hw/CounterUtils.h"
#include "fboss/agent/hw/StatsConstants.h"

#include <fb303/ServiceData.h>
#include <folly/logging/xlog.h>

#include "common/stats/DynamicStats.h"

namespace {

DEFINE_dynamic_quantile_stat(
    buffer_watermark_ucast,
    "buffer_watermark_ucast.{}.{}.{}",
    facebook::fb303::ExportTypeConsts::kNone,
    std::array<double, 1>{{1.0}});

DEFINE_dynamic_quantile_stat(
    egress_gvoq_watermark,
    "egress_gvoq_watermark.{}.{}.{}",
    facebook::fb303::ExportTypeConsts::kNone,
    std::array<double, 1>{{1.0}});

} // unnamed namespace

namespace facebook::fboss {

std::string HwBasePortFb303Stats::statName(
    folly::StringPiece statName,
    folly::StringPiece portName) {
  return folly::to<std::string>(portName, ".", statName);
}

std::string HwBasePortFb303Stats::statName(
    folly::StringPiece statName,
    folly::StringPiece portName,
    int queueId,
    folly::StringPiece queueName) {
  return folly::to<std::string>(
      portName, ".", "queue", queueId, ".", queueName, ".", statName);
}

std::string HwBasePortFb303Stats::statName(
    folly::StringPiece statName,
    folly::StringPiece portName,
    PfcPriority priority) {
  return folly::to<std::string>(portName, ".", statName, ".priority", priority);
}

std::string HwBasePortFb303Stats::pgStatName(
    folly::StringPiece statName,
    folly::StringPiece portName,
    int priority) {
  return folly::to<std::string>(portName, ".", statName, ".pg", priority);
}

int64_t HwBasePortFb303Stats::getCounterLastIncrement(
    folly::StringPiece statKey,
    std::optional<int64_t> defaultVal) const {
  return portCounters_.getCounterLastIncrement(statKey.str(), defaultVal);
}

void HwBasePortFb303Stats::reinitStats(std::optional<std::string> oldPortName) {
  XLOG(DBG2) << "Reinitializing stats for " << portName_;

  for (auto statKey : kPortMonotonicCounterStatKeys()) {
    reinitStat(statKey, portName_, oldPortName);
  }
  for (auto statKey : kPortFb303CounterStatKeys()) {
    // For fb303 - init will happen on the next set. So here we
    // just delete the counter
    utility::deleteCounter(statName(statKey, oldPortName.value_or("")));
  }
  // Phy transition timeseries and latch-derived stats are not in either list
  // above. Only their old names need clearing here.
  if (oldPortName) {
    for (auto statKey :
         {kLineRxSignalDetectChanged(),
          kSystemRxSignalDetectChanged(),
          kLineRxCdrLockChanged(),
          kSystemRxCdrLockChanged(),
          kLineLocalFaultChanged(),
          kLineRemoteFaultChanged(),
          kLinkDownDuringWarmBoot()}) {
      utility::deleteCounter(statName(statKey, *oldPortName));
    }
  }
  for (const auto& queueIdAndName : queueId2Name_) {
    for (auto statKey : kQueueMonotonicCounterStatKeys()) {
      auto newStatName = statName(
          statKey, portName_, queueIdAndName.first, queueIdAndName.second);
      std::optional<std::string> oldStatName = oldPortName
          ? std::optional<std::string>(statName(
                statKey,
                *oldPortName,
                queueIdAndName.first,
                queueIdAndName.second))
          : std::nullopt;
      portCounters_.reinitStat(newStatName, oldStatName);
    }
    for (auto statKey : kQueueFb303CounterStatKeys()) {
      utility::deleteCounter(statName(
          statKey,
          oldPortName.value_or(""),
          queueIdAndName.first,
          queueIdAndName.second));
    }
  }
  if (macsecStatsInited_) {
    reinitMacsecStats(oldPortName);
  }
  if (srv6MysidDiscardCounterSupported_) {
    reinitStat(kInSrv6MySidDiscards(), portName_, oldPortName);
  }
  if (mplsLabelLookupFailCounterSupported_) {
    reinitStat(kInLabelMissDiscards(), portName_, oldPortName);
  }
  if (linkDebounceRetriggerCounterSupported_) {
    reinitStat(kLinkDownDebounceRetriggerCount(), portName_, oldPortName);
    reinitStat(kLinkUpDebounceRetriggerCount(), portName_, oldPortName);
  }
  if (sllHllDiscardCounterSupported_) {
    reinitStat(kOutDiscardsSll(), portName_, oldPortName);
    reinitStat(kOutDiscardsHll(), portName_, oldPortName);
  }
  if (portLatchStatusSupported_) {
    fb303::fbData->setCounter(
        statName(kLinkDownDuringWarmBoot(), portName_), 0);
  }
  // Init per priority PFC stats
  if (getEnabledPfcPriorities().size()) {
    reinitPfcStats(oldPortName);
  }
}

/*
 * Reinit macsec stats
 */
void HwBasePortFb303Stats::reinitMacsecStats(
    std::optional<std::string> oldPortName) {
  auto reinitStats = [this, &oldPortName](const auto& keys) {
    for (auto statKey : keys) {
      reinitStat(statKey, portName_, oldPortName);
    }
  };
  reinitStats(kInMacsecPortMonotonicCounterStatKeys());
  reinitStats(kOutMacsecPortMonotonicCounterStatKeys());

  macsecStatsInited_ = true;
}

void HwBasePortFb303Stats::reinitPfcStats(
    std::optional<std::string> oldPortName) {
  auto reinitPortPriorityPfcStats =
      [&](const std::vector<folly::StringPiece>& keys) {
        for (const auto& pfcPriority : getEnabledPfcPriorities()) {
          for (auto statKey : keys) {
            auto newStatName = statName(statKey, portName_, pfcPriority);
            std::optional<std::string> oldStatName = oldPortName
                ? std::optional<std::string>(
                      statName(statKey, *oldPortName, pfcPriority))
                : std::nullopt;
            portCounters_.reinitStat(newStatName, oldStatName);
          }
        }
      };

  // Init per priority PFC stats
  reinitPortPriorityPfcStats(kPfcMonotonicCounterStatKeys());

  // Init aggregated port PFC counters as well
  for (auto statKey : kPfcMonotonicCounterStatKeys()) {
    auto newStatName = statName(statKey, portName_);
    std::optional<std::string> oldStatName = oldPortName
        ? std::optional<std::string>(statName(statKey, *oldPortName))
        : std::nullopt;
    portCounters_.reinitStat(newStatName, oldStatName);
  }

  // Init PFC deadlock counters as well
  for (auto statKey : kPfcDeadlockMonotonicCounterStatKeys()) {
    auto newStatName = statName(statKey, portName_);
    std::optional<std::string> oldStatName = oldPortName
        ? std::optional<std::string>(statName(statKey, *oldPortName))
        : std::nullopt;
    portCounters_.reinitStat(newStatName, oldStatName);
  }

  // Init PFC RX/TX duration counters as well
  if (pfcInfo_.rxPfcDurationEnabled) {
    reinitPortPriorityPfcStats({kRxPfcDurationUsec()});
  }
  if (pfcInfo_.txPfcDurationEnabled) {
    reinitPortPriorityPfcStats({kTxPfcDurationUsec()});
  }

  // Init priority group stats, keyed by PG id (see pgIdsForStats()).
  for (auto pg : pgIdsForStats()) {
    if (pfcInfo_.inCongestionDiscardCountSupported) {
      for (auto statKey : kPriorityGroupMonotonicCounterStatKeys()) {
        auto newStatName = pgStatName(statKey, portName_, pg);
        std::optional<std::string> oldStatName = oldPortName
            ? std::optional<std::string>(pgStatName(statKey, *oldPortName, pg))
            : std::nullopt;
        portCounters_.reinitStat(newStatName, oldStatName);
      }
      // kPriorityGroupCounterStatKeys() need not be inited, init will happen on
      // the next set.
    }
  }
}

const std::vector<int>& HwBasePortFb303Stats::pgIdsForStats() const {
  // The PG id set is fixed, so build it once and return a reference to avoid a
  // per-call allocation on the (per-port, per-update) stats path.
  static const std::vector<int> kNoPgs;
  static const std::vector<int> kAllPgs = [] {
    std::vector<int> pgIds;
    for (int pg = 0; pg <= cfg::switch_config_constants::PORT_PG_VALUE_MAX();
         ++pg) {
      pgIds.push_back(pg);
    }
    return pgIds;
  }();
  return getEnabledPfcPriorities().empty() ? kNoPgs : kAllPgs;
}

/*
 * Reinit port stat
 */
void HwBasePortFb303Stats::reinitStat(
    folly::StringPiece statKey,
    const std::string& portName,
    std::optional<std::string> oldPortName) {
  portCounters_.reinitStat(
      statName(statKey, portName),
      oldPortName ? std::optional<std::string>(statName(statKey, *oldPortName))
                  : std::nullopt);
}

/*
 * Reinit port queue stat
 */
void HwBasePortFb303Stats::reinitStat(
    folly::StringPiece statKey,
    int queueId,
    std::optional<std::string> oldQueueName) {
  portCounters_.reinitStat(
      statName(statKey, portName_, queueId, queueId2Name_[queueId]),
      oldQueueName ? std::optional<std::string>(
                         statName(statKey, portName_, queueId, *oldQueueName))
                   : std::nullopt);
}

void HwBasePortFb303Stats::queueChanged(
    int queueId,
    const std::string& queueName) {
  auto qitr = queueId2Name_.find(queueId);
  std::optional<std::string> oldQueueName = qitr == queueId2Name_.end()
      ? std::nullopt
      : std::optional<std::string>(qitr->second);
  queueId2Name_[queueId] = queueName;
  for (auto statKey : kQueueMonotonicCounterStatKeys()) {
    reinitStat(statKey, queueId, oldQueueName);
  }
  for (auto statKey : kQueueFb303CounterStatKeys()) {
    utility::deleteCounter(
        statName(statKey, portName_, queueId, queueId2Name_[queueId]));
  }
}

void HwBasePortFb303Stats::queueRemoved(int queueId) {
  for (auto statKey : kQueueMonotonicCounterStatKeys()) {
    portCounters_.removeStat(
        statName(statKey, portName_, queueId, queueId2Name_[queueId]));
  }
  for (auto statKey : kQueueFb303CounterStatKeys()) {
    utility::deleteCounter(
        statName(statKey, portName_, queueId, queueId2Name_[queueId]));
  }
  queueId2Name_.erase(queueId);
}

void HwBasePortFb303Stats::pfcConfigChanged(
    std::vector<PfcPriority> enabledPriorities,
    std::optional<cfg::PortPfc> pfc) {
  auto removeAllPfcPriorityKeys =
      [&](const std::vector<PfcPriority>& priorities, const auto& keys) {
        for (const auto& priority : priorities) {
          // Remove old keys
          for (auto statKey : keys) {
            portCounters_.removeStat(statName(statKey, portName_, priority));
          }
        }
      };

  auto reinitAllPfcPriorityKeys =
      [&](const std::vector<PfcPriority>& priorities, const auto& keys) {
        for (const auto& priority : priorities) {
          // Reinit keys
          for (auto statKey : keys) {
            portCounters_.reinitStat(
                statName(statKey, portName_, priority), std::nullopt);
          }
        }
      };

  auto removeAllPriorityGroupKeys = [&](const std::vector<int>& pgIds) {
    for (const auto& pg : pgIds) {
      if (pfcInfo_.inCongestionDiscardCountSupported) {
        for (auto statKey : kPriorityGroupMonotonicCounterStatKeys()) {
          portCounters_.removeStat(pgStatName(statKey, portName_, pg));
        }
      }
    }
  };

  auto reinitAllPriorityGroupKeys = [&](const std::vector<int>& pgIds) {
    for (const auto& pg : pgIds) {
      if (pfcInfo_.inCongestionDiscardCountSupported) {
        for (auto statKey : kPriorityGroupMonotonicCounterStatKeys()) {
          portCounters_.reinitStat(
              pgStatName(statKey, portName_, pg), std::nullopt);
        }
        // kPriorityGroupCounterStatKeys() need not be inited, init will
        // happen on the next set.
      }
    }
  };

  bool rxPfcDurationEnabled =
      pfc.has_value() && pfc->rxPfcDurationEnable().has_value()
      ? *pfc->rxPfcDurationEnable()
      : false;
  bool txPfcDurationEnabled =
      pfc.has_value() && pfc->txPfcDurationEnable().has_value()
      ? *pfc->txPfcDurationEnable()
      : false;
  // Duration counters exist per enabled priority while the duration is
  // enabled, so they must follow a change to either.
  auto updatePfcDurationKeys =
      [&](bool& enabled, bool newEnabled, folly::StringPiece statKey) {
        bool prioritiesChanged = getEnabledPfcPriorities() != enabledPriorities;
        auto keys = {statKey};
        if (enabled && (!newEnabled || prioritiesChanged)) {
          removeAllPfcPriorityKeys(getEnabledPfcPriorities(), keys);
        }
        if (newEnabled && (!enabled || prioritiesChanged)) {
          reinitAllPfcPriorityKeys(enabledPriorities, keys);
        }
        enabled = newEnabled;
      };
  updatePfcDurationKeys(
      pfcInfo_.rxPfcDurationEnabled,
      rxPfcDurationEnabled,
      kRxPfcDurationUsec());
  updatePfcDurationKeys(
      pfcInfo_.txPfcDurationEnabled,
      txPfcDurationEnabled,
      kTxPfcDurationUsec());

  // Handle a change in PFC priorities
  if (getEnabledPfcPriorities() != enabledPriorities) {
    auto pfckeys = kPfcMonotonicCounterStatKeys();
    removeAllPfcPriorityKeys(getEnabledPfcPriorities(), pfckeys);
    removeAllPriorityGroupKeys(pgIdsForStats());
    pfcInfo_.enabledPfcPriorities = std::move(enabledPriorities);
    reinitAllPfcPriorityKeys(getEnabledPfcPriorities(), pfckeys);
    reinitAllPriorityGroupKeys(pgIdsForStats());
  }
  if (getEnabledPfcPriorities().size()) {
    // If PFC is enabled for priorities, init aggregated port
    // PFC counters as well.
    for (auto statKey : kPfcMonotonicCounterStatKeys()) {
      portCounters_.reinitStat(statName(statKey, portName_), std::nullopt);
    }
    // If PFC is enabled for priorities, init aggregated port
    // PFC deadlock counters as well.
    for (auto statKey : kPfcDeadlockMonotonicCounterStatKeys()) {
      portCounters_.reinitStat(statName(statKey, portName_), std::nullopt);
    }
  } else {
    for (auto statKey : kPfcMonotonicCounterStatKeys()) {
      portCounters_.removeStat(statName(statKey, portName_));
    }
    for (auto statKey : kPfcDeadlockMonotonicCounterStatKeys()) {
      portCounters_.removeStat(statName(statKey, portName_));
    }
  }
}

void HwBasePortFb303Stats::updateLeakyBucketFlapCnt(int cnt) {
  auto now = duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch());
  updateStat(now, kLeakyBucketFlapCnt(), cnt);
}

void HwBasePortFb303Stats::updatePhyChanged(
    folly::StringPiece statKey,
    bool changed) {
  tcData().addStatValue(
      statName(statKey, portName_), changed ? 1 : 0, fb303::SUM);
}

void HwBasePortFb303Stats::updateStat(
    const std::chrono::seconds& now,
    folly::StringPiece statKey,
    int queueId,
    int64_t val) {
  portCounters_.updateStat(
      now, statName(statKey, portName_, queueId, queueId2Name_[queueId]), val);
}

void HwBasePortFb303Stats::updateStat(
    const std::chrono::seconds& now,
    folly::StringPiece statKey,
    int64_t val) {
  portCounters_.updateStat(now, statName(statKey, portName_), val);
}

void HwBasePortFb303Stats::updateStat(
    const std::chrono::seconds& now,
    folly::StringPiece statKey,
    PfcPriority priority,
    int64_t val) {
  portCounters_.updateStat(now, statName(statKey, portName_, priority), val);
}

void HwBasePortFb303Stats::updatePgStat(
    const std::chrono::seconds& now,
    folly::StringPiece statKey,
    int pg,
    int64_t val) {
  portCounters_.updateStat(now, pgStatName(statKey, portName_, pg), val);
}

void HwBasePortFb303Stats::setPgCounter(
    const std::chrono::seconds& now,
    folly::StringPiece statKey,
    int pg,
    int64_t val) {
  fb303::fbData->setCounter(pgStatName(statKey, portName_, pg), val);
}

void HwBasePortFb303Stats::updateQueueWatermarkStats(
    const std::map<int16_t, int64_t>& queueWatermarks) const {
  for (const auto& [queueId, queueName] : queueId2Name_) {
    auto watermarkItr = queueWatermarks.find(queueId);
    CHECK(watermarkItr != queueWatermarks.end());
    STATS_buffer_watermark_ucast.addValue(
        watermarkItr->second,
        portName_,
        folly::to<std::string>("queue", queueId),
        queueName);
  }
}

void HwBasePortFb303Stats::updateEgressGvoqWatermarkStats(
    const std::map<int16_t, int64_t>& gvoqWatermarks) const {
  for (const auto& [queueId, queueName] : queueId2Name_) {
    auto watermarkItr = gvoqWatermarks.find(queueId);
    CHECK(watermarkItr != gvoqWatermarks.end());
    STATS_egress_gvoq_watermark.addValue(
        watermarkItr->second,
        portName_,
        folly::to<std::string>("queue", queueId),
        queueName);
  }
}

void HwBasePortFb303Stats::clearStat(folly::StringPiece statKey) {
  portCounters_.clearStat(statName(statKey, portName_));
}
} // namespace facebook::fboss
