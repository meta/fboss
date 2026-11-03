# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from distro_cli.lib import execute
from fboss.oss.scripts import component_version


class ExecuteTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.producer = self.root / "producer"
        self.script = self.producer / "kernel/build.sh"
        self.script.parent.mkdir(parents=True)
        self.script.write_text("#!/bin/bash\n")
        self.git("init", "--quiet")
        self.git("add", "kernel/build.sh")
        self.git(
            "-c",
            "user.name=Test",
            "-c",
            "user.email=test@example.com",
            "commit",
            "--quiet",
            "-m",
            "Producer fixture",
        )
        self.revision = self.git("rev-parse", "HEAD").strip()
        self.common = self.root / "common"
        self.common.mkdir()
        shutil.copyfile(
            component_version.__file__, self.common / "component_version.py"
        )
        self.volumes = {
            self.root: Path("/src"),
            self.common: Path("/fboss/oss/scripts"),
        }
        environment = mock.patch.dict(
            os.environ,
            {
                "FBOSS_SOURCE_REVISION": "",
                "FBOSS_SOURCE_DIRTY": "",
                "FBOSS_BUILD_VERSION": "",
            },
        )
        environment.start()
        self.addCleanup(environment.stop)
        builder = mock.patch.object(execute, "build_fboss_builder_image")
        builder.start()
        self.addCleanup(builder.stop)
        launcher = mock.patch.object(execute, "run_container", return_value=0)
        self.launch = launcher.start()
        self.addCleanup(launcher.stop)

    def git(self, *args: str) -> str:
        return subprocess.check_output(
            ["git", "-C", str(self.producer), *args], text=True
        )

    def build(self) -> dict[str, str]:
        execute.execute_build_in_container(
            image_name="test-image",
            command=["/src/producer/kernel/build.sh", "6.11.1", "/output"],
            volumes=self.volumes,
            component_name="kernel",
            working_dir="/src/producer/kernel",
        )
        return self.launch.call_args.kwargs["env"]

    def test_captures_actual_producer_checkout_before_launch(self) -> None:
        self.assertEqual(
            self.build(),
            {"FBOSS_SOURCE_REVISION": self.revision, "FBOSS_SOURCE_DIRTY": "false"},
        )

    def test_assembler_revision_does_not_replace_dirty_producer_identity(self) -> None:
        self.script.write_text("#!/bin/bash\necho modified\n")
        with mock.patch.dict(
            os.environ,
            {
                "FBOSS_SOURCE_REVISION": "a" * 40,
                "FBOSS_SOURCE_DIRTY": "false",
                "FBOSS_BUILD_VERSION": "kernel-42",
                "UNRELATED_SECRET": "not producer metadata",
            },
        ):
            self.assertEqual(
                self.build(),
                {
                    "FBOSS_SOURCE_REVISION": self.revision,
                    "FBOSS_SOURCE_DIRTY": "true",
                    "FBOSS_BUILD_VERSION": "kernel-42",
                },
            )

    def test_source_only_checkout_leaves_revision_and_dirty_state_absent(self) -> None:
        shutil.rmtree(self.producer / ".git")
        with mock.patch.dict(os.environ, {"FBOSS_SOURCE_REVISION": "a" * 40}):
            self.assertEqual(self.build(), {})

    def test_capture_failure_prevents_container_launch(self) -> None:
        (self.common / "component_version.py").unlink()
        with self.assertRaisesRegex(
            execute.BuildError, "Failed to capture kernel producer identity"
        ):
            self.build()
        self.launch.assert_not_called()

    def test_forwarded_identity_reaches_service_and_kernel_capture(self) -> None:
        scripts = self.root / "source-only/kernel/scripts"
        scripts.mkdir(parents=True)
        output = self.root / "kernel-output"

        def prepare(*, env: dict[str, str], **kwargs: object) -> int:
            subprocess.run(
                [
                    sys.executable,
                    str(self.common / "component_version.py"),
                    "generate",
                    "--source-dir",
                    str(scripts),
                    "--output-dir",
                    str(output / "service"),
                    "--target",
                    "fsdb",
                ],
                env={**os.environ, **env},
                check=True,
            )
            subprocess.run(
                [
                    sys.executable,
                    str(self.common / "component_version.py"),
                    "kernel",
                    "--prepare",
                    "--output-dir",
                    str(output),
                    "--scripts-dir",
                    str(scripts),
                ],
                env={**os.environ, **env},
                check=True,
            )
            return 0

        self.launch.side_effect = prepare
        self.build()
        record = json.loads(
            (output / component_version.KERNEL_BUILD_INFO_PATH).read_text()
        )
        self.assertEqual(
            record["build"]["fboss"],
            {"source_revision": self.revision, "source_dirty": False},
        )
        self.assertIn(self.revision, (output / "service/fsdb.cpp").read_text())


if __name__ == "__main__":
    unittest.main()
