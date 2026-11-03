# Running QSFP HAL Tests

## Overview

QSFP HAL tests validate that a transceiver's control plane complies with FBOSS
expectations. They talk to the module directly over I2C, checking behaviors such
as:

- Firmware upgrade and downgrade
- Datapath bring up
- Module delay advertisements (datapath init/deinit, module power up timings)
- Application and datapath mode selection
- VDM (Versatile Diagnostics Monitoring) latch behavior

These tests don't validate links or datapath.

This guide is for vendors who already have an FBOSS distro running on the
switch.

## Overall Outcomes

### Setup

- QSFP Service stopped so it does not contend for the I2C bus
- `qsfp_hal_test` binary and its shared libraries present on the switch
- a firmware manifest describing your firmware images
- a HAL test config enumerating the ports under test

### Run Tests

- a single smoke test passes against the module
- broader test suites can be scoped and run

## Setup

### Step 1: Stop QSFP Service

QSFP HAL tests communicate with transceivers over the same I2C path that QSFP
Service uses, so the two conflict if both are running. Stop the service:

```bash
systemctl stop qsfp_service
```

Confirm it is stopped before continuing:

```bash
systemctl status qsfp_service
```

Restart it with `systemctl start qsfp_service` once testing is complete.

### Step 2: Get the Test Binary and Shared Libraries

Prebuilt binaries are published by the scheduled runs of the
[Build & Test QSFP Service][qsfp-build] GitHub Actions workflow on `main`.

1. Open the latest successful run and download the `fboss` artifact from the
   run's **Artifacts** section. GitHub requires you to be signed in to download
   artifacts.
2. Extract the binaries tarball from the artifact:

   ```bash
   unzip fboss.zip
   mkdir fboss_bins
   tar -xf fboss_bins.tar.zst --zstd -C fboss_bins
   ```

   The artifact also contains `fboss_docker_image.tar.zst`, which is not needed
   to run the tests.
3. Copy the `qsfp_hal_test` binary from `bin/` and the whole `lib/` directory
   to the switch, for example to `/tmp/bin/` and `/tmp/lib/`:

   ```bash
   ssh <switch> mkdir -p /tmp/bin
   scp fboss_bins/bin/qsfp_hal_test <switch>:/tmp/bin/
   scp -r fboss_bins/lib <switch>:/tmp/
   ```

[qsfp-build]: https://github.com/meta/fboss/actions/workflows/qsfp-build.yml?query=event%3Aschedule+branch%3Amain+is%3Asuccess

### Step 3: Set the Library Search Path

```bash
export LD_LIBRARY_PATH=/tmp/lib/
```

Replace `/tmp/lib/` with the directory used in Step 2.

### Step 4: Create the Firmware Manifest

The manifest maps a firmware handle to firmware images on disk, their versions,
and the programming properties the tests need. Place it alongside the images so
that relative `file:` paths resolve, for example
`/tmp/firmware/fboss_firmware.yaml` with the images in the same directory.

```yaml
# YAML file for describing the firmware for FBOSS platforms
#
# PLACEHOLDER EXAMPLE. Replace the handle, versions, file paths, and
# md5sums with the values for your own module and firmware images.
- name: "EXAMPLE-TRANSCEIVER"
  description: "Example transceiver firmware, production build"
  versions:
    - version: "2.0.1024"
      file: "example_firmware1.bin"
      md5sum: "00000000000000000000000000000000"
      properties:
        - msa_password: "0x00001011"
          header_length: "110"
          image_type: "application"
          info: "Firmware 2.0 build 1024 for Example transceiver"
    - version: "2.1.2048"
      file: "example_firmware2.bin"
      md5sum: "11111111111111111111111111111111"
      properties:
        - msa_password: "0x00001011"
          header_length: "110"
          image_type: "application"
          info: "Firmware 2.1 build 2048 for Example transceiver"
    - version: "2.1.3072"
      file: "example_firmware3.bin"
      md5sum: "22222222222222222222222222222222"
      properties:
        - msa_password: "0x00001011"
          header_length: "110"
          image_type: "application"
          info: "Firmware 2.1 build 3072 for Example transceiver"
```

Every value in the example is a placeholder. Replace them as follows.

| Field | What to use |
| --- | --- |
| `name` | Your firmware handle. Must match the value side of `fwHandleMap` in the HAL test config. |
| `version` | The version string the module reports, in decimal rather than hex. Must match the `version` entries in the HAL test config exactly. |
| `file` | Path to your firmware image, relative to the manifest's own directory. |
| `md5sum` | Checksum of the image, from `md5sum <file>` after copying it to the switch. The placeholder values above will fail validation. |
| `header_length` | Image header length, from your optics programming specification. |
| `image_type` | `application` or `dsp`, matching `fwType` in the HAL test config (`1` or `2` respectively). |

`msa_password` is `0x00001011`. This is the standard MSA value and does not
change per module.

### Step 5: Create the HAL Test Config

The config enumerates the ports under test, the firmware versions to qualify,
and the mapping from transceiver part number to firmware handle.

```json
{
  "transceivers": [
    {
      "id": 9,
      "name": "eth1/9/1",
      "startupConfig": {
        "firmware": {
          "versions": [
            { "fwType": 1, "version": "2.1.3072" }
          ]
        }
      },
      "previousFirmware": {
        "versions": [
          { "fwType": 1, "version": "2.1.2048" }
        ]
      }
    },
    {
      "id": 20,
      "name": "eth1/20/1",
      "startupConfig": {
        "firmware": {
          "versions": [
            { "fwType": 1, "version": "2.1.3072" }
          ]
        }
      },
      "previousFirmware": {
        "versions": [
          { "fwType": 1, "version": "2.1.2048" }
        ]
      }
    }
  ],
  "fwHandleMap": {
    "EXAMPLE-OSFP800-PN1": "EXAMPLE-TRANSCEIVER"
  }
}
```

Update `fwHandleMap` for your own module:

- The key is your transceiver's part number, exactly as the module reports it in
  EEPROM (page 0, bytes 148 to 163). If it does not match, no firmware handle is
  found and the firmware tests will not run against the module.
- The value is the firmware handle defined as `name:` in `fboss_firmware.yaml`.
  The two strings must match exactly.

`id` and `name` must match the ports populated on your system, and every
`version` must be declared in the manifest.

#### Field Notes

- `fwType` is `1` for `APPLICATION` and `2` for `DSP`. The example qualifies
  application firmware only. Add a `"fwType": 2` entry to also qualify DSP
  firmware.
- The firmware upgrade test runs A to B and B back to A, where A is
  `previousFirmware` and B is `startupConfig.firmware`. In the example that is
  2.1.2048 and 2.1.3072. If `previousFirmware` is omitted it defaults to the
  firmware in `startupConfig`.
- Multiple transceiver entries run the same qualification across several ports,
  which is useful for catching port specific I2C issues.

#### How the Two Files Relate

```text
HAL test config                        fboss_firmware.yaml
---------------                        -------------------
fwHandleMap:
  "<your part number>"  ----------+
       : "EXAMPLE-TRANSCEIVER" ---+---> name: "EXAMPLE-TRANSCEIVER"
                                  |
startupConfig.firmware            |      versions:
  versions[].version: "2.1.3072" -+---->   - version: "2.1.3072"
previousFirmware                  |            file: example_firmware3.bin
  versions[].version: "2.1.2048" -+---->   - version: "2.1.2048"
                                               file: example_firmware2.bin
```

The part number is the only string that comes from the module itself. Everything
else is a name you choose, as long as it is consistent across both files.

`example_firmware1.bin` (version 2.0.1024) is present in the manifest but unused
by this config. This is expected, since a manifest usually carries more versions
than any single test run exercises.

## Run Tests

### Step 1: Run a Smoke Test

Start with a single inexpensive test that verifies the module advertises a known
media interface. If this fails, the module is not being read correctly and there
is no value in running the longer tests.

```bash
./qsfp_hal_test \
  --hal_test_config /tmp/hal_test_config.json \
  --default_firmware_manifest /tmp/firmware/fboss_firmware.yaml \
  --gtest_filter=T1HalTest.verifyModuleMediaInterfaceIsNotUnknown
```

### Step 2: Scope to Broader Test Sets

List the available tests:

```bash
./qsfp_hal_test --gtest_list_tests
```

Then widen `--gtest_filter` as needed:

```text
--gtest_filter=T1HalTest.*           # a whole suite
--gtest_filter='*FirmwareUpgrade*'   # a related group by name pattern
--gtest_filter='*'                   # everything
```

Run one test at a time while bringing up a new module. The firmware upgrade
tests reflash the module and take significantly longer than the others.

### Step 3: [Optional] Restore QSFP Service

```bash
systemctl start qsfp_service
```

## Understanding What Each Test Does

The HAL tests are GoogleTest C++ files and are the authoritative description of
what is validated. When debugging a failure, read the test source rather than
inferring behavior from the test name. The tests live in
[fboss/qsfp_service/test/hal_test](https://github.com/meta/fboss/tree/main/fboss/qsfp_service/test/hal_test).

Test cases are grouped by area, one file per area.

| File | What it covers |
| --- | --- |
| `HalTestModuleAdvertisement.cpp` | What the module advertises about itself: media interface, capabilities, and declared delays. Contains `verifyModuleMediaInterfaceIsNotUnknown`. |
| `HalTestModuleInit.cpp` | Module initialization and bring up from a cold or reset state. |
| `HalTestApplicationModes.cpp` | Application and datapath mode selection and configuration. |
| `HalTestFirmwareUpgrade.cpp` | Firmware upgrade and downgrade. |
| `HalTestVdmLatch.cpp` | VDM latch behavior. |

Supporting files:

| File | Purpose |
| --- | --- |
| `HalTest.cpp`, `HalTest.h` | The `T1HalTest` fixture, including setup and teardown applied to every test. |
| `HalTestUtils.cpp` | Shared helpers, including how the config and firmware manifest are loaded. |
| `hal_test_config.thrift` | Schema for the HAL test config, and the definitive list of valid fields. |
| `Main.cpp` | Entry point and command line flags. |

To diagnose a failing test:

1. Note the failing test name from the GoogleTest output, for example
   `T1HalTest.someCheck`.
2. Find `TEST_F(T1HalTest, someCheck)` in the file matching its area.
3. Read the assertions to see which register read, advertised value, or timing
   the module did not satisfy.
