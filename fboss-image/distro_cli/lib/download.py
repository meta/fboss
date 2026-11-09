# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

"""Download utilities for FBOSS image builder."""

import json
import logging
import os
import shutil
import subprocess
import urllib.request
from http import HTTPStatus
from pathlib import Path

from .artifact import ArtifactStore
from .constants import DEFAULT_MANIFOLD_CLI, MANIFOLD_CLI_VAR, MANIFOLD_URL_PREFIX
from .exceptions import ArtifactError

logger = logging.getLogger(__name__)

FILE_URL_PREFIX = "file:"
HTTP_METADATA_FILENAME = ".http_metadata.json"  # Stored alongside cached artifact


def download_artifact(
    url: str,
    manifest_dir: Path | None = None,
    cached_data_files: list[Path] | None = None,
    cached_metadata_files: list[Path] | None = None,
) -> tuple[bool, list[Path], list[Path]]:
    """Download artifact from URL.

    Supports http://, https://, file:// and manifold: URLs.
    HTTP(S) downloads use ETag and Last-Modified headers for conditional requests.

    For downloads, we expect exactly one data file and one metadata file.

    Args:
        url: URL to download from
        manifest_dir: Base directory for resolving relative file:// paths
        cached_data_files: Cached data files (expects 0 or 1 file for downloads)
        cached_metadata_files: Cached metadata files (expects 0 or 1 file for downloads)

    Returns:
        Tuple of (cache_hit, data_files, metadata_files)

    Raises:
        ArtifactError: If download fails
    """
    logger.info(f"Downloading artifact from: {url}")

    if url.startswith(FILE_URL_PREFIX):
        file_path = url.removeprefix(FILE_URL_PREFIX)
        if manifest_dir and not Path(file_path).is_absolute():
            source_path = (manifest_dir / file_path).resolve()
        else:
            source_path = Path(file_path).resolve()

        if not source_path.exists():
            raise ArtifactError(f"Local file not found: {source_path}")

        # Check modification time to detect changes
        current_mtime = source_path.stat().st_mtime

        # For downloads, we expect at most one metadata file with a known name
        cached_mtime = None
        if cached_metadata_files:
            # Use the first (and only) metadata file
            metadata_path = cached_metadata_files[0]
            if metadata_path.name == HTTP_METADATA_FILENAME:
                metadata = _load_http_metadata(metadata_path)
                cached_mtime = metadata.get("mtime")

        # Check if file has been modified
        if cached_mtime is not None and current_mtime == cached_mtime:
            logger.info(f"Using cached file:// artifact (unchanged): {source_path}")
            return (True, cached_data_files, cached_metadata_files)  # pyre-ignore[7]

        # File is new or modified - copy to temp dir so the artifact store
        # can safely move the copy without removing the original source file.
        temp_download_dir = ArtifactStore.create_temp_dir(prefix="download-")
        try:
            # Copy the source file into the temp directory
            temp_data_path = temp_download_dir / source_path.name
            shutil.copy2(str(source_path), str(temp_data_path))

            # Save metadata with mtime
            metadata_path = temp_download_dir / HTTP_METADATA_FILENAME
            metadata = {"mtime": current_mtime}
            with metadata_path.open("w") as f:
                json.dump(metadata, f, indent=2)
        except Exception as e:
            logger.warning(
                f"Failed to prepare file:// artifact from {source_path}: {e}"
            )
            # Clean up temp dir on error
            ArtifactStore.delete_temp_dir(temp_download_dir)
            raise

        logger.info(f"Using local file: {source_path}")
        return (False, [temp_data_path], [metadata_path])

    if url.startswith(MANIFOLD_URL_PREFIX):
        return _download_manifold(url)

    return _download_http_with_cache(url, cached_data_files, cached_metadata_files)


def _download_manifold(url: str) -> tuple[bool, list[Path], list[Path]]:
    """Fetch a Manifold-hosted artifact by shelling out to the manifold CLI.

    Manifold has no HTTP endpoint reachable from the build container, so there is
    no ETag/mtime to compare against; every call re-fetches.

    Args:
        url: manifold:<bucket>/<path> URL

    Returns:
        Tuple of (cache_hit, data_files, metadata_files)

    Raises:
        ArtifactError: If the CLI is missing or the fetch fails
    """
    blob_path = url.removeprefix(MANIFOLD_URL_PREFIX)
    cli = os.environ.get(MANIFOLD_CLI_VAR, DEFAULT_MANIFOLD_CLI)

    temp_download_dir = ArtifactStore.create_temp_dir(prefix="download-")
    temp_data_path = temp_download_dir / Path(blob_path).name
    try:
        subprocess.run(
            [cli, "get", blob_path, str(temp_data_path)],
            check=True,
            capture_output=True,
            text=True,
        )
    except FileNotFoundError as e:
        ArtifactStore.delete_temp_dir(temp_download_dir)
        raise ArtifactError(
            f"manifold CLI not found at {cli!r}; set {MANIFOLD_CLI_VAR}"
        ) from e
    except subprocess.CalledProcessError as e:
        ArtifactStore.delete_temp_dir(temp_download_dir)
        raise ArtifactError(f"Failed to fetch {url}: {e.stderr}") from e

    logger.info(f"Fetched from Manifold: {blob_path}")
    return (False, [temp_data_path], [])


def _load_http_metadata(metadata_path: Path) -> dict:
    """Load HTTP metadata from file.

    Args:
        metadata_path: Path to metadata file

    Returns:
        Dictionary with 'etag' and 'last_modified' keys
    """
    if metadata_path:
        try:
            with metadata_path.open() as f:
                return json.load(f)
        except Exception as e:
            logger.warning(f"Failed to load HTTP metadata from {metadata_path}: {e}")
    return {}


def _save_http_metadata(
    artifact_dir: Path, etag: str | None, last_modified: str | None
):
    """Save HTTP metadata to file.

    Args:
        artifact_dir: Directory for metadata file
        etag: ETag header value
        last_modified: Last-Modified header value
    """
    metadata_path = artifact_dir / HTTP_METADATA_FILENAME
    metadata = {
        "etag": etag,
        "last_modified": last_modified,
    }
    try:
        with metadata_path.open("w") as f:
            json.dump(metadata, f, indent=2)
    except Exception as e:
        logger.warning(f"Failed to save HTTP metadata to {metadata_path}: {e}")


def _download_http_with_cache(
    url: str,
    cached_data_files: list[Path] | None,
    cached_metadata_files: list[Path] | None,
) -> tuple[bool, list[Path], list[Path]]:
    """Download from HTTP/HTTPS with caching support using ETag and Last-Modified headers.

    For downloads, we expect exactly one metadata file.

    Args:
        url: HTTP or HTTPS URL
        cached_data_files: Cached data files (expects 0 or 1 file)
        cached_metadata_files: Cached metadata files (expects 0 or 1 file)

    Returns:
        Tuple of (cache_hit, data_files, metadata_files)

    Raises:
        ArtifactError: If download fails
    """
    # For downloads, expect at most one metadata file with a known name
    http_metadata = {}
    if cached_metadata_files:
        # Construct static path using known filename instead of searching
        metadata_dir = cached_metadata_files[0].parent
        metadata_path = metadata_dir / HTTP_METADATA_FILENAME
        if metadata_path.exists():
            http_metadata = _load_http_metadata(metadata_path)

    request = urllib.request.Request(url)
    if http_metadata:
        if http_metadata.get("etag"):
            request.add_header("If-None-Match", http_metadata["etag"])
            logger.debug(f"Added If-None-Match: {http_metadata['etag']}")
        if http_metadata.get("last_modified"):
            request.add_header("If-Modified-Since", http_metadata["last_modified"])
            logger.debug(f"Added If-Modified-Since: {http_metadata['last_modified']}")

    temp_download_dir = None
    try:
        response = urllib.request.urlopen(request)

        # Use temporary directory on same filesystem as artifact store
        temp_download_dir = ArtifactStore.create_temp_dir(prefix="download-")
        filename = url.split("/")[-1]
        artifact_path = temp_download_dir / filename

        with artifact_path.open("wb") as f:
            shutil.copyfileobj(response, f)

        etag = response.headers.get("ETag")
        last_modified = response.headers.get("Last-Modified")
        metadata_path = temp_download_dir / HTTP_METADATA_FILENAME
        _save_http_metadata(temp_download_dir, etag, last_modified)
        logger.debug(f"Saved HTTP metadata: ETag={etag}, Last-Modified={last_modified}")

        logger.info(f"Downloaded: {url} -> {artifact_path}")
        return (False, [artifact_path], [metadata_path])

    except urllib.error.HTTPError as e:
        if e.code == HTTPStatus.NOT_MODIFIED:
            logger.info(f"HTTP 304 Not Modified for {url} - content unchanged")
            return (True, cached_data_files, cached_metadata_files)  # pyre-ignore[7]
        # Clean up temp dir on error
        if temp_download_dir:
            ArtifactStore.delete_temp_dir(temp_download_dir)
        raise ArtifactError(f"HTTP error {e.code} downloading {url}: {e}") from e

    except Exception as e:
        # Clean up temp dir on error
        if temp_download_dir:
            ArtifactStore.delete_temp_dir(temp_download_dir)
        raise ArtifactError(f"Failed to download {url}: {e}") from e
