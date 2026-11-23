# Enable Logging (Meta External)

## When to Use

When a test **fails** and the initial log analysis ([analyze-logs.md](analyze-logs.md)) does not yield a root cause. These options add verbose logging but come with caveats — use sparingly.

**Each option requires re-running the test.** Options can be combined.

## Logging Options

### (A) SDK Verbose Logging — `--enable_sai_log DEBUG`

Enables verbose logging from the vendor SDK. Produces detailed SAI API traces.

Add to the test command:
```bash
--enable_sai_log DEBUG
```

**Inspect**: test binary output **and** `/var/log/messages` on the switch for suspicious error logs.

**When to use**: when you suspect the issue is in vendor SDK behavior and need to see internal SDK decisions.

### (B) FBOSS Verbose Logging — `--logging='fboss=DBG5'`

Enables verbose FBOSS agent logging at debug level 5.

Add to the test command:
```bash
--logging='fboss=DBG5'
```

**Inspect**: test binary output for detailed FBOSS agent traces.

**When to use**: when you suspect the issue is in FBOSS agent logic (state machine, config application, handler code).

### (C) SAI Get Attribute Logging — `--enable_get_attr_log`

Enables logging of SAI get attribute calls in the SAI replayer log.

Add to the test command:
```bash
--enable_get_attr_log
```

**Inspect**: SAI replayer logs (`sai_replayer-cb.log` / `sai_replayer-wb.log`) — they will contain additional get attribute entries.

**When to use**: when you need to see what values the agent is reading back from the SDK, not just what it's writing.

### (D) Packet TX Logging — `--enable_packet_log`

Enables logging of SAI packet transmit calls (`send_hostif_packet`) in the SAI replayer log.

Add to the test command (or, in multi mode, to the `fboss_hw_agent` processes):
```bash
--enable_packet_log
```

**Inspect**: the SAI replayer log for packet-send entries.

**When to use sparingly**: to rule out whether packets are being sent at all, or to include packet-send calls in a vendor-escalation replayer log. Tests that send large numbers of packets can overwhelm the logs with this option enabled.

### (E) SAI Call Elapsed-Time Logging — `--enable_elapsed_time_log`

Appends the real SDK wall-clock duration to each logged SAI call in the replayer log
(`... rv: 0 elapsed time 873 μs`). Requires `--enable_replayer`.

```bash
--enable_replayer --enable_elapsed_time_log
```

**When to use**: to profile per-call SDK latency — e.g. find which SAI primitive dominates a
slow warmboot or a scale operation.

> **Full reference**: for the complete SAI Replayer capability list (all flags), plus how to
> capture, post-process, build, and run the `sai_replayer-<impl>-<ver>` binary, see
> [sai-replayer.md](sai-replayer.md).

### (F) Broadcom SDK Debug Log — `--bcm_sdk_log_file <soc-file>`

Next level below the SAI replayer: the replayer shows the SAI *calls* FBOSS made,
this shows what the SDK *did* with them — logical-table operations, API traces.
Broadcom only.

The flag takes a diag/SOC command file, injected as the SDK's
`sai_preinit_cmd_file` and `sai_postinit_cmd_file` (throws `FbossError` if the
path does not exist):

```bash
# /tmp/enable_bcm_debugs.soc on the switch
debug bcm info
log file=/tmp/brcm_sdk.log
```

```bash
--bcm_sdk_log_file /tmp/enable_bcm_debugs.soc
```

**What each debug module dumps** (one cold boot; sizes approximate):

| SOC command | Log size | Content |
|-------------|----------|---------|
| `debug bcm info` | ~49 MB | Master switch. All `lt <TABLE> <lookup|update|insert>` op traces plus driver chatter. Sufficient alone for table-programming checks; submodule `info` lines add nothing. |
| `debug soc info`/`verbose`, `debug bcmlt info`, `debug bcmapi info` | ~580 B | Silent — init banners + warnings only. |
| `debug bcmlt verbose` | ~1 GB | Full table ops with field-level detail (`TABLE_FIELD_INFO` lookups: keys, widths, limits). |
| `debug bcmapi verbose` | ~180 MB | BCM API call traces (`API: bcm_* ->`). No table content. |

**Inspect**: the `log file=` path — analyze per [analyze-logs.md](analyze-logs.md) Step 3.

**When to use**: replayer shows the right SAI calls with `rv:0` but HW behavior
is still wrong — confirm the SDK wrote the expected table entries. Start with
`debug bcm info`; escalate to `debug bcmlt verbose` for field detail. Delete
large logs from `/tmp` when done.

## Example: Combining Options

Re-run a failing cold boot test with SDK and FBOSS verbose logging:

```bash
/root/<user>/<versioned-binary-name> \
  --config <config-path> \
  --gtest_filter=<TestSuite>.<TestName> \
  --enable_replayer --sai_log /root/<user>/sai_replayer-cb.log \
  --enable_sai_log DEBUG \
  --logging='fboss=DBG5' \
  --setup-for-warmboot
```

## Decision Guide

| Symptom | Try Option |
|---------|------------|
| SAI call returns unexpected error | (A) SDK logs |
| Agent logic seems wrong | (B) FBOSS logs |
| Need to see values read from SDK | (C) Get attribute logs |
| Packets not arriving / test expects packet | (D) Packet TX logs |
| SAI calls look right but HW state wrong | (F) BCM SDK debug log |
| No clue where to start | (A) + (B) together |

## Next Steps

- Re-run the failing test with chosen logging options
- Analyze the enhanced logs with [analyze-logs.md](analyze-logs.md)
- If hypothesis formed: see [hypothesis-driven-debug.md](hypothesis-driven-debug.md)
- If the root cause is the vendor SDK/silicon: assemble a reproducible vendor-escalation
  package (replayer log + hw_config + case report) with the [vendor-escalation skill](../../vendor-escalation/SKILL.md)
