#
# Copyright 2004-present Facebook. All Rights Reserved.
#
# This is the source of truth for QsfpServiceConfig. A hand-maintained mirror
# lives at configerator source/neteng/fboss/coop/inputs/qsfp_service_config.thrift
# and is what produces the configs deserialized here. There is no automated sync,
# so any change to this struct needs a matching configerator diff with identical
# field IDs.
namespace cpp2 facebook.fboss.cfg
namespace go neteng.fboss.qsfp_service_config
namespace py neteng.fboss.qsfp_service_config
namespace py3 neteng.fboss
namespace py.asyncio neteng.fboss.asyncio.qsfp_service_config

include "fboss/qsfp_service/if/transceiver_validation.thrift"
include "fboss/qsfp_service/if/transceiver.thrift"
include "fboss/agent/switch_config.thrift"
include "thrift/annotation/thrift.thrift"

@thrift.AllowLegacyMissingUris
package;

struct QsfpSdkVersion {
  // The version associated with the desired Sdk
  2: string version;
}

enum TransceiverPartNumber {
  UNKNOWN = 1,
}

// The matching logic will treat the absent of an optional field as match all.
struct TransceiverConfigOverrideFactor {
  1: optional TransceiverPartNumber transceiverPartNumber;
  2: optional transceiver.SMFMediaInterfaceCode applicationCode;
  3: optional transceiver.ActiveCuHostInterfaceCode hostApplicationCode;
}

struct Sff8636Overrides {
  1: optional i16 rxPreemphasis;
  2: optional i16 rxAmplitude;
  3: optional i16 txEqualization;
}

struct CmisOverrides {
  1: optional transceiver.RxEqualizerSettings rxEqualizerSettings;
}

union TransceiverOverrides {
  1: Sff8636Overrides sff;
  2: CmisOverrides cmis;
}

struct TransceiverConfigOverride {
  1: TransceiverConfigOverrideFactor factor;
  2: TransceiverOverrides config;
}

struct CabledTestPair {
  1: string aPortName;
  2: string zPortName;
  3: switch_config.PortProfileID profileID;
}

struct QsfpTestConfig {
  1: list<CabledTestPair> cabledPortPairs;
  2: TransceiverFirmware firmwareForUpgradeTest;
  // Maximum qsfp_service RSS (bytes) allowed at the end of each qsfp_hw_test.
  // The check is skipped when unset.
  3: optional i64 qsfpServiceMemLimitBytes;
}

enum FirmwareType {
  APPLICATION = 1,
  DSP = 2,
}

struct FirmwareVersion {
  1: FirmwareType fwType;
  2: string version;
}

struct Firmware {
  1: list<FirmwareVersion> versions;
}

struct TransceiverFirmware {
  // Transceiver Part Number to Firmware version(s) map
  1: map<string, Firmware> versionsMap;

  // Transceiver Part Number to its firmware handle in fboss_firmware.yaml map
  2: map<string, string> fwHandleMap;
}

struct TransceiverI2cLogging {
  1: bool writeLog;
  2: bool readLog;
  3: bool disableOnFail;
  4: i32 bufferSlots;
}

// Center optical channel frequency can be specified in either of the following ways
// 1. Frequency in MHz
// 2. Channel number
union CenterFrequencyConfig {
  1: i32 frequencyMhz;
  2: i32 channelNumber;
}

// Frequency config can be specified in either of the following ways
// 1. Frequency grid + Frequency(MhZ), internally software will calculate channel number based on frequency and grid
// And program the module with channel number and grid.
// 2. Frequency grid + Channel number, directly program the module with channel number and grid.
struct FrequencyConfig {
  1: transceiver.FrequencyGrid frequencyGrid;
  2: CenterFrequencyConfig centerFrequencyConfig;
}

struct OpticalChannelConfig {
  // Frequency config
  1: FrequencyConfig frequencyConfig;
  // Transmitter power in 0.01 dBm
  2: i32 txPower0P01Dbm;
  // Application Select code published by the META ZR spec
  3: i32 appSelCode;
  // Rx Consequent Action Hold-off Timer in ms (must be a multiple of 10).
  // Max 655350ms. Delays squelch/LF while inserting PCS Idles. Defaults to 0
  // (disabled). Only programmed on modules that implement the register; a
  // module that doesn't advertise support is left untouched (no error).
  4: i32 rxConsActHoldOffTimerMs = 0;
}

enum TransceiverDspType {
  MARVELL_SPICA_5NM = 1,
  MARVELL_SPICA_PLUS = 2,
  BRCM_GEMERA = 3,
  BRCM_PORTOFINO = 4,
  MARVELL_SPICA_GEN2 = 5,
  // LPO (linear pluggable optics) modules have no DSP at all. Kept as an
  // explicit type rather than omitting the module, so an LPO part resolves
  // to a known zero delay instead of falling through as "unknown".
  NO_DSP_LPO = 6,
}

struct TransceiverDspDelayInfo {
  1: i32 txDelayNs;
  2: i32 rxDelayNs;
  3: i32 txErrorNs;
  4: i32 rxErrorNs;
}

struct TransceiverDelayConstants {
  // DSP type to delay values
  1: map<TransceiverDspType, TransceiverDspDelayInfo> dspDelayMap;
  // Transceiver MPN to DSP type
  2: map<string, TransceiverDspType> mpnToDspMap;
}

// Only used by the deprecated QsfpServiceConfig field 11.
struct TransceiverProperties {
  1: optional TransceiverDelayConstants delayConstants;
}

struct QsfpServiceConfig {
  // This is used to override the default command line arguments we
  // pass to qsfp service.
  1: map<string, string> defaultCommandLineArgs = {};

  // This has a list of overrides of settings on the optic together with the
  // factor that specify the condition to apply.
  2: list<TransceiverConfigOverride> transceiverConfigOverrides = [];

  // Sdk versions for QSFP service
  3: optional QsfpSdkVersion sdk_version;

  4: optional QsfpTestConfig qsfpTestConfig;

  5: optional TransceiverFirmware transceiverFirmwareVersions;

  6: optional TransceiverI2cLogging transceiverI2cLogging;

  7: optional list<
    transceiver_validation.VendorConfig
  > transceiverValidationConfig;

  // Port name to optical channel config map
  8: optional map<string, OpticalChannelConfig> tunableOpticsConfig;

  // PHY/Retimer configuration content for platforms with external PHY (eg: Ladakh)
  // This is the raw content of the phy_config_file stored as a single-line
  // string with \n for newlines. At runtime, this content is written to a
  // temp file and passed to SAI initialization via the phy_config_file flag.
  // NOTE: This field is MANDATORY for Ladakh platform - QSFP service will
  // fail to start if this field is missing
  9: optional string phyConfig;

  // Map of thrift API name to Rate limit in thrift API queries per second.
  // Methods not present in the map are not rate limited.
  10: map<string, double> thriftApiToRateLimitInQps = {};

  // [DEPRECATED] fboss2 reads the transceiver catalog from configerator directly.
  11: optional TransceiverProperties transceiverProperties_DEPRECATED;

  // Platform mapping placeholder vendors (used in
  // PlatformPortConfigOverrideFactor.vendor) and the real transceivers each
  // stands for.
  12: optional list<
    transceiver.TransceiverVendorOverride
  > transceiverVendorOverrides;
}
