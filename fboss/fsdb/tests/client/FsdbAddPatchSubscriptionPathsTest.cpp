// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <folly/coro/BlockingWait.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <chrono>

#include "fboss/fsdb/if/gen-cpp2/FsdbService.h"
#include "fboss/fsdb/tests/utils/FsdbTestServer.h"
#include "fboss/lib/CommonUtils.h"

namespace facebook::fboss::fsdb::test {

namespace {
AddPatchSubscriptionPathsRequest makeRequest(
    const std::string& instanceId,
    int64_t uid,
    std::map<SubscriptionKey, RawOperPath> paths,
    StreamRevision lastStreamRevision = 0) {
  AddPatchSubscriptionPathsRequest req;
  req.clientId()->instanceId() = instanceId;
  req.subscriptionUid() = uid;
  req.paths() = std::move(paths);
  req.lastStreamRevision() = lastStreamRevision;
  return req;
}

AddPatchSubscriptionPathsRequest makeExtRequest(
    const std::string& instanceId,
    int64_t uid,
    std::map<SubscriptionKey, ExtendedOperPath> extPaths,
    StreamRevision lastStreamRevision = 0) {
  AddPatchSubscriptionPathsRequest req;
  req.clientId()->instanceId() = instanceId;
  req.subscriptionUid() = uid;
  req.extPaths() = std::move(extPaths);
  req.lastStreamRevision() = lastStreamRevision;
  return req;
}

SubRequest makeSubRequest(const std::string& instanceId, bool extended) {
  SubRequest req;
  req.clientId()->instanceId() = instanceId;
  req.forceSubscribe() = true;
  if (extended) {
    ExtendedOperPath ep;
    OperPathElem elem;
    elem.set_raw("agent");
    ep.path() = std::vector<OperPathElem>{std::move(elem)};
    req.extPaths() = {{0, std::move(ep)}};
  } else {
    RawOperPath path;
    path.path() = std::vector<std::string>{"agent"};
    req.paths() = {{0, std::move(path)}};
  }
  return req;
}

AddPatchSubscriptionPathsRequest
makeAddRequest(const std::string& instanceId, int64_t uid, bool extended) {
  if (extended) {
    ExtendedOperPath ep;
    OperPathElem elem;
    elem.set_raw("agent");
    ep.path() = std::vector<OperPathElem>{std::move(elem)};
    return makeExtRequest(instanceId, uid, {{1, std::move(ep)}});
  }
  RawOperPath path;
  path.path() = std::vector<std::string>{"agent"};
  return makeRequest(instanceId, uid, {{1, std::move(path)}});
}
} // namespace

class FsdbAddPatchSubscriptionPathsTest : public ::testing::Test {
 protected:
  FsdbTestServer server_;
};

// Empty path map is rejected with INVALID_REQUEST, confirming the new RPC is
// wired through the handler and maps a storage error to an FsdbException.
TEST_F(FsdbAddPatchSubscriptionPathsTest, EmptyPathsRejected) {
  auto client = server_.getClient();
  auto req = makeRequest("noSub", /*uid=*/1, /*paths=*/{});
  try {
    folly::coro::blockingWait(client->co_addStatePatchSubscriptionPaths(req));
    FAIL() << "expected FsdbException";
  } catch (const FsdbException& e) {
    EXPECT_EQ(*e.errorCode(), FsdbErrorCode::INVALID_REQUEST);
  }
}

// Adding paths to a non-existent subscription is rejected with ID_NOT_FOUND.
TEST_F(FsdbAddPatchSubscriptionPathsTest, UnknownSubscriptionNotFound) {
  auto client = server_.getClient();
  RawOperPath p;
  p.path() = std::vector<std::string>{"agent"};
  auto req =
      makeRequest("noSuchSubscriber", /*uid=*/12345, {{1, std::move(p)}});
  try {
    folly::coro::blockingWait(client->co_addStatePatchSubscriptionPaths(req));
    FAIL() << "expected FsdbException";
  } catch (const FsdbException& e) {
    EXPECT_EQ(*e.errorCode(), FsdbErrorCode::ID_NOT_FOUND);
  }
}

// Stats tree variant: unknown subscription is rejected with ID_NOT_FOUND.
TEST_F(FsdbAddPatchSubscriptionPathsTest, UnknownStatsSubscriptionNotFound) {
  auto client = server_.getClient();
  RawOperPath p;
  p.path() = std::vector<std::string>{"agent"};
  auto req =
      makeRequest("noSuchSubscriber", /*uid=*/12345, {{1, std::move(p)}});
  try {
    folly::coro::blockingWait(client->co_addStatsPatchSubscriptionPaths(req));
    FAIL() << "expected FsdbException";
  } catch (const FsdbException& e) {
    EXPECT_EQ(*e.errorCode(), FsdbErrorCode::ID_NOT_FOUND);
  }
}

// Positive end-to-end: open a live patch subscription, capture the server uid
// from OperSubInitResponse, then append a raw path and confirm the RPC is
// accepted. Data delivery for appended paths is covered via FsdbSubManager.
TEST_F(FsdbAddPatchSubscriptionPathsTest, AddPathsToLiveSubscription) {
  auto client = server_.getClient();

  // Open a patch subscription on "agent"; forceSubscribe so it registers even
  // without a connected publisher.
  SubRequest subReq;
  RawOperPath subPath;
  subPath.path() = std::vector<std::string>{"agent"};
  subReq.paths() = {{0, std::move(subPath)}};
  subReq.clientId()->instanceId() = "liveSub";
  subReq.forceSubscribe() = true;
  auto subResult = folly::coro::blockingWait(client->co_subscribeState(subReq));

  // The server returns a non-empty subscriptionUid that the client echoes back
  // when appending paths.
  ASSERT_TRUE(subResult.response.subscriptionUid().has_value());
  auto uid = *subResult.response.subscriptionUid();

  // Hold the stream open so the subscription stays registered while we append.
  auto stream = std::move(subResult.stream);

  // Append a second raw path (distinct key) under the same publisher root.
  RawOperPath addPath;
  addPath.path() = std::vector<std::string>{"agent"};
  auto addReq = makeRequest("liveSub", uid, {{1, std::move(addPath)}});
  EXPECT_NO_THROW(
      folly::coro::blockingWait(
          client->co_addStatePatchSubscriptionPaths(addReq)));
}

// Calls the handler directly and never consumes the returned stream, so the
// stream generator never runs. The subscription must still be addressable by
// the uid returned in the init response, and be pruned once the stream is
// dropped.
TEST_F(FsdbAddPatchSubscriptionPathsTest, AddWhileStreamNotStartedThenPrune) {
  auto& handler = server_.serviceHandler();
  auto inStorage = [&](int64_t uid) {
    for (const auto& info : handler.getStateStorageSubscriptions()) {
      if (info.subscriptionUid() == uid) {
        return true;
      }
    }
    return false;
  };
  int64_t uid{0};
  {
    // Direct handler calls keep the returned stream unstarted.
    auto subResult = folly::coro::blockingWait(
        // NOLINTNEXTLINE(facebook-thrift-handler-direct-call)
        handler.co_subscribeStateExtended(
            std::make_unique<SubRequest>(
                makeSubRequest("unstartedSub", /*extended=*/true))));
    ASSERT_TRUE(subResult.response.subscriptionUid().has_value());
    uid = *subResult.response.subscriptionUid();

    EXPECT_NO_THROW(
        folly::coro::blockingWait(
            // NOLINTNEXTLINE(facebook-thrift-handler-direct-call)
            handler.co_addStatePatchSubscriptionPaths(
                std::make_unique<AddPatchSubscriptionPathsRequest>(
                    makeAddRequest("unstartedSub", uid, /*extended=*/true)))));
    WITH_RETRIES(EXPECT_EVENTUALLY_TRUE(inStorage(uid)));
  }

  WITH_RETRIES({
    EXPECT_EVENTUALLY_FALSE(inStorage(uid));
    EXPECT_EVENTUALLY_TRUE(server_.getActiveSubscriptions().empty());
  });
}

// A subscription that cannot be created (paths span two publisher roots) gets
// an init response without a uid; the error is reported on the stream with its
// code.
TEST_F(FsdbAddPatchSubscriptionPathsTest, SubscribeErrorReportedOnStream) {
  auto client = server_.getClient();
  SubRequest subReq;
  subReq.clientId()->instanceId() = "crossRootSub";
  subReq.forceSubscribe() = true;
  for (const auto& [key, root] :
       std::map<SubscriptionKey, std::string>{{0, "agent"}, {1, "bgp"}}) {
    OperPathElem elem;
    elem.set_raw(root);
    ExtendedOperPath path;
    path.path() = std::vector<OperPathElem>{std::move(elem)};
    subReq.extPaths()[key] = std::move(path);
  }
  auto subResult =
      folly::coro::blockingWait(client->co_subscribeStateExtended(subReq));
  EXPECT_FALSE(subResult.response.subscriptionUid().has_value());
  auto gen = std::move(subResult.stream).toAsyncGenerator();
  try {
    folly::coro::blockingWait(gen.next());
    FAIL() << "expected FsdbException";
  } catch (const FsdbException& e) {
    EXPECT_EQ(*e.errorCode(), FsdbErrorCode::INVALID_PATH);
  }
}

// Extended paths: empty extPaths map is rejected.
TEST_F(FsdbAddPatchSubscriptionPathsTest, EmptyExtPathsRejected) {
  auto client = server_.getClient();
  auto req = makeExtRequest("noSub", /*uid=*/1, /*extPaths=*/{});
  try {
    folly::coro::blockingWait(client->co_addStatePatchSubscriptionPaths(req));
    FAIL() << "expected FsdbException";
  } catch (const FsdbException& e) {
    EXPECT_EQ(*e.errorCode(), FsdbErrorCode::INVALID_REQUEST);
  }
}

// Extended paths: unknown subscription is rejected with ID_NOT_FOUND.
TEST_F(FsdbAddPatchSubscriptionPathsTest, ExtPathsUnknownSubscriptionNotFound) {
  auto client = server_.getClient();
  ExtendedOperPath ep;
  OperPathElem elem;
  elem.set_raw("agent");
  ep.path() = std::vector<OperPathElem>{std::move(elem)};
  auto req =
      makeExtRequest("noSuchSubscriber", /*uid=*/12345, {{1, std::move(ep)}});
  try {
    folly::coro::blockingWait(client->co_addStatePatchSubscriptionPaths(req));
    FAIL() << "expected FsdbException";
  } catch (const FsdbException& e) {
    EXPECT_EQ(*e.errorCode(), FsdbErrorCode::ID_NOT_FOUND);
  }
}

// Positive end-to-end for extended paths: open a live patch subscription,
// capture the server uid, then append a valid extended path and confirm the
// extended RPC branch is accepted.
TEST_F(FsdbAddPatchSubscriptionPathsTest, AddExtPathsToLiveSubscription) {
  auto client = server_.getClient();

  // Open a patch subscription on "agent"; forceSubscribe so it registers even
  // without a connected publisher.
  SubRequest subReq;
  RawOperPath subPath;
  subPath.path() = std::vector<std::string>{"agent"};
  subReq.paths() = {{0, std::move(subPath)}};
  subReq.clientId()->instanceId() = "liveExtSub";
  subReq.forceSubscribe() = true;
  auto subResult = folly::coro::blockingWait(client->co_subscribeState(subReq));

  ASSERT_TRUE(subResult.response.subscriptionUid().has_value());
  auto uid = *subResult.response.subscriptionUid();

  // Hold the stream open so the subscription stays registered while we append.
  auto stream = std::move(subResult.stream);

  // Append a valid extended path (distinct key) under the same publisher root.
  ExtendedOperPath ep;
  OperPathElem elem;
  elem.set_raw("agent");
  ep.path() = std::vector<OperPathElem>{std::move(elem)};
  auto addReq = makeExtRequest("liveExtSub", uid, {{1, std::move(ep)}});
  EXPECT_NO_THROW(
      folly::coro::blockingWait(
          client->co_addStatePatchSubscriptionPaths(addReq)));
}

// Both paths and extPaths set → INVALID_REQUEST (mutual exclusivity).
TEST_F(FsdbAddPatchSubscriptionPathsTest, BothPathsAndExtPathsRejected) {
  auto client = server_.getClient();
  AddPatchSubscriptionPathsRequest req;
  req.clientId()->instanceId() = "sub";
  req.subscriptionUid() = 1;
  RawOperPath p;
  p.path() = std::vector<std::string>{"agent"};
  req.paths() = {{1, std::move(p)}};
  ExtendedOperPath ep;
  OperPathElem elem;
  elem.set_raw("agent");
  ep.path() = std::vector<OperPathElem>{std::move(elem)};
  req.extPaths() = {{2, std::move(ep)}};
  try {
    folly::coro::blockingWait(client->co_addStatePatchSubscriptionPaths(req));
    FAIL() << "expected FsdbException";
  } catch (const FsdbException& e) {
    EXPECT_EQ(*e.errorCode(), FsdbErrorCode::INVALID_REQUEST);
  }
}

// Serve intervals are long enough that a subscription cannot become visible to
// a live add through a serve cycle within the time of these tests.
class FsdbAddPatchSubscriptionPathsSlowServeTest : public ::testing::Test {
 protected:
  enum class Kind { StateRaw, StateExtended, StatsRaw };

  // Subscribes, then appends a path immediately with no retry.
  void subscribeThenAddImmediately(Kind kind) {
    const bool extended = kind == Kind::StateExtended;
    const bool isStats = kind == Kind::StatsRaw;
    auto client = server_.getClient();
    auto subReq = makeSubRequest("immediateSub", extended);
    auto subResult = folly::coro::blockingWait(
        isStats        ? client->co_subscribeStats(subReq)
            : extended ? client->co_subscribeStateExtended(subReq)
                       : client->co_subscribeState(subReq));
    ASSERT_TRUE(subResult.response.subscriptionUid().has_value());
    auto uid = *subResult.response.subscriptionUid();
    // Hold the stream open so the subscription stays registered.
    auto stream = std::move(subResult.stream);

    auto addReq = makeAddRequest("immediateSub", uid, extended);
    const auto start = std::chrono::steady_clock::now();
    std::optional<FsdbErrorCode> errCode;
    try {
      folly::coro::blockingWait(
          isStats ? client->co_addStatsPatchSubscriptionPaths(addReq)
                  : client->co_addStatePatchSubscriptionPaths(addReq));
    } catch (const FsdbException& e) {
      errCode = *e.errorCode();
    }
    XLOG(INFO) << "add after subscribe took "
               << std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - start)
                      .count()
               << "ms, errorCode="
               << (errCode ? static_cast<int>(*errCode) : -1);
    EXPECT_FALSE(errCode.has_value())
        << "add rejected with FsdbErrorCode " << static_cast<int>(*errCode);
  }

  FsdbTestServer server_{
      0,
      /*stateSubscriptionServe_ms=*/2000,
      /*statsSubscriptionServe_ms=*/2000};
};

// A live add right after the subscribe response must find the subscription
// even though no serve cycle has run.
TEST_F(FsdbAddPatchSubscriptionPathsSlowServeTest, StateRawAddImmediately) {
  subscribeThenAddImmediately(Kind::StateRaw);
}

TEST_F(
    FsdbAddPatchSubscriptionPathsSlowServeTest,
    StateExtendedAddImmediately) {
  subscribeThenAddImmediately(Kind::StateExtended);
}

TEST_F(FsdbAddPatchSubscriptionPathsSlowServeTest, StatsRawAddImmediately) {
  subscribeThenAddImmediately(Kind::StatsRaw);
}

} // namespace facebook::fboss::fsdb::test
