#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

# pyre-strict

"""Generate a coop config for a {role, hardware} pair that does not exist yet.

Takes a role that already runs on some source hardware, and emits the config as
if one real source switch were target hardware instead. Everything about the
device -- name, region, fabric topology, features -- is inherited from that
switch; only the hardware identity and the hardware-shaped inputs are swapped.

The result is intended for off-box validation (agent invariant tests) before
any provisioning exists. A run that fails is still useful: the failure names
the concrete incompatibility between the two platforms.

Name a switch and the hardware to pretend it is:

    buck2 run //fboss/util/mimic_config_gen:mimic_config_gen -- \\
        --source-device ssw008.s001.m065.lco1.tfbnw.net --target-hw MINIPACK3N

or name the cohort and let it pick a healthy device:

    buck2 run //fboss/util/mimic_config_gen:mimic_config_gen -- \\
        --role SSW --source-hw MONTBLANC --target-hw MINIPACK3N
"""

from __future__ import annotations

import argparse
import json
import logging
import pathlib
import sys

from fboss.util.mimic_config_gen import discovery, forge, generate, policydb, verify
from fboss.util.mimic_config_gen.defs import (
    Blocker,
    CONFLICT,
    Donors,
    emit,
    MimicError,
    section,
    SelectionCtx,
)

logger: logging.Logger = logging.getLogger(__name__)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument(
        "--target-hw", required=True, help="Hardware to generate for, e.g. MINIPACK3N"
    )
    p.add_argument(
        "--source-device",
        help="Switch to clone. Its role and hardware are read from netwhoami, "
        "so --role/--source-hw are not needed with it.",
    )
    p.add_argument(
        "--role",
        help="Target role, e.g. SSW. Derived from --source-device if given.",
    )
    p.add_argument(
        "--source-hw",
        help="Hardware to clone from, e.g. MONTBLANC. Derived from "
        "--source-device if given.",
    )
    p.add_argument(
        "--bridge-role",
        help="Role supplying target-hw platform artifacts. Defaults to the "
        "best-populated role that both hardwares serve.",
    )
    p.add_argument(
        "--out",
        type=pathlib.Path,
        default=pathlib.Path("/tmp/mimic_config_gen"),
        help="Output directory",
    )
    p.add_argument(
        "--feature-overrides",
        help="Force feature states, e.g. 'dc_sflow_mirror:off,pfc:on'. Applied "
        "over the states inherited from the source switch. Suppress a feature "
        "to see what coop emits without it, then re-enable incrementally.",
    )
    p.add_argument(
        "--scan-only",
        action="store_true",
        help="Report donors and input gaps, then stop without generating",
    )
    p.add_argument("--debug", action="store_true")
    return p.parse_args(argv)


def _report_donors(
    donors: Donors, target_roles: dict[str, int], warnings: list[str]
) -> None:
    section("Donors")
    emit(f"  target        {donors.target}")
    emit(f"  role donor    {donors.role_donor}")
    emit(f"  hw donor      {donors.hw_donor}")
    emit(f"  pivot         {donors.pivot}")
    emit()
    emit(
        f"  bridge role   {donors.bridge_role} "
        f"({target_roles[donors.bridge_role]} {donors.target_hw} devices)"
    )
    others = sorted(r for r in target_roles if r != donors.bridge_role)
    if others:
        emit(f"  {donors.target_hw} also serves: " + ", ".join(others))
    for warning in warnings:
        emit(f"  WARNING {warning}")


def _report_blockers(blockers: list[Blocker], unsourceable: list[str]) -> None:
    section(f"Missing coop inputs ({len(blockers)})")
    if not blockers:
        emit("  none -- this pair already resolves every forwarding-stack input")
    for b in blockers:
        # Report where the artifact actually came from, not what the shape
        # implies: a role-shaped input with no role donor still falls back to
        # the hardware donor.
        origin = "target-hw" if b.chosen_from == "hw_donor" else "source-hw"
        flag = "  <-- CONFLICT, review" if b.shape == CONFLICT else ""
        emit(f"  {b.name:34} {b.shape:8} <- {origin}{flag}")
    if unsourceable:
        section(f"Inputs with no configerator donor ({len(unsourceable)})")
        for name in unsourceable:
            emit(f"  {name}   (not a coop input -- e.g. an fbpkg reference)")


def _report_checks(checks: list[verify.Check]) -> int:
    section("Findings")
    for c in checks:
        emit(f"  [{c.marker}] {c.name:22} {c.detail}")
    return sum(1 for c in checks if not c.ok)


def run(args: argparse.Namespace) -> int:
    out: pathlib.Path = args.out.resolve()
    baseline_dir = out / "baseline"
    mimic_dir = out / "mimic"
    work = out / "work"

    # Parse before anything expensive: a malformed spec should not cost a full
    # baseline generation to discover, and --scan-only should reject it too.
    feature_overrides = forge.parse_feature_overrides(args.feature_overrides)

    donors, target_roles, warnings = discovery.resolve(
        args.role, args.source_hw, args.target_hw, args.source_device, args.bridge_role
    )
    _report_donors(donors, target_roles, warnings)

    # Real config for the real device. Also yields its live netwhoami and the
    # feature states we will pin.
    section("Baseline")
    emit(f"  generating real config for {donors.source_device} ...")
    ok, log = generate.baseline(donors.source_device, baseline_dir)
    # Write the log before acting on the result: the failure path is the one
    # that needs it, and it is the path the error message points at.
    work.mkdir(parents=True, exist_ok=True)
    (work / "baseline.log").write_text(log)
    if not ok:
        raise MimicError(
            f"baseline generation failed: {generate.failure_reason(log)}\n"
            f"(full log: {work / 'baseline.log'})"
        )
    emit(f"  ok -> {baseline_dir}")

    whoami_file = baseline_dir / "netwhoami.json"
    try:
        base_whoami = json.loads(whoami_file.read_text())
    except (OSError, json.JSONDecodeError) as e:
        raise MimicError(
            f"baseline reported success but {whoami_file} is unusable ({e}); "
            f"see {work / 'baseline.log'}"
        ) from e

    profiles: dict[str, tuple[str | None, str | None]] = {}
    for hw in (donors.source_hw, donors.target_hw):
        asic, vendor, asic_warnings = discovery.asic_profile(hw)
        profiles[hw] = (asic, vendor)
        for warning in asic_warnings:
            emit(f"  WARNING {warning}")

    # Pin the source switch's feature states before forging: multi-selection
    # artifacts resolve their selector against these states, so selection must
    # see the final (post-override) pins.
    section("Features")
    pins = forge.capture_feature_pins(baseline_dir)
    on = sum(1 for v in pins.values() if v == "on")
    emit(
        f"  features pinned to source switch: {len(pins)} ({on} on, {len(pins) - on} off)"
    )
    for name, before, after in forge.apply_feature_overrides(pins, feature_overrides):
        origin = before if before is not None else "unpinned"
        emit(f"  feature override {name}: {origin} -> {after}")
    if feature_overrides:
        # Say what coop actually receives: an override for an unpinned feature
        # adds an entry, and a no-op override prints nothing above.
        emit(f"  {len(feature_overrides)} override(s) applied, {len(pins)} to coop")
    forge.write_json(pins, work / "feature_pins.json")

    # Identities for selection resolution: the role donor (whose template we
    # clone) and the hw donor (whose platform artifacts we borrow), each with
    # the donor's pinned feature states. Same construction as the scan
    # corners, so selection sees exactly what PolicyDB matching saw.
    enabled = frozenset(n for n, s in pins.items() if s == "on")
    role_ctx = SelectionCtx(
        forge.to_thrift(
            forge.respin_json(
                base_whoami,
                donors.role,
                donors.source_hw,
                *profiles[donors.source_hw],
            )
        ),
        enabled,
    )
    hw_ctx = SelectionCtx(
        forge.to_thrift(
            forge.respin_json(
                base_whoami,
                donors.bridge_role,
                donors.target_hw,
                *profiles[donors.target_hw],
            )
        ),
        enabled,
    )

    # Which inputs does the target pair not resolve, and where can each come from?
    section("Scanning PolicyDB")
    scanner = policydb.Scanner(donors, base_whoami, profiles)
    blockers, unsourceable = scanner.scan()
    _report_blockers(blockers, unsourceable)

    if args.scan_only:
        return 0

    # Materialise the donor artifacts as local override files.
    overrides_dir = work / "overrides"
    overrides_dir.mkdir(parents=True, exist_ok=True)
    overrides: dict[str, pathlib.Path] = {}
    for b in blockers:
        path = overrides_dir / b.name
        path.write_text(scanner.fetch(b.chosen_path))
        overrides[b.name] = path

    # Re-key the cloned switch config onto the target's port-id space.
    section("Forging")
    target_rif: str | None = None
    template_blocker = next(
        (b for b in blockers if b.name == "agent_sw_template"), None
    )
    if template_blocker is not None:
        # The template came from the source platform, so its logical port ids
        # mean different physical ports on the target. Remapping needs the
        # target's platform_mapping; without it we would emit a mis-wired
        # config, so this is an error rather than a skip.
        if "platform_mapping" not in overrides:
            raise MimicError(
                "the switch-config template is cloned from the source platform "
                "but platform_mapping is not, so its port ids cannot be "
                "re-keyed. Emitting it unchanged would silently point every "
                "port at the wrong physical port on the target."
            )
        pmap = json.loads(overrides["platform_mapping"].read_text())
        template = json.loads(overrides["agent_sw_template"].read_text())
        remapped = forge.remap_port_ids(
            template, forge.port_name_to_id(pmap, role_ctx), role_ctx
        )
        emit(f"  port ids remapped by name: {len(remapped)}")

        hw_template_path = template_blocker.hw_path
        if hw_template_path:
            hw_template = json.loads(scanner.fetch(hw_template_path))
            target_rif = verify.template_rif_model(hw_template, hw_ctx)
            cloned_rif = verify.template_rif_model(template, role_ctx)
            if target_rif != cloned_rif:
                emit(
                    f"  WARNING RIF model differs: cloned={cloned_rif} "
                    f"target_hw_uses={target_rif}"
                )
            for name, before, after in forge.reconcile_management_ports(
                template, hw_template, role_ctx, hw_ctx
            ):
                emit(f"  mgmt port {name}: speed/profile {before} -> {after}")
        else:
            emit(
                "  WARNING no target-hardware template; RIF-model check and "
                "mgmt-port reconcile both skipped"
            )
        overrides["agent_sw_template"].write_text(json.dumps(template))
    else:
        emit("  no port remap needed (the target resolves its own template)")

    asic, vendor = profiles[donors.target_hw]
    whoami_path = forge.write_json(
        forge.respin_json(base_whoami, donors.role, donors.target_hw, asic, vendor),
        work / "netwhoami.json",
    )
    emit(f"  netwhoami: hw={donors.target_hw} asic={asic} asic_vendor={vendor}")

    section("Generating")
    ok, log = generate.mimic(whoami_path, mimic_dir, overrides, pins)
    (work / "mimic.log").write_text(log)
    if not ok:
        emit(f"  FAILED: {generate.failure_reason(log)}")
        emit(f"  log: {work / 'mimic.log'}")
        section("Result")
        emit("  No config produced. The failure above names the incompatibility")
        emit("  between the two platforms -- that is the gap to close.")
        return 1
    emit(f"  ok -> {mimic_dir}")

    agent_config = mimic_dir / "agent" / "current"
    if not agent_config.is_file():
        # coop exited 0 without producing the artifact. That is a tool or
        # harness failure, not a finding about the target hardware.
        raise MimicError(
            f"coop reported success but {agent_config} was not written; "
            f"see {work / 'mimic.log'}"
        )

    gaps = _report_checks(
        verify.compare(agent_config, baseline_dir / "agent" / "current", target_rif)
    )
    section("Result")
    emit(f"  config    {mimic_dir / 'agent' / 'current'}")
    emit(f"  baseline  {baseline_dir / 'agent' / 'current'}")
    emit()
    if gaps:
        emit(f"  Config produced, with {gaps} finding(s) above. That is a normal")
        emit("  outcome: each GAP is a place where coop's config-generation logic")
        emit("  does not yet handle this hardware in this role.")
    else:
        emit("  Config produced with no findings.")
    return 0


def main() -> int:
    args = parse_args()
    logging.basicConfig(
        level=logging.DEBUG if args.debug else logging.INFO,
        format="%(levelname)s %(name)s: %(message)s",
        stream=sys.stderr,
    )
    try:
        return run(args)
    except MimicError as e:
        logger.debug("mimic_config_gen failed", exc_info=True)
        emit()
        emit(f"error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
