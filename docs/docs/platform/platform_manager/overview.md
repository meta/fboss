---
id: overview
title: PlatformManager Overview
sidebar_label: Overview
sidebar_position: 1
slug: /platform/platform_manager
---

# PlatformManager

## Introduction

The objective of PlatformManager is to generalize low level device setup and
management across platforms. This avoids platform specific customized software
logic. All the Platform specific information should be encoded in configuration
files such that the same generic software can run on all platforms.

## Functions of Interest

- **Inventory Management**: A switch system could have different FRUs.
  PlatformManager is responsible for building the inventory of the FRUs in a
  switch. PlatformManager needs to monitor in real time what FRU has been
  removed or inserted into the switch. It provides an interface to software
  components within and outside of PlatformManager about the FRUs and the FRU
  EEPROM contents.
- **Dynamic Device Management**: When an FRU has been detected by the Inventory
  Management, Dynamic Device Management will load the necessary kmods and enable
  communication with the FRU. PlatformManager will also ensure that the FRU
  devices get consistent naming within each platform irrespective of how it is
  plugged in and across reboots.

## Components

- **Platform Specific Configuration (provided by Vendors):** It has information
  about the FRUs, I2C devices and how they can be accessed.

  The configuration format is defined in [platform_manager_config.thrift](https://github.com/meta/fboss/blob/main/fboss/platform/platform_manager/platform_manager_config.thrift).

  See here for a [sample config](https://github.com/meta/fboss/blob/main/fboss/configs/platforms/generic/sample/platform_stack/platform_manager.json) .


- **Platform Agnostic Software (developed by Meta):** Meta will write and maintain the software.


  See here for the [source code](https://github.com/meta/fboss/tree/main/fboss/platform/platform_manager).


## Documentation

| Topic                                                                  | Description                                      |
| ---------------------------------------------------------------------- | ------------------------------------------------ |
| [Modeling Requirements](/docs/platform/platform_manager/modeling)      | PMUnit, IDPROM, and hardware modeling rules      |
| [Block Configurations](/docs/platform/platform_manager/block_configs)  | Bulk FPGA sub-device configuration               |
| [Exploration Process](/docs/platform/platform_manager/exploration)     | How platform discovery works, status and errors  |
| [Device Map](/docs/platform/platform_manager/devmap)                   | Symbolic link structure for stable device paths  |
| [Running PlatformManager](/docs/platform/platform_manager/running)     | Command-line flags, run modes, systemd integration |
| [Package Manager](/docs/platform/platform_manager/package_manager)     | BSP RPM management and kmods loading             |
| [Thrift Service API](/docs/platform/platform_manager/service_api)      | Service endpoints for querying platform state    |

## Workflow

### On Initialization

- Determine Platform Type using dmidecode set by BIOS. For details, refer to
  the [Meta Switch Architecture](/docs/architecture/meta_switch_architecture)
  documentation, which also links to the BMC-Lite specification.
- Load the platform specific PlatformManager Config
- Discover the devices (by polling present registers or reading sysfs paths)
  based on the config and load the necessary kmods.

### Periodically

- Detect devices by listening to present polling/interrupts (by polling for now,
  since interrupts are not full-fledged)
- On device detection, use the PlatformManager Config to set up the device and
  load the necessary kmods.

## Usage

- PlatformManager will be used at provisioning/boot-up and be executed before
  any FRU/EEPROM/FW access.
- PlatformManager will be run in steady state of the switch to facilitate
  dynamic detection of devices.

## Vendor Expectations

- **Configuration**: Vendor will provide the platform specific configuration
  according to the [specification](https://github.com/meta/fboss/blob/main/fboss/platform/platform_manager/platform_manager_config.thrift)
  in fboss repository.
- **Handoff**: Vendor will run PlatformManager as part of the handoff. The running
  and testing instructions will be provided in fboss repository.
- **PCI Device IDs**: All PCI devices (e.g., FPGA) should have unique Vendor ID and
  Device ID in the PCI configuration space.
- **Dmidecode Setup**: The platform type should be obtainable through dmidecode.
  See [Meta Switch Architecture](/docs/architecture/meta_switch_architecture) for
  details.
- **BSP**: The BSP should follow the [BSP requirements specification](/docs/platform/bsp_development_requirements)
  to enable device discovery and setup.
