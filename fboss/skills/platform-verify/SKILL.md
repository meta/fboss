---
description: End-to-end workflow for building a platform binary, deploying to a switch, running it, and verifying output for errors, BSP version, and exploration status. Triggers on test on switch, verify on switch, build and run on switch, check platform_manager output.
---

# FBOSS Platform Build-Run-Verify

Build a platform binary, deploy to a switch, run it, and verify correctness.

Steps below are written as abstract operations. Map them to your environment
using the device-access reference (see Reference Routing):

```text
UPLOAD <local-file> TO <switch>:<remote-file>
RUN ON <switch>: <command>
```

## Workflow

1. **Build** — see the build reference
2. **Deploy** — `UPLOAD <binary_path> TO <switch>:/tmp/`
3. **Stop services** — stop dependent services before running
4. **Run** — execute the binary on the switch
5. **Verify** — check output for errors, versions, and exploration status

## Stop Services

Only needed if reloading kmods (`--reload_kmods`) or if the BSP version is changing. Otherwise you can run platform_manager directly without stopping services.

```text
RUN ON <switch>: systemctl stop platform_manager fan_service sensor_service data_corral_service
```

`qsfp_service` may not be loaded on all platforms — exit code 5 for "Unit not loaded" is benign.

## Identify Platform

FBOSS reads the platform name from the BIOS, so the same lookup works
anywhere:

```text
RUN ON <switch>: dmidecode -s system-product-name
```

This is what `PlatformNameLib::getPlatformNameFromBios` uses. Some deployments
cache the resolved name in a file — see the device-access reference.

## Run

```text
RUN ON <switch>: /tmp/platform_manager --reload_kmods 2>&1
```

| Flag | Purpose |
|------|---------|
| `--reload_kmods` | Reinstall and reload BSP kernel modules |
| `--enable_pkg_mgmnt=False` | Skip RPM install, use existing kmods |

## Verification

Output is often >200KB. Use grep on the saved output file rather than reading it all.

### 1. Check for errors

```bash
grep -i "error\|fatal\|fail\|exception" <output> | grep -v "Error: No matching Packages to list"
```

"No matching Packages to list" is benign (no old BSP packages to remove).

### 2. Confirm BSP version

```bash
grep -i "bsp\|<version>" <output>
```

Look for `BSP Kmods <bsp_package>-...-<version> is already installed` or
`Installing BSP ...`. The package name is the BSP for your platform's vendor,
as named in `platform_manager.json`.

### 3. Check exploration status

```bash
tail -5 <output>
```

Expected: `Successfully explored <PLATFORM>...` and `exploration_status:SUCCEEDED`

### 4. Verify change-specific behavior

Inspect the change you are testing and decide what evidence of it should appear
on the switch. Look for that evidence specifically in the platform_manager
output or in the resulting sysfs devices — a clean `exploration_status` alone
does not prove your change did anything.

## Reference Routing

Load the `facebook/` version first if it exists in your checkout. Otherwise
load the `references/` version.

| Need | Try first | Fallback |
|------|-----------|----------|
| Building the binary | `facebook/build.md` | `references/build.md` |
| Device access — upload, run on switch, platform-name cache | `facebook/device-access.md` | `references/device-access.md` |
