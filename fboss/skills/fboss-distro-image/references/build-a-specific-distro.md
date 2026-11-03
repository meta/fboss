# Build a Specific FBOSS Distro

Use this workflow when the user names a switch or platform rather than handing
you a finished manifest. The useful default output is not general advice or an
automatically started build: it is a resolved component plan, a proposed valid
manifest, and exact copy-paste commands.

## 1. Choose the closest build path

Commands in this reference are advisory examples. Present them without running
them unless the user has explicitly entered execution mode.

Inspect the checkout before asking the user to choose:

| Starting point | Use when | Next action |
|---|---|---|
| `fboss-image/from_source.json` | A source-built development image with fake SAI is acceptable | Provide its build command or use it as the basis for another source manifest |
| `fboss-image/manifests/<platform>.json` | A checked-in platform manifest matches the target and its artifact paths are available | Provide artifact checks and the build command |
| `fboss-image/manifests/generic.json` | The user already has separately built platform, BSP, SAI, Agent, FSDB, QSFP, and CLI artifacts | Provide staging and generic-image build commands |
| New manifest | No existing manifest matches the target | Start with the closest manifest and replace every platform-specific input explicitly |

Do not infer hardware support from a similar platform name. If no checked-in
manifest matches, report that fact and ask for the platform's BSP and SAI
artifact sources.

## 2. Resolve the build profile

Fill this worksheet from the manifest and filesystem. Ask one grouped question
for the cells that remain unresolved.

| Input | Required decision | Evidence to collect |
|---|---|---|
| Target | Switch/platform and intended use | Exact platform identifier |
| Kernel | Version plus `execute` script or artifact | Kernel RPM tar and release |
| BSPs | Zero or more BSP RPM tarballs | Supported platform and kernel ABI |
| NPU SAI | Fake SAI or vendor artifact | SAI/SDK version, headers/library, runtime RPM or kmods |
| PHY SAI | Empty or vendor artifact | PHY library/runtime requirements |
| Platform stack | Source build or archive | Archive extracted under `/opt/fboss` |
| Forwarding stack | Source build or one or more archives | Built against the selected SAI artifacts |
| Extra dependencies/hooks | RPMs and hook JSON files | Existing paths and intended image changes |
| Formats | Any subset of USB, PXE, ONIE | Installation method the user will use |
| Output | Artifact names and optional real-filesystem work directory | Writable paths and sufficient space |

Treat the kernel release as the compatibility spine. The BSP and SAI kmods
must match it, and the forwarding stack must use the same SAI headers and
libraries that supply the runtime payload.

## 3. Produce the manifest

Prefer modifying a copy over changing a checked-in reference manifest. If the
user did not choose a path, propose
`fboss-image/manifests/<platform>-local.json` before writing it.

Every non-empty component needs a real `download` or `execute` value. Preserve
empty `{}` or `[]` entries only when the component is intentionally absent.
Resolve relative paths from the manifest's directory, not the shell's current
directory. Use target-specific output names when multiple images may coexist.

In advisory mode, review what is directly readable and provide commands for
the user to perform the remaining checks. Do not run the checks yourself.

Before presenting the manifest, account for these checks:

1. It is valid JSON and contains `distribution_formats` and `kernel`.
2. Every key is one of the supported component names.
3. Every local download and build script exists.
4. Each executed build has the correct expected artifact pattern.
5. The kernel, BSP, SAI, and forwarding-stack compatibility claims have
   evidence. Mark unknown compatibility as a blocker rather than an assumption.

## 4. Concrete build paths

### Existing platform manifest

For `minipack3.json`, report the actual prerequisites rather than saying only
that a manifest exists:

- `/tmp/minipack3_image_staging/kernel-6.11.1.rpms.tar`
- `/tmp/minipack3_image_staging/fboss_bins.tar.zst`
- `/tmp/minipack3_image_staging/fboss_bsp_kmods.tar`
- `/tmp/minipack3_image_staging/sai-devel.tar`

The checked-in manifest has an empty forwarding-stack entry. Surface that fact:
it can assemble the declared image, but it will not add forwarding services
unless the user supplies compatible forwarding-stack artifacts or changes the
manifest to build them.

After the user verifies those inputs, provide this build command:

```bash
python3 -m json.tool fboss-image/manifests/minipack3.json
./fboss-image/distro_cli/fboss-image build \
  fboss-image/manifests/minipack3.json \
  --output-dir /tmp/fboss-image-minipack3
```

The checked-in platform manifests use local staging paths. Confirm those files
exist before starting; the manifest name alone does not provide the artifacts.

### Generic prebuilt-artifact image

When the user has all required prebuilt archives, stage them with the public
helper:

```bash
./fboss-image/manifests/stage_artifacts.sh \
  --platform <platform_fboss_bins.tar.zst> \
  --bsp <fboss_bsp_kmods.tar> \
  --sai <fboss_sai_kmods.tar> \
  --agent <agent_fboss_bins.tar.zst> \
  --fsdb <fsdb_fboss_bins.tar.zst> \
  --qsfp <qsfp_fboss_bins.tar.zst> \
  --fboss2 <fboss2_fboss_bins.tar.zst>

./fboss-image/distro_cli/fboss-image build \
  fboss-image/manifests/generic.json \
  --output-dir /tmp/fboss-image-output
```

### Source development image

For an image that intentionally uses fake SAI:

```bash
./fboss-image/distro_cli/fboss-image build \
  fboss-image/from_source.json \
  --output-dir /tmp/fboss-image-from-source
```

Say explicitly that fake SAI is suitable for development and image-pipeline
validation, not for programming a real switching ASIC.

## 5. Present and execute

Before an expensive build, show:

```text
Target: <platform>
Manifest: <path>
Components: <source/download choice for each component>
Compatibility: <verified facts and unresolved blockers>
Formats: <usb/pxe/onie>
Command: <exact command>
Expected outputs: <exact filenames>
```
In advisory mode, stop after presenting the commands. If the user explicitly
requests execution and confirms an expensive build, run one build at a time and
preserve the first actionable failure. On success, verify each requested
artifact exists, record its size and SHA-256 digest, and state which USB, PXE,
or ONIE provisioning workflow consumes it.
