# FBOSS Platform BSP Tests

This test suite is intended to be a comprehensive test suite for the
FBOSS platform BSP. Tests are designed to cover each statement in the
two BSP specification documents:

* [BSP API Specification](/docs/platform/bsp_api_specification/)
* [BSP Development Requirements](/docs/platform/bsp_development_requirements/)

## Building

The test suite is a C++ binary built in the same way as the platform services.
Follow the instructions ["building fboss on docker containers"](/docs/build/building_fboss_on_docker_containers/)
and use the argument `--cmake-target bsp_tests`

## Running

Simply send the test binary to the switch under test and execute the binary.

* Note that the BSP you want to test must already be installed.
* To avoid conflicts with devices and files, ensure that no platform services
are running on the switch when testing.

You can filter the tests to run by using the `--gtest_filter` argument. E.g. to
only run I2C tests:

```bash
./bsp_tests --gtest_filter="I2C*"
```


## Test Failures

A test failure indicates that at least one of the requirements of the
specifications linked above is not met.

## Test Configuration

BSP Tests use the `platform_manager.json` file to know which devices to test,
but some additional information needs to be provided to the `bsp_tests.json`
configuration file. More details can be found in the [thrift file](https://github.com/meta/fboss/blob/main/fboss/platform/bsp_tests/bsp_tests_config.thrift)
and [the sample bsp_tests.json](https://github.com/meta/fboss/tree/main/fboss/configs/platforms/generic/sample/platform_stack/bsp_tests.json)

### Versioned PmUnits

A PmUnit that has been respun may have a `versionedPmUnitConfigs` entry in
`platform_manager.json`, because a device can move to a different address or
use a different driver between revisions.

BSP Tests pick the matching entry automatically when they can read the PmUnit's
version, so normally there is nothing to do. The requirements from the Running
section above are unchanged -- in particular, no platform services should be
running during the test.

The version is the PmUnit's `ProductionState`, `ProductionSubState` and
`RespinVariantIndicator`, which `weutil` reports as `Product Production State`,
`Product Version` and `Product Sub-Version`. There is no way to supply it by
hand, deliberately: `platform_manager` has no such override either, and BSP
Tests exist to validate the board against the configuration `platform_manager`
would apply.

BSP Tests read each PmUnit's IDPROM themselves before the tests start: they
load the BSP kernel modules, bring up the I2C adapters the IDPROMs sit behind,
and identify each PmUnit by the same rules `platform_manager` uses. Nothing
needs to have run beforehand, so a board with only the BSP installed works. A
PmUnit whose IDPROM cannot be read is validated against its default
`pmUnitConfigs` entry, and the tests warn and name any PmUnit that declares
respins but could not be resolved.

If a device fails detection at an address that does not match your hardware,
check whether it has a versioned entry before using Expected Errors below.

### Expected Errors

In some cases it may not be possible for a specific hardware to meet all
specification requirements. In that case we provide an ExpectedErrors field
in the bsp tests config. Errors apply to one device and require a `reason`
string to be supplied to it. Currently supported error types can be found with
their documentation in the thrift file and sample config.

We do not plan to use this as a way to get around difficult requirements,
but if your hardware truly cannot meet a requirement for some reason please
reach out to your Meta contact.

## Contributing

Vendors are welcome to contribute to the test suite. The test suite lives
at [fboss/platform/bsp_tests](https://github.com/meta/fboss/tree/main/fboss/platform/bsp_tests)
