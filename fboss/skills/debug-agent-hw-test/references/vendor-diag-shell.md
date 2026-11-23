# Vendor Diagnostic Shell (Meta External)

## When to Use

When you need to inspect hardware state directly — counters, route/neighbor entries, port status — to verify what the ASIC actually programmed vs what FBOSS intended.

## Keeping the Agent Alive for Inspection

The diagnostic shell attaches to a **running** agent. A normal AgentHwTest tears the agent
down as soon as the test body finishes, so the ASIC is no longer programmed by the time you
want to inspect it. Hold the agent (and its ASIC programming) open with `--run_forever`:

```bash
sai_agent_hw_test --config <cfg> --gtest_filter=<TestSuite.TestName> --run_forever
```

- `--run_forever` — after the test body completes (PASS **or** FAIL is reported first),
  `AgentHwTest::TearDown()` enters an infinite sleep loop **before** tearing down the
  ensemble, so the agent stays up with the ASIC in its end-of-test state indefinitely.
- `--run_forever_on_failure` — same, but only holds when the test actually failed.

Both are compiled-in gflags, so an existing binary already supports them — no rebuild needed.
Inspect the still-programmed ASIC, then stop the held process when done.

## Accessing the Diagnostic Shell

Use the vendor's one-shot diag-command tool to send a single command to the running agent's
diagnostic shell and capture its output, e.g. confirm the attached ASIC:

```
<vendor-diag-tool> --command 'version'
```

The exact tool, port, auth/reason, and write-mode flags are environment-specific.

> **Customization point**: If your environment provides a specific diag-shell tool (e.g. a
> vendor CLI reached over a lab SSH/MCP path), create `facebook/vendor-diag-shell.md` in this
> skill directory with the concrete tool, flags, and access method. The skill's Reference
> Routing will prefer it over this file.

## Running Vendor Techsupport Scripts

Vendor SDKs ship **techsupport** bundles — pre-canned collections of diag commands grouped by
feature — which are the fastest way to capture the ASIC state a vendor needs to root-cause an
issue. On Broadcom these are `.soc` scripts (e.g. `basic`, `cosq`, `qos`, `port`, `field`),
run from the diag shell with:

```
<vendor-diag-tool> --command 'rcload <path-to>/<feature>.soc'
<vendor-diag-tool> --command 'fp show'    # field/IFP (ACL) group dump
```

Capture each feature's output to its own file and bundle them for handoff. These dumps are
strong evidence for a `FAIL_VENDOR` escalation — attach them to the package built per
the [vendor-escalation skill](../../vendor-escalation/SKILL.md).

## Counter Commands

Check hardware counters to understand packet flow and drops:

<!-- TODO: User to provide specific counter commands -->

### Port Counters

<!-- TODO: Commands to check per-port TX/RX counters, error counters -->

### Drop Counters

<!-- TODO: Commands to check drop reasons and counts -->

## Route/Neighbor Entry Verification

Verify that routes and neighbor entries are correctly programmed in hardware:

<!-- TODO: User to provide commands to dump route and neighbor tables -->

### Route Table

<!-- TODO: Commands to check L3 route entries -->

### Neighbor Table

<!-- TODO: Commands to check L2/L3 neighbor entries -->

## Port Status

Check physical port state, link status, and configuration:

<!-- TODO: User to provide port status commands -->

## ACL/QoS Verification

<!-- TODO: User to provide commands to check ACL and QoS programming -->

## Common Diagnostic Patterns

<!-- TODO: User to provide common diagnostic patterns -->

| Symptom | Diag Command | What to Look For |
|---------|-------------|-----------------|
| Packets not forwarded | Check route table | Missing or wrong route entry |
| Packets dropped | Check drop counters | Specific drop reason |
| Wrong port behavior | Check port config | Mismatched speed/FEC settings |

## Expected Output

- Hardware state that can be compared against FBOSS agent's intended state
- Specific counter values or table entries that explain the test failure

## Next Steps

- If hardware state doesn't match FBOSS intent: investigate agent programming logic
- If hardware state is correct but test still fails: re-examine test expectations
- If vendor SDK bug suspected: categorize as `FAIL_VENDOR` with diagnostic evidence
