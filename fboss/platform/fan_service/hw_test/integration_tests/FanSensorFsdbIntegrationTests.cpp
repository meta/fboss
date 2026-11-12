// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <fb303/ServiceData.h>
#include <folly/Subprocess.h>
#include <gtest/gtest.h>

#include "fboss/lib/CommonUtils.h"
#include "fboss/platform/config_lib/ConfigLib.h"
#include "fboss/platform/fan_service/ControlLogic.h"
#include "fboss/platform/helpers/Init.h"

DEFINE_bool(run_forever, false, "run the test forever");
DEFINE_string(
    fsdb_service_unit,
    "fsdb_service_for_testing",
    "systemd unit of the fsdb that the test stops and restarts");
DEFINE_string(
    sensor_service_unit,
    "sensor_service_for_testing",
    "systemd unit of the sensor_service that the test restarts");

using namespace facebook::fboss::platform::fan_service;
using namespace facebook::fboss::platform;

namespace {

void systemctl(const std::string& action, const std::string& unit) {
  folly::Subprocess({"/bin/systemctl", action, unit}).waitChecked();
}

void stopFsdbService() {
  XLOG(INFO) << "Stopping FSDB Service";
  systemctl("stop", FLAGS_fsdb_service_unit);
}

void startFsdbService() {
  XLOG(INFO) << "Starting FSDB Service";
  systemctl("start", FLAGS_fsdb_service_unit);
}

void restartFsdbService() {
  XLOG(INFO) << "Restarting FSDB Service";
  systemctl("restart", FLAGS_fsdb_service_unit);
}

void restartSensorService() {
  XLOG(INFO) << "Restarting Sensor Service";
  systemctl("restart", FLAGS_sensor_service_unit);
}

class FanSensorFsdbIntegrationTests : public ::testing::Test {
 public:
  void SetUp() override {
    std::string fanServiceConfJson = ConfigLib().getFanServiceConfig();
    auto config =
        apache::thrift::SimpleJSONSerializer::deserialize<FanServiceConfig>(
            fanServiceConfJson);
    XLOG(INFO) << apache::thrift::SimpleJSONSerializer::serialize<std::string>(
        config);
    auto pBsp = std::make_shared<Bsp>(config);
    controlLogic_ = std::make_unique<ControlLogic>(config, pBsp);
  }

  void TearDown() override {
    if (FLAGS_run_forever) {
      while (true) {
        /* sleep override */ sleep(1);
      }
    }
    // Restart Sensor and FSDB to bring it back to healthy state for the next
    // test
    restartSensorService();
    restartFsdbService();
  }

  std::unique_ptr<ControlLogic> controlLogic_{nullptr};
};

} // namespace

namespace facebook::fboss::platform::fan_service {

TEST_F(FanSensorFsdbIntegrationTests, sensorUpdate) {
  SensorData initialSensorData;

  // Get the sensor data separately from sensor service via thrift. Expect the
  // same sensors to be available in the fan service cache later. This would
  // confirm that the sensor service publishes its sensors to fsdb and the fan
  // service correctly subscribes to those sensors from fsdb
  auto referenceSensorData = std::make_shared<SensorData>();
  controlLogic_->getSensorDataThrift(referenceSensorData);
  // Expect non-zero sensors
  ASSERT_TRUE(referenceSensorData->getSensorEntries().size());

  WITH_RETRIES_N_TIMED(6, std::chrono::seconds(10), {
    // Kick off the control fan logic, which will try to fetch the sensor
    // data from sensor_service
    controlLogic_->controlFan();
    initialSensorData = controlLogic_->sensorData();
    // Confirm that the fan service received the same sensors from fsdb as
    // returned by sensor service via thrift
    ASSERT_EVENTUALLY_TRUE(
        initialSensorData.getSensorEntries().size() >=
        referenceSensorData->getSensorEntries().size());
    for (const auto& [sensorName, sensorEntry] :
         referenceSensorData->getSensorEntries()) {
      ASSERT_TRUE(initialSensorData.getSensorEntry(sensorName));
    }
  });

  // Fetch the sensor data again and expect sensor timestamps to advance
  WITH_RETRIES_N_TIMED(6, std::chrono::seconds(10), {
    controlLogic_->controlFan();
    auto latestSensorData = controlLogic_->sensorData();
    ASSERT_EVENTUALLY_TRUE(
        latestSensorData.getSensorEntries().size() ==
        initialSensorData.getSensorEntries().size());
    int sensorsWithUpdatedTimestamps = 0;
    for (const auto& [sensorName, initialEntry] :
         initialSensorData.getSensorEntries()) {
      auto latestEntry = latestSensorData.getSensorEntry(sensorName);
      ASSERT_TRUE(latestEntry);
      XLOG(INFO) << "Sensor: " << sensorName
                 << ". Initial timestamp: " << initialEntry.lastUpdated
                 << ", Current timestamp: " << latestEntry->lastUpdated;
      // The timestamps should advance. Sometimes the timestamps are 0 for
      // some sensors returned by sensor data, so add a special check for
      // that too
      if (latestEntry->lastUpdated > initialEntry.lastUpdated ||
          (latestEntry->lastUpdated == 0 && initialEntry.lastUpdated == 0)) {
        sensorsWithUpdatedTimestamps++;
      }
    }
    // All sensors should have updated timestamps (or both be 0)
    ASSERT_EVENTUALLY_EQ(
        sensorsWithUpdatedTimestamps,
        initialSensorData.getSensorEntries().size());
  });
}

// Verifies sensor data is synced correctly after a fsdb restart and that
// thrift fallback kicks in when FSDB is unavailable
TEST_F(FanSensorFsdbIntegrationTests, fsdbRestart) {
  SensorData initialFsdbSensorData;

  // Fetch the sensor data from sensor_service over thrift. This way we know
  // which sensors were explicitly published by sensor service
  auto referenceSensorData = std::make_shared<SensorData>();
  controlLogic_->getSensorDataThrift(referenceSensorData);
  // Expect non-zero sensors
  ASSERT_TRUE(referenceSensorData->getSensorEntries().size());

  XLOG(INFO) << "Waiting for FSDB data to become available (2 minutes)";
  // Verify we are getting sensor data from fsdb.
  WITH_RETRIES_N_TIMED(12, std::chrono::seconds(10), {
    controlLogic_->controlFan();
    initialFsdbSensorData = controlLogic_->sensorData();
    auto thriftFallbackCounter =
        fb303::fbData->getCounter(kFsdbSensorDataThriftFallback);
    ASSERT_EVENTUALLY_EQ(thriftFallbackCounter, 0)
        << "Should be getting sensor data from FSDB, not thrift fallback";
  });

  XLOG(INFO) << "Verifying sensor data from FSDB";
  // Verify the content we received from fsdb.
  ASSERT_TRUE(initialFsdbSensorData.getSensorEntries().size() > 0);
  for (const auto& [sensorName, sensorEntry] :
       referenceSensorData->getSensorEntries()) {
    ASSERT_TRUE(initialFsdbSensorData.getSensorEntry(sensorName))
        << "Sensor " << sensorName << " should be available via fsdb";
  }

  // Stop FSDB
  stopFsdbService();

  // With FSDB stopped, the fan_service will fall back to thrift from
  // sensor_service after the FSDB data becomes stale. Wait for staleness
  // threshold (90 seconds) plus buffer, then verify fallback is active.
  XLOG(INFO) << "Waiting for FSDB data to become stale and fallback to thrift";
  // Wait for FSDB data to become stale and verify fallback kicks in
  WITH_RETRIES_N_TIMED(12, std::chrono::seconds(10), {
    controlLogic_->controlFan();
    auto thriftFallbackCounter =
        fb303::fbData->getCounter(kFsdbSensorDataThriftFallback);
    ASSERT_EVENTUALLY_EQ(thriftFallbackCounter, 1)
        << "Should be getting sensor data from thrift fallback";
  });

  XLOG(INFO) << "Verifying sensor data from thrift fallback";
  // Verify the content we received from thrift.
  auto sensorDataDuringFallback = controlLogic_->sensorData();
  ASSERT_TRUE(sensorDataDuringFallback.getSensorEntries().size() > 0)
      << "Should still have sensor data via thrift fallback";
  for (const auto& [sensorName, sensorEntry] :
       referenceSensorData->getSensorEntries()) {
    ASSERT_TRUE(sensorDataDuringFallback.getSensorEntry(sensorName))
        << "Sensor " << sensorName
        << " should be available via thrift fallback";
  }

  // Verify thrift fallback is fetching fresh data by checking timestamps
  // advance. Wait another fetch interval and confirm sensor timestamps are
  // updating.
  XLOG(INFO) << "Verifying thrift fallback fetches fresh data (timestamps "
                "should advance)";
  WITH_RETRIES_N_TIMED(12, std::chrono::seconds(10), {
    controlLogic_->controlFan();
    auto latestSensorData = controlLogic_->sensorData();
    int sensorsWithUpdatedTimestamps = 0;
    for (const auto& [sensorName, snapshotEntry] :
         sensorDataDuringFallback.getSensorEntries()) {
      auto latestEntry = latestSensorData.getSensorEntry(sensorName);
      if (latestEntry && latestEntry->lastUpdated > snapshotEntry.lastUpdated) {
        sensorsWithUpdatedTimestamps++;
      }
    }
    XLOG(INFO) << "Timestamps advanced for " << sensorsWithUpdatedTimestamps
               << " out of "
               << sensorDataDuringFallback.getSensorEntries().size()
               << " sensors";
    // At least some sensor timestamps should advance, confirming fresh data
    ASSERT_EVENTUALLY_GT(sensorsWithUpdatedTimestamps, 0);
  });

  // Start FSDB
  startFsdbService();

  XLOG(INFO) << "Verifying sensor data from FSDB after restart";
  // Expect the sensor count to be same, and fallback counter to return to 0
  // (back to FSDB)
  WITH_RETRIES_N_TIMED(12, std::chrono::seconds(10), {
    controlLogic_->controlFan();
    auto sensorDataAfterFsdbRestart = controlLogic_->sensorData();
    auto thriftFallbackCounter =
        fb303::fbData->getCounter(kFsdbSensorDataThriftFallback);
    // Fallback counter should return to 0 when FSDB is back
    ASSERT_EVENTUALLY_EQ(thriftFallbackCounter, 0);
    for (const auto& [sensorName, initialEntry] :
         initialFsdbSensorData.getSensorEntries()) {
      auto latestEntry = sensorDataAfterFsdbRestart.getSensorEntry(sensorName);
      ASSERT_TRUE(latestEntry);
      XLOG(INFO) << "Sensor: " << sensorName
                 << ". Initial timestamp: " << initialEntry.lastUpdated
                 << ", Current timestamp: " << latestEntry->lastUpdated;
      // The timestamps should advance. Sometimes the timestamps are 0 for
      // some sensors returned by sensor data, so add a special check for
      // that too
      ASSERT_EVENTUALLY_TRUE(
          (latestEntry->lastUpdated > initialEntry.lastUpdated) ||
          (latestEntry->lastUpdated == 0 && initialEntry.lastUpdated == 0));
    }
  });
}

TEST_F(FanSensorFsdbIntegrationTests, qsfpSync) {
  std::unordered_map<
      std::string,
      std::pair<uint64_t /* timestamp */, int /* count */>>
      lastSyncTimeAndCountMap;

  WITH_RETRIES_N_TIMED(9, std::chrono::seconds(10), {
    // Kick off the control fan logic, which will try to process the optics data
    // synced from fsdb
    controlLogic_->controlFan();
    SensorData sensorData = controlLogic_->sensorData();
    // Ensure that the optic entry is not empty
    ASSERT_EVENTUALLY_TRUE(sensorData.getOpticEntries().size() > 0);
    for (const auto& [opticName, opticEntry] : sensorData.getOpticEntries()) {
      if (lastSyncTimeAndCountMap.find(opticName) ==
          lastSyncTimeAndCountMap.end()) {
        lastSyncTimeAndCountMap[opticName] = {0, 0};
      }
      // For every optic sensor that is synced, this timestamp should advance.
      auto& [timestamp, count] = lastSyncTimeAndCountMap[opticName];
      if (opticEntry.dataProcessTimeStamp > timestamp) {
        count++;
        timestamp = opticEntry.dataProcessTimeStamp;
      }
      // Let the test verify at least 2 syncs
      ASSERT_EVENTUALLY_TRUE(count >= 2);
    }
  });
}

} // namespace facebook::fboss::platform::fan_service

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  facebook::fboss::platform::helpers::init(&argc, &argv);
  return RUN_ALL_TESTS();
}
