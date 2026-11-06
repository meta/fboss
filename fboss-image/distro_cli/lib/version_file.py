# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

"""The one file that says what a distro image was assembled from.

An image is assembled from components built elsewhere. Each already states
something about itself, so this collects those statements and writes a single
record, letting an image be identified and rebuilt.

Reading and writing are separated. A schema per manifest component declares what
to read from that component and which version each reading provides; one writer
turns whatever was collected into the record. Supporting a new component, or a
new version from an existing one, is a schema entry -- no reader, and no part of
the writer, has to change.

Every component is described the same way:

    source       what the manifest asked for, and any claim its artifact
                 contradicts
    artifact     the staged name and its checksum
    provenance   what the artifact reports about itself, verbatim

A component that can report nothing carries no provenance, which is where a gap
shows -- on the component itself, not in a list apart from it.
"""

import hashlib
import json
import logging
import os
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

from distro_cli.lib.artifact_versions import list_members, READERS

logger: logging.Logger = logging.getLogger(__name__)

FILE_NAME = "fboss-distro-version.json"
SCHEMA = 1

_READ_CHUNK_BYTES = 1024 * 1024


# Extractors: pull one version out of what a reader returned. Each is named in
# a schema below and none knows which component it ran for.


# Version-and-release without the architecture: the granularity that decides
# whether a module loads. Dropping the release would make two builds of one
# upstream version indistinguishable.
_KERNEL_RELEASE: re.Pattern = re.compile(r"(\d+\.\d+\.\d+-[0-9][\w.]*?\.el\d+)")
_KERNEL_VERSION: re.Pattern = re.compile(r"(\d+\.\d+\.\d+)")


def kernel_release(text: str, strict: bool = False) -> str | None:
    """Return the kernel release named anywhere in a string.

    Normalises an RPM dependency, a vermagic and a package name to one form so
    they can be compared. Pass strict where the string may name no kernel: a
    package carries its own version too, and reading that as a kernel would
    invent a disagreement out of a string that never mentioned one.
    """
    found = _KERNEL_RELEASE.search(text)
    if not found and not strict:
        found = _KERNEL_VERSION.search(text)
    return found.group(1) if found else None


def _entries(provenance: dict[str, object], reader: str) -> list[dict[str, object]]:
    """Return the records one reader found, ignoring anything malformed.

    A reader returns what it read from an artifact, so the shape is only a
    convention -- a record that does not match is dropped, not fatal.
    """
    found = provenance.get(reader)
    if not isinstance(found, list):
        return []
    return [entry for entry in found if isinstance(entry, dict)]


def _strings(entry: dict[str, object], field: str) -> list[str]:
    """Return one field of a record as strings, however it was written."""
    value = entry.get(field)
    if isinstance(value, list):
        return [str(item) for item in value]
    return [] if value is None else [str(value)]


def kernel_from_packages(provenance: dict[str, object]) -> str | None:
    """Return the kernel release an artifact's packages name.

    A kmod RPM that declares no kernel dependency still carries it in its own
    name, the only place the BSP states it.
    """
    for package in _entries(provenance, "packages"):
        for requirement in _strings(package, "kernel_requires"):
            release = kernel_release(requirement)
            if release:
                return release

    for package in _entries(provenance, "packages"):
        for nevra in _strings(package, "nevra"):
            release = kernel_release(nevra, strict=True)
            if release:
                return release
    return None


def kernel_from_modules(provenance: dict[str, object]) -> str | None:
    """Return the kernel release an artifact's modules were built for.

    vermagic leads with the release and continues with build flags.
    """
    for module in _entries(provenance, "modules"):
        for vermagic in _strings(module, "vermagic"):
            release = kernel_release(vermagic.split()[0])
            if release:
                return release
    return None


def bsp_from_packages(provenance: dict[str, object]) -> str | None:
    """Return the package release an artifact carries.

    NEVRA is NAME-VERSION-RELEASE.ARCH and the name contains dashes, so it is
    split from the right.
    """
    for package in _entries(provenance, "packages"):
        for nevra in _strings(package, "nevra"):
            parts = nevra.rsplit("-", 2)
            if len(parts) == 3:
                return f"{parts[1]}-{parts[2].rsplit('.', 1)[0]}"
    return None


def _from_sdk(provenance: dict[str, object], key: str) -> str | None:
    """Return the one SDK release every binary in an artifact agrees on.

    Metadata is per binary, so taking the first would depend on dict order.
    """
    binaries = provenance.get("sdk")
    if not isinstance(binaries, dict):
        return None

    found: dict[str, list[str]] = {}
    for name, entry in binaries.items():
        if not isinstance(entry, dict):
            continue
        versions = entry.get("sdkVersion")
        if not isinstance(versions, dict):
            continue
        value = versions.get(key)
        if value:
            found.setdefault(str(value), []).append(str(name))

    if len(found) > 1:
        # No value is reported: every candidate is equally supported, so naming
        # one would record a version this artifact does not actually have.
        logger.warning(
            f"binaries in one artifact disagree on {key}, so none is recorded: "
            + "; ".join(
                f"{v} ({', '.join(sorted(n))})" for v, n in sorted(found.items())
            )
        )
        return None
    return next(iter(found), None)


def sai_from_sdk(provenance: dict[str, object]) -> str | None:
    """Return the vendor SAI release an artifact was linked against."""
    return _from_sdk(provenance, "saiSdk")


def native_sdk_from_sdk(provenance: dict[str, object]) -> str | None:
    """Return the native ASIC SDK release an artifact was linked against."""
    return _from_sdk(provenance, "asicSdk")


# The schema: what to read per component, and what each reading provides.


@dataclass(frozen=True)
class Provides:
    """A version a component can report, and how to pull it out."""

    version: str
    extract: Callable[[dict[str, object]], str | None]


@dataclass(frozen=True)
class Claims:
    """A version a component's locator states, and how to pull it out.

    Publishing paths carry versions, so the locator is a claim the artifact
    itself can corroborate or contradict.
    """

    version: str
    pattern: re.Pattern


@dataclass(frozen=True)
class ModuleSchema:
    """What to read from one manifest component, and what it yields."""

    readers: tuple[str, ...] = ()
    provides: tuple[Provides, ...] = ()
    claims: tuple[Claims, ...] = field(default_factory=tuple)


_KERNEL_IN_PATH: re.Pattern = re.compile(r"/(\d+\.\d+\.\d+)/")
_SAI_IN_PATH: re.Pattern = re.compile(r"/agent/[^/]+/([^/]+)/")

# Keyed by manifest component. A component absent here is still recorded with
# its locator and checksum; it simply reports nothing about itself yet.
MODULE_SCHEMAS: dict[str, ModuleSchema] = {
    "kernel": ModuleSchema(
        readers=("packages", "build_info"),
        provides=(Provides("kernel", kernel_from_packages),),
        claims=(Claims("kernel", _KERNEL_IN_PATH),),
    ),
    "bsps": ModuleSchema(
        readers=("packages",),
        provides=(
            Provides("bsp", bsp_from_packages),
            Provides("kernel", kernel_from_packages),
        ),
        claims=(Claims("kernel", _KERNEL_IN_PATH),),
    ),
    "npu_sai": ModuleSchema(
        readers=("modules",),
        provides=(Provides("kernel", kernel_from_modules),),
        claims=(Claims("kernel", _KERNEL_IN_PATH),),
    ),
    "phy_sai": ModuleSchema(
        readers=("modules",),
        provides=(Provides("kernel", kernel_from_modules),),
    ),
    "fboss-forwarding-stack": ModuleSchema(
        readers=("sdk", "build_info"),
        provides=(
            Provides("sai", sai_from_sdk),
            Provides("native_sdk", native_sdk_from_sdk),
        ),
        claims=(Claims("sai", _SAI_IN_PATH),),
    ),
    "fboss-platform-stack": ModuleSchema(readers=("build_info",)),
}

_NO_SCHEMA = ModuleSchema()


def schema_for(component: str) -> ModuleSchema:
    """Return the schema for a component, ignoring any list index."""
    return MODULE_SCHEMAS.get(component.split("[")[0], _NO_SCHEMA)


# --------------------------------------------------------------------------
# Reading, per the schema.
# --------------------------------------------------------------------------


def read_artifact(component: str, artifact: Path) -> dict[str, object]:
    """Read what a component's schema says this artifact reports."""
    schema = schema_for(component)
    if not schema.readers:
        return {}

    members = list_members(artifact)
    if not members:
        return {}

    found: dict[str, object] = {}
    for name in schema.readers:
        reader = READERS.get(name)
        if reader is None:
            continue
        result = reader(artifact, members)
        if result:
            found[name] = result
    return found


def versions_reported_by(
    component: str, provenance: dict[str, object]
) -> dict[str, str]:
    """Return the versions a component's artifact reports about itself."""
    reported = {}
    for provides in schema_for(component).provides:
        value = provides.extract(provenance)
        if value:
            reported[provides.version] = value
    return reported


def versions_claimed_by(component: str, locator: str) -> dict[str, str]:
    """Return the versions a component's locator states."""
    claimed = {}
    for claims in schema_for(component).claims:
        found = claims.pattern.search(locator)
        if found:
            claimed[claims.version] = found.group(1)
    return claimed


def _corroborates(reported: str, claimed: str) -> bool:
    """Whether what an artifact reports bears out what its locator claims.

    A path is less precise -- 6.11.1 against 6.11.1-1.fboss.el9 -- so an exact
    comparison would contradict everything.
    """
    if reported == claimed:
        return True
    return any(reported.startswith(claimed + sep) for sep in ("-", ".", "_"))


def contradictions(
    component: str, locator: str, provenance: dict[str, object]
) -> list[str]:
    """Return the locator's claims this artifact contradicts.

    A claim with nothing to check it against is left alone, not guessed at.
    """
    claimed = versions_claimed_by(component, locator)
    reported = versions_reported_by(component, provenance)
    return sorted(
        f"{name}={value}"
        for name, value in claimed.items()
        if name in reported and not _corroborates(reported[name], value)
    )


# Writing: one path for every component, driven by what was collected.


def checksum(path: Path) -> str:
    """Return the sha256 of a file as "sha256:<hex>"."""
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(_READ_CHUNK_BYTES):
            digest.update(chunk)
    return f"sha256:{digest.hexdigest()}"


def locator_of(entry: object) -> str | None:
    """Return what the manifest asked for, for one artifact entry."""
    if not isinstance(entry, dict):
        return None
    locator = entry.get("download") or entry.get("execute")
    if isinstance(locator, list):
        locator = locator[0] if locator else None
    return locator if isinstance(locator, str) else None


def entries_for(manifest_data: dict, component: str) -> list[object]:
    """Return a component's manifest entries, in order to pair with artifacts."""
    data = manifest_data.get(component)
    return data if isinstance(data, list) else [data]


def label_for(component: str, entry: object, index: int, count: int) -> str:
    """Return how one artifact of a component is recorded.

    Several artifacts are qualified by the name the manifest gave each, falling
    back to the position, which says nothing about which artifact it is.
    """
    if count == 1:
        return component

    name = entry.get("name") if isinstance(entry, dict) else None
    return f"{component}[{name if isinstance(name, str) and name else index}]"


def describe_artifact(
    label: str, artifact: Path, locator: str | None
) -> dict[str, object]:
    """Describe one staged artifact: asked for, delivered, and reported."""
    provenance = read_artifact(label, artifact)

    record: dict[str, object] = {
        "artifact": {"name": artifact.name, "sha256": checksum(artifact)}
    }

    if locator:
        source: dict[str, object] = {"locator": locator}
        contradicted = contradictions(label, locator, provenance)
        if contradicted:
            source["unsupported_claims"] = contradicted
            logger.warning(
                f"{label}: locator claims {', '.join(contradicted)}, which "
                f"{artifact.name} does not report"
            )
        record["source"] = source

    if provenance:
        record["provenance"] = provenance

    return record


def describe_components(
    manifest_data: dict, component_artifacts: dict[str, object]
) -> dict[str, object]:
    """Describe every staged artifact, keyed by manifest component.

    A component naming several artifacts is recorded as "<component>[<name>]",
    so each keeps its own locator, checksum and provenance.
    """
    records: dict[str, object] = {}

    for component, staged in sorted(component_artifacts.items()):
        if staged is None:
            continue

        artifacts: list[object] = staged if isinstance(staged, list) else [staged]
        entries = entries_for(manifest_data, component)

        # Artifacts are staged in manifest order, so entry i describes artifact
        # i -- but only while none were dropped. An element that built nothing is
        # skipped, which would shift every later artifact onto the wrong
        # locator and invent or hide a contradiction. Pair only when the counts
        # agree, and otherwise record the artifacts with no claim to check.
        paired = len(artifacts) == len(entries)
        if not paired:
            logger.warning(
                f"{component}: {len(artifacts)} artifact(s) staged for "
                f"{len(entries)} manifest entry(ies), so which locator describes "
                "which artifact is unknown; their claims cannot be checked"
            )

        for index, path in enumerate(artifacts):
            if not isinstance(path, (str, Path)):
                continue
            entry = entries[index] if paired else None
            label = label_for(component, entry, index, len(artifacts))
            records[label] = describe_artifact(label, Path(path), locator_of(entry))

    return records


# Versions identifying the whole image, so every component reporting one must
# agree. A per-artifact version is not here: two BSPs legitimately sit at
# different releases, and requiring them to match would fail a valid build.
IMAGE_WIDE_VERSIONS = ("kernel", "sai", "native_sdk")


def resolve_versions(components: dict[str, object]) -> tuple[dict[str, str], list[str]]:
    """Resolve the versions identifying an image, and the disagreements found.

    A disagreement is returned rather than raised so the record is still
    written: it is the evidence needed to diagnose the mismatch. The caller
    reports it once the build is done. A disputed version is omitted, because
    no single value is the image's once its components disagree.
    """
    found: dict[str, str] = {}
    reported_by: dict[str, str] = {}
    conflicts: list[str] = []
    disputed: set[str] = set()

    for component, record in components.items():
        if not isinstance(record, dict):
            continue
        provenance = record.get("provenance")
        if not isinstance(provenance, dict):
            continue
        for name, value in versions_reported_by(component, provenance).items():
            if name in IMAGE_WIDE_VERSIONS and name in found and found[name] != value:
                conflicts.append(
                    f"components disagree on {name}: "
                    f"{reported_by[name]} says {found[name]}, "
                    f"{component} says {value}"
                )
                disputed.add(name)
            found.setdefault(name, value)
            reported_by.setdefault(name, component)

    # A disputed version is dropped rather than resolved to whichever component
    # happened to be visited first: with the components disagreeing, no value
    # here would be the image's. The conflict names both, and each component
    # keeps its own reading in its provenance.
    for name in disputed:
        del found[name]

    return found, conflicts


def build(
    *,
    manifest_path: Path,
    manifest_data: dict,
    component_artifacts: dict[str, object],
    source_revision: str,
) -> tuple[dict[str, object], list[str]]:
    """Assemble the record describing one image, and any disagreements in it.

    The disagreements are returned rather than written into the record so the
    caller can report them once the build is done, where they will be read.
    """
    components = describe_components(manifest_data, component_artifacts)
    versions, conflicts = resolve_versions(components)

    record: dict[str, object] = {
        "schema": SCHEMA,
        "image": {
            "manifest": manifest_path.name,
            "manifest_sha256": checksum(manifest_path),
            "source_revision": source_revision,
        },
        "versions": versions,
        "components": components,
    }
    return record, conflicts


def _write_atomically(body: str, destination: Path) -> None:
    """Write a file that is either absent or complete, never truncated.

    A crash mid-write would otherwise ship a record that parses as a shorter,
    wrong one.
    """
    destination.parent.mkdir(parents=True, exist_ok=True)
    scratch = destination.with_name(destination.name + ".tmp")
    scratch.write_text(body)
    os.replace(scratch, destination)


def write(record: dict[str, object], destinations: list[Path]) -> None:
    """Write the same bytes to each destination."""
    body = json.dumps(record, indent=2, sort_keys=True) + "\n"
    for destination in destinations:
        _write_atomically(body, destination)
        logger.info(f"Wrote {destination}")
