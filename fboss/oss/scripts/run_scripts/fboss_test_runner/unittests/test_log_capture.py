#!/usr/bin/env python3
# @noautodeps
# Copyright Meta Platforms, Inc. and affiliates.

"""Unit tests for log_capture.py.

Pure helpers (run-dir naming, command-file contents, flag/subcommand parsing),
the zip bundler, and the LogCapture tee. Runnable directly on the devserver with
``python3 fboss_test_runner/unittests/test_log_capture.py`` (stdlib unittest;
no pytest / container required).
"""

import os
import subprocess
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from fboss_test_runner.log_capture import (
    _OutputTee,
    build_command_file_contents,
    build_run_dir_name,
    collect_generated_configs,
    collect_result_csvs,
    collect_service_logs,
    collect_system_logs,
    create_log_bundle,
    derive_test_type,
    LogCapture,
)


class DeriveTestTypeTest(unittest.TestCase):
    def test_returns_first_positional_token(self):
        self.assertEqual(
            derive_test_type(["run_test.py", "sai_agent", "--config", "x"]),
            "sai_agent",
        )

    def test_skips_leading_flags(self):
        self.assertEqual(
            derive_test_type(["run_test.py", "--log-bundle", "link"]), "link"
        )

    def test_none_when_no_subcommand(self):
        self.assertIsNone(derive_test_type(["run_test.py"]))
        self.assertIsNone(derive_test_type(["run_test.py", "--log-bundle"]))


class BuildRunDirNameTest(unittest.TestCase):
    def test_includes_type(self):
        # Styled after FBOSS log archives: <name>.log-<timestamp>
        self.assertEqual(
            build_run_dir_name("20260617141138", "sai_agent"),
            "run_test_sai_agent.log-20260617141138",
        )

    def test_defaults_name_when_no_type(self):
        self.assertEqual(
            build_run_dir_name("20260617141138", None),
            "run_test.log-20260617141138",
        )


class BuildCommandFileContentsTest(unittest.TestCase):
    def test_contains_command_and_env(self):
        content = build_command_file_contents(
            argv=["./run_test.py", "sai_agent", "--config", "/x y/z.conf"],
            env={"FBOSS_BIN": "/opt/fboss/bin", "FBOSS_LIB": "/opt/fboss/lib"},
            timestamp="2026-06-17_21-04-33",
            hostname="fboss123.snc1",
        )
        self.assertIn("2026-06-17_21-04-33", content)
        self.assertIn("fboss123.snc1", content)
        self.assertIn("/opt/fboss/bin", content)
        self.assertIn("/opt/fboss/lib", content)
        self.assertIn("sai_agent", content)
        # The command should be shell-quoted so it is copy-pasteable.
        self.assertIn("'/x y/z.conf'", content)


class CreateLogBundleTest(unittest.TestCase):
    def test_zips_run_dir_contents(self):
        with tempfile.TemporaryDirectory() as parent:
            run_dir = os.path.join(parent, "run_test_sai.log-20260617141138")
            os.makedirs(run_dir)
            with open(os.path.join(run_dir, "run_test.log"), "w") as f:
                f.write("log contents")
            with open(os.path.join(run_dir, "command.txt"), "w") as f:
                f.write("command")

            zip_path = create_log_bundle(run_dir)

            self.assertEqual(zip_path, run_dir + ".zip")
            self.assertTrue(os.path.isfile(zip_path))
            with zipfile.ZipFile(zip_path) as z:
                names = z.namelist()
            self.assertTrue(any(n.endswith("run_test.log") for n in names))
            self.assertTrue(any(n.endswith("command.txt") for n in names))


class OutputTeeIntegrationTest(unittest.TestCase):
    """Drive _OutputTee in a child process so it does not hijack this test's
    own stdout/stderr. Verifies the tee captures both Python stdout and the
    inherited stderr of a grandchild process, while still reaching the terminal.
    """

    def test_captures_python_and_child_stderr(self):
        run_scripts = os.path.abspath(
            os.path.join(os.path.dirname(__file__), "..", "..")
        )
        with tempfile.TemporaryDirectory() as d:
            log_path = os.path.join(d, "run_test.log")
            script_path = os.path.join(d, "drive.py")
            script = (
                "import os, sys, subprocess\n"
                f"sys.path.insert(0, {run_scripts!r})\n"
                "from fboss_test_runner.log_capture import _OutputTee\n"
                f"cap = _OutputTee({log_path!r})\n"
                "cap.start()\n"
                "print('from-python', flush=True)\n"
                "subprocess.run(['sh', '-c', 'echo from-child-stderr 1>&2'])\n"
                "cap.stop()\n"
                "print('after-stop-not-captured', flush=True)\n"
            )
            with open(script_path, "w") as f:
                f.write(script)

            result = subprocess.run(
                [sys.executable, script_path], capture_output=True, text=True
            )

            with open(log_path) as f:
                logged = f.read()
            # Runner stdout and the inherited stderr of the spawned binary are
            # both captured into the log.
            self.assertIn("from-python", logged)
            self.assertIn("from-child-stderr", logged)
            # Output after stop() is no longer captured.
            self.assertNotIn("after-stop-not-captured", logged)
            # It is a tee, not a redirect: the terminal (here, the captured
            # stdout of the child) still saw the line.
            self.assertIn("from-python", result.stdout)


class OutputTeeStartFailureTest(unittest.TestCase):
    def test_start_does_not_leak_fds_when_tee_spawn_fails(self):
        tee = _OutputTee(os.path.join(tempfile.gettempdir(), "unused.log"))
        dupped: list[int] = []
        real_dup = os.dup

        def tracking_dup(fd: int) -> int:
            new_fd = real_dup(fd)
            dupped.append(new_fd)
            return new_fd

        with (
            patch("os.dup", side_effect=tracking_dup),
            patch("subprocess.Popen", side_effect=FileNotFoundError("no tee")),
        ):
            with self.assertRaises(FileNotFoundError):
                tee.start()

        # start() dup'd fds 1 and 2 before the failed spawn; both must be closed
        # again (os.fstat on a closed fd raises) so they don't leak.
        self.assertEqual(len(dupped), 2)
        for fd in dupped:
            with self.assertRaises(OSError):
                os.fstat(fd)


class CollectServiceLogsTest(unittest.TestCase):
    def test_copies_only_log_files_into_run_dir(self):
        with (
            tempfile.TemporaryDirectory() as src,
            tempfile.TemporaryDirectory() as run_dir,
        ):
            for name in ("fboss_hw_agent_oss@0.log", "fsdb_service_oss.log"):
                with open(os.path.join(src, name), "w") as f:
                    f.write("service log")
            # A non-.log file must be ignored.
            with open(os.path.join(src, "notes.txt"), "w") as f:
                f.write("ignore me")

            collect_service_logs(run_dir, source_dir=src)

            self.assertEqual(
                sorted(os.listdir(run_dir)),
                ["fboss_hw_agent_oss@0.log", "fsdb_service_oss.log"],
            )

    def test_missing_source_dir_is_noop(self):
        with tempfile.TemporaryDirectory() as run_dir:
            collect_service_logs(run_dir, source_dir="/no/such/dir")
            self.assertEqual(os.listdir(run_dir), [])


class CollectResultCsvsTest(unittest.TestCase):
    def _write_csv(self, directory: str, name: str, mtime: float) -> str:
        path = os.path.join(directory, name)
        with open(path, "w") as f:
            f.write("Test Name,Result\n")
        os.utime(path, (mtime, mtime))
        return path

    def test_copies_csvs_modified_at_or_after_since(self):
        with (
            tempfile.TemporaryDirectory() as src,
            tempfile.TemporaryDirectory() as run_dir,
        ):
            self._write_csv(src, "hwtest_results_run.csv", mtime=5000.0)
            collect_result_csvs(run_dir, since=2000.0, source_dir=src)
            self.assertEqual(os.listdir(run_dir), ["hwtest_results_run.csv"])

    def test_skips_stale_csvs_from_earlier_runs(self):
        with (
            tempfile.TemporaryDirectory() as src,
            tempfile.TemporaryDirectory() as run_dir,
        ):
            self._write_csv(src, "hwtest_results_old.csv", mtime=1000.0)
            self._write_csv(src, "hwtest_results_new.csv", mtime=5000.0)
            collect_result_csvs(run_dir, since=2000.0, source_dir=src)
            self.assertEqual(os.listdir(run_dir), ["hwtest_results_new.csv"])

    def test_ignores_non_csv_files(self):
        with (
            tempfile.TemporaryDirectory() as src,
            tempfile.TemporaryDirectory() as run_dir,
        ):
            self._write_csv(src, "notes.txt", mtime=5000.0)
            collect_result_csvs(run_dir, since=0.0, source_dir=src)
            self.assertEqual(os.listdir(run_dir), [])


class CollectGeneratedConfigsTest(unittest.TestCase):
    def test_copies_current_runner_configs_modified_during_run(self):
        with (
            tempfile.TemporaryDirectory() as source_root,
            tempfile.TemporaryDirectory() as run_dir,
        ):
            source_dir = os.path.join(source_root, "sai")
            os.makedirs(source_dir)
            stale = os.path.join(source_dir, "stale.conf")
            current = os.path.join(source_dir, "current.conf")
            for path, mtime in ((stale, 1000.0), (current, 5000.0)):
                with open(path, "w") as config:
                    config.write("{}\n")
                os.utime(path, (mtime, mtime))

            collect_generated_configs(
                run_dir, "sai", since=2000.0, source_root=source_root
            )

            destination = os.path.join(run_dir, "generated_configs", "sai")
            self.assertEqual(["current.conf"], os.listdir(destination))


class LogCaptureOrchestrationTest(unittest.TestCase):
    """LogCapture (the bundler) creates the per-run dir, records the command, and
    tees output on enter; on exit it collects the logs/CSVs and zips. The tee and
    collectors are mocked here -- they have their own tests -- so this locks the
    orchestration glue (and guards against a collector being dropped)."""

    def test_enter_sets_up_then_exit_collects_and_zips(self):
        with (
            tempfile.TemporaryDirectory() as root,
            patch("fboss_test_runner.log_capture._OutputTee") as tee,
            patch("fboss_test_runner.log_capture.collect_service_logs") as collect_svc,
            patch("fboss_test_runner.log_capture.collect_result_csvs") as collect_csv,
            patch(
                "fboss_test_runner.log_capture.collect_generated_configs"
            ) as collect_configs,
            patch("fboss_test_runner.log_capture.collect_system_logs") as collect_sys,
            patch("fboss_test_runner.log_capture.create_log_bundle") as create_bundle,
        ):
            with LogCapture("sai", root=root):
                pass

            # __enter__ created the per-run dir and recorded the command.
            entries = os.listdir(root)
            self.assertEqual(len(entries), 1)
            run_dir = os.path.join(root, entries[0])
            self.assertTrue(entries[0].startswith("run_test_sai.log-"))
            self.assertTrue(os.path.isfile(os.path.join(run_dir, "command.txt")))

            # __enter__/__exit__ drove the tee into <run_dir>/run_test.log.
            tee.assert_called_once_with(os.path.join(run_dir, "run_test.log"))
            tee.return_value.start.assert_called_once()
            tee.return_value.stop.assert_called_once()

            # __exit__ collected the logs/CSVs/system logs and zipped the dir.
            collect_svc.assert_called_once_with(run_dir)
            collect_csv.assert_called_once()
            self.assertEqual(collect_csv.call_args.args[0], run_dir)
            self.assertIsInstance(collect_csv.call_args.args[1], float)
            collect_configs.assert_called_once()
            self.assertEqual(collect_configs.call_args.args[0], run_dir)
            self.assertEqual(collect_configs.call_args.args[1], "sai")
            self.assertIsInstance(collect_configs.call_args.args[2], float)
            collect_sys.assert_called_once_with(run_dir)
            create_bundle.assert_called_once_with(run_dir)


class CollectSystemLogsTest(unittest.TestCase):
    def test_writes_dmesg_output_to_run_dir(self):
        completed = subprocess.CompletedProcess(
            ["dmesg", "-T"], 0, stdout="[Tue Jun 26] kernel: hello\n", stderr=""
        )
        with tempfile.TemporaryDirectory() as run_dir:
            with patch("subprocess.run", return_value=completed) as run:
                collect_system_logs(run_dir, messages_path="/no/such/file")
            run.assert_called_once()
            self.assertEqual(run.call_args[0][0], ["dmesg", "-T"])
            with open(os.path.join(run_dir, "dmesg.log")) as f:
                self.assertIn("kernel: hello", f.read())

    def test_copies_var_log_messages(self):
        with (
            tempfile.TemporaryDirectory() as src,
            tempfile.TemporaryDirectory() as run_dir,
        ):
            messages_path = os.path.join(src, "messages")
            with open(messages_path, "w") as f:
                f.write("syslog line\n")
            # dmesg unavailable: the messages copy must still happen.
            with patch("subprocess.run", side_effect=FileNotFoundError("no dmesg")):
                collect_system_logs(run_dir, messages_path=messages_path)
            with open(os.path.join(run_dir, "messages")) as f:
                self.assertEqual(f.read(), "syslog line\n")

    def test_missing_sources_are_noop(self):
        with tempfile.TemporaryDirectory() as run_dir:
            with patch("subprocess.run", side_effect=FileNotFoundError("no dmesg")):
                collect_system_logs(run_dir, messages_path="/no/such/file")
            self.assertEqual(os.listdir(run_dir), [])


if __name__ == "__main__":
    unittest.main()
