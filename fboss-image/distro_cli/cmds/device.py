# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

"""Device command implementation."""

import json
import logging
import os
import shutil
import sys
import urllib.request
from http import HTTPStatus
from pathlib import Path

from distro_cli.lib.cli import validate_path
from distro_cli.lib.device_update import DeviceUpdateError, DeviceUpdater
from distro_cli.lib.distro_infra import (
    deploy_image_to_device,
    DISTRO_INFRA_CONTAINER,
    find_persistent_dir,
    get_interface_name,
    GETIP_SCRIPT_CONTAINER_PATH,
)
from distro_cli.lib.docker import container
from distro_cli.lib.exceptions import DistroInfraError
from distro_cli.lib.manifest import ImageManifest

logger = logging.getLogger(__name__)


def print_to_console(message: str) -> None:
    """Print message to console"""
    print(message)  # noqa: T201


def _download_with_cache(cache_dir: Path, url_prefix: str, filename: str) -> Path:
    """Download from HTTP/HTTPS with caching support using ETag and Last-Modified headers.

    Args:
        url: HTTP or HTTPS URL
        cache_dir: Directory to cache the downloaded file

    Returns:
        Path to the cached artifact file

    Raises:
        DistroInfraError: If download fails
    """
    artifact_path = cache_dir / filename
    metadata_path = cache_dir / (filename + ".http_metadata.json")
    url = url_prefix + filename

    # Load existing metadata if available
    http_metadata = {}
    if metadata_path.exists():
        try:
            with metadata_path.open() as f:
                http_metadata = json.load(f)
        except Exception as e:
            logger.warning(f"Failed to load HTTP metadata from {metadata_path}: {e}")

    # Create request with conditional headers
    request = urllib.request.Request(url)

    if http_metadata.get("etag"):
        request.add_header("If-None-Match", http_metadata["etag"])
        logger.debug(f"Added If-None-Match: {http_metadata['etag']}")
    if http_metadata.get("last_modified"):
        request.add_header("If-Modified-Since", http_metadata["last_modified"])
        logger.debug(f"Added If-Modified-Since: {http_metadata['last_modified']}")

    try:
        response = urllib.request.urlopen(request)

        # Download the file
        logger.info(f"Downloading: {url} -> {artifact_path}")
        with artifact_path.open("wb") as f:
            shutil.copyfileobj(response, f)

        # Save metadata
        metadata = {
            "etag": response.headers.get("ETag"),
            "last_modified": response.headers.get("Last-Modified"),
        }
        try:
            with metadata_path.open("w") as f:
                json.dump(metadata, f, indent=2)
        except Exception as e:
            logger.warning(f"Failed to save HTTP metadata to {metadata_path}: {e}")

        logger.info(f"Downloaded: {url} -> {artifact_path}")
        return artifact_path

    except urllib.error.HTTPError as e:
        if e.code == HTTPStatus.NOT_MODIFIED:
            logger.info(f"Not modified {url} - using cached file")
            if artifact_path.exists():
                return artifact_path
            raise DistroInfraError(
                f"HTTP 304 Not Modified but cached file not found: {artifact_path}"
            ) from e
        raise DistroInfraError(f"HTTP error {e.code} downloading {url}: {e}") from e

    except Exception as e:
        raise DistroInfraError(f"Failed to download {url}: {e}") from e


def image_upstream_command(args):
    """Download full image from upstream repository and set it to be loaded onto device"""
    image_repo = "https://meta.github.io/fboss/images/latest"

    filename = "fboss"
    filename += f"_{args.hw_agent_sai}"
    if args.qsfp_service_sai != "":
        filename += f"_{args.qsfp_service_sai}"
    filename += f"_{args.kernel}"
    filename += f"_{args.bsps}"
    filename += ".tar"

    url_prefix = f"{image_repo}/{args.train}/"
    logger.info(f"Would download {url_prefix}{filename}")

    cache_dir = find_persistent_dir() / "cache"

    # Download with HTTP caching
    try:
        logger.info(f"Downloading image from: {url_prefix}{filename}")
        artifact_path = _download_with_cache(cache_dir, url_prefix, filename)
        logger.info(f"Image cached at: {artifact_path}")
    except DistroInfraError as e:
        logger.error(f"Failed to download image: {e}")
        sys.exit(1)

    # Deploy the image to the device
    try:
        deploy_image_to_device(args.mac, artifact_path)
        logger.info(
            f"Successfully configured device {args.mac} with image {artifact_path}"
        )
        logger.info("Device is ready for PXE boot")
    except DistroInfraError as e:
        logger.error(f"Failed to deploy image to device: {e}")
        sys.exit(1)


def image_command(args):
    """Set device image from file and configure PXE boot"""
    logger.info(f"Setting image for device {args.mac}: {args.image_path}")

    try:
        deploy_image_to_device(args.mac, args.image_path)
        logger.info(
            f"Successfully configured device {args.mac} with image {args.image_path}"
        )
        logger.info("Device is ready for PXE boot")

    except DistroInfraError as e:
        logger.error(f"Failed to configure device: {e}")
        sys.exit(1)
    except Exception as e:
        logger.error(f"Unexpected error: {e}")
        sys.exit(1)


def reprovision_command(args):
    """Reprovision device"""
    ip_address = get_device_ip(args.mac)

    if not ip_address:
        logger.error("No IP address found for device")
        return

    # devpart -> /dev/nvme0n1p3
    # dev     -> /dev/nvme0n3
    # part    -> 3
    cmd = r"""
    if [ ! -d /opt/fboss ]; then echo "Not an FBOSS device. Aborting"; exit 1; fi; \
    rm -rf /boot/efi/EFI/*;
    root_devpart=$(mount | awk '/\/ type/ { print $1 }');
    root_dev=$(mount | awk -F 'p' '/\/ type/ { print $1 }');
    root_part=$(mount | awk -F '[[:space:]p]' '/\/ type/ { print $2 }');
    dd if=/dev/zero of=${root_devpart} bs=1M count=50;
    (sleep 1; echo yes; sleep 1; echo ignore) | parted ---pretend-input-tty ${root_dev} rm ${root_part};
    reboot --force
    """
    os.execvp(
        "ssh",
        [
            "ssh",
            "-o",
            "StrictHostKeyChecking=no",
            "-o",
            "UserKnownHostsFile=/dev/null",
            f"root@{ip_address}",
            cmd,
        ],
    )


def update_command(args):
    """Update specific components on device"""
    logger.info(f"Updating device {args.mac}")
    logger.info(f"Manifest: {args.manifest}")
    logger.info(f"Components: {' '.join(args.components)}")

    manifest = ImageManifest(Path(args.manifest))

    # Get device IP once for all components
    device_ip = get_device_ip(args.mac)
    if not device_ip:
        logger.error("Cannot update: device IP not found")
        sys.exit(1)

    for component in args.components:
        try:
            updater = DeviceUpdater(
                mac=args.mac,
                manifest=manifest,
                component=component,
                device_ip=device_ip,
            )
            updater.update()
            logger.info(f"Successfully updated {component}")
        except DeviceUpdateError as e:
            logger.error(f"Failed to update {component}: {e}")
            sys.exit(1)


def get_device_ip(mac: str) -> str | None:
    """Get device IP address by querying the distro-infra container.

    Args:
        mac: Device MAC address

    Returns:
        IP address string (IPv4 preferred, IPv6 fallback), or None if not found
    """
    if not container.container_is_running(DISTRO_INFRA_CONTAINER):
        logger.error(f"Container '{DISTRO_INFRA_CONTAINER}' is not running")
        logger.error("Please start the distro-infra container first")
        return None

    try:
        interface = get_interface_name()
    except DistroInfraError as e:
        logger.error(f"Failed to get interface name: {e}")
        return None

    cmd = [GETIP_SCRIPT_CONTAINER_PATH, mac, interface]

    # Execute in container
    exit_code, stdout, stderr = container.exec_in_container(DISTRO_INFRA_CONTAINER, cmd)

    if exit_code != 0:
        logger.error(f"getip.sh failed with exit code {exit_code}")
        if stderr:
            logger.error(f"stderr: {stderr}")
        if stdout:
            logger.error(f"stdout: {stdout}")
        return None

    try:
        result = json.loads(stdout)

        if "error_code" in result:
            logger.error(f"Error: {result.get('error', 'Unknown error')}")
            logger.error(f"Error code: {result['error_code']}")
            return None

        ipv4 = result.get("ipv4")
        ipv6 = result.get("ipv6")

        return ipv4 if ipv4 else ipv6

    except json.JSONDecodeError as e:
        logger.error(f"Failed to parse JSON output: {e}")
        logger.error(f"Output was: {stdout}")
        return None


def getip_command(args):
    """Get device IP address"""
    logger.info(f"Getting IP for device {args.mac}")

    ip_address = get_device_ip(args.mac)

    if ip_address:
        print_to_console(ip_address)
    else:
        logger.error("No IP address found in response")


def ssh_command(args):
    """SSH to device"""
    logger.info(f"SSH to device {args.mac}")

    ip_address = get_device_ip(args.mac)

    if not ip_address:
        logger.error("No IP address found for device")
        return

    logger.info(f"Connecting to {ip_address}")
    os.execvp(
        "ssh",
        [
            "ssh",
            "-o",
            "StrictHostKeyChecking=no",
            "-o",
            "UserKnownHostsFile=/dev/null",
            f"root@{ip_address}",
        ],
    )


def setup_device_commands(cli):
    """Setup the device commands"""
    device = cli.add_command_group(
        "device",
        help_text="Manage FBOSS devices",
        arguments=[("mac", {"help": "Device MAC address"})],
    )

    device.add_command(
        "image-upstream",
        image_upstream_command,
        help_text="Download and set upstream Distro Image to be loaded onto device",
        arguments=[
            (
                "--hw_agent_sai",
                {
                    "required": True,
                    "help": "hw_agent ASIC SAI version to use, decides ASIC support",
                },
            ),
            (
                "--train",
                {
                    "default": "stable",
                    "choices": ("stable", "head"),
                    "help": "Image release train to use",
                },
            ),
            (
                "--kernel",
                {
                    "default": "v6.11",
                    "choices": ("v6.11", "v6.4"),
                    "help": "Kernel version to use",
                },
            ),
            (
                "--qsfp_service_sai",
                {
                    "default": "",
                    "help": "qsfp_service Phy SAI version to use",
                },
            ),
            (
                "--bsps",
                {
                    "default": "oss",
                    "help": "BSP set to use",
                },
            ),
            (
                "--tag",
                {
                    "default": "",
                    "help": "Misc image identification tag",
                },
            ),
        ],
    )

    device.add_command(
        "image",
        image_command,
        help_text="Set Distro Image file to be loaded onto device",
        arguments=[
            (
                "image_path",
                {
                    "type": lambda p: validate_path(p, must_exist=True),
                    "help": "Path to image file",
                },
            )
        ],
    )

    device.add_command(
        "reprovision", reprovision_command, help_text="Reprovision device", arguments=[]
    )

    device.add_command(
        "update",
        update_command,
        help_text="Update specific components on device",
        arguments=[
            (
                "manifest",
                {
                    "type": lambda p: validate_path(p, must_exist=True),
                    "help": "Path to manifest JSON file",
                },
            ),
            ("components", {"nargs": "+", "help": "Component names to update"}),
        ],
    )

    device.add_command(
        "getip",
        getip_command,
        help_text="Get device IP address",
    )

    device.add_command(
        "ssh",
        ssh_command,
        help_text="SSH to device",
    )
