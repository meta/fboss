# Standard Config-Driven Platform

Use this path when the platform's ASIC and SAI implementation already exist and
the generic platform and port classes provide the required behavior.

## 1. Confirm the Generic Path

Read the current vendor factory before editing:

- `fboss/agent/platforms/sai/SaiPlatformInitBcm.cpp`
- `fboss/agent/platforms/sai/SaiPlatformInitTajo.cpp`
- `fboss/agent/platforms/sai/SaiPlatformInitYangra.cpp`

Confirm that an otherwise unknown `PlatformType` selects the generic platform
and port for that SAI implementation. If it does, do not add a platform-specific
factory branch.

Also confirm the ASIC is constructible through `HwAsic::makeAsic()` and already
implements the behavior needed by the new board. If not, follow
`asic-and-custom-behavior.md` before returning here.

## 2. Add the Stable Platform Identity

Add one unique enum value to `PlatformType` in
`fboss/lib/if/fboss_common.thrift`. Inspect the current enum and choose the next
valid value; never reuse a retired value or guess from documentation.

For a standard descriptor-driven platform, do not add the platform to:

- `PlatformProductInfo.cpp` legacy product/mode chains
- `PlatformMode` conversion switches
- `PlatformMappingUtils.cpp` compiled-in mapping switches
- a new `*PlatformMapping.cpp` JSON wrapper
- a new SAI platform or port class

Those are legacy/custom extension points, not routine onboarding steps.

## 3. Add Platform Mapping Inputs

Create:

```text
fboss/configs/platforms/<vendor>/<platform>/platform_mapping/
```

Use the smallest same-ASIC, same-topology platform as the file-shape example.
The normal source set is:

```text
<platform>_static_mapping.csv
<platform>_si_settings.csv
<platform>_port_profile_mapping.csv
<platform>_profile_settings.csv
<platform>_vendor_config.json
<platform>_platform_descriptor.csv
```

Add `<platform>_integrated_transceiver_mapping.csv` only when the hardware has
integrated/CPO optics. Populate every file from the platform's actual board and
SDK data; never retain values merely because they appeared in the example.

Add a `README.md` beside the inputs. Existing platforms carry one, and it is
how a multi-diff bring-up stays legible. Record the generator command, the
output paths, and a per-file status table saying which inputs hold real vendor
data and which are still stubs, plus the steps left before the platform is
complete. Update it whenever an input changes status.

The descriptor is a single data row:

```csv
System_Vendor,Platform_Type,Product_Name_Prefixes,Mode_Names,Asic_Type
<vendor>,PLATFORM_<NAME>,<fruid-prefix>,<mode>,ASIC_TYPE_<ASIC>
```

Multiple product prefixes or mode names use the generator's `-` list
delimiter. Avoid ambiguous prefixes: registry matching is prefix-based, and
descriptor discovery order must not decide between two platforms.

For variants, add the optional `Variant_Attributes` column. Use semicolon-
separated boolean gflag requirements such as `test_fixture=true;rack=false`.
Keep one unqualified/default descriptor when the platform must work without a
variant flag. Variant directories live under:

```text
fboss/configs/platforms/<vendor>/<platform>/variants/<variant>/platform_mapping/
```

## 4. Register and Generate

Add the base platform name and every variant identifier to the correct `False`
(single-NPU) or `True` (multi-NPU) list in
`OSS_MULTI_NPU_SUPPORTED_PLATFORMS` in
`fboss/lib/platform_mapping_v2/gen.py`. Variant directories are generated as
separate platform aliases, so omitting one also omits its regression coverage.
This shared registry drives the generated-file verification test.

From the open-source repository root, generate the artifacts:

```bash
./fboss/lib/platform_mapping_v2/run-helper.sh \
  --platform-name <platform> [--multi-npu]
```

Run the command separately for the base platform and each variant identifier.
The checked-in output belongs beside the corresponding inputs:

```text
fboss/configs/platforms/<vendor>/<platform>/platform_mapping/generated/
  platform_mapping.json
  platform_descriptor.json
```

The generator derives `numSwitchAsics` from unique physical NPU entries in the
static mapping. Do not hand-edit it in generated JSON.

## 5. Inspect the Generated Contract

Before building, verify:

- `platformType`, `productNamePrefixes`, `modeNames`, and `asicType` match the
  source descriptor.
- `numSwitchAsics` matches the physical topology.
- Every mapped port has the intended controlling port, profiles, lane order,
  polarity, and SI settings.
- Variant output lands under the variant directory and selects the intended
  descriptor attributes.
- Regenerating produces no unexplained changes to other platforms.

## 6. Point the Runtime at the Descriptor Tree

Generating the data is not enough—the services have to be told to load it. The
open-source default configs live in:

```text
fboss/oss/scripts/run_configs/default_configs/<platform>/
  agent.conf
  qsfp.conf
  num_hw_agents
```

In `agent.conf`, add `platform_descriptor_config_path` to the
`defaultCommandLineArgs` object—a JSON key, not a `--` flag—pointing at the
descriptor-tree root the services will read. Do the same in `qsfp.conf` when
QSFP service also consumes the descriptor, and set its `mode` to the
descriptor's `Mode_Names` value. Set `num_hw_agents` to the platform's NPU
count. Copy the file shape from the same reference platform used for the
mapping inputs, and take the values from this platform's topology.

Without this, the platform builds and generates cleanly but fails at startup:
`initPlatformMapping` has no compiled-in mapping to fall back on for any
platform newer than wedge800, and throws.

Then follow the selected verification reference from `SKILL.md`.
