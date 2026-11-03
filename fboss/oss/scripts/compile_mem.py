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

"""Per-TU compiler peak-memory ceilings for heavy FSDB instantiation units.

Checks <tu>.peak.txt measurements (one line: exit=.. wall_s=.. VmPeak_GiB=..
VmHWM_GiB=.. cap_GiB=..) against vmpeak_gib ceilings in a budgets file, so a
template-expansion memory regression fails fast instead of surfacing as an
intermittent OSS LLVM OOM. --update-budgets reseeds ceilings from current
measurements (never raises an existing ceiling). Only stdlib is used; file
discovery and compiling stay with the caller.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from dataclasses import dataclass


NEW_TU_HEADROOM_RATIO = 0.10


@dataclass(frozen=True)
class PeakMeasurement:
    tu: str
    exit_code: int
    wall_s: float
    vmpeak_gib: float
    vmhwm_gib: float
    cap_gib: float


def parse_peak_line(tu: str, line: str) -> PeakMeasurement | None:
    try:
        fields: dict[str, float] = {}
        for token in line.split():
            key, _, value = token.partition("=")
            if key and value:
                fields[key] = float(value)
        return PeakMeasurement(
            tu=tu,
            exit_code=int(fields["exit"]),
            wall_s=fields.get("wall_s", 0.0),
            vmpeak_gib=fields["VmPeak_GiB"],
            vmhwm_gib=fields.get("VmHWM_GiB", 0.0),
            cap_gib=fields.get("cap_GiB", 0.0),
        )
    except (KeyError, ValueError):
        return None


def load_measurements(
    peaks_dir: pathlib.Path,
) -> tuple[dict[str, PeakMeasurement], list[str]]:
    measurements: dict[str, PeakMeasurement] = {}
    malformed: list[str] = []
    for path in sorted(peaks_dir.glob("*.peak.txt")):
        tu = path.name[: -len(".peak.txt")]
        try:
            lines = path.read_text().splitlines()
        except OSError:
            malformed.append(tu)
            continue
        parsed = parse_peak_line(tu, lines[0]) if lines else None
        if parsed is None:
            malformed.append(tu)
        else:
            measurements[tu] = parsed
    return measurements, malformed


def check_budgets(
    budgets: dict[str, float],
    measurements: dict[str, PeakMeasurement],
    malformed: frozenset[str] = frozenset(),
) -> tuple[list[str], list[str]]:
    """Return (violations, unmeasured TUs); empty violations means budgets hold.

    Unmeasured is separate because a run may re-measure only some TUs, which
    is no breach. A malformed peak file stays a violation: it did get measured.
    """
    violations: list[str] = []
    unmeasured: list[str] = []
    for tu, budget in sorted(budgets.items()):
        if tu in malformed:
            violations.append(f"{tu}: unparsable peak file, treating as over budget")
            continue
        measurement = measurements.get(tu)
        if measurement is None:
            unmeasured.append(tu)
            continue
        if measurement.exit_code != 0:
            violations.append(
                f"{tu}: compile failed (exit={measurement.exit_code}), "
                "peak numbers are meaningless"
            )
            continue
        # Compare at the precision budgets are recorded in, so the message
        # can never show two identical numbers for a breach.
        peak = round(measurement.vmpeak_gib, 2)
        if peak > budget:
            violations.append(
                f"{tu}: VmPeak {peak:.2f} GiB over budget {budget:.2f} GiB"
            )
    return violations, unmeasured


def load_budgets(path: pathlib.Path) -> tuple[dict[str, float], str | None]:
    """Read ceilings; never raise — the caller reports the error and exits."""
    try:
        raw = json.loads(path.read_text())
    except OSError as e:
        return {}, f"cannot read budgets file {path}: {e}"
    except ValueError as e:
        return {}, f"cannot parse budgets file {path}: {e}"
    tus = raw.get("tus") if isinstance(raw, dict) else None
    if not isinstance(tus, dict):
        return {}, f"budgets file {path} has no 'tus' object"
    budgets, bad = {}, []
    for tu, spec in tus.items():
        try:
            budgets[tu] = float(spec["vmpeak_gib"])
        except (KeyError, TypeError, ValueError):
            bad.append(tu)
    if bad:
        return {}, f"budgets file {path} has non-numeric ceilings: {sorted(bad)}"
    return budgets, None


def cmd_check(
    budgets_path: pathlib.Path,
    peaks_dir: pathlib.Path,
    require_measurements: bool = False,
) -> int:
    budgets, error = load_budgets(budgets_path)
    if error is not None:
        print(f"BUDGET ERROR: {error}")
        return 2
    measurements, malformed = load_measurements(peaks_dir)
    violations, unmeasured = check_budgets(
        budgets, measurements, malformed=frozenset(malformed)
    )
    fatal_unmeasured = require_measurements and bool(unmeasured)
    label = "BUDGET VIOLATION" if fatal_unmeasured else "BUDGET UNMEASURED"
    for tu in unmeasured:
        print(f"{label}: {tu}: no measurement in this run")
    for violation in violations:
        print(f"BUDGET VIOLATION: {violation}")
    return 2 if violations or fatal_unmeasured else 0


def cmd_update_budgets(budgets_path: pathlib.Path, peaks_dir: pathlib.Path) -> int:
    measurements, _ = load_measurements(peaks_dir)
    if not measurements:
        print(
            f"BUDGET ERROR: no peak files in {peaks_dir}; "
            f"refusing to overwrite {budgets_path}"
        )
        return 2
    try:
        existing = json.loads(budgets_path.read_text())
    except (OSError, ValueError):
        existing = {}
    if not isinstance(existing, dict):
        existing = {}
    old_tus = existing.get("tus", {})
    if not isinstance(old_tus, dict):
        old_tus = {}
    specs, blocked = {}, []
    for tu, m in sorted(measurements.items()):
        measured = round(m.vmpeak_gib, 2)
        old_spec = old_tus.get(tu, {})
        raw_budget = old_spec.get("vmpeak_gib") if isinstance(old_spec, dict) else None
        try:
            old_budget = float(raw_budget) if raw_budget is not None else None
        except (TypeError, ValueError):
            print(
                f"BUDGET ERROR: non-numeric ceiling for {tu} in {budgets_path}; "
                "fix it manually"
            )
            return 2
        if old_budget is None:
            # First sighting: seed above measured, matching the pool's
            # MEASUREMENT_HEADROOM_RATIO (kept in sync by hand).
            ceiling = round(m.vmpeak_gib * (1 + NEW_TU_HEADROOM_RATIO), 2)
        else:
            # Never move an existing ceiling: auto-lowering erodes
            # headroom to the noise floor, auto-raising hides growth.
            ceiling = old_budget
            if measured > old_budget:
                blocked.append(f"{tu}: measured {measured} over ceiling {old_budget}")
        specs[tu] = {"vmpeak_gib": ceiling, "measured_gib": measured}
    # Merge: a peaks-dir holding only re-measured TUs must not delete
    # siblings that were not re-measured this run.
    existing["tus"] = {**old_tus, **specs}
    budgets_path.write_text(json.dumps(existing, indent=2) + "\n")
    for line in blocked:
        print(f"BUDGET NOT RAISED (edit manually): {line}")
    return 2 if blocked else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--budgets", required=True, help="path to *_budgets.json")
    parser.add_argument("--peaks-dir", required=True, help="dir of <tu>.peak.txt files")
    parser.add_argument(
        "--update-budgets",
        action="store_true",
        help="rewrite budgets from current measurements instead of checking",
    )
    parser.add_argument(
        "--require-measurements",
        action="store_true",
        help="also fail when a budgeted TU has no measurement, for sweeps that "
        "are expected to measure every tracked TU",
    )
    args = parser.parse_args(argv)
    budgets_path = pathlib.Path(args.budgets)
    peaks_dir = pathlib.Path(args.peaks_dir)
    if args.update_budgets:
        return cmd_update_budgets(budgets_path, peaks_dir)
    return cmd_check(budgets_path, peaks_dir, args.require_measurements)


if __name__ == "__main__":
    sys.exit(main())
