#include "fboss/platform/bsp_tests/utils/GpioUtils.h"

#include <gpiod.h>
#include <cerrno>
#include <memory>
#include <stdexcept>
#include <string>

#include <fmt/format.h>
#include <folly/String.h>
#include <folly/logging/xlog.h>

#include "fboss/lib/GpiodLine.h"

namespace facebook::fboss::platform::bsp_tests {

namespace {

struct GpiodChipCloser {
  void operator()(gpiod_chip* chip) const {
    gpiod_chip_close(chip);
  }
};
using GpiodChipPtr = std::unique_ptr<gpiod_chip, GpiodChipCloser>;

struct GpiodChipIterFreer {
  void operator()(gpiod_chip_iter* iter) const {
    gpiod_chip_iter_free(iter);
  }
};
using GpiodChipIterPtr = std::unique_ptr<gpiod_chip_iter, GpiodChipIterFreer>;

GpiodChipPtr openChip(const std::string& name) {
  GpiodChipPtr chip{gpiod_chip_open_lookup(name.c_str())};
  if (!chip) {
    throw std::runtime_error(
        fmt::format(
            "Failed to open GPIO chip {}: {}", name, folly::errnoStr(errno)));
  }
  return chip;
}

} // namespace

std::vector<GpioDetectResult> GpioUtils::gpiodetect(const std::string& label) {
  std::vector<GpioDetectResult> results;

  GpiodChipIterPtr iter{gpiod_chip_iter_new()};
  if (!iter) {
    XLOG(ERR) << "Failed to enumerate GPIO chips: " << folly::errnoStr(errno);
    return results;
  }

  gpiod_chip* chip = nullptr;
  gpiod_foreach_chip(iter.get(), chip) {
    std::string chipLabel = gpiod_chip_label(chip);
    // If label is provided, only add devices with matching label
    if (label.empty() || chipLabel == label) {
      results.push_back(
          {gpiod_chip_name(chip),
           chipLabel,
           static_cast<int>(gpiod_chip_num_lines(chip))});
    }
  }

  return results;
}

std::vector<GpioInfoResult> GpioUtils::gpioinfo(const std::string& name) {
  std::vector<GpioInfoResult> results;

  try {
    auto chip = openChip(name);
    const unsigned int numLines = gpiod_chip_num_lines(chip.get());
    for (unsigned int offset = 0; offset < numLines; ++offset) {
      gpiod_line* line = gpiod_chip_get_line(chip.get(), offset);
      if (!line) {
        throw std::runtime_error(
            fmt::format(
                "Failed to get line {} of {}: {}",
                offset,
                name,
                folly::errnoStr(errno)));
      }
      const char* lineName = gpiod_line_name(line);
      results.push_back(
          {lineName ? lineName : "unnamed",
           static_cast<int>(offset),
           gpiod_line_direction(line) == GPIOD_LINE_DIRECTION_OUTPUT
               ? "output"
               : "input"});
    }
  } catch (const std::exception& e) {
    XLOG(ERR) << "Error in gpioinfo: " << e.what();
  }

  return results;
}

int GpioUtils::gpioget(const std::string& name, int line) {
  try {
    auto chip = openChip(name);
    return GpiodLine(chip.get(), line, "bsp_tests").getValue();
  } catch (const std::exception& e) {
    XLOG(ERR) << "Error in gpioget: " << e.what();
    throw std::runtime_error(
        fmt::format(
            "Failed to get GPIO value for {}-{}: {}", name, line, e.what()));
  }
}

} // namespace facebook::fboss::platform::bsp_tests
