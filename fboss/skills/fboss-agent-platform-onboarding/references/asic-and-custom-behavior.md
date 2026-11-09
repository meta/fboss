# New ASIC and Custom Behavior

Use this reference only after proving the standard data-driven path is
insufficient.

## New ASIC on an Existing SAI Backend

Treat ASIC support as reusable chip support, not as platform code.

1. Add a unique `AsicType` in `fboss/agent/switch_config.thrift`.
2. Add the ASIC trait under `fboss/agent/hw/switch_asics/`, deriving from the
   closest correct vendor/family base.
3. Register construction in `HwAsic::makeAsic()` and update the local Buck and
   open-source CMake source lists.
4. Add focused ASIC tests beside the existing switch-ASIC tests.
5. Implement only behavior required by the chip. Inspect callers and sibling
   ASICs before deciding which APIs need overrides.

Platform behavior previously duplicated in SAI platform subclasses now belongs
on ASIC traits when it is chip-wide. Important extension points include:

- `getNumLanesPerCore()`
- `getNumCellsAvailable()`
- `getSaiPhysicalLaneId()`
- `getInternalSystemPortConfig()`
- feature support, queue/resource limits, stream types, and loopback behavior

Do not copy every override from a sibling ASIC. An inherited default may be the
correct behavior, and an unsupported operation should fail explicitly rather
than return an invented constant.

If the ASIC requires a SAI implementation or vendor backend that FBOSS does not
already build, stop and split that SDK/backend integration from the platform
onboarding. The platform descriptor cannot create an unlinked backend.

## Custom Platform or Port Behavior

First identify the exact behavior that cannot be represented by:

- static lane/polarity mapping
- port profiles or SI settings
- vendor ASIC configuration
- descriptor variant attributes
- an existing ASIC trait
- the generic vendor platform or port class

Keep the generic platform object when only port behavior differs. Add a narrow
port subclass that overrides the required hook, then route only that
`PlatformType` to it in the applicable `SaiPlatformInit*.cpp` factory before
the generic fallback.

Add a dedicated platform subclass only for platform-level behavior unavailable
from the generic class. In either case:

- Put shared chip semantics on the ASIC trait.
- Add only the new source/header to the applicable vendor source list in
  `fboss/agent/platforms/sai/platform.bzl` and the matching OSS CMake file.
- Add the platform to the generic exclusion predicate only when a custom
  **platform** subclass replaces the generic platform. A port-only subclass
  must remain eligible for generic platform construction.
- Preserve external descriptor/mapping loading for all data the generic path
  can still consume.
- Test the exceptional behavior directly and also run a generic-platform
  startup test so the custom branch does not bypass descriptor loading.

## Scope Check

Before finishing, state which category each change belongs to:

| Category | Expected location |
|---|---|
| Board wiring, profiles, SI, SDK configuration | Platform config inputs |
| Product/mode identity and variants | Platform descriptor |
| Chip behavior shared by boards | `HwAsic` subclass |
| One board's exceptional runtime behavior | Narrow platform/port subclass |
| New SDK or SAI backend | Separate integration work |
| Per-platform class in another FBOSS service (QSFP, LED, BSP) | That service's own onboarding; keep it, but out of scope here |

The rule below applies only to **Agent** C++—the platform, port, and ASIC
classes this skill governs. If an Agent C++ change cannot be placed in the
`HwAsic`, platform/port subclass, or separate-integration rows with a concrete
reason, remove it from the onboarding.

A per-platform class belonging to another service is a normal part of bring-up
even when the Agent side stays fully data-driven. Leave it in place.
