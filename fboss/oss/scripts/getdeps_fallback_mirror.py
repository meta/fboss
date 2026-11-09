#!/usr/bin/env python3
# Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
# All Rights Reserved

"""
Pre-seed getdeps' download directory for packages hosted on GNU mirrors.

getdeps pins a single download URL per manifest and retries only that URL, so a
mirror that drops an old release (404) or refuses the connection (proxy 503)
fails the whole build. This script scans the manifests read-only for archives
pinned to a GNU mirror, fetches each from the first working mirror, and writes
it into the download directory under the name getdeps expects. getdeps then
finds a hash-verified file already present and skips its own download entirely.

Never fails the build: any package this cannot prepare is left for getdeps to
fetch normally.
"""

from __future__ import annotations

import configparser
import hashlib
import http.client
import os
import shutil
import subprocess
import sys
import time
import urllib.request
from urllib.parse import urlparse

DOWNLOAD_TIMEOUT_SECONDS = 120
DOWNLOAD_CHUNK_BYTES = 64 * 1024
MAX_DOWNLOAD_BYTES = 100 * 1024 * 1024

# Total wall-clock this may spend reaching out. Every mirror hanging until it
# times out would otherwise cost packages x mirrors x DOWNLOAD_TIMEOUT_SECONDS,
# and a build that has to wait tens of minutes to learn the network is down is
# worse off than one that just lets getdeps fail. A deadline bounds that
# directly, without having to infer from failures whether a route out exists.
NETWORK_BUDGET_SECONDS = 300

# mirrors.kernel.org is listed first: it is a full GNU archive that retains old
# releases, whereas ftpmirror.gnu.org round-robins to mirrors that may carry
# only current ones.
_GNU_MIRRORS = [
    "https://mirrors.kernel.org/gnu/",
    "https://ftpmirror.gnu.org/gnu/",
    "https://ftp.gnu.org/gnu/",
]
MIRROR_FALLBACKS = {
    "ftpmirror.gnu.org/gnu/": _GNU_MIRRORS,
    "ftp.gnu.org/gnu/": _GNU_MIRRORS,
}

# Failures a mirror fetch can raise that should move us to the next mirror
# rather than abort the build. OSError covers URLError, HTTPError and timeouts;
# http.client.HTTPException covers a mirror truncating the body mid-transfer
# (IncompleteRead, BadStatusLine), which derives from Exception, not OSError.
_FETCH_ERRORS = (OSError, ValueError, http.client.HTTPException)


class _TooLarge(ValueError):
    """The archive exceeded MAX_DOWNLOAD_BYTES. Retrying a different mirror for
    the same archive cannot help, so this is kept distinct from a fetch error."""


class _ManifestParser(configparser.ConfigParser):
    """Reads manifests the way getdeps does.

    Manifests carry bare keys in sections unrelated to downloads (rpm/deb
    lists, autoconf args), which the default parser rejects, and getdeps keeps
    option-key case rather than lowercasing it.
    """

    def __init__(self) -> None:
        super().__init__(allow_no_value=True, interpolation=None)

    def optionxform(self, optionstr: str) -> str:
        return optionstr


def resolve_download_dir(getdeps_path: str, getdeps_args: list[str]) -> str | None:
    """Return the download directory that getdeps will use."""
    scratch_path = None
    for index, arg in enumerate(getdeps_args):
        if arg == "--scratch-path":
            if index + 1 >= len(getdeps_args):
                print("  WARNING - --scratch-path has no value; skipping prefetch")
                return None
            scratch_path = getdeps_args[index + 1]
            break
        if arg.startswith("--scratch-path="):
            scratch_path = arg.split("=", 1)[1]
            break

    if scratch_path is None:
        try:
            scratch_path = subprocess.check_output(
                [getdeps_path, "show-scratch-dir"], text=True
            ).strip()
        except (OSError, subprocess.SubprocessError) as ex:
            print(f"  WARNING - cannot resolve getdeps scratch directory: {ex}")
            return None

    if not scratch_path:
        print("  WARNING - getdeps returned an empty scratch directory")
        return None
    return os.path.join(os.path.realpath(scratch_path), "downloads")


def sha256_file(path: str) -> str | None:
    h = hashlib.sha256()
    try:
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(DOWNLOAD_CHUNK_BYTES), b""):
                h.update(chunk)
        return h.hexdigest()
    except OSError:
        return None


def file_size(path: str) -> int | None:
    try:
        return os.path.getsize(path)
    except OSError:
        return None


def parse_manifest(manifest_path: str) -> dict[str, str] | None:
    config = _ManifestParser()
    try:
        with open(manifest_path, encoding="utf-8") as manifest_file:
            config.read_file(manifest_file)
    except (OSError, UnicodeDecodeError, configparser.Error) as ex:
        print(f"  {os.path.basename(manifest_path)}: WARNING - parse failed: {ex}")
        return None

    # A platform-conditional [download.os=...] section is not read here, so such
    # a manifest yields nothing and is left to getdeps.
    if "download" not in config:
        return None

    # getdeps derives the download filename from the manifest's declared name,
    # so read that rather than inferring it from the path. getdeps asserts the
    # two agree, making the basename fallback below unreachable in practice.
    name = config["manifest"].get("name", "") if "manifest" in config else ""
    return {
        "name": name or os.path.basename(manifest_path),
        "url": config["download"].get("url", ""),
        "sha256": config["download"].get("sha256", ""),
    }


def discover_manifests(manifests_dir: str) -> list[dict[str, str]]:
    """Every manifest pinned to a host we know mirrors for.

    Discovered by scanning rather than from a hardcoded list, so a dependency
    that later moves onto a GNU mirror is covered without editing this file.
    The cheap substring test first keeps us from parsing ~130 unrelated
    manifests, and from reporting parse errors for files we do not care about.
    """
    found = []
    try:
        names = sorted(os.listdir(manifests_dir))
    except OSError as ex:
        print(f"  WARNING - cannot list {manifests_dir}: {ex}")
        return found

    for name in names:
        path = os.path.join(manifests_dir, name)
        if not os.path.isfile(path):
            continue
        # A non-UTF-8 file raises UnicodeDecodeError, which is a ValueError
        # rather than an OSError; both mean "not a manifest we can read".
        try:
            with open(path, encoding="utf-8") as f:
                text = f.read()
        except (OSError, UnicodeDecodeError):
            continue
        if not any(pattern in text for pattern in MIRROR_FALLBACKS):
            continue

        info = parse_manifest(path)
        if info and info["url"] and get_fallback_mirrors(info["url"]):
            found.append(info)
    return found


def get_fallback_mirrors(url: str) -> list[str]:
    for pattern, mirrors in MIRROR_FALLBACKS.items():
        if pattern in url:
            path = url[url.find(pattern) + len(pattern) :]
            return [mirror + path for mirror in mirrors]
    return []


def download_url(url: str, filepath: str) -> None:
    """Download to a temporary file so a failure never leaves a partial archive."""
    tmp_filepath = filepath + ".tmp"
    if os.path.exists(tmp_filepath):
        os.remove(tmp_filepath)

    # urllib honours http_proxy/https_proxy, which the build container sets.
    request = urllib.request.Request(
        url, headers={"User-Agent": "fboss-getdeps-fallback/1.0"}
    )
    try:
        with (
            urllib.request.urlopen(
                request, timeout=DOWNLOAD_TIMEOUT_SECONDS
            ) as response,
            open(tmp_filepath, "wb") as output,
        ):
            copied = 0
            while True:
                chunk = response.read(DOWNLOAD_CHUNK_BYTES)
                if not chunk:
                    break
                copied += len(chunk)
                if copied > MAX_DOWNLOAD_BYTES:
                    raise _TooLarge(f"download exceeds {MAX_DOWNLOAD_BYTES} bytes")
                output.write(chunk)
        os.replace(tmp_filepath, filepath)
    finally:
        if os.path.exists(tmp_filepath):
            os.remove(tmp_filepath)


def prepare_download(
    info: dict[str, str],
    download_dir: str,
    verified: dict[str, str],
    deadline: float,
) -> bool:
    """Seed one archive, returning whether getdeps will now find it."""
    url = info["url"]
    expected_sha256 = info["sha256"]
    mirrors = get_fallback_mirrors(url)

    # Must match ArchiveFetcher's "{manifest name}-{basename of url}" exactly;
    # seeding any other name is silently ignored by getdeps. It takes the
    # basename of the parsed path, so any ?query or #fragment is excluded.
    filename = f"{info['name']}-{os.path.basename(urlparse(url).path)}"
    filepath = os.path.join(download_dir, filename)

    actual_sha256 = sha256_file(filepath) if os.path.exists(filepath) else None
    if actual_sha256 == expected_sha256:
        print(f"  {filename}: OK (already present)")
        verified[expected_sha256] = filepath
        return True
    if actual_sha256 is not None:
        print(
            f"  {filename}: WARNING - removing invalid download sha256={actual_sha256}"
        )
        os.remove(filepath)

    # Manifests can pin the same archive under different names (libiberty and
    # libiberty-python both use binutils), and getdeps wants a copy per name.
    # Reuse the bytes we already verified rather than refetching them.
    twin = verified.get(expected_sha256)
    if twin is not None:
        # Via a temporary, like download_url: a copy that dies partway (disk
        # full) must not leave a truncated archive under the real name.
        tmp_filepath = filepath + ".tmp"
        try:
            shutil.copyfile(twin, tmp_filepath)
            os.replace(tmp_filepath, filepath)
        finally:
            if os.path.exists(tmp_filepath):
                os.remove(tmp_filepath)
        print(f"  {filename}: OK (copied from {os.path.basename(twin)})")
        return True

    for mirror_url in mirrors:
        if time.monotonic() >= deadline:
            print(f"  {filename}: skipped, network budget spent")
            return False

        print(f"  {filename}: trying {mirror_url}...")
        try:
            download_url(mirror_url, filepath)
        except _TooLarge as ex:
            # A size cap is a property of the archive, not of this mirror, so
            # retrying elsewhere would just redownload it to discard it again.
            print(f"  {filename}: WARNING - {ex}, leaving it to getdeps")
            return False
        except _FETCH_ERRORS as ex:
            print(f"  {filename}: WARNING - download failed: {ex}")
            continue

        actual_sha256 = sha256_file(filepath)
        if actual_sha256 == expected_sha256:
            print(f"  {filename}: OK (downloaded, {file_size(filepath)} bytes)")
            verified[expected_sha256] = filepath
            return True

        print(
            f"  {filename}: WARNING - sha256 mismatch from {mirror_url}: "
            f"expected={expected_sha256} actual={actual_sha256} "
            f"size={file_size(filepath)}"
        )
        os.remove(filepath)

    print(f"  {filename}: WARNING - all mirrors failed, leaving it to getdeps")
    return False


def prefetch(manifests_dir: str, download_dir: str) -> tuple[int, int]:
    """Seed download_dir from mirrors. Returns (ready, checked)."""
    os.makedirs(download_dir, exist_ok=True)

    checked = 0
    ready = 0
    deadline = time.monotonic() + NETWORK_BUDGET_SECONDS
    verified: dict[str, str] = {}
    for info in discover_manifests(manifests_dir):
        package = info["name"]
        if not info["sha256"]:
            print(f"  {package}: WARNING - skipped fallback without sha256")
            continue

        checked += 1
        try:
            if prepare_download(info, download_dir, verified, deadline):
                ready += 1
        except _FETCH_ERRORS as ex:
            print(f"  {package}: WARNING - fallback preparation failed: {ex}")

    print(f"  fallback mirror downloads ready: {ready}/{checked}")
    return ready, checked


def main() -> None:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <manifests_dir> <download_dir>", file=sys.stderr)
        sys.exit(1)

    prefetch(manifests_dir=sys.argv[1], download_dir=sys.argv[2])


if __name__ == "__main__":
    main()
