# Run Tests (Meta External)

## When to Use

When you have a test binary loaded on the switch and need to execute tests, parse results, and determine pass/fail status.

## Inputs Required

| Input | Example | Required? |
|-------|---------|-----------|
| Binary path on switch | `/root/<user>/sai_agent_hw_test-brcm-12.2.0.0_dnx_odp` | Yes |
| Config file path | `/root/<user>/meru800bia.agent.materialized_JSON` | Yes |
| Test filter | `AgentVoqSwitchTest.addRemoveNeighbor` | Yes |
| HW agent binary path on switch | `/root/<user>/fboss_hw_agent-<sdk-version>` | No — derived from SDK version |
| Switch IDs to test | `0 2` | No — extract from config (platform-specific, see SKILL.md) |

## Running Tests

Tests are run via shell scripts that handle the full cold boot + conditional warm boot cycle, cleanup, and result summary. Upload the script to the switch, run it in the background, and monitor progress.

### Execution Pattern

See the resolved device-access reference selected via the Reference
Routing table in `SKILL.md` for the specific upload, run, monitor, and
download commands in your environment. The general pattern is:

```
1. UPLOAD: Copy the test script to the switch (once per session)
   UPLOAD scripts/<script>.sh TO <switch>:/tmp/<script>.sh

2. RUN IN BACKGROUND: Start the test
   RUN IN BACKGROUND ON <switch>: bash /tmp/<script>.sh <args>

3. MONITOR: Check output periodically until complete

4. DOWNLOAD: Fetch log files when done
   DOWNLOAD <switch>:/tmp/<output>.log TO /tmp/<output>.log
```

### Mono Mode

Use `scripts/run_mono_test.sh`. Args: `<binary_path> <config_path> <test_filter> <user> [ld_library_path]`

> **Leaba/Cisco devices**: For Leaba ASIC targets, pass the LD_LIBRARY_PATH as the 5th argument. This sets `LD_LIBRARY_PATH` and `BASE_OUTPUT_DIR` for the test binary. Example: `/root/<user>/lib/dyn/`
>
> `BASE_OUTPUT_DIR` must be the directory holding the SDK's `res/`. It defaults to `/root/<user>`; when the runtime lives elsewhere (one directory per SDK version, say), set `BASE_OUTPUT_DIR` in the environment of the launching command. Set `ASIC` there too on platforms that need it (`ASIC=GR2_A0` on G200). Without the right values the SDK aborts during device creation, after FBOSS init has already succeeded.

```
# Upload script (once per session)
UPLOAD scripts/run_mono_test.sh TO <switch>:/tmp/run_mono_test.sh

# Run (Broadcom — no 5th arg)
RUN IN BACKGROUND ON <switch>: bash /tmp/run_mono_test.sh /root/<user>/<binary> /root/<user>/<config> <TestSuite.TestName> <user>

# Run (Leaba — with LD_LIBRARY_PATH)
RUN IN BACKGROUND ON <switch>: bash /tmp/run_mono_test.sh /root/<user>/<binary> /root/<user>/<config> <TestSuite.TestName> <user> /root/<user>/lib/dyn/

# Run (Leaba G200, runtime in a per-version directory)
RUN IN BACKGROUND ON <switch>: ASIC=GR2_A0 BASE_OUTPUT_DIR=<dir>/ bash /tmp/run_mono_test.sh <dir>/<binary> <dir>/<config> <TestSuite.TestName> <user> <dir>/lib/dyn/

# Monitor until complete

# Download output
DOWNLOAD <switch>:/tmp/mono_test_output.log TO /tmp/mono_test_output.log
```

### Multi-switch Mode

Use `scripts/run_multi_test.sh`. Args: `<hw_agent_binary> <test_binary> <config_path> <test_filter> <switch_id>`

> **Derivable inputs**: The HW agent binary name is derived from the SDK version as `fboss_hw_agent-<resolved-sdk-version>` (e.g., `fboss_hw_agent-brcm-12.2.0.0_dnx_odp` for SDK `12.2`). The test binary is always `multi_switch_agent_hw_test` (not SDK-versioned). Switch IDs are platform-specific — extract from the config's `switchIdToSwitchInfo` (e.g., meru800bfa uses `0 2`, janga800bic uses `0 4`).

> **Config file**: Use the `<platform>_multi.agent.materialized_JSON` config for multi-switch mode. See [build-and-load.md](build-and-load.md#config-files) for where to find config files. Note that the `--config` flag passed to the hw_agent processes is effectively ignored when `--hw_agent_for_testing` is set — the hw_agents wait for the test binary to provide config via a shared file. The `--config` flag is only consumed by the test binary itself.

> **hw_agent health checks**: The script passes `--exit_for_any_hw_disconnect` to the test binary, which makes it exit immediately if any hw_agent disconnects (instead of hanging). After each test phase, the script also checks that both hw_agent processes are still alive — if the test binary passed but an hw_agent crashed, the result is overridden to FAILED. This matches netcastle's `check_hw_agent_exit_status()` behavior.

**For each switch_id**, upload and run the script:

```
# Upload script (once per session)
UPLOAD scripts/run_multi_test.sh TO <switch>:/tmp/run_multi_test.sh

# Run for switch_id 0
RUN IN BACKGROUND ON <switch>: bash /tmp/run_multi_test.sh /root/<user>/fboss_hw_agent-<sdk> /root/<user>/multi_switch_agent_hw_test /root/<user>/<config> <TestSuite.TestName> 0

# Monitor until complete

# Download output
DOWNLOAD <switch>:/tmp/multi_test_sid0_output.log TO /tmp/multi_test_sid0_output.log

# Repeat for remaining switch_ids (e.g., 2 for meru800bfa, 4 for janga800bic)
```

## Parsing Results

The scripts print a `=== Test Results ===` summary to stdout. Read this to determine pass/fail.

**On failure**, check the log files for these patterns:
- `[  PASSED  ]` — Test passed
- `[  FAILED  ]` — Test failed, check assertion message
- `Segmentation fault` or `SIGABRT` — Crash, see [crash-debug.md](crash-debug.md)
- `timeout` — Test hung, may need to investigate deadlock

For multi-switch tests, output is in log files (not stdout), so check the log files printed in the summary.

## Log File Visibility

**Always print log file paths** to the user after each test run. The scripts print them in the `=== Log Files ===` section.

**On failure**: Download logs to the build host and read them to show the failure reason:

```
DOWNLOAD <switch>:/tmp/multi_switch_agent_hw_test_switch_id_<switch_id>_cb.log TO /tmp/multi_switch_agent_hw_test_switch_id_<switch_id>_cb.log
DOWNLOAD <switch>:/tmp/multi_switch_agent_hw_test_switch_id_<switch_id>_wb.log TO /tmp/multi_switch_agent_hw_test_switch_id_<switch_id>_wb.log
DOWNLOAD <switch>:/tmp/fboss_hw_agent_log_0.log TO /tmp/fboss_hw_agent_log_0.log
DOWNLOAD <switch>:/tmp/fboss_hw_agent_log_1.log TO /tmp/fboss_hw_agent_log_1.log
```

**On success**: Just print the log file paths on the switch (no need to copy back).

## Results Summary

After running tests, print a summary table. Use text indicators: PASS, FAIL, SKIPPED. Do NOT use emoji.

```
| Test Name                              | Switch ID | Cold Boot | Warm Boot                |
|----------------------------------------|-----------|-----------|--------------------------|
| AgentVoqSwitchTest.addRemoveNeighbor   | 0         | PASS      | PASS                     |
| AgentVoqSwitchTest.addRemoveNeighbor   | 4         | PASS      | FAIL                     |
```

Warm boot column values: `PASS`, `FAIL`, or `SKIPPED (cold boot fail)`.

## Batch Mode (Mode B)

When running from a test list file, orchestrate at the skill/agent level (the shell scripts handle one test at a time — no script changes needed).

### Batch Loop

```
1. Read the test list file (one test name per line, skip comments/blanks)
2. For each test:
   a. Run the test using the appropriate script (mono or multi)
   b. If PASS on all switch_ids: record PASS_NO_CHANGE, move to next test
   c. If FAIL: enter debug loop (up to 5 iterations), then categorize and move on
   d. Update the running results table after each test
3. After all tests: print the batch summary table
```

### Batch Results Table

Print this combined table at the end of the batch:

```
| # | Test Name                              | Switch ID | Cold Boot | Warm Boot          | Category         | Iterations | Notes                        |
|---|----------------------------------------|-----------|-----------|--------------------|------------------|------------|------------------------------|
| 1 | AgentVoqSwitchTest.addRemoveNeighbor   | 0         | PASS      | PASS               | PASS_NO_CHANGE   | 1          |                              |
| 1 | AgentVoqSwitchTest.addRemoveNeighbor   | 4         | PASS      | PASS               | PASS_NO_CHANGE   | 1          |                              |
| 2 | AgentAclCounterTest.AclStatTest        | 0         | FAIL      | SKIPPED (cold fail) | FAIL_VENDOR      | 4          | SAI_STATUS_NOT_SUPPORTED     |
```

Follow with the category summary counts and pass rate.

## Gotchas

These apply to every vendor, and to hand-run commands as well as the scripts.

- **Cold boot with `--setup-for-warmboot` exits 0 even when an `EXPECT_*` failed.** Exit code alone is not a pass. Count gtest failure lines, `grep -acE ':[0-9]+: Failure$' <log>`, which must be 0. `run_mono_test.sh` does this check and marks the cold boot FAILED. In the warm-boot log, confirm `Boot Type: WARM_BOOT`; otherwise the second run silently cold booted.
- **One test per process.** `SetUp()` initializes the process once. A `--gtest_filter` matching two tests runs the first and aborts (SIGABRT, exit 134) in the second `SetUp()`. Run each test as its own invocation, as Mode B does.
- **`pkill -f <name>` sent as a remote command kills itself.** The pattern matches the remote shell's own command line, so the command dies (exit 137) and nothing else is killed. Run it as a separate command with a bracket pattern, `pkill -9 -f '[s]ai_agent_hw_test'`, or kill by PID. Wait about 2 s before the next run.
- **Output buffering.** Redirect to a file and run under `stdbuf -oL -eL`, then grep the file. Piping a long run through `tail` or `head` shows nothing until it exits.
- **Per-test ceiling of about 5 minutes.** Past that, the test is most likely stuck in SAI init or deadlocked; kill it and categorize as `FAIL_TIMEOUT`.
- **fb303 counters publish about 1 s late.** A single read right after traffic races the publish; poll with retries.
- **Stale warm-boot state.** A switch keeps the previous run's state under `/dev/shm/fboss/`, and the next run warm boots into it. Both scripts clear it; clear it yourself before a hand-run cold boot.

## Next Steps

- If cold boot PASSED and warm boot PASSED: categorize as `PASS_NO_CHANGE` and move to next test
- If cold boot PASSED but warm boot FAILED: debug the warm boot failure
- If cold boot FAILED with assertion: proceed to [analyze-logs.md](analyze-logs.md)
- If CRASHED: proceed to [crash-debug.md](crash-debug.md)
- If unclear: enable more logging via [enable-logging.md](enable-logging.md)
