# Tests and test artifacts

## Three tiers

| Tier | Needs | Examples |
|---|---|---|
| Unit tests | nothing | `api_test`, `store_test`, `switch_test`, the platform service `*_test` binaries, `fboss2_cmd_test`, `fsdb_client_test` |
| Hardware tests | a real switch and, for the agent, a real SDK | `sai_agent_hw_test-sai_impl`, `qsfp_hw_test`, `led_service_hw_test`, `*_hw_test`, the link tests |
| Benchmarks | `BENCHMARK_INSTALL`, and hardware for the real-SDK ones | `sai_all_benchmarks-*`, `qsfp_hw_test_benchmark` |

The fake-SAI unit tests exist only when `BUILD_SAI_FAKE` is set. Every
`-sai_impl` test binary exists only when a vendor `libsai_impl.a` was found.

**What you can honestly run with no hardware** is the unit-test tier. Anything
named `*_hw_test` requires a switch. Do not read a green unit-test run as
evidence the agent works.

One partial exception: `run_test.py` accepts `--simulator <asic>`, which
overlays an ASIC-simulator environment and starts `brcmsim.py` before each
test, so agent and SAI tests can run without real silicon. It is not a
general escape hatch. The simulator payloads it installs from
(`install_bcmsim.sh`, `bcmsim.tar.gz` and a `config/` directory under
`/usr/local/fboss/brcmsim/simulator/<asic>/`) are vendor deliverables and are
**not** in this repository, so this path is only open to you if your SDK
licence includes them.

## Running the no-hardware tests

The packaged sweep, which is what public CI runs. It takes the tarball from
[Packaging](#artifacts), so package first:

```bash
./fboss/oss/scripts/github_actions/docker-unittest.py <fboss_bins.tar.zst>
```

It unpacks the tarball, takes every executable in `bin/` whose name ends in
`test` or `tests`, and runs each one inside the build image, skipping
`*hw_test(s)`, `*integration_test(s)` and `*hal_test(s)`. Before each run it
deletes `build/deps/github_hashes/` and re-extracts the stable-commits snapshot
over the checkout, so edits under `build/fbcode_builder` are reverted. Run as
root inside the container, that extraction also restores the archive's owner
onto the checkout.

Before trusting a green result, confirm the sweep had something to run:

```bash
tar --zstd -tf fboss_bins.tar.zst | grep -cE '^\./bin/[^/]*tests?$'
```

If that prints `0`, a pass means nothing. Services-only targets such as
`fboss_fake_agent_targets` contain no test binaries. The fake-SAI agent tests
(`sai_test-fake` and friends) end in `-fake`, so the sweep's pattern skips them
even when they are packaged; run them directly if you need them.

### <a id="asan"></a>From an ASAN build

Add `--asan` for a tarball from a `run-getdeps.py --asan` build:

```bash
./fboss/oss/scripts/github_actions/docker-unittest.py --asan <fboss_bins.tar.zst>
```

Unless `ASAN_OPTIONS` is already set, it uses the build's
`detect_leaks=0:detect_odr_violation=1` (see
[run-getdeps-reference.md](run-getdeps-reference.md#asan-builds)). It also
points ASAN at `llvm-symbolizer` so reports carry file:line, and preloads the
ASAN runtime into Python tests, whose interpreter is not instrumented. Without
it, Python tests fail to import with `undefined symbol: __asan_...`. For a leak
sweep, set `ASAN_OPTIONS=detect_leaks=1:detect_odr_violation=1`; Python tests
keep leak detection off regardless, since CPython's own allocations read as
leaks.

The sweep runs each test binary as one process, so a test that leaves state
behind can fail a later one in the same binary. A failure that disappears with
`--gtest_filter=<Suite.Case>` is that, not an ASAN finding.

### CTest

The unit tests are also registered with CTest via `gtest_discover_tests`, so
`ctest` works in the build directory. Note that CTest registers
`Suite.Case` names, not target names, so `ctest -R <target>` usually matches
nothing. Use `ctest -N` first to see what is actually registered.

## <a id="artifacts"></a>Packaging

Two packagers with different philosophies:

| Script | Produces |
|---|---|
| `package-fboss.py` | sweeps **everything** out of the build tree into `bin/ lib/ share/`, resolves each binary's shared libraries, and copies in all test configs and test-data directories. Large (multiple GB) but complete. |
| `package.py` | a curated set per target (`forwarding-stack`, `platform-stack`, `agent-benchmarks`, `bgp`, `openr`), emitting `<target>.tar` and `<target>-tests.tar`. |

```bash
./fboss/oss/scripts/package-fboss.py \
  --copy-root-libs --scratch-path <scratch> --compress
```

**Always pass `--copy-root-libs`.** It controls whether shared libraries that
resolve under `/lib*`, meaning the build container's own system libraries, are
copied into the package alongside the getdeps-built ones. Without it the
package silently assumes the target switch has matching system libraries, and
since the switch is not running the build container's distribution, that
assumption breaks as a loader error at service start rather than anything
useful at package time.

`--compress` additionally produces `fboss_bins.tar.zst`. The script leaves its
uncompressed staging tree behind next to it, named `fboss_bins-<random>`, and a
new one appears on every run. It duplicates the tarball and is large (tens of
GB for a full build), so once the tarball is known good, delete it:

```bash
package-fboss.py --copy-root-libs --scratch-path <scratch> --compress \
  && tar --zstd -tf <scratch>/fboss_bins.tar.zst >/dev/null \
  && rm -rf <scratch>/fboss_bins-*/
```

The `tar -t` check is not optional: `package-fboss.py` exits 0 even when
compression fails (for example on a full disk), and the staging tree is then
the only complete copy. The trailing slash matches only the staging
directories, never `fboss_bins.tar.zst`. Packaging in the container writes
root-owned files, so re-run the ownership fix-up afterwards; see
[container-setup.md](container-setup.md#uid-gid). Keep the staging tree only if you packaged without
`--compress`, since then it is the output.

The resulting tree is meant to be unpacked at `/opt/fboss` on a switch:

```text
/opt/fboss/
  bin/    binaries, plus run_test.py and the fboss_test_runner package,
          setup_fboss_env, setup.py
  lib/    resolved shared libraries
  share/  test configs, known-bad and unsupported lists, production features,
          per-ASIC run configs, sanity-test filter files
```

Use `package-fboss.py` when the package needs to run tests: it takes the whole
build tree, including the `fboss_test_runner/` package that `bin/run_test.py`
imports. `package.py` produces curated subsets; if you use it, confirm
`bin/fboss_test_runner/` is in the tarball before relying on `run_test.py`.

## Running tests on a switch

Everything is driven by `./bin/run_test.py <subcommand>`, run from the FBOSS
root with the environment sourced:

```bash
source /opt/fboss/bin/setup_fboss_env
cd /opt/fboss
./bin/run_test.py sai_agent --config ./share/hw_test_configs/<platform>.agent.materialized_JSON
```

Subcommands at time of writing (see `./bin/run_test.py -h` for the current
set): `sai`, `sai_agent`, `sai_agent_scale`, `sai_invariant_agent`, `link`,
`qsfp`, `led`, `platform`, `fboss2_integration`, `benchmark`, `bcm`.

Behaviours worth knowing before you interpret a result:

- Each selected test runs **twice** by default, once cold boot and once warm
  boot, each as its own process. `--coldboot_only` disables the second pass.
- `--agent-run-mode` defaults to `multi_switch` for the agent subcommands.
  Mono mode is deprecated and prints a banner.
- Platform config selection is **manual**. There is no auto-detection; you
  pass `--config` (and `--qsfp-config`) explicitly.
- `run_test.py platform` with no `--type` runs all the platform-service
  hardware tests in sequence and aggregates the results.

## Known-bad and unsupported tests

`--skip-known-bad-tests <KEY>` filters the run against the JSON lists shipped
under `share/`. **Without that flag nothing is filtered**, and the runner says
so.

The files look like:

```json
{"known_bad_tests": {"<config-key>": [{"test_name_regex": "SomeTest.Case$"}]}}
```

The key encodes the hardware and SDK combination, and the format differs per
test type, for example `brcm/10.2.0.0_odp/10.2.0.0_odp/tomahawk/multi_switch`
for the agent tests and `<platform>/physdk-<phy>/<phy>` for qsfp. The runner
tries both the key you gave and the same key with the run-mode suffix
added or removed, merging whatever matches. A key that matches nothing only
prints `Warning: Could not find tests for key ...` and the run continues
unfiltered, so check the output for it.

Matching is `re.match`, so patterns are anchored at the **start** of the test
name only. That is why nearly every entry ends in `$`.

Why the mechanism exists: a test can be correct and still fail on one
ASIC/SDK combination. Disabling it in C++ would remove coverage everywhere, so
the exclusion is expressed as data keyed by the exact combination instead.
Note that these lists are generated from an internal source and synced into
the repository, so hand-editing them is not durable.

`fboss/oss/production_features/` maps each ASIC to the features FBOSS actually
uses in production. With `--enable-production-features <ASIC>` (agent tests
only) the runner asks the binary which features each test needs and skips
tests requiring features that ASIC does not ship.

## What a run leaves behind

- `hwtest_results_<timestamp>.csv` in the working directory, with
  `Test Name,Result` where result is `PASSED`/`FAILED`/`SKIPPED`/`TIMEOUT`.
- `--results-json <path>` additionally writes structured records including
  durations.
- Exit code is non-zero only for `FAILED` or `TIMEOUT`; `SKIPPED` does not fail
  a run. A missing binary or a setup exception yields a distinct temp-fail
  code rather than a normal failure.
- `--log-bundle` (a **per-subcommand** flag, so it goes after the subcommand)
  collects service logs, the result CSV, generated configs, `dmesg` and system
  logs into a timestamped directory and zips it. Without it, nothing is
  bundled. This is the artifact to attach when reporting a failure.
- Core dumps are enabled by the generated service units but are **not**
  collected by the bundler; they go wherever the host's core pattern points.

Service logs land in `/opt/fboss/logs/<service>.log`.

## Sanity tiers and filter files

`fboss/oss/hw_sanity_tests/` holds filter files defining T0/T1/T2 tiers. T0 is
simple critical functionality; T2 is complex or performance-sensitive and does
not block bring-up. Use them with `--filter_file`:

```bash
./bin/run_test.py sai_agent --config <cfg> \
  --filter_file=./share/hw_sanity_tests/t0_agent_hw_tests.conf
```

The format is `<gtest-pattern> [tags...]`, `#` for comments. `--profile=s`
selects only lines tagged `s`. `--filter` and `--filter_file` are mutually
exclusive.

## Benchmarks

Individual benchmarks are **not** separate binaries. Everything is
consolidated into `sai_all_benchmarks-<impl>` and
`sai_multi_switch_all_benchmarks-<impl>`, selected at run time with
`--bm_regex`. The reason is link cost: each benchmark executable would
otherwise whole-archive the entire SAI SDK.

Thresholds live in the packaged benchmark JSON. When no threshold is
configured for a benchmark the runner prints a warning that the benchmark
**will always pass** until thresholds are added.
