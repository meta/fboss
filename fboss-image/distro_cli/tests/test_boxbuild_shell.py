"""Shell boundary fixtures: no containers, VM boots, or disk operations."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


class BoxbuildShellTest(unittest.TestCase):
    def setUp(self):
        resource = Path(__file__).with_name("boxbuild.sh")
        source = Path(__file__).parents[2] / "image_builder/lib/boxbuild.sh"
        self.lib = Path(
            os.environ.get(
                "BOXBUILD_LIB", str(resource if resource.exists() else source)
            )
        )

    def shell(self, body, env=None):
        return subprocess.run(
            [
                "bash",
                "-c",
                'set -o pipefail; source "$1"; ' + body,
                "fixture",
                str(self.lib),
            ],
            env={**os.environ, **(env or {})},
            capture_output=True,
            text=True,
        )

    def test_signals_clean_up_description_and_box_cache(self):
        resource = Path(__file__).with_name("build_image_in_container.sh")
        source = (
            Path(__file__).parents[2] / "image_builder/bin/build_image_in_container.sh"
        )
        traps = "\n".join(
            line
            for line in (resource if resource.exists() else source)
            .read_text()
            .splitlines()
            if line.startswith("trap '")
        )
        for signal, expected in (
            ("HUP", 129),
            ("INT", 130),
            ("QUIT", 131),
            ("PIPE", 141),
            ("TERM", 143),
        ):
            with (
                self.subTest(signal=signal),
                tempfile.TemporaryDirectory() as tmp,
            ):
                result = self.shell(
                    'PRIVATE_DESCRIPTION="$TMP/description"; BOX_CACHE="$TMP/cache"; '
                    'mkdir "$PRIVATE_DESCRIPTION" "$BOX_CACHE"; '
                    'BOX_PROXY_PROFILE=""; cleanup_rc=""; PXE_PID=""; ONIE_PID=""; '
                    + traps
                    + f'; kill -s {signal} "$BASHPID"',
                    {"TMP": tmp},
                )
                self.assertEqual(result.returncode, expected, result.stderr)
                self.assertFalse((Path(tmp) / "description").exists())
                self.assertFalse((Path(tmp) / "cache").exists())

    def test_builds_run_concurrently_and_report_both_failures(self):
        resource = Path(__file__).with_name("build_image_in_container.sh")
        source = (
            Path(__file__).parents[2] / "image_builder/bin/build_image_in_container.sh"
        )
        lines = (resource if resource.exists() else source).read_text().splitlines()
        start = lines.index("# Generate the images")
        end = lines.index("RC=$((PXE_RC + ONIE_RC))")
        with tempfile.TemporaryDirectory() as tmp:
            result = self.shell(
                """
                set -e
                dprint() { printf '%s\\n' "$*"; }
                run_kiwi_boxbuild() {
                    printf '%s %s\\n' "$1" "$3" >> "$TMP/calls"
                    touch "$TMP/started-${3##*/}"
                    # Each build waits for the other: only concurrent builds finish.
                    for _ in $(seq 100); do
                        [[ -e $TMP/started-pxe && -e $TMP/started-onie ]] && return 17
                        sleep 0.05
                    done
                    return 99
                }
                TARGET_DIR="$TMP/output"; BOX_CACHE="$TMP/box"; BUILD_PXE=yes; BUILD_ONIE=yes
                KIWI_DEBUG=(); PXE_PID=""; ONIE_PID=""
                """
                + "\n".join(lines[start : end + 1])
                + '\necho "RC=$RC"',
                {"TMP": tmp},
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertCountEqual(
                (Path(tmp) / "calls").read_text().splitlines(),
                [f"PXE/USB Installer {tmp}/box/pxe", f"ONIE installer {tmp}/box/onie"],
            )
            self.assertIn("ERROR: PXE/USB image build failed", result.stdout)
            self.assertIn("ERROR: ONIE installer build failed", result.stdout)
            self.assertIn("RC=34", result.stdout)

    def test_guest_failure_not_hidden_by_logging(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = self.shell(
                'dprint() { :; }; kiwi-ng-3() { return 17; }; LOG_FILE="$TMP/log"; run_kiwi_boxbuild PXE "$TMP/result" "$TMP/cache" --profile FBOSS --type oem',
                {"TMP": tmp},
            )
            self.assertEqual(result.returncode, 17, result.stderr)

    def test_missing_guest_status_is_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = self.shell(
                'dprint() { :; }; kiwi-ng-3() { return 0; }; LOG_FILE="$TMP/log"; run_kiwi_boxbuild PXE "$TMP/result" "$TMP/cache" --profile FBOSS --type oem',
                {"TMP": tmp},
            )
            self.assertNotEqual(result.returncode, 0)

    def test_fresh_artifact_requires_one_match(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = self.shell(
                'touch "$TMP/a.install.iso" "$TMP/b.install.iso"; move_box_result "$TMP" "*.install.iso" "$TMP/final.iso"',
                {"TMP": tmp},
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((Path(tmp) / "final.iso").exists())

    def test_artifact_normalization_does_not_assume_suffix(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = self.shell(
                'printf data > "$TMP/image-42.install.iso"; move_box_result "$TMP" "*.install.iso" "$TMP/final.iso"',
                {"TMP": tmp},
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((Path(tmp) / "final.iso").read_text(), "data")

    def test_proxy_profile_private_and_cleaned(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = self.shell(
                'DESCRIPTION_DIR="$TMP"; http_proxy="http://proxy/quoted value"; prepare_box_proxy_profile; stat -c %a "$BOX_PROXY_PROFILE"; source "$BOX_PROXY_PROFILE"; printf "%s\\n" "$http_proxy"; cleanup_box_proxy_profile',
                {"TMP": tmp},
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                result.stdout.splitlines(), ["600", "http://proxy/quoted value"]
            )
            self.assertFalse((Path(tmp) / "boxroot/etc/boxproxy").exists())

    def test_boxbuild_uses_kvm_virtiofs_and_pinned_guest_kiwi(self):
        for memory, expected in (("", "8G"), ("16", "16G")):
            with self.subTest(memory=memory), tempfile.TemporaryDirectory() as tmp:
                result = self.shell(
                    """
                    LOG_FILE="$TMP/log"; nproc() { echo 64; };
                    [[ -n $MEMORY ]] && export FBOSS_BOX_MEMORY_GB=$MEMORY;
                    kiwi-ng-3() {
                        printf '%s\\n' "$KIWI_BOXED_CACHE_DIR $*" >> "$TMP/args";
                        local target="${*: -1}"; mkdir -p "$target"; echo 0 > "$target/result.code";
                    };
                    run_kiwi_boxbuild PXE "$TMP/result" "$TMP/cache" --profile FBOSS --type oem
                """,
                    {"TMP": tmp, "MEMORY": memory},
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                args = (Path(tmp) / "args").read_text().splitlines()
                self.assertEqual(len(args), 1)
                self.assertTrue(args[0].startswith(f"{tmp}/cache "))
                self.assertIn("system boxbuild --box fedora", args[0])
                self.assertNotIn("--no-accel", args[0])
                self.assertNotIn("--container", args[0])
                self.assertIn("--virtiofs-sharing", args[0])
                self.assertIn("--no-update-check", args[0])
                self.assertIn("--kiwi-version 11.0.1", args[0])
                self.assertIn("--box-smp-cpus 32", args[0])
                self.assertIn(f"--box-memory {expected}", args[0])

    def test_box_fetched_once_and_copied_without_extracted_kernel(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = self.shell(
                """
                python3() {
                    cat >/dev/null
                    flock -n "$TMP/shared/.lock" true && return 9
                    echo "$KIWI_BOXED_CACHE_DIR $*" >> "$TMP/fetches"
                    mkdir -p "$KIWI_BOXED_CACHE_DIR/fedora"
                    cd "$KIWI_BOXED_CACHE_DIR/fedora" || return 1
                    touch Fedora-Box.x86_64-System.qcow2 Fedora-Box.x86_64-System.qcow2.sha256 \\
                        Fedora-Box.x86_64-Kernel.tar.xz kernel.x86_64 initrd.x86_64
                };
                stage_box_caches "$TMP/shared" "$TMP/box/pxe" "$TMP/box/onie"
            """,
                {"TMP": tmp},
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                (Path(tmp) / "fetches").read_text().splitlines(),
                [f"{tmp}/shared -s - fedora"],
            )
            for build in ("pxe", "onie"):
                with self.subTest(build=build):
                    self.assertEqual(
                        sorted(
                            p.name
                            for p in (Path(tmp) / "box" / build / "fedora").iterdir()
                        ),
                        [
                            "Fedora-Box.x86_64-Kernel.tar.xz",
                            "Fedora-Box.x86_64-System.qcow2",
                            "Fedora-Box.x86_64-System.qcow2.sha256",
                        ],
                    )
