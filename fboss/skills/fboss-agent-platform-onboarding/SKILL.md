---
name: fboss-agent-platform-onboarding
description: Use when planning, implementing, or reviewing a new FBOSS hardware platform for wedge_agent, including PlatformType registration, platform descriptor and mapping inputs, generic SAI platform selection, ASIC traits, or exceptional platform-specific behavior.
user-invocable: true
argument-hint: <platform-name> [vendor] [asic-type]
---

# FBOSS Agent Platform Onboarding

Onboard new Agent platforms with the smallest maintainable change. A standard
platform on an existing ASIC is data-driven: add a `PlatformType`, platform
mapping inputs, and a platform descriptor. Do not copy legacy per-platform C++
classes or dispatch branches unless a concrete hardware behavior requires them.

## Start Here

Determine these facts from the request and checkout before editing:

| Input | Why it matters |
|---|---|
| Platform name and system vendor | Selects the config directory and generated descriptor path |
| FRUID product-name prefixes and `--mode` names | Drives runtime platform detection |
| `AsicType`, SAI implementation, and NPU count | Selects the generic platform and single-/multi-NPU generation |
| Base platform and variants | Identifies the smallest matching config example |
| Required behavior absent from generic classes | Determines whether any custom C++ is justified |

Read the current source before choosing enum values, build targets, or class
names. Never infer them from an old diff or hardcode a guessed “next” enum value.
Use the smallest existing platform with the same vendor, ASIC family, topology,
and variant shape as the structural example; copy only its structure, not its
board data.

## Choose the Path

| Situation | Path |
|---|---|
| Existing supported ASIC, no custom Agent behavior | Read `references/standard-platform.md` |
| New ASIC on an existing SAI implementation | Read `references/standard-platform.md`, then `references/asic-and-custom-behavior.md` |
| Custom platform or port behavior | Read `references/asic-and-custom-behavior.md`; keep everything else data-driven |
| New SAI vendor/backend | Treat SDK/backend integration as a separate prerequisite; do not disguise it as ordinary platform onboarding |
| Updating only an existing platform mapping | Stop this workflow and use the environment's existing-platform mapping-maintenance workflow |

## Scope: Agent Only

This workflow covers the Agent (`wedge_agent`) view of a platform. Bringing a
board up end to end also needs work that is out of scope here:

| Area | Where it lives |
|---|---|
| QSFP service and transceiver management | `fboss/qsfp_service`, `fboss/lib/bsp` |
| LED service, including a per-platform LED manager | `fboss/led_service` |
| Platform services config (platform_manager, sensors, fans, fw_util) | `fboss/configs/platforms/<vendor>/<platform>/platform_stack/` |
| BSP onboarding | The environment's BSP onboarding workflow |

Those areas legitimately need per-platform code that this skill's rules do not
cover; leave it alone rather than deleting it. Do not report the platform as
onboarded when only the Agent side is done—name the areas still outstanding.

## Required Invariants

- Preserve `PlatformType`; it is the stable identity used by downstream APIs.
- Put board wiring, profiles, SI settings, vendor config, identity, and variants
  in config inputs—not C++.
- A new standard platform must not add legacy detection branches,
  `PlatformMapping.cpp` JSON literals, dedicated SAI platform classes, or
  dedicated SAI port classes.
- ASIC-wide behavior belongs on the `HwAsic` trait. Platform-only exceptions
  must be narrow and justified by behavior the generic path cannot express.
- Generated JSON is output, not hand-authored source. Regenerate after every
  input change and review both the source inputs and generated result.
- `--platform_mapping_override_path` tests only an external mapping.
  `--platform_descriptor_config_path` tests descriptor-driven detection and
  mapping together; use the latter for the final onboarding validation.
- Do not commit, submit, publish, deploy, or modify a device unless the user
  requested that action.
- Use only verification commands from the selected verification reference or
  from current project documentation you opened during the task. Do not invent
  test targets, `run-getdeps.py` invocations, SDK selectors, or library paths.

## Verification Routing

For each reference pair below, load the `facebook/` version first if it exists
in the checkout. Otherwise load the `references/` version. The generic
reference is complete on its own; the override only supplies Meta-specific
commands.

| Need | Try first | Fallback |
|---|---|---|
| Generate, build, lint, and smoke-test | `facebook/verification.md` | `references/verification.md` |

Always finish with a concise matrix covering identity, mapping generation,
ASIC support, custom behavior, build/tests, and runtime smoke status. Separate
verified results from commands that still need to be run.
