// Copyright 2004-present Facebook. All Rights Reserved.

#include <fcntl.h>
#include <folly/init/Init.h>
#include <gflags/gflags.h>
#include <cstdio>
#include <string.h>
#include <sys/mman.h>
#include <sysexits.h>
#include <unistd.h>

/*
 * CLI:
 * nic_util --nic_read_reg --reg_addr <register-address> [--verbose]
 * nic_util --nic_write_reg --reg_addr <register-address> --reg_val <data-value>
 * [--verbose]
 */
DEFINE_bool(
    nic_read_reg,
    false,
    "Read register from NIC i210, use with --reg_addr");
DEFINE_int32(reg_addr, 0, "NIC i210 register address");
DEFINE_bool(
    nic_write_reg,
    false,
    "Write register from NIC i210, use with --reg_addr and --reg_val");
DEFINE_uint32(reg_val, 0, "NIC i210 register value");

/*
 * CLI:
 * nic_util --nic_phy_read_reg --phy_reg_addr <register-address> [--phy_page
 * <phy-page-id>] [--verbose] nic_util --nic_phy_write_reg --phy_reg_addr
 * <register-address>  --phy_reg_val <data-value> [--phy_page <phy-page-id>]
 * [--verbose]
 */
DEFINE_bool(
    nic_phy_read_reg,
    false,
    "Read register from NIC i210 PHY, use with --phy_reg_addr and --phy_page");
DEFINE_int32(phy_reg_addr, 0, "NIC i210 PHY register address");
DEFINE_bool(
    nic_phy_write_reg,
    false,
    "Write register from NIC i210 PHY, use with --phy_reg_addr, --phy_page and phy_reg_val");
DEFINE_uint32(phy_reg_val, 0, "NIC i210 PHY register value");
DEFINE_uint32(phy_page, -1, "NIC i210 PHY Page id");

/*
 * CLI:
 * nic_util --nic_link_show_config
 * nic_util --nic_link_show_status
 */
DEFINE_bool(nic_link_show_config, false, "Read link config from NIC i210");
DEFINE_bool(nic_link_show_status, false, "Read link status from NIC i210");

/*
 * CLI:
 * nic_util --nic_reset [--soft_reset | --phy_reset | --device_reset]
 */
DEFINE_bool(
    nic_reset,
    false,
    "Reset the NIC i210, use with --soft_reset, --phy_reset and --device_reset");
DEFINE_bool(soft_reset, false, "Software reset for NIC i210");
DEFINE_bool(phy_reset, false, "Internal PHY reset for NIC i210");
DEFINE_bool(device_reset, false, "Device reset for NIC i210");

/*
 * CLI:
 * Extra option: --verbose
 */
DEFINE_bool(verbose, false, "detailed prints");

/*
 * Intel NIC I210 register definition
 * For full list of register definition pl refer to I210 spec from Intel site:
 * https://www.intel.com/content/www/us/en/design/products-and-solutions/networking-and-io/ethernet-controller-i210-i211/technical-library.html
 */
constexpr uint16_t kDeviceControlRegister = 0x0000;
constexpr uint16_t kDeviceStatusRegister = 0x0008;
constexpr uint16_t kExtendedDeviceControlRegister = 0x0018;
constexpr uint16_t kMdiControlRegister = 0x0020;
constexpr uint16_t kCopperSwitchControlRegister = 0x0034;
constexpr uint16_t kPcsConfigurationRegister = 0x4200;
constexpr uint16_t kPcsLinkStatusRegister = 0x420c;

/*
 * map_info_s
 *
 * Structure to keep NIC i210 device address mapping to user address space
 */
struct nic_info_s {
  unsigned int mapSize;
  unsigned char* mapAddr;
};

bool verbosePrint = false;

/*
 * nicReadRegInternal
 *
 * Read the NIC i210 register. The register address is 16 bit value, the data
 * read is 32 bit value. These could be MAC, PCS, PCIe block registers.
 */
unsigned int nicReadRegInternal(struct nic_info_s* pNic, uint32_t regAddr) {
  unsigned char* ptr = pNic->mapAddr;
  if (regAddr >= pNic->mapSize) {
    return 0;
  }
  ptr += regAddr;
  return *(unsigned int*)ptr;
}

/*
 * nicWriteRegInternal
 *
 * Write the NIC i210 register. The register address is 16 bit value, the data
 * is 32 bit value. These could be MAC, PCS, PCIe block registers.
 */
void nicWriteRegInternal(
    struct nic_info_s* pNic,
    uint32_t regAddr,
    uint32_t regVal) {
  unsigned char* ptr = pNic->mapAddr;
  if (regAddr >= pNic->mapSize) {
    return;
  }
  ptr += regAddr;
  *(unsigned int*)ptr = regVal;
}

/*
 * nicShowReg
 *
 * Read and displays the NIC i210 register. The register address is 16 bit
 * value, the data read is 32 bit value.
 */
bool nicShowReg(struct nic_info_s* pNic, uint32_t regAddr) {
  unsigned char* ptr = pNic->mapAddr;
  if (verbosePrint) {
    printf("Map address: 0x%lx\n", (unsigned long)ptr);
  }
  unsigned int regVal = nicReadRegInternal(pNic, regAddr);
  printf("Reg 0x%x = 0x%.8x\n", regAddr, regVal);
  return true;
}

/*
 * nicWriteReg
 *
 * Writes the NIC i210 register. The register address is 16 bit value, the data
 * read is 32 bit value. These could be MAC, PCS, PCIe block registers.
 */
bool nicWriteReg(struct nic_info_s* pNic, uint32_t regAddr, uint32_t regVal) {
  unsigned char* ptr = pNic->mapAddr;
  if (verbosePrint) {
    printf("Map address: 0x%lx\n", (unsigned long)ptr);
  }
  nicWriteRegInternal(pNic, regAddr, regVal);
  regVal = nicReadRegInternal(pNic, regAddr);
  printf("Reg 0x%x = 0x%.8x\n", regAddr, regVal);
  return true;
}

/*
 * waitForMdioCompletion
 *
 * This function waits for the MDIO transaction completion and then returns
 * the MDIO control register word
 */
uint32_t waitForMdioCompletion(struct nic_info_s* pNic) {
  uint32_t mdioRegVal;
  uint32_t iteration = 0, maxIterations = 100;

  unsigned char* ptr = pNic->mapAddr + 0x20;

  // Keep reading MDIO control register till bit 28 (Ready Bit) becomes 1
  mdioRegVal = *(unsigned int*)ptr;
  while ((mdioRegVal & (1 << 28)) == 0 && (iteration < maxIterations)) {
    // printf("mdioRegVal 0x%x 0x%x\n", mdioRegVal, (mdioRegVal & (1<<28)));
    usleep(10000);
    mdioRegVal = *(unsigned int*)ptr;
    iteration++;
  }
  return mdioRegVal;
}

/*
 * nicPhyReadReg
 *
 * Reads the NIC i210 internal Phy register. The Phy register is internally
 * connected using MDIO. The phy registers are standard clause 22 page based
 * registers. The phy register address is 8 bit value, the data read is 16 bit
 * value. The 'phyPage' is optional parameter and if user does not tell the
 * phy page id then the read is done from existing page register.
 */
bool nicPhyReadReg(
    struct nic_info_s* pNic,
    uint32_t phyRegAddr,
    uint32_t phyPage) {
  uint32_t mdioRegVal;

  unsigned char* ptr = pNic->mapAddr;
  if (verbosePrint) {
    printf("Map address: 0x%lx\n", (unsigned long)ptr);
  }

  // NIC register 0x20 is MDIO control register
  ptr += kMdiControlRegister;

  // If phy_page is specified then first set the page id in phy register 22
  if (phyPage >= 0) {
    // OP (27..26): 1 (MDIO write)
    // PhyReg (20..16): 22 (page id)
    // DataVal (15..0): phyPage
    mdioRegVal = (1 << 26) | (22 << 16) | phyPage;
    // Write MDIO control register
    *(unsigned int*)ptr = mdioRegVal;

    // Keep reading MDIO control register till bit 28 (Ready Bit) becomes 1
    mdioRegVal = waitForMdioCompletion(pNic);
  }

  // Now read the actual PHY rgister

  // OP (27..26): 2 (MDIO read)
  // PhyReg (20..16): phyRegAddr
  mdioRegVal = (2 << 26) | (phyRegAddr << 16);
  // Write MDIO control register
  *(unsigned int*)ptr = mdioRegVal;

  // Keep reading MDIO control register till bit 28 (Ready Bit) becomes 1
  mdioRegVal = waitForMdioCompletion(pNic);

  printf(
      "PHY Reg 0x%x = 0x%.4x\n",
      (mdioRegVal >> 16) & 0x1f,
      mdioRegVal & 0xffff);
  return true;
}

/*
 * nicPhyWriteReg
 *
 * Writes the NIC i210 internal Phy register. The Phy register is internally
 * connected using MDIO. The phy registers are standard clause 22 page based
 * registers. The phy register address is 8 bit value, the data read is 16 bit
 * value. The 'phyPage' is optional parameter and if user does not tell the
 * phy page id then the read is done from existing page register.
 */
bool nicPhyWriteReg(
    struct nic_info_s* pNic,
    uint32_t phyRegAddr,
    uint32_t phyRegVal,
    uint32_t phyPage) {
  uint32_t mdioRegVal;
  uint32_t iteration = 0, maxIterations = 100;

  unsigned char* ptr = pNic->mapAddr;
  if (verbosePrint) {
    printf("Map address: 0x%lx\n", (unsigned long)ptr);
  }

  // NIC register 0x20 is MDIO control register
  ptr += kMdiControlRegister;

  // If phy_page is specified then first set the page id in phy register 22
  if (phyPage >= 0) {
    // OP (27..26): 1 (MDIO write)
    // PhyReg (20..16): 22 (page id)
    // DataVal (15..0): phyPage
    mdioRegVal = (1 << 26) | (22 << 16) | phyPage;
    // Write MDIO control register
    *(unsigned int*)ptr = mdioRegVal;

    // Keep reading MDIO control register till bit 28 (Ready Bit) becomes 1
    mdioRegVal = waitForMdioCompletion(pNic);
  }

  // Now write the actual PHY rgister

  // OP (27..26): 1 (MDIO write)
  // PhyReg (20..16): phyRegAddr
  // DataVal (15..0): phyRegVal
  mdioRegVal = (1 << 26) | (phyRegAddr << 16) | phyRegVal;
  *(unsigned int*)ptr = mdioRegVal;

  // Keep reading MDIO control register till bit 28 (Ready Bit) becomes 1
  mdioRegVal = waitForMdioCompletion(pNic);
  while ((mdioRegVal & (1 << 28)) == 0 && (iteration < maxIterations)) {
    usleep(10000);
    mdioRegVal = *(unsigned int*)ptr;
    iteration++;
  }

  printf(
      "PHY Reg 0x%x = 0x%.4x\n",
      (mdioRegVal >> 16) & 0x1f,
      mdioRegVal & 0xffff);
  return true;
}

/*
 * nicLinkShowConfig
 *
 * Shows the NIC i210 link configuration from these blocks:
 *  MAC block
 *  PCS block
 *  Internal PHY block
 */
bool nicLinkShowConfig(struct nic_info_s* pNic) {
  uint32_t regVal, mdioRegVal;
  std::string speedString[] = {"10 Mbps", "100 Mbps", "1 Gbps", "None"};
  std::string linkMode[] = {
      "Internal PHY (1000-BASE-T)", "1000-Base-KX", "SGMII", "Serdes"};

  unsigned char* ptr = pNic->mapAddr;
  printf("Map address: 0x%lx\n", (unsigned long)ptr);
  printf("i210 MAC Config:\n");
  // Read Device Control Register
  regVal = nicReadRegInternal(pNic, kDeviceControlRegister);
  printf("  Set Link Up:         %s\n", (regVal & 0x40) ? "Up" : "Down");
  printf(
      "  Link speed:          %s\n",
      speedString[((regVal >> 8) & 0x3)].c_str());
  printf("  Duplex:              %s\n", (regVal & 0x1) ? "Full" : "Half");
  // Read Extended Device Control Register
  regVal = nicReadRegInternal(pNic, kExtendedDeviceControlRegister);
  printf(
      "  Link Mode:           %s\n", linkMode[((regVal >> 22) & 0x3)].c_str());
  printf("  Driver Loaded:       %s\n", (regVal & 0x10000000) ? "Yes" : "No");

  printf("i210 PCS Config:\n");
  // Read PCS Configuration register
  regVal = nicReadRegInternal(pNic, kPcsConfigurationRegister);
  printf(
      "  PCS Enabled:         %s\n", (regVal & 0x8) ? "Enabled" : "Disabled");

  printf("i210 PHY Config:\n");

  // Set Phy page 0
  mdioRegVal = (1 << 26) | (22 << 16);
  nicWriteRegInternal(pNic, kMdiControlRegister, mdioRegVal);
  mdioRegVal = waitForMdioCompletion(pNic);

  // Read reg 0
  mdioRegVal = (2 << 26) | (0 << 16);
  nicWriteRegInternal(pNic, kMdiControlRegister, mdioRegVal);
  mdioRegVal = waitForMdioCompletion(pNic);

  printf("  Power Down:          %s\n", (mdioRegVal & 0x800) ? "Yes" : "No");
  printf("  AN Enabled:          %s\n", (mdioRegVal & 0x1000) ? "Yes" : "No");
  regVal = ((mdioRegVal >> 5) & 0x2) | ((mdioRegVal >> 13) & 0x1);
  printf("  Speed:               %s\n", speedString[regVal].c_str());
  printf("  Duplex:              %s\n", (mdioRegVal & 0x100) ? "Full" : "Half");

  return true;
}

/*
 * nicLinkShowStatus
 *
 * Shows the NIC i210 link status from these blocks:
 *  MAC block
 *  PCS block
 *  Internal PHY block
 */
bool nicLinkShowStatus(struct nic_info_s* pNic) {
  uint32_t regVal, mdioRegVal;
  std::string speedString[] = {"10 Mbps", "100 Mbps", "1 Gbps", "None"};

  unsigned char* ptr = pNic->mapAddr;
  printf("Map address: 0x%lx\n", (unsigned long)ptr);
  printf("i210 MAC Status:\n");
  // Read Device Status Register
  regVal = nicReadRegInternal(pNic, kDeviceStatusRegister);
  printf("  Link:                %s\n", (regVal & 0x2) ? "Up" : "Down");
  printf(
      "  Link speed:          %s\n",
      speedString[((regVal >> 6) & 0x3)].c_str());
  printf("  Duplex:              %s\n", (regVal & 0x1) ? "Full" : "Half");

  // Read Copper switch control register
  regVal = nicReadRegInternal(pNic, kCopperSwitchControlRegister);
  printf(
      "  PHY Signal:          %s\n",
      (regVal & 0x400) ? "Detected" : "Not detected");
  printf(
      "  Serdes Signal:       %s\n",
      (regVal & 0x200) ? "Detected" : "Not detected");
  printf("  Phy power:           %s\n", (regVal & 0x800) ? "Down" : "Up");

  printf("i210 PCS Status:\n");
  // Read PCS status register
  regVal = nicReadRegInternal(pNic, kPcsLinkStatusRegister);
  printf("  PCS Link:            %s\n", (regVal & 0x1) ? "Up" : "Down");
  printf(
      "  Link speed:          %s\n",
      speedString[((regVal >> 1) & 0x3)].c_str());
  printf("  Duplex:              %s\n", (regVal & 0x8) ? "Full" : "Half");

  printf("i210 PHY Status:\n");

  // Set Phy page 0
  mdioRegVal = (1 << 26) | (22 << 16);
  nicWriteRegInternal(pNic, kMdiControlRegister, mdioRegVal);
  mdioRegVal = waitForMdioCompletion(pNic);

  // Read reg 1
  mdioRegVal = (2 << 26) | (0x1 << 16);
  nicWriteRegInternal(pNic, kMdiControlRegister, mdioRegVal);
  mdioRegVal = waitForMdioCompletion(pNic);
  printf("  Link:                %s\n", (mdioRegVal & 0x4) ? "Up" : "Down");
  printf(
      "  AN Status:           %s\n",
      (mdioRegVal & 0x20) ? "Completed" : "Incomplete");

  // Read reg 17
  mdioRegVal = (2 << 26) | (0x11 << 16);
  nicWriteRegInternal(pNic, kMdiControlRegister, mdioRegVal);
  mdioRegVal = waitForMdioCompletion(pNic);
  printf(
      "  Speed:               %s\n",
      speedString[((mdioRegVal >> 14) & 0x3)].c_str());
  printf(
      "  Duplex:              %s\n", (mdioRegVal & 0x2000) ? "Full" : "Half");
  printf(
      "  Link Energy:         %s\n",
      (mdioRegVal & 0x10) ? "Not detected" : "Detected");

  return true;
}

/*
 * nicReset
 *
 * Does the NIC I210 block reset for the following components:
 *  Soft reset
 *  Internal PHY reset
 *  Device reset
 */
bool nicReset(
    struct nic_info_s* pNic,
    bool softReset,
    bool phyReset,
    bool deviceReset) {
  uint32_t regVal;
  unsigned char* ptr = pNic->mapAddr;

  printf("Map address: 0x%lx\n", (unsigned long)ptr);

  // Read Device Control Register and do soft reset
  regVal = nicReadRegInternal(pNic, kDeviceControlRegister);
  if (softReset) {
    // Soft reset bit 26 is self clear
    regVal |= (1 << 26);
    nicWriteRegInternal(pNic, kDeviceControlRegister, regVal);
    printf("NIC: Soft Reset done\n");
  }
  usleep(1000000);

  // Read Device Control Register and do PHY reset
  regVal = nicReadRegInternal(pNic, kDeviceControlRegister);
  if (phyReset) {
    // Phy reset bit 31 is not self clear
    regVal |= (1 << 31);
    nicWriteRegInternal(pNic, kDeviceControlRegister, regVal);
    usleep(1000000);
    regVal &= ~(1 << 31);
    nicWriteRegInternal(pNic, kDeviceControlRegister, regVal);
    printf("NIC: Internal PHY Reset done\n");
  }
  usleep(1000000);

  // Read Device Control Register and do Device reset
  regVal = nicReadRegInternal(pNic, kDeviceControlRegister);
  if (deviceReset) {
    // Device reset bit 29 is self clear
    regVal |= (1 << 29);
    nicWriteRegInternal(pNic, kDeviceControlRegister, regVal);
    printf("NIC: Device Reset done\n");
  }
  usleep(1000000);

  return true;
}

/*
 * This utility program provides the access CLI for Intel NIC i210
 * which is used in most of the FBOSS platform. This NIC provides
 * eth0 management interface in our platform on uServer side.
 * This CLI is supported for all platforms where i210 NIC is
 * there. It picks up the PCI configuration of NIC and then does
 * read/write to its registers: MAC, PCS, PHY. The MAC, PCS are
 * direct access whereas internal PHY registers are accessed via
 * internal MDIO bus.
 */
int main(int argc, char* argv[]) {
  const folly::Init init(&argc, &argv, true);
  gflags::SetCommandLineOptionWithMode(
      "minloglevel", "0", gflags::SET_FLAGS_DEFAULT);

  char sysfsPath[256];
  char pciId[32];

  // Find the PCI bus:slot:function for the Intel NIC i210
  // For Intel NIC i210:
  //   vendor id = 8086 (Intel)
  //   device id = 1533 (i210)
  FILE* fp = popen("lspci -d 8086:1533", "r");
  if (!fp) {
    printf("NIC i210 not found on PCI bus\n");
    return EX_SOFTWARE;
  }

  // Build the sysfs file path for this NIC device resource0
  // where all registers are mapped
  if (fscanf(fp, "%31s", pciId) != 1) {
    printf("Unable to read NIC i210 PCI id\n");
    pclose(fp);
    return EX_SOFTWARE;
  }
  int const nChars = snprintf(
      sysfsPath,
      sizeof(sysfsPath),
      "/sys/bus/pci/devices/0000:%s/resource0",
      pciId);
  if (nChars < 0 || static_cast<size_t>(nChars) >= sizeof(sysfsPath)) {
    printf("NIC i210 sysfs path is too long\n");
    pclose(fp);
    return EX_SOFTWARE;
  }
  pclose(fp);

  // Open the sysfs file to access the device
  int fd = open(sysfsPath, O_RDWR | O_SYNC);
  if (fd == -1) {
    printf("Unable to open up the sysfs file %s\n", sysfsPath);
    return EX_SOFTWARE;
  }

  // Mmeory map the device's 64KB memory register space to our
  // user space
  struct nic_info_s nicInfo;
  nicInfo.mapSize = 0x10000;
  nicInfo.mapAddr = (unsigned char*)mmap(
      nullptr, 0x10000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (nicInfo.mapAddr == MAP_FAILED) {
    printf("mmap failed for resource\n");
    close(fd);
    return EX_SOFTWARE;
  }

  // CLI: --verbose option
  if (FLAGS_verbose) {
    verbosePrint = true;
  }

  // CLI: nic_util --nic_read_reg --reg_addr <register-address>
  if (FLAGS_nic_read_reg) {
    printf("Calling nicShowReg with address 0x%x\n", FLAGS_reg_addr);
    nicShowReg(&nicInfo, FLAGS_reg_addr);
  }

  // CLI: nic_util --nic_write_reg --reg_addr <register-address>
  //               --reg_val <data-value>
  if (FLAGS_nic_write_reg) {
    printf(
        "Calling nicWriteReg with address 0x%x value 0x%x\n",
        FLAGS_reg_addr,
        FLAGS_reg_val);
    nicWriteReg(&nicInfo, FLAGS_reg_addr, FLAGS_reg_val);
  }

  // CLI: nic_util --nic_phy_read_reg --phy_reg_addr <register-address>
  //              [--phy_page <phy-page-id>]
  if (FLAGS_nic_phy_read_reg) {
    if (FLAGS_phy_page >= 0) {
      printf(
          "Calling nicPhyReadReg with address 0x%x (page %d)\n",
          FLAGS_phy_reg_addr,
          FLAGS_phy_page);
    } else {
      printf("Calling nicPhyReadReg with address 0x%x\n", FLAGS_phy_reg_addr);
    }
    nicPhyReadReg(&nicInfo, FLAGS_phy_reg_addr, FLAGS_phy_page);
  }

  // CLI: nic_util --nic_phy_write_reg --phy_reg_addr <register-address>
  //               --phy_reg_val <data-value> [--phy_page <phy-page-id>]
  if (FLAGS_nic_phy_write_reg) {
    if (FLAGS_phy_page >= 0) {
      printf(
          "Calling nicPhyWriteReg with address 0x%x value 0x%x (page %d)\n",
          FLAGS_phy_reg_addr,
          FLAGS_phy_reg_val,
          FLAGS_phy_page);
    } else {
      printf(
          "Calling nicPhyWriteReg with address 0x%x value 0x%x\n",
          FLAGS_phy_reg_addr,
          FLAGS_phy_reg_val);
    }
    nicPhyWriteReg(
        &nicInfo, FLAGS_phy_reg_addr, FLAGS_phy_reg_val, FLAGS_phy_page);
  }

  // CLI: nic_util --nic_link_show_config
  if (FLAGS_nic_link_show_config) {
    printf("Calling nicLinkConfig to get link configuration\n");
    nicLinkShowConfig(&nicInfo);
  }

  // CLI: nic_util --nic_link_show_status
  if (FLAGS_nic_link_show_status) {
    printf("Calling nicLinkStatus to get link status\n");
    nicLinkShowStatus(&nicInfo);
  }

  // CLI: nic_util --nic_reset [--soft_reset | --phy_reset | --device_reset]
  if (FLAGS_nic_reset) {
    printf("Calling nicReset to reset the NIC I210\n");
    nicReset(&nicInfo, FLAGS_soft_reset, FLAGS_phy_reset, FLAGS_device_reset);
  }

  munmap(nicInfo.mapAddr, 0x10000);
  close(fd);
  return EX_OK;
}
