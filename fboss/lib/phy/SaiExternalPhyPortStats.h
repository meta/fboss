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

#include "fboss/lib/phy/ExternalPhy.h"
#include "fboss/lib/phy/ExternalPhyPortStatsUtils.h"

namespace facebook::fboss {

// Concrete ExternalPhyPortStatsUtils for SaiPhyManager-based XPHYs (e.g. the
// Broadcom Agera3 retimer on Ladakh/Leh800bcls). Unlike NullPortStats it does
// NOT override updateXphyStats(), so it reuses the base implementation that
// publishes the common XPHY FEC counters
// (<prefix>.xphy.{system,line}.fec_{correctable,uncorrectable}) and per-lane
// signal_detect/cdr_lock to fb303. PRBS lane stats are served directly through
// SaiPhyManager's SAI attribute path (getPortPrbsStats), so the PRBS hooks here
// are intentionally no-ops.
class SaiExternalPhyPortStats : public ExternalPhyPortStatsUtils {
 public:
  explicit SaiExternalPhyPortStats(std::string prefix)
      : ExternalPhyPortStatsUtils(std::move(prefix)) {}

  std::vector<phy::PrbsLaneStats> getPrbsStats(
      phy::Side /* side */) const override {
    return {};
  }

 private:
  void updateLanePrbsStats(
      phy::Side /* side */,
      LaneID /* lane */,
      const phy::ExternalPhyLaneStats& /* laneStats */) override {}
};

} // namespace facebook::fboss
