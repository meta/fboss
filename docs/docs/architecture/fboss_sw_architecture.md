# FBOSS SW Architecture

FBOSS (Facebook Open Switching System) is a software stack that runs on the x86 CPU of a network switch. It is designed to be modular, scalable, and flexible, allowing for easy integration with various hardware platforms. The FBOSS software architecture consists of multiple micro-services that work together to provide a comprehensive networking solution.

![FBOSS SW Architecture](/img/architecture/fboss_sw/fboss_sw_architecture.png)

## Components

1. **Platform Manager**
   Manages the low-level device setup and management. More info [here](https://meta.github.io/fboss/docs/platform/platform_manager/).

2. **Agent**
   Responsible for managing the forwarding switch ASIC / NPU.

3. **QSFP Service**
   Manages transceivers and external PHYs.

4. **LED Service**
   Controls the front panel port LEDs.

5. **Sensor Service**
   Collects data from various onboard (temperature, voltage, current, and power) sensors and exposes them to FSDB.

6. **Fan Service**
   Subscribes to sensor data from FSDB and programs Fan PWM to ensure system thermal health.

7. **Data Corral Service**
   Detects FRU presence and programs LEDs to indicate the presence.

## FBOSS Command Line Interfaces (CLIs)

1. **fboss2** and **fboss2-dev**
   Provides the ability to inspect and modify the state of the switch through show, set, clear options.

   `fboss2-dev` is the same as `fboss2` but additionally includes `config` subcommands which can be used to manage the switch configuration locally.

   See [FBOSS CLI](/docs/debugging/fboss_cli/) for more details.

2. **wedge_qsfp_util**
   Communicates with qsfp_service over Thrift to inspect and modify the state of transceivers. In the absence of qsfp_service, wedge_qsfp_util can directly communicate with transceivers using the BSP drivers.

3. **fw_util**
   Provides the ability to upgrade firmware of all the FPDs (Field Programmable Device), including BIOS, FPGA, CPLD, etc.

4. **weutil**
   Reads FRU EEPROMs, parses contents, and prints human-readable information, such as serial number, product name, etc.
