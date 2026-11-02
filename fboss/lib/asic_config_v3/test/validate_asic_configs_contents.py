#!/usr/bin/env python3
# pyre-strict

"""Validate ASIC config v3 contents against checked-in internal references.

Usage (internal, from fbcode/):
    buck2 run //fboss/lib/asic_config_v3/test:validate_asic_configs_contents

Usage (OSS, from the root of the fboss repository):
    ./fboss/lib/asic_config_v3/validate-contents-helper.sh

Both print a per-variant comparison table and a summary counting BYTE_MATCH,
SEMANTIC_MATCH and CONTENT_MISMATCH variants. Note that a bare
``python3 -m ...`` invocation does not work: the generators import
thrift-python modules that only exist once buck or cmake has run codegen.
"""

import argparse
import difflib
import io
import json
import os
import sys
import unittest
from enum import Enum
from typing import Any, ClassVar, NamedTuple, Optional
from unittest.mock import patch

import yaml
from yaml.nodes import MappingNode, ScalarNode, SequenceNode

from fboss.lib.asic_config_v3.base_generator import resolve_variant_config
from fboss.lib.asic_config_v3.gen import get_generator, get_output_filename
from fboss.lib.asic_config_v3.paths import AsicConfigPaths, discover_platforms


class ComparisonResult(Enum):
    BYTE_MATCH = "BYTE_MATCH"
    SEMANTIC_MATCH = "SEMANTIC_MATCH"
    CONTENT_MISMATCH = "CONTENT_MISMATCH"


class ComparisonRecord(NamedTuple):
    platform: str
    variant: str
    reference: str
    result: str


class ContentMismatch(NamedTuple):
    record: ComparisonRecord
    generated: str
    reference: str


class VariantVerification(NamedTuple):
    record: ComparisonRecord
    mismatch: Optional[ContentMismatch] = None
    stale_generated_file: Optional[str] = None
    error: Optional[str] = None


class GeneratedOutput(NamedTuple):
    filename: str
    contents: str
    stale_generated_file: Optional[str] = None


class VerificationResults(NamedTuple):
    records: list[ComparisonRecord]
    mismatches: list[ContentMismatch]
    stale_generated_files: list[str]
    errors: list[str]


class TestValidateAsicConfigV3Contents(unittest.TestCase):
    """Validate ASIC config v3 output against internal reference contents."""

    _FBOSS_ROOT = "fboss"
    _GENERATE_FRESH_OUTPUT = True
    _SYNCED_ASIC_CONFIG_DIR = os.path.join(
        "lib", "asic_config_v2", "synced_asic_configs"
    )
    # Some variants have no standalone synced ASIC config; their source of
    # truth is the YAML embedded in a materialized agent config. Keyed by the
    # variant's effective ``config_gen_type`` so that any platform declaring
    # one of these gen types is picked up without editing this table.
    _MATERIALIZED_REFERENCE_DIRS: ClassVar[dict[str, str]] = {
        "HW_TEST": os.path.join("oss", "hw_test_configs"),
        "BENCHMARK": os.path.join("oss", "hw_bench_configs"),
        "LINK_TEST": os.path.join("oss", "link_test_configs"),
    }
    _MATERIALIZED_REFERENCE_SUFFIX = ".agent.materialized_JSON"
    _MATERIALIZED_REFERENCE_LINK_TEST_SUFFIX = ".materialized_JSON"
    _MATERIALIZED_YAML_PATH = ("platform", "chip", "asicConfig", "common", "yamlConfig")
    _MAX_DIFF_LINES = 200

    def _reference_filename(self, platform: str, variant: str, extension: str) -> str:
        suffix = "" if variant == "default" else f"_{variant}"
        return f"{platform}{suffix}{extension}"

    def _read_materialized_reference(self, path: str) -> str:
        with open(path, encoding="utf-8") as f:
            value: Any = json.load(f)

        for key in self._MATERIALIZED_YAML_PATH:
            if not isinstance(value, dict) or key not in value:
                raise ValueError(
                    f"Materialized config {path} does not contain "
                    f"{'.'.join(self._MATERIALIZED_YAML_PATH)}"
                )
            value = value[key]

        if not isinstance(value, str):
            raise ValueError(f"Materialized config {path} has a non-string YAML config")
        return value

    def _materialized_reference_path(
        self, platform: str, variant: str, platform_config: dict[str, Any]
    ) -> Optional[str]:
        """Reference path for variants sourced from a materialized agent config.

        Returns None for variants that compare against a synced ASIC config.
        """
        variant_config = resolve_variant_config(platform_config, variant)
        config_gen_type = variant_config.get("asic_config_params", {}).get(
            "config_gen_type", ""
        )
        reference_dir = self._MATERIALIZED_REFERENCE_DIRS.get(config_gen_type)
        if reference_dir is None:
            return None
        suffix = (
            self._MATERIALIZED_REFERENCE_LINK_TEST_SUFFIX
            if config_gen_type == "LINK_TEST"
            else self._MATERIALIZED_REFERENCE_SUFFIX
        )
        return os.path.join(reference_dir, f"{platform}{suffix}")

    def _read_reference(
        self,
        paths: AsicConfigPaths,
        materialized_relative_path: Optional[str],
        platform: str,
        variant: str,
        output_filename: str,
    ) -> tuple[str, str]:
        if materialized_relative_path:
            materialized_path = os.path.join(
                paths.fboss_root, materialized_relative_path
            )
            return materialized_relative_path, self._read_materialized_reference(
                materialized_path
            )

        reference_extension = os.path.splitext(output_filename)[1]
        if reference_extension == ".json":
            reference_extension = ".materialized_JSON"
        reference_filename = self._reference_filename(
            platform, variant, reference_extension
        )
        synced_relative_path = os.path.join(
            self._SYNCED_ASIC_CONFIG_DIR, reference_filename
        )
        synced_path = os.path.join(paths.fboss_root, synced_relative_path)
        with open(synced_path, encoding="utf-8") as f:
            return synced_relative_path, f.read()

    def _canonicalize_yaml_node(self, node: Any) -> tuple[Any, ...]:
        """Ignore mapping order while preserving YAML structure and scalar types."""
        if isinstance(node, ScalarNode):
            return ("scalar", node.tag, node.value)
        if isinstance(node, SequenceNode):
            return (
                "sequence",
                node.tag,
                tuple(self._canonicalize_yaml_node(child) for child in node.value),
            )
        if isinstance(node, MappingNode):
            entries = [
                (self._canonicalize_yaml_node(key), self._canonicalize_yaml_node(value))
                for key, value in node.value
            ]
            return ("mapping", node.tag, tuple(sorted(entries, key=repr)))
        raise TypeError(f"Unsupported YAML node type: {type(node).__name__}")

    def _canonicalize_yaml(self, contents: str) -> tuple[tuple[Any, ...], ...]:
        return tuple(
            self._canonicalize_yaml_node(document)
            for document in yaml.compose_all(contents)
        )

    def _compare(self, generated: str, reference: str) -> ComparisonResult:
        if generated == reference:
            return ComparisonResult.BYTE_MATCH
        if self._canonicalize_yaml(generated) == self._canonicalize_yaml(reference):
            return ComparisonResult.SEMANTIC_MATCH
        return ComparisonResult.CONTENT_MISMATCH

    def _print_summary(self, records: list[ComparisonRecord]) -> None:
        headers = ("Platform", "Variant", "Reference", "Result")
        rows = [
            (
                record.platform,
                record.variant or "<default>",
                record.reference,
                record.result,
            )
            for record in records
        ]
        widths = [
            max(len(headers[index]), *(len(row[index]) for row in rows))
            for index in range(len(headers))
        ]

        print("\nASIC config v3 comparison results:\n", file=sys.stderr)
        print(
            "  ".join(
                header.ljust(widths[index]) for index, header in enumerate(headers)
            ),
            file=sys.stderr,
        )
        print("  ".join("-" * width for width in widths), file=sys.stderr)
        for row in rows:
            print(
                "  ".join(
                    value.ljust(widths[index]) for index, value in enumerate(row)
                ),
                file=sys.stderr,
            )

        counts = {
            result.value: sum(record.result == result.value for record in records)
            for result in ComparisonResult
        }
        print("\nSummary:", file=sys.stderr)
        print(
            f"  Platforms: {len({record.platform for record in records})}",
            file=sys.stderr,
        )
        print(f"  Variants: {len(records)}", file=sys.stderr)
        for result in ComparisonResult:
            print(f"  {result.value}: {counts[result.value]}", file=sys.stderr)

    def _print_content_mismatches(self, mismatches: list[ContentMismatch]) -> None:
        if not mismatches:
            return

        print("\nCONTENT_MISMATCH platform variants:", file=sys.stderr)
        for mismatch in mismatches:
            variant = mismatch.record.variant or "<default>"
            print(
                f"  - {mismatch.record.platform}/{variant}\n"
                f"    Reference: {mismatch.record.reference}",
                file=sys.stderr,
            )
            diff = list(
                difflib.unified_diff(
                    mismatch.reference.splitlines(),
                    mismatch.generated.splitlines(),
                    fromfile=mismatch.record.reference,
                    tofile=f"asic_config_v3:{mismatch.record.platform}/{variant}",
                    lineterm="",
                )
            )
            for line in diff[: self._MAX_DIFF_LINES]:
                print(f"    {line}", file=sys.stderr)
            if len(diff) > self._MAX_DIFF_LINES:
                print(
                    f"    ... diff truncated after {self._MAX_DIFF_LINES} lines",
                    file=sys.stderr,
                )

    def _get_generated_output(
        self,
        paths: AsicConfigPaths,
        platform: str,
        variant: str,
        platform_config: dict[str, Any],
        output_dir: str,
    ) -> tuple[Optional[GeneratedOutput], Optional[VariantVerification]]:
        display_variant = variant or "<default>"
        try:
            generator = get_generator(platform, variant, platform_config, paths)
        except Exception as error:
            return (
                None,
                VariantVerification(
                    ComparisonRecord(
                        platform, variant, "<not compared>", "GENERATION_ERROR"
                    ),
                    error=f"{platform}/{display_variant}: generation failed: {error}",
                ),
            )
        output_filename = get_output_filename(variant, generator.output_extension)
        generated_path = os.path.join(output_dir, output_filename)
        try:
            with open(generated_path, encoding="utf-8") as f:
                checked_in = f.read()
        except FileNotFoundError:
            if not self._GENERATE_FRESH_OUTPUT:
                return (
                    None,
                    VariantVerification(
                        ComparisonRecord(
                            platform,
                            variant,
                            "<not compared>",
                            "GENERATED_FILE_MISSING",
                        ),
                        error=(
                            f"{platform}/{display_variant}: generated file missing: "
                            f"{generated_path}"
                        ),
                    ),
                )
            checked_in = None

        if not self._GENERATE_FRESH_OUTPUT:
            if checked_in is None:
                raise AssertionError("Missing generated file was not reported")
            return GeneratedOutput(output_filename, checked_in), None

        try:
            generated = generator.generate()
        except Exception as error:
            return (
                None,
                VariantVerification(
                    ComparisonRecord(
                        platform, variant, "<not compared>", "GENERATION_ERROR"
                    ),
                    error=f"{platform}/{display_variant}: generation failed: {error}",
                ),
            )

        stale_generated_file = generated_path if generated != checked_in else None
        return (GeneratedOutput(output_filename, generated, stale_generated_file), None)

    def _verify_variant(
        self,
        paths: AsicConfigPaths,
        platform: str,
        variant: str,
        platform_config: dict[str, Any],
        output_dir: str,
    ) -> VariantVerification:
        display_variant = variant or "<default>"
        generated_output, generation_error = self._get_generated_output(
            paths, platform, variant, platform_config, output_dir
        )
        if generation_error:
            return generation_error
        if generated_output is None:
            raise AssertionError("Generated output is missing without an error")

        generated = generated_output.contents
        output_filename = generated_output.filename
        stale_generated_file = generated_output.stale_generated_file

        materialized_relative_path = self._materialized_reference_path(
            platform, variant, platform_config
        )
        try:
            reference_path, reference = self._read_reference(
                paths, materialized_relative_path, platform, variant, output_filename
            )
        except FileNotFoundError as error:
            return VariantVerification(
                ComparisonRecord(
                    platform, variant, str(error.filename), "REFERENCE_MISSING"
                ),
                stale_generated_file=stale_generated_file,
                error=(
                    f"{platform}/{display_variant}: reference file missing: "
                    f"{error.filename}"
                ),
            )
        except ValueError as error:
            return VariantVerification(
                ComparisonRecord(
                    platform,
                    variant,
                    materialized_relative_path or "<unknown>",
                    "REFERENCE_ERROR",
                ),
                stale_generated_file=stale_generated_file,
                error=f"{platform}/{display_variant}: invalid reference: {error}",
            )

        try:
            result = self._compare(generated, reference)
        except yaml.YAMLError as error:
            return VariantVerification(
                ComparisonRecord(platform, variant, reference_path, "PARSE_ERROR"),
                stale_generated_file=stale_generated_file,
                error=(f"{platform}/{display_variant}: failed to parse YAML: {error}"),
            )

        record = ComparisonRecord(platform, variant, reference_path, result.value)
        mismatch = (
            ContentMismatch(record, generated, reference)
            if result is ComparisonResult.CONTENT_MISMATCH
            else None
        )
        return VariantVerification(record, mismatch, stale_generated_file)

    def _collect_verification_results(
        self, paths: AsicConfigPaths
    ) -> VerificationResults:
        records: list[ComparisonRecord] = []
        mismatches: list[ContentMismatch] = []
        stale_generated_files: list[str] = []
        errors: list[str] = []

        for platform, (platform_config, output_dir) in sorted(
            discover_platforms(paths).items()
        ):
            for variant in platform_config.get("variants", {}):
                verification = self._verify_variant(
                    paths, platform, variant, platform_config, output_dir
                )
                records.append(verification.record)
                if verification.mismatch:
                    mismatches.append(verification.mismatch)
                if verification.stale_generated_file:
                    stale_generated_files.append(verification.stale_generated_file)
                if verification.error:
                    errors.append(verification.error)

        return VerificationResults(records, mismatches, stale_generated_files, errors)

    def _assert_verification_success(self, results: VerificationResults) -> None:
        records = results.records
        mismatches = results.mismatches
        stale_generated_files = results.stale_generated_files
        errors = results.errors

        self._print_summary(records)
        self._print_content_mismatches(mismatches)

        if stale_generated_files:
            print("\nSTALE generated files:", file=sys.stderr)
            for path in stale_generated_files:
                print(f"  - {path}", file=sys.stderr)

        if errors:
            print("\nVerification errors:", file=sys.stderr)
            for error in errors:
                print(f"  - {error}", file=sys.stderr)

        failures = []
        if mismatches:
            mismatch_variants = ", ".join(
                f"{mismatch.record.platform}/{mismatch.record.variant or '<default>'}"
                for mismatch in mismatches
            )
            failures.append(
                f"{len(mismatches)} ASIC config v3 variant(s) differ from their "
                f"internal references: {mismatch_variants}"
            )
        if stale_generated_files:
            failures.append(
                f"{len(stale_generated_files)} checked-in generated file(s) are stale"
            )
        failures.extend(errors)

        if failures:
            self.fail("ASIC config v3 verification failed:\n" + "\n".join(failures))

    def test_generated_files_match_internal_references(self) -> None:
        paths = AsicConfigPaths.from_root(self._FBOSS_ROOT)
        self._assert_verification_success(self._collect_verification_results(paths))

    def test_comparison_result_classification(self) -> None:
        self.assertEqual(
            self._compare("key: value\n", "key: value\n"), ComparisonResult.BYTE_MATCH
        )
        self.assertEqual(
            self._compare("first: 1\nsecond: 2\n", "second: 2\nfirst: 1\n"),
            ComparisonResult.SEMANTIC_MATCH,
        )
        self.assertEqual(
            self._compare("key: first\n", "key: second\n"),
            ComparisonResult.CONTENT_MISMATCH,
        )

    def test_content_mismatch_is_listed_and_fails(self) -> None:
        record = ComparisonRecord(
            "test_platform",
            "test_variant",
            "reference.yml",
            ComparisonResult.CONTENT_MISMATCH.value,
        )
        results = VerificationResults(
            records=[record],
            mismatches=[
                ContentMismatch(record, "key: generated\n", "key: reference\n")
            ],
            stale_generated_files=[],
            errors=[],
        )
        stderr = io.StringIO()

        with (
            patch("sys.stderr", stderr),
            self.assertRaisesRegex(
                AssertionError, "1 ASIC config v3 variant.*test_platform/test_variant"
            ),
        ):
            self._assert_verification_success(results)

        output = stderr.getvalue()
        self.assertIn("CONTENT_MISMATCH platform variants:", output)
        self.assertIn("test_platform/test_variant", output)
        self.assertIn("Reference: reference.yml", output)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--fboss-root",
        type=str,
        required=True,
        help=(
            "Path to the fboss/ source directory itself, for example "
            "/path/to/fbcode/fboss."
        ),
    )
    parser.add_argument(
        "--compare-checked-in",
        action="store_true",
        help=(
            "Compare the checked-in generated files against their references "
            "instead of regenerating them. Does not catch generator regressions."
        ),
    )
    args = parser.parse_args()
    TestValidateAsicConfigV3Contents._FBOSS_ROOT = os.path.abspath(args.fboss_root)
    TestValidateAsicConfigV3Contents._GENERATE_FRESH_OUTPUT = (
        not args.compare_checked_in
    )
    suite = unittest.TestSuite(
        [
            TestValidateAsicConfigV3Contents(
                "test_generated_files_match_internal_references"
            )
        ]
    )
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)


if __name__ == "__main__":
    main()
