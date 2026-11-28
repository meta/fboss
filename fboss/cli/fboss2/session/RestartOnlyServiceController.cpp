// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/session/RestartOnlyServiceController.h"

#include <folly/logging/xlog.h>
#include <utility>

namespace facebook::fboss {

RestartOnlyServiceController::RestartOnlyServiceController(
    std::string unit,
    std::optional<std::string> netosUnit)
    : unit_(std::move(unit)), netosUnit_(std::move(netosUnit)) {}

RestartOnlyServiceController::RestartOnlyServiceController(
    std::string unit,
    std::optional<std::string> netosUnit,
    std::unique_ptr<SystemdInterface> systemd)
    : unit_(std::move(unit)),
      netosUnit_(std::move(netosUnit)),
      sequencer_(std::move(systemd)) {}

std::vector<std::string> RestartOnlyServiceController::restart() {
  const auto unit =
      netosUnit_ ? sequencer_.resolveUnit(unit_, *netosUnit_) : unit_;
  XLOG(DBG1) << "Restarting " << unit << " (restart)...";
  sequencer_.restartAndWait(unit);
  return {unit};
}

} // namespace facebook::fboss
