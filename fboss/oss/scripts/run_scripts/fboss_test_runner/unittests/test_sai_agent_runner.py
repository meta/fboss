# Copyright Meta Platforms, Inc. and affiliates.
# @noautodeps
"""Tests for SaiAgentTestRunner.

Covers the mono vs multi_switch branching that drives binary, warmboot file,
SAI replayer flags, and platform-mapping override placement, plus the
production-feature filtering implemented in _filter_tests."""

import json
from unittest.mock import MagicMock, patch

import pytest
from fboss_test_runner.runners.sai_agent_test_runner import (
    SaiAgentTestRunner,
    sdk_version_key,
)


@pytest.fixture
def sai_agent_runner():
    return SaiAgentTestRunner()


def _make_args(**overrides):
    args = MagicMock()
    args.agent_run_mode = "multi_switch"
    args.num_npus = 1
    args.platform_mapping_override_path = None
    args.mgmt_if = "eth0"
    args.enable_production_features = None
    args.list_tests_for_features = None
    args.production_features = None
    args.test_config_options = None
    args.known_bad_tests_file = None
    args.unsupported_tests_file = None
    for k, v in overrides.items():
        setattr(args, k, v)
    return args


class TestWarmbootCheckFile:
    """Mono uses switch_index=0 (can_warm_boot_0); multi_switch uses
    switch_index=None (can_warm_boot). Mismatched paths silently skip the
    warmboot."""

    def test_mono_uses_switch_index_zero(self, sai_agent_runner):
        with patch.object(
            sai_agent_runner, "args", new=_make_args(agent_run_mode="mono")
        ):
            assert sai_agent_runner._get_warmboot_check_file().endswith("_0")

    def test_multi_switch_uses_no_switch_index(self, sai_agent_runner):
        with patch.object(
            sai_agent_runner, "args", new=_make_args(agent_run_mode="multi_switch")
        ):
            # multi_switch path must NOT have the _<index> suffix.
            path = sai_agent_runner._get_warmboot_check_file()
            assert not path.endswith("_0")


class TestBinaryName:
    def test_multi_switch_binary_name(self, sai_agent_runner):
        with patch.object(
            sai_agent_runner,
            "args",
            new=_make_args(agent_run_mode="multi_switch"),
        ):
            assert (
                sai_agent_runner._get_test_binary_name() == "multi_switch_agent_hw_test"
            )

    def test_mono_binary_name(self, sai_agent_runner):
        with patch.object(
            sai_agent_runner,
            "args",
            new=_make_args(agent_run_mode="mono"),
        ):
            assert (
                sai_agent_runner._get_test_binary_name() == "sai_agent_hw_test-sai_impl"
            )


class TestSaiReplayerLoggingFlagsByMode:
    """In multi_switch the SAI replayer is configured via the systemd unit
    file (hw agent runs as a service), so the test binary must NOT receive
    --enable-replayer flags. In mono the flags go on the binary."""

    def test_multi_switch_returns_empty_when_path_set(self, sai_agent_runner):
        with patch.object(
            sai_agent_runner, "args", new=_make_args(agent_run_mode="multi_switch")
        ):
            assert sai_agent_runner._get_sai_replayer_logging_flags("/tmp/replay") == []

    def test_mono_returns_replayer_flags_when_path_set(self, sai_agent_runner):
        with patch.object(
            sai_agent_runner, "args", new=_make_args(agent_run_mode="mono")
        ):
            flags = sai_agent_runner._get_sai_replayer_logging_flags("/tmp/replay")
            assert "--enable-replayer" in flags
            assert "--sai-log" in flags
            assert "/tmp/replay" in flags

    def test_returns_empty_when_path_none(self, sai_agent_runner):
        # Regardless of mode, no path → no flags.
        with patch.object(
            sai_agent_runner, "args", new=_make_args(agent_run_mode="mono")
        ):
            assert sai_agent_runner._get_sai_replayer_logging_flags(None) == []


class TestTestRunArgsByMode:
    """--platform_mapping_override_path is added to the binary args ONLY in
    mono mode — multi_switch passes it via the systemd unit file instead."""

    def test_mono_includes_platform_mapping_override(self, sai_agent_runner):
        with patch.object(
            sai_agent_runner,
            "args",
            new=_make_args(
                agent_run_mode="mono",
                platform_mapping_override_path="/tmp/pm.json",
            ),
        ):
            args_list = sai_agent_runner._get_test_run_args("dummy.conf")
            assert "--platform_mapping_override_path" in args_list
            assert "/tmp/pm.json" in args_list

    def test_multi_switch_omits_platform_mapping_override(self, sai_agent_runner):
        with patch.object(
            sai_agent_runner,
            "args",
            new=_make_args(
                agent_run_mode="multi_switch",
                platform_mapping_override_path="/tmp/pm.json",
            ),
        ):
            args_list = sai_agent_runner._get_test_run_args("dummy.conf")
            # Override must NOT appear; multi_switch routes it via systemd unit.
            assert "--platform_mapping_override_path" not in args_list
            assert "/tmp/pm.json" not in args_list

    def test_forwards_num_npus_to_test_binary(self, sai_agent_runner):
        with patch.object(sai_agent_runner, "args", new=_make_args(num_npus=2)):
            args_list = sai_agent_runner._get_test_run_args("dummy.conf")
            assert "--undefok=num_npus_for_testing" in args_list
            assert "--num_npus_for_testing" in args_list
            assert args_list[args_list.index("--num_npus_for_testing") + 1] == "2"


class TestFilterTestsProductionFeatures:
    """--enable-production-features ASIC: load asicToFeatureNames from the
    JSON, then per-test invoke the binary with --list_production_feature and
    keep tests whose feature set is a subset of the ASIC's features."""

    def _features_json(self, tmp_path, mapping):
        path = tmp_path / "features.json"
        path.write_text(json.dumps({"asicToFeatureNames": mapping}))
        return str(path)

    def test_keeps_tests_whose_features_are_subset_of_asic(
        self, sai_agent_runner, tmp_path
    ):
        path = self._features_json(
            tmp_path, {"tomahawk5": ["ACL_COUNTER", "DLB", "SINGLE_ACL_TABLE"]}
        )

        # First test reports features that are a subset → keep.
        # Second test reports a feature NOT in the ASIC → drop.
        def fake_run(cmd, **_):
            test = next(
                arg.split("=", 1)[1] for arg in cmd if arg.startswith("--gtest_filter=")
            )
            if test == "AgentAclTest.OK":
                stdout = "Feature List: ACL_COUNTER\n"
            else:
                stdout = "Feature List: NOT_SUPPORTED_FEATURE\n"
            return MagicMock(stdout=stdout)

        with (
            patch.object(
                sai_agent_runner,
                "args",
                new=_make_args(
                    agent_run_mode="multi_switch",
                    enable_production_features="tomahawk5",
                    production_features=path,
                ),
            ),
            patch("subprocess.run", side_effect=fake_run),
        ):
            kept = sai_agent_runner._filter_tests(
                ["AgentAclTest.OK", "AgentExoticTest.Bad"]
            )
        assert kept == ["AgentAclTest.OK"]

    def test_no_filter_when_neither_flag_set(self, sai_agent_runner):
        with patch.object(
            sai_agent_runner,
            "args",
            new=_make_args(agent_run_mode="multi_switch"),
        ):
            tests = ["AgentA.t", "AgentB.t"]
            assert sai_agent_runner._filter_tests(tests) == tests


class TestSdkGatedProductionFeatures:
    # Feature ids mirror production_features.thrift: MIRROR_ON_DROP=49, SRV6_ENCAP=100
    FEATURES = {
        "asicToFeatures": {"yuba": [3, 49], "tomahawk5": [3, 100]},
        "asicToFeatureNames": {
            "yuba": ["L3_FORWARDING", "MIRROR_ON_DROP"],
            "tomahawk5": ["L3_FORWARDING", "SRV6_ENCAP"],
        },
        "featureSupportSpecs": [
            {
                "feature": 49,
                "supportedFrom": [
                    {"asics": ["yuba"], "sdk": 2, "minSdkVersion": "26.2.4210"}
                ],
            },
            {
                "feature": 100,
                "supportedFrom": [
                    {
                        "asics": ["tomahawk5"],
                        "sdk": 1,
                        "minSdkVersion": "15.4_ea_odp",
                        "requiredTestConfigOptions": ["rbb"],
                    }
                ],
            },
        ],
    }

    @pytest.mark.parametrize(
        "asic,sai_sdk,options,gated_feature",
        [
            ("yuba", "25.11.4210-2", None, "MIRROR_ON_DROP"),
            ("yuba", "26.2.4210-1", None, None),
            ("tomahawk5", "15.4.0.0_odp", None, "SRV6_ENCAP"),
            ("tomahawk5", "15.4.0.0_odp", "rbb", None),
            ("tomahawk5", "14.2.0.0_odp", "rbb", "SRV6_ENCAP"),
            # Unknown running SDK: no gating
            ("yuba", "unknown", None, None),
            ("yuba", None, None, None),
        ],
    )
    def test_sdk_gating(
        self,
        sai_agent_runner,
        tmp_path,
        monkeypatch,
        asic,
        sai_sdk,
        options,
        gated_feature,
    ):
        monkeypatch.setenv("FBOSS_DATA", str(tmp_path))
        if sai_sdk:
            (tmp_path / "npu_sdk_metadata.json").write_text(
                json.dumps(
                    {
                        "fboss_hw_agent-sai_impl": {
                            "sdkVersion": {"asicSdk": sai_sdk, "saiSdk": sai_sdk}
                        }
                    }
                )
            )
        with patch.object(
            sai_agent_runner, "args", new=_make_args(test_config_options=options)
        ):
            supported = sai_agent_runner._get_sdk_gated_production_features(
                asic, self.FEATURES
            )
        expected = set(self.FEATURES["asicToFeatureNames"][asic]) - {gated_feature}
        assert supported == expected


@pytest.mark.parametrize(
    "vendor_sdk,running,minimum,expected",
    [
        # BRCM SAI: EA and GA of the same release compare equal, as in netcastle
        (1, "15.4_ea_odp", "15.4.0.0_odp", True),
        (1, "14.2.0.0_odp", "15.4_ea_odp", False),
        (1, "16.0_ea_dnx_odp", "16.0", True),
        # Leaba: OSS records a "-N" packaging revision
        (2, "26.2.4210-1", "26.2.4210", True),
        (2, "25.11.4210-2", "26.2.4210", False),
        # Nvidia: "_" separated components count
        (3, "2511.36_0_20", "2511.36_0_21", False),
        (3, "2605.37", "2511.36", True),
    ],
)
def test_sdk_version_key(vendor_sdk, running, minimum, expected):
    assert (
        sdk_version_key(vendor_sdk, running) >= sdk_version_key(vendor_sdk, minimum)
    ) is expected


@pytest.mark.parametrize(
    "vendor_sdk,version",
    [(1, "16"), (1, "15.4-1"), (2, "26.2"), (2, "unknown"), (3, "unknown"), (4, "1.0")],
)
def test_sdk_version_key_rejects_malformed(vendor_sdk, version):
    with pytest.raises(ValueError):
        sdk_version_key(vendor_sdk, version)
