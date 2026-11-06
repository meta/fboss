// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"

#include "fboss/agent/hw/HwSwitchFb303Stats.h"

namespace facebook::fboss {

void logDropBitmapReasons(const HwSwitchDropBitmapStats& /*stats*/) {}

void publishSwitchWatermarks(HwSwitchWatermarkStats& /*watermarkStats*/) {}

void publishSwitchPipelineStats(HwSwitchPipelineStats& /*pipelineStats*/) {}

void publishSwitchTemperatureStats(
    HwSwitchTemperatureStats& /*temperatureStats*/) {}

} // namespace facebook::fboss
