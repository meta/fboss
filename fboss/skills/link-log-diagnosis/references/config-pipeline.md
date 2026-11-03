# Config Pipeline Layout

`fetch_logs.sh` collects config history from a git repo on the switch, and
`filter_config_changes.sh` picks the hardware-affecting changes out of it. Both
need to know how your deployment lays that repo out, because the paths differ
between environments.

## Settings

Set these before running the scripts. Unset paths cause the corresponding
section to be skipped with a message naming the variable, rather than reporting
"no changes found".

| Variable | What it points at |
|---|---|
| `FBOSS_CONFIG_REPO` | Directory of the git repo on the switch holding pushed config. Unset disables config collection entirely. |
| `FBOSS_AGENT_CONFIG_PATH` | Path within that repo to the AgentConfig thrift the agent actually reads. |
| `FBOSS_PLATFORM_MAPPING_PATH` | Path to the platform mapping input, if the pipeline stages one. |
| `FBOSS_CONFIG_VERSION_PATH` | Path to version metadata whose change can trigger a warmboot. |

Example:

```bash
export FBOSS_CONFIG_REPO=/etc/switch-config
export FBOSS_AGENT_CONFIG_PATH=<path/to/agent_config.json>
export FBOSS_CONFIG_VERSION_PATH=<path/to/version.json>
```

## Identifying the paths

If you do not already know the layout, run one config-push commit through
`git show --stat` in the repo on the switch and look for:

- A JSON file matching the AgentConfig thrift schema, containing a `ports` list
  with `speed`, `profileID`, `state` and `fecMode` keys. That is the agent
  config.
- A file whose only change on most pushes is a hash or version string. That is
  the version metadata, and it is the one to suspect when links flap without
  any port config change.

## What always works

Sections that match on FBOSS config *fields* rather than repo paths run
regardless of these settings: the config push timeline, port speed / FEC /
lane / interface changes, drain operations, and the list of changed files.
Those cover most hardware-affecting changes on their own.
