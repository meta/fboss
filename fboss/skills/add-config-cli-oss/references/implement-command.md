## Pick a structure

For a *family* of related scalar tunables on one Thrift object (like the 5
ARP timers), use a **single handler class with a `kValidAttrs` set** rather
than one class per subcommand. The reference implementation is
`fboss/cli/fboss2/commands/config/arp/CmdConfigArp.h`.

For a single one-off command with a complex arg shape, follow
`fboss/cli/fboss2/commands/config/l2/learning_mode/CmdConfigL2LearningMode.h`.

## Files to touch (family pattern)

### No string literals in handler code

Code review style rejects literal string constants sprinkled inside
handler logic — lift them to named `constexpr std::string_view` in an
anonymous namespace at the top of the `.cpp`. See
`fboss/cli/fboss2/session/FbossServiceUtil.cpp`
for the canonical pattern (e.g. `kWedgeAgent`, `kSwAgent`,
`kHwAgentPrefix`). Apply the same style to every CLI attribute name
the dispatch code compares against, and build `k<Area>ValidAttrs`
from those constants rather than raw strings.

### New files

1. `fboss/cli/fboss2/commands/config/<area>/CmdConfig<Area>.h`
   - Declare the `class <Area>ConfigArgs`, the `CmdConfig<Area>Traits`
     struct, and the `CmdConfig<Area>` handler. Keep implementation detail
     out of the header; the exception is the attribute-name constants when
     they must be shared with another command tree (e.g. `config arp` and
     `delete arp` share a `namespace arp_attrs` of
     `constexpr std::string_view` names plus a `kValidAttrs` array in the
     header — see `CmdConfigArp.h`). If nothing else needs them, keep them
     in the `.cpp`.
   - `class <Area>ConfigArgs : public utils::BaseObjectArgType<std::string>`
     with a `std::vector<std::string>` ctor that validates exactly 2 args,
     first ∈ valid set, second parses to a non-negative `int32_t` via
     `folly::to<int32_t>` (catch `folly::ConversionError`).
   - `struct CmdConfig<Area>Traits : public WriteCommandTraits` with
     `ObjectArgType = <Area>ConfigArgs`, `RetType = std::string`, and a
     `static void addCliArg(CLI::App& cmd, std::vector<std::string>& args)`
     method that registers the positional via
     `cmd.add_option("<area>_attr_value", args, "<help text>")`. This is
     where the CLI11 registration lives now — there is no central enum or
     switch to touch (config commands migrated off the old
     `ObjectArgTypeId` mechanism in commit `5b534a2aad`). Chain
     `->required()->expected(N)` when the arg count is fixed (see the
     CLI11 pitfall in [Pitfalls observed](review-and-pitfalls.md#pitfalls-observed)).
   - `class CmdConfig<Area> : public CmdHandler<CmdConfig<Area>, CmdConfig<Area>Traits>`
     declaring `queryClient(HostInfo, ObjectArgType)` + `printOutput(RetType)`.

2. `fboss/cli/fboss2/commands/config/<area>/CmdConfig<Area>.cpp`
   - `#include "fboss/cli/fboss2/CmdHandler.cpp"` — important, this is how the
     template gets instantiated
   - The attribute-name constants and valid-attrs set (unless they had to
     live in the header for sharing, per above): one
     `constexpr std::string_view k<Area>Attr<Name>` per CLI attribute name
     in an anonymous namespace, followed by a valid-attrs container built
     from those constants.
   - `<Area>ConfigArgs` constructor body (use the constants for error
     messages + membership checks).
   - `queryClient()` body: get `ConfigSession::getInstance()`, dispatch on
     `args.getAttribute()` against the named constants (not string
     literals), mutate `config.sw()->*` fields, call
     `session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::<LEVEL>)`,
     return success string.
   - `printOutput()` → `std::cout << msg << std::endl;`
   - Trailing `template void CmdHandler<..., ...>::run();` explicit
     instantiation

3. `fboss/cli/fboss2/test/config/CmdConfig<Area>Test.cpp` — inherit
   `CmdConfigTestBase` with a seed-config JSON, test arg validation
   (valid / bad arity / unknown attr / non-integer / negative) and `queryClient()`
   for each attr. See
   `fboss/cli/fboss2/test/config/CmdConfigArpTest.cpp`.
   - **Shape the seed JSON after the production config** (grepped from
     the [Inputs](plan-command.md#inputs) sample in [Map CLI names → Thrift fields](plan-command.md#map-cli-names--thrift-fields)). Copy enough of the sub-tree that each attr's
     `queryClient()` has something realistic to mutate: present
     optionals, populated ordered-lists, existing enum values. A
     reviewer should be able to eyeball the seed and recognize it as a
     subset of what a real device is running.
   - Cite the source in a comment at the top of the fixture so a future
     reader can trace it back.

4. `fboss/cli/fboss2/test/integration_test/Config<Area>Test.cpp` — inherit
   `Fboss2IntegrationTest`; for each attr read current value via
   `getRunningConfig()` thrift, set new value with `runCli()`, `commitConfig()`,
   verify, restore. See
   `fboss/cli/fboss2/test/integration_test/ConfigArpTest.cpp`.
   - Prefer helpers that *derive* targets from the live running config
     (e.g. "find the first queue that has a `pktsPerSec` cap", "use the
     reason that already maps to something") rather than hardcoding
     IDs/names. The sample config tells you which shapes to expect, and
     the running config tells you which specific values this device has —
     basing the test on the latter keeps it portable across devices.

### Modified files

| File | Change |
|---|---|
| `fboss/cli/fboss2/CmdListConfig.cpp` | Add `#include` alphabetically; add `{"config", "<area>", "…", commandHandler<CmdConfig<Area>>, argRegistrar<CmdConfig<Area>Traits>}` to `kConfigCommandTree()` alphabetically. `argRegistrar<T>` dispatches to the Traits' `addCliArg` — do not use the legacy `argTypeHandler<T>` (that's the old `ObjectArgTypeId` path, still used only by not-yet-migrated non-config commands). |
| `cmake/CliFboss2.cmake` | Add the two new source files to `fboss2_config_lib` (alphabetically). |
| `fboss/cli/fboss2/BUCK` | Mirror the cmake additions into `srcs` and `headers` of `fboss2-config-lib`. |
| `cmake/CliFboss2TestConfig.cmake` | Add unit test `.cpp` (alphabetically). |
| `fboss/cli/fboss2/test/config/BUCK` | Mirror. |
| `cmake/CliFboss2TestIntegrationTest.cmake` | Add integration test `.cpp` (alphabetically). |
| `fboss/cli/fboss2/test/integration_test/BUCK` | Mirror. |

Keep the cmake and BUCK file lists in sync — the repo builds with more
than one build system, and a file added to only one of them breaks the
other.
