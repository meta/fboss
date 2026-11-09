# M4062NHP platform mapping (config-driven model)

Input CSVs consumed by the platform_mapping_v2 generator to migrate M4062NHP
(Nexthop, Broadcom Tomahawk 6 / Peregrine) onto the config-driven "standard
platform" onboarding path (`--platform_descriptor_config_path`), replacing the
manual per-platform agent C++.

Generator: `buck2 run @//mode/opt fbcode//fboss/lib/platform_mapping_v2/facebook:gen`
Output:    `generated/{platform_mapping.json,platform_descriptor.json}`

## File status

| File | Status |
|------|--------|
| `m4062nhp_platform_descriptor.csv` | DONE — identity + ASIC (`ASIC_TYPE_TOMAHAWK6`), product-name prefix `M4062NHP`, mode `m4062nhp`, vendor `nexthop` |
| `m4062nhp_static_mapping.csv` | HEADER ONLY — needs vendor NPU core-lane -> transceiver-lane map + TX/RX polarity swaps |
| `m4062nhp_port_profile_mapping.csv` | HEADER ONLY — needs front-panel port names + supported port-profile IDs per port |
| `m4062nhp_profile_settings.csv` | HEADER ONLY — needs per-profile speed/lane/FEC/modulation/interface-type rows |
| `m4062nhp_si_settings.csv` | HEADER ONLY — needs vendor SerDes / signal-integrity (TX FIR taps, RX) values |
| `m4062nhp_vendor_config.json` | SKELETON — adjust ASIC vendor knobs as needed |

Column formats mirror `../wedge800bact/` (the reference migrated BCM platform);
data-row values are TH6-specific and must come from Nexthop's hardware spec.

## Empty-first bring-up (current state)

The header-only CSVs generate cleanly — an empty `platform_mapping.json`
(`ports: {}`) plus a correct `platform_descriptor.json`. This is functionally
equivalent to the old empty `{}` agent C++ stub, but on the config-driven path:
the descriptor lets the agent detect M4062NHP and build `GenericSaiBcmPlatform`.
`m4062nhp` is registered (single-NPU) in both:
- `test/verify_generated_files.py` `_OSS_MULTI_NPU_SUPPORTED_PLATFORMS[False]`
- `facebook/gen.py` `_MULTI_NPU_SUPPORTED_PLATFORMS[False]`

so the checked-in `generated/` JSONs stay in sync with CI.

## Remaining steps once vendor data lands

1. Fill the HEADER-ONLY CSVs above with Nexthop hardware data.
2. Re-run the generator:
   `buck2 run @//mode/opt fbcode//fboss/lib/platform_mapping_v2/facebook:gen`
   and commit the regenerated `generated/` JSONs.
3. Package the descriptor tree into the agent (and optionally qsfp/led) fbpkg so
   it is present at the `--platform_descriptor_config_path` root at runtime.
