# getdeps: what it is and why the build works this way

`build/fbcode_builder/getdeps.py` is a dependency fetcher and build driver
shared by several Meta open-source C++ projects (folly, fbthrift, wangle,
fizz, fb303 and FBOSS among them). FBOSS does not have a bespoke build
bootstrapper; it uses this one, and `fboss/oss/scripts/run-getdeps.py` is a
thin wrapper on top.

Understanding four concepts explains almost every surprising behaviour:
manifests, fetchers, the scratch layout, and the project hash.

## Manifests

Every project getdeps knows about is one INI file under
`build/fbcode_builder/manifests/<name>`. The filename must equal the
`[manifest] name` field. A manifest says how to fetch the source, which
builder to run, and what it depends on. An abridged `manifests/fboss`, with
illustrative values (read the file for current ones):

```ini
[manifest]
name = fboss
fbsource_path = fbcode/fboss
shipit_project = fboss

[git]
repo_url = https://github.com/facebook/fboss.git

[build.os=linux]
builder = cmake
job_weight_mib = 3072

[build.not(os=linux)]
builder = nop

[dependencies]
folly-python
fb303
...
```

### Conditional sections

Any of the interesting sections can carry a boolean suffix:

```ini
[dependencies.os=linux]
[rpms.all(distro_family=rhel,distro_vers=9)]
[debs.not(all(distro=ubuntu,any(distro_vers="18.04",distro_vers="20.04")))]
```

The grammar is `KEY=VALUE`, `not(...)`, `all(...)`, `any(...)`. Values
containing a dot must be quoted. The variables available are `os`, `distro`,
`distro_vers`, `distro_family`, and a few others describing the build context.

This suffix mechanism is how one manifest text serves every platform. It is
also where platform support quietly ends: a project whose *only* package
section is `[rpms.all(distro_family=rhel,distro_vers=9)]` has **no fetcher at
all** on a Debian host, and the build fails with

```text
KeyError: 'project gcc12 has no fetcher configuration or system packages
matching {distro=debian, distro_family=debian, distro_vers=12, ...}'
```

Note that `list-deps` still succeeds in that situation, because it resolves
the graph without constructing fetchers. The failure only appears at fetch or
build time. See [limits.md](limits.md#platforms).

### Scalar vs collection lookups

A subtlety worth knowing when editing manifests: for single-value lookups the
**first matching section in file order wins**, and an unconditional
`[section]` short-circuits the search entirely (if it exists but lacks the
key, you get the default, and later conditional blocks are never consulted).
For collections (`[cmake.defines]`, `[dependencies]`, ...) **all** matching
sections merge in file order, with later entries winning.

## Fetchers

getdeps picks a fetcher per project, in this order:

1. A local directory, if `--src-dir` names this project.
2. A source mirror derived from a monorepo checkout, when the manifest
   declares `fbsource_path` and you are inside such a checkout.
3. Preinstalled, if every variable in `[preinstalled.env]` is set.
4. System packages, if the packages are actually installed.

Steps 3 and 4 are only considered for a project that has no `[git]` or
`[download]` URL, or for any project when `--allow-system-packages` is given.
5. A vendored directory, if `--vendor-dir` is set.
6. `git clone`, if `[git] repo_url` is set.
7. A downloaded archive, if `[download] url` is set.

If none apply, you get the `KeyError` above. Two consequences worth
internalising:

- A project with no URL can *only* come from the host, so the `KeyError`
  for one means its packages are not installed: a stale image, a build run
  outside the container, or an unsupported distribution.
  `--allow-system-packages` lets dependencies that do have a URL come from
  installed packages instead of being built, which the container relies on.
- <a id="pinning"></a>**`--src-dir` overrides everything.** Without it, the
  `[git]` fetcher wins for FBOSS itself, and getdeps clones
  `github.com/facebook/fboss.git` at the revision recorded in
  `build/deps/github_hashes/facebook/fboss-rev.txt`. Your local edits are not
  compiled and nothing warns you. Confirm with
  `getdeps.py show-source-dir [--src-dir .] fboss`.

## Pinned revisions and stable commits

`build/deps/github_hashes/<org>/<repo>-rev.txt` files contain a line of the
form `Subproject commit <sha>`. getdeps reads them to pin each dependency.

FBOSS additionally ships `fboss/oss/stable_commits/latest_stable_hashes.tar.gz`,
a snapshot of that whole directory (plus the `build/fbcode_builder/` tree)
that has been built and integration-tested together. Applying it is the
difference between "a combination someone validated" and "whatever upstream
happens to be today":

```bash
rm -rf build/deps/github_hashes/
tar xzf fboss/oss/stable_commits/latest_stable_hashes.tar.gz
```

It pins the *software dependency graph only*. It does not pin an ASIC SDK.

<a id="stable-commits-overwrite"></a>**It also replaces your getdeps.** The
tarball carries the whole `build/fbcode_builder` tree, not just the revision
pins: every manifest, the getdeps Python and the CMake helpers. Applying it
silently reverts any local change you made there, for example a dependency you
added to `manifests/fboss`. If you are changing anything under
`build/fbcode_builder`, either extract only the pins, keeping your tree:

```bash
rm -rf build/deps/github_hashes/
tar xzf fboss/oss/stable_commits/latest_stable_hashes.tar.gz build/deps/github_hashes
```

or skip the step and build against upstream heads, which is what the
maintainers' CI does when that tree changes. Extracting only the pins is the
lighter option, but newer manifests against older pinned revisions can
mismatch, so if the build fails in a dependency, try skipping instead.

## <a id="scratch-layout"></a>The scratch directory

Everything getdeps produces lives under one scratch root, chosen from
`--scratch-path` if given, otherwise a generated temporary path. Pass it
explicitly; several helpers assume `/var/FBOSS/tmp_bld_dir` when they cannot
see the real value, and a mismatch means caches silently are not reused.

```text
<scratch>/
  downloads/          fetched archives
  extracted/          unpacked archives
  repos/              git clones
  build/<project>/    build trees   <- your binaries are in build/fboss/
  installed/<project>/ install trees
```

There is no `--install-dir`. "Where did my binary go" is answered by
`<scratch>/build/fboss/`, or by `getdeps.py show-build-dir fboss`.

Third-party projects get a **configuration hash** appended to their directory
name (`googletest-w2kCjNev0...`); first-party ones do not. That is why you see
both `build/fboss` and `build/boost-trBxCcQ...` side by side.

## The project hash, and what forces a rebuild

The hash folded into third-party directory names is a SHA-256 over: the
install and scratch paths, the OS and distro, the resolved feature set, the
compiler paths, `CXXFLAGS`/`CPPFLAGS`/`LDFLAGS`/`CC`/`CXX`, the
`--extra-cmake-defines` string **verbatim**, the manifest contents, any
patch file, and recursively every dependency's hash.

Practical consequences:

- Changing whitespace inside `--extra-cmake-defines` changes the hash and
  rebuilds the world. Keep the string byte-identical between invocations.
- Moving your scratch directory invalidates everything.
- Changing compilers invalidates everything.
- Conversely, an environment variable that is *not* in that list does **not**
  invalidate anything. This is why a project can declare
  `[depends.environment]` to force a variable into the hash.

`<install-dir>/.built-by-getdeps` records the hash of what was built. `test`
refuses to run when it is missing.

## Useful subcommands

Pass the same `--scratch-path` you built with to anything that inspects or
modifies the scratch directory, or it acts on a different, generated one.

```bash
getdeps.py show-host-type                      # linux-centos_stream-9
getdeps.py list-deps fboss                     # topologically sorted graph
getdeps.py show-source-dir --src-dir . fboss
getdeps.py show-build-dir --scratch-path <scratch> fboss
getdeps.py show-inst-dir --recursive fboss
getdeps.py query-paths --recursive fboss       # shell-assignable paths
getdeps.py validate-manifest manifests/<name>
getdeps.py install-system-deps --recursive fboss
run-getdeps.py <wrapper flags> debug <getdeps flags> fboss   # shell with the build env
run-getdeps.py <wrapper flags> env <getdeps flags> fboss     # print that env
```

Dependency directories are named `<name>-<hash>`, and the hash covers
`CXXFLAGS`/`LDFLAGS`, which `run-getdeps.py` sets. Raw `getdeps.py` therefore
computes different paths and a different environment for anything that
involves a dependency directory (`show-inst-dir`, `query-paths`, `debug`,
`env`). Run those through `run-getdeps.py`: take your exact build command and
replace `build` with the subcommand.

`getdeps.py clean` deletes `build/`, `installed/`, `extracted/` and `shipit/`,
keeping only `downloads/` and `repos/`, so every dependency recompiles. It is
a last resort; ask the user first.

## Iterating without getdeps

Once getdeps has configured the FBOSS build directory, you can rebuild directly
with ninja, which skips all dependency assessment and is the fastest
edit-compile loop:

```bash
ninja -C <scratch>/build/fboss -j <N> <target>
```

This is only safe while no CMake file changes. The SAI selection and spec
version reach CMake as environment variables set by `run-getdeps.py`, not as
cached options, so if ninja re-runs CMake (for example after you add a `.cpp`
to a `cmake/*.cmake` file) from a plain shell, it reconfigures with no SAI
implementation and the default spec version. After a CMake change, re-run the
wrapper, or run ninja from `run-getdeps.py debug` with your build flags.

## Parallelism and memory

getdeps picks `-j` as `min(cpu_count, available_RAM_MiB / job_weight_mib)`.
The FBOSS manifest raises `job_weight_mib` above the getdeps default because
FBOSS translation units are unusually large; read the current value from
`build/fbcode_builder/manifests/fboss`. getdeps additionally applies a virtual
address-space limit (`RLIMIT_AS`) of `job_weight_mib * 10` MiB to the
`cmake --build` step and everything it spawns (for example, 3072 gives 30 GiB), so a runaway compile gets
`std::bad_alloc` rather than inviting the OOM killer to shoot something else.
The cap is per process and applies regardless of `--num-jobs`, so an unusually
heavy translation unit can hit it with plenty of RAM free; see
[troubleshooting.md](troubleshooting.md).

Overriding `--num-jobs` upward opts out of the RAM-based sizing, not the cap.

## Why not just use CMake directly?

You can, via ninja in the configured build directory, once the dependencies
exist. getdeps exists
to produce those dependencies reproducibly: dozens of projects, pinned, built with a
matching toolchain and flags. The alternative is asking every contributor and
vendor to assemble a matching folly/fbthrift/fizz/wangle stack by hand, which
is not tractable.
