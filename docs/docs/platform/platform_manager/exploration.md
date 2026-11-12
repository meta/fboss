---
id: exploration
title: Exploration Process
sidebar_label: Exploration
sidebar_position: 5
---

# PlatformManager Exploration

Platform exploration is the process of discovering and setting up all devices
on the platform based on the configuration file.

## Exploration Workflow

1. **Load Configuration**: Read platform-specific JSON config
2. **Package Management**: Install BSP RPM and load kernel modules
3. **Discover Root PmUnit**: Start from the root PmUnit (typically SCM)
4. **Recursive Exploration**:
   - Read IDPROM to identify PmUnit type
   - Create PCI sub-devices (from FPGA)
   - Create I2C devices
   - Create symbolic links in `/run/devmap/`
   - Explore child slots recursively
5. **Publish Versions**: Read and publish firmware/hardware versions
6. **Report Status**: Log results and publish ODS counters

## Exploration Status and Errors

Exploration status and errors can be found in [platform_manager_service.thrift](https://github.com/meta/fboss/blob/main/fboss/platform/platform_manager/platform_manager_service.thrift) under `ExplorationStatus` and `ExplorationError`.


## Querying Exploration Results

### Via Thrift API

Other FBOSS services can query exploration results programmatically using the
[Service API](/docs/platform/platform_manager/service_api):

- `getLastPMStatus()` - Returns exploration status and failed devices map
- `getAllPmUnits()` - Returns all discovered PmUnits with their status

### Via Logs

Exploration results are logged at INFO level:

```
Successfully explored <platform_name>...
```

Errors are logged at WARNING or ERROR level with device paths and error
messages.
