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

#include <sys/types.h>
#include "common/stats/MonotonicCounter.h"

#include <optional>
#include <string>
#include "fboss/agent/FbossError.h"
#include "folly/container/F14Map.h"

namespace facebook::fboss {

struct HwFb303Counter {
  facebook::stats::MonotonicCounter fb303Counter;
  int64_t cumulativeValue{0};
  explicit HwFb303Counter(facebook::stats::MonotonicCounter counter)
      : fb303Counter(std::move(counter)) {}
};

class HwFb303Stats {
 public:
  explicit HwFb303Stats(std::optional<std::string> multiSwitchStatsPrefix)
      : multiSwitchStatsPrefix_(multiSwitchStatsPrefix) {}
  ~HwFb303Stats();

  int64_t getCounterLastIncrement(
      const std::string& statName,
      std::optional<int64_t> defaultVal) const;

  /*
   * Reinit stat
   */
  void reinitStat(
      const std::string& statName,
      std::optional<std::string> oldStatName);
  void updateStat(
      const std::chrono::seconds& now,
      const std::string& statName,
      int64_t val);
  void removeStat(const std::string& statName);
  const std::string getMonotonicCounterName(const std::string& statName) const;
  uint64_t getCumulativeValueIf(const std::string& statName) const;
  void clearStat(const std::string& statName);

 private:
  /*
   * Update queue stat
   */
  facebook::stats::MonotonicCounter* getCounterIf(const std::string& statName);
  const facebook::stats::MonotonicCounter* getCounterIf(
      const std::string& statName) const;

  folly::F14FastMap<std::string, HwFb303Counter> counters_;
  std::optional<std::string> multiSwitchStatsPrefix_;
};
} // namespace facebook::fboss
