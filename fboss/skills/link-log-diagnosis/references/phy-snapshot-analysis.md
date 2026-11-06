# PHY Snapshot Analysis

LinkSnapshot data from `wedge_agent_snapshots.log` and `qsfp_service_snapshots.log` provides hardware-level diagnostic signals for link flap root cause analysis.

## LinkSnapshot Structure

Defined in `fboss/lib/phy/phy.thrift`. LinkSnapshot is a union:
- `PhyInfo` — ASIC-side PHY diagnostics (from wedge_agent_snapshots.log)
- `TransceiverInfo` — optics-side diagnostics (from qsfp_service_snapshots.log)

### PhyInfo Key Fields

```
PhyInfo
├── state: PhyState
│   ├── linkState (UP/DOWN)
│   ├── speed
│   ├── system: PhySideState (ASIC → SerDes)
│   │   ├── pmd: PmdState
│   │   │   ├── lanes[]: LaneState
│   │   │   │   ├── signalDetectLive (bool)
│   │   │   │   ├── cdrLockLive (bool)
│   │   │   │   └── lane (int)
│   │   │   └── (optional) eye/txSettings
│   │   └── pcs: PcsState (FEC counters)
│   └── line: PhySideState (SerDes → optics)
│       └── pmd → same structure
│   └── linkFaultStatus: LinkFaultStatus
│       ├── localFault (bool)
│       └── remoteFault (bool)
├── stats: PhyStats
│   ├── system/line: PhySideStats
│   │   ├── pmd.lanes[]: PmdLaneStats (SNR, eye info)
│   │   └── pcs: PcsStats
│   │       └── rsFec: RsFecInfo
│   │           ├── correctedCodewords (i64)
│   │           └── uncorrectedCodewords (i64)
│   └── linkFaultStatus (same as state)
```

## Diagnostic Decision Tree

### Fault Status Interpretation

**IMPORTANT: Fault status is always from the perspective of the device whose PHY snapshot you are reading.**
- `localFault` on device X means device X's ASIC has issues on its **receive** path (could be caused by bad transmit from the remote side, local RX hardware, or fiber).
- `remoteFault` on device X means the **remote** device is reporting a fault on **its** receive path (the remote side is asserting a fault indication over the link).

Always state which device's snapshot you are reading when reporting fault status.

| localFault | remoteFault | signalDetect | cdrLock | Interpretation (from this device's perspective) |
|------------|-------------|--------------|---------|----------------|
| False | True | True | True | **Remote side has RX issue** — this device's local signal is healthy, but the remote switch reports a fault on its receive path. Investigate remote side's RX, or this device's TX. |
| True | False | False | False | **Local RX signal loss** — no light reaching this device's receiver. Check fiber, remote TX, or optics hardware. |
| True | False | True | True | **Local ASIC RX issue** — signal present at this device but ASIC reporting fault. Check ASIC config or FEC. |
| True | True | False | False | **Complete link failure** — both sides down, no signal. Fiber cut or both-side hardware issue. |
| False | False | True | True | **Healthy link** — no faults, signal locked. |

### FEC Analysis

- `uncorrectedCodewords > 0` near a flap → signal quality degradation preceded the flap
- `correctedCodewords` increasing rapidly → link is marginal, may flap under stress
- Compare `uncorrectedCodewords` before and during the flap window

### SNR (Signal-to-Noise Ratio)

- Found in `stats.line.pmd.lanes[].snr` or `stats.system.pmd.lanes[].snr`
- Low SNR preceding a flap → signal degradation (dirty fiber, failing optics, bad connector)

## Searching Snapshot Logs

### Grep Patterns for Transceiver Logs

For a port `eth1/N/x`, the transceiver ID is `N-1`. Use these patterns to find relevant entries:

```bash
# Primary patterns (cover most log formats)
grep -E "TransceiverID\(${TCVR_ID}\)" <qsfp_service_snapshots.log>
grep -E "ransceiver ${TCVR_ID}[^0-9]" <qsfp_service.log>
grep -E "ransceiver:${TCVR_ID}[^0-9]" <qsfp_service.log>
grep -E "=${TCVR_ID}[^0-9]" <qsfp_service.log>
```

### Grep Patterns for PHY Snapshots

```bash
# Find LinkSnapshot entries near a timestamp
grep -E "LinkSnapshot|PhyInfo|linkFaultStatus" <wedge_agent_snapshots.log>

# Filter to specific port
grep -E "eth1/N/x|portId.*{port_id}" <wedge_agent_snapshots.log>
```

### Time-Window Extraction

To find snapshots around a flap event at time HH:MM:SS, search a window of ±2 minutes. Snapshot logs are typically collected every 30-60 seconds.

## Common Root Cause Signatures

### Warmboot Flap (Remote Side Reprogrammed)

```
Device A PHY:  remoteFault=True, signalDetect=True, cdrLock=True
Device B logs: "Will attempt WARM boot" → "Trying to set application code" → "with resetting data path"
```
**Cause:** Device B's warmboot unnecessarily reprogrammed its transceiver with a datapath reset, disrupting its TX. Device A sees remoteFault because Device B is asserting fault on its RX path during reprogram.

### Cold Boot Flap (Expected)

```
Device A PHY:  localFault=True, signalDetect=False, cdrLock=False
Device B logs: "Will attempt COLD boot" → "with resetting data path"
```
**Cause:** Device B cold boot — expected behavior. Device B's TX goes down, so Device A loses signal (localFault with no signalDetect).

### Hardware Signal Loss (No Boot)

```
Device A PHY:  localFault=True, signalDetect=False, cdrLock=False, uncorrectedFEC > 0
No warmboot events nearby on either side
```
**Cause:** Hardware event — fiber issue, failing optics, or remote-side hardware problem. Check SNR trend.

### Remediation Loop

```
Device A PHY:  alternating remoteFault/localFault
Device A qsfp_service: repeated "Remediated, ports=" for same Transceiver:{id} (>3x in 30 min)
```
**Cause:** Software remediation loop — remediation action itself causes the next flap.
**NOTE:** Remediation only fires after a port has been down for 120 seconds (initial cooldown). It can NEVER be the initial cause of a flap — only a secondary cause that prolongs or repeats flapping.
