/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/api/PortApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"

#include "fboss/agent/hw/sai/store/tests/SaiStoreTest.h"

using namespace facebook::fboss;

class PortStoreTest : public SaiStoreTest {
 protected:
  SaiPortTraits::CreateAttributes makeAttrs(
      uint32_t lane,
      uint32_t speed,
      std::optional<bool> adminStateOpt = true) {
    std::vector<uint32_t> lanes;
    lanes.push_back(lane);
    return SaiPortTraits::CreateAttributes{
        lanes,        speed,        adminStateOpt, std::nullopt,
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
        std::nullopt, std::nullopt,
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 11, 0)
        std::nullopt, // Port Fabric Isolate
#endif
        std::nullopt, std::nullopt, std::nullopt,  std::nullopt, std::nullopt,
        std::nullopt, std::nullopt, std::nullopt,  std::nullopt, std::nullopt,
        std::nullopt, // TAM object
        std::nullopt, // Ingress Mirror Session
        std::nullopt, // Egress Mirror Session
        std::nullopt, // Ingress Sample Packet
        std::nullopt, // Egress Sample Packet
        std::nullopt, // Ingress mirror sample session
        std::nullopt, // Egress mirror sample session
        std::nullopt, // PRBS Polynomial
        std::nullopt, // PRBS Config
        std::nullopt, // Ingress macsec acl
        std::nullopt, // Egress macsec acl
        std::nullopt, // System Port Id
        std::nullopt, // PTP Mode
        std::nullopt, // PFC Mode
        std::nullopt, // PFC Priorities
#if !defined(TAJO_SDK)
        std::nullopt, // PFC Rx Priorities
        std::nullopt, // PFC Tx Priorities
#endif
        std::nullopt, // TC to Priority Group map
        std::nullopt, // PFC Priority to Queue map
        std::nullopt, // PFC Priority to Priority Group map
#if defined(SAI_CBFC_SUPPORTED)
        std::nullopt, // TC to VC map
        std::nullopt, // Queue to VC map
        std::nullopt, // CBFC sender credit size
        std::nullopt, // CBFC sender packet overhead
        std::nullopt, // CBFC sender credit limit
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
        std::nullopt, // Inter Frame Gap
#endif
        std::nullopt, // Link Training Enable
        std::nullopt, // FDR Enable
        std::nullopt, // Rx Lane Squelch Enable
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
        std::nullopt, // PFC Deadlock Detection Interval
        std::nullopt, // PFC Deadlock Recovery Interval
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
        std::nullopt, // ARS enable
        std::nullopt, // ARS scaling factor
        std::nullopt, // ARS port load past weight
        std::nullopt, // ARS port load future weight
#endif
        std::nullopt, // Reachability Group
        std::nullopt, // CondEntropyRehashEnable
        std::nullopt, // CondEntropyRehashPeriodUS
        std::nullopt, // CondEntropyRehashSeed
        std::nullopt, // ShelEnable
        std::nullopt, // FecErrorDetectEnable
        std::nullopt, // AmIdles
        std::nullopt, // FabricSystemPort
        std::nullopt, // StaticModuleId
        std::nullopt, // IsHyperPortMember
        std::nullopt, // HyperPortMemberList
        std::nullopt, // PfcMonitorDirection
        std::nullopt, // QosDot1pToTcMap
        std::nullopt, // QosTcAndColorToDot1pMap
        std::nullopt, // QosIngressBufferProfileList
        std::nullopt, // QosEgressBufferProfileList
        std::nullopt, // CablePropagationDelayMediaType
        std::nullopt, // LinkScanMode
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
        std::nullopt, // LlrModeLocal
        std::nullopt, // LlrModeRemote
        std::nullopt, // LlrProfile
#endif
        std::nullopt, // PfcPauseDurationOverride
        std::nullopt, // Ingress ACL
        std::nullopt, // IsolationGroup
        std::nullopt, // Metadata
        std::nullopt, // CsigTagAction
        std::nullopt, // CsigLinkLocator
    };
  }

  PortSaiId createPort(uint32_t lane) {
    SaiPortTraits::CreateAttributes c = makeAttrs(lane, 100000);
    return saiApiTable->portApi().create<SaiPortTraits>(c, 0);
  }

#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
  SaiPortLlrProfileTraits::CreateAttributes makeLlrProfileAttrs() {
    return SaiPortLlrProfileTraits::CreateAttributes{
        SaiPortLlrProfileTraits::Attributes::OutstandingFramesMax{32},
        SaiPortLlrProfileTraits::Attributes::OutstandingBytesMax{4096},
        SaiPortLlrProfileTraits::Attributes::ReplayTimerMax{5000},
        SaiPortLlrProfileTraits::Attributes::ReplayCountMax{sai_uint8_t(7)},
        SaiPortLlrProfileTraits::Attributes::PcsLostTimeout{1000},
        SaiPortLlrProfileTraits::Attributes::DataAgeTimeout{200000},
        SaiPortLlrProfileTraits::Attributes::InitLlrFrameAction{
            SAI_LLR_FRAME_ACTION_BEST_EFFORT},
        SaiPortLlrProfileTraits::Attributes::FlushLlrFrameAction{
            SAI_LLR_FRAME_ACTION_BLOCK},
        SaiPortLlrProfileTraits::Attributes::ReInitOnFlush{true},
        SaiPortLlrProfileTraits::Attributes::CtlosTargetSpacing{
            sai_uint16_t(2048)}};
  }
#endif
};

TEST_F(PortStoreTest, loadPort) {
  // create a port
  auto id = createPort(0);

  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiPortTraits>();

  std::vector<uint32_t> lanes{0};
  SaiPortTraits::AdapterHostKey k{lanes};
  auto got = store.get(k);
  EXPECT_EQ(got->adapterKey(), id);
}

TEST_F(PortStoreTest, loadPortFromJson) {
  // create a port
  auto id = createPort(0);

  SaiStore s(0);
  s.reload();
  auto json = s.adapterKeysFollyDynamic();
  SaiStore s2(0);
  s2.reload(&json);
  auto& store = s2.get<SaiPortTraits>();

  std::vector<uint32_t> lanes{0};
  SaiPortTraits::AdapterHostKey k{lanes};
  auto got = store.get(k);
  EXPECT_EQ(got->adapterKey(), id);
}

TEST_F(PortStoreTest, portLoadCtor) {
  auto portId = createPort(0);
  auto portObj = createObj<SaiPortTraits>(portId);
  EXPECT_EQ(portObj.adapterKey(), portId);
  EXPECT_EQ(GET_ATTR(Port, Speed, portObj.attributes()), 100000);
  EXPECT_EQ(GET_OPT_ATTR(Port, AdminState, portObj.attributes()), true);
}

TEST_F(PortStoreTest, portCreateCtor) {
  SaiPortTraits::CreateAttributes attrs = makeAttrs(0, 100000);
  SaiPortTraits::AdapterHostKey adapterHostKey =
      std::get<SaiPortTraits::Attributes::HwLaneList>(attrs);
  auto obj = createObj<SaiPortTraits>(adapterHostKey, attrs, 0);
  auto apiSpeed = saiApiTable->portApi().getAttribute(
      obj.adapterKey(), SaiPortTraits::Attributes::Speed{});
  EXPECT_EQ(apiSpeed, 100000);
}

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
TEST_F(PortStoreTest, reloadDoesNotConsumeClearOnReadOperStatusLatch) {
  auto portId = createPort(0);
  auto& fakePort = FakeSai::getInstance()->portManager.get(portId);
  fakePort.portExtOperStatusLatch.current_status = true;
  fakePort.portExtOperStatusLatch.changed = true;

  SaiStore s(0);
  s.reload();

  EXPECT_TRUE(fakePort.portExtOperStatusLatch.changed);
}
#endif

TEST_F(PortStoreTest, portSetSpeed) {
  auto portId = createPort(0);
  SaiObject<SaiPortTraits> portObj = createObj<SaiPortTraits>(portId);
  EXPECT_EQ(GET_ATTR(Port, Speed, portObj.attributes()), 100000);
  auto newAttrs = makeAttrs(0, 25000);
  portObj.setAttributes(newAttrs);
  EXPECT_EQ(GET_ATTR(Port, Speed, portObj.attributes()), 25000);
  auto apiSpeed = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::Speed{});
  EXPECT_EQ(apiSpeed, 25000);
}

TEST_F(PortStoreTest, portSetOnlySpeed) {
  auto portId = createPort(0);
  SaiObject<SaiPortTraits> portObj = createObj<SaiPortTraits>(portId);
  EXPECT_EQ(GET_ATTR(Port, Speed, portObj.attributes()), 100000);
  auto apiSpeed = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::Speed{});
  EXPECT_EQ(apiSpeed, 100000);
  portObj.setAttribute(SaiPortTraits::Attributes::Speed{25000});
  EXPECT_EQ(GET_ATTR(Port, Speed, portObj.attributes()), 25000);
  apiSpeed = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::Speed{});
  EXPECT_EQ(apiSpeed, 25000);
}

TEST_F(PortStoreTest, portSetAdminState) {
  auto portId = createPort(0);
  SaiObject<SaiPortTraits> portObj = createObj<SaiPortTraits>(portId);
  EXPECT_EQ(GET_OPT_ATTR(Port, AdminState, portObj.attributes()), true);
  auto newAttrs = makeAttrs(0, 25000, false);
  portObj.setAttributes(newAttrs);
  EXPECT_EQ(GET_OPT_ATTR(Port, AdminState, portObj.attributes()), false);
  auto apiAdminState = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::AdminState{});
  EXPECT_EQ(apiAdminState, false);
}

TEST_F(PortStoreTest, portSetOnlyAdminState) {
  auto portId = createPort(0);
  SaiObject<SaiPortTraits> portObj = createObj<SaiPortTraits>(portId);
  auto apiAdminState = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::AdminState{});
  EXPECT_EQ(apiAdminState, true);
  portObj.setOptionalAttribute(SaiPortTraits::Attributes::AdminState{false});
  EXPECT_EQ(GET_OPT_ATTR(Port, AdminState, portObj.attributes()), false);
  apiAdminState = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::AdminState{});
  EXPECT_EQ(apiAdminState, false);
}

TEST_F(PortStoreTest, portUnsetAdminState) {
  auto portId = createPort(0);
  SaiObject<SaiPortTraits> portObj = createObj<SaiPortTraits>(portId);
  EXPECT_EQ(GET_OPT_ATTR(Port, AdminState, portObj.attributes()), true);
  auto newAttrs = makeAttrs(0, 25000, std::nullopt);
  portObj.setAttributes(newAttrs);
  /*
  auto apiAdminState = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::AdminState{});
   * Once we properly handle un-setting and defaults, un-setting admin state
   * should make it false.
  EXPECT_EQ(apiAdminState, false);
  */
}

TEST_F(PortStoreTest, portSetMtu) {
  auto portId = createPort(0);
  SaiObject<SaiPortTraits> portObj = createObj<SaiPortTraits>(portId);
  EXPECT_EQ(GET_OPT_ATTR(Port, Mtu, portObj.attributes()), 1514);
  auto newAttrs = makeAttrs(0, 25000);
  constexpr sai_uint32_t kMtu{9000};
  std::get<std::optional<SaiPortTraits::Attributes::Mtu>>(newAttrs) = kMtu;
  portObj.setAttributes(newAttrs);
  EXPECT_EQ(GET_OPT_ATTR(Port, Mtu, portObj.attributes()), kMtu);
  auto apiMtu = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::Mtu{});
  EXPECT_EQ(apiMtu, kMtu);
}

TEST_F(PortStoreTest, portSetIngressAcl) {
  auto portId = createPort(0);
  SaiObject<SaiPortTraits> portObj = createObj<SaiPortTraits>(portId);
  constexpr sai_object_id_t kIngressAclId{42};
  auto newAttrs = makeAttrs(0, 25000);
  std::get<std::optional<SaiPortTraits::Attributes::IngressAcl>>(newAttrs) =
      kIngressAclId;
  portObj.setAttributes(newAttrs);
  EXPECT_EQ(
      GET_OPT_ATTR(Port, IngressAcl, portObj.attributes()), kIngressAclId);
  EXPECT_EQ(
      saiApiTable->portApi().getAttribute(
          portId, SaiPortTraits::Attributes::IngressAcl{}),
      kIngressAclId);
}
#if SAI_API_VERSION >= SAI_VERSION(1, 11, 0)
TEST_F(PortStoreTest, portFabricIsolate) {
  auto portId = createPort(0);
  SaiObject<SaiPortTraits> portObj = createObj<SaiPortTraits>(portId);
  EXPECT_EQ(GET_OPT_ATTR(Port, FabricIsolate, portObj.attributes()), false);
  auto newAttrs = makeAttrs(0, 25000);
  constexpr bool kFabricIsolate{true};
  std::get<std::optional<SaiPortTraits::Attributes::FabricIsolate>>(newAttrs) =
      kFabricIsolate;
  portObj.setAttributes(newAttrs);
  EXPECT_EQ(
      GET_OPT_ATTR(Port, FabricIsolate, portObj.attributes()), kFabricIsolate);
  auto apiFabricIsolate = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::FabricIsolate{});
  EXPECT_EQ(apiFabricIsolate, kFabricIsolate);
}
#endif

TEST_F(PortStoreTest, portSetQoSMaps) {
  auto portId = createPort(0);
  SaiObject<SaiPortTraits> portObj = createObj<SaiPortTraits>(portId);
  EXPECT_EQ(GET_OPT_ATTR(Port, Mtu, portObj.attributes()), 1514);
  auto newAttrs = makeAttrs(0, 25000);
  std::get<std::optional<SaiPortTraits::Attributes::QosDscpToTcMap>>(newAttrs) =
      42;
  std::get<std::optional<SaiPortTraits::Attributes::QosTcToQueueMap>>(
      newAttrs) = 43;
  portObj.setAttributes(newAttrs);
  EXPECT_EQ(GET_OPT_ATTR(Port, QosDscpToTcMap, portObj.attributes()), 42);
  EXPECT_EQ(GET_OPT_ATTR(Port, QosTcToQueueMap, portObj.attributes()), 43);
  auto apiQosDscpToTc = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::QosDscpToTcMap{});
  EXPECT_EQ(apiQosDscpToTc, 42);
  auto apiQosTcToQueue = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::QosTcToQueueMap{});
  EXPECT_EQ(apiQosTcToQueue, 43);
}

TEST_F(PortStoreTest, portSetDisableTtl) {
  auto portId = createPort(0);
  SaiObject<SaiPortTraits> portObj = createObj<SaiPortTraits>(portId);
  EXPECT_FALSE(GET_OPT_ATTR(Port, DisableTtlDecrement, portObj.attributes()));
  auto newAttrs = makeAttrs(0, 25000);
  std::get<std::optional<SaiPortTraits::Attributes::DisableTtlDecrement>>(
      newAttrs) = true;
  portObj.setAttributes(newAttrs);
  EXPECT_TRUE(GET_OPT_ATTR(Port, DisableTtlDecrement, portObj.attributes()));
  EXPECT_TRUE(saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::DisableTtlDecrement{}));
}

TEST_F(PortStoreTest, portSetMetadata) {
  auto portId = createPort(0);
  SaiObject<SaiPortTraits> portObj = createObj<SaiPortTraits>(portId);
  EXPECT_EQ(
      GET_OPT_ATTR(Port, Metadata, portObj.attributes()),
      static_cast<sai_uint32_t>(0));

  constexpr sai_uint32_t kMetadata{42};
  auto newAttrs = makeAttrs(0, 25000);
  std::get<std::optional<SaiPortTraits::Attributes::Metadata>>(newAttrs) =
      kMetadata;
  portObj.setAttributes(newAttrs);

  EXPECT_EQ(GET_OPT_ATTR(Port, Metadata, portObj.attributes()), kMetadata);
  EXPECT_EQ(
      saiApiTable->portApi().getAttribute(
          portId, SaiPortTraits::Attributes::Metadata{}),
      kMetadata);
}
/*
 * Confirm that moving out of a SaiObject<SaiPortTraits> works as expected
 */
TEST_F(PortStoreTest, testMove) {
  auto portId = createPort(0);
  auto portObj = createObj<SaiPortTraits>(portId);
  {
    SaiObject<SaiPortTraits> portObj2(std::move(portObj));
    auto apiSpeed = saiApiTable->portApi().getAttribute(
        portId, SaiPortTraits::Attributes::Speed{});
    EXPECT_EQ(apiSpeed, 100000);
  }
  // port should be deleted
  // TODO(borisb): fix FakeSai to return the correct status code when the
  // object is missing instead of throwing a map::at exception. That will
  // let us improve the expected exception to SaiApiError
  EXPECT_THROW(
      saiApiTable->portApi().getAttribute(
          portId, SaiPortTraits::Attributes::Speed{}),
      std::exception);
}

TEST_F(PortStoreTest, serDeserStore) {
  // create a port
  auto id = createPort(0);
  // Look for this key in port adaptor JSON
  verifyAdapterKeySerDeser<SaiPortTraits>({id}, true);
}

TEST_F(PortStoreTest, toStrStore) {
  std::ignore = createPort(0);
  verifyToStr<SaiPortTraits>();
}

TEST_F(PortStoreTest, portSetPtpMode) {
  auto portId = createPort(0);
  SaiObject<SaiPortTraits> portObj = createObj<SaiPortTraits>(portId);
  EXPECT_EQ(
      GET_OPT_ATTR(Port, PtpMode, portObj.attributes()),
      SAI_PORT_PTP_MODE_NONE);
  auto newAttrs = makeAttrs(0, 25000);
  constexpr sai_uint32_t kPtpMode{SAI_PORT_PTP_MODE_SINGLE_STEP_TIMESTAMP};
  std::get<std::optional<SaiPortTraits::Attributes::PtpMode>>(newAttrs) =
      kPtpMode;
  portObj.setAttributes(newAttrs);
  EXPECT_EQ(GET_OPT_ATTR(Port, PtpMode, portObj.attributes()), kPtpMode);
  auto apiPtpMode = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::PtpMode{});
  EXPECT_EQ(apiPtpMode, kPtpMode);
}

TEST_F(PortStoreTest, portSetDisableLinkTraining) {
  auto portId = createPort(0);
  SaiObject<SaiPortTraits> portObj = createObj<SaiPortTraits>(portId);
  EXPECT_FALSE(GET_OPT_ATTR(Port, LinkTrainingEnable, portObj.attributes()));
  auto newAttrs = makeAttrs(0, 25000);
  std::get<std::optional<SaiPortTraits::Attributes::LinkTrainingEnable>>(
      newAttrs) = true;
  portObj.setAttributes(newAttrs);
  EXPECT_TRUE(GET_OPT_ATTR(Port, LinkTrainingEnable, portObj.attributes()));
  EXPECT_TRUE(saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::LinkTrainingEnable{}));

  std::get<std::optional<SaiPortTraits::Attributes::LinkTrainingEnable>>(
      newAttrs) = false;
  portObj.setAttributes(newAttrs);
  EXPECT_FALSE(GET_OPT_ATTR(Port, LinkTrainingEnable, portObj.attributes()));
  EXPECT_FALSE(saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::LinkTrainingEnable{}));
}

TEST_F(PortStoreTest, portGetLinkTrainingRxStatus) {
  auto portId = createPort(0);
  SaiObject<SaiPortTraits> portObj = createObj<SaiPortTraits>(portId);
  auto rxStatus = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::LinkTrainingRxStatus{});
  EXPECT_EQ(rxStatus, 0); // NOT_TRAINED default
}

TEST_F(PortStoreTest, portGetPortPgPktDropStatus) {
  auto portId = createPort(0);
  SaiObject<SaiPortTraits> portObj = createObj<SaiPortTraits>(portId);

  // Test reading the port PG packet drop status (read-only attribute)
  auto pgPktDropStatus = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::PgDropStatus{});

  // Since this is a read-only attribute, we just verify we can read it
  // The fake implementation returns an empty vector by default
  EXPECT_TRUE(pgPktDropStatus.empty());
}

TEST_F(PortStoreTest, portSetStaticModuleId) {
  auto portId = createPort(0);
  SaiObject<SaiPortTraits> portObj = createObj<SaiPortTraits>(portId);

  // Check default value
  auto apiStaticModuleId = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::StaticModuleId{});
  EXPECT_EQ(apiStaticModuleId, 0);

  // Set static module ID list directly using the API instead of through the
  // SaiObject
  uint32_t moduleId = 1;
  SaiPortTraits::Attributes::StaticModuleId staticModuleId(moduleId);
  saiApiTable->portApi().setAttribute(portId, staticModuleId);

  // Verify the attribute was set correctly
  apiStaticModuleId = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::StaticModuleId{});
  EXPECT_EQ(apiStaticModuleId, moduleId);
}

TEST_F(PortStoreTest, portSetResetQueueCreditBalance) {
  auto portId = createPort(0);
  SaiObject<SaiPortTraits> portObj = createObj<SaiPortTraits>(portId);

  // Check default value
  auto apiResetQueueCreditBalance = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::ResetQueueCreditBalance{});
  EXPECT_EQ(apiResetQueueCreditBalance, false);

  // Set reset queue credit balance to true
  SaiPortTraits::Attributes::ResetQueueCreditBalance resetQueueCreditBalance(
      true);
  saiApiTable->portApi().setAttribute(portId, resetQueueCreditBalance);

  // Verify the attribute was set correctly
  apiResetQueueCreditBalance = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::ResetQueueCreditBalance{});
  EXPECT_EQ(apiResetQueueCreditBalance, true);

  // Set it back to false
  resetQueueCreditBalance =
      SaiPortTraits::Attributes::ResetQueueCreditBalance(false);
  saiApiTable->portApi().setAttribute(portId, resetQueueCreditBalance);

  // Verify the attribute was set correctly
  apiResetQueueCreditBalance = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::ResetQueueCreditBalance{});
  EXPECT_EQ(apiResetQueueCreditBalance, false);
}

TEST_F(PortStoreTest, portSetLinkScanMode) {
  auto portId = createPort(0);
  SaiObject<SaiPortTraits> portObj = createObj<SaiPortTraits>(portId);

  // Check default value
  auto apiLinkScanMode = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::LinkScanMode{});
  EXPECT_EQ(apiLinkScanMode, 0);

  // Set link scan mode to hardware (SAI_PORT_LINKSCAN_MODE_HW == 2)
  SaiPortTraits::Attributes::LinkScanMode linkScanMode(2);
  saiApiTable->portApi().setAttribute(portId, linkScanMode);

  // Verify the attribute was set correctly
  apiLinkScanMode = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::LinkScanMode{});
  EXPECT_EQ(apiLinkScanMode, 2);
}

TEST_F(PortStoreTest, portSetPfcMonitorDirection) {
  auto portId = createPort(0);
  SaiObject<SaiPortTraits> portObj = createObj<SaiPortTraits>(portId);

  // Check default value
  auto apiPfcMonitorDirection = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::PfcMonitorDirection{});
  EXPECT_EQ(apiPfcMonitorDirection, 0);

  // Set PFC monitor direction to 1
  SaiPortTraits::Attributes::PfcMonitorDirection pfcMonitorDirection(1);
  saiApiTable->portApi().setAttribute(portId, pfcMonitorDirection);

  // Verify the attribute was set correctly
  apiPfcMonitorDirection = saiApiTable->portApi().getAttribute(
      portId, SaiPortTraits::Attributes::PfcMonitorDirection{});
  EXPECT_EQ(apiPfcMonitorDirection, 1);
}

#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
// A port bound to an LLR profile is reclaimed on warm-boot reload with its
// LlrProfile binding intact, and the referenced profile object is reclaimed
// too.
TEST_F(PortStoreTest, loadPortBoundToLlrProfile) {
  auto profileId = saiApiTable->portApi().create<SaiPortLlrProfileTraits>(
      makeLlrProfileAttrs(), 0);
  // Admin disabled: the fake refuses a profile attach on an enabled port, as
  // the SDK does (CS00012478409). This passed before only because the profile
  // was given id 0, making the attach below a write of SAI_NULL_OBJECT_ID.
  auto portId = saiApiTable->portApi().create<SaiPortTraits>(
      makeAttrs(0, 100000, /*adminStateOpt*/ false), 0);
  saiApiTable->portApi().setAttribute(
      portId,
      SaiPortTraits::Attributes::LlrProfile{
          static_cast<sai_object_id_t>(profileId)});

  SaiStore s(0);
  s.reload();

  // Port reclaimed and still bound to the profile.
  std::vector<uint32_t> lanes{0};
  auto gotPort =
      s.get<SaiPortTraits>().get(SaiPortTraits::AdapterHostKey{lanes});
  ASSERT_NE(gotPort, nullptr);
  EXPECT_EQ(gotPort->adapterKey(), portId);
  EXPECT_EQ(
      saiApiTable->portApi().getAttribute(
          gotPort->adapterKey(), SaiPortTraits::Attributes::LlrProfile{}),
      static_cast<sai_object_id_t>(profileId));

  // The referenced profile object is reclaimed too.
  SaiPortLlrProfileTraits::AdapterHostKey profileKey = makeLlrProfileAttrs();
  auto gotProfile = s.get<SaiPortLlrProfileTraits>().get(profileKey);
  ASSERT_NE(gotProfile, nullptr);
  EXPECT_EQ(gotProfile->adapterKey(), profileId);
}
#endif
