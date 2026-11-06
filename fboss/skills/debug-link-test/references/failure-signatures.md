# Link Test Failure Signatures

All strings are verified against the FBOSS source (`fboss/agent/test/link_tests/`,
`fboss/agent/test/AgentEnsemble*`, `fboss/qsfp_service/`) and against real failed
runs. Line numbers drift between the checkout and the binary that ran: match the
**string and the enclosing function**, then read the source at the revision that
ran if you need exact lines. Use `grep -F` for literal matching.

## 0. How a link test run is shaped (read once)

```text
runner: deploy agent config + qsfp_service (separate process, own log) [+ hw agents]
  -> wait for qsfp_service ACTIVE (+ optics firmware upgrade)
  -> timeout -s ABRT 900 <link_test_bin> --gtest_filter=<Suite.Test> --config <agent cfg> [--setup_for_warmboot]
       AgentEnsembleTest::SetUp            (agent init, initial config apply)
       AgentEnsembleLinkTest::SetUp        (AgentEnsembleLinkTest.cpp SetUp)
         initializeCabledPorts()           cabled = ports with expectedLLDPValues /
                                           expectedNeighborReachability in the agent config
         G1 waitForAllCabledPorts(up, 60, 5s)
         G2 waitForAllTransceiverStates(ACTIVE | TRANSCEIVER_PROGRAMMED in port-manager mode, 60, 5s)
         G3 waitForPortStateMachineState(PORT_UP, 60, 5s)   (port-manager mode only)
       test body (often verifyAcrossWarmBoots)
       AgentEnsembleLinkTest::TearDown   qsfp_service / fsdb alive checks, i2c log dump
         -> AgentEnsembleTest::TearDown -> tearDownAgentEnsemble(): with --setup_for_warmboot
            -> AgentEnsemble::gracefulExit() -> exit(0 | 1 if HasFailure())   (gtest summary never printed)
```

Consequences for reading logs:

- **No `[  FAILED  ]` line is normal.** Cold-boot runs pass `--setup_for_warmboot`;
  after the link-test TearDown checks, `tearDownAgentEnsemble()` calls
  `AgentEnsemble::gracefulExit()` (passes `!HasFailure()`), and the initializer
  exits the process with `exit(1)` on failure (`MonolithicAgentInitializer::handleExitSignal`
  in mono mode; `SplitSwAgentInitializer::exitForWarmBoot` -> `handleExitSignal` in
  multi-switch mode) before gtest prints its summary. The log then ends with `[Exit] Total graceful Exit time`
  and `RestartTimeTracker ... Cannot call restart_time::mark()`. The gtest
  `Failure` block is the verdict.
- Each gate polls ~5 minutes; G1+G2+G3 can consume ~15 minutes. Against the 900 s
  cap, a slow SetUp can be killed by the watchdog (see section 4).
- `qsfp_service` is **not** in the test process. Everything about transceiver
  state, programming, I2C, firmware, and module capability is only in the
  qsfp_service log; the test sees it through Thrift (`[::1]:5910`).
- Transceiver IDs are **0-based** (`Transceiver:6` / `tcvr 6` = front panel
  `eth1/7`); BSP device paths are 1-based (`xcvr_io_7`).

## 1. Link test log — killers (report these)

Scope every failure to the `[ RUN      ] <Suite>.<Test>` window. Failure blocks are
chronological: the first is usually the trigger, but later ones are cascades only
when they depend on it (e.g. a re-wait for links after a failed remediation). Tests
that assert per port (FEC/BER, PRBS lanes) emit independent failures — read the line
before each block for the port.

| Log string | Source | Meaning |
|---|---|---|
| `unknown file: Failure` + `C++ exception with description "Unexpected Link status 0 for <ports>" thrown in SetUp().` | `AgentEnsembleTest::waitForLinkStatus` (throw) called from `AgentEnsembleLinkTest::SetUp` → `waitForAllCabledPorts` | **G1**: cabled ports never came up in ~5 min. Named test body never ran. Before throwing, `logLinkDbgMessage` dumps per port `Debug information for <port>`, `IPHY INFO:`, `XPHY INFO:` / `XPHY info missing`, `Transceiver INFO: TransceiverInfo {...}` — read it (section 3) |
| Same text inside `Actual: it throws facebook::fboss::FbossError with description "Unexpected Link status 0 ..."` at a suite `.cpp:<line>: Failure` | a test body re-waiting for links (`EXPECT_NO_THROW(waitForAllCabledPorts(...))`) | Ports did not come back after the test disturbed them (TX disable, flap, remediation, speed change). Check the FIRST failure in the window — often an earlier expectation (e.g. `numPortsRemediated > 0`) |
| `Unexpected Link status 1 for <ports>` | same helper, waiting for DOWN | Ports stayed up when the test expected them down (TX-disable not effective, wrong peer) |
| `Transceivers:[<ids>] don't have expected TransceiverStateMachineState:<state>` | `LinkTestUtils.cpp` `waitForAllTransceiverStates` → `waitForStateMachineState` (throw) | **G2**: qsfp_service never got these (0-based) transceivers to `ACTIVE` (or `TRANSCEIVER_PROGRAMMED` in port-manager mode). Always qsfp side |
| `Ports:[<ids>] don't have expected PortStateMachineState:<state>` | `LinkTestUtils.cpp` `waitForPortStateMachineState` | **G3** (port-manager mode): port state machine never reached `PORT_UP`. qsfp side |
| `TransceiverInfo was never populated.` | `LinkTestUtils.cpp` `waitForTransceiverInfo` | qsfp_service returned no present transceiver at all (service down, restarting, or every module unreadable) |
| `QSFP Service no longer alive after the test` / `QSFP Service run state no longer active after the test` | `AgentEnsembleLinkTest::TearDown` | qsfp_service crashed or left ACTIVE during the test. Only reached when TearDown runs (not after a warmboot hard exit) |
| `FSDB no longer alive after the test` | same (internal builds) | FSDB died during the test |
| `SW Agent RSS memory <n> above expected <m>` | `AgentEnsembleLinkTest::checkAgentMemoryInBounds` | Agent memory regression (asicLinkFlap) |
| `F<MMDD> ... Check failed: <expr>` | any `CHECK` | Process abort; the expression names the precondition. Example: `AgentEnsemblePrbsTest.cpp ... Check failed: !portsToTest_.empty()` = no connected pair matched the test's media/PRBS predicates (read the preceding `Tcvr to Tcvr PRBS test: ... validMediaA 0 validMediaZ 0 supPrbsA 1 supPrbsZ 1` rows: media mismatch vs PRBS unsupported vs no pairs) |
| `SwSwitch.cpp ... encountered a fatal error: FbossError: failed to find port: <id>` preceded by `MultiHwSwitchHandler ... Failed to get state update result for switch id <n>` | `SwSwitch` state-update fatal path; `SaiPlatform::getPort` | A state update referenced a port the (switch-local) platform mapping does not own → agent abort. Config / switch-scope mismatch |
| `Failed to create sai entity SwitchSaiId(0)` → `Terminated due to: ...` → SIGABRT, stack in `SaiSwitch::initLocked` | `fboss/agent/hw/sai/api/SaiApi.h` (`saiApiCheckError` in `SaiApi::create`; logged from `SaiApiError.h`) | SAI switch init failed before any test. The lines just above are the vendor SDK's reason (e.g. `bcm_init() Failed`, `initialization command "INIT_DNX" failed`) — vendor output, not an FBOSS source line |
| `switch initialization failed: <exception>` | `SwAgentInitializer` init thread | Agent init threw |
| `Unsupported field/action for aclEntry: <name>` followed by an abort with `RefMap` / `SaiAclTableManager` / `SaiSwitch::processAclTableGroupDelta` frames | `SaiAclTableManager` (warning) + `SaiSwitch` (state apply) | ACL programming on this ASIC/SDK rejected an entry; the counter lifetime path then aborted (seen on warmboot roundtrips) |
| `<file>.cpp:<line>: Failure` with `Expected: (inDiscards) <= (maxNumDiscards)` preceded by per-port `Port: <id> in discards: <n>` lines | `AgentEnsembleTest::assertNoInDiscards` (`EXPECT_LE`); sibling `assertNoInErrors` | **Links are up but traffic was lost** — the core check of `warmbootIsHitLess`, `qsfpWarmbootIsHitLess`, `ptpEnableIsHitless`, `fabricLinkHealth`, MACsec hitless. Name the ports with nonzero discards and the phase (during warmboot / qsfp restart / PTP enable). Not an optics-down problem: go to agent/SDK (buffer, ECMP, hitless programming) first; the qsfp log matters only if the qsfp warm restart reprogrammed a port (look for disruptive programming around the restart) |
| `FEC different on both ends of the link: <fecA><fecB>` | `AgentEnsembleLinkTest::getPortPairsForFecErrInj` (legacy `LinkTest`) `throw FbossError` | The two ends of a cabled pair have different FEC in their port profiles — agent config / platform-mapping mismatch, not a link-quality issue |
| `No VLAN found for aggregate port in addAggPort` | `fboss/agent/test/TrunkUtils.cpp` (called from `AgentEnsembleLacpSanityTests.cpp`) | LACP test selected ports without VLAN membership (e.g. fabric ports on multi-switch) — test/topology bug |
| `Expected: (rsFecNow->preFECBer().value()) != (0), actual: 0 vs 0` | `AgentEnsemblePhyInfoTest.cpp` `verifyIphyFecBerCounters` | Corrected codewords moved but the sampled pre-FEC BER reads 0. The test pairs a codeword delta from its own two snapshots with a BER computed by the PHY over its own interval, so a sampling/measurement inconsistency is the likely reading (hypothesis) unless BER is also elevated on other samples; list the affected ports from the line before each block |
| `Value of: waitForSwitchStateCondition(aggPortUp, ...)` / partner `IN_SYNC` expectations | `AgentEnsembleLacpSanityTests.cpp` | LACP aggregate did not converge; check member links first |
| `Did not detect any optical transceivers` / `No eligible optical ports found for speed change` / `No optics capable of this test` / `No cabled interface-port pair available` | optics / speed-change / VDM / debounce suites | Topology/applicability: the lab setup has no qualifying optics for this test. Often a known-bad candidate, not a product bug |
| `Hw Agent exited with non-zero status code.` (result record) | runner post-test check | Multi-switch: the **hw agent** exited badly even if the test binary printed `[  PASSED  ]`. Analyze the hw agent log (same analyzer) |
| `Failed to start hw agent before test run.` / `Failed to start qsfp_service on <host>` / `qsfp_service is not active on <host>. Current state = <s>` (result record / runner) | runner setup | Setup failed before the binary ran; for qsfp, go to its log |

## 2. Link test log — benign (dismiss)

| Log string | Why benign |
|---|---|
| `Checking link status on <ports>` | Progress of the G1 poll |
| `XPHY info missing for <port>` | Platforms / ports with no external PHY; debug-dump filler |
| `IPHY INFO:` / `Transceiver INFO:` dumps | Diagnostics printed *because* of a G1 failure; evidence, not a second failure |
| `Failed to call qsfp_service getInterfacePhyInfo()` / `getTransceiverInfo()` inside the debug dump | Best-effort evidence collection. A handful inside a retry loop is interim; **many** across the whole run means qsfp_service was down (then it matters) |
| `fboss_mka_service_conn: Reconnecting to server on port: 5990` / `PacketStreamClient.cpp ... Connect to server failed` | MKA not deployed / not needed for this test |
| `getConfigAppliedInfo thrift request received/succeeded` | qsfp_service polling the agent |
| `No lldp neighbors on : <port>` / `Wrong lldp neighbor size for port <p>, should be 1 but got <n>` | Interim inside LLDP waits; terminal only via the enclosing eventual assertion |
| `Failed to dump i2c log for port <port>` | Best-effort teardown (passive cables have no EEPROM) |
| `... is on-board optics, skip it` / `is an active cable, skip it` / `is not optics, skip it` / `TransceiverInfo ... is not present, skip it` | Candidate filtering |
| `runImmediatelyOrRunInFbossEventBaseThreadAndWait for non-running ...` | Emitted during shutdown |
| `RestartTimeTracker ... Cannot call restart_time::mark() before restart_time::init` | Harmless at test exit |
| `==<pid>==WARNING: ASan doesn't fully support makecontext/swapcontext` | Sanitizer build banner |
| `Exception <e>` then `Trace:` right before a rethrow | Diagnostic of the assertion that follows |

Rule of thumb for glog severity: `V`/`I` noise, `W`/`E` suspects (confirm against
section 1), only `F` is a failure by itself. gtest `Failure` lines have no glog
prefix, which is why they hide in the flood.

## 3. Reading the per-port debug dump (G1)

`analyze_link_log.sh` turns it into a table. Fields and what they mean:

| Field (in `Transceiver INFO`) | Decode |
|---|---|
| `present (bool)` | Module seen. `false` with `communicationError (bool) = true` = the last I2C refresh **failed** (not proof the slot is empty) |
| `stateMachineState (i32)` | enum in `qsfp_service/if/transceiver.thrift` `TransceiverStateMachineState`: 0 NOT_PRESENT, 1 PRESENT, 2 DISCOVERED, 3 IPHY_PORTS_PROGRAMMED, 4 XPHY_PORTS_PROGRAMMED, 5 TRANSCEIVER_PROGRAMMED, 6 ACTIVE, 7 INACTIVE, 8 UPGRADING, 9 TRANSCEIVER_READY. `9` = programming never finished; `7` with ports down = links down |
| `SignalFlags` `rxLos` / `rxLol` / `txLos` / `txLol (i32)` | **Per-lane bitmasks** (`transceiver.thrift` `struct SignalFlags`). `240` = `0xF0` = lanes 4-7; `255` = all 8 lanes |
| `mediaLaneSignals[].rxLos/rxLol (bool)` | Same, per media lane |
| `hostLaneSignals[].txLos/txLol` | Host (ASIC→module) side; `true` = no signal from the ASIC into the module |
| `partNumber` / `tcvrName` | Module identity; `tcvrName` `eth1/7` = transceiver 6 |
| `remediationCounter` | Remediation attempts by qsfp_service |
| `IPHY ... signalDetectLive` / `cdrLockLive` | ASIC SerDes receive side |

Patterns:

| Pattern | Likely cause | Next |
|---|---|---|
| Same lanes dark on several modules (e.g. only `/5` and `/7` ports, `rxLos=0xF0`) and state `TRANSCEIVER_READY` | Module rejected the requested application for those lanes, or a shared peer/fiber path | qsfp log: `Unsupported Application` (section 5) |
| `present=false` + `communicationError=true` on several modules | I2C / BSP access failure | qsfp log: `BspTransceiverIO::read() failed` |
| All lanes `rxLos=255`, state `INACTIVE`, module readable | No light from the peer (peer down, fiber, peer optic) | qsfp log on **both** ends if available |
| Module `ACTIVE`, media lanes clean, IPHY `signalDetectLive=false` / `cdrLockLive=false` | Host-side SerDes / ASIC / profile mismatch | agent side; qsfp log only to rule out |
| `Transceiver info missing for <port>` on every port | qsfp_service not answering | qsfp log: restarts / crash |

## 4. Aborts: crash vs watchdog

`*** Aborted at <unix>` then `*** Signal 6 (SIGABRT) ... received by PID <A> ... (maybe from PID <B>, ...) (code: ...)`:

| Header | Meaning | Diagnosis |
|---|---|---|
| `B == A`, `sent by tkill or tgkill` | **Self-abort**: CHECK / `XLOG(FATAL)` / `std::terminate` / sanitizer | The stack **is** the diagnosis. Skip handler/libc/glog frames; the first `facebook::fboss::` frames localize it. The `F`-line or `Terminated due to:` just above names it. A `terminate` can mask the original error — trust the stack |
| `B != A`, `sent by kill, sigsend or raise`, at ~the command timeout (900 s; 7200 s for stress) | **External kill**: the runner's `timeout -s ABRT` watchdog — a **hang** | Frames show where the receiving thread was. If it is the test thread (e.g. `AgentEnsembleLinkTest::SetUp` → `waitForLinkStatus` → `SwSwitch::getPortStatus`), SetUp was still polling at the cap: the root cause is why links/transceivers never converged (treat like G1/G2). If it is an unrelated thread (log writer, event base), read the last test-thread log lines before the abort |

Precedence: a named cause before a near-cap abort is the **cause**; the timeout is the
**mechanism**. Pure "hang" only when nothing is named.

Vendor-SDK frames (`_brcm_sai_*`, `bcmimm_*`, `sal_*`, Leaba/NVIDIA SDK symbols) with no
preceding assertion text localize the **subsystem and phase** (e.g. SDK unit shutdown during
warmboot exit) and confirm the abort mechanism; they do not tell you the violated invariant.
Say "vendor SDK assert in <function> during <phase>", not "double free" or "corruption",
unless the log states it.

## 5. qsfp_service log — what matters

`analyze_qsfp_service_log.sh` summarizes these. Format: syslog prefix (optional) +
glog line; every line carries `<sev><MMDD> <HH:MM:SS>`. State machine lines are
`[SM]Transceiver:<id> ...` (`TransceiverStateMachine.h`) and
`[Port: <name>, PortID: <id>] State changed from X to Y` (`PortStateMachine.h`).

| Log string | Source | Meaning |
|---|---|---|
| `CmisModule.cpp ... Unsupported Application` then `[SM]Transceiver:N programTransceiver failed:...FbossError: Port: N Unsupported Application by the module:` | `CmisModule::getAppSelCodeForSpeed` throws; `TransceiverStateMachine.h` programTransceiver wrapper logs WARN + retries | The module does not advertise an application for the requested speed / host-lane split. Programming never completes (state stuck `TRANSCEIVER_READY`); the affected lanes' datapath never comes up. Config (port profile) vs module capability — typical for new optics / new profiles. Lines before it (`Trying to set application code for speed <S> on startHostLane <L>`, `Application codes supporting current speed: <n>`) show what was asked |
| `WedgeQsfp.cpp ... Read at offset <o> ... failed: BspTransceiverIO::read() failed to read from tcvr <n+1>: ... errno = Input/output error` (or `Bad address`) / `failed to read management interface` | `WedgeQsfp::readTransceiver` | I2C / BSP read failure. Many modules = bus/FPGA/BSP; one module = module/cage |
| `Unknown media lane code byte`, `is missing transceiverManagementInterface`, `no tunable optics config`, `BaldEagleError`, `BcmPhyError`, `MdioError` | see debug-qsfp-hw-test `references/failure-signatures.md` | Same meaning as in qsfp HW tests |
| `XPHY<n> lane<m> eyes still zero after 3 tries.` | Credo XPHY driver | No signal into the external PHY lane |
| `fetchFwBuildNumberFromCdb: CDB command failed` | `CmisModule` | CMIS CDB not answering (firmware / module) |
| `TCVR_EV_UPGRADE_FIRMWARE Failed to apply` / firmware upgrade errors | state machine / firmware upgrader | Optics firmware upgrade did not complete |
| `FBOSS_EVENT(LINK_ALERT): reported latched link fault flags on ports [...]: txLos=... rxLos=... txLol=... rxLol=... txFault=...` | `QsfpModule` (LINK_ALERT on change) | Module-reported latched faults at that moment; hex per-lane masks (`rxLos=0xff rxLol=0xff` = no light / no lock on all lanes: optic, fiber, or peer TX) |
| `Overriding default flag from config: remediation_enabled=false` | qsfp_service startup flag from the qsfp config | Remediation is off for this run: `testOpticsRemediation` must fail with `numPortsRemediated ... 0 vs 0` and TX-disabled links stay down. Config/policy, usually known-bad — not an optics fault |
| `init() failed to release reset TCVR` on every transceiver, only at service start | BSP CPLD access | Startup transient; causal only if it persists or is followed by presence/programming failures |
| `[SM]Transceiver:N State changed from X to Y` | `TransceiverStateMachine.h` | The timeline. A transceiver the link test waited on whose last state is not ACTIVE (or TRANSCEIVER_PROGRAMMED in port-manager mode) is the qsfp-side cause |

Benign in the qsfp_service log (agent restarting between tests, or not up yet):

| Log string | Why |
|---|---|
| `programInternalPhyPorts failed:...FbossBaseError: switch is still initializing or is exiting and is not fully configured yet` | The agent is exiting/starting; qsfp retries |
| `programInternalPhyPorts failed:...TTransportException: Dropping unsent request. Connection closed ... Connection refused` | Agent not listening yet |
| `Failed to call wedge_agent getConfigAppliedInfo()` | Same |
| `Skipped queueing event: TCVR_EV_PROGRAM_IPHY, since exit already started` | qsfp_service shutting down |
| `readyTransceiver returned False` (a few, early) | Transient during bring-up |
| `Unable to get xphy info when PhyManager is not set` | No XPHY on this platform |
| `FsdbStreamClient ... ALL_PUBLISHERS_GONE` | Publisher (agent) exited |
| `Thrift serialization is only defined for structs and unions` | Logging quirk |

A new `QSFP Service PHY SDK Version:` banner (or a new PID in the syslog prefix)
inside the window is a **restart**: intentional in `qsfpWarmbootIsHitLess` /
`qsfpColdbootAfterAgentUp`, a crash otherwise.

**Time-span rule:** a qsfp_service log is only evidence if its glog span covers the
failure time. Per-run logs are named by service start time; a roundtrip or version
change restarts the service into a new file, so a same-switch log can end minutes
before the failure.

## 6. Known-bad and unsupported

The OSS-synced known-bad list is
`fboss/oss/link_known_bad_tests/agent_ensemble_link_known_bad_tests.materialized_JSON`
(legacy: `fboss_link_known_bad_tests.materialized_JSON`), keyed by test config
`<platform>/<sai|bcm>/asicsdk-<from>/<to>[/physdk-<from>/<to>]`, entries
`test_name_regex` (search semantics). Known-bad tests still run; their failures are
tolerated. Unsupported tests are skipped before running. Several list variables
named `unsupported_*` are actually known-bad patterns — check which list the entry
is in, not its name.

## 7. Worked examples (real failures)

| Result | Link log alone | Root cause (with qsfp log where used) |
|---|---|---|
| `cold_boot.AgentEnsembleLinkSanityTestDataPlaneFlood.qsfpWarmbootIsHitLess` (montblanc; every test in the job failed) | G1: `Unexpected Link status 0 for eth1/7/5,eth1/7/7,eth1/11/5,...,eth1/19/7`; 4 modules `TRANSCEIVER_READY`, `rxLos=240` (lanes 4-7) | qsfp: `Transceiver:6 eth1/7/1: Unsupported Application` / `programTransceiver failed: ... Unsupported Application by the module` on tcvr 6/10/14/18 for the whole run. Module does not support the 2-host-lane 200G application; lanes 4-7 never activated. **qsfp log required** |
| `cold_boot.AgentEnsembleFsdbTest.statsPublishSubscribe` (morgan800cc; whole job) | G1 on 16 ports; 6 modules `present=false`, 2 `INACTIVE` with `rxLos=255` | qsfp: `BspTransceiverIO::read() failed ... errno = Bad address` x700 each on 6 modules — BSP/I2C access failure on the lab device; environment, not FSDB |
| `cold_boot.AgentEnsembleEmptyLinkTest.CheckInit` (wedge400; whole job) | G1 on 4 ports; modules `present=false`, `INACTIVE`, `communicationError=true` | I2C refresh failure (qsfp log no longer on device) — hypothesis-level on the physical cause |
| `cold_boot.AgentEnsembleQsfpFsdbTest.portState` (meru800bfa multi-switch; 16 tests x3 attempts) | `failed to find port: 2048` for switch id 2 → `encountered a fatal error` → self-abort | Port ownership/scope mismatch between config and the switch-local platform mapping; no qsfp involvement |
| `cold_boot.Prbs_TCVR_L_P31Q_TO_TCVR_L_P31Q_FR1_200G.prbsSanity` (fuji; known-bad) | `validMediaA 0 validMediaZ 0 supPrbsA 1 supPrbsZ 1` for every pair, then `Check failed: !portsToTest_.empty()` | No FR1_200G optics in the lab setup; applicability, suppressed by policy |
| `roundtrip...cold_boot.AgentFabricLinkTest.linkActiveAndLoopStatus` / `cold_boot.AgentEnsembleLinkTest.getTransceivers` | External SIGABRT at ~899 s; test-thread stack in `AgentEnsembleLinkTest::SetUp` → `waitForLinkStatus` → `SwSwitch::getPortStatus` | Watchdog: SetUp was still polling links at the 900 s cap (slow ASAN build + links not converging). Cause is the link convergence, mechanism is timeout |
| `roundtrip...warm_boot.AgentEnsembleLinkTest.xPhyInfoTest` (yamp) | Self-abort, `terminate` in `SaiSwitch::processAclTableGroupDelta` / `RefMap` | Warmboot ACL programming: `Unsupported field/action for aclEntry: mpls-dest-nomatch`, counter reference lifetime → abort. Agent bug on that SDK/roundtrip |
| `roundtrip...warm_boot.AgentEnsembleQsfpFsdbTest.phy` (montblanc multi-switch) | Binary `[  PASSED  ]`; result message `Hw Agent exited with non-zero status code.` | hw agent log: self-abort `_brcm_sai_switch_assert` in `bcmimm_unit_shutdown` during warmboot exit — vendor SDK assert in hw agent shutdown |
| `cold_boot.AgentEnsembleEmptyLinkTest.CheckInit` (meru800bia_c0) | `bcm_init() Failed`, `INIT_DNX failed`, `Failed to create sai entity SwitchSaiId(0)`, self-abort in `SaiSwitch::initLocked` | SAI/SDK switch init failure — environment/SDK, every test on that run fails |
| `switch.2.cold_boot.AgentEnsembleLacpTest.lacpFlap` (meru800bfa; known-bad) | `No VLAN found for aggregate port in addAggPort` in the test body | Multi-switch LACP test picked fabric ports without VLANs — test bug |
| `cold_boot.AgentEnsembleLinkTest.verifyIphyFecBerCounters` (icecube) | `AgentEnsemblePhyInfoTest.cpp:599: Failure ... preFECBer ... != 0, actual: 0 vs 0` | Likely sampling inconsistency between the codeword delta and the BER sample (hypothesis) — not a link-quality failure by itself |
