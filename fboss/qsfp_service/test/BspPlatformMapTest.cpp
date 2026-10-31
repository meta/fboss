// Copyright 2021-present Facebook. All Rights Reserved.

#include <folly/testing/TestUtil.h>
#include <gtest/gtest.h>
#include "fboss/lib/bsp/icecube800bc/Icecube800bcBspPlatformMapping.h"
#include "fboss/lib/bsp/janga800bic/Janga800bicBspPlatformMapping.h"
#include "fboss/lib/bsp/ladakh800bcls/Ladakh800bclsBspPlatformMapping.h"
#include "fboss/lib/bsp/leh800bcls/Leh800bclsBspPlatformMapping.h"
#include "fboss/lib/bsp/m4061nhp/M4061nhpBspPlatformMapping.h"
#include "fboss/lib/bsp/m4062nhp/M4062nhpBspPlatformMapping.h"
#include "fboss/lib/bsp/m4340nhp/M4340nhpBspPlatformMapping.h"
#include "fboss/lib/bsp/meru800bfa/Meru800bfaBspPlatformMapping.h"
#include "fboss/lib/bsp/meru800bia/Meru800biaBspPlatformMapping.h"
#include "fboss/lib/bsp/minipack3bta/Minipack3BTABspPlatformMapping.h"
#include "fboss/lib/bsp/minipack3n/Minipack3NBspPlatformMapping.h"
#include "fboss/lib/bsp/montblanc/MontblancBspPlatformMapping.h"
#include "fboss/lib/bsp/morgan800cc/Morgan800ccBspPlatformMapping.h"
#include "fboss/lib/bsp/tahan800bc/Tahan800bcBspPlatformMapping.h"
#include "fboss/lib/bsp/tahansb800bc/Tahansb800bcBspPlatformMapping.h"
#include "fboss/lib/bsp/wedge800bact/Wedge800BACTBspPlatformMapping.h"
#include "fboss/lib/bsp/wedge800cact/Wedge800CACTBspPlatformMapping.h"

namespace facebook::fboss {

class BspPlatformMapTest : public ::testing::Test {
 public:
};

TEST_F(BspPlatformMapTest, checkNumPimTransceivers) {
  // Check Montblanc
  auto mbBspPlatformMap = MontblancBspPlatformMapping();
  EXPECT_EQ(mbBspPlatformMap.numPims(), 1);
  EXPECT_EQ(mbBspPlatformMap.numTransceivers(), 65);
  // Check Minkpack3BTA
  auto mp3btaBspPlatformMap = Minipack3BTABspPlatformMapping();
  EXPECT_EQ(mp3btaBspPlatformMap.numPims(), 1);
  EXPECT_EQ(mp3btaBspPlatformMap.numTransceivers(), 65);
  // Check Minipack3N
  auto mp3nBspPlatformMap = Minipack3NBspPlatformMapping();
  EXPECT_EQ(mp3nBspPlatformMap.numPims(), 1);
  EXPECT_EQ(mp3nBspPlatformMap.numTransceivers(), 65);
  // Check Icecube800bc
  auto icecube800bcBspPlatformMap = Icecube800bcBspPlatformMapping();
  EXPECT_EQ(icecube800bcBspPlatformMap.numPims(), 1);
  EXPECT_EQ(icecube800bcBspPlatformMap.numTransceivers(), 65);
  // Check Tahansb800bc
  auto tahansb800bcBspPlatformMap = Tahansb800bcBspPlatformMapping();
  EXPECT_EQ(tahansb800bcBspPlatformMap.numPims(), 1);
  EXPECT_EQ(tahansb800bcBspPlatformMap.numTransceivers(), 9);
  // Check WEDGE800BACT
  auto wedge800bactBspPlatformMap = Wedge800BACTBspPlatformMapping();
  EXPECT_EQ(wedge800bactBspPlatformMap.numPims(), 1);
  EXPECT_EQ(wedge800bactBspPlatformMap.numTransceivers(), 33);
  // Check WEDGE800CACT
  auto wedge800cactBspPlatformMap = Wedge800CACTBspPlatformMapping();
  EXPECT_EQ(wedge800cactBspPlatformMap.numPims(), 1);
  EXPECT_EQ(wedge800cactBspPlatformMap.numTransceivers(), 33);
  // Check Janga800bic
  auto janga800bicBspPlatformMap = Janga800bicBspPlatformMapping();
  EXPECT_EQ(janga800bicBspPlatformMap.numPims(), 1);
  EXPECT_EQ(janga800bicBspPlatformMap.numTransceivers(), 46);
  // Check Tahan800bc
  auto tahan800bcBspPlatformMap = Tahan800bcBspPlatformMapping();
  EXPECT_EQ(tahan800bcBspPlatformMap.numPims(), 1);
  EXPECT_EQ(tahan800bcBspPlatformMap.numTransceivers(), 33);
  // Check Ladakh800bcls
  auto ladakh800bclsBspPlatformMap = Ladakh800bclsBspPlatformMapping();
  EXPECT_EQ(ladakh800bclsBspPlatformMap.numPims(), 1);
  EXPECT_EQ(ladakh800bclsBspPlatformMap.numTransceivers(), 2);
  // Check Leh800bcls
  auto leh800bclsBspPlatformMap = Leh800bclsBspPlatformMapping();
  EXPECT_EQ(leh800bclsBspPlatformMap.numPims(), 1);
  EXPECT_EQ(leh800bclsBspPlatformMap.numTransceivers(), 2);
  // Check Meru800bia
  auto m800biaBspPlatformMap = Meru800biaBspPlatformMapping();
  EXPECT_EQ(m800biaBspPlatformMap.numPims(), 1);
  EXPECT_EQ(m800biaBspPlatformMap.numTransceivers(), 39);
  // Check Meru800bfa
  auto m800bfaBspPlatformMap = Meru800bfaBspPlatformMapping();
  EXPECT_EQ(m800bfaBspPlatformMap.numPims(), 1);
  EXPECT_EQ(m800bfaBspPlatformMap.numTransceivers(), 128);
  // Check Morgan800cc
  auto morgan800ccBspPlatformMap = Morgan800ccBspPlatformMapping();
  EXPECT_EQ(morgan800ccBspPlatformMap.numPims(), 1);
  EXPECT_EQ(morgan800ccBspPlatformMap.numTransceivers(), 65);
  // Check M4062nhp
  auto m4062nhpBspPlatformMap = M4062nhpBspPlatformMapping();
  EXPECT_EQ(m4062nhpBspPlatformMap.numPims(), 1);
  EXPECT_EQ(m4062nhpBspPlatformMap.numTransceivers(), 129);
  // Check M4061nhp: 64 OSFP + 1 QSFP28 mgmt port
  auto m4061nhpBspPlatformMap = M4061nhpBspPlatformMapping();
  EXPECT_EQ(m4061nhpBspPlatformMap.numPims(), 1);
  EXPECT_EQ(m4061nhpBspPlatformMap.numTransceivers(), 65);
  EXPECT_TRUE(m4061nhpBspPlatformMap.hasTcvrMapping(65));
  EXPECT_FALSE(m4061nhpBspPlatformMap.hasTcvrMapping(66));
  EXPECT_EQ(m4061nhpBspPlatformMap.getLedId(65, 1), 65);
  // Check M4340nhp
  auto m4340nhpBspPlatformMap = M4340nhpBspPlatformMapping();
  EXPECT_EQ(m4340nhpBspPlatformMap.numPims(), 1);
  EXPECT_EQ(m4340nhpBspPlatformMap.numTransceivers(), 66);
  EXPECT_TRUE(m4340nhpBspPlatformMap.hasTcvrMapping(66));
  EXPECT_EQ(m4340nhpBspPlatformMap.getLedId(66, 1), 66);
}

} // namespace facebook::fboss
