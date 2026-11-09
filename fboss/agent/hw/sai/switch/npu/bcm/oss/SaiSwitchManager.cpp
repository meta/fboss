// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"

#include "fboss/agent/hw/HwSwitchFb303Stats.h"

extern "C" {
#include <experimental/saiswitchextensions.h>
}

namespace facebook::fboss {

void publishSwitchWatermarks(HwSwitchWatermarkStats& /*watermarkStats*/) {}

void publishSwitchPipelineStats(HwSwitchPipelineStats& /*pipelineStats*/) {}

void publishSwitchTemperatureStats(
    HwSwitchTemperatureStats& /*temperatureStats*/) {}

// Decoding the raw reason codes needs the vendor tables, which are not open
// sourced, so drop them rather than CHECK the lists are empty: on XGS >= 15.0
// the caller really does populate them.
void logDropReasons(
    const std::vector<sai_int32_t>& /*ingressDropReasons*/,
    const std::vector<sai_int32_t>& /*egressDropReasons*/) {}

} // namespace facebook::fboss
