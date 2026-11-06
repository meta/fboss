# BGP CLI Configuration Test Plan

This document describes the testing workflow for the BGP CLI commands and the JSON-to-CLI (j2c) tool, which enables round-trip conversion of BGP++ configurations.

## Overview

The BGP CLI implementation provides:
1. CLI commands to configure BGP++ compatible JSON configuration
2. A j2c (JSON-to-CLI) tool that converts BGP++ JSON back to CLI commands
3. Round-trip capability: `JSON → CLI → JSON (via fboss2)`

## Build Verification

### Building the fboss2 binary

```bash
buck build //fboss/cli/fboss2:fboss2-dev
```

### Building the config library

```bash
buck build //fboss/cli/fboss2:fboss2-config-lib
```

## Actual Test Workflow Execution (Verified)

The following workflow was executed and verified on 2026-02-02:

### Step 1: Prepare Test Environment

```bash
# Set up the fboss2 binary path
export FBOSS2_BIN=$(buck build --show-output //fboss/cli/fboss2:fboss2-dev 2>/dev/null | awk '{print $2}')

# Clear any existing BGP configuration
rm -f ~/.fboss2/bgp_config.json
```

### Step 2: Test Input JSON (`/tmp/rsw_bgp_nao.txt`)

A production-like RSW BGP configuration with:
- 1 networks6 entry
- 2 peer_groups (RSW-FSW-V6, RSW-RTSW-V6)
- 10 peers (various configurations)
- switch_limit_config with all fields

### Step 3: Generate CLI Script

```bash
python3 fboss/cli/fboss2/tools/bgp_json_to_cli.py \
    /tmp/rsw_bgp_nao.txt \
    --binary "$FBOSS2_BIN" \
    --output /tmp/configure_rsw_nao.sh
```

**Output:** `Generated 119 commands to /tmp/configure_rsw_nao.sh`

### Step 4: Sample Generated Commands

```bash
# Global configuration
fboss2 config protocol bgp global router-id 0.17.1.1
fboss2 config protocol bgp global network6 add 2401:db00:501c::/64 policy ORIGINATE_RACK_PRIVATE_PREFIXES install-to-fib false min-routes 0
fboss2 config protocol bgp global switch-limit 10000
fboss2 config protocol bgp global switch-limit-total-path 250000
fboss2 config protocol bgp global switch-limit-max-golden-vips 2860
fboss2 config protocol bgp global switch-limit-overload-protection-mode 2

# Peer group configuration
fboss2 config protocol bgp peer-group RSW-FSW-V6 description "BGP peering from RSW to FSW, IPv6 sessions"
fboss2 config protocol bgp peer-group RSW-FSW-V6 next-hop-self true
fboss2 config protocol bgp peer-group RSW-FSW-V6 confed-peer true
fboss2 config protocol bgp peer-group RSW-FSW-V6 ingress-policy PROPAGATE_RSW_FSW_IN
fboss2 config protocol bgp peer-group RSW-FSW-V6 egress-policy PROPAGATE_RSW_FSW_OUT
fboss2 config protocol bgp peer-group RSW-FSW-V6 afi ipv4-over-ipv6-nh true
fboss2 config protocol bgp peer-group RSW-FSW-V6 timers hold-time 30
fboss2 config protocol bgp peer-group RSW-FSW-V6 timers keepalive 10
fboss2 config protocol bgp peer-group RSW-RTSW-V6 afi disable-ipv4-afi true

# Peer configuration (with explicit disable_ipv4_afi: false preserved)
fboss2 config protocol bgp neighbor 2401:db00:501c::/64 remote-asn 65000
fboss2 config protocol bgp neighbor 2401:db00:501c::/64 bind-addr address 2401:db00:501c::a
fboss2 config protocol bgp neighbor 2401:db00:501c::/64 passive true
fboss2 config protocol bgp neighbor 2401:db00:501c::/64 afi disable-ipv4-afi false
```

### Step 5: Execute Script

```bash
chmod +x /tmp/configure_rsw_nao.sh
bash /tmp/configure_rsw_nao.sh
```

**Result:** All 119 commands executed successfully.
**Output:** `BGP configuration complete! Config saved to: ~/.fboss2/bgp_config.json`

### Step 6: Compare Results

```bash
diff <(jq -S . /tmp/rsw_bgp_nao.txt) <(jq -S . ~/.fboss2/bgp_config.json)
```

**Verified Differences (Expected):**

| Field | Input | Output | Explanation |
|-------|-------|--------|-------------|
| Default fields | N/A | `communities`, `hold_time`, `listen_addr`, `listen_port` | BgpConfigSession adds sensible defaults |
| `link_bandwidth_bps` | `"10G"` | `10000000000` | CLI normalizes to numeric (bps) |
| `advertise_link_bandwidth` | `1` | `true` | CLI normalizes to boolean |

**All critical fields are preserved exactly:**
- ✅ `networks6` with `prefix`, `policy_name`, `install_to_fib: false`, `minimum_supporting_routes`
- ✅ `switch_limit_config` with all 4 fields
- ✅ `disable_ipv4_afi: false` preserved (not omitted)
- ✅ `disable_ipv4_afi: true` preserved
- ✅ All peer group timers and pre_filter settings
- ✅ All peer configurations including peer_group_name inheritance

## Commands Reference

| Category | Command | Description |
|----------|---------|-------------|
| Global | `config protocol bgp global router-id <id>` | Set router ID |
| Global | `config protocol bgp global local-asn <asn>` | Set local AS number |
| Global | `config protocol bgp global network6 add <prefix> [policy <name>] [install-to-fib <bool>] [min-routes <n>]` | Add IPv6 network |
| Global | `config protocol bgp global switch-limit <n>` | Set prefix limit |
| Global | `config protocol bgp global switch-limit-total-path <n>` | Set total path limit |
| Global | `config protocol bgp global switch-limit-max-golden-vips <n>` | Set max golden VIPs |
| Global | `config protocol bgp global switch-limit-overload-protection-mode <n>` | Set overload protection mode |
| Peer Group | `config protocol bgp peer-group <name> remote-asn <asn>` | Set remote AS |
| Peer Group | `config protocol bgp peer-group <name> description <text>` | Set description |
| Peer Group | `config protocol bgp peer-group <name> next-hop-self <bool>` | Enable/disable next-hop-self |
| Peer Group | `config protocol bgp peer-group <name> afi disable-ipv4-afi <bool>` | Disable IPv4 AFI |
| Peer Group | `config protocol bgp peer-group <name> afi ipv4-over-ipv6-nh <bool>` | Enable v4-over-v6 nexthop |
| Peer Group | `config protocol bgp peer-group <name> timers hold-time <sec>` | Set hold time |
| Peer Group | `config protocol bgp peer-group <name> timers keepalive <sec>` | Set keepalive interval |
| Peer Group | `config protocol bgp peer-group <name> max-route pre-filter <n>` | Set max routes (pre-policy; 0 = unlimited) |
| Neighbor | `config protocol bgp neighbor <addr> remote-asn <asn>` | Set remote AS |
| Neighbor | `config protocol bgp neighbor <addr> peer-group <name>` | Assign to peer group |
| Neighbor | `config protocol bgp neighbor <addr> bind-addr address <addr>` | Set local address |
| Neighbor | `config protocol bgp neighbor <addr> passive <true|false>` | Listen-only (true) or listen-and-connect (false) session |
| Neighbor | `delete protocol bgp neighbor <addr>` | Delete a neighbor |
| Neighbor | `config protocol bgp neighbor <addr> afi disable-ipv4-afi <bool>` | Disable IPv4 AFI |
| Neighbor | `config protocol bgp neighbor <addr> link-bandwidth <bw>` | Set link bandwidth |

## Automated Test Script

```bash
#!/bin/bash
# BGP CLI Round-Trip Test Script

set -e

echo "=== BGP CLI Round-Trip Test ==="

# Setup
FBOSS2_BIN=$(buck build --show-output //fboss/cli/fboss2:fboss2-dev 2>/dev/null | awk '{print $2}')
J2C_TOOL="fboss/cli/fboss2/tools/bgp_json_to_cli.py"
TEST_INPUT="/tmp/test_bgp_input.json"
CLI_SCRIPT="/tmp/test_bgp_cli.sh"
OUTPUT_JSON="$HOME/.fboss2/bgp_config.json"

# Create test input
cat > "$TEST_INPUT" << 'EOF'
{
  "router_id": "10.0.0.1",
  "networks6": [
    {
      "prefix": "2001:db8::/32",
      "policy_name": "TEST_POLICY",
      "install_to_fib": false,
      "minimum_supporting_routes": 1
    }
  ],
  "peer_groups": [
    {
      "name": "TEST-GROUP",
      "next_hop_self": true,
      "disable_ipv4_afi": false
    }
  ],
  "peers": [
    {
      "peer_addr": "2001:db8::1",
      "remote_as_4_byte": 65001,
      "disable_ipv4_afi": false
    }
  ],
  "switch_limit_config": {
    "prefix_limit": 1000,
    "total_path_limit": 5000
  }
}
EOF

# Clean up previous config
rm -f "$OUTPUT_JSON"

# Generate and execute CLI commands
echo "Generating CLI commands..."
python3 "$J2C_TOOL" "$TEST_INPUT" --binary "$FBOSS2_BIN" --output "$CLI_SCRIPT"
chmod +x "$CLI_SCRIPT"

echo "Executing CLI commands..."
bash "$CLI_SCRIPT"

# Compare results
echo "Comparing JSON output..."
if diff <(jq -S . "$TEST_INPUT") <(jq -S . "$OUTPUT_JSON") > /dev/null; then
    echo "✅ TEST PASSED: Generated JSON matches input"
    exit 0
else
    echo "❌ TEST FAILED: JSON mismatch"
    echo "Differences:"
    diff <(jq -S . "$TEST_INPUT") <(jq -S . "$OUTPUT_JSON")
    exit 1
fi
```

## Known Limitations

1. **networks4**: Currently not implemented (empty array only)
2. **Order of fields**: JSON key ordering may differ but content is semantically equivalent
3. **Bandwidth format**: Link bandwidth can be specified as string (e.g., "10G") or integer (bps)
