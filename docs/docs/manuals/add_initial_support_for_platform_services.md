---
id: add_initial_support_for_platform_services
title: Add Initial Support for Platform Services
description: How to add initial support for Platform Services
keywords:
    - FBOSS
    - OSS
    - platform
    - onboard
    - bsp
    - manual
oncall: fboss_oss
---

## Overview

Adding support for Platform Services consists of creating the PlatformManager
config, adding BSP support, ensuring that PlatformManager can start, and passing
100% of HW tests for Platform Services.

If you run into issues, try checking our [troubleshooting guide](/docs/onboarding/troubleshooting/).

After each section, check the **Overall Outcomes** below to ensure everything is
completed.

## Overall Outcomes

### Setup

- an FBOSS platform stack Docker container to perform builds in

### Add PlatformManager Support

- `platform_manager.json` is committed to the FBOSS repository
- `platform_manager.json` abides by the specification

### Add BSP Support

- the BSP abides by the API specification and development requirements
- the BSP is compiled as out-of-tree kernel modules and deployed in FBOSS to
unblock FBOSS development
- `bsp_tests.json` is modified as needed based on guidance in
`bsp_tests_config.thrift`
- 100% of BSP tests pass
- expected errors are defined in the test config if and only if the hardware has
a valid reason and this is agreed to by Meta

### Build, Run, and Test Platform Services

- platform services can be built successfully
- platform manager can start successfully
- 100% of platform service hardware tests pass

## Setup

This setup step is required for adding BSP support and validating platform
services. At the end of it, you should have an FBOSS platform stack Docker
container that can be used to build and test artifacts in all other steps.

### Step 1: Clone the FBOSS Repository

```bash file=./static/code_snips/clone_fboss.sh
```

### Step 2: Stop Existing Containers and Clean Docker Artifacts

```bash file=./static/code_snips/clean_docker.sh
```

### Step 3: Build the FBOSS Docker Image

```bash file=./static/code_snips/build_docker_image.sh
```

### Step 4: Start a Platform Stack Container

```bash file=./static/code_snips/start_platform_stack_container.sh
```

At this point, you should have a Docker container which can be used to build
the required artifacts in later steps.

## Add PlatformManager Support

PlatformManager support is required for all other platform services to run. A
configuration needs to be created and placed in the repository.

### Step 1: Create the Config File

The vendor needs to create the `platform_manager.json` config file. There is a
[sample file](https://github.com/meta/fboss/blob/main/fboss/configs/platforms/generic/sample/platform_stack/platform_manager.json)
located in the repository. The guidelines for creating the config file are
specified [here](/docs/platform/platform_manager/).

### Step 2: Upstream the Changes

After the config is created, raise a PR placing it in its config directory.
Configs are stored [here](https://github.com/meta/fboss/tree/main/fboss/configs/platforms),
where the name of the directory should be the name of the new platform using all
lowercase.

## Add BSP Support

Adding BSP support involves implementing the BSP to support your platforms
and then running tests to ensure adherence to guidelines.

### Step 1: Clone the BSP Repository

```bash file=./static/code_snips/clone_bsp.sh
```

### Step 2: Implement the BSP

Implement the BSP to support the platforms under development. When implementing
the BSP, you need to ensure it abides by the
[API specifications](/docs/platform/bsp_api_specification/) and
[development requirements](/docs/platform/bsp_development_requirements/).

You should only need to add support for your platform by implementing CPLD and
FPGA drivers. The core functionality of the BSP is already implemented and all
platforms adhering to the Meta Switch Architecture should be compatible with it.

### Step 3: Install the KMODS on the Switch

Install the KMODS on the switch.

### Step 4: Configure BSP Tests

BSP Tests use the `platform_manager.json` file to know which devices to test,
but some additional information needs to be provided to the `bsp_tests.json`
configuration file. More details can be found in the [thrift file](https://github.com/meta/fboss/blob/main/fboss/platform/bsp_tests/bsp_tests_config.thrift)
and [bsp_tests.json file](https://github.com/meta/fboss/tree/main/fboss/configs/platforms/generic/sample/platform_stack/bsp_tests.json).

### Step 5: Build BSP Tests

Use the platform stack Docker container from the [Setup](./#setup-1) step.

#### Step 5.1: Enter the Container

```bash file=./static/code_snips/enter_docker_container.sh
```

#### Step 5.2: Build BSP Tests

Build BSP tests by using the build command, where `$TARGET` is `bsp_tests`:

```bash file=./static/code_snips/build_with_cmake_target.sh
```

### Step 6: Send Build Artifacts to the Switch

#### Step 6.1: Package the BSP Tests

```bash file=./static/code_snips/package_fboss.sh
```

#### Step 6.2: Copy and Set Up the Package

```bash file=./static/code_snips/copy_and_set_up_package.sh
```

### Step 7: Run BSP Tests

BSP tests are intended to be a comprehensive test suite for the FBOSS platform
BSP. Tests are designed to cover each statement in the two BSP specification
documents mentioned above.

On the switch that you have copied the package to and installed KMODS on, simply
set up the environment and invoke the binary to run tests:

```bash
cd /opt/fboss
source ./bin/setup_fboss_env

# Run all tests
./bin/bsp_tests

# Filter on specific tests
./bin/bsp_tests --gtest_filter="I2C*"
```

**Note:** A test failure indicates that at least one of the requirements of the
specifications linked above is not met.

### Expected Errors

In some cases it may not be possible for a specific hardware to meet all
specification requirements. In that case we provide an `ExpectedErrors` field
in the bsp tests config. Errors apply to one device and require a `reason`
string to be supplied to it. Currently supported error types can be found with
their documentation in the thrift file and sample config.

We do not plan to use this as a way to get around difficult requirements,
but if your hardware truly cannot meet a requirement for some reason please
reach out to your Meta contact.

### Contributing

Vendors are welcome to contribute to the test suite. The test suite lives
at [fboss/platform/bsp_tests](https://github.com/meta/fboss/tree/main/fboss/platform/bsp_tests/cpp).

## Build, Run, and Test Platform Services

### Step 1: Build Platform Services

Use the platform stack Docker container from the [Setup](./#setup-1) step.

#### Step 1.1: Enter the Container

```bash file=./static/code_snips/enter_docker_container.sh
```

#### Step 1.2: Build Platform Services

Build platform services by using the build command, where `$TARGET` is
`fboss_platform_services`:

```bash file=./static/code_snips/build_with_cmake_target.sh
```

### Step 2: Send Build Artifacts to the Switch

#### Step 2.1: Package the Platform Services

```bash file=./static/code_snips/package_fboss.sh
```

#### Step 2.2: Copy and Set Up the Package

```bash file=./static/code_snips/copy_and_set_up_package.sh
```

### Step 3: Run Platform Manager

Ensure that platform manager can run:

```bash
cd /opt/fboss
source ./bin/setup_fboss_env

./bin/setup.py
./bin/platform_manager
```

### Step 4: Test Platform Services

We have binaries that test the functionality of our platform services. On the
switch that you have copied the package to, simply set up the environment
variables, run the binaries, and observe the output for each binary:

```bash
cd /opt/fboss
source ./bin/setup_fboss_env

./bin/run_test.py platform
```
