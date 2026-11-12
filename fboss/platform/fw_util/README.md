# Firmware Util - A FBOSS Platform Utility

## Objective
The primary functionality of this utility is to updating all firmwares in FBOSS based network switches


## Design Concept
* Configuration driven, platform sepecific firmware util configuration JSON files are located in a directory
that bear the name of the platform. We are embedding the json file in a cpp file. Our goal is to use as few Meta
specific library as possible in the TARGET files because we have specific size requirement to meet since fw_util
will be run in ramdisk and should ideally be under 1MB.

## Config Json
* Vendor needs to provide the config json file. A example of config json can be found in fw_util/darwinFwutil directory.
fw_util will read from the config json to upgrade and pull the version of several field programmable components


## Usage
Below is an example of running fw_util on darwin

Firmware Target name (aka fw entities such as bios, cpld... etc) will varies based on the platform and will be part of the provided json

```
$ fw_util --help
Firmware utility for FBOSS platforms
Usage: fw_util [OPTIONS]

Options:
  -h,--help                   Print this help message and exit
  --version                   Display program version information and exit
[Option Group: Firmware Options]
  Specify firmware action, target, and optional binary file
  Options:
    --fw_target_name TEXT=all   Firmware target name, to see list of targets, run fw_util --fw_action=list
    --fw_action TEXT:{program,verify,read,version,list,audit}=version
                                Firmware action (program, verify, read, version, list, audit)
    --fw_binary_file TEXT       Firmware binary file path to be programmed
    --config_file TEXT          Path to the fw_util config file
[Option Group: Flags]
  Additional flags
  Options:
    --verify_sha1sum            Verify SHA1 sum of the firmware
    --dry_run                   Dry run without actually programming firmware

Examples:
  fw_util  (defaults to --fw_action=version --fw_target_name=all)
  fw_util --fw_action=program --fw_target_name=bios --fw_binary_file=/path/to/firmware.bin
  fw_util --fw_action=verify --fw_target_name=FPGA --fw_binary_file=/path/to/firmware.bin
  fw_util --fw_action=version --fw_target_name=CPLD
  fw_util --fw_action=list
  fw_util --fw_action=audit
```
