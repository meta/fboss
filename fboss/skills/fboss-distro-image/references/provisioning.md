# USB, PXE, and ONIE Provisioning

Default to advisory mode: provide provisioning commands without running them.
Execution requires an explicit user request plus the confirmations below.

Provisioning changes disks and may make a switch unavailable. First identify
the exact artifact, switch, management MAC, boot method, and installation disk.
Do not perform a write, reboot, or reprovision until the user explicitly
confirms those targets and acknowledges data loss.

## USB

Use the manifest's `.iso` output with a trusted USB imaging tool for the user's
operating system. FBOSS does not publish a canonical raw-write command, so do
not invent one or choose a block device for the user. If the user requests a
command-line writer, enumerate removable devices read-only, have the user name
the exact unmounted target, explain that it will be overwritten, and wait for
explicit confirmation.

## PXE

PXE uses the `.tar` output and a provisioning host with L2 adjacency to the
switch management port. The helper container uses host networking and
`NET_ADMIN` and must remain running while the switch boots.

From the repository root, build and start the helper:

```bash
cd fboss-image/distro_infra
./build.sh
mkdir -p images
./distro_infra.sh --intf <interface> --persist-dir images
```

Add `--nodhcpv6` when another DHCPv6 server is authoritative. In that case the
external server must advertise:

```text
bootfile-url = tftp://[<server-ipv6>]/ipxev6.efi
```

With the container running, stage a built PXE tarball for one management MAC:

```bash
./fboss-image/distro_cli/fboss-image \
  device <management-mac> image <fboss-distro-image_pxe.tar>
```

The command normalizes the MAC, extracts the tar into the persistent directory,
and enables PXE for that device. The PXE script currently installs to
`/dev/nvme0n1`; verify that this is the intended switch disk before rebooting.

PXE enablement is one-shot. Fetching the completion marker removes the device's
boot files and DHCP match so the next reboot does not reinstall the switch. To
serve the image again, repeat the `device ... image` command.

The PXE tar must contain the kernel, initrd, compressed image, checksum,
boot-options file, and the two `pxeboot.*` files expected by `distro_infra`.

## ONIE

The builder can produce a self-extracting `.bin` installer. It verifies and
unpacks its payload, selects a platform configuration, partitions the target,
installs the root filesystem, and configures the bootloader.

FBOSS documentation does not define one canonical command for installing this
artifact, and ONIE support is still described as in development. Use the
switch/vendor ONIE procedure and inspect the generated installer before use.
Do not pass the `.bin` to `fboss-image device ... image`; that path rejects
non-tar inputs and only configures PXE.

## Verification

After provisioning, verify the installed version and `/etc/build-info`, then
check that platform services and the forwarding stack start in dependency
order. Keep the manifest, artifact hashes, switch identity, and provisioning
method in the result report.
