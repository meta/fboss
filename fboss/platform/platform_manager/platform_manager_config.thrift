// Copyright (c) Meta Platforms, Inc. and affiliates.

namespace cpp2 facebook.fboss.platform.platform_manager
namespace hack NetengFbossPlatformManager
namespace py3 fboss.platform.platform_manager

include "fboss/platform/platform_manager/platform_manager_presence.thrift"
include "thrift/annotation/thrift.thrift"

@thrift.AllowLegacyMissingUris
package;

//            +-+-+-+ +-+-+-+ +-+-+-+-+-+-+ +-+-+-+-+-+-+-+-+-+-+
//            |I|2|C| |B|u|s| |N|a|m|i|n|g| |C|o|n|v|e|n|t|i|o|n|
//            +-+-+-+ +-+-+-+ +-+-+-+-+-+-+ +-+-+-+-+-+-+-+-+-+-+
//
// I2C bus names for a PmUnit are assigned from the PmUnit perspective.  From
// the PmUnit's perspective the origin of the bus can be external (coming
// directly from the slot), or can be a mux within a PmUnit.
//
// If the I2C Adapter name is known, then that should be used as bus name.  If
// not, use the below logic.
//
// If the source of the bus is the slot where the PmUnit is plugged in, then
// the bus is named INCOMING@<incoming_index>.  In the below example, PmUnit A
// has two incoming buses falling into this category.  Similarly PmUnit B has
// three incoming buses named as INCOMING@<incoming_index>
//
// If the source of the bus is a mux within a PmUnit, then the bus is assigned
// the name <MUX_NAME>@<channel_number>.  The MUX_NAME, which is represented as
// `I2cDeviceConfig::pmUnitScopedName` should be unique for each mux within the
// PmUnit. In the example, in PmUnit A, the INCOMING@1 bus gets muxed into
// muxA@0, muxA@1 and muxA@2.  So, the outgoing buses out of the PmUnit Slot
// from PmUnit A are muxA@0, INCOMING@0 and muxA@2.
//
// If the source of the bus is a FPGA within a PmUnit, then the bus is assigned
// the name <PmUnit_SCOPED_NAME> of the I2C Adapter within the FPGA. In the
// example below, fpga1_I2C_2 is the adapter name of the I2C bus coming out of
// the fpga.
//
// Note, the three incoming buses of PmUnit B are assigned the names
// INCOMING@0, INCOMING@1 and INCOMING@2.  These names are independent of how
// the buses originated from PmUnit A.
//                                             PmUnit
//            ┌────────────────────┐           Boundary      ┌────────────────────┐
//            │      PmUnit A ┌────┤            │            │      PmUnit B      │
//            │  ┌────┬─────┐ │Slot│                         │   ┌────┬─────┐     │
//  INCOMING@0│  │ 12 │     │ │    │muxA@0      │  INCOMING@0│   │ 12 │     │┌────┤
// ───────────┼┬▶├────┘     │┌┼────┼─────────────────────────┼┬─▶├────┘     ││Slot│
//            ││ │ sensor1  │││    │            │            ││  │ sensor1  ││    │
//            ││ └──────────┘││    │                         ││  └──────────┘│    │
//            ││             ││    │            │            ││              │    │INCOMING@0
//            ││             ││    │INCOMING@0     INCOMING@1│└──────────────┼────┼─────────▶
//            │└─────────────┼┼────┼────────────┼────────────┼──▶            │    │
//            │              ││    │                         │               │    │muxB@0
//            │ ┌────────┐   ││    │            │            │ ┌────────┐  ┌─┼────┼─────────▶
//  INCOMING@1│ │  muxA  ├───┘│    │                         │ │  muxB  ├──┘ │    │
// ───────────┼▶├────┐   ├─▶  │    │muxA@2      │  INCOMING@2│ ├────┐   ├─▶  │    │muxB@2
//            │ │ 54 │   ├────┼────┼─────────────────────────┼▶│ 54 │   ├────┼────┼─────────▶
//            │ └────┴───┘    └────┤            │            │ └────┴───┘    │    │
//            │         ┌────────┐ │                         │               │    │fpga1_I2C_2
//            │         │  muxC  │ │            │            │           ┌───┼────┼─────────▶
//            │         ├────┐   │ │                         │           │   │    │
//            │         │ 12 │   │ │                         │           │   │    │
//            │         └─▲──┴───┘ │                         │           │   │    │
//            │           │        │                         │           │   └────┤
//            │     SMBus │        │                         │           │        │
//            │    Adapter│        │                         │           │        │
//            │           │        │                         │           │        │
//            │ ┌─────────┴──────┐ │                         │ ┌─────────┴──────┐ │
//            │ │      CPU       │ │                         │ │     fpga1      │ │
//            │ └────────────────┘ │                         │ └────────────────┘ │
//            └────────────────────┘                         └────────────────────┘

// ============================================================================

//                             +-+-+-+-+-+-+-+-+
//                             |S|l|o|t|P|a|t|h|
//                             +-+-+-+-+-+-+-+-+
//
// SlotPaths are constructs used to reference slots in the platform. The
// virtual root slot is a forward slash (/).  The SlotPaths are constructed in
// the sequence of the slot names in which they are plugged in.  The separator
// between slot names is a forward slash (/).  For example, in the below
// platform,
// - Root PmUnit is plugged in SlotPath /
// - First XYZ PmUnit is plugged in SlotPath /XYZ_SLOT@0
// - Second XYZ PmUnit is plugged in SlotPath /XYZ_SLOT@1
// - ABC1 PmUnit is plugged in SlotPath /ABC_SLOT@0
// - ABC2 PmUnit is plugged in SlotPath /ABC_SLOT@1
// - DEF PmUnit is plugged in SlotPath /ABC_SLOT@1/DEF_SLOT@0
//
// ┌─────────────────┐   ┌──────────────────────────┐   ┌─────────────────┐
// │   ABC1 PmUnit   │   │       Root PmUnit        │   │   XYZ PmUnit    │
// │   ┌───────┐     │   ├──────────┐    ┌──────────┤   │   ┌───────┐     │
// │   │ cpld1 │     ├ ─ ┤ABC_SLOT@0│    │XYZ_SLOT@0├ ─ ┤   │sensor1│     │
// │   └───────┘     │   ├──────────┘    └──────────┤   │   └───────┘     │
// └─────────────────┘   │                          │   └─────────────────┘
//                       │                          │
// ┌─────────────────┐   │                          │   ┌─────────────────┐
// │   ABC2 PmUnit   │   ├──────────┐    ┌──────────┤   │   XYZ PmUnit    │
// │┌─────┐   ┌─────┐├ ─ ┤ABC_SLOT@1│    │XYZ_SLOT@1├ ─ ┤   ┌───────┐     │
// ││cpld1│   │cpld2││   ├──────────┘    └──────────┤   │   │sensor2│     │
// │└─────┘   └─────┘│   │                          │   │   └───────┘     │
// │   ┌──────────┐  │   │     ┌───────────────┐    │   └─────────────────┘
// │   │DEF_SLOT@0│  │   │     │     fpga1     │    │
// └───┴────┬─────┴──┘   │     │  ┌──────────┐ │    │
//                       │     │  │gpiochip0 │ │    │
// ┌────────┴────────┐   │     └──┴──────────┴─┘    │
// │   DEF PmUnit    │   │                          │
// │   ┌───────┐     │   └──────────────────────────┘
// │   │sensor3│     │
// │   └───────┘     │
// └─────────────────┘
//
// ============================================================================

//                            +-+-+-+-+-+-+-+-+-+-+
//                            |D|e|v|i|c|e|P|a|t|h|
//                            +-+-+-+-+-+-+-+-+-+-+
//
// DevicePaths are constructs used to refer to devices in the platform. To
// represent a device in a PmUnit, the path should contain the SlotPath where
// the PmUnit is plugged in, followed by the device name.  The device itself is
// represented within square brackets (e.g., [DeviceName]). The device should
// be the leaf (last token), of the path. I2C buses are also considered as
// devices
//
// The devices in the above example are represented as follows
// - /[fpga1]
// - /[gpiochip0]
// - /XYZ_SLOT@0/[sensor1]
// - /XYZ_SLOT@0/[INCOMING@0]
// - /XYZ_SLOT@1/[sensor1]
// - /XYZ_SLOT@1/[INCOMING@0]
// - /ABC_SLOT@0/[cpld1]
// - /ABC_SLOT@0/[INCOMING@0]
// - /ABC_SLOT@1/[cpld1]
// - /ABC_SLOT@1/[INCOMING@0]
// - /ABC_SLOT@1/[cpld2]
// - /ABC_SLOT@1/DEF_SLOT@0/[sensor3]
// - /ABC_SLOT@1/DEF_SLOT@0/[INCOMING@0]

// ============================================================================

// Defines desired value of the given I2C register(s).
//
// `regOffset`: I2C device register offset.
//
// `ioBuf`: Data to be written to the device register.
struct I2cRegData {
  1: i32 regOffset;
  2: list<byte> ioBuf;
}

// Defines a sysfs attribute to create on a CPLD device via the
// fbcpld_generic kernel driver.
//
// `name`: Name of the sysfs file to create.
//
// `mode`: Access mode - "ro" (read-only, 0444), "rw" (read-write, 0644),
// or "wo" (write-only, 0200). Defaults to "ro".
//
// `regAddr`: Register address as hex string (e.g., "0x10").
//
// `bitOffset`: Starting bit position in the register (0-7). Defaults to 0.
//
// `numBits`: Number of bits (1-8). E.g., bitOffset=4 numBits=4 selects
// bits [7:4]. Defaults to 1.
//
// `flags`: Optional list of behavior flags:
//   "negate"     - Invert value (active-low signals)
//   "decimal"    - Show as decimal (default is hex)
//   "show_notes" - Include register info in output
//   "log_write"  - Log writes to kernel log
//
// `description`: Help text for the attribute.
struct CpldSysfsAttr {
  1: string name;
  2: string mode = "ro";
  3: string regAddr;
  4: i32 bitOffset = 0;
  5: i32 numBits = 1;
  6: list<string> flags;
  7: string description;
}

// Configuration for a generic fan CPLD device (fbfancpld driver).
// Sent to the driver via ioctl after device creation.
//
// `numFans`: Number of fan trays.
//
// `pwmMax`: Maximum PWM register value (e.g. 40 or 64).
//
// `speedMultiplier`: Tach register to RPM multiplier (e.g. 150 or 300).
//
// `hasRearTach`: Whether fans have both front and rear tach sensors.
//
// `hasLeds`: Whether fan trays have LED indicators.
struct FanCpldConfig {
  1: i32 numFans;
  2: i32 pwmMax;
  3: i32 speedMultiplier;
  4: bool hasRearTach;
  5: bool hasLeds;
}

// A value to write to a sysfs attribute (relative to the i2c device dir)
// after the device's kernel driver has bound.
struct I2cDeviceSysfsValue {
  1: string attr; // e.g. "idle_state"
  2: string value; // e.g. "-2"
}

// `I2cDeviceConfig` defines a i2c device within any PmUnit.
//
// `busName`: Refer to Bus Naming Convention above.
//
// `address`: I2c address used by the device in hex notation
//
// `kernelDeviceName`: The device name used by kernel to identify the device
//
// `pmUnitScopedName`: The name assigned to the device in the config, unique
// within the scope of PmUnit.
//
// `isGpioChip`: Whether this I2C Device is a GpioChip
//
// `numOutgoingChannels`: Number of outgoing channels (applies only for mux)
//
// `hasBmcMac`: Whether this has BMC MAC address (applies only to EEPROM)
//
// `hasCpuMac`: Whether this has CPU MAC address (applies only to EEPROM)
//
// `hasSwitchAsicMac`: Whether this has Switch ASIC MAC addresses (applies
// only to EEPROM)
//
// `hasReservedMac`: Whether this has Reserved MAC addresses (applies only to
// EEPROM)
//
// `initRegSettings`: initial I2c register values before device creation.
//
// `isWatchdog`: Whether this I2C Device is a Watchdog device
//
// `isEeprom`: Whether this I2C Device is an EEPROM device
//
// `eepromOffset`: offset for eeprom content.  Applies only to EEPROM device
//
// `cpldSysfsAttrs`: list of CPLD sysfs attributes to create for this device
//
// `fanCpldConfig`: configuration for a generic fan CPLD device, sent to the
// fbfancpld driver via ioctl after device creation
//
// `postCreateSysfsValues`: A value to write to a sysfs attributes
//
// For example, the three i2c devices in the below Sample PmUnit will be modeled
// as follows
//
// sensor1 = I2cDeviceConfig( busName="INCOMING@0", address="0x12",
// kernelDeviceName="lm75", pmUnitScopedName="sensor1")
//
// sensor2 = I2cDeviceConfig( busName="mux1@0", address="0x13",
// kernelDeviceName="lm75", pmUnitScopedName="sensor2")
//
// mux1 = I2cDeviceConfig( busName="INCOMING@1", address="0x54",
// kernelDeviceName="pca9x48", pmUnitScopedName="mux1", numOutgoingChannels=3)
//                    ┌──────────────────────────────────────────┐
//                    │               Sample PmUnit              │
//    INCOMING@0      │                       ┌────┬─────┐       │
// ───────────────────┼──────────────────────▶│ 12 │     │       │
//                    │                       ├────┘     │       │
//                    │                       │ sensor1  │       │
//                    │                       └──────────┘       │
//                    │                                          │
//                    │     ┌─────────┐  mux1@0   ┌────┬─────┐   │
//    INCOMING@1      │     │   mux1  ├──────────▶│ 13 │     │   │
// ───────────────────┼────▶├────┐    ├───▶       ├────┘     │   │
//                    │     │ 54 │    ├───▶       │ sensor2  │   │
//                    │     └────┴────┘           └──────────┘   │
//                    └──────────────────────────────────────────┘
struct I2cDeviceConfig {
  1: string busName;
  2: string address;
  3: string kernelDeviceName;
  4: string pmUnitScopedName;
  5: bool isGpioChip;
  6: optional i32 numOutgoingChannels;
  7: bool hasBmcMac;
  8: bool hasCpuMac;
  9: bool hasSwitchAsicMac;
  10: bool hasReservedMac;
  11: optional list<I2cRegData> initRegSettings;
  12: bool isWatchdog;
  13: bool isEeprom;
  14: optional i16 eepromOffset;
  15: optional list<CpldSysfsAttr> cpldSysfsAttrs;
  16: optional list<I2cDeviceSysfsValue> postCreateSysfsValues;
  17: optional FanCpldConfig fanCpldConfig;
}

// Configs for sensors which are embedded (eg within CPU).
//
// `pmUnitScopedName`: The name used to refer to this device. It should be
// be unique within the PmUnit.
//
// `sysfsPath`: This is the path assigned to the device by the kernel. It
// is the path where the `hwmon` directory is present.
//
struct EmbeddedSensorConfig {
  1: string pmUnitScopedName;
  2: string sysfsPath;
}

// The IDPROM which contains information about the PmUnit.  The
// PmUnitScopedName of the IDPROM device is always just "IDPROM".
//
// `busName`: This bus should be directly from the CPU, or an incoming bus into
// the PmUnit (i.e., there should not be any mux or fpga in between).  In the
// case of former, the I2C Adapter name should be used, and in the case of
// latter, the INCOMING@ notation should be used. Note, this bus can originate
// from a mux/fpga in an upstream PmUnit.
//
// `address`: I2C address of the IDPROM in hex notation
//
// `kernelDeviceName`: The device name used by kernel to identify the device
//
// `offset`: The offset at which Meta V5 IDPROM format resides.
struct IdpromConfig {
  1: string busName;
  2: string address;
  3: string kernelDeviceName;
  4: i16 offset;
}

// Defines a generic IP block in the FPGA
//
// `pmUnitScopedName`: The name used to refer to this device. It should be
// be unique within the PmUnit.
//
// `deviceName`: It is the name used in the ioctl system call to create the
// corresponding device. It should one of the compatible strings specified in
// the kernel driver.
//
// `iobufOffset`: It is the iobuf register hex offset of the SPI Master in the
// FPGA.
//
// `csrOffset`: It is the csr register hex offset of the SPI Master in the FPGA.
struct FpgaIpBlockConfig {
  1: string pmUnitScopedName;
  2: string deviceName;
  3: string iobufOffset;
  4: string csrOffset;
}

// Defines the I2C Adapter config in FPGAs.
//
// `fpgaIpBlockConfig`: See FgpaIpBlockConfig above
//
// `numberOfAdapters`: Number of I2C Adapters created by this block.
//
// `busFreqHz`: I2C bus clock frequency in Hz. Applies to all buses in this block.
struct I2cAdapterConfig {
  1: FpgaIpBlockConfig fpgaIpBlockConfig;
  2: i32 numberOfAdapters;
  3: optional i32 busFreqHz;
}

// Defines generic I2C Adapter block in FPGAs.
//
// `pmUnitScopedNamePrefix`: The prefix used to refer to this device
//  Example: pmUnitScopedNamePrefix: SMB_I2C_ADAPTER, the expanded form would be
//  SMB_I2C_ADAPTER_1, SMB_I2C_ADAPTER_2, etc.
//
// `deviceName`: It is the name used in the ioctl system call to create the
// corresponding device. It should be one of the compatible strings specified in
// the kernel driver.
//
// `csrOffsetCalc`: Calculation to get the csr offset for fpga block
//  This expression includes a base start address and an adapter index.
//  Final offset result is in hex format.
//  Example:
//  csrOffsetCalc: "0x1000 + {({adapterIndex} - {startAdapterIndex})}*0x100"
//  adapterIndex=1, startAdapterIndex=1:
//    csrOffsetCalc: "0x1000 + 0*0x100"
//    csrOffsetCalc: "0x1000"
//  adapterIndex=2, startAdapterIndex=1:
//    csrOffsetCalc: "0x1000 + 1*0x100"
//    csrOffsetCalc: "0x1100"
//
// `startAdapterIndex`: Starting adapter index for calculation for each block config
//
// `numAdapters`: Number of I2C adapters for this block
//
// `numBusesPerAdapter`: Number of I2C buses created per adapter for this block
//  This is equivalent to `numberOfAdapters` in I2cAdapterConfig.
//  The default value is 1 bus per adapter.
//
// `iobufOffsetCalc`: Calculation to get the iobuf offset for fpga block
//  This expression includes a base start address and an adapter index.
//  Final offset result is in hex format.
//  Example:
//  iobufOffsetCalc: "0x2000 + {({adapterIndex} - {startAdapterIndex})}*0x100"
//  adapterIndex=1, startAdapterIndex=1:
//    iobufOffsetCalc: "0x2000 + 0*0x100"
//    iobufOffsetCalc: "0x2000"
//  adapterIndex=2, startAdapterIndex=1:
//    iobufOffsetCalc: "0x2000 + 1*0x100"
//    iobufOffsetCalc: "0x2100"
//
// `busFreqHz`: I2C bus clock frequency in Hz. Applies to all buses in this block.
struct I2cAdapterBlockConfig {
  1: string pmUnitScopedNamePrefix;
  2: string deviceName;
  3: string csrOffsetCalc;
  4: i32 startAdapterIndex;
  5: i32 numAdapters;
  6: i32 numBusesPerAdapter = 1;
  7: string iobufOffsetCalc;
  8: optional i32 busFreqHz;
}

// Defines a Spi Device in FPGAs.
//
// `pmUnitScopedName`: The name used to refer to this device. It should be be
// unique among other SpiSlaves and in associated pmUnit.
// SpiDeviceConfig.pmUnitScopedName is the name of the SpiSlave device, whereas
// SpiMasterConfig.fpgaIpBlockConfig.pmUnitScopedName is the name of the
// SpiMaster device.
//
// `modalias`: Type of SpiSlave Device. spi_dev or any id in
// https://github.com/torvalds/linux/blob/master/drivers/spi/spidev.c#L702
//
// `chipSelect`: Value of chip select on the board.
//
// `maxSpeedHz`: Maximum clock rate to be used with this chip on the board.
struct SpiDeviceConfig {
  1: string pmUnitScopedName;
  2: string modalias;
  3: i32 chipSelect;
  4: i32 maxSpeedHz;
}

// Defines the SPI Master block in FPGAs.
//
// `fpgaIpBlockConfig`: See FgpaIpBlockConfig above
//
// `spiDeviceConfigs`: See SpiDeviceConfig above.
struct SpiMasterConfig {
  1: FpgaIpBlockConfig fpgaIpBlockConfig;
  2: list<SpiDeviceConfig> spiDeviceConfigs;
}

// Defines the Fan/PWM Controller block in FPGAs
//
// `fpgaIpBlockConfig`: See FgpaIpBlockConfig above
//
// `numFans`: Number of fans which is associated with this config.
struct FanPwmCtrlConfig {
  1: FpgaIpBlockConfig fpgaIpBlockConfig;
  2: i32 numFans;
}

// Defines the Transceiver Controller block in FPGAs.
//
// `fpgaIpBlockConfig`: See FgpaIpBlockConfig above
//
// `portNumber`: Port number which is associated with this config.
// Deprecated: do not use
struct XcvrCtrlConfig {
  1: FpgaIpBlockConfig fpgaIpBlockConfig;
  2: i32 portNumber;
}

// Defines generic Transceiver Controller block in FPGAs.
//
// `pmUnitScopedNamePrefix`: The prefix used to refer to this device
//  Example: pmUnitScopedNamePrefix: XCVR_CTRL, the expanded form would be
//  XCVR_CTRL_PORT_1, XCVR_CTRL_PORT_2, etc.
//
// `deviceName`: It is the name used in the ioctl system call to create the
// corresponding device. It should one of the compatible strings specified in
// the kernel driver.
//
// `csrOffsetCalc`: Calculation to get the csr offset for fpga block
//  This expression includes a base start address, port, a starting port number
//  or index. Final offset result is in hex format.
//  Example:
//  csrOffsetCalc: "0x1000 + ({portNum} - {startPort})*0x4"
//  portNum=1, startPort=1:
//    csrOffsetCalc: "0x1000 + (1 - 1)*0x4"
//    csrOffsetCalc: "0x1000"
//  portNum=2, startPort=1::
//    csrOffsetCalc: "0x1000 + (2 - 1)*0x4"
//    csrOffsetCalc: "0x1004"
//
// `numPorts`: Number of ports for this block config
//
// `startPort`: Starting port for calculation for each block config
//
// `iobufOffsetCalc`: Calculation to iobuf register hex offset of the SPI Master in
//  the FPGA. This expression includes a base start address, port, a starting port
//  number or index. Final offset result is in hex format.
//  Example
//  iobufOffsetCalc: "0x1000 + ({portNum} - {startPort})*0x4"
//  portNum=1, startPort=1:
//    iobufOffsetCalc: "0x1000 + (1 - 1)*0x4"
//    iobufOffsetCalc: "0x1000"
//  portNum=2, startPort=1::
//    iobufOffsetCalc: "0x1000 + (2 - 1)*0x4"
//    iobufOffsetCalc: "0x1004"
struct XcvrCtrlBlockConfig {
  1: string pmUnitScopedNamePrefix;
  2: string deviceName;
  3: string csrOffsetCalc;
  4: i32 numPorts;
  6: i32 startPort;
  7: string iobufOffsetCalc;
}

// Defines the LED Controller block in FPGAs.
//
// `fpgaIpBlockConfig`: See FgpaIpBlockConfig above
//
// `portNumber`: Port number which is associated with this config. Used
// for port LEDs
//
// `ledId`: Led ID for this config.
//
// Deprecated: do not use
struct LedCtrlConfig {
  1: FpgaIpBlockConfig fpgaIpBlockConfig;
  2: i32 portNumber;
  3: i32 ledId;
}

// Defines generic LED Controller block in FPGAs.
//
// `pmUnitScopedNamePrefix`: The prefix used to refer to this device
//  Example: pmUnitScopedNamePrefix: LED_CTRL, the expanded form would be
//  LED_CTRL_PORT_1_LED_1, LED_CTRL_PORT_1_LED_2, etc.
//
// `deviceName`: It is the name used in the ioctl system call to create the
// corresponding device. It should one of the compatible strings specified in
// the kernel driver.
//
// `csrOffsetCalc`: Calculation to get the csr offset for fpga block
//  This expression includes a base start address, port, a starting port number
//  or index, and a led number. Final offset result is in hex format.
//  Example:
//  csrOffsetCalc: "0x1000 + ({portNum} - {startPort})*0x8 + ({ledNum} - 1)*0x4"
//  portNum=1, ledNum=1, startPort=1:
//    csrOffsetCalc: "0x1000 + (1 - 1)*0x8 + (1 - 1)*0x4"
//    csrOffsetCalc: "0x1000"
//  portNum=1, ledNum=2, startPort=1::
//    csrOffsetCalc: "0x1000 + (1 - 1)*0x8 + (2 - 1)*0x4"
//    csrOffsetCalc: "0x1004"
//  portNum=2, ledNum=2, startPort=1:
//    csrOffsetCalc: "0x1000 + (2 - 1)*0x8 + (2 - 1)*0x4"
//    csrOffsetCalc: "0x100c"
//
// `numPorts`: Number of ports for this block config
//
// `ledPerPort`: Number of LEDs per port
//
// `startPort`: Starting port for calculation for each block config
//
// `iobufOffsetCalc`: Calculation to iobuf register hex offset of the SPI Master in
//  the FPGA. This expression includes a base start address, port, a starting port number
//  or index, and a led number. Final offset result is in hex format.
// Example
//  iobufOffsetCalc: "0x1000 + ({portNum} - {startPort})*0x8 + ({ledNum} - 1)*0x4"
//  portNum=1, ledNum=1, startPort=1:
//    iobufOffsetCalc: "0x1000 + (1 - 1)*0x8 + (1 - 1)*0x4"
//    iobufOffsetCalc: "0x1000"
//  portNum=1, ledNum=2, startPort=1::
//    iobufOffsetCalc: "0x1000 + (1 - 1)*0x8 + (2 - 1)*0x4"
//    iobufOffsetCalc: "0x1004"
//  portNum=2, ledNum=2, startPort=1:
//    iobufOffsetCalc: "0x1000 + (2 - 1)*0x8 + (2 - 1)*0x4"
//    iobufOffsetCalc: "0x100c"
//
// `lanesPerPort`: Number of transceiver lanes per port in this block.
//  Used to build the lane-to-LED mapping in BspPlatformMapping.
//
struct LedCtrlBlockConfig {
  1: string pmUnitScopedNamePrefix;
  2: string deviceName;
  3: string csrOffsetCalc;
  4: i32 numPorts;
  5: i32 ledPerPort;
  6: i32 startPort;
  7: string iobufOffsetCalc;
  8: i32 lanesPerPort = 8;
}

// Defines generic MDIO BUS Controller block in FPGAs.
//
// `pmUnitScopedNamePrefix`: The prefix used to refer to this device
//  Example: pmUnitScopedNamePrefix: RTM_L_MDIO_BUS, the expanded form would be
//  RTM_L_MDIO_BUS_1, RTM_L_MDIO_BUS_2, etc.
//
// `deviceName`: It is the name used in the ioctl system call to create the
// corresponding device. It should one of the compatible strings specified in
// the kernel driver.
//
// `csrOffsetCalc`: Calculation to get the csr offset for fpga block
//  This expression includes a base start address, busIndex
//  Final offset result is in hex format.
//  Example:
//  csrOffsetCalc: "0x200 + {busIndex}*0x20"
//  busIndex=0:
//    csrOffsetCalc: "0x200 + 0*0x20"
//    csrOffsetCalc: "0x200"
//  busIndex=1:
//    csrOffsetCalc: "0x200 + 1*0x20"
//    csrOffsetCalc: "0x220"
//
//
// `numBuses`: Number of buses for this block config
//
//
// `iobufOffsetCalc`: Calculation to get the iobuf offset for fpga block
//  This expression includes a base start address, busIndex
//  Final offset result is in hex format.
//  Example:
//  iobufOffsetCalc: "0x200 + {busIndex}*0x4"
//  busIndex=0:
//    iobufOffsetCalc: "0x200 + 0*0x4"
//    iobufOffsetCalc: "0x200"
//  busIndex=1:
//    iobufOffsetCalc: "0x200 + 1*0x4"
//    iobufOffsetCalc: "0x204"
struct MdioBusBlockConfig {
  1: string pmUnitScopedNamePrefix;
  2: string deviceName;
  3: string csrOffsetCalc;
  4: i32 numBuses;
  5: string iobufOffsetCalc;
}

// Defines the Retimer Controller block in FPGAs.
//
// `fpgaIpBlockConfig`: See FgpaIpBlockConfig above
//
// `portNumber`: Port number which is associated with this config.
struct RtmCtrlConfig {
  1: FpgaIpBlockConfig fpgaIpBlockConfig;
  2: i32 portNumber;
}

// Defines generic Retimer Controller block in FPGAs.
//
// `pmUnitScopedNamePrefix`: The prefix used to refer to this device
//  Example: pmUnitScopedNamePrefix: RTM_CTRL, the expanded form would be
//  {prefix}_RTM_CTRL_PORT_{port} (e.g. RTM_L_RTM_CTRL_PORT_1).
//
// `deviceName`: It is the name used in the ioctl system call to create the
// corresponding device. It should one of the compatible strings specified in
// the kernel driver.
//
// `csrOffsetCalc`: Calculation to get the csr offset for fpga block
//  This expression includes a base start address, port, a starting port number
//  or index. Final offset result is in hex format.
//  Example:
//  csrOffsetCalc: "BASE + ({portNum} - {startPort})*0x4"
//  portNum=1, startPort=1:
//    csrOffsetCalc: "BASE + (1 - 1)*0x4"
//    csrOffsetCalc: "BASE"
//  portNum=2, startPort=1::
//    csrOffsetCalc: "BASE + (2 - 1)*0x4"
//    csrOffsetCalc: "BASE + 0x4"
//
// `numPorts`: Number of ports for this block config
//
// `startPort`: Starting port for calculation for each block config
//
// `iobufOffsetCalc`: Calculation to iobuf register hex offset of the RTM controller in
//  the FPGA. This expression includes a base start address, port, a starting port
//  number or index. Final offset result is in hex format.
//  Example
//  iobufOffsetCalc: "BASE + ({portNum} - {startPort})*0x4"
//  portNum=1, startPort=1:
//    iobufOffsetCalc: "BASE + (1 - 1)*0x4"
//    iobufOffsetCalc: "BASE"
//  portNum=2, startPort=1::
//    iobufOffsetCalc: "BASE + (2 - 1)*0x4"
//    iobufOffsetCalc: "BASE + 0x4"
struct RtmCtrlBlockConfig {
  1: string pmUnitScopedNamePrefix;
  2: string deviceName;
  3: string csrOffsetCalc;
  4: i32 numPorts;
  5: i32 startPort;
  6: string iobufOffsetCalc;
}

// Defines PCI Devices in the PmUnits. A new PciDeviceConfig should be created
// for each unique combination of <vendorId, deviceId, subSystemVendorId,
// subSystemDeviceId>.
//
// In the case of one device (e.g. DOM FPGA) memory mapped to another PCI
// Device (e.g. IOB FPGA) in a different PmUnit, all of them might show up as a
// single PCI device in the system.  In this case, the PciDeviceConfig with the
// same <vendorId, deviceId, subSystemVendorId, subSystemDeviceId> should be
// present in both PmUnitConfigs.  For example, if a DOM FPGA in SMB PmUnit is
// memory mapped to an IOB FPGA in MCB PmUnit, there should be a
// PciDeviceConfig in MCB PmUnitConfig listing all controllers getting created
// as part of IOB FPGA, and there should be another PciDeviceConfig with the
// same identifiers in PMUnitConfig of SMB listing all the controllers getting
// created as part of the DOM FPGA.
//
// `pmUnitScopedName`: The name assigned to the device in the config, unique
// within the scope of PmUnit.
//
// `vendorId`: PCIe Vendor ID, and it must be a 4-digit hexadecimal value, such
// as “1d9b”
//
// `deviceId`: PCIe Device ID, and it must be a 4-digit hexadecimal value, such
// as “0011”
//
// `subSystemVendorId`: PCIe Sub System Vendor ID, and it must be a 4-digit
// hexadecimal value, such as “1d9b”
//
// `subSystemDeviceId`: PCIe Sub System Device ID, and it must be a 4-digit
// hexadecimal value, such as “0011”
//
// `desiredDriver`: The desired driver to support the device. The (optional)
// field allows the platform_manager to pass the device ID to the desired
// driver at run time (via "new_id" sysfs file), when the device ID cannot be
// included in the driver's static compiled-in ID table.
//
// The remaining fields are configs per controller block in the FPGA
//
struct PciDeviceConfig {
  1: string pmUnitScopedName;
  2: string vendorId;
  3: string deviceId;
  4: string subSystemVendorId;
  5: string subSystemDeviceId;
  6: list<I2cAdapterConfig> i2cAdapterConfigs; // Deprecated: do not use
  7: list<SpiMasterConfig> spiMasterConfigs;
  8: list<FpgaIpBlockConfig> gpioChipConfigs;
  9: list<FpgaIpBlockConfig> watchdogConfigs;
  10: list<FanPwmCtrlConfig> fanTachoPwmConfigs;
  11: list<LedCtrlConfig> ledCtrlConfigs; // Deprecated: do not use
  12: list<XcvrCtrlConfig> xcvrCtrlConfigs; // Deprecated: do not use
  13: list<FpgaIpBlockConfig> infoRomConfigs;
  14: list<FpgaIpBlockConfig> miscCtrlConfigs;
  15: optional string desiredDriver;
  16: list<LedCtrlBlockConfig> ledCtrlBlockConfigs;
  17: list<XcvrCtrlBlockConfig> xcvrCtrlBlockConfigs;
  18: list<FpgaIpBlockConfig> mdioBusConfigs; // Deprecated: do not use
  19: list<FpgaIpBlockConfig> sysLedCtrlConfigs;
  20: list<MdioBusBlockConfig> mdioBusBlockConfigs;
  21: list<I2cAdapterBlockConfig> i2cAdapterBlockConfigs;
  22: list<RtmCtrlBlockConfig> rtmCtrlBlockConfigs;
}

// These are the PmUnit slot types. Examples: "PIM_SLOT", "PSU_SLOT" and
// "FAN_SLOT"
typedef string SlotType

// The below struct holds the global properties for each SlotType within any
// platform.  This means all slots of the same SlotType within a platform
// should have the same number of outgoing I2C buses, and same IdpromConfig. At
// least one of idpromConfig or pmUnitName should be present.
//
// If both are present, the exploration will use pmUnitName to proceed with
// exploration.
//
// Also, if both are present, the pmUnitName in idprom contents should match
// pmUnitName defined here.  The exploration will warn if there is mismatch of
// pmUnitName.
struct SlotTypeConfig {
  1: i32 numOutgoingI2cBuses;
  2: optional IdpromConfig idpromConfig;
  3: optional string pmUnitName;
}

// SlotConfig holds information specific to each slot.
//
// `slotType`: Type of the slot. Examples: "PIM_SLOT", "PSU_SLOT"  and
// "FAN_SLOT".
//
// `presenceDetection`: Logic to determine whether a PmUnit has been plugged in
// this slot. Need not be described if there is no presence detection for this
// slot
//
// `outgoingI2cBusNames`: is the list of the buses from the PmUnit perspective
// which are going out in the slot.  Refer to Bus Naming Convention above.
struct SlotConfig {
  1: SlotType slotType;
  2: optional platform_manager_presence.PresenceDetection presenceDetection;
  3: list<string> outgoingI2cBusNames;
}

// `PmUnitConfig` defines the configuration of PmUnit.
//
// `pluggedInSlotType`: The SlotType where the PmUnit is plugged in.
//
// `i2cDeviceConfigs`: List of I2cDeviceConfigs on the PmUnit
//
// `outgoingSlotConfigs`: Details about the slots present on the PmUnit. Slot
// Name is the key.
struct PmUnitConfig {
  1: SlotType pluggedInSlotType;
  2: list<I2cDeviceConfig> i2cDeviceConfigs;
  3: map<string, SlotConfig> outgoingSlotConfigs;
  4: list<PciDeviceConfig> pciDeviceConfigs;
  5: list<EmbeddedSensorConfig> embeddedSensorConfigs;
}

// `VersionedPmUnitConfig` defined a configuration of PmUnit in re-spinned
// platforms.
//
// `PmUnitConfig`: PmUnit configuration. Refer to PmUnitConfig definition above.
//
// `productSubVersion`: This refers to field Type 10 in Meta EEPROM V5.
//
// `pmUnitVersions`: List of PmUnit versions this config applies to. A system
// matching any version in this list will use this config.
// `productSubVersion` is ignored when `pmUnitVersions` is present. At least
// one of `productSubVersion` or `pmUnitVersions` must be set.
struct VersionedPmUnitConfig {
  1: PmUnitConfig pmUnitConfig;
  3: optional i16 productSubVersion;
  4: optional list<PmUnitVersion> pmUnitVersions;
}

// `PmUnitInfo`: Details of a PmUnit.
//
// `name`: Name of the PmUnit.
//
// `version`: Version of the pmUnit.
//
// `eepromProductName`: Product Name from EEPROM (Type 1). Unlike `name`,
// this is always the raw EEPROM value and is never overridden by config.
// Used by sensor_service for vendor-specific sensor resolution (e.g.,
// different PSU vendors have different thresholds).
struct PmUnitInfo {
  1: string name;
  2: optional PmUnitVersion version;
  3: optional platform_manager_presence.PresenceInfo presenceInfo;
  4: bool successfullyExplored;
  5: optional string eepromProductName;
}

// `PmUnitVersion`: Version of a PmUnit.
//
// `productionState`: Production State (EEPROM V6 Type 8).
//
// `productionSubState`: Production Sub-State (EEPROM V6 Type 9).
//
// `respinVariantIndicator`: Re-Spin/Variant Indicator (EEPROM V6 Type 10).
struct PmUnitVersion {
  1: i16 productionState;
  2: i16 productionSubState;
  3: i16 respinVariantIndicator;
}

// Defines thrift structure used for the Bsp Kmods file under /usr/local/{vendor}_bsp/...
// This file will be written during BSP development. PM will consume this file to unload
// specified kmods before exploration.
//
// `bspKmods`: Specify the list of names of bsp kmods on the installed rpm.
//
// `sharedKmods`: Specify the list of names of shared kmods on the installed rpm.
// These shared kmods will be unloaded after bspKmods.
struct BspKmodsFile {
  1: list<string> bspKmods;
  2: list<string> sharedKmods;
}

// Defines the whole Platform. The top level struct.
struct PlatformConfig {
  // Name of the platform.  Should match the name set in dmidecode
  1: string platformName;

  // This is the PmUnit from which the exploration will begin. The IDPROM of
  // this PmUnit should be directly connected to the CPU SMBus.
  2: string rootPmUnitName;

  // This is the SlotType of the rootPmUnit.
  3: string rootSlotType;

  // Map from SlotType name to the global properties of the SlotType.
  11: map<SlotType, SlotTypeConfig> slotTypeConfigs;

  // List of PmUnits which the platform can support. Key is the PmUnit name.
  12: map<string, PmUnitConfig> pmUnitConfigs;

  // List of the i2c buses created from the CPU.  Entries can use either:
  //  (a) Virtual names "CPU_BUS@N" — resolved at runtime by detecting the
  //      CPU vendor (via folly::CpuId) and scanning sysfs for the
  //      corresponding adapter:
  //        - Intel: matches "SMBus I801 adapter at <offset>" by adapter
  //          name.  Only CPU_BUS@0 is supported today.
  //        - AMD: identifies DesignWare I2C buses via ACPI
  //          firmware_node/path under /sys/devices/platform/AMDI0010:*.
  //          CPU_BUS@0 maps to \_SB_.I2CB, CPU_BUS@1 to \_SB_.I2CA,
  //          CPU_BUS@2 to \_SB_.I2CC, CPU_BUS@3 to \_SB_.I2CD.
  //  (b) Exact adapter name matching /sys/bus/i2c/devices/i2c-N/name
  //      (e.g. "SMBus I801 adapter at 5000").
  // All entries in a single config must use the same style.
  13: list<string> i2cAdaptersFromCpu;

  // Global mapping from an application friendly path (symbolic link) to
  // DevicePath. DevicePath documentation can be found earlier in the file
  14: map<string, string> symbolicLinkToDevicePath;

  // Map from PmUnit name to a list of PmUnitConfigs which apply to specific
  // versions of the platform.  This typically applies to re-spins and
  // second-source boards/PmUnits.
  15: map<string, list<VersionedPmUnitConfig>> versionedPmUnitConfigs;

  // Chassis EEPROM DevicePath. This is used to find the Production
  // Phase (EVT/DVT/PVT/MP) and Serial Number of the chassis.
  16: string chassisEepromDevicePath;

  // Number of transceivers in the platform.
  17: i16 numXcvrs;

  // Name and version of the rpm containing the BSP kmods for this platform
  21: string bspKmodsRpmName;
  22: string bspKmodsRpmVersion;

  // Specify the list of in-tree kmods which are required to be loaded before PM
  // exploration.
  // Most kmods are loaded automatically during device creation. This field is
  // only for kmods which need to be loaded before any devices are created in
  // order to work properly.
  25: list<string> nonBspKmodsToLoad;

  // Number of retimers in the platform.
  26: i16 numRtms;
}
