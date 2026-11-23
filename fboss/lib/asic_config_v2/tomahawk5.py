# pyre-unsafe
from typing import List, Optional, Tuple

import neteng.fboss.asic_config_v2.thrift_types as asic_config_thrift
import yaml
from fboss.lib.asic_config_v2.bcmxgs import (
    BCM_CFG_COMMON,
    BcmXgsAsicConfig,
    SAI_CFG_COMMON,
)

TABLE_NAMES = [
    "PC_PM_CORE",
    "PC_PORT_PHYS_MAP",
    "PC_PORT",
    "PORT_CONFIG",
    "global",
    "port",
    "TM_THD_CONFIG",
    "PORT",
    "DEVICE_CONFIG",
    "FP_CONFIG",
    "CTR_EFLEX_CONFIG",
    "PC_PMD_FIRMWARE",
]

TH5_SAI_CFG = {
    "rx_cosq_mapping_management_mode": "0",
    "sai_field_group_auto_prioritize": "1",
    "bcm_tunnel_term_compatible_mode": "1",
    "l3_iif_reservation_skip": "0",
    "sai_l2_cpu_fdb_event_suppress": "1",
    "sai_port_phy_time_sync_en": "1",
    "sai_stats_support_mask": "0x802",
    "sai_disable_internal_port_serdes": "1",
    "stat_custom_receive0_management_mode": "1",
    "sai_stats_disable_mask": "0x800",
}


class Tomahawk5AsicConfig(BcmXgsAsicConfig):
    MMU_SIZE = 9416

    MGMT_PORT_LOGICAL_ID = 76
    MGMT_PORT_PHYSICAL_ID = 513
    MGMT_PORT_SPEED = 100000
    MGMT_PORT_NUM_LANES = 4
    MGMT_PORT_FEC = "PC_FEC_RS528"
    MGMT_PORT_NUM_PORTS = 1
    # (logical, physical) port IDs of the mgmt PM lanes, used when the mgmt
    # port is broken out into MGMT_PORT_NUM_PORTS ports (e.g. 4x25G).
    MGMT_BREAKOUT_PORTS = [(76, 513), (164, 514), (186, 515), (274, 516)]

    def __init__(
        self, asic_config_params: asic_config_thrift.AsicConfigParameters
    ) -> None:
        super(Tomahawk5AsicConfig, self).__init__(asic_config_params)
        for table_name in TABLE_NAMES:
            self.values[table_name] = {}
        self.num_ports_per_core = 0

    def _read_preamble(self, file_name: str) -> str:
        with open(f"fboss/lib/asic_config_v2/{file_name}", "r") as f:
            return f.read()

    def generate_lane_map(self) -> None:
        static_mapping = self.get_static_mapping()
        lane_maps = static_mapping.phy_lane_map
        for core_id, lane_info in lane_maps.items():
            tx_lane_info, rx_lane_info = lane_info.tx_lane_info, lane_info.rx_lane_info
            pm_key = ("PC_PM_ID: {}".format(core_id + 1), "CORE_INDEX: 0")
            pm_value = (
                "RX_LANE_MAP: {}".format(self.get_lane_map_str(rx_lane_info)),
                "RX_LANE_MAP_AUTO: 0",
                "TX_LANE_MAP: {}".format(self.get_lane_map_str(tx_lane_info)),
                "TX_LANE_MAP_AUTO: 0",
            )
            pm_value = self.values["PC_PM_CORE"].get(pm_key, ()) + pm_value
            self.values["PC_PM_CORE"][pm_key] = pm_value

    def generate_polarity_map(self) -> None:
        static_mapping = self.get_static_mapping()
        pn_swap_maps = static_mapping.polarity_swap_map
        for core_id, swap_map in pn_swap_maps.items():
            tx_swap_map, rx_swap_map = swap_map.tx_lane_info, swap_map.rx_lane_info
            pm_key = ("PC_PM_ID: {}".format(core_id + 1), "CORE_INDEX: 0")
            pm_value = (
                "RX_POLARITY_FLIP: {}".format(self.get_polarity_map_str(rx_swap_map)),
                "RX_POLARITY_FLIP_AUTO: 0",
                "TX_POLARITY_FLIP: {}".format(self.get_polarity_map_str(tx_swap_map)),
                "TX_POLARITY_FLIP_AUTO: 0",
            )
            pm_value = self.values["PC_PM_CORE"].get(pm_key, ()) + pm_value
            self.values["PC_PM_CORE"][pm_key] = pm_value

    def generate_yaml_string(self, preamble: Optional[str] = None) -> str:
        if (
            self.asic_config_params.configType
            != asic_config_thrift.AsicConfigType.YAML_CONFIG
        ):
            raise Exception("This Bcm config does not use yaml config.")
        unit = 0
        tables = []
        for table_name in TABLE_NAMES:
            if (
                table_name.startswith("PC_")
                or table_name.startswith("PORT_CONFIG")
                or table_name.startswith("TM_THD_CONFIG")
                or table_name.startswith("PORT")
                or table_name.startswith("DEVICE_CONFIG")
                or table_name.startswith("FP_CONFIG")
                or table_name.startswith("CTR_EFLEX_CONFIG")
            ):
                device = "device"
            else:
                device = "bcm_device"

            if table_name in self.values and self.values[table_name]:
                tables.append({device: {unit: {table_name: self.values[table_name]}}})

        preamble_str = self._read_preamble(preamble) if preamble else ""

        return preamble_str + yaml.dump_all(
            tables, sort_keys=False, explicit_start=True, explicit_end=True
        ).replace(" !!python/tuple", "").replace("- ", "  ").replace("'", "")

    def generate_global_settings(self) -> None:
        self.values["PORT_CONFIG"]["PORT_SYSTEM_PROFILE_OPERMODE_PIPEUNIQUE"] = 1

        self.values["global"]["l3_alpm_template"] = 1

        self.values["global"]["l3_alpm_hit_mode"] = 1
        self.values["global"]["ipv6_lpm_128b_enable"] = 1

        self.values["global"]["pktio_driver_type"] = 1

        self.values["global"]["qos_map_multi_get_mode"] = 1

        self.values["global"]["rx_cosq_mapping_management_mode"] = 1

        self.values["global"]["l3_iif_reservation_skip"] = 1

        for attribute, value in BCM_CFG_COMMON.items():
            self.values["global"][attribute] = value
        for attribute, value in SAI_CFG_COMMON.items():
            if self.asic_config_params.mmuLossless and attribute in [
                "sai_mmu_qgroups_default",
                "sai_optimized_mmu",
            ]:
                continue
            self.values["global"][attribute] = value
        for attribute, value in TH5_SAI_CFG.items():
            self.values["global"][attribute] = value

        if not self.asic_config_params.mmuLossless:
            self.values["TM_THD_CONFIG"]["THRESHOLD_MODE"] = "LOSSY"
        else:
            self.values["TM_THD_CONFIG"]["THRESHOLD_MODE"] = "LOSSY_AND_LOSSLESS"
            self.values["TM_THD_CONFIG"]["SKIP_BUFFER_RESERVATION"] = 1
            self.values["global"]["sai_mmu_custom_config"] = 1

            self.values["global"]["sai_rdma_udf_disable"] = 1
            self.values["global"]["sai_l3_byte1_udf_disable"] = 1

            self.values["global"]["clm_enable"] = 1

        if self.asic_config_params.exactMatch:
            self.values["global"]["fpem_mem_entries_width"] = 1
            self.values["global"]["fpem_mem_entries"] = 65536

        self.values["FP_CONFIG"]["FP_ING_OPERMODE"] = "GLOBAL_PIPE_AWARE"

    def get_logical_port_to_physical_port_mapping(self) -> List[List[int]]:
        NUM_LOGICAL_PORTS_PER_DATAPATH = 10
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

    def get_cpu_logical_port_to_physical_port_mapping(self) -> List[int]:
        return [0, 0]

    def generate_logical_port_to_physical_port_mapping(
        self, mgmt_port: bool = False
    ) -> None:
        lp_mapping = self.get_logical_port_to_physical_port_mapping()
        lp_mapping.append(self.get_cpu_logical_port_to_physical_port_mapping())
        for lp_map in lp_mapping:
            pm_key = ("PORT_ID: {}".format(lp_map[0]),)
            pm_value = ("PC_PHYS_PORT_ID: {}".format(lp_map[1]),)
            self.values["PC_PORT_PHYS_MAP"][pm_key] = pm_value
        if mgmt_port:
            for logical_id, physical_id in self.get_mgmt_logical_to_physical_ports():
                pm_key = ("PORT_ID: {}".format(logical_id),)
                pm_value = ("PC_PHYS_PORT_ID: {}".format(physical_id),)
                self.values["PC_PORT_PHYS_MAP"][pm_key] = pm_value

    def get_mgmt_logical_to_physical_ports(self) -> List[Tuple[int, int]]:
        if self.MGMT_PORT_NUM_PORTS == 1:
            return [(self.MGMT_PORT_LOGICAL_ID, self.MGMT_PORT_PHYSICAL_ID)]
        return self.MGMT_BREAKOUT_PORTS[: self.MGMT_PORT_NUM_PORTS]

    def generate_autoload_board_settings(self) -> None:
        self.values["DEVICE_CONFIG"]["AUTOLOAD_BOARD_SETTINGS"] = 0

    def generate_flex_counter_settings(self) -> None:
        self.values["global"]["global_flexctr_ing_action_num_reserved"] = 20
        self.values["global"]["global_flexctr_ing_pool_num_reserved"] = 8
        self.values["global"]["global_flexctr_ing_op_profile_num_reserved"] = 20
        self.values["global"]["global_flexctr_ing_group_num_reserved"] = 2
        self.values["global"]["global_flexctr_egr_action_num_reserved"] = 8
        self.values["global"]["global_flexctr_egr_pool_num_reserved"] = 5
        self.values["global"]["global_flexctr_egr_op_profile_num_reserved"] = 10
        self.values["global"]["global_flexctr_egr_group_num_reserved"] = 1
        self.values["CTR_EFLEX_CONFIG"]["CTR_ING_EFLEX_OPERMODE_PIPEUNIQUE"] = 1
        self.values["CTR_EFLEX_CONFIG"][
            "CTR_ING_EFLEX_OPERMODE_PIPE_INSTANCE_UNIQUE"
        ] = 1
        self.values["CTR_EFLEX_CONFIG"]["CTR_EGR_EFLEX_OPERMODE_PIPEUNIQUE"] = 1
        self.values["CTR_EFLEX_CONFIG"][
            "CTR_EGR_EFLEX_OPERMODE_PIPE_INSTANCE_UNIQUE"
        ] = 1

    def generate_asic_vendor_config(self) -> None:
        asic_vendor_config = self.get_asic_vendor_config()
        for (
            key,
            value,
        ) in asic_vendor_config.get_asic_vendor_common_config().items():
            self.values[key] = value

        config = asic_vendor_config.get_asic_vendor_config(
            # pyre-ignore
            self.asic_config_params.configGenType
        )
        for key, value in config.items():
            self.values["global"][key] = value

    def generate_dlb_specific_config(self) -> None:
        self.values["global"]["ecmp_dlb_port_speeds"] = "1"

        self.values["global"]["l3_ecmp_member_secondary_mem_size"] = "4096"
