---
sidebar_position: 5
---

# Schema Field Reference

This page summarizes the fields defined by the JSON Schemas under
`fboss/lib/asic_config_v3/schemas/`. The schemas are the authoritative
reference. They define the complete structure of each configuration file, and
every field includes a description.

The configuration files contain two kinds of keys.

- **Framework fields** are the fields listed on this page. They are declared
  in the schemas and interpreted directly by the generator. Introducing a new
  framework field requires adding it to the corresponding schema.
- **Setting maps** are sections such as `platform_sai_overrides`,
  `platform_sdk_overrides`, `device_config_overrides`, the XGS ASIC-level
  `global_defaults` and `sai_overrides`, or the DNX ASIC-level
  `base_sdk_settings`, whose keys are vendor SDK and SAI setting names, port
  speeds, or output table entries. The schemas deliberately accept any key
  inside a setting map, because the set of valid vendor settings cannot be
  enumerated, so adding a setting there requires no schema change.

## Platform Configuration

Platform configuration, defined by `platform_config.schema.json`. This schema
validates every
`fboss/configs/platforms/<system_vendor>/<platform>/asic_config/asic_config.json`
file, for both the XGS and DNX families. Fields that apply to a single family are marked with
the family name.

### Top-level fields

| Field | Required | Description |
|---|---|---|
| `platform_name` | yes | Platform identifier. Must match the platform directory name. |
| `vendor` | yes | ASIC vendor identifier. Must match a directory name under `fboss/configs/asic_vendors/`. |
| `asic` | yes | ASIC chip identifier. Must match the basename of a JSON file under `fboss/configs/asic_vendors/<vendor>/<family>/asics/`. |
| `num_ports_per_core` | no | XGS. Number of front-panel ports per ASIC core. Combined with the ASIC's lanes-per-core value to derive the number of lanes per port. |
| `num_lanes_per_port` | no | XGS. Explicit override of the derived lanes-per-port value, for platforms that wire only a subset of the lanes. |
| `output_structure` | no | DNX. Declares how generated keys are distributed across the thrift `AsicConfig` struct. The only supported `mode` is `common_only`, for single-NPU platforms. |
| `defaults` | no | Shared settings inherited by every variant. A variant's own settings take precedence over the corresponding defaults. |
| `variants` | yes | Named configuration variants. Each variant produces a separate output file. |

### Variant fields

Each entry under `variants`, as well as the `defaults` block, may declare the
following fields.

| Field | Description |
|---|---|
| `asic_config_params` | Parameters controlling generator behavior, listed in the next table. |
| `port_config` | XGS. Front-panel port settings: `default_speed` (in Mbps), `speed_to_fec` (mapping from speed to FEC mode), `enable` (the value emitted for `PC_PORT.ENABLE`), and `pc_port_overrides` (extra `PC_PORT` key/value lines emitted verbatim, for example `LINK_TRAINING`). This object is distinct from the DNX `port_config` string inside `asic_config_params`. |
| `cpu_port` | XGS. CPU port settings: `speed` (in Mbps) and `num_lanes`. |
| `mgmt_port` | XGS. Management port settings: `enabled` (whether the port is emitted at all), `enable` (the `PC_PORT.ENABLE` value), `in_port_block` (include the port in the `PORT` MTU range), and `speed_variants` (per-speed settings keyed by speed). |
| `mgmt_port_overrides` | XGS. Platform overrides of the ASIC-level management port defaults: `logical_id`, `physical_id`, `speed`, `num_lanes`, and `fec`. |
| `port_mapping_overrides` | XGS. Adjustments to the logical-to-physical port mapping formula: `core_range` (which cores participate, for example `"0-15,48-63"`), `num_lp_ports_on_even_core`, `num_logical_ports_per_datapath`, `lp_start_step_offset`, `lp_offset_simple`, `special_core_offset_apply_all`, and `exclude_logical_ports` (logical port IDs omitted from `PC_PORT_PHYS_MAP`, `PC_PORT`, and `PORT`, for front-panel ports that are not populated on the platform). |
| `platform_sai_overrides` | XGS. Platform-level SAI settings. These take precedence within the layered `global` sources and override the ASIC-level SAI overrides. |
| `platform_sdk_overrides` | DNX. Platform-level SOC properties applied on top of the ASIC `base_sdk_settings` and declarative tables. Keys carry their SOC suffix inline. |
| `device_config_overrides` | XGS. Key/value pairs written to the `DEVICE_CONFIG` output table, for example clock frequencies. |
| `features` | Boolean toggles for optional configuration sections: `generate_dlb_config`, `generate_dlb_ecmp_config`, `generate_low_clock_freq_settings`, and `generate_autoload_board_settings`. Absent flags default to disabled. |
| `conditional_settings` | Platform-level conditional settings, evaluated after the ASIC-level ones so that platform values take precedence. |
| `preamble_file` | XGS. Path, relative to the `asic_config_v3` directory, of a preamble file prepended to the generated YAML. Honored only when the ASIC declares `preamble_support: true`. |
| `platform_mapping_name` | Overrides the `platform_mapping_v2` directory consulted for lane-map and polarity data. Defaults to `platform_name`. Useful when several variants share one `asic_config.json` but consume sibling platform mapping directories. |
| `ctr_eflex_config` | XGS. Variant-level override of the ASIC's `ctr_eflex_config` (enhanced flex counter) pass-through block. When present, it replaces the ASIC-level value in its entirety. |

### `asic_config_params`

| Parameter | Description |
|---|---|
| `config_type` | Output configuration format: `YAML_CONFIG` (XGS), `KEY_VALUE_CONFIG` (DNX), or `JSON_CONFIG`. |
| `config_gen_type` | Generation profile, `DEFAULT` or `HW_TEST`. Conditional settings in both ASIC families may select on it. |
| `exact_match` | XGS. Enables exact-match forwarding table entries. |
| `mmu_lossless` | XGS. Enables MMU lossless mode for PFC and RDMA workloads. |
| `port_config` | DNX. Port-map profile name, for example `default` or `18*400G-4`. Conditional settings select the profile by exact name and the topology family by prefix, for example `dual_stage`. |
| `multistage_role` | DNX. Multistage fabric role, for example `NONE`, `FAP`, `FE13`, or `FE2`. Conditional settings select the settings for the role. |
| `hyper_port` | DNX. Boolean gate for hyper-port conditional settings. |

In addition to the fields above, the XGS generator honors a variant-level
override of any data block routed by the ASIC's `pass_through_settings`.
Declaring `flex_counter_settings`, `port_config_defaults`,
`fp_config_defaults`, or `tm_thd_config_defaults` in a variant replaces the
corresponding ASIC-level block, in the same way as `ctr_eflex_config`. The
schema currently declares only `ctr_eflex_config` among these.

The platform schema accepts unknown keys at the top and variant levels, so a
key that is not listed here does not fail validation; the generator ignores it
without an error or warning. When a setting appears to have no effect, verify
the field name against this reference.

## Broadcom XGS ASIC Configuration

Broadcom XGS ASIC configuration, defined by
`broadcom_xgs_asic_config.schema.json`. This schema validates the per-ASIC
files under `fboss/configs/asic_vendors/broadcom/xgs/asics/`.

| Field | Description |
|---|---|
| `vendor`, `asic` | Chip identity, for example `broadcom` and `tomahawk5`. |
| `port_architecture` | Core, lane, and datapath layout consumed by the port mapping logic: `num_cores`, `num_lanes_per_core`, `num_logical_ports_per_datapath`, `num_physical_ports_per_datapath`, and `num_lp_ports_on_even_core_default`. |
| `mmu_size` | MMU buffer size in bytes. Also used as the `MAX_FRAME_SIZE` and port MTU values. |
| `table_names` | Ordered list of output YAML table names. Controls which tables appear in the output and their order. |
| `mgmt_port_defaults` | Default management port settings: `logical_id`, `physical_id`, `speed`, `num_lanes`, and `fec`. Platforms may override these per variant. |
| `preamble_support` | Whether the ASIC supports a preamble section prepended to the output YAML. When `false`, any `preamble_file` declared by a platform is ignored. |
| `global_defaults` | ASIC-specific SDK settings copied into the `global` output table. |
| `sai_overrides` | ASIC-specific SAI settings layered on top of the vendor SAI common values. |
| `flex_counter_settings`, `ctr_eflex_config`, `port_config_defaults`, `fp_config_defaults`, `tm_thd_config_defaults` | Named data blocks routed into output tables by `pass_through_settings`. |
| `dlb_defaults`, `dlb_ecmp_config_defaults` | Dynamic Load Balancing settings, applied through conditional settings gated by the corresponding feature toggles. |
| `pass_through_settings` | Declarative routing list. Each entry names a `source` data block in this file and the `target_table` it is copied into. |
| `conditional_settings` | ASIC-wide conditional settings, described below. |

## Broadcom DNX ASIC Configuration

Broadcom DNX ASIC configuration, defined by
`broadcom_dnx_asic_config.schema.json`. This schema validates the per-ASIC
files under `fboss/configs/asic_vendors/broadcom/dnx/asics/`.

| Field | Required | Description |
|---|---|---|
| `vendor`, `asic` | yes | Chip identity, for example `broadcom` and `jericho3`. |
| `asic_family` | yes | Always `dnx`. |
| `config_type` | yes | Always `KEY_VALUE_CONFIG`. DNX output is the key/value SOC property map carried in `AsicConfigEntry.config`. |
| `asic_suffix` | yes | SOC key suffix appended to generated keys, for example `BCM8889X`. |
| `additional_asic_suffixes` | no | Further SOC key suffixes appearing in this ASIC's settings data, for example `BCM8889X_ADAPTER` for the adapter target. Listed for documentation. Settings data carries these suffixes inline. |
| `num_lanes_per_core` | yes | Serdes lanes per core. The generator flattens a core identifier and core lane pair into the SOC lane number as `core_id * num_lanes_per_core + core_lane`. |
| `core_types` | yes | The `platform_mapping_v2` core types whose connections are traversed for wiring keys. Each core type is mapped to a `lane_map_family` and a `polarity_family` token, which select the key families used when rendering its wiring keys. Connections with undeclared core types are skipped. |
| `key_formats` | yes | Format strings for the generated wiring keys: `lane_map`, `lane_map_value`, `polarity_rx`, and `polarity_tx`, with `{family}`, `{lane}`, `{suffix}`, `{rx}`, and `{tx}` placeholders. |
| `default_polarity_settings` | no | Default polarity keys that carry no lane number, emitted verbatim, for example `phy_rx_polarity_flip.BCM8889X`. Jericho 3 declares them. An ASIC without such keys omits the field. |
| `base_sdk_settings` | no | ASIC-wide SOC properties emitted for every platform and variant. |
| `declarative_tables` | no | Fixed ASIC tables: `port_speed_map`, `tm_port_headers`, `dtm_flow_region_map` (`key_format`, `start_region`, `count`, and `value`), and `flow_remote_cores`. |
| `conditional_settings` | no | ASIC-level conditional settings, evaluated before platform-level ones. |

## Conditional Setting Entries

Conditional setting entries are defined once, in
`conditional_setting.schema.json`, and referenced by the platform, Broadcom
XGS, and Broadcom DNX schemas. Every `conditional_settings` list therefore
validates against the same structure. Whether a generator can execute a
given effect is checked by the generator itself when it is constructed.

| Field | Required | Description |
|---|---|---|
| `name` | yes | Identifier, unique within the surrounding list. |
| `description` | no | Free-form explanation of the setting. |
| `condition` | yes | The test evaluated against the variant configuration. Its keys are listed below. |
| `apply` | no | Settings written when the condition holds, keyed by output target: a table name for XGS, a section name (`common`) for DNX. Values are integers or strings for XGS and strings for DNX. |
| `apply_from` | no | Copies the named `source` settings block of the ASIC file into the named `target_table` when the condition holds. Executes before `apply` within the same entry. Supported by both families. |
| `skip_from_sai_common` | no | Keys omitted from the vendor SAI common layer while the condition holds. Supported by XGS only. |

An entry must declare at least one effect. The schema enforces the structure
of an entry, including operator and operand types. In addition, a generator
rejects an entry that declares an effect it does not support, has a
malformed effect payload, targets an output table or section that does not
exist, applies an empty settings block, or applies a value of the wrong type
for its output format.

The `condition` object names the parameter to test and exactly one operator.
A parameter the variant does not declare evaluates as null, and each negative
operator is the strict complement of its positive counterpart.

| Key | Description |
|---|---|
| `source` | The variant-config section holding the parameter, either `asic_config_params` or `features`. Defaults to `asic_config_params` when omitted. |
| `param` | The name of the parameter to test. |
| `equals` | Satisfied when the parameter equals this string, number, or boolean. |
| `not_equals` | Satisfied when the parameter does not equal this value, including when the parameter is absent. |
| `in` | Satisfied when the parameter equals one of the values in this non-empty array. |
| `not_in` | Satisfied when the parameter equals none of the values in this non-empty array, including when the parameter is absent. |
| `starts_with` | Satisfied when the parameter is a string starting with this prefix. |
| `not_starts_with` | Satisfied when the parameter is not a string starting with this prefix, including when the parameter is absent or not a string. |

## Vendor Common Configuration

Vendor common configuration, defined by `vendor_common.schema.json`. This
schema validates the family-wide common files, such as
`fboss/configs/asic_vendors/broadcom/xgs/sdk_common.json` and
`fboss/configs/asic_vendors/broadcom/xgs/sai_common.json`. The DNX family has no family-wide
common files.

| Field | Required | Description |
|---|---|---|
| `global` | yes | SDK or SAI settings copied into the `global` output table according to the layering order described on the [Broadcom XGS](./broadcom_xgs.md) page. |
