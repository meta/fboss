# SRv6 coverage pack (DRAFT — not yet validated)

Status: drafted from a report-mode run over `AgentSrv6ResourceUsageTests.cpp` + `AgentSrv6MidpointTest.cpp`; NOT validated, NOT --apply-eligible. Needs human review + an eval corpus + a spot-check before --apply (see `../contributing.md`).

The universal gates reach the right keep/fold verdict on SRv6 only with careful judgment — the distinctions below are SRv6-specific and should be made mechanical by this pack.

## Extra fingerprint fields

Capture under `coveragePack["srv6"]`, in addition to the universal fingerprint:

- **`sidType`** — set of MySid entry types programmed: `DECAP_AND_LOOKUP`, `ADJACENCY_uA`, `END`, `END_uN`, `BINDING_SID`, `ENCAP_SOURCE`. From the `MySidType` / `cfg::*MySidConfig` the test builds.
- **`sidFormat`** — `uSID` (micro-SID: locator block + function, advanced by a bit-shift, e.g. `fdad:ffff:1:2::` → `fdad:ffff:2::`) vs `fullSID` (128-bit segment popped from a segment list).
- **`usidOp`** — for midpoint/transit: `shift` (pop active uSID, advance to next) vs `terminal` (last uSID, no next → drop). Distinguishes a forward test from a last-SID drop.
- **`srv6Role`** — `encap` / `decap` / `midpoint` / `binding`. The pipeline block under test; orthogonal to traffic direction and often a distinct ProductionFeature with distinct ASIC support.
- **`mySidDropReason`** — for negative tests: `unresolved_binding`, `no_match`, `terminal_no_next_usid`,
  `hoplimit_exceeded`, each paired with the discard counter asserted. Not all bump `inSrv6MySidDiscards`:
  `verifyUnconfiguredSidDrop` asserts `inDstNullDiscards` while `inSrv6MySidDiscards` stays flat (Gate 6).
- **`outerHeaderTransform`** — outer-header mutations verified: `dstRewrite`, `hopLimitDecrement`, `tcPreserve`, `ecnPreserve`, `flowLabel`.
- **`innerInvariance`** — whether the inner packet is asserted unchanged, plus inner address family/families (`v4`/`v6`). Midpoint must not touch the inner packet.
- **`segmentListDepth`** — number of SIDs/segments exercised (1 vs N). Depth-1 ≠ depth-N transit.
- **`resourceCounter`** — SRv6 resource-pool stats verified, e.g. `my_sid_entries_free`. Marks resource-accounting tests as a verification axis distinct from dataplane checks.
- **`mySidBindingPath`** — `RIB_direct` (`MySidEntry` via `rib->update`) vs `config_mySidConfig` (`cfg.mySidConfig` + observer resolve). Different programming/observer code paths.
- **`tunnelType`** — `SRV6_ENCAP` tunnel presence + `ttlMode` (UNIFORM/PIPE) for encap-side coverage.

## Extra gate predicates

A candidate SRv6 test is subsumed only if, in addition to the 6 universal gates passing:

- **(a) SID-type containment.** `superset.sidType ⊇ subset.sidType`. A midpoint-uA test never covers a DECAP-and-lookup entry.
- **(b) SID-format parity.** `sidFormat` must match — a uSID-shift test does not subsume a full-SID segment-pop test.
- **(c) uSID-op parity.** If either test sets `usidOp`, they must match: `shift` ≠ `terminal`.
- **(d) Drop-reason parity.** Two negative SRv6 tests are equivalent only if `mySidDropReason` matches, even when they bump the same discard counter (this is the universal Gate 6 root-cause rule, specialized for SRv6).
- **(e) Outer-transform containment + inner invariance.** `superset.outerHeaderTransform ⊇ subset.outerHeaderTransform`; if the subset asserts `innerInvariance`, the superset must too.
- **(f) Role parity.** `srv6Role` must match — encap/decap/midpoint/binding are distinct pipeline blocks and distinct ProductionFeatures (e.g. tomahawk5 has MIDPOINT but not DECAP/ENCAP).
- **(g) Resource-axis orthogonality.** A test with a non-empty `resourceCounter` verifies a free-pool axis; a dataplane test never subsumes it (the universal Gate 4 HW-state/dataplane split, specialized for SRv6 resource stats).

## Worked notes (from the report-mode run)

- `verifyMySidResourceUsage`: `sidType={DECAP_AND_LOOKUP, ADJACENCY_uA}`, `resourceCounter={my_sid_entries_free}`, `mySidBindingPath=RIB_direct` — predicates (a), (f), (g) each independently block folding it into any Midpoint test (also Gate 1 last-owner of `SRV6_DECAP`).
- `dropPacketUASidIsLastSid` vs `sendPacketForUASidUnresolvedDropped`: identical under all 6 universal gates, but `mySidDropReason=terminal_no_next_usid` vs `unresolved_binding` and `usidOp=terminal` vs resolved — predicates (d)+(c) keep both mechanically (previously this needed the skeptic).
- `sendPacketForUASid`: sole owner of the forward-correctness bundle `usidOp=shift`, `outerHeaderTransform={dstRewrite,hopLimitDecrement,tcPreserve,ecnPreserve}`, `innerInvariance={v4,v6}` — predicate (e) keeps it.

## Fixture mode (SRv6)

For SRv6 the load-bearing mode axes are SAI vs native and the nexthop-id overrides (`FLAGS_enable_nexthop_id_manager` / `FLAGS_resolve_nexthops_from_id`) that both files pin via `setCmdLineFlagOverrides()` — these change the nexthop programming path. Record as `pinned:<tokens>` per universal Gate 5.
