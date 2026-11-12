# FBOSS Requirements for LPO Transceivers

## Scope

This document describes what FBOSS needs from a Linear Pluggable Optics (LPO)
transceiver, and how a platform adds support for one. It covers the signal
integrity (SI) settings in the platform mapping, the transceiver vendor
overrides that select them, and the `qsfp_service` config that enables them.

## What is an LPO

A conventional optical transceiver has a DSP that retimes the electrical signal
from the host before driving the optics, and retimes the received signal before
sending it back to the host. That DSP isolates the host SerDes from the channel,
so the same host SI settings work for any module of the same type.

An LPO has no DSP and no retimer. The module uses linear analog drivers and
receivers, so the host SerDes drives the optical channel almost directly. This
lowers power and latency, but it means:

- The host SerDes SI settings (TX FIR taps) have to be tuned for the specific
  LPO part, and often per lane, instead of using a generic optics setting.
- The module's own TX input equalization (driver peaking) has to be set by the
  host to match the host SerDes and board trace.

Note that deriving and setting driver peaking might not be necessary for an
optimal signal, and it can be bypassed if the TX FIR taps update provides
enough improvement for the signal integrity.

FBOSS handles both: `wedge_agent` programs the host SerDes from the platform
mapping, and `qsfp_service` programs the module's driver peaking.

## Transceiver Requirements

- The module must be CMIS managed.
- The module must advertise the LPO applications FBOSS uses to identify an LPO.
  A CMIS module is treated as LPO when its advertised applications are exactly
  the following host and media interface pairs:

  | Host interface (FBOSS) | Host interface code | SFF-8024 name | Media interface |
  |------------------------|---------------------|---------------|-----------------|
  | `LPO_100G`             | `0x20`              | LEI-100G-PAM4-1 (LPO MSA) | `100G-FR1` |
  | `LPO_400G`             | `0x22`              | LEI-400G-PAM4-4 (LPO MSA) | `400G-FR4` |
  | `LPO_800G`             | `0x23`              | LEI-800G-PAM4-8 (LPO MSA) | `800G-FR8` |

  The host interface codes are the LPO entries of the "Host Electrical
  Interface IDs" table (Table 4-5) in
  [SFF-8024](https://www.snia.org/technology-communities/sff/specifications),
  which defines the host electrical interface codes for LPO transceivers.

  A module that advertises a different set is programmed as a regular CMIS
  optic, and its driver peaking is not set. Current LPOs are expected to
  support 100G, 400G and 800G. If a new LPO supports a different set of host
  interfaces than the ones defined here, the vendor shall update the expected
  set (`expectedLpoInterfaces` in `CmisModule::isLpoModule()`, in
  [`CmisModule.cpp`](https://github.com/meta/fboss/blob/main/fboss/qsfp_service/module/cmis/CmisModule.cpp))
  to match.
- The module must support host-controlled TX input equalization through CMIS
  Explicit Control, using the fixed TX input equalization target
  (`FixedInputEqTargetTx`, page 10h bytes 156-159, 4 bits per lane).
- The vendor name and part number in the module EEPROM must be stable. FBOSS
  selects the SI settings by an exact match on both.

## Platform SI Settings

LPO SI settings live in the platform's SI settings CSV, alongside the
non-LPO settings. See
[`montblanc_si_settings.csv`](https://github.com/meta/fboss/blob/main/fboss/configs/platforms/celestica/montblanc/platform_mapping/montblanc_si_settings.csv)
for an example, and [Platform Mapping Config Generation](../developing/platform_mapping.md)
for the full file format.

A row applies to a transceiver only when these three columns are set:

| Column | Description |
|--------|-------------|
| **TCVR_VENDOR** | Vendor name of the transceiver the row applies to. Use a placeholder name (for example `Vendor_1`), not the real vendor name. See [Transceiver Vendor Overrides](#transceiver-vendor-overrides). |
| **TCVR_PART_NUM** | Part number of the transceiver the row applies to. Use a placeholder (for example `PartNum_1`), not the real part number. |
| **DRIVER_PEAKING** | TX input equalization value that `qsfp_service` writes to this lane of the module. Range 0-15 (4 bits). |

If `TCVR_VENDOR` is set, `TCVR_PART_NUM` and `DRIVER_PEAKING` are required and
the platform mapping generator fails without them. Rows with these columns empty
are the default settings, used for any transceiver that no LPO row matches.

The rest of the row works as for any other SI setting: `LANE_SPEED(mbps)` and
`MEDIA_TYPE` select which port profiles the row applies to, and the `TX_*`
columns hold the host SerDes settings for that lane. These are rows from
`montblanc_si_settings.csv`, for the first four lanes of core 0 at 100G per lane:

```csv
SLOT_ID,CHIP_ID,CHIP_TYPE,CORE_ID,CORE_TYPE,CORE_LANE,LANE_SPEED(mbps),MEDIA_TYPE,TCVR_VENDOR,TCVR_PART_NUM,DRIVER_PEAKING,TCVR_MEDIA,NIC_VENDOR,CABLE_LENGTH(m),TX_PRE3,TX_PRE2,TX_PRE1,TX_MAIN,TX_POST1,TX_POST2,TX_POST3,RX_CTLE_CODE,RX_DSP_MODE,RX_AFE_TRIM,TX_PRECODING,RX_PRECODING,RX_REACH
1,1,NPU,0,TH5_NIF,0,100000,BACKPLANE,Vendor_1,PartNum_1,2,,,,0,9,-30,105,-12,-9,0,,,,1,1,1
1,1,NPU,0,TH5_NIF,1,100000,BACKPLANE,Vendor_1,PartNum_1,2,,,,0,7,-29,106,-14,-10,0,,,,1,1,1
1,1,NPU,0,TH5_NIF,2,100000,BACKPLANE,Vendor_1,PartNum_1,2,,,,0,8,-29,106,-14,-8,0,,,,1,1,1
1,1,NPU,0,TH5_NIF,3,100000,BACKPLANE,Vendor_1,PartNum_1,2,,,,0,7,-28,102,-22,-7,0,,,,1,1,1
```

After editing the CSV, regenerate the platform mapping with
`fboss/lib/platform_mapping_v2/run-helper.sh --platform-name <platform>`. Each
LPO row becomes a `portConfigOverrides` entry whose factor carries the
placeholder vendor, with the host SerDes settings in `pins` and the per-lane
driver peaking in `driverPeaking`:

```json
{
  "factor": {
    "ports": [1],
    "profiles": [38],
    "vendor": {"name": "Vendor_1", "partNumber": "PartNum_1", ...}
  },
  "pins": {
    "iphy": [
      {
        "id": {"chip": "NPU-TH5_NIF-slot1/chip1/core0", "lane": 0},
        "tx": {"pre": -30, "pre2": 9, "main": 105, "post": -12, "post2": -9, ...}
      },
      ...
    ]
  },
  "driverPeaking": {"0": 2, "1": 2, "2": 2, "3": 2}
}
```

## Transceiver Vendor Overrides

The SI CSV and the generated platform mapping are open source, so they use a
placeholder vendor and part number instead of the real ones. A placeholder can
also stand for several part numbers that share the same SI settings, so one set
of rows covers all of them.

The mapping from a placeholder to the real transceivers is a transceiver vendor
override:

- **placeholder**: the vendor and part number used in the SI CSV
  (`TCVR_VENDOR`, `TCVR_PART_NUM`).
- **transceivers**: the real vendor names and part numbers, as read from the
  module EEPROM, that the placeholder stands for.

When FBOSS looks up the SI settings for a port, an override entry with a
placeholder vendor matches only if the plugged-in transceiver's vendor name and
part number exactly match one of the `transceivers` listed for that placeholder.
`wedge_agent` uses the match to pick the host SerDes settings, and
`qsfp_service` uses it to pick the driver peaking.

### Where the overrides come from

The overrides are set in the `qsfp_service` config, in
`transceiverVendorOverrides`. `qsfp_service` reads them at startup, copies them
into each transceiver's state (`TcvrState.vendorOverrides`), and `wedge_agent`
receives them with the rest of the transceiver info. Both services therefore
resolve the placeholder from the same list, and supporting a new part number
needs only a config change, no code change.

If the config has no `transceiverVendorOverrides`, or the list is empty, FBOSS
falls back to a mapping built into the FBOSS code. That built-in mapping is
empty in the open source build, so **an open source platform must set
`transceiverVendorOverrides` in its `qsfp_service` config** for any LPO SI
setting to apply.

## qsfp_service Config

Add the overrides to the platform's `qsfp_service` config
(`/etc/coop/qsfp.conf` on the switch). The example below maps the placeholder
`Vendor_1` / `PartNum_1` from the SI CSV to two part numbers. Replace the
`transceivers` entries with the vendor names and part numbers your modules
report.

It also sets the `enable_explicit_control` flag, which is off by default.
Without it `qsfp_service` does not write driver peaking to the module, even when
an LPO row matches.

```json
{
  "defaultCommandLineArgs": {
    "enable_explicit_control": "true"
  },
  "transceiverVendorOverrides": [
    {
      "placeholder": {
        "vendor": "Vendor_1",
        "partNumber": "PartNum_1"
      },
      "transceivers": [
        {
          "vendor": "Eoptolink",
          "partNumber": "1234"
        },
        {
          "vendor": "Eoptolink",
          "partNumber": "5678"
        }
      ]
    }
  ]
}
```

Each placeholder gets its own entry in `transceiverVendorOverrides`. To support
a part number that needs different SI settings, add rows with a new placeholder
(for example `Vendor_2` / `PartNum_2`) to the SI CSV and a matching entry here.

## Checklist for Adding an LPO to a Platform

1. Confirm the module meets the [transceiver requirements](#transceiver-requirements).
2. Characterize the host SerDes TX settings and the driver peaking for each lane
   and lane speed the platform uses with this module.
3. Add the LPO rows to the platform's SI CSV with a placeholder `TCVR_VENDOR`
   and `TCVR_PART_NUM`, and the characterized `DRIVER_PEAKING` and `TX_*`
   values.
4. Regenerate the platform mapping.
5. Add a `transceiverVendorOverrides` entry that maps the placeholder to the
   real vendor names and part numbers, and set `enable_explicit_control` to
   `true` in the `qsfp_service` config.
6. Restart `qsfp_service` and `wedge_agent` so they pick up the new config and
   platform mapping.
