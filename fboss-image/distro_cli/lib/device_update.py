# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

"""Device update logic for updating FBOSS services on devices."""

import logging
import subprocess
import uuid
from pathlib import Path

from distro_cli.builder.image_builder import ImageBuilder
from distro_cli.lib.exceptions import DistroInfraError
from distro_cli.lib.manifest import ImageManifest

logger = logging.getLogger(__name__)

# Component to systemd services mapping.
COMPONENT_SERVICES: dict[str, list[str]] = {
    "fboss-forwarding-stack": ["wedge_agent", "fsdb", "qsfp_service"],
    "fboss-platform-stack": [
        "platform_manager",
        "sensor_service",
        "fan_service",
        "data_corral_service",
    ],
    "other_dependencies": [],
}

# Path to the update script that runs on the device
UPDATE_SCRIPT_PATH = Path(__file__).parent.parent / "scripts" / "update_component.sh"


class DeviceUpdateError(DistroInfraError):
    """Error during device update."""


class DeviceUpdater:
    """Handles updating FBOSS services on a device.

    Workflow:
    1. Validate component is supported for update
    2. Acquire artifacts (build OR download)
    3. SCP artifact and update_component.sh to device
    4. SSH: run update_component.sh
    """

    def __init__(
        self,
        mac: str,
        manifest: ImageManifest,
        component: str,
        device_ip: str | None = None,
    ):
        """Initialize the DeviceUpdater.

        Args:
            mac: Device MAC address
            manifest: Parsed image manifest
            component: Component name to update
            device_ip: Optional device IP (if already known)
        """
        self.mac = mac
        self.manifest = manifest
        self.component = component
        self.device_ip = device_ip

    def _get_services(self) -> list[str]:
        """Get systemd services for the component."""
        return COMPONENT_SERVICES.get(self.component, [])

    def validate(self) -> None:
        """Validate the update request.

        Raises:
            DeviceUpdateError: If validation fails
        """
        if self.component not in COMPONENT_SERVICES:
            raise DeviceUpdateError(
                f"Component '{self.component}' is not updatable. "
                f"Updatable components: {', '.join(COMPONENT_SERVICES.keys())}"
            )

        if not self.manifest.has_component(self.component):
            raise DeviceUpdateError(
                f"Component '{self.component}' not found in manifest"
            )

        if self.component == "other_dependencies":
            # other_dependencies are validated by ImageBuilder
            return

        services = self._get_services()
        if not services:
            raise DeviceUpdateError(
                f"Component '{self.component}' has no services defined in COMPONENT_SERVICES"
            )

        component_data = self.manifest.get_component(self.component)
        has_download = "download" in component_data  # pyre-ignore[58]
        has_execute = "execute" in component_data  # pyre-ignore[58]

        if not has_download and not has_execute:
            raise DeviceUpdateError(
                f"Component '{self.component}' has neither 'download' nor 'execute'"
            )

    def _acquire_artifacts(self) -> Path | list[Path]:
        """Acquire component artifacts via build or download.

        Uses ImageBuilder to handle both execute (build) and download modes.
        Dependencies are automatically built if needed.

        Returns:
            Path to the component artifact (tarball)

        Raises:
            DeviceUpdateError: If artifact acquisition fails
        """
        logger.info(f"Acquiring artifacts for {self.component}")

        builder = ImageBuilder(self.manifest)
        builder.build_components([self.component])

        artifact_path = builder.component_artifacts.get(self.component)
        if not artifact_path:
            raise DeviceUpdateError(
                f"No artifact produced for component '{self.component}'"
            )

        logger.info(f"Artifact acquired: {artifact_path}")
        return artifact_path

    def _transfer_and_execute(
        self, artifact_path: Path | list[Path], services: list[str]
    ) -> None:
        """Transfer artifact and update script to device and execute.

        Args:
            artifact_path: Path to the component artifact tarball
            services: List of services to update

        Raises:
            DeviceUpdateError: If transfer or execution fails
        """
        if not self.device_ip:
            raise DeviceUpdateError("Device IP not set")

        if not UPDATE_SCRIPT_PATH.exists():
            raise DeviceUpdateError(f"Update script not found: {UPDATE_SCRIPT_PATH}")

        device_user = "root"
        remote_dir = f"/tmp/fboss-update-{uuid.uuid4().hex[:8]}"
        ssh_opts = [
            "-o",
            "StrictHostKeyChecking=no",
            "-o",
            "UserKnownHostsFile=/dev/null",
        ]

        logger.info(f"Transferring files to {self.device_ip}:{remote_dir}")

        try:
            # Create remote directory
            result = subprocess.run(
                [
                    "ssh",
                    *ssh_opts,
                    f"{device_user}@{self.device_ip}",
                    f"mkdir -p {remote_dir}",
                ],
                capture_output=True,
                check=False,
            )
            if result.returncode != 0:
                raise DeviceUpdateError(
                    f"Failed to create remote directory: {result.stderr.decode()}"
                )

            # SCP artifact(s) and update script to device
            # Handle both single artifact (Path) and multiple artifacts (list of Paths)
            artifacts: list[Path] = (
                artifact_path if isinstance(artifact_path, list) else [artifact_path]
            )
            scp_files = [str(a) for a in artifacts] + [str(UPDATE_SCRIPT_PATH)]
            # scp requires IPv6 hosts in brackets to find the path separator.
            scp_host = (
                f"[{self.device_ip}]" if ":" in self.device_ip else self.device_ip
            )

            result = subprocess.run(
                [
                    "scp",
                    *ssh_opts,
                    *scp_files,
                    f"{device_user}@{scp_host}:{remote_dir}/",
                ],
                capture_output=True,
                check=False,
            )
            if result.returncode != 0:
                raise DeviceUpdateError(
                    f"Failed to transfer files: {result.stderr.decode()}"
                )

            services_arg = " ".join(services)
            remote_cmd = (
                f"cd {remote_dir} && "
                f"chmod +x update_component.sh && "
                f"./update_component.sh {self.component} {services_arg}"
            )

            logger.info("Executing update on device...")
            result = subprocess.run(
                ["ssh", *ssh_opts, f"{device_user}@{self.device_ip}", remote_cmd],
                capture_output=True,
                check=False,
            )
            if result.returncode != 0:
                raise DeviceUpdateError(
                    f"Failed to execute update: {result.stderr.decode()}"
                )

            logger.info(f"Update output:\n{result.stdout.decode()}")
        finally:
            # Cleanup remote directory if present
            subprocess.run(
                [
                    "ssh",
                    *ssh_opts,
                    f"{device_user}@{self.device_ip}",
                    f"rm -rf {remote_dir}",
                ],
                capture_output=True,
                check=False,
            )

    def update(self) -> bool:
        """Execute the update workflow.

        Returns:
            True if update succeeded

        Raises:
            DeviceUpdateError: If update fails
        """
        if not self.device_ip:
            raise DeviceUpdateError("Device IP not set")

        self.validate()

        services = self._get_services()
        logger.info(f"Updating {self.component} on device {self.mac}")
        logger.info(f"Services to restart: {', '.join(services)}")

        artifact_path = self._acquire_artifacts()
        self._transfer_and_execute(artifact_path, services)

        logger.info(f"Successfully updated {self.component} on device {self.mac}")
        return True
