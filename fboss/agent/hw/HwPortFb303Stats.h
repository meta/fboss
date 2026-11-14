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
#include <ostream>
#include <string>

namespace facebook::fboss {

class HwPortFb303Stats : public HwBasePortFb303Stats {
 public:
  explicit HwPortFb303Stats(
      const std::string& portName,
      QueueId2Name queueId2Name = {},
      const std::vector<PfcPriority>& enabledPfcPriorities = {},
      const std::optional<cfg::PortPfc>& pfcCfg = std::nullopt,
      bool inCongestionDiscardCountSupported = false,
      bool inCongestionDiscardSeenSupported = false,
      bool srv6MysidDiscardCounterSupported = false,
      bool mplsLabelLookupFailCounterSupported = false,
      bool linkDebounceRetriggerCounterSupported = false,
      bool sllHllDiscardCounterSupported = false,
      bool portLatchStatusSupported = false)
      : HwBasePortFb303Stats(
            portName,
            std::move(queueId2Name),
            enabledPfcPriorities,
            pfcCfg,
            inCongestionDiscardCountSupported,
            inCongestionDiscardSeenSupported,
            srv6MysidDiscardCounterSupported,
            mplsLabelLookupFailCounterSupported,
            linkDebounceRetriggerCounterSupported,
            sllHllDiscardCounterSupported,
            portLatchStatusSupported) {
    portStats_.portName_() = portName;
    reinitStats(std::nullopt);
  }

  ~HwPortFb303Stats() override = default;
  void updateStats(
      const HwPortStats& latestStats,
      const std::chrono::seconds& retrievedAt);
  void updateLinkDownDuringWarmBootCounter(bool linkDownDuringWarmBoot);

  virtual void portNameChanged(const std::string& newName) override {
    portStats_.portName_() = newName;
    HwBasePortFb303Stats::portNameChanged(newName);
  }

  const HwPortStats& portStats() const {
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
  HwPortStats portStats_;
  std::chrono::seconds timeRetrieved_{0};
};

} // namespace facebook::fboss
