// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <folly/init/Init.h>
#include <gtest/gtest.h>

#include "fboss/qsfp_service/module/properties/TransceiverPropertiesManager.h"
#include "fboss/qsfp_service/test/hal_test/HalTest.h"

DEFINE_string(
    hal_test_transceiver_properties_config,
    "",
    "Path to transceiver_properties.json config file for HAL tests. "
    "If empty, uses the built-in default config.");

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  const folly::Init init(&argc, &argv);

  if (!FLAGS_hal_test_transceiver_properties_config.empty()) {
    facebook::fboss::TransceiverPropertiesManager::init(
        FLAGS_hal_test_transceiver_properties_config);
  } else {
    facebook::fboss::TransceiverPropertiesManager::initDefault();
  }
  facebook::fboss::registerApplicationModeTests();

  return RUN_ALL_TESTS();
}
