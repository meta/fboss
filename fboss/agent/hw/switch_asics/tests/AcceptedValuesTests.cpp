/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/switch_asics/HwAsic.h"

#include <gtest/gtest.h>

using namespace facebook::fboss;

TEST(AcceptedValuesTest, rangeAcceptsEndpointsAndNothingOutside) {
  const auto accepted = HwAsic::AcceptedValues::range(2, 500);

  EXPECT_TRUE(accepted.isRange());
  EXPECT_EQ(accepted.asRange(), std::make_tuple(2, 500));

  EXPECT_TRUE(accepted.accepts(2));
  EXPECT_TRUE(accepted.accepts(250));
  EXPECT_TRUE(accepted.accepts(500));
  EXPECT_FALSE(accepted.accepts(1));
  EXPECT_FALSE(accepted.accepts(501));

  EXPECT_EQ(accepted.str(), "[2, 500]");
}

TEST(AcceptedValuesTest, oneOfAcceptsOnlyTheListedValues) {
  const HwAsic::AcceptedValues::Values values{10, 20, 40};
  const auto accepted = HwAsic::AcceptedValues::oneOf(values);

  EXPECT_FALSE(accepted.isRange());
  EXPECT_EQ(accepted.asValues(), values);

  EXPECT_TRUE(accepted.accepts(10));
  EXPECT_TRUE(accepted.accepts(40));
  // In the [10, 40] span, but not a value the SDK takes.
  EXPECT_FALSE(accepted.accepts(30));

  EXPECT_EQ(accepted.str(), "{10, 20, 40}");
}
