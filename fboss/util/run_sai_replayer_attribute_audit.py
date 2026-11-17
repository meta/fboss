#!/usr/bin/env python3

# pyre-unsafe

# Run as python3 fboss/util/run_sai_replayer_audit.py
#
#
# This script looks for SAI attributes that are currently not logged by Sai Replayer.
# It works as follows:
#   - find all attributes defined in *Api.h
#   - match them with attributes logged by Sai Replayer.
#   - If not, flag and printout the diff.

import subprocess
import sys

API_FILES_TO_SEARCH = "fboss/agent/hw/sai/api/*Api.h"
TRACER_FILES_TO_SEARCH = "fboss/agent/hw/sai/tracer/*ApiTracer.cpp"

# There are certain calls replayer don't wrap (e.g. RxPacket as it's callback from SDK).
# Exclude these patterns when we audit the attributes.
PATTERN_TO_EXCLUDE = [
    "RxPacket",
    # sai api only used to get counters
    "SwitchPipeline",
]


def get_used_attributes():
    grep_cmd = f"grep SAI_ATTRIBUTE_NAME {API_FILES_TO_SEARCH}"
    grep_output = (
        subprocess.run(grep_cmd, shell=True, stdout=subprocess.PIPE)
        .stdout.decode("utf-8")
        .split("\n")[:-1]
    )
    used_attributes = set()
    for line in grep_output:
        if not any(exclude_pattern in line for exclude_pattern in PATTERN_TO_EXCLUDE):
            used_attributes.add(line.split("(")[1].split(")")[0])
    return used_attributes


def get_logged_attributes():
    import glob
    import re

    logged_attributes = set()

    # Read all tracer files and parse them properly to handle multi-line macros
    for tracer_file in glob.glob(TRACER_FILES_TO_SEARCH):
        with open(tracer_file, "r") as f:
            content = f.read()

        # Find all SAI_ATTR_MAP macros (handles multi-line)
        # Pattern: SAI_ATTR_MAP(Type, Attribute) or SAI_EXT_ATTR_MAP(Type, Attribute)
        attr_map_pattern = r"SAI_(?:EXT_)?ATTR_MAP\s*\(\s*([^,)]+)\s*,\s*([^)]+)\s*\)"
        for match in re.finditer(attr_map_pattern, content, re.MULTILINE | re.DOTALL):
            type_name = match.group(1).strip()
            attr_name = match.group(2).strip()
            logged_attributes.add(f"{type_name}, {attr_name}")

        # Find all SAI_EXT_ATTR_MAP_2 macros (handles multi-line)
        # Pattern: SAI_EXT_ATTR_MAP_2(MapType, DataType, Attribute)
        # We want the last two parameters (DataType, Attribute)
        ext_attr_map_2_pattern = (
            r"SAI_EXT_ATTR_MAP_2\s*\(\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^)]+)\s*\)"
        )
        for match in re.finditer(
            ext_attr_map_2_pattern, content, re.MULTILINE | re.DOTALL
        ):
            data_type = match.group(2).strip()
            attribute = match.group(3).strip()
            logged_attributes.add(f"{data_type}, {attribute}")

    return logged_attributes


def main() -> None:
    used_attributes = get_used_attributes()
    logged_attributes = get_logged_attributes()
    diff = used_attributes - logged_attributes
    if len(diff):
        print("Attributes missing from Sai Replayer:")
        for attr in sorted(diff):
            print(f"    {attr}")
        sys.exit(1)
    else:
        print("No missing attribute from Sai Replayer")
        sys.exit(0)


if __name__ == "__main__":
    main()
