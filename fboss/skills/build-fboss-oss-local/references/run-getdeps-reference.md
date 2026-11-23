# `run-getdeps.py` reference

`fboss/oss/scripts/run-getdeps.py` is the FBOSS wrapper around
`build/fbcode_builder/getdeps.py`. Always go through it rather than calling
getdeps directly for a build: several things CMake requires are set up only
here.

Run `./fboss/oss/scripts/run-getdeps.py -h` for the live flag list and
`--getdeps-help` for getdeps' own. The tables below explain what the flags
*mean*; the script is authoritative on what exists.

## Command shape

```text
./fboss/oss/scripts/run-getdeps.py <wrapper flags> <getdeps subcommand> <getdeps flags> fboss
```

The wrapper parses its own flags, then forwards **everything from the
subcommand onward** to getdeps verbatim. Put wrapper flags first or they are
silently passed through to getdeps, which will reject them.

## Wrapper flags

### NPU (switch ASIC) SAI SDK

| Flag | Meaning |
|---|---|
| `--npu-sai-impl <TOKEN>` | Which implementation. Supported tokens: see `-h` and the `CMakeLists.txt` `if/elseif` chain. Exported as `<TOKEN>=1`. |
| `--npu-sai-sdk-version <SELECTOR>` | **Required** whenever `--npu-sai-impl` is given. e.g. `SAI_VERSION_14_2_0_0_ODP`. Becomes a bare `-D<SELECTOR>` compile define. |
| `--npu-sai-version <X.Y.Z>` | OCP SAI **spec** version to download. Any release tag of the OCP SAI repository; there is no allowlist. |
| `--npu-libsai-impl-path <DIR>` | Directory *containing* `libsai_impl.a` or `libsai_impl.so`. Not the library file itself. |
| `--npu-experiments-path <DIR>` | Directory containing the vendor's flat SAI extension headers. |
| `--npu-libsai-impl-tarball <FILE>` | Alternative to the two path flags: a tarball the wrapper extracts and stages. |

`--npu-libsai-impl-path` and `--npu-experiments-path` must be given
**together**. `--npu-libsai-impl-tarball` is mutually exclusive with both.

### PHY / PAI SDK

| Flag | Meaning |
|---|---|
| `--phy-sai-impl <TOKEN>` | Officially supported: `SAI_BRCM_PAI_IMPL`. |
| `--phy-sai-version <X.Y.Z>` | OCP SAI spec version for the PHY pass. May differ from the NPU one. |
| `--phy-pai-sdk-path <DIR>` | Unpacked PAI SDK; must contain `lib/{libepdm.a,libpai.a,libphymodepil.a}` and `include/{sai,pai_macsec,epdm}`. |
| `--phy-pai-sdk-tarball <FILE>` | Same, as a tarball. Mutually exclusive with `--phy-pai-sdk-path`. |

The three `--phy-*` detail flags are **silently ignored** (warning only) if
`--phy-sai-impl` is not set.

### Everything else

| Flag | Meaning |
|---|---|
| `--benchmark-install` | Sets `BENCHMARK_INSTALL=1`. Without it, benchmark binaries are not built or installed. |
| `--skip-install` | Sets `SKIP_ALL_INSTALL=1`; build without running install rules. |
| `--preserve-env` | Leave already-set environment variables alone. Default is the opposite: flags win over pre-existing environment. |
| `--use-gcc` | Stay on GCC. Default is to force Clang. |
| `--asan` | AddressSanitizer build of FBOSS and every dependency; see [ASAN builds](#asan-builds). |
| `--getdeps-help` | Short-circuits everything and prints getdeps' help. |

### <a id="asan-builds"></a>ASAN builds

`--asan` builds FBOSS and every dependency with AddressSanitizer, and FBOSS
with UBSan as well. Instrumenting the dependencies matters: if only folly and
FBOSS are instrumented, the linker can keep an uninstrumented copy of inline
folly code from another library, which takes code paths folly disables under
ASAN, and ASAN reports them as overflows. Setting `WITH_ASAN=1` yourself
instead of passing `--asan` produces exactly that half-instrumented build.

- **Use a separate `--scratch-path`.** Turning `--asan` on or off rebuilds
  every dependency, and switching back rebuilds them again.
- **Memory.** getdeps' per-process address-space cap is turned off, because
  ASAN binaries reserve terabytes of address space at startup and the build
  runs some (gtest discovery, config generators). The RAM-based job count
  still applies, and peak build memory runs under 10% above a non-ASAN build's.
- **`ASAN_OPTIONS` during the build** is `detect_leaks=0:detect_odr_violation=1`.
  Leak reports would fail instrumented tools such as the thrift compiler at
  exit. Boost is linked into both `libfolly.so` and each binary, a same-size
  duplicate that the default ODR check aborts on; level 1 still reports
  duplicates whose sizes differ.
- **Size.** ASAN binaries are 3.5 to 4x their usual size and carry line tables
  only: file:line in sanitizer reports, but no variables in a debugger. A full
  fake-SAI scratch directory is about 70 GB, against about 30 GB without ASAN.
- **Tests.** Run them with `docker-unittest.py --asan`; see
  [testing.md](testing.md#asan).

## Commonly used getdeps flags (passed through)

| Flag | Meaning |
|---|---|
| `--scratch-path <DIR>` | Where all output goes. Always pass it explicitly. |
| `--src-dir <DIR>` | Build this tree rather than a pinned clone. See [getdeps-concepts.md](getdeps-concepts.md#pinning). |
| `--cmake-target <T>` | Build one aggregate target. Repeatable. Default is `install`. |
| `--allow-system-packages` | Satisfy dependencies from installed packages. Effectively required. |
| `--build-type <T>` | `Debug`, `RelWithDebInfo` (default), `MinSizeRel`, `Release`. |
| `--num-jobs <N>` | Parallelism. Omit by default: getdeps sizes it from available RAM. Pass it only to go lower when an out-of-memory failure coincides with genuinely low free memory; see [troubleshooting.md](troubleshooting.md). |
| `--only-deps` / `--no-deps` | Split the dependency phase from the project phase. |
| `--extra-cmake-defines '<json>'` | Extra `-D` values. The **exact string** feeds the rebuild hash. |
| `--no-tests` | Has no effect on which FBOSS targets build: FBOSS's manifest and CMake do not read it. Scope a build with `--cmake-target` instead. |

## What the wrapper does that getdeps does not

1. **Translates SAI flags into environment variables** that CMake reads
   (`SAI_BRCM_IMPL=1`, `SAI_SDK_VERSION=...`, `SAI_VERSION=...`,
   `BUILD_SAI_FAKE=1`, and the NPU SDK version pair). CMake reads these from
   the environment, not only from `-D`.
2. **Stages the vendor SDK** into the layout CMake searches. See
   [supplying-your-sdk.md](supplying-your-sdk.md).
3. **Pins the SAI spec version** by rewriting
   `build/fbcode_builder/manifests/libsai` in place with the matching URL and
   checksum. getdeps requires a sha256 in every download manifest, so the
   wrapper streams the tarball once to compute it and caches only the hash in
   `<scratch>/sai_spec_shas/<version>.sha256`. The first build of a version in a
   scratch directory therefore needs to reach GitHub, and a version with no
   matching tag fails there with a 404.
4. **Forces the toolchain to Clang** via `update-alternatives`, and
   synthesises the compiler flags by reading the Clang block out of the repo's
   `CMakeLists.txt`. This requires root and effectively only works inside the
   build container.
5. **Runs up to two build passes** into the same tree so one command can link
   the agent against an NPU SDK and `qsfp_targets` against a PHY SDK.
6. **Pre-seeds the download directory** from alternate GNU mirrors, because
   getdeps pins exactly one URL per manifest and mirror rotation can 404 it.

## The pass model

| Flags given | Passes |
|---|---|
| neither | 1: fake SAI (`BUILD_SAI_FAKE=1`) |
| NPU only | 1: NPU |
| PHY only, no `--cmake-target qsfp_targets` | 2: full fake-SAI base, then PHY over `qsfp_targets` |
| PHY only, **with** `--cmake-target qsfp_targets` | 1: PHY only |
| NPU + PHY | 2: NPU base, then PHY over `qsfp_targets` |

Both passes write into the same build tree, and the later one overwrites the
earlier one's `qsfp` artifacts. That is the mechanism, not a bug: a single
CMake configure can hold only one SAI implementation.

Two consequences:

- If you only want qsfp against PAI, pass `--cmake-target qsfp_targets`
  explicitly. Otherwise you pay for a full fake-SAI build first, roughly
  doubling the work.
- In NPU+PHY mode, pass 2 replaces whatever `--cmake-target` you asked for
  with `qsfp_targets`. Your final qsfp binaries are always the PAI-linked
  ones.

Between passes the wrapper resets the SAI environment variables and restores
the `libsai` manifest, so one pass's choices cannot leak into the next.

## Things it does to your checkout

- Rewrites `build/fbcode_builder/manifests/libsai`, and does **not** restore it
  after the final pass.
- Comments out `binutils` in every manifest that lists it, when Clang is used.
  Permanent.
- Neither is reverted. Restore only the files it touched (`git checkout
  build/fbcode_builder/manifests/libsai`, or re-extract the stable-commits
  snapshot); a checkout of the whole `manifests/` directory also discards
  your own edits and the stable pins, and the next build rebuilds everything.

## Non-obvious behaviours

- **Run it from the repository root.** Its Clang setup reads `./CMakeLists.txt`
  from the current directory; from anywhere else it prints
  `Warning: CMakeLists.txt not found, skipping clang-specific flags` and then
  sets *no* compiler flags at all.
- **An unsupported `--npu-sai-impl` value is not rejected.** It is turned into
  an environment variable verbatim, CMake ignores it, and you get a build with
  no SAI implementation selected. Only an advisory note tells you. See
  [supplying-your-sdk.md](supplying-your-sdk.md#new-vendors).
- **An unregistered `--npu-sai-sdk-version` builds**, but the SDK version
  metadata is recorded as `unknown`.
- **Non-`build` subcommands still go through the pass machinery**, so a
  `list-deps` in PHY mode gets `--cmake-target qsfp_targets` injected. Use the
  raw `getdeps.py` for graph queries such as `list-deps`, but not for
  anything involving hashed dependency paths; see
  [getdeps-concepts.md](getdeps-concepts.md#useful-subcommands).
- **Scratch-path detection is a literal scan** of the forwarded arguments with
  a hard-coded fallback. Unusual quoting makes it miss the value and stage SDK
  tarballs into the wrong directory. Pass `--scratch-path` plainly.
