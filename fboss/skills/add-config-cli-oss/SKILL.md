---
name: add-config-cli-oss
description: Implement a new `fboss2-dev config <area> <attr> <value>` CLI command family mapping to `SwitchConfig` attributes. Figures out the correct action level (HITLESS / AGENT_WARMBOOT / AGENT_COLDBOOT), wires the handler through the full registration + build graph, writes unit + integration tests, runs them on a test switch, and prepares the change for review.
when_to_use: |
  User asks to add a new CLI command (or family of related commands) under
  `fboss2-dev config ...` that sets one or more fields on the agent's
  `SwitchConfig` object. Typical request: "implement a CLI for
  configuring <something>".
argument-hint: "[subcommand-spec] [branch] [test-switch] [sample-config]"
---

# Adding a new `fboss2-dev config <area>` CLI command family

## References

Source paths in the references are relative to the FBOSS repository root.

Read the reference for each workflow step before acting.

| Need | Reference |
|---|---|
| Inputs, field mapping, action levels | [Plan the command](references/plan-command.md) |
| Handler structure, implementation, build registration, tests | [Implement the command](references/implement-command.md) |
| Local build, unit tests, include hygiene | [Build and unit test](references/build-and-unit-test.md) |
| Device testing, crash diagnosis, agent fixes | [Device testing](references/device-testing.md) |
| Commit description and pitfalls | [Review and pitfalls](references/review-and-pitfalls.md) |
