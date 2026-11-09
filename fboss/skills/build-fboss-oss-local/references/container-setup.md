# The build container

FBOSS builds inside a container. Do not install build dependencies on the
host: the image pins a toolchain the build depends on, and getdeps folds the
resolved compiler paths into its rebuild hash, so a host-versus-container
mismatch silently invalidates everything.

## The images

| File | Base | Status |
|---|---|---|
| `fboss/oss/docker/Dockerfile` | CentOS Stream 9 | The supported build image. |
| `fboss/oss/docker/Dockerfile.debian` | Debian bookworm | Publishes an alternate image. Not a working FBOSS build environment today; see [limits.md](limits.md#platforms). |
| `fboss/oss/docker/prefetch/Dockerfile` | the image above | Adds pre-fetched dependency sources. |

The CentOS image contains **no FBOSS source**. The repository is bind-mounted
at run time. (The Debian image is the opposite: it copies a snapshot of the
repo in at image-build time, which then goes stale.)

## What is in the image, and why

- **Both GCC 12 and Clang/LLVM**, with Clang the default. Clang is preferred
  because `lld` links the agent dramatically faster than GNU `ld` for a modest
  increase in peak memory, and a GCC-built vendor SDK can still be linked by
  Clang, so vendors do not have to rebuild their SDK. Switch inside a running
  container with the provided `use-gcc` / `use-clang` helpers.
- **LLVM installed under `/usr/local/llvm`**, not `/usr/local`, specifically to
  stop GCC picking up LLVM's headers.
- **`binutils` deliberately removed.** It installs real files at `/usr/bin/ar`,
  `/usr/bin/ld` and friends, which `update-alternatives` cannot swap, so the
  GCC/Clang toggle would not work. The build also comments `binutils` out of
  the getdeps manifests for the same reason.
- **Python pinned to 3.12**, matching the getdeps `python` manifest. The base
  image default is too old, and every `-python` dependency variant must use
  the same interpreter consistently.
- **jemalloc.** Non-ASAN FBOSS binaries link jemalloc and compile in an
  allocator configuration; the build hard-fails without it.
- **`sccache`**, wired up automatically by getdeps when present on `PATH`.
- **Extra packages for building vendor ASIC SDKs** inside the same container,
  including a static `libyaml`, a set of Perl modules, and `aspell-en` (a SAI
  code-generation step spellchecks, and the base `aspell` package ships no
  word lists).

The clang version is not hard-coded in the Dockerfile: it is read from the
`mirrors-clang-format` revision in `.pre-commit-config.yaml`. Bumping that
pre-commit hook changes the compiler for everyone.

## Fixed paths

These are conventions in the scripts and, for the PAI SDK, hard-coded in
CMake. Use exactly these destinations.

| Path in container | What |
|---|---|
| `/var/FBOSS/fboss` | the repository (bind-mounted), and the working directory |
| `/var/FBOSS/tmp_bld_dir` | scratch / build output (bind-mounted) |
| `/var/FBOSS/pai_impl` | PHY/PAI SDK, read directly by CMake |
| `/opt/sdk` | NPU SDK, by convention |
| `/var/extras` | optional extra mount |

## Running it

```bash
sudo docker rm -f FBOSS_BUILD_CONTAINER 2>/dev/null
sudo docker run -d -it \
    --network=host \
    --cap-add=CAP_AUDIT_WRITE \
    -e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory \
    -e GIT_CONFIG_VALUE_0='*' \
    --name=FBOSS_BUILD_CONTAINER \
    -v "$PWD":/var/FBOSS/fboss \
    -v ~/fboss_build:/var/FBOSS/tmp_bld_dir:z \
    -v ~/sdk:/opt/sdk:z \
    fboss_image:latest bash

sudo docker exec -it FBOSS_BUILD_CONTAINER bash
```

- **The `GIT_CONFIG_*` variables tell git inside the container to trust every
  directory.** The build hands the scratch directory back to you afterwards
  (see [The uid/gid trap](#uid-gid)), after which root git refuses the
  dependency clones under `repos/` as owned by someone else, and every resumed
  build fails. The build script sets the same thing, so either alone is enough.
- **`--cap-add=CAP_AUDIT_WRITE` is required**, or `sudo` inside the container
  fails. Hand-written invocations forget this constantly.
- The container needs outbound network for dependency downloads.
- On some hosts the default bridge network fails to initialise
  (`netavark: ... can't initialize iptables table 'nat'`); `--network=host`
  avoids it.
- Highly parallel builds can exhaust the container PID limit, surfacing as
  `ninja: fatal: posix_spawn: Operation not permitted`. Raise or disable it
  (`--pids-limit=0`).

## <a id="uid-gid"></a>The uid/gid trap

The Dockerfile defaults to `USERNAME=root / USER_UID=0 / USER_GID=0`, and user
creation is an **image build argument, not a run-time flag**. Unless the image
was built with those overridden, the container runs as real uid 0 and
**everything it writes into your bind mounts is owned by root**, including your
whole scratch directory. You then need `sudo` to `rm`, `tar` or edit your own
build output.

Expect this and hand ownership back after each build, rather than trying to
build a per-user image. CI does the same. The build script in
[first-build.md](first-build.md#5b-put-the-build-in-a-script) does it from
inside the container with `chown -R --reference=<scratch> <scratch>`, which
copies ownership from the scratch directory itself: that is still yours,
because writing inside a bind mount never changes the mount point.

Handing ownership back has one consequence to know about: root git inside the
container then refuses the dependency clones under `repos/`, and resumed
builds fail with "detected dubious ownership". The script and the container's
`GIT_CONFIG_*` environment both tell git to trust them.

To recover a directory that is already root-owned, from the host:
`sudo chown -R "$(id -u):$(id -g)" <your scratch dir>`.

**Do not try to fix this by rebuilding the image as yourself.** It looks
appealing, since `docker-build.py` passes `USERNAME`/`USER_UID`/`USER_GID`
while `build_docker.sh` passes none and forwards no extra arguments. But the
Dockerfile runs `groupadd -g $USER_GID $USERNAME`, and a primary gid of `100`
(`users`) already exists in the base image, so the build fails outright for any
user whose primary group is `users`. Working around it means changing an `ARG`,
which invalidates the layer cache for every subsequent `RUN` and turns a
resumable build into a full multi-gigabyte rebuild.

## `docker-build.py`: the one-shot front end

```bash
./fboss/oss/scripts/docker-build.py \
  --scratch-path ~/fboss_build \
  --target fboss_platform_services \
  --target qsfp_targets \
  --env-var BUILD_SAI_FAKE \
  --local
```

It builds the image and runs the build inside it. This is what every public CI
workflow uses. Things to know:

- `--target` is repeatable, but **each target gets a brand-new container**
  (stopped and removed between targets). State that lives only in the
  container, not in the mounted scratch path, does not survive.
- `--env-var VAR` becomes `VAR=1`. Use only that form: `VAR:VAL` is passed to
  `docker run -e` unchanged, which does not set `VAR` to `VAL`.
- It **rewrites your working tree on every invocation**: it deletes
  `build/deps/github_hashes/` and unpacks the stable-commits snapshot over the
  repository. Hand-edited pins are lost.
- It hard-codes `sudo` for both `docker build` and `docker run`, which is
  awkward on rootless setups.

## Two image names

`build_docker.sh` tags the image `fboss_builder`. `docker-build.py`, the CI
workflows, and the unit-test runner all look for `fboss_image`. They are not
interchangeable, and `docker-build.py` always builds `fboss_image` itself
before building. Pick one, or alias:

```bash
sudo docker tag fboss_builder:latest fboss_image:latest
```

## The prefetch image

`fboss/oss/docker/prefetch/` builds an image with dependency sources already
downloaded, published periodically as a GitHub Actions artifact. The intent is
to spare you the download step.

Treat it as **unverified**: confirm it actually works before relying on it
for an offline build, and see [limits.md](limits.md#offline).

## Is my image stale?

Source and SDKs are bind-mounted, so editing them never needs a rebuild. The
image does bake the toolchain, system packages and Python environment, so it
goes stale when those change, and a stale image fails **late**: the build runs
the whole dependency graph and then dies at FBOSS's configure step on a missing
system library. See
[troubleshooting.md](troubleshooting.md#cmake-configure-fails-on-a-missing-system-library).

Three paths change what is in the image:

```text
fboss/oss/docker/
.pre-commit-config.yaml     # setup_clang.sh reads the LLVM version from it
requirements-dev.txt
```

`.pre-commit-config.yaml` is the non-obvious one: bumping a lint hook changes
the compiler.

Compare your image against them:

```bash
sudo docker inspect -f '{{.Created}}' fboss_image:latest
git -C <repo root> log -1 --format=%ci -- fboss/oss/docker/ \
  .pre-commit-config.yaml requirements-dev.txt
```

Image older than that commit means rebuild. Both `docker-build.py` and
`build_docker.sh` always run `docker build` and lean on Docker's layer cache,
so rebuilding costs little when nothing really changed.

Two caveats:

- The image's system packages also derive from the dependency manifests under
  `build/fbcode_builder/`, so a manifest change can stale the image without
  touching the paths above. Including that directory in the check is safer
  but, for interactive work, rebuilds far more often than is useful.
- **No date check is sufficient**, because the base image tag is unpinned. The
  Dockerfile can be untouched and your image still differ from a fresh build.
  The reliable backstop is the symptom: a dependency that is unexpectedly
  missing at configure time means rebuild the image.

## Base image drift

The base image tag is unpinned, so image builds can break without any change
on your side. This has happened more than once (a new `/usr/bin/python`
appearing; a spellcheck dictionary no longer arriving transitively). If an
image build suddenly fails and nothing in the repository changed, suspect the
base image.
