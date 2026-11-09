"""Tests for check_copyright_headers.py."""

import tempfile
import unittest
from pathlib import Path

from fboss.oss.scripts import check_copyright_headers


class CheckCopyrightHeadersTest(unittest.TestCase):
    def check(self, contents: str) -> str | None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "example.cpp"
            path.write_text(contents, encoding="utf-8")
            return check_copyright_headers.check_header(path)

    def assert_error_contains(self, expected: str, contents: str) -> None:
        error = self.check(contents)
        if error is None:
            self.fail("expected the copyright header check to fail")
        self.assertIn(expected, error)

    def test_accepts_canonical_meta_header(self) -> None:
        self.assertIsNone(
            self.check("// Copyright (c) Meta Platforms, Inc. and affiliates.\n")
        )

    def test_rejects_external_holder_header(self) -> None:
        self.assert_error_contains(
            "must use the canonical Meta copyright header",
            "// Copyright (c) 2026 Example Corp.\n",
        )

    def test_rejects_private_header(self) -> None:
        contents = (
            "// (c) Meta Platforms, Inc. and affiliates. "
            "Confidential and proprietary.\n"
        )

        self.assertEqual(
            self.check(contents),
            "1: invalid copyright header: the internal-only "
            "'Confidential and proprietary' header is not allowed in OSS\n\n"
            "Expected:\n"
            "  // Copyright (c) Meta Platforms, Inc. and affiliates.\n\n"
            "Found:\n"
            "  // (c) Meta Platforms, Inc. and affiliates. Confidential and "
            "proprietary.\n\n"
            "Replace the existing copyright line with the expected header.",
        )

    def test_rejects_legacy_meta_header(self) -> None:
        self.assert_error_contains(
            "Facebook, Inc.",
            "// Copyright (c) 2004-present, Facebook, Inc.\n",
        )

    def test_rejects_meta_header_with_year(self) -> None:
        self.assert_error_contains(
            "must use only the canonical Meta copyright header",
            "// Copyright (c) 2026 Meta Platforms, Inc. and affiliates.\n",
        )

    def test_rejects_missing_header(self) -> None:
        error = self.check("int main() { return 0; }\n")
        if error is None:
            self.fail("expected the copyright header check to fail")
        self.assertIn("the required copyright header is missing", error)
        self.assertIn("Found:\n  <no copyright header found>", error)
        self.assertIn("Add the expected header at the top of the file.", error)
