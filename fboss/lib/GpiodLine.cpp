/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/lib/GpiodLine.h"
#include <folly/String.h>

namespace facebook::fboss {

GpiodLine::GpiodLine(
    struct gpiod_chip* chip,
    uint32_t offset,
    const std::string& name)
    : name_(name) {
  if (nullptr == chip) {
    throw GpiodLineError(
        fmt::format(
            "GpiodLineTrace: Invalid gpiod_chip* passed in for {}", name_));
  }
  line_ = gpiod_chip_get_line(chip, offset);
  if (nullptr == line_) {
    throw GpiodLineError(
        fmt::format(
            "GpiodLineTrace: gpiod_chip_get_line() failed to open {}", name_));
  }
}

GpiodLine::~GpiodLine() {
  gpiod_line_release(line_); // Release GPIO line
}

int GpiodLine::getValue() {
  if (gpiod_line_request_input(line_, name_.c_str()) != 0) {
    throw GpiodLineError(
        fmt::format(
            "GpiodLineTrace: gpiod_line_request_input() failed to set {} direction to input",
            name_));
  }

  int val = gpiod_line_get_value(line_); // Read the line status

  if (-1 == val) {
    throw GpiodLineError(
        fmt::format(
            "GpiodLineTrace: gpiod_line_get_value() failed to get {} status, errno = {}",
            name_,
            folly::errnoStr(errno)));
  }

  return val;
}

int GpiodLine::getValuePreservingDirection() {
  struct gpiod_line_request_config config = {
      .consumer = name_.c_str(),
      .request_type = GPIOD_LINE_REQUEST_DIRECTION_AS_IS,
      .flags = 0};

  if (gpiod_line_request(line_, &config, 0) != 0) {
    throw GpiodLineError(
        fmt::format(
            "GpiodLineTrace: gpiod_line_request() failed for {}", name_));
  }

  int val = gpiod_line_get_value(line_); // Read the line value

  if (-1 == val) {
    throw GpiodLineError(
        fmt::format(
            "GpiodLineTrace: gpiod_line_get_value() failed to get {} value, errno = {}",
            name_,
            folly::errnoStr(errno)));
  }

  return val;
}

void GpiodLine::setValue(int defaultVal, int targetVal) {
  if (!gpiod_line_is_used(line_)) {
    if (gpiod_line_request_output(line_, name_.c_str(), defaultVal) != 0) {
      throw GpiodLineError(
          fmt::format(
              "GpiodLineTrace: gpiod_line_request_output() failed to set {} direction to output",
              name_));
    }
  }

  if (gpiod_line_set_value(line_, targetVal) != 0) {
    throw GpiodLineError(
        fmt::format(
            "GpiodLineTrace: gpiod_line_set_value() failed to set {} status, errno = {}",
            name_,
            folly::errnoStr(errno)));
  }
}

} // namespace facebook::fboss
