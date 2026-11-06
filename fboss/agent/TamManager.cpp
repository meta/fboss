// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/TamManager.h"

#include <algorithm>

#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/SwitchState.h"

using facebook::fboss::DeltaFunctions::isEmpty;

namespace facebook::fboss {

TamManager::TamManager(SwSwitch* sw)
    : sw_(sw), v4Resolver_(sw), v6Resolver_(sw) {
  sw_->registerStateObserver(this, "TamManager");
}

TamManager::~TamManager() {
  sw_->unregisterStateObserver(this);
}

void TamManager::stateUpdated(const StateDelta& delta) {
  if (!hasMirrorOnDropChanges(delta)) {
    return;
  }

  auto updateReportsFn = [this](const std::shared_ptr<SwitchState>& state) {
    return resolveMirrorOnDropReports(state);
  };
  sw_->updateState("Updating MirrorOnDropReports", updateReportsFn);
}

bool TamManager::hasMirrorOnDropChanges(const StateDelta& delta) {
  if (!sw_->getState()->getMirrorOnDropReports()->numNodes()) {
    return false;
  }

  // Check if MirrorOnDropReports themselves changed
  if (!isEmpty(delta.getMirrorOnDropReportsDelta())) {
    return true;
  }

  return delta.hasRouteOrNeighborChanges();
}

template <typename AddrT, typename ResolverT>
std::pair<std::optional<folly::MacAddress>, std::optional<cfg::PortDescriptor>>
TamManager::resolveCollectorIp(
    ResolverT& resolver,
    const std::shared_ptr<SwitchState>& state,
    const AddrT& ip,
    const HwSwitchMatcher& matcher) {
  std::optional<folly::MacAddress> resolvedMac;
  std::optional<cfg::PortDescriptor> resolvedPort;

  auto nexthops = resolver.resolveNextHops(state, ip);
  for (const auto& nexthop : nexthops) {
    auto neighbor = resolver.resolveNextHopNeighbor(state, ip, nexthop);
    if (!neighbor) {
      continue;
    }
    auto port = resolver.resolveEgressPort(state, neighbor);
    if (!port) {
      continue;
    }
    // A port on another NPU is resolved by that NPU's own copy.
    auto portScope = sw_->getScopeResolver()->scope(state, port.value());
    if (!std::any_of(
            portScope.switchIds().begin(),
            portScope.switchIds().end(),
            [&matcher](const SwitchID& switchId) {
              return matcher.has(switchId);
            })) {
      continue;
    }
    resolvedMac = neighbor->getMac();
    resolvedPort = port->toThrift();
    break;
  }

  return {resolvedMac, resolvedPort};
}

std::shared_ptr<SwitchState> TamManager::resolveMirrorOnDropReports(
    const std::shared_ptr<SwitchState>& state) {
  auto updatedState = state->clone();
  auto mnpuReports = state->getMirrorOnDropReports()->modify(&updatedState);
  bool reportsUpdated = false;

  for (auto mniter = mnpuReports->cbegin(); mniter != mnpuReports->cend();
       ++mniter) {
    auto matcher = HwSwitchMatcher(mniter->first);
    auto& reports = std::as_const(mniter->second);
    for (auto iter = reports->cbegin(); iter != reports->cend(); ++iter) {
      auto report = iter->second;

      // A configured mirrorPortId means the egress port is static (e.g. DNX),
      // so no mirror resolution is needed. Resolution applies only when it is
      // unset (e.g. XGS platforms like TH5).
      if (report->getMirrorPortId().has_value()) {
        continue;
      }

      const auto collectorIp = report->getCollectorIp();

      auto [resolvedMac, resolvedPort] = collectorIp.isV4()
          ? resolveCollectorIp(v4Resolver_, state, collectorIp.asV4(), matcher)
          : resolveCollectorIp(v6Resolver_, state, collectorIp.asV6(), matcher);

      // Check if resolution changed
      bool isNowResolved = resolvedMac.has_value() && resolvedPort.has_value();
      bool wasResolved = report->isResolved();

      if (isNowResolved != wasResolved ||
          (isNowResolved &&
           (resolvedMac != report->getResolvedCollectorMac() ||
            resolvedPort != report->getResolvedEgressPort()))) {
        auto updatedReport = report->clone();
        updatedReport->setIsResolved(isNowResolved);
        if (isNowResolved) {
          updatedReport->setResolvedCollectorMac(resolvedMac.value());
          updatedReport->setResolvedEgressPort(resolvedPort.value());
          XLOG(DBG2) << "MirrorOnDropReport: " << report->getID()
                     << " resolved to MAC=" << resolvedMac.value()
                     << ", port=" << resolvedPort->portId().value();
        } else {
          XLOG(DBG2) << "MirrorOnDropReport: " << report->getID()
                     << " unresolved";
        }
        reports->updateNode(std::move(updatedReport));
        reportsUpdated = true;
      }
    }
  }

  if (!reportsUpdated) {
    updatedState.reset();
  }
  return updatedState;
}

} // namespace facebook::fboss
