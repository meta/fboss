# fixmyfboss

Diagnoses common platform problems on an FBOSS switch and suggests a
remediation for each.

## Running

On the switch:

```
fixmyfboss                # run all checks that apply to this platform
fixmyfboss --list-checks  # show checks, and why any would be skipped
fixmyfboss -v             # also print the commands and outputs behind failures
```

From a dev server, against a remote switch:

```
fixmyfboss --hostname rsw1.snc1                    # x86 + BMC (rsw1-oob.snc1)
fixmyfboss --hostname rsw1.snc1 --checks 'bmc|sol' # a subset, by name regex
fixmyfboss --hostname rsw1.snc1 --json             # machine-readable CheckReport
```

| Flag | Meaning |
|---|---|
| `--hostname` | Run against this switch over SSH instead of the local machine. |
| `--bmc-hostname` | BMC to check. Default with `--hostname`: `<hostname>-oob`. |
| `--no-bmc` | The switch has no BMC; skip all BMC-related checks. |
| `--transport` | `auto` (sush2 if installed), `ssh` or `sush2`. |
| `--include-disruptive` | Also run checks that reboot or power cycle the switch. |
| `--hw-tests-dir` | Prebuilt platform hw tests and OpenBMC `tests2/`; see `facebook/build_hw_tests.sh`. |
| `--bmc-platform` | Platform for the BMC CIT runner. Default: lowercase platform name. |

If the x86 or BMC rejects key login but accepts a password, fixmyfboss asks
for it at startup (input is hidden) and logs in with OpenSSH. There is no
password flag, so passwords never end up in the logged command line.

Checks whose host is unavailable, disruptive checks without
`--include-disruptive`, and hw tests without artifacts are reported as
SKIPPED with the reason. Exit code is 0 iff no check reported a PROBLEM or
ERROR.

## How it works

Every check (`fboss/platform/platform_checks/checks/`) does all of its I/O
through a `Host`:

- `LocalHost` is the machine fixmyfboss runs on.
- `RemoteHost` runs commands on a switch over SSH, through one long-lived
  shell per host (driven by `ExpectSession`), so that a check reading
  hundreds of sysfs files stays fast.

So the same check code runs on-device and remotely. `createAllChecks()`
(`CheckRegistry.cpp`) wires each check to the x86 and/or BMC host of the
`CheckEnvironment`; `CheckRunner` decides what runs, in which order, and why
the rest is skipped.

## Adding a check

1. Subclass `PlatformCheck` (or use `CommandCheck` if "this command succeeds"
   is the whole check), taking a `CheckTarget` per host it needs.
2. Use only `host()` for I/O; record commands with `CommandLog` so failures
   carry their evidence in `CheckResult.details`.
3. Override `isDisruptive()` if it reboots, power cycles or stops services.
4. Add a `CheckType`, register it in `createAllChecks()`, and test it with
   `MockHost`.
