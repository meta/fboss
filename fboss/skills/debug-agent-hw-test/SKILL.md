---
description: Debug FBOSS AgentHwTest failures - build, run, analyze logs, crash debug, vendor diag shell, hypothesis-driven debugging. Use when running sai_agent_hw_test or multi_switch_agent_hw_test and investigating test failures.
user-invocable: true
allowed-tools: Bash, Read, Edit, Write
---

# Debug AgentHwTest Failures

End-to-end workflow for building, running, and debugging FBOSS AgentHwTest failures on switches. Drives iterative test-debug-fix cycles.

## Prerequisites: Device Access

This skill requires access to a lab switch to deploy and run tests.
Read the device access reference (see Reference Routing below) to learn
the available commands for your environment (upload, download, run
commands on switch).

## Invocation Modes

The skill supports two modes:

| Mode | Input | Behavior |
|------|-------|----------|
| **Mode A** — Single test | A test name (e.g. `AgentVoqSwitchTest.addRemoveNeighbor`) | Run one test, debug if it fails (today's behavior) |
| **Mode B** — Batch test list | A file path (e.g. `/tmp/test_list.txt`) | Read test names from file, run each sequentially |

**Mode selection**: If the user provides a single test name, run Mode A. If the user provides a file path, read the file and run Mode B.

### Test List File Format (Mode B)

One test name per line. Lines starting with `#` are comments, blank lines are ignored.

```
# /tmp/test_list.txt
AgentVoqSwitchTest.addRemoveNeighbor
AgentAclCounterTest.AclStatTest
```

## Inputs

Gather these before starting:

| Input | Example | Required? |
|-------|---------|-----------|
| Test name | `AgentVoqSwitchTest.addRemoveNeighbor` | Yes (Mode A) |
| Test list file | `/tmp/test_list.txt` | Yes (Mode B) |
| Mode | `mono` or `multi` | Yes |
| SDK version | `12.2 DNX` | Yes |
| Switch name | `fboss325966941.ash6` | Yes |
| Config file path | `/root/<user>/<platform>.agent.materialized_JSON` (mono) or `/root/<user>/<platform>_multi.agent.materialized_JSON` (multi) | Yes — see [build-and-load.md](references/build-and-load.md#config-files) for how to obtain |
| HW agent binary path | `/root/<user>/fboss_hw_agent-brcm-12.2.0.0_dnx_odp` | No — derived from SDK version |
| Switch IDs | `0 2` | No — extract from config's `switchIdToSwitchInfo` (see below) |

### Deriving Switch IDs from Config

Switch IDs are platform-specific. Extract them from the materialized config file:

```bash
python3 -c "
import json, sys
with open(sys.argv[1]) as f:
    cfg = json.load(f)
for sid, info in cfg['sw']['switchSettings']['switchIdToSwitchInfo'].items():
    print(f'switchId={sid} switchIndex={info[\"switchIndex\"]}')
" /root/<user>/<platform>_multi.agent.materialized_JSON
```

Known platform switch IDs:

| Platform | Switch IDs | Switch Indices |
|----------|-----------|----------------|
| meru800bfa | 0, 2 | 0, 1 |
| janga800bic | 0, 4 | 0, 1 |

The `--switch_id_for_testing` flag takes the **switch ID** (not the switch index).

## Build-Before-Run Rule

**MANDATORY**: Always build binaries on every skill invocation before running any test. Never skip the build step, even if binaries already exist on the switch from a previous session or invocation. Stale binaries produce incorrect results because the source code may have changed.

On every skill invocation:
1. **Build** the required binaries (mono or multi-switch)
2. **Strip and copy** them to the switch (`strip_and_copy.sh` strips the binary and prints its md5; the separate upload step uses md5 dedup — an unchanged binary skips the network copy)
3. **Copy** the config file and test scripts to the switch
4. Then proceed to run the test

The md5 dedup in the upload step makes this safe and fast — if the binary hasn't changed, the copy is skipped. But the build must always run to ensure the binary reflects the current source.

## Debug Loop

Follow this iterative cycle for each failing test:

1. **Build** the test binary (mono or multi-switch) — **never skip this step**
2. **Load** the binary onto the switch
3. **Copy firmware** if testing on a Broadcom DNX switch (see [build-and-load.md](references/build-and-load.md#broadcom-dnx-firmware-required-for-all-tests-on-dnx-platforms))
4. **Run** the test (cold or warm boot)
5. **Analyze** output — logs, crashes, vendor diag shell
6. **Fix** or adjust — code change, feature flag, XLOG insertion
7. **Re-run** to verify

**Broadcom DNX firmware prerequisite**: All tests on Broadcom DNX switches (Jericho3, Ramon3, etc.) require the firmware `db/` directory at `/tmp/db/` on the switch. See [build-and-load.md](references/build-and-load.md#broadcom-dnx-firmware-required-for-all-tests-on-dnx-platforms) for details. Without this, hw_agent processes abort with `FW: ... is not accessible error:-1`. This does not apply to Broadcom XGS or Leaba/Cisco platforms.

**Leaba/Cisco prerequisite**: the SDK runtime (`lib/dyn` and `res/`) from the same SDK build as the binary must be on the switch. The run also needs `LD_LIBRARY_PATH`, `BASE_OUTPUT_DIR` and, on some ASICs, `ASIC` (`GR2_A0` on G200). Without them the SDK aborts after FBOSS init has succeeded. See the Leaba row in Reference Routing below.

**Discipline**: Work on one test at a time. Try up to 5 iterations before categorizing and moving to the next test.

## Batch Execution Loop (Mode B)

When running in batch mode, follow this loop for each test in the list:

```
for each test in list:
  1. RUN the test (cold boot + warm boot, across all switch_ids)
  2. if PASS on all switch_ids:
       - Record as PASS_NO_CHANGE
       - Proceed immediately to next test (no debug needed)
  3. if FAIL on any switch_id:
       - Enter debug loop (up to 5 iterations)
       - Use debug references: analyze-logs, crash-debug, vendor-diag-shell, etc.
       - Categorize result (PASS_FEATURE_FLAG, PASS_FBOSS_FIX, FAIL_FBOSS, etc.)
       - After 5 iterations or categorization, MOVE ON to next test
  4. Update the running results table after each test
```

**Key principle**: Passing tests get zero debug time. Failing tests get bounded debug time (max 5 iterations). The batch always makes forward progress — never get stuck on one test.

## Session Tracking

Maintain a results table at `/tmp/agent_hw_test_results.md` to track progress across the debug session. Update after each test attempt.

### Mode A — Single Test Results Table

```markdown
| Test Name                              | Switch ID | Cold Boot | Warm Boot |
|----------------------------------------|-----------|-----------|-----------|
| AgentVoqSwitchTest.addRemoveNeighbor   | 0         | PASS      | PASS      |
| AgentVoqSwitchTest.addRemoveNeighbor   | 4         | PASS      | PASS      |
```

### Mode B — Batch Results Table

```markdown
| # | Test Name                              | Switch ID | Cold Boot | Warm Boot          | Category         | Iterations | Notes                        |
|---|----------------------------------------|-----------|-----------|--------------------|------------------|------------|------------------------------|
| 1 | AgentVoqSwitchTest.addRemoveNeighbor   | 0         | PASS      | PASS               | PASS_NO_CHANGE   | 1          |                              |
| 1 | AgentVoqSwitchTest.addRemoveNeighbor   | 4         | PASS      | PASS               | PASS_NO_CHANGE   | 1          |                              |
| 2 | AgentAclCounterTest.AclStatTest        | 0         | FAIL      | SKIPPED (cold fail) | FAIL_VENDOR      | 4          | SAI_STATUS_NOT_SUPPORTED     |
| 2 | AgentAclCounterTest.AclStatTest        | 4         | FAIL      | SKIPPED (cold fail) | FAIL_VENDOR      | 4          |                              |
```

At the end of a batch run, also print a summary:

```markdown
=== Summary ===
Total tests: 2
  PASS_NO_CHANGE:    1
  PASS_FEATURE_FLAG: 0
  PASS_FBOSS_FIX:    0
  PASS_VENDOR_FIX:   0
  FAIL_FBOSS:        0
  FAIL_VENDOR:       1
  TODO_ASIC_CONFIG:  0

Pass rate: 1/2 (50%)
```

## Test Categorization Taxonomy

Categorize every test into one of these 7 outcomes:

| Category | Code | Meaning |
|----------|------|---------|
| PASS without code change | `PASS_NO_CHANGE` | Test passes as-is on the platform |
| PASS after HwAsic feature flag change | `PASS_FEATURE_FLAG` | Needed to enable/disable an ASIC feature flag |
| PASS after FBOSS code change | `PASS_FBOSS_FIX` | Required a fix in FBOSS agent code |
| PASS after Vendor SDK code change | `PASS_VENDOR_FIX` | Required a fix in vendor SAI/SDK code |
| FAIL: FBOSS needs manual debug/fix | `FAIL_FBOSS` | Identified as FBOSS issue, needs more work |
| FAIL: Vendor SDK needs manual debug/fix | `FAIL_VENDOR` | Identified as vendor SDK issue, needs more work |
| FAIL: Test hung or timed out | `FAIL_TIMEOUT` | Test did not complete within allowed time, possible deadlock |
| TODO: assess ASIC chip config change | `TODO_ASIC_CONFIG` | May need ASIC config (lane map, MMU, etc.) change |

For `FAIL_VENDOR` (or `PASS_VENDOR_FIX`), produce a vendor-escalation package — SAI Replayer log + hw_config + a case report the vendor can reproduce from — using the `vendor-escalation` skill (`../vendor-escalation/SKILL.md`). It is vendor-generic (Broadcom CSP / Cisco TAC / NVIDIA support).

## Scripts

### On-Switch Scripts (uploaded and run on the switch)

These scripts run **on the switch**. Upload them once per session, then run in the background.

| Script | Purpose | Args |
|--------|---------|------|
| `scripts/run_mono_test.sh` | Mono cold+warm boot cycle | `<binary> <config> <filter> <user> [ld_library_path]` |
| `scripts/run_multi_test.sh` | Multi-switch cold+warm boot for one switch_id | `<hw_agent> <test_binary> <config> <filter> <switch_id>` |

### Local Helper Scripts (run on build host only)

| Script | Purpose | Args | Output |
|--------|---------|------|--------|
| `scripts/strip_and_copy.sh` | Strip binary, print stripped path + md5 | `<source_path> <dest_name> [output_dir]` | Stripped binary at `<output_dir>/<dest_name>` (default `/tmp`) |

> **Vendor firmware scripts**: See [build-environment.md](references/build-environment.md)
> for environment-specific SDK path resolution and firmware preparation scripts.

### Remote Execution Pattern

Upload scripts to the switch, run tests in the background, and fetch
logs when complete. First resolve the device-access reference via the
Reference Routing table below, then follow
[run-tests.md](references/run-tests.md) for the full execution pattern.

## Reference Routing

For each reference pair below, load the `facebook/` version first if it
exists in your checkout. Otherwise load the `references/` version. Treat
the selected file as the source of truth for that topic.

This routing is client-agnostic:
- internal checkouts typically provide `facebook/` overrides with lab-specific
  device access and build instructions
- OSS checkouts fall back to `references/` with standard ssh/scp and
  open-source build instructions
- client-specific command forms are optional conveniences only; the resolved
  reference must always contain a direct runnable path as fallback

| Need | Try first | Fallback |
|------|-----------|----------|
| Device access (upload, download, run on switch) | `facebook/device-access.md` | `references/device-access.md` |
| Build commands, SDK paths, config locations | `facebook/build-environment.md` | `references/build-environment.md` |
| Build mono/multi binaries, copy to switch | — | `references/build-and-load.md` |
| Leaba/Cisco runs — SDK runtime staging and checks, environment, benchmarks, lab devices | `facebook/leaba-device.md` | `references/build-and-load.md` (Leaba/Cisco SDK Libraries) and `references/run-tests.md` |
| Run tests (cold/warm, mono/multi), parse results | — | `references/run-tests.md` |
| Analyze SAI Replayer logs, read code for root cause | — | `references/analyze-logs.md` |
| Enable SAI logging, replayer logging, packet tx logs, BCM SDK debug logs | — | `references/enable-logging.md` |
| SAI Replayer — capabilities/flags (packet/get/elapsed-time log), capture, build, run | `facebook/sai-replayer.md` | `references/sai-replayer.md` |
| Debug crashes — non-stripped binaries, GDB, stack traces | — | `references/crash-debug.md` |
| Vendor diagnostic shell — counters, routes, neighbors, techsupport dumps | `facebook/vendor-diag-shell.md` | `references/vendor-diag-shell.md` |
| Insert XLOGs, re-run, validate hypotheses | — | `references/hypothesis-driven-debug.md` |
| Full categorization guide and tracking template | — | `references/test-categorization.md` |
| Build a vendor-escalation package (SAI replayer log + hw_config + case report) for `FAIL_VENDOR` | — | the `vendor-escalation` skill (`../vendor-escalation/SKILL.md`) |
