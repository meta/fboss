// Copyright 2021- Facebook. All rights reserved.
// This file contains all the function implementation
// that are OSS specific. The counterpart should be
// implemented in Meta specific file.

#include "fboss/platform/fan_service/DataFetcher.h"

#include <folly/logging/xlog.h>

#include "fboss/platform/sensor_service/if/gen-cpp2/SensorServiceThriftAsyncClient.h"
#include "fboss/platform/sensor_service/if/gen-cpp2/sensor_config_types.h"
#include "fboss/platform/sensor_service/if/gen-cpp2/sensor_service_clients.h"
#include "fboss/qsfp_service/lib/QsfpClient.h"

using namespace facebook::fboss;
using namespace facebook::fboss::platform::fan_service;

namespace facebook::fboss::platform::fan_service {

void getTransceivers(
    std::map<int32_t, TransceiverInfo>& cacheTable,
    folly::EventBase& evb) {
  const std::vector<int32_t> ports{};
  auto client = std::move(QsfpClient::createClient(&evb)).getVia(&evb);
  client->sync_getTransceiverInfo(cacheTable, ports);
}

sensor_service::SensorReadResponse getSensorValueThroughThrift(
    int sensorServiceThriftPort,
    folly::EventBase& evb) {
  folly::EventBase evbSensorInternal;
  folly::SocketAddress sockAddr("::1", sensorServiceThriftPort);
  auto socket = folly::AsyncSocket::newSocket(
      &evbSensorInternal, sockAddr, kSensorSendTimeoutMs);
  auto channel =
      apache::thrift::RocketClientChannel::newChannel(std::move(socket));
  auto client = std::make_unique<apache::thrift::Client<
      facebook::fboss::platform::sensor_service::SensorServiceThrift>>(
      std::move(channel));
  sensor_service::SensorReadResponse res;
  try {
    client->sync_getSensorValuesByNames(res, {});
  } catch (std::exception& ex) {
    XLOG(ERR) << "Exception talking to sensor_service. " << ex.what();
  }
  return res;
}

asic_temp::AsicTempResponse getAsicTempThroughSensorService(
    int sensorServiceThriftPort,
    folly::EventBase& evb) {
  asic_temp::AsicTempResponse res;
  auto sensorReadResponse =
      getSensorValueThroughThrift(sensorServiceThriftPort, evb);
  res.timeStamp() = *sensorReadResponse.timeStamp();
  for (const auto& sensorData : *sensorReadResponse.sensorData()) {
    if (!sensorData.value() || !sensorData.timeStamp() ||
        *sensorData.sensorType() != sensor_config::SensorType::TEMPERATURE) {
      continue;
    }
    asic_temp::AsicTempData asicTempData;
    asicTempData.name() = *sensorData.name();
    asicTempData.value() = *sensorData.value();
    asicTempData.timeStamp() = *sensorData.timeStamp();
    res.asicTempData()->push_back(std::move(asicTempData));
  }
  return res;
}

} // namespace facebook::fboss::platform::fan_service
