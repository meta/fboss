// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/fsdb/client/FsdbPublisher.h"

#include "fboss/fsdb/if/gen-cpp2/fsdb_common_types.h"
#include "fboss/fsdb/if/gen-cpp2/fsdb_oper_types.h"

#include <folly/logging/xlog.h>
#include <chrono>
#include <memory>
#include <type_traits>
#include <utility>

DEFINE_int32(
    fsdb_publisher_heartbeat_interval_secs,
    1,
    "Heartbeat interval for publisher");

DEFINE_int32(
    publish_queue_memory_limit_mb,
    0,
    "Limit in MB for total size of pending updates in FSDB Publisher queue (0 to disable)");

DEFINE_int32(
    publish_queue_full_min_updates,
    5,
    "minimum number of pending updates to trigger FSDB Publisher queue memory limit check");

// Disambiguates state vs stats and Path/Delta/Patch publishers that share a
// clientId within the same process (e.g., agent has both "agent" state and
// "agent" stats publishers).
#define PUB_XLOG(LEVEL)                                                     \
  XLOG(LEVEL) << "[" << typeStr() << "/" << (isStats() ? "stats" : "state") \
              << "] "

namespace facebook::fboss::fsdb {

template <typename PubUnit>
std::string FsdbPublisher<PubUnit>::typeStr() {
  if constexpr (std::is_same_v<PubUnit, OperDelta>) {
    return "Delta";
  } else if constexpr (std::is_same_v<PubUnit, OperState>) {
    return "Path";
  } else {
    return "Patch";
  }
}

template <typename PubUnit>
OperPubRequest FsdbPublisher<PubUnit>::createRequest() const {
  OperPath operPath;
  operPath.raw() = publishPath_;
  OperPubRequest request;
  request.path() = operPath;
  request.publisherId() = clientId();
  return request;
}

template <typename PubUnit>
void FsdbPublisher<PubUnit>::handleStateChange(
    State /*oldState*/,
    State newState) {
#if FOLLY_HAS_COROUTINES
  auto pipeWPtr = asyncPipe_.wlock();
  // setState() invokes this callback outside its state lock, so a CONNECTING
  // callback can run after the service loop has already moved to CONNECTED and
  // would reset the fresh pipe. Ignore stale transitions.
  if (newState != getState()) {
    return;
  }
  if (newState != State::CONNECTED) {
    cancelHeartbeatLoop();
    // If we went to any other state than CONNECTED, reset the publish queue.
    // Per FSDB protocol, publishers are required to do a full-sync post
    // reconnect, so there is no point storing previous states.
    pipeWPtr->reset();
    queueSize_ = 0;
    enqueuedDataSize_.store(0);
    servedDataSize_.store(0);
  } else {
    *pipeWPtr = makePipe();
    scheduleHeartbeatLoop();
  }
#endif
}

template <typename PubUnit>
bool FsdbPublisher<PubUnit>::write(PubUnit&& pubUnit) {
  pubUnit.metadata().ensure();
  {
    auto ts = std::chrono::system_clock::now().time_since_epoch();
    if (!pubUnit.metadata()->lastConfirmedAt()) {
      pubUnit.metadata()->lastConfirmedAt() =
          std::chrono::duration_cast<std::chrono::seconds>(ts).count();
    }
    lastConfirmedAt_ = *pubUnit.metadata()->lastConfirmedAt();
    if (!pubUnit.metadata()->lastPublishedAt()) {
      pubUnit.metadata()->lastPublishedAt() =
          std::chrono::duration_cast<std::chrono::milliseconds>(ts).count();
    }
  }

#if FOLLY_HAS_COROUTINES
  bool writeOk{true};
  size_t pubUnitSize{0};

  auto pipeUPtr = asyncPipe_.ulock();
  if (!(*pipeUPtr)) {
    PUB_XLOG(ERR) << "Could not enqueue pub unit";
    writeOk = false;
  } else if (publishQueueMemoryLimit_ > 0) {
    pubUnitSize = getPubUnitSize(pubUnit);
    auto queuedChunks = (*pipeUPtr)->second.getOccupiedSpace();
    if (queuedChunks > FLAGS_publish_queue_full_min_updates) {
      size_t queuedSize = enqueuedDataSize_.load() - servedDataSize_.load();
      if ((queuedSize + pubUnitSize) > publishQueueMemoryLimit_) {
        PUB_XLOG(ERR)
            << "Publish queue at memory limit, resetting stream. Data enqueued: "
            << queuedSize << " new data size: " << pubUnitSize
            << " memory limit: " << publishQueueMemoryLimit_ << " bytes";
        writeOk = false;
      }
    }
  }
  if (writeOk) {
    if (tryWrite((*pipeUPtr)->second, std::move(pubUnit))) {
      enqueuedDataSize_.fetch_add(pubUnitSize);
    } else {
      PUB_XLOG(ERR) << "Queue overflow, reset queue pointer";
      writeOk = false;
    }
  }
  if (!writeOk) {
    if (*pipeUPtr) {
      // Reset queue pointer so the service loop breaks and we fall
      // back to full sync protocol
      pipeUPtr.moveFromUpgradeToWrite()->reset();
      queueSize_ = 0;
      enqueuedDataSize_.store(0);
      servedDataSize_.store(0);
    }
    writeErrors_.addValue(1);
  }
#endif
  return writeOk;
}

template <typename PubUnit>
bool FsdbPublisher<PubUnit>::tryWrite(
    folly::coro::BoundedAsyncPipe<PubUnit, false>& pipe,
    PubUnit&& pubUnit) {
  if (pipe.try_write(pubUnit)) {
    queueSize_++;
    chunksWritten_.addValue(1);
    return true;
  } else {
    return false;
  }
}

#if FOLLY_HAS_COROUTINES
template <typename PubUnit>
folly::coro::AsyncGenerator<PubUnit&&>
FsdbPublisher<PubUnit>::createGenerator() {
  while (true) {
    // Extract pubUnit from pipe, then release lock before co_yield
    // This allows write() to reset the pipe even if we have yielded at
    // co_yield. This also ensures we dont hold the lock for longer than
    // necessary
    std::optional<PubUnit> pubUnit;
    {
      auto pipeRPtr = asyncPipe_.rlock();
      if (*pipeRPtr) {
        auto pubElement = co_await (*pipeRPtr)->first.next();

        if (!pubElement) {
          continue;
        }
        pubUnit = std::move(*pubElement);
        queueSize_--;
      } else {
        PUB_XLOG(ERR) << "Publish queue is null, unable to dequeue";
        FsdbException ex;
        ex.errorCode() = FsdbErrorCode::DISCONNECTED;
        ex.message() = "Publisher queue is null, cannot dequeue";
        co_yield folly::coro::co_error(ex);
        continue;
      }
    }
    co_yield std::move(*pubUnit);
  }
}
#endif

template <typename PubUnit>
bool FsdbPublisher<PubUnit>::disconnectForGR() {
#if FOLLY_HAS_COROUTINES
  // Mark the client as cancelling gracefully, and let the service loop know
  // that it should send GR disconnect.
  auto baton = folly::Baton<>();
  setGracefulServiceLoopCompletion([&baton]() { baton.post(); });
  // To inform the service loop, we will send an empty PubUnit to the pipe
  // as we can't write anything other than PubUnit to the pipe.
  {
    auto pipeUPtr = asyncPipe_.ulock();
    if (!(*pipeUPtr)) {
      PUB_XLOG(ERR) << "asyncPipe_.ulock() failed, nullptr";
      return false;
    }
    PubUnit emptyPubUnit;
    if (!(*pipeUPtr)->second.try_write(std::move(emptyPubUnit))) {
      XLOG(DBG2) << "pipe.try_write() failed, can ignore failure";
    }
  }
  // wait for the service loop to complete, and then cancel the client
  baton.wait();
  cancel();
#endif
  return true;
}

template <typename PubUnit>
void FsdbPublisher<PubUnit>::sendHeartbeat() {
  if (!initialSyncComplete_) {
    return;
  }
  auto pipeUPtr = asyncPipe_.ulock();
  if ((*pipeUPtr)) {
    // flush any pending update before sending heartbeat
    flush((*pipeUPtr)->second);
    PubUnit emptyPubUnit;
    if (((*pipeUPtr)->second).try_write(std::move(emptyPubUnit))) {
      queueSize_++;
    } else {
      PUB_XLOG(ERR) << "sendHeartbeat: queue full, reset pipe";
      pipeUPtr.moveFromUpgradeToWrite()->reset();
      queueSize_ = 0;
    }
  }
}

template <typename PubUnit>
void FsdbPublisher<PubUnit>::scheduleHeartbeatLoop() {
  if (FLAGS_fsdb_publisher_heartbeat_interval_secs != 0) {
    XLOG(DBG2) << "Scheduling FsdbPublisher.HeartbeatLoop(interval: "
               << FLAGS_fsdb_publisher_heartbeat_interval_secs << " seconds)";
    functionScheduler_.addFunction(
        [this]() {
          auto evb = getStreamEventBase();
          evb->runInEventBaseThread([this]() { sendHeartbeat(); });
        },
        std::chrono::seconds(FLAGS_fsdb_publisher_heartbeat_interval_secs),
        "FsdbPublisherHeartbeat");
    functionScheduler_.start();
  }
}

template <typename PubUnit>
void FsdbPublisher<PubUnit>::cancelHeartbeatLoop() {
  if (FLAGS_fsdb_publisher_heartbeat_interval_secs != 0) {
    XLOG(DBG2) << "Cancelling FsdbPublisher.HeartbeatLoop";
    functionScheduler_.cancelFunction("FsdbPublisherHeartbeat");
    functionScheduler_.shutdown();
  }
}

template class FsdbPublisher<OperDelta>;
template class FsdbPublisher<OperState>;
template class FsdbPublisher<Patch>;

} // namespace facebook::fboss::fsdb
