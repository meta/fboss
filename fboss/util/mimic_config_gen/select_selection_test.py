# Copyright (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

# pyre-strict

"""Tests for defs.select_selection: coop-faithful multi-selection resolution."""

import typing as t
import unittest

from fboss.util.mimic_config_gen.defs import MimicError, select_selection, SelectionCtx


def _whoami(
    name: str,
    lc_type: int | None = None,
) -> t.Any:
    from netwhoami.thrift_enums import LinecardType
    from netwhoami.thrift_types import NetWhoAmI

    kwargs: dict[str, t.Any] = {"name": name}
    if lc_type is not None:
        kwargs["lc_type"] = LinecardType(lc_type)
    return NetWhoAmI(**kwargs)


def _doc(*sels: tuple[str, dict[str, t.Any]]) -> dict[str, t.Any]:
    return {
        "selections": [
            {"selector": sel, "data": {"arm": {"picked": name}}} for name, sel in sels
        ]
    }


# Shapes mirror production artifacts: GTSW agent_sw_template carries
# {gtsw_yolo} / {VULCANO_LAB name regex} / {} and FA agent_sw_template
# carries lc_type x sai_bcm variants.
GTSW_DOC = _doc(
    ("yolo", {"features": ["gtsw_yolo"]}),
    (
        "lab",
        {
            "regex_match": {
                "name": "GTSW_VULCANO_LAB",
                "regexes": ["gtsw00[1-8]\\.l1003\\.c089\\.nha6"],
            }
        },
    ),
    ("default", {}),
)

FA_DOC = _doc(
    ("lc4-sai", {"lc_type": 4, "features": ["sai_bcm"]}),
    ("lc3-sai", {"lc_type": 3, "features": ["sai_bcm"]}),
    ("lc4", {"lc_type": 4}),
    ("lc3", {"lc_type": 3}),
)


def _picked(
    doc: dict[str, t.Any],
    name: str,
    enabled: t.FrozenSet[str],
    lc: int | None = None,
) -> str:
    ctx = SelectionCtx(_whoami(name, lc), enabled)
    inner = select_selection(doc, ctx, "agent_sw_template")
    picked = inner["picked"]
    assert isinstance(picked, str)
    return picked


class SelectSelectionTest(unittest.TestCase):
    def test_single_selection_legacy_path(self) -> None:
        doc = _doc(("only", {}))
        self.assertEqual(_picked(doc, "any001.tfbnw.net", frozenset()), "only")

    def test_gtsw_prod_donor_gets_default(self) -> None:
        self.assertEqual(
            _picked(GTSW_DOC, "gtsw001.l1001.c087.mwg2.tfbnw.net", frozenset()),
            "default",
        )

    def test_gtsw_yolo_feature_selects_yolo(self) -> None:
        self.assertEqual(
            _picked(
                GTSW_DOC,
                "gtsw001.l1001.c087.mwg2.tfbnw.net",
                frozenset({"gtsw_yolo"}),
            ),
            "yolo",
        )

    def test_gtsw_lab_name_selects_lab(self) -> None:
        self.assertEqual(
            _picked(GTSW_DOC, "gtsw003.l1003.c089.nha6.tfbnw.net", frozenset()),
            "lab",
        )

    def test_fa_lc3_sai_selects_lc3_sai(self) -> None:
        self.assertEqual(
            _picked(FA_DOC, "fa001-du001.eag3.tfbnw.net", frozenset({"sai_bcm"}), lc=3),
            "lc3-sai",
        )

    def test_fa_lc4_no_sai_selects_lc4(self) -> None:
        self.assertEqual(
            _picked(FA_DOC, "fa002-du001.eag3.tfbnw.net", frozenset(), lc=4),
            "lc4",
        )

    def test_first_match_wins(self) -> None:
        doc = _doc(("first", {}), ("second", {}))
        self.assertEqual(_picked(doc, "any001.tfbnw.net", frozenset()), "first")

    def test_no_match_raises_with_input_name(self) -> None:
        doc = _doc(("yolo", {"features": ["gtsw_yolo"]}))
        # Two selections so the legacy single path does not apply; neither
        # matches a donor without the feature.
        doc["selections"].append(
            {"selector": {"features": ["other_feature"]}, "data": {"arm": {}}}
        )
        with self.assertRaisesRegex(MimicError, "agent_sw_template"):
            _picked(doc, "any001.tfbnw.net", frozenset())

    def test_empty_selections_raises(self) -> None:
        with self.assertRaises(MimicError):
            _picked({"selections": []}, "any001.tfbnw.net", frozenset())

    def test_missing_selections_raises(self) -> None:
        with self.assertRaises(MimicError):
            _picked({}, "any001.tfbnw.net", frozenset())


if __name__ == "__main__":
    unittest.main()
