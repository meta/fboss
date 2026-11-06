# Manifest and Component Integration

## Manifest shape

An image manifest is JSON. `distribution_formats` and `kernel` are required;
unknown top-level keys are rejected. Supported metadata and component keys are
defined by `fboss-image/distro_cli/lib/constants.py`, and validation lives in
`fboss-image/distro_cli/lib/manifest.py`.

`distribution_formats` maps any subset of `usb`, `pxe`, and `onie` to the final
artifact filename. A full build requires at least one supported format.

Each component is one of:

- `download`: fetch a prebuilt artifact. `${ARTIFACT_BASE}` may supply a
  machine-specific artifact root. `file:` paths are resolved relative to the
  manifest; `file:/` paths are absolute.
- `execute`: run a build script inside a fresh builder container. Use either a
  string script path or an array containing the script and arguments. The
  script writes its artifact beneath `/output`.
- Empty object or list: intentionally omit the component.

`download` and `execute` are mutually exclusive. Use `artifact` to override the
filename pattern used to locate an executed build's output.

## Dependency graph

The builder enforces this relationship:

```text
kernel ──> bsps
kernel ──> npu_sai ─┐
kernel ──> phy_sai ─┴──> fboss-forwarding-stack
```

The platform stack is independent and does not require the kernel or vendor
SAI. The forwarding stack consumes both NPU and PHY SAI inputs when present.
`other_dependencies` and `image_build_hooks` are also independent image inputs.

## Artifact contracts

### Kernel

Produce an uncompressed tar archive containing the kernel RPMs in the standard
Red Hat layout. The reference builder creates `kernel-<version>.rpms.tar`.
Kernel development packages are made available to dependent BSP and SAI builds,
but headers/devel/source RPMs are not installed in the final image.

### Other dependencies

Each entry produces or downloads one RPM. It is installed in the final image;
normal runtime dependencies are resolved by the package manager.

### Platform and forwarding stacks

Produce tar archives whose contents are extracted under `/opt/fboss` in the
image. `fboss/oss/scripts/build_fboss_stack.sh platform` builds platform
services without vendor SAI. The `forwarding` mode loads the available SAI
environment and packages the forwarding services.

A manifest may provide multiple forwarding-stack archives, such as separate
Agent, FSDB, QSFP, and CLI bundles. The staging layer preserves artifacts with
duplicate basenames by disambiguating their destination names.

### BSPs

Each BSP artifact is a tar of RPMs. The image copies those RPMs into
`/usr/local/share/local_rpm_repo`; it does not install them during image
assembly. At runtime, `platform_manager` selects and installs the platform's
BSP package, runs dependency processing, and loads the modules.

Treat the BSP's kernel ABI as a hard compatibility boundary. Verify the BSP
RPM was built against the same kernel release packaged in the image.

### NPU SAI and Agent kmods

The complete development artifact contains:

```text
sai_build.env
include/
lib/libsai_impl.a
sai-runtime.rpm
```

`sai_build.env` selects the implementation and SAI/SDK versions used when
building the forwarding stack. The runtime RPM normally supplies SDK kernel
modules and initialization files. Current image assembly also accepts a direct
`lib/modules/<kernel-version>/.../*.ko` tree and runs dependency processing.

Keep SAI headers, static library, runtime package or modules, SDK version, and
kernel ABI from one compatible build. A successful forwarding-stack compile is
not proof that its runtime kmods match the image kernel.

### PHY SAI

The full PHY artifact parallels NPU SAI:

```text
phy_sai_build.env
include/
lib/libphy_sai.a
phy_sai-runtime.rpm
```

The build helper may emit only `phy_sai_build.env` for fake or metadata-only
workflows; vendor integration still needs the headers, library, and runtime
payload required by that implementation.

### Image hooks

`image_build_hooks` may point to these JSON input files:

- `after_pkgs_install`: JSON containing a `packages` list.
- `after_pkgs_execute`: JSON containing an `execute` list of argument arrays.

Use `other_dependencies` for local RPMs and other files that are not available
from the base distribution repositories.

## Review checklist

Before building, confirm:

1. All referenced scripts and local artifacts exist relative to the manifest.
2. Every executed script emits exactly one artifact matching its pattern.
3. Kernel, BSP, NPU SAI, and PHY SAI artifacts agree on kernel and SDK ABI.
4. The forwarding stack is built against the same SAI artifacts shipped at
   runtime.
5. `distribution_formats` names only the formats actually needed.
6. Hook JSON is valid and uses argument arrays rather than shell strings.
