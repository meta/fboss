## Build + unit-test locally

Build, with whatever build system your environment uses, the following
targets:

- the `fboss2-dev` binary (`fboss/cli/fboss2`)
- the config unit tests (`fboss/cli/fboss2/test/config`, cmake target
  `fboss2_cmd_config_test`)
- the integration tests (`fboss/cli/fboss2/test/integration_test`, cmake
  target `fboss2_integration_test`)

Then run the unit tests, filtered to the new fixture:

```bash
<path-to>/fboss2_cmd_config_test --gtest_filter='CmdConfig<Area>TestFixture.*'
```

## Clean up includes (before committing)

**After** the build + tests pass, **before** committing, run your
environment's include hygiene tooling (e.g. clang's include-cleaner, IWYU)
over the files you added or changed — add direct includes where a symbol
was being picked up transitively, and remove redundant ones. Re-run the
build + tests afterward.
