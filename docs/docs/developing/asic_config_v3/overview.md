---
sidebar_position: 1
---

# Overview

The `asic_config_v3` tool, located at `fboss/lib/asic_config_v3/`, reads inputs
from `fboss/configs/` and generates the per-platform ASIC configuration that a
switch ASIC needs at SDK and SAI initialization time. It is the data-driven
successor to the code-driven `asic_config_v2` implementation. Whereas
`asic_config_v2` required a dedicated Python module for every platform,
`asic_config_v3` describes each platform in JSON data files that are processed
by a generic generator shared by all platforms of an ASIC family. As a result,
adding support for a new platform on an existing ASIC family requires no code
changes.

Two ASIC families are currently supported, each with its own generator and
output format:

| Family | Generator | Output format | Output file |
|---|---|---|---|
| [Broadcom XGS](./broadcom_xgs.md) | `BroadcomXgsGenerator` | Multi-document YAML with typed tables such as `PC_PM_CORE`, `PC_PORT`, and `global` | `<variant>.yml` |
| [Broadcom DNX](./broadcom_dnx.md) | `BroadcomDnxGenerator` | Flat SOC property key/value pairs in the JSON representation of the thrift `AsicConfig` struct | `<variant>.json` |

The two ASIC families share the same framework. Platform discovery, variants,
schema validation, wiring data from `platform_mapping_v2`, and conditional
settings all work the same way in both. The ASIC families differ in how
settings are layered and in the structure of the output. The
[Broadcom XGS](./broadcom_xgs.md) and [Broadcom DNX](./broadcom_dnx.md)
pages describe these differences, and the [Examples](./examples.md) page
walks through adding platforms and variants.

## Design

### Input layers

The configuration input is split into layers according to who owns the data.
Each layer is a JSON file:

| Layer | Families | File | Contents |
|---|---|---|---|
| OCP SAI common | XGS | `fboss/configs/asic_vendors/common/ocp_sai_common.json` | Vendor-agnostic SAI defaults shared by all vendors |
| Vendor family common | XGS | `fboss/configs/asic_vendors/<vendor>/<family>/sdk_common.json` and `sai_common.json` | SDK and SAI settings shared by all ASICs in a family |
| Per-ASIC | XGS and DNX | `fboss/configs/asic_vendors/<vendor>/<family>/asics/<asic>.json` | Data intrinsic to the chip |
| Platform | XGS and DNX | `fboss/configs/platforms/<system_vendor>/<platform>/asic_config/asic_config.json` | Board-specific data, such as the ASIC selection, configuration variants, and platform-level overrides |

The DNX family has no family-wide common files; everything above the platform
layer lives in its per-ASIC file.

Port wiring information is not duplicated in the files listed above. The
generator derives the lane and polarity maps for both families, as well as the
XGS-only logical-to-physical port mapping, from the platform mapping data
maintained under
`fboss/configs/platforms/<system_vendor>/<platform>/platform_mapping/`.

What the per-ASIC file contains differs by family. For XGS it declares the
port architecture, output table names, global defaults, SAI overrides,
pass-through data blocks, and ASIC-wide conditional settings. For DNX it
declares the SOC key suffix and key format strings, the core types to include
in wiring generation, always-emitted base SDK settings, and declarative
tables. Both schemas are documented in the
[Schema Field Reference](./schema_field_reference.md).

### Generation pipeline

The entry point, `gen.py`, discovers every
`platforms/*/*/asic_config/asic_config.json`
file under `fboss/configs/`, looks up the appropriate generator in a registry
keyed on the ASIC vendor and ASIC names, and runs that generator once per
[variant](#platform-configuration-and-variants):

```python
_GENERATOR_REGISTRY = {
    ("broadcom", "tomahawk5"): BroadcomXgsGenerator,
    ("broadcom", "tomahawk6"): BroadcomXgsGenerator,
    ("broadcom", "jericho3"): BroadcomDnxGenerator,
}
```

Each family applies its input layers in a fixed order that is the same for
every platform and variant. When two steps write the same key, the later step
wins, so the step table on each family page is also in ascending
precedence order.

### Schema validation

The configuration files are validated against the JSON Schemas stored in the
`schemas/` directory (`platform_config.schema.json`,
`broadcom_xgs_asic_config.schema.json`, `broadcom_dnx_asic_config.schema.json`,
`conditional_setting.schema.json`, and `vendor_common.schema.json`). The
schemas reject unknown keys in most
places, so a new configuration key with a description must be added to the
corresponding schema in the same change. Run the validator with:

```shell
python3 -m fboss.lib.asic_config_v3.test.validate_asic_configs_schemas \
  --fboss-root "$PWD/fboss"
```

The schemas document every field and are the fastest way to look up valid keys
and values; the [Schema Field Reference](./schema_field_reference.md) page
summarizes the important ones. The schemas are read only by the validator,
never by the generator, so a schema change by itself never alters the
generated output.

### Running the generator

Run the helper script from the root of the repository:

```shell
./fboss/lib/asic_config_v3/run-helper.sh
```

Output files are written beside each platform input under
`fboss/configs/platforms/<system_vendor>/<platform>/asic_config/generated/`,
one per platform variant.
The file extension is determined by the family, `.yml` for XGS and `.json`
for DNX, matching the output formats in the family table at the top of this
[page](#overview). To generate a single platform, pass the `--platform <name>`
argument.

### Verifying generated output

Generated output is verified by comparing it against known-good reference
configurations.

- The committed files under
  `fboss/configs/platforms/<system_vendor>/<platform>/asic_config/generated/`
  are the permanent regression references. Regenerating with an unchanged
  input configuration must reproduce them exactly.
- During the migration from `asic_config_v2`, a platform's output is
  additionally compared against the v2 references. The output must match the
  files under `fboss/lib/asic_config_v2/generated_asic_configs/` exactly,
  and the synced references under
  `fboss/lib/asic_config_v2/synced_asic_configs/` semantically.

## Configuration details

### Conditional settings

Conditional settings express feature toggles as data instead of code
branches. The mechanism is shared by both ASIC families: the condition
grammar is defined once in `schemas/conditional_setting.schema.json` and
evaluated by the base generator class. Each family's generator declares the
effects it can execute. Each entry carries a name, a condition, and one or
more effects. Unconditional data does not belong here; it has dedicated
mechanisms instead (pass-through settings on XGS; `base_sdk_settings` and
`platform_sdk_overrides` on DNX).

A condition names a parameter from the variant's `asic_config_params` or
`features` section and exactly one operator: `equals`, `not_equals`, `in`,
`not_in`, `starts_with`, or `not_starts_with`. A parameter the variant does
not declare evaluates as null, and each negative operator is the strict
complement of its positive counterpart, so an absent parameter satisfies
every negative operator.

An effect names what the entry does when its condition holds. The `apply`
effect writes settings to a named output target, which is an output table
for XGS and an output section for DNX. The `apply_from` effect copies a
named settings block from the ASIC file into an output target. The
`skip_from_sai_common` effect omits keys from the vendor SAI common layer
and is meaningful only for XGS, which is the only family that layers that
block. Validation is split between the schema and the generator. The schema
validator checks the structure of every entry, including operator and
operand types. Each generator additionally declares the effects it supports
(all three for XGS, and `apply` and `apply_from` for DNX) and, when it is
constructed,
rejects an entry that declares any other effect, has a malformed effect
payload, targets an unknown output table or section, or applies a value of
the wrong type for its output format. The generator check covers every
entry, not only those whose condition currently holds, so an invalid entry
cannot remain latent until another variant activates it. Operand types are
checked only by the schema validator, so rerun it after editing conditions.

Entries are evaluated once per variant. ASIC-level entries come before
platform-level entries, each in array order, so a platform value overrides
a chip-level one. Within an entry, `apply_from` executes before `apply`, so
an inline setting overrides the copied block. Each generator executes the
effects at the point in its pipeline where they belong: XGS collects
`skip_from_sai_common` before it layers the vendor SAI common block and
writes `apply` and `apply_from` settings in its conditional-settings step,
while DNX writes them in its final step.

The following XGS entry from the Tomahawk5 file illustrates the mechanism:

```json
{
  "name": "mmu_lossless",
  "description": "Enables MMU lossless mode for PFC and RDMA workloads.",
  "condition": {
    "source": "asic_config_params",
    "param": "mmu_lossless",
    "equals": true
  },
  "apply": {
    "global": {
      "sai_mmu_custom_config": 1,
      "sai_rdma_udf_disable": 1,
      "sai_l3_byte1_udf_disable": 1,
      "clm_enable": 1
    },
    "TM_THD_CONFIG": {
      "THRESHOLD_MODE": "LOSSY_AND_LOSSLESS",
      "SKIP_BUFFER_RESERVATION": 1
    }
  },
  "skip_from_sai_common": [
    "sai_mmu_qgroups_default",
    "sai_optimized_mmu"
  ]
}
```

The `apply_from` effect serves the same purpose as `apply` but avoids
repeating a large settings block inside the entry: the entry names a
top-level block of the ASIC file and the output target to copy it into. The
following Tomahawk5 entry copies the chip's `dlb_defaults` block into the
`global` table when the variant enables the `generate_dlb_config` feature:

```json
"dlb_defaults": {
  "ecmp_dlb_port_speeds": 1,
  "l3_ecmp_member_secondary_mem_size": 4096
},
"conditional_settings": [
  {
    "name": "dlb_config",
    "description": "Enables Dynamic Load Balancing settings for ECMP groups.",
    "condition": {
      "source": "features",
      "param": "generate_dlb_config",
      "equals": true
    },
    "apply_from": {
      "source": "dlb_defaults",
      "target_table": "global"
    }
  }
]
```

Because `apply_from` executes before `apply` within an entry, an entry may
copy a block with `apply_from` and then override individual keys of that
block with an inline `apply`.

The following DNX entry from the meru800bia platform file selects the
single-stage port map for every port configuration that is not a dual-stage
topology:

```json
{
  "name": "single_stage_port_map",
  "condition": {
    "param": "port_config",
    "not_starts_with": "dual_stage"
  },
  "apply": {
    "common": {
      "fabric_connect_mode.BCM8889X": "FE",
      "ucode_port_0.BCM8889X": "CPU.0:core_0.0"
    }
  }
}
```

On DNX, scenario selection is expressed entirely through conditional settings
on the `asic_config_params` values. The `config_gen_type` parameter selects
the generation profile, such as production or hardware test. The
`port_config` parameter selects the port-map profile by exact name and the
topology family by prefix. The `multistage_role` parameter selects the
multistage fabric role. Because scenario selection happens through these
conditional settings, the bucketed `vendor_config.json` files that
`asic_config_v2` consumed through `platform_mapping_v2` are unnecessary;
`asic_config_v3` does not read them.

### Platform configuration and variants

Each platform file declares the platform identity (`platform_name`, `vendor`,
and `asic`), which selects both the per-ASIC data file and the generator to
use. It then declares a `defaults` block together with one or more named
`variants`. A variant represents one deliverable configuration for the
platform, and the generator produces one output file per variant, named
`generated/<variant>.<extension>` beside the platform input. The enclosing
platform directory provides the platform identity, so every variant name must
be non-empty.

The effective configuration for a variant is formed by merging the variant's
settings on top of the `defaults` block. The merge is recursive for nested
objects. An object present in both places has its keys combined, with the
variant winning for any key declared on both sides. Scalar values and lists
are not combined; a variant that declares one replaces the default value
outright. This arrangement keeps shared settings in one place and lets each
variant declare only what makes it different. A platform with a single variant
typically keeps all of its settings in `defaults` and leaves the variant
entry empty.

A variant name is a non-empty, free-form label chosen by the platform author;
it is not drawn from a fixed set and the generator does not interpret it. The
names in use today (`default`, `rack`, `chassis`, `test_fixture`, and so on) are
identifiers only, and the name determines the generated filename. There is no
need to reuse a name from another platform, and adding a variant never requires
a code change.

### Variant fields

A `defaults` block or a variant may declare any of the variant fields listed
in the [Schema Field Reference](./schema_field_reference.md). Every field is
optional, and a variant only needs to declare the fields that differ from the
platform defaults. Most variant fields belong to a single family. For
example, the `port_config` object, `cpu_port`, and `platform_sai_overrides`
are XGS fields, while `platform_sdk_overrides` and the `port_config`,
`multistage_role`, and `hyper_port` parameters inside `asic_config_params`
belong to DNX.

Because the generator does not act on the variant name, two variants produce
different output only through these fields. The parameters that switch
behavior on and off are `asic_config_params` and `features`. A
`conditional_settings` entry names one of their values as its condition and
applies extra settings when it matches, as described in
[Conditional settings](#conditional-settings). To give a variant new
behavior, set the relevant parameter or feature flag and add the matching
conditional setting; no new variant "type" or generator change is involved.
The `asic_config_params` that currently drive output through conditional
settings are `mmu_lossless`, `exact_match`, and `config_gen_type` on XGS, and
`config_gen_type`, `port_config`, and `multistage_role` on DNX.

### Overriding a setting outside these fields

A setting that no variant field covers can usually still be overridden
without a code change.

On XGS:

- A key in the `global` output table can be set through
  `platform_sai_overrides`, which takes precedence over all the other layered
  `global` sources (steps 1 through 6 of the [XGS pipeline](./broadcom_xgs.md) table).
- A key in the `DEVICE_CONFIG` table can be set through
  `device_config_overrides`.
- A table fed by a pass-through block can be replaced in its entirety by
  declaring the block at the variant level, as described in
  [Pass-through settings](./broadcom_xgs.md#pass-through-settings).
- Any other output table the ASIC declares can be modified through a
  platform-level `conditional_settings` entry whose `apply` block names the
  table. The generator rejects an entry that targets a table not listed in
  the ASIC's `table_names` when it is constructed.

On DNX:

- An unconditional SOC property can be set through
  `platform_sdk_overrides`, which overrides `base_sdk_settings` and the
  declarative tables.
- A scenario-dependent SOC property belongs in a platform-level
  `conditional_settings` entry applying to `common`. Conditional settings
  are the highest-priority step of the DNX pipeline and therefore also
  override generated wiring keys.

In both ASIC families, a setting that requires computation or new structure (for
example, new port mapping logic) needs a generic, data-driven extension of
the generator and the schema rather than a platform-specific branch in the
code.
