---
name: link-log-diagnosis
description: Diagnose link flaps from wedge_agent and qsfp_service logs — determine software vs hardware root cause, point to evidence, suggest fixes or detection strategies
user-invocable: true
argument-hint: <local_host>:<local_port>-<remote_host>:<remote_port>
tags:
  - fboss_optics_phy_oncall
---

# Link Log Diagnosis

Diagnose link issues by analyzing wedge_agent, qsfp_service, and PHY snapshot logs from both sides of a link. Determines whether the root cause is software (e.g., warmboot reprogramming, remediation loops) or hardware (e.g., bad optics, signal loss), points to specific log evidence, and suggests fixes.

## Step 1: Gather Information

Ask the user for:
1. **Link(s) to check** in format `<local_host>:<local_port>-<remote_host>:<remote_port>` (e.g., `switch-a.example.net:eth1/6/1-switch-b.example.net:eth1/48/1`). Multiple links can be provided.
2. **Time range** — when did the issue occur? Accept natural language ("yesterday 6am to noon", "last 2 hours", "March 30 14:00 to 18:00").

Parse each link to extract:
- `local_hostname`, `local_port` (from left side of `-`)
- `remote_hostname`, `remote_port` (from right side of `-`)
- Derive transceiver IDs: port `eth1/N/x` → transceiver ID = `N-1`

## Step 2: Fetch Logs

Construct the `fetch_logs.sh` command for the user. Use the skill directory path for the script:

```
bash <skill_dir>/fetch_logs.sh <local_hostname> <remote_hostname> '<start_time>' '<end_time>'
```

The script fetches from **both** local and remote switches:
- `wedge_agent.log` + `wedge_agent_snapshots.log` (selected archives + current log if the window extends past the latest rollover)
- `qsfp_service.log` + `qsfp_service_snapshots.log` (same)
- Config changes, when a config repo is configured (git log with patches for the time range)

Output goes to `/tmp/fboss-logs/<local_hostname>/local/` and `.../remote/`.

Transport, on-switch log locations, and the config repo layout are
environment-specific. Read the config-pipeline reference (see Reference
Routing) before the first run in a new environment.

> **Modifying the archive selection logic?** Archive filename timestamps are **rollover** times (= end of the data inside), not data-content times. Keep that in mind when changing the selection window.

**Tell the user to run the command and continue the session once logs are collected.**

## Step 3: Analyze

When the user continues, load `references/analysis-patterns.md` and perform all analysis.

### 3a. Flap Timeline & Warmboot Correlation

Run `analyze_flaps.sh` against **both** local and remote log dirs, filtered to the relevant port(s):

```
bash <skill_dir>/analyze_flaps.sh <log_dir>/local <local_port>
bash <skill_dir>/analyze_flaps.sh <log_dir>/remote <remote_port>
```

This produces a merged timeline of warmboot events and link flaps with attribution.

### 3b. Transceiver Programming Trace

Run `trace_transceiver.sh` against the log dir for each relevant transceiver:

```
bash <skill_dir>/trace_transceiver.sh <log_dir>/local <transceiver_id> [time_filter]
bash <skill_dir>/trace_transceiver.sh <log_dir>/remote <transceiver_id> [time_filter]
```

This produces a consolidated view of: warmboot events with PIDs, state machine transitions, programming actions (hitless vs disruptive), IPHY skip check, link state changes, and PHY fault status. Use the optional `time_filter` (e.g., `"10:27"`) to narrow to a specific warmboot window.

Key patterns: `Speed matches` (hitless/good) vs `Trying to set application code` (reprogrammed/bad). Load `references/cmis-disruptive-ops.md` for the full decision tree.

### 3c. PHY Snapshot Diagnostics

Load `references/phy-snapshot-analysis.md`. Search `wedge_agent_snapshots.log` and `qsfp_service_snapshots.log` around each flap timestamp for `LinkSnapshot` or `PhyInfo` entries. **Always specify which device's snapshot you are reading.** Key diagnostic signals:
- `remoteFault=True` on device A with healthy local signal → the remote device (B) has an RX issue; investigate A's TX or B's RX
- `localFault=True` on device A with signal loss → A's receiver lost signal; investigate fiber, B's TX, or A's optics
- `signalDetect=False`, `cdrLock=False` → complete signal loss at this device's receiver

### 3d. Compare Flapping vs Non-Flapping Warmboots

When only some warmboots cause flaps, compare a flapping and non-flapping warmboot trace side-by-side:
- Run `trace_transceiver.sh` with the time filter for each warmboot
- Check if agent-only, qsfp_service-only, or combined warmboots correlate with flaps
- Compare the complete programming path — if identical, the cause is not in programming
- Look for differences in timing, I2C errors, or concurrent operations

### 3e. Config Change Correlation

Run `filter_config_changes.sh` against both log dirs to extract hardware-affecting changes from the raw config git patches (which are typically 10-100MB+ and dominated by routing policy noise):

```
bash <skill_dir>/filter_config_changes.sh <log_dir>/local/config_changes.log
bash <skill_dir>/filter_config_changes.sh <log_dir>/remote/config_changes.log
```

This extracts: config push timeline, TX/RX SerDes setting changes (platform_mapping), port speed/FEC/lane changes, drain operations, and agent config version changes. Cross-reference timestamps with `triggerAgentConfigChangeEvent` in qsfp_service logs.

### 3f. Cross-Reference Local vs Remote

Compare local and remote analysis to determine fault direction:
- Local sees `remoteFault` + remote had warmboot → remote warmboot caused flap
- Both sides see `localFault` + signal loss → possible fiber/hardware issue
- One side has remediation loop → that side's software is the root cause

### 3g. Report

Present findings: trigger, evidence (quoted log lines with timestamps), fault direction, and suggested fix or detection strategy.

## Reference Routing

For each pair below, load the `facebook/` version first if it exists in your
checkout. Otherwise load the `references/` version. Treat the selected file as
the source of truth for that topic.

Internal checkouts provide `facebook/` overrides with the Meta config-pipeline
layout and lab examples. Open-source checkouts fall back to `references/`,
which describes the same workflow in terms of settings you supply.

| Need | Try first | Fallback |
|------|-----------|----------|
| Config repo layout, environment settings | `facebook/config-pipeline.md` | `references/config-pipeline.md` |
| Worked examples with real hostnames | `facebook/examples.md` | — (generic examples are inline above) |
| Verified log strings, grep commands, analysis workflow | — | `references/analysis-patterns.md` |
| CMIS transceiver disruptive vs hitless operations | — | `references/cmis-disruptive-ops.md` |
| PHY snapshot analysis (LinkSnapshot, fault status, signal integrity) | — | `references/phy-snapshot-analysis.md` |

`facebook/env.sh`, when present, is sourced automatically by the scripts and
supplies the transport commands and config paths. No manual setup is needed in
internal checkouts.
