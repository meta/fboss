// Copyright 2021- Facebook. All rights reserved.

#include "fboss/platform/fan_service/Bsp.h"

#include <ctime>
#include <string>

#include <fmt/chrono.h>
#include <folly/String.h>
#include <folly/logging/xlog.h>
#include <folly/system/ThreadName.h>

#include "common/time/Time.h"
#include "fboss/agent/FbossError.h"
#include "fboss/fsdb/common/Flags.h"
#include "fboss/lib/CommonFileUtils.h"
#include "fboss/platform/fan_service/DataFetcher.h"
#include "fboss/platform/fan_service/FsdbSensorSubscriber.h"
#include "fboss/platform/fan_service/if/gen-cpp2/fan_service_config_constants.h"
#include "fboss/platform/fan_service/if/gen-cpp2/fan_service_config_types.h"
#include "fboss/platform/helpers/PlatformUtils.h"
#include "fboss/platform/sensor_service/if/gen-cpp2/sensor_service_types.h"

namespace constants =
    facebook::fboss::platform::fan_service::fan_service_config_constants;

DEFINE_bool(
    subscribe_to_qsfp_data_from_fsdb,
    false,
    "For subscribing to qsfp state and stats from FSDB");

namespace {
auto constexpr kDefaultMaxBrightness = 255;
auto constexpr kSensordThriftPort = 5970;

} // namespace

namespace facebook::fboss::platform::fan_service {

std::optional<std::string> getOpticTypeFromMediaCode(
    std::optional<facebook::fboss::MediaInterfaceCode> mediaInterfaceCode,
    const std::string& unknownFallback) {
  using facebook::fboss::MediaInterfaceCode;
  if (!mediaInterfaceCode ||
      *mediaInterfaceCode == MediaInterfaceCode::UNKNOWN) {
    return unknownFallback;
  }
  switch (*mediaInterfaceCode) {
    case MediaInterfaceCode::CWDM4_100G:
    case MediaInterfaceCode::CR4_100G:
    case MediaInterfaceCode::FR1_100G:
      return constants::OPTIC_TYPE_100_GENERIC();
    case MediaInterfaceCode::FR4_200G:
      return constants::OPTIC_TYPE_200_GENERIC();
    case MediaInterfaceCode::FR4_400G:
    case MediaInterfaceCode::LR4_400G_10KM:
    case MediaInterfaceCode::DR4_400G:
      return constants::OPTIC_TYPE_400_GENERIC();
    case MediaInterfaceCode::FR4_2x400G:
    case MediaInterfaceCode::FR4_LITE_2x400G:
    case MediaInterfaceCode::FR4_LPO_2x400G:
    case MediaInterfaceCode::DR4_2x400G:
    case MediaInterfaceCode::FR8_800G:
    case MediaInterfaceCode::LR4_2x400G_10KM:
    case MediaInterfaceCode::CR8_800G:
      return constants::OPTIC_TYPE_800_GENERIC();
    case MediaInterfaceCode::ZR_800G:
      return constants::OPTIC_TYPE_800_ZR();
    case MediaInterfaceCode::DR4_2x800G:
    case MediaInterfaceCode::FR4_2x800G:
    case MediaInterfaceCode::DR8_1600G:
    case MediaInterfaceCode::CR8_1600G:
      return constants::OPTIC_TYPE_1600_GENERIC();
    default:
      return std::nullopt;
  }
}

Bsp::Bsp(const FanServiceConfig& config) : config_(config) {
  fsdbPubSubMgr_ = std::make_unique<fsdb::FsdbPubSubManager>("fan_service");
  fsdbSensorSubscriber_ =
      std::make_unique<FsdbSensorSubscriber>(fsdbPubSubMgr_.get());
  if (FLAGS_subscribe_to_stats_from_fsdb) {
    fsdbSensorSubscriber_->subscribeToSensorServiceStat();
    fsdbSensorSubscriber_->subscribeToAgentStat();
    if (FLAGS_subscribe_to_qsfp_data_from_fsdb) {
      fsdbSensorSubscriber_->subscribeToQsfpServiceStat();
      fsdbSensorSubscriber_->subscribeToQsfpServiceState();
    }
  }
  thread_.reset(new std::thread([=, this] {
    folly::setThreadName("bsp-evb-thread");
    evbSensor_.loopForever();
  }));
}

void Bsp::getSensorData(std::shared_ptr<SensorData> pSensorData) {
  if (FLAGS_subscribe_to_stats_from_fsdb) {
    auto subscribedData = fsdbSensorSubscriber_->getSensorData();
    bool fallbackToThrift =
        subscribedData.empty() || fsdbSensorSubscriber_->isSensorDataStale();
    if (fallbackToThrift) {
      XLOG(WARNING) << "FSDB sensor data is empty or stale, falling back to "
                       "thrift from sensor_service";
      fb303::fbData->setCounter(kFsdbSensorDataThriftFallback, 1);
      getSensorDataThrift(pSensorData);
    } else {
      fb303::fbData->setCounter(kFsdbSensorDataThriftFallback, 0);
      for (const auto& [sensorName, sensorData] : subscribedData) {
        if (sensorData.value().has_value() &&
            sensorData.timeStamp().has_value()) {
          pSensorData->updateSensorEntry(
              *sensorData.name(), *sensorData.value(), *sensorData.timeStamp());
        }
      }
      XLOG(INFO) << fmt::format(
          "Got sensor data from fsdb.  Item count: {}", subscribedData.size());
    }
  } else {
    getSensorDataThrift(pSensorData);
  }

  if (!initialSensorDataRead_) {
    initialSensorDataRead_ = true;
  }
}
bool Bsp::checkIfInitialSensorDataRead() const {
  return initialSensorDataRead_;
}

int Bsp::emergencyShutdown(bool enable) {
  int rc = 0;
  bool currentState = getEmergencyState();
  if (enable && !currentState) {
    if (config_.shutdownCmd()->empty()) {
      XLOG(ERR) << "Emergency shutdown called but shutdownCmd is empty!";
      return -1;
    }
    auto [exitStatus, standardOut] =
        PlatformUtils().execCommand(*config_.shutdownCmd());
    rc = exitStatus;
    setEmergencyState(enable);
  }
  return rc;
}

void Bsp::kickWatchdog() {
  if (!config_.watchdog()) {
    return;
  }
  std::string valueStr = std::to_string(*config_.watchdog()->value());
  if (!writeToWatchdog(valueStr)) {
    XLOG(ERR) << "Failed to kick watchdog";
  }
  XLOG_EVERY_MS(INFO, 5000) << "Watchdog has been kicked";
}

void Bsp::closeWatchdog() {
  if (!watchdogFd_.has_value()) {
    return;
  }
  std::cout << "Closing watchdog" << std::endl;
  try {
    writeToWatchdog("V");
  } catch (std::exception& e) {
    XLOG(ERR) << "Error magic closing watchdog: " << e.what();
  }
  close(watchdogFd_.value());
  watchdogFd_.reset();
}

bool Bsp::writeToWatchdog(const std::string& value) {
  auto writeFd = [](int fd, const std::string& val) {
    auto ret = write(fd, val.c_str(), val.size());
    if (ret < 0) {
      XLOG(ERR) << "Failed to write to file descriptor " << fd << ": "
                << folly::errnoStr(errno);
    }
    return ret > 0;
  };
  if (!config_.watchdog().has_value()) {
    return false;
  }
  auto sysfsPath = config_.watchdog()->sysfsPath()->c_str();
  if (!watchdogFd_.has_value()) {
    int fd = open(sysfsPath, O_WRONLY);
    if (fd < 0) {
      XLOG(ERR) << fmt::format("Failed to open watchdog file: {}", sysfsPath);
      return false;
    }
    watchdogFd_ = fd;
    XLOG(INFO) << fmt::format(
        "Opened watchdog file: {} ({})", sysfsPath, watchdogFd_.value());
  }
  bool res = false;
  try {
    res = writeFd(watchdogFd_.value(), value);
  } catch (std::exception& e) {
    XLOG(ERR) << "Could not write to watchdog: " << e.what();
    res = false;
  }
  return res;
}

std::map<std::string, std::vector<OpticData>> Bsp::processOpticEntries(
    const Optic& opticsGroup,
    uint64_t& currentQsfpSvcTimestamp,
    const std::map<int32_t, TransceiverInfo>& transceiverInfoMap) {
  std::map<std::string, std::vector<OpticData>> data{};
  std::vector<int32_t> txvrsIdsWithNoData;

  if (opticsGroup.tempToPwmMaps()->empty() &&
      opticsGroup.pidSettings()->empty()) {
    XLOG(ERR) << "Optic group has no tempToPwmMaps or pidSettings";
    return data;
  }
  const auto& unknownFallback = !opticsGroup.tempToPwmMaps()->empty()
      ? opticsGroup.tempToPwmMaps()->begin()->first
      : opticsGroup.pidSettings()->begin()->first;

  for (const auto& [xvrId, transceiverInfo] : transceiverInfoMap) {
    const TcvrState& tcvrState = *transceiverInfo.tcvrState();
    const TcvrStats& tcvrStats = *transceiverInfo.tcvrStats();

    if (!tcvrStats.sensor()) {
      txvrsIdsWithNoData.push_back(xvrId);
      continue;
    }

    if (*tcvrStats.timeCollected() > currentQsfpSvcTimestamp) {
      currentQsfpSvcTimestamp = *tcvrStats.timeCollected();
    }

    float temp = static_cast<float>(*(tcvrStats.sensor()->temp()->value()));
    // Skip entries where temperature is 0.0 - meaning the port is
    // not populated in qsfp_service or read failure occurred.
    if (temp == 0.0) {
      continue;
    }

    auto opticType = getOpticTypeFromMediaCode(
        tcvrState.moduleMediaInterface().to_optional(), unknownFallback);
    if (!opticType) {
      auto mediaCode = tcvrState.moduleMediaInterface().to_optional();
      XLOG(INFO) << fmt::format(
          "Transceiver id {} has unsupported media type {}. Ignoring.",
          xvrId,
          mediaCode ? static_cast<int>(*mediaCode) : -1);
      continue;
    }
    data[*opticType].push_back(OpticData{xvrId, temp});
  }

  if (!txvrsIdsWithNoData.empty()) {
    XLOG(INFO) << fmt::format(
        "Transceivers with no data (ignored): {}",
        folly::join(", ", txvrsIdsWithNoData));
  }
  if (!data.empty()) {
    for (const auto& [opticType, transceivers] : data) {
      std::vector<std::string> tempStrings;
      tempStrings.reserve(transceivers.size());
      for (const auto& opticData : transceivers) {
        tempStrings.push_back(
            fmt::format("{}({:.1f}C)", opticData.txvrId, opticData.temp));
      }
      XLOG(INFO) << fmt::format(
          "Transceivers with data [{}]: {}",
          opticType,
          folly::join(", ", tempStrings));
    }
  }

  return data;
}

void Bsp::getOpticData(
    const Optic& opticsGroup,
    std::shared_ptr<SensorData> pSensorData) {
  std::map<int32_t, TransceiverInfo> transceiverInfoMap{};
  uint64_t currentQsfpSvcTimestamp = 0;
  try {
    if (FLAGS_subscribe_to_stats_from_fsdb &&
        FLAGS_subscribe_to_qsfp_data_from_fsdb) {
      auto subscribedQsfpDataState = fsdbSensorSubscriber_->getTcvrState();
      auto subscribedQsfpDataStats = fsdbSensorSubscriber_->getTcvrStats();
      for (const auto& [tcvrId, tcvrState] : subscribedQsfpDataState) {
        auto tcvrStatIt = subscribedQsfpDataStats.find(tcvrId);
        // Expect to see a stat if state exists. If not, ignore this port
        if (tcvrStatIt == subscribedQsfpDataStats.end()) {
          continue;
        }
        TransceiverInfo transceiverInfo{};
        transceiverInfo.tcvrState() = tcvrState;
        transceiverInfo.tcvrStats() = tcvrStatIt->second;
        transceiverInfoMap.emplace(tcvrId, transceiverInfo);
      }
    } else {
      getTransceivers(transceiverInfoMap, evb_);
    }
  } catch (std::exception& e) {
    XLOG(ERR) << "Failed to read optics data from Qsfp for "
              << *opticsGroup.opticName()
              << ", exception: " << folly::exceptionStr(e);
    // If thrift fails, just return without updating sensor data
    // control logic will see that the timestamp did not change,
    // and do the right error handling.
    return;
  }

  // Parse the data
  auto data = processOpticEntries(
      opticsGroup, currentQsfpSvcTimestamp, transceiverInfoMap);

  auto opticEntry = pSensorData->getOpticEntry(*opticsGroup.opticName());
  // Using the timestamp, check if the data is too old or not.
  // QsfpService's cache is updated every 30 seconds. So we check
  // both of the following, to see if this data is old data :
  // 1. qsfpService timestamp is still old
  // 2. fanService timestamp is more than 60 seconds ago
  // If both condition meet, we can infer that we did not get
  // any new data from qsfpService for more than 30 seconds :
  // In this case, we consider the cache data is not meaningful.
  uint64_t now = getCurrentTime();
  if (currentQsfpSvcTimestamp == opticEntry->qsfpServiceTimeStamp &&
      now > opticEntry->lastOpticsUpdateTimeInSec + 60) {
    pSensorData->resetOpticData(*opticsGroup.opticName());
  } else {
    pSensorData->updateOpticEntry(
        *opticsGroup.opticName(), data, currentQsfpSvcTimestamp);
  }
  auto qsfpTimestamp = static_cast<time_t>(currentQsfpSvcTimestamp);
  std::tm qsfpTm{};
  localtime_r(&qsfpTimestamp, &qsfpTm);
  XLOG(INFO) << fmt::format(
      "Got optics data from Qsfp. OpticTypes: {}. QsfpSvcTimestamp: {} ({:%Y-%m-%d %H:%M:%S})",
      data.size(),
      currentQsfpSvcTimestamp,
      qsfpTm);
}

void Bsp::getOpticsData(std::shared_ptr<SensorData> pSensorData) {
  for (const auto& optic : *config_.optics()) {
    getOpticData(optic, pSensorData);
  }
}

void Bsp::getAsicTempData(const std::shared_ptr<SensorData>& pSensorData) {
  if (FLAGS_subscribe_to_stats_from_fsdb) {
    getAsicTempThroughFsdb(pSensorData);
  } else {
    getAsicTempDataFromSensorService(pSensorData);
  }
}

void Bsp::getAsicTempThroughFsdb(
    const std::shared_ptr<SensorData>& pSensorData) {
  try {
    auto subscribedData = fsdbSensorSubscriber_->getAgentData();
    for (const auto& [asicTempName, asicTempData] : subscribedData) {
      // Only parse the entry with value and timestamp set.
      if (asicTempData.value().has_value() &&
          asicTempData.timeStamp().has_value()) {
        pSensorData->updateSensorEntry(
            *asicTempData.name(),
            *asicTempData.value(),
            *asicTempData.timeStamp());
      }
    }
    XLOG(INFO) << fmt::format(
        "Got ASIC Temp data from fsdb.  Item count: {}", subscribedData.size());
  } catch (std::exception& e) {
    XLOG(ERR) << "Failed to get ASIC temp data using FSDB, with error : "
              << e.what();
  }
}

void Bsp::getAsicTempDataFromSensorService(
    const std::shared_ptr<SensorData>& pSensorData) {
  try {
    auto sensorReadResponse =
        getAsicTempThroughSensorService(kSensordThriftPort, evbSensor_);
    for (const auto& asicTempData : *sensorReadResponse.asicTempData()) {
      // Again, for Thrift too, only honor the entry with value and timestamp.
      if (asicTempData.value() && asicTempData.timeStamp()) {
        pSensorData->updateSensorEntry(
            *asicTempData.name(),
            *asicTempData.value(),
            *asicTempData.timeStamp());
      }
    }
    XLOG(INFO) << fmt::format(
        "Got ASIC Temp data from sensor_service.  Item count: {}",
        sensorReadResponse.asicTempData()->size());
  } catch (std::exception& e) {
    XLOG(ERR) << "Failed to get ASIC temp data from sensor_service: "
              << e.what();
  }
}

uint64_t Bsp::getCurrentTime() const {
  return facebook::WallClockUtil::NowInSecFast();
}

bool Bsp::getEmergencyState() const {
  return emergencyShutdownState_;
}

void Bsp::setEmergencyState(bool state) {
  emergencyShutdownState_ = state;
}

void Bsp::getSensorDataThrift(std::shared_ptr<SensorData> pSensorData) {
  sensor_service::SensorReadResponse sensorReadResponse;
  try {
    sensorReadResponse =
        getSensorValueThroughThrift(kSensordThriftPort, evbSensor_);
  } catch (std::exception& e) {
    XLOG(ERR) << "Failed to get sensor data from sensor_service: " << e.what();
    return;
  }
  for (auto& sensorData : *sensorReadResponse.sensorData()) {
    if (sensorData.value() && sensorData.timeStamp()) {
      pSensorData->updateSensorEntry(
          *sensorData.name(), *sensorData.value(), *sensorData.timeStamp());
      auto timestamp = static_cast<time_t>(*sensorData.timeStamp());
      std::tm sensorTm{};
      localtime_r(&timestamp, &sensorTm);
      XLOG(DBG1) << fmt::format(
          "Storing sensor {} with value {} timestamp {} ({:%Y-%m-%d %H:%M:%S})",
          *sensorData.name(),
          *sensorData.value(),
          *sensorData.timeStamp(),
          sensorTm);
    }
  }
  XLOG(INFO) << fmt::format(
      "Got sensor data from sensor_service.  Item count: {}",
      sensorReadResponse.sensorData()->size());
}

float Bsp::readSysfs(const std::string& path) const {
  float retVal;
  std::string buf = facebook::fboss::readSysfs(path);
  try {
    retVal = std::stof(buf);
  } catch (const std::exception&) {
    XLOG(ERR) << "Failed to convert sysfs read to float!! ";
    throw;
  }
  return retVal;
}

bool Bsp::writeSysfs(const std::string& path, int value) {
  std::string valueStr = std::to_string(value);
  return facebook::fboss::writeSysfs(path, valueStr);
}

bool Bsp::setFanPwmSysfs(const std::string& path, int pwm) {
  return writeSysfs(path, pwm);
}

bool Bsp::turnOnLedSysfs(const std::string& path) {
  auto max_brightness = getLedMaxBrightness(path);
  return writeSysfs(
      path + "/brightness",
      max_brightness.has_value() ? max_brightness.value()
                                 : kDefaultMaxBrightness);
}

std::optional<int> Bsp::getLedMaxBrightness(const std::string& path) const {
  try {
    return std::stoi(facebook::fboss::readSysfs(path + "/max_brightness"));
  } catch (const std::exception&) {
    XLOG(ERR) << "Failed to read max brightness from " << path;
  }
  return std::nullopt;
}

Bsp::~Bsp() {
  if (thread_) {
    evbSensor_.runInEventBaseThread([this] { evbSensor_.terminateLoopSoon(); });
    thread_->join();
  }
  fsdbSensorSubscriber_.reset();
  fsdbPubSubMgr_.reset();
  closeWatchdog();
}

} // namespace facebook::fboss::platform::fan_service
