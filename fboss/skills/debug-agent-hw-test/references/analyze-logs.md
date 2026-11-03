# Analyze Logs (Meta External)

## When to Use

When a test **fails** and you need to determine the root cause. Only analyze logs after a failure — skip this for passing tests.

## Log Sources

| Log | Location | Contains |
|-----|----------|----------|
| Test output (stdout/stderr) | Captured during test run | gtest assertions, XLOG messages, crash signals |
| SAI Replayer log | `/root/<user>/sai_replayer-cb.log` or `sai_replayer-wb.log` | Every SAI API call and its return code |
| BCM SDK log | `log file=` path from `--bcm_sdk_log_file` (e.g. `/tmp/brcm_sdk.log`) | SDK logical-table ops and API traces (only if captured, see [enable-logging.md](enable-logging.md) option (F)) |

## Step 1: Analyze Test Output

Scan the test output for these patterns, in priority order:

### Crashes

Look for fatal signals — these indicate the process died:

- `SIGABRT` — assertion failure or `CHECK`/`DCHECK` fired
- `SIGSEGV` — null pointer or bad memory access
- `Terminated due to` — killed by signal or timeout
- `core dumped` — crash left a core dump

If a crash is found, proceed to [crash-debug.md](crash-debug.md).

### Assertion Failures

Look for gtest assertion mismatches — the "expected vs actual" pattern:

```
Expected: <expected-value>
  Actual: <actual-value>
```

Also look for:
- `EXPECT_EQ`, `EXPECT_TRUE`, `ASSERT_EQ`, `ASSERT_TRUE` failures
- `Check failed:` — FBOSS `CHECK()` macro failures
- `Expected equality of these values:` — gtest detailed mismatch

Record the expected vs actual values — this is the primary clue for root cause.

### Error Log Lines

Look for `E` (error) and `F` (fatal) level log lines:
- Lines starting with `E0` or `F0` (e.g., `E0312 12:35:19.902899`)
- `SaiApiError` — SAI API returned an error
- `NOT IMPLEMENTED` — feature not supported by this ASIC/SDK

## Step 2: Analyze SAI Replayer Logs

Read the SAI replayer log **from the end** (reverse scan). The failure is most likely near the end of the log.

```bash
# On the switch — scan from end for error return codes
tail -100 /root/<user>/sai_replayer-cb.log

# Search for non-zero return codes (SAI errors)
grep -n 'status.*SAI_STATUS_' /root/<user>/sai_replayer-cb.log | grep -v 'SAI_STATUS_SUCCESS'
```

What to look for:
- `SAI_STATUS_FAILURE` — generic failure
- `SAI_STATUS_NOT_SUPPORTED` — operation not supported on this ASIC
- `SAI_STATUS_NOT_IMPLEMENTED` — SDK doesn't implement this API
- `SAI_STATUS_INVALID_PARAMETER` — wrong parameter passed
- `SAI_STATUS_ITEM_NOT_FOUND` — object doesn't exist
- Any `SAI_STATUS_*` that is not `SAI_STATUS_SUCCESS`

The SAI API call immediately before the error return tells you which operation failed.

## Step 3: Analyze BCM SDK Logs (If Captured)

Only when the run used `--bcm_sdk_log_file`. Line format is
`lt <TABLE> <op> <KEY=...> <FIELD=...>` with ops `lookup`/`update`/`insert`.

```bash
# Did the SDK touch the table at all?
grep -c "TM_THD_UC_Q" /tmp/brcm_sdk.log

# What did it program for one port/queue?
grep "TM_THD_UC_Q update PORT_ID=268 TM_UC_Q_ID=2 " /tmp/brcm_sdk.log
```

What to look for:
- No `update` for the entry — SDK never wrote it; the SAI call didn't map to a table write (vendor issue, escalate with the replayer + SDK logs).
- `update` with an unexpected value — compare against the SAI call's attributes.
- `OBJECT IN USE` / `ERROR` lines — teardown or state-conflict failures.

With `debug bcmlt verbose`, entries carry field-level detail (`Unit=0 Table <T>:`,
`TABLE_FIELD_INFO` lookups); with `debug bcmapi verbose`, look for the
`API: bcm_*` call sequence instead of table content.

## Step 4: Correlate

1. **Match test assertion to SAI call**: the expected vs actual mismatch in the test often maps to a specific SAI API call that returned wrong data or failed
2. **Find the code path**: locate the test source and trace through agent code to the SAI call
3. **Determine ownership**: is the bug in FBOSS code (wrong API call, wrong parameters) or vendor SDK (correct call, wrong result)?

## Common Patterns

| Test Output | SAI Replayer | Likely Cause | Next Step |
|-------------|-------------|-------------|-----------|
| Assertion mismatch | SAI call returns error | SDK doesn't support operation | Check feature flags |
| Assertion mismatch | All SAI calls succeed | Agent logic bug | Trace agent code |
| SIGABRT / CHECK failed | N/A | FBOSS internal assertion | Read stack trace |
| SIGSEGV | N/A | Null pointer / bad memory | See [crash-debug.md](crash-debug.md) |
| No errors in output | SAI call returns error near end | Silent SDK failure | Investigate SAI error code |
| Assertion mismatch | All SAI calls succeed (`rv:0`) | SDK didn't program HW | Check SDK log for missing/wrong table `update` |

## Next Steps

- If root cause is clear: fix the code and proceed to [build-and-load.md](build-and-load.md)
- If crash occurred: see [crash-debug.md](crash-debug.md)
- If more logging needed: see [enable-logging.md](enable-logging.md)
- If vendor SDK issue suspected: see [vendor-diag-shell.md](vendor-diag-shell.md)
- If hypothesis formed: see [hypothesis-driven-debug.md](hypothesis-driven-debug.md)
