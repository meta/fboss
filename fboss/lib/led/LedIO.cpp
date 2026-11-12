/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/lib/led/LedIO.h"
#include <folly/Conv.h>
#include <folly/logging/xlog.h>
#include <fstream>
#include <string>
#include "fboss/led_service/LedUtils.h"
#include "fboss/lib/led/gen-cpp2/led_mapping_types.h"

namespace facebook::fboss {

LedIO::LedIO(LedMapping ledMapping) : id_(*ledMapping.id()) {
  if (!ledMapping.bluePath().has_value() ||
      !ledMapping.yellowPath().has_value()) {
    throw LedIOError(
        fmt::format("No color path set for ID {:d} (0 base)", id_));
  }
  bluePath_ = ledMapping.bluePath().value();
  yellowPath_ = ledMapping.yellowPath().value();
  initMaxBrightness(bluePath_, blueMaxBrightness_);
  initMaxBrightness(yellowPath_, yellowMaxBrightness_);
  init();
}

void LedIO::initMaxBrightness(
    const std::string& path,
    std::string& maxBrightness) {
  std::fstream fs;
  auto maxBrightnessPath = path + kLedMaxBrightnessPath;
  fs.open(maxBrightnessPath, std::fstream::in);
  if (fs.is_open()) {
    fs >> maxBrightness;
    fs.close();
  } else {
    throw LedIOError(
        fmt::format(
            "initMaxBrightness() failed to open {} for ID {:d} (0 base)",
            maxBrightnessPath,
            id_));
  }
  auto value = folly::to<int>(maxBrightness);
  CHECK((value >= kMinBrightness) && (value <= kMaxBrightness)) << fmt::format(
      "Value of brightness is not in range {} {}",
      kMinBrightness,
      kMaxBrightness);
}

void LedIO::setLedState(led::LedState ledState) {
  if (currState_ == ledState) {
    return;
  }
  auto toSetColor = ledState.ledColor().value();

  // Turn off all LEDs first
  turnOffAllLeds();
  switch (toSetColor) {
    case led::LedColor::BLUE:
      blueOn(ledState.blink().value());
      break;
    case led::LedColor::YELLOW:
      yellowOn(ledState.blink().value());
      break;
    case led::LedColor::OFF:
      // Leds already turned off earlier before switch
      break;
    default:
      throw LedIOError(
          fmt::format("setLedState() invalid color for ID {:d} (0 base)", id_));
  }
  currState_ = ledState;
}

led::LedState LedIO::getLedState() const {
  return currState_;
}

void LedIO::init() {
  led::LedState ledState =
      utility::constructLedState(led::LedColor::OFF, led::Blink::OFF);
  currState_ = ledState;
  blueOff();
  yellowOff();
}

void LedIO::blueOn(led::Blink blink) {
  setBlink(bluePath_, blink);
  setLed(bluePath_, blueMaxBrightness_);
  XLOG(INFO) << fmt::format(
      "Trace: set LED {:d} (0 base) to Blue and blink {:s}",
      id_,
      apache::thrift::util::enumNameSafe(blink));
}

void LedIO::blueOff() {
  setBlink(bluePath_, led::Blink::OFF);
  setLed(bluePath_, kLedOff);
}

void LedIO::yellowOn(led::Blink blink) {
  setBlink(yellowPath_, blink);
  setLed(yellowPath_, yellowMaxBrightness_);
  XLOG(INFO) << fmt::format(
      "Trace: set LED {:d} (0 base) to Yellow and blink {:s}",
      id_,
      apache::thrift::util::enumNameSafe(blink));
}

void LedIO::yellowOff() {
  setBlink(yellowPath_, led::Blink::OFF);
  setLed(yellowPath_, kLedOff);
}

void LedIO::setLed(const std::string& ledBasePath, const std::string& ledOp) {
  std::string ledPath = ledBasePath + kLedBrightnessPath;
  std::fstream fs;
  fs.open(ledPath, std::fstream::out);

  if (fs.is_open()) {
    fs << ledOp;
    fs.close();
  } else {
    throw LedIOError(
        fmt::format(
            "setLed() failed to open {} for ID {:d} (0 base)", ledPath, id_));
  }
}

void LedIO::setDelay(const std::string& ledBasePath, const std::string& delay) {
  // Write to delay_on and delay_off
  std::fstream fsOn, fsOff;
  std::string ledPathOn = ledBasePath + kLedDelayOnPath;
  std::string ledPathOff = ledBasePath + kLedDelayOffPath;
  fsOn.open(ledPathOn, std::fstream::out);
  fsOff.open(ledPathOff, std::fstream::out);
  if (fsOn.is_open() && fsOff.is_open()) {
    fsOn << delay;
    fsOff << delay;
    fsOn.close();
    fsOff.close();
  } else {
    // Not throwing an exception here until all existing BSPs support
    // blinking
    XLOG(ERR) << fmt::format(
        "setDelay() failed to open {} or {} for ID {:d} (0 base)",
        ledPathOn,
        ledPathOff,
        id_);
    return;
  }
}

void LedIO::setBlink(const std::string& ledBasePath, led::Blink blink) {
  // Enable writes to delay_on and delay_off by enabling the timer trigger if
  // required
  std::fstream fsTrigger;
  {
    std::string ledTriggerPath = ledBasePath + kLedTriggerPath;
    fsTrigger.open(ledTriggerPath, std::fstream::out);

    if (!fsTrigger.is_open()) {
      // Not throwing an exception here until all existing BSPs support
      // blinking
      XLOG(ERR) << fmt::format(
          "setBlink() failed to open {} for ID {:d} (0 base)",
          ledTriggerPath,
          id_);
      return;
    }
  }

  std::string delayOn, delayOff;
  // Compute delay_on and delay_off
  switch (blink) {
    case led::Blink::OFF:
    case led::Blink::UNKNOWN:
      // Disabling the trigger will automatically disable the blinking.
      fsTrigger << kLedTimerTriggerOff;
      break;
    case led::Blink::SLOW:
      fsTrigger << kLedTimerTrigger;
      fsTrigger.flush();
      setDelay(ledBasePath, kLedBlinkSlow);
      break;
    case led::Blink::FAST:
      fsTrigger << kLedTimerTrigger;
      fsTrigger.flush();
      setDelay(ledBasePath, kLedBlinkFast);
      break;
  }
}

void LedIO::turnOffAllLeds() {
  blueOff();
  yellowOff();
}

} // namespace facebook::fboss
