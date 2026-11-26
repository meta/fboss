// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/util/qsfp/QsfpUtilContainer.h"
#include <folly/Singleton.h>

namespace facebook::fboss {

folly::Singleton<QsfpUtilContainer> _qsfpUtilContainer;

std::shared_ptr<QsfpUtilContainer> QsfpUtilContainer::getInstance() {
  return _qsfpUtilContainer.try_get();
}

TransceiverI2CApi* QsfpUtilContainer::getTransceiverBus() {
  // call_once retries on a later call if detection throws.
  std::call_once(busOnce_, [this] { detectTransceiverBus(); });
  CHECK(bus_);
  return bus_.get();
}

/*
 * This function gets the transceiver bus.
 */
void QsfpUtilContainer::detectTransceiverBus() {
  auto busAndError = getTransceiverAPI();

  if (busAndError.second) {
    throw FbossError(
        fmt::format("Unknown platform error: {:d}", busAndError.second));
  }

  bus_ = std::move(busAndError.first);
}

} // namespace facebook::fboss
