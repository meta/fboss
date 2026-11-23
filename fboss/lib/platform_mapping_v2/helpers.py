# pyre-strict
import json
import re
from collections import defaultdict
from typing import Any, Dict, List, Optional

from fboss.lib.platform_mapping_v2.si_settings import SiSettings
from fboss.lib.platform_mapping_v2.static_mapping import StaticMapping
from neteng.fboss.phy.phy.thrift_types import (
    DataPlanePhyChip,
    DataPlanePhyChipType,
    FecMode,
    Pin,
    PinConfig,
    PinConnection,
    PinID,
    PortPinConfig,
    PortProfileConfig,
    ProfileSideConfig,
    RxSettings,
    TxSettings,
)
from neteng.fboss.platform_config.platform_config.thrift_types import (
    PlatformPortConfigFactor,
    PlatformPortConfigOverride,
    PlatformPortConfigOverrideFactor,
    PlatformPortProfileConfigEntry,
)
from neteng.fboss.platform_mapping_config.thrift_types import (
    Chip,
    ChipType,
    ConnectionEnd,
    ConnectionPair,
    CoreType,
    Port,
    SiSettingPinConnection,
    SpeedSetting,
)
from neteng.fboss.switch_config.thrift_types import PortProfileID, PortSpeed
from neteng.fboss.transceiver.thrift_types import TransmitterTechnology, Vendor


def profile_to_port_speed(profile: PortProfileID) -> List[PortSpeed]:
    if profile in [
        PortProfileID.PROFILE_53POINT125G_1_PAM4_RS545_COPPER,
        PortProfileID.PROFILE_53POINT125G_1_PAM4_RS545_OPTICAL,
    ]:
        return [PortSpeed.FIFTYTHREEPOINTONETWOFIVEG]
    if profile in [
        PortProfileID.PROFILE_106POINT25G_1_PAM4_RS544_OPTICAL,
        PortProfileID.PROFILE_106POINT25G_1_PAM4_RS544_COPPER,
    ]:
        return [PortSpeed.HUNDREDANDSIXPOINTTWOFIVEG]
    if profile in [
        PortProfileID.PROFILE_100G_4_NRZ_RS528_COPPER,
        PortProfileID.PROFILE_100G_4_NRZ_NOFEC_COPPER,
        PortProfileID.PROFILE_100G_4_NRZ_RS528_OPTICAL,
        PortProfileID.PROFILE_100G_2_PAM4_RS544_COPPER,
        PortProfileID.PROFILE_100G_1_PAM4_RS544_OPTICAL,
        PortProfileID.PROFILE_100G_1_PAM4_RS544_COPPER,
        PortProfileID.PROFILE_100G_1_PAM4_NOFEC_COPPER,
    ]:
        return [PortSpeed.HUNDREDG]
    if profile in [
        PortProfileID.PROFILE_200G_4_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_200G_4_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_200G_2_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_200G_2_PAM4_RS544_COPPER,
        PortProfileID.PROFILE_200G_1_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_200G_1_PAM4_RS544X2N_COPPER,
    ]:
        return [PortSpeed.TWOHUNDREDG]
    if profile in [
        PortProfileID.PROFILE_212POINT5G_1_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_212POINT5G_1_PAM4_RS544X2N_COPPER,
    ]:
        return [PortSpeed.TWOHUNDREDANDTWELVEPOINTFIVEG]
    if profile in [
        PortProfileID.PROFILE_400G_4_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_400G_4_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_400G_8_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_400G_2_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_400G_2_PAM4_RS544X2N_COPPER,
    ]:
        return [PortSpeed.FOURHUNDREDG]
    if profile in [
        PortProfileID.PROFILE_800G_8_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_800G_8_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_800G_4_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_800G_4_PAM4_RS544X2N_COPPER,
    ]:
        return [PortSpeed.EIGHTHUNDREDG]
    if profile in [
        PortProfileID.PROFILE_1600G_8_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_1600G_8_PAM4_RS544X2N_COPPER,
    ]:
        return [PortSpeed.ONEPOINTSIXT]
    if profile in [
        PortProfileID.PROFILE_10G_1_NRZ_NOFEC_COPPER,
        PortProfileID.PROFILE_10G_1_NRZ_NOFEC_OPTICAL,
    ]:
        return [PortSpeed.XG]
    if profile in [
        PortProfileID.PROFILE_50G_2_NRZ_RS528_OPTICAL,
        PortProfileID.PROFILE_50G_2_NRZ_NOFEC_OPTICAL,
        PortProfileID.PROFILE_50G_2_NRZ_NOFEC_COPPER,
        PortProfileID.PROFILE_50G_2_NRZ_RS528_COPPER,
        PortProfileID.PROFILE_50G_1_PAM4_RS544_COPPER,
    ]:
        return [PortSpeed.FIFTYG]
    if profile in [
        PortProfileID.PROFILE_25G_1_NRZ_NOFEC_OPTICAL,
        PortProfileID.PROFILE_25G_1_NRZ_RS528_COPPER,
        PortProfileID.PROFILE_25G_1_NRZ_RS528_OPTICAL,
        PortProfileID.PROFILE_25G_1_NRZ_NOFEC_COPPER,
    ]:
        return [PortSpeed.TWENTYFIVEG]
    if profile in [PortProfileID.PROFILE_DEFAULT]:
        return [PortSpeed.DEFAULT]
    raise Exception("Can't convert profile to speed for profileID ", profile)


def num_lanes_from_profile(profile: PortProfileID) -> int:
    if profile in [
        PortProfileID.PROFILE_10G_1_NRZ_NOFEC_COPPER,
        PortProfileID.PROFILE_53POINT125G_1_PAM4_RS545_COPPER,
        PortProfileID.PROFILE_53POINT125G_1_PAM4_RS545_OPTICAL,
        PortProfileID.PROFILE_106POINT25G_1_PAM4_RS544_OPTICAL,
        PortProfileID.PROFILE_106POINT25G_1_PAM4_RS544_COPPER,
        PortProfileID.PROFILE_100G_1_PAM4_RS544_OPTICAL,
        PortProfileID.PROFILE_100G_1_PAM4_RS544_COPPER,
        PortProfileID.PROFILE_100G_1_PAM4_NOFEC_COPPER,
        PortProfileID.PROFILE_50G_1_PAM4_RS544_COPPER,
        PortProfileID.PROFILE_10G_1_NRZ_NOFEC_OPTICAL,
        PortProfileID.PROFILE_25G_1_NRZ_NOFEC_OPTICAL,
        PortProfileID.PROFILE_25G_1_NRZ_RS528_COPPER,
        PortProfileID.PROFILE_25G_1_NRZ_RS528_OPTICAL,
        PortProfileID.PROFILE_25G_1_NRZ_NOFEC_COPPER,
        PortProfileID.PROFILE_200G_1_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_200G_1_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_212POINT5G_1_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_212POINT5G_1_PAM4_RS544X2N_COPPER,
    ]:
        return 1
    if profile in [
        PortProfileID.PROFILE_50G_2_NRZ_RS528_OPTICAL,
        PortProfileID.PROFILE_50G_2_NRZ_NOFEC_OPTICAL,
        PortProfileID.PROFILE_50G_2_NRZ_RS528_COPPER,
        PortProfileID.PROFILE_50G_2_NRZ_NOFEC_COPPER,
        PortProfileID.PROFILE_100G_2_PAM4_RS544_COPPER,
        PortProfileID.PROFILE_200G_2_PAM4_RS544_COPPER,
        PortProfileID.PROFILE_200G_2_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_400G_2_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_400G_2_PAM4_RS544X2N_COPPER,
    ]:
        return 2
    if profile in [
        PortProfileID.PROFILE_100G_4_NRZ_RS528_COPPER,
        PortProfileID.PROFILE_100G_4_NRZ_NOFEC_COPPER,
        PortProfileID.PROFILE_100G_4_NRZ_RS528_OPTICAL,
        PortProfileID.PROFILE_200G_4_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_200G_4_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_400G_4_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_400G_4_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_800G_4_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_800G_4_PAM4_RS544X2N_COPPER,
    ]:
        return 4
    if profile in [
        PortProfileID.PROFILE_400G_8_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_800G_8_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_800G_8_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_1600G_8_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_1600G_8_PAM4_RS544X2N_COPPER,
    ]:
        return 8
    if profile in [PortProfileID.PROFILE_DEFAULT]:
        return 0
    raise Exception("Can't find num lanes for profile ", profile)


def is_npu(chip_type: ChipType) -> bool:
    return chip_type == ChipType.NPU


def is_transceiver(chip_type: ChipType) -> bool:
    return chip_type == ChipType.TRANSCEIVER


def is_backplane(chip_type: ChipType) -> bool:
    return chip_type == ChipType.BACKPLANE


def is_xphy(chip_type: ChipType) -> bool:
    return chip_type == ChipType.XPHY


def is_optical_engine(chip_type: ChipType) -> bool:
    return chip_type == ChipType.OPTICAL_ENGINE


def is_laser_source(chip_type: ChipType) -> bool:
    return chip_type == ChipType.LASER_SOURCE


# Any terminal connection (front panel transceivers or backplane connectors)
def is_terminal_chip(chip_type: ChipType) -> bool:
    return is_transceiver(chip_type) or is_backplane(chip_type)


def get_npu_chip_name(chip: Chip) -> str:
    return f"{ChipType(chip.chip_type).name}-{CoreType(chip.core_type).name}-slot{chip.slot_id}/chip{chip.chip_id}/core{chip.core_id}"


def is_multi_core_transceiver(core_type: CoreType) -> bool:
    return core_type == CoreType.BANKED_CMIS_INTEGRATED


def get_terminal_chip_name(chip: Chip) -> str:
    base = f"{ChipType(chip.chip_type).name}-{CoreType(chip.core_type).name}-slot{chip.slot_id}/chip{chip.chip_id}"
    if is_multi_core_transceiver(chip.core_type):
        return f"{base}/core{chip.core_id}"
    return base


def get_npu_chip(chip: Chip) -> DataPlanePhyChip:
    if not is_npu(chip.chip_type):
        raise Exception(chip.chip_type, " is not a NPU")
    return DataPlanePhyChip(
        name=get_npu_chip_name(chip=chip),
        type=DataPlanePhyChipType.IPHY,
        physicalID=chip.core_id,
    )


def get_transceiver_chip(chip: Chip, physical_id: int) -> DataPlanePhyChip:
    if not is_transceiver(chip.chip_type):
        raise Exception(chip.chip_type, " is not a Transceiver")
    core_id = chip.core_id if is_multi_core_transceiver(chip.core_type) else None
    return DataPlanePhyChip(
        name=get_terminal_chip_name(chip=chip),
        type=DataPlanePhyChipType.TRANSCEIVER,
        physicalID=physical_id,
        coreId=core_id,
    )


def get_backplane_chip(chip: Chip) -> DataPlanePhyChip:
    if not is_backplane(chip.chip_type):
        raise Exception(chip.chip_type, " is not backplane")
    return DataPlanePhyChip(
        name=get_terminal_chip_name(chip=chip),
        type=DataPlanePhyChipType.BACKPLANE,
        physicalID=chip.chip_id - 1,  # We need 0 indexed physicalIDs
    )


def get_xphy_chip_name(chip: Chip) -> str:
    return f"{ChipType(chip.chip_type).name}-{CoreType(chip.core_type).name}-slot{chip.slot_id}/chip{chip.chip_id}/core{chip.core_id}"


def get_xphy_chip(chip: Chip) -> DataPlanePhyChip:
    if not is_xphy(chip.chip_type):
        raise Exception(chip.chip_type, " is not an XPHY")
    return DataPlanePhyChip(
        name=get_xphy_chip_name(chip=chip),
        type=DataPlanePhyChipType.XPHY,
        physicalID=chip.chip_id - 1,  # We need 0 indexed physicalIDs
    )


def get_optical_engine_chip_name(chip: Chip) -> str:
    return f"{ChipType(chip.chip_type).name}-{CoreType(chip.core_type).name}-slot{chip.slot_id}/chip{chip.chip_id}/core{chip.core_id}"


def get_optical_engine_chip(chip: Chip) -> DataPlanePhyChip:
    if not is_optical_engine(chip.chip_type):
        raise Exception(chip.chip_type, " is not an Optical Engine")
    return DataPlanePhyChip(
        name=get_optical_engine_chip_name(chip=chip),
        type=DataPlanePhyChipType.OPTICAL_ENGINE,
        physicalID=chip.chip_id - 1,
    )


def get_laser_source_chip_name(chip: Chip) -> str:
    return f"{ChipType(chip.chip_type).name}-{CoreType(chip.core_type).name}-slot{chip.slot_id}/chip{chip.chip_id}"


def get_laser_source_chip(chip: Chip) -> DataPlanePhyChip:
    if not is_laser_source(chip.chip_type):
        raise Exception(chip.chip_type, " is not a Laser Source")
    return DataPlanePhyChip(
        name=get_laser_source_chip_name(chip=chip),
        type=DataPlanePhyChipType.LASER_SOURCE,
        physicalID=chip.chip_id - 1,
    )


def _create_xphy_profile_side_config(
    speed_setting: SpeedSetting,
    use_a_chip_settings: bool,
) -> ProfileSideConfig:
    """Create a ProfileSideConfig for XPHY line or system side.

    Args:
        speed_setting: The speed setting containing lane/modulation/media info
        use_a_chip_settings: If True, use a_chip_settings; else use z_chip_settings

    Returns:
        Configured ProfileSideConfig object
    """
    chip_settings = (
        speed_setting.a_chip_settings
        if use_a_chip_settings
        else speed_setting.z_chip_settings
    )

    config = ProfileSideConfig(
        numLanes=speed_setting.num_lanes,
        modulation=speed_setting.modulation,
        fec=chip_settings.chip_fec,
        interfaceType=chip_settings.chip_interface_type,
        medium=speed_setting.media_type,
    )
    return config


# Creates a PlatformPortProfileConfigEntry for a given profileID, numlanes,
# modulation, fec mode and interface type
def get_platform_config_entry(
    profile: PortProfileID,
    npu_speed_setting: SpeedSetting,
    xphy_line_speed_setting: Optional[SpeedSetting] = None,
    xphy_system_speed_setting: Optional[SpeedSetting] = None,
) -> Optional[PlatformPortProfileConfigEntry]:
    factor = PlatformPortConfigFactor(profileID=profile)
    profile_config = PortProfileConfig()

    profile_config_speed = npu_speed_setting.speed

    # Configure NPU/IPHY side
    if is_npu(npu_speed_setting.a_chip_settings.chip_type) or is_npu(
        npu_speed_setting.z_chip_settings.chip_type
    ):
        if is_npu(npu_speed_setting.a_chip_settings.chip_type):
            npu_interface_type = npu_speed_setting.a_chip_settings.chip_interface_type
        else:
            npu_interface_type = npu_speed_setting.z_chip_settings.chip_interface_type

        npu_profile_side_config = ProfileSideConfig(
            numLanes=npu_speed_setting.num_lanes,
            modulation=npu_speed_setting.modulation,
            fec=npu_speed_setting.a_chip_settings.chip_fec,
            interfaceType=npu_interface_type,
            medium=npu_speed_setting.media_type,
        )

        xphy_line = None
        if xphy_line_speed_setting is not None:
            # XPHY Line side config (from XPHY→BACKPLANE row, XPHY is A-end)
            xphy_line = _create_xphy_profile_side_config(
                xphy_line_speed_setting, use_a_chip_settings=True
            )

        xphy_system = None
        if xphy_system_speed_setting is not None:
            # XPHY System side config (from NPU→XPHY row, XPHY is Z-end)
            xphy_system = _create_xphy_profile_side_config(
                xphy_system_speed_setting,
                use_a_chip_settings=False,
            )

        profile_config = PortProfileConfig(
            speed=profile_config_speed,
            iphy=npu_profile_side_config,
            xphyLine=xphy_line,
            xphySystem=xphy_system,
        )

        return PlatformPortProfileConfigEntry(factor=factor, profile=profile_config)
    return None


def _convert_xphy_core_type(
    from_core_type: CoreType, suffix_from: str, suffix_to: str
) -> Optional[CoreType]:
    """Convert between XPHY core types (e.g., LINE ↔ SYSTEM)."""
    from_core_type_name = CoreType(from_core_type).name
    if not from_core_type_name.endswith(suffix_from):
        return None

    target_core_type_name = from_core_type_name.replace(suffix_from, suffix_to)

    for ct in CoreType:
        if ct.name == target_core_type_name:
            return ct

    return None


def _find_matching_xphy_connection_in_pairs(
    reference_connection: ConnectionEnd,
    target_core_type: CoreType,
    connection_pairs: List[ConnectionPair],
) -> Optional[ConnectionEnd]:
    """
    For XPHY chips, SYSTEM lane = LINE lane + 8
    - When matching from SYSTEM to LINE: expected_line_lane = system_lane - 8
    - When matching from LINE to SYSTEM: expected_system_lane = line_lane + 8
    """
    reference_core_type_name = CoreType(reference_connection.chip.core_type).name
    target_core_type_name = CoreType(target_core_type).name

    reference_lane = reference_connection.lane.logical_id
    if reference_core_type_name.endswith("_SYSTEM") and target_core_type_name.endswith(
        "_LINE"
    ):
        expected_lane = reference_lane - 8
    elif reference_core_type_name.endswith("_LINE") and target_core_type_name.endswith(
        "_SYSTEM"
    ):
        expected_lane = reference_lane + 8
    else:
        # Same type or unknown, use same lane
        expected_lane = reference_lane

    for connection_pair in connection_pairs:
        for connection_end in [connection_pair.a, connection_pair.z]:
            if (
                connection_end
                and connection_end.chip
                and connection_end.lane
                and is_xphy(connection_end.chip.chip_type)
                and connection_end.chip.core_type == target_core_type
                and connection_end.chip.slot_id == reference_connection.chip.slot_id
                and connection_end.chip.chip_id == reference_connection.chip.chip_id
                and connection_end.chip.core_id == reference_connection.chip.core_id
                and connection_end.lane.logical_id == expected_lane
            ):
                return connection_end
    return None


def find_corresponding_xphy_system_lane(
    line_connection_end: ConnectionEnd, static_mapping: StaticMapping
) -> Optional[ConnectionEnd]:
    """Find corresponding SYSTEM lane for a retimer XPHY LINE connection."""
    if not is_xphy(line_connection_end.chip.chip_type):
        raise Exception("Expected line_connection_end to be an XPHY chip")

    # Convert LINE to SYSTEM core type
    system_core_type = _convert_xphy_core_type(
        line_connection_end.chip.core_type, "_LINE", "_SYSTEM"
    )

    if system_core_type is None:
        line_core_type_name = CoreType(line_connection_end.chip.core_type).name
        if not line_core_type_name.endswith("_LINE"):
            raise Exception(
                f"Expected line_connection_end to have a *_LINE core type, got {line_core_type_name}"
            )
        else:
            raise Exception(
                f"Could not find corresponding SYSTEM core type for {line_core_type_name}"
            )

    return _find_matching_xphy_connection_in_pairs(
        line_connection_end,
        system_core_type,
        static_mapping._az_connections,
    )


# Given a port name, find the connection end to start the topology traversal
def get_start_connection_end(
    port_name: str, static_mapping: StaticMapping
) -> ConnectionEnd:
    match = re.match(r"(rcy|eth|fab|evt|hyp)(\d+)/(\d+)/(\d+)", port_name)
    if not match:
        raise Exception(f"Port name {port_name} is not a valid format.")
    if match[1] in ["rcy", "evt", "hyp"]:
        # Recycle ports are named "rcy{slot_id}/{chip_id}/{core_id}"
        return static_mapping.find_connection_end(
            slot_id=int(match[2]),
            chip_id=int(match[3]),
            chip_types={ChipType.NPU},
            core_id=int(match[4]),
            logical_lane_id=0,
        )
    # Front panel ports are named "eth|fab{slot_id}/{virtual_transceiver_id}/{lane_id}"
    # Backplane ports are named "eth{slot_id}/{backplane_chip_id}/{backplane_lane_id}"
    virtual_id = int(match[3])
    # Banked (CPO) transceivers are modeled as a single core with global lane ids.
    # The port virtual id selects a bank on a module, so recover the global lane as
    # bank_lane_offset + lane_in_bank.
    port_virtual_map = static_mapping.get_port_virtual_transceiver_map()
    if virtual_id in port_virtual_map:
        chip_id, lane_offset = port_virtual_map[virtual_id]
        return static_mapping.find_connection_end(
            slot_id=int(match[2]),
            chip_id=chip_id,
            chip_types={ChipType.TRANSCEIVER, ChipType.BACKPLANE},
            core_id=0,
            logical_lane_id=lane_offset + int(match[4]) - 1,  # Lanes are 0 indexed
        )
    virtual_map = static_mapping.get_virtual_transceiver_map()
    if virtual_id in virtual_map:
        chip_id, core_id = virtual_map[virtual_id]
    else:
        chip_id = virtual_id
        core_id = 0
    return static_mapping.find_connection_end(
        slot_id=int(match[2]),
        chip_id=chip_id,
        chip_types={ChipType.TRANSCEIVER, ChipType.BACKPLANE},
        core_id=core_id,
        logical_lane_id=int(match[4]) - 1,  # Lanes are 0 indexed in CSV
    )


# Given a port and its speed, find all the connection end points required
def get_connection_pairs_for_profile(
    static_mapping: StaticMapping, port: Port, num_lanes: int, profile: PortProfileID
) -> List[ConnectionPair]:
    connections = []
    if num_lanes == 0:
        return connections
    start_connection_end = get_start_connection_end(
        port_name=port.port_name, static_mapping=static_mapping
    )
    for lane in range(num_lanes):
        # Start with given lane
        current_connection_end = static_mapping.find_connection_end(
            slot_id=start_connection_end.chip.slot_id,
            chip_id=start_connection_end.chip.chip_id,
            chip_types={start_connection_end.chip.chip_type},
            core_id=start_connection_end.chip.core_id,
            logical_lane_id=start_connection_end.lane.logical_id + lane,
        )
        # Iterate until we reach NPU
        while True:
            next_connection_end = static_mapping.get_other_connection_end(
                one_connection_end=current_connection_end
            )
            if next_connection_end:
                connections.append(
                    ConnectionPair(
                        a=next_connection_end,
                        z=current_connection_end,
                    )
                )
            else:
                connections.append(
                    ConnectionPair(
                        a=current_connection_end,
                    )
                )

            # Check if next_connection_end is a retimer XPHY *_LINE type
            # If so, replace with corresponding *_SYSTEM
            if (
                next_connection_end
                and next_connection_end.chip.chip_type == ChipType.XPHY
                and CoreType(next_connection_end.chip.core_type).name.endswith("_LINE")
            ):
                system_connection = find_corresponding_xphy_system_lane(
                    next_connection_end,
                    static_mapping,
                )
                if system_connection:
                    next_connection_end = system_connection
                else:
                    line_core_type_name = CoreType(
                        next_connection_end.chip.core_type
                    ).name
                    system_core_type_name = line_core_type_name.replace(
                        "_LINE", "_SYSTEM"
                    )
                    raise Exception(
                        f"Could not find corresponding {system_core_type_name} for {line_core_type_name}"
                    )

            current_connection_end = next_connection_end
            if (
                not current_connection_end
                or not current_connection_end.chip
                or not current_connection_end.chip.chip_type
                or is_npu(current_connection_end.chip.chip_type)
            ):
                # Iterate until we reach NPU
                break
    return connections


# Creates a pin config from ConnectionEnd type
def get_pin_config(connection_end: ConnectionEnd) -> PinConfig:
    if is_npu(connection_end.chip.chip_type):
        pin_name = get_npu_chip_name(connection_end.chip)
    elif is_terminal_chip(connection_end.chip.chip_type):
        pin_name = get_terminal_chip_name(connection_end.chip)
    elif is_xphy(connection_end.chip.chip_type):
        pin_name = get_xphy_chip_name(connection_end.chip)
    elif is_optical_engine(connection_end.chip.chip_type):
        pin_name = get_optical_engine_chip_name(connection_end.chip)
    elif is_laser_source(connection_end.chip.chip_type):
        pin_name = get_laser_source_chip_name(connection_end.chip)
    else:
        raise Exception("Don't understand chip type ", connection_end.chip.chip_type)

    return PinConfig(
        id=PinID(chip=pin_name, lane=connection_end.lane.logical_id),
    )


def _create_override_from_si_setting(
    si_setting_and_factor: Any,
    pin_conf: PinConfig,
    profile: PortProfileID,
    port_id: int,
    chip_type: ChipType,
) -> PlatformPortConfigOverride:
    """Create platform port config override from SI setting factor."""
    si_setting_vendor = si_setting_and_factor.factor.tcvr_override_setting.vendor
    # we only care about the vendor name and part number for overrides.
    vendor = Vendor(
        name=si_setting_vendor.name,
        oui=b"",
        partNumber=si_setting_vendor.partNumber,
        rev="",
        serialNumber="",
        dateCode="",
    )
    override_factor = PlatformPortConfigOverrideFactor(
        ports=[port_id],
        profiles=[profile],
        transceiverManagementInterface=None,
        vendor=vendor,
    )

    port_pin_config = PortPinConfig(
        iphy=[pin_conf] if is_npu(chip_type) else None,
        transceiver=None,
        xphySys=[pin_conf] if is_xphy(chip_type) else None,
        xphyLine=None,
    )

    driver_peaking = si_setting_and_factor.driver_peaking
    return PlatformPortConfigOverride(
        factor=override_factor, pins=port_pin_config, driverPeaking=driver_peaking
    )


def _format_custom_collection_json(custom_collection: Dict[str, List[int]]) -> str:
    return json.dumps(
        {
            "attributes": [
                {
                    key: {
                        "sai_metadata": {
                            "sai_attr_value_type": "SAI_ATTR_VALUE_TYPE_INT32_LIST",
                        },
                        "value": value,
                    }
                }
                for key, value in custom_collection.items()
            ]
        }
    )


def _aggregate_custom_collection_attributes(
    custom_tx_collection: List[Dict[str, int]],
    custom_rx_collection: List[Dict[str, int]],
) -> Optional[str]:
    tx_collection = defaultdict(list)
    for lane_collection in custom_tx_collection:
        for attribute, value in lane_collection.items():
            tx_collection[attribute].append(value)

    rx_collection = defaultdict(list)
    for lane_collection in custom_rx_collection:
        for attribute, value in lane_collection.items():
            rx_collection[attribute].append(value)

    custom_collection = tx_collection.copy()
    custom_collection.update(rx_collection)

    return (
        _format_custom_collection_json(custom_collection) if custom_collection else None
    )


def _process_connection_with_si_settings(
    connection: ConnectionEnd,
    si_settings: SiSettings,
    profile: PortProfileID,
    lane_speed: PortSpeed,
    port_id: Optional[int] = None,
) -> tuple[
    List[PinConfig],
    List[PlatformPortConfigOverride],
    List[Dict[str, int]],
    List[Dict[str, int]],
]:
    """Generic connection processing that applies SI settings and handles overrides."""
    pin_config = get_pin_config(connection_end=connection)
    pin_connection = SiSettingPinConnection(
        chip=connection.chip,
        logical_lane_id=connection.lane.logical_id,
    )
    si_setting_and_factor_list = si_settings.get_factor_and_setting(
        pin_connection=pin_connection,
        lane_speed=lane_speed,
        media_type=transmitter_tech_from_profile(profile),
    )

    if len(si_setting_and_factor_list) == 0:
        return [pin_config], [], [], []

    configured_pins = []
    overrides = []

    custom_tx_collection_list = []
    custom_rx_collection_list = []

    for si_setting_and_factor in si_setting_and_factor_list:
        tx = (
            si_setting_and_factor.tx_setting
            if si_setting_and_factor.tx_setting != TxSettings()
            else pin_config.tx
        )
        rx = (
            si_setting_and_factor.rx_setting
            if si_setting_and_factor.rx_setting != RxSettings()
            else pin_config.rx
        )
        pin_conf = PinConfig(id=pin_config.id, tx=tx, rx=rx)
        if si_setting_and_factor.custom_tx_collection:
            custom_tx_collection_list.append(si_setting_and_factor.custom_tx_collection)
        if si_setting_and_factor.custom_rx_collection:
            custom_rx_collection_list.append(si_setting_and_factor.custom_rx_collection)

        if si_setting_and_factor.factor is None:
            configured_pins.append(pin_conf)
        elif si_setting_and_factor.factor.tcvr_override_setting:
            if port_id is not None:
                override = _create_override_from_si_setting(
                    si_setting_and_factor,
                    pin_conf,
                    profile,
                    port_id,
                    connection.chip.chip_type,
                )
                overrides.append(override)

    # pyrefly: ignore [bad-return]
    return (
        configured_pins,
        overrides,
        custom_tx_collection_list,
        custom_rx_collection_list,
    )


def _process_xphy_connection(
    connection: ConnectionEnd,
    si_settings: SiSettings,
    profile: PortProfileID,
    lane_speed: PortSpeed,
) -> tuple[
    List[PinConfig],
    List[PinConfig],
    List[Dict[str, int]],
    List[Dict[str, int]],
]:
    """Process XPHY connection and return (sys_pins, line_pins)."""
    configured_pins, _, custom_tx_collection_list, custom_rx_collection_list = (
        _process_connection_with_si_settings(
            connection, si_settings, profile, lane_speed
        )
    )

    core_type_name = CoreType(connection.chip.core_type).name
    if core_type_name.endswith("_SYSTEM"):
        return configured_pins, [], custom_tx_collection_list, custom_rx_collection_list
    elif core_type_name.endswith("_LINE"):
        return [], configured_pins, custom_tx_collection_list, custom_rx_collection_list
    else:
        return [], [], [], []


def _process_npu_connection(
    connection: ConnectionEnd,
    si_settings: SiSettings,
    profile: PortProfileID,
    lane_speed: PortSpeed,
    port_id: int,
) -> tuple[
    List[PinConfig],
    List[PlatformPortConfigOverride],
    List[Dict[str, int]],
    List[Dict[str, int]],
]:
    """Process NPU connection and return (iphy_pins, overrides)."""
    return _process_connection_with_si_settings(
        connection, si_settings, profile, lane_speed, port_id
    )


# Helper function that creates a PortPinConfig from the types that store
# the parsed information from the CSVs
def get_pin_data_from_connections(
    connections: List[ConnectionPair],
    si_settings: SiSettings,
    profile: PortProfileID,
    lane_speed: PortSpeed,
    port_id: int,
    integrated_tcvr_mapping: Optional[Any] = None,
) -> tuple[PortPinConfig, List[PlatformPortConfigOverride]]:
    port_pin_config_iphy = []
    port_pin_config_tcvr = []
    port_pin_config_xphy_sys = []
    port_pin_config_xphy_line = []
    port_pin_config_optical_engine = []
    port_pin_config_laser_source = []
    port_pin_config_overrides = []

    port_custom_tx_collection_list = []
    port_custom_rx_collection_list = []

    for connection_pair in connections:
        for connection in [connection_pair.a, connection_pair.z]:
            if not connection:
                continue

            if is_terminal_chip(connection.chip.chip_type):
                port_pin_config_tcvr.append(get_pin_config(connection_end=connection))
                if integrated_tcvr_mapping is not None:
                    itc = integrated_tcvr_mapping.get_connection(
                        connection.chip.chip_id,
                        connection.chip.core_id,
                        connection.lane.logical_id,
                    )
                    if itc is not None:
                        port_pin_config_optical_engine.append(
                            get_pin_config(connection_end=itc.opticalEngine)
                        )
                        port_pin_config_laser_source.append(
                            get_pin_config(connection_end=itc.laserSource)
                        )
            elif is_xphy(connection.chip.chip_type):
                sys_pins, line_pins, _, _ = _process_xphy_connection(
                    connection, si_settings, profile, lane_speed
                )
                port_pin_config_xphy_sys.extend(sys_pins)
                port_pin_config_xphy_line.extend(line_pins)
            elif is_npu(connection.chip.chip_type):
                (
                    iphy_pins,
                    overrides,
                    custom_tx_collection_list,
                    custom_rx_collection_list,
                ) = _process_npu_connection(
                    connection, si_settings, profile, lane_speed, port_id
                )
                port_pin_config_iphy.extend(iphy_pins)
                port_pin_config_overrides.extend(overrides)
                port_custom_tx_collection_list.extend(custom_tx_collection_list)
                port_custom_rx_collection_list.extend(custom_rx_collection_list)

    custom_collection_str = _aggregate_custom_collection_attributes(
        port_custom_tx_collection_list, port_custom_rx_collection_list
    )

    port_pin_config_ret = PortPinConfig(
        iphy=port_pin_config_iphy,
        transceiver=port_pin_config_tcvr or None,
        xphySys=port_pin_config_xphy_sys or None,
        xphyLine=port_pin_config_xphy_line or None,
        serdesCustomCollection=custom_collection_str or None,
        opticalEngine=port_pin_config_optical_engine or None,
        laserSource=port_pin_config_laser_source or None,
    )

    return (
        port_pin_config_ret,
        port_pin_config_overrides,
    )


def _find_corresponding_xphy_line_and_terminal(
    xphy_sys_connection: ConnectionEnd, connection_pairs: List[ConnectionPair]
) -> tuple[Optional[ConnectionEnd], Optional[ConnectionEnd]]:
    """Find corresponding XPHY line and terminal connections for a XPHY system connection."""
    if not (xphy_sys_connection.chip and xphy_sys_connection.lane):
        return None, None

    # Convert SYSTEM to LINE core type using utility function
    line_core_type = _convert_xphy_core_type(
        xphy_sys_connection.chip.core_type, "_SYSTEM", "_LINE"
    )

    if line_core_type is None:
        return None, None

    # Find matching XPHY line connection using utility function
    xphy_line_connection = _find_matching_xphy_connection_in_pairs(
        xphy_sys_connection, line_core_type, connection_pairs
    )

    if not xphy_line_connection:
        return None, None

    # Find the terminal connection from the same connection pair
    terminal_connection = None
    for connection_pair in connection_pairs:
        if connection_pair.a == xphy_line_connection:
            if connection_pair.z and connection_pair.z.chip:
                if is_terminal_chip(connection_pair.z.chip.chip_type):
                    terminal_connection = connection_pair.z
            break

    return xphy_line_connection, terminal_connection


def _create_xphy_junction(
    xphy_sys_connection: ConnectionEnd,
    xphy_line_connection: ConnectionEnd,
    terminal_connection: ConnectionEnd,
) -> Pin:
    """Create XPHY junction pin structure."""
    from neteng.fboss.phy.phy.thrift_types import PinJunction

    xphy_sys_pin_name = get_xphy_chip_name(xphy_sys_connection.chip)
    xphy_line_pin_name = get_xphy_chip_name(xphy_line_connection.chip)
    terminal_pin_name = get_terminal_chip_name(terminal_connection.chip)

    line_connection = PinConnection(
        a=PinID(
            chip=xphy_line_pin_name,
            lane=xphy_line_connection.lane.logical_id,
        ),
        z=Pin(
            end=PinID(
                chip=terminal_pin_name,
                lane=terminal_connection.lane.logical_id,
            )
        ),
    )

    junction = PinJunction(
        system=PinID(
            chip=xphy_sys_pin_name,
            lane=xphy_sys_connection.lane.logical_id,
        ),
        line=[line_connection],
    )

    return Pin(junction=junction)


def _is_xphy_system_core_type(core_type: CoreType) -> bool:
    """Check if the core type is an XPHY *_SYSTEM type."""
    core_type_name = CoreType(core_type).name
    return core_type_name.endswith("_SYSTEM")


# Helper function that creates a list of PinConnections that go into platform mapping
# given the list of ConnectionPairs (how the CSV information is stored internally)
def get_mapping_pins(connection_pairs: List[ConnectionPair]) -> List[PinConnection]:
    pin_connections = []

    for connection in connection_pairs:
        if not (connection.a and is_npu(connection.a.chip.chip_type)):
            continue

        # Set NPU side (A side)
        npu_pin_name = get_npu_chip_name(connection.a.chip)
        a_pin = PinID(chip=npu_pin_name, lane=connection.a.lane.logical_id)

        z_pin = None
        if connection.z and connection.z.chip:
            z_chip = connection.z.chip
            if is_xphy(z_chip.chip_type) and _is_xphy_system_core_type(
                z_chip.core_type
            ):
                # This is an NPU -> XPHY(System) connection
                # Build complete junction: system -> line -> terminal
                xphy_sys_connection = connection.z

                if xphy_sys_connection:
                    xphy_line_connection, terminal_connection = (
                        _find_corresponding_xphy_line_and_terminal(
                            xphy_sys_connection, connection_pairs
                        )
                    )

                    if xphy_line_connection and terminal_connection:
                        z_pin = _create_xphy_junction(
                            xphy_sys_connection,
                            xphy_line_connection,
                            terminal_connection,
                        )
                    else:
                        sys_core_type_name = CoreType(
                            xphy_sys_connection.chip.core_type
                        ).name
                        line_core_type_name = sys_core_type_name.replace(
                            "_SYSTEM", "_LINE"
                        )
                        raise Exception(
                            f"Failed to build complete XPHY junction for {sys_core_type_name} connection. "
                            f"Expected to find corresponding {line_core_type_name} and terminal connections for "
                            f"slot_id={xphy_sys_connection.chip.slot_id}, "
                            f"chip_id={xphy_sys_connection.chip.chip_id}, "
                            f"core_id={xphy_sys_connection.chip.core_id}, "
                            f"lane_id={xphy_sys_connection.lane.logical_id}"
                        )

            elif connection.z:
                # Direct terminal connection (no XPHY)
                connection_z = connection.z
                if is_terminal_chip(connection_z.chip.chip_type):
                    terminal_pin_name = get_terminal_chip_name(connection_z.chip)
                    z_pin = Pin(
                        end=PinID(
                            chip=terminal_pin_name,
                            lane=connection_z.lane.logical_id,
                        )
                    )
                else:
                    raise Exception(
                        "Unsupported chip type ", connection_z.chip.chip_type
                    )

        pin_connections.append(PinConnection(a=a_pin, z=z_pin))

    return pin_connections


def transmitter_tech_from_profile(
    profile: PortProfileID,
) -> List[TransmitterTechnology]:
    if profile in [
        PortProfileID.PROFILE_53POINT125G_1_PAM4_RS545_OPTICAL,
        PortProfileID.PROFILE_200G_2_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_200G_4_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_400G_4_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_800G_8_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_106POINT25G_1_PAM4_RS544_OPTICAL,
        PortProfileID.PROFILE_100G_4_NRZ_RS528_OPTICAL,
        PortProfileID.PROFILE_100G_1_PAM4_RS544_OPTICAL,
        PortProfileID.PROFILE_50G_2_NRZ_RS528_OPTICAL,
        PortProfileID.PROFILE_50G_2_NRZ_NOFEC_OPTICAL,
        PortProfileID.PROFILE_10G_1_NRZ_NOFEC_OPTICAL,
        PortProfileID.PROFILE_25G_1_NRZ_NOFEC_OPTICAL,
        PortProfileID.PROFILE_25G_1_NRZ_RS528_OPTICAL,
        PortProfileID.PROFILE_800G_4_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_400G_2_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_200G_1_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_212POINT5G_1_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_1600G_8_PAM4_RS544X2N_OPTICAL,
    ]:
        return [TransmitterTechnology.OPTICAL, TransmitterTechnology.BACKPLANE]
    if profile in [
        PortProfileID.PROFILE_53POINT125G_1_PAM4_RS545_COPPER,
        PortProfileID.PROFILE_200G_4_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_106POINT25G_1_PAM4_RS544_COPPER,
        PortProfileID.PROFILE_10G_1_NRZ_NOFEC_COPPER,
        PortProfileID.PROFILE_25G_1_NRZ_RS528_COPPER,
        PortProfileID.PROFILE_25G_1_NRZ_NOFEC_COPPER,
        PortProfileID.PROFILE_50G_1_PAM4_RS544_COPPER,
        PortProfileID.PROFILE_50G_2_NRZ_RS528_COPPER,
        PortProfileID.PROFILE_50G_2_NRZ_NOFEC_COPPER,
        PortProfileID.PROFILE_100G_1_PAM4_RS544_COPPER,
        PortProfileID.PROFILE_100G_2_PAM4_RS544_COPPER,
        PortProfileID.PROFILE_100G_4_NRZ_RS528_COPPER,
        PortProfileID.PROFILE_100G_4_NRZ_NOFEC_COPPER,
        PortProfileID.PROFILE_200G_2_PAM4_RS544_COPPER,
        PortProfileID.PROFILE_400G_4_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_400G_8_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_100G_1_PAM4_NOFEC_COPPER,
        PortProfileID.PROFILE_800G_8_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_1600G_8_PAM4_RS544X2N_COPPER,
    ]:
        return [TransmitterTechnology.COPPER]
    if profile in [
        PortProfileID.PROFILE_200G_1_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_212POINT5G_1_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_400G_2_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_800G_4_PAM4_RS544X2N_COPPER,
    ]:
        return [TransmitterTechnology.COPPER, TransmitterTechnology.BACKPLANE]
    if profile in [PortProfileID.PROFILE_DEFAULT]:
        return [TransmitterTechnology.UNKNOWN]
    raise Exception("Can't figure out transmitter tech for profile ", profile)


def fec_from_profile(profile: PortProfileID) -> FecMode:
    # NOFEC profiles
    if profile in [
        PortProfileID.PROFILE_10G_1_NRZ_NOFEC,
        PortProfileID.PROFILE_10G_1_NRZ_NOFEC_COPPER,
        PortProfileID.PROFILE_10G_1_NRZ_NOFEC_OPTICAL,
        PortProfileID.PROFILE_20G_2_NRZ_NOFEC,
        PortProfileID.PROFILE_20G_2_NRZ_NOFEC_COPPER,
        PortProfileID.PROFILE_20G_2_NRZ_NOFEC_OPTICAL,
        PortProfileID.PROFILE_25G_1_NRZ_NOFEC,
        PortProfileID.PROFILE_25G_1_NRZ_NOFEC_COPPER,
        PortProfileID.PROFILE_25G_1_NRZ_NOFEC_OPTICAL,
        PortProfileID.PROFILE_25G_1_NRZ_NOFEC_COPPER_RACK_YV3_T1,
        PortProfileID.PROFILE_40G_4_NRZ_NOFEC,
        PortProfileID.PROFILE_40G_4_NRZ_NOFEC_COPPER,
        PortProfileID.PROFILE_40G_4_NRZ_NOFEC_OPTICAL,
        PortProfileID.PROFILE_50G_2_NRZ_NOFEC,
        PortProfileID.PROFILE_50G_2_NRZ_NOFEC_COPPER,
        PortProfileID.PROFILE_50G_2_NRZ_NOFEC_OPTICAL,
        PortProfileID.PROFILE_100G_4_NRZ_NOFEC,
        PortProfileID.PROFILE_100G_4_NRZ_NOFEC_COPPER,
        PortProfileID.PROFILE_100G_1_PAM4_NOFEC_COPPER,
    ]:
        return FecMode.NONE
    # CL74 profiles
    if profile in [
        PortProfileID.PROFILE_25G_1_NRZ_CL74_COPPER,
        PortProfileID.PROFILE_50G_2_NRZ_CL74_COPPER,
    ]:
        return FecMode.CL74
    # CL91 profiles
    if profile in [
        PortProfileID.PROFILE_100G_4_NRZ_CL91,
        PortProfileID.PROFILE_100G_4_NRZ_CL91_COPPER,
        PortProfileID.PROFILE_100G_4_NRZ_CL91_OPTICAL,
        PortProfileID.PROFILE_100G_4_NRZ_CL91_COPPER_RACK_YV3_T1,
    ]:
        return FecMode.CL91
    # RS528 profiles
    if profile in [
        PortProfileID.PROFILE_25G_1_NRZ_RS528_COPPER,
        PortProfileID.PROFILE_25G_1_NRZ_RS528_OPTICAL,
        PortProfileID.PROFILE_50G_2_NRZ_RS528_COPPER,
        PortProfileID.PROFILE_50G_2_NRZ_RS528_OPTICAL,
        PortProfileID.PROFILE_100G_4_NRZ_RS528,
        PortProfileID.PROFILE_100G_4_NRZ_RS528_COPPER,
        PortProfileID.PROFILE_100G_4_NRZ_RS528_OPTICAL,
    ]:
        return FecMode.RS528
    # RS544 profiles
    if profile in [
        PortProfileID.PROFILE_50G_1_PAM4_RS544_COPPER,
        PortProfileID.PROFILE_100G_1_PAM4_RS544_COPPER,
        PortProfileID.PROFILE_100G_1_PAM4_RS544_OPTICAL,
        PortProfileID.PROFILE_100G_2_PAM4_RS544_COPPER,
        PortProfileID.PROFILE_106POINT25G_1_PAM4_RS544_COPPER,
        PortProfileID.PROFILE_106POINT25G_1_PAM4_RS544_OPTICAL,
        PortProfileID.PROFILE_200G_2_PAM4_RS544_COPPER,
    ]:
        return FecMode.RS544
    # RS544_2N profiles
    if profile in [
        PortProfileID.PROFILE_100G_2_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_100G_2_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_100G_1_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_200G_2_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_200G_4_PAM4_RS544X2N,
        PortProfileID.PROFILE_200G_4_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_200G_4_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_200G_1_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_200G_1_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_212POINT5G_1_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_212POINT5G_1_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_400G_8_PAM4_RS544X2N,
        PortProfileID.PROFILE_400G_8_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_400G_8_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_400G_4_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_400G_4_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_400G_2_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_400G_2_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_800G_8_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_800G_8_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_800G_4_PAM4_RS544X2N_COPPER,
        PortProfileID.PROFILE_800G_4_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_1600G_8_PAM4_RS544X2N_OPTICAL,
        PortProfileID.PROFILE_1600G_8_PAM4_RS544X2N_COPPER,
    ]:
        return FecMode.RS544_2N
    # RS545 profiles
    if profile in [
        PortProfileID.PROFILE_53POINT125G_1_PAM4_RS545_COPPER,
        PortProfileID.PROFILE_53POINT125G_1_PAM4_RS545_OPTICAL,
    ]:
        return FecMode.RS545
    if profile in [PortProfileID.PROFILE_DEFAULT]:
        return FecMode.NONE
    raise Exception("Can't figure out FEC mode for profile ", profile)


def get_unique_connection_pairs(
    connection_pairs: List[ConnectionPair],
) -> List[ConnectionPair]:
    connection_pair_strs = set()
    unique_connection_pairs = []

    for connection_pair in connection_pairs:
        connection_pair_str = str(connection_pair)
        if connection_pair_str not in connection_pair_strs:
            connection_pair_strs.add(connection_pair_str)
            unique_connection_pairs.append(connection_pair)
    return unique_connection_pairs
