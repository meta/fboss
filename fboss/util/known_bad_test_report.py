#!/usr/bin/env python3

# Copyright 2004-present Facebook. All Rights Reserved.

# sample usage:
# To run script and create output json file
#       $buck run fbcode//fboss/util:known_bad_test_report -- --query_scuba  --json
# To run script, send email and create task for user
#       $buck run fbcode//fboss/util:known_bad_test_report -- --query_scuba --user  --send_email --create_task
# To run script for all test classes (fboss_qsfp, fboss_agent_ensemble_link_l1, fboss_agent_ensemble_link_l2, sai_agent)
#       $buck run fbcode//fboss/util:known_bad_test_report -- --query_scuba  --test_class all
# To run script for one test class only
#       $buck run fbcode//fboss/util:known_bad_test_report -- --query_scuba  --test_class fboss_qsfp
# in stack diff
# sample usage:
# To run script and create output json file
#        $buck run fbcode//fboss/util:known_bad_test_report -- --query_scuba  --json
# To run script, send email and create task for fboss_agent_push oncall
#        $buck run fbcode//fboss/util:known_bad_test_report -- --query_scuba  --send_email --create_task
#
# To run script to create monthly report of known bad tests
#        $buck run fbcode//fboss/util:known_bad_test_report -- --get_monthly_stats


# pyre-unsafe

import argparse
import getpass
import json
import logging
import os
import re
import sys
import time
import typing as t
from dataclasses import dataclass
from datetime import date, datetime
from typing import Any, Dict, List, Optional

import libfb.py.asyncio.tasks as task_api
from analytics.bamboo import Bamboo as bb
from libfb.py import employee
from libfb.py.asyncio.await_utils import asyncio
from libfb.py.asyncio.sandcastle import AsyncSandcastleClient
from libfb.py.asyncio.tasks import Task, TaskPriority
from libfb.py.mail import async_send_internal_email
from libfb.py.thrift_clients.oncall_thrift_client import OncallThriftClient
from schedulers.chronos.py.scripts.chronos.ji_log_actions import get_chronos_ji_logs


# Configure logging
logger: logging.Logger = logging.getLogger(__name__)
logging.basicConfig(
    level=logging.INFO, format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)

# Constants
DEFAULT_MAX_RECORDS = "200"  # Default number of records returned from scuba
DEFAULT_OUTPUT_FILE = "~/known_bad_tests_data.json"
DEFAULT_CLUSTER = "gp"
PASSED = 1
FAILED = 0
USER_UNIX_ID = "prasoon"
KNOWN_BAD_TEST = "known_bad_tests"
FBOSS_KNOWN_BAD_TESTS = f"fboss_{KNOWN_BAD_TEST}"
DEFAULT_LOG_TYPE = "stderr"

_RE_PLATFORM = re.compile(r"_run_netcastle_([^_]+)")
_RE_VARIANT_FAMILY = re.compile(r"_run_netcastle_{platform}_(.*?)_ensemble_link_")
_RE_RUN_MODE = re.compile(r"_(mono|multi_switch)_test_")
_RE_SDK_RAW = re.compile(
    r"_(?:mono|multi_switch)_test_([^_]+(?:_[^_]+)*)_l2_known_bad_test"
)


@dataclass
class TestConfig:
    """Configuration for a specific test class."""

    name: str
    job_name_regex: str
    oncall: str
    cluster: str = DEFAULT_CLUSTER
    description: str = ""


# Define test configurations
TEST_CONFIGS = {
    "sai_agent": TestConfig(
        name="sai_agent",
        job_name_regex=".*sai_agent_known_bad_test.*",
        oncall="fboss_agent_push",
        description="SAI Agent known bad tests",
    ),
    "agent_ensemble_link_l1": TestConfig(
        name="agent_ensemble_link_l1",
        job_name_regex=".*ensemble_link.*l1_known_bad_test.*",
        oncall="fboss_optics_phy",
        description="Agent Ensemble Link L1 known bad tests",
    ),
    "agent_ensemble_link_l2": TestConfig(
        name="agent_ensemble_link_l2",
        job_name_regex=".*ensemble_link.*l2_known_bad_test.*",
        oncall="fboss_agent_push",
        description="Agent Ensemble Link L2 known bad tests",
    ),
    "fboss_qsfp": TestConfig(
        name="fboss_qsfp",
        job_name_regex=".*qsfp.*known_bad.*",
        oncall="fboss_optics_phy",
        description="Qsfp Hw known bad tests",
    ),
}


class UserAndEmailHandler:
    def get_oncall_info(self, oncall_name: str) -> Any:
        """
        Retrieve the user ID of the current on-call person for a given rotation by short name.

        Args:
            oncall_name (str): The short name of the on-call rotation.

        Returns:
            int: The user ID of the current on-call person.
        """
        with OncallThriftClient() as client:
            oncall = client.getCurrentOncallForRotationByShortName(oncall_name)
            logger.info(f"Current on-call for {oncall}: {oncall.uid}")
            return oncall

    def _get_user_data(
        self, isUser: bool, test_config: TestConfig
    ) -> t.Tuple[str, int, str]:
        """
        Get the current user's name and ID, falling back to on-call information if needed.

        Args:
            isUser: Whether to use the current user instead of oncall
            test_config: Configuration for the test class

        Returns:
            tuple: A tuple containing:
                - str: The username (either the current user or the on-call name)
                - int: The user ID corresponding to the username
                - str: The email of the on-call person
        """
        if isUser:
            user_name = os.environ.get("SUDO_USER", getpass.getuser())
            user_id = employee.unixname_to_uid(user_name)
            return user_name, user_id, ""
        else:
            oncall = UserAndEmailHandler().get_oncall_info(test_config.oncall)
            user_name = test_config.oncall
            logger.info(f"User name: {user_name}, User ID: {oncall.uid}")
            return user_name, oncall.uid, oncall.person_email

    async def send_email(
        self,
        isUser: bool,
        test_config: TestConfig,
        html: str,
        images: Optional[Dict[str, bytes]],
    ) -> None:
        """
        Send an email with the given HTML content and images.

        Args:
            isUser: Whether to use the current user instead of oncall
            test_config: Configuration for the test class
            html (str): The HTML content of the email.
            images (Dict[str, bytes]): A dictionary of image names and their byte content to be included in the email.
        """
        subject = "Known bad tests report. Date: %s" % date.today().strftime("%Y-%m-%d")

        user_name, _user_id, user_email = self._get_user_data(isUser, test_config)

        if user_email:
            to_addrs = [user_email]
        else:
            to_addrs = [("%s@meta.com" % user_name)]

        logger.info(f"Sending email to {to_addrs}")
        await async_send_internal_email(
            sender="noreply@meta.com",
            to=to_addrs,
            subject=subject,
            body=html,
            inline_images=images,
            is_html=True,
        )

    async def create_task(
        self, isUser: bool, test_config: TestConfig, tests: Dict[str, int]
    ) -> Task:
        owner, _user_id, _user_email = self._get_user_data(isUser, test_config)

        return await task_api.create_task(
            creator=USER_UNIX_ID,
            title=f"Known Bad Tests Passing Continuously for a week for {test_config.name}. Please remove them from known bad.",
            description=self.text_format_known_bad_list(tests, test_config.name),
            priority=TaskPriority.HIGH,
            tags=[FBOSS_KNOWN_BAD_TESTS],
            owner=owner,
        )

    def HTTP_format_known_bad_list(
        self,
        tests: List[Dict[str, str]],
        known_bad_tests: Dict[str, int],
        job_name: str,
    ) -> str:
        """Format the test data into a Workplace post."""
        html = (
            f"<h1>Known bad tests passing continuously for a week for {job_name}</h1>"
        )
        html += "<p>Here is the list of known bad tests passing 7 times in a row:</p>"
        html += "<p>Please remove them from known bad. </p>"

        columns = [
            "Test",
            "Platform",
            "Variant",
            "Run Mode",
            "SDK Version",
            "Test Case",
        ]
        html += "<table>"
        html += "<tr>"
        for col in columns:
            html += f"<th>{col}</th>"
        html += "</tr>"
        for row in tests:
            html += "<tr>"
            for col in columns:
                html += f"<td>{row.get(col, '')}</td>"
            html += "</tr>"
        html += "</table>"

        html += "<h1>Total known bad tests</h1>"
        html += "<p>Here is the list of all known bad tests:</p>"
        html += "<table>"
        html += "<tr><th>Test Name</th><th>Count</th></tr>"
        for test_name, status in known_bad_tests.items():
            html += f"<tr><td>{test_name}</td><td>{status}</td></tr>"
        html += "</table>"

        return html

    def text_format_known_bad_list(self, tests: Dict[str, int], job_name: str) -> str:
        """Format the test data into a Workplace post."""
        text = f"Known bad tests passing continuously for a week for {job_name}\n"
        text += "Here is the list of known bad tests passing 7 times in a row:\n"
        text += "Please remove them from known bad.\n"
        text += "Test Name\n"
        for test_name, _status in tests.items():
            text += f"{test_name}\n"

        text += "\nTotal known bad tests\n"
        text += "Here is the list of all known bad tests:\n"
        text += "Test Name\n"
        for test_name, status in tests.items():
            text += f"{test_name}: {status}\n"

        return text

    def parse_known_bad_line(self, line: str) -> Optional[Dict[str, str]]:
        """
        Auto-detect the test format and dispatch to the appropriate parser.

        Supported formats:
        - ensemble_link: contains '::' and 'ensemble_link' in prefix
        - sai_agent: contains '/' separators (e.g. netcastle_test/sai_agent_test/...)
        - QSFP: contains '::' and 'qsfp' in prefix
        """
        if not line:
            return None
        s = line.strip()
        if not s:
            return None

        # unescape underscores from chat formatting
        s = s.replace(r"\_", "_")

        if "/" in s and "sai_agent" in s:
            return self._parse_sai_agent_line(s)
        elif "::" in s and "qsfp" in s:
            return self._parse_qsfp_line(s)
        elif "::" in s:
            return self._parse_ensemble_link_line(s)
        return None

    def _parse_ensemble_link_line(self, s: str) -> Optional[Dict[str, str]]:
        """
        Parse an ensemble_link line like:
        netcastle_agent_ensemble_link_stress_run_netcastle_wedge400_ensemble_link_mono_test_preprod2trunk2preprod_10_2_0_10_2_0_l2_known_bad_test::cold_boot.TestClass.TestMethod
        """
        prefix, testpart = s.split("::", 1)

        plat_m = _RE_PLATFORM.search(prefix)
        platform = plat_m.group(1) if plat_m else ""

        rm_m = _RE_RUN_MODE.search(prefix)
        run_mode = rm_m.group(1) if rm_m else ""
        run_mode = (
            "Mono"
            if run_mode == "mono"
            else ("Multi" if run_mode == "multi_switch" else "")
        )

        # variant family: platform_(variant)_ensemble_link ; empty if none exists
        vf_re = re.compile(
            _RE_VARIANT_FAMILY.pattern.format(platform=re.escape(platform))
        )
        vf_m = vf_re.search(prefix)
        variant_family = vf_m.group(1) if vf_m else ""

        # sdk raw: everything after _<mono|multi_switch>_test_ up to _l2_known_bad_test
        sdk_m = _RE_SDK_RAW.search(prefix)
        sdk_version = sdk_m.group(1) if sdk_m else ""

        return {
            "Test": "ensemble_link",
            "Platform": platform,
            "Variant": variant_family,
            "Run Mode": run_mode,
            "SDK Version": sdk_version,
            "Test Case": testpart,
        }

    def _parse_sai_agent_line(self, s: str) -> Optional[Dict[str, str]]:
        """
        Parse a sai_agent line like:
        netcastle_test/sai_agent_test/brcm/10.2.0.0_odp/10.2.0.0_odp/tomahawk3/mono - ECMPFullTrunkHalf4X3WideTrunksV4MplsFrontPanelTraffic (cold_boot.AgentTrunkLoadBalancerTest)
        """
        # Split into path part and test part by ' - '
        parts = s.split(" - ", 1)
        if len(parts) != 2:
            return None

        path_part, test_info = parts
        segments = path_part.split("/")
        # Expected: [netcastle_test, sai_agent_test, asic_family, sdk, sdk, asic, run_mode]
        if len(segments) < 7:
            return None

        asic_family = segments[2]
        sdk_version = segments[3]
        asic = segments[5]
        run_mode = segments[6]
        run_mode = (
            "Mono"
            if run_mode == "mono"
            else ("Multi" if run_mode == "multi_switch" else run_mode)
        )

        # Parse test_method and (boot_type.TestClass) from test_info
        # e.g. "ECMPFull... (cold_boot.AgentTrunkLoadBalancerTest)"
        test_match = re.match(r"(.+?)\s+\((.+)\)", test_info)
        if test_match:
            test_method = test_match.group(1)
            test_class = test_match.group(2)
            test_case = f"{test_class}.{test_method}"
        else:
            test_case = test_info

        return {
            "Test": "sai_agent",
            "Platform": asic_family,
            "Variant": asic,
            "Run Mode": run_mode,
            "SDK Version": sdk_version,
            "Test Case": test_case,
        }

    def _parse_qsfp_line(self, s: str) -> Optional[Dict[str, str]]:
        """
        Parse a QSFP line like:
        netcastle_fboss_qsfp_stress_run_netcastle_fboss_qsfp_test_fuji_8x16q_barchetta2_5_2_barchetta2_5_2_known_bad_test::cold_boot.HwPortPrbsTest_LINE_PAM4_true.SetPrbs
        """
        if "::" not in s:
            return None

        prefix, test_case = s.split("::", 1)

        # Strip known prefix and suffix
        qsfp_prefix = "netcastle_fboss_qsfp_stress_run_netcastle_fboss_qsfp_test_"
        qsfp_suffix = "_known_bad_test"
        if not prefix.startswith(qsfp_prefix) or not prefix.endswith(qsfp_suffix):
            return None

        middle = prefix[len(qsfp_prefix) : -len(qsfp_suffix)]
        # middle is e.g. "fuji_8x16q_barchetta2_5_2_barchetta2_5_2"
        # SDK appears twice consecutively; find the repeated suffix
        tokens = middle.split("_")
        n = len(tokens)

        platform = ""
        variant = ""
        sdk_version = ""

        for k in range(1, (n // 2) + 1):
            if tokens[n - 2 * k : n - k] == tokens[n - k : n]:
                sdk_version = "_".join(tokens[n - 2 * k : n - k])
                platform_variant_tokens = tokens[: n - 2 * k]
                if platform_variant_tokens:
                    platform = platform_variant_tokens[0]
                    variant = "_".join(platform_variant_tokens[1:])
                break

        return {
            "Test": "QSFP",
            "Platform": platform,
            "Variant": variant,
            "SDK Version": sdk_version,
            "Test Case": test_case,
        }

    def parse_known_bad(self, tests: t.Dict[str, int]) -> t.List[t.Dict[str, str]]:
        """
        Parse many known-bad test identifiers from a dict of tests.
        Args:
            tests: Dict whose keys are known-bad test identifiers (one per line in the old format).
        Returns:
            list[dict]: Parsed rows for each test key.
        """
        if not tests:
            return []

        rows: t.List[t.Dict[str, str]] = []
        for test_key in tests.keys():
            d = self.parse_known_bad_line(test_key)
            if d:
                rows.append(d)
        return rows


class ScubaQueryBuilder:
    """Class to build and execute Scuba queries for Chronos jobs."""

    def build_query_for_test_results(
        self,
        job_name_regex: str,
        limit: str = DEFAULT_MAX_RECORDS,
        consider_results_since: Optional[int] = None,
        status: Optional[bool] = None,
    ) -> str:
        """Build a Scuba query for Chronos jobs."""
        current_time = int(time.time())
        if not consider_results_since:
            consider_results_since = current_time - 60 * 60 * 24 * 7  # Last 7 days

        sql_query = f"""
            SELECT
                COUNT(1) AS `count`,
                `Sandcastle Job Alias` as `sandcastle_alias`,
                `Test Case` as `test_case`,
                `Status` AS `status`
            FROM `fboss_testing`
            WHERE
                {consider_results_since} <= `time`
                AND `time` <= {current_time}
                AND `Sandcastle Job Alias` RLIKE '{job_name_regex}'
                AND (`Purpose` = 'STRESS_RUN')
                AND (`Status` = 'PASSED')
            GROUP BY
                `Sandcastle Job Alias`,
                `Test Case`,
                `Status`
            ORDER BY
                `count` DESC
            """

        return sql_query

    def build_query_for_chronos_job_id(
        self,
        job_name_regex: str,
        cluster: str = DEFAULT_CLUSTER,
        excluded_reasons: Optional[List[str]] = None,
        limit: str = DEFAULT_MAX_RECORDS,
        queue: Optional[str] = None,
        consider_results_since: Optional[int] = None,
        query_time: Optional[int] = None,
    ) -> str:
        """Build a Scuba query for Chronos jobs."""
        if not query_time:
            query_time = int(time.time())
        if not consider_results_since:
            consider_results_since = query_time - 60 * 60 * 24  # Last 24 hours

        sql_base = f"""
            SELECT
                IF(exit_code != '0', "Fail", "Success"),
                cluster_name + '/' + jobname,
                `job_instance_id`,
                `jobname`,
                `jobId`,
                `jobVersion`
            FROM `chronos_job_instance_states`
            WHERE
                {consider_results_since} <= `time`
                AND `time` <= {query_time}
                AND (cluster_name + '/' + jobname) RLIKE '{job_name_regex}'
            """

        if cluster:
            sql_base += f" AND ((`cluster_name`) IN ('{cluster}'))"

        if excluded_reasons:
            excluded_reasons_str = "', '".join(excluded_reasons)
            sql_base += f" AND NOT ((`reason`) IN ('{excluded_reasons_str}'))"

        if queue:
            sql_base += f" AND ((`queue`) IN ('{queue}'))"

        return sql_base

    def execute_query(self, sql_query: str) -> Optional[Any]:
        """Execute a Scuba query and return the results."""
        try:
            df = bb.query_scuba_nullable(sql_query)
            return df
        except Exception as e:
            logger.error(f"Error querying Scuba: {e}")
            return None


class SandcastleClient:
    """Class to interact with Sandcastle jobs."""

    async def get_logs(self, job_id: int) -> Dict[str, str]:
        """Get logs from a Sandcastle job."""
        try:
            async with AsyncSandcastleClient() as client:
                job_logs_dict = {}

                job_spec = await client.get_spec(job_id)

                logger.info(f"Processing Sandcastle job: {job_id}")

                # Extract steps based on job type
                if "steps" in job_spec["args"]:
                    steps = [s["name"] for s in job_spec["args"]["steps"]]
                elif (
                    job_spec["command"] == "SandcastleSkycastleCommand"
                    and "steps" not in job_spec["args"]
                ):
                    # For SandcastleSkycastleCommand without steps, extract from logs
                    workflow_log = await client.get_log(job_id)
                    pattern = r'Starting step "(.*?)"'
                    steps = [
                        match
                        for line in workflow_log
                        for match in re.findall(pattern, line)
                    ]
                    logger.warning(
                        f"Skycastle {job_id} unexpectedly has no steps created"
                    )
                else:
                    raise ValueError(f"Unsupported command: {job_spec['command']}")

                logger.info(f"Found {len(steps)} steps in Sandcastle job {job_id}")
                logger.info("Getting logs for step: Run tests")
                logs_list = await client.get_log(job_id, step_name="Run tests")
                job_logs_dict["Run tests"] = "\n".join(logs_list)

                return job_logs_dict

        except Exception as e:
            logger.error(f"Error retrieving Sandcastle logs for job {job_id}: {e}")
            raise

    def _extract_content_sandcastle_logs(self, logs: str) -> int:
        """Extract content from Sandcastle logs."""
        # Extract content from logs
        tests = int(0)
        # Use regex to find the pattern "Enqueued <number> tests"
        match = re.search(r"multiprocess_test_orchestrator: Enqueued (\d+) tests", logs)
        if match:
            tests = int(match.group(1))

        return tests


def query_monthly_job_data(args, test_config: TestConfig) -> Dict[str, Dict[str, int]]:
    """Query and process job data for monthly statistics comparison.

    Args:
        args: Command line arguments containing date information
        test_config: Configuration for the test class

    Returns:
        Dictionary containing job data for both time periods:
        {
            "today": {job_name: job_count, ...},
            "current_month_first": {job_name: job_count, ...}
        }
    """
    logger.info(f"Querying monthly job data for {test_config.job_name_regex}")

    # Calculate today and 1st of current month
    # Use --date if provided, otherwise use today's date
    if args.date:
        base_date = datetime.strptime(args.date, "%Y-%m-%d").date()
    else:
        base_date = date.today()

    current_month_first = datetime(base_date.year, base_date.month, 2)
    today_date = datetime(base_date.year, base_date.month, base_date.day)

    dates_to_query = [
        ("today", today_date),
        ("current_month_first", current_month_first),
    ]
    print("today:", today_date.strftime("%Y-%m-%d"))
    print("current_month_first:", current_month_first.strftime("%Y-%m-%d"))

    # Store results for both time periods
    monthly_results = {}

    for month_label, query_date in dates_to_query:
        logger.info(f"Processing {month_label}: {query_date.strftime('%Y-%m-%d')}")

        start_timestamp = int(time.mktime(query_date.timetuple()))

        sql_query = ScubaQueryBuilder().build_query_for_chronos_job_id(
            job_name_regex=test_config.job_name_regex,
            cluster=test_config.cluster,
            query_time=start_timestamp,
        )
        df = ScubaQueryBuilder().execute_query(sql_query)
        if df is None:
            logger.error(f"Error: Could not execute Scuba query for {month_label}")
            continue

        print(f"Total jobs running for {month_label}: {len(df)}")

        known_bad_jobs = {}
        for _i, (job_id, job_name, instance_id) in enumerate(
            zip(df["jobId"], df["jobname"], df["job_instance_id"])
        ):
            logger.info(
                f"job id: {job_id}, job name: {job_name} instance id: {instance_id}"
            )
            known_bad_jobs[job_name] = asyncio.run(
                ChronosJobExtractor().process_instance(int(instance_id), job_name)
            )

        # Store results for this time period
        monthly_results[month_label] = known_bad_jobs

        print(f"\nResults for {month_label} ({query_date.strftime('%Y-%m-%d')}):")
        for job_name, job_count in known_bad_jobs.items():
            logger.info(f"Job name: {job_name}, Known bad count: {job_count}")
        print("-" * 50)

    return monthly_results


def analyze_monthly_improvements(monthly_results: Dict[str, Dict[str, int]]) -> None:
    """Analyze and display percentage improvements between monthly job data.

    Args:
        monthly_results: Dictionary containing job data for both time periods
    """
    if "current_month_first" not in monthly_results or "today" not in monthly_results:
        logger.warning(
            "Could not calculate percentage improvement - missing data for one or both time periods"
        )
        return

    print("\n" + "=" * 80)
    print("PERCENTAGE IMPROVEMENT (DECREASE) ANALYSIS")
    print("=" * 80)

    month_start_jobs = monthly_results["current_month_first"]
    today_jobs = monthly_results["today"]

    # Get all unique job names from both time periods
    all_job_names = set(month_start_jobs.keys()) | set(today_jobs.keys())

    print(f"{'Job Name':<50} {'Month Start':<12} {'Today':<10} {'% Decrease':<12}")
    print("-" * 84)

    for job_name in sorted(all_job_names):
        month_start_count = month_start_jobs.get(job_name, 0)
        today_count = today_jobs.get(job_name, 0)

        if month_start_count > 0:
            # Calculate percentage decrease: ((month_start - today) / month_start) * 100
            percentage_decrease = (
                (month_start_count - today_count) / month_start_count
            ) * 100
            status = (
                "IMPROVEMENT"
                if percentage_decrease > 0
                else "REGRESSION"
                if percentage_decrease < 0
                else "NO CHANGE"
            )

            print(
                f"{job_name:<50} {month_start_count:<12} {today_count:<10} {percentage_decrease:+7.1f}% ({status})"
            )
        elif today_count > 0:
            # New job appeared since month start
            print(
                f"{job_name:<50} {month_start_count:<12} {today_count:<10} {'NEW JOB':<12}"
            )
        else:
            # Job had 0 counts in both periods
            print(
                f"{job_name:<50} {month_start_count:<12} {today_count:<10} {'NO DATA':<12}"
            )

    print("-" * 84)

    # Summary statistics
    total_month_start = sum(month_start_jobs.values())
    total_today = sum(today_jobs.values())

    if total_month_start > 0:
        overall_percentage = (
            (total_month_start - total_today) / total_month_start
        ) * 100
        print(
            f"{'OVERALL TOTALS':<50} {total_month_start:<12} {total_today:<10} {overall_percentage:+7.1f}%"
        )
    else:
        print(
            f"{'OVERALL TOTALS':<50} {total_month_start:<12} {total_today:<10} {'N/A':<12}"
        )


def get_monthly_stats(args, test_config: TestConfig) -> None:
    """Get monthly stats comparing today vs 1st of month.

    Args:
        args: Command line arguments
        test_config: Configuration for the test class
    """
    logger.info(f"Getting monthly stats for {test_config.job_name_regex}")

    # Query job data for both time periods
    monthly_results = query_monthly_job_data(args, test_config)

    # Analyze and display percentage improvements
    analyze_monthly_improvements(monthly_results)


class ChronosJobExtractor:
    """Main class to extract data from Chronos jobs."""

    def __init__(self) -> None:
        """Initialize the ChronosJobExtractor class."""

    def get_logs(
        self,
        job_instance_id: int,
        log_type: str = DEFAULT_LOG_TYPE,
        cluster: str = DEFAULT_CLUSTER,
    ) -> str:
        """Get logs from a Chronos job instance."""
        try:
            return get_chronos_ji_logs(
                job_instance_id=job_instance_id,
                cluster=cluster,
                log_type=log_type,
                head=100,
                tail=None,
                suppress_logs=True,
            )
        except Exception as e:
            if "Cannot find the job instance" in str(e):
                error_msg = [
                    f"Error: Job instance {job_instance_id} not found.",
                    "This could be because:",
                    "  1. The job ID is incorrect",
                    "  2. The job has expired and its logs are no longer available",
                    f"  3. The job belongs to a different cluster (default is '{cluster}')",
                    "\nPlease verify the job ID and try again.",
                    "For more information, see: https://www.internalfb.com/intern/wiki/ChronosGuide/logging-and-datasets/Chronos_Unified_Log_API/",
                ]
                logger.error("\n".join(error_msg))
                return f"ERROR: Job instance {job_instance_id} not found."
            else:
                logger.error(f"Error retrieving Chronos logs: {e}")
                raise

    def _get_sandcastle_jobs_from_instance(
        self, instance_id: int, cluster: str = DEFAULT_CLUSTER
    ) -> List[str]:
        """Get Sandcastle job IDs from a Chronos job instance."""
        try:
            instance_logs = self.get_logs(instance_id, cluster=cluster)

            # Extract Sandcastle job IDs from logs
            sandcastle_ids = []
            for line in instance_logs.split("\n"):
                if "Sandcastle job created:" in line:
                    match = re.search(
                        r"https://www\.internalfb\.com/intern/sandcastle/job/(\d+)/",
                        line,
                    )
                    if match:
                        sandcastle_ids.append(match.group(1))

            return sandcastle_ids
        except Exception as e:
            logger.error(
                f"Error getting Sandcastle jobs from instance {instance_id}: {e}"
            )
            return []

    async def process_instance(
        self,
        instance_id: int,
        job_name: str = "default_job_name",
        cluster: str = DEFAULT_CLUSTER,
        include_sandcastle: bool = True,
    ) -> int:
        """Process a Chronos job instance and return its data."""
        try:
            tests = 0
            # Get Sandcastle jobs if requested
            if include_sandcastle:
                sandcastle_ids = self._get_sandcastle_jobs_from_instance(
                    instance_id, cluster
                )

                for sc_id in sandcastle_ids:
                    try:
                        sc_logs = await SandcastleClient().get_logs(int(sc_id))

                        # Process the "Run tests" step if it exists
                        if "Run tests" in sc_logs:
                            logger.info(
                                f"Found 'Run tests' step in Sandcastle job {sc_id}"
                            )
                            tests = SandcastleClient()._extract_content_sandcastle_logs(
                                sc_logs["Run tests"]
                            )
                            logger.info(f"Enqueued {tests} tests")
                        else:
                            logger.info(
                                f"No 'Run tests' step found in Sandcastle job {sc_id}"
                            )
                            tests = 0
                    except Exception as e:
                        logger.error(f"Error processing Sandcastle job {sc_id}: {e}")

            return tests

        except Exception as e:
            logger.error(f"Error processing instance {instance_id}: {e}")
            return 0  # Changed return type to int


def create_report_task(
    args: argparse.Namespace,
    test_config: TestConfig,
    tests: Dict[str, str],
    error_code: int,
) -> int:
    logger.info("Creating task for oncall...")
    try:
        task = asyncio.run(
            # pyrefly: ignore [bad-argument-type]
            UserAndEmailHandler().create_task(args.user, test_config, tests)
        )
    except Exception:
        logger.exception("Failed to create task for %s", test_config.name)
        return 1
    logger.info(f"Task created successfully: T{task.task_number}")
    return error_code


def main() -> Optional[int]:
    """Main function to parse command line arguments and run the script."""
    parser = argparse.ArgumentParser(
        description="Get data from Scuba for known bad tests"
    )

    parser.add_argument(
        "--query_scuba",
        action="store_true",
        help="Query Scuba for test results",
    )

    parser.add_argument(
        "--debug",
        action="store_true",
        help="Print debug messages",
    )

    parser.add_argument(
        "--user",
        action="store_true",
        help="Send email to user instead of oncall",
    )

    parser.add_argument(
        "--send_email",
        action="store_true",
        help="Send email to user or oncall",
    )

    parser.add_argument(
        "--create_task",
        action="store_true",
        help="Create task for user or oncall",
    )

    parser.add_argument(
        "--json",
        action="store_true",
        help="Create json file with test data",
    )

    parser.add_argument(
        "--get_monthly_stats",
        action="store_true",
        help="Get monthly stats for known bad tests",
    )

    parser.add_argument(
        "--date",
        type=str,
        help="Time to query for. Format: YYYY-MM-DD. Default is today's date.",
    )

    parser.add_argument(
        "--test_class",
        type=str,
        default="all",
        choices=list(TEST_CONFIGS.keys()) + ["all"],
        help=f"Test class to query. Options: {', '.join(list(TEST_CONFIGS.keys()) + ['all'])}. Default: {'all'}",
    )

    args = parser.parse_args()

    test_configs = []
    if args.test_class == "all":
        test_configs = TEST_CONFIGS.values()
    else:
        test_configs = [TEST_CONFIGS[args.test_class]]

    error_code = 0
    for test_config in test_configs:
        # Get the test configuration based on command line argument
        logger.info(f"Using test configuration: {test_config.job_name_regex}")

        if args.query_scuba:
            # Query Scuba for test result
            logger.info("Querying Scuba for test results...")
            sql_query = ScubaQueryBuilder().build_query_for_test_results(
                job_name_regex=test_config.job_name_regex
            )
            df = ScubaQueryBuilder().execute_query(sql_query)

            if df is None:
                logger.error("Error: Could not execute Scuba query")
                error_code = 1
                continue
            logger.info(f"Scuba query returned {len(df)} rows")
            tests = {}

            for _, row in df.iterrows():
                if row["status"] == "PASSED" and row["count"] == 7:
                    test_key = f"{row['sandcastle_alias']}::{row['test_case']}"
                    if test_key not in tests:
                        tests[test_key] = row["status"]

            logger.info(f"Found {len(tests)} known bad tests passing 7 times in a row")

            # query scuba for job ids
            sql_query = ScubaQueryBuilder().build_query_for_chronos_job_id(
                job_name_regex=test_config.job_name_regex,
                cluster=test_config.cluster,
            )
            df = ScubaQueryBuilder().execute_query(sql_query)
            if df is None:
                error_code = 1

            known_bad_jobs = {}
            for _i, (job_id, job_name, instance_id) in enumerate(
                # pyrefly: ignore [unsupported-operation]
                zip(df["jobId"], df["jobname"], df["job_instance_id"])
            ):
                logger.info(
                    f"job id: {job_id}, job name: {job_name} instance id: {instance_id}"
                )
                known_bad_jobs[job_name] = asyncio.run(
                    ChronosJobExtractor().process_instance(int(instance_id), job_name)
                )

            for job_name, job_count in known_bad_jobs.items():
                logger.info(f"Job name: {job_name}, Known bad count: {job_count}")

            # Write test data to a separate JSON file
            if args.json:
                with open(os.path.expanduser(DEFAULT_OUTPUT_FILE), "w") as f:
                    test_structure = {
                        "passing_tests": tests,
                        "job_test_count": known_bad_jobs,
                    }
                    json.dump(test_structure, f, indent=2, default=str)
                logger.info(f"Results written to {DEFAULT_OUTPUT_FILE}")

            # send email to user or oncall
            if args.send_email:
                logger.info("Sending email...")
                html = UserAndEmailHandler().HTTP_format_known_bad_list(
                    # pyrefly: ignore [bad-argument-type]
                    UserAndEmailHandler().parse_known_bad(tests),
                    known_bad_jobs,
                    test_config.name,
                )

                logger.info("html generated successfully for email body")
                images = {}
                asyncio.run(
                    UserAndEmailHandler().send_email(
                        args.user, test_config, html, images
                    )
                )

                logger.info("Email sent successfully")
            else:
                logger.info("No email sent")

                # create task for user or oncall
            if args.create_task:
                error_code = create_report_task(args, test_config, tests, error_code)
            else:
                logger.info("No task created")

            # # parse tests
            # data = UserAndEmailHandler().parse_known_bad(tests)
            # logger.info(f"Known bad Table: {data}")

        # Get monthly stats comparing today vs 1st of month.
        if args.get_monthly_stats:
            get_monthly_stats(args, test_config)

    return error_code


if __name__ == "__main__":
    sys.exit(main())
