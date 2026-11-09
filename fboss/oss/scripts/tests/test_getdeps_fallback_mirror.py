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

import os
import unittest
from unittest import mock

from fboss.oss.scripts import getdeps_fallback_mirror


class ResolveDownloadDirTest(unittest.TestCase):
    @mock.patch.object(getdeps_fallback_mirror.subprocess, "check_output")
    def test_explicit_scratch_path(self, mock_check_output: mock.Mock) -> None:
        download_dir = getdeps_fallback_mirror.resolve_download_dir(
            "/path/to/getdeps.py",
            ["build", "--scratch-path", "/tmp/scratch", "fboss"],
        )

        self.assertEqual(
            os.path.realpath("/tmp/scratch/downloads"),
            download_dir,
        )
        mock_check_output.assert_not_called()

    @mock.patch.object(getdeps_fallback_mirror.subprocess, "check_output")
    def test_explicit_scratch_path_equals_form(
        self, mock_check_output: mock.Mock
    ) -> None:
        download_dir = getdeps_fallback_mirror.resolve_download_dir(
            "/path/to/getdeps.py",
            ["build", "--scratch-path=/tmp/scratch", "fboss"],
        )

        self.assertEqual(
            os.path.realpath("/tmp/scratch/downloads"),
            download_dir,
        )
        mock_check_output.assert_not_called()

    @mock.patch.object(
        getdeps_fallback_mirror.subprocess,
        "check_output",
        return_value="/tmp/implicit-scratch\n",
    )
    def test_implicit_scratch_path(self, mock_check_output: mock.Mock) -> None:
        download_dir = getdeps_fallback_mirror.resolve_download_dir(
            "/path/to/getdeps.py", ["build", "fboss"]
        )

        self.assertEqual(
            os.path.realpath("/tmp/implicit-scratch/downloads"),
            download_dir,
        )
        mock_check_output.assert_called_once_with(
            ["/path/to/getdeps.py", "show-scratch-dir"], text=True
        )

    @mock.patch.object(
        getdeps_fallback_mirror.subprocess,
        "check_output",
        side_effect=OSError("missing getdeps"),
    )
    def test_implicit_scratch_path_resolution_failure(
        self, mock_check_output: mock.Mock
    ) -> None:
        download_dir = getdeps_fallback_mirror.resolve_download_dir(
            "/path/to/getdeps.py", ["build", "fboss"]
        )

        self.assertIsNone(download_dir)
        mock_check_output.assert_called_once()

    @mock.patch.object(getdeps_fallback_mirror.subprocess, "check_output")
    def test_missing_explicit_scratch_path_value(
        self, mock_check_output: mock.Mock
    ) -> None:
        download_dir = getdeps_fallback_mirror.resolve_download_dir(
            "/path/to/getdeps.py", ["build", "fboss", "--scratch-path"]
        )

        self.assertIsNone(download_dir)
        mock_check_output.assert_not_called()
