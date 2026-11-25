// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/MySidResourceAccountant.h"
#include "fboss/agent/state/MySid.h"
#include "fboss/agent/test/ResourceAccountantTestBase.h"

namespace facebook::fboss {

using MySidResourceAccountantTest = ResourceAccountantTestBase;

TEST_F(MySidResourceAccountantTest, checkMySidResource) {
  MySidResourceAccountant accountant(asicTable_.get());
  FLAGS_enable_mysid_resource_protection = true;

  // MockAsic supports 8 MySID entries
  auto maxMySid = asicTable_->getHwAsic(SwitchID(0))->getMaxMySidEntries();
  ASSERT_TRUE(maxMySid.has_value());
  EXPECT_EQ(maxMySid.value(), 8);

  // Empty state passes both checks
  EXPECT_TRUE(accountant.checkMySidResource(true /* intermediateState */));
  EXPECT_TRUE(accountant.checkMySidResource(false /* intermediateState */));

  // At 75% (6/8) - passes both
  accountant.mySidUsage_ = 6;
  EXPECT_TRUE(accountant.checkMySidResource(true /* intermediateState */));
  EXPECT_TRUE(accountant.checkMySidResource(false /* intermediateState */));

  // At 87.5% (7/8) - passes intermediate (100%), fails final (75%)
  accountant.mySidUsage_ = 7;
  EXPECT_TRUE(accountant.checkMySidResource(true /* intermediateState */));
  EXPECT_FALSE(accountant.checkMySidResource(false /* intermediateState */));

  // At 100% (8/8) - passes intermediate, fails final
  accountant.mySidUsage_ = 8;
  EXPECT_TRUE(accountant.checkMySidResource(true /* intermediateState */));
  EXPECT_FALSE(accountant.checkMySidResource(false /* intermediateState */));

  // Above 100% (9/8) - fails both
  accountant.mySidUsage_ = 9;
  EXPECT_FALSE(accountant.checkMySidResource(true /* intermediateState */));
  EXPECT_FALSE(accountant.checkMySidResource(false /* intermediateState */));
}

TEST_F(MySidResourceAccountantTest, mySidStateChanged) {
  MySidResourceAccountant accountant(asicTable_.get());
  FLAGS_enable_mysid_resource_protection = true;

  HwSwitchMatcher mySidScope(std::unordered_set<SwitchID>{SwitchID(10)});

  auto makeMySidEntry = [](const std::string& addr, uint8_t len) {
    state::MySidFields fields;
    fields.type() = MySidType::NODE_MICRO_SID;
    facebook::network::thrift::IPPrefix thriftPrefix;
    thriftPrefix.prefixAddress() =
        facebook::network::toBinaryAddress(folly::IPAddress(addr));
    thriftPrefix.prefixLength() = len;
    fields.mySid() = thriftPrefix;
    return std::make_shared<MySid>(fields);
  };

  // Add 2 entries
  auto oldState = std::make_shared<SwitchState>();
  oldState->publish();

  auto newState = oldState->clone();
  auto mySids = newState->getMySids()->modify(&newState);
  mySids->addNode(makeMySidEntry("fc00:100::1", 48), mySidScope);
  mySids->addNode(makeMySidEntry("fc00:200::1", 48), mySidScope);
  newState->publish();

  StateDelta addDelta(oldState, newState);
  accountant.stateChanged(addDelta);
  EXPECT_EQ(accountant.mySidUsage_, 2);

  // Remove one entry
  auto newState2 = newState->clone();
  auto mySids2 = newState2->getMySids()->modify(&newState2);
  mySids2->removeNode("fc00:100::1/48");
  newState2->publish();

  StateDelta removeDelta(newState, newState2);
  accountant.stateChanged(removeDelta);
  EXPECT_EQ(accountant.mySidUsage_, 1);

  // Change type (should not affect count)
  auto newState3 = newState2->clone();
  auto mySids3 = newState3->getMySids()->modify(&newState3);
  auto updated = mySids3->getNode("fc00:200::1/48")->clone();
  updated->setType(MySidType::DECAPSULATE_AND_LOOKUP);
  mySids3->updateNode(updated, mySidScope);
  newState3->publish();

  StateDelta changeDelta(newState2, newState3);
  accountant.stateChanged(changeDelta);
  EXPECT_EQ(accountant.mySidUsage_, 1);
}

} // namespace facebook::fboss
