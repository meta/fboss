#
# Copyright 2004-present Facebook. All Rights Reserved.
#
namespace py neteng.fboss.platform_config
namespace py3 neteng.fboss.platform_config
namespace py.asyncio neteng.fboss.asyncio.platform_config
namespace cpp2 facebook.fboss.cfg
namespace go neteng.fboss.platform_config

include "fboss/agent/hw/bcm/bcm_config.thrift"
include "fboss/agent/hw/sai/config/asic_config.thrift"
include "fboss/agent/hw/config/asic_config_v2.thrift"
include "fboss/lib/phy/phy.thrift"
include "fboss/agent/switch_config.thrift"
include "fboss/qsfp_service/if/transceiver.thrift"
include "thrift/annotation/thrift.thrift"
include "thrift/annotation/hack.thrift"

@hack.NamePrefix{prefix = "fboss_platform_config_"}
@hack.LegacyOmitPrefixInNameString
@thrift.AllowLegacyMissingUris
package;

enum PlatformAttributes {
  CONNECTION_HANDLE = 1,
  MAC = 2,
}

enum PlatformMappingProfile {
  DEFAULT = 0,
  INFERENCE = 1,
}

union ChipConfig {
  1: bcm_config.BcmConfig bcm;
  2: asic_config.AsicConfig asic;
  3: asic_config_v2.AsicConfig asicConfig;
}

struct PlatformConfig {
  1: ChipConfig chip;
  3: optional map<PlatformAttributes, string> platformSettings;
  4: map<i16, i64> switchIndexToSwitchId;
  5: map<i16, ChipConfig> switchIndexToChipConfigs;
  6: map<i16, map<PlatformAttributes, string>> switchIndexToPlatformSettings;
  7: optional map<i32, PortAssignment> portIdToPortAssignment;
}

// Separates deployment-specific port IDs from the static, name-keyed hardware
// topology in PlatformMapping.rawPlatformPorts. The two parts reconstruct the
// conventional ID-keyed PlatformMapping.ports map at runtime.
struct PortAssignment {
  1: string portName;
  2: switch_config.PortType portType;
  3: optional i32 attachedCorePortIndex;
  4: switch_config.Scope scope;
}

// Standalone carrier for PlatformConfig.portIdToPortAssignment, used to publish
// port assignments as their own artifact. PlatformConfig cannot serve this role:
// Configerator treats its 'chip' union as required, so a PlatformConfig holding
// only assignments fails validation.
struct PortIdToPortAssignmentConfig {
  1: map<i32, PortAssignment> portIdToPortAssignment;
}

struct PlatformPortEntry {
  1: PlatformPortMapping mapping;
  2: map<switch_config.PortProfileID, PlatformPortConfig> supportedProfiles;
}

struct PlatformPortMapping {
  1: i32 id;
  2: string name;
  3: i32 controllingPort;
  4: list<phy.PinConnection> pins;
  5: switch_config.PortType portType = switch_config.PortType.INTERFACE_PORT;
  6: optional i32 attachedCoreId;
  7: optional i32 attachedCorePortIndex;
  8: optional i32 virtualDeviceId;
  9: switch_config.Scope scope = switch_config.Scope.LOCAL;
  // Name-based form of controllingPort used by rawPlatformPorts.
  10: optional string controllingPortName;
}

struct PlatformPortConfig {
  1: optional list<i32> subsumedPorts;
  2: phy.PortPinConfig pins;
  // Name-based form of subsumedPorts used by rawPlatformPorts.
  3: optional list<string> subsumedPortNames;
}

// Currently we have 'PlatformPortConfig' in PlatformPortEntry to define the
// subsumedPorts and PortPinConfig(mainly tx settings) based on the specific
// port and specific PortProfileID. This PlatformPortConfig is usually unique.
// However, we also come across the following cases:
// 1) In som platform, this PlatformPortConfig is the same for multiple ports
// even regardless of the PortProfileID. For example, in Wedge400, for all the
// downlink ports, we'll use the same TX settings no matter of what speed we use
// 2) In case like Hardware engineer wants to play with the tx_settings for
// specific port and override the default settings from PlatformPortEntry
// To support the above cases, we decided to introduce this
// `PlatformPortConfigOverride`, which is kinda like a lookup table. So if
// list<PlatformPortConfigOverride> portConfigOverrides is defined in
// `PlatformMapping` for the specific platform, we will first check whether
// a port matches to any onverride.factor. If so, we'll directly to use
// override.pins to get the tx settings; otherwise, we will fall back to use
// the default tx_settings from `PlatformPortEntry`

// This struct will define an override factor.
// When deciding whether it's a match:
// 1) All the fields here are using "&&", which means if any field is set,
// we need to match every field.
// 2) While the elements in one field are using "||"
// For example:
// ports:[1, 2], profiles:[PROFILE_100G_4_NRZ_RS528_COPPER], means:
// port 1 with PROFILE_100G_4_NRZ_RS528_COPPER
// **OR**
// port 2 with PROFILE_100G_4_NRZ_RS528_COPPER
// is a match
struct PlatformPortConfigOverrideFactor {
  1: optional list<i32> ports;
  2: optional list<switch_config.PortProfileID> profiles;
  3: optional list<double> cableLengths;
  // Name-based form of ports used by rawPlatformPorts.
  4: optional list<string> portNames;
  5: optional transceiver.TransceiverManagementInterface transceiverManagementInterface;
  6: optional list<phy.DataPlanePhyChip> chips;
  7: optional transceiver.MediaInterfaceCode mediaInterfaceCode;
  8: optional transceiver.Vendor vendor;
  // Runtime only, filled from the plugged transceiver's
  // TcvrState.vendorOverrides. Not meant to be set in platform mapping
  // overrides. When set, it is used to resolve the placeholder in `vendor`.
  9: optional list<transceiver.TransceiverVendorOverride> vendorOverrides;
}

// Resolves the placeholder vendor carried by a PlatformPortConfigOverrideFactor
// to the optics it stands for. Patterns are RE2 and must match the module
// EEPROM vendor name and part number in full.
struct TransceiverVendorPattern {
  1: string vendorName;
  2: string partNumber;
}

struct TransceiverVendorOverrideEntry {
  1: transceiver.Vendor placeholder;
  2: list<TransceiverVendorPattern> patterns;
}

struct TransceiverVendorOverrideMap {
  1: list<TransceiverVendorOverrideEntry> entries;
}

struct PlatformPortConfigOverride {
  1: PlatformPortConfigOverrideFactor factor;
  2: optional phy.PortPinConfig pins;
  3: optional phy.PortProfileConfig portProfileConfig;
  4: optional map<i32, i32> driverPeaking;
}

/*
  We introduce PlatformPortConfigFactor as the set of
  all possible factors that can contribute to profile config selection and
  introduce a list of PlatformPortProfileConfigEntry
  As an example, we can have a mixed pim platform with the following
  PlatformPortProfileConfigEntrys:
  [
    {
      factor: {
        profileID: P1,
        pimIDs: [1, 2, 3]
      },
      profile: conf1
    },
    {
      factor: {
        profileID: P1,
        pimIDs: [4, 5, 6]
      },
      profile: conf2
    }
  ]
  In the above example, a query (profileID=P1, pimID=2) will equate to conf1,
  while a query (profileID=P1, pimID=6) will equate to conf2, thereby supporting
  different profile configs, udner the same profileID P1, depending on pimID
*/
struct PlatformPortConfigFactor {
  1: switch_config.PortProfileID profileID;
  2: optional set<i32> pimIDs;
}

struct PlatformPortProfileConfigEntry {
  1: PlatformPortConfigFactor factor;
  2: phy.PortProfileConfig profile;
}

// TODO: Will deprecate the optional fields in PlatformConfig and start using
// this new struct in agent code
struct PlatformMapping {
  1: map<i32, PlatformPortEntry> ports;
  3: list<phy.DataPlanePhyChip> chips;
  4: optional map<PlatformAttributes, string> platformSettings;
  5: optional list<PlatformPortConfigOverride> portConfigOverrides;
  7: list<PlatformPortProfileConfigEntry> platformSupportedProfiles;
  // Static hardware topology keyed and cross-referenced by stable port names.
  // Numeric port IDs are supplied separately by portIdToPortAssignment.
  8: optional map<string, PlatformPortEntry> rawPlatformPorts;
}
