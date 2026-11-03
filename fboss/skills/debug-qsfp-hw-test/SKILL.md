---
description: Debug QSFP HW test failures from logs - isolate the root-cause assertion among benign noise. Use when a qsfp_hw_test gtest failed and you have its log as pasted text, a file, or a CI run link.
user-invocable: true
allowed-tools: Bash, Read
---

# Debug QSFP HW Test Failures

Find what actually caused a QSFP HW test (`qsfp_hw_test-<impl>-<version>`) run
to fail by reading its log. QSFP HW test logs are noisy: retry loops, SetUp
polling, and best-effort telemetry emit `WARN`/`ERR` lines on healthy runs.
The real failure is almost always a gtest `Failure` block, a fatal `CHECK`
abort, or a `TearDown` expectation — everything else is a suspect, not a
verdict.

This skill reports root cause plus evidence only. It does not suggest code
fixes, file known-bad entries, or assign blame.

## Inputs

Accept any one of these:

| Input | Example |
|-------|---------|
| Pasted log text | Raw gtest output pasted into the conversation |
| Log file path | `/tmp/qsfp_hw_test.log` |
| CI run link | Link to the failing release node or test job |

How to turn a link into a log is environment-specific — see Reference Routing
below (`log-input` row). If the input is pasted text, save it to
`/tmp/qsfp_hw_test_debug.log` first so `grep` commands work uniformly.

A first-pass parser ships with the skill: `scripts/analyze_qsfp_log.sh
/tmp/qsfp_hw_test_debug.log` prints the failing gtest(s), the killer
signature lines with line numbers, and a benign-noise count. Treat its output
as a candidate list — always confirm by reading the log around the reported
lines.

## Workflow

### Step 1: Isolate the verdict

Find every gtest verdict line:

```bash
grep -F "[  FAILED  ]" /tmp/qsfp_hw_test_debug.log
grep -F "[  PASSED  ]" /tmp/qsfp_hw_test_debug.log | tail -n 20
```

Record for each failed test:

1. **Full name** including phase prefix: `cold_boot.<Suite>.<Test>` or
   `warm_boot.<Suite>.<Test>`. The prefix matters (see Step 4).
2. **`[KNOWN BAD: ...]` tag** — if the FAILED line carries one, the failure
   is suppressed by policy. Stop here and report it as known-bad, not as a
   regression.
3. **Cold/warm asymmetry** — did the same test pass cold and fail warm (or
   vice versa)? Did warm-boot tests never run at all (cold abort)?

### Step 2: Find the killer, dismiss the noise

Search **backwards** from each `[  FAILED  ]` line for the first killer block.
Killer signatures (fatal, in priority order):

1. A gtest `Failure` block: lines matching `.cpp:<line>: Failure` (or
   `unknown file: Failure`) followed by `Value of:` / `Actual:` /
   `Expected:` — includes `C++ exception ... thrown in the test body`.
2. A `TearDown` expectation: `HeartbeatMissedCount`, `forceColdBoot file
   exists`.
3. A thrown programming error naming the test context (`FbossError`,
   `Verify with retry failed`, `Never got ...`, `Never refreshed ...`,
   `accessing unset optional`).
4. `Ports that did not reach` — the SetUp programming gate failed.
5. Crash markers: `SIGSEGV` / `SIGABRT` / `signal killed` / `Service exit
   status:` / symbolized stack. A printed core path vs `No core dumps
   found` disambiguates segfault from clean abort.
6. ABRT near the timeout cap with no assertion above it — timeout/hang, not
   a logic failure.
7. Every test failing identically (e.g. `EmptyHwTest.CheckInit` fails) —
   whole-binary setup failure, environmental rather than the test body.

If there is **no** FAILED line but the binary died mid-run, it was a fatal
`CHECK` abort: the last 50 lines before death are the killer, and every test
listed as not-run afterwards is collateral, not a failure.

Benign patterns that must be quoted-but-dismissed, never reported as cause —
see `failure-signatures.md` for the full table with sources:

- Refresh/retry `WARN`s (`transceiverInfo not returned after refresh`,
  `timestamp ... after refresh`, `should be programmed but are not`).
- `doesn't have expected state=` and `don't meet the expected state` lines
  **inside** a retry loop (only the final-iteration `EXPECT` counts).
- `Skipping port ... not managed by qsfp_service`, `is present but not in
  the config` (lab hygiene, `EXTRA` telemetry row).
- Firmware-mismatch info lines (`will not affect overall config validity`).
- `XPHY not present in system` + `continue`, `Skip verifying ... passive
  copper cable`, `STAGE:` progress lines.

### Step 3: Map to source

Map the failing `<Suite>.<Test>` to its file using `test-catalog.md`, then
read the enclosing `TEST_F` to state what the check means (state machine,
I2C, firmware, config validation, media compliance, PRBS, reset). Quote the
file and line of the failing assertion.

### Step 4: Report

Present, in this order:

1. **Failing test** — full name with phase prefix.
2. **Cold vs warm** — what the asymmetry implies: cold-only points at SetUp /
   programming gate; warm-only points at state preservation across warmboot;
   both points at the test body itself.
3. **Killer** — quoted log lines with timestamps plus the source file:line
   and one sentence on what the check verifies.
4. **Dismissed noise** — the benign lines present in this log, each with one
   phrase on why it is not the cause.

## Scripts

| Script | Purpose | Args |
|--------|---------|------|
| `scripts/analyze_qsfp_log.sh` | First-pass log parse: failing gtest(s), killer signature lines with line numbers, benign-noise count | `<logfile>` (or `-` for stdin) |
| `facebook/scripts/fetch_qsfp_failure.sh` | Internal: query the result store for recent failures, print test case + full-log URLs | `[--team fboss_qsfp] [--test <substr>] [--hours 168] [--limit 5]` |

`analyze_qsfp_log.sh` is open-source safe (pure text parsing, no infra
deps) and runs anywhere. `fetch_qsfp_failure.sh` needs the internal `meta`
CLI and stays under `facebook/`.

## Reference Routing

For each reference pair below, load the `facebook/` version first if it
exists in your checkout. Otherwise load the `references/` version. Treat
the selected file as the source of truth for that topic.

| Need | Try first | Fallback |
|------|-----------|----------|
| Turn a CI run link into a raw log; check known-bad suppressions | `facebook/log-input.md` | `references/log-input.md` |
| Full fetch flow: result store, Details blob, full-log links, known-bad flag (internal) | `facebook/fetching-logs.md` | `references/log-input.md` |
| Killer vs benign log signatures with sources | — | `references/failure-signatures.md` |
| Test name to source file map and what each suite verifies | — | `references/test-catalog.md` |
