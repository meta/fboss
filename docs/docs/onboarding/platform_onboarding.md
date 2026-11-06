---
id: platform_onboarding
title: New Platform Onboarding
description: How to onboard a new platform to FBOSS using OSS
keywords:
    - FBOSS
    - OSS
    - platform
    - onboard
oncall: fboss_oss
---
# New Platform Onboarding

## Overview

This document describes the steps involved in onboarding a new platform to FBOSS using OSS.
For common problems and their solutions, please refer to the [troubleshooting guide](/docs/onboarding/troubleshooting/).

## Preparation Steps
1. Request access of [FBOSS OSS New Platform Onboarding Tracker Template](https://docs.google.com/spreadsheets/d/1jZiAA9cBWnQml0frf4mC5cnWqsJRL4EK7w3qiYwl5Fo/edit?usp=sharing)
2. Make a copy of this template for your new platform
3. Fill in project details in the Tracker copy and share the file to External Vendor/Meta Workplace Group
4. Regularly update the project tracker to cover new platform development/testing progress

## Steps

1. [[Manual](/docs/manuals/add_initial_support_for_platform_services/)] **Add Initial Support for Platform Services**

   **Work Items:**

   - [ ] Add platform manager support
   - [ ] Add BSP support
   - [ ] Provide the firmware onboarding spec (`spec.json`)
   - [ ] Deliver the platform firmware binaries to the BSP repository
   - [ ] Build FBOSS Platform Stack
   - [ ] Ensure PlatformManager can start successfully
   - [ ] Run HW tests for platform services

   **Outcomes:**

   - [ ] `platform_manager.json` is committed to the FBOSS repository
   - [ ] `platform_manager.json` abides by the specification
   - [ ] `spec.json` is committed to `fboss/platform/firmware_onboarding/configs/<platform>/` in the FBOSS repository
   - [ ] `spec.json` abides by the [platform spec schema](https://github.com/meta/fboss/blob/main/fboss/platform/firmware_onboarding/references/vendor-spec-schema.md), using [configs/icecube800bc/spec.json](https://github.com/meta/fboss/blob/main/fboss/platform/firmware_onboarding/configs/icecube800bc/spec.json) as the worked example
   - [ ] `spec.json` lists every firmware-bearing component on the platform, with its location, field-replaceability and upgrade priority
   - [ ] `spec.json` passes the [pre-flight validator](https://github.com/meta/fboss/blob/main/fboss/platform/firmware_onboarding/README.md) with no errors
   - [ ] The BSP repository is setup
   - [ ] The BSP repository is accessible by Meta and linked in the summary page of the onboarding tracker
   - [ ] The BSP abides by the API specification and development requirements
   - [ ] The BSP is compiled as out-of-tree kernel modules and deployed in FBOSS to unblock FBOSS development
   - [ ] The platform firmware binaries are delivered to the BSP repository at `https://github.com/facebookexternal/fboss.bsp.<vendor_name>/<platform_name>/firmware/`
   - [ ] The firmware package abides by the [Firmware Repository Specification](https://github.com/meta/fboss/blob/main/fboss/docs/firmware_deliverables_requirements.md) — directory layout, at least `package_0` and `package_1`, the `P_<Platform>_F_<fw_target_name>_V_<Version>.<extension>` naming convention, `README.md` and `testsPlanPerformed.md`
   - [ ] Each firmware file's `<fw_target_name>` matches the target name in `fw_util.json` and the corresponding component name in `spec.json`, and its `<Version>` matches what `fw_util` reads back from the device
   - [ ] `bsp_tests.json` is modified as needed based on guidance in `bsp_tests_config.thrift`
   - [ ] 100% of BSP tests pass
   - [ ] Expected errors are defined in the test config if and only if the hardware has a valid reason and this is agreed to by Meta
   - [ ] Platform services can be built successfully
   - [ ] Platform manager can start successfully
   - [ ] 100% of platform service hardware tests pass

2. **Add Necessary Development Code**

   **Work Items:**

   - [ ] [Add Platform Mapping Config](/docs/developing/platform_mapping/)
   - [ ] [Add BSP Mapping Config](/docs/developing/bsp_mapping/)
   - [ ] [Add All Platform Support Code in Agent and Qsfp Service](/docs/developing/new_platform_support/)
   - [ ] [Support and Run LED HW Tests](/docs/testing/led_service_test/)
   - [ ] Onboard NPU [Optional depending on new platform needs]
   - [ ] Onboard New Transceiver/Phy [Optional depending on new platform needs]
   - [ ] Build FBOSS Forwarding Stack

   **Outcomes:**

   - [ ] Provide vendor platform mapping csv files and submit to FBOSS OSS repo
   - [ ] Generate platform mapping json files and corresponding cpp files
   - [ ] Generate bsp config json files and corresonding cpp files
   - [ ] Generate all necessary FBOSS Agent cpp files
   - [ ] Generate all necessary FBOSS Qsfp cpp files
   - [ ] 100% Pass LED HW tests
   - [ ] Agent binary can be built linking to the appropriate vendor SDK
   - [ ] Agent HW tests binary can be built linking to the appropriate vendor SDK
   - [ ] SAI tests binary can be built linking to the appropriate vendor SDK
   - [ ] QSFP Service binary can be built

3. **Validate FBOSS Agent Changes**

   **Work Items:**

   - [ ] Generate Agent HW test config
   - [ ] Pass [T0 Agent tests](/docs/testing/test_categories/#agent-hw-tests) and [T0 SAI tests](/docs/testing/test_categories/#sai-tests)

   **Outcomes:**

   - [ ] Generate Agent config for testing
   - [ ] 100% Pass T0 Agent HW tests (include multi-switch binary)
   - [ ] 100% Pass T0 SAI tests

4. **Validate Transceiver/Phy Changes**

   **Work Items:**

   - [ ] Build QSFP HW test binaries
   - [ ] [Build a QSFP/Link test setup with 100% port coverage](/docs/testing/qsfp_and_link_test_topology/)
   - [ ] [Generate QSFP HW Tests Config](/docs/developing/qsfp_test_config/)
   - [ ] Pass [T0 QSFP tests](/docs/testing/test_categories/#qsfp-hw-tests)

   **Outcomes:**

   - [ ] QSFP HW tests binary can be built
   - [ ] Vendor should use Meta recommended transceivers to setup 100% port coverage test setup
   - [ ] Generate QSFP config for testing
   - [ ] 100% Pass T0 QSFP tests

5. **Validate FBOSS Forwarding Stack**

   **Work Items:**

   - [ ] Build FBOSS Link Test binary
   - [ ] Generate Link Test config - (Currently Meta will provide a link test config)
   - [ ] Pass [T0 Link tests](/docs/testing/test_categories/#link-tests)
   - [ ] [Build a Ping test setup](/docs/manuals/perform_a_ping_test/)
   - [ ] Pass ping tests

   **Outcomes:**

   - [ ] Link Test binary can be built linking to the appropriate vendor SDK
   - [ ] Work with Meta to generate the Link Test config based on the 100% port coverage test setup
   - [ ] 100% Pass T0 Link tests
   - [ ] Successfully use systemd to bringup FBOSS Agent and QSFP Service on two connected switches
   - [ ] The two switches are able to ping each other and fboss can learn arp/ndp

6. **Achieve 100% Test Pass Rate**

   **Work Items:**

   - [ ] Stabilize remaining tests

   **Outcomes:**

   - [ ] 100% Pass Agent HW tests
   - [ ] 100% Pass Agent SAI tests
   - [ ] 100% Pass Agent Benchmark tests
   - [ ] 100% Pass QSFP HW tests
   - [ ] 100% Pass Link Tests
   - [ ] 100% Pass Data Corral Service HW tests
   - [ ] 100% Pass Fan Service HW tests
   - [ ] 100% Pass Sensor Service HW tests
   - [ ] 100% Pass Weutil HW tests
   - [ ] 100% Pass Platform Manager HW tests
   - [ ] 100% Pass FW Util HW tests
