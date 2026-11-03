#!/usr/bin/env python3

# pyre-strict

import unittest
from typing import Any

from fboss.lib.asic_config_v3.base_generator import BaseAsicConfigGenerator
from fboss.lib.asic_config_v3.paths import AsicConfigPaths


class RecordingGenerator(BaseAsicConfigGenerator):
    """Minimal generator that records applied settings per target."""

    SUPPORTED_EFFECTS: frozenset[str] = frozenset(
        {"apply", "apply_from", "skip_from_sai_common"}
    )
    TARGETS: frozenset[str] = frozenset({"global", "common"})

    def __init__(
        self,
        asic_config: dict[str, Any],
        variant: dict[str, Any],
        strings_only: bool = False,
    ) -> None:
        platform_config = {
            "platform_name": "testboard",
            "vendor": "vendor",
            "asic": "asic",
            "variants": {"default": variant},
        }
        super().__init__(
            "testboard", "default", platform_config, AsicConfigPaths.from_root("/")
        )
        self.asic_config = asic_config
        self.strings_only = strings_only
        self.applied: list[tuple[str, dict[str, Any]]] = []
        self._validate_conditional_settings()

    @property
    def output_extension(self) -> str:
        return ".txt"

    def generate(self) -> str:
        self._execute_apply_effects()
        return ""

    def _apply_settings(self, target: str, settings: dict[str, Any]) -> None:
        self.applied.append((target, dict(settings)))

    def _validate_apply_target(self, name: str, target: str) -> None:
        if target not in self.TARGETS:
            raise ValueError(f"{name}: unknown target {target}")

    def _validate_apply_value(
        self, name: str, target: str, key: str, value: Any
    ) -> None:
        if self.strings_only and not isinstance(value, str):
            raise ValueError(f"{name}: {key} is not a string")


# A condition that holds for every test variant: the parameter is never
# declared, so it evaluates as None and satisfies the negative operator.
# Passed explicitly by tests whose subject is effects or ordering rather
# than condition evaluation.
_HOLDS: dict[str, Any] = {"param": "_test_param_not_set", "not_equals": "_sentinel"}


def entry(name: str, condition: dict[str, Any], **effects: Any) -> dict[str, Any]:
    """Build an entry; the condition is required, mirroring the schema."""
    return {"name": name, "condition": condition, **effects}


def matches(condition: dict[str, Any], params: dict[str, Any]) -> bool:
    generator = RecordingGenerator(
        {
            "conditional_settings": [
                entry("probe", condition, apply={"common": {"k": "v"}})
            ]
        },
        {"asic_config_params": params},
    )
    return bool(generator._matching_conditional_settings())


class ConditionEvaluationTest(unittest.TestCase):
    def test_equals_and_not_equals_are_complements(self) -> None:
        for params in ({"p": "a"}, {"p": "b"}, {"p": 1}, {}):
            positive = matches({"param": "p", "equals": "a"}, params)
            negative = matches({"param": "p", "not_equals": "a"}, params)
            self.assertNotEqual(positive, negative, params)
        self.assertTrue(matches({"param": "p", "equals": "a"}, {"p": "a"}))
        self.assertFalse(matches({"param": "p", "equals": "a"}, {}))
        self.assertTrue(matches({"param": "p", "not_equals": "a"}, {}))

    def test_in_and_not_in_are_complements(self) -> None:
        for params in ({"p": "a"}, {"p": "c"}, {"p": 2}, {}):
            positive = matches({"param": "p", "in": ["a", "b"]}, params)
            negative = matches({"param": "p", "not_in": ["a", "b"]}, params)
            self.assertNotEqual(positive, negative, params)
        self.assertTrue(matches({"param": "p", "in": ["a", "b"]}, {"p": "b"}))
        self.assertFalse(matches({"param": "p", "in": ["a", "b"]}, {}))

    def test_starts_with_and_not_starts_with_are_complements(self) -> None:
        for params in ({"p": "dual_stage_x"}, {"p": "single"}, {"p": 3}, {}):
            positive = matches({"param": "p", "starts_with": "dual"}, params)
            negative = matches({"param": "p", "not_starts_with": "dual"}, params)
            self.assertNotEqual(positive, negative, params)
        self.assertTrue(
            matches({"param": "p", "starts_with": "dual"}, {"p": "dual_stage"})
        )
        self.assertFalse(matches({"param": "p", "starts_with": "dual"}, {"p": 3}))
        self.assertTrue(matches({"param": "p", "not_starts_with": "dual"}, {}))

    def test_features_source(self) -> None:
        generator = RecordingGenerator(
            {
                "conditional_settings": [
                    entry(
                        "f",
                        {"source": "features", "param": "flag", "equals": True},
                        apply={"common": {"k": "v"}},
                    )
                ]
            },
            {"features": {"flag": True}},
        )
        self.assertEqual(len(generator._matching_conditional_settings()), 1)

    def test_entry_without_condition_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            RecordingGenerator(
                {
                    "conditional_settings": [
                        {"name": "bare", "apply": {"common": {"k": "v"}}}
                    ]
                },
                {},
            )

    def test_entry_with_empty_condition_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            matches({}, {})

    def test_unknown_source_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            matches({"source": "bogus", "param": "p", "equals": 1}, {})

    def test_operator_count_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            matches({"param": "p"}, {})
        with self.assertRaises(ValueError):
            matches({"param": "p", "equals": 1, "starts_with": "x"}, {})


class EffectTest(unittest.TestCase):
    def test_ordering_asic_then_platform_then_array_order(self) -> None:
        generator = RecordingGenerator(
            {
                "block": {"from_block": "1"},
                "conditional_settings": [
                    entry("a1", _HOLDS, apply={"common": {"a1": "1"}}),
                    entry(
                        "a2",
                        _HOLDS,
                        apply={"common": {"a2": "1"}},
                        apply_from={"source": "block", "target_table": "common"},
                    ),
                ],
            },
            {
                "conditional_settings": [
                    entry("p1", _HOLDS, apply={"common": {"p1": "1"}})
                ]
            },
        )
        generator.generate()
        self.assertEqual(
            generator.applied,
            [
                ("common", {"a1": "1"}),
                ("common", {"from_block": "1"}),
                ("common", {"a2": "1"}),
                ("common", {"p1": "1"}),
            ],
        )

    def test_collect_effect_values_spans_both_scopes(self) -> None:
        generator = RecordingGenerator(
            {"conditional_settings": [entry("a", _HOLDS, skip_from_sai_common=["x"])]},
            {
                "conditional_settings": [
                    entry("p", _HOLDS, skip_from_sai_common=["y", "z"])
                ]
            },
        )
        self.assertEqual(
            generator._collect_effect_values("skip_from_sai_common"), ["x", "y", "z"]
        )

    def test_non_matching_entries_are_skipped(self) -> None:
        generator = RecordingGenerator(
            {
                "conditional_settings": [
                    entry(
                        "off",
                        {"param": "p", "equals": "no"},
                        apply={"common": {"k": "v"}},
                    )
                ]
            },
            {"asic_config_params": {"p": "yes"}},
        )
        generator.generate()
        self.assertEqual(generator.applied, [])


class EagerValidationTest(unittest.TestCase):
    def test_unsupported_effect_rejected_even_when_condition_is_false(self) -> None:
        with self.assertRaises(ValueError):
            RecordingGenerator(
                {
                    "conditional_settings": [
                        entry(
                            "x",
                            {"param": "p", "equals": "no"},
                            apply={"common": {"k": "v"}},
                            skip_generated={"generator": "polarity_map"},
                        )
                    ]
                },
                {"asic_config_params": {"p": "yes"}},
            )

    def test_unknown_target_rejected(self) -> None:
        with self.assertRaises(ValueError):
            RecordingGenerator(
                {
                    "conditional_settings": [
                        entry("x", _HOLDS, apply={"bogus": {"k": "v"}})
                    ]
                },
                {},
            )

    def test_missing_apply_from_source_rejected(self) -> None:
        with self.assertRaises(ValueError):
            RecordingGenerator(
                {
                    "conditional_settings": [
                        entry(
                            "x",
                            _HOLDS,
                            apply_from={"source": "missing", "target_table": "common"},
                        )
                    ]
                },
                {},
            )

    def test_empty_settings_rejected(self) -> None:
        with self.assertRaises(ValueError):
            RecordingGenerator(
                {"conditional_settings": [entry("x", _HOLDS, apply={"common": {}})]}, {}
            )

    def test_entry_without_effect_rejected(self) -> None:
        with self.assertRaises(ValueError):
            RecordingGenerator(
                {"conditional_settings": [entry("x", {"param": "p", "equals": 1})]}, {}
            )

    def test_malformed_effect_shapes_rejected(self) -> None:
        for effects in (
            {"skip_from_sai_common": "foo"},
            {"skip_from_sai_common": []},
            {"skip_from_sai_common": [1]},
            {"apply_from": {"source": "block"}},
            {"apply_from": "block"},
            {"apply": "common"},
        ):
            with self.assertRaises(ValueError, msg=str(effects)):
                RecordingGenerator(
                    {
                        "block": {"k": "v"},
                        "conditional_settings": [entry("x", _HOLDS, **effects)],
                    },
                    {},
                )

    def test_value_validation_hook(self) -> None:
        with self.assertRaises(ValueError):
            RecordingGenerator(
                {
                    "conditional_settings": [
                        entry("x", _HOLDS, apply={"common": {"k": 1}})
                    ]
                },
                {},
                strings_only=True,
            )
        RecordingGenerator(
            {"conditional_settings": [entry("x", _HOLDS, apply={"common": {"k": 1}})]},
            {},
        )


if __name__ == "__main__":
    unittest.main()
