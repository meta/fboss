# Agent Hw Test

## Overview

FBOSS Agent Hw tests are used to verify all FBOSS features like L3 routing, ACL, L2 learning, QoS, Copp, etc. Similar to Link Test, Agent Hw test binary is essentially a `wedge_agent` binary with Google Test (gtest) integrated.

## Setup

Unlike link tests, agent hw tests run on standalone duts and no cable connections are required. All tests involving data plane traffic are implemented by injecting packets to front panel ports put in loopback mode.

## Run Modes (mono vs multi_switch)

The FBOSS agent runs in one of two modes, and agent hw tests can run in either via `run_test.py --agent-run-mode`:

- **multi_switch (recommended)** — the SwSwitch control plane and the HwAgent (ASIC) run as separate processes communicating over Thrift, exactly as in production. `run_test.py` brings up the `hw_agent` service automatically. Binary: `multi_switch_agent_hw_test`.
- **mono (deprecated)** — SwSwitch and HwSwitch run in a single process. Binary: `sai_agent_hw_test-sai_impl`.

**Prefer `multi_switch`** — it mirrors the production process model. `mono` is deprecated and will be removed in the future. When `--agent-run-mode` is omitted, `run_test.py` defaults to `multi_switch`. See the [run reference](/docs/build/packaging_and_running_fboss_hw_tests_on_switch/#sai-agent-tests) for full invocation details and flags.

## Warmboot Soak (N-warmboot iterations)

To stress warmboot stability, run a single test across many consecutive warmboots with `--num-warmboot-iterations N` (default 1). `run_test.py` coldboots once, then warmboots the same test N times — each iteration re-arms warmboot so the next boot warmboots, and the soak stops early on the first non-passing iteration. Typically used with `AgentEmptyTest.CheckInit`:

```bash
./bin/run_test.py sai_agent \
    --config ./share/hw_test_configs/$CONFIG --agent-run-mode multi_switch \
    --filter AgentEmptyTest.CheckInit --num-warmboot-iterations 64
```

## Related Agent Test Types

These build on the same Agent Hw Test framework and are documented separately:

- [Agent Scale Test](/docs/testing/agent_scale_test/) — features at production-like scale (max ECMP groups/members, max ACL entries).
- [Agent Invariant Test](/docs/testing/agent_invariant_test/) — core forwarding invariants (ACL, CoPP, load balancing, DSCP-to-queue) after a production-like config.
- [Benchmark Test](/docs/testing/benchmark_test/) — performance measurements (route scale, init/exit time, etc.).

## Test Cases

All agent hw test cases could be found at

https://github.com/meta/fboss/tree/main/fboss/agent/test/agent_hw_tests
https://github.com/meta/fboss/tree/main/fboss/agent/hw/sai/hw_test

## Steps to Run Agent Hw Test

1. **Compile FBOSS Agent Code**
    1. Please refer to [FBOSS build documentation](https://meta.github.io/fboss/docs/build/building_fboss_on_docker_containers/) on how to build SAI test and FBOSS wedge agent and HW test binary. `build-helper.py` will build all three binaries. [FBOSS Wiki](https://meta.github.io/fboss/docs/build/building_fboss_on_docker_containers/).

2. **Configuration Files Used in SAI Test HW Test**
    1. OSS HW agent test configurations files used in the next section could be found under this folder `fboss/oss/hw_test_configs`. The file in general could split to two parts, FBOSS agent config and Broadcom ASIC bring up config. The FBOSS agent config is in JSON format. Broadcom ASIC bring up config is in yaml format and embeded as plain text in "platform" dicnationary in FBOSS agent JSON config. [example empty agent config with TH5 yaml ASIC config](https://github.com/meta/fboss/blob/main/fboss/oss/hw_test_configs/montblanc.agent.materialized_JSON). This [python script](https://github.com/meta/fboss/blob/main/fboss/lib/asic_config_v2/generate_agent_config_from_asic_config.py) could embed asic config to FBOSS agent config file.
    2. `fruid.json` determines the switch and type, and the example `fruid.json` files can be found [here](https://github.com/meta/fboss/blob/main/fbcode/fboss/oss/scripts/run_configs/). Model name is stored in this file, please read step 5 for more details.

3. **Basic SAI Test**
    1. Here are two commands to run two basic tests, one from FBOSS agent and the other from SAI test to verify the new NPU is working.

    ```bash
    ./sai_test-sai_impl-1.16 -logging DEBUG --gtest_filter=HwVlanTest.VlanApplyConfig --flexports --fruid_filepath /home/bcmsim/config/fruid.json --config icecube_configuration/icepack.agent.materialized_JSON --logging DBG6

    ./sai_agent_hw_test --gtest_filter=AgentEmptyTest.CheckInit --fruid_filepath /home/bcmsim/config/fruid.json --config icepack.agent.materialized_JSON
    ```

    Here is an example `fruid.json` configuration file: [fruid.json](https://github.com/meta/fboss/blob/main/fboss/lib/platforms/PlatformProductInfo.cpp).

4. **Common Debug Steps**

    ```bash
    Fboss agent calls SAI API to communicate with ASIC, here are options to collect SAI replayer.

    -enable_replayer -enable_elapsed_time_log -sai_replayer_sdk_log_level DEBUG -sai_log /root/sai_log
    ```

## Different Batches of Agent Hw Tests

When onboarding new ASIC or platforms, all eligbile hw tests not listed in below known bad test lists are supposed to pass.
https://github.com/meta/fboss/tree/main/fboss/oss/hw_known_bad_tests

However, it is challenging to verify and debug all these hundreds of agent hw tests. Thus, they are split into three different batches of agent hw tests depending on the importance and complexity.

1. **Batch A tests**
These are simple functionality test cases that should provide basic confidence before getting the ping milestone.
- Vlan tests: all tests matching `.*Vlan.*`
- L2 learning: `.*MacLearning.* | .*MacSwLearning.*`
- Neighbor resolution: `*Neighbor.*`
- L3 routing: `.*L3.*`
- Control Plane: `.*Copp.* | .*PacketSend.* | .*RxReason.* | .*PacketFlood.*`
- Queuing: `.*SendPacketToQueue.* | .*DscpQueueMapping.* | .*PortBandwidth.*`
- Prbs: `.*Prbs.*`

2. **Batch B tests**
These are more complicated test cases that should pass, before testing teams run test plan to verify overall functionality.
- Acl: `.*Acl.* | .*DscpMarking.*`
- PFC: `.*Pfc.*`
- QoS: `.*Qos.*`
- Aqm: `.*Aqm.*`
- ECMP: `.*Ecmp.* | .*LoadBalancer.*`
- UDA ACL/Hashing: `.*HwUdfTest.*`
- Queue Per Host: `.*QueuePerHost.*`

3. **Batch C tests**
These are tests either for complicated features or related to performance tuning. Normally, they will not block FBOSS bring up on new platforms at early stages, but they will block platform qualification before deploying in production networks.
- Trunk/LAG: `.*Trunk.*`
- sFlow/Mirroring: `.*Sflow.* | .*Mirror.*`
- PTP: `.*Ptp.*`
- MMU Tuning: `.*IngressBuffer.* | .*MmuTuning.*`
- Stats: `.*ResourceStats.*`
