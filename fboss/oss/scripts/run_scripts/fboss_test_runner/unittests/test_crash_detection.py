#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.

"""Unit tests for fboss_test_runner.crash_detection."""

import json
from unittest.mock import MagicMock, patch

import fboss_test_runner.crash_detection as _mod
import pytest
from fboss_test_runner.crash_detection import (
    core_is_from,
    find_unclean_unit_exits,
    list_core_dumps,
)


def _journal(*messages: str, stamp_usec: int = 5_000_000_000) -> str:
    """journalctl -o json output: one record per line, PID 1 style."""
    return "\n".join(
        json.dumps(
            {
                "_PID": "1",
                "UNIT": m.split(":", 1)[0],
                "MESSAGE": m,
                "__REALTIME_TIMESTAMP": str(stamp_usec),
            }
        )
        for m in messages
    )


def _patched_journalctl(stdout: str, returncode: int = 0):
    return patch.object(
        _mod.subprocess,
        "run",
        return_value=MagicMock(stdout=stdout, returncode=returncode),
    )


def test_unclean_exits_reports_crashes_not_clean_stops():
    """Cores, kills by anything but the stop signal, OOM kills and non-zero
    exits are crashes. A clean exit, a SIGTERM death and the 128+15 status a
    wrapper relays on SIGTERM are what `systemctl stop/restart` produces and
    must not fail a test (the fboss2 CLI restarts the agents on every
    commit; a console logout bounces serial-getty with exit 0)."""
    out = _journal(
        "fboss_hw_agent@0.service: Main process exited, code=dumped, status=6/ABRT",
        "fboss_sw_agent.service: Main process exited, code=exited, status=0/SUCCESS",
        "serial-getty@ttyS0.service: Main process exited, code=exited, status=0/SUCCESS",
        "qsfp_service.service: Main process exited, code=killed, status=15/TERM",
        "hostcfgd.service: Main process exited, code=exited, status=143/n/a",
        "sensor_service.service: Main process exited, code=killed, status=11/SEGV",
        "fan_service.service: Main process exited, code=exited, status=1/FAILURE",
        "platform_manager.service: A process of this unit has been killed by the OOM killer.",
        "fboss_hw_agent@0.service: Scheduled restart job, restart counter is at 1.",
        "Started FBOSS hw agent.",
    )
    with _patched_journalctl(out) as run:
        reasons = find_unclean_unit_exits(1_000.0)
    assert reasons == [
        "fboss_hw_agent@0.service main process dumped core (status=6/ABRT)",
        "sensor_service.service main process was killed (status=11/SEGV)",
        "fan_service.service main process exited (status=1/FAILURE)",
        "platform_manager.service killed by the OOM killer",
    ]
    # one journalctl call, PID 1 only, reading from the second the test started
    cmd = run.call_args[0][0]
    assert cmd[0] == "journalctl"
    assert "_PID=1" in cmd
    assert "--since=@1000" in cmd


def test_unclean_exits_uses_record_timestamp_not_since_granularity():
    """`--since=@N` is whole-second; the record's own microsecond timestamp
    decides. A cold boot the runner performed right before launching the
    binary (same second, earlier microseconds) is not charged to the test."""
    before = _journal(
        "fboss_sw_agent.service: Main process exited, code=dumped, status=6/ABRT",
        stamp_usec=1_000_400_000,
    )
    after = _journal(
        "fboss_hw_agent@0.service: Main process exited, code=dumped, status=6/ABRT",
        stamp_usec=1_000_900_000,
    )
    with _patched_journalctl(before + "\n" + after):
        assert find_unclean_unit_exits(1_000.5) == [
            "fboss_hw_agent@0.service main process dumped core (status=6/ABRT)"
        ]


def test_unclean_exits_ignores_unparseable_lines():
    out = (
        "not json\n"
        + json.dumps({"MESSAGE": ["array", "not", "str"]})
        + "\n"
        + _journal("x.service: Main process exited, code=dumped, status=11/SEGV")
    )
    with _patched_journalctl(out):
        assert find_unclean_unit_exits(0.0) == [
            "x.service main process dumped core (status=11/SEGV)"
        ]


def test_unclean_exits_empty_when_journalctl_unavailable():
    with _patched_journalctl("", returncode=1):
        assert find_unclean_unit_exits(0.0) == []
    with patch.object(_mod.subprocess, "run", side_effect=FileNotFoundError):
        assert find_unclean_unit_exits(0.0) == []


def test_list_core_dumps_lists_files_in_every_dir(tmp_path, monkeypatch):
    a = tmp_path / "coredump"
    b = tmp_path / "core"
    a.mkdir()
    b.mkdir()
    (a / "sub").mkdir()  # directories are not cores
    monkeypatch.setattr(
        _mod, "_CORE_DUMP_DIRS", [str(a), str(b), str(tmp_path / "nope")]
    )
    (a / "core.fboss_hw_agent-.0.abc.100.1000.zst").write_bytes(b"x")
    (b / "core.fboss_sw_agent.0.abc.300.3000").write_bytes(b"x")
    assert list_core_dumps() == {
        str(a / "core.fboss_hw_agent-.0.abc.100.1000.zst"),
        str(b / "core.fboss_sw_agent.0.abc.300.3000"),
    }


def test_list_core_dumps_missing_dir(tmp_path, monkeypatch):
    monkeypatch.setattr(_mod, "_CORE_DUMP_DIRS", [str(tmp_path / "does-not-exist")])
    assert list_core_dumps() == set()


def test_core_is_from_truncates_comm():
    """The kernel truncates the comm in the core name to 15 characters."""
    core = "/var/lib/systemd/coredump/core.sai_test-sai_im.0.x.5.6.zst"
    assert core_is_from(core, "sai_test-sai_impl")
    assert not core_is_from(core, "sai_agent_hw_test-sai_impl")
    assert core_is_from("/var/core/core.fboss_sw_agent.0.x.1.2", "fboss_sw_agent")


# ---------------------------------------------------------------------------
# describe_core_dump (coredumpctl stack -> stable one-line crash summary)
# ---------------------------------------------------------------------------

# `coredumpctl info` output for three real fboss_hw_agent cores with three
# different root causes, all of which previously produced the same "dumped
# core (status=6/ABRT)" failure reason. Frames below the ones that matter are
# dropped and the longest template-heavy symbols are cut short; coredumpctl
# on the switch prints mangled names because the image has no demangler.
UNCLAIMED_WB_HANDLES = """           PID: 752068 (fboss_hw_agent-)
           UID: 0 (root)
           GID: 0 (root)
        Signal: 6 (ABRT)
    Executable: /opt/fboss/bin/fboss_hw_agent-sai_impl
          Unit: fboss_hw_agent@0.service

                Stack trace of thread 752240:
                #0  0x00007f7cb6a8d38c __pthread_kill_implementation (libc.so.6 + 0x8d38c)
                #1  0x00007f7cb6a3fa86 raise (libc.so.6 + 0x3fa86)
                #2  0x00007f7cb74cc0fe _ZN5folly10symbolizer12_GLOBAL__N_113signalHandlerEiP9siginfo_tPv (libfolly.so.0.58.0-dev + 0x2cc0fe)
                #3  0x00007f7cb6a3fb30 __restore_rt (libc.so.6 + 0x3fb30)
                #4  0x00007f7cb6a8d38c __pthread_kill_implementation (libc.so.6 + 0x8d38c)
                #5  0x00007f7cb6a3fa86 raise (libc.so.6 + 0x3fa86)
                #6  0x00007f7cb6a29873 abort (libc.so.6 + 0x29873)
                #7  0x00007f7cb75c6058 _ZNK5folly11LogCategory12admitMessageERKNS_10LogMessageEb (libfolly.so.0.58.0-dev + 0x3c6058)
                #8  0x00007f7cb75c90df _ZN5folly18LogStreamProcessor6logNowEv (libfolly.so.0.58.0-dev + 0x3c90df)
                #9  0x00007f7cb743a6fc _ZN5folly16LogStreamVoidifyILb1EEanERSo (libfolly.so.0.58.0-dev + 0x23a6fc)
                #10 0x000000000f79097d _ZNK8facebook5fboss8SaiStore39checkUnexpectedUnclaimedWarmbootHandlesEv (fboss_hw_agent-sai_impl + 0xf59097d)
                #11 0x000000000f4820df _ZN8facebook5fboss9SaiSwitch19initialStateAppliedEv (fboss_hw_agent-sai_impl + 0xf2820df)
                #12 0x000000000ef9c9ef _ZN8facebook5fboss15OperDeltaSyncer12operSyncLoopEv (fboss_hw_agent-sai_impl + 0xed9c9ef)
"""
STATEDELTA_CTOR = """           PID: 50616 (fboss_hw_agent-)
           UID: 0 (root)
           GID: 0 (root)
        Signal: 6 (ABRT)
    Executable: /opt/fboss/bin/fboss_hw_agent-sai_impl
          Unit: fboss_hw_agent@0.service

                Stack trace of thread 50699:
                #0  0x00007ff811c8d38c __pthread_kill_implementation (libc.so.6 + 0x8d38c)
                #1  0x00007ff811c3fa86 raise (libc.so.6 + 0x3fa86)
                #2  0x00007ff812cdaf9a _ZN5folly10symbolizer12_GLOBAL__N_113signalHandlerEiP9siginfo_tPv (libfolly.so.0.58.0-dev + 0x2daf9a)
                #3  0x00007ff811c3fb30 __restore_rt (libc.so.6 + 0x3fb30)
                #4  0x00007ff811c8d38c __pthread_kill_implementation (libc.so.6 + 0x8d38c)
                #5  0x00007ff811c3fa86 raise (libc.so.6 + 0x3fa86)
                #6  0x00007ff811c29873 abort (libc.so.6 + 0x29873)
                #7  0x00000000100da55c _ZN12_GLOBAL__N_116terminateHandlerEv (fboss_hw_agent-sai_impl + 0xfeda55c)
                #8  0x00007ff8120ad53c _ZN10__cxxabiv111__terminateEPFvvE (libstdc++.so.6 + 0xad53c)
                #9  0x00007ff8120ad5a7 _ZSt9terminatev (libstdc++.so.6 + 0xad5a7)
                #10 0x00007ff8120ad809 __cxa_throw (libstdc++.so.6 + 0xad809)
                #11 0x00007ff812cc7e9d __cxa_throw (libfolly.so.0.58.0-dev + 0x2c7e9d)
                #12 0x000000001024bb89 _ZN8facebook5fboss10StateDeltaC2ESt10shared_ptrINS0_11SwitchStateEENS0_4fsdb9OperDeltaE (fboss_hw_agent-sai_impl + 0x1004bb89)
                #13 0x000000000fc5c974 _ZSt12construct_atIN8facebook5fboss10StateDeltaEJRSt10shared_ptrINS1_11SwitchStateEERKNS1_ (fboss_hw_agent-sai_impl + 0xfa5c974)
                #14 0x000000000fc5bbd2 _ZNSt6vectorIN8facebook5fboss10StateDeltaESaIS2_EE12emplace_backIJRSt10shared_ptrINS1_11Sw (fboss_hw_agent-sai_impl + 0xfa5bbd2)
                #15 0x000000000fc59e93 _ZN8facebook5fboss8HwSwitch12stateChangedERKSt6vectorINS0_4fsdb9OperDeltaESaIS4_EERKNS0_19 (fboss_hw_agent-sai_impl + 0xfa59e93)
                #16 0x000000000f31a386 _ZN8facebook5fboss15OperDeltaSyncer12operSyncLoopEv (fboss_hw_agent-sai_impl + 0xf11a386)
"""
PORTQUEUE_SAIAPIERROR = """           PID: 212895 (fboss_hw_agent-)
           UID: 0 (root)
           GID: 0 (root)
        Signal: 6 (ABRT)
    Executable: /opt/fboss/bin/fboss_hw_agent-sai_impl
          Unit: fboss_hw_agent@0.service

                Stack trace of thread 213047:
                #0  0x00007f0542e8d38c __pthread_kill_implementation (libc.so.6 + 0x8d38c)
                #1  0x00007f0542e3fa86 raise (libc.so.6 + 0x3fa86)
                #2  0x00007f0543edaf9a _ZN5folly10symbolizer12_GLOBAL__N_113signalHandlerEiP9siginfo_tPv (libfolly.so.0.58.0-dev + 0x2daf9a)
                #3  0x00007f0542e3fb30 __restore_rt (libc.so.6 + 0x3fb30)
                #4  0x00007f0542e8d38c __pthread_kill_implementation (libc.so.6 + 0x8d38c)
                #5  0x00007f0542e3fa86 raise (libc.so.6 + 0x3fa86)
                #6  0x00007f0542e29873 abort (libc.so.6 + 0x29873)
                #7  0x00000000100da55c _ZN12_GLOBAL__N_116terminateHandlerEv (fboss_hw_agent-sai_impl + 0xfeda55c)
                #8  0x00007f05432ad53c _ZN10__cxxabiv111__terminateEPFvvE (libstdc++.so.6 + 0xad53c)
                #9  0x00007f05432ad5a7 _ZSt9terminatev (libstdc++.so.6 + 0xad5a7)
                #10 0x00007f05432ad809 __cxa_throw (libstdc++.so.6 + 0xad809)
                #11 0x00007f0543ec7e9d __cxa_throw (libfolly.so.0.58.0-dev + 0x2c7e9d)
                #12 0x000000000916423a _ZN8facebook5fboss16saiApiCheckErrorIJNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE (fboss_hw_agent-sai_impl + 0x8f6423a)
                #13 0x000000000fa1edd6 _ZNK8facebook5fboss6SaiApiINS0_7PortApiEE6createITkNS0_17ObjectIdSaiObjectENS0_13SaiPortTr (fboss_hw_agent-sai_impl + 0xf81edd6)
                #14 0x000000000fa1ec6b _ZN8facebook5fboss9SaiObjectINS0_13SaiPortTraitsEE12createHelperITkNS0_17ObjectIdSaiObject (fboss_hw_agent-sai_impl + 0xf81ec6b)
"""


def _patched_coredumpctl(stdout: str, returncode: int = 0):
    return _patched_journalctl(stdout, returncode)  # same subprocess.run patch


_CORE = "/var/lib/systemd/coredump/core.fboss_hw_agent-.0.61556c93.752068.1790547375000000.zst"


@pytest.mark.parametrize(
    "info, expected",
    [
        (
            UNCLAIMED_WB_HANDLES,
            "fboss_hw_agent@0.service SIGABRT in "
            "SaiStore::checkUnexpectedUnclaimedWarmbootHandles < "
            "SaiSwitch::initialStateApplied < OperDeltaSyncer::operSyncLoop",
        ),
        (
            STATEDELTA_CTOR,
            "fboss_hw_agent@0.service SIGABRT in "
            "StateDelta::StateDelta < HwSwitch::stateChanged < "
            "OperDeltaSyncer::operSyncLoop",
        ),
        (
            PORTQUEUE_SAIAPIERROR,
            "fboss_hw_agent@0.service SIGABRT in "
            "saiApiCheckError < SaiApi::create < SaiObject::createHelper",
        ),
    ],
    ids=["xlog-fatal", "uncaught-exception-in-ctor", "uncaught-sai-error"],
)
def test_core_summary_names_the_crash_not_the_death(info, expected):
    """The signal, terminate, throw and FATAL-log frames are the same for
    every abort; the summary starts at the first frame that says what
    crashed, and carries no addresses, pids or template arguments so the
    same root cause yields the same string on every hit."""
    with _patched_coredumpctl(info) as run:
        assert _mod.describe_core_dump(_CORE) == expected
    cmd = run.call_args[0][0]
    assert cmd[:4] == ["coredumpctl", "-q", "--no-pager", "info"]
    assert cmd[4] == f"COREDUMP_FILENAME={_CORE}"


def test_core_summary_retries_by_pid_then_falls_back():
    """No record under the file name → try the pid from the core name; no
    record at all (workstations, fboss-sim) → name the file."""
    empty = MagicMock(stdout="", returncode=1)
    with patch.object(
        _mod.subprocess,
        "run",
        side_effect=[empty, MagicMock(stdout=UNCLAIMED_WB_HANDLES, returncode=0)],
    ) as run:
        assert _mod.describe_core_dump(_CORE).startswith(
            "fboss_hw_agent@0.service SIGABRT in SaiStore::"
        )
    assert run.call_args_list[1][0][0][4] == "752068"
    with patch.object(_mod.subprocess, "run", side_effect=[empty, empty]):
        assert _mod.describe_core_dump(_CORE) == (
            "core core.fboss_hw_agent-.0.61556c93.752068.1790547375000000.zst"
        )
    with patch.object(_mod.subprocess, "run", side_effect=FileNotFoundError):
        assert _mod.describe_core_dump("/var/core/core.1234") == "core core.1234"


def test_core_summary_without_unit_or_symbols():
    info = (
        "           PID: 42 (bgpd)\n"
        "        Signal: 11 (SEGV)\n"
        "    Executable: /opt/fboss/bin/bgpd\n"
        "Stack trace of thread 42:\n"
        "                #0  0x00007f00 raise (libc.so.6 + 0x1)\n"
        "                #1  0x00007f01 n/a (bgpd + 0x2)\n"
        "                #2  0x00007f02 _ZN3bgp4peer8sendOpenEv (bgpd + 0x3)\n"
        "\n"
        "Stack trace of thread 43:\n"
        "                #0  0x00007f03 _ZN3bgp7ignoredEv (bgpd + 0x4)\n"
    )
    assert _mod._summarize_core_stack(info) == (
        "bgpd SIGSEGV in n/a < bgp::peer::sendOpen"
    )
    assert _mod._summarize_core_stack("Signal: 6 (ABRT)\n") == (
        "unknown unit SIGABRT in unknown stack"
    )


@pytest.mark.parametrize(
    "symbol, short",
    [
        (
            "_ZN8facebook5fboss10StateDeltaC2ESt10shared_ptrIN",
            "facebook::fboss::StateDelta::StateDelta",
        ),
        (
            "_ZN8facebook5fboss10StateDeltaD1Ev",
            "facebook::fboss::StateDelta::~StateDelta",
        ),
        (
            "_ZNK8facebook5fboss6SaiApiINS0_7PortApiEE6createITkNS0_17ObjectIdSaiObjectEQ15SaiObjectForApiITL0__T_EEENS7_10AdapterKeyERKNS7_16CreateAttributesEm",
            "facebook::fboss::SaiApi::create",
        ),
        (
            "_ZN8facebook5fboss14SaiObjectStoreINS0_13SaiPortTraitsEE7programERKSt5tuple",
            "facebook::fboss::SaiObjectStore::program",
        ),
        ("_ZSt9terminatev", "std::terminate"),
        (
            "_ZNSt6vectorIN8facebook5fboss10StateDeltaESaIS2_EE12emplace_backIJ",
            "std::vector::emplace_back",
        ),
        ("_ZN12_GLOBAL__N_116terminateHandlerEv", "_GLOBAL__N_1::terminateHandler"),
        (
            "_ZNK5folly11LogCategory12admitMessageERKNS_10LogMessageEb",
            "folly::LogCategory::admitMessage",
        ),
        ("bcmlm_sw_linkscan_link_get", "bcmlm_sw_linkscan_link_get"),
        (
            "facebook::fboss::SaiSwitch::foo<int>(int, bool) const",
            "facebook::fboss::SaiSwitch::foo",
        ),
        ("n/a", "n/a"),
    ],
)
def test_demangle_short(symbol, short):
    assert _mod._demangle_short(symbol) == short


def test_core_pid_from_name():
    assert _mod._core_pid_from_name(_CORE) == "752068"
    assert (
        _mod._core_pid_from_name(
            "/var/lib/systemd/coredump/core.qsfp_service.0.abc.31.1790547375000000"
        )
        == "31"
    )
    assert _mod._core_pid_from_name("/var/core/core.1234") is None
