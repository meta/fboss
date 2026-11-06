// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

include "fboss/qsfp_service/if/qsfp_service_config.thrift"

package "facebook.com/fboss/qsfp_service/test/hal_test"

namespace cpp2 facebook.fboss

struct HalTestTransceiverConfig {
  1: qsfp_service_config.Firmware firmware;
}

struct HalTestTransceiverEntry {
  1: i32 id;
  2: string name;
  // Optional path overrides (used only when no platform is specified)
  3: optional string i2cDevicePath;
  4: optional string presentPath;
  5: optional string resetPath;
  6: HalTestTransceiverConfig startupConfig;
  // Used for firmware upgrade A->B and B->A test, where A is previousFirmware
  // and B is startupConfig's firmware. If not specified, defaults to
  // startupConfig's firmware.
  7: optional qsfp_service_config.Firmware previousFirmware;
}

struct HalTestConfig {
  1: list<HalTestTransceiverEntry> transceivers;
  // Transceiver Part Number to its firmware handle in fboss_firmware.yaml map,
  // same as qsfp_service_config.TransceiverFirmware.fwHandleMap
  2: map<string, string> fwHandleMap;
}
