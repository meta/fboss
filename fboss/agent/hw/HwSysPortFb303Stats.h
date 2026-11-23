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

#include "fboss/agent/hw/HwBasePortFb303Stats.h"
#include "fboss/agent/hw/HwFb303Stats.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"

#include "folly/container/F14Map.h"

#include <optional>
#include <string>

namespace facebook::fboss {

class HwSysPortFb303Stats : public HwBasePortFb303Stats {
 public:
  explicit HwSysPortFb303Stats(
      const std::string& portName,
      QueueId2Name queueId2Name = {},
      std::optional<std::string> multiSwitchStatsPrefix = std::nullopt,
      bool initStats = true)
      : HwBasePortFb303Stats(
            portName,
            queueId2Name,
            {} /*enabledPfcPriorities*/,
            std::nullopt /*pfcCfg*/,
            false /*inCongestionDiscardCountSupported*/,
            false /*inCongestionDiscardSeenSupported*/,
            false /*srv6MysidDiscardCounterSupported*/,
            false /*mplsLabelLookupFailCounterSupported*/,
            false /*linkDebounceRetriggerCounterSupported*/,
            false /*sllHllDiscardCounterSupported*/,
            false /*portLatchStatusSupported*/,
            multiSwitchStatsPrefix) {
    portStats_.portName_() = portName;

    if (initStats) {
      reinitStats(std::nullopt);
      initialized_ = true;
    }
  }

  ~HwSysPortFb303Stats() override = default;
  void updateStats(
      const HwSysPortStats& latestStats,
      const std::chrono::seconds& retrievedAt);

  virtual void portNameChanged(const std::string& newName) override {
    portStats_.portName_() = newName;
    if (initialized_) {
      HwBasePortFb303Stats::portNameChanged(newName);
    }
  }

  virtual void queueChanged(int queueId, const std::string& queueName)
      override {
    if (initialized_) {
      HwBasePortFb303Stats::queueChanged(queueId, queueName);
    } else {
      queueId2Name_[queueId] = queueName;
    }
  }

  virtual void queueRemoved(int queueId) override {
    if (initialized_) {
      HwBasePortFb303Stats::queueRemoved(queueId);
    } else {
      queueId2Name_.erase(queueId);
    }
  }

  const HwSysPortStats& portStats() const {
    return portStats_;
  }

  std::chrono::seconds timeRetrieved() const {
    return timeRetrieved_;
  }

  const std::vector<folly::StringPiece>& kPortMonotonicCounterStatKeys()
      const override;
  const std::vector<folly::StringPiece>& kPortFb303CounterStatKeys()
      const override;
  const std::vector<folly::StringPiece>& kQueueMonotonicCounterStatKeys()
      const override;
  const std::vector<folly::StringPiece>& kQueueFb303CounterStatKeys()
      const override;
  const std::vector<folly::StringPiece>& kInMacsecPortMonotonicCounterStatKeys()
      const override;
  const std::vector<folly::StringPiece>&
  kOutMacsecPortMonotonicCounterStatKeys() const override;
  const std::vector<folly::StringPiece>& kPfcMonotonicCounterStatKeys()
      const override;
  const std::vector<folly::StringPiece>& kPfcDeadlockMonotonicCounterStatKeys()
      const override;
  const std::vector<folly::StringPiece>&
  kPriorityGroupMonotonicCounterStatKeys() const override;
  const std::vector<folly::StringPiece>& kPriorityGroupCounterStatKeys()
      const override;

 private:
  std::chrono::seconds timeRetrieved_{0};
  HwSysPortStats portStats_;
  bool initialized_{false};
};

} // namespace facebook::fboss
