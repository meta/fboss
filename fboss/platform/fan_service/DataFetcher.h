// Copyright 2021- Facebook. All rights reserved.

// This include file is used by both MetaHelper.cpp and OssHelper.cpp,
// to define functions that are implemented differently in Meta in OSS.
#pragma once

#include <folly/io/async/EventBase.h>
#include "fboss/agent/if/gen-cpp2/asic_temp_types.h"
#include "fboss/platform/sensor_service/if/gen-cpp2/sensor_service_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"

namespace facebook::fboss::platform::fan_service {
constexpr int kSensorSendTimeoutMs = 5000;
constexpr int kSensorConnTimeoutMs = 2000;

void getTransceivers(
    std::map<int32_t, facebook::fboss::TransceiverInfo>& cacheTable,
    folly::EventBase& evb);

sensor_service::SensorReadResponse getSensorValueThroughThrift(
    int sensorServiceThriftPort,
    folly::EventBase& evb);

facebook::fboss::asic_temp::AsicTempResponse getAsicTempThroughSensorService(
    int sensorServiceThriftPort,
    folly::EventBase& evb);

} // namespace facebook::fboss::platform::fan_service
