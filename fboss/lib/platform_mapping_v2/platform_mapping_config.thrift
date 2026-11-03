namespace py neteng.fboss.platform_mapping_config
namespace py3 neteng.fboss
namespace py.asyncio neteng.fboss.asyncio.platform_mapping_config
namespace cpp2 facebook.fboss.platform_mapping_config
namespace go neteng.fboss.platform_mapping_config

include "fboss/agent/switch_config.thrift"
include "fboss/lib/phy/phy.thrift"
include "fboss/qsfp_service/if/transceiver.thrift"
include "thrift/annotation/thrift.thrift"

@thrift.AllowLegacyMissingUris
package;

// One of these is what is expected in the A/Z_CHIP_TYPE fields in the CSVs
enum ChipType {
  NPU = 0,
  TRANSCEIVER = 1,
  BACKPLANE = 2,
  XPHY = 3,
  OPTICAL_ENGINE = 4,
  LASER_SOURCE = 5,
}

// One of these is what is expected in the A/Z_CORE_TYPE fields in the CSVs
enum CoreType {
  // NPUs
  TH5_NIF = 0, // TH5
  J3_NIF = 1, // J3 NIF
  J3_FE = 2, // J3 Fabric
  R3_FE = 3, // Ramon3
  R_FE = 4, // Ramon
  J3_RCY = 5, // J3 Recycle Port
  G200 = 6, // Yuba/Kodiak3
  J3_EVT = 7, // J3 Eventor Port
  CHENAB_NIF = 8,
  TH6_NIF = 9, // TH6
  Q4D_NIF = 10, // Q4D
  J4SIM_NIF = 11, // J4Sim
  J4_NIF = 12, // J4 NIF
  J4_FE = 13, // J4 Fabric
  P200 = 14, // m5120

  // Transceivers
  OSFP = 100,
  QSFP28 = 101,
  QSFPDD = 102,
  BANKED_CMIS_INTEGRATED = 103,

  // Backplane Connectors
  EXAMAX = 200,

  // XPHY cores
  AGERA3_SYSTEM = 300,
  AGERA3_LINE = 301,

  // Optical Engine
  INTEGRATED_OE = 400,

  // External Laser SFP
  ELSFP = 500,
}

// Stores the A/Z_CORE_LANE, A/Z_TX_PHYSICAL_LANE, A/Z_RX_PHYSICAL_LANE, A/Z_TX_POLARITY_SWAP, A/Z_RX_POLARITY_SWAP fields from the static mapping CSV
struct Lane {
  1: i32 logical_id;
  // Optional because not applicable to lanes of ports such as recycle port.
  2: optional i32 tx_physical_lane;
  3: optional i32 rx_physical_lane;
  4: optional bool tx_polarity_swap;
  5: optional bool rx_polarity_swap;
}

// Stores the values from A/Z_SLOT_ID, A/Z_CHIP_ID, A/Z_CHIP_TYPE, A/Z_CORE_ID, A/Z_CORE_TYPE, columns in the CSVs
struct Chip {
  1: i32 slot_id;
  2: i32 chip_id;
  3: ChipType chip_type;
  4: i32 core_id;
  5: CoreType core_type;
}

// Stores the information parsed from Port Profile Mapping CSV
struct Port {
  1: i32 global_port_id;
  2: i32 logical_port_id;
  3: string port_name;
  4: list<switch_config.PortProfileID> supported_profiles;
  5: optional i32 attached_coreid;
  6: optional i32 attached_core_portid;
  7: optional i32 virtual_device_id;
  8: switch_config.PortType port_type;
  9: switch_config.Scope scope;
  10: optional i32 parent_port_id;
  // Optional explicit controlling port. If set, this value is used directly
  // instead of computing based on pin overlap logic.
  11: optional i32 controlling_port;
}

struct ChipSetting {
  1: ChipType chip_type;
  2: phy.InterfaceType chip_interface_type;
  3: optional phy.FecMode chip_fec;
}

// Stores the information parsed from Profile Settings CSV
struct SpeedSetting {
  1: switch_config.PortSpeed speed;
  2: ChipSetting a_chip_settings;
  3: ChipSetting z_chip_settings;
  4: i32 num_lanes;
  5: phy.IpModulation modulation;
  6: phy.FecMode fec;
  7: transceiver.TransmitterTechnology media_type;
}

struct TransceiverOverrideSetting {
  1: transceiver.Vendor vendor;
}

// Represents all the factors in the SI Settings CSV
struct SiSettingFactor {
  1: optional switch_config.PortSpeed lane_speed;
  2: optional transceiver.TransmitterTechnology media_type;
  3: optional float cable_length;
  4: optional TransceiverOverrideSetting tcvr_override_setting;
}

struct SiSettingPinConnection {
  1: Chip chip;
  2: i32 logical_lane_id;
}

// Represents a single row from the SI Settings CSV
struct SiSettingRow {
  1: SiSettingPinConnection pin_connection;
  2: SiSettingFactor factor;
  3: phy.TxSettings tx_setting;
  4: phy.RxSettings rx_setting;
  5: optional map<string, i32> custom_tx_collection;
  6: optional map<string, i32> custom_rx_collection;
  7: optional i32 driver_peaking;
}

struct ConnectionEnd {
  1: Chip chip;
  2: Lane lane;
}

struct ConnectionPair {
  1: ConnectionEnd a;
  // Some connections may represent internal ports and so may not have a 'z' end
  2: optional ConnectionEnd z;
}

struct SiFactorAndSetting {
  1: phy.TxSettings tx_setting;
  2: phy.RxSettings rx_setting;
  3: optional SiSettingFactor factor;
  4: optional map<string, i32> custom_tx_collection;
  5: optional map<string, i32> custom_rx_collection;
  6: optional map<i32, i32> driver_peaking;
}

struct TxRxLaneInfo {
  1: list<i32> tx_lane_info;
  2: list<i32> rx_lane_info;
}

struct IntegratedTransceiverConnection {
  1: ConnectionEnd transceiver;
  2: ConnectionEnd opticalEngine;
  3: ConnectionEnd laserSource;
}

struct StaticMapping {
  1: map<i32, TxRxLaneInfo> phy_lane_map;
  2: map<i32, TxRxLaneInfo> polarity_swap_map;
  3: list<ConnectionPair> az_connections;
  // Per-NPU view of fields 1/2, which are keyed by core_id alone and so
  // collapse both NPUs of a multi-NPU platform into one key space.
  4: optional map<i32, map<i32, TxRxLaneInfo>> phy_lane_map_by_chip;
  5: optional map<i32, map<i32, TxRxLaneInfo>> polarity_swap_map_by_chip;
}
