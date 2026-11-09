#!/usr/bin/env python3
# pyre-strict

"""
Validate ASIC config JSON files against their JSON schemas.

Usage:
    python3 -m fboss.lib.asic_config_v3.test.validate_asic_configs_schemas \
        --fboss-root /path/to/fboss
"""

import argparse
import glob
import json
import os
import sys
import unittest
from typing import Any, NamedTuple

import jsonschema
from fboss.lib.asic_config_v3.paths import AsicConfigPaths

try:
    from referencing import Registry, Resource
except ImportError:  # jsonschema releases before the referencing package
    Registry = Resource = None


class ValidationTarget(NamedTuple):
    description: str
    schema: str
    files_glob: str


class ValidationResults(NamedTuple):
    passed: int
    failures: list[str]


def get_validation_targets(paths: AsicConfigPaths) -> list[ValidationTarget]:
    return [
        ValidationTarget(
            "Vendor common configs",
            "vendor_common.schema.json",
            os.path.join(paths.asic_vendors_dir, "*", "*", "*_common.json"),
        ),
        ValidationTarget(
            "OCP SAI common config",
            "vendor_common.schema.json",
            os.path.join(paths.asic_vendors_dir, "common", "ocp_sai_common.json"),
        ),
        ValidationTarget(
            "Broadcom XGS ASIC configs",
            "broadcom_xgs_asic_config.schema.json",
            os.path.join(
                paths.asic_vendors_dir, "broadcom", "xgs", "asics", "tomahawk*.json"
            ),
        ),
        ValidationTarget(
            "Broadcom DNX ASIC configs",
            "broadcom_dnx_asic_config.schema.json",
            os.path.join(paths.asic_vendors_dir, "broadcom", "dnx", "asics", "*.json"),
        ),
        ValidationTarget(
            "Platform configs",
            "platform_config.schema.json",
            os.path.join(
                paths.platforms_dir, "*", "*", "asic_config", "asic_config.json"
            ),
        ),
    ]


def load_schemas(paths: AsicConfigPaths) -> dict[str, dict[str, Any]]:
    """Load every schema in the schemas directory, keyed by file name."""
    schemas: dict[str, dict[str, Any]] = {}
    for path in sorted(glob.glob(os.path.join(paths.schemas_dir, "*.schema.json"))):
        with open(path) as f:
            schemas[os.path.basename(path)] = json.load(f)
    return schemas


def make_validator(schema: dict[str, Any], schemas: dict[str, dict[str, Any]]) -> Any:
    """Build a validator that resolves references between the schema files."""
    validator_cls = jsonschema.validators.validator_for(schema)
    validator_cls.check_schema(schema)
    if Registry is None or Resource is None:
        resolver = jsonschema.RefResolver.from_schema(
            schema, store={s["$id"]: s for s in schemas.values() if "$id" in s}
        )
        return validator_cls(schema, resolver=resolver)
    registry = Registry().with_resources(
        (s["$id"], Resource.from_contents(s)) for s in schemas.values() if "$id" in s
    )
    return validator_cls(schema, registry=registry)


def validate(validator: Any, config_path: str) -> None:
    with open(config_path) as f:
        config: dict[str, Any] = json.load(f)
    validator.validate(config)


def validate_asic_config_schemas(paths: AsicConfigPaths) -> ValidationResults:
    passed = 0
    failures = []
    schemas = load_schemas(paths)

    for target in get_validation_targets(paths):
        validator = make_validator(schemas[target.schema], schemas)

        files = sorted(glob.glob(target.files_glob, recursive=True))
        print(f"{target.description} ({target.schema}):")

        if not files:
            print("  No files found.")
            continue

        for path in files:
            name = os.path.relpath(path, paths.configs_dir)
            try:
                validate(validator, path)
                print(f"  {name}: PASSED")
                passed += 1
            except jsonschema.ValidationError as error:
                print(f"  {name}: FAILED - {error.message}")
                failures.append(f"{name}: {error.message}")

        print()

    print(f"{passed} passed, {len(failures)} failed")
    return ValidationResults(passed, failures)


class TestValidateAsicConfigV3Schemas(unittest.TestCase):
    _FBOSS_ROOT = "fboss"

    def test_asic_config_schemas(self) -> None:
        results = validate_asic_config_schemas(
            AsicConfigPaths.from_root(self._FBOSS_ROOT)
        )
        if results.failures:
            self.fail(
                "ASIC config v3 schema validation failed:\n"
                + "\n".join(results.failures)
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
    args = parser.parse_args()
    TestValidateAsicConfigV3Schemas._FBOSS_ROOT = os.path.abspath(args.fboss_root)
    suite = unittest.TestSuite(
        [TestValidateAsicConfigV3Schemas("test_asic_config_schemas")]
    )
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)


if __name__ == "__main__":
    main()
