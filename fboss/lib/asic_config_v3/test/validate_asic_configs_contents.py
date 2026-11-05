#!/usr/bin/env python3
"""Validate ASIC config v3 contents and compare available internal references.

Usage (internal, from fbcode/):
    buck2 run //fboss/lib/asic_config_v3/test:validate_asic_configs_contents

Usage (OSS, from the root of the fboss repository):
    ./fboss/lib/asic_config_v3/validate-contents-helper.sh

Both print a per-variant comparison table and a summary counting BYTE_MATCH,
SEMANTIC_MATCH, GENERATED_ONLY and CONTENT_MISMATCH variants. GENERATED_ONLY
means no legacy or materialized reference exists, so validation is limited to
ensuring that freshly generated output matches the checked-in v3 artifact.
Note that a bare ``python3 -m ...`` invocation does not work: the generators
import thrift-python modules that only exist once buck or cmake has run
codegen.
"""

import argparse
import difflib
import io
import json
import os
import sys
import unittest
from enum import Enum
from typing import Any, cast, NamedTuple, Optional
from unittest.mock import patch

import yaml
from fboss.lib.asic_config_v3.base_generator import (
    GeneratedVariant,
    resolve_variant_config,
)
from fboss.lib.asic_config_v3.gen import (
    generate_asic_configs,
    GeneratedAsicConfigs,
    get_generator,
    get_output_filename,
    get_output_files,
)
from fboss.lib.asic_config_v3.generators.broadcom_xgs_generator import (
    BroadcomXgsGenerator,
)
from fboss.lib.asic_config_v3.paths import AsicConfigPaths, discover_platforms
from yaml.nodes import MappingNode, ScalarNode, SequenceNode


class ComparisonResult(Enum):
    BYTE_MATCH = "BYTE_MATCH"
    SEMANTIC_MATCH = "SEMANTIC_MATCH"
    GENERATED_ONLY = "GENERATED_ONLY"
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
    stale_generated_files: tuple[str, ...] = ()
    error: Optional[str] = None


class GeneratedOutput(NamedTuple):
    files: dict[str, str]
    stale_generated_files: tuple[str, ...] = ()


class VerificationResults(NamedTuple):
    records: list[ComparisonRecord]
    mismatches: list[ContentMismatch]
    stale_generated_files: list[str]
    errors: list[str]


class TestValidateAsicConfigV3Contents(unittest.TestCase):
    """Validate generated v3 output and any available reference contents."""

    _FBOSS_ROOT = "fboss"
    _GENERATE_FRESH_OUTPUT = True
    _SYNCED_ASIC_CONFIG_DIR = os.path.join(
        "lib", "asic_config_v2", "synced_asic_configs"
    )
    # Some variants have no standalone synced ASIC config; their source of
    # truth is the YAML embedded in a materialized agent config. Keyed by the
    # variant's effective ``config_gen_type`` so that any platform declaring
    # one of these gen types is picked up without editing this table.
    _MATERIALIZED_REFERENCE_DIRS = {
        "HW_TEST": os.path.join("oss", "hw_test_configs"),
    }
    _MATERIALIZED_REFERENCE_SUFFIX = ".agent.materialized_JSON"
    _MATERIALIZED_YAML_PATH = (
        "platform",
        "chip",
        "asicConfig",
        "common",
        "yamlConfig",
    )
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
        return os.path.join(
            reference_dir, f"{platform}{self._MATERIALIZED_REFERENCE_SUFFIX}"
        )

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
                (
                    self._canonicalize_yaml_node(key),
                    self._canonicalize_yaml_node(value),
                )
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
        print(
            "  ".join("-" * width for width in widths),
            file=sys.stderr,
        )
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
        generated_variant: GeneratedVariant | None = None,
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

        checked_in = self._read_checked_in_outputs(
            output_dir, variant, generator.output_extension
        )
        if not self._GENERATE_FRESH_OUTPUT:
            if not checked_in:
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
                            f"{platform}/{display_variant}: no generated files found "
                            f"in {output_dir}"
                        ),
                    ),
                )
            return GeneratedOutput(checked_in), None

        try:
            generated = get_output_files(
                variant,
                generator.output_extension,
                (
                    generated_variant
                    if generated_variant is not None
                    else generator.generate()
                ),
            )
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

        stale_generated_files = tuple(
            os.path.join(output_dir, filename)
            for filename in sorted(generated.keys() | checked_in.keys())
            if generated.get(filename) != checked_in.get(filename)
        )
        return (
            GeneratedOutput(generated, stale_generated_files),
            None,
        )

    def _read_checked_in_outputs(
        self, output_dir: str, variant: str, extension: str
    ) -> dict[str, str]:
        """Read every valid artifact shape belonging to one variant.

        Including both the common and indexed naming schemes lets validation
        report obsolete files when a variant changes between identical and
        distinct NPU mappings.
        """
        if not os.path.isdir(output_dir):
            return {}

        common_filename = get_output_filename(variant, extension)
        section_prefix = f"{variant}_"
        outputs = {}
        for filename in sorted(os.listdir(output_dir)):
            if filename == common_filename:
                pass
            elif filename == f"{variant}_common{extension}":
                pass
            elif filename.startswith(section_prefix) and filename.endswith(extension):
                index = filename[len(section_prefix) : -len(extension)]
                if not index.isdigit():
                    continue
            else:
                continue

            with open(os.path.join(output_dir, filename), encoding="utf-8") as f:
                outputs[filename] = f.read()
        return outputs

    def _verify_variant(
        self,
        paths: AsicConfigPaths,
        platform: str,
        variant: str,
        platform_config: dict[str, Any],
        output_dir: str,
        generated_variant: GeneratedVariant | None = None,
    ) -> VariantVerification:
        display_variant = variant or "<default>"
        generated_output, generation_error = self._get_generated_output(
            paths,
            platform,
            variant,
            platform_config,
            output_dir,
            generated_variant,
        )
        if generation_error:
            return generation_error
        if generated_output is None:
            raise AssertionError("Generated output is missing without an error")

        generated_files = generated_output.files
        stale_generated_files = generated_output.stale_generated_files

        common_filename = get_output_filename(
            variant,
            os.path.splitext(next(iter(generated_files)))[1],
        )
        if set(generated_files) != {common_filename}:
            return VariantVerification(
                ComparisonRecord(
                    platform,
                    variant,
                    "<per-NPU generated outputs>",
                    ComparisonResult.GENERATED_ONLY.value,
                ),
                stale_generated_files=stale_generated_files,
            )

        output_filename = common_filename
        generated = generated_files[output_filename]

        materialized_relative_path = self._materialized_reference_path(
            platform, variant, platform_config
        )
        try:
            reference_path, reference = self._read_reference(
                paths,
                materialized_relative_path,
                platform,
                variant,
                output_filename,
            )
        except FileNotFoundError as error:
            missing_reference = str(error.filename)
            if os.path.isabs(missing_reference):
                missing_reference = os.path.relpath(missing_reference, paths.fboss_root)
            return VariantVerification(
                ComparisonRecord(
                    platform,
                    variant,
                    missing_reference,
                    ComparisonResult.GENERATED_ONLY.value,
                ),
                stale_generated_files=stale_generated_files,
            )
        except ValueError as error:
            return VariantVerification(
                ComparisonRecord(
                    platform,
                    variant,
                    materialized_relative_path or "<unknown>",
                    "REFERENCE_ERROR",
                ),
                stale_generated_files=stale_generated_files,
                error=f"{platform}/{display_variant}: invalid reference: {error}",
            )

        try:
            result = self._compare(generated, reference)
        except yaml.YAMLError as error:
            return VariantVerification(
                ComparisonRecord(platform, variant, reference_path, "PARSE_ERROR"),
                stale_generated_files=stale_generated_files,
                error=(f"{platform}/{display_variant}: failed to parse YAML: {error}"),
            )

        record = ComparisonRecord(platform, variant, reference_path, result.value)
        mismatch = (
            ContentMismatch(record, generated, reference)
            if result is ComparisonResult.CONTENT_MISMATCH
            else None
        )
        return VariantVerification(record, mismatch, stale_generated_files)

    def _collect_verification_results(
        self,
        paths: AsicConfigPaths,
        generated_configs: GeneratedAsicConfigs | None = None,
    ) -> VerificationResults:
        records: list[ComparisonRecord] = []
        mismatches: list[ContentMismatch] = []
        stale_generated_files: list[str] = []
        errors: list[str] = []

        for platform, (platform_config, output_dir) in sorted(
            discover_platforms(paths).items()
        ):
            for variant in platform_config.get("variants", {}):
                generated_variant = (
                    generated_configs[platform][variant]
                    if generated_configs is not None
                    else None
                )
                verification = self._verify_variant(
                    paths,
                    platform,
                    variant,
                    platform_config,
                    output_dir,
                    generated_variant,
                )
                records.append(verification.record)
                if verification.mismatch:
                    mismatches.append(verification.mismatch)
                stale_generated_files.extend(verification.stale_generated_files)
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

    def test_generated_files_match_available_references(self) -> None:
        paths = AsicConfigPaths.from_root(self._FBOSS_ROOT)
        generated_configs = (
            generate_asic_configs(paths) if self._GENERATE_FRESH_OUTPUT else None
        )
        if generated_configs is not None:
            self.assertTrue(generated_configs, "No registered ASIC configs generated")
        self._assert_verification_success(
            self._collect_verification_results(paths, generated_configs)
        )

    def test_comparison_result_classification(self) -> None:
        self.assertEqual(
            self._compare("key: value\n", "key: value\n"),
            ComparisonResult.BYTE_MATCH,
        )
        self.assertEqual(
            self._compare("first: 1\nsecond: 2\n", "second: 2\nfirst: 1\n"),
            ComparisonResult.SEMANTIC_MATCH,
        )
        self.assertEqual(
            self._compare("key: first\n", "key: second\n"),
            ComparisonResult.CONTENT_MISMATCH,
        )

    def test_output_file_contract(self) -> None:
        self.assertEqual(
            get_output_files("rack", ".yml", GeneratedVariant(common="shared")),
            {"rack.yml": "shared"},
        )
        self.assertEqual(
            get_output_files(
                "rack",
                ".yml",
                GeneratedVariant(common=None, npu_entries={0: "npu0", 1: "npu1"}),
            ),
            {"rack_0.yml": "npu0", "rack_1.yml": "npu1"},
        )
        self.assertEqual(
            get_output_files(
                "default",
                ".json",
                GeneratedVariant(common="common", npu_entries={0: "npu0", 1: "npu1"}),
            ),
            {
                "default_common.json": "common",
                "default_0.json": "npu0",
                "default_1.json": "npu1",
            },
        )

    def test_output_file_contract_rejects_missing_and_gapped_sections(self) -> None:
        with self.assertRaisesRegex(ValueError, "no common or NPU payloads"):
            get_output_files("rack", ".yml", GeneratedVariant(common=None))
        with self.assertRaisesRegex(ValueError, "non-contiguous NPU indices"):
            get_output_files(
                "rack",
                ".yml",
                GeneratedVariant(common=None, npu_entries={0: "npu0", 2: "npu2"}),
            )

    def test_leh800bcls_rack_npu_pm_core_differences(self) -> None:
        paths = AsicConfigPaths.from_root(self._FBOSS_ROOT)
        platform_config, _ = discover_platforms(paths)["leh800bcls"]
        generator = cast(
            BroadcomXgsGenerator,
            get_generator("leh800bcls", "rack", platform_config, paths),
        )

        generator.generate()
        self.assertEqual(set(generator.npu_pm_core), {0, 1})
        npu0 = generator.npu_pm_core[0]
        npu1 = generator.npu_pm_core[1]
        differing_keys = {key for key in npu0 if npu0[key] != npu1[key]}
        differing_pm_ids = {
            int(key[0].removeprefix("PC_PM_ID: ")) for key in differing_keys
        }
        self.assertEqual(differing_pm_ids, {29, 31, 32, 33, 34, 35, 36})

        for key in differing_keys:
            self.assertEqual(npu0[key][:4], npu1[key][:4])
            self.assertNotEqual(npu0[key][4:], npu1[key][4:])

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

        with patch("sys.stderr", stderr):
            with self.assertRaisesRegex(
                AssertionError,
                "1 ASIC config v3 variant.*test_platform/test_variant",
            ):
                self._assert_verification_success(results)

        output = stderr.getvalue()
        self.assertIn("CONTENT_MISMATCH platform variants:", output)
        self.assertIn("test_platform/test_variant", output)
        self.assertIn("Reference: reference.yml", output)

    def test_missing_reference_uses_generated_output(self) -> None:
        missing_reference = "synced_asic_configs/new_platform.yml"
        generated_output = GeneratedOutput({"default.yml": "key: generated\n"})

        with (
            patch.object(
                self,
                "_get_generated_output",
                return_value=(generated_output, None),
            ),
            patch.object(
                self,
                "_read_reference",
                side_effect=FileNotFoundError(
                    2, "No such file or directory", missing_reference
                ),
            ),
        ):
            verification = self._verify_variant(
                AsicConfigPaths.from_root(self._FBOSS_ROOT),
                "new_platform",
                "default",
                {"variants": {"default": {}}},
                "generated",
            )

        self.assertEqual(
            verification.record,
            ComparisonRecord(
                "new_platform",
                "default",
                missing_reference,
                ComparisonResult.GENERATED_ONLY.value,
            ),
        )
        self.assertIsNone(verification.error)
        self.assertFalse(verification.stale_generated_files)

    def test_generated_only_is_reported_and_succeeds(self) -> None:
        record = ComparisonRecord(
            "new_platform",
            "default",
            "synced_asic_configs/new_platform.yml",
            ComparisonResult.GENERATED_ONLY.value,
        )
        stderr = io.StringIO()

        with patch("sys.stderr", stderr):
            self._assert_verification_success(VerificationResults([record], [], [], []))

        output = stderr.getvalue()
        self.assertIn("new_platform", output)
        self.assertIn("GENERATED_ONLY", output)
        self.assertIn("GENERATED_ONLY: 1", output)

    def test_generated_only_still_fails_for_stale_generated_output(self) -> None:
        stale_path = "generated/default.yml"
        record = ComparisonRecord(
            "new_platform",
            "default",
            "synced_asic_configs/new_platform.yml",
            ComparisonResult.GENERATED_ONLY.value,
        )

        with patch("sys.stderr", io.StringIO()):
            with self.assertRaisesRegex(
                AssertionError,
                "1 checked-in generated file.*stale",
            ):
                self._assert_verification_success(
                    VerificationResults([record], [], [stale_path], [])
                )


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
                "test_generated_files_match_available_references"
            )
        ]
    )
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)


if __name__ == "__main__":
    main()
