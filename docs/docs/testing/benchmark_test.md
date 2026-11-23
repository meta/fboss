# Benchmark Test

## Overview

FBOSS Benchmark Tests measure the performance of agent operations on real hardware — for example route add/delete at scale, ECMP group shrink, init-and-exit time, and control-plane RX rates. They are used to catch performance regressions and to qualify a platform's performance characteristics before production.

All benchmarks are consolidated into two binaries:

- `sai_all_benchmarks-sai_impl` (mono)
- `sai_multi_switch_all_benchmarks-sai_impl` (multi-switch)

The runner discovers benchmarks from the binary via `--bm_list`, optionally pre-filters known-bad tests, and runs each benchmark in its own process for full setup → run → teardown isolation.

## Setup

Benchmarks run on a standalone DUT using the per-platform agent config under `fboss/oss/hw_test_configs`. Results are written to a CSV that includes timing (`benchmark_time_ps`) and, when a platform key is supplied, threshold validation (`threshold_status`).

## Test Cases

All benchmark registrations can be found at:

https://github.com/meta/fboss/tree/main/fboss/agent/hw/benchmarks

To list the benchmarks present in a binary:

```bash
./bin/sai_all_benchmarks-sai_impl --bm_list
```

## Run Modes

Benchmarks can run in either agent run mode:

- **multi_switch (recommended)** — runs `sai_multi_switch_all_benchmarks-sai_impl`; the runner passes `--multi_switch --hw_agent_for_testing` and manages the `hw_agent` service lifecycle automatically, mirroring production.
- **mono (deprecated)** — runs `sai_all_benchmarks-sai_impl` in a single process.

**Prefer `multi_switch`** — it mirrors the production process model. `mono` is deprecated and will be removed in the future. (Note: for the `benchmark` subcommand the historical default is `mono`; pass `--agent-run-mode multi_switch` explicitly to use the recommended mode.)

## Steps to Run Benchmark Test

1. **Build and package FBOSS** following the [build guide](/docs/build/building_fboss_on_docker_containers/) and copy the package to the switch per [Packaging and Running FBOSS HW Tests on Switch](/docs/build/packaging_and_running_fboss_hw_tests_on_switch/). You must be in the `/opt/fboss` directory when running tests.

2. **Run in multi_switch mode (recommended):**

    ```bash
    ./bin/run_test.py benchmark \
        --agent-run-mode multi_switch \
        --config ./share/hw_test_configs/$CONFIG \
        --skip-known-bad-tests "brcm/13.3.0.0_odp/tomahawk5" \
        --test-run-timeout 1800
    ```

3. **Run in mono mode:**

    ```bash
    ./bin/run_test.py benchmark \
        --agent-run-mode mono \
        --config ./share/hw_test_configs/$CONFIG \
        --skip-known-bad-tests "brcm/13.3.0.0_odp/tomahawk5" \
        --test-run-timeout 1800
    ```

    By default (no `--filter`/`--filter_file`) `run_test.py` discovers and runs **all** benchmarks in the binary.

4. **Run a single benchmark** (optional). The name below is **for illustration only** — benchmarks available vary by platform, so use `--bm_list` to find a valid name for your binary first:

    ```bash
    ./bin/sai_all_benchmarks-sai_impl --bm_list   # list valid names

    ./bin/run_test.py benchmark \
        --agent-run-mode multi_switch \
        --config ./share/hw_test_configs/$CONFIG \
        --filter HwFswScaleRouteAddBenchmark
    ```

Common flags:

1. `--agent-run-mode`: `mono` or `multi_switch`. Selects the mono vs multi-switch benchmark binary.
1. `--num-npus {1,2}`: number of NPUs for multi-switch mode (default 1). With 2, `--multi_npu_platform_mapping` is passed to the binary.
1. `--filter`: regex matched against discovered benchmark names to narrow which benchmarks run.
1. `--filter_file`: file with one registered `BENCHMARK()` name per line.
1. `--skip-known-bad-tests`: platform key (`vendor/sdk/asic` — note: a single SDK segment, unlike agent tests) used both to pre-filter known-bad benchmarks *before* running and to load per-platform thresholds. Without it, all discovered benchmarks run and threshold validation is skipped (results show `NO_THRESHOLD`).
1. `--test-run-timeout`: per-benchmark timeout in seconds (default 1200).

Threshold validation looks up entries by benchmark name in `share/hw_benchmark_tests/sai_bench.materialized_JSON`, preferring the matching mode (mono vs multi-switch) and falling back to the other if only one is configured. When no threshold is configured, the runner prints a `WILL ALWAYS PASS` warning so missing entries are visible.
