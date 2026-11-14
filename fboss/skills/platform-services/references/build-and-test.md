# Platform Services — Build and Test Targets

Platform services build with CMake inside the FBOSS container. See the build
documentation at https://meta.github.io/fboss/ for container setup; the
runnable snippets are in `docs/static/code_snips/`.

For the build system itself (container setup, `run-getdeps.py` flags, build
targets, troubleshooting), the `build-fboss-oss-local` skill is the canonical
reference. This file covers the platform-services specifics.

## Build everything

`fboss_platform_services` is an aggregate target covering all services, the CLI
utilities, and the unit tests:

```bash
./fboss/oss/scripts/run-getdeps.py \
  build \
  --allow-system-packages \
  --build-type MinSizeRel \
  --scratch-path /var/FBOSS/tmp_bld_dir \
  --cmake-target fboss_platform_services \
  fboss
```

## Individual targets

Pass any of these to `--cmake-target` to build just that binary.

The target name is the binary name, so the five services build as
`platform_manager`, `sensor_service`, `fan_service`, `data_corral_service` and
`rackmon`.

### CLI utilities

| Tool | Purpose |
|------|---------|
| `fw_util` | Firmware updates for BIOS, CPLD, FPGA. Config-driven, ramdisk-safe (<1MB). |
| `weutil` | Reads and parses ID EEPROMs. |
| `fixmyfboss` | Platform health diagnostics (kernel, MAC, PCI, power, SMBus). |
| `sensor_service_client` | Query sensor readings. |
| `reboot_cause_finder` | Reports why the switch last rebooted. |
| `watchdog_util` | Watchdog control. |

## Unit tests (no hardware required)

These build as part of `fboss_platform_services` and are registered with CTest
via `gtest_discover_tests`:

```text
platform_config_lib_config_lib_test
platform_manager_config_validator_test
platform_manager_device_path_resolver_test
platform_manager_platform_explorer_test
platform_manager_data_store_test
platform_manager_presence_checker_test
platform_manager_i2c_explorer_test
platform_manager_pci_explorer_test
platform_manager_utils_test
platform_helpers_platform_name_lib_test
platform_helpers_platform_fs_utils_test
platform_helpers_platform_utils_test
platform_data_corral_sw_test
sensor_service_sw_test
sensor_service_utils_test
rackmon_test
weutil_parser_utils_test
```

Run everything from the build directory:

```bash
ctest
```

**Filtering: `-R` matches registered test names, not target names.**
`gtest_discover_tests` registers each case as `Suite.Case` — `ConfigLibTest.*`,
`ConfigValidator.*` and so on — and none of these targets set `TEST_PREFIX`, so
the target name does not appear in the registered name. `ctest -R
platform_config_lib_config_lib_test` matches nothing and reports "No tests were
found".

List the real names first, then filter on a suite:

```bash
ctest -N
ctest -R ConfigLibTest
```

`fan_service_sw_test` is **not** in the list above. It is built and installed
(`add_executable` + `install(TARGETS ...)` in `PlatformFanService.cmake`) but
has no `gtest_discover_tests` call, so CTest does not know about it. Run the
binary directly.

## Hardware tests (require a physical switch)

These build as ordinary executables rather than registered tests, because they
must run on real hardware:

```text
platform_manager_hw_test
sensor_service_hw_test
fan_service_hw_test
data_corral_service_hw_test
fw_util_hw_test
weutil_hw_test
platform_hw_test
bsp_tests
```

## Where the targets are defined

One CMake file per component under `fboss/github/cmake/`, named
`Platform*.cmake` — for example `PlatformPlatformManager.cmake`,
`PlatformFanService.cmake`, `PlatformSensorService.cmake`. Read these to find
targets not listed here, or to see what a target links against.

The `fboss_platform_services` aggregate is defined in the top-level
`CMakeLists.txt`.
