#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.

"""Detect a systemd unit crashing while a test binary runs.

Every runner leaves some production units running while its test binary
executes (sai_test keeps fsdb and the platform services up, sai_agent keeps
qsfp_service up, the fboss2 CLI suite runs against the production agents
themselves), and those units run with ``Restart=always``. A crash the test
caused is therefore invisible to gtest: systemd brings the unit back, the
binary's own readiness wait rides through it, and the test reports OK while
a core sits on disk and the hardware may never have received the config.

Two signals, both read after the binary exits, cover it:

* :func:`find_unclean_unit_exits` -- PID 1's own journal records of a unit's
  main process dying uncleanly inside the window.
* :func:`list_core_dumps` -- taken before and after the binary runs; a path
  in the second set but not the first is a core dumped inside the window.

:func:`describe_core_dump` then turns each new core into a one-line summary
of the crash (unit, signal, first frames below the signal machinery) that is
stable across hits, for the test's failure reason.
"""

import json
import os
import re
import signal
import subprocess

_CORE_DUMP_DIRS = ["/var/core", "/var/lib/systemd/coredump"]

# The kernel truncates the process name embedded in a core file name
# (systemd-coredump: core.<comm>.<uid>.<boot>.<pid>.<ts>[.zst]) to
# TASK_COMM_LEN - 1 characters.
_TASK_COMM_LEN = 15

# systemd (PID 1) logs one line per main-process exit of every unit, e.g.
#   "fboss_hw_agent@0.service: Main process exited, code=dumped, status=6/ABRT"
# Parsing this line is preferable to `systemctl show -p NRestarts,Result`:
# NRestarts only moves for Restart=-triggered restarts (so a manual restart
# followed by a crash reads "unchanged"), and Result is reset to "success" the
# moment the unit is started again, so by the time a check runs after the test
# the crash has already been papered over. The journal keeps the record.
_MAIN_PROCESS_EXIT_RE = re.compile(
    r"^(?P<unit>\S+): Main process exited, code=(?P<code>\w+), "
    r"status=(?P<status>\d+)/(?P<name>\S+)$"
)
_OOM_KILL_RE = re.compile(
    r"^(?P<unit>\S+): A process of this unit has been killed by the OOM killer"
)

# Exits systemd's own `systemctl stop/restart` produces on a well-behaved unit:
# a clean exit, or termination by the stop signal (SIGTERM; SIGINT/SIGHUP for
# units that set KillSignal=) either as a signal death or as the 128+N status
# a shell/python wrapper returns when it relays it.
_GRACEFUL_SIGNALS = frozenset({"TERM", "INT", "HUP"})
_GRACEFUL_EXIT_STATUSES = frozenset(
    {0, 128 + signal.SIGTERM, 128 + signal.SIGINT, 128 + signal.SIGHUP}
)


def _classify_unit_exit(message: str) -> str | None:
    """Return a human-readable reason if `message` (a PID 1 journal line)
    records an unclean main-process exit of a unit, else None."""
    m = _OOM_KILL_RE.match(message)
    if m:
        return f"{m['unit']} killed by the OOM killer"
    m = _MAIN_PROCESS_EXIT_RE.match(message)
    if not m:
        return None
    code, status, name = m["code"], int(m["status"]), m["name"]
    if code == "exited" and status in _GRACEFUL_EXIT_STATUSES:
        return None
    if code == "killed" and name in _GRACEFUL_SIGNALS:
        return None
    what = {"dumped": "dumped core", "killed": "was killed", "exited": "exited"}.get(
        code, code
    )
    return f"{m['unit']} main process {what} (status={status}/{name})"


def find_unclean_unit_exits(start_time: float) -> list[str]:
    """Return one reason per systemd unit whose main process died uncleanly
    since `start_time` (crash, abort, OOM kill, non-zero exit), oldest first.

    Reads PID 1's journal records for the window. Clean stops and restarts --
    the ones the test infrastructure and the fboss2 CLI perform on purpose --
    exit 0 or die by SIGTERM and are not reported, so bouncing a unit inside
    the window is fine; only an exit systemd would count as a failure is.
    Units the runner stopped beforehand are inactive and produce nothing.
    Returns [] when journalctl is unavailable or fails (workstations,
    containers).

    The window is compared on the record's own microsecond timestamp, so a
    stop the runner performed right before launching the binary (a cold boot
    of the agents, say) is not charged to the test; `--since` only bounds how
    much journal is read.
    """
    since_usec = int(start_time * 1_000_000)
    try:
        result = subprocess.run(
            [
                "journalctl",
                "-q",
                "--no-pager",
                "-o",
                "json",
                f"--since=@{int(start_time)}",
                "_PID=1",
            ],
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError:
        return []
    if result.returncode != 0:
        return []
    reasons: list[str] = []
    for line in result.stdout.splitlines():
        try:
            record = json.loads(line)
            message = record.get("MESSAGE")
            stamp = int(record.get("__REALTIME_TIMESTAMP", since_usec))
        except (ValueError, TypeError, AttributeError):
            continue
        if stamp < since_usec or not isinstance(message, str):
            continue
        reason = _classify_unit_exit(message)
        if reason:
            reasons.append(reason)
    return reasons


def list_core_dumps() -> set[str]:
    """Return the paths of every core file currently on disk.

    Callers take this before and after a test binary runs and report the
    difference. Comparing sets rather than mtimes means one core is charged
    to exactly one test, even when systemd-coredump finishes writing it a
    moment after the check that should have seen it, or when the next test
    starts within a second of the previous one. Unreadable directories and
    entries are skipped.
    """
    found: set[str] = set()
    for dir_path in _CORE_DUMP_DIRS:
        if not os.path.isdir(dir_path):
            continue
        try:
            with os.scandir(dir_path) as it:
                for entry in it:
                    try:
                        if entry.is_file():
                            found.add(entry.path)
                    except OSError:
                        continue
        except OSError:
            continue
    return found


def core_is_from(core_path: str, exe_name: str) -> bool:
    """True if the core file at `core_path` was dumped by `exe_name`."""
    name = os.path.basename(core_path)
    return name.startswith(f"core.{exe_name[:_TASK_COMM_LEN]}.") or exe_name in name


# `coredumpctl info` frame line: "#12 0x000000000916423a <symbol> (<module> + 0x...)".
_CORE_FRAME_RE = re.compile(r"^\s*#(?P<n>\d+)\s+0x[0-9a-fA-F]+\s+(?P<sym>\S+)")
_CORE_HEADER_RE = re.compile(
    r"^\s*(?P<key>PID|Signal|Unit|Executable|Command Line):\s*(?P<val>.*)$"
)
_CORE_SIGNAL_RE = re.compile(r"^\d+\s*\((?P<name>[A-Z0-9]+)\)")
# Everything between the signal and the code that raised it. Matched against
# the short (demangled, unqualified) frame name.
_CORE_SKIP_FRAME_RE = re.compile(
    r"^(__pthread_kill.*|__GI_.*|raise|gsignal|abort|__restore_rt|__clone.*"
    r"|.*signalHandler|.*terminateHandler|__terminate|terminate|__cxa_(re)?throw"
    r"|__cxxabiv1::.*|std::.*"
    r"|folly::(LogCategory|LogStreamProcessor|LogStreamVoidify|throw_exception).*"
    r"|google::(LogMessage|LogMessageFatal).*|folly::detail::.*)$"
)
_MANGLED_BACKREF_RE = re.compile(r"S[0-9A-Z]*_")
_CORE_STACK_FRAMES = 3
_CORE_INFO_TIMEOUT_SEC = 30


def _core_pid_from_name(core_path: str) -> str | None:
    """systemd-coredump names cores core.<comm>.<uid>.<boot-id>.<pid>.<ts>[.zst]."""
    parts = os.path.basename(core_path).split(".")
    if parts and parts[-1] in ("zst", "lz4", "xz", "gz"):
        parts.pop()
    if len(parts) >= 6 and parts[0] == "core" and parts[-2].isdigit():
        return parts[-2]
    return None


def _skip_mangled_group(symbol: str, i: int) -> int:
    """Return the index just past the E that closes the group opened at i."""
    depth = 0
    while i < len(symbol):
        c = symbol[i]
        if c.isdigit():  # <length><identifier>: skip it whole, letters and all
            j = i
            while j < len(symbol) and symbol[j].isdigit():
                j += 1
            i = j + int(symbol[i:j])
            continue
        if c == "S":
            # A back-reference to an earlier name (S_, S0_ ... S9_, SA_ ...,
            # base-36 seq-id ending in _) or a two-letter std:: shorthand
            # (St, Sa, Ss, ...). Neither may be read as a length prefix.
            m = _MANGLED_BACKREF_RE.match(symbol, i)
            i = m.end() if m else i + 2
            continue
        if c in "INJL":
            depth += 1
        elif c == "E":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return i


def _demangle_short(symbol: str) -> str:
    """Reduce a stack-frame symbol to its qualified function name.

    coredumpctl prints Itanium-mangled names when no demangler is at hand
    (the DUT image ships none), so walk the nested-name prefix of the
    mangling by hand: enough to name the function, dropping template and
    parameter encodings. Already-demangled input just loses its argument
    list and template arguments. Anything else is returned as is.
    """
    if not symbol.startswith("_Z"):
        return re.sub(r"<.*|\(.*", "", symbol)
    i = 2
    nested = symbol.startswith("_ZN")
    if nested:
        i = 3
        while i < len(symbol) and symbol[i] in "KVr":
            i += 1
    parts: list[str] = []
    while i < len(symbol):
        c = symbol[i]
        if c.isdigit():
            j = i
            while j < len(symbol) and symbol[j].isdigit():
                j += 1
            n = int(symbol[i:j])
            parts.append(symbol[j : j + n])
            i = j + n
            if not nested:
                break
        elif symbol.startswith("St", i):
            parts.append("std")
            i += 2
        elif c in "CD" and i + 1 < len(symbol) and symbol[i + 1] in "0123":
            if parts:
                parts.append(("~" if c == "D" else "") + parts[-1])
            i += 2
        elif c == "I" and nested:
            i = _skip_mangled_group(symbol, i)  # template args of a prefix
        else:
            break  # parameters or the closing E
    return "::".join(parts) if parts else symbol


def _summarize_core_stack(info: str) -> str:
    """Turn `coredumpctl info` output into "<unit> <SIGNAL> in f1 < f2 < f3".

    Takes the first thread listed (the one that crashed) and names the
    first `_CORE_STACK_FRAMES` frames below the signal / terminate / throw
    / FATAL-log machinery, so the string identifies the crash rather than
    the way the process died. Stable across hits: no addresses, pids or
    template arguments.
    """
    unit = signal_name = None
    frames: list[str] = []
    in_stack = False
    for line in info.splitlines():
        if not in_stack:
            m = _CORE_HEADER_RE.match(line)
            if m and m["key"] == "Unit":
                unit = m["val"].strip()
            elif m and m["key"] == "Signal":
                sm = _CORE_SIGNAL_RE.match(m["val"].strip())
                signal_name = f"SIG{sm['name']}" if sm else m["val"].strip()
            elif m and m["key"] in ("Executable", "Command Line") and not unit:
                unit = os.path.basename(m["val"].strip().split()[0])
            elif line.strip().startswith("Stack trace of thread"):
                in_stack = True
            continue
        m = _CORE_FRAME_RE.match(line)
        if not m:
            break  # end of the first thread's stack
        name = _demangle_short(m["sym"])
        if _CORE_SKIP_FRAME_RE.match(name):
            continue
        frames.append(name.removeprefix("facebook::fboss::"))
        if len(frames) >= _CORE_STACK_FRAMES:
            break
    where = " < ".join(frames) if frames else "unknown stack"
    return f"{unit or 'unknown unit'} {signal_name or 'crashed'} in {where}"


def describe_core_dump(core_path: str) -> str:
    """One-line description of the crash behind a core, stable across hits.

    Falls back to naming the core file when coredumpctl is unavailable or
    has no record for it (workstations, the fboss-sim container).
    """
    fallback = f"core {os.path.basename(core_path)}"
    matches = [f"COREDUMP_FILENAME={core_path}"]
    pid = _core_pid_from_name(core_path)
    if pid:
        matches.append(pid)
    for match in matches:
        try:
            result = subprocess.run(
                ["coredumpctl", "-q", "--no-pager", "info", match],
                capture_output=True,
                text=True,
                check=False,
                timeout=_CORE_INFO_TIMEOUT_SEC,
            )
        except (OSError, subprocess.TimeoutExpired):
            return fallback
        if result.returncode == 0 and "Stack trace of thread" in result.stdout:
            return _summarize_core_stack(result.stdout)
    return fallback
