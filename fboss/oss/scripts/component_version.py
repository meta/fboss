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

"""Capture producer identity and artifact evidence for OSS components."""

import argparse
import hashlib
import io
import json
import logging
import os
import re
import subprocess
import tarfile
import tempfile
from collections.abc import Mapping
from pathlib import Path

SCHEMA = 1
SECTION = ".fboss.build"
RECORD_DIRECTORY = "fboss_build_info"
DECLARATION_PATH = "share/fboss-component-version.json"
KERNEL_BUILD_INFO_PATH = ".kernel-build-info.json"
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


def checksum(path: Path | bytes) -> str:
    if isinstance(path, bytes):
        return hashlib.sha256(path).hexdigest()
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _has_git_metadata(directory: Path) -> bool:
    marker = directory / ".git"
    return marker.is_file() or (marker / "HEAD").is_file()


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
        source_dir = source_dir.resolve()
        repository = next(
            (
                parent
                for parent in (source_dir, *source_dir.parents)
                if _has_git_metadata(parent) or (parent / ".hg").is_dir()
            ),
            source_dir,
        )
        sapling = (repository / ".hg").is_dir() and not _has_git_metadata(repository)
        revision_command = (
            [
                "sl",
                "log",
                "-r",
                ".",
                "-T",
                "{node}",
                "--reason",
                "Capture producer revision - sl help log",
            ]
            if sapling
            else ["git", "-C", str(repository), "rev-parse", "HEAD"]
        )
        status_command = (
            [
                "sl",
                "status",
                "--reason",
                "Capture producer dirty state - sl help status",
            ]
            if sapling
            else [
                "git",
                "-C",
                str(repository),
                "status",
                "--porcelain",
                "--untracked-files=normal",
                "--",
                ".",
            ]
        )
        try:
            source_revision = subprocess.check_output(
                revision_command,
                cwd=repository,
                stderr=subprocess.DEVNULL,
                text=True,
                timeout=30,
            ).strip()
            if not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", source_revision):
                raise ValueError("source revision must be a full Git or Sapling hash")
            build_info["source_revision"] = source_revision
            if source_dirty is None:
                source_dirty = bool(
                    subprocess.check_output(
                        status_command,
                        cwd=repository,
                        stderr=subprocess.DEVNULL,
                        text=True,
                        timeout=30,
                    ).strip()
                )
        except (OSError, subprocess.SubprocessError, ValueError):
            logger.warning(
                "Producer revision or dirty state unavailable: %s", source_dir
            )
    if source_dirty is not None:
        build_info["source_dirty"] = source_dirty
    build_info["build_version"] = build_version or "dev"
    return build_info


def get_source_build_info_from_environment(
    source_dir: Path,
    source_revision: str | None = None,
    source_dirty: str | None = None,
    build_version: str | None = None,
) -> dict[str, object]:
    if source_revision is None:
        source_revision = os.environ.get("FBOSS_SOURCE_REVISION")
    if source_dirty is None:
        source_dirty = os.environ.get("FBOSS_SOURCE_DIRTY") or None
    if source_dirty not in (None, "true", "false"):
        raise ValueError("FBOSS_SOURCE_DIRTY must be true or false when supplied")
    if build_version is None:
        build_version = os.environ.get("FBOSS_BUILD_VERSION") or "dev"
    return get_source_build_info(
        source_dir,
        source_revision,
        None if source_dirty is None else source_dirty == "true",
        build_version,
    )


def get_source_build_environment(source_dir: Path) -> dict[str, str]:
    identity = get_source_build_info_from_environment(source_dir)
    env: dict[str, str] = {}
    if "source_revision" in identity:
        env["FBOSS_SOURCE_REVISION"] = str(identity["source_revision"])
    if "source_dirty" in identity:
        env["FBOSS_SOURCE_DIRTY"] = "true" if identity["source_dirty"] else "false"
    if os.environ.get("FBOSS_BUILD_VERSION"):
        env["FBOSS_BUILD_VERSION"] = str(identity["build_version"])
    return env


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
    output: Path,
    binaries: Mapping[str, object],
    producer: str,
    *,
    build: Mapping[str, object] | None = None,
    versions: Mapping[str, str] | None = None,
) -> None:
    """Write executable records or a component producer build record."""
    if not binaries and not build and not versions:
        return
    declaration: dict[str, object] = {
        "declaration_schema": SCHEMA,
        "producer": producer,
    }
    if binaries:
        declaration["binaries"] = binaries
    if build:
        declaration["build"] = build
    if versions:
        declaration["versions"] = versions
    write_if_changed(output, json.dumps(declaration, indent=2, sort_keys=True) + "\n")


def prepare_kernel_build(output_dir: Path, scripts_dir: Path) -> None:
    identity = get_source_build_info_from_environment(scripts_dir.parent)
    identity.pop("build_version")
    write_declaration(
        output_dir / KERNEL_BUILD_INFO_PATH,
        {},
        "build_kernel.sh",
        build={"fboss": identity},
    )


def _read_rpm_member(rpm: Path, member: str) -> bytes:
    with subprocess.Popen(["rpm2cpio", str(rpm)], stdout=subprocess.PIPE) as source:
        assert source.stdout is not None
        result = subprocess.run(
            ["cpio", "-i", "--to-stdout", "--quiet", member, "./" + member],
            stdin=source.stdout,
            capture_output=True,
            check=True,
        )
        source.stdout.close()
        if source.wait() or not result.stdout:
            raise ValueError(f"Could not read {member} from {rpm}")
    return result.stdout


def _query_rpm(rpm: Path, format: str) -> str:
    return subprocess.check_output(
        ["rpm", "-qp", "--qf", format, str(rpm)], text=True
    ).strip()


def _one_rpm(rpms: list[Path], prefix: str, suffix: str) -> Path:
    matches = [
        rpm for rpm in rpms if rpm.name.startswith(prefix) and rpm.name.endswith(suffix)
    ]
    if len(matches) != 1:
        raise ValueError(f"Expected one {prefix}*{suffix} RPM, found {len(matches)}")
    return matches[0]


def _kernel_source_version(makefile: str) -> str:
    numbers = []
    for field in ("VERSION", "PATCHLEVEL", "SUBLEVEL"):
        match = re.search(rf"^{field}\s*=\s*(\d+)\s*$", makefile, re.MULTILINE)
        if match is None:
            raise ValueError(f"Missing {field} in kernel source Makefile")
        numbers.append(match.group(1))
    extra = re.search(r"^EXTRAVERSION\s*=([^\n]*)", makefile, re.MULTILINE)
    return ".".join(numbers) + (extra.group(1).strip() if extra else "")


def _kernel_source_identity(
    spec: str, version: str, contents: bytes
) -> dict[str, object]:
    archive: dict[str, object] = {
        "name": f"linux-{version}.tar.xz",
        "sha256": checksum(contents),
    }
    source_url = re.search(r"^Source0:\s*(\S+)", spec, re.MULTILINE)
    if source_url:
        archive["declared_url"] = source_url.group(1).replace("%{version}", version)
    download_url = os.environ.get("KERNEL_SOURCE_DOWNLOAD_URL")
    if download_url:
        archive["download_url"] = download_url
    return {"version": version, "source_archive": archive}


def _kernel_build_string(core: Path, release: str) -> str | None:
    try:
        image = _read_rpm_member(core, f"boot/vmlinuz-{release}")
        if image[0x202:0x206] == b"HdrS" and len(image) >= 0x210:
            offset = int.from_bytes(image[0x20E:0x210], "little") + 0x200
            value = image[offset:].split(b"\0", 1)[0].decode()
            return value or None
    except (ValueError, subprocess.CalledProcessError):
        logger.warning("Kernel build string unavailable")
    return None


def _kernel_build_context(output_dir: Path) -> dict[str, object]:
    record = output_dir / KERNEL_BUILD_INFO_PATH
    if not record.is_file():
        return {}
    identity = json.loads(record.read_text()).get("build", {}).get("fboss")
    return {"fboss": identity} if identity else {}


def capture_kernel_build(output_dir: Path, scripts_dir: Path) -> None:
    rpms = sorted(output_dir.rglob("*.rpm"))
    if len({rpm.name for rpm in rpms}) != len(rpms):
        raise ValueError("Duplicate kernel RPM filenames")
    source = _one_rpm(rpms, "kernel-", ".src.rpm")
    core = _one_rpm(rpms, "kernel-core-", ".rpm")
    binary_rpms = [rpm for rpm in rpms if not rpm.name.endswith(".src.rpm")]
    for rpm in binary_rpms:
        if _query_rpm(rpm, "%{SOURCERPM}") != source.name:
            raise ValueError(
                f"{rpm.name} does not name {source.name} as its source RPM"
            )
    version = _query_rpm(source, "%{VERSION}")
    archive_name = f"linux-{version}.tar.xz"
    source_archive = _read_rpm_member(source, archive_name)
    with tarfile.open(fileobj=io.BytesIO(source_archive)) as archive:
        makefile = archive.extractfile(f"linux-{version}/Makefile")
        if (
            makefile is None
            or _kernel_source_version(makefile.read().decode()) != version
        ):
            raise ValueError("Kernel source version does not match its RPM")
    source_inputs = {
        name: _read_rpm_member(source, name)
        for name in (
            "kernel.spec",
            "fboss-reference.config",
            "fboss-local-overrides.config",
        )
    }
    inputs = {name: checksum(contents) for name, contents in source_inputs.items()}
    for name in ("prepare_config.sh", "merge_config.py"):
        inputs[name] = checksum(scripts_dir / name)
    build: dict[str, object] = {
        **_kernel_build_context(output_dir),
        "upstream": _kernel_source_identity(
            source_inputs["kernel.spec"].decode(), version, source_archive
        ),
        "inputs": inputs,
        # The current kernel.spec applies configuration overrides, not source patches.
        "patches": [],
        "source_rpm": {
            "name": source.name,
            "sha256": checksum(source),
        },
        "rpm_sha256": {rpm.name: checksum(rpm) for rpm in binary_rpms},
    }
    release = _query_rpm(core, "%{VERSION}-%{RELEASE}.%{ARCH}")
    config = _read_rpm_member(core, f"boot/config-{release}")
    build["config_sha256"] = checksum(config)
    build["config_file"] = f"boot/config-{release}"
    build["kernel_release"] = release
    build_string = _kernel_build_string(core, release)
    if build_string:
        build["kernel_build_string"] = build_string
    compiler = re.search(
        r'^CONFIG_CC_VERSION_TEXT="([^"\n]+)"$', config.decode(), re.MULTILINE
    )
    if compiler:
        build["compiler"] = compiler.group(1)
    build_version = os.environ.get("FBOSS_BUILD_VERSION")
    if build_version:
        build["build_version"] = build_version
    write_declaration(
        output_dir / "share/kernel-version.json",
        {},
        "build_kernel.sh",
        build=build,
        versions={"kernel": _query_rpm(core, "%{VERSION}-%{RELEASE}")},
    )
    (output_dir / KERNEL_BUILD_INFO_PATH).unlink(missing_ok=True)


def capture_bsp_build(
    output_dir: Path,
    source_dir: Path | None,
    kmod_name: str = "fboss_bsp_kmods",
    source_rpm: Path | None = None,
) -> None:
    rpms = [
        rpm for rpm in output_dir.glob("*.rpm") if not rpm.name.endswith(".src.rpm")
    ]
    prefix = kmod_name + "-"
    rpm = _one_rpm(rpms, prefix, ".rpm")
    name, version, release, arch = _query_rpm(
        rpm, "%{NAME}\n%{VERSION}\n%{RELEASE}\n%{ARCH}"
    ).splitlines()
    if not name.startswith(prefix) or name == prefix:
        raise ValueError("BSP RPM does not name its target kernel release")
    kernel_release = name.removeprefix(prefix)
    identity: dict[str, object] = {}
    if source_rpm is not None:
        source_name, source_version = _query_rpm(
            source_rpm, "%{NAME}\n%{VERSION}"
        ).splitlines()
        if source_name != kmod_name or source_version != version:
            raise ValueError("BSP binary RPM does not match the supplied source RPM")
        identity["source_rpm"] = {
            "name": source_rpm.name,
            "sha256": checksum(source_rpm),
        }
    elif source_dir is not None:
        identity.update(get_source_build_info(source_dir))
        identity.pop("build_version")
    else:
        raise ValueError("BSP source directory or source RPM is required")
    build: dict[str, object] = {
        **identity,
        "kernel_release": kernel_release,
        "rpm_sha256": {rpm.name: checksum(rpm)},
    }
    fboss = get_source_build_info_from_environment(Path(__file__).parent)
    fboss.pop("build_version")
    if fboss:
        build["fboss"] = fboss
    build_version = os.environ.get("FBOSS_BUILD_VERSION")
    if build_version:
        build["build_version"] = build_version
    write_declaration(
        output_dir / "share/bsp-version.json",
        {},
        "component_version.py",
        build=build,
        versions={
            "bsp": f"{version}-{release}",
            "kernel": kernel_release.removesuffix("." + arch),
        },
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    generate = commands.add_parser("generate")
    generate.add_argument("--source-dir", type=Path, required=True)
    generate.add_argument("--source-revision")
    generate.add_argument("--build-version")
    generate.add_argument("--source-dirty", choices=("true", "false"))
    generate.add_argument("--output-dir", type=Path, required=True)
    generate.add_argument("--target", action="append", required=True)
    environment = commands.add_parser("environment")
    environment.add_argument("--source-dir", type=Path, required=True)
    record = commands.add_parser("record")
    record.add_argument("--binary", type=Path, required=True)
    record.add_argument("--objcopy", default="objcopy")
    kernel = commands.add_parser(
        "kernel", help="Capture kernel producer and RPM identity"
    )
    kernel.add_argument("--output-dir", type=Path, required=True)
    kernel.add_argument("--scripts-dir", type=Path, required=True)
    kernel.add_argument("--prepare", action="store_true")
    bsp = commands.add_parser("bsp", help="Capture BSP producer and RPM identity")
    bsp.add_argument("--output-dir", type=Path, required=True)
    bsp.add_argument("--kmod-name", default="fboss_bsp_kmods")
    bsp_source = bsp.add_mutually_exclusive_group(required=True)
    bsp_source.add_argument("--source-dir", type=Path)
    bsp_source.add_argument("--source-rpm", type=Path)
    args = parser.parse_args()
    if args.command == "generate":
        try:
            build_info = get_source_build_info_from_environment(
                args.source_dir,
                args.source_revision,
                args.source_dirty,
                args.build_version,
            )
        except ValueError as error:
            parser.error(str(error))
        generate_build_info_sources(
            args.output_dir,
            args.target,
            build_info,
        )
    elif args.command == "environment":
        print(json.dumps(get_source_build_environment(args.source_dir)))
    elif args.command == "kernel":
        if args.prepare:
            prepare_kernel_build(args.output_dir, args.scripts_dir)
        else:
            capture_kernel_build(args.output_dir, args.scripts_dir)
    elif args.command == "bsp":
        capture_bsp_build(
            args.output_dir, args.source_dir, args.kmod_name, args.source_rpm
        )
    else:
        write_binary_build_info(args.binary, args.objcopy)


if __name__ == "__main__":
    main()
