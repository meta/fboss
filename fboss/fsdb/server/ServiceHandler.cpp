// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/fsdb/server/ServiceHandler.h"
#include "fboss/fsdb/if/gen-cpp2/fsdb_common_types.h"

#include <fboss/lib/LogThriftCall.h>
#include <folly/Try.h>
#include <folly/coro/BlockingWait.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp2/server/Cpp2Worker.h>
#include "fboss/fsdb/common/Flags.h"
#include "fboss/fsdb/common/Utils.h"
#include "fboss/fsdb/if/gen-cpp2/fsdb_common_constants.h"
#include "fboss/fsdb/oper/DeltaValue.h"
#include "fboss/fsdb/oper/PathValidator.h"
#include "fboss/fsdb/oper/SubscriptionCommon.h"
#include "folly/CancellationToken.h"

#include <algorithm>
#include <chrono>
#include <iterator>

using namespace std::chrono_literals; // @donotremove

namespace apache {
namespace thrift {
class ThriftServer;
} // namespace thrift
} // namespace apache

DEFINE_int32(metricsTtl_s, 1 * 12 * 3600 /* twelve hours */, "TTL for metrics");

DEFINE_bool(
    checkOperOwnership,
    false,
    "Enable/Disable permission checks on publisher id");

DEFINE_bool(trackMetadata, true, "Enable metadata tracking");

DEFINE_int32(
    statsSubscriptionServe_ms,
    10000,
    "Interval at which stats subscriptions are served");

// 0 serves every subscriber at the default cadence; it never disables stats
// serving.
DEFINE_int32(
    statsSubscriptionServeTick_ms,
    0,
    "Granularity of the stats serve loop; 0 serves everything at the default interval");

// Off by default: auto-exported per switch, so enable only while debugging.
DEFINE_bool(
    statsExportPerBucketServeMetrics,
    false,
    "Export serve latency/count per serve-interval bucket on the stats storage");

// queue size for serving stats subscriptions is chosen
// to accommodate pending updates generated over 1 min interval
// (6 updates + 2 heartbeats).
// This limits memory usage on server due to subscription serve
// queue while not disconnecting subscriber aggressively.
DEFINE_int32(
    statsSubscriptionServeQueueSize,
    8,
    "Max stats subscription updates to hold in server queue");

DEFINE_int32(
    statsSubscriptionHeartbeat_s,
    30,
    "Interval at which heartbeats are sent for stats subscribers");

DEFINE_int32(
    stateSubscriptionServe_ms,
    50,
    "Interval at which state subscriptions are served");

DEFINE_int32(
    stateSubscriptionHeartbeat_s,
    5,
    "Interval at which heartbeats are sent for state subscribers");

DEFINE_int32(
    deltaSubscriptionQueueFullMinSize,
    5,
    "minimum number of pending updates to trigger memory-based subscription queue full detection");

DEFINE_int32(
    deltaSubscriptionQueueMemoryLimit_mb,
    0,
    "total memory size of queued updates for delta subscription at which memory-based subscription queue full detection is triggered (0 to disable)");

DEFINE_bool(
    checkSubscriberConfig,
    true,
    "whether to check SubscriberConfig on subscription requests");

DEFINE_bool(
    enforceSubscriberConfig,
    false,
    "whether to enforce SubscriberConfig on subscription requests");

DEFINE_bool(
    enforcePublisherConfig,
    false,
    "whether to enforce PublisherConfig for publish stream requests");
DEFINE_bool(
    forceRegisterSubscriptions,
    false,
    "Whether to bypass unique subscriber check. Should only be used during debugging");
DEFINE_bool(
    enableHybridStateStorage,
    false,
    "Whether to use hybrid thrift-cow storage for the FSDB state tree");

static constexpr auto kWatchdogThreadHeartbeatMissed =
    "watchdog_thread_heartbeat_missed";
static constexpr auto kHybridStateStorageEnabled =
    "hybrid_state_storage_enabled";

namespace {

using facebook::fboss::fsdb::ExtendedOperPath;
using facebook::fboss::fsdb::OperGetRequest;
using facebook::fboss::fsdb::OperPath;
using facebook::fboss::fsdb::OperPubRequest;
using facebook::fboss::fsdb::OperSubRequest;
using facebook::fboss::fsdb::OperSubRequestExtended;
using facebook::fboss::fsdb::Path;
using facebook::fboss::fsdb::PubRequest;
using facebook::fboss::fsdb::SubRequest;

// Both intervals are operator-settable, so resolve the pair into a usable tick
// rather than let StorageParams' CHECKs abort a Tier-0 process on a bad gflag.
std::chrono::milliseconds resolveStatsServeTick() {
  const int32_t defaultMs = FLAGS_statsSubscriptionServe_ms;
  const int32_t requested = FLAGS_statsSubscriptionServeTick_ms;
  const uint32_t tick = facebook::fboss::fsdb::resolveServeTickMs(
      requested, static_cast<uint32_t>(defaultMs));
  XLOG_IF(WARNING, requested > 0 && static_cast<int32_t>(tick) != requested)
      << "statsSubscriptionServeTick_ms " << requested << " is unusable against"
      << " statsSubscriptionServe_ms " << defaultMs << "; serving at " << tick
      << " instead";
  return std::chrono::milliseconds(tick);
}

// Strip leading and trailing ':' from a SubscriberConfig key so wildcard
// forms (":agent" prefix wildcard or "agent:" suffix wildcard) parse to
// the same FsdbClient as a literal key via subscriberId2ClientId().
std::string stripWildcardColons(const std::string& key) {
  std::string result = key;
  while (!result.empty() && result.front() == ':') {
    result.erase(0, 1);
  }
  while (!result.empty() && result.back() == ':') {
    result.pop_back();
  }
  return result;
}

template <typename OperRequest>
std::string getRequestDetails(const OperRequest& request) {
  static_assert(
      std::is_same_v<OperRequest, OperPubRequest> ||
      std::is_same_v<OperRequest, OperSubRequest> ||
      std::is_same_v<OperRequest, OperSubRequestExtended> ||
      std::is_same_v<OperRequest, OperGetRequest> ||
      std::is_same_v<OperRequest, SubRequest> ||
      std::is_same_v<OperRequest, PubRequest>);
  std::string clientID;
  if constexpr (
      std::is_same_v<OperRequest, PubRequest> ||
      std::is_same_v<OperRequest, SubRequest>) {
    clientID = *request.clientId()->instanceId();
  } else if constexpr (std::is_same_v<OperRequest, OperPubRequest>) {
    clientID = request.get_publisherId();
  } else if constexpr (std::is_same_v<OperRequest, OperSubRequest>) {
    clientID = request.get_subscriberId();
  } else if constexpr (std::is_same_v<OperRequest, OperSubRequestExtended>) {
    clientID = request.get_subscriberId();
  } else if constexpr (std::is_same_v<OperRequest, OperGetRequest>) {
    // TODO: enforce clientId on polling apis
    clientID = "adhoc";
  }
  std::string pathStr;
  if constexpr (
      std::is_same_v<OperRequest, OperPubRequest> ||
      std::is_same_v<OperRequest, OperSubRequest>) {
    pathStr = folly::join("/", request.path()->get_raw());
  } else if constexpr (std::is_same_v<OperRequest, PubRequest>) {
    pathStr = folly::join("/", request.path()->get_path());
  } else if constexpr (std::is_same_v<OperRequest, SubRequest>) {
    std::vector<std::string> pathStrings;
    pathStrings.reserve(request.paths()->size());
    for (const auto& path : *request.paths()) {
      pathStrings.push_back(
          fmt::format(
              "{}:{}", path.first, folly::join("/", *path.second.path())));
    }
    pathStr = folly::join(", ", std::move(pathStrings));
  } else if constexpr (std::is_same_v<OperRequest, OperSubRequestExtended>) {
    // TODO: set path str for extended subs
  }
  return fmt::format("Client ID: {}, Path: {}", std::move(clientID), pathStr);
}

Path buildPathUnion(facebook::fboss::fsdb::OperSubscriberInfo info) {
  Path pathUnion;
  if (info.path() && info.path()->raw()->size() > 0) {
    OperPath operPath;
    operPath.raw() = *info.path()->raw();
    pathUnion.set_operPath(operPath);
  } else if (info.extendedPaths() && info.extendedPaths()->size() > 0) {
    std::vector<ExtendedOperPath> extendedPaths;
    pathUnion.set_extendedPaths(*info.extendedPaths());
  } else if (info.paths() && info.paths()->size() > 0) {
    pathUnion.set_rawPaths(*info.paths());
  }
  return pathUnion;
}

Path buildPathUnion(facebook::fboss::fsdb::OperPublisherInfo info) {
  Path pathUnion;
  OperPath operPath;
  operPath.raw() = *info.path()->raw();
  pathUnion.set_operPath(operPath);
  return pathUnion;
}

void updateMetadata(facebook::fboss::fsdb::OperMetadata& metadata) {
  // Timestamp at server if chunk was not timestamped
  // by publisher
  if (!metadata.lastConfirmedAt() || !metadata.lastPublishedAt()) {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    if (!metadata.lastConfirmedAt()) {
      metadata.lastConfirmedAt() =
          std::chrono::duration_cast<std::chrono::seconds>(now).count();
    }
    if (!metadata.lastPublishedAt()) {
      metadata.lastPublishedAt() =
          std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    }
  }
}

template <typename Request>
facebook::fboss::fsdb::SubscriptionStorageParams subscriptionParamsFromRequest(
    const Request& request) {
  facebook::fboss::fsdb::SubscriptionStorageParams params;
  if (request.heartbeatInterval().has_value()) {
    params.heartbeatInterval_ =
        std::chrono::seconds(request.heartbeatInterval().value());
  }
  // Any positive value is accepted: the storage rounds it up to a whole tick
  // and clamps it to the default interval. A subscription type that does not
  // support intervals ignores it rather than failing the subscribe.
  if (request.serveIntervalSec().has_value() &&
      request.serveIntervalSec().value() > 0) {
    params.serveIntervalMs_ =
        static_cast<uint64_t>(request.serveIntervalSec().value()) * 1000;
  }
  return params;
}

} // namespace

namespace facebook::fboss::fsdb {

ServiceHandler::ServiceHandler(
    std::shared_ptr<FsdbConfig> fsdbConfig,
    Options options)
    : FacebookBase2("FsdbService"),
      fsdbConfig_(std::move(fsdbConfig)),
      options_(std::move(options)),
      num_instances_(
          fb303::ThreadCachedServiceData::get()->getThreadStats(),
          folly::to<std::string>(
              fsdb_common_constants::kFsdbServiceHandlerNativeStatsPrefix(),
              "num_instances")),
      num_publishers_(
          fb303::ThreadCachedServiceData::get()->getThreadStats(),
          folly::to<std::string>(
              fsdb_common_constants::kFsdbServiceHandlerNativeStatsPrefix(),
              "num_publishers")),
      num_subscribers_(
          fb303::ThreadCachedServiceData::get()->getThreadStats(),
          folly::to<std::string>(
              fsdb_common_constants::kFsdbServiceHandlerNativeStatsPrefix(),
              "num_subscribers")),
      num_subscriptions_(
          fb303::ThreadCachedServiceData::get()->getThreadStats(),
          folly::to<std::string>(
              fsdb_common_constants::kFsdbServiceHandlerNativeStatsPrefix(),
              "num_subscriptions")),
      num_disconnected_subscribers_(
          fb303::ThreadCachedServiceData::get()->getThreadStats(),
          folly::to<std::string>(
              fsdb_common_constants::kFsdbServiceHandlerNativeStatsPrefix(),
              "num_disconnected_subscribers")),
      num_disconnected_subscriptions_(
          fb303::ThreadCachedServiceData::get()->getThreadStats(),
          folly::to<std::string>(
              fsdb_common_constants::kFsdbServiceHandlerNativeStatsPrefix(),
              "num_disconnected_subscriptions")),
      num_disconnected_publishers_(
          fb303::ThreadCachedServiceData::get()->getThreadStats(),
          folly::to<std::string>(
              fsdb_common_constants::kFsdbServiceHandlerNativeStatsPrefix(),
              "num_disconnected_publishers")),
      num_subscriptions_rejected_(
          fb303::ThreadCachedServiceData::get()->getThreadStats(),
          folly::to<std::string>(
              fsdb_common_constants::kFsdbServiceHandlerNativeStatsPrefix(),
              "num_subscriptions_disallowed"),
          fb303::SUM,
          fb303::RATE),
      num_publisher_unknown_requests_rejected_(
          fb303::ThreadCachedServiceData::get()->getThreadStats(),
          folly::to<std::string>(
              fsdb_common_constants::kFsdbServiceHandlerNativeStatsPrefix(),
              "num_publisher_unknown_requests_rejected"),
          fb303::SUM,
          fb303::RATE),
      num_publisher_path_requests_rejected_(
          fb303::ThreadCachedServiceData::get()->getThreadStats(),
          folly::to<std::string>(
              fsdb_common_constants::kFsdbServiceHandlerNativeStatsPrefix(),
              "num_publisher_path_requests_rejected"),
          fb303::SUM,
          fb303::RATE),
      num_dropped_stats_changes_(
          fb303::ThreadCachedServiceData::get()->getThreadStats(),
          folly::to<std::string>(
              fsdb_common_constants::kFsdbServiceHandlerNativeStatsPrefix(),
              "num_dropped_stats_changes"),
          fb303::SUM,
          fb303::RATE),
      num_dropped_state_changes_(
          fb303::ThreadCachedServiceData::get()->getThreadStats(),
          folly::to<std::string>(
              fsdb_common_constants::kFsdbServiceHandlerNativeStatsPrefix(),
              "num_dropped_state_changes"),
          fb303::SUM,
          fb303::RATE),
      operStatsStorage_(
          {},
          NaivePeriodicSubscribableStorageBase::StorageParams(
              std::chrono::milliseconds(FLAGS_statsSubscriptionServe_ms),
              std::chrono::seconds(FLAGS_statsSubscriptionHeartbeat_s),
              FLAGS_trackMetadata,
              "stats",
              options_.serveIdPathSubs,
              true,
              true,
              true /* serveGetRequestsWithLastPublishedState */,
              FLAGS_statsSubscriptionServeQueueSize,
              FLAGS_statsSubscriptionServeQueueSize)
              .setServeTickInterval(resolveStatsServeTick())
              .setExportPerBucketServeMetrics(
                  FLAGS_statsExportPerBucketServeMetrics)
              .setDeltaSubscriptionQueueFullMinSize(
                  FLAGS_deltaSubscriptionQueueFullMinSize)
              .setDeltaSubscriptionQueueMemoryLimit(
                  FLAGS_deltaSubscriptionQueueMemoryLimit_mb * 1024 * 1024)) {
  // StorageParams holds metricPrefix by reference.
  const std::string metricPrefix{"fsdb"};
  auto stateStorageParams =
      NaivePeriodicSubscribableStorageBase::StorageParams(
          std::chrono::milliseconds(FLAGS_stateSubscriptionServe_ms),
          std::chrono::seconds(FLAGS_stateSubscriptionHeartbeat_s),
          FLAGS_trackMetadata,
          metricPrefix,
          options_.serveIdPathSubs,
          true,
          true)
          .setDeltaSubscriptionQueueFullMinSize(
              FLAGS_deltaSubscriptionQueueFullMinSize)
          .setDeltaSubscriptionQueueMemoryLimit(
              FLAGS_deltaSubscriptionQueueMemoryLimit_mb * 1024 * 1024);
  operStorage_ = makeFsdbStateStorage(
      FLAGS_enableHybridStateStorage, FsdbOperStateRoot{}, stateStorageParams);
  XLOG(INFO) << "FSDB state storage mode: "
             << (operStorage_->usingHybridStorage() ? "hybrid" : "cow");
  tcData().setCounter(
      kHybridStateStorageEnabled, operStorage_->usingHybridStorage() ? 1 : 0);

  num_instances_.incrementValue(1);

  initPerStreamCounters();

  stateStorageBase().start_impl();
  operStatsStorage_.start();
  tcData().setCounter(kWatchdogThreadHeartbeatMissed, 0);

  // Create a watchdog that will monitor operStorage_ and operStatsStorage_
  // increment the missed counter when there is no heartbeat on at least one
  // thread in the last FLAGS_storage_thread_heartbeat_ms * 10 time
  XLOG(DBG1) << "Starting fsdb ServiceHandler thread heartbeat watchdog";
  heartbeatWatchdog_ = std::make_unique<ThreadHeartbeatWatchdog>(
      std::chrono::milliseconds(FLAGS_storage_thread_heartbeat_ms * 10),
      [this]() {
        watchdogThreadHeartbeatMissedCount_ += 1;
        tcData().setCounter(
            kWatchdogThreadHeartbeatMissed,
            watchdogThreadHeartbeatMissedCount_);
        // Surface liveness loss for OSS vendors without fb303/ODS.
        XLOG(ERR) << "FSDB storage thread heartbeat missed (count="
                  << watchdogThreadHeartbeatMissedCount_ << ")";
      });
  heartbeatWatchdog_->startMonitoringHeartbeat(
      stateStorageBase().getThreadHeartbeat());
  heartbeatWatchdog_->startMonitoringHeartbeat(
      operStatsStorage_.getThreadHeartbeat());
  heartbeatWatchdog_->start();
}

ServiceHandler::~ServiceHandler() {
  if (heartbeatWatchdog_) {
    XLOG(DBG1) << "Stopping fsdb ServiceHandler thread heartbeat watchdog";
    heartbeatWatchdog_->stop();
    heartbeatWatchdog_.reset();
  }
  XLOG(INFO) << "Destroying ServiceHandler";
  num_instances_.incrementValue(-1);
}

FsdbOperStatsRoot ServiceHandler::operStatsRootExpensive() const {
  return operStatsStorage_.currentStateExpensive();
}

OperPublisherInfo ServiceHandler::makePublisherInfo(
    const RawPathT& path,
    const PublisherId& publisherId,
    PubSubType type,
    bool isStats) {
  OperPublisherInfo info;
  info.publisherId() = publisherId;
  info.type() = type;
  info.path()->raw() = path;
  info.isStats() = isStats;
  try {
    auto pathConfig = fsdbConfig_->getPathConfig(publisherId, path);
    info.isExpectedPath() = *pathConfig.get().isExpected();
  } catch (const std::exception&) {
    // ignore exception if PathConfig is not available
  }
  return info;
}

void ServiceHandler::registerPublisher(
    const OperPublisherInfo& info,
    std::shared_ptr<folly::Synchronized<PublisherStreamState>> streamState) {
  XLOG(DBG2) << "Publisher connected " << *info.publisherId() << " : "
             << folly::join("/", *info.path()->raw());
  if (info.publisherId()->empty()) {
    // Per-id throttle protects against a buggy client that retries connect
    // in a tight loop. Watchdog still fires once per second per call site.
    XLOG(WARN) << "FSDB rejected publisher: EMPTY_PUBLISHER_ID path="
               << folly::join("/", *info.path()->raw())
               << " isStats=" << *info.isStats();
    throw Utils::createFsdbException(
        FsdbErrorCode::EMPTY_PUBLISHER_ID, "Publisher Id must not be empty");
  }
  auto key = ClientKey(
      *info.publisherId(), buildPathUnion(info), *info.type(), *info.isStats());
  ActivePublisherEntry entry{info, std::move(streamState)};
  auto resp =
      activePublishers_.wlock()->insert({std::move(key), std::move(entry)});
  if (!resp.second) {
    XLOG(WARN) << "FSDB rejected publisher: ID_ALREADY_EXISTS publisherId="
               << *info.publisherId()
               << " path=" << folly::join("/", *info.path()->raw())
               << " isStats=" << *info.isStats();
    throw Utils::createFsdbException(
        FsdbErrorCode::ID_ALREADY_EXISTS, "Dup publisher id");
  }

  auto config = fsdbConfig_->getPublisherConfig(*info.publisherId());
  bool skipThriftStreamLivenessCheck = config.has_value() &&
      *config.value().get().skipThriftStreamLivenessCheck();

  if (*info.isStats()) {
    operStatsStorage_.registerPublisher(
        info.path()->raw()->begin(),
        info.path()->raw()->end(),
        skipThriftStreamLivenessCheck);
  } else {
    stateStorageBase().registerPublisher(
        info.path()->raw()->begin(),
        info.path()->raw()->end(),
        skipThriftStreamLivenessCheck);
  }
  num_publishers_.incrementValue(1);
  try {
    auto pathConfig =
        fsdbConfig_->getPathConfig(*info.publisherId(), *info.path()->raw());
    if (*pathConfig.get().isExpected()) {
      num_disconnected_publishers_.incrementValue(-1);
      auto counter = disconnectedPublishers_.find(
          PublisherKey(*info.publisherId(), *info.isStats()));
      if (counter != disconnectedPublishers_.end()) {
        counter->second.incrementValue(-1);
      }
    }
  } catch (const std::exception&) {
    // ignore exception if PathConfig is not available
  };
}

void ServiceHandler::unregisterPublisher(
    const OperPublisherInfo& info,
    FsdbErrorCode disconnectReason) {
  XLOG(DBG2) << " Publisher complete " << *info.publisherId() << " : "
             << folly::join("/", *info.path()->raw()) << " disconnectReason: "
             << apache::thrift::util::enumNameSafe(disconnectReason);
  auto key = ClientKey(
      *info.publisherId(), buildPathUnion(info), *info.type(), *info.isStats());
  activePublishers_.wlock()->erase(std::move(key));
  if (*info.isStats()) {
    operStatsStorage_.unregisterPublisher(
        info.path()->raw()->begin(),
        info.path()->raw()->end(),
        disconnectReason);
  } else {
    stateStorageBase().unregisterPublisher(
        info.path()->raw()->begin(),
        info.path()->raw()->end(),
        disconnectReason);
  }
  num_publishers_.incrementValue(-1);
  try {
    auto config =
        fsdbConfig_->getPathConfig(*info.publisherId(), *info.path()->raw());
    if (*config.get().isExpected()) {
      num_disconnected_publishers_.incrementValue(1);
      auto counter = disconnectedPublishers_.find(
          PublisherKey(*info.publisherId(), *info.isStats()));
      if (counter != disconnectedPublishers_.end()) {
        counter->second.incrementValue(1);
      }
    }
  } catch (const std::exception&) {
    // ignore exception if PathConfig is not available
  };
}

template <typename PubUnit>
apache::thrift::SinkConsumer<PubUnit, OperPubFinalResponse>
ServiceHandler::makeSinkConsumer(
    RawPathT&& path,
    const PublisherId& publisherId,
    bool isStats) {
  validateOperPublishPermissions(publisherId, path);
  PubSubType pubSubType{PubSubType::PATH};
  if constexpr (std::is_same_v<PubUnit, OperDelta>) {
    pubSubType = PubSubType::DELTA;
  } else if constexpr (std::is_same_v<PubUnit, PublisherMessage>) {
    pubSubType = PubSubType::PATCH;
  }

  auto info = makePublisherInfo(path, publisherId, pubSubType, isStats);
  auto streamState =
      std::make_shared<folly::Synchronized<PublisherStreamState>>();
  streamState->wlock()->connectedAt = std::time(nullptr);
  registerPublisher(info, streamState);
  std::shared_ptr<FsdbErrorCode> disconnectReason =
      std::make_shared<FsdbErrorCode>(FsdbErrorCode::ALL_PUBLISHERS_GONE);
  auto cleanupPublisher =
      folly::makeGuard([this, info = std::move(info), disconnectReason]() {
        unregisterPublisher(info, *disconnectReason);
      });

  apache::thrift::SinkConsumer<PubUnit, OperPubFinalResponse> consumer{
      [this,
       publisherId,
       disconnectReason = std::move(disconnectReason),
       cleanupPublisher = std::move(cleanupPublisher),
       path = std::move(path),
       isStats,
       streamState =
           std::move(streamState)](folly::coro::AsyncGenerator<PubUnit&&> gen)
          -> folly::coro::Task<OperPubFinalResponse> {
        OperPubFinalResponse finalResponse;
        try {
          while (auto chunk = co_await gen.next()) {
            XLOG(DBG5) << " chunk received";
            std::optional<StorageError> patchErr;
            bool isHeartbeat = false;
            size_t chunkSize = 0;
            if constexpr (std::is_same_v<PubUnit, OperState>) {
              updateMetadata(chunk->metadata().ensure());
              isHeartbeat = *chunk->isHeartbeat();
              if (!isHeartbeat && chunk->contents().has_value()) {
                chunkSize = chunk->contents()->size();
              }
              if (isStats) {
                if (isHeartbeat) {
                  operStatsStorage_.publisherHeartbeat(
                      path.begin(), path.end(), chunk->metadata().ensure());
                } else {
                  patchErr = operStatsStorage_.set_encoded(
                      path.begin(), path.end(), *chunk);
                }
              } else {
                if (isHeartbeat) {
                  stateStorageBase().publisherHeartbeat(
                      path.begin(), path.end(), chunk->metadata().ensure());
                } else {
                  patchErr = operStorage_->set_encoded(
                      path.begin(), path.end(), *chunk);
                }
              }
            } else if constexpr (std::is_same_v<PubUnit, OperDelta>) {
              updateMetadata(chunk->metadata().ensure());
              // filter out invalid paths for cases where publisher is newer
              // than fsdb
              auto isPathValid = isStats ? PathValidator::isStatsPathValid
                                         : PathValidator::isStatePathValid;
              auto numChanges = chunk->changes()->size();
              chunk->changes()->erase(
                  std::remove_if(
                      chunk->changes()->begin(),
                      chunk->changes()->end(),
                      [isPathValid](auto& change) {
                        return !isPathValid(*change.path()->raw());
                      }),
                  chunk->changes()->end());

              isHeartbeat = chunk->changes()->empty();
              if (!isHeartbeat) {
                for (const auto& change : *chunk->changes()) {
                  if (change.oldState().has_value()) {
                    chunkSize += change.oldState()->size();
                  }
                  if (change.newState().has_value()) {
                    chunkSize += change.newState()->size();
                  }
                }
              }
              if (isStats) {
                if (isHeartbeat) {
                  operStatsStorage_.publisherHeartbeat(
                      path.begin(), path.end(), chunk->metadata().ensure());
                } else {
                  patchErr = operStatsStorage_.patch(*chunk);
                }
              } else {
                if (isHeartbeat) {
                  stateStorageBase().publisherHeartbeat(
                      path.begin(), path.end(), chunk->metadata().ensure());
                } else {
                  patchErr = operStorage_->patch(*chunk);
                }
              }
              auto numDropped = numChanges - chunk->changes()->size();
              if (numDropped) {
                XLOG(DBG2) << "Dropping " << numDropped << " changes from "
                           << (isStats ? "stats" : "state")
                           << " chunk with invalid path";
                if (isStats) {
                  num_dropped_stats_changes_.addValue(numDropped);
                } else {
                  num_dropped_state_changes_.addValue(numDropped);
                }
              }
            } else if constexpr (std::is_same_v<PubUnit, PublisherMessage>) {
              if (chunk->getType() == PublisherMessage::Type::heartbeat) {
                auto heartbeat = chunk->heartbeat_ref();
                if (heartbeat->metadata().has_value()) {
                  if (isStats) {
                    operStatsStorage_.publisherHeartbeat(
                        path.begin(),
                        path.end(),
                        heartbeat->metadata().value());
                  } else {
                    stateStorageBase().publisherHeartbeat(
                        path.begin(),
                        path.end(),
                        heartbeat->metadata().value());
                  }
                }
                // Update heartbeat timestamp before continuing
                {
                  auto state = streamState->wlock();
                  auto nowMs = getCurrentTimeMs();
                  state->lastHeartbeatReceivedAt = nowMs;
                }
                continue;
              }
              auto patchChunk = chunk->move_patch();
              chunkSize = getPatchNodeSize(*patchChunk.patch());
              for (const auto& pathElem : *patchChunk.basePath()) {
                chunkSize += pathElem.size();
              }
              updateMetadata(*patchChunk.metadata());
              if (isStats) {
                patchErr = operStatsStorage_.patch(std::move(patchChunk));
              } else {
                patchErr = operStorage_->patch(std::move(patchChunk));
              }
            }

            // Update per-stream tracking state
            {
              auto state = streamState->wlock();
              auto nowMs = getCurrentTimeMs();
              if (isHeartbeat) {
                state->lastHeartbeatReceivedAt = nowMs;
              } else {
                state->lastUpdateReceivedAt = nowMs;
                state->numUpdatesReceived++;
                state->receivedDataSize += chunkSize;
                if (!patchErr) {
                  state->lastUpdatePublishedAt = nowMs;
                  if (state->initialSyncCompletedAt == 0) {
                    state->initialSyncCompletedAt = nowMs;
                  }
                }
              }
            }

            XLOG(DBG5) << "Chunk patch result "
                       << (patchErr
                               ? fmt::format("error: {}", patchErr->toString())
                               : "success");
          }
          co_return finalResponse;
        } catch (const fsdb::FsdbException& ex) {
          XLOG(ERR) << "Publisher " << publisherId
                    << " Server:sink: FsdbException: "
                    << apache::thrift::util::enumNameSafe(
                           folly::copy(ex.errorCode().value()))
                    << ": " << ex.message().value();
          *disconnectReason = ex.errorCode().value();
          throw;
        } catch (const std::exception& e) {
          XLOG(INFO) << "Publisher " << publisherId
                     << " Got exception in publish loop: " << e.what();
          throw;
        }
      },
      10 /* bufferSize */};
  return consumer;
}

folly::coro::Task<apache::thrift::ResponseAndSinkConsumer<
    OperPubInitResponse,
    OperState,
    OperPubFinalResponse>>
ServiceHandler::co_publishOperStatePath(
    std::unique_ptr<OperPubRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO, getRequestDetails(*request));
  PathValidator::validateStatePath(*request->path()->raw());
  co_return {
      {},
      makeSinkConsumer<OperState>(
          std::move(*request->path()->raw()), *request->publisherId(), false)};
}

folly::coro::Task<apache::thrift::ResponseAndSinkConsumer<
    OperPubInitResponse,
    OperState,
    OperPubFinalResponse>>
ServiceHandler::co_publishOperStatsPath(
    std::unique_ptr<OperPubRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO, getRequestDetails(*request));
  PathValidator::validateStatsPath(*request->path()->raw());
  co_return {
      {},
      makeSinkConsumer<OperState>(
          std::move(*request->path()->raw()), *request->publisherId(), true)};
}

folly::coro::Task<apache::thrift::ResponseAndSinkConsumer<
    OperPubInitResponse,
    OperDelta,
    OperPubFinalResponse>>
ServiceHandler::co_publishOperStateDelta(
    std::unique_ptr<OperPubRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO, getRequestDetails(*request));
  PathValidator::validateStatePath(*request->path()->raw());
  co_return {
      {},
      makeSinkConsumer<OperDelta>(
          std::move(*request->path()->raw()), *request->publisherId(), false)};
}

folly::coro::Task<apache::thrift::ResponseAndSinkConsumer<
    OperPubInitResponse,
    OperDelta,
    OperPubFinalResponse>>
ServiceHandler::co_publishOperStatsDelta(
    std::unique_ptr<OperPubRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO, getRequestDetails(*request));
  PathValidator::validateStatsPath(*request->path()->raw());
  co_return {
      {},
      makeSinkConsumer<OperDelta>(
          std::move(*request->path()->raw()), *request->publisherId(), true)};
}

folly::coro::Task<apache::thrift::ResponseAndSinkConsumer<
    OperPubInitResponse,
    PublisherMessage,
    OperPubFinalResponse>>
ServiceHandler::co_publishState(std::unique_ptr<PubRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO, getRequestDetails(*request));
  PathValidator::validateStatePath(*request->path()->path());
  co_return {
      {},
      makeSinkConsumer<PublisherMessage>(
          std::move(*request->path()->path()),
          *request->clientId()->instanceId(),
          false)};
}

folly::coro::Task<apache::thrift::ResponseAndSinkConsumer<
    OperPubInitResponse,
    PublisherMessage,
    OperPubFinalResponse>>
ServiceHandler::co_publishStats(std::unique_ptr<PubRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO, getRequestDetails(*request));
  PathValidator::validateStatsPath(*request->path()->path());
  co_return {
      {},
      makeSinkConsumer<PublisherMessage>(
          std::move(*request->path()->path()),
          *request->clientId()->instanceId(),
          true)};
}

namespace {

OperSubscriberInfo makeSubscriberInfo(
    const OperSubRequest& req,
    PubSubType type,
    bool isStats,
    uint64_t uid) {
  OperSubscriberInfo info;
  info.subscriberId() = *req.subscriberId();
  info.type() = type;
  info.path() = *req.path();
  info.isStats() = isStats;
  info.subscribedSince() = static_cast<int64_t>(std::time(nullptr));
  info.subscriptionUid() = uid;
  return info;
}

OperSubscriberInfo makeSubscriberInfo(
    const OperSubRequestExtended& req,
    PubSubType type,
    bool isStats,
    uint64_t uid) {
  OperSubscriberInfo info;
  info.subscriberId() = *req.subscriberId();
  info.type() = type;
  info.extendedPaths() = *req.paths();
  info.isStats() = isStats;
  info.subscribedSince() = static_cast<int64_t>(std::time(nullptr));
  info.subscriptionUid() = uid;
  return info;
}

OperSubscriberInfo makeSubscriberInfo(
    const SubRequest& req,
    PubSubType type,
    bool isStats,
    uint64_t uid) {
  OperSubscriberInfo info;
  info.subscriberId() = *req.clientId()->instanceId();
  info.type() = type;
  info.paths() = *req.paths();
  if (!req.extPaths()->empty()) {
    std::vector<ExtendedOperPath> extPaths;
    for (const auto& kv : *req.extPaths()) {
      extPaths.push_back(kv.second);
    }
    info.extendedPaths() = extPaths;
  }
  info.isStats() = isStats;
  info.subscribedSince() = static_cast<int64_t>(std::time(nullptr));
  info.subscriptionUid() = uid;
  return info;
}

void validatePaths(
    const std::map<SubscriptionKey, RawOperPath>& paths,
    bool isStats) {
  auto validatePath = isStats ? PathValidator::validateStatsPath
                              : PathValidator::validateStatePath;
  for (const auto& path : paths) {
    validatePath(*path.second.path());
  }
}

// Serves a subscription stream, recording per-stream serve stats. Patch
// subscribe handlers create the subscription before returning its uid, so a
// live add-paths right after the init response finds it; a creation error is
// still reported on the stream. cleanupSubscriber lives as long as the stream.
template <typename T, typename Guard>
folly::coro::AsyncGenerator<T&&> serveStreamReader(
    folly::Try<SubscriptionStreamReader<SubscriptionServeQueueElement<T>>>
        streamReader,
    [[maybe_unused]] Guard cleanupSubscriber) {
  if (streamReader.hasException()) {
    co_yield folly::coro::co_error(streamReader.exception());
  }
  while (auto item = co_await streamReader->generator_.next()) {
    streamReader->streamInfo_->servedDataSize.fetch_add(
        (*item).allocatedBytes, std::memory_order_relaxed);
    streamReader->streamInfo_->numUpdatesServed.fetch_add(
        1, std::memory_order_relaxed);
    streamReader->streamInfo_->lastUpdateWrittenAt.store(
        getCurrentTimeMs(), std::memory_order_relaxed);
    co_yield std::move((*item).val);
  }
}

OperSubInitResponse makeOperSubInitResponse(
    uint64_t subscriptionUid,
    bool created) {
  OperSubInitResponse resp;
  // A uid that live add-paths cannot resolve is not advertised.
  if (created) {
    resp.subscriptionUid() = subscriptionUid;
  }
  return resp;
}

} // namespace

SubscriptionIdentifier ServiceHandler::makeSubscriptionIdentifier(
    const OperSubscriberInfo& info) {
  CHECK(info.subscriptionUid().has_value());
  return SubscriptionIdentifier(*info.subscriberId(), *info.subscriptionUid());
}

void ServiceHandler::updateSubscriptionCounters(
    const OperSubscriberInfo& info,
    bool isConnected,
    bool uniqueSubForClient) {
  auto connectedCountIncrement = isConnected ? 1 : -1;
  auto disconnectCountIncrement = isConnected ? -1 : 1;

  num_subscriptions_.incrementValue(connectedCountIncrement);

  auto config = fsdbConfig_->getSubscriberConfig(*info.subscriberId());
  if (config.has_value() && *config.value().second.get().trackReconnect()) {
    auto& clientId = config.value().first;
    num_disconnected_subscriptions_.incrementValue(disconnectCountIncrement);
    if (auto counter = disconnectedSubscriptions_.find(clientId);
        counter != disconnectedSubscriptions_.end()) {
      counter->second.incrementValue(disconnectCountIncrement);
    }
    if (auto counter = connectedSubscriptions_.find(clientId);
        counter != connectedSubscriptions_.end()) {
      counter->second.incrementValue(connectedCountIncrement);
      // per-subscriber counters: checks global subscription count
      bool isFirstSubscriptionConnected = isConnected && uniqueSubForClient;
      bool isLastSubscriptionDisconnected = !isConnected && uniqueSubForClient;
      if (isFirstSubscriptionConnected || isLastSubscriptionDisconnected) {
        num_subscribers_.incrementValue(connectedCountIncrement);
        num_disconnected_subscribers_.incrementValue(disconnectCountIncrement);
        if (auto counter1 = disconnectedSubscribers_.find(clientId);
            counter1 != disconnectedSubscribers_.end()) {
          counter1->second.incrementValue(disconnectCountIncrement);
        }
      }
    }
  }

  if (config.has_value()) {
    const auto& subCfg = config.value().second.get();
    if (subCfg.expectedSubscriptionType().has_value()) {
      // Pass the matched config key (not the runtime subscriberId) so that
      // wildcard configs whose subscribers carry non-canonical ids (e.g.
      // ":agent" matching "node:agent") still resolve to the right
      // FsdbClient counter.
      updateExpectedSubscriptionCounter(
          config.value().first, *subCfg.expectedSubscriptionType());
    }
  }
}

void ServiceHandler::updateExpectedSubscriptionCounter(
    const SubscriberId& configKey,
    ExpectedSubscriptionType expectedType) {
  auto client = *subscriberId2ClientId(stripWildcardColons(configKey)).client();
  auto it = expectedSubscriptionsCounters_.find(client);
  if (it == expectedSubscriptionsCounters_.end()) {
    return;
  }
  const auto& counterName = it->second;

  int64_t value = 0;
  activeSubscriptions_.withRLock([&](const auto& activeSubscriptions) {
    // Each active OperSubscriberInfo has its matched configKey cached
    // at register time, so this is an O(1) string compare per active sub.
    auto belongsToThisConfig = [&](const OperSubscriberInfo& sub) {
      return sub.configKey().has_value() && *sub.configKey() == configKey;
    };

    if (expectedType == ExpectedSubscriptionType::ALL) {
      // gauge=1 iff some subscriberId matching this config currently has
      // both a state and a stats subscription active. Short-circuit the
      // moment any subscriberId hits both kinds. F14FastMap + reserve
      // keeps the per-call alloc tight while we hold the RLock.
      folly::F14FastMap<SubscriberId, std::pair<bool, bool>> seen;
      seen.reserve(activeSubscriptions.size());
      for (const auto& [_, subs] : activeSubscriptions) {
        for (const auto& sub : subs) {
          if (!belongsToThisConfig(sub)) {
            continue;
          }
          auto& kinds = seen[*sub.subscriberId()];
          (*sub.isStats() ? kinds.second : kinds.first) = true;
          if (kinds.first && kinds.second) {
            value = 1;
            break;
          }
        }
        if (value == 1) {
          break;
        }
      }
    } else {
      // STATE or STATS: gauge=1 iff >=1 active sub of that kind matches
      // this config (any subscriberId).
      bool wantStats = (expectedType == ExpectedSubscriptionType::STATS);
      for (const auto& [_, subs] : activeSubscriptions) {
        for (const auto& sub : subs) {
          if (*sub.isStats() == wantStats && belongsToThisConfig(sub)) {
            value = 1;
            break;
          }
        }
        if (value == 1) {
          break;
        }
      }
    }
  });
  tcData().setCounter(counterName, value);
}

void ServiceHandler::registerSubscription(
    const OperSubscriberInfo& info,
    bool forceSubscribe) {
  if (info.subscriberId()->empty()) {
    XLOG(WARN) << "FSDB rejected subscriber: EMPTY_SUBSCRIBER_ID isStats="
               << *info.isStats();
    throw Utils::createFsdbException(
        FsdbErrorCode::EMPTY_SUBSCRIBER_ID, "Subscriber Id must not be empty");
  }
  XLOG(INFO) << "Registering subscription " << *info.subscriberId();
  bool hasRawPath = info.path() && !info.path()->raw()->empty();
  bool hasExtendedPath = info.extendedPaths() && !info.extendedPaths()->empty();
  bool hasMultiPaths = info.paths() && !info.paths()->empty();
  validateSubscriptionPermissions(
      *info.subscriberId(),
      *info.type(),
      *info.isStats(),
      hasRawPath || hasMultiPaths,
      hasExtendedPath);
  auto key = ClientKey(
      *info.subscriberId(),
      buildPathUnion(info),
      *info.type(),
      *info.isStats());
  // Cache the matched subscriber config key on the stored copy so
  // updateExpectedSubscriptionCounter() can do an O(1) field compare per
  // active sub instead of re-running the wildcard scan over all configs.
  // Skip the lookup entirely when no expected_subscriptions counters are
  // configured -- the cached field is only consulted by that code path.
  OperSubscriberInfo storedInfo = info;
  if (!expectedSubscriptionsCounters_.empty()) {
    if (auto cfg = fsdbConfig_->getSubscriberConfig(*info.subscriberId());
        cfg.has_value()) {
      storedInfo.configKey() = cfg.value().first;
    }
  }
  // update activeSubscriptions_ : TODO: move into SubscriptionManager
  bool forceRegisterNewSubscription =
      FLAGS_forceRegisterSubscriptions || forceSubscribe;
  int numSubsForClient{0};
  activeSubscriptions_.withWLock([&key,
                                  &info,
                                  &storedInfo,
                                  &numSubsForClient,
                                  forceRegisterNewSubscription](
                                     auto& activeSubscriptions) {
    if (forceRegisterNewSubscription) {
      // also track new subscription for same ClientKey
      auto it = activeSubscriptions.find(key);
      if (it == activeSubscriptions.end()) {
        // initializer_list elements are const, so build the vector
        // explicitly to actually move storedInfo (avoid degrading to
        // a copy via braced-init-list).
        std::vector<OperSubscriberInfo> subs;
        subs.emplace_back(std::move(storedInfo));
        activeSubscriptions.insert({std::move(key), std::move(subs)});
      } else {
        it->second.emplace_back(std::move(storedInfo));
      }
    } else {
      std::vector<OperSubscriberInfo> subs;
      subs.emplace_back(std::move(storedInfo));
      auto resp = activeSubscriptions.insert({std::move(key), std::move(subs)});
      if (!resp.second) {
        XLOG(WARN)
            << "FSDB rejected subscriber: ID_ALREADY_EXISTS subscriberId="
            << *info.subscriberId() << " isStats=" << *info.isStats();
        throw Utils::createFsdbException(
            FsdbErrorCode::ID_ALREADY_EXISTS,
            "Dup subscriber id: ",
            *info.subscriberId());
      }
    }
    // check for existing subs by same SubscriberId
    for (const auto& it : activeSubscriptions) {
      for (const auto& subscription : it.second) {
        if (std::get<0>(key) == subscription.subscriberId()) {
          numSubsForClient++;
        }
      }
    }
  });
  updateSubscriptionCounters(info, true, (numSubsForClient == 1));
}

void ServiceHandler::unregisterSubscription(const OperSubscriberInfo& info) {
  std::string pathStr;
  // TODO: handle extended path to string
  if (info.path()) {
    pathStr = folly::join("/", *info.path()->raw());
  }
  XLOG(INFO) << "Subscription complete " << *info.subscriberId() << " : "
             << pathStr;
  auto key = ClientKey(
      *info.subscriberId(),
      buildPathUnion(info),
      *info.type(),
      *info.isStats());
  int numSubsForClient{0};
  activeSubscriptions_.withWLock(
      [&key, &info, &numSubsForClient](auto& activeSubscriptions) {
        // check for existing subs by same SubscriberId
        for (const auto& it : activeSubscriptions) {
          for (const auto& subscription : it.second) {
            if (std::get<0>(key) == subscription.subscriberId()) {
              numSubsForClient++;
            }
          }
        }
        auto it = activeSubscriptions.find(key);
        if (it != activeSubscriptions.end()) {
          // remove active subscription with matching uid
          auto& subs = it->second;
          subs.erase(
              std::remove_if(
                  subs.begin(),
                  subs.end(),
                  [&info](const auto& sub) {
                    return (*info.subscriptionUid() == *sub.subscriptionUid());
                  }),
              subs.end());
          if (subs.size() == 0) {
            activeSubscriptions.erase(std::move(key));
          }
        }
      });
  updateSubscriptionCounters(info, false, (numSubsForClient == 1));
}

folly::coro::AsyncGenerator<DeltaValue<OperState>&&>
ServiceHandler::makeStateStreamGenerator(
    std::unique_ptr<OperSubRequest> request,
    bool isStats,
    SubscriptionIdentifier&& subId) {
  auto subscriptionParams = subscriptionParamsFromRequest(*request);

  return isStats ? operStatsStorage_.subscribe_encoded(
                       std::move(subId),
                       request->path()->raw()->begin(),
                       request->path()->raw()->end(),
                       *request->protocol(),
                       subscriptionParams)
                 : stateStorageBase().subscribe_encoded_impl(
                       std::move(subId),
                       request->path()->raw()->begin(),
                       request->path()->raw()->end(),
                       *request->protocol(),
                       subscriptionParams);
}

folly::coro::AsyncGenerator<std::vector<DeltaValue<TaggedOperState>>&&>
ServiceHandler::makeExtendedStateStreamGenerator(
    std::unique_ptr<OperSubRequestExtended> request,
    bool isStats,
    SubscriptionIdentifier&& subId) {
  auto subscriptionParams = subscriptionParamsFromRequest(*request);

  return isStats ? operStatsStorage_.subscribe_encoded_extended(
                       std::move(subId),
                       std::move(*request->paths()),
                       *request->protocol(),
                       subscriptionParams)
                 : stateStorageBase().subscribe_encoded_extended_impl(
                       std::move(subId),
                       std::move(*request->paths()),
                       *request->protocol(),
                       subscriptionParams);
}

SubscriptionStreamReader<SubscriptionServeQueueElement<SubscriberMessage>>
ServiceHandler::makePatchStreamGenerator(
    std::unique_ptr<SubRequest> request,
    bool isStats,
    SubscriptionIdentifier&& subId) {
  auto subscriptionParams = subscriptionParamsFromRequest(*request);

  if (!request->paths()->empty()) {
    auto streamReader = isStats
        ? operStatsStorage_.subscribe_patch(
              std::move(subId), *request->paths(), subscriptionParams)
        : stateStorageBase().subscribe_patch_impl(
              std::move(subId), *request->paths(), subscriptionParams);
    return streamReader;
  } else {
    CHECK_GT(request->extPaths()->size(), 0);
    auto streamReader = isStats
        ? operStatsStorage_.subscribe_patch_extended(
              std::move(subId), *request->extPaths(), subscriptionParams)
        : stateStorageBase().subscribe_patch_extended_impl(
              std::move(subId), *request->extPaths(), subscriptionParams);
    return streamReader;
  }
}

folly::coro::Task<
    apache::thrift::ResponseAndServerStream<OperSubInitResponse, OperState>>
ServiceHandler::co_subscribeOperStatePath(
    std::unique_ptr<OperSubRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO, getRequestDetails(*request));
  PathValidator::validateStatePath(*request->path()->raw());

  auto subscriberInfo = makeSubscriberInfo(
      *request, PubSubType::PATH, false, lastSubscriptionUid_.fetch_add(1));
  auto subId = makeSubscriptionIdentifier(subscriberInfo);
  registerSubscription(subscriberInfo, *request->forceSubscribe());
  auto cleanupSubscriber =
      folly::makeGuard([this, subscriberInfo = std::move(subscriberInfo)]() {
        unregisterSubscription(subscriberInfo);
      });

  co_return {
      {},
      folly::coro::co_invoke(
          [this,
           request = std::move(request),
           subId = std::move(subId),
           cleanupSubscriber = std::move(cleanupSubscriber)]() mutable
              -> folly::coro::AsyncGenerator<OperState&&> {
            auto generator = makeStateStreamGenerator(
                std::move(request), false, std::move(subId));
            while (auto item = co_await generator.next()) {
              // got value
              auto&& delta = *item;
              if (delta.newVal) {
                co_yield std::move(*delta.newVal);
              } else {
                // yield empty OperState path
                OperState empty;
                empty.protocol() = OperProtocol::BINARY;
                co_yield std::move(empty);
              }
            }
          })};
}

folly::coro::Task<
    apache::thrift::ResponseAndServerStream<OperSubInitResponse, OperState>>
ServiceHandler::co_subscribeOperStatsPath(
    std::unique_ptr<OperSubRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO, getRequestDetails(*request));
  PathValidator::validateStatsPath(*request->path()->raw());

  auto subscriberInfo = makeSubscriberInfo(
      *request, PubSubType::PATH, true, lastSubscriptionUid_.fetch_add(1));
  auto subId = makeSubscriptionIdentifier(subscriberInfo);
  registerSubscription(subscriberInfo, *request->forceSubscribe());
  auto cleanupSubscriber =
      folly::makeGuard([this, subscriberInfo = std::move(subscriberInfo)]() {
        unregisterSubscription(subscriberInfo);
      });

  co_return {
      {},
      folly::coro::co_invoke(
          [this,
           request = std::move(request),
           subId = std::move(subId),
           cleanupSubscriber = std::move(cleanupSubscriber)]() mutable
              -> folly::coro::AsyncGenerator<OperState&&> {
            auto generator = makeStateStreamGenerator(
                std::move(request), true, std::move(subId));
            while (auto item = co_await generator.next()) {
              // got value
              auto&& delta = *item;
              if (delta.newVal) {
                co_yield std::move(*delta.newVal);
              } else {
                // yield empty OperState path
                OperState empty;
                empty.protocol() = OperProtocol::BINARY;
                co_yield std::move(empty);
              }
            }
          })};
}

SubscriptionStreamReader<SubscriptionServeQueueElement<OperDelta>>
ServiceHandler::makeDeltaStreamGenerator(
    std::unique_ptr<OperSubRequest> request,
    bool isStats,
    SubscriptionIdentifier&& subId) {
  auto subscriptionParams = subscriptionParamsFromRequest(*request);

  auto streamReader = isStats ? operStatsStorage_.subscribe_delta(
                                    std::move(subId),
                                    request->path()->raw()->begin(),
                                    request->path()->raw()->end(),
                                    *request->protocol(),
                                    subscriptionParams)
                              : stateStorageBase().subscribe_delta_impl(
                                    std::move(subId),
                                    request->path()->raw()->begin(),
                                    request->path()->raw()->end(),
                                    *request->protocol(),
                                    subscriptionParams);
  return streamReader;
}

SubscriptionStreamReader<
    SubscriptionServeQueueElement<std::vector<TaggedOperDelta>>>
ServiceHandler::makeExtendedDeltaStreamGenerator(
    std::unique_ptr<OperSubRequestExtended> request,
    bool isStats,
    SubscriptionIdentifier&& subId) {
  auto subscriptionParams = subscriptionParamsFromRequest(*request);

  auto streamReader = isStats
      ? operStatsStorage_.subscribe_delta_extended(
            std::move(subId),
            *request->paths(),
            *request->protocol(),
            subscriptionParams)
      : stateStorageBase().subscribe_delta_extended_impl(
            std::move(subId),
            *request->paths(),
            *request->protocol(),
            subscriptionParams);
  return streamReader;
}

folly::coro::Task<
    apache::thrift::ResponseAndServerStream<OperSubInitResponse, OperDelta>>
ServiceHandler::co_subscribeOperStateDelta(
    std::unique_ptr<OperSubRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO, getRequestDetails(*request));
  PathValidator::validateStatePath(*request->path()->raw());

  auto subscriberInfo = makeSubscriberInfo(
      *request, PubSubType::DELTA, false, lastSubscriptionUid_.fetch_add(1));
  auto subId = makeSubscriptionIdentifier(subscriberInfo);
  registerSubscription(subscriberInfo, *request->forceSubscribe());
  auto cleanupSubscriber =
      folly::makeGuard([this, subscriberInfo = std::move(subscriberInfo)]() {
        unregisterSubscription(subscriberInfo);
      });

  co_return {
      {},
      folly::coro::co_invoke(
          [this,
           request = std::move(request),
           subId = std::move(subId),
           cleanupSubscriber = std::move(cleanupSubscriber)]() mutable
              -> folly::coro::AsyncGenerator<OperDelta&&> {
            auto streamReader = makeDeltaStreamGenerator(
                std::move(request), false, std::move(subId));
            while (auto item = co_await streamReader.generator_.next()) {
              // Update served data size and then yield
              streamReader.streamInfo_->servedDataSize.fetch_add(
                  (*item).allocatedBytes, std::memory_order_relaxed);
              streamReader.streamInfo_->numUpdatesServed.fetch_add(
                  1, std::memory_order_relaxed);
              streamReader.streamInfo_->lastUpdateWrittenAt.store(
                  getCurrentTimeMs(), std::memory_order_relaxed);
              co_yield std::move((*item).val);
            }
          })};
}

folly::coro::Task<apache::thrift::ResponseAndServerStream<
    OperSubInitResponse,
    OperSubPathUnit>>
ServiceHandler::co_subscribeOperStatePathExtended(
    std::unique_ptr<OperSubRequestExtended> request) {
  auto log = LOG_THRIFT_CALL(INFO, getRequestDetails(*request));

  PathValidator::validateExtendedStatePaths(*request->paths());

  auto subscriberInfo = makeSubscriberInfo(
      *request, PubSubType::PATH, false, lastSubscriptionUid_.fetch_add(1));
  auto subId = makeSubscriptionIdentifier(subscriberInfo);
  registerSubscription(subscriberInfo, *request->forceSubscribe());
  auto cleanupSubscriber =
      folly::makeGuard([this, subscriberInfo = std::move(subscriberInfo)]() {
        unregisterSubscription(subscriberInfo);
      });

  co_return {
      {},
      folly::coro::co_invoke(
          [this,
           request = std::move(request),
           subId = std::move(subId),
           cleanupSubscriber = std::move(cleanupSubscriber)]() mutable
              -> folly::coro::AsyncGenerator<OperSubPathUnit&&> {
            auto generator = makeExtendedStateStreamGenerator(
                std::move(request), false, std::move(subId));
            while (auto item = co_await generator.next()) {
              // got item
              auto&& deltas = *item;

              OperSubPathUnit unit;
              std::transform(
                  std::make_move_iterator(deltas.begin()),
                  std::make_move_iterator(deltas.end()),
                  std::back_inserter(*unit.changes()),
                  [](auto&& delta) {
                    // we expect newVal to always be set, even in
                    // the case of a deleted path. For deleted
                    // paths, lower layers will create a
                    // TaggedOperState with empty contents.
                    return *std::move(delta.newVal);
                  });
              co_yield std::move(unit);
            }
          })};
}

folly::coro::Task<apache::thrift::ResponseAndServerStream<
    OperSubInitResponse,
    OperSubDeltaUnit>>
ServiceHandler::co_subscribeOperStateDeltaExtended(
    std::unique_ptr<OperSubRequestExtended> request) {
  auto log = LOG_THRIFT_CALL(INFO, getRequestDetails(*request));

  PathValidator::validateExtendedStatePaths(*request->paths());

  auto subscriberInfo = makeSubscriberInfo(
      *request, PubSubType::DELTA, false, lastSubscriptionUid_.fetch_add(1));
  auto subId = makeSubscriptionIdentifier(subscriberInfo);
  registerSubscription(subscriberInfo, *request->forceSubscribe());
  auto cleanupSubscriber =
      folly::makeGuard([this, subscriberInfo = std::move(subscriberInfo)]() {
        unregisterSubscription(subscriberInfo);
      });

  co_return {
      {},
      folly::coro::co_invoke(
          [this,
           request = std::move(request),
           subId = std::move(subId),
           cleanupSubscriber = std::move(cleanupSubscriber)]() mutable
              -> folly::coro::AsyncGenerator<OperSubDeltaUnit&&> {
            auto streamReader = makeExtendedDeltaStreamGenerator(
                std::move(request), false, std::move(subId));
            while (auto item = co_await streamReader.generator_.next()) {
              // Update served data size and then yield
              streamReader.streamInfo_->servedDataSize.fetch_add(
                  item->allocatedBytes, std::memory_order_relaxed);
              streamReader.streamInfo_->numUpdatesServed.fetch_add(
                  1, std::memory_order_relaxed);
              streamReader.streamInfo_->lastUpdateWrittenAt.store(
                  getCurrentTimeMs(), std::memory_order_relaxed);

              auto&& delta = std::move(item->val);

              OperSubDeltaUnit unit;

              unit.changes() = delta;

              co_yield std::move(unit);
            }
          })};
}

folly::coro::Task<
    apache::thrift::ResponseAndServerStream<OperSubInitResponse, OperDelta>>
ServiceHandler::co_subscribeOperStatsDelta(
    std::unique_ptr<OperSubRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO, getRequestDetails(*request));
  PathValidator::validateStatsPath(*request->path()->raw());

  auto subscriberInfo = makeSubscriberInfo(
      *request, PubSubType::DELTA, true, lastSubscriptionUid_.fetch_add(1));
  auto subId = makeSubscriptionIdentifier(subscriberInfo);
  registerSubscription(subscriberInfo, *request->forceSubscribe());
  auto cleanupSubscriber =
      folly::makeGuard([this, subscriberInfo = std::move(subscriberInfo)]() {
        unregisterSubscription(subscriberInfo);
      });

  co_return {
      {},
      folly::coro::co_invoke(
          [this,
           request = std::move(request),
           subId = std::move(subId),
           cleanupSubscriber = std::move(cleanupSubscriber)]() mutable
              -> folly::coro::AsyncGenerator<OperDelta&&> {
            auto streamReader = makeDeltaStreamGenerator(
                std::move(request), true, std::move(subId));
            while (auto item = co_await streamReader.generator_.next()) {
              // Update served data size and then yield
              streamReader.streamInfo_->servedDataSize.fetch_add(
                  (*item).allocatedBytes, std::memory_order_relaxed);
              streamReader.streamInfo_->numUpdatesServed.fetch_add(
                  1, std::memory_order_relaxed);
              streamReader.streamInfo_->lastUpdateWrittenAt.store(
                  getCurrentTimeMs(), std::memory_order_relaxed);
              co_yield std::move((*item).val);
            }
          })};
}

folly::coro::Task<apache::thrift::ResponseAndServerStream<
    OperSubInitResponse,
    OperSubPathUnit>>
ServiceHandler::co_subscribeOperStatsPathExtended(
    std::unique_ptr<OperSubRequestExtended> request) {
  auto log = LOG_THRIFT_CALL(INFO, getRequestDetails(*request));

  PathValidator::validateExtendedStatsPaths(*request->paths());

  auto subscriberInfo = makeSubscriberInfo(
      *request, PubSubType::PATH, true, lastSubscriptionUid_.fetch_add(1));
  auto subId = makeSubscriptionIdentifier(subscriberInfo);
  registerSubscription(subscriberInfo, *request->forceSubscribe());
  auto cleanupSubscriber =
      folly::makeGuard([this, subscriberInfo = std::move(subscriberInfo)]() {
        unregisterSubscription(subscriberInfo);
      });

  co_return {
      {},
      folly::coro::co_invoke(
          [this,
           request = std::move(request),
           subId = std::move(subId),
           cleanupSubscriber = std::move(cleanupSubscriber)]() mutable
              -> folly::coro::AsyncGenerator<OperSubPathUnit&&> {
            auto generator = makeExtendedStateStreamGenerator(
                std::move(request), true, std::move(subId));
            while (auto item = co_await generator.next()) {
              // got item
              auto&& deltas = *item;

              OperSubPathUnit unit;
              std::transform(
                  std::make_move_iterator(deltas.begin()),
                  std::make_move_iterator(deltas.end()),
                  std::back_inserter(*unit.changes()),
                  [](auto&& delta) {
                    // we expect newVal to always be set, even in
                    // the case of a deleted path. For deleted
                    // paths, lower layers will create a
                    // TaggedOperState with empty contents.
                    return *std::move(delta.newVal);
                  });
              co_yield std::move(unit);
            }
          })};
}

folly::coro::Task<apache::thrift::ResponseAndServerStream<
    OperSubInitResponse,
    OperSubDeltaUnit>>
ServiceHandler::co_subscribeOperStatsDeltaExtended(
    std::unique_ptr<OperSubRequestExtended> request) {
  auto log = LOG_THRIFT_CALL(INFO, getRequestDetails(*request));

  PathValidator::validateExtendedStatsPaths(*request->paths());

  auto subscriberInfo = makeSubscriberInfo(
      *request, PubSubType::DELTA, true, lastSubscriptionUid_.fetch_add(1));
  auto subId = makeSubscriptionIdentifier(subscriberInfo);
  registerSubscription(subscriberInfo, *request->forceSubscribe());
  auto cleanupSubscriber =
      folly::makeGuard([this, subscriberInfo = std::move(subscriberInfo)]() {
        unregisterSubscription(subscriberInfo);
      });

  co_return {
      {},
      folly::coro::co_invoke(
          [this,
           request = std::move(request),
           subId = std::move(subId),
           cleanupSubscriber = std::move(cleanupSubscriber)]() mutable
              -> folly::coro::AsyncGenerator<OperSubDeltaUnit&&> {
            auto streamReader = makeExtendedDeltaStreamGenerator(
                std::move(request), true, std::move(subId));
            while (auto item = co_await streamReader.generator_.next()) {
              // Update served data size and then yield
              streamReader.streamInfo_->servedDataSize.fetch_add(
                  item->allocatedBytes, std::memory_order_relaxed);
              streamReader.streamInfo_->numUpdatesServed.fetch_add(
                  1, std::memory_order_relaxed);
              streamReader.streamInfo_->lastUpdateWrittenAt.store(
                  getCurrentTimeMs(), std::memory_order_relaxed);
              auto&& delta = std::move(item->val);

              OperSubDeltaUnit unit;

              unit.changes() = delta;

              co_yield std::move(unit);
            }
          })};
}

folly::coro::Task<apache::thrift::ResponseAndServerStream<
    OperSubInitResponse,
    SubscriberMessage>>
ServiceHandler::co_subscribeState(std::unique_ptr<SubRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO, getRequestDetails(*request));

  if (!request->extPaths()->empty()) {
    XLOG(WARN)
        << "FSDB rejected subscribeState: INVALID_REQUEST (ExtendedPaths) clientId="
        << clientIdToString(*request->clientId());
    throw Utils::createFsdbException(
        FsdbErrorCode::INVALID_REQUEST,
        "Invalid Subscription request with ExtendedPaths from: ",
        clientIdToString(*request->clientId()));
  } else if (request->paths()->empty()) {
    XLOG(WARN)
        << "FSDB rejected subscribeState: INVALID_REQUEST (empty Paths) clientId="
        << clientIdToString(*request->clientId());
    throw Utils::createFsdbException(
        FsdbErrorCode::INVALID_REQUEST,
        "Request without Paths from: ",
        clientIdToString(*request->clientId()));
  } else {
    validatePaths(*request->paths(), false);
  }

  auto subscriberInfo = makeSubscriberInfo(
      *request, PubSubType::PATCH, false, lastSubscriptionUid_.fetch_add(1));
  auto subId = makeSubscriptionIdentifier(subscriberInfo);
  auto subscriptionUid = subId.uid();
  registerSubscription(subscriberInfo, *request->forceSubscribe());
  auto cleanupSubscriber =
      folly::makeGuard([this, subscriberInfo = std::move(subscriberInfo)]() {
        unregisterSubscription(subscriberInfo);
      });
  auto streamReader = folly::makeTryWith([&] {
    return makePatchStreamGenerator(
        std::move(request), false, std::move(subId));
  });
  co_return {
      makeOperSubInitResponse(subscriptionUid, !streamReader.hasException()),
      serveStreamReader(std::move(streamReader), std::move(cleanupSubscriber))};
}

folly::coro::Task<apache::thrift::ResponseAndServerStream<
    OperSubInitResponse,
    SubscriberMessage>>
ServiceHandler::co_subscribeStats(std::unique_ptr<SubRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO, getRequestDetails(*request));

  if (!request->extPaths()->empty()) {
    XLOG(WARN)
        << "FSDB rejected subscribeStats: INVALID_REQUEST (ExtendedPaths) clientId="
        << clientIdToString(*request->clientId());
    throw Utils::createFsdbException(
        FsdbErrorCode::INVALID_REQUEST,
        "Invalid Subscription request with ExtendedPaths from: ",
        clientIdToString(*request->clientId()));
  } else if (request->paths()->empty()) {
    XLOG(WARN)
        << "FSDB rejected subscribeStats: INVALID_REQUEST (empty Paths) clientId="
        << clientIdToString(*request->clientId());
    throw Utils::createFsdbException(
        FsdbErrorCode::INVALID_REQUEST,
        "Request without Paths from: ",
        clientIdToString(*request->clientId()));
  } else {
    validatePaths(*request->paths(), true);
  }

  auto subscriberInfo = makeSubscriberInfo(
      *request, PubSubType::PATCH, true, lastSubscriptionUid_.fetch_add(1));
  auto subId = makeSubscriptionIdentifier(subscriberInfo);
  auto subscriptionUid = subId.uid();
  registerSubscription(subscriberInfo, *request->forceSubscribe());
  auto cleanupSubscriber =
      folly::makeGuard([this, subscriberInfo = std::move(subscriberInfo)]() {
        unregisterSubscription(subscriberInfo);
      });

  auto streamReader = folly::makeTryWith([&] {
    return makePatchStreamGenerator(std::move(request), true, std::move(subId));
  });
  co_return {
      makeOperSubInitResponse(subscriptionUid, !streamReader.hasException()),
      serveStreamReader(std::move(streamReader), std::move(cleanupSubscriber))};
}

folly::coro::Task<apache::thrift::ResponseAndServerStream<
    OperSubInitResponse,
    SubscriberMessage>>
ServiceHandler::co_subscribeStateExtended(std::unique_ptr<SubRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO, getRequestDetails(*request));

  if (request->extPaths()->empty()) {
    XLOG(WARN)
        << "FSDB rejected subscribeStateExtended: INVALID_REQUEST (no ExtendedPaths) clientId="
        << clientIdToString(*request->clientId());
    throw Utils::createFsdbException(
        FsdbErrorCode::INVALID_REQUEST,
        "subscribeStateExtended request without ExtendedPaths from: ",
        clientIdToString(*request->clientId()));
  } else if (!request->paths()->empty()) {
    XLOG(WARN)
        << "FSDB rejected subscribeStateExtended: INVALID_REQUEST (Paths present) clientId="
        << clientIdToString(*request->clientId());
    throw Utils::createFsdbException(
        FsdbErrorCode::INVALID_REQUEST,
        "subscribeStateExtended request with Paths from: ",
        clientIdToString(*request->clientId()));
  }

  for (const auto& kv : *request->extPaths()) {
    PathValidator::validateExtendedStatePath(kv.second);
  }

  auto subscriberInfo = makeSubscriberInfo(
      *request, PubSubType::PATCH, false, lastSubscriptionUid_.fetch_add(1));
  auto subId = makeSubscriptionIdentifier(subscriberInfo);
  auto subscriptionUid = subId.uid();
  registerSubscription(subscriberInfo, *request->forceSubscribe());
  auto cleanupSubscriber =
      folly::makeGuard([this, subscriberInfo = std::move(subscriberInfo)]() {
        unregisterSubscription(subscriberInfo);
      });

  auto streamReader = folly::makeTryWith([&] {
    return makePatchStreamGenerator(
        std::move(request), false, std::move(subId));
  });
  co_return {
      makeOperSubInitResponse(subscriptionUid, !streamReader.hasException()),
      serveStreamReader(std::move(streamReader), std::move(cleanupSubscriber))};
}

folly::coro::Task<apache::thrift::ResponseAndServerStream<
    OperSubInitResponse,
    SubscriberMessage>>
ServiceHandler::co_subscribeStatsExtended(std::unique_ptr<SubRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO, getRequestDetails(*request));

  if (request->extPaths()->empty()) {
    XLOG(WARN)
        << "FSDB rejected subscribeStatsExtended: INVALID_REQUEST (no ExtendedPaths) clientId="
        << clientIdToString(*request->clientId());
    throw Utils::createFsdbException(
        FsdbErrorCode::INVALID_REQUEST,
        "subscribeStatsExtended request without ExtendedPaths from: ",
        clientIdToString(*request->clientId()));
  } else if (!request->paths()->empty()) {
    XLOG(WARN)
        << "FSDB rejected subscribeStatsExtended: INVALID_REQUEST (Paths present) clientId="
        << clientIdToString(*request->clientId());
    throw Utils::createFsdbException(
        FsdbErrorCode::INVALID_REQUEST,
        "subscribeStatsExtended request with Paths from: ",
        clientIdToString(*request->clientId()));
  }

  for (const auto& kv : *request->extPaths()) {
    PathValidator::validateExtendedStatsPath(kv.second);
  }

  auto subscriberInfo = makeSubscriberInfo(
      *request, PubSubType::PATCH, true, lastSubscriptionUid_.fetch_add(1));
  auto subId = makeSubscriptionIdentifier(subscriberInfo);
  auto subscriptionUid = subId.uid();
  registerSubscription(subscriberInfo, *request->forceSubscribe());
  auto cleanupSubscriber =
      folly::makeGuard([this, subscriberInfo = std::move(subscriberInfo)]() {
        unregisterSubscription(subscriberInfo);
      });

  auto streamReader = folly::makeTryWith([&] {
    return makePatchStreamGenerator(std::move(request), true, std::move(subId));
  });
  co_return {
      makeOperSubInitResponse(subscriptionUid, !streamReader.hasException()),
      serveStreamReader(std::move(streamReader), std::move(cleanupSubscriber))};
}

folly::coro::Task<std::unique_ptr<AddPatchSubscriptionPathsResponse>>
ServiceHandler::addPatchSubscriptionPathsImpl(
    std::unique_ptr<AddPatchSubscriptionPathsRequest> request,
    bool isStats) {
  const auto clientId = clientIdToString(*request->clientId());
  const auto subscriptionUid = *request->subscriptionUid();
  const auto* methodName = isStats ? "addStatsPatchSubscriptionPaths"
                                   : "addStatePatchSubscriptionPaths";
  bool hasRawPaths = !request->paths()->empty();
  bool hasExtPaths = !request->extPaths()->empty();
  if (hasRawPaths && hasExtPaths) {
    XLOG(WARN) << "FSDB rejected " << methodName
               << ": INVALID_REQUEST (both paths and extPaths) clientId="
               << clientId << " subscriptionUid=" << subscriptionUid;
    throw Utils::createFsdbException(
        FsdbErrorCode::INVALID_REQUEST,
        "addPatchSubscriptionPaths: cannot set both paths and extPaths");
  }
  if (!hasRawPaths && !hasExtPaths) {
    XLOG(WARN) << "FSDB rejected " << methodName
               << ": INVALID_REQUEST (no paths) clientId=" << clientId
               << " subscriptionUid=" << subscriptionUid;
    throw Utils::createFsdbException(
        FsdbErrorCode::INVALID_REQUEST,
        "addPatchSubscriptionPaths: must set paths or extPaths");
  }
  // For patch subscriptions the subscriberId is the clientId instanceId, paired
  // with the server-assigned uid the client received in OperSubInitResponse.
  SubscriptionIdentifier id(
      SubscriberId(*request->clientId()->instanceId()), subscriptionUid);
  // lastStreamRevision defaults to 0; treat 0 as "not using the round-trip" and
  // forward nullopt so the storage layer doesn't stamp (and clobber)
  // OperMetadata.streamRevision on every served patch. Non-zero is opt-in.
  std::optional<StreamRevision> streamRevision;
  if (const auto rev = *request->lastStreamRevision(); rev != 0) {
    streamRevision = rev;
  }
  std::optional<FsdbErrorCode> err;
  // The validators may throw an FsdbException (specific code) or a generic
  // exception. Rethrow FsdbException unchanged to preserve its code; map
  // anything else to INVALID_REQUEST so the Thrift `throws (FsdbException)`
  // contract holds.
  try {
    if (hasExtPaths) {
      // Validate extended paths for parity with the raw branch, so a malformed
      // path is rejected here rather than deeper in the storage layer.
      for (const auto& [_, extPath] : *request->extPaths()) {
        if (isStats) {
          PathValidator::validateExtendedStatsPath(extPath);
        } else {
          PathValidator::validateExtendedStatePath(extPath);
        }
      }
    } else {
      validatePaths(*request->paths(), isStats);
    }
  } catch (const fsdb::FsdbException&) {
    throw;
  } catch (const std::exception& ex) {
    XLOG(WARN) << "FSDB rejected " << methodName
               << ": INVALID_REQUEST (invalid path) clientId=" << clientId
               << " subscriptionUid=" << subscriptionUid << " : " << ex.what();
    throw Utils::createFsdbException(
        FsdbErrorCode::INVALID_REQUEST,
        "Invalid path in addPatchSubscriptionPaths from: ",
        clientId);
  }
  if (hasExtPaths) {
    err = isStats
        ? operStatsStorage_.add_extended_patch_subscription_path(
              std::move(id), std::move(*request->extPaths()), streamRevision)
        : stateStorageBase().add_extended_patch_subscription_path_impl(
              std::move(id), std::move(*request->extPaths()), streamRevision);
  } else {
    err = isStats
        ? operStatsStorage_.add_patch_subscription_path(
              std::move(id), std::move(*request->paths()), streamRevision)
        : stateStorageBase().add_patch_subscription_path_impl(
              std::move(id), std::move(*request->paths()), streamRevision);
  }
  if (err.has_value()) {
    XLOG(WARN) << "FSDB rejected " << methodName << ": "
               << apache::thrift::util::enumNameSafe(*err)
               << " clientId=" << clientId
               << " subscriptionUid=" << subscriptionUid;
    throw Utils::createFsdbException(
        *err, "addPatchSubscriptionPaths failed for subscriber: ", clientId);
  }
  co_return std::make_unique<AddPatchSubscriptionPathsResponse>();
}

folly::coro::Task<std::unique_ptr<AddPatchSubscriptionPathsResponse>>
ServiceHandler::co_addStatePatchSubscriptionPaths(
    std::unique_ptr<AddPatchSubscriptionPathsRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO);
  co_return co_await addPatchSubscriptionPathsImpl(std::move(request), false);
}

folly::coro::Task<std::unique_ptr<AddPatchSubscriptionPathsResponse>>
ServiceHandler::co_addStatsPatchSubscriptionPaths(
    std::unique_ptr<AddPatchSubscriptionPathsRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO);
  co_return co_await addPatchSubscriptionPathsImpl(std::move(request), true);
}

folly::coro::Task<std::unique_ptr<OperState>> ServiceHandler::co_getOperState(
    std::unique_ptr<OperGetRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO, getRequestDetails(*request));
  PathValidator::validateStatePath(*request->path()->raw());
  auto encoded = operStorage_->get_encoded(
      request->path()->raw()->begin(),
      request->path()->raw()->end(),
      *request->protocol());
  // value() on an lvalue Expected would deep-copy the serialized subtree.
  auto ret = std::make_unique<OperState>(std::move(encoded).value());
  co_return std::move(ret);
}

folly::coro::Task<std::unique_ptr<OperState>> ServiceHandler::co_getOperStats(
    std::unique_ptr<OperGetRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO, getRequestDetails(*request));
  PathValidator::validateStatsPath(*request->path()->raw());
  auto ret =
      std::make_unique<OperState>(operStatsStorage_
                                      .get_encoded(
                                          request->path()->raw()->begin(),
                                          request->path()->raw()->end(),
                                          *request->protocol())
                                      .value());
  co_return std::move(ret);
}

folly::coro::Task<std::unique_ptr<std::vector<TaggedOperState>>>
ServiceHandler::co_getOperStateExtended(
    std::unique_ptr<OperGetRequestExtended> request) {
  auto log = LOG_THRIFT_CALL(INFO);
  PathValidator::validateExtendedStatePaths(*request->paths());

  auto ret = std::make_unique<std::vector<TaggedOperState>>();

  for (const auto& path : *request->paths()) {
    auto curr = operStorage_->get_encoded_extended(
        path.path()->begin(), path.path()->end(), *request->protocol());
    ret->insert(
        ret->end(),
        std::make_move_iterator(curr->begin()),
        std::make_move_iterator(curr->end()));
  }

  co_return std::move(ret);
}

folly::coro::Task<std::unique_ptr<std::vector<TaggedOperState>>>
ServiceHandler::co_getOperStatsExtended(
    std::unique_ptr<OperGetRequestExtended> request) {
  auto log = LOG_THRIFT_CALL(INFO);
  PathValidator::validateExtendedStatsPaths(*request->paths());

  auto ret = std::make_unique<std::vector<TaggedOperState>>();

  for (const auto& path : *request->paths()) {
    auto curr = operStatsStorage_.get_encoded_extended(
        path.path()->begin(), path.path()->end(), *request->protocol());
    ret->insert(
        ret->end(),
        std::make_move_iterator(curr->begin()),
        std::make_move_iterator(curr->end()));
  }

  co_return std::move(ret);
}

namespace {

void populatePublisherStreamState(
    OperPublisherInfo& info,
    const folly::Synchronized<PublisherStreamState>& streamState) {
  // Copy under rlock for a consistent snapshot
  PublisherStreamState stateCopy;
  {
    auto state = streamState.rlock();
    stateCopy = *state;
  }
  info.connectedAt() = stateCopy.connectedAt;
  info.initialSyncCompletedAt() = stateCopy.initialSyncCompletedAt;
  info.lastUpdateReceivedAt() = stateCopy.lastUpdateReceivedAt;
  info.lastHeartbeatReceivedAt() = stateCopy.lastHeartbeatReceivedAt;
  info.lastUpdatePublishedAt() = stateCopy.lastUpdatePublishedAt;
  info.numUpdatesReceived() = stateCopy.numUpdatesReceived;
  info.receivedDataSize() = stateCopy.receivedDataSize;
}

} // namespace

folly::coro::Task<std::unique_ptr<PublisherIdToOperPublisherInfo>>
ServiceHandler::co_getAllOperPublisherInfos() {
  auto publishers = std::make_unique<PublisherIdToOperPublisherInfo>();
  activePublishers_.withRLock([&](const auto& activePublishers) {
    for (const auto& it : activePublishers) {
      auto& entry = it.second;
      auto info = entry.info;
      if (entry.streamState) {
        populatePublisherStreamState(info, *entry.streamState);
      }
      (*publishers)[*info.publisherId()].push_back(std::move(info));
    }
  });
  co_return publishers;
}

folly::coro::Task<std::unique_ptr<PublisherIdToOperPublisherInfo>>
ServiceHandler::co_getOperPublisherInfos(
    std::unique_ptr<PublisherIds> publisherIds) {
  auto log = LOG_THRIFT_CALL(INFO);
  auto publishers = std::make_unique<PublisherIdToOperPublisherInfo>();
  activePublishers_.withRLock([&](const auto& activePublishers) {
    for (const auto& it : activePublishers) {
      auto& entry = it.second;
      if (publisherIds->find(*entry.info.publisherId()) ==
          publisherIds->end()) {
        continue;
      }
      auto info = entry.info;
      if (entry.streamState) {
        populatePublisherStreamState(info, *entry.streamState);
      }
      (*publishers)[*info.publisherId()].push_back(std::move(info));
    }
  });
  co_return publishers;
}

// helper to merge values from OperSubscriberInfo
void mergeOperSubscriberInfo(
    SubscriberIdToOperSubscriberInfos& infos,
    const std::vector<OperSubscriberInfo>& mergeInfo) {
  std::map<uint64_t, SubscriberId> subscriberUids;
  for (const auto& it : infos) {
    for (const auto& sub : it.second) {
      CHECK(sub.subscriptionUid().has_value());
      subscriberUids[*sub.subscriptionUid()] = *sub.subscriberId();
    }
  }
  for (const auto& subInfo : mergeInfo) {
    CHECK(subInfo.subscriptionUid().has_value());
    auto it = subscriberUids.find(*subInfo.subscriptionUid());
    if (it != subscriberUids.end()) {
      for (auto& sub : infos[it->second]) {
        CHECK(sub.subscriptionUid().has_value());
        if (*sub.subscriptionUid() == *subInfo.subscriptionUid()) {
          if (subInfo.subscriptionQueueWatermark().has_value()) {
            sub.subscriptionQueueWatermark() =
                *subInfo.subscriptionQueueWatermark();
          }
          if (subInfo.subscriptionChunksCoalesced().has_value()) {
            sub.subscriptionChunksCoalesced() =
                *subInfo.subscriptionChunksCoalesced();
          }
          if (subInfo.enqueuedDataSize().has_value()) {
            sub.enqueuedDataSize() = *subInfo.enqueuedDataSize();
          }
          if (subInfo.servedDataSize().has_value()) {
            sub.servedDataSize() = *subInfo.servedDataSize();
          }
          if (subInfo.initialSyncCompletedAt().has_value()) {
            sub.initialSyncCompletedAt() = *subInfo.initialSyncCompletedAt();
          }
          if (subInfo.lastUpdateEnqueuedAt().has_value()) {
            sub.lastUpdateEnqueuedAt() = *subInfo.lastUpdateEnqueuedAt();
          }
          if (subInfo.lastHeartbeatSentAt().has_value()) {
            sub.lastHeartbeatSentAt() = *subInfo.lastHeartbeatSentAt();
          }
          if (subInfo.lastEnqueuedUpdatePublishedAt().has_value()) {
            sub.lastEnqueuedUpdatePublishedAt() =
                *subInfo.lastEnqueuedUpdatePublishedAt();
          }
          if (subInfo.lastUpdateWrittenAt().has_value()) {
            sub.lastUpdateWrittenAt() = *subInfo.lastUpdateWrittenAt();
          }
          if (subInfo.numUpdatesServed().has_value()) {
            sub.numUpdatesServed() = *subInfo.numUpdatesServed();
          }
        }
      }
    }
  }
}

folly::coro::Task<std::unique_ptr<SubscriberIdToOperSubscriberInfos>>
ServiceHandler::co_getAllOperSubscriberInfos() {
  auto log = LOG_THRIFT_CALL(INFO);
  auto subscriptions = std::make_unique<SubscriberIdToOperSubscriberInfos>();
  activeSubscriptions_.withRLock([&](const auto& activeSubscriptions) {
    for (const auto& it : activeSubscriptions) {
      for (const auto& subscription : it.second) {
        (*subscriptions)[*subscription.subscriberId()].push_back(subscription);
      }
    }
  });
  mergeOperSubscriberInfo(
      *subscriptions, stateStorageBase().getSubscriptions());
  mergeOperSubscriberInfo(*subscriptions, operStatsStorage_.getSubscriptions());
  co_return subscriptions;
}

folly::coro::Task<std::unique_ptr<SubscriberIdToOperSubscriberInfos>>
ServiceHandler::co_getOperSubscriberInfos(
    std::unique_ptr<SubscriberIds> subscriberIds) {
  auto log = LOG_THRIFT_CALL(INFO);
  auto subscriptions = std::make_unique<SubscriberIdToOperSubscriberInfos>();
  activeSubscriptions_.withRLock([&](const auto& activeSubscriptions) {
    for (const auto& it : activeSubscriptions) {
      for (const auto& subscription : it.second) {
        if (subscriberIds->find(*subscription.subscriberId()) !=
            subscriberIds->end()) {
          (*subscriptions)[*subscription.subscriberId()].push_back(
              subscription);
        }
      }
    }
  });
  mergeOperSubscriberInfo(
      *subscriptions, stateStorageBase().getSubscriptions());
  mergeOperSubscriberInfo(*subscriptions, operStatsStorage_.getSubscriptions());
  co_return subscriptions;
}

void ServiceHandler::initPerStreamCounters(void) {
  for (const auto& [key, value] : *fsdbConfig_->getThrift().subscribers()) {
    if (value.trackReconnect().value()) {
      disconnectedSubscribers_.emplace(
          key,
          TLCounter(
              fb303::ThreadCachedServiceData::get()->getThreadStats(),
              folly::to<std::string>(
                  fsdb_common_constants::kFsdbServiceHandlerNativeStatsPrefix(),
                  "disconnected_subscriber.",
                  key)));
      auto count = value.numExpectedSubscriptions().value();
      if (count > 0) {
        if (auto counter = disconnectedSubscribers_.find(key);
            counter != disconnectedSubscribers_.end()) {
          counter->second.incrementValue(1);
        }
        num_disconnected_subscriptions_.incrementValue(count);
      }
      disconnectedSubscriptions_.emplace(
          key,
          TLCounter(
              fb303::ThreadCachedServiceData::get()->getThreadStats(),
              folly::to<std::string>(
                  fsdb_common_constants::kFsdbServiceHandlerNativeStatsPrefix(),
                  "disconnected_subscriptions.",
                  key)));
      if (auto counter = disconnectedSubscriptions_.find(key);
          counter != disconnectedSubscriptions_.end()) {
        counter->second.incrementValue(count);
      }
      connectedSubscriptions_.emplace(
          key,
          TLCounter(
              fb303::ThreadCachedServiceData::get()->getThreadStats(),
              folly::to<std::string>(
                  fsdb_common_constants::kFsdbServiceHandlerNativeStatsPrefix(),
                  "connected_subscriptions.",
                  key)));
    }
  }

  for (const auto& [key, value] : *fsdbConfig_->getThrift().publishers()) {
    for (const auto& pathConfig : *value.paths()) {
      if (*pathConfig.isExpected()) {
        PublisherKey publisherKey(key, *pathConfig.isStats());
        disconnectedPublishers_.emplace(
            publisherKey,
            TLCounter(
                fb303::ThreadCachedServiceData::get()->getThreadStats(),
                folly::to<std::string>(
                    fsdb_common_constants::
                        kFsdbServiceHandlerNativeStatsPrefix(),
                    "disconnected_publisher.",
                    key,
                    (*pathConfig.isStats() ? "-stats" : ""))));
        auto counter = disconnectedPublishers_.find(publisherKey);
        counter->second.incrementValue(1);
        num_disconnected_publishers_.incrementValue(1);
      }
    }
  }

  for (const auto& [key, value] : *fsdbConfig_->getThrift().subscribers()) {
    if (!value.expectedSubscriptionType().has_value()) {
      continue;
    }
    auto client = *subscriberId2ClientId(stripWildcardColons(key)).client();
    if (client == FsdbClient::UNSPECIFIED) {
      XLOG(WARN) << "Skipping expected_subscriptions counter: cannot derive "
                 << "FsdbClient from subscriber config key '" << key << "'";
      continue;
    }
    auto name = folly::to<std::string>(
        fsdb_common_constants::kFsdbServiceHandlerNativeStatsPrefix(),
        "expected_subscriptions.",
        fsdbClient2string(client));
    auto [it, inserted] = expectedSubscriptionsCounters_.emplace(client, name);
    if (!inserted) {
      // Fail-fast: two configs resolving to the same FsdbClient cannot
      // share a single counter without conflating their (possibly
      // different) expectedSubscriptionType semantics at runtime.
      XLOG(ERR) << "Duplicate expected_subscriptions counter for FsdbClient "
                << fsdbClient2string(client) << ": subscriber config key '"
                << key << "' conflicts with a previously registered counter ('"
                << it->second
                << "'). Only one subscriber config per FsdbClient may set "
                << "expectedSubscriptionType; disambiguate the configs.";
      continue;
    }
    tcData().setCounter(name, 0);
  }
}

void ServiceHandler::validateSubscriptionPermissions(
    SubscriberId id,
    PubSubType /* type */,
    bool /* isStats */,
    bool hasRawPath,
    bool hasExtendedPath) {
  if (!hasRawPath && !hasExtendedPath) {
    // No subscriptions allowed at root. Subscription must be contained within
    // a publisher root.
    XLOG(WARN)
        << "FSDB rejected subscription: INVALID_PATH (empty) subscriberId="
        << id;
    throw Utils::createFsdbException(
        FsdbErrorCode::INVALID_PATH, "Subscription path cannot be empty");
  }

  if (!FLAGS_checkSubscriberConfig) {
    return;
  }
  auto config = fsdbConfig_->getSubscriberConfig(id);
  if (hasExtendedPath && config.has_value() &&
      !*config.value().second.get().allowExtendedSubscriptions()) {
    XLOG(WARNING) << "[S:" << id << "]: extended subscriptions not permitted";
    num_subscriptions_rejected_.addValue(1);
    if (FLAGS_enforceSubscriberConfig) {
      throw Utils::createFsdbException(
          FsdbErrorCode::SUBSCRIPTION_NOT_PERMITTED,
          fmt::format(
              "[S:{}]: extended subscriptions not permitted", std::string(id)));
    }
  }
}

void ServiceHandler::validateOperPublishPermissions(
    PublisherId id,
    const RawPathT& path) {
  if (!FLAGS_checkOperOwnership) {
    return;
  }

  try {
    fsdbConfig_->getPathConfig(id, path);
    // path is configured, so we have permission to publish
    return;
  } catch (const fsdb::FsdbException& ex) {
    if (ex.errorCode() == FsdbErrorCode::PUBLISHER_NOT_PERMITTED) {
      num_publisher_path_requests_rejected_.addValue(1);
    } else {
      num_publisher_unknown_requests_rejected_.addValue(1);
    }
    if (FLAGS_enforcePublisherConfig) {
      throw;
    }
    return;
  }
}

void ServiceHandler::preStart(const folly::SocketAddress* /*address*/) {
  if (!server_) {
    return;
  }
  for (auto& socket : server_->getSockets()) {
    folly::SocketAddress socketAddress = socket->getAddress();
    int port = socketAddress.getPort();
    if (port == FLAGS_fsdbPort_high_priority) {
      // set tos = 0xc0, wherein dscp=48 (network control)
      int tos = fsdb_common_constants::kTosForClassOfServiceNC();
      XLOG(INFO) << "set port " << port << " to tos=0xc0";
      socklen_t optsize = sizeof(tos);
      folly::NetworkSocket netsocket = socket->getNetworkSocket();
      folly::netops::setsockopt(
          netsocket, IPPROTO_IPV6, IPV6_TCLASS, &tos, optsize);
    }
  }
}

} // namespace facebook::fboss::fsdb
