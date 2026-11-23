/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/lib/restart_tracker/RestartTimeTracker.h"

#include <fb303/ServiceData.h>

#include <folly/Conv.h>
#include <folly/FileUtil.h>
#include <folly/Range.h>
#include <folly/Synchronized.h>
#include <folly/logging/xlog.h>

#include <sys/stat.h>
#include <unistd.h>
#include <mutex>

namespace facebook::fboss {

using namespace std::chrono;
using TimePoint = time_point<steady_clock>;

namespace {
constexpr auto kWarmBootPrefix = "warm_boot.";
constexpr auto kColdBootPrefix = "cold_boot.";
constexpr auto kRestartPrefix = "restart.";
constexpr auto kStageCounterSuffix = "stage_duration_ms";
constexpr auto kTotalCounterSuffix = "total_duration_ms";
constexpr auto kMaxWarmBootTime = minutes(5);

// The output of /proc/<pid>/stat outputs a bunch of ints separated by
// space. The 21st token stores jiffies since boot for startup time.
constexpr auto kStatStartupTimeIndex = 21;

TimePoint writeTimePointToFile(TimePoint tp, const folly::StringPiece path) {
  auto millis = time_point_cast<milliseconds>(tp).time_since_epoch().count();
  writeFileAtomic(path, std::to_string(millis));
  return tp;
}

std::optional<TimePoint> processStartTime(int pid) {
  std::string statContents;
  auto path = folly::to<std::string>("/proc/", pid, "/stat");
  if (!folly::readFile(path.c_str(), statContents)) {
    return std::nullopt;
  }

  std::vector<folly::StringPiece> tokens;
  folly::split(' ', statContents, tokens);

  if (tokens.size() <= kStatStartupTimeIndex) {
    // Unexpected output of /proc/<pid>/stat
    return std::nullopt;
  }

  uint64_t millisecondsSinceBoot =
      std::stol(tokens[kStatStartupTimeIndex].begin()) *
      (1000 / sysconf(_SC_CLK_TCK));
  return TimePoint() + milliseconds(millisecondsSinceBoot);
}

std::optional<TimePoint> readAndRemoveTimePointFile(
    const folly::StringPiece path) {
  std::optional<TimePoint> ret{std::nullopt};

  std::string out;
  if (folly::readFile(path.begin(), out)) {
    XLOG(INFO) << "Read contents " << path;
    ret = TimePoint() + std::chrono::milliseconds(std::stol(out));
  } else {
    XLOG(DBG3) << "Failed to read contents " << path;
  }

  // ensure the file is deleted
  unlink(path.begin());

  return ret;
}

std::string to_string(RestartEvent type) {
  switch (type) {
    case RestartEvent::PARENT_PROCESS_STARTED:
      return "parent_process_started";
    case RestartEvent::PROCESS_STARTED:
      return "process_started";
    case RestartEvent::INITIALIZED:
      return "initialized";
    case RestartEvent::CONFIGURED:
      return "configured";
    case RestartEvent::FIB_SYNCED_BGPD:
      return "fib_synced_bgp";
    case RestartEvent::FIB_SYNCED_OPENR:
      return "fib_synced_openr";
    case RestartEvent::VRFS_SYNCED:
      return "vrfs_synced";
    case RestartEvent::SIGNAL_RECEIVED:
      return "signal_received";
    case RestartEvent::SHUTDOWN:
      return "shutdown";
    default:
      throw std::runtime_error(
          folly::to<std::string>(
              "Unhandled RestartEvent: ", static_cast<uint16_t>(type)));
  }
}

void exportDurationCounter(
    TimePoint from,
    TimePoint to,
    folly::StringPiece counterName) {
  auto stageDuration = duration_cast<milliseconds>(to - from);
  fb303::fbData->setCounter(counterName, stageDuration.count());
  XLOG(DBG2) << counterName << " -> " << stageDuration.count() << "ms";
}

class RestartTimeTracker {
 public:
  RestartTimeTracker(
      const std::string& warmBootDir,
      bool warmBoot,
      const std::optional<std::string>& serviceName)
      : warmBootDir_(warmBootDir),
        prefix_(
            (serviceName) ? kRestartPrefix
                          : (warmBoot ? kWarmBootPrefix : kColdBootPrefix)),
        serviceName_(serviceName) {
    if (warmBoot) {
      auto signalled = newEvent(RestartEvent::SIGNAL_RECEIVED);
      // only consider recent warm boots to avoid bogus high readings
      if (signalled) {
        auto time_since_signal = steady_clock::now() - *signalled;
        if (time_since_signal < kMaxWarmBootTime) {
          firstEvent_ = signalled;
          newEvent(RestartEvent::SHUTDOWN);
          // If we add more events from previous process, add here
          newEvent(RestartEvent::PARENT_PROCESS_STARTED);
        }
      }
      // make sure we always gather events since process start.
      newEvent(RestartEvent::PROCESS_STARTED);
    } else {
      firstEvent_ = newEvent(RestartEvent::PROCESS_STARTED);
    }
  }

  std::optional<TimePoint> newEvent(RestartEvent type) {
    auto tp = create(type);

    if (tp) {
      if (lastEvent_) {
        exportDurationCounter(*lastEvent_, *tp, stageCounterName(type));
      }
      if (type == RestartEvent::CONFIGURED) {
        completed_ = true;
        if (firstEvent_) {
          // terminal state, export total counter
          exportDurationCounter(*firstEvent_, *tp, totalCounterName());
        }
      }
      lastEvent_ = tp;
    }
    return tp;
  }

 private:
  std::string savePath(RestartEvent type) {
    if (serviceName_) {
      return folly::to<std::string>(
          warmBootDir_, "/", *serviceName_, "/", to_string(type));
    } else {
      return folly::to<std::string>(warmBootDir_, "/", to_string(type));
    }
  }

  std::string stageCounterName(RestartEvent type) {
    return folly::to<std::string>(
        prefix_, to_string(type), ".", kStageCounterSuffix);
  }

  std::string totalCounterName() {
    return folly::to<std::string>(prefix_, kTotalCounterSuffix);
  }

  std::optional<TimePoint> create(RestartEvent type) {
    XLOG(INFO) << "Creating event: " << to_string(type);

    switch (type) {
      case RestartEvent::PARENT_PROCESS_STARTED:
        return processStartTime(getppid());
      case RestartEvent::PROCESS_STARTED:
        return processStartTime(getpid());
      case RestartEvent::INITIALIZED:
      case RestartEvent::CONFIGURED:
      case RestartEvent::FIB_SYNCED_BGPD:
      case RestartEvent::FIB_SYNCED_OPENR:
      case RestartEvent::VRFS_SYNCED:
        return steady_clock::now();
      case RestartEvent::SIGNAL_RECEIVED:
      case RestartEvent::SHUTDOWN:
        if (completed_) {
          return writeTimePointToFile(steady_clock::now(), savePath(type));
        } else {
          return readAndRemoveTimePointFile(savePath(type));
        }
      default:
        throw std::runtime_error(
            folly::to<std::string>(
                "Unhandled RestartEvent: ", static_cast<uint16_t>(type)));
    }
  }

  const std::string warmBootDir_;
  const std::string prefix_;
  const std::optional<std::string> serviceName_;
  std::optional<TimePoint> firstEvent_;
  std::optional<TimePoint> lastEvent_;
  bool completed_{false};
};
} // namespace

namespace restart_time {

folly::Synchronized<std::unique_ptr<RestartTimeTracker>, std::mutex> impl_;

void init(
    const std::string& warmBootDir,
    bool warmBoot,
    const std::optional<std::string>& serviceName) {
  auto tracker = impl_.lock();
  if (*tracker) {
    throw std::runtime_error("Called restart_time::init twice...");
  }
  *tracker =
      std::make_unique<RestartTimeTracker>(warmBootDir, warmBoot, serviceName);
}

void mark(RestartEvent event) {
  auto tracker = impl_.lock();
  if (*tracker) {
    (*tracker)->newEvent(event);
  } else {
    XLOG(ERR) << "Cannot call restart_time::mark() before restart_time::init";
  }
}

void stop() {
  auto tracker = impl_.lock();
  if (*tracker) {
    (*tracker).reset();
  }
}

} // namespace restart_time

} // namespace facebook::fboss
