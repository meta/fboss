# pyre-unsafe
import copy
import json
import os
import sys
from dataclasses import dataclass
from enum import IntEnum
from typing import Any, Dict, List, Optional, Tuple

from fboss.lib.platform_mapping_v2.asic_vendor_config import AsicVendorConfig
from fboss.lib.platform_mapping_v2.integrated_transceiver_mapping import (
    IntegratedTransceiverMapping,
)
from fboss.lib.platform_mapping_v2.port_profile_mapping import PortProfileMapping
from fboss.lib.platform_mapping_v2.profile_settings import ProfileSettings
from fboss.lib.platform_mapping_v2.si_settings import SiSettings
from fboss.lib.platform_mapping_v2.static_mapping import StaticMapping
from neteng.fboss.asic_config_v2.thrift_types import AsicVendorConfigParams
from neteng.fboss.fboss_common.thrift_types import PlatformType
from neteng.fboss.phy.phy.thrift_types import (
    FecMode,
    InterfaceType,
    IpModulation,
    RxReach,
    RxSettings,
    TxSettings,
)
from neteng.fboss.platform_mapping_config.thrift_types import (
    Chip,
    ChipSetting,
    ChipType,
    ConnectionEnd,
    ConnectionPair,
    CoreType,
    IntegratedTransceiverConnection,
    Lane,
    Port,
    SiSettingFactor,
    SiSettingPinConnection,
    SiSettingRow,
    SpeedSetting,
    TransceiverOverrideSetting,
)
from neteng.fboss.switch_config.thrift_types import (
    AsicType,
    PortProfileID,
    PortSpeed,
    PortType,
    Scope,
)
from neteng.fboss.transceiver.thrift_types import TransmitterTechnology, Vendor


@dataclass(frozen=True)
class PlatformMappingInput:
    base_platform: str
    input_dir: str
    vendor: str
    data: Dict[str, str]


PlatformMappingInputs = Dict[str, PlatformMappingInput]


def read_vendor_data(input_file_path: str) -> Dict[str, str]:
    vendor_data = {}
    if not os.path.exists(input_file_path):
        raise FileNotFoundError(f"The folder '{input_file_path}' does not exist.")

    for filename in sorted(os.listdir(input_file_path)):
        filepath = os.path.join(input_file_path, filename)
        if (
            filepath.endswith(".csv") or filepath.endswith(".json")
        ) and not os.path.isdir(filepath):
            with open(filepath, "r") as file:
                content = file.read()
            vendor_data[filename] = content

    return vendor_data


def _add_platform_mapping_input(
    inputs: PlatformMappingInputs,
    name: str,
    platform_input: PlatformMappingInput,
) -> None:
    # Platform names are globally unique
    if name in inputs:
        raise ValueError(
            f"Duplicate platform mapping input '{name}' in "
            f"'{inputs[name].input_dir}' and "
            f"'{platform_input.input_dir}'"
        )
    inputs[name] = platform_input


def _find_variant_inputs(
    variants_dir: str,
    mapping_subdir: str,
) -> List[Tuple[str, str]]:
    if not os.path.isdir(variants_dir):
        return []

    variant_inputs = []
    for variant in sorted(os.listdir(variants_dir)):
        variant_input_dir = os.path.join(variants_dir, variant, mapping_subdir)
        if os.path.isdir(variant_input_dir):
            variant_inputs.append((variant, variant_input_dir))
    return variant_inputs


def discover_platform_mapping_inputs(
    platforms_dir: str,
    mapping_subdir: str = "platform_mapping",
) -> PlatformMappingInputs:
    inputs: PlatformMappingInputs = {}

    print(f"Reading platform mapping inputs in {platforms_dir}...", file=sys.stderr)

    for vendor in sorted(os.listdir(platforms_dir)):
        vendor_dir = os.path.join(platforms_dir, vendor)
        if not os.path.isdir(vendor_dir):
            continue
        for platform in sorted(os.listdir(vendor_dir)):
            platform_dir = os.path.join(vendor_dir, platform)
            if not os.path.isdir(platform_dir):
                continue

            input_dir = os.path.join(platform_dir, mapping_subdir)
            variants_dir = os.path.join(platform_dir, "variants")
            has_input = os.path.isdir(input_dir)
            variant_inputs = _find_variant_inputs(variants_dir, mapping_subdir)

            if not has_input and not variant_inputs:
                continue
            if variant_inputs and not has_input:
                raise ValueError(
                    f"Platform '{platform}' has platform mapping variants but no "
                    f"base input directory at '{input_dir}'"
                )

            _add_platform_mapping_input(
                inputs,
                platform,
                PlatformMappingInput(
                    base_platform=platform,
                    input_dir=input_dir,
                    vendor=vendor,
                    data=read_vendor_data(input_dir),
                ),
            )

            for variant, variant_input_dir in variant_inputs:
                _add_platform_mapping_input(
                    inputs,
                    variant,
                    PlatformMappingInput(
                        base_platform=platform,
                        input_dir=variant_input_dir,
                        vendor=vendor,
                        data=read_vendor_data(variant_input_dir),
                    ),
                )

    return inputs


def get_content(directory: Dict[str, str], filename: str) -> str:
    if filename not in directory:
        raise FileNotFoundError(
            f"File {filename} not found in directory with keys {directory.keys()}"
        )
    return directory[filename]


def column_int_enum_generator(string_list: str):
    return IntEnum(
        "Column", {item: idx for idx, item in enumerate(string_list.split())}
    )


def split_csv_list(value: str) -> List[str]:
    return [item for item in value.split("-") if item]


def parse_bool_map(value: str) -> Dict[str, bool]:
    bool_map = {}
    for item in value.split(";"):
        if not item:
            continue
        parts = [part.strip() for part in item.split("=", 1)]
        if len(parts) != 2:
            raise ValueError(
                f"Invalid bool map entry, expected key=value format: {item}"
            )
        key, raw_value = parts
        normalized_value = raw_value.lower()
        if normalized_value not in ("true", "false"):
            raise ValueError(f"Invalid bool map value {item}")
        bool_map[key] = normalized_value == "true"
    return bool_map


def read_static_mapping(directory: Dict[str, str], prefix: str) -> StaticMapping:
    STATIC_MAPPING_SUFFIX = "_static_mapping.csv"
    Column = column_int_enum_generator(
        "A_SLOT_ID A_CHIP_ID A_CHIP_TYPE A_CORE_ID A_CORE_TYPE A_CORE_LANE A_PHYSICAL_TX_LANE "
        + "A_PHYSICAL_RX_LANE A_TX_POLARITY_SWAP A_RX_POLARITY_SWAP Z_SLOT_ID "
        + "Z_CHIP_ID Z_CHIP_TYPE Z_CORE_ID Z_CORE_TYPE Z_CORE_LANE Z_PHYSICAL_TX_LANE "
        + "Z_PHYSICAL_RX_LANE Z_TX_POLARITY_SWAP Z_RX_POLARITY_SWAP",
    )
    connections = []
    for index, line in enumerate(
        get_content(directory, prefix + STATIC_MAPPING_SUFFIX).splitlines()
    ):
        if index < 1:
            # Skip the header
            continue
        row = line.split(",")
        # pyrefly: ignore [missing-attribute]
        if row[Column.A_PHYSICAL_TX_LANE]:
            # If there this a physical tx lane, there should be information
            # about its and the corresponding rx lane
            # pyrefly: ignore [missing-attribute]
            a_tx_physical_lane = int(row[Column.A_PHYSICAL_TX_LANE])
            # pyrefly: ignore [missing-attribute]
            a_rx_physical_lane = int(row[Column.A_PHYSICAL_RX_LANE])
            # pyrefly: ignore [missing-attribute]
            a_tx_polarity_swap = bool(row[Column.A_TX_POLARITY_SWAP] == "Y")
            # pyrefly: ignore [missing-attribute]
            a_rx_polarity_swap = bool(row[Column.A_RX_POLARITY_SWAP] == "Y")
        else:
            a_tx_physical_lane = None
            a_rx_physical_lane = None
            a_tx_polarity_swap = None
            a_rx_polarity_swap = None
        a_lane = Lane(
            # pyrefly: ignore [missing-attribute]
            logical_id=int(row[Column.A_CORE_LANE]),
            tx_physical_lane=a_tx_physical_lane,
            rx_physical_lane=a_rx_physical_lane,
            tx_polarity_swap=a_tx_polarity_swap,
            rx_polarity_swap=a_rx_polarity_swap,
        )
        a_chip = Chip(
            # pyrefly: ignore [missing-attribute]
            slot_id=int(row[Column.A_SLOT_ID]),
            # pyrefly: ignore [missing-attribute]
            chip_id=int(row[Column.A_CHIP_ID]),
            # pyrefly: ignore [missing-attribute]
            chip_type=ChipType[row[Column.A_CHIP_TYPE]],
            # pyrefly: ignore [missing-attribute]
            core_id=int(row[Column.A_CORE_ID]),
            # pyrefly: ignore [missing-attribute]
            core_type=CoreType[row[Column.A_CORE_TYPE]],
        )
        a_connection_end = ConnectionEnd(
            chip=a_chip,
            lane=a_lane,
        )
        # Z end is optional for ports like recycle ports
        # pyrefly: ignore [missing-attribute]
        if row[Column.Z_SLOT_ID]:
            z_chip = Chip(
                # pyrefly: ignore [missing-attribute]
                slot_id=int(row[Column.Z_SLOT_ID]),
                # pyrefly: ignore [missing-attribute]
                chip_id=int(row[Column.Z_CHIP_ID]),
                # pyrefly: ignore [missing-attribute]
                chip_type=ChipType[row[Column.Z_CHIP_TYPE]],
                # pyrefly: ignore [missing-attribute]
                core_id=int(row[Column.Z_CORE_ID]),
                # pyrefly: ignore [missing-attribute]
                core_type=CoreType[row[Column.Z_CORE_TYPE]],
            )
            z_lane = Lane(
                # pyrefly: ignore [missing-attribute]
                logical_id=int(row[Column.Z_CORE_LANE]),
                # pyrefly: ignore [missing-attribute]
                tx_physical_lane=int(row[Column.Z_PHYSICAL_TX_LANE]),
                # pyrefly: ignore [missing-attribute]
                rx_physical_lane=int(row[Column.Z_PHYSICAL_RX_LANE]),
                # pyrefly: ignore [missing-attribute]
                tx_polarity_swap=bool(row[Column.Z_TX_POLARITY_SWAP] == "Y"),
                # pyrefly: ignore [missing-attribute]
                rx_polarity_swap=bool(row[Column.Z_RX_POLARITY_SWAP] == "Y"),
            )
            z_connection_end = ConnectionEnd(
                chip=z_chip,
                lane=z_lane,
            )
        else:
            z_connection_end = None

        connections.append(ConnectionPair(a=a_connection_end, z=z_connection_end))

    return StaticMapping(az_connections=connections)


def read_port_profile_mapping(
    directory: Dict[str, str], prefix: str, multi_npu: bool
) -> PortProfileMapping:
    PORT_PROFILE_MAPPING_SUFFIX = "_port_profile_mapping.csv"
    Column = column_int_enum_generator(
        "GLOBAL_PORT_ID LOGICAL_PORT_ID PORT_NAME SUPPORTED_PROFILES ATTACHED_COREID ATTACHED_CORE_PORTID VIRTUAL_DEVICE_ID PORT_TYPE SCOPE PARENT_PORT CONTROLLING_PORT",
    )
    ports = {}
    for index, line in enumerate(
        get_content(directory, prefix + PORT_PROFILE_MAPPING_SUFFIX).splitlines()
    ):
        if index < 1:
            # Skip the header
            continue
        row = line.split(",")
        # pyrefly: ignore [missing-attribute]
        global_port_id = int(row[Column.GLOBAL_PORT_ID])
        # pyrefly: ignore [missing-attribute]
        logical_port_id = int(row[Column.LOGICAL_PORT_ID])

        # If global_port_id is not the same as logical_port_id then this is a multi_npu system.
        # Only include second NPU ports if multi_npu option is used
        if not multi_npu and global_port_id != logical_port_id:
            continue

        # pyrefly: ignore [missing-attribute]
        port_name = row[Column.PORT_NAME]
        # pyrefly: ignore [missing-attribute]
        if row[Column.ATTACHED_COREID]:
            # pyrefly: ignore [missing-attribute]
            attached_coreid = int(row[Column.ATTACHED_COREID])
        else:
            attached_coreid = None
        # pyrefly: ignore [missing-attribute]
        if row[Column.ATTACHED_CORE_PORTID]:
            # pyrefly: ignore [missing-attribute]
            attached_core_portid = int(row[Column.ATTACHED_CORE_PORTID])
        else:
            attached_core_portid = None
        # pyrefly: ignore [missing-attribute]
        if row[Column.VIRTUAL_DEVICE_ID]:
            # pyrefly: ignore [missing-attribute]
            virtual_device_id = int(row[Column.VIRTUAL_DEVICE_ID])
        else:
            virtual_device_id = None
        supported_profiles = []
        # pyrefly: ignore [missing-attribute]
        for profile in row[Column.SUPPORTED_PROFILES].split("-"):
            supported_profiles.append(PortProfileID(int(profile)))
        # pyrefly: ignore [missing-attribute]
        port_type = PortType(int(row[Column.PORT_TYPE]))
        # pyrefly: ignore [missing-attribute]
        scope = Scope(int(row[Column.SCOPE]))
        # pyrefly: ignore [missing-attribute]
        if Column.PARENT_PORT < len(row) and row[Column.PARENT_PORT]:
            # pyrefly: ignore [missing-attribute]
            parent_port_id = int(row[Column.PARENT_PORT])
        else:
            parent_port_id = None
        # pyrefly: ignore [missing-attribute]
        if Column.CONTROLLING_PORT < len(row) and row[Column.CONTROLLING_PORT]:
            # pyrefly: ignore [missing-attribute]
            controlling_port = int(row[Column.CONTROLLING_PORT])
        else:
            controlling_port = None
        assert global_port_id not in ports
        ports[global_port_id] = Port(
            global_port_id=global_port_id,
            logical_port_id=logical_port_id,
            port_name=port_name,
            supported_profiles=supported_profiles,
            attached_coreid=attached_coreid,
            attached_core_portid=attached_core_portid,
            virtual_device_id=virtual_device_id,
            port_type=port_type,
            scope=scope,
            parent_port_id=parent_port_id,
            controlling_port=controlling_port,
        )
    return PortProfileMapping(ports=ports)


def read_platform_descriptor(directory: Dict[str, str], prefix: str) -> Dict[str, Any]:
    PLATFORM_DESCRIPTOR_SUFFIX = "_platform_descriptor.csv"
    VARIANT_ATTRIBUTES_COLUMN = 5
    Column = column_int_enum_generator(
        "SYSTEM_VENDOR PLATFORM_TYPE PRODUCT_NAME_PREFIXES MODE_NAMES ASIC_TYPE VARIANT_ATTRIBUTES"
    )
    for index, line in enumerate(
        get_content(directory, prefix + PLATFORM_DESCRIPTOR_SUFFIX).splitlines()
    ):
        if index < 1:
            continue
        row = line.split(",")
        # pyrefly: ignore [missing-attribute]
        system_vendor = row[Column.SYSTEM_VENDOR]
        # pyrefly: ignore [missing-attribute]
        platform_type = PlatformType[row[Column.PLATFORM_TYPE]]
        # pyrefly: ignore [missing-attribute]
        product_name_prefixes = split_csv_list(row[Column.PRODUCT_NAME_PREFIXES])
        # pyrefly: ignore [missing-attribute]
        mode_names = split_csv_list(row[Column.MODE_NAMES])
        # pyrefly: ignore [missing-attribute]
        asic_type = AsicType[row[Column.ASIC_TYPE]]
        descriptor = {
            "systemVendor": system_vendor,
            "platformType": int(platform_type),
            "productNamePrefixes": product_name_prefixes,
            "modeNames": mode_names,
            "asicType": int(asic_type),
        }
        if VARIANT_ATTRIBUTES_COLUMN < len(row) and row[VARIANT_ATTRIBUTES_COLUMN]:
            descriptor["variantAttributes"] = parse_bool_map(
                row[VARIANT_ATTRIBUTES_COLUMN]
            )
        return descriptor
    raise ValueError(f"No platform descriptor row found for {prefix}")


def read_profile_settings(directory: Dict[str, str], prefix: str) -> ProfileSettings:
    PROFILE_SETTINGS_SUFFIX = "_profile_settings.csv"
    Column = column_int_enum_generator(
        "PORT_SPEED_MBPS A_CHIP_TYPE Z_CHIP_TYPE NUM_LANES MODULATION A_FEC Z_FEC MEDIA_TYPE A_INTERFACE_TYPE Z_INTERFACE_TYPE"
    )
    profiles = []
    for index, line in enumerate(
        get_content(directory, prefix + PROFILE_SETTINGS_SUFFIX).splitlines()
    ):
        if index < 1:
            # Skip the header
            continue
        row = line.split(",")
        # pyrefly: ignore [missing-attribute]
        speed = int(row[Column.PORT_SPEED_MBPS])
        try:
            PortSpeed(speed)
        except ValueError:
            raise Exception("Invalid speed ", speed)
        # pyrefly: ignore [missing-attribute]
        a_chip_type = ChipType[row[Column.A_CHIP_TYPE]]
        # pyrefly: ignore [missing-attribute]
        if row[Column.Z_CHIP_TYPE]:
            # pyrefly: ignore [missing-attribute]
            z_chip_type = ChipType[row[Column.Z_CHIP_TYPE]]
        else:
            z_chip_type = None
        # pyrefly: ignore [missing-attribute]
        num_lanes = int(row[Column.NUM_LANES])
        # pyrefly: ignore [missing-attribute]
        modulation = IpModulation[row[Column.MODULATION]]
        # pyrefly: ignore [missing-attribute]
        a_fec = FecMode[row[Column.A_FEC]]
        #  if empty, Z_FEC default to None
        # pyrefly: ignore [missing-attribute]
        z_fec = FecMode[row[Column.Z_FEC]] if row[Column.Z_FEC] else None
        # pyrefly: ignore [missing-attribute]
        media_type = TransmitterTechnology[row[Column.MEDIA_TYPE]]
        a_interface_type = (
            # pyrefly: ignore [missing-attribute]
            InterfaceType[row[Column.A_INTERFACE_TYPE]]
            # pyrefly: ignore [missing-attribute]
            if row[Column.A_INTERFACE_TYPE]
            else InterfaceType.NONE
        )
        z_interface_type = (
            # pyrefly: ignore [missing-attribute]
            InterfaceType[row[Column.Z_INTERFACE_TYPE]]
            # pyrefly: ignore [missing-attribute]
            if row[Column.Z_INTERFACE_TYPE]
            else InterfaceType.NONE
        )
        profiles.append(
            SpeedSetting(
                speed=PortSpeed(speed),
                a_chip_settings=ChipSetting(
                    chip_type=a_chip_type,
                    chip_interface_type=a_interface_type,
                    chip_fec=a_fec,
                ),
                z_chip_settings=ChipSetting(
                    chip_type=z_chip_type,
                    chip_interface_type=z_interface_type,
                    chip_fec=z_fec,
                ),
                num_lanes=num_lanes,
                modulation=modulation,
                fec=a_fec,  # Keep backward compatibility - fec field uses A_FEC
                media_type=media_type,
            )
        )
    return ProfileSettings(speed_settings=profiles)


def read_si_settings(  # noqa: C901
    directory: Dict[str, str], prefix: str, version: Optional[str] = None
) -> SiSettings:
    si_suffix = f"_{version}" if version is not None else ""
    SI_SETTINGS_SUFFIX = f"_si_settings{si_suffix}.csv"
    CUSTOM_TX_PREFIX = "CUSTOM_TX_"
    CUSTOM_RX_PREFIX = "CUSTOM_RX_"

    si_settings = []
    Column = None
    column_names = ""
    for index, line in enumerate(
        get_content(directory, prefix + SI_SETTINGS_SUFFIX).splitlines()
    ):
        row = line.split(",")
        if index < 1:
            column_names = " ".join(f"{name}" for name in row)
            column_names = column_names.replace("(mbps)", "")
            column_names = column_names.replace("(m)", "")
            Column = column_int_enum_generator(column_names)
            continue
        chip = Chip(
            # pyrefly: ignore [missing-attribute]
            slot_id=int(row[Column.SLOT_ID]),
            # pyrefly: ignore [missing-attribute]
            chip_id=int(row[Column.CHIP_ID]),
            # pyrefly: ignore [missing-attribute]
            chip_type=ChipType[row[Column.CHIP_TYPE]],
            # pyrefly: ignore [missing-attribute]
            core_type=CoreType[row[Column.CORE_TYPE]],
            # pyrefly: ignore [missing-attribute]
            core_id=int(row[Column.CORE_ID]),
        )
        pin_connection = SiSettingPinConnection(
            chip=chip,
            # pyrefly: ignore [missing-attribute]
            logical_lane_id=int(row[Column.CORE_LANE]),
        )

        lane_speed = None
        # pyrefly: ignore [missing-attribute]
        if row[Column.LANE_SPEED]:
            try:
                # pyrefly: ignore [missing-attribute]
                PortSpeed(int(row[Column.LANE_SPEED]))
            except ValueError:
                # pyrefly: ignore [missing-attribute]
                raise Exception("Invalid speed ", row[Column.LANE_SPEED])
            # pyrefly: ignore [missing-attribute]
            lane_speed = PortSpeed(int(row[Column.LANE_SPEED]))

        media_type = None
        # pyrefly: ignore [missing-attribute]
        if row[Column.MEDIA_TYPE]:
            try:
                # pyrefly: ignore [missing-attribute]
                media_type = TransmitterTechnology[row[Column.MEDIA_TYPE]]
            except KeyError:
                # pyrefly: ignore [missing-attribute]
                raise Exception("Invalid media type ", row[Column.MEDIA_TYPE])

        cable_length = None
        # pyrefly: ignore [missing-attribute]
        if row[Column.CABLE_LENGTH]:
            # pyrefly: ignore [missing-attribute]
            cable_length = float(row[Column.CABLE_LENGTH])

        # Add optics settings if present in the csv file.
        driver_peaking = None
        tcvr_setting = None
        # pyrefly: ignore [missing-attribute]
        tcvr_vendor = row[Column.TCVR_VENDOR]
        if tcvr_vendor:
            # if the vendor is specified, we need to check the part number and
            # the driver peaking values that they are specified
            # pyrefly: ignore [missing-attribute]
            tcvr_part_num = row[Column.TCVR_PART_NUM]
            if not tcvr_part_num:
                raise Exception(
                    "Invalid transceiver part number type not populated ",
                    # pyrefly: ignore [missing-attribute]
                    row[Column.TCVR_PART_NUM],
                )
            vendor = Vendor(
                # pyrefly: ignore [missing-attribute]
                name=str(row[Column.TCVR_VENDOR]),
                partNumber=str(tcvr_part_num),
            )
            # pyrefly: ignore [missing-attribute]
            driver_peaking = int(row[Column.DRIVER_PEAKING])
            if driver_peaking is None:
                raise Exception(
                    "Invalid driver peaking value not populated ",
                    # pyrefly: ignore [missing-attribute]
                    row[Column.DRIVER_PEAKING],
                )
            tcvr_setting = TransceiverOverrideSetting(vendor=vendor)
        si_setting_factor = SiSettingFactor(
            lane_speed=lane_speed,
            media_type=media_type,
            cable_length=cable_length,
            tcvr_override_setting=tcvr_setting,
        )

        tx_kwargs: Dict[str, Any] = {}
        if chip.core_type in (CoreType.G200, CoreType.P200):
            # pyrefly: ignore [missing-attribute]
            if "TX_PRE3" in column_names and row[Column.TX_PRE3]:
                # pyrefly: ignore [missing-attribute]
                tx_kwargs["firPre3"] = int(row[Column.TX_PRE3])
            # pyrefly: ignore [missing-attribute]
            if "TX_PRE2" in column_names and row[Column.TX_PRE2]:
                # pyrefly: ignore [missing-attribute]
                tx_kwargs["firPre2"] = int(row[Column.TX_PRE2])
            # pyrefly: ignore [missing-attribute]
            if "TX_PRE1" in column_names and row[Column.TX_PRE1]:
                # pyrefly: ignore [missing-attribute]
                tx_kwargs["firPre1"] = int(row[Column.TX_PRE1])
            # pyrefly: ignore [missing-attribute]
            if "TX_MAIN" in column_names and row[Column.TX_MAIN]:
                # pyrefly: ignore [missing-attribute]
                tx_kwargs["firMain"] = int(row[Column.TX_MAIN])
            # pyrefly: ignore [missing-attribute]
            if "TX_POST1" in column_names and row[Column.TX_POST1]:
                # pyrefly: ignore [missing-attribute]
                tx_kwargs["firPost1"] = int(row[Column.TX_POST1])
            # pyrefly: ignore [missing-attribute]
            if "TX_POST2" in column_names and row[Column.TX_POST2]:
                # pyrefly: ignore [missing-attribute]
                tx_kwargs["firPost2"] = int(row[Column.TX_POST2])
            # pyrefly: ignore [missing-attribute]
            if "TX_POST3" in column_names and row[Column.TX_POST3]:
                # pyrefly: ignore [missing-attribute]
                tx_kwargs["firPost3"] = int(row[Column.TX_POST3])
        else:
            # pyrefly: ignore [missing-attribute]
            if "TX_PRE3" in column_names and row[Column.TX_PRE3]:
                # pyrefly: ignore [missing-attribute]
                tx_kwargs["pre3"] = int(row[Column.TX_PRE3])
            # pyrefly: ignore [missing-attribute]
            if "TX_PRE2" in column_names and row[Column.TX_PRE2]:
                # pyrefly: ignore [missing-attribute]
                tx_kwargs["pre2"] = int(row[Column.TX_PRE2])
            # pyrefly: ignore [missing-attribute]
            if "TX_PRE1" in column_names and row[Column.TX_PRE1]:
                # pyrefly: ignore [missing-attribute]
                tx_kwargs["pre"] = int(row[Column.TX_PRE1])
            # pyrefly: ignore [missing-attribute]
            if "TX_MAIN" in column_names and row[Column.TX_MAIN]:
                # pyrefly: ignore [missing-attribute]
                tx_kwargs["main"] = int(row[Column.TX_MAIN])
            # pyrefly: ignore [missing-attribute]
            if "TX_POST1" in column_names and row[Column.TX_POST1]:
                # pyrefly: ignore [missing-attribute]
                tx_kwargs["post"] = int(row[Column.TX_POST1])
            # pyrefly: ignore [missing-attribute]
            if "TX_POST2" in column_names and row[Column.TX_POST2]:
                # pyrefly: ignore [missing-attribute]
                tx_kwargs["post2"] = int(row[Column.TX_POST2])
            # pyrefly: ignore [missing-attribute]
            if "TX_POST3" in column_names and row[Column.TX_POST3]:
                # pyrefly: ignore [missing-attribute]
                tx_kwargs["post3"] = int(row[Column.TX_POST3])

        # pyrefly: ignore [missing-attribute]
        if "TX_PRECODING" in column_names and row[Column.TX_PRECODING]:
            # pyrefly: ignore [missing-attribute]
            tx_kwargs["precoding"] = int(row[Column.TX_PRECODING])
        # pyrefly: ignore [missing-attribute]
        if "TX_DIFF_ENCODER_EN" in column_names and row[Column.TX_DIFF_ENCODER_EN]:
            # pyrefly: ignore [missing-attribute]
            tx_kwargs["diffEncoderEn"] = int(row[Column.TX_DIFF_ENCODER_EN])
        # pyrefly: ignore [missing-attribute]
        if "TX_DIG_GAIN" in column_names and row[Column.TX_DIG_GAIN]:
            # pyrefly: ignore [missing-attribute]
            tx_kwargs["digGain"] = int(row[Column.TX_DIG_GAIN])
        # pyrefly: ignore [missing-attribute]
        if "TX_DRIVER_SWING" in column_names and row[Column.TX_DRIVER_SWING]:
            # pyrefly: ignore [missing-attribute]
            tx_kwargs["driverSwing"] = int(row[Column.TX_DRIVER_SWING])
        # pyrefly: ignore [missing-attribute]
        if "TX_DRIVE_CURRENT" in column_names and row[Column.TX_DRIVE_CURRENT]:
            # pyrefly: ignore [missing-attribute]
            tx_kwargs["driveCurrent"] = int(row[Column.TX_DRIVE_CURRENT])
        # pyrefly: ignore [missing-attribute]
        if "TX_FFE_COEFF_0" in column_names and row[Column.TX_FFE_COEFF_0]:
            # pyrefly: ignore [missing-attribute]
            tx_kwargs["ffeCoeff0"] = int(row[Column.TX_FFE_COEFF_0])
        # pyrefly: ignore [missing-attribute]
        if "TX_FFE_COEFF_1" in column_names and row[Column.TX_FFE_COEFF_1]:
            # pyrefly: ignore [missing-attribute]
            tx_kwargs["ffeCoeff1"] = int(row[Column.TX_FFE_COEFF_1])
        # pyrefly: ignore [missing-attribute]
        if "TX_FFE_COEFF_2" in column_names and row[Column.TX_FFE_COEFF_2]:
            # pyrefly: ignore [missing-attribute]
            tx_kwargs["ffeCoeff2"] = int(row[Column.TX_FFE_COEFF_2])
        # pyrefly: ignore [missing-attribute]
        if "TX_FFE_COEFF_3" in column_names and row[Column.TX_FFE_COEFF_3]:
            # pyrefly: ignore [missing-attribute]
            tx_kwargs["ffeCoeff3"] = int(row[Column.TX_FFE_COEFF_3])
        # pyrefly: ignore [missing-attribute]
        if "TX_FFE_COEFF_4" in column_names and row[Column.TX_FFE_COEFF_4]:
            # pyrefly: ignore [missing-attribute]
            tx_kwargs["ffeCoeff4"] = int(row[Column.TX_FFE_COEFF_4])
        # pyrefly: ignore [missing-attribute]
        if "TX_INNER_EYE_NEG" in column_names and row[Column.TX_INNER_EYE_NEG]:
            # pyrefly: ignore [missing-attribute]
            tx_kwargs["innerEyeNeg"] = int(row[Column.TX_INNER_EYE_NEG])
        # pyrefly: ignore [missing-attribute]
        if "TX_INNER_EYE_POS" in column_names and row[Column.TX_INNER_EYE_POS]:
            # pyrefly: ignore [missing-attribute]
            tx_kwargs["innerEyePos"] = int(row[Column.TX_INNER_EYE_POS])
        # pyrefly: ignore [missing-attribute]
        if "TX_LDO_BYPASS" in column_names and row[Column.TX_LDO_BYPASS]:
            # pyrefly: ignore [missing-attribute]
            tx_kwargs["ldoBypass"] = int(row[Column.TX_LDO_BYPASS])
        # pyrefly: ignore [missing-attribute]
        if "TX_FFE_COEFF_5" in column_names and row[Column.TX_FFE_COEFF_5]:
            # pyrefly: ignore [missing-attribute]
            tx_kwargs["ffeCoeff5"] = int(row[Column.TX_FFE_COEFF_5])
        tx_setting = TxSettings(**tx_kwargs)

        rx_kwargs: Dict[str, Any] = {}
        # pyrefly: ignore [missing-attribute]
        if "RX_REACH" in column_names and row[Column.RX_REACH]:
            rx_kwargs["rxReach"] = (
                RxReach.RX_NORMAL_REACH
                # pyrefly: ignore [missing-attribute]
                if int(row[Column.RX_REACH]) == 0
                else RxReach.RX_EXTENDED_REACH
            )
        # pyrefly: ignore [missing-attribute]
        if "RX_CTLE_CODE" in column_names and row[Column.RX_CTLE_CODE]:
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["ctlCode"] = int(row[Column.RX_CTLE_CODE])
        # pyrefly: ignore [missing-attribute]
        if "RX_DSP_MODE" in column_names and row[Column.RX_DSP_MODE]:
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["dspMode"] = int(row[Column.RX_DSP_MODE])
        # pyrefly: ignore [missing-attribute]
        if "RX_AFE_TRIM" in column_names and row[Column.RX_AFE_TRIM]:
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["afeTrim"] = int(row[Column.RX_AFE_TRIM])
        # pyrefly: ignore [missing-attribute]
        if "RX_INSTG_BOOST1_STRT" in column_names and row[Column.RX_INSTG_BOOST1_STRT]:
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["instgBoost1Start"] = int(row[Column.RX_INSTG_BOOST1_STRT])
        # pyrefly: ignore [missing-attribute]
        if "RX_INSTG_BOOST1_STEP" in column_names and row[Column.RX_INSTG_BOOST1_STEP]:
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["instgBoost1Step"] = int(row[Column.RX_INSTG_BOOST1_STEP])
        # pyrefly: ignore [missing-attribute]
        if "RX_INSTG_BOOST1_STOP" in column_names and row[Column.RX_INSTG_BOOST1_STOP]:
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["instgBoost1Stop"] = int(row[Column.RX_INSTG_BOOST1_STOP])
        if (
            "RX_INSTG_BOOST2_OR_HR_STRT" in column_names
            # pyrefly: ignore [missing-attribute]
            and row[Column.RX_INSTG_BOOST2_OR_HR_STRT]
        ):
            rx_kwargs["instgBoost2OrHrStart"] = int(
                # pyrefly: ignore [missing-attribute]
                row[Column.RX_INSTG_BOOST2_OR_HR_STRT]
            )
        if (
            "RX_INSTG_BOOST2_OR_HR_STEP" in column_names
            # pyrefly: ignore [missing-attribute]
            and row[Column.RX_INSTG_BOOST2_OR_HR_STEP]
        ):
            rx_kwargs["instgBoost2OrHrStep"] = int(
                # pyrefly: ignore [missing-attribute]
                row[Column.RX_INSTG_BOOST2_OR_HR_STEP]
            )
        if (
            "RX_INSTG_BOOST2_OR_HR_STOP" in column_names
            # pyrefly: ignore [missing-attribute]
            and row[Column.RX_INSTG_BOOST2_OR_HR_STOP]
        ):
            rx_kwargs["instgBoost2OrHrStop"] = int(
                # pyrefly: ignore [missing-attribute]
                row[Column.RX_INSTG_BOOST2_OR_HR_STOP]
            )
        if (
            "RX_INSTG_C1_START_1P7" in column_names
            # pyrefly: ignore [missing-attribute]
            and row[Column.RX_INSTG_C1_START_1P7]
        ):
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["instgC1Start1p7"] = int(row[Column.RX_INSTG_C1_START_1P7])
        # pyrefly: ignore [missing-attribute]
        if "RX_INSTG_C1_STEP_1P7" in column_names and row[Column.RX_INSTG_C1_STEP_1P7]:
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["instgC1Step1p7"] = int(row[Column.RX_INSTG_C1_STEP_1P7])
        # pyrefly: ignore [missing-attribute]
        if "RX_INSTG_C1_STOP_1P7" in column_names and row[Column.RX_INSTG_C1_STOP_1P7]:
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["instgC1Stop1p7"] = int(row[Column.RX_INSTG_C1_STOP_1P7])
        if (
            "RX_INSTG_DFE_START_1P7" in column_names
            # pyrefly: ignore [missing-attribute]
            and row[Column.RX_INSTG_DFE_START_1P7]
        ):
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["instgDfeStart1p7"] = int(row[Column.RX_INSTG_DFE_START_1P7])
        if (
            "RX_INSTG_DFE_STEP_1P7" in column_names
            # pyrefly: ignore [missing-attribute]
            and row[Column.RX_INSTG_DFE_STEP_1P7]
        ):
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["instgDfeStep1p7"] = int(row[Column.RX_INSTG_DFE_STEP_1P7])
        if (
            "RX_INSTG_DFE_STOP_1P7" in column_names
            # pyrefly: ignore [missing-attribute]
            and row[Column.RX_INSTG_DFE_STOP_1P7]
        ):
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["instgDfeStop1p7"] = int(row[Column.RX_INSTG_DFE_STOP_1P7])
        # pyrefly: ignore [missing-attribute]
        if "RX_DIFF_ENCODER_EN" in column_names and row[Column.RX_DIFF_ENCODER_EN]:
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["diffEncoderEn"] = int(row[Column.RX_DIFF_ENCODER_EN])
        if (
            "RX_ENABLE_SCAN_SELECTION" in column_names
            # pyrefly: ignore [missing-attribute]
            and row[Column.RX_ENABLE_SCAN_SELECTION]
        ):
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["enableScanSelection"] = int(row[Column.RX_ENABLE_SCAN_SELECTION])
        if (
            "RX_INSTG_SCAN_USE_SR_SETTINGS" in column_names
            # pyrefly: ignore [missing-attribute]
            and row[Column.RX_INSTG_SCAN_USE_SR_SETTINGS]
        ):
            rx_kwargs["instgScanUseSrSettings"] = int(
                # pyrefly: ignore [missing-attribute]
                row[Column.RX_INSTG_SCAN_USE_SR_SETTINGS]
            )
        # pyrefly: ignore [missing-attribute]
        if "RX_CDR_CFG_OV_EN" in column_names and row[Column.RX_CDR_CFG_OV_EN]:
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["cdrCfgOvEn"] = int(row[Column.RX_CDR_CFG_OV_EN])
        if (
            "RX_CDR_TDET_1ST_ORD_STEP_OV_VAL" in column_names
            # pyrefly: ignore [missing-attribute]
            and row[Column.RX_CDR_TDET_1ST_ORD_STEP_OV_VAL]
        ):
            rx_kwargs["cdrTdet1stOrdStepOvVal"] = int(
                # pyrefly: ignore [missing-attribute]
                row[Column.RX_CDR_TDET_1ST_ORD_STEP_OV_VAL]
            )
        if (
            "RX_CDR_TDET_2ND_ORD_STEP_OV_VAL" in column_names
            # pyrefly: ignore [missing-attribute]
            and row[Column.RX_CDR_TDET_2ND_ORD_STEP_OV_VAL]
        ):
            rx_kwargs["cdrTdet2ndOrdStepOvVal"] = int(
                # pyrefly: ignore [missing-attribute]
                row[Column.RX_CDR_TDET_2ND_ORD_STEP_OV_VAL]
            )
        if (
            "RX_CDR_TDET_FINE_STEP_OV_VAL" in column_names
            # pyrefly: ignore [missing-attribute]
            and row[Column.RX_CDR_TDET_FINE_STEP_OV_VAL]
        ):
            rx_kwargs["cdrTdetFineStepOvVal"] = int(
                # pyrefly: ignore [missing-attribute]
                row[Column.RX_CDR_TDET_FINE_STEP_OV_VAL]
            )
        # pyrefly: ignore [missing-attribute]
        if "RX_LDO_BYPASS" in column_names and row[Column.RX_LDO_BYPASS]:
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["ldoBypass"] = int(row[Column.RX_LDO_BYPASS])
        # pyrefly: ignore [missing-attribute]
        if "RX_FFE_LENGTH_BITMAP" in column_names and row[Column.RX_FFE_LENGTH_BITMAP]:
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["ffeLengthBitmap"] = int(row[Column.RX_FFE_LENGTH_BITMAP])
        # pyrefly: ignore [missing-attribute]
        if "RX_INSTG_ENABLE_SCAN" in column_names and row[Column.RX_INSTG_ENABLE_SCAN]:
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["instgEnableScan"] = int(row[Column.RX_INSTG_ENABLE_SCAN])
        # pyrefly: ignore [missing-attribute]
        if "RX_DCW_EN" in column_names and row[Column.RX_DCW_EN]:
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["dcwEn"] = int(row[Column.RX_DCW_EN])
        if (
            "RX_DCW_STEP_COARSE_OV_VAL" in column_names
            # pyrefly: ignore [missing-attribute]
            and row[Column.RX_DCW_STEP_COARSE_OV_VAL]
        ):
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["dcwStepCoarseOvVal"] = int(row[Column.RX_DCW_STEP_COARSE_OV_VAL])
        if (
            "RX_DCW_STEP_FINE_OV_VAL" in column_names
            # pyrefly: ignore [missing-attribute]
            and row[Column.RX_DCW_STEP_FINE_OV_VAL]
        ):
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["dcwStepFineOvVal"] = int(row[Column.RX_DCW_STEP_FINE_OV_VAL])
        # pyrefly: ignore [missing-attribute]
        if "RX_DCW_OV_EN" in column_names and row[Column.RX_DCW_OV_EN]:
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["dcwOvEn"] = int(row[Column.RX_DCW_OV_EN])
        if (
            "RX_FFE_LMS_DYNAMIC_GATING_EN" in column_names
            # pyrefly: ignore [missing-attribute]
            and row[Column.RX_FFE_LMS_DYNAMIC_GATING_EN]
        ):
            rx_kwargs["ffeLmsDynamicGatingEn"] = int(
                # pyrefly: ignore [missing-attribute]
                row[Column.RX_FFE_LMS_DYNAMIC_GATING_EN]
            )
        # pyrefly: ignore [missing-attribute]
        if "RX_PRECODING" in column_names and row[Column.RX_PRECODING]:
            # pyrefly: ignore [missing-attribute]
            rx_kwargs["precoding"] = int(row[Column.RX_PRECODING])
        rx_setting = RxSettings(**rx_kwargs)

        # Handle custom collection attributes.
        tx_custom_collection = {}
        rx_custom_collection = {}

        for idx, column_name in enumerate(column_names.split(" ")):
            value = row[idx]
            if not value:
                continue
            if column_name.startswith(CUSTOM_TX_PREFIX):
                tx_custom_collection[column_name] = int(value)
            elif column_name.startswith(CUSTOM_RX_PREFIX):
                rx_custom_collection[column_name] = int(value)

        si_settings.append(
            SiSettingRow(
                pin_connection=pin_connection,
                factor=si_setting_factor,
                tx_setting=tx_setting,
                rx_setting=rx_setting,
                custom_tx_collection=tx_custom_collection or None,
                custom_rx_collection=rx_custom_collection or None,
                driver_peaking=driver_peaking,
            )
        )

    return SiSettings(si_settings=si_settings)


def read_asic_vendor_config(directory: Dict[str, str], prefix: str) -> AsicVendorConfig:
    VENDOR_CONFIG_SUFFIX = "_vendor_config.json"
    asic_vendor_config_json_str = get_content(directory, prefix + VENDOR_CONFIG_SUFFIX)
    asic_vendor_config_json = json.loads(asic_vendor_config_json_str)

    def stringify_map_values(map: Dict[str, Any]) -> Dict[str, str]:
        return {key: json.dumps(value) for key, value in map.items()}

    common_config = copy.deepcopy(asic_vendor_config_json["config"]["common_config"])
    prod_config = copy.deepcopy(asic_vendor_config_json["config"]["prod_config_only"])
    hw_test_config = copy.deepcopy(
        asic_vendor_config_json["config"]["hw_test_config_only"]
    )
    link_test_config = copy.deepcopy(
        asic_vendor_config_json["config"]["link_test_config_only"]
    )
    benchmark_config = copy.deepcopy(
        asic_vendor_config_json["config"]["benchmark_config_only"]
    )
    port_map_config = copy.deepcopy(
        asic_vendor_config_json["config"]["port_map_config"]
    )
    if "multistage_config" in asic_vendor_config_json["config"]:
        multistage_config = copy.deepcopy(
            asic_vendor_config_json["config"]["multistage_config"]
        )
    else:
        multistage_config = {}
    asic_vendor_config_params = AsicVendorConfigParams(
        commonConfig=stringify_map_values(common_config),
        prodConfig=stringify_map_values(prod_config),
        hwTestConfig=stringify_map_values(hw_test_config),
        linkTestConfig=stringify_map_values(link_test_config),
        benchmarkConfig=stringify_map_values(benchmark_config),
        portMapConfig=stringify_map_values(port_map_config),
        multistageConfig=stringify_map_values(multistage_config),
    )

    return AsicVendorConfig(asic_vendor_config_params=asic_vendor_config_params)


def read_integrated_transceiver_mapping(
    directory: Dict[str, str], prefix: str
) -> IntegratedTransceiverMapping:
    SUFFIX = "_integrated_transceiver_mapping.csv"
    Column = column_int_enum_generator(
        "TCVR_CHIP_ID TCVR_CORE_ID TCVR_LANE "
        + "OE_CHIP_ID OE_CORE_ID OE_LANE "
        + "LASER_SOURCE_CHIP_ID LASER_SOURCE_CORE_ID LASER_SOURCE_LANE",
    )
    connections = []
    for index, line in enumerate(get_content(directory, prefix + SUFFIX).splitlines()):
        if index < 1:
            continue
        row = line.split(",")
        tcvr_chip = Chip(
            slot_id=1,
            # pyrefly: ignore [missing-attribute]
            chip_id=int(row[Column.TCVR_CHIP_ID]),
            chip_type=ChipType.TRANSCEIVER,
            # pyrefly: ignore [missing-attribute]
            core_id=int(row[Column.TCVR_CORE_ID]),
            core_type=CoreType.BANKED_CMIS_INTEGRATED,
        )
        # pyrefly: ignore [missing-attribute]
        tcvr_lane = Lane(logical_id=int(row[Column.TCVR_LANE]))
        oe_chip = Chip(
            slot_id=1,
            # pyrefly: ignore [missing-attribute]
            chip_id=int(row[Column.OE_CHIP_ID]),
            chip_type=ChipType.OPTICAL_ENGINE,
            # pyrefly: ignore [missing-attribute]
            core_id=int(row[Column.OE_CORE_ID]),
            core_type=CoreType.INTEGRATED_OE,
        )
        # pyrefly: ignore [missing-attribute]
        oe_lane = Lane(logical_id=int(row[Column.OE_LANE]))
        laser_source_chip = Chip(
            slot_id=1,
            # pyrefly: ignore [missing-attribute]
            chip_id=int(row[Column.LASER_SOURCE_CHIP_ID]),
            chip_type=ChipType.LASER_SOURCE,
            # pyrefly: ignore [missing-attribute]
            core_id=int(row[Column.LASER_SOURCE_CORE_ID]),
            core_type=CoreType.ELSFP,
        )
        # pyrefly: ignore [missing-attribute]
        laser_source_lane = Lane(logical_id=int(row[Column.LASER_SOURCE_LANE]))
        connections.append(
            IntegratedTransceiverConnection(
                transceiver=ConnectionEnd(chip=tcvr_chip, lane=tcvr_lane),
                opticalEngine=ConnectionEnd(chip=oe_chip, lane=oe_lane),
                laserSource=ConnectionEnd(
                    chip=laser_source_chip, lane=laser_source_lane
                ),
            )
        )
    return IntegratedTransceiverMapping(connections=connections)
