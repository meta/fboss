/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/api/ArsApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/store/tests/SaiStoreTest.h"

using namespace facebook::fboss;

class ArsStoreTest : public SaiStoreTest {
 public:
  ArsSaiId createArs(
      sai_uint32_t idleTime,
      sai_uint32_t maxFlows,
      sai_uint32_t primaryPathQualityThreshold,
      sai_uint32_t alternatePathCost,
      sai_uint32_t alternatePathBias) {
    return saiApiTable->arsApi().create<SaiArsTraits>(
        getArsAttributes(
            idleTime,
            maxFlows,
            primaryPathQualityThreshold,
            alternatePathCost,
            alternatePathBias),
        0);
  }

 protected:
  // Standard attribute set for the mode-key tests -- only Mode varies, so the
  // AdapterHostKey difference can only come from Mode.
  SaiArsTraits::CreateAttributes getArsAttributes(sai_int32_t mode) {
    return getArsAttributes(20000, 2000, 100, 200, 50, mode);
  }

  SaiArsTraits::CreateAttributes getArsAttributes(
      sai_uint32_t idleTime,
      sai_uint32_t maxFlows,
      sai_uint32_t primaryPathQualityThreshold,
      sai_uint32_t alternatePathCost,
      sai_uint32_t alternatePathBias,
      sai_int32_t mode = SAI_ARS_MODE_FLOWLET_QUALITY) {
    return SaiArsTraits::CreateAttributes{
        SaiArsTraits::Attributes::Mode{mode},
        SaiArsTraits::Attributes::IdleTime{idleTime},
        SaiArsTraits::Attributes::MaxFlows{maxFlows},
        SaiArsTraits::Attributes::PrimaryPathQualityThreshold{
            primaryPathQualityThreshold},
        SaiArsTraits::Attributes::AlternatePathCost{alternatePathCost},
        SaiArsTraits::Attributes::AlternatePathBias{alternatePathBias},
        std::nullopt, // NextHopGroupType
        std::nullopt, // SourcePortPrune
        std::nullopt, // EcmpMemberCount
        std::nullopt, // MaxAltMembersPerGroup
        std::nullopt, // MaxPrimaryMembersPerGroup
        std::nullopt}; // CommonMembersThresholdCount
  }
};

TEST_F(ArsStoreTest, loadArs) {
  auto arsSaiId1 = createArs(20000, 2000, 100, 200, 50);

  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiArsTraits>();

  auto attributes = getArsAttributes(20000, 2000, 100, 200, 50);
  auto hostKey = getAdapterHostKey(attributes);
  auto got = store.get(hostKey);
  EXPECT_EQ(got->adapterKey(), arsSaiId1);
}

// Mode is part of SaiArsTraits::AdapterHostKey, so ARS objects differing only
// by switching mode land in distinct store entries. This is what lets the
// standby DLB group object coexist with the primary instead of one
// reprogramming the other.
TEST_F(ArsStoreTest, arsModeDistinguishesStoreEntries) {
  auto flowletAttrs = getArsAttributes(SAI_ARS_MODE_FLOWLET_QUALITY);
  auto fixedAttrs = getArsAttributes(SAI_ARS_MODE_FIXED);

  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiArsTraits>();

  auto flowletObj =
      store.setObject(getAdapterHostKey(flowletAttrs), flowletAttrs);
  auto fixedObj = store.setObject(getAdapterHostKey(fixedAttrs), fixedAttrs);

  EXPECT_NE(flowletObj->adapterKey(), fixedObj->adapterKey());
  EXPECT_EQ(
      GET_ATTR(Ars, Mode, flowletObj->attributes()),
      SAI_ARS_MODE_FLOWLET_QUALITY);
  EXPECT_EQ(GET_ATTR(Ars, Mode, fixedObj->attributes()), SAI_ARS_MODE_FIXED);
}

// Warm boot re-derives the AdapterHostKey by reading attributes back from the
// adapter rather than deserializing a stored key. Both mode-distinguished
// objects must still be recoverable, otherwise one is left unclaimed and the
// agent aborts with "unclaimed objects found in store".
TEST_F(ArsStoreTest, arsModeKeyRecoveredOnReload) {
  auto flowletAttrs = getArsAttributes(SAI_ARS_MODE_FLOWLET_QUALITY);
  auto fixedAttrs = getArsAttributes(SAI_ARS_MODE_FIXED);
  auto flowletSaiId =
      saiApiTable->arsApi().create<SaiArsTraits>(flowletAttrs, 0);
  auto fixedSaiId = saiApiTable->arsApi().create<SaiArsTraits>(fixedAttrs, 0);
  ASSERT_NE(flowletSaiId, fixedSaiId);

  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiArsTraits>();

  auto gotFlowlet = store.get(getAdapterHostKey(flowletAttrs));
  ASSERT_NE(gotFlowlet, nullptr);
  EXPECT_EQ(gotFlowlet->adapterKey(), flowletSaiId);

  auto gotFixed = store.get(getAdapterHostKey(fixedAttrs));
  ASSERT_NE(gotFixed, nullptr);
  EXPECT_EQ(gotFixed->adapterKey(), fixedSaiId);
}

// IdleTime and MaxFlows are part of the key too, so ARS objects sharing a
// switching mode but differing in either one stay distinct across a reload.
TEST_F(ArsStoreTest, arsIdleTimeAndMaxFlowsDistinguishStoreEntries) {
  auto baseAttrs = getArsAttributes(20000, 2000, 100, 200, 50);
  auto idleTimeAttrs = getArsAttributes(40000, 2000, 100, 200, 50);
  auto maxFlowsAttrs = getArsAttributes(20000, 4000, 100, 200, 50);
  auto baseSaiId = saiApiTable->arsApi().create<SaiArsTraits>(baseAttrs, 0);
  auto idleTimeSaiId =
      saiApiTable->arsApi().create<SaiArsTraits>(idleTimeAttrs, 0);
  auto maxFlowsSaiId =
      saiApiTable->arsApi().create<SaiArsTraits>(maxFlowsAttrs, 0);

  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiArsTraits>();

  auto gotBase = store.get(getAdapterHostKey(baseAttrs));
  ASSERT_NE(gotBase, nullptr);
  EXPECT_EQ(gotBase->adapterKey(), baseSaiId);

  auto gotIdleTime = store.get(getAdapterHostKey(idleTimeAttrs));
  ASSERT_NE(gotIdleTime, nullptr);
  EXPECT_EQ(gotIdleTime->adapterKey(), idleTimeSaiId);

  auto gotMaxFlows = store.get(getAdapterHostKey(maxFlowsAttrs));
  ASSERT_NE(gotMaxFlows, nullptr);
  EXPECT_EQ(gotMaxFlows->adapterKey(), maxFlowsSaiId);
}

TEST_F(ArsStoreTest, arsLoadCtor) {
  auto arsSaiId = createArs(20000, 2000, 120, 250, 60);
  auto obj = createObj<SaiArsTraits>(arsSaiId);
  EXPECT_EQ(obj.adapterKey(), arsSaiId);
  EXPECT_EQ(GET_OPT_ATTR(Ars, IdleTime, obj.attributes()), 20000);
  EXPECT_EQ(GET_OPT_ATTR(Ars, MaxFlows, obj.attributes()), 2000);
  EXPECT_EQ(
      GET_OPT_ATTR(Ars, PrimaryPathQualityThreshold, obj.attributes()), 120);
  EXPECT_EQ(GET_OPT_ATTR(Ars, AlternatePathCost, obj.attributes()), 250);
  EXPECT_EQ(GET_OPT_ATTR(Ars, AlternatePathBias, obj.attributes()), 60);
}

TEST_F(ArsStoreTest, arsCreateCtor) {
  SaiArsTraits::CreateAttributes c{
      SaiArsTraits::Attributes::Mode{SAI_ARS_MODE_FLOWLET_QUALITY},
      SaiArsTraits::Attributes::IdleTime{40000},
      SaiArsTraits::Attributes::MaxFlows{4000},
      SaiArsTraits::Attributes::PrimaryPathQualityThreshold{150},
      SaiArsTraits::Attributes::AlternatePathCost{300},
      SaiArsTraits::Attributes::AlternatePathBias{75},
      std::nullopt, // NextHopGroupType
      std::nullopt, // SourcePortPrune
      std::nullopt, // EcmpMemberCount
      std::nullopt, // MaxAltMembersPerGroup
      std::nullopt, // MaxPrimaryMembersPerGroup
      std::nullopt}; // CommonMembersThresholdCount
  auto hostKey = getAdapterHostKey(c);
  auto obj = createObj<SaiArsTraits>(hostKey, c, 0);
  EXPECT_EQ(GET_OPT_ATTR(Ars, IdleTime, obj.attributes()), 40000);
  EXPECT_EQ(GET_OPT_ATTR(Ars, MaxFlows, obj.attributes()), 4000);
  EXPECT_EQ(
      GET_OPT_ATTR(Ars, PrimaryPathQualityThreshold, obj.attributes()), 150);
  EXPECT_EQ(GET_OPT_ATTR(Ars, AlternatePathCost, obj.attributes()), 300);
  EXPECT_EQ(GET_OPT_ATTR(Ars, AlternatePathBias, obj.attributes()), 75);
}

TEST_F(ArsStoreTest, arsSerDeser) {
  auto arsSaiId = createArs(30000, 3000, 130, 260, 65);
  verifyAdapterKeySerDeser<SaiArsTraits>({arsSaiId});
}

TEST_F(ArsStoreTest, arsToStr) {
  std::ignore = createArs(50000, 5000, 140, 280, 70);
  verifyToStr<SaiArsTraits>();
}
