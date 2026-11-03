# Log Input (Generic)

How to turn the user's input into local files for analysis.

## What you want

| File | Why |
|---|---|
| The link test binary's full stdout/stderr | Failure blocks, gate, per-port dump, crash stack. The tail is almost always teardown / exit noise — never conclude from it |
| The result record, if your runner produces one (test name, test config, known-bad flag, command, exit status, a runner message) | Known-bad, phase prefixes, "hw agent exited non-zero", scope across the job |
| For multi-switch runs: each hw agent's log | When the hw agent crashed but the test binary passed |
| The qsfp_service log for the same window | See `qsfp-log.md` |

## Pasted text or file path

```bash
cat > /tmp/link_test_debug.log << 'EOF'
<pasted text>
EOF
# or
cp <user-provided-path> /tmp/link_test_debug.log
```

If the log came from the system journal, the `Mon DD HH:MM:SS host prog[pid]: `
prefix is fine — the analyzers strip it.

## CI run link

Fetching logs from a CI run needs environment-specific access.

> **Customization point**: create `facebook/log-input.md` in this skill directory
> with the commands that fetch the full test log, the runner's result record, hw
> agent logs, and the known-bad list for your CI. The skill prefers it over this file.

Without that override, ask the user for the full stdout of the failing test (not a
truncated preview) and, if available, the runner's per-test result record.

## Known-bad check

1. If the result record marks the test known-bad, report it as suppressed.
2. Otherwise check the open-source known-bad list
   `fboss/oss/link_known_bad_tests/agent_ensemble_link_known_bad_tests.materialized_JSON`:
   key = test config `<platform>/<sai|bcm>/asicsdk-<from>/<to>[/physdk-<from>/<to>]`,
   entries = `test_name_regex` (regex search against the full result name, including
   `cold_boot.` / `warm_boot.` prefixes).
3. No match: treat the failure as real.
