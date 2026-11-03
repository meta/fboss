/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/tracer/SaiTracer.h"

#include <folly/Singleton.h>
#include <gtest/gtest.h>

extern "C" {
#include <sai.h>
}

#include <folly/IPAddress.h>
#include "fboss/agent/hw/sai/api/SaiVersion.h"

// Provide stub implementation of fromSaiIpAddress for sai_ip6_t
// This is needed because the sai_tracer library is built with
// undefined_symbols=True and the test binary doesn't link against address_util
namespace facebook::fboss {
folly::IPAddressV6 fromSaiIpAddress(const sai_ip6_t& ip6) {
  return folly::IPAddressV6::fromBinary(folly::ByteRange(ip6, 16));
}
} // namespace facebook::fboss

DECLARE_bool(enable_replayer);
DECLARE_string(sai_log);

namespace facebook::fboss {

class SaiTracerTest : public ::testing::Test {
 public:
  void SetUp() override {
    FLAGS_enable_replayer = false;
  }

  void TearDown() override {
    FLAGS_enable_replayer = false;
  }

  std::shared_ptr<SaiTracer> getTracer() {
    return SaiTracer::getInstance();
  }

  // Helper to create a set of nexthop group member attributes
  void setupNhgMemberAttrs(
      sai_attribute_t* attrs,
      sai_object_id_t group_oid,
      sai_object_id_t nhop_oid,
      uint32_t weight) {
    attrs[0].id = SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_GROUP_ID;
    attrs[0].value.oid = group_oid;
    attrs[1].id = SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_ID;
    attrs[1].value.oid = nhop_oid;
    attrs[2].id = SAI_NEXT_HOP_GROUP_MEMBER_ATTR_WEIGHT;
    attrs[2].value.u32 = weight;
  }
};

// =====================================================================
// logBulkCreateFn tests - replayer disabled (early return path)
// =====================================================================

TEST_F(SaiTracerTest, LogBulkCreateFnWithZeroObjectsReplayerDisabled) {
  auto tracer = getTracer();
  if (!tracer) {
    return;
  }

  // With replayer disabled and zero objects, should early-return without crash
  tracer->logBulkCreateFn(
      "create_next_hop_group_members",
      0, // switch_id
      0, // object_count
      nullptr, // attr_count
      nullptr, // attr_list
      SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
      nullptr, // object_id
      nullptr, // object_statuses
      SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER,
      SAI_STATUS_SUCCESS);

  // Verify replayer is still disabled (function didn't change the flag)
  EXPECT_FALSE(FLAGS_enable_replayer);
}

TEST_F(SaiTracerTest, LogBulkCreateFnWithSingleObjectReplayerDisabled) {
  auto tracer = getTracer();
  if (!tracer) {
    return;
  }

  sai_attribute_t attrs[3];
  setupNhgMemberAttrs(attrs, 1001, 2001, 1);

  const sai_attribute_t* attr_list[] = {attrs};
  uint32_t attr_count[] = {3};
  sai_object_id_t object_ids[] = {12345};
  sai_status_t object_statuses[] = {SAI_STATUS_SUCCESS};

  tracer->logBulkCreateFn(
      "create_next_hop_group_members",
      100, // switch_id
      1, // object_count
      attr_count,
      attr_list,
      SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
      object_ids,
      object_statuses,
      SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER,
      SAI_STATUS_SUCCESS);

  // Verify data is unchanged after early-return path
  EXPECT_EQ(object_statuses[0], SAI_STATUS_SUCCESS);
  EXPECT_EQ(object_ids[0], 12345);
}

TEST_F(SaiTracerTest, LogBulkCreateFnWithMultipleObjectsReplayerDisabled) {
  auto tracer = getTracer();
  if (!tracer) {
    return;
  }

  sai_attribute_t attrs0[3];
  setupNhgMemberAttrs(attrs0, 1001, 2001, 1);

  sai_attribute_t attrs1[3];
  setupNhgMemberAttrs(attrs1, 1001, 2002, 2);

  sai_attribute_t attrs2[3];
  setupNhgMemberAttrs(attrs2, 1001, 2003, 3);

  const sai_attribute_t* attr_list[] = {attrs0, attrs1, attrs2};
  uint32_t attr_count[] = {3, 3, 3};
  sai_object_id_t object_ids[] = {12345, 12346, 12347};
  sai_status_t object_statuses[] = {
      SAI_STATUS_SUCCESS, SAI_STATUS_SUCCESS, SAI_STATUS_SUCCESS};

  tracer->logBulkCreateFn(
      "create_next_hop_group_members",
      100, // switch_id
      3, // object_count
      attr_count,
      attr_list,
      SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
      object_ids,
      object_statuses,
      SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER,
      SAI_STATUS_SUCCESS);

  // Verify all object data remains intact after early-return
  const std::vector<sai_object_id_t> expectedIds = {12345, 12346, 12347};
  EXPECT_EQ(
      std::vector<sai_object_id_t>(object_ids, object_ids + 3), expectedIds);
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(object_statuses[i], SAI_STATUS_SUCCESS);
  }
}

// =====================================================================
// logBulkCreateFn tests - error status handling
// =====================================================================

TEST_F(SaiTracerTest, LogBulkCreateFnWithFailedStatusReplayerDisabled) {
  auto tracer = getTracer();
  if (!tracer) {
    return;
  }

  sai_attribute_t attrs[3];
  setupNhgMemberAttrs(attrs, 1001, 2001, 1);

  const sai_attribute_t* attr_list[] = {attrs};
  uint32_t attr_count[] = {3};
  sai_object_id_t object_ids[] = {0};
  sai_status_t object_statuses[] = {SAI_STATUS_FAILURE};

  // Should handle failure status gracefully in early-return path
  tracer->logBulkCreateFn(
      "create_next_hop_group_members",
      100,
      1,
      attr_count,
      attr_list,
      SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
      object_ids,
      object_statuses,
      SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER,
      SAI_STATUS_FAILURE);

  // Verify error status is preserved
  EXPECT_EQ(object_statuses[0], SAI_STATUS_FAILURE);
  EXPECT_EQ(object_ids[0], 0);
}

TEST_F(SaiTracerTest, LogBulkCreateFnWithMixedStatusesReplayerDisabled) {
  auto tracer = getTracer();
  if (!tracer) {
    return;
  }

  sai_attribute_t attrs0[3];
  setupNhgMemberAttrs(attrs0, 1001, 2001, 1);

  sai_attribute_t attrs1[3];
  setupNhgMemberAttrs(attrs1, 1001, 2002, 2);

  const sai_attribute_t* attr_list[] = {attrs0, attrs1};
  uint32_t attr_count[] = {3, 3};
  sai_object_id_t object_ids[] = {12345, 0};
  sai_status_t object_statuses[] = {SAI_STATUS_SUCCESS, SAI_STATUS_FAILURE};

  tracer->logBulkCreateFn(
      "create_next_hop_group_members",
      100,
      2,
      attr_count,
      attr_list,
      SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
      object_ids,
      object_statuses,
      SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER,
      SAI_STATUS_SUCCESS);

  // Verify mixed statuses are preserved
  EXPECT_EQ(object_statuses[0], SAI_STATUS_SUCCESS);
  EXPECT_EQ(object_statuses[1], SAI_STATUS_FAILURE);
  EXPECT_EQ(object_ids[0], 12345);
  EXPECT_EQ(object_ids[1], 0);
}

// =====================================================================
// logBulkCreateFn tests - varying attribute counts
// =====================================================================

TEST_F(SaiTracerTest, LogBulkCreateFnWithVaryingAttrCountsReplayerDisabled) {
  auto tracer = getTracer();
  if (!tracer) {
    return;
  }

  // Object 0 has 2 attrs, object 1 has 3 attrs
  sai_attribute_t attrs0[2];
  attrs0[0].id = SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_GROUP_ID;
  attrs0[0].value.oid = 1001;
  attrs0[1].id = SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_ID;
  attrs0[1].value.oid = 2001;

  sai_attribute_t attrs1[3];
  setupNhgMemberAttrs(attrs1, 1001, 2002, 5);

  const sai_attribute_t* attr_list[] = {attrs0, attrs1};
  uint32_t attr_count[] = {2, 3};
  sai_object_id_t object_ids[] = {111, 222};
  sai_status_t object_statuses[] = {SAI_STATUS_SUCCESS, SAI_STATUS_SUCCESS};

  tracer->logBulkCreateFn(
      "create_next_hop_group_members",
      100,
      2,
      attr_count,
      attr_list,
      SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
      object_ids,
      object_statuses,
      SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER,
      SAI_STATUS_SUCCESS);

  // Verify varying attr_count values are preserved
  EXPECT_EQ(attr_count[0], 2u);
  EXPECT_EQ(attr_count[1], 3u);
  EXPECT_EQ(object_ids[0], 111);
  EXPECT_EQ(object_ids[1], 222);
}

// =====================================================================
// logBulkCreateFn tests - different bulk error modes
// =====================================================================

TEST_F(SaiTracerTest, LogBulkCreateFnWithStopOnErrorModeReplayerDisabled) {
  auto tracer = getTracer();
  if (!tracer) {
    return;
  }

  sai_attribute_t attrs[3];
  setupNhgMemberAttrs(attrs, 1001, 2001, 1);

  const sai_attribute_t* attr_list[] = {attrs};
  uint32_t attr_count[] = {3};
  sai_object_id_t object_ids[] = {12345};
  sai_status_t object_statuses[] = {SAI_STATUS_SUCCESS};

  tracer->logBulkCreateFn(
      "create_next_hop_group_members",
      100,
      1,
      attr_count,
      attr_list,
      SAI_BULK_OP_ERROR_MODE_STOP_ON_ERROR,
      object_ids,
      object_statuses,
      SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER,
      SAI_STATUS_SUCCESS);

  EXPECT_EQ(object_statuses[0], SAI_STATUS_SUCCESS);
  EXPECT_EQ(object_ids[0], 12345);
}

// =====================================================================
// logBulkCreateFn tests - replayer flag toggling
// =====================================================================

TEST_F(SaiTracerTest, LogBulkCreateFnReplayerFlagToggle) {
  auto tracer = getTracer();
  if (!tracer) {
    return;
  }

  sai_attribute_t attrs[3];
  setupNhgMemberAttrs(attrs, 1001, 2001, 1);

  const sai_attribute_t* attr_list[] = {attrs};
  uint32_t attr_count[] = {3};
  sai_object_id_t object_ids[] = {12345};
  sai_status_t object_statuses[] = {SAI_STATUS_SUCCESS};

  // Call with replayer disabled (default from SetUp)
  tracer->logBulkCreateFn(
      "create_next_hop_group_members",
      100,
      1,
      attr_count,
      attr_list,
      SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
      object_ids,
      object_statuses,
      SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER,
      SAI_STATUS_SUCCESS);

  EXPECT_EQ(object_ids[0], 12345);

  // Toggle replayer off again explicitly and call again
  FLAGS_enable_replayer = false;
  object_ids[0] = 99999;
  tracer->logBulkCreateFn(
      "create_next_hop_group_members",
      100,
      1,
      attr_count,
      attr_list,
      SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
      object_ids,
      object_statuses,
      SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER,
      SAI_STATUS_SUCCESS);

  // Verify data is still intact after second call
  EXPECT_EQ(object_ids[0], 99999);
}

// =====================================================================
// logBulkRemoveFn tests - replayer disabled (early return path)
// =====================================================================

TEST_F(SaiTracerTest, LogBulkRemoveFnWithZeroObjectsReplayerDisabled) {
  auto tracer = getTracer();
  if (!tracer) {
    return;
  }

  // Zero objects with nullptr should not crash
  tracer->logBulkRemoveFn(
      "remove_next_hop_group_members",
      0, // object_count
      nullptr, // object_id
      SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
      nullptr, // object_statuses
      SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER,
      SAI_STATUS_SUCCESS);

  // Verify replayer flag is unchanged
  EXPECT_FALSE(FLAGS_enable_replayer);
}

TEST_F(SaiTracerTest, LogBulkRemoveFnWithSingleObjectReplayerDisabled) {
  auto tracer = getTracer();
  if (!tracer) {
    return;
  }

  sai_object_id_t object_ids[] = {12345};
  sai_status_t object_statuses[] = {SAI_STATUS_SUCCESS};

  tracer->logBulkRemoveFn(
      "remove_next_hop_group_members",
      1, // object_count
      object_ids,
      SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
      object_statuses,
      SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER,
      SAI_STATUS_SUCCESS);

  EXPECT_EQ(object_statuses[0], SAI_STATUS_SUCCESS);
  EXPECT_EQ(object_ids[0], 12345);
}

TEST_F(SaiTracerTest, LogBulkRemoveFnWithMultipleObjectsReplayerDisabled) {
  auto tracer = getTracer();
  if (!tracer) {
    return;
  }

  sai_object_id_t object_ids[] = {12345, 12346, 12347, 12348};
  sai_status_t object_statuses[] = {
      SAI_STATUS_SUCCESS,
      SAI_STATUS_SUCCESS,
      SAI_STATUS_SUCCESS,
      SAI_STATUS_SUCCESS};

  tracer->logBulkRemoveFn(
      "remove_next_hop_group_members",
      4, // object_count
      object_ids,
      SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
      object_statuses,
      SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER,
      SAI_STATUS_SUCCESS);

  // Verify all object data remains intact
  const std::vector<sai_object_id_t> expectedIds = {12345, 12346, 12347, 12348};
  EXPECT_EQ(
      std::vector<sai_object_id_t>(object_ids, object_ids + 4), expectedIds);
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(object_statuses[i], SAI_STATUS_SUCCESS);
  }
}

// =====================================================================
// logBulkRemoveFn tests - error status handling
// =====================================================================

TEST_F(SaiTracerTest, LogBulkRemoveFnWithFailedStatusReplayerDisabled) {
  auto tracer = getTracer();
  if (!tracer) {
    return;
  }

  sai_object_id_t object_ids[] = {12345};
  sai_status_t object_statuses[] = {SAI_STATUS_FAILURE};

  tracer->logBulkRemoveFn(
      "remove_next_hop_group_members",
      1,
      object_ids,
      SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
      object_statuses,
      SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER,
      SAI_STATUS_FAILURE);

  EXPECT_EQ(object_statuses[0], SAI_STATUS_FAILURE);
  EXPECT_EQ(object_ids[0], 12345);
}

TEST_F(SaiTracerTest, LogBulkRemoveFnWithMixedStatusesReplayerDisabled) {
  auto tracer = getTracer();
  if (!tracer) {
    return;
  }

  sai_object_id_t object_ids[] = {100, 200, 300};
  sai_status_t object_statuses[] = {
      SAI_STATUS_SUCCESS, SAI_STATUS_FAILURE, SAI_STATUS_SUCCESS};

  tracer->logBulkRemoveFn(
      "remove_next_hop_group_members",
      3,
      object_ids,
      SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
      object_statuses,
      SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER,
      SAI_STATUS_SUCCESS);

  // Verify mixed statuses are preserved
  EXPECT_EQ(object_statuses[0], SAI_STATUS_SUCCESS);
  EXPECT_EQ(object_statuses[1], SAI_STATUS_FAILURE);
  EXPECT_EQ(object_statuses[2], SAI_STATUS_SUCCESS);
}

// =====================================================================
// logBulkRemoveFn tests - different bulk error modes
// =====================================================================

TEST_F(SaiTracerTest, LogBulkRemoveFnWithStopOnErrorModeReplayerDisabled) {
  auto tracer = getTracer();
  if (!tracer) {
    return;
  }

  sai_object_id_t object_ids[] = {12345, 12346};
  sai_status_t object_statuses[] = {SAI_STATUS_SUCCESS, SAI_STATUS_SUCCESS};

  tracer->logBulkRemoveFn(
      "remove_next_hop_group_members",
      2,
      object_ids,
      SAI_BULK_OP_ERROR_MODE_STOP_ON_ERROR,
      object_statuses,
      SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER,
      SAI_STATUS_SUCCESS);

  EXPECT_EQ(object_ids[0], 12345);
  EXPECT_EQ(object_ids[1], 12346);
  EXPECT_EQ(object_statuses[0], SAI_STATUS_SUCCESS);
  EXPECT_EQ(object_statuses[1], SAI_STATUS_SUCCESS);
}

// =====================================================================
// logBulkRemoveFn tests - replayer flag toggling
// =====================================================================

TEST_F(SaiTracerTest, LogBulkRemoveFnReplayerFlagToggle) {
  auto tracer = getTracer();
  if (!tracer) {
    return;
  }

  sai_object_id_t object_ids[] = {12345};
  sai_status_t object_statuses[] = {SAI_STATUS_SUCCESS};

  // Call with replayer disabled
  tracer->logBulkRemoveFn(
      "remove_next_hop_group_members",
      1,
      object_ids,
      SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
      object_statuses,
      SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER,
      SAI_STATUS_SUCCESS);

  EXPECT_EQ(object_ids[0], 12345);

  // Toggle and call again
  FLAGS_enable_replayer = false;
  object_ids[0] = 54321;
  tracer->logBulkRemoveFn(
      "remove_next_hop_group_members",
      1,
      object_ids,
      SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
      object_statuses,
      SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER,
      SAI_STATUS_SUCCESS);

  EXPECT_EQ(object_ids[0], 54321);
}

// =====================================================================
// Large batch tests
// =====================================================================

TEST_F(SaiTracerTest, LogBulkCreateFnWithLargeBatchReplayerDisabled) {
  auto tracer = getTracer();
  if (!tracer) {
    return;
  }

  constexpr int kBatchSize = 64;
  sai_attribute_t allAttrs[kBatchSize][3];
  const sai_attribute_t* attr_list[kBatchSize];
  uint32_t attr_count[kBatchSize];
  sai_object_id_t object_ids[kBatchSize];
  sai_status_t object_statuses[kBatchSize];

  for (int i = 0; i < kBatchSize; ++i) {
    setupNhgMemberAttrs(allAttrs[i], 1001, 2000 + i, i + 1);
    attr_list[i] = allAttrs[i];
    attr_count[i] = 3;
    object_ids[i] = 10000 + i;
    object_statuses[i] = SAI_STATUS_SUCCESS;
  }

  tracer->logBulkCreateFn(
      "create_next_hop_group_members",
      100,
      kBatchSize,
      attr_count,
      attr_list,
      SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
      object_ids,
      object_statuses,
      SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER,
      SAI_STATUS_SUCCESS);

  // Verify first, last, and middle elements preserved
  EXPECT_EQ(object_ids[0], 10000);
  EXPECT_EQ(object_ids[kBatchSize / 2], 10000 + kBatchSize / 2);
  EXPECT_EQ(object_ids[kBatchSize - 1], 10000 + kBatchSize - 1);
  for (int i = 0; i < kBatchSize; ++i) {
    EXPECT_EQ(object_statuses[i], SAI_STATUS_SUCCESS);
  }
}

TEST_F(SaiTracerTest, LogBulkRemoveFnWithLargeBatchReplayerDisabled) {
  auto tracer = getTracer();
  if (!tracer) {
    return;
  }

  constexpr int kBatchSize = 64;
  sai_object_id_t object_ids[kBatchSize];
  sai_status_t object_statuses[kBatchSize];

  for (int i = 0; i < kBatchSize; ++i) {
    object_ids[i] = 10000 + i;
    object_statuses[i] = SAI_STATUS_SUCCESS;
  }

  tracer->logBulkRemoveFn(
      "remove_next_hop_group_members",
      kBatchSize,
      object_ids,
      SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
      object_statuses,
      SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER,
      SAI_STATUS_SUCCESS);

  // Verify first, last, and middle elements preserved
  EXPECT_EQ(object_ids[0], 10000);
  EXPECT_EQ(object_ids[kBatchSize / 2], 10000 + kBatchSize / 2);
  EXPECT_EQ(object_ids[kBatchSize - 1], 10000 + kBatchSize - 1);
  for (int i = 0; i < kBatchSize; ++i) {
    EXPECT_EQ(object_statuses[i], SAI_STATUS_SUCCESS);
  }
}

// =====================================================================
// logMySidEntryCreateFn tests - tests setMySidEntry with SID address
// =====================================================================

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
TEST_F(SaiTracerTest, LogMySidEntryCreateFnWithReplayerEnabled) {
  // Destroy existing singleton to create a new one with replayer enabled
  folly::SingletonVault::singleton()->destroyInstances();
  folly::SingletonVault::singleton()->reenableInstances();

  // Enable replayer and set log path before creating singleton
  FLAGS_enable_replayer = true;
  FLAGS_sai_log = "/dev/null";

  auto tracer = getTracer();
  if (!tracer) {
    FLAGS_enable_replayer = false;
    return;
  }

  // Create a my_sid_entry with a valid SID (IPv6 address)
  sai_my_sid_entry_t my_sid_entry{};
  my_sid_entry.switch_id = 100;
  my_sid_entry.vr_id = 200;
  my_sid_entry.locator_block_len = 32;
  my_sid_entry.locator_node_len = 16;
  my_sid_entry.function_len = 16;
  my_sid_entry.args_len = 0;

  // Set up a valid IPv6 SID address (2001:db8::1)
  // IPv6 addresses are stored in network byte order
  my_sid_entry.sid[0] = 0x20;
  my_sid_entry.sid[1] = 0x01;
  my_sid_entry.sid[2] = 0x0d;
  my_sid_entry.sid[3] = 0xb8;
  my_sid_entry.sid[4] = 0x00;
  my_sid_entry.sid[5] = 0x00;
  my_sid_entry.sid[6] = 0x00;
  my_sid_entry.sid[7] = 0x00;
  my_sid_entry.sid[8] = 0x00;
  my_sid_entry.sid[9] = 0x00;
  my_sid_entry.sid[10] = 0x00;
  my_sid_entry.sid[11] = 0x00;
  my_sid_entry.sid[12] = 0x00;
  my_sid_entry.sid[13] = 0x00;
  my_sid_entry.sid[14] = 0x00;
  my_sid_entry.sid[15] = 0x01;

  // Create minimal attributes for the my_sid_entry
  sai_attribute_t attrs[1];
  attrs[0].id = SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR;
  attrs[0].value.s32 = SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_E;

  // Call logMySidEntryCreateFn which internally calls setMySidEntry
  // This exercises the fromSaiIpAddress conversion for the SID
  tracer->logMySidEntryCreateFn(
      &my_sid_entry,
      1, // attr_count
      attrs,
      SAI_STATUS_SUCCESS);

  // Verify replayer flag is still enabled
  EXPECT_TRUE(FLAGS_enable_replayer);

  // Clean up: destroy singleton and reset flags
  tracer.reset();
  folly::SingletonVault::singleton()->destroyInstances();
  folly::SingletonVault::singleton()->reenableInstances();
  FLAGS_enable_replayer = false;
}
#endif

/*
 * A new api has to be registered in three separate maps in SaiTracer --
 * varNames_, varCounts_ (via initVarCounts) and fnPrefix_. All three are
 * folly::get_or_throw lookups keyed on object type, so a missing entry links
 * cleanly and throws the first time the object is traced. Nothing else in the
 * build or test suite exercises those maps, so assert them directly.
 */
TEST_F(SaiTracerTest, IsolationGroupObjectTypesAreRegistered) {
  folly::SingletonVault::singleton()->destroyInstances();
  folly::SingletonVault::singleton()->reenableInstances();

  FLAGS_enable_replayer = true;
  FLAGS_sai_log = "/dev/null";

  auto tracer = getTracer();
  if (!tracer) {
    FLAGS_enable_replayer = false;
    return;
  }

  sai_object_id_t oid = 1000;
  sai_attribute_t attr{};
  attr.id = SAI_ISOLATION_GROUP_ATTR_TYPE;
  attr.value.s32 = SAI_ISOLATION_GROUP_TYPE_PORT;

  // A missing entry throws out of these calls and fails the test.
  for (auto objectType :
       {SAI_OBJECT_TYPE_ISOLATION_GROUP,
        SAI_OBJECT_TYPE_ISOLATION_GROUP_MEMBER}) {
    // varNames_ and varCounts_
    auto [declaration, varName] = tracer->declareVariable(&oid, objectType);
    EXPECT_FALSE(declaration.empty());
    EXPECT_FALSE(varName.empty());
    // fnPrefix_
    EXPECT_FALSE(tracer->logCreateFn("create_fn", &oid, 0, 1, &attr, objectType)
                     .empty());
  }

  tracer.reset();
  folly::SingletonVault::singleton()->destroyInstances();
  folly::SingletonVault::singleton()->reenableInstances();
  FLAGS_enable_replayer = false;
}

} // namespace facebook::fboss
