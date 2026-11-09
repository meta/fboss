# Getting the qsfp_service Log (Generic)

During a link test, `qsfp_service` runs as its own process on the switch. Its log is
the only evidence for transceiver programming, module capability, I2C, firmware, and
the transceiver / port state machines. The link test only sees the result through
RPC ("not ACTIVE", "link down").

## Where it is

It depends on how your runner starts the service:

| Runner setup | Typical location |
|---|---|
| The open-source test runner (`fboss/oss/scripts/run_scripts/fboss_test_runner`) | systemd unit `qsfp_service_oss`, logging to the journal: `journalctl -u qsfp_service_oss --since "<start>" --until "<end>" > /tmp/qsfp_service_window.log` |
| A dedicated test unit with its own log file | A per-run file named by service start time, e.g. `/tmp/qsfp_service_log_<YYYYMMDD-HHMMSS>` |
| The regular systemd unit | `journalctl -u qsfp_service` (or the unit name your runner uses), or the service's log file |
| Started by hand | Wherever stdout/stderr was redirected |

Journal lines carry a `Mon DD HH:MM:SS host unit[pid]: ` prefix; the analyzers
strip it, and a PID change marks a service restart.

> **Customization point**: create `facebook/qsfp-log.md` with your environment's
> location, transport, and any archived copies. The skill prefers it over this file.

## Fetch only the window you need

The log can be hundreds of MB. Copy only the failure window. Every line carries a glog
timestamp `<sev>MMDD HH:MM:SS`, which is enough to slice:

```text
RUN ON <switch>:
  f=<qsfp_service log file covering the failure time>
  awk -v s="0921 18:36:30" -v e="0921 18:58:30" \
    '{ if (match($0, /[IVWEF][0-9][0-9][0-9][0-9] [0-9][0-9]:[0-9][0-9]:[0-9][0-9]/)) {
         t = substr($0, RSTART + 1, 13); keep = (t >= s && t <= e) }
       if (keep && $0 !~ /BspTransceiverIOTrace/) print }' "$f" | gzip -c > /tmp/qsfp_window.log.gz
DOWNLOAD <switch>:/tmp/qsfp_window.log.gz TO /tmp/qsfp_window.log.gz
RUN ON <switch>: rm -f /tmp/qsfp_window.log.gz
```

Pick the window from the link test log: from ~20 minutes before the failure line
(the SetUp gates poll for up to ~15 minutes) to ~2 minutes after.

## Pick the right file — the most common mistake

Several runs on the same switch leave several log files, and a warmboot roundtrip or
version change restarts qsfp_service into a new file. The right file:

- started **at or before** the failure time, and
- was last written **at or after** the failure time, and
- after slicing, its first/last glog timestamps bracket the failure line.

If none qualifies, the log is gone. Say so and fall back to the link-log-only verdict.
Do not use the nearest file: it describes a different run.

## Then

```bash
bash scripts/analyze_qsfp_service_log.sh /tmp/qsfp_window.log --tcvr <ids> \
  --from "MMDD HH:MM:SS" --to "MMDD HH:MM:SS"
```

and interpret it with `failure-signatures.md` section 5 plus the `debug-qsfp-hw-test`
skill's `references/failure-signatures.md`.
