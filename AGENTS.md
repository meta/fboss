# AGENTS.md

Guidance for AI coding agents working in this repository.

FBOSS (Facebook Open Switching System) is switch software: `agent` (ASIC
control via SAI), `qsfp_service` (optics), platform services (sensors, fans,
LEDs, firmware), the `fboss2` CLI, and FSDB (state distribution).

## Skills

`fboss/skills/` contains task-specific workflow guides — debugging agent
hardware tests, applying FBOSS coding standards, and reviewing changes. Read
`fboss/skills/README.md` first, then load a skill's `SKILL.md` when its task
matches what you are doing. Prefer these over improvising a workflow.

## Layout

| Path | Contents |
|------|----------|
| `fboss/agent/` | Core agent; `hw/sai/` SAI implementation, `hw/switch_asics/` per-ASIC support |
| `fboss/platform/` | Platform services (sensors, fans, LEDs, firmware) |
| `fboss/configs/platforms/` | Per-platform mapping sources, by vendor and platform |
| `fboss/qsfp_service/` | Transceiver management |
| `fboss/cli/fboss2/` | Command line interface |
| `fboss/fsdb/` | State database |
| `fboss/lib/` | Shared libraries; `platform_mapping_v2/` generates the mappings |
| `fboss/oss/` | Build scripts, test configs, SDK versions |
| `fboss/skills/` | Agent workflow guides |

## Build

FBOSS builds inside a container. See the build documentation at
https://meta.github.io/fboss/ for current commands; the runnable snippets
live in `docs/static/code_snips/`.

- The container image (`fboss/oss/docker/Dockerfile`) provides the toolchain
  and system packages. Do not install build dependencies on the host.
- Builds are memory-hungry. If compilation dies unexpectedly, lower
  parallelism before assuming a compiler or source problem.
- Do not delete the getdeps scratch directory to "fix" a build. It forces a
  full rebuild and rarely addresses the actual failure.
- Forwarding-stack builds against a real vendor SDK require a precompiled
  `libsai_impl.a` and its matching SAI headers. With no SAI implementation
  selected the build uses fake SAI instead, which needs no vendor artifact.
  See `fboss/oss/scripts/run-getdeps.py -h` for the implementation and SDK
  version flags.

## Test

Unit tests and fake-SAI tests run without hardware. Anything named
`*_hw_test` or `sai_test` built against a real SDK requires a switch.

Known-bad and unsupported test lists live under `fboss/oss/`.

## Conventions

- End every file with a trailing newline; use unix line endings.
- Never edit files marked `@generated`.
- Platform mapping JSON is generated into a `generated/` subdirectory. Edit
  the CSVs in `fboss/configs/platforms/<vendor>/<platform>/platform_mapping/`
  (or `.../<platform>/variants/<variant>/platform_mapping/`) and never edit
  anything under `generated/`. Note that `<platform>_vendor_config.json`,
  which sits beside the CSVs, is hand-maintained rather than generated.
- Thrift files generate code. Regenerate rather than hand-editing output.

## Not in this repository

This repository does not contain the Buck build, Meta-internal configuration,
Meta-internal test infrastructure, or Meta-internal CI. If a document or code
comment references those, it applies only to internal checkouts.

For questions, use GitHub Issues or Discussions.
