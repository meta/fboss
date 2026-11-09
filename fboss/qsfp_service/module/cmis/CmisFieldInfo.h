/* * Copyright (c) 2004-present, Facebook, Inc.  * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once
#include <cstdint>
#include <map>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"
#include "fboss/qsfp_service/module/QsfpFieldInfo.h"
#include "fboss/qsfp_service/module/cmis/gen-cpp2/cmis_types.h"

/*
 * Parse transceiver data fields, as outlined in
 * Common Management Interface Specification by SFF commitee at
 * http://www.qsfp-dd.com/wp-content/uploads/2019/05/QSFP-DD-CMIS-rev4p0.pdf
 */

namespace facebook {
namespace fboss {

enum class CmisPages;

enum FieldMasks : uint8_t {
  CABLE_LENGTH_MASK = 0x3f, // Highest two bits are multipliers.
  CDR_IMPL_MASK = 0x01,

  // Byte 26 (Module Global Controls) bitmask:
  // Bit 6: LowPwrAllowRequestHW in CMIS 5.0, LowPwr in 4.0
  // Bit 5: Squelch control: 0 = Squelch reduces OMA, 1 = Squelch reduces Pave
  // Bit 4: LowPwrRequestSW in CMIS 5.0, ForceLowPwr in 4.0
  LOW_PWR_BIT = (1 << 6),
  SQUELCH_CONTROL = (1 << 5),
  FORCE_LOW_PWR_BIT = (1 << 4),
  // When clearing LP mode, we'll clear both bits. When setting LP mode,
  // we'll only set bit 6
  POWER_CONTROL_MASK = LOW_PWR_BIT | FORCE_LOW_PWR_BIT,

  APP_SEL_BITSHIFT = 4,
  APP_SEL_MASK = 0xf0,
  DATA_PATH_ID_MASK = 0xe,
  DATA_PATH_ID_BITSHIFT = 0x1,
  FWFAULT_MASK = 0x06,
  MODULE_STATE_CHANGED_MASK = 0x01,
  UPPER_FOUR_BITS_MASK = 0xf0,
  LOWER_FOUR_BITS_MASK = 0x0f,
  VDM_SUPPORT_MASK = 0x40,
  VDM_LATCH_REQUEST_MASK = 0x80,
  // Page 2Fh byte 145 bit 7 = FreezeDone
  VDM_LATCH_DONE_MASK = 0x80,
  DIAGS_SUPPORT_MASK = 0x20,
  CDB_SUPPORT_MASK = 0xc0,
  LOOPBACK_SYS_SUPPOR_MASK = 0x09,
  LOOPBACK_LINE_SUPPORT_MASK = 0x06,
  LOOPBACK_MEDIA_OUTPUT_MASK = 0x01,
  LOOPBACK_MEDIA_INPUT_MASK = 0x02,
  LOOPBACK_HOST_OUTPUT_MASK = 0x04,
  LOOPBACK_HOST_INPUT_MASK = 0x08,
  LOOPBACK_PER_LANE_HOST_MASK = 0x10,
  LOOPBACK_PER_LANE_MEDIA_MASK = 0x20,
  LOOPBACK_SIMULTANEOUS_HOST_MEDIA_MASK = 0x40,
  PRBS_SYS_SUPPRT_MASK = 0x0f,
  PRBS_LINE_SUPPRT_MASK = 0xf0,
  TX_BIAS_MULTIPLIER_MASK = 0x18,
  MODULE_STATUS_MASK = 0x0E,
  TX_DISABLE_SUPPORT_MASK = 0x02,
  RX_DISABLE_SUPPORT_MASK = 0x02,
  SNR_LINE_SUPPORT_MASK = 0x20,
  SNR_SYS_SUPPORT_MASK = 0x10,
  VDM_GROUPS_SUPPORT_MASK = 0x03,
  VDM_DESCRIPTOR_RESOURCE_MASK = 0x0f,
  CDB_FW_DOWNLOAD_EPL_SUPPORTED = 0x10,
  CDB_FW_DOWNLOAD_LPL_EPL_SUPPORTED = 0x11,
  BER_CTRL_RESET_STAT_MASK = 0x20,
  // Page 38h, Byte 137, Bits 7-4 (rxConsAct):
  //   0001 = insert LF (media to host direction)
  RX_CONS_ACT_INSERT_LF = 0x10,
  // Page 45h, Byte 129, Bit 1: rxConsActImpl
  RX_CONS_ACT_IMPL_MASK = 0x02,
  // Page 45h, Byte 129, Bit 2: rxConsActHoldOffTmrImpl
  RX_CONS_ACT_HOLD_OFF_TMR_IMPL_MASK = 0x04,
  // Page 01h, Byte 191 (Meta custom feature advertisement)
  // Bit 0: ModeMismatchFlagSupported
  // Bit 1: DSPTempMarginSupported
  // Bit 2: LaserTempMarginSupported
  MODE_MISMATCH_SUPPORT_MASK = 0x01,
  DSP_TEMP_MARGIN_SUPPORT_MASK = 0x02,
  LASER_TEMP_MARGIN_SUPPORT_MASK = 0x04,
  // Lower Memory, Byte 67 (Meta custom latched flags)
  // Bit 0: ModeMismatchFlag
  // Bit 1: DSPTempNegativeMarginFlag
  // Bit 2: LaserTempNegativeMarginFlag
  MODE_MISMATCH_FLAG_MASK = 0x01,
  DSP_TEMP_NEGATIVE_MARGIN_FLAG_MASK = 0x02,
  LASER_TEMP_NEGATIVE_MARGIN_FLAG_MASK = 0x04,
  // 3 bits for PROTOCOL_SEL
  HOST_ETH_PROTOCOL_SELECT_MASK = 0x7,
};

enum FieldBitShift : uint8_t {
  MODULE_STATUS_BITSHIFT = 1,
};

enum DeviceTechnologyCmis : uint8_t {
  OPTICAL_MAX_VALUE_CMIS = 0x09,
  UNKNOWN_VALUE_CMIS = 0x08,
  C_BAND_TUNABLE_LASER_CMIS = 0x10,
  L_BAND_TUNABLE_LASER_CMIS = 0x11,
};

class CmisFieldInfo : public QsfpFieldInfo<CmisField, CmisPages> {
 public:
  // Render degrees Celcius from fix-point integer value
  static double getTemp(uint16_t temp);

  // Render Vcc in volts from fixed-point value
  static double getVcc(uint16_t temp);

  // Render TxBias in mA from fixed-point value
  static double getTxBias(uint16_t temp);

  // Render power in mW from fixed-point value
  static double getPwr(uint16_t temp);

  // Render SNR from 16 bits fixed-point value
  static double getSnr(const uint16_t data);

  // Render Precursor from 4 bits fixed-point value
  static double getPreCursor(const uint8_t data);

  // Render Postcursor from 4 bits fixed-point value
  static double getPostCursor(const uint8_t data);

  // Render Amplitude from 4 bits fixed-point value
  static std::pair<uint32_t, uint32_t> getAmplitude(const uint8_t data);

  // Render result as a member of FeatureState enum
  static FeatureState getFeatureState(uint8_t support, uint8_t enabled = 1);

  // Get Tx Bias current multiplier
  static uint8_t getTxBiasMultiplier(const uint8_t data);
};

// Store multipliers for various conversion functions:

using CmisFieldMultiplier = std::map<CmisField, std::uint32_t>;

// Store the mapping between port speed and ApplicationCode.
// We use the module Media Interface ID, which is located at the second byte
// of the APPLICATION_ADVERTISING field, as Application ID here, which is the
// code from Table4-7 SM media interface codes in Sff-8024.

using SpeedApplicationMapping =
    std::map<cfg::PortSpeed, std::vector<SMFMediaInterfaceCode>>;

} // namespace fboss
} // namespace facebook
