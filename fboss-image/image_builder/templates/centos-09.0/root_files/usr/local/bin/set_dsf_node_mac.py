#!/usr/bin/env python3
"""Fill the local DSF node MAC in agent.conf from the chassis EEPROM.

On a VOQ platform the agent requires a nodeMac on its own dsfNodes entry and
has no fallback for it, while the default configs are per platform, not per
unit. This writes the EEPROM "Switch ASIC MAC Base" into every local
dsfNodes entry that has no nodeMac, and into the matching switchMac when that
is unset too. Entries that already carry a MAC are left alone. Configs with
no such entry (NPU platforms) are not touched and the EEPROM is not read.

Exit status: 0 when nothing needed doing or the MAC was written, 1 when a MAC
was needed but the EEPROM has no usable value or the config can't be written.
"""

import argparse
import json
import os
import re
import sys
import tempfile

EEPROM_FIELD = "Switch ASIC MAC Base"
MAC_RE = re.compile(r"^[0-9a-f]{2}(:[0-9a-f]{2}){5}$")


def log(msg: str) -> None:
    print(f"[set_dsf_node_mac] {msg}", file=sys.stderr)


def read_eeprom_mac(path: str) -> str:
    """Return the validated ASIC base MAC, or raise ValueError saying why not."""
    try:
        with open(path) as f:
            lines = f.read().splitlines()
    except OSError as e:
        raise ValueError(f"cannot read the chassis EEPROM {path}: {e}") from e
    prefix = f"{EEPROM_FIELD}:"
    values = [line[len(prefix) :].strip() for line in lines if line.startswith(prefix)]
    if not values or not values[0]:
        raise ValueError(f'"{EEPROM_FIELD}" is not set in {path}')
    mac = values[0].lower()
    if not MAC_RE.match(mac):
        raise ValueError(f'"{EEPROM_FIELD}" in {path} is not a MAC address: {mac!r}')
    if mac in ("00:00:00:00:00:00", "ff:ff:ff:ff:ff:ff") or int(mac[:2], 16) & 1:
        raise ValueError(f'"{EEPROM_FIELD}" in {path} is not a unicast MAC: {mac}')
    return mac


def write_atomically(path: str, config: dict) -> None:
    """Replace path with config so a crash leaves either the old or new file."""
    st = os.stat(path)
    directory = os.path.dirname(os.path.abspath(path))
    fd, tmp = tempfile.mkstemp(dir=directory, prefix=".agent.conf.")
    try:
        with os.fdopen(fd, "w") as f:
            json.dump(config, f, indent=2)
            f.write("\n")
            f.flush()
            os.fsync(f.fileno())
        os.chmod(tmp, st.st_mode & 0o7777)
        os.chown(tmp, st.st_uid, st.st_gid)
        os.replace(tmp, path)
    except BaseException:
        os.unlink(tmp)
        raise
    dir_fd = os.open(directory, os.O_RDONLY)
    try:
        os.fsync(dir_fd)
    finally:
        os.close(dir_fd)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="/etc/coop/agent.conf")
    parser.add_argument(
        "--eeprom", default="/run/devmap/eeproms/CHASSIS_EEPROM_PARSED"
    )
    args = parser.parse_args()

    with open(args.config) as f:
        config = json.load(f)
    sw = config.get("sw", {})
    switch_infos = sw.get("switchSettings", {}).get("switchIdToSwitchInfo", {})
    # Only local nodes: remote nodes must come with their MAC in the config.
    nodes = {
        switch_id: node
        for switch_id, node in (sw.get("dsfNodes") or {}).items()
        if switch_id in switch_infos and not node.get("nodeMac")
    }
    if not nodes:
        return 0
    names = ", ".join(node.get("name", switch_id) for switch_id, node in nodes.items())

    try:
        mac = read_eeprom_mac(args.eeprom)
    except ValueError as e:
        log(f"ERROR: {e}; nodeMac left unset for {names}, the agent will not start")
        return 1

    for switch_id, node in nodes.items():
        node["nodeMac"] = mac
        # Keep the MAC the agent programs consistent with the node MAC.
        if not switch_infos[switch_id].get("switchMac"):
            switch_infos[switch_id]["switchMac"] = mac
    try:
        write_atomically(args.config, config)
    except OSError as e:
        log(f"ERROR: cannot write {args.config}: {e}")
        return 1
    log(f"Set nodeMac {mac} on {names} in {args.config}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
