// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <iostream>
#include <thread>

#include <folly/String.h>
#include <folly/init/Init.h>
#include <folly/synchronization/Baton.h>
#include <gflags/gflags.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include "fboss/fsdb/client/FsdbPubSubManager.h"
#include "fboss/fsdb/client/FsdbSyncManager.h"
#include "fboss/fsdb/client/instantiations/FsdbCowStateSubManager.h"
#include "fboss/fsdb/client/instantiations/FsdbCowStatsSubManager.h"
#include "fboss/fsdb/common/Flags.h"
#include "fboss/fsdb/if/gen-cpp2/fsdb_common_constants.h"
#include "fboss/fsdb/if/gen-cpp2/fsdb_common_types.h"
#include "fboss/lib/thrift_service_client/ConnectionOptions.h"
#include "fboss/thrift_cow/storage/tests/TestDataFactory.h"

DEFINE_int32(
    consumeDelayMs,
    0,
    "Delay in milliseconds before consuming each chunk");

DEFINE_int32(
    resetDelayAfterNChunks,
    0,
    "Reset delay to 0 after consuming N chunks");

DEFINE_bool(printChunks, false, "print chunks to stdout");

DEFINE_bool(count, false, "Print count of received chunks");

DEFINE_bool(stats, false, "Subscribe to stats instead of state");

DEFINE_string(host, "::1", "FSDB server host");

DEFINE_string(
    client_id,
    "",
    "FSDB Client ID string. If not specified, defaults to stress_test_client_<streamType>");

DEFINE_int32(
    port,
    facebook::fboss::fsdb::fsdb_common_constants::PORT(),
    "FSDB server port");

DEFINE_int32(
    publishIntervalMs,
    1000,
    "Interval in milliseconds between publishes for publishPath");

DEFINE_string(
    publishRole,
    "MaxScale",
    "Role for data generation scale (Minimal, MaxScale, RSW, FSW, SSW, RTSW, FTSW, STSW, XSW, MA, FA, RDSW, FDSW, SDSW, EDSW, RGSW)");

using namespace facebook::fboss::fsdb;
using namespace facebook::fboss::test_data;

facebook::fboss::utils::ConnectionOptions getConnectionOptions() {
  return facebook::fboss::utils::ConnectionOptions(FLAGS_host, FLAGS_port);
}

// Wrapper to allow using addPath() with runtime string paths
template <bool IsStats>
struct RawPath {
  using RootT =
      std::conditional_t<IsStats, FsdbOperStatsRoot, FsdbOperStateRoot>;

  explicit RawPath(std::vector<std::string> tokens)
      : tokens_(std::move(tokens)) {}

  std::vector<std::string> idTokens() const {
    return tokens_;
  }

 private:
  std::vector<std::string> tokens_;
};

RoleSelector parseRoleSelector(const std::string& roleStr) {
  if (auto role = statsRoleFromString(roleStr)) {
    return *role;
  }
  std::cerr << "Unknown role: " << roleStr << ", using Minimal" << std::endl;
  return RoleSelector::Minimal;
}

template <typename T>
void consume(
    const T& /* chunk */,
    int32_t delayMs,
    int32_t resetDelayAfterNChunks) {
  static int nChunksConsumed{0};
  nChunksConsumed++;
  if (FLAGS_count) {
    std::cout << "Received chunk " << nChunksConsumed << std::endl;
  }
  if ((delayMs > 0) && (nChunksConsumed <= resetDelayAfterNChunks)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
  }
}

void subscribe(const std::vector<std::string>& rawPath, bool subscribeStats) {
  auto pubSubMgr = std::make_unique<FsdbPubSubManager>(
      FLAGS_client_id.empty() ? "stress_test_client_path" : FLAGS_client_id);

  auto connectionOptions = getConnectionOptions();

  folly::Baton<> baton;

  auto stateChangeCb = [](SubscriptionState oldState,
                          SubscriptionState newState,
                          std::optional<bool> /* initialSyncHasData */) {
    std::cout << "Path subscription state changed: "
              << static_cast<int>(oldState) << " -> "
              << static_cast<int>(newState) << std::endl;
  };

  auto operStateCb = [](OperState&& state) {
    consume(state, FLAGS_consumeDelayMs, FLAGS_resetDelayAfterNChunks);
    if (FLAGS_printChunks) {
      auto contents = state.contents() ? *state.contents() : "null";
      std::cout << "New value: " << contents << std::endl;
    }
  };

  if (subscribeStats) {
    pubSubMgr->addStatPathSubscription(
        rawPath,
        std::move(stateChangeCb),
        std::move(operStateCb),
        std::move(connectionOptions));
    std::cout
        << "Subscribed via FsdbPubSubManager (stat path). Press Ctrl+C to exit."
        << std::endl;
  } else {
    pubSubMgr->addStatePathSubscription(
        rawPath,
        std::move(stateChangeCb),
        std::move(operStateCb),
        std::move(connectionOptions));
    std::cout
        << "Subscribed via FsdbPubSubManager (state path). Press Ctrl+C to exit."
        << std::endl;
  }

  baton.wait();
}

void subscribeDelta(
    const std::vector<std::string>& rawPath,
    bool subscribeStats) {
  auto pubSubMgr = std::make_unique<FsdbPubSubManager>(
      FLAGS_client_id.empty() ? "stress_test_client_delta" : FLAGS_client_id);

  auto connectionOptions = getConnectionOptions();

  folly::Baton<> baton;

  auto stateChangeCb = [](SubscriptionState oldState,
                          SubscriptionState newState,
                          std::optional<bool> /* initialSyncHasData */) {
    std::cout << "Delta subscription state changed: "
              << static_cast<int>(oldState) << " -> "
              << static_cast<int>(newState) << std::endl;
  };

  auto operDeltaCb = [](OperDelta&& delta) {
    consume(delta, FLAGS_consumeDelayMs, FLAGS_resetDelayAfterNChunks);
    if (FLAGS_printChunks) {
      std::cout << "New Delta: " << std::endl;
      for (auto& change : *delta.changes()) {
        if (change.newState()) {
          std::cout << "\t/" << folly::join("/", *change.path()->raw()) << ": "
                    << *change.newState() << std::endl;
        } else {
          std::cout << "\tDeleted /" << folly::join("/", *change.path()->raw())
                    << std::endl;
        }
      }
    }
  };

  if (subscribeStats) {
    auto operStateCb = [](OperState&& state) {
      consume(state, FLAGS_consumeDelayMs, FLAGS_resetDelayAfterNChunks);
      if (FLAGS_printChunks) {
        auto contents = state.contents() ? *state.contents() : "null";
        std::cout << "New value: " << contents << std::endl;
      }
    };
    pubSubMgr->addStatPathSubscription(
        rawPath,
        std::move(stateChangeCb),
        std::move(operStateCb),
        std::move(connectionOptions));
    std::cout
        << "Subscribed via FsdbPubSubManager (stat path). Press Ctrl+C to exit."
        << std::endl;
  } else {
    pubSubMgr->addStateDeltaSubscription(
        rawPath,
        std::move(stateChangeCb),
        std::move(operDeltaCb),
        std::move(connectionOptions));
    std::cout
        << "Subscribed via FsdbPubSubManager (state delta). Press Ctrl+C to exit."
        << std::endl;
  }

  baton.wait();
}

template <typename PubRootT>
void publishPathImpl(
    const std::vector<std::string>& basePath,
    bool publishStats,
    IDataGenerator& dataFactory) {
  // Enable the appropriate flag for FsdbSyncManager
  if (publishStats) {
    FLAGS_publish_stats_to_fsdb = true;
  } else {
    FLAGS_publish_state_to_fsdb = true;
  }

  auto syncManager = std::make_unique<FsdbSyncManager<PubRootT>>(
      FLAGS_client_id.empty() ? "stress_test_client_path_publisher"
                              : FLAGS_client_id,
      basePath,
      publishStats,
      PubSubType::PATH);

  std::cout << "Starting FsdbSyncManager for "
            << (publishStats ? "stats" : "state") << " publishing..."
            << std::endl;

  syncManager->start();

  std::cout << "FsdbSyncManager started. Publishing data every "
            << FLAGS_publishIntervalMs << "ms with role=" << FLAGS_publishRole
            << ". Press Ctrl+C to exit." << std::endl;

  int64_t version = 0;
  while (true) {
    // Generate test data using the factory
    TaggedOperState taggedState = dataFactory.getStateUpdate(version, false);

    // Update the state in SyncManager which will automatically publish
    syncManager->updateState(
        [taggedState](const auto& /* oldState */) {
          // Deserialize the generated state into a CowState
          auto newState = std::make_shared<
              facebook::fboss::thrift_cow::ThriftStructNode<PubRootT>>();
          auto iobuf =
              folly::IOBuf::copyBuffer(*taggedState.state()->contents());
          newState->fromEncodedBuf(
              *taggedState.state()->protocol(), std::move(*iobuf));
          return newState;
        },
        false /* printUpdateDelay */);

    if (FLAGS_printChunks) {
      auto contents = taggedState.state()->contents()
          ? taggedState.state()->contents()->substr(0, 200) + "..."
          : "null";
      std::cout << "Published version " << version << ": " << contents
                << std::endl;
    }
    if (FLAGS_count) {
      std::cout << "Published chunk " << version + 1 << std::endl;
    }

    version++;
    std::this_thread::sleep_for(
        std::chrono::milliseconds(FLAGS_publishIntervalMs));
  }

  syncManager->stop();
}

// Specialized publisher for BGP data. The data factory generates a full
// FsdbOperStateRoot, but we need to publish only the BgpData subtree at
// basePath ["bgp"] to match the FSDB schema.
void publishBgpPath(IDataGenerator& dataFactory) {
  FLAGS_publish_state_to_fsdb = true;

  const thriftpath::RootThriftPath<FsdbOperStateRoot> fsdbStateRootPath;
  auto bgpPath = fsdbStateRootPath.bgp();

  auto syncManager = std::make_unique<FsdbSyncManager<BgpData>>(
      FLAGS_client_id.empty() ? "stress_test_client_path_publisher"
                              : FLAGS_client_id,
      bgpPath.tokens(),
      false /* isStats */,
      PubSubType::PATH);

  std::cout << "Starting FsdbSyncManager for BGP state publishing..."
            << std::endl;

  syncManager->start();

  std::cout << "FsdbSyncManager started. Publishing BGP data every "
            << FLAGS_publishIntervalMs << "ms with role=" << FLAGS_publishRole
            << ". Press Ctrl+C to exit." << std::endl;

  int64_t version = 0;
  while (true) {
    TaggedOperState taggedState = dataFactory.getStateUpdate(version, false);

    // Deserialize the full FsdbOperStateRoot, then extract BgpData
    auto iobuf = folly::IOBuf::copyBuffer(*taggedState.state()->contents());
    auto fullRoot = std::make_shared<
        facebook::fboss::thrift_cow::ThriftStructNode<FsdbOperStateRoot>>();
    fullRoot->fromEncodedBuf(
        *taggedState.state()->protocol(), std::move(*iobuf));

    // Extract the BgpData from the full root and publish it
    syncManager->updateState(
        [fullRoot](const auto& /* oldState */) {
          auto bgpThrift = fullRoot->toThrift().bgp().value();
          auto newState = std::make_shared<
              facebook::fboss::thrift_cow::ThriftStructNode<BgpData>>();
          newState->fromThrift(std::move(bgpThrift));
          return newState;
        },
        false /* printUpdateDelay */);

    if (FLAGS_printChunks) {
      std::cout << "Published BGP version " << version << std::endl;
    }
    if (FLAGS_count) {
      std::cout << "Published chunk " << version + 1 << std::endl;
    }

    version++;
    std::this_thread::sleep_for(
        std::chrono::milliseconds(FLAGS_publishIntervalMs));
  }

  syncManager->stop();
}

void publishPath(const std::vector<std::string>& rawPath, bool publishStats) {
  // Parse the role and create appropriate data factory
  RoleSelector role = parseRoleSelector(FLAGS_publishRole);

  if (!rawPath.empty() && rawPath[0] == "bgp") {
    BgpRibMapDataGenerator dataFactory(role);
    publishBgpPath(dataFactory);
  } else if (publishStats) {
    FsdbStatsDataFactory dataFactory(role);
    publishPathImpl<FsdbOperStatsRoot>(rawPath, publishStats, dataFactory);
  } else {
    FsdbStateDataFactory dataFactory(role);
    publishPathImpl<FsdbOperStateRoot>(rawPath, publishStats, dataFactory);
  }
}

void subscribePatch(const std::vector<std::string>& rawPath, bool isStats) {
  auto connectionOptions = getConnectionOptions();

  folly::Baton<> baton;

  auto stateChangeCb = [](SubscriptionState oldState,
                          SubscriptionState newState,
                          std::optional<bool> /* initialSyncHasData */) {
    std::cout << "Subscription state changed: " << static_cast<int>(oldState)
              << " -> " << static_cast<int>(newState) << std::endl;
  };

  auto doSubscribe = [&]<typename SubManagerT, bool IsStats>() {
    SubscriptionOptions options(
        FLAGS_client_id.empty() ? "stress_test_client_patch" : FLAGS_client_id,
        isStats);
    auto subscriber = std::make_unique<SubManagerT>(
        std::move(options), std::move(connectionOptions));

    RawPath<IsStats> path(rawPath);
    subscriber->addPath(std::move(path));

    subscriber->subscribe(
        [&](auto update) {
          consume(update, FLAGS_consumeDelayMs, FLAGS_resetDelayAfterNChunks);
          if (FLAGS_printChunks) {
            std::cout << "Received patch update:" << std::endl;
            std::cout << "  Updated paths: " << update.updatedPaths.size()
                      << std::endl;
            for (const auto& p : update.updatedPaths) {
              std::cout << "    /" << folly::join("/", p) << std::endl;
            }
            if (update.lastServedAt.has_value()) {
              std::cout << "  Last served at: " << *update.lastServedAt
                        << std::endl;
            }
            if (update.lastPublishedAt.has_value()) {
              std::cout << "  Last published at: " << *update.lastPublishedAt
                        << std::endl;
            }
          }
        },
        std::move(stateChangeCb));

    std::cout << "Subscribed via FsdbSubManager ("
              << (isStats ? "stat" : "state")
              << " patch). Press Ctrl+C to exit." << std::endl;
    baton.wait();
  };

  if (isStats) {
    doSubscribe.template operator()<FsdbCowStatsSubManager, true>();
  } else {
    doSubscribe.template operator()<FsdbCowStateSubManager, false>();
  }
}

int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);

  if (argc < 3) {
    std::cout
        << "Usage: stress_test_client <streamType> <path> [--stats] [--consumeDelayMs=<ms>] [--resetDelayAfterNChunks=<n>] [--count] [--publishIntervalMs=<ms>] [--publishRole=<role>]"
        << std::endl;
    std::cout
        << "streamType: subscribePath, subscribeDelta, subscribePatch, publishPath"
        << std::endl;
    std::cout
        << "publishRole: Minimal, MaxScale, RSW, FSW, SSW, RTSW, FTSW, STSW, XSW, MA, FA, RDSW, FDSW, SDSW, EDSW"
        << std::endl;
    return 1;
  }

  try {
    std::string streamType(argv[1]);
    std::string pathStr(argv[2]);

    std::vector<std::string> rawPath;
    folly::split('#', pathStr, rawPath);

    if (streamType == "subscribePath") {
      subscribe(std::move(rawPath), FLAGS_stats);
    } else if (streamType == "subscribeDelta") {
      subscribeDelta(std::move(rawPath), FLAGS_stats);
    } else if (streamType == "subscribePatch") {
      subscribePatch(std::move(rawPath), FLAGS_stats);
    } else if (streamType == "publishPath") {
      publishPath(std::move(rawPath), FLAGS_stats);
    } else {
      std::cout << "Incorrect usage. Choose from 'subscribePath', "
                << "'subscribeDelta', 'subscribePatch', or 'publishPath'"
                << std::endl;
      return 1;
    }
  } catch (const FsdbException& e) {
    std::cerr << "errorCode=" << apache::thrift::util::enumName(*e.errorCode())
              << ", error=" << *e.message() << std::endl;
    return 5;
  }
}
