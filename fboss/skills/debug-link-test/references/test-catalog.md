# Link Test Catalog

Suites live in `fboss/agent/test/link_tests/`. The agent ensemble binaries are
`sai_mono_link_test-<impl>-<sdk>` (SwSwitch + HwSwitch in one process),
`sai_multi_switch_link_test-<impl>-<sdk>` (SwSwitch + separately launched hw agents;
`sai_multi_link_test-*` in the open-source CMake build) and
`agent_ensemble_bcm_link_test-<sdk>` (native BCM, mono). Legacy `LinkTest`-based
binaries (`sai_link_test-*`, `bcm_link_test-*`) share most test names without the
`AgentEnsemble` prefix. Every test inherits the SetUp gates and TearDown checks in
`failure-signatures.md` sections 0-1.

qsfp column: **Q** = direct qsfp_service RPC / CLI / restart in the body (a failure
can be rooted in qsfp_service even after SetUp passed); **q** = qsfp only through the
SetUp link/transceiver gates.

## Result-name prefixes (strip before mapping)

| Prefix | Meaning |
|---|---|
| `cold_boot.` / `warm_boot.` | Phase. Cold runs with `--setup_for_warmboot` (hard exit, no gtest summary); warm verifies after the agent warmboots |
| `warm_boot_for_warm_boot.<n>.` | Repeated warmboot iteration |
| `roundtrip.<from>_2_<to>_2_<from>.` | Cross-version warmboot (e.g. preprod→trunk→preprod). A failure only here suggests warmboot/state incompatibility between versions |
| `switch.<id>.` | Multi-switch runs: logical switch ID under test (`--switch_id_for_testing`) |

## Suites

| Suite / test | File | What it verifies | qsfp |
|---|---|---|---|
| `AgentEnsembleEmptyLinkTest.CheckInit` | `AgentEnsembleEmptyLinkTest.cpp` | Setup sanity: init + all gates + transceiver config-validation query. **If this fails, the setup is broken and every other test in the run will fail too** | Q |
| `AgentEnsembleLinkTest.asicLinkFlap` | `AgentEnsembleLinkSanityTests.cpp` | All cabled ports / transceivers / port SM go down and up; memory bounded | Q |
| `AgentEnsembleLinkTest.getTransceivers` | `AgentEnsembleLinkSanityTests.cpp` | Each cabled port has a transceiver spec and index mapping | Q |
| `AgentEnsembleLinkTest.trafficRxTx` | `AgentEnsembleLinkSanityTests.cpp` | LLDP reachability on all cabled links | q |
| `AgentEnsembleLinkTest.opticsTxDisableEnable` / `opticsTxDisableRandomPorts` | `AgentEnsembleLinkSanityTests.cpp` | TX-disable optics (via `wedge_qsfp_util`) takes links down; re-enable brings them back | Q |
| `AgentEnsembleLinkTest.testOpticsRemediation` | `AgentEnsembleLinkSanityTests.cpp` | TX disable → qsfp_service remediation counter increases → links recover | Q |
| `AgentEnsembleLinkTest.qsfpColdbootAfterAgentUp` | `AgentEnsembleLinkSanityTests.cpp` | Cold-restart qsfp_service with the agent up; everything reconverges | Q |
| `AgentEnsembleLinkTest.fabricLinkHealth` | `AgentEnsembleLinkSanityTests.cpp` | Fabric traffic without in-errors / uncorrectable FEC | q |
| `AgentEnsembleLinkSanityTestDataPlaneFlood.warmbootIsHitLess` / `qsfpWarmbootIsHitLess` / `ptpEnableIsHitless` | `AgentEnsembleLinkSanityTests.cpp` | Agent warmboot / qsfp_service warm restart / PTP enable is hitless under L3 flood (no discards, ECMP intact) | Q |
| `AgentEnsembleOpticsTest.verifyTxRxLatches` | `AgentEnsembleOpticsTest.cpp` | Host TX LOS/LOL and peer media RX LOS/LOL latch and clear | Q |
| `AgentEnsembleLinkTest.opticsVdmPerformanceMonitoring` | `AgentEnsembleOpticsTest.cpp` | VDM pre-FEC BER / FEC tail / SNR within thresholds | Q |
| `Prbs_<A>_TO_<B>_<MEDIA>.prbsSanity` (e.g. `Prbs_TCVR_L_P31Q_TO_TCVR_L_P31Q_FR1_200G`, `Prbs_ASIC_P31_TO_TCVR_S_P31Q_DR4_800G`, `Prbs_ASIC_P31_TO_ASIC_P31`) | `AgentEnsemblePrbsTest.cpp` | Enable PRBS on the named components for pairs with the named media; lock, no loss of lock, BER within threshold. SetUp `CHECK(!portsToTest_.empty())` if no pair qualifies | Q (TCVR components) |
| `AgentEnsembleSpeedChangeTest.<FROM>To<TO>` | `AgentEnsembleSpeedChangeTest.cpp` | Change speed/profile on eligible optical ports, verify links/LLDP/ECMP across warmboot | Q |
| `AgentEnsembleLinkEcmpTest.ecmpShrink` | `AgentEnsembleDependencyTest.cpp` | ECMP shrinks as members go down, restores | q |
| `AgentFabricLinkTest.linkActiveAndLoopStatus` | `AgentEnsembleFabricLinkTests.cpp` | Fabric port active state before/after drain; data-cell filter on fabric switches | q |
| `AgentEnsembleLacpTest.lacpFlap` | `AgentEnsembleLacpSanityTests.cpp` | Two aggregates converge, member flap, reconverge | q |
| `AgentEnsembleLinkDebounceTest.DebounceTimerWithinTolerance` | `AgentEnsembleLinkDebounceTests.cpp` | 500 ms link-down debounce within ±10% | Q |
| `AgentEnsembleLoopbackTest.systemLoopbackTest` / `lineLoopbackTest` | `AgentEnsembleLoopbackTest.cpp` | Transceiver system/line loopback makes ports hear themselves; recovery after clear | Q |
| `AgentEnsembleMacLearningTest.l2EntryFlap` | `AgentEnsembleMacLearningTests.cpp` | L2 learn / age / relearn | q |
| `AgentEnsemblePtpTests.verifyPtpTcDelayRequest` / `verifyPtpTcAfterLinkFlap` / `enablePtpPortDown` | `AgentEnsemblePtpTests.cpp` | PTP transparent-clock correction, including across flaps | q |
| `AgentEnsembleLinkTest.iPhyInfoTest` / `xPhyInfoTest` | `AgentEnsemblePhyInfoTest.cpp` | IPHY / XPHY snapshots present, fresh, consistent (xPhy is unsupported on non-XPHY platforms) | q / Q |
| `AgentEnsembleLinkTest.verifyIphyFecCounters` / `verifyIphyFecBerCounters` / `clearIphyInterfaceCounters` | `AgentEnsemblePhyInfoTest.cpp` | FEC error injection seen at peer; pre-FEC BER / histogram / FEC tail; counter clear | q |
| `AgentEnsembleQsfpFsdbTest.tcvr` / `phy` / `portState` / `portStateWithResetHold` | `AgentEnsembleQsfpFsdbTests.cpp` | qsfp_service publishes transceiver / phy / port-programming state to FSDB | Q |
| `AgentEnsembleOpenBmcUpgradeTest.openBmcHitlessUpgrade` | `AgentEnsembleOpenBmcUpgradeTests.cpp` | BMC upgrade is hitless | — |
| FSDB / MACsec suites | `facebook/` (internal only) | Not present in open-source checkouts | — |

## Fixture facts that shape failure reading

- Cabled ports come from the **agent config** (`expectedLLDPValues` /
  `expectedNeighborReachability`), not from live LLDP. A port the lab cabled but the
  config does not list is invisible to the test; a listed port whose cable is missing
  fails G1.
- A test that fails within ~5-15 minutes of start with a SetUp exception never ran
  its body; blame the gate, not the test name.
- When the first test of a run (`AgentEnsembleEmptyLinkTest.CheckInit`) fails, later
  tests of the same run usually fail identically; root-cause once.
- `verifyAcrossWarmBoots(setup, verify)`: cold-only failure → setup/gates; warm-only
  → state preservation across warmboot (or version incompatibility on roundtrips);
  both → the test body.
