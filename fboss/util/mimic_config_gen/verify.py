# Copyright (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

# pyre-strict

"""Characterise a generated mimic config.

The config is whatever coop produces, and it is expected to be wrong in places
-- that is the point. These observations turn "wrong somewhere" into specific,
located deltas, each of which is a candidate gap in coop or in config
generation. They are findings, not a pass/fail gate: a config with findings is
a successful run.

The canonical example: if the target hardware uses a different routed-interface
model than the source, coop's hardware-branching interface join yields nothing
and every interface silently loses its addresses -- no exception, no warning.
"""

from __future__ import annotations

import dataclasses
import json
import logging
import pathlib
import typing as t

from fboss.util.mimic_config_gen.defs import MimicError, select_selection, SelectionCtx

logger: logging.Logger = logging.getLogger(__name__)

_INTERFACE_TYPE_NAMES = {1: "VLAN", 2: "SYSTEM_PORT", 3: "PORT"}


@dataclasses.dataclass
class Check:
    """One observation about the generated config.

    `ok` False means "this differs from what the target hardware expects", i.e.
    a lead to chase in coop -- not a tool failure.
    """

    name: str
    ok: bool
    detail: str

    @property
    def marker(self) -> str:
        return "ok " if self.ok else "GAP"


def _load(path: pathlib.Path) -> dict[str, t.Any] | None:
    try:
        return json.loads(path.read_text())
    except Exception as e:  # noqa: BLE001 - report rather than crash the run
        logger.warning("could not read %s: %s", path, e)
        return None


def _rif_model(sw: t.Mapping[str, t.Any]) -> str:
    kinds = {i.get("type") for i in sw.get("interfaces") or []}
    kinds.discard(None)
    if not kinds:
        return "none"
    return "+".join(sorted(_INTERFACE_TYPE_NAMES.get(k, str(k)) for k in kinds))


def template_rif_model(
    template_doc: t.Mapping[str, t.Any],
    ctx: SelectionCtx,
    input_name: str = "agent_sw_template",
) -> str:
    """RIF model declared by a coop switch-config template.

    Returns "unknown" rather than raising: this is a characterisation input,
    and an artifact shape we cannot read should not abort generation.
    """
    try:
        return _rif_model(select_selection(template_doc, ctx, input_name))
    except MimicError as e:
        logger.warning("could not read the target RIF model: %s", e)
        return "unknown"


def compare(
    mimic_path: pathlib.Path,
    baseline_path: pathlib.Path,
    target_rif_model: str | None = None,
) -> list[Check]:
    """Compare a mimic agent config against the source device's real one.

    `target_rif_model` is the model the target hardware's own config uses. It
    must come from the hardware donor rather than from either generated config:
    both of those inherit the source template's model, so comparing them to
    each other cannot reveal a mismatch with the target platform.
    """
    mimic = _load(mimic_path)
    base = _load(baseline_path)
    checks: list[Check] = []
    if mimic is None:
        return [Check("parse", False, f"could not parse {mimic_path}")]
    checks.append(Check("parse", True, f"{mimic_path.stat().st_size} bytes"))

    msw = mimic.get("sw") or {}
    have_baseline = base is not None
    bsw = (base or {}).get("sw") or {}
    if not have_baseline:
        # Without it, every comparison below degrades to a verdict derived from
        # an empty dict -- which reads as a pass. Say so once, loudly.
        checks.append(
            Check("baseline", False, f"missing or unreadable: {baseline_path}")
        )

    # 1. Interface addressing. The silent-failure canary. Compared against the
    #    baseline's own blank count, because a production config can carry a
    #    legitimately address-less interface and a perfect mimic inherits it.
    intfs = msw.get("interfaces") or []
    blank = [i for i in intfs if not i.get("ipAddresses")]
    base_intfs = bsw.get("interfaces") or []
    base_blank = [i for i in base_intfs if not i.get("ipAddresses")]
    if not intfs:
        checks.append(
            Check("interface addressing", False, "config has NO interfaces at all")
        )
    else:
        extra = len(blank) - len(base_blank)
        detail = f"{len(intfs) - len(blank)}/{len(intfs)} interfaces have IPs"
        if have_baseline:
            detail += (
                f" (source: {len(base_intfs) - len(base_blank)}/{len(base_intfs)})"
            )
        if extra > 0:
            detail += f" -- {extra} MORE blank than the source, not routable"
        checks.append(
            Check("interface addressing", extra <= 0 and have_baseline, detail)
        )

    # 2. Routed-interface model. A mismatch with the TARGET hardware's model is
    #    the usual cause of (1): coop branches on hardware when joining
    #    interfaces to the GSC, so a template carrying the wrong model produces
    #    no join at all -- silently, with no error.
    emitted = _rif_model(msw)
    if target_rif_model in (None, "unknown"):
        # Not knowing the target model means the tool's most important check
        # did not run. Report that as a finding rather than as a pass.
        checks.append(
            Check(
                "RIF model",
                False,
                f"cloned={emitted}, target model UNDETERMINED -- this check "
                "did not run",
            )
        )
    else:
        matches = emitted == target_rif_model
        checks.append(
            Check(
                "RIF model",
                matches,
                f"cloned={emitted} target_hw_uses={target_rif_model}"
                + (
                    ""
                    if matches
                    else "  <-- MISMATCH; the cloned template needs RIF conversion"
                ),
            )
        )

    # 3. Topology preserved: same physical ports as the source device.
    m_names = {p.get("name") for p in msw.get("ports") or []}
    b_names = {p.get("name") for p in bsw.get("ports") or []}
    if b_names:
        checks.append(
            Check(
                "port-name parity",
                m_names == b_names,
                f"{len(m_names)} ports"
                + (
                    ""
                    if m_names == b_names
                    else f"; +{len(m_names - b_names)} -{len(b_names - m_names)} vs source"
                ),
            )
        )

    # 4. Platform actually swapped -- otherwise we just cloned the source.
    #    Order-insensitive, and "absent" is distinguished from a real value so
    #    a config with no switch settings cannot read as a successful swap.
    def asics_of(sw: t.Mapping[str, t.Any]) -> list[str] | None:
        settings = sw.get("switchSettings") or {}
        info = settings.get("switchIdToSwitchInfo")
        if not info:
            return None
        return sorted(str(v.get("asicType")) for v in info.values())

    m_asic, b_asic = asics_of(msw), asics_of(bsw)
    if m_asic is None or b_asic is None:
        missing = "target" if m_asic is None else "source"
        checks.append(
            Check("asic swapped", False, f"{missing} config has no switchSettings")
        )
    else:
        swapped = m_asic != b_asic
        checks.append(
            Check(
                "asic swapped",
                swapped,
                f"source={','.join(b_asic)} target={','.join(m_asic)}"
                + ("" if swapped else "  <-- IDENTICAL, platform did not swap"),
            )
        )

    # 5. Port ids re-keyed onto the target's space.
    m_ids = {p.get("logicalID") for p in msw.get("ports") or [] if "logicalID" in p}
    b_ids = {p.get("logicalID") for p in bsw.get("ports") or [] if "logicalID" in p}
    if b_ids and m_ids:
        checks.append(
            Check(
                "port ids re-keyed",
                m_ids != b_ids,
                f"source max={max(b_ids)} target max={max(m_ids)}",
            )
        )
    elif have_baseline:
        checks.append(
            Check("port ids re-keyed", False, "one of the configs has no port ids")
        )

    sdk = msw.get("sdkVersion")
    if sdk:
        checks.append(Check("sdk version", True, json.dumps(sdk, sort_keys=True)))
    return checks
