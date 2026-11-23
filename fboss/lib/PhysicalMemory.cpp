/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/lib/PhysicalMemory.h"

#include <cstdint>

#include <fmt/format.h>
#include <folly/Exception.h>
#include <folly/File.h>
#include <folly/logging/xlog.h>

#include <sys/mman.h>

namespace {
constexpr auto kLockPath = "/tmp";
constexpr uint64_t kPageSize = 4096;
constexpr uint64_t kPageMask = (kPageSize - 1);
} // namespace

namespace facebook::fboss {
PhysicalMemory::PhysicalMemory(uint64_t phyAddr, uint32_t size, bool mustLock)
    : phyAddr_(phyAddr), size_(size) {
  // Both size_ and phyAddr_ must be aligned to page size
  if ((size_ & kPageMask)) {
    folly::throwSystemErrorExplicit(EINVAL, "size is not aligned");
  }
  if ((phyAddr_ & kPageMask)) {
    folly::throwSystemErrorExplicit(EINVAL, "physical address is not aligned");
  }

  // Resource free in case of throw
  SCOPE_FAIL {
    cleanup();
  };

  // construct the lock file name
  const auto lockFN = fmt::format("{}/pmem_{:x}_lock", kLockPath, phyAddr_);

  // lock it. O_NOFOLLOW refuses a pre-planted symlink at the predictable /tmp
  // lock path (this process runs as root), and O_CLOEXEC avoids leaking the fd
  // across exec.
  lockFile_ = folly::File(lockFN, O_CREAT | O_WRONLY | O_NOFOLLOW | O_CLOEXEC);
  auto locked = lockFile_.try_lock();
  XLOG(DBG1) << fmt::format(
      "{} to acquire lock, {}({})",
      (locked) ? "Success" : "Fail",
      lockFN,
      lockFile_.fd());

  if (mustLock && !locked) {
    folly::throwSystemErrorExplicit(
        EWOULDBLOCK, "Cannot lock the file ", lockFN);
  }
}

void PhysicalMemory::mmap() {
  if (virtAddr_) {
    return;
  }

  // open the memory now
  memFile_ = folly::File("/dev/mem", O_RDWR | O_SYNC);

  // map it now
  virtAddr_ = ::mmap(
      nullptr,
      size_,
      PROT_READ | PROT_WRITE,
      MAP_SHARED,
      memFile_.fd(),
      phyAddr_);
  if (virtAddr_ == (void*)-1) {
    virtAddr_ = nullptr;
    auto errnoCopy = errno;
    folly::throwSystemErrorExplicit(
        errnoCopy,
        fmt::format("Cannot mmap {:#x} with size {:#x}", phyAddr_, size_));
  }

  XLOG(DBG1) << fmt::format(
      "Mapped physical memory {:#x} with size {:#x} into {:#x}",
      phyAddr_,
      size_,
      reinterpret_cast<uint64_t>(virtAddr_));
}

void PhysicalMemory::munmap() noexcept {
  if (virtAddr_) {
    XLOG(DBG1) << fmt::format(
        "unmapping @ {:#x} with size {:#x}",
        reinterpret_cast<uint64_t>(virtAddr_),
        size_);
    ::munmap(virtAddr_, size_);
    virtAddr_ = nullptr;
  }

  memFile_.closeNoThrow();
}

void PhysicalMemory::cleanup() noexcept {
  this->munmap();

  XLOG(DBG1) << "closing lock file";
  lockFile_.closeNoThrow();
}

} // namespace facebook::fboss
