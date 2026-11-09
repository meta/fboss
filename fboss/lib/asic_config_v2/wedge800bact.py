# pyre-unsafe
from itertools import chain
from typing import List, Optional

import neteng.fboss.asic_config_v2.thrift_types as asic_config_thrift
import neteng.fboss.platform_mapping_config.thrift_types as pm_types
from fboss.lib.asic_config_v2.tomahawk5 import Tomahawk5AsicConfig
from fboss.lib.platform_mapping_v2.asic_vendor_config import AsicVendorConfig
from fboss.lib.platform_mapping_v2.platform_mapping_v2 import PlatformMappingParser
from fboss.lib.platform_mapping_v2.read_files_utils import (
    discover_platform_mapping_inputs,
)


class Wedge800bActAsicConfig(Tomahawk5AsicConfig):
    def __init__(
        self,
        asic_config_params: asic_config_thrift.AsicConfigParameters,
        platform_mapping_input_dir: str,
        mgmt_port_speed: Optional[int] = None,
    ) -> None:
        super(Wedge800bActAsicConfig, self).__init__(asic_config_params)

        self.parser = PlatformMappingParser(
            discover_platform_mapping_inputs(platform_mapping_input_dir), "wedge800bact"
        )
        self.num_ports_per_core = 1
        if mgmt_port_speed == 10000:
            self.MGMT_PORT_SPEED: int = mgmt_port_speed
            self.MGMT_PORT_NUM_LANES: int = 1
            self.MGMT_PORT_FEC: str = "PC_FEC_NONE"

    def get_asic_vendor_config(self) -> AsicVendorConfig:
        asic_vendor_config = self.parser.get_asic_vendor_config()
        if not asic_vendor_config:
            raise Exception("No asic vendor config found.")
        return asic_vendor_config

    def get_static_mapping(self) -> pm_types.StaticMapping:
        return self.parser.get_static_mapping().get_static_mapping()

    def generate_port_config(self, mgmt_port: bool = False) -> None:
        self.values["PC_PORT"][("PORT_ID: {}".format(0),)] = (
            "ENABLE: 1",
            "SPEED: 10000",
            "NUM_LANES: 1",
        )
        speed_to_fec = {
            100000: "PC_FEC_RS544",
            200000: "PC_FEC_RS544_2XN",
            400000: "PC_FEC_RS544_2XN",
            800000: "PC_FEC_RS544_2XN",
        }
        speed = 800000
        port_ranges = []
        for lp_map in self.get_logical_port_to_physical_port_mapping():
            port_ranges.append("[{}, {}]".format(lp_map[0], lp_map[0]))
        pc_key = ("PORT_ID: [{}]".format(", ".join(port_ranges)),)
        pc_value = (
            "ENABLE: 0",
            "SPEED: {}".format(speed),
            "NUM_LANES: {}".format(8),
            "FEC_MODE: {}".format(speed_to_fec[speed]),
            "MAX_FRAME_SIZE: {}".format(self.MMU_SIZE),
        )
        self.values["PC_PORT"][pc_key] = pc_value
        if mgmt_port:
            mgmt_port_key = ("PORT_ID: {}".format(self.MGMT_PORT_LOGICAL_ID),)
            mgmt_port_value = (
                "ENABLE: 0",
                "SPEED: {}".format(self.MGMT_PORT_SPEED),
                "NUM_LANES: {}".format(self.MGMT_PORT_NUM_LANES),
                "FEC_MODE: {}".format(self.MGMT_PORT_FEC),
                "MAX_FRAME_SIZE: {}".format(self.MMU_SIZE),
            )
            self.values["PC_PORT"][mgmt_port_key] = mgmt_port_value
            if self.MGMT_PORT_SPEED == 10000:
                mgmt_port_medium_value = (
                    "MEDIUM_TYPE_AUTO: 0",
                    "MEDIUM_TYPE: PC_PHY_MEDIUM_COPPER",
                )
                self.values["PC_PMD_FIRMWARE"][mgmt_port_key] = mgmt_port_medium_value

        self.values["PORT"][pc_key] = (
            "MTU: {}".format(self.MMU_SIZE),
            "MTU_CHECK: 1",
        )

    def generate_sai_stats_disable_mask(self) -> None:
        self.values["global"]["sai_stats_disable_mask"] = "0x800"

    def generate_sai_eapol_trap_use_bcast_mac(self) -> None:
        # Trap broadcast EAPOL packets to the CPU
        self.values["global"]["sai_eapol_trap_use_bcast_mac"] = "1"

    def generate_asic_config(self) -> None:
        self.generate_global_settings()
        self.generate_logical_port_to_physical_port_mapping(mgmt_port=True)
        self.generate_lane_map()
        self.generate_polarity_map()
        self.generate_port_config(mgmt_port=True)
        self.generate_autoload_board_settings()
        self.generate_flex_counter_settings()
        self.generate_asic_vendor_config()
        self.generate_dlb_specific_config()
        self.generate_sai_stats_disable_mask()
        self.generate_sai_eapol_trap_use_bcast_mac()

    def get_logical_port_to_physical_port_mapping(self) -> List[List[int]]:
        NUM_LOGICAL_PORTS_PER_DATAPATH = 10
        NUM_PHYSICAL_PORTS_PER_DATAPATH = 16
        NUM_LP_PORTS_ON_EVEN_CORE = 4
        NUM_LANES_PER_CORE = 8
        logical_to_physical_port_mapping = []
        num_lanes_per_port = NUM_LANES_PER_CORE // self.num_ports_per_core
        for core_num in chain(range(0, 16), range(48, 64)):
            core_offset = 1 if core_num <= 1 else 0
            lp_start_offset = 0 if core_num % 2 == 0 else NUM_LP_PORTS_ON_EVEN_CORE
            lp_start = (
                (core_num // 2) * (NUM_LOGICAL_PORTS_PER_DATAPATH + 1)
                + lp_start_offset
                + core_offset
            )
            pp_start_offset = 0 if core_num % 2 == 0 else NUM_LANES_PER_CORE
            pp_start = (
                ((core_num // 2) * NUM_PHYSICAL_PORTS_PER_DATAPATH)
                + 1
                + pp_start_offset
            )

            for i in range(self.num_ports_per_core):
                lp_offset = i * (num_lanes_per_port if core_num % 2 == 0 else 1)
                pp_offset = i * num_lanes_per_port
                lp_id = lp_start + lp_offset
                pp_id = pp_start + pp_offset
                logical_to_physical_port_mapping.append([lp_id, pp_id])

        return logical_to_physical_port_mapping


def gen_wedge800bact_asic_config(
    asic_config_params: asic_config_thrift.AsicConfigParameters,
    platform_mapping_input_dir: str,
    mgmt_port_speed: Optional[int] = None,
) -> Wedge800bActAsicConfig:
    cfg = Wedge800bActAsicConfig(
        asic_config_params, platform_mapping_input_dir, mgmt_port_speed
    )
    cfg.generate_asic_config()
    return cfg
