# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

"""Execute utilities for FBOSS image builder."""

import logging
from pathlib import Path

from distro_cli.lib.docker.container import run_container
from distro_cli.lib.docker.image import build_fboss_builder_image

from .exceptions import BuildError

logger = logging.getLogger(__name__)


def execute_build_in_container(
    image_name: str,
    command: list[str],
    volumes: dict[Path, Path],
    component_name: str,
    working_dir: str | None = None,
    dependency_install_paths: dict[str, Path] | None = None,
    read_only_volumes: dict[Path, Path] | None = None,
) -> None:
    """Execute build command in Docker container.

    Args:
        image_name: Docker image name
        command: Command to execute as list
        volumes: Host to container path mappings
        component_name: Component name
        working_dir: Working directory in container
        dependency_install_paths: Dict mapping dependency names to their container mount paths
                                  (e.g., {'kernel': Path('/deps/kernel')})
                                  RPMs from these paths will be installed before running the build
        read_only_volumes: Host to container path mappings mounted read-only

    Raises:
        BuildError: If build fails
    """
    logger.info(f"Executing {component_name} build in Docker container: {image_name}")

    # Ensure fboss_builder image is built
    build_fboss_builder_image()

    # Use build_entrypoint.py from /tools (mounted from distro_cli/tools)
    if dependency_install_paths:
        logger.info(
            f"Build entry point will process {len(dependency_install_paths)} dependencies"
        )

    # Build the command: python3 /tools/build_entrypoint.py <build_command>
    # The entry point will look for dependencies in /deps and install them.
    cmd_list = ["python3", "/tools/build_entrypoint.py", *command]
    logger.info("Build will produce uncompressed artifacts (.tar)")

    logger.info(f"Running in container: {' '.join(cmd_list)}")

    try:
        exit_code = run_container(
            image=image_name,
            command=cmd_list,
            volumes=volumes,
            read_only_volumes=read_only_volumes,
            privileged=True,
            working_dir=working_dir,
        )

        if exit_code != 0:
            raise BuildError(
                f"{component_name} build failed with exit code {exit_code}"
            )

        logger.info(f"{component_name} build in container complete")

    except RuntimeError as e:
        raise BuildError(f"Failed to run Docker container: {e}")
