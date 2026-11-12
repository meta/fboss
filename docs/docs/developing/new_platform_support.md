# New Platform Support

## Overview

This document describes how to onboard a new platform to the FBOSS `wedge_agent`, `qsfp_service`, and `led_service`.

FBOSS is transitioning to a **config-driven platform model**. Historically, adding a
platform required new per-platform C++ classes and edits to several hardcoded `if/else`
dispatch chains. With the config-driven model, a *standard* platform is instead described
by two generated JSON files — a **platform mapping** and a **platform descriptor** — and is
instantiated at runtime through generic platform/port classes. This path is gated behind the
`--platform_descriptor_config_path` flag and is the recommended way to add new standard
platforms.

The historical manual path still works and remains the default when
`--platform_descriptor_config_path` is not set. It is documented below as a fallback and for
platforms that require custom C++ behavior. Please follow this [GitHub Commit](https://github.com/meta/fboss/commit/00b86dfee299bd164f6380938c501e0ee92482c2)
as an example if the code pointers are not sufficient.

## Config-Driven Platform Support (Recommended)

With the config-driven path, a standard platform needs **no new per-platform C++ classes** in
the agent. Platform identity, detection, and ASIC selection come from a `PlatformDescriptor`,
and the agent instantiates generic platform and port classes at runtime.

### 1. Add the PlatformType enum entry

The `PlatformType` enum in [fboss/lib/if/fboss_common.thrift](https://github.com/meta/fboss/blob/main/fboss/lib/if/fboss_common.thrift)
is intentionally preserved because it has broad downstream impact (Thrift RPCs, FSDB, DSF node
configs). A new platform therefore still needs a single new enum entry. This is the only
required Thrift change.

### 2. Generate the platform mapping and platform descriptor

Follow [Platform Mapping Config Generation](https://meta.github.io/fboss/docs/developing/platform_mapping/).
In addition to the existing CSVs, provide a `PLATFORM_platform_descriptor.csv` describing the
platform identity (system vendor, `PlatformType`, product-name prefixes, mode names, and ASIC
type). The generator writes **both** `platform_mapping.json` and `platform_descriptor.json`
beside the platform inputs in
`fboss/configs/platforms/<vendor>/<platform>/platform_mapping/generated/`.

### 3. Deploy the descriptor tree and run with the flag

Deploy the generated files under a descriptor config root using the layout
`<root>/<system_vendor>/<platform_name>/platform_descriptor.json` and
`<root>/<system_vendor>/<platform_name>/platform_mapping.json`, then start the binaries with
`--platform_descriptor_config_path <root>`. On startup:

- `PlatformDescriptorRegistry` ([fboss/lib/platforms/PlatformDescriptor.h](https://github.com/meta/fboss/blob/main/fboss/lib/platforms/PlatformDescriptor.h) /
  [.cpp](https://github.com/meta/fboss/blob/main/fboss/lib/platforms/PlatformDescriptor.cpp))
  scans the tree and indexes descriptors by product-name prefix and mode name.
- `PlatformProductInfo::initMode()` resolves the `PlatformType` from the registry, falling back
  to the legacy detection chains if no descriptor matches.
- `SaiPlatformInit` loads the external `platform_mapping.json` (instead of a compiled-in string)
  and, based on the descriptor's `asicType`, dispatches to `GenericSaiBcmPlatform`
  ([source](https://github.com/meta/fboss/blob/main/fboss/agent/platforms/sai/GenericSaiBcmPlatform.h))
  or `GenericSaiTajoPlatform`
  ([source](https://github.com/meta/fboss/blob/main/fboss/agent/platforms/sai/GenericSaiTajoPlatform.h)).
  Ports use the shared `SaiBcmPlatformPort` / `SaiTajoPlatformPort` classes.

### 4. Ensure ASIC support

Per-ASIC constants and formulas — such as `numLanesPerCore()`, `numCellsAvailable()`, the SAI
physical lane IDs, and internal system-port config — now live on the ASIC traits
(`HwAsic` subclasses in
[fboss/agent/hw/switch_asics/](https://github.com/meta/fboss/tree/main/fboss/agent/hw/switch_asics)),
not on platform classes. If your platform uses an already-supported ASIC, no C++ is needed. If
it introduces a new ASIC, add or extend the corresponding ASIC trait — see
[Agent Development](https://meta.github.io/fboss/docs/developing/agent_development/).

### 5. Custom behavior (only if needed)

Most platforms are pure data and need no C++. If your platform has non-standard behavior that
the generic classes cannot express, add a dedicated platform/port subclass (see the manual path
below) for that specific behavior; the generic path continues to serve everything else.

:::note
The platform descriptor currently carries platform identity and ASIC type. Additional
capabilities — such as embedding hardware topology in the descriptor and per-variant descriptor
selection — are being added incrementally.
:::

## Manual Path (fallback / custom platforms)

When `--platform_descriptor_config_path` is not set, or for platforms that require custom C++,
the agent uses the manual wiring below. Note that many of the per-platform classes and `if/else`
branches listed here have been superseded by the generic classes and ASIC traits described
above; add them only if your platform needs behavior beyond the generic path.

### Common Code Changes
1. Add a new entry to the PlatformType enum in [fboss/lib/if/fboss_common.thrift](https://github.com/meta/fboss/blob/main/fboss/lib/if/fboss_common.thrift).
2. Add a new entry to the `toString(PlatformType mode)` function in [fboss/lib/platforms/PlatformMode.h](https://github.com/meta/fboss/blob/main/fboss/lib/platforms/PlatformMode.h).
3. Add a new entry to `initMode()` in [fboss/lib/platforms/PlatformProductInfo.cpp](https://github.com/meta/fboss/blob/main/fboss/lib/platforms/PlatformProductInfo.cpp).
4. Add a new switch case for your platform to `initPlatformMapping()` in [fboss/agent/platforms/common/PlatformMappingUtils.cpp](https://github.com/meta/fboss/blob/main/fboss/agent/platforms/common/PlatformMappingUtils.cpp#L124).
6. Based on your files created in [Platform Mapping Config Generation](https://meta.github.io/fboss/docs/developing/platform_mapping/), add all platform mapping CSV files under `fboss/configs/platforms/<system_vendor>/<platform>/platform_mapping/`.
7. Similarly, create a new folder and add a source file and header for your platform mapping. This requires copying the generated platform mapping JSON into `kJsonPlatformMappingStr` within the `.cpp` file.
   - `fboss/agent/platforms/common/{PLATFORM}/{PLATFORM}PlatformMapping.h` [Header example](https://github.com/meta/fboss/blob/main/fboss/agent/platforms/common/montblanc/MontblancPlatformMapping.h)
   - `fboss/agent/platforms/common/{PLATFORM}/{PLATFORM}PlatformMapping.cpp` [Source example](https://github.com/meta/fboss/blob/main/fboss/agent/platforms/common/montblanc/MontblancPlatformMapping.cpp)
   - Embedding the mapping JSON as a C++ string literal is the legacy approach. With the
     config-driven path, the mapping is loaded from an external `platform_mapping.json` instead;
     see [Config-Driven Platform Support](#config-driven-platform-support-recommended) above.

### Agent Code Changes

For a standard platform, prefer the generic classes over the per-platform classes below:
instantiate `GenericSaiBcmPlatform` / `GenericSaiTajoPlatform` and the shared
`SaiBcmPlatformPort` / `SaiTajoPlatformPort`, and keep ASIC-specific values in the ASIC trait.
Only add the dedicated classes below if your platform needs custom behavior.

1. Add a `SaiBcm{PLATFORM}PlatformPort` header and source file for your platform:
   - `fboss/agent/platforms/sai/SaiBcm{PLATFORM}PlatformPort.h` [Header example](https://github.com/meta/fboss/blob/ad0121584f3dc5f9b3d631c2473d14412599e6c7/fboss/agent/platforms/sai/SaiBcmMontblancPlatformPort.h)
   - `fboss/agent/platforms/sai/SaiBcm{PLATFORM}PlatformPort.cpp` [Source example](https://github.com/meta/fboss/blob/ad0121584f3dc5f9b3d631c2473d14412599e6c7/fboss/agent/platforms/sai/SaiBcmMontblancPlatformPort.cpp)

2. Add a `SaiBcm{PLATFORM}Platform` header and source file for your platform:
   - `fboss/agent/platforms/sai/SaiBcm{PLATFORM}Platform.h` [Header example](https://github.com/meta/fboss/blob/ad0121584f3dc5f9b3d631c2473d14412599e6c7/fboss/agent/platforms/sai/SaiBcmMontblancPlatform.h)
   - `fboss/agent/platforms/sai/SaiBcm{PLATFORM}Platform.cpp` [Source example](https://github.com/meta/fboss/blob/ad0121584f3dc5f9b3d631c2473d14412599e6c7/fboss/agent/platforms/sai/SaiBcmMontblancPlatform.cpp)

3. Add a new entry to `initPorts()` in [fboss/agent/platforms/sai/SaiPlatform.cpp](https://github.com/meta/fboss/blob/main/fboss/agent/platforms/sai/SaiPlatform.cpp).
4. Add a new entry to `chooseSaiPlatform()` in [fboss/agent/platforms/sai/SaiPlatformInit.cpp](https://github.com/meta/fboss/blob/main/fboss/agent/platforms/sai/SaiPlatformInit.cpp).
5. Make changes to CMake files to support building in open source:
   - Add `cmake/AgentPlatformsCommon{PLATFORM}.cmake` for your platform ([Example](https://github.com/meta/fboss/blob/main/cmake/AgentPlatformsCommonMontblanc.cmake))
   - Then add this library name to `sai_platform` link libraries in [cmake/AgentPlatformsSai.cmake](https://github.com/meta/fboss/blob/main/cmake/AgentPlatformsSai.cmake#L104)

### Qsfp Service Code Changes
1. Create BspPlatformMapping header and source files under
   `fboss/lib/bsp/{PLATFORM}/`
   - `fboss/lib/bsp/{PLATFORM}/{PLATFORM}BspPlatformMapping.h` [Header example](https://github.com/meta/fboss/blob/main/fboss/lib/bsp/montblanc/MontblancBspPlatformMapping.h)
   - `fboss/lib/bsp/{PLATFORM}/{PLATFORM}BspPlatformMapping.cpp` [Source example](https://github.com/meta/fboss/blob/main/fboss/lib/bsp/montblanc/MontblancBspPlatformMapping.cpp)

2. Add an LED Manager class for your platform:
   - `fboss/led_service/{PLATFORM}LedManager.h` [Header example](https://github.com/meta/fboss/blob/main/fboss/led_service/MontblancLedManager.h)
   - `fboss/led_service/{PLATFORM}LedManager.cpp` [Source example](https://github.com/meta/fboss/blob/main/fboss/led_service/MontblancLedManager.cpp)
   - Add to [fboss/led_service/LedManagerInit.cpp](https://github.com/meta/fboss/blob/main/fboss/led_service/LedManagerInit.cpp).

3. In `fboss/qsfp_service/platforms/wedge/WedgeManagerInit.cpp`, add a function called `create{PLATFORM}WedgeManager` that instantiates a `WedgeManager` object with the platform mapping JSON file.
   - `fboss/qsfp_service/platforms/wedge/WedgeManagerInit.h` [Header example](https://github.com/meta/fboss/blob/main/fboss/qsfp_service/platforms/wedge/WedgeManagerInit.h#L59)
   - `fboss/qsfp_service/platforms/wedge/WedgeManagerInit.cpp` [Source example](https://github.com/meta/fboss/blob/main/fboss/qsfp_service/platforms/wedge/WedgeManagerInit.cpp#L181)

4. Make changes to CMake files to support building in open source:
   - Add BSP library definition to [cmake/QsfpService.cmake](https://github.com/meta/fboss/blob/main/cmake/QsfpService.cmake) and link to `qsfp_bsp_core`
   - Add LED Manager source file to `led_manager_lib` in [cmake/LedService.cmake](https://github.com/meta/fboss/blob/main/cmake/LedService.cmake) and link BSP and platform mapping libraries
   - Add platform mapping library to `qsfp_platforms_wedge` in [cmake/QsfpServicePlatformsWedge.cmake](https://github.com/meta/fboss/blob/main/cmake/QsfpServicePlatformsWedge.cmake)
