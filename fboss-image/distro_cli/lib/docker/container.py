# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

"""Docker container lifecycle management."""

import logging
import subprocess
from pathlib import Path

logger = logging.getLogger(__name__)


def run_container(  # noqa: PLR0913
    image: str,
    command: list[str],
    volumes: dict[Path, Path] | None = None,
    read_only_volumes: dict[Path, Path] | None = None,
    env: dict[str, str] | None = None,
    privileged: bool = False,
    interactive: bool = False,
    ephemeral: bool = True,
    detach: bool = False,
    working_dir: str | None = None,
    name: str | None = None,
) -> int:
    """Run a command in a Docker container.

    Args:
        image: Docker image name to run
        command: Command to execute in container (as list)
        volumes: Dictionary mapping host paths to container paths
        read_only_volumes: Dictionary mapping host paths to container paths,
            mounted read-only
        env: Dictionary of environment variables
        privileged: Run container in privileged mode
        interactive: Run container in interactive mode (-it)
        ephemeral: Remove container after it exits (--rm, default: True)
        working_dir: Working directory inside container
        name: Name for the container

    Returns:
        Exit code from the container

    Raises:
        RuntimeError: If docker command fails to start
    """
    logger.info(f"Running container from image: {image}")
    logger.info(f"Executing: {command}")

    # Build docker run command
    cmd = ["docker", "run"]

    # Start with default flags
    cmd.append("--network=host")

    # Podman caps container pids at 2048 by default (Docker does not cap);
    # parallel dep builds routinely exceed that with compiler subprocess
    # trees, failing spawns with EAGAIN. Remove the cap.
    cmd.extend(["--pids-limit", "0"])

    # Add flags
    if ephemeral:
        cmd.append("--rm")

    if interactive:
        cmd.extend(["-i", "-t"])

    if detach:
        cmd.append("-d")

    if privileged:
        cmd.append("--privileged")

    if name:
        cmd.extend(["--name", name])

    if working_dir:
        cmd.extend(["-w", working_dir])

    if volumes:
        for host_path, container_path in volumes.items():
            cmd.extend(["-v", f"{host_path}:{container_path}"])

    if read_only_volumes:
        for host_path, container_path in read_only_volumes.items():
            cmd.extend(["-v", f"{host_path}:{container_path}:ro"])

    if env:
        for key, value in env.items():
            cmd.extend(["-e", f"{key}={value}"])

    cmd.append(image)

    # Finally, add the actual command to run
    cmd.extend(command)

    logger.debug(f"Running: {' '.join(str(c) for c in cmd)}")

    try:
        result = subprocess.run(
            cmd,
            check=False,  # Don't raise on non-zero exit
        )
        logger.info(f"Container exited with code: {result.returncode}")
        return result.returncode
    except FileNotFoundError as e:
        raise RuntimeError(
            "Docker command not found. Is Docker installed and in PATH?"
        ) from e
    except subprocess.CalledProcessError as e:
        logger.error(f"Command failed: {e}")
        return e.returncode


def exec_in_container(
    name: str,
    command: list[str],
) -> tuple[int, str, str]:
    """Execute a command in a running Docker container.

    Args:
        name: Name of the container
        command: Command to execute in container (as list)

    Returns:
        Tuple of exit code, stdout, and stderr from the command execution

    Raises:
        RuntimeError: If docker command fails
    """
    logger.info(f"Executing command in container {name}: {command}")

    cmd = ["docker", "exec", name]
    cmd.extend(command)

    logger.debug(f"Running: {' '.join(str(c) for c in cmd)}")

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=True,
        )
        return result.returncode, result.stdout, result.stderr
    except FileNotFoundError:
        raise RuntimeError("Docker command not found. Is Docker installed and in PATH?")
    except subprocess.CalledProcessError as e:
        logger.error(f"Command failed: {e}")
        return e.returncode, e.stdout, e.stderr


def container_is_running(name: str) -> bool:
    """Check if a Docker container is running.

    Args:
        name: Name of the container

    Returns:
        True if container is running, False otherwise

    Raises:
        RuntimeError: If docker command fails
    """
    logger.info(f"Checking if container is running: {name}")

    cmd = ["docker", "ps", "-aq", "--filter", f"name={name}"]

    logger.debug(f"Running: {' '.join(str(c) for c in cmd)}")

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=True,
        )
        return result.returncode == 0 and bool(result.stdout.strip())
    except FileNotFoundError:
        raise RuntimeError("Docker command not found. Is Docker installed and in PATH?")
    except subprocess.CalledProcessError as e:
        logger.error(f"Check command failed: {e}")
        return False


def stop_container(name: str) -> int:
    """Stop a Docker container.

    Args:
        name: Name of the container

    Returns:
        Exit code from the stop command

    Raises:
        RuntimeError: If docker command fails
    """
    logger.info(f"Stopping container: {name}")

    cmd = ["docker", "stop", name]

    logger.debug(f"Running: {' '.join(str(c) for c in cmd)}")

    try:
        result = subprocess.run(cmd, check=True)
        logger.info(f"Stop command exited with code: {result.returncode}")
        return result.returncode
    except FileNotFoundError as e:
        raise RuntimeError(
            "Docker command not found. Is Docker installed and in PATH?"
        ) from e
    except subprocess.CalledProcessError as e:
        logger.error(f"Stop command failed: {e}")
        return e.returncode


def remove_container(name: str) -> int:
    """Remove a Docker container.

    Args:
        name: Name of the container

    Returns:
        Exit code from the remove command

    Raises:
        RuntimeError: If docker command fails
    """
    logger.info(f"Removing container: {name}")

    cmd = ["docker", "rm", "-f", name]

    logger.debug(f"Running: {' '.join(str(c) for c in cmd)}")

    try:
        result = subprocess.run(cmd, check=True)
        logger.info(f"Remove command exited with code: {result.returncode}")
        return result.returncode
    except FileNotFoundError as e:
        raise RuntimeError(
            "Docker command not found. Is Docker installed and in PATH?"
        ) from e
    except subprocess.CalledProcessError as e:
        logger.error(f"Remove command failed: {e}")
        return e.returncode


def stop_and_remove_container(name: str) -> int:
    """Stop and remove a Docker container.

    Args:
        name: Name of the container

    Returns:
        Exit code from the remove command

    Raises:
        RuntimeError: If docker command fails
    """
    logger.info(f"Stopping and removing container: {name}")
    stop_exit_code = stop_container(name)
    if stop_exit_code != 0:
        return stop_exit_code
    return remove_container(name)
