// Copyright 2021-present Facebook. All Rights Reserved.

#include <folly/logging/xlog.h>

#include "fboss/platform/fan_service/FanServiceHandler.h"

namespace facebook::fboss::platform::fan_service {

FanServiceHandler::FanServiceHandler(std::shared_ptr<ControlLogic> controlLogic)
    : controlLogic_(std::move(controlLogic)) {
  XLOG(INFO) << "FanServiceHandler Started";
}

void FanServiceHandler::getFanStatuses(FanStatusesResponse& response) {
  response.fanStatuses() = controlLogic_->getFanStatuses();
}

} // namespace facebook::fboss::platform::fan_service
