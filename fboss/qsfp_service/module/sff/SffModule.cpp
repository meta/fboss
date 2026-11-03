// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/qsfp_service/module/sff/SffModule.h"
#include "fboss/qsfp_service/module/QsfpHelper.h"

#include <assert.h>
#include <boost/assign.hpp>
#include <iomanip>
#include <string>
#include "fboss/agent/FbossError.h"
#include "fboss/lib/usb/TransceiverI2CApi.h"
#include "fboss/qsfp_service/StatsPublisher.h"
#include "fboss/qsfp_service/lib/QsfpConfigParserHelper.h"
#include "fboss/qsfp_service/module/QsfpFieldInfo.h"
#include "fboss/qsfp_service/module/TransceiverImpl.h"
#include "fboss/qsfp_service/module/sff/SffFieldInfo.h"

#include <fmt/core.h>
#include <folly/io/IOBuf.h>
#include <folly/io/async/EventBase.h>
#include <folly/logging/xlog.h>

#include <thrift/lib/cpp/util/EnumUtils.h>

using folly::IOBuf;
using std::lock_guard;
using std::memcpy;
using std::mutex;

using namespace apache::thrift;

namespace {

constexpr int kUsecBetweenPowerModeFlap = 100000;
// Temperature thresholds for CWDM4_100G optics special handling
DEFINE_int32(
    cwdm4_100g_high_temp_alarm_threshold,
    60,
    "High temperature alarm threshold for CWDM4_100G optics in degrees Celsius");
DEFINE_int32(
    cwdm4_100g_high_temp_warning_threshold,
    55,
    "High temperature warning threshold for CWDM4_100G optics in degrees Celsius");

bool cdrSupportedSpeed(facebook::fboss::cfg::PortSpeed speed) {
  return speed == facebook::fboss::cfg::PortSpeed::HUNDREDG ||
      speed == facebook::fboss::cfg::PortSpeed::FIFTYG ||
      speed == facebook::fboss::cfg::PortSpeed::TWENTYFIVEG;
}
} // namespace

namespace facebook {
namespace fboss {

using namespace facebook::fboss::phy;

// As per SFF-8636
static QsfpFieldInfo<SffField, SffPages>::QsfpFieldMap qsfpFields = {
    // Fields for reading an entire page
    {SffField::PAGE_LOWER, {SffPages::LOWER, 0, 128}},
    {SffField::PAGE_UPPER0, {SffPages::PAGE0, 128, 128}},
    {SffField::PAGE_UPPER3, {SffPages::PAGE3, 128, 128}},
    // Base page values, including alarms and sensors
    {SffField::IDENTIFIER, {SffPages::LOWER, 0, 1}},
    {SffField::STATUS, {SffPages::LOWER, 1, 2}},
    {SffField::LOS, {SffPages::LOWER, 3, 1}},
    {SffField::FAULT, {SffPages::LOWER, 4, 1}},
    {SffField::LOL, {SffPages::LOWER, 5, 1}},
    {SffField::TEMPERATURE_ALARMS, {SffPages::LOWER, 6, 1}},
    {SffField::VCC_ALARMS, {SffPages::LOWER, 7, 1}},
    {SffField::CHANNEL_RX_PWR_ALARMS, {SffPages::LOWER, 9, 2}},
    {SffField::CHANNEL_TX_BIAS_ALARMS, {SffPages::LOWER, 11, 2}},
    {SffField::CHANNEL_TX_PWR_ALARMS, {SffPages::LOWER, 13, 2}},
    {SffField::TEMPERATURE, {SffPages::LOWER, 22, 2}},
    {SffField::VCC, {SffPages::LOWER, 26, 2}},
    {SffField::CHANNEL_RX_PWR, {SffPages::LOWER, 34, 8}},
    {SffField::CHANNEL_TX_BIAS, {SffPages::LOWER, 42, 8}},
    {SffField::CHANNEL_TX_PWR, {SffPages::LOWER, 50, 8}},
    {SffField::TX_DISABLE, {SffPages::LOWER, 86, 1}},
    {SffField::RATE_SELECT_RX, {SffPages::LOWER, 87, 1}},
    {SffField::RATE_SELECT_TX, {SffPages::LOWER, 88, 1}},
    {SffField::POWER_CONTROL, {SffPages::LOWER, 93, 1}},
    {SffField::CDR_CONTROL, {SffPages::LOWER, 98, 1}},
    {SffField::PAGE_SELECT_BYTE, {SffPages::LOWER, 127, 1}},

    // Page 0 values, including vendor info:
    {SffField::EXTENDED_IDENTIFIER, {SffPages::PAGE0, 129, 1}},
    {SffField::ETHERNET_COMPLIANCE, {SffPages::PAGE0, 131, 1}},
    {SffField::EXTENDED_RATE_COMPLIANCE, {SffPages::PAGE0, 141, 1}},
    {SffField::LENGTH_SM_KM, {SffPages::PAGE0, 142, 1}},
    {SffField::LENGTH_OM3, {SffPages::PAGE0, 143, 1}},
    {SffField::LENGTH_OM2, {SffPages::PAGE0, 144, 1}},
    {SffField::LENGTH_OM1, {SffPages::PAGE0, 145, 1}},
    {SffField::LENGTH_COPPER, {SffPages::PAGE0, 146, 1}},
    {SffField::DEVICE_TECHNOLOGY, {SffPages::PAGE0, 147, 1}},
    {SffField::VENDOR_NAME, {SffPages::PAGE0, 148, 16}},
    {SffField::VENDOR_OUI, {SffPages::PAGE0, 165, 3}},
    {SffField::PART_NUMBER, {SffPages::PAGE0, 168, 16}},
    {SffField::REVISION_NUMBER, {SffPages::PAGE0, 184, 2}},
    {SffField::PAGE0_CSUM, {SffPages::PAGE0, 191, 1}},
    {SffField::EXTENDED_SPECIFICATION_COMPLIANCE, {SffPages::PAGE0, 192, 1}},
    {SffField::OPTIONS, {SffPages::PAGE0, 195, 1}},
    {SffField::VENDOR_SERIAL_NUMBER, {SffPages::PAGE0, 196, 16}},
    {SffField::MFG_DATE, {SffPages::PAGE0, 212, 8}},
    {SffField::DIAGNOSTIC_MONITORING_TYPE, {SffPages::PAGE0, 220, 1}},
    {SffField::ENHANCED_OPTIONS, {SffPages::PAGE0, 221, 1}},
    {SffField::PAGE0_EXTCSUM, {SffPages::PAGE0, 223, 1}},

    // These are custom fields FB gets cable vendors to populate.
    // TODO: add support for turning off these fields via a command-line flag
    {SffField::LENGTH_COPPER_DECIMETERS, {SffPages::PAGE0, 236, 1}},
    {SffField::DAC_GAUGE, {SffPages::PAGE0, 237, 1}},

    // Page 1 values, diagnostic capabilities
    {SffField::RX_CONTROL_SUPPORT, {SffPages::PAGE0, 194, 1}},
    {SffField::TX_CONTROL_SUPPORT, {SffPages::PAGE0, 195, 1}},

    // Page 3 values, including alarm and warning threshold values:
    {SffField::TEMPERATURE_THRESH, {SffPages::PAGE3, 128, 8}},
    {SffField::VCC_THRESH, {SffPages::PAGE3, 144, 8}},
    {SffField::RX_PWR_THRESH, {SffPages::PAGE3, 176, 8}},
    {SffField::TX_BIAS_THRESH, {SffPages::PAGE3, 184, 8}},
    {SffField::TX_PWR_THRESH, {SffPages::PAGE3, 192, 8}},
    {SffField::FR1_PRBS_SUPPORT, {SffPages::PAGE3, 231, 1}},
    {SffField::TX_EQUALIZATION, {SffPages::PAGE3, 234, 2}},
    {SffField::RX_EMPHASIS, {SffPages::PAGE3, 236, 2}},
    {SffField::RX_AMPLITUDE, {SffPages::PAGE3, 238, 2}},
    {SffField::SQUELCH_CONTROL, {SffPages::PAGE3, 240, 1}},
    {SffField::TXRX_OUTPUT_CONTROL, {SffPages::PAGE3, 241, 1}},

    // Page 128 values for Miniphoton diags
    {SffField::MINIPHOTON_LOOPBACK, {SffPages::PAGE128, 245, 1}},
};

static SffFieldMultiplier qsfpMultiplier = {
    {SffField::LENGTH_SM_KM, 1000},
    {SffField::LENGTH_OM3, 2},
    {SffField::LENGTH_OM2, 1},
    {SffField::LENGTH_OM1, 1},
    {SffField::LENGTH_COPPER, 1},
    {SffField::LENGTH_COPPER_DECIMETERS, 0.1},
};

constexpr uint8_t kPage0CsumRangeStart = 128;
constexpr uint8_t kPage0CsumRangeLength = 63;
constexpr uint8_t kPage0ExtCsumRangeStart = 192;
constexpr uint8_t kPage0ExtCsumRangeLength = 31;

struct checksumInfoStruct {
  uint8_t checksumRangeStartOffset;
  uint8_t checksumRangeStartLength;
  SffField checksumValOffset;
};

static std::map<SffPages, std::vector<checksumInfoStruct>> checksumInfoSff = {
    {
        SffPages::PAGE0,
        {
            {kPage0CsumRangeStart, kPage0CsumRangeLength, SffField::PAGE0_CSUM},
            {kPage0ExtCsumRangeStart,
             kPage0ExtCsumRangeLength,
             SffField::PAGE0_EXTCSUM},
        },
    },
};

static std::map<ExtendedSpecComplianceCode, MediaInterfaceCode>
    mediaInterfaceMapping = {
        {ExtendedSpecComplianceCode::CWDM4_100G,
         MediaInterfaceCode::CWDM4_100G},
        {ExtendedSpecComplianceCode::CR4_100G, MediaInterfaceCode::CR4_100G},
        {ExtendedSpecComplianceCode::FR1_100G, MediaInterfaceCode::FR1_100G},
        // If we need to support other 50G copper channel interface types, we'll
        // need to use byte 113 to determine the number of channels
        {ExtendedSpecComplianceCode::CR_50G_CHANNELS,
         MediaInterfaceCode::CR4_200G},
};

void getQsfpFieldAddress(
    SffField field,
    int& dataAddress,
    int& offset,
    int& length) {
  auto info =
      QsfpFieldInfo<SffField, SffPages>::getQsfpFieldAddress(qsfpFields, field);
  dataAddress = info.dataAddress;
  offset = info.offset;
  length = info.length;
}

SffModule::SffModule(
    std::set<std::string> portNames,
    TransceiverImpl* qsfpImpl,
    std::shared_ptr<const TransceiverConfig> cfg,
    std::string tcvrName)
    : QsfpModule(
          std::move(portNames),
          qsfpImpl,
          std::move(tcvrName),
          std::move(cfg)) {}

SffModule::~SffModule() {}

void SffModule::readSffField(
    SffField field,
    uint8_t* data,
    bool skipPageChange) {
  int dataLength, dataPage, dataOffset;
  getQsfpFieldAddress(field, dataPage, dataOffset, dataLength);
  readField(field, dataPage, dataOffset, dataLength, data, skipPageChange);
}

void SffModule::writeSffField(
    SffField field,
    uint8_t* data,
    bool skipPageChange) {
  int dataLength, dataPage, dataOffset;
  getQsfpFieldAddress(field, dataPage, dataOffset, dataLength);
  writeField(field, dataPage, dataOffset, dataLength, data, skipPageChange);
}

void SffModule::readField(
    SffField field,
    int dataPage,
    int dataOffset,
    int dataLength,
    uint8_t* data,
    bool skipPageChange) {
  if (static_cast<SffPages>(dataPage) != SffPages::LOWER && !flatMem_ &&
      !skipPageChange) {
    // Only change page when it's not a flatMem module (which don't allow
    // changing page) and when the skipPageChange argument is not true
    uint8_t page = static_cast<uint8_t>(dataPage);
    qsfpImpl_->writeTransceiver(
        {TransceiverAccessParameter::ADDR_QSFP, 127, sizeof(page)},
        &page,
        POST_I2C_WRITE_DELAY_US,
        CAST_TO_INT(SffField::PAGE_CHANGE));
  }
  qsfpImpl_->readTransceiver(
      {TransceiverAccessParameter::ADDR_QSFP, dataOffset, dataLength},
      data,
      CAST_TO_INT(field));
}

void SffModule::writeField(
    SffField field,
    int dataPage,
    int dataOffset,
    int dataLength,
    uint8_t* data,
    bool skipPageChange) {
  if (static_cast<SffPages>(dataPage) != SffPages::LOWER && !flatMem_ &&
      !skipPageChange) {
    // Only change page when it's not a flatMem module (which don't allow
    // changing page) and when the skipPageChange argument is not true
    uint8_t page = static_cast<uint8_t>(dataPage);
    qsfpImpl_->writeTransceiver(
        {TransceiverAccessParameter::ADDR_QSFP, 127, sizeof(page)},
        &page,
        POST_I2C_WRITE_DELAY_US,
        CAST_TO_INT(SffField::PAGE_CHANGE));
  }
  qsfpImpl_->writeTransceiver(
      {TransceiverAccessParameter::ADDR_QSFP, dataOffset, dataLength},
      data,
      POST_I2C_WRITE_DELAY_US,
      CAST_TO_INT(field));
}

FlagLevels SffModule::getQsfpSensorFlags(SffField fieldName) {
  int offset;
  int length;
  int dataAddress;

  /* Determine if QSFP is present */
  getQsfpFieldAddress(fieldName, dataAddress, offset, length);
  const uint8_t* data = getQsfpValuePtr(dataAddress, offset, length);
  return getQsfpFlags(data, 4);
}

double SffModule::getQsfpDACLength() const {
  auto base = getQsfpCableLength(SffField::LENGTH_COPPER);
  auto fractional = getQsfpCableLength(SffField::LENGTH_COPPER_DECIMETERS);
  if (fractional < 0) {
    // fractional value not populated, fall back to integer value
    return base;
  } else if (fractional >= 1) {
    // some vendors misunderstood and expressed the full length in terms of dm
    return fractional;
  } else {
    return base + fractional;
  }
}

int SffModule::getQsfpDACGauge() const {
  auto info = QsfpFieldInfo<SffField, SffPages>::getQsfpFieldAddress(
      qsfpFields, SffField::DAC_GAUGE);
  const uint8_t* val =
      getQsfpValuePtr(info.dataAddress, info.offset, info.length);
  // Guard against FF default value
  auto gauge = *val;
  if (gauge == EEPROM_DEFAULT) {
    return 0;
  } else if (gauge > MAX_GAUGE) {
    // HACK: We never use cables with more than 30 (in decimal) gauge
    // However, some vendors put in hex values. For example, some put
    // 0x28 to represent 28 gauge cable (why?!?), which we would
    // incorrectly interpret as 40 if using decimal. This converts
    // values > 30 to the hex value.
    return (gauge / HEX_BASE) * DECIMAL_BASE + gauge % HEX_BASE;
  } else {
    return gauge;
  }
}

GlobalSensors SffModule::getSensorInfo() {
  GlobalSensors info = GlobalSensors();
  info.temp()->value() =
      getQsfpSensor(SffField::TEMPERATURE, SffFieldInfo::getTemp);
  info.temp()->flags() = getQsfpSensorFlags(SffField::TEMPERATURE_ALARMS);

  // Special handling for CWDM4_100G optics
  auto extSpecCompliance = getExtendedSpecificationComplianceCode();
  if (extSpecCompliance &&
      *extSpecCompliance == ExtendedSpecComplianceCode::CWDM4_100G) {
    info.temp()->flags()->alarm()->high() =
        (*info.temp()->value() > FLAGS_cwdm4_100g_high_temp_alarm_threshold);
    info.temp()->flags()->warn()->high() =
        (*info.temp()->value() > FLAGS_cwdm4_100g_high_temp_warning_threshold);
  }

  info.vcc()->value() = getQsfpSensor(SffField::VCC, SffFieldInfo::getVcc);
  info.vcc()->flags() = getQsfpSensorFlags(SffField::VCC_ALARMS);
  return info;
}

Vendor SffModule::getVendorInfo() const {
  Vendor vendor = Vendor();
  *vendor.name() = getQsfpString(SffField::VENDOR_NAME);
  *vendor.oui() = getQsfpString(SffField::VENDOR_OUI);
  *vendor.partNumber() = getQsfpString(SffField::PART_NUMBER);
  *vendor.rev() = getQsfpString(SffField::REVISION_NUMBER);
  *vendor.serialNumber() = getQsfpString(SffField::VENDOR_SERIAL_NUMBER);
  *vendor.dateCode() = getQsfpString(SffField::MFG_DATE);
  return vendor;
}

Cable SffModule::getCableInfo() {
  Cable cable = Cable();
  cable.transmitterTech() = getQsfpTransmitterTechnology();
  cable.mediaTypeEncoding() = MediaTypeEncodings::OPTICAL_SMF;

  cable.singleMode() = getQsfpCableLength(SffField::LENGTH_SM_KM);
  if (cable.singleMode().value_or({}) == 0) {
    cable.singleMode().reset();
  }
  cable.om3() = getQsfpCableLength(SffField::LENGTH_OM3);
  if (cable.om3().value_or({}) == 0) {
    cable.om3().reset();
  }
  cable.om2() = getQsfpCableLength(SffField::LENGTH_OM2);
  if (cable.om2().value_or({}) == 0) {
    cable.om2().reset();
  }
  cable.om1() = getQsfpCableLength(SffField::LENGTH_OM1);
  if (cable.om1().value_or({}) == 0) {
    cable.om1().reset();
  }
  cable.copper() = getQsfpCableLength(SffField::LENGTH_COPPER);
  if (cable.copper().value_or({}) == 0) {
    cable.copper().reset();
  }
  if (!cable.copper()) {
    // length and gauge fields currently only supported for copper
    // TODO: migrate all cable types
    return cable;
  }

  // For now, we only have passive copper SFF cables.
  cable.mediaTypeEncoding() = MediaTypeEncodings::PASSIVE_CU;

  auto overrideDacCableInfo = getDACCableOverride();
  if (overrideDacCableInfo) {
    cable.length() = overrideDacCableInfo->first;
    cable.gauge() = overrideDacCableInfo->second;
  } else {
    cable.length() = getQsfpDACLength();
    cable.gauge() = getQsfpDACGauge();
  }
  if (cable.length().value_or({}) == 0) {
    cable.length().reset();
  }
  if (cable.gauge().value_or({}) == 0) {
    cable.gauge().reset();
  }
  return cable;
}

/*
 * Threhold values are stored just once;  they aren't per-channel,
 * so in all cases we simple assemble two-byte values and convert
 * them based on the type of the field.
 */
ThresholdLevels SffModule::getThresholdValues(
    SffField field,
    double (*conversion)(uint16_t value)) {
  int offset;
  int length;
  int dataAddress;
  ThresholdLevels thresh;

  CHECK(!flatMem_);

  getQsfpFieldAddress(field, dataAddress, offset, length);
  const uint8_t* data = getQsfpValuePtr(dataAddress, offset, length);

  CHECK_GE(length, 8);
  thresh.alarm()->high() = conversion(data[0] << 8 | data[1]);
  thresh.alarm()->low() = conversion(data[2] << 8 | data[3]);
  thresh.warn()->high() = conversion(data[4] << 8 | data[5]);
  thresh.warn()->low() = conversion(data[6] << 8 | data[7]);

  return thresh;
}

std::optional<AlarmThreshold> SffModule::getThresholdInfo() {
  if (flatMem_) {
    return {};
  }
  AlarmThreshold threshold = AlarmThreshold();
  threshold.temp() =
      getThresholdValues(SffField::TEMPERATURE_THRESH, SffFieldInfo::getTemp);
  threshold.vcc() =
      getThresholdValues(SffField::VCC_THRESH, SffFieldInfo::getVcc);
  threshold.rxPwr() =
      getThresholdValues(SffField::RX_PWR_THRESH, SffFieldInfo::getPwr);
  threshold.txBias() =
      getThresholdValues(SffField::TX_BIAS_THRESH, SffFieldInfo::getTxBias);
  threshold.txPwr() =
      getThresholdValues(SffField::TX_PWR_THRESH, SffFieldInfo::getPwr);
  return threshold;
}

uint8_t SffModule::getSettingsValue(SffField field, uint8_t mask) const {
  int offset;
  int length;
  int dataAddress;

  getQsfpFieldAddress(field, dataAddress, offset, length);
  const uint8_t* data = getQsfpValuePtr(dataAddress, offset, length);

  return data[0] & mask;
}

TransceiverModuleIdentifier SffModule::getIdentifier() {
  return (TransceiverModuleIdentifier)getSettingsValue(SffField::IDENTIFIER);
}

TransceiverSettings SffModule::getTransceiverSettingsInfo() {
  TransceiverSettings settings = TransceiverSettings();
  settings.cdrTx() = SffFieldInfo::getFeatureState(
      getSettingsValue(SffField::EXTENDED_IDENTIFIER, EXT_ID_CDR_TX_MASK),
      getSettingsValue(SffField::CDR_CONTROL, UPPER_BITS_MASK));
  settings.cdrRx() = SffFieldInfo::getFeatureState(
      getSettingsValue(SffField::EXTENDED_IDENTIFIER, EXT_ID_CDR_RX_MASK),
      getSettingsValue(SffField::CDR_CONTROL, LOWER_BITS_MASK));
  settings.powerMeasurement() = SffFieldInfo::getFeatureState(getSettingsValue(
      SffField::DIAGNOSTIC_MONITORING_TYPE, POWER_MEASUREMENT_MASK));
  settings.powerControl() = getPowerControlValue(true /* readFromCache */);
  settings.rateSelect() = getRateSelectValue();
  settings.rateSelectSetting() =
      getRateSelectSettingValue(*settings.rateSelect());

  settings.mediaInterface() = std::vector<MediaInterfaceId>(numMediaLanes());
  if (!getMediaInterfaceId(*(settings.mediaInterface()))) {
    settings.mediaInterface().reset();
  }

  settings.mediaLaneSettings() =
      std::vector<MediaLaneSettings>(numMediaLanes());
  settings.hostLaneSettings() = std::vector<HostLaneSettings>(numHostLanes());
  if (!flatMem_) {
    if (!getMediaLaneSettings(*(settings.mediaLaneSettings()))) {
      settings.mediaLaneSettings().reset();
    }
    if (!getHostLaneSettings(*(settings.hostLaneSettings()))) {
      settings.hostLaneSettings().reset();
    }
  }
  return settings;
}

std::vector<uint8_t> SffModule::configuredHostLanes(
    uint8_t hostStartLane) const {
  // MP3N uses lanes 0 and 1 for 2x25G mode
  if (currentConfiguredSpeed_ == cfg::PortSpeed::TWENTYFIVEG) {
    return {hostStartLane};
  }

  // Remaining profiles all start on lane 0

  if (hostStartLane != 0) {
    return {};
  }
  if (currentConfiguredSpeed_ == cfg::PortSpeed::FIFTYG) {
    return {0, 1};
  } else {
    return {0, 1, 2, 3};
  }
}

std::vector<uint8_t> SffModule::configuredMediaLanes(
    uint8_t hostStartLane) const {
  if (flatMem_) {
    return {};
  }

  auto ext_comp_code = getExtendedSpecificationComplianceCode();

  // MP3N uses 2x25G mode, on host/media lanes 0+1
  if (ext_comp_code &&
      *ext_comp_code == ExtendedSpecComplianceCode::CWDM4_100G &&
      currentConfiguredSpeed_ == cfg::PortSpeed::TWENTYFIVEG) {
    return {hostStartLane};
  }

  // Remaining profiles should always start on lane 0
  if (hostStartLane != 0) {
    return {};
  }

  if (ext_comp_code && *ext_comp_code == ExtendedSpecComplianceCode::FR1_100G) {
    return {0};
  }

  if (currentConfiguredSpeed_ == cfg::PortSpeed::FIFTYG) {
    return {0, 1};
  } else {
    return {0, 1, 2, 3};
  }
}

RateSelectSetting SffModule::getRateSelectSettingValue(RateSelectState state) {
  /* This refers to the optimised bit rate
   * The various values here are listed in the spec:
   * ftp://ftp.seagate.com/sff/SFF-8636.PDF - page 36
   */
  if (state != RateSelectState::EXTENDED_RATE_SELECT_V1 &&
      state != RateSelectState::EXTENDED_RATE_SELECT_V2) {
    return RateSelectSetting::UNSUPPORTED;
  }

  // Each byte has settings for 4 different channels
  // Currently we only support setting them all to the same value
  // We also expect rx and tx to have the same setting
  uint8_t rateRx = getSettingsValue(SffField::RATE_SELECT_RX);
  uint8_t rateTx = getSettingsValue(SffField::RATE_SELECT_TX);
  if (rateRx != rateTx) {
    QSFP_LOG(ERR, this) << fmt::format(
        "Unable to retrieve rate select setting: rx({:#x}) and tx({:#x}) are not equal",
        rateRx,
        rateTx);

    return RateSelectSetting::UNSUPPORTED;
  }

  int channelRate = rateRx & 0b11;
  if (state == RateSelectState::EXTENDED_RATE_SELECT_V2) {
    // Offset so that we can correctly index into the enum
    channelRate += 3;
  }
  return (RateSelectSetting)channelRate;
}

RateSelectState SffModule::getRateSelectValue() {
  /* Rate select can be in one of 3 states:
   * 1. Not supported
   * 2. Rate selection using extended rate select
   * 3. Rate selection with application select tables
   *
   * We currently only support 1 and 2
   * The spec: ftp://ftp.seagate.com/sff/SFF-8636.PDF has a thorough
   * discussion of this on page 36.
   */
  uint8_t enhancedOptions =
      getSettingsValue(SffField::ENHANCED_OPTIONS, ENH_OPT_RATE_SELECT_MASK);
  uint8_t options = getSettingsValue(SffField::OPTIONS, OPT_RATE_SELECT_MASK);

  if (!enhancedOptions && !options) {
    return RateSelectState::UNSUPPORTED;
  }

  uint8_t extendedRateCompliance =
      getSettingsValue(SffField::EXTENDED_RATE_COMPLIANCE);
  if (enhancedOptions == 0b10 && (extendedRateCompliance & 0b01)) {
    return RateSelectState::EXTENDED_RATE_SELECT_V1;
  } else if (enhancedOptions == 0b10 && (extendedRateCompliance & 0b10)) {
    return RateSelectState::EXTENDED_RATE_SELECT_V2;
  } else if (enhancedOptions == 0b01) {
    return RateSelectState::APPLICATION_RATE_SELECT;
  }

  return RateSelectState::UNSUPPORTED;
}

PowerControlState SffModule::getPowerControlValue(bool readFromCache) {
  uint8_t powerControl;
  if (readFromCache) {
    powerControl = getSettingsValue(
        SffField::POWER_CONTROL, uint8_t(PowerControl::POWER_CONTROL_MASK));
  } else {
    readSffField(SffField::POWER_CONTROL, &powerControl);
    powerControl &= uint8_t(PowerControl::POWER_CONTROL_MASK);
  }
  switch (static_cast<PowerControl>(powerControl)) {
    case PowerControl::POWER_SET_BY_HW:
      return PowerControlState::POWER_SET_BY_HW;
    case PowerControl::HIGH_POWER_OVERRIDE:
      return PowerControlState::HIGH_POWER_OVERRIDE;
    case PowerControl::POWER_OVERRIDE:
      return PowerControlState::POWER_OVERRIDE;
    default:
      return PowerControlState::POWER_LPMODE;
  }
}

/*
 * Iterate through the host lanes collecting appropriate data;
 */

bool SffModule::getSignalsPerHostLane(std::vector<HostLaneSignals>& signals) {
  assert(signals.size() == numHostLanes());

  auto txLos = getSettingsValue(SffField::LOS, UPPER_BITS_MASK) >> 4;
  auto txLol = getSettingsValue(SffField::LOL, UPPER_BITS_MASK) >> 4;
  auto txAdaptEqFault = getSettingsValue(SffField::FAULT, UPPER_BITS_MASK) >> 4;

  for (int lane = 0; lane < signals.size(); lane++) {
    auto laneMask = (1 << lane);
    signals[lane].lane() = lane;
    signals[lane].txLos() = txLos & laneMask;
    signals[lane].txLol() = txLol & laneMask;
    signals[lane].txAdaptEqFault() = txAdaptEqFault & laneMask;
  }

  return true;
}

/*
 * Iterate through the media channels collecting appropriate data;
 */
bool SffModule::getSignalsPerMediaLane(std::vector<MediaLaneSignals>& signals) {
  assert(signals.size() == numMediaLanes());

  auto rxLos = getSettingsValue(SffField::LOS, LOWER_BITS_MASK);
  auto rxLol = getSettingsValue(SffField::LOL, LOWER_BITS_MASK);
  auto txFault = getSettingsValue(SffField::FAULT, LOWER_BITS_MASK);

  for (int lane = 0; lane < signals.size(); lane++) {
    auto laneMask = (1 << lane);
    signals[lane].lane() = lane;
    signals[lane].rxLos() = rxLos & laneMask;
    signals[lane].rxLol() = rxLol & laneMask;
    signals[lane].txFault() = txFault & laneMask;
  }

  return true;
}

/*
 * Iterate through the four channels collecting appropriate data;
 */

bool SffModule::getSensorsPerChanInfo(std::vector<Channel>& channels) {
  int offset;
  int length;
  int dataAddress;
  auto channel_count = numMediaLanes();

  /*
   * Interestingly enough, the QSFP stores the four alarm flags
   * (alarm high, alarm low, warning high, warning low) in two bytes by
   * channel in order 2, 1, 4, 3;  by using this set of offsets, we
   * should be able to read them in order, by reading the appriopriate
   * bit offsets combined with a byte offset into the data.
   *
   * That is, read bits 4 through 7 of the first byte, then 0 through 3,
   * then 4 through 7 of the second byte, and so on.  Ugh.
   */
  std::array<uint8_t, 4> bitOffset{{4, 0, 4, 0}};
  std::array<uint8_t, 4> byteOffset{{0, 0, 1, 1}};

  assert(channels.size() == channel_count);

  getQsfpFieldAddress(
      SffField::CHANNEL_RX_PWR_ALARMS, dataAddress, offset, length);
  const uint8_t* data = getQsfpValuePtr(dataAddress, offset, length);

  for (int channel = 0; channel < channel_count; channel++) {
    channels[channel].sensors()->rxPwr()->flags() =
        getQsfpFlags(data + byteOffset[channel], bitOffset[channel]);
  }

  getQsfpFieldAddress(
      SffField::CHANNEL_TX_BIAS_ALARMS, dataAddress, offset, length);
  data = getQsfpValuePtr(dataAddress, offset, length);

  for (int channel = 0; channel < channel_count; channel++) {
    channels[channel].sensors()->txBias()->flags() =
        getQsfpFlags(data + byteOffset[channel], bitOffset[channel]);
  }

  getQsfpFieldAddress(
      SffField::CHANNEL_TX_PWR_ALARMS, dataAddress, offset, length);
  data = getQsfpValuePtr(dataAddress, offset, length);

  for (int channel = 0; channel < channel_count; channel++) {
    channels[channel].sensors()->txPwr()->flags() =
        getQsfpFlags(data + byteOffset[channel], bitOffset[channel]);
  }

  getQsfpFieldAddress(SffField::CHANNEL_RX_PWR, dataAddress, offset, length);
  data = getQsfpValuePtr(dataAddress, offset, length);

  for (auto& channel : channels) {
    uint16_t value = data[0] << 8 | data[1];
    auto pwr = SffFieldInfo::getPwr(value);
    channel.sensors()->rxPwr()->value() = pwr;
    Sensor rxDbm;
    rxDbm.value() = mwToDb(pwr);
    channel.sensors()->rxPwrdBm() = rxDbm;
    data += 2;
    length--;
  }
  CHECK_GE(length, 0);

  getQsfpFieldAddress(SffField::CHANNEL_TX_BIAS, dataAddress, offset, length);
  data = getQsfpValuePtr(dataAddress, offset, length);
  for (auto& channel : channels) {
    uint16_t value = data[0] << 8 | data[1];
    channel.sensors()->txBias()->value() = SffFieldInfo::getTxBias(value);
    data += 2;
    length--;
  }
  CHECK_GE(length, 0);

  getQsfpFieldAddress(SffField::CHANNEL_TX_PWR, dataAddress, offset, length);
  data = getQsfpValuePtr(dataAddress, offset, length);

  for (auto& channel : channels) {
    uint16_t value = data[0] << 8 | data[1];
    auto pwr = SffFieldInfo::getPwr(value);
    channel.sensors()->txPwr()->value() = pwr;
    Sensor txDbm;
    txDbm.value() = mwToDb(pwr);
    channel.sensors()->txPwrdBm() = txDbm;
    data += 2;
    length--;
  }
  CHECK_GE(length, 0);

  return true;
}

std::string SffModule::getQsfpString(SffField field) const {
  int offset;
  int length;
  int dataAddress;

  getQsfpFieldAddress(field, dataAddress, offset, length);
  const uint8_t* data = getQsfpValuePtr(dataAddress, offset, length);

  while (length > 0 && data[length - 1] == ' ') {
    --length;
  }

  std::string value(reinterpret_cast<const char*>(data), length);
  return validateQsfpString(value) ? value : "UNKNOWN";
}

double SffModule::getQsfpSensor(
    SffField field,
    double (*conversion)(uint16_t value)) {
  auto info =
      QsfpFieldInfo<SffField, SffPages>::getQsfpFieldAddress(qsfpFields, field);
  const uint8_t* data =
      getQsfpValuePtr(info.dataAddress, info.offset, info.length);
  return conversion(data[0] << 8 | data[1]);
}

/*
 * Cable length is report as a single byte;  each field has a
 * specific multiplier to use to get the true length.  For instance,
 * single mode fiber length is specified in km, so the multiplier
 * is 1000.  In addition, the raw value of 255 indicates that the
 * cable is longer than can be represented.  We use a negative
 * value of the appropriate magnitude to communicate that to thrift
 * clients.
 */
double SffModule::getQsfpCableLength(SffField field) const {
  double length;
  auto info =
      QsfpFieldInfo<SffField, SffPages>::getQsfpFieldAddress(qsfpFields, field);
  const uint8_t* data =
      getQsfpValuePtr(info.dataAddress, info.offset, info.length);
  auto multiplier = qsfpMultiplier.at(field);
  length = *data * multiplier;
  if (*data == EEPROM_DEFAULT) {
    // TODO: does this really mean the cable is too long?
    length = -(EEPROM_DEFAULT - 1) * multiplier;
  }
  return length;
}

TransmitterTechnology SffModule::getQsfpTransmitterTechnology() const {
  auto info = QsfpFieldInfo<SffField, SffPages>::getQsfpFieldAddress(
      qsfpFields, SffField::DEVICE_TECHNOLOGY);
  const uint8_t* data =
      getQsfpValuePtr(info.dataAddress, info.offset, info.length);

  uint8_t transTech = *data >> DeviceTechnologySff::TRANSMITTER_TECH_SHIFT;
  if (transTech == DeviceTechnologySff::UNKNOWN_VALUE_SFF) {
    return TransmitterTechnology::UNKNOWN;
  } else if (transTech <= DeviceTechnologySff::OPTICAL_MAX_VALUE_SFF) {
    return TransmitterTechnology::OPTICAL;
  } else {
    return TransmitterTechnology::COPPER;
  }
}

SignalFlags SffModule::getSignalFlagInfo() {
  SignalFlags signalFlags = SignalFlags();

  signalFlags.txLos() = getSettingsValue(SffField::LOS, UPPER_BITS_MASK);
  *signalFlags.txLos() >>= 4;
  signalFlags.rxLos() = getSettingsValue(SffField::LOS, LOWER_BITS_MASK);
  signalFlags.txLol() = getSettingsValue(SffField::LOL, UPPER_BITS_MASK);
  *signalFlags.txLol() >>= 4;
  signalFlags.rxLol() = getSettingsValue(SffField::LOL, LOWER_BITS_MASK);

  return signalFlags;
}

std::optional<ExtendedSpecComplianceCode>
SffModule::getExtendedSpecificationComplianceCode() const {
  return (ExtendedSpecComplianceCode)getSettingsValue(
      SffField::EXTENDED_SPECIFICATION_COMPLIANCE);
}

bool SffModule::getMediaInterfaceId(
    std::vector<MediaInterfaceId>& mediaInterface) {
  assert(mediaInterface.size() == numMediaLanes());

  // Currently setting the same media interface for all media lanes
  auto extSpecCompliance = getExtendedSpecificationComplianceCode();
  if (!extSpecCompliance) {
    QSFP_LOG(ERR, this)
        << "getExtendedSpecificationComplianceCode returned nullopt";
    return false;
  }
  for (int lane = 0; lane < mediaInterface.size(); lane++) {
    mediaInterface[lane].lane() = lane;
    MediaInterfaceUnion media;
    media.extendedSpecificationComplianceCode() = *extSpecCompliance;
    if (auto it = mediaInterfaceMapping.find(*extSpecCompliance);
        it != mediaInterfaceMapping.end()) {
      mediaInterface[lane].code() = it->second;
    } else {
      QSFP_LOG(ERR, this) << "Unable to find MediaInterfaceCode for "
                          << apache::thrift::util::enumNameSafe(
                                 *extSpecCompliance);
      mediaInterface[lane].code() = MediaInterfaceCode::UNKNOWN;
    }
    mediaInterface[lane].media() = media;
  }

  return true;
}

MediaInterfaceCode SffModule::getModuleMediaInterface() const {
  auto extSpecCompliance = getExtendedSpecificationComplianceCode();
  if (!extSpecCompliance) {
    QSFP_LOG(ERR, this)
        << "getExtendedSpecificationComplianceCode returned nullopt";
    return MediaInterfaceCode::UNKNOWN;
  }
  if (auto it = mediaInterfaceMapping.find(*extSpecCompliance);
      it != mediaInterfaceMapping.end()) {
    return it->second;
  }
  QSFP_LOG(ERR, this) << "Cannot find "
                      << apache::thrift::util::enumNameSafe(*extSpecCompliance)
                      << " in mediaInterfaceMapping";
  return MediaInterfaceCode::UNKNOWN;
}

ModuleStatus SffModule::getModuleStatus() {
  ModuleStatus moduleStatus;
  std::array<uint8_t, 2> status = {{0, 0}};
  int offset;
  int length;
  int dataAddress;

  getQsfpFieldAddress(SffField::STATUS, dataAddress, offset, length);
  getQsfpValue(dataAddress, offset, length, status.data());
  moduleStatus.dataNotReady() = status[1] & (1 << 0);
  moduleStatus.interruptL() = status[1] & (1 << 1);

  return moduleStatus;
}

void SffModule::setQsfpFlatMem() {
  std::array<uint8_t, 2> status = {{0, 0}};
  int offset;
  int length;
  int dataAddress;

  if (!present_) {
    throw FbossError("Failed setting QSFP Flat Mem: QSFP is not present");
  }

  // Check if the data is ready
  getQsfpFieldAddress(SffField::STATUS, dataAddress, offset, length);
  getQsfpValue(dataAddress, offset, length, status.data());
  if (status[1] & (1 << 0)) {
    StatsPublisher::bumpModuleErrors();
    throw QsfpModuleError("Failed setting QSFP Flat Mem: QSFP is not ready");
  }
  flatMem_ = status[1] & (1 << 2);
  QSFP_LOG(DBG3, this) << "Detected flatMem=" << flatMem_;
}

const uint8_t*
SffModule::getQsfpValuePtr(int dataAddress, int offset, int length) const {
  /* if the cached values are not correct */
  if (!cacheIsValid()) {
    throw FbossError("Qsfp is either not present or the data is not read");
  }
  if (dataAddress == static_cast<int>(SffPages::LOWER)) {
    CHECK_LE(offset + length, sizeof(lowerPage_));
    /* Copy data from the cache */
    return (lowerPage_ + offset);
  } else {
    offset -= MAX_QSFP_PAGE_SIZE;
    CHECK_GE(offset, 0);
    CHECK_LE(offset, MAX_QSFP_PAGE_SIZE);
    if (dataAddress == static_cast<int>(SffPages::PAGE0)) {
      CHECK_LE(offset + length, sizeof(page0_));
      /* Copy data from the cache */
      return (page0_ + offset);
    } else if (dataAddress == static_cast<int>(SffPages::PAGE3) && !flatMem_) {
      CHECK_LE(offset + length, sizeof(page3_));
      /* Copy data from the cache */
      return (page3_ + offset);
    } else {
      throw FbossError("Invalid Data Address 0x%d", dataAddress);
    }
  }
}

RawDOMData SffModule::getRawDOMData() {
  lock_guard<std::mutex> g(qsfpModuleMutex_);
  RawDOMData data;
  if (present_) {
    *data.lower() = IOBuf::wrapBufferAsValue(lowerPage_, MAX_QSFP_PAGE_SIZE);
    *data.page0() = IOBuf::wrapBufferAsValue(page0_, MAX_QSFP_PAGE_SIZE);
    if (!flatMem_) {
      data.page3() = IOBuf::wrapBufferAsValue(page3_, MAX_QSFP_PAGE_SIZE);
    }
  }
  return data;
}

DOMDataUnion SffModule::getDOMDataUnion() {
  lock_guard<std::mutex> g(qsfpModuleMutex_);
  Sff8636Data sffData;
  if (present_) {
    *sffData.lower() = IOBuf::wrapBufferAsValue(lowerPage_, MAX_QSFP_PAGE_SIZE);
    *sffData.page0() = IOBuf::wrapBufferAsValue(page0_, MAX_QSFP_PAGE_SIZE);
    if (!flatMem_) {
      sffData.page3() = IOBuf::wrapBufferAsValue(page3_, MAX_QSFP_PAGE_SIZE);
    }
  }
  sffData.timeCollected() = lastQsfpDataUpdateTime_;
  DOMDataUnion data;
  data.sff8636() = sffData;
  return data;
}

void SffModule::getFieldValue(SffField fieldName, uint8_t* fieldValue) {
  lock_guard<std::mutex> g(qsfpModuleMutex_);
  int offset;
  int length;
  int dataAddress;
  getQsfpFieldAddress(fieldName, dataAddress, offset, length);
  getQsfpValue(dataAddress, offset, length, fieldValue);
}

void SffModule::updateQsfpData(bool allPages) {
  // expects the lock to be held
  if (!present_) {
    return;
  }
  try {
    QSFP_LOG(DBG2, this) << "Performing " << ((allPages) ? "full" : "partial")
                         << " qsfp data cache refresh";
    readSffField(SffField::PAGE_LOWER, lowerPage_);
    lastQsfpDataUpdateTime_ = std::time(nullptr);
    dirty_ = false;
    setQsfpFlatMem();

    if (!allPages) {
      // Only the first page has fields that change often so provide
      // an option to only fetch that page. Also the write path is
      // particularly slow due to using an i2c bus, so writing the
      // bytes needed to select later pages on non-flat memories can
      // be quite expensive.
      return;
    }

    readSffField(SffField::PAGE_UPPER0, page0_);
    if (!flatMem_) {
      readSffField(SffField::PAGE_UPPER3, page3_);
    }
  } catch (const std::exception& ex) {
    // No matter what kind of exception throws, we need to set the dirty_ flag
    // to true.
    dirty_ = true;
    QSFP_LOG(ERR, this) << "Error update data: " << ex.what();
    throw;
  }
}

void SffModule::clearTransceiverPrbsStats(
    const std::string& portName,
    phy::Side side) {
  // We are asked to clear the prbs stats, therefore reset the bit count
  // reference points so that the BER calculations get reset too.

  // Since we need to read/write from the module here, need to acquire the
  // mutex and run this in i2c evb
  auto clearTransceiverPrbsStatsLambda = [side, this]() {
    lock_guard<std::mutex> g(qsfpModuleMutex_);
    if (side == Side::SYSTEM) {
      auto systemPrbs = systemPrbsSnapshot_.wlock();
      for (auto laneId = 0; laneId < systemPrbs->bitErrorCount.size();
           laneId++) {
        systemPrbs->bitErrorCount[laneId] =
            getPrbsBitErrorCountLocked(side, laneId);
        systemPrbs->totalBitCount[laneId] =
            getPrbsTotalBitCountLocked(side, laneId);
      }
    } else {
      auto linePrbs = linePrbsSnapshot_.wlock();
      for (auto laneId = 0; laneId < linePrbs->bitErrorCount.size(); laneId++) {
        linePrbs->bitErrorCount[laneId] =
            getPrbsBitErrorCountLocked(side, laneId);
        linePrbs->totalBitCount[laneId] =
            getPrbsTotalBitCountLocked(side, laneId);
      }
    }
  };
  auto i2cEvb = qsfpImpl_->getI2cEventBase();
  if (!i2cEvb) {
    // Certain platforms cannot execute multiple I2C transactions in parallel
    // and therefore don't have an I2C evb thread
    clearTransceiverPrbsStatsLambda();
  } else {
    via(i2cEvb)
        .thenValue([clearTransceiverPrbsStatsLambda](auto&&) mutable {
          clearTransceiverPrbsStatsLambda();
        })
        .get();
  }

  // Call the base class implementation to clear the common stats
  QsfpModule::clearTransceiverPrbsStats(portName, side);
}

phy::PrbsStats SffModule::getPortPrbsStatsSideLocked(
    phy::Side side,
    bool checkerEnabled,
    const phy::PrbsStats& lastStats) {
  if (!checkerEnabled || !lastStats.laneStats()->size()) {
    // If the checker is not enabled or the stats are uninitialized, return the
    // default PrbsStats object with some of the parameters initialized
    phy::PrbsStats stats;
    stats.portId() = getID();
    stats.component() = side == Side::SYSTEM
        ? phy::PortComponent::TRANSCEIVER_SYSTEM
        : phy::PortComponent::TRANSCEIVER_LINE;
    int lanes = side == Side::SYSTEM ? numHostLanes() : numMediaLanes();
    for (int lane = 0; lane < lanes; lane++) {
      phy::PrbsLaneStats laneStat;
      laneStat.laneId() = lane;
      stats.laneStats()->push_back(laneStat);
    }
    return stats;
  }
  // Initialize the counter snapshots if they are not already
  auto hostLanes = numHostLanes();
  auto mediaLanes = numMediaLanes();
  if (side == Side::SYSTEM) {
    auto systemPrbsSnapshot = systemPrbsSnapshot_.wlock();
    if (systemPrbsSnapshot->bitErrorCount.size() <= hostLanes) {
      // Assume if one field is uninitialized, the other is also
      PrbsBitCount snapshot;
      snapshot.bitErrorCount = std::vector<long long>(hostLanes, 0);
      snapshot.totalBitCount = std::vector<long long>(hostLanes, 0);
      *systemPrbsSnapshot = snapshot;
    }
  } else {
    auto linePrbsSnapshot = linePrbsSnapshot_.wlock();
    if (linePrbsSnapshot->bitErrorCount.size() <= mediaLanes) {
      // Assume if one field is uninitialized, the other is also
      PrbsBitCount snapshot;
      snapshot.bitErrorCount = std::vector<long long>(mediaLanes, 0);
      snapshot.totalBitCount = std::vector<long long>(mediaLanes, 0);
      *linePrbsSnapshot = snapshot;
    }
  }

  if (auto prbsStats = getPortPrbsStatsOverrideLocked(side)) {
    return *prbsStats;
  }
  return phy::PrbsStats();
}

// Returns the total prbs bit count
// This function expects caller to hold the qsfp module level lock
long long SffModule::getPrbsTotalBitCountLocked(Side side, uint8_t lane) {
  if (auto bitCount = getPrbsTotalBitCountOverrideLocked(side, lane)) {
    return *bitCount;
  }
  return -1;
}

// Returns the total prbs bit error count
// This function expects caller to hold the qsfp module level lock
long long SffModule::getPrbsBitErrorCountLocked(Side side, uint8_t lane) {
  if (auto errCount = getPrbsBitErrorCountOverrideLocked(side, lane)) {
    return *errCount;
  }
  return -1;
}

// Returns the prbs lock status of lanes on the given side
// This function expects caller to hold the qsfp module level lock
int SffModule::getPrbsLockStatusLocked(Side side) {
  if (auto lockStatus = getPrbsLockStatusOverrideLocked(side)) {
    return *lockStatus;
  }
  return 0;
}

void SffModule::setCdrIfSupported(
    cfg::PortSpeed speed,
    FeatureState currentStateTx,
    FeatureState currentStateRx) {
  /*
   * Note that this function expects to be called with qsfpModuleMutex_
   * held.
   */
  QSFP_LOG(INFO, this) << "Checking if we need to change CDR";

  if (currentStateTx == FeatureState::UNSUPPORTED &&
      currentStateRx == FeatureState::UNSUPPORTED) {
    QSFP_LOG(INFO, this) << "CDR is not supported";
    return;
  }

  // If only one of Rx or Tx is supported, it doesn't matter what
  // we set the value to, so in that case, treat is as if
  // no change is needed
  auto toChange = [speed](FeatureState state) {
    return state != FeatureState::UNSUPPORTED &&
        ((cdrSupportedSpeed(speed) && state != FeatureState::ENABLED) ||
         (!cdrSupportedSpeed(speed) && state != FeatureState::DISABLED));
  };

  bool changeRx = toChange(currentStateRx);
  bool changeTx = toChange(currentStateTx);
  if (!changeRx && !changeTx) {
    QSFP_LOG(INFO, this) << "Not changing CDR setting, already correctly set";
    return;
  }

  // If one of rx and tx need a change, set the whole byte - whichever
  // isn't supported will be ignored anyway
  FeatureState newState = FeatureState::DISABLED;
  uint8_t value = 0x0;
  if (cdrSupportedSpeed(speed)) {
    value = 0xFF;
    newState = FeatureState::ENABLED;
  }
  writeSffField(SffField::CDR_CONTROL, &value);
  QSFP_LOG(INFO, this) << "Setting CDR to state: "
                       << apache::thrift::util::enumNameSafe(newState);
}

void SffModule::setRateSelectIfSupported(
    cfg::PortSpeed speed,
    RateSelectState currentState,
    RateSelectSetting currentSetting) {
  if (currentState == RateSelectState::UNSUPPORTED) {
    return;
  } else if (currentState == RateSelectState::APPLICATION_RATE_SELECT) {
    // Currently only support extended rate select, so treat application
    // rate select as an invalid option
    throw FbossError(
        folly::to<std::string>(
            "Port: ",
            qsfpImpl_->getName(),
            " Rate select in unknown state, treating as unsupported: ",
            apache::thrift::util::enumNameSafe(currentState)));
  }

  uint8_t value;
  RateSelectSetting newSetting;
  bool alreadySet = false;
  auto translateEnum = [currentSetting, &value, &newSetting](
                           RateSelectSetting desired, uint8_t newValue) {
    if (currentSetting == desired) {
      return true;
    }
    newSetting = desired;
    value = newValue;
    return false;
  };

  if (currentState == RateSelectState::EXTENDED_RATE_SELECT_V1) {
    // Use the highest possible speed in this version
    alreadySet =
        translateEnum(RateSelectSetting::FROM_6_6GB_AND_ABOVE, 0b10101010);
  } else if (speed == cfg::PortSpeed::FORTYG) {
    // Optimised for 10G channels
    alreadySet = translateEnum(RateSelectSetting::LESS_THAN_12GB, 0b00000000);
  } else if (cdrSupportedSpeed(speed)) {
    // Optimised for 25GB channels
    alreadySet =
        translateEnum(RateSelectSetting::FROM_24GB_to_26GB, 0b10101010);
  } else {
    throw FbossError(
        folly::to<std::string>(
            "Port: ",
            qsfpImpl_->getName(),
            " Unable to set rate select for port speed: ",
            apache::thrift::util::enumNameSafe(speed)));
  }

  if (alreadySet) {
    return;
  }

  writeSffField(SffField::RATE_SELECT_RX, &value);
  writeSffField(SffField::RATE_SELECT_RX, &value);
  QSFP_LOG(INFO, this) << "Set rate select to "
                       << apache::thrift::util::enumNameSafe(newSetting);
}

void SffModule::setPowerOverrideIfSupportedLocked(
    PowerControlState currentState) {
  /* Wedge forces Low Power mode via a pin;  we have to reset this
   * to force High Power mode on all transceivers except SR4-40G.
   *
   * Note that this function expects to be called with qsfpModuleMutex_
   * held.
   */

  int offset;
  int length;
  int dataAddress;
  getQsfpFieldAddress(
      SffField::ETHERNET_COMPLIANCE, dataAddress, offset, length);
  const uint8_t* ether = getQsfpValuePtr(dataAddress, offset, length);

  getQsfpFieldAddress(
      SffField::EXTENDED_IDENTIFIER, dataAddress, offset, length);
  const uint8_t* extId = getQsfpValuePtr(dataAddress, offset, length);

  auto desiredSetting = PowerControlState::POWER_OVERRIDE;

  // SR4-40G is represented by a value of 2 - SFF-8636
  // This is the only transceiver that should use LP mode
  if (*ether == EthernetCompliance::SR4_40GBASE) {
    desiredSetting = PowerControlState::POWER_LPMODE;
  } else {
    uint8_t highPowerLevel = (*extId & EXT_ID_HI_POWER_MASK);

    if (highPowerLevel > 0) {
      desiredSetting = PowerControlState::HIGH_POWER_OVERRIDE;
    }
  }

  QSFP_LOG(DBG1, this) << "Power control "
                       << apache::thrift::util::enumNameSafe(currentState)
                       << " Ext ID " << std::hex << static_cast<int>(*extId)
                       << " Ethernet compliance " << std::hex
                       << static_cast<int>(*ether) << " Desired power control "
                       << apache::thrift::util::enumNameSafe(desiredSetting);

  if (currentState == desiredSetting) {
    QSFP_LOG(INFO, this)
        << "Power override already correctly set, doing nothing";
    return;
  }

  uint8_t power = uint8_t(PowerControl::POWER_OVERRIDE);
  if (desiredSetting == PowerControlState::HIGH_POWER_OVERRIDE) {
    power = uint8_t(PowerControl::HIGH_POWER_OVERRIDE);
  } else if (desiredSetting == PowerControlState::POWER_LPMODE) {
    power = uint8_t(PowerControl::POWER_LPMODE);
  }

  // enable target power class
  writeSffField(SffField::POWER_CONTROL, &power);

  QSFP_LOG(INFO, this) << "QSFP set to power setting "
                       << apache::thrift::util::enumNameSafe(desiredSetting)
                       << " (" << power << ")";
}

/*
 * Put logic here that should only be run on ports that have been
 * down for a long time. These are actions that are potentially more
 * disruptive, but have worked in the past to recover a transceiver.
 */
void SffModule::remediateFlakyTransceiver(
    bool /* allPortsDown */,
    const std::vector<std::string>& /* ports */) {
  QSFP_LOG(INFO, this) << "Performing potentially disruptive remediations";

  resetLowPowerMode();
  ensureTxEnabled();
  lastRemediateTime_ = std::time(nullptr);
}
void SffModule::resetLowPowerMode() {
  // Newer transceivers will have a auto-clearing reset bit that is
  // probably better to use than this.

  int offset, length, dataAddress;
  getQsfpFieldAddress(SffField::POWER_CONTROL, dataAddress, offset, length);

  uint8_t oldPower;
  getQsfpValue(dataAddress, offset, length, &oldPower);

  uint8_t lowPower = uint8_t(PowerControl::POWER_LPMODE);

  if (oldPower != lowPower) {
    // first set to low power
    writeSffField(SffField::POWER_CONTROL, &lowPower);

    // Transceivers need a bit of time to handle the low power setting
    // we just sent. We should be able to use the status register to be
    // smarter about this, but just sleeping 0.1s for now.
    usleep(kUsecBetweenPowerModeFlap);
  }

  // set back to previous value
  writeSffField(SffField::POWER_CONTROL, &oldPower);
}

void SffModule::ensureTxEnabled() {
  // Sometimes transceivers lock up and disable TX. When we customize
  // the transceiver let's also ensure that tx is enabled. We have
  // even seen transceivers report to have tx enabled in the DOM, but
  // no traffic was flowing. When we forcibly set the tx_enable bits
  // again, traffic began flowing.  Because of this, we ALWAYS set the
  // bits (even if they report enabled).
  //
  // This is likely a very rare occurence, so we make sure we only write
  // to the transceiver at most every cooldown seconds.

  // Force enable
  std::array<uint8_t, 1> buf = {{0}};
  writeSffField(SffField::TX_DISABLE, buf.data());
}

bool SffModule::getMediaLaneSettings(
    std::vector<MediaLaneSettings>& laneSettings) {
  assert(laneSettings.size() == numMediaLanes());
  auto txSquelch = getSettingsValue(SffField::SQUELCH_CONTROL, LOWER_BITS_MASK);
  auto txAdaptEq =
      getSettingsValue(SffField::TXRX_OUTPUT_CONTROL, LOWER_BITS_MASK);
  auto txDisable = getSettingsValue(SffField::TX_DISABLE);

  for (int lane = 0; lane < laneSettings.size(); lane++) {
    auto laneMask = (1 << lane);
    laneSettings[lane].lane() = lane;
    laneSettings[lane].txSquelch() = txSquelch & laneMask;
    laneSettings[lane].txAdaptiveEqControl() = txAdaptEq & laneMask;
    laneSettings[lane].txDisable() = txDisable & laneMask;
  }
  return true;
}

bool SffModule::getHostLaneSettings(
    std::vector<HostLaneSettings>& laneSettings) {
  assert(laneSettings.size() == numHostLanes());
  int offset;
  int length;
  int dataAddress;

  std::array<uint8_t, 2> txEq = {{0, 0}};
  getQsfpFieldAddress(SffField::TX_EQUALIZATION, dataAddress, offset, length);
  getQsfpValue(dataAddress, offset, length, txEq.data());

  std::array<uint8_t, 2> rxEmp = {{0, 0}};
  getQsfpFieldAddress(SffField::RX_EMPHASIS, dataAddress, offset, length);
  getQsfpValue(dataAddress, offset, length, rxEmp.data());

  std::array<uint8_t, 2> rxAmp = {{0, 0}};
  getQsfpFieldAddress(SffField::RX_AMPLITUDE, dataAddress, offset, length);
  getQsfpValue(dataAddress, offset, length, rxAmp.data());

  auto rxSquelch = getSettingsValue(SffField::SQUELCH_CONTROL) >> 4;
  auto rxOutput = getSettingsValue(SffField::TXRX_OUTPUT_CONTROL) >> 4;

  for (int lane = 0; lane < laneSettings.size(); lane++) {
    bool evenLane = (lane % 2 == 0);
    auto laneMask = (1 << lane);
    laneSettings[lane].lane() = lane;
    laneSettings[lane].txInputEqualization() =
        evenLane ? txEq[lane / 2] >> 4 : txEq[lane / 2] & 0xf;
    laneSettings[lane].rxOutputEmphasis() =
        evenLane ? rxEmp[lane / 2] >> 4 : rxEmp[lane / 2] & 0xf;
    laneSettings[lane].rxOutputAmplitude() =
        evenLane ? rxAmp[lane / 2] >> 4 : rxAmp[lane / 2] & 0xf;
    laneSettings[lane].rxSquelch() = rxSquelch & laneMask;
    laneSettings[lane].rxOutput() = rxOutput & laneMask;
  }
  return true;
}

void SffModule::overwriteChannelControlSettings() {
  // Most transceiver we use have their RX emphasis control set to 0.
  // However modules from AOI set those as 2dB which causes lower
  // signal quality when working with credo xphy on yamp. Thus as part
  // of the redmediation, we set that value to 0.
  auto cachedInfo = info_.rlock();

  if (!cachedInfo->has_value()) {
    return;
  }

  auto getSettingForAllLanes =
      [](uint8_t oneLaneValue) -> std::array<uint8_t, 2> {
    std::array<uint8_t, 2> buf = {{0}};
    // Example: If oneLaneValue = 2, we need to return {0x22, 0x22} for all 4
    // lanes.
    buf.fill(((oneLaneValue & 0xF) << 4) | (oneLaneValue & 0xF));
    return buf;
  };

  bool settingsOverwritten = false;
  auto updateIfChanged = [this, &settingsOverwritten, getSettingForAllLanes](
                             auto field, auto settingOverride, auto& name) {
    if (!settingOverride) {
      return;
    }

    std::array<uint8_t, 2> currSettings;
    readSffField(field, currSettings.data());
    auto settingValue = getSettingForAllLanes(*settingOverride);
    if (currSettings == settingValue) {
      return;
    }
    QSFP_LOG(INFO, this) << "Programming new " << name << " settings. Old: [0x"
                         << std::hex << static_cast<int>(currSettings[0])
                         << ", 0x" << static_cast<int>(currSettings[1])
                         << "], New: [0x" << static_cast<int>(settingValue[0])
                         << ", 0x" << static_cast<int>(settingValue[1]) << "]";
    writeSffField(field, settingValue.data());
    settingsOverwritten = true;
  };
  // Set the Equalizer setting based on QSFP config.
  for (const auto& override : tcvrConfig_->overridesConfig_) {
    updateIfChanged(
        SffField::TX_EQUALIZATION,
        sffTxEqualizationOverride(*override.config()),
        "TX EQ");

    updateIfChanged(
        SffField::RX_EMPHASIS,
        sffRxPreemphasisOverride(*override.config()),
        "RX Pre-emphasis");

    updateIfChanged(
        SffField::RX_AMPLITUDE,
        sffRxAmplitudeOverride(*override.config()),
        "RX Amplitude");
  }

  if (settingsOverwritten) {
    // Bump up the ODS counter.
    StatsPublisher::bumpAOIOverride();
  }
}

bool SffModule::tcvrPortStateSupported(TransceiverPortState& portState) const {
  lock_guard<std::mutex> g(qsfpModuleMutex_);
  if (portState.transmitterTech != getQsfpTransmitterTechnology()) {
    return false;
  }

  if (getQsfpTransmitterTechnology() == TransmitterTechnology::COPPER) {
    // Return true irrespective of speed as the copper cables are mostly
    // flexible with all speeds. We can change this later when we know of any
    // limitations
    return true;
  }

  return (portState.speed == cfg::PortSpeed::TWENTYFIVEG &&
          (portState.startHostLane == 0 || portState.startHostLane == 1) &&
          portState.numHostLanes == 1) ||
      (portState.speed == cfg::PortSpeed::FIFTYG &&
       portState.startHostLane == 0 && portState.numHostLanes == 2) ||
      ((portState.speed == cfg::PortSpeed::HUNDREDG ||
        portState.speed == cfg::PortSpeed::FORTYG) &&
       (portState.startHostLane == 0) && portState.numHostLanes == 4);
}

void SffModule::customizeTransceiverLocked(
    const TransceiverPortState& portState) {
  auto speed = portState.speed;
  QSFP_LOG(INFO, this) << fmt::format(
      "customizeTransceiverLocked: PortName {}, Speed {}, StartHostLane {}, numHostLanes {}",
      portState.portName,
      apache::thrift::util::enumNameSafe(speed),
      portState.startHostLane,
      portState.numHostLanes);
  /*
   * This must be called with a lock held on qsfpModuleMutex_
   */
  if (customizationSupported()) {
    TransceiverSettings settings = getTransceiverSettingsInfo();

    // Before turning up power, check whether we want to override
    // channel control settings or no.
    overwriteChannelControlSettings();

    // We want this on regardless of speed
    setPowerOverrideIfSupportedLocked(
        getPowerControlValue(false /* readFromCache */));

    if (speed != cfg::PortSpeed::DEFAULT) {
      setCdrIfSupported(speed, *settings.cdrTx(), *settings.cdrRx());
      setRateSelectIfSupported(
          speed, *settings.rateSelect(), *settings.rateSelectSetting());
    }
  } else {
    QSFP_LOG(DBG1, this) << "Customization not supported";
  }
  currentConfiguredSpeed_ = speed;
}

/*
 * ensureTransceiverReadyLocked
 *
 * If the current power configuration state (either of: LP mode, Power
 * override, High Power override) is not same as desired one then change it to
 * that (by configuring power mode register) otherwise return true when module
 * is in ready state otherwise return false.
 */
bool SffModule::ensureTransceiverReadyLocked(
    bool /* hasTunableOpticsConfig */) {
  // If customization is not supported then the Power control bit can't be
  // touched. Return true as nothing needs to be done here
  if (!customizationSupported()) {
    QSFP_LOG(DBG1, this)
        << "ensureTransceiverReadyLocked: Customization not supported";
    return true;
  }

  int offset;
  int length;
  int dataAddress;

  getQsfpFieldAddress(
      SffField::ETHERNET_COMPLIANCE, dataAddress, offset, length);
  const uint8_t* ether = getQsfpValuePtr(dataAddress, offset, length);

  getQsfpFieldAddress(
      SffField::EXTENDED_IDENTIFIER, dataAddress, offset, length);
  const uint8_t* extId = getQsfpValuePtr(dataAddress, offset, length);

  auto desiredSetting = PowerControlState::POWER_OVERRIDE;

  // For 40G SR4 the desired power setting is LP mode. For other modules if
  // the Power Class is defined then use that value for high power override
  // otherwise just do power override
  if (*ether == EthernetCompliance::SR4_40GBASE) {
    desiredSetting = PowerControlState::POWER_LPMODE;
  } else {
    uint8_t highPowerLevel = (*extId & EXT_ID_HI_POWER_MASK);
    if (highPowerLevel > 0) {
      desiredSetting = PowerControlState::HIGH_POWER_OVERRIDE;
    }
  }

  // Find the current power control value
  uint8_t powerCtrl;
  PowerControlState currPowerControl;
  readSffField(SffField::POWER_CONTROL, &powerCtrl);
  switch (static_cast<PowerControl>(
      powerCtrl & uint8_t(PowerControl::POWER_CONTROL_MASK))) {
    case PowerControl::POWER_SET_BY_HW:
      currPowerControl = PowerControlState::POWER_SET_BY_HW;
      break;
    case PowerControl::HIGH_POWER_OVERRIDE:
      currPowerControl = PowerControlState::HIGH_POWER_OVERRIDE;
      break;
    case PowerControl::POWER_OVERRIDE:
      currPowerControl = PowerControlState::POWER_OVERRIDE;
      break;
    default:
      currPowerControl = PowerControlState::POWER_LPMODE;
      break;
  }

  // If the current power control value is same as desired power value then
  // check if the module is in ready state then return true, otherwise return
  // false as the module is in transition and needs more time to be ready for
  // any other configuration
  if (currPowerControl == desiredSetting) {
    // Check if the data is ready
    std::array<uint8_t, 2> status = {{0, 0}};
    readSffField(SffField::STATUS, status.data());

    return (!(status[1] & DATA_NOT_READY_MASK));
  }

  // Set the SFF power value register as either of: LP Mode, Power override,
  // High Power Override
  uint8_t power = uint8_t(PowerControl::POWER_OVERRIDE);
  if (desiredSetting == PowerControlState::HIGH_POWER_OVERRIDE) {
    power = uint8_t(PowerControl::HIGH_POWER_OVERRIDE);
  } else if (desiredSetting == PowerControlState::POWER_LPMODE) {
    power = uint8_t(PowerControl::POWER_LPMODE);
  }

  // enable target power class and return false as the optics need some time
  // to come back to ready state
  writeSffField(SffField::POWER_CONTROL, &power);
  QSFP_LOG(INFO, this) << "QSFP set to power setting "
                       << apache::thrift::util::enumNameSafe(desiredSetting)
                       << " (" << power << ")";
  return false;
}

/*
 * verifyEepromChecksums
 *
 * This function verifies the module's eeprom register checksum in various
 * pages. For SFF8636 module the checksums are kept in 2 pages:
 *   Page 0: Register 191 contains checksum for values in register 128 to 190
 *   Page 0: Register 223 contains checksum for values in register 192 to 222
 *   Page 1: Register 128 contains checksum for values in register 129 to 255
 * These checksums are 8 bit sum of all the 8 bit values
 */
bool SffModule::verifyEepromChecksums() {
  bool rc = true;
  // Verify checksum for all pages
  for (auto& csumInfoIt : checksumInfoSff) {
    // For flat memory module, check for page 0 only
    if (flatMem_ && csumInfoIt.first != SffPages::PAGE0) {
      continue;
    }
    rc &= verifyEepromChecksum(csumInfoIt.first);
  }
  QSFP_LOG_IF(WARN, !rc, this) << "EEPROM Checksum Failed";
  return rc;
}

/*
 * verifyEepromChecksums
 *
 * This function verifies the module's eeprom register checksum for a given
 * page. The checksum is 8 bit sum of all the 8 bit values in range of
 * registers
 */
bool SffModule::verifyEepromChecksum(SffPages pageId) {
  int offset;
  int length;
  int dataAddress;
  const uint8_t* data;
  uint8_t checkSum, expectedChecksum;

  // Return false if the registers are not cached yet (this is not expected)
  if (!cacheIsValid()) {
    QSFP_LOG(WARN, this)
        << "Can't do eeprom checksum as the register cache is not populated";
    return false;
  }
  // Return false if we don't know range of registers to validate the checksum
  // on this page
  if (checksumInfoSff.find(pageId) == checksumInfoSff.end()) {
    QSFP_LOG(WARN, this) << "Can't do eeprom checksum for page "
                         << static_cast<int>(pageId);
    return false;
  }

  // Get the range of registers, compute checksum and compare
  for (auto& csumRange : checksumInfoSff[pageId]) {
    dataAddress = static_cast<int>(pageId);
    offset = csumRange.checksumRangeStartOffset;
    length = csumRange.checksumRangeStartLength;
    data = getQsfpValuePtr(dataAddress, offset, length);

    checkSum = 0;
    for (int i = 0; i < length; i++) {
      checkSum += data[i];
    }

    getQsfpFieldAddress(
        csumRange.checksumValOffset, dataAddress, offset, length);
    data = getQsfpValuePtr(dataAddress, offset, length);
    expectedChecksum = data[0];

    if (checkSum != expectedChecksum) {
      QSFP_LOG(ERR, this) << fmt::format(
          "Page {:d}: expected checksum {:#x}, actual {:#x}",
          static_cast<int>(pageId),
          expectedChecksum,
          checkSum);
      return false;
    } else {
      QSFP_LOG(DBG5, this) << fmt::format(
          "Page {:d}: checksum verified successfully {:#x}",
          static_cast<int>(pageId),
          checkSum);
    }
  }
  return true;
}

bool SffModule::supportRemediate() {
  // Since Miniphton module is always showing as present and four ports
  // sharing a single optical module. Doing remediation on one port will
  // have side effect on the neighbor port as well. So we don't do
  // remediation as suggested by our HW optic team.
  const auto& cachedTcvrInfo = getTransceiverInfo();
  if (cachedTcvrInfo.tcvrState()->vendor().has_value() &&
      *cachedTcvrInfo.tcvrState()->vendor()->partNumber() ==
          kMiniphotonPartNumber) {
    return false;
  } else if (
      cachedTcvrInfo.tcvrState()->cable() &&
      *cachedTcvrInfo.tcvrState()->cable()->transmitterTech() ==
          TransmitterTechnology::COPPER) {
    return false;
  }

  return true;
}

/*
 * setDiagsCapability
 *
 * This function reads the module register from cache and populates the
 * diagnostic capability. This function is called from Module State Machine
 * when the MSM enters Module Discovered state after EEPROM read.
 */
void SffModule::setDiagsCapability() {
  auto diagsCapability = diagsCapability_.wlock();
  if (!(*diagsCapability).has_value()) {
    if (auto diagsCapabilityOverride = getDiagsCapabilityOverride()) {
      *diagsCapability = *diagsCapabilityOverride;
    }

    auto txOpCtrl = getSettingsValue(
                        SffField::TX_CONTROL_SUPPORT,
                        DiagsCapabilityMask::TX_DISABLE_SUPPORT_MASK)
        ? true
        : false;
    auto rxOpCtrl = getSettingsValue(
                        SffField::RX_CONTROL_SUPPORT,
                        DiagsCapabilityMask::RX_DISABLE_SUPPORT_MASK)
        ? true
        : false;

    // Only Miniphoton has loopback capability
    bool lbCapability{false};
    const auto& cachedTcvrInfo = getTransceiverInfo();
    if (cachedTcvrInfo.tcvrState().value().vendor().has_value() &&
        *cachedTcvrInfo.tcvrState().value().vendor()->partNumber() ==
            kMiniphotonPartNumber) {
      lbCapability = true;
    }

    if ((*diagsCapability).has_value()) {
      diagsCapability->value().txOutputControl() = txOpCtrl;
      diagsCapability->value().rxOutputControl() = rxOpCtrl;
      diagsCapability->value().loopbackLine() = lbCapability;
      diagsCapability->value().loopbackSystem() = lbCapability;
    } else {
      DiagsCapability diags;
      diags.txOutputControl() = txOpCtrl;
      diags.rxOutputControl() = rxOpCtrl;
      diags.loopbackLine() = lbCapability;
      diags.loopbackSystem() = lbCapability;
      *diagsCapability = diags;
    }

    // If tx/rx disable or loopback is supported then diagnostic support is
    // true
    if ((txOpCtrl || rxOpCtrl || lbCapability) &&
        !(*diagsCapability).value().diagnostics().value()) {
      (*diagsCapability).value().diagnostics() = true;
    }
  }

  QSFP_LOG(DBG2, this) << fmt::format(
      "setDiagsCapability: Module {} TxOpCtrl {:s} RxOpCtrl {:s} LbLine {:s} LbSys {:s}",
      qsfpImpl_->getName(),
      ((*diagsCapability).value().txOutputControl().value() ? "Y" : "N"),
      ((*diagsCapability).value().rxOutputControl().value() ? "Y" : "N"),
      ((*diagsCapability).value().loopbackLine().value() ? "Y" : "N"),
      ((*diagsCapability).value().loopbackSystem().value() ? "Y" : "N"));
}

/*
 * Set the PRBS Generator and Checker on a module for the desired side (Line
 * or System side).
 * This function expects the caller to hold the qsfp module level lock
 */
bool SffModule::setPortPrbsLocked(
    const std::string& /* portName */,
    phy::Side side,
    const prbs::InterfacePrbsState& prbs) {
  auto enable = (prbs.generatorEnabled().has_value() &&
                 prbs.generatorEnabled().value()) ||
      (prbs.checkerEnabled().has_value() && prbs.checkerEnabled().value());
  auto polynomial = *(prbs.polynomial());

  if (isTransceiverFeatureSupported(TransceiverFeature::PRBS, side)) {
    // Check if there is an override function available for setting prbs
    // state
    if (auto prbsEnable = setPortPrbsOverrideLocked(side, prbs)) {
      QSFP_LOG(INFO, this) << fmt::format(
          "Prbs {:s} {:s} on {:s} side",
          apache::thrift::util::enumNameSafe(polynomial),
          enable ? "enabled" : "disabled",
          apache::thrift::util::enumNameSafe(side));
      return true;
    }
  }

  QSFP_LOG(WARNING, this) << fmt::format(
      "Does not support PRBS on {:s} side",
      apache::thrift::util::enumNameSafe(side));
  return false;
}

// This function expects caller to hold the qsfp module level lock
prbs::InterfacePrbsState SffModule::getPortPrbsStateLocked(
    std::optional<const std::string> /* portName */,
    Side side) {
  if (!isTransceiverFeatureSupported(TransceiverFeature::PRBS, side)) {
    return prbs::InterfacePrbsState();
  }

  // Certain modules have proprietary methods to get prbs state, check if
  // there exists one
  if (auto prbsState = getPortPrbsStateOverrideLocked(side)) {
    return *prbsState;
  }
  return prbs::InterfacePrbsState();
}

/*
 * setTransceiverTxLocked
 *
 * Set the Tx output enabled/disabled for the given channels of a transceiver
 * in a given direction. For line side this will cause LOS on the peer optics
 * Rx. For system side this will cause LOS on the corresponding IPHY lanes or
 * the XPHY line side lanes in case of system with external PHY
 */
bool SffModule::setTransceiverTxLocked(
    const std::string& portName,
    phy::Side side,
    std::optional<uint8_t> userChannelMask,
    bool enable) {
  // Get the list of lanes to disable/enable the Tx output
  auto tcvrLanes = getTcvrLanesForPort(portName, side);

  if (tcvrLanes.empty()) {
    QSFP_LOG(ERR, this) << fmt::format(
        "No lanes available for port {:s}", portName);
    return false;
  }

  return setTransceiverTxImplLocked(tcvrLanes, side, userChannelMask, enable);
}

bool SffModule::setTransceiverTxImplLocked(
    const std::set<uint8_t>& tcvrLanes,
    phy::Side side,
    std::optional<uint8_t> userChannelMask,
    bool enable) {
  if (tcvrLanes.empty()) {
    QSFP_LOG(ERR, this) << "No lanes available";
    return false;
  }

  // Check if the module supports Tx control feature first
  if (!isTransceiverFeatureSupported(TransceiverFeature::TX_DISABLE, side)) {
    throw FbossError(
        fmt::format(
            "Module {:s} does not support transceiver TX output control on {:s}",
            qsfpImpl_->getName(),
            ((side == phy::Side::LINE) ? "Line" : "System")));
  }

  auto txControlReg = (side == phy::Side::LINE) ? SffField::TX_DISABLE
                                                : SffField::TXRX_OUTPUT_CONTROL;
  uint8_t txDisableVal, rxTxCtrl;

  readSffField(txControlReg, &rxTxCtrl);
  txDisableVal = (side == phy::Side::LINE) ? rxTxCtrl : ((rxTxCtrl >> 4) & 0xF);

  txDisableVal =
      setTxChannelMask(tcvrLanes, userChannelMask, enable, txDisableVal);

  rxTxCtrl = (side == phy::Side::LINE)
      ? txDisableVal
      : ((txDisableVal << 4) | (rxTxCtrl & 0xF));
  writeSffField(txControlReg, &rxTxCtrl);
  return true;
}

/*
 * setTransceiverLoopbackLocked
 *
 * Sets or resets the loopback on the given lanes for the SW Port on system
 * or line side of the Transceiver. For line side this should bring up the NPU
 * port. For system side, this should bring up the peer side port.
 */
void SffModule::setTransceiverLoopbackLocked(
    const std::string& portName,
    phy::Side side,
    bool setLoopback,
    phy::LoopbackMode mode) {
  if (mode != phy::LoopbackMode::INPUT) {
    throw FbossError(
        fmt::format(
            "Module {:s} only supports INPUT loopback, got {:s}",
            portName,
            apache::thrift::util::enumNameSafe(mode)));
  }

  // Check if the module supports Loopback feature first
  if (!isTransceiverFeatureSupported(TransceiverFeature::LOOPBACK, side)) {
    throw FbossError(
        fmt::format(
            "Module {:s} does not support transceiver loopback on {:s}",
            portName,
            ((side == phy::Side::LINE) ? "Line" : "System")));
  }

  // For Miniphoton alone, there is no need to cache read the diag page 128
  // every refresh cycle so let's do the direct register access
  // Page 128, register 245:
  // 0b01010101 - host loopback
  // 0b10101010 - line loopback
  // 0 - no loopback
  uint8_t loopbackVal{0};
  if (setLoopback) {
    loopbackVal = (side == phy::Side::SYSTEM) ? MINIPHOTON_LPBK_SYSTEM_MASK
                                              : MINIPHOTON_LPBK_LINE_MASK;
  }

  writeSffField(SffField::MINIPHOTON_LOOPBACK, &loopbackVal);

  QSFP_LOG(DBG2, this) << fmt::format(
      "Port {:s} wrote Loopback register value {:#x}", portName, loopbackVal);

  QSFP_LOG(DBG2, this) << fmt::format(
      "Port {:s} {:s} Loopback {:s}",
      portName,
      (side == phy::Side::SYSTEM ? "system" : "line"),
      (setLoopback ? "set" : "reset"));
}

} // namespace fboss
} // namespace facebook
