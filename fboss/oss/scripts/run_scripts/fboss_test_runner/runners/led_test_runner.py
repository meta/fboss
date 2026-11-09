#!/usr/bin/env python3
# @noautodeps
# (c) 2026 Nexthop Systems Inc.

from argparse import Namespace

from fboss_test_runner.runners.test_runner import TestRunner
from fboss_test_runner.tier_selection import TestTier

LED_TEST_BINARY = "led_service_hw_test"


class LedTestRunner(TestRunner):
    """Runner for the led_service_hw_test gtest binary.

    The binary reads its platform/LED config from /etc/coop/led.conf (the same
    file led_service uses on the DUT), so no --config or --qsfp-config is
    passed. led_service must stay running while the test runs, so it is
    deliberately omitted from TEST_DISABLE_SERVICES. The
    test's hardware readback path (LedManager::getLedStateFromHW, exercised by
    testTcvrLos' slow-blink LOS checks) depends on led_service having
    initialized the LED controllers. Stopping led_service tears that down and
    makes the physical readback fail even though the in-process color/blink
    logic is correct. LED tests do not warmboot.
    """

    def _get_test_binary_name(self) -> str:
        return LED_TEST_BINARY

    def _get_uniform_test_tier(self) -> TestTier:
        return TestTier.T0

    def _get_warmboot_check_file(self) -> str:
        return ""

    def _get_test_run_args(self, conf_file: str) -> list[str]:  # noqa: ARG002
        return []

    def run_test(self, args: Namespace) -> int:
        # led_service_hw_test does not accept --fruid_filepath (it derives its
        # platform from /etc/coop/led.conf), so null the default fruid path to
        # keep the base runner from appending a gflag the binary would reject.
        args.fruid_path = None

        # LED hw tests do not support warmboot setup/warmboot verification.
        # Force the shared runner down the coldboot-only path so it does not
        # pass --setup-for-warmboot to led_service_hw_test.
        args.coldboot_only = True

        return super().run_test(args)
