// Copyright (c) Meta Platforms, Inc. and affiliates.

namespace cpp2 facebook.fboss.platform.platform_manager
namespace hack NetengFbossPlatformManager
namespace py3 fboss.platform.platform_manager

// List of allowed PMUnit names that can be used in platform configurations.
// This ensures consistency across platform configs and prevents typos.
const list<string> ALLOWED_PMUNIT_NAMES = [
  // List of allowed standard PMUnit names
  "FAN_TRAY",
  "3V3_L",
  "3V3_R",
  "BCB",
  "BMC",
  "FAN",
  "FCB",
  "FCB_B",
  "FCB_T",
  "JUMPER",
  "MCB",
  "NETLAKE",
  "NETLAKE20",
  "PDB",
  "PDB_L",
  "PDB_R",
  "PEM",
  "PIC",
  "PIC_B",
  "PIC_T",
  "PIM_16Q",
  "PIM_8DD",
  "PSU",
  "RACKMON",
  "RUNBMC",
  "SCM",
  "SMB",
  "RTM_L",
  "RTM_R",
  "SMB_L",
  "SMB_R",
  // ======= EXCEPTIONS (should not be used in new platforms) ========
  // MINIPACK3 has unique naming in eeproms
  "MINIPACK3_3V3_L",
  "MINIPACK3_3V3_R",
  "MINIPACK3_BMC",
  "MINIPACK3_FCB_B",
  "MINIPACK3_FCB_T",
  "MINIPACK3_PDB_L",
  "MINIPACK3_PDB_R",
  "MINIPACK3_SCM",
  "MINIPACK3_SMB",
  // MINIPACK3M has unique naming in eeproms
  "MINIPACK3M_3V3_L",
  "MINIPACK3M_3V3_R",
  "MINIPACK3M_BMC",
  "MINIPACK3M_FCB_B",
  "MINIPACK3M_FCB_T",
  "MINIPACK3M_PDB_L",
  "MINIPACK3M_PDB_R",
  "MINIPACK3M_SCM",
  "MINIPACK3M_SMB",
  // The BIOS infers the PlatformName from MCB EEPROM in these platforms
  // This is for platforms reliant on NETLAKE BIOS.
  "ICECUBE_MCB",
  "ICECUBEM_MCB",
  "LADAKH800BCLS_MCB",
  "LADAKH800BCLSM_MCB",
  "LEH800BCLS_MCB",
  "LEH800BCLSM_MCB",
  "MINIPACK3_MCB",
  "MINIPACK3M_MCB",
  "MINIPACK3BA_MCB",
  "MINIPACK3BAM_MCB",
  "MINIPACK3N_MCB",
  "MINIPACK3NM_MCB",
  "TAHANSB800BC_MCB",
  "TAHANSB800BCM_MCB",
  // The whole board is a PmUnit for these
  "TAHAN",
  "JANGA",
  // Misc
  "MINERVA_BMC",
  "MORGAN800CC",
  "YOLO_MAX",
  "SMB_FRU",
  "PSU_2GH",
];

// List of platforms that are allowed to have chassisEepromDevicePath
// pointing to an IDPROM device. This is a legacy exception list.
// New platforms should NOT use IDPROM for chassisEepromDevicePath.
const list<string> PLATFORMS_WITH_IDPROM_CHASSIS_EEPROM = [
  "MERU800BFA",
  "MERU800BIA",
  "MORGAN800CC",
  "JANGA800BIC",
  "TAHAN800BC",
  "M4062NHP",
  "M4340NHP",
];

// List of platforms that are allowed to have logical EEPROMs.
// Logical EEPROMs are formed when multiple EEPROMs in the platform
// use the same physical EEPROM (bus and address).
const list<string> PLATFORMS_WITH_LOGICAL_EEPROMS = [
  "GLATH05A-64O",
  "BLACKWOLF800BANW",
];

// Platforms where optics devices (xcvr, port LEDs) must NOT be managed
// through platform_manager. On these platforms, qsfp_service and
// led_service interact with the SCD FPGA directly using I2C transactions.
// Managing them through platform_manager gives a false impression about
// their management. Once we start managing these devices from PM, we can
// add them to the PM config.
const list<string> PLATFORMS_WITHOUT_PM_OPTICS = ["DARWIN", "DARWIN48V"];
