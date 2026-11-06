/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/fixmyfboss/CheckEnvironment.h"

#include <gtest/gtest.h>

using namespace facebook::fboss::platform::fixmyfboss;

TEST(CheckEnvironmentTest, DeriveOobHostname) {
  EXPECT_EQ(deriveOobHostname("fboss1.snc1"), "fboss1-oob.snc1");
  EXPECT_EQ(
      deriveOobHostname("rsw1.p001.f01.snc1.facebook.com"),
      "rsw1-oob.p001.f01.snc1.facebook.com");
  EXPECT_EQ(deriveOobHostname("fboss1"), "fboss1-oob");
}
