# pyre-strict

import json
import os
from typing import Any

from fboss.lib.asic_config_v3.base_generator import BaseAsicConfigGenerator
from fboss.lib.asic_config_v3.paths import AsicConfigPaths
from fboss.lib.platform_mapping_v2.platform_mapping_v2 import PlatformMappingParser
from fboss.lib.platform_mapping_v2.read_files_utils import (
    discover_platform_mapping_inputs,
)


class BroadcomDnxGenerator(BaseAsicConfigGenerator):
    """Data-driven ASIC config generator for the Broadcom DNX family.

    Produces key-value JSON output in the thrift AsicConfig layout.
    """

    ASIC_FAMILY: str = "dnx"
    SUPPORTED_EFFECTS: frozenset[str] = frozenset({"apply", "apply_from"})

    def __init__(
        self,
        platform_name: str,
        variant: str,
        platform_config: dict[str, Any],
        paths: AsicConfigPaths,
    ) -> None:
        super().__init__(platform_name, variant, platform_config, paths)

        self.common: dict[str, str] = {}
        # Output sections of the thrift AsicConfig struct that conditional
        # settings may target. Single-NPU platforms have only the common section.
        self.sections: dict[str, dict[str, str]] = {"common": self.common}

        self._load_asic_vendor_input_configs()
        self._validate_conditional_settings()

        output_structure = self.platform_config.get("output_structure", {})
        self.output_mode: str = output_structure.get("mode", "common_only")
        if self.output_mode != "common_only":
            raise NotImplementedError(
                f"output_structure.mode '{self.output_mode}' is not supported; "
                "only single-NPU (common_only) platforms are implemented"
            )

        # A variant may target a sibling platform_mapping_v2 directory rather
        # than the platform's own when several variants share an asic_config
        # but consume different lane / polarity data.
        mapping_name = self.variant_config.get(
            "platform_mapping_name", self.platform_name
        )
        self.parser = PlatformMappingParser(
            discover_platform_mapping_inputs(self.paths.platforms_dir), mapping_name
        )

    @property
    def output_extension(self) -> str:
        return ".json"

    def _load_asic_vendor_input_configs(self) -> None:
        """Load the input config files from the asic_vendors directory."""
        asic_config_path = os.path.join(
            self.paths.asic_vendors_dir,
            self.asic_vendor,
            self.ASIC_FAMILY,
            "asics",
            f"{self.asic_name}.json",
        )
        with open(asic_config_path) as f:
            self.asic_config = json.load(f)

    def _apply_base_sdk_settings(self) -> None:
        """Apply the ASIC-wide SDK settings emitted for every variant."""
        self.common.update(self.asic_config.get("base_sdk_settings", {}))

    def _apply_declarative_tables(self) -> None:
        """Emit the declarative ASIC tables.

        The tables cover port speeds, TM port headers, DTM flow regions, and
        flow remote cores.
        """
        tables = self.asic_config.get("declarative_tables", {})

        self.common.update(tables.get("port_speed_map", {}))

        self.common.update(tables.get("tm_port_headers", {}))

        region_map = tables.get("dtm_flow_region_map")
        if region_map:
            start = region_map["start_region"]
            for region in range(start, start + region_map["count"]):
                key = region_map["key_format"].format(region=region)
                self.common[key] = region_map["value"]

        self.common.update(tables.get("flow_remote_cores", {}))

    def _apply_platform_sdk_overrides(self) -> None:
        """Apply platform-level SDK settings on top of the ASIC-wide base."""
        self.common.update(self.variant_config.get("platform_sdk_overrides", {}))

    def _generate_lane_and_polarity_maps(self) -> None:
        """Emit lane-to-serdes and polarity-flip keys from platform_mapping_v2.

        Iterates the platform connections, keeps the ends whose core type the
        ASIC declares, computes ``lane = core_id * num_lanes_per_core +
        logical_lane``, and renders the SOC keys through the ASIC key formats.
        """
        core_types = self.asic_config.get("core_types", {})
        key_formats = self.asic_config["key_formats"]
        suffix = self.asic_config["asic_suffix"]
        lanes_per_core = self.asic_config.get("num_lanes_per_core", 8)

        for pair in self.parser.get_static_mapping().get_az_connections():
            end = pair.a
            core_type_name = end.chip.core_type.name
            families = core_types.get(core_type_name)
            if not families:
                continue

            if end.lane.rx_physical_lane is None or end.lane.tx_physical_lane is None:
                raise ValueError(
                    f"Connection end for core type {core_type_name} "
                    f"(core {end.chip.core_id}, lane {end.lane.logical_id}) "
                    "has no physical lane data"
                )

            lane = end.chip.core_id * lanes_per_core + end.lane.logical_id
            lane_key = key_formats["lane_map"].format(
                family=families["lane_map_family"], lane=lane, suffix=suffix
            )
            self.common[lane_key] = key_formats["lane_map_value"].format(
                rx=end.lane.rx_physical_lane, tx=end.lane.tx_physical_lane
            )

            for direction in ("rx", "tx"):
                polarity_key = key_formats[f"polarity_{direction}"].format(
                    family=families["polarity_family"], lane=lane, suffix=suffix
                )
                swap = getattr(end.lane, f"{direction}_polarity_swap")
                self.common[polarity_key] = "1" if swap else "0"

        # Default polarity keys without a lane number, emitted when the ASIC
        # declares them.
        self.common.update(self.asic_config.get("default_polarity_settings", {}))

    def _apply_settings(self, target: str, settings: dict[str, Any]) -> None:
        """Write settings into the named output section."""
        self.sections[target].update(settings)

    def _validate_apply_target(self, name: str, target: str) -> None:
        """Raise ValueError when the named output section does not exist."""
        if target not in self.sections:
            raise ValueError(
                f"Conditional setting '{name}' targets '{target}', which is not an "
                "output section of this platform"
            )

    def _validate_apply_value(
        self, name: str, target: str, key: str, value: Any
    ) -> None:
        """Raise ValueError unless the value is a string, as SOC properties are."""
        if not isinstance(value, str):
            raise ValueError(
                f"Conditional setting '{name}' sets {target}.{key} to a non-string value"
            )

    def generate(self) -> str:
        """Build the full ASIC config and return it as a JSON string.

        Priority order (later steps override earlier ones on key overlap):

          1. ASIC-wide base SDK settings.
          2. Declarative ASIC tables (port speeds, TM port headers, DTM flow
             regions, flow remote cores).
          3. Platform-level SDK overrides.
          4. Lane and polarity maps from platform_mapping_v2.
          5. Conditional settings (ASIC-level, then platform-level).
        """
        self._apply_base_sdk_settings()
        self._apply_declarative_tables()
        self._apply_platform_sdk_overrides()
        self._generate_lane_and_polarity_maps()
        self._execute_apply_effects()

        return self._serialize()

    def _serialize(self) -> str:
        """Render the key-value map as JSON in the thrift AsicConfig layout."""
        asic_config = {"common": {"config": dict(sorted(self.common.items()))}}
        return json.dumps(asic_config, indent=2)
