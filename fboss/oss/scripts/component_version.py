# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Embed source identity in OSS executables and record their checksums."""

import argparse
import hashlib
import json
import logging
import os
import re
import subprocess
import tempfile
from collections.abc import Mapping
from pathlib import Path

SCHEMA = 1
SECTION = ".fboss.build"
RECORD_DIRECTORY = "fboss_build_info"
DECLARATION_PATH = "share/fboss-component-version.json"
logger = logging.getLogger(__name__)


def write_if_changed(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_file() and path.read_text() == content:
        return
    descriptor, temporary = tempfile.mkstemp(dir=path.parent)
    try:
        with os.fdopen(descriptor, "w") as output:
            output.write(content)
        os.chmod(temporary, 0o444)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def checksum(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def get_source_build_info(
    source_dir: Path,
    source_revision: str | None = None,
    source_dirty: bool | None = None,
    build_version: str = "dev",
) -> dict[str, object]:
    build_info: dict[str, object] = {}
    if source_revision:
        if not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", source_revision):
            raise ValueError("source revision must be a full Git or Sapling hash")
        build_info["source_revision"] = source_revision
    else:
        try:
            source_revision = subprocess.check_output(
                ["git", "-C", str(source_dir), "rev-parse", "HEAD"],
                stderr=subprocess.DEVNULL,
                text=True,
            ).strip()
            build_info["source_revision"] = source_revision
            if source_dirty is None:
                source_dirty = bool(
                    subprocess.check_output(
                        [
                            "git",
                            "-C",
                            str(source_dir),
                            "status",
                            "--porcelain",
                            "--untracked-files=normal",
                            "--",
                            ".",
                        ],
                        text=True,
                    ).strip()
                )
        except (OSError, subprocess.CalledProcessError):
            logger.warning("Source revision unavailable; supply --source-revision")
    if source_dirty is not None:
        build_info["source_dirty"] = source_dirty
    build_info["build_version"] = build_version or "dev"
    return build_info


def generate_build_info_sources(
    output_dir: Path, targets: list[str], source_build_info: Mapping[str, object]
) -> None:
    for target in targets:
        if not re.fullmatch(r"[A-Za-z0-9_.+-]+", target):
            raise ValueError(f"Invalid executable name: {target}")
        build_info = {"schema": SCHEMA, "build_target": target, **source_build_info}
        build_info_json = json.dumps(build_info, sort_keys=True)
        cpp_string_literal = json.dumps(build_info_json)
        write_if_changed(
            output_dir / f"{target}.cpp",
            'extern "C" {\n'
            f'__attribute__((used, section("{SECTION}"), visibility("hidden")))\n'
            f"extern const char fboss_build_info[] = {cpp_string_literal};\n"
            "}\n",
        )


def read_embedded_build_info(
    binary: Path, objcopy: str = "objcopy"
) -> dict[str, object]:
    with tempfile.TemporaryDirectory() as temporary:
        build_info_file = Path(temporary) / "build_info.json"
        subprocess.run(
            [
                objcopy,
                f"--dump-section={SECTION}={build_info_file}",
                f"--only-section={SECTION}",
                str(binary.resolve()),
                str(Path(temporary) / "identity.elf"),
            ],
            check=True,
            capture_output=True,
        )
        if not build_info_file.is_file():
            raise ValueError(f"Missing {SECTION} section in {binary}")
        build_info = json.loads(build_info_file.read_bytes().rstrip(b"\0"))
    if not isinstance(build_info, dict) or build_info.get("schema") != SCHEMA:
        raise ValueError(f"Invalid embedded build identity in {binary}")
    if build_info.get("build_target") != binary.name:
        raise ValueError(f"Build identity does not match executable {binary.name}")
    return build_info


def write_binary_build_info(binary: Path, objcopy: str = "objcopy") -> None:
    build_info = read_embedded_build_info(binary, objcopy)
    build_info["sha256"] = checksum(binary)
    record_path = binary.parent / RECORD_DIRECTORY / f"{binary.name}.json"
    write_if_changed(
        record_path, json.dumps(build_info, indent=2, sort_keys=True) + "\n"
    )


def collect_binary_records(
    contents: Mapping[Path, str],
    records_dir: Path | None = None,
) -> dict[str, dict[str, object]]:
    records: dict[str, dict[str, object]] = {}
    for binary, destination in contents.items():
        if not destination.startswith("bin/") or not binary.is_file():
            continue
        directory = records_dir or binary.parent / RECORD_DIRECTORY
        path = directory / f"{binary.name}.json"
        if not path.is_file():
            continue
        record = json.loads(path.read_text())
        if (
            not isinstance(record, dict)
            or record.get("schema") != SCHEMA
            or record.get("build_target") != binary.name
            or record.get("sha256") != checksum(binary)
        ):
            raise ValueError(f"Stale or invalid build metadata for {binary}")
        records[Path(destination).name] = record
    return records


def write_declaration(
    output: Path, binaries: Mapping[str, object], producer: str
) -> None:
    if not binaries:
        return
    declaration = {
        "declaration_schema": SCHEMA,
        "producer": producer,
        "binaries": binaries,
    }
    write_if_changed(output, json.dumps(declaration, indent=2, sort_keys=True) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    generate = commands.add_parser("generate")
    generate.add_argument("--source-dir", type=Path, required=True)
    generate.add_argument(
        "--source-revision", default=os.environ.get("FBOSS_SOURCE_REVISION")
    )
    generate.add_argument(
        "--build-version", default=os.environ.get("FBOSS_BUILD_VERSION") or "dev"
    )
    generate.add_argument(
        "--source-dirty",
        choices=("true", "false"),
        default=os.environ.get("FBOSS_SOURCE_DIRTY"),
    )
    generate.add_argument("--output-dir", type=Path, required=True)
    generate.add_argument("--target", action="append", required=True)
    record = commands.add_parser("record")
    record.add_argument("--binary", type=Path, required=True)
    record.add_argument("--objcopy", default="objcopy")
    args = parser.parse_args()
    if args.command == "generate":
        if args.source_dirty not in (None, "true", "false"):
            parser.error("FBOSS_SOURCE_DIRTY must be true or false when supplied")
        source_dirty = (
            None if args.source_dirty is None else args.source_dirty == "true"
        )
        generate_build_info_sources(
            args.output_dir,
            args.target,
            get_source_build_info(
                args.source_dir, args.source_revision, source_dirty, args.build_version
            ),
        )
    else:
        write_binary_build_info(args.binary, args.objcopy)


if __name__ == "__main__":
    main()
