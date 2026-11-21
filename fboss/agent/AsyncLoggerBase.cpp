/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <fcntl.h>
#include <unistd.h>
#include <iomanip>

#include "fboss/agent/AsyncLoggerBase.h"

#include <fb303/ServiceData.h>
#include <folly/FileUtil.h>
#include <folly/String.h>
#include <folly/logging/xlog.h>
#include <gflags/gflags.h>

DEFINE_bool(
    disable_async_logger,
    false,
    "Flag to indicate whether to disable async logging and directly write into the file");

namespace {

constexpr auto kBuildRevision = "build_revision";
constexpr auto kVerboseSdkVersion = "Verbose SDK Version";
constexpr auto kSdkLogPath = "/var/facebook/logs/fboss/sdk/";
constexpr auto kStateDeltaLogPath = "/var/facebook/logs/fboss/";

bool isWarmBoot;
std::mutex bootTypeLatch_;

} // namespace

namespace facebook::fboss {

AsyncLoggerBase::AsyncLoggerBase(
    const std::string& filePath,
    uint32_t logTimeout,
    LoggerSrcType srcType,
    char* logBuf,
    char* flushBuf,
    uint32_t bufferSize)
    : bufferSize_(bufferSize), srcType_(srcType) {
  openLogFile(filePath);

  if (!FLAGS_disable_async_logger) {
    logBuffer_ = logBuf;
    flushBuffer_ = flushBuf;
    logTimeout_ = std::chrono::milliseconds(logTimeout);
  }
}

void AsyncLoggerBase::workerThread() {
  while (enableLogging_) {
    std::unique_lock<std::mutex> lock(latch_);

    cv_.wait_for(lock, logTimeout_, [this] {
      return this->forceFlush_ || this->fullFlush_;
    });

    char* writeBuffer = logBuffer_;
    uint32_t currSize = getOffset();
    setOffset(0);
    swapCurBuf();
    logBuffer_ = flushBuffer_;

    if (fullFlush_) {
      fullFlush_ = false;
    }

    lock.unlock();
    cv_.notify_one();

    if (currSize > 0) {
      flushCount_++;
      auto bytesWritten = logFile_.withWLock([&](auto& lockedFile) {
        return folly::writeFull(lockedFile.fd(), writeBuffer, currSize);
      });

      // Drop the logs rather than throw: an exception here escapes the
      // thread and terminates the process (e.g. on a full disk).
      if (bytesWritten < 0) {
        XLOG_EVERY_MS(ERR, 10000)
            << "[Async Logger] Failed to write " << currSize
            << " bytes to log file: " << folly::errnoStr(errno);
      }
    }

    memset(writeBuffer, 0, bufferSize_);
    flushBuffer_ = writeBuffer;

    if (forceFlush_) {
      forceFlush_ = false;
      promise_.set_value(0);
      promise_ = std::promise<int>();
    }
  }
}

void AsyncLoggerBase::setBootType(bool canWarmBoot) {
  bootTypeLatch_.lock();
  isWarmBoot = canWarmBoot;
  bootTypeLatch_.unlock();
}

bool AsyncLoggerBase::getBootType() {
  std::lock_guard<std::mutex> guard(bootTypeLatch_);
  return isWarmBoot;
}

void AsyncLoggerBase::startFlushThread() {
  enableLogging_ = true;
  if (!FLAGS_disable_async_logger) {
    flushThread_ = new std::thread(&AsyncLoggerBase::workerThread, this);
  }
  writeNewBootHeader();
}

void AsyncLoggerBase::stopFlushThread() {
  if (!FLAGS_disable_async_logger && enableLogging_) {
    enableLogging_ = false;
    flushThread_->join();
    delete flushThread_;
  }
}
void AsyncLoggerBase::forceFlush() {
  if (!FLAGS_disable_async_logger) {
    future_ = promise_.get_future();
    forceFlush_ = true;
    cv_.notify_one();
    future_.get();
  }
}

void AsyncLoggerBase::appendLog(const char* logRecord, size_t logSize) {
  if (!enableLogging_ || logSize == 0) {
    return;
  }

  auto writeDirectlyToFile = [this](const char* logRecord, size_t logSize) {
    auto bytesWritten = logFile_.withWLock([&](auto& lockedFile) {
      return folly::writeFull(lockedFile.fd(), logRecord, logSize);
    });

    // Drop the logs rather than throw: an exception here escapes the
    // thread and terminates the process (e.g. on a full disk).
    if (bytesWritten < 0) {
      XLOG_EVERY_MS(ERR, 10000)
          << "[Async Logger] Failed to write " << logSize
          << " bytes to log file: " << folly::errnoStr(errno);
    }
  };

  if (FLAGS_disable_async_logger) {
    writeDirectlyToFile(logRecord, logSize);
    return;
  }

  // Acquire the lock and check if there's enough space in the buffer
  latch_.lock();
  if (logSize + getOffset() >= bufferSize_) {
    // Release the lock and notify worker thread to flush logs
    fullFlush_ = true;
    auto currentFlushCount = flushCount_.load();
    latch_.unlock();
    cv_.notify_one();

    if (logSize >= bufferSize_) {
      // If log size is greater than buffer size, directly write to file instead
      // of writing into buffer. To optimistically maintain the same ordering,
      // it will wait for current buffer to be flushed to disk and then write to
      // file.
      std::unique_lock<std::mutex> lock(latch_);
      cv_.wait(lock, [this, currentFlushCount] {
        return flushCount_ == currentFlushCount + 1 || this->getOffset() == 0;
      });

      // If the log is larger than the buffer, write directly to file
      writeDirectlyToFile(logRecord, logSize);
    } else {
      // Wait for worker to finish
      std::unique_lock<std::mutex> lock(latch_);
      cv_.wait(lock, [this, logSize] {
        return logSize + this->getOffset() < this->bufferSize_;
      });

      memcpy(logBuffer_ + getOffset(), logRecord, logSize);
      setOffset(getOffset() + logSize);
    }
  } else {
    // Directly write to buffer
    memcpy(logBuffer_ + getOffset(), logRecord, logSize);
    setOffset(getOffset() + logSize);
    latch_.unlock();
  }
}

void AsyncLoggerBase::openLogFile(const std::string& filePath) {
  std::string srcTypeStr;
  std::string logPath;
  switch (srcType_) {
    case (BCM_CINTER):
      srcTypeStr = "Bcm Cinter";
      logPath = kSdkLogPath;
      break;
    case (SAI_REPLAYER):
      srcTypeStr = "Sai Replayer";
      logPath = kSdkLogPath;
      break;
    case (STATE_DELTA):
      srcTypeStr = "State Delta";
      logPath = kStateDeltaLogPath;
      break;
  }
  try {
    if (filePath.find(logPath) == 0) {
      logFile_ = folly::File(filePath, O_RDWR | O_CREAT | O_APPEND);
    } else {
      logFile_ = folly::File(filePath, O_RDWR | O_CREAT | O_TRUNC);
    }
  } catch (const std::system_error&) {
    auto last_slash = filePath.find_last_of('/');

    std::string directory = filePath.substr(0, last_slash);
    std::string file_name = filePath.substr(last_slash + 1);

    XLOG(WARN) << "[Async Logger] Failed to create " << file_name << " under "
               << directory << ". Logging " << srcTypeStr << " log at /tmp/"
               << file_name;

    // The agent runs as root and /tmp is world-writable, so opening a
    // predictable /tmp path with O_TRUNC is a symlink-following hazard: a local
    // attacker could pre-place the path as a symlink and redirect the truncate
    // to an arbitrary file. Unlink any pre-existing entry first (unlink never
    // follows a symlink/hardlink) then atomically create a fresh file with
    // O_EXCL | O_NOFOLLOW.
    const std::string fallbackPath = "/tmp/" + file_name;
    ::unlink(fallbackPath.c_str());
    try {
      logFile_ =
          folly::File(fallbackPath, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW);
    } catch (const std::system_error& ex) {
      // Best-effort logging: if the symlink-safe create fails (e.g. the entry
      // could not be unlinked on a sticky /tmp, or a link was re-planted in the
      // race window so O_EXCL|O_NOFOLLOW refuses it), degrade to /dev/null
      // rather than throwing out of the constructor and crashing the agent.
      XLOG(ERR) << "[Async Logger] Failed to safely open fallback log "
                << fallbackPath << ": " << ex.what() << "; discarding "
                << srcTypeStr << " logs";
      // Use folly::openNoInt (EINTR-safe, returns -1 on failure, never throws)
      // + the non-throwing folly::File(fd, ownsFd) ctor, so a failed /dev/null
      // open still cannot throw out of this constructor.
      int devNullFd = folly::openNoInt("/dev/null", O_WRONLY | O_CLOEXEC);
      logFile_ = folly::File(devNullFd, /*ownsFd=*/devNullFd >= 0);
    }
    return;
  }

  XLOG(DBG2) << "[Async Logger] Logging " << srcTypeStr << " log at "
             << filePath;
}

void AsyncLoggerBase::writeNewBootHeader() {
  auto now = std::chrono::system_clock::now();
  auto timer = std::chrono::system_clock::to_time_t(now);
  std::tm tm;
  localtime_r(&timer, &tm);
  auto bootType = isWarmBoot ? "warmboot" : "coldboot";
  std::ostringstream oss;
  oss << "// Start of a " << bootType << " "
      << std::put_time(&tm, "%Y-%m-%d %T") << std::endl;
  auto ossString = oss.str();
  appendLog(ossString.c_str(), ossString.size());
  std::string commitId;
  fb303::fbData->getExportedValue(commitId, kBuildRevision);
  std::string verboseSdkVersion;
  fb303::fbData->getExportedValue(verboseSdkVersion, kVerboseSdkVersion);
  verboseSdkVersion.erase(
      std::remove(verboseSdkVersion.begin(), verboseSdkVersion.end(), '\n'),
      verboseSdkVersion.end());
  std::string agentVersion = folly::to<std::string>(
      "// Commit id : ",
      commitId,
      "\n",
      "// SDK version : ",
      verboseSdkVersion,
      "\n");
  appendLog(agentVersion.c_str(), agentVersion.size());
}

} // namespace facebook::fboss
