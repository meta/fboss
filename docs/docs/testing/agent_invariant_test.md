# Agent Invariant Test

## Overview

Agent Invariant Tests verify that a set of core forwarding "invariants" hold after the agent applies a production-like configuration — for example ACLs are programmed, CoPP (control-plane policing) is in effect, load balancing works across ECMP members, DSCP-to-queue mapping is correct, and the diag/Thrift handlers respond. They are a fast confidence check that a config produces a correctly functioning switch, and they share the Agent Hw Test framework (see [Agent Hw Test](/docs/testing/agent_hw_test/)).

Several role-specific variants exist (RSW, RTSW, FTSW, STSW), each applying the config and buffer settings appropriate to that switch role before checking invariants.

## Setup

Invariant tests run on a standalone DUT using the per-platform agent config under `fboss/oss/hw_test_configs`. Some role variants expect role-appropriate settings (for example the RTSW variant enables MMU lossless mode), so use a config that matches the role you are exercising.

## Test Cases

All invariant test cases can be found at:

https://github.com/meta/fboss/tree/main/fboss/agent/test/prod_invariant_tests
https://github.com/meta/fboss/blob/main/fboss/agent/test/prod_agent_tests/ProdInvariantTests.cpp

Example gtest names: `ProdInvariantTest.verifyInvariants`, `ProdInvariantRtswTest.verifyInvariants`.

## Run Modes

Invariant tests can run in either agent run mode:

- **multi_switch (recommended)** — SwSwitch and HwAgent run as separate processes over Thrift, as in production. `run_test.py` brings up the `hw_agent` service automatically. Binary: `multi_switch_invariant_agent_test`.
- **mono (deprecated)** — single-process. Binary: `sai_invariant_agent_test-sai_impl`.

**Prefer `multi_switch`** — it mirrors the production process model. `mono` is deprecated and will be removed in the future. If `--agent-run-mode` is not specified, `run_test.py` defaults to `multi_switch`.

## Steps to Run Agent Invariant Test

1. **Build and package FBOSS** following the [build guide](/docs/build/building_fboss_on_docker_containers/) and copy the package to the switch per [Packaging and Running FBOSS HW Tests on Switch](/docs/build/packaging_and_running_fboss_hw_tests_on_switch/). You must be in the `/opt/fboss` directory when running tests.

2. **Run the full invariant suite (recommended).** `run_test.py` automatically discovers and runs **all** invariant tests; pass `--skip-known-bad-tests` to skip tests known not to pass for your ASIC/SDK (key format `vendor/coldboot-sdk/warmboot-sdk/asic/mode`):

    ```bash
    ./bin/run_test.py sai_invariant_agent \
        --config ./share/hw_test_configs/$CONFIG \
        --agent-run-mode multi_switch \
        --skip-known-bad-tests "brcm/13.3.0.0_odp/13.3.0.0_odp/tomahawk5/multi_switch"
    ```

3. **Run a single test** (optional). `--filter` is only needed to narrow the run; the test name below is **for illustration only** — use any valid gtest name (e.g. a role variant like `ProdInvariantRtswTest.verifyInvariants`):

    ```bash
    # multi_switch (recommended)
    ./bin/run_test.py sai_invariant_agent \
        --config ./share/hw_test_configs/$CONFIG \
        --agent-run-mode multi_switch \
        --filter ProdInvariantTest.verifyInvariants

    # mono
    ./bin/run_test.py sai_invariant_agent \
        --config ./share/hw_test_configs/$CONFIG \
        --agent-run-mode mono \
        --filter ProdInvariantTest.verifyInvariants
    ```

Common flags:

1. `--filter`: GTEST filter passed through to the test binary ([doc](https://google.github.io/googletest/advanced.html#running-a-subset-of-the-tests)). Use it to select a role variant, e.g. `ProdInvariantRtswTest.verifyInvariants`.
1. `--agent-run-mode`: `mono` or `multi_switch` (default `multi_switch`).
1. `--num-npus {1,2}`: number of NPUs to run in multi_switch mode (default 1).
1. `--skip-known-bad-tests`: ASIC/SDK key (`vendor/coldboot-sdk/warmboot-sdk/asic/mode`) used to skip known-bad/unsupported tests. Invariant tests reuse the SAI Agent known-bad lists under `fboss/oss/hw_known_bad_tests` and `fboss/oss/sai_hw_unsupported_tests`.
