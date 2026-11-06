---
sidebar_position: 3
---

# Broadcom DNX

The DNX generator builds a single flat map of SOC properties, each with a
string key and a string value. All settings are
written to the `common` section of the output, which is described in
[Output structure](#output-structure).

The shared design is described in the [Overview](./overview.md).

## Generation pipeline

| Order | Setting | Source |
|---|---|---|
| 1 | Base SDK settings | `asic_vendors/broadcom/dnx/asics/<asic>.json` (`base_sdk_settings`) |
| 2 | [Declarative tables](#declarative-tables) for port speeds, TM port headers, DTM flow regions, and flow remote cores | `asic_vendors/broadcom/dnx/asics/<asic>.json` (`declarative_tables`) |
| 3 | Platform SDK overrides | `platforms/<system_vendor>/<platform>/asic_config/asic_config.json` (`platform_sdk_overrides`) |
| 4 | Lane and polarity keys | computed from `platform_mapping_v2` connections and the ASIC `core_types`, `key_formats`, and `num_lanes_per_core`, plus the ASIC `default_polarity_settings` |
| 5 | [Conditional settings](./overview.md#conditional-settings), ASIC entries then platform entries, each in array order | `asic_vendors/broadcom/dnx/asics/<asic>.json` (`conditional_settings`), then `platforms/<system_vendor>/<platform>/asic_config/asic_config.json` (`conditional_settings`) |

The lane and polarity keys in step 4 reproduce the wiring that
`asic_config_v2` computed in per-platform Python code. The generator iterates
the platform's `platform_mapping_v2` connections and keeps the connection
ends whose core type the ASIC declares in `core_types`. For each remaining
connection it computes the SOC lane number as
`core_id * num_lanes_per_core + core_lane` and renders each key through the
ASIC's `key_formats` strings. Core types that are not declared, for example
the Jericho 3 recycle and eventor ports, are skipped automatically.

## Output structure

The DNX output file is the JSON representation of the thrift `AsicConfig`
struct, which is defined in `fboss/agent/hw/config/asic_config_v2.thrift`.
The struct consists of a `common` entry holding a key/value `config` map,
together with an optional `npuEntries` map for platforms with more than one
NPU. The platform file declares which structure to produce through the
top-level `output_structure` field:

```json
"output_structure": {"mode": "common_only"}
```

- The `common_only` mode is for single-NPU platforms such as meru800bia.
  Every generated key is written to `common.config`. This is the only
  supported mode.
- Support for multi-NPU platforms, which distribute the per-chip wiring
  keys for lanes and polarity into per-NPU sections while the
  chip-independent settings remain in `common.config`, is planned as a
  further mode.

The number of NPUs is a property of the board rather than of the ASIC, which
is why `output_structure` is declared in the platform file and not in the
per-ASIC file.

## ASIC data

The per-ASIC file, for example
`fboss/configs/asic_vendors/broadcom/dnx/asics/jericho3.json`,
declares the data intrinsic to the chip. It contains the following groups of
fields.

- **Key formats and suffixes.** The `asic_suffix` value, for example
  `BCM8889X`, is appended to generated SOC keys. The `key_formats` object
  holds the format strings for the lane map and polarity keys, with
  `{family}`, `{lane}`, and `{suffix}` placeholders. The `core_types` object
  maps each `platform_mapping_v2` core type the chip exposes, for example
  `J3_NIF` and `J3_FE`, to the key families used when rendering its wiring
  keys.
- **Base SDK settings.** The `base_sdk_settings` map holds the SDK settings
  emitted for every platform and variant of this chip. Keys carry their SOC
  suffix inline.
- **Default polarity settings.** The `default_polarity_settings` map holds
  the default polarity keys that carry no lane number. Jericho 3 emits
  them. An ASIC without such keys omits the field.
- **Declarative tables.** These are described in the next section.

### Declarative tables

Declarative tables capture the fixed tables that `asic_config_v2` generated
in per-ASIC Python code:

| Table | Emits | Notes |
|---|---|---|
| `port_speed_map` | `port_init_speed_*` keys | Interface-type-to-speed entries, emitted verbatim. |
| `tm_port_headers` | `tm_port_header_type_{in,out}_*` keys | Emitted verbatim for every variant. Topology-specific header sets are selected through conditional settings. |
| `dtm_flow_region_map` | `dtm_flow_mapping_mode_region_<N>` keys | Expanded from `key_format`, `start_region`, `count`, and `value`. |
| `flow_remote_cores` | `dtm_flow_nof_remote_cores_region*` keys | Emitted verbatim. |
