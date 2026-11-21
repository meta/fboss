#!/usr/bin/env python3
"""Image-time, fail-closed adaptation of kiwi-boxed-plugin 0.2.58.

Upstream does not expose virtiofsd options in its config schema. Its namespace
sandbox needs SYS_ADMIN and file handles need DAC_READ_SEARCH, neither of which
we grant. Use virtiofsd's supported chroot sandbox and never use file handles.
Upstream also forwards a fixed host port (10022, all interfaces) to the guest's
SSH; only the unused sshfs backend needs it, and under host networking it would
expose the guest and collide between concurrent builds, so drop it.
Its virtiofsd sockets are fixed /tmp paths, which would collide between the
concurrent PXE/USB and ONIE builds; place them in each build's box cache.
The exact upstream source is checked before modification; do not apply at run
 time or silently adapt a different plugin release.

Inspected PyPI sdist sha256:
c5d3f4195b3456e210da63cc23ef88f42a5b771643e47ef79b8f6b067e662abe
"""

import hashlib
import importlib.metadata
from pathlib import Path

DEFAULTS_SHA256 = "66df3c088a3c3b2ecac39f0fa08fc5426b0e255f6c55b5b84075d7b085cb6b3f"

REWRITES = (
    (
        "'--sandbox', 'namespace',",
        "'--sandbox', 'chroot',\n                    '--inode-file-handles=never',",
    ),
    (
        "user,model=virtio,hostfwd=tcp::{Defaults.box_ssh_port_forwarded_to_host}-:22",
        "user,model=virtio",
    ),
    (
        "'--socket-path=/tmp/vhostqemu_{0}'.format(index),",
        "'--socket-path={0}/vhostqemu_{1}'.format("
        "os.environ['KIWI_BOXED_CACHE_DIR'], index),",
    ),
    (
        "'socket,id=char{0},path=/tmp/vhostqemu_{0}'.format(index),",
        "'socket,id=char{0},path={1}/vhostqemu_{0}'.format("
        "index, os.environ['KIWI_BOXED_CACHE_DIR']),",
    ),
)


def rewrite_options(source: str) -> str:
    result = source
    for old, new in REWRITES:
        if result.count(old) != 1:
            raise RuntimeError(f"Unexpected boxed plugin source: {old}")
        result = result.replace(old, new)
    compile(result, "defaults.py", "exec")
    return result


def configure() -> None:
    if importlib.metadata.version("kiwi-boxed-plugin") != "0.2.58":
        raise RuntimeError("Unreviewed kiwi-boxed-plugin version")
    if importlib.metadata.version("kiwi") != "10.3.10":
        raise RuntimeError(
            "Unreviewed KIWI launcher version; boxed plugin requires 10.3.10"
        )
    distribution = importlib.metadata.distribution("kiwi-boxed-plugin")
    defaults = Path(distribution.locate_file("kiwi_boxed_plugin/defaults.py"))
    source = defaults.read_bytes()
    if hashlib.sha256(source).hexdigest() != DEFAULTS_SHA256:
        raise RuntimeError("Unreviewed boxed plugin source; refusing to patch")
    defaults.write_text(rewrite_options(source.decode()))


if __name__ == "__main__":
    configure()
