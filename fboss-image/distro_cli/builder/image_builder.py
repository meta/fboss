# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

"""Image Builder - handles building FBOSS images from manifests."""

import hashlib
import logging
import os
import shutil
import subprocess
from pathlib import Path
from typing import ClassVar

from distro_cli.builder.component import ComponentBuilder
from distro_cli.lib import version_file
from distro_cli.lib.artifact import ArtifactStore, find_artifact_in_dir
from distro_cli.lib.constants import FBOSS_BUILDER_IMAGE, IMAGE_COMPONENTS
from distro_cli.lib.docker.container import run_container
from distro_cli.lib.docker.image import build_fboss_builder_image
from distro_cli.lib.exceptions import BuildError, ComponentError, ManifestError
from distro_cli.lib.paths import get_abs_path

logger = logging.getLogger(__name__)

# Set by whoever launches the build; the container cannot work it out itself.
SOURCE_REVISION_VAR = "FBOSS_SOURCE_REVISION"


# Component-specific artifact patterns
# These are used when the manifest doesn't specify an "artifact" field
# Patterns use base .tar extension - the artifact finder will automatically
# try both uncompressed (.tar) and zstd-compressed (.tar.zst) variants
COMPONENT_ARTIFACT_PATTERNS = {
    "kernel": "kernel-*.rpms.tar",
    "npu_sai": "sai-*.tar",
    "phy_sai": "phy_sai-*.tar",
    "fboss-platform-stack": "platform-stack*.tar",
    "fboss-forwarding-stack": "forwarding-stack*.tar",
    "bsps": "bsp-*.tar",
    "other_dependencies": "*.rpm",
}


def _disambiguate_names(
    artifacts: list[Path], component_name: str
) -> list[tuple[Path, str]]:
    """Pair each artifact with a unique filename within its component directory.

    Artifacts published per service share a basename -- every forwarding-stack
    tarball in Manifold is `fboss_bins.tar.zst`, distinguished only by the
    directory it sits in. Staging them all under their own basename silently
    overwrote all but one. Where a name repeats, prefix it with the source's
    parent directory, which is what actually distinguishes them.
    """
    counts: dict[str, int] = {}
    for artifact_path in artifacts:
        counts[artifact_path.name] = counts.get(artifact_path.name, 0) + 1

    paired = []
    used: set[str] = set()
    for artifact_path in artifacts:
        name = artifact_path.name
        if counts[name] > 1:
            name = f"{artifact_path.parent.name}_{name}"
        # The parent directory can repeat too (a cache layout that hashes the
        # URL, say), so fall back to a numeric suffix rather than colliding.
        candidate, index = name, 1
        while candidate in used:
            stem = name.split(".", 1)
            suffix = f".{stem[1]}" if len(stem) > 1 else ""
            candidate = f"{stem[0]}_{index}{suffix}"
            index += 1
        used.add(candidate)
        paired.append((artifact_path, candidate))

    if len(used) != len({a.name for a in artifacts}):
        logger.info(f"{component_name}: staged names disambiguated: {sorted(used)}")
    return paired


class ImageBuilder:
    """Handles building FBOSS images from manifests."""

    # Component dependencies - defines build order and dependencies
    # Format: {component: [list of dependencies]}
    # The order of keys defines the build order (dependencies first)
    COMPONENTS: ClassVar[dict[str, list[str]]] = {
        "kernel": [],
        "other_dependencies": [],
        "fboss-platform-stack": [],
        "bsps": ["kernel"],  # Each BSP needs kernel headers/modules
        "npu_sai": ["kernel"],  # HW SAI needs kernel headers/RPMs
        "phy_sai": ["kernel"],  # PHY SAI needs kernel headers/RPMs
        "fboss-forwarding-stack": [
            "npu_sai",
            "phy_sai",
        ],  # Forwarding stack needs both
        "image_build_hooks": [],
    }

    def __init__(self, manifest, kiwi_ng_debug=False, output_dir=None):
        self.manifest = manifest
        self.workspace_root = manifest.manifest_dir
        self.store = ArtifactStore()
        self.component_artifacts = {}
        self.kiwi_ng_debug = kiwi_ng_debug
        self.output_dir = Path(output_dir) if output_dir else None
        # Setup the image builder directory
        self.image_builder_dir = get_abs_path("fboss-image/image_builder")
        self.centos_template_dir = self.image_builder_dir / "templates" / "centos-09.0"
        self.after_pkgs_install_file = (
            self.centos_template_dir / "after_pkgs_install_file.json"
        )
        self.after_pkgs_execute_file = (
            self.centos_template_dir / "after_pkgs_execute_file.json"
        )
        self.compress_artifacts = False

    def _source_revision(self) -> str:
        """Best-effort source revision of the tree this manifest came from.

        The environment is asked first because the build container has no
        revision to probe -- the source is bind mounted without its VCS
        metadata, and sl is not installed -- so whoever launched the build is
        the only one that knows.
        """
        declared = os.environ.get(SOURCE_REVISION_VAR, "").strip()
        if declared:
            return declared

        for cmd in (["sl", "id", "-i"], ["git", "rev-parse", "HEAD"]):
            try:
                result = subprocess.run(
                    cmd,
                    cwd=self.manifest.manifest_dir,
                    capture_output=True,
                    text=True,
                    timeout=30,
                    check=True,
                )
            except (OSError, subprocess.SubprocessError):
                continue
            revision = result.stdout.strip()
            if revision:
                return revision
        return "unknown"

    def _write_build_provenance(self) -> None:
        """Record where this build came from, for /etc/build-info.

        `build_image_in_container.sh` runs inside the container and can see
        neither the manifest nor the source tree, so the values it cannot
        derive are handed to it through the /image_builder bind mount.
        """
        manifest_path = self.manifest.manifest_path
        digest = hashlib.sha256(manifest_path.read_bytes()).hexdigest()
        lines = [
            f"Manifest: {manifest_path.name}",
            f"Manifest-sha256: {digest}",
            f"Source-revision: {self._source_revision()}",
        ]
        provenance_file = self.image_builder_dir / "build-provenance"
        provenance_file.write_text("\n".join(lines) + "\n")
        logger.info(f"Recorded build provenance in {provenance_file}")

    def _report_version_conflicts(self) -> None:
        """Report evidence that disagreed, after the build rather than during it.

        A warning logged while artifacts are still being read scrolls past
        behind the image build; repeating it here is the only place it is read.
        The image is still produced, because the record is what diagnoses the
        mismatch -- but its parts are not known to work together.
        """
        conflicts = getattr(self, "_version_conflicts", [])
        if not conflicts:
            return

        logger.warning(
            f"{len(conflicts)} version disagreement(s) in this image; its parts "
            f"may not work together. See {version_file.FILE_NAME}:"
        )
        for conflict in conflicts:
            logger.warning(f"  {conflict}")

    def _write_version_file(self) -> None:
        """Write the one file describing what this image was assembled from.

        Written before the image is built, because that is the only way it can
        be in it: the container copies it into the rootfs, and by the time the
        images exist the rootfs is sealed. So this records what the image was
        assembled from and not the image's own checksum, which could not be read
        from inside the image anyway.
        """
        record, self._version_conflicts = version_file.build(
            manifest_path=self.manifest.manifest_path,
            manifest_data=self.manifest.data,
            component_artifacts=self.component_artifacts,
            source_revision=self._source_revision(),
        )

        # Handed to the container the same way build-provenance is, rather than
        # into the checked-in rootfs template: a generated file does not belong
        # in source, and one left there would ship in the *next* image.
        destinations = [self.image_builder_dir / version_file.FILE_NAME]
        if self.output_dir:
            destinations.append(self.output_dir / version_file.FILE_NAME)

        version_file.write(record, destinations)

    def _compress_artifact(self, artifact_path: Path, component_name: str) -> Path:
        """Compress artifact using zstd."""
        if not self.compress_artifacts:
            return artifact_path

        if artifact_path.name.endswith(".tar.zst"):
            return artifact_path

        compressed_path = artifact_path.with_suffix(".tar.zst")
        logger.info(f"{component_name}: Compressing {artifact_path.name}")

        try:
            subprocess.run(
                ["zstd", "-T0", str(artifact_path), "-o", str(compressed_path)],
                check=True,
                capture_output=True,
                text=True,
            )
            artifact_path.unlink()
            logger.info(f"{component_name}: Compressed to {compressed_path.name}")
            return compressed_path

        except subprocess.CalledProcessError as e:
            raise BuildError(f"{component_name}: Compression failed: {e.stderr}") from e

    def _create_component_builder(
        self,
        component_name: str,
        component_data: dict,
        dependency_artifacts: dict[str, Path] | None = None,
    ) -> ComponentBuilder:
        """Create a ComponentBuilder for the given component using conventions.

        Args:
            component_name: Name of the component (from JSON key)
            component_data: Component data dict from manifest
            dependency_artifacts: Dictionary mapping dependency names to their artifact paths

        Returns:
            ComponentBuilder instance configured for the component
        """
        # For array elements, extract the base name
        base_name = component_name.split("[")[0]

        # Get artifact pattern from the predefined patterns
        artifact_pattern = COMPONENT_ARTIFACT_PATTERNS.get(base_name)

        # Determine artifact key salt based on whether we'll compress the artifact
        # This ensures compressed and uncompressed builds use different artifact store keys
        artifact_key_salt = "compressed" if self.compress_artifacts else "uncompressed"

        return ComponentBuilder(
            component_name=component_name,
            component_data=component_data,
            manifest_dir=self.manifest.manifest_dir,
            store=self.store,
            artifact_pattern=artifact_pattern,
            dependency_artifacts=dependency_artifacts or {},
            artifact_key_salt=artifact_key_salt,
        )

    def build_all(self):
        """Build all components and distribution artifacts."""
        logger.info("Building FBOSS Image")

        # Full build: components will be immediately extracted and used
        # Skip compression to save several minutes of overhead
        self.compress_artifacts = False

        for component in self.COMPONENTS:
            if self.manifest.has_component(component):
                self._build_component(component)

        self._build_base_image()

    def build_components(self, component_names: list[str]):
        """Build specific components."""
        logger.info(f"Building components: {', '.join(component_names)}")

        # Component-only build: artifacts may be stored/reused later
        # Enable compression for component builds (not the default)
        self.compress_artifacts = True

        for component in component_names:
            if not self.manifest.has_component(component):
                raise ComponentError(f"Component '{component}' not found in manifest")

        for component in self.COMPONENTS:
            if component in component_names:
                self._build_component(component)

    def _move_distro_file(self, format_name: str, file_extension: str):
        dist_formats = self.manifest.data.get("distribution_formats")
        if not dist_formats or format_name not in dist_formats:
            return

        actual_output_dir = (
            self.output_dir if self.output_dir else self.image_builder_dir / "output"
        )
        output = find_artifact_in_dir(
            output_dir=actual_output_dir,
            pattern=f"FBOSS-Distro-Image.x86_64-1.0.install.{file_extension}",
            component_name="Base image",
        )
        image = Path(dist_formats[format_name])
        logger.info(f"Moving {format_name} artifact: {output} -> {image.resolve()}")
        shutil.move(str(output), str(image))

    def _stage_component_artifacts(self) -> Path:
        """Stage component artifacts for container mounting.

        Returns the *container* path where artifacts will be visible.
        Artifacts are staged under image_builder_dir/deps_staging on the host,
        which is exposed inside the container via the /image_builder bind mount.
        This allows for hardlink operations (cp -la) within a single filesystem.
        """
        deps_tmpdir = self.image_builder_dir / "deps_staging"
        deps_tmpdir.mkdir(parents=True, exist_ok=True)

        if not self.component_artifacts:
            raise BuildError("No component artifacts found; cannot build image.")

        logger.info(f"Staging component artifacts in {deps_tmpdir}")

        for component_name, artifact in self.component_artifacts.items():
            if artifact is None:
                continue

            component_dir = deps_tmpdir / component_name
            component_dir.mkdir(parents=True, exist_ok=True)

            artifacts_to_copy = (
                [artifact] if not isinstance(artifact, list) else artifact
            )
            artifacts_to_copy = _disambiguate_names(artifacts_to_copy, component_name)

            for artifact_path, dest_name in artifacts_to_copy:
                dest_path = component_dir / dest_name
                # Hardlink (same fs, read-only in container) to avoid duplicating
                # multi-GB artifacts; copy if the link fails (e.g. cross-device).
                try:
                    os.link(artifact_path, dest_path)
                except OSError:
                    shutil.copy2(artifact_path, dest_path)
                logger.info(f"Staged {component_name}: {dest_name}")

        return Path("/image_builder/deps_staging")

    def _build_base_image(self):
        """Build the base OS image and create distribution artifacts."""
        logger.info("Starting base OS image build")

        dist_formats = self.manifest.data.get("distribution_formats")
        if not dist_formats or not any(
            k in dist_formats for k in ["usb", "pxe", "onie"]
        ):
            raise ManifestError("No distribution formats specified in manifest")

        logger.info(f"Using image builder: {self.image_builder_dir}")

        build_fboss_builder_image()

        volumes = {
            self.image_builder_dir: Path("/image_builder"),
            Path("/dev"): Path("/dev"),
        }

        if self.output_dir:
            self.output_dir.mkdir(parents=True, exist_ok=True)
            volumes[self.output_dir] = Path("/image_builder/output")

        self._stage_component_artifacts()
        self._write_build_provenance()
        self._write_version_file()

        command = [
            "/image_builder/bin/build_image_in_container.sh",
        ]

        # Add flags for PXE/USB if either is requested
        if "usb" in dist_formats or "pxe" in dist_formats:
            command.append("--build-pxe-usb")

        # Add flag for ONIE if requested
        if "onie" in dist_formats:
            command.append("--build-onie")

        if self.kiwi_ng_debug:
            command.append("--debug")

        image_build_hooks = self.manifest.data.get("image_build_hooks")
        try:
            Path.unlink(self.after_pkgs_install_file)  # Best effort
            Path.unlink(self.after_pkgs_execute_file)
        except FileNotFoundError:
            pass

        # Helper function to process hook files
        def process_hook_file(hook_key: str, target_file: Path, cmd_flag: str):
            if hook_key in image_build_hooks:
                user_input_file = self.manifest.manifest_dir / Path(
                    image_build_hooks[hook_key]
                )

                if user_input_file.is_file():
                    # Copy to fixed name in template/centos-09.0 directory
                    shutil.copy(str(user_input_file), target_file)
                    logger.info(
                        f"Copied {user_input_file.name} to {self.centos_template_dir}"
                    )

                    # Pass the filename as an argument to the build_image_in_container.sh script
                    command.append(cmd_flag)
                    command.append(target_file.name)
                else:
                    raise BuildError(
                        f"User specified {hook_key}, but file not found: {user_input_file.name}"
                    )

        # Process both hook files
        process_hook_file(
            "after_pkgs_install",
            self.after_pkgs_install_file,
            "--after-pkgs-input-file",
        )
        process_hook_file(
            "after_pkgs_execute",
            self.after_pkgs_execute_file,
            "--after-pkgs-execute-file",
        )

        # Run the build script inside fboss_builder container
        try:
            exit_code = run_container(
                image=FBOSS_BUILDER_IMAGE,
                command=command,
                volumes=volumes,
                privileged=True,
            )

            if exit_code != 0:
                raise BuildError(f"Base image build failed with exit code {exit_code}")

            self._move_distro_file("usb", "iso")
            self._move_distro_file("pxe", "tar")
            self._move_distro_file("onie", "bin")

            self._report_version_conflicts()

            logger.info("Finished base OS image build")
        finally:
            deps_tmpdir = self.image_builder_dir / "deps_staging"
            if deps_tmpdir.exists():
                shutil.rmtree(deps_tmpdir)
                logger.debug(f"Cleaned up temporary deps directory: {deps_tmpdir}")

    def _get_dependency_artifacts(self, component: str) -> dict[str, Path]:
        """Get artifacts for all dependencies of a component.

        For array elements like 'bsps[0]', extracts base name 'bsps' to look up dependencies.

        Args:
            component: Component name (e.g., 'kernel' or 'bsps[0]')

        Returns:
            Dictionary mapping dependency names to their artifact paths
        """
        # Extract base component name for array elements (e.g., 'bsps[0]' -> 'bsps')
        base_component = component.split("[")[0]
        dependencies = self.COMPONENTS.get(base_component, [])
        return {
            dep: self.component_artifacts[dep]
            for dep in dependencies
            if dep in self.component_artifacts
        }

    def _ensure_dependencies_built(self, component: str):
        """Ensure all dependencies for a component are built.

        For array elements like 'bsps[0]', extracts base name 'bsps' to look up dependencies.

        Args:
            component: Component name (e.g., 'kernel' or 'bsps[0]')

        Raises:
            ComponentError: If a dependency cannot be built
        """
        # Extract base component name for array elements (e.g., 'bsps[0]' -> 'bsps')
        base_component = component.split("[")[0]
        dependencies = self.COMPONENTS.get(base_component, [])

        for dep in dependencies:
            # Skip if dependency is already built
            if dep in self.component_artifacts:
                logger.debug(f"Dependency '{dep}' already built for '{component}'")
                continue

            # Check if dependency exists in manifest
            if not self.manifest.has_component(dep):
                raise ComponentError(
                    f"Component '{component}' depends on '{dep}', but '{dep}' is not defined in the manifest"
                )

            # Build the dependency
            logger.info(f"Building dependency '{dep}' for '{component}'")
            self._build_component(dep)

    def _build_component(self, component: str):
        """Build a specific component by delegating to component builder."""
        logger.info(f"Building: {component}")

        component_data = self.manifest.get_component(component)

        # Check if component is an array - if so, build each element
        if isinstance(component_data, list):
            artifact_paths = []
            for idx, element_data in enumerate(component_data):
                element_name = f"{component}[{idx}]"
                logger.info(f"Building: {element_name}")

                # Ensure dependencies are built before building this array element
                self._ensure_dependencies_built(element_name)

                # Get dependency artifacts to pass to the builder
                dependency_artifacts = self._get_dependency_artifacts(element_name)

                # Create a ComponentBuilder for this array element
                component_builder = self._create_component_builder(
                    element_name, element_data, dependency_artifacts
                )
                artifact_path = component_builder.build()

                # Compress artifact if needed (happens on host, outside container)
                # Skip compression for downloaded artifacts - use them as-is
                if artifact_path:
                    if "download" not in element_data:
                        artifact_path = self._compress_artifact(
                            artifact_path, element_name
                        )
                    artifact_paths.append(artifact_path)

            self.component_artifacts[component] = (
                artifact_paths if artifact_paths else None
            )
            return

        # Ensure dependencies are built before building this component
        self._ensure_dependencies_built(component)

        # Get dependency artifacts to pass to the builder
        dependency_artifacts = self._get_dependency_artifacts(component)

        # Create the component builder and build it
        component_builder = self._create_component_builder(
            component, component_data, dependency_artifacts
        )
        artifact_path = component_builder.build()

        # Compress artifact if needed (happens on host, outside container)
        # Skip compression for downloaded artifacts - use them as-is
        if artifact_path and "download" not in component_data:
            artifact_path = self._compress_artifact(artifact_path, component)

        self.component_artifacts[component] = artifact_path


# The build order above and the canonical component list must describe the same
# set: manifest validation rejects anything outside IMAGE_COMPONENTS, so a
# component added here but not there would be unreachable.
if set(ImageBuilder.COMPONENTS) != set(IMAGE_COMPONENTS):
    raise RuntimeError(
        "ImageBuilder.COMPONENTS and constants.IMAGE_COMPONENTS disagree: "
        f"{set(ImageBuilder.COMPONENTS) ^ set(IMAGE_COMPONENTS)}"
    )
