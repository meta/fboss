# pyre-strict
import neteng.fboss.asic_config_v2.thrift_types as asic_config_thrift
import neteng.fboss.platform_mapping_config.thrift_types as pm_types
from fboss.lib.asic_config_v2.tomahawk6 import Tomahawk6AsicConfig
from fboss.lib.platform_mapping_v2.asic_vendor_config import AsicVendorConfig
from fboss.lib.platform_mapping_v2.platform_mapping_v2 import PlatformMappingParser
from fboss.lib.platform_mapping_v2.read_files_utils import (
    discover_platform_mapping_inputs,
)


class Icecube800bcAsicConfig(Tomahawk6AsicConfig):
    def __init__(
        self,
        asic_config_params: asic_config_thrift.AsicConfigParameters,
        platform_mapping_input_dir: str,
    ) -> None:
        super(Icecube800bcAsicConfig, self).__init__(asic_config_params)
        self.parser = PlatformMappingParser(
            discover_platform_mapping_inputs(platform_mapping_input_dir), "icecube800bc"
        )
        self.num_ports_per_core = 2

    def get_asic_vendor_config(self) -> AsicVendorConfig:
        asic_vendor_config = self.parser.get_asic_vendor_config()
        if not asic_vendor_config:
            raise Exception("No asic vendor config found.")
        return asic_vendor_config

    def get_static_mapping(self) -> pm_types.StaticMapping:
        #  `ttypes.StaticMapping`. Will be fixed when platform_mapping_v2
        #  migrates to thrift-python.
        return self.parser.get_static_mapping().get_static_mapping()

    def generate_port_config(self, mgmt_port: bool = False) -> None:
        # TODO: update these default settings if necessary, currently taken from montblanc
        # We also need to enable CPU port
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

        # add MTU check config so that such ports can drop packets larger
        # than MTU size as previous platforms.
        # The default Egress MTU on TH4 is 16K.
        self.values["PORT"][pc_key] = (
            "MTU: {}".format(self.MMU_SIZE),
            "MTU_CHECK: 1",
        )

    def generate_asic_config(self) -> None:
        # TODO: update if necessary, same as montblanc for now
        self.generate_global_settings()
        self.generate_logical_port_to_physical_port_mapping(mgmt_port=True)
        self.generate_lane_map()
        self.generate_polarity_map()
        self.generate_port_config(mgmt_port=True)
        self.generate_autoload_board_settings()
        self.generate_flex_counter_settings()
        self.generate_asic_vendor_config()


def gen_icecube800bc_asic_config(
    asic_config_params: asic_config_thrift.AsicConfigParameters,
    platform_mapping_input_dir: str,
) -> Icecube800bcAsicConfig:
    cfg = Icecube800bcAsicConfig(asic_config_params, platform_mapping_input_dir)
    cfg.generate_asic_config()
    return cfg
