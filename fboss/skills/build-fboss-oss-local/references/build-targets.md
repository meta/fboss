# Build targets and what they produce

## How the CMake build is organised

The top-level `CMakeLists.txt` reads its options, declares the generated
Thrift libraries, and then globs and `include()`s every file under `cmake/`.
They are `include()`d, not added as subdirectories, so **everything shares one
directory scope and one global target namespace**. The one-file-per-source-
directory convention (`fboss/foo/bar` is built by `cmake/FooBar.cmake`) is
organisational only.

Two consequences:

- CMake does not error on an unknown name in `target_link_libraries`. It emits
  `-l<name>` to the linker instead, so a typo shows up much later as
  `cannot find -lfoo` or a missing symbol.
- The glob has no `CONFIGURE_DEPENDS`, so **adding a new `cmake/*.cmake` file
  is not noticed by an incremental build**. Re-run the configure step.

## Aggregate targets

Pass one with `--cmake-target`. These are the user-facing entry points. The
contents drift; `CMakeLists.txt` and `cmake/*.cmake` are authoritative.
`--cmake-target` also accepts any single CMake target, for example `fboss2`,
which is the fastest way to build one binary.

| Target | Contents |
|---|---|
| `fboss_platform_services` | `platform_manager`, `sensor_service`, `fan_service`, `data_corral_service`, `rackmon`, `fw_util`, `weutil`, `reboot_cause_finder`, `watchdog_util`, `fixmyfboss`, `sensor_service_client`, plus their tests |
| `qsfp_targets` | `qsfp_service`, `wedge_qsfp_util`, `qsfp_hw_test`, `qsfp_hal_test`, `cmis_test`, `transceiver_manager_test`, `transceiver_state_machine_test` |
| `led_targets` | `led_service`, `led_service_hw_test` |
| `fboss2_targets` | `fboss2`, `fboss2-dev`, `fboss2_cmd_test`, `fboss2_cmd_config_test`, `fboss2_framework_test`, `fboss2_integration_test` |
| `fsdb_all_services` | `fsdb` and its tests and benchmarks |
| `fboss_forwarding_stack` | the agent binaries plus `diag_shell_client`, `fboss2`, `fboss2-dev`, `fsdb`, `qsfp_service`, `wedge_qsfp_util` |
| `fboss_other_services` | qsfp + led + fboss2 + fsdb aggregates combined |
| `fboss_platform_mapping_gen` | the platform-mapping generator |
| `fboss_exported_libraries` | the library set intended for downstream consumers |

Fake-SAI only (they do not exist unless `BUILD_SAI_FAKE` is set):

| Target | Contents |
|---|---|
| `fboss_fake_agent_targets` | `fboss_sw_agent`, `wedge_agent-sai_impl`, `fboss_hw_agent-sai_impl`, `sai_replayer-fake`, Thrift libraries |
| `fboss_fake_agent_test_targets` | `sai_test-fake`, `sai_agent_hw_test-fake`, `sai_agent_scale_test-fake`, `sai_invariant_agent_test-fake`, `multi_switch_agent_hw_test`, `switch_test` |
| `fboss_fake_agent_benchmarks` | also needs `BUILD_SAI_FAKE_BENCHMARKS` |

`fboss_forwarding_stack` is only *defined* when a SAI implementation or fake
SAI is available. Ask for it with neither and CMake reports an unknown target
rather than something helpful.

Omitting `--cmake-target` entirely builds everything (the default target is
`install`), which is substantially slower.

## Service binaries

| Binary | What |
|---|---|
| `fboss_sw_agent` | control-plane agent process |
| `fboss_hw_agent-sai_impl` | ASIC-facing agent process |
| `wedge_agent-sai_impl` | single-process agent |
| `qsfp_service` | optics / transceiver management |
| `fsdb` | state database |
| `led_service` | LEDs |
| `platform_manager`, `sensor_service`, `fan_service`, `data_corral_service`, `rackmon`, `weutil`, `fw_util`, `reboot_cause_finder`, `watchdog_util`, `fixmyfboss` | platform services |
| `fboss2`, `fboss2-dev` | CLI |

Utility binaries include `wedge_qsfp_util`, `diag_shell_client`,
`fboss_pai_diag_shell_client`, `sensor_service_client`, `sai_replayer-*`, and
several Python zipapps such as `fboss-platform-mapping-gen` and
`fboss-asic-config-gen`.

## <a id="binary-naming"></a>Binary naming

- Agent service binaries **always** carry the `-sai_impl` suffix, including in
  a fake-SAI build. This is deliberate: it keeps them at the same installed
  paths so service unit files and packaging do not have to differ between fake
  and real builds. There is no `wedge_agent-fake`.
- **Test** binaries do get the suffix that matches the implementation:
  `sai_test-fake` versus `sai_test-sai_impl`.
- Multi-switch test binaries are not suffixed at all and are built
  unconditionally: `multi_switch_agent_hw_test`,
  `multi_switch_agent_scale_test`, `multi_switch_invariant_agent_test`.

Because the name does not change, **size is how you tell a real-SDK binary from
a fake one.** In one build tree, reconfiguring from a real Broadcom SDK to fake
SAI took `fboss_hw_agent-sai_impl` from roughly 1.3 GB to roughly 140 MB. If
you expected a real SDK build and got a small binary, the SDK was not linked
in. This also illustrates the one-implementation-per-tree rule: the second
configure replaced the first, it did not coexist with it.

## Build options

These are read from **environment variables** as well as `-D`. Each `option()`
is followed by an environment override, which means a stale exported variable
from an earlier shell can win over what you think you set.

| Variable | Effect |
|---|---|
| `BUILD_SAI_FAKE` | build the in-tree fake SAI implementation and the fake targets |
| `SAI_BRCM_IMPL` / `SAI_TAJO_IMPL` / `CHENAB_SAI_SDK` / `SAI_BRCM_PAI_IMPL` | select a vendor implementation (mutually exclusive) |
| `SAI_SDK_VERSION` | the SDK selector token; becomes a bare `-D<token>` |
| `SAI_VERSION` | SAI spec version; split into `SAI_VER_MAJOR/MINOR/RELEASE` |
| `BENCHMARK_INSTALL` | build and install benchmark binaries |
| `BUILD_SAI_FAKE_BENCHMARKS`, `BUILD_SAI_FAKE_LINK_TEST` | extra fake-SAI targets |
| `SKIP_ALL_INSTALL` | build without running install rules |
| `WITH_ASAN` | ASAN + UBSAN build of FBOSS and folly only; also disables the jemalloc injection. Use `run-getdeps.py --asan`, which sets it and instruments the other dependencies too |
| `FBOSS_BUILD_PROFILE` | `full` (default), `fsdb_client`, or `exported_libraries` |
| `GITHUB_ACTIONS_BUILD` | adds one extra aggregate target used by CI |

`FBOSS_BUILD_PROFILE` prunes the default build down to a subset, so a
downstream consumer that only needs the exported libraries does not compile
agent, SAI, qsfp, platform, LED and the test trees. Non-`full` profiles
require a newer CMake than the file's stated minimum.

## Per-platform and per-ASIC support

There is **no per-platform switch**. Every platform mapping library and every
ASIC is compiled in unconditionally and linked into the platform and SAI
libraries. Selecting a platform happens at run time via configuration, not at
build time.

## Adding a source file

Because the internal and open-source build definitions are maintained as
hand-written twins, a new `.cpp` must be added to the right `cmake/*.cmake`
target explicitly, usually the one whose source list holds its neighbouring
files. Nothing generates one build definition from the other.
`fboss/oss/scripts/buck_cmake_dep_checker.py` can diff them:

```bash
python3 fboss/oss/scripts/buck_cmake_dep_checker.py              # dependency consistency
python3 fboss/oss/scripts/buck_cmake_dep_checker.py --check-files
python3 fboss/oss/scripts/buck_cmake_dep_checker.py --check-sorted
```

Run it from the repository root (the directory containing `cmake/`). It is a
regex-based tool, not a build-graph analysis, so treat a clean run as weak
evidence: it cannot see targets generated by macros, it skips anything under
an `oss/` or `facebook/` path component by design, and its parser silently
misses targets whose definition does not match its expected formatting.
Check `.pre-commit-config.yaml` for whether its hook is enabled.

## Header-only and SAI-linked targets

Two patterns to copy when adding targets:

- A header-only library must set `LINKER_LANGUAGE` explicitly or CMake cannot
  determine the language.
- Every SAI-touching target needs its own `set_target_properties(...
  COMPILE_FLAGS "-DSAI_VER_MAJOR=... -DSAI_VER_MINOR=... -DSAI_VER_RELEASE=...")`
  block. These are per-target, not global; omitting them produces confusing
  preprocessor errors in version-gated code.

Note also that the SAI switch library is intentionally linked with unresolved
symbols allowed, because the SAI implementation is supplied by whatever links
last. Undefined SAI symbols there are expected; a real problem only surfaces
at final executable link time.
