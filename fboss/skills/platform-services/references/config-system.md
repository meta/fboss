# Platform Services — Configuration System

## Config Directory Layout

Platform configs live in `configs/<platform_name>/` as JSON files. ~27 platforms supported.

## JSON Config Types

| File | Purpose |
|------|---------|
| `platform_manager.json` | Hardware topology (I2C buses, PCI devices, GPIO chips, PmUnits, slots) |
| `sensor_service.json` | Sensor definitions (sysfs paths, thresholds, types) |
| `fan_service.json` | Thermal zones, sensor-to-PWM mappings, PID parameters |
| `fw_util.json` | Firmware device definitions and upgrade paths |
| `led_manager.json` | LED configuration |
| `bsp_tests.json` | BSP test parameters |
| `rma_showtech.json` | Additional utility configs |

## Build-Time Compilation

The `config_lib` compiles JSON configs into C++ at build time via `ConfigGenerator` and `GeneratedConfig.h`. All services depend on it. When adding or modifying platform configs, the config validation test must pass:

```bash
ctest -R ConfigLibTest
```

`gtest_discover_tests` registers CTest names as `Suite.Case`, so filter on the
gtest suite (`ConfigLibTest`), not the CMake target name. `ctest -N` lists the
registered names.

## Thrift Schemas

| File | Defines |
|------|---------|
| `platform_manager/platform_manager_config.thrift` | Core BSP topology: I2C bus naming, SlotPaths, DevicePaths, PmUnit structures |
| `platform_manager/platform_manager_service.thrift` | PlatformManagerService RPCs: `getPlatformSnapshot()`, `getLastPMStatus()`, `getPmUnitInfo()` |
| `sensor_service/if/sensor_service.thrift` | SensorServiceThrift: `getSensorValuesByNames()`, SensorData types |
| `sensor_service/if/sensor_config.thrift` | Sensor config schema (thresholds, types, sysfs paths) |
| `fan_service/if/fan_service.thrift` | FanService RPC: `getFanStatuses()` |
| `fan_service/if/fan_service_config.thrift` | Fan zone config schema (PID params, PWM tables) |
| `rackmon/if/rackmonsvc.thrift` | Rackmon RPCs, Modbus device types (ORV2/ORV3 PSU, BBU, RPU) |

## Shared Infrastructure

- **`config_lib/`** — Embeds all per-platform JSON configs at build time. All services depend on it.
- **`helpers/`** — Shared utilities: `platform_name_lib` (platform detection), `platform_utils` (subprocess helpers), `platform_fs_utils` (filesystem ops), `init`/`init_cli` (fb303 initialization).

## OSS vs Internal Split

Several services have `facebook/` and `oss/` subdirectories with alternate
implementations of the same interface. Open-source builds compile the `oss/`
variants; the `facebook/` ones are not present. When reading a service and a
symbol seems to be missing, check whether it lives in an `oss/` file.

## Build Definitions

Each component has a CMake file at `fboss/github/cmake/Platform*.cmake` — for
example `PlatformPlatformManager.cmake`, `PlatformFanService.cmake`,
`PlatformSensorService.cmake`. Adding a source file or a dependency means
editing the corresponding file there.
