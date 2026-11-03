# QSFP HW Test Catalog

Test suite to source file map for `fboss/qsfp_service/test/hw_test/`. The
binary is `qsfp_hw_test-<impl>-<version>`; every gtest runs as
`cold_boot.<Suite>.<Test>` and `warm_boot.<Suite>.<Test>`.

## Test files

| Suite | File | What it verifies |
|-------|------|------------------|
| `HwTest` | `HwTest.cpp` | Transceiver name/interface mapping; CMIS CDB firmware-timeout advertisement |
| `EmptyHwTest` | `EmptyHwTest.cpp` | Sanity baseline (`CheckInit` — if this fails, the setup itself is broken) |
| `HwStateMachineTest` | `HwStateMachineTest.cpp` | Detection, IPHY/XPHY programming sequence, ACTIVE/INACTIVE on port up/down, removal, remediation, agent-config-change |
| `HwTransceiverConfigTest` | `HwTransceiverConfigTest.cpp` | `transceiverConfigOverrides` (CMIS RxEq, SFF pre-emphasis/TxEq/RxAmp) actually programmed |
| `HwTransceiverConfigValidationTest` | `HwTransceiverConfigValidationTest.cpp` | Every present cabled module has a validated vendor/PN/firmware config (`EXPECT_TRUE(invalid.empty())`) |
| `HwTransceiverResetTest` | `HwTransceiverResetTest.cpp` | Hard reset, hold/release, `RESET_THEN_CLEAR` state transitions |
| `HwTest_PROFILE_*` | `HwPortProfileTest.cpp` | Per-profile XPHY programming (idempotent double-program) plus optics and lane-map verification; suite name embeds the profile (e.g. `HwTest_PROFILE_400G_2_PAM4_RS544X2N_OPTICAL`) |
| `HwPortPrbsTestAll` | `HwPortPrbsTest.cpp` | `{SYSTEM,LINE} x {NRZ,PAM4} x {enable,disable}` PRBS program/readback plus lane-count stats |
| `HwMacsecTest` | `HwMacsecTest.cpp` | SAI-only MACSEC install/rotate/idempotent/update/cleanup/ACL |
| `HwPimTest` | `HwPimTest.cpp` | PIM presence and error-free PIM state |
| `HwStatsCollectionTest` | `HwStatsCollectionTest.cpp` | Stats publish, XPHY stats collection, port/PRBS/transceiver/PHY IO stats |
| `HwXphyPortStatsCollectionTest` | `HwStatsCollectionTest.cpp` | XPHY port stats collection completeness (`checkXphyStatsCollectionDone`); shares the file with `HwStatsCollectionTest` — grep the file, not the suite name |
| `HwXphyFirmwareTest` | `HwXphyFirmwareTest.cpp` | Per-platform expected XPHY firmware version |
| `HwXphyPortReprogramTest` | `HwXphyPortReprogramTest.cpp` | Remove, verify cleared, reprogram, verify |
| `HwI2CStressTest` (`i2cStressRead`, `cmisPageChange`, `publishStats`) | `HwI2CStressTest.cpp` | 200 iterations of I2C reads/writes; failures point at bus, cabling, FPGA I2C, or module defects |
| `HwI2cSelectTest` (`i2cUniqueSerialNumbers`) | `HwI2cSelectTest.cpp` | Duplicate serials allowed only for back-to-back copper DAC/AEC pairs |
| `OpticsFwUpgradeTest` | `OpticsFwUpgradeTest.cpp` | Same-version no-op, warmboot no-op, triggered upgrade with start/end-time verification |
| `HwTransceiverThermalDataTest` | `facebook/HwTransceiverThermalDataTest.cpp` | Internal-only; not present in open-source checkouts |

## Fixture behavior that shapes failure reading

- `HwTest::SetUp()` forces remediation intervals to 0, pauses remediation
  for 10 minutes, then blocks in `waitTillCabledTcvrProgrammed()` (up to 30
  retries, 5s apart) before the test body runs. A test that fails in the
  first seconds usually failed this gate, not its own assertions.
- `verifyAcrossWarmBoots(setup, verify)` runs `setup()` only on cold boot,
  `verify()` on both phases. Cold-only failures implicate `setup()` or the
  gate; warm-only failures implicate warmboot state preservation.
- `TearDown()` dumps per-transceiver telemetry (best-effort, never fails the
  test) and then checks watchdog heartbeat count and the cold-boot flag file.
  A `TearDown` failure with a passing body means infra/hang, not bad optics.
