#!/usr/bin/env python3

"""Check copyright headers in newly added FBOSS C and C++ files."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Iterable, Optional, Sequence


_SOURCE_EXTENSIONS = frozenset({".c", ".cc", ".cpp", ".h", ".hh", ".hpp"})
_GENERATED_OR_VENDOR_DIRECTORIES = frozenset(
    {"gen-cpp2", "generated", "third-party", "third_party", "vendor"}
)
_HEADER_LINE_LIMIT = 30
_META_HEADER = "Copyright (c) Meta Platforms, Inc. and affiliates."
_EXPECTED_HEADER = f"// {_META_HEADER}"
_GIT_REVISION_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._/@{}~^:+-]*$")


def _normalized_comment(line: str) -> str:
    line = re.sub(r"^\s*(?://+|/\*+|\*+)\s*", "", line)
    return re.sub(r"\s*\*/\s*$", "", line).strip()


def _is_candidate(path: Path) -> bool:
    return path.suffix.lower() in _SOURCE_EXTENSIONS and not any(
        part in _GENERATED_OR_VENDOR_DIRECTORIES for part in path.parts
    )


def _invalid_header(line_number: int, reason: str, found: Optional[str]) -> str:
    found_display = found if found is not None else "<no copyright header found>"
    action = (
        "Replace the existing copyright line with the expected header."
        if found is not None
        else "Add the expected header at the top of the file."
    )
    return (
        f"{line_number}: invalid copyright header: {reason}\n\n"
        f"Expected:\n  {_EXPECTED_HEADER}\n\n"
        f"Found:\n  {found_display}\n\n"
        f"{action}"
    )


def check_header(path: Path) -> Optional[str]:
    """Return an error message when path does not have an approved header."""
    try:
        with path.open(encoding="utf-8") as source:
            header = [
                (line_number, line.rstrip("\r\n"), _normalized_comment(line))
                for line_number, line in zip(range(1, _HEADER_LINE_LIMIT + 1), source)
            ]
    except (OSError, UnicodeError) as error:
        return f"could not read file: {error}"

    private_line = next(
        (
            entry
            for entry in header
            if "confidential and proprietary" in entry[2].lower()
        ),
        None,
    )
    if private_line is not None:
        return _invalid_header(
            private_line[0],
            "the internal-only 'Confidential and proprietary' header is not allowed in OSS",
            private_line[1].strip(),
        )

    copyright_lines = [
        entry for entry in header if entry[2].lower().startswith("copyright")
    ]
    legacy_line = next(
        (entry for entry in copyright_lines if "facebook, inc." in entry[2].lower()),
        None,
    )
    if legacy_line is not None:
        return _invalid_header(
            legacy_line[0],
            "the legacy 'Facebook, Inc.' copyright owner is not allowed",
            legacy_line[1].strip(),
        )

    meta_lines = [
        entry for entry in copyright_lines if "meta platforms" in entry[2].lower()
    ]
    if meta_lines:
        if all(entry[2] == _META_HEADER for entry in copyright_lines):
            return None
        unexpected_line = next(
            entry for entry in copyright_lines if entry[2] != _META_HEADER
        )
        return _invalid_header(
            unexpected_line[0],
            "new FBOSS OSS files must use only the canonical Meta copyright header",
            unexpected_line[1].strip(),
        )

    if copyright_lines:
        return _invalid_header(
            copyright_lines[0][0],
            "new FBOSS OSS files must use the canonical Meta copyright header",
            copyright_lines[0][1].strip(),
        )

    return _invalid_header(
        1,
        "the required copyright header is missing",
        None,
    )


def _git_added_files(from_ref: Optional[str], to_ref: Optional[str]) -> set[str]:
    if bool(from_ref) != bool(to_ref):
        raise ValueError("--from-ref and --to-ref must be specified together")
    for revision in (from_ref, to_ref):
        if revision and not _GIT_REVISION_PATTERN.fullmatch(revision):
            raise ValueError(f"invalid Git revision: {revision!r}")

    command = ["git", "diff", "--diff-filter=A", "--name-only", "-z"]
    if from_ref and to_ref:
        command.append(f"{from_ref}...{to_ref}")
    else:
        command.append("--cached")
    command.append("--")

    result = subprocess.run(command, check=True, capture_output=True)
    return {path.decode("utf-8") for path in result.stdout.split(b"\0") if path}


def _files_to_check(
    filenames: Iterable[str],
    from_ref: Optional[str],
    to_ref: Optional[str],
    check_all: bool,
) -> list[Path]:
    supplied = {Path(filename).as_posix() for filename in filenames}
    selected = supplied if check_all else supplied & _git_added_files(from_ref, to_ref)
    return sorted(
        (Path(filename) for filename in selected if _is_candidate(Path(filename))),
        key=lambda path: path.as_posix(),
    )


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--from-ref",
        default=os.environ.get("PRE_COMMIT_FROM_REF"),
        help="base revision used to find added files (requires --to-ref)",
    )
    parser.add_argument(
        "--to-ref",
        default=os.environ.get("PRE_COMMIT_TO_REF"),
        help="target revision used to find added files (requires --from-ref)",
    )
    parser.add_argument(
        "--check-all",
        action="store_true",
        help="check every supplied file instead of only newly added files",
    )
    parser.add_argument("filenames", nargs="*")
    args = parser.parse_args(argv)

    try:
        paths = _files_to_check(
            args.filenames,
            args.from_ref,
            args.to_ref,
            args.check_all,
        )
    except (subprocess.CalledProcessError, ValueError) as error:
        print(f"ERROR: could not determine newly added files: {error}", file=sys.stderr)
        return 2

    errors = []
    for path in paths:
        if message := check_header(path):
            errors.append(f"{path}:{message}")

    if errors:
        print("FBOSS copyright header check failed:\n", file=sys.stderr)
        for error in errors:
            print(f"{error}\n", file=sys.stderr)
        print(
            "Rerun pre-commit after correcting the file.\n"
            "See CONTRIBUTING.md#copyright-headers.",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
