---
sidebar_position: 4
---

# Examples

The walkthroughs on this page build on one another. The general design is
described in the [Overview](./overview.md), and the family specifics on
the [Broadcom XGS](./broadcom_xgs.md) and [Broadcom DNX](./broadcom_dnx.md)
pages.

## Adding an XGS platform

No Python changes are required to add a platform on an ASIC family that the
tool already supports. Suppose a new Tomahawk5 board named `newboard` is being
added.

1. Ensure that `fboss/configs/platforms/<system_vendor>/newboard/platform_mapping/` exists and
   contains the platform's static mapping and vendor data (see the
   [platform mapping documentation](../platform_mapping.md)).
2. Create
   `fboss/configs/platforms/<system_vendor>/newboard/asic_config/asic_config.json`.
   The following is a complete example declaring a single variant; an existing
   platform on the same ASIC is a good starting template:

```json
{
  "platform_name": "newboard",
  "vendor": "broadcom",
  "asic": "tomahawk5",
  "num_ports_per_core": 2,
  "defaults": {
    "asic_config_params": {
      "config_type": "YAML_CONFIG",
      "config_gen_type": "DEFAULT",
      "exact_match": false,
      "mmu_lossless": false
    },
    "port_config": {
      "default_speed": 400000,
      "speed_to_fec": {
        "100000": "PC_FEC_RS544",
        "200000": "PC_FEC_RS544_2XN",
        "400000": "PC_FEC_RS544_2XN",
        "800000": "PC_FEC_RS544_2XN"
      }
    },
    "cpu_port": {
      "speed": 10000,
      "num_lanes": 1
    },
    "mgmt_port": {
      "enabled": true,
      "speed_variants": {
        "100000": { "num_lanes": 4, "fec": "PC_FEC_RS528" }
      }
    },
    "features": {
      "generate_dlb_config": true,
      "generate_autoload_board_settings": true
    }
  },
  "variants": {
    "newvariant": {}
  }
}
```

3. Validate and generate:

```shell
python3 -m fboss.lib.asic_config_v3.test.validate_asic_configs_schemas \
  --fboss-root "$PWD/fboss"
./fboss/lib/asic_config_v3/run-helper.sh
```

The output appears beside the input as
`generated/newvariant.yml`.

If the platform needs a setting that no listed field covers, see
[Overriding a setting outside these fields](./overview.md#overriding-a-setting-outside-these-fields).

## Adding a DNX platform

Adding a platform on the DNX family follows the same pattern as on XGS. The
fields under the `defaults` section and in the variants are ASIC family
specific. The meru800bia platform file at
`fboss/configs/platforms/arista/meru800bia/asic_config/asic_config.json`
is a complete reference. The following example shows the overall structure:

```json
{
  "platform_name": "newdnxboard",
  "vendor": "broadcom",
  "asic": "jericho3",
  "output_structure": {"mode": "common_only"},
  "defaults": {
    "asic_config_params": {
      "config_type": "KEY_VALUE_CONFIG",
      "config_gen_type": "DEFAULT",
      "port_config": "default",
      "multistage_role": "NONE"
    },
    "platform_sdk_overrides": {
      "appl_param_local_system_port_voq_connector_start": "93184"
    },
    "conditional_settings": [
      {
        "name": "port_profile_default",
        "condition": {"param": "port_config", "equals": "default"},
        "apply": {"common": {"ucode_port_8.BCM8889X": "CDGE4_0:core_0.2"}}
      },
      {
        "name": "prod_sdk_settings",
        "condition": {"param": "config_gen_type", "equals": "DEFAULT"},
        "apply": {"common": {"dpp_db_path": "/etc/packages/neteng-fboss-wedge_agent/current/db"}}
      }
    ]
  },
  "variants": {
    "default": {}
  }
}
```

The following steps complete the platform.

1. Ensure that `fboss/configs/platforms/<system_vendor>/<name>/platform_mapping/`
   contains the platform's complete `platform_mapping_v2` data set: static
   mapping, port profile mapping, profile settings, and SI settings. The
   platform mapping parser loads them together, and the generator derives
   every lane and polarity key from the static mapping.
2. Declare the SOC properties that are specific to this board and apply to
   every variant in `platform_sdk_overrides`.
3. Declare the port-map profile entries, such as the `ucode_port_*` keys, in
   a conditional setting keyed on `port_config`. Declare per-scenario
   settings keyed on `config_gen_type`, and multistage-role settings keyed
   on `multistage_role`, as applicable.
4. Validate, generate, and compare the output against the platform's
   reference in `fboss/lib/asic_config_v2/synced_asic_configs/`. The output
   appears beside the input as `generated/<variant>.json`.

## Adding a variant

A variant is added by inserting one entry under `variants`; everything not
declared in it is inherited from `defaults`. The mechanism is the same for
both families. Continuing the XGS example from
[Adding an XGS platform](#adding-an-xgs-platform),
the following adds a `newvariant2` variant that enables MMU lossless mode
while the `newvariant` variant keeps the defaults:

```json
"variants": {
  "newvariant": {},
  "newvariant2": {
    "asic_config_params": {
      "mmu_lossless": true
    }
  }
}
```

The next generator run produces two output files, `newvariant.yml` and
`newvariant2.yml`. In the `newvariant2` output, `mmu_lossless`
satisfies the Tomahawk5 conditional setting of the same name, so the lossless MMU settings are
applied and the corresponding SAI common keys are suppressed; all other
settings are identical to `newvariant` because the merge with `defaults` fills
in every field the variant does not declare.

Another common pattern is a variant per hardware configuration, where each
variant reads its platform mapping data from a different `platform_mapping`
directory:

```json
"variants": {
  "rack": {
    "platform_mapping_name": "newboard_rack"
  },
  "test_fixture": {
    "platform_mapping_name": "newboard_test_fixture"
  }
}
```

## Adding a new ASIC family

Supporting a new ASIC family, such as a different vendor or a different
product line of an existing vendor, requires one new generator plus its data
files. The Broadcom DNX family was added following these steps, and its
files can be used as a reference.

1. **Generator.** Add a generator module under `generators/` that subclasses
   `BaseAsicConfigGenerator`. The subclass declares `SUPPORTED_EFFECTS` and
   implements `generate()`, `output_extension`, and the conditional-setting
   hooks `_apply_settings`, `_validate_apply_target`, and
   `_validate_apply_value`. Reuse the shared patterns where possible. The
   variant and defaults merge, the `asic_config_params` handling, and
   condition evaluation come from the base class, platform mapping data comes
   from the platform mapping parser, and feature toggles use conditional
   settings.
   Keep the behavior data-driven and avoid per-platform branches.
2. **Vendor data.** Add
   `fboss/configs/asic_vendors/<vendor>/<family>/asics/<asic>.json` with
   the chip-intrinsic data the generator needs. Add family-wide common files
   only if several ASICs in the family share those settings. The DNX family
   has no family-wide common files.
3. **Registration.** Add the new vendor and ASIC pair to
   `_GENERATOR_REGISTRY` in `gen.py`.
4. **Schema.** Add a schema for the new ASIC file under `schemas/` and
   register it in `test/validate_asic_configs_schemas.py`. Extend
   `platform_config.schema.json` if the family introduces new platform-level
   fields. The DNX family added `output_structure`,
   `platform_sdk_overrides`, and new `asic_config_params` parameters.
5. **Build.** Add the new generator source to
   `cmake/AsicConfigV3ConfigCli.cmake`, and add a `python_library` target
   for it in `fboss/lib/asic_config_v3/BUCK`, wired into the `gen` binary's
   dependencies.
6. **Platform.** Add
   `fboss/configs/platforms/<system_vendor>/<name>/asic_config/asic_config.json`
   selecting the new vendor and ASIC.
7. **Verification.** Compare the generated output against a known-good
   reference configuration for that family. For ASIC families with synced
   references, the match must be exact.
