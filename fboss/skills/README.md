# FBOSS Skills

This directory contains reusable skills for working on
[FBOSS](https://github.com/meta/fboss), the Facebook Open Switching
System. They cover FBOSS services and components such as Agent, QSFP, FSDB,
platform code, SAI/SDK integration, configuration, and hardware tests. The
skills are plain-text workflow instructions that can be used by coding agents
such as Claude Code, Codex, MetaCode, or other tools that understand
skill-style task guidance. They are written to be useful in both open-source
checkouts and Meta-internal environments.

The open-source skill set focuses on six workflows:

- Debugging FBOSS AgentHwTest failures.
- Packaging a vendor SDK defect for the vendor (escalation package).
- Debugging QSFP HW test failures from logs.
- Adding support for a new transceiver.
- Applying FBOSS coding standards while changing code.
- Reviewing FBOSS diffs with FBOSS-specific review guidance.
- Building, customizing, and provisioning FBOSS Distro images.
- Building FBOSS from source locally with getdeps and CMake.

## Directory Layout

In an open-source FBOSS checkout, the exported skills are expected under:

```text
fboss/skills/
  build-fboss-oss-local/
  add-config-cli-oss/
  debug-agent-hw-test/
  vendor-escalation/
  debug-link-test/
  debug-qsfp-hw-test/
  fboss-distro-image/
  fboss-transceiver-npi/
  fboss-code-standards/
  fboss-review/
```

In Meta-internal checkouts, the same source lives under:

```text
fbcode/claude-templates/components/plugins/fboss/skills/
```

The internal path contains `claude-templates` for packaging/history reasons,
but the skill content is not tied to Claude.

Some skills support optional environment-specific overrides under a
`facebook/` subdirectory. Those overrides are not required for open-source use.
When an override is not present, the skill falls back to the generic files under
`references/`.

## How To Use

Install or expose the `fboss/skills/` directory using the skill-loading
mechanism supported by your coding agent. Then ask the agent to use the relevant
skill by name:

```text
Use debug-agent-hw-test to debug AgentAclTest.AclNexthopTest on my switch.
Use debug-qsfp-hw-test to find why warm_boot.HwStateMachineTest.CheckPortsProgrammed failed in this log.
Use debug-link-test to find why cold_boot.AgentEnsembleEmptyLinkTest.CheckInit failed in this link test log.
Use fboss-distro-image to explain and build fboss-image/from_source.json.
Use fboss-transceiver-npi to add support for an Innolight 2x800G-DR4 optic.
Use fboss-code-standards while changing the route updater.
Use fboss-review to review this pull request.
Use build-fboss-oss-local to do a fake-SAI build of this checkout.
```

Each skill has a `SKILL.md` entry point. The entry point tells the agent which
reference files to read for the task, so users do not need to know the full
directory structure.

## Skills

### `add-config-cli-oss`

Use this skill to add a `fboss2-dev config` command or a related command
family that updates `SwitchConfig`. It covers field mapping, config action
levels, handlers, command registration, build files, unit tests, and device
integration tests.

### `debug-agent-hw-test`

Use this skill when building, running, and debugging FBOSS AgentHwTest failures.
It covers:

- Running one test or a batch of tests.
- Mono and multi-switch AgentHwTest flows.
- Building test binaries and copying them to a switch.
- Running cold boot and warm boot test cycles.
- Reading test output, SAI replayer logs, and crash logs.
- Categorizing failures as FBOSS, vendor SDK, timeout, config, or pass.
- Handing `FAIL_VENDOR` tests to `vendor-escalation`.

The skill is intentionally environment-neutral. The open-source references use
standard `ssh`/`scp` style examples. Meta environments may provide their own
device-access and build-system overrides.

### `vendor-escalation`

Use this skill when an AgentHwTest fails below SAI and the vendor needs a
standalone repro. It builds one package with a README (the case report), an
open-source test and patch, the agent config and hw_config, cold and warm boot
logs, SAI replayer traces and checksums. It covers:

- Shrinking the failing setup to a minimal config with a passing control.
- Choosing SAI replayer options for the symptom: programming failures, packet
  path (`--enable_packet_log`), read-back and warm boot
  (`--enable_get_attr_log`), latency (`--enable_elapsed_time_log`), SDK logs,
  and A/B differentials.
- Capturing mono and multi-switch runs, including the hw_config snapshot.
- Summarizing replayer traces with line numbers for the README, and assembling
  and scrubbing the package.

```text
Use vendor-escalation to package AgentAclCounterTest.VerifyCounterBumpOnTtlHit for Broadcom.
```

### `debug-link-test`

Use this skill when an FBOSS link test failed and you have its log, the
runner's result record, or a CI run link. It covers:

- Finding the verdict and classifying the failure: SetUp link-up /
  transceiver-state / port-state gates, test-body assertions, fatal CHECKs,
  SAI init failures, and aborts (self-abort vs timeout watchdog) with the
  meaningful stack frames.
- Decoding the per-port IPHY / XPHY / transceiver dump printed for ports that
  never came up.
- Deciding when the link test log is not enough and root-causing the optics
  side from the qsfp_service log for the same window (module capability,
  I2C, programming, remediation, firmware).

The skill reports root cause plus evidence only. It does not suggest code
fixes or file known-bad entries.

How to use it (two modes):

- **Hand it the logs.** Give the link test log and, when you have them, the
  qsfp_service log for the same run, the hw agent logs (multi-switch), and the
  per-test result record. It analyzes only what you give it and fetches
  nothing. If the answer needs a log you didn't provide, it tells you exactly
  which log, time window, and transceivers would settle it.

  ```text
  Use debug-link-test on /tmp/link_test.log and /tmp/qsfp_service.log.
  ```

- **Let it fetch.** In checkouts that include the environment-specific
  `facebook/` files, give it a CI link, a result record, or a failing test name.
  It finds the failure, downloads the link test log, and pulls the matching
  qsfp_service log for the failure window (from the runner's upload or from the
  switch), checking that the log really covers the failure time.

  ```text
  Use debug-link-test to find why cold_boot.AgentEnsembleEmptyLinkTest.CheckInit failed on montblanc today.
  ```

### `debug-qsfp-hw-test`

Use this skill when a QSFP HW test (`qsfp_hw_test-<impl>-<version>`) failed
and you have its log as pasted text, a file, or a CI run link. It covers:

- Isolating the failing `cold_boot.` / `warm_boot.` gtest verdict.
- Searching backwards for the killer assertion, fatal CHECK, or setup-gate
  failure.
- Dismissing benign retry, telemetry, and teardown noise with sources.
- Mapping the failing suite to its source file and reporting root cause
  plus quoted evidence.

The skill reports root cause plus evidence only. It does not suggest code
fixes or file known-bad entries.

### `fboss-transceiver-npi`

Use this skill to add support for a new transceiver. In an open-source
checkout it covers the code change that teaches `qsfp_service` about a new
media type:

- Assigning a `MediaInterfaceCode` from the SFF-8024 reference tables.
- Adding the enum entries to `fboss/qsfp_service/if/transceiver.thrift`.
- Adding the matching entry to `TransceiverPropertiesDefault.h`, including
  lane maps, speed combinations, and speed-change transitions.
- Building and running the two tests that cover those files,
  `transceiver_properties_manager_test` and `cmis_test`.

`TransceiverPropertiesDefault.h` is keyed by media type rather than by vendor,
so a new vendor part for an already-supported media type usually needs no code
change at all — the skill checks for that first.

Two further phases — placing firmware images and registering automated-test
nodes — depend on infrastructure that has no open-source counterpart, and are
available only where the corresponding `facebook/` overrides are present.

### `fboss-code-standards`

Use this skill as passive coding guidance when modifying FBOSS code. It captures
review patterns and common pitfalls across:

- Agent architecture, including mono and multi-switch behavior.
- Warmboot state preservation.
- SAI/SDK object lifecycle and attribute handling.
- FSDB and `thrift_cow` usage.
- Platform and config changes.
- Agent HW test patterns.
- General C++ conventions used in FBOSS.

This is the skill to consult before or during implementation.

### `fboss-review`

Use this skill for a structured FBOSS code review. It combines general code
review with FBOSS-specific reviewers for:

- Reliability and error handling.
- Engineering quality and performance.
- Code clarity and API design.
- Summary and test-plan quality.
- Silent failure risks.
- Agent architecture.
- FSDB and `thrift_cow`.
- Platform and config changes.
- SAI/SDK integration.
- Testing standards.
- Cross-cutting FBOSS architecture.

The skill reports findings to the user only. It does not post review comments
automatically.

### `fboss-distro-image`

Use this skill to build, customize, explain, or provision FBOSS Distro images.
It covers:

- Guided platform-to-manifest planning with concrete artifact requirements.
- Full images and component-only builds with the `fboss-image` CLI.
- Manifest structure and dependency ordering.
- Kernel, BSP, NPU/PHY SAI, platform-stack, and forwarding-stack artifacts.
- USB, PXE, and ONIE output formats.
- PXE provisioning with `distro_infra`.
- Build and provisioning failure diagnosis.

The workflow is self-contained for open-source checkouts.

### `build-fboss-oss-local`

Use this skill to build FBOSS from source on your own machine, to diagnose a
failed build, or to answer questions about how the open-source build works. It
covers:

- The build container and the `run-getdeps.py` wrapper around getdeps.
- A first build with fake SAI, which needs no vendor SDK or hardware.
- Supplying a vendor NPU or PHY SAI SDK.
- Build targets, packaging, and the tests that run without hardware.
- Failures ordered by the error message you see.
- Why the build is shaped the way it is, and what it cannot do.

## Typical Usage

For an AgentHwTest failure:

1. Use `debug-agent-hw-test`.
2. Gather the test name, mode, SDK/vendor SAI, switch, and config file.
3. Build the relevant binaries.
4. Run cold boot and warm boot.
5. Analyze logs only for failing tests.
6. Categorize the result and record the evidence.

For code changes:

1. Use `fboss-code-standards` while implementing.
2. Run the relevant unit tests, AgentHwTests, or build targets.
3. Use `fboss-review` before submitting a diff or pull request.

For a distro image:

1. Use `fboss-distro-image` to inspect or author the manifest.
2. Choose a complete image or component-only build.
3. Verify the generated USB, PXE, or ONIE artifacts.
4. Use the matching provisioning workflow with its safety checks.

## Environment Notes

The skills avoid hardcoding one company's lab or build setup. They describe
abstract operations such as:

```text
UPLOAD <local-file> TO <switch>:<remote-file>
RUN ON <switch>: <command>
DOWNLOAD <switch>:<remote-file> TO <local-file>
```

Map those operations to your environment:

- In a generic open-source lab, this may be `ssh` and `scp`.
- In a Meta-internal lab, this may be a lab access tool or MCP integration.
- In CI, this may be a job runner or artifact upload/download step.

The important part is the workflow: build fresh binaries, deploy them, run the
test, inspect the right logs, and keep enough evidence for the result.

## Contributing

When adding or updating a skill:

- Keep instructions generic unless they belong in an environment-specific
  override.
- Put reusable workflow documentation under `references/`.
- Keep Meta-only or lab-specific commands out of open-source references.
- Prefer small, checkable rules with a clear "what to check" and "why it
  matters".
- Test the skill on a real or representative FBOSS workflow before publishing.
