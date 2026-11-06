---
description: Build FBOSS locally from source with getdeps and CMake, in a container - container setup, run-getdeps.py, supplying a vendor SAI SDK, fake SAI, build targets, packaging, tests and test artifacts, and why the build works the way it does. Use when building FBOSS from source on your own machine, debugging a local getdeps or CMake failure, or answering questions about the open-source build system. Also answers how the OSS CI and export work; not for triaging a specific CI run.
user-invocable: true
allowed-tools: Bash, Read, Edit, Write
---

# Build FBOSS OSS Locally

FBOSS builds from source with **getdeps** (a dependency fetcher and build
driver) plus **CMake**, inside a container. This skill covers the whole path:
container, dependencies, build targets, supplying a vendor SAI SDK, packaging,
and running the resulting tests.

This skill is also the reference for *why* the build is shaped this way. If you
are answering a question rather than running a build, the concept files under
`references/` carry the rationale.

## Start here

If you have never built FBOSS, do the **fake-SAI build** first. It needs no
vendor SDK, no hardware, and no licence, and it is the configuration the
public CI builds. Follow [first-build.md](references/first-build.md).

Do not paste build commands from memory. Two details are load-bearing and are
wrong in most older write-ups:

- **`--src-dir` is required to build your own checkout.** Without it, getdeps
  ignores your working tree and builds the pinned upstream revision recorded in
  `build/deps/github_hashes/facebook/fboss-rev.txt`. Your changes silently do
  nothing. (`--current-project fboss`, run from inside the checkout, is the other way to say the same thing.) See [getdeps-concepts.md](references/getdeps-concepts.md#pinning).
- **`run-getdeps.py` has its own flags, and they come *before* the getdeps
  subcommand.** Everything after the subcommand is forwarded to getdeps
  untouched. The shape is always:

  ```text
  ./fboss/oss/scripts/run-getdeps.py <wrapper flags> build <getdeps flags> fboss
  ```

## What you are choosing

Three decisions drive every build.

If the user has not specified the SAI implementation or the build target, and
earlier turns have not settled them, ask in one message before building. Offer
a full fake-SAI build as the default, and ask whether they want an NPU or PHY
SDK instead, or to scope down to one target. For an SDK build, also ask which
SDK version if they have not said; it usually follows from the target switch. A full build is substantially
more work than a single target, so it is worth one question. If scope is already
clear, for example "same as the CI rule" or a named target, state what you are
building and proceed.

| Decision | Options | Where |
|---|---|---|
| SAI implementation | fake SAI (no SDK), an NPU (switch ASIC) SDK, a PHY/PAI SDK, or NPU+PHY together | [supplying-your-sdk.md](references/supplying-your-sdk.md) |
| What to build | one aggregate target (`fboss_platform_services`, `qsfp_targets`, `fsdb_all_services`, `fboss2_targets`, `fboss_forwarding_stack`, ...) or everything | [build-targets.md](references/build-targets.md) |
| Where output goes | `--scratch-path`; there is no `--install-dir` | [getdeps-concepts.md](references/getdeps-concepts.md#scratch-layout) |

Exactly **one** SAI implementation can be active per CMake configure. NPU and
PHY are built as two sequential passes, each reconfiguring the same tree,
which `run-getdeps.py` does for you when you pass both. Building a different
SAI setup into an existing scratch path reconfigures it and replaces its
binaries, and packaging overwrites its `fboss_bins.tar.zst`. A separate
`--scratch-path` (any path works, including a subdirectory of the mounted one)
keeps both builds but rebuilds every dependency, which is the long part. Weigh
that against archiving the old package and reusing the scratch path.

## Preflight

Before starting a build, check the things that fail late and expensively, then
report them to the user in **one compact line** that names what each check
found, so a wrong conclusion is visible. For example (values illustrative):

```text
Preflight: image fresh (built 09-22, docker paths last changed 09-17) · --src-dir set ·
stable commits applied · 142 GB free · SDK tarball found at /opt/sdk/<...>/sdk.tar.gz ·
56 jobs (getdeps default: 168 GB available / 3 GB per job, 166 cores) ·
outputs in scratch: fboss_bins.tar.zst 3.7 GB, distro images 6.4 GB
```

| Check | Why | How |
|---|---|---|
| Container image not stale | a stale image dies at configure time after the whole dependency build | [container-setup.md](references/container-setup.md#is-my-image-stale) |
| `--src-dir` in the command | without it your checkout is silently not compiled | [getdeps-concepts.md](references/getdeps-concepts.md#pinning) |
| Stable commits applied | otherwise dependencies build from upstream heads | [first-build.md](references/first-build.md) |
| Free disk for the scratch path | a full build needs roughly 100 GB | [limits.md](references/limits.md#resources) |
| SDK artifact present, if using one | the path is only checked when staging starts | [supplying-your-sdk.md](references/supplying-your-sdk.md) |
| Job count | makes the parallelism visible before it matters | below |
| Output footprint | packaged tarballs, distro images and leftover packaging trees accumulate in the scratch dir | `du -sh <scratch>/*`; report, never delete outputs |

**Job count.** Do not pass `--num-jobs` by default. Without it, getdeps sizes
parallelism from available RAM and `job_weight_mib` in
`build/fbcode_builder/manifests/fboss`; an explicit value replaces that
calculation, so a number copied from elsewhere (for example a CI cap) can
exceed what the machine's RAM supports. Report the computed number and its
inputs. The mechanism, and the per-process memory cap that comes with it, are
in [getdeps-concepts.md](references/getdeps-concepts.md#parallelism-and-memory).

Skip checks that do not apply, such as the SDK one for a fake-SAI build. Do not
probe network reachability here.

If a check fails or changes the plan, say so in its own sentence instead of
folding it into the line, for example: "Image is stale (built 06-08, docker
paths changed 09-17), rebuilding it first."

## Reference Routing

For each reference pair below, load the `facebook/` version first if it
exists in your checkout. Otherwise load the `references/` version. Treat
the selected file as the source of truth for that topic. Some `facebook/`
files say they add to the matching `references/` file rather than replace it;
in that case read both.

"Your checkout" here means **this skill's own directory**, which is often not
the same tree as your FBOSS source. List the skill directory to decide.

An open-source checkout has no `facebook/` directory; the `references/`
version is complete on its own.

| Need | Try first | Fallback |
|------|-----------|----------|
| First build, end to end | `facebook/first-build.md` | `references/first-build.md` |
| Supplying a vendor SAI SDK | `facebook/supplying-your-sdk.md` | `references/supplying-your-sdk.md` |
| Diagnosing a build failure | `facebook/troubleshooting.md` | `references/troubleshooting.md` |
| CI, and how source reaches this repo | `facebook/ci-and-sync.md` | `references/ci-and-sync.md` |
| How the source is exported to this repo | `facebook/export.md` | the "one-way export" section of `references/ci-and-sync.md` |
| Relationship to the other build system | `facebook/buck-contrast.md` | the "Build-system parity" section of `references/limits.md` |
| Container image, mounts, toolchain | n/a | `references/container-setup.md` |
| What getdeps is and why it exists | n/a | `references/getdeps-concepts.md` |
| Every `run-getdeps.py` flag | n/a | `references/run-getdeps-reference.md` |
| Build targets and the binaries they produce | n/a | `references/build-targets.md` |
| Tests, test artifacts, known-bad lists | n/a | `references/testing.md` |
| What this build cannot do | n/a | `references/limits.md` |

## Rules

- **Build in the container.** Do not install build dependencies on the host.
  The image pins the toolchain the build expects.
- **Never delete the scratch directory to fix a build.** It forces a full
  rebuild and rarely addresses the actual failure. Read
  [troubleshooting.md](references/troubleshooting.md) first; it is ordered by
  the error string you actually saw.
- **Run builds from a script**, one per build configuration under
  `<scratch>/scripts/`, reusing an existing one when it matches rather than
  writing a new one. When reusing an older script, bring it in line with
  current guidance first, for example by removing a hard-coded `--num-jobs`.
  Tell the user the path so they can rerun it themselves.
  See [first-build.md](references/first-build.md#5b-put-the-build-in-a-script).
- **Always tee the build to a timestamped log** under `<scratch>/logs/`, named
  `build-$(date +%Y%m%d-%H%M%S).log`, with a `latest.log` symlink, and use
  `set -o pipefail`. Do this from the first build rather than after the first
  failure: builds fail late and the real error is far above the last
  screenful. This applies to every build, not just the first one. Full pattern
  in
  [first-build.md](references/first-build.md#5a-always-keep-a-build-log).
- **Check the container image is not stale** before reusing an old one. A stale
  image fails after the entire dependency build, on a missing system package.
  See [container-setup.md](references/container-setup.md#is-my-image-stale).
- **On an out-of-memory error, check free memory before lowering
  `--num-jobs`.** With plenty free, the cause is getdeps' per-process cap,
  which job count does not affect; see
  [troubleshooting.md](references/troubleshooting.md).
- **`run-getdeps.py` modifies your checkout.** It rewrites
  `build/fbcode_builder/manifests/libsai` when a SAI spec version is given and
  comments out `binutils` in the manifests when it sets up Clang, and does not
  revert either. Expect a dirty working tree; that is
  normal, not a failed build.
- Read [limits.md](references/limits.md) before promising anything. The build
  is x86-64 Linux only, and a lot of what looks buildable is not.
