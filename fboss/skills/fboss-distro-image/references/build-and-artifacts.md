# Building FBOSS Distro Images

Commands in this reference are examples to provide to the user. Do not run
them unless the user explicitly requests execution.

## Locate the checkout

The public repository places the image tooling under `fboss-image/` and the
FBOSS source under `fboss/`. Run commands from the repository root unless the
user supplies another working directory.

The CLI requires Python 3.10 or newer and has no external Python dependencies.
Image and component builds run in privileged Docker containers and may download
large source and package sets. Confirm that `python3` and `docker` are usable
before starting a build.

Use the checkout as the source of truth:

```bash
python3 fboss-image/distro_cli/fboss-image build --help
python3 -m json.tool <manifest.json>
```

Read the manifest before choosing a command. If a download URL contains
`${ARTIFACT_BASE}`, require that environment variable to point at the artifact
root before building.

## Full image build

With no component arguments, the CLI builds every component present in the
manifest, assembles the base OS, and creates every format named by
`distribution_formats`:

```bash
./fboss-image/distro_cli/fboss-image build fboss-image/from_source.json
```

On a host where the source tree is on a FUSE filesystem such as EdenFS, place
KIWI's working output on a real filesystem:

```bash
mkdir -p /tmp/fboss-image-output
./fboss-image/distro_cli/fboss-image build \
  fboss-image/from_source.json \
  --output-dir /tmp/fboss-image-output
```

The final artifact filenames come from `distribution_formats`. Relative names
are resolved by the CLI process, so report the paths printed by the build rather
than assuming that every artifact remains under `--output-dir`.

The reference `from_source.json` builds a kernel, the platform stack, and the
forwarding stack; it uses fake SAI and demonstrates image customization hooks.
Platform-specific or release manifests may instead download prebuilt artifacts.

## Component-only build

Pass one or more exact component names after the manifest:

```bash
./fboss-image/distro_cli/fboss-image build <manifest.json> kernel
./fboss-image/distro_cli/fboss-image build \
  <manifest.json> npu_sai fboss-forwarding-stack
```

Valid component keys are:

- `kernel`
- `other_dependencies`
- `fboss-platform-stack`
- `bsps`
- `npu_sai`
- `phy_sai`
- `fboss-forwarding-stack`
- `image_build_hooks`

The dependency walker builds prerequisites declared by the selected component.
For example, `fboss-forwarding-stack` pulls in `npu_sai` and `phy_sai`, which in
turn pull in `kernel` when those components are defined.

A component-only invocation does **not** compose USB, PXE, or ONIE images.
Source-built component artifacts are compressed for reuse; downloaded artifacts
are retained as supplied.

## Expected complete-image outputs

The conventional manifest names are:

| Format | Typical artifact | Purpose |
|---|---|---|
| USB | `fboss-distro-image_usb.iso` | Bootable installer ISO |
| PXE | `fboss-distro-image_pxe.tar` | Files consumed by the PXE helper |
| ONIE | `fboss-distro-image_onie.bin` | Self-extracting ONIE installer |

Verify only formats requested by the manifest:

```bash
ls -lh <artifact-paths>
sha256sum <artifact-paths>
tar -tf <pxe-tar> | head
```

The image records its manifest name and digest, source revision when available,
and staged component hashes in `/etc/build-info`.

## Caching and reproducibility

The `fboss_builder` Docker image is keyed by the Dockerfile and build-system
inputs and expires after 24 hours by default. Change
`FBOSS_BUILDER_CACHE_EXPIRATION_HOURS` only when a different refresh window is
intentional. Do not delete build caches as a first response to a failure;
diagnose the failing stage and inputs first.
