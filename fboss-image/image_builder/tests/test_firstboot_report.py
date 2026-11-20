#!/usr/bin/env fbpython

import configparser
import importlib.resources
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from parameterized import parameterized


REPORT_URL_CASES = [
    ("legacy", "https://events.example/sendEvent", True),
    ("origin", "https://events.example:443", True),
    ("custom_path", "https://events.example/provisioning/first-boot", True),
    ("query", "https://events.example/report?tenant=my%20lab&source=kiwi", True),
    ("ipv6", "https://[2001:db8::1]:8443/report", True),
    ("http", "http://events.example/report", False),
    ("http_query", "http://events.example/report?tenant=horizon", False),
    ("ftp", "ftp://events.example/report", False),
    ("empty", "", False),
    ("empty_authority", "https://", False),
    ("missing_authority", "https:///report", False),
    ("query_without_authority", "https://?tenant=horizon", False),
    ("space", "https://events.example/report?tenant=my lab", False),
    ("tab", "https://events.example/re\tport", False),
    ("control", "https://events.example/re\x01port", False),
]


REPORTER_CURL_STUBS = """
curl() {
    printf '%s\\n' "$@" > "$REPORT_TEST_LOG"
    printf '%s' "${REPORT_TEST_HTTP_STATUS:-200}"
}
export -f curl
exec bash "$REPORT_SCRIPT"
"""


TRANSFER_HOOK_STUBS = """
getarg() {
    case "$1" in
        fboss.distro.report.asset_id=) echo "$REPORT_TEST_ASSET_ID" ;;
        fboss.distro.report.url=) echo "$REPORT_TEST_EVENT_URL" ;;
    esac
}
warn() { printf 'WARN: %s\\n' "$*" >&2; }
source "$REPORT_SCRIPT"
printf 'next-hook\\n'
"""


class FirstbootReportTest(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempdir.cleanup)
        self.root = Path(self.tempdir.name)
        self.sources = importlib.resources.files("fboss_image.image_builder").joinpath(
            "firstboot"
        )

    def test_hook_installed_only_for_kiwi_dump(self):
        module = self.sources.joinpath("module-setup.sh")
        for modules, expected in [
            ("", 1),
            ("kiwi-dump-reboot", 1),
            ("kiwi-dump-extra", 1),
            ("kiwi-dump", 0),
            ("kiwi-dump kiwi-dump-reboot", 0),
            ("network-manager kiwi-dump-reboot kiwi-dump", 0),
        ]:
            result = subprocess.run(
                ["bash", "-c", 'source "$moddir"; check'],
                env={**os.environ, "moddir": str(module), "add_dracutmodules": modules},
                check=False,
            )
            self.assertEqual(result.returncode, expected)
        result = subprocess.run(
            [
                "bash",
                "-c",
                'inst_hook() { printf "%s %s %s" "$1" "$2" "${3##*/}"; }; source "$moddir/module-setup.sh"; install',
            ],
            env={**os.environ, "moddir": str(module.parent)},
            capture_output=True,
            text=True,
            check=True,
        )
        self.assertEqual(result.stdout, "pre-mount 35 transfer-report-args.sh")

    def test_hook_needs_no_extra_installer_commands(self):
        module = self.sources.joinpath("module-setup.sh")
        result = subprocess.run(
            [
                "bash",
                "-ec",
                'inst_multiple() { echo unexpected-command >&2; return 1; }; inst_hook() { :; }; source "$moddir/module-setup.sh"; install',
            ],
            env={**os.environ, "moddir": str(module.parent)},
            capture_output=True,
            text=True,
            check=True,
        )
        self.assertEqual(result.stdout, "")
        self.assertEqual(result.stderr, "")

    def run_transfer(
        self, asset_id: str, event_url: str
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["bash", "-euc", TRANSFER_HOOK_STUBS],
            env={
                **os.environ,
                "REPORT_SCRIPT": str(self.sources.joinpath("transfer-report-args.sh")),
                "FBOSS_REPORT_BOOTOPTIONS": str(self.root / "bootoptions"),
                "REPORT_TEST_ASSET_ID": asset_id,
                "REPORT_TEST_EVENT_URL": event_url,
            },
            capture_output=True,
            text=True,
            timeout=10,
        )

    @parameterized.expand(
        [
            ("absent", "", "", False),
            ("valid", "123456", "https://events.example/report?tenant=my%20lab", True),
            ("partial", "123456", "", False),
            ("opaque_id", "ABC123", "https://events.example/report", False),
            ("http", "123456", "http://events.example/report", False),
            ("space", "123456", "https://events.example/my report", False),
            ("control", "123456", "https://events.example/re\x01port", False),
            ("quote", "123456", 'https://events.example/re"port', False),
            ("backslash", "123456", "https://events.example/re\\port", False),
        ]
    )
    def test_hook_transfers_only_safe_reporting_arguments(
        self, name: str, asset_id: str, event_url: str, transferred: bool
    ) -> None:
        boot_options = self.root / "bootoptions"
        original = "root=UUID=installed console=ttyS0\n"
        boot_options.write_text(original)
        result = self.run_transfer(asset_id, event_url)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "next-hook\n")
        if (asset_id or event_url) and not transferred:
            self.assertIn("Invalid first-boot reporting arguments", result.stderr)
        self.assertEqual(
            "WARN:" in result.stderr, bool(asset_id or event_url) and not transferred
        )
        expected = original
        if transferred:
            expected += f" fboss.distro.report.asset_id={asset_id} fboss.distro.report.url={event_url}\n"
        self.assertEqual(boot_options.read_text(), expected)
        self.assertFalse((self.root / "config").exists())

    def test_missing_boot_options_does_not_stop_installation(self) -> None:
        result = self.run_transfer("123456", "https://events.example/report")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("WARN:", result.stderr)
        self.assertEqual(result.stdout, "next-hook\n")
        self.assertFalse((self.root / "bootoptions").exists())

    def run_reporter(
        self, http_status: str = "200"
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["bash", "-c", REPORTER_CURL_STUBS],
            env={
                **os.environ,
                "REPORT_SCRIPT": str(self.sources.joinpath("reporter")),
                "FBOSS_REPORT_MARKER": str(self.root / "reported"),
                "FBOSS_REPORT_CMDLINE": str(self.root / "cmdline"),
                "REPORT_TEST_LOG": str(self.root / "request"),
                "REPORT_TEST_HTTP_STATUS": http_status,
            },
            capture_output=True,
            text=True,
            timeout=10,
        )

    @parameterized.expand(
        [
            ("origin", "https://events.example:443"),
            ("query", "https://events.example/report?tenant=my%20lab&source=kiwi"),
            ("ipv6", "https://[2001:db8::1]:8443/report"),
        ]
    )
    def test_transferred_arguments_reach_the_installed_reporter(
        self, name: str, event_url: str
    ) -> None:
        boot_options = self.root / "bootoptions"
        boot_options.write_text("root=UUID=installed console=ttyS0\n")
        transfer = self.run_transfer("123456", event_url)
        self.assertEqual(transfer.returncode, 0, transfer.stderr)
        (self.root / "cmdline").write_text(boot_options.read_text().replace("\n", " "))
        result = self.run_reporter()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.root / "config").exists())
        self.assertTrue((self.root / "reported").exists())
        self.assertEqual(
            (self.root / "request").read_text().splitlines()[-1], event_url
        )

    def test_failed_delivery_does_not_report_on_a_later_boot(self) -> None:
        event_url = "https://events.example/report"
        (self.root / "cmdline").write_text(
            f"fboss.distro.report.asset_id=123456 fboss.distro.report.url={event_url}"
        )
        failed = self.run_reporter(http_status="500")
        self.assertEqual(failed.returncode, 1, failed.stderr)
        self.assertFalse((self.root / "config").exists())
        self.assertFalse((self.root / "reported").exists())
        (self.root / "request").unlink()
        (self.root / "cmdline").write_text("root=UUID=installed")
        retried = self.run_reporter()
        self.assertEqual(retried.returncode, 0, retried.stderr)
        self.assertFalse((self.root / "reported").exists())
        self.assertFalse((self.root / "request").exists())

    def test_unconfigured_boot_does_not_report(self) -> None:
        (self.root / "cmdline").write_text("root=UUID=installed console=ttyS0")
        result = self.run_reporter()
        self.assertEqual(result.returncode, 0, result.stderr)
        for filename in ["config", "request", "reported"]:
            self.assertFalse((self.root / filename).exists())

    def test_legacy_config_is_ignored_without_boot_arguments(self) -> None:
        (self.root / "config").write_text(
            "ASSET_ID=123456\nEVENT_URL=https://events.example/report\n"
        )
        (self.root / "cmdline").write_text("root=UUID=installed")
        result = self.run_reporter()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.root / "reported").exists())
        self.assertFalse((self.root / "request").exists())

    @parameterized.expand(
        [
            ("asset_only", "fboss.distro.report.asset_id=123456"),
            ("url_only", "fboss.distro.report.url=https://events.example/report"),
            (
                "invalid_asset",
                "fboss.distro.report.asset_id=ABC123 fboss.distro.report.url=https://events.example/report",
            ),
            (
                "http",
                "fboss.distro.report.asset_id=123456 fboss.distro.report.url=http://events.example/report",
            ),
        ]
    )
    def test_invalid_boot_arguments_do_not_report(
        self, name: str, cmdline: str
    ) -> None:
        (self.root / "cmdline").write_text(cmdline)
        result = self.run_reporter()
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("Invalid first-boot reporting arguments", result.stderr)
        for filename in ["config", "request", "reported"]:
            self.assertFalse((self.root / filename).exists())

    @parameterized.expand(REPORT_URL_CASES)
    def test_reporter_url_validation(
        self, name: str, event_url: str, valid: bool
    ) -> None:
        cmdline = self.root / "cmdline"
        marker = self.root / "reported"
        log = self.root / "request"
        url_arg = (
            f'"{event_url}"' if any(char.isspace() for char in event_url) else event_url
        )
        cmdline.write_text(
            f"fboss.distro.report.asset_id=123456 fboss.distro.report.url={url_arg}"
        )
        result = subprocess.run(
            ["bash", "-c", REPORTER_CURL_STUBS],
            env={
                **os.environ,
                "REPORT_SCRIPT": str(self.sources.joinpath("reporter")),
                "FBOSS_REPORT_CMDLINE": str(cmdline),
                "FBOSS_REPORT_MARKER": str(marker),
                "REPORT_TEST_LOG": str(log),
            },
            capture_output=True,
            text=True,
            timeout=10,
        )
        self.assertEqual(result.returncode, 0 if valid else 1, result.stderr)
        self.assertEqual(marker.exists(), valid)
        self.assertEqual(log.exists(), valid)
        if valid:
            self.assertEqual(log.read_text().splitlines()[-1], event_url)
            self.assertIn("assetID=123456", log.read_text())

    def test_reporter_retries_and_only_reports_once(self):
        script = self.sources.joinpath("reporter")
        cmdline = self.root / "cmdline"
        marker = self.root / "reported"
        log = self.root / "request"
        cmdline.write_text(
            "fboss.distro.report.asset_id=123456 fboss.distro.report.url=https://events.example/sendEvent"
        )
        bin_dir = self.root / "bin"
        bin_dir.mkdir()
        curl = bin_dir / "curl"
        curl.write_text(
            '#!/bin/bash\nprintf "%s\\n" "$@" > "$REPORT_TEST_LOG"\nprintf "200"\nexit "${REPORT_TEST_EXIT:-0}"\n'
        )
        curl.chmod(0o755)
        environment = {
            **os.environ,
            "PATH": f"{bin_dir}:{os.environ['PATH']}",
            "FBOSS_REPORT_CMDLINE": str(cmdline),
            "FBOSS_REPORT_MARKER": str(marker),
            "REPORT_TEST_LOG": str(log),
        }
        result = subprocess.run(
            ["bash", str(script)], env={**environment, "REPORT_TEST_EXIT": "22"}
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(marker.exists())
        subprocess.run(["bash", str(script)], env=environment, check=True)
        self.assertTrue(marker.exists())
        self.assertIn(
            "--retry\n2\n--retry-all-errors\n--retry-delay\n15\n", log.read_text()
        )
        self.assertIn("--max-time\n20\n", log.read_text())
        self.assertIn("name=FBOSS_DISTRO.FIRST_BOOT", log.read_text())
        self.assertIn("assetID=123456", log.read_text())
        log.unlink()
        subprocess.run(["bash", str(script)], env=environment, check=True)
        self.assertFalse(log.exists())

    @parameterized.expand(
        [
            ("ok", "200", True),
            ("created", "201", True),
            ("no_content", "204", True),
            ("informational", "100", False),
            ("moved", "301", False),
            ("found", "302", False),
            ("temporary_redirect", "307", False),
            ("permanent_redirect", "308", False),
            ("client_error", "400", False),
            ("server_error", "500", False),
            ("no_response", "000", False),
        ]
    )
    def test_reporter_requires_2xx_response(
        self, name: str, http_status: str, accepted: bool
    ) -> None:
        cmdline = self.root / "cmdline"
        marker = self.root / "reported"
        log = self.root / "request"
        cmdline.write_text(
            "fboss.distro.report.asset_id=123456 fboss.distro.report.url=https://events.example/report"
        )
        result = subprocess.run(
            ["bash", "-c", REPORTER_CURL_STUBS],
            env={
                **os.environ,
                "REPORT_SCRIPT": str(self.sources.joinpath("reporter")),
                "FBOSS_REPORT_CMDLINE": str(cmdline),
                "FBOSS_REPORT_MARKER": str(marker),
                "REPORT_TEST_LOG": str(log),
                "REPORT_TEST_HTTP_STATUS": http_status,
            },
            capture_output=True,
            text=True,
            timeout=10,
        )
        self.assertEqual(result.returncode, 0 if accepted else 1, result.stderr)
        self.assertEqual(marker.exists(), accepted)
        self.assertTrue(log.exists())
        self.assertNotIn("--location", log.read_text().splitlines())
        self.assertNotIn("-L", log.read_text().splitlines())

    def test_service_does_not_restart_after_retry_exhaustion(self):
        service = configparser.ConfigParser(strict=False)
        service.read_string(self.sources.joinpath("service").read_text())
        self.assertEqual(service["Service"]["Restart"], "no")
        self.assertEqual(service["Service"]["TimeoutStartSec"], "2min")
        self.assertNotIn("RestartSec", service["Service"])
        self.assertNotIn(
            "ConditionPathExists=/etc/fboss-firstboot-report.conf",
            self.sources.joinpath("service").read_text(),
        )
