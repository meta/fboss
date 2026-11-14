# FBOSS Platform Testing Requirements

## Introduction

This FBOSS platform testing requirement is a minimal set of requirements that
vendors must perform before every software and hardware release. Vendors are
responsible for adding additional tests needed based on the platform design and
features, to ensure units shipped to Meta are ready for Meta internal platform
development.

## x86

### One month before shipping the Unit to Meta

- Represent the Platform with PlatformManager configuration and ensure it passes
  validation.

### Before every software/hardware release, run the following tests

#### Software Tests

- Run all the tests in the fboss/platform directory. These tests can be run on
  any host. They are important if there are code changes to the fboss/platform
  directory (in addition to configuration changes). Note: These tests will also
  run on github when the vendor raises a PR.

  Test cases can be found in this file, in “fboss_platform_services”, any tests
  does not end with “\_hw_test” :
  [https://github.com/meta/fboss/blob/main/CMakeLists.txt#L1002](https://github.com/meta/fboss/blob/main/CMakeLists.txt#L1002)

#### Hardware Tests

- These tests need to run on the actual HW platform. Note: These tests will not
  run on github PRs. Test cases can be found in this file, in
  “fboss_platform_services”, end with “\_hw_test” :
  [https://github.com/meta/fboss/blob/main/CMakeLists.txt#L1002](https://github.com/meta/fboss/blob/main/CMakeLists.txt#L1002)
  - platform_manager_hw_test

  - sensor_service_hw_test

  - fan_service_hw_test

  - data_corral_service_hw_test

  - weutil_hw_test

  - fw_util_hw_test

#### Other Tests

- These tests need to run on actual HW, but due to their nature are not part of any hw_test.

  - Run `i2cdetect` against every I2C bus visible to userspace and verify that no adverse effects occur. Ensure that all FBOSS
    services are able to run properly after this operation.

---

## BMC {#bmc}

|      Category      | Test Name                                    | Description                                                                                                                                                                                                                                                                                                   |
| :----------------: | -------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
|     **Basics**     | fbobmc.bmc.bootup                            | Power on the switch and make sure OpenBMC can boot up to the login prompt successfully without user interactions.                                                                                                                                                                                             |
|                    | fbobmc.uboot.version                         | Boot up OpenBMC and make sure u-boot version is Meta accepted                                                                                                                                                                                                                                                 |
|                    | fbobmc.kernel.version                        | Boot up OpenBMC and make sure kernel version is Meta accepted                                                                                                                                                                                                                                                 |
|                    | fbobmc.yocto.version                         | Boot up OpenBMC and make sure yocto version is lf-master by verifying ID=openbmc-fb-master in /etc/os-release                                                                                                                                                                                                 |
|                    | fbobmc.system.initializer                    | Boot up OpenBMC and make sure systemd is the system initializer (rather than sysV)                                                                                                                                                                                                                            |
|                    | fbobmc.reboot.bmc.wont.affect.userver.oob    | Reboot OpenBMC (by running "reboot") and make sure uServer is still reachable (pingable & SSHable) while OpenBMC is booting.                                                                                                                                                                                  |
|                    | fbobmc.reboot.bmc.wont.affect.userver.inband | Reboot OpenBMC (by running "reboot") and make sure uServer inband traffic is not affected while OpenBMC is booting.                                                                                                                                                                                           |
|                    | fbobmc.reboot.userver.wont.affect.openbmc    | Reboot uServer (by running "reboot" from uServer) and make sure OpenBMC is reachable (pingable & SSHable) when and after uServer is booting up.                                                                                                                                                               |
|                    | fbobmc.reboot.userver.bmc.in.parallel        | Reboot uServer and OpenBMC at the same time (by running "reboot" command) for at least 10 times: make sure OpenBMC and uServer OS can always boot up independently.                                                                                                                                           |
|                    | fbobmc.no.kernel.panic                       | Boot up OpenBMC and do not reboot OpenBMC for at least 7 days: make sure there is no kernel panic during the period.                                                                                                                                                                                          |
|                    | fbobmc.upgrade.primary.flash                 | Make sure "flash0" mtd partition is pointed to the primary flash, and OpenBMC's primary flash can be upgraded using "flashcp -v \<openbmc-image\> /dev/mtd\#" command successfully.                                                                                                                           |
|                    | fbobmc.upgrade.2nd.flash                     | Make sure "flash1" mtd partition is pointing to the 2nd flash, and OpenBMC's secondary flash can be upgraded using "flashcp -v \<openbmc-image\> /dev/mtd\#" command successfully.                                                                                                                            |
|                    | fbobmc.data0.partition                       | Boot up OpenBMC and make sure /mnt/data is mounted to UBIFS based flash data0 partition, and the data0 partition size is 64MB.                                                                                                                                                                                |
| **Serial Console** | fbobmc.bmc.console.access                    | Make sure OpenBMC u-boot, kernel and userspace console output can be accessed from the front-panel console port at baudrate 9600\.                                                                                                                                                                            |
|                    | fbobmc.userver.console.access                | Run "sol.sh" from OpenBMC and make sure uServer UEFI and Kernel serial logs can be accessed properly, and "ctrl+x" can quit uServer console successfully.                                                                                                                                                     |
|                    | fbobmc.mterm.service                         | Reboot uServer and make sure uServer UEFI and Kernel serial logs can be captured by OpenBMC and stored in /var/log/mTerm_wedge.log.                                                                                                                                                                           |
|    **Ethernet**    | fbobmc.eth0.consistent.mac.address           | Reboot OpenBMC for 10 times and make sure OpenBMC eth0's MAC address is consistent across reboots, and the MAC address matches the BMC MAC stored in chassis eeprom.                                                                                                                                          |
|                    | fbobmc.eth0.dhcp.ipv6.address                | Boot up OpenBMC and make sure OpenBMC can obtain global ipv6 address automatically after bootup.                                                                                                                                                                                                              |
|                    | fbobmc.eth0.reachable.from.outside           | Boot up OpenBMC and make sure OpenBMC eth0's global ipv6 address is pingable and sshable from outside the switch.                                                                                                                                                                                             |
|                    | fbobmc.eth0.global.ipv6.from.userver         | Make sure OpenBMC eth0's global ipv6 address is pingable and sshable from uServer OS.                                                                                                                                                                                                                         |
|                    | fbobmc.eth0.link.local.ipv6.from.userver     | Make sure OpenBMC eth0's link local ipv6 address is pingable and sshable from uServer OS.                                                                                                                                                                                                                     |
|                    | fbobmc.eth0.4088.reachable                   | Make sure OpenBMC eth0.4088 interface is pingable and sshable from uServer OS.                                                                                                                                                                                                                                |
|                    | fbobmc.eth0.dhcp.id                          | Make sure OpenBMC include the pre-defined vendor options in dhcpv6 request. Refer to [https://github.com/facebook/openbmc/blob/helium/common/recipes-core/systemd-networkd/files/10-eth0.network](https://github.com/facebook/openbmc/blob/helium/common/recipes-core/systemd-networkd/files/10-eth0.network) |
|                    | fbobmc.eth0.lldp_util                        | Make sure lldp-util in OpenBMC can detect the uplink/management switch                                                                                                                                                                                                                                        |
|                    | fbobmc.mdio.oob.switch.access                | Make sure MDIO registers are readable and writeable using oob-mdio-util.sh                                                                                                                                                                                                                                    |
| **EEPROM Access**  | fbobmc.weutil.chassis.eeprom                 | Run "weutil" from OpenBMC and make sure "weutil" can parse and print the Chassis EEPROM content properly.                                                                                                                                                                                                     |
|                    | fbobmc.weutil.chassis.eeprom.fields          | From output of weutil, all mandatory fields are populated: 5 fields (Product Name, Product Production State, Product Version, Product Subversion, Product Serial Number)                                                                                                                                      |
|                    | fbobmc.weutil.chassis.eeprom.crc             | From output of weutil, EEPROM checksum is calculated and matches EEPROM programmed checksum. The output should show that in the very last line e.g. CRC16: 0x33ce (CRC Matched)                                                                                                                               |
|                    | fbobmc.weutil.scm.eeprom                     | Run "weutil --eeprom scm_eeprom" or "weutil --path <path_for_scm>" from OpenBMC and make sure "weutil" can parse and print the scm EEPROM content properly.                                                                                                                                                   |
|                    | fbobmc.userver.mac.address                   | Check userver (x86) mac address from weutil (from SCM eeprom, or Chassis eeprom) output to make sure the MAC address is a valid one, not default or uninitialized, and match the output from wedge_us_mac.sh                                                                                                  |
|    **Watchdog**    | fbobmc.primary.watchdog.enabled.by.default   | Boot up OpenBMC and run "devmem 0x1e78500c": make sure both bit 0 and bit 1 are set.                                                                                                                                                                                                                          |
|                    | fbobmc.dual.boot.enabled.by.default          | Reboot OpenBMC and interrrupt u-boot: run "otp info strap" in u-boot command line and make sure 0x2b (OTPSTRAP\[43\] trap_en_bspiabr) is set to 1\.                                                                                                                                                           |
|                    | fbobmc.dual.boot.wdt.disabled.after.boot     | Boot up OpenBMC and run "devmem 0x1e620064": make sure bit 0 is 0 (means FMC_WDT2 watchdog is disabled).                                                                                                                                                                                                      |
|                    | fbobmc.force.boot.from.2nd.flash             | Login OpenBMC and run "boot_info.sh bmc reset slave": make sure OpenBMC boots from the 2nd flash at next bootup.                                                                                                                                                                                              |
|                    | fbobmc.primary.kernel.corrupted              | Corrupt the primary flash's "fit" partition and then reboot OpenBMC: make sure OpenBMC can boot from the 2nd flash successfully.                                                                                                                                                                              |
|                    | fbobmc.primary.uboot.corrupted               | Corrupt the primary flash's "u-boot" partition and then reboot OpenBMC: make sure OpenBMC can boot from the 2nd flash successfully.                                                                                                                                                                           |
|                    | fbobmc.restore.boot.order                    | Force OpenBMC to boot from the 2nd flash, and then run "boot_info.sh bmc reset master": make sure OpenBMC is booted from the primary flash at next bootup.                                                                                                                                                    |
| **Power Control**  | fbobmc.reset.userver                         | Login OpenBMC and run "wedge_power.sh reset": make sure userver is reset properly (uServer OS will reboot successfully after "wedge_power.sh reset").                                                                                                                                                         |
|                    | fbobmc.reset.userver.in.parallel             | Login OpenBMC and run "wedge_power.sh reset" from different terminals at the same time: make sure uServer is reset properly (uServer OS will reboot successfully after "wedge_power.sh reset").                                                                                                               |
|                    | fbobmc.reset.chassis                         | Login OpenBMC and run "[wedge_power.sh](http://wedge_power.sh/) reset -s": make sure both BMC and userver are reset successfully.                                                                                                                                                                             |
|      **IPMI**      | fbobmc.ipmid.service                         | Login OpenBMC and make sure ipmid.service is running properly (systemctl status ipmid)                                                                                                                                                                                                                        |
|                    | fbobmc.kcsd.service                          | Login OpenBMC and make sure kcsd@\[0-2\].service is running properly (systemctl status kcsd@0)                                                                                                                                                                                                                |
|                    | fbobmc.ipmi.mc.info.from.userver             | Login uServer and run "ipmitool mc info": make sure the command can return successfully.                                                                                                                                                                                                                      |
|                    | fbobmc.ipmi.sel.list.from.userver            | Login uServer and run "ipmitool sel list": make sure the command can return successfully.                                                                                                                                                                                                                     |
|                    | fbobmc.ipmi.sel.injection.from.userver       | Login uServer and make sure SEL entries can be added manually. For example, "ipmitool raw 0x0a 0x44 0x01 0x00 0x02 0xab 0xcd 0xef 0x00 0x01 0x00 0x04 0x01 0x17 0x00 0xa0 0x04 0x07 01 00" and then "ipmitool sel list".                                                                                      |
|  **Recover Path**  | fbobmc.bios.deselected.by.default            | Login OpenBMC and make sure the BIOS chip is not reachable (deselected) by default.                                                                                                                                                                                                                           |
|                    | fbobmc.recover.bios                          | Login OpenBMC and make sure the BIOS chip can be upgraded by "spi_util.sh" command successfully.                                                                                                                                                                                                              |
|                    | fbobmc.recover.bios.in.parallel              | Make sure only one instance of bios upgrade utility is allowed. Login OpenBMC and run "spi_util.sh" from multiple terminals at the same time: make sure the first instance can upgrade BIOS successfully, and all the following "spi_util.sh" instances must fail with "Error: another instance is running".  |
|      **Misc**      | fbobmc.ssh.to.userver.via.usb0               | Login OpenBMC and make sure people can ssh to the userver by running "ssh root@fe80::2%usb0".                                                                                                                                                                                                                 |
|                    | fbobmc.ssh.to.bmc.via.usb0                   | Login uServer and make sure people can ssh to the OpenBMC by running "ssh root@fe80::1%usb0"                                                                                                                                                                                                                  |
|                    | fbobmc.tpm                                   | Login OpenBMC and make sure "/dev/tpm0" device is created successfully.                                                                                                                                                                                                                                       |
|                    | fbobmc.restapi                               | Make sure OpenBMC is reachable via restapi. For example "curl [http://localhost:8080/api/sys/bmc](http://localhost:8080/api/sys/bmc)"                                                                                                                                                                         |

---

## BSP

Please refer to:
[https://meta.github.io/fboss/docs/testing/bsp_tests/](https://meta.github.io/fboss/docs/testing/bsp_tests/)

---

## Firmware {#firmware}

As we do not have an open-source firmware test repository to share with vendors
, we expect them to perform the basic tests outlined below by writing their own
scripts. Some of these tests will be manual in nature. This represents the
minimum firmware testing qualification that Meta expects vendors to meet before
delivering firmware binaries.

####

| Basic Tests                  | Details                                                                                                                                                |
| ---------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Firmware Upgrade             | Upgrade to the new binary                                                                                                                              |
| Firmware Downgrade           | Revert back to previous Binary                                                                                                                         |
| Power Cycle                  | Power cycle in between test to make sure the Box comes back after upgrade & Downgrade                                                                  |
| BMC OOB Reachability         | Verify that the OOB is ssh’able after upgrade                                                                                                          |
| x86 Reachability             | Verify X86 is ssh’able after upgrade                                                                                                                   |
| ASIC Detection               | Verify that ASIC can be detected on the PCI bus after upgrade                                                                                          |
| Memory & CPU Consumption     | Verify the Memory & CPU consumption is within acceptable threshold after FW Upgrade                                                                    |
| LED & Display Testing        | Check all LEDs, display, or indicators functions as expected (Blinking patterns, color changes)                                                        |
| Buttons                      | Test all buttons (push/switch) for correct and consistent response. Applicable for HW platforms with programmable (e.g. CPLD, FPGA) controlled buttons |
| FW version Readout           | Verify that version can be read after upgrade & downgrade and it meets the expected version                                                            |
| BIOS & CPU_CPLD Back up path | Test BIOS & CPU_CPLD OpenBMC Back-up path FW upgrade works as expected                                                                                 |

---

## Stress Test

The FBOSS Platform Software vendor stress test is to ensure the stability of
FBOSS platform software on the underlying HW that Meta team can have the
confidence the platform software passed the stress test requirements is ready
for each development phase exit.

### Stress Test Prerequisites

#### Platform SW stack stress test components on x86

1. Linux Kernel ready
2. Pass all BSP tests
3. Pass all platform SW tests
4. Pass all HW tests for all services and utils
   1. Platform Manager
   2. Fan Service
   3. Sensor Service
   4. Data Corral Service
   5. FW util
   6. Weutil
5. Pass all firmware tests in [Firmware](#firmware) section

#### SW stack stress test components on BMC

1. Pass All BMC tests in [BMC](#bmc) section

### Below are the stress test cases, all should pass without failure

| Category                            | Test Name                           |     Sub Name     | Details                                                                                                                                                                                                                                                                                                                                                                                                               |
| ----------------------------------- | ----------------------------------- | :--------------: | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **x86 BSP**                         | Kernel module load/unload           |                  | Run bsp kmod test for 500 times ([https://github.com/meta/fboss/tree/main/fboss/platform/bsp_tests](https://github.com/meta/fboss/tree/main/fboss/platform/bsp_tests))                                                                                                                                                                                                                                        |
|                                     | SPI Access                          |                  | Read and write each SPI device for 500 times                                                                                                                                                                                                                                                                                                                                                                          |
|                                     | FPGA                                | I2C Transactions | Read and Write I2C bus for each I2C device for 500 times                                                                                                                                                                                                                                                                                                                                                              |
|                                     | FPGA                                |       MDIO       | Read and write scratch pad register 100 times                                                                                                                                                                                                                                                                                                                                                                         |
| **x86 Platform Services and Utils** | Platform Services                   |                  | Run all platform services non-stop for 7 days without memory leak, crash and exception (Rackmon service is for TOR only)                                                                                                                                                                                                                                                                                              |
|                                     | Platform Utils                      |     fw_util      | Program (upgrade-\>reboot-\>downgrade-\>reboot) all firmware continually for 100 times without failure                                                                                                                                                                                                                                                                                                                |
|                                     | Platform Utils                      |      weutil      | Use weutil to get all the EEPROM info for 500 times without failure                                                                                                                                                                                                                                                                                                                                                   |
|                                     | Showtech                            |                  | Run `rma-showtech` continuously on the x86 (uServer) for 500 iterations back-to-back while the forwarding stack is running with live or simulated traffic. Each run must complete without failure, and inband traffic must not be affected during or after any run (no packet loss, no link flaps, no FBOSS service crashes). After the stress completes, both x86 (uServer) and OpenBMC must remain reachable (ping/ssh). |
| **BMC Platform**                    | Primary flash BMC Update            |                  | Perform primary flash OpenBMC upgrade/downgrade 500 times                                                                                                                                                                                                                                                                                                                                                             |
|                                     | Secondary flash BMC Update          |                  | Perform secondary flash OpenBMC upgrade/downgrade 500 times                                                                                                                                                                                                                                                                                                                                                           |
|                                     | I2C Transaction                     |                  | Read and Write I2C bus for each I2C device for 500 times                                                                                                                                                                                                                                                                                                                                                              |
|                                     | Reboot                              |    BMC Reboot    | Reboot OpenBMC 500 times with primary flash without issue, it should not boot into secondary flash. Reboot OpenBMC 500 times in secondary flash without issue, it should not boot into primary flash. and make sure: OpenBMC can always boot from the primary flash ("boot_info.sh bmc" can report boot source) OpenBMC bootup time must be within 5 minutes, including when data0 partition needs to be re-formatted |
|                                     | Reboot                              |    X86 Reboot    | Login OpenBMC and run "wedge_power.sh reset" for at least 500 times: make sure uServer is always reset properly (uServer OS will reboot successfully after "wedge_power.sh reset").                                                                                                                                                                                                                                   |
|                                     | IPMI Stress Test                    |                  | Use ipmitool to verify BMC system info for 500 times                                                                                                                                                                                                                                                                                                                                                                  |
|                                     | Primary/Secondary OpenBMC Boot Swap |                  | Boot swap test for 1000 times. E.g. boot from primary \-\> secondary \-\> primary, so on and so forth                                                                                                                                                                                                                                                                                                                 |
|                                     | BMC Recovery Path BIOS              |                  | Log into OpenBMC and try to upgrade/downgrade BIOS in OpenBMC for 500 times                                                                                                                                                                                                                                                                                                                                           |
|                                     | Showtech                            |                  | Run `show_tech.py` continuously on OpenBMC for 500 iterations back-to-back while the forwarding stack on the uServer is running with live or simulated traffic. Each run must complete without failure, and inband traffic must not be affected during or after any run (no packet loss, no link flaps, no FBOSS service crashes). After the stress completes, both x86 (uServer) and OpenBMC must remain reachable (ping/ssh). |
| **Whole System**                    | Power Cycle                         |                  | Log into OpenBMC and run "[wedge_power.sh](http://wedge_power.sh/) reset \-s" for 500 times, with x86 and OpenBMC kernels Meta required vendor to use, and the whole chassis (x86 and BMC) boot successfully each time                                                                                                                                                                                                |

####
