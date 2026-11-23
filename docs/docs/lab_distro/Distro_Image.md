---
id: distro_image_manifest
title: Distro Image Manifest format
description: How to assemble a custom FBOSS Distro Image
keywords:
    - FBOSS
    - OSS
    - build
oncall: fboss_oss
---

# Distro Image Manifest Format

## Manifest Format

The manifest is a JSON file that defines what components to build, where to get them, and how to compose the final FBOSS distribution image.


For example:

```json
{
  "distribution_formats": {
    "pxe": "FBOSS-k6.4.3.tar",
    "onie": "FBOSS-k6.4.3.bin",
    "usb": "FBOSS-k6.4.3.iso"
  },
  "kernel": {
    "download": "https://artifact..../fboss/fboss-oss_kernel_v6.4.3.tar"
  },
  "other_dependencies": [
    {"download": "https://artifact.github.com/.../fsdb/fsdb.rpm"},
    {"download": "file:vendor_debug_tools/tools.rpm"}
  ],
  "fboss-platform-stack": {
    "execute": ["../fboss/oss/scripts/build_fboss_stack.sh", "platform"]
  },
  "bsps": [
    {"execute": "vendor_bsp/build.make"},
    {"execute": "fboss/oss/reference_bsp/build.py"}
  ],
  "npu_sai": {
    "execute": "fboss_brcm_sai/build.sh"
  },
  "phy_sai": {
    "execute": ["../fboss/oss/scripts/build_phy_sai.sh"]
  },
  "fboss-forwarding-stack": {
    "execute": ["../fboss/oss/scripts/build_fboss_stack.sh", "forwarding"]
  },
  "image_build_hooks": {
    "after_pkgs_install": "additional_os_pkgs.json",
    "after_pkgs_execute": "additional_image_mods.json"
  }
}
```

### Distribution Formats

`distribution_formats` specifies which output image formats to produce. Supported formats:
- `onie`: ONIE installer binary for network switches. Normal extension `.bin`
- `usb`: Bootable ISO image for USB installation. Normal extension `.iso`
- `pxe`: Tarball for network installation (PXE). Normal extension `.tar`

### Image Build Hooks

`image_build_hooks` specifies customization points in the build process. Supported hooks:
- `after_pkgs_install`: JSON file with list of additional packages to install
- `after_pkgs_execute`: JSON file with list of commands to execute after packages are installed

To install additional files or RPM packages not in the CentOS RPM repos, use the "other_dependencies" component.

`after_pkgs_install` hook file example:
```json
{
  "packages": [
    "tcpdump"
  ]
}
```

`after_pkgs_execute` hook file example:
```json
{
  "execute": [
    [ "date"],
    [ "uname", "-a"]
  ]
}
```

### Components

Each remaining top-level key in the manifest represents a component or component list to build. Components can be one of
two types:
- `download`: URL or local file path to download the component from. The component will be extracted from the downloaded
  archive. If the component artifact type is a tarball, then it can be compressed or uncompressed. `file:` URLs are
  relative to the manifest file. `file:/` URLs are absolute.
- `execute`: Script to execute to build the component. The command is executed in the directory of the script file. Two
  forms are supported: a string script path to execute, or an array of the script path followed by arguments to pass to
  the script.

Component build scripts are responsible for caching and incremental build support. All build scripts are executed inside
a fresh build container with the component dependencies either installed or available in `/deps` as appropriate. The
output artifact of the build script must be placed into /output to be copied outside the build container.

TODO document `artifact`

For components that are lists (e.g. bsps), each list element is built independently and the artifacts are collected
together.

### Kernel output artifact

The kernel build script must output an uncompressed tarball containing the kernel RPMs along the standard Redhat
structure. See the FBOSS-OSS kernel under `fboss-image/kernel` for an example.

### Other dependencies output artifact

Each other_dependency must produce a single RPM which is installed in the final image. These RPMs may have runtime
dependencies which will be satisfied by dnf. See `examples/thirdparty_build.sh` for an example of how to build a
third-party RPM.

During each of these builds, the kernel devel package will be installed in the build container.

### FBOSS Platform and Forwarding Stack

See `fboss/oss/scripts/build_fboss_stack.sh` for details on how to build the FBOSS stacks. The artifact format is a
tarball of files which will be extracted under `/opt/fboss` in the final image.

Each of these two stacks are built separately. The Platform Stack has only the kernel installed, while the Forwarding
Stack also has the SAI implementation installed.

### BSPs

Each BSP artifact is a tarball of RPMs. The RPM files will be copied (not installed) into the image in the local RPM
repository at `/usr/local/share/local_rpm_repo`. platform_manager will install the BSP RPMs from this repository at
runtime.

During each of these builds, the kernel devel package will be installed in the build container.

### HW Agent SAI

The HW Agent SAI build script must output a tarball containing the following file structure:
```
sai_build.env   -- FBOSS SAI build environment variables
include/        -- SAI headers
lib/            -- SAI library (libsai_impl.a or libsai_impl.so)
sai-runtime.rpm -- SAI runtime package
```

The `sai_build.env` file defines the environment variables needed to build the FBOSS platform and forwarding stacks
against the SAI implementation. See [Building FBOSS on docker
containers](/docs/build/building_fboss_on_docker_containers/#set-important-environment-variables).
The file might look like:
```
SAI_BRCM_IMPL=1
SAI_SDK_VERSION=SAI_VERSION_13_3_0_0_ODP
SAI_VERSION=1.16.1
```

The `sai-runtime.rpm` package contains the SAI runtime dependencies, normally kernel modules and init scripts. It is
installed into the image at build-time.

The SAI is built with the kernel devel packages installed.

A special `fboss/oss/scripts/fake-sai-devel.tar.zstd` is provided which will activate the Fake-SAI build of the
Forwarding Stack.

### QSFP Service SAI

The QSFP Service SAI build script must output a tarball containing the following file structure:
```
phy_sai_build.env   -- QSFP Service SAI build environment variables
include/                     -- QSFP SAI headers
lib/                         -- QSFP SAI library (libphy_sai.a)
phy_sai-runtime.rpm -- QSFP Service SAI runtime package
```

The `phy_sai_build.env` file defines the environment variables needed to build the QSFP Service against the
SAI implementation. The file might look like:
```
SAI_BRCM_PAI_IMPL=1
SAI_SDK_VERSION=PHY_SAI_SDK
```

## Environment variables

The environment `FBOSS_BUILDER_CACHE_EXPIRATION_HOURS` controls how long to cache the fboss_builder Docker image,
irrespective of whether the hashed contents have changed. The default is 24 hours.

## Development

### Running Tests

```bash
# Build and run all tests
cmake --build . --target distro_cli_tests

# Run via CTest
ctest -R distro_cli -V

### Running manual tests
A few tests are deliberately skipped while unit testing due to longer execution times. They can however be run manually as needed. The following are some of the examples.

# Testing kernel build with compressed artifacts
```bash
python -m pytest distro_cli/tests/kernel_build_test.py::TestKernelBuildE2E::test_real_kernel_build_compressed -v -s -m e2e
```
