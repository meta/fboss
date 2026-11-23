# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

"""Execute utilities for FBOSS image builder."""

import json
import logging
import os
import subprocess
import sys
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

    script = Path(command[0])
    source_dir = next(
        (
            (host / script.relative_to(container)).parent
            for host, container in volumes.items()
            if script.is_relative_to(container)
        ),
        None,
    )
    capture_script = next(
        (
            host / "component_version.py"
            for host, container in volumes.items()
            if container == Path("/fboss/oss/scripts")
        ),
        None,
    )
    producer_env: dict[str, str] = {}
    if source_dir is not None and capture_script is not None:
        try:
            producer_env = json.loads(
                subprocess.check_output(
                    [
                        sys.executable,
                        str(capture_script),
                        "environment",
                        "--source-dir",
                        str(source_dir),
                    ],
                    env={
                        name: value
                        for name, value in os.environ.items()
                        if name not in ("FBOSS_SOURCE_REVISION", "FBOSS_SOURCE_DIRTY")
                    },
                    text=True,
                )
            )
        except (OSError, subprocess.SubprocessError, ValueError) as error:
            raise BuildError(
                f"Failed to capture {component_name} producer identity: {error}"
            ) from error

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
            env=producer_env,
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
