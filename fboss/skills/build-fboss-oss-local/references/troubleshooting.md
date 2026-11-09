# Troubleshooting

Ordered by the error string you actually saw. Before anything else:

- **Do not delete the scratch directory.** It forces a full rebuild and rarely
  addresses the real failure.

Where the real error sits depends on which phase failed, so it is worth
knowing both shapes rather than always starting in the same place:

- A **configure** failure (`CMake Error at ...`) stops immediately, so it is at
  the end of the log, give or take some trailing output from dependency
  downloads still finishing in parallel.
- A **compile** failure is different: the builder passes `-k 0` to ninja, so
  ninja keeps launching jobs after the first `FAILED:`. Later failures can be
  cascades of the first, and successful output continues past it. If the tail
  does not explain itself, search back for the first `FAILED:`.

Each project also writes its own `<build-dir>/<project>/getdeps_build.log`,
which is often the quickest way to isolate *which* dependency broke without
reading one interleaved stream.

## `KeyError: 'project <name> has no fetcher configuration or system packages matching {...}'`

getdeps could not work out how to obtain a dependency on this host.

1. Are you running inside a current container image? A project with no
   download URL can only come from installed packages, so a stale image or a
   build outside the container is the common cause. See
   [container-setup.md](container-setup.md#is-my-image-stale).
2. Have you run `getdeps.py install-system-deps --recursive fboss`? The error
   text suggests it.
3. Did you pass `--allow-system-packages`? Without it, dependencies that have
   a URL are built from source rather than taken from the image.
4. Is the named project simply unsupported on your distribution? Check its
   manifest for a section matching your `distro_family`. If the only package
   section is RPM-family and you are on Debian, this is
   [a hard platform limit](limits.md#platforms), not a fixable local problem.

## `SAI_IMPL_DIR-NOTFOUND`, or `cannot find -lsai_impl`

CMake could not find your vendor SDK.

- `--npu-libsai-impl-path` takes the **directory containing** `libsai_impl.a`,
  not the `.a` file. Older examples pass the file and now fail.
- `--npu-libsai-impl-path` and `--npu-experiments-path` must be given
  **together**. Giving neither is not an error at the wrapper level; it just
  prints that it is skipping SDK preparation, and you fail later in CMake.
- The library must be named exactly `libsai_impl.a` or `libsai_impl.so`.
- The staging directory lives under `<scratch>/installed/` and is recorded in
  the CMake cache. If you deleted it, or ran a later build with different SDK
  flags (which replaces it), an incremental rebuild cannot find the old one.
  Re-run with the SDK flags.

## `fatal error: experimental/<something>.h: No such file or directory`

Your SDK ships extension headers flat, and FBOSS is including one with an
`experimental/` prefix. The wrapper handles this by symlinking your headers
directory twice (as both `include/` and `experimental/`). If you are hitting
it anyway, you are probably on an older `run-getdeps.py`, or you bypassed the
wrapper and staged the SDK by hand. Update the source rather than repackaging
the SDK.

## CMake fails immediately complaining `SAI_SDK_VERSION` is empty

`--npu-sai-sdk-version` is mandatory with `SAI_BRCM_IMPL` and
`SAI_TAJO_IMPL`. If you called getdeps directly instead of going through
`run-getdeps.py`, nothing set it for you.

## CMake configure fails on a missing system library

For example `CMake Error at CMakeLists.txt:<n>: jemalloc is required for
non-ASAN FBOSS builds`. System libraries come from the container image, and
FBOSS checks for them at its own configure step, after every dependency has
built, so this fails late.

The usual cause is a stale image: run the staleness check in
[container-setup.md](container-setup.md#is-my-image-stale). Also confirm the
build is running inside the container at all. After rebuilding the image,
recreate the container from it with the same mounts; a running container keeps
the old image. Dependencies are cached in the bind-mounted scratch directory,
so the retry usually goes straight to FBOSS; if the new image changed the
compiler path, they rebuild.

> **Re-run the failed command byte-identically.** Do not "improve" it on the
> retry by adding flags from the canonical command in
> [first-build.md](first-build.md). `--extra-cmake-defines` is hashed verbatim,
> so adding it, or even changing its whitespace, invalidates every cached
> dependency and restarts the full build. Adopt a different flag set from a
> fresh scratch path, not mid-recovery. See
> [getdeps-concepts.md](getdeps-concepts.md#the-project-hash-and-what-forces-a-rebuild).

## `error: unrecognized arguments: ...`

A flag was given to the wrong program, or does not exist in this checkout.
`run-getdeps.py -h` lists the wrapper's own flags, which must come before the
getdeps subcommand; `run-getdeps.py --getdeps-help` lists getdeps' flags, which
come after it.

## `/var/FBOSS/pai_impl already exists and is not a symlink`

You have a real directory there, probably from an older manual PAI recipe.
Remove it; the wrapper wants to own that path as a symlink.

## `Warning: CMakeLists.txt not found, skipping clang-specific flags`

You ran `run-getdeps.py` from somewhere other than the repository root. It
reads `./CMakeLists.txt` to derive compiler flags, and when it cannot it sets
**no** flags at all, not just the Clang-specific ones. `cd` to the root.

## `detected dubious ownership in repository` on a resumed build

The scratch directory was handed back to your user after the last build, so
root git inside the container now distrusts the dependency clones under
`repos/`. The first build works and every resumed one fails. Tell git to trust
them, either inside the container (`git config --global --add
safe.directory '*'`) or by starting the container with
`-e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory -e GIT_CONFIG_VALUE_0='*'`.
The canonical build script does the former. See
[container-setup.md](container-setup.md#uid-gid).

## `ninja: fatal: posix_spawn: Operation not permitted`

The container's PID limit was reached. Re-run the container with
`--pids-limit=0`.

## `LLVM ERROR: out of memory`, `std::bad_alloc`, or compiles dying with no message

Two different causes produce the same message, and they need opposite fixes.
Check free memory before doing anything (`free -g` on the host):

**Free memory is low: real memory pressure.** Too many large compiles at once.
Lower `--num-jobs`.

**Plenty of memory is free: getdeps' per-process address-space cap.** getdeps
sets `RLIMIT_AS` on the `cmake --build` step, inherited by ninja and every compiler,
at `job_weight_mib * 10` MiB (read `job_weight_mib` from the fboss manifest;
for example, 3072 gives 30 GiB) of *virtual* address space per process, and a compiler's virtual
size runs well above its resident size. A single translation unit with a
resident peak around 15 GB can exceed it. Lowering `--num-jobs` cannot help:
the cap is per process, not shared, and free RAM is irrelevant. Note that
`ulimit -v` in your own shell reports `unlimited` and is misleading here; the
cap exists only in getdeps' children. To confirm, while it is compiling:

```bash
grep 'Max address space' /proc/<compiler pid>/limits    # bytes = job_weight_mib * 10 * 2^20
```

The fix is to build the remaining targets without the cap, using the build
directory getdeps already configured. Open the build environment with your
exact build command, `build` replaced by `debug`; that shell is not under the
cap and carries the SAI environment, so a CMake re-run inside it stays correct:

```bash
./fboss/oss/scripts/run-getdeps.py <wrapper flags> debug <getdeps flags> fboss
# in that shell (omit <target> to finish everything):
ninja -C <scratch>/build/fboss -j <small number> <target>
```

Once it finishes, re-running your original build command (for example to
install or package) is safe: the heavy objects are already built, so the capped
step does not recompile them.

Plain `ninja` from an ordinary container shell also escapes the cap, but only
while no CMake file changes; see
[getdeps-concepts.md](getdeps-concepts.md#iterating-without-getdeps). Keep the
job count low, since these translation units are genuinely large.
Raising `job_weight_mib` in the manifest would also lift the cap, but changing the manifest changes the rebuild
hash and rebuilds every dependency. If your getdeps honours
`GETDEPS_NO_MEMORY_LIMIT`, setting it to `1` before the build drops the cap
without either.

## `AddressSanitizer failed to allocate ... ReserveShadowMemoryRange failed`

An ASAN binary ran under an address-space limit: it reserves terabytes for
shadow memory at startup. In a build, that is getdeps' per-process cap (see
above) applied to a test binary during gtest discovery or to a code generator.
`--asan` turns the cap off, but only with a getdeps that honours
`GETDEPS_NO_MEMORY_LIMIT`; an older stable-commits snapshot replaces
`build/fbcode_builder` with one that does not. Outside a build, check
`ulimit -v`.

## `ImportError: ... undefined symbol: __asan_...`

A Python test loaded an extension module from an ASAN build into the
uninstrumented interpreter. Run the sweep with `docker-unittest.py --asan`, which
preloads the ASAN runtime; see [testing.md](testing.md#asan).

## `sudo: ... pam_loginuid` / sudo fails inside the container

Missing `--cap-add=CAP_AUDIT_WRITE` on `docker run`.

## `netavark: ... can't initialize iptables table 'nat'`

Container networking failed to set up on this host. Use `--network=host`.

## Downloads fail: 404, connection reset, or a mirror error

getdeps pins one URL per dependency and retries only that URL, and
`run-getdeps.py` already tries alternate mirrors for GNU-hosted archives before
the build starts. Most download failures are transient: retry the same
command. If one keeps failing, check network reachability from the build host.

## Your code changes have no effect

You almost certainly omitted `--src-dir`. Confirm:

```bash
python3 build/fbcode_builder/getdeps.py show-source-dir fboss
```

If it prints `Using pinned rev <sha> for https://github.com/facebook/fboss.git`
and a path under `repos/`, getdeps is building a pinned upstream clone rather
than your tree.

If `--src-dir` was already correct, check that you are **running** the binary
you just built. The build writes to `<scratch>/build/fboss/`, while a packaged
or previously deployed copy lives at `/opt/fboss/bin/`. Compare timestamps
before concluding the compiler ignored your edit.

## A new `cmake/*.cmake` file is ignored

The glob that includes them has no `CONFIGURE_DEPENDS`, so an incremental
build does not notice the new file. Force a reconfigure.

## `undefined reference to` a symbol you just added

Usually a new `.cpp` that the CMake build does not know about: the CMake files
are maintained by hand, separately from any other build definition, so a file
added elsewhere is not picked up here. The call site compiling also shows your
checkout is being built, so this is not the `--src-dir` problem. See
[build-targets.md](build-targets.md#adding-a-source-file). Adding the file
changes a CMake file, so rebuild through `run-getdeps.py`, not plain `ninja`.

## `cannot find -lsomething` for a name you expect to be a CMake target

All the CMake files share one directory scope and one global target namespace,
and CMake does not error on an unknown name in `target_link_libraries`: it
passes it to the linker as `-lname`. Check the spelling against the file that
defines the target.

## Undefined SAI symbols

If they appear when linking the SAI switch library, that is expected: it is
deliberately linked allowing unresolved symbols, because the implementation is
supplied by whatever links last. A real problem shows up at final executable
link time, and usually means the SAI implementation was not whole-archived in.

## Everything rebuilt when you only changed a flag

The rebuild hash covers the exact `--extra-cmake-defines` string, the compiler
paths, the scratch path and several environment variables. Changing
whitespace inside the JSON is enough. Keep the string byte-identical between
invocations; see [getdeps-concepts.md](getdeps-concepts.md#the-project-hash-and-what-forces-a-rebuild).

## Recovering without a full rebuild

Pass the same `--scratch-path` you built with, or these act on a different
directory:

```bash
# shell with the build environment: your exact build command, with `build`
# replaced by `debug`, so the SAI env and dependency hashes match
./fboss/oss/scripts/run-getdeps.py <wrapper flags> debug <getdeps flags> fboss

# rebuild one target from the already-configured tree, no dependency checks;
# safe only while no CMake file changes
ninja -C <scratch>/build/fboss -j <N> <target>
```

`getdeps.py clean` is not a recovery step: it rebuilds every dependency. See
[getdeps-concepts.md](getdeps-concepts.md#useful-subcommands).

## Still stuck

Transient failures do happen. Interrupting and retrying, or recreating the
container, resolves a surprising number of them before you start bisecting.
