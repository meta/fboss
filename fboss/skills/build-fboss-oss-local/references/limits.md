# What this build cannot do

Read this before promising anything. Most of these are design decisions rather
than bugs, but a few are simply gaps.

## <a id="platforms"></a>Platforms

- **x86-64 Linux only.** There is no aarch64 path: the container pulls x86-64
  toolchain artifacts, and the build targets an x86-64 microarchitecture
  level. No macOS, no Windows. The getdeps manifest makes FBOSS a no-op build
  on any non-Linux host.
- **CentOS Stream 9 is the only working container in practice.** The Debian
  image exists and is published, but at least one dependency in the FBOSS
  graph declares *only* an RPM section for RHEL-family 9. On a Debian-family
  host it has no fetcher at all, and the build aborts during dependency
  resolution with

  ```text
  KeyError: 'project gcc12 has no fetcher configuration or system packages
  matching {distro=debian, distro_family=debian, distro_vers=12, ...}'
  ```

  `list-deps` still succeeds, because it resolves the graph without
  constructing fetchers, so a successful `list-deps` is not evidence that a
  build will work.

## One SAI implementation per build tree

The implementation options are a mutually exclusive `if/elseif` chain that
selects which per-vendor source files compile. A single configure can hold
exactly one. Building the agent against an NPU SDK and qsfp against a PHY SDK
is therefore **two passes into the same tree**, which `run-getdeps.py`
orchestrates for you. You cannot produce two differently-linked agents from
one build tree: the agent binary has the same name whichever SDK it links, so
a second configure replaces the first. Keeping both means a separate
`--scratch-path`, at the cost of rebuilding the dependencies there.

## No vendor SDK is included

No ASIC SDK appears anywhere in the dependency graph. Only the public SAI
*specification* headers are downloaded. Everything else must be supplied by
whoever holds the licence. Consequently:

- A fresh clone cannot build a hardware-capable agent for any ASIC.
- Only the SAI implementations in the `CMakeLists.txt` `if/elseif` chain are
  wired in. Anything else silently produces
  a binary with no implementation selected; see
  [supplying-your-sdk.md](supplying-your-sdk.md#new-vendors).

## Testing

- **No hardware tests without hardware.** Anything named `*_hw_test`, and all
  the link tests, need a real switch. The honest no-hardware surface is the
  unit-test tier.
- The public CI runs **fake SAI only**. There is no public CI signal that any
  vendor SDK integration still works, so a green badge does not mean your SDK
  build is fine.
- Benchmarks are not built unless `BENCHMARK_INSTALL` is set, and real-SDK
  benchmarks additionally need hardware.

## <a id="network"></a>Network

There is **no fully offline build path** that is known to work. Even a fake-SAI
build downloads the SAI specification tarball. An image build needs outbound
access to, at minimum: the container registry, the distribution's package
mirrors, PyPI, and GitHub (for the compiler toolchain, the compiler cache, and
a library built from source). A source build additionally needs every
dependency's upstream URL.

<a id="offline"></a>Three partial mechanisms exist, none of them a documented,
verified offline story:

- getdeps has `vendor --output-dir <dir>` to copy every dependency source out,
  and `--vendor-dir <dir>` to build from that copy with no network. This is
  the most promising route but is not part of any FBOSS-specific
  documentation.
- The published prefetch container images are intended to carry dependency
  sources, but see the warning in
  [container-setup.md](container-setup.md#the-prefetch-image).
- `getdeps_fallback_mirror.py` only covers a handful of GNU-hosted archives
  and is explicitly best-effort.

If you need an air-gapped build, plan to validate the `vendor`/`--vendor-dir`
route yourself.

## Resources

FBOSS translation units are unusually large: 1-4 GB of RAM each, sometimes
more. The dependency graph is dozens of projects, most compiled from source.

- Budget roughly 100 GB of disk for the scratch directory, more if you build
  tests and benchmarks.
- High core counts do not help linearly, and over-parallelising causes
  failures that look like compiler bugs. getdeps also caps per-process memory, so an
  unusually heavy translation unit can fail with RAM to spare; see
  [troubleshooting.md](troubleshooting.md).
- Wall-clock time depends heavily on the machine and on whether downloads are
  cached; [first-build.md](first-build.md) has one measured example.

## Build-system parity

The open-source CMake definition is a **hand-maintained twin** of a different
internal build definition. Nothing generates one from the other, and it cannot
be generated in the current design because the internal per-SDK target
expansion lives in files that are not part of this repository.

Practical consequence for a contributor: code can compile internally and still
fail here, most often because a new source file or dependency was added to one
build definition and not the other. If you add a `.cpp`, add it to the
matching `cmake/*.cmake` target yourself, and see
[build-targets.md](build-targets.md#adding-a-source-file) for the checker.

The `BUCK` files present in this repository belong to that other build system.
They are not the build system for this repository, and some of the files they
reference are not exported, so they are not usable here.

## Contributions

This repository is a one-way export of an internal source of truth. Pull
requests are **not merged on GitHub**: they are imported, reviewed and landed
internally, after which the export closes the PR. See
[ci-and-sync.md](ci-and-sync.md).
