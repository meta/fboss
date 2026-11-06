## Inputs

- A spec of the desired subcommand(s), ideally spelling out the
  `fboss2-dev config <area> <attr> <value>` shape. Related subcommands
  (e.g. a family of timers on the same Thrift object) are best implemented
  together in one change.
- **Optional**: A git branch based on `main`. Will be created automatically
  if it doesn't exist yet.
- **Optional**: the name of a test switch that is already provisioned with a
  recent FBOSS image built from the branch. If provided, skip straight to
  the integration-testing step in [Integration testing on a device](device-testing.md#integration-testing-on-a-device).
- **Optional but strongly preferred**: a path to a real production FBOSS
  `agent.conf` from a deployed device. This anchors the tests to the config
  shape the CLI must actually be able to drive in production.
  - Do **not** read the file end-to-end — production configs are hundreds
    of thousands of lines. Wait until [Map CLI names → Thrift fields](#map-cli-names--thrift-fields), where you've identified the
    Thrift fields of interest, and then use the `Grep` tool with the
    field names (`cpuQueues`, `cpuTrafficPolicy`, `loadBalancers`, etc.)
    to locate the relevant sub-trees. Read a small window around each
    hit (≤200 lines) to capture the nested shape and any unusual
    per-field values.
  - Use what you find to shape the seed JSON in the [Files to touch](implement-command.md#files-to-touch-family-pattern) unit-test fixture
    and the target fields in the [Files to touch](implement-command.md#files-to-touch-family-pattern) integration test — so that the unit
    test proves the CLI mutates a config matching production, and the
    integration test operates on a device state that resembles production.
  - If the user provides only a CLI spec and no sample config, ask whether
    one is available before falling back to an invented fixture —
    production shapes surface edge cases (union fields, ordered lists,
    optional wrappers) that synthetic fixtures miss.

## Map CLI names → Thrift fields

Map each CLI attribute onto the `SwitchConfig` Thrift field it should
mutate, by code exploration:

1. Read `fboss/agent/switch_config.thrift`
   and grep for keywords from the CLI spec (the attribute name, the area
   name, related protocol terms). Field names usually track the CLI
   vocabulary closely (e.g. `timeout` → `arpTimeoutSeconds`,
   `age-interval` → `arpAgerInterval`).
2. Cross-check candidates in `fboss/agent/ApplyThriftConfig.cpp` — the
   place a field is *consumed* confirms both that you found the right
   field and what it actually controls.
3. When a field lives inside a nested struct/list/union (e.g.
   `loadBalancers[id=...].fieldSelection.ipv4Fields`), write down the
   full JSON path from the `sw` root — the handler will need to navigate
   or create that path.

Example mapping (ARP family):

| CLI attr         | SwitchConfig field       |
|------------------|--------------------------|
| `timeout`        | `arpTimeoutSeconds` (i32) |
| `age-interval`   | `arpAgerInterval` (i32)   |
| `max-probes`     | `maxNeighborProbes` (i32) |
| `stale-interval` | `staleEntryInterval` (i32)|

Note the field *types* (i32 / i64 / enum / bool) — the arg-validation
code uses them.

**Now** is the time to grep the sample production config (if one was
provided in [Inputs](#inputs)) for the field names you just mapped. Run
`Grep pattern="<field1>\|<field2>" path=<sample-config>` and read a
small window (≤200 lines) around each hit to capture the nested shape,
ordering, and any unusual values. The JSON you see here is what the
unit-test seed config in [Files to touch](implement-command.md#files-to-touch-family-pattern) should mirror, and it tells you which
specific fields the integration test in [Files to touch](implement-command.md#files-to-touch-family-pattern) is most likely to find on a
real device.

## Determine `cli::ConfigActionLevel`

For each field, answer: **can the agent apply this change at runtime, or does
it need a warmboot/coldboot?**

Check, in order:

1. `fboss/agent/ApplyThriftConfig.cpp` — find where the field is read. If
   it's just a `newSwitchSettings->set<Field>(...)` style assignment, it's
   almost certainly HITLESS.
   If the field is missing from `ApplyThriftConfig.cpp` entirely, it is
   **dead code in the agent** — raise this with the user before wiring the
   CLI (see [Dead-code attributes](#dead-code-attributes) below).
2. `fboss/agent/hw/sai/switch/SaiSwitch.cpp` — grep for `<field>ChangeProhibited()`
   or `<field>` appearing next to a `FbossError` throw inside a
   `SwitchSettingsDelta` handler. If one exists, the field can't change
   hitlessly — use `AGENT_COLDBOOT` (or `AGENT_WARMBOOT` if the comment
   allows).
3. `fboss/agent/state/SwitchSettings.h` — the setter should be a simple
   `set<tag>(value)`. Anything more interesting means look harder.

The three action levels live in
`fboss/cli/fboss2/cli_metadata.thrift`:
`HITLESS`, `AGENT_WARMBOOT`, `AGENT_COLDBOOT`.

Your own code-path investigation (ApplyThriftConfig, SaiSwitch,
SwitchSettings) is the source of truth — treat any level someone guessed
up front as a hint, not fact, and make sure the handler calls
`saveConfig(...)` with the level the code paths actually support.

### Dead-code attributes

Some SwitchConfig fields are declared but never applied by the agent (e.g.
a field present in `switch_config.thrift` but absent from
`switch_state.thrift` and never read by `ApplyThriftConfig.cpp`). Wiring a
CLI for these produces a no-op. Ask the user whether to:

1. Implement the CLI anyway + add a warning comment
2. Expand the change to wire the agent side too
3. Drop that subcommand from scope and defer
