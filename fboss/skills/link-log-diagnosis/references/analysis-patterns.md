# Link Log Analysis Patterns

All log strings below are verified from the FBOSS source code. Use `grep -F` for literal matching.

## 1. Boot Detection

Search **both** wedge_agent.log and qsfp_service.log:

```
grep -F "Will attempt" <logfile>
```

| Log String | Source | Meaning |
|-----------|--------|---------|
| `Will attempt WARM boot` | `TransceiverManager.cpp:295`, `SwSwitchWarmBootHelper.cpp:77` | Warmboot — link flaps are NOT expected |
| `Will attempt COLD boot` | Same files | Coldboot — link flaps ARE expected (all ports reprogram) |

**Coldboot vs Warmboot key difference:**
- Coldboot sets `needResetDataPath=true` → full datapath reset → all ports drop and come back
- Warmboot sets `needResetDataPath=false` → preserves existing datapath if speed/AppSel matches
- Log: `"Programmed with resetting data path"` (coldboot) vs `"without resetting data path"` (warmboot) — prefixed with `Transceiver:{id}` via MODULE_LOG macro

**Agent-only vs Agent+qsfp_service warmboots:**
Both services can warmboot independently or together. Distinguishing this is critical for root cause analysis:
- Check wedge_agent logs for `"Will attempt WARM boot"` from `HwSwitchWarmBootHelper` → agent warmboot
- Check qsfp_service logs for `"Will attempt WARM boot"` from `TransceiverManager` → qsfp_service warmboot
- If both appear within ~30s, it's a combined agent+qsfp_service warmboot
- If only one appears, it's a single-service warmboot
- Compare which warmboot types correlate with flaps — this narrows the root cause to a specific service

**Canary devices:** Some switches reboot frequently for testing. Many warmboots is NOT a "warmboot loop" — check device role before drawing conclusions about boot frequency.

**Remediation timing:** Remediation CANNOT be the initial trigger for a link flap. It only fires after a port has been down for 120 seconds (initial cooldown), with 360 seconds between subsequent attempts. When analyzing flaps, remediation is always a **secondary** cause — look for the original trigger first.

## 2. Link State Changes (wedge_agent.log)

```
grep -F "LinkState:" <wedge_agent.log>
```

| Log String | Source | Meaning |
|-----------|--------|---------|
| `LinkState: Port {port} Up` | `agent/oss/SwSwitch.cpp` | Port came up |
| `LinkState: Port {port} Down` | Same | Port went down |
| `SW Link state changed: {name} id: {id} [UP->DOWN]` | `agent/SwSwitch.cpp` | Alternate format for link state change |

> `{port}` is the numeric port ID in open-source builds. Some builds log the
> port name instead, so match on the `LinkState: Port` prefix and resolve the
> identifier afterwards rather than grepping for a port name directly.
>
> Source line numbers are omitted on purpose. They drift — grep for the string.

Build a timeline of all `LinkState` events. If a port was specified, filter to that port name.

## 3. IPHY Programming Check (wedge_agent.log)

During warmboot, check if the agent actually reprogrammed IPHY for the transceiver or skipped it:

```
grep -F "programInternalPhyPorts" <wedge_agent.log>
```

| Log String | Source | Meaning |
|-----------|--------|---------|
| `programInternalPhyPorts for present Transceiver:{ID} matches current SwitchState. Skip re-programming` | `ThriftHandler.cpp:2047` | IPHY programming skipped — no change needed (**hitless**) |
| `programInternalPhyPorts for Transceiver:{ID}` (without "Skip") | Same file | IPHY programming was applied — check if it changed port config |

If IPHY was skipped AND `Speed matches` in qsfp_service → the entire programming path was hitless. If the link still flapped, the cause is NOT in the programming path.

## 4. Config Change Correlation (config_changes.log)

Config pushes can trigger agent config change events, causing transceiver reprogramming and potential link flaps. When a config repo is configured, `fetch_logs.sh` captures its git history from each switch. These files are typically 10-100MB+ and dominated by routing policy noise.

**IMPORTANT: Never `cat` or manually grep the raw config_changes.log.** Use the `filter_config_changes.sh` script:

```
bash <skill_dir>/filter_config_changes.sh <log_dir>/config_changes.log [port_filter]
```

The script extracts only hardware-affecting changes:
- **Agent config changes** — the actual config the agent reads. If it didn't change, port config (speed/profile/FEC/state) is unchanged.
- **TX/RX SerDes settings** (`main`, `pre`, `pre2`, `post`, `post2`, `post3`) from the platform mapping input — see caveat below
- **Port speed, FEC, lane, interface** configuration changes
- **Drain/undrain operations**
- **Agent config version** changes (to correlate with `triggerAgentConfigChangeEvent`)
- **Config push timeline** with commit messages

### How config reaches the agent

The layout of the config repo on the switch is deployment-specific. Read
`config-pipeline.md` (see the routing table in `SKILL.md`) for the paths in
your environment and how they map to the fields below.

Whatever the layout, three things matter:

1. **The agent config itself** — the AgentConfig thrift the agent reads. If the
   port entries in it did not change, port speed, profile, FEC and state are
   unchanged, and a config push is not the direct cause of a flap.
2. **Version metadata** — many deployments trigger a warmboot on a config
   *version* change, independent of whether the content the agent cares about
   changed. This is the most common false positive: a routing-policy edit bumps
   the version, the agent warmboots, qsfp_service reprograms, links flap, and
   the port config was identical throughout.
3. **Platform mapping inputs** — usually not consumed at runtime, because the
   agent uses its compiled-in platform mapping. Only relevant when
   `--platform_mapping_override_path` is set.

### Key config change patterns

| Config Change | Impact | How It Causes Flaps |
|--------------|--------|---------------------|
| Agent config port speed/profile change | Port reconfiguration | `Create only attribute changed` → port destroyed/recreated |
| Agent config port state change | Admin state change | Port enabled/disabled |
| Version metadata only | Warmboot trigger | Version hash change → warmboot → reprogramming cycle → flap (even if config unchanged) |
| Drain operations | Admin state change | Port taken down intentionally |
| platform_mapping TX SerDes | Usually none at runtime | Config-pipeline input only — agent typically uses compiled-in mapping. Only matters if `--platform_mapping_override_path` is set. |

**Cross-reference with agent logs:**
- Config push → `triggerAgentConfigChangeEvent` in qsfp_service → transceiver reprogramming cycle
- If `triggerAgentConfigChangeEvent` appears near a flap → check whether the agent config actually changed or just version metadata
- If the agent config is unchanged → flap is caused by unnecessary warmboot/reprogramming, not by config change
- If no config changes in the time window → config push is ruled out as a trigger

## 5. Disruptive Agent Operations (wedge_agent.log)

These operations in the agent cause link flaps. Search for them near link DOWN events.

### SAI Path (Modern Platforms)

```
grep -F "Create only attribute" <wedge_agent.log>
```

| Log String | Source | Meaning |
|-----------|--------|---------|
| `Create only attribute (e.g. lane, speed etc.) changed for {portID}` | `npu/SaiPortManager.cpp:394` | Port being destroyed and recreated — speed or lane change. **Always disruptive.** |

### BCM Path (Legacy Platforms)

```
grep -E "Changing port (speed\|resource) on up port" <wedge_agent.log>
grep -F "Reconfiguring port" <wedge_agent.log>
```

| Log String | Source | Meaning |
|-----------|--------|---------|
| `Changing port speed on up port. This will disrupt traffic. Port: {name}` | `BcmPort.cpp:1008` | Speed change on a running port |
| `Changing port resource on up port. This might disrupt traffic. Port: {name}` | `BcmPort.cpp:3114` | FEC/speed/phy_lane_config change on running port |
| `Reconfiguring port {id} from using {N} lanes to {M} lanes` | `BcmPortGroup.cpp:317` | Lane mode change — always disruptive, disables all ports in group |

## 6. Disruptive qsfp_service Operations (qsfp_service.log)

### Remediation

```
grep -F "allPortsDown" <qsfp_service.log>
grep -F "Doing datapath reinit" <qsfp_service.log>
grep -F "Remediated, ports=" <qsfp_service.log>
```

| Log String | Source | Meaning |
|-----------|--------|---------|
| `allPortsDown = 1. Performing potentially disruptive remediations on {ports}` | `CmisModule.cpp:3673` | Full module reset — all ports on this transceiver drop |
| `allPortsDown = false` (in same log line) | Same | Per-port remediation only (less disruptive) |
| `Doing datapath reinit for {port} with lane mask {int}` | `CmisModule.cpp` | Per-port datapath reinit — that specific port drops |
| `Remediated, ports={ports}` | `TransceiverManager.cpp` (via MODULE_LOG_IF) | Confirms remediation was executed — prefixed with `Transceiver:{id}` |

**Remediation loop detection:** If `Remediated, ports=` appears repeatedly for the same transceiver ID within a short window (< 10 min), it's a remediation loop. Check `shouldRemediateLocked()` logic:
- Initial cooldown: 120 seconds after port goes down
- Repeat cooldown: 360 seconds between remediation attempts

### Transceiver Programming

```
grep -F "Trying to set application code" <qsfp_service.log>
grep -F "Speed matches" <qsfp_service.log>
grep -F "starting datapath" <qsfp_service.log>
```

| Log String | Source | Meaning |
|-----------|--------|---------|
| `Trying to set application code for speed {speed} on startHostLane {N}` | `CmisModule.cpp:3445` | AppSel being programmed — **disruptive** (requires datapath reset) |
| `Speed matches: currentApplication {hex}. Doing nothing` | `CmisModule.cpp:3403` | AppSel already correct — **hitless**, no reprogramming needed |
| `Port {name} starting datapath INIT` | `CmisModule.cpp:5432` | Datapath initialization — **disruptive** |
| `Port {name} starting datapath DEINIT` | Same | Datapath deinitialization — **disruptive** |
| `Port {name} DP_INIT completed in {N} ms` | Same | Datapath init finished |
| `Port {name} DP_DEINIT not compeleted` | Same | Datapath deinit timed out (note: typo "compeleted" is in source) |

### State Machine Transitions

```
grep -F "State changed from" <qsfp_service.log>
```

| Log String | Source | Meaning |
|-----------|--------|---------|
| `[SM]Transceiver:{ID} State changed from {old} to {new}` | `TransceiverStateMachine.h` | Every state machine transition (prefixed with `[SM]` via TCVR_SM_LOG) |

Normal programming cycle: `NOT_PRESENT → PRESENT → DISCOVERED → IPHY_PORTS_PROGRAMMED → XPHY_PORTS_PROGRAMMED → TRANSCEIVER_READY → TRANSCEIVER_PROGRAMMED → ACTIVE`

Remediation cycle: `INACTIVE → XPHY_PORTS_PROGRAMMED → TRANSCEIVER_READY → TRANSCEIVER_PROGRAMMED → ...`

### Agent Config Change Trigger

```
grep -F "triggerAgentConfigChangeEvent" <qsfp_service.log>
grep -F "triggerProgrammingEvents" <qsfp_service.log>
```

| Log String | Source | Meaning |
|-----------|--------|---------|
| `triggerAgentConfigChangeEvent has {N} transceivers state machines set back to discovered` | `TransceiverManager.cpp` | Agent config change (boot or config push) reset all transceivers to reprogramming cycle |
| `triggerProgrammingEvents has {N} IPHY programming, {N} XPHY programming, {N} TCVR programming` | `TransceiverManager.cpp` | Periodic programming check — shows how many transceivers need reprogramming |

## 7. Hardware Signal Patterns (qsfp_service.log)

```
grep -E "Read at offset.*failed" <qsfp_service.log>
grep -F "unknown Transceiver interface" <qsfp_service.log>
grep -F "Datapath could not deactivate" <qsfp_service.log>
grep -F "Module not ready" <qsfp_service.log>
grep -F "Coherent VDM stats not available" <qsfp_service.log>
```

| Log String | Meaning |
|-----------|---------|
| `Read at offset {N} with length {N} failed` | EEPROM I/O error — prefixed with `Transceiver:{N}` via WEDGE_QSFP_LOG. Persistent failures indicate bad module |
| `is detected but has an unknown Transceiver interface: {N}` | Module detected but unrecognized — prefixed with `Transceiver:{N}` via MODULE_LOG. Bad EEPROM or unsupported module |
| `Datapath could not deactivate even after waiting {N} uSec` | Datapath stuck — possible hardware issue |
| `Datapath didn't come out of deactivated state even after waiting {N} uSec` | Datapath init timeout — possible hardware issue |
| `Module not ready even after waiting {N} uSec` | Module power-up timeout — possible hardware issue |
| `Coherent VDM stats not available` | VDM read failure — may indicate degraded coherent optics module |

### Power/Optics Signals

```
grep -F "customizeTransceiverLocked" <qsfp_service.log>
grep -F "Module not in ready state" <qsfp_service.log>
grep -F "low power" <qsfp_service.log>
```

| Log String | Meaning |
|-----------|---------|
| `Module not in ready state` | Module failed to reach ready state after power-up |
| `Module is kept in low power mode for AppSel programming` | Normal — module in low power during programming |
| `Module already in high power and ready state` | Normal — no power transition needed |
| `Power override already correctly set, doing nothing` | Hitless — no power change |

## 8. Analysis Workflow

Follow this order when analyzing logs:

### Step 1: Find link flaps
```bash
grep -F "LinkState:" <wedge_agent.log> | grep -i "down"
```
If a port was specified, add `| grep "{port_name}"`. Build a timeline.

### Step 2: Check for boot events near flaps
```bash
grep -F "Will attempt" <wedge_agent.log> <qsfp_service.log>
```
If WARM boot + flaps → **software bug** (warmboot should be hitless).
If COLD boot + flaps → **expected behavior**.

### Step 3: Check for disruptive agent operations
```bash
grep -E "Create only attribute|Changing port (speed|resource) on up port|Reconfiguring port" <wedge_agent.log>
```
Any of these near a flap timestamp → **software-triggered**.

### Step 4: Check for disruptive qsfp_service operations
```bash
grep -E "allPortsDown|Doing datapath reinit|starting datapath|Trying to set application code|triggerAgentConfigChangeEvent" <qsfp_service.log>
```
Cross-reference timestamps with link DOWN events.

### Step 5: Warmboot flap analysis
If warmboot detected AND flaps occurred:
```bash
# Was AppSel reprogrammed unnecessarily?
grep -F "Speed matches" <qsfp_service.log>       # hitless — good
grep -F "Trying to set application code" <qsfp_service.log>  # reprogrammed — bad if speed didn't change
```

### Step 6: Check for hardware signals
```bash
grep -E "Read at offset.*failed|unknown Transceiver interface|Datapath could not deactivate|Module not ready" <qsfp_service.log>
```

### Step 7: Check for remediation loops
```bash
grep -F "Remediated, ports=" <qsfp_service.log> | sort | uniq -c | sort -rn
```
High counts for a single transceiver → remediation loop.

## 9. Suggested Fixes

### Software Fixes

| Root Cause | Evidence | Suggestion |
|-----------|----------|------------|
| Warmboot reprogramming | `Trying to set application code` after `Will attempt WARM boot` when speed didn't change | Compare current AppSel with desired before programming; skip if matching (check `getAppSelCodeForSpeed()` logic) |
| Remediation loop | `Remediated, ports=` appearing repeatedly for same Transceiver:{id} | Add exponential backoff to remediation; cap max retries. Current cooldowns: 120s initial, 360s repeat |
| Agent config push flap | `Create only attribute changed` or `Changing port speed on up port` after config change | Implement hitless config updates; only reprogram truly changed parameters |
| Unnecessary coldboot | `Will attempt COLD boot` when warmboot should have been possible | Check warmboot state file; investigate why `canWarmBoot` was false |
| Datapath reset during warmboot | `Programmed with resetting data path` after warmboot | Verify `needResetDataPath` is correctly set to false during warmboot path |

### Hardware Detection

| Issue | Evidence | Detection Strategy |
|-------|----------|--------------------|
| Bad EEPROM | `Read from transceiver {N}...failed` persistently | Track EEPROM read failure rate per module; alert when failures exceed N in M minutes |
| Unknown module | `unknown Transceiver interface` | Flag modules with unrecognized interface type; may need EEPROM reprogramming or replacement |
| Stuck datapath | `Datapath could not deactivate` or `didn't come out of deactivated state` | Track datapath timeout events per module; escalate after N timeouts |
| Module power failure | `Module not ready even after waiting` | Monitor module ready-state transitions; flag modules that consistently fail to reach ready state |
| Degraded coherent optics | `Coherent VDM stats not available` persistently | Compare VDM availability across modules of same type; flag outliers |
