# Validating Optics and Links on FBOSS Distro

## Overview

These manual tests validate that transceivers and links behave correctly on a
switch running the FBOSS distro image, with all FBOSS services running as they
do by default. They check:

- Link bring-up and LLDP neighbor discovery
- Transceiver DOM (Digital Optical Monitoring) readings
- Link recovery after host port disable / enable
- Link recovery after optics Tx disable / enable
- Host side and media side transceiver loopback

Run them at every port speed and application mode the module supports.

Unlike the [QSFP HAL tests](/docs/testing/qsfp_hal_test/), these tests run with
QSFP Service and the agent up, and exercise the datapath end to end.

This guide is for vendors who already have an FBOSS distro running on the
switch.

## Overall Outcomes

### Setup

- FBOSS services running
- the ports under test identified
- the port speeds and application modes to test identified

### Run Tests

- every port under test links up with the expected LLDP neighbor
- DOM readings are sane on every port under test
- links recover after every port disable / enable and Tx disable / enable
- loopback behaves as expected and the original link is restored afterwards
- all of the above holds at every port speed and application mode

## Setup

### Step 1: Confirm FBOSS Services Are Running

```bash
systemctl list-units 'fboss*' 'qsfp*' 'fsdb*'
```

`fboss_sw_agent`, every `fboss_hw_agent@N`, `qsfp_service` and `fsdb` should be
`active running`. If any of them is stopped, start it before continuing, for
example:

```bash
systemctl start fsdb qsfp_service fboss_hw_agents.target fboss_sw_agent
```

### Step 2: Identify the Ports Under Test

```bash
fboss2 show port
```

Note the `Name` of each port with a module under test, for example `eth1/9/1`.
Both `fboss2` and `wedge_qsfp_util` take the port name.

### Step 3: Identify the Port Speeds and Application Modes to Test

Each port speed and application mode is selected through a port profile. List
the profiles each port supports:

```bash
fboss2 show interface capabilities
```

Every profile the module supports must be tested.

## Run Tests

Run every test on every port with a module under test, and repeat the full set
for every port speed and application mode found in Setup Step 3. The examples
use port `eth1/9/1`.

### Change the Port Speed and Application Mode

Set the profile on the port, review the staged change, then apply it:

```bash
fboss2-dev config interface eth1/9/1 profile PROFILE_100G_4_NRZ_CL91
fboss2-dev config session diff      # review staged change
fboss2-dev config session commit    # apply
```

Set the same profile on the far end of the link, then confirm with
`fboss2 show port` that the `Speed` and `ProfileID` columns show the new values
before running the tests below.

### Test 1: Link Bring-up

```bash
fboss2 show port
```

Every port under test should show `AdminState` Enabled and `LinkState` Up.

### Test 2: LLDP

```bash
fboss2 show lldp
```

Every port under test should list the expected neighbor in `LLDP-Peer` and
`Peer-Int`, matching how the setup is cabled.

### Test 3: DOM Validation

```bash
fboss2 show transceiver eth1/9/1
wedge_qsfp_util eth1/9/1
```

Confirm that:

- vendor, part number, serial number and application / DSP firmware versions
  match the module under test
- temperature, supply voltage, Tx bias current, Tx power, Rx power and Rx SNR
  are reported for every lane, and are within the warning thresholds the module
  advertises
- no alarm or warning flags are set
- Tx power is at the configured target, and Rx power is consistent with the
  far-end Tx power and the link loss
- `fboss2` and `wedge_qsfp_util` report the same values

### Test 4: Host Port Disable / Enable

Repeat 10 times per port:

```bash
fboss2 set port eth1/9/1 state disable --yes
fboss2 show port eth1/9/1    # LinkState should be Down
fboss2 set port eth1/9/1 state enable --yes
fboss2 show port eth1/9/1    # LinkState should come back Up
fboss2 show lldp             # LLDP neighbor should be re-established
```

`--yes` skips the confirmation prompt. Record how long the link takes to come
back up after each enable.

### Test 5: Optics Tx Disable / Enable

`fboss2` does not have a Tx disable command, so this test uses
`wedge_qsfp_util`. Repeat 10 times per port:

```bash
wedge_qsfp_util eth1/9/1 --tx_disable
fboss2 show port eth1/9/1    # LinkState should be Down on both ends
wedge_qsfp_util eth1/9/1 --tx_enable
fboss2 show port eth1/9/1    # LinkState should come back Up
fboss2 show lldp             # LLDP neighbor should be re-established
```

### Test 6: Transceiver Loopback

Host side loopback loops the switch ASIC's signal back inside the module:

```bash
fboss2 set interface eth1/9/1 loopback transceiver_system enable --yes
fboss2 show port eth1/9/1
fboss2 show lldp
```

The local port should come up looped back to itself, and LLDP should show the
local switch and port as its own neighbor.

Media side loopback loops the signal received from the fiber back to the far
end:

```bash
fboss2 set interface eth1/9/1 loopback transceiver_line enable --yes
```

The far-end port should come up looped back to itself, with the far-end switch
and port as its own LLDP neighbor. Check this with `fboss2 show port` and
`fboss2 show lldp` on the far-end switch.

After each case, clear the loopback and confirm the original link and LLDP
neighbor are restored:

```bash
fboss2 set interface eth1/9/1 loopback transceiver_system disable --yes
fboss2 set interface eth1/9/1 loopback transceiver_line disable --yes
```

If the module does not advertise a loopback mode, record it in the report.

### Restore the Switch

Make sure every port under test is left with no loopback, Tx enabled and the
port enabled, then confirm links and LLDP with Test 1 and Test 2.

## Pass Criteria

- every port under test links up with the expected LLDP neighbor
- DOM readings are sane on every port under test
- every iteration of Test 4 and Test 5 recovers link and LLDP
- loopback behaves as described in Test 6, and the original link is restored
  once loopback is cleared
- all of the above holds at every port speed and application mode the module
  supports

## What to Report

- FBOSS distro image version
- port speeds and application modes (port profiles) tested
- output of `fboss2 show port`, `fboss2 show lldp`,
  `fboss2 show transceiver <port>` and `wedge_qsfp_util <port>` at the start,
  after each disruptive test and after loopback is cleared
- time from enable to link Up for each iteration of Test 4 and Test 5
- any iteration that did not recover, with the command output at that point
