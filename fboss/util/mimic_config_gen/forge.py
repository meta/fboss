# Copyright (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

# pyre-strict

"""Build the synthetic inputs coop will read.

Three transforms live here:

  * the netwhoami respin -- the source switch's real identity with only the
    hardware fields rewritten
  * the port-id remap -- logical port ids are platform-specific, so a cloned
    switch config has to be re-keyed onto the target's id space by port name
  * the management-port reconcile -- mgmt ports are re-keyed like any other
    port, but their speed and profile differ between platforms and have to be
    taken from the target hardware
"""

from __future__ import annotations

import json
import logging
import pathlib
import re
import typing as t

from fboss.util.mimic_config_gen.defs import MimicError, select_selection, SelectionCtx

logger: logging.Logger = logging.getLogger(__name__)

# Logical-port-id fields this module knows how to rewrite. Deliberately NOT a
# complete list of the port-id carriers in cfg::SwitchConfig -- the nested and
# map-keyed ones below are not handled, so we refuse to run when they are
# populated rather than emit a config with source-platform ids in them.
_PORT_ID_FIELDS: tuple[tuple[str, str], ...] = (
    ("ports", "logicalID"),
    ("vlanPorts", "logicalPort"),
)

# Port-id carriers we cannot yet remap. Each is a (top-level key, description);
# a non-empty value means the cloned config would keep source-platform ids,
# which silently points them at different physical ports on the target.
_UNHANDLED_PORT_ID_CARRIERS: tuple[tuple[str, str], ...] = (
    ("aggregatePorts", "memberPorts[].memberPortID"),
    ("mirrors", "destination.egressPort.logicalID"),
    ("mirrorOnDropReports", "mirrorPortId"),
    ("sFlowCollectors", "port references"),
)

_MANAGEMENT_PORT_TYPE = 4  # cfg::PortType::MANAGEMENT_PORT

# Coop feature names are `name`, or `name/value` for enum-valued features.
_FEATURE_NAME: re.Pattern[str] = re.compile(r"^[A-Za-z0-9_]+(/[A-Za-z0-9_]+)?$")


def enum_value(enum_name: str, member: str) -> int:
    """Resolve a netwhoami enum member to its wire value.

    netwhoami is inconsistent about enum casing (`Hardware` but `ASIC`), so try
    the documented spellings rather than hardcoding one.
    """
    from netwhoami import thrift_types as tt

    for candidate in (enum_name, enum_name.upper(), enum_name.capitalize()):
        enum = getattr(tt, candidate, None)
        if enum is not None:
            break
    else:
        raise MimicError(f"no netwhoami enum named {enum_name}")

    value = getattr(enum, member.upper(), None)
    if value is None:
        raise MimicError(f"{member} is not a valid netwhoami {enum_name}")
    return int(value)


def respin_json(
    base: t.Mapping[str, t.Any],
    role: str,
    hw: str,
    asic: str | None,
    asic_vendor: str | None,
) -> dict[str, t.Any]:
    """Copy a real netwhoami, rewriting only identity fields.

    Everything not listed here -- name, region, dc, chmodel, lcmodel, tags --
    is inherited verbatim, which is what makes the output "this switch's config
    as if it were target hardware" rather than a generic template.
    """
    out = dict(base)
    out["role"] = enum_value("Role", role)
    out["hw"] = enum_value("Hardware", hw)
    # An unknown asic must clear the field, not leave the source switch's in
    # place: keeping it would produce an identity that cannot exist ({hw:
    # target, asic: source}), and generator code branches on asic directly.
    for field, enum_name, value in (
        ("asic", "Asic", asic),
        ("asic_vendor", "AsicVendor", asic_vendor),
    ):
        if value:
            out[field] = enum_value(enum_name, value)
        else:
            out.pop(field, None)
    return out


def to_thrift(whoami_json: t.Mapping[str, t.Any]) -> t.Any:
    from netwhoami.thrift_types import NetWhoAmI
    from thrift.python import serializer

    return serializer.deserialize(
        NetWhoAmI, json.dumps(whoami_json).encode(), serializer.Protocol.JSON
    )


def write_json(obj: t.Any, path: pathlib.Path) -> pathlib.Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, indent=2, sort_keys=True))
    return path


def port_name_to_id(
    platform_mapping_doc: t.Mapping[str, t.Any],
    ctx: SelectionCtx,
) -> dict[str, int]:
    """Build {port name -> logical id} from a platform_mapping artifact."""
    inner = select_selection(platform_mapping_doc, ctx, "platform_mapping")
    ports = inner.get("ports")
    if not isinstance(ports, dict):
        raise MimicError("platform_mapping artifact has no ports map")
    mapping: dict[str, int] = {}
    for pid, entry in ports.items():
        name = (entry.get("mapping") or {}).get("name")
        if not name:
            continue
        if name in mapping:
            raise MimicError(
                f"target platform_mapping lists port name {name} twice "
                f"(ids {mapping[name]} and {pid}); cannot key a remap on names"
            )
        mapping[name] = int(pid)
    if not mapping:
        raise MimicError("target platform_mapping has no named ports")
    return mapping


def remap_port_ids(
    template_doc: dict[str, t.Any],
    name_to_id: t.Mapping[str, int],
    ctx: SelectionCtx,
) -> dict[int, int]:
    """Re-key a cloned switch-config template onto the target's port-id space.

    Logical ids are assigned per platform: the same id denotes a different
    physical port on different hardware, so cloning ids across platforms
    silently mis-wires the config. Port *names* are the portable key.

    Mutates template_doc in place; returns {old id -> new id}.
    """
    inner = select_selection(template_doc, ctx, "agent_sw_template")
    ports = inner.get("ports") or []
    if not ports:
        raise MimicError("switch-config template has no ports to remap")

    _reject_unhandled_port_id_carriers(inner)

    old_to_new: dict[int, int] = {}
    missing: list[str] = []
    for port in ports:
        name = port.get("name")
        if "logicalID" not in port:
            raise MimicError(f"template port {name!r} has no logicalID to remap")
        if name not in name_to_id:
            missing.append(str(name))
            continue
        old_to_new[port["logicalID"]] = name_to_id[name]
    if missing:
        raise MimicError(
            f"{len(missing)} port(s) in the source config do not exist on the "
            f"target platform, e.g. {missing[:5]}. The two platforms are not "
            "port-compatible; pick a different --source-hw."
        )
    if len(old_to_new) != len(ports):
        raise MimicError(
            f"template has {len(ports)} ports but only {len(old_to_new)} "
            "distinct logical ids; refusing to remap an ambiguous mapping"
        )
    if len(set(old_to_new.values())) != len(old_to_new):
        raise MimicError(
            "two source ports map onto the same target port id; the source "
            "config is not port-compatible with the target platform"
        )

    for collection, field in _PORT_ID_FIELDS:
        for item in inner.get(collection) or []:
            old = item.get(field)
            if old in old_to_new:
                item[field] = old_to_new[old]
            elif old is not None:
                raise MimicError(
                    f"{collection}[].{field}={old} has no port to remap onto; "
                    "refusing to emit a config with a dangling port reference"
                )
    return old_to_new


def _reject_unhandled_port_id_carriers(inner: t.Mapping[str, t.Any]) -> None:
    """Refuse to remap a config containing port ids we cannot rewrite.

    Failing loudly here is the point: a populated aggregatePort or PORT-type
    interface would otherwise keep source-platform ids, which denote different
    physical ports on the target -- a mis-wired config with no error.
    """
    populated = [
        f"{key} ({what})" for key, what in _UNHANDLED_PORT_ID_CARRIERS if inner.get(key)
    ]
    port_rifs = [
        i for i in inner.get("interfaces") or [] if i.get("portID") is not None
    ]
    if port_rifs:
        populated.append(f"interfaces[].portID ({len(port_rifs)} PORT-type RIFs)")
    if inner.get("dataPlaneTrafficPolicy", {}).get("portIdToQosPolicy"):
        populated.append("dataPlaneTrafficPolicy.portIdToQosPolicy (map keys)")
    if populated:
        raise MimicError(
            "the source config uses port-id fields this tool cannot remap yet: "
            + "; ".join(populated)
            + ". Remapping only ports[] and vlanPorts[] would leave "
            "source-platform ids in those fields, silently pointing them at "
            "different physical ports on the target."
        )


def reconcile_management_ports(
    template_doc: dict[str, t.Any],
    hw_donor_template_doc: t.Mapping[str, t.Any],
    ctx: SelectionCtx,
    hw_ctx: SelectionCtx,
) -> list[tuple[str, tuple[int, int], tuple[int, int]]]:
    """Take mgmt-port speed/profile from the target hardware's own config.

    Management ports are not part of the fabric and their supported speeds
    differ between platforms, so the cloned values are frequently invalid.
    Returns [(name, (old speed, old profile), (new speed, new profile))].
    """
    donor = {
        p["name"]: p
        for p in select_selection(
            hw_donor_template_doc, hw_ctx, "agent_sw_template"
        ).get("ports")
        or []
        if p.get("portType") == _MANAGEMENT_PORT_TYPE
    }
    changed: list[tuple[str, tuple[int, int], tuple[int, int]]] = []
    for port in (
        select_selection(template_doc, ctx, "agent_sw_template").get("ports") or []
    ):
        if port.get("portType") != _MANAGEMENT_PORT_TYPE:
            continue
        ref = donor.get(port["name"])
        if ref is None:
            logger.warning(
                "mgmt port %s absent from the target hardware config; leaving as-is",
                port["name"],
            )
            continue
        before = (port.get("speed"), port.get("profileID"))
        after = (ref.get("speed"), ref.get("profileID"))
        if None in after:
            logger.warning(
                "target hardware mgmt port %s has no speed/profile; leaving as-is",
                port["name"],
            )
            continue
        if before != after:
            port["speed"], port["profileID"] = after
            changed.append((port["name"], before, after))
    return changed


def parse_feature_overrides(spec: str | None) -> dict[str, str]:
    """Parse a `name:on,name:off` string into explicit feature states.

    Same syntax as `coop_test --feature-overrides`, including splitting on the
    LAST colon as coop does, so a spec moves between the two tools unchanged.
    """
    states: dict[str, str] = {}
    for entry in (spec or "").split(","):
        entry = entry.strip()
        if not entry:
            continue
        name, sep, state = entry.rpartition(":")
        name, state = name.strip(), state.strip().lower()
        if not sep or not name or state not in ("on", "off"):
            raise MimicError(
                f"invalid feature override {entry!r}: expected 'name:on' or 'name:off'"
            )
        if not _FEATURE_NAME.match(name):
            # coop joins the name straight into a filesystem path with
            # parents=True, so a path-shaped name would write outside the
            # coop dir instead of failing.
            raise MimicError(
                f"invalid feature name {name!r}: expected `name` or `name/value`"
            )
        if name in states:
            raise MimicError(
                f"feature {name} given twice in --feature-overrides; "
                "remove the duplicate rather than relying on ordering"
            )
        states[name] = state
    return states


def apply_feature_overrides(
    pins: dict[str, str], overrides: t.Mapping[str, str]
) -> list[tuple[str, str | None, str]]:
    """Layer explicit feature states over the source switch's pinned ones.

    Suppressing a feature answers "what would coop emit without this?", which
    is how you find the one feature responsible for a bad config and then
    re-enable the rest incrementally.

    Mutates `pins`; returns [(name, previous, new)] for the ones that changed.
    `previous` is None when the feature was not pinned for the source switch,
    which usually means a typo but is legitimate when the feature is outside
    that device's scope -- coop simply never reads the override in that case.
    """
    changes: list[tuple[str, str | None, str]] = []
    for name, state in sorted(overrides.items()):
        before = pins.get(name)
        if before == state:
            continue
        pins[name] = state
        changes.append((name, before, state))
    return changes


def capture_feature_pins(coop_dir: pathlib.Path) -> dict[str, str]:
    """Read every feature state coop computed for the source switch.

    Feature scopes key on hardware, so re-evaluating them against the forged
    identity would silently enable features meant for the target's other roles.
    Pinning the source switch's states keeps the config faithful to "this
    switch, different hardware".

    Coop stores boolean features at `features/<name>/current/{on,off}` and
    enum-valued ones one level deeper at `features/<name>/<value>/current/`,
    naming the latter `<name>/<value>`. Both must be pinned: the enum ones are
    hardware-scoped knobs, so missing them defeats the purpose of pinning.
    """
    root = coop_dir / ".METADATA" / "features"
    if not root.is_dir():
        raise MimicError(f"no feature metadata under {root}; did the baseline run?")

    def state_of(d: pathlib.Path) -> str | None:
        current = d / "current"
        if not current.is_dir():
            return None
        states = {p.name for p in current.iterdir()}
        return "on" if "on" in states else ("off" if "off" in states else None)

    pins: dict[str, str] = {}
    for feature_dir in sorted(root.iterdir()):
        if not feature_dir.is_dir():
            continue
        state = state_of(feature_dir)
        if state is not None:
            pins[feature_dir.name] = state
            continue
        # Enum-valued: one sub-directory per possible value.
        for value_dir in sorted(feature_dir.iterdir()):
            if not value_dir.is_dir():
                continue
            value_state = state_of(value_dir)
            if value_state is not None:
                pins[f"{feature_dir.name}/{value_dir.name}"] = value_state
    if not pins:
        raise MimicError(f"no feature states found under {root}")
    return pins
