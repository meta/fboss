// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/lib/bsp/BspResetUtils.h"

#include <fmt/format.h>
#include <folly/logging/xlog.h>
#include <unistd.h>
#include "fboss/lib/CommonFileUtils.h"

namespace facebook::fboss {

namespace {
constexpr int kResetHoldDelayUs = 100; // 100 microseconds
} // namespace

bool holdResetViaSysfs(
    const std::optional<std::string>& resetPath,
    const std::string& componentName) {
  if (!resetPath) {
    XLOG(DBG5) << fmt::format(
        "BspResetUtils: {} no reset path for {}, skipping hold",
        __func__,
        componentName);
    return false;
  }

  XLOG(INFO) << fmt::format(
      "BspResetUtils: {} holding {} in reset via {}",
      __func__,
      componentName,
      *resetPath);

  try {
    writeSysfs(*resetPath, "0");
    usleep(kResetHoldDelayUs);
    return true;
  } catch (const std::exception& ex) {
    XLOG(ERR) << fmt::format(
        "BspResetUtils: {} failed to hold reset for {}: {}",
        __func__,
        componentName,
        ex.what());
    return false;
  }
}

bool releaseResetViaSysfs(
    const std::optional<std::string>& resetPath,
    const std::string& componentName) {
  if (!resetPath) {
    XLOG(DBG5) << fmt::format(
        "BspResetUtils: {} no reset path for {}, skipping release",
        __func__,
        componentName);
    return false;
  }

  XLOG(INFO) << fmt::format(
      "BspResetUtils: {} releasing {} from reset via {}",
      __func__,
      componentName,
      *resetPath);

  try {
    writeSysfs(*resetPath, "1");
    return true;
  } catch (const std::exception& ex) {
    XLOG(ERR) << fmt::format(
        "BspResetUtils: {} failed to release reset for {}: {}",
        __func__,
        componentName,
        ex.what());
    return false;
  }
}

} // namespace facebook::fboss
