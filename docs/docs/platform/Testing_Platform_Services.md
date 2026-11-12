---
id: testing_platform_services
title: Testing Platform Services
keywords:
  - FBOSS
  - OSS
  - build
  - docker
oncall: fboss_oss
---

We expect vendors to run all the following tests before shipping software or
hardware.

## Hardware Independent Tests:

- These tests can be run on any host.
- Any test which is not inside a `hw_test` directory is a hardware independent
  test.
- The tests are listed under cmake target `fboss_platform_services` in
  [CMakeLists.txt](https://github.com/meta/fboss/blob/main/CMakeLists.txt)
- These tests run as part of github actions on PRs.

## Hardware Dependent Tests:

- These tests need to be run on the specific network hardware.
- These tests are inside `hw_test` directory of individual services.
- The following are the current set of hardware tests.
  - platform_hw_test
  - data_corral_service_hw_test
  - fan_service_hw_test
  - sensor_service_hw_test
  - weutil_hw_test
  - platform_manager_hw_test
- All these tests can be run without any parameters.
- These tests do not run as part of github actions on PRs.
