# pyre-strict

import json
import os
from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class AsicConfigPaths:
    fboss_root: str

    @classmethod
    def from_root(cls, fboss_root: str) -> "AsicConfigPaths":
        return cls(os.path.abspath(fboss_root))

    @property
    def module_dir(self) -> str:
        return os.path.join(self.fboss_root, "lib", "asic_config_v3")

    @property
    def schemas_dir(self) -> str:
        return os.path.join(self.module_dir, "schemas")

    @property
    def configs_dir(self) -> str:
        return os.path.join(self.fboss_root, "configs")

    @property
    def asic_vendors_dir(self) -> str:
        return os.path.join(self.configs_dir, "asic_vendors")

    @property
    def platforms_dir(self) -> str:
        return os.path.join(self.configs_dir, "platforms")


def discover_platforms(
    paths: AsicConfigPaths,
) -> dict[str, tuple[dict[str, Any], str]]:
    """Return each ASIC config v3 platform and its generated output directory."""
    platforms: dict[str, tuple[dict[str, Any], str]] = {}
    platform_vendors: dict[str, str] = {}

    if not os.path.isdir(paths.platforms_dir):
        raise FileNotFoundError(
            f"Platform config directory '{paths.platforms_dir}' does not exist"
        )

    for platform_vendor in sorted(os.listdir(paths.platforms_dir)):
        vendor_path = os.path.join(paths.platforms_dir, platform_vendor)
        if not os.path.isdir(vendor_path):
            continue

        for platform_name in sorted(os.listdir(vendor_path)):
            platform_path = os.path.join(vendor_path, platform_name, "asic_config")
            if not os.path.isdir(platform_path):
                continue

            config_path = os.path.join(platform_path, "asic_config.json")
            if not os.path.exists(config_path):
                continue

            if platform_name in platforms:
                raise ValueError(
                    f"Duplicate platform '{platform_name}' found under system vendors "
                    f"'{platform_vendors[platform_name]}' and '{platform_vendor}'"
                )

            with open(config_path) as f:
                platform_config: dict[str, Any] = json.load(f)
            output_dir = os.path.join(platform_path, "generated")
            platforms[platform_name] = (platform_config, output_dir)
            platform_vendors[platform_name] = platform_vendor

    return platforms
