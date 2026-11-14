# FBOSS Platform Services Support Requirements

## Revision 0.6.0

This document describes the configurations and software packages that Vendors
must provide to support FBOSS X86 Software development.

## FBOSS Platform Services Overview

### fan_service

`fan_service` reads temperature sensors and adjusts fan speed hardware to
achieve desired thermal requirements.

### sensor_service

`sensor_service` reads various hardware monitoring sensors regularly, and
answers clients’ requests via thrift interface. `fan_service` is one of the
clients, which requests temperature sensor values from `sensor_service`.

### data_corral_service

`data_corral_service` serves as whole chassis information aggregator, and front
panel LED controller based on chassis/FRU health

### platform_manager

`platform_manager` handles creation/deletion/initialization of platform devices.
For example, it creates and initializes I2C devices when a PIM card is plugged
and deletes these I2C devices when the card is removed.

### weutil

`weutil` is a CLI/API software package that provides chassis/FRU information
based on Meta format. It should contain the APIs which can be used by other
platform services.

### rackmon

`rackmon` service provides communication channels between Rack-level PSUs/BBUs
and rack-power management services, and it enables rack-power management
services to monitor power loss events and read/write PSU/BBU registers via
RS485. `rackmon` is only deployed in TOR switches.

## Requirements

* We avoid “platform” specific source code. For each new platform, only
  configuration files are needed. Vendor needs to compile the service and run it
  with the desired configuration (specified below). Vendor should verify the
  service is able to achieve its desired objective. For example,
  `platform_manager` should be able to setup all the devices mentioned in its
  config successfully, sensor_service should be able to read all the sensors
  mentioned in its config, weutil should be able to display the EEPROM
  information of the specified FRU etc.
* Vendor needs to ensure all the tests associated with these services pass. The
  tests are located in the same folder as the service code.

| Platform Service   | Configurations/Requirements                                                                                                                                                                                                 |
|--------------------|------------------------------------------------------------------------------------------------------------------------------------------------|
| fan_service        | [fan_service_config.thrift](https://github.com/meta/fboss/blob/main/fboss/platform/fan_service/if/fan_service_config.thrift)               |
| sensor_service     | [sensor_config.thrift](https://github.com/meta/fboss/blob/main/fboss/platform/sensor_service/if/sensor_config.thrift)                      |
| platform_manager   | [platform_manager_config.thrift](https://github.com/meta/fboss/blob/main/fboss/platform/platform_manager/platform_manager_config.thrift)   |
| weutil             | [weutil_config.thrift](https://github.com/meta/fboss/blob/main/fboss/platform/weutil/if/weutil_config.thrift)                              |
| fw_util            | - [fw_util_config.thrift](https://github.com/meta/fboss/blob/main/fboss/platform/fw_util/if/fw_util_config.thrift) <br/> - For each command/binary used, they must be statically compiled, no other libraries/dependencies <br/> - Must use only SPI driver, I2C driver or JTAG driver <br/> - Must not use vendor specific tools |
| rackmon            | - Three UART devices (usually /dev/ttyUSB*) for modbus communications. Example: [rackmon.conf](https://github.com/meta/fboss/blob/main/fboss/platform/rackmon/configs/interface/rackmon.conf) <br/> - 6 GPIO Lines for Power Loss Siren. Example: [rackmon_pls.conf](https://github.com/meta/fboss/blob/main/fboss/platform/rackmon/configs/interface/rackmon_pls.conf) |
| data_corral_service| [data_corral_service.thrift](https://github.com/meta/fboss/blob/main/fboss/platform/data_corral_service/if/data_corral_service.thrift) |

## Device References

Devices should not be referenced using absolute sysfs/chardev paths. They should
instead be referenced using the symbolic device paths created by
PlatformManager. The symbolic device paths has the following structure:

* **root directory**: All the symlinks are found under the root directory
  `/run/devmap/`.

* **group directories**: The symlinks are categorized by groups. The supported
  groups are: `i2c-busses`, `gpiochips`, `mdio-busses`, `watchdogs`, `sensors`,
  `eeproms`, `spi-devices`, `fpgas`,  `cplds` etc.

* **leaf symlinks**: The leaf symbolic links point to the corresponding device file.
  Leaf symlink files must be assigned meaningful names, containing upper-case
  letters, numbers (0-9) and underscores.

Good Reference:

```bash
/run/devmap/i2c-busses/OPTICS_1
```

Bad Reference:

```bash
/dev/i2c-5
```
