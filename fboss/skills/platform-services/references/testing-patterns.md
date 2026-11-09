# Platform Services — Testing Patterns

## Unit Tests

Unit tests use mock libraries for dependency injection:
- `mock_platform_utils` — subprocess/system call mocks
- `mock_platform_fs_utils` — filesystem operation mocks
- `mock_config_lib` — config loading mocks

## Rackmon Tests

Rackmon tests compile with `-D__TEST__` to enable test-specific code paths. This preprocessor flag gates test-only functionality in production code.

## Version Tests

Version tests (`test_*_version`) are `custom_unittest` targets that verify binaries produce valid version strings. They build the binary and check its `--version` output.

## BSP Tests

BSP tests validate hardware subsystems against platform_manager config on actual hardware:
- I2C bus enumeration and device presence
- GPIO chip detection and pin state
- Hwmon sensor file presence and readability
- Character device (Cdev) presence
- Kernel module (Kmod) loading
- LED device control
- Watchdog device presence
- Transceiver (Xcvr) detection

BSP test parameters are defined per-platform in `configs/<platform>/bsp_tests.json`.

Build target: `bsp_tests`

## Hardware Tests

Hardware tests build as ordinary executables rather than registered tests,
because they must run on physical switch hardware and cannot run in CI. See
`build-and-test.md` for the full list.

## Config Validation

When modifying platform configs, always run the config validation test:

```bash
ctest -R ConfigLibTest
```

`gtest_discover_tests` registers CTest names as `Suite.Case`, so filter on the
gtest suite (`ConfigLibTest`), not the CMake target name. `ctest -N` lists the
registered names.

This test validates all JSON configs against their Thrift schemas and checks for structural correctness.
