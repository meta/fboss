/**
 * (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.
 * This file is generated. Do not modify it manually!
 * @codegen-source : configerator/source/fboss/test/test_metadata.thrift
 * @generated SignedSource<<7a50ad49454d016011ca0a108d005da3>>
 */
#
# @oncall fboss_oss
# Copyright 2004-present Facebook. All Rights Reserved.
#

package "meta.com/fboss/test_metadata"

namespace py neteng.fboss.test_metadata
namespace py3 neteng.fboss
namespace py.asyncio neteng.fboss.asyncio.test_metadata
namespace cpp2 facebook.fboss.test_metadata
namespace go neteng.fboss.test_metadata
namespace php fboss_test_metadata

// Definitions follow the FBOSS test-category documentation:
// https://meta.github.io/fboss/docs/testing/test_categories/#overview
enum TestTier {
  UNSPECIFIED = 0,
  // Simple, critical functionality needed to enable other tests.
  T0 = 1,
  // More complicated tests that verify overall functionality.
  T1 = 2,
  // Complex or performance-related tests required before production.
  T2 = 3,
}

enum TestCapability {
  TRANSCEIVER = 1,
  EXTERNAL_PHY = 2,
}

enum TestProfile {
  // Legacy filter-file profile "t".
  TRADITIONAL = 1,
  // Legacy filter-file profile "s".
  SCALE_UP = 2,
}

struct TestMetadataRule {
  // GTest-compatible filter pattern for concrete test names.
  1: string testNamePattern;
  // Tier assigned to matching tests.
  2: TestTier tier;
  // Capabilities required to run matching tests.
  3: list<TestCapability> requiredCapabilities = [];
  // Empty emits an untagged entry, selected only when no profile is requested.
  4: list<TestProfile> profiles = [];
}

struct TestTypeMetadata {
  1: list<TestMetadataRule> rules = [];
}
