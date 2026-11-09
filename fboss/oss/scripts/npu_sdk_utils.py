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

from __future__ import annotations

import argparse
import fcntl
import json
import os
import pathlib
import stat
import tempfile
from collections.abc import Mapping, Sequence
from datetime import datetime, timezone


READ_ONLY_MODE = stat.S_IRUSR | stat.S_IRGRP | stat.S_IROTH
NPU_HW_AGENT_BINARY = "fboss_hw_agent-sai_impl"
NPU_SDK_METADATA_FILENAME = "npu_sdk_metadata.json"


def _load_json_object(path: pathlib.Path, description: str) -> dict[str, object]:
    try:
        contents = json.loads(path.read_text())
    except FileNotFoundError as error:
        raise FileNotFoundError(f"{description} not found: {path}") from error
    except json.JSONDecodeError as error:
        raise ValueError(f"Invalid {description} JSON: {path}") from error
    if not isinstance(contents, dict):
        raise ValueError(f"{description} must be a JSON object: {path}")
    return contents


def _read_metadata(path: pathlib.Path) -> dict[str, object]:
    if not path.exists():
        return {}
    return _load_json_object(path, "NPU SDK metadata")


def _write_metadata(path: pathlib.Path, metadata: Mapping[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_path = tempfile.mkstemp(
        dir=path.parent,
        prefix=f".{path.name}.",
        text=True,
    )
    try:
        with os.fdopen(descriptor, "w") as output:
            json.dump(metadata, output, indent=2, sort_keys=True)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.chmod(temporary_path, READ_ONLY_MODE)
        os.replace(temporary_path, path)
    finally:
        if os.path.exists(temporary_path):
            os.unlink(temporary_path)


def _update_metadata(
    path: pathlib.Path,
    binary_name: str,
    entry: Mapping[str, object] | None,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lock_path = path.with_name(f".{path.name}.lock")
    with lock_path.open("a+") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        metadata = _read_metadata(path)
        if entry is None:
            if binary_name not in metadata:
                return
            del metadata[binary_name]
        else:
            metadata[binary_name] = dict(entry)

        if metadata:
            _write_metadata(path, metadata)
        elif path.exists():
            path.unlink()


def record_binary_metadata(
    path: pathlib.Path,
    binary_name: str,
    npu_sai_impl: str,
    npu_sai_sdk_selector: str,
    asic_sdk_version: str,
    sai_sdk_version: str,
    built_at: str | None = None,
) -> None:
    _update_metadata(
        path,
        binary_name,
        {
            "npuSaiImpl": npu_sai_impl,
            "npuSaiSdkSelector": npu_sai_sdk_selector,
            "sdkVersion": {
                "asicSdk": asic_sdk_version,
                "saiSdk": sai_sdk_version,
            },
            "builtAt": built_at
            or datetime.now(timezone.utc)
            .isoformat(timespec="seconds")
            .replace("+00:00", "Z"),
        },
    )


def remove_binary_metadata(path: pathlib.Path, binary_name: str) -> None:
    _update_metadata(path, binary_name, None)


def load_binary_sdk_version(
    metadata_path: pathlib.Path, binary_name: str
) -> dict[str, str]:
    """Return the validated config-facing SDK versions for one executable."""
    metadata = _load_json_object(metadata_path, "NPU SDK metadata")
    entry = metadata.get(binary_name)
    if not isinstance(entry, dict):
        raise ValueError(
            f"NPU SDK metadata has no entry for binary '{binary_name}': {metadata_path}"
        )

    sdk_version = entry.get("sdkVersion")
    if not isinstance(sdk_version, dict):
        raise ValueError(
            f"NPU SDK metadata entry for '{binary_name}' has no sdkVersion object"
        )

    result = {}
    for field in ("asicSdk", "saiSdk"):
        value = sdk_version.get(field)
        if not isinstance(value, str) or not value:
            raise ValueError(
                f"NPU SDK metadata entry for '{binary_name}' has invalid {field}"
            )
        result[field] = value
    return result


def materialize_agent_config(
    config_path: pathlib.Path,
    metadata_path: pathlib.Path,
    binary_name: str,
    output_directory: pathlib.Path,
) -> pathlib.Path:
    """Write an adjusted config copy while leaving the supplied config untouched."""
    config = _load_json_object(config_path, "agent config")
    sw_config = config.get("sw")
    if not isinstance(sw_config, dict):
        raise ValueError(f"Agent config has no sw object: {config_path}")

    sw_config["sdkVersion"] = load_binary_sdk_version(metadata_path, binary_name)
    output_path = output_directory / config_path.name
    with output_path.open("w") as output:
        json.dump(config, output, indent=2)
        output.write("\n")
    return output_path


def _create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Manage NPU SDK metadata and apply it to FBOSS configs."
    )
    subparsers = parser.add_subparsers(dest="operation", required=True)

    record = subparsers.add_parser("record")
    record.add_argument("--metadata-path", type=pathlib.Path, required=True)
    record.add_argument("--binary-name", required=True)
    record.add_argument("--npu-sai-impl", required=True)
    record.add_argument("--npu-sai-sdk-selector", required=True)
    record.add_argument("--asic-sdk-version", required=True)
    record.add_argument("--sai-sdk-version", required=True)

    remove = subparsers.add_parser("remove")
    remove.add_argument("--metadata-path", type=pathlib.Path, required=True)
    remove.add_argument("--binary-name", required=True)

    materialize = subparsers.add_parser("materialize-agent-config")
    materialize.add_argument("--config-path", type=pathlib.Path, required=True)
    materialize.add_argument("--metadata-path", type=pathlib.Path, required=True)
    materialize.add_argument("--binary-name", required=True)
    materialize.add_argument("--output-directory", type=pathlib.Path, required=True)

    return parser


def main(argv: Sequence[str] | None = None) -> None:
    args = _create_parser().parse_args(argv)
    if args.operation == "record":
        record_binary_metadata(
            path=args.metadata_path,
            binary_name=args.binary_name,
            npu_sai_impl=args.npu_sai_impl,
            npu_sai_sdk_selector=args.npu_sai_sdk_selector,
            asic_sdk_version=args.asic_sdk_version,
            sai_sdk_version=args.sai_sdk_version,
        )
    elif args.operation == "remove":
        remove_binary_metadata(args.metadata_path, args.binary_name)
    else:
        materialize_agent_config(
            config_path=args.config_path,
            metadata_path=args.metadata_path,
            binary_name=args.binary_name,
            output_directory=args.output_directory,
        )


if __name__ == "__main__":
    main()
