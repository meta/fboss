# FBOSS BSP Mapping

A platform's BSP mapping is derived directly from its `platform_manager.json` (`fboss/configs/platforms/VENDOR/PLATFORM/platform_stack/platform_manager.json`).

The same configuration that PlatformManager uses to create the transceiver and LED device nodes is also the single source of the BSP mapping consumed by `qsfp_service` and `led_service`. There is no separate BSP mapping input to maintain.

For example, the `lanesPerPort` field on a port's LED control block, together with `ledPerPort`, lets the lane-to-LED mapping be derived directly from `platform_manager.json`: the port's lanes are distributed evenly across its LEDs, so a port with `lanesPerPort: 8` and `ledPerPort: 2` maps lanes 1–4 to the first LED and lanes 5–8 to the second.

The exception is platforms with external PHYs (today Leh and Ladakh). The `platform_manager.json` builder doesn't support external PHYs, so their whole BSP mapping, transceivers and PHYs, still comes from CSV files; see [External PHY mapping](#external-phy-mapping) below.

## Transceiver access paths

For each transceiver `N`, PlatformManager creates a consistent set of device nodes under `/run/devmap/xcvrs/`, all keyed off the transceiver id — so transceiver `N` always uses index `N`, with no per-platform translation table:

- `xcvr_io_N` — I2C data path to the module; `qsfp_service` uses it to read/write the module memory map (EEPROM/ID, DOM, registers, config).
- `xcvr_ctrl_N` — control node exposing `xcvr_reset_N` (reset) and `xcvr_present_N` (presence detect).

## External PHY mapping

On platforms with external PHYs, `qsfp_service` needs to know how to reach, communicate with and reset every PHY. This comes from two CSV files in [`fboss/lib/bsp/bspmapping/input/`](https://github.com/meta/fboss/tree/main/fboss/lib/bsp/bspmapping/input):

- `PLATFORM_BspMapping.csv`: transceivers and LEDs. Required: the generator starts from this file and takes the PIM ID from it.
- `PLATFORM_PhyBspMapping.csv`: external PHYs, one row per PHY. Read only if it exists next to the file above, with the same `PLATFORM` prefix.

Examples: [Ladakh800bcls_PhyBspMapping.csv](https://github.com/meta/fboss/blob/main/fboss/lib/bsp/bspmapping/input/Ladakh800bcls_PhyBspMapping.csv) and [Leh800bcls_PhyBspMapping.csv](https://github.com/meta/fboss/blob/main/fboss/lib/bsp/bspmapping/input/Leh800bcls_PhyBspMapping.csv).

### PHY CSV columns

Every row has exactly these 9 columns, in this order:

| Column | Definition | Example (Ladakh) |
|--------|------------|------------------|
| **PhyId** | Unique ID of the PHY, starting at 0. It must match the PHY's `physicalID` in the platform mapping, which is the static mapping `CHIP_ID` minus 1 (see [Platform Mapping](https://meta.github.io/fboss/docs/developing/platform_mapping/#external-phys-xphy)). | `0` |
| **PhyCoreId** | Core within the PHY chip, for PHYs with more than one core. Set it to `0` for single-core PHYs such as Agera3. FBOSS doesn't use this value today. | `0` |
| **PimId** | PIM the PHY is on. Use the same value as `PimId` in `PLATFORM_BspMapping.csv`; this is `1` on fixed (non-PIM) platforms. | `1` |
| **PhyResetPath** | sysfs file that resets this PHY. Each PHY has its own reset path. | `/run/devmap/rtms/rtm_ctrl_mdio_l_2_1/rtm_reset_2` |
| **IoControlType** | Management interface. Only `MDIO` is supported. | `MDIO` |
| **IoControllerId** | ID of the MDIO controller the PHY is on. PHYs on the same MDIO bus share this ID. | `2` |
| **IoControllerResetPath** | sysfs file that resets the MDIO controller. | `/run/devmap/mdio-busses/mdio_bus_ctrl_l_2/reset_bus` |
| **IoPath** | Device path of the MDIO controller. | `/run/devmap/mdio-busses/mdio_bus_io_l_2` |
| **PhyAddr** | MDIO address of the PHY on its bus. It only has to be unique within one controller. | `0` |

`IoControllerResetPath` and `IoPath` describe the controller, not the PHY.

```
PhyId,PhyCoreId,PimId,PhyResetPath,IoControlType,IoControllerId,IoControllerResetPath,IoPath,PhyAddr
0,0,1,/run/devmap/rtms/rtm_ctrl_mdio_r_1_1/rtm_reset_57,MDIO,1,/run/devmap/mdio-busses/mdio_bus_ctrl_r_1/reset_bus,/run/devmap/mdio-busses/mdio_bus_io_r_1,0
1,0,1,/run/devmap/rtms/rtm_ctrl_mdio_r_1_2/rtm_reset_58,MDIO,1,/run/devmap/mdio-busses/mdio_bus_ctrl_r_1/reset_bus,/run/devmap/mdio-busses/mdio_bus_io_r_1,2
```

The paths in this CSV must exist on the switch: PlatformManager creates the MDIO controller and reset devices (`mdioBusBlockConfigs`, `rtmCtrlBlockConfigs`) and their `/run/devmap/` links from `platform_manager.json` ([Ladakh example](https://github.com/meta/fboss/blob/main/fboss/configs/platforms/celestica/ladakh800bcls/platform_stack/platform_manager.json)).

### Generating the BSP mapping

Run `./fboss/lib/bsp/bspmapping/run-helper.sh` from the root of the FBOSS repository. It writes `PLATFORM.json` to `/tmp/generated_configs/`. Copy that JSON into `fboss/lib/bsp/PLATFORM/PLATFORMBspPlatformMapping.cpp`, and include it in the same PR as the CSV change. For a new platform, also add it to [`Main.cpp`](https://github.com/meta/fboss/blob/main/fboss/lib/bsp/bspmapping/Main.cpp) and [`Parser.h`](https://github.com/meta/fboss/blob/main/fboss/lib/bsp/bspmapping/Parser.h).
