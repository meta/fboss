# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

"""Generic component builder for FBOSS image components."""

import hashlib
import logging
from os.path import commonpath
from pathlib import Path

from distro_cli.lib.artifact import find_artifact_in_dir
from distro_cli.lib.constants import FBOSS_BUILDER_IMAGE
from distro_cli.lib.download import download_artifact
from distro_cli.lib.exceptions import ComponentError
from distro_cli.lib.execute import execute_build_in_container
from distro_cli.lib.paths import get_abs_path

logger = logging.getLogger(__name__)


def _get_component_directory(component_name: str, script_path: str) -> str:
    """Determine the component directory for build artifacts.

    For scripts_path that has the component_name, we return the path in script_path
    leading to the component_name. Otherwise, the script's parent directory is returned.

    Examples:
        kernel component:
            component_name="kernel"
            script_path="fboss-image/kernel/scripts/build_kernel.sh"
            returns: "fboss-image/kernel" (component_name found in path)

        npu_sai component:
            component_name="npu_sai"
            script_path="broadcom-sai-sdk/build_fboss_sai.sh"
            returns: "broadcom-sai-sdk" (fallback to script's parent)

    Args:
        component_name: Base component name (without array index)
        script_path: Path to the build script from the execute directive

    Returns:
        Component directory path (relative to workspace root)

    """
    script_path_obj = Path(script_path)

    # Check if component_name appears in the script path
    if component_name in script_path_obj.parts:
        # Find the last occurrence of component_name in the path
        # Handle cases where component name appears multiple times
        # e.g., /src/kernel/fboss/12345/kernel/build.sh -> use the last "kernel"
        parts = script_path_obj.parts
        # Find last occurrence by reversing and using index
        component_index = len(parts) - 1 - parts[::-1].index(component_name)
        # Return the path up to and including the component_name
        return str(Path(*parts[: component_index + 1]))

    # Fall back to script's parent directory
    return str(script_path_obj.parent)


class ComponentBuilder:
    """Generic builder for FBOSS image components.

    Supports two modes:
    - download: Download pre-built artifact from URL
    - execute: Build component using a build script in Docker container

    Component-specific logic (argument parsing, paths, etc.) should be
    embedded in the component build script, not in this builder.
    """

    def __init__(
        self,
        component_name: str,
        component_data: dict,
        manifest_dir: Path,
        store,
        artifact_pattern: str | None = None,
        dependency_artifacts: dict[str, Path] | None = None,
        artifact_key_salt: str | None = None,
    ):
        """Initialize the component builder.

        Args:
            component_name: Name of the component
            component_data: Component data dict from manifest
            manifest_dir: Path to the manifest directory
            store: ArtifactStore instance
            artifact_pattern: Glob pattern for finding build artifacts (e.g., "kernel-*.rpms.tar.zst")
                             If None, component cannot use execute mode
            dependency_artifacts: Optional dict mapping dependency names to their artifact paths
            artifact_key_salt: Salt added to artifact store key to differentiate variants
        """
        self.component_name = component_name
        self.component_data = component_data
        self.manifest_dir = manifest_dir
        self.store = store
        self.artifact_pattern = artifact_pattern
        self.dependency_artifacts = dependency_artifacts or {}
        self.artifact_key_salt = artifact_key_salt

    def build(self) -> Path:
        """Build or download the component.

        Returns:
            Path to the component artifact (or None for empty components)

        Raises:
            ComponentError: If component has invalid structure
        """
        if self.component_data is None:
            raise ComponentError(f"Component '{self.component_name}' has no data")

        # ComponentBuilder handles single component instances only
        # Array components should be handled at a higher level (e.g., ImageBuilder)
        # by creating one ComponentBuilder instance per array element
        if isinstance(self.component_data, list):
            raise ComponentError(
                f"Component '{self.component_name}' data is an array. "
                "ComponentBuilder only handles single component instances. "
                "Create one ComponentBuilder per array element instead."
            )

        # Check for both download and execute (invalid)
        has_download = "download" in self.component_data
        has_execute = "execute" in self.component_data

        if has_download and has_execute:
            raise ComponentError(
                f"Component '{self.component_name}' has both 'download' and 'execute' fields. "
                "Only one is allowed."
            )

        # Allow empty components
        if not has_download and not has_execute:
            logger.info(f"Component '{self.component_name}' is empty, skipping")
            return None  # pyre-ignore[7]

        if has_download:
            return self._download_component(self.component_data["download"])

        # this must be an "execute"
        # Use artifact pattern from manifest if specified, otherwise use default
        artifact_pattern = self.component_data.get("artifact", self.artifact_pattern)
        return self._execute_component(self.component_data["execute"], artifact_pattern)

    def _download_component(self, url: str) -> Path:
        """Download component artifact from URL.

        Args:
            url: URL to download from

        Returns:
            Path to downloaded artifact (cached)
        """
        store_key = (
            f"{self.component_name}-download-{hashlib.sha256(url.encode()).hexdigest()}"
        )
        data_files, metadata_files = self.store.get(
            store_key,
            lambda data, meta: download_artifact(url, self.manifest_dir, data, meta),
        )

        if not data_files:
            raise ComponentError(f"No artifact files found for {self.component_name}")

        artifact_path = data_files[0]
        logger.info(f"{self.component_name} artifact ready: {artifact_path}")
        if metadata_files:
            logger.debug(f"  with {len(metadata_files)} metadata file(s)")
        return artifact_path

    def _execute_component(
        self, cmd_line: str | list[str], artifact_pattern: str | None = None
    ) -> Path:
        """Execute component build in Docker container.

        Args:
            cmd_line: Command line to execute (string or list of strings)
            artifact_pattern: Optional artifact pattern override from manifest

        Returns:
            Path to build artifact in cache
        """
        # For store key, convert to string (works for both str and list)
        # Include artifact_key_salt to differentiate cache variants (e.g., compressed vs uncompressed)
        store_key_str = f"{cmd_line}-salt={self.artifact_key_salt}"

        # _execute_build as a fetch_fn always starts a build expecting the underlying
        # build system to provide build specific optimizations. The objects are returned
        # back to the store with a store-miss indication.
        store_key = f"{self.component_name}-build-{hashlib.sha256(store_key_str.encode()).hexdigest()[:8]}"
        data_files, _ = self.store.get(
            store_key,
            lambda _data, _meta: (
                False,
                [self._execute_build(cmd_line, artifact_pattern)],
                [],
            ),
        )

        if not data_files:
            raise ComponentError(f"No artifact files found for {self.component_name}")

        artifact_path = data_files[0]
        logger.info(f"{self.component_name} build complete: {artifact_path}")
        return artifact_path

    def _execute_build(
        self, cmd_line: str | list[str], artifact_pattern: str | None = None
    ) -> Path:
        """Execute build in Docker container.

        Args:
            cmd_line: Command line to execute (string or list of strings)
            artifact_pattern: Optional artifact pattern override from manifest

        Returns:
            Path to build artifact

        Raises:
            ComponentError: If build fails or artifact not found
        """
        # Get script path from command line
        script_path_str = (
            cmd_line[0] if isinstance(cmd_line, list) else cmd_line.split()[0]
        )

        # Resolve script path: if absolute, use as-is; if relative, resolve from manifest_dir
        script_path = Path(script_path_str)
        resolved_script_path = (
            script_path
            if script_path.is_absolute()
            else (self.manifest_dir / script_path).resolve()
        )

        # Verify the script exists
        if not resolved_script_path.exists():
            raise ComponentError(
                f"Build script not found: {resolved_script_path} "
                f"(from manifest path: {script_path_str})"
            )

        # We mount the common parent of the script path and manifest dir
        src_dir = Path(commonpath([resolved_script_path, self.manifest_dir]))
        script_relative_to_src = resolved_script_path.relative_to(src_dir)
        container_script_path = Path("/src") / script_relative_to_src

        # For array elements, extract the base name
        base_name = (
            self.component_name.split("[")[0]
            if "[" in self.component_name
            else self.component_name
        )

        # Determine component directory (component root if known, else script's parent)
        # Use the resolved script path relative to src_dir
        script_relative_to_src = resolved_script_path.relative_to(src_dir)
        component_dir = _get_component_directory(base_name, str(script_relative_to_src))

        # Create build and dist directories under the component directory
        artifact_base_dir = src_dir / component_dir

        # Use artifact_pattern from parameter, or fall back to instance pattern, or use generic pattern
        # Generic pattern uses .tar (will match both .tar and .tar.zst via compression variant finder)
        if artifact_pattern is None:
            artifact_pattern = self.artifact_pattern or "*.tar"
        if not artifact_pattern:
            logger.warning(
                f"Component '{self.component_name}' has no artifact_pattern specified. "
                f"Using generic pattern: {artifact_pattern}"
            )

        build_dir = artifact_base_dir / ".build"
        build_dir.mkdir(parents=True, exist_ok=True)

        dist_dir = artifact_base_dir / "dist"
        dist_dir.mkdir(parents=True, exist_ok=True)

        # Mount src_dir as /src in the container
        logger.info(f"Mounting {src_dir} as /src")

        # Mount distro_cli/tools as /tools for build utilities
        tools_dir = get_abs_path("fboss-image/distro_cli/tools")

        # Mount fboss/oss/scripts for common build utilities (sccache config, etc.)
        common_scripts_dir = get_abs_path("fboss/oss/scripts")

        volumes = {
            src_dir: Path("/src"),
            build_dir: Path("/build"),
            dist_dir: Path("/output"),
            tools_dir: Path("/tools"),
            common_scripts_dir: Path("/fboss/oss/scripts"),
        }

        # Manifest-declared extra mounts. /src only covers the common parent
        # of the build script and the manifest; a component whose script needs
        # host paths outside that subtree declares them here. Generic on
        # purpose: a list of {"host": ..., "container": ...} mappings, with
        # host paths resolved from the manifest dir unless absolute, plus an
        # optional "mode" of "rw" (default) or "ro".
        read_only_volumes: dict[Path, Path] = {}
        for mount in self.component_data.get("mounts", []):
            if not isinstance(mount, dict):
                raise ComponentError(
                    f"Component '{self.component_name}' mount must be an object "
                    f"with 'host' and 'container' keys, found: {mount!r}"
                )
            host_raw = mount.get("host")
            container_raw = mount.get("container")
            if not host_raw or not container_raw:
                raise ComponentError(
                    f"Component '{self.component_name}' mount needs both 'host' "
                    f"and 'container' keys, found: {mount!r}"
                )
            host_path = Path(host_raw)
            resolved_host = (
                host_path
                if host_path.is_absolute()
                else (self.manifest_dir / host_path).resolve()
            )
            if not resolved_host.exists():
                raise ComponentError(
                    f"Component '{self.component_name}' mount host path not found: "
                    f"{resolved_host} (from manifest path: {host_raw})"
                )
            container_path = Path(container_raw)
            if not container_path.is_absolute():
                raise ComponentError(
                    f"Component '{self.component_name}' mount container path must be "
                    f"absolute, found: {container_raw}"
                )
            if container_path in volumes.values():
                raise ComponentError(
                    f"Component '{self.component_name}' mount container path "
                    f"{container_path} collides with a standard mount"
                )
            mode = mount.get("mode", "rw")
            if mode not in ("rw", "ro"):
                raise ComponentError(
                    f"Component '{self.component_name}' mount mode must be 'rw' "
                    f"or 'ro', found: {mode!r}"
                )
            if mode == "ro":
                read_only_volumes[resolved_host] = container_path
            else:
                volumes[resolved_host] = container_path
            logger.info(
                f"Mounting extra '{resolved_host}' at {container_path} ({mode}) "
                f"for '{self.component_name}'"
            )

        # Mount dependency artifacts into the container
        dependency_install_paths = {}
        for dep_name, dep_artifact in self.dependency_artifacts.items():
            dep_mount_point = Path(f"/deps/{dep_name}")
            volumes[dep_artifact] = dep_mount_point
            dependency_install_paths[dep_name] = dep_mount_point
            logger.info(
                f"Mounting dependency '{dep_name}' at {dep_mount_point}: {dep_artifact}"
            )

        # Working directory is the parent of the script
        working_dir = str(container_script_path.parent)

        # Build the container command using the container script path
        if isinstance(cmd_line, list):
            # Replace first element with container path, keep the rest
            container_cmd = [str(container_script_path), *cmd_line[1:]]
        else:
            # For string commands, replace the script path with the in-container version
            container_cmd = [str(container_script_path)]

        logger.info(f"Container command: {container_cmd}")

        # Execute build command
        execute_build_in_container(
            image_name=FBOSS_BUILDER_IMAGE,
            command=container_cmd,
            volumes=volumes,
            component_name=self.component_name,
            working_dir=working_dir,
            dependency_install_paths=(
                dependency_install_paths if dependency_install_paths else None
            ),
            read_only_volumes=read_only_volumes or None,
        )

        return find_artifact_in_dir(
            output_dir=dist_dir,
            pattern=artifact_pattern,
            component_name=self.component_name,
        )
