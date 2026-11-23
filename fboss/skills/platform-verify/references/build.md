# Building a Platform Binary

Platform services build inside the FBOSS container. Full setup is in the build
documentation at https://meta.github.io/fboss/; the runnable snippets live
in `docs/static/code_snips/`.

For the build system itself (container setup, `run-getdeps.py` flags, build
targets, troubleshooting), the `build-fboss-oss-local` skill is the canonical
reference. This file covers what platform verification needs on top.

## Target

The whole platform stack builds from one cmake target:

```text
fboss_platform_services
```

That produces `platform_manager`, `sensor_service`, `fan_service`,
`data_corral_service` and the platform CLI utilities.

## Build

From inside the container, at the repository root:

```bash
./fboss/oss/scripts/run-getdeps.py \
  build \
  --allow-system-packages \
  --build-type MinSizeRel \
  --scratch-path /var/FBOSS/tmp_bld_dir \
  --cmake-target fboss_platform_services \
  fboss
```

To build a single service instead, pass its target name to `--cmake-target`.
Buildable targets appear in the cmake scripts as `add_executable` or
`add_library`.

## Finding the binary

The built binaries land under the scratch path passed to `--scratch-path`.
Locate the one you need before the deploy step:

```bash
find /var/FBOSS/tmp_bld_dir -name platform_manager -type f
```

## Notes

- `--build-type MinSizeRel` keeps the binary small, which matters because it is
  copied to the switch on every iteration. Use `RelWithDebInfo` when you need
  symbols for a crash.
- Building from a local checkout rather than the pinned commit requires
  `--src-dir`; see the build documentation.
