# Copyright (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

# pyre-strict

"""Shared types and small helpers for mimic_config_gen."""

from __future__ import annotations

import dataclasses
import json
import sys
import typing as t

# Resource that holds every coop forwarding-stack input.
FORWARDING_STACK_RESOURCE = "fboss_forwarding_stack"
POLICYDB_RESOURCES_PATH = "neteng/policydb/resources"

# Buck target of the off-box coop generator we drive.
COOP_TEST_TARGET = "fbcode//neteng/fboss/coop/tests:coop_test"


class MimicError(Exception):
    """Any condition that should stop generation with an explained message."""


@dataclasses.dataclass(frozen=True)
class Cohort:
    """A (role, hardware) pair, optionally pinned to one real device."""

    role: str
    hw: str
    device: str | None = None

    def __str__(self) -> str:
        base = f"{self.role.lower()}/{self.hw.lower()}"
        return f"{base} [{self.device}]" if self.device else base


@dataclasses.dataclass(frozen=True)
class Donors:
    """The 2x2 the mimic is built from.

    Naming follows the CLI: `source` is the hardware the config is cloned from,
    `target` is the hardware we are pretending the device is.

        role_donor   {role, source_hw}   supplies role-shaped inputs + identity
        hw_donor     {bridge_role, target_hw}  supplies hardware-shaped inputs
        pivot        {bridge_role, source_hw}  lets us tell the two apart
    """

    role: str
    source_hw: str
    target_hw: str
    bridge_role: str
    source_device: str

    @property
    def role_donor(self) -> Cohort:
        return Cohort(self.role, self.source_hw, self.source_device)

    @property
    def hw_donor(self) -> Cohort:
        return Cohort(self.bridge_role, self.target_hw)

    @property
    def pivot(self) -> Cohort:
        return Cohort(self.bridge_role, self.source_hw)

    @property
    def target(self) -> Cohort:
        return Cohort(self.role, self.target_hw)


class Shape(str):
    """Which axis an input's content actually varies along."""


HW_SHAPED = Shape("hw")
ROLE_SHAPED = Shape("role")
CONFLICT = Shape("conflict")
EITHER = Shape("either")


@dataclasses.dataclass(frozen=True)
class Blocker:
    """A coop input that resolves for the donors but not for the target."""

    name: str
    shape: Shape
    chosen_path: str
    chosen_from: str
    role_path: str | None
    hw_path: str | None


def emit(msg: str = "") -> None:
    """Write a line of the user-facing report.

    A dedicated writer rather than print() so report output stays separable
    from logging, which goes to stderr.
    """
    sys.stdout.write(f"{msg}\n")


def section(title: str) -> None:
    emit()
    emit(f"== {title} " + "=" * max(0, 68 - len(title)))


def cfgr_path(artifact_value: str) -> str:
    """Strip materialization decoration to get a fetchable configerator path.

    Mirrors PolicyDBResourceResolver.__call__ in
    neteng/fboss/coop/data/input_path_determinator.py, including its unanchored
    replaces, so we resolve to exactly the path coop would have used -- but only
    for CONFIGERATOR artifacts. Callers must filter on artifact type first;
    passing an fbpkg artifact through here yields a package name, not a path.
    """
    return (
        artifact_value.replace("materialized_configs/", "")
        .replace("raw_configs/", "")
        .replace(".materialized_JSON", "")
    )


def unwrap_selection(doc: t.Mapping[str, t.Any]) -> dict[str, t.Any]:
    """Return the payload inside a coop input artifact.

    Artifacts are `{name, selections: [{selector, data: {<union arm>: ...}}]}`.
    The mimic only ever rewrites single-selection artifacts; callers that need
    multi-selection handling must do it themselves.
    """
    try:
        selections = doc["selections"]
        if len(selections) != 1:
            raise MimicError(
                f"expected exactly 1 selection, got {len(selections)}; "
                "this input needs selector-aware rewriting"
            )
        data = selections[0]["data"]
    except (KeyError, IndexError, TypeError) as e:
        raise MimicError(f"artifact is not shaped like a coop input: {e}") from e
    if len(data) != 1:
        raise MimicError(f"expected a single union arm, got {sorted(data)}")
    inner = data[next(iter(data))]
    if not isinstance(inner, dict):
        raise MimicError(
            f"expected a struct in the union arm, got {type(inner).__name__}"
        )
    return inner


@dataclasses.dataclass(frozen=True)
class SelectionCtx:
    """Who to resolve a multi-selection artifact for.

    whoami is a NetWhoAmI thrift (same type coop matches against) and
    enabled_features the device's on-state feature names (same membership
    coop checks selector `features` against).
    """

    whoami: t.Any
    enabled_features: t.FrozenSet[str]


def select_selection(
    doc: t.Mapping[str, t.Any],
    ctx: SelectionCtx,
    input_name: str,
) -> dict[str, t.Any]:
    """Return the payload of the selection coop would serve this device.

    Single-selection docs take the legacy path unchanged. Multi-selection
    docs pick the first selection whose selector matches, exactly like
    neteng.fboss.coop.data.input_chooser.resolve_selector (first match wins,
    evaluated with coop's own match_input_to_device, so semantics cannot
    drift from production). Raises MimicError with coop's nearest-mismatch
    report when nothing matches.
    """
    try:
        selections = doc["selections"]
    except (KeyError, TypeError) as e:
        raise MimicError(
            f"artifact {input_name!r} is not shaped like a coop input: {e}"
        ) from e
    if len(selections) == 1:
        return unwrap_selection(doc)
    if not selections:
        raise MimicError(f"artifact {input_name!r} has no selections to choose from")
    from configerator.structs.neteng.fboss.coop.mapping.thrift_types import Selector
    from neteng.fboss.coop.data.input_chooser import match_input_to_device
    from thrift.python import serializer

    nearest: list[str] = []
    for sel in selections:
        raw = sel.get("selector") or {}
        selector = serializer.deserialize(
            Selector, json.dumps(raw).encode(), serializer.Protocol.JSON
        )
        result = match_input_to_device(selector, ctx.whoami, set(ctx.enabled_features))
        if result.matches():
            return unwrap_selection({"selections": [sel]})
        nearest.append(result.get_match_string())
    raise MimicError(
        f"{input_name}: {len(selections)} selections but none matches this "
        f"device; nearest: {'; '.join(nearest)}"
    )
