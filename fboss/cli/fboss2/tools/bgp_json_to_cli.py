#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

# pyre-unsafe

"""
BGP++ JSON to fboss2 CLI converter (j2c tool)

Converts BGP++ thrift-compatible JSON configuration to fboss2 CLI commands.
This enables round-trip conversion: JSON -> CLI -> JSON (via fboss2).

Usage:
    python3 bgp_json_to_cli.py <input.json> [--output output.sh]

Example:
    python3 bgp_json_to_cli.py rsw_bgp_config.json > configure_bgp.sh
"""

import argparse
import json
import shlex
import sys
from typing import Any


def escape_shell_arg(arg: Any) -> str:
    """Quote a JSON-sourced value for the generated shell script.

    Applied to every value taken from the input JSON, numeric fields included:
    the script does not validate types, and shlex.quote leaves plain digits
    and identifiers unchanged, so valid input produces identical output.
    """
    return shlex.quote(str(arg))


def _shell_bool(value: Any) -> str:
    return escape_shell_arg(str(value).lower())


def _generate_global_basic_commands(config: dict[str, Any]) -> list[str]:
    """Generate basic global BGP commands."""
    commands = []
    if "router_id" in config:
        commands.append(
            f"config protocol bgp global router-id {escape_shell_arg(config['router_id'])}"
        )
    if "local_as_4_byte" in config:
        commands.append(
            f"config protocol bgp global local-asn {escape_shell_arg(config['local_as_4_byte'])}"
        )
    if "hold_time" in config:
        commands.append(
            f"config protocol bgp global hold-time {escape_shell_arg(config['hold_time'])}"
        )
    if "local_confed_as_4_byte" in config:
        commands.append(
            f"config protocol bgp global confed-asn {escape_shell_arg(config['local_confed_as_4_byte'])}"
        )
    return commands


def _generate_network6_commands(networks: list[dict[str, Any]]) -> list[str]:
    """Generate network6 commands for global BGP config."""
    commands = []
    for network in networks:
        prefix = network.get("prefix", "")
        if not prefix:
            continue
        cmd_parts = [
            f"config protocol bgp global network6 add {escape_shell_arg(prefix)}"
        ]
        if "policy_name" in network:
            cmd_parts.append(f"policy {escape_shell_arg(network['policy_name'])}")
        if "install_to_fib" in network:
            cmd_parts.append(f"install-to-fib {_shell_bool(network['install_to_fib'])}")
        if "minimum_supporting_routes" in network:
            cmd_parts.append(
                f"min-routes {escape_shell_arg(network['minimum_supporting_routes'])}"
            )
        commands.append(" ".join(cmd_parts))
    return commands


def _generate_switch_limit_commands(switch_limit: dict[str, Any]) -> list[str]:
    """Generate switch_limit_config commands."""
    commands = []
    if "prefix_limit" in switch_limit:
        commands.append(
            f"config protocol bgp global switch-limit {escape_shell_arg(switch_limit['prefix_limit'])}"
        )
    if "total_path_limit" in switch_limit:
        commands.append(
            f"config protocol bgp global switch-limit-total-path {escape_shell_arg(switch_limit['total_path_limit'])}"
        )
    if "max_golden_vips" in switch_limit:
        commands.append(
            f"config protocol bgp global switch-limit-max-golden-vips {escape_shell_arg(switch_limit['max_golden_vips'])}"
        )
    if "overload_protection_mode" in switch_limit:
        commands.append(
            f"config protocol bgp global switch-limit-overload-protection-mode {escape_shell_arg(switch_limit['overload_protection_mode'])}"
        )
    return commands


def generate_global_commands(config: dict[str, Any]) -> list[str]:
    """Generate CLI commands for global BGP settings."""
    commands = []
    commands.extend(_generate_global_basic_commands(config))
    commands.extend(_generate_network6_commands(config.get("networks6", [])))
    commands.extend(
        _generate_switch_limit_commands(config.get("switch_limit_config", {}))
    )
    return commands


def format_bandwidth(bps: int) -> str:
    """Format bandwidth in bps with the largest exact K/M/G suffix.

    Only K, M and G are emitted: those are the multipliers bgpd's
    link-bandwidth parser accepts, so a "T" spelling would not round-trip.
    """
    if bps >= 1_000_000_000 and bps % 1_000_000_000 == 0:
        return f"{bps // 1_000_000_000}G"
    if bps >= 1_000_000 and bps % 1_000_000 == 0:
        return f"{bps // 1_000_000}M"
    if bps >= 1_000 and bps % 1_000 == 0:
        return f"{bps // 1_000}K"
    return str(bps)


def _printable(text: Any) -> str:
    """A JSON-sourced name made safe for a `#` comment line: no control
    characters, so it cannot break out of the comment."""
    return "".join(ch if ch.isprintable() else "?" for ch in str(text))


# ---------------------------------------------------------------------------
# Fields BgpPeer and PeerGroup share. The neighbor and peer-group dispatchers
# spell these attributes identically, so one table drives both generators.
# ---------------------------------------------------------------------------

# (json field, CLI attribute) — boolean flags.
_BOOL_FIELDS = [
    ("is_rr_client", "rr-client"),
    ("is_confed_peer", "confed-peer"),
    ("is_redistribute_peer", "redistribute-peer"),
    ("enhanced_route_refresh", "enhanced-route-refresh"),
    ("route_refresh", "route-refresh"),
    ("remove_private_as", "remove-private-as"),
    ("enforce_first_as", "enforce-first-as"),
    ("disable_ipv4_afi", "afi disable-ipv4-afi"),
    ("disable_ipv6_afi", "afi disable-ipv6-afi"),
    ("v4_over_v6_nexthop", "afi ipv4-over-ipv6-nh"),
    ("enable_stateful_ha", "graceful-restart stateful-ha"),
    ("next_hop_self", "next-hop-self"),
]

# (json field, CLI attribute) — integer-valued attributes.
_INT_FIELDS = [("ttl_security_hops", "ttl-security-hops")]

_TIMER_FIELDS = [
    ("hold_time_seconds", "timers hold-time"),
    ("keep_alive_seconds", "timers keepalive"),
    ("out_delay_seconds", "timers out-delay"),
    ("withdraw_unprog_delay_seconds", "timers withdraw-unprog-delay"),
    ("graceful_restart_seconds", "graceful-restart restart-time"),
]

# RouteLimit leaves, as (json field, attribute suffix, is_bool); the prefix is
# `max-route pre-` or `max-route post-`.
_ROUTE_LIMIT_FIELDS = [
    ("max_routes", "filter", False),
    ("warning_limit", "warning-threshold", False),
    ("warning_only", "warning-only", True),
]

# Every shared key the generators below convert. Anything else present on the
# JSON object is reported (see _unconverted_field_warnings) instead of being
# dropped silently.
_SHARED_HANDLED_FIELDS = (
    {
        "is_passive",
        "link_bandwidth_bps",
        "advertise_link_bandwidth",
        "receive_link_bandwidth",
        "add_path",
        "bgp_peer_timers",
        "pre_filter",
        "post_filter",
    }
    | {field for field, _ in _BOOL_FIELDS}
    | {field for field, _ in _INT_FIELDS}
)
_TIMER_HANDLED_FIELDS = {field for field, _ in _TIMER_FIELDS}
_ROUTE_LIMIT_HANDLED_FIELDS = {field for field, _, _ in _ROUTE_LIMIT_FIELDS}


def _generate_scalar_commands(
    obj: dict[str, Any], prefix: str, fields: list[tuple[str, str]]
) -> list[str]:
    """Generate one command per CLI attribute, preferring its first field."""
    commands = []
    generated_attributes = set()
    for field, cli_name in fields:
        if field in obj and cli_name not in generated_attributes:
            commands.append(f"{prefix} {cli_name} {escape_shell_arg(obj[field])}")
            generated_attributes.add(cli_name)
    return commands


def _generate_bool_commands(obj: dict[str, Any], prefix: str) -> list[str]:
    """Generate boolean flag commands for a neighbor or peer group."""
    commands = []
    for field, cli_name in _BOOL_FIELDS:
        if field in obj:
            commands.append(f"{prefix} {cli_name} {_shell_bool(obj[field])}")
    return commands


def _generate_link_bandwidth_commands(obj: dict[str, Any], prefix: str) -> list[str]:
    """Generate the link-bandwidth trio for a neighbor or peer group.

    advertise/receive enum values are passed through as found (name or
    integer): the dispatchers accept both spellings for a thrift enum.
    """
    commands = []
    if "link_bandwidth_bps" in obj:
        bw = obj["link_bandwidth_bps"]
        bw_str = format_bandwidth(bw) if isinstance(bw, int) else str(bw)
        commands.append(f"{prefix} link-bandwidth {escape_shell_arg(bw_str)}")
    if "advertise_link_bandwidth" in obj:
        commands.append(
            f"{prefix} advertise-lbw {escape_shell_arg(obj['advertise_link_bandwidth'])}"
        )
    if "receive_link_bandwidth" in obj:
        commands.append(
            f"{prefix} receive-lbw {escape_shell_arg(obj['receive_link_bandwidth'])}"
        )
    return commands


def _generate_add_path_commands(obj: dict[str, Any], prefix: str) -> list[str]:
    """Generate add-path commands from the AddPath enum (RECEIVE=1, SEND=2, BOTH=3)."""
    if "add_path" not in obj:
        return []
    raw = obj["add_path"]
    value = (
        {"RECEIVE": 1, "SEND": 2, "BOTH": 3}.get(raw, raw)
        if isinstance(raw, str)
        else raw
    )
    value = int(value)
    commands = []
    if value & 1:
        commands.append(f"{prefix} add-path receive true")
    if value & 2:
        commands.append(f"{prefix} add-path send true")
    return commands


def _generate_timer_commands(timers: dict[str, Any], prefix: str) -> list[str]:
    """Generate timer and graceful-restart commands for a neighbor or peer group."""
    return _generate_scalar_commands(timers, prefix, _TIMER_FIELDS)


def _generate_route_limit_commands(obj: dict[str, Any], prefix: str) -> list[str]:
    """Generate max-route commands from pre_filter/post_filter."""
    commands = []
    for struct, side in (("pre_filter", "pre"), ("post_filter", "post")):
        limit = obj.get(struct, {})
        for field, suffix, is_bool in _ROUTE_LIMIT_FIELDS:
            if field in limit:
                value = (
                    _shell_bool(limit[field])
                    if is_bool
                    else escape_shell_arg(limit[field])
                )
                commands.append(f"{prefix} max-route {side}-{suffix} {value}")
    return commands


def _generate_session_commands(obj: dict[str, Any], prefix: str) -> list[str]:
    """Generate the commands for every field BgpPeer and PeerGroup share."""
    commands = []
    if "is_passive" in obj:
        commands.append(f"{prefix} passive {_shell_bool(obj['is_passive'])}")
    commands.extend(_generate_bool_commands(obj, prefix))
    commands.extend(_generate_scalar_commands(obj, prefix, _INT_FIELDS))
    commands.extend(_generate_link_bandwidth_commands(obj, prefix))
    commands.extend(_generate_add_path_commands(obj, prefix))
    commands.extend(_generate_timer_commands(obj.get("bgp_peer_timers", {}), prefix))
    commands.extend(_generate_route_limit_commands(obj, prefix))
    return commands


def _unconverted_field_warnings(
    obj: dict[str, Any], handled: set[str], subject: str
) -> list[str]:
    """One `# WARNING` comment line per JSON field no generator converts.

    The dispatchers only expose the fields bgpd reads at that level, so a
    field left here is either dead in the daemon or per-peer-only; either
    way the operator must see it was not carried over.
    """
    warnings = []

    def warn(field: str) -> None:
        warnings.append(
            f"# WARNING: {subject}: field {_printable(field)} has no CLI "
            "equivalent, not converted"
        )

    for field in obj:
        if field not in handled:
            warn(field)
    for field in obj.get("bgp_peer_timers", {}):
        if field not in _TIMER_HANDLED_FIELDS:
            warn(f"bgp_peer_timers.{field}")
    for struct in ("pre_filter", "post_filter"):
        for field in obj.get(struct, {}):
            if field not in _ROUTE_LIMIT_HANDLED_FIELDS:
                warn(f"{struct}.{field}")
    return warnings


# (json field, CLI attribute) — BgpPeer-only fields whose value maps 1:1 onto
# a neighbor attribute token. The deprecated i32 ASNs are converted to the
# 4-byte attribute: bgpd reads either representation and the CLI stores the
# 4-byte field.
_NEIGHBOR_SCALAR_FIELDS = [
    ("remote_as_4_byte", "remote-asn"),
    ("remote_as", "remote-asn"),
    ("local_as_4_byte", "local-asn"),
    ("local_as", "local-asn"),
    ("peer_group_name", "peer-group"),
    ("description", "description"),
    ("peer_tag", "peer-tag"),
    ("local_addr", "bind-addr address"),
    ("ingress_policy_name", "ingress-policy"),
    ("egress_policy_name", "egress-policy"),
    ("next_hop4", "next-hop4"),
    ("next_hop6", "next-hop6"),
    ("peer_id", "peer-id"),
    ("type", "type"),
]

_NEIGHBOR_HANDLED_FIELDS = (
    {"peer_addr"}
    | {field for field, _ in _NEIGHBOR_SCALAR_FIELDS}
    | _SHARED_HANDLED_FIELDS
)


def generate_peer_commands(peer: dict[str, Any]) -> list[str]:
    """Generate `config protocol bgp neighbor` CLI commands for a peer."""
    peer_addr = peer.get("peer_addr", "")
    if not peer_addr:
        return []

    prefix = f"config protocol bgp neighbor {escape_shell_arg(peer_addr)}"
    commands = _unconverted_field_warnings(
        peer, _NEIGHBOR_HANDLED_FIELDS, f"neighbor {_printable(peer_addr)}"
    )
    commands.extend(_generate_scalar_commands(peer, prefix, _NEIGHBOR_SCALAR_FIELDS))
    commands.extend(_generate_session_commands(peer, prefix))
    return commands


# (json field, CLI attribute) — PeerGroup-only fields whose value maps 1:1
# onto a peer-group attribute token. Deprecated i32 ASNs map to the 4-byte
# attribute, as for neighbors. PeerGroup also carries local_addr, next_hop4,
# next_hop6, enabled and router_port_id, but bgpd reads none of them from a
# group (the first three per peer only, the last two nowhere), so the
# dispatcher has no attribute for them and they are flagged instead.
_PEER_GROUP_SCALAR_FIELDS = [
    ("remote_as_4_byte", "remote-asn"),
    ("remote_as", "remote-asn"),
    ("local_as_4_byte", "local-asn"),
    ("local_as", "local-asn"),
    ("description", "description"),
    ("peer_tag", "peer-tag"),
    ("ingress_policy_name", "ingress-policy"),
    ("egress_policy_name", "egress-policy"),
]

_PEER_GROUP_HANDLED_FIELDS = (
    {"name"}
    | {field for field, _ in _PEER_GROUP_SCALAR_FIELDS}
    | _SHARED_HANDLED_FIELDS
)


def generate_peer_group_commands(peer_group: dict[str, Any]) -> list[str]:
    """Generate `config protocol bgp peer-group` CLI commands for a peer group.

    The peer-group and neighbor dispatchers share one attribute grammar for
    the fields both thrift structs carry, so the per-shape generators are
    shared; only the scalar field list differs.
    """
    name = peer_group.get("name", "")
    if not name:
        return []

    prefix = f"config protocol bgp peer-group {escape_shell_arg(name)}"
    commands = _unconverted_field_warnings(
        peer_group, _PEER_GROUP_HANDLED_FIELDS, f"peer-group {_printable(name)}"
    )
    commands.extend(
        _generate_scalar_commands(peer_group, prefix, _PEER_GROUP_SCALAR_FIELDS)
    )
    commands.extend(_generate_session_commands(peer_group, prefix))
    # `name` is the only required PeerGroup field and bgpd accepts a group
    # carrying nothing else, so a bare create is needed to bring it into
    # existence; otherwise neighbors referring to it fail to load.
    if all(command.startswith("#") for command in commands):
        commands.append(prefix)
    return commands


# Emitted verbatim (never prefixed with the binary) for JSON the CLI cannot
# express, so a replayed script neither fails nor silently drops the field.
_WARNING_PREFIX = "# WARNING:"


def _warning(text: str) -> str:
    # Warning text interpolates JSON-sourced names and values; a control
    # character in one would end the `#` comment and become shell input.
    return f"{_WARNING_PREFIX} {_printable(text)}"


_BOOLEAN_OPERATOR_NAMES = {1: "AND", 2: "OR", 3: "NOT"}


def _boolean_operator_name(raw: Any) -> str:
    """routing_policy.BooleanOperator as its name, from the int or the name.

    Not a validator: an unrecognised value is returned as-is so the CLI
    rejects it on replay with its own message.
    """
    if isinstance(raw, str):
        return raw
    try:
        return _BOOLEAN_OPERATOR_NAMES.get(int(raw), str(raw))
    except (TypeError, ValueError):
        return str(raw)


def generate_as_path_list_commands(as_path_list: dict[str, Any]) -> list[str]:
    """Generate `config protocol bgp policy as-path-list` commands for one list.

    bgpd matches on `as_paths` (one `regex` line each) and `boolean_operator`
    (emitted only when it differs from the OR default). `as_path_list_names`
    and the `as_path_list` entries are never read by bgpd and have no CLI
    spelling here, so they surface as warnings rather than vanishing.
    """
    name = as_path_list.get("name", "")
    if not name:
        return []

    prefix = f"config protocol bgp policy as-path-list {escape_shell_arg(name)}"
    commands = []
    if as_path_list.get("description"):
        commands.append(
            f"{prefix} description {escape_shell_arg(as_path_list['description'])}"
        )
    for regex in as_path_list.get("as_paths") or []:
        commands.append(f"{prefix} regex {escape_shell_arg(regex)}")
    if "boolean_operator" in as_path_list:
        operator = _boolean_operator_name(as_path_list["boolean_operator"])
        if operator == "NOT":
            commands.append(
                _warning(
                    f"as-path-list {name}: boolean_operator NOT is not "
                    "accepted by the CLI (bgpd treats it as AND); not emitted"
                )
            )
        elif operator != "OR":
            commands.append(f"{prefix} boolean-operator {escape_shell_arg(operator)}")
    if as_path_list.get("as_path_list_names"):
        commands.append(
            _warning(
                f"as-path-list {name}: as_path_list_names is not read by bgpd "
                "and has no CLI equivalent; not emitted"
            )
        )
    if as_path_list.get("as_path_list"):
        commands.append(
            _warning(
                f"as-path-list {name}: as_path_list entries are not read by "
                "bgpd (bgpd matches as_paths); not emitted"
            )
        )
    if not commands:
        # Nothing to set: still recreate the (empty) list by name.
        commands.append(prefix)
    return commands


def generate_community_list_commands(community_list: dict[str, Any]) -> list[str]:
    """Generate `config protocol bgp policy community-list` commands for one list.

    bgpd matches on `communities` (one `community` line each), `boolean_operator`
    (emitted only when it differs from the OR default) and `exact_match`.
    `community_list_names` and the `members` entries are never read by bgpd and
    have no CLI spelling here, so they surface as warnings rather than vanishing.
    """
    name = community_list.get("name", "")
    if not name:
        return []

    prefix = f"config protocol bgp policy community-list {escape_shell_arg(name)}"
    commands = []
    if community_list.get("description"):
        commands.append(
            f"{prefix} description {escape_shell_arg(community_list['description'])}"
        )
    for community in community_list.get("communities") or []:
        commands.append(f"{prefix} community {escape_shell_arg(community)}")
    if "boolean_operator" in community_list:
        operator = _boolean_operator_name(community_list["boolean_operator"])
        if operator == "NOT":
            commands.append(
                _warning(
                    f"community-list {name}: boolean_operator NOT is not "
                    "accepted by the CLI (bgpd treats it as AND); not emitted"
                )
            )
        elif operator != "OR":
            commands.append(f"{prefix} boolean-operator {escape_shell_arg(operator)}")
    if "exact_match" in community_list:
        commands.append(
            f"{prefix} exact-match {_shell_bool(community_list['exact_match'])}"
        )
    if community_list.get("community_list_names"):
        commands.append(
            _warning(
                f"community-list {name}: community_list_names is not read by "
                "bgpd and has no CLI equivalent; not emitted"
            )
        )
    if community_list.get("members"):
        commands.append(
            _warning(
                f"community-list {name}: members entries are not read by bgpd "
                "(bgpd matches communities); not emitted"
            )
        )
    if not commands:
        # Nothing to set: still recreate the (empty) community-list by name.
        commands.append(prefix)
    return commands


_COMPARISON_OPERATOR_NAMES = {
    1: "EQ",
    2: "GE",
    3: "LE",
    4: "NE",
    5: "GT",
    6: "LT",
    7: "RG",
}
_IP_VERSION_KEYWORDS = {4: "v4", 6: "v6"}


def _comparison_operator_name(raw: Any) -> str:
    """routing_policy.ComparisonOperator as its name; see _boolean_operator_name."""
    if isinstance(raw, str):
        return raw
    try:
        return _COMPARISON_OPERATOR_NAMES.get(int(raw), str(raw))
    except (TypeError, ValueError):
        return str(raw)


def _prefix_list_warnings(name: str, prefix_list: dict[str, Any]) -> list[str]:
    """Warnings for PrefixList fields that have no CLI spelling."""
    warnings = []
    if prefix_list.get("prefix_list_names"):
        warnings.append(
            _warning(
                f"prefix-list {name}: prefix_list_names has no CLI equivalent; "
                "not emitted"
            )
        )
    if "ip_version" in prefix_list:
        warnings.append(
            _warning(
                f"prefix-list {name}: ip_version has no CLI equivalent (the CLI "
                "writes `version`); not emitted"
            )
        )
    return warnings


def generate_prefix_list_commands(prefix_list: dict[str, Any]) -> list[str]:
    """Generate `config protocol bgp policy prefix-list` commands for one list.

    `ip-version` is the CLI spelling of the `version` field (4/6). bgpd
    accepts only `boolean_operator` OR and no list-level `compare_operator`
    (PrefixTreeMatch::validateAndCreatePrefixTree), and the CLI offers nothing
    else, so other values surface as warnings rather than commands. The
    `prefix_list_names` references, the `ip_version` enum field and (until the
    entry subcommand lands) `prefixes` have no CLI spelling and also warn.
    """
    name = prefix_list.get("name", "")
    if not name:
        return []

    prefix = f"config protocol bgp policy prefix-list {escape_shell_arg(name)}"
    commands = []
    if prefix_list.get("description"):
        commands.append(
            f"{prefix} description {escape_shell_arg(prefix_list['description'])}"
        )
    if "boolean_operator" in prefix_list:
        operator = _boolean_operator_name(prefix_list["boolean_operator"])
        if operator != "OR":
            commands.append(
                _warning(
                    f"prefix-list {name}: boolean_operator {operator} is not "
                    "accepted by bgpd (only OR); not emitted"
                )
            )
    if "compare_operator" in prefix_list:
        operator = _comparison_operator_name(prefix_list["compare_operator"])
        commands.append(
            _warning(
                f"prefix-list {name}: list-level compare_operator {operator} is "
                "not accepted by bgpd; not emitted"
            )
        )
    if "version" in prefix_list:
        try:
            keyword = _IP_VERSION_KEYWORDS.get(prefix_list["version"])
        except TypeError:
            keyword = None
        if keyword is None:
            commands.append(
                _warning(
                    f"prefix-list {name}: version {prefix_list['version']} is "
                    "neither 4 nor 6; not emitted"
                )
            )
        else:
            commands.append(f"{prefix} ip-version {keyword}")
    commands.extend(_prefix_list_warnings(name, prefix_list))
    for entry in prefix_list.get("prefixes") or []:
        commands.extend(generate_prefix_list_entry_commands(name, entry))
    if all(command.startswith("#") for command in commands):
        # Warnings do not recreate the list; preserve it with a bare command.
        commands.append(prefix)
    return commands


_MATCH_LOGIC_NAMES = {0: "EQUAL", 1: "NOT_EQUAL"}


def _prefix_list_entry_scalar_commands(
    prefix: str, label: str, entry: dict[str, Any]
) -> list[str]:
    """The single-valued entry attributes, in the CLI's attribute order.

    bgpd accepts only match_logic EQUAL (PrefixTreeMatch), so any other value
    is a warning, not a command.
    """
    commands = []
    if entry.get("description"):
        commands.append(
            f"{prefix} description {escape_shell_arg(entry['description'])}"
        )
    if "match_logic" in entry:
        raw = entry["match_logic"]
        logic = (
            raw if isinstance(raw, str) else _MATCH_LOGIC_NAMES.get(int(raw), str(raw))
        )
        if logic != "EQUAL":
            commands.append(
                _warning(
                    f"{label}: match_logic {logic} is not accepted by bgpd "
                    "(only EQUAL); not emitted"
                )
            )
    if "max_allowed_golden_prefix_subnet_count" in entry:
        commands.append(
            f"{prefix} max-allowed-subnet-count "
            f"{escape_shell_arg(entry['max_allowed_golden_prefix_subnet_count'])}"
        )
    return commands


def _prefix_list_entry_range_commands(
    prefix: str, label: str, entry: dict[str, Any]
) -> list[str]:
    """`prefix-len-range` lines for the single range the CLI can express."""
    ranges = entry.get("prefix_len_ranges") or []
    if not ranges:
        return []
    first = ranges[0]
    commands = []
    if "compare_operator" in first:
        operator = _comparison_operator_name(first["compare_operator"])
        if operator == "RG":
            # bgpd's toPolicyComparisonOperator() throws on RG, and a range
            # replayed without its operator would store an unset enum that
            # bgpd rejects just the same, so the whole range is dropped.
            commands.append(
                _warning(
                    f"{label}: prefix_len_ranges compare_operator RG is not "
                    "accepted by bgpd; the range is not emitted"
                )
            )
            if len(ranges) > 1:
                commands.append(
                    _warning(
                        f"{label}: only the first of {len(ranges)} "
                        "prefix_len_ranges is expressible; the rest are not "
                        "emitted"
                    )
                )
            return commands
        commands.append(
            f"{prefix} prefix-len-range compare-operator {escape_shell_arg(operator)}"
        )
    if "value" in first:
        commands.append(
            f"{prefix} prefix-len-range value {escape_shell_arg(first['value'])}"
        )
    if len(ranges) > 1:
        commands.append(
            _warning(
                f"{label}: only the first of {len(ranges)} prefix_len_ranges is "
                "expressible; the rest are not emitted"
            )
        )
    return commands


def generate_prefix_list_entry_commands(
    list_name: str, entry: dict[str, Any]
) -> list[str]:
    """Generate `... prefix-list <name> prefix <prefix/len>` commands for one entry.

    The CLI keys entries by base_prefix, the identity bgpd keeps (it merges
    entries by prefix and rejects seq_num), and supports a single
    prefix_len_range; anything beyond that surfaces as a warning.
    """
    if not entry.get("base_prefix"):
        return [
            _warning(
                f"prefix-list {list_name}: an entry has no base_prefix and cannot "
                "be addressed by the CLI; not emitted"
            )
        ]
    base_prefix = entry["base_prefix"]
    label = f"prefix-list {list_name} prefix {base_prefix}"
    prefix = (
        f"config protocol bgp policy prefix-list {escape_shell_arg(list_name)} "
        f"prefix {escape_shell_arg(base_prefix)}"
    )
    commands = []
    if "seq_num" in entry:
        commands.append(
            _warning(
                f"{label}: seq_num {entry['seq_num']} is rejected by bgpd and "
                "not part of the CLI's entry identity; dropped"
            )
        )
    commands.extend(_prefix_list_entry_scalar_commands(prefix, label, entry))
    commands.extend(_prefix_list_entry_range_commands(prefix, label, entry))
    if entry.get("regex"):
        commands.append(f"{prefix} regex {escape_shell_arg(entry['regex'])}")
    for community in sorted(entry.get("communities") or []):
        commands.append(f"{prefix} communities {escape_shell_arg(community)}")
    if "ip_version" in entry:
        commands.append(
            _warning(f"{label}: ip_version has no CLI equivalent; not emitted")
        )
    if all(c.startswith(_WARNING_PREFIX) for c in commands):
        # Nothing expressible to set: still create the entry by its prefix.
        commands.append(prefix)
    return commands


_FLOW_CONTROL_ACTION_NAMES = {
    1: "ACCEPT",
    2: "DENY",
    3: "NEXT_TERM",
    4: "NEXT_POLICY",
    5: "LOG_AND_NEXT_TERM",
    6: "LOG_AND_ACCEPT",
    7: "LOG_AND_DENY",
}


def _flow_control_action_name(raw: object) -> str:
    if isinstance(raw, str):
        return raw
    if isinstance(raw, int) and not isinstance(raw, bool):
        return _FLOW_CONTROL_ACTION_NAMES.get(raw, str(raw))
    return str(raw)


def generate_routing_policy_commands(policy: dict[str, Any]) -> list[str]:
    """Generate `config protocol bgp policy routing-policy` commands for one
    policy statement. The policy-level `result` has no CLI spelling (only a
    term's action result does) and surfaces as a warning when not the DENY
    default."""
    name = policy.get("name", "")
    if not name:
        return []

    prefix = f"config protocol bgp policy routing-policy {escape_shell_arg(name)}"
    commands = []
    if policy.get("description"):
        commands.append(
            f"{prefix} description {escape_shell_arg(policy['description'])}"
        )
    if "result" in policy and _flow_control_action_name(policy["result"]) != "DENY":
        commands.append(
            _warning(
                f"routing-policy {name}: policy-level result "
                f"{_flow_control_action_name(policy['result'])} has no CLI "
                "equivalent; not emitted"
            )
        )
    for term in policy.get("policy_entries") or []:
        commands.extend(generate_routing_policy_term_commands(name, term))
    if not commands:
        # Nothing to set: still recreate the (empty) routing-policy by name.
        commands.append(prefix)
    return commands


def generate_routing_policy_term_commands(
    policy_name: str, term: dict[str, Any]
) -> list[str]:
    """Generate `... routing-policy <name> term <seq-num>` commands for one
    term. The CLI keys terms by sequence_number."""
    if "sequence_number" not in term:
        return [
            _warning(
                f"routing-policy {policy_name}: term '{term.get('name', '')}' has "
                "no sequence_number and cannot be addressed by the CLI; not emitted"
            )
        ]
    prefix = (
        f"config protocol bgp policy routing-policy {escape_shell_arg(policy_name)} "
        f"term {escape_shell_arg(term['sequence_number'])}"
    )
    commands = []
    if term.get("description"):
        commands.append(f"{prefix} description {escape_shell_arg(term['description'])}")
    # Nested term generators (action, match) hook in here.
    if not commands:
        commands.append(prefix)
    return commands


def generate_policy_commands(config: dict[str, Any]) -> list[str]:
    """Generate the `config protocol bgp policy ...` commands.

    Lists come before the routing-policies that reference them, and the whole
    block precedes the peer-group/peer commands that name a policy, so a
    replayed script never stages a dangling reference.
    """
    policies = config.get("policies", {})
    commands = []
    for as_path_list in policies.get("aspath_lists", []):
        commands.extend(generate_as_path_list_commands(as_path_list))
    for community_list in policies.get("community_lists", []):
        commands.extend(generate_community_list_commands(community_list))
    for prefix_list in policies.get("prefix_lists", []):
        commands.extend(generate_prefix_list_commands(prefix_list))
    # Routing-policies last: they reference the lists above by name.
    for policy in policies.get("bgp_policy_statements", []):
        commands.extend(generate_routing_policy_commands(policy))
    return commands


def generate_exec_commands(commands: list[str], binary: str = "fboss2") -> list[str]:
    """Generate executable commands with custom binary."""
    exec_commands = [
        "#!/bin/bash",
        "# BGP configuration script generated by j2c",
        "# This script runs in normal mode (writes to ~/.fboss2/bgp_config.json)",
        "",
        "set -e  # Exit on first error",
        "",
    ]
    for cmd in commands:
        exec_commands.append(cmd if cmd.startswith("#") else f"{binary} {cmd}")

    exec_commands.extend(
        [
            "",
            "echo ''",
            "echo 'BGP configuration complete!'",
            "echo 'Config saved to: ~/.fboss2/bgp_config.json'",
        ]
    )
    return exec_commands


def generate_commands(config: dict[str, Any]) -> list[str]:
    """Convert BGP++ JSON config to raw CLI commands (plus `#` warning lines
    for fields that have no CLI equivalent)."""
    commands = []
    commands.extend(generate_global_commands(config))
    # Policy objects before the peer-group/peer lines that reference them.
    commands.extend(generate_policy_commands(config))
    for peer_group in config.get("peer_groups", []):
        commands.extend(generate_peer_group_commands(peer_group))
    for peer in config.get("peers", []):
        commands.extend(generate_peer_commands(peer))
    return commands


def json_to_cli(config: dict[str, Any], binary: str = "fboss2") -> list[str]:
    """Convert BGP++ JSON config to an executable CLI script."""
    return generate_exec_commands(generate_commands(config), binary)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert BGP++ JSON configuration to fboss2 CLI commands"
    )
    parser.add_argument("input", help="Input JSON file")
    parser.add_argument(
        "--output", "-o", help="Output file (default: stdout)", default=None
    )
    parser.add_argument(
        "--binary",
        "-b",
        default="fboss2",
        help="Binary name or path (default: fboss2). Use full path for buck run output.",
    )
    parser.add_argument(
        "--raw",
        action="store_true",
        help="Output raw CLI commands only (no shell script wrapper)",
    )
    args = parser.parse_args()

    try:
        with open(args.input) as f:
            config = json.load(f)
    except FileNotFoundError:
        print(f"Error: Input file '{args.input}' not found", file=sys.stderr)
        return 1
    except json.JSONDecodeError as e:
        print(f"Error: Invalid JSON in '{args.input}': {e}", file=sys.stderr)
        return 1

    commands = generate_commands(config)
    # Fields with no CLI equivalent are flagged in the script AND on stderr,
    # so a scripted conversion cannot lose them silently.
    for line in commands:
        if line.startswith("# WARNING"):
            print(line[2:], file=sys.stderr)
    if not args.raw:
        commands = generate_exec_commands(commands, args.binary)
    output = "\n".join(commands)

    if args.output:
        with open(args.output, "w") as f:
            f.write(output + "\n")
        print(f"Generated {len(commands)} commands to {args.output}", file=sys.stderr)
    else:
        print(output)

    return 0


if __name__ == "__main__":
    sys.exit(main())
