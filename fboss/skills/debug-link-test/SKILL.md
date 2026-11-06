---
description: Debug FBOSS link test (agent ensemble link test) failures from logs - find the root cause among benign noise, and when the link test log is not enough (ports never came up, transceivers not ACTIVE, qsfp_service errors) use the qsfp_service log for the same window to root-cause the optics side. Works on logs handed to it (link test log, optionally qsfp_service / hw agent logs and the result record) without fetching anything, or fetches them itself from a result record or CI run link. Use when a link test (sai_mono_link_test / sai_multi_switch_link_test / agent_ensemble link test) failed.
user-invocable: true
allowed-tools: Bash, Read
---

# Debug Link Test Failures

Find what actually caused an FBOSS link test run to fail. Link tests run the
agent (SwSwitch+HwSwitch in one process, or SwSwitch plus separate hw agents)
against **real optics and real cabling**, with `qsfp_service` running as a
**separate process**. So a failure can originate in any of three places:

| Where the cause lives | Evidence you need |
|---|---|
| Agent / SDK / test body (asserts, crashes, config, ACL, LACP, FEC math) | The link test log (gtest stdout) |
| Multi-switch hw agent (hw agent crashed / exited non-zero) | The hw agent log, not the test log |
| Optics / transceiver programming / I2C / module capability | The **qsfp_service log** for the same time window |

The link test log is noisy (thousands of `E`/`W` glog lines on healthy runs)
and usually **has no `[  FAILED  ]` line**. Cold-boot runs pass
`--setup_for_warmboot`, and the fixture hard-exits through the warmboot
graceful-exit path (exit code 1 when the test failed) before gtest prints its
summary. The gtest `Failure` block (or a CHECK / abort) is the verdict.

This skill reports root cause plus evidence only. It does not change code,
file known-bad entries, or touch devices beyond read-only log retrieval.

## Inputs

The skill runs in one of two modes. Decide which before doing anything else.

| Mode | When | What you do |
|---|---|---|
| **Provided logs** | The caller hands you the logs: a link test log and optionally a qsfp_service log, a hw agent log, and/or the result record (as files or pasted text) | Analyze **only** what was given. Do **not** query result stores, CI, or switches, even if the tools are available, unless the caller asks you to fetch |
| **Fetch** | You get a CI link, a result record with log links, a test name, or a switch + time | Get the logs yourself (see Reference Routing: `log-input`, `qsfp-log`), then analyze |

Accepted inputs, in either mode:

| Input | Used for |
|---|---|
| Link test log (required): gtest stdout of the link test binary | Steps 2-4 |
| Result record: the per-test blob with `Test Info` / `message` / `stdout` / `stderr` sections | Step 1 (known-bad, prefixes, hw agent exit, scope) |
| qsfp_service log(s): whole run or a time slice; several files are fine | Step 3 |
| hw agent log(s) (multi-switch runs) | Step 2, when the result says the hw agent exited non-zero |

Save pasted text to files first, e.g. `/tmp/link_test_debug.log` and
`/tmp/qsfp_service_debug.log`.

In **provided-logs** mode:

- Missing pieces are not an error; do the steps you have inputs for. With no
  result record, skip Step 1, but still read `Known Bad?`, the command and the
  exit status if they appear in the pasted text. Say scope is unknown.
- The failure time for `--at` comes from the link test log itself: the glog time
  of the failure line, or the analyzer's `time window` line.
- If a qsfp_service log was given, run the Step 3 coverage check on it. When it
  reports `covered: NO`, do not use that log. Report it as the wrong log, not as
  evidence. With several qsfp logs, use the one that covers the failure time.
- If the verdict needs a log you weren't given (qsfp_service or hw agent), say
  exactly which log and time window would settle it. Give the best verdict from
  what you have, labeled as a hypothesis.

## Workflow

### Step 1: Triage the result record before the log

If you have the result record, read it first. It answers questions the log cannot:

1. **Known bad?** If the result is marked known-bad (or the test name matches
   the known-bad list for its test config), report it as suppressed-by-policy.
   Still state the mechanism, but do not call it a regression.
2. **Name prefixes**: strip runner prefixes to get the gtest name:
   `cold_boot.` / `warm_boot.` / `warm_boot_for_warm_boot.<n>.` (phase),
   `roundtrip.<from>_2_<to>_2_<from>.` (cross-version warmboot: failure may be
   a version incompatibility), `switch.<id>.` (multi-switch: logical switch ID).
3. **message section**: `Hw Agent exited with non-zero status code.` + `Hw
   agent logs for switch N: <link>` means the test binary may have **passed**.
   Analyze the hw agent log instead (Step 2 works on it unchanged).
   `Failed to start hw agent before test run.` / `Failed to start qsfp_service`
   mean setup failed before the binary ran.
4. **Exit**: `Return Code: 1` (test failed; normal), signal 6 / SIGABRT (see
   Step 2 abort classification), `TIMEOUT`.
5. **Scope**: if every other test in the same CI job failed too, it is a
   setup/environment failure of the device, not N independent bugs. Say so,
   and root-cause one representative (prefer `AgentEnsembleEmptyLinkTest.CheckInit`).
   If you only have one result and no job view, say scope is unknown rather than guessing.

Script paths below are relative to this skill's directory.

### Step 2: Analyze the link test log

```bash
bash scripts/analyze_link_log.sh /tmp/link_test_debug.log
```

It prints: the test, gtest verdict (or why there is none), every `Failure`
block with its **phase** (SetUp / test body / TearDown), the **gate / class**,
fatal CHECKs, `terminate` / `Terminated due to:` / SAI-init lines, vendor SDK
init errors, sanitizer reports, aborts with **meaningful stack frames**, a
**per-down-port table** decoded from the debug dump, qsfp_service RPC
failures, and a `QSFP_SERVICE_LOG_NEEDED` decision with focus transceivers and
a time window. Treat it as a candidate list; confirm by reading ±30 lines
around each reported line number.

Classify with `references/failure-signatures.md`. The main classes:

- **G1 SetUp link-up gate**: `Unexpected Link status 0 for <ports>` thrown in
  `SetUp()`. The named test body never ran. Read the per-port table: which
  ports, which lanes (`rxLos` / `rxLol` bitmasks), transceiver state, present,
  IPHY signal detect / CDR lock. A regular pattern across modules (e.g. every
  `/5` and `/7` port = lanes 4-7) points at config/capability or a shared peer
  path, not N bad optics.
- **G2 / G3 qsfp gates**: `Transceivers:[..] don't have expected
  TransceiverStateMachineState` / `Ports:[..] don't have expected
  PortStateMachineState`. Always qsfp_service side, so go to Step 3.
- **Test-body failure**: a `file:line: Failure` in the suite's `.cpp`. Map it
  with `references/test-catalog.md`, read the `TEST_F`, and decide product bug
  vs test bug vs environment.
- **Abort**: the analyzer labels each abort:
  - `self-abort` (signal sent by the same PID): CHECK / fatal / terminate.
    The **stack is the diagnosis** (e.g. `SaiSwitch::initLocked` = SAI switch
    init; `processAclTableGroupDelta` = ACL programming).
  - `EXTERNAL kill` (signal from another PID at ~the timeout cap): the runner's
    `timeout -s ABRT` watchdog, i.e. a hang. The frames show where it was stuck
    **only if** they are on the test thread. If it was killed inside the G1
    link wait, no down-port dump was printed. Find the down ports by comparing
    the `Checking link status on` list with the ports the log later reports up
    (`LinkState` / port up lines), and group them by port profile (e.g. only the
    copper ports stayed down). Those are the transceivers to check in the
    qsfp_service log.
- **Binary passed but result FAILED**: see Step 1.3.

### Step 3: Go to the qsfp_service side when the link log is not enough

Mandatory when the analyzer says `QSFP_SERVICE_LOG_NEEDED: YES` (G2/G3,
`TransceiverInfo was never populated.`, `QSFP Service no longer alive`, down
ports whose transceiver is not ACTIVE / not present / dark on media lanes) and
recommended for optics, PRBS, qsfp-fsdb, TX-disable, remediation tests.

1. **Get the log for the exact window.** In provided-logs mode, use the
   qsfp_service log(s) you were given. In fetch mode, see Reference Routing
   (`qsfp-log`): it lives on the switch as a per-run file and may also have been
   uploaded by the runner. Either way, **validate that the log's time span covers
   the failure**. A log from an earlier run or an earlier service start on the
   same switch is the most common way to reach a wrong verdict.
2. **Analyze it**:

   ```bash
   bash scripts/analyze_qsfp_service_log.sh <qsfp.log> --tcvr <ids> \
     --from "MMDD HH:MM:SS" --to "MMDD HH:MM:SS" --at "MMDD HH:MM:SS"
   ```

   Copy `--tcvr` / `--from` / `--to` from the link analyzer's hand-off lines; `--at` is
   the failure line's glog time. `Failure time ... covered: NO` means **wrong log** —
   stop and get the right one.

   Transceiver IDs are **0-based** (`[SM]Transceiver:6` = `eth1/7` on most
   platforms; the link analyzer reads the exact id from the dump). Evidence is split
   into **focus** (the transceivers the test waited on), **other-tcvr** (usually not
   evidence) and **global** (no transceiver id, e.g. agent load shedding). It reports service
   restarts, fatal lines, known root-cause signatures (Unsupported Application,
   I2C read failures, bad EEPROM, vendor PHY errors, XPHY eyes zero, CDB/
   firmware failures, LINK_ALERT flags), state-machine failure reasons grouped
   across transceivers, each focus transceiver's final state and last
   transitions, port state machine states, and a normalized WARN/ERR histogram.
3. **Interpret it with the qsfp HW test knowledge**: load the
   `debug-qsfp-hw-test` skill's `references/failure-signatures.md`. Its
   state-machine wrapper, vendor-error, I2C, EEPROM, and tunable-optics
   signatures apply unchanged to a qsfp_service daemon log; its gtest-marker
   guidance does not. Its golden rule carries over: SM `... failed:` WARNs are
   **retry context naming the WHY**, not a terminal verdict by themselves.
4. **Correlate strictly**: only errors on the transceivers the link test
   waited on, inside the failure window, count. Errors on other modules, or
   after the failure (agent restarting: `switch is still initializing or is
   exiting`, `Dropping unsent request`), are noise.

If the qsfp_service log was not provided (provided-logs mode) or cannot be
obtained (switch reprovisioned, file gone, no access), say so explicitly: name
the switch if known, the time window, and the transceivers. Give the best
link-log-only verdict with the per-port evidence, labeled as a hypothesis for
the optics-side cause.

### Step 4: Report

1. **Failing test**: full result name, the gtest it maps to, phase, known-bad status.
2. **Scope**: single test vs whole job (setup / environment).
3. **Killer**: quoted lines with log line numbers and timestamps, the source
   `file:line` that emits it, and one sentence on what it checks.
4. **qsfp_service evidence** (when used): which log, how it was obtained,
   its time span, quoted lines for the focus transceivers.
5. **Root cause**: 1-2 sentences, category (hardware-optic, cabling/peer,
   I2C, module-capability/config, qsfp_service bug, agent/SDK bug, test bug,
   infra/env, known-bad, hang), labeled **confirmed** (quoted) vs
   **hypothesis** (inferred).
6. **What the link log alone showed**, when the qsfp log changed the verdict.
7. **Dismissed noise**: the benign lines present, one phrase each.

## Scripts

| Script | Purpose |
|---|---|
| `scripts/analyze_link_log.sh <log\|->` | Link test (or hw agent) log: failure blocks + phase, gate class, per-port dump table, CHECK/terminate/SAI-init, abort classification + stack, qsfp hand-off decision |
| `scripts/analyze_qsfp_service_log.sh <log\|-> [--tcvr ids] [--from T] [--to T]` | qsfp_service daemon log: restarts, fatal, root-cause signatures, grouped SM failure reasons, focus transceiver timelines, port SM, WARN/ERR histogram |
| `facebook/scripts/fetch_link_failure.sh` | Internal: find recent failures, print result-record fields + same-job scope, download stdout / runner / hw agent logs |
| `facebook/scripts/fetch_qsfp_service_log.sh` | Internal: fetch the qsfp_service log window from the runner upload or the switch |

The two `scripts/` analyzers are pure text parsing (POSIX awk), with no infra
dependencies.

## Reference Routing

For each reference pair below, load the `facebook/` version first if it
exists in your checkout. Otherwise load the `references/` version. Treat
the selected file as the source of truth for that topic.

| Need | Try first | Fallback |
|------|-----------|----------|
| Turn a CI link / result into logs; known-bad check (**fetch mode only**) | `facebook/log-input.md` | `references/log-input.md` |
| Get the qsfp_service log for the failure window (**fetch mode only**; in provided-logs mode read it only for log naming and format) | `facebook/qsfp-log.md` | `references/qsfp-log.md` |
| Killer vs benign signatures (link log and qsfp_service log), gates, abort shapes | — | `references/failure-signatures.md` |
| Suite to source file, what each test verifies, qsfp dependency | — | `references/test-catalog.md` |
| Worked examples from real failures | — | `references/failure-signatures.md` (section 7) |

Worked examples show what a finished verdict looks like. They are **not
evidence**. A log that resembles an example still needs its own quoted lines
for every claim; if the deciding log (for example the qsfp_service log) is
missing, the verdict stays a hypothesis even when an example matches.
