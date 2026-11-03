#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>

#include <folly/logging/xlog.h>
#include <gtest/gtest.h>

#include "fboss/platform/bsp_tests/BspTest.h"
#include "fboss/platform/bsp_tests/utils/CdevUtils.h"
#include "fboss/platform/platform_manager/uapi/fbiob-ioctl.h"

namespace facebook::fboss::platform::bsp_tests {

// Test fixture for character device tests
class CdevTest : public BspTest {
 protected:
  // Helper function to get empty aux data
  struct fbiob_aux_data getEmptyAuxData() const {
    struct fbiob_aux_data auxData{};
    return auxData;
  }

  // Helper function to get invalid aux data
  struct fbiob_aux_data getInvalidAuxData() const {
    struct fbiob_aux_data auxData{};
    auxData.csr_offset = static_cast<__u32>(-1);
    return auxData;
  }
};

// Test that character device files are created for each PCI device
TEST_F(CdevTest, CdevIsCreated) {
  const auto pciDevices = *GetRuntimeConfig().devices();
  ASSERT_FALSE(pciDevices.empty()) << "No PCI devices found in runtime config";

  for (const auto& pciDevice : pciDevices) {
    std::string path = CdevUtils::makePciPath(*pciDevice.pciInfo());
    EXPECT_TRUE(std::filesystem::exists(path))
        << "Character device file does not exist: " << path;
  }
}

// Test that empty aux data is handled appropriately
TEST_F(CdevTest, EmptyAuxData) {
  const auto pciDevices = *GetRuntimeConfig().devices();
  ASSERT_FALSE(pciDevices.empty()) << "No PCI devices found in runtime config";

  for (const auto& pciDevice : pciDevices) {
    struct fbiob_aux_data auxData = getEmptyAuxData();
    std::string path = CdevUtils::makePciPath(*pciDevice.pciInfo());

    int fd = open(path.c_str(), O_RDWR);
    ASSERT_GE(fd, 0) << "Failed to open device: " << path
                     << ", error: " << strerror(errno);

    try {
      int res = ioctl(fd, FBIOB_IOC_NEW_DEVICE, &auxData);
      EXPECT_EQ(res, 0)
          << "FBIOB_IOC_NEW_DEVICE ioctl failed with empty aux data";

      res = ioctl(fd, FBIOB_IOC_DEL_DEVICE, &auxData);
      EXPECT_EQ(res, 0)
          << "FBIOB_IOC_DEL_DEVICE ioctl failed with empty aux data";
    } catch (const std::exception& e) {
      close(fd);
      FAIL() << "Error running ioctl commands: " << e.what();
    }

    close(fd);
  }
}

// Test that invalid aux data is rejected
TEST_F(CdevTest, CdevRejectsInvalidData) {
  const auto pciDevices = *GetRuntimeConfig().devices();
  ASSERT_FALSE(pciDevices.empty()) << "No PCI devices found in runtime config";

  for (const auto& pciDevice : pciDevices) {
    struct fbiob_aux_data auxData = getInvalidAuxData();
    std::string path = CdevUtils::makePciPath(*pciDevice.pciInfo());

    int fd = open(path.c_str(), O_RDWR);
    ASSERT_GE(fd, 0) << "Failed to open device: " << path
                     << ", error: " << strerror(errno);

    // Expect the ioctl to fail with invalid data
    int res = ioctl(fd, FBIOB_IOC_NEW_DEVICE, &auxData);
    EXPECT_LT(res, 0)
        << "FBIOB_IOC_NEW_DEVICE ioctl unexpectedly succeeded with invalid data";

    close(fd);
  }
}

// Test creating and deleting devices
TEST_F(CdevTest, CdevCreateAndDelete) {
  const auto pciDevices = *GetRuntimeConfig().devices();
  ASSERT_FALSE(pciDevices.empty()) << "No PCI devices found in runtime config";

  for (const auto& pciDevice : pciDevices) {
    if (pciDevice.auxDevices()->empty()) {
      continue;
    }
    for (const auto& auxDevice : *pciDevice.auxDevices()) {
      EXPECT_NO_THROW(
          CdevUtils::createNewDevice(*pciDevice.pciInfo(), auxDevice, 1));

      // Sleep for a short time to ensure device is created
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      EXPECT_NO_THROW(
          CdevUtils::deleteDevice(*pciDevice.pciInfo(), auxDevice, 1));
    }
  }
}

} // namespace facebook::fboss::platform::bsp_tests
