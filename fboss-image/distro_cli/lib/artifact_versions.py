# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

"""Readers for the version evidence a component artifact carries.

Each reader pulls one kind of evidence out of an artifact and knows nothing
about which component it came from or where the result is written. The
per-module schema decides which readers apply, so supporting a new component is
a schema entry rather than a change here.

Every reader is best effort. A host without rpm or modinfo, an artifact that is
not the archive expected, or metadata that will not parse costs that one reader
its result and nothing else.
"""

import json
import logging
import os
import re
import subprocess
import tempfile
from pathlib import Path
from typing import Callable

logger: logging.Logger = logging.getLogger(__name__)

# Written by the CMake POST_BUILD hook in fboss/github/functions.cmake, one
# entry per SDK-linked binary.
SDK_METADATA_MEMBER = "share/npu_sdk_metadata.json"
BUILD_INFO_MEMBER = "share/fboss-component-version.json"
KERNEL_BUILD_INFO_MEMBER = "share/kernel-version.json"
BSP_BUILD_INFO_MEMBER = "share/bsp-version.json"

_MODULE_SUFFIXES = (".ko", ".ko.xz", ".ko.gz")

# An artifact can hold hundreds of members; enough are read to identify it.
_MAX_MEMBERS = 64

# A kmod package requires the kernel release it was built for, and separately
# every kernel ABI symbol it links against -- "kernel(__const_udelay) = 0x...",
# of which there are hundreds. Only the release requirement identifies a kernel,
# so match "kernel = " and "kernel-<name> = " and never the symbol form.
KERNEL_REQUIREMENT: re.Pattern = re.compile(r"^kernel(-[a-z0-9-]+)?\s*=\s*\S")


def _warn_if_truncated(artifact: Path, kind: str, found: int) -> None:
    """Report evidence this reader will not look at.

    Members are read up to a cap, so the one bearing the identifying version
    can sort past it. Silently reading fewer would let a mismatch through.
    """
    if found > _MAX_MEMBERS:
        logger.warning(
            f"{artifact.name} carries {found} {kind}; reading the first "
            f"{_MAX_MEMBERS}, so later ones report no versions"
        )


def _run(command: list[str]) -> str | None:
    """Run a command, returning its output or None if it could not be run."""
    try:
        result = subprocess.run(command, capture_output=True, text=True, check=False)
    except OSError as error:
        logger.debug(f"{command[0]} unavailable: {error}")
        return None
    if result.returncode != 0:
        logger.debug(f"{' '.join(command)} failed: {result.stderr.strip()}")
        return None
    return result.stdout


def list_members(artifact: Path) -> list[str]:
    """List an archive's members, as stored."""
    output = _run(["tar", "-tf", str(artifact)])
    if output is None:
        # Distinguishable from an empty archive only here: returning [] would
        # record no evidence and let the build pass as though the artifact
        # genuinely carried none.
        logger.warning(f"Could not list {artifact.name}; it will report no versions")
        return []

    # Split only on newlines: a member path may contain spaces, and splitting
    # on any whitespace would fragment it into names that match nothing.
    return [line for line in output.splitlines() if line]


def _contained(member: str, dest: Path) -> bool:
    """Whether extracting a member stays under dest.

    Member names come from the archive, so an artifact could name "../" or an
    absolute path and write outside the workspace when extracted.
    """
    if member.startswith("/"):
        return False
    resolved = os.path.normpath(os.path.join(str(dest), member))
    return resolved == str(dest) or resolved.startswith(str(dest) + os.sep)


def _extract(artifact: Path, members: list[str], dest: Path) -> list[Path]:
    """Extract members into dest, returning those that arrived."""
    safe = [member for member in members if _contained(member, dest)]
    for member in set(members) - set(safe):
        logger.warning(
            f"{artifact.name}: refusing to extract {member!r} outside {dest}"
        )
    if not safe:
        return []

    _run(["tar", "-xf", str(artifact), "-C", str(dest), *safe])
    return [dest / member for member in safe if (dest / member).is_file()]


def read_sdk(artifact: Path, members: list[str]) -> dict[str, object]:
    """Read the SDK versions an artifact's binaries were linked against."""
    # Matched without the "./" that published artifacts store, but kept with it:
    # tar only accepts a member under its stored name.
    member = next(
        (name for name in members if name.removeprefix("./") == SDK_METADATA_MEMBER),
        None,
    )
    if member is None:
        return {}

    # -O writes the member to stdout, so the rest of the archive is never unpacked.
    content = _run(["tar", "-xOf", str(artifact), member])
    if content is None:
        # A failed read would otherwise look like an artifact that ships no SDK
        # metadata, and the SAI mismatch this evidence exists to catch would
        # never be reported.
        logger.warning(f"Could not read {member} from {artifact.name}")
        return {}

    if not content:
        return {}

    try:
        metadata = json.loads(content)
    except json.JSONDecodeError as error:
        logger.debug(f"{artifact.name}: {SDK_METADATA_MEMBER} will not parse: {error}")
        return {}

    return metadata if isinstance(metadata, dict) else {}


def read_build_info(artifact: Path, members: list[str]) -> dict[str, object]:
    """Read packaged executable or component producer build records."""
    candidates = [
        name
        for name in members
        if name.removeprefix("./")
        in (BUILD_INFO_MEMBER, KERNEL_BUILD_INFO_MEMBER, BSP_BUILD_INFO_MEMBER)
    ]
    if not candidates:
        return {}
    if len(candidates) != 1:
        logger.warning(f"{artifact.name}: ambiguous build info declarations")
        return {}
    member = candidates[0]
    content = _run(["tar", "-xOf", str(artifact), member])
    if content is None:
        logger.warning(f"Could not read {member} from {artifact.name}")
        return {}
    try:
        declaration = json.loads(content)
    except json.JSONDecodeError:
        logger.warning(f"{artifact.name}: {member} will not parse")
        return {}
    if (
        not isinstance(declaration, dict)
        or declaration.get("declaration_schema") != 1
        or not any(
            isinstance(declaration.get(key), dict) for key in ("binaries", "build")
        )
        or any(
            key in declaration and not isinstance(declaration[key], dict)
            for key in ("binaries", "build")
        )
    ):
        logger.warning(f"{artifact.name}: unsupported build info declaration")
        return {}
    return declaration


def read_packages(artifact: Path, members: list[str]) -> list[dict[str, object]]:
    """Read the identity of each RPM an artifact carries.

    An RPM states its own name, version, release and architecture, and a kmod
    package additionally states the kernel release it was built for.
    """
    # Source RPMs ship in no image and report the build architecture rather
    # than their own, so recording them would describe something absent.
    rpms = sorted(
        member
        for member in members
        if member.endswith(".rpm") and not member.endswith(".src.rpm")
    )
    _warn_if_truncated(artifact, "RPMs", len(rpms))
    if not rpms:
        return []

    packages: list[dict[str, object]] = []
    with tempfile.TemporaryDirectory() as workspace:
        for path in _extract(artifact, rpms[:_MAX_MEMBERS], Path(workspace)):
            entry: dict[str, object] = {"file": path.name}

            nevra = _run(
                [
                    "rpm",
                    "-qp",
                    "--qf",
                    "%{NAME}-%{VERSION}-%{RELEASE}.%{ARCH}",
                    str(path),
                ]
            )
            if nevra and nevra.strip():
                entry["nevra"] = nevra.strip()

            requires = _run(["rpm", "-qp", "--requires", str(path)])
            if requires:
                kernel = sorted(
                    line.strip()
                    for line in requires.splitlines()
                    if KERNEL_REQUIREMENT.match(line.strip())
                )
                if kernel:
                    entry["kernel_requires"] = kernel

            if len(entry) > 1:
                packages.append(entry)

    return packages


def read_modules(artifact: Path, members: list[str]) -> list[dict[str, object]]:
    """Read the identity of each kernel module an artifact carries.

    A module states the kernel it was built for in its vermagic and the content
    of its own source in its srcversion, which together identify it more
    precisely than any version string.
    """
    modules = sorted(member for member in members if member.endswith(_MODULE_SUFFIXES))
    if not modules:
        return []
    _warn_if_truncated(artifact, "modules", len(modules))

    described: list[dict[str, object]] = []
    with tempfile.TemporaryDirectory() as workspace:
        for path in _extract(artifact, modules[:_MAX_MEMBERS], Path(workspace)):
            entry: dict[str, object] = {"name": path.name}
            for field in ("vermagic", "srcversion"):
                value = _run(["modinfo", "-F", field, str(path)])
                if value and value.strip():
                    entry[field] = value.strip()
            if len(entry) > 1:
                described.append(entry)

    return described


# What a reader is called in a schema, and the function that implements it.
# Typed as callable so a schema can invoke one without a suppression; the return
# shape is the loose part, which the extractors narrow.
Reader = Callable[[Path, list[str]], object]

READERS: dict[str, Reader] = {
    "sdk": read_sdk,
    "build_info": read_build_info,
    "packages": read_packages,
    "modules": read_modules,
}
