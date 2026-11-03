# Meta/OpenBMC Software Requirements

This document defines requirements for developing and delivering Meta OpenBMC software.

## Table of Contents

- [Meta/OpenBMC Software Requirements](#metaopenbmc-software-requirements)
- [Table of Contents](#table-of-contents)
- [1 Pull Requests Guidelines](#1-pull-requests-guidelines)
- [2 Common Requirements](#2-common-requirements)
  - [2.1 U-boot](#21-u-boot)
  - [2.2 Linux](#22-linux)
  - [2.3 Yocto](#23-yocto)
  - [2.4 systemd](#24-systemd)
  - [2.5 CLI commands/tools](#25-cli-commandstools)
  - [2.6 Naming](#26-naming)
  - [2.7 Machine-layer OpenBMC Code](#27-machine-layer-openbmc-code)
  - [2.8 Open Source](#28-open-source)
- [3 Meta-OpenBMC Image Types](#3-meta-openbmc-image-types)
- [4 Minimum OpenBMC Image](#4-minimum-openbmc-image)
- [5 Provisioning-Ready OpenBMC Image](#5-provisioning-ready-openbmc-image)
  - [5.1 Fixed MAC Address](#51-fixed-mac-address)
  - [5.2 DHCP Requests](#52-dhcp-requests)
  - [5.3 “wedge\_power.sh”](#53-wedge_powersh)
  - [5.4 weutil](#54-weutil)
  - [5.5 wedge\_us\_mac.sh](#55-wedge_us_macsh)
  - [5.6 lldp-util](#56-lldp-util)
  - [5.7 X86 Console Access](#57-x86-console-access)
- [6 Complete OpenBMC Image](#6-complete-openbmc-image)
  - [6.1 MMC Utilities](#61-mmc-utilities)
  - [6.2 OSS OpenBMC build](#62-oss-openbmc-build)
  - [6.3 IPMI](#63-ipmi)
  - [6.4 REST-API](#64-rest-api)
  - [6.5 Backup Path Utilities](#65-backup-path-utilities)
- [7 OpenBMC CIT](#7-openbmc-cit)
  - [7.1 QEMU Support](#71-qemu-support)
  - [7.2 Test coverage](#72-test-coverage)

## 1 Pull Requests Guidelines

Engineers must follow the guidelines defined in [Meta/OpenBMC Pull Request
Guidelines](/docs/openbmc/openbmc_pr_guidelines) document when submitting Pull Requests for Meta OpenBMC projects.

## 2 Common Requirements

### 2.1 U-boot

- Always adopt the latest u-boot version when starting a new platform. The
  latest u-boot version is v2019.04 as of Aug. 2024.
- Make sure ECC (memory) is enabled in u-boot.
- Make sure the 2nd-watchdog timer is programmed to 5 minutes in u-boot: the
  2nd-watchdog timer will be disabled by Linux after boot up.
- Make sure `tftpboot` works in the u-boot command line: u-boot ip can be manually
  or DHCP assigned.   Ipv6 or ipv4?

### 2.2 Linux

- Always adopt the latest Linux kernel when starting a new platform. The latest
  Linux version is v6.18 as of May 2026.
- Enable the primary watchdog in device tree, but do not install additional
  services to kick the primary watchdog: use the default kthread spawned by watchdog driver.
- Make sure the 2nd watchdog is disabled at the end of Linux boot up
  `(PACKAGECONFIG += "disable-watchdog")`
- Apply flash partitions defined in the below file instead of inventing a new
  layout.

    [facebook-bmc-flash-layout-128.dtsi](https://github.com/torvalds/linux/blob/master/arch/arm/boot/dts/facebook-bmc-flash-layout-128.dtsi)

- Include `conf/machine/include/mtd-ubifs.inc` in `<machine>.conf` file: it
  enables UBIFS on BMC boot flashe’s `data0` partition, and the partition will
  be mounted under `/mnt/data` at boot up time.
- Add `"emmc"` and `"emmc-ext4"` in machine features `(MACHINE_FEATURES += "emmc
  emmc-ext4")`: it installs necessary eMMC utilities, format eMMC with EXT4 and
  mounted under `/mnt/data1` at boot up time.

### 2.3 Yocto

- Always adopt the latest yocto version when starting a new platform.

### 2.4 systemd

- `systemd` (rather than `sysV`) is required for all the FBOSS BMC-lite systems.

### 2.5 CLI commands/tools

- All the CLI commands and tools must `exit 0` on success, and a positive number
  for failures.
- All the CLI commands must support the `-h` option, which prints help message
  and `exit 0`.
- A positive return value must always indicate errors: if it’s no-op (for
  example, power on a component which is already on), the command must `exit 0`.

### 2.6 Naming

Do NOT use vendor’s code name in file/directory names or source files: always
use Meta provided code name.

### 2.7 Machine-layer OpenBMC Code

Machine-layer OpenBMC source code (under `meta-facebook/meta-<platform>`) must
be less than 500 lines.

### 2.8 Open Source

All the OpenBMC source code is open sourced, with no exception.

## 3 Meta-OpenBMC Image Types

Meta-OpenBMC Image are categorized to 3 types based on feature list and project
timelines:

1. **Minimum OpenBMC Image.**

    The minimum OpenBMC Image ensures the image is bootable and upgradable with
    the minimum set of software (drivers and applications).

2. **Provisioning-Ready OpenBMC Image.**

    The Provisioning-Ready OpenBMC Image is “Minimum OpenBMC Image” plus all the
    required features for FBOSS provisioning.

3. **Full OpenBMC Image.**

    The full OpenBMC Image is “Provisioning-Ready OpenBMC Image” plus all the
    required services and utilities, such as `ipmid`, `rest-api`, etc.

Section 4, 5 and 6 describes the detailed requirements of the above 3 image
types.

## 4 Minimum OpenBMC Image

The Minimum OpenBMC Image is used to bring up and sanity verify the compatibility
between BMC SoC and U-boot/Linux software.

**Partners must submit all the relevant Pull Requests to produce the Minimum
OpenBMC Image by the end of <DVT + 1 month>.**

Below are the detailed requirements of the Minimum OpenBMC image:

- `bitbake <platform>-image` can produce the minimum OpenBMC image from
  `openbmc.<vendor>` repository successfully.
- OpenBMC can boot up and reach the Linux login prompt without user intervention:
  no errors or warnings dumped at boot up time.
- OpenBMC is reachable via serial console after boot up.
- OpenBMC is pingable and sshable via DHCP-assigned or SLAAC global IPv6 address.
- OpenBMC can run for at least 24 hours without reboots (no watchdog timeout or
  kernel panic, etc.).
- OpenBMC is upgradable, means flash0/flash1 is exported to user space via
  `/dev/mtd#`, and people can update new OpenBMC images using `flashcp`.

The minimum image may not have all the client (I2C, SPI, GPIO, etc.) devices
initialized, and OpenBMC IP may change across reboots, and it’s acceptable for
the minimum OpenBMC image.

## 5 Provisioning-Ready OpenBMC Image

The provisioning-Ready OpenBMC Image is “Minimum OpenBMC Image” plus all the
features required by FBOSS provisioning.

**All the switches shipped to Meta site must contain at least the
Provisioning-Ready OpenBMC Image.**

**Partners must submit all the revent Pull Requests to produce the
Provisioning-Ready OpenBMC Image by the end of <DVT + 3 months>.**

Below are the detailed requirements for the Provisioning-Ready OpenBMC Image (in
addition to the requirements of Minimum OpenBMC Image). For more context, please
refer to “Meta FBOSS Provisioning requirements” document.

### 5.1 Fixed MAC Address

OpenBMC MAC address must be fixed after booting up Linux (means
`eth0_mac_fixup.sh` must be installed at bootup).

### 5.2 DHCP Requests

`weutil-dhcp-id` package must be included in `<platform>-image.bb` to ensure
model ID and serial number being included in DHCP requests.

### 5.3 “wedge_power.sh”

- `wedge_power.sh` must be installed in OpenBMC for power control purposes, and it
  must return to the console within 15 seconds (in the worst case) regardless of
  system state. Below are the mandatory command line arguments:
  - **`wedge_power.sh off` to power off x86.**
    - If x86 is already in power-off state, the command is no-op and it must
          return 0 immediately.
    - If additional time is needed for the capacitor to complete discharge,
          the discharge must complete within 3 seconds. In this case,
          polling/delay logic is needed in `wedge_power.sh off` to make sure x86
          is powered off successfully when the command returns to the console.
    - If the command cannot power off x86 within 15 seconds, it must return
          a positive error code (1-255, such as `EIO` or `ETIME`) and print a
          descriptive message to indicate the failure.
    - If the command was issued in the process of x86 power sequencing, it
          needs to return the error code `EBUSY`.
  - **`wedge_power.sh on` to power on x86.**
    - If x86 is already powered on, the command is no-op and it must return
         0 immediately.
    - If the command cannot power on x86 within 15 seconds, it must return a
          positive error code (such as `EIO` or `ETIME`) to indicate the failure.
    - If the command was issued in the process of x86 power sequencing, it
          should return error code `EBUSY`.
  - **`wedge_power.sh reset` to reset x86.**
    - If x86 is in power-off state, the command should return error code EINVAL and print:

      ```bash
      userver is off, please run <wedge_power.sh on> to power on userver
      ```

    - If x86 is in power-on state, the command resets x86. If hardware needs
          additional time to reset x86, delay/poll logic is needed in
          `wedge_power.sh reset` to make sure x86 is reset properly when the
          command exits.
    - If the command was issued in the process of x86 power sequencing, it
          should return error code `EBUSY`.
    - If the command cannot reset x86 within 15 seconds, it must return a
          positive error code (such as `EIO` or `ETIME`) to indicate the failure.
  - **`wedge_power.sh status` to test x86 power status.**
    - The command must print “Microserver power is on” if x86 is in power-on
          state.
    - The command must print “Microserver power is off” if x86 is in
          power-off state.
    - If the command was issued in the process of x86 power sequencing, the
          command needs to wait till power sequencing is complete, and then print
          x86 power state.
    - If the command cannot determine power state within 15 seconds, it must
          return a positive error code to indicate the failure.
  - **`wedge_power.sh reset -s` to reset the entire chassis.**
    - The command is used to power cycle the entire chassis (BMC and X86)
          ungracefully: hardware must be able to complete power cycling within 3
          seconds.
    - The command won’t return to the console in normal conditions, because
          the entire chassis (including OpenBMC) is power cycled.
    - If the power cycle cannot be done within 15 seconds, the command must
          return a positive error code to indicate the failure.

### 5.4 weutil

`weutil` is designed to retrieve the Serial Number and other information
essential for device identification.

The output is provided as colon+space separated keys and values, one per line.
And below are the mandatory fields:

- Product Name
- Production State
- Production Sub-State
- Re-Spin/Variant Indicator
- Product Serial Number
- BMC MAC Base
- CRC16

### 5.5 wedge_us_mac.sh

`wedge_us_mac.sh` returns the mac address of the x86/microserver. The mac
address is printed in the standard colon-separated hex format followed by a
newline.

For example:

```bash
bash$ wedge_us_mac.sh

de:ad:be:ef:ca:fe
```

### 5.6 lldp-util

`lldp-util` must be installed in OpenBMC image, and this command is expected to
display the received lldp broadcasts in the following format:

```bash
OpenBMC# lldp-util

Listening for LLDP Packets…

LLDP: local_port=eth0 remote_system=rsw1aa.99.tst1 remote_port=Ethernet36
```

### 5.7 X86 Console Access

`sol.sh` and `mTerm` must be installed to access the x86 console.

## 6 Complete OpenBMC Image

The Complete OpenBMC Image is “Provisioning-Ready OpenBMC Image” plus all the
services and utilities for system health management.

**Partners must submit all the revent Pull Requests to produce the Complete
OpenBMC Image by the end of DVT exit.**

Below section described the requirements in addition to the Provisioning-Ready
OpenBMC image requirements.

### 6.1 MMC Utilities

`mmc`, `mmcraw` and `blkdiscard` commands must be installed: these commands are
used to wipe data from the eMMC device.

### 6.2 OSS OpenBMC build

People must be able to build OSS OpenBMC images from
[facebook/openbmc](https://github.com/facebook/openbmc)
repository. The OSS build must meet all the requirements defined for
Provisioning-Ready OpenBMC Image, because a switch may need to be reprovisioned
later.

### 6.3 IPMI

**All the FBOSS OpenBMC platforms must be adopted following the ipmid2’s
architecture.  That is a configuration driving  model rather than using the
LIBPAL to define platform-specific LIBPAL functions.**

New NPIs need to define all platform-specific configurations in the `config.json`
file.

Example:  `meta-facebook/meta-elbert/recipes-elbert/ipmid-v2/files/config.json`

### 6.4 REST-API

What endpoints must be implemented in BMC-Lite OpenBMC??

### 6.5 Backup Path Utilities

`recover_x86.sh`

`fw-util`

## 7 OpenBMC CIT

### 7.1 QEMU Support

1. Vendor must implement OpenBMC platform QEMU machine support in OpenBMC QEMU
2. Vendor must upstream all QEMU patches, and get approval for all patches
   before DVT exit

### 7.2 Test coverage

1. Implement all relevant tests from
https://github.com/facebook/openbmc/tree/helium/tests2/common
