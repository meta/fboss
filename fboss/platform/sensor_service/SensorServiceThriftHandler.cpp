/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/sensor_service/SensorServiceThriftHandler.h"

#include <vector>

#include <folly/logging/xlog.h>
#include <gflags/gflags.h>

#include "fboss/lib/LogThriftCall.h"
#include "fboss/platform/reboot_cause_finder/RebootHistory.h"
#include "fboss/platform/reboot_cause_finder/if/gen-cpp2/reboot_cause_config_constants.h"
#include "fboss/platform/sensor_service/Utils.h"

DEFINE_string(
    reboot_history_dir,
    facebook::fboss::platform::reboot_cause_config::
        reboot_cause_config_constants::REBOOT_HISTORY_DIR(),
    "Directory holding reboot_cause_finder's per-boot records. Overridable "
    "for tests.");

namespace facebook::fboss::platform::sensor_service {

void SensorServiceThriftHandler::getSensorValuesByNames(
    SensorReadResponse& response,
    std::unique_ptr<std::vector<std::string>> request) {
  auto log = LOG_THRIFT_CALL(DBG1);

  response.timeStamp() = Utils::nowInSecs();

  // Request list is not empty
  if (!request->empty()) {
    response.sensorData() = sensorServiceImpl_->getSensorsData(*request);
    return;
  }

  // Request list is empty, we send all the sensor data
  for (const auto& sensorDataItr : sensorServiceImpl_->getAllSensorData()) {
    response.sensorData()->push_back(sensorDataItr.second);
  }
}

// Reads a directory written by a different process on its own schedule. Every
// failure here is someone else's partial write, a stale file, or a full disk --
// none of which is a reason for this RPC, let alone sensor_service, to fail.
// So: no exception escapes, and anything unreadable is counted rather than
// thrown.
void SensorServiceThriftHandler::getRebootHistory(
    RebootHistoryResponse& response) {
  response.records() = {};
  response.skippedCount() = 0;
  response.warnings() = {};

  // Filled in place, so a throw below still returns whatever was read first.
  reboot_cause_finder::RebootHistory history;
  try {
    reboot_cause_finder::readRebootHistory(FLAGS_reboot_history_dir, history);
  } catch (const std::exception& ex) {
    // Backstop. Nothing above is expected to throw, but this RPC must not be
    // the reason sensor_service returns an error.
    XLOG(ERR) << "getRebootHistory failed: " << ex.what();
    reboot_cause_finder::addWarning(
        history, "Reboot history could not be read");
  } catch (...) {
    XLOG(ERR) << "getRebootHistory failed with a non-standard exception";
    reboot_cause_finder::addWarning(
        history, "Reboot history could not be read");
  }
  response.records() = std::move(history.records);
  response.skippedCount() = history.skippedCount;
  response.warnings() = std::move(history.warnings);
}

} // namespace facebook::fboss::platform::sensor_service
