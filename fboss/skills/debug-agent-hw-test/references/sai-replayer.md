# SAI Replayer (Meta External)

## What It Is

The **SAI Replayer** (a.k.a. SAI Tracer) wraps every SAI API call the agent makes and writes
it out as **compilable C++** — a `run_trace()` function that re-issues the exact same
`create_*` / `remove_*` / `set_attribute` / `get_*` / `send_hostif_packet` sequence against
the real SDK. It is the highest-fidelity record of *what FBOSS actually programmed*.

Use it to:
- **Verify programming** — confirm FBOSS issued the SAI calls you expect (right attributes,
  right order, `rv:0`), to rule a FBOSS bug in or out.
- **Hand the vendor a standalone repro** — a captured log compiles into a binary the vendor
  can run on their own setup with no FBOSS, no test harness. The core artifact of a
  `FAIL_VENDOR` escalation (see the [vendor-escalation skill](../../vendor-escalation/SKILL.md)).
- **Profile SDK latency** — with elapsed-time logging, see how long each SDK call takes
  (e.g. to find a warmboot or scale bottleneck).

> **What it does NOT show**: the replayer records the SAI *calls* (and their return values),
> not the ASIC's runtime packet-processing behavior. A bug where the SDK programs the call
> fine but mis-handles packets at runtime (e.g. wrong egress queue, missing ECN marking)
> will **not** be visible in a replayer trace — use the vendor diag shell + live counters
> for those ([vendor-diag-shell.md](vendor-diag-shell.md)).

## Capabilities (gflags)

All flags are off/default unless set. `--enable_replayer` is the master switch; the others
are no-ops without it.

| Flag | Default | What it enables |
|------|---------|-----------------|
| `--enable_replayer` | `false` | **Master switch.** Wrap + log all SAI calls to the trace. |
| `--sai_log <path>` | `/var/facebook/logs/fboss/sdk/sai_replayer.log` | Output path for the generated C++ trace. |
| `--enable_packet_log` | `false` | Also log `send_hostif_packet` (TX packet buffer + attrs). Off by default — packet-heavy tests can flood the log. |
| `--enable_get_attr_log` | `false` | Also log `get_*_attribute` and `get_stats`/`get_stats_ext` calls, with the values read back. |
| `--enable_elapsed_time_log` | `false` | Append per-call SDK wall-clock latency to each logged line (see format below). |
| `--sai_replayer_sdk_log_level` | `CRITICAL` | SDK log verbosity baked into the replay binary (`DEBUG\|INFO\|NOTICE\|WARN\|ERROR\|CRITICAL`). |
| `--default_list_size` | `4096` | Size of list/attr scratch buffers in the generated code. |
| `--default_list_count` | `6` | Initial number of list scratch buffers (grows on demand). |
| `--log_timeout <ms>` | `100` | Background flush interval; logger flushes even if the buffer isn't full. |
| `--log_variable_name` | `false` | `false` → emit raw numeric object IDs; `true` → emit named variables (more readable, more overhead). |

### Per-line output format & elapsed time

Every logged SAI call emits a `//` C++ comment with a microsecond timestamp, the object id,
and the return value:

```
// 2026-06-30 14:03:21.512348 object_id: 4503599627370497 (0x10000000001) rv: 0
```

With `--enable_elapsed_time_log`, the same line additionally carries the real SDK call
duration — ideal for finding slow primitives:

```
// 2026-06-30 14:03:21.512348 object_id: 4503599627370497 (0x10000000001) rv: 0 elapsed time 873 μs
```

## Choosing flags

| Goal | Flags |
|------|-------|
| Verify FBOSS's create/set/remove programming | `--enable_replayer` (alone) |
| See values FBOSS reads back from the SDK | `+ --enable_get_attr_log` |
| Include the TX packet flood (vendor packet repro) | `+ --enable_packet_log` |
| Profile per-call SDK latency (bottleneck hunt) | `+ --enable_elapsed_time_log` |
| Clean packet-only repro for the vendor | `--enable_replayer --enable_packet_log` and leave `--enable_get_attr_log` off (no GET noise) |

## Capture → Compile → Run

1. **Capture**: run the agent/test with `--enable_replayer` (+ the flags above) and a chosen
   `--sai_log`. The trace is written to that path.
2. **Post-process**: a large trace is split into compilable chunks by
   `fboss/agent/hw/sai/tracer/run/postProcess.py` (chunked at ~400k lines/file) — produces
   the `run_trace()` body, a header, and a `main`.
3. **Stage**: the generated code replaces the stub `run_trace()` in
   `fboss/agent/hw/sai/tracer/run/SaiLog.cpp`.
4. **Build**: the replay binary target is `sai_replayer-<impl>-<ver>` (e.g.
   `sai_replayer-brcm-<ver>`, `sai_replayer-leaba-<ver>`), defined in
   `fboss/agent/hw/sai/tracer/run/run.bzl`.
5. **Run**: copy the binary to the switch; it replays every captured SAI call against the
   real SDK — no FBOSS, no test harness.

> **Customization point**: for the concrete capture/build/run commands in your environment
> (how to run the agent, fetch the log, and build the `sai_replayer-<impl>-<ver>` target),
> create `facebook/sai-replayer.md` in this skill directory. The skill's Reference Routing
> will prefer it over this file.

## Next Steps

- Inspect the trace to confirm programming, or compile it for the vendor.
- For a `FAIL_VENDOR` escalation, bundle the trace with the [vendor-escalation skill](../../vendor-escalation/SKILL.md).
- For runtime ASIC-behavior bugs (queues, marking, drops) the trace can't explain, switch to
  [vendor-diag-shell.md](vendor-diag-shell.md).
