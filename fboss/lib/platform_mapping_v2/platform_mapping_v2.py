# pyre-strict
import sys
from typing import Any, Callable, Dict, List, Mapping, Optional, Sequence, Tuple

from fboss.lib.platform_mapping_v2.asic_vendor_config import AsicVendorConfig
from fboss.lib.platform_mapping_v2.helpers import (
    get_backplane_chip,
    get_connection_pairs_for_profile,
    get_laser_source_chip,
    get_mapping_pins,
    get_npu_chip,
    get_optical_engine_chip,
    get_pin_data_from_connections,
    get_platform_config_entry,
    get_transceiver_chip,
    get_unique_connection_pairs,
    get_xphy_chip,
    is_backplane,
    is_laser_source,
    is_npu,
    is_optical_engine,
    is_transceiver,
    is_xphy,
)
from fboss.lib.platform_mapping_v2.integrated_transceiver_mapping import (
    IntegratedTransceiverMapping,
)
from fboss.lib.platform_mapping_v2.port_profile_mapping import PortProfileMapping
from fboss.lib.platform_mapping_v2.profile_settings import ProfileSettings
from fboss.lib.platform_mapping_v2.read_files_utils import (
    PlatformMappingInputs,
    read_asic_vendor_config,
    read_integrated_transceiver_mapping,
    read_port_profile_mapping,
    read_profile_settings,
    read_si_settings,
    read_static_mapping,
)
from fboss.lib.platform_mapping_v2.si_settings import SiSettings
from fboss.lib.platform_mapping_v2.static_mapping import StaticMapping
from neteng.fboss.phy.phy.thrift_types import (
    DataPlanePhyChip,
    DataPlanePhyChipType,
    PortPinConfig,
    Side,
)
from neteng.fboss.platform_config.platform_config.thrift_types import (
    PlatformMapping,
    PlatformPortConfig,
    PlatformPortConfigOverride,
    PlatformPortConfigOverrideFactor,
    PlatformPortEntry,
    PlatformPortMapping,
    PlatformPortProfileConfigEntry,
    PortAssignment,
)
from neteng.fboss.platform_mapping_config.thrift_types import ChipType, CoreType
from neteng.fboss.switch_config.thrift_types import PortProfileID, PortType


class PlatformMappingParser:
    def __init__(
        self,
        directory_map: PlatformMappingInputs,
        platform: str,
        multi_npu: bool = False,
        version: Optional[str] = None,
    ) -> None:
        self.platform = platform
        self.version = version
        self._directory_map = directory_map
        self._multi_npu = multi_npu
        self._static_mapping: Optional[StaticMapping] = None
        self._port_profile_mapping: Optional[PortProfileMapping] = None
        self._profile_settings: Optional[ProfileSettings] = None
        self._si_settings: Optional[SiSettings] = None
        self._asic_vendor_config: Optional[AsicVendorConfig] = None
        self._integrated_tcvr_mapping: Optional[IntegratedTransceiverMapping] = None
        self._read_csvs()

    def get_directory(self, use_base_platform: bool = False) -> Dict[str, str]:
        platform = self.get_base_platform() if use_base_platform else self.platform
        return self._directory_map[platform].data

    def get_base_platform(self) -> str:
        return self._directory_map[self.platform].base_platform

    def get_mapping_prefix(self) -> str:
        return self.get_base_platform()

    def _read_csvs_with_base_platform_fallback(
        self, reader_func: Callable[..., Any], *args: Any, **kwargs: Any
    ) -> Any:
        # First attempt to search for the actual platform file, then fall back to the base platform.
        try:
            return reader_func(
                self.get_directory(),
                self.get_mapping_prefix(),
                *args,
                **kwargs,
            )
        except FileNotFoundError:
            return reader_func(
                self.get_directory(use_base_platform=True),
                self.get_mapping_prefix(),
                *args,
                **kwargs,
            )

    def _read_csvs(self) -> None:
        if self.platform == "yangra" or self.platform == "yangra2":
            # TODO(pshaikh): add processing for yangra platform csv processing
            self._static_mapping = StaticMapping(az_connections=[])
            self._port_profile_mapping = PortProfileMapping(ports={})
            self._profile_settings = ProfileSettings(speed_settings=[])
            self._si_settings = SiSettings(si_settings=[])
            return

        self._port_profile_mapping = self._read_csvs_with_base_platform_fallback(
            read_port_profile_mapping, multi_npu=self._multi_npu
        )
        self._static_mapping = self._read_csvs_with_base_platform_fallback(
            read_static_mapping
        )
        self._profile_settings = self._read_csvs_with_base_platform_fallback(
            read_profile_settings
        )
        self._si_settings = self._read_csvs_with_base_platform_fallback(
            read_si_settings, version=self.version
        )

        # Asic Vendor Config
        # We don't fail if asic_vendor_config is not found, since this file isn't needed for platform mapping generation directly.
        try:
            self._asic_vendor_config = self._read_csvs_with_base_platform_fallback(
                read_asic_vendor_config
            )
        except FileNotFoundError:
            print("No asic vendor config found...", file=sys.stderr)

        # Integrated transceiver mapping is optional — only CPO platforms have this CSV
        try:
            self._integrated_tcvr_mapping = read_integrated_transceiver_mapping(
                self.get_directory(),
                self.get_mapping_prefix(),
            )
        except (FileNotFoundError, KeyError):
            print("No integrated transceiver mapping found...", file=sys.stderr)

    def get_static_mapping(self) -> StaticMapping:
        if not self._static_mapping:
            raise TypeError(f"Static mapping file not defined for {self.platform}")
        return self._static_mapping

    def get_port_profile_mapping(self) -> PortProfileMapping:
        if not self._port_profile_mapping:
            raise TypeError(
                f"Port profile mapping file not defined for {self.platform}"
            )
        return self._port_profile_mapping

    def get_profile_settings(self) -> ProfileSettings:
        if not self._profile_settings:
            raise TypeError(f"Profile settings file not defined for {self.platform}")
        return self._profile_settings

    def get_si_settings(self) -> SiSettings:
        if not self._si_settings:
            raise TypeError(f"SI settings file not defined for {self.platform}")
        return self._si_settings

    def get_asic_vendor_config(self) -> Optional[AsicVendorConfig]:
        return self._asic_vendor_config

    def get_integrated_transceiver_mapping(
        self,
    ) -> Optional[IntegratedTransceiverMapping]:
        return self._integrated_tcvr_mapping


class PlatformMappingV2:
    def __init__(
        self,
        directory_map: PlatformMappingInputs,
        platform: str,
        multi_npu: bool = False,
        version: Optional[str] = None,
    ) -> None:
        self.platform = platform
        self._multi_npu = multi_npu
        self.pm_parser = PlatformMappingParser(
            directory_map, platform, multi_npu, version
        )
        self._name2entry: Dict[str, PlatformPortEntry] = {}
        self.platform_mapping: PlatformMapping = self._generate_platform_mapping()

    def _uses_root_port_controlling(self) -> bool:
        """For TH6+ platforms, all subsumed ports use ethX/Y/1 as controlling port."""
        # TODO: will add tahansb800bc once the test passed on tahansb800bc
        base_platform = self.pm_parser.get_base_platform()
        if base_platform in ("ladakh800bcls", "leh800bcls", "tahansb800bc"):
            return False
        for chip in self.pm_parser.get_static_mapping().get_chips():
            if chip.chip_type == ChipType.NPU:
                core_type_name = chip.core_type.name
                if not core_type_name.startswith("TH"):
                    return False
                if chip.core_type == CoreType.TH5_NIF:
                    return False
                return True
        return False

    def get_platform_mapping(self) -> PlatformMapping:
        return self.platform_mapping

    def get_platform_profiles(self) -> Sequence[PlatformPortProfileConfigEntry]:
        return self.platform_mapping.platformSupportedProfiles

    def get_platform_port_map(self) -> Mapping[int, PlatformPortEntry]:
        return self.platform_mapping.ports

    def get_port_id_to_port_assignment(self) -> Dict[int, PortAssignment]:
        return {
            port_id: PortAssignment(
                portName=entry.mapping.name,
                portType=entry.mapping.portType,
                attachedCorePortIndex=entry.mapping.attachedCorePortIndex,
                scope=entry.mapping.scope,
            )
            for port_id, entry in self.platform_mapping.ports.items()
        }

    @staticmethod
    def _to_raw_port_entry(
        entry: PlatformPortEntry, id_to_name: Mapping[int, str]
    ) -> PlatformPortEntry:
        return PlatformPortEntry(
            mapping=PlatformPortMapping(
                id=0,
                name=entry.mapping.name,
                controllingPort=0,
                controllingPortName=id_to_name[entry.mapping.controllingPort],
                pins=entry.mapping.pins,
                attachedCoreId=entry.mapping.attachedCoreId,
                virtualDeviceId=entry.mapping.virtualDeviceId,
            ),
            supportedProfiles={
                profile: PlatformPortConfig(
                    pins=config.pins,
                    subsumedPortNames=(
                        [id_to_name[port_id] for port_id in config.subsumedPorts]
                        if config.subsumedPorts is not None
                        else None
                    ),
                )
                for profile, config in entry.supportedProfiles.items()
            },
        )

    @staticmethod
    def _merge_raw_port_entries(
        existing: PlatformPortEntry, incoming: PlatformPortEntry
    ) -> PlatformPortEntry:
        name = existing.mapping.name
        if (
            existing.mapping.attachedCoreId != incoming.mapping.attachedCoreId
            or existing.mapping.virtualDeviceId != incoming.mapping.virtualDeviceId
            or existing.mapping.controllingPortName
            != incoming.mapping.controllingPortName
        ):
            raise ValueError(f"Conflicting hardware topology for port {name}")

        merged_pins = list(existing.mapping.pins)
        for pin in incoming.mapping.pins:
            if pin not in merged_pins:
                merged_pins.append(pin)
        merged_pins.sort(key=lambda pin: pin.a.lane)

        merged_profiles = dict(existing.supportedProfiles)
        for profile, config in incoming.supportedProfiles.items():
            if profile not in merged_profiles:
                merged_profiles[profile] = config
                continue

            existing_config = merged_profiles[profile]
            if existing_config.pins != config.pins:
                raise ValueError(
                    f"Conflicting profile pins for {profile} on port {name}"
                )
            subsumed_names: set[str] = set()
            subsumed_names.update(existing_config.subsumedPortNames or [])
            subsumed_names.update(config.subsumedPortNames or [])
            merged_profiles[profile] = PlatformPortConfig(
                pins=config.pins,
                subsumedPortNames=sorted(subsumed_names) or None,
            )

        return PlatformPortEntry(
            mapping=PlatformPortMapping(
                id=0,
                name=name,
                controllingPort=0,
                controllingPortName=existing.mapping.controllingPortName,
                pins=merged_pins,
                attachedCoreId=existing.mapping.attachedCoreId,
                virtualDeviceId=existing.mapping.virtualDeviceId,
            ),
            supportedProfiles=dict(
                sorted(merged_profiles.items(), key=lambda item: int(item[0]))
            ),
        )

    @staticmethod
    def _normalize_raw_override(
        override: PlatformPortConfigOverride, id_to_name: Mapping[int, str]
    ) -> PlatformPortConfigOverride:
        factor = override.factor
        return PlatformPortConfigOverride(
            factor=PlatformPortConfigOverrideFactor(
                profiles=factor.profiles,
                cableLengths=factor.cableLengths,
                portNames=(
                    [id_to_name[port_id] for port_id in factor.ports]
                    if factor.ports is not None
                    else None
                ),
                transceiverManagementInterface=factor.transceiverManagementInterface,
                chips=factor.chips,
                mediaInterfaceCode=factor.mediaInterfaceCode,
                vendor=factor.vendor,
            ),
            pins=override.pins,
            portProfileConfig=override.portProfileConfig,
            driverPeaking=override.driverPeaking,
        )

    @staticmethod
    def generate_raw_platform_mapping(
        platform_mappings: Sequence[PlatformMapping],
    ) -> PlatformMapping:
        if not platform_mappings:
            raise ValueError("At least one platform mapping is required")

        raw_ports: Dict[str, PlatformPortEntry] = {}
        raw_overrides: List[PlatformPortConfigOverride] = []
        for platform_mapping in platform_mappings:
            id_to_name = {
                port_id: entry.mapping.name
                for port_id, entry in platform_mapping.ports.items()
            }
            for entry in platform_mapping.ports.values():
                name = entry.mapping.name
                incoming = PlatformMappingV2._to_raw_port_entry(entry, id_to_name)
                if name not in raw_ports:
                    raw_ports[name] = incoming
                    continue
                raw_ports[name] = PlatformMappingV2._merge_raw_port_entries(
                    raw_ports[name], incoming
                )

            for override in platform_mapping.portConfigOverrides or []:
                normalized_override = PlatformMappingV2._normalize_raw_override(
                    override, id_to_name
                )
                if normalized_override not in raw_overrides:
                    raw_overrides.append(normalized_override)

        first_mapping = platform_mappings[0]
        return PlatformMapping(
            ports={},
            chips=first_mapping.chips,
            portConfigOverrides=raw_overrides or None,
            platformSupportedProfiles=first_mapping.platformSupportedProfiles,
            rawPlatformPorts=dict(sorted(raw_ports.items())),
        )

    def get_chips(self) -> Sequence[DataPlanePhyChip]:
        return self.platform_mapping.chips

    def get_num_switch_asics(self) -> int:
        return self.pm_parser.get_static_mapping().get_num_switch_asics()

    def get_override_factors(self) -> Optional[Sequence[PlatformPortConfigOverride]]:
        return self.platform_mapping.portConfigOverrides

    # Sort unique_factors by:
    # ports[0], then profiles[0], then vendor.name then vendor.partNumber
    def _sort_key(
        self,
        factor_in: Tuple[PlatformPortConfigOverrideFactor, Tuple[Tuple[int, int], ...]],
    ) -> tuple[str, int, str, str]:
        # Use empty string or 0 if list is empty or attribute is missing
        factor = factor_in[0]
        profile = (
            str(int(factor.profiles[0]))
            if getattr(factor, "profiles", None)
            and factor.profiles is not None
            and len(factor.profiles) > 0
            else ""
        )
        port = (
            factor.ports[0]
            if getattr(factor, "ports", None)
            and factor.ports is not None
            and len(factor.ports) > 0
            else 0
        )
        vendor_name = ""
        if (
            getattr(factor, "vendor", None)
            and factor.vendor is not None
            and hasattr(factor.vendor, "name")
        ):
            vendor_name = factor.vendor.name or ""
        part_number = ""
        if (
            getattr(factor, "vendor", None)
            and factor.vendor is not None
            and hasattr(factor.vendor, "partNumber")
        ):
            part_number = factor.vendor.partNumber or ""
        return (profile, port, vendor_name, part_number)

    def _sort_and_merge_port_config_overrides(
        self, port_config_overrides: List[PlatformPortConfigOverride]
    ) -> List[PlatformPortConfigOverride]:
        # merge lists
        merged_factors: List[
            Tuple[
                PlatformPortConfigOverrideFactor,
                List[PortPinConfig],
                Dict[int, int],
            ]
        ] = []
        for port_config_override in port_config_overrides:
            found = False
            for factor, pins, driver_peakings in merged_factors:
                if factor == port_config_override.factor:
                    found = True
                    if port_config_override.pins is not None:
                        pins.append(port_config_override.pins)
                    if port_config_override.driverPeaking is not None:
                        driver_peakings.update(port_config_override.driverPeaking)

            if not found:
                driver_peaking: Dict[int, int] = {}
                if port_config_override.driverPeaking is not None:
                    driver_peaking.update(port_config_override.driverPeaking)
                if port_config_override.pins is not None:
                    merged_factors.append(
                        (
                            port_config_override.factor,
                            [port_config_override.pins],
                            driver_peaking,
                        )
                    )

        # generate output
        retval: List[PlatformPortConfigOverride] = []
        unique_factors: set[
            Tuple[PlatformPortConfigOverrideFactor, Tuple[Tuple[int, int], ...]]
        ] = set()

        for factor, _, driver_peaking in merged_factors:
            unique_factors.add((factor, tuple(driver_peaking.items())))

        unique_factors_list = sorted(unique_factors, key=self._sort_key)

        # pyrefly: ignore [bad-assignment]
        for unique_factor, driver_peaking in unique_factors_list:
            port_pin_config_list: List[PortPinConfig] = []
            for merged_factor, merged_port_pin_config_list, _ in merged_factors:
                if merged_factor == unique_factor:
                    port_pin_config_list.extend(merged_port_pin_config_list or [])
            all_iphy_pins = []
            for port_pin_config in port_pin_config_list:
                if len(port_pin_config.iphy) > 0:
                    all_iphy_pins.extend(port_pin_config.iphy)

            if len(all_iphy_pins) > 0:
                final_port_pin_config = PortPinConfig(iphy=all_iphy_pins)
                platform_port_config_override = PlatformPortConfigOverride(
                    factor=unique_factor,
                    driverPeaking=dict[int, int](driver_peaking),
                    pins=final_port_pin_config,
                )
                retval.append(platform_port_config_override)

        return retval

    def _generate_platform_mapping(self) -> PlatformMapping:
        (ports, port_config_overrides) = self._generate_ports()
        return PlatformMapping(
            ports=ports,
            chips=self._generate_chips(),
            portConfigOverrides=port_config_overrides,
            platformSupportedProfiles=self._generate_platform_supported_profiles(),
        )

    def _generate_chips(self) -> List[DataPlanePhyChip]:
        parsed_chips = self.pm_parser.get_static_mapping().get_chips()
        chips = []
        if self.platform == "yangra" or self.platform == "yangra2":
            # TODO(pshaikh): add logic to generate chips for yangra
            return chips

        reverse_tcvr_map = (
            self.pm_parser.get_static_mapping().get_reverse_transceiver_map()
        )

        for chip in parsed_chips:
            if is_npu(chip.chip_type):
                # Skip adding NPUs other than the first if it's not multi npu
                if not self._multi_npu and chip.chip_id != 1:
                    continue
                chips.append(get_npu_chip(chip))
            elif is_transceiver(chip.chip_type):
                key = (chip.chip_id, chip.core_id)
                if key not in reverse_tcvr_map:
                    raise Exception(
                        f"Transceiver chip_id={chip.chip_id}, core_id={chip.core_id} "
                        f"not found in reverse transceiver map"
                    )
                virtual_id = reverse_tcvr_map[key]
                chips.append(get_transceiver_chip(chip, physical_id=virtual_id - 1))
            elif is_backplane(chip.chip_type):
                chips.append(get_backplane_chip(chip))
            elif is_xphy(chip.chip_type):
                chips.append(get_xphy_chip(chip))
            else:
                raise Exception("Unhandled chip_type ", chip.chip_type)

        integrated_mapping = self.pm_parser.get_integrated_transceiver_mapping()
        if integrated_mapping is not None:
            for chip in integrated_mapping.get_chips():
                if is_optical_engine(chip.chip_type):
                    chips.append(get_optical_engine_chip(chip))
                elif is_laser_source(chip.chip_type):
                    chips.append(get_laser_source_chip(chip))

        chips = sorted(chips, key=lambda chip: (chip.type, chip.physicalID, chip.name))
        return chips

    def _generate_platform_supported_profiles(
        self,
    ) -> List[PlatformPortProfileConfigEntry]:
        platform_config_entry = []
        if self.platform == "yangra" or self.platform == "yangra2":
            # TODO(pshaikh): add logic to generate platform supported profiles for yangra
            return platform_config_entry

        all_profiles = self.pm_parser.get_port_profile_mapping().get_all_profiles()

        for profile in all_profiles:
            # Get NPU speed setting (always required)
            npu_speed_setting = self.pm_parser.get_profile_settings().get_speed_setting(
                profile,
                DataPlanePhyChipType.IPHY,
            )

            # Get XPHY line speed setting
            xphy_line_speed_setting = None
            try:
                xphy_line_speed_setting = (
                    self.pm_parser.get_profile_settings().get_speed_setting(
                        profile,
                        DataPlanePhyChipType.XPHY,
                        Side.LINE,
                    )
                )
            except Exception:
                # XPHY line speed setting doesn't exist for this profile, which is okay
                pass

            # Get XPHY system speed setting
            xphy_system_speed_setting = None
            try:
                xphy_system_speed_setting = (
                    self.pm_parser.get_profile_settings().get_speed_setting(
                        profile,
                        DataPlanePhyChipType.XPHY,
                        Side.SYSTEM,
                    )
                )
            except Exception:
                # XPHY system speed setting doesn't exist for this profile, which is okay
                pass

            entry = get_platform_config_entry(
                profile=profile,
                npu_speed_setting=npu_speed_setting,
                xphy_line_speed_setting=xphy_line_speed_setting,
                xphy_system_speed_setting=xphy_system_speed_setting,
            )
            if entry:
                platform_config_entry.append(entry)

        return platform_config_entry

    def _generate_ports(
        self,
    ) -> tuple[
        Dict[int, PlatformPortEntry], Optional[List[PlatformPortConfigOverride]]
    ]:
        ports = {}
        port_config_overrides: List[PlatformPortConfigOverride] = []
        if self.platform == "yangra" or self.platform == "yangra2":
            # TODO(pshaikh): add logic to generate ports for yangra
            return (ports, port_config_overrides)

        for port_id, port_detail in (
            self.pm_parser.get_port_profile_mapping().get_ports().items()
        ):
            supported_profiles = {}
            all_connection_pairs = []
            # Get pins for all supported profiles
            for profile in port_detail.supported_profiles:
                speed_setting = self.pm_parser.get_profile_settings().get_speed_setting(
                    profile=profile,
                    phy_chip_type=DataPlanePhyChipType.IPHY,
                )
                profile_connections = get_connection_pairs_for_profile(
                    static_mapping=self.pm_parser.get_static_mapping(),
                    port=port_detail,
                    num_lanes=speed_setting.num_lanes,
                    profile=profile,
                )
                all_connection_pairs = all_connection_pairs + profile_connections

                lane_speed = (
                    0
                    if speed_setting.num_lanes == 0
                    else speed_setting.speed / speed_setting.num_lanes
                )
                [
                    pins,
                    platform_port_config_override,
                ] = get_pin_data_from_connections(
                    connections=profile_connections,
                    si_settings=self.pm_parser.get_si_settings(),
                    profile=profile,
                    # pyre-fixme[6]: Expected `PortSpeed` for 4th param, but got `float`.
                    lane_speed=lane_speed,
                    port_id=port_detail.global_port_id,
                    integrated_tcvr_mapping=self.pm_parser.get_integrated_transceiver_mapping(),
                )
                if len(platform_port_config_override) > 0:
                    port_config_overrides.extend(platform_port_config_override)
                supported_profiles[profile] = PlatformPortConfig(pins=pins)

            # We now know pins used for each of the supported profiles.
            # Combine all the pins and populate mapping.pins
            all_connection_pairs = get_unique_connection_pairs(all_connection_pairs)
            mapping_pins = get_mapping_pins(all_connection_pairs)
            # Sort pins by lane id of the 'a' end
            mapping_pins = sorted(mapping_pins, key=lambda pin: pin.a.lane)
            # If explicit controlling_port is specified in the CSV, use it directly.
            controlling_port = (
                port_detail.controlling_port
                if port_detail.controlling_port is not None
                else port_id
            )
            mapping = PlatformPortMapping(
                id=port_id,
                name=port_detail.port_name,
                pins=mapping_pins,
                controllingPort=controlling_port,
                portType=port_detail.port_type,
                scope=port_detail.scope,
                attachedCoreId=port_detail.attached_coreid,
                attachedCorePortIndex=port_detail.attached_core_portid,
                virtualDeviceId=port_detail.virtual_device_id,
            )
            port_entry = PlatformPortEntry(
                supportedProfiles=dict(
                    sorted(supported_profiles.items(), key=lambda x: int(x[0]))
                ),
                mapping=mapping,
            )

            ports[port_id] = port_entry

            self._name2entry[mapping.name] = port_entry

        # Sort and Merge port_config_overrides
        merged_port_config_overrides = self._sort_and_merge_port_config_overrides(
            port_config_overrides
        )

        # Populate subsumedPorts and controllingPort now that we have
        # information about every port.
        # The logic is - when a port is left with no iphy pins to use after some
        # other port uses its iphy pins, then the former port needs to be a
        # subsumed port controlled by the later port
        #
        # Two-pass approach: first collect what changes need to be made,
        # then reconstruct the affected structs with the new data.
        # This is needed because thrift-python structs are immutable.

        # First pass: collect subsumedPorts and controllingPort updates
        # Key: (port_id, profile) -> list of subsumed port ids
        subsumed_ports_map: Dict[int, Dict[int, List[int]]] = {}
        # Key: port_id -> new controlling port id
        controlling_port_updates: Dict[int, int] = {}
        # Key: port_id -> list of subsumed port ids for HYPER_PORT
        hyper_port_subsumed: Dict[int, List[int]] = {}

        uses_root_port_controlling = self._uses_root_port_controlling()

        for port_id, port_entry in ports.items():
            port_detail = self.pm_parser.get_port_profile_mapping().get_ports()[port_id]
            has_explicit_controlling_port = port_detail.controlling_port is not None

            for port_config in port_entry.supportedProfiles.values():
                if port_entry.mapping.portType != PortType.HYPER_PORT:
                    all_iphy_pins_needed = port_config.pins.iphy
                    for other_port_id, other_port_entry in ports.items():
                        if (
                            port_id == other_port_id
                            or other_port_entry.mapping.portType == PortType.HYPER_PORT
                        ):
                            continue
                        for (
                            other_profile,
                            other_port_config,
                        ) in other_port_entry.supportedProfiles.items():
                            other_port_pin_ids = [
                                pin_config.id
                                for pin_config in other_port_config.pins.iphy
                            ]
                            needed_pin_ids = [
                                pin_config.id for pin_config in all_iphy_pins_needed
                            ]

                            if needed_pin_ids and all(
                                needed_iphy_pin_id in other_port_pin_ids
                                for needed_iphy_pin_id in needed_pin_ids
                            ):
                                if other_port_id not in subsumed_ports_map:
                                    subsumed_ports_map[other_port_id] = {}
                                if (
                                    other_profile
                                    not in subsumed_ports_map[other_port_id]
                                ):
                                    subsumed_ports_map[other_port_id][
                                        other_profile
                                    ] = []
                                if (
                                    port_id
                                    not in subsumed_ports_map[other_port_id][
                                        other_profile
                                    ]
                                ):
                                    subsumed_ports_map[other_port_id][
                                        other_profile
                                    ].append(port_id)
                                if not has_explicit_controlling_port:
                                    controlling_port_updates[port_id] = other_port_id
                                if uses_root_port_controlling:
                                    # For TH6+ platforms, all subsumed ports
                                    # use ethx/x/1 as the controlling port
                                    rootPortName = port_entry.mapping.name[:-1] + "1"
                                    rootPortEntry = self._name2entry[rootPortName]
                                    controlling_port_updates[port_id] = (
                                        rootPortEntry.mapping.controllingPort
                                    )

                elif port_entry.mapping.portType == PortType.HYPER_PORT:
                    for other_port_id, other_port_entry in ports.items():
                        other_port_detail = (
                            self.pm_parser.get_port_profile_mapping().get_ports()[
                                other_port_id
                            ]
                        )
                        if (
                            port_id == other_port_id
                            or other_port_entry.mapping.portType == PortType.HYPER_PORT
                        ):
                            continue
                        if other_port_detail.parent_port_id == port_id:
                            if port_id not in hyper_port_subsumed:
                                hyper_port_subsumed[port_id] = []
                            hyper_port_subsumed[port_id].append(other_port_id)

        # Second pass: reconstruct ports with subsumedPorts and controllingPort updates
        for port_id in set(
            list(subsumed_ports_map.keys())
            + list(controlling_port_updates.keys())
            + list(hyper_port_subsumed.keys())
        ):
            old_entry = ports[port_id]
            new_supported_profiles = {}
            for profile, port_config in old_entry.supportedProfiles.items():
                subsumed = None
                if (
                    port_id in subsumed_ports_map
                    and profile in subsumed_ports_map[port_id]
                ):
                    subsumed = subsumed_ports_map[port_id][profile]
                elif (
                    port_id in hyper_port_subsumed
                    and profile == PortProfileID.PROFILE_DEFAULT
                ):
                    subsumed = hyper_port_subsumed[port_id]
                if subsumed is not None:
                    new_supported_profiles[profile] = PlatformPortConfig(
                        pins=port_config.pins,
                        subsumedPorts=subsumed,
                    )
                else:
                    new_supported_profiles[profile] = port_config

            new_controlling_port = old_entry.mapping.controllingPort
            if port_id in controlling_port_updates:
                new_controlling_port = controlling_port_updates[port_id]

            new_mapping = PlatformPortMapping(
                id=old_entry.mapping.id,
                name=old_entry.mapping.name,
                pins=old_entry.mapping.pins,
                controllingPort=new_controlling_port,
                portType=old_entry.mapping.portType,
                scope=old_entry.mapping.scope,
                attachedCoreId=old_entry.mapping.attachedCoreId,
                attachedCorePortIndex=old_entry.mapping.attachedCorePortIndex,
                virtualDeviceId=old_entry.mapping.virtualDeviceId,
            )
            ports[port_id] = PlatformPortEntry(
                supportedProfiles=dict(
                    sorted(new_supported_profiles.items(), key=lambda x: int(x[0]))
                ),
                mapping=new_mapping,
            )

        sorted_ports = dict(sorted(ports.items()))
        if len(merged_port_config_overrides) == 0:
            merged_port_config_overrides = None
        return (sorted_ports, merged_port_config_overrides)
