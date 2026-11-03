// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/platform/bsp_tests/BspTestEnvironment.h"

#include <folly/FileUtil.h>
#include <folly/logging/xlog.h>

#include "fboss/platform/bsp_tests/RuntimeConfigBuilder.h"
#include "fboss/platform/config_lib/ConfigLib.h"
#include "fboss/platform/helpers/PlatformNameLib.h"

DEFINE_string(
    bsp_tests_config_file,
    "",
    "[OSS-Only] Path to bsp_tests.json file.");
DEFINE_string(
    platform_manager_config_file,
    "",
    "[OSS-Only] Path to platform_manager.json file.");

namespace facebook::fboss::platform::bsp_tests {

// Initialize the static instance pointer
BspTestEnvironment* BspTestEnvironment::instance_ = nullptr;

BspTestEnvironment::~BspTestEnvironment() {}

void BspTestEnvironment::SetUp() {
  if (platform_.empty()) {
    platform_ =
        helpers::PlatformNameLib().getPlatformName().value_or("unknown");
    XLOG(INFO) << "Platform: " << platform_;
  }

  if (!testConfig_.has_value()) {
    std::string bspTestsConfigJson;
    if (FLAGS_bsp_tests_config_file.empty()) {
      bspTestsConfigJson = ConfigLib().getBspTestConfig();
    } else {
      folly::readFile(FLAGS_bsp_tests_config_file.c_str(), bspTestsConfigJson);
    }
    // Load bsp test config
    testConfig_ = loadTestConfig(platform_, bspTestsConfigJson);
  }

  std::string pmConfigJson;
  if (!platformManagerConfig_.has_value()) {
    if (FLAGS_platform_manager_config_file.empty()) {
      pmConfigJson = ConfigLib().getPlatformManagerConfig();
    } else {
      folly::readFile(FLAGS_platform_manager_config_file.c_str(), pmConfigJson);
    }
    // Load platform manager configuration
    platformManagerConfig_ = loadPlatformManagerConfig(platform_, pmConfigJson);
  }

  if (!pkgManager_) {
    pkgManager_ =
        std::make_unique<platform_manager::PkgManager>(*platformManagerConfig_);
    kmods_ = pkgManager_->readKmodsFile();
  }

  if (!runtimeConfig_.has_value()) {
    // Build runtime configuration using RuntimeConfigBuilder
    RuntimeConfigBuilder configBuilder;
    runtimeConfig_ = configBuilder.buildRuntimeConfig(
        *testConfig_, *platformManagerConfig_, kmods_, platform_);
    // The default config is enough to reach every IDPROM: no
    // versionedPmUnitConfigs entry moves the muxes, FPGA adapters or slot buses
    // leading to one.
    if (pmUnitVersionDetector_) {
      runtimeConfig_ = configBuilder.buildRuntimeConfig(
          *testConfig_,
          *platformManagerConfig_,
          kmods_,
          platform_,
          pmUnitVersionDetector_(*platformManagerConfig_, *runtimeConfig_));
    }
  }
}

void BspTestEnvironment::setPmUnitVersionDetector(
    PmUnitVersionDetector detector) {
  pmUnitVersionDetector_ = std::move(detector);
}

void BspTestEnvironment::TearDown() {}

const platform_manager::PlatformConfig&
BspTestEnvironment::getPlatformManagerConfig() const {
  return *platformManagerConfig_;
}

const BspTestsConfig& BspTestEnvironment::getTestConfig() const {
  return *testConfig_;
}

const RuntimeConfig& BspTestEnvironment::getRuntimeConfig() const {
  return *runtimeConfig_;
}

const platform_manager::BspKmodsFile& BspTestEnvironment::getKmodsJson() const {
  return kmods_;
}

platform_manager::PlatformConfig BspTestEnvironment::loadPlatformManagerConfig(
    const std::string& platform,
    const std::string& pmConfigJson) {
  std::string configJson;
  if (!pmConfigJson.empty()) {
    configJson = pmConfigJson;
  } else {
    configJson = ConfigLib().getPlatformManagerConfig(platform);
  }
  PlatformConfig config;
  try {
    apache::thrift::SimpleJSONSerializer::deserialize<PlatformConfig>(
        configJson, config);
  } catch (const std::exception& e) {
    XLOG(ERR) << "Failed to deserialize platform config: " << e.what();
    throw;
  }
  XLOG(DBG2) << apache::thrift::SimpleJSONSerializer::serialize<std::string>(
      config);
  return config;
}

BspTestsConfig BspTestEnvironment::loadTestConfig(
    const std::string& platform,
    const std::string& bspTestsConfigJson) {
  std::string configJson;
  if (!bspTestsConfigJson.empty()) {
    configJson = bspTestsConfigJson;
  } else {
    configJson = ConfigLib().getBspTestConfig(platform);
  }
  BspTestsConfig config;
  try {
    apache::thrift::SimpleJSONSerializer::deserialize<BspTestsConfig>(
        configJson, config);
  } catch (const std::exception& e) {
    XLOG(ERR) << "Failed to deserialize test config: " << e.what();
    throw;
  }
  XLOG(DBG2) << apache::thrift::SimpleJSONSerializer::serialize<std::string>(
      config);
  return config;
}

void BspTestEnvironment::recordExpectedError(
    const std::string& testName,
    const std::string& deviceName,
    const std::string& reason) {
  RecordedError error;
  error.testName = testName;
  error.deviceName = deviceName;
  error.reason = reason;

  recordedErrors_.push_back(error);
}

void BspTestEnvironment::printAllRecordedErrors() const {
  if (recordedErrors_.empty()) {
    return;
  }

  XLOG(INFO) << "=== EXPECTED ERRORS SUMMARY ===";
  XLOG(INFO) << "Total errors recorded: " << recordedErrors_.size();

  for (size_t i = 0; i < recordedErrors_.size(); ++i) {
    const auto& error = recordedErrors_[i];
    XLOG(INFO) << "Error " << (i + 1) << ":";
    XLOG(INFO) << "  Test: " << error.testName;
    XLOG(INFO) << "  Device: " << error.deviceName;
    XLOG(INFO) << "  Reason: "
               << (error.reason.empty() ? "<no reason provided>"
                                        : error.reason);
  }
  XLOG(INFO) << "=== END ERRORS SUMMARY ===";
}

} // namespace facebook::fboss::platform::bsp_tests
