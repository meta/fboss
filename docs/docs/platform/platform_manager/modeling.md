---
id: modeling
title: Modeling Requirements
sidebar_label: Modeling Requirements
sidebar_position: 2
---

# Modeling Requirements

This document describes the hardware modeling requirements for PlatformManager
configuration.

## PMUnit

Unit used for modeling in PlatformManager. It typically matches with a FRU, but
not always. PMUnit and FRU terminologies are used interchangeably in this
document. PMUnit and FRU differences will be explicitly called out when they are
not interchangeable.

### What is modeled as PMUnit

- Any unit which has an IDPROM (e.g., PSU, SCM, SMB).
- Any unit which does not have an IDPROM and can be field swapped and does not
  have any devices like I2C, FPGA, CPLD, SPI, EEPROM etc. (e.g., FanTray).

### What is not modeled as PMUnit

- Any unit which does not have an IDPROM and cannot be field swapped.
- If such a unit has no devices (I2C, FPGA, CPLD, etc.), then it does not need
  modeling in PlatformManager.
- If such a unit has devices (I2C, FPGA, CPLD, etc.), then it will be modeled as
  part of another PMUnit (referred to as hosting PMUnit). This is one case where
  one PMUnit could model multiple FRUs together. In this case, when the device
  is re-spun, the IDPROM of the hosting PMUnit MUST be updated to reflect the
  re-spin (e.g. product version/sub-version number change). It should also
  include IDPROM changes of the hosting PMUnit.

![PlatformManager Flowchart](/img/platform/platform_manager/platform_manager_flowchart.jpg)

## EEPROM

It should store content in [Meta EEPROM V6 format](/docs/platform/meta_eeprom_format_v6).

## IDPROM

- Any EEPROM which is used to identify the FRU/PMUnit type is called IDPROM.
- The IDPROM in the FRU MUST be connected in one of the following ways:
  - Directly to an incoming I2C bus from the parent FRU.
  - Directly to the CPU's SMBus I2C Controller.
- The Product Name field in the IDPROM should have the corresponding PMUnit name
  used in the PlatformManager configuration as the value.
- The Product Name should not be cryptic or in code words. The Product Name
  should be obvious about the functionality of the PMUnit. Some examples:
  - Use SCM for a PMUnit containing CPU (not EAGLE or PLATFORM_NAME_SCM)
  - Use SMB for a PMUnit containing the switch ASIC.
  - Use PSU for Power Supply Unit
  - Use FAN for FAN PmUnit
  - PDB, MCB, JUMPER, FCB etc., are other valid names
- Chassis EEPROM is not considered an IDPROM.

## Chassis EEPROM

- Chassis EEPROM will be modeled as just an ordinary EEPROM (within the containing
  PMUnit).
- Chassis EEPROM should be a dedicated EEPROM. It MUST NOT also serve as a
  FRU/PMUnit IDPROM described above.
- The Product Name field of the Chassis EEPROM must be the platform name. It
  must be the same as the output of `dmidecode -s system-product-name`.

## Field-Replaceability

- The unit must be a PMUnit (FRU) for it to be field-replaceable.

## Re-Spin

- Any respin of a PMUnit, should involve update of the IDPROM of that PMUnit.
- Any respin of a non-PMUnit, should be bundled with changes in IDPROM of a
  hosting PMUnit.
- The same platform config will be used for both re-spun and original
  platform. For the PMUnit which has difference across re-spin and original
  platform, the platform config will have two definitions of the PMUnit - one
  each for re-spin and original platform. Hardware Version will be used as a key
  to look up the right PMUnit config.

**Example configuration for re-spin:**

```json
{
  "pmUnitConfigs": {
    "SMB": [
      {
        "productProductionState": 0,
        "productVersion": 1,
        "productSubVersion": 0,
        "i2cDeviceConfigs": { ... }
      },
      {
        "productProductionState": 0,
        "productVersion": 2,
        "productSubVersion": 0,
        "i2cDeviceConfigs": { ... }
      }
    ]
  }
}
```

PlatformManager reads the IDPROM to determine which PMUnit config to use.
- IDPROM content difference for the PMUnit across respins is a combination of
  the below three keys in IDPROM:
  - Product Production State
  - Product Version
  - Product Sub-Version

## PMUnit IDPROM I2C Addressing

PMUnits of the same slot type should have the same address for the IDPROM. For
example, all PIM8DD and PIM16Q PMUnits, which are of slot type PIM, should have
the same address for their IDPROM. This enables these PMUnits to be plugged into
any PIM slot and be discovered by PlatformManager.

## I2C Devices

All I2C devices should be connected in one of the following ways:

- Directly to an incoming I2C bus from a parent PMUnit
- Directly to a MUX or FPGA present within the PMUnit (or containing PMUnit)
- Directly to the CPU's SMBus I2C Controller

## PMUnit Presence

PMUnit presence bits must be managed by the chips present in the parent PMUnit.

### Presence Detection

PlatformManager can detect whether a PmUnit (FRU) is physically present in a slot. Before exploring a slot, it checks if a PmUnit is actually plugged in. This serves two key purposes:

- Prevents errors caused by attempting to access empty slots.
- Enables reporting of which slots are populated, supporting hot-plug detection and proper handling of optional components.

Presence Detection information can be found in [platform_manager_presence.thrift](https://github.com/meta/fboss/blob/main/fboss/platform/platform_manager/platform_manager_presence.thrift) under `PresenceDetection` and `PresenceInfo`.

## Root PmUnit

This is the PmUnit which is used by Platform Manager as the starting node for
Platform exploration. The choice of root PmUnit should ensure all the above
conditions are met.
