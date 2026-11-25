from typing import List, Optional

from fboss.lib.platform_mapping_v2.helpers import (
    fec_from_profile,
    is_npu,
    is_xphy,
    num_lanes_from_profile,
    profile_to_port_speed,
    transmitter_tech_from_profile,
)
from neteng.fboss.phy.phy.thrift_types import DataPlanePhyChipType, Side
from neteng.fboss.platform_mapping_config.thrift_types import SpeedSetting
from neteng.fboss.switch_config.thrift_types import PortProfileID


class ProfileSettings:
    def __init__(self, speed_settings: List[SpeedSetting]) -> None:
        self._speed_settings = speed_settings

    def _matches_chip_type(
        self,
        setting: SpeedSetting,
        phy_chip_type: DataPlanePhyChipType,
        side: Optional[Side] = None,
    ) -> bool:
        """Check if setting matches the requested chip type and side.

        For IPHY, we only check A-end because the NPU row is always structured as
        NPU→something (NPU is A-end). Previously we checked both A and Z ends, but
        that was redundant, there's no CSV row where NPU appears as Z-end.

        For XPHY, the side parameter determines which end to check:
        - LINE: XPHY is A-end (XPHY→BACKPLANE row) - used for xphyLine config
        - SYSTEM: XPHY is Z-end (NPU→XPHY row) - used for xphySystem config

        """
        if phy_chip_type == DataPlanePhyChipType.IPHY:
            return is_npu(setting.a_chip_settings.chip_type)
        if phy_chip_type == DataPlanePhyChipType.XPHY:
            if side == Side.LINE:
                # XPHY line side: XPHY is A-end (XPHY→BACKPLANE row)
                return is_xphy(setting.a_chip_settings.chip_type)
            elif side == Side.SYSTEM:
                # XPHY system side: XPHY is Z-end (NPU→XPHY row)
                return is_xphy(setting.z_chip_settings.chip_type)
            else:
                # Default behavior: XPHY as A-end
                return is_xphy(setting.a_chip_settings.chip_type)
        return False

    # Given a port profile and a chip type (iphy/xphy/transceiver/backplane),
    # return the row in Profile Settings matching that.
    def get_speed_setting(
        self,
        profile: PortProfileID,
        phy_chip_type: DataPlanePhyChipType,
        side: Optional[Side] = None,
    ) -> SpeedSetting:
        speeds = profile_to_port_speed(profile)
        media_type = transmitter_tech_from_profile(profile)
        num_lanes = num_lanes_from_profile(profile)
        expected_fec = fec_from_profile(profile)

        # First pass: Try to find an exact match including FEC
        for setting in self._speed_settings:
            if (
                setting.speed in speeds
                and setting.media_type in media_type
                and setting.num_lanes == num_lanes
                and setting.fec == expected_fec
                and self._matches_chip_type(setting, phy_chip_type, side)
            ):
                return setting

        # Second pass: Fall back to matching without FEC if no exact match found
        for setting in self._speed_settings:
            if (
                setting.speed in speeds
                and setting.media_type in media_type
                and setting.num_lanes == num_lanes
                and self._matches_chip_type(setting, phy_chip_type, side)
            ):
                return setting

        raise Exception("Can't find speed setting for profile ", profile)
