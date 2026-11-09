# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

"""Image Manifest handling."""

import json
import logging
import os
import sys
from difflib import get_close_matches
from pathlib import Path
from typing import Any, ClassVar

from distro_cli.lib.constants import (
    ARTIFACT_BASE_VAR,
    DEFAULT_ARTIFACT_BUCKET,
    IMAGE_COMPONENTS,
    MANIFEST_METADATA_FIELDS,
    VARIATION_KEYS,
)

logger = logging.getLogger(__name__)


class ImageManifest:
    """Represents an FBOSS image manifest."""

    REQUIRED_FIELDS: ClassVar[list[str]] = ["distribution_formats", "kernel"]

    def __init__(self, manifest_path: Path):
        self.manifest_path = manifest_path.resolve()
        self.manifest_dir = self.manifest_path.parent
        self.data = self._load_manifest()
        self._substitute_artifact_base()
        self._validate_manifest()

    def _substitute_artifact_base(self):
        """Expand ${ARTIFACT_BASE} in download URLs from the environment.

        A manifest names the artifact it needs; where artifacts come from is a
        property of the machine building it. Keeping the base out of the file
        is what lets one landed manifest build from Manifold internally and
        from a local directory or mirror elsewhere.
        """
        placeholder = "${" + ARTIFACT_BASE_VAR + "}"
        base = os.environ.get(ARTIFACT_BASE_VAR, "").rstrip("/")

        def expand(node: Any) -> None:
            if isinstance(node, dict):
                url = node.get("download")
                if isinstance(url, str) and placeholder in url:
                    if not base:
                        logger.error(
                            f"Manifest uses {placeholder} but {ARTIFACT_BASE_VAR} "
                            f"is not set in the environment: {url}"
                        )
                        logger.error(
                            f"Set it to where artifacts live, e.g. "
                            f"{ARTIFACT_BASE_VAR}=manifold:{DEFAULT_ARTIFACT_BUCKET}"
                        )
                        sys.exit(1)
                    node["download"] = url.replace(placeholder, base)
                for value in node.values():
                    expand(value)
            elif isinstance(node, list):
                for item in node:
                    expand(item)

        expand(self.data)

    def _load_manifest(self) -> dict[str, Any]:
        """Load and validate the manifest file."""
        try:
            with self.manifest_path.open() as f:
                data = json.load(f)
            logger.info(f"Loaded manifest: {self.manifest_path}")
            return data
        except FileNotFoundError:
            logger.error(f"Manifest file not found: {self.manifest_path}")
            sys.exit(1)
        except json.JSONDecodeError as e:
            logger.error(f"Invalid JSON in manifest: {e}")
            sys.exit(1)

    def _validate_manifest(self):
        """Validate manifest has required fields and no unrecognised ones."""
        missing_required = [
            field for field in self.REQUIRED_FIELDS if field not in self.data
        ]
        if missing_required:
            logger.error(
                f"Missing required fields in manifest: {', '.join(missing_required)}"
            )
            sys.exit(1)

        known = set(IMAGE_COMPONENTS) | set(MANIFEST_METADATA_FIELDS)
        unknown = [field for field in self.data if field not in known]
        if unknown:
            # An unrecognised key is never built: the builder iterates the
            # components it knows rather than the manifest's keys, so a
            # misspelling silently drops that component from the image.
            for field in unknown:
                close = get_close_matches(field, sorted(known), n=1)
                hint = f" (did you mean '{close[0]}'?)" if close else ""
                logger.error(f"Unrecognised field in manifest: '{field}'{hint}")
            logger.error(f"Known fields: {', '.join(sorted(known))}")
            sys.exit(1)

        self._validate_variation()

    def _validate_variation(self):
        """Validate the optional variation block."""
        variation = self.data.get("variation")
        if variation is None:
            return

        if not isinstance(variation, dict):
            logger.error(
                f"'variation' must be an object, found {type(variation).__name__}"
            )
            sys.exit(1)

        unknown = [key for key in variation if key not in VARIATION_KEYS]
        if unknown:
            for key in unknown:
                close = get_close_matches(key, VARIATION_KEYS, n=1)
                hint = f" (did you mean '{close[0]}'?)" if close else ""
                logger.error(f"Unrecognised key in 'variation': '{key}'{hint}")
            logger.error(f"Known variation keys: {', '.join(VARIATION_KEYS)}")
            sys.exit(1)

    def variation(self) -> dict[str, str]:
        """Return the manifest's variation fields, empty if it declares none."""
        return self.data.get("variation", {})

    def has_component(self, component: str) -> bool:
        """Check if component is present in manifest."""
        return component in self.data

    def get_component(self, component: str) -> dict | None:
        """Return component data in manifest."""
        if component not in self.data:
            return None
        return self.data[component]

    def resolve_path(self, path: str) -> str | Path:
        """Resolve a path relative to the manifest file."""
        if path.startswith("http://") or path.startswith("https://"):
            return path
        return (self.manifest_dir / path).resolve()
