#!/usr/bin/env python3
# @noautodeps
# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

import enum
import json
from collections.abc import Collection, Iterable
from dataclasses import dataclass
from typing import TypeVar

__test__ = False


class TestTier(str, enum.Enum):
    UNSPECIFIED = "UNSPECIFIED"
    T0 = "T0"
    T1 = "T1"
    T2 = "T2"


class TestCapability(str, enum.Enum):
    TRANSCEIVER = "TRANSCEIVER"
    EXTERNAL_PHY = "EXTERNAL_PHY"


class TestProfile(str, enum.Enum):
    TRADITIONAL = "TRADITIONAL"
    SCALE_UP = "SCALE_UP"


@dataclass(frozen=True)
class TestMetadataRule:
    test_name_pattern: str
    tier: TestTier
    required_capabilities: tuple[TestCapability, ...] = ()
    profiles: tuple[TestProfile, ...] = ()


class TestMetadataError(ValueError):
    """Raised when a test metadata file cannot be read or parsed."""


_EnumT = TypeVar("_EnumT", bound=enum.Enum)


_CLI_TIERS = {
    "0": TestTier.T0,
    "1": TestTier.T1,
    "2": TestTier.T2,
}


def parse_test_tier(value: str) -> TestTier:
    """Parse the numeric spelling used by ``run_test.py --tier``."""
    try:
        return _CLI_TIERS[value]
    except KeyError as error:
        raise ValueError(f"invalid test tier: {value!r}") from error


def load_test_metadata_rules(file_path: str) -> list[TestMetadataRule]:
    """Load the Configerator-produced test metadata JSON."""
    try:
        with open(file_path, encoding="utf-8") as metadata_file:
            metadata = json.load(metadata_file)
    except (OSError, json.JSONDecodeError) as error:
        raise TestMetadataError(
            f"Unable to load test metadata from {file_path!r}: {error}"
        ) from error

    if not isinstance(metadata, dict):
        raise TestMetadataError("test metadata must be a JSON object")

    raw_rules = metadata.get("rules", [])
    if not isinstance(raw_rules, list):
        raise TestMetadataError("field 'rules' must be a list")

    rules = []
    for index, raw_rule in enumerate(raw_rules):
        if not isinstance(raw_rule, dict):
            raise TestMetadataError(f"rule {index} must be a JSON object")

        pattern = raw_rule.get("testNamePattern")
        if not isinstance(pattern, str) or not pattern:
            raise TestMetadataError(
                f"rule {index} field 'testNamePattern' must be a non-empty string"
            )

        rules.append(
            TestMetadataRule(
                test_name_pattern=pattern,
                tier=_parse_enum(TestTier, raw_rule.get("tier"), index, "tier"),
                required_capabilities=_parse_enum_list(
                    TestCapability,
                    raw_rule.get("requiredCapabilities", []),
                    index,
                    "requiredCapabilities",
                ),
                profiles=_parse_enum_list(
                    TestProfile,
                    raw_rule.get("profiles", []),
                    index,
                    "profiles",
                ),
            )
        )
    return rules


def _parse_enum(
    enum_type: type[_EnumT], value: object, rule_index: int, field_name: str
) -> _EnumT:
    if not isinstance(value, str):
        raise TestMetadataError(
            f"rule {rule_index} field {field_name!r} must be a string"
        )
    try:
        return enum_type(value)
    except ValueError as error:
        raise TestMetadataError(
            f"rule {rule_index} field {field_name!r} has unknown value {value!r}"
        ) from error


def _parse_enum_list(
    enum_type: type[_EnumT], value: object, rule_index: int, field_name: str
) -> tuple[_EnumT, ...]:
    if not isinstance(value, list):
        raise TestMetadataError(
            f"rule {rule_index} field {field_name!r} must be a list"
        )
    return tuple(_parse_enum(enum_type, item, rule_index, field_name) for item in value)


def _profile_matches(
    rule_profiles: Collection[TestProfile], requested_profile: TestProfile | None
) -> bool:
    if requested_profile is not None:
        return requested_profile in rule_profiles

    # Preserve the existing filter-file behavior: without --profile, select
    # untagged rules and rules tagged for the traditional profile.
    return not rule_profiles or TestProfile.TRADITIONAL in rule_profiles


def select_test_patterns_for_tier(
    rules: Iterable[TestMetadataRule],
    tier: TestTier,
    *,
    available_capabilities: Collection[TestCapability] = (),
    profile: TestProfile | None = None,
) -> list[str]:
    """Return GTest filter patterns from the applicable rules for ``tier``.

    A rule is applicable when its tier and profile match and all its required
    capabilities are available. A pattern may appear in multiple tiers and is
    therefore selectable in each of those tier runs. Rule order is preserved
    exactly. The caller passes these patterns to GTest, which remains
    authoritative for pattern expansion.
    """
    return [
        rule.test_name_pattern
        for rule in rules
        if (
            rule.tier is tier
            and set(rule.required_capabilities).issubset(available_capabilities)
            and _profile_matches(rule.profiles, profile)
        )
    ]
