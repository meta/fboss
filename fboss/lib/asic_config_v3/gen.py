# pyre-strict

import argparse
import os
import sys

from fboss.lib.asic_config_v3.base_generator import BaseAsicConfigGenerator
from fboss.lib.asic_config_v3.generators.broadcom_dnx_generator import (
    BroadcomDnxGenerator,
)
from fboss.lib.asic_config_v3.generators.broadcom_xgs_generator import (
    BroadcomXgsGenerator,
)
from fboss.lib.asic_config_v3.paths import AsicConfigPaths, discover_platforms

# Add a new (vendor, asic) entry when bringing up a new ASIC family.
_GENERATOR_REGISTRY: dict[tuple[str, str], type[BaseAsicConfigGenerator]] = {
    ("broadcom", "tomahawk5"): BroadcomXgsGenerator,
    ("broadcom", "tomahawk6"): BroadcomXgsGenerator,
    ("broadcom", "jericho3"): BroadcomDnxGenerator,
}


def get_generator(
    platform_name: str, variant: str, platform_config: dict, paths: AsicConfigPaths
) -> BaseAsicConfigGenerator:
    """Instantiate the correct generator based on vendor and ASIC."""
    vendor = platform_config["vendor"]
    asic = platform_config["asic"]
    key = (vendor, asic)
    generator_cls = _GENERATOR_REGISTRY.get(key)
    if not generator_cls:
        raise ValueError(f"No generator registered for vendor={vendor}, asic={asic}")
    return generator_cls(platform_name, variant, platform_config, paths)


def get_output_filename(variant: str, extension: str) -> str:
    """Return the generated filename for a named platform variant."""
    if not variant:
        raise ValueError("ASIC config variant names must not be empty")
    return f"{variant}{extension}"


def _generate_platform(
    platform_name: str,
    platform_config: dict,
    output_dir: str,
    paths: AsicConfigPaths,
    clean_output: bool = False,
) -> None:
    """Generate ASIC configs for every variant of a single platform.

    Output files are written to the platform's ``generated`` directory.
    Platforms whose (vendor, asic) pair has no registered generator are skipped.
    """
    vendor = platform_config.get("vendor", "")
    asic = platform_config.get("asic", "")

    if (vendor, asic) not in _GENERATOR_REGISTRY:
        print(
            f"Skipping {platform_name} (no generator for vendor={vendor}, asic={asic})",
            file=sys.stderr,
        )
        return

    if clean_output:
        _clean_output(output_dir)

    variants = platform_config.get("variants", {})

    for variant_name in variants:
        display_name = f"{platform_name}/{variant_name}"
        print(f"Generating ASIC config for {display_name}...", file=sys.stderr)

        try:
            generator = get_generator(
                platform_name, variant_name, platform_config, paths
            )
            output = generator.generate()

            output_filename = get_output_filename(
                variant_name, generator.output_extension
            )
            output_path = os.path.join(output_dir, output_filename)

            print(f"Writing to {output_path}", file=sys.stderr)
            os.makedirs(output_dir, exist_ok=True)
            with open(output_path, "w", encoding="utf-8") as f:
                f.write(output)

        except Exception as e:
            print(f"Error generating config for {display_name}: {e}", file=sys.stderr)
            raise


def _clean_output(output_dir: str) -> None:
    """Remove previously generated outputs from one platform directory."""
    if not os.path.isdir(output_dir):
        return

    for filename in sorted(os.listdir(output_dir)):
        if not filename.endswith((".json", ".yml")):
            continue
        os.remove(os.path.join(output_dir, filename))


def _clean_all_outputs(paths: AsicConfigPaths) -> None:
    """Remove generated outputs, including those for deleted platform configs."""
    for platform_vendor in sorted(os.listdir(paths.platforms_dir)):
        vendor_path = os.path.join(paths.platforms_dir, platform_vendor)
        if not os.path.isdir(vendor_path):
            continue

        for platform_name in sorted(os.listdir(vendor_path)):
            output_dir = os.path.join(
                vendor_path, platform_name, "asic_config", "generated"
            )
            _clean_output(output_dir)


def generate_all_asic_configs(paths: AsicConfigPaths) -> None:
    """Generate ASIC configs for every discovered platform and variant."""
    platforms = discover_platforms(paths)
    _clean_all_outputs(paths)

    for platform_name in sorted(platforms):
        platform_config, output_dir = platforms[platform_name]
        _generate_platform(platform_name, platform_config, output_dir, paths)


def generate_single_platform(platform_name: str, paths: AsicConfigPaths) -> None:
    """Generate ASIC configs for a single platform's variants."""
    platforms = discover_platforms(paths)

    if platform_name not in platforms:
        available = "\n".join(f"  - {p}" for p in sorted(platforms)) or "  (none)"
        raise ValueError(
            f"Unknown platform '{platform_name}'.\nAvailable platforms:\n{available}"
        )

    platform_config, output_dir = platforms[platform_name]
    _generate_platform(
        platform_name, platform_config, output_dir, paths, clean_output=True
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate asic_config_v3 ASIC config YAML."
    )
    parser.add_argument(
        "--fboss-root",
        type=str,
        required=True,
        help=(
            "Path to the fboss/ source directory itself, for example "
            "/path/to/fbcode/fboss."
        ),
    )
    parser.add_argument(
        "--platform",
        type=str,
        default=None,
        help=(
            "Generate only this platform (matches a platforms/<vendor>/<name> dir). "
            "Defaults to generating all discovered platforms."
        ),
    )
    args = parser.parse_args()
    paths = AsicConfigPaths.from_root(args.fboss_root)

    if args.platform:
        generate_single_platform(args.platform, paths)
    else:
        generate_all_asic_configs(paths)


if __name__ == "__main__":
    main()
