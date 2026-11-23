// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <fcntl.h>
#include <folly/Conv.h>
#include <folly/String.h>
#include <folly/init/Init.h>
#include <gflags/gflags.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sysexits.h>
#include <unistd.h>
#include <sstream>
#include <string>

DEFINE_bool(
    read_reg,
    false,
    "Read register from device, use with --offset [--length]");
DEFINE_int32(offset, 0, "Device register address");
DEFINE_int32(length, 1, "Number of registers to read");
DEFINE_bool(
    write_reg,
    false,
    "Write register from device, use with --offset --data");
DEFINE_uint32(data, -1, "Register data for write");
DEFINE_bool(qsfp_info, false, "Display QSFP related info");

struct scd_info_s {
  unsigned int mapSize;
  unsigned char* mapAddr;
};

constexpr uint32_t kQsfpRegAddr = 0xA010;
constexpr uint32_t kMaxQsfpModules = 32;

/*
 * scdReadRegInternal
 *
 * Read the SCD/SAT FPGA register. The register address is 16 bit value, the
 * data read is 32 bit value. The register could be in SCD or SAT FPGA
 */
unsigned int scdReadRegInternal(struct scd_info_s* pMem, uint32_t regAddr) {
  unsigned char* ptr = pMem->mapAddr;
  if (regAddr >= pMem->mapSize) {
    // Reg offset should not be more than map size
    throw std::runtime_error("Register offset exceeds memory map size");
  }
  ptr += regAddr;
  return *(unsigned int*)ptr;
}

/*
 * scdReadReg
 *
 * Reads the SCD/SAT FPGA register.
 */
bool scdReadReg(struct scd_info_s* pMem, uint32_t regAddr, uint32_t length) {
  unsigned int regVal;

  for (int i = 0; i < length; i++) {
    regVal = scdReadRegInternal(pMem, regAddr + i * 4);
    if (!(i % 4)) {
      if (i) {
        printf("\n");
      }
      printf("0x%x: ", regAddr + i * 4);
    }
    printf("%.8x ", regVal);
  }
  printf("\n");
  return true;
}

/*
 * scdWriteRegInternal
 *
 * Write the SCD/SAT FPGA register. The register address is 16 bit value, the
 * data is 32 bit value. The register could be in SCD or SAT FPGA
 */
void scdWriteRegInternal(
    struct scd_info_s* pMem,
    uint32_t regAddr,
    uint32_t regVal) {
  unsigned char* ptr = pMem->mapAddr;
  if (regAddr >= pMem->mapSize) {
    return;
  }
  ptr += regAddr;
  *(unsigned int*)ptr = regVal;
}

/*
 * scdWriteReg
 *
 * Writes to the SCD/SAT FPGA register.
 */
bool scdWriteReg(struct scd_info_s* pMem, uint32_t regAddr, uint32_t regVal) {
  scdWriteRegInternal(pMem, regAddr, regVal);
  regVal = scdReadRegInternal(pMem, regAddr);
  printf("Reg 0x%x = 0x%.8x\n", regAddr, regVal);
  return true;
}

/*
 * scdQsfpInfo
 *
 * Prints QSFP related information
 */
bool scdQsfpInfo(struct scd_info_s* pMem) {
  uint32_t regAddr = kQsfpRegAddr;
  uint32_t regVal;

  printf("Port        ModReset    LowPower    Present\n");

  for (int port = 0; port < kMaxQsfpModules; port++) {
    regVal = scdReadRegInternal(pMem, regAddr + port * 0x10);
    printf(
        "%2d          %d           %d           %d\n",
        port + 1,
        (regVal & 0x80) >> 7,
        (regVal & 0x40) >> 6,
        !((regVal & 0x4) >> 2));
  }
  return true;
}

/*
 * scd_fpga_util
 *
 * This utility finds the SCD FPGA on PCI link in Darwin platform. It then
 * allows user to read/write to the SCD/SAT FPGA registers
 */
int main(int argc, char* argv[]) {
  const folly::Init init(&argc, &argv, true);
  gflags::SetCommandLineOptionWithMode(
      "minloglevel", "0", gflags::SET_FLAGS_DEFAULT);

  std::string sysfsPath;
  std::array<char, 64> busId;

  FILE* fp = popen("lspci -d 3475:0001 | sed -e \'s/ .*//\'", "r");
  if (!fp) {
    printf("SCD FPGA not found on PCI bus\n");
    return EX_SOFTWARE;
  }
  fgets(busId.data(), 64, fp);
  std::istringstream iStream(folly::to<std::string>(busId));
  std::string busIdStr;
  iStream >> busIdStr;

  // Build the sysfs file path for resource0 where all registers are mapped
  sysfsPath = folly::to<std::string>(
      "/sys/bus/pci/devices/0000:", busIdStr, "/resource0");

  pclose(fp);

  // Open the sysfs file to access the device
  int fd = open(sysfsPath.c_str(), O_RDWR | O_SYNC);
  if (fd == -1) {
    printf("Unable to open up the sysfs file %s\n", sysfsPath.c_str());
    return EX_SOFTWARE;
  }

  // Mmeory map the device's 512KB memory register space to our
  // user space
  struct scd_info_s scdInfo;
  scdInfo.mapSize = 0x80000;
  scdInfo.mapAddr = (unsigned char*)mmap(
      nullptr, 0x80000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (scdInfo.mapAddr == MAP_FAILED) {
    printf("mmap failed for resource\n");
    close(fd);
    return EX_SOFTWARE;
  }

  if (FLAGS_read_reg) {
    printf(
        "Calling scdReadReg with address 0x%x length %d\n",
        FLAGS_offset,
        FLAGS_length);
    scdReadReg(&scdInfo, FLAGS_offset, FLAGS_length);
  }
  if (FLAGS_write_reg) {
    if (FLAGS_data == -1) {
      printf("Valid data need to be specified for write operation\n");
      return EX_SOFTWARE;
    }
    printf(
        "Calling scdWriteReg with address 0x%x value 0x%x\n",
        FLAGS_offset,
        FLAGS_data);
    scdWriteReg(&scdInfo, FLAGS_offset, FLAGS_data);
  }
  if (FLAGS_qsfp_info) {
    printf("Calling scdQsfpInfo\n");
    scdQsfpInfo(&scdInfo);
  }

  munmap(scdInfo.mapAddr, 0x80000);
  close(fd);
  return EX_OK;
}
