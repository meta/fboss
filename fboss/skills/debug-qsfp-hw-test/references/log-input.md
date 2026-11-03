# Log Input (Generic)

How to turn the user's input into a local log file at
`/tmp/qsfp_hw_test_debug.log` for grep-based analysis.

## Pasted text or file path

If the user pasted raw log text, save it verbatim:

```bash
cat > /tmp/qsfp_hw_test_debug.log << 'EOF'
<pasted text>
EOF
```

If the user gave a file path, copy it:

```bash
cp <user-provided-path> /tmp/qsfp_hw_test_debug.log
```

## CI run link

A link to a CI release node or test job cannot be resolved with generic
tooling alone: fetching the raw gtest log and checking the known-bad
suppression list both require environment-specific access.

> **Customization point**: If your environment can fetch CI logs (e.g. an
> internal release tracker, test-result database, or job artifact store),
> create `facebook/log-input.md` in this skill directory with the fetch
> commands and the known-bad lookup. The skill will automatically prefer it
> over this file.

Without that override, ask the user to download the raw log from the linked
run and provide it as text or a file.

## Known-bad check without internal tooling

If the `[  FAILED  ]` line ends with `[KNOWN BAD: <reason>]`, report the
failure as suppressed and stop. Without environment-specific config access,
a FAILED line with no such tag should be treated as a real failure.
