# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

"""Tests for the one file describing what an image was assembled from."""

import io
import json
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from distro_cli.lib import artifact_versions, version_file
from distro_cli.lib.artifact_versions import READERS, SDK_METADATA_MEMBER


def make_tar(path: Path, members: dict[str, bytes]) -> Path:
    """Build a real tarball, storing members with the "./" prefix real ones use."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(path, "w") as tar:
        for name, content in members.items():
            member = tarfile.TarInfo(f"./{name}")
            member.size = len(content)
            tar.addfile(member, io.BytesIO(content))
    return path


def sdk_metadata(sai: str = "14.2.0.0_odp", asic: str = "6.5.34") -> bytes:
    return json.dumps(
        {
            "fboss_hw_agent-sai_impl": {
                "npuSaiImpl": "SAI_BRCM_IMPL",
                "sdkVersion": {"asicSdk": asic, "saiSdk": sai},
            }
        }
    ).encode()


class VersionFileTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def manifest(self) -> Path:
        path = self.root / "6.11.1_xgs_6_5_34.json"
        path.write_text("{}")
        return path

    def agent(self, name: str = "agent.tar.zst", **kwargs) -> Path:
        return make_tar(self.root / name, {SDK_METADATA_MEMBER: sdk_metadata(**kwargs)})


class SchemaTest(unittest.TestCase):
    """The schema is what makes a component's versions readable"""

    def test_every_schema_names_a_reader_that_exists(self):
        for component, schema in version_file.MODULE_SCHEMAS.items():
            for reader in schema.readers:
                self.assertIn(reader, READERS, f"{component} names unknown {reader}")

    def test_every_claim_can_be_checked_by_something_the_component_reports(self):
        """A claim with no matching provider could never be verified."""
        for component, schema in version_file.MODULE_SCHEMAS.items():
            provided = {provides.version for provides in schema.provides}
            for claims in schema.claims:
                self.assertIn(
                    claims.version,
                    provided,
                    f"{component} claims {claims.version} but never reports it",
                )

    def test_service_components_read_packaged_build_info(self):
        for component in ("fboss-forwarding-stack", "fboss-platform-stack"):
            self.assertIn("build_info", version_file.schema_for(component).readers)

    def test_a_component_absent_from_the_schema_reads_nothing(self):
        schema = version_file.schema_for("other_dependencies")
        self.assertEqual(schema.readers, ())
        self.assertEqual(schema.provides, ())

    def test_a_list_component_uses_its_base_schema(self):
        indexed = version_file.schema_for("fboss-forwarding-stack[2]")
        base = version_file.schema_for("fboss-forwarding-stack")
        self.assertEqual(indexed, base)


class ExtractorTest(unittest.TestCase):
    """Pulling one version out of what a reader returned"""

    def test_kernel_from_a_module_vermagic(self):
        """vermagic leads with the release and continues with build flags."""
        provenance = {
            "modules": [
                {"vermagic": "6.11.1-1.fboss.el9.x86_64 SMP mod_unload modversions"}
            ]
        }
        self.assertEqual(
            version_file.kernel_from_modules(provenance), "6.11.1-1.fboss.el9"
        )

    def test_package_release_from_a_nevra_whose_name_contains_dashes(self):
        provenance = {
            "packages": [
                {"nevra": "fboss_bsp_kmods-6.11.1-1.fboss.el9.x86_64-4.4.2-1.x86_64"}
            ]
        }
        self.assertEqual(version_file.bsp_from_packages(provenance), "4.4.2-1")

    def test_sai_and_native_sdk_from_sdk_metadata(self):
        provenance = {
            "sdk": {
                "agent": {"sdkVersion": {"saiSdk": "14.2.0.0_odp", "asicSdk": "6.5.34"}}
            }
        }
        self.assertEqual(version_file.sai_from_sdk(provenance), "14.2.0.0_odp")
        self.assertEqual(version_file.native_sdk_from_sdk(provenance), "6.5.34")

    def test_nothing_reported_extracts_nothing(self):
        for extract in (
            version_file.kernel_from_packages,
            version_file.kernel_from_modules,
            version_file.bsp_from_packages,
            version_file.sai_from_sdk,
            version_file.native_sdk_from_sdk,
        ):
            self.assertIsNone(extract({}), extract.__name__)


class KernelReleaseTest(unittest.TestCase):
    """Every form of kernel evidence normalises to one comparable release"""

    def test_the_release_is_kept_not_just_the_version(self):
        """Two builds of one upstream version must be distinguishable."""
        self.assertEqual(
            version_file.kernel_release("6.11.1-1.fboss.el9.x86_64"),
            "6.11.1-1.fboss.el9",
        )

    def test_a_bare_version_is_still_read(self):
        self.assertEqual(version_file.kernel_release("6.11.1"), "6.11.1")

    def test_a_string_with_no_version_reports_nothing(self):
        self.assertIsNone(version_file.kernel_release("fboss_bins.tar.zst"))

    def test_every_form_of_evidence_agrees_on_one_release(self):
        """Comparing the raw strings would never match across these forms.

        This is what makes the cross-component check possible at all.
        """
        release = "6.11.1-1.fboss.el9"
        evidence = [
            "kernel-core = 1:" + release,
            release + ".x86_64 SMP mod_unload modversions",
            "fboss_bsp_kmods-" + release + ".x86_64-4.4.2-1.x86_64",
        ]

        self.assertEqual(
            {version_file.kernel_release(text) for text in evidence}, {release}
        )

    def test_a_package_version_is_not_mistaken_for_a_kernel(self):
        """A kmod package names its own version too, and reading that as a
        kernel would invent a disagreement out of nothing."""
        self.assertIsNone(
            version_file.kernel_from_packages(
                {"packages": [{"nevra": "fboss_bsp_kmods-4.4.2-1.x86_64"}]}
            )
        )

    def test_a_package_name_is_used_when_no_dependency_states_it(self):
        """The BSP declares no kernel dependency; its name is the only source."""
        self.assertEqual(
            version_file.kernel_from_packages(
                {
                    "packages": [
                        {
                            "nevra": "fboss_bsp_kmods-6.11.1-1.fboss.el9.x86_64-4.4.2-1.x86_64"
                        }
                    ]
                }
            ),
            "6.11.1-1.fboss.el9",
        )


class DisputedVersionTest(unittest.TestCase):
    """A version its components disagree on is omitted, never guessed"""

    def kernel_pair(self, one: str, other: str) -> dict[str, object]:
        return {
            "kernel": {
                "provenance": {
                    "packages": [{"kernel_requires": [f"kernel-core = 1:{one}"]}]
                }
            },
            "npu_sai": {
                "provenance": {"modules": [{"vermagic": f"{other}.x86_64 SMP"}]}
            },
        }

    def test_the_result_does_not_depend_on_which_component_is_seen_first(self):
        """Recording the first-seen value would make the record depend on dict
        order, so neither is recorded."""
        forward = self.kernel_pair("6.11.1-1.fboss.el9", "6.11.2-1.fboss.el9")
        reverse = self.kernel_pair("6.11.2-1.fboss.el9", "6.11.1-1.fboss.el9")

        one, _ = version_file.resolve_versions(forward)
        other, _ = version_file.resolve_versions(reverse)

        self.assertNotIn("kernel", one)
        self.assertNotIn("kernel", other)
        self.assertEqual(one, other)

    def test_a_disputed_version_does_not_suppress_an_undisputed_one(self):
        components = self.kernel_pair("6.11.1-1.fboss.el9", "6.11.2-1.fboss.el9")
        components["bsps"] = {
            "provenance": {"packages": [{"nevra": "fboss_bsp_kmods-4.4.2-1.x86_64"}]}
        }

        versions, conflicts = version_file.resolve_versions(components)

        self.assertNotIn("kernel", versions)
        self.assertEqual(versions["bsp"], "4.4.2-1")
        self.assertEqual(len(conflicts), 1)

    def test_the_conflict_still_names_both_values(self):
        """The record omits the version, so the message has to carry it."""
        _, conflicts = version_file.resolve_versions(
            self.kernel_pair("6.11.1-1.fboss.el9", "6.11.2-1.fboss.el9")
        )

        self.assertIn("6.11.1-1.fboss.el9", conflicts[0])
        self.assertIn("6.11.2-1.fboss.el9", conflicts[0])


class PairingTest(VersionFileTestCase):
    """A locator is only checked against the artifact it actually describes"""

    def test_a_dropped_artifact_does_not_shift_locators(self):
        """An element that builds nothing is skipped, so the counts disagree and
        index pairing would check every later artifact against the wrong
        locator -- inventing or hiding a contradiction."""
        data = {
            "fboss-forwarding-stack": [
                {"name": "agent", "download": "x/agent/brcm/14.2.0.0_odp/a.tar"},
                {"name": "fsdb", "download": "x/fsdb/b.tar"},
            ]
        }

        with self.assertLogs("distro_cli.lib.version_file", level="WARNING") as logged:
            records = version_file.describe_components(
                data, {"fboss-forwarding-stack": [self.agent("b.tar")]}
            )

        self.assertIn("cannot be checked", "".join(logged.output))
        # No locator is recorded, so no claim is attributed to this artifact --
        # the checksum and provenance are still kept.
        record = records["fboss-forwarding-stack"]
        self.assertNotIn("source", record)
        self.assertIn("sha256", record["artifact"])

    def test_matching_counts_still_pair_by_position(self):
        data = {
            "fboss-forwarding-stack": [
                {"name": "agent", "download": "x/agent/brcm/14.2.0.0_odp/a.tar"},
                {"name": "fsdb", "download": "x/fsdb/b.tar"},
            ]
        }

        records = version_file.describe_components(
            data,
            {"fboss-forwarding-stack": [self.agent("a.tar"), self.agent("b.tar")]},
        )

        self.assertEqual(
            records["fboss-forwarding-stack[agent]"]["source"]["locator"],
            "x/agent/brcm/14.2.0.0_odp/a.tar",
        )


class PerArtifactVersionTest(unittest.TestCase):
    """Only a version identifying the whole image has to agree"""

    def test_two_bsps_at_different_releases_do_not_fail_the_build(self):
        """bsp names the release of one package, not of the image, so two BSP
        artifacts legitimately differ."""
        components = {
            "bsps[a]": {
                "provenance": {
                    "packages": [
                        {
                            "nevra": "fboss_bsp_kmods-6.11.1-1.fboss.el9.x86_64-4.4.2-1.x86_64"
                        }
                    ]
                }
            },
            "bsps[b]": {
                "provenance": {
                    "packages": [
                        {
                            "nevra": "other_bsp_kmods-6.11.1-1.fboss.el9.x86_64-5.1.0-2.x86_64"
                        }
                    ]
                }
            },
        }

        versions, conflicts = version_file.resolve_versions(components)

        self.assertEqual(conflicts, [])
        self.assertEqual(versions["kernel"], "6.11.1-1.fboss.el9")
        self.assertIn(versions["bsp"], ("4.4.2-1", "5.1.0-2"))

    def test_the_versions_that_must_agree_are_stated_once(self):
        """Every image-wide version has a provider, or the check is vacuous."""
        provided = {
            provides.version
            for schema in version_file.MODULE_SCHEMAS.values()
            for provides in schema.provides
        }

        for name in version_file.IMAGE_WIDE_VERSIONS:
            self.assertIn(name, provided)


class ConflictTest(unittest.TestCase):
    """Evidence that disagrees is reported, and the record still written"""

    def test_binaries_disagreeing_on_the_sdk_warns_and_does_not_raise(self):
        provenance = {
            "sdk": {
                "fboss_hw_agent": {"sdkVersion": {"saiSdk": "14.2.0.0_odp"}},
                "sai_test": {"sdkVersion": {"saiSdk": "13.3.0.0_odp"}},
            }
        }
        with self.assertLogs("distro_cli.lib.version_file", level="WARNING") as logged:
            value = version_file.sai_from_sdk(provenance)

        message = "".join(logged.output)
        self.assertIn("14.2.0.0_odp", message)
        self.assertIn("13.3.0.0_odp", message)
        # No value: naming one would record an SDK this artifact does not have.
        self.assertIsNone(value)

    def test_components_disagreeing_on_the_kernel_is_reported_not_raised(self):
        """Modules built for one kernel will not load into another, but the
        record is still written: it is what diagnoses the mismatch."""
        components = {
            "kernel": {
                "provenance": {
                    "packages": [
                        {"kernel_requires": ["kernel-core = 1:6.11.1-1.fboss.el9"]}
                    ]
                }
            },
            "npu_sai": {
                "provenance": {
                    "modules": [{"vermagic": "6.11.2-1.fboss.el9.x86_64 SMP"}]
                }
            },
        }
        versions, conflicts = version_file.resolve_versions(components)

        self.assertEqual(len(conflicts), 1)
        self.assertIn("kernel", conflicts[0])
        self.assertIn("6.11.1-1.fboss.el9", conflicts[0])
        self.assertIn("6.11.2-1.fboss.el9", conflicts[0])
        # Neither value is recorded: with the components disagreeing, no single
        # value is the image's, and picking one would depend on visit order.
        self.assertNotIn("kernel", versions)

    def test_components_that_agree_report_no_conflict(self):
        components = {
            "kernel": {
                "provenance": {
                    "packages": [
                        {"kernel_requires": ["kernel-core = 1:6.11.1-1.fboss.el9"]}
                    ]
                }
            },
            "npu_sai": {
                "provenance": {
                    "modules": [{"vermagic": "6.11.1-1.fboss.el9.x86_64 SMP"}]
                }
            },
        }

        self.assertEqual(version_file.resolve_versions(components)[1], [])


class MemberListingTest(unittest.TestCase):
    """Member names come back whole"""

    def test_a_member_name_containing_a_space_survives(self):
        """Splitting tar output on any whitespace would fragment the name into
        pieces that match nothing, silently dropping that member's evidence."""
        with tempfile.TemporaryDirectory() as workspace:
            root = Path(workspace) / "root"
            (root / "share").mkdir(parents=True)
            (root / "share" / "npu sdk metadata.json").write_text("{}")
            archive = Path(workspace) / "a.tar"
            subprocess.run(
                ["tar", "-cf", str(archive), "-C", str(root), "."], check=True
            )

            members = artifact_versions.list_members(archive)

        self.assertIn("./share/npu sdk metadata.json", members)


class AtomicWriteTest(VersionFileTestCase):
    """A record is either absent or complete"""

    def test_no_scratch_file_is_left_behind(self):
        with tempfile.TemporaryDirectory() as workspace:
            destination = Path(workspace) / "out" / version_file.FILE_NAME
            version_file.write({"schema": 1}, [destination])

            self.assertTrue(destination.is_file())
            self.assertEqual(
                [p.name for p in destination.parent.iterdir()], [version_file.FILE_NAME]
            )

    def test_an_existing_record_is_replaced_whole(self):
        with tempfile.TemporaryDirectory() as workspace:
            destination = Path(workspace) / version_file.FILE_NAME
            destination.write_text("stale, much longer than what replaces it\n")
            version_file.write({"schema": 1}, [destination])

            self.assertEqual(json.loads(destination.read_text()), {"schema": 1})


class UnreadableArtifactTest(unittest.TestCase):
    """A tooling failure is reported, not read as an empty archive"""

    def test_listing_an_unreadable_archive_warns(self):
        missing = Path("/tmp/fboss-distro-version-no-such-archive.tar")

        with self.assertLogs(
            "distro_cli.lib.artifact_versions", level="WARNING"
        ) as logged:
            members = artifact_versions.list_members(missing)

        self.assertEqual(members, [])
        self.assertIn("report no versions", "".join(logged.output))


class ObservableSkipTest(VersionFileTestCase):
    """Evidence this code declines to read is reported, never dropped silently"""

    READER_LOG = "distro_cli.lib.artifact_versions"

    def test_an_unreadable_sdk_member_warns(self):
        """Otherwise a failed read looks like an artifact shipping no metadata,
        and the SAI mismatch is never reported."""
        with tempfile.TemporaryDirectory() as workspace:
            archive = Path(workspace) / "a.tar"
            archive.write_text("not a tar")

            with self.assertLogs(self.READER_LOG, level="WARNING") as logged:
                sdk = artifact_versions.read_sdk(
                    archive, [artifact_versions.SDK_METADATA_MEMBER]
                )

        self.assertEqual(sdk, {})
        self.assertIn("Could not read", "".join(logged.output))

    def test_truncating_the_member_list_warns(self):
        members = [f"./p{i:03d}.rpm" for i in range(artifact_versions._MAX_MEMBERS + 5)]

        with self.assertLogs(self.READER_LOG, level="WARNING") as logged:
            artifact_versions.read_packages(Path("/tmp/none.tar"), members)

        self.assertIn("report no versions", "".join(logged.output))


class ExtractionSafetyTest(unittest.TestCase):
    """Member names come from the archive, so they are not trusted as paths"""

    def test_a_member_escaping_the_destination_is_refused(self):
        for member in ("../escaped.ko", "a/../../escaped.ko", "/etc/passwd"):
            self.assertFalse(
                artifact_versions._contained(member, Path("/tmp/work")),
                f"{member} should be refused",
            )

    def test_an_ordinary_member_is_allowed(self):
        for member in ("./share/x.json", "lib/modules/a.ko", "a/../b.ko"):
            self.assertTrue(
                artifact_versions._contained(member, Path("/tmp/work")),
                f"{member} should be allowed",
            )

    def test_an_escaping_member_is_not_extracted_and_is_reported(self):
        """A crafted archive must not write outside the workspace."""
        with tempfile.TemporaryDirectory() as workspace:
            dest = Path(workspace) / "dest"
            dest.mkdir()

            with self.assertLogs(
                "distro_cli.lib.artifact_versions", level="WARNING"
            ) as logged:
                extracted = artifact_versions._extract(
                    Path("/tmp/no-such-archive.tar"), ["../escaped.ko"], dest
                )

            self.assertEqual(extracted, [])
            self.assertIn("refusing to extract", "".join(logged.output))
            self.assertFalse((Path(workspace) / "escaped.ko").exists())


class MalformedEvidenceTest(unittest.TestCase):
    """Metadata an artifact ships is external input, not a contract"""

    def test_a_non_dict_sdk_version_does_not_crash_the_build(self):
        """An artifact's metadata file is read verbatim, so a scalar where a
        dict was expected must yield nothing rather than raise."""
        provenance = {"sdk": {"agent": {"sdkVersion": "not-a-dict"}}}

        self.assertIsNone(version_file.sai_from_sdk(provenance))
        self.assertIsNone(version_file.native_sdk_from_sdk(provenance))

    def test_a_malformed_entry_does_not_hide_a_well_formed_one(self):
        provenance = {
            "sdk": {
                "broken": {"sdkVersion": ["also", "not", "a", "dict"]},
                "agent": {"sdkVersion": {"saiSdk": "14.2.0.0_odp"}},
            }
        }

        self.assertEqual(version_file.sai_from_sdk(provenance), "14.2.0.0_odp")


class NarrowingTest(unittest.TestCase):
    """Reader output is a convention, so a malformed record is dropped"""

    def test_a_reader_that_returned_the_wrong_shape_reports_nothing(self):
        """The record is read from an artifact, so its shape is not guaranteed."""
        for provenance in (
            {"packages": "not-a-list"},
            {"packages": [None, 7, "text"]},
            {"modules": {"not": "a list"}},
        ):
            self.assertIsNone(version_file.kernel_from_packages(provenance))
            self.assertIsNone(version_file.kernel_from_modules(provenance))
            self.assertIsNone(version_file.bsp_from_packages(provenance))

    def test_a_malformed_record_does_not_hide_a_well_formed_one(self):
        provenance = {
            "packages": [
                "not-a-dict",
                {"kernel_requires": ["kernel-core = 1:6.11.1-1.fboss.el9"]},
            ]
        }

        self.assertEqual(
            version_file.kernel_from_packages(provenance), "6.11.1-1.fboss.el9"
        )

    def test_a_field_written_as_a_scalar_reads_the_same_as_a_list(self):
        """kernel_requires is a list and vermagic a scalar, read the same way."""
        scalar = {"modules": [{"vermagic": "6.11.1-1.fboss.el9.x86_64 SMP"}]}
        listed = {"modules": [{"vermagic": ["6.11.1-1.fboss.el9.x86_64 SMP"]}]}

        self.assertEqual(
            version_file.kernel_from_modules(scalar),
            version_file.kernel_from_modules(listed),
        )


class ClaimsTest(unittest.TestCase):
    """What a locator states, per the component's schema"""

    def test_kernel_release_claimed_by_a_versioned_path(self):
        self.assertEqual(
            version_file.versions_claimed_by(
                "kernel", "manifold:x/kernel/6.11.1/kernel-6.11.1.rpms.tar"
            ),
            {"kernel": "6.11.1"},
        )

    def test_sai_release_claimed_by_an_agent_path(self):
        self.assertEqual(
            version_file.versions_claimed_by(
                "fboss-forwarding-stack", "manifold:x/agent/brcm/14.2.0.0_odp/f.tar.zst"
            ),
            {"sai": "14.2.0.0_odp"},
        )

    def test_a_flat_path_claims_nothing(self):
        self.assertEqual(
            version_file.versions_claimed_by("kernel", "file:/tmp/staging/k.tar"), {}
        )

    def test_a_component_only_claims_what_its_schema_declares(self):
        """The forwarding stack path has no kernel claim to make."""
        claimed = version_file.versions_claimed_by(
            "fboss-forwarding-stack", "manifold:x/agent/brcm/14.2.0.0_odp/f.tar.zst"
        )
        self.assertNotIn("kernel", claimed)


class ContradictionTest(VersionFileTestCase):
    """A claim the artifact disagrees with"""

    def test_a_contradicted_claim_is_reported(self):
        artifact = self.agent(sai="13.3.0.0_odp")
        provenance = version_file.read_artifact("fboss-forwarding-stack", artifact)

        self.assertEqual(
            version_file.contradictions(
                "fboss-forwarding-stack",
                "manifold:x/agent/brcm/14.2.0.0_odp/agent.tar.zst",
                provenance,
            ),
            ["sai=14.2.0.0_odp"],
        )

    def test_a_corroborated_claim_is_not_reported(self):
        artifact = self.agent(sai="14.2.0.0_odp")
        provenance = version_file.read_artifact("fboss-forwarding-stack", artifact)

        self.assertEqual(
            version_file.contradictions(
                "fboss-forwarding-stack",
                "manifold:x/agent/brcm/14.2.0.0_odp/agent.tar.zst",
                provenance,
            ),
            [],
        )

    def test_a_claim_the_artifact_cannot_check_is_left_alone(self):
        """Reporting nothing is not disagreement."""
        artifact = make_tar(self.root / "empty.tar", {"bin/x": b""})
        self.assertEqual(
            version_file.contradictions("kernel", "manifold:x/kernel/6.11.1/k.tar", {}),
            [],
        )
        self.assertEqual(version_file.read_artifact("kernel", artifact), {})


class ReadArtifactTest(VersionFileTestCase):
    """Reading only what the component's schema asks for"""

    def test_reads_the_reader_its_schema_names(self):
        provenance = version_file.read_artifact("fboss-forwarding-stack", self.agent())
        self.assertIn("sdk", provenance)

    def test_does_not_read_a_reader_its_schema_omits(self):
        """The SDK member is present but kernel's schema does not read it."""
        artifact = make_tar(self.root / "k.tar", {SDK_METADATA_MEMBER: sdk_metadata()})
        self.assertNotIn("sdk", version_file.read_artifact("kernel", artifact))

    def test_platform_stack_without_a_declaration_reports_nothing(self):
        self.assertEqual(
            version_file.read_artifact("fboss-platform-stack", self.agent()), {}
        )

    def test_service_build_records_are_preserved_in_the_distro_record(self):
        declaration = {
            "declaration_schema": 1,
            "producer": "package.py",
            "binaries": {
                "fsdb": {
                    "schema": 1,
                    "build_target": "fsdb",
                    "source_revision": "a" * 40,
                    "sha256": "b" * 64,
                },
                "fan_service": {
                    "schema": 1,
                    "build_target": "fan_service",
                    "source_revision": "c" * 40,
                    "source_dirty": True,
                    "sha256": "d" * 64,
                },
            },
        }
        artifact = make_tar(
            self.root / "services.tar",
            {artifact_versions.BUILD_INFO_MEMBER: json.dumps(declaration).encode()},
        )
        for label in (
            "fboss-forwarding-stack[agent]",
            "fboss-forwarding-stack[fsdb]",
            "fboss-forwarding-stack[qsfp]",
            "fboss-forwarding-stack[fboss2]",
            "fboss-forwarding-stack[led]",
            "fboss-platform-stack",
        ):
            with self.subTest(component=label):
                record, conflicts = version_file.build(
                    manifest_path=self.manifest(),
                    manifest_data={},
                    component_artifacts={label: artifact},
                    source_revision="image-revision",
                )
                self.assertEqual(
                    record["components"][label]["provenance"]["build_info"],
                    declaration,
                )
                self.assertEqual(record["image"]["source_revision"], "image-revision")
                self.assertEqual(conflicts, [])

    def test_kernel_build_evidence_is_collected_without_replacing_rpm_release(self):
        declaration = {
            "declaration_schema": 1,
            "producer": "build_kernel.sh",
            "build": {
                "upstream": {
                    "version": "6.11.1",
                    "source_archive": {"sha256": "a" * 64},
                },
                "build_version": "kernel-42",
                "inputs": {"kernel.spec": "b" * 64},
                "config_sha256": "c" * 64,
            },
        }
        artifact = make_tar(
            self.root / "kernel.tar",
            {
                artifact_versions.KERNEL_BUILD_INFO_MEMBER: json.dumps(
                    declaration
                ).encode(),
            },
        )
        packages = [{"nevra": "kernel-core-6.11.1-1.fboss.el9.x86_64"}]
        with mock.patch.dict(READERS, {"packages": lambda artifact, members: packages}):
            record, conflicts = version_file.build(
                manifest_path=self.manifest(),
                manifest_data={},
                component_artifacts={"kernel": artifact},
                source_revision="assembler",
            )
        self.assertEqual(
            record["components"]["kernel"]["provenance"]["build_info"], declaration
        )
        self.assertEqual(
            record["components"]["kernel"]["provenance"]["packages"], packages
        )
        self.assertEqual(record["versions"]["kernel"], "6.11.1-1.fboss.el9")
        self.assertEqual(record["image"]["source_revision"], "assembler")
        self.assertEqual(conflicts, [])

    def test_legacy_kernel_artifact_keeps_rpm_evidence_without_build_info(self):
        artifact = make_tar(
            self.root / "legacy-kernel.tar", {"kernel-core.rpm": b"fixture"}
        )
        packages = [{"nevra": "kernel-core-6.11.1-1.fboss.el9.x86_64"}]
        with mock.patch.dict(READERS, {"packages": lambda artifact, members: packages}):
            self.assertEqual(
                version_file.read_artifact("kernel", artifact), {"packages": packages}
            )

    def test_bsp_build_evidence_is_preserved_with_rpm_versions(self):
        declaration = {
            "declaration_schema": 1,
            "producer": "component_version.py",
            "build": {
                "source_revision": "a" * 40,
                "source_dirty": False,
                "fboss": {"source_revision": "b" * 40, "source_dirty": True},
                "kernel_release": "6.11.1-1.fboss.el9.x86_64",
                "rpm_sha256": {"fboss_bsp_kmods.rpm": "c" * 64},
            },
            "versions": {"bsp": "4.4.2-1", "kernel": "6.11.1-1.fboss.el9"},
        }
        packages = [
            {"nevra": "fboss_bsp_kmods-6.11.1-1.fboss.el9.x86_64-4.4.2-1.x86_64"}
        ]
        for rpm_member in (
            "fboss_bsp_kmods.rpm",
            "usr/local/share/local_rpm_repo/fboss_bsp_kmods.rpm",
        ):
            for label in ("bsps", "bsps[fboss]"):
                with self.subTest(rpm_member=rpm_member, component=label):
                    artifact = make_tar(
                        self.root / "bsp.tar",
                        {
                            rpm_member: b"fixture",
                            artifact_versions.BSP_BUILD_INFO_MEMBER: json.dumps(
                                declaration
                            ).encode(),
                        },
                    )
                    with mock.patch.dict(
                        READERS, {"packages": lambda artifact, members: packages}
                    ):
                        record, conflicts = version_file.build(
                            manifest_path=self.manifest(),
                            manifest_data={},
                            component_artifacts={label: artifact},
                            source_revision="assembler",
                        )
                    self.assertEqual(
                        record["components"][label]["provenance"],
                        {"packages": packages, "build_info": declaration},
                    )
                    self.assertEqual(record["versions"], declaration["versions"])
                    self.assertEqual(record["image"]["source_revision"], "assembler")
                    self.assertEqual(conflicts, [])

    def test_legacy_bsp_artifact_keeps_rpm_evidence_without_build_info(self):
        artifact = make_tar(
            self.root / "legacy-bsp.tar", {"fboss_bsp_kmods.rpm": b"fixture"}
        )
        packages = [{"nevra": "fboss_bsp_kmods-4.4.2-1.x86_64"}]
        with mock.patch.dict(READERS, {"packages": lambda artifact, members: packages}):
            self.assertEqual(
                version_file.read_artifact("bsps", artifact), {"packages": packages}
            )

    def test_invalid_or_ambiguous_bsp_declarations_keep_rpm_evidence(self):
        declaration = b'{"declaration_schema":1,"build":{}}'
        packages = [{"nevra": "fboss_bsp_kmods-4.4.2-1.x86_64"}]
        for metadata in (
            {artifact_versions.BSP_BUILD_INFO_MEMBER: b"not json"},
            {
                artifact_versions.BSP_BUILD_INFO_MEMBER: b'{"declaration_schema":2,"build":{}}'
            },
            {
                artifact_versions.BSP_BUILD_INFO_MEMBER: declaration,
                artifact_versions.KERNEL_BUILD_INFO_MEMBER: declaration,
            },
        ):
            with self.subTest(metadata=metadata):
                artifact = make_tar(
                    self.root / "invalid-bsp.tar",
                    {"fboss_bsp_kmods.rpm": b"fixture", **metadata},
                )
                with (
                    mock.patch.dict(
                        READERS, {"packages": lambda artifact, members: packages}
                    ),
                    self.assertLogs("distro_cli.lib.artifact_versions", "WARNING"),
                ):
                    self.assertEqual(
                        version_file.read_artifact("bsps", artifact),
                        {"packages": packages},
                    )

    def test_invalid_build_declarations_are_ignored(self):
        for content in (
            b"not json",
            b'{"declaration_schema":2,"binaries":{}}',
            b'{"declaration_schema":1,"binaries":[]}',
            b'{"declaration_schema":1,"build":[]}',
            b'{"declaration_schema":1,"build":{},"binaries":[]}',
            b'{"declaration_schema":1}',
        ):
            with self.subTest(content=content):
                artifact = make_tar(
                    self.root / "invalid.tar",
                    {artifact_versions.BUILD_INFO_MEMBER: content},
                )
                with self.assertLogs("distro_cli.lib.artifact_versions", "WARNING"):
                    self.assertEqual(
                        version_file.read_artifact("fboss-platform-stack", artifact),
                        {},
                    )


class LocatorTest(VersionFileTestCase):
    """Resolving what the manifest asked for"""

    def test_scalar_component_has_one_locator(self):
        data = {"kernel": {"download": "manifold:x/kernel/6.11.1/k.tar"}}
        self.assertEqual(
            [
                version_file.locator_of(e)
                for e in version_file.entries_for(data, "kernel")
            ],
            ["manifold:x/kernel/6.11.1/k.tar"],
        )

    def test_list_component_has_one_locator_per_artifact_in_order(self):
        data = {
            "fboss-forwarding-stack": [
                {"download": "x/agent/brcm/14.2.0.0_odp/a.tar"},
                {"download": "x/fsdb/b.tar"},
            ]
        }
        self.assertEqual(
            [
                version_file.locator_of(e)
                for e in version_file.entries_for(data, "fboss-forwarding-stack")
            ],
            ["x/agent/brcm/14.2.0.0_odp/a.tar", "x/fsdb/b.tar"],
        )

    def test_built_component_records_its_script(self):
        data = {"kernel": {"execute": ["kernel/build.sh", "6.11.1"]}}
        self.assertEqual(
            [
                version_file.locator_of(e)
                for e in version_file.entries_for(data, "kernel")
            ],
            ["kernel/build.sh"],
        )

    def test_component_absent_from_the_manifest(self):
        self.assertEqual(
            [
                version_file.locator_of(e)
                for e in version_file.entries_for({}, "kernel")
            ],
            [None],
        )

    def test_empty_component_has_no_locator(self):
        self.assertEqual(
            [
                version_file.locator_of(e)
                for e in version_file.entries_for({"phy_sai": {}}, "phy_sai")
            ],
            [None],
        )


class LabelTest(unittest.TestCase):
    """How an artifact of a component is named in the record"""

    def test_a_single_artifact_is_recorded_under_the_component_name(self):
        entry = {"download": "x/k.tar"}
        self.assertEqual(version_file.label_for("kernel", entry, 0, 1), "kernel")

    def test_several_artifacts_are_recorded_under_the_names_the_manifest_gives(self):
        entry = {"name": "agent", "download": "x/agent/brcm/14.2.0.0_odp/f.tar.zst"}
        self.assertEqual(
            version_file.label_for("fboss-forwarding-stack", entry, 0, 4),
            "fboss-forwarding-stack[agent]",
        )

    def test_an_unnamed_artifact_falls_back_to_its_position(self):
        """Manifests predating names still produce a stable label."""
        self.assertEqual(
            version_file.label_for("fboss-forwarding-stack", {"download": "x"}, 2, 4),
            "fboss-forwarding-stack[2]",
        )

    def test_a_name_that_is_not_a_string_is_ignored(self):
        self.assertEqual(
            version_file.label_for("fboss-forwarding-stack", {"name": 7}, 1, 2),
            "fboss-forwarding-stack[1]",
        )

    def test_a_named_label_still_resolves_to_the_component_schema(self):
        """The schema is keyed on the component, not the label."""
        self.assertEqual(
            version_file.schema_for("fboss-forwarding-stack[agent]"),
            version_file.schema_for("fboss-forwarding-stack"),
        )


class DescribeComponentsTest(VersionFileTestCase):
    """One record per staged artifact"""

    def test_records_source_artifact_and_provenance(self):
        data = {
            "fboss-forwarding-stack": {
                "download": "manifold:x/agent/brcm/14.2.0.0_odp/agent.tar.zst"
            }
        }
        records = version_file.describe_components(
            data, {"fboss-forwarding-stack": self.agent()}
        )
        record = records["fboss-forwarding-stack"]

        self.assertEqual(
            record["source"]["locator"],
            "manifold:x/agent/brcm/14.2.0.0_odp/agent.tar.zst",
        )
        self.assertEqual(record["artifact"]["name"], "agent.tar.zst")
        self.assertTrue(record["artifact"]["sha256"].startswith("sha256:"))
        self.assertIn("sdk", record["provenance"])

    def test_a_named_list_component_records_each_artifact_under_its_name(self):
        data = {
            "fboss-forwarding-stack": [
                {"name": "agent", "download": "x/agent/brcm/14.2.0.0_odp/a.tar"},
                {"name": "fsdb", "download": "x/fsdb/b.tar"},
            ]
        }
        records = version_file.describe_components(
            data,
            {"fboss-forwarding-stack": [self.agent("a.tar"), self.agent("b.tar")]},
        )

        self.assertEqual(
            records["fboss-forwarding-stack[agent]"]["source"]["locator"],
            "x/agent/brcm/14.2.0.0_odp/a.tar",
        )
        self.assertEqual(
            records["fboss-forwarding-stack[fsdb]"]["source"]["locator"],
            "x/fsdb/b.tar",
        )

    def test_a_contradicted_claim_is_recorded_not_hidden(self):
        data = {
            "fboss-forwarding-stack": {
                "download": "manifold:x/agent/brcm/14.2.0.0_odp/agent.tar.zst"
            }
        }
        records = version_file.describe_components(
            data, {"fboss-forwarding-stack": self.agent(sai="13.3.0.0_odp")}
        )

        self.assertEqual(
            records["fboss-forwarding-stack"]["source"]["unsupported_claims"],
            ["sai=14.2.0.0_odp"],
        )

    def test_an_artifact_reporting_nothing_still_records_its_checksum(self):
        artifact = make_tar(self.root / "fsdb.tar.zst", {"bin/fsdb": b""})
        records = version_file.describe_components(
            {"fboss-forwarding-stack": {"download": "x/fsdb/fsdb.tar.zst"}},
            {"fboss-forwarding-stack": artifact},
        )
        record = records["fboss-forwarding-stack"]

        self.assertTrue(record["artifact"]["sha256"].startswith("sha256:"))
        self.assertNotIn("provenance", record)

    def test_components_that_were_not_staged_are_skipped(self):
        self.assertEqual(version_file.describe_components({}, {"kernel": None}), {})


class ResolveVersionsTest(VersionFileTestCase):
    """The versions a reader wants first"""

    def test_each_version_comes_from_a_component_declared_to_report_it(self):
        components = {
            "kernel": {
                "provenance": {
                    "packages": [
                        {"kernel_requires": ["kernel-core = 1:6.11.1-1.fboss.el9"]}
                    ]
                }
            },
            "bsps": {
                "provenance": {
                    "packages": [{"nevra": "fboss_bsp_kmods-4.4.2-1.x86_64"}]
                }
            },
            "fboss-forwarding-stack[agent]": {
                "provenance": {
                    "sdk": {
                        "agent": {
                            "sdkVersion": {
                                "saiSdk": "14.2.0.0_odp",
                                "asicSdk": "6.5.34",
                            }
                        }
                    }
                }
            },
        }

        self.assertEqual(
            version_file.resolve_versions(components)[0],
            {
                "kernel": "6.11.1-1.fboss.el9",
                "bsp": "4.4.2-1",
                "sai": "14.2.0.0_odp",
                "native_sdk": "6.5.34",
            },
        )

    def test_a_version_is_not_taken_from_a_component_that_does_not_declare_it(self):
        """The kernel RPM has a NEVRA, but kernel does not report a bsp."""
        components = {
            "kernel": {
                "provenance": {"packages": [{"nevra": "kernel-6.11.1-1.x86_64"}]}
            }
        }

        self.assertNotIn("bsp", version_file.resolve_versions(components)[0])

    def test_no_components_resolves_to_no_versions(self):
        self.assertEqual(version_file.resolve_versions({}), ({}, []))


class PackagingTest(unittest.TestCase):
    """The file has to be written before the image is built to be in it"""

    def source(self) -> str:
        return (
            Path(__file__).resolve().parents[1] / "builder" / "image_builder.py"
        ).read_text()

    def test_the_version_file_is_written_before_the_container_runs(self):
        """The container copies it into the rootfs, so a later write misses it.

        This cannot be caught by calling the writer directly, which is how the
        file came to be written after the image was already sealed.
        """
        body = self.source()
        wrote = body.index("self._write_version_file()")
        built = body.index("exit_code = run_container(")

        self.assertLess(
            wrote,
            built,
            "the version file must be written before the image is built, or it "
            "will not be in the image",
        )

    def test_the_version_file_is_not_written_into_the_checked_in_rootfs(self):
        """A generated file under root_files/ would ship in the *next* image."""
        writer = self.source()
        start = writer.index("def _write_version_file")
        end = writer.index("def ", start + 10)

        self.assertNotIn("root_files", writer[start:end])


class BuildAndWriteTest(VersionFileTestCase):
    """The file itself"""

    def build(self, **overrides) -> dict:
        # Annotated because a component names either one artifact or a list of
        # them, which an inferred dict[str, Path] would not accept.
        artifacts: dict[str, object] = {"fboss-forwarding-stack": self.agent()}
        kwargs = {
            "manifest_path": self.manifest(),
            "manifest_data": {
                "fboss-forwarding-stack": {
                    "download": "manifold:x/agent/brcm/14.2.0.0_odp/agent.tar.zst"
                }
            },
            "component_artifacts": artifacts,
            "source_revision": "06ce17a628aa494f1caad0ca4f911979b9e79aad",
        }
        kwargs.update(overrides)
        return version_file.build(**kwargs)[0]

    def test_the_record_answers_what_the_image_is(self):
        record = self.build()

        self.assertEqual(record["schema"], version_file.SCHEMA)
        self.assertEqual(record["image"]["manifest"], "6.11.1_xgs_6_5_34.json")
        self.assertTrue(record["image"]["manifest_sha256"].startswith("sha256:"))
        self.assertEqual(
            record["image"]["source_revision"],
            "06ce17a628aa494f1caad0ca4f911979b9e79aad",
        )
        self.assertEqual(record["versions"]["sai"], "14.2.0.0_odp")
        self.assertEqual(record["versions"]["native_sdk"], "6.5.34")

    def test_a_gap_shows_on_the_component_that_would_have_filled_it(self):
        """No separate list: a silent artifact simply carries no provenance."""
        artifacts: dict[str, object] = {"fboss-platform-stack": self.agent()}
        record = self.build(
            manifest_data={"fboss-platform-stack": {"download": "x/platform.tar.zst"}},
            component_artifacts=artifacts,
        )

        self.assertNotIn("versions_missing", record)
        self.assertNotIn("provenance", record["components"]["fboss-platform-stack"])
        self.assertEqual(record["versions"], {})

    def test_writes_the_same_bytes_to_every_destination(self):
        record = self.build()
        beside = self.root / "out" / version_file.FILE_NAME
        rootfs = self.root / "root_files" / "etc" / version_file.FILE_NAME

        version_file.write(record, [beside, rootfs])

        self.assertEqual(beside.read_text(), rootfs.read_text())
        self.assertEqual(json.loads(beside.read_text())["schema"], version_file.SCHEMA)

    def test_the_written_file_is_stable_across_runs(self):
        destination = self.root / version_file.FILE_NAME

        version_file.write(self.build(), [destination])
        first = destination.read_text()
        version_file.write(self.build(), [destination])

        self.assertEqual(first, destination.read_text())


if __name__ == "__main__":
    unittest.main()
