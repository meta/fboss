/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include "fboss/platform/sensor_service/SensorServiceImpl.h"
#include "fboss/platform/sensor_service/if/gen-cpp2/SensorServiceThrift.h"

#include <memory>
#include <string>
#include <vector>

namespace facebook::fboss::platform::sensor_service {

class SensorServiceThriftHandler : public SensorServiceThriftSvIf {
 public:
  explicit SensorServiceThriftHandler(
      std::shared_ptr<SensorServiceImpl> sensorServiceImpl)
      : sensorServiceImpl_(std::move(sensorServiceImpl)) {}

  void getSensorValuesByNames(
      SensorReadResponse& response,
      std::unique_ptr<std::vector<std::string>> request) override;

  void getRebootHistory(RebootHistoryResponse& response) override;

  SensorServiceImpl* getServiceImpl() {
    return sensorServiceImpl_.get();
  }

 private:
  std::shared_ptr<SensorServiceImpl> sensorServiceImpl_;
};
} // namespace facebook::fboss::platform::sensor_service
