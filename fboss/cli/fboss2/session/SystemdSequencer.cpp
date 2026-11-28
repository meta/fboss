// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/session/SystemdSequencer.h"

#include <fmt/format.h>
#include <folly/Range.h>
#include <folly/String.h>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
constexpr std::string_view kSystemdServiceSuffix = ".service";
} // namespace

namespace facebook::fboss {

SystemdSequencer::SystemdSequencer()
    : systemd_(std::make_unique<SystemdInterface>()) {}

SystemdSequencer::SystemdSequencer(std::unique_ptr<SystemdInterface> systemd)
    : systemd_(std::move(systemd)) {}

void SystemdSequencer::restartAndWait(const std::string& unit) {
  systemd_->restartService(unit);
  systemd_->waitForServiceActive(unit);
}

std::string SystemdSequencer::resolveUnit(
    const std::string& classic,
    const std::string& netosPattern) const {
  if (!systemd_
           ->getMatchingServices(
               fmt::format("{}{}", classic, kSystemdServiceSuffix))
           .empty()) {
    return classic;
  }

  auto matches = systemd_->getMatchingServices(
      fmt::format("{}{}", netosPattern, kSystemdServiceSuffix));
  if (matches.size() > 1) {
    throw std::runtime_error(
        fmt::format(
            "Found multiple systemd services matching {}: {}",
            netosPattern,
            folly::join(", ", matches)));
  }
  if (matches.size() == 1) {
    auto resolved = std::move(matches.front());
    if (folly::StringPiece(resolved).endsWith(kSystemdServiceSuffix)) {
      resolved.resize(resolved.size() - kSystemdServiceSuffix.size());
    }
    return resolved;
  }
  throw std::runtime_error(
      fmt::format(
          "No systemd service found for {} and {}", classic, netosPattern));
}

} // namespace facebook::fboss
