---
id: distro_infra_container
title: Distro Infrastructure Container
description: PXE boot infrastructure container for FBOSS Lab Distribution
keywords:
    - FBOSS
    - OSS
    - PXE
    - provisioning
    - container
    - distro
oncall: fboss_oss
---

# Distro Infrastructure Container

This page documents the FBOSS Distro Infrastructure container. It provides the necessary support for IPv4
and IPv6 PXE boot. Under IPv4 the network must have a DHCP server providing IP addresses.

## Building

Build the container with `./build.sh` from the `fboss-image/distro_infra` directory.

## Usage

Start the container by running the `distro_infra.sh` script which will start the container. This script takes two
arguments:

- **interface**: The interface to attach to. This must have L2 adjacency with the management port of the FBOSS DUTs.
- **persistent directory**: The directory to use for persistent storage, primarily of images to load.

For example:

```bash
mkdir images
./distro_infra.sh --intf vlan1033 --persist-dir images
```

This will start an interactive tool to configure supplying PXE boot options to a given MAC address. Exiting the tool
terminates the container.

The first time a MAC address is given, a `<MAC>` directory under the persistent directory will be created. Into this the
image files to boot must be manually extracted. For example:

```
$ ./distro_infra.sh --intf vlan1033 --persist-dir images
Listening on vlan1033 - 10.250.33.194
dnsmasq: started, version 2.85 DNS disabled
dnsmasq: compile time options: IPv6 GNU-getopt DBus no-UBus no-i18n IDN2 DHCP DHCPv6 no-Lua TFTP no-conntrack ipset auth
cryptohash DNSSEC loop-detect inotify dumpfile
dnsmasq-dhcp: DHCP, proxy on subnet 10.250.33.194
dnsmasq-dhcp: DHCP, sockets bound exclusively to interface vlan1033
dnsmasq-tftp: TFTP root is /distro_infra/persistent secure mode
Enter MAC address (blank to exit): DC-DA-4D-FC-AD-2D
```

Now `images/dc-da-4d-fc-ad-2d` has been created. Into that directory the following files must be copied (or hardlinked
from the also created `images/cache` directory), or extracted from the PXE installer tarball, which contains these
precise names:

```
$ cd images/dc-da-4d-fc-ad-2d
$ tar -xf fboss-distro-image_pxe.tar
$ ls -1
FBOSS-Distro-Image.x86_64-1.0.config.bootoptions
FBOSS-Distro-Image.x86_64-1.0.initrd
FBOSS-Distro-Image.x86_64-1.0.kernel
FBOSS-Distro-Image.x86_64-1.0.sha256
FBOSS-Distro-Image.x86_64-1.0.xz
pxeboot.FBOSS-Distro-Image.x86_64-1.0.kernel
pxeboot.FBOSS-Distro-Image.x86_64-1.0.initrd
```

Other files will be generated and populated automatically.

The DUT can then be rebooted and PXE boot will start. Once PXE boot has completed, the Distro Infrastructure container
will stop serving PXE boot to that MAC. To serve PXE boot again, re-enter the MAC address at the menu. It is not
necessary to recopy the image files.

To terminate the script and container, enter a blank MAC address at the prompt: `Enter MAC address (blank to exit):`.

## IPv6 and DHCPv6

By default the Distro Infrastructure container will serve DHCPv6 with the necessary PXE boot options. However this will
conflict with a pre-existing DHCPv6 server. The `--nodhcpv6` option will prevent the Distro Infrastructure container
from serving DHCPv6.

To support PXE booting with an external DHCPv6 server, that server must send the `bootfile-url` option with the value
`tftp://[<v6_ip>]/ipxev6.efi`. Where `<v6_ip>` is the global IPv6 address of the interface supplied in the argument to
`distro_infra.sh`.

For example, dnsmasq can be configured like so:

```
dhcp-option=option6:bootfile-url,tftp://[<v6_ip>]/ipxev6.efi
```

And with KEA the following configuration snippet would be added:

```json
"option-data": [
    {
        "name": "bootfile-url",
        "data": "tftp://[<v6_ip>]/ipxv6.efi"
    }
]
```
