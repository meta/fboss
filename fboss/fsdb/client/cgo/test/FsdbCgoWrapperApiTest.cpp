// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/fsdb/client/cgo/FsdbCgoPubSubWrapper.h"

#include <folly/init/Phase.h>
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include "fboss/fsdb/client/FsdbStreamClient.h"

using namespace facebook::fboss::fsdb;

namespace facebook::fboss::fsdb::test {

// ============================================================================
// Tests for extern "C" API functions
// ============================================================================

class FsdbCgoWrapperApiTest : public ::testing::Test {};

TEST_F(FsdbCgoWrapperApiTest, FsdbCgoAbiVersionMatchesHeader) {
  EXPECT_EQ(FsdbCgoAbiVersion(), FSDB_CGO_ABI_VERSION);
}

TEST_F(FsdbCgoWrapperApiTest, FsdbInitRejectsAbiMismatch) {
  // Both directions: too-old and too-new consumer.
  EXPECT_EQ(FsdbInit(FSDB_CGO_ABI_VERSION - 1), 2);
  EXPECT_EQ(FsdbInit(FSDB_CGO_ABI_VERSION + 1), 2);
}

// FsdbInit tests
TEST_F(FsdbCgoWrapperApiTest, FsdbInitCanBeCalledOnce) {
  EXPECT_EQ(FsdbInit(FSDB_CGO_ABI_VERSION), 0);
}

TEST_F(FsdbCgoWrapperApiTest, FsdbInitIsIdempotent) {
  // Phase advances past Init iff the folly::call_once body actually ran.
  EXPECT_EQ(FsdbInit(FSDB_CGO_ABI_VERSION), 0);
  EXPECT_NE(folly::get_process_phase(), folly::ProcessPhase::Init);
  EXPECT_EQ(FsdbInit(FSDB_CGO_ABI_VERSION), 0);
  EXPECT_EQ(FsdbInit(FSDB_CGO_ABI_VERSION), 0);
  EXPECT_NE(folly::get_process_phase(), folly::ProcessPhase::Init);
}

// FsdbSetFlag tests
TEST_F(FsdbCgoWrapperApiTest, FsdbSetFlagNullName) {
  EXPECT_EQ(FsdbSetFlag(nullptr, "1"), 1);
}

TEST_F(FsdbCgoWrapperApiTest, FsdbSetFlagNullValue) {
  EXPECT_EQ(FsdbSetFlag("logtostderr", nullptr), 1);
}

TEST_F(FsdbCgoWrapperApiTest, FsdbSetFlagUnknownFlag) {
  EXPECT_EQ(FsdbSetFlag("this_flag_does_not_exist_anywhere", "x"), 1);
}

TEST_F(FsdbCgoWrapperApiTest, FsdbSetFlagKnownFlag) {
  // logtostderr is a glog flag, always defined when glog is linked in.
  EXPECT_EQ(FsdbSetFlag("logtostderr", "1"), 0);
}

TEST_F(FsdbCgoWrapperApiTest, CreateFsdbWrapperSetsChunkTimeouts) {
  gflags::FlagSaver flagSaver;
  ASSERT_EQ(FsdbInit(FSDB_CGO_ABI_VERSION), 0);
  FsdbWrapperHandle handle = CreateFsdbWrapper("chunk-timeout-defaults");
  ASSERT_NE(handle, nullptr);
  EXPECT_EQ(FLAGS_fsdb_state_chunk_timeout, 30);
  EXPECT_EQ(FLAGS_fsdb_stat_chunk_timeout, 90);
  DestroyFsdbWrapper(handle);
}

TEST_F(FsdbCgoWrapperApiTest, CreateFsdbWrapperKeepsConsumerChunkTimeout) {
  gflags::FlagSaver flagSaver;
  ASSERT_EQ(FsdbInit(FSDB_CGO_ABI_VERSION), 0);
  ASSERT_EQ(FsdbSetFlag("fsdb_state_chunk_timeout", "0"), 0);
  FsdbWrapperHandle handle = CreateFsdbWrapper("chunk-timeout-override");
  ASSERT_NE(handle, nullptr);
  EXPECT_EQ(FLAGS_fsdb_state_chunk_timeout, 0);
  EXPECT_EQ(FLAGS_fsdb_stat_chunk_timeout, 90);
  DestroyFsdbWrapper(handle);
}

// WaitForStateUpdates null-handle tests
TEST_F(FsdbCgoWrapperApiTest, WaitForStateUpdatesNullHandle) {
  FsdbPortStateUpdate out[10];
  EXPECT_EQ(WaitForStateUpdates(nullptr, out, 10), -1);
}

TEST_F(FsdbCgoWrapperApiTest, WaitForStateUpdatesNullOut) {
  // Even with a valid-looking handle, null out should return -1
  // We use a dummy non-null handle; the null-out check comes first
  int dummy = 0;
  EXPECT_EQ(WaitForStateUpdates(&dummy, nullptr, 10), -1);
}

TEST_F(FsdbCgoWrapperApiTest, WaitForStateUpdatesZeroMaxCount) {
  FsdbPortStateUpdate out[1];
  int dummy = 0;
  EXPECT_EQ(WaitForStateUpdates(&dummy, out, 0), -1);
}

// WaitForStatsUpdates null-handle tests
TEST_F(FsdbCgoWrapperApiTest, WaitForStatsUpdatesNullHandle) {
  FsdbStatsUpdate out[10];
  EXPECT_EQ(WaitForStatsUpdates(nullptr, out, 10), -1);
}

TEST_F(FsdbCgoWrapperApiTest, WaitForStatsUpdatesNullOut) {
  int dummy = 0;
  EXPECT_EQ(WaitForStatsUpdates(&dummy, nullptr, 10), -1);
}

TEST_F(FsdbCgoWrapperApiTest, WaitForStatsUpdatesZeroMaxCount) {
  FsdbStatsUpdate out[1];
  int dummy = 0;
  EXPECT_EQ(WaitForStatsUpdates(&dummy, out, 0), -1);
}

// FreeStateUpdates / FreeStatsUpdates null-handle safety
TEST_F(FsdbCgoWrapperApiTest, FreeStateUpdatesNullHandle) {
  // Should not crash
  EXPECT_NO_THROW(FreeStateUpdates(nullptr));
}

TEST_F(FsdbCgoWrapperApiTest, FreeStatsUpdatesNullHandle) {
  // Should not crash
  EXPECT_NO_THROW(FreeStatsUpdates(nullptr));
}

// A null or empty host is accepted (treated as localhost) and must not crash;
// with no reachable server it simply leaves the subscription unestablished or
// pending — the contract here is "no throw, no crash".
TEST_F(FsdbCgoWrapperApiTest, SubscribeToPortMapsNullHost) {
  ASSERT_EQ(FsdbInit(FSDB_CGO_ABI_VERSION), 0);
  FsdbWrapperHandle handle = CreateFsdbWrapper("portmaps-null-host");
  ASSERT_NE(handle, nullptr);
  EXPECT_NO_THROW(SubscribeToPortMaps(handle, nullptr, -1));
  EXPECT_NO_THROW(SubscribeToPortMaps(handle, "", -1));
  DestroyFsdbWrapper(handle);
}

TEST_F(FsdbCgoWrapperApiTest, SubscribeToStatsNullTokens) {
  ASSERT_EQ(FsdbInit(FSDB_CGO_ABI_VERSION), 0);
  FsdbWrapperHandle handle = CreateFsdbWrapper("stats-path-null-tokens");
  ASSERT_NE(handle, nullptr);
  EXPECT_NO_THROW(SubscribeToStats(handle, nullptr, 1, nullptr, -1));
  EXPECT_EQ(HasStatsSubscription(handle), 0);
  DestroyFsdbWrapper(handle);
}

TEST_F(FsdbCgoWrapperApiTest, SubscribeToStatsZeroTokens) {
  ASSERT_EQ(FsdbInit(FSDB_CGO_ABI_VERSION), 0);
  FsdbWrapperHandle handle = CreateFsdbWrapper("stats-path-zero-tokens");
  ASSERT_NE(handle, nullptr);
  const char* tokens[] = {"agent"};
  EXPECT_NO_THROW(SubscribeToStats(handle, tokens, 0, nullptr, -1));
  EXPECT_EQ(HasStatsSubscription(handle), 0);
  DestroyFsdbWrapper(handle);
}

// ShutdownFsdbWrapper null-safety and idempotency
TEST_F(FsdbCgoWrapperApiTest, ShutdownFsdbWrapperNullHandle) {
  EXPECT_EQ(ShutdownFsdbWrapper(nullptr), 0);
}

TEST_F(FsdbCgoWrapperApiTest, ShutdownFsdbWrapperIdempotent) {
  ASSERT_EQ(FsdbInit(FSDB_CGO_ABI_VERSION), 0);
  FsdbWrapperHandle handle = CreateFsdbWrapper("shutdown-idempotent");
  ASSERT_NE(handle, nullptr);
  EXPECT_EQ(ShutdownFsdbWrapper(handle), 0);
  EXPECT_EQ(ShutdownFsdbWrapper(handle), 0);
  DestroyFsdbWrapper(handle);
}

// Full lifecycle: Create, check no subscription, WaitFor returns -1, Free,
// Destroy
TEST_F(FsdbCgoWrapperApiTest, FullLifecycleNoSubscription) {
  ASSERT_EQ(FsdbInit(FSDB_CGO_ABI_VERSION), 0);

  FsdbWrapperHandle handle = CreateFsdbWrapper("api-test-client");
  ASSERT_NE(handle, nullptr);

  // No subscription yet
  EXPECT_EQ(HasStateSubscription(handle), 0);
  EXPECT_EQ(HasStatsSubscription(handle), 0);
  EXPECT_EQ(HasStatePathSubscription(handle), 0);

  // GetClientId should work
  const char* clientId = GetClientId(handle);
  ASSERT_NE(clientId, nullptr);
  EXPECT_STREQ(clientId, "api-test-client");

  // WaitFor* should return -1 (no subscription)
  FsdbPortStateUpdate stateOut[10];
  EXPECT_EQ(WaitForStateUpdates(handle, stateOut, 10), -1);

  FsdbStatsUpdate statsOut[10];
  EXPECT_EQ(WaitForStatsUpdates(handle, statsOut, 10), -1);

  FsdbStatePathUpdate spOut[10];
  EXPECT_EQ(WaitForStatePathUpdates(handle, spOut, 10), -1);

  // Free should be safe even without prior WaitFor
  FreeStateUpdates(handle);
  FreeStatsUpdates(handle);
  FreeStatePathUpdates(handle);

  // Destroy
  DestroyFsdbWrapper(handle);
}

// New state-path API: null safety
TEST_F(FsdbCgoWrapperApiTest, SubscribeToStateNullHandle) {
  const char* tokens[] = {"agent"};
  EXPECT_NO_THROW(SubscribeToState(nullptr, tokens, 1, nullptr, -1));
}

TEST_F(FsdbCgoWrapperApiTest, SubscribeToStateNullTokens) {
  ASSERT_EQ(FsdbInit(FSDB_CGO_ABI_VERSION), 0);
  FsdbWrapperHandle handle = CreateFsdbWrapper("state-path-null-tokens");
  ASSERT_NE(handle, nullptr);
  EXPECT_NO_THROW(SubscribeToState(handle, nullptr, 1, nullptr, -1));
  EXPECT_EQ(HasStatePathSubscription(handle), 0);
  DestroyFsdbWrapper(handle);
}

TEST_F(FsdbCgoWrapperApiTest, SubscribeToStateZeroTokens) {
  ASSERT_EQ(FsdbInit(FSDB_CGO_ABI_VERSION), 0);
  FsdbWrapperHandle handle = CreateFsdbWrapper("state-path-zero-tokens");
  ASSERT_NE(handle, nullptr);
  const char* tokens[] = {"agent"};
  EXPECT_NO_THROW(SubscribeToState(handle, tokens, 0, nullptr, -1));
  EXPECT_EQ(HasStatePathSubscription(handle), 0);
  DestroyFsdbWrapper(handle);
}

TEST_F(FsdbCgoWrapperApiTest, WaitForStatePathUpdatesNullHandle) {
  FsdbStatePathUpdate out[10];
  EXPECT_EQ(WaitForStatePathUpdates(nullptr, out, 10), -1);
}

TEST_F(FsdbCgoWrapperApiTest, WaitForStatePathUpdatesNullOut) {
  int dummy = 0;
  EXPECT_EQ(WaitForStatePathUpdates(&dummy, nullptr, 10), -1);
}

TEST_F(FsdbCgoWrapperApiTest, WaitForStatePathUpdatesZeroMaxCount) {
  int dummy = 0;
  FsdbStatePathUpdate out[1];
  EXPECT_EQ(WaitForStatePathUpdates(&dummy, out, 0), -1);
}

TEST_F(FsdbCgoWrapperApiTest, FreeStatePathUpdatesNullHandle) {
  EXPECT_NO_THROW(FreeStatePathUpdates(nullptr));
}

TEST_F(FsdbCgoWrapperApiTest, HasStatePathSubscriptionNullHandle) {
  EXPECT_EQ(HasStatePathSubscription(nullptr), 0);
}

// Existing C API functions with null handles
TEST_F(FsdbCgoWrapperApiTest, CreateFsdbWrapperNullClientId) {
  EXPECT_EQ(CreateFsdbWrapper(nullptr), nullptr);
}

TEST_F(FsdbCgoWrapperApiTest, DestroyFsdbWrapperNullHandle) {
  // Should not crash
  EXPECT_NO_THROW(DestroyFsdbWrapper(nullptr));
}

TEST_F(FsdbCgoWrapperApiTest, HasStateSubscriptionNullHandle) {
  EXPECT_EQ(HasStateSubscription(nullptr), 0);
}

TEST_F(FsdbCgoWrapperApiTest, HasStatsSubscriptionNullHandle) {
  EXPECT_EQ(HasStatsSubscription(nullptr), 0);
}

TEST_F(FsdbCgoWrapperApiTest, GetClientIdNullHandle) {
  EXPECT_EQ(GetClientId(nullptr), nullptr);
}

TEST_F(FsdbCgoWrapperApiTest, SubscribeToPortMapsNullHandle) {
  EXPECT_NO_THROW(SubscribeToPortMaps(nullptr, nullptr, -1));
}

TEST_F(FsdbCgoWrapperApiTest, SubscribeToStatsNullHandle) {
  const char* tokens[] = {"agent"};
  EXPECT_NO_THROW(SubscribeToStats(nullptr, tokens, 1, nullptr, -1));
}

} // namespace facebook::fboss::fsdb::test
