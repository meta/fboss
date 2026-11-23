// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <optional>
#include <utility>

#include <folly/MacAddress.h>

#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/NextHopResolver.h"
#include "fboss/agent/StateObserver.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/state/StateDelta.h"

namespace facebook::fboss {

class SwSwitch;

/**
 * TamManager (Telemetry and Monitoring Manager)
 *
 * This class manages the resolution of collector IP addresses for Mirror on
 * Drop (MoD) reports. Similar to MirrorManager which resolves ERSPAN mirror
 * destinations, TamManager:
 *
 * 1. Observes state changes (routes, neighbors, MirrorOnDropReports)
 * 2. Resolves collector IPs to MAC addresses and egress ports
 * 3. Updates MirrorOnDropReport state with resolved values
 *
 * The resolved information is then used by SaiTamManager when programming
 * the hardware.
 */
class TamManager : public StateObserver {
 public:
  explicit TamManager(SwSwitch* sw);
  ~TamManager() override;

  TamManager(const TamManager&) = delete;
  TamManager& operator=(const TamManager&) = delete;
  TamManager(TamManager&&) = delete;
  TamManager& operator=(TamManager&&) = delete;

  void stateUpdated(const StateDelta& delta) override;

 private:
  SwSwitch* sw_;
  NextHopResolverV4 v4Resolver_;
  NextHopResolverV6 v6Resolver_;

  // Check if there are changes that require re-resolving MoD reports
  bool hasMirrorOnDropChanges(const StateDelta& delta);

  // Resolve collector IPs for all MirrorOnDropReports
  std::shared_ptr<SwitchState> resolveMirrorOnDropReports(
      const std::shared_ptr<SwitchState>& state);

  // Resolves only over nexthops egressing on a switch in `matcher`.
  template <typename AddrT, typename ResolverT>
  std::
      pair<std::optional<folly::MacAddress>, std::optional<cfg::PortDescriptor>>
      resolveCollectorIp(
          ResolverT& resolver,
          const std::shared_ptr<SwitchState>& state,
          const AddrT& ip,
          const HwSwitchMatcher& matcher);
};

} // namespace facebook::fboss
