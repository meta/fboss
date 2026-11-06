# Phase 1 — Transceiver Code Implementation

Two files, edited in this order. Both are in the open-source tree.

## Step 1: Update `transceiver.thrift`

**File:** `fboss/qsfp_service/if/transceiver.thrift`

1. Add the new `MediaInterfaceCode` entry
2. Add any new `SMFMediaInterfaceCode` entries
3. Add any new per-lane `MediaInterfaceCode` entries

Take the media interface code values from SFF-8024, the public SNIA reference
code tables. Which table depends on the media type:

| Pattern | Category | Table |
|---|---|---|
| `DR*`, `FR*`, `LR*`, `ER*`, `ZR*` | SMF | 4-7 |
| `SR*` | MMF | 4-6 |
| `AEC*`, `ACC*` | ActiveCu | 4-8/4-9 |
| `DAC*`, `CR*` | PassiveCu | 4-8 |

## Step 2: Update `TransceiverPropertiesDefault.h`

**File:** `fboss/qsfp_service/module/properties/TransceiverPropertiesDefault.h`

Add one entry keyed by the `MediaInterfaceCode` integer value, in sorted order
by key. Every port in every speed combination MUST include
`"mediaInterfaceCode": <int>`.

To determine the correct `mediaInterfaceCode` integer for each `mediaLaneCode`:

1. Check existing entries in this file for the same `mediaLaneCode` hex value
2. For per-lane codes newly added in Step 1, use their newly assigned integer values

**Do NOT** read `CmisHelper::getSmfMediaInterfaceMapping()` — it is a
legacy/fallback path not used in config-driven mode. All mappings are fully
determined by `TransceiverPropertiesDefault.h`.

### Example entry

```json
"23": {
  "firstApplicationAdvertisement": {
    "mediaInterfaceCode": 0x77,
    "hostStartLanes": [0, 4],
    "hostInterfaceCode": 0x82
  },
  "smfLength": 500,
  "numHostLanes": 8,
  "numMediaLanes": 8,
  "displayName": "DR4_2x800G",
  "supportedSpeedCombinations": {
    "2x800G-DR4": {
      "ports": [
        {"speed": 800000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": 0x77, "mediaInterfaceCode": 22},
        {"speed": 800000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": 0x77, "mediaInterfaceCode": 22}
      ]
    }
  },
  "speedChangeTransitions": [
    ["2x800G-DR4", "4x400G-DR2"]
  ]
}
```

## Step 3: Build, test, submit

Resolve the build/test and submit references through the routing table in
`SKILL.md`, then follow them.
