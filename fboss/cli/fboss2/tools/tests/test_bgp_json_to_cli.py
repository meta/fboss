#!/usr/bin/env python3
# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

# pyre-unsafe

"""
Tests for BGP++ JSON to fboss2 CLI converter (j2c tool).

Tests cover:
- escape_shell_arg function for shell escaping
- format_bandwidth function for bandwidth formatting
- generate_global_commands for global BGP settings
- generate_peer_group_commands for peer group configuration
- generate_peer_commands for peer configuration
- json_to_cli full integration
"""

import unittest
from typing import Any

from fboss.cli.fboss2.tools.bgp_json_to_cli import (
    escape_shell_arg,
    format_bandwidth,
    generate_as_path_list_commands,
    generate_commands,
    generate_community_list_commands,
    generate_exec_commands,
    generate_global_commands,
    generate_peer_commands,
    generate_peer_group_commands,
    generate_policy_commands,
    generate_prefix_list_commands,
    generate_prefix_list_entry_commands,
    generate_routing_policy_commands,
    generate_routing_policy_term_action_commands,
    generate_routing_policy_term_commands,
    json_to_cli,
)


class EscapeShellArgTest(unittest.TestCase):
    """Tests for escape_shell_arg function."""

    def test_simple_string_not_escaped(self) -> None:
        """Simple strings without special chars should not be quoted."""
        result = escape_shell_arg("simple")
        self.assertEqual(result, "simple")

    def test_string_with_space_is_quoted(self) -> None:
        """Strings with spaces should be wrapped in single quotes."""
        result = escape_shell_arg("hello world")
        self.assertEqual(result, "'hello world'")

    def test_string_with_special_chars_is_quoted(self) -> None:
        """Strings with shell special characters should be quoted."""
        result = escape_shell_arg("value$var")
        self.assertEqual(result, "'value$var'")

    def test_string_with_backslash_is_quoted(self) -> None:
        """Strings with backslash should be quoted."""
        result = escape_shell_arg("path\\to\\file")
        self.assertEqual(result, "'path\\to\\file'")

    def test_empty_string(self) -> None:
        """Empty string is quoted as empty single quotes."""
        result = escape_shell_arg("")
        self.assertEqual(result, "''")

    def test_ipv6_address_not_escaped(self) -> None:
        """IPv6 addresses should not be quoted."""
        result = escape_shell_arg("2401:db00:501c::/64")
        self.assertEqual(result, "2401:db00:501c::/64")

    def test_command_injection_payload_neutralized(self) -> None:
        """Command injection payloads should be rendered literal."""
        result = escape_shell_arg("$(reboot)")
        self.assertEqual(result, "'$(reboot)'")

    def test_backtick_injection_neutralized(self) -> None:
        """Backtick command substitution should be rendered literal."""
        result = escape_shell_arg("`reboot`")
        self.assertEqual(result, "'`reboot`'")

    def test_command_separator_neutralized(self) -> None:
        """Separators like ; & | must not split the generated command."""
        self.assertEqual(escape_shell_arg("x;id"), "'x;id'")
        self.assertEqual(escape_shell_arg("a&&b|c"), "'a&&b|c'")


class FormatBandwidthTest(unittest.TestCase):
    """Tests for format_bandwidth function."""

    def test_terabits_stay_in_gigabits(self) -> None:
        """bgpd's parser knows only K/M/G, so 1 Tbps must be emitted as 1000G."""
        result = format_bandwidth(1_000_000_000_000)
        self.assertEqual(result, "1000G")

    def test_gigabits(self) -> None:
        """Bandwidth in gigabits should format as 'G'."""
        result = format_bandwidth(10_000_000_000)
        self.assertEqual(result, "10G")

    def test_megabits(self) -> None:
        """Bandwidth in megabits should format as 'M'."""
        result = format_bandwidth(100_000_000)
        self.assertEqual(result, "100M")

    def test_kilobits(self) -> None:
        """Bandwidth in kilobits should format as 'K'."""
        result = format_bandwidth(1_000)
        self.assertEqual(result, "1K")

    def test_raw_bits(self) -> None:
        """Non-standard values should return as raw string."""
        result = format_bandwidth(12345)
        self.assertEqual(result, "12345")

    def test_100g(self) -> None:
        """100G should format correctly."""
        result = format_bandwidth(100_000_000_000)
        self.assertEqual(result, "100G")


class GenerateGlobalCommandsTest(unittest.TestCase):
    """Tests for generate_global_commands function."""

    def test_router_id(self) -> None:
        """Router ID should generate correct command."""
        config = {"router_id": "10.0.0.1"}
        commands = generate_global_commands(config)
        self.assertEqual(commands, ["config protocol bgp global router-id 10.0.0.1"])

    def test_local_as(self) -> None:
        """Local AS should generate correct command."""
        config = {"local_as_4_byte": 65000}
        commands = generate_global_commands(config)
        self.assertEqual(commands, ["config protocol bgp global local-asn 65000"])

    def test_hold_time(self) -> None:
        """Hold time should generate correct command."""
        config = {"hold_time": 90}
        commands = generate_global_commands(config)
        self.assertEqual(commands, ["config protocol bgp global hold-time 90"])

    def test_confed_asn(self) -> None:
        """Confederation AS should generate correct command."""
        config = {"local_confed_as_4_byte": 65001}
        commands = generate_global_commands(config)
        self.assertEqual(commands, ["config protocol bgp global confed-asn 65001"])

    def test_networks6_with_all_fields(self) -> None:
        """networks6 should generate command with all fields."""
        config = {
            "networks6": [
                {
                    "prefix": "2001:db8::/32",
                    "policy_name": "TEST_POLICY",
                    "install_to_fib": False,
                    "minimum_supporting_routes": 2,
                }
            ]
        }
        commands = generate_global_commands(config)
        expected = [
            "config protocol bgp global network6 add 2001:db8::/32 policy TEST_POLICY install-to-fib false min-routes 2"
        ]
        self.assertEqual(commands, expected)

    def test_networks6_with_install_to_fib_true(self) -> None:
        """networks6 with install_to_fib=true should preserve value."""
        config = {"networks6": [{"prefix": "2001:db8::/32", "install_to_fib": True}]}
        commands = generate_global_commands(config)
        self.assertIn("install-to-fib true", commands[0])

    def test_networks6_empty_prefix_skipped(self) -> None:
        """networks6 with empty prefix should be skipped."""
        config = {"networks6": [{"prefix": "", "policy_name": "ORPHAN"}]}
        commands = generate_global_commands(config)
        self.assertEqual(commands, [])

    def test_switch_limit_prefix_limit(self) -> None:
        """switch_limit_config.prefix_limit should generate correct command."""
        config = {"switch_limit_config": {"prefix_limit": 10000}}
        commands = generate_global_commands(config)
        self.assertEqual(commands, ["config protocol bgp global switch-limit 10000"])

    def test_switch_limit_total_path_limit(self) -> None:
        """switch_limit_config.total_path_limit should generate correct command."""
        config = {"switch_limit_config": {"total_path_limit": 250000}}
        commands = generate_global_commands(config)
        self.assertEqual(
            commands, ["config protocol bgp global switch-limit-total-path 250000"]
        )

    def test_switch_limit_max_golden_vips(self) -> None:
        """switch_limit_config.max_golden_vips should generate correct command."""
        config = {"switch_limit_config": {"max_golden_vips": 2860}}
        commands = generate_global_commands(config)
        self.assertEqual(
            commands, ["config protocol bgp global switch-limit-max-golden-vips 2860"]
        )

    def test_switch_limit_overload_protection_mode(self) -> None:
        """switch_limit_config.overload_protection_mode should generate correct command."""
        config = {"switch_limit_config": {"overload_protection_mode": 2}}
        commands = generate_global_commands(config)
        self.assertEqual(
            commands,
            ["config protocol bgp global switch-limit-overload-protection-mode 2"],
        )

    def test_all_switch_limit_fields(self) -> None:
        """All switch_limit_config fields should generate correct commands."""
        config = {
            "switch_limit_config": {
                "prefix_limit": 10000,
                "total_path_limit": 250000,
                "max_golden_vips": 2860,
                "overload_protection_mode": 2,
            }
        }
        commands = generate_global_commands(config)
        self.assertEqual(len(commands), 4)
        self.assertIn("config protocol bgp global switch-limit 10000", commands)
        self.assertIn(
            "config protocol bgp global switch-limit-total-path 250000", commands
        )
        self.assertIn(
            "config protocol bgp global switch-limit-max-golden-vips 2860", commands
        )
        self.assertIn(
            "config protocol bgp global switch-limit-overload-protection-mode 2",
            commands,
        )

    def test_empty_config(self) -> None:
        """Empty config should return empty list."""
        config = {}
        commands = generate_global_commands(config)
        self.assertEqual(commands, [])


class GeneratePeerGroupCommandsTest(unittest.TestCase):
    """Tests for generate_peer_group_commands (peer-group grammar)."""

    PREFIX = "config protocol bgp peer-group TEST"

    def test_empty_name_returns_empty(self) -> None:
        """Peer group without name should return empty list."""
        peer_group = {"remote_as_4_byte": 65000}
        commands = generate_peer_group_commands(peer_group)
        self.assertEqual(commands, [])

    def test_name_only_emits_bare_create(self) -> None:
        """A group with only its required name must still be created, or
        neighbors referring to it would name a group that never existed."""
        commands = generate_peer_group_commands({"name": "TEST"})
        self.assertEqual(commands, [self.PREFIX])

    def test_only_unrepresentable_fields_emits_bare_create(self) -> None:
        """Warning comments are not executable commands, so a group whose
        fields all lack a CLI equivalent still needs the bare create."""
        commands = generate_peer_group_commands({"name": "TEST", "enabled": True})
        self.assertEqual(
            commands,
            [
                "# WARNING: peer-group TEST: field enabled has no CLI equivalent, "
                "not converted",
                self.PREFIX,
            ],
        )

    def test_attribute_present_emits_no_bare_create(self) -> None:
        """Setting an attribute creates the group implicitly."""
        commands = generate_peer_group_commands(
            {"name": "TEST", "remote_as_4_byte": 65000}
        )
        self.assertEqual(commands, [f"{self.PREFIX} remote-asn 65000"])

    def test_remote_asn(self) -> None:
        """remote_as_4_byte should generate correct command."""
        peer_group = {"name": "TEST-GROUP", "remote_as_4_byte": 65000}
        commands = generate_peer_group_commands(peer_group)
        self.assertIn(
            "config protocol bgp peer-group TEST-GROUP remote-asn 65000", commands
        )

    def test_local_asn(self) -> None:
        """local_as_4_byte should generate local-asn command."""
        peer_group = {"name": "TEST", "local_as_4_byte": 64512}
        commands = generate_peer_group_commands(peer_group)
        self.assertIn(f"{self.PREFIX} local-asn 64512", commands)

    def test_description_with_spaces(self) -> None:
        """Description with spaces should be quoted."""
        peer_group = {"name": "TEST", "description": "Test peer group"}
        commands = generate_peer_group_commands(peer_group)
        self.assertIn(f"{self.PREFIX} description 'Test peer group'", commands)

    def test_boolean_flag_true(self) -> None:
        """Boolean true should generate 'true'."""
        peer_group = {"name": "TEST", "next_hop_self": True}
        commands = generate_peer_group_commands(peer_group)
        self.assertIn(f"{self.PREFIX} next-hop-self true", commands)

    def test_boolean_flag_false(self) -> None:
        """Boolean false should generate 'false' (not be omitted)."""
        peer_group = {"name": "TEST", "disable_ipv4_afi": False}
        commands = generate_peer_group_commands(peer_group)
        self.assertIn(f"{self.PREFIX} afi disable-ipv4-afi false", commands)

    def test_afi_flags(self) -> None:
        """AFI flags use the two-token `afi ...` attributes."""
        peer_group = {
            "name": "TEST",
            "disable_ipv4_afi": True,
            "disable_ipv6_afi": False,
            "v4_over_v6_nexthop": True,
        }
        commands = generate_peer_group_commands(peer_group)
        self.assertIn(f"{self.PREFIX} afi disable-ipv4-afi true", commands)
        self.assertIn(f"{self.PREFIX} afi disable-ipv6-afi false", commands)
        self.assertIn(f"{self.PREFIX} afi ipv4-over-ipv6-nh true", commands)

    def test_session_flags(self) -> None:
        """Session flags map onto their single-token boolean attributes."""
        peer_group = {
            "name": "TEST",
            "is_rr_client": True,
            "is_confed_peer": True,
            "is_redistribute_peer": False,
            "enhanced_route_refresh": True,
            "enable_stateful_ha": True,
        }
        commands = generate_peer_group_commands(peer_group)
        self.assertIn(f"{self.PREFIX} rr-client true", commands)
        self.assertIn(f"{self.PREFIX} confed-peer true", commands)
        self.assertIn(f"{self.PREFIX} redistribute-peer false", commands)
        self.assertIn(f"{self.PREFIX} enhanced-route-refresh true", commands)
        self.assertIn(f"{self.PREFIX} graceful-restart stateful-ha true", commands)

    def test_is_passive(self) -> None:
        """is_passive should generate the passive flag."""
        peer_group = {"name": "TEST", "is_passive": True}
        commands = generate_peer_group_commands(peer_group)
        self.assertIn(f"{self.PREFIX} passive true", commands)
        peer_group["is_passive"] = False
        commands = generate_peer_group_commands(peer_group)
        self.assertIn(f"{self.PREFIX} passive false", commands)

    def test_policies(self) -> None:
        """Policy names should generate correct commands."""
        peer_group = {
            "name": "TEST",
            "ingress_policy_name": "IN_POLICY",
            "egress_policy_name": "OUT_POLICY",
        }
        commands = generate_peer_group_commands(peer_group)
        self.assertIn(f"{self.PREFIX} ingress-policy IN_POLICY", commands)
        self.assertIn(f"{self.PREFIX} egress-policy OUT_POLICY", commands)

    def test_add_path(self) -> None:
        """add_path BOTH should expand to both directions; RECEIVE to one."""
        peer_group: dict[str, Any] = {"name": "TEST", "add_path": 3}
        commands = generate_peer_group_commands(peer_group)
        self.assertIn(f"{self.PREFIX} add-path receive true", commands)
        self.assertIn(f"{self.PREFIX} add-path send true", commands)
        peer_group["add_path"] = "RECEIVE"
        commands = generate_peer_group_commands(peer_group)
        self.assertIn(f"{self.PREFIX} add-path receive true", commands)
        self.assertNotIn(f"{self.PREFIX} add-path send true", commands)

    def test_timers(self) -> None:
        """Timers, including zero values, should generate correct commands."""
        peer_group = {
            "name": "TEST",
            "bgp_peer_timers": {
                "hold_time_seconds": 30,
                "keep_alive_seconds": 10,
                "out_delay_seconds": 5,
                "withdraw_unprog_delay_seconds": 0,
                "graceful_restart_seconds": 120,
            },
        }
        commands = generate_peer_group_commands(peer_group)
        self.assertIn(f"{self.PREFIX} timers hold-time 30", commands)
        self.assertIn(f"{self.PREFIX} timers keepalive 10", commands)
        self.assertIn(f"{self.PREFIX} timers out-delay 5", commands)
        self.assertIn(f"{self.PREFIX} timers withdraw-unprog-delay 0", commands)
        self.assertIn(f"{self.PREFIX} graceful-restart restart-time 120", commands)

    def test_hold_time_zero_preserved(self) -> None:
        """hold_time_seconds: 0 is a real value (no keepalives), not 'unset'."""
        peer_group = {"name": "TEST", "bgp_peer_timers": {"hold_time_seconds": 0}}
        commands = generate_peer_group_commands(peer_group)
        self.assertIn(f"{self.PREFIX} timers hold-time 0", commands)

    def test_pre_filter(self) -> None:
        """pre_filter maps onto the max-route pre-* attributes."""
        peer_group = {
            "name": "TEST",
            "pre_filter": {
                "max_routes": 45000,
                "warning_limit": 100,
                "warning_only": True,
            },
        }
        commands = generate_peer_group_commands(peer_group)
        self.assertIn(f"{self.PREFIX} max-route pre-filter 45000", commands)
        self.assertIn(f"{self.PREFIX} max-route pre-warning-threshold 100", commands)
        self.assertIn(f"{self.PREFIX} max-route pre-warning-only true", commands)

    def test_post_filter(self) -> None:
        """post_filter maps onto the max-route post-* attributes."""
        peer_group = {
            "name": "TEST",
            "post_filter": {
                "max_routes": 50000,
                "warning_limit": 48000,
                "warning_only": False,
            },
        }
        commands = generate_peer_group_commands(peer_group)
        self.assertIn(f"{self.PREFIX} max-route post-filter 50000", commands)
        self.assertIn(f"{self.PREFIX} max-route post-warning-threshold 48000", commands)
        self.assertIn(f"{self.PREFIX} max-route post-warning-only false", commands)

    def test_peer_tag(self) -> None:
        """peer_tag should generate peer-tag command."""
        peer_group = {"name": "TEST", "peer_tag": "FSW"}
        commands = generate_peer_group_commands(peer_group)
        self.assertIn(f"{self.PREFIX} peer-tag FSW", commands)

    def test_complete_peer_group_loses_no_field(self) -> None:
        """Every CLI-settable PeerGroup field produces exactly one command, in
        the grammar the peer-group dispatcher accepts."""
        peer_group: dict[str, Any] = {
            "name": "TEST",
            "remote_as_4_byte": 65000,
            "local_as_4_byte": 64512,
            "description": "spine group",
            "peer_tag": "FSW",
            "ingress_policy_name": "IN_POLICY",
            "egress_policy_name": "OUT_POLICY",
            "is_passive": True,
            "is_rr_client": True,
            "is_confed_peer": False,
            "is_redistribute_peer": True,
            "enhanced_route_refresh": True,
            "route_refresh": False,
            "remove_private_as": True,
            "enforce_first_as": True,
            "disable_ipv4_afi": True,
            "disable_ipv6_afi": False,
            "v4_over_v6_nexthop": True,
            "enable_stateful_ha": True,
            "next_hop_self": True,
            "ttl_security_hops": 2,
            "link_bandwidth_bps": "10G",
            "advertise_link_bandwidth": 1,
            "receive_link_bandwidth": "ACCEPT",
            "add_path": 3,
            "bgp_peer_timers": {
                "hold_time_seconds": 90,
                "keep_alive_seconds": 30,
                "out_delay_seconds": 5,
                "withdraw_unprog_delay_seconds": 10,
                "graceful_restart_seconds": 120,
            },
            "pre_filter": {
                "max_routes": 45000,
                "warning_limit": 40000,
                "warning_only": True,
            },
            "post_filter": {
                "max_routes": 50000,
                "warning_limit": 48000,
                "warning_only": False,
            },
        }
        expected = [
            "remote-asn 65000",
            "local-asn 64512",
            "description 'spine group'",
            "peer-tag FSW",
            "ingress-policy IN_POLICY",
            "egress-policy OUT_POLICY",
            "passive true",
            "rr-client true",
            "confed-peer false",
            "redistribute-peer true",
            "enhanced-route-refresh true",
            "route-refresh false",
            "remove-private-as true",
            "enforce-first-as true",
            "afi disable-ipv4-afi true",
            "afi disable-ipv6-afi false",
            "afi ipv4-over-ipv6-nh true",
            "graceful-restart stateful-ha true",
            "next-hop-self true",
            "ttl-security-hops 2",
            "link-bandwidth 10G",
            "advertise-lbw 1",
            "receive-lbw ACCEPT",
            "add-path receive true",
            "add-path send true",
            "timers hold-time 90",
            "timers keepalive 30",
            "timers out-delay 5",
            "timers withdraw-unprog-delay 10",
            "graceful-restart restart-time 120",
            "max-route pre-filter 45000",
            "max-route pre-warning-threshold 40000",
            "max-route pre-warning-only true",
            "max-route post-filter 50000",
            "max-route post-warning-threshold 48000",
            "max-route post-warning-only false",
        ]
        commands = generate_peer_group_commands(peer_group)
        self.assertEqual(commands, [f"{self.PREFIX} {e}" for e in expected])
        # One command per leaf field: nothing in the input was dropped.
        leaf_fields = (
            len(peer_group)
            - 1  # name is the key, not an attribute
            - 3  # the three nested structs are counted by their leaves below
            + 1  # add_path BOTH expands to two commands
            + len(peer_group["bgp_peer_timers"])
            + len(peer_group["pre_filter"])
            + len(peer_group["post_filter"])
        )
        self.assertEqual(len(commands), leaf_fields)
        self.assertEqual(len(commands), 36)

    def test_link_bandwidth(self) -> None:
        """The link-bandwidth trio bgpd resolves peer > group is generated."""
        peer_group = {
            "name": "TEST",
            "link_bandwidth_bps": 10_000_000_000,
            "advertise_link_bandwidth": "ADVERTISE",
            "receive_link_bandwidth": 1,
        }
        commands = generate_peer_group_commands(peer_group)
        self.assertIn(f"{self.PREFIX} link-bandwidth 10G", commands)
        self.assertIn(f"{self.PREFIX} advertise-lbw ADVERTISE", commands)
        self.assertIn(f"{self.PREFIX} receive-lbw 1", commands)

    def test_deprecated_asn_fields_map_to_4_byte_attribute(self) -> None:
        """The deprecated i32 ASNs are carried over, not dropped."""
        peer_group = {"name": "TEST", "remote_as": 65000, "local_as": 64512}
        commands = generate_peer_group_commands(peer_group)
        self.assertIn(f"{self.PREFIX} remote-asn 65000", commands)
        self.assertIn(f"{self.PREFIX} local-asn 64512", commands)
        self.assertFalse(any(c.startswith("# WARNING") for c in commands))

    def test_canonical_asn_fields_override_deprecated_fields(self) -> None:
        """Canonical 4-byte ASNs win when both representations are present."""
        peer_group = {
            "name": "TEST",
            "remote_as_4_byte": 4200000000,
            "remote_as": 23456,
            "local_as_4_byte": 4200000001,
            "local_as": 23457,
        }
        commands = generate_peer_group_commands(peer_group)
        self.assertIn(f"{self.PREFIX} remote-asn 4200000000", commands)
        self.assertIn(f"{self.PREFIX} local-asn 4200000001", commands)
        self.assertNotIn(f"{self.PREFIX} remote-asn 23456", commands)
        self.assertNotIn(f"{self.PREFIX} local-asn 23457", commands)

    def test_unrepresentable_peer_group_fields_warn(self) -> None:
        """PeerGroup fields bgpd never reads from a group are reported, not
        silently dropped, and produce no config command."""
        peer_group = {
            "name": "TEST",
            "remote_as_4_byte": 65000,
            "local_addr": "2001:db8::2",
            "next_hop4": "10.1.1.1",
            "next_hop6": "2001:db8::a",
            "enabled": True,
            "router_port_id": 7,
            "bgp_peer_timers": {
                "hold_time_seconds": 90,
                "graceful_restart_end_of_rib_seconds": 30,
            },
        }
        commands = generate_peer_group_commands(peer_group)
        warnings = [c for c in commands if c.startswith("# WARNING")]
        self.assertEqual(
            warnings,
            [
                f"# WARNING: peer-group TEST: field {f} has no CLI equivalent, "
                "not converted"
                for f in (
                    "local_addr",
                    "next_hop4",
                    "next_hop6",
                    "enabled",
                    "router_port_id",
                    "bgp_peer_timers.graceful_restart_end_of_rib_seconds",
                )
            ],
        )
        self.assertEqual(
            [c for c in commands if not c.startswith("#")],
            [f"{self.PREFIX} remote-asn 65000", f"{self.PREFIX} timers hold-time 90"],
        )


class GeneratePeerCommandsTest(unittest.TestCase):
    """Tests for generate_peer_commands (neighbor grammar)."""

    def test_empty_peer_addr_returns_empty(self) -> None:
        """Peer without peer_addr should return empty list."""
        peer = {"remote_as_4_byte": 65000}
        commands = generate_peer_commands(peer)
        self.assertEqual(commands, [])

    def test_remote_asn(self) -> None:
        """remote_as_4_byte should generate correct command."""
        peer = {"peer_addr": "2001:db8::1", "remote_as_4_byte": 65000}
        commands = generate_peer_commands(peer)
        self.assertIn(
            "config protocol bgp neighbor 2001:db8::1 remote-asn 65000", commands
        )

    def test_local_asn(self) -> None:
        """local_as_4_byte should generate local-asn command."""
        peer = {"peer_addr": "2001:db8::1", "local_as_4_byte": 64512}
        commands = generate_peer_commands(peer)
        self.assertIn(
            "config protocol bgp neighbor 2001:db8::1 local-asn 64512", commands
        )

    def test_peer_group_name(self) -> None:
        """peer_group_name should generate peer-group command."""
        peer = {"peer_addr": "2001:db8::1", "peer_group_name": "MY-GROUP"}
        commands = generate_peer_commands(peer)
        self.assertIn(
            "config protocol bgp neighbor 2001:db8::1 peer-group MY-GROUP", commands
        )

    def test_local_addr(self) -> None:
        """local_addr should generate bind-addr address command."""
        peer = {"peer_addr": "2001:db8::1", "local_addr": "2001:db8::a"}
        commands = generate_peer_commands(peer)
        self.assertIn(
            "config protocol bgp neighbor 2001:db8::1 bind-addr address 2001:db8::a",
            commands,
        )

    def test_is_passive(self) -> None:
        """is_passive should generate the passive flag."""
        peer = {"peer_addr": "2001:db8::1", "is_passive": True}
        commands = generate_peer_commands(peer)
        self.assertIn("config protocol bgp neighbor 2001:db8::1 passive true", commands)
        peer["is_passive"] = False
        commands = generate_peer_commands(peer)
        self.assertIn(
            "config protocol bgp neighbor 2001:db8::1 passive false", commands
        )

    def test_disable_ipv4_afi_false_preserved(self) -> None:
        """disable_ipv4_afi: false should NOT be omitted."""
        peer = {"peer_addr": "2001:db8::1", "disable_ipv4_afi": False}
        commands = generate_peer_commands(peer)
        self.assertIn(
            "config protocol bgp neighbor 2001:db8::1 afi disable-ipv4-afi false",
            commands,
        )

    def test_disable_ipv4_afi_true(self) -> None:
        """disable_ipv4_afi: true should generate correct command."""
        peer = {"peer_addr": "2001:db8::1", "disable_ipv4_afi": True}
        commands = generate_peer_commands(peer)
        self.assertIn(
            "config protocol bgp neighbor 2001:db8::1 afi disable-ipv4-afi true",
            commands,
        )

    def test_stateful_ha(self) -> None:
        """enable_stateful_ha should generate graceful-restart stateful-ha."""
        peer = {"peer_addr": "2001:db8::1", "enable_stateful_ha": True}
        commands = generate_peer_commands(peer)
        self.assertIn(
            "config protocol bgp neighbor 2001:db8::1 graceful-restart stateful-ha true",
            commands,
        )

    def test_add_path_both(self) -> None:
        """add_path BOTH (3) should generate both directions."""
        peer = {"peer_addr": "2001:db8::1", "add_path": 3}
        commands = generate_peer_commands(peer)
        self.assertIn(
            "config protocol bgp neighbor 2001:db8::1 add-path receive true", commands
        )
        self.assertIn(
            "config protocol bgp neighbor 2001:db8::1 add-path send true", commands
        )

    def test_add_path_receive_only(self) -> None:
        """add_path RECEIVE (1) should generate only the receive direction."""
        peer = {"peer_addr": "2001:db8::1", "add_path": 1}
        commands = generate_peer_commands(peer)
        self.assertIn(
            "config protocol bgp neighbor 2001:db8::1 add-path receive true", commands
        )
        self.assertNotIn(
            "config protocol bgp neighbor 2001:db8::1 add-path send true", commands
        )

    def test_timers(self) -> None:
        """Timer fields should generate correct commands."""
        peer = {
            "peer_addr": "2001:db8::1",
            "bgp_peer_timers": {
                "hold_time_seconds": 120,
                "keep_alive_seconds": 40,
                "out_delay_seconds": 0,
                "graceful_restart_seconds": 60,
            },
        }
        commands = generate_peer_commands(peer)
        self.assertIn(
            "config protocol bgp neighbor 2001:db8::1 timers hold-time 120", commands
        )
        self.assertIn(
            "config protocol bgp neighbor 2001:db8::1 timers keepalive 40", commands
        )
        self.assertIn(
            "config protocol bgp neighbor 2001:db8::1 timers out-delay 0", commands
        )
        self.assertIn(
            "config protocol bgp neighbor 2001:db8::1 graceful-restart restart-time 60",
            commands,
        )

    def test_route_limits(self) -> None:
        """pre_filter/post_filter should generate max-route commands."""
        peer = {
            "peer_addr": "2001:db8::1",
            "pre_filter": {"max_routes": 45000},
            "post_filter": {"max_routes": 50000, "warning_limit": 48000},
        }
        commands = generate_peer_commands(peer)
        self.assertIn(
            "config protocol bgp neighbor 2001:db8::1 max-route pre-filter 45000",
            commands,
        )
        self.assertIn(
            "config protocol bgp neighbor 2001:db8::1 max-route post-filter 50000",
            commands,
        )
        self.assertIn(
            "config protocol bgp neighbor 2001:db8::1 max-route post-warning-threshold 48000",
            commands,
        )

    def test_parity_fields(self) -> None:
        """Fields carried over from the old peer commands map to the new grammar."""
        peer = {
            "peer_addr": "2001:db8::1",
            "next_hop4": "0.0.0.0",
            "next_hop6": "2001:db8::a",
            "next_hop_self": True,
            "link_bandwidth_bps": "10G",
            "advertise_link_bandwidth": 1,
            "receive_link_bandwidth": 1,
            "peer_id": "test-peer:v6:1",
            "type": "BGP_MONITOR",
            "bgp_peer_timers": {"withdraw_unprog_delay_seconds": 5},
            "pre_filter": {"warning_limit": 100, "warning_only": True},
        }
        commands = generate_peer_commands(peer)
        prefix = "config protocol bgp neighbor 2001:db8::1"
        self.assertIn(f"{prefix} next-hop4 0.0.0.0", commands)
        self.assertIn(f"{prefix} next-hop6 2001:db8::a", commands)
        self.assertIn(f"{prefix} next-hop-self true", commands)
        self.assertIn(f"{prefix} link-bandwidth 10G", commands)
        self.assertIn(f"{prefix} advertise-lbw 1", commands)
        self.assertIn(f"{prefix} receive-lbw 1", commands)
        self.assertIn(f"{prefix} peer-id test-peer:v6:1", commands)
        self.assertIn(f"{prefix} type BGP_MONITOR", commands)
        self.assertIn(f"{prefix} timers withdraw-unprog-delay 5", commands)
        self.assertIn(f"{prefix} max-route pre-warning-threshold 100", commands)
        self.assertIn(f"{prefix} max-route pre-warning-only true", commands)

    def test_zero_values_preserved(self) -> None:
        """Explicit zero timers/limits must not be dropped."""
        peer = {
            "peer_addr": "2001:db8::1",
            "bgp_peer_timers": {"hold_time_seconds": 0, "keep_alive_seconds": 0},
            "pre_filter": {"max_routes": 0},
        }
        commands = generate_peer_commands(peer)
        prefix = "config protocol bgp neighbor 2001:db8::1"
        self.assertIn(f"{prefix} timers hold-time 0", commands)
        self.assertIn(f"{prefix} timers keepalive 0", commands)
        self.assertIn(f"{prefix} max-route pre-filter 0", commands)

    def test_add_path_symbolic(self) -> None:
        """add_path serialized as an enum name must not crash the tool."""
        peer = {"peer_addr": "2001:db8::1", "add_path": "BOTH"}
        commands = generate_peer_commands(peer)
        self.assertIn(
            "config protocol bgp neighbor 2001:db8::1 add-path receive true", commands
        )
        self.assertIn(
            "config protocol bgp neighbor 2001:db8::1 add-path send true", commands
        )

    def test_session_flags(self) -> None:
        """The session knobs bgpd resolves peer > group all have a spelling."""
        peer = {
            "peer_addr": "2001:db8::1",
            "route_refresh": True,
            "remove_private_as": True,
            "enforce_first_as": False,
            "ttl_security_hops": 2,
        }
        commands = generate_peer_commands(peer)
        prefix = "config protocol bgp neighbor 2001:db8::1"
        self.assertIn(f"{prefix} route-refresh true", commands)
        self.assertIn(f"{prefix} remove-private-as true", commands)
        self.assertIn(f"{prefix} enforce-first-as false", commands)
        self.assertIn(f"{prefix} ttl-security-hops 2", commands)

    def test_deprecated_asn_fields_map_to_4_byte_attribute(self) -> None:
        """The deprecated i32 ASNs are carried over, not dropped."""
        peer = {"peer_addr": "2001:db8::1", "remote_as": 65000, "local_as": 64512}
        commands = generate_peer_commands(peer)
        prefix = "config protocol bgp neighbor 2001:db8::1"
        self.assertIn(f"{prefix} remote-asn 65000", commands)
        self.assertIn(f"{prefix} local-asn 64512", commands)
        self.assertFalse(any(c.startswith("# WARNING") for c in commands))

    def test_complete_neighbor_loses_no_field(self) -> None:
        """Every BgpPeer field bgpd reads produces exactly one command, in the
        grammar the neighbor dispatcher accepts, and nothing is flagged."""
        peer: dict[str, Any] = {
            "peer_addr": "2001:db8::1",
            "remote_as_4_byte": 65000,
            "local_as_4_byte": 64512,
            "peer_group_name": "SPINE",
            "description": "spine peer",
            "peer_tag": "FSW",
            "local_addr": "2001:db8::2",
            "ingress_policy_name": "IN_POLICY",
            "egress_policy_name": "OUT_POLICY",
            "next_hop4": "10.1.1.1",
            "next_hop6": "2001:db8::a",
            "peer_id": "test-peer:v6:1",
            "type": "BGP_MONITOR",
            "is_passive": True,
            "is_rr_client": True,
            "is_confed_peer": False,
            "is_redistribute_peer": True,
            "enhanced_route_refresh": True,
            "route_refresh": False,
            "remove_private_as": True,
            "enforce_first_as": True,
            "disable_ipv4_afi": True,
            "disable_ipv6_afi": False,
            "v4_over_v6_nexthop": True,
            "enable_stateful_ha": True,
            "next_hop_self": True,
            "ttl_security_hops": 2,
            "link_bandwidth_bps": "10G",
            "advertise_link_bandwidth": 1,
            "receive_link_bandwidth": "ACCEPT",
            "add_path": 3,
            "bgp_peer_timers": {
                "hold_time_seconds": 90,
                "keep_alive_seconds": 30,
                "out_delay_seconds": 5,
                "withdraw_unprog_delay_seconds": 10,
                "graceful_restart_seconds": 120,
            },
            "pre_filter": {
                "max_routes": 45000,
                "warning_limit": 40000,
                "warning_only": True,
            },
            "post_filter": {
                "max_routes": 50000,
                "warning_limit": 48000,
                "warning_only": False,
            },
        }
        expected = [
            "remote-asn 65000",
            "local-asn 64512",
            "peer-group SPINE",
            "description 'spine peer'",
            "peer-tag FSW",
            "bind-addr address 2001:db8::2",
            "ingress-policy IN_POLICY",
            "egress-policy OUT_POLICY",
            "next-hop4 10.1.1.1",
            "next-hop6 2001:db8::a",
            "peer-id test-peer:v6:1",
            "type BGP_MONITOR",
            "passive true",
            "rr-client true",
            "confed-peer false",
            "redistribute-peer true",
            "enhanced-route-refresh true",
            "route-refresh false",
            "remove-private-as true",
            "enforce-first-as true",
            "afi disable-ipv4-afi true",
            "afi disable-ipv6-afi false",
            "afi ipv4-over-ipv6-nh true",
            "graceful-restart stateful-ha true",
            "next-hop-self true",
            "ttl-security-hops 2",
            "link-bandwidth 10G",
            "advertise-lbw 1",
            "receive-lbw ACCEPT",
            "add-path receive true",
            "add-path send true",
            "timers hold-time 90",
            "timers keepalive 30",
            "timers out-delay 5",
            "timers withdraw-unprog-delay 10",
            "graceful-restart restart-time 120",
            "max-route pre-filter 45000",
            "max-route pre-warning-threshold 40000",
            "max-route pre-warning-only true",
            "max-route post-filter 50000",
            "max-route post-warning-threshold 48000",
            "max-route post-warning-only false",
        ]
        prefix = "config protocol bgp neighbor 2001:db8::1"
        commands = generate_peer_commands(peer)
        self.assertEqual(commands, [f"{prefix} {e}" for e in expected])
        # One command per leaf field: nothing in the input was dropped.
        leaf_fields = (
            len(peer)
            - 1  # peer_addr is the key, not an attribute
            - 3  # the three nested structs are counted by their leaves below
            + 1  # add_path BOTH expands to two commands
            + len(peer["bgp_peer_timers"])
            + len(peer["pre_filter"])
            + len(peer["post_filter"])
        )
        self.assertEqual(len(commands), leaf_fields)

    def test_unrepresentable_neighbor_fields_warn(self) -> None:
        """Fields nothing in bgpd reads are reported, not silently dropped."""
        peer = {
            "peer_addr": "2001:db8::1",
            "remote_as_4_byte": 65000,
            "enabled": True,
            "router_port_id": 7,
            "bgp_peer_timers": {
                "hold_time_seconds": 90,
                "graceful_restart_end_of_rib_seconds": 30,
            },
        }
        commands = generate_peer_commands(peer)
        prefix = "config protocol bgp neighbor 2001:db8::1"
        warnings = [c for c in commands if c.startswith("# WARNING")]
        self.assertEqual(
            warnings,
            [
                "# WARNING: neighbor 2001:db8::1: field enabled has no CLI "
                "equivalent, not converted",
                "# WARNING: neighbor 2001:db8::1: field router_port_id has no CLI "
                "equivalent, not converted",
                "# WARNING: neighbor 2001:db8::1: field "
                "bgp_peer_timers.graceful_restart_end_of_rib_seconds has no CLI "
                "equivalent, not converted",
            ],
        )
        # ... and only the convertible fields produce commands.
        self.assertEqual(
            [c for c in commands if not c.startswith("#")],
            [f"{prefix} remote-asn 65000", f"{prefix} timers hold-time 90"],
        )

    def test_warning_subject_cannot_break_out_of_comment(self) -> None:
        """A hostile peer_addr must not turn a warning into a script line."""
        peer = {"peer_addr": "x\nrm -rf /", "enabled": True}
        script = json_to_cli(peer_config(peer), binary="fboss2")
        self.assertFalse(any(line.startswith("rm ") for line in script))
        self.assertTrue(
            any(line.startswith("# WARNING: neighbor x?rm") for line in script)
        )


def peer_config(peer: dict) -> dict:
    """A whole-config wrapper around one peer."""
    return {"peers": [peer]}


class GenerateAsPathListCommandsTest(unittest.TestCase):
    """Tests for generate_as_path_list_commands (as-path-list grammar)."""

    PREFIX = "config protocol bgp policy as-path-list ASPL"

    def test_empty_name_returns_empty(self) -> None:
        self.assertEqual(generate_as_path_list_commands({"as_paths": ["^1_"]}), [])

    def test_bare_list_is_recreated(self) -> None:
        """A list with nothing to set still comes back by name."""
        self.assertEqual(
            generate_as_path_list_commands({"name": "ASPL"}), [self.PREFIX]
        )

    def test_description(self) -> None:
        commands = generate_as_path_list_commands(
            {"name": "ASPL", "description": "spine as paths"}
        )
        self.assertEqual(commands, [f"{self.PREFIX} description 'spine as paths'"])

    def test_empty_description_not_emitted(self) -> None:
        """bgpd's running config carries the "" default; nothing to replay."""
        commands = generate_as_path_list_commands({"name": "ASPL", "description": ""})
        self.assertEqual(commands, [self.PREFIX])

    def test_regex_per_as_path(self) -> None:
        """Each as_paths pattern is one quoted `regex` line, in order."""
        commands = generate_as_path_list_commands(
            {"name": "ASPL", "as_paths": ["^65000_", "_65001$"]}
        )
        self.assertEqual(
            commands,
            [f"{self.PREFIX} regex '^65000_'", f"{self.PREFIX} regex '_65001$'"],
        )

    def test_boolean_operator_and(self) -> None:
        for raw in (1, "AND"):
            commands = generate_as_path_list_commands(
                {"name": "ASPL", "boolean_operator": raw}
            )
            self.assertEqual(commands, [f"{self.PREFIX} boolean-operator AND"], raw)

    def test_boolean_operator_or_is_default(self) -> None:
        """OR is the thrift default and the CLI's; nothing to emit."""
        for raw in (2, "OR"):
            commands = generate_as_path_list_commands(
                {"name": "ASPL", "boolean_operator": raw}
            )
            self.assertEqual(commands, [self.PREFIX], raw)

    def test_boolean_operator_not_warns(self) -> None:
        commands = generate_as_path_list_commands(
            {"name": "ASPL", "boolean_operator": 3}
        )
        self.assertEqual(len(commands), 1)
        self.assertTrue(commands[0].startswith("# WARNING:"), commands[0])
        self.assertIn("NOT", commands[0])

    def test_malformed_boolean_operator_passes_through(self) -> None:
        """j2c does not validate enum values; the CLI rejects them on replay."""
        for raw, rendered in ((None, "None"), ([1], "'[1]'")):
            commands = generate_as_path_list_commands(
                {"name": "ASPL", "boolean_operator": raw}
            )
            self.assertEqual(
                commands, [f"{self.PREFIX} boolean-operator {rendered}"], raw
            )

    def test_dead_fields_warn(self) -> None:
        """Fields bgpd never reads surface as warnings, not silently dropped."""
        commands = generate_as_path_list_commands(
            {
                "name": "ASPL",
                "as_path_list_names": ["OTHER"],
                "as_path_list": [{"sequence_number": 10}],
            }
        )
        self.assertEqual(len(commands), 2)
        self.assertIn("as_path_list_names", commands[0])
        self.assertIn("as_path_list entries", commands[1])
        for c in commands:
            self.assertTrue(c.startswith("# WARNING:"), c)

    def test_injection_in_regex_neutralized(self) -> None:
        commands = generate_as_path_list_commands(
            {"name": "ASPL", "as_paths": ["$(reboot)"]}
        )
        self.assertEqual(commands, [f"{self.PREFIX} regex '$(reboot)'"])

    def test_policy_block_orders_lists_first(self) -> None:
        config = {"policies": {"aspath_lists": [{"name": "A"}, {"name": "B"}]}}
        commands = generate_policy_commands(config)
        self.assertEqual(
            commands,
            [
                "config protocol bgp policy as-path-list A",
                "config protocol bgp policy as-path-list B",
            ],
        )

    def test_no_policies_key(self) -> None:
        self.assertEqual(generate_policy_commands({}), [])


class GenerateCommunityListCommandsTest(unittest.TestCase):
    """Tests for generate_community_list_commands (community-list grammar)."""

    PREFIX = "config protocol bgp policy community-list CL"

    def test_empty_name_returns_empty(self) -> None:
        self.assertEqual(generate_community_list_commands({"exact_match": True}), [])

    def test_bare_list_is_recreated(self) -> None:
        self.assertEqual(
            generate_community_list_commands({"name": "CL"}), [self.PREFIX]
        )

    def test_scalar_attributes(self) -> None:
        commands = generate_community_list_commands(
            {
                "name": "CL",
                "description": "transit communities",
                "boolean_operator": 1,
                "exact_match": True,
            }
        )
        self.assertEqual(
            commands,
            [
                f"{self.PREFIX} description 'transit communities'",
                f"{self.PREFIX} boolean-operator AND",
                f"{self.PREFIX} exact-match true",
            ],
        )

    def test_community_per_value(self) -> None:
        """Each communities value is one quoted `community` line, in order."""
        commands = generate_community_list_commands(
            {"name": "CL", "communities": ["65000:100", "^65000:.*$"]}
        )
        self.assertEqual(
            commands,
            [
                f"{self.PREFIX} community {escape_shell_arg('65000:100')}",
                f"{self.PREFIX} community {escape_shell_arg('^65000:.*$')}",
            ],
        )

    def test_injection_in_community_neutralized(self) -> None:
        commands = generate_community_list_commands(
            {"name": "CL", "communities": ["$(reboot)"]}
        )
        self.assertEqual(
            commands, [f"{self.PREFIX} community {escape_shell_arg('$(reboot)')}"]
        )

    def test_boolean_operator_or_omitted_not_warns(self) -> None:
        self.assertEqual(
            generate_community_list_commands({"name": "CL", "boolean_operator": "OR"}),
            [self.PREFIX],
        )
        commands = generate_community_list_commands(
            {"name": "CL", "boolean_operator": 3}
        )
        self.assertEqual(len(commands), 1)
        self.assertTrue(commands[0].startswith("# WARNING:"), commands[0])
        self.assertIn("NOT", commands[0])

    def test_exact_match_false_preserved(self) -> None:
        self.assertEqual(
            generate_community_list_commands({"name": "CL", "exact_match": False}),
            [f"{self.PREFIX} exact-match false"],
        )

    def test_dead_fields_warn(self) -> None:
        """Fields bgpd never reads surface as warnings, not silently dropped."""
        commands = generate_community_list_commands(
            {
                "name": "CL",
                "community_list_names": ["X"],
                "members": [{"community": {"name": "CM1", "value": "65000:1"}}],
            }
        )
        self.assertEqual(len(commands), 2)
        self.assertIn("community_list_names", commands[0])
        self.assertIn("members entries", commands[1])
        for c in commands:
            self.assertTrue(c.startswith("# WARNING:"), c)

    def test_policy_block_lists_after_as_path_lists(self) -> None:
        config = {
            "policies": {
                "aspath_lists": [{"name": "A"}],
                "community_lists": [{"name": "CL"}],
            }
        }
        commands = generate_policy_commands(config)
        self.assertEqual(
            commands,
            [
                "config protocol bgp policy as-path-list A",
                "config protocol bgp policy community-list CL",
            ],
        )


class GeneratePrefixListCommandsTest(unittest.TestCase):
    """Tests for generate_prefix_list_commands (prefix-list grammar)."""

    PREFIX = "config protocol bgp policy prefix-list PL"

    def test_empty_name_returns_empty(self) -> None:
        self.assertEqual(generate_prefix_list_commands({"version": 4}), [])

    def test_bare_list_is_recreated(self) -> None:
        self.assertEqual(generate_prefix_list_commands({"name": "PL"}), [self.PREFIX])

    def test_scalar_attributes(self) -> None:
        commands = generate_prefix_list_commands(
            {
                "name": "PL",
                "description": "loopbacks",
                "boolean_operator": "AND",
                "compare_operator": 2,
                "version": 6,
            }
        )
        self.assertEqual(
            commands,
            [
                f"{self.PREFIX} description loopbacks",
                "# WARNING: prefix-list PL: boolean_operator AND is not accepted "
                "by bgpd (only OR); not emitted",
                "# WARNING: prefix-list PL: list-level compare_operator GE is "
                "not accepted by bgpd; not emitted",
                f"{self.PREFIX} ip-version v6",
            ],
        )

    def test_boolean_operator_not_or_warns(self) -> None:
        """bgpd: "PrefixList BooleanOperator can only be OR"."""
        for raw, rendered in ((1, "AND"), (3, "NOT"), ("NOT", "NOT")):
            commands = generate_prefix_list_commands(
                {"name": "PL", "boolean_operator": raw}
            )
            self.assertEqual(
                commands,
                [
                    f"# WARNING: prefix-list PL: boolean_operator {rendered} is "
                    "not accepted by bgpd (only OR); not emitted",
                    self.PREFIX,
                ],
                raw,
            )

    def test_or_default_omitted(self) -> None:
        self.assertEqual(
            generate_prefix_list_commands({"name": "PL", "boolean_operator": 2}),
            [self.PREFIX],
        )

    def test_unhashable_version_warns_and_recreates_list(self) -> None:
        commands = generate_prefix_list_commands({"name": "PL", "version": [4]})
        self.assertEqual(
            commands,
            [
                "# WARNING: prefix-list PL: version [4] is neither 4 nor 6; "
                "not emitted",
                self.PREFIX,
            ],
        )

    def test_unexpressible_values_warn(self) -> None:
        commands = generate_prefix_list_commands(
            {
                "name": "PL",
                "compare_operator": 7,
                "version": 5,
                "prefix_list_names": ["X"],
                "ip_version": 1,
            }
        )
        warnings = [command for command in commands if command.startswith("# WARNING:")]
        self.assertEqual(len(warnings), 4)
        self.assertEqual(commands[-1], self.PREFIX)

    def test_any_compare_operator_warns(self) -> None:
        """bgpd rejects a list-level compare_operator whatever its value."""
        for raw, rendered in ((2, "GE"), (7, "RG"), (None, "None"), ([2], "[2]")):
            commands = generate_prefix_list_commands(
                {"name": "PL", "compare_operator": raw}
            )
            self.assertEqual(
                commands,
                [
                    f"# WARNING: prefix-list PL: list-level compare_operator "
                    f"{rendered} is not accepted by bgpd; not emitted",
                    self.PREFIX,
                ],
                raw,
            )

    def test_warning_text_is_single_comment_line(self) -> None:
        """A control character in a JSON name must not end the # comment."""
        name = "PL\nrm -rf /"
        commands = generate_prefix_list_commands(
            {"name": name, "prefix_list_names": ["X"]}
        )
        self.assertEqual(len(commands), 2)
        warning = commands[0]
        self.assertTrue(warning.startswith("# WARNING:"), warning)
        self.assertNotIn("\n", warning)
        self.assertIn("PL?rm -rf /", warning)
        self.assertEqual(
            commands[1],
            f"config protocol bgp policy prefix-list {escape_shell_arg(name)}",
        )

    def test_policy_block_order(self) -> None:
        config = {
            "policies": {
                "aspath_lists": [{"name": "A"}],
                "community_lists": [{"name": "CL"}],
                "prefix_lists": [{"name": "PL"}],
            }
        }
        commands = generate_policy_commands(config)
        self.assertEqual(
            commands,
            [
                "config protocol bgp policy as-path-list A",
                "config protocol bgp policy community-list CL",
                "config protocol bgp policy prefix-list PL",
            ],
        )


class GeneratePrefixListEntryCommandsTest(unittest.TestCase):
    """Tests for generate_prefix_list_entry_commands (entry grammar)."""

    PREFIX = "config protocol bgp policy prefix-list PL prefix 10.0.0.0/8"

    def test_full_entry(self) -> None:
        commands = generate_prefix_list_entry_commands(
            "PL",
            {
                "base_prefix": "10.0.0.0/8",
                "description": "rfc1918 a",
                "match_logic": 1,
                "max_allowed_golden_prefix_subnet_count": 4,
                "prefix_len_ranges": [{"compare_operator": 3, "value": 24}],
                "regex": "^10\\.",
                "communities": ["65000:2", "65000:1"],
            },
        )
        self.assertEqual(
            commands,
            [
                f"{self.PREFIX} description 'rfc1918 a'",
                "# WARNING: prefix-list PL prefix 10.0.0.0/8: match_logic NOT_EQUAL "
                "is not accepted by bgpd (only EQUAL); not emitted",
                f"{self.PREFIX} max-allowed-subnet-count 4",
                f"{self.PREFIX} prefix-len-range compare-operator LE",
                f"{self.PREFIX} prefix-len-range value 24",
                f"{self.PREFIX} regex '^10\\.'",
                f"{self.PREFIX} communities 65000:1",
                f"{self.PREFIX} communities 65000:2",
            ],
        )

    def test_equal_default_omitted(self) -> None:
        commands = generate_prefix_list_entry_commands(
            "PL", {"base_prefix": "10.0.0.0/8", "match_logic": 0}
        )
        self.assertEqual(commands, [self.PREFIX])

    def test_range_operator_rg_warns(self) -> None:
        commands = generate_prefix_list_entry_commands(
            "PL",
            {
                "base_prefix": "10.0.0.0/8",
                "prefix_len_ranges": [{"compare_operator": 7, "value": 24}],
            },
        )
        self.assertEqual(
            commands,
            [
                "# WARNING: prefix-list PL prefix 10.0.0.0/8: prefix_len_ranges "
                "compare_operator RG is not accepted by bgpd; the range is not "
                "emitted",
                # The entry itself still exists, so it is created by its prefix.
                self.PREFIX,
            ],
        )

    def test_missing_base_prefix_warns(self) -> None:
        commands = generate_prefix_list_entry_commands("PL", {"seq_num": 10})
        self.assertEqual(len(commands), 1)
        self.assertTrue(commands[0].startswith("# WARNING:"))
        self.assertIn("no base_prefix", commands[0])

    def test_seq_num_is_dropped_with_warning(self) -> None:
        """bgpd rejects seq_num; the prefix is the entry's identity."""
        commands = generate_prefix_list_entry_commands(
            "PL", {"seq_num": 10, "base_prefix": "10.0.0.0/8"}
        )
        self.assertEqual(
            commands,
            [
                "# WARNING: prefix-list PL prefix 10.0.0.0/8: seq_num 10 is "
                "rejected by bgpd and not part of the CLI's entry identity; dropped",
                self.PREFIX,
            ],
        )

    def test_extra_ranges_and_ip_version_warn(self) -> None:
        commands = generate_prefix_list_entry_commands(
            "PL",
            {
                "base_prefix": "10.0.0.0/8",
                "prefix_len_ranges": [{"value": 24}, {"value": 32}],
                "ip_version": 1,
            },
        )
        self.assertEqual(commands[0], f"{self.PREFIX} prefix-len-range value 24")
        self.assertEqual(len(commands), 3)
        self.assertTrue(commands[1].startswith("# WARNING:"))
        self.assertTrue(commands[2].startswith("# WARNING:"))

    def test_entries_emitted_inside_list(self) -> None:
        commands = generate_prefix_list_commands(
            {"name": "PL", "version": 4, "prefixes": [{"base_prefix": "10.0.0.0/8"}]}
        )
        self.assertEqual(
            commands,
            ["config protocol bgp policy prefix-list PL ip-version v4", self.PREFIX],
        )


class GenerateRoutingPolicyCommandsTest(unittest.TestCase):
    """Tests for generate_routing_policy_commands (routing-policy grammar)."""

    PREFIX = "config protocol bgp policy routing-policy RM"

    def test_empty_name_returns_empty(self) -> None:
        self.assertEqual(generate_routing_policy_commands({"description": "x"}), [])

    def test_bare_policy_is_recreated(self) -> None:
        self.assertEqual(
            generate_routing_policy_commands({"name": "RM"}), [self.PREFIX]
        )

    def test_description(self) -> None:
        commands = generate_routing_policy_commands(
            {"name": "RM", "description": "import from spine"}
        )
        self.assertEqual(commands, [f"{self.PREFIX} description 'import from spine'"])

    def test_result_deny_default_silent_other_warns(self) -> None:
        self.assertEqual(
            generate_routing_policy_commands({"name": "RM", "result": 2}), [self.PREFIX]
        )
        commands = generate_routing_policy_commands({"name": "RM", "result": "ACCEPT"})
        self.assertEqual(len(commands), 1)
        self.assertTrue(commands[0].startswith("# WARNING:"))
        self.assertIn("ACCEPT", commands[0])

    def test_null_result_warns_instead_of_raising(self) -> None:
        commands = generate_routing_policy_commands({"name": "RM", "result": None})
        self.assertEqual(len(commands), 1)
        self.assertTrue(commands[0].startswith("# WARNING:"))
        self.assertIn("None", commands[0])

    def test_policy_block_order_lists_then_policies(self) -> None:
        config = {
            "policies": {
                "bgp_policy_statements": [{"name": "RM"}],
                "aspath_lists": [{"name": "A"}],
                "prefix_lists": [{"name": "PL"}],
            }
        }
        commands = generate_policy_commands(config)
        self.assertEqual(
            commands,
            [
                "config protocol bgp policy as-path-list A",
                "config protocol bgp policy prefix-list PL",
                self.PREFIX,
            ],
        )


class GenerateRoutingPolicyTermCommandsTest(unittest.TestCase):
    """Tests for generate_routing_policy_term_commands (term grammar)."""

    PREFIX = "config protocol bgp policy routing-policy RM term 10"

    def test_bare_term_is_recreated(self) -> None:
        commands = generate_routing_policy_term_commands("RM", {"sequence_number": 10})
        self.assertEqual(commands, [self.PREFIX])

    def test_description(self) -> None:
        commands = generate_routing_policy_term_commands(
            "RM", {"sequence_number": 10, "description": "deny bogons"}
        )
        self.assertEqual(commands, [f"{self.PREFIX} description 'deny bogons'"])

    def test_missing_sequence_number_warns(self) -> None:
        commands = generate_routing_policy_term_commands("RM", {"name": "t1"})
        self.assertEqual(len(commands), 1)
        self.assertTrue(commands[0].startswith("# WARNING:"))
        self.assertIn("t1", commands[0])

    def test_terms_emitted_inside_policy(self) -> None:
        commands = generate_routing_policy_commands(
            {
                "name": "RM",
                "description": "d",
                "policy_entries": [{"sequence_number": 10}, {"sequence_number": 20}],
            }
        )
        self.assertEqual(
            commands,
            [
                "config protocol bgp policy routing-policy RM description d",
                self.PREFIX,
                "config protocol bgp policy routing-policy RM term 20",
            ],
        )


class GenerateRoutingPolicyTermActionCommandsTest(unittest.TestCase):
    """Tests for generate_routing_policy_term_action_commands."""

    TERM = "config protocol bgp policy routing-policy RM term 10"

    def test_result(self) -> None:
        # The result is the flow-control action entry (the action on a match).
        for raw, keyword in (
            (5, "ACCEPT"),
            (6, "REJECT"),
            (7, "CONTINUE"),
            ("DENY", "REJECT"),
        ):
            commands = generate_routing_policy_term_action_commands(
                self.TERM, {"policy_action_entries": [{"type": raw}]}
            )
            self.assertEqual(commands, [f"{self.TERM} action result {keyword}"], raw)

    def test_term_miss_action_default_omitted(self) -> None:
        self.assertEqual(
            generate_routing_policy_term_action_commands(
                self.TERM, {"term_miss_action": 3}
            ),
            [],
        )

    def test_term_miss_action_non_default_warns(self) -> None:
        # term_miss_action is the action on a non-match; the CLI has no
        # spelling for it.
        for raw in (1, 2, 6):
            commands = generate_routing_policy_term_action_commands(
                self.TERM, {"term_miss_action": raw}
            )
            self.assertEqual(len(commands), 1, raw)
            self.assertTrue(commands[0].startswith("# WARNING:"), raw)

    def test_set_actions(self) -> None:
        term = {
            "policy_action_entries": [
                {"type": 1, "set_as_path_prepend": {"asn": 65000, "repeat_times": 2}},
                {
                    "type": 2,
                    "community_action": {"communities": ["65000:1"], "action_type": 1},
                },
                {
                    "type": "COMMUNITY_LIST",
                    "community_action": {"communities": ["65000:2"], "action_type": 2},
                },
                {"type": 3, "set_local_pref": {"local_pref": 200}},
                {"type": 4, "set_origin": 3},
                {
                    "type": 8,
                    "set_nexthop": {"next_hop": {"next_hop_prefix": "10.0.0.1"}},
                },
                {"type": 10, "med_action": {"med_value": 50, "med_action_type": 1}},
                {
                    "type": 15,
                    "weight_action": {"weight_value": 7, "weight_action_type": 1},
                },
            ]
        }
        commands = generate_routing_policy_term_action_commands(self.TERM, term)
        self.assertEqual(
            commands,
            [
                f"{self.TERM} action set as-path prepend 65000 65000",
                f"{self.TERM} action set community 65000:1 additive",
                f"{self.TERM} action set community 65000:2",
                f"{self.TERM} action set local-pref 200",
                f"{self.TERM} action set origin INCOMPLETE",
                f"{self.TERM} action set next-hop 10.0.0.1",
                f"{self.TERM} action set med 50",
                f"{self.TERM} action set weight 7",
            ],
        )

    def test_unexpressible_actions_warn(self) -> None:
        term = {
            "policy_action_entries": [
                {"type": 8, "set_nexthop": {"set_self": True}},
                {"type": 10, "med_action": {"med_value": 1, "med_action_type": 2}},
                {"type": 11},
                {
                    "type": 2,
                    "community_action": {"communities": ["a", "b"], "action_type": 2},
                },
            ]
        }
        commands = generate_routing_policy_term_action_commands(self.TERM, term)
        self.assertEqual(len(commands), 4)
        for c in commands:
            self.assertTrue(c.startswith("# WARNING:"), c)

    def test_actions_emitted_inside_term(self) -> None:
        commands = generate_routing_policy_term_commands(
            "RM",
            {
                "sequence_number": 10,
                "policy_action_entries": [
                    {"type": 5},
                    {"type": 3, "set_local_pref": {"local_pref": 5}},
                ],
            },
        )
        self.assertEqual(
            commands,
            [
                f"{self.TERM} action result ACCEPT",
                f"{self.TERM} action set local-pref 5",
            ],
        )


class GenerateScriptCommandsTest(unittest.TestCase):
    """Tests for generate_exec_commands."""

    def test_exec_commands_contains_header(self) -> None:
        """Exec commands should contain shebang and header."""
        commands = ["test command"]
        result = generate_exec_commands(commands, "fboss2")
        self.assertIn("#!/bin/bash", result)
        # Check for set -e with any comment
        self.assertTrue(any("set -e" in cmd for cmd in result))

    def test_exec_commands_pass_warnings_through(self) -> None:
        """Warning comments are never prefixed with the binary."""
        result = generate_exec_commands(
            ["config protocol bgp global hold-time 90", "# WARNING: x"], "fboss2"
        )
        self.assertIn("fboss2 config protocol bgp global hold-time 90", result)
        self.assertIn("# WARNING: x", result)
        self.assertNotIn("fboss2 # WARNING: x", result)

    def test_exec_commands_prepends_binary(self) -> None:
        """Exec commands should prepend binary to each command."""
        commands = ["config protocol bgp global router-id 10.0.0.1"]
        result = generate_exec_commands(commands, "/path/to/fboss2")
        self.assertIn(
            "/path/to/fboss2 config protocol bgp global router-id 10.0.0.1", result
        )

    def test_exec_commands_keep_warning_comments(self) -> None:
        """A `# WARNING` line is a comment in the script, not a command."""
        commands = [
            "# WARNING: neighbor 2001:db8::1: field enabled has no CLI equivalent"
        ]
        result = generate_exec_commands(commands, "/path/to/fboss2")
        self.assertIn(commands[0], result)
        self.assertFalse(any(line.startswith("/path/to/fboss2 #") for line in result))

    def test_generate_commands_carries_warnings_in_raw_output(self) -> None:
        """--raw output keeps the warning lines next to the object's commands."""
        commands = generate_commands(
            peer_config({"peer_addr": "2001:db8::1", "enabled": True})
        )
        self.assertTrue(commands[0].startswith("# WARNING: neighbor 2001:db8::1"))


class GenerateCommandsOrderTest(unittest.TestCase):
    """generate_commands is the single generator sequence for --raw and the
    script: global, then policy objects, then the peer-groups and peers that
    reference them."""

    def test_policies_between_global_and_peer_groups(self) -> None:
        config = {
            "router_id": "10.0.0.1",
            "policies": {"aspath_lists": [{"name": "ASPL", "as_paths": ["^1_"]}]},
            "peer_groups": [{"name": "PG", "remote_as_4_byte": 65000}],
            "peers": [{"peer_addr": "10.0.0.2", "peer_group_name": "PG"}],
        }
        commands = generate_commands(config)
        idx = {
            "global": next(i for i, c in enumerate(commands) if "global" in c),
            "policy": next(i for i, c in enumerate(commands) if "as-path-list" in c),
            "peer_group": next(
                i for i, c in enumerate(commands) if "peer-group PG" in c
            ),
            "peer": next(i for i, c in enumerate(commands) if "neighbor" in c),
        }
        self.assertLess(idx["global"], idx["policy"])
        self.assertLess(idx["policy"], idx["peer_group"])
        self.assertLess(idx["peer_group"], idx["peer"])

    def test_script_wraps_same_sequence(self) -> None:
        config = {"policies": {"aspath_lists": [{"name": "ASPL"}]}}
        script = json_to_cli(config, binary="fboss2")
        self.assertIn("fboss2 config protocol bgp policy as-path-list ASPL", script)


class JsonToCliIntegrationTest(unittest.TestCase):
    """Integration tests for json_to_cli function."""

    def test_minimal_config(self) -> None:
        """Minimal config with just router_id should work."""
        config = {"router_id": "10.0.0.1"}
        commands = json_to_cli(config, binary="fboss2")
        self.assertTrue(any("router-id 10.0.0.1" in cmd for cmd in commands))

    def test_full_rsw_config(self) -> None:
        """Full RSW-like config should generate all expected commands."""
        config = {
            "router_id": "0.17.1.1",
            "networks6": [
                {
                    "prefix": "2401:db00:501c::/64",
                    "policy_name": "ORIGINATE_RACK_PRIVATE_PREFIXES",
                    "install_to_fib": False,
                    "minimum_supporting_routes": 0,
                }
            ],
            "switch_limit_config": {
                "prefix_limit": 10000,
                "total_path_limit": 250000,
                "max_golden_vips": 2860,
                "overload_protection_mode": 2,
            },
            "peer_groups": [
                {
                    "name": "RSW-FSW-V6",
                    "next_hop_self": True,
                    "is_confed_peer": True,
                    "v4_over_v6_nexthop": True,
                    "bgp_peer_timers": {
                        "hold_time_seconds": 30,
                        "keep_alive_seconds": 10,
                    },
                },
                {"name": "RSW-RTSW-V6", "disable_ipv4_afi": True},
            ],
            "peers": [
                {
                    "peer_addr": "2401:db00:501c::/64",
                    "remote_as_4_byte": 65000,
                    "disable_ipv4_afi": False,
                    "is_passive": True,
                },
                {
                    "peer_addr": "2401:db00:e50e:1000::",
                    "peer_group_name": "RSW-FSW-V6",
                    "remote_as_4_byte": 6001,
                },
            ],
        }

        commands = json_to_cli(config, binary="fboss2")
        joined = "\n".join(commands)

        # Verify global commands
        self.assertIn("router-id 0.17.1.1", joined)
        self.assertIn("network6 add 2401:db00:501c::/64", joined)
        self.assertIn("install-to-fib false", joined)
        self.assertIn("switch-limit 10000", joined)
        self.assertIn("switch-limit-total-path 250000", joined)

        # Verify peer group commands
        self.assertIn("peer-group RSW-FSW-V6 next-hop-self true", joined)
        self.assertIn("peer-group RSW-FSW-V6 confed-peer true", joined)
        self.assertIn("peer-group RSW-FSW-V6 afi ipv4-over-ipv6-nh true", joined)
        self.assertIn("peer-group RSW-FSW-V6 timers hold-time 30", joined)
        self.assertIn("peer-group RSW-RTSW-V6 afi disable-ipv4-afi true", joined)

        # Verify peer commands
        self.assertIn("neighbor 2401:db00:501c::/64 remote-asn 65000", joined)
        self.assertIn("neighbor 2401:db00:501c::/64 afi disable-ipv4-afi false", joined)
        self.assertIn("neighbor 2401:db00:e50e:1000:: peer-group RSW-FSW-V6", joined)

    def test_empty_config(self) -> None:
        """Empty config should produce script with no commands."""
        config = {}
        commands = json_to_cli(config, binary="fboss2")
        # Should only have header and footer, no actual BGP commands
        self.assertTrue(any("#!/bin/bash" in cmd for cmd in commands))

    def test_custom_binary(self) -> None:
        """Custom binary path should be used in commands."""
        config = {"router_id": "10.0.0.1"}
        commands = json_to_cli(config, binary="/custom/path/fboss2")
        self.assertTrue(any("/custom/path/fboss2" in cmd for cmd in commands))

    def test_injection_in_router_id_neutralized(self) -> None:
        """Command injection in router_id should be escaped."""
        config = {"router_id": "$(reboot)"}
        commands = json_to_cli(config, binary="fboss2")
        joined = "\n".join(commands)
        self.assertIn("router-id '$(reboot)'", joined)
        self.assertNotIn("router-id $(reboot)", joined)

    def test_injection_in_peer_addr_neutralized(self) -> None:
        """Command injection in peer_addr should be escaped."""
        config = {"peers": [{"peer_addr": "$(malicious)", "remote_as_4_byte": 65000}]}
        commands = json_to_cli(config, binary="fboss2")
        joined = "\n".join(commands)
        self.assertIn("neighbor '$(malicious)'", joined)
        self.assertNotIn("neighbor $(malicious)", joined)

    def test_injection_in_numeric_and_bool_fields_neutralized(self) -> None:
        """Fields expected to be numeric or boolean are quoted like any other."""
        config = {
            "local_as_4_byte": "65000;id",
            "networks6": [{"prefix": "2401::/64;id", "install_to_fib": "true|id"}],
            "peer_groups": [{"name": "g", "remote_as_4_byte": "1&&id"}],
            "peers": [
                {
                    "peer_addr": "10.0.0.1",
                    "remote_as_4_byte": "2;id",
                    "is_rr_client": "false;id",
                    "bgp_peer_timers": {"hold_time_seconds": "90;id"},
                    "pre_filter": {"max_routes": "5|id"},
                }
            ],
        }
        joined = "\n".join(json_to_cli(config, binary="fboss2"))
        for payload in (
            "local-asn '65000;id'",
            "network6 add '2401::/64;id' install-to-fib 'true|id'",
            "remote-asn '1&&id'",
            "remote-asn '2;id'",
            "rr-client 'false;id'",
            "hold-time '90;id'",
            "pre-filter '5|id'",
        ):
            self.assertIn(payload, joined)
        for line in joined.splitlines():
            self.assertNotRegex(line, r"[;|&]id\b(?!')")

    def test_valid_numeric_fields_render_unquoted(self) -> None:
        """Quoting is a no-op for well-formed integers and booleans."""
        config = {
            "local_as_4_byte": 65000,
            "peers": [
                {
                    "peer_addr": "10.0.0.1",
                    "remote_as_4_byte": 65001,
                    "is_rr_client": True,
                    "bgp_peer_timers": {"hold_time_seconds": 90},
                }
            ],
        }
        joined = "\n".join(json_to_cli(config, binary="fboss2"))
        self.assertIn("local-asn 65000", joined)
        self.assertIn("remote-asn 65001", joined)
        self.assertIn("rr-client true", joined)
        self.assertIn("hold-time 90", joined)
        self.assertNotIn("'", joined.split("set -e")[1].split("echo")[0])


if __name__ == "__main__":
    unittest.main()
