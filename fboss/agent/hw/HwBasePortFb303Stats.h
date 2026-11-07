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

#include "fboss/agent/hw/HwFb303Stats.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/types.h"

#include "folly/container/F14Map.h"

#include <optional>
#include <string>

namespace facebook::fboss {

struct PfcPriorityInfo {
  std::vector<PfcPriority> enabledPfcPriorities{};
  bool txPfcDurationEnabled{false};
  bool rxPfcDurationEnabled{false};
  bool inCongestionDiscardCountSupported{false};
  bool inCongestionDiscardSeenSupported{false};
};

class HwBasePortFb303Stats {
 public:
  using QueueId2Name = folly::F14FastMap<int, std::string>;
  explicit HwBasePortFb303Stats(
      const std::string& portName,
      QueueId2Name queueId2Name = {},
      std::vector<PfcPriority> enabledPfcPriorities = {},
      std::optional<cfg::PortPfc> pfcCfg = std::nullopt,
      bool inCongestionDiscardCountSupported = false,
      bool inCongestionDiscardSeenSupported = false,
      bool srv6MysidDiscardCounterSupported = false,
      bool mplsLabelLookupFailCounterSupported = false,
      bool linkDebounceRetriggerCounterSupported = false,
      bool sllHllDiscardCounterSupported = false,
      bool portLatchStatusSupported = false,
      std::optional<std::string> multiSwitchStatsPrefix = std::nullopt)
      : queueId2Name_(std::move(queueId2Name)),
        portLatchStatusSupported_(portLatchStatusSupported),
        portName_(portName),
        srv6MysidDiscardCounterSupported_(srv6MysidDiscardCounterSupported),
        mplsLabelLookupFailCounterSupported_(
            mplsLabelLookupFailCounterSupported),
        linkDebounceRetriggerCounterSupported_(
            linkDebounceRetriggerCounterSupported),
        sllHllDiscardCounterSupported_(sllHllDiscardCounterSupported),
        portCounters_(HwFb303Stats(std::move(multiSwitchStatsPrefix))) {
    pfcInfo_.inCongestionDiscardCountSupported =
        inCongestionDiscardCountSupported;
    pfcInfo_.inCongestionDiscardSeenSupported =
        inCongestionDiscardSeenSupported;
    pfcInfo_.enabledPfcPriorities = std::move(enabledPfcPriorities);
    pfcInfo_.rxPfcDurationEnabled =
        pfcCfg.has_value() && pfcCfg->rxPfcDurationEnable().has_value()
        ? *pfcCfg->rxPfcDurationEnable()
        : false;
    pfcInfo_.txPfcDurationEnabled =
        pfcCfg.has_value() && pfcCfg->txPfcDurationEnable().has_value()
        ? *pfcCfg->txPfcDurationEnable()
        : false;
  }

  virtual ~HwBasePortFb303Stats() = default;

  const std::string& portName() const {
    return portName_;
  }

  virtual void portNameChanged(const std::string& newName) {
    auto oldPortName = portName_;
    portName_ = newName;
    reinitStats(oldPortName);
  }
  virtual void queueChanged(int queueId, const std::string& queueName);
  virtual void queueRemoved(int queueId);
  void pfcConfigChanged(
      std::vector<PfcPriority> enabledPriorities,
      std::optional<cfg::PortPfc> pfc);
  void updateLeakyBucketFlapCnt(int cnt);
  // Publishes whether a phy status changed during the interval just read, as
  // a SUM timeseries so ODS sees .sum/.sum.60/... rather than a level a slower
  // scrape could step over. statKey must be one of the side qualified
  // k*Changed() keys in StatsConstants.h.
  void updatePhyChanged(folly::StringPiece statKey, bool changed);

  /*
   * Port stat name
   */
  static std::string statName(
      folly::StringPiece statName,
      folly::StringPiece portName);

  /*
   * Port queue stat name
   */
  static std::string statName(
      folly::StringPiece statName,
      folly::StringPiece portName,
      int queueId,
      folly::StringPiece queueName);

  /*
   * Port PFC stat name
   */
  static std::string statName(
      folly::StringPiece statName,
      folly::StringPiece portName,
      PfcPriority priority);
  /*
   * clear port stat
   */
  void clearStat(folly::StringPiece statKey);

  /*
   * Priority group stat name
   */
  static std::string
  pgStatName(folly::StringPiece statName, folly::StringPiece portName, int pg);

  int64_t getCounterLastIncrement(
      folly::StringPiece statKey,
      std::optional<int64_t> defaultVal = std::nullopt) const;

  virtual const std::vector<folly::StringPiece>& kPortMonotonicCounterStatKeys()
      const = 0;
  virtual const std::vector<folly::StringPiece>& kPortFb303CounterStatKeys()
      const = 0;
  virtual const std::vector<folly::StringPiece>&
  kQueueMonotonicCounterStatKeys() const = 0;
  virtual const std::vector<folly::StringPiece>& kQueueFb303CounterStatKeys()
      const = 0;
  virtual const std::vector<folly::StringPiece>&
  kInMacsecPortMonotonicCounterStatKeys() const = 0;
  virtual const std::vector<folly::StringPiece>&
  kOutMacsecPortMonotonicCounterStatKeys() const = 0;
  virtual const std::vector<folly::StringPiece>& kPfcMonotonicCounterStatKeys()
      const = 0;
  virtual const std::vector<folly::StringPiece>&
  kPfcDeadlockMonotonicCounterStatKeys() const = 0;
  virtual const std::vector<folly::StringPiece>&
  kPriorityGroupMonotonicCounterStatKeys() const = 0;
  virtual const std::vector<folly::StringPiece>& kPriorityGroupCounterStatKeys()
      const = 0;

 protected:
  void reinitStats(std::optional<std::string> oldPortName);
  void reinitMacsecStats(std::optional<std::string> oldPortName);
  void reinitPfcStats(std::optional<std::string> oldPortName);
  /*
   * update port stat
   */
  void updateStat(
      const std::chrono::seconds& now,
      folly::StringPiece statKey,
      int64_t val);
  /*
   * update port queue stat
   */
  void updateStat(
      const std::chrono::seconds& now,
      folly::StringPiece statKey,
      int queueId,
      int64_t val);
  /*
   * update port PFC stat
   */
  void updateStat(
      const std::chrono::seconds& now,
      folly::StringPiece statKey,
      PfcPriority priority,
      int64_t val);
  /*
   * update port priority group stat
   */
  void updatePgStat(
      const std::chrono::seconds& now,
      folly::StringPiece statKey,
      int pg,
      int64_t val);
  /*
   * Set the absolute value of a port priority group counter.
   */
  void setPgCounter(
      const std::chrono::seconds& now,
      folly::StringPiece statKey,
      int pg,
      int64_t val);

  void updateQueueWatermarkStats(
      const std::map<int16_t, int64_t>& queueWatermarkBytes) const;

  void updateEgressGvoqWatermarkStats(
      const std::map<int16_t, int64_t>& gvoqWatermarks) const;

  bool macsecStatsInited() const {
    return macsecStatsInited_;
  }
  const QueueId2Name& queueId2Name() const {
    return queueId2Name_;
  }
  const std::vector<PfcPriority>& getEnabledPfcPriorities() const {
    return pfcInfo_.enabledPfcPriorities;
  }
  bool isRxPfcDurationEnabled() const {
    return pfcInfo_.rxPfcDurationEnabled;
  }
  bool isTxPfcDurationEnabled() const {
    return pfcInfo_.txPfcDurationEnabled;
  }
  bool isSrv6MysidDiscardCounterSupported() const {
    return srv6MysidDiscardCounterSupported_;
  }
  bool isMplsLabelLookupFailCounterSupported() const {
    return mplsLabelLookupFailCounterSupported_;
  }
  bool isLinkDebounceRetriggerCounterSupported() const {
    return linkDebounceRetriggerCounterSupported_;
  }
  bool isSllHllDiscardCounterSupported() const {
    return sllHllDiscardCounterSupported_;
  }

  // PG ids for per-PG congestion counters: the full range
  // [0, PORT_PG_VALUE_MAX] when PFC is enabled, else empty. Returns a reference
  // to a shared static to avoid a per-call allocation on the stats path.
  const std::vector<int>& pgIdsForStats() const;

 protected:
  QueueId2Name queueId2Name_;
  bool portLatchStatusSupported_{false};

 private:
  /*
   * Reinit port stat
   */
  void reinitStat(
      folly::StringPiece statKey,
      const std::string& portName,
      std::optional<std::string> oldPortName);
  /*
   * Reinit port queue stat
   */
  void reinitStat(
      folly::StringPiece statKey,
      int queueId,
      std::optional<std::string> oldQueueName);

  std::string portName_;
  bool srv6MysidDiscardCounterSupported_{false};
  bool mplsLabelLookupFailCounterSupported_{false};
  bool linkDebounceRetriggerCounterSupported_{false};
  bool sllHllDiscardCounterSupported_{false};
  HwFb303Stats portCounters_;
  bool macsecStatsInited_{false};
  PfcPriorityInfo pfcInfo_;
};

} // namespace facebook::fboss
