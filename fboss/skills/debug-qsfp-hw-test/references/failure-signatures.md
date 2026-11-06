# QSFP HW Test Failure Signatures

All log strings below are verified from the QSFP HW test source under
`fboss/qsfp_service/test/hw_test/`. Use `grep -F` for literal matching.

## 1. Killer signatures (report these)

Scope every failure to its own gtest window: from `[ RUN      ] <Suite>.<Test>`
to the matching `[  FAILED  ] <Suite>.<Test>`. The trailing `N FAILED TEST(S)`
block is a quick index when a binary runs many tests. Search **backwards**
from each `[  FAILED  ]` line. The first match is almost always the trigger;
later ones are often cascades. The `(NNNNNN ms)` wall-clock on a FAILED line
near a timeout cap (e.g. ~900000 ms) means suspect a hang, not a logic
failure.

| Log string | Source | Meaning |
|------------|--------|---------|
| `.cpp:<line>: Failure` or `unknown file: Failure` + `Value of:` / `Actual:` / `Expected:` / `C++ exception ... thrown in the test body` | gtest, emitted by any `EXPECT_*`/`ASSERT_*` in the test body | The failing assertion. Quote the whole block including `Which is:` lines. `unknown file` + `accessing unset optional value` means an unguarded `.value()` on an empty optional |
| `HeartbeatMissedCount` in a `TearDown` failure | `HwTest.cpp` `TearDown()` `EXPECT_EQ(...getStateMachineThreadHeartbeatMissedCount(), 0)` | A deadlocked state-machine thread, not the test body |
| `forceColdBoot` in a `TearDown` failure | `HwTest.cpp` `TearDown()` `EXPECT_FALSE(checkFileExists(...forceColdBootFileName()))` | A cold-boot flag leaked past the test |
| `Verify with retry failed` | `fboss/lib/CommonUtils.h` `checkWithRetry()` | `refreshTransceiversWithRetry()` or `waitTillCabledTcvrProgrammed()` exhausted retries |
| `Ports that did not reach` | `HwTest.cpp` `waitTillCabledTcvrProgrammed()` `XLOG(ERR)` | The SetUp programming gate failed; tests that never ran are collateral |
| `SIGSEGV`, `SIGABRT`, `signal killed`, `*** Aborted at`, symbolized `@ 0x...` stack | crash handler / sanitizer | Real crash. **Read the stack**: skip the `signalHandler`/`__tsan`/`abort`/`raise` preamble and find the first `facebook::fboss::` frames — they localize it (e.g. `BspPimContainer::BspPimContainer` → `HwQsfpEnsemble::init`). Core path vs `No core dumps found` distinguishes segfault from clean abort. `analyze_qsfp_log.sh` surfaces these frames for you |
| `Failed to create sai entity SwitchSaiId` / `terminate called after throwing ... SaiApiError` | `fboss/agent/hw/sai/api/SaiApi.h:89-99` (`saiApiCheckError` in `SaiApi::create`) | SAI switch/entity init failed → abort before any test body. A preceding vendor line `[Coldboot] ReadProfile() failed ... <profile>.xml` (vendor SAI logging, **not** FBOSS — 0 hits in `fboss/`) names the missing firmware profile. Env / firmware-provisioning |
| `is missing transceiverManagementInterface` / `is missing moduleMediaInterface` | `HwTransceiverUtils.cpp:677-681` (`verifyDataPathEnabled`) / `:715-723` (`verifyDiagsCapability`) `throw FbossError` | Module came up unreadable / without a management interface. **Numbering trap:** `Transceiver:N` here prints `*tcvrState.port()` — the **port** number, not a transceiver id; map via platform mapping before blaming transceiver N |
| `Unknown media lane code byte: <n>` | `TransceiverPropertiesManager.cpp:328-334` map miss → `throw FbossError` (SFF/CMIS shared) | Module reports an unmappable media lane code (`0` = blank/garbled EEPROM) → programming rejected. Surfaces via the `programTransceiver failed:` wrapper |
| ABRT near the timeout cap | test runner `timeout -s ABRT <cap>` wrapper | **Precedence:** if a *named* cause precedes the near-cap elapsed, report the named cause as the **cause** and timeout as the **mechanism** ("the SetUp gate retried ~14 min before throwing"). Reserve pure "timeout/hang" for ABRT-at-cap with **no** named cause above it |
| Every test fails identically / failure in `HwTest::SetUp()` / `HwQsfpEnsemble::init()` / `waitTillCabledTcvrProgrammed` (e.g. `EmptyHwTest.CheckInit`) | harness SetUp | Run-level **scope, not the cause — read the stack to localize.** Split *environmental* (missing firmware/service/cabling — e.g. the SAI `ReadProfile` case) vs *code/threading bug in init* (e.g. `terminate called without an active exception` from a joinable `std::thread` destroyed during `BspPimContainer` construction). A `terminate` can **mask** the original error (nothing logged before it) — trust the stack |
| `should NOT be remediated but transceiverJustRemediated is true` | `HwStateMachineTest.cpp` `refreshStateMachinesTillMeetAllStates()` | Unexpected remediation during the refresh cycle |
| `should NOT be remediated during config change` | `HwStateMachineTest.cpp` `CheckAgentConfigChanged` | Remediation fired during a config change with ports up |

**Retry/context wrappers are NOT terminal.** State-machine WARN lines —
`programExternalPhyPorts failed:`, `readyTransceiver failed with abort:`,
`programTransceiver failed:`, `discover transceiver failed:`,
`programInternalPhyPorts failed:`, `tryRemediateTransceiver failed:`
(`fboss/qsfp_service/TransceiverStateMachine.h:344/365/386/410/446/508`) — are
`XLOG(WARN)` + retry. They usually carry the *real reason* (a vendor `BaldEagleError` /
`BcmPhyError` / `MdioError`, a tunable-optics config gap, a bad EEPROM), so read them as
**context/why** — but the *terminal verdict* is the SetUp gate (`Never got all
transceivers programmed`) or a gtest assertion. Vendor error classes live under
`lib/phy/facebook/` (internal-only): match the **string**, don't cite the header in OSS.

If there is no `[  FAILED  ]` line but the binary died, it was a fatal
`CHECK` abort (see section 3). The last 50 lines before death are the
killer.

## 2. Benign signatures (dismiss these)

These appear on healthy runs. Quote-but-dismiss them; never report as cause
unless paired with a killer from section 1.

| Log string | Source | Why it is benign |
|------------|--------|------------------|
| `transceiverInfo not returned after refresh` | `HwTest.cpp` `refreshTransceiversWithRetry()` `XLOG(WARN)` | Retry predicate returning false; a later retry may succeed |
| `timestamp in the TransceiverInfo after refresh =` | Same function | Refresh raced the cache; same retry logic as above |
| `Transceivers that should be programmed but are not:` | `HwTest.cpp` `waitTillCabledTcvrProgrammed()` `XLOG(WARN)` | Interim SetUp polling; tunable optics can need ~100s |
| `Skipping port ... - not managed by qsfp_service (no TRANSCEIVER or XPHY)` | `HwTest.cpp` `waitTillCabledTcvrProgrammed()` `XLOG(INFO)` | Backplane ports are out of scope by design |
| `is present but not in the config` | `HwTest.cpp` `dumpPortErrors()` `XLOG(WARN)` | Lab hygiene telemetry (`EXTRA` row), emitted from `TearDown` on passing runs too |
| `Could not map port ... to a transceiver for test metadata:` | `HwTest.cpp` `addTestedPorts()` `XLOG(WARN)` | Breakout/backplane port in metadata path; best-effort only |
| `Failed to build qsfp hw test port info` / `Failed to build qsfp hw test port errors` | `HwTest.cpp` `dumpTestMetadata()` / `dumpPortErrors()` | Telemetry dump is best-effort and never fails the test |
| `Failed to write qsfp hw test port info to` / `Failed to write qsfp hw test port errors to` | Same | Same; file-write failure is telemetry-only |
| `BspTransceiverIOTrace: read()/write() successfully ...` | per-I2C-op verbose trace | Normal I/O volume; skim past |
| `LINK_SNAPSHOT_EVENT ... <linkSnapshot:{ ... }>` multi-KB JSON | `SnapshotManager.cpp` | Routine snapshot dump; pure volume, skim past |
| `FsdbPublisher ... Cancelling` / `... Cancel(l)ed` / `... Destroying` / `QsfpFsdbSubscriber stopped` | process teardown | Shutdown runs on pass and fail; it is after the failure, skim past |
| `Stopping thread heartbeat watchdog` / `Terminated TransceiverStateMachineUpdateThread` | thread teardown | Normal shutdown |
| `communicationError:true` on a module with `present:false` | empty slot report | Expected; no module to talk to |
| `rxLos:255`, `rxLol:255`, `rxPwrdBm:-40`, `txBias:0` on absent/dark lanes | dark-lane telemetry | No light on unused lanes; expected |
| `state:INACTIVE` for a dark/uncabled transceiver | state report | Only relevant if a cabled/expected transceiver is stuck |
| `doesn't have expected state=` | `HwStateMachineTest.cpp` `meetAllExpectedState()` / `meetAllExpectedPortState()` `XLOG(WARN)` | Per-iteration retry noise; only the final `EXPECT_EVENTUALLY_*` verdict counts |
| `transceivers don't meet the expected state` / `ports don't meet the expected state` | `HwStateMachineTest.cpp` `refreshStateMachinesTillMeetAllStates()` `XLOG_IF(WARN, ...)` | Same retry-loop noise |
| `XPHY not present in system` | `HwXphyFirmwareTest.cpp` `XLOG(ERR)` + `continue` | Generic mapping covers PIMs without XPHY; the test skips them |
| `Skip verifying lane map` / `Skip verifying ... for passive copper cable` | `HwTransceiverUtils.cpp` | DACs are never programmed; skipped by design |
| `Combination of firmware version and dsp firmware version is not validated. This will not affect overall config validity.` | Transceiver validation logging | Firmware-only mismatch does not fail validity |
| `STAGE: cold boot setup()` / `STAGE: verify` / `STAGE: setupForWarmboot` | `HwTest.h` `verifyAcrossWarmBoots()` `XLOG(INFO)` | Progress markers, not results |
| `HwTest::SetUp() complete` | `HwTest.cpp` `SetUp()` `XLOG(INFO)` | Progress marker |
| `Transceiver ... has a validated configuration` | Transceiver validation logging | Informational |
| `Triggering firmware upgrade for interfaces:` | `OpticsFwUpgradeTest.cpp` `XLOG(INFO)` | Progress marker |

## 3. Fatal `CHECK` aborts (no FAILED line)

`CHECK_*` comes from `folly/logging/xlog.h` and aborts the process. In QSFP
HW tests it guards test-infra preconditions:

| Log string | Source | Meaning |
|------------|--------|---------|
| `Check failed: !insertedTransceiversMap.empty()` (or nearby `CHECK`) | `HwTest.cpp` `CheckTcvrNameAndInterfaces` | No transceivers discovered at all |
| `Check failed: !xphyPortAndProfiles.empty()` (or nearby `CHECK`) | `HwExternalPhyPortTest.cpp` | No XPHY ports available for the test |
| `Check failed: aPortID.has_value()` (or nearby `CHECK`) | `HwI2cSelectTest.cpp` | Port-to-transceiver mapping missing |
| `Check failed: !tcvrsToTest.empty()` (or nearby `CHECK`) | `OpticsFwUpgradeTest.cpp` | No upgrade candidates found |
| `FbossError:` with test context | `fboss/agent/FbossError.h` thrown from test helpers | Programming error, e.g. `Can't find platform port` |

A `CHECK` abort kills the binary: subsequent tests show as not-run. Report
the abort location as the killer and list the not-run tests as collateral.

Rule of thumb for glog severity (`<severity><MMDD> ...` prefix): `V`/
`I` lines are noise, `W`/`E` are suspects (confirm against section 1
before citing), and only `F` (fatal) is a failure by itself. gtest
`Failure` lines are not glog — they have no severity prefix — which is why
they hide in the flood.

## 4. Context hints (lines just before the killer)

The lines just **before** the section-1 signature usually explain *why*.
When they correlate in time with the failure and concern an
expected/cabled transceiver, treat them as signal:

| Hint near the failure | Likely cause |
|---|---|
| Cabled transceiver never reaches `TRANSCEIVER_PROGRAMMED`; stuck `INACTIVE`/`NOT_PRESENT` | Programming/state-machine failure or bad cabling |
| `numReadFailed>0` / `numWriteFailed>0`, repeated `BspTransceiverIO::read() failed` on a `present:true` module | Real I2C / hardware issue; errors across many modules point at bus/FPGA, on one module at module/cable |
| `communicationError:true` on a `present:true` module | Module comms fault |
| CMIS module not reaching expected state, `fwFault!=0`, firmware/DSP version mismatch | Firmware / module issue |
| Config-validation mismatch (expected vs actual appSel / mediaInterface / FEC) | Config or platform-mapping bug |
| Failure only on an upgraded SDK/firmware leg | Regression tied to that upgrade |
| All tests fail at the same setup point | Environmental (service init, lab device), not the test |

Always label each conclusion **confirmed** (quoted from the log) vs
**hypothesis** (inferred).

## 5. Phase prefixes

- **Cold-only failure** — points at `SetUp` / the programming gate
  (`waitTillCabledTcvrProgrammed`) or the cold half of
  `verifyAcrossWarmBoots`.
- **Warm-only failure** — points at state preservation across warmboot
  (`can_warm_boot` flag path, `setupForWarmboot` / `gracefulExit`).
- **Both phases fail** — points at the test body itself.
