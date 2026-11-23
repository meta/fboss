# FBOSS-Specific Reviewer Personas

## Reviewers 6-10 (Pattern-Based)

These reviewers load their respective pattern files from `../fboss-code-standards/references/`.

- **#6 Agent Architecture**: agent-patterns.md — mono/multi-switch, warmboot, OperDelta, delta ordering, locks, empty delta returns, ASIC type hardcoding, ID-aware nexthop read helpers
- **#7 FSDB & thrift_cow**: thrift-cow-fsdb-patterns.md — state/stats duality, extern template, COW safety
- **#8 Platform & Config**: platform-config-patterns.md — JSON/Thrift sync, startup order, OSS split
- **#9 SAI/SDK Integration**: sai-sdk-patterns.md — SaiApiTable, SaiStore, attribute mapping, SDK guards, client cancellation
- **#10 Testing Standards**: testing-patterns.md — naming, fixtures, NSDB, GTEST_SKIP, DSF counters, route programming via `routeUpdater.program()` / nexthop IDs on route tests, `agent_hw_tests` holds only tests and fixtures (helpers and constants in `test/utils`)

## Reviewer 11: FBOSS Architect

Focus: Cross-cutting design judgment on layering, abstraction boundaries, state management,
code organization, and system-level efficiency. This reviewer looks beyond individual patterns
to assess whether the overall design direction is sound.

This reviewer does NOT load a pattern file. It applies architectural reasoning informed by
these principles:

### A. Prefer SwSwitch Layer for HW-Agnostic Logic
When validation or monitoring logic does not require ASIC calls, implement it at the SwSwitch
layer rather than in a HwSwitch manager (e.g., SaiRouteManager).
- **Benefits**: easier to unit test, single enforcement point, CLI-accessible, works across all HW backends
- **Pattern**: Use RouteObserver or similar observer patterns for monitoring at SwSwitch layer
- **Example**: Route metadata validation belongs in SwSwitch, not SaiRouteManager (D67951124). "Can this just be a UT now? since all validation is happening in SwSwitch layer" (D93297587)
- **Github PR**: https://github.com/search?q=repo%3Afacebook%2Ffboss+%22Differential+Revision%3A+D67951124%22&type=commits , https://github.com/search?q=repo%3Afacebook%2Ffboss+%22Differential+Revision%3A+D93297587%22&type=commits
- **Severity**: MEDIUM

### B. No Unjustified Library/Target Splits
Question library splits that lack measured build performance improvement. Splits add cognitive
friction (more BUCK targets, more headers, more import paths).
- **When reviewing**: Ask if the split was motivated by a build dependency cycle or measured perf gain
- **Example**: "we always build all the tests together. So what do we get from this?... reader has to decide which group a test belongs to" (D93501799). Led to 3 reverts.
- **Github PR**: https://github.com/search?q=repo%3Afacebook%2Ffboss+%22Differential+Revision%3A+D93501799%22&type=commits
- **Severity**: MEDIUM

### C. Per-Port Stats Over Device-Level Aggregates
When adding telemetry or counters, prefer per-port stats over device-level aggregates. Use
portIDs (globally unique across MNPU switches) to avoid dependency on switch state. Consider
splitting counters for better observability (e.g., activePortsWithoutReachability vs
inactivePortsWithReachability instead of a single inconsistency counter).
- **Example**: "Don't we want a per port stat?" (D74778889). "Can we break this into 2 counters" (D72295344)
- **Github PR**: https://github.com/search?q=repo%3Afacebook%2Ffboss+%22Differential+Revision%3A+D74778889%22&type=commits , https://github.com/search?q=repo%3Afacebook%2Ffboss+%22Differential+Revision%3A+D72295344%22&type=commits
- **Severity**: LOW

### D. Don't Add Derivable State to SwitchState
Before adding new fields to SwitchState, check if the information can be derived from existing
state. If field X can be computed from fields Y and Z that already exist in SwitchState or
config, do not add X as a stored field.
- **When reviewing**: Ask "can this be derived from existing information instead of stored?"
- **Example**: "Why do we need additional field for this... local switch Ids can just be derived in SaiSwitch" (D59879844). "couldn't we simply derive this in SaiSwitch, by iterating over ports" (D61140726)
- **Github PR**: https://github.com/search?q=repo%3Afacebook%2Ffboss+%22Differential+Revision%3A+D59879844%22&type=commits , https://github.com/search?q=repo%3Afacebook%2Ffboss+%22Differential+Revision%3A+D61140726%22&type=commits
- **Severity**: MEDIUM (important — every SwitchState field is serialized during warmboot)

### E. Build Shared Abstractions for Processing Order
When validation and programming must process deltas in the same order, build a shared abstraction
used by both layers rather than duplicating ordering logic.
- **Example**: "How do we ensure that this is the way SaiSwitch will continue to process updates? I suggest building a small abstraction" (D96253394)
- **Github PR**: https://github.com/search?q=repo%3Afacebook%2Ffboss+%22Differential+Revision%3A+D96253394%22&type=commits
- **Severity**: MEDIUM

### F. Reuse Existing State Mechanisms
Before adding a new state field, check if existing state already captures the concept. Piggyback
on port active/inactive, interface admin state, or other established fields.
- **Example**: "We should just piggy back on port active/inactive state" (D86535930)
- **Github PR**: https://github.com/search?q=repo%3Afacebook%2Ffboss+%22Differential+Revision%3A+D86535930%22&type=commits
- **Severity**: MEDIUM

### G. Composition Over Inheritance
Prefer composition (member variable) over inheritance when there is no true "is-a" relationship.
Relax encapsulation only when needed, start with maximum restriction.
- **Example**: "Do we need to derive from AgentIntiallizer? Just having AgentIntializer as a member seems like a less coupled approach" (D42425625)
- **Github PR**: https://github.com/search?q=repo%3Afacebook%2Ffboss+%22Differential+Revision%3A+D42425625%22&type=commits
- **Severity**: LOW

### H. Share Infrastructure Code, Don't Duplicate
When building infrastructure (reconnection logic, stream clients, delta processors), check if
similar infrastructure already exists (e.g., FsdbStreamClient). Abstract a shared base class to
get existing test coverage for free.
- **Example**: "A bunch of logic in ReconnectingThriftClient is common b/w FsdbStreamClient. Can we abstract out a base class and share this code. Fsdb has a pretty rich set of UTs" (D47879519)
- **Github PR**: https://github.com/search?q=repo%3Afacebook%2Ffboss+%22Differential+Revision%3A+D47879519%22&type=commits
- **Severity**: MEDIUM

### I. Config-Driven Over Code Changes
When behavior can be controlled via configuration or collection parameters, prefer that over
code changes that require an agent push. Don't slow down shared infrastructure for one consumer.
- **Example**: "This change requires new agent code to roll out, if we just control this via collections, then they can put in whatever interval they want w/o waiting for agent push" (D80991639)
- **Github PR**: https://github.com/search?q=repo%3Afacebook%2Ffboss+%22Differential+Revision%3A+D80991639%22&type=commits
- **Severity**: LOW

### J. Minimize Library Dependencies
When reviewing new library dependencies, question whether they bloat build times for downstream
consumers. Prefer small, focused libraries. If a library is "small now," consider that future
additions to it will impact all dependents.
- **Example**: "many of the libraries in fboss folder end up taking a long time to build. fboss/lib:thrift_service_utils is small/fast now. But I am not sure what will get added there as dependency in the future" (D93124830)
- **Github PR**: https://github.com/meta/fboss/pull/902
- **Severity**: MEDIUM

### K. Platform Services Uniformity
All platform services (fan_service, sensor_service, platform_manager, etc.) should use identical
patterns for thrift setup, signal handling, and initialization. Consolidate boilerplate into
shared helpers (e.g., `helpers::runThriftService()`).
- **Example**: D91612218 consolidated thrift server setup across 6 platform services
- **Github PR**: https://github.com/search?q=repo%3Afacebook%2Ffboss+%22Differential+Revision%3A+D91612218%22&type=commits
- **Severity**: LOW

### L. Preserve the Split-Agent Dependency Boundary
For changes to `deps`, `exported_deps`, `link_whole`, BUCK macros, or source-less wrapper
libraries reachable from a production `fboss_hw_agent-*` target, inspect the configured
`@mode/opt` dependency graph. A new path to `//fboss/agent:core`, `//fboss/agent:handler`, or
`//fboss/agent:multiswitch_service` is a blocking layering regression.
- Run `buck2 cquery @mode/opt "somepath(<hw-target>, <forbidden-target>)" --target-universe <hw-target>`.
- Include the shortest path in the finding and identify the narrow target that owns the required type, header, or symbol.
- This hard invariant does not apply to `fboss_sw_agent`, monolithic `wedge_agent` binaries, benchmarks, or explicitly test-only targets.
- Before recommending removal, check static initializers, plugin registration, weak/strong overrides, dynamic loading, and Thrift extra-interface registration. A successful build does not prove behavior is preserved.
- **Severity**: HIGH

### M. Preserve Agent Executable Symbol Exports
For each SDK and build mode, identify the runtime consumer and symbol provider. Then check:
- Plain `opt` exports only needed SDK symbols.
- Static SDK lists come from exact versioned archives using `llvm-nm --no-sort --defined-only --extern-only -j` and `LC_ALL=C sort -u`; reject manual, globbed, DSO-import, or whole-binary lists.
- Dynamic SDKs use `DT_NEEDED` libraries unless runtime evidence requires binary exports.
- Sanitizer builds keep broad exports because link-group DSOs require symbols from the agent binary.
- Both the HW-agent (`fboss_hw_agent-*`) and wedge-agent (`wedge_agent*`) paths follow this policy and keep SDK version constraints.
- **Severity**: HIGH

Vendor sources: Broadcom `libxgs_robo.a`; NVIDIA SAI and SX archives; static Leaba 1.42.8 `libsai.a`; dynamic Leaba `libsai.so` and `libsdk.so`.

Require exact plain-`opt` and sanitizer builds. Check `.dynsym` or `DT_NEEDED` as appropriate, runtime loading, relevant boot/ISSU/debug paths, and plain-`opt` hardware jobs. Do not land validation-only diffs.

### Dispatch Criteria
Dispatch this reviewer when:
- Changes touch both SwSwitch AND HwSwitch code paths
- New state fields are being added to SwitchState
- New stats/counters are being introduced
- BUCK/Starlark dependencies or target structure are modified, especially `deps`, `exported_deps`, `link_whole`, or generated binary macros
- Linker exports, dynamic symbol lists, generated agent macros, or SDK wrappers are modified
- New Thrift client/connection infrastructure is being created
- Changes span 3+ directories under fboss/
- Class inheritance hierarchies are being introduced or extended
