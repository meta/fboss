# FBOSS External PHY Requirements

## 1. Scope

This document captures FBOSS requirements for external PHYs.

**FBOSS supports an external PHY only through a SAI SDK.** A PHY without a SAI SDK is not supported.

- **Section 3, External PHY Vendor Requirements:** what the PHY chip and its SAI SDK must support, and how we engage with the PHY vendor.
- **Section 4, Switch Vendor Requirements:** what the switch (board) vendor must provide for the external PHYs on the board.

### Contents

- [Requirement levels and delivery tiers](#2-requirement-levels-and-delivery-tiers)
- [External PHY Vendor Requirements](#3-external-phy-vendor-requirements)
- [Switch Vendor Requirements](#4-switch-vendor-requirements)

## 2. Requirement levels and delivery tiers

### 2.1 Requirement levels

- **Must Have (MH):** The vendor must satisfy "must have" requirements. By default, every requirement is MH unless it is marked otherwise.
- **Strong Desire (SD):** The vendor must support "strong desire" requirements. If one is not supported yet, the vendor gives a clear timeline for when it will be.
- **Wish List (WL):** "Wish list" requirements are what Meta is evaluating. The vendor states any limitation in supporting them.

### 2.2 Delivery tiers

Each requirement also has a delivery tier: the drop in which we recommend the vendor delivers it. Each vendor section defines what its tiers contain.

- **T0:** Features that cover critical functionality. They are needed to bring up the platform and to enable all other features.
- **T1:** Features that are more involved and help verify and operate the platform as a whole.
- **T2:** Features that are complex or related to performance and lifecycle operations. They normally do not block early bring-up on a new platform, but they do block platform qualification before deployment in production networks.

## 3. External PHY Vendor Requirements

### 3.1 How FBOSS drives an external PHY

```
qsfp_service (one process)
 ├─ SAI switch object per PHY chip   (SAI_SWITCH_TYPE_PHY)
 │    └─ MDIO access via FBOSS-provided REGISTER_READ / REGISTER_WRITE callbacks
 ├─ per logical port:
 │    ├─ system-side port
 │    ├─ line-side port
 │    └─ port connector
 └─ telemetry loop: every 10 s, all PHYs in parallel, every port, both sides
```

| ID | Requirement |
| --- | --- |
| <span style={{whiteSpace: 'nowrap'}}><b>G-1</b></span> | **SAI-based SDK**<br/>The SDK implements the SAI switch and port APIs for the PHY. FBOSS uses the standard SAI headers (`saiswitch.h`, `saiport.h`). |
| <span style={{whiteSpace: 'nowrap'}}><b>G-2</b></span> | **Standard SAI only**<br/>Vendor extensions are acceptable only where standard SAI has no equivalent. |
| <span style={{whiteSpace: 'nowrap'}}><b>G-3</b></span> | **One process, many chips**<br/>A single process creates N switch objects, one per PHY chip, and may call into different switch objects concurrently from different threads. The SDK must be multi-thread safe: no races, data corruption or deadlocks under concurrent calls. |
| <span style={{whiteSpace: 'nowrap'}}><b>G-4</b></span> | **No implicit behavior**<br/>The SDK avoids implementing features implicitly. When unavoidable, the vendor documents and shares the implicit behavior. |

Reference pointers in our open source code for external PHY SAI programming: [SaiPhyRetimer.cpp](https://github.com/meta/fboss/blob/main/fboss/lib/phy/SaiPhyRetimer.cpp), [BspSaiPhyManager.cpp](https://github.com/meta/fboss/blob/main/fboss/lib/phy/BspSaiPhyManager.cpp), [SaiPhyPlatform.cpp](https://github.com/meta/fboss/blob/main/fboss/agent/platforms/sai/SaiPhyPlatform.cpp)

### 3.2 Delivery tiers for external PHYs

| Tier | Features |
| --- | --- |
| **T0** | **Cold boot:** the SDK initializes every PHY chip from power-on, loads firmware, applies configuration and reports firmware status.<br/>**Port programming:** system-side port, line-side port and connector per logical port, with speed, lanes, FEC and TX equalization set independently per side.<br/>**Link training and precoding support:** per side.<br/>**Shell access and debug scripts:** a vendor diagnostic shell plus the commands to collect when a link has a problem. |
| **T1** | **PRBS and loopback:** per side, for bring-up and fault isolation.<br/>**PHY diagnostics:** per-side, per-lane signal detect, lock, SNR, FEC alignment and link-training status, polled continuously in production without disturbing the link.<br/>**Port stats:** FEC counters - including FEC histogram, symbol errors and corrected bits - per side for link health. |
| **T2** | **Warm boot:** `qsfp_service` restarts while the PHYs keep carrying traffic, with no link flap.<br/>**Speed change:** a port moves to a new speed or lane count without affecting other ports. |

### 3.3 T0: Bring-up

#### Initialization and cold boot

| ID | Requirement |
| --- | --- |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-1</b></span> | **SAI library entry points**<br/>`sai_api_initialize` accepts FBOSS's service method table (`profile_get_value`, `profile_get_next_value`) and uses it to read profile keys. `sai_api_query(SAI_API_SWITCH, SAI_API_PORT)` returns the switch and port API tables. `sai_log_set` levels are honored per API. |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-2</b></span> | **Boot type: `SAI_KEY_BOOT_TYPE`** (profile key)<br/>`0` = cold boot. Warm boot (`1`) is covered in T2. |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-3</b></span> | **Board config file: `SAI_KEY_INIT_CONFIG_FILE`** (profile key)<br/>Path to a vendor-format board config file: reference clock, lane map and polarity per lane and side, per-chip init settings. One file may cover all chips, or one file per chip selected by `SWITCH_PROFILE_ID`. The vendor documents the format. |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-4</b></span> | **Switch create: `SAI_SWITCH_ATTR_INIT_SWITCH = true`, `SAI_SWITCH_ATTR_TYPE = SAI_SWITCH_TYPE_PHY`** (create)<br/>Initializes the chip as a PHY switch. |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-5</b></span> | **Chip identity: `SAI_SWITCH_ATTR_SWITCH_HARDWARE_INFO`, `SAI_SWITCH_ATTR_SWITCH_PROFILE_ID`** (create)<br/>Hardware info identifies the chip instance (FBOSS passes the PHY ID) and must be stable across restarts. Each switch object reads its own profile values. |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-6</b></span> | **Hardware access: `SAI_SWITCH_ATTR_REGISTER_READ` / `REGISTER_WRITE`, `SAI_SWITCH_ATTR_PLATFROM_CONTEXT`, `SAI_SWITCH_ATTR_HARDWARE_ACCESS_BUS = MDIO`** (create)<br/>All hardware access goes through the FBOSS MDIO (Clause 45) callbacks. The SDK must not open device files or buses itself, and needs no kernel module. The platform context is passed back unchanged on every callback. |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-7</b></span> | **Firmware load: `SAI_SWITCH_ATTR_FIRMWARE_LOAD_METHOD = INTERNAL`, `SAI_SWITCH_ATTR_FIRMWARE_LOAD_TYPE = AUTO`** (create)<br/>Firmware ships inside the SDK and loads on cold boot. `SAI_SWITCH_ATTR_FIRMWARE_PATH_NAME` may be null. |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-8</b></span> | **Firmware status and version: `SAI_SWITCH_ATTR_FIRMWARE_STATUS`, `FIRMWARE_MAJOR_VERSION`, `FIRMWARE_MINOR_VERSION`** (get)<br/>Running firmware state and version. The version is deterministic for a given SDK release. |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-9</b></span> | **Cold-boot time**<br/>Cold-boot initialization of all external PHYs on the switch, including firmware load, finishes within 10 s (**SD**) / 30 s (**MH**). |

#### Port programming

| ID | Requirement |
| --- | --- |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-10</b></span> | **Lanes: `SAI_PORT_ATTR_HW_LANE_LIST`** (create, get)<br/>Separate system-side and line-side ports, each with its own lane list. Lane counts may differ between the two sides (gearbox). |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-11</b></span> | **Speed: `SAI_PORT_ATTR_SPEED`** (create, get)<br/>All speeds and lane counts required by the target platform. |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-12</b></span> | **Admin state: `SAI_PORT_ATTR_ADMIN_STATE`** (create, set, get)<br/>Per side. After admin down → up, the side restarts adaptation (and link training, if enabled) from scratch. |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-13</b></span> | **FEC: `SAI_PORT_ATTR_FEC_MODE`** (create, get)<br/>Set independently per side (for example, line side RS544_2N with system side NONE). Unsupported FEC modes are rejected with an error at create time, never accepted silently. |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-14</b></span> | **Oper status: `SAI_PORT_ATTR_OPER_STATUS`** (get)<br/>Reflects the real link state on that side. |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-15</b></span> | **Connector: `SAI_PORT_CONNECTOR_ATTR_SYSTEM_SIDE_PORT_ID`, `LINE_SIDE_PORT_ID`** (create, get)<br/>Data path between the two ports. Both getters return the programmed IDs. |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-16</b></span> | **Serdes: `SAI_PORT_SERDES_ATTR_PORT_ID`** (create)<br/>One serdes object per side. |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-17</b></span> | **TX FIR: `SAI_PORT_SERDES_ATTR_TX_FIR_PRE1`, `MAIN`, `POST1` (per lane)** (create, get)<br/>Used when link training is off on that side. Getters return the values programmed in hardware. |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-18</b></span> | **Extended TX FIR: `SAI_PORT_SERDES_ATTR_TX_FIR_PRE2`, `POST2`, `POST3` (per lane)** (create, get)<br/>Same as T0-17. Unsupported taps return `SAI_STATUS_ATTR_NOT_SUPPORTED_*` and are documented. |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-19</b></span> | **Port removal order** (remove)<br/>FBOSS removes in this order: connector, serdes, line port, system port. Each removal succeeds without affecting other ports. |

#### Link training and precoding

| ID | Requirement |
| --- | --- |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-20</b></span> | **Link training: `SAI_PORT_ATTR_LINK_TRAINING_ENABLE`** (create, get)<br/>Per side and independent (system only, line only, both, or neither). When link training is on, the trained result owns the TX FIR, and FBOSS does not program taps. |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-21</b></span> | **Link-training restart on timeout**<br/>If link training does not complete within a timeout, the firmware restarts it automatically. This is enabled by default. |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-22</b></span> | **Precoding: `SAI_PORT_SERDES_ATTR_TX_PRECODING`, `SAI_PORT_SERDES_ATTR_RX_PRECODING` (per lane)** (create, get)<br/>Settable at create time per side. Getters return the configured value, never empty. |

#### Shell access and debug scripts

| ID | Requirement |
| --- | --- |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-23</b></span> | **Diagnostic shell: `SAI_SWITCH_ATTR_SWITCH_SHELL_ENABLE`** (set)<br/>Starts the vendor diagnostic shell for that switch object on the process's stdin/stdout. FBOSS runs it on a PTY, so it must work without a real terminal. A shell open on one chip must not block calls on other chips. |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-24</b></span> | **Shell commands**<br/>At minimum: list chips and ports, switch chip context, per-lane dump (signal detect, lock, SNR, adaptation and firmware state, TX taps, precoding), lane map and polarity, firmware version, raw register read and write. Read-only commands must not disturb the link. |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-25</b></span> | **Debug scripts**<br/>The vendor documents the shell commands and provides the standard set of commands and data to collect when a link is down or flapping. |
| <span style={{whiteSpace: 'nowrap'}}><b>T0-26</b></span> | **(WL) Programmatic shell API**<br/>A SAI or C API that runs one shell command and returns its output, without a PTY. |

### 3.4 T1: Monitoring and debug

#### PRBS and loopback

| ID | Requirement |
| --- | --- |
| <span style={{whiteSpace: 'nowrap'}}><b>T1-1</b></span> | **PRBS: `SAI_PORT_ATTR_PRBS_POLYNOMIAL`, `SAI_PORT_ATTR_PRBS_CONFIG` (`ENABLE_TX_RX` / `DISABLE`)** (set, get)<br/>Per side, with at least PRBS31. |
| <span style={{whiteSpace: 'nowrap'}}><b>T1-2</b></span> | **(SD) Additional PRBS polynomials** (set, get)<br/>PRBS7, 9, 13, 15 and 23, where the line rate allows. |
| <span style={{whiteSpace: 'nowrap'}}><b>T1-3</b></span> | **PRBS status: `SAI_PORT_ATTR_PRBS_RX_STATE`** (get)<br/>Status (`OK`, `LOCK_WITH_ERRORS`, `NOT_LOCKED`, `LOST_LOCK`) plus error count. |
| <span style={{whiteSpace: 'nowrap'}}><b>T1-4</b></span> | **Loopback: `SAI_PORT_ATTR_LOOPBACK_MODE`** (set, get)<br/>Per side. |

#### PHY diagnostics

FBOSS reads these every 10 s on every port, both sides and all lanes, unless noted otherwise.

| ID | Requirement |
| --- | --- |
| <span style={{whiteSpace: 'nowrap'}}><b>T1-5</b></span> | **Signal detect: `SAI_PORT_ATTR_RX_SIGNAL_DETECT`** (get)<br/>Per lane `{current, changed}`. |
| <span style={{whiteSpace: 'nowrap'}}><b>T1-6</b></span> | **CDR lock: `SAI_PORT_ATTR_RX_LOCK_STATUS`** (get)<br/>Per lane `{current, changed}`. |
| <span style={{whiteSpace: 'nowrap'}}><b>T1-7</b></span> | **FEC alignment: `SAI_PORT_ATTR_FEC_ALIGNMENT_LOCK`** (get)<br/>Per FEC lane `{current, changed}`, on ports with RS FEC enabled. |
| <span style={{whiteSpace: 'nowrap'}}><b>T1-8</b></span> | **SNR: `SAI_PORT_ATTR_RX_SNR`** (get)<br/>Per lane, in units of 1/256 dB. |
| <span style={{whiteSpace: 'nowrap'}}><b>T1-9</b></span> | **Link-training status: `SAI_PORT_ATTR_LINK_TRAINING_RX_STATUS`** (get)<br/>Per side when link training is enabled. |
| <span style={{whiteSpace: 'nowrap'}}><b>T1-10</b></span> | **(SD) Link-training failure reason: `SAI_PORT_ATTR_LINK_TRAINING_FAILURE_STATUS`** (get)<br/>Per side when link training is enabled. |
| <span style={{whiteSpace: 'nowrap'}}><b>T1-11</b></span> | **Link state and lanes: `SAI_PORT_ATTR_OPER_STATUS`, `SAI_PORT_ATTR_HW_LANE_LIST`** (get)<br/>Current link state and lane list per side. |
| <span style={{whiteSpace: 'nowrap'}}><b>T1-12</b></span> | **SI settings readback: TX FIR (T0-17, T0-18), TX/RX precoding (T0-22)** (get)<br/>Read every 360 s and on demand. Returns the values in effect in hardware, per lane. |
| <span style={{whiteSpace: 'nowrap'}}><b>T1-13</b></span> | **Diagnostics read time**<br/>One full round of T1-5 to T1-11 and T1-14 to T1-17 for one chip, all ports and both sides, finishes within 10 ms (**SD**) / 30 ms (**MH**) combined. |

#### Port stats

| ID | Requirement |
| --- | --- |
| <span style={{whiteSpace: 'nowrap'}}><b>T1-14</b></span> | **FEC frames: `SAI_PORT_STAT_IF_IN_FEC_CORRECTABLE_FRAMES`, `SAI_PORT_STAT_IF_IN_FEC_NOT_CORRECTABLE_FRAMES`** (get)<br/>Per side. |
| <span style={{whiteSpace: 'nowrap'}}><b>T1-15</b></span> | **FEC corrected bits: `SAI_PORT_STAT_IF_IN_FEC_CORRECTED_BITS`** (get)<br/>Per side. Used for pre-FEC BER. |
| <span style={{whiteSpace: 'nowrap'}}><b>T1-16</b></span> | **FEC histogram: `SAI_PORT_STAT_IF_IN_FEC_CODEWORD_ERRORS_S0` … `S16`** (get)<br/>Per-codeword symbol-error histogram (S0–S15 for RS544). |
| <span style={{whiteSpace: 'nowrap'}}><b>T1-17</b></span> | **FEC symbol errors: `SAI_PORT_STAT_IF_IN_FEC_SYMBOL_ERRORS`** (get)<br/>Returns `NOT_SUPPORTED` if unavailable. |

### 3.5 T2: Warm boot and speed change

#### Warm boot sequence

| Step | Shutdown (`qsfp_service` exits) | Warm boot (`qsfp_service` restarts) |
| --- | --- | --- |
| 1 | `set(SAI_SWITCH_ATTR_RESTART_WARM = true)` | Profile `SAI_KEY_BOOT_TYPE = 1`, plus `SAI_KEY_WARM_BOOT_READ_FILE` / `SAI_KEY_WARM_BOOT_WRITE_FILE` |
| 2 | `set(SAI_SWITCH_ATTR_PRE_SHUTDOWN = true)` may be sent. Accept it, or treat it as a no-op | `create_switch(INIT_SWITCH = true, FIRMWARE_LOAD_TYPE = SKIP, WARM_RECOVER = true)` with the same identity attributes as cold boot |
| 3 | FBOSS saves its own state: adapter keys, adapter host keys and switch state | FBOSS rebuilds its object list from its cached object IDs |
| 4 | `remove_switch`. The chip keeps running and carrying traffic | FBOSS reads back every create attribute of every object |

#### Warm boot

| ID | Requirement |
| --- | --- |
| <span style={{whiteSpace: 'nowrap'}}><b>T2-1</b></span> | **Hitless**<br/>With all ports up and traffic at line rate, a `qsfp_service` warm boot causes 0 link flaps on either side and no link-training restarts. |
| <span style={{whiteSpace: 'nowrap'}}><b>T2-2</b></span> | **Shutdown keeps the link**<br/>`remove_switch` after `RESTART_WARM` does not reset the chip, reload firmware, retrain or drop the link. |
| <span style={{whiteSpace: 'nowrap'}}><b>T2-3</b></span> | **Warm restore**<br/>Warm `create_switch` restores from `SAI_KEY_WARM_BOOT_READ_FILE` without reset, firmware reload, retrain or any data-path write. |
| <span style={{whiteSpace: 'nowrap'}}><b>T2-4</b></span> | **Stable object IDs**<br/>Object IDs (switch, ports, serdes, connectors) are identical across warm boot, so FBOSS's cached keys resolve. |
| <span style={{whiteSpace: 'nowrap'}}><b>T2-5</b></span> | **Exact readback**<br/>Every create attribute is readable after warm boot and returns exactly the value in effect: speed, lanes, FEC, admin state, link-training enable, TX FIR, TX/RX precoding, connector IDs. An empty or default readback makes FBOSS reprogram a live link, so it counts as a failure. |
| <span style={{whiteSpace: 'nowrap'}}><b>T2-6</b></span> | **Changes after warm boot**<br/>Create, set and remove after warm boot work normally and are included in the next warm-boot save. |
| <span style={{whiteSpace: 'nowrap'}}><b>T2-7</b></span> | **Parallel restore**<br/>Warm restore of all N PHY chips in parallel threads is safe. |
| <span style={{whiteSpace: 'nowrap'}}><b>T2-8</b></span> | **Cold boot after warm shutdown**<br/>A cold boot (`BOOT_TYPE = 0`) after a warm-capable shutdown fully re-initializes the chip, regardless of any leftover state files. |

#### Speed change

| ID | Requirement |
| --- | --- |
| <span style={{whiteSpace: 'nowrap'}}><b>T2-9</b></span> | **Re-create at a new speed** (remove, create)<br/>FBOSS changes a port's speed or lane count by removing its connector, serdes and both ports (T0-19) and creating them again with the new values. This works repeatedly, with no leaked objects or resources. |
| <span style={{whiteSpace: 'nowrap'}}><b>T2-10</b></span> | **No impact on other ports**<br/>Changing one port's speed does not disturb any other port on the same chip, on either side. |

### 3.6 External PHY Hardware Requirements

#### Management

| ID | Requirement |
| --- | --- |
| <span style={{whiteSpace: 'nowrap'}}><b>PHY-1</b></span> | **MDIO Clause 45**<br/>The PHY is managed over Clause 45 MDIO. No feature requires I2C, SPI or a vendor-specific side channel.<br/>Example: FBOSS creates each PHY with `SAI_SWITCH_ATTR_HARDWARE_ACCESS_BUS = MDIO` and its own register read/write callbacks in [SaiPhyRetimer.cpp](https://github.com/meta/fboss/blob/main/fboss/lib/phy/SaiPhyRetimer.cpp). |
| <span style={{whiteSpace: 'nowrap'}}><b>PHY-2</b></span> | **MDIO clock**<br/>The PHY supports a clock of at least **9 MHz**. |

#### Reset

| ID | Requirement |
| --- | --- |
| <span style={{whiteSpace: 'nowrap'}}><b>PHY-3</b></span> | **Hardware reset pin**<br/>Each PHY chip has a dedicated hardware reset pin. Asserting it returns the whole chip, including firmware, to the same state as just after power-on. |
| <span style={{whiteSpace: 'nowrap'}}><b>PHY-4</b></span> | **Reset timing**<br/>The vendor documents the minimum reset assert time and the time from reset release until the PHY responds on MDIO. |

#### Firmware

| ID | Requirement |
| --- | --- |
| <span style={{whiteSpace: 'nowrap'}}><b>PHY-5</b></span> | **Firmware over MDIO**<br/>The PHY supports firmware download from the SDK over MDIO on cold boot. Firmware download over MDIO meets the cold-boot time in T0-9. |
| <span style={{whiteSpace: 'nowrap'}}><b>PHY-6</b></span> | **(WL) Skip firmware download on cold boot**<br/>On cold boot, the PHY skips redownloading the firmware over MDIO. |

### 3.7 Vendor engagement

#### SDK

- One SDK for all platforms that use a certain external PHY.
- For newer platforms, Meta is initially OK with a different SDK for feature development, but the vendor must converge on one SDK for all platforms and give a clear ETA for it.
- The SDK is SAI based (G-1).

#### SDK release cadence

- For platforms in production, Meta takes only patches on the SDK deployed in production.
- For platforms in feature development, the vendor releases an SDK every 4 weeks. Each release enables more features or HW tests.

#### Open source validation

- Vendors are expected to qualify their external phy with FBOSS open source (OSS) tests.

#### Warm boot upgrades

- Every patch, other than an SDK upgrade, is warm-boot upgradeable and downgradeable.
- Every feature estimate includes the warm-boot effort.

#### Interfacing with the vendor

- Meta works with one vendor software team for all platforms, with one POC.
- The vendor provides continued support (for example, sufficient staffing).

#### Support

- Join a debugging meeting quickly when needed.
- Turn around production fixes quickly.
- Share the generic set of data (SDK state and shell dumps) to collect during a production issue (T0-25).
- Proactively try to reproduce issues in house.
- Hold weekly meetings on open issues and the roadmap.
- Tell Meta early if a feature slips.

## 4. Switch Vendor Requirements

### 4.1 Hardware requirements

#### MDIO buses

| ID | Requirement |
| --- | --- |
| <span style={{whiteSpace: 'nowrap'}}><b>BRD-1</b></span> | **Dedicated MDIO from the MDIO controller (usually FPGA)**<br/>Every external PHY is reached over an MDIO controller (usually implemented in the system FPGA). MDIO buses to external PHYs are not shared with any other device type. |
| <span style={{whiteSpace: 'nowrap'}}><b>BRD-2</b></span> | **One MDIO controller per PHY**<br/>Each external PHY has its own MDIO controller, with its own reset. This lets FBOSS access all PHYs in parallel, so an access to one PHY never waits for another. |
| <span style={{whiteSpace: 'nowrap'}}><b>BRD-3</b></span> | **MDIO clock**<br/>The FPGA MDIO controller supports a default clock of **9 MHz** or higher, but should support as low as 2.5 MHz. |
| <span style={{whiteSpace: 'nowrap'}}><b>BRD-4</b></span> | **MDIO controller reset**<br/>Each MDIO controller can be reset on its own, to recover a hung bus, without affecting other controllers or the PHYs. |

#### Reset and clock

| ID | Requirement |
| --- | --- |
| <span style={{whiteSpace: 'nowrap'}}><b>BRD-5</b></span> | **Per-PHY reset line**<br/>Each PHY has a hardware reset pin. A reset line is never shared between PHYs and is independent of the MDIO controller reset. |
| <span style={{whiteSpace: 'nowrap'}}><b>BRD-6</b></span> | **Reference clock**<br/>Each PHY receives a reference clock that meets the PHY vendor's requirements. |

#### Board data

| ID | Requirement |
| --- | --- |
| <span style={{whiteSpace: 'nowrap'}}><b>SW-1</b></span> | **PHY mapping**<br/>The switch vendor provides the per-PHY mapping in the BSP mapping CSV and the platform mapping CSVs.<br/>See [External PHY mapping in BSP mapping](../developing/bsp_mapping.md#external-phy-mapping) and [External PHYs (XPHY) in Platform mapping](../developing/platform_mapping.md#external-phys-xphy) for the file formats and examples. |

### 4.2 BSP requirements

| ID | Requirement |
| --- | --- |
| <span style={{whiteSpace: 'nowrap'}}><b>SW-2</b></span> | **MDIO controller device**<br/>Each MDIO controller exports one character device for the whole bus, not one per PHY. Platform Manager links it as:<br/>`/run/devmap/mdio-busses/mdio_bus_io_<N>`<br/>Example: the `IoPath` column in [Ladakh800bcls_PhyBspMapping.csv](https://github.com/meta/fboss/blob/main/fboss/lib/bsp/bspmapping/input/Ladakh800bcls_PhyBspMapping.csv). `qsfp_service` opens it in [BspDeviceMdio.cpp](https://github.com/meta/fboss/blob/main/fboss/mdio/BspDeviceMdio.cpp). |
| <span style={{whiteSpace: 'nowrap'}}><b>SW-3</b></span> | **MDIO transactions**<br/>The character device supports `FBIOC_MDIO_C45_READ` and `FBIOC_MDIO_C45_WRITE` with `struct fb_mdio_xfer { phy_addr; dev_addr; reg_addr; reg_data; }`, as defined in [fbiob-ioctl.h](https://github.com/meta/fboss/blob/main/fboss/platform/platform_manager/uapi/fbiob-ioctl.h). Each call is one complete Clause 45 access.<br/>Example: client in [BspDeviceMdio.cpp](https://github.com/meta/fboss/blob/main/fboss/mdio/BspDeviceMdio.cpp). |
| <span style={{whiteSpace: 'nowrap'}}><b>SW-4</b></span> | **MDIO controller reset**<br/>Each MDIO controller exports a sysfs reset control that Platform Manager links as:<br/>`/run/devmap/mdio-busses/mdio_bus_ctrl_<N>/reset_bus` |
| <span style={{whiteSpace: 'nowrap'}}><b>SW-5</b></span> | **Per-PHY reset**<br/>Each PHY reset line is exported as a sysfs reset control that Platform Manager links as:<br/>`/run/devmap/rtms/rtm_ctrl_<M>/rtm_reset_<n>`<br/>Writing `1` holds the PHY in reset and writing `0` releases it.<br/>Example: the `PhyResetPath` column in [Ladakh800bcls_PhyBspMapping.csv](https://github.com/meta/fboss/blob/main/fboss/lib/bsp/bspmapping/input/Ladakh800bcls_PhyBspMapping.csv). |
| <span style={{whiteSpace: 'nowrap'}}><b>SW-6</b></span> | **Concurrency**<br/>Different MDIO controllers can be accessed at the same time from different threads or processes. An access on one controller never blocks an access on another. The BSP driver handles concurrency on each controller: it serializes calls and keeps each Clause 45 access atomic, so callers need no locking of their own. |
