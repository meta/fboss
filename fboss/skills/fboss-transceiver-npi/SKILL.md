---
name: fboss-transceiver-npi
description: "Use when onboarding a new transceiver NPI end-to-end or partially — adding transceiver code, firmware, and/or conveyor nodes. Trigger phrases: new transceiver NPI, onboard transceiver, add firmware, conveyor nodes, HAL test node, link test config, new optic. For processing an optics firmware intake task (butterfly form) on an existing optic, use the transceiver-fw-intake skill instead."
user-invocable: true
argument-hint: <freeform-args>
---

# FBOSS Transceiver NPI Onboarding

## Overview

End-to-end workflow for onboarding a new transceiver or transceiver firmware,
in three phases. Any subset can be selected.

| Phase | What it does | Available in |
|-------|--------------|--------------|
| **Phase 1: Transceiver Code** | `MediaInterfaceCode` enum entries in `transceiver.thrift`; the matching entry in `TransceiverPropertiesDefault.h` | Every checkout |
| **Phase 2: Firmware** | Firmware image placement, manifest entry, part-number-to-firmware mapping | Internal checkouts only |
| **Phase 3: Conveyor Nodes** | HAL/QSFP/Link Conveyor test nodes, link test configs, optional intake | Internal checkouts only |

Phases 2 and 3 depend on infrastructure that has no open-source counterpart —
the firmware delivery tree, the fleet configuration repository, and the
Conveyor test pipelines. In an open-source checkout this skill covers Phase 1,
and the `facebook/` files named below are not present. Say so plainly if a user
in such a checkout asks for firmware or test-node work, rather than inventing a
path.

## Reference Routing

For each reference pair below, load the `facebook/` version first if it exists
in your checkout. Otherwise load the `references/` version. Treat the selected
file as the source of truth for that topic.

Rows with no fallback are internal-only: the topic itself is an internal
system, so there is nothing for an open-source checkout to fall back to. If the
`facebook/` file is absent, that phase does not apply — see the table above.

| Need | Try first | Fallback |
|------|-----------|----------|
| Vendor names and part-number prefixes | `facebook/transceiver-vendors.md` | `references/transceiver-vendors.md` |
| Phase 1: thrift + `TransceiverPropertiesDefault.h` edits | — | `references/code-implementation-steps.md` |
| Phase 1: build and run the two covering tests | `facebook/build-and-test.md` | `references/build-and-test.md` |
| Phase 1: commit and submit | `facebook/submitting-changes.md` | `references/submitting-changes.md` |
| Phase 1: mirror enum entries into the fleet config repository | `facebook/code-cfgr-updates.md` | *internal only* |
| Phase 2: info gathering, defaults, rollout stages | `facebook/firmware-overview.md` | *internal only* |
| Phase 2: image placement, manifest, part-number mapping | `facebook/firmware-fbcode-steps.md` | *internal only* |
| Phase 2: fleet rollout stage config | `facebook/firmware-cfgr-config.md` | *internal only* |
| Phase 3: required fields, NPI naming, target machine | `facebook/conveyor-overview.md` | *internal only* |
| Phase 3: link test topology configs | `facebook/conveyor-link-test-configs.md` | *internal only* |
| Phase 3: Conveyor node definitions + intake | `facebook/npi-conveyor-nodes.md` | *internal only* |

Media interface codes come from SFF-8024 (*SFF Module Management Reference Code
Tables*), a public SNIA specification. If the `sff-8024` skill is installed,
use it for the tables; otherwise read the published document.

## Input Parsing

**FIRST:** Resolve and read the vendor reference (routing table above) BEFORE
parsing the user's prompt. It maps vendor names and lists the known vendors.

**THEN:** Extract as much as possible from the user's prompt. Match vendor names
against the vendor list. Only ask for:

1. **Vendor name** — if not recognizable from prompt + vendor reference
2. **Media type** (e.g., `2x800G-DR4`) — if not in prompt

## Upfront Questionnaire

Gather ALL information needed across ALL selected phases in **one round-trip**.
Omit the Phase 2 and Phase 3 rows when those phases are unavailable.

> | Phase | Question | Value |
> |-------|----------|-------|
> | **Phase 1** | Vendor | `Innolight` (from prompt) |
> | **Phase 1** | Media type | `2x800G-DR4` (from prompt) |
> | **Phase 2** | Firmware to add? | Yes/No |
> | **Phase 2** | Firmware binary path(s), version(s), part number(s) | ? |
> | **Phase 2** | Paired DSP firmware? | Yes/No |
> | **Phase 3** | Conveyor nodes needed? | Yes/No |
> | **Phase 3** | Which nodes? | HAL / QSFP / Link / Intake |
> | **Phase 3** | Device type, target machine | ? |

## Step 0: Start From Current Sources

Update every repository the selected phases touch to its latest upstream state
before making any edit. Phase 1 alone needs only the FBOSS repository; Phases 2
and 3 also need the fleet configuration repository.

## Phase 1: Add New Transceiver Code

### Check existing support

Read `fboss/qsfp_service/if/transceiver.thrift`. If the media type already has
a `MediaInterfaceCode`, no code change is needed — in an internal checkout,
skip ahead to the fleet-config check in `facebook/code-cfgr-updates.md`.

This is the common case for a new vendor part on an already-supported media
type: `TransceiverPropertiesDefault.h` is keyed by media type, not by vendor.

### Information gathering

Present pre-populated defaults in one table. Auto-derive from SFF-8024,
existing codebase entries, and the media type. Ask the user to **confirm or
correct**. For speed combinations, only the **names** are needed — derive lane
mappings and codes. **At most 2 round-trips.**

### Implementation

Follow `references/code-implementation-steps.md`, then the build/test and
submit references resolved from the routing table. In an internal checkout,
also apply `facebook/code-cfgr-updates.md`.

**Wait for Phase 1 to complete before Phase 2 & 3.**

## Phase 2 & 3 (Parallel)

Internal checkouts only. Run in parallel after Phase 1, using parallel
subagents. Load the Phase 2 and Phase 3 overview references first for
information gathering, then their execution references.

## Final Summary

```
| Phase | Status | Details |
|-------|--------|---------|
| Phase 1: Transceiver Code | Done/Skipped | <change reference> |
| Phase 2: Firmware | Done/Skipped/N/A | <change reference> |
| Phase 3: Conveyor Nodes | Done/Skipped/N/A | HAL/QSFP/Link nodes |
```
