# FBOSS BSP Kernel Module API Specification

import TestCase from '@site/src/components/TestCase';

## Version 1.0.0

- [FBOSS BSP Kernel Module API Specification](#fboss-bsp-kernel-module-api-specification)
  - [Version 1.0.0](#version-100)
- [1. Device References](#1-device-references)
- [2. PCIe Character Device Interface](#2-pcie-character-device-interface)
  - [2.1 Subdevice (I/O Controller) Creation \& Deletion](#21-subdevice-io-controller-creation--deletion)
  - [2.2 Supported Controller Interfaces](#22-supported-controller-interfaces)
    - [2.2.1 FPGA Information (fpga\_info)](#221-fpga-information-fpga_info)
    - [2.2.2 I2C Controller (i2c\_master)](#222-i2c-controller-i2c_master)
    - [2.2.3 SPI Controller (spi\_master)](#223-spi-controller-spi_master)
    - [2.2.4 GPIO Controller (gpiochip)](#224-gpio-controller-gpiochip)
    - [2.2.5 HWMON and PMBUS Device](#225-hwmon-and-pmbus-device)
    - [2.2.6 Fan Controller (fan\_ctrl)](#226-fan-controller-fan_ctrl)
    - [2.2.7 Fan Watchdog (watchdog\_fan)](#227-fan-watchdog-watchdog_fan)
    - [2.2.8 Transceiver Controller (xcvr\_ctrl)](#228-transceiver-controller-xcvr_ctrl)
    - [2.2.9 LED](#229-led)

:::tip
<TestCase id="Note">
This indicates that this statement is covered by a BSP Tests test case. Hover to
see the test case id.
</TestCase>
:::

## 1. Device References

Any interaction between userspace and devices shall use the device interfaces
created by the FBOSS PlatformManager service.

## 2. PCIe Character Device Interface

Most I/O (I2C, SPI, etc) Controllers are provided by the PCIe FPGA in the FBOSS
environment.

### 2.1 Subdevice (I/O Controller) Creation & Deletion

Device topology/settings are managed from userspace with the PlatformManager
service.

<TestCase id="CDEV.CdevIsCreated">
The PCIe FPGA driver registers a character device for each FPGA instance in the
system with the following naming pattern:

`fbiob_%04x.%04x.%04x.%04x (vendor, device, subsystem_vendor, subsystem_device)`

</TestCase>

<TestCase id="CDEV.CdevCreateAndDelete">
This character device supports the following IOCTL commands:

- `FBIOB_IOC_NEW_DEVICE`
- `FBIOB_IOC_DEL_DEVICE`

</TestCase>

More details can be found in the [fbiob-ioctl.h](https://github.com/meta/fboss/blob/main/fboss/platform/platform_manager/uapi/fbiob-ioctl.h)
header file.

### 2.2 Supported Controller Interfaces

#### 2.2.1 FPGA Information (fpga_info)

**Interface:**

The FPGA Info driver exports the following sysfs files:

- `fw_ver`
  - **Description**: This file reports the FPGA's firmware version in the format
    `"%u.%u\n", major_ver, minor_ver`
  - **Type**: unsigned integer
  - **Read/Write**: RO
- `fpga_ver`
  - **Description**: FPGA's major firmware version
  - **Type**: unsigned integer
  - **Read/Write**: RO
- `fpga_sub_ver`
  - **Description**: FPGA's minor firmware version
  - **Type**: unsigned integer
  - **Read/Write**: RO

#### 2.2.2 I2C Controller (i2c_master)

**Interface:**

<TestCase id="I2C.I2CAdapterCreatesBusses">

Below are the list of I2C sysfs and character device interfaces used by FBOSS
user space services:

- `/sys/bus/i2c/drivers/`
- `/sys/bus/i2c/devices/i2c-#/`
- `/sys/bus/i2c/devices/<bus>-00<addr>/`
- `/dev/i2c-#`

</TestCase>

<TestCase id="I2C.I2CAdapterNames">

PlatformManager creates mappings between `/run/devmap/i2c-busses/BUS_NAME` and
the corresponding character device `/dev/i2c-#`. For example:

- `/run/devmap/i2c-busses/OPTICS_1` → `/dev/i2c-5`

</TestCase>

<TestCase id="I2C.I2CAdapterDevicesExist">

Once an i2c adapter has been created, devices are instantiated by using
userspace API as described here:

- [docs.kernel.org/i2c/instantiating-devices](https://docs.kernel.org/i2c/instantiating-devices.html#method-4-instantiate-from-user-space)

Example:

    # echo eeprom 0x50 > /sys/bus/i2c/devices/i2c-3/new_device

</TestCase>

<TestCase id="I2C.I2CTransactions">

If devices attached to this adapter are not managed by a kernel driver it is
possible to interact with them directly by using `ioctl` commands. See the below
document for a full explanation.

- [kernel.org/doc/Documentation/i2c/dev-interface](https://www.kernel.org/doc/Documentation/i2c/dev-interface)

</TestCase>

#### 2.2.3 SPI Controller (spi_master)

- **Interface:**
  - FBOSS user space services don’t access SPI busses directly, instead they operate
    on SPI clients. API specification is described in the following document.
    [kernel.org/doc/html/latest/spi/spi-summary](https://www.kernel.org/doc/html/latest/spi/spi-summary.html#how-do-these-driver-programming-interfaces-work)
- **SPI EEPROMs**
  - Flashes and EEPROM devices must be managed by kernel drivers, and user space
    programs access devices through their client driver interfaces. Please see the
    documentation for the eeprom driver interface definition.
- **SPIDEV**
  - Such devices are accessed from user space via the character device
    `/dev/spidev[bus].[cs]`. Refer to below URL for details:

    [kernel.org/doc/Documentation/spi/spidev.rst](https://www.kernel.org/doc/Documentation/spi/spidev.rst)

#### 2.2.4 GPIO Controller (gpiochip)

**Interface:**

<TestCase id="GPIO.GpioCreated">

All FBOSS user space services access GPIOs via the character device interface at
`/dev/gpiochip#`. Full description can be found here.
[https://docs.kernel.org/userspace-api/gpio/chardev.html](https://docs.kernel.org/userspace-api/gpio/chardev.html)

</TestCase>

<TestCase id="GPIO.GpioInfo">

A GPIO Line is identified by `<gpiochip, offset>` pair: `gpiochip` is a symlink
located under `/run/devmap/gpiochips/`, and offset is a non-negative offset
within the corresponding `gpiochip`.

</TestCase>

#### 2.2.5 HWMON and PMBUS Device

Typical hardware monitoring devices in FBOSS include temperature, voltage,
current and power sensors, PWM and Fan, etc. These device drivers are
developed under the Linux hardware monitoring framework.

**Interface:**

<TestCase id="HWMON.HwmonSensors">

FBOSS services access hwmon devices via `/sys/class/hwmon/hwmon#`.

hwmon drivers must report values to user space with proper units, and below link
defines all the details:

- [kernel.org/doc/Documentation/hwmon/sysfs-interface](https://www.kernel.org/doc/Documentation/hwmon/sysfs-interface)

</TestCase>

#### 2.2.6 Fan Controller (fan_ctrl)

**Interface:**

Fan control devices shall be implemented as hwmon devices and as such will
follow the hwmon sysfs interface generally.

**Behavior:**

In cases where a fan is not present, all writes and reads to/from that fan must
fail. For example, reading the RPM from a fan which is not present should not
return 0 or some other "bad" value, but rather the operation must return an
error code.

#### 2.2.7 Fan Watchdog (watchdog_fan)

**Interface:**

<TestCase id="WATCHDOG.WatchdogStart">

FBOSS adopts the standard Linux watchdog daemon to feed watchdog via character
device interface `/dev/watchdog#`. The watchdog API is defined here.
[kernel.org/doc/html/v6.4/watchdog/watchdog-api](https://www.kernel.org/doc/html/v6.4/watchdog/watchdog-api.html)

</TestCase>

<TestCase id="WATCHDOG.WatchdogPing">

FBOSS Watchdog devices support `start`, `stop`, `ping` and `set_timeout`
operations.

</TestCase>

**Behavior:**

When the watchdog expires and is subsequently kicked, the watchdog shall rearm
itself. This behavior ensures that the watchdog remains active and continues
to monitor system status after an expiration event and subsequent service recovery.

#### 2.2.8 Transceiver Controller (xcvr_ctrl)

**Interface:**

<TestCase id="XCVR.XcvrCreatesSysfsFiles">

The `xcvr_ctrl` driver exports following sysfs entries for each transceiver port
(port_num is 1-based integer):

- `xcvr_reset_<portnum>`
  - **Type**: Bit
  - **Description**: Setting to 1 puts the optics in reset state, 0 takes it out
    of reset.
  - **Read/Write**: RW
- `xcvr_low_power_<portnum>`
  - **Type**: Bit
  - **Description**: 1 sets the xcvr to low power mode.
  - **Read/Write**: RW
- `xcvr_present_<portnum>`
  - **Type**: Bit
  - **Description**: 1 indicates xcvr is present, 0 indicates not present.
  - **Read/Write**: RO

</TestCase>

#### 2.2.9 LED

LEDs are accessed via their original sysfs paths since they are non-dynamic. No
symlinks are created by PlatformManager. All leds are found at `/sys/class/leds/`

**Naming:**

<TestCase id="LED.LedsCreated">

LEDs are named with a common scheme:

    <type><id>_led<ledid>:<color>:status

For example:

    port10_led1:blue:status
    port1_led2:amber:status
    sys_led:amber:status

Port LEDs always include both a port number and a LED number (1-based).
System-level LEDs (e.g. `sys_led`, `fan_led`) have no numeric id in the name.

</TestCase>

**Interface:**

<TestCase id="LED.DeviceLedsCreated">

- FBOSS services control LEDs by writing `0` or `max_brightness` to
  `/sys/class/leds/<LED_NAME>/brightness` files.
- If a physical LED is separated into multiple logical LEDs (entries in
  `/sys/class/leds/`), then writing non-zero to any color will turn off
  the other colors.
  - This behavior must be reflected in the `brightness` file. For example,
    ```bash
    $ echo 1 > /sys/class/leds/port1_led1:blue:status/brightness
    $ cat /sys/class/leds/port1_led1:blue:status/brightness
    1
    $ echo 1 > /sys/class/leds/port1_led1:amber:status/brightness
    $ cat /sys/class/leds/port1_led1:blue:status/brightness
    0
    ```

</TestCase>

**Blinking:**

- FBOSS services blink LEDs using the kernel `timer` LED trigger. Each LED
  must accept `timer` and `none` written to
  `/sys/class/leds/<LED_NAME>/trigger`.
- When the `timer` trigger is active, the LED must expose `delay_on` and
  `delay_off` files (in milliseconds) under `/sys/class/leds/<LED_NAME>/`.
  FBOSS uses two blink rates, with equal on and off times:
  - Fast blink: `500` ms
  - Slow blink: `2000` ms
- To start blinking, FBOSS writes `timer` to `trigger`, the blink rate to
  `delay_on` and `delay_off`, and then `max_brightness` to `brightness`. To
  stop blinking, FBOSS writes `none` to `trigger`.
  - The configured blink rate must be reflected in the `delay_on` and
    `delay_off` files. For example,
    ```bash
    $ echo timer > /sys/class/leds/port1_led1:blue:status/trigger
    $ echo 500 > /sys/class/leds/port1_led1:blue:status/delay_on
    $ echo 500 > /sys/class/leds/port1_led1:blue:status/delay_off
    $ echo 1 > /sys/class/leds/port1_led1:blue:status/brightness
    $ cat /sys/class/leds/port1_led1:blue:status/delay_on
    500
    $ cat /sys/class/leds/port1_led1:blue:status/delay_off
    500
    ```
