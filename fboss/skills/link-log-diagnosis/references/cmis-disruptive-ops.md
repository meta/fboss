# CMIS Disruptive vs Hitless Operations

Operations on CMIS transceivers categorized by whether they disrupt the link.

## Disruptive Operations (Cause Link Flaps)

| Operation | CMIS Register/Action | Impact | Log String |
|-----------|---------------------|--------|------------|
| DataPathDeinit | Write DATA_PATH_DEINIT register | Affected lanes go down | `"DATA_PATH_DEINIT set and reset done for host lane mask 0x{hex}"` |
| DataPathInit | Clear DATA_PATH_DEINIT, wait for activation | Lanes come back up (may take seconds) | `"Port {name} starting datapath INIT"` |
| AppSel Code Change | Write STAGED_CONTROL_SET + DataPath reset | Speed/mode change requires full lane reset | `"Trying to set application code for speed {speed}"` |
| Module Reset | Hardware reset via triggerModuleReset() | All ports on module drop | `"allPortsDown = true. Performing potentially disruptive remediations"` |
| Low Power → High Power | Clear LowPowerMode bit, wait for ModuleReady | ~100ms outage, all ports affected | `"Clearing low power bit to enable high power mode"` |
| TX Disable | Set TX_DISABLE for specific lanes | Affected lanes stop transmitting | via `setTransceiverTxLocked()` |
| Firmware Upgrade | CDB command sequence | Extended outage (minutes) | `"Upgrading CMIS Module Firmware"` |
| Loopback Enable/Disable | Set loopback register for lanes | Traffic path changes | via `setTransceiverLoopbackLocked()` |

## Hitless Operations (No Link Flaps)

| Operation | What It Does | Log String |
|-----------|-------------|------------|
| EEPROM cache refresh | Re-read module EEPROM pages into memory | `"Performing partial qsfp data cache refresh"` |
| VDM stats read | Read Vendor Diagnostic Monitoring counters | `"Coherent VDM stats not available"` (on failure) |
| Temperature/voltage read | Monitor module health sensors | No specific log (part of cache refresh) |
| Power override check (no change) | Verify power state is correct | `"Power override already correctly set, doing nothing"` |
| AppSel check (no change) | Verify speed/application matches | `"Speed matches: currentApplication {hex}. Doing nothing"` |
| RX equalizer config | Set host-side RX EQ values | `"configureModule for application {app} starting on host lane {N}"` |

## Key Decision Points in Code

### When does qsfp_service decide to reprogram?

1. **Agent config change** (`TransceiverManager.cpp:910`): Fires `RESET_TO_DISCOVERED` for all present transceivers → full reprogramming cycle. Both warmboot and coldboot trigger this.

2. **Remediation** (`TransceiverStateMachine.h:484-501`): When ports are INACTIVE and remediation guard passes → sets `isTransceiverProgrammed=false` → re-enters programming cycle.

3. **New transceiver insertion**: `DETECT_TRANSCEIVER` event → transitions back to `PRESENT` → full programming cycle.

4. **Periodic check** (`TransceiverManager.cpp:1193`): `triggerProgrammingEvents()` checks state machine attributes and fires programming events for any transceiver not fully programmed.

### What makes warmboot hitless?

During warmboot, `getAppSelCodeForSpeed()` checks if the current application/speed matches what's desired:
- If match → returns `std::nullopt` → **no datapath reset** → hitless
- If mismatch → returns new AppSel code → **datapath reset required** → disruptive

A warmboot flap is a **bug** if the speed/application didn't actually change. Look for `"Trying to set application code"` after `"Will attempt WARM boot"` — this means qsfp_service thought reprogramming was needed when it shouldn't have been.
