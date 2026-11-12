# First build: fake SAI, no vendor SDK

This is the shortest path from a clean x86-64 Linux host to running FBOSS
binaries. It needs no vendor SDK, no licence, and no switch. It is the same
path the public CI takes, so if it fails for you it is very likely a real
break rather than a local mistake.

The first run is long: the dependency graph is dozens of projects and most of
them are compiled from source. Later runs reuse the scratch directory and are
much faster.

> One measured data point, so you can scale from it: on a 166-core / 223 GB
> machine with the dependency archives already downloaded, the dependency
> phase took ~14 minutes and `--cmake-target fboss_fake_agent_targets` took a
> further ~16 minutes. A smaller machine, a cold download cache, or building
> everything rather than one target will all be substantially slower. Do not
> treat this as a target; treat it as evidence the build is not hung.

## 0. Prerequisites

- x86-64 Linux with Docker or Podman. There is no aarch64 path.
- Outbound network access (see [limits.md](limits.md#network) for the exact
  set of hosts).
- **At least 100 GB of free disk** for the scratch directory, and ideally
  64 GB+ of RAM. The published guidance is ">50GB"; that is optimistic once
  test binaries and benchmarks are included.

## 1. Get the source

```bash
git clone https://github.com/meta/fboss.git
cd fboss
```

## 2. Get the container image

Build it:

```bash
sudo docker build . -t fboss_image -f fboss/oss/docker/Dockerfile
```

The container runs as root, so it writes root-owned files into your mounts.
The build script hands ownership back afterwards; see
[container-setup.md](container-setup.md#uid-gid).

## 3. Pin the dependency revisions

FBOSS ships a known-good snapshot of every upstream dependency revision. Apply
it before building:

```bash
rm -rf build/deps/github_hashes/
tar xzf fboss/oss/stable_commits/latest_stable_hashes.tar.gz
```

Skipping this builds against upstream `main` for folly, fbthrift and friends,
which is not a combination anyone validates. See
[getdeps-concepts.md](getdeps-concepts.md#pinning).

The tarball also overwrites all of `build/fbcode_builder`, so if you have
edited a manifest or anything else under it, applying it reverts your change;
see [getdeps-concepts.md](getdeps-concepts.md#stable-commits-overwrite).

## 4. Start the container

```bash
mkdir -p ~/fboss_build ~/sdk
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

`--cap-add=CAP_AUDIT_WRITE` is not optional: without it `sudo` inside the
container fails. The repository and scratch mount points are fixed by
convention and by hard-coded paths in the build, so use exactly these
destinations. The `/opt/sdk` mount only matters for a vendor SDK build; put
your SDK artifacts under `~/sdk` on the host.

## 5. Build

```bash
cd /var/FBOSS/fboss
time ./fboss/oss/scripts/run-getdeps.py \
  build \
  --allow-system-packages \
  --build-type MinSizeRel \
  --extra-cmake-defines='{"CMAKE_CXX_STANDARD": "20", "RANGE_V3_TESTS": "OFF", "RANGE_V3_PERF": "OFF"}' \
  --scratch-path /var/FBOSS/tmp_bld_dir \
  --src-dir /var/FBOSS/fboss \
  fboss
```

What each part is doing:

- No `--npu-sai-impl` and no `--phy-sai-impl`, so the wrapper prints
  `No SAI implementation provided, defaulting to fake SAI build
  (BUILD_SAI_FAKE=1)` and builds FBOSS's in-tree SAI implementation.
- `--src-dir /var/FBOSS/fboss` builds **your** tree. Omit it and getdeps builds
  a pinned upstream clone instead, silently. This is the single most common
  first-build mistake.
- `--allow-system-packages` lets getdeps take dependencies from the
  container's installed packages instead of compiling them, as CI does.
- No `--cmake-target`, so this builds everything: every service, CLI and test
  binary. That is only the default to suggest when the user has not said what
  they want. Whatever they ask for wins: a full build, one aggregate target such
  as `fboss_forwarding_stack`, or several (`--cmake-target` is repeatable). A
  single target is substantially less work than a full build. See
  [build-targets.md](build-targets.md).
- `--build-type MinSizeRel` keeps binaries small. Use `RelWithDebInfo` if you
  need usable stack traces; the default when unset is `RelWithDebInfo`.

`docker-build.py` passes a different `--extra-cmake-defines` string, so the
two flows do not share built dependencies even in the same scratch directory;
pick one flow and stay on it. `RANGE_V3_TESTS`/`RANGE_V3_PERF` stop a dependency building its own test and
benchmark suites. `CMAKE_CXX_STANDARD` applies to every CMake dependency, not
only FBOSS, so dropping it changes how the dependencies compile; keep it. And **the exact string feeds the rebuild hash**, so decide
once and keep it byte-identical; changing it later invalidates every cached
dependency. See
[getdeps-concepts.md](getdeps-concepts.md#the-project-hash-and-what-forces-a-rebuild).

### Verifying you built your own tree

```bash
python3 build/fbcode_builder/getdeps.py show-source-dir --src-dir /var/FBOSS/fboss fboss
# -> /var/FBOSS/fboss

python3 build/fbcode_builder/getdeps.py show-source-dir fboss
# -> Using pinned rev <sha> for https://github.com/facebook/fboss.git
#    -> <scratch>/repos/github.com-facebook-fboss.git
```

If the second form is what your build is doing, your edits are not being
compiled.

## 5a. Always keep a build log

Builds are long and fail late, and the interesting error is usually thousands
of lines above where you stopped reading. `run-getdeps.py` does not log to a
file, so capture it yourself, from the first build rather than after the first
failure. The script in [5b](#5b-put-the-build-in-a-script) does all of this;
what matters and why:

- **`tee` to a timestamped file under `<scratch>/logs/`, never overwrite.** The
  run you most want to keep is the one that failed, and the reflex after a
  failure is to fix and re-run immediately. Logs are small: a full cold build
  is on the order of 10 MB.
- **Keep the exit status honest.** Through a pipe, `$?` is `tee`'s status,
  which is always 0, so a failed build looks like a success unless you use
  `pipefail` or capture the status explicitly.
- **A `latest.log` symlink, relative not absolute.** The build runs in the
  container but you read the log on the host, where the path differs, so an
  absolute link dangles.
- **Hand ownership back afterwards.** The container runs as root, so the
  scratch directory ends up root-owned on the host; see
  [container-setup.md](container-setup.md#uid-gid). Doing this is what makes
  root git distrust the dependency clones on the next run, so the script also
  tells git to trust them. The two always go together.

When reporting a failure, send the whole log rather than the last screenful.
A configure failure does sit at the end, but a compile failure may not: the
builder passes `-k 0` to ninja, so other jobs keep running past the first
`FAILED:` and their output lands after it. See
[troubleshooting.md](troubleshooting.md).

## 5b. Put the build in a script

Rather than typing the build by hand, keep it in a script, one per build
configuration, under `<scratch>/scripts/`, named for what it builds
(`build-fake.sh`, `build-<vendor>-<sdk>.sh`). Before writing one, check
whether a script for that configuration already exists and reuse or update it,
so scripts do not pile up as you move between features. The scratch directory
is visible from both the host and the container, and the scripts go away with
it.

This is the canonical shape. It runs inside the container:

```bash
#!/bin/bash
# Fake SAI, full build.
set -euo pipefail
SCRATCH=/var/FBOSS/tmp_bld_dir
LOGDIR=$SCRATCH/logs; mkdir -p "$LOGDIR"
LOG="$LOGDIR/build-$(date +%Y%m%d-%H%M%S).log"

cd /var/FBOSS/fboss
# Required because of the chown at the end: after it, root git here refuses the
# dependency clones under repos/ as owned by another user.
git config --global --replace-all safe.directory '*'
# Pins dependency revisions, and also replaces all of build/fbcode_builder. If
# you are editing a manifest there, extract only build/deps/github_hashes.
# --no-same-owner: as root, tar would restore the archive's owner onto the
# bind-mounted checkout.
rm -rf build/deps/github_hashes/
tar xzf fboss/oss/stable_commits/latest_stable_hashes.tar.gz --no-same-owner

rc=0
./fboss/oss/scripts/run-getdeps.py build --allow-system-packages \
  --build-type MinSizeRel \
  --extra-cmake-defines='{"CMAKE_CXX_STANDARD": "20", "RANGE_V3_TESTS": "OFF", "RANGE_V3_PERF": "OFF"}' \
  --scratch-path "$SCRATCH" --src-dir /var/FBOSS/fboss \
  fboss 2>&1 | tee "$LOG" || rc=$?

ln -sfn "$(basename "$LOG")" "$LOGDIR/latest.log"
chown -R --reference="$SCRATCH" "$SCRATCH"
echo "exit=$rc  log=$LOG"
exit $rc
```

`|| rc=$?` matters: under `set -e` a failed build would otherwise exit before
the log symlink and ownership fix-up run, which is exactly when you need them.
For an SDK build, add the SAI flags before `build` and name the script for the
SDK. Leave out `--num-jobs`, per the Job count check in SKILL.md.

### Waiting on a build

Run the script in the background and read `latest.log` when it exits, rather
than polling or sleeping.

## 6. Find the output

```bash
ls /var/FBOSS/tmp_bld_dir/build/fboss/
```

Binaries land in the build tree, not in an install prefix. A fake-SAI build
produces, among other things:

```text
fboss_hw_agent-sai_impl
fboss_sw_agent
wedge_agent-sai_impl
sai_replayer-fake
```

Note the naming: the **service** binaries keep the `-sai_impl` suffix even in a
fake-SAI build, deliberately, so packaging and service unit files do not change
between fake and real builds. There is no `wedge_agent-fake`. Only *test*
binaries take the `-fake` suffix, as `sai_replayer-fake` shows. See
[build-targets.md](build-targets.md#binary-naming).

## 7. Package it

```bash
./fboss/oss/scripts/package-fboss.py \
  --copy-root-libs \
  --scratch-path /var/FBOSS/tmp_bld_dir \
  --compress
```

This produces a `bin/ lib/ share/` tree (and `fboss_bins.tar.zst`) intended to
be unpacked at `/opt/fboss` on a switch. From a fake-SAI build, the agent in it
cannot program the switch ASIC; platform services, `fsdb` and `fboss2` are
still usable. The no-hardware test step below takes this tarball as input, so
package before testing. It also leaves a large uncompressed
`fboss_bins-<random>` staging tree behind; delete it only after verifying the
tarball.
See [testing.md](testing.md#artifacts) for that, what is in the package, and
how to run it.

## 8. Run the tests that need no hardware

```bash
./fboss/oss/scripts/github_actions/docker-unittest.py <path-to>/fboss_bins.tar.zst
```

This runs every executable in `bin/` whose name ends in `test`/`tests`,
skipping `*hw_test*`, `*integration_test*` and `*hal_test*`. Like
`docker-build.py`, it first re-extracts the stable-commits snapshot over your
checkout, reverting any edits under `build/fbcode_builder`. That is the
honest extent of what you can validate without a switch. Before trusting a
green result, confirm the package contains tests at all; see
[testing.md](testing.md).

## Next steps

- Real vendor SDK: [supplying-your-sdk.md](supplying-your-sdk.md)
- Other services: [build-targets.md](build-targets.md)
- It failed: [troubleshooting.md](troubleshooting.md)
