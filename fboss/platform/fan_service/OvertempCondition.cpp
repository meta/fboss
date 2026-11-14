// Copyright 2025- Facebook. All rights reserved.

#include "fboss/platform/fan_service/OvertempCondition.h"
#include <folly/logging/xlog.h>
#include "fboss/agent/FbossError.h"

namespace facebook::fboss::platform::fan_service {
std::vector<std::string> OvertempCondition::setupShutdownConditions(
    const FanServiceConfig& config) {
  std::vector<std::string> watchList;
  if (!config.shutdownCondition()) {
    return watchList;
  }
  setNumOvertempSensorForShutdown(
      *config.shutdownCondition()->numOvertempSensorForShutdown());
  if (numOvertempForShutdown <= 0) {
    XLOG(ERR) << "Invalid number of overtemp sensor for shutdown: "
              << numOvertempForShutdown;
    throw std::runtime_error("Invalid number of overtemp sensor for shutdown");
  }
  for (const auto& condition : *config.shutdownCondition()->conditions()) {
    if (entries.contains(*condition.sensorName())) {
      throw facebook::fboss::FbossError(
          "Duplicate sensor name in overtemp condition is not allowed!");
    }
    watchList.push_back(*condition.sensorName());
    int windowSize =
        (condition.slidingWindowSize() ? *condition.slidingWindowSize() : 1);
    addSensorForTracking(
        *condition.sensorName(), *condition.overtempThreshold(), windowSize);
  }
  return watchList;
}

void OvertempCondition::setNumOvertempSensorForShutdown(int number) {
  numOvertempForShutdown = number;
}

void OvertempCondition::addSensorForTracking(
    std::string name,
    float threshold,
    int windowSize) {
  entries[name].sensorName = name;
  entries[name].overtempThreshold = threshold;
  entries[name].slidingWindowSize = windowSize;
}

void OvertempCondition::processSensorData(
    std::string sensorName,
    float sensorValue) {
  if (!isTracked(sensorName)) {
    throw std::runtime_error(
        "Unregistered sensor name is given for overtemp processing! :" +
        sensorName);
  }
  auto& condition = entries[sensorName];
  if (condition.length < condition.slidingWindowSize) {
    // If circular queue is not full, fill it up
    condition.length += 1;
    condition.sensorReads.push_back(sensorValue);
  } else {
    // If circular queue is full, remove the next entry and add the new one
    condition.sumTotal -= condition.sensorReads[condition.cursor];
    condition.sensorReads[condition.cursor] = sensorValue;
  }
  condition.sumTotal += sensorValue;
  condition.cursor = (condition.cursor + 1) % condition.slidingWindowSize;
}

bool OvertempCondition::checkIfOvertemp() {
  int overtempCount = 0;
  if (numOvertempForShutdown <= 0) {
    // Overtemp condition is not enabled
    return false;
  }
  for (auto overtempItem = entries.begin(); overtempItem != entries.end();
       overtempItem++) {
    auto& entry = overtempItem->second;
    if (entry.length == 0) {
      continue;
    }
    float movingAverage = entry.sumTotal / entry.length;
    if (movingAverage > entry.overtempThreshold) {
      XLOG(ERR) << fmt::format(
          "Overtemp condition detected for sensor:{} value:{}, entry_count:{}",
          entry.sensorName,
          movingAverage,
          entry.length);
      overtempCount++;
    }
  }
  return overtempCount >= numOvertempForShutdown;
}

bool OvertempCondition::isTracked(const std::string& sensorName) {
  return entries.contains(sensorName);
}
} // namespace facebook::fboss::platform::fan_service
