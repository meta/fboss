/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/rackmon/RackmonThriftHandler.h"
#include <glog/logging.h>
#include "fboss/platform/rackmon/RackmonConfig.h"

#include <fb303/ServiceData.h>

namespace rackmonsvc {

auto constexpr kDevCRCErrors = "dev{}.crc_errors";
auto constexpr kDevTimeouts = "dev{}.timeouts";
auto constexpr kDevMiscErrors = "dev{}.misc_errors";
auto constexpr kDevDeviceErrors = "dev{}.device_errors";
auto constexpr kDevIsActive = "dev{}.is_active";
auto constexpr kTotalCRCErrors = "rack.crc_errors";
auto constexpr kTotalTimeouts = "rack.timeouts";
auto constexpr kTotalMiscErrors = "rack.misc_errors";
auto constexpr kTotalDeviceErrors = "rack.device_errors";
auto constexpr kTotalActive = "rack.num_active";
auto constexpr kTotalDormant = "rack.num_dormant";

ModbusDeviceType typeFromString(const std::string& str) {
  if (str == "ORV2_PSU") {
    return ModbusDeviceType::ORV2_PSU;
  }
  if (str == "ORV3_PSU") {
    return ModbusDeviceType::ORV3_PSU;
  }
  if (str == "ORV3_BBU") {
    return ModbusDeviceType::ORV3_BBU;
  }
  throw std::runtime_error("Unknown PSU: " + str);
}

std::string typeToString(ModbusDeviceType type) {
  switch (type) {
    case ModbusDeviceType::ORV2_PSU:
      return "ORV2_PSU";
    case ModbusDeviceType::ORV3_PSU:
      return "ORV3_PSU";
    case ModbusDeviceType::ORV3_BBU:
      return "ORV3_BBU";
    default:
      break;
  }
  throw std::runtime_error("Unknown Type");
}

ModbusDeviceInfo ThriftHandler::transformModbusDeviceInfo(
    const rackmon::ModbusDeviceInfo& source) {
  ModbusDeviceInfo target;
  target.devAddress() = source.deviceAddress;
  target.crcErrors() = source.crcErrors;
  target.miscErrors() = source.miscErrors;
  target.baudrate() = source.baudrate;
  target.deviceType() = typeFromString(source.deviceType);
  target.mode() = source.mode == rackmon::ModbusDeviceMode::ACTIVE
      ? ModbusDeviceMode::ACTIVE
      : ModbusDeviceMode::DORMANT;
  target.uniqueDevAddress() =
      rackmon::DeviceLocationFilter::combine(source.port, source.deviceAddress);
  return target;
}

ModbusRegisterValue ThriftHandler::transformRegisterValue(
    const rackmon::RegisterValue& value) {
  ModbusRegisterValue target{};

  target.timestamp() = value.timestamp;
  switch (value.type) {
    case rackmon::RegisterValueType::HEX: {
      target.type() = RegisterValueType::RAW;
      auto& hexValue = std::get<std::vector<uint8_t>>(value.value);
      std::vector<int8_t> conv(hexValue.size());
      std::copy(hexValue.begin(), hexValue.end(), conv.begin());
      target.value()->rawValue() = conv;
    } break;
    case rackmon::RegisterValueType::INTEGER:
      target.type() = RegisterValueType::INTEGER;
      target.value()->intValue() = std::get<int32_t>(value.value);
      break;
    case rackmon::RegisterValueType::LONG:
      target.type() = RegisterValueType::LONG;
      target.value()->longValue() = std::get<int64_t>(value.value);
      break;
    case rackmon::RegisterValueType::STRING:
      target.type() = RegisterValueType::STRING;
      target.value()->strValue() = std::get<std::string>(value.value);
      break;
    case rackmon::RegisterValueType::FLOAT:
      target.type() = RegisterValueType::FLOAT;
      target.value()->floatValue() = std::get<float>(value.value);
      break;
    case rackmon::RegisterValueType::FLAGS: {
      target.type() = RegisterValueType::FLAGS;
      auto& flagsValue =
          std::get<rackmon::RegisterValue::FlagsType>(value.value);
      std::vector<FlagType> flags;
      for (const auto& [flagVal, flagName, flagPos] : flagsValue) {
        FlagType flag;
        flag.name() = flagName;
        flag.value() = flagVal;
        flag.bitOffset() = flagPos;
        flags.emplace_back(flag);
      }
      target.value()->flagsValue() = flags;
    } break;
    default:
      throw std::runtime_error("Unsupported Value");
  }
  return target;
}

ModbusRegisterStore ThriftHandler::transformRegisterStoreValue(
    const rackmon::RegisterStoreValue& source) {
  ModbusRegisterStore target;

  target.regAddress() = source.regAddr;
  target.name() = source.name;
  for (const auto& reg : source.history) {
    target.history()->emplace_back(transformRegisterValue(reg));
  }
  return target;
}

RackmonMonitorData ThriftHandler::transformModbusDeviceValueData(
    const rackmon::ModbusDeviceValueData& source) {
  RackmonMonitorData data;
  data.devInfo() = transformModbusDeviceInfo(source);
  for (const auto& reg : source.registerList) {
    data.regList()->emplace_back(transformRegisterStoreValue(reg));
  }
  return data;
}

RackmonStatusCode ThriftHandler::exceptionToStatusCode(
    std::exception& baseException) {
  if (rackmon::TimeoutException* ex;
      (ex = dynamic_cast<rackmon::TimeoutException*>(&baseException)) !=
      nullptr) {
    return RackmonStatusCode::ERR_TIMEOUT;
  } else if (rackmon::CRCError* crcEx;
             (crcEx = dynamic_cast<rackmon::CRCError*>(&baseException)) !=
             nullptr) {
    return RackmonStatusCode::ERR_BAD_CRC;
  } else if (rackmon::ModbusError* modbusEx;
             (modbusEx = dynamic_cast<rackmon::ModbusError*>(&baseException)) !=
             nullptr) {
    switch (modbusEx->errorCode) {
      case rackmon::ModbusErrorCode::ILLEGAL_FUNCTION:
        return RackmonStatusCode::ERR_ILLEGAL_FUNCTION;
      case rackmon::ModbusErrorCode::ILLEGAL_DATA_ADDRESS:
        return RackmonStatusCode::ERR_ILLEGAL_DATA_ADDRESS;
      case rackmon::ModbusErrorCode::ILLEGAL_DATA_VALUE:
        return RackmonStatusCode::ERR_ILLEGAL_DATA_VALUE;
      case rackmon::ModbusErrorCode::SLAVE_DEVICE_FAILURE:
        return RackmonStatusCode::ERR_SLAVE_DEVICE_FAILURE;
      case rackmon::ModbusErrorCode::ACKNOWLEDGE:
        return RackmonStatusCode::ERR_ACKNOWLEDGE;
      case rackmon::ModbusErrorCode::SLAVE_DEVICE_BUSY:
        return RackmonStatusCode::ERR_SLAVE_DEVICE_BUSY;
      case rackmon::ModbusErrorCode::NEGATIVE_ACKNOWLEDGE:
        return RackmonStatusCode::ERR_NEGATIVE_ACKNOWLEDGE;
      case rackmon::ModbusErrorCode::MEMORY_PARITY_ERROR:
        return RackmonStatusCode::ERR_MEMORY_PARITY_ERROR;
      default:
        return RackmonStatusCode::ERR_UNDEFINED_MODBUS_ERROR;
    }
  } else if (std::underflow_error* underflowEx;
             (underflowEx = dynamic_cast<std::underflow_error*>(
                  &baseException)) != nullptr) {
    return RackmonStatusCode::ERR_UNDERFLOW;
  } else if (std::overflow_error* overflowEx;
             (overflowEx = dynamic_cast<std::overflow_error*>(
                  &baseException)) != nullptr) {
    return RackmonStatusCode::ERR_OVERFLOW;
  } else if (std::logic_error* logicEx;
             (logicEx = dynamic_cast<std::logic_error*>(&baseException)) !=
             nullptr) {
    return RackmonStatusCode::ERR_INVALID_ARGS;
  } else if (std::out_of_range* rangeEx;
             (rangeEx = dynamic_cast<std::out_of_range*>(&baseException)) !=
             nullptr) {
    return RackmonStatusCode::ERR_INVALID_ARGS;
  }
  return RackmonStatusCode::ERR_IO_FAILURE;
}

void ThriftHandler::serviceMonitor() {
  auto devs = rackmond_.listDevices();
  uint32_t crcErrors = 0;
  uint32_t timeouts = 0;
  uint32_t miscErrors = 0;
  uint32_t devErrors = 0;
  uint32_t numActive = 0;
  uint32_t numDormant = 0;
  for (auto& dev : devs) {
    uint32_t active = dev.mode == rackmon::ModbusDeviceMode::ACTIVE ? 1 : 0;
    uint32_t devAddress = +dev.deviceAddress;
    facebook::fb303::fbData->setCounter(
        fmt::format(kDevCRCErrors, devAddress), dev.crcErrors);
    facebook::fb303::fbData->setCounter(
        fmt::format(kDevTimeouts, devAddress), dev.timeouts);
    facebook::fb303::fbData->setCounter(
        fmt::format(kDevMiscErrors, devAddress), dev.miscErrors);
    facebook::fb303::fbData->setCounter(
        fmt::format(kDevDeviceErrors, devAddress), dev.deviceErrors);
    facebook::fb303::fbData->setCounter(
        fmt::format(kDevIsActive, devAddress), active);
    crcErrors += dev.crcErrors;
    timeouts += dev.timeouts;
    miscErrors += dev.miscErrors;
    devErrors += dev.deviceErrors;
    numActive += active;
    numDormant += active ? 0 : 1;
  }
  facebook::fb303::fbData->setCounter(kTotalCRCErrors, crcErrors);
  facebook::fb303::fbData->setCounter(kTotalMiscErrors, miscErrors);
  facebook::fb303::fbData->setCounter(kTotalDeviceErrors, devErrors);
  facebook::fb303::fbData->setCounter(kTotalTimeouts, timeouts);
  facebook::fb303::fbData->setCounter(kTotalActive, numActive);
  facebook::fb303::fbData->setCounter(kTotalDormant, numDormant);
}

ThriftHandler::ThriftHandler() {
  rackmond_.loadInterface(nlohmann::json::parse(getInterfaceConfig()));
  const std::vector<std::string> regMaps = getRegisterMapConfig();
  for (const auto& regmap : regMaps) {
    rackmond_.loadRegisterMap(nlohmann::json::parse(regmap));
  }
  rackmond_.start();

  plsManager_.loadPlsConfig(nlohmann::json::parse(getRackmonPlsConfig()));

  monThread_ = std::make_shared<rackmon::PollThread<ThriftHandler>>(
      &ThriftHandler::serviceMonitor, this, rackmon::PollThreadTime(10));
  monThread_->start();
}

void ThriftHandler::listModbusDevices(std::vector<ModbusDeviceInfo>& devices) {
  std::vector<rackmon::ModbusDeviceInfo> info = rackmond_.listDevices();
  for (auto& dev : info) {
    devices.emplace_back(transformModbusDeviceInfo(dev));
  }
}

void ThriftHandler::getMonitorData(std::vector<RackmonMonitorData>& data) {
  std::vector<rackmon::ModbusDeviceValueData> indata;
  rackmond_.getValueData(indata);
  for (auto& dev : indata) {
    data.emplace_back(transformModbusDeviceValueData(dev));
  }
}

void ThriftHandler::transformMonitorDataFilter(
    const MonitorDataFilter& filter,
    rackmon::ModbusDeviceFilter& devFilter,
    rackmon::ModbusRegisterFilter& regFilter,
    bool& latestOnly) {
  const DeviceFilter* reqDevFilter =
      apache::thrift::get_pointer(filter.deviceFilter());
  const RegisterFilter* reqRegFilter =
      apache::thrift::get_pointer(filter.registerFilter());
  latestOnly = folly::copy(filter.latestValueOnly().value());
  if (reqDevFilter) {
    if (reqDevFilter->addressFilter().has_value()) {
      std::set<uint16_t> unsignedDevs;
      for (const auto& value : reqDevFilter->addressFilter().value()) {
        unsignedDevs.insert(static_cast<uint16_t>(value));
      }
      devFilter.locationFilter = rackmon::DeviceLocationFilter(unsignedDevs);
    } else if (reqDevFilter->typeFilter().has_value()) {
      std::set<ModbusDeviceType> types = reqDevFilter->typeFilter().value();
      devFilter.typeFilter.emplace();
      for (auto& type : types) {
        devFilter.typeFilter->insert(typeToString(type));
      }
    } else {
      LOG(ERROR) << "Unsupported empty dev filter" << std::endl;
    }
  }
  if (reqRegFilter) {
    if (reqRegFilter->addressFilter().has_value()) {
      std::set<int32_t> regs = reqRegFilter->addressFilter().value();
      regFilter.addrFilter.emplace();
      for (auto& addr : regs) {
        regFilter.addrFilter->insert(uint16_t(addr));
      }
    } else if (reqRegFilter->nameFilter().has_value()) {
      regFilter.nameFilter = reqRegFilter->nameFilter().value();
    } else {
      LOG(ERROR) << "Unsupported empty reg filter" << std::endl;
    }
  }
}

void ThriftHandler::getMonitorDataEx(
    std::vector<RackmonMonitorData>& data,
    std::unique_ptr<MonitorDataFilter> filter) {
  rackmon::ModbusDeviceFilter devFilter{};
  rackmon::ModbusRegisterFilter regFilter{};
  bool latestOnly{};
  transformMonitorDataFilter(*filter, devFilter, regFilter, latestOnly);
  std::vector<rackmon::ModbusDeviceValueData> indata;
  rackmond_.getValueData(indata, devFilter, regFilter, latestOnly);
  for (auto& dev : indata) {
    data.emplace_back(transformModbusDeviceValueData(dev));
  }
}

void ThriftHandler::reload(
    std::unique_ptr<MonitorDataFilter> filter,
    bool synchronous) {
  rackmon::ModbusDeviceFilter devFilter{};
  rackmon::ModbusRegisterFilter regFilter{};
  bool latestOnly{};
  transformMonitorDataFilter(*filter, devFilter, regFilter, latestOnly);
  if (synchronous) {
    rackmond_.reload(devFilter, regFilter);
  } else {
    auto tid = std::thread([this, devFilter, regFilter]() {
      try {
        rackmond_.reload(devFilter, regFilter);
      } catch (std::exception& e) {
        LOG(ERROR) << "Async reload failed: " << e.what() << std::endl;
      }
    });
    tid.detach();
  }
}

void ThriftHandler::readHoldingRegisters(
    ReadWordRegistersResponse& response,
    std::unique_ptr<ReadWordRegistersRequest> request) {
  if (folly::copy(request->numRegisters().value()) > kMaxNumRegisters) {
    // Modbus protocol has a 1 byte size field, anything
    // more than 128 registers would fail. Return error
    // early.
    response.status() = RackmonStatusCode::ERR_INVALID_ARGS;
    return;
  }
  try {
    std::vector<uint16_t> regs(folly::copy(request->numRegisters().value()));
    int32_t* timeout = apache::thrift::get_pointer(request->timeout());
    auto [port, devAddr] = rackmon::DeviceLocationFilter::decompose(
        folly::copy(request->devAddress().value()));
    uint16_t regAddr = folly::copy(request->regAddress().value());
    if (timeout) {
      rackmon::ModbusTime tmo(*timeout);
      rackmond_.readHoldingRegisters(devAddr, port, regAddr, regs, tmo);
    } else {
      rackmond_.readHoldingRegisters(devAddr, port, regAddr, regs);
    }
    response.status() = RackmonStatusCode::SUCCESS;
    response.regValues()->resize(regs.size());
    std::copy(regs.begin(), regs.end(), response.regValues()->begin());
  } catch (std::exception& ex) {
    response.status() = exceptionToStatusCode(ex);
  }
}

RackmonStatusCode ThriftHandler::writeSingleRegister(
    std::unique_ptr<WriteSingleRegisterRequest> request) {
  try {
    int32_t* timeout = apache::thrift::get_pointer(request->timeout());
    auto [port, devAddr] = rackmon::DeviceLocationFilter::decompose(
        folly::copy(request->devAddress().value()));
    uint16_t regAddr = folly::copy(request->regAddress().value());
    uint16_t regValue = folly::copy(request->regValue().value());
    if (timeout) {
      rackmon::ModbusTime tmo(*timeout);
      rackmond_.writeSingleRegister(devAddr, port, regAddr, regValue, tmo);
    } else {
      rackmond_.writeSingleRegister(devAddr, port, regAddr, regValue);
    }
    return RackmonStatusCode::SUCCESS;
  } catch (std::exception& ex) {
    return exceptionToStatusCode(ex);
  }
}

RackmonStatusCode ThriftHandler::presetMultipleRegisters(
    std::unique_ptr<PresetMultipleRegistersRequest> request) {
  if (request->regValue().value().size() > kMaxNumRegisters) {
    return RackmonStatusCode::ERR_INVALID_ARGS;
  }
  try {
    int32_t* timeout = apache::thrift::get_pointer(request->timeout());
    auto [port, devAddr] = rackmon::DeviceLocationFilter::decompose(
        folly::copy(request->devAddress().value()));
    uint16_t regAddr = folly::copy(request->regAddress().value());
    std::vector<uint16_t> regValues;
    std::copy(
        request->regValue().value().begin(),
        request->regValue().value().end(),
        std::back_inserter(regValues));
    if (timeout) {
      rackmon::ModbusTime tmo(*timeout);
      rackmond_.writeMultipleRegisters(devAddr, port, regAddr, regValues, tmo);
    } else {
      rackmond_.writeMultipleRegisters(devAddr, port, regAddr, regValues);
    }
    return RackmonStatusCode::SUCCESS;
  } catch (std::exception& ex) {
    return exceptionToStatusCode(ex);
  }
}

void ThriftHandler::readFileRecord(
    ReadFileRecordResponse& response,
    std::unique_ptr<ReadFileRecordRequest> request) {
  try {
    int32_t* timeout = apache::thrift::get_pointer(request->timeout());
    auto [port, devAddr] = rackmon::DeviceLocationFilter::decompose(
        folly::copy(request->devAddress().value()));
    std::vector<rackmon::FileRecord> records;
    for (const auto& rec : request->records().value()) {
      if (folly::copy(rec.dataSize().value()) > kMaxNumRegisters) {
        throw std::out_of_range("Request Registers");
      }
      records.emplace_back(
          (uint16_t)folly::copy(rec.fileNum().value()),
          (uint16_t)folly::copy(rec.recordNum().value()),
          (size_t)folly::copy(rec.dataSize().value()));
    }
    if (timeout) {
      rackmon::ModbusTime tmo(*timeout);
      rackmond_.readFileRecord(devAddr, port, records, tmo);
    } else {
      rackmond_.readFileRecord(devAddr, port, records);
    }
    response.status() = RackmonStatusCode::SUCCESS;
    auto transformFileRecord = [](const auto& rec) {
      FileRecord ret;
      ret.fileNum() = rec.fileNum;
      ret.recordNum() = rec.recordNum;
      std::vector<int32_t> data(rec.data.size());
      std::copy(rec.data.begin(), rec.data.end(), data.begin());
      ret.data() = data;
      return ret;
    };
    for (const auto& rec : records) {
      response.data()->emplace_back(transformFileRecord(rec));
    }
  } catch (std::exception& ex) {
    response.status() = exceptionToStatusCode(ex);
  }
}

RackmonStatusCode ThriftHandler::controlRackmond(
    RackmonControlRequest request) {
  try {
    switch (request) {
      case RackmonControlRequest::PAUSE_RACKMOND:
        rackmond_.stop();
        break;
      case RackmonControlRequest::RESUME_RACKMOND:
        rackmond_.start();
        break;
      case RackmonControlRequest::RESCAN:
        rackmond_.forceScan();
        break;
      default:
        return RackmonStatusCode::ERR_INVALID_ARGS;
    }
  } catch (std::exception& ex) {
    return exceptionToStatusCode(ex);
  }
  return RackmonStatusCode::SUCCESS;
}

void ThriftHandler::getPowerLossSiren(PowerLossSiren& pls) {
  std::vector<std::pair<bool, bool>> plsInfo;

  plsInfo = plsManager_.getPowerState();
  if (plsInfo.size() != 3) {
    throw std::runtime_error("failed to get power state from all 3 ports");
  }

  pls.port1()->powerLost() = plsInfo[0].first;
  pls.port1()->redundancyLost() = plsInfo[0].second;
  pls.port2()->powerLost() = plsInfo[1].first;
  pls.port2()->redundancyLost() = plsInfo[1].second;
  pls.port3()->powerLost() = plsInfo[2].first;
  pls.port3()->redundancyLost() = plsInfo[2].second;
}

void ThriftHandler::sendRawCommand(
    RawCommandResponse& response,
    std::unique_ptr<RawCommandRequest> request) {
  try {
    rackmon::Request req;
    rackmon::Response resp;

    // Copy raw command bytes from Thrift request to Modbus Request
    for (auto byte : *request->cmd()) {
      req << static_cast<uint8_t>(byte);
    }

    // Validate and set expected response length
    auto responseLength = folly::copy(request->responseLength().value());
    if (responseLength <= 0 ||
        static_cast<size_t>(responseLength) > rackmon::Msg::kMaxModbusLength) {
      response.status() = RackmonStatusCode::ERR_INVALID_ARGS;
      return;
    }
    resp.len = static_cast<size_t>(responseLength);

    // Get timeout (optional, defaults to 0)
    int32_t* timeout = apache::thrift::get_pointer(request->timeout());
    rackmon::ModbusTime tmo =
        timeout ? rackmon::ModbusTime(*timeout) : rackmon::ModbusTime::zero();

    // Execute raw command
    // Note: uniqueDevAddress is currently ignored in this version of rackmond
    // The device address is already embedded in the raw command bytes
    std::optional<uint16_t> uniqueDevAddr;
    if (request->uniqueDevAddress().has_value()) {
      uniqueDevAddr = static_cast<uint16_t>(*request->uniqueDevAddress());
    }
    rackmond_.rawCmd(req, uniqueDevAddr, resp, tmo);

    // Copy response bytes to Thrift response (excluding CRC which was added
    // back)
    response.status() = RackmonStatusCode::SUCCESS;
    response.data()->clear();
    // resp.len includes CRC (2 bytes), remove them for clean response
    for (size_t i = 0; i < resp.len - 2; i++) {
      response.data()->push_back(static_cast<int8_t>(resp.raw[i]));
    }
  } catch (std::exception& ex) {
    response.status() = exceptionToStatusCode(ex);
    response.data()->clear();
  }
}
} // namespace rackmonsvc
