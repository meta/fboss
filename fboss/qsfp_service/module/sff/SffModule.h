// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/qsfp_service/module/QsfpModule.h"

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"

namespace facebook {
namespace fboss {

enum class SffPages : int {
  LOWER = -1,
  PAGE0 = 0,
  PAGE3 = 3,
  PAGE7 = 7,
  PAGE128 = 128,
};

enum class SffField;

class SffModule : public QsfpModule {
 public:
  explicit SffModule(
      std::set<std::string> portNames,
      TransceiverImpl* qsfpImpl,
      std::shared_ptr<const TransceiverConfig> cfg,
      std::string tcvrName);
  virtual ~SffModule() override;

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
    return TransceiverManagementInterface::SFF;
  }

  /*
   * Get the QSFP EEPROM Field
   */
  void getFieldValue(SffField fieldName, uint8_t* fieldValue);

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
    // Number of channels per module
    CHANNEL_COUNT = 4,
  };

  using LengthAndGauge = std::pair<double, uint8_t>;

  /*
   * This function veifies the Module eeprom register checksum for various
   * pages.
   */
  bool verifyEepromChecksums() override;

  bool supportRemediate() override;

  bool tcvrPortStateSupported(TransceiverPortState& portState) const override;

 protected:
  // QSFP+ requires a bottom 128 byte page describing important monitoring
  // information, and then an upper 128 byte page with less frequently
  // referenced information, including vendor identifiers.  There are
  // three other optional pages;  the third provides a bunch of
  // alarm and warning thresholds which we are interested in.
  // This needs to be initialized to 0 because in software simulated
  // environment this should not contain any arbitrary values
  uint8_t lowerPage_[MAX_QSFP_PAGE_SIZE] = {0};
  uint8_t page0_[MAX_QSFP_PAGE_SIZE] = {0};
  uint8_t page3_[MAX_QSFP_PAGE_SIZE] = {0};

  void customizeTransceiverLocked(
      const TransceiverPortState& portState) override;

  /*
   * If the current power state is not same as desired one then change it and
   * return true when module is in ready state
   * @param hasTunableOpticsConfig - ignored for SFF modules as they don't
   *        support tunable optics
   */
  virtual bool ensureTransceiverReadyLocked(
      bool hasTunableOpticsConfig) override;

  /*
   * This function returns a pointer to the value in the static cached
   * data after checking the length fits. The thread needs to have the lock
   * before calling this function.
   */
  const uint8_t* getQsfpValuePtr(int dataAddress, int offset, int length)
      const override;
  /*
   * Based on identifier, sets whether the upper memory of the module is flat or
   * paged.
   */
  void setQsfpFlatMem() override;
  /*
   * Set power mode
   * Wedge forces Low Power mode via a pin;  we have to reset this
   * to force High Power mode on LR4s.
   */
  void setPowerOverrideIfSupportedLocked(
      PowerControlState currentState) override;
  /*
   * Enable or disable CDR
   * Which action to take is determined by the port speed
   */
  void setCdrIfSupported(
      cfg::PortSpeed speed,
      FeatureState currentStateTx,
      FeatureState currentStateRx) override;
  /*
   * Set appropriate rate select value for PortSpeed, if supported
   */
  void setRateSelectIfSupported(
      cfg::PortSpeed speed,
      RateSelectState currentState,
      RateSelectSetting currentSetting) override;
  /*
   * returns individual sensor values after scaling
   */
  double getQsfpSensor(SffField field, double (*conversion)(uint16_t value));
  /*
   * returns cable length (negative for "longer than we can represent")
   */
  double getQsfpCableLength(SffField field) const;

  /*
   * returns the freeside transceiver technology type
   */
  TransmitterTechnology getQsfpTransmitterTechnology() const override;
  /*
   * Extract sensor flag levels
   */
  FlagLevels getQsfpSensorFlags(SffField fieldName);
  /*
   * This function returns various strings from the QSFP EEPROM
   * caller needs to check if DOM is supported or not
   */
  std::string getQsfpString(SffField flag) const;

  /*
   * Fills in values for alarm and warning thresholds based on field name
   */
  ThresholdLevels getThresholdValues(
      SffField field,
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
   * Gather the vendor info for thrift queries
   */
  virtual Vendor getVendorInfo() const override;
  /*
   * Gather the cable info for thrift queries
   */
  Cable getCableInfo() override;
  /*
   * Retrieves the values of settings based on field name and bit placement
   * Default mask is a noop
   */
  virtual uint8_t getSettingsValue(SffField field, uint8_t mask = 0xff) const;
  /*
   * Gather info on what features are enabled and supported
   */
  TransceiverSettings getTransceiverSettingsInfo() override;
  /*
   * Fetches the media interface ids per media lane and returns false if it
   * fails
   */
  bool getMediaInterfaceId(
      std::vector<MediaInterfaceId>& mediaInterface) override;
  /*
   * Return which rate select capability is being used, if any
   */
  virtual RateSelectState getRateSelectValue();
  /*
   * Return the rate select optimised bit rates for each channel
   */
  virtual RateSelectSetting getRateSelectSettingValue(RateSelectState state);
  /*
   * Return what power control capability is currently enabled
   */
  PowerControlState getPowerControlValue(bool readFromCache) override;
  /*
   * Return SignalFlag which contains Tx/Rx LOS/LOL
   */
  virtual SignalFlags getSignalFlagInfo() override;
  /*
   * Return the extended specification compliance code of the module.
   * This is the field of Byte 192 on page00 and following table 4-4
   * of SFF-8024.
   */
  std::optional<ExtendedSpecComplianceCode>
  getExtendedSpecificationComplianceCode() const override;
  /*
   * Return information in the identifier byte 0
   */
  TransceiverModuleIdentifier getIdentifier() override;
  /*
   * Returns the status in bytes 1 and 2 in the lower page
   */
  virtual ModuleStatus getModuleStatus() override;
  /*
   * Gather host side per lane configuration settings and return false when it
   * fails
   */
  bool getHostLaneSettings(std::vector<HostLaneSettings>& laneSettings);
  /*
   * Gather media side per lane configuration settings and return false when it
   * fails
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
  void updateQsfpData(bool allPages = true) override;

  void resetDataPath(const std::string& /* portName */) override {
    // no-op
  }

  void clearTransceiverPrbsStats(
      const std::string& /* portName */,
      phy::Side side) override;

  std::vector<uint8_t> configuredHostLanes(
      uint8_t hostStartLane) const override;

  std::vector<uint8_t> configuredMediaLanes(
      uint8_t hostStartLane) const override;

  MediaInterfaceCode getModuleMediaInterface() const override;

  /*
   * Set the Transceiver Tx channel endbale/disable
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
   * Set the Transceiver loopback system/line side
   */
  virtual void setTransceiverLoopbackLocked(
      const std::string& portName,
      phy::Side side,
      bool setLoopback,
      phy::LoopbackMode mode) override;

 private:
  // no copy or assignment
  SffModule(SffModule const&) = delete;
  SffModule& operator=(SffModule const&) = delete;

  /* Helper function to read/write a SFFField. The function will extract the
   * page number, offset and length information from the SffField and then make
   * the corresponding qsfpImpl->readTransceiver and qsfpImpl->writeTransceiver
   * calls. The user should avoid making direct calls to
   * qsfpImpl->read/writeTransceiver and instead do register IO using
   * readSffField/writeSffField helper functions. The helper function will also
   * change the page when it's supported by the transceiver and when not
   * specifically asked to skip page change (for batch operations). */
  void readSffField(SffField field, uint8_t* data, bool skipPageChange = false);
  void
  writeSffField(SffField field, uint8_t* data, bool skipPageChange = false);
  void
  readSffFr1Field(SffField field, uint8_t* data, bool skipPageChange = false);
  void
  writeSffFr1Field(SffField field, uint8_t* data, bool skipPageChange = false);

  /* readField and writeField are not intended to be used directly in the
   * application code. These just help the readSffField/writeSffField to make
   * the appropriate read/writeTransceiver calls. */
  void readField(
      SffField field,
      int dataPage,
      int dataOffset,
      int dataLength,
      uint8_t* data,
      bool skipPageChange);
  void writeField(
      SffField field,
      int dataPage,
      int dataOffset,
      int dataLength,
      uint8_t* data,
      bool skipPageChange);

  enum : unsigned int {
    EEPROM_DEFAULT = 255,
    MAX_GAUGE = 30,
    DECIMAL_BASE = 10,
    HEX_BASE = 16,
  };

  /*
   * Helpers to parse DOM data for DAC cables. These incorporate some
   * extra fields that FB has vendors put in the 'Vendor specific'
   * byte range of the SFF spec.
   */
  double getQsfpDACLength() const override;
  int getQsfpDACGauge() const;
  /*
   * Provides the option to override the length/gauge values read from
   * the DOM for certain transceivers. This is useful when vendors
   * input incorrect data and the accuracy of these fields is
   * important for proper tuning.
   */
  const std::optional<LengthAndGauge> getDACCableOverride() const;

  /*
   * Sets the diags capability. Called by MSM when module enters discovered
   * state
   */
  virtual void setDiagsCapability() override;
  /*
   * Provides the option to override the diags capability for certain
   * transceivers
   */
  const std::optional<DiagsCapability> getDiagsCapabilityOverride();
  /*
   * Provides the option to override the prbs state for certain transceivers
   */
  const std::optional<prbs::InterfacePrbsState> getPortPrbsStateOverrideLocked(
      phy::Side side);
  /*
   * 100G-FR1 modules have a proprietary method to get the prbs state
   */
  const prbs::InterfacePrbsState getFr1PortPrbsStateOverrideLocked(
      phy::Side side);

  /*
   * Provides the option to override the implementation of setPortPrbs for
   * certain transceivers
   */
  const std::optional<bool> setPortPrbsOverrideLocked(
      phy::Side /* side */,
      const prbs::InterfacePrbsState& /* prbs */);
  /*
   * 100G-FR1 modules have a proprietary method to set the prbs state
   */
  bool setFr1PortPrbsOverrideLocked(
      phy::Side /* side */,
      const prbs::InterfacePrbsState& /* prbs */);

  /*
   * Provides the option to override the implementation of getPortPrbsStats for
   * certain transceivers
   */
  const std::optional<phy::PrbsStats> getPortPrbsStatsOverrideLocked(
      phy::Side /* side */);
  /*
   * 100G-FR1 modules have a proprietary method to get prbs stats
   */
  const std::optional<phy::PrbsStats> getFr1PortPrbsStatsOverrideLocked(
      phy::Side /* side */);

  /*
   * Put logic here that should only be run on ports that have been
   * down for a long time. These are actions that are potentially more
   * disruptive, but have worked in the past to recover a transceiver.
   */
  void remediateFlakyTransceiver(
      bool allPortsDown,
      const std::vector<std::string>& ports) override;

  // make sure that tx_disable bits are clear
  virtual void ensureTxEnabled() override;

  // set to low power and then back to original setting. This has
  // effect of restarting the transceiver state machine
  virtual void resetLowPowerMode() override;

  // Some of the transceiver has set their channel control settings
  // different from the others which causes low signal quality when working
  // with some xphy. overwrite the value to default as a remediation.
  void overwriteChannelControlSettings();

  /*
   * This function veifies the Module eeprom register checksum for a given page
   */
  bool verifyEepromChecksum(SffPages pageId);

  /*
   * Returns the current state of prbs (enabled/polynomial)
   */
  prbs::InterfacePrbsState getPortPrbsStateLocked(
      std::optional<const std::string> portName,
      phy::Side side) override;

  /*
   * Set the PRBS Generator and Checker on a module for the desired side (Line
   * or System side).
   * This function expects the caller to hold the qsfp module level lock
   */
  bool setPortPrbsLocked(
      const std::string& /* portName */,
      phy::Side /* side */,
      const prbs::InterfacePrbsState& /* prbs */) override;

  /*
   * Allows the option to override the implementation of getting total bit count
   */
  const std::optional<long long> getPrbsTotalBitCountOverrideLocked(
      phy::Side side,
      uint8_t lane);
  /*
   * Allows the option to override the implementation of getting total bit error
   * count
   */
  const std::optional<long long> getPrbsBitErrorCountOverrideLocked(
      phy::Side side,
      uint8_t lane);
  /*
   * Allows the option to override the implementation of getting prbs lock
   * status
   */
  const std::optional<int> getPrbsLockStatusOverrideLocked(phy::Side side);
  /*
   * 100G-FR1 modules have a proprietary method to read prbs lock status
   */
  const std::optional<int> getFr1PrbsLockStatusOverrideLocked(phy::Side side);
  /*
   * Returns the total bit count
   * This function expects the caller to hold the qsfp module level lock
   */
  long long getPrbsTotalBitCountLocked(phy::Side side, uint8_t lane);
  /*
   * Returns the total PRBS bit error count
   * This function expects the caller to hold the qsfp module level lock
   */
  long long getPrbsBitErrorCountLocked(phy::Side side, uint8_t lane);
  /*
   * Returns the prbs lock status of lanes on the given side
   * This function expects the caller to hold the qsfp module level lock
   */
  int getPrbsLockStatusLocked(phy::Side side);

  /*
   * Get the PRBS stats for a module
   */
  phy::PrbsStats getPortPrbsStatsSideLocked(
      phy::Side side,
      bool checkerEnabled,
      const phy::PrbsStats& lastStats) override;

  /*
   * Helper struct to hold the total prbs bit count and total bit error count
   */
  struct PrbsBitCount {
    std::vector<long long> totalBitCount;
    std::vector<long long> bitErrorCount;
  };

  /*
   * These snapshots hold the reference point from where the BER calculation is
   * done. The current BER is calculated by doing a difference of current bit
   * count values with the one in the snapshot. These need to be synchronized
   * because they will be read in the refresh thread to figure out the BER and
   * written by the thrift thread when user wants to clear the prbs stats
   */
  folly::Synchronized<PrbsBitCount> systemPrbsSnapshot_;
  folly::Synchronized<PrbsBitCount> linePrbsSnapshot_;

  cfg::PortSpeed currentConfiguredSpeed_{cfg::PortSpeed::DEFAULT};
};

} // namespace fboss
} // namespace facebook
