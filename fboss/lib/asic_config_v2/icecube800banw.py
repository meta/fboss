# pyre-strict
from typing import List

import neteng.fboss.asic_config_v2.thrift_types as asic_config_thrift
import neteng.fboss.platform_mapping_config.thrift_types as pm_types
from fboss.lib.asic_config_v2.tomahawk6 import Tomahawk6AsicConfig
from fboss.lib.platform_mapping_v2.asic_vendor_config import AsicVendorConfig
from fboss.lib.platform_mapping_v2.platform_mapping_v2 import PlatformMappingParser
from fboss.lib.platform_mapping_v2.read_files_utils import (
    discover_platform_mapping_inputs,
)

SAI_CFG = {
    "l3_ecmp_member_secondary_mem_size": "8192",
    "sai_port_phy_time_sync_en": "0",
    "sai_eapol_trap_use_bcast_mac": "1",
    "sai_stats_disable_mask": "0x800",
}


class Icecube800banwConfig(Tomahawk6AsicConfig):
    MGMT_PORT_LOGICAL_ID = 268
    MGMT_PORT_PHYSICAL_ID = 514

    def __init__(
        self,
        asic_config_params: asic_config_thrift.AsicConfigParameters,
        platform_mapping_input_dir: str,
    ) -> None:
        super(Icecube800banwConfig, self).__init__(asic_config_params)
        self.parser = PlatformMappingParser(
            discover_platform_mapping_inputs(platform_mapping_input_dir),
            "icecube800banw",
        )
        self.num_ports_per_core = 2

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
            100000: "PC_FEC_RS544_2XN",
            200000: "PC_FEC_RS544_2XN_IEEE",
            400000: "PC_FEC_RS544_2XN_IEEE",
            800000: "PC_FEC_RS544_2XN_IEEE",
        }
        speed = 800000
        port_ranges = []
        for lp_map in self.get_logical_port_to_physical_port_mapping():
            port_ranges.append("[{}, {}]".format(lp_map[0], lp_map[0]))
        pc_key = ("PORT_ID: [{}]".format(", ".join(port_ranges)),)
        pc_value = (
            "ENABLE: 0",
            "SPEED: {}".format(speed),
            "NUM_LANES: {}".format(4),
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

        self.values["PORT"][pc_key] = (
            "MTU: {}".format(self.MMU_SIZE),
            "MTU_CHECK: 1",
        )

    def generate_global_settings(self) -> None:
        super(Icecube800banwConfig, self).generate_global_settings()
        for attribute, value in SAI_CFG.items():
            self.values["global"][attribute] = value

    def generate_asic_config(self) -> None:
        self.generate_global_settings()
        self.generate_logical_port_to_physical_port_mapping(mgmt_port=True)
        self.generate_lane_map()
        self.generate_polarity_map()
        self.generate_port_config(mgmt_port=True)
        self.generate_autoload_board_settings()
        self.generate_flex_counter_settings()
        self.generate_asic_vendor_config()

    def get_logical_port_to_physical_port_mapping(self) -> List[List[int]]:
        NUM_LOGICAL_PORTS_PER_DATAPATH = 16
        NUM_PHYSICAL_PORTS_PER_DATAPATH = 16
        NUM_CORES = 64
        NUM_LP_PORTS_ON_EVEN_CORE = 8
        NUM_LANES_PER_CORE = 8
        logical_to_physical_port_mapping = []
        num_lanes_per_port = NUM_LANES_PER_CORE // self.num_ports_per_core

        for core_num in range(NUM_CORES):
            core_offset = 1 if core_num <= 1 else 0
            lp_start_offset = 0 if core_num % 2 == 0 else NUM_LP_PORTS_ON_EVEN_CORE
            lp_start = (
                (core_num // 2) * (NUM_LOGICAL_PORTS_PER_DATAPATH + 2)
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
                lp_offset = i * num_lanes_per_port
                pp_offset = i * num_lanes_per_port
                lp_id = lp_start + lp_offset
                pp_id = pp_start + pp_offset
                logical_to_physical_port_mapping.append([lp_id, pp_id])

        return logical_to_physical_port_mapping


def gen_icecube800banw_asic_config(
    asic_config_params: asic_config_thrift.AsicConfigParameters,
    platform_mapping_input_dir: str,
) -> Icecube800banwConfig:
    cfg = Icecube800banwConfig(asic_config_params, platform_mapping_input_dir)
    cfg.generate_asic_config()
    return cfg
