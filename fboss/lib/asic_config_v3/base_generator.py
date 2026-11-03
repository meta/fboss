# pyre-strict

import copy
from abc import ABC, abstractmethod
from typing import Any, Callable

from fboss.lib.asic_config_v3.paths import AsicConfigPaths


def _deep_merge(base: dict[str, Any], override: dict[str, Any]) -> dict[str, Any]:
    """Recursively merge ``override`` on top of ``base``.

    Dict values present in both are merged recursively. For any other value
    type (scalars, lists, or a type mismatch between the two sides) the
    override replaces the base entry outright. Returns a new dict; the inputs
    are not mutated.
    """
    result = copy.deepcopy(base)
    for key, ov_value in override.items():
        if (
            key in result
            and isinstance(result[key], dict)
            and isinstance(ov_value, dict)
        ):
            result[key] = _deep_merge(result[key], ov_value)
        else:
            result[key] = copy.deepcopy(ov_value)
    return result


def resolve_variant_config(
    platform_config: dict[str, Any], variant: str
) -> dict[str, Any]:
    """Return the effective config for ``variant``.

    The platform JSON may declare a top-level ``defaults`` block inherited by
    every variant. The effective variant config is produced by deep-merging the
    variant-specific entries on top of ``defaults``. Dict values are merged
    recursively; scalars and lists are replaced.
    """
    return _deep_merge(
        platform_config.get("defaults", {}),
        platform_config.get("variants", {}).get(variant, {}),
    )


# Sections of a variant config from which a condition may read its parameter.
_CONDITION_SOURCES: tuple[str, ...] = ("asic_config_params", "features")

# Condition operators, each mapped to its test of (parameter value, operand).
# Each negative operator is the strict complement of its positive counterpart.
_CONDITION_OPERATORS: dict[str, Callable[[Any, Any], bool]] = {
    "equals": lambda value, operand: value == operand,
    "not_equals": lambda value, operand: value != operand,
    "in": lambda value, operand: value in operand,
    "not_in": lambda value, operand: value not in operand,
    "starts_with": lambda value, operand: isinstance(value, str)
    and value.startswith(operand),
    "not_starts_with": lambda value, operand: not (
        isinstance(value, str) and value.startswith(operand)
    ),
}

# Keys of a conditional-setting entry that do not name an effect.
_ENTRY_KEYS: frozenset[str] = frozenset({"name", "description", "condition"})


class BaseAsicConfigGenerator(ABC):
    """Abstract base class for ASIC config generators.

    Subclasses implement ``generate()`` to produce vendor-specific output
    such as YAML or JSON.
    """

    # Effects this generator can execute. An entry declaring any other effect
    # is rejected when the generator is constructed.
    SUPPORTED_EFFECTS: frozenset[str] = frozenset()

    def __init__(
        self,
        platform_name: str,
        variant: str,
        platform_config: dict[str, Any],
        paths: AsicConfigPaths,
    ) -> None:
        self.platform_name = platform_name
        self.variant = variant
        self.platform_config = platform_config
        self.paths = paths

        vendor = platform_config.get("vendor")
        asic = platform_config.get("asic")
        if not vendor:
            raise ValueError("platform asic_config.json must define 'vendor'")
        if not asic:
            raise ValueError("platform asic_config.json must define 'asic'")
        self.asic_vendor: str = vendor
        self.asic_name: str = asic

        self.variant_config: dict[str, Any] = resolve_variant_config(
            platform_config, variant
        )
        self.asic_config_params: dict[str, Any] = self.variant_config.get(
            "asic_config_params", {}
        )

        # Populated by the subclass from the per-ASIC config file.
        self.asic_config: dict[str, Any] = {}
        self._active_conditional_settings: list[dict[str, Any]] | None = None

    def _conditional_setting_entries(self) -> list[dict[str, Any]]:
        """Return all conditional-setting entries, ASIC-level before platform-level."""
        return list(self.asic_config.get("conditional_settings", [])) + list(
            self.variant_config.get("conditional_settings", [])
        )

    def _validate_conditional_settings(self) -> None:
        """Reject conditional settings this generator cannot execute.

        Every entry is checked regardless of whether its condition currently
        holds, so an invalid entry cannot remain latent until another variant
        activates it.
        """
        for entry in self._conditional_setting_entries():
            name = entry.get("name", "<unnamed>")
            for key in entry:
                if key not in _ENTRY_KEYS and key not in self.SUPPORTED_EFFECTS:
                    raise ValueError(
                        f"Conditional setting '{name}' declares '{key}', which "
                        f"{type(self).__name__} does not support"
                    )
            if not any(effect in entry for effect in self.SUPPORTED_EFFECTS):
                raise ValueError(f"Conditional setting '{name}' declares no effect")
            self._validate_condition(name, entry.get("condition"))
            if "apply" in entry:
                apply = entry["apply"]
                if not isinstance(apply, dict) or not apply:
                    raise ValueError(
                        f"Conditional setting '{name}' has an empty or malformed apply"
                    )
                for target, settings in apply.items():
                    self._validate_apply_target(name, target)
                    self._validate_settings(name, target, settings)
            if "apply_from" in entry:
                apply_from = entry["apply_from"]
                if (
                    not isinstance(apply_from, dict)
                    or not {"source", "target_table"} <= apply_from.keys()
                ):
                    raise ValueError(
                        f"Conditional setting '{name}' has a malformed apply_from; "
                        "expected source and target_table"
                    )
                source = apply_from["source"]
                target = apply_from["target_table"]
                settings = self.asic_config.get(source)
                if not isinstance(settings, dict):
                    raise ValueError(
                        f"Conditional setting '{name}' copies from '{source}', "
                        "which is not a settings block in the ASIC config"
                    )
                self._validate_apply_target(name, target)
                self._validate_settings(name, target, settings)
            if "skip_from_sai_common" in entry:
                keys = entry["skip_from_sai_common"]
                if (
                    not isinstance(keys, list)
                    or not keys
                    or not all(isinstance(key, str) for key in keys)
                ):
                    raise ValueError(
                        f"Conditional setting '{name}' must list the "
                        "skip_from_sai_common keys as a non-empty array of strings"
                    )

    def _validate_condition(self, name: str, condition: dict[str, Any] | None) -> None:
        """Raise ValueError unless the condition names a parameter and one operator."""
        if not condition:
            raise ValueError(
                f"Conditional setting '{name}' has no condition; unconditional "
                "settings belong in the unconditional layers"
            )
        source = condition.get("source", "asic_config_params")
        if source not in _CONDITION_SOURCES:
            raise ValueError(
                f"Conditional setting '{name}' has unknown condition source '{source}'"
            )
        if "param" not in condition:
            raise ValueError(f"Conditional setting '{name}' names no parameter")
        operators = [op for op in _CONDITION_OPERATORS if op in condition]
        if len(operators) != 1:
            raise ValueError(
                f"Conditional setting '{name}' must use exactly one of "
                + ", ".join(_CONDITION_OPERATORS)
            )

    def _validate_settings(
        self, name: str, target: str, settings: dict[str, Any]
    ) -> None:
        """Raise ValueError when a settings block is empty or holds invalid values."""
        if not isinstance(settings, dict) or not settings:
            raise ValueError(
                f"Conditional setting '{name}' applies no settings to '{target}'"
            )
        for key, value in settings.items():
            self._validate_apply_value(name, target, key, value)

    def _evaluate_condition(self, condition: dict[str, Any]) -> bool:
        """Evaluate a condition against this variant.

        An absent parameter evaluates as None, so each negative operator is
        the strict complement of its positive counterpart.
        """
        source = condition.get("source", "asic_config_params")
        param = condition["param"]
        if source == "asic_config_params":
            value = self.asic_config_params.get(param)
        elif source == "features":
            value = self.variant_config.get("features", {}).get(param)
        else:
            raise ValueError(f"Unknown condition source '{source}'")

        for operator, test in _CONDITION_OPERATORS.items():
            if operator in condition:
                return test(value, condition[operator])
        raise ValueError(f"Condition on '{param}' has no supported operator")

    def _matching_conditional_settings(self) -> list[dict[str, Any]]:
        """Return the entries whose condition holds, evaluated once per variant.

        ASIC-level entries precede platform-level entries, each in array order.
        """
        if self._active_conditional_settings is None:
            self._active_conditional_settings = [
                entry
                for entry in self._conditional_setting_entries()
                if self._evaluate_condition(entry["condition"])
            ]
        return self._active_conditional_settings

    def _collect_effect_values(self, effect: str) -> list[Any]:
        """Concatenate the values of a list-valued effect across matching entries."""
        values: list[Any] = []
        for entry in self._matching_conditional_settings():
            values.extend(entry.get(effect, []))
        return values

    def _execute_apply_effects(self) -> None:
        """Write the settings of every matching entry to their targets.

        Within an entry, apply_from executes before apply so that inline
        settings override the copied block.
        """
        for entry in self._matching_conditional_settings():
            if "apply_from" in entry:
                apply_from = entry["apply_from"]
                self._apply_settings(
                    apply_from["target_table"], self.asic_config[apply_from["source"]]
                )
            for target, settings in entry.get("apply", {}).items():
                self._apply_settings(target, settings)

    @abstractmethod
    def _apply_settings(self, target: str, settings: dict[str, Any]) -> None:
        """Write settings into the named output target."""
        ...

    @abstractmethod
    def _validate_apply_target(self, name: str, target: str) -> None:
        """Raise ValueError when the named output target does not exist."""
        ...

    @abstractmethod
    def _validate_apply_value(
        self, name: str, target: str, key: str, value: Any
    ) -> None:
        """Raise ValueError when a setting value is invalid for this output format."""
        ...

    @abstractmethod
    def generate(self) -> str:
        """Generate the complete ASIC config and return it as a string."""
        ...

    @property
    @abstractmethod
    def output_extension(self) -> str:
        """File extension for the generated output (e.g. '.yml' or '.json')."""
        ...
