#!/usr/bin/env python3

# pyre-strict

import os
import unittest
from unittest.mock import MagicMock, patch

from fboss.lib.platform_mapping_v2.facebook import gen


class GenerateDiffTitleTest(unittest.TestCase):
    def test_no_diff_returns_base_title(self) -> None:
        title = gen._generate_diff_title("")
        self.assertEqual(
            title,
            "[platform-mapping-sync] Syncing Platform Mapping Changes to Configerator",
        )
        self.assertNotIn("from", title)

    def test_with_diff_includes_diff_number(self) -> None:
        title = gen._generate_diff_title("D102639093")
        self.assertIn("from D102639093", title)


class GenerateDiffDescriptionTest(unittest.TestCase):
    def test_continuous_description_mentions_oncall_and_continuous(self) -> None:
        summary = gen._generate_diff_description("", is_continuous=True)
        self.assertIn("fboss_optics_phy oncall", summary)
        self.assertIn("continuous trunk-validation", summary)
        self.assertNotIn("Depends on", summary)
        self.assertNotIn("manually running", summary)

    def test_diff_attributed_description_includes_dependency(self) -> None:
        summary = gen._generate_diff_description("D102639093", is_continuous=False)
        self.assertIn("Depends on D102639093", summary)

    def test_manual_description_when_neither_diff_nor_continuous(self) -> None:
        summary = gen._generate_diff_description("", is_continuous=False)
        self.assertIn("manually running", summary)
        self.assertNotIn("Depends on", summary)


class CommitWithConfigoContinuousTest(unittest.TestCase):
    @patch.dict(os.environ, {"SANDCASTLE_SCHEDULE_TYPE": "continuous"}, clear=False)
    @patch("fboss.lib.platform_mapping_v2.facebook.gen.sync_configo_objects")
    def test_continuous_skips_attribution_and_uses_automation_reviewers(
        self, sync_configo_objects: MagicMock
    ) -> None:
        sync_configo_objects.return_value = 12345678

        result = gen.commit_with_configo(
            cconf_files={},
            add_fbcode_dependency=True,
            manual_configerator_diff="",
        )

        self.assertEqual(12345678, result)
        sync_configo_objects.assert_called_once()
        config = sync_configo_objects.call_args.args[1]
        self.assertEqual(
            ("fboss_optics_phy", "fboss_agent", "fboss_oss"), config.reviewers
        )
        self.assertEqual(
            frozenset(
                {
                    "fboss_platform_mapping_sync",
                    "publish_when_ready",
                    "accept2ship",
                }
            ),
            config.tags,
        )
        title, summary = config.metadata("")
        self.assertNotIn("from D", title)
        self.assertIn("continuous trunk-validation", summary)
        self.assertFalse(sync_configo_objects.call_args.kwargs["add_fbcode_dependency"])

    @patch.dict(os.environ, {"SANDCASTLE_SCHEDULE_TYPE": "diff"}, clear=False)
    @patch("fboss.lib.platform_mapping_v2.facebook.gen.sync_configo_objects")
    def test_diff_schedule_keeps_attribution(
        self, sync_configo_objects: MagicMock
    ) -> None:
        gen.commit_with_configo(
            cconf_files={},
            add_fbcode_dependency=True,
            manual_configerator_diff="",
        )

        sync_configo_objects.assert_called_once()
        config = sync_configo_objects.call_args.args[1]
        title, summary = config.metadata("D102639093")
        self.assertIn("from D102639093", title)
        self.assertIn("Depends on D102639093", summary)
        self.assertIn("publish_when_ready", config.tags)
        self.assertIn("accept2ship", config.tags)
        self.assertTrue(sync_configo_objects.call_args.kwargs["add_fbcode_dependency"])


if __name__ == "__main__":
    unittest.main()
