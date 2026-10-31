// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/platform_manager/CpldManager.h"

#include <fcntl.h>
#include <sys/ioctl.h>

#include <cstring>
#include <filesystem>
#include <stdexcept>

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/File.h>
#include <folly/String.h>
#include <folly/logging/xlog.h>

#include "fboss/platform/platform_manager/Utils.h"
#include "fboss/platform/platform_manager/uapi/fbcpld-ioctl.h"

namespace facebook::fboss::platform::platform_manager {
namespace {

constexpr auto kCharDevWaitSecs = std::chrono::seconds(5);

} // namespace

std::string getCpldCharDevPath(uint16_t busNum, const I2cAddr& addr) {
  return fmt::format("/dev/fbcpld-{}-{}", busNum, addr.hex4Str());
}

const std::unordered_map<std::string, uint32_t>& getCpldFlagMap() {
  static const std::unordered_map<std::string, uint32_t> flagMap = {
      {"log_write", FBCPLD_FLAG_LOG_WRITE},
      {"show_notes", FBCPLD_FLAG_SHOW_NOTES},
      {"decimal", FBCPLD_FLAG_SHOW_DEC},
      {"negate", FBCPLD_FLAG_VAL_NEGATE},
  };
  return flagMap;
}

struct fbcpld_ioctl_create_request buildCpldIoctlRequest(
    const std::vector<CpldSysfsAttr>& cpldSysfsAttrs) {
  struct fbcpld_ioctl_create_request request{};
  request.num_attrs = folly::to<uint32_t>(cpldSysfsAttrs.size());

  for (size_t i = 0; i < cpldSysfsAttrs.size(); ++i) {
    const auto& src = cpldSysfsAttrs[i];
    auto& dst = request.attrs[i];

    std::strncpy(dst.name, src.name()->c_str(), NAME_MAX - 1);
    if (*src.mode() == "rw") {
      dst.mode = 0644;
    } else if (*src.mode() == "wo") {
      dst.mode = 0200;
    } else {
      dst.mode = 0444;
    }
    dst.reg_addr = folly::to<uint32_t>(std::stoul(*src.regAddr(), nullptr, 16));
    dst.bit_offset = *src.bitOffset();
    dst.num_bits = *src.numBits();

    for (const auto& flag : *src.flags()) {
      auto it = getCpldFlagMap().find(flag);
      if (it != getCpldFlagMap().end()) {
        dst.flags |= it->second;
      }
    }

    std::strncpy(
        dst.help_str, src.description()->c_str(), FBCPLD_HELP_STR_MAX - 1);
  }

  return request;
}

void createCpldSysfsAttrs(
    uint16_t busNum,
    const I2cAddr& addr,
    const std::vector<CpldSysfsAttr>& cpldSysfsAttrs) {
  auto charDevPath = getCpldCharDevPath(busNum, addr);

  if (!Utils().checkDeviceReadiness(
          [&]() -> bool { return std::filesystem::exists(charDevPath); },
          fmt::format(
              "CPLD char dev {} is not yet created. Waiting for at most {}s",
              charDevPath,
              kCharDevWaitSecs.count()),
          kCharDevWaitSecs)) {
    throw std::runtime_error(
        fmt::format(
            "Failed to create CPLD sysfs attrs: char dev {} not found",
            charDevPath));
  }

  if (cpldSysfsAttrs.size() > FBCPLD_MAX_ATTRS) {
    throw std::runtime_error(
        fmt::format(
            "Too many CPLD sysfs attrs for {}: {} exceeds max {}",
            charDevPath,
            cpldSysfsAttrs.size(),
            FBCPLD_MAX_ATTRS));
  }

  XLOG(INFO) << fmt::format(
      "Creating {} CPLD sysfs attributes on {}",
      cpldSysfsAttrs.size(),
      charDevPath);

  folly::File devFile;
  try {
    devFile = folly::File(charDevPath.c_str(), O_RDWR);
  } catch (const std::system_error& e) {
    throw std::runtime_error(
        fmt::format(
            "Failed to open CPLD char dev {}: {}", charDevPath, e.what()));
  }

  // Clear attrs left over from a previous run (driver state outlives a
  // platform_manager restart) so CREATE below doesn't hit "already exists".
  ioctl(devFile.fd(), FBCPLD_IOC_SYSFS_DESTROY_ALL);

  auto request = buildCpldIoctlRequest(cpldSysfsAttrs);

  int ret = ioctl(devFile.fd(), FBCPLD_IOC_SYSFS_CREATE, &request);
  if (ret < 0) {
    throw std::runtime_error(
        fmt::format(
            "Failed to create CPLD sysfs attrs on {}: ioctl failed: {}",
            charDevPath,
            folly::errnoStr(errno)));
  }

  XLOG(INFO) << fmt::format(
      "Created {} CPLD sysfs attributes on {}",
      cpldSysfsAttrs.size(),
      charDevPath);
}

} // namespace facebook::fboss::platform::platform_manager
