include "thrift/annotation/thrift.thrift"

@thrift.AllowLegacyMissingUris
package;

namespace cpp2 facebook.fboss.platform.fw_util_config

// Define a struct for writeToPort arguments
struct WriteToPortConfig {
  // hexadecimal Byte value to be written
  1: string hexByteValue;
  // Port file to write to
  2: string portFile;
  // hexadecimal Offset in the port file
  3: string hexOffset;
}

// Define a struct for flashrom firmware configuration
struct FlashromConfig {
  // Programmer type for flashrom
  1: optional string programmer_type;
  // Programmer for flashrom
  2: optional string programmer;
  // Chip for flashrom (optional).
  // In some case, the detection will be done at runtime
  3: optional list<string> chip;
  // Extra arguments for flashrom (optional)
  4: optional list<string> flashromExtraArgs;
  // argument for the spi_layout
  5: optional string spi_layout;
  // Contents for overriding sections in the binary
  6: optional string custom_content;
  // Offset for the custom content
  7: optional i32 custom_content_offset;
}

// Define the struct for Jam tooling upgrade cases
struct JamConfig {
  // jam extra argument
  1: list<string> jamExtraArgs;
}

// Define the struct for Xapp tooling upgrade cases
struct XappConfig {
  //Xapp extra argument
  1: list<string> xappExtraArgs;
}

// Define the struct for psu_util tooling upgrade cases
struct PsuUtilConfig {
  // psu_util extra arguments
  1: list<string> psuUtilExtraArgs;
}

// Define a struct for gpioset firmware configuration
struct GpiosetConfig {
  // GPIO chip for gpioset
  1: string gpioChip;
  // GPIO chip column for gpioset
  2: string gpioChipValue;
  // GPIO chip pin for gpioset
  3: string gpioChipPin;
}
// Define a struct for gpioget firmware configuration
struct GpiogetConfig {
  1: string gpioChip; // GPIO chip for gpioget
  2: string gpioChipPin; // GPIO chip pin for gpioget
}

// Define struct for jtag register configuration
struct JtagConfig {
  // path to write to
  1: string path;
  // integer value to write to
  2: i32 value;
}

// Define a struct for pre-upgrade configuration
struct PreFirmwareOperationConfig {
  // Command type for pre-upgrade
  1: string commandType;
  // Arguments for pre-upgrade command
  2: optional WriteToPortConfig writeToPortArgs;
  // configuration for the platform firmware
  3: optional FlashromConfig flashromArgs;
  // configuration for setting gpios
  4: optional GpiosetConfig gpiosetArgs;
  // configuration for writing to jtag register
  5: optional JtagConfig jtagArgs;
}

// Define a struct for verify configuration
struct VerifyFirmwareOperationConfig {
  // Command type for verifying firmware
  1: string commandType;
  // configuration for the platform firmware
  2: optional FlashromConfig flashromArgs;
}

// Define a struct for read configuration
struct ReadFirmwareOperationConfig {
  // Command type for reading firmware
  1: string commandType;
  // configuration for the platform firmware
  2: optional FlashromConfig flashromArgs;
}

// Define a struct for post-upgrade configuration
struct PostFirmwareOperationConfig {
  // Command type for post-upgrade
  1: string commandType;
  // Arguments for post-upgrade command
  2: optional WriteToPortConfig writeToPortArgs;
  // configuration for setting gpios
  3: optional GpiogetConfig gpiogetArgs;
  // configuration for writing to jtag register
  4: optional JtagConfig jtagArgs;
}

// Define a struct for upgrade configuration
struct UpgradeConfig {
  // Command type for upgrade
  1: string commandType;
  // Arguments for upgrade command
  2: optional FlashromConfig flashromArgs;
  // cases where jam is being used
  3: optional JamConfig jamArgs;
  // cases where xapp is being used
  4: optional XappConfig xappArgs;
  // cases where psu_util is being used
  5: optional PsuUtilConfig psuUtilArgs;
}

// Define a struct for version configuration
struct VersionConfig {
  // Type of version (e.g. dmidecode, sysfs)
  1: string versionType;
  // Path which contains the version
  2: optional string path;
  // this command is used for darwin and will be
  // remove once darwin is moved to platform manager
  3: optional string getVersionCmd;
}

// Define a struct for firmware operation
struct FwConfig {
  // Pre-upgrade configuration
  1: optional list<PreFirmwareOperationConfig> preUpgrade;
  // Upgrade configuration
  2: optional list<UpgradeConfig> upgrade;
  // Post-upgrade configuration
  3: optional list<PostFirmwareOperationConfig> postUpgrade;
  // verify configuration
  4: optional VerifyFirmwareOperationConfig verify;
  // read configuration
  5: optional ReadFirmwareOperationConfig read;
  // Version configuration
  6: VersionConfig version;
  // Priority for firmware upgrade
  7: i32 priority;
  // SHA-1 sum for firmware
  8: optional string sha1sum;
  // Desired version for firmware
  9: optional string desiredVersion;
  // Seconds to wait after the upgrade operations complete
  10: optional i32 delayAfterUpgradeSec;
}

typedef string DeviceName

// Top-level config struct
struct FwUtilConfig {
  1: map<DeviceName, FwConfig> fwConfigs;
}
