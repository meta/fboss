// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <folly/json/json.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "fboss/platform/bsp_tests/RuntimeConfigBuilder.h"
#include "fboss/platform/bsp_tests/gen-cpp2/bsp_tests_config_types.h"
#include "fboss/platform/bsp_tests/gen-cpp2/bsp_tests_runtime_config_types.h"
#include "fboss/platform/platform_manager/PkgManager.h"
#include "fboss/platform/platform_manager/gen-cpp2/platform_manager_config_types.h"

namespace facebook::fboss::platform::bsp_tests {

/**
 * Global test environment for BSP tests.
 * This class is responsible for loading and parsing configuration files,
 * and providing access to the runtime configuration.
 */
class BspTestEnvironment : public ::testing::Environment {
 public:
  struct RecordedError {
    std::string testName;
    std::string deviceName;
    std::string reason;
  };

  virtual ~BspTestEnvironment() override;

  BspTestEnvironment(const BspTestEnvironment&) = delete;
  BspTestEnvironment(BspTestEnvironment&&) = delete;
  BspTestEnvironment& operator=(const BspTestEnvironment&) = delete;
  BspTestEnvironment& operator=(BspTestEnvironment&&) = delete;

  void SetUp() override;
  void TearDown() override;

  const platform_manager::PlatformConfig& getPlatformManagerConfig() const;
  const BspTestsConfig& getTestConfig() const;
  const RuntimeConfig& getRuntimeConfig() const;
  const platform_manager::BspKmodsFile& getKmodsJson() const;

  // Reads PmUnit versions off the hardware, given the runtime config built
  // against default PmUnitConfigs. Injected because the utils that bring up
  // adapters depend on this library.
  using PmUnitVersionDetector = std::function<PmUnitVersionMap(
      const platform_manager::PlatformConfig&,
      const RuntimeConfig&)>;
  void setPmUnitVersionDetector(PmUnitVersionDetector detector);

  void recordExpectedError(
      const std::string& testName,
      const std::string& deviceName,
      const std::string& reason);

  void printAllRecordedErrors() const;

  // Static accessor method for the singleton instance
  static BspTestEnvironment* GetInstance() {
    if (!instance_) {
      instance_ = new BspTestEnvironment();
    }
    return instance_;
  }

 private:
  // Private constructor to enforce singleton pattern
  BspTestEnvironment() = default;

  platform_manager::PlatformConfig loadPlatformManagerConfig(
      const std::string& platform,
      const std::string& pmConfigJson);

  BspTestsConfig loadTestConfig(
      const std::string& platform,
      const std::string& bspTestsConfigJson);

  // Static instance pointer for singleton pattern
  static BspTestEnvironment* instance_;

  std::string platform_;
  std::optional<BspTestsConfig> testConfig_;
  std::optional<platform_manager::PlatformConfig> platformManagerConfig_;
  std::optional<RuntimeConfig> runtimeConfig_;
  PmUnitVersionDetector pmUnitVersionDetector_;
  platform_manager::BspKmodsFile kmods_;
  std::unique_ptr<platform_manager::PkgManager> pkgManager_;
  std::vector<RecordedError> recordedErrors_;
};

} // namespace facebook::fboss::platform::bsp_tests
