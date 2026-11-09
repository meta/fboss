// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/hw/sai/switch/SaiSwitch.h"
#include "fboss/agent/hw/test/HwTestThriftHandler.h"
#include "fboss/agent/state/RouteNextHopEntry.h"

namespace facebook::fboss::utility {

cfg::SwitchingMode HwTestThriftHandler::getFwdSwitchingMode(
    std::unique_ptr<state::RouteNextHopEntry> routeNextHopEntry) {
  auto saiSwitch = static_cast<SaiSwitch*>(hwSwitch_);
  // Reconstruct from the full thrift struct so the NextHopSetID fields survive
  // the RPC; the ID-aware FibHelpers path CHECK-fails on an inline-only entry
  // when resolve_nexthops_from_id is on.
  return saiSwitch->getFwdSwitchingMode(RouteNextHopEntry(*routeNextHopEntry));
}

} // namespace facebook::fboss::utility
