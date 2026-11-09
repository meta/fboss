#!/usr/bin/env python3

# pyre-strict

import json
import os
import tempfile
import unittest
from pathlib import Path
from typing import Dict, List, Optional

from fboss.lib.platform_mapping_v2.gen import (
    generate_platform_mappings_from_vendor_data,
)
from fboss.lib.platform_mapping_v2.platform_mapping_v2 import (
    PlatformMappingParser,
    PlatformMappingV2,
)
from fboss.lib.platform_mapping_v2.read_files_utils import (
    discover_platform_mapping_inputs,
    PlatformMappingInput,
    PlatformMappingInputs,
    read_platform_descriptor,
    read_vendor_data,
)
from fboss.lib.platform_mapping_v2.static_mapping import StaticMapping
from neteng.fboss.phy.phy.thrift_types import (
    DataPlanePhyChip,
    DataPlanePhyChipType,
    FecMode,
    InterfaceType,
    IpModulation,
    Pin,
    PinConfig,
    PinConnection,
    PinID,
    PortPinConfig,
    PortProfileConfig,
    ProfileSideConfig,
    TxSettings,
)
from neteng.fboss.platform_config.platform_config.thrift_types import (
    PlatformPortConfig,
    PlatformPortConfigFactor,
    PlatformPortConfigOverride,
    PlatformPortConfigOverrideFactor,
    PlatformPortEntry,
    PlatformPortMapping,
    PlatformPortProfileConfigEntry,
)
from neteng.fboss.platform_mapping_config import thrift_types as pm_types
from neteng.fboss.platform_mapping_config.thrift_types import ChipType, CoreType
from neteng.fboss.switch_config.thrift_types import (
    PortProfileID,
    PortSpeed,
    PortType,
    Scope,
)
from neteng.fboss.transceiver.thrift_types import TransmitterTechnology, Vendor


class TestPlatformMappingInputDiscovery(unittest.TestCase):
    def _create_input(
        self,
        root: str,
        vendor: str,
        platform: str,
        variant: Optional[str] = None,
        mapping_subdir: str = "platform_mapping",
    ) -> str:
        path_parts = [root, vendor, platform]
        if variant is not None:
            path_parts.extend(["variants", variant])
        path_parts.append(mapping_subdir)
        input_dir = os.path.join(*path_parts)
        os.makedirs(input_dir)
        with open(os.path.join(input_dir, "input.json"), "w") as config_file:
            config_file.write("{}")
        return input_dir

    def test_discovers_base_and_variant_sources(self) -> None:
        with tempfile.TemporaryDirectory() as platforms_dir:
            base_dir = self._create_input(platforms_dir, "arista", "meru800bia")
            variant_dir = self._create_input(
                platforms_dir,
                "arista",
                "meru800bia",
                variant="unrelated_alias",
            )

            inputs = discover_platform_mapping_inputs(platforms_dir)

            self.assertEqual(set(inputs), {"meru800bia", "unrelated_alias"})
            self.assertEqual(
                inputs["meru800bia"],
                PlatformMappingInput(
                    base_platform="meru800bia",
                    input_dir=base_dir,
                    vendor="arista",
                    data={"input.json": "{}"},
                ),
            )
            self.assertEqual(
                inputs["unrelated_alias"],
                PlatformMappingInput(
                    base_platform="meru800bia",
                    input_dir=variant_dir,
                    vendor="arista",
                    data={"input.json": "{}"},
                ),
            )

    def test_rejects_duplicate_platform_names_across_vendors(self) -> None:
        with tempfile.TemporaryDirectory() as platforms_dir:
            self._create_input(platforms_dir, "arista", "duplicate")
            self._create_input(platforms_dir, "celestica", "duplicate")

            with self.assertRaisesRegex(
                ValueError, "Duplicate platform mapping input 'duplicate'"
            ):
                discover_platform_mapping_inputs(platforms_dir)

    def test_rejects_variants_without_base_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as platforms_dir:
            variant_dir = os.path.join(
                platforms_dir,
                "arista",
                "meru800bia",
                "variants",
                "meru800bia_variant",
                "platform_mapping",
            )
            os.makedirs(variant_dir)

            with self.assertRaisesRegex(
                ValueError, "has platform mapping variants but no base input directory"
            ):
                discover_platform_mapping_inputs(platforms_dir)

    def test_ignores_variants_for_other_services(self) -> None:
        with tempfile.TemporaryDirectory() as platforms_dir:
            os.makedirs(
                os.path.join(
                    platforms_dir,
                    "arista",
                    "meru800bia",
                    "variants",
                    "service_only_variant",
                    "services",
                )
            )

            inputs = discover_platform_mapping_inputs(platforms_dir)

            self.assertEqual(len(inputs), 0)

    def test_discovers_custom_mapping_subdirectory(self) -> None:
        with tempfile.TemporaryDirectory() as platforms_dir:
            input_dir = self._create_input(
                platforms_dir,
                "cisco",
                "morgan800cc",
                mapping_subdir=os.path.join("facebook", "platform_mapping"),
            )

            inputs = discover_platform_mapping_inputs(
                platforms_dir,
                mapping_subdir=os.path.join("facebook", "platform_mapping"),
            )

            self.assertEqual(
                inputs,
                {
                    "morgan800cc": PlatformMappingInput(
                        base_platform="morgan800cc",
                        input_dir=input_dir,
                        vendor="cisco",
                        data={"input.json": "{}"},
                    )
                },
            )

    def test_parser_uses_catalog_base_platform(self) -> None:
        base_data = read_vendor_data("fboss/lib/platform_mapping_v2/test/test_data")
        inputs = {
            "test": PlatformMappingInput("test", "", "test", base_data),
            "unrelated_alias": PlatformMappingInput("test", "", "test", {}),
        }

        parser = PlatformMappingParser(inputs, "unrelated_alias")

        self.assertEqual(parser.get_base_platform(), "test")
        self.assertTrue(parser.get_static_mapping().get_chips())


class TestPlatformMappingGeneration(unittest.TestCase):
    def _get_test_vendor_data(self, folder: str) -> PlatformMappingInputs:
        input_dir = f"fboss/lib/platform_mapping_v2/test/{folder}"
        return {
            "test": PlatformMappingInput(
                "test", input_dir, "test", read_vendor_data(input_dir)
            )
        }

    def _get_expected_single_npu_test_ports(self) -> Dict[int, PlatformPortEntry]:
        port_one_mapping = PlatformPortEntry(
            mapping=PlatformPortMapping(
                id=1,
                name="eth1/2/1",
                controllingPort=1,
                pins=[
                    PinConnection(
                        a=PinID(chip="NPU-TH5_NIF-slot1/chip1/core0", lane=0),
                        z=Pin(end=PinID(chip="TRANSCEIVER-OSFP-slot1/chip2", lane=0)),
                    ),
                    PinConnection(
                        a=PinID(chip="NPU-TH5_NIF-slot1/chip1/core0", lane=1),
                        z=Pin(end=PinID(chip="TRANSCEIVER-OSFP-slot1/chip2", lane=1)),
                    ),
                    PinConnection(
                        a=PinID(chip="NPU-TH5_NIF-slot1/chip1/core0", lane=2),
                        z=Pin(end=PinID(chip="TRANSCEIVER-OSFP-slot1/chip2", lane=2)),
                    ),
                    PinConnection(
                        a=PinID(chip="NPU-TH5_NIF-slot1/chip1/core0", lane=3),
                        z=Pin(end=PinID(chip="TRANSCEIVER-OSFP-slot1/chip2", lane=3)),
                    ),
                ],
                portType=PortType.INTERFACE_PORT,
                scope=Scope.LOCAL,
            ),
            supportedProfiles={
                PortProfileID.PROFILE_100G_4_NRZ_RS528_OPTICAL: PlatformPortConfig(
                    subsumedPorts=[2],
                    pins=PortPinConfig(
                        iphy=[
                            PinConfig(
                                id=PinID(chip="NPU-TH5_NIF-slot1/chip1/core0", lane=0),
                                tx=TxSettings(
                                    pre=-12,
                                    pre2=0,
                                    main=144,
                                    post=-8,
                                    post2=0,
                                    post3=0,
                                    pre3=0,
                                ),
                            ),
                            PinConfig(
                                id=PinID(chip="NPU-TH5_NIF-slot1/chip1/core0", lane=1),
                                tx=TxSettings(
                                    pre=-12,
                                    pre2=0,
                                    main=144,
                                    post=-8,
                                    post2=0,
                                    post3=0,
                                    pre3=0,
                                ),
                            ),
                            PinConfig(
                                id=PinID(chip="NPU-TH5_NIF-slot1/chip1/core0", lane=2),
                                tx=TxSettings(
                                    pre=-12,
                                    pre2=0,
                                    main=144,
                                    post=-8,
                                    post2=0,
                                    post3=0,
                                    pre3=0,
                                ),
                            ),
                            PinConfig(
                                id=PinID(chip="NPU-TH5_NIF-slot1/chip1/core0", lane=3),
                                tx=TxSettings(
                                    pre=-12,
                                    pre2=0,
                                    main=144,
                                    post=-8,
                                    post2=0,
                                    post3=0,
                                    pre3=0,
                                ),
                            ),
                        ],
                        transceiver=[
                            PinConfig(
                                id=PinID(chip="TRANSCEIVER-OSFP-slot1/chip2", lane=0)
                            ),
                            PinConfig(
                                id=PinID(chip="TRANSCEIVER-OSFP-slot1/chip2", lane=1)
                            ),
                            PinConfig(
                                id=PinID(chip="TRANSCEIVER-OSFP-slot1/chip2", lane=2)
                            ),
                            PinConfig(
                                id=PinID(chip="TRANSCEIVER-OSFP-slot1/chip2", lane=3)
                            ),
                        ],
                    ),
                ),
                PortProfileID.PROFILE_100G_1_PAM4_RS544_OPTICAL: PlatformPortConfig(
                    pins=PortPinConfig(
                        iphy=[
                            PinConfig(
                                id=PinID(chip="NPU-TH5_NIF-slot1/chip1/core0", lane=0)
                            )
                        ],
                        transceiver=[
                            PinConfig(
                                id=PinID(chip="TRANSCEIVER-OSFP-slot1/chip2", lane=0)
                            )
                        ],
                    )
                ),
            },
        )
        port_two_mapping = PlatformPortEntry(
            mapping=PlatformPortMapping(
                id=2,
                name="eth1/2/2",
                controllingPort=1,
                pins=[
                    PinConnection(
                        a=PinID(chip="NPU-TH5_NIF-slot1/chip1/core0", lane=1),
                        z=Pin(end=PinID(chip="TRANSCEIVER-OSFP-slot1/chip2", lane=1)),
                    )
                ],
                portType=PortType.INTERFACE_PORT,
                scope=Scope.LOCAL,
            ),
            supportedProfiles={
                PortProfileID.PROFILE_100G_1_PAM4_RS544_OPTICAL: PlatformPortConfig(
                    pins=PortPinConfig(
                        iphy=[
                            PinConfig(
                                id=PinID(chip="NPU-TH5_NIF-slot1/chip1/core0", lane=1)
                            )
                        ],
                        transceiver=[
                            PinConfig(
                                id=PinID(chip="TRANSCEIVER-OSFP-slot1/chip2", lane=1)
                            )
                        ],
                    )
                )
            },
        )
        return {1: port_one_mapping, 2: port_two_mapping}

    def _get_expected_multi_npu_test_ports(self) -> Dict[int, PlatformPortEntry]:
        multi_npu_port_one = PlatformPortEntry(
            mapping=PlatformPortMapping(
                id=102,
                name="eth1/3/1",
                controllingPort=102,
                pins=[
                    PinConnection(
                        a=PinID(chip="NPU-TH5_NIF-slot1/chip2/core0", lane=0),
                        z=Pin(end=PinID(chip="TRANSCEIVER-OSFP-slot1/chip3", lane=0)),
                    ),
                    PinConnection(
                        a=PinID(chip="NPU-TH5_NIF-slot1/chip2/core0", lane=1),
                        z=Pin(end=PinID(chip="TRANSCEIVER-OSFP-slot1/chip3", lane=1)),
                    ),
                    PinConnection(
                        a=PinID(chip="NPU-TH5_NIF-slot1/chip2/core0", lane=2),
                        z=Pin(end=PinID(chip="TRANSCEIVER-OSFP-slot1/chip3", lane=2)),
                    ),
                    PinConnection(
                        a=PinID(chip="NPU-TH5_NIF-slot1/chip2/core0", lane=3),
                        z=Pin(end=PinID(chip="TRANSCEIVER-OSFP-slot1/chip3", lane=3)),
                    ),
                ],
                portType=PortType.INTERFACE_PORT,
                scope=Scope.LOCAL,
            ),
            supportedProfiles={
                PortProfileID.PROFILE_100G_4_NRZ_RS528_OPTICAL: PlatformPortConfig(
                    subsumedPorts=[103],
                    pins=PortPinConfig(
                        iphy=[
                            PinConfig(
                                id=PinID(chip="NPU-TH5_NIF-slot1/chip2/core0", lane=0)
                            ),
                            PinConfig(
                                id=PinID(chip="NPU-TH5_NIF-slot1/chip2/core0", lane=1)
                            ),
                            PinConfig(
                                id=PinID(chip="NPU-TH5_NIF-slot1/chip2/core0", lane=2)
                            ),
                            PinConfig(
                                id=PinID(chip="NPU-TH5_NIF-slot1/chip2/core0", lane=3)
                            ),
                        ],
                        transceiver=[
                            PinConfig(
                                id=PinID(chip="TRANSCEIVER-OSFP-slot1/chip3", lane=0)
                            ),
                            PinConfig(
                                id=PinID(chip="TRANSCEIVER-OSFP-slot1/chip3", lane=1)
                            ),
                            PinConfig(
                                id=PinID(chip="TRANSCEIVER-OSFP-slot1/chip3", lane=2)
                            ),
                            PinConfig(
                                id=PinID(chip="TRANSCEIVER-OSFP-slot1/chip3", lane=3)
                            ),
                        ],
                    ),
                ),
                PortProfileID.PROFILE_100G_1_PAM4_RS544_OPTICAL: PlatformPortConfig(
                    pins=PortPinConfig(
                        iphy=[
                            PinConfig(
                                id=PinID(chip="NPU-TH5_NIF-slot1/chip2/core0", lane=0)
                            )
                        ],
                        transceiver=[
                            PinConfig(
                                id=PinID(chip="TRANSCEIVER-OSFP-slot1/chip3", lane=0)
                            )
                        ],
                    )
                ),
            },
        )
        multi_npu_port_two = PlatformPortEntry(
            mapping=PlatformPortMapping(
                id=103,
                name="eth1/3/2",
                controllingPort=102,
                pins=[
                    PinConnection(
                        a=PinID(chip="NPU-TH5_NIF-slot1/chip2/core0", lane=1),
                        z=Pin(end=PinID(chip="TRANSCEIVER-OSFP-slot1/chip3", lane=1)),
                    )
                ],
                portType=PortType.INTERFACE_PORT,
                scope=Scope.LOCAL,
            ),
            supportedProfiles={
                PortProfileID.PROFILE_100G_1_PAM4_RS544_OPTICAL: PlatformPortConfig(
                    pins=PortPinConfig(
                        iphy=[
                            PinConfig(
                                id=PinID(chip="NPU-TH5_NIF-slot1/chip2/core0", lane=1)
                            )
                        ],
                        transceiver=[
                            PinConfig(
                                id=PinID(chip="TRANSCEIVER-OSFP-slot1/chip3", lane=1)
                            )
                        ],
                    )
                )
            },
        )
        return self._get_expected_single_npu_test_ports() | {
            102: multi_npu_port_one,
            103: multi_npu_port_two,
        }

    def _get_expected_single_npu_test_chips(self) -> List[DataPlanePhyChip]:
        chip_one = DataPlanePhyChip(
            name="NPU-TH5_NIF-slot1/chip1/core0",
            type=DataPlanePhyChipType.IPHY,
            physicalID=0,
        )
        chip_two = DataPlanePhyChip(
            name="TRANSCEIVER-OSFP-slot1/chip2",
            type=DataPlanePhyChipType.TRANSCEIVER,
            physicalID=1,
        )
        chip_three = DataPlanePhyChip(
            name="TRANSCEIVER-OSFP-slot1/chip3",
            type=DataPlanePhyChipType.TRANSCEIVER,
            physicalID=2,
        )
        return [chip_one, chip_two, chip_three]

    def _get_expected_multi_npu_test_chips(self) -> List[DataPlanePhyChip]:
        return self._get_expected_single_npu_test_chips() + [
            DataPlanePhyChip(
                name="NPU-TH5_NIF-slot1/chip2/core0",
                type=DataPlanePhyChipType.IPHY,
                physicalID=0,
            )
        ]

    def _get_expected_single_npu_supported_profiles(
        self,
    ) -> List[PlatformPortProfileConfigEntry]:
        entry_one = PlatformPortProfileConfigEntry(
            factor=PlatformPortConfigFactor(
                profileID=PortProfileID.PROFILE_100G_4_NRZ_RS528_OPTICAL
            ),
            profile=PortProfileConfig(
                speed=PortSpeed.HUNDREDG,
                iphy=ProfileSideConfig(
                    numLanes=4,
                    modulation=IpModulation.NRZ,
                    fec=FecMode.RS528,
                    medium=TransmitterTechnology.OPTICAL,
                    interfaceType=InterfaceType.SR4,
                ),
            ),
        )
        entry_two = PlatformPortProfileConfigEntry(
            factor=PlatformPortConfigFactor(
                profileID=PortProfileID.PROFILE_100G_1_PAM4_RS544_OPTICAL
            ),
            profile=PortProfileConfig(
                speed=PortSpeed.HUNDREDG,
                iphy=ProfileSideConfig(
                    numLanes=1,
                    modulation=IpModulation.PAM4,
                    fec=FecMode.RS544_2N,
                    medium=TransmitterTechnology.BACKPLANE,
                    interfaceType=InterfaceType.KR4,
                ),
            ),
        )
        return [entry_one, entry_two]

    def _get_expected_override_factors(self) -> List[PlatformPortConfigOverride]:
        return [
            PlatformPortConfigOverride(
                factor=PlatformPortConfigOverrideFactor(
                    ports=[1],
                    profiles=[PortProfileID.PROFILE_100G_4_NRZ_RS528_OPTICAL],
                    vendor=Vendor(
                        name="VENDOR_1",
                        oui=b"",
                        partNumber="PART_NUM_1",
                        rev="",
                        serialNumber="",
                        dateCode="",
                    ),
                ),
                pins=PortPinConfig(
                    iphy=[
                        PinConfig(
                            id=PinID(chip="NPU-TH5_NIF-slot1/chip1/core0", lane=0),
                            tx=TxSettings(
                                pre=1,
                                pre2=1,
                                main=1,
                                post=1,
                                post2=1,
                                post3=1,
                                pre3=1,
                            ),
                        ),
                        PinConfig(
                            id=PinID(chip="NPU-TH5_NIF-slot1/chip1/core0", lane=1),
                            tx=TxSettings(
                                pre=2,
                                pre2=2,
                                main=2,
                                post=2,
                                post2=2,
                                post3=2,
                                pre3=2,
                            ),
                        ),
                        PinConfig(
                            id=PinID(chip="NPU-TH5_NIF-slot1/chip1/core0", lane=2),
                            tx=TxSettings(
                                pre=3,
                                pre2=3,
                                main=3,
                                post=3,
                                post2=3,
                                post3=3,
                                pre3=3,
                            ),
                        ),
                        PinConfig(
                            id=PinID(chip="NPU-TH5_NIF-slot1/chip1/core0", lane=3),
                            tx=TxSettings(
                                pre=4,
                                pre2=4,
                                main=4,
                                post=4,
                                post2=4,
                                post3=4,
                                pre3=4,
                            ),
                        ),
                    ]
                ),
                driverPeaking={
                    0: 0,
                    1: 1,
                    2: 2,
                    3: 3,
                },
            )
        ]

    def _verify_single_npu_platform_mapping(
        self,
        platform_mapping: PlatformMappingV2,
        overrides: Optional[List[PlatformPortConfigOverride]],
    ) -> None:
        # Verify ports
        self.assertEqual(
            platform_mapping.get_platform_port_map(),
            self._get_expected_single_npu_test_ports(),
            "Ports do not match.",
        )

        # Verify chips
        chips = platform_mapping.get_chips()
        self.assertEqual(len(chips), 3, "Number of chips does not match.")
        self.assertTrue(
            all(chip in chips for chip in self._get_expected_single_npu_test_chips()),
            "Chips do not match.",
        )

        # Verify supportedProfiles
        supportedProfiles = platform_mapping.get_platform_profiles()
        self.assertEqual(
            len(supportedProfiles), 2, "Number of supported profiles does not match."
        )
        self.assertTrue(
            all(
                entry in supportedProfiles
                for entry in self._get_expected_single_npu_supported_profiles()
            ),
            "Supported profiles do not match.",
        )
        self.assertEqual(
            platform_mapping.get_override_factors(),
            overrides,
            "Override factors not equal",
        )

    def _verify_multi_npu_platform_mapping(
        self,
        platform_mapping: PlatformMappingV2,
        overrides: Optional[List[PlatformPortConfigOverride]],
    ) -> None:
        # Verify ports
        self.assertEqual(
            platform_mapping.get_platform_port_map(),
            self._get_expected_multi_npu_test_ports(),
            "Ports do not match.",
        )

        # Verify chips
        chips = platform_mapping.get_chips()
        self.assertEqual(len(chips), 4, "Number of chips does not match.")
        self.assertTrue(
            all(chip in chips for chip in self._get_expected_multi_npu_test_chips()),
            "Chips do not match.",
        )

        # Verify supportedProfiles (supportedProfiles should be the same for both single and multi NPU)
        supportedProfiles = platform_mapping.get_platform_profiles()
        self.assertEqual(
            len(supportedProfiles), 2, "Number of supported profiles does not match."
        )
        self.assertTrue(
            all(
                entry in supportedProfiles
                for entry in self._get_expected_single_npu_supported_profiles()
            ),
            "Supported profiles do not match.",
        )
        self.assertEqual(
            platform_mapping.get_override_factors(),
            overrides,
            "Override factors not equal",
        )

    def test_get_platform_mapping_single_npu(self) -> None:
        platform_mapping = PlatformMappingV2(
            self._get_test_vendor_data("test_data"), "test", multi_npu=False
        )
        self._verify_single_npu_platform_mapping(platform_mapping, None)

    def test_get_platform_mapping_multi_npu(self) -> None:
        platform_mapping = PlatformMappingV2(
            self._get_test_vendor_data("test_data"), "test", multi_npu=True
        )
        self._verify_multi_npu_platform_mapping(platform_mapping, None)

    def test_get_num_switch_asics(self) -> None:
        platform_mapping = PlatformMappingV2(
            self._get_test_vendor_data("test_data"), "test", multi_npu=True
        )

        self.assertEqual(2, platform_mapping.get_num_switch_asics())

    def test_generated_descriptor_includes_num_switch_asics(self) -> None:
        vendor_data = self._get_test_vendor_data("test_data")
        vendor_data["test"].data["test_platform_descriptor.csv"] = "\n".join(
            [
                "System_Vendor,Platform_Type,Product_Name_Prefixes,Mode_Names,Asic_Type",
                "celestica,PLATFORM_WEDGE800BACT,TEST,test,ASIC_TYPE_TOMAHAWK5",
            ]
        )

        with tempfile.TemporaryDirectory() as output_dir:
            generate_platform_mappings_from_vendor_data(
                vendor_data, output_dir, "test", is_multi_npu=True
            )
            with open(
                Path(output_dir) / "celestica" / "test" / "platform_descriptor.json"
            ) as descriptor_file:
                descriptor = json.load(descriptor_file)

        self.assertEqual(2, descriptor["numSwitchAsics"])

    def test_get_platform_mapping_single_npu_with_overrides(self) -> None:
        platform_mapping = PlatformMappingV2(
            self._get_test_vendor_data("test_data_factor_overrides"),
            "test",
            multi_npu=False,
        )
        self._verify_single_npu_platform_mapping(
            platform_mapping, self._get_expected_override_factors()
        )

    def test_get_platform_mapping_multi_npu_with_overrides(self) -> None:
        platform_mapping = PlatformMappingV2(
            self._get_test_vendor_data("test_data_factor_overrides"),
            "test",
            multi_npu=True,
        )
        self._verify_multi_npu_platform_mapping(
            platform_mapping, self._get_expected_override_factors()
        )

    def test_montblanc_family_port_five_uses_cage_root_controller(
        self,
    ) -> None:
        vendor_data = discover_platform_mapping_inputs("fboss/configs/platforms")
        platform_mappings = [
            PlatformMappingV2(
                vendor_data, platform, multi_npu=False
            ).get_platform_mapping()
            for platform in (
                "montblanc",
                "montblanc_odd_ports_8x100G",
                "montblanc_gtsw_yolo",
            )
        ]
        try:
            raw_mapping = PlatformMappingV2.generate_raw_platform_mapping(
                platform_mappings
            )
        except ValueError as error:
            self.fail(str(error))
        raw_ports = raw_mapping.rawPlatformPorts
        self.assertIsNotNone(raw_ports)
        if raw_ports is None:
            return

        port_fives = {
            name: entry for name, entry in raw_ports.items() if name.endswith("/5")
        }
        self.assertEqual(64, len(port_fives))
        for name, entry in port_fives.items():
            cage_root = f"{name.rsplit('/', 1)[0]}/1"
            self.assertEqual(cage_root, entry.mapping.controllingPortName, name)

    def test_read_platform_descriptor_variant_attributes(self) -> None:
        descriptor = read_platform_descriptor(
            {
                "test_platform_descriptor.csv": "\n".join(
                    [
                        "System_Vendor,Platform_Type,Product_Name_Prefixes,Mode_Names,Asic_Type,Variant_Attributes",
                        "celestica,PLATFORM_TAHANSB800BC,TAHANSB800BC,tahansb800bc,ASIC_TYPE_TOMAHAWK6,test_fixture=true;rack=false",
                    ]
                )
            },
            "test",
        )

        self.assertEqual(
            descriptor["variantAttributes"],
            {"test_fixture": True, "rack": False},
        )

    def test_read_platform_descriptor_without_variant_attributes(self) -> None:
        descriptor = read_platform_descriptor(
            {
                "test_platform_descriptor.csv": "\n".join(
                    [
                        "System_Vendor,Platform_Type,Product_Name_Prefixes,Mode_Names,Asic_Type",
                        "celestica,PLATFORM_TAHANSB800BC,TAHANSB800BC,tahansb800bc,ASIC_TYPE_TOMAHAWK6",
                    ]
                )
            },
            "test",
        )

        self.assertNotIn("variantAttributes", descriptor)


class StaticMappingPerChipTest(unittest.TestCase):
    """Per-chip lane/polarity maps on a multi-NPU platform.

    The flat maps are keyed by core_id alone, so two NPUs sharing a core_id
    collapse into one entry. These cover the chip-aware views that keep them
    apart.
    """

    CORE_ID = 3
    NUM_LANES = 4

    def _connection(
        self, chip_id: int, lane_id: int, tx_swap: bool, rx_swap: bool
    ) -> pm_types.ConnectionPair:
        return pm_types.ConnectionPair(
            a=pm_types.ConnectionEnd(
                chip=pm_types.Chip(
                    slot_id=1,
                    chip_id=chip_id,
                    chip_type=ChipType.NPU,
                    core_id=self.CORE_ID,
                    core_type=CoreType.TH6_NIF,
                ),
                lane=pm_types.Lane(
                    logical_id=lane_id,
                    tx_physical_lane=lane_id + (10 * chip_id),
                    rx_physical_lane=lane_id + (20 * chip_id),
                    tx_polarity_swap=tx_swap,
                    rx_polarity_swap=rx_swap,
                ),
            )
        )

    def setUp(self) -> None:
        # chip 1 and chip 2 share core_id but differ in polarity on every lane,
        # mirroring the leh800bcls board-level P/N swap on one NPU only.
        connections = []
        for lane_id in range(self.NUM_LANES):
            connections.append(self._connection(1, lane_id, True, False))
            connections.append(self._connection(2, lane_id, False, True))
        self.static_mapping = StaticMapping(connections)

    def test_pn_swap_filtered_by_chip_id(self) -> None:
        chip1_tx = self.static_mapping._get_pn_swap_map_by_core(
            self.CORE_ID, "TX", chip_id=1
        )
        chip2_tx = self.static_mapping._get_pn_swap_map_by_core(
            self.CORE_ID, "TX", chip_id=2
        )
        self.assertEqual([1] * self.NUM_LANES, chip1_tx)
        self.assertEqual([0] * self.NUM_LANES, chip2_tx)

        chip1_rx = self.static_mapping._get_pn_swap_map_by_core(
            self.CORE_ID, "RX", chip_id=1
        )
        chip2_rx = self.static_mapping._get_pn_swap_map_by_core(
            self.CORE_ID, "RX", chip_id=2
        )
        self.assertEqual([0] * self.NUM_LANES, chip1_rx)
        self.assertEqual([1] * self.NUM_LANES, chip2_rx)

    def test_phy_lane_map_filtered_by_chip_id(self) -> None:
        self.assertEqual(
            [lane + 10 for lane in range(self.NUM_LANES)],
            self.static_mapping._get_phy_lane_map_by_core(
                self.CORE_ID, "TX", chip_id=1
            ),
        )
        self.assertEqual(
            [lane + 20 for lane in range(self.NUM_LANES)],
            self.static_mapping._get_phy_lane_map_by_core(
                self.CORE_ID, "TX", chip_id=2
            ),
        )
        self.assertEqual(
            [lane + 20 for lane in range(self.NUM_LANES)],
            self.static_mapping._get_phy_lane_map_by_core(
                self.CORE_ID, "RX", chip_id=1
            ),
        )
        self.assertEqual(
            [lane + 40 for lane in range(self.NUM_LANES)],
            self.static_mapping._get_phy_lane_map_by_core(
                self.CORE_ID, "RX", chip_id=2
            ),
        )

    def test_omitting_chip_id_preserves_flat_behaviour(self) -> None:
        # Without a chip_id the walk spans both chips and the last row wins,
        # which is exactly why the by_chip views are needed.
        flat_tx = self.static_mapping._get_pn_swap_map_by_core(self.CORE_ID, "TX")
        chip2_tx = self.static_mapping._get_pn_swap_map_by_core(
            self.CORE_ID, "TX", chip_id=2
        )
        self.assertEqual(chip2_tx, flat_tx)

    def test_by_chip_maps_keep_npus_distinct(self) -> None:
        polarity = self.static_mapping.gen_polarity_swap_map_by_chip()
        self.assertEqual([1, 2], sorted(polarity))
        self.assertNotEqual(polarity[1][self.CORE_ID], polarity[2][self.CORE_ID])
        self.assertEqual([1] * self.NUM_LANES, polarity[1][self.CORE_ID].tx_lane_info)
        self.assertEqual([0] * self.NUM_LANES, polarity[2][self.CORE_ID].tx_lane_info)

        lanes = self.static_mapping.gen_phy_lane_map_by_chip()
        self.assertEqual([1, 2], sorted(lanes))
        self.assertNotEqual(lanes[1][self.CORE_ID], lanes[2][self.CORE_ID])

    def test_get_static_mapping_populates_both_views(self) -> None:
        mapping = self.static_mapping.get_static_mapping()
        # Flat maps still present and unchanged for existing consumers.
        self.assertIn(self.CORE_ID, mapping.phy_lane_map)
        self.assertIn(self.CORE_ID, mapping.polarity_swap_map)
        # New per-chip views carry both NPUs. The fields are optional on the
        # thrift struct, so bind and narrow before indexing.
        polarity_by_chip = mapping.polarity_swap_map_by_chip
        lane_by_chip = mapping.phy_lane_map_by_chip
        assert polarity_by_chip is not None
        assert lane_by_chip is not None
        self.assertEqual([1, 2], sorted(polarity_by_chip))
        self.assertEqual([1, 2], sorted(lane_by_chip))
        self.assertNotEqual(
            polarity_by_chip[1][self.CORE_ID],
            polarity_by_chip[2][self.CORE_ID],
        )


def run_tests() -> None:
    # Provided for add_fb_python_executable callable
    loader = unittest.TestLoader()
    suite = unittest.TestSuite(
        (
            loader.loadTestsFromTestCase(TestPlatformMappingInputDiscovery),
            loader.loadTestsFromTestCase(TestPlatformMappingGeneration),
            loader.loadTestsFromTestCase(StaticMappingPerChipTest),
        )
    )
    result = unittest.TextTestRunner().run(suite)

    if not result.wasSuccessful():
        raise Exception("Test failures.")
