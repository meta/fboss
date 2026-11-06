include "thrift/annotation/thrift.thrift"

@thrift.AllowLegacyMissingUris
package;

namespace cpp2 facebook.fboss
namespace go neteng.fboss.cmis
namespace py neteng.fboss.cmis
namespace py3 neteng.fboss
namespace py.asyncio neteng.fboss.asyncio.cmis

enum CmisField {
  // First 10 Fields reserved for fields common between
  // cmis/sff/sff8472
  // Raw Read / Write.
  RAW = 0,
  // Field associated with CDB Command
  CDB_COMMAND = 1,
  // Field associated with FW upgrade
  FW_UPGRADE = 2,
  // Field associated with page change
  PAGE_CHANGE = 3,
  // Management Interface
  MGMT_INTERFACE = 4,
  // Part Number Operations
  PART_NUM = 5,
  // Firmware Version
  FW_VERSION = 6,

  // Lower Page
  PAGE_LOWER = 11,
  IDENTIFIER = 12,
  REVISION_COMPLIANCE = 13,
  FLAT_MEM = 14,
  MODULE_STATE = 15,
  BANK0_FLAGS = 16,
  BANK1_FLAGS = 17,
  BANK2_FLAGS = 18,
  BANK3_FLAGS = 19,
  MODULE_FLAG = 20,
  MODULE_ALARMS = 21,
  TEMPERATURE = 22,
  VCC = 23,
  MODULE_CONTROL = 24,
  FIRMWARE_REVISION = 25,
  MEDIA_TYPE_ENCODINGS = 26,
  APPLICATION_ADVERTISING1 = 27,
  BANK_SELECT = 28,
  PAGE_SELECT_BYTE = 29,
  // Page 00h
  PAGE_UPPER00H = 30,
  VENDOR_NAME = 31,
  VENDOR_OUI = 32,
  PART_NUMBER = 33,
  REVISION_NUMBER = 34,
  VENDOR_SERIAL_NUMBER = 35,
  MFG_DATE = 36,
  EXTENDED_SPECIFICATION_COMPLIANCE = 37,
  LENGTH_COPPER = 38,
  MEDIA_INTERFACE_TECHNOLOGY = 39,
  PAGE0_CSUM = 40,
  // Page 01h
  PAGE_UPPER01H = 41,
  LENGTH_SMF = 42,
  LENGTH_OM5 = 43,
  LENGTH_OM4 = 44,
  LENGTH_OM3 = 45,
  LENGTH_OM2 = 46,
  VDM_DIAG_SUPPORT = 47,
  TX_CONTROL_SUPPORT = 48,
  RX_CONTROL_SUPPORT = 49,
  TX_BIAS_MULTIPLIER = 50,
  TX_SIG_INT_CONT_AD = 51,
  RX_SIG_INT_CONT_AD = 52,
  CDB_SUPPORT = 53,
  MEDIA_LANE_ASSIGNMENT = 54,
  DSP_FW_VERSION = 55,
  BUILD_REVISION = 56,
  APPLICATION_ADVERTISING2 = 57,
  PAGE1_CSUM = 58,
  // Page 02h
  PAGE_UPPER02H = 59,
  TEMPERATURE_THRESH = 60,
  VCC_THRESH = 61,
  TX_PWR_THRESH = 62,
  TX_BIAS_THRESH = 63,
  RX_PWR_THRESH = 64,
  PAGE2_CSUM = 65,
  // Page 10h
  PAGE_UPPER10H = 66,
  DATA_PATH_DEINIT = 67,
  TX_POLARITY_FLIP = 68,
  TX_DISABLE = 69,
  TX_SQUELCH_DISABLE = 70,
  TX_FORCE_SQUELCH = 71,
  TX_ADAPTATION_FREEZE = 72,
  TX_ADAPTATION_STORE = 73,
  RX_POLARITY_FLIP = 74,
  RX_DISABLE = 75,
  RX_SQUELCH_DISABLE = 76,
  STAGE_CTRL_SET_0 = 77,
  STAGE_CTRL_SET0_IMMEDIATE = 78,
  APP_SEL_LANE_1_8 = 79,
  APP_SEL_LANE_1 = 80,
  APP_SEL_LANE_2 = 81,
  APP_SEL_LANE_3 = 82,
  APP_SEL_LANE_4 = 83,
  APP_SEL_LANE_5 = 84,
  APP_SEL_LANE_6 = 85,
  APP_SEL_LANE_7 = 86,
  APP_SEL_LANE_8 = 87,
  RX_CONTROL_PRE_CURSOR = 88,
  RX_CONTROL_POST_CURSOR = 89,
  RX_CONTROL_MAIN = 90,
  // Page 11h
  PAGE_UPPER11H = 91,
  DATA_PATH_STATE = 92,
  TX_FAULT_FLAG = 93,
  TX_LOS_FLAG = 94,
  TX_LOL_FLAG = 95,
  TX_EQ_FLAG = 96,
  TX_PWR_FLAG = 97,
  TX_BIAS_FLAG = 98,
  RX_LOS_FLAG = 99,
  RX_LOL_FLAG = 100,
  RX_PWR_FLAG = 101,
  CHANNEL_TX_PWR = 102,
  CHANNEL_TX_BIAS = 103,
  CHANNEL_RX_PWR = 104,
  CONFIG_ERROR_LANES = 105,
  ACTIVE_CTRL_ALL_LANES = 106,
  ACTIVE_CTRL_LANE_1 = 107,
  ACTIVE_CTRL_LANE_2 = 108,
  ACTIVE_CTRL_LANE_3 = 109,
  ACTIVE_CTRL_LANE_4 = 110,
  ACTIVE_CTRL_LANE_5 = 111,
  ACTIVE_CTRL_LANE_6 = 112,
  ACTIVE_CTRL_LANE_7 = 113,
  ACTIVE_CTRL_LANE_8 = 114,
  TX_CDR_CONTROL = 115,
  RX_CDR_CONTROL = 116,
  RX_OUT_PRE_CURSOR = 117,
  RX_OUT_POST_CURSOR = 118,
  RX_OUT_MAIN = 119,
  // Page 13h
  PAGE_UPPER13H = 120,
  // Page 14h
  PAGE_UPPER14H = 121,
  // Page 1Ch
  PAGE_UPPER1CH = 122,
  LOOPBACK_CAPABILITY = 123,
  PATTERN_CAPABILITY = 124,
  DIAGNOSTIC_CAPABILITY = 125,
  PATTERN_CHECKER_CAPABILITY = 126,
  HOST_SUPPORTED_GENERATOR_PATTERNS = 127,
  MEDIA_SUPPORTED_GENERATOR_PATTERNS = 128,
  HOST_SUPPORTED_CHECKER_PATTERNS = 129,
  MEDIA_SUPPORTED_CHECKER_PATTERNS = 130,
  HOST_GEN_ENABLE = 131,
  HOST_GEN_INV = 132,
  HOST_GEN_PRE_FEC = 133,
  HOST_PATTERN_SELECT_LANE_2_1 = 134,
  HOST_PATTERN_SELECT_LANE_4_3 = 135,
  HOST_PATTERN_SELECT_LANE_6_5 = 136,
  HOST_PATTERN_SELECT_LANE_8_7 = 137,
  MEDIA_GEN_ENABLE = 138,
  MEDIA_GEN_INV = 139,
  MEDIA_GEN_PRE_FEC = 140,
  MEDIA_PATTERN_SELECT_LANE_2_1 = 141,
  MEDIA_PATTERN_SELECT_LANE_4_3 = 142,
  MEDIA_PATTERN_SELECT_LANE_6_5 = 143,
  MEDIA_PATTERN_SELECT_LANE_8_7 = 144,
  HOST_CHECKER_ENABLE = 145,
  HOST_CHECKER_INV = 146,
  HOST_CHECKER_POST_FEC = 147,
  HOST_CHECKER_PATTERN_SELECT_LANE_2_1 = 148,
  HOST_CHECKER_PATTERN_SELECT_LANE_4_3 = 149,
  HOST_CHECKER_PATTERN_SELECT_LANE_6_5 = 150,
  HOST_CHECKER_PATTERN_SELECT_LANE_8_7 = 151,
  MEDIA_CHECKER_ENABLE = 152,
  MEDIA_CHECKER_INV = 153,
  MEDIA_CHECKER_POST_FEC = 154,
  MEDIA_CHECKER_PATTERN_SELECT_LANE_2_1 = 155,
  MEDIA_CHECKER_PATTERN_SELECT_LANE_4_3 = 156,
  MEDIA_CHECKER_PATTERN_SELECT_LANE_6_5 = 157,
  MEDIA_CHECKER_PATTERN_SELECT_LANE_8_7 = 158,
  REF_CLK_CTRL = 159,
  BER_CTRL = 160,
  MEDIA_OUTPUT_LB_EN = 161,
  MEDIA_INPUT_LB_EN = 162,
  HOST_OUTPUT_LB_EN = 163,
  HOST_INPUT_LB_EN = 164,
  REF_CLK_LOSS = 165,
  HOST_CHECKER_GATING_COMPLETE = 166,
  MEDIA_CHECKER_GATING_COMPLETE = 167,
  HOST_PPG_LOL = 168,
  MEDIA_PPG_LOL = 169,
  HOST_BERT_LOL = 170,
  MEDIA_BERT_LOL = 171,
  // Page 1Dh
  PAGE_UPPER1DH = 172,
  DIAG_SEL = 173,
  HOST_LANE_GENERATOR_LOL_LATCH = 174,
  MEDIA_LANE_GENERATOR_LOL_LATCH = 175,
  HOST_LANE_CHECKER_LOL_LATCH = 176,
  MEDIA_LANE_CHECKER_LOL_LATCH = 177,
  HOST_BER = 178,
  MEDIA_BER_HOST_SNR = 179,
  MEDIA_SNR = 180,
  // Page 20h
  PAGE_UPPER20H = 181,
  // Page 21h
  PAGE_UPPER21H = 182,
  // Page 22h
  PAGE_UPPER22H = 183,
  // Page 24h
  PAGE_UPPER24H = 184,
  // Page 25h
  PAGE_UPPER25H = 185,
  // Page 26h
  PAGE_UPPER26H = 186,
  // Page 2Fh
  PAGE_UPPER2FH = 187,
  VDM_GROUPS_SUPPORT = 188,
  VDM_LATCH_REQUEST = 189,
  VDM_LATCH_DONE = 190,

  // Page 2Ch
  PAGE_UPPER2CH = 191,
  PAM4_MPI_ALARMS = 192,

  // Fields Newly added
  // Datapath Init / Deinit max time
  MAX_DPINIT_TIME = 193,

  // Page 23h (VDM Group 4 Config)
  PAGE_UPPER23H = 194,
  // Page 27h (VDM Group 4 Data)
  PAGE_UPPER27H = 195,

  // Custom Fields
  // These are fields that are custom to specific transceivers
  //
  // FEC sampling percentage currenlty active on the module.
  // The value applies to all datapaths.
  // Datapath state or module operation would not be
  // interrupted during this configuration.
  FEC_SAMPLING_PCT = 400,

  // ZR optics specific fields
  // Page 04h,  Laser Capabilities Advertising
  PAGE_UPPER04H = 401,
  LASER_GRIDS_ADVER = 402,
  FINE_TUNING_ADVER = 403,
  LASER_3P125_GHZ_LO_CHAN = 404,
  LASER_3P125_GHZ_HI_CHAN = 405,
  LASER_6P25_GHZ_LO_CHAN = 406,
  LASER_6P25_GHZ_HI_CHAN = 407,
  LASER_12P5_GHZ_LO_CHAN = 408,
  LASER_12P5_GHZ_HI_CHAN = 409,
  LASER_25_GHZ_LO_CHAN = 410,
  LASER_25_GHZ_HI_CHAN = 411,
  LASER_50_GHZ_LO_CHAN = 412,
  LASER_50_GHZ_HI_CHAN = 413,
  LASER_100_GHZ_LO_CHAN = 414,
  LASER_100_GHZ_HI_CHAN = 415,
  LASER_33_GHZ_LO_CHAN = 416,
  LASER_33_GHZ_HI_CHAN = 417,
  LASER_75_GHZ_LO_CHAN = 418,
  LASER_75_GHZ_HI_CHAN = 419,
  LASER_150_GHZ_LO_CHAN = 420,
  LASER_150_GHZ_HI_CHAN = 421,
  LASER_FINE_TUNE_RES = 422,
  LASER_FINE_TUNE_LO_OFFSET = 423,
  LASER_FINE_TUNE_HI_OFFSET = 424,
  MEDIA_TX_PROG_OUT_PWR_ADVER = 425,
  MEDIA_MIN_TX_PROG_OUT_PWR = 426,
  MEDIA_MAX_TX_PROG_OUT_PWR = 427,
  PAGE4_CSUM = 428,

  // Page 12h, Tunable Laser Control and Status
  PAGE_UPPER12H = 429,
  MEDIA_TX_1_GRID_AND_FINE_TUNE_ENA = 430,
  MEDIA_TX_1_CHAN_NBR_SEL = 431,
  MEDIA_TX_1_FINE_TUNE_FREQ_OFFSET = 432,
  MEDIA_TX_1_CURR_LAS_FREQ = 433,
  MEDIA_TX_1_TGT_OUTPUT_PWR = 434,
  MEDIA_TX_1_LAS_STAT = 435,
  MEDIA_TX_LAS_TUNE_SUM = 436,
  MEDIA_TX_1_LAS_STAT_FLAGS = 437,
  MEDIA_TX_1_LAS_STAT_MASKS = 438,

  APP_SEL_LANE_1_2 = 439,
  APP_SEL_LANE_3_4 = 440,
  APP_SEL_LANE_5_6 = 441,
  APP_SEL_LANE_7_8 = 442,
  APP_SEL_LANE_1_4 = 443,
  APP_SEL_LANE_5_8 = 444,

  RX_CONTROL_PRE_CURSOR_LANE_01 = 445,
  RX_CONTROL_PRE_CURSOR_LANE_23 = 446,
  RX_CONTROL_PRE_CURSOR_LANE_45 = 447,
  RX_CONTROL_PRE_CURSOR_LANE_67 = 448,

  RX_CONTROL_POST_CURSOR_LANE_01 = 449,
  RX_CONTROL_POST_CURSOR_LANE_23 = 450,
  RX_CONTROL_POST_CURSOR_LANE_45 = 451,
  RX_CONTROL_POST_CURSOR_LANE_67 = 452,

  RX_CONTROL_MAIN_LANE_01 = 453,
  RX_CONTROL_MAIN_LANE_23 = 454,
  RX_CONTROL_MAIN_LANE_45 = 455,
  RX_CONTROL_MAIN_LANE_67 = 456,

  # Page 10 Input Equilizer settig
  INPUT_EQ_TX_1_2 = 457,
  INPUT_EQ_TX_3_4 = 458,
  INPUT_EQ_TX_5_6 = 459,
  INPUT_EQ_TX_7_8 = 460,

  // C-CMIS Performance Monitoring Pages
  // Page 34h - Lane FEC Performance Monitoring
  PAGE_UPPER34H = 461,
  // Page 35h - Lane Link Performance Monitoring
  PAGE_UPPER35H = 462,

  // Page 38h - Data Path Host Interface Configuration
  PAGE_UPPER38H = 463,
  // Page 38h, Byte 137: Consequent Action control
  //   Bits 7-4 (rxConsAct): media to host direction
  //   Bits 3-0 (txConsAct): host to media direction
  CONS_ACT_CONTROL = 464,

  // Page 45h - Host Lane Provisioning Advertisement
  PAGE_UPPER45H = 465,
  // Page 45h, Byte 129: Host Lane Provisioning Advertisement
  // Bits 6-0: txCStatLCKInsertionImpl, rxRplcSigInsertionImpl,
  //   txRplcSigInsertionImpl, txConsActHoldOffTmrImpl,
  //   rxConsActHoldOffTmrImpl, rxConsActImpl, txConsActImpl
  HOST_LANE_PROV_AD = 466,
  // Page 38h, Bytes 141-142: Rx Consequent Action Hold-off Timer
  // 16-bit value in 10ms increments. 0 = disabled.
  CONS_ACT_HOLD_OFF_TMR = 467,

  // Lower Page 00h, Byte 70: Maximum CMIS bank capacity. Holds the module's
  // peak bank count directly (e.g. 4 for a 32-lane module).
  MAX_BANK_CAPACITY = 468,

  // Page 01h, Byte 191: Meta custom feature advertisement
  //   Bit 2: LaserTempMarginSupported
  //   Bit 1: DSPTempMarginSupported
  //   Bit 0: ModeMismatchFlagSupported
  SUPPORTED_CUSTOM_FEATURES = 469,

  // Lower Page 00h, Byte 67: Meta custom latched flags
  //   Bit 2: LaserTempNegativeMarginFlag
  //   Bit 1: DSPTempNegativeMarginFlag
  //   Bit 0: ModeMismatchFlag
  CUSTOM_FLAGS = 470,
  // Lower Page 00h, Byte 68: DSP temperature margin, S8 in 1/4 degree C
  DSP_TEMP_MARGIN = 471,
  // Lower Page 00h, Byte 69: laser temperature margin, S8 in 1/4 degree C
  LASER_TEMP_MARGIN = 472,

  // Page 14h, Byte 130: per-host-lane mode mismatch, one bit per lane
  HOST_MODE_MISMATCH = 473,
  // Page 14h, Byte 131: per-media-lane mode mismatch, one bit per lane
  MEDIA_MODE_MISMATCH = 474,
  // Page C0, Byte 131: Host Lane Ethernet Control (ZR modules)
  HOST_LANE_ETH_CTRL = 475,
}
