## Commit + review

Write a commit message / review description with:

- **Summary** — what the commands do, action level, deferred pieces
- **Test Plan** — paste unit test output, integration test output, sample
  CLI transcript

If any design choice is deferred (e.g. a sub-attribute out of scope),
record *why* in the review description or the tracking issue so reviewers
don't have to reconstruct the reasoning.

## Pitfalls observed

- **Forgetting `addCliArg` on the Traits (or wiring `argTypeHandler`
  instead of `argRegistrar` in `CmdListConfig.cpp`)** — the command tree
  registers fine but argument parsing silently no-ops.
  `BaseCommandTraits::addCliArg` is a no-op default, so a Traits that
  omits it compiles cleanly and just never receives its positional.
- **Parent "branch" node must have a handler for depth to increment**.
  If the parent of your leaf commands in `CmdListConfig.cpp` has no
  `commandHandler`, `addCommandBranch()` does not increment depth, so all
  leaves register their positional args at the same
  `CmdArgsLists::data_[0]` slot. Give the parent a trivial
  "Incomplete command" handler (see `CmdConfigCopp`, `CmdConfigL2`, or
  `CmdConfigQos` for the pattern) and set `ParentCmd = <ParentHandler>`
  on each leaf's Traits — otherwise the first argument of a multi-token
  positional gets dropped or the wrong slot gets read.
- **CLI11 subcommand fallthrough silently reclassifies positionals**
  when an argument value happens to match a subcommand name elsewhere in
  the tree. `config copp reason arp queue 0` broke because CLI11's
  `_valid_subcommand` recurses up the ancestor chain and matches "arp"
  against `config arp`, stealing it from reason's positional option. The
  fix is `->required()->expected(N)` on the `add_option(...)` chain in
  your Traits' `addCliArg`, which makes `_parse_subcommand()` route the
  token to `_parse_positional()` instead (CLI11 checks
  `_count_remaining_positionals(required=true) > 0` before doing the
  subcommand match). Apply whenever a positional arg's value space
  overlaps with subcommand names — especially protocol names like `arp`,
  `ndp`, `bgp`, `lldp` which are subcommands of `show`, `clear`, and
  `config`.
- **Using `session.saveConfig()` without args** when the action level is not
  HITLESS — the overload defaults to `AGENT` + `HITLESS`. Always pass the
  explicit service + level pair unless HITLESS is correct.
- **Missing `#include "fboss/cli/fboss2/CmdHandler.cpp"`** in the .cpp file
  — you'll get a link error about missing `CmdHandler<...>::run()`
  instantiation.
- **Testing `queryClient()`, not `printOutput()`** — unit tests should
  focus on the query/mutation side. Don't write tests that capture stdout
  from `printOutput`.
- **Device state assumptions** — don't assume a freshly imaged test switch
  is actually healthy. Always run through the [Integration testing on a device](device-testing.md#integration-testing-on-a-device) sanity checklist before
  trying to run integration tests.
- **Deploying `fboss2` instead of `fboss2-dev`** — only the `-dev` binary
  has the `config` subcommand tree. The parent binary rejects `config ...`
  with `The following arguments were not expected: ...`, which reads like a
  CLI parsing bug but is really the wrong target. Always build and copy
  the `fboss2-dev` output.
- **Device state leaks between integration-test runs** — tests capture the
  initial state as `originalFields` / `originalValue` for restore, but a
  crash-interrupted test run leaves the device in whatever the last
  successful commit produced, not the pre-test baseline. The next run
  captures *that* as "original" and a hardcoded restore string can then
  mismatch. Prefer deriving restore tokens from the captured state; if you
  hardcode them, know the first run after a failure may fail for
  test-setup reasons unrelated to the code under test (just re-run once
  the device settles).
- **Truncated core dumps are useless** — `coredumpctl info <pid>` showing
  `Storage: ... (truncated)` means gdb can't recover a real backtrace. Go
  straight to `/var/facebook/logs/fboss/wedge_agent.log` instead (see [When the CLI surfaces an agent-side crash](device-testing.md#when-the-cli-surfaces-an-agent-side-crash)).
- **sw_agent crash trail is a red herring** in the hw/sw-split deployment —
  when the hw_agent dies, the sw_agent aborts seconds later inside a
  cleanup path. Focus on the hw_agent crash; the sw_agent stack won't
  point at the real bug.
