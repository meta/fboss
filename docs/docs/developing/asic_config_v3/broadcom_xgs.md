---
sidebar_position: 2
---

# Broadcom XGS

This page describes the Broadcom XGS specifics of `asic_config_v3`: the
generation pipeline and the pass-through settings mechanism. The shared
design is described in the [Overview](./overview.md).

## Generation pipeline

The XGS generator builds named YAML tables. Steps 1 through 6 all feed the
`global` table and form a single priority chain. The remaining steps mostly
write to their own tables.

| Order | Setting | Source, in priority order | Output table |
|---|---|---|---|
| 1 | OCP SAI common | `asic_vendors/common/ocp_sai_common.json` (`global`) | `global` |
| 2 | ASIC global defaults | `asic_vendors/broadcom/xgs/asics/<asic>.json` (`global_defaults`) | `global` |
| 3 | Vendor SDK common | `asic_vendors/broadcom/xgs/sdk_common.json` (`global`) | `global` |
| 4 | Vendor SAI common, minus keys listed in an active `skip_from_sai_common` | `asic_vendors/broadcom/xgs/sai_common.json` (`global`) | `global` |
| 5 | ASIC SAI overrides | `asic_vendors/broadcom/xgs/asics/<asic>.json` (`sai_overrides`) | `global` |
| 6 | Platform SAI overrides | `platforms/<system_vendor>/<platform>/asic_config/asic_config.json` (`platform_sai_overrides`) | `global` |
| 7 | [Pass-through settings](#pass-through-settings) | `asic_vendors/broadcom/xgs/asics/<asic>.json`, or `platforms/<system_vendor>/<platform>/asic_config/asic_config.json` when the variant redefines a block | `PORT_CONFIG`, `FP_CONFIG`, `TM_THD_CONFIG`, `CTR_EFLEX_CONFIG`, `global` |
| 8 | [Conditional settings](./overview.md#conditional-settings), ASIC entries then platform entries, each in array order | `asic_vendors/broadcom/xgs/asics/<asic>.json` (`conditional_settings`), then `platforms/<system_vendor>/<platform>/asic_config/asic_config.json` (`conditional_settings`) | tables named by each entry, for example `global`, `TM_THD_CONFIG`, `DEVICE_CONFIG` |
| 9 | Device configuration overrides | `platforms/<system_vendor>/<platform>/asic_config/asic_config.json` (`device_config_overrides`) | `DEVICE_CONFIG` |
| 10 | Port mapping, port configuration, lane map, and polarity map | computed from `platform_mapping_v2`, the ASIC `port_architecture` and `mgmt_port_defaults`, and the platform `port_config`, `cpu_port`, `mgmt_port`, `mgmt_port_overrides`, and `port_mapping_overrides` | `PC_PM_CORE`, `PC_PORT_PHYS_MAP`, `PC_PORT`, `PORT` |

## Pass-through settings

Pass-through settings route a named data block from the ASIC file into an
output table declaratively, without any dedicated code:

```json
"pass_through_settings": [
  {"source": "flex_counter_settings", "target_table": "global"},
  {"source": "ctr_eflex_config", "target_table": "CTR_EFLEX_CONFIG"}
]
```

A platform variant may declare a block under the same source key to replace
the ASIC-level block entirely, which lets a platform emit different values
than the chip-wide defaults.

:::note

The DNX family does not use pass-through settings. Its output has no named
tables, and unconditional data blocks are expressed directly as
`base_sdk_settings`, `declarative_tables`, or `platform_sdk_overrides`.

:::
