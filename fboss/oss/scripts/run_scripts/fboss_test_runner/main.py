#!/usr/bin/env python3
# @noautodeps
# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

import os
import shutil
import sys
from argparse import ArgumentParser, Namespace

from fboss_test_runner.constants import (
    OPT_ARG_FILTER,
    OPT_ARG_FILTER_FILE,
    OPT_ARG_PROFILE,
    OPT_ARG_TIER,
    SUB_CMD_BCM,
    SUB_CMD_BENCHMARK,
    SUB_CMD_FBOSS2_INTEGRATION,
    SUB_CMD_LED,
    SUB_CMD_LINK,
    SUB_CMD_PLATFORM,
    SUB_CMD_QSFP,
    SUB_CMD_SAI,
    SUB_CMD_SAI_AGENT,
    SUB_CMD_SAI_AGENT_SCALE,
    SUB_CMD_SAI_INVARIANT_AGENT,
    SUB_CMD_SERVICES,
)
from fboss_test_runner.errors import TestRunnerUsageError
from fboss_test_runner.log_capture import (
    derive_test_type,
    LOG_BUNDLE_FLAG,
    LogCapture,
    RUN_TEST_LOG_ROOT,
)
from fboss_test_runner.runners.bcm_test_runner import BcmTestRunner
from fboss_test_runner.runners.benchmark_test_runner import BenchmarkTestRunner
from fboss_test_runner.runners.fboss2_integration_test_runner import (
    Fboss2IntegrationTestRunner,
)
from fboss_test_runner.runners.led_test_runner import LedTestRunner
from fboss_test_runner.runners.link_test_runner import LinkTestRunner
from fboss_test_runner.runners.platform_services_test_runner import (
    PlatformServicesTestRunner,
)
from fboss_test_runner.runners.qsfp_test_runner import QsfpTestRunner
from fboss_test_runner.runners.sai_agent_scale_test_runner import (
    SaiAgentScaleTestRunner,
)
from fboss_test_runner.runners.sai_agent_test_runner import SaiAgentTestRunner
from fboss_test_runner.runners.sai_invariant_agent_test_runner import (
    SaiInvariantAgentTestRunner,
)
from fboss_test_runner.runners.sai_test_runner import SaiTestRunner
from fboss_test_runner.runners.service_lifecycle_runner import ServiceLifecycleRunner
from fboss_test_runner.runners.test_runner import TestRunner
from setup import setup_fboss_env


def _build_parser() -> ArgumentParser:
    """Build the CLI parser. Instantiates the runners, which snapshot the
    environment, so this must be called after ``setup_fboss_env()``."""
    ap = ArgumentParser(
        description="FBOSS Hardware Test Runner",
    )
    subparsers = ap.add_subparsers(dest="command", title="test types")

    def _register_runner(
        cmd: str, help_text: str, runner_cls: type[TestRunner]
    ) -> None:
        parser = subparsers.add_parser(cmd, help=help_text)
        # Per-subcommand flag: pass it after the test type, e.g. `sai --log-bundle`.
        parser.add_argument(
            LOG_BUNDLE_FLAG,
            action="store_true",
            help=(
                "Capture this run's output and the systemd service logs into a "
                f"per-run directory under {RUN_TEST_LOG_ROOT}/ and zip it. "
                "Off by default."
            ),
        )
        runner = runner_cls()
        parser.set_defaults(runner=runner)
        runner.add_subcommand_arguments(parser)

    _register_runner(SUB_CMD_BCM, "run bcm tests", BcmTestRunner)
    _register_runner(SUB_CMD_SAI, "run sai tests", SaiTestRunner)
    _register_runner(SUB_CMD_QSFP, "run qsfp tests", QsfpTestRunner)
    _register_runner(SUB_CMD_LINK, "run link tests", LinkTestRunner)
    _register_runner(SUB_CMD_LED, "run led service tests", LedTestRunner)
    _register_runner(SUB_CMD_SAI_AGENT, "run sai agent tests", SaiAgentTestRunner)
    _register_runner(
        SUB_CMD_SAI_AGENT_SCALE, "run sai agent scale tests", SaiAgentScaleTestRunner
    )
    _register_runner(
        SUB_CMD_SAI_INVARIANT_AGENT,
        "run sai agent invariant config tests",
        SaiInvariantAgentTestRunner,
    )
    _register_runner(
        SUB_CMD_PLATFORM, "run platform services test", PlatformServicesTestRunner
    )
    _register_runner(
        SUB_CMD_FBOSS2_INTEGRATION,
        "run fboss2 integration tests",
        Fboss2IntegrationTestRunner,
    )
    _register_runner(SUB_CMD_BENCHMARK, "run benchmark tests", BenchmarkTestRunner)

    services_parser = subparsers.add_parser(
        SUB_CMD_SERVICES, help="start or stop FBOSS services without running tests"
    )
    services_parser.set_defaults(log_bundle=False)
    ServiceLifecycleRunner().add_subcommands(services_parser)
    return ap


def _parse_args(argv: list[str] | None = None) -> Namespace:
    """Build the parser, parse ``argv`` (defaults to ``sys.argv``), and validate
    cross-argument constraints. Must run after ``setup_fboss_env()`` because
    building the parser instantiates the runners, which snapshot the environment.
    """
    args = _build_parser().parse_args(argv)

    if getattr(args, "filter", None) and getattr(args, "filter_file", None):
        raise TestRunnerUsageError(
            f"Only one of the {OPT_ARG_FILTER} or {OPT_ARG_FILTER_FILE} can be specified at any time"
        )

    if (
        getattr(args, "profile", None)
        and not getattr(args, "filter_file", None)
        and not getattr(args, "tier", None)
    ):
        raise TestRunnerUsageError(
            f"{OPT_ARG_PROFILE} requires {OPT_ARG_FILTER_FILE} or {OPT_ARG_TIER} to be specified"
        )

    return args


def _runner_action(args: Namespace):
    if runner_action := getattr(args, "runner_action", None):
        return runner_action
    return args.runner.list_tests if args.list_tests else args.runner.run_test


def _get_fboss_root() -> str:
    return os.environ.get("FBOSS") or os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", "..")
    )


def _setup_platform_descriptors() -> None:
    fboss_data = os.environ.get("FBOSS_DATA")
    if not fboss_data:
        print(
            "Warning: FBOSS_DATA is not set; platform descriptors will not be configured.",
            file=sys.stderr,
        )
        return

    source = os.path.abspath(os.path.join(fboss_data, "platform_descriptors"))
    if not os.path.isdir(source):
        print(
            f"Warning: packaged platform descriptors not found at {source}; "
            "platform descriptors will not be configured.",
            file=sys.stderr,
        )
        return

    destination = "/tmp/platform_descriptors"
    if source == destination:
        return
    if os.path.lexists(destination):
        if os.path.isdir(destination) and not os.path.islink(destination):
            shutil.rmtree(destination)
        else:
            os.unlink(destination)
    print(f"Creating platform descriptor symlink: {destination} -> {source}")
    os.symlink(source, destination, target_is_directory=True)


def main() -> None:
    os.chdir(_get_fboss_root())

    # A test-type subcommand is required; derive it from argv before the (slower)
    # env setup so a bare invocation fails fast with help.
    test_type = derive_test_type(sys.argv)
    if test_type is None:
        _build_parser().print_help()
        sys.exit(1)

    setup_fboss_env()
    _setup_platform_descriptors()
    if ("FBOSS_BIN" not in os.environ) or ("FBOSS_LIB" not in os.environ):
        print("FBOSS environment not set. Run `source /opt/fboss/bin/setup_fboss_env'")
        sys.exit(0)

    try:
        args = _parse_args()
        runner_action = _runner_action(args)

        # Log bundling is opt-in via the per-subcommand --log-bundle flag. Without it,
        # run plainly: no per-run dir, no tee, no zip; result CSVs go to the cwd.
        if not args.log_bundle:
            exit_code = runner_action(args)
            sys.exit(exit_code)

        # LogCapture owns the per-run bundle: it creates the dir, tees output, records
        # the command, and on exit collects the logs/CSVs and zips -- even on failure.
        with LogCapture(test_type):
            exit_code = runner_action(args)
        sys.exit(exit_code)
    except TestRunnerUsageError as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(2)
