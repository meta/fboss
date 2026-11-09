---
description: FBOSS platform services - BMC-Lite stack (platform_manager, sensor_service, fan_service, data_corral_service, rackmon), per-platform JSON configs, CLI utilities (fw_util, weutil, fixmyfboss), hardware tests, BSP kernel modules
---

# FBOSS Platform Services

## Overview

Platform services = BMC-Lite software stack for FBOSS switches. Below the agent, above raw hardware.

**Startup order**: platform_manager -> sensor_service -> fan_service, data_corral_service; rackmon independent.

| Service | Purpose |
|---------|---------|
| `platform_manager` | Hardware topology discovery (I2C/PCI/GPIO), creates sysfs devices |
| `sensor_service` | Reads sensors, publishes to FSDB + Thrift |
| `fan_service` | Zone-based thermal control (PID or table-driven), subscribes to sensor_service |
| `data_corral_service` | LED management, FRU presence, EEPROM data |
| `rackmon` | Rack power monitoring over RS485/Modbus (PSU, BBU, RPU) |

## When to Load References

For each pair below, load the `facebook/` version first if it exists in your
checkout. Otherwise load the `references/` version. Build tooling differs
between environments; everything else is the same.

| Topic | Try first | Fallback |
|-------|-----------|----------|
| Build targets (services, CLI tools, unit and hardware tests) | `facebook/build-and-test.md` | `references/build-and-test.md` |
| Config system (JSON types, Thrift schemas, config_lib, per-platform configs), oss/facebook source split | — | `references/config-system.md` |
| Testing patterns (mocks, `-D__TEST__`, BSP tests, version tests, hardware test binaries) | — | `references/testing-patterns.md` |
