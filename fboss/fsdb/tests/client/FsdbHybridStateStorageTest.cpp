// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "fboss/fsdb/client/FsdbPubSubManager.h"
#include "fboss/fsdb/oper/ExtendedPathBuilder.h"
#include "fboss/fsdb/tests/client/FsdbTestClients.h"
#include "fboss/fsdb/tests/utils/FsdbTestServer.h"
#include "fboss/lib/CommonUtils.h"
#include "fboss/lib/thrift_service_client/ThriftServiceClient.h"

DECLARE_bool(enableHybridStateStorage);

namespace facebook::fboss::fsdb::test {

namespace {
auto constexpr kPublisherId = "fsdb_hybrid_test_publisher";
const std::vector<std::string> kPublishRoot{"agent"};
} // namespace

class FsdbHybridStateStorageTest : public ::testing::TestWithParam<bool> {
 public:
  void SetUp() override {
    savedFlag_ = FLAGS_enableHybridStateStorage;
    FLAGS_enableHybridStateStorage = GetParam();
    // ServiceHandler reads the flag in its ctor.
    fsdb_ = std::make_unique<FsdbTestServer>();
  }

  void TearDown() override {
    if (pubSubManager_) {
      pubSubManager_->removeStatePathPublisher();
      pubSubManager_.reset();
    }
    fsdb_.reset();
    FLAGS_enableHybridStateStorage = savedFlag_;
  }

 protected:
  void createStatePublisher() {
    pubSubManager_ = std::make_unique<FsdbPubSubManager>(kPublisherId);
    pubSubManager_->createStatePathPublisher(
        kPublishRoot, [](auto, auto) {}, fsdb_->getFsdbPort());
    WITH_RETRIES({
      auto metadata =
          fsdb_->getPublisherRootMetadata(*kPublishRoot.begin(), false);
      ASSERT_EVENTUALLY_TRUE(metadata);
      EXPECT_EVENTUALLY_EQ(metadata->numOpenConnections, 1);
    });
  }

  void publishConfigAndWait(const cfg::AgentConfig& config) {
    createStatePublisher();
    pubSubManager_->publishState(makeState(config));
    WITH_RETRIES({
      auto root = fsdb_->serviceHandler().operRootExpensive();
      EXPECT_EVENTUALLY_EQ(*root.agent()->config(), config);
    });
  }

  bool savedFlag_{false};
  std::unique_ptr<FsdbTestServer> fsdb_;
  std::unique_ptr<FsdbPubSubManager> pubSubManager_;
};

TEST_P(FsdbHybridStateStorageTest, selectsRequestedStorage) {
  EXPECT_EQ(fsdb_->serviceHandler().usingHybridStateStorage(), GetParam());
}

TEST_P(FsdbHybridStateStorageTest, publishedStateVisibleOnSelectedStorage) {
  auto config = makeAgentConfig({{"foo", "bar"}});
  publishConfigAndWait(config);
}

TEST_P(FsdbHybridStateStorageTest, getEncodedStateFromSelectedStorage) {
  auto config = makeAgentConfig({{"foo", "bar"}});
  publishConfigAndWait(config);

  folly::EventBase evb;
  auto client = utils::createFsdbClient(
      utils::ConnectionOptions("::1", fsdb_->getFsdbPort()), &evb);

  OperGetRequest req;
  req.path()->raw() = {"agent", "config"};
  req.protocol() = OperProtocol::BINARY;
  OperState state;
  client->sync_getOperState(state, req);
  ASSERT_TRUE(state.contents().has_value());
  EXPECT_EQ(
      apache::thrift::BinarySerializer::deserialize<cfg::AgentConfig>(
          *state.contents()),
      config);

  OperGetRequestExtended extReq;
  extReq.paths() = {ext_path_builder::raw("agent").raw("config").get()};
  extReq.protocol() = OperProtocol::BINARY;
  std::vector<TaggedOperState> extStates;
  client->sync_getOperStateExtended(extStates, extReq);
  ASSERT_EQ(extStates.size(), 1);
  ASSERT_TRUE(extStates[0].state()->contents().has_value());
  EXPECT_EQ(
      apache::thrift::BinarySerializer::deserialize<cfg::AgentConfig>(
          *extStates[0].state()->contents()),
      config);
}

INSTANTIATE_TEST_SUITE_P(
    StorageMode,
    FsdbHybridStateStorageTest,
    ::testing::Bool(),
    [](const ::testing::TestParamInfo<bool>& info) {
      return info.param ? "Hybrid" : "Cow";
    });

} // namespace facebook::fboss::fsdb::test
