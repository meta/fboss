/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Transceiver.h"
#include "fboss/agent/state/TransceiverMap.h"
#include "fboss/agent/test/TestUtils.h"

#include <gtest/gtest.h>

using namespace facebook::fboss;

namespace {
HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}};
}
} // namespace

TEST(TransceiverSpec, SerializeTransceiver) {
  auto tcvr = std::make_unique<TransceiverSpec>(TransceiverID(1));
  tcvr->setCableLength(3.5);
  tcvr->setMediaInterface(MediaInterfaceCode::FR1_100G);
  tcvr->setManagementInterface(TransceiverManagementInterface::SFF);

  auto serialized = tcvr->toThrift();
  auto tcvrBack = std::make_shared<TransceiverSpec>(serialized);

  EXPECT_TRUE(*tcvr == *tcvrBack);
  validateNodeSerialization(*tcvr);
}

TEST(TransceiverSpec, VendorOverridesToOverrideFactor) {
  TransceiverVendorPartNumber placeholder, transceiver;
  placeholder.vendor() = "Vendor_1";
  placeholder.partNumber() = "PartNum_1";
  transceiver.vendor() = "FBOSSONE";
  transceiver.partNumber() = "PN_1";
  TransceiverVendorOverride vendorOverride;
  vendorOverride.placeholder() = placeholder;
  vendorOverride.transceivers() = {transceiver};
  const std::vector<TransceiverVendorOverride> vendorOverrides = {
      vendorOverride};
  TransceiverInfo tcvrInfo;
  tcvrInfo.tcvrState()->present() = true;
  tcvrInfo.tcvrState()->port() = 1;
  tcvrInfo.tcvrState()->vendorOverrides() = vendorOverrides;

  auto tcvr = TransceiverSpec::createPresentTransceiver(tcvrInfo);

  EXPECT_EQ(tcvr->getVendorOverrides(), vendorOverrides);
  EXPECT_EQ(
      tcvr->toPlatformPortConfigOverrideFactor()
          .vendorOverrides()
          .to_optional(),
      vendorOverrides);
  validateNodeSerialization(*tcvr);
}

TEST(TransceiverSpec, VendorOverridesUnset) {
  TransceiverInfo tcvrInfo;
  tcvrInfo.tcvrState()->present() = true;
  tcvrInfo.tcvrState()->port() = 1;

  auto tcvr = TransceiverSpec::createPresentTransceiver(tcvrInfo);

  EXPECT_FALSE(tcvr->getVendorOverrides().has_value());
  EXPECT_FALSE(
      tcvr->toPlatformPortConfigOverrideFactor().vendorOverrides().has_value());
}

TEST(TransceiverSpec, SerializeSwitchState) {
  auto platform = createMockPlatform();
  auto state = std::make_shared<SwitchState>();

  auto tcvr1 = std::make_shared<TransceiverSpec>(TransceiverID(1));
  tcvr1->setCableLength(3.5);
  tcvr1->setMediaInterface(MediaInterfaceCode::FR1_100G);
  tcvr1->setManagementInterface(TransceiverManagementInterface::SFF);

  auto tcvr2 = std::make_shared<TransceiverSpec>(TransceiverID(2));
  tcvr2->setCableLength(1.5);
  tcvr2->setMediaInterface(MediaInterfaceCode::CWDM4_100G);
  tcvr2->setManagementInterface(TransceiverManagementInterface::CMIS);

  auto transceivers = std::make_shared<MultiSwitchTransceiverMap>();
  transceivers->addNode(tcvr1, scope());
  transceivers->addNode(tcvr2, scope());
  state->resetTransceivers(transceivers);

  auto serialized = state->toThrift();
  auto stateBack = SwitchState::fromThrift(serialized);

  // Check all transceivers should be there
  for (auto tcvrID : {TransceiverID(1), TransceiverID(2)}) {
    EXPECT_TRUE(
        *state->getTransceivers()->getNode(tcvrID) ==
        *stateBack->getTransceivers()->getNode(tcvrID));
  }

  validateNodeSerialization(*tcvr1);
  validateNodeSerialization(*tcvr2);
  validateThriftMapMapSerialization(*transceivers);
}

TEST(TransceiverMap, addTransceiver) {
  auto transceiverMap = std::make_shared<MultiSwitchTransceiverMap>();
  EXPECT_EQ(0, transceiverMap->getGeneration());
  EXPECT_FALSE(transceiverMap->isPublished());

  auto transceiver1 = std::make_shared<TransceiverSpec>(TransceiverID(1));
  auto transceiver2 = std::make_shared<TransceiverSpec>(TransceiverID(2));

  transceiverMap->addNode(transceiver1, scope());
  transceiverMap->addNode(transceiver2, scope());

  auto gotTransceiver1 = transceiverMap->getNode(TransceiverID(1));
  auto gotTransceiver2 = transceiverMap->getNode(TransceiverID(2));
  EXPECT_EQ(TransceiverID(1), gotTransceiver1->getID());
  EXPECT_EQ(TransceiverID(2), gotTransceiver2->getID());

  auto anotherTransceiver2 =
      std::make_shared<TransceiverSpec>(TransceiverID(2));
  // Attempting to register a duplicate transceiver ID should fail
  EXPECT_THROW(
      transceiverMap->addNode(anotherTransceiver2, scope()), FbossError);

  // Registering non-sequential IDs should work
  auto transceiver10 = std::make_shared<TransceiverSpec>(TransceiverID(10));
  transceiverMap->addNode(transceiver10, scope());
  auto gotTransceiver10 = transceiverMap->getNode(TransceiverID(10));
  EXPECT_EQ(TransceiverID(10), gotTransceiver10->getID());

  // Getting non-existent transceiver should fail
  EXPECT_THROW(transceiverMap->getNode(TransceiverID(0)), FbossError);
  EXPECT_THROW(transceiverMap->getNode(TransceiverID(5)), FbossError);
  EXPECT_THROW(transceiverMap->getNode(TransceiverID(200)), FbossError);

  // Publishing the TransceiverMap should also mark all transceivers as
  // published
  transceiverMap->publish();
  EXPECT_TRUE(transceiverMap->isPublished());
  EXPECT_TRUE(transceiver1->isPublished());
  EXPECT_TRUE(transceiver2->isPublished());
  EXPECT_TRUE(transceiver10->isPublished());
}
