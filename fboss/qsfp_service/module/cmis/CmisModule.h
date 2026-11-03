// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include <sys/types.h>
#include "fboss/qsfp_service/module/QsfpModule.h"

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/lib/firmware_storage/FbossFirmware.h"
#include "fboss/qsfp_service/if/gen-cpp2/qsfp_service_config_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"

#include <array>
#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

namespace facebook {
namespace fboss {

enum class CmisField;

enum class CmisPages : int {
  LOWER = -1,
  PAGE00 = 0,
  PAGE01 = 1,
  PAGE02 = 2,
  PAGE04 = 4,
  PAGE10 = 0x10,
  PAGE11 = 0x11,
  PAGE12 = 0x12,
  PAGE13 = 0x13,
  PAGE14 = 0x14,
  PAGE20 = 0x20,
  PAGE21 = 0x21,
  PAGE22 = 0x22,
  PAGE23 = 0x23,
  PAGE24 = 0x24,
  PAGE25 = 0x25,
  PAGE26 = 0x26,
  PAGE27 = 0x27,
  PAGE2C = 0x2C,
  PAGE2F = 0x2F,
  PAGE34 = 0x34,
  PAGE35 = 0x35,
  PAGE38 = 0x38,
  PAGE45 = 0x45,
  PAGEC0 = 0xC0,
};

enum VdmConfigType {
  UNSUPPORTED = 0,
  SNR_MEDIA_IN = 5,
  SNR_HOST_IN = 6,
  PAM4_LTP_MEDIA_IN = 7,
  PRE_FEC_BER_MEDIA_IN_MIN = 9,
  PRE_FEC_BER_HOST_IN_MIN = 10,
  PRE_FEC_BER_MEDIA_IN_MAX = 11,
  PRE_FEC_BER_HOST_IN_MAX = 12,
  PRE_FEC_BER_MEDIA_IN_AVG = 13,
  PRE_FEC_BER_HOST_IN_AVG = 14,
  PRE_FEC_BER_MEDIA_IN_CUR = 15,
  PRE_FEC_BER_HOST_IN_CUR = 16,
  ERR_FRAME_MEDIA_IN_MIN = 17,
  ERR_FRAME_HOST_IN_MIN = 18,
  ERR_FRAME_MEDIA_IN_MAX = 19,
  ERR_FRAME_HOST_IN_MAX = 20,
  ERR_FRAME_MEDIA_IN_AVG = 21,
  ERR_FRAME_HOST_IN_AVG = 22,
  ERR_FRAME_MEDIA_IN_CUR = 23,
  ERR_FRAME_HOST_IN_CUR = 24,
  PAM4_LEVEL0_STANDARD_DEVIATION_LINE = 100,
  PAM4_LEVEL1_STANDARD_DEVIATION_LINE = 101,
  PAM4_LEVEL2_STANDARD_DEVIATION_LINE = 102,
  PAM4_LEVEL3_STANDARD_DEVIATION_LINE = 103,
  PAM4_MPI_LINE = 104,
  FEC_TAIL_MEDIA_IN_MAX = 106,
  FEC_TAIL_MEDIA_IN_CURR = 107,
  FEC_TAIL_HOST_IN_MAX = 108,
  FEC_TAIL_HOST_IN_CURR = 109,
  // Coherent 800G ZR VDM parameters (C-CMIS-01.3, Table 8)
  MODULATOR_BIAS_XI = 128,
  MODULATOR_BIAS_XQ = 129,
  MODULATOR_BIAS_YI = 130,
  MODULATOR_BIAS_YQ = 131,
  MODULATOR_BIAS_X_PHASE = 132,
  MODULATOR_BIAS_Y_PHASE = 133,
  CD_LOW_GRANULARITY = 135,
  CD_HIGH_GRANULARITY = 134,
  DGD = 136,
  SOPMD_HIGH_GRANULARITY = 137,
  PDL = 138,
  OSNR = 139,
  ESNR = 140,
  CFO = 141,
  EVM = 142,
  TX_POWER = 143,
  RX_TOTAL_POWER = 144,
  RX_SIGNAL_POWER = 145,
  SOP_ROC = 146,
  MER = 147,
  CLOCK_RECOVERY_LOOP = 148,
  SOPMD_LOW_GRANULARITY = 149,
  SNR_MARGIN = 150,
  Q_FACTOR = 151,
  Q_MARGIN = 152,
};

enum class DiagnosticFeatureEncoding : uint8_t {
  NONE = 0x0,
  BER = 0x1,
  SNR = 0x6,
  LATCHED_BER = 0x11,
};

class CmisModule : public QsfpModule {
 public:
  explicit CmisModule(
      std::set<std::string> portNames,
      TransceiverImpl* qsfpImpl,
      std::shared_ptr<const TransceiverConfig> cfg,
      bool supportRemediate,
      std::string tcvrName);
  virtual ~CmisModule() override;

  struct ApplicationAdvertisingField {
    uint8_t ApSelCode;
    uint8_t moduleMediaInterface;
    uint8_t moduleHostInterface{};
    int hostLaneCount;
    int mediaLaneCount;
    std::vector<int> hostStartLanes;
    std::vector<int> mediaStartLanes;
  };

  static constexpr int kMaxOsfpNumLanes = 8;
  static constexpr int kHostInterfaceCodeOffset = 0;
  static constexpr int kMediaInterfaceCodeOffset = 1;
  static constexpr int32_t kDefaultFrequencyMhz = 193100000;
  static constexpr uint8_t kInvalidApplication = 0;
  // CMIS requires 10ms to repopulate page 14h after DIAG_SEL writes
  static constexpr int kUsecDiagSelectLatchWait = 10000;
  // Some modules are non-compliant and take 100ms
  static constexpr std::array<std::string_view, 2> kSlowDiagSelectPartNumbers =
      {"QDD-400G-XDR4", "FB-P800G-2XDR4-1"};
  static constexpr int kUsecDiagSelectLatchWaitSlow = 100000;

  using ApplicationAdvertisingFields = std::vector<ApplicationAdvertisingField>;

  using AllLaneConfig = std::array<uint8_t, kMaxOsfpNumLanes>;

  using LengthAndGauge = std::pair<double, uint8_t>;

  using VdmDiagsLocationStatus = struct VdmDiagsLocationStatus_t {
    bool vdmConfImplementedByModule = false;
    CmisPages vdmValPage;
    int vdmValOffset;
    int vdmValLength;
    uint8_t localThresholdSetID = static_cast<uint8_t>(-1);
  };

  /*
   * Return a valid type.
   */
  TransceiverType type() const override {
    return TransceiverType::QSFP;
  }

  /*
   * Return the spec this transceiver follows.
   */
  TransceiverManagementInterface managementInterface() const override {
    return TransceiverManagementInterface::CMIS;
  }

  /*
   * Get the QSFP EEPROM Field
   */
  void getFieldValue(CmisField fieldName, uint8_t* fieldValue) const;

  RawDOMData getRawDOMData() override;

  DOMDataUnion getDOMDataUnion() override;

  /*
   * The size of the pages used by QSFP.  See below for an explanation of
   * how they are laid out.  This needs to be publicly accessible for
   * testing.
   */
  enum : unsigned int {
    // Size of page read from QSFP via I2C
    MAX_QSFP_PAGE_SIZE = 128,
  };

  void configureModule(uint8_t startHostLane) override;

  /*
   * This function veifies the Module eeprom register checksum for various
   * pages.
   */
  bool verifyEepromChecksums() override;

  /*
   * Returns the current state of prbs (enabled/polynomial)
   */
  prbs::InterfacePrbsState getPortPrbsStateLocked(
      std::optional<const std::string> portName,
      phy::Side side) override;

  VdmDiagsLocationStatus getVdmDiagsValLocation(VdmConfigType vdmConf) const;

  bool tcvrPortStateSupported(TransceiverPortState& portState) const override;

  bool isRequestValidMultiportSpeedConfig(
      cfg::PortSpeed speed,
      uint8_t startHostLane,
      uint8_t numLanes);

  // Public since its used for unit testing
  ApplicationAdvertisingFields getModuleCapabilities() {
    return moduleCapabilities_;
  }
  // Public since its used for unit testing
  static uint8_t laneMask(uint8_t startLane, uint8_t numLanes) {
    return ((1 << numLanes) - 1) << startLane;
  }

  // Set the module to low power mode (writes SQUELCH_CONTROL | LOW_PWR_BIT)
  void setModuleLowPowerModeLocked();

  // Release low power mode (clears LOW_PWR_BIT, writes SQUELCH_CONTROL only)
  void releaseModuleLowPowerModeLocked();

  // Check if the module is in READY state
  bool isModuleInReadyState();

  // Poll until the module reaches READY state (up to 60s for tunable optics,
  // else 5s)
  bool moduleReadyStatePoll() override;

  // Read the datapath init max delay time from the module spec (in usec)
  std::optional<uint64_t> getDatapathMaxDelayFromModuleSpec(bool init);

 protected:
  // QSFP+ requires a bottom 128 byte page describing important monitoring
  // information, and then an upper 128 byte page with less frequently
  // referenced information, including vendor identifiers.  There are
  // three other optional pages;  the third provides a bunch of
  // alarm and warning thresholds which we are interested in.
  uint8_t lowerPage_[MAX_QSFP_PAGE_SIZE]{};
  uint8_t page0_[MAX_QSFP_PAGE_SIZE]{};
  uint8_t page01_[MAX_QSFP_PAGE_SIZE]{};
  uint8_t page02_[MAX_QSFP_PAGE_SIZE]{};
  uint8_t page04_[MAX_QSFP_PAGE_SIZE]{};
  // Per-bank page buffer: one 128-byte array per CMIS bank (index = bank).
  using BankedPage = std::vector<std::array<uint8_t, MAX_QSFP_PAGE_SIZE>>;
  // Pages 10h/11h/13h/14h carry per-lane data and are read for every bank on
  // multi-bank (CPO) modules; stored per-bank with bank 0 at index 0. Sized to
  // getMaxNumBanks() at refresh; default 1 bank (zeroed) for
  // single-bank/flatMem modules.
  BankedPage page10_{std::array<uint8_t, MAX_QSFP_PAGE_SIZE>{}};
  BankedPage page11_{std::array<uint8_t, MAX_QSFP_PAGE_SIZE>{}};
  uint8_t page12_[MAX_QSFP_PAGE_SIZE]{};
  BankedPage page13_{std::array<uint8_t, MAX_QSFP_PAGE_SIZE>{}};
  BankedPage page14_{std::array<uint8_t, MAX_QSFP_PAGE_SIZE>{}};
  // VDM config pages 20h-23h are bank-invariant (they describe the data
  // layout), so they stay flat. VDM data pages 24h-27h hold per-lane values and
  // are read per-bank on multi-bank (CPO) modules (bank 0 at index 0).
  uint8_t page20_[MAX_QSFP_PAGE_SIZE]{};
  uint8_t page21_[MAX_QSFP_PAGE_SIZE]{};
  uint8_t page22_[MAX_QSFP_PAGE_SIZE]{};
  uint8_t page23_[MAX_QSFP_PAGE_SIZE]{};
  BankedPage page24_{std::array<uint8_t, MAX_QSFP_PAGE_SIZE>{}};
  BankedPage page25_{std::array<uint8_t, MAX_QSFP_PAGE_SIZE>{}};
  BankedPage page26_{std::array<uint8_t, MAX_QSFP_PAGE_SIZE>{}};
  BankedPage page27_{std::array<uint8_t, MAX_QSFP_PAGE_SIZE>{}};
  // C-CMIS Performance Monitoring pages (coherent optics)
  uint8_t page34_[MAX_QSFP_PAGE_SIZE]{};
  uint8_t page35_[MAX_QSFP_PAGE_SIZE]{};
  // Page 38h - Data Path Host Interface Configuration (Consequent Action)
  uint8_t page38_[MAX_QSFP_PAGE_SIZE]{};
  // Page 45h - Host Lane Provisioning Advertisement
  uint8_t page45_[MAX_QSFP_PAGE_SIZE]{};

  // Some of the pages are static and they need not be read every refresh cycle
  bool staticPagesCached_{false};

  // Cached firmware build number from CDB Get Firmware Info command
  std::optional<uint16_t> cachedFwBuildNumber_;

  // Cached CDB I2C write delay for firmware upgrade, computed from media type
  std::optional<uint64_t> cachedCdbWriteDelayUsec_;

  // Cached maximum number of CMIS banks supported by the module, read from
  // Lower Page 00h byte 70. See cacheMaxNumBanks()/getMaxNumBanks().
  std::optional<uint8_t> maxNumBanks_;

  // Cached <major, minor> CMIS revision the module complies with, read from
  // Lower Page 00h byte 1. See cacheCmisRevision()/getCmisRevision().
  std::pair<uint8_t, uint8_t> cmisRevision_{0, 0};

  /* Maximum number of CMIS banks supported by the module. Returns 1 until
   * cacheMaxNumBanks() has populated the cache. */
  uint8_t getMaxNumBanks() const {
    return maxNumBanks_.value_or(1);
  }

  /* Whether the bank select register (Lower Page 00h byte 126) cached by the
   * last refresh names a bank the module doesn't have. */
  bool hasInvalidBankSelect() const override;

  /* A global host/media lane (0..numLanes-1) lives in bank
   * (lane / kMaxOsfpNumLanes); its position within that bank's banked register
   * is (lane % kMaxOsfpNumLanes). A port's lanes are confined to one bank, so
   * per-port programming selects laneToBank(startHostLane) and uses intra-bank
   * lane offsets. On single-bank modules these collapse to the identity. */
  static uint8_t laneToBank(int globalLane) {
    return globalLane / kMaxOsfpNumLanes;
  }
  static uint8_t laneInBank(int globalLane) {
    return globalLane % kMaxOsfpNumLanes;
  }
  // Convenience for the common case of needing both at once:
  //   auto [bank, intraLane] = bankAndLane(globalLane);
  static std::pair<uint8_t, uint8_t> bankAndLane(int globalLane) {
    return {laneToBank(globalLane), laneInBank(globalLane)};
  }
  // Inverse of laneToBank/laneInBank: the global lane index for an intra-bank
  // lane offset within a bank.
  static int globalLane(uint8_t bank, int laneOffset) {
    return bank * kMaxOsfpNumLanes + laneOffset;
  }

  /* Global-lane accessors for banked per-lane fields. A "global" lane spans all
   * banks (e.g. 0..31 for a 4-bank module); it maps to bank = lane /
   * kMaxOsfpNumLanes and intra-bank lane = lane % kMaxOsfpNumLanes. For
   * single-bank modules these collapse to the existing bank-0 behavior.
   *
   * getLaneValuePtr: pointer to the bytesPerLane bytes for globalLane in a
   * field whose per-lane values are laid out contiguously (e.g.
   * CHANNEL_RX_PWR). getLaneFlagSet: whether the per-lane bit for globalLane is
   * set in a 1-byte flag field (e.g. RX_LOS_FLAG). getLaneNibble: the 4-bit
   * nibble for globalLane in a nibble-packed field (e.g. RX_OUT_PRE_CURSOR). */
  const uint8_t*
  getLaneValuePtr(CmisField field, int globalLane, int bytesPerLane) const;
  bool getLaneFlagSet(CmisField field, int globalLane) const;
  uint8_t getLaneNibble(CmisField field, int globalLane) const;

  // Convenience wrappers over getVdmLaneValues/getVdmLaneValue for the two
  // common per-lane VDM decodings, keeping the decode lambdas in one place:
  // U16 (integer byte + fractional byte / 256) and F16 (CMIS half-precision
  // used for BER / PM values). Values are keyed by global lane across all
  // banks.
  std::map<int, double> getVdmLaneValuesU16(VdmConfigType vdmConf);
  std::map<int, double> getVdmLaneValuesF16(VdmConfigType vdmConf);
  std::optional<double> getVdmLaneValueF16(
      VdmConfigType vdmConf,
      int globalLane);

  /*
   * Structure to hold datapath init/deinit state per port using timers
   * progStartTimer: Time point when datapath programming started.
   * progDoneTimer: Time point when datapath programming finished.
   * elapsedTime: Elapsed time for datapath programming (in milliseconds).
   */
  struct PortTimer {
    std::chrono::steady_clock::time_point progStartTimer;
    std::chrono::steady_clock::time_point progDoneTimer;
    std::chrono::milliseconds elapsedTime{0};
  };

  /*
   * Structure to track datapath initialization and de-initialization state
   * per port.
   *
   * deInitTimers: Timers tracking datapath de-initialization
   *               (start, done, and elapsed time)
   * initTimers: Timers tracking datapath initialization
   *             (start, done, and elapsed time)
   * dpDeinitFailureCounter: Counter tracking the number of times spec violation
   *                         for dp-deinit duration module advertisement.
   * dpInitFailureCounter: Counter tracking the number of times spec violation
   *                       for dp-init duration module advertisement.
   */
  struct DatapathState {
    PortTimer deInitTimers;
    PortTimer initTimers;
    bool dpDeinitDone{false};
    bool dpInitDone{false};
    uint64_t dpDeinitFailureCounter{0};
    uint64_t dpInitFailureCounter{0};
    // Lanes whose activation the in-progress datapath init waits for. Kept so
    // retries check the same lanes the init was started on.
    std::optional<uint8_t> initLaneMask;
  };

  /*
   * Map to track datapath init/deinit state per port ID
   * Key: Port ID (string), Value: DatapathState structure
   */
  std::map<std::string, DatapathState> portDatapathStates_;

  /*
   * This function returns a pointer to the value in the static cached
   * data after checking the length fits. The thread needs to have the lock
   * before calling this function.
   */
  const uint8_t* getQsfpValuePtr(int dataAddress, int offset, int length)
      const override;

  /* Like getQsfpValuePtr but returns the cached bytes for a specific bank of a
   * banked page (pages 10h/11h/13h). bank 0 is index 0 of the same per-bank
   * buffer (equivalent to getQsfpValuePtr). Throws if the page isn't cached
   * per-bank or that bank wasn't read. */
  const uint8_t* getBankedQsfpValuePtr(
      int dataAddress,
      int offset,
      int length,
      uint8_t bank) const;

  /*
   * Perform transceiver customization
   * This must be called with a lock held on qsfpModuleMutex_
   */
  void customizeTransceiverLocked(
      const TransceiverPortState& portState) override;

  /*
   * Returns whether customization is supported at all.
   * Checks if something is plugged in and checks if it is optical (SMF)
   * or an active electrical cable.
   * We do not support customization (for now) on passive copper cables.
   */
  virtual bool customizationSupported() const override {
    return present_ &&
        (getQsfpTransmitterTechnology() == TransmitterTechnology::OPTICAL ||
         isAecModule());
  }

  /*
   * If the current power state is not same as desired one then change it and
   * return true when module is in ready state
   * @param hasTunableOpticsConfig - indicates if tunable optics config is
   *        present. For tunable optics modules without config, an exception
   *        is thrown to prevent high power mode transition.
   */
  virtual bool ensureTransceiverReadyLocked(
      bool hasTunableOpticsConfig) override;

  /*
   * Based on identifier, sets whether the upper memory of the module is flat
   * or paged.
   */
  void setQsfpFlatMem() override;
  /*
   * Set power mode
   * Wedge forces Low Power mode via a pin;  we have to reset this
   * to force High Power mode on LR4s.
   */
  virtual void setPowerOverrideIfSupportedLocked(
      PowerControlState currentState) override;
  /*
   * Program the tunable optics module
   * Program following parameters
   *    1. Frequency
   *    2. Tx Power - TODO
   *
   * Convert the C or L band frequency and grid
   * to Grid channel number. If channel number is provided directly
   * pass the channel number directly
   */
  void programTunableModule(
      const cfg::OpticalChannelConfig& opticalChannelConfig);
  /*
   * Set appropriate application code for PortSpeed, if supported
   * if newAppSelCode is provided, use that directly instead of deriving
   */
  void setApplicationCodeLocked(
      const TransceiverPortState& portState,
      uint8_t newAppSelCode);

  /*
   * Helper function to discover and return the appropriate application
   * capability based on module capabilities and speed requirements. Returns
   * the full ApplicationAdvertisingField if a suitable application is found,
   * or std::nullopt if no matching application is available or if the current
   * config already matches.
   */
  std::optional<ApplicationAdvertisingField> getAppSelCodeForSpeed(
      const std::string& portName,
      cfg::PortSpeed speed,
      uint8_t startHostLane,
      uint8_t numHostLanesForPort);

  /*
   * Helper function to program a given AppSel code to the module.
   * This contains the common logic for resetting datapath, programming,
   * and verifying the application code.
   *
   * If appSelectFunc is provided, it will be used for programming.
   * Otherwise, the default setApplicationSelectCode will be used.
   */
  void programApplicationSelectCode(
      uint8_t appSelCode,
      uint8_t moduleMediaInterfaceCode,
      const TransceiverPortState& state,
      uint8_t numHostLanes,
      std::optional<std::function<void()>> appSelectFunc = std::nullopt,
      std::optional<uint8_t> deinitLaneMask = std::nullopt,
      std::optional<uint8_t> initLaneMask = std::nullopt);

  /*
   * Program the AppSel code for a port, programming all lanes in one datapath
   * cycle when the requested config is not a valid multiport config on its own
   */
  void programPortApplicationSelectCode(
      uint8_t appSelCode,
      uint8_t moduleMediaInterfaceCode,
      const TransceiverPortState& state,
      uint8_t numHostLanes);

  // Stage set 0 AppSel bytes setApplicationSelectCodeAllPorts programs for the
  // port's bank, or nullopt if there is no valid speed combo
  std::optional<AllLaneConfig> getAllPortsStageSet0Config(
      const TransceiverPortState& state,
      uint8_t numHostLanes);
  // {deinit, init} intra-bank lane masks for programming all ports: deinit
  // covers lanes in the current and new configs, init covers lanes the new
  // config puts in an application
  std::pair<uint8_t, uint8_t> getAllPortsResetLaneMasks(
      const TransceiverPortState& state,
      uint8_t numHostLanes);

  /*
   * Helper function to read an interface code (host or media) for a given
   * AppSel code from the EEPROM based on the byte offset.
   * - For media interface code, use byteOffset = kMediaInterfaceCodeOffset (1)
   * - For host interface code, use byteOffset = kHostInterfaceCodeOffset (0)
   */
  uint8_t getInterfaceCodeForAppSel(uint8_t appSelCode, int byteOffset);

  /*
   * Helper function to read the current application select code for a given
   * lane.
   */
  uint8_t getCurrentAppSelCode(uint8_t startHostLane) const;
  /*
   * returns individual sensor values after scaling
   */
  double getQsfpSensor(CmisField field, double (*conversion)(uint16_t value));
  /*
   * returns cable length (negative for "longer than we can represent")
   */
  double getQsfpSMFLength() const;

  double getQsfpOMLength(CmisField field) const;

  /*
   * returns the freeside transceiver technology type
   */
  virtual TransmitterTechnology getQsfpTransmitterTechnology() const override;
  /*
   * Convert FrequencyGrid enum to grid selection byte value for CMIS register
   */
  uint8_t frequencyGridToGridSelection(FrequencyGrid grid) const;
  /*
   * Return the Channel number when frequency and grid is provided
   */
  int16_t getChannelNumFromFrequency(
      int32_t frequencyMhz,
      FrequencyGrid frequencyGrid);
  /*
   * Extract sensor flag levels
   */
  FlagLevels getQsfpSensorFlags(CmisField fieldName, int offset);
  /*
   * This function returns various strings from the QSFP EEPROM
   * caller needs to check if DOM is supported or not
   */
  std::string getQsfpString(CmisField flag) const;

  /*
   * Fills in values for alarm and warning thresholds based on field name
   */
  ThresholdLevels getThresholdValues(
      CmisField field,
      double (*conversion)(uint16_t value));
  /*
   * Retrieves all alarm and warning thresholds
   */
  std::optional<AlarmThreshold> getThresholdInfo() override;
  /*
   * Gather the sensor info for thrift queries
   */
  GlobalSensors getSensorInfo() override;
  /*
   * Gather per-channel information for thrift queries
   */
  bool getSensorsPerChanInfo(std::vector<Channel>& channels) override;
  /*
   * Gather per-media-lane signal information for thrift queries
   */
  bool getSignalsPerMediaLane(std::vector<MediaLaneSignals>& signals) override;
  /*
   * Gather per-host-lane signal information for thrift queries
   */
  bool getSignalsPerHostLane(std::vector<HostLaneSignals>& signals) override;
  /*
   * Gather per-channel flag values from bitfields.
   */
  FlagLevels getChannelFlags(CmisField field, int channel);
  /*
   * Gather the vendor info for thrift queries
   */
  Vendor getVendorInfo() const override;
  /*
   * Gather the cable info for thrift queries
   */
  Cable getCableInfo() override;
  /*
   * Retrieves the values of settings based on field name and bit placement
   * Default mask is a noop
   */
  virtual uint8_t getSettingsValue(CmisField field, uint8_t mask = 0xff) const;
  /*
   * Gather info on what features are enabled and supported
   */
  virtual TransceiverSettings getTransceiverSettingsInfo() override;
  /*
   * Gather supported applications for this module, and store them in
   * moduleCapabilities_
   */
  void getApplicationCapabilities();
  /*
   * Return which rate select capability is being used, if any
   */
  RateSelectState getRateSelectValue();
  /*
   * Return the rate select optimised bit rates for each channel
   */
  RateSelectSetting getRateSelectSettingValue(RateSelectState state);
  /*
   * Return what power control capability is currently enabled
   */
  PowerControlState getPowerControlValue(bool readFromCache) override;
  /*
   * Return SignalFlag which contains Tx/Rx LOS/LOL
   */
  virtual SignalFlags getSignalFlagInfo() override;
  /*
   * Returns the identifier in byte 0
   */
  TransceiverModuleIdentifier getIdentifier() override;
  /*
   * Returns the module status in byte 3
   */
  ModuleStatus getModuleStatus() override;
  /*
   * Fetches the media interface ids per media lane and returns false if it
   * fails
   */
  bool getMediaInterfaceId(
      std::vector<MediaInterfaceId>& mediaInterface) override;
  /*
   * Gets the Media Type encoding (byte 85 in CMIS)
   */
  MediaTypeEncodings getMediaTypeEncoding() const;

  /*
   * Get the curent application set for the lane (i.e. programmed in the
   * transceiver). Based on Interface codes from SFF-8024.
   * For Optical SMF transceivers, the application is the media interface
   * code, so the offset is 1. For Active Cables, the application is the host
   * interface code, so the offset is 0.
   */
  uint8_t getCurrentApplication(uint8_t lane, int offset) const;

  /*
   * Get the SMF Media Interface Code for the lane. uses
   * getCurrentApplication.
   * TODO: Should add a check for translation is to an enum that is
   * supported or defined in thrift.
   */
  SMFMediaInterfaceCode getSmfMediaInterface(uint8_t lane) const {
    return (SMFMediaInterfaceCode)getCurrentApplication(
        lane, kMediaInterfaceCodeOffset);
  }

  /*
   * Get the Active Cable Interface Code for the lane. uses
   * getCurrentApplication.
   * TODO: Should add a check for translation is to an enum that is
   * supported or defined in thrift.
   */
  ActiveCuHostInterfaceCode getActiveCuMediaInterface(uint8_t lane) const {
    return (ActiveCuHostInterfaceCode)getCurrentApplication(
        lane, kHostInterfaceCodeOffset);
  }

  /*
   * Returns the firmware version
   * <Module firmware version, DSP version, Build revision>
   */
  std::array<std::string, 3> getFwRevisions();
  FirmwareStatus getFwStatus();

  /* CMIS revision the module complies with, as <major, minor>. Returns {0, 0}
   * until cacheCmisRevision() has populated the cache. */
  std::pair<uint8_t, uint8_t> getCmisRevision() const {
    return cmisRevision_;
  }

  /*
   * Fetches the firmware build number from CDB Get Firmware Info command.
   * Returns the build number if successful, or std::nullopt on failure.
   */
  std::optional<uint16_t> fetchFwBuildNumberFromCdb();

  /*
   * Gather host side per lane configuration settings and return false when it
   * fails
   */
  bool getHostLaneSettings(std::vector<HostLaneSettings>& laneSettings);
  /*
   * Gather media side per lane configuration settings and return false when
   * it fails
   */
  bool getMediaLaneSettings(std::vector<MediaLaneSettings>& laneSettings);
  /*
   * Update the cached data with the information from the physical QSFP.
   *
   * The 'allPages' parameter determines which pages we refresh. Data
   * on the first page holds most of the fields that actually change,
   * so unless we have reason to believe the transceiver was unplugged
   * there is not much point in refreshing static data on other pages.
   */
  virtual void updateQsfpData(bool allPages = true) override;

  /*
   * Put logic here that should only be run on ports that have been
   * down for a long time. These are actions that are potentially more
   * disruptive, but have worked in the past to recover a transceiver.
   */
  void remediateFlakyTransceiver(
      bool allPortsDown,
      const std::vector<std::string>& ports) override;

  virtual void setDiagsCapability() override;

  /*
   * Populate the Meta custom feature bits (mode mismatch, DSP/laser thermal
   * margin) advertised in Page 01h Byte 191.
   */
  void setCustomFeatureCapability(DiagsCapability& diags);

  /*
   * Populate the Meta custom latched flags (mode mismatch, negative DSP/laser
   * thermal margin) from Lower Memory Byte 67.
   */
  void setCustomLatchedFlags(ModuleStatus& moduleStatus);

  /*
   * Whether the module advertises the Meta mode-mismatch feature, which gates
   * both the Byte 67 latched flag and the per-lane Page 14h registers.
   */
  bool isModeMismatchSupported() const;

  ThermalMargins getThermalMargins() override;

  virtual std::optional<VdmDiagsStats> getVdmDiagsStatsInfo() override;

  virtual std::optional<VdmPerfMonitorStats> getVdmPerfMonitorStats() override;

  virtual VdmPerfMonitorStatsForOds getVdmPerfMonitorStatsForOds(
      VdmPerfMonitorStats& vdmPerfMonStats) override;

  virtual std::map<std::string, CdbDatapathSymErrHistogram>
  getCdbSymbolErrorHistogramLocked() override;

  /*
   * Trigger next VDM stats capture
   */
  void triggerVdmStatsCapture() override;

  bool supportRemediate() override;

  void resetDataPath(const std::string& portName) override;
  // Intra-bank mask of lanes with a non-zero active AppSel code
  uint8_t getActiveAppSelLaneMask(uint8_t bank);

  void resetDatapathProgrammingStateLocked() override;

  /*
   * Returns true if the current module is LPO
   */
  bool isLpoModule() const override;

  /*
   * Return if module is AEC cable.
   */
  bool isAecModule() const override {
    return getMediaTypeEncoding() == MediaTypeEncodings::ACTIVE_CABLES;
  }

  /*
   * returns whether optics frequency is tunable or not
   */
  bool isTunableOptics() const override;
  bool isCBandTunable() const override;
  bool isLBandTunable() const override;

  /*
   * Disable TX and RX squelch on all lanes for tunable optics modules.
   * Squelch is only disabled if the module advertises rxConsActImpl
   * (Page 45h, Byte 129, Bit 1). Programs Rx/Tx Consequent Action
   * (LF insertion) via Page 38h before disabling squelch.
   */
  void disableTxRxSquelchForTunableOptics();

  /*
   * Temporary fix for ZR in 1x800G mode: T289920421
   * Set Protocol Sel to AM Transparent Mode. This is a temporary
   * workaround for ZR modules where the PCS alignment marker
   * mode, when in termination mode, could cause misalignment and the
   * port does not come up. (only PCS errors are seen, no FEC errors).
   * This will be removed from the code once we get a FW fix.
   */
  void setPcsToAmTransparent();

  /*
   * Check if the module advertises Rx Consequent Action support.
   * Reads Page 45h (Host Lane Provisioning Advertisement),
   * Byte 129, Bit 1 (rxConsActImpl).
   */
  bool isRxConsActImplSupported() const override;

  /*
   * Enable Rx Consequent Action (LF insertion) for tunable optics.
   * Writes to Page 38h, Byte 137, Bits 7-4 (rxConsAct): 0001 = insert LF.
   */
  void enableRxLfInsertionForTunableOptics();

  bool isRxConsActHoldOffTmrImplSupported() const override;

  void configureRxConsActHoldOffTimer(int32_t timerMs);

  /*
   * returns the tunable optics laser status and laser frequency
   */
  std::optional<TunableLaserStatus> getTunableLaserStatus() override;
  /*
   * Returns the ApplicationAdvertisingField corresponding to the application
   * or nullopt if it doesn't exist
   */
  std::optional<ApplicationAdvertisingField> getApplicationField(
      uint8_t application,
      uint8_t startHostLane) const;

  // Returns the list of host lanes configured in the same datapath as the
  // provided startHostLane
  std::vector<uint8_t> configuredHostLanes(
      uint8_t startHostLane) const override;

  // Returns the list of media lanes configured in the same datapath as the
  // provided startHostLane
  std::vector<uint8_t> configuredMediaLanes(
      uint8_t startHostLane) const override;

  /*
   * Set the Transceiver Tx channel enable/disable
   */
  virtual bool setTransceiverTxLocked(
      const std::string& portName,
      phy::Side side,
      std::optional<uint8_t> userChannelMask,
      bool enable) override;

  virtual bool setTransceiverTxImplLocked(
      const std::set<uint8_t>& tcvrLanes,
      phy::Side side,
      std::optional<uint8_t> userChannelMask,
      bool enable) override;

  /*
   * Set the Transceiver loopback system side
   */
  virtual void setTransceiverLoopbackLocked(
      const std::string& portName,
      phy::Side side,
      bool setLoopback,
      phy::LoopbackMode mode) override;

  /* How long this module needs to repopulate page 14h after DIAG_SEL changes,
   * keyed off the part number. */
  int getDiagSelLatchWaitUsec() const;

  /* Read-modify-write the bits of a one-byte CmisField selected by mask: those
   * bits take their new state from value, the rest are preserved.
   * Value is expected to be already positioned within the byte, not shifted in
   * here, so only its masked bits are consulted. Returns the byte written.
   *
   * The write is unconditional -- some registers act on the write itself, so
   * skipping it when the byte is unchanged would drop the side effect.
   *
   * Throws if the field is wider than one byte, which would otherwise overrun
   * the single-byte read buffer. */
  uint8_t readModifyWriteCmisField(
      CmisField field,
      uint8_t mask,
      uint8_t value,
      bool skipBankAndPageChange = false,
      std::optional<uint8_t> bank = std::nullopt);

 private:
  // no copy or assignment
  CmisModule(CmisModule const&) = delete;
  CmisModule& operator=(CmisModule const&) = delete;
  CmisModule(CmisModule&&) = delete;
  CmisModule& operator=(CmisModule&&) = delete;

  // VDM data location of each VDM config types
  std::map<VdmConfigType, VdmDiagsLocationStatus> vdmConfigDataLocations_;

  /* Helper functions to read/write a CmisField. They extract the page number,
   * offset and length information from the CmisField and then make the
   * corresponding qsfpImpl->readTransceiver/writeTransceiver calls. Callers
   * should avoid direct qsfpImpl->read/writeTransceiver calls and instead do
   * register IO via these helpers.
   *
   * Before the access, the helper selects the page and bank via
   * selectPageAndBank: it writes the bank-select register (byte 126) before
   * the page-select register (byte 127), since a banked page's contents depend
   * on the active bank. This is skipped for flatMem modules (no paging) and
   * when skipBankAndPageChange is set (batch operations where bank/page is
   * already selected).
   *
   * bank: optional bank to select, only valid for banked pages (see
   * isBankedPage) -- supplying one for a non-banked page throws. The
   * bank-select register is only written for multi-bank modules
   * (getMaxNumBanks() > 1); on a single-bank module bank 0 is the only bank, so
   * the register is left untouched. Defaults to std::nullopt, which also leaves
   * it untouched. */
  void selectPageAndBank(int dataPage, std::optional<uint8_t> bank);
  void readCmisField(
      CmisField field,
      uint8_t* data,
      bool skipBankAndPageChange = false,
      std::optional<uint8_t> bank = std::nullopt);
  void writeCmisField(
      CmisField field,
      uint8_t* data,
      bool skipBankAndPageChange = false,
      std::optional<uint8_t> bank = std::nullopt);

  /* Read the maximum number of CMIS banks supported by the module from Lower
   * Page 00h byte 70 and cache it in maxNumBanks_. The register holds the bank
   * count directly (e.g. 4 for a 32-lane module); legacy/non-CPO modules that
   * report 0 fall back to a single bank. Expects the lower page to be cached
   * (i.e. call after the lower page read in updateQsfpData). */
  void cacheMaxNumBanks();

  /* Read the CMIS revision the module complies with from Lower Page 00h byte 1
   * and cache it in cmisRevision_. The upper nibble is the major number and the
   * lower nibble the minor number (e.g. 0x50 -> 5.0). Expects the lower page to
   * be cached (i.e. call after the lower page read in updateQsfpData). */
  void cacheCmisRevision();

  /* Read a banked page for every bank into its per-bank buffer (dest), sized to
   * getMaxNumBanks(). On a single-bank module this is exactly
   * readCmisField(field, dest[0]) (no bank-select write). On a multi-bank (CPO)
   * module it reads each bank with an explicit bank select (the bank-select
   * register is sticky) into dest[bank], with bank 0 at index 0. */
  void readBankedPage(CmisField field, BankedPage& dest);

  /* Read page 14h (SNR diagnostics) into dest for every bank. Page 14h is a
   * multiplexed diagnostic page selected by DIAG_SEL, which itself lives on the
   * banked page, so DIAG_SEL=SNR is written under each bank's selection before
   * reading. Reads bank 0 last so the module is left selected on bank 0. */
  void readSnrDiagPageLocked(BankedPage& dest);

  /*
  1. Check if existing diagSel matches the desired one
  2. If not, write the value and wait for page 14h to repopulate
   Note that latchWaitUsec overrides getDiagSelLatchWaitUsec().
  */
  void setDiagSel(
      DiagnosticFeatureEncoding diagSel,
      std::optional<uint8_t> bank = std::nullopt,
      std::optional<int> latchWaitUsec = std::nullopt);

  void getFieldValueLocked(CmisField fieldName, uint8_t* fieldValue) const;
  /*
   * Helpers to parse DOM data for DAC cables. These incorporate some
   * extra fields that FB has vendors put in the 'Vendor specific'
   * byte range of the SFF spec.
   */
  double getQsfpDACLength() const override;

  /*
   * Set the optics Rx euqlizer pre/post/main values
   */
  void setModuleRxEqualizerLocked(
      RxEqualizerSettings rxEqualizer,
      uint8_t startHostLane,
      uint8_t numLanes);

  /*
   * We found that some module did not enable Rx output squelch by default,
   * which introduced some difficulty to bring link back up when flapped.
   * This function is to ensure that Rx output squelch is always enabled.
   */
  void ensureRxOutputSquelchEnabled(
      const std::vector<HostLaneSettings>& hostLaneSettings) override;

  /*
   * Check if the module has accepted the lane configuration specified by
   * ApSel or other settings like RxEqualizer setting. In case of config
   * rejection the function returns false
   */
  bool checkLaneConfigError(uint8_t startHostLane, uint8_t hostLaneCount);

  /*
   * This function veifies the Module eeprom register checksum for a given
   * page
   */
  bool verifyEepromChecksum(CmisPages pageId);

  /*
   * Reads the CMIS standard L-Module State Changed latched register
   * (Will be 1 on first read after a state change, and will read 0 on
   * subsequent reads)
   */
  virtual bool getModuleStateChanged();

  /*
   * Application advertising fields.
   */
  ApplicationAdvertisingFields moduleCapabilities_;

  /*
   * Gets the module media interface. This is the intended media interface
   * application for this module. The module may be able to run in a different
   * application (with lesser bandwidth). For example if a 200G-FR4 module is
   * configured for 100G-CWDM4 application, then getModuleMediaInterface will
   * return 200G-FR4
   */
  MediaInterfaceCode getModuleMediaInterface() const override;

  // Returns the CDB I2C write delay for firmware upgrade based on media type.
  // Legacy optics need the 5ms inter-write delay; new media types default to 0.
  uint64_t getFwUpgradeCdbWriteDelayUsec() const;

  uint64_t getExpectedDatapathDelayUsec(bool /*init*/);
  uint64_t maxDatapathStatePolls(bool /*init*/);

  /*
   * Program the datapath for the specified port. When isInit is true, releases
   * lanes from DEACTIVATED state to ACTIVATED state. When isInit is false, sets
   * lanes to DEACTIVATED state. This function:
   * - Writes to DATA_PATH_DEINIT register (set bits for deinit, clear for init)
   * - Polls the datapath state machine until lanes reach target state
   * - Tracks timing and failure counters
   * - Returns true if operation succeeds, false on timeout or failure
   *
   * @param portName The name of the port being programmed
   * @param hostLaneMask Intra-bank bitmask of host lanes to program
   * @param isInit If true, initialize (activate); if false, deinitialize
   * (deactivate)
   * @param bank Bank that the port's lanes live in (0 for single-bank modules)
   * @return true if datapath operation completed successfully, false otherwise
   */
  bool dataPathProgram(
      const std::string& portName,
      uint8_t hostLaneMask,
      bool isInit,
      uint8_t bank = 0);

  /*
   * Check if the datapath for the specified lanes has been updated to one of
   * the desired states. laneMask carries intra-bank lane bits for the given
   * bank.
   */
  bool isDatapathUpdated(
      uint8_t laneMask,
      const std::vector<CmisLaneState>& states,
      uint8_t bank = 0);

  void resetDataPathWithFunc(
      const std::string& portName,
      std::optional<std::function<void()>> afterDataPathDeinitFunc =
          std::nullopt,
      uint8_t hostLaneMask = 0xFF,
      uint8_t bank = 0,
      std::optional<uint8_t> initLaneMask = std::nullopt);

  /*
   * Helper function to reset data path for tunable optics (ZR modules)
   * using dataPathProgram for deinit/init operations
   */
  void resetDataPathForTunableOptics(
      const std::string& portName,
      std::optional<std::function<void()>> afterDataPathDeinitFunc,
      uint8_t hostLaneMask,
      uint8_t bank = 0,
      std::optional<uint8_t> initLaneMask = std::nullopt);

  /*
   * Set the PRBS Generator and Checker on a module for the desired side (Line
   * or System side)
   */
  bool setPortPrbsLocked(
      const std::string& portName,
      phy::Side side,
      const prbs::InterfacePrbsState& prbs) override;

  /*
   * Get the PRBS stats for a module
   */
  phy::PrbsStats getPortPrbsStatsSideLocked(
      phy::Side side,
      bool checkerEnabled,
      const phy::PrbsStats& lastStats) override;

  void updateVdmCacheLocked();

  /*
   * Non-blocking VDM ForOds capture handshake, driven once per refresh from
   * updateCachedTransceiverInfoLocked(). Spreads the freeze across two refresh
   * cycles so the slow FreezeDone wait never blocks the refresh thread:
   *   - cycle N: on the StatsPublisher trigger, write FreezeRequest (no wait).
   *   - cycle N+1: one non-blocking FreezeDone read, then read the frozen pages
   *     and unfreeze (reading anyway if FreezeDone is still unset).
   * Returns true on the cycle a frozen snapshot was read (caller refreshes the
   * ForOds stats that cycle).
   */
  bool driveVdmCaptureLocked() override;
  void requestVdmFreezeLocked();
  bool finishVdmFreezeReadLocked();
  // True only when the module is in the READY state (initialized, out of low
  // power, DSP powered on) so it can process a VDM freeze. Freezing while the
  // module is still initializing / in low power hangs the latch on some modules
  // and corrupts VDM intervals, so we gate the freeze request on this.
  bool isReadyForVdmFreezeLocked();
  // Reset the async capture state and clear any in-flight freeze (e.g. on a
  // reprogram/reset while a capture was in progress).
  void resetVdmCaptureStateLocked();
  // Low-level VDM freeze helpers shared by the blocking and async paths.
  void writeVdmFreezeRequestLocked(bool freeze);
  void readVdmFrozenPagesLocked();
  bool isVdmFreezeDoneLocked();

  void updateCmisStateChanged(
      ModuleStatus& moduleStatus,
      std::optional<ModuleStatus> curModuleStatus = std::nullopt) override;

  // Returns the currently configured mediaInterfaceCode on a host lane
  uint8_t currentConfiguredMediaInterfaceCode(uint8_t hostLane) const;

  CmisLaneState getDatapathLaneStateLocked(
      uint8_t lane,
      bool readFromCache = true);

  bool upgradeFirmwareLockedImpl(FbossFirmware* fbossFw) const override;

  void readFromCacheOrHw(
      CmisField field,
      uint8_t* data,
      bool forcedReadFromHw = false);

  void updateVdmDiagsValLocation();

  double f16ToDouble(uint8_t byte0, uint8_t byte1);
  std::pair<std::optional<const uint8_t*>, int> getVdmDataValPtr(
      VdmConfigType vdmConf,
      uint8_t bank = 0);

  /* Per-lane VDM accessors that span all banks. The VDM config (and thus the
   * data location) is bank-invariant; each bank's data page holds that bank's
   * lanes. getVdmLaneValues returns a map keyed by global lane (bank *
   * kMaxOsfpNumLanes + intra-bank lane); getVdmLaneValue returns a single
   * global lane's decoded value. `decode` turns the lane's 2 bytes into a
   * double. */
  std::map<int, double> getVdmLaneValues(
      VdmConfigType vdmConf,
      const std::function<double(const std::array<uint8_t, 2>&)>& decode);
  std::optional<double> getVdmLaneValue(
      VdmConfigType vdmConf,
      int globalLane,
      const std::function<double(const std::array<uint8_t, 2>&)>& decode);

  // VDM value reading helper methods - read 2 bytes from VDM data
  std::optional<double> readU16VdmValue(VdmConfigType vdmConf, double lsb);
  std::optional<double> readS16VdmValue(VdmConfigType vdmConf, double lsb);

  // Read U16/S16 from raw byte data at given offset
  static double readU16(const uint8_t* p, int off);
  static double readS16(const uint8_t* p, int off);

  bool isMultiPortOptics() {
    return getIdentifier() == TransceiverModuleIdentifier::OSFP;
  }

  // Utility functions for power state management
  PowerControlState getCurrentPowerControlState();

  // Check if module should be kept in low power mode for AppSel programming.
  bool programAppSelInLowPowerMode() const;

  // Apply Rx-SNR correction and return the corrected value
  double applyRxSnrCorrection(uint16_t rawValue, double snrValue) const;

  // Check if CDB Get Firmware Info (0x0100) should be retried on status 0x40.
  bool shouldRetryCdbFwInfo() const;

  // Private functions to extract and fill in VDM performance monitoring stats
  bool fillVdmPerfMonitorSnr(VdmPerfMonitorStats& vdmStats);
  bool fillVdmPerfMonitorBer(VdmPerfMonitorStats& vdmStats);
  bool fillVdmPerfMonitorFecErr(VdmPerfMonitorStats& vdmStats);
  bool fillVdmPerfMonitorFecTail(VdmPerfMonitorStats& vdmStats);
  bool fillVdmPerfMonitorLtp(VdmPerfMonitorStats& vdmStats);
  bool fillVdmPerfMonitorPam4Data(VdmPerfMonitorStats& vdmStats);
  bool fillVdmPerfMonitorPam4AlarmData(VdmPerfMonitorStats& vdmStats);
  bool fillVdmPerfMonitorCoherentVdm(VdmPerfMonitorStats& vdmStats);
  bool fillVdmPerfMonitorFecPm(VdmPerfMonitorStats& vdmStats);
  bool fillVdmPerfMonitorLinkPm(VdmPerfMonitorStats& vdmStats);

  // Link PM helper methods: read avg/min/max from page 35h + current from VDM
  link::LinkPerfMonitorParamEachSideVal
  readLinkPmMetricS32(int startByte, double lsb, VdmConfigType vdmConf);
  link::LinkPerfMonitorParamEachSideVal
  readLinkPmMetricU16(int startByte, double lsb, VdmConfigType vdmConf);
  link::LinkPerfMonitorParamEachSideVal
  readLinkPmMetricS16(int startByte, double lsb, VdmConfigType vdmConf);

  void applyHostControlledInputEquilizerTx(
      uint8_t lane,
      uint8_t value,
      uint8_t bank = 0);

  uint8_t setExplicitControl(
      const TransceiverPortState& state,
      const uint8_t laneMask);

  void setApplicationSelectCode(
      uint8_t apSelCode,
      uint8_t mediaInterfaceCode,
      const TransceiverPortState& state,
      uint8_t numHostLanes,
      uint8_t hostLaneMask);
  void setApplicationSelectCodeAllPorts(
      const TransceiverPortState& state,
      uint8_t numHostLanes);

  // Sets the sampling percentage for
  // FEC errors if supported by transceiver.
  void setMaxFecSamplingLocked();

  bool supportRemediate_;
  std::map<int32_t, SymErrHistogramBin> getCdbSymbolErrorHistogramLocked(
      uint8_t datapathId,
      bool mediaSide);

  void clearTransceiverPrbsStats(const std::string& portName, phy::Side side)
      override;

  std::time_t vdmIntervalStartTime_{0};

  // Async VDM ForOds capture state (see driveVdmCaptureLocked). IDLE until the
  // StatsPublisher trigger arms a capture; FREEZE_REQUESTED for the one refresh
  // between writing FreezeRequest and reading the frozen snapshot.
  enum class VdmCaptureState { IDLE, FREEZE_REQUESTED };
  VdmCaptureState vdmCaptureState_{VdmCaptureState::IDLE};
};

} // namespace fboss
} // namespace facebook
