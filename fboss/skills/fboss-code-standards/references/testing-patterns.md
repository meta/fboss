# Testing Patterns

Patterns for FBOSS test code, including agent HW tests, multinode tests, and test infrastructure.

### 1. Test Naming Conventions
**Check**: Agent hardware tests follow the `AgentHw<Feature>Test` naming pattern (e.g., `AgentHwAclTest`, `AgentHwRouteTest`). Multi-switch variants use `AgentHwMultiSwitch<Feature>Test`.
**Why**: Consistent naming enables test discovery, filtering, and CI configuration. Non-standard names may be excluded from test suites.
**Confidence**: HIGH (always follow the naming convention)

### 2. SAI/Non-SAI Test Variants
**Check**: When adding agent HW tests, include both SAI and non-SAI (if applicable) test variants unless the feature is SAI-only.
**Why**: FBOSS supports multiple hardware abstraction layers. Tests that only cover SAI miss regressions on other platforms.
**Confidence**: MEDIUM (depends on feature scope)

### 3. Test Fixture Hierarchy
**Check**: Use existing test fixtures (`AgentHwTest`, `AgentTest`, `HwTest`) rather than creating new base classes. Extend fixtures via composition or inheritance from the established hierarchy.
**Why**: Custom fixtures often miss shared setup/teardown logic, causing flaky tests or incorrect test environments.
**Confidence**: MEDIUM (new fixtures are sometimes warranted)

### 4. Test Plan Format
**Check**: Diff test plans should reference actual test execution results. Include paste numbers or links to test runs.
**Why**: "Tested locally" without evidence doesn't demonstrate actual test coverage.
**Confidence**: MEDIUM (enforcement varies by reviewer)

### 5. NSDB Impact Testing
**Check**: Changes to core FSDB, thrift_cow, or state management code must include NSDB (Network State Database) test runs in the test plan.
**Why**: NSDB is a critical consumer of FSDB. Core changes that break NSDB cause production network state visibility outages.
**Confidence**: HIGH (always required for core FSDB changes)

### 6. No GTEST_SKIP in Agent HW Tests
**Check**: Agent hardware tests must NOT use `GTEST_SKIP()`. Use ProductionFeatures filtering via netcastle to exclude unsupported tests per platform.
**Why**: GTEST_SKIP wastes test infra resources (ASIC init runs before the skip). The pattern also gets copied to new tests. Use production feature filtering so netcastle selects tests per platform.
**Confidence**: HIGH (always wrong in agent HW tests)

### 7. DSF Test Counters
**Check**: DSF (Distributed Switching Fabric) tests must check reassembly errors, not `outDiscards`/`inDiscards`. Discard counters are not supported on fabric ports.
**Why**: Unsupported counters stay zero, producing false passes. Check reassembly errors and work backwards to identify affected switches/ports.
**Confidence**: HIGH (discard counters always wrong on fabric ports)

### 8. Targeted Unit Tests for New Modules
**Check**: New classes or modules must have dedicated unit tests, not just integration or HW tests. When reviewing, ask: "Do we have targeted tests for [NewClass]?"
**Why**: Integration tests may cover the new code incidentally but don't verify it in isolation. When the integration test fails, it's unclear whether the new module or its consumers are at fault.
**Confidence**: HIGH (always require targeted tests for new modules)

### 9. Deep Comparison Over Size Checks
**Check**: Verify state correctness with deep comparison (compare full objects or field-by-field), not just collection size checks. `EXPECT_EQ(map.size(), expected)` does not prove content equality.
**Why**: Size equality can mask content corruption — an insert paired with an accidental delete preserves size but changes data. Deep comparison catches these bugs.
**Confidence**: HIGH (always compare content, not just size)

### 10. Verify Negative Cases Explicitly
**Check**: Tests must explicitly verify negative/absence conditions, not just assert no exception is thrown. Check that values are absent, counters are zero, or states are not set. Ask: "What does this check? Just no throw?"
**Why**: A test that only checks for no-throw provides no signal — it passes even if the code does nothing. Explicit negative assertions (e.g., `EXPECT_FALSE(map.count(key))`) prove the invariant holds.
**Confidence**: HIGH (always verify both positive and negative conditions)

### 11. Test Overflow and Error Paths
**Check**: Tests must cover resource exhaustion and overflow conditions, not just the happy path. Verify the system handles table-full, buffer-full, and limit-exceeded gracefully instead of crashing.
**Why**: Happy-path-only tests miss the most dangerous production failures. Overflow handling code is rarely exercised and often has latent bugs.
**Confidence**: HIGH (always test error paths for resource-bounded operations)

### 12. Optimize Test Execution Time
**Check**: Minimize test execution time by using smallest valid packet sizes, tightest loops, and avoiding unnecessary sleeps or waits. If a test takes 15 seconds due to large packets, parameterize the size and use 64-byte packets when full-size is not required.
**Why**: Long-running tests waste CI hardware resources and slow developer iteration. Each second saved is multiplied across thousands of test runs.
**Confidence**: MEDIUM (balance speed against test fidelity)

### 13. Test Transient Invalid States
**Check**: Test that validation catches invalid intermediate states during delta processing, not just final state correctness. E.g., test that N1(MAC_A→MAC_B) and N2(MAC_A→MAC_B) doesn't create a transient duplicate.
**Why**: State machine bugs only manifest during transitions. Final-state-only tests miss ordering-dependent failures.
**Source**: D96253394 reviewer feedback
**Confidence**: HIGH

### 14. Consolidate Test Helpers into Base Classes
**Check**: When similar test helper methods exist across multiple test files, consolidate into the shared base class (e.g., `AgentArsBase`) rather than duplicating.
**Why**: Duplicated helpers diverge over time, causing inconsistent test behavior and maintenance burden.
**Source**: D91603952, D91606153 reviewer feedback
**Confidence**: HIGH

### 15. Prefer Unit Tests Over Integration Tests for Validation Logic
**Check**: Pure validation logic (e.g., neighbor MAC uniqueness checking) should have dedicated unit tests, not rely solely on agent_hw_test integration tests.
**Why**: Unit tests are faster, more targeted, and easier to debug. Integration tests may incidentally cover validation but don't isolate it.
**Source**: D93297587, D96253394 reviewer feedback
**Confidence**: MEDIUM

### 16. Use initialConfig() Factory Pattern for Test Setup
**Check**: Tests should define configuration via `initialConfig()` override rather than ad-hoc setup lambdas or inline config construction.
**Why**: Decouples config setup from test startup. Config applied only on cold boot, reused on warmboot. Standard pattern across agent HW tests.
**Source**: D42425625 reviewer feedback
**Confidence**: MEDIUM

### 17. Do Not Modify the Setup of Existing Tests
**Check**: Never change the `setup` lambda or `initialConfig` of an existing agent HW test. If new functionality needs different setup, create a new test instead.
**Why**: Existing tests run in trunk-to-prod (and prod-to-trunk) warmboot roundtrip CI. When you modify a test's setup, the trunk version programs different HW state than the prod version. On warmboot from trunk back to prod, the prod binary encounters unexpected programmed state, causing test failures that block releases. The setup defines the HW contract between cold boot and warmboot — changing it breaks that contract across commits.
**Source**: D102269880 — modified setup of an existing RouteTest to program v4 routes; broke warmboot roundtrip because prod binary didn't expect v4 route state programmed by trunk.
**Confidence**: HIGH (always create a new test instead of modifying existing setup)

### 18. Program Routes in Tests Through routeUpdater.program()
**Check**: Tests that construct routes must program them through `routeUpdater.program()` (via `SwSwitchRouteUpdateWrapper`), which uses the ID-aware route update path so every route gets a refcounted nexthop ID. Do NOT manually construct routes and add them to the FIB — that bypasses the `NextHopIDManager` and fails the consistency checker (on by default in all unit tests and agent HW tests). Keep the `NextHopIDManager` on (`FLAGS_enable_nexthop_id_manager=true`, the default); do NOT disable it in route tests.
- **Tests driving the real path** (`createTestHandle` → `ThriftHandler::addUnicastRoutes`/`syncFib`, or `SwSwitchRouteUpdateWrapper::program()`): IDs are assigned automatically. Assert each resolved fwd entry with action `NEXTHOPS` carries an ID — non-`nullopt` `getResolvedNextHopSetID()` and `getNormalizedResolvedNextHopSetID()`.
- **Tests hand-building a `SwitchState`/FIB directly** (bypassing the RIB): IDs are NOT assigned. Either route the adds through `routeUpdater.program()` instead, or — if the test must build state directly — call `assignNextHopIdsToAllRoutes(idManager, state)` (or per-entry `allocateRouteNextHopIds`) from `fboss/agent/test/utils/NextHopIdTestUtils.h` so every route gets an ID.
- **Reading nexthops in tests**: tests operate on a published `SwitchState`, so use the FibHelper state-based read API (`getNextHops(state, entry)`, `getNormalizedNextHops(state, entry)`, `getClientNextHops(state, entry)`); do NOT call `getNextHopSet()`, which reads inline nexthops directly off the route object (see agent-patterns.md rule 18 for the full FIB-vs-RIB helper split).
**Why**: ID-based nexthops are shipping to prod. Hand-built, ID-less routes give false coverage and crash ID consumers under `FLAGS_resolve_nexthops_from_id` (e.g. `FibHelpers.cpp` `getFwdSwitchingMode` CHECK).
**Confidence**: HIGH (every route in a test must be programmed through the ID-aware update path)

### 19. Macro-Generated and Alias Fixture Names Must Be Selectable On Diff
**Check**: In `agent_hw_tests`, a macro that builds a fixture name with token pasting (`##`) must start it with a fixture class name defined in the same file, e.g. `struct AgentPortProfileTest_##PROFILE : AgentPortProfileTest<...>`, not `struct AgentTest_##PROFILE`. A `using` alias that names a test fixture must end in `Test` (`AgentSflowMirrorTruncateV4Test`, not `AgentSflowMirrorTruncateTestV4`).
**Why**: On-diff test selection (UTD `test_only_regex.td.bzl`) reads the changed file as text and only collects `class`/`struct X :` names, `using XTest =` aliases and `TEST_F`/`TEST_P`/`TYPED_TEST` first arguments; netcastle then runs a test only if one of those names is a substring of the test name. A `##`-built name exists only after preprocessing, so its tests are silently never selected on diff (previously all 33 `AgentTest_PROFILE_*` tests). An alias is only collected from `using XTest =`, so one not ending in `Test` and used through a wrapper macro (e.g. `SFLOW_SAMPLING_TEST`) is missed the same way.
**Confidence**: HIGH (always wrong for macro-generated or alias fixtures in agent_hw_tests)

### 20. Keep Helpers and Constants Out of agent_hw_tests
**Check**: A new or moved file in `fboss/agent/test/agent_hw_tests/` must be a test file (`TEST_F`/`TEST_P`/`TYPED_TEST`) or a test fixture: a class deriving from `AgentHwTest` (or a subclass), its `.cpp`, or a class implementing an interface a fixture in the folder declares (e.g. `XgsMirrorOnDropImpl` for `MirrorOnDropImpl`). Free helper functions, standalone helper classes and constants go in `fboss/agent/test/utils/` as `FooTestUtils.{h,cpp}` or `FooConstants.h`, with a `cpp_library` in `test/utils/BUCK`.
**Why**: `test/utils` is where shared test helpers and constants live, so they are found and reused there (agent HW, link and benchmark tests) instead of being duplicated in `agent_hw_tests`.
**Confidence**: HIGH for a new file with only free functions or constants; MEDIUM for a helper class that only one fixture uses
