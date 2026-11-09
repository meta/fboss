# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import json
import pathlib
import tempfile
import unittest
from unittest import mock

import compile_mem


PEAK_LINE = "exit=0 wall_s=968.5 VmPeak_GiB=15.03 VmHWM_GiB=14.61 cap_GiB=30.0\n"


class ParsePeakLineTest(unittest.TestCase):
    def test_parses_all_fields(self) -> None:
        m = compile_mem.parse_peak_line("SomeTU", PEAK_LINE)
        assert m is not None
        self.assertEqual(m.tu, "SomeTU")
        self.assertEqual(m.exit_code, 0)
        self.assertAlmostEqual(m.wall_s, 968.5)
        self.assertAlmostEqual(m.vmpeak_gib, 15.03)
        self.assertAlmostEqual(m.vmhwm_gib, 14.61)
        self.assertAlmostEqual(m.cap_gib, 30.0)


class CheckBudgetsTest(unittest.TestCase):
    def _measurement(
        self, peak: float, exit_code: int = 0
    ) -> compile_mem.PeakMeasurement:
        return compile_mem.PeakMeasurement(
            tu="SomeTU",
            exit_code=exit_code,
            wall_s=10.0,
            vmpeak_gib=peak,
            vmhwm_gib=peak,
            cap_gib=30.0,
        )

    def test_under_budget_passes(self) -> None:
        self.assertEqual(
            compile_mem.check_budgets(
                {"SomeTU": 16.0}, {"SomeTU": self._measurement(15.03)}
            ),
            ([], []),
        )

    def test_over_budget_fails(self) -> None:
        violations, _ = compile_mem.check_budgets(
            {"SomeTU": 16.0}, {"SomeTU": self._measurement(17.25)}
        )
        self.assertEqual(len(violations), 1)
        self.assertIn("17.25", violations[0])
        self.assertIn("16.00", violations[0])

    def test_failed_compile_is_a_violation(self) -> None:
        violations, _ = compile_mem.check_budgets(
            {"SomeTU": 99.0}, {"SomeTU": self._measurement(1.0, exit_code=1)}
        )
        self.assertEqual(len(violations), 1)
        self.assertIn("exit=1", violations[0])

    def test_missing_measurement_is_not_a_violation(self) -> None:
        violations, unmeasured = compile_mem.check_budgets({"SomeTU": 16.0}, {})
        self.assertEqual([], violations)
        self.assertEqual(["SomeTU"], unmeasured)

    def test_malformed_tu_is_reported_once(self) -> None:
        violations, unmeasured = compile_mem.check_budgets(
            {"SomeTU": 16.0}, {}, malformed=frozenset({"SomeTU"})
        )
        self.assertEqual(len(violations), 1)
        self.assertIn("unparsable", violations[0])
        self.assertEqual([], unmeasured)

    def test_marginal_peak_within_rounding_passes(self) -> None:
        violations, _ = compile_mem.check_budgets(
            {"SomeTU": 16.0}, {"SomeTU": self._measurement(16.004)}
        )
        self.assertEqual([], violations)


class PartialPeaksDirTest(unittest.TestCase):
    def _run(self, require: bool) -> int:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = pathlib.Path(temp_dir)
            budgets = temp_path / "budgets.json"
            budgets.write_text(
                '{"tus": {"Measured": {"vmpeak_gib": 16.0},'
                ' "NotMeasured": {"vmpeak_gib": 16.0}}}'
            )
            (temp_path / "Measured.peak.txt").write_text(
                "exit=0 wall_s=1.0 VmPeak_GiB=1.0 VmHWM_GiB=1.0 cap_GiB=30.0\n"
            )
            return compile_mem.cmd_check(budgets, temp_path, require)

    def test_partial_peaks_dir_passes_by_default(self) -> None:
        self.assertEqual(0, self._run(require=False))

    def test_partial_peaks_dir_fails_when_measurements_required(self) -> None:
        self.assertEqual(2, self._run(require=True))


class UpdateBudgetsTest(unittest.TestCase):
    def test_update_preserves_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = pathlib.Path(temp_dir)
            budgets = temp_path / "budgets.json"
            budgets.write_text(
                '{"_provenance": "seeded", "tus": {"SomeTU": {"vmpeak_gib": 16.0}}}'
            )
            (temp_path / "SomeTU.peak.txt").write_text(PEAK_LINE)
            self.assertEqual(
                0,
                compile_mem.main(
                    [
                        "--budgets",
                        str(budgets),
                        "--peaks-dir",
                        str(temp_path),
                        "--update-budgets",
                    ]
                ),
            )
            rewritten = json.loads(budgets.read_text())
            self.assertEqual("seeded", rewritten["_provenance"])
            self.assertEqual(16.0, rewritten["tus"]["SomeTU"]["vmpeak_gib"])
            self.assertEqual(15.03, rewritten["tus"]["SomeTU"]["measured_gib"])

    def test_update_never_lowers_ceilings(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = pathlib.Path(temp_dir)
            budgets = temp_path / "budgets.json"
            budgets.write_text('{"tus": {"SomeTU": {"vmpeak_gib": 16.0}}}')
            (temp_path / "SomeTU.peak.txt").write_text(PEAK_LINE)
            self.assertEqual(
                0,
                compile_mem.main(
                    [
                        "--budgets",
                        str(budgets),
                        "--peaks-dir",
                        str(temp_path),
                        "--update-budgets",
                    ]
                ),
            )
            specs = json.loads(budgets.read_text())["tus"]["SomeTU"]
            self.assertEqual(16.0, specs["vmpeak_gib"])
            self.assertEqual(15.03, specs["measured_gib"])

    def test_update_rejects_non_numeric_ceiling(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = pathlib.Path(temp_dir)
            budgets = temp_path / "budgets.json"
            before = '{"tus": {"SomeTU": {"vmpeak_gib": "huge"}}}'
            budgets.write_text(before)
            (temp_path / "SomeTU.peak.txt").write_text(PEAK_LINE)
            self.assertEqual(
                2,
                compile_mem.main(
                    [
                        "--budgets",
                        str(budgets),
                        "--peaks-dir",
                        str(temp_path),
                        "--update-budgets",
                    ]
                ),
            )
            self.assertEqual(before, budgets.read_text())

    def test_update_preserves_unmeasured_tus(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = pathlib.Path(temp_dir)
            budgets = temp_path / "budgets.json"
            budgets.write_text(
                '{"tus": {"SomeTU": {"vmpeak_gib": 16.0}, '
                '"OtherTU": {"vmpeak_gib": 14.0, "measured_gib": 13.4}}}'
            )
            (temp_path / "SomeTU.peak.txt").write_text(PEAK_LINE)
            self.assertEqual(
                0,
                compile_mem.main(
                    [
                        "--budgets",
                        str(budgets),
                        "--peaks-dir",
                        str(temp_path),
                        "--update-budgets",
                    ]
                ),
            )
            tus = json.loads(budgets.read_text())["tus"]
            self.assertEqual({"vmpeak_gib": 14.0, "measured_gib": 13.4}, tus["OtherTU"])
            self.assertEqual(16.0, tus["SomeTU"]["vmpeak_gib"])

    def test_update_never_raises_ceilings(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = pathlib.Path(temp_dir)
            budgets = temp_path / "budgets.json"
            budgets.write_text('{"tus": {"SomeTU": {"vmpeak_gib": 1.0}}}')
            (temp_path / "SomeTU.peak.txt").write_text(PEAK_LINE)
            self.assertEqual(
                2,
                compile_mem.main(
                    [
                        "--budgets",
                        str(budgets),
                        "--peaks-dir",
                        str(temp_path),
                        "--update-budgets",
                    ]
                ),
            )
            self.assertIn('"vmpeak_gib": 1.0', budgets.read_text())

    def test_malformed_peak_files_are_violations(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = pathlib.Path(temp_dir)
            budgets = temp_path / "budgets.json"
            budgets.write_text('{"tus": {"SomeTU": {"vmpeak_gib": 16.0}}}')
            (temp_path / "SomeTU.peak.txt").write_text("truncated garbage\n")
            (temp_path / "EmptyTU.peak.txt").write_text("")
            measurements, malformed = compile_mem.load_measurements(temp_path)
            self.assertEqual({}, measurements)
            self.assertEqual(["EmptyTU", "SomeTU"], sorted(malformed))
            self.assertEqual(
                2,
                compile_mem.main(
                    ["--budgets", str(budgets), "--peaks-dir", str(temp_path)]
                ),
            )

    def test_update_from_missing_file(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = pathlib.Path(temp_dir)
            (temp_path / "SomeTU.peak.txt").write_text(PEAK_LINE)
            self.assertEqual(
                0,
                compile_mem.main(
                    [
                        "--budgets",
                        str(temp_path / "budgets.json"),
                        "--peaks-dir",
                        str(temp_path),
                        "--update-budgets",
                    ]
                ),
            )

    def test_update_with_no_peaks_refuses_to_wipe(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = pathlib.Path(temp_dir)
            budgets = temp_path / "budgets.json"
            before = '{"tus": {"SomeTU": {"vmpeak_gib": 16.0, "measured_gib": 15.03}}}'
            budgets.write_text(before)
            self.assertEqual(
                2,
                compile_mem.main(
                    [
                        "--budgets",
                        str(budgets),
                        "--peaks-dir",
                        str(temp_path),
                        "--update-budgets",
                    ]
                ),
            )
            self.assertEqual(before, budgets.read_text())

    def test_update_new_tu_seeds_ceiling_above_measured(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = pathlib.Path(temp_dir)
            budgets = temp_path / "budgets.json"
            budgets.write_text('{"tus": {}}')
            (temp_path / "NewTU.peak.txt").write_text(PEAK_LINE)
            self.assertEqual(
                0,
                compile_mem.main(
                    [
                        "--budgets",
                        str(budgets),
                        "--peaks-dir",
                        str(temp_path),
                        "--update-budgets",
                    ]
                ),
            )
            specs = json.loads(budgets.read_text())["tus"]["NewTU"]
            self.assertEqual(15.03, specs["measured_gib"])
            self.assertEqual(16.53, specs["vmpeak_gib"])


class LoadMeasurementsTest(unittest.TestCase):
    def test_discovers_peak_files_by_tu_name(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            peaks_dir = pathlib.Path(temp_dir)
            (peaks_dir / "SomeTU.peak.txt").write_text(PEAK_LINE)
            measurements, malformed = compile_mem.load_measurements(peaks_dir)
            self.assertEqual(set(measurements), {"SomeTU"})
            self.assertEqual([], malformed)
            self.assertAlmostEqual(measurements["SomeTU"].vmpeak_gib, 15.03)

    def test_unreadable_peak_file_is_malformed_not_a_crash(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            peaks_dir = pathlib.Path(temp_dir)
            (peaks_dir / "SomeTU.peak.txt").write_text(PEAK_LINE)
            # chmod would not fail for root, which CI may run as.
            with mock.patch.object(
                pathlib.Path, "read_text", side_effect=OSError("unreadable")
            ):
                measurements, malformed = compile_mem.load_measurements(peaks_dir)
            self.assertEqual({}, measurements)
            self.assertEqual(["SomeTU"], malformed)

    def test_unreadable_peak_file_becomes_a_violation(self) -> None:
        violations, unmeasured = compile_mem.check_budgets(
            {"SomeTU": 16.0}, {}, malformed=frozenset({"SomeTU"})
        )
        self.assertEqual([], unmeasured)
        self.assertIn("unparsable", violations[0])


class LoadBudgetsTest(unittest.TestCase):
    def test_missing_file_is_an_error_not_a_crash(self) -> None:
        budgets, error = compile_mem.load_budgets(
            pathlib.Path("/nonexistent/budgets.json")
        )
        self.assertEqual({}, budgets)
        self.assertIsNotNone(error)

    def test_malformed_file_is_an_error_not_a_crash(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            budgets = pathlib.Path(temp_dir) / "budgets.json"
            budgets.write_text('{"tus": {"SomeTU": {"vmpeak_gib": "huge"}}}')
            _, error = compile_mem.load_budgets(budgets)
            self.assertIsNotNone(error)
