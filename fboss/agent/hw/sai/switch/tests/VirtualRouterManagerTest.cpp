/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiVirtualRouterManager.h"
#include "fboss/agent/hw/sai/switch/tests/ManagerTestBase.h"
#include "fboss/agent/state/Vrf.h"
#include "fboss/agent/state/VrfMap.h"

#include <string>

#include <gtest/gtest.h>

using namespace facebook::fboss;

class VirtualRouterManagerTest : public ManagerTestBase {};

TEST_F(VirtualRouterManagerTest, defaultVirtualRouterTest) {
  SaiVirtualRouterHandle* virtualRouterHandle =
      saiManagerTable->virtualRouterManager().getVirtualRouterHandle(
          RouterID(0));
  EXPECT_TRUE(virtualRouterHandle);
  EXPECT_EQ(virtualRouterHandle->virtualRouter->adapterKey(), uint64_t(0));

  EXPECT_TRUE(virtualRouterHandle->mplsRouterInterface);

  EXPECT_EQ(
      GET_ATTR(
          MplsRouterInterface,
          Type,
          virtualRouterHandle->mplsRouterInterface->attributes()),
      SAI_ROUTER_INTERFACE_TYPE_MPLS_ROUTER);
  EXPECT_EQ(
      GET_ATTR(
          MplsRouterInterface,
          VirtualRouterId,
          virtualRouterHandle->mplsRouterInterface->attributes()),
      virtualRouterHandle->virtualRouter->adapterKey());
}

TEST_F(VirtualRouterManagerTest, getRouterIDTest) {
  // Get the virtual router handle for RouterID(0)
  SaiVirtualRouterHandle* virtualRouterHandle =
      saiManagerTable->virtualRouterManager().getVirtualRouterHandle(
          RouterID(0));
  EXPECT_TRUE(virtualRouterHandle);

  // Get the SAI object ID (adapter key) from the handle
  auto vrId = virtualRouterHandle->virtualRouter->adapterKey();

  // Use the new getRouterID API to do a reverse lookup
  auto routerId = saiManagerTable->virtualRouterManager().getRouterID(vrId);

  // Verify we found the correct RouterID
  EXPECT_EQ(routerId, RouterID(0));
}

TEST_F(VirtualRouterManagerTest, getRouterIDNotFoundTest) {
  // Use an invalid SAI object ID that doesn't exist in the handles map
  sai_object_id_t invalidVrId = 999999;

  // The reverse lookup should throw FbossError
  EXPECT_THROW(
      saiManagerTable->virtualRouterManager().getRouterID(invalidVrId),
      FbossError);
}

TEST_F(VirtualRouterManagerTest, addVirtualRouter) {
  auto& manager = saiManagerTable->virtualRouterManager();
  auto vrId = manager.addVirtualRouter(RouterID(1001));

  auto handle = manager.getVirtualRouterHandle(RouterID(1001));
  ASSERT_NE(handle, nullptr);
  EXPECT_EQ(handle->virtualRouter->adapterKey(), vrId);
  EXPECT_EQ(handle->mplsRouterInterface, nullptr);
  EXPECT_EQ(manager.getRouterID(vrId), RouterID(1001));
}

TEST_F(VirtualRouterManagerTest, addDuplicateVirtualRouter) {
  auto& manager = saiManagerTable->virtualRouterManager();
  manager.addVirtualRouter(RouterID(1001));
  EXPECT_THROW(manager.addVirtualRouter(RouterID(1001)), FbossError);
  EXPECT_THROW(manager.addVirtualRouter(RouterID(0)), FbossError);
}

TEST_F(VirtualRouterManagerTest, removeVirtualRouter) {
  auto& manager = saiManagerTable->virtualRouterManager();
  auto vrId = manager.addVirtualRouter(RouterID(1001));

  manager.removeVirtualRouter(RouterID(1001));

  EXPECT_EQ(manager.getVirtualRouterHandle(RouterID(1001)), nullptr);
  EXPECT_FALSE(FakeSai::getInstance()->virtualRouteManager.exists(vrId));
  EXPECT_NE(manager.getVirtualRouterHandle(RouterID(0)), nullptr);
}

TEST_F(VirtualRouterManagerTest, removeInvalidVirtualRouter) {
  auto& manager = saiManagerTable->virtualRouterManager();
  EXPECT_THROW(manager.removeVirtualRouter(RouterID(0)), FbossError);
  EXPECT_THROW(manager.removeVirtualRouter(RouterID(1001)), FbossError);
}

TEST_F(VirtualRouterManagerTest, vrfDelta) {
  auto& manager = saiManagerTable->virtualRouterManager();
  auto newState = programmedState->clone();
  auto vrf = std::make_shared<Vrf>(RouterID(1001));
  newState->getVrfs()->modify(&newState)->addNode(
      vrf, scopeResolver().scope(vrf));
  applyNewState(newState);
  ASSERT_NE(manager.getVirtualRouterHandle(RouterID(1001)), nullptr);

  newState = programmedState->clone();
  newState->getVrfs()->modify(&newState)->removeNode(RouterID(1001));
  applyNewState(newState);
  EXPECT_EQ(manager.getVirtualRouterHandle(RouterID(1001)), nullptr);
}

class VirtualRouterManagerVrfSetupTest : public ManagerTestBase {
 public:
  void SetUp() override {
    setupStage = SetupStage::PORT | SetupStage::VLAN | SetupStage::VRF |
        SetupStage::INTERFACE;
    testInterfaceRouterIds = {{1, RouterID(1001)}};
    ManagerTestBase::SetUp();
  }
};

TEST_F(VirtualRouterManagerVrfSetupTest, interfaceInVrf) {
  EXPECT_NE(
      saiManagerTable->virtualRouterManager().getVirtualRouterHandle(
          RouterID(1001)),
      nullptr);
  auto state = getProgrammedState();
  EXPECT_EQ(
      state->getInterfaces()->getNode(InterfaceID(1))->getRouterID(),
      RouterID(1001));
  EXPECT_EQ(
      state->getInterfaces()->getNode(InterfaceID(0))->getRouterID(),
      RouterID(0));
}
