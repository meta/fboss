# FSDB CGO Wrapper

A C-callable interface to FBOSS's FSDB pub/sub library, intended for use from
Go binaries via `cgo`. Lets external consumers (e.g. switch vendors integrating
their Go-based control planes) subscribe to FSDB state and stats paths without
linking the entire Meta C++ runtime themselves.

## What you get

The vendor handoff is a **single static archive plus one C header**:

- `libfsdb_cgo_bundle.a` — fat archive with the wrapper plus every transitive
  C++ dep (Folly, Thrift, fb303, glog, gflags, fmt, double-conversion, boost,
  libevent, zlib/zstd/lz4, libsodium, …) merged into one file. Compiled with
  `-fvisibility=hidden`, so the only globally visible symbols are the
  wrapper's entry points; everything else is local to the archive.
- `fsdb_cgo_api.h` — pure C header (no C++ includes) declaring the 20 entry
  points. The Go cgo preamble `#include`s this directly.

Dynamic dependencies the vendor's hosts must provide:
- `libssl.so` / `libcrypto.so` (OpenSSL — Meta's FBOSS OSS Docker baseline is
  OpenSSL 1.1.1+)
- `libstdc++.so` (GCC ≥ 9.x recommended)
- `libpthread.so`, `libdl.so`, `libm.so`, `libc.so` (system libraries)

## Building

Set up the FBOSS OSS container and dependencies per the FBOSS OSS build
instructions: https://meta.github.io/fboss/ . Then build just the
`fsdb_cgo_pub_sub_wrapper` target (rather than all of FBOSS) and bundle it:

```bash
# 1. Build only the wrapper target (and its deps), scoped with --cmake-target.
#    The build/scratch dir inside the container is /var/FBOSS/tmp_bld_dir.
./fboss/oss/scripts/run-getdeps.py build \
    --cmake-target fsdb_cgo_pub_sub_wrapper \
    --scratch-path /var/FBOSS/tmp_bld_dir \
    --src-dir fboss_src fboss

# 2. Bundle every transitive .a into a single fat archive. Pass the scratch dir.
./fboss/fsdb/client/cgo/oss/build_fat_archive.sh /var/FBOSS/tmp_bld_dir ./out

# 3. Ship out/libfsdb_cgo_bundle.a, out/fsdb_cgo_api.h, and out/runtime_libs/
#    to the vendor.
```

Scoping to `--cmake-target fsdb_cgo_pub_sub_wrapper` keeps the bundle to the
wrapper plus its dependency closure instead of every FBOSS component.

The packaging script:
1. Walks the build dir and finds every `*.a`, excluding `libfsdb_cgo_bundle.a`
   (self-ingest from prior runs) and `libgflags*.a` (linked dynamically via
   `libglog.so` to avoid runtime double-registration).
2. Merges all archives into `libfsdb_cgo_bundle.a` via `ar -M addlib` (MRI
   script mode). Preserves duplicate-named members across archives — required
   because `libthrift_service_client.a`, `libfmt.a`, and others contain
   multiple `.o` members sharing a basename, which extract-then-rebundle
   approaches silently drop.
3. Verifies the 20 entry-point symbols are present (`nm | awk`); exits
   non-zero on any miss.
4. Copies `fsdb_cgo_api.h` next to the archive.
5. Writes a `MANIFEST.txt` with the bundle's size, SHA-256, member count,
   and build provenance.

The script does **not** run `objcopy --localize-hidden` on the bundle:
third-party deps like libfmt are built with `-fvisibility=hidden` too, so
bundle-wide localization would convert symbols vendors need at link time
(e.g. fmt's `vformat`) from external to local. The wrapper TU's own
`-fvisibility=hidden` already keeps internal symbols hidden in its `.o`
files; that visibility carries through `ar -M addlib` unchanged.

## Linking from Go

Sample `cgo` preamble (see `fsdb_cgo_example.go` for a complete CLI client):

```go
/*
#cgo CFLAGS: -I${SRCDIR}
#cgo LDFLAGS: -L${SRCDIR} -lfsdb_cgo_bundle \
              -lstdc++ -lm -lpthread -ldl -lssl -lcrypto -l:libz.so.1 \
              -lgflags -lglog -l:libunwind.so.8 \
              -l:libdouble-conversion.so.3 -l:libbz2.so.1 -l:libsnappy.so.1 \
              -l:liblz4.so.1 -l:liblzma.so.5 -lxxhash -lzstd

#include <stdlib.h>
#include "fsdb_cgo_api.h"
*/
import "C"
```

Place `fsdb_cgo_api.h` and `libfsdb_cgo_bundle.a` next to the Go source file
(or adjust `-I` / `-L` to point elsewhere).

### Why all those `-l` flags?

The bundle is a closed *static* link unit for FBOSS code, but a few system
shared libs are still needed at link time: zlib, double-conversion, the
compression family (bz2/snappy/lz4/lzma/zstd), xxhash, libunwind, gflags, glog.
gflags is intentionally excluded from the bundle because libglog.so links it
dynamically — bundling it as static causes a runtime "flag defined more than
once" double-registration error.

### Toolchain requirements

The bundle is compiled with **clang** and the libstdc++ new ABI (cxx11). Link
with a compatible toolchain — clang or recent gcc — set via the cgo
environment:

```bash
CC=clang CXX=clang++ go build -o my_client ./my_client.go
```

If glog reports `libunwind.so.1: cannot open shared object file` at runtime,
provide a soname shim (`ln -sf /usr/lib64/libunwind.so.8 ./runtime/libunwind.so.1`)
and run with `LD_LIBRARY_PATH=./runtime ./my_client`. Distros differ on the
libunwind soname.

## API surface

Two API shapes coexist:

**Typed APIs** — flat C structs, no Thrift decoding required on the Go side.
Designed for the most common consumer paths.

| Function | Purpose |
|---|---|
| `SubscribeToPortMaps(host, port)` | Subscribe to `agent.switchState.portMaps`. `host` NULL/"" → localhost; pass a host string for remote subscriptions. |
| `WaitForStateUpdates` | Receive `FsdbPortStateUpdate` (port_name, port_id, oper_state). |
| `GetPortSnapshot(host, port, out, max_count)` | Synchronous one-shot GET of all ports (no subscription) as `FsdbPortStateUpdate[]`, for reconciliation. Returns count or -1. |
| `FreeStateUpdates` | Release borrowed-pointer buffer. |

**Path APIs** — for arbitrary FSDB paths. Returns raw OperState bytes plus
the `OperProtocol` enum value; the consumer decodes using their own Thrift
bindings.

| Function | Purpose |
|---|---|
| `SubscribeToStats(path_tokens, num_tokens, host, port)` | Subscribe to any stats path (e.g. `["agent"]`, `["qsfp_service","stats"]`). `host` NULL/"" → localhost. |
| `WaitForStatsUpdates` | Receive `FsdbStatsUpdate` (key, raw bytes, length, protocol). |
| `FreeStatsUpdates` | Release borrowed-pointer buffer. |
| `SubscribeToState(path_tokens, num_tokens, host, port)` | Subscribe to any state path (e.g. `["agent","switchState","interfaceMap"]`). `host` NULL/"" → localhost. |
| `WaitForStatePathUpdates` | Receive `FsdbStatePathUpdate` (key, raw bytes, length, protocol). |
| `FreeStatePathUpdates` | Release borrowed-pointer buffer. |

**Lifecycle** (shared by both APIs):

| Function | Purpose |
|---|---|
| `FsdbCgoAbiVersion()` | Returns the ABI version baked into the library. Most consumers don't need this — pass `FSDB_CGO_ABI_VERSION` to `FsdbInit` and let it do the check. |
| `FsdbInit(consumer_abi_version)` | One-time process-wide Folly bootstrap. Idempotent. Pass `FSDB_CGO_ABI_VERSION`; the library compares it against its own baked-in version. Returns 0 on success, 1 on caught exception, **2 on ABI mismatch (do not call any other entry point)**. |
| `CreateFsdbWrapper(client_id)` | Allocate one wrapper per FSDB connection. |
| `ShutdownFsdbWrapper(handle)` | Wake any goroutine parked inside `WaitFor*` within ~100 ms. |
| `DestroyFsdbWrapper(handle)` | Free the wrapper. Call AFTER `Shutdown` and AFTER all waiters have returned. |
| `HasStateSubscription` / `HasStatsSubscription` / `HasStatePathSubscription` | Status checks. |
| `GetClientId(handle)` | Returns the client id passed to `CreateFsdbWrapper`. |

## API contract

- **ABI version is checked inside `FsdbInit`.** Pass `FSDB_CGO_ABI_VERSION`
  (the macro value from the header you compiled against). The library compares
  it against its own baked-in version and returns 2 on mismatch — struct
  layouts and function signatures may have changed between versions and silent
  skew leads to memory corruption. Abort on return code 2; do not call any
  other entry point. The Go example wraps this in `Init()`.
- **Init once per process.** `FsdbInit` bootstraps Folly singletons and
  gflags. Guarded by `folly::call_once`; subsequent calls are no-ops.
- **One wrapper per FSDB connection.** Don't share a `FsdbWrapperHandle` for
  multiple distinct subscribers — create separate wrappers.
- **One consumer thread per subscription.** Every `WaitFor*` call (typed or
  path) blocks the calling thread until data arrives. Each subscription's
  queue is single-producer-single-consumer; calling the same `WaitFor*` from
  multiple goroutines on the same subscription is undefined behavior.
- **Borrowed pointers.** `FsdbPortStateUpdate.port_name`, `FsdbStatsUpdate.data`,
  and `FsdbStatePathUpdate.data` are pointers into wrapper-owned buffers. They
  remain valid until the next `WaitFor*` call on the same subscription, until
  `Free*` is called for that subscription, or until the wrapper is destroyed —
  whichever happens first. Copy them to Go memory (`C.GoString`, `C.GoBytes`)
  before any of those events.
- **Shutdown then destroy.** The teardown sequence is:
  1. `ShutdownFsdbWrapper(handle)` — flips an atomic flag.
  2. Wait for every goroutine that called `WaitFor*` to return (each will see
     the flag and return 0 within ~100 ms).
  3. `DestroyFsdbWrapper(handle)` — frees the wrapper.
  Skipping step 2 and going straight to `Destroy` while a goroutine is parked
  in `WaitFor*` is a use-after-free.
- **Surplus stays queued.** If `WaitFor*` returns `count == max_count`, the
  unconsumed updates remain in the queue and are returned on the next call.
  No data loss, no truncation.

## Known limitations (v2)

- **Snapshot is ports-only.** `GetPortSnapshot` covers the synchronous
  `getOperState` use case for `portMaps`; there is no generic one-shot GET for
  arbitrary stats/state paths yet (subscribe to those instead).
- **Subscription failures are logged, not returned.** If
  `SubscribeToPortMaps`/`SubscribeToStats`/`SubscribeToState`
  fails (network error, FSDB rejected the subscription, etc.), the failure is
  written to `XLOG(ERR)` but the API call returns void. Consumers should poll
  `HasStateSubscription` / `HasStatsSubscription` / `HasStatePathSubscription`
  after subscribing to confirm success.
- **Remote host.** Each subscribe function takes a `host` + `port`: pass a host
  string + explicit port to reach a remote FSDB; a NULL/empty host connects to
  `::1`. The OSS bundle connects **plaintext** by construction (the encrypted/CPE
  path is non-OSS and not linked into the bundle), so the remote FSDB must accept
  plaintext.
- **Shutdown latency ≈ 100 ms.** `WaitFor*` polls the shutdown flag every
  100 ms (instead of using a wakeup primitive) to keep the SPSC contract on
  the internal queues. A `folly::Baton`-based wakeup is on the roadmap if
  shorter shutdown latency becomes a requirement.

## Files in this directory

- `fsdb_cgo_api.h` — vendor-facing C header (single source of truth for the ABI).
- `FsdbCgoPubSubWrapper.h` / `.cpp` — C++ implementation.
- `BUCK` — Buck target with `-fvisibility=hidden` compiler flags.
- `fsdb_cgo_example.go` — reference Go CGO client with `watchports` /
  `watchstats` / `watchboth` subcommands. Demonstrates the proper
  `Shutdown` → wait → `Close` teardown.
- `oss/build_fat_archive.sh` — packaging script for vendor handoff.
- `test/` — unit and integration tests (run via `buck2 test`).
