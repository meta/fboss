# Supplying a vendor SAI SDK

FBOSS never ships an ASIC SDK. The build downloads the **SAI specification**
headers from the public OCP repository, and takes the **implementation** as a
pre-built static archive that you supply at build time. If you have no SDK,
build against fake SAI instead: see [the fake SAI path](#fake-sai).

> Wrap every command on this page in the logging pattern from
> [first-build.md](first-build.md#5a-always-keep-a-build-log). Real-SDK builds
> are the long ones and the ones most likely to fail, so they are exactly where
> a log earns its keep. The commands below are shown bare only to keep the
> flags readable.

## The three versions, and why they are different things

Vendors confuse these constantly, and a mismatch produces errors that look
like FBOSS bugs.

| Concept | Flag | Example | What it is |
|---|---|---|---|
| SAI **spec** version | `--npu-sai-version` | `<major>.<minor>.<patch>` | The OCP SAI header release FBOSS compiles against. getdeps downloads it from `github.com/opencomputeproject/SAI`. |
| SDK **selector token** | `--npu-sai-sdk-version` | `SAI_VERSION_<release>` | An identifier for *your vendor's SDK release*. Injected as a bare `-D<TOKEN>` define. |
| The **binary** | `--npu-libsai-impl-path` / `-tarball` | `libsai_impl.a` | Your compiled SAI implementation. |

All three must agree. The selector token is not decoration: FBOSS's
`SaiVersion.h` switches on it to derive aggregate feature flags (ASIC family,
"SDK is at least version N", and so on). The design intent is to keep
per-version `#ifdef`s out of the wider codebase by concentrating them in one
header.

`SAI_SDK_VERSION` is **mandatory** for `SAI_BRCM_IMPL` and `SAI_TAJO_IMPL`;
CMake fails at configure time without it. Because the token becomes a bare
define, a typo produces a define nobody tests for, and the wrong
version-conditional code compiles silently.

> Any OCP SAI release tag is accepted as the spec version. Nothing checks that
> it is the one your SDK was built against, so getting it right is on you.

### Working out which versions you need

Do not copy version numbers out of a document; they go stale. Derive them:

1. **Selector tokens.** `fboss/oss/scripts/sdk_versions.py` lists every
   registered selector (`SAI_VERSION_*`, `TAJO_SDK_VERSION_*`,
   `CHENAB_SAI_SDK_VERSION_*`) mapped to its `(asic_sdk, sai_sdk)` pair. The
   same list is printed by `run-getdeps.py -h`.
2. **Per-platform production pairings.** `fboss/oss/sdk_versions/wedge_agent.json`
   and `fboss/oss/sdk_versions/qsfp_service.json` give, per platform, the
   `asicSdk` / `saiSdk` / `saiVersion` triple that platform actually runs. If
   you are targeting a specific switch, start here and work backwards to the
   selector token.
3. **Your own SDK.** Ultimately the SAI spec version must match what your
   implementation was compiled against. If your vendor tells you a different
   spec version than the tables suggest, believe your vendor.

**For PHY/PAI you can just read it.** Because PHY vendors ship the SAI headers
inside their SDK, the spec version is a fact about the artifact rather than
something you look up:

```bash
tar xzf pai_impl.tar.gz pai_impl/include/sai/inc/saiversion.h -O | grep -E 'SAI_(MAJOR|MINOR|REVISION)'
# #define SAI_MAJOR 1
# #define SAI_MINOR 18
# #define SAI_REVISION 1      -> --phy-sai-version 1.18.1 (in this example)
```

**For NPU, usually you cannot.** A typical NPU drop contains only the
library and the vendor's extension headers; the SAI spec comes from the OCP
download, so nothing in the tarball records which spec version it was built
against, and it has to be looked up. The exception is an NPU SDK that ships its
own SAI headers in `sai/inc/` (see below): then the version is read from them,
as for PHY.

If you cannot resolve a consistent triple, build against fake SAI and raise it
upstream rather than guessing. A wrong spec version does not fail cleanly.

## NPU (switch ASIC) SDKs

### What you must provide

- Your SAI implementation as either a static archive named **exactly
  `libsai_impl.a`** or a shared library named **exactly `libsai_impl.so`**.
  CMake locates it with `find_library(SAI_IMPL sai_impl)`, so the name is
  load-bearing; it prefers the `.so` when both are present.
- A directory of your SAI **extension** headers, laid out flat.
- Optionally, the core SAI headers your SDK was compiled against, as `sai/inc/`
  beside the library. FBOSS then compiles against those instead of the OCP
  download, and `run-getdeps.py` sets the spec version from their
  `saiversion.h`, so `--npu-sai-version` can be omitted; if given, it must
  match. Ship them whenever your SDK modified the spec headers, because
  compiling against different headers than the SDK used does not fail cleanly.

A shared `libsai_impl.so` is linked dynamically, so binaries share one copy of
the SDK instead of each embedding it, which shrinks them and the package
considerably. Two requirements:

- Its SONAME must be `libsai_impl.so`. Without one, the linker records the
  build-time path in every binary.
- Every other SDK library must ship beside it and be findable from it, for
  example with a `$ORIGIN` RUNPATH on each (`patchelf --set-rpath '$ORIGIN'`).
  That includes libraries the SDK only `dlopen`s, such as per-device ones,
  which no dependency list mentions. `package-fboss.py` copies every shared
  library beside `libsai_impl.so` into the package's `lib/`, not just what
  `ldd` reports.

Two accepted tarball shapes, each optionally wrapped in one top-level
directory:

```text
# flat vendor layout                  # build-helper layout
sdk/                                  sdk/
  libsai_impl.a                         lib/libsai_impl.a
  experimental/                         include/
    brcm_sai_extensions.h                 ...
    ...
```

### How it is consumed

`run-getdeps.py` creates a staging prefix inside the scratch directory
containing **three** symlinks and prepends it to `CMAKE_PREFIX_PATH`:

```text
<scratch>/installed/sai_impl_staging-<fingerprint>/
  lib         -> <your libsai_impl.{a,so} directory>
  include     -> <your extension headers directory>
  experimental-> <your extension headers directory>   # same target
```

The headers directory is linked **twice** on purpose. FBOSS includes vendor
extension headers in two forms, bare (`<brcm_sai_extensions.h>`) and
prefixed (`<experimental/saitamextensions.h>`), while vendor SDKs ship them
flat in one directory. Linking the same directory as both `include/` and
`experimental/` makes both include forms resolve without asking you to
repackage anything.

CMake then finds it with `find_path(SAI_IMPL_DIR NAMES lib/libsai_impl.a lib/libsai_impl.so)`,
which is why the staging prefix must contain a `lib/` subdirectory.

The `<fingerprint>` is derived from the SDK's contents. The staging path ends
up in every compile command's include flags, so an unchanged SDK keeps the same
path and reruns stay incremental, while a different SDK gets a new path and
rebuilds everything that includes its headers. That is intended: it is how a
changed SDK is guaranteed to be picked up. Older staging directories are
removed on each run.

### Extra link dependencies

A shared `libsai_impl.so` records its own dependencies, so this section only
applies to a static archive, which records none.

If `libsai_impl.a` needs additional shared libraries, declare them rather
than patching CMake. Ship a `sai_dependencies.txt` next to the archive:

```text
<your SDK>/lib/libsai_impl.a
<your SDK>/lib/sai_dependencies.txt
```

One entry per line; bare filenames, `-lfoo`, or absolute paths all work. When the
file exists, the build also adds `<SAI_IMPL_DIR>/lib` as a link directory. A repository-root
`sdk_dependencies.txt` exists for the same purpose and ships empty; the
per-SDK file is the vendor-friendly route because it travels with your
artifact.

### Confirming the staging worked

The wrapper prints exactly what it did. A healthy tarball run looks like:

```text
Extracted SDK tarball /path/sdk.tar.gz to <scratch>/installed/sai_impl_tarball
Symlinked <scratch>/installed/sai_impl_tarball/sdk             -> <scratch>/installed/sai_impl_staging-<fingerprint>/lib
Symlinked <scratch>/installed/sai_impl_tarball/sdk/experimental -> <scratch>/installed/sai_impl_staging-<fingerprint>/include
Symlinked <scratch>/installed/sai_impl_tarball/sdk/experimental -> <scratch>/installed/sai_impl_staging-<fingerprint>/experimental
Set ENV SAI_BRCM_IMPL=1
Set ENV SAI_SDK_VERSION=SAI_VERSION_...
Set ENV SAI_VERSION=...
```

Three symlinks, with the headers directory appearing twice. If you see fewer,
the staging did not do what CMake expects and you will fail later with
`SAI_IMPL_DIR-NOTFOUND` or a missing `experimental/` header.

Afterwards, two cheap sanity checks on the result:

- The agent binary should be **large**. Linking a real SDK produces something
  on the order of 1 GB; a fake-SAI agent is a fraction of that. A
  suspiciously small binary means the SDK was not linked in.
- `<build-dir>/npu_sdk_metadata.json` should contain an entry for your binary
  recording `npuSaiImpl`, `npuSaiSdkSelector` and the resolved
  `asicSdk`/`saiSdk` versions. If it is missing or says `unknown`, your
  selector is not registered in `sdk_versions.py`.

### Example

```bash
./fboss/oss/scripts/run-getdeps.py \
  --npu-sai-impl SAI_BRCM_IMPL \
  --npu-sai-sdk-version <selector token> \
  --npu-sai-version <spec version> \
  --npu-libsai-impl-tarball /opt/sdk/sdk.tar.gz \
  build \
  --allow-system-packages \
  --build-type MinSizeRel \
  --extra-cmake-defines='{"CMAKE_CXX_STANDARD": "20", "RANGE_V3_TESTS": "OFF", "RANGE_V3_PERF": "OFF"}' \
  --scratch-path /var/FBOSS/tmp_bld_dir \
  --src-dir "$PWD" \
  fboss
```

## PHY / PAI SDKs

The PHY path is deliberately different, and knowing why saves confusion:

1. The artifact is not `libsai.a` but `libpai.a`, a different thing.
2. PHY vendors ship the SAI headers **inside** their SDK, whereas switch ASIC
   vendors let FBOSS download the public SAI headers by version.
3. PAI needs several companion archives, not one library.

So CMake reads it from a **hard-coded path**, `/var/FBOSS/pai_impl`, rather
than through `CMAKE_PREFIX_PATH`. The wrapper symlinks your SDK into place.

### Required layout

```text
pai_impl/
  lib/
    libpai.a
    libepdm.a
    libphymodepil.a
  include/
    sai/
    pai_macsec/
    epdm/
```

`run-getdeps.py` validates this before doing anything, so a malformed drop
fails fast rather than deep inside CMake.

`/var/FBOSS/pai_impl` must be absent or already a symlink. If it exists as a
real directory (for example from an older manual recipe) the wrapper refuses
to continue; remove it first.

### Example

```bash
./fboss/oss/scripts/run-getdeps.py \
  --phy-sai-impl SAI_BRCM_PAI_IMPL \
  --phy-sai-version <from the PAI SDK's saiversion.h> \
  --phy-pai-sdk-tarball /opt/sdk/pai_impl.tar.gz \
  build \
  --allow-system-packages \
  --build-type MinSizeRel \
  --extra-cmake-defines='{"CMAKE_CXX_STANDARD": "20", "RANGE_V3_TESTS": "OFF", "RANGE_V3_PERF": "OFF"}' \
  --scratch-path /var/FBOSS/tmp_bld_dir \
  --src-dir "$PWD" \
  --cmake-target qsfp_targets \
  fboss
```

Include `--cmake-target qsfp_targets`. Without it the wrapper does a full
fake-SAI build first and *then* rebuilds qsfp against PAI.

Pass `--phy-sai-version` even though the wrapper does not require it. The PHY
pass does not inherit `--npu-sai-version`; without its own value, CMake falls
back to its built-in default spec version, which is unlikely to match the PAI
SDK's headers. Read it from `saiversion.h` as shown above.

### Verifying qsfp actually linked against PAI

```bash
objdump -t <scratch>/build/fboss/qsfp_service | grep pai_create
```

## NPU and PHY together

Pass both. The wrapper runs two sequential passes into one build tree: the
agent against your NPU SDK, then `qsfp_targets` against the PAI SDK. The two
channels routinely need **different SAI spec versions**, which is precisely
why they cannot share one CMake configure:

```bash
./fboss/oss/scripts/run-getdeps.py \
  --npu-sai-impl SAI_BRCM_IMPL \
  --npu-sai-sdk-version <selector token> \
  --npu-sai-version <spec version> \
  --npu-libsai-impl-tarball /opt/sdk/sdk.tar.gz \
  --phy-sai-impl SAI_BRCM_PAI_IMPL \
  --phy-sai-version <phy spec version> \
  --phy-pai-sdk-tarball /opt/sdk/pai_impl.tar.gz \
  build \
  --allow-system-packages \
  --build-type MinSizeRel \
  --extra-cmake-defines='{"CMAKE_CXX_STANDARD": "20", "RANGE_V3_TESTS": "OFF", "RANGE_V3_PERF": "OFF"}' \
  --scratch-path /var/FBOSS/tmp_bld_dir \
  --src-dir "$PWD" \
  --cmake-target fboss_forwarding_stack \
  fboss
```

The `--cmake-target` here applies to **pass 1 only**; pass 2 always forces
`qsfp_targets` regardless of what you asked for. Omitting it builds everything
in pass 1, which is usually not what you want.

Note that `fboss_forwarding_stack` does **not** include the platform services
(`platform_manager`, `sensor_service`, `fan_service`, ...). If you need those
too, add `--cmake-target fboss_platform_services`.

## <a id="fake-sai"></a>Fake SAI: building with no SDK at all

Pass no SAI flags. The wrapper prints

```text
No SAI implementation provided, defaulting to fake SAI build (BUILD_SAI_FAKE=1)
```

and CMake builds `fake_sai`, an in-tree implementation of the SAI C API
assembled from the sources under `fboss/agent/hw/sai/fake/`. The same agent,
test and benchmark executables are produced against it.

This is the entire basis of the public CI, so it is well exercised. It is also
the right starting point for any new platform work: you can develop and run
unit tests long before an SDK is available.

Note the naming rule: fake-SAI **service** binaries keep the `-sai_impl`
suffix (`wedge_agent-sai_impl`), deliberately, so packaging and service unit
files do not differ between fake and real builds. Only **test** binaries get
`-fake`.

## <a id="new-vendors"></a>If your ASIC is not one of the wired implementations

The build recognises only the selectors in the top-level `CMakeLists.txt`
`if/elseif` chain; `run-getdeps.py -h` lists the tokens the wrapper knows.

**Passing any other value does not work, and does not tell you so.** The
wrapper only prints an advisory note, exports your string as an environment
variable, CMake does not recognise it, and you get a build with no SAI
implementation selected. There is no error.

Adding a new implementation is an upstream source change, not a flag. At
minimum it requires:

- a new `option()` plus include/link wiring in the top-level `CMakeLists.txt`;
- a branch in the `if/elseif` chains that select per-vendor source files, in
  `cmake/AgentHwSaiApi.cmake`, `cmake/AgentHwSaiSwitch.cmake` and
  `cmake/AgentPlatformsSai.cmake` (these choose which `.cpp` variants compile,
  so there is no `#ifdef`-only shortcut);
- a branch in `fboss/agent/hw/sai/api/SaiVersion.h` mapping your selector
  tokens to the aggregate feature flags the code tests;
- an entry in `fboss/oss/scripts/sdk_versions.py` so version metadata is
  recorded rather than reported as `unknown`;
- an ASIC definition under `fboss/agent/hw/switch_asics/` and a platform
  mapping.

If you are bringing up a new ASIC, raise it upstream before investing in a
local fork. Build against fake SAI in the meantime.

## Why SDKs are not vendored

Mechanically: no vendor ASIC SDK appears in the getdeps manifest graph at all.
The only SAI-related manifest is `libsai`, which downloads the public OCP
specification headers. Everything else about the implementation arrives
through the flags above, supplied by whoever holds the licence for it.
