// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <stdexcept>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "fboss/platform/config_lib/ConfigLib.h"
#include "fboss/platform/fan_service/Bsp.h"
#include "fboss/platform/fan_service/ControlLogic.h"
#include "fboss/platform/fan_service/SensorData.h"

using namespace ::testing;
using namespace facebook::fboss::platform::fan_service;

namespace {

float kDefaultRpm = 31;

// An optic type the sample platform config has no thermal profile for.
const std::string kUnprofiledOpticType = "OPTIC_TYPE_800_ZR";
const std::string kProfiledOpticType = "OPTIC_TYPE_400_GENERIC";

std::array<int, 5> kExpectedPwms = std::array<int, 5>{49, 49, 48, 48, 47};
std::array<int, 5> kExpectedBoostModePwms =
    std::array<int, 5>{51, 51, 52, 52, 53};

}; // namespace

namespace facebook::fboss::platform {
class MockBsp : public Bsp {
 public:
  explicit MockBsp(const FanServiceConfig& config) : Bsp(config) {}
  MOCK_METHOD(bool, setFanPwmSysfs, (const std::string&, int));
  MOCK_METHOD(bool, turnOnLedSysfs, (const std::string&));
  MOCK_METHOD(bool, checkIfInitialSensorDataRead, (), (const));
  MOCK_METHOD(float, readSysfs, (const std::string&), (const));
  std::optional<int> getLedMaxBrightness(const std::string&) const override {
    return 255;
  }
};

class ControlLogicTests : public testing::Test {
 public:
  void SetUp() override {
    // fb303 counters are process-global. Without this, a counter set by an
    // earlier test satisfies assertions in a later one.
    fb303::fbData->resetAllData();

    auto fanServiceConfJson = ConfigLib().getFanServiceConfig("sample");

    apache::thrift::SimpleJSONSerializer::deserialize<FanServiceConfig>(
        fanServiceConfJson, fanServiceConfig_);
    mockBsp_ = std::make_shared<MockBsp>(fanServiceConfig_);
    controlLogic_ = std::make_shared<ControlLogic>(fanServiceConfig_, mockBsp_);
    sensorData_ = std::make_shared<SensorData>();

    float value = 30.0;
    for (const auto& sensor : *fanServiceConfig_.sensors()) {
      sensorData_->updateSensorEntry(
          *sensor.sensorName(), value, mockBsp_->getCurrentTime());
      value += 10.5;
    }

    for (const auto& optic : *fanServiceConfig_.optics()) {
      value = 30.0;
      std::map<std::string, std::vector<OpticData>> opticData;
      int32_t txvrId = 1;
      for (const auto& [opticType, tempToPwm] : *optic.tempToPwmMaps()) {
        opticData[opticType].push_back(OpticData{txvrId++, value});
        value += 12.0;
      }
      sensorData_->updateOpticEntry(
          *optic.opticName(), opticData, mockBsp_->getCurrentTime());
    }

    ASSERT_TRUE(kExpectedPwms.size() == fanServiceConfig_.fans()->size());
  }

  // Sets up the fan sysfs expectations shared by the updateControl() tests.
  void expectFanAccess() {
    EXPECT_CALL(*mockBsp_, checkIfInitialSensorDataRead())
        .WillRepeatedly(Return(true));
    for (const auto& fan : *fanServiceConfig_.fans()) {
      EXPECT_CALL(*mockBsp_, setFanPwmSysfs(*fan.pwmSysfsPath(), _))
          .WillRepeatedly(Return(true));
      EXPECT_CALL(*mockBsp_, turnOnLedSysfs(*fan.goodLedSysfsPath()))
          .WillRepeatedly(Return(true));
      EXPECT_CALL(*mockBsp_, readSysfs(*fan.rpmSysfsPath()))
          .WillRepeatedly(Return(kDefaultRpm));
      EXPECT_CALL(*mockBsp_, readSysfs(*fan.presenceSysfsPath()))
          .WillRepeatedly(Return(1 /* fan exists */));
    }
  }

  // Removes an optic type from every optic group's cached data, as if the
  // transceiver had been unplugged.
  void removeOpticTypeFromSensorData(const std::string& opticType) {
    for (const auto& optic : *fanServiceConfig_.optics()) {
      auto opticEntry = sensorData_->getOpticEntry(*optic.opticName());
      auto opticData = opticEntry->data;
      opticData.erase(opticType);
      sensorData_->updateOpticEntry(
          *optic.opticName(), opticData, mockBsp_->getCurrentTime());
    }
  }

  // Empties every optic group's cached data, as if qsfp_service had stopped
  // publishing.
  void clearOpticDataForAllGroups() {
    for (const auto& optic : *fanServiceConfig_.optics()) {
      sensorData_->updateOpticEntry(
          *optic.opticName(), {}, mockBsp_->getCurrentTime());
    }
  }

  // Adds an extra optic type to every optic group's cached data.
  void addOpticTypeToSensorData(const std::string& opticType, float temp) {
    for (const auto& optic : *fanServiceConfig_.optics()) {
      auto opticEntry = sensorData_->getOpticEntry(*optic.opticName());
      auto opticData = opticEntry->data;
      opticData[opticType].push_back(OpticData{99, temp});
      sensorData_->updateOpticEntry(
          *optic.opticName(), opticData, mockBsp_->getCurrentTime());
    }
  }

  FanServiceConfig fanServiceConfig_;
  std::shared_ptr<SensorData> sensorData_;
  std::shared_ptr<MockBsp> mockBsp_;
  std::shared_ptr<ControlLogic> controlLogic_;
};

TEST_F(ControlLogicTests, SetTransitionValueSuccess) {
  for (const auto& fan : *fanServiceConfig_.fans()) {
    EXPECT_CALL(*mockBsp_, setFanPwmSysfs(*fan.pwmSysfsPath(), _))
        .WillOnce(Return(true));
    EXPECT_CALL(*mockBsp_, turnOnLedSysfs(*fan.goodLedSysfsPath())).Times(0);
  }

  controlLogic_->setTransitionValue();

  for (const auto& [fanName, fanStatus] : controlLogic_->getFanStatuses()) {
    EXPECT_EQ(*fanStatus.fanFailed(), false);
    EXPECT_EQ(
        *fanStatus.pwmToProgram(), *fanServiceConfig_.pwmTransitionValue());
  }
}

TEST_F(ControlLogicTests, SetTransitionValueFailure) {
  for (const auto& fan : *fanServiceConfig_.fans()) {
    EXPECT_CALL(*mockBsp_, setFanPwmSysfs(*fan.pwmSysfsPath(), _))
        .WillOnce(Return(false));
    EXPECT_CALL(*mockBsp_, turnOnLedSysfs(*fan.failLedSysfsPath()))
        .Times(1)
        .WillOnce(Return(true));
  }

  controlLogic_->setTransitionValue();

  for (const auto& [fanName, fanStatus] : controlLogic_->getFanStatuses()) {
    EXPECT_EQ(*fanStatus.fanFailed(), true);
    EXPECT_EQ(
        *fanStatus.pwmToProgram(), *fanServiceConfig_.pwmTransitionValue());
  }
}

TEST_F(ControlLogicTests, UpdateControlSuccess) {
  EXPECT_CALL(*mockBsp_, checkIfInitialSensorDataRead()).WillOnce(Return(true));
  for (const auto& fan : *fanServiceConfig_.fans()) {
    EXPECT_CALL(*mockBsp_, setFanPwmSysfs(*fan.pwmSysfsPath(), _))
        .Times(2)
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mockBsp_, turnOnLedSysfs(*fan.goodLedSysfsPath()))
        .WillOnce(Return(true));
    EXPECT_CALL(*mockBsp_, readSysfs(*fan.rpmSysfsPath()))
        .WillOnce(Return(kDefaultRpm));
    EXPECT_CALL(*mockBsp_, readSysfs(*fan.presenceSysfsPath()))
        .WillOnce(Return(1 /* fan exists */));
  }

  auto startTime = mockBsp_->getCurrentTime();

  controlLogic_->setTransitionValue();

  controlLogic_->updateControl(sensorData_);
  const auto fanStatuses = controlLogic_->getFanStatuses();
  EXPECT_EQ(fanStatuses.size(), fanServiceConfig_.fans()->size());
  int i = 0;
  for (const auto& [fanName, fanStatus] : fanStatuses) {
    EXPECT_EQ(*fanStatus.fanFailed(), false);
    EXPECT_EQ(*fanStatus.rpm(), kDefaultRpm);
    EXPECT_GE(*fanStatus.lastSuccessfulAccessTime(), startTime);
    EXPECT_EQ(*fanStatus.pwmToProgram(), kExpectedPwms[i++]);
    EXPECT_EQ(fb303::fbData->getCounter(fmt::format("{}.absent", fanName)), 0);
    EXPECT_EQ(
        fb303::fbData->getCounter(fmt::format("{}.rpm_read.failure", fanName)),
        0);
  }
  EXPECT_EQ(fb303::fbData->getCounter("has.fan_rpm_read_failure"), 0);
  EXPECT_EQ(fb303::fbData->getCounter("has.fan_pwm_write_failure"), 0);
  EXPECT_EQ(fb303::fbData->getCounter("has.fan_absent"), 0);
  const auto& sensorCaches = controlLogic_->getSensorCaches();
  for (const auto& [sensorName, sensorCache] : sensorCaches) {
    EXPECT_EQ(sensorCache.sensorFailed, false);
    EXPECT_EQ(
        fb303::fbData->getCounter(
            fmt::format("{}.sensor_read.failure", sensorName)),
        0);
  }
}

TEST_F(ControlLogicTests, UpdateControlFailureDueToMissingFans) {
  EXPECT_CALL(*mockBsp_, checkIfInitialSensorDataRead()).WillOnce(Return(true));
  for (const auto& fan : *fanServiceConfig_.fans()) {
    EXPECT_CALL(*mockBsp_, setFanPwmSysfs(*fan.pwmSysfsPath(), _))
        .Times(2)
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mockBsp_, turnOnLedSysfs(*fan.failLedSysfsPath()))
        .WillOnce(Return(true));
    EXPECT_CALL(*mockBsp_, readSysfs(*fan.rpmSysfsPath())).Times(0);
    EXPECT_CALL(*mockBsp_, readSysfs(*fan.presenceSysfsPath()))
        .WillOnce(Return(0 /* fan missing */));
  }

  controlLogic_->setTransitionValue();

  controlLogic_->updateControl(sensorData_);
  const auto fanStatuses = controlLogic_->getFanStatuses();
  int i = 0;
  EXPECT_EQ(fanStatuses.size(), fanServiceConfig_.fans()->size());
  for (const auto& [fanName, fanStatus] : fanStatuses) {
    EXPECT_EQ(*fanStatus.fanFailed(), true);
    EXPECT_EQ(fanStatus.rpm().has_value(), false);
    EXPECT_EQ(*fanStatus.lastSuccessfulAccessTime(), 0);
    EXPECT_EQ(*fanStatus.pwmToProgram(), kExpectedBoostModePwms[i++]);
    EXPECT_EQ(fb303::fbData->getCounter(fmt::format("{}.absent", fanName)), 1);
    EXPECT_EQ(
        fb303::fbData->getCounter(fmt::format("{}.rpm_read.failure", fanName)),
        1);
  }
  EXPECT_EQ(fb303::fbData->getCounter("has.fan_rpm_read_failure"), 1);
  EXPECT_EQ(fb303::fbData->getCounter("has.fan_pwm_write_failure"), 0);
  EXPECT_EQ(fb303::fbData->getCounter("has.fan_absent"), 1);
}

TEST_F(ControlLogicTests, UpdateControlFailureDueToFanInaccessible) {
  EXPECT_CALL(*mockBsp_, checkIfInitialSensorDataRead()).WillOnce(Return(true));
  for (const auto& fan : *fanServiceConfig_.fans()) {
    EXPECT_CALL(*mockBsp_, setFanPwmSysfs(*fan.pwmSysfsPath(), _))
        .Times(2)
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mockBsp_, turnOnLedSysfs(*fan.failLedSysfsPath()))
        .Times(1)
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mockBsp_, readSysfs(*fan.rpmSysfsPath()))
        .WillOnce(Throw(std::exception()));
    EXPECT_CALL(*mockBsp_, readSysfs(*fan.presenceSysfsPath()))
        .WillOnce(Return(1 /* fan exists */));
  }

  controlLogic_->setTransitionValue();

  controlLogic_->updateControl(sensorData_);
  const auto fanStatuses = controlLogic_->getFanStatuses();
  EXPECT_EQ(fanStatuses.size(), fanServiceConfig_.fans()->size());
  int i = 0;
  for (const auto& [fanName, fanStatus] : fanStatuses) {
    EXPECT_EQ(*fanStatus.fanFailed(), true);
    EXPECT_EQ(fanStatus.rpm().has_value(), false);
    EXPECT_EQ(*fanStatus.lastSuccessfulAccessTime(), 0);
    EXPECT_EQ(*fanStatus.pwmToProgram(), kExpectedBoostModePwms[i++]);
    EXPECT_EQ(fb303::fbData->getCounter(fmt::format("{}.absent", fanName)), 0);
    EXPECT_EQ(
        fb303::fbData->getCounter(fmt::format("{}.rpm_read.failure", fanName)),
        1);
  }
  EXPECT_EQ(fb303::fbData->getCounter("has.fan_rpm_read_failure"), 1);
  EXPECT_EQ(fb303::fbData->getCounter("has.fan_pwm_write_failure"), 0);
  EXPECT_EQ(fb303::fbData->getCounter("has.fan_absent"), 0);
}

TEST_F(
    ControlLogicTests,
    UpdateControlFailureDueToFanInaccessibleAfterSuccessLTThreshold) {
  EXPECT_CALL(*mockBsp_, checkIfInitialSensorDataRead())
      .Times(2)
      .WillRepeatedly(Return(true));
  for (const auto& fan : *fanServiceConfig_.fans()) {
    EXPECT_CALL(*mockBsp_, setFanPwmSysfs(*fan.pwmSysfsPath(), _))
        .Times(3)
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mockBsp_, turnOnLedSysfs(*fan.goodLedSysfsPath()))
        .Times(2)
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mockBsp_, readSysfs(*fan.rpmSysfsPath()))
        .WillOnce(Return(kDefaultRpm))
        .WillOnce(Throw(std::exception()));
    EXPECT_CALL(*mockBsp_, readSysfs(*fan.presenceSysfsPath()))
        .Times(2)
        .WillRepeatedly(Return(1 /* fan exists */));
  }

  auto startTime = mockBsp_->getCurrentTime();

  controlLogic_->setTransitionValue();

  // Simulate two cycles of programing fan.
  controlLogic_->updateControl(sensorData_);
  controlLogic_->updateControl(sensorData_);
  const auto fanStatuses = controlLogic_->getFanStatuses();
  EXPECT_EQ(fanStatuses.size(), fanServiceConfig_.fans()->size());
  for (const auto& [fanName, fanStatus] : fanStatuses) {
    EXPECT_EQ(*fanStatus.fanFailed(), false);
    EXPECT_EQ(fanStatus.rpm().has_value(), false);
    EXPECT_GE(*fanStatus.lastSuccessfulAccessTime(), startTime);
    EXPECT_EQ(fb303::fbData->getCounter(fmt::format("{}.absent", fanName)), 0);
    EXPECT_EQ(
        fb303::fbData->getCounter(fmt::format("{}.rpm_read.failure", fanName)),
        1);
  }
}
TEST_F(ControlLogicTests, UpdateControlSensorReadFailure) {
  EXPECT_CALL(*mockBsp_, checkIfInitialSensorDataRead()).WillOnce(Return(true));
  for (const auto& fan : *fanServiceConfig_.fans()) {
    EXPECT_CALL(*mockBsp_, setFanPwmSysfs(*fan.pwmSysfsPath(), _))
        .Times(2)
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mockBsp_, turnOnLedSysfs(*fan.goodLedSysfsPath()))
        .WillOnce(Return(true));
    EXPECT_CALL(*mockBsp_, readSysfs(*fan.rpmSysfsPath()))
        .WillOnce(Return(kDefaultRpm));
    EXPECT_CALL(*mockBsp_, readSysfs(*fan.presenceSysfsPath()))
        .WillOnce(Return(1 /* fan exists */));
  }

  auto startTime = mockBsp_->getCurrentTime();

  controlLogic_->setTransitionValue();

  for (const auto& sensor : *fanServiceConfig_.sensors()) {
    sensorData_->delSensorEntry(*sensor.sensorName());
  }
  controlLogic_->updateControl(sensorData_);
  const auto fanStatuses = controlLogic_->getFanStatuses();
  EXPECT_EQ(fanStatuses.size(), fanServiceConfig_.fans()->size());
  int i = 0;
  for (const auto& [fanName, fanStatus] : fanStatuses) {
    EXPECT_EQ(*fanStatus.fanFailed(), false);
    EXPECT_EQ(*fanStatus.rpm(), kDefaultRpm);
    EXPECT_GE(*fanStatus.lastSuccessfulAccessTime(), startTime);
    EXPECT_EQ(*fanStatus.pwmToProgram(), kExpectedPwms[i++]);
    EXPECT_EQ(fb303::fbData->getCounter(fmt::format("{}.absent", fanName)), 0);
    EXPECT_EQ(
        fb303::fbData->getCounter(fmt::format("{}.rpm_read.failure", fanName)),
        0);
  }
  const auto& sensorCaches = controlLogic_->getSensorCaches();
  for (const auto& [sensorName, sensorCache] : sensorCaches) {
    EXPECT_EQ(sensorCache.sensorFailed, true);
    EXPECT_EQ(
        fb303::fbData->getCounter(
            fmt::format("{}.sensor_read.failure", sensorName)),
        1);
  }
}

TEST_F(ControlLogicTests, OpticsFb303CountersAreSet) {
  EXPECT_CALL(*mockBsp_, checkIfInitialSensorDataRead()).WillOnce(Return(true));
  for (const auto& fan : *fanServiceConfig_.fans()) {
    EXPECT_CALL(*mockBsp_, setFanPwmSysfs(*fan.pwmSysfsPath(), _))
        .Times(2)
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mockBsp_, turnOnLedSysfs(*fan.goodLedSysfsPath()))
        .WillOnce(Return(true));
    EXPECT_CALL(*mockBsp_, readSysfs(*fan.rpmSysfsPath()))
        .WillOnce(Return(kDefaultRpm));
    EXPECT_CALL(*mockBsp_, readSysfs(*fan.presenceSysfsPath()))
        .WillOnce(Return(1 /* fan exists */));
  }

  controlLogic_->setTransitionValue();
  controlLogic_->updateControl(sensorData_);

  // Verify optics counters are set per optic type.
  // SetUp creates optic data with temps 30.0, 42.0, 54.0 for each optic type
  // (in std::map alphabetical order: 100_GENERIC, 200_GENERIC, 400_GENERIC).
  // Expected PWM values based on tempToPwmMaps:
  //   - OPTIC_TYPE_100_GENERIC: temp=30, PWM=24 (30 >= 5)
  //   - OPTIC_TYPE_200_GENERIC: temp=42, PWM=26 (42 >= 5, < 43)
  //   - OPTIC_TYPE_400_GENERIC: temp=54, PWM=36 (54 >= 5, < 59)
  std::map<std::string, std::pair<int64_t, int64_t>> expectedOpticValues = {
      {"OPTIC_TYPE_100_GENERIC", {30, 24}},
      {"OPTIC_TYPE_200_GENERIC", {42, 26}},
      {"OPTIC_TYPE_400_GENERIC", {54, 36}},
  };

  for (const auto& [opticType, expected] : expectedOpticValues) {
    const auto& [expectedTemp, expectedPwm] = expected;
    EXPECT_EQ(
        fb303::fbData->getCounter(
            fmt::format("{}.optics_read.max.value", opticType)),
        expectedTemp);
    EXPECT_EQ(
        fb303::fbData->getCounter(
            fmt::format("{}.optics_pwm.value", opticType)),
        expectedPwm);
  }

  // Verify aggregate optics PWM counter is max(24, 26, 36) = 36.
  EXPECT_EQ(fb303::fbData->getCounter("agg.optics_pwm.value"), 36);

  // Every configured optic type has a profile.
  for (const auto& [opticType, _] : expectedOpticValues) {
    EXPECT_EQ(
        fb303::fbData->getCounter(
            fmt::format("{}.optics_no_profile", opticType)),
        0);
  }
}

TEST_F(ControlLogicTests, UnprofiledOpticTypeIsReportedExcludedAndBoosts) {
  expectFanAccess();
  addOpticTypeToSensorData(kUnprofiledOpticType, 70.0);

  controlLogic_->setTransitionValue();
  controlLogic_->updateControl(sensorData_);

  // The temperature is still published so the optic stays visible, and the
  // no-profile counter flags it.
  EXPECT_EQ(
      fb303::fbData->getCounter(
          fmt::format("{}.optics_read.max.value", kUnprofiledOpticType)),
      70);
  EXPECT_EQ(
      fb303::fbData->getCounter(
          fmt::format("{}.optics_no_profile", kUnprofiledOpticType)),
      1);

  // It contributes nothing to the optics aggregate, which stays at the
  // max over the profiled types (24, 26, 36).
  EXPECT_EQ(fb303::fbData->getCounter("agg.optics_pwm.value"), 36);

  // An optic we cannot control must fail toward more cooling, not less.
  const auto fanStatuses = controlLogic_->getFanStatuses();
  int i = 0;
  EXPECT_EQ(fanStatuses.size(), fanServiceConfig_.fans()->size());
  for (const auto& [fanName, fanStatus] : fanStatuses) {
    EXPECT_EQ(*fanStatus.pwmToProgram(), kExpectedBoostModePwms[i++]);
  }
}

TEST_F(
    ControlLogicTests,
    UnprofiledOpticTypeUnderPidAggregationIsReportedAndBoosts) {
  // Same contract as the MAX case above, under PID aggregation: an optic type
  // with no profile is reported and boosts, and must never abort the control
  // loop. PID is the branch where getting it wrong throws rather than
  // degrading, because a profile miss must not reach pidLogics_.
  PidSetting pidSetting;
  pidSetting.kp() = -4;
  pidSetting.ki() = -0.06;
  pidSetting.kd() = 0;
  pidSetting.setPoint() = 67.0;
  pidSetting.posHysteresis() = 0.0;
  pidSetting.negHysteresis() = 3.0;
  for (auto& optic : *fanServiceConfig_.optics()) {
    for (const auto& [opticType, _] : *optic.tempToPwmMaps()) {
      optic.pidSettings()->emplace(opticType, pidSetting);
    }
    optic.tempToPwmMaps()->clear();
    optic.aggregationType() = "OPTIC_AGGREGATION_TYPE_PID";
  }

  // Before constructing ControlLogic: its constructor programs the fans.
  expectFanAccess();
  auto pidControlLogic =
      std::make_shared<ControlLogic>(fanServiceConfig_, mockBsp_);
  addOpticTypeToSensorData(kUnprofiledOpticType, 70.0);

  EXPECT_NO_THROW(pidControlLogic->updateControl(sensorData_));

  EXPECT_EQ(
      fb303::fbData->getCounter(
          fmt::format("{}.optics_no_profile", kUnprofiledOpticType)),
      1);
  // No PWM was computed for it, so no pwm counter should exist.
  EXPECT_FALSE(
      fb303::fbData->hasCounter(
          fmt::format("{}.optics_pwm.value", kUnprofiledOpticType)));

  // An optic we cannot control must fail toward more cooling, not less.
  const auto fanStatuses = pidControlLogic->getFanStatuses();
  int i = 0;
  EXPECT_EQ(fanStatuses.size(), fanServiceConfig_.fans()->size());
  for (const auto& [fanName, fanStatus] : fanStatuses) {
    EXPECT_EQ(*fanStatus.pwmToProgram(), kExpectedBoostModePwms[i++]);
  }
}

TEST_F(ControlLogicTests, OpticCountersAreClearedWhenOpticTypeDisappears) {
  expectFanAccess();
  addOpticTypeToSensorData(kUnprofiledOpticType, 70.0);

  controlLogic_->setTransitionValue();
  controlLogic_->updateControl(sensorData_);

  EXPECT_TRUE(
      fb303::fbData->hasCounter(
          fmt::format("{}.optics_read.max.value", kUnprofiledOpticType)));
  EXPECT_EQ(
      fb303::fbData->getCounter(
          fmt::format("{}.optics_no_profile", kUnprofiledOpticType)),
      1);
  // A profiled type is the only one that ever gets a pwm counter, so it has
  // to be part of this test for the pwm counter's clearing to be covered.
  EXPECT_TRUE(
      fb303::fbData->hasCounter(
          fmt::format("{}.optics_pwm.value", kProfiledOpticType)));

  // Unplug both. The counters must stop reporting rather than latch their last
  // value, otherwise an alert on optics_no_profile fires forever.
  removeOpticTypeFromSensorData(kUnprofiledOpticType);
  removeOpticTypeFromSensorData(kProfiledOpticType);
  controlLogic_->updateControl(sensorData_);

  for (const auto& opticType : {kUnprofiledOpticType, kProfiledOpticType}) {
    EXPECT_FALSE(
        fb303::fbData->hasCounter(
            fmt::format("{}.optics_read.max.value", opticType)));
    EXPECT_FALSE(
        fb303::fbData->hasCounter(
            fmt::format("{}.optics_pwm.value", opticType)));
    EXPECT_FALSE(
        fb303::fbData->hasCounter(
            fmt::format("{}.optics_no_profile", opticType)));
  }
}

TEST_F(ControlLogicTests, OpticCountersSurviveEmptyOpticData) {
  expectFanAccess();

  controlLogic_->setTransitionValue();
  controlLogic_->updateControl(sensorData_);

  EXPECT_TRUE(
      fb303::fbData->hasCounter(
          fmt::format("{}.optics_read.max.value", kProfiledOpticType)));
  EXPECT_TRUE(
      fb303::fbData->hasCounter(
          fmt::format("{}.optics_pwm.value", kProfiledOpticType)));

  // An empty optic group means qsfp_service is unreachable or its data has
  // gone stale, not that the optics were unplugged. Fan control keeps running
  // off the cached pwm, so the counters must keep reporting instead of
  // disappearing from monitoring at exactly that moment.
  clearOpticDataForAllGroups();
  controlLogic_->updateControl(sensorData_);

  EXPECT_TRUE(
      fb303::fbData->hasCounter(
          fmt::format("{}.optics_read.max.value", kProfiledOpticType)));
  EXPECT_TRUE(
      fb303::fbData->hasCounter(
          fmt::format("{}.optics_pwm.value", kProfiledOpticType)));
}

} // namespace facebook::fboss::platform
