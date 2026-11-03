// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/qsfp_service/module/cmis/CmisModule.h"

#include <boost/assign.hpp>
#include <boost/bimap.hpp>
#include <fmt/core.h>
#include <folly/io/IOBuf.h>
#include <folly/io/async/EventBase.h>
#include <folly/logging/xlog.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include "common/time/Time.h"
#include "fboss/agent/FbossError.h"
#include "fboss/lib/phy/gen-cpp2/prbs_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/qsfp_service_config_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"
#include "fboss/qsfp_service/lib/QsfpConfigParserHelper.h"
#include "fboss/qsfp_service/module/CdbCommandBlock.h"
#include "fboss/qsfp_service/module/FirmwareUpgrader.h"
#include "fboss/qsfp_service/module/QsfpFieldInfo.h"
#include "fboss/qsfp_service/module/QsfpHelper.h"
#include "fboss/qsfp_service/module/TransceiverImpl.h"
#include "fboss/qsfp_service/module/cmis/CmisFieldInfo.h"
#include "fboss/qsfp_service/module/cmis/CmisHelper.h"
#include "fboss/qsfp_service/module/properties/TransceiverPropertiesManager.h"

#include <thrift/lib/cpp/util/EnumUtils.h>

using folly::IOBuf;
using std::lock_guard;
using std::memcpy;
using std::mutex;
using namespace apache::thrift;

DEFINE_bool(
    set_max_fec_sampling,
    false,
    "Flag to enable setting max FEC sampling for module");

DEFINE_bool(
    enable_explicit_control,
    false,
    "Flag to explicit control to module values overridden for specific vendors");

namespace {

// Lower Page 00h register offsets used to switch the active bank/page before a
// paged access (CMIS bank-select is byte 126, page-select is byte 127).
constexpr uint8_t kBankSelectByteOffset = 126;
constexpr uint8_t kPageSelectByteOffset = 127;

constexpr int kUsecBetweenPowerModeFlap = 100000;
constexpr int kUsecBetweenLaneInit = 10000; // 10ms
constexpr int kUsecDiagSelectLatchWaitPrbs = 350000; // 350 ms
constexpr int kUsecAfterAppProgramming = 500000;
constexpr int kUsecDatapathStateUpdateTime = 10000000; // 10 seconds
constexpr int kUsecDatapathStatePollTime = 500000; // 500 ms
// Application Advertising: ApSel 1-8 in Lower Page 00h, 9-15 in Page 01h
constexpr uint8_t kNumLowerPageApplications = 8;
constexpr uint8_t kMaxNumApplications = 15;
constexpr double kU16TypeLsbDivisor = 256.0;
constexpr int kVdmDescriptorLength = 2;

// Definitions for CDB Histogram
constexpr int kCdbSymErrHistBinSize = 6;
constexpr int kCdbSymErrHistMaxOffset = 1;
constexpr int kCdbSymErrHistAvgOffset = 3;
constexpr int kCdbSymErrHistCurOffset = 5;

constexpr int kMaxFecTailRs544 = 15;

// Datapath init/deinit variables
constexpr uint8_t DP_INIT_MAX_MASK = 0x0F;
constexpr uint8_t DP_DINIT_MAX_MASK = 0xF0;
constexpr uint8_t DP_DINIT_BITSHIFT = 4;

// DeInitAllMask
constexpr uint8_t kFullDataPathDeInitMask = 0xFF;

// TODO @sanabani: Change To Map
std::array<std::string, 9> channelConfigErrorMsg = {
    "No status available, config under progress",
    "Config accepted and applied",
    "Config rejected due to unknown reason",
    "Config rejected due to invalid ApSel code request",
    "Config rejected due to ApSel requested on invalid lane combination",
    "Config rejected due to invalid SI control set request",
    "Config rejected due to some lanes currently in use by other application",
    "Config rejected due to incomplete lane info",
    "Config rejected due to other reasons"};

} // namespace

namespace facebook {
namespace fboss {

using namespace facebook::fboss::phy;

// VDM Config pages: 20h (Group 1), 21h (Group 2), 22h (Group 3), 23h (Group 4)
constexpr std::array<CmisField, 4> kVdmConfPages = {
    CmisField::PAGE_UPPER20H,
    CmisField::PAGE_UPPER21H,
    CmisField::PAGE_UPPER22H,
    CmisField::PAGE_UPPER23H};

// VDM Data pages: 24h (Group 1), 25h (Group 2), 26h (Group 3), 27h (Group 4)
constexpr std::array<CmisField, 4> kVdmDataPages = {
    CmisField::PAGE_UPPER24H,
    CmisField::PAGE_UPPER25H,
    CmisField::PAGE_UPPER26H,
    CmisField::PAGE_UPPER27H};

// Datapath init/deinit variables
static const std::unordered_map<uint8_t, uint64_t> DpInitValToTimeMap = {
    {0, 1000}, // Tstate < 1 ms
    {1, 5000}, // 1 ms <= Tstate < 5 ms
    {2, 10000}, // 5 ms <= Tstate < 10 ms
    {3, 50000}, // 10 ms <= Tstate < 50 ms
    {4, 100000}, // 50 ms <= Tstate < 100 ms
    {5, 500000}, // 100 ms <= Tstate < 500 ms
    {6, 1000000}, // 500 ms <= Tstate < 1 s
    {7, 5000000}, // 1 s <= Tstate < 5 s
    {8, 10000000}, // 5 s <= Tstate < 10 s
    {9, 60000000}, // 10 s <= Tstate < 1 min
    {10, 300000000}, // 1 min <= Tstate < 5 min
    {11, 600000000}, // 5 min <= Tstate < 10 min
    {12, 3000000000}, // 10 min <= Tstate < 50 min
};

// As per CMIS4.0
static const QsfpFieldInfo<CmisField, CmisPages>::QsfpFieldMap cmisFields = {
    // Lower Page
    {CmisField::PAGE_LOWER, {CmisPages::LOWER, 0, 128}},
    {CmisField::IDENTIFIER, {CmisPages::LOWER, 0, 1}},
    {CmisField::REVISION_COMPLIANCE, {CmisPages::LOWER, 1, 1}},
    {CmisField::FLAT_MEM, {CmisPages::LOWER, 2, 1}},
    {CmisField::MODULE_STATE, {CmisPages::LOWER, 3, 1}},
    {CmisField::BANK0_FLAGS, {CmisPages::LOWER, 4, 1}},
    {CmisField::BANK1_FLAGS, {CmisPages::LOWER, 5, 1}},
    {CmisField::BANK2_FLAGS, {CmisPages::LOWER, 6, 1}},
    {CmisField::BANK3_FLAGS, {CmisPages::LOWER, 7, 1}},
    {CmisField::MODULE_FLAG, {CmisPages::LOWER, 8, 1}},
    {CmisField::MODULE_ALARMS, {CmisPages::LOWER, 9, 3}},
    {CmisField::TEMPERATURE, {CmisPages::LOWER, 14, 2}},
    {CmisField::VCC, {CmisPages::LOWER, 16, 2}},
    {CmisField::MODULE_CONTROL, {CmisPages::LOWER, 26, 1}},
    {CmisField::FIRMWARE_REVISION, {CmisPages::LOWER, 39, 2}},
    {CmisField::FEC_SAMPLING_PCT, {CmisPages::LOWER, 65, 1}},
    {CmisField::CUSTOM_FLAGS, {CmisPages::LOWER, 67, 1}},
    {CmisField::DSP_TEMP_MARGIN, {CmisPages::LOWER, 68, 1}},
    {CmisField::LASER_TEMP_MARGIN, {CmisPages::LOWER, 69, 1}},
    {CmisField::MAX_BANK_CAPACITY, {CmisPages::LOWER, 70, 1}},
    {CmisField::MEDIA_TYPE_ENCODINGS, {CmisPages::LOWER, 85, 1}},
    {CmisField::APPLICATION_ADVERTISING1, {CmisPages::LOWER, 86, 4}},
    {CmisField::BANK_SELECT, {CmisPages::LOWER, 126, 1}},
    {CmisField::PAGE_SELECT_BYTE, {CmisPages::LOWER, 127, 1}},
    // Page 00h
    {CmisField::PAGE_UPPER00H, {CmisPages::PAGE00, 128, 128}},
    {CmisField::VENDOR_NAME, {CmisPages::PAGE00, 129, 16}},
    {CmisField::VENDOR_OUI, {CmisPages::PAGE00, 145, 3}},
    {CmisField::PART_NUMBER, {CmisPages::PAGE00, 148, 16}},
    {CmisField::REVISION_NUMBER, {CmisPages::PAGE00, 164, 2}},
    {CmisField::VENDOR_SERIAL_NUMBER, {CmisPages::PAGE00, 166, 16}},
    {CmisField::MFG_DATE, {CmisPages::PAGE00, 182, 8}},
    {CmisField::LENGTH_COPPER, {CmisPages::PAGE00, 202, 1}},
    {CmisField::MEDIA_INTERFACE_TECHNOLOGY, {CmisPages::PAGE00, 212, 1}},
    {CmisField::PAGE0_CSUM, {CmisPages::PAGE00, 222, 1}},
    // Page 01h
    {CmisField::PAGE_UPPER01H, {CmisPages::PAGE01, 128, 128}},
    {CmisField::LENGTH_SMF, {CmisPages::PAGE01, 132, 1}},
    {CmisField::LENGTH_OM5, {CmisPages::PAGE01, 133, 1}},
    {CmisField::LENGTH_OM4, {CmisPages::PAGE01, 134, 1}},
    {CmisField::LENGTH_OM3, {CmisPages::PAGE01, 135, 1}},
    {CmisField::LENGTH_OM2, {CmisPages::PAGE01, 136, 1}},
    {CmisField::VDM_DIAG_SUPPORT, {CmisPages::PAGE01, 142, 1}},
    {CmisField::MAX_DPINIT_TIME, {CmisPages::PAGE01, 144, 1}},
    {CmisField::TX_CONTROL_SUPPORT, {CmisPages::PAGE01, 155, 1}},
    {CmisField::RX_CONTROL_SUPPORT, {CmisPages::PAGE01, 156, 1}},
    {CmisField::TX_BIAS_MULTIPLIER, {CmisPages::PAGE01, 160, 1}},
    {CmisField::TX_SIG_INT_CONT_AD, {CmisPages::PAGE01, 161, 1}},
    {CmisField::RX_SIG_INT_CONT_AD, {CmisPages::PAGE01, 162, 1}},
    {CmisField::CDB_SUPPORT, {CmisPages::PAGE01, 163, 1}},
    {CmisField::MEDIA_LANE_ASSIGNMENT, {CmisPages::PAGE01, 176, 15}},
    {CmisField::SUPPORTED_CUSTOM_FEATURES, {CmisPages::PAGE01, 191, 1}},
    {CmisField::DSP_FW_VERSION, {CmisPages::PAGE01, 194, 2}},
    {CmisField::BUILD_REVISION, {CmisPages::PAGE01, 196, 2}},
    {CmisField::APPLICATION_ADVERTISING2, {CmisPages::PAGE01, 223, 4}},
    {CmisField::PAGE1_CSUM, {CmisPages::PAGE01, 255, 1}},
    // Page 02h
    {CmisField::PAGE_UPPER02H, {CmisPages::PAGE02, 128, 128}},
    {CmisField::TEMPERATURE_THRESH, {CmisPages::PAGE02, 128, 8}},
    {CmisField::VCC_THRESH, {CmisPages::PAGE02, 136, 8}},
    {CmisField::TX_PWR_THRESH, {CmisPages::PAGE02, 176, 8}},
    {CmisField::TX_BIAS_THRESH, {CmisPages::PAGE02, 184, 8}},
    {CmisField::RX_PWR_THRESH, {CmisPages::PAGE02, 192, 8}},
    {CmisField::PAGE2_CSUM, {CmisPages::PAGE02, 255, 1}},
    // Page 04h
    {CmisField::PAGE_UPPER04H, {CmisPages::PAGE04, 128, 128}},
    {CmisField::LASER_GRIDS_ADVER, {CmisPages::PAGE04, 128, 1}},
    {CmisField::FINE_TUNING_ADVER, {CmisPages::PAGE04, 129, 1}},
    {CmisField::LASER_3P125_GHZ_LO_CHAN, {CmisPages::PAGE04, 130, 2}},
    {CmisField::LASER_3P125_GHZ_HI_CHAN, {CmisPages::PAGE04, 132, 2}},
    {CmisField::LASER_6P25_GHZ_LO_CHAN, {CmisPages::PAGE04, 134, 2}},
    {CmisField::LASER_6P25_GHZ_HI_CHAN, {CmisPages::PAGE04, 136, 2}},
    {CmisField::LASER_12P5_GHZ_LO_CHAN, {CmisPages::PAGE04, 138, 2}},
    {CmisField::LASER_12P5_GHZ_HI_CHAN, {CmisPages::PAGE04, 140, 2}},
    {CmisField::LASER_25_GHZ_LO_CHAN, {CmisPages::PAGE04, 142, 2}},
    {CmisField::LASER_25_GHZ_HI_CHAN, {CmisPages::PAGE04, 144, 2}},
    {CmisField::LASER_50_GHZ_LO_CHAN, {CmisPages::PAGE04, 146, 2}},
    {CmisField::LASER_50_GHZ_HI_CHAN, {CmisPages::PAGE04, 148, 2}},
    {CmisField::LASER_100_GHZ_LO_CHAN, {CmisPages::PAGE04, 150, 2}},
    {CmisField::LASER_100_GHZ_HI_CHAN, {CmisPages::PAGE04, 152, 2}},
    {CmisField::LASER_33_GHZ_LO_CHAN, {CmisPages::PAGE04, 154, 2}},
    {CmisField::LASER_33_GHZ_HI_CHAN, {CmisPages::PAGE04, 156, 2}},
    {CmisField::LASER_75_GHZ_LO_CHAN, {CmisPages::PAGE04, 158, 2}},
    {CmisField::LASER_75_GHZ_HI_CHAN, {CmisPages::PAGE04, 160, 2}},
    {CmisField::LASER_150_GHZ_LO_CHAN, {CmisPages::PAGE04, 162, 2}},
    {CmisField::LASER_150_GHZ_HI_CHAN, {CmisPages::PAGE04, 164, 2}},
    {CmisField::LASER_FINE_TUNE_RES, {CmisPages::PAGE04, 190, 2}},
    {CmisField::LASER_FINE_TUNE_LO_OFFSET, {CmisPages::PAGE04, 192, 2}},
    {CmisField::LASER_FINE_TUNE_HI_OFFSET, {CmisPages::PAGE04, 194, 2}},
    {CmisField::MEDIA_TX_PROG_OUT_PWR_ADVER, {CmisPages::PAGE04, 196, 1}},
    {CmisField::MEDIA_MIN_TX_PROG_OUT_PWR, {CmisPages::PAGE04, 198, 2}},
    {CmisField::MEDIA_MAX_TX_PROG_OUT_PWR, {CmisPages::PAGE04, 200, 2}},
    {CmisField::PAGE4_CSUM, {CmisPages::PAGE04, 255, 1}},
    // Page 10h
    {CmisField::PAGE_UPPER10H, {CmisPages::PAGE10, 128, 128}},
    {CmisField::DATA_PATH_DEINIT, {CmisPages::PAGE10, 128, 1}},
    {CmisField::TX_POLARITY_FLIP, {CmisPages::PAGE10, 129, 1}},
    {CmisField::TX_DISABLE, {CmisPages::PAGE10, 130, 1}},
    {CmisField::TX_SQUELCH_DISABLE, {CmisPages::PAGE10, 131, 1}},
    {CmisField::TX_FORCE_SQUELCH, {CmisPages::PAGE10, 132, 1}},
    {CmisField::TX_ADAPTATION_FREEZE, {CmisPages::PAGE10, 134, 1}},
    {CmisField::TX_ADAPTATION_STORE, {CmisPages::PAGE10, 135, 2}},
    {CmisField::RX_POLARITY_FLIP, {CmisPages::PAGE10, 137, 1}},
    {CmisField::RX_DISABLE, {CmisPages::PAGE10, 138, 1}},
    {CmisField::RX_SQUELCH_DISABLE, {CmisPages::PAGE10, 139, 1}},
    {CmisField::STAGE_CTRL_SET_0, {CmisPages::PAGE10, 143, 1}},
    {CmisField::STAGE_CTRL_SET0_IMMEDIATE, {CmisPages::PAGE10, 144, 1}},
    {CmisField::APP_SEL_LANE_1_8, {CmisPages::PAGE10, 145, 8}},
    {CmisField::APP_SEL_LANE_1_2, {CmisPages::PAGE10, 145, 2}},
    {CmisField::APP_SEL_LANE_3_4, {CmisPages::PAGE10, 147, 2}},
    {CmisField::APP_SEL_LANE_5_6, {CmisPages::PAGE10, 149, 2}},
    {CmisField::APP_SEL_LANE_7_8, {CmisPages::PAGE10, 151, 2}},
    {CmisField::APP_SEL_LANE_1_4, {CmisPages::PAGE10, 145, 4}},
    {CmisField::APP_SEL_LANE_5_8, {CmisPages::PAGE10, 149, 4}},
    {CmisField::APP_SEL_LANE_1, {CmisPages::PAGE10, 145, 1}},
    {CmisField::APP_SEL_LANE_2, {CmisPages::PAGE10, 146, 1}},
    {CmisField::APP_SEL_LANE_3, {CmisPages::PAGE10, 147, 1}},
    {CmisField::APP_SEL_LANE_4, {CmisPages::PAGE10, 148, 1}},
    {CmisField::APP_SEL_LANE_5, {CmisPages::PAGE10, 149, 1}},
    {CmisField::APP_SEL_LANE_6, {CmisPages::PAGE10, 150, 1}},
    {CmisField::APP_SEL_LANE_7, {CmisPages::PAGE10, 151, 1}},
    {CmisField::APP_SEL_LANE_8, {CmisPages::PAGE10, 152, 1}},
    {CmisField::INPUT_EQ_TX_1_2, {CmisPages::PAGE10, 156, 1}},
    {CmisField::INPUT_EQ_TX_3_4, {CmisPages::PAGE10, 157, 1}},
    {CmisField::INPUT_EQ_TX_5_6, {CmisPages::PAGE10, 158, 1}},
    {CmisField::INPUT_EQ_TX_7_8, {CmisPages::PAGE10, 159, 1}},
    {CmisField::RX_CONTROL_PRE_CURSOR, {CmisPages::PAGE10, 162, 4}},
    {CmisField::RX_CONTROL_PRE_CURSOR_LANE_01, {CmisPages::PAGE10, 162, 1}},
    {CmisField::RX_CONTROL_PRE_CURSOR_LANE_23, {CmisPages::PAGE10, 163, 1}},
    {CmisField::RX_CONTROL_PRE_CURSOR_LANE_45, {CmisPages::PAGE10, 164, 1}},
    {CmisField::RX_CONTROL_PRE_CURSOR_LANE_67, {CmisPages::PAGE10, 165, 1}},
    {CmisField::RX_CONTROL_POST_CURSOR, {CmisPages::PAGE10, 166, 4}},
    {CmisField::RX_CONTROL_POST_CURSOR_LANE_01, {CmisPages::PAGE10, 166, 1}},
    {CmisField::RX_CONTROL_POST_CURSOR_LANE_23, {CmisPages::PAGE10, 167, 1}},
    {CmisField::RX_CONTROL_POST_CURSOR_LANE_45, {CmisPages::PAGE10, 168, 1}},
    {CmisField::RX_CONTROL_POST_CURSOR_LANE_67, {CmisPages::PAGE10, 169, 1}},
    {CmisField::RX_CONTROL_MAIN, {CmisPages::PAGE10, 170, 4}},
    {CmisField::RX_CONTROL_MAIN_LANE_01, {CmisPages::PAGE10, 170, 1}},
    {CmisField::RX_CONTROL_MAIN_LANE_23, {CmisPages::PAGE10, 171, 1}},
    {CmisField::RX_CONTROL_MAIN_LANE_45, {CmisPages::PAGE10, 172, 1}},
    {CmisField::RX_CONTROL_MAIN_LANE_67, {CmisPages::PAGE10, 173, 1}},
    // Page 11h
    {CmisField::PAGE_UPPER11H, {CmisPages::PAGE11, 128, 128}},
    {CmisField::DATA_PATH_STATE, {CmisPages::PAGE11, 128, 4}},
    {CmisField::TX_FAULT_FLAG, {CmisPages::PAGE11, 135, 1}},
    {CmisField::TX_LOS_FLAG, {CmisPages::PAGE11, 136, 1}},
    {CmisField::TX_LOL_FLAG, {CmisPages::PAGE11, 137, 1}},
    {CmisField::TX_EQ_FLAG, {CmisPages::PAGE11, 138, 1}},
    {CmisField::TX_PWR_FLAG, {CmisPages::PAGE11, 139, 4}},
    {CmisField::TX_BIAS_FLAG, {CmisPages::PAGE11, 143, 4}},
    {CmisField::RX_LOS_FLAG, {CmisPages::PAGE11, 147, 1}},
    {CmisField::RX_LOL_FLAG, {CmisPages::PAGE11, 148, 1}},
    {CmisField::RX_PWR_FLAG, {CmisPages::PAGE11, 149, 4}},
    {CmisField::CHANNEL_TX_PWR, {CmisPages::PAGE11, 154, 16}},
    {CmisField::CHANNEL_TX_BIAS, {CmisPages::PAGE11, 170, 16}},
    {CmisField::CHANNEL_RX_PWR, {CmisPages::PAGE11, 186, 16}},
    {CmisField::CONFIG_ERROR_LANES, {CmisPages::PAGE11, 202, 4}},
    {CmisField::ACTIVE_CTRL_ALL_LANES, {CmisPages::PAGE11, 206, 8}},
    {CmisField::ACTIVE_CTRL_LANE_1, {CmisPages::PAGE11, 206, 1}},
    {CmisField::ACTIVE_CTRL_LANE_2, {CmisPages::PAGE11, 207, 1}},
    {CmisField::ACTIVE_CTRL_LANE_3, {CmisPages::PAGE11, 208, 1}},
    {CmisField::ACTIVE_CTRL_LANE_4, {CmisPages::PAGE11, 209, 1}},
    {CmisField::ACTIVE_CTRL_LANE_5, {CmisPages::PAGE11, 210, 1}},
    {CmisField::ACTIVE_CTRL_LANE_6, {CmisPages::PAGE11, 211, 1}},
    {CmisField::ACTIVE_CTRL_LANE_7, {CmisPages::PAGE11, 212, 1}},
    {CmisField::ACTIVE_CTRL_LANE_8, {CmisPages::PAGE11, 213, 1}},
    {CmisField::TX_CDR_CONTROL, {CmisPages::PAGE11, 221, 1}},
    {CmisField::RX_CDR_CONTROL, {CmisPages::PAGE11, 222, 1}},
    {CmisField::RX_OUT_PRE_CURSOR, {CmisPages::PAGE11, 223, 4}},
    {CmisField::RX_OUT_POST_CURSOR, {CmisPages::PAGE11, 227, 4}},
    {CmisField::RX_OUT_MAIN, {CmisPages::PAGE11, 231, 4}},
    // Page 12h
    {CmisField::PAGE_UPPER12H, {CmisPages::PAGE12, 128, 128}},
    {CmisField::MEDIA_TX_1_GRID_AND_FINE_TUNE_ENA, {CmisPages::PAGE12, 128, 1}},
    {CmisField::MEDIA_TX_1_CHAN_NBR_SEL, {CmisPages::PAGE12, 136, 2}},
    {CmisField::MEDIA_TX_1_FINE_TUNE_FREQ_OFFSET, {CmisPages::PAGE12, 152, 2}},
    {CmisField::MEDIA_TX_1_CURR_LAS_FREQ, {CmisPages::PAGE12, 168, 4}},
    {CmisField::MEDIA_TX_1_TGT_OUTPUT_PWR, {CmisPages::PAGE12, 200, 2}},
    {CmisField::MEDIA_TX_1_LAS_STAT, {CmisPages::PAGE12, 222, 1}},
    {CmisField::MEDIA_TX_LAS_TUNE_SUM, {CmisPages::PAGE12, 230, 1}},
    {CmisField::MEDIA_TX_1_LAS_STAT_FLAGS, {CmisPages::PAGE12, 231, 1}},
    {CmisField::MEDIA_TX_1_LAS_STAT_MASKS, {CmisPages::PAGE12, 239, 1}},
    // Page 13h
    {CmisField::PAGE_UPPER13H, {CmisPages::PAGE13, 128, 128}},
    {CmisField::LOOPBACK_CAPABILITY, {CmisPages::PAGE13, 128, 1}},
    {CmisField::PATTERN_CAPABILITY, {CmisPages::PAGE13, 129, 1}},
    {CmisField::DIAGNOSTIC_CAPABILITY, {CmisPages::PAGE13, 130, 1}},
    {CmisField::PATTERN_CHECKER_CAPABILITY, {CmisPages::PAGE13, 131, 1}},
    {CmisField::HOST_SUPPORTED_GENERATOR_PATTERNS, {CmisPages::PAGE13, 132, 2}},
    {CmisField::MEDIA_SUPPORTED_GENERATOR_PATTERNS,
     {CmisPages::PAGE13, 134, 2}},
    {CmisField::HOST_SUPPORTED_CHECKER_PATTERNS, {CmisPages::PAGE13, 136, 2}},
    {CmisField::MEDIA_SUPPORTED_CHECKER_PATTERNS, {CmisPages::PAGE13, 138, 2}},
    {CmisField::HOST_GEN_ENABLE, {CmisPages::PAGE13, 144, 1}},
    {CmisField::HOST_GEN_INV, {CmisPages::PAGE13, 145, 1}},
    {CmisField::HOST_GEN_PRE_FEC, {CmisPages::PAGE13, 147, 1}},
    {CmisField::HOST_PATTERN_SELECT_LANE_2_1, {CmisPages::PAGE13, 148, 1}},
    {CmisField::HOST_PATTERN_SELECT_LANE_4_3, {CmisPages::PAGE13, 149, 1}},
    {CmisField::HOST_PATTERN_SELECT_LANE_6_5, {CmisPages::PAGE13, 150, 1}},
    {CmisField::HOST_PATTERN_SELECT_LANE_8_7, {CmisPages::PAGE13, 151, 1}},
    {CmisField::MEDIA_GEN_ENABLE, {CmisPages::PAGE13, 152, 1}},
    {CmisField::MEDIA_GEN_INV, {CmisPages::PAGE13, 153, 1}},
    {CmisField::MEDIA_GEN_PRE_FEC, {CmisPages::PAGE13, 155, 1}},
    {CmisField::MEDIA_PATTERN_SELECT_LANE_2_1, {CmisPages::PAGE13, 156, 1}},
    {CmisField::MEDIA_PATTERN_SELECT_LANE_4_3, {CmisPages::PAGE13, 157, 1}},
    {CmisField::MEDIA_PATTERN_SELECT_LANE_6_5, {CmisPages::PAGE13, 158, 1}},
    {CmisField::MEDIA_PATTERN_SELECT_LANE_8_7, {CmisPages::PAGE13, 159, 1}},
    {CmisField::HOST_CHECKER_ENABLE, {CmisPages::PAGE13, 160, 1}},
    {CmisField::HOST_CHECKER_INV, {CmisPages::PAGE13, 161, 1}},
    {CmisField::HOST_CHECKER_POST_FEC, {CmisPages::PAGE13, 163, 1}},
    {CmisField::HOST_CHECKER_PATTERN_SELECT_LANE_2_1,
     {CmisPages::PAGE13, 164, 1}},
    {CmisField::HOST_CHECKER_PATTERN_SELECT_LANE_4_3,
     {CmisPages::PAGE13, 165, 1}},
    {CmisField::HOST_CHECKER_PATTERN_SELECT_LANE_6_5,
     {CmisPages::PAGE13, 166, 1}},
    {CmisField::HOST_CHECKER_PATTERN_SELECT_LANE_8_7,
     {CmisPages::PAGE13, 167, 1}},
    {CmisField::MEDIA_CHECKER_ENABLE, {CmisPages::PAGE13, 168, 1}},
    {CmisField::MEDIA_CHECKER_INV, {CmisPages::PAGE13, 169, 1}},
    {CmisField::MEDIA_CHECKER_POST_FEC, {CmisPages::PAGE13, 171, 1}},
    {CmisField::MEDIA_CHECKER_PATTERN_SELECT_LANE_2_1,
     {CmisPages::PAGE13, 172, 1}},
    {CmisField::MEDIA_CHECKER_PATTERN_SELECT_LANE_4_3,
     {CmisPages::PAGE13, 173, 1}},
    {CmisField::MEDIA_CHECKER_PATTERN_SELECT_LANE_6_5,
     {CmisPages::PAGE13, 174, 1}},
    {CmisField::MEDIA_CHECKER_PATTERN_SELECT_LANE_8_7,
     {CmisPages::PAGE13, 175, 1}},
    {CmisField::REF_CLK_CTRL, {CmisPages::PAGE13, 176, 1}},
    {CmisField::BER_CTRL, {CmisPages::PAGE13, 177, 1}},
    {CmisField::MEDIA_OUTPUT_LB_EN, {CmisPages::PAGE13, 180, 1}},
    {CmisField::MEDIA_INPUT_LB_EN, {CmisPages::PAGE13, 181, 1}},
    {CmisField::HOST_OUTPUT_LB_EN, {CmisPages::PAGE13, 182, 1}},
    {CmisField::HOST_INPUT_LB_EN, {CmisPages::PAGE13, 183, 1}},
    {CmisField::REF_CLK_LOSS, {CmisPages::PAGE13, 206, 1}},
    {CmisField::HOST_CHECKER_GATING_COMPLETE, {CmisPages::PAGE13, 208, 1}},
    {CmisField::MEDIA_CHECKER_GATING_COMPLETE, {CmisPages::PAGE13, 209, 1}},
    {CmisField::HOST_PPG_LOL, {CmisPages::PAGE13, 210, 1}},
    {CmisField::MEDIA_PPG_LOL, {CmisPages::PAGE13, 211, 1}},
    {CmisField::HOST_BERT_LOL, {CmisPages::PAGE13, 212, 1}},
    {CmisField::MEDIA_BERT_LOL, {CmisPages::PAGE13, 213, 1}},
    // Page 14h
    {CmisField::PAGE_UPPER14H, {CmisPages::PAGE14, 128, 128}},
    {CmisField::DIAG_SEL, {CmisPages::PAGE14, 128, 1}},
    {CmisField::HOST_MODE_MISMATCH, {CmisPages::PAGE14, 130, 1}},
    {CmisField::MEDIA_MODE_MISMATCH, {CmisPages::PAGE14, 131, 1}},
    {CmisField::HOST_LANE_GENERATOR_LOL_LATCH, {CmisPages::PAGE14, 136, 1}},
    {CmisField::MEDIA_LANE_GENERATOR_LOL_LATCH, {CmisPages::PAGE14, 137, 1}},
    {CmisField::HOST_LANE_CHECKER_LOL_LATCH, {CmisPages::PAGE14, 138, 1}},
    {CmisField::MEDIA_LANE_CHECKER_LOL_LATCH, {CmisPages::PAGE14, 139, 1}},
    {CmisField::HOST_BER, {CmisPages::PAGE14, 192, 16}},
    {CmisField::MEDIA_BER_HOST_SNR, {CmisPages::PAGE14, 208, 16}},
    {CmisField::MEDIA_SNR, {CmisPages::PAGE14, 240, 16}},
    // Page 20h
    {CmisField::PAGE_UPPER20H, {CmisPages::PAGE20, 128, 128}},
    // Page 21h
    {CmisField::PAGE_UPPER21H, {CmisPages::PAGE21, 128, 128}},
    // Page 22h
    {CmisField::PAGE_UPPER22H, {CmisPages::PAGE22, 128, 128}},
    // Page 23h
    {CmisField::PAGE_UPPER23H, {CmisPages::PAGE23, 128, 128}},
    // Page 24h
    {CmisField::PAGE_UPPER24H, {CmisPages::PAGE24, 128, 128}},
    // Page 25h
    {CmisField::PAGE_UPPER25H, {CmisPages::PAGE25, 128, 128}},
    // Page 26h
    {CmisField::PAGE_UPPER26H, {CmisPages::PAGE26, 128, 128}},
    // Page 27h
    {CmisField::PAGE_UPPER27H, {CmisPages::PAGE27, 128, 128}},
    // Page 2Ch
    {CmisField::PAGE_UPPER2CH, {CmisPages::PAGE2C, 128, 128}},
    {CmisField::PAM4_MPI_ALARMS, {CmisPages::PAGE2C, 208, 4}},
    // Page 2Fh
    {CmisField::PAGE_UPPER2FH, {CmisPages::PAGE2F, 128, 128}},
    {CmisField::VDM_GROUPS_SUPPORT, {CmisPages::PAGE2F, 128, 1}},
    {CmisField::VDM_LATCH_REQUEST, {CmisPages::PAGE2F, 144, 1}},
    {CmisField::VDM_LATCH_DONE, {CmisPages::PAGE2F, 145, 1}},
    // Page 34h - Lane FEC Performance Monitoring (C-CMIS)
    {CmisField::PAGE_UPPER34H, {CmisPages::PAGE34, 128, 128}},
    // Page 35h - Lane Link Performance Monitoring (C-CMIS)
    {CmisField::PAGE_UPPER35H, {CmisPages::PAGE35, 128, 128}},
    // Page 38h - Data Path Host Interface Configuration
    {CmisField::PAGE_UPPER38H, {CmisPages::PAGE38, 128, 128}},
    // Page 38h, Byte 137 - Consequent Action control
    // Bits 7-4 (rxConsAct), Bits 3-0 (txConsAct)
    {CmisField::CONS_ACT_CONTROL, {CmisPages::PAGE38, 137, 1}},
    // Page 38h, Bytes 141-142 - Rx Consequent Action Hold-off Timer
    {CmisField::CONS_ACT_HOLD_OFF_TMR, {CmisPages::PAGE38, 141, 2}},
    // Page 45h - Host Lane Provisioning Advertisement
    {CmisField::PAGE_UPPER45H, {CmisPages::PAGE45, 128, 128}},
    // Page 45h, Byte 129 - Host Lane Provisioning Advertisement
    {CmisField::HOST_LANE_PROV_AD, {CmisPages::PAGE45, 129, 1}},
    // Page C0h, Byte 131 - Host Lane Ethernet Control
    {CmisField::HOST_LANE_ETH_CTRL, {CmisPages::PAGEC0, 131, 1}},
};

CmisField laneToAppSelField(const std::set<uint8_t>& lanes) {
  const std::map<std::set<uint8_t>, CmisField> kLanesToCmisField = {
      {{0}, CmisField::APP_SEL_LANE_1},
      {{1}, CmisField::APP_SEL_LANE_2},
      {{2}, CmisField::APP_SEL_LANE_3},
      {{3}, CmisField::APP_SEL_LANE_4},
      {{4}, CmisField::APP_SEL_LANE_5},
      {{5}, CmisField::APP_SEL_LANE_6},
      {{6}, CmisField::APP_SEL_LANE_7},
      {{7}, CmisField::APP_SEL_LANE_8},
      {{0, 1}, CmisField::APP_SEL_LANE_1_2},
      {{2, 3}, CmisField::APP_SEL_LANE_3_4},
      {{4, 5}, CmisField::APP_SEL_LANE_5_6},
      {{6, 7}, CmisField::APP_SEL_LANE_7_8},
      {{0, 1, 2, 3}, CmisField::APP_SEL_LANE_1_4},
      {{4, 5, 6, 7}, CmisField::APP_SEL_LANE_5_8},
      {{0, 1, 2, 3, 4, 5, 6, 7}, CmisField::APP_SEL_LANE_1_8},
  };
  if (const auto& it = kLanesToCmisField.find(lanes);
      it != kLanesToCmisField.end()) {
    return it->second;
  }
  throw FbossError("Can't find app sel for lanes ", folly::join(",", lanes));
}

static std::unordered_map<int, CmisField> laneToActiveCtrlField = {
    {0, CmisField::ACTIVE_CTRL_LANE_1},
    {1, CmisField::ACTIVE_CTRL_LANE_2},
    {2, CmisField::ACTIVE_CTRL_LANE_3},
    {3, CmisField::ACTIVE_CTRL_LANE_4},
    {4, CmisField::ACTIVE_CTRL_LANE_5},
    {5, CmisField::ACTIVE_CTRL_LANE_6},
    {6, CmisField::ACTIVE_CTRL_LANE_7},
    {7, CmisField::ACTIVE_CTRL_LANE_8},
};

static CmisFieldMultiplier qsfpMultiplier = {
    {CmisField::LENGTH_SMF, 100},
    {CmisField::LENGTH_OM5, 2},
    {CmisField::LENGTH_OM4, 2},
    {CmisField::LENGTH_OM3, 2},
    {CmisField::LENGTH_OM2, 1},
    {CmisField::LENGTH_COPPER, 0.1},
};

static const std::unordered_map<int, std::pair<uint8_t, CmisField>>
    laneToInputEqTxField = {
        {0, {0, CmisField::INPUT_EQ_TX_1_2}},
        {1, {4, CmisField::INPUT_EQ_TX_1_2}},
        {2, {0, CmisField::INPUT_EQ_TX_3_4}},
        {3, {4, CmisField::INPUT_EQ_TX_3_4}},
        {4, {0, CmisField::INPUT_EQ_TX_5_6}},
        {5, {4, CmisField::INPUT_EQ_TX_5_6}},
        {6, {0, CmisField::INPUT_EQ_TX_7_8}},
        {7, {4, CmisField::INPUT_EQ_TX_7_8}},
};

// A map of programmable FEC sampling pct per Module Media type.
static const std::unordered_map<MediaInterfaceCode, uint8_t>
    kMaxProgFecSamplingSupportedMap_ = {
        {MediaInterfaceCode::FR4_2x400G, 20},
};

constexpr uint8_t kPage0CsumRangeStart = 128;
constexpr uint8_t kPage0CsumRangeLength = 94;
constexpr uint8_t kPage1CsumRangeStart = 130;
constexpr uint8_t kPage1CsumRangeLength = 125;
constexpr uint8_t kPage2CsumRangeStart = 128;
constexpr uint8_t kPage2CsumRangeLength = 127;

struct checksumInfoStruct {
  uint8_t checksumRangeStartOffset;
  uint8_t checksumRangeLength;
  CmisField checksumValOffset;
};

static std::map<CmisPages, checksumInfoStruct> checksumInfoCmis = {
    {CmisPages::PAGE00,
     {kPage0CsumRangeStart, kPage0CsumRangeLength, CmisField::PAGE0_CSUM}},
    {CmisPages::PAGE01,
     {kPage1CsumRangeStart, kPage1CsumRangeLength, CmisField::PAGE1_CSUM}},
    {CmisPages::PAGE02,
     {kPage2CsumRangeStart, kPage2CsumRangeLength, CmisField::PAGE2_CSUM}},
};

// Bidirectional map for storing the mapping of prbs polynomial to patternID in
// the spec
using PrbsMap = boost::bimap<prbs::PrbsPolynomial, uint32_t>;
// clang-format off
const PrbsMap prbsPatternMap = boost::assign::list_of<PrbsMap::relation>(
  prbs::PrbsPolynomial::PRBS7, 11)(
  prbs::PrbsPolynomial::PRBS9, 9)(
  prbs::PrbsPolynomial::PRBS13, 7)(
  prbs::PrbsPolynomial::PRBS15, 5)(
  prbs::PrbsPolynomial::PRBS23, 3)(
  prbs::PrbsPolynomial::PRBS31, 1)(
  prbs::PrbsPolynomial::PRBS7Q, 10)(
  prbs::PrbsPolynomial::PRBS9Q, 8)(
  prbs::PrbsPolynomial::PRBS13Q, 6)(
  prbs::PrbsPolynomial::PRBS15Q, 4)(
  prbs::PrbsPolynomial::PRBS23Q, 2)(
  prbs::PrbsPolynomial::PRBS31Q, 0)(
  prbs::PrbsPolynomial::PRBSSSPRQ, 12);
// clang-format on

std::array<CmisField, 4> prbsGenMediaPatternFields = {
    CmisField::MEDIA_PATTERN_SELECT_LANE_2_1,
    CmisField::MEDIA_PATTERN_SELECT_LANE_4_3,
    CmisField::MEDIA_PATTERN_SELECT_LANE_6_5,
    CmisField::MEDIA_PATTERN_SELECT_LANE_8_7};
std::array<CmisField, 4> prbsGenHostPatternFields = {
    CmisField::HOST_PATTERN_SELECT_LANE_2_1,
    CmisField::HOST_PATTERN_SELECT_LANE_4_3,
    CmisField::HOST_PATTERN_SELECT_LANE_6_5,
    CmisField::HOST_PATTERN_SELECT_LANE_8_7};

std::array<CmisField, 4> prbsChkMediaPatternFields = {
    CmisField::MEDIA_CHECKER_PATTERN_SELECT_LANE_2_1,
    CmisField::MEDIA_CHECKER_PATTERN_SELECT_LANE_4_3,
    CmisField::MEDIA_CHECKER_PATTERN_SELECT_LANE_6_5,
    CmisField::MEDIA_CHECKER_PATTERN_SELECT_LANE_8_7,
};
std::array<CmisField, 4> prbsChkHostPatternFields = {
    CmisField::HOST_CHECKER_PATTERN_SELECT_LANE_2_1,
    CmisField::HOST_CHECKER_PATTERN_SELECT_LANE_4_3,
    CmisField::HOST_CHECKER_PATTERN_SELECT_LANE_6_5,
    CmisField::HOST_CHECKER_PATTERN_SELECT_LANE_8_7,
};

// Each of the configuration byte controls 2 lanes. There are 4 bytes each for
// pre-cursor, post-cursor and main amplitude. In this map, key is 0-3 (one of
// the 4 bytes), and the value is a list of CmisField for settings corresponding
// to that index
std::map<int, std::vector<CmisField>> offsetIndexToCmisField = {
    {0,
     {CmisField::RX_CONTROL_PRE_CURSOR_LANE_01,
      CmisField::RX_CONTROL_POST_CURSOR_LANE_01,
      CmisField::RX_CONTROL_MAIN_LANE_01}},
    {1,
     {CmisField::RX_CONTROL_PRE_CURSOR_LANE_23,
      CmisField::RX_CONTROL_POST_CURSOR_LANE_23,
      CmisField::RX_CONTROL_MAIN_LANE_23}},
    {2,
     {CmisField::RX_CONTROL_PRE_CURSOR_LANE_45,
      CmisField::RX_CONTROL_POST_CURSOR_LANE_45,
      CmisField::RX_CONTROL_MAIN_LANE_45}},
    {3,
     {CmisField::RX_CONTROL_PRE_CURSOR_LANE_67,
      CmisField::RX_CONTROL_POST_CURSOR_LANE_67,
      CmisField::RX_CONTROL_MAIN_LANE_67}},
};

void getQsfpFieldAddress(
    CmisField field,
    int& dataAddress,
    int& offset,
    int& length) {
  auto info = QsfpFieldInfo<CmisField, CmisPages>::getQsfpFieldAddress(
      cmisFields, field);
  dataAddress = info.dataAddress;
  offset = info.offset;
  length = info.length;
}

bool isValidVdmConfigType(int vdmConf) {
  if (vdmConf == static_cast<int>(SNR_MEDIA_IN) ||
      vdmConf == static_cast<int>(SNR_HOST_IN) ||
      vdmConf == static_cast<int>(PAM4_LTP_MEDIA_IN) ||
      vdmConf == static_cast<int>(PRE_FEC_BER_MEDIA_IN_MIN) ||
      vdmConf == static_cast<int>(PRE_FEC_BER_HOST_IN_MIN) ||
      vdmConf == static_cast<int>(PRE_FEC_BER_MEDIA_IN_MAX) ||
      vdmConf == static_cast<int>(PRE_FEC_BER_HOST_IN_MAX) ||
      vdmConf == static_cast<int>(PRE_FEC_BER_MEDIA_IN_AVG) ||
      vdmConf == static_cast<int>(PRE_FEC_BER_HOST_IN_AVG) ||
      vdmConf == static_cast<int>(PRE_FEC_BER_MEDIA_IN_CUR) ||
      vdmConf == static_cast<int>(PRE_FEC_BER_HOST_IN_CUR) ||
      vdmConf == static_cast<int>(ERR_FRAME_MEDIA_IN_MIN) ||
      vdmConf == static_cast<int>(ERR_FRAME_HOST_IN_MIN) ||
      vdmConf == static_cast<int>(ERR_FRAME_MEDIA_IN_MAX) ||
      vdmConf == static_cast<int>(ERR_FRAME_HOST_IN_MAX) ||
      vdmConf == static_cast<int>(ERR_FRAME_MEDIA_IN_AVG) ||
      vdmConf == static_cast<int>(ERR_FRAME_HOST_IN_AVG) ||
      vdmConf == static_cast<int>(ERR_FRAME_MEDIA_IN_CUR) ||
      vdmConf == static_cast<int>(ERR_FRAME_HOST_IN_CUR) ||
      vdmConf == static_cast<int>(PAM4_LEVEL0_STANDARD_DEVIATION_LINE) ||
      vdmConf == static_cast<int>(PAM4_LEVEL1_STANDARD_DEVIATION_LINE) ||
      vdmConf == static_cast<int>(PAM4_LEVEL2_STANDARD_DEVIATION_LINE) ||
      vdmConf == static_cast<int>(PAM4_LEVEL3_STANDARD_DEVIATION_LINE) ||
      vdmConf == static_cast<int>(PAM4_MPI_LINE) ||
      vdmConf == static_cast<int>(FEC_TAIL_MEDIA_IN_MAX) ||
      vdmConf == static_cast<int>(FEC_TAIL_MEDIA_IN_CURR) ||
      vdmConf == static_cast<int>(FEC_TAIL_HOST_IN_MAX) ||
      vdmConf == static_cast<int>(FEC_TAIL_HOST_IN_CURR) ||
      vdmConf == static_cast<int>(MODULATOR_BIAS_XI) ||
      vdmConf == static_cast<int>(MODULATOR_BIAS_XQ) ||
      vdmConf == static_cast<int>(MODULATOR_BIAS_YI) ||
      vdmConf == static_cast<int>(MODULATOR_BIAS_YQ) ||
      vdmConf == static_cast<int>(MODULATOR_BIAS_X_PHASE) ||
      vdmConf == static_cast<int>(MODULATOR_BIAS_Y_PHASE) ||
      vdmConf == static_cast<int>(CD_LOW_GRANULARITY) ||
      vdmConf == static_cast<int>(SOPMD_LOW_GRANULARITY) ||
      vdmConf == static_cast<int>(CD_HIGH_GRANULARITY) ||
      vdmConf == static_cast<int>(DGD) ||
      vdmConf == static_cast<int>(SOPMD_HIGH_GRANULARITY) ||
      vdmConf == static_cast<int>(PDL) || vdmConf == static_cast<int>(OSNR) ||
      vdmConf == static_cast<int>(ESNR) || vdmConf == static_cast<int>(CFO) ||
      vdmConf == static_cast<int>(EVM) ||
      vdmConf == static_cast<int>(TX_POWER) ||
      vdmConf == static_cast<int>(RX_TOTAL_POWER) ||
      vdmConf == static_cast<int>(RX_SIGNAL_POWER) ||
      vdmConf == static_cast<int>(SOP_ROC) ||
      vdmConf == static_cast<int>(MER) ||
      vdmConf == static_cast<int>(CLOCK_RECOVERY_LOOP) ||
      vdmConf == static_cast<int>(SNR_MARGIN) ||
      vdmConf == static_cast<int>(Q_FACTOR) ||
      vdmConf == static_cast<int>(Q_MARGIN)) {
    return true;
  }
  return false;
}

std::optional<CmisModule::ApplicationAdvertisingField>
CmisModule::getApplicationField(uint8_t application, uint8_t startHostLane)
    const {
  // Module capabilities advertise per-bank (intra-bank) host start lanes. A
  // port's lanes are confined to one bank, so match against the intra-bank
  // position (identity for single-bank modules / lanes 0-7).
  uint8_t intraStartHostLane = laneInBank(startHostLane);
  for (const auto& capability : moduleCapabilities_) {
    if (capability.moduleMediaInterface == application &&
        std::find(
            capability.hostStartLanes.begin(),
            capability.hostStartLanes.end(),
            intraStartHostLane) != capability.hostStartLanes.end()) {
      return capability;
    }
  }
  return std::nullopt;
}

CmisModule::CmisModule(
    std::set<std::string> portNames,
    TransceiverImpl* qsfpImpl,
    std::shared_ptr<const TransceiverConfig> cfg,
    bool supportRemediate,
    std::string tcvrName)
    : QsfpModule(
          std::move(portNames),
          qsfpImpl,
          std::move(tcvrName),
          std::move(cfg)),
      supportRemediate_(supportRemediate) {}

CmisModule::~CmisModule() {}

namespace {
bool isBankedPage(CmisPages page) {
  // Module-level pages (lower, 00h, 01h, 02h, 04h) describe the whole module
  // and are not banked. All per-lane/per-datapath pages are banked.
  return page != CmisPages::LOWER && page != CmisPages::PAGE00 &&
      page != CmisPages::PAGE01 && page != CmisPages::PAGE02 &&
      page != CmisPages::PAGE04;
}

/* Decode ModuleState out of Lower Page 00h byte 3. The state is bits 1-3;
 * bits 4-7 are reserved and some modules do populate them, so they have to be
 * masked off rather than merely shifted past. Every reader of byte 3 must go
 * through here - this decode used to be open-coded per call site, and the
 * copies drifted apart. */
CmisModuleState moduleStateFromStatusByte(uint8_t statusByte) {
  return static_cast<CmisModuleState>(
      (statusByte & MODULE_STATUS_MASK) >> MODULE_STATUS_BITSHIFT);
}
} // namespace

void CmisModule::cacheMaxNumBanks() {
  // The max bank capacity register (Lower Page 00h byte 70) is only defined for
  // co-packaged optics (CPO). On other modules that byte can carry unrelated
  // data, so treat every non-CPO module as single-bank.
  if (getIdentifier() != TransceiverModuleIdentifier::CPO) {
    maxNumBanks_ = 1;
  } else {
    // For CPO the register holds the max CMIS bank count directly (e.g. 4 for a
    // 32-lane module); fall back to a single bank if it reads 0.
    uint8_t banks = getSettingsValue(CmisField::MAX_BANK_CAPACITY);
    maxNumBanks_ = banks ? banks : 1;
  }
  // Size the per-bank page buffers once, now that the bank count is known, and
  // zero-initialize them. The per-refresh reads then index these directly,
  // without reallocating, and any allocation failure surfaces here (once)
  // rather than on every read.
  uint8_t numBanks = getMaxNumBanks();
  page10_.assign(numBanks, {});
  page11_.assign(numBanks, {});
  page13_.assign(numBanks, {});
  page14_.assign(numBanks, {});
  page24_.assign(numBanks, {});
  page25_.assign(numBanks, {});
  page26_.assign(numBanks, {});
  page27_.assign(numBanks, {});
}

bool CmisModule::hasInvalidBankSelect() const {
  uint8_t bankSelect = getSettingsValue(CmisField::BANK_SELECT);
  if (bankSelect < getMaxNumBanks()) {
    return false;
  }
  QSFP_LOG(ERR, this) << fmt::format(
      "Bank select register holds {} but the module only has {} bank(s)",
      bankSelect,
      getMaxNumBanks());
  return true;
}

void CmisModule::cacheCmisRevision() {
  uint8_t revision = getSettingsValue(CmisField::REVISION_COMPLIANCE);
  cmisRevision_ = std::make_pair(
      static_cast<uint8_t>(revision >> 4),
      static_cast<uint8_t>(revision & 0xf));
}

void CmisModule::selectPageAndBank(int dataPage, std::optional<uint8_t> bank) {
  auto page = static_cast<CmisPages>(dataPage);
  if (page == CmisPages::LOWER || flatMem_) {
    return;
  }
  // The conditions for driving the bank-select register are broken out (rather
  // than &&'d together) so each case is explicit and easy to attribute when
  // debugging.
  if (bank.has_value()) {
    if (!isBankedPage(page)) {
      // A bank only selects a sub-region of a banked page; supplying one for a
      // non-banked (module-level) page is a caller bug, so fail loudly rather
      // than silently ignoring it.
      throw FbossError(
          fmt::format(
              "Bank {} supplied for non-banked page {:#x}", *bank, dataPage));
    } else if (getMaxNumBanks() > 1) {
      // Multi-bank (CPO): drive the (sticky) bank-select register.
      uint8_t bankVal = *bank;
      qsfpImpl_->writeTransceiver(
          {TransceiverAccessParameter::ADDR_QSFP,
           kBankSelectByteOffset,
           sizeof(bankVal),
           static_cast<int>(CmisPages::LOWER)},
          &bankVal,
          POST_I2C_WRITE_DELAY_US,
          CAST_TO_INT(CmisField::BANK_SELECT));
    }
    // else: single-bank module -- bank 0 is the only bank, so reading bank 0 of
    // a banked page needs no bank-select write. This is a valid case (e.g. the
    // page-13h PRBS reads done on every refresh), not an error; skipping the
    // write also avoids POST_I2C_WRITE_DELAY_US (20ms) per banked-page access.
  }
  uint8_t pageVal = static_cast<uint8_t>(dataPage);
  qsfpImpl_->writeTransceiver(
      {TransceiverAccessParameter::ADDR_QSFP,
       kPageSelectByteOffset,
       sizeof(pageVal),
       static_cast<int>(CmisPages::LOWER)},
      &pageVal,
      POST_I2C_WRITE_DELAY_US,
      CAST_TO_INT(CmisField::PAGE_CHANGE));
}

void CmisModule::readCmisField(
    CmisField field,
    uint8_t* data,
    bool skipBankAndPageChange,
    std::optional<uint8_t> bank) {
  int dataLength, dataPage, dataOffset;
  getQsfpFieldAddress(field, dataPage, dataOffset, dataLength);
  if (!skipBankAndPageChange) {
    selectPageAndBank(dataPage, bank);
  }
  qsfpImpl_->readTransceiver(
      {TransceiverAccessParameter::ADDR_QSFP, dataOffset, dataLength, dataPage},
      data,
      CAST_TO_INT(field));
}

void CmisModule::writeCmisField(
    CmisField field,
    uint8_t* data,
    bool skipBankAndPageChange,
    std::optional<uint8_t> bank) {
  int dataLength, dataPage, dataOffset;
  getQsfpFieldAddress(field, dataPage, dataOffset, dataLength);
  if (!skipBankAndPageChange) {
    selectPageAndBank(dataPage, bank);
  }
  qsfpImpl_->writeTransceiver(
      {TransceiverAccessParameter::ADDR_QSFP, dataOffset, dataLength, dataPage},
      data,
      POST_I2C_WRITE_DELAY_US,
      CAST_TO_INT(field));
}

uint8_t CmisModule::readModifyWriteCmisField(
    CmisField field,
    uint8_t mask,
    uint8_t value,
    bool skipBankAndPageChange,
    std::optional<uint8_t> bank) {
  int dataLength, dataPage, dataOffset;
  getQsfpFieldAddress(field, dataPage, dataOffset, dataLength);
  if (dataLength != 1) {
    throw FbossError(
        fmt::format(
            "Read-modify-write of field {} needs a one byte field, got {:d} bytes",
            apache::thrift::util::enumNameSafe(field),
            dataLength));
  }

  uint8_t data;
  readCmisField(field, &data, skipBankAndPageChange, bank);
  data = (data & ~mask) | (value & mask);
  writeCmisField(field, &data, skipBankAndPageChange, bank);
  return data;
}

FlagLevels CmisModule::getQsfpSensorFlags(CmisField fieldName, int offset) {
  int dataOffset;
  int dataLength;
  int dataAddress;

  getQsfpFieldAddress(fieldName, dataAddress, dataOffset, dataLength);
  const uint8_t* data = getQsfpValuePtr(dataAddress, dataOffset, dataLength);

  // CMIS uses different mappings for flags than Sff therefore not using
  // getQsfpFlags here
  FlagLevels flags;
  CHECK_GE(offset, 0);
  CHECK_LE(offset, 4);
  flags.alarm()->high() = (*data & (1 << offset));
  flags.alarm()->low() = (*data & (1 << ++offset));
  flags.warn()->high() = (*data & (1 << ++offset));
  flags.warn()->low() = (*data & (1 << ++offset));

  return flags;
}

double CmisModule::getQsfpDACLength() const {
  uint8_t value;
  getFieldValueLocked(CmisField::LENGTH_COPPER, &value);
  auto base = value & FieldMasks::CABLE_LENGTH_MASK;
  auto multiplier =
      std::pow(10, value >> 6) * qsfpMultiplier.at(CmisField::LENGTH_COPPER);
  return base * multiplier;
}

double CmisModule::getQsfpSMFLength() const {
  if (flatMem_) {
    return 0;
  }
  uint8_t value;
  getFieldValueLocked(CmisField::LENGTH_SMF, &value);
  auto base = value & FieldMasks::CABLE_LENGTH_MASK;
  auto multiplier =
      std::pow(10, value >> 6) * qsfpMultiplier.at(CmisField::LENGTH_SMF);
  return base * multiplier;
}

double CmisModule::getQsfpOMLength(CmisField field) const {
  if (flatMem_) {
    return 0;
  }
  uint8_t value;
  getFieldValueLocked(field, &value);
  return value * qsfpMultiplier.at(field);
}

GlobalSensors CmisModule::getSensorInfo() {
  GlobalSensors info = GlobalSensors();
  info.temp()->value() =
      getQsfpSensor(CmisField::TEMPERATURE, CmisFieldInfo::getTemp);
  info.temp()->flags() = getQsfpSensorFlags(CmisField::MODULE_ALARMS, 0);
  info.vcc()->value() = getQsfpSensor(CmisField::VCC, CmisFieldInfo::getVcc);
  info.vcc()->flags() = getQsfpSensorFlags(CmisField::MODULE_ALARMS, 4);
  return info;
}

Vendor CmisModule::getVendorInfo() const {
  Vendor vendor = Vendor();
  *vendor.name() = getQsfpString(CmisField::VENDOR_NAME);
  *vendor.oui() = getQsfpString(CmisField::VENDOR_OUI);
  *vendor.partNumber() = getQsfpString(CmisField::PART_NUMBER);
  *vendor.rev() = getQsfpString(CmisField::REVISION_NUMBER);
  *vendor.serialNumber() = getQsfpString(CmisField::VENDOR_SERIAL_NUMBER);
  *vendor.dateCode() = getQsfpString(CmisField::MFG_DATE);
  return vendor;
}

std::array<std::string, 3> CmisModule::getFwRevisions() {
  int offset;
  int length;
  int dataAddress;
  std::array<std::string, 3> fwVersions;
  // Get module f/w version
  getQsfpFieldAddress(
      CmisField::FIRMWARE_REVISION, dataAddress, offset, length);
  const uint8_t* data = getQsfpValuePtr(dataAddress, offset, length);
  fwVersions[0] = fmt::format("{}.{}", data[0], data[1]);
  if (!flatMem_) {
    // Get DSP f/w version
    getQsfpFieldAddress(CmisField::DSP_FW_VERSION, dataAddress, offset, length);
    data = getQsfpValuePtr(dataAddress, offset, length);
    fwVersions[1] = fmt::format("{}.{}", data[0], data[1]);
    // Get the build revision
    getQsfpFieldAddress(CmisField::BUILD_REVISION, dataAddress, offset, length);
    data = getQsfpValuePtr(dataAddress, offset, length);
    fwVersions[2] = fmt::format("{}.{}", data[0], data[1]);
  } else {
    fwVersions[1] = "";
    fwVersions[2] = "";
  }
  return fwVersions;
}

Cable CmisModule::getCableInfo() {
  Cable cable = Cable();
  cable.transmitterTech() = getQsfpTransmitterTechnology();
  cable.mediaTypeEncoding() = getMediaTypeEncoding();

  if (auto length = getQsfpSMFLength(); length != 0) {
    cable.singleMode() = length;
  }
  if (auto length = getQsfpOMLength(CmisField::LENGTH_OM5); length != 0) {
    cable.om5() = length;
  }
  if (auto length = getQsfpOMLength(CmisField::LENGTH_OM4); length != 0) {
    cable.om4() = length;
  }
  if (auto length = getQsfpOMLength(CmisField::LENGTH_OM3); length != 0) {
    cable.om3() = length;
  }
  if (auto length = getQsfpOMLength(CmisField::LENGTH_OM2); length != 0) {
    cable.om2() = length;
  }
  if (auto length = getQsfpDACLength(); length != 0) {
    cable.length() = length;
  }
  return cable;
}

FirmwareStatus CmisModule::getFwStatus() {
  FirmwareStatus fwStatus;
  auto fwRevisions = getFwRevisions();
  fwStatus.version() = fwRevisions[0];
  fwStatus.dspFwVer() = fwRevisions[1];
  fwStatus.buildRev() = fwRevisions[2];
  fwStatus.fwFault() =
      (getSettingsValue(CmisField::MODULE_FLAG, FWFAULT_MASK) >> 1);

  // Use cached firmware build number from full EEPROM read
  if (cachedFwBuildNumber_.has_value()) {
    fwStatus.buildNumber() = cachedFwBuildNumber_.value();
  }

  return fwStatus;
}

std::optional<uint16_t> CmisModule::fetchFwBuildNumberFromCdb() {
  if (flatMem_) {
    return std::nullopt;
  }

  constexpr int kRetryPollIntervalUsec = 100000;
  constexpr int kMaxRetries = 3;
  const int maxAttempts = shouldRetryCdbFwInfo() ? kMaxRetries : 1;

  for (int attempt = 1; attempt <= maxAttempts; attempt++) {
    CdbCommandBlock commandBlockBuf;
    commandBlockBuf.createCdbCmdGetFirmwareInfo();
    auto ret = commandBlockBuf.cmisRunCdbCommand(qsfpImpl_);

    if (ret) {
      auto buildNumber = commandBlockBuf.getFwBuildNumber();
      if (buildNumber.has_value()) {
        QSFP_LOG(DBG1, this)
            << "fetchFwBuildNumberFromCdb: build number " << buildNumber.value()
            << " (attempt " << attempt << ")";
      }
      return buildNumber;
    }

    auto cdbStatus = commandBlockBuf.getLastCdbStatus();
    if (cdbStatus != kCdbCommandStatusFailed || attempt == maxAttempts) {
      QSFP_LOG(ERR, this) << "fetchFwBuildNumberFromCdb: CDB command failed"
                          << " cdbStatus=0x" << std::hex << (int)cdbStatus
                          << std::dec << " after " << attempt << " attempt(s)";
      return std::nullopt;
    }

    QSFP_LOG(WARN, this)
        << "fetchFwBuildNumberFromCdb: CDB returned 0x40 (attempt " << attempt
        << "/" << maxAttempts << "), retrying";
    /* sleep override */
    usleep(kRetryPollIntervalUsec);
  }
  return std::nullopt;
}

ModuleStatus CmisModule::getModuleStatus() {
  ModuleStatus moduleStatus;
  moduleStatus.cmisModuleState() =
      moduleStateFromStatusByte(getSettingsValue(CmisField::MODULE_STATE));
  moduleStatus.fwStatus() = getFwStatus();
  moduleStatus.cmisStateChanged() = getModuleStateChanged();
  setCustomLatchedFlags(moduleStatus);
  return moduleStatus;
}

/*
 * Byte 67 is CMIS Custom space, so only report the flags a module has
 * explicitly advertised in Page 01h Byte 191; anything else there belongs to
 * some other vendor's feature. The flags are read-to-clear, and the lower page
 * is re-read on every refresh, so each value covers the interval since the
 * previous refresh.
 */
void CmisModule::setCustomLatchedFlags(ModuleStatus& moduleStatus) {
  const auto diagsCapability = getDiagsCapability();
  if (!diagsCapability.has_value()) {
    return;
  }

  const uint8_t data = getSettingsValue(CmisField::CUSTOM_FLAGS);
  if (*diagsCapability->modeMismatchFlag()) {
    moduleStatus.modeMismatchFlag() =
        (data & FieldMasks::MODE_MISMATCH_FLAG_MASK) != 0;
  }
  if (*diagsCapability->dspTempMargin()) {
    moduleStatus.dspTempNegativeMarginFlag() =
        (data & FieldMasks::DSP_TEMP_NEGATIVE_MARGIN_FLAG_MASK) != 0;
  }
  if (*diagsCapability->laserTempMargin()) {
    moduleStatus.laserTempNegativeMarginFlag() =
        (data & FieldMasks::LASER_TEMP_NEGATIVE_MARGIN_FLAG_MASK) != 0;
  }
}

/*
 * Bytes 68-69 hold S8 margins in quarter-degree Celsius steps. Decode to whole
 * degrees so consumers don't need to know the register scaling.
 */
ThermalMargins CmisModule::getThermalMargins() {
  ThermalMargins margins;
  const auto diagsCapability = getDiagsCapability();
  if (!diagsCapability.has_value()) {
    return margins;
  }

  constexpr double kQuarterDegreeC = 0.25;
  if (*diagsCapability->dspTempMargin()) {
    margins.dspTempMargin = kQuarterDegreeC *
        static_cast<int8_t>(getSettingsValue(CmisField::DSP_TEMP_MARGIN));
  }
  if (*diagsCapability->laserTempMargin()) {
    margins.laserTempMargin = kQuarterDegreeC *
        static_cast<int8_t>(getSettingsValue(CmisField::LASER_TEMP_MARGIN));
  }
  return margins;
}

bool CmisModule::isModeMismatchSupported() const {
  const auto diagsCapability = getDiagsCapability();
  return diagsCapability.has_value() && *diagsCapability->modeMismatchFlag();
}

/*
 * Threhold values are stored just once;  they aren't per-channel,
 * so in all cases we simple assemble two-byte values and convert
 * them based on the type of the field.
 */
ThresholdLevels CmisModule::getThresholdValues(
    CmisField field,
    double (*conversion)(uint16_t value)) {
  int offset;
  int length;
  int dataAddress;

  CHECK(!flatMem_);

  ThresholdLevels thresh;

  getQsfpFieldAddress(field, dataAddress, offset, length);
  const uint8_t* data = getQsfpValuePtr(dataAddress, offset, length);

  CHECK_GE(length, 8);
  thresh.alarm()->high() = conversion(data[0] << 8 | data[1]);
  thresh.alarm()->low() = conversion(data[2] << 8 | data[3]);
  thresh.warn()->high() = conversion(data[4] << 8 | data[5]);
  thresh.warn()->low() = conversion(data[6] << 8 | data[7]);

  // For Tx Bias threshold, take care of multiplier
  if (field == CmisField::TX_BIAS_THRESH) {
    getQsfpFieldAddress(
        CmisField::TX_BIAS_MULTIPLIER, dataAddress, offset, length);
    data = getQsfpValuePtr(dataAddress, offset, length);
    auto biasMultiplier = CmisFieldInfo::getTxBiasMultiplier(data[0]);

    thresh.alarm()->high() = thresh.alarm()->high().value() * biasMultiplier;
    thresh.alarm()->low() = thresh.alarm()->low().value() * biasMultiplier;
    thresh.warn()->high() = thresh.warn()->high().value() * biasMultiplier;
    thresh.warn()->low() = thresh.warn()->low().value() * biasMultiplier;
  }

  return thresh;
}

std::optional<AlarmThreshold> CmisModule::getThresholdInfo() {
  if (flatMem_) {
    return {};
  }
  AlarmThreshold threshold = AlarmThreshold();
  threshold.temp() =
      getThresholdValues(CmisField::TEMPERATURE_THRESH, CmisFieldInfo::getTemp);
  threshold.vcc() =
      getThresholdValues(CmisField::VCC_THRESH, CmisFieldInfo::getVcc);
  threshold.rxPwr() =
      getThresholdValues(CmisField::RX_PWR_THRESH, CmisFieldInfo::getPwr);
  threshold.txPwr() =
      getThresholdValues(CmisField::TX_PWR_THRESH, CmisFieldInfo::getPwr);
  threshold.txBias() =
      getThresholdValues(CmisField::TX_BIAS_THRESH, CmisFieldInfo::getTxBias);
  return threshold;
}

uint8_t CmisModule::getSettingsValue(CmisField field, uint8_t mask) const {
  int offset;
  int length;
  int dataAddress;

  getQsfpFieldAddress(field, dataAddress, offset, length);
  const uint8_t* data = getQsfpValuePtr(dataAddress, offset, length);

  return data[0] & mask;
}

TransceiverSettings CmisModule::getTransceiverSettingsInfo() {
  TransceiverSettings settings = TransceiverSettings();
  if (!flatMem_) {
    settings.cdrTx() = CmisFieldInfo::getFeatureState(
        getSettingsValue(CmisField::TX_SIG_INT_CONT_AD, CDR_IMPL_MASK),
        getSettingsValue(CmisField::TX_CDR_CONTROL));
    settings.cdrRx() = CmisFieldInfo::getFeatureState(
        getSettingsValue(CmisField::RX_SIG_INT_CONT_AD, CDR_IMPL_MASK),
        getSettingsValue(CmisField::RX_CDR_CONTROL));
  } else {
    settings.cdrTx() = FeatureState::UNSUPPORTED;
    settings.cdrRx() = FeatureState::UNSUPPORTED;
  }
  settings.powerMeasurement() =
      flatMem_ ? FeatureState::UNSUPPORTED : FeatureState::ENABLED;

  settings.powerControl() = getPowerControlValue(true /* readFromCache */);
  settings.rateSelect() = flatMem_ ? RateSelectState::UNSUPPORTED
                                   : RateSelectState::APPLICATION_RATE_SELECT;
  settings.rateSelectSetting() = RateSelectSetting::UNSUPPORTED;

  settings.mediaLaneSettings() =
      std::vector<MediaLaneSettings>(numMediaLanes());
  settings.hostLaneSettings() = std::vector<HostLaneSettings>(numHostLanes());

  if (!flatMem_) {
    if (!getMediaLaneSettings(*(settings.mediaLaneSettings()))) {
      settings.mediaLaneSettings()->clear();
      settings.mediaLaneSettings().reset();
    }

    if (!getHostLaneSettings(*(settings.hostLaneSettings()))) {
      settings.hostLaneSettings()->clear();
      settings.hostLaneSettings().reset();
    }
  }

  settings.mediaInterface() = std::vector<MediaInterfaceId>(numMediaLanes());
  if (!getMediaInterfaceId(*(settings.mediaInterface()))) {
    settings.mediaInterface()->clear();
    settings.mediaInterface().reset();
  }

  return settings;
}

bool CmisModule::getMediaLaneSettings(
    std::vector<MediaLaneSettings>& laneSettings) {
  assert(laneSettings.size() == numMediaLanes());

  if (flatMem_) {
    return false;
  }
  for (int lane = 0; lane < laneSettings.size(); lane++) {
    laneSettings[lane].lane() = lane;
    laneSettings[lane].txDisable() =
        getLaneFlagSet(CmisField::TX_DISABLE, lane);
    laneSettings[lane].txSquelch() =
        getLaneFlagSet(CmisField::TX_SQUELCH_DISABLE, lane);
    laneSettings[lane].txSquelchForce() =
        getLaneFlagSet(CmisField::TX_FORCE_SQUELCH, lane);
  }

  return true;
}

bool CmisModule::getHostLaneSettings(
    std::vector<HostLaneSettings>& laneSettings) {
  assert(laneSettings.size() == numHostLanes());

  if (flatMem_) {
    return false;
  }

  for (int lane = 0; lane < laneSettings.size(); lane++) {
    laneSettings[lane].lane() = lane;
    laneSettings[lane].rxOutput() = getLaneFlagSet(CmisField::RX_DISABLE, lane);
    laneSettings[lane].rxSquelch() =
        getLaneFlagSet(CmisField::RX_SQUELCH_DISABLE, lane);
    uint8_t pre = getLaneNibble(CmisField::RX_OUT_PRE_CURSOR, lane);
    QSFP_LOG(DBG3, this) << fmt::format("Lane = {:d}, Pre = {:d}", lane, pre);
    laneSettings[lane].rxOutputPreCursor() = pre;

    uint8_t post = getLaneNibble(CmisField::RX_OUT_POST_CURSOR, lane);
    QSFP_LOG(DBG3, this) << fmt::format("Lane = {:d}, Post = {:d}", lane, post);
    laneSettings[lane].rxOutputPostCursor() = post;

    uint8_t mainVal = getLaneNibble(CmisField::RX_OUT_MAIN, lane);
    QSFP_LOG(DBG3, this) << fmt::format(
        "Lane = {:d}, Main = {:d}", lane, mainVal);
    laneSettings[lane].rxOutputAmplitude() = mainVal;

    uint8_t appSel =
        (getLaneValuePtr(CmisField::ACTIVE_CTRL_ALL_LANES, lane, 1)[0] &
         APP_SEL_MASK) >>
        APP_SEL_BITSHIFT;
    laneSettings[lane].currentAppSel() = appSel;
  }
  return true;
}

// Returns the currently configured mediaInterfaceCode on a host lane
uint8_t CmisModule::currentConfiguredMediaInterfaceCode(
    uint8_t hostLane) const {
  auto mediaTypeEncoding = getMediaTypeEncoding();
  uint8_t application = 0;
  if (mediaTypeEncoding == MediaTypeEncodings::OPTICAL_SMF) {
    application = getCurrentApplication(hostLane, kMediaInterfaceCodeOffset);
  } else if (
      mediaTypeEncoding == MediaTypeEncodings::PASSIVE_CU &&
      !moduleCapabilities_.empty()) {
    // For Passive DAC cables that don't get programmed, just return the media
    // interface code for the first capability.
    auto firstModuleCapability = moduleCapabilities_.begin();
    application = firstModuleCapability->moduleMediaInterface;
  } else if (mediaTypeEncoding == MediaTypeEncodings::ACTIVE_CABLES) {
    application = getCurrentApplication(hostLane, kHostInterfaceCodeOffset);
  }
  return application;
}

// Returns the list of host lanes configured in the same datapath as the
// provided startHostLane
std::vector<uint8_t> CmisModule::configuredHostLanes(
    uint8_t startHostLane) const {
  std::vector<uint8_t> cfgLanes;
  auto currentMediaInterface =
      currentConfiguredMediaInterfaceCode(startHostLane);
  if (auto applicationAdvertisingField =
          getApplicationField(currentMediaInterface, startHostLane)) {
    for (uint8_t lane = startHostLane;
         lane < startHostLane + applicationAdvertisingField->hostLaneCount;
         lane++) {
      cfgLanes.push_back(lane);
    }
  }
  return cfgLanes;
}

// Returns the list of media lanes configured in the same datapath as the
// provided startHostLane
std::vector<uint8_t> CmisModule::configuredMediaLanes(
    uint8_t startHostLane) const {
  std::vector<uint8_t> cfgLanes;
  if (flatMem_) {
    // FlatMem_ modules won't have page01 to read the media lane assignment
    return cfgLanes;
  }

  auto currentMediaInterface =
      currentConfiguredMediaInterfaceCode(startHostLane);
  if (auto applicationAdvertisingField =
          getApplicationField(currentMediaInterface, startHostLane)) {
    // Module capabilities advertise per-bank (intra-bank) lane assignments. A
    // port's host and media lanes live in the same bank, so match against the
    // intra-bank host start lane and offset the resulting media lanes back to
    // the port's bank to report global media lanes.
    const uint8_t bank = laneToBank(startHostLane);
    const uint8_t intraStartHostLane = laneInBank(startHostLane);

    // The assignment byte has a '1' for every datapath that starts at that
    // lane. We first need to find out the 'index (say n)' of the datapath
    // using the given start host lane. We'll then look for a nth '1' in the
    // corresponding media lane assignment.
    // For example, if the hostLaneAssignment is 0x55, the corresponding
    // mediaLaneAssignment can be 0xF. Which means that the pairing of
    // host->media lanes will be (hostLane:0, mediaLane:0), (hostLane:2,
    // mediaLane:1), (hostLane:4, mediaLane:2), (hostLane:6, mediaLane:3)
    const auto& hostStartLanes = applicationAdvertisingField->hostStartLanes;
    const auto it = std::find(
        hostStartLanes.begin(), hostStartLanes.end(), intraStartHostLane);
    if (it == hostStartLanes.end()) {
      QSFP_LOG(ERR, this) << fmt::format(
          "Couldn't find the intra-bank hostStartLane {} (global {}, bank {}) in the advertised host start lanes",
          intraStartHostLane,
          startHostLane,
          bank);
      return cfgLanes;
    }

    const auto index = std::distance(hostStartLanes.begin(), it);
    const auto& mediaStartLanes = applicationAdvertisingField->mediaStartLanes;
    uint8_t mediaStartLane = 0;
    if (index < mediaStartLanes.size()) {
      mediaStartLane = globalLane(bank, mediaStartLanes[index]);
    } else if (mediaStartLanes.size() == 1) {
      // Multiple host datapaths multiplexed onto a single media datapath (e.g.
      // 3x200G ZR on one coherent media lane) all share that media lane
      mediaStartLane = globalLane(bank, mediaStartLanes[0]);
    } else {
      QSFP_LOG(ERR, this) << "Index " << index << " out of range for "
                          << folly::join(
                                 ",",
                                 applicationAdvertisingField->mediaStartLanes);
      return cfgLanes;
    }

    for (uint8_t start = mediaStartLane;
         start < mediaStartLane + applicationAdvertisingField->mediaLaneCount;
         start++) {
      cfgLanes.push_back(start);
    }
  }
  return cfgLanes;
}

uint8_t CmisModule::getCurrentApplication(uint8_t lane, int byteOffset) const {
  // lane is a global lane. This is a read/refresh-path accessor, so soft-fail
  // (log + return 0 = undefined app) for an out-of-range lane rather than
  // throwing from getBankedQsfpValuePtr on a non-existent bank.
  if (lane >= getMaxNumBanks() * kMaxOsfpNumLanes) {
    QSFP_LOG(ERR, this) << fmt::format(
        "Lane {} out of range for a module with {} bank(s)",
        lane,
        getMaxNumBanks());
    return 0;
  }
  uint8_t currentApplicationSel;
  if (!flatMem_) {
    // getCurrentAppSelCode reads ACTIVE_CTRL for the lane's bank and returns
    // the app sel already masked and shifted to the higher four bits.
    currentApplicationSel = getCurrentAppSelCode(lane);
  } else {
    // FlatMem modules don't expose page 11h's operational app-sel register;
    // report 0 (handled as "not selected" just below), preserving the previous
    // behavior. These modules resolve their application via the static
    // advertising path instead.
    currentApplicationSel = 0;
  }

  // Application select value 0 means application is not selected by module yet
  if (currentApplicationSel == 0) {
    QSFP_LOG(ERR, this) << "Module has not selected application yet";
    // Based on SFF-8024, an App / App Sel of 0 is undefined/Unknown.
    return 0;
  }

  uint8_t currentApplication;
  int offset;
  int length;
  int dataAddress;

  // The ApSel value from 1 to 8 are present in the lower page and values from
  // 9 to 15 are in page 1
  if (currentApplicationSel <= 8) {
    getQsfpFieldAddress(
        CmisField::APPLICATION_ADVERTISING1, dataAddress, offset, length);
    // We use the module Media Interface ID for Optical modules, which is
    // located at the second byte of the field (byteOffset = 1), as Application
    // ID here. If we have an AEC cable, we use the module host interface ID
    // which has a byteOffset of 0. This is in page 00h app sel advertising
    // (starting at offset 86 for page)
    offset += (currentApplicationSel - 1) * length + byteOffset;
  } else {
    getQsfpFieldAddress(
        CmisField::APPLICATION_ADVERTISING2, dataAddress, offset, length);
    // This page contains Module media interface id on second byte of field
    // for ApSel 9 onwards
    offset += (currentApplicationSel - 9) * length + byteOffset;
  }

  getQsfpValue(dataAddress, offset, 1, &currentApplication);

  return currentApplication;
}

MediaTypeEncodings CmisModule::getMediaTypeEncoding() const {
  return static_cast<MediaTypeEncodings>(
      getSettingsValue(CmisField::MEDIA_TYPE_ENCODINGS));
}

bool CmisModule::getMediaInterfaceId(
    std::vector<MediaInterfaceId>& mediaInterface) {
  assert(mediaInterface.size() == numMediaLanes());
  MediaTypeEncodings encoding = getMediaTypeEncoding();
  if (encoding == MediaTypeEncodings::OPTICAL_SMF) {
    for (int lane = 0; lane < mediaInterface.size(); lane++) {
      auto smfMediaInterface = getSmfMediaInterface(lane);
      mediaInterface[lane].lane() = lane;
      MediaInterfaceUnion media;
      media.smfCode() = smfMediaInterface;
      if (TransceiverPropertiesManager::isKnown(getModuleMediaInterface())) {
        mediaInterface[lane].code() =
            TransceiverPropertiesManager::mediaLaneCodeToMediaInterfaceCode(
                static_cast<uint8_t>(smfMediaInterface));
      } else {
        mediaInterface[lane].code() =
            CmisHelper::getMediaInterfaceCode<SMFMediaInterfaceCode>(
                smfMediaInterface, CmisHelper::getSmfMediaInterfaceMapping());
      }
      if (mediaInterface[lane].code() == MediaInterfaceCode::UNKNOWN) {
        QSFP_LOG(ERR, this)
            << "Unable to find MediaInterfaceCode for "
            << apache::thrift::util::enumNameSafe(smfMediaInterface);
      }
      mediaInterface[lane].media() = media;
    }
  } else if (
      encoding == MediaTypeEncodings::PASSIVE_CU &&
      !moduleCapabilities_.empty()) {
    // For Passive DAC cables that don't get programmed, just return the media
    // interface code for the first capability.
    auto firstModuleCapability = moduleCapabilities_.begin();
    for (int lane = 0; lane < mediaInterface.size(); lane++) {
      mediaInterface[lane].lane() = lane;
      MediaInterfaceUnion media;
      media.passiveCuCode() = static_cast<PassiveCuMediaInterfaceCode>(
          firstModuleCapability->moduleMediaInterface);
      // FIXME: Remove CR8_400G hardcoding and derive this from number of
      // lanes/host electrical interface instead
      mediaInterface[lane].code() = MediaInterfaceCode::CR8_400G;
      mediaInterface[lane].media() = media;
    }
  } else if (encoding == MediaTypeEncodings::ACTIVE_CABLES) {
    for (int lane = 0; lane < mediaInterface.size(); lane++) {
      auto activeCuInterfaceCode = getActiveCuMediaInterface(lane);
      mediaInterface[lane].lane() = lane;
      MediaInterfaceUnion media;
      media.activeCuCode() = activeCuInterfaceCode;
      mediaInterface[lane].code() =
          CmisHelper::getMediaInterfaceCode<ActiveCuHostInterfaceCode>(
              activeCuInterfaceCode,
              CmisHelper::getActiveMediaInterfaceMapping());
      if (mediaInterface[lane].code() == MediaInterfaceCode::UNKNOWN) {
        QSFP_LOG(ERR, this)
            << "Unable to find MediaInterfaceCode for "
            << apache::thrift::util::enumNameSafe(activeCuInterfaceCode);
      }
      mediaInterface[lane].media() = media;
    }
  } else {
    return false;
  }

  return true;
}

void CmisModule::getApplicationCapabilities() {
  const uint8_t* data;
  int offset;
  int length;
  int dataAddress;

  moduleCapabilities_.clear();
  // Flat memory modules don't have page 01h, so only advertise ApSel 1-8
  const uint8_t maxApplications =
      flatMem_ ? kNumLowerPageApplications : kMaxNumApplications;
  for (uint8_t i = 0; i < maxApplications; i++) {
    if (i < kNumLowerPageApplications) {
      getQsfpFieldAddress(
          CmisField::APPLICATION_ADVERTISING1, dataAddress, offset, length);
      data = getQsfpValuePtr(dataAddress, offset + i * length, length);
    } else {
      getQsfpFieldAddress(
          CmisField::APPLICATION_ADVERTISING2, dataAddress, offset, length);
      data = getQsfpValuePtr(
          dataAddress,
          offset + (i - kNumLowerPageApplications) * length,
          length);
    }

    if (data[0] == 0xff) {
      break;
    }

    ApplicationAdvertisingField applicationAdvertisingField;
    applicationAdvertisingField.ApSelCode = (i + 1);
    // For Active cables, we use the Host Interface Code as the designated
    // identifier for the rate of the application. The Media side of active
    // cables, per spec, specifies only the BER for the cable, which might
    // be the same for all the supported rates.
    if (isAecModule()) {
      applicationAdvertisingField.moduleMediaInterface = data[0];
    } else {
      applicationAdvertisingField.moduleMediaInterface = data[1];
    }
    if (i >= kNumLowerPageApplications &&
        applicationAdvertisingField.moduleMediaInterface == 0) {
      // Unused Page 01h entry that wasn't terminated with 0xFF
      continue;
    }
    QSFP_LOG(DBG3, this) << fmt::format(
        "Adding module capability: {:#x} at position {:d}", data[1], i + 1);
    applicationAdvertisingField.moduleHostInterface = data[0];
    applicationAdvertisingField.hostLaneCount =
        (data[2] & FieldMasks::UPPER_FOUR_BITS_MASK) >> 4;
    applicationAdvertisingField.mediaLaneCount =
        data[2] & FieldMasks::LOWER_FOUR_BITS_MASK;
    for (int lane = 0; lane < 8; lane++) {
      if (data[3] & (1 << lane)) {
        applicationAdvertisingField.hostStartLanes.push_back(lane);
      }
    }

    if (!flatMem_) {
      getQsfpFieldAddress(
          CmisField::MEDIA_LANE_ASSIGNMENT, dataAddress, offset, length);
      offset += i;
      uint8_t mediaLaneAssignment;
      getQsfpValue(dataAddress, offset, 1, &mediaLaneAssignment);
      for (int lane = 0; lane < 8; lane++) {
        if (mediaLaneAssignment & (1 << lane)) {
          applicationAdvertisingField.mediaStartLanes.push_back(lane);
        }
      }
    }

    moduleCapabilities_.push_back(applicationAdvertisingField);
  }
}

PowerControlState CmisModule::getPowerControlValue(bool readFromCache) {
  uint8_t moduleControl;
  if (readFromCache) {
    moduleControl = getSettingsValue(
        CmisField::MODULE_CONTROL, uint8_t(POWER_CONTROL_MASK));
  } else {
    readCmisField(CmisField::MODULE_CONTROL, &moduleControl);
    moduleControl &= POWER_CONTROL_MASK;
  }
  if (moduleControl) {
    return PowerControlState::POWER_LPMODE;
  } else {
    return PowerControlState::HIGH_POWER_OVERRIDE;
  }
}

PowerControlState CmisModule::getCurrentPowerControlState() {
  uint8_t currentModuleControl;
  readCmisField(CmisField::MODULE_CONTROL, &currentModuleControl);

  if (currentModuleControl & POWER_CONTROL_MASK) {
    QSFP_LOG(INFO, this)
        << "getCurrentPowerControlState: Current power state is LOW POWER MODE (LP bit set)";
    return PowerControlState::POWER_LPMODE;
  } else {
    QSFP_LOG(INFO, this)
        << "getCurrentPowerControlState: Current power state is HIGH POWER MODE";
    return PowerControlState::HIGH_POWER_OVERRIDE;
  }
}

bool CmisModule::isModuleInReadyState() {
  uint8_t moduleStatus;
  readCmisField(CmisField::MODULE_STATE, &moduleStatus);
  const bool isReady =
      moduleStateFromStatusByte(moduleStatus) == CmisModuleState::READY;

  if (isReady) {
    QSFP_LOG(DBG2, this) << "isModuleInReadyState: Module is in READY state";
  } else {
    QSFP_LOG(INFO, this)
        << "isModuleInReadyState: Module is not ready yet - need more time to be ready";
  }

  return isReady;
}

bool CmisModule::moduleReadyStatePoll() {
  auto retries = 0;
  constexpr int kUsecModuleReadyStatePollTime = 100000; // 100 ms
  // Tunable (coherent/ZR) optics take much longer to settle than fixed
  // wavelength optics, so give them a longer ready timeout.
  constexpr int kUsecModuleReadyTunable = 60000000; // 60 seconds
  constexpr int kUsecModuleReadyNonTunable = 5000000; // 5 seconds
  const int kUsecModuleReadyStateUpdateTimeMax =
      isTunableOptics() ? kUsecModuleReadyTunable : kUsecModuleReadyNonTunable;
  auto maxRetriesReady =
      kUsecModuleReadyStateUpdateTimeMax / kUsecModuleReadyStatePollTime;

  while (retries++ < maxRetriesReady) {
    /* sleep override */
    usleep(kUsecModuleReadyStatePollTime);
    if (isModuleInReadyState()) {
      return true;
    }
  }
  if (retries >= maxRetriesReady) {
    QSFP_LOG(ERR, this) << fmt::format(
        "Module not ready even after waiting {:d} uSec",
        kUsecModuleReadyStateUpdateTimeMax);
  }
  return false;
}

void CmisModule::setModuleLowPowerModeLocked() {
  // Set to 0x60 = (SquelchControl=Reduce Pave | LowPwr)
  uint8_t newModuleControl = SQUELCH_CONTROL | LOW_PWR_BIT;
  QSFP_LOG(INFO, this) << fmt::format(
      "setModuleLowPowerModeLocked: Setting module control to {:#x}",
      newModuleControl);
  writeCmisField(CmisField::MODULE_CONTROL, &newModuleControl);
  // Wait for 100ms before resetting the LP mode
  /* sleep override */
  usleep(kUsecBetweenPowerModeFlap);
}

void CmisModule::releaseModuleLowPowerModeLocked() {
  // Clear low power bit (set to 0x20)
  uint8_t newModuleControl = SQUELCH_CONTROL;
  QSFP_LOG(INFO, this) << fmt::format(
      "releaseModuleLowPowerModeLocked: Clearing low power bit, module control to {:#x}",
      newModuleControl);
  writeCmisField(CmisField::MODULE_CONTROL, &newModuleControl);
}

/*
 * For the specified field, collect alarm and warning flags for the channel.
 */

FlagLevels CmisModule::getChannelFlags(CmisField field, int channel) {
  FlagLevels flags;
  int offset;
  int length;
  int dataAddress;

  // channel is a global lane. This is a read/refresh-path accessor, so
  // soft-fail (log + return empty flags) for an out-of-range channel rather
  // than throwing from getBankedQsfpValuePtr on a non-existent bank.
  if (channel < 0 || channel >= getMaxNumBanks() * kMaxOsfpNumLanes) {
    QSFP_LOG(ERR, this) << fmt::format(
        "Channel {} out of range for a module with {} bank(s)",
        channel,
        getMaxNumBanks());
    return flags;
  }

  // Each flag field is 4 bytes (alarm high/low, warn high/low), one bit per
  // intra-bank lane; select the channel's bank and test its bit.
  uint8_t bank = channel / kMaxOsfpNumLanes;
  int intraLane = channel % kMaxOsfpNumLanes;
  getQsfpFieldAddress(field, dataAddress, offset, length);
  const uint8_t* data =
      getBankedQsfpValuePtr(dataAddress, offset, length, bank);

  flags.warn()->low() = (data[3] & (1 << intraLane));
  flags.warn()->high() = (data[2] & (1 << intraLane));
  flags.alarm()->low() = (data[1] & (1 << intraLane));
  flags.alarm()->high() = (data[0] & (1 << intraLane));

  return flags;
}

/*
 * Iterate through channels collecting appropriate data;
 */

bool CmisModule::getSignalsPerMediaLane(
    std::vector<MediaLaneSignals>& signals) {
  assert(signals.size() == numMediaLanes());
  if (flatMem_) {
    return false;
  }

  // Hoisted: getDiagsCapability() copies the whole DiagsCapability under a
  // lock, so it must not be called per lane.
  const bool modeMismatchSupported = isModeMismatchSupported();

  for (int lane = 0; lane < signals.size(); lane++) {
    signals[lane].lane() = lane;
    signals[lane].rxLos() = getLaneFlagSet(CmisField::RX_LOS_FLAG, lane);
    signals[lane].rxLol() = getLaneFlagSet(CmisField::RX_LOL_FLAG, lane);
    signals[lane].txFault() = getLaneFlagSet(CmisField::TX_FAULT_FLAG, lane);
    if (modeMismatchSupported) {
      signals[lane].modeMismatch() =
          getLaneFlagSet(CmisField::MEDIA_MODE_MISMATCH, lane);
    }
  }

  return true;
}

/*
 * Iterate through channels collecting appropriate data;
 */

bool CmisModule::getSignalsPerHostLane(std::vector<HostLaneSignals>& signals) {
  assert(signals.size() == numHostLanes());
  if (flatMem_) {
    return false;
  }

  // Hoisted: getDiagsCapability() copies the whole DiagsCapability under a
  // lock, so it must not be called per lane.
  const bool modeMismatchSupported = isModeMismatchSupported();

  for (int lane = 0; lane < signals.size(); lane++) {
    signals[lane].lane() = lane;
    signals[lane].dataPathDeInit() =
        getLaneFlagSet(CmisField::DATA_PATH_DEINIT, lane);
    signals[lane].cmisLaneState() = getDatapathLaneStateLocked(lane);
    signals[lane].txLos() = getLaneFlagSet(CmisField::TX_LOS_FLAG, lane);
    signals[lane].txLol() = getLaneFlagSet(CmisField::TX_LOL_FLAG, lane);
    signals[lane].txAdaptEqFault() =
        getLaneFlagSet(CmisField::TX_EQ_FLAG, lane);
    if (modeMismatchSupported) {
      signals[lane].modeMismatch() =
          getLaneFlagSet(CmisField::HOST_MODE_MISMATCH, lane);
    }
  }

  return true;
}

/*
 * Iterate through channels collecting appropriate data;
 */

bool CmisModule::getSensorsPerChanInfo(std::vector<Channel>& channels) {
  if (flatMem_) {
    return false;
  }
  // Every loop below indexes channels.at(channel) for channel <
  // numMediaLanes(), so require the caller to have sized channels accordingly.
  // Bounds-checked .at() keeps the accesses safe even if that contract is
  // violated.
  CHECK_GE(channels.size(), numMediaLanes());

  for (int channel = 0; channel < numMediaLanes(); channel++) {
    channels.at(channel).sensors()->rxPwr()->flags() =
        getChannelFlags(CmisField::RX_PWR_FLAG, channel);
  }

  for (int channel = 0; channel < numMediaLanes(); channel++) {
    channels.at(channel).sensors()->txBias()->flags() =
        getChannelFlags(CmisField::TX_BIAS_FLAG, channel);
  }

  for (int channel = 0; channel < numMediaLanes(); channel++) {
    channels.at(channel).sensors()->txPwr()->flags() =
        getChannelFlags(CmisField::TX_PWR_FLAG, channel);
  }

  // AEC modules don't support rx/tx power monitoring
  const bool isAec = isAecModule();

  for (int channel = 0; channel < numMediaLanes(); channel++) {
    const uint8_t* data =
        getLaneValuePtr(CmisField::CHANNEL_RX_PWR, channel, 2);
    uint16_t value = data[0] << 8 | data[1];
    auto pwr = CmisFieldInfo::getPwr(value); // This is in mW
    // TODO: we should probably make rxPwr optional as well
    channels.at(channel).sensors()->rxPwr()->value() = pwr;
    if (!isAec) {
      Sensor rxDbm;
      rxDbm.value() = mwToDb(pwr);
      channels.at(channel).sensors()->rxPwrdBm() = rxDbm;
    }
  }

  // For Tx bias, take care of multiplier. The multiplier is module-level
  // (page 01h), not per-lane.
  int offset, length, dataAddress;
  getQsfpFieldAddress(
      CmisField::TX_BIAS_MULTIPLIER, dataAddress, offset, length);
  auto biasMultiplier = CmisFieldInfo::getTxBiasMultiplier(
      getQsfpValuePtr(dataAddress, offset, length)[0]);

  for (int channel = 0; channel < numMediaLanes(); channel++) {
    const uint8_t* data =
        getLaneValuePtr(CmisField::CHANNEL_TX_BIAS, channel, 2);
    uint16_t value = data[0] << 8 | data[1];
    channels.at(channel).sensors()->txBias()->value() =
        CmisFieldInfo::getTxBias(value) * biasMultiplier;
  }

  for (int channel = 0; channel < numMediaLanes(); channel++) {
    const uint8_t* data =
        getLaneValuePtr(CmisField::CHANNEL_TX_PWR, channel, 2);
    uint16_t value = data[0] << 8 | data[1];
    auto pwr = CmisFieldInfo::getPwr(value); // This is in mW
    // TODO: we should probably make txPwr optional as well
    channels.at(channel).sensors()->txPwr()->value() = pwr;
    if (!isAec) {
      Sensor txDbm;
      txDbm.value() = mwToDb(pwr);
      channels.at(channel).sensors()->txPwrdBm() = txDbm;
    }
  }

  for (int channel = 0; channel < numMediaLanes(); channel++) {
    const uint8_t* data =
        getLaneValuePtr(CmisField::MEDIA_BER_HOST_SNR, channel, 2);
    // SNR value are LSB.
    uint16_t value = data[1] << 8 | data[0];
    channels.at(channel).sensors()->txSnr() = Sensor();
    channels.at(channel).sensors()->txSnr()->value() =
        CmisFieldInfo::getSnr(value);
  }

  for (int channel = 0; channel < numMediaLanes(); channel++) {
    const uint8_t* data = getLaneValuePtr(CmisField::MEDIA_SNR, channel, 2);
    // SNR value are LSB.
    uint16_t value = data[1] << 8 | data[0];
    channels.at(channel).sensors()->rxSnr() = Sensor();

    // Compute SNR value from raw value, then apply Rx-SNR correction.
    double snrValue = applyRxSnrCorrection(value, CmisFieldInfo::getSnr(value));
    channels.at(channel).sensors()->rxSnr()->value() = snrValue;
  }

  return true;
}

std::string CmisModule::getQsfpString(CmisField field) const {
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

double CmisModule::getQsfpSensor(
    CmisField field,
    double (*conversion)(uint16_t value)) {
  auto info = QsfpFieldInfo<CmisField, CmisPages>::getQsfpFieldAddress(
      cmisFields, field);
  const uint8_t* data =
      getQsfpValuePtr(info.dataAddress, info.offset, info.length);
  return conversion(data[0] << 8 | data[1]);
}

TransmitterTechnology CmisModule::getQsfpTransmitterTechnology() const {
  auto info = QsfpFieldInfo<CmisField, CmisPages>::getQsfpFieldAddress(
      cmisFields, CmisField::MEDIA_INTERFACE_TECHNOLOGY);
  const uint8_t* data =
      getQsfpValuePtr(info.dataAddress, info.offset, info.length);

  uint8_t transTech = *data;
  if (transTech == DeviceTechnologyCmis::UNKNOWN_VALUE_CMIS) {
    return TransmitterTechnology::UNKNOWN;
    // TODO(T232092663): Fix this, introduce the TUNABLE_OPTICS
  } else if (
      (transTech <= DeviceTechnologyCmis::OPTICAL_MAX_VALUE_CMIS) ||
      (transTech == DeviceTechnologyCmis::C_BAND_TUNABLE_LASER_CMIS) ||
      (transTech == DeviceTechnologyCmis::L_BAND_TUNABLE_LASER_CMIS)) {
    return TransmitterTechnology::OPTICAL;
  } else {
    return TransmitterTechnology::COPPER;
  }
}

SignalFlags CmisModule::getSignalFlagInfo() {
  SignalFlags signalFlags = SignalFlags();

  if (!flatMem_) {
    signalFlags.txLos() = getSettingsValue(CmisField::TX_LOS_FLAG);
    signalFlags.rxLos() = getSettingsValue(CmisField::RX_LOS_FLAG);
    signalFlags.txLol() = getSettingsValue(CmisField::TX_LOL_FLAG);
    signalFlags.rxLol() = getSettingsValue(CmisField::RX_LOL_FLAG);
  }
  return signalFlags;
}

/*
 * updateVdmDiagsValLocation
 *
 * This function scans the VDM config pages by looking into each 2 byte
 * descriptors. It builds up the mapping from VDM config type to the VDM value
 * location (page, offset and length). These config could be module based config
 * or lane/datapath based config. The function updates the lowest offset of the
 * corresponding VDM data value. For config present in VDM page 0x20-23, the
 * corresponding data is present in VDM pages 0x24-27
 */
void CmisModule::updateVdmDiagsValLocation() {
  if (!cacheIsValid() || !isVdmSupported()) {
    QSFP_LOG(DBG2, this) << "Module does not support VDM diagnostics";
    return;
  }

  for (uint8_t group = 1; group <= vdmSupportedGroupsMax_; group++) {
    auto field = kVdmConfPages[group - 1];
    int page;
    int startOffset;
    int endOffset;
    int length;
    uint8_t data[128];
    const uint8_t* dataPtr = data;
    getQsfpFieldAddress(field, page, startOffset, length);
    endOffset = startOffset + length - 1;
    readFromCacheOrHw(field, data, true);

    // Each 2 byte descriptor:
    //    byte_1[7..0] -> VDM config type
    enum VdmConfigType lastConfig = UNSUPPORTED;
    for (auto offset = startOffset; offset <= endOffset;
         offset += kVdmDescriptorLength, dataPtr += kVdmDescriptorLength) {
      if (isValidVdmConfigType(dataPtr[1])) {
        if (static_cast<VdmConfigType>(dataPtr[1]) == lastConfig) {
          vdmConfigDataLocations_[lastConfig].vdmValLength += 2;
        } else {
          VdmDiagsLocationStatus vdmConfStatus;
          vdmConfStatus.vdmConfImplementedByModule = true;
          vdmConfStatus.vdmValPage = static_cast<CmisPages>(page + 4);
          vdmConfStatus.vdmValOffset = offset;
          vdmConfStatus.vdmValLength = 2;
          // Extract bits 7-4 from byte 0 (Even Address byte) for
          // LocalThresholdSetID
          vdmConfStatus.localThresholdSetID = (dataPtr[0] >> 4) & 0x0F;
          lastConfig = static_cast<VdmConfigType>(dataPtr[1]);
          vdmConfigDataLocations_[lastConfig] = vdmConfStatus;
        }
      }
    }
  }

  QSFP_LOG(DBG2, this) << "Module's VDM Config Locations found:";
  for (auto& it : vdmConfigDataLocations_) {
    QSFP_LOG(DBG2, this) << "VDM Config Type: " << static_cast<int>(it.first)
                         << ", Page: " << static_cast<int>(it.second.vdmValPage)
                         << ", Offset: " << it.second.vdmValOffset
                         << ", Length: " << it.second.vdmValLength;
  }
}

/*
 * getVdmDiagsValLocation
 *
 * For a given VDM config type, this function returns the VDM data location
 * values.
 */
CmisModule::VdmDiagsLocationStatus CmisModule::getVdmDiagsValLocation(
    VdmConfigType vdmConf) const {
  // Try to return VDM data location info now. If still no info available then
  // return empty values
  if (vdmConfigDataLocations_.find(vdmConf) == vdmConfigDataLocations_.end()) {
    return CmisModule::VdmDiagsLocationStatus{};
  }
  return vdmConfigDataLocations_.at(vdmConf);
}

/*
 * getCdbSymbolErrorHistogramLocked
 *
 * Return symbol error histogram data for all bins for a given datapath id and
 * the media/host side
 */
std::map<int32_t, SymErrHistogramBin>
CmisModule::getCdbSymbolErrorHistogramLocked(
    uint8_t datapathId,
    bool mediaSide) {
  std::map<int32_t, SymErrHistogramBin> histData;
  CdbCommandBlock commandBlockBuf;

  commandBlockBuf.createCdbCmdSymbolErrorHistogram(datapathId, mediaSide);
  auto ret = commandBlockBuf.cmisRunCdbCommand(qsfpImpl_);
  if (ret && commandBlockBuf.getCdbRlplLength() >= 1) {
    int numBins = commandBlockBuf.getCdbLplFlatMemory()[0];
    // Clamp numBins to prevent OOB read: each bin reads kCdbSymErrHistBinSize
    // bytes from the fixed 120-byte LPL buffer (first byte is numBins itself).
    numBins = std::min(
        numBins,
        (CdbCommandBlock::kCdbLplMemoryLength - 1) / kCdbSymErrHistBinSize);
    for (auto bin = 0; bin < numBins; bin++) {
      SymErrHistogramBin binHistData;
      binHistData.nbitSymbolErrorMax() = f16ToDouble(
          commandBlockBuf.getCdbLplFlatMemory()
              [bin * kCdbSymErrHistBinSize + kCdbSymErrHistMaxOffset],
          commandBlockBuf.getCdbLplFlatMemory()
              [bin * kCdbSymErrHistBinSize + kCdbSymErrHistMaxOffset + 1]);
      binHistData.nbitSymbolErrorAvg() = f16ToDouble(
          commandBlockBuf.getCdbLplFlatMemory()
              [bin * kCdbSymErrHistBinSize + kCdbSymErrHistAvgOffset],
          commandBlockBuf.getCdbLplFlatMemory()
              [bin * kCdbSymErrHistBinSize + kCdbSymErrHistAvgOffset + 1]);
      binHistData.nbitSymbolErrorCur() = f16ToDouble(
          commandBlockBuf.getCdbLplFlatMemory()
              [bin * kCdbSymErrHistBinSize + kCdbSymErrHistCurOffset],
          commandBlockBuf.getCdbLplFlatMemory()
              [bin * kCdbSymErrHistBinSize + kCdbSymErrHistCurOffset + 1]);
      histData[bin] = binHistData;
    }
  }
  return histData;
}

/*
 * getCdbSymbolErrorHistogramLocked
 *
 * Return symbol error histogram data for all bins for all datapaths for the
 * both media/host side
 */
std::map<std::string, CdbDatapathSymErrHistogram>
CmisModule::getCdbSymbolErrorHistogramLocked() {
  std::map<std::string, CdbDatapathSymErrHistogram> cdbDpSymErrHist;
  for (auto& [portName, hostLanes] : getPortNameToHostLanes()) {
    // Datapath Id is same as first lane Id
    int datapathId = *hostLanes.begin();
    cdbDpSymErrHist[portName].media() =
        getCdbSymbolErrorHistogramLocked(datapathId, true);
    cdbDpSymErrHist[portName].host() =
        getCdbSymbolErrorHistogramLocked(datapathId, false);
  }
  return cdbDpSymErrHist;
}

std::optional<VdmDiagsStats> CmisModule::getVdmDiagsStatsInfo() {
  VdmDiagsStats vdmStats;

  if (!isVdmSupported() || !cacheIsValid()) {
    return std::nullopt;
  }

  vdmStats.statsCollectionTme() = WallClockUtil::NowInSecFast();

  // Fill in channel SNR Media In (per global lane across all banks).
  for (const auto& [lane, snr] : getVdmLaneValuesU16(SNR_MEDIA_IN)) {
    vdmStats.eSnrMediaChannel()[lane] = snr;
  }

  // Lambda to extract a module-level BER or Frame Error value for a given VDM
  // config type (these are single values, not per-lane).
  auto captureVdmBerFrameErrorValues =
      [&](VdmConfigType vdmConfType) -> std::optional<double> {
    auto [data, length] = getVdmDataValPtr(vdmConfType);
    if (data) {
      return f16ToDouble(data.value()[0], data.value()[1]);
    }
    return std::nullopt;
  };

  // Fill in Media Pre FEC BER values
  if (auto berVal = captureVdmBerFrameErrorValues(PRE_FEC_BER_MEDIA_IN_MIN)) {
    vdmStats.preFecBerMediaMin() = berVal.value();
  }

  if (auto berVal = captureVdmBerFrameErrorValues(PRE_FEC_BER_MEDIA_IN_MAX)) {
    vdmStats.preFecBerMediaMax() = berVal.value();
  }

  if (auto berVal = captureVdmBerFrameErrorValues(PRE_FEC_BER_MEDIA_IN_AVG)) {
    vdmStats.preFecBerMediaAvg() = berVal.value();
  }

  if (auto berVal = captureVdmBerFrameErrorValues(PRE_FEC_BER_MEDIA_IN_CUR)) {
    vdmStats.preFecBerMediaCur() = berVal.value();
  }

  if (auto fecTailMax = captureVdmBerFrameErrorValues(FEC_TAIL_MEDIA_IN_MAX)) {
    vdmStats.fecTailMediaMax() = fecTailMax.value();
  }

  if (auto fecTailCurr =
          captureVdmBerFrameErrorValues(FEC_TAIL_MEDIA_IN_CURR)) {
    vdmStats.fecTailMediaCurr() = fecTailCurr.value();
  }

  // Fill in Host Pre FEC BER values
  if (auto berVal = captureVdmBerFrameErrorValues(PRE_FEC_BER_HOST_IN_MIN)) {
    vdmStats.preFecBerHostMin() = berVal.value();
  }

  if (auto berVal = captureVdmBerFrameErrorValues(PRE_FEC_BER_HOST_IN_MAX)) {
    vdmStats.preFecBerHostMax() = berVal.value();
  }

  if (auto berVal = captureVdmBerFrameErrorValues(PRE_FEC_BER_HOST_IN_AVG)) {
    vdmStats.preFecBerHostAvg() = berVal.value();
  }

  if (auto berVal = captureVdmBerFrameErrorValues(PRE_FEC_BER_HOST_IN_CUR)) {
    vdmStats.preFecBerHostCur() = berVal.value();
  }

  // Fill in Media Post FEC Errored Frames values
  if (auto errFrames = captureVdmBerFrameErrorValues(ERR_FRAME_MEDIA_IN_MIN)) {
    vdmStats.errFrameMediaMin() = errFrames.value();
  }

  if (auto errFrames = captureVdmBerFrameErrorValues(ERR_FRAME_MEDIA_IN_MAX)) {
    vdmStats.errFrameMediaMax() = errFrames.value();
  }

  if (auto errFrames = captureVdmBerFrameErrorValues(ERR_FRAME_MEDIA_IN_AVG)) {
    vdmStats.errFrameMediaAvg() = errFrames.value();
  }

  if (auto errFrames = captureVdmBerFrameErrorValues(ERR_FRAME_MEDIA_IN_CUR)) {
    vdmStats.errFrameMediaCur() = errFrames.value();
  }

  // Fill in Host Post FEC Errored Frame values
  if (auto errFrames = captureVdmBerFrameErrorValues(ERR_FRAME_HOST_IN_MIN)) {
    vdmStats.errFrameHostMin() = errFrames.value();
  }

  if (auto errFrames = captureVdmBerFrameErrorValues(ERR_FRAME_HOST_IN_MAX)) {
    vdmStats.errFrameHostMax() = errFrames.value();
  }

  if (auto errFrames = captureVdmBerFrameErrorValues(ERR_FRAME_HOST_IN_AVG)) {
    vdmStats.errFrameHostAvg() = errFrames.value();
  }

  if (auto errFrames = captureVdmBerFrameErrorValues(ERR_FRAME_HOST_IN_CUR)) {
    vdmStats.errFrameHostCur() = errFrames.value();
  }

  if (auto fecTailMax = captureVdmBerFrameErrorValues(FEC_TAIL_HOST_IN_MAX)) {
    vdmStats.fecTailHostMax() = fecTailMax.value();
  }

  if (auto fecTailCurr = captureVdmBerFrameErrorValues(FEC_TAIL_HOST_IN_CURR)) {
    vdmStats.fecTailHostCurr() = fecTailCurr.value();
  }

  // Fill in VDM Advance group3 performance monitoring info
  if (isVdmSupported(3)) {
    // Lambda to read the per-global-lane VDM PM value for the given VDM Config
    auto getVdmPmLaneValues = [this](VdmConfigType vdmConf) {
      return getVdmLaneValuesF16(vdmConf);
    };

    // PAM4 Level0
    auto sdL0Map = getVdmPmLaneValues(PAM4_LEVEL0_STANDARD_DEVIATION_LINE);
    for (auto [lane, sdL0] : sdL0Map) {
      vdmStats.pam4Level0SDLine()[lane] = sdL0;
    }

    // PAM4 Level1
    auto sdL1Map = getVdmPmLaneValues(PAM4_LEVEL1_STANDARD_DEVIATION_LINE);
    for (auto [lane, sdL1] : sdL1Map) {
      vdmStats.pam4Level1SDLine()[lane] = sdL1;
    }

    // PAM4 Level2
    auto sdL2Map = getVdmPmLaneValues(PAM4_LEVEL2_STANDARD_DEVIATION_LINE);
    for (auto [lane, sdL2] : sdL2Map) {
      vdmStats.pam4Level2SDLine()[lane] = sdL2;
    }

    // PAM4 Level3
    auto sdL3Map = getVdmPmLaneValues(PAM4_LEVEL3_STANDARD_DEVIATION_LINE);
    for (auto [lane, sdL3] : sdL3Map) {
      vdmStats.pam4Level3SDLine()[lane] = sdL3;
    }

    // PAM4 MPI
    auto mpiMap = getVdmPmLaneValues(PAM4_MPI_LINE);
    for (auto [lane, mpi] : mpiMap) {
      vdmStats.pam4MPILine()[lane] = mpi;
    }
  }

  // Fill in channel LTP Media In (per global lane across all banks).
  for (const auto& [lane, ltp] : getVdmLaneValuesU16(PAM4_LTP_MEDIA_IN)) {
    vdmStats.pam4LtpMediaChannel()[lane] = ltp;
  }

  return vdmStats;
}

/*
 * getVdmPerfMonitorStats
 *
 * This function extracts all VDM info from the VDM specific pages and then
 * returns VDM Performance Monitoring Diags stats.
 */
std::optional<VdmPerfMonitorStats> CmisModule::getVdmPerfMonitorStats() {
  VdmPerfMonitorStats vdmStats;

  if (!isVdmSupported() || !cacheIsValid()) {
    return std::nullopt;
  }

  vdmStats.statsCollectionTme() = WallClockUtil::NowInSecFast();
  vdmStats.intervalStartTime() = vdmIntervalStartTime_;

  if (!fillVdmPerfMonitorSnr(vdmStats)) {
    QSFP_LOG(ERR, this) << "Failed to get VDM Perf Monitor SNR";
  }
  if (!fillVdmPerfMonitorBer(vdmStats)) {
    QSFP_LOG(ERR, this) << "Failed to get VDM Perf Monitor BER";
  }
  if (!fillVdmPerfMonitorFecErr(vdmStats)) {
    QSFP_LOG(ERR, this) << "Failed to get VDM Perf Monitor FEC Error Rate";
  }
  if (!fillVdmPerfMonitorFecTail(vdmStats)) {
    QSFP_LOG(ERR, this) << "Failed to get VDM Perf Monitor FEC Tail";
  }
  if (!fillVdmPerfMonitorLtp(vdmStats)) {
    QSFP_LOG(ERR, this) << "Failed to get VDM Perf Monitor LTP";
  }
  if (!fillVdmPerfMonitorPam4Data(vdmStats)) {
    QSFP_LOG(ERR, this) << "Failed to get VDM Perf Monitor PAM4 data";
  }
  if (!fillVdmPerfMonitorPam4AlarmData(vdmStats)) {
    QSFP_LOG(ERR, this) << "Failed to get VDM Perf Monitor PAM4 alarm data";
  }
  if (!fillVdmPerfMonitorCoherentVdm(vdmStats)) {
    QSFP_LOG(DBG5, this) << "Coherent VDM stats not available";
  }
  if (!fillVdmPerfMonitorFecPm(vdmStats)) {
    QSFP_LOG(DBG5, this) << "FEC PM stats not available";
  }
  if (!fillVdmPerfMonitorLinkPm(vdmStats)) {
    QSFP_LOG(DBG5, this) << "Link PM stats not available";
  }

  QSFP_LOG(DBG5, this) << "Read VDM Performance Monitoring stats";
  QSFP_LOG(DBG5, this) << "Stats Collection Time: "
                       << vdmStats.statsCollectionTme().value();
  QSFP_LOG(DBG5, this) << "Read " << vdmStats.mediaPortVdmStats()->size()
                       << " ports on media side and "
                       << vdmStats.hostPortVdmStats()->size()
                       << " ports on host side";
  for (auto& [portName, mediaVdmStats] : vdmStats.mediaPortVdmStats().value()) {
    QSFP_LOG(DBG5, this) << "Port: " << portName
                         << " recorded media side stats for "
                         << mediaVdmStats.laneSNR()->size() << " lanes";
  }
  return vdmStats;
}

/*
 * getTunableLaserStatus
 *
 * This function extracts tunable laser status information from the module
 * and returns the laser status and current frequency if available.
 */
std::optional<TunableLaserStatus> CmisModule::getTunableLaserStatus() {
  if (!isTunableOptics()) {
    return std::nullopt;
  }

  TunableLaserStatus tunableLaserStatus;
  // Initialize with default value to avoid bad_optional_field_access
  tunableLaserStatus.tuningStatus() =
      LaserStatusBitMask::LASER_TUNE_NOT_IN_PROGRESS;
  tunableLaserStatus.wavelengthLockingStatus() =
      LaserStatusBitMask::WAVELENGTH_LOCKED;
  tunableLaserStatus.laserFrequencyMhz() = kDefaultFrequencyMhz;
  tunableLaserStatus.laserStatusFlagsByte() = 0;
  // Read laser status from MEDIA_TX_1_LAS_STAT field
  uint8_t laserStatusByte;
  readCmisField(CmisField::MEDIA_TX_1_LAS_STAT, &laserStatusByte);

  // Read laser status flags from MEDIA_TX_1_LAS_STAT_FLAGS field
  uint8_t laserStatusFlagsByte = 0;
  readCmisField(CmisField::MEDIA_TX_1_LAS_STAT_FLAGS, &laserStatusFlagsByte);
  tunableLaserStatus.laserStatusFlagsByte() = laserStatusFlagsByte;

  // Laser status byte bit 7 indicates if the laser is in progress of tuning.
  // Value 0: tuning not in progress, Value 1: tuning in progress
  if (laserStatusByte &
      static_cast<uint8_t>(LaserStatusBitMask::LASER_TUNE_IN_PROGRESS)) {
    tunableLaserStatus.tuningStatus() =
        LaserStatusBitMask::LASER_TUNE_IN_PROGRESS;
  }

  if (laserStatusByte &
      static_cast<uint8_t>(LaserStatusBitMask::WAVELENGTH_UNLOCKED)) {
    tunableLaserStatus.wavelengthLockingStatus() =
        LaserStatusBitMask::WAVELENGTH_UNLOCKED;
  }

  // Read current laser frequency from MEDIA_TX_1_CURR_LAS_FREQ field (4
  // bytes)
  uint8_t frequencyBytes[4] = {0};
  readCmisField(CmisField::MEDIA_TX_1_CURR_LAS_FREQ, frequencyBytes);

  // Convert 4 bytes to frequency in MHz
  // According to CMIS spec, this is stored as a 32-bit unsigned integer in
  // MHz
  uint32_t frequencyMhz = (frequencyBytes[0] << 24) |
      (frequencyBytes[1] << 16) | (frequencyBytes[2] << 8) | frequencyBytes[3];

  tunableLaserStatus.laserFrequencyMhz() = static_cast<int64_t>(frequencyMhz);

  QSFP_LOG(INFO, this) << fmt::format(
      "Laser status byte: 0x{:X}, laserStatusFlagsByte: 0x{:X}, "
      "frequency_MHz: {}, TuningStatus: {}, WavelengthLockingStatus: {}",
      laserStatusByte,
      laserStatusFlagsByte,
      frequencyMhz,
      apache::thrift::util::enumNameSafe(
          tunableLaserStatus.tuningStatus().value()),
      apache::thrift::util::enumNameSafe(
          tunableLaserStatus.wavelengthLockingStatus().value()));
  return tunableLaserStatus;
}

/*
 * getVdmPerfMonitorStatsForOds
 *
 * Consolidate the VDM stats for publishing to ODS/Fbagent
 * - For Pre FEC BER and Post FEC BER -> Report Max value
 * - For SNR -> Report Min value across all lanes
 * - For PAM4 SD, MPI, LTP -> Report Max value across all lanes
 */
VdmPerfMonitorStatsForOds CmisModule::getVdmPerfMonitorStatsForOds(
    VdmPerfMonitorStats& vdmPerfMonStats) {
  VdmPerfMonitorStatsForOds vdmPerfMonOdsStats;

  vdmPerfMonOdsStats.statsCollectionTme() =
      vdmPerfMonStats.statsCollectionTme().value();

  // Lambda to report Min and Max value from a map of lane id to lane values
  auto findMinMax =
      [](std::map<int32_t, double>& vdmStats) -> std::pair<double, double> {
    double min = std::numeric_limits<double>::max();
    double max = std::numeric_limits<double>::lowest();
    for (auto& [lane, vdmVal] : vdmStats) {
      if (vdmVal > max) {
        max = vdmVal;
      }
      if (vdmVal < min) {
        min = vdmVal;
      }
    }
    return std::make_pair(min, max);
  };

  // Media side stats consolidation
  for (auto& [portName, portMediaVdmStats] :
       vdmPerfMonStats.mediaPortVdmStats().value()) {
    // Report BER and Post Fec BER, need to report Max only
    vdmPerfMonOdsStats.mediaPortVdmStats()[portName].datapathBERMax() =
        portMediaVdmStats.datapathBER()->max().value();
    vdmPerfMonOdsStats.mediaPortVdmStats()[portName]
        .datapathErroredFramesMax() =
        portMediaVdmStats.datapathErroredFrames()->max().value();

    if (auto fecTailMax = portMediaVdmStats.fecTailMax()) {
      vdmPerfMonOdsStats.mediaPortVdmStats()[portName].fecTailMax() =
          fecTailMax.value();
    }

    // For SNR, report Min value among all lanes
    vdmPerfMonOdsStats.mediaPortVdmStats()[portName].laneSNRMin() =
        findMinMax(portMediaVdmStats.laneSNR().value()).first;

    // For PAM4 SD, MPI, LTP, report Max value among all lanes
    vdmPerfMonOdsStats.mediaPortVdmStats()[portName].lanePam4Level0SDMax() =
        findMinMax(portMediaVdmStats.lanePam4Level0SD().value()).second;
    vdmPerfMonOdsStats.mediaPortVdmStats()[portName].lanePam4Level1SDMax() =
        findMinMax(portMediaVdmStats.lanePam4Level1SD().value()).second;
    vdmPerfMonOdsStats.mediaPortVdmStats()[portName].lanePam4Level2SDMax() =
        findMinMax(portMediaVdmStats.lanePam4Level2SD().value()).second;
    vdmPerfMonOdsStats.mediaPortVdmStats()[portName].lanePam4Level3SDMax() =
        findMinMax(portMediaVdmStats.lanePam4Level3SD().value()).second;
    vdmPerfMonOdsStats.mediaPortVdmStats()[portName].lanePam4MPIMax() =
        findMinMax(portMediaVdmStats.lanePam4MPI().value()).second;
    vdmPerfMonOdsStats.mediaPortVdmStats()[portName].lanePam4LTPMax() =
        findMinMax(portMediaVdmStats.lanePam4LTP().value()).second;
  }

  // Host side stats consolidation
  for (auto& [portName, portHostVdmStats] :
       vdmPerfMonStats.hostPortVdmStats().value()) {
    // Report BER and Post Fec BER, need to report Max only
    vdmPerfMonOdsStats.hostPortVdmStats()[portName].datapathBERMax() =
        portHostVdmStats.datapathBER()->max().value();
    vdmPerfMonOdsStats.hostPortVdmStats()[portName].datapathErroredFramesMax() =
        portHostVdmStats.datapathErroredFrames()->max().value();

    if (auto fecTailMax = portHostVdmStats.fecTailMax()) {
      vdmPerfMonOdsStats.hostPortVdmStats()[portName].fecTailMax() =
          *fecTailMax;
    }
  }

  return vdmPerfMonOdsStats;
}

TransceiverModuleIdentifier CmisModule::getIdentifier() {
  return (TransceiverModuleIdentifier)getSettingsValue(CmisField::IDENTIFIER);
}

void CmisModule::setQsfpFlatMem() {
  uint8_t flatMem;
  int offset;
  int length;
  int dataAddress;

  if (!present_) {
    throw FbossError("Failed setting QSFP flatMem: QSFP is not present");
  }

  getQsfpFieldAddress(CmisField::FLAT_MEM, dataAddress, offset, length);
  getQsfpValue(dataAddress, offset, length, &flatMem);
  flatMem_ = flatMem & (1 << 7);
  QSFP_LOG(DBG3, this) << "Detected QSFP, flatMem=" << flatMem_;
}

const uint8_t*
CmisModule::getQsfpValuePtr(int dataAddress, int offset, int length) const {
  /* if the cached values are not correct */
  if (!cacheIsValid()) {
    throw FbossError("Qsfp is either not present or the data is not read");
  }
  if (dataAddress == static_cast<int>(CmisPages::LOWER)) {
    CHECK_LE(offset + length, sizeof(lowerPage_));
    /* Copy data from the cache */
    return (lowerPage_ + offset);
  } else {
    offset -= MAX_QSFP_PAGE_SIZE;
    CHECK_GE(offset, 0);
    CHECK_LE(offset, MAX_QSFP_PAGE_SIZE);

    // If this is a flatMem module, we will only have PAGE00 here.
    // Only when flatMem is false will we have data for other pages.

    if (dataAddress == static_cast<int>(CmisPages::PAGE00)) {
      CHECK_LE(offset + length, sizeof(page0_));
      return (page0_ + offset);
    }

    if (flatMem_) {
      throw FbossError(
          "Accessing upper page ", dataAddress, " on flatMem module.");
    }

    switch (static_cast<CmisPages>(dataAddress)) {
      case CmisPages::PAGE01:
        CHECK_LE(offset + length, sizeof(page01_));
        return (page01_ + offset);
      case CmisPages::PAGE02:
        CHECK_LE(offset + length, sizeof(page02_));
        return (page02_ + offset);
      case CmisPages::PAGE04:
        CHECK_LE(offset + length, sizeof(page04_));
        return (page04_ + offset);
      // Pages 10h/11h/13h are stored per bank; index [0] is bank 0, which is
      // also the value seen by non-banked (bank-agnostic) reads. Per-bank
      // access for banks > 0 goes through getBankedQsfpValuePtr.
      case CmisPages::PAGE10:
        CHECK_LE(offset + length, MAX_QSFP_PAGE_SIZE);
        return (page10_[0].data() + offset);
      case CmisPages::PAGE11:
        CHECK_LE(offset + length, MAX_QSFP_PAGE_SIZE);
        return (page11_[0].data() + offset);
      case CmisPages::PAGE12:
        CHECK_LE(offset + length, sizeof(page12_));
        return (page12_ + offset);
      case CmisPages::PAGE13:
        CHECK_LE(offset + length, MAX_QSFP_PAGE_SIZE);
        return (page13_[0].data() + offset);
      case CmisPages::PAGE14:
        CHECK_LE(offset + length, MAX_QSFP_PAGE_SIZE);
        return (page14_[0].data() + offset);
      case CmisPages::PAGE20:
        CHECK_LE(offset + length, sizeof(page20_));
        return (page20_ + offset);
      case CmisPages::PAGE21:
        CHECK_LE(offset + length, sizeof(page21_));
        return (page21_ + offset);
      case CmisPages::PAGE22:
        CHECK_LE(offset + length, sizeof(page22_));
        return (page22_ + offset);
      case CmisPages::PAGE23:
        CHECK_LE(offset + length, sizeof(page23_));
        return (page23_ + offset);
      case CmisPages::PAGE24:
        CHECK_LE(offset + length, MAX_QSFP_PAGE_SIZE);
        return (page24_[0].data() + offset);
      case CmisPages::PAGE25:
        CHECK_LE(offset + length, MAX_QSFP_PAGE_SIZE);
        return (page25_[0].data() + offset);
      case CmisPages::PAGE26:
        CHECK_LE(offset + length, MAX_QSFP_PAGE_SIZE);
        return (page26_[0].data() + offset);
      case CmisPages::PAGE27:
        CHECK_LE(offset + length, MAX_QSFP_PAGE_SIZE);
        return (page27_[0].data() + offset);
      case CmisPages::PAGE34:
        CHECK_LE(offset + length, sizeof(page34_));
        return (page34_ + offset);
      case CmisPages::PAGE35:
        CHECK_LE(offset + length, sizeof(page35_));
        return (page35_ + offset);
      case CmisPages::PAGE38:
        CHECK_LE(offset + length, sizeof(page38_));
        return (page38_ + offset);
      case CmisPages::PAGE45:
        CHECK_LE(offset + length, sizeof(page45_));
        return (page45_ + offset);
      default:
        throw FbossError("Invalid Data Address 0x%d", dataAddress);
    }
  }
}

const uint8_t* CmisModule::getBankedQsfpValuePtr(
    int dataAddress,
    int offset,
    int length,
    uint8_t bank) const {
  // Bank 0 is index 0 of the same per-bank buffer; reuse the common accessor.
  if (bank == 0) {
    return getQsfpValuePtr(dataAddress, offset, length);
  }
  if (!cacheIsValid()) {
    throw FbossError("Qsfp is either not present or the data is not read");
  }
  const BankedPage* page = nullptr;
  // Only the per-bank pages have cases here; the default handles every other
  // CmisPages value, so the unlisted enumerators are intentional.
  // NOLINTNEXTLINE(clang-diagnostic-switch-enum)
  switch (static_cast<CmisPages>(dataAddress)) {
    case CmisPages::PAGE10:
      page = &page10_;
      break;
    case CmisPages::PAGE11:
      page = &page11_;
      break;
    case CmisPages::PAGE13:
      page = &page13_;
      break;
    case CmisPages::PAGE14:
      page = &page14_;
      break;
    case CmisPages::PAGE24:
      page = &page24_;
      break;
    case CmisPages::PAGE25:
      page = &page25_;
      break;
    case CmisPages::PAGE26:
      page = &page26_;
      break;
    case CmisPages::PAGE27:
      page = &page27_;
      break;
    default:
      throw FbossError(
          fmt::format("Page {:#x}", dataAddress),
          " is not cached per-bank; bank ",
          static_cast<int>(bank),
          " is unavailable");
  }
  offset -= MAX_QSFP_PAGE_SIZE;
  CHECK_GE(offset, 0);
  CHECK_LE(offset + length, MAX_QSFP_PAGE_SIZE);
  if (bank >= page->size()) {
    // This bank wasn't read this refresh -- e.g. a diagnostic page (14h or VDM
    // 24h-27h) that is only read for every bank when the module is READY.
    // Return a zero-filled view so per-lane consumers iterating all banks
    // (numHostLanes / numMediaLanes) see "no data" for the unread banks,
    // consistent with how an unpopulated bank-0 read returns zeros rather than
    // throwing.
    static const std::array<uint8_t, MAX_QSFP_PAGE_SIZE> kUnreadBank{};
    return kUnreadBank.data() + offset;
  }
  return (*page)[bank].data() + offset;
}

const uint8_t* CmisModule::getLaneValuePtr(
    CmisField field,
    int globalLane,
    int bytesPerLane) const {
  int dataAddress, offset, length;
  getQsfpFieldAddress(field, dataAddress, offset, length);
  // The field must hold bytesPerLane bytes for each of the (up to)
  // kMaxOsfpNumLanes intra-bank lanes; catch a mismatched bytesPerLane here
  // rather than silently returning a pointer into the wrong lane.
  CHECK_LE(bytesPerLane * kMaxOsfpNumLanes, length);
  uint8_t bank = globalLane / kMaxOsfpNumLanes;
  int intraLane = globalLane % kMaxOsfpNumLanes;
  return getBankedQsfpValuePtr(
      dataAddress, offset + intraLane * bytesPerLane, bytesPerLane, bank);
}

bool CmisModule::getLaneFlagSet(CmisField field, int globalLane) const {
  int dataAddress, offset, length;
  getQsfpFieldAddress(field, dataAddress, offset, length);
  // A flag field is a single byte holding one bit per intra-bank lane; assert
  // the contract so a wider field can't be silently misread as 1 byte.
  CHECK_EQ(length, 1);
  uint8_t bank = globalLane / kMaxOsfpNumLanes;
  int intraLane = globalLane % kMaxOsfpNumLanes;
  const uint8_t* data = getBankedQsfpValuePtr(dataAddress, offset, 1, bank);
  return data[0] & (1 << intraLane);
}

uint8_t CmisModule::getLaneNibble(CmisField field, int globalLane) const {
  int dataAddress, offset, length;
  getQsfpFieldAddress(field, dataAddress, offset, length);
  uint8_t bank = globalLane / kMaxOsfpNumLanes;
  int intraLane = globalLane % kMaxOsfpNumLanes;
  // A nibble-packed field holds two intra-bank lanes per byte.
  const uint8_t* data =
      getBankedQsfpValuePtr(dataAddress, offset, length, bank);
  return (data[intraLane / 2] >> ((intraLane % 2) * 4)) & 0xF;
}

void CmisModule::readBankedPage(CmisField field, BankedPage& dest) {
  // dest is pre-sized to getMaxNumBanks() (>= 1) by cacheMaxNumBanks().
  uint8_t numBanks = getMaxNumBanks();
  if (numBanks <= 1) {
    // Single-bank module: legacy behavior, no bank-select write.
    readCmisField(field, dest.at(0).data());
  } else {
    // Multi-bank: every read must explicitly select its bank (the bank-select
    // register is sticky). Read bank 0 last so the module is left selected on
    // bank 0 -- pages that are still read bank-agnostically (12h/VDM) rely
    // on bank 0 being the active selection.
    for (int bank = numBanks - 1; bank >= 0; --bank) {
      readCmisField(
          field, dest.at(bank).data(), /*skipBankAndPageChange=*/false, bank);
    }
  }
}

void CmisModule::readSnrDiagPageLocked(BankedPage& dest) {
  // Page 14h is a multiplexed diagnostic page; DIAG_SEL selects which feature
  // (SNR vs BER) its data region reflects. DIAG_SEL lives on the banked page
  // itself, so it must be written under each bank's selection before the read.
  // dest is pre-sized to getMaxNumBanks() (>= 1) by cacheMaxNumBanks().
  uint8_t numBanks = getMaxNumBanks();
  if (numBanks <= 1) {
    // Single-bank module: legacy behavior, no bank-select write.
    setDiagSel(DiagnosticFeatureEncoding::SNR);
    readCmisField(CmisField::PAGE_UPPER14H, dest.at(0).data());
  } else {
    // Multi-bank: select SNR and read 14h for each bank. Bank 0 is done last so
    // the module is left selected on bank 0 for the subsequent bank-agnostic
    // reads (VDM).
    for (int bank = numBanks - 1; bank >= 0; --bank) {
      setDiagSel(DiagnosticFeatureEncoding::SNR, bank);
      readCmisField(
          CmisField::PAGE_UPPER14H,
          dest.at(bank).data(),
          /*skipBankAndPageChange=*/false,
          bank);
    }
  }
}

int CmisModule::getDiagSelLatchWaitUsec() const {
  const auto partNumber = getQsfpString(CmisField::PART_NUMBER);
  return std::find(
             kSlowDiagSelectPartNumbers.begin(),
             kSlowDiagSelectPartNumbers.end(),
             partNumber) != kSlowDiagSelectPartNumbers.end()
      ? kUsecDiagSelectLatchWaitSlow
      : kUsecDiagSelectLatchWait;
}

void CmisModule::setDiagSel(
    DiagnosticFeatureEncoding diagSel,
    std::optional<uint8_t> bank,
    std::optional<int> latchWaitUsec) {
  uint8_t desired = static_cast<uint8_t>(diagSel);
  uint8_t current = 0;
  readCmisField(
      CmisField::DIAG_SEL, &current, /*skipBankAndPageChange=*/false, bank);
  if (current == desired) {
    return;
  }
  writeCmisField(
      CmisField::DIAG_SEL, &desired, /*skipBankAndPageChange=*/false, bank);
  /* sleep override */
  usleep(
      latchWaitUsec.has_value() ? *latchWaitUsec : getDiagSelLatchWaitUsec());
}

/*
 * readFromCacheOrHw
 *
 * This function reads the register field from either register cache or from
 * hardware (if the cache is not available). This function assumes the input
 * data pointer has the space allocated for the entire given CMIS register space
 */
void CmisModule::readFromCacheOrHw(
    CmisField field,
    uint8_t* data,
    bool forcedReadFromHw) {
  int offset;
  int length;
  int dataAddress;
  getQsfpFieldAddress(field, dataAddress, offset, length);
  if (cacheIsValid() && !forcedReadFromHw) {
    getQsfpValue(dataAddress, offset, length, data);
  } else {
    readCmisField(field, data);
  }
}

RawDOMData CmisModule::getRawDOMData() {
  lock_guard<std::mutex> g(qsfpModuleMutex_);
  RawDOMData data;
  if (present_) {
    *data.lower() = IOBuf::wrapBufferAsValue(lowerPage_, MAX_QSFP_PAGE_SIZE);
    *data.page0() = IOBuf::wrapBufferAsValue(page0_, MAX_QSFP_PAGE_SIZE);
    data.page10() =
        IOBuf::wrapBufferAsValue(page10_[0].data(), MAX_QSFP_PAGE_SIZE);
    data.page11() =
        IOBuf::wrapBufferAsValue(page11_[0].data(), MAX_QSFP_PAGE_SIZE);
  }
  return data;
}

DOMDataUnion CmisModule::getDOMDataUnion() {
  lock_guard<std::mutex> g(qsfpModuleMutex_);
  CmisData cmisData;
  if (present_) {
    *cmisData.lower() =
        IOBuf::wrapBufferAsValue(lowerPage_, MAX_QSFP_PAGE_SIZE);
    *cmisData.page0() = IOBuf::wrapBufferAsValue(page0_, MAX_QSFP_PAGE_SIZE);
    if (!flatMem_) {
      cmisData.page01() = IOBuf::wrapBufferAsValue(page01_, MAX_QSFP_PAGE_SIZE);
      cmisData.page02() = IOBuf::wrapBufferAsValue(page02_, MAX_QSFP_PAGE_SIZE);
      cmisData.page10() =
          IOBuf::wrapBufferAsValue(page10_[0].data(), MAX_QSFP_PAGE_SIZE);
      cmisData.page11() =
          IOBuf::wrapBufferAsValue(page11_[0].data(), MAX_QSFP_PAGE_SIZE);
      if (isTunableOptics()) {
        cmisData.page12() =
            IOBuf::wrapBufferAsValue(page12_, MAX_QSFP_PAGE_SIZE);
      }
      cmisData.page13() =
          IOBuf::wrapBufferAsValue(page13_[0].data(), MAX_QSFP_PAGE_SIZE);
      cmisData.page14() =
          IOBuf::wrapBufferAsValue(page14_[0].data(), MAX_QSFP_PAGE_SIZE);
      cmisData.page20() = IOBuf::wrapBufferAsValue(page20_, MAX_QSFP_PAGE_SIZE);
      cmisData.page21() = IOBuf::wrapBufferAsValue(page21_, MAX_QSFP_PAGE_SIZE);
      cmisData.page22() = IOBuf::wrapBufferAsValue(page22_, MAX_QSFP_PAGE_SIZE);
      cmisData.page23() = IOBuf::wrapBufferAsValue(page23_, MAX_QSFP_PAGE_SIZE);
      cmisData.page24() =
          IOBuf::wrapBufferAsValue(page24_[0].data(), MAX_QSFP_PAGE_SIZE);
      cmisData.page25() =
          IOBuf::wrapBufferAsValue(page25_[0].data(), MAX_QSFP_PAGE_SIZE);
      cmisData.page26() =
          IOBuf::wrapBufferAsValue(page26_[0].data(), MAX_QSFP_PAGE_SIZE);
      cmisData.page27() =
          IOBuf::wrapBufferAsValue(page27_[0].data(), MAX_QSFP_PAGE_SIZE);
    }
  }
  cmisData.timeCollected() = lastQsfpDataUpdateTime_;
  DOMDataUnion data;
  data.cmis() = cmisData;
  return data;
}

void CmisModule::getFieldValue(CmisField fieldName, uint8_t* fieldValue) const {
  lock_guard<std::mutex> g(qsfpModuleMutex_);
  int offset;
  int length;
  int dataAddress;
  getQsfpFieldAddress(fieldName, dataAddress, offset, length);
  getQsfpValue(dataAddress, offset, length, fieldValue);
}

void CmisModule::getFieldValueLocked(CmisField fieldName, uint8_t* fieldValue)
    const {
  // Expect lock being held here.
  int offset;
  int length;
  int dataAddress;
  getQsfpFieldAddress(fieldName, dataAddress, offset, length);
  getQsfpValue(dataAddress, offset, length, fieldValue);
}

void CmisModule::updateQsfpData(bool allPages) {
  // expects the lock to be held
  if (!present_) {
    return;
  }
  try {
    QSFP_LOG(DBG2, this) << "Performing " << ((allPages) ? "full" : "partial")
                         << " qsfp data cache refresh";
    readCmisField(CmisField::PAGE_LOWER, lowerPage_);
    lastQsfpDataUpdateTime_ = std::time(nullptr);
    dirty_ = false;
    setQsfpFlatMem();
    cacheMaxNumBanks();
    cacheCmisRevision();

    readCmisField(CmisField::PAGE_UPPER00H, page0_);
    if (!flatMem_) {
      // 10h/11h/13h/14h carry per-lane/per-datapath data and are read for every
      // bank on multi-bank (CPO) modules. 12h/VDM remain bank-0 for now (12h is
      // tunable-only; VDM needs per-bank location handling) -- tracked as
      // follow-up.
      readBankedPage(CmisField::PAGE_UPPER10H, page10_);
      readBankedPage(CmisField::PAGE_UPPER11H, page11_);
      if (isTunableOptics()) {
        readCmisField(CmisField::PAGE_UPPER12H, page12_);
      }

      bool isReady = moduleStateFromStatusByte(getSettingsValue(
                         CmisField::MODULE_STATE)) == CmisModuleState::READY;
      if (isReady) {
        // A full refresh implies a (re)discovered/reprogrammed module; abort
        // any in-flight async VDM capture and clear a lingering freeze.
        if (allPages) {
          resetVdmCaptureStateLocked();
        }
        readSnrDiagPageLocked(page14_);
        updateVdmCacheLocked();
      }
    }

    if (!allPages) {
      // Update the application capabilities once we have read from eeprom.
      // Note that this function may also need to read information from page01
      // which is only read when allPages_ is true. However, we always read
      // allPages the first time so we'll have cached information of page01
      // (when applicable) by now. That information is also static so it's okay
      // that we are not reading it again.
      getApplicationCapabilities();
      // The information on the following pages are static. Thus no need to
      // fetch them every time. We just need to do it when we first retriving
      // the data from this module.
      return;
    }

    if (!flatMem_) {
      readCmisField(CmisField::PAGE_UPPER01H, page01_);
      readCmisField(CmisField::PAGE_UPPER02H, page02_);
      readBankedPage(CmisField::PAGE_UPPER13H, page13_);

      // Cache firmware build number from CDB Get Firmware Info command
      cachedFwBuildNumber_ = fetchFwBuildNumberFromCdb();

      // Cache CDB write delay based on media type
      cachedCdbWriteDelayUsec_ = getFwUpgradeCdbWriteDelayUsec();
    }

    // Update the application capabilities once we have read from eeprom
    getApplicationCapabilities();
  } catch (const std::exception& ex) {
    // No matter what kind of exception throws, we need to set the dirty_ flag
    // to true.
    dirty_ = true;
    QSFP_LOG(ERR, this) << "Error update data: " << ex.what();
    throw;
  }
}

/*
 * applyHostControlledInputEquilizerTx
 * Sets the InputEquilizerTx value for a given lane.
 */
void CmisModule::applyHostControlledInputEquilizerTx(
    uint8_t lane,
    uint8_t value,
    uint8_t bank) {
  // lane is the intra-bank lane; INPUT_EQ_TX_* lives on banked page 10h, so
  // select the port's bank.
  auto itr = laneToInputEqTxField.find(lane);
  if (itr == laneToInputEqTxField.end()) {
    QSFP_LOG(WARN, this) << fmt::format(
        "Warning: lane {:#d} is out of range for InputEqTx map", lane);
    return;
  }
  const auto& [shift, field] = itr->second;
  // only 4 bits are applicable.
  uint8_t valueToApply = value & 0xF;
  if (valueToApply != value) {
    QSFP_LOG(WARN, this) << fmt::format(
        "Warning: Value applied {:#d} is out of range for InputEqTx 4 bits",
        value);
    return;
  }
  valueToApply = valueToApply << shift;
  // Read field first. Apply the value for the lane, then write it back
  uint8_t currentVal = 0;
  readCmisField(field, &currentVal, false, bank);
  // Zero out the correct nibble
  currentVal &= ~(0xF << shift);
  // apply current value
  currentVal |= valueToApply;
  writeCmisField(field, &currentVal, false, bank);
}

/*
 * setExplicitControl
 *
 * Sets the HostControlledInputEquilizerTx based on driverPeaking values in
 * TransceiverPortState. Returns 0x1 when the explicit control is set.
 */

uint8_t CmisModule::setExplicitControl(
    const TransceiverPortState& state,
    const uint8_t laneMask) {
  uint8_t retVal = 0;
  if (!FLAGS_enable_explicit_control) {
    return retVal;
  }
  // For now, this is only applicable to LPO
  if (!isLpoModule()) {
    return retVal;
  }
  auto driverPeaking = state.driverPeaking;
  if (!driverPeaking) {
    QSFP_LOG(WARN, this) << "Warning: Driver peaking not set for LPO Module";
    return retVal;
  }

  std::vector<uint8_t> lanes;
  for (uint8_t lane = 0; lane < 8; ++lane) {
    if (laneMask & (1 << lane)) {
      lanes.push_back(lane);
    }
  }

  if (driverPeaking->size() < lanes.size()) {
    QSFP_LOG(WARN, this) << fmt::format(
        "Warning: Driver peaking override count {:#d} is smaller than Lane count {:#d}",
        driverPeaking->size(),
        lanes.size());
    return retVal;
  }
  // lanes above are intra-bank; INPUT_EQ_TX_* is on banked page 10h, so target
  // the port's bank.
  const uint8_t bank = laneToBank(state.startHostLane);
  for (const auto& lane : lanes) {
    auto itr = driverPeaking->find(lane);
    if (itr == driverPeaking->end()) {
      QSFP_LOG(WARN, this) << fmt::format(
          "Warning: Driver peaking override for lane {:#d} is missing", lane);
      continue;
    }
    retVal = 0x1;
    applyHostControlledInputEquilizerTx(lane, itr->second, bank);
  }
  return retVal;
}

/*
 * setApplicationSelectCode
 *
 * Set the Application code to the optics for just one software port. If it
 * needs cleanup of existing config first then the lanes are released first
 * before programming new application select code
 */
void CmisModule::setApplicationSelectCode(
    uint8_t apSelCode,
    uint8_t mediaInterfaceCode,
    const TransceiverPortState& state,
    uint8_t numHostLanes,
    uint8_t hostLaneMask) {
  // A port's lanes are confined to one bank; program that bank using
  // intra-bank lane offsets. dataPathId is the intra-bank lane index and is
  // also the on-wire DATA_PATH_ID field value (which is bank-relative).
  const uint8_t bank = laneToBank(state.startHostLane);
  const uint8_t dataPathId = laneInBank(state.startHostLane);

  // We can't use numHostLanes() to get the hostLaneCount here since
  // that function relies on the configured application select but at
  // this point appSel hasn't been updated.
  uint8_t applySetForConfigureLanes = hostLaneMask;
  uint8_t applySetForReleaseLanes = 0;

  std::set<uint8_t> lanesToRelease;
  // Read and cache all laneToActiveCtrlField. We can't rely on existing
  // cache because we may not have got a chance to update in between
  // programming different ports in a sequence
  std::array<uint8_t, 8> laneToActiveCtrlFieldVals{};
  readCmisField(
      CmisField::ACTIVE_CTRL_ALL_LANES,
      laneToActiveCtrlFieldVals.data(),
      false,
      bank);

  for (uint8_t lane = dataPathId; lane < dataPathId + numHostLanes; lane++) {
    lanesToRelease.insert(lane);
    applySetForReleaseLanes |= (1 << lane);
    // Get all lanes with the same data path ID as this lane
    uint8_t currDataPathId =
        (laneToActiveCtrlFieldVals[lane] & DATA_PATH_ID_MASK) >>
        DATA_PATH_ID_BITSHIFT;
    uint8_t currAppSel =
        (laneToActiveCtrlFieldVals[lane] & APP_SEL_MASK) >> APP_SEL_BITSHIFT;
    // If currently App Sel is 0, it means this lane is not part of any
    // active data path yet. No need to find other lanes to release
    if (currAppSel == 0) {
      continue;
    }
    // If we are here, it means that this lane is part of an active data
    // path. Find out which other lanes are active with the same data path
    // id and then release them
    for (auto it = laneToActiveCtrlField.begin();
         it != laneToActiveCtrlField.end();
         it++) {
      auto otherLane = it->first;
      uint8_t otherAppSel =
          (laneToActiveCtrlFieldVals[otherLane] & APP_SEL_MASK) >>
          APP_SEL_BITSHIFT;
      // Ignore lanes with app sel 0 as that means that the lane is not part
      // of any data path
      if (otherAppSel == 0) {
        continue;
      }
      uint8_t otherDataPathId =
          (laneToActiveCtrlFieldVals[otherLane] & DATA_PATH_ID_MASK) >>
          DATA_PATH_ID_BITSHIFT;
      if (currDataPathId == otherDataPathId) {
        lanesToRelease.insert(otherLane);
        applySetForReleaseLanes |= (1 << otherLane);
      }
    }
  }

  // hostLaneMask already identifies the (intra-bank) lanes being programmed.
  const uint8_t lanesToProgram = hostLaneMask;
  // Use application dependent settings
  const uint8_t explicitControl = setExplicitControl(state, lanesToProgram);
  const uint8_t newApSelCode = (apSelCode << APP_SEL_BITSHIFT) |
      (dataPathId << DATA_PATH_ID_BITSHIFT) | explicitControl;
  QSFP_LOG(INFO, this) << fmt::format("newApSelCode: {:#x}", newApSelCode);

  // First release the lanes if they are already part of any datapath
  std::vector<uint8_t> zeroApSelCode(lanesToRelease.size(), 0);
  for (auto it = lanesToRelease.begin(); it != lanesToRelease.end(); it++) {
    QSFP_LOG(INFO, this) << fmt::format("Releasing lane {:#x}", *it);
  }
  writeCmisField(
      laneToAppSelField(lanesToRelease), zeroApSelCode.data(), false, bank);
  // We don't need to check if lanesToRelease is empty or not before setting
  // stage_ctrl_set_0 because there will always be lanes to release. At the
  // minimum, we'll try to release the same lane we are trying to configure
  writeCmisField(
      CmisField::STAGE_CTRL_SET_0, &applySetForReleaseLanes, false, bank);

  std::set<uint8_t> lanesToProgramAppSel;
  std::vector<uint8_t> appSelCode;
  for (uint8_t lane = dataPathId; lane < dataPathId + numHostLanes; lane++) {
    // Assign ApSel code to each lane
    QSFP_LOG(INFO, this) << fmt::format(
        "Configuring lane {:#x} with apsel code {:#x}", lane, newApSelCode);
    lanesToProgramAppSel.insert(lane);
    appSelCode.push_back(newApSelCode);
  }
  writeCmisField(
      laneToAppSelField(lanesToProgramAppSel), appSelCode.data(), false, bank);

  writeCmisField(
      CmisField::STAGE_CTRL_SET_0, &applySetForConfigureLanes, false, bank);

  datapathResetPendingMask_[bank] = applySetForConfigureLanes;

  QSFP_LOG(INFO, this) << fmt::format(
      "set application to {:#x}", mediaInterfaceCode);
}

/*
 * getAllPortsStageSet0Config
 *
 * Returns the stage set 0 AppSel bytes setApplicationSelectCodeAllPorts
 * programs on all lanes of the port's bank for a valid speed combo matching the
 * port's request, or nullopt if there is none
 */
std::optional<CmisModule::AllLaneConfig> CmisModule::getAllPortsStageSet0Config(
    const TransceiverPortState& state,
    uint8_t numHostLanes) {
  // Speed-combo lookups are bank-relative (intra-bank lane indices).
  const uint8_t intraStartHostLane = laneInBank(state.startHostLane);
  std::vector<uint8_t> laneProgramValues;
  if (isAecModule()) {
    laneProgramValues =
        CmisHelper::getValidMultiportSpeedConfig<ActiveCuHostInterfaceCode>(
            state.speed,
            intraStartHostLane,
            numHostLanes,
            laneMask(intraStartHostLane, numHostLanes),
            getNameString(),
            moduleCapabilities_,
            CmisHelper::getActiveValidSpeedCombinations(),
            CmisHelper::getActiveSpeedApplication());
  } else {
    auto mediaInterface = getModuleMediaInterface();
    std::vector<SMFMediaInterfaceCode> configCodes;
    if (TransceiverPropertiesManager::isKnown(mediaInterface)) {
      configCodes = TransceiverPropertiesManager::getMediaCodesForSpeed<
          SMFMediaInterfaceCode>(mediaInterface, state.speed);
    }
    SmfSpeedApplicationMap configMapping;
    if (!configCodes.empty()) {
      configMapping[state.speed] = std::move(configCodes);
    }
    laneProgramValues =
        CmisHelper::getValidMultiportSpeedConfig<SMFMediaInterfaceCode>(
            state.speed,
            intraStartHostLane,
            numHostLanes,
            laneMask(intraStartHostLane, numHostLanes),
            getNameString(),
            moduleCapabilities_,
            TransceiverPropertiesManager::isKnown(mediaInterface)
                ? TransceiverPropertiesManager::getSpeedCombinations<
                      SMFMediaInterfaceCode>(mediaInterface)
                : std::vector<
                      std::array<SMFMediaInterfaceCode, kMaxOsfpNumLanes>>{},
            configMapping.empty() ? CmisHelper::getSmfSpeedApplicationMapping()
                                  : configMapping);
  }
  if (laneProgramValues.size() != kMaxOsfpNumLanes) {
    return std::nullopt;
  }
  AllLaneConfig stageSet0Config;
  for (auto lane = 0; lane < kMaxOsfpNumLanes;) {
    if (auto laneCapability =
            getApplicationField(laneProgramValues[lane], lane)) {
      uint8_t currApSelCode = laneCapability.value().ApSelCode;
      for (auto currApLane = lane;
           currApLane < lane + laneCapability.value().hostLaneCount;
           currApLane++) {
        const uint8_t lanesToProgram =
            laneMask(lane, lane + laneCapability.value().hostLaneCount);
        const uint8_t explicitControl =
            setExplicitControl(state, lanesToProgram);
        stageSet0Config[currApLane] = currApSelCode << APP_SEL_BITSHIFT |
            (lane << DATA_PATH_ID_BITSHIFT) | explicitControl;
      }
      lane += laneCapability.value().hostLaneCount;
    } else {
      stageSet0Config[lane++] = 0;
    }
  }
  return stageSet0Config;
}

/*
 * getAllPortsResetLaneMasks
 *
 * Returns the {deinit, init} lane masks for programming the AppSel on all lanes
 * for a port's request
 */
std::pair<uint8_t, uint8_t> CmisModule::getAllPortsResetLaneMasks(
    const TransceiverPortState& state,
    uint8_t numHostLanes) {
  // Programming all lanes changes the application on lanes outside this port,
  // so deinit every lane in the current and new configs and init every lane the
  // new config puts in an application
  const uint8_t bank = laneToBank(state.startHostLane);
  const uint8_t portLaneMask =
      laneMask(laneInBank(state.startHostLane), numHostLanes);
  uint8_t allPortsInitLaneMask = 0;
  if (auto stageSet0Config = getAllPortsStageSet0Config(state, numHostLanes)) {
    for (int lane = 0; lane < kMaxOsfpNumLanes; lane++) {
      if (((*stageSet0Config)[lane] & APP_SEL_MASK) >> APP_SEL_BITSHIFT) {
        allPortsInitLaneMask |= (1 << lane);
      }
    }
  }
  if (allPortsInitLaneMask == 0) {
    // No valid speed combo, so setApplicationSelectCodeAllPorts won't touch
    // other lanes. Limit the reset to this port.
    return {portLaneMask, portLaneMask};
  }
  const uint8_t initLaneMask = allPortsInitLaneMask | portLaneMask;
  const uint8_t deinitLaneMask = getActiveAppSelLaneMask(bank) | initLaneMask;
  return {deinitLaneMask, initLaneMask};
}

/*
 * setApplicationSelectCodeAllPorts
 *
 * This function programs the application select code on all the software port
 * for a given optics. This is required when the optics has to transition to a
 * valid configuration for all the lanes
 */
void CmisModule::setApplicationSelectCodeAllPorts(
    const TransceiverPortState& state,
    uint8_t numHostLanes) {
  // This programs the whole 8-lane bank that the port lives in. Stage-0 config
  // is bank-relative (intra-bank lane indices).
  const uint8_t bank = laneToBank(state.startHostLane);
  auto stageSet0Config = getAllPortsStageSet0Config(state, numHostLanes);
  if (!stageSet0Config) {
    XLOG(WARNING) << "Transceiver " << getNameString() << " port "
                  << state.startHostLane << ": "
                  << "Failed to find valid speed combo for speed "
                  << apache::thrift::util::enumNameSafe(state.speed)
                  << ", module will not be reprogrammed";
    return;
  }
  writeCmisField(
      CmisField::APP_SEL_LANE_1_8, stageSet0Config->data(), false, bank);

  // Trigger the Set 0 application code setting to be applied on data
  // path init for all the lanes. The actual data-path init will be
  // triggered from the caller function
  uint8_t applySetForSpecificLanes = laneMask(0, kMaxOsfpNumLanes);
  writeCmisField(
      CmisField::STAGE_CTRL_SET_0, &applySetForSpecificLanes, false, bank);

  datapathResetPendingMask_[bank] = applySetForSpecificLanes;
}

/*
 * setMaxFecSamplingLocked
 *
 * Sets the FEC monitor sampling ratio to maximum.
 * Datapath state or module operation would not be interrupted during this
 * configuration
 */
void CmisModule::setMaxFecSamplingLocked() {
  // FLAGS_set_max_fec_sampling is used to roll out the feature
  if (FLAGS_set_max_fec_sampling) {
    auto mediaInterface = getModuleMediaInterface();
    auto itr = kMaxProgFecSamplingSupportedMap_.find(mediaInterface);
    if (itr != kMaxProgFecSamplingSupportedMap_.end()) {
      uint8_t max = itr->second;
      writeCmisField(CmisField::FEC_SAMPLING_PCT, &max);
      QSFP_LOG(INFO, this) << fmt::format(
          "set sampling rate to max: {} for module media interface {}",
          max,
          apache::thrift::util::enumNameSafe(mediaInterface));
    }
  }
}

/*
 * programApplicationSelectCode
 *
 * Helper function to program a given AppSel code to the module. This contains
 * the common logic for resetting datapath, programming the AppSel code,
 * waiting for the module to process, and verifying the configurations.
 *
 * If appSelectFunc is provided, it will be used. Otherwise, the default
 * setApplicationSelectCode will be used.
 */
void CmisModule::programApplicationSelectCode(
    uint8_t appSelCode,
    uint8_t moduleMediaInterfaceCode,
    const TransceiverPortState& state,
    uint8_t numHostLanes,
    std::optional<std::function<void()>> appSelectFunc,
    std::optional<uint8_t> deinitLaneMask,
    std::optional<uint8_t> initLaneMask) {
  // A port's lanes live in one bank; program with an intra-bank lane mask and
  // select that bank for the datapath reset.
  const uint8_t bank = laneToBank(state.startHostLane);
  uint8_t hostLaneMask =
      laneMask(laneInBank(state.startHostLane), numHostLanes);
  const uint8_t resetLaneMask = deinitLaneMask.value_or(hostLaneMask);

  // Use provided function or create default one
  if (!appSelectFunc) {
    QSFP_LOG(INFO, this) << fmt::format(
        "Programming App sel on lanes {:#x}", hostLaneMask);

    appSelectFunc = std::bind(
        &CmisModule::setApplicationSelectCode,
        this,
        appSelCode,
        moduleMediaInterfaceCode,
        state,
        numHostLanes,
        hostLaneMask);
  }

  resetDataPathWithFunc(
      state.portName, appSelectFunc, resetLaneMask, bank, initLaneMask);

  datapathResetPendingMask_[bank] &= ~resetLaneMask;

  // Certain OSFP Modules require a long time to finish application
  // programming. The modules say config is accepted and applied, but
  // internally the module will still be processing the config. If we don't
  // have a delay here, the next application programming on a different lane
  // gets rejected.
  /* sleep override */
  usleep(kUsecAfterAppProgramming);

  // Check if the config has been applied correctly or not
  // TODO: This is a failure scenario. We should Fail somehow !
  if (!checkLaneConfigError(state.startHostLane, numHostLanes)) {
    QSFP_LOG(ERR, this) << fmt::format(
        "application {:#x} could not be set", moduleMediaInterfaceCode);
  }
}

/*
 * getInterfaceCodeForAppSel
 *
 * Helper function to read an interface code (host or media) for a given AppSel
 * code from the EEPROM based on the byte offset.
 * - For media interface code, use byteOffset = kMediaInterfaceCodeOffset (1)
 * - For host interface code, use byteOffset = kHostInterfaceCodeOffset (0)
 */
uint8_t CmisModule::getInterfaceCodeForAppSel(
    uint8_t appSelCode,
    int byteOffset) {
  if (appSelCode == 0 || appSelCode > 15) {
    QSFP_LOG(INFO, this) << fmt::format(
        "Invalid appSelCode {:#x}, returning 0", appSelCode);
    return 0;
  }

  uint8_t interfaceCode = 0;
  int offset;
  int length;
  int dataAddress;

  if (appSelCode >= 1 && appSelCode <= 8) {
    getQsfpFieldAddress(
        CmisField::APPLICATION_ADVERTISING1, dataAddress, offset, length);
    offset += (appSelCode - 1) * length + byteOffset;
    getQsfpValue(dataAddress, offset, 1, &interfaceCode);
  } else if (appSelCode >= 9 && appSelCode <= 15) {
    getQsfpFieldAddress(
        CmisField::APPLICATION_ADVERTISING2, dataAddress, offset, length);
    offset += (appSelCode - 9) * length + byteOffset;
    getQsfpValue(dataAddress, offset, 1, &interfaceCode);
  }

  QSFP_LOG(INFO, this) << fmt::format(
      "interfaceCode: {:#x} for appSelCode {:#x} byteOffset {}",
      interfaceCode,
      appSelCode,
      byteOffset);

  return interfaceCode;
}

/*
 * getCurrentAppSelCode
 *
 * Helper function to read the current application select code for a given lane.
 */
uint8_t CmisModule::getCurrentAppSelCode(uint8_t startHostLane) const {
  // ACTIVE_CTRL_LANE_n is one byte per intra-bank lane on banked page 11h; map
  // the global start lane to its bank and intra-bank position.
  auto [bank, intraLane] = bankAndLane(startHostLane);
  int dataAddress, offset, length;
  getQsfpFieldAddress(
      laneToActiveCtrlField[intraLane], dataAddress, offset, length);
  uint8_t currentApplicationSel =
      getBankedQsfpValuePtr(dataAddress, offset, 1, bank)[0] & APP_SEL_MASK;
  return currentApplicationSel >> APP_SEL_BITSHIFT;
}

/*
 * getAppSelCodeForSpeed
 *
 * Helper function that discovers and returns the appropriate application
 * capability based on module capabilities and speed requirements. Returns
 * the full ApplicationAdvertisingField if a suitable application is found,
 * or std::nullopt if no matching application is available or if the current
 * config already matches.
 */
std::optional<CmisModule::ApplicationAdvertisingField>
CmisModule::getAppSelCodeForSpeed(
    const std::string& portName,
    cfg::PortSpeed speed,
    uint8_t startHostLane,
    uint8_t numHostLanesForPort) {
  std::vector<uint8_t> appCodes;

  if (isAecModule()) {
    appCodes = CmisHelper::getInterfaceCode<ActiveCuHostInterfaceCode>(
        speed, CmisHelper::getActiveSpeedApplication());
  } else {
    auto mediaInterface = getModuleMediaInterface();
    std::vector<SMFMediaInterfaceCode> configCodes;
    if (TransceiverPropertiesManager::isKnown(mediaInterface)) {
      configCodes = TransceiverPropertiesManager::getMediaCodesForSpeed<
          SMFMediaInterfaceCode>(mediaInterface, speed);
    }
    if (!configCodes.empty()) {
      for (auto code : configCodes) {
        appCodes.push_back(static_cast<uint8_t>(code));
      }
    } else {
      appCodes = CmisHelper::getInterfaceCode<SMFMediaInterfaceCode>(
          speed, CmisHelper::getSmfSpeedApplicationMapping());
    }
  }

  if (appCodes.empty()) {
    QSFP_LOG(INFO, this) << "Unsupported Speed.";
    throw FbossError(
        folly::to<std::string>(
            "Transceiver: ",
            qsfpImpl_->getName(),
            " Unsupported speed: ",
            apache::thrift::util::enumNameSafe(speed)));
  }

  QSFP_LOG(INFO, this) << "Application codes supporting current speed: "
                       << folly::join(",", appCodes);

  // Currently we will have the same application across all the lanes. So here
  // we only take one of them to look at.
  uint8_t currentApplicationSel = getCurrentAppSelCode(startHostLane);

  QSFP_LOG(INFO, this) << fmt::format(
      "currentApplicationSel: {:#x} speed {:s} startHostLane {:d} numHostLanesForPort {:d}",
      currentApplicationSel,
      apache::thrift::util::enumNameSafe(speed),
      startHostLane,
      numHostLanesForPort);

  // We use the module Media Interface ID for Optical modules, which is located
  // at the second byte of the field (byteOffset = 1), as Application ID here.
  // If we have an AEC cable, we use the module host interface ID which has a
  // byteOffset of 0. This is in page 00h app sel advertising (starting at
  // offset 86 for page)
  int byteOffset =
      isAecModule() ? kHostInterfaceCodeOffset : kMediaInterfaceCodeOffset;

  // Get the current application interface code based on module type
  uint8_t currentApplication = 0;
  if (currentApplicationSel >= 1 && currentApplicationSel <= 15) {
    currentApplication =
        getInterfaceCodeForAppSel(currentApplicationSel, byteOffset);
  } else {
    QSFP_LOG(INFO, this) << "currentApplication: not selected yet";
  }

  // Loop through all the applications that we support for the given speed and
  // check if any of those are present in the moduleCapabilities. We configure
  // the first application that both we support and the module supports
  for (auto application : appCodes) {
    auto capability = getApplicationField(application, startHostLane);

    // Check if the module supports the application
    if (!capability) {
      continue;
    }

    auto numHostLanes = capability->hostLaneCount;
    // A speed can be served by applications with different host lane counts
    // (800G as 8x100G via 800GAUI-8, or as 4x200G via 800GAUI-4), so the lane
    // count always has to match, at every speed. getMediaIntfCodeFromSpeed()
    // and tcvrPortStateSupported() already filter unconditionally; this keeps
    // all three selection paths in agreement.
    if (numHostLanesForPort != numHostLanes) {
      continue;
    }

    // If the currently configured application is the same as what we are trying
    // to configure, then skip the configuration
    if (application == currentApplication) {
      QSFP_LOG(INFO, this) << fmt::format(
          "Speed matches: currentApplication {:#x}. Doing nothing",
          currentApplication);
      // Make sure the datapath is initialized, otherwise initialize it before
      // returning. The port's lanes live in one bank; use an intra-bank mask.
      const uint8_t bank = laneToBank(startHostLane);
      uint8_t hostLaneMask = laneMask(laneInBank(startHostLane), numHostLanes);
      if (datapathResetPendingMask_[bank] & hostLaneMask) {
        resetDataPathWithFunc(portName, std::nullopt, hostLaneMask, bank);
        datapathResetPendingMask_[bank] &= ~hostLaneMask;
        QSFP_LOG(INFO, this) << fmt::format(
            "Reset datapath for lane mask {:#x} before returning",
            hostLaneMask);
      }
      return std::nullopt;
    }

    // Found a matching application - return the full capability
    return *capability;
  }

  // We didn't find an application that both we support and the module supports
  QSFP_LOG(INFO, this) << "Unsupported Application";
  throw FbossError(
      folly::to<std::string>(
          "Port: ",
          qsfpImpl_->getName(),
          " Unsupported Application by the module: "));
}

/*
 * setApplicationCodeLocked
 *
 * This function programs the application select code for a port using the speed
 * value, start lane number and number of lanes. It goes through module's
 * advertised media interface support capabilities to find appropriate
 * application code tp program. If required, it programs valid configuration on
 * other lanes of the module also.
 */
void CmisModule::setApplicationCodeLocked(
    const TransceiverPortState& state,
    uint8_t newAppSelCode) {
  QSFP_LOG(INFO, this) << fmt::format(
      "Trying to set application code for speed {} on startHostLane {}",
      apache::thrift::util::enumNameSafe(state.speed),
      state.startHostLane);

  // For tunable optics, directly program the AppSel code from config
  if (isTunableOptics()) {
    if (newAppSelCode == kInvalidApplication) {
      throw FbossError("newAppSelCode is invalid for tunable optics");
    }

    QSFP_LOG(INFO, this) << fmt::format(
        "Direct AppSelCode programming for speed {} on startHostLane {} newAppSelCode {}",
        apache::thrift::util::enumNameSafe(state.speed),
        state.startHostLane,
        newAppSelCode);

    // Check if current AppSel matches the desired one
    uint8_t currentAppSelCode = getCurrentAppSelCode(state.startHostLane);
    auto& dpState = portDatapathStates_[state.portName];
    auto& initTimers = dpState.initTimers;
    if (currentAppSelCode == newAppSelCode &&
        (initTimers.progStartTimer.time_since_epoch().count() == 0)) {
      QSFP_LOG(INFO, this)
          << "AppSel codes for tunable optics are matching, skipping programming";
      return;
    }

    // Get the media interface code and program the AppSel
    uint8_t moduleMediaInterfaceCode =
        getInterfaceCodeForAppSel(newAppSelCode, kMediaInterfaceCodeOffset);

    programPortApplicationSelectCode(
        newAppSelCode, moduleMediaInterfaceCode, state, state.numHostLanes);
    return;
  }

  // For non-tunable optics, discover the AppSel code based on capabilities
  auto capability = getAppSelCodeForSpeed(
      state.portName, state.speed, state.startHostLane, state.numHostLanes);

  // If nullopt, means current config already matches, nothing to do
  if (!capability) {
    return;
  }

  uint8_t appSelCode = capability->ApSelCode;
  uint8_t moduleMediaInterfaceCode = capability->moduleMediaInterface;
  uint8_t numHostLanes = capability->hostLaneCount;

  programPortApplicationSelectCode(
      appSelCode, moduleMediaInterfaceCode, state, numHostLanes);
}

/*
 * programPortApplicationSelectCode
 *
 * Programs the AppSel code for a port. Handles the OSFP multiport case: in
 * 400G-FR4 case we will have 8 host lanes instead of 4. Further more, we need
 * to deactivate all the lanes when we switch to an application with a different
 * lane count (CMIS4.0-8.8.4). When the requested config is not a valid
 * multiport config on its own, the AppSel is programmed on all lanes in one
 * datapath cycle.
 */
void CmisModule::programPortApplicationSelectCode(
    uint8_t appSelCode,
    uint8_t moduleMediaInterfaceCode,
    const TransceiverPortState& state,
    uint8_t numHostLanes) {
  std::optional<std::function<void()>> appSelectFunc = std::nullopt;
  std::optional<uint8_t> deinitLaneMask = std::nullopt;
  std::optional<uint8_t> initLaneMask = std::nullopt;

  if (!isRequestValidMultiportSpeedConfig(
          state.speed, state.startHostLane, numHostLanes)) {
    std::tie(deinitLaneMask, initLaneMask) =
        getAllPortsResetLaneMasks(state, numHostLanes);
    QSFP_LOG(INFO, this) << fmt::format(
        "Programming App sel on ALL lanes: speed={}, startHostLane={}, numHostLanes={}, deinitLaneMask={:#x}, initLaneMask={:#x}",
        apache::thrift::util::enumNameSafe(state.speed),
        state.startHostLane,
        numHostLanes,
        *deinitLaneMask,
        *initLaneMask);
    appSelectFunc = std::bind(
        &CmisModule::setApplicationSelectCodeAllPorts,
        this,
        state,
        numHostLanes);
  }

  programApplicationSelectCode(
      appSelCode,
      moduleMediaInterfaceCode,
      state,
      numHostLanes,
      appSelectFunc,
      deinitLaneMask,
      initLaneMask);
}

/*
 * isRequestValidMultiportSpeedConfig
 *
 * This function returns if the requested speed on given number of lanes will
 * result in valid config on the overall optics. If the requested config on
 * given lanes will result in non-supported speed config (as described in static
 * list getSmfValidSpeedCombinations) then this function returns false otherwise
 * returns true. This function does not rely on cache and does the directHW read
 * to know the current speed config on the lanes.
 */
bool CmisModule::isRequestValidMultiportSpeedConfig(
    cfg::PortSpeed speed,
    uint8_t startHostLane,
    uint8_t numLanes) {
  if (!isMultiPortOptics()) {
    // For non-multiport supporting optics, return true rightaway
    return true;
  }

  // A port's lanes are confined to one bank; the speed combos and
  // ACTIVE_CTRL_ALL_LANES are per-bank, so read that bank and use intra-bank
  // lane indices.
  const uint8_t bank = laneToBank(startHostLane);
  const uint8_t intraStartHostLane = laneInBank(startHostLane);

  // Get the current speed config on the Multiport optics lanes. Avoid cache
  // and read from HW directly
  AllLaneConfig currHwSpeedConfig;
  readCmisField(
      CmisField::ACTIVE_CTRL_ALL_LANES, currHwSpeedConfig.data(), false, bank);
  for (int laneId = 0; laneId < kMaxOsfpNumLanes; laneId++) {
    currHwSpeedConfig[laneId] =
        (currHwSpeedConfig[laneId] & APP_SEL_MASK) >> APP_SEL_BITSHIFT;
  }

  uint8_t mask = laneMask(intraStartHostLane, numLanes);
  auto tcvrName = getNameString();

  if (isAecModule()) {
    return CmisHelper::checkSpeedCombo<ActiveCuHostInterfaceCode>(
        speed,
        intraStartHostLane,
        numLanes,
        mask,
        tcvrName,
        moduleCapabilities_,
        currHwSpeedConfig,
        CmisHelper::getActiveValidSpeedCombinations(),
        CmisHelper::getActiveSpeedApplication());
  } else {
    auto mediaInterface = getModuleMediaInterface();
    std::vector<SMFMediaInterfaceCode> configCodes;
    if (TransceiverPropertiesManager::isKnown(mediaInterface)) {
      configCodes = TransceiverPropertiesManager::getMediaCodesForSpeed<
          SMFMediaInterfaceCode>(mediaInterface, speed);
    }
    SmfSpeedApplicationMap configMapping;
    if (!configCodes.empty()) {
      configMapping[speed] = std::move(configCodes);
    }
    return CmisHelper::checkSpeedCombo<SMFMediaInterfaceCode>(
        speed,
        intraStartHostLane,
        numLanes,
        mask,
        tcvrName,
        moduleCapabilities_,
        currHwSpeedConfig,
        TransceiverPropertiesManager::isKnown(mediaInterface)
            ? TransceiverPropertiesManager::getSpeedCombinations<
                  SMFMediaInterfaceCode>(mediaInterface)
            : std::vector<
                  std::array<SMFMediaInterfaceCode, kMaxOsfpNumLanes>>{},
        configMapping.empty() ? CmisHelper::getSmfSpeedApplicationMapping()
                              : configMapping);
  }
}

/*
 * This function checks if the previous lane configuration has been successul
 * or rejected. It will log error and return false if config on a lane is
 * rejected. This function should be run after ApSel setting or any other
 * lane configuration like Rx Equalizer setting etc
 */
bool CmisModule::checkLaneConfigError(
    uint8_t startHostLane,
    uint8_t hostLaneCount) {
  bool success;

  uint8_t configErrors[4];

  // A port's lanes are confined to one bank; CONFIG_ERROR_LANES is per-bank,
  // so read that bank and index it with intra-bank lane offsets.
  const uint8_t bank = laneToBank(startHostLane);
  const uint8_t intraStartHostLane = laneInBank(startHostLane);

  // In case some channel information is not available then we can retry after
  // lane init time
  int retryCount = 2;
  while (retryCount) {
    retryCount--;
    readCmisField(CmisField::CONFIG_ERROR_LANES, configErrors, false, bank);

    bool allStatusAvailable = true;
    success = true;

    for (int channel = intraStartHostLane;
         channel < intraStartHostLane + hostLaneCount;
         channel++) {
      uint8_t byte = channel / 2;
      uint8_t cfgErr = configErrors[byte] >> ((channel % 2) * 4);
      cfgErr &= 0x0f;
      if (cfgErr >= 8) {
        cfgErr = 8;
      }
      // If some lane info is not available then we need to try again
      if (cfgErr == 0) {
        allStatusAvailable = false;
      }
      // Status other than 1 is considered failed
      if (cfgErr != 1) {
        success = false;
      }
      QSFP_LOG(INFO, this) << fmt::format(
          "Lane {:d} config stats: {:s}",
          channel,
          channelConfigErrorMsg[cfgErr]);
    }

    // If all channel information is available then no need to retry and break
    // from this loop, otherwise retry one more time after a wait period
    if (allStatusAvailable) {
      break;
    } else if (retryCount) {
      QSFP_LOG(INFO, this) << "Some lane status not available so trying again";
      /* sleep override */
      usleep(kUsecBetweenLaneInit);
    } else {
      QSFP_LOG(ERR, this) << "Some lane status not available even after retry";
    }
  };

  return success;
}

/*
 * Put logic here that should only be run on ports that have been
 * down for a long time. These are actions that are potentially more
 * disruptive, but have worked in the past to recover a transceiver.
 */
void CmisModule::remediateFlakyTransceiver(
    bool allPortsDown,
    const std::vector<std::string>& ports) {
  QSFP_LOG(INFO, this) << "allPortsDown = " << allPortsDown
                       << ". Performing potentially disruptive remediations on "
                       << folly::join(",", ports);

  if (allPortsDown) {
    // This api accept 1 based module id however the module id in WedgeManager
    // is 0 based.
    triggerModuleReset();
  } else {
    auto portNameToHostLanesMap = getPortNameToHostLanes();
    for (const auto& port : ports) {
      if (portNameToHostLanesMap.find(port) != portNameToHostLanesMap.end()) {
        auto& lanes = portNameToHostLanesMap[port];
        if (!lanes.empty()) {
          // A port's lanes are confined to one bank; reset that bank with an
          // intra-bank lane mask.
          const uint8_t bank = laneToBank(*lanes.begin());
          auto portLaneMask =
              laneMask(laneInBank(*lanes.begin()), lanes.size());
          QSFP_LOG(INFO, this)
              << "Doing datapath reinit for " << port << " with lane mask "
              << static_cast<int>(portLaneMask);
          resetDataPathWithFunc(port, std::nullopt, portLaneMask, bank);
        } else {
          QSFP_LOG(ERR, this) << "Host lanes empty for " << port
                              << ". Skipping individual datapath remediation.";
        }
      } else {
        QSFP_LOG(ERR, this) << "Host lanes unavailable for " << port
                            << ". Skipping individual datapath remediation.";
      }
    }
  }

  // Reset lastRemediateTime_ so we can use cool down before next remediation
  lastRemediateTime_ = std::time(nullptr);
}

void CmisModule::setPowerOverrideIfSupportedLocked(
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

  if (currentState == PowerControlState::HIGH_POWER_OVERRIDE) {
    QSFP_LOG(INFO, this)
        << "Power override already correctly set, doing nothing";
    return;
  }

  getQsfpFieldAddress(CmisField::MODULE_CONTROL, dataAddress, offset, length);

  uint8_t currentModuleControl;
  getFieldValueLocked(CmisField::MODULE_CONTROL, &currentModuleControl);

  // LowPwr is on the 6 bit of ModuleControl.
  currentModuleControl = currentModuleControl | (1 << 6);

  // first set to low power
  writeCmisField(CmisField::MODULE_CONTROL, &currentModuleControl);

  // Transceivers need a bit of time to handle the low power setting
  // we just sent. We should be able to use the status register to be
  // smarter about this, but just sleeping 0.1s for now.
  usleep(kUsecBetweenPowerModeFlap);

  // then enable target power class
  currentModuleControl = currentModuleControl & 0x3f;

  writeCmisField(CmisField::MODULE_CONTROL, &currentModuleControl);

  QSFP_LOG(INFO, this) << fmt::format(
      "QSFP module control field set to {:#x}", currentModuleControl);
}

void CmisModule::ensureRxOutputSquelchEnabled(
    const std::vector<HostLaneSettings>& hostLanesSettings) {
  bool allLanesRxOutputSquelchEnabled = true;
  for (auto& hostLaneSettings : hostLanesSettings) {
    if (hostLaneSettings.rxSquelch().has_value() &&
        *hostLaneSettings.rxSquelch()) {
      allLanesRxOutputSquelchEnabled = false;
      break;
    }
  }

  if (!allLanesRxOutputSquelchEnabled) {
    uint8_t enableAllLaneRxOutputSquelch = 0x0;

    // RX_SQUELCH_DISABLE is a per-bank register; clear it on every bank to
    // cover all lanes of a multi-bank (CPO) module.
    for (uint8_t bank = 0; bank < getMaxNumBanks(); ++bank) {
      writeCmisField(
          CmisField::RX_SQUELCH_DISABLE,
          &enableAllLaneRxOutputSquelch,
          false,
          bank);
    }
    QSFP_LOG(INFO, this) << "Enabled Rx output squelch on all lanes.";
  }
}

bool CmisModule::isRxConsActImplSupported() const {
  auto info = QsfpFieldInfo<CmisField, CmisPages>::getQsfpFieldAddress(
      cmisFields, CmisField::HOST_LANE_PROV_AD);
  const uint8_t* data =
      getQsfpValuePtr(info.dataAddress, info.offset, info.length);
  // Page 45h (Host Lane Provisioning Advertisement), Byte 129, Bit 1:
  // rxConsActImpl - indicates Rx Consequent Action is implemented
  return (*data & RX_CONS_ACT_IMPL_MASK) != 0;
}

void CmisModule::disableTxRxSquelchForTunableOptics() {
  uint8_t squelchDisableValue = 0xFF;
  // Squelch disable registers are per-bank; write each bank to cover all lanes.
  for (uint8_t bank = 0; bank < getMaxNumBanks(); ++bank) {
    writeCmisField(
        CmisField::TX_SQUELCH_DISABLE, &squelchDisableValue, false, bank);
  }
  QSFP_LOG(INFO, this) << "Disabled TX Squelch for tunable optics";

  if (!isRxConsActImplSupported()) {
    QSFP_LOG(WARN, this)
        << "Module does not advertise rxConsActImpl "
        << "(Page 45h, Byte 129, Bit 1). Keeping squelch enabled.";
    return;
  }
  // Enable Rx Consequent Action (LF insertion) before disabling squelch
  enableRxLfInsertionForTunableOptics();

  for (uint8_t bank = 0; bank < getMaxNumBanks(); ++bank) {
    writeCmisField(
        CmisField::RX_SQUELCH_DISABLE, &squelchDisableValue, false, bank);
  }
  QSFP_LOG(INFO, this) << "Disabled RX Squelch for tunable optics";
}

void CmisModule::enableRxLfInsertionForTunableOptics() {
  // Read current value to preserve txConsAct (Bits 3-0)
  auto info = QsfpFieldInfo<CmisField, CmisPages>::getQsfpFieldAddress(
      cmisFields, CmisField::CONS_ACT_CONTROL);
  const uint8_t* data =
      getQsfpValuePtr(info.dataAddress, info.offset, info.length);
  // Set Bits 7-4 (rxConsAct) to 0001 = insert LF, preserve Bits 3-0
  uint8_t lfInsertValue =
      (*data & LOWER_FOUR_BITS_MASK) | RX_CONS_ACT_INSERT_LF;
  writeCmisField(CmisField::CONS_ACT_CONTROL, &lfInsertValue);
  QSFP_LOG(INFO, this)
      << "Enabled Rx LF insertion (rxConsAct=0x1) for tunable optics";
}

bool CmisModule::isRxConsActHoldOffTmrImplSupported() const {
  auto info = QsfpFieldInfo<CmisField, CmisPages>::getQsfpFieldAddress(
      cmisFields, CmisField::HOST_LANE_PROV_AD);
  const uint8_t* data =
      getQsfpValuePtr(info.dataAddress, info.offset, info.length);
  return (*data & RX_CONS_ACT_HOLD_OFF_TMR_IMPL_MASK) != 0;
}

void CmisModule::configureRxConsActHoldOffTimer(int32_t timerMs) {
  if (timerMs < 0 || timerMs > 655350 || timerMs % 10 != 0) {
    throw FbossError(
        "Hold-off timer must be a non-negative multiple of 10ms "
        "in range [0, 655350]: ",
        timerMs);
  }
  int32_t registerValue = timerMs / 10;
  uint16_t regVal = static_cast<uint16_t>(registerValue);
  uint8_t timerData[2];
  timerData[0] = static_cast<uint8_t>((regVal >> 8) & 0xFF);
  timerData[1] = static_cast<uint8_t>(regVal & 0xFF);

  if (isRxConsActHoldOffTmrImplSupported()) {
    writeCmisField(CmisField::CONS_ACT_HOLD_OFF_TMR, timerData);
    QSFP_LOG(INFO, this) << "Configured Rx Consequent Action Hold-off Timer to "
                         << timerMs << "ms (register value=" << regVal << ")";
    return;
  }

  // The module doesn't advertise support for programming the hold-off timer
  // (Page 45h, Byte 129, Bit 2), so we can't write it. Read the current value:
  // if it already matches the requested value there's nothing to do; otherwise
  // fail loudly rather than silently leaving a value we couldn't set (e.g. a
  // module with a non-zero default when we requested it be disabled).
  uint8_t currentData[2];
  readCmisField(CmisField::CONS_ACT_HOLD_OFF_TMR, currentData);
  uint16_t currentRegVal =
      (static_cast<uint16_t>(currentData[0]) << 8) | currentData[1];
  if (currentRegVal != regVal) {
    throw FbossError(
        "Module does not support programming rxConsActHoldOffTmr "
        "(Page 45h, Byte 129, Bit 2); current value ",
        currentRegVal * 10,
        "ms differs from requested ",
        timerMs,
        "ms");
  }
  QSFP_LOG(INFO, this)
      << "Rx Consequent Action Hold-off Timer already at requested " << timerMs
      << "ms; module does not support programming it (no-op)";
}

bool CmisModule::tcvrPortStateSupported(TransceiverPortState& portState) const {
  lock_guard<std::mutex> g(qsfpModuleMutex_);
  auto currTransmitterTechnology = getQsfpTransmitterTechnology();
  bool activeElectricalCable = false;
  if (isAecModule()) {
    activeElectricalCable = true;
  }
  if (currTransmitterTechnology == TransmitterTechnology::OPTICAL &&
      (portState.transmitterTech != TransmitterTechnology::OPTICAL &&
       portState.transmitterTech != TransmitterTechnology::BACKPLANE)) {
    // For optics, we allow both BACKPLANE and OPTICAL media in platform mapping
    return false;
  } else if (
      currTransmitterTechnology == TransmitterTechnology::COPPER &&
      !activeElectricalCable) {
    // For Active cables, the tcvr is configrable, so we need to check.
    // For passive copper cables, return true irrespective of speed as the
    // copper cables are mostly flexible with all speeds. We can change this
    // later when we know of any limitations.
    return true;
  }

  auto speed = portState.speed;
  auto startHostLane = portState.startHostLane;
  auto numHostLanes = portState.numHostLanes;
  std::vector<uint8_t> appCodes;
  if (activeElectricalCable) {
    appCodes = CmisHelper::getInterfaceCode(
        speed, CmisHelper::getActiveSpeedApplication());
  } else {
    auto mediaInterface = getModuleMediaInterface();
    std::vector<SMFMediaInterfaceCode> configCodes;
    if (TransceiverPropertiesManager::isKnown(mediaInterface)) {
      configCodes = TransceiverPropertiesManager::getMediaCodesForSpeed<
          SMFMediaInterfaceCode>(mediaInterface, speed);
    }
    if (!configCodes.empty()) {
      for (auto code : configCodes) {
        appCodes.push_back(static_cast<uint8_t>(code));
      }
    } else {
      appCodes = CmisHelper::getInterfaceCode(
          speed, CmisHelper::getSmfSpeedApplicationMapping());
    }
  }
  if (appCodes.empty()) {
    // Speed Not supported
    return false;
  }

  for (auto application : appCodes) {
    if (auto capability = getApplicationField(application, startHostLane)) {
      // Application supported on the starting host lane
      auto hostLaneCount = capability->hostLaneCount;
      if (numHostLanes == hostLaneCount) {
        // Host Lane count also matches
        return true;
      }
    }
  }
  return false;
}

void CmisModule::customizeTransceiverLocked(
    const TransceiverPortState& portState) {
  QSFP_LOG(INFO, this) << fmt::format(
      "customizeTransceiverLocked: PortName {}, Speed {}, StartHostLane {}, NumHostLanes{}",
      portState.portName,
      apache::thrift::util::enumNameSafe(portState.speed),
      portState.startHostLane,
      portState.numHostLanes);
  /*
   * This must be called with a lock held on qsfpModuleMutex_
   */
  if (customizationSupported()) {
    if (!programAppSelInLowPowerMode()) {
      // We want this on regardless of speed
      setPowerOverrideIfSupportedLocked(
          getPowerControlValue(false /* readFromCache */));
    } else {
      QSFP_LOG(INFO, this)
          << "Module is kept in low power mode for AppSel programming and skipping power override";
    }

    if (isTunableOptics()) {
      if (portState.opticalChannelConfig.has_value()) {
        auto& dpState = portDatapathStates_[portState.portName];
        auto& initTimers = dpState.initTimers;
        // If dp-initialization start timer is not set, invoke
        // programTunableModule
        if (initTimers.progStartTimer.time_since_epoch().count() == 0) {
          programTunableModule(portState.opticalChannelConfig.value());
        } else {
          QSFP_LOG(INFO, this) << "DP_INIT in prog";
        }
      } else {
        throw FbossError(
            "Tunable optics requires optical channel config for transceiver programming");
      }
    }

    if (portState.speed != cfg::PortSpeed::DEFAULT) {
      if (isTunableOptics()) {
        const auto& chanConfig = portState.opticalChannelConfig;
        if (!chanConfig.has_value() ||
            !is_non_optional_field_set_manually_or_by_serializer(
                chanConfig.value().appSelCode())) {
          throw FbossError(
              "Tunable optics requires optical channel config with appSelCode for speed configuration");
        }
        auto newAppSelCode = *chanConfig.value().appSelCode();
        setApplicationCodeLocked(portState, newAppSelCode);
      } else {
        setApplicationCodeLocked(portState, kInvalidApplication);
      }
    }

    // For 200G-FR4 module operating in 2x50G mode, disable squelch on all lanes
    // so that each lanes can operate independently
    if (getModuleMediaInterface() == MediaInterfaceCode::FR4_200G &&
        portState.speed == cfg::PortSpeed::FIFTYTHREEPOINTONETWOFIVEG) {
      const uint8_t bank = laneToBank(portState.startHostLane);
      uint8_t squelchDisableValue = 0xF;
      writeCmisField(
          CmisField::TX_SQUELCH_DISABLE, &squelchDisableValue, false, bank);
      writeCmisField(
          CmisField::RX_SQUELCH_DISABLE, &squelchDisableValue, false, bank);
      QSFP_LOG(DBG1, this) << "Disabled TX and RX Squelch";
    }
    // Set the FEC sampling if applicable.
    setMaxFecSamplingLocked();
    // Handle vendor-specific low power mode clearing after AppSel programming
    if (programAppSelInLowPowerMode()) {
      const PowerControlState powerState = getCurrentPowerControlState();
      if (powerState == PowerControlState::HIGH_POWER_OVERRIDE &&
          isModuleInReadyState()) {
        QSFP_LOG(INFO, this) << "Module already in high power and ready state";
      } else {
        uint8_t currentModuleControl;
        readCmisField(CmisField::MODULE_CONTROL, &currentModuleControl);
        uint8_t newModuleControl = currentModuleControl & ~LOW_PWR_BIT;
        QSFP_LOG(INFO, this) << fmt::format(
            "Clearing low power bit to enable high power mode: {:#x} currentModuleControl: {:#x})",
            newModuleControl,
            currentModuleControl);
        writeCmisField(CmisField::MODULE_CONTROL, &newModuleControl);

        getCurrentPowerControlState();

        if (!moduleReadyStatePoll()) {
          QSFP_LOG(ERR, this) << "Module not in ready state";
        }
      }
    }
  } else {
    QSFP_LOG(DBG1, this) << "Customization not supported";
  }
  return;
}

void CmisModule::programTunableModule(
    const cfg::OpticalChannelConfig& opticalChannelConfig) {
  int16_t channelNum = 0;
  int32_t frequencyMhz = 0;
  const auto& freqConfig = opticalChannelConfig.frequencyConfig();
  const auto& centerFreq = freqConfig->centerFrequencyConfig();

  QSFP_LOG(INFO, this) << "Program tunable optics module";
  // Program the Rx Consequent Action hold-off timer (defaults to 0 = disabled).
  // configureRxConsActHoldOffTimer writes it on modules that implement the
  // register; on modules that don't, it's a no-op only if the current value
  // already matches the request, otherwise it throws (so we never silently
  // leave a value we couldn't set).
  configureRxConsActHoldOffTimer(
      *opticalChannelConfig.rxConsActHoldOffTimerMs());

  // Disable TX and RX squelch on all lanes
  disableTxRxSquelchForTunableOptics();

  switch (centerFreq->getType()) {
    case cfg::CenterFrequencyConfig::Type::frequencyMhz: {
      frequencyMhz = centerFreq->frequencyMhz().value();
      channelNum = getChannelNumFromFrequency(
          frequencyMhz, *freqConfig->frequencyGrid());
      break;
    }
    case cfg::CenterFrequencyConfig::Type::channelNumber:
      channelNum = centerFreq->channelNumber().value();
      break;
    default:
      // Handle error case - no field set
      throw FbossError("No field set in CenterFrequencyConfig");
  }

  uint8_t gridSelection =
      frequencyGridToGridSelection(*freqConfig->frequencyGrid());

  // Write grid selection followed by channel number, which is the typical order
  // recommended
  writeCmisField(CmisField::MEDIA_TX_1_GRID_AND_FINE_TUNE_ENA, &gridSelection);
  QSFP_LOG(INFO, this) << fmt::format(
      "Programmed gridSelection {} on the tunable optics", gridSelection);

  uint8_t channelNumBytes[2];
  channelNumBytes[1] = static_cast<uint8_t>(channelNum & 0XFF);
  channelNumBytes[0] = static_cast<uint8_t>((channelNum >> 8) & 0XFF);

  // Channel number programming
  writeCmisField(CmisField::MEDIA_TX_1_CHAN_NBR_SEL, channelNumBytes);
  QSFP_LOG(INFO, this) << fmt::format(
      "Programmed Channel number on the tunable optics. frequency {} channel_number {} channelNumBytes[0] {} channelNumBytes[1] {}",
      frequencyMhz,
      channelNum,
      channelNumBytes[0],
      channelNumBytes[1]);
  /*
   * Program the module with tx-power supplied from the qsfp_service_config
   * throw an error if tx-power value is not specified
   * TODO: tx-power range based sanity check 04h:198-201
   */
  if (!apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          opticalChannelConfig.txPower0P01Dbm())) {
    throw FbossError("Tx-power not specified on the qsfp_service_config");
  }
  int16_t txPower = *opticalChannelConfig.txPower0P01Dbm();
  QSFP_LOG(INFO, this) << fmt::format(
      "OpticalChannelConfig txPower {}", txPower);
  uint8_t txPowerBytes[2];
  txPowerBytes[1] = static_cast<uint8_t>(txPower & 0XFF);
  txPowerBytes[0] = static_cast<uint8_t>((txPower >> 8) & 0XFF);
  // Tx Power programming
  writeCmisField(CmisField::MEDIA_TX_1_TGT_OUTPUT_PWR, txPowerBytes);
  QSFP_LOG(INFO, this) << fmt::format("Tx power {} got programmed", txPower);
}

uint8_t CmisModule::frequencyGridToGridSelection(FrequencyGrid grid) const {
  uint8_t gridSelection = 0x0;
  switch (grid) {
    case FrequencyGrid::LASER_3P125GHZ:
      gridSelection = 0x00;
      break;
    case FrequencyGrid::LASER_6P25GHZ:
      gridSelection = 0x10;
      break;
    case FrequencyGrid::LASER_12P5GHZ:
      gridSelection = 0x20;
      break;
    case FrequencyGrid::LASER_25GHZ:
      gridSelection = 0x30;
      break;
    case FrequencyGrid::LASER_50GHZ:
      gridSelection = 0x40;
      break;
    case FrequencyGrid::LASER_100GHZ:
      gridSelection = 0x50;
      break;
    case FrequencyGrid::LASER_33GHZ:
      gridSelection = 0x60;
      break;
    case FrequencyGrid::LASER_75GHZ:
      gridSelection = 0x70;
      break;
    case FrequencyGrid::LASER_150GHZ:
      gridSelection = 0x80;
      break;
    default:
      throw FbossError("Invalid FrequencyGrid value: ", static_cast<int>(grid));
  }
  return gridSelection;
}

int16_t CmisModule::getChannelNumFromFrequency(
    int32_t frequencyMhz,
    FrequencyGrid frequencyGrid) {
  int64_t diffMhz = static_cast<int64_t>(frequencyMhz) - kDefaultFrequencyMhz;
  int32_t channelNum = 0;
  switch (frequencyGrid) {
    case FrequencyGrid::LASER_150GHZ:
      channelNum = static_cast<int32_t>((diffMhz * 40) / 1000000 - 3);
      break;
    case FrequencyGrid::LASER_100GHZ:
      channelNum = static_cast<int32_t>((diffMhz * 10) / 1000000);
      break;
    case FrequencyGrid::LASER_75GHZ:
      channelNum = static_cast<int32_t>((diffMhz * 40) / 1000000);
      break;
    case FrequencyGrid::LASER_50GHZ:
      channelNum = static_cast<int32_t>((diffMhz * 20) / 1000000);
      break;
    case FrequencyGrid::LASER_33GHZ:
      channelNum = static_cast<int32_t>((diffMhz * 30) / 1000000);
      break;
    case FrequencyGrid::LASER_25GHZ:
      channelNum = static_cast<int32_t>((diffMhz * 40) / 1000000);
      break;
    case FrequencyGrid::LASER_12P5GHZ:
      channelNum = static_cast<int32_t>((diffMhz * 80) / 1000000);
      break;
    case FrequencyGrid::LASER_6P25GHZ:
      channelNum = static_cast<int32_t>((diffMhz * 160) / 1000000);
      break;
    case FrequencyGrid::LASER_3P125GHZ:
      channelNum = static_cast<int32_t>((diffMhz * 320) / 1000000);
      break;
    default:
      throw FbossError(
          "Unsupported frequency grid: ",
          apache::thrift::util::enumNameOrThrow(frequencyGrid));
  }
  return channelNum;
}

/*
 * ensureTransceiverReadyLocked
 *
 * If the current power configuration state is not same as desired one then
 * change it to that (by setting and resetting LP mode) otherwise return true
 * when module is in ready state otherwise return false.
 *
 * @param hasTunableOpticsConfig - indicates if tunable optics config is
 *        present in qsfp_service_config. For tunable optics modules without
 *        config, an exception is thrown to prevent high power mode transition.
 */
bool CmisModule::ensureTransceiverReadyLocked(bool hasTunableOpticsConfig) {
  // If customization is not supported then the Power control bit can't be
  // touched. Return true as nothing needs to be done here
  if (!customizationSupported()) {
    QSFP_LOG(DBG1, this)
        << "ensureTransceiverReadyLocked: Customization not supported";
    return true;
  }

  // For tunable optics modules, if tunable optics config is not present in
  // qsfp_service_config, throw an exception. This requires operators to
  // explicitly provide the necessary config before tunable optics can be
  // brought up to high power mode.
  if (isTunableOptics() && !hasTunableOpticsConfig) {
    throw FbossError(
        "ensureTransceiverReadyLocked: Tunable optics module ",
        qsfpImpl_->getName(),
        " detected but no tunable optics config present in qsfp_service_config. "
        "Cannot move to high power mode without optical channel configuration.");
  }

  // Read the current power configuration values. Don't depend on refresh
  // because that may be delayed
  const PowerControlState powerState = getCurrentPowerControlState();

  // If Optics current power configuration is High Power then the config is
  // correct. We need to check if the Module's current status is READY then
  // return true else return false as the optics state machine might be in
  // transition and need more time to be ready
  if (powerState == PowerControlState::HIGH_POWER_OVERRIDE) {
    return isModuleInReadyState();
  }

  // If the optics current power configuration is Low Power then set the LP
  // mode, wait, reset the LP mode and then return false since the module
  // needs some time to converge its state machine

  setModuleLowPowerModeLocked();

  if (isTunableOptics()) {
    QSFP_LOG(INFO, this) << fmt::format(
        "Optics is tunable {}", getNameString());
    // Deactivate all the datapath lane before putting into the high power mode
    // for shutting down the laser
    if (!dataPathProgram(
            getNameString(), kFullDataPathDeInitMask, /*isInit*/ false)) {
      return false;
    }
  }

  // Clear low power bit (set to 0x20)
  if (!programAppSelInLowPowerMode()) {
    releaseModuleLowPowerModeLocked();
    // Enforces next refresh is a full refresh.
    dirty_ = true;
    return false;
  } else {
    // Maintaining the optics to low power mode until AppSel programming
    // completion
    QSFP_LOG(INFO, this)
        << "ensureTransceiverReadyLocked: Optics in low power mode for AppSel programming";
    return true;
  }
}

/*
 * configureModule
 *
 * Set the module serdes / Rx equalizer after module has been discovered. This
 * is done only if current serdes setting is different from desired one and if
 * the setting is specified in the qsfp config
 */
void CmisModule::configureModule(uint8_t startHostLane) {
  auto moduleTypeEncoding = getMediaTypeEncoding();
  if (moduleTypeEncoding == MediaTypeEncodings::PASSIVE_CU ||
      moduleTypeEncoding == MediaTypeEncodings::ACTIVE_CABLES) {
    // Nothing to configure for passive copper modules
    // TODO: The getModuleConfigOverrideFactor and config expect SMF
    // values. These values dont seem to be applicable to Active Cables,
    // so we will ignore this for now.
    return;
  }

  auto appCode = getSmfMediaInterface(startHostLane);
  auto capability =
      getApplicationField(static_cast<uint8_t>(appCode), startHostLane);

  if (!capability) {
    QSFP_LOG(ERR, this) << "can't find the application capability for "
                        << apache::thrift::util::enumNameSafe(appCode);
    return;
  }
  QSFP_LOG(INFO, this) << "configureModule for application "
                       << apache::thrift::util::enumNameSafe(appCode)
                       << " starting on host lane " << startHostLane;

  auto moduleFactor = getModuleConfigOverrideFactor(
      std::nullopt, // Part Number : TODO: Read and cache tcvrPartNumber
      appCode // Application code
  );

  // Set the Rx equalizer setting based on QSFP config
  for (const auto& override : tcvrConfig_->overridesConfig_) {
    // Check if there is an override for all kinds of transceivers or
    // an override for the current application code(speed)
    if (overrideFactorMatchFound(
            *override.factor(), // override factor
            moduleFactor)) {
      // Check if this override factor requires overriding RxEqualizerSettings
      if (auto rxEqSetting =
              cmisRxEqualizerSettingOverride(*override.config())) {
        setModuleRxEqualizerLocked(
            *rxEqSetting, startHostLane, capability->hostLaneCount);
        return;
      }
    }
  }

  QSFP_LOG(INFO, this)
      << "Rx Equalizer configuration not specified in the QSFP config";
}

bool CmisModule::isTunableOptics() const {
  return isCBandTunable() || isLBandTunable();
}

bool CmisModule::isCBandTunable() const {
  auto info = QsfpFieldInfo<CmisField, CmisPages>::getQsfpFieldAddress(
      cmisFields, CmisField::MEDIA_INTERFACE_TECHNOLOGY);
  const uint8_t* data =
      getQsfpValuePtr(info.dataAddress, info.offset, info.length);
  return *data == DeviceTechnologyCmis::C_BAND_TUNABLE_LASER_CMIS;
}

bool CmisModule::isLBandTunable() const {
  auto info = QsfpFieldInfo<CmisField, CmisPages>::getQsfpFieldAddress(
      cmisFields, CmisField::MEDIA_INTERFACE_TECHNOLOGY);
  const uint8_t* data =
      getQsfpValuePtr(info.dataAddress, info.offset, info.length);
  return *data == DeviceTechnologyCmis::L_BAND_TUNABLE_LASER_CMIS;
}

bool CmisModule::isLpoModule() const {
  // FACETESTLPO is for testing purposes.
  Vendor vendor = getVendorInfo();
  if (vendor.name().value() == "FACETESTLPO") {
    return true;
  }
  // Expected host/media interface codes for LPO transceivers.
  std::set<std::pair<ActiveCuHostInterfaceCode, SMFMediaInterfaceCode>>
      expectedLpoInterfaces = {
          {ActiveCuHostInterfaceCode::LPO_100G,
           SMFMediaInterfaceCode::FR1_100G},
          {ActiveCuHostInterfaceCode::LPO_400G,
           SMFMediaInterfaceCode::FR4_400G},
          {ActiveCuHostInterfaceCode::LPO_800G,
           SMFMediaInterfaceCode::FR8_800G}};

  if (moduleCapabilities_.size() == expectedLpoInterfaces.size()) {
    for (const auto& capability : moduleCapabilities_) {
      if (!expectedLpoInterfaces.erase(
              std::make_pair(
                  static_cast<ActiveCuHostInterfaceCode>(
                      capability.moduleHostInterface),
                  static_cast<SMFMediaInterfaceCode>(
                      capability.moduleMediaInterface)))) {
        break;
      }
    }
    if (expectedLpoInterfaces.empty()) {
      return true;
    }
  }
  return false;
}

MediaInterfaceCode CmisModule::getModuleMediaInterface() const {
  // Return the MediaInterfaceCode based on the first application
  auto moduleMediaInterface = MediaInterfaceCode::UNKNOWN;
  auto mediaTypeEncoding = getMediaTypeEncoding();
  if (mediaTypeEncoding == MediaTypeEncodings::PASSIVE_CU) {
    // FIXME: Remove CR8_400G hardcoding and derive this from number of
    // lanes/host electrical interface instead
    moduleMediaInterface = MediaInterfaceCode::CR8_400G;
  } else if (
      mediaTypeEncoding == MediaTypeEncodings::OPTICAL_SMF &&
      moduleCapabilities_.size() > 0) {
    auto firstModuleCapability = moduleCapabilities_.begin();
    auto smfCode = static_cast<SMFMediaInterfaceCode>(
        firstModuleCapability->moduleMediaInterface);
    std::vector<int> hostStartLanes(
        firstModuleCapability->hostStartLanes.begin(),
        firstModuleCapability->hostStartLanes.end());
    auto smfLength = static_cast<int>(getQsfpSMFLength());
    // Config-driven SMF derivation. The bank count (Lower Page 00h byte 70)
    // disambiguates two codes whose application advertisement is otherwise
    // identical, e.g. a multi-bank DR4_8x800G from a single-bank DR4_2x800G.
    moduleMediaInterface = TransceiverPropertiesManager::deriveSmfCode(
        static_cast<uint8_t>(smfCode),
        hostStartLanes,
        firstModuleCapability->moduleHostInterface,
        smfLength,
        getMaxNumBanks());
    if (moduleMediaInterface == MediaInterfaceCode::UNKNOWN) {
      // Fallback to existing mapping for unrecognized modules
      moduleMediaInterface =
          CmisHelper::getMediaInterfaceCode<SMFMediaInterfaceCode>(
              smfCode, CmisHelper::getSmfMediaInterfaceMapping());
    }
  } else if (
      mediaTypeEncoding == MediaTypeEncodings::ACTIVE_CABLES &&
      !moduleCapabilities_.empty()) {
    // The module-level code describes the whole cable, so derive it from the
    // widest application it advertises (an 8-lane 1.6TAUI-8 part reports
    // CR8_1600G, an 8-lane 800GAUI-8 part still reports CR8_800G). Narrower
    // breakout applications are reported per lane by getMediaInterfaceId().
    auto widest = std::max_element(
        moduleCapabilities_.begin(),
        moduleCapabilities_.end(),
        [](const auto& a, const auto& b) {
          return a.hostLaneCount < b.hostLaneCount;
        });
    moduleMediaInterface =
        CmisHelper::getMediaInterfaceCode<ActiveCuHostInterfaceCode>(
            static_cast<ActiveCuHostInterfaceCode>(
                widest->moduleMediaInterface),
            CmisHelper::getActiveMediaInterfaceMapping());
  }

  return moduleMediaInterface;
};

/*
 * setModuleRxEqualizerLocked
 *
 * Customize the optics for Rx pre-cursor, Rx post cursor, Rx main amplitude
 * 1. Check P11h, B223-234 to compare current and desired pre/post/main
 * 2. Write P10h, B162-165 for Pre
 * 3. Write P10h, B166-169 for Post
 * 4. Write P10h, B170-173 for Main
 * 5. Write P10h, B145-152 bit 0 = 1 to use Staged Set 0 values
 * 6. Write P10h, B144 = 0xFF to apply stage 0 control value immediately
 */
void CmisModule::setModuleRxEqualizerLocked(
    RxEqualizerSettings rxEqualizer,
    uint8_t startHostLane,
    uint8_t hostLaneCount) {
  // A port's lanes are confined to one bank; the RX equalizer registers are
  // per-bank, so read/write that bank and index it with intra-bank lanes.
  const uint8_t bank = laneToBank(startHostLane);
  const uint8_t intraStartHostLane = laneInBank(startHostLane);

  uint8_t currPre[4], currPost[4], currMain[4];
  // Read the existing settings to compare with desired settings later
  readCmisField(CmisField::RX_OUT_PRE_CURSOR, currPre, false, bank);
  readCmisField(CmisField::RX_OUT_POST_CURSOR, currPost, false, bank);
  readCmisField(CmisField::RX_OUT_MAIN, currMain, false, bank);

  uint8_t desiredPre[4], desiredPost[4], desiredMain[4];
  // Initialize desired settings with the current settings
  for (int i = 0; i < 4; i++) {
    desiredPre[i] = currPre[i];
    desiredPost[i] = currPost[i];
    desiredMain[i] = currMain[i];
  }
  bool changePre = false, changePost = false, changeMain = false;

  QSFP_LOG(INFO, this) << "setModuleRxEqualizerLocked called with startLane = "
                       << startHostLane
                       << ", hostLaneCount = " << hostLaneCount;

  // Update the desired settings for the relevant lanes
  for (auto lane = intraStartHostLane;
       lane <= (intraStartHostLane + hostLaneCount - 1);
       lane++) {
    // Two lanes share the same byte. offsetIndex tracks which of the 4 bytes
    // corresponds to lane
    int offsetIndex = lane / 2;
    // For odd lanes, values are at the upper 4 bits. shiftOffset will be 4 for
    // odd lanes, 0 for even lanes
    int shiftOffset = 0;
    if (lane % 2) {
      shiftOffset = 4;
    }
    // Clear the bits so that we can set them next
    desiredPre[offsetIndex] &= ~(0xf << shiftOffset);
    desiredPost[offsetIndex] &= ~(0xf << shiftOffset);
    desiredMain[offsetIndex] &= ~(0xf << shiftOffset);

    // Set the settings for the corresponding lane
    desiredPre[offsetIndex] |=
        ((*rxEqualizer.preCursor() & 0xf) << shiftOffset);
    desiredPost[offsetIndex] |=
        ((*rxEqualizer.postCursor() & 0xf) << shiftOffset);
    desiredMain[offsetIndex] |=
        ((*rxEqualizer.mainAmplitude() & 0xf) << shiftOffset);
  }

  auto compareSettings = [intraStartHostLane, hostLaneCount](
                             uint8_t currSettings[],
                             uint8_t desiredSettings[],
                             int length,
                             bool& changeNeeded) {
    // Two lanes share the same byte so loop only until numLanes / 2
    for (auto i = intraStartHostLane / 2;
         i <= (intraStartHostLane + hostLaneCount - 1) / 2;
         i++) {
      if (i < length && currSettings[i] != desiredSettings[i]) {
        // Some of the pre-cursor value needs to be changed so break from
        // here
        changeNeeded = true;
        return;
      }
    }
  };

  // Compare current Pre cursor value to see if the change is needed
  compareSettings(currPre, desiredPre, 4, changePre);
  // Compare current Post cursor value to see if the change is needed
  compareSettings(currPost, desiredPost, 4, changePost);
  // Compare current Rx Main value to see if the change is needed
  compareSettings(currMain, desiredMain, 4, changeMain);

  // If anything is changed then apply the change and trigger it
  if (changePre || changePost || changeMain) {
    for (auto i = intraStartHostLane / 2;
         i <= (intraStartHostLane + hostLaneCount - 1) / 2;
         i++) {
      // Apply the change for pre/post/main if needed
      if (changePre) {
        writeCmisField(
            offsetIndexToCmisField[i][0], &desiredPre[i], false, bank);
        QSFP_LOG(INFO, this) << fmt::format(
            "customized index {:d} for Pre-cursor 0x{:x}", i, desiredPre[i]);
      }
      if (changePost) {
        writeCmisField(
            offsetIndexToCmisField[i][1], &desiredPost[i], false, bank);
        QSFP_LOG(INFO, this) << fmt::format(
            "customized index {:d} for Post-cursor 0x{:x}", i, desiredPost[i]);
      }
      if (changeMain) {
        writeCmisField(
            offsetIndexToCmisField[i][2], &desiredMain[i], false, bank);
        QSFP_LOG(INFO, this) << fmt::format(
            "customized index {:d} for Rx-out-main 0x{:x}", i, desiredMain[i]);
      }
    }

    // Apply the change using stage 0 control. This read selects page 10h on the
    // target bank, which the subsequent skipBankAndPageChange write relies on.
    uint8_t stage0Control[8];
    readCmisField(CmisField::APP_SEL_LANE_1_8, stage0Control, false, bank);
    std::set<uint8_t> lanesToConfigure;
    std::vector<uint8_t> stageControlToWrite;
    for (int i = intraStartHostLane; i < intraStartHostLane + hostLaneCount;
         i++) {
      stage0Control[i] |= 1;
      lanesToConfigure.insert(i);
      stageControlToWrite.push_back(stage0Control[i]);
    }
    writeCmisField(
        laneToAppSelField(lanesToConfigure),
        stageControlToWrite.data(),
        true /* skipBankAndPageChange */);

    // Trigger the stage 0 control values to be operational in optics
    uint8_t stage0ControlTrigger = laneMask(intraStartHostLane, hostLaneCount);
    writeCmisField(
        CmisField::STAGE_CTRL_SET0_IMMEDIATE,
        &stage0ControlTrigger,
        false,
        bank);

    // Check if the config has been applied correctly or not
    if (!checkLaneConfigError(startHostLane, hostLaneCount)) {
      QSFP_LOG(ERR, this) << "customization config rejected";
    }
  }
}

/*
 * setDiagsCapability
 *
 * This function reads the module register from cache and populates the
 * diagnostic capability. This function is called from Module State Machine
 * when the MSM enters Module Discovered state after EEPROM read.
 */
void CmisModule::setDiagsCapability() {
  if (flatMem_) {
    // Upper pages > 0 are not applicable for flatMem_ modules, and hence
    // diagsCapability isn't valid either
    return;
  }
  // Limiting the scope of diagsCapability_ write lock
  {
    auto diagsCapability = diagsCapability_.wlock();
    if (!diagsCapability->has_value()) {
      QSFP_LOG(INFO, this) << "Setting diag capability";
      DiagsCapability diags;

      auto getPrbsCapabilities =
          [&](CmisField generatorField,
              CmisField checkerField) -> std::vector<prbs::PrbsPolynomial> {
        int offset;
        int length;
        int dataAddress;
        getQsfpFieldAddress(generatorField, dataAddress, offset, length);
        CHECK_EQ(length, 2);
        getQsfpFieldAddress(checkerField, dataAddress, offset, length);
        CHECK_EQ(length, 2);

        uint8_t generatorCapsData[2];
        readFromCacheOrHw(generatorField, generatorCapsData);
        uint16_t generatorCaps =
            (generatorCapsData[1] << 8) | generatorCapsData[0];

        uint8_t checkerCapsData[2];
        readFromCacheOrHw(checkerField, checkerCapsData);
        uint16_t checkerCaps = (checkerCapsData[1] << 8) | checkerCapsData[0];

        std::vector<prbs::PrbsPolynomial> caps;
        for (auto patternIDPolynomialPair : prbsPatternMap.right) {
          // We claim PRBS polynomial is supported when both generator and
          // checker support the polynomial
          if (generatorCaps & (1 << patternIDPolynomialPair.first) &&
              checkerCaps & (1 << patternIDPolynomialPair.first)) {
            caps.push_back(patternIDPolynomialPair.second);
          }
        }
        return caps;
      };

      uint8_t data;
      readFromCacheOrHw(CmisField::VDM_DIAG_SUPPORT, &data);
      diags.vdm() = (data & FieldMasks::VDM_SUPPORT_MASK) ? true : false;
      diags.diagnostics() =
          (data & FieldMasks::DIAGS_SUPPORT_MASK) ? true : false;

      readFromCacheOrHw(CmisField::CDB_SUPPORT, &data);
      diags.cdb() = (data & FieldMasks::CDB_SUPPORT_MASK) ? true : false;

      readFromCacheOrHw(CmisField::TX_CONTROL_SUPPORT, &data);
      diags.txOutputControl() =
          (data & FieldMasks::TX_DISABLE_SUPPORT_MASK) ? true : false;
      readFromCacheOrHw(CmisField::RX_CONTROL_SUPPORT, &data);
      diags.rxOutputControl() =
          (data & FieldMasks::RX_DISABLE_SUPPORT_MASK) ? true : false;

      if (*diags.diagnostics()) {
        readFromCacheOrHw(CmisField::LOOPBACK_CAPABILITY, &data);
        diags.loopbackSystem() =
            (data & FieldMasks::LOOPBACK_SYS_SUPPOR_MASK) ? true : false;
        diags.loopbackLine() =
            (data & FieldMasks::LOOPBACK_LINE_SUPPORT_MASK) ? true : false;
        LoopbackCapability lbCap;
        lbCap.mediaSideOutput() = data & FieldMasks::LOOPBACK_MEDIA_OUTPUT_MASK;
        lbCap.mediaSideInput() = data & FieldMasks::LOOPBACK_MEDIA_INPUT_MASK;
        lbCap.hostSideOutput() = data & FieldMasks::LOOPBACK_HOST_OUTPUT_MASK;
        lbCap.hostSideInput() = data & FieldMasks::LOOPBACK_HOST_INPUT_MASK;
        lbCap.perLaneHostSide() =
            data & FieldMasks::LOOPBACK_PER_LANE_HOST_MASK;
        lbCap.perLaneMediaSide() =
            data & FieldMasks::LOOPBACK_PER_LANE_MEDIA_MASK;
        lbCap.simultaneousHostAndMediaSide() =
            data & FieldMasks::LOOPBACK_SIMULTANEOUS_HOST_MEDIA_MASK;
        diags.loopbackCapability() = lbCap;

        readFromCacheOrHw(CmisField::PATTERN_CHECKER_CAPABILITY, &data);
        diags.prbsLine() =
            (data & FieldMasks::PRBS_LINE_SUPPRT_MASK) ? true : false;
        diags.prbsSystem() =
            (data & FieldMasks::PRBS_SYS_SUPPRT_MASK) ? true : false;
        if (*diags.prbsLine()) {
          diags.prbsLineCapabilities() = getPrbsCapabilities(
              CmisField::MEDIA_SUPPORTED_GENERATOR_PATTERNS,
              CmisField::MEDIA_SUPPORTED_CHECKER_PATTERNS);
        }
        if (*diags.prbsSystem()) {
          diags.prbsSystemCapabilities() = getPrbsCapabilities(
              CmisField::HOST_SUPPORTED_GENERATOR_PATTERNS,
              CmisField::HOST_SUPPORTED_CHECKER_PATTERNS);
        }

        readFromCacheOrHw(CmisField::DIAGNOSTIC_CAPABILITY, &data);
        diags.snrLine() = data & FieldMasks::SNR_LINE_SUPPORT_MASK;
        diags.snrSystem() = data & FieldMasks::SNR_SYS_SUPPORT_MASK;
      }

      if (*diags.vdm()) {
        readCmisField(CmisField::VDM_GROUPS_SUPPORT, &data);
        vdmSupportedGroupsMax_ = (data & VDM_GROUPS_SUPPORT_MASK) + 1;
      }

      setCustomFeatureCapability(diags);

      *diagsCapability = diags;
    }
  }
  // Scan and update the VDM diags locations
  updateVdmDiagsValLocation();
}

/*
 * The mode mismatch and thermal margin registers live in CMIS Custom space
 * (Lower Memory bytes 64-84, Page 01h bytes 191-222), which carries no
 * standard meaning. Only modules built to the Meta FW spec populate them, and
 * that spec mandates CMIS >= 5.1 -- so require that revision before trusting
 * byte 191, otherwise an unrelated vendor's custom data could be read as an
 * advertisement.
 */
void CmisModule::setCustomFeatureCapability(DiagsCapability& diags) {
  const auto [major, minor] = getCmisRevision();
  if (major < 5 || (major == 5 && minor < 1)) {
    return;
  }

  uint8_t data;
  readFromCacheOrHw(CmisField::SUPPORTED_CUSTOM_FEATURES, &data);
  diags.modeMismatchFlag() =
      (data & FieldMasks::MODE_MISMATCH_SUPPORT_MASK) != 0;
  diags.dspTempMargin() =
      (data & FieldMasks::DSP_TEMP_MARGIN_SUPPORT_MASK) != 0;
  diags.laserTempMargin() =
      (data & FieldMasks::LASER_TEMP_MARGIN_SUPPORT_MASK) != 0;
}

/*
 * verifyEepromChecksums
 *
 * This function verifies the module's eeprom register checksum in various
 * pages. For CMIS module the checksums are kept in 3 pages:
 *   Page 0: Register 222 contains checksum for values in register 128 to 221
 *   Page 1: Register 255 contains checksum for values in register 130 to 254
 *   Page 2: Register 255 contains checksum for values in register 128 to 254
 * These checksums are 8 bit sum of all the 8 bit values
 */
bool CmisModule::verifyEepromChecksums() {
  bool rc = true;
  // Verify checksum for all pages
  for (auto& csumInfoIt : checksumInfoCmis) {
    // For flat memory module, check for page 0 only
    if (flatMem_ && csumInfoIt.first != CmisPages::PAGE00) {
      continue;
    }
    rc &= verifyEepromChecksum(csumInfoIt.first);
  }
  QSFP_LOG_IF(WARN, !rc, this) << "EEPROM Checksum Failed";
  return rc;
}

/*
 * verifyEepromChecksum
 *
 * This function verifies the module's eeprom register checksum for a given
 * page. The checksum is 8 bit sum of all the 8 bit values in range of
 * registers
 */
bool CmisModule::verifyEepromChecksum(CmisPages pageId) {
  int offset;
  int length;
  int dataAddress;
  const uint8_t* data;
  uint8_t checkSum = 0, expectedChecksum;

  // Return false if the registers are not cached yet (this is not expected)
  if (!cacheIsValid()) {
    QSFP_LOG(WARN, this)
        << "can't do eeprom checksum as the register cache is not populated";
    return false;
  }
  // Return false if we don't know range of registers to validate the checksum
  // on this page
  if (checksumInfoCmis.find(pageId) == checksumInfoCmis.end()) {
    QSFP_LOG(WARN, this) << "can't do eeprom checksum for page "
                         << static_cast<int>(pageId);
    return false;
  }

  // Get the range of registers, compute checksum and compare
  dataAddress = static_cast<int>(pageId);
  offset = checksumInfoCmis[pageId].checksumRangeStartOffset;
  length = checksumInfoCmis[pageId].checksumRangeLength;
  data = getQsfpValuePtr(dataAddress, offset, length);

  for (int i = 0; i < length; i++) {
    checkSum += data[i];
  }

  getQsfpFieldAddress(
      checksumInfoCmis[pageId].checksumValOffset, dataAddress, offset, length);
  data = getQsfpValuePtr(dataAddress, offset, length);
  expectedChecksum = data[0];

  if (checkSum != expectedChecksum) {
    QSFP_LOG(ERR, this) << fmt::format(
        "Page {:d}: expected eeprom checksum {:#x}, actual {:#x}",
        static_cast<int>(pageId),
        expectedChecksum,
        checkSum);
    return false;
  } else {
    QSFP_LOG(DBG5, this) << fmt::format(
        "Page {:d}: eeprom checksum verified successfully {:#x}",
        static_cast<int>(pageId),
        checkSum);
  }
  return true;
}

// Set (freeze=true) or clear (freeze=false) the VDM FreezeRequest bit
// (Page 2Fh byte 144 bit 7) via read-modify-write.
void CmisModule::writeVdmFreezeRequestLocked(bool freeze) {
  uint8_t latchRequest;
  readCmisField(CmisField::VDM_LATCH_REQUEST, &latchRequest);
  if (freeze) {
    latchRequest |= FieldMasks::VDM_LATCH_REQUEST_MASK;
  } else {
    latchRequest &= ~FieldMasks::VDM_LATCH_REQUEST_MASK;
  }
  writeCmisField(CmisField::VDM_LATCH_REQUEST, &latchRequest);
}

// Read the (frozen) VDM data pages 24h-27h, for every bank on multi-bank
// modules, into the page caches.
void CmisModule::readVdmFrozenPagesLocked() {
  std::array<BankedPage*, 4> dataPageBuffers = {
      &page24_, &page25_, &page26_, &page27_};
  for (uint8_t group = 1; group <= vdmSupportedGroupsMax_; group++) {
    readBankedPage(kVdmDataPages[group - 1], *dataPageBuffers[group - 1]);
  }
}

// Single non-blocking read of FreezeDone (Page 2Fh byte 145 bit 7).
bool CmisModule::isVdmFreezeDoneLocked() {
  uint8_t doneFlag = 0;
  readCmisField(CmisField::VDM_LATCH_DONE, &doneFlag);
  return (doneFlag & FieldMasks::VDM_LATCH_DONE_MASK) != 0;
}

/*
 * driveVdmCaptureLocked
 *
 * Non-blocking per-refresh driver for the VDM ForOds capture (see the handshake
 * description in CmisModule.h). Returns true on the refresh where a frozen
 * snapshot was read, so updateCachedTransceiverInfoLocked() refreshes the
 * ForOds stats that cycle.
 */
bool CmisModule::driveVdmCaptureLocked() {
  if (!isVdmSupported()) {
    captureVdmStats_ = false;
    return false;
  }
  if (vdmCaptureState_ == VdmCaptureState::IDLE) {
    // Arm on the StatsPublisher trigger, but only issue the freeze once the
    // module is ready (powered up, out of low power) -- freezing while the
    // module is still initializing / in low power (DSP off) hangs the latch and
    // corrupts VDM intervals. If not ready yet, leave captureVdmStats_ armed
    // and retry on a later refresh. The readiness check does I2C, so gate it
    // behind the cheap armed-flag load.
    if (captureVdmStats_.load() && isReadyForVdmFreezeLocked() &&
        captureVdmStats_.exchange(false)) {
      requestVdmFreezeLocked();
    }
    return false;
  }
  // FREEZE_REQUESTED: the freeze was requested last refresh; finish it now.
  return finishVdmFreezeReadLocked();
}

// The module can only process a VDM freeze once it has finished initializing
// and is out of low power (DSP powered on). Gate on the module being in the
// READY state rather than on per-lane datapath activation, since some lanes may
// be intentionally left uninitialized.
bool CmisModule::isReadyForVdmFreezeLocked() {
  return isModuleInReadyState();
}

// Async step 1: write FreezeRequest and move to FREEZE_REQUESTED. Returns
// immediately -- the module completes the freeze (<2s) well within one refresh.
void CmisModule::requestVdmFreezeLocked() {
  QSFP_LOG(DBG3, this) << "requestVdmFreezeLocked";
  writeVdmFreezeRequestLocked(true);
  // Per CMIS 8.19.6, the module resets its internal accumulators and starts the
  // new statistics interval at the freeze instant (not at unfreeze), so stamp
  // the interval start here. getVdmPerfMonitorStats() reports this as
  // intervalStartTime for the running interval.
  vdmIntervalStartTime_ = std::time(nullptr);
  vdmCaptureState_ = VdmCaptureState::FREEZE_REQUESTED;
}

// Async step 2 (one refresh after the request): a single non-blocking
// FreezeDone read, then read the frozen pages and unfreeze. If FreezeDone is
// still unset (slow/non-implementing module) we read anyway rather than wait.
// Returns true.
bool CmisModule::finishVdmFreezeReadLocked() {
  if (!isVdmFreezeDoneLocked()) {
    QSFP_LOG(WARN, this)
        << "VDM FreezeDone not set one refresh after request; reading anyway";
  }
  readVdmFrozenPagesLocked();

  // Release the freeze to resume VDM data collection. We don't poll
  // UnfreezeDone: some modules never toggle it, and the next capture cycle
  // gives ample time to resume. (vdmIntervalStartTime_ was stamped at the
  // freeze request, per the CMIS interval-start semantics.)
  writeVdmFreezeRequestLocked(false);
  vdmCaptureState_ = VdmCaptureState::IDLE;
  return true;
}

// Abort any in-flight async capture and clear a freeze we requested (e.g. on a
// reprogram/reset). No-op (no I2C) when already IDLE.
void CmisModule::resetVdmCaptureStateLocked() {
  if (vdmCaptureState_ == VdmCaptureState::IDLE) {
    return;
  }
  if (isVdmSupported()) {
    writeVdmFreezeRequestLocked(false);
  }
  vdmCaptureState_ = VdmCaptureState::IDLE;
}

/*
 * triggerVdmStatsCapture
 *
 * This function triggers the next VDM stats capture by qsfp_service refresh
 * thread. This function gets called by ODS cycle (every 5 mins)
 */
void CmisModule::triggerVdmStatsCapture() {
  if (!isVdmSupported()) {
    return;
  }
  QSFP_LOG(DBG3, this) << "triggerVdmStatsCapture";

  captureVdmStats_ = true;
}

bool CmisModule::getModuleStateChanged() {
  return getSettingsValue(CmisField::MODULE_FLAG, MODULE_STATE_CHANGED_MASK);
}

void CmisModule::clearTransceiverPrbsStats(
    const std::string& portName,
    phy::Side side) {
  auto clearTransceiverPrbsStatsLambda = [side, portName, this]() {
    lock_guard<std::mutex> g(qsfpModuleMutex_);
    // Read modify write
    // Write bit 5 in 13h.177 to 1 and then 0 to reset counters. BER_CTRL is a
    // per-bank register, so reset every bank's counters on multi-bank (CPO).
    for (uint8_t bank = 0; bank < getMaxNumBanks(); ++bank) {
      uint8_t val;
      readCmisField(CmisField::BER_CTRL, &val, false, bank);
      val |= BER_CTRL_RESET_STAT_MASK;
      writeCmisField(CmisField::BER_CTRL, &val, false, bank);
      val &= ~BER_CTRL_RESET_STAT_MASK;
      writeCmisField(CmisField::BER_CTRL, &val, false, bank);
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

/*
 * setPortPrbsLocked
 *
 * This function starts or stops the PRBS generator and checker on a given
 * side of optics (line side or host side). The PRBS is supported on new 200G
 * and 400G CMIS optics. This function expects the caller to hold the qsfp
 * module level lock
 */
bool CmisModule::setPortPrbsLocked(
    const std::string& portName,
    phy::Side side,
    const prbs::InterfacePrbsState& prbs) {
  // If PRBS is not supported then return
  if (!isPrbsSupported(side)) {
    QSFP_LOG(ERR, this) << fmt::format(
        "PRBS not supported on {:s} side",
        (side == phy::Side::LINE ? "Line" : "System"));
    return false;
  }

  // Return error for invalid PRBS polynominal
  auto prbsPatternItr = prbsPatternMap.left.find(
      static_cast<prbs::PrbsPolynomial>(*prbs.polynomial()));

  // Get the list of lanes to enable/disable PRBS
  auto tcvrLanes = getTcvrLanesForPort(portName, side);
  if (tcvrLanes.empty()) {
    QSFP_LOG(ERR, this) << fmt::format(
        "Empty lane list for port {:s}", portName);
    return false;
  }

  // A port's lanes are confined to one bank; the PRBS pattern/enable registers
  // (page 13h) are per-bank, so target that bank with intra-bank lane offsets.
  const uint8_t bank = laneToBank(*tcvrLanes.begin());

  bool startGen{false}, stopGen{false};
  bool startChk{false}, stopChk{false};
  if (!prbs.generatorEnabled().has_value() &&
      !prbs.checkerEnabled().has_value()) {
    QSFP_LOG(ERR, this) << "Invalid generator/checker input";
    return false;
  }

  if (prbs.generatorEnabled().has_value()) {
    startGen = prbs.generatorEnabled().value();
    stopGen = !prbs.generatorEnabled().value();
  }
  if (prbs.checkerEnabled().has_value()) {
    startChk = prbs.checkerEnabled().value();
    stopChk = !prbs.checkerEnabled().value();
  }

  // Step 1: Set the pattern for Generator (for starting case)
  if (startGen) {
    auto cmisRegisters = (side == phy::Side::LINE) ? prbsGenMediaPatternFields
                                                   : prbsGenHostPatternFields;

    // Check that a valid polynomial is provided
    if (prbsPatternItr == prbsPatternMap.left.end()) {
      QSFP_LOG(ERR, this) << fmt::format(
          "PRBS Polynominal {} not supported",
          apache::thrift::util::enumNameSafe(prbs.polynomial().value()));
      return false;
    }
    auto prbsPolynominal = prbsPatternItr->second;
    // There are 4 bytes, each contains pattern for 2 lanes
    uint8_t patternVal;
    for (auto lane : tcvrLanes) {
      auto intraLane = laneInBank(lane);
      auto cmisReg = cmisRegisters[intraLane / 2];
      readCmisField(cmisReg, &patternVal, false, bank);
      patternVal = (intraLane % 2 == 0)
          ? (patternVal & 0xF0) | (prbsPolynominal & 0x0F)
          : (patternVal & 0x0F) | ((prbsPolynominal << 4) & 0xF0);
      writeCmisField(cmisReg, &patternVal, false, bank);
    }
  }

  // Step 2: Start/Stop the generator
  // Get the bitmask for Start/Stop of generator/checker on the given side
  auto cmisRegister = (side == phy::Side::LINE) ? CmisField::MEDIA_GEN_ENABLE
                                                : CmisField::HOST_GEN_ENABLE;

  if (startGen || stopGen) {
    uint8_t startGenLaneMask;
    readCmisField(cmisRegister, &startGenLaneMask, false, bank);
    for (auto lane : tcvrLanes) {
      if (startGen) {
        startGenLaneMask |= (1 << laneInBank(lane));
      } else {
        startGenLaneMask &= ~(1 << laneInBank(lane));
      }
    }
    writeCmisField(cmisRegister, &startGenLaneMask, false, bank);

    QSFP_LOG(INFO, this) << fmt::format(
        "PRBS Generator on side {:s} Lanemask {:#x} {:s}",
        ((side == phy::Side::LINE) ? "Line" : "Host"),
        startGenLaneMask,
        (startGen ? "Started" : "Stopped"));
  }

  // Step 3: Set the pattern for Checker (for starting case)
  if (startChk) {
    auto& fields = (side == phy::Side::LINE) ? prbsChkMediaPatternFields
                                             : prbsChkHostPatternFields;

    // Check that a valid polynomial is provided
    if (prbsPatternItr == prbsPatternMap.left.end()) {
      QSFP_LOG(ERR, this) << fmt::format(
          "PRBS Polynominal {} not supported",
          apache::thrift::util::enumNameSafe(prbs.polynomial().value()));
      return false;
    }
    auto prbsPolynominal = prbsPatternItr->second;
    // There are 4 bytes, each contains pattern for 2 lanes
    uint8_t patternVal;
    for (auto lane : tcvrLanes) {
      auto intraLane = laneInBank(lane);
      auto cmisReg = fields[intraLane / 2];
      readCmisField(cmisReg, &patternVal, false, bank);
      patternVal = (intraLane % 2 == 0)
          ? (patternVal & 0xF0) | (prbsPolynominal & 0x0F)
          : (patternVal & 0x0F) | ((prbsPolynominal << 4) & 0xF0);
      writeCmisField(cmisReg, &patternVal, false, bank);
    }
  }

  // Step 4: Start/Stop the checker
  cmisRegister = (side == phy::Side::LINE) ? CmisField::MEDIA_CHECKER_ENABLE
                                           : CmisField::HOST_CHECKER_ENABLE;

  if (startChk || stopChk) {
    uint8_t startChkLaneMask;
    readCmisField(cmisRegister, &startChkLaneMask, false, bank);
    for (auto lane : tcvrLanes) {
      if (startChk) {
        startChkLaneMask |= (1 << laneInBank(lane));
      } else {
        startChkLaneMask &= ~(1 << laneInBank(lane));
      }
    }
    writeCmisField(cmisRegister, &startChkLaneMask, false, bank);

    QSFP_LOG(INFO, this) << fmt::format(
        "PRBS Checker on side {:s} Lanemask {:#x} {:s}",
        ((side == phy::Side::LINE) ? "Line" : "Host"),
        startChkLaneMask,
        (startChk ? "Started" : "Stopped"));
  }

  return true;
}

// This function expects caller to hold the qsfp module level lock
prbs::InterfacePrbsState CmisModule::getPortPrbsStateLocked(
    std::optional<const std::string> portName,
    Side side) {
  if (flatMem_) {
    return prbs::InterfacePrbsState();
  }
  {
    if (!isTransceiverFeatureSupported(TransceiverFeature::PRBS, side)) {
      return prbs::InterfacePrbsState();
    }
  }
  prbs::InterfacePrbsState state;

  // Get the list of lanes to check PRBS. The PRBS enable/pattern registers
  // (page 13h) are per-bank 8-bit, so resolve the port's bank and build an
  // intra-bank lane mask.
  uint8_t laneMask = 0;
  uint8_t bank = 0;
  if (portName.has_value()) {
    auto tcvrLanes = getTcvrLanesForPort(portName.value(), side);
    if (tcvrLanes.empty()) {
      QSFP_LOG(ERR, this) << fmt::format(
          "Empty lane list for port {:s}", portName.value());
      return prbs::InterfacePrbsState();
    }
    bank = laneToBank(*tcvrLanes.begin());
    for (auto lane : tcvrLanes) {
      laneMask |= (1 << laneInBank(lane));
    }
  } else {
    // No port specified: a single per-bank 8-bit register can only represent
    // one bank's lanes, so report bank 0's lanes (capped at the per-bank
    // width).
    uint8_t numLanes =
        (side == phy::Side::LINE) ? numMediaLanes() : numHostLanes();
    laneMask = (1 << std::min<uint8_t>(numLanes, kMaxOsfpNumLanes)) - 1;
  }
  if (!laneMask) {
    QSFP_LOG(ERR, this) << fmt::format(
        "Lanes not available for getPortPrbsState {:s}", qsfpImpl_->getName());
    return prbs::InterfacePrbsState();
  }

  auto cmisRegister = (side == phy::Side::LINE) ? CmisField::MEDIA_GEN_ENABLE
                                                : CmisField::HOST_GEN_ENABLE;
  uint8_t generator;
  readCmisField(cmisRegister, &generator, false, bank);

  cmisRegister = (side == phy::Side::LINE) ? CmisField::MEDIA_CHECKER_ENABLE
                                           : CmisField::HOST_CHECKER_ENABLE;
  uint8_t checker;
  readCmisField(cmisRegister, &checker, false, bank);

  state.generatorEnabled() = (generator & laneMask);
  state.checkerEnabled() = (checker & laneMask);
  // PRBS is enabled if either generator is enabled or the checker is enabled
  auto enabled =
      state.generatorEnabled().value() || state.checkerEnabled().value();

  // If state is enabled, check the polynomial
  if (enabled) {
    std::array<CmisField, 4> cmisPatternRegister = (side == phy::Side::LINE)
        ? prbsGenMediaPatternFields
        : prbsGenHostPatternFields;

    int firstLane = 0, tempLaneMask = laneMask;
    while (tempLaneMask) {
      if (tempLaneMask & 0x1) {
        break;
      }
      firstLane++;
      tempLaneMask >>= 1;
    }

    uint8_t patternByte, pattern;
    // Intentionally reading only 1 byte instead of 'length'
    // We assume the same polynomial is configured on all lanes so only
    // reading 1 byte which gives the polynomial configured on lane 0
    cmisRegister = cmisPatternRegister[firstLane / 2];
    readCmisField(cmisRegister, &patternByte, false, bank);
    pattern = (patternByte >> (((firstLane % 2) * 4))) & 0xF;
    auto polynomialItr = prbsPatternMap.right.find(pattern);
    if (polynomialItr != prbsPatternMap.right.end()) {
      state.polynomial() = prbs::PrbsPolynomial(polynomialItr->second);
    }
  }
  return state;
}

/*
 * getPortPrbsStatsSideLocked
 *
 * This function retrieves the PRBS stats for all the lanes in a module for
 * the given side of optics (line side or host side). The PRBS checker lock
 * and BER stats are returned.
 */
phy::PrbsStats CmisModule::getPortPrbsStatsSideLocked(
    phy::Side side,
    bool checkerEnabled,
    const phy::PrbsStats& lastStats) {
  phy::PrbsStats prbsStats;

  // If PRBS is not supported then return
  if (!isPrbsSupported(side)) {
    QSFP_LOG(ERR, this) << fmt::format(
        "PRBS not supported on {:s} side",
        (side == phy::Side::LINE ? "Line" : "System"));
    return phy::PrbsStats{};
  }

  prbsStats.portId() = getID();
  prbsStats.component() = side == Side::SYSTEM
      ? phy::PortComponent::TRANSCEIVER_SYSTEM
      : phy::PortComponent::TRANSCEIVER_LINE;

  if (!checkerEnabled || !lastStats.laneStats()->size()) {
    // If the checker is not enabled or the stats are uninitialized, return
    // the default PrbsStats object with some of the parameters initialized
    int lanes = side == Side::SYSTEM ? numHostLanes() : numMediaLanes();
    for (int lane = 0; lane < lanes; lane++) {
      phy::PrbsLaneStats laneStat;
      laneStat.laneId() = lane;
      prbsStats.laneStats()->push_back(laneStat);
    }
    return prbsStats;
  }

  int numLanes = (side == phy::Side::LINE) ? numMediaLanes() : numHostLanes();
  bool snrSupported = isSnrSupported(side);
  auto lockField = (side == phy::Side::LINE)
      ? CmisField::MEDIA_LANE_CHECKER_LOL_LATCH
      : CmisField::HOST_LANE_CHECKER_LOL_LATCH;
  auto berField = (side == phy::Side::LINE) ? CmisField::MEDIA_BER_HOST_SNR
                                            : CmisField::HOST_BER;
  auto snrField = (side == phy::Side::LINE) ? CmisField::MEDIA_SNR
                                            : CmisField::MEDIA_BER_HOST_SNR;

  std::vector<phy::PrbsLaneStats> laneStatsList(numLanes);
  for (int laneId = 0; laneId < numLanes; laneId++) {
    laneStatsList[laneId].laneId() = laneId;
  }

  // The checker-lock latch and the BER/SNR diagnostic page (14h, selected by
  // DIAG_SEL) are per-bank. Read each bank explicitly; iterate banks in reverse
  // so the module is left selected on bank 0. Single-bank modules pass no bank
  // (legacy behavior, no bank-select write).
  uint8_t numBanks = getMaxNumBanks();
  for (int bank = numBanks - 1; bank >= 0; bank--) {
    auto bankArg = (numBanks > 1) ? std::optional<uint8_t>(bank) : std::nullopt;

    uint8_t checkerLockMask;
    readCmisField(lockField, &checkerLockMask, false, bankArg);

    setDiagSel(
        DiagnosticFeatureEncoding::BER, bankArg, kUsecDiagSelectLatchWaitPrbs);
    std::array<uint8_t, 16> laneBerList{};
    readCmisField(berField, laneBerList.data(), false, bankArg);

    std::array<uint8_t, 16> laneSnrList{};
    if (snrSupported) {
      setDiagSel(
          DiagnosticFeatureEncoding::SNR,
          bankArg,
          kUsecDiagSelectLatchWaitPrbs);
      readCmisField(snrField, laneSnrList.data(), false, bankArg);
    }

    for (int intra = 0; intra < kMaxOsfpNumLanes; intra++) {
      int laneId = bank * kMaxOsfpNumLanes + intra;
      if (laneId >= numLanes) {
        break;
      }
      auto& laneStats = laneStatsList[laneId];
      laneStats.locked() = (checkerLockMask & (1 << intra)) == 0;
      laneStats.ber() = QsfpModule::getBerFloatValue(
          laneBerList.at(intra * 2), laneBerList.at(intra * 2 + 1));
      if (snrSupported) {
        uint16_t snrRawVal =
            (laneSnrList.at(intra * 2 + 1) << 8) | laneSnrList.at(intra * 2);
        laneStats.snr() = CmisFieldInfo::getSnr(snrRawVal);
      }
      laneStats.timeCollected() = std::time(nullptr);
    }
  }

  for (auto& laneStats : laneStatsList) {
    prbsStats.laneStats()->push_back(laneStats);
  }
  prbsStats.timeCollected() = std::time(nullptr);
  return prbsStats;
}

std::optional<uint64_t> CmisModule::getDatapathMaxDelayFromModuleSpec(
    bool init) {
  // Read the datapath init/deinit max time from module.
  uint8_t specVal;
  readCmisField(CmisField::MAX_DPINIT_TIME, &specVal);
  // MAX_DP_DEINIT_TIME: bits 7-4
  // MAX_DP_INIT_TIME: bits 3-0
  uint8_t spec = 0;
  if (init) {
    spec = specVal & DP_INIT_MAX_MASK;
  } else {
    spec = specVal & DP_DINIT_MAX_MASK;
    spec >>= DP_DINIT_BITSHIFT;
  }
  auto itr = DpInitValToTimeMap.find(spec);
  if (itr != DpInitValToTimeMap.end()) {
    uint64_t maxTime = itr->second;
    QSFP_LOG(INFO, this) << fmt::format(
        "Datapath max {:s} time from spec is {:d} uSec",
        init ? "init" : "deinit",
        maxTime);
    return maxTime;
  }
  QSFP_LOG(ERR, this) << fmt::format(
      "Datapath max {:s} time unable to retrieve from val map spec {:x}",
      init ? "init" : "deinit",
      spec);
  return std::nullopt;
}

uint64_t CmisModule::getExpectedDatapathDelayUsec(bool init) {
  if (isTunableOptics() || isAecModule()) {
    // For tunable optics (ZR modules) and AEC cables, use module advertisement.
    auto maxTime = getDatapathMaxDelayFromModuleSpec(init);
    if (maxTime.has_value()) {
      QSFP_LOG(INFO, this) << fmt::format(
          "Module time required for {} is {}",
          (init) ? "init" : "deinit",
          maxTime.value());
      return maxTime.value();
    } else {
      QSFP_LOG(ERR, this) << fmt::format(
          "Module did not specify the max time required for {}",
          (init) ? "init" : "deinit");
    }
  } else {
    QSFP_LOG(INFO, this) << fmt::format(
        "Default max Init/DeInit time {:d} uSec", kUsecDatapathStateUpdateTime);
  }

  return kUsecDatapathStateUpdateTime;
}

uint64_t CmisModule::maxDatapathStatePolls(bool init) {
  return getExpectedDatapathDelayUsec(init) / kUsecDatapathStatePollTime;
}

bool CmisModule::isDatapathUpdated(
    uint8_t laneMask,
    const std::vector<CmisLaneState>& states,
    uint8_t bank) {
  // laneMask carries intra-bank lane bits; map each back to its global lane so
  // the bank-aware getDatapathLaneStateLocked reads the right bank's page.
  const auto moduleHostLanes = numHostLanes();
  for (uint8_t intraLane = 0; intraLane < kMaxOsfpNumLanes; intraLane++) {
    if (!((1 << intraLane) & laneMask)) {
      continue;
    }
    auto lane = globalLane(bank, intraLane);
    if (lane >= moduleHostLanes) {
      // A port's host lane mask can be wider than the module's actual datapath
      // (e.g. a full-bank 0xff mask on a 4-lane module). Lanes beyond
      // numHostLanes() have no datapath and report DATA_PATH_STATE 0, which
      // never matches a target state
      continue;
    }
    auto dpState = getDatapathLaneStateLocked(lane, false);
    if (std::find(states.begin(), states.end(), dpState) == states.end()) {
      return false;
    }
  }
  return true;
}

uint8_t CmisModule::getActiveAppSelLaneMask(uint8_t bank) {
  AllLaneConfig activeCtrl;
  readCmisField(
      CmisField::ACTIVE_CTRL_ALL_LANES, activeCtrl.data(), false, bank);
  uint8_t mask = 0;
  for (int lane = 0; lane < kMaxOsfpNumLanes; lane++) {
    if ((activeCtrl[lane] & APP_SEL_MASK) >> APP_SEL_BITSHIFT) {
      mask |= (1 << lane);
    }
  }
  return mask;
}

void CmisModule::resetDataPath(const std::string& portName) {
  if (flatMem_) {
    return;
  }
  // Only reset lanes that are part of an active application. Lanes left unused
  // by the current application (e.g. lanes 6-7 in 3x200G ZR) never reach
  // DPActivated, so including them would make datapath init never complete.
  uint8_t hostLaneMask = getActiveAppSelLaneMask(/*bank=*/0);
  if (hostLaneMask == 0) {
    hostLaneMask = 0xFF;
  }
  const uint8_t skippedLaneMask =
      hostLaneMask & datapathInitDoneInProgramMask_[0];
  hostLaneMask &= ~skippedLaneMask;
  if (hostLaneMask == 0) {
    QSFP_LOG(INFO, this) << fmt::format(
        "Port {} skipping datapath reset, lanes {:#x} were initialized in this programming cycle",
        portName,
        skippedLaneMask);
    return;
  }
  QSFP_LOG(INFO, this) << fmt::format(
      "Port {} resetting datapath on host lane mask {:#x}, skipping lanes {:#x} initialized in this programming cycle",
      portName,
      hostLaneMask,
      skippedLaneMask);
  resetDataPathWithFunc(portName, std::nullopt, hostLaneMask);
}

void CmisModule::resetDatapathProgrammingStateLocked() {
  if (portDatapathStates_.empty()) {
    return;
  }
  QSFP_LOG(INFO, this) << fmt::format(
      "Discarding datapath programming state for {} port(s) after module reset",
      portDatapathStates_.size());
  portDatapathStates_.clear();
}

bool CmisModule::dataPathProgram(
    const std::string& portName,
    uint8_t hostLaneMask,
    bool isInit,
    uint8_t bank) {
  if (flatMem_) {
    return true;
  }

  auto& dpState = portDatapathStates_[portName];
  auto& timers = isInit ? dpState.initTimers : dpState.deInitTimers;
  auto& dpDone = isInit ? dpState.dpInitDone : dpState.dpDeinitDone;
  auto& dpFailureCounter =
      isInit ? dpState.dpInitFailureCounter : dpState.dpDeinitFailureCounter;

  // For deinit, check if already done
  if (!isInit && dpState.dpDeinitDone) {
    QSFP_LOG(INFO, this) << "Port " << portName
                         << " data path deinit already done";
    return true;
  }

  const std::string opName = isInit ? "init" : "deinit";
  const std::string activationName = isInit ? "ACTIVATION" : "DEACTIVATION";

  // If programming start timer is not set, set it and trigger the operation
  if (timers.progStartTimer.time_since_epoch().count() == 0) {
    timers.progStartTimer = std::chrono::steady_clock::now();

    // Read current register value
    uint8_t dataPathDeInitReg;
    readCmisField(CmisField::DATA_PATH_DEINIT, &dataPathDeInitReg, false, bank);

    // For init: clear bits (release lanes from deactivation)
    // For deinit: set bits (deactivate lanes)
    uint8_t dataPathDeInit = isInit ? (dataPathDeInitReg & ~hostLaneMask)
                                    : (dataPathDeInitReg | hostLaneMask);
    writeCmisField(CmisField::DATA_PATH_DEINIT, &dataPathDeInit, false, bank);

    QSFP_LOG(INFO, this) << fmt::format(
        "Port {} starting datapath {}", portName, opName);
  }

  // Get expected delay from module spec
  auto expectedDelayUsec = getExpectedDatapathDelayUsec(isInit);

  // Target states for init/deinit
  std::vector<CmisLaneState> targetStates = isInit
      ? std::vector<
            CmisLaneState>{CmisLaneState::ACTIVATED, CmisLaneState::DATAPATH_INITIALIZED}
      : std::vector<CmisLaneState>{CmisLaneState::DEACTIVATED};

  // Give the operation a single poll interval to progress, then check once. We
  // do not block for the whole datapath time here: dataPathProgram is called
  // repeatedly (the transceiver state machine re-fires programming each
  // refresh, and HAL tests poll), progStartTimer persists in
  // portDatapathStates_ so each call resumes without re-triggering the
  // datapath, and expectedDelayUsec above is the cumulative deadline across
  // those attempts. A datapath that needs longer than this single poll is
  // simply confirmed on a later refresh, once the hardware has finished -- the
  // programming itself still completes in the hardware's own time. Polling only
  // briefly keeps the module lock from being held for long on a slow (e.g. ZR)
  // optic.
  /* sleep override */
  usleep(kUsecDatapathStatePollTime);

  if (isDatapathUpdated(hostLaneMask, targetStates, bank)) {
    // Mark operation as done
    timers.progDoneTimer = std::chrono::steady_clock::now();
    dpDone = true;

    // Calculate and store elapsed time
    timers.elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        timers.progDoneTimer - timers.progStartTimer);

    QSFP_LOG(INFO, this) << fmt::format(
        "Port {} DP_{} completed in {} ms, dp_{}_failure_counter {}",
        portName,
        activationName,
        timers.elapsedTime.count(),
        opName,
        dpFailureCounter);
    timers.progStartTimer = std::chrono::steady_clock::time_point();
    /*
     * If data_path_init is completed set the dpInitDone and dpDeInitDone flag
     * to false for the next ProgramTransceiver event
     */
    if (isInit) {
      QSFP_LOG(INFO, this) << "Port " << portName
                           << " data path init done resetting flags";
      dpState.dpInitDone = false;
      dpState.dpDeinitDone = false;
    }
    return true;
  }

  // If operation exceeded expected time, log error
  auto currentTime = std::chrono::steady_clock::now();
  auto elapsedUsec = std::chrono::duration_cast<std::chrono::microseconds>(
                         currentTime - timers.progStartTimer)
                         .count();

  if (elapsedUsec > expectedDelayUsec) {
    // Reset timer and increment failure counter
    timers.progStartTimer = std::chrono::steady_clock::time_point();
    dpFailureCounter++;
    QSFP_LOG(ERR, this) << fmt::format(
        "Port {} datapath {} exceeded expected time ({} us > {} us), "
        "dp_{}_failure_counter {}",
        portName,
        isInit ? "activation" : "deactivation",
        elapsedUsec,
        expectedDelayUsec,
        opName,
        dpFailureCounter);
    return false;
  }

  if (!dpDone) {
    QSFP_LOG(INFO, this) << fmt::format(
        "Port {} DP_{} not compeleted", portName, activationName);
  }

  return false;
}

void CmisModule::resetDataPathForTunableOptics(
    const std::string& portName,
    std::optional<std::function<void()>> afterDataPathDeinitFunc,
    uint8_t hostLaneMask,
    uint8_t bank,
    std::optional<uint8_t> initLaneMask) {
  const uint8_t initMask = initLaneMask.value_or(hostLaneMask);
  // Step 1: De-initialize data path
  if (!dataPathProgram(portName, hostLaneMask, false, bank)) {
    throw FbossError(
        "Data path de-initialize not yet completed for port ", portName);
  }

  // Step 2: Execute callback function after deactivation
  if (afterDataPathDeinitFunc) {
    auto& dpState = portDatapathStates_[portName];
    auto& initTimers = dpState.initTimers;
    // If dp-initialization start timer is not set, invoke the AppSel callback
    if (initTimers.progStartTimer.time_since_epoch().count() == 0) {
      (*afterDataPathDeinitFunc)();
    } else {
      QSFP_LOG(INFO, this) << fmt::format(
          "DATA_PATH_INIT in progresss dpInitTimer {:d}",
          initTimers.progStartTimer.time_since_epoch().count());
    }
  }

  // Step 3: Initialize data path. Retries keep checking the lanes the init was
  // started on, even if the caller now passes a narrower mask.
  auto& dpState = portDatapathStates_[portName];
  if (dpState.initTimers.progStartTimer.time_since_epoch().count() == 0) {
    dpState.initLaneMask = initMask;
  }
  const uint8_t waitInitMask = dpState.initLaneMask.value_or(initMask);
  if (!dataPathProgram(portName, waitInitMask, true, bank)) {
    throw FbossError(
        "Data path initialize not yet completed for port ", portName);
  }

  dpState.initLaneMask.reset();
  datapathInitDoneInProgramMask_[bank] |= waitInitMask;

  // Step 4: Workaround to set PCS to AM Transparent: T289920421
  setPcsToAmTransparent();

  // Step 5: Update last reset time for affected lanes
  for (int lane = 0; lane < CmisModule::kMaxOsfpNumLanes; lane++) {
    if ((1 << lane) & hostLaneMask) {
      lastDatapathResetTimes_[lane] = std::time(nullptr);
    }
  }
}

void CmisModule::resetDataPathWithFunc(
    const std::string& portName,
    std::optional<std::function<void()>> afterDataPathDeinitFunc,
    uint8_t hostLaneMask,
    uint8_t bank,
    std::optional<uint8_t> initLaneMask) {
  if (flatMem_) {
    return;
  }

  // Lanes deinitialized (hostLaneMask) may be a superset of the lanes to
  // initialize: lanes left without an application stay in deinit
  const uint8_t initMask = initLaneMask.value_or(hostLaneMask);
  if (isTunableOptics()) {
    // For tunable optics (ZR modules), use dataPathProgram for deinit/init
    resetDataPathForTunableOptics(
        portName, afterDataPathDeinitFunc, hostLaneMask, bank, initMask);
  } else {
    // For non-tunable optics, use inline deinit/init logic
    uint8_t dataPathDeInitReg;
    readCmisField(CmisField::DATA_PATH_DEINIT, &dataPathDeInitReg, false, bank);
    // First deactivate all the lanes
    uint8_t dataPathDeInit = dataPathDeInitReg | hostLaneMask;
    writeCmisField(CmisField::DATA_PATH_DEINIT, &dataPathDeInit, false, bank);

    // Wait for all datapath state machines to get Deactivated
    const auto maxRetriesDeInit = maxDatapathStatePolls(/*init=*/false);

    auto retries = 0;
    while (retries++ < maxRetriesDeInit) {
      /* sleep override */
      usleep(kUsecDatapathStatePollTime);
      if (isDatapathUpdated(hostLaneMask, {CmisLaneState::DEACTIVATED}, bank)) {
        break;
      }
    }
    if (retries >= maxRetriesDeInit) {
      QSFP_LOG(ERR, this) << fmt::format(
          "Datapath could not deactivate even after waiting {:d} uSec",
          kUsecDatapathStateUpdateTime);
    }

    // Call the afterDataPathDeinitFunc() after deactivate all lanes
    if (afterDataPathDeinitFunc) {
      (*afterDataPathDeinitFunc)();
    }

    // Release the lanes from DeInit.
    dataPathDeInit = (dataPathDeInitReg | hostLaneMask) & ~initMask;
    writeCmisField(CmisField::DATA_PATH_DEINIT, &dataPathDeInit, false, bank);

    // Wait for the datapath to come out of deactivated state
    const auto maxRetriesInit = maxDatapathStatePolls(/*init=*/true);
    retries = 0;
    while (retries++ < maxRetriesInit) {
      /* sleep override */
      usleep(kUsecDatapathStatePollTime);
      if (isDatapathUpdated(
              initMask,
              {CmisLaneState::ACTIVATED, CmisLaneState::DATAPATH_INITIALIZED},
              bank)) {
        break;
      }
    }
    if (retries >= maxRetriesInit) {
      QSFP_LOG(ERR, this) << fmt::format(
          "Datapath didn't come out of deactivated state even after waiting {:d} uSec",
          kUsecDatapathStateUpdateTime);
    } else {
      datapathInitDoneInProgramMask_[bank] |= initMask;
    }

    // Update the last datapath reset time for all the lanes in hostLaneMask
    for (int lane = 0; lane < CmisModule::kMaxOsfpNumLanes; lane++) {
      if ((1 << lane) & hostLaneMask) {
        lastDatapathResetTimes_[lane] = std::time(nullptr);
      }
    }
  }

  QSFP_LOG(INFO, this) << fmt::format(
      "DATA_PATH_DEINIT set and reset done for host lane mask 0x{:#x}",
      hostLaneMask);
}

/*
 * getDatapathLaneStateLocked
 *
 * Reads the datapath state for a given lane of transceiver either from HW or
 * from SW cache (default)
 */
CmisLaneState CmisModule::getDatapathLaneStateLocked(
    uint8_t lane,
    bool readFromCache) {
  // lane is a global lane; it maps to bank = lane / kMaxOsfpNumLanes. Reject
  // lanes outside the module's banked lane space rather than reading a bank
  // that doesn't exist.
  if (lane >= getMaxNumBanks() * kMaxOsfpNumLanes) {
    throw FbossError(
        fmt::format(
            "Datapath lane {} is out of range for a module with {} bank(s)",
            lane,
            getMaxNumBanks()));
  }
  if (readFromCache) {
    return (CmisLaneState)getLaneNibble(CmisField::DATA_PATH_STATE, lane);
  }
  // DATA_PATH_STATE is a 4-byte nibble-packed field per bank; read the lane's
  // bank explicitly.
  uint8_t bank = lane / kMaxOsfpNumLanes;
  int intraLane = lane % kMaxOsfpNumLanes;
  uint8_t dataPathStates[4];
  readCmisField(
      CmisField::DATA_PATH_STATE,
      dataPathStates,
      /*skipBankAndPageChange=*/false,
      bank);
  auto laneDatapathState = dataPathStates[intraLane / 2];
  laneDatapathState = ((intraLane % 2) == 0) ? (laneDatapathState & 0xF)
                                             : ((laneDatapathState >> 4) & 0xF);
  return (CmisLaneState)laneDatapathState;
}

void CmisModule::updateVdmCacheLocked() {
  if (!isVdmSupported()) {
    QSFP_LOG(DBG5, this) << "Doesn't support VDM, skip updating VDM cache";
    return;
  }

  std::array<uint8_t*, 4> confPageBuffers = {
      page20_, page21_, page22_, page23_};
  std::array<BankedPage*, 4> dataPageBuffers = {
      &page24_, &page25_, &page26_, &page27_};

  for (uint8_t group = 1; group <= vdmSupportedGroupsMax_; group++) {
    uint8_t idx = group - 1;
    // Cache config pages only once (static). Config (20h-23h) is bank-invariant
    // and read on bank 0 (readBankedPage below leaves the module on bank 0).
    if (!staticPagesCached_) {
      readCmisField(kVdmConfPages[idx], confPageBuffers[idx]);
    }
    // Always read data pages (dynamic), for every bank on multi-bank modules.
    readBankedPage(kVdmDataPages[idx], *dataPageBuffers[idx]);
  }

  if (vdmSupportedGroupsMax_ >= 1) {
    staticPagesCached_ = true;
  }
  // Read C-CMIS PM pages, Rx Consequent Action control, and Host Lane
  // Provisioning Advertisement for coherent optics
  if (isTunableOptics()) {
    readCmisField(CmisField::PAGE_UPPER34H, page34_);
    readCmisField(CmisField::PAGE_UPPER35H, page35_);
    readCmisField(CmisField::PAGE_UPPER38H, page38_);
    readCmisField(CmisField::PAGE_UPPER45H, page45_);
  }
}

void CmisModule::updateCmisStateChanged(
    ModuleStatus& moduleStatus,
    std::optional<ModuleStatus> curModuleStatus) {
  if (!present_) {
    return;
  }
  // If `moduleStatus` already has true `cmisStateChanged`, no need to update
  if (auto cmisStateChanged = moduleStatus.cmisStateChanged();
      cmisStateChanged && *cmisStateChanged) {
    return;
  }
  // Otherwise, update it using curModuleStatus
  if (curModuleStatus) {
    if (auto curCmisStateChanged = curModuleStatus->cmisStateChanged()) {
      moduleStatus.cmisStateChanged() = *curCmisStateChanged;
    }
  } else {
    // If curModuleStatus is nullopt, we call getModuleStateChanged() to get
    // the latest moduleStateChanged
    moduleStatus.cmisStateChanged() = getModuleStateChanged();
  }
}

bool CmisModule::supportRemediate() {
  return supportRemediate_;
}

/*
 * setTransceiverTx
 *
 * Set the Tx output enabled/disabled for the given channels of a transceiver
 * in either line side or host side. For line side, this will cause LOS on the
 * peer optics Rx. For host side, this will cause LOS on the corresponding
 * IPHY lanes or the XPHY lanes in case of system with external PHY
 */
bool CmisModule::setTransceiverTxLocked(
    const std::string& portName,
    phy::Side side,
    std::optional<uint8_t> userChannelMask,
    bool enable) {
  // Get the list of lanes to disable/enable the Tx output
  auto tcvrLanes = getTcvrLanesForPort(portName, side);
  if (tcvrLanes.empty()) {
    QSFP_LOG(ERR, this) << fmt::format(
        "Empty lane list for port {:s}", portName);
    return false;
  }

  return setTransceiverTxImplLocked(tcvrLanes, side, userChannelMask, enable);
}

bool CmisModule::setTransceiverTxImplLocked(
    const std::set<uint8_t>& tcvrLanes,
    phy::Side side,
    std::optional<uint8_t> userChannelMask,
    bool enable) {
  if (tcvrLanes.empty()) {
    QSFP_LOG(ERR, this) << "Empty lane list";
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

  // Set the Tx output register for these lanes in given direction
  auto txDisableRegister =
      (side == phy::Side::LINE) ? CmisField::TX_DISABLE : CmisField::RX_DISABLE;
  uint8_t txDisableVal;

  // A port's lanes are confined to one bank; the TX/RX disable register is
  // per-bank, so reduce the lanes to intra-bank offsets and select that bank.
  const uint8_t bank = laneToBank(*tcvrLanes.begin());
  std::set<uint8_t> intraBankLanes;
  for (auto lane : tcvrLanes) {
    // Enforce the single-bank invariant: lanes from another bank would be
    // silently squashed into this bank's channel mask.
    if (laneToBank(lane) != bank) {
      throw FbossError(
          fmt::format(
              "Port lanes span multiple banks (lane {} in bank {}, expected bank {})",
              lane,
              laneToBank(lane),
              bank));
    }
    intraBankLanes.insert(laneInBank(lane));
  }

  readCmisField(txDisableRegister, &txDisableVal, false, bank);

  // userChannelMask bits are global lane indices, but setTxChannelMask now
  // matches against intra-bank lanes; reduce the mask to this bank's intra-bank
  // numbering (dropping bits for lanes outside the port's bank). For bank 0
  // this is the identity.
  std::optional<uint8_t> intraBankChannelMask;
  if (userChannelMask.has_value()) {
    uint8_t reduced = 0;
    uint8_t remaining = *userChannelMask;
    for (uint8_t globalLane = 0; remaining; remaining >>= 1, ++globalLane) {
      if ((remaining & 0x1) && laneToBank(globalLane) == bank) {
        reduced |= (1 << laneInBank(globalLane));
      }
    }
    intraBankChannelMask = reduced;
  }

  txDisableVal = setTxChannelMask(
      intraBankLanes, intraBankChannelMask, enable, txDisableVal);

  writeCmisField(txDisableRegister, &txDisableVal, false, bank);
  return true;
}

uint64_t CmisModule::getFwUpgradeCdbWriteDelayUsec() const {
  auto mediaInterface = getModuleMediaInterface();
  switch (mediaInterface) {
    case MediaInterfaceCode::CWDM4_100G:
    case MediaInterfaceCode::FR1_100G:
    case MediaInterfaceCode::FR4_200G:
    case MediaInterfaceCode::FR4_400G:
    case MediaInterfaceCode::LR4_400G_10KM:
    case MediaInterfaceCode::DR4_400G:
    case MediaInterfaceCode::FR4_2x400G:
    case MediaInterfaceCode::DR4_2x400G:
    case MediaInterfaceCode::FR8_800G:
    case MediaInterfaceCode::FR4_LITE_2x400G:
    case MediaInterfaceCode::LR4_2x400G_10KM:
    case MediaInterfaceCode::LR4_200G:
    case MediaInterfaceCode::FR4_LPO_2x400G:
    case MediaInterfaceCode::FR4_800G:
      return POST_I2C_WRITE_DELAY_CDB_US;
    default:
      return POST_I2C_WRITE_NO_DELAY_US;
  }
}

bool CmisModule::upgradeFirmwareLockedImpl(FbossFirmware* fbossFw) const {
  QSFP_LOG(INFO, this) << "Upgrading CMIS Module Firmware";

  auto fwUpgradeObj = std::make_unique<CmisFirmwareUpgrader>(
      qsfpImpl_,
      getID(),
      fbossFw,
      getCmisRevision().first,
      cachedCdbWriteDelayUsec_.value_or(POST_I2C_WRITE_DELAY_CDB_US));

  bool ret = fwUpgradeObj->cmisModuleFirmwareUpgrade();
  return ret;
}

/*
 * setTransceiverLoopbackLocked
 *
 * Sets or resets the loopback on the given lanes for the SW Port. side selects
 * the host (SYSTEM) or media (LINE) side of the module, and mode selects the
 * input or output loopback on that side (CMIS page 13h bytes 180-183).
 */
void CmisModule::setTransceiverLoopbackLocked(
    const std::string& portName,
    phy::Side side,
    bool setLoopback,
    phy::LoopbackMode mode) {
  // Get the list of lanes to disable/enable the loopback
  auto tcvrLanes = getTcvrLanesForPort(portName, side);
  if (tcvrLanes.empty()) {
    QSFP_LOG(ERR, this) << fmt::format(
        "No {:s} lanes available for port {:s}",
        (side == phy::Side::SYSTEM ? "HOST" : "LINE"),
        portName);
    return;
  }

  const bool hostSide = (side == phy::Side::SYSTEM);
  const bool input = (mode == phy::LoopbackMode::INPUT);
  CmisField regField;
  bool supported = false;
  std::optional<LoopbackCapability> lbCap;
  {
    auto diagsCapability = diagsCapability_.rlock();
    if (diagsCapability->has_value()) {
      lbCap = (*diagsCapability)->loopbackCapability().to_optional();
    }
  }
  if (hostSide && input) {
    regField = CmisField::HOST_INPUT_LB_EN;
    supported = lbCap && *lbCap->hostSideInput();
  } else if (hostSide) {
    regField = CmisField::HOST_OUTPUT_LB_EN;
    supported = lbCap && *lbCap->hostSideOutput();
  } else if (input) {
    regField = CmisField::MEDIA_INPUT_LB_EN;
    supported = lbCap && *lbCap->mediaSideInput();
  } else {
    regField = CmisField::MEDIA_OUTPUT_LB_EN;
    supported = lbCap && *lbCap->mediaSideOutput();
  }

  if (!supported) {
    // Nothing can be enabled in an unsupported loopback, so there is nothing
    // to clear; this keeps "disable all loopbacks" callers simple.
    if (!setLoopback) {
      return;
    }
    throw FbossError(
        fmt::format(
            "Module {:s} does not support {:s} side {:s} loopback",
            portName,
            hostSide ? "host" : "media",
            apache::thrift::util::enumNameSafe(mode)));
  }

  // A port's lanes are confined to one bank; the loopback enable register is
  // per-bank, so reduce the lanes to intra-bank offsets and select that bank.
  const uint8_t bank = laneToBank(*tcvrLanes.begin());
  std::set<uint8_t> intraBankLanes;
  for (auto lane : tcvrLanes) {
    // Enforce the single-bank invariant: lanes from another bank would be
    // silently squashed into this bank's channel mask.
    if (laneToBank(lane) != bank) {
      throw FbossError(
          fmt::format(
              "Port lanes span multiple banks (lane {} in bank {}, expected bank {})",
              lane,
              laneToBank(lane),
              bank));
    }
    intraBankLanes.insert(laneInBank(lane));
  }

  if (setLoopback && !*lbCap->simultaneousHostAndMediaSide()) {
    // Refuse rather than silently clear a loopback on the opposite side.
    auto oppositeSide = hostSide ? phy::Side::LINE : phy::Side::SYSTEM;
    std::map<uint8_t, uint8_t> oppositeLaneMaskPerBank;
    for (auto lane : getTcvrLanesForPort(portName, oppositeSide)) {
      oppositeLaneMaskPerBank[laneToBank(lane)] |= (1 << laneInBank(lane));
    }
    auto oppositeFields = hostSide
        ? std::array<
              CmisField,
              2>{CmisField::MEDIA_OUTPUT_LB_EN, CmisField::MEDIA_INPUT_LB_EN}
        : std::array<CmisField, 2>{
              CmisField::HOST_OUTPUT_LB_EN, CmisField::HOST_INPUT_LB_EN};
    for (const auto& [oppositeBank, laneMask] : oppositeLaneMaskPerBank) {
      for (auto field : oppositeFields) {
        uint8_t enabled;
        readCmisField(field, &enabled, false, oppositeBank);
        if (enabled & laneMask) {
          throw FbossError(
              fmt::format(
                  "Module {:s} does not support simultaneous host and media side loopback; disable the {:s} side loopback first",
                  portName,
                  hostSide ? "media" : "host"));
        }
      }
    }
  }

  uint8_t lbEnable;
  readCmisField(regField, &lbEnable, false, bank);

  lbEnable =
      setTxChannelMask(intraBankLanes, std::nullopt, !setLoopback, lbEnable);

  writeCmisField(regField, &lbEnable, false, bank);
}

/*
 * f16ToDouble
 *
 * Convert CMIS VDM F16 type (16 bit) to a floating point number
 */
double CmisModule::f16ToDouble(uint8_t byte0, uint8_t byte1) {
  double ber;
  int expon = byte0 >> 3;
  expon -= 24;
  int mant = ((byte0 & 0x7) << 8) | byte1;
  ber = mant * exp10(expon);
  return ber;
}

/*
 * getVdmDataValPtr
 *
 * Returns the VDM data value pointer from register cache along with VDM data
 * length, otherwise returns null
 */
std::pair<std::optional<const uint8_t*>, int> CmisModule::getVdmDataValPtr(
    VdmConfigType vdmConf,
    uint8_t bank) {
  const uint8_t* data;
  int offset;
  int length;
  int dataAddress;

  if (!cacheIsValid()) {
    return std::make_pair(std::nullopt, 0);
  }
  auto vdmDiagsValLocation = getVdmDiagsValLocation(vdmConf);
  if (vdmDiagsValLocation.vdmConfImplementedByModule) {
    dataAddress = static_cast<int>(vdmDiagsValLocation.vdmValPage);
    offset = vdmDiagsValLocation.vdmValOffset;
    length = vdmDiagsValLocation.vdmValLength;
    // The VDM config (and thus the location) is bank-invariant; the data lives
    // per-bank on pages 24h-27h. bank 0 resolves to the same bytes as before.
    data = getBankedQsfpValuePtr(dataAddress, offset, length, bank);
    return std::make_pair(data, length);
  }
  return std::make_pair(std::nullopt, 0);
}

std::map<int, double> CmisModule::getVdmLaneValues(
    VdmConfigType vdmConf,
    const std::function<double(const std::array<uint8_t, 2>&)>& decode) {
  std::map<int, double> result;
  for (uint8_t bank = 0; bank < getMaxNumBanks(); bank++) {
    auto [data, length] = getVdmDataValPtr(vdmConf, bank);
    if (!data) {
      continue;
    }
    // Each lane's value occupies kVdmDescriptorLength (2) consecutive bytes;
    // iterate while a full 2-byte lane entry fits within the returned region.
    for (int intra = 0; (intra + 1) * kVdmDescriptorLength <= length; intra++) {
      int globalLane = bank * kMaxOsfpNumLanes + intra;
      const int byteOffset = intra * kVdmDescriptorLength;
      result[globalLane] =
          decode({data.value()[byteOffset], data.value()[byteOffset + 1]});
    }
  }
  return result;
}

std::optional<double> CmisModule::getVdmLaneValue(
    VdmConfigType vdmConf,
    int globalLane,
    const std::function<double(const std::array<uint8_t, 2>&)>& decode) {
  uint8_t bank = globalLane / kMaxOsfpNumLanes;
  int intra = globalLane % kMaxOsfpNumLanes;
  auto [data, length] = getVdmDataValPtr(vdmConf, bank);
  // The lane's full 2-byte entry (bytes byteOffset and byteOffset + 1) must be
  // present in the returned region.
  const int byteOffset = intra * kVdmDescriptorLength;
  if (data && length >= byteOffset + kVdmDescriptorLength) {
    return decode({data.value()[byteOffset], data.value()[byteOffset + 1]});
  }
  return std::nullopt;
}

std::map<int, double> CmisModule::getVdmLaneValuesU16(VdmConfigType vdmConf) {
  return getVdmLaneValues(vdmConf, [](const std::array<uint8_t, 2>& d) {
    return d[0] + (d[1] / kU16TypeLsbDivisor);
  });
}

std::map<int, double> CmisModule::getVdmLaneValuesF16(VdmConfigType vdmConf) {
  return getVdmLaneValues(vdmConf, [this](const std::array<uint8_t, 2>& d) {
    return f16ToDouble(d[0], d[1]);
  });
}

std::optional<double> CmisModule::getVdmLaneValueF16(
    VdmConfigType vdmConf,
    int globalLane) {
  return getVdmLaneValue(
      vdmConf, globalLane, [this](const std::array<uint8_t, 2>& d) {
        return f16ToDouble(d[0], d[1]);
      });
}

/*
 * readU16VdmValue
 *
 * Read a single U16 (unsigned 16-bit) VDM value and convert to double.
 * Returns std::nullopt if the VDM data is not available.
 */
std::optional<double> CmisModule::readU16VdmValue(
    VdmConfigType vdmConf,
    double lsb) {
  auto [data, length] = getVdmDataValPtr(vdmConf);
  if (data && length >= 2) {
    return readU16(data.value(), 0) * lsb;
  }
  return std::nullopt;
}

/*
 * readS16VdmValue
 *
 * Read a single S16 (signed 16-bit) VDM value and convert to double.
 * Returns std::nullopt if the VDM data is not available.
 */
std::optional<double> CmisModule::readS16VdmValue(
    VdmConfigType vdmConf,
    double lsb) {
  auto [data, length] = getVdmDataValPtr(vdmConf);
  if (data && length >= 2) {
    return readS16(data.value(), 0) * lsb;
  }
  return std::nullopt;
}

/*
 * fillVdmPerfMonitorSnr
 *
 * Private function to fill in the VDM performance monitor stats for SNR
 */
bool CmisModule::fillVdmPerfMonitorSnr(VdmPerfMonitorStats& vdmStats) {
  if (!isVdmSupported() || !cacheIsValid()) {
    return false;
  }

  // Get the SW Ports and the Channels for each port
  auto& portNameToMediaLanes = getPortNameToMediaLanes();

  // Fill in channel SNR Media In (keyed by global lane across all banks).
  std::map<int, double> channelSnrMap = getVdmLaneValuesU16(SNR_MEDIA_IN);
  for (auto& [portName, mediaLanes] : portNameToMediaLanes) {
    for (auto& mediaLane : mediaLanes) {
      vdmStats.mediaPortVdmStats()[portName].laneSNR()[mediaLane] =
          channelSnrMap[mediaLane];
    }
  }
  return true;
}

/*
 * fillVdmPerfMonitorBer
 *
 * Private function to fill in the VDM performance monitor stats for BER (Bit
 * Error Rate) on both Media and Host side
 */
bool CmisModule::fillVdmPerfMonitorBer(VdmPerfMonitorStats& vdmStats) {
  if (!isVdmSupported() || !cacheIsValid()) {
    return false;
  }

  // Get the SW Ports and the Channels for each port
  // Get the SW Ports and the Channels for each port
  auto& portNameToMediaLanes = getPortNameToMediaLanes();
  auto& portNameToHostLanes = getPortNameToHostLanes();

  // Lambda to extract a BER / Frame Error value for a given VDM config type at
  // a SW Port's (global) start lane, selecting that lane's bank.
  auto captureVdmBerFrameErrorValues =
      [this](VdmConfigType vdmConfType, int startLane) {
        return getVdmLaneValueF16(vdmConfType, startLane);
      };

  // Fill in Media side per port values
  for (auto& [portName, mediaLanes] : portNameToMediaLanes) {
    auto startLane = *mediaLanes.begin();

    // Fill in Media Pre FEC BER values
    if (auto berVal = captureVdmBerFrameErrorValues(
            PRE_FEC_BER_MEDIA_IN_MIN, startLane)) {
      vdmStats.mediaPortVdmStats()[portName].datapathBER()->min() =
          berVal.value();
    }
    if (auto berVal = captureVdmBerFrameErrorValues(
            PRE_FEC_BER_MEDIA_IN_MAX, startLane)) {
      vdmStats.mediaPortVdmStats()[portName].datapathBER()->max() =
          berVal.value();
    }
    if (auto berVal = captureVdmBerFrameErrorValues(
            PRE_FEC_BER_MEDIA_IN_AVG, startLane)) {
      vdmStats.mediaPortVdmStats()[portName].datapathBER()->avg() =
          berVal.value();
    }
    if (auto berVal = captureVdmBerFrameErrorValues(
            PRE_FEC_BER_MEDIA_IN_CUR, startLane)) {
      vdmStats.mediaPortVdmStats()[portName].datapathBER()->cur() =
          berVal.value();
    }
  }

  // Fill in Host side per port values
  for (auto& [portName, hostLanes] : portNameToHostLanes) {
    auto startLane = *hostLanes.begin();

    // Fill in Host Pre FEC BER values
    if (auto berVal =
            captureVdmBerFrameErrorValues(PRE_FEC_BER_HOST_IN_MIN, startLane)) {
      vdmStats.hostPortVdmStats()[portName].datapathBER()->min() =
          berVal.value();
    }
    if (auto berVal =
            captureVdmBerFrameErrorValues(PRE_FEC_BER_HOST_IN_MAX, startLane)) {
      vdmStats.hostPortVdmStats()[portName].datapathBER()->max() =
          berVal.value();
    }
    if (auto berVal =
            captureVdmBerFrameErrorValues(PRE_FEC_BER_HOST_IN_AVG, startLane)) {
      vdmStats.hostPortVdmStats()[portName].datapathBER()->avg() =
          berVal.value();
    }
    if (auto berVal =
            captureVdmBerFrameErrorValues(PRE_FEC_BER_HOST_IN_CUR, startLane)) {
      vdmStats.hostPortVdmStats()[portName].datapathBER()->cur() =
          berVal.value();
    }
  }
  return true;
}

/*
 * fillVdmPerfMonitorFecErr
 *
 * Private function to fill in the VDM performance monitor stats for FEC Error
 * Rate (Post FEC BER) on both Media and Host side
 */
bool CmisModule::fillVdmPerfMonitorFecErr(VdmPerfMonitorStats& vdmStats) {
  if (!isVdmSupported() || !cacheIsValid()) {
    return false;
  }

  // Get the SW Ports and the Channels for each port
  auto& portNameToMediaLanes = getPortNameToMediaLanes();
  auto& portNameToHostLanes = getPortNameToHostLanes();

  // Lambda to extract a BER / Frame Error value for a given VDM config type at
  // a SW Port's (global) start lane, selecting that lane's bank.
  auto captureVdmBerFrameErrorValues =
      [this](VdmConfigType vdmConfType, int startLane) {
        return getVdmLaneValueF16(vdmConfType, startLane);
      };

  // Fill in Media side per port values
  for (auto& [portName, mediaLanes] : portNameToMediaLanes) {
    auto startLane = *mediaLanes.begin();

    // Fill in Media Post FEC Errored Frames values
    if (auto errFrames =
            captureVdmBerFrameErrorValues(ERR_FRAME_MEDIA_IN_MIN, startLane)) {
      vdmStats.mediaPortVdmStats()[portName].datapathErroredFrames()->min() =
          errFrames.value();
    }
    if (auto errFrames =
            captureVdmBerFrameErrorValues(ERR_FRAME_MEDIA_IN_MAX, startLane)) {
      vdmStats.mediaPortVdmStats()[portName].datapathErroredFrames()->max() =
          errFrames.value();
    }
    if (auto errFrames =
            captureVdmBerFrameErrorValues(ERR_FRAME_MEDIA_IN_AVG, startLane)) {
      vdmStats.mediaPortVdmStats()[portName].datapathErroredFrames()->avg() =
          errFrames.value();
    }
    if (auto errFrames =
            captureVdmBerFrameErrorValues(ERR_FRAME_MEDIA_IN_CUR, startLane)) {
      vdmStats.mediaPortVdmStats()[portName].datapathErroredFrames()->cur() =
          errFrames.value();
    }
  }

  // Fill in Host side per port values
  for (auto& [portName, hostLanes] : portNameToHostLanes) {
    auto startLane = *hostLanes.begin();

    // Fill in Host Post FEC Errored Frame values
    if (auto errFrames =
            captureVdmBerFrameErrorValues(ERR_FRAME_HOST_IN_MIN, startLane)) {
      vdmStats.hostPortVdmStats()[portName].datapathErroredFrames()->min() =
          errFrames.value();
    }
    if (auto errFrames =
            captureVdmBerFrameErrorValues(ERR_FRAME_HOST_IN_MAX, startLane)) {
      vdmStats.hostPortVdmStats()[portName].datapathErroredFrames()->max() =
          errFrames.value();
    }
    if (auto errFrames =
            captureVdmBerFrameErrorValues(ERR_FRAME_HOST_IN_AVG, startLane)) {
      vdmStats.hostPortVdmStats()[portName].datapathErroredFrames()->avg() =
          errFrames.value();
    }
    if (auto errFrames =
            captureVdmBerFrameErrorValues(ERR_FRAME_HOST_IN_CUR, startLane)) {
      vdmStats.hostPortVdmStats()[portName].datapathErroredFrames()->cur() =
          errFrames.value();
    }
  }
  return true;
}

/*
 * fillVdmPerfMonitorFecTail
 *
 * Private function to fill in the VDM performance monitor stats for FEC Tail
 * on both Media and Host side
 */
bool CmisModule::fillVdmPerfMonitorFecTail(VdmPerfMonitorStats& vdmStats) {
  if (!isVdmSupported() || !cacheIsValid()) {
    return false;
  }

  // Get the SW Ports and the Channels for each port
  auto& portNameToMediaLanes = getPortNameToMediaLanes();
  auto& portNameToHostLanes = getPortNameToHostLanes();

  // Lambda to extract FEC tail for a given VDM config type at a SW Port's
  // (global) start lane, selecting that lane's bank.
  auto captureVdmFecTailValues = [this](
                                     VdmConfigType vdmConfType,
                                     int startLane) -> std::optional<double> {
    return getVdmLaneValue(
        vdmConfType, startLane, [](const std::array<uint8_t, 2>& d) {
          return static_cast<double>(d[0] + d[1]);
        });
  };

  // Fill in Media side per port values
  for (auto& [portName, mediaLanes] : portNameToMediaLanes) {
    auto startLane = *mediaLanes.begin();

    // Fill in Media FEC tail values
    if (auto fecTailMax =
            captureVdmFecTailValues(FEC_TAIL_MEDIA_IN_MAX, startLane)) {
      vdmStats.mediaPortVdmStats()[portName].fecTailMax() = fecTailMax.value();
      // FIXME: We should check FEC type and set the max supported FEC tail.
      // FEC Type is currently not available and hence hardcoding to 15 for
      // now
      vdmStats.mediaPortVdmStats()[portName].maxSupportedFecTail() =
          kMaxFecTailRs544;
    }
    if (auto fecTailCurr =
            captureVdmFecTailValues(FEC_TAIL_MEDIA_IN_CURR, startLane)) {
      vdmStats.mediaPortVdmStats()[portName].fecTailCurr() =
          fecTailCurr.value();
    }
  }

  // Fill in Host side per port values
  for (auto& [portName, hostLanes] : portNameToHostLanes) {
    auto startLane = *hostLanes.begin();

    // Fill in Host FEC tail values
    if (auto fecTailMax =
            captureVdmFecTailValues(FEC_TAIL_HOST_IN_MAX, startLane)) {
      vdmStats.hostPortVdmStats()[portName].fecTailMax() = fecTailMax.value();
      // FIXME: We should check FEC type and set the max supported FEC tail.
      // FEC Type is currently not available and hence hardcoding to 15 for
      // now
      vdmStats.hostPortVdmStats()[portName].maxSupportedFecTail() =
          kMaxFecTailRs544;
    }
    if (auto fecTailCurr =
            captureVdmFecTailValues(FEC_TAIL_HOST_IN_CURR, startLane)) {
      vdmStats.hostPortVdmStats()[portName].fecTailCurr() = fecTailCurr.value();
    }
  }
  return true;
}

/*
 * fillVdmPerfMonitorLtp
 *
 * Private function to fill in the VDM performance monitor stats for LTP
 * (Level Transition Parameter) on Media side
 */
bool CmisModule::fillVdmPerfMonitorLtp(VdmPerfMonitorStats& vdmStats) {
  if (!isVdmSupported() || !cacheIsValid()) {
    return false;
  }

  // Get the SW Ports and the Channels for each port
  auto& portNameToMediaLanes = getPortNameToMediaLanes();

  // Fill in channel LTP Media In (keyed by global lane across all banks).
  std::map<int, double> channelLtpMap = getVdmLaneValuesU16(PAM4_LTP_MEDIA_IN);
  for (auto& [portName, mediaLanes] : portNameToMediaLanes) {
    for (auto& mediaLane : mediaLanes) {
      vdmStats.mediaPortVdmStats()[portName].lanePam4LTP()[mediaLane] =
          channelLtpMap[mediaLane];
    }
  }
  return true;
}

/*
 * fillVdmPerfMonitorPam4Data
 *
 * Private function to fill in the VDM performance monitor stats for PAM4 like
 * each level standard deviation, MPI (Multi Path Interference) on Media side
 */
bool CmisModule::fillVdmPerfMonitorPam4Data(VdmPerfMonitorStats& vdmStats) {
  if (!isVdmSupported(3) || !cacheIsValid()) {
    return false;
  }

  // Get the SW Ports and the Channels for each port
  auto& portNameToMediaLanes = getPortNameToMediaLanes();

  // Fill in VDM Advance group3 performance monitoring info

  // Lambda to read the per-global-lane VDM PM value for the given VDM Config.
  auto getVdmPmLaneValues = [this](VdmConfigType vdmConf) {
    return getVdmLaneValuesF16(vdmConf);
  };

  // PAM4 Level0, Level1, Level2 , Level3, MPI
  auto sdL0Map = getVdmPmLaneValues(PAM4_LEVEL0_STANDARD_DEVIATION_LINE);
  auto sdL1Map = getVdmPmLaneValues(PAM4_LEVEL1_STANDARD_DEVIATION_LINE);
  auto sdL2Map = getVdmPmLaneValues(PAM4_LEVEL2_STANDARD_DEVIATION_LINE);
  auto sdL3Map = getVdmPmLaneValues(PAM4_LEVEL3_STANDARD_DEVIATION_LINE);
  auto mpiMap = getVdmPmLaneValues(PAM4_MPI_LINE);
  for (auto& [portName, mediaLanes] : portNameToMediaLanes) {
    for (auto& mediaLane : mediaLanes) {
      vdmStats.mediaPortVdmStats()[portName].lanePam4Level0SD()[mediaLane] =
          sdL0Map[mediaLane];
      vdmStats.mediaPortVdmStats()[portName].lanePam4Level1SD()[mediaLane] =
          sdL1Map[mediaLane];
      vdmStats.mediaPortVdmStats()[portName].lanePam4Level2SD()[mediaLane] =
          sdL2Map[mediaLane];
      vdmStats.mediaPortVdmStats()[portName].lanePam4Level3SD()[mediaLane] =
          sdL3Map[mediaLane];
      vdmStats.mediaPortVdmStats()[portName].lanePam4MPI()[mediaLane] =
          mpiMap[mediaLane];
    }
  }
  return true;
}

/*
 * fillVdmPerfMonitorPam4AlarmData
 *
 * Reads and processes the latched alarm and warning flags for PAM4 MPI
 * values.
 *
 * These flags are stored in VDM page 0x2C, bytes 208-211, with the following
 * bit layout:
 *
 * Byte 208:
 *   - bit 0: High alarm for lane 0
 *   - bit 2: High warning for lane 0
 *   - bit 4: High alarm for lane 1
 *   - bit 6: High warning for lane 1
 *
 * Byte 209:
 *   - bit 0: High alarm for lane 2
 *   - bit 2: High warning for lane 2
 *   - bit 4: High alarm for lane 3
 *   - bit 6: High warning for lane 3
 *
 * Byte 210:
 *   - bit 0: High alarm for lane 4
 *   - bit 2: High warning for lane 4
 *   - bit 4: High alarm for lane 5
 *   - bit 6: High warning for lane 5
 *
 * Byte 211:
 *   - bit 0: High alarm for lane 6
 *   - bit 2: High warning for lane 6
 *   - bit 4: High alarm for lane 7
 *   - bit 6: High warning for lane 7
 *
 * The function reads these flags and stores them in the VdmPerfMonitorStats
 * structure.
 */
bool CmisModule::fillVdmPerfMonitorPam4AlarmData(
    VdmPerfMonitorStats& vdmStats) {
  if (!isVdmSupported(3) || !cacheIsValid()) {
    return false;
  }
  auto vdmConfStatus = getVdmDiagsValLocation(PAM4_MPI_LINE);
  if (!vdmConfStatus.vdmConfImplementedByModule) {
    return false;
  }
  // For Line-side histogram based MPI_metric value, the ThresholdSetID ID
  // should be 34
  uint8_t thresholdSetID = vdmConfStatus.localThresholdSetID + 33;
  if (thresholdSetID != 34) {
    QSFP_LOG(WARN, this)
        << "Skipping reading PAM4_MPI_LINE warning/alarms, unexpected ThresholdSetID: "
        << static_cast<int>(thresholdSetID);
    return false;
  }

  auto& portNameToMediaLanes = getPortNameToMediaLanes();
  // VDM page 0x2C byte 208-211 contain the Alarms and Warnings flags for MPI
  std::array<uint8_t, 4>
      alarmData; // Buffer to read alarm data (4 bytes for 8 lanes)
  try {
    // Read the alarm data using the CmisField we defined
    readCmisField(CmisField::PAM4_MPI_ALARMS, alarmData.data());
  } catch (const std::exception& ex) {
    QSFP_LOG(ERR, this) << "Error reading MPI alarm data: " << ex.what();
    return false;
  }
  // Process the alarm/warning flags for each lane
  for (auto& [portName, mediaLanes] : portNameToMediaLanes) {
    for (auto& mediaLane : mediaLanes) {
      if (mediaLane > 7) {
        // We only support up to 8 lanes (0-7)
        continue;
      }
      // Calculate which byte and bit position to read for this lane
      int byteOffset = mediaLane / 2;
      int bitOffset = (mediaLane % 2) * 4; // 0 or 4 depending on even/odd lane

      // Extract alarm and warning flags directly
      bool alarmHigh = (alarmData[byteOffset] & (1 << bitOffset)) != 0;
      bool warnHigh = (alarmData[byteOffset] & (1 << (bitOffset + 2))) != 0;

      QSFP_LOG(DBG3, this) << "Lane " << mediaLane
                           << " MPI Alarm: " << (alarmHigh ? "true" : "false")
                           << " MPI Warning: " << (warnHigh ? "true" : "false");

      FlagLevels flags;
      flags.alarm()->high() = alarmHigh;
      flags.alarm()->low() = false; // Low alarm not used for MPI
      flags.warn()->high() = warnHigh;
      flags.warn()->low() = false; // Low warning not used for MPI

      vdmStats.mediaPortVdmStats()[portName].lanePam4MPIFlags()[mediaLane] =
          flags;
    }
  }
  return true;
}

/*
 * fillVdmPerfMonitorCoherentVdm
 *
 * Private function to fill in VDM performance monitor stats for coherent
 * 800G ZR modules. These are VDM-unique parameters from pages 20h-23h
 * per OIF C-CMIS-01.3, Section 7.3.1, Table 8:
 *   - Modulator Bias XI/XQ/YI/YQ/XPhase/YPhase (identifiers 128-133)
 *   - CD low granularity (identifier 135)
 *   - SOPMD low granularity (identifier 149)
 *
 * These parameters are only available on coherent (DCO) modules.
 * The function returns false if no coherent VDM parameters are found,
 * which is expected for non-coherent modules.
 */
bool CmisModule::fillVdmPerfMonitorCoherentVdm(VdmPerfMonitorStats& vdmStats) {
  if (!isTunableOptics() || !isVdmSupported() || !cacheIsValid()) {
    return false;
  }

  // Modulator bias parameters use U16 format with LSB = 100/65535
  constexpr double kModulatorBiasLsb = 100.0 / 65535.0;
  // CD low granularity uses S16 format with LSB = 20 (can be negative)
  constexpr double kCdLowGranLsb = 20.0;
  // SOPMD low granularity uses U16 format with LSB = 1
  constexpr double kSopmdLowGranLsb = 1.0;

  bool foundAny = false;
  auto& portNameToMediaLanes = getPortNameToMediaLanes();

  // Read modulator bias parameters (module-level, not per-lane)
  auto biasXI = readU16VdmValue(MODULATOR_BIAS_XI, kModulatorBiasLsb);
  auto biasXQ = readU16VdmValue(MODULATOR_BIAS_XQ, kModulatorBiasLsb);
  auto biasYI = readU16VdmValue(MODULATOR_BIAS_YI, kModulatorBiasLsb);
  auto biasYQ = readU16VdmValue(MODULATOR_BIAS_YQ, kModulatorBiasLsb);
  auto biasXPhase = readU16VdmValue(MODULATOR_BIAS_X_PHASE, kModulatorBiasLsb);
  auto biasYPhase = readU16VdmValue(MODULATOR_BIAS_Y_PHASE, kModulatorBiasLsb);
  auto cdLowGran = readS16VdmValue(CD_LOW_GRANULARITY, kCdLowGranLsb);
  auto sopmdLowGran = readU16VdmValue(SOPMD_LOW_GRANULARITY, kSopmdLowGranLsb);

  // Populate stats for each media port
  for (auto& [portName, mediaLanes] : portNameToMediaLanes) {
    auto& coherentVdm =
        vdmStats.mediaPortVdmStats()[portName].coherentVdmStats().ensure();
    if (biasXI.has_value()) {
      coherentVdm.modulatorBiasXI() = biasXI.value();
      foundAny = true;
    }
    if (biasXQ.has_value()) {
      coherentVdm.modulatorBiasXQ() = biasXQ.value();
      foundAny = true;
    }
    if (biasYI.has_value()) {
      coherentVdm.modulatorBiasYI() = biasYI.value();
      foundAny = true;
    }
    if (biasYQ.has_value()) {
      coherentVdm.modulatorBiasYQ() = biasYQ.value();
      foundAny = true;
    }
    if (biasXPhase.has_value()) {
      coherentVdm.modulatorBiasXPhase() = biasXPhase.value();
      foundAny = true;
    }
    if (biasYPhase.has_value()) {
      coherentVdm.modulatorBiasYPhase() = biasYPhase.value();
      foundAny = true;
    }
    if (cdLowGran.has_value()) {
      coherentVdm.cdLowGranularity() = cdLowGran.value();
      foundAny = true;
    }
    if (sopmdLowGran.has_value()) {
      coherentVdm.sopmdLowGranularity() = sopmdLowGran.value();
      foundAny = true;
    }
  }

  return foundAny;
}

/*
 * fillVdmPerfMonitorFecPm
 *
 * Private function to fill in FEC Performance Monitoring stats from
 * C-CMIS Page 34h (Section 7.4.7, Table 14). This page is a banked page
 * with each bank referring to a single media lane.
 *
 * Page 34h byte layout (per OIF C-CMIS-01.3):
 *   Bytes 128-135: rxBitsPm (U64) - Rx bits during prior PM interval
 *   Bytes 136-143: rxBitsSubIntPm (U64) - Rx bits during any sub-interval
 *   Bytes 144-151: rxCorrBitsPm (U64) - Corrected bits during prior PM
 *   Bytes 152-159: rxMinCorrBitsSubIntPm (U64) - Min corrected bits sub-int
 *   Bytes 160-167: rxMaxCorrBitsSubIntPm (U64) - Max corrected bits sub-int
 *   Bytes 168-171: rxFramesPm (U32) - Rx frames during prior PM
 *   Bytes 172-175: rxFramesSubIntPm (U32) - Rx frames during any sub-interval
 *   Bytes 176-179: rxFramesUncorrErrPm (U32) - Uncorrectable error frames
 *   Bytes 180-183: rxMinFramesUncorrErrSubIntPm (U32) - Min uncorr sub-int
 *   Bytes 184-187: rxMaxFramesUncorrErrSubIntPm (U32) - Max uncorr sub-int
 *
 * Only available on coherent (tunable) optics modules.
 */
bool CmisModule::fillVdmPerfMonitorFecPm(VdmPerfMonitorStats& vdmStats) {
  if (!isTunableOptics() || !cacheIsValid()) {
    return false;
  }

  // Page 34h data is cached in page34_ buffer (128 bytes, offset 128-255)
  const uint8_t* buf = page34_;

  // Lambda to read a U64 value from the buffer (big-endian)
  auto readU64 = [&](int offset) -> int64_t {
    int idx = offset - 128; // Buffer starts at byte 128
    uint64_t val = 0;
    for (int i = 0; i < 8; i++) {
      val = (val << 8) | buf[idx + i];
    }
    return static_cast<int64_t>(val);
  };

  // Lambda to read a U32 value from the buffer (big-endian)
  auto readU32 = [&](int offset) -> int32_t {
    int idx = offset - 128;
    uint32_t val = 0;
    for (int i = 0; i < 4; i++) {
      val = (val << 8) | buf[idx + i];
    }
    return static_cast<int32_t>(val);
  };

  auto& portNameToMediaLanes = getPortNameToMediaLanes();

  // ZR modules have a single media lane, so page 34h has only one bank.
  // The same buffer data applies to all ports — no bank selection needed.
  for (auto& [portName, mediaLanes] : portNameToMediaLanes) {
    FecPm fecPm;
    fecPm.rxBitsPm() = readU64(128);
    fecPm.rxBitsSubIntPm() = readU64(136);
    fecPm.rxCorrBitsPm() = readU64(144);
    fecPm.rxMinCorrBitsSubIntPm() = readU64(152);
    fecPm.rxMaxCorrBitsSubIntPm() = readU64(160);
    fecPm.rxFramesPm() = readU32(168);
    fecPm.rxFramesSubIntPm() = readU32(172);
    fecPm.rxFramesUncorrErrPm() = readU32(176);
    fecPm.rxMinFramesUncorrErrSubIntPm() = readU32(180);
    fecPm.rxMaxFramesUncorrErrSubIntPm() = readU32(184);

    vdmStats.mediaPortVdmStats()[portName].coherentVdmStats().ensure().fecPm() =
        fecPm;
  }
  return true;
}

/*
 * readLinkPmMetricS32
 *
 * Read a Link PM metric with S32 avg/min/max from page 35h (4 bytes each)
 * and S16 current value from VDM pages.
 */
link::LinkPerfMonitorParamEachSideVal CmisModule::readLinkPmMetricS32(
    int startByte,
    double lsb,
    VdmConfigType vdmConf) {
  const uint8_t* buf = page35_;
  int idx = startByte - 128;

  auto readS32 = [buf](int off) -> double {
    int32_t raw = static_cast<int32_t>(
        (static_cast<uint32_t>(buf[off]) << 24) |
        (static_cast<uint32_t>(buf[off + 1]) << 16) |
        (static_cast<uint32_t>(buf[off + 2]) << 8) | buf[off + 3]);
    return static_cast<double>(raw);
  };

  link::LinkPerfMonitorParamEachSideVal val;
  val.avg() = readS32(idx) * lsb;
  val.min() = readS32(idx + 4) * lsb;
  val.max() = readS32(idx + 8) * lsb;
  val.cur() = readS16VdmValue(vdmConf, lsb).value_or(0);
  return val;
}

/*
 * readU16
 *
 * Read an unsigned 16-bit value from a byte buffer at the given offset.
 */
double CmisModule::readU16(const uint8_t* p, int off) {
  return static_cast<double>((static_cast<uint16_t>(p[off]) << 8) | p[off + 1]);
}

/*
 * readS16
 *
 * Read a signed 16-bit value from a byte buffer at the given offset.
 */
double CmisModule::readS16(const uint8_t* p, int off) {
  return static_cast<double>(
      static_cast<int16_t>((static_cast<uint16_t>(p[off]) << 8) | p[off + 1]));
}

/*
 * readLinkPmMetricU16
 *
 * Read a Link PM metric with U16 avg/min/max from page 35h (2 bytes each)
 * and U16 current value from VDM pages.
 */
link::LinkPerfMonitorParamEachSideVal CmisModule::readLinkPmMetricU16(
    int startByte,
    double lsb,
    VdmConfigType vdmConf) {
  const uint8_t* buf = page35_;
  int idx = startByte - 128;

  link::LinkPerfMonitorParamEachSideVal val;
  val.avg() = readU16(buf, idx) * lsb;
  val.min() = readU16(buf, idx + 2) * lsb;
  val.max() = readU16(buf, idx + 4) * lsb;
  val.cur() = readU16VdmValue(vdmConf, lsb).value_or(0);
  return val;
}

/*
 * readLinkPmMetricS16
 *
 * Read a Link PM metric with S16 avg/min/max from page 35h (2 bytes each)
 * and S16 current value from VDM pages.
 */
link::LinkPerfMonitorParamEachSideVal CmisModule::readLinkPmMetricS16(
    int startByte,
    double lsb,
    VdmConfigType vdmConf) {
  const uint8_t* buf = page35_;
  int idx = startByte - 128;

  link::LinkPerfMonitorParamEachSideVal val;
  val.avg() = readS16(buf, idx) * lsb;
  val.min() = readS16(buf, idx + 2) * lsb;
  val.max() = readS16(buf, idx + 4) * lsb;
  val.cur() = readS16VdmValue(vdmConf, lsb).value_or(0);
  return val;
}

/*
 * fillVdmPerfMonitorLinkPm
 *
 * Fill in Link Performance Monitoring stats from C-CMIS Page 35h
 * (Section 7.4.8, Table 15) and VDM pages (20h-23h).
 *
 * Page 35h provides avg/min/max values over the prior PM interval.
 * Current (real-time) values come from VDM pages using identifiers 134-152.
 * Only available on coherent (tunable) optics modules.
 */
bool CmisModule::fillVdmPerfMonitorLinkPm(VdmPerfMonitorStats& vdmStats) {
  if (!isTunableOptics() || !cacheIsValid()) {
    return false;
  }

  auto& portNameToMediaLanes = getPortNameToMediaLanes();

  for (auto& [portName, mediaLanes] : portNameToMediaLanes) {
    LinkPm linkPm;

    linkPm.cd() = readLinkPmMetricS32(128, 1.0, CD_HIGH_GRANULARITY);
    linkPm.dgd() = readLinkPmMetricU16(140, 0.01, DGD);
    linkPm.sopmd() = readLinkPmMetricU16(146, 0.01, SOPMD_HIGH_GRANULARITY);
    linkPm.pdl() = readLinkPmMetricU16(152, 0.1, PDL);
    linkPm.osnr() = readLinkPmMetricU16(158, 0.1, OSNR);
    linkPm.esnr() = readLinkPmMetricU16(164, 0.1, ESNR);
    linkPm.cfo() = readLinkPmMetricS16(170, 1.0, CFO);
    linkPm.evmModem() = readLinkPmMetricU16(176, 100.0 / 65535.0, EVM);
    linkPm.txPower() = readLinkPmMetricS16(182, 0.01, TX_POWER);
    linkPm.rxPower() = readLinkPmMetricS16(188, 0.01, RX_TOTAL_POWER);
    linkPm.rxSigPower() = readLinkPmMetricS16(194, 0.01, RX_SIGNAL_POWER);
    linkPm.sopcr() = readLinkPmMetricS16(200, 1.0, SOP_ROC);
    linkPm.mer() = readLinkPmMetricU16(206, 0.1, MER);
    linkPm.clockRecoveryLoop() =
        readLinkPmMetricS16(212, 100.0 / 32767.0, CLOCK_RECOVERY_LOOP);
    linkPm.snrMargin() = readLinkPmMetricS16(224, 0.1, SNR_MARGIN);
    linkPm.qFactor() = readLinkPmMetricU16(230, 0.1, Q_FACTOR);
    linkPm.qMargin() = readLinkPmMetricS16(236, 0.1, Q_MARGIN);

    vdmStats.mediaPortVdmStats()[portName]
        .coherentVdmStats()
        .ensure()
        .linkPm() = linkPm;
  }
  return true;
}

} // namespace fboss
} // namespace facebook
