// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <string>

#include "fboss/cli/fboss2/session/SystemdInterface.h"

namespace facebook::fboss {

// Restarts of systemd units. Knows nothing about which service a unit
// belongs to.
class SystemdSequencer {
 public:
  SystemdSequencer();
  explicit SystemdSequencer(std::unique_ptr<SystemdInterface> systemd);

  // Restarts one unit and waits until it is active.
  void restartAndWait(const std::string& unit);

  // The classic unit name if that unit is installed, otherwise the one
  // installed unit matching netosPattern. Throws if neither exists or the
  // pattern matches several.
  std::string resolveUnit(
      const std::string& classic,
      const std::string& netosPattern) const;

 private:
  std::unique_ptr<SystemdInterface> systemd_;
};

} // namespace facebook::fboss
