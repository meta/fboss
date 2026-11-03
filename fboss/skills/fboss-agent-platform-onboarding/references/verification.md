# Open-Source Verification

Run the narrowest checks that prove the platform's generated data and runtime
selection. Do not report a command as passed unless its output was observed.

## Generate

From the FBOSS repository root:

```bash
./fboss/lib/platform_mapping_v2/run-helper.sh \
  --platform-name <platform> [--multi-npu]
```

For a temporary aggregate descriptor tree, add:

```bash
--output-dir /tmp/platform_descriptors
```

The resulting runtime root must contain:

```text
/tmp/platform_descriptors/<vendor>/<platform>/platform_descriptor.json
/tmp/platform_descriptors/<vendor>/<platform>/platform_mapping.json
```

## Generated-File Regression Check

Use the OSS helper to build and run the installed no-regression test:

```bash
python3 fboss/lib/oss/run-helper.py \
  --target platform_mapping_gen_no_regression_test
```

If the local container or SDK setup uses a different command, inspect the
current OSS build documentation and CMake targets rather than guessing flags.

## Build

Build the `wedge_agent` variant for the actual SAI implementation and SDK only
when the checkout's current OSS build documentation supplies the command and
the required SDK inputs are known. Otherwise report that build as pending.
Never synthesize a `run-getdeps.py` command, SDK selector, library path, or test
target from memory.

When C++ was added for a new ASIC or custom behavior, find its owning build and
test targets in the current checkout. Run those exact targets or report them as
pending; do not generalize a target name from a nearby platform.

## Runtime Smoke Test

Deploy the generated descriptor tree without changing its vendor/platform
layout. Start the Agent with:

```text
--platform_descriptor_config_path <descriptor-root>
```

Use a real FRUID product name when available; separately exercise `--mode` if
mode-based detection is supported. Verify from startup output or state that:

1. The expected `PlatformType` was selected.
2. The expected ASIC and generic vendor platform were constructed.
3. The generated mapping was loaded from the descriptor tree.
4. The reported switch count matches `numSwitchAsics`.
5. Cold boot, warm boot, representative port profiles, and link state work.
6. Any custom behavior or new-ASIC trait added by the change is exercised
   directly.

`--platform_mapping_override_path` is useful for isolating mapping problems,
but it does not validate descriptor discovery. Do not use it as the only final
test.

## Final Report

Report:

| Area | Evidence |
|---|---|
| Identity | Enum and descriptor values |
| Generated data | Generator command and changed artifacts |
| ASIC/generic path | Existing trait or new tested trait; selected generic/custom class |
| Build | Exact successful targets or commands |
| Runtime | Detection, mapping load, cold/warm boot, port/link results |
| Remaining work | Explicit blockers or commands not run |
