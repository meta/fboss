# QSFP Service Development

## Overview

This document describes the changes required to add support for a new platform or a new optic.

## Platform Development

### Step 1: Add Platform Mapping

This step is common for agent, `qsfp_service`, and `led_service` development. Platform Mapping describes the internals of a platform. It can be added by following the instructions defined in the [Platform Mapping Documentation](https://meta.github.io/fboss/docs/developing/platform_mapping/).

Note that for the purpose of `qsfp_service` development or `qsfp_hw_test`, the relevant parts in platform mapping are the number of ports and the number of transceivers in the system. The rest of the sections can be initially mocked up to unblock `qsfp_hw_test` on a platform.

### Step 2: Add BSP Platform Mapping

This step is common for `qsfp_service` and `led_service` development. BSP Platform Mapping defines the sysfs paths to control the transceiver (and their respective LEDs in `led_service`). It can be added by following the instructions defined in the [BSP Platform Mapping Documentation](https://meta.github.io/fboss/docs/developing/bsp_mapping/).

### Step 3: Add Per Platform Support

QSFP Service utilizes multiple per-platform classes to select the corresponding platform's Platform Mapping and BSP Platform Mapping.

The type of platform the `qsfp_service`/`qsfp_hw_test` is running on is defined by the `fruid.json` file located at `/var/facebook/fboss/fruid.json`. Alternatively, the platform type can be overridden by the “mode” argument in the `defaultCommandLineArgs` parameter in the QSFP config.

Please refer to the "Common Code Changes" and "Qsfp Service Code Changes" in [FBOSS New Platform Support](https://meta.github.io/fboss/docs/developing/new_platform_support/).

## Optics Development

It is currently not straightforward to add support for a new optic. We are in the process of making it seamless.
