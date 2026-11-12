// Copyright 2021-present Facebook. All Rights Reserved.

// Handler class handles Fan Service thrift calls.
#pragma once

#include "fboss/platform/fan_service/ControlLogic.h"
#include "fboss/platform/fan_service/if/gen-cpp2/FanService.h"

namespace facebook::fboss::platform::fan_service {
class FanServiceHandler : public apache::thrift::ServiceHandler<FanService> {
 public:
  explicit FanServiceHandler(std::shared_ptr<ControlLogic> controlLogic);

  ~FanServiceHandler() override = default;

  void getFanStatuses(FanStatusesResponse&) override;

 private:
  std::shared_ptr<ControlLogic> controlLogic_{nullptr};
};
} // namespace facebook::fboss::platform::fan_service
