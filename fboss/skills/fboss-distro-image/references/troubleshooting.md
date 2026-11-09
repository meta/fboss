# Troubleshooting Distro Image Workflows

## First isolate the failing stage

Classify the failure before proposing a fix:

1. Manifest loading or validation
2. Builder-container creation
3. Component download or build
4. Artifact staging
5. KIWI base-image assembly
6. USB, PXE, or ONIE provisioning
7. First boot or service startup

Capture the exact command, exit code, first actionable error, manifest path,
selected components/formats, and host environment. The image-builder log is:

```text
fboss-image/image_builder/logs/build_image_in_container.log
```

Do not assume a cache problem merely because a retry differs. Do not erase the
getdeps scratch tree as a generic fix.

## Manifest failures

- Invalid JSON: run `python3 -m json.tool <manifest>` and fix the reported
  syntax error.
- Missing required field: provide both `distribution_formats` and `kernel`.
- Unrecognized field or component: use the exact supported names; `npu_sai`
  and `phy_sai` replace the ambiguous name `sai`.
- `${ARTIFACT_BASE}` error: set the variable to the artifact root or replace
  the placeholder with a usable public/local URL.
- Script not found: resolve `execute` paths relative to the manifest file.
- Both `download` and `execute`: choose exactly one source for that component.

## Container and build failures

- Docker missing or inaccessible: verify the daemon and the user's permission
  to run privileged containers.
- Builder-image failure: inspect the Dockerfile/package error; the cached image
  is refreshed automatically when its inputs change or its lifetime expires.
- FUSE/Eden filesystem errors: provide `--output-dir` on a real filesystem such
  as `/tmp` for KIWI's working files.
- Package or source fetch failure: record the failing URL and proxy/network
  context; do not silently substitute an unrelated version.
- Component succeeded but artifact was not found: compare `/output` with the
  component's `artifact` pattern and default pattern.
- Forwarding-stack compile failure: inspect `sai_build.env`, headers,
  `libsai_impl.a`, and SAI/SDK version flags as one unit.
- Kmod or first-boot failure: compare the image kernel release with BSP and SAI
  runtime module ABI before changing service configuration.
- KIWI failure: use the complete image-builder log and preserve any partx trace
  printed before the container exits.

## PXE failures

- Confirm the helper container is running and has the expected persistent
  volume and interface.
- Confirm the provisioning host is L2-adjacent to the management port.
- Check that the normalized MAC directory contains every file from the PXE tar.
- For an external DHCPv6 server, use `ipxev6.efi` exactly.
- Verify HTTP port 6969 and TFTP reachability from the management network.
- If the device booted once, remember that the completion marker deliberately
  disables future PXE responses for that MAC; restage the image to re-enable it.
- Before rebooting, verify the current PXE script's installation target matches
  the switch disk layout.

## Reporting a diagnosis

Report the failing stage, evidence, likely cause, one next diagnostic command,
and whether the proposed next action is read-only, expensive, or destructive.
Do not claim success until the requested artifacts exist or the switch passes
post-install verification.
