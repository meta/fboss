## Integration testing on a device

Use a test switch provisioned with an FBOSS image built from your branch,
with the agent stack (`fboss_sw_agent`, `fboss_hw_agent@0`, `fsdb`,
`qsfp_service`) up and running. How you obtain and provision one is
environment-specific.

Sanity-check before running tests:

1. All FBOSS services are `active` under systemd.
2. The agent config exists and the agent has finished initializing.
3. `fboss2-dev show interface` succeeds without `Connection refused`.

Build + copy + run. **Two gotchas worth calling out up front**:

- The `config` commands live in the **`fboss2-dev`** binary, not `fboss2`.
  Build and deploy `fboss2-dev` — if you copy `fboss2` instead, the CLI
  rejects `config ...` with "The following arguments were not expected: ...".
- `scp` copies binaries over as mode 555. A second `scp` to the same path
  fails with `Permission denied` even for the owner. Use a fresh versioned
  filename rather than fighting the mode.

```bash
scp <path-to>/fboss2-dev <switch>:/tmp/fboss2-dev-<suffix>
scp <path-to>/fboss2_integration_test <switch>:/tmp/fboss2_it_<suffix>
ssh <switch> "/tmp/fboss2_it_<suffix> --gtest_filter='Config<Area>Test.*'"
```

Also capture an interactive session for the review's "Sample usage" — for
each attr run the CLI, then `config session diff`, then
`config session commit`, then show the relevant `show` output (or agent
running config) reflecting the new value.

## When the CLI surfaces an agent-side crash

Integration tests exercise code paths on the agent that may have latent
bugs. The CLI correctly producing a config delta that then crashes the
agent is a common outcome — the fboss_sw_agent and fboss_hw_agent@0
processes both auto-restart under systemd, so the device usually recovers
even though the test reports a failure.

**Find the real crash site**. The systemd core dump is typically
truncated (`coredumpctl info <pid>` shows "Storage: ... (truncated)")
and gdb without debug symbols is useless on it. Three better sources,
in order:

1. **`/var/facebook/logs/fboss/wedge_agent.log`** — the agent writes its
   full glog + the signal-handler's demangled stack trace here. Grep for
   `F<MMDD>|CHECK failed|Failed to|Terminated due to|SaiApiError`
   around the crash timestamp. This is the single most useful source —
   it includes the preceding error line (e.g.
   `[hash] Failed to remove sai object : HashSaiId(...): OBJECT IN USE`)
   which almost always names the faulty SAI call or CHECK.
2. **`sudo journalctl -u fboss_hw_agent@0 --since '<crash-time>'`** —
   useful for the process lifecycle (which process died first, when
   systemd restarted it) but the stack traces here are mostly unmangled
   thread frames with heavy idle/worker noise. Filter aggressively.
3. **gdb on the core** — `sudo coredumpctl debug <pid>
   --debugger-arguments='-batch -ex "thread 1" -ex "bt"'`. Only useful
   when the core isn't truncated.

In a hw/sw-agent split deployment, the **hw_agent crashes first**; the
sw_agent follows seconds later when it notices the hw_agent disconnected.
The sw_agent's stack trace shows an unrelated cleanup-path abort in
`HwSwitchConnectionStatusTable::disconnected` — ignore it and focus on
the hw_agent's crash.

## Fixing an agent-side bug in the same change

When the integration tests reveal an agent bug, decide scope:

- **Small mechanical fix (≲50 lines, clear precedent from a nearby
  working code path)**: fold it into the same commit. Example: a LAG
  hash delta crash was a missing clear-before-set in
  `SaiSwitchManager::addOrUpdateLagLoadBalancer` — the ECMP path already
  had the pattern and there was even a `TODO` flagging the gap. Ported
  in ~20 lines, tested on the same device.
- **Larger or uncertain fix**: keep the CLI change scoped, file a
  follow-up issue, disable or `GTEST_SKIP` the integration tests that
  reproduce the bug so CI doesn't core-dump the agent, and link the
  issue from the review.

If you include an agent fix, rebuild the hw_agent binary for your SAI
implementation, then redeploy:

```bash
scp <path-to>/fboss_hw_agent-sai_impl \
    <switch>:/tmp/fboss_hw_agent-sai_impl.<suffix>
ssh <switch> "sudo systemctl stop fboss_hw_agent@0 fboss_sw_agent && \
    sudo cp /tmp/fboss_hw_agent-sai_impl.<suffix> \
            /opt/fboss/bin/fboss_hw_agent-sai_impl && \
    sudo chmod +x /opt/fboss/bin/fboss_hw_agent-sai_impl && \
    sudo systemctl start fboss_hw_agent@0 fboss_sw_agent"
```

The binary is large so the scp can take a while on a management
network. Wait for the agent to come back up with a valid config before
re-running tests — the agent reports "switch is still initializing"
for the first few seconds:

```bash
until ssh <switch> "/tmp/fboss2-dev-<suffix> show interface 2>/dev/null | grep -q 'eth1/'"; do sleep 5; done
```
