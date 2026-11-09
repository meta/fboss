#!/usr/bin/env python3
# @noautodeps
# Copyright Meta Platforms, Inc. and affiliates.

# FBOSS Hardware Test Runner
#
# This script runs hardware tests for FBOSS. It supports multiple test types:
# - sai_agent: SAI agent hardware tests
# - sai: SAI hardware tests
# - bcm: BCM hardware tests
# - qsfp: QSFP hardware tests
# - link: Link tests
# - platform: Platform service hardware tests
# - led: LED service hardware tests
# - sai_agent_scale: SAI agent scale tests
# - sai_invariant_agent: SAI agent invariant config tests
# - benchmark: Benchmark tests
# - cli: CLI tests
#
# USAGE EXAMPLES:
#
# Run all SAI agent tests:
#   ./run_test.py sai_agent --config $CONFIG
#
# Run all SAI tests:
#   ./run_test.py sai --config $CONFIG
#
# Run all QSFP tests:
#   ./run_test.py qsfp --qsfp-config $QSFP_CONFIG
#
# Run all link tests:
#   ./run_test.py link --config $CONFIG --qsfp-config $QSFP_CONFIG
#
# Run only coldboot tests:
#   ./run_test.py sai --config $CONFIG --coldboot_only
#
# Run specific tests using filter:
#   ./run_test.py sai --config $CONFIG --filter=HwVlanTest.VlanApplyConfig
#   ./run_test.py sai --config $CONFIG --filter=*Route*V6*
#
# Run tests with multiple filters and negative filter:
#   ./run_test.py sai --config $CONFIG --filter=*Vlan*:*Port*:-*Mac*:*Intf*
#
# Run tests from a filter file:
#   ./run_test.py sai_agent --config $CONFIG --filter_file=./share/hw_sanity_tests/t0_agent_hw_tests.conf
#
# Skip known bad tests:
#   ./run_test.py sai --config $CONFIG --skip-known-bad-tests $KEY
#   Example: --skip-known-bad-tests "brcm/8.2.0.0_odp/8.2.0.0_odp/tomahawk4"
#
# Filter by production features (sai_agent only):
#   ./run_test.py sai_agent --config $CONFIG --enable-production-features $ASIC
#   Features below their minSdkVersion for the built SDK are skipped; for RBB:
#   ./run_test.py sai_agent --config $CONFIG --enable-production-features $ASIC --test-config-options rbb
#
# List tests without running:
#   ./run_test.py sai --config $CONFIG --list_tests
#
# Enable SAI replayer logging:
#   ./run_test.py sai --config $CONFIG --sai_replayer_logging /tmp/logs
#   ./run_test.py sai --config $CONFIG --sai_replayer_logging /tmp/logs --sai_replayer_sdk_log_level DEBUG
#
# Enable FBOSS logging:
#   ./run_test.py sai --config $CONFIG --fboss_logging DBG5
#
# Start FBOSS services without running tests, then stop them when done:
#   ./run_test.py services start --agent-config $AGENT_CONFIG --qsfp-config $QSFP_CONFIG
#   ./run_test.py services start agent --agent-config $AGENT_CONFIG
#   ./run_test.py services start agent qsfp fsdb --agent-config $AGENT_CONFIG --qsfp-config $QSFP_CONFIG
#   ./run_test.py services stop --num-npus 1
#
# KNOWN BAD TESTS AND UNSUPPORTED TESTS:
#
# Known bad tests and unsupported tests are maintained in JSON files indexed by test configuration key.
# Key format: vendor/coldboot-sai/warmboot-sai/asic (e.g., "brcm/8.2.0.0_odp/8.2.0.0_odp/tomahawk4")
#
# File locations:
# - SAI:           ./share/hw_known_bad_tests/sai_known_bad_tests.materialized_JSON
#                  ./share/sai_hw_unsupported_tests/sai_hw_unsupported_tests.materialized_JSON
# - SAI Agent:     ./share/hw_known_bad_tests/sai_agent_known_bad_tests.materialized_JSON
#                  ./share/sai_hw_unsupported_tests/sai_agent_hw_unsupported_tests.materialized_JSON
# - QSFP: ./share/qsfp_known_bad_tests/fboss_qsfp_known_bad_tests.materialized_JSON
#         ./share/qsfp_unsupported_tests/fboss_qsfp_unsupported_tests.materialized_JSON
# - Link: ./share/link_known_bad_tests/agent_ensemble_link_known_bad_tests.materialized_JSON
#         ./share/link_known_bad_tests/agent_ensemble_link_unsupported_tests.materialized_JSON

if __name__ == "__main__":
    from fboss_test_runner.main import main

    main()
