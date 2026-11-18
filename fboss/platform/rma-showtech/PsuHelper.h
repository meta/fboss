#pragma once

#include <array>
#include <vector>

#include "fboss/lib/i2c/I2cDevIo.h"

namespace facebook::fboss::platform {

class PsuHelper {
 public:
  enum class ValueType { ASCII, HEX, LINEAR_11, LINEAR_16 };
  using PsuRegister = std::tuple<std::string_view, uint8_t, int, ValueType>;

  PsuHelper(int busNum, int devAddr);

  void dumpRegisters() const;

 private:
  int devAddr_;
  std::unique_ptr<I2cDevIo> i2cPsuIo_;
  std::vector<uint8_t> readRegister(const PsuRegister& reg) const;

  static constexpr std::array<PsuRegister, 31> genericPsuRegisters_{{
      // { Name, Offset, Size, DataType }
      {"MFR_ID", 0x99, 0, ValueType::ASCII},
      {"MFR_MODEL", 0x9A, 0, ValueType::ASCII},
      {"MFR_REVISION", 0x9B, 0, ValueType::ASCII},
      {"MFR_LOCATION", 0x9C, 0, ValueType::ASCII},
      {"MFR_DATE", 0x9D, 0, ValueType::ASCII},
      {"MFR_SERIAL", 0x9E, 0, ValueType::ASCII},
      {"MFR_POUT_MAX", 0xA7, 2, ValueType::LINEAR_11},
      {"WRITE_PROTECT", 0x10, 1, ValueType::HEX},
      {"STATUS_BYTE", 0x78, 1, ValueType::HEX},
      {"STATUS_WORD", 0x79, 2, ValueType::HEX},
      {"STATUS_VOUT", 0x7A, 1, ValueType::HEX},
      {"STATUS_IOUT", 0x7B, 1, ValueType::HEX},
      {"STATUS_INPUT", 0x7C, 1, ValueType::HEX},
      {"STATUS_TEMPERATURE", 0x7D, 1, ValueType::HEX},
      {"STATUS_CML", 0x7E, 1, ValueType::HEX},
      {"STATUS_FANS_1_2", 0x81, 1, ValueType::HEX},
      {"FAN_CONFIG_1_2", 0x3A, 1, ValueType::HEX},
      {"FAN_COMMAND_1", 0x3B, 2, ValueType::LINEAR_11},
      {"FAN_COMMAND_2", 0x3C, 2, ValueType::LINEAR_11},
      {"READ_VIN", 0x88, 2, ValueType::LINEAR_11},
      {"READ_IIN", 0x89, 2, ValueType::LINEAR_11},
      {"READ_VOUT", 0x8B, 2, ValueType::LINEAR_16},
      {"READ_IOUT", 0x8C, 2, ValueType::LINEAR_11},
      {"READ_TEMPERATURE_1", 0x8D, 2, ValueType::LINEAR_11},
      {"READ_TEMPERATURE_2", 0x8E, 2, ValueType::LINEAR_11},
      {"READ_TEMPERATURE_3", 0x8F, 2, ValueType::LINEAR_11},
      {"READ_FAN_SPEED_1", 0x90, 2, ValueType::LINEAR_11},
      {"READ_FAN_SPEED_2", 0x91, 2, ValueType::LINEAR_11},
      {"READ_POUT", 0x96, 2, ValueType::LINEAR_11},
      {"READ_PIN", 0x97, 2, ValueType::LINEAR_11},
      {"PMBUS_REVISION", 0x98, 1, ValueType::HEX},
  }};
  static constexpr PsuRegister voutModeRegister_{
      "VOUT_MODE",
      0x20,
      1,
      ValueType::HEX};
};

} // namespace facebook::fboss::platform
