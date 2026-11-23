# FBOSS BSP Kernel Module Development Requirements

import TestCase from '@site/src/components/TestCase';

## Version: 1.0.0

The Meta Kernel team doesn't take local kernel patches, so the BSP kernel
patches (modules, drivers, etc.) must be upstreamed to the Linux kernel
community.

Before the BSP kernel drivers are accepted by the upstream maintainers (for
example, due to review delay), the drivers will be compiled as out-of-tree
kernel modules and deployed in FBOSS to unblock FBOSS development. This
document describes the BSP kernel module requirements before they are accepted
by upstream Linux. The major purposes are:

* Avoid kernel crashes caused by BSP driver bugs.
* Leverage the existing kernel subsystem APIs whenever possible to ensure
consistent ABIs for user space software.

## Table of Contents

* [FBOSS BSP Kernel Module Development Requirements](#fboss-bsp-kernel-module-development-requirements)
  * [Table of Contents](#table-of-contents)
* [1. Levels of Requirements](#1-levels-of-requirements)
* [2. Terms and Abbreviations](#2-terms-and-abbreviations)
* [3. Compatible Kernel Versions](#3-compatible-kernel-versions)
* [4. Common Requirements](#4-common-requirements)
  * [4.1 Source Code Repository Structure](#41-source-code-repository-structure)
* [5. The PCIe FPGA Driver](#5-the-pcie-fpga-driver)
  * [5.1 Auxiliary Bus](#51-auxiliary-bus)
  * [5.2 Subdevice (I/O Controller) Creation \& Deletion](#52-subdevice-io-controller-creation--deletion)
    * [5.2.1 Character Device Interface](#521-character-device-interface)
    * [5.2.2 Sysfs Interface](#522-sysfs-interface)
  * [5.3 Supported FPGA I/O Controllers](#53-supported-fpga-io-controllers)
    * [5.3.1 FPGA Information (fpga\_info)](#531-fpga-information-fpga_info)
    * [5.3.2 I2C Controller (i2c\_master)](#532-i2c-controller-i2c_master)
    * [5.3.3 SPI Controller (spi\_master)](#533-spi-controller-spi_master)
    * [5.3.4 GPIO Controller (gpiochip)](#534-gpio-controller-gpiochip)
    * [5.3.5 Fan/PWM Controller (fan\_ctrl)](#535-fanpwm-controller-fan_ctrl)
    * [5.3.6 Fan Watchdog (watchdog\_fan)](#536-fan-watchdog-watchdog_fan)
    * [5.3.7 Transceiver Controller (xcvr\_ctrl)](#537-transceiver-controller-xcvr_ctrl)
    * [5.3.8 LED (led)](#538-led-led)
    * [5.3.9 MDIO Controller (mdio\_controller)](#539-mdio-controller-mdio_controller)
* [6. I2C/SMBus Adapter Drivers](#6-i2csmbus-adapter-drivers)
  * [6.1 I2C Adapter Driver Development](#61-i2c-adapter-driver-development)
  * [6.2 ABIs for User Space](#62-abis-for-user-space)
  * [6.3 Test Requirement](#63-test-requirement)
  * [6.4 References](#64-references)
* [7. GPIO Controller Drivers](#7-gpio-controller-drivers)
  * [7.1 GPIO Controller Driver Development](#71-gpio-controller-driver-development)
  * [7.2 ABIs for User Space](#72-abis-for-user-space)
  * [7.3 Test Requirements](#73-test-requirements)
  * [7.4 References](#74-references)
* [8. Watchdog Drivers](#8-watchdog-drivers)
  * [8.1 Watchdog Driver Development](#81-watchdog-driver-development)
  * [8.2 ABIs for User Space](#82-abis-for-user-space)
  * [8.3 Test Requirements](#83-test-requirements)
  * [8.4 References](#84-references)
* [9. Hardware Monitoring Device Drivers](#9-hardware-monitoring-device-drivers)
  * [9.1 HWMON Driver Development](#91-hwmon-driver-development)
  * [9.2 ABIs for User Space](#92-abis-for-user-space)
  * [9.3 Test Requirements](#93-test-requirements)
* [10. PMBus Drivers](#10-pmbus-drivers)
  * [10.1 PMBus Driver Development](#101-pmbus-driver-development)
  * [10.2 ABIs for User Space](#102-abis-for-user-space)
  * [10.3 Test Requirements](#103-test-requirements)
* [11. LED Drivers](#11-led-drivers)
  * [11.1 LED Driver Development](#111-led-driver-development)
  * [11.2 Test Requirements](#112-test-requirements)
* [12. SPI Master Drivers](#12-spi-master-drivers)
  * [12.1 SPI Master Driver Development](#121-spi-master-driver-development)
  * [12.2 ABIs for User Space](#122-abis-for-user-space)
  * [12.3 Test Requirements](#123-test-requirements)
* [13. MDIO Controller Drivers](#13-mdio-controller-drivers)
  * [13.1 MDIO Controller Driver Development](#131-mdio-controller-driver-development)
  * [13.2 ABIs for User Space](#132-abis-for-user-space)
  * [13.3 Test Requirements](#133-test-requirements)

## 1. Levels of Requirements

* **Must Have (MH)**: The BSP must satisfy “must have” requirements. By default,
 all requirements are “must have” unless they are explicitly specified
 otherwise.
* **Strong Desire (SD)**: The BSP must support “strong desire” requirements.
There is a clear timeline when the requirements will be fully supported, if it
is not supported yet.
* **Wish List (WL)**: “Wish list” requirements are what Meta is evaluating. Any
limitation on hardware/driver to support this type of requirement shall be
explicitly stated by the chip vendor.

## 2. Terms and Abbreviations

| Term     | Definition                                                         |
|----------|--------------------------------------------------------------------|
| ABI      | Application Binary Interface                                       |
| ACPI     | Advanced Configuration and Power Interface                         |
| API      | Application Programming Interface                                  |
| BSP      | Board Support Package                                              |
| CPLD     | Complex Programmable Logic Device                                  |
| EEPROM   | Electrically Erasable Programmable Read-Only Memory                |
| FBOSS    | Facebook Open Switching System                                     |
| FPGA     | Field Programmable Gate Array                                      |
| hwmon    | The Linux Hardware Monitoring Subsystem                            |
| IP Block | A reusable unit of integrated circuit that is the Intellectual Property of one party|
| IRQ      | Interrupt Request                                                  |
| I2C      | Inter-Integrated Circuit                                           |
| MDIO     | Management Data Input/Output                                       |
| MFD      | Multi-Function Device                                              |
| PMBus    | Power Management Bus                                               |
| SMBus    | System Management Bus                                              |
| SPI      | Serial Peripheral Interface                                        |
| sysfs    | A Linux pseudo file system that exports information about various kernel subsystems to user space via virtual files|

## 3. Compatible Kernel Versions

The BSP kernel modules must be compatible with the supported kernel versions.
The current list of supported versions, along with planned/upcoming versions, is
maintained on a dedicated page:

* [Supported and Planned Kernel Versions](./kernel_versions.md)

Being compatible means:

1. The kernel modules can be compiled against the kernel version successfully.
2. The kernel modules can be loaded in the above kernel versions and behave as expected.

The per-driver test requirements that must pass on each supported kernel version
are defined in the "Test Requirement(s)" subsections under each driver type
below (see sections [6](#6-i2csmbus-adapter-drivers)–[13](#13-mdio-controller-drivers)).

## 4. Common Requirements

Below are the requirements that apply to all the kernel modules/drivers:

1. Follow the Linux kernel coding style in all the source files. Refer to the
below URL for Linux kernel coding style:

    [https://www.kernel.org/doc/html/latest/process/coding-style.html](https://www.kernel.org/doc/html/latest/process/coding-style.html)

    “scripts/checkpatch.pl” tool in the Linux kernel tree must be used to catch
    violations in coding style.
2. No warnings during kernel module compilation.
3. No unused source code. The source code is defined as “unused” if it will
never be exercised in the FBOSS environment.
4. Do not comment out source code (using `#if 0` or `//`, etc.): delete the code
if it’s not used.
5. Do not export kernel symbols (`EXPORT_SYMBOL_*`) unless they are required by
other modules.
6. Do not export ABIs (such as sysfs nodes, character devices, etc.) unless they
are required by user space FBOSS services.

    If additional ABIs are needed (for example, for debugging purposes), these
    ABIs must be disabled by default, and a “kill switch” needs to be
    implemented to enable/disable the ABIs easily. For example, CONFIG_*
    option(s), module
    parameter(s), or sysfs node(s) to turn on/off the ABIs at runtime.

7. Use “devres” managed APIs (`devm_*`) in the drivers whenever possible.

    [https://docs.kernel.org/driver-api/driver-model/devres.html#list-of-managed-interfaces](https://docs.kernel.org/driver-api/driver-model/devres.html#list-of-managed-interfaces)

8. All the kernel modules can be unloaded cleanly from the running system and
reloaded without problems.
9. Use caution when dumping logs in I/O functions or interrupt handlers, and
this is to avoid potential log spew.
10. The BSP must include a bash script named `fbsp-remove.sh` which successfully
unloads all of the provided kernel modules. This script must be kept up to date
with any changes made to the BSP.
11. A file `kmods.json` must be included in the same directory as `fbsp-remove.sh`
and this file contains a list of kmods (as named in `lsmod`) in the following
format:

    ```json
    {
        "bspKmods": [
            "kmod_A",
            "kmod_B",
        ],
        "sharedKmods": [
            "shared_kmod_C",
            "shared_kmod_D"
        ]
    }
    ```

    `fbsp-remove.sh` must read `kmods.json` and remove `bspKmods` before `sharedKmods`.
    A "shared" kmod is any kmod which other kmods depend upon.

12. No driver shall use any auto-probing mechanisms.  When a module is loaded it must
not do any transactions to detect devices to attempt to automatically probe and attach.
This applies to backported drivers as well.  If they support auto-probing and detection,
that functionality must be removed before backporting. An example is the `.address_list`
entry in i2c drivers.

### 4.1 Source Code Repository Structure

Source code shared with Meta must have the following general structure:

```json
/source-repo
├── kmods
│ ├── Makefile
│ ├── module.c
│ └── ...
└── rpmbuild
  └── <vendor>\_bsp_kmods.spec
```

The source repository must have a top-level directory named "kmods" and another
named "rpmbuild". The kmods directory must have a `Makefile` which will build
the bsp kernel modules. Any other structure decisions are left to developers.

The "rpmbuild" directory must have a file named `<vendor\>\_bsp_kmods.spec` which is
an rpm spec file. This spec file should result in an rpm that conforms to the
rest of the specifications in this document

### 4.2 RPM Package Installation Requirements

The BSP RPM must install files to specific locations for proper integration with
the FBOSS platform management system.

#### 4.2.1 Kernel Module Installation

Kernel modules must be installed in the standard kernel module path:

```
/lib/modules/<kernel-version>/extra/
```

This ensures proper integration with the Linux kernel module loading system and
allows `depmod` to correctly resolve module dependencies.

#### 4.2.2 BSP Utility Files Installation

The BSP utility files must be installed in a vendor-specific directory structure:

```
/usr/local/<vendor>_bsp/<kernel-version>/kmods.json
/usr/local/<vendor>_bsp/<kernel-version>/fbsp-remove.sh
```

Where:
- `<vendor>` is extracted from the RPM name
- `<kernel-version>` is the target kernel version (e.g., "6.4.3-0_fbk1_755_ga25447393a1d")

The Platform Manager uses this path structure to locate and manage BSP components
during installation, upgrade, and removal operations.

## 5. The PCIe FPGA Driver

Most I/O (I2C, SPI, etc) Controllers are provided by the PCIe FPGA in the FBOSS
environment. The I/O controller drivers must be developed using the
corresponding Linux subsystem APIs (described in chapter 6-13), and this section
explains:

1. List of supported I/O controller types
2. How to create I/O controller instances in the PCIe FPGA driver.

### 5.1 Auxiliary Bus

The Auxiliary Bus framework is used to manage the FPGA/CPLD IO controllers in
the FBOSS environment. MFD is not an option because MFD doesn’t support the
removal of individual I/O controllers.

### 5.2 Subdevice (I/O Controller) Creation & Deletion

* In FBOSS systems device creation and deletion is managed from userspace,
  specifically the PlatformManager service.
* Device topology and settings are managed from userspace as well
* When PCIe FPGA is enumerated, the probe function shall only intialize global
  resources
  * e.g. Enabling device, mapping memory, etc.
* The PCIe driver shall not create any subdevices
* The PCIe driver registers a character device interface which allows
  PlatformManager to trigger device creation/deletion from userspace.

#### 5.2.1 Character Device Interface

* The PCIe FPGA driver must register a character device for each FPGA instance
  in the system, and the character device name must match the format:
  `fbiob_%04x.%04x.%04x.%04x% (vendor, device, subsystem_vendor, subsystem_device)`
  For example:

`/dev/fbiob_1d9b.0011.10ee.0007`

* The character device must support 2 ioctl requests:
  * `FBIOB_IOC_NEW_DEVICE` for creating FPGA IO Controllers
  * `FBIOB_IOC_DEL_DEVICE` for deleting IO controllers.
* Please refer to below header file for details:

[fbiob-ioctl.h](https://github.com/meta/fboss/blob/main/fboss/platform/platform_manager/uapi/fbiob-ioctl.h)

#### 5.2.2 Sysfs Interface

It is nice (not mandatory) to have a set of sysfs files created by the PCIe FPGA
driver to achieve the similar goals described in section
[5.2.1](#521-character-device-interface), just because it’s easier for people to
manipulate the files manually (during NPI bringup).

Below is the list of sysfs files. All the files need to be created under
`/sys/bus/pci/devices/<fpga_sbdf>/` and similarly, users need to write `<name>`,
`<instance_id>`, `<csr_offset>` and `<buf_offset>` to the files to trigger
device creation/deletion.

* `new_device`
* `delete_device`

### 5.3 Supported FPGA I/O Controllers

Below is the list of FPGA I/O Controllers supported by the FBOSS
PlatformManager. If you need additional I/O Controller types, please reach out
to the FBOSS team for approval.

#### 5.3.1 FPGA Information (fpga_info)

* An `fpga_info` driver instance shall be created for each FPGA (including
  satellite FPGAs)
  * For example given one IOB FPGA and two satellite DOM FPGAs, the following
    instances will be created:
    * `fpga_info_iob.0`
    * `fpga_info_dom.0`
    * `fpga_info_dom.1`

* The `fpga_info` driver must match devices with `fpga_info_${FPGA_NAME}` naming
style. Below is an example:

```c
static const struct auxiliary_device_id fpga_info_id_table[] = {
        { .name = “fbiob_pci.fpga_info_iob", },
        { .name = “fbiob_pci.fpga_info_dom", },
        {},
};

MODULE_DEVICE_TABLE(auxiliary, fpga_info_id_table);
```

* The `fpga_info` driver shall export the following sysfs files to userspace:
  * `fw_ver`
    * **Description**: This file reports the FPGA's firmware version in the format
      `"%u.%u\n", major_ver, minor_ver`
    * **Type**: unsigned integer
    * **Read/Write**: RO
  * `fpga_ver`
    * **Description**: FPGA's major firmware version
    * **Type**: unsigned integer
    * **Read/Write**: RO
  * `fpga_sub_ver`
    * **Description**: FPGA's minor firmware version
    * **Type**: unsigned integer
    * **Read/Write**: RO

#### 5.3.2 I2C Controller (i2c_master)

The FPGA I2C Controller driver must match devices with the following naming style:
`[<device>_]i2c_master[_<TYPE>]`, where both `<device>_` and `_<TYPE>` are optional.
For example:

```c
static const struct auxiliary_device_id i2c_master_id_table[] = {

        { .name = "fbiob_pci.i2c_master", },
        { .name = "fbiob_pci.i2c_master_ext", },
        { .name = "fbiob_pci.iob_i2c_master", },
        {},
};

MODULE_DEVICE_TABLE(auxiliary, i2c_master_id_table);
```

Please refer to [Section 6, “I2C/SMBus Adapter Drivers”](#6-i2csmbus-adapter-drivers)
for more details about the driver implementation.

#### 5.3.3 SPI Controller (spi_master)

The FPGA SPI Controller driver must match devices with `spi_master` or
`spi_master_${TYPE}` naming style. For example:

```c
static const struct auxiliary_device_id spi_master_id_table[] = {

        { .name = “fbiob_pci.spi_master", },

        {},

};

MODULE_DEVICE_TABLE(auxiliary, spi_master_id_table);
```

Please refer to [Section 12, “SPI Master Drivers”](#12-spi-master-drivers) for
more details about the driver implementation.

#### 5.3.4 GPIO Controller (gpiochip)

The FPGA GPIO Controller driver must match devices with `gpiochip` or
`gpiochip_${TYPE}` naming style. For example:

```c
static const struct auxiliary_device_id gpiochip_id_table[] = {

        { .name = “fbiob_pci.gpiochip", },

        {},

};

MODULE_DEVICE_TABLE(auxiliary, gpiochip_id_table);
```

Please refer to [Section 7, “GPIO Controller Drivers”](#7-gpio-controller-drivers)
for more details about the driver implementation.

#### 5.3.5 Fan/PWM Controller (fan_ctrl)

If the FAN/PWM is managed by an FPGA, the FBOSS PlatformManager would create
`fan_ctrl` auxiliary device instance with proper register offset and number of
fans.

Please refer to [Section 9, “Hardware Monitoring Device Drivers”](#9-hardware-monitoring-device-drivers)
for the driver implementation details.

#### 5.3.6 Fan Watchdog (watchdog_fan)

If the Fan Watchdog is managed by an FPGA, the FBOSS PlatformManager would
create `watchdog_fan` auxiliary device instance with proper register offset.

Please refer to [Section 8, “Watchdog Driver”](#8-watchdog-drivers) for the
driver implementation details.

#### 5.3.7 Transceiver Controller (xcvr_ctrl)

* An instance of `xcvr_ctrl` driver will be created for each transceiver port.
* Port number and register offset are provided when creating the auxiliary
  device.
* The `xcvr_ctrl` driver exports following sysfs entries for each transceiver
  port (port_num is 1-based integer):
  * `“xcvr_reset_%d” % port_num`
  * `“xcvr_low_power_%d” % port_num`
  * `“xcvr_present_%d” % port_num`

#### 5.3.8 LED (led)

* PlatformManager will create “led” auxiliary device instances for each
  “physical” LED.
* Supported LED names the LED drivers need to match (in the auxiliary_device_id
  table):
  * `port_led`
  * `sys_led`
  * `fan_led`
  * `psu_led`
  * `smb_led`

Please refer to [Section 11, “LED Drivers”](#11-led-drivers) for driver
implementation details.

#### 5.3.9 MDIO Controller (mdio_controller)

TBD (not actively used by 2023).

## 6. I2C/SMBus Adapter Drivers

An I2C Adapter refers to the hardware component which initiates I2C transactions,
and it’s also called I2C Master in some contexts. An I2C Client is a chip that
responds to communications when addressed by the adapter, and sometimes it’s
also called I2C slave or I2C target.

SMBus is based on the I2C protocol, and is mostly a subset of I2C protocols and
signaling. Many I2C client devices work properly in the SMBus environment (in
FBOSS), thus I2C and SMBus can be interchanged in this document.

### 6.1 I2C Adapter Driver Development

* An I2C Adapter must be registered by calling `devm_i2c_add_adapter()` or
  `i2c_add_numbered_adapter()` APIs declared in `<inux/i2c.h>`
* Notes regarding I2C adapter registration:
  * If I2C bus numbers are dynamically assigned, I2C adapters’ names must be
    “global-unique” so user space programs can identify I2C adapters easily.
    * For example: “FPGA 0000:07:01.0 I2C Adapter #1 Channel #2”
  * If there is internal multiplexing in the I2C adapter (for example, a
    physical adapter with 4 channels), then each channel/port shall be
    registered as a (virtual) adapter and channel access shall be synchronized
    in the driver. For example, `drivers/i2c/busses/i2c-eg20t.c.`

### 6.2 ABIs for User Space

Below are the list of I2C sysfs and character device interfaces used by FBOSS
user space services:

* `/sys/bus/i2c/drivers/`
* `/sys/bus/i2c/devices/i2c-#/`
* `/sys/bus/i2c/devices/<bus>-00<addr>/`
* `/dev/i2c-#`

PlatformManager creates mappings between `/run/devmap/i2c-busses/BUS_NAME` and
the corresponding character device `/dev/i2c-#`. For example:

* `/run/devmap/i2c-busses/OPTICS_1` → `/dev/i2c-5`

Below page contains instructions for issuing I2C I/O from user space. This is
only intended to be used by I2C client devices without kernel drivers.

* [kernel.org/doc/Documentation/i2c/dev-interface](https://www.kernel.org/doc/Documentation/i2c/dev-interface)

### 6.3 Test Requirement

<TestCase id="I2C.I2CAdapterNames">

* `i2cdetect -l` can list all the I2C buses, and each bus is assigned a
global-unique and descriptive name.

</TestCase>

<TestCase id="I2C.I2CAdapterDevicesExist">

* If the BSP contains multiple I2C adapter drivers, then for each I2C adapter
driver:
* Pick up a I2C bus randomly, make sure `i2cdetect -y <bus_num>` can detect all
the devices that are present in the bus.

  >NOTE: if a certain device is supposed to be present but cannot be detected
  by `i2cdetect -y`, please try `i2cdetect -r` and `i2cdetect -q`: test can
  be marked pass if `-r` or `-q` options works.

</TestCase>

<TestCase id="I2C.I2CTransactions">

* Pick up a I2C client device which is physically present (for example, QSFP
EEPROM), and run below testing:
  * make sure `i2cdump -y -f <bus_num> <dev_addr>` prints device registers
    correctly.
  * make sure `i2cget -y -f <bus_num> <dev_addr> <reg_addr>` reads back the
    device register correctly.
  * make sure `i2cset -y -f <bus_num> <dev_addr> <reg_addr> <data>` sets the
    value to the corresponding device register correctly.

</TestCase>

<TestCase id="I2C.I2CBusWithDevicesCanBeUnloaded">

* Pick up an I2C bus, create a few I2C client devices on the bus, and then
unload the I2C bus driver: make sure the module can be unloaded cleanly.

</TestCase>

<TestCase id="I2C.I2CAdapterCreatesBusses">

* If a physical I2C adapter has multiple internal channels, and each channel is
assigned a I2C bus, then:
* Pick up at least 2 I2C buses from the same I2C adapter, and launch `i2cdump`/
`i2cset`/`i2cget` simultaneously: make sure all the I2C transactions produce
expected results.
* Trigger I2C transactions in a tight loop for 1000 times: no kernel panic, and
no kernel log spew.

</TestCase>

### 6.4 References

* [kernel.org/doc/Documentation/i2c/](https://www.kernel.org/doc/Documentation/i2c/)
* [nxp.com/docs/en/user-guide/UM10204.pdf](https://www.nxp.com/docs/en/user-guide/UM10204.pdf)

## 7. GPIO Controller Drivers

A GPIO Controller, sometimes called GPIO Chip, is the hardware which handles
multiple GPIO lines (sometimes called GPIO Pins). A GPIO Controller is described
by `struct gpio_chip` in the Linux kernel.

### 7.1 GPIO Controller Driver Development

GPIO Controllers must be registered to the Linux GPIO subsystem by
`devm_gpiochip_add_data()` API defined in `<linux/gpio/driver.h>`. For more
information, please refer to:

* [driver.txt](https://www.kernel.org/doc/Documentation/gpio/driver.txt)

### 7.2 ABIs for User Space

`CONFIG_GPIO_SYSFS` is disabled in FBOSS, and all the FBOSS user space services
access GPIOs via the new character device interface (`/dev/gpiochip#`).

A GPIO Line is identified by `<gpiochip, offset>` pair: `gpiochip` is a symlink
located under `/run/devmap/gpiochips/`, and offset is a non-negative offset
within the corresponding `gpiochip`.

### 7.3 Test Requirements

<TestCase id="GPIO.GpioCreated">

* Make sure `gpiodetect` can print all the GPIO Controllers, their names and
number of supported GPIO lines correctly.

</TestCase>

<TestCase id="GPIO.GpioInfo">

* Make sure `gpioinfo` can print all the GPIO lines of all the GPIO controllers
correctly.

</TestCase>

<TestCase id="GPIO.GpioGet">

* Make sure `gpioset` can be used to set value of output GPIO lines.
* Make sure `gpioget` can be used to read value of input GPIO lines.

</TestCase>

<TestCase id="GPIO.DriverUnload">

* Make sure all the GPIO controller driver modules can be unloaded cleanly and
reloaded to the kernel successfully.

</TestCase>

### 7.4 References

* [kernel.org/doc/html/latest/driver-api/gpio/](https://www.kernel.org/doc/html/latest/driver-api/gpio/)
* [git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/about/](https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/about/)

## 8. Watchdog Drivers

A Watchdog Timer (WDT) is a hardware circuit that can reset the computer system
in case of a software fault. Watchdog Timer is usually an FPGA's IP Block in the
FBOSS environment.

### 8.1 Watchdog Driver Development

Watchdog devices must be registered by calling `devm_watchdog_register_device()`
API defined in `<linux/watchdog.h>`.

FBOSS Watchdog devices must support `start`, `stop`, `ping` and `set_timeout`
operations.

**Desired to have features:**

* adding `timeout_sec` module parameter to configure default watchdog timeout at
module load time. This feature is required by the provisioning team and it’s
quite useful at the bringup stage.
* Turning on Magic Close feature.

### 8.2 ABIs for User Space

FBOSS adopts the standard Linux watchdog daemon to feed watchdog via character
device interface `/dev/watchdog#`, and the watchdog daemon can be installed by
`dnf install watchdog`.

watchdog-device is set to `/run/devmap/watchdogs/FBOSS_WDT` in
`/etc/watchdog.conf`, thus a symbolic link must be created
(`/run/devmap/watchdogs/FBOSS_WDT` → `/dev/watchdog#`) to make sure the correct
watchdog is referenced.

### 8.3 Test Requirements

<TestCase id="WATCHDOG.WatchdogStart">

* Start watchdog daemon by `systemctl start watchdog`: make sure system doesn't
reboot in 2 hours.
* Start watchdog daemon and run `echo 1 > /run/devmap/watchdogs/FBOSS_WDT`:
command must fail with EBUSY.

</TestCase>

<TestCase id="WATCHDOG.WatchdogSetTimeout">

* Write a program to update watchdog timeout and make sure watchdog timeout is
updated properly.

</TestCase>

<TestCase id="WATCHDOG.WatchdogMagicClose">

* Stop watchdog daemon and run `echo V > /run/devmap/watchdogs/FBOSS_WDT`:
watchdog is stopped at file close.

</TestCase>

<TestCase id="WATCHDOG.WatchdogDriverUnload">

* Unloading the driver removes the instances of `/dev/watchdog#`

</TestCase>

### 8.4 References

* [kernel.org/doc/html/latest/watchdog/watchdog-api.html](https://www.kernel.org/doc/html/latest/watchdog/watchdog-api.html)
* [watchdog.conf](https://linux.die.net/man/5/watchdog.conf)

## 9. Hardware Monitoring Device Drivers

Typical hardware monitoring devices in FBOSS include temperature, voltage,
current and power sensors, PWM and Fan, etc. These device drivers should be
developed under the Linux hardware monitoring framework (also referred to as
`hwmon` subsystem).

### 9.1 HWMON Driver Development

hwmon devices must be registered by calling `devm_hwmon_device_register_with_info()`
API. Please refer to below link for details:

* [kernel.org/doc/html/latest/hwmon/hwmon-kernel-api.html](https://www.kernel.org/doc/html/latest/hwmon/hwmon-kernel-api.html)

Note:

* A sensor chip must register a single instance of `hwmon`: all the sensor
channels (fan, pwm, temp, in, etc) must be registered by a single call to the
`devm_hwmon_device_register_with_info()` API.
* Please DO NOT use the deprecated `hwmon_device_register()` API.

### 9.2 ABIs for User Space

FBOSS services access hwmon devices via `/sys/class/hwmon/hwmon#`, and udev
rules are needed to create mappings for the hwmon devices.

hwmon drivers must report values to user space with proper units, and below link
defines all the details:

* [kernel.org/doc/Documentation/hwmon/sysfs-interface](https://www.kernel.org/doc/Documentation/hwmon/sysfs-interface)

### 9.3 Test Requirements

<TestCase id="HWMON.HwmonSensors">

* `sensors` command can detect and print all the hwmon sensors.

</TestCase>

## 10. PMBus Drivers

The Linux PMBus driver API is built on top of Linux hwmon subsystem, but it’s
discussed separately because different APIs shall be used to build PMBus drivers.

NOTE: if the generic `pmbus` driver is compatible with the device, an additional
PMBus driver is not needed.

### 10.1 PMBus Driver Development

In general, please leverage the pmbus_core framework as much as possible.
Ideally the PMBus driver would contain a `pmbus_driver_info` structure and a
`pmbus_do_probe()` call, and rest is handled by `pmbus_core`.

For more information, please refer to:

* [kernel.org/doc/html/latest/hwmon/pmbus-core.html](https://www.kernel.org/doc/html/latest/hwmon/pmbus-core.html)

### 10.2 ABIs for User Space

Same with the [“Hardware Monitoring Device Drivers”](#9-hardware-monitoring-device-drivers)
section.

### 10.3 Test Requirements

Same with the [“Hardware Monitoring Device Drivers”](#9-hardware-monitoring-device-drivers)
section.

## 11. LED Drivers

### 11.1 LED Driver Development

LED devices must be registered by calling `devm_led_classdev_register()` API.
For more details, please refer to:

* [kernel.org/doc/html/latest/leds/leds-class.html](https://www.kernel.org/doc/html/latest/leds/leds-class.html)

LED Naming Conventions:

* Any index (port num, fan num, led id) must be a 1-based integer.

* **Port LEDs:**
  * The LED sysfs entries must be `port%d_led%d:<color>:status`.
    E.g.: `port1_led1:blue:status`, `port32_led2:amber:status`
  * Device name must match `port_led`
* **Fan LEDs:**
  * The LED sysfs entry must be `fan%d_led:<color>:status`. For example,
    `fan1_led:yellow:status`
  * General fan status LED should be named `fan_led:<color>:status` if it
    exists
  * Device name must match `fan_led`
* **PSU LEDs:**
  * The LED sysfs entry must be `psu%d_led:<color>:status`. E.g.:
    `psu1_led:yellow:status`
  * General psu status LED should be named `psu_led:<color>:status` if it
    exists
  * Device name must match `psu_led`
* **System LED:**
  * General sys status LED should be named `sys_led:<color>:status` if it
    exists
  * Device name must match `sys_led`
* **SMB LED:**
  * General sys status LED should be named `smb_led:<color>:status` if it
    exists
  * Device name must match `smb_led`

Notes:

* All the LED entries must follow `<devicename>:<color>:<function>` format
* Please make use of the pre-defined LED “colors” and “functions” defined in
below header file. If you need additional types which are not defined in the
header file, please reach out to Meta for support.

  [github.com/torvalds/linux/blob/master/include/dt-bindings/leds/common.h](https://github.com/torvalds/linux/blob/master/include/dt-bindings/leds/common.h)

### 11.2 Test Requirements

<TestCase id="LED.LedsCreated">

* Make sure all the required LEDs and LED colors are found at `/sys/class/leds`.

</TestCase>

<TestCase id="LED.DeviceLedsCreated">

* Write `0` to LEDs' `brightness` file: make sure the color is turned off.
* Write `max_brightness` to LEDs' `brightness` file: make sure the color is
turned on.

</TestCase>

## 12. SPI Master Drivers

A typical SPI bus contains a SPI Master which originates I/O transactions, and
one or more SPI slaves selected by CS (chip select) pins. CS is also referred to
as SS (slave select) in some documents.

Typical SPI slaves in the FBOSS environment include SPI flashes, EEPROMs and TPM.

### 12.1 SPI Master Driver Development

SPI Masters must be registered to the Linux SPI framework by calling
`devm_spi_register_master()` API. For more details, please refer to:

* [kernel.org/doc/html/latest/spi/spi-summary.html](https://www.kernel.org/doc/html/latest/spi/spi-summary.html)

### 12.2 ABIs for User Space

FBOSS user space services don’t access SPI busses directly, instead they operate
on SPI slaves.

* **SPI Flashes**
* **SPI EEPROMs**
  * Flashes and EEPROM devices must be managed by kernel drivers, and user space
  programs access devices through the client driver interfaces.
* **SPIDEV**
  * Such devices are accessed from user space via the character device
  `/dev/spidev[bus].[cs]`. Refer to below URL for details:

    [kernel.org/doc/Documentation/spi/spidev.rst](https://www.kernel.org/doc/Documentation/spi/spidev.rst)

### 12.3 Test Requirements

* TBD

## 13. MDIO Controller Drivers

MDIO Controllers are used to access PHY devices in the FBOSS environment, and
it’s mapped to `mii_bus` structure in the Linux kernel.

### 13.1 MDIO Controller Driver Development

The `mii_bus` structure is allocated by `devm_mdiobus_alloc[_size]` API, and
MDIO bus needs to be registered by calling `devm_mdiobus_register()` API.

For details, please refer to:

* [include/linux/phy.h](https://github.com/torvalds/linux/blob/master/include/linux/phy.h)

### 13.2 ABIs for User Space

PHY devices can be accessed through the character device interface exported by
the corresponding MDIO controller. udev rules are needed to map between
`/run/devmap/mdio-busses/BUS_NAME` and MDIO controller’s character device
(`/dev/mdio/mdio#`).

The MDIO controller character device supports at least 2 ioctl requests:

* C45_READ

    [fboss/mdio/Mdio.h#L60](https://github.com/meta/fboss/blob/main/fboss/mdio/Mdio.h#L60)

* C45_WRITE

    [fboss/mdio/Mdio.h#L65](https://github.com/meta/fboss/blob/main/fboss/mdio/Mdio.h#L65)

### 13.3 Test Requirements

* TBD
