# Platform Mapping Config Generation

## Introduction
This document gives an overview of the vendor-provided configuration files needed to run FBOSS binaries on new platforms. Meta uses these files to generate a "Platform Mapping" JSON (used by `wedge_agent` and `qsfp_service`) and an ASIC configuration file (used by `wedge_agent`). The Platform Mapping JSON is built into FBOSS binaries and encompasses all lane mappings for connections between NPUs, external phys, and transceivers. It is used for programming the ports with the appropriate settings and includes all system ports with their possible speeds, lane configurations, and signal integrity settings.

This page first outlines the required structure of each necessary configuration file, then outlines the expected workflow for external FBOSS vendors to provide these configuration files and validate their settings through building / running FBOSS binaries.


## Platform Configuration File Usage
![A flowchart showing which vendor provided source files are needed for different FBOSS services.](/img/developing/platform_mapping/platform_mapping_workflow_diagram.png)

We expect vendors to provide the source files in blue, run the parser / config generation tool, and validate their Platform Mapping JSON before adding it to the FBOSS codebase through a pull request.


## Source File Specification

### Static Mapping (Board Configuration)

File: `PLATFORM_static_mapping.csv` ([example](https://github.com/meta/fboss/blob/main/fboss/configs/platforms/celestica/montblanc/platform_mapping/montblanc_static_mapping.csv))

This config file contains swap, polarities, and other board layout-related information.
- Enumerates all pairs of A->Z pins on the board.
- Represents the bidirectional pins connecting different hardware components on the board irrespective of the speed or any other configuration.
- Contains information about the TX and RX lanes, including the lane ID and PN Swaps.

### Signal Integrity Settings

File: `PLATFORM_si_settings.csv` ([example](https://github.com/meta/fboss/blob/main/fboss/configs/platforms/celestica/montblanc/platform_mapping/montblanc_si_settings.csv))

This config file contains the signal integrity settings like pre, post, and main for every port / serdes speed combination.

- Defines signal integrity configuration for all the hardware components on the board.
- Provides the ability to define settings for different factors. For example:
  - Different settings for different lane speed.
  - Different settings for copper vs. optic.
  - Different settings for different copper cable lengths.
- Factors / settings not applicable for the board can be left empty.

### Port Profile Mapping

File: `PLATFORM_port_profile_mapping.csv` ([example](https://github.com/meta/fboss/blob/main/fboss/configs/platforms/celestica/montblanc/platform_mapping/montblanc_port_profile_mapping.csv))

This config file contains information about the ports that exist on the switch and what [profiles](https://github.com/meta/fboss/blob/main/fboss/agent/switch_config.thrift#L121) each port supports.

### Profile Settings

File: `PLATFORM_profile_settings.csv` ([example](https://github.com/meta/fboss/blob/main/fboss/configs/platforms/celestica/montblanc/platform_mapping/montblanc_profile_settings.csv))

This config file contains the information about what modulation, FEC, InterfaceType, etc., needs to be configured on different hardware components (NPU/XPHY) for different port speeds.

- Defines speed-specific settings. For example, for each speed, it defines the number of lanes used, FEC configuration that needs to be applied, Interface Type to be configured, etc.
- Each row is for an A->Z connection.
- For settings not applicable for any component, the cell should be left blank. For example, InterfaceType is not valid for the Transceiver component.

### Vendor Specific Configuration

File: `PLATFORM_vendor_config.json` ([example](https://github.com/meta/fboss/blob/main/fboss/configs/platforms/celestica/montblanc/platform_mapping/montblanc_vendor_config.json))

This config file contains vendor-specific configurations that are included in the `wedge_agent` configuration and passed directly to the SAI SDK during ASIC initialization. This file only contains information that cannot be derived from the other configuration files provided by the vendor. For example, this shouldn’t contain lane swap or polarity swap properties as that’s already derived from the static mapping file.

### Platform Descriptor (Config-Driven Platform Detection)

File: `PLATFORM_platform_descriptor.csv` ([example](https://github.com/meta/fboss/blob/main/fboss/configs/platforms/accton/wedge800bact/platform_mapping/wedge800bact_platform_descriptor.csv))

This config file describes the platform's identity so the agent can detect the platform and
select the right ASIC and mapping **at runtime**, without hardcoded `if/else` dispatch chains.
The generator turns this single-row CSV into a `platform_descriptor.json` file (see
[Platform Mapping JSON Generation Tool](#platform-mapping-json-generation-tool) below), which
is consumed by the `PlatformDescriptorRegistry` in the agent. See
[New Platform Support](https://meta.github.io/fboss/docs/developing/new_platform_support/)
for how the descriptor drives config-driven platform instantiation.

The CSV has a single data row with the following columns:

| Column Name | Definition | Valid Values |
|------------------------------------|-------------------------------------------------------------|-------|
| **System_Vendor** | System vendor (OEM) that builds the platform. Also determines the output subdirectory (`<system_vendor>/<platform_name>/`). | e.g. `accton`, `celestica`, `arista` |
| **Platform_Type** | The `PlatformType` enum name for this platform. | [enum names](https://github.com/meta/fboss/blob/main/fboss/lib/if/fboss_common.thrift) |
| **Product_Name_Prefixes** | One or more product/model name prefixes (from FRUID) used to detect this platform. Separate multiple values with the list delimiter. | e.g. `Wedge800BACT` |
| **Mode_Names** | One or more `--mode` flag values that map to this platform. | e.g. `wedge800bact` |
| **Asic_Type** | The `AsicType` enum name. The SAI backend (BCM vs Tajo) is derived from this. | [enum names](https://github.com/meta/fboss/blob/main/fboss/agent/switch_config.thrift) |

### CSV Column Definitions

| Column Name | Definition | Valid Values |
|------------------------------------|-------------------------------------------------------------|-------|
| **A/Z** | Represents endpoints of two same / different hardware components. Any component / endpoint can be represented as A or Z side. | A, Z |
| **A/Z_SLOT_ID** | Unique ID of the FRU Slot that the corresponding chip is on. This is either 1 (for a fixed system) or the corresponding PIM / Linecard ID. |  Range: >=1 |
| **A/Z_CHIP_ID** | Unique ID of the chip. For example, if there are two NPUs on the board, the chip IDs would be 1 and 2. Different chips can have the same ID. |  Range: >=1 |
| **A/Z_CHIP_TYPE** | String defining the chip. | [enum names](https://github.com/meta/fboss/blob/main/fboss/lib/platform_mapping_v2/platform_mapping_config.thrift#L13) |
| **A/Z_CORE_ID** | Unique ID of the core within the chip. | Range: >=0 |
| **A/Z_CORE_TYPE** | String defining the core. | [enum names](https://github.com/meta/fboss/blob/main/fboss/lib/platform_mapping_v2/platform_mapping_config.thrift#L20) |
| **A/Z_CORE_LANE** | Unique logical ID of the lane within the core. For example, if a NPU Core has 8 lanes cores, the core lane ranges from [0, 7]. Lanes of different core can have the same lane ID but the lane ID within a core should be unique. | Range: >=0 |
| **A/Z_PHYSICAL_TX/RX_LANE** | Physical lane / serdes ID within the core. | Chip dependent |
| **A/Z_TX/RX_POLARITY_SWAP** | Polarity swap of the physical serdes lane. | Y, N |
| **PORT_SPEED** | Speed for the port (MBPS). | [enum values](https://github.com/meta/fboss/blob/main/fboss/agent/switch_config.thrift) |
| **LOGICAL_PORTID** | Unique portID associated to a port within a chip. | Range: >=0 |
| **GLOBAL_PORTID** | Unique portID associated to a port across the chips. For example - if there are two NPUs each with 100 ports, logical_portIDs range from 1-100 on both NPUs whereas global_portID ranges from 1-200. | Range: >=0 |
| **MEDIA_TYPE** | If the column is empty, the setting will be applied for both copper and optic media type. | COPPER, OPTIC, (empty) |
| **CABLE_LENGTH** | Length of the cable in meters the setting needs to be applied for – only applicable for copper media type. If the column is empty, the setting will be applied for all copper cable lengths. | Range: >=0, (empty) |
| **A/Z_INTERFACE_TYPE** | Interface type of connection. | [enum names](https://github.com/meta/fboss/blob/main/fboss/lib/phy/phy.thrift#L79) |


## Platform Mapping Generation Workflow
### Platform Mapping JSON Generation Tool
We provide a script to help external FBOSS vendors generate their respective `platform_mapping.json` file.

#### Prerequisites

Python 3 is required in order to run the helper script. You can install it via one of the below commands depending on which Linux distribution you're on.

##### Debian

```shell
sudo apt update
sudo apt -y upgrade
sudo apt install python3
```

##### CentOS

```shell
sudo dnf upgrade -y
sudo dnf install python3
```

#### Instructions
Platform mapping config generation is done via running the helper script below from the root of the FBOSS repository.

```shell
$ ./fboss/lib/platform_mapping_v2/run-helper.sh --platform-name XXX
```

Below are the command line arguments that are relevant to this script.

| Argument                    | Description                                          |
|------------------------------------|-------------------------------------------------------------|
| --fboss-root (required) | Path to the `fboss/` source directory. The helper supplies `fboss` when it is run from the repository root. |
| --platform-name (required)  | Platform name that each CSV file has as prefix (e.g. montblanc in `montblanc_static_mapping.csv`).   |
| --input-dir | Platform config root containing vendor and platform directories (default: `FBOSS_ROOT/configs/platforms/`). |
| --output-dir  | Optional common directory for generated platform mappings. When omitted, output is written beside the inputs under `platform_mapping/generated/`. |
| --multi-npu  | Generates multi-NPU platform mapping config (default: `False`).   |


To use this command, place base platform inputs under
`<system_vendor>/<platform>/platform_mapping/`. Place variant-specific
overrides under
`<system_vendor>/<platform>/variants/<variant>/platform_mapping/`; omitted
files are inherited from the base platform. Each input directory uses the base
`PLATFORM` filename prefix:
- `PLATFORM_port_profile_mapping.csv`
- `PLATFORM_profile_settings.csv`
- `PLATFORM_si_settings.csv`
- `PLATFORM_static_mapping.csv`
- `PLATFORM_vendor_config.json`
- `PLATFORM_platform_descriptor.csv` (for config-driven platform detection)

Internal platform mapping inputs are kept under
`platforms/<vendor>/<platform>/facebook/platform_mapping/`, with generated
artifacts under its `generated/` subdirectory.

#### Generated Output

When a `PLATFORM_platform_descriptor.csv` is present, the tool emits two files
beside the inputs in `platform_mapping/generated/`:

- `platform_mapping.json` — the platform mapping described above.
- `platform_descriptor.json` — the platform identity (`platformType`, `productNamePrefixes`,
  `modeNames`, `asicType`) and the derived physical switch ASIC count
  (`numSwitchAsics`) consumed by the agent's `PlatformDescriptorRegistry`.

`numSwitchAsics` is derived by counting unique `(slot_id, chip_id)` pairs whose
chip type is `NPU` in the static mapping. Core and lane entries belonging to the
same physical ASIC do not increase the count.

Variant output is similarly written under
`variants/<variant>/platform_mapping/generated/`. When `--output-dir` is
provided, all mappings use the aggregate
`<output-dir>/<vendor>/<platform-or-variant>/platform_mapping.json`
layout.

Generated mappings for internal platform inputs are colocated under the same
`platforms/<vendor>/<platform>/facebook/platform_mapping/` path.


### Validating Platform Mapping JSON
The first step in validing your platform mapping source files is to ensure a valid JSON is created in `output-dir` by running the above tool.

The second step is using this platform mapping JSON to ensure `qsfp_service` and `wedge_agent` binaries are brought up correctly. We typically have platfom mapping JSONs embedded into our FBOSS binaries, but to enable faster testing for external users, you can run both binaries with the `--platform_mapping_override_path` flag followed by the filepath to your platform mapping JSON – e.g. `./qsfp_service --platform_mapping_override_path /tmp/generated_platform_mappings/VENDOR/PLATFORM/platform_mapping.json`.

To exercise the full config-driven path (external platform mapping **and** descriptor-based
platform detection), point the binaries at the descriptor config root instead, using the
`--platform_descriptor_config_path` flag – e.g.
`./wedge_agent --platform_descriptor_config_path /tmp/generated_platform_mappings`. The root
must follow the `<root>/<system_vendor>/<platform_name>/` layout produced by the generator,
containing both `platform_descriptor.json` and `platform_mapping.json`. When this flag is empty
(the default), the agent uses the legacy embedded mapping and hardcoded detection instead.


### Updating FBOSS Code to Use New Platform Mapping
Please refer to [New Platform Support](https://meta.github.io/fboss/docs/developing/new_platform_support/) for instructions on how to incorporate this new Platform Mapping file within the FBOSS codebase.

If you are introducing a new platform, please also add your platform name to [verify_generated_files.py](https://github.com/meta/fboss/blob/0f1f4840dde66923f294fbabe79492d4a970a0ef/fboss/lib/platform_mapping_v2/test/verify_generated_files.py#L26).
