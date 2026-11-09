# pyre-strict

"""Runs the pre-flight pipeline against every canonical per-platform
`spec.json` under `fboss/platform/firmware_onboarding/configs/<platform>/`.
Catches regressions where a schema or validator change breaks an existing
onboarded spec.

When a new platform is onboarded, the `firmware-npi-onboard` skill's
**Platform sub-skill** (Phase 5, Step 6) automatically registers the new
platform here as a separate draft diff:

1. Adds an entry to `_CANONICAL_PLATFORMS` below.
2. Adds a corresponding entry to the `resources` dict in
   `//fboss/platform/firmware_onboarding/validator:test_canonical_examples`.

If you are manually adding a new platform without going through the
orchestrator, perform the two steps above by hand.
"""

from __future__ import annotations

import tempfile
import unittest
from importlib.resources import files
from pathlib import Path

from fboss.platform.firmware_onboarding.validator.preflight import (
    _is_hard_failure,
    run_preflight,
)

_CANONICAL_PLATFORMS: list[str] = ["icecube800bc", "icecube800banw", "m4062nhp"]


class CanonicalExamplesTest(unittest.TestCase):
    def test_each_canonical_spec_passes_preflight(self) -> None:
        package = __package__ or "fboss.platform.firmware_onboarding.validator.tests"
        # Materialize EVERY canonical spec into a shared tempdir laid out like
        # the real `configs/<platform>/spec.json` tree, then validate each
        # against that shared tree. The cross-spec `shared_with_platforms`
        # check therefore sees every sibling — so a diff that introduces (or
        # modifies a spec into) a mismatched `platform_identifier` for a
        # shared firmware image fails CI right here, before it lands.
        #
        # The diff being tested supplies the latest version of every canonical
        # spec via the BUCK resources dict, so this also catches edits to a
        # landed spec that would silently break a sibling.
        with tempfile.TemporaryDirectory() as tmp:
            configs_root = Path(tmp)
            for platform in _CANONICAL_PLATFORMS:
                try:
                    spec_text = (files(package) / f"{platform}.json").read_text()
                except (FileNotFoundError, ModuleNotFoundError):
                    src_spec = (
                        Path(__file__).resolve().parents[2]
                        / "configs"
                        / platform
                        / "spec.json"
                    )
                    spec_text = src_spec.read_text()
                spec_dir = configs_root / platform
                spec_dir.mkdir(parents=True, exist_ok=True)
                (spec_dir / "spec.json").write_text(spec_text)

            for platform in _CANONICAL_PLATFORMS:
                with self.subTest(platform=platform):
                    spec_path = configs_root / platform / "spec.json"
                    errors = run_preflight(spec_path, configs_root=configs_root)
                    # Reuse preflight's hard-failure predicate so this test
                    # stays in sync if new tiers (e.g. `[depcheck]`) are added.
                    hard = [e for e in errors if _is_hard_failure(e)]
                    self.assertEqual(
                        hard, [], f"{platform}/spec.json failed preflight: {errors}"
                    )
