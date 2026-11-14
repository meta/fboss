# Agent Scale Test

## Overview

Agent Scale Tests stress FBOSS features at production-like scale — for example programming the maximum number of ECMP groups/members and the maximum number of ACL table entries. They share the Agent Hw Test framework (see [Agent Hw Test](/docs/testing/agent_hw_test/)) but link only the scale test cases, so they are a focused way to validate that an ASIC and SDK can hold the resource counts FBOSS expects in the fleet.

## Setup

Like agent hw tests, scale tests run on a standalone DUT with no cabling required; dataplane traffic is exercised via front-panel ports in loopback. They use the same per-platform agent config under `fboss/oss/hw_test_configs`.

## Test Cases

All scale test cases can be found at:

https://github.com/meta/fboss/tree/main/fboss/agent/test/agent_hw_tests (see `AgentEcmpScaleTests.cpp` and `AgentAclScaleTests.cpp`)

## Run Modes

FBOSS agent runs in one of two modes, and scale tests can run in either:

- **multi_switch (recommended)** — the SwSwitch control plane and the HwAgent (ASIC) run as separate processes communicating over Thrift, exactly as in production. `run_test.py` brings up the `hw_agent` service automatically. Binary: `multi_switch_agent_scale_test`.
- **mono (deprecated)** — SwSwitch and HwSwitch run in a single process. Binary: `sai_agent_scale_test-sai_impl`.

**Prefer `multi_switch`** — it mirrors the production process model. `mono` is deprecated and will be removed in the future. If `--agent-run-mode` is not specified, `run_test.py` defaults to `multi_switch`.

## Steps to Run Agent Scale Test

1. **Build and package FBOSS** following the [build guide](/docs/build/building_fboss_on_docker_containers/) and copy the package to the switch per [Packaging and Running FBOSS HW Tests on Switch](/docs/build/packaging_and_running_fboss_hw_tests_on_switch/). You must be in the `/opt/fboss` directory when running tests.

2. **Run the full scale suite (recommended).** `run_test.py` automatically discovers and runs **all** scale tests; pass `--skip-known-bad-tests` to skip tests known not to pass for your ASIC/SDK (key format `vendor/sdk/sdk/asic/mode`):

    ```bash
    ./bin/run_test.py sai_agent_scale \
        --config ./share/hw_test_configs/$CONFIG \
        --agent-run-mode multi_switch \
        --skip-known-bad-tests "<vendor>/<sdk>/<sdk>/<asic>/<mode>"
    ```

3. **Run a single test** (optional). `--filter` is only needed to narrow the run; the test name below is **for illustration only** — use any valid gtest name:

    ```bash
    # multi_switch (recommended)
    ./bin/run_test.py sai_agent_scale \
        --config ./share/hw_test_configs/$CONFIG \
        --agent-run-mode multi_switch \
        --filter AgentEcmpTest.CreateMaxEcmpGroups

    # mono
    ./bin/run_test.py sai_agent_scale \
        --config ./share/hw_test_configs/$CONFIG \
        --agent-run-mode mono \
        --filter AgentEcmpTest.CreateMaxEcmpGroups
    ```

Common flags:

1. `--filter`: GTEST filter passed through to the test binary ([doc](https://google.github.io/googletest/advanced.html#running-a-subset-of-the-tests)).
1. `--agent-run-mode`: `mono` or `multi_switch` (default `multi_switch`).
1. `--num-npus {1,2}`: number of NPUs to run in multi_switch mode (default 1).
1. `--skip-known-bad-tests`: ASIC/SDK key used to skip known-bad/unsupported scale tests. Scale tests use the dedicated scale known-bad list `fboss/oss/hw_known_bad_tests/sai_agent_scale_known_bad_tests.materialized_JSON` (auto-synced from Meta's internal config); unsupported tests still come from `fboss/oss/sai_hw_unsupported_tests`.
