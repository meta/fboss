# Build and Test — Phase 1

Both tests that cover the Phase 1 files are built by the CMake build. Their
targets are declared in `cmake/`, at the root of the repository:

| Target | CMake file | Covers |
|--------|-----------|--------|
| `transceiver_properties_manager_test` | `cmake/QsfpServiceModuleProperties.cmake` | `TransceiverPropertiesDefault.h` parses, and every entry resolves |
| `cmis_test` | `cmake/QsfpServiceModuleTests.cmake` | CMIS module behaviour against the new entry |

## Build

Build each target with `run-getdeps.py`, following
`docs/docs/build/Building_FBOSS_on_containers.md` for the container setup:

```bash
./fboss/oss/scripts/run-getdeps.py build \
  --allow-system-packages \
  --src-dir . \
  --cmake-target transceiver_properties_manager_test \
  fboss
```

Repeat with `--cmake-target cmis_test`. `--src-dir .` is required: without it
getdeps builds the pinned upstream commit, not your working tree, and the tests
pass without ever seeing the new entry. Both binaries land in the CMake build
directory under the getdeps scratch path.

## Run

```bash
./transceiver_properties_manager_test
./cmis_test
```

Interpreting failures:

- **`cmis_test`** — expect no failures. A new entry should not change existing
  CMIS behaviour. If it does, investigate the entry; do **not** edit
  `cmis_test` to accommodate it.
- **`transceiver_properties_manager_test`** — failures here are normally yours
  to fix, and are almost always a missing `mediaInterfaceCode` on one of the
  ports in a speed combination, or an integer that does not match the enum
  value added to `transceiver.thrift`.

## Formatting

Format the changed files with `clang-format` before submitting.
