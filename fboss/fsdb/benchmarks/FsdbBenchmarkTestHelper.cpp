// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/fsdb/benchmarks/FsdbBenchmarkTestHelper.h"
#include <folly/Subprocess.h>
#include <folly/Utility.h>
#include <folly/system/HardwareConcurrency.h>
#include <thrift/lib/cpp2/op/Get.h>
#include "fboss/thrift_cow/nodes/Types.h"

namespace {

const thriftpath::RootThriftPath<facebook::fboss::fsdb::FsdbOperStateRoot>
    stateRoot;
const thriftpath::RootThriftPath<facebook::fboss::fsdb::FsdbOperStatsRoot>
    statsRoot;
const std::vector<std::string> kPublishRoot{"agent"};
const uint32_t kStateServeIntervalMs = 50;
const uint32_t kStatsServeIntervalMs = 50;

} // anonymous namespace

namespace facebook::fboss::fsdb::test {

void FsdbBenchmarkTestHelper::setup(
    int32_t numSubscriptions,
    bool startFsdbTestServer,
    std::optional<std::string> serviceFileName,
    std::optional<uint32_t> stateServeIntervalMs) {
  if (startFsdbTestServer) {
    fsdbTestServer_ = std::make_unique<fsdb::test::FsdbTestServer>(
        0,
        stateServeIntervalMs.value_or(kStateServeIntervalMs),
        kStatsServeIntervalMs);
    FLAGS_fsdbPort = fsdbTestServer_->getFsdbPort();

    // Share two bounded IO pools across all managers so the client thread count
    // does not grow linearly with the cluster size. Each FsdbPubSubManager
    // otherwise spawns a local thread per unsupplied EventBase (4 roles), i.e.
    // ~2800 threads at the default 699 subscribers, exhausting host thread/pid
    // limits on resource-constrained test hosts. Cap each pool at the available
    // (container-aware) concurrency.
    const size_t numIoThreads = std::min<size_t>(
        std::max<int32_t>(numSubscriptions, 1),
        std::max<size_t>(1, folly::available_concurrency()));
    ioPoolA_ = std::make_shared<folly::IOThreadPoolExecutor>(numIoThreads);
    ioPoolB_ = std::make_shared<folly::IOThreadPoolExecutor>(numIoThreads);
    // Manager 0 is the sole publisher. Give its publisher roles a dedicated
    // pool so the publish path is not co-scheduled with the
    // reconnect/subscriber work of the shared pools, which would distort
    // benchmark measurements.
    publisherPool_ = std::make_shared<folly::IOThreadPoolExecutor>(2);

    subscriptionConnected_ = std::vector<std::atomic_bool>(numSubscriptions);
    for (int i = 0; i < numSubscriptions; i++) {
      const std::string clientId = folly::to<std::string>("agent_", i);
      // Supply event bases for all four roles so each manager spawns zero local
      // threads. Pure subscribers (i != 0) never use their publisher evbs, so
      // routing those to the shared pools is fine.
      auto* statsPublisherEvb =
          (i == 0) ? publisherPool_->getEventBase() : ioPoolA_->getEventBase();
      auto* statePublisherEvb =
          (i == 0) ? publisherPool_->getEventBase() : ioPoolB_->getEventBase();
      pubsubMgrs_.push_back(
          std::make_unique<fsdb::FsdbPubSubManager>(
              clientId,
              ioPoolA_->getEventBase(),
              ioPoolB_->getEventBase(),
              statsPublisherEvb,
              statePublisherEvb));
    }
  }
  if (serviceFileName) {
    serviceFileName_ = serviceFileName;
    std::vector<std::string> kStartSaiBench = {
        "/bin/systemctl", "start", *serviceFileName + ".service"};

    folly::Subprocess(kStartSaiBench).waitChecked();
    // sleep for 10 seconds to allow SaiBench to start
    std::this_thread::sleep_for(std::chrono::seconds(10));
  }
}

void FsdbBenchmarkTestHelper::publishPath(
    const AgentStats& stats,
    uint64_t stamp) {
  fsdb::OperState state;
  state.contents() =
      apache::thrift::BinarySerializer::serialize<std::string>(stats);
  state.protocol() = fsdb::OperProtocol::BINARY;
  state.metadata() = fsdb::OperMetadata();
  state.metadata()->lastConfirmedAt() = stamp;
  pubsubMgrs_.at(0)->publishStat(std::move(state));
}

void FsdbBenchmarkTestHelper::publishStatePatch(
    const state::SwitchState& state,
    uint64_t stamp) {
  AgentData agentData;
  agentData.switchState() = state;

  auto prevState = std::make_shared<thrift_cow::ThriftStructNode<AgentData>>();
  auto switchState =
      std::make_shared<thrift_cow::ThriftStructNode<AgentData>>(agentData);
  auto basePath = {folly::to<std::string>(folly::to_underlying(
      apache::thrift::op::
          get_field_id_v<FsdbOperStateRoot, apache::thrift::ident::agent>))};

  auto patch =
      thrift_cow::PatchBuilder::build(prevState, switchState, basePath);

  patch.metadata() = fsdb::OperMetadata();
  patch.metadata()->lastConfirmedAt() = stamp;
  pubsubMgrs_.at(0)->publishState(std::move(patch));
}

void FsdbBenchmarkTestHelper::startPublisher(bool stats) {
  auto stateChangeCb = [this](
                           fsdb::FsdbStreamClient::State /* oldState */,
                           fsdb::FsdbStreamClient::State newState) {
    if (newState == fsdb::FsdbStreamClient::State::CONNECTED) {
      publisherConnected_.post();
      readyForPublishing_.store(true);
    } else {
      readyForPublishing_.store(false);
    }
  };

  if (stats) {
    // agent is Path publisher for stats
    pubsubMgrs_.at(0)->createStatPathPublisher(
        getAgentStatsPath(), std::move(stateChangeCb));
  } else {
    // Create patch publisher for agent state since DFS uses patch
    pubsubMgrs_.at(0)->createStatePatchPublisher(
        kPublishRoot, std::move(stateChangeCb));
  }
}

void FsdbBenchmarkTestHelper::waitForPublisherConnected() {
  publisherConnected_.wait();
}

void FsdbBenchmarkTestHelper::stopPublisher(bool gr) {
  readyForPublishing_.store(false);
  pubsubMgrs_.at(0)->removeStatPathPublisher(gr);
}

void FsdbBenchmarkTestHelper::TearDown(bool stopFsdbTestServer) {
  if (stopFsdbTestServer) {
    fsdbTestServer_.reset();
  }
  if (!pubsubMgrs_.empty()) {
    stopPublisher();
    CHECK(!readyForPublishing_.load());
    // Destroy managers before the IO pools whose event bases they borrow, so a
    // subsequent setup() cannot leave managers holding dangling EventBase*.
    pubsubMgrs_.clear();
    ioPoolA_.reset();
    ioPoolB_.reset();
    publisherPool_.reset();
  }

  if (serviceFileName_) {
    std::vector<std::string> kStatusSaiBench = {
        "/bin/systemctl", "is-active", *serviceFileName_ + ".service"};
    while (true) {
      folly::Subprocess p(
          kStatusSaiBench, folly::Subprocess::Options().pipeStdout());
      auto [standardOut, standardErr] = p.communicate();
      try {
        p.wait();
        std::string output = folly::trimWhitespace(standardOut).str();
        if (output.find("inactive") != std::string::npos) {
          break;
        } else {
          std::this_thread::sleep_for(std::chrono::seconds(1));
        }
      } catch (const std::exception& e) {
        XLOG(ERR) << "Exception while getting systemctl status: " << e.what();
      }
    }
  }
}

std::optional<fboss::fsdb::FsdbOperTreeMetadata>
FsdbBenchmarkTestHelper::getPublisherRootMetadata(bool isStats) {
  auto metadata =
      fsdbTestServer_->getPublisherRootMetadata(*kPublishRoot.begin(), isStats);
  return metadata;
}

std::vector<std::string> FsdbBenchmarkTestHelper::getAgentStatsPath() {
  return statsRoot.agent().tokens();
}

void FsdbBenchmarkTestHelper::addSubscription(
    const fsdb::FsdbStateSubscriber::FsdbOperStateUpdateCb& stateUpdateCb) {
  auto stateChangeCb = [this](
                           fsdb::SubscriptionState /*oldState*/,
                           fsdb::SubscriptionState newState,
                           std::optional<bool> /*initialSyncHasData*/) {
    if (newState == fsdb::SubscriptionState::CONNECTED) {
      subscriptionConnected_.at(0).store(true);
    } else {
      subscriptionConnected_.at(0).store(false);
    }
  };

  // agent is Path publisher for stats
  pubsubMgrs_.at(0)->addStatPathSubscription(
      getAgentStatsPath(), stateChangeCb, stateUpdateCb);
}

void FsdbBenchmarkTestHelper::addStatePatchSubscription(
    const fsdb::FsdbPatchSubscriber::FsdbOperPatchUpdateCb& stateUpdateCb,
    int32_t subscriberID) {
  auto stateChangeCb = [this, subscriberID](
                           fsdb::SubscriptionState /*oldState*/,
                           fsdb::SubscriptionState newState,
                           std::optional<bool> /*initialSyncHasData*/) {
    if (newState == fsdb::SubscriptionState::CONNECTED) {
      subscriptionConnected_.at(subscriberID).store(true);
    } else {
      subscriptionConnected_.at(subscriberID).store(false);
    }
  };

  fsdb::RawOperPath p;
  p.path() = kPublishRoot;
  // Create patch publisher for agent state since DFS uses patch
  pubsubMgrs_.at(subscriberID)
      ->addStatePatchSubscription(
          {{subscriberID, p}},
          stateChangeCb,
          stateUpdateCb,
          utils::ConnectionOptions("::1", fsdbTestServer_->getFsdbPort()));
}

bool FsdbBenchmarkTestHelper::isSubscriptionConnected(int32_t subscriberId) {
  return subscriptionConnected_.at(subscriberId).load();
}

void FsdbBenchmarkTestHelper::waitForAllSubscribersConnected(
    int32_t numSubscribers) {
  if (numSubscribers > subscriptionConnected_.size()) {
    throw std::runtime_error(
        "Waiting on more subscribers than number of subscribers created");
  }
  for (int i = 0; i < numSubscribers; i++) {
    while (!isSubscriptionConnected(i)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  return;
}

void FsdbBenchmarkTestHelper::removeSubscription(
    bool stats,
    int32_t subscriberID) {
  if (stats) {
    pubsubMgrs_.at(0)->removeStatPathSubscription(getAgentStatsPath());
  } else {
    pubsubMgrs_.at(subscriberID)
        ->removeStatePatchSubscription(kPublishRoot, "::1");
  }
};

} // namespace facebook::fboss::fsdb::test
