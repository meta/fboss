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
#include "fboss/agent/hw/sai/api/SaiApiError.h"
#include "fboss/agent/hw/sai/api/SaiObjectApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <folly/logging/xlog.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

using namespace facebook::fboss;

class PortApiTest : public ::testing::Test {
 public:
  void SetUp() override {
    fs = FakeSai::getInstance();
    sai_api_initialize(0, nullptr);
    portApi = std::make_unique<PortApi>();
  }

  PortSaiId createPort(
      uint32_t speed,
      const std::vector<uint32_t>& lanes,
      bool adminState) const {
    SaiPortTraits::CreateAttributes a{
        lanes,        speed,        adminState,   std::nullopt,
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
        std::nullopt, std::nullopt,
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 11, 0)
        std::nullopt, // Port Fabric Isolate
#endif
        std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
        std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
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
        std::nullopt, // Inter frame gap
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
    return portApi->create<SaiPortTraits>(a, 0);
  }

  PortSerdesSaiId createPortSerdes(
      PortSaiId portSaiId,
      std::vector<sai_uint32_t> preemphasis,
      std::vector<sai_uint32_t> txPre1,
      std::vector<sai_uint32_t> txMain,
      std::vector<sai_uint32_t> txPost1,
      std::vector<sai_int32_t> rxCtlCode,
      std::vector<sai_int32_t> rxDspMode,
      std::vector<sai_int32_t> rxAfeTrim,
      std::vector<sai_int32_t> rxAcCouplingByPass,
      std::vector<sai_int32_t> rxAfeAdaptiveEnable,
      std::vector<sai_uint32_t> txPre3) const {
    SaiPortSerdesTraits::CreateAttributes a{
        portSaiId,
#if !defined(CHENAB_SAI_SDK)
        preemphasis,
#endif
        std::nullopt, // IDriver
        txPre1,
        std::nullopt, // txPre2
        txPre3,
        txMain,
        txPost1,
        std::nullopt, // txPost2
        std::nullopt, // txPost3
        std::nullopt, // txLutMode
        rxCtlCode,
        rxDspMode,
        rxAfeTrim,
        rxAcCouplingByPass,
        rxAfeAdaptiveEnable,
        std::nullopt, // txDiffEncoderEn
        std::nullopt, // txDigGain
        std::nullopt, // txFfeCoeff0
        std::nullopt, // txFfeCoeff1
        std::nullopt, // txFfeCoeff2
        std::nullopt, // txFfeCoeff3
        std::nullopt, // txFfeCoeff4
        std::nullopt, // txDriverSwing
        std::nullopt, // rxInstgBoost1Start
        std::nullopt, // rxInstgBoost1Step
        std::nullopt, // rxInstgBoost1Stop
        std::nullopt, // rxInstgBoost2OrHrStart
        std::nullopt, // rxInstgBoost2OrHrStep
        std::nullopt, // rxInstgBoost2OrHrStop
        std::nullopt, // rxInstgC1Start1p7
        std::nullopt, // rxInstgC1Step1p7
        std::nullopt, // rxInstgC1Stop1p7
        std::nullopt, // rxInstgDfeStart1p7
        std::nullopt, // rxInstgDfeStep1p7
        std::nullopt, // rxInstgDfeStop1p7
        std::nullopt, // rxEnableScanSelection
        std::nullopt, // rxInstgScanUseSrSettings
        std::nullopt, // rxCdrCfgOvEn
        std::nullopt, // rxCdrTdet1stOrdStepOvVal
        std::nullopt, // rxCdrTdet2ndOrdStepOvVal
        std::nullopt, // rxCdrTdetFineStepOvVal
        std::nullopt, // TxLdoBypass
        std::nullopt, // RxLdoBypass
        std::nullopt, // RxDiffEncoderEn
        std::nullopt, // RxInstgEnableScan
        std::nullopt, // RxFfeLengthBitmap
        std::nullopt, // RxFfeLmsDynamicGatingEn
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 4)
        std::nullopt, // CustomCollection
#endif
#if defined(BRCM_SAI_SDK_GTE_13_0)
        std::nullopt, // RxReach
#endif
#if defined(BRCM_SAI_SDK_GTE_13_0) ||            \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) && \
     !defined(BRCM_SAI_SDK_XGS_AND_DNX))
        std::nullopt, // TxPrecoding
        std::nullopt, // RxPrecoding
#endif
    };
    return portApi->create<SaiPortSerdesTraits>(a, 0 /*switch id*/);
  }

  std::vector<PortSaiId> createFivePorts() const {
    std::vector<PortSaiId> portIds;
    portIds.push_back(createPort(100000, {0, 1, 2, 3}, true));
    for (uint32_t i = 4; i < 8; ++i) {
      portIds.push_back(createPort(25000, {i}, false));
    }
    for (const auto& portId : portIds) {
      checkPort(portId);
    }
    return portIds;
  }

  void checkPort(PortSaiId portId) const {
    SaiPortTraits::Attributes::AdminState adminStateAttribute;
    std::vector<uint32_t> ls;
    ls.resize(4);
    SaiPortTraits::Attributes::HwLaneList hwLaneListAttribute(ls);
    SaiPortTraits::Attributes::Speed speedAttribute;
    auto gotAdminState = portApi->getAttribute(portId, adminStateAttribute);
    auto gotSpeed = portApi->getAttribute(portId, speedAttribute);
    auto lanes = portApi->getAttribute(portId, hwLaneListAttribute);
    EXPECT_EQ(fs->portManager.get(portId).adminState, gotAdminState);
    EXPECT_EQ(fs->portManager.get(portId).speed, gotSpeed);
    EXPECT_EQ(fs->portManager.get(portId).id, portId);
    EXPECT_EQ(fs->portManager.get(portId).lanes, lanes);
  }

  std::shared_ptr<FakeSai> fs;
  std::unique_ptr<PortApi> portApi;
};

TEST_F(PortApiTest, onePort) {
  auto id = createPort(100000, {42}, true);
  SaiPortTraits::Attributes::AdminState as_blank;
  std::vector<uint32_t> ls;
  ls.resize(1);
  SaiPortTraits::Attributes::HwLaneList l_blank(ls);
  SaiPortTraits::Attributes::Speed s_blank;
  EXPECT_EQ(portApi->getAttribute(id, as_blank), true);
  EXPECT_EQ(portApi->getAttribute(id, s_blank), 100000);
  auto lanes = portApi->getAttribute(id, l_blank);
  EXPECT_EQ(lanes.size(), 1);
  EXPECT_EQ(lanes[0], 42);
}

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
TEST_F(PortApiTest, readExtOperStatusLatchClearsChanged) {
  auto id = createPort(100000, {42}, true);
  auto& fakePort = fs->portManager.get(id);
  fakePort.portExtOperStatusLatch.current_status = true;
  fakePort.portExtOperStatusLatch.changed = true;

  auto firstRead = portApi->getAttribute(
      id, SaiPortTraits::Attributes::ExtOperStatusLatch{});
  EXPECT_TRUE(firstRead.current_status);
  EXPECT_TRUE(firstRead.changed);

  auto secondRead = portApi->getAttribute(
      id, SaiPortTraits::Attributes::ExtOperStatusLatch{});
  EXPECT_TRUE(secondRead.current_status);
  EXPECT_FALSE(secondRead.changed);
}
#endif

#if defined(SAI_CBFC_SUPPORTED)
TEST_F(PortApiTest, readNativeCbfcReceiverAttributes) {
  auto id = createPort(100000, {42}, true);

  // READ_ONLY, derived by hardware from the MMU carving. Fake cans them, so
  // this asserts the types and union members round-trip, not the values.
  EXPECT_EQ(
      portApi->getAttribute(
          id, SaiPortTraits::Attributes::CbfcReceiverNativeCreditSize{}),
      256);
  EXPECT_EQ(
      portApi->getAttribute(
          id, SaiPortTraits::Attributes::CbfcReceiverNativeTotalCredits{}),
      1000);
  // Signed on purpose: the spec range is -16..127, and modelling this as
  // unsigned would read back 65520 rather than -16.
  EXPECT_EQ(
      portApi->getAttribute(
          id, SaiPortTraits::Attributes::CbfcReceiverNativePacketOverhead{}),
      -16);
}

TEST_F(PortApiTest, setCbfcSenderCreditSize) {
  auto id = createPort(100000, {42}, true);
  EXPECT_EQ(
      portApi->getAttribute(
          id, SaiPortTraits::Attributes::CbfcSenderCreditSize{}),
      128);

  portApi->setAttribute(
      id, SaiPortTraits::Attributes::CbfcSenderCreditSize{56});
  EXPECT_EQ(
      portApi->getAttribute(
          id, SaiPortTraits::Attributes::CbfcSenderCreditSize{}),
      56);
}

TEST_F(PortApiTest, setCbfcSenderPacketOverhead) {
  auto id = createPort(100000, {42}, true);
  EXPECT_EQ(
      portApi->getAttribute(
          id, SaiPortTraits::Attributes::CbfcSenderPacketOverhead{}),
      0);

  // Negative is legal (-16..127) and catches an unsigned model.
  portApi->setAttribute(
      id, SaiPortTraits::Attributes::CbfcSenderPacketOverhead{-16});
  EXPECT_EQ(
      portApi->getAttribute(
          id, SaiPortTraits::Attributes::CbfcSenderPacketOverhead{}),
      -16);
}
#endif

TEST_F(PortApiTest, fourPorts) {
  auto portIds = createFivePorts();
}

TEST_F(PortApiTest, setPortAttributes) {
  auto portIds = createFivePorts();

  SaiPortTraits::Attributes::AdminState as_attr(true);
  SaiPortTraits::Attributes::Speed speed_attr(50000);
  constexpr sai_object_id_t kIngressAclId{42};
  SaiPortTraits::Attributes::IngressAcl ingressAclAttr{kIngressAclId};
  // set speeds
  portApi->setAttribute(portIds[0], speed_attr);
  portApi->setAttribute(portIds[2], speed_attr);
  // set admin state
  portApi->setAttribute(portIds[2], as_attr);
  portApi->setAttribute(portIds[0], ingressAclAttr);
  // confirm admin states
  EXPECT_EQ(portApi->getAttribute(portIds[0], as_attr), true);
  EXPECT_EQ(portApi->getAttribute(portIds[1], as_attr), false);
  EXPECT_EQ(portApi->getAttribute(portIds[2], as_attr), true);
  EXPECT_EQ(portApi->getAttribute(portIds[3], as_attr), false);
  // confirm speeds
  EXPECT_EQ(portApi->getAttribute(portIds[0], speed_attr), 50000);
  EXPECT_EQ(portApi->getAttribute(portIds[1], speed_attr), 25000);
  EXPECT_EQ(portApi->getAttribute(portIds[2], speed_attr), 50000);
  EXPECT_EQ(portApi->getAttribute(portIds[3], speed_attr), 25000);
  EXPECT_EQ(portApi->getAttribute(portIds[0], ingressAclAttr), kIngressAclId);
  // confirm consistency internally, too
  for (const auto& portId : portIds) {
    checkPort(portId);
  }
}

TEST_F(PortApiTest, removePort) {
  {
    // basic remove
    auto portId = createPort(25000, {42}, true);
    portApi->remove(portId);
  }
  {
    // remove a const portId
    const auto portId = createPort(25000, {42}, true);
    portApi->remove((portId));
  }
  {
    // remove rvalue id
    portApi->remove(createPort(25000, {42}, true));
  }
  {
    // remove in "canonical" for-loop
    auto portIds = createFivePorts();
    for (const auto& portId : portIds) {
      portApi->remove(portId);
    }
  }
}

TEST_F(PortApiTest, getLaneListPresized) {
  std::vector<uint32_t> inLanes{0, 1, 2, 3};
  auto portId = createPort(100000, inLanes, true);
  std::vector<uint32_t> tempLanes;
  tempLanes.resize(4);
  SaiPortTraits::Attributes::HwLaneList hwLaneListAttr{tempLanes};
  auto gotLanes = portApi->getAttribute(portId, hwLaneListAttr);
  EXPECT_EQ(gotLanes, inLanes);
}

TEST_F(PortApiTest, getLaneListUnsized) {
  std::vector<uint32_t> inLanes{0, 1, 2, 3};
  auto portId = createPort(100000, inLanes, true);
  SaiPortTraits::Attributes::HwLaneList hwLaneListAttr;
  auto gotLanes = portApi->getAttribute(portId, hwLaneListAttr);
  EXPECT_EQ(gotLanes, inLanes);
}

TEST_F(PortApiTest, setGetOptionalAttributes) {
  auto portId = createPort(25000, {42}, true);

  // Fec Mode get/set
  int32_t saiFecMode = SAI_PORT_FEC_MODE_RS;
  SaiPortTraits::Attributes::FecMode fecMode{saiFecMode};
  portApi->setAttribute(portId, fecMode);
  auto gotFecMode = portApi->getAttribute(portId, fecMode);
  EXPECT_EQ(gotFecMode, saiFecMode);

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  // Internal Loopback Mode get/set
  int32_t saiLoopbackMode = SAI_PORT_LOOPBACK_MODE_MAC;
  SaiPortTraits::Attributes::PortLoopbackMode loopbackMode{saiLoopbackMode};
  portApi->setAttribute(portId, loopbackMode);
  auto gotLoopbackMode = portApi->getAttribute(portId, loopbackMode);
  EXPECT_EQ(gotLoopbackMode, saiLoopbackMode);
#endif

  // Internal Loopback Mode get/set
  int32_t saiInternalLoopbackMode = SAI_PORT_INTERNAL_LOOPBACK_MODE_MAC;
  SaiPortTraits::Attributes::InternalLoopbackMode internalLoopbackMode{
      saiInternalLoopbackMode};
  portApi->setAttribute(portId, internalLoopbackMode);
  auto gotInternalLoopbackMode =
      portApi->getAttribute(portId, internalLoopbackMode);
  EXPECT_EQ(gotInternalLoopbackMode, saiInternalLoopbackMode);

  // Media type get/set
  int32_t saiMediaType = SAI_PORT_MEDIA_TYPE_COPPER;
  SaiPortTraits::Attributes::MediaType mediaType{saiMediaType};
  portApi->setAttribute(portId, mediaType);
  auto gotMediaType = portApi->getAttribute(portId, mediaType);
  EXPECT_EQ(gotMediaType, saiMediaType);

  // Global Flow Control get/set
  int32_t saiFlowControl = SAI_PORT_FLOW_CONTROL_MODE_RX_ONLY;
  SaiPortTraits::Attributes::GlobalFlowControlMode flowControl{saiFlowControl};
  portApi->setAttribute(portId, flowControl);
  auto gotFlowControl = portApi->getAttribute(portId, flowControl);
  EXPECT_EQ(gotFlowControl, saiFlowControl);

  // Ingress port vlan get/set
  sai_vlan_id_t saiPortVlanId = 2000;
  SaiPortTraits::Attributes::PortVlanId portVlanId{saiPortVlanId};
  portApi->setAttribute(portId, portVlanId);
  auto gotPortVlanId = portApi->getAttribute(portId, portVlanId);
  EXPECT_EQ(gotPortVlanId, saiPortVlanId);

  // Port MTU
  sai_uint32_t mtu{9000};
  SaiPortTraits::Attributes::Mtu portMtu{mtu};
  portApi->setAttribute(portId, portMtu);
  auto gotPortMtu = portApi->getAttribute(portId, portMtu);
  EXPECT_EQ(gotPortMtu, mtu);

  // Port metadata
  constexpr sai_uint32_t kMetadata{42};
  SaiPortTraits::Attributes::Metadata metadata{kMetadata};
  portApi->setAttribute(portId, metadata);
  EXPECT_EQ(portApi->getAttribute(portId, metadata), kMetadata);

  // Port DSCP to TC
  sai_object_id_t qosMapDscpToTc{42};
  SaiPortTraits::Attributes::QosDscpToTcMap portDscpToTc{qosMapDscpToTc};
  portApi->setAttribute(portId, portDscpToTc);
  auto gotPortDscpToTc = portApi->getAttribute(portId, portDscpToTc);
  EXPECT_EQ(gotPortDscpToTc, qosMapDscpToTc);

  // Port TC to queue
  sai_object_id_t qosMapTcToQueue{43};
  SaiPortTraits::Attributes::QosTcToQueueMap portTcToQueue{qosMapTcToQueue};
  portApi->setAttribute(portId, portTcToQueue);
  auto gotPortTcToQueue = portApi->getAttribute(portId, portTcToQueue);
  EXPECT_EQ(gotPortTcToQueue, qosMapTcToQueue);

  // Port PFC priority to priority group
  sai_object_id_t qosMapPfcPriorityToPg{46};
  SaiPortTraits::Attributes::QosPfcPriorityToPriorityGroupMap
      portPfcPriorityToPg{qosMapPfcPriorityToPg};
  portApi->setAttribute(portId, portPfcPriorityToPg);
  auto gotPortPfcPriorityToPg =
      portApi->getAttribute(portId, portPfcPriorityToPg);
  EXPECT_EQ(gotPortPfcPriorityToPg, qosMapPfcPriorityToPg);

  // Port Dot1p (PCP) to TC map get/set
  sai_object_id_t qosMapDot1pToTc{44};
  SaiPortTraits::Attributes::QosDot1pToTcMap portDot1pToTc{qosMapDot1pToTc};
  portApi->setAttribute(portId, portDot1pToTc);
  auto gotPortDot1pToTc = portApi->getAttribute(portId, portDot1pToTc);
  EXPECT_EQ(gotPortDot1pToTc, qosMapDot1pToTc);

  // Port TC and Color to Dot1p (PCP) map get/set
  sai_object_id_t qosMapTcAndColorToDot1p{45};
  SaiPortTraits::Attributes::QosTcAndColorToDot1pMap portTcAndColorToDot1p{
      qosMapTcAndColorToDot1p};
  portApi->setAttribute(portId, portTcAndColorToDot1p);
  auto gotPortTcAndColorToDot1p =
      portApi->getAttribute(portId, portTcAndColorToDot1p);
  EXPECT_EQ(gotPortTcAndColorToDot1p, qosMapTcAndColorToDot1p);

  // Port TTL decrement
  SaiPortTraits::Attributes::DisableTtlDecrement disableTtlDec{true};
  portApi->setAttribute(portId, disableTtlDec);
  EXPECT_TRUE(portApi->getAttribute(portId, disableTtlDec));

  // Pkt TX
  SaiPortTraits::Attributes::PktTxEnable txEnable{false};
  portApi->setAttribute(portId, txEnable);
  EXPECT_FALSE(portApi->getAttribute(portId, txEnable));

  // System port ID
  uint16_t systemPortId{1001};
  portApi->setAttribute(
      portId, SaiPortTraits::Attributes::SystemPortId{systemPortId});
  auto gotSystemPortId =
      portApi->getAttribute(portId, SaiPortTraits::Attributes::SystemPortId{});
  EXPECT_EQ(gotSystemPortId, systemPortId);

  // Prbs Polynomial
  uint32_t prbsPolynomial{42};
  portApi->setAttribute(
      portId, SaiPortTraits::Attributes::PrbsPolynomial{prbsPolynomial});
  auto gotPrbsPolynomial = portApi->getAttribute(
      portId, SaiPortTraits::Attributes::PrbsPolynomial{});
  EXPECT_EQ(gotPrbsPolynomial, prbsPolynomial);

  // Prbs Config
  int32_t prbsConfig = SAI_PORT_PRBS_CONFIG_ENABLE_TX_RX;
  portApi->setAttribute(
      portId, SaiPortTraits::Attributes::PrbsConfig{prbsConfig});
  auto gotPrbsConfig =
      portApi->getAttribute(portId, SaiPortTraits::Attributes::PrbsConfig{});
  EXPECT_EQ(gotPrbsConfig, prbsConfig);

  // PTP Mode get/set
  int32_t saiPtpMode = SAI_PORT_PTP_MODE_NONE;
  SaiPortTraits::Attributes::PtpMode ptpMode{saiPtpMode};
  portApi->setAttribute(portId, ptpMode);
  auto gotPtpMode = portApi->getAttribute(portId, ptpMode);
  EXPECT_EQ(gotPtpMode, saiPtpMode);

#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
  // Inter frame gap
  uint32_t interFrameGap = 352;
  portApi->setAttribute(
      portId, SaiPortTraits::Attributes::InterFrameGap{interFrameGap});
  auto gotInterFrameGap =
      portApi->getAttribute(portId, SaiPortTraits::Attributes::InterFrameGap{});
  EXPECT_EQ(interFrameGap, gotInterFrameGap);
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 11, 0)
  // Port Fabric Isolate
  SaiPortTraits::Attributes::FabricIsolate fabricIsolate_attr(true);
  portApi->setAttribute(portId, fabricIsolate_attr);
  EXPECT_EQ(portApi->getAttribute(portId, fabricIsolate_attr), true);
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  // ARS related attributes
  // Ars enable
  SaiPortTraits::Attributes::ArsEnable arsEnable_attr(true);
  portApi->setAttribute(portId, arsEnable_attr);
  EXPECT_EQ(portApi->getAttribute(portId, arsEnable_attr), true);
  // Ars scaling factor
  SaiPortTraits::Attributes::ArsPortLoadScalingFactor
      arsPortLoadScalingFactor_attr(16);
  portApi->setAttribute(portId, arsPortLoadScalingFactor_attr);
  EXPECT_EQ(portApi->getAttribute(portId, arsPortLoadScalingFactor_attr), 16);
  // Ars port load past weight
  SaiPortTraits::Attributes::ArsPortLoadPastWeight arsPortLoadPastWeight_attr(
      60);
  portApi->setAttribute(portId, arsPortLoadPastWeight_attr);
  EXPECT_EQ(portApi->getAttribute(portId, arsPortLoadPastWeight_attr), 60);
  // Ars port load future weight
  SaiPortTraits::Attributes::ArsPortLoadFutureWeight
      arsPortLoadFutureWeight_attr(20);
  portApi->setAttribute(portId, arsPortLoadFutureWeight_attr);
  EXPECT_EQ(portApi->getAttribute(portId, arsPortLoadFutureWeight_attr), 20);
#endif

  // Link scan mode get/set (SAI_PORT_LINKSCAN_MODE_HW == 2)
  SaiPortTraits::Attributes::LinkScanMode linkScanMode{2};
  portApi->setAttribute(portId, linkScanMode);
  EXPECT_EQ(portApi->getAttribute(portId, linkScanMode), 2);
}

// ObjectApi tests
TEST_F(PortApiTest, portCount) {
  createFivePorts();
  auto count = getObjectCount<SaiPortTraits>(0);
  EXPECT_EQ(count, 5);
}

TEST_F(PortApiTest, portKeys) {
  auto portIds = createFivePorts();
  auto keys = getObjectKeys<SaiPortTraits>(0);
  EXPECT_EQ(keys.size(), 5);
  std::sort(portIds.begin(), portIds.end());
  std::sort(keys.begin(), keys.end());
  EXPECT_EQ(keys, portIds);
}

TEST_F(PortApiTest, getAllStats) {
  auto id = createPort(100000, {42}, true);
  auto stats = portApi->getStats<SaiPortTraits>(id, SAI_STATS_MODE_READ);
  EXPECT_EQ(stats.size(), SaiPortTraits::CounterIdsToRead.size());
}

TEST_F(PortApiTest, getSome) {
  auto id = createPort(100000, {42}, true);
  auto stats = portApi->getStats<SaiPortTraits>(
      id,
      {SAI_PORT_STAT_IF_IN_OCTETS, SAI_PORT_STAT_IF_IN_UCAST_PKTS},
      SAI_STATS_MODE_READ);
  EXPECT_EQ(stats.size(), 2);
}

#if defined(SAI_CBFC_SUPPORTED)
// Three of the six SAI_PORT_STAT_CBFC_* counters have no BCM counter behind
// them on Tomahawk Ultra 1 (SENDER_CREDITS_USED is B0-stepping only). Because
// get_port_stats is all-or-nothing, one of them in the list would fail every
// CBFC stat read, so guard against them creeping back in.
TEST_F(PortApiTest, cbfcStatsOmitsUnbackedCounters) {
  const auto& ids = SaiPortTraits::cbfcStats();
  for (auto unbacked :
       {SAI_PORT_STAT_CBFC_SENDER_CREDITS_USED,
        SAI_PORT_STAT_CBFC_SENDER_CREDITS_USED_WATERMARK,
        SAI_PORT_STAT_CBFC_NUM_CC_UPDATE_MESSAGES_RX}) {
    EXPECT_EQ(std::find(ids.begin(), ids.end(), unbacked), ids.end());
  }
}

TEST_F(PortApiTest, getCbfcStats) {
  auto id = createPort(100000, {42}, true);
  auto stats = portApi->getStats<SaiPortTraits>(
      id, SaiPortTraits::cbfcStats(), SAI_STATS_MODE_READ);
  EXPECT_EQ(stats.size(), SaiPortTraits::cbfcStats().size());
}
#endif

TEST_F(PortApiTest, serdesApi) {
  auto id = createPort(100000, {42}, true);
  auto serdesId =
      createPortSerdes(id, {0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}, {8}, {9});
#if !defined(CHENAB_SAI_SDK)
  auto preemphasis = portApi->getAttribute(
      serdesId, SaiPortSerdesTraits::Attributes::Preemphasis{});
#endif
  auto txFirPre1 = portApi->getAttribute(
      serdesId, SaiPortSerdesTraits::Attributes::TxFirPre1{});
  auto txFirPre3 = portApi->getAttribute(
      serdesId, SaiPortSerdesTraits::Attributes::TxFirPre3{});
  auto txFirMain = portApi->getAttribute(
      serdesId, SaiPortSerdesTraits::Attributes::TxFirMain{});
  auto txFirPost1 = portApi->getAttribute(
      serdesId, SaiPortSerdesTraits::Attributes::TxFirPost1{});
  auto rxCtleCode = portApi->getAttribute(
      serdesId, SaiPortSerdesTraits::Attributes::RxCtleCode{});
  auto rxDspMode = portApi->getAttribute(
      serdesId, SaiPortSerdesTraits::Attributes::RxDspMode{});
  auto rxAfeTrim = portApi->getAttribute(
      serdesId, SaiPortSerdesTraits::Attributes::RxAfeTrim{});
  auto rxAcCouplingByPass = portApi->getAttribute(
      serdesId, SaiPortSerdesTraits::Attributes::RxAcCouplingByPass{});
  auto rxAfeAdaptiveEnable = portApi->getAttribute(
      serdesId, SaiPortSerdesTraits::Attributes::RxAfeAdaptiveEnable{});
#if !defined(CHENAB_SAI_SDK)
  EXPECT_EQ(preemphasis, std::vector<sai_uint32_t>{0});
#endif
  EXPECT_EQ(txFirPre1, std::vector<sai_uint32_t>{1});
  EXPECT_EQ(txFirMain, std::vector<sai_uint32_t>{2});
  EXPECT_EQ(txFirPost1, std::vector<sai_uint32_t>{3});
  EXPECT_EQ(rxCtleCode, std::vector<sai_int32_t>{4});
  EXPECT_EQ(rxDspMode, std::vector<sai_int32_t>{5});
  EXPECT_EQ(rxAfeTrim, std::vector<sai_int32_t>{6});
  EXPECT_EQ(rxAcCouplingByPass, std::vector<sai_int32_t>{7});
  EXPECT_EQ(rxAfeAdaptiveEnable, std::vector<sai_int32_t>{8});
  EXPECT_EQ(txFirPre3, std::vector<sai_uint32_t>{9});
}

TEST_F(PortApiTest, optionalSerdesListAttributesHaveDefaults) {
  EXPECT_TRUE(SaiPortSerdesTraits::Attributes::RxReach::defaultValue().empty());
  EXPECT_TRUE(
      SaiPortSerdesTraits::Attributes::TransmitPrecodingState::defaultValue()
          .empty());
  EXPECT_TRUE(
      SaiPortSerdesTraits::Attributes::ReceivePrecodingState::defaultValue()
          .empty());
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  EXPECT_TRUE(
      SaiPortSerdesTraits::Attributes::TxPrecoding::defaultValue().empty());
  EXPECT_TRUE(
      SaiPortSerdesTraits::Attributes::RxPrecoding::defaultValue().empty());
#endif
}

// The precoding vendor extensions are programmed after serdes create, the way
// SaiPortManager does it
TEST_F(PortApiTest, serdesPrecodingState) {
  auto id = createPort(100000, {42}, true);
  auto serdesId =
      createPortSerdes(id, {0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}, {8}, {9});
  const std::vector<sai_int32_t> enabled{1};

  portApi->setAttribute(
      serdesId,
      SaiPortSerdesTraits::Attributes::TransmitPrecodingState{enabled});
  portApi->setAttribute(
      serdesId,
      SaiPortSerdesTraits::Attributes::ReceivePrecodingState{enabled});

  EXPECT_EQ(
      portApi->getAttribute(
          serdesId,
          SaiPortSerdesTraits::Attributes::TransmitPrecodingState{
              std::vector<sai_int32_t>(1)}),
      enabled);
  EXPECT_EQ(
      portApi->getAttribute(
          serdesId,
          SaiPortSerdesTraits::Attributes::ReceivePrecodingState{
              std::vector<sai_int32_t>(1)}),
      enabled);
}

#if !defined(IS_OSS)
// interface modes used by fb, not available in OSS yet
TEST_F(PortApiTest, setInterfaceType) {
  auto id = createPort(100000, {42}, true);
  SaiPortTraits::Attributes::InterfaceType interface_type{
      SAI_PORT_INTERFACE_TYPE_CAUI};
  portApi->setAttribute(id, interface_type);
  EXPECT_EQ(
      portApi->getAttribute(id, SaiPortTraits::Attributes::InterfaceType{}),
      SAI_PORT_INTERFACE_TYPE_CAUI);
}
#endif

TEST_F(PortApiTest, getFabricAttachedSwitchId) {
  auto id = createPort(100000, {42}, true);
  auto swId = portApi->getAttribute(
      id, SaiPortTraits::Attributes::FabricAttachedSwitchId{});
  EXPECT_EQ(swId, 0);
}

TEST_F(PortApiTest, getFabricAttached) {
  auto id = createPort(100000, {42}, true);
  EXPECT_FALSE(
      portApi->getAttribute(id, SaiPortTraits::Attributes::FabricAttached{}));
}

TEST_F(PortApiTest, getFabricAttachedPortIndex) {
  auto id = createPort(100000, {42}, true);
  EXPECT_FALSE(portApi->getAttribute(
      id, SaiPortTraits::Attributes::FabricAttachedPortIndex{}));
}

TEST_F(PortApiTest, getFabricAttachedSwitchType) {
  auto id = createPort(100000, {42}, true);
  auto swType = portApi->getAttribute(
      id, SaiPortTraits::Attributes::FabricAttachedSwitchType{});
  EXPECT_EQ(swType, SAI_SWITCH_TYPE_VOQ);
}

TEST_F(PortApiTest, getLinkTrainingRxStatus) {
  auto id = createPort(100000, {42}, true);
  auto rxStatus = portApi->getAttribute(
      id, SaiPortTraits::Attributes::LinkTrainingRxStatus{});
  EXPECT_EQ(rxStatus, 0); // NOT_TRAINED default
}

TEST_F(PortApiTest, getFabricReachability) {
  const auto switchId = 3;
  auto id = createPort(100000, {42}, true);
  sai_fabric_port_reachability_t reachability;
  reachability.switch_id = switchId;
  auto reachabilityGot = portApi->getAttribute(
      id, SaiPortTraits::Attributes::FabricReachability{reachability});
  EXPECT_EQ(reachabilityGot.switch_id, switchId);
  EXPECT_TRUE(reachabilityGot.reachable);
}

TEST_F(PortApiTest, getQosEgressBufferProfileListPresized) {
  auto portId = createPort(100000, {0, 1, 2, 3}, true);
  std::vector<sai_object_id_t> tempProfileList;
  tempProfileList.resize(4);
  SaiPortTraits::Attributes::QosEgressBufferProfileList profileListAttr{
      tempProfileList};
  auto gotProfiles = portApi->getAttribute(portId, profileListAttr);
  EXPECT_EQ(gotProfiles.size(), 0);
}

TEST_F(PortApiTest, getQosEgressBufferProfileListUnsized) {
  auto portId = createPort(100000, {0, 1, 2, 3}, true);
  SaiPortTraits::Attributes::QosEgressBufferProfileList profileListAttr;
  auto gotProfiles = portApi->getAttribute(portId, profileListAttr);
  EXPECT_EQ(gotProfiles.size(), 0);
}

TEST_F(PortApiTest, setQosEgressBufferProfileList) {
  auto portId = createPort(100000, {0, 1, 2, 3}, true);

  // Set a list of buffer profile IDs
  std::vector<sai_object_id_t> profileIds{10, 20, 30};
  SaiPortTraits::Attributes::QosEgressBufferProfileList profileListAttr{
      profileIds};
  portApi->setAttribute(portId, profileListAttr);

  // Get the list back and verify
  SaiPortTraits::Attributes::QosEgressBufferProfileList getProfileListAttr;
  auto gotProfiles = portApi->getAttribute(portId, getProfileListAttr);
  EXPECT_EQ(gotProfiles.size(), 3);
  EXPECT_EQ(gotProfiles, profileIds);

  // Clear the list
  std::vector<sai_object_id_t> emptyList;
  SaiPortTraits::Attributes::QosEgressBufferProfileList emptyListAttr{
      emptyList};
  portApi->setAttribute(portId, emptyListAttr);

  // Verify it's empty
  auto gotEmptyProfiles = portApi->getAttribute(portId, getProfileListAttr);
  EXPECT_EQ(gotEmptyProfiles.size(), 0);
}

TEST_F(PortApiTest, getQosIngressBufferProfileListPresized) {
  auto portId = createPort(100000, {0, 1, 2, 3}, true);
  std::vector<sai_object_id_t> tempProfileList;
  tempProfileList.resize(4);
  SaiPortTraits::Attributes::QosIngressBufferProfileList profileListAttr{
      tempProfileList};
  auto gotProfiles = portApi->getAttribute(portId, profileListAttr);
  EXPECT_EQ(gotProfiles.size(), 0);
}

TEST_F(PortApiTest, getQosIngressBufferProfileListUnsized) {
  auto portId = createPort(100000, {0, 1, 2, 3}, true);
  SaiPortTraits::Attributes::QosIngressBufferProfileList profileListAttr;
  auto gotProfiles = portApi->getAttribute(portId, profileListAttr);
  EXPECT_EQ(gotProfiles.size(), 0);
}

TEST_F(PortApiTest, setQosIngressBufferProfileList) {
  auto portId = createPort(100000, {0, 1, 2, 3}, true);

  // Set a list of buffer profile IDs
  std::vector<sai_object_id_t> profileIds{40, 50, 60};
  SaiPortTraits::Attributes::QosIngressBufferProfileList profileListAttr{
      profileIds};
  portApi->setAttribute(portId, profileListAttr);

  // Get the list back and verify
  SaiPortTraits::Attributes::QosIngressBufferProfileList getProfileListAttr;
  auto gotProfiles = portApi->getAttribute(portId, getProfileListAttr);
  EXPECT_EQ(gotProfiles.size(), 3);
  EXPECT_EQ(gotProfiles, profileIds);

  // Clear the list
  std::vector<sai_object_id_t> emptyList;
  SaiPortTraits::Attributes::QosIngressBufferProfileList emptyListAttr{
      emptyList};
  portApi->setAttribute(portId, emptyListAttr);

  // Verify it's empty
  auto gotEmptyProfiles = portApi->getAttribute(portId, getProfileListAttr);
  EXPECT_EQ(gotEmptyProfiles.size(), 0);
}

TEST_F(PortApiTest, getPortErrStatusToctouRetrySucceeds) {
  auto portId = createPort(100000, {0, 1}, true);
  auto& port = fs->portManager.get(portId);
  port.portErrStatusList = {SAI_PORT_ERR_STATUS_DATA_UNIT_CRC_ERROR};

  // Simulate TOCTOU: grow error list on the first 2 getAttribute calls,
  // then stop growing so the retry eventually succeeds.
  int callCount = 0;
  port.onGetAttribute = [&]() {
    if (callCount < 2) {
      port.portErrStatusList.push_back(SAI_PORT_ERR_STATUS_SIGNAL_LOCAL_ERROR);
    }
    ++callCount;
  };

  SaiPortTraits::Attributes::PortErrStatus errStatusAttr;
  auto gotStatus = portApi->getAttribute(portId, errStatusAttr);
  EXPECT_EQ(gotStatus, port.portErrStatusList);
}

TEST_F(PortApiTest, getPortErrStatusToctouExhaustsRetries) {
  auto portId = createPort(100000, {0, 1}, true);
  auto& port = fs->portManager.get(portId);
  port.portErrStatusList = {SAI_PORT_ERR_STATUS_DATA_UNIT_CRC_ERROR};

  // Simulate TOCTOU: grow error list on every getAttribute call so
  // the buffer never catches up and retries are exhausted.
  port.onGetAttribute = [&]() {
    port.portErrStatusList.push_back(SAI_PORT_ERR_STATUS_SIGNAL_LOCAL_ERROR);
  };

  // EXPECT_DEATH handles both UBSan-enabled builds (abort on invalid
  // enum from reading past the buffer) and non-UBSan builds (uncaught
  // SaiApiError → std::terminate).
  SaiPortTraits::Attributes::PortErrStatus errStatusAttr;
  EXPECT_DEATH(portApi->getAttribute(portId, errStatusAttr), ".*");
}

#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
namespace {
// A LLR profile with a distinct value per attribute so a get() reading back the
// wrong field is caught.
PortLlrProfileSaiId createLlrProfile(PortApi* portApi) {
  SaiPortLlrProfileTraits::CreateAttributes a{
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
  return portApi->create<SaiPortLlrProfileTraits>(a, 0);
}
} // namespace

TEST_F(PortApiTest, createLlrProfile) {
  auto id = createLlrProfile(portApi.get());

  SaiPortLlrProfileTraits::Attributes::OutstandingFramesMax framesBlank;
  SaiPortLlrProfileTraits::Attributes::OutstandingBytesMax bytesBlank;
  SaiPortLlrProfileTraits::Attributes::ReplayTimerMax replayTimerBlank;
  SaiPortLlrProfileTraits::Attributes::ReplayCountMax replayCountBlank;
  SaiPortLlrProfileTraits::Attributes::PcsLostTimeout pcsLostBlank;
  SaiPortLlrProfileTraits::Attributes::DataAgeTimeout dataAgeBlank;
  SaiPortLlrProfileTraits::Attributes::InitLlrFrameAction initActionBlank;
  SaiPortLlrProfileTraits::Attributes::FlushLlrFrameAction flushActionBlank;
  SaiPortLlrProfileTraits::Attributes::ReInitOnFlush reInitBlank;
  SaiPortLlrProfileTraits::Attributes::CtlosTargetSpacing ctlosBlank;

  EXPECT_EQ(portApi->getAttribute(id, framesBlank), 32);
  EXPECT_EQ(portApi->getAttribute(id, bytesBlank), 4096);
  EXPECT_EQ(portApi->getAttribute(id, replayTimerBlank), 5000);
  EXPECT_EQ(portApi->getAttribute(id, replayCountBlank), 7);
  EXPECT_EQ(portApi->getAttribute(id, pcsLostBlank), 1000);
  EXPECT_EQ(portApi->getAttribute(id, dataAgeBlank), 200000);
  EXPECT_EQ(
      portApi->getAttribute(id, initActionBlank),
      SAI_LLR_FRAME_ACTION_BEST_EFFORT);
  EXPECT_EQ(
      portApi->getAttribute(id, flushActionBlank), SAI_LLR_FRAME_ACTION_BLOCK);
  EXPECT_EQ(portApi->getAttribute(id, reInitBlank), true);
  EXPECT_EQ(portApi->getAttribute(id, ctlosBlank), 2048);

  // Cross-check the fake store's view matches what the api returned.
  const auto& profile = fs->portLlrProfileManager.get(id);
  EXPECT_EQ(profile.outstandingFramesMax, 32);
  EXPECT_EQ(profile.outstandingBytesMax, 4096);
  EXPECT_EQ(profile.replayTimerMax, 5000);
  EXPECT_EQ(profile.replayCountMax, 7);
  EXPECT_EQ(profile.pcsLostTimeout, 1000);
  EXPECT_EQ(profile.dataAgeTimeout, 200000);
  EXPECT_EQ(profile.initLlrFrameAction, SAI_LLR_FRAME_ACTION_BEST_EFFORT);
  EXPECT_EQ(profile.flushLlrFrameAction, SAI_LLR_FRAME_ACTION_BLOCK);
  EXPECT_EQ(profile.reInitOnFlush, true);
  EXPECT_EQ(profile.ctlosTargetSpacing, 2048);
}

TEST_F(PortApiTest, setLlrProfileAttributes) {
  auto id = createLlrProfile(portApi.get());

  SaiPortLlrProfileTraits::Attributes::OutstandingFramesMax frames{64};
  SaiPortLlrProfileTraits::Attributes::ReplayCountMax replayCount{
      sai_uint8_t(3)};
  SaiPortLlrProfileTraits::Attributes::ReInitOnFlush reInit{false};
  SaiPortLlrProfileTraits::Attributes::InitLlrFrameAction initAction{
      SAI_LLR_FRAME_ACTION_DISCARD};
  portApi->setAttribute(id, frames);
  portApi->setAttribute(id, replayCount);
  portApi->setAttribute(id, reInit);
  portApi->setAttribute(id, initAction);

  EXPECT_EQ(portApi->getAttribute(id, frames), 64);
  EXPECT_EQ(portApi->getAttribute(id, replayCount), 3);
  EXPECT_EQ(portApi->getAttribute(id, reInit), false);
  EXPECT_EQ(
      portApi->getAttribute(id, initAction), SAI_LLR_FRAME_ACTION_DISCARD);

  // Untouched attributes keep their created values.
  SaiPortLlrProfileTraits::Attributes::OutstandingBytesMax bytesBlank;
  EXPECT_EQ(portApi->getAttribute(id, bytesBlank), 4096);
}

TEST_F(PortApiTest, removeLlrProfile) {
  auto id = createLlrProfile(portApi.get());
  EXPECT_EQ(fs->portLlrProfileManager.map().size(), 1);
  portApi->remove(id);
  EXPECT_EQ(fs->portLlrProfileManager.map().size(), 0);
}

TEST_F(PortApiTest, portLlrAttributes) {
  // Admin disabled, since a profile cannot be attached to an enabled port.
  auto portId = createPort(100000, {42}, false);
  auto profileId = createLlrProfile(portApi.get());

  // Defaults on a freshly created port: LLR disabled, no profile, status OFF.
  SaiPortTraits::Attributes::LlrModeLocal modeLocalBlank;
  SaiPortTraits::Attributes::LlrModeRemote modeRemoteBlank;
  SaiPortTraits::Attributes::LlrProfile profileBlank;
  SaiPortTraits::Attributes::LlrTxStatus txStatusBlank;
  SaiPortTraits::Attributes::LlrRxStatus rxStatusBlank;
  EXPECT_EQ(portApi->getAttribute(portId, modeLocalBlank), false);
  EXPECT_EQ(portApi->getAttribute(portId, modeRemoteBlank), false);
  EXPECT_EQ(portApi->getAttribute(portId, profileBlank), SAI_NULL_OBJECT_ID);
  EXPECT_EQ(
      portApi->getAttribute(portId, txStatusBlank), SAI_PORT_LLR_TX_STATUS_OFF);
  EXPECT_EQ(
      portApi->getAttribute(portId, rxStatusBlank), SAI_PORT_LLR_RX_STATUS_OFF);

  // Bind the profile and enable LLR, then read back.
  SaiPortTraits::Attributes::LlrProfile profileAttr{
      static_cast<sai_object_id_t>(profileId)};
  SaiPortTraits::Attributes::LlrModeLocal modeLocal{true};
  SaiPortTraits::Attributes::LlrModeRemote modeRemote{true};
  portApi->setAttribute(portId, profileAttr);
  portApi->setAttribute(portId, modeLocal);
  portApi->setAttribute(portId, modeRemote);

  EXPECT_EQ(
      portApi->getAttribute(portId, profileBlank),
      static_cast<sai_object_id_t>(profileId));
  EXPECT_EQ(portApi->getAttribute(portId, modeLocalBlank), true);
  EXPECT_EQ(portApi->getAttribute(portId, modeRemoteBlank), true);

  const auto& port = fs->portManager.get(portId);
  EXPECT_EQ(port.llrModeLocal, true);
  EXPECT_EQ(port.llrModeRemote, true);
  EXPECT_EQ(port.llrProfile, static_cast<sai_object_id_t>(profileId));
}

// Clearing an LLR mode, and attaching or detaching a profile, are refused while
// the port is administratively enabled (Broadcom CS00012478409). Setting a mode
// is not: that is how the one-shot LLR_MODE_REMOTE trigger is armed on link up.
TEST_F(PortApiTest, portLlrAttributesOnEnabledPort) {
  auto portId = createPort(100000, {42}, false);
  auto profileId = createLlrProfile(portApi.get());
  portApi->setAttribute(
      portId,
      SaiPortTraits::Attributes::LlrProfile{
          static_cast<sai_object_id_t>(profileId)});
  portApi->setAttribute(portId, SaiPortTraits::Attributes::LlrModeLocal{true});
  portApi->setAttribute(portId, SaiPortTraits::Attributes::AdminState{true});

  EXPECT_THROW(
      portApi->setAttribute(
          portId, SaiPortTraits::Attributes::LlrModeLocal{false}),
      SaiApiError);
  auto otherProfileId = createLlrProfile(portApi.get());
  EXPECT_THROW(
      portApi->setAttribute(
          portId,
          SaiPortTraits::Attributes::LlrProfile{
              static_cast<sai_object_id_t>(otherProfileId)}),
      SaiApiError);

  // Re-asserting a mode on an enabled port is allowed.
  portApi->setAttribute(portId, SaiPortTraits::Attributes::LlrModeRemote{true});
  SaiPortTraits::Attributes::LlrModeRemote modeRemoteBlank;
  EXPECT_EQ(portApi->getAttribute(portId, modeRemoteBlank), true);
}
#endif

TEST_F(PortApiTest, setGetIsolationGroup) {
  auto portIds = createFivePorts();
  constexpr sai_object_id_t kIsolationGroupId{42};
  using IsolationGroup = SaiPortTraits::Attributes::IsolationGroup;

  // getAttribute fills in the attribute it is handed, so every read uses a
  // fresh one -- reusing the attribute being set would clobber its value.
  EXPECT_EQ(
      portApi->getAttribute(portIds[0], IsolationGroup{}), SAI_NULL_OBJECT_ID);

  portApi->setAttribute(portIds[0], IsolationGroup{kIsolationGroupId});
  EXPECT_EQ(
      portApi->getAttribute(portIds[0], IsolationGroup{}), kIsolationGroupId);
  // Binding one port must not bind any other.
  EXPECT_EQ(
      portApi->getAttribute(portIds[1], IsolationGroup{}), SAI_NULL_OBJECT_ID);

  // Unbinding is an explicit write of the null oid.
  portApi->setAttribute(portIds[0], IsolationGroup{SAI_NULL_OBJECT_ID});
  EXPECT_EQ(
      portApi->getAttribute(portIds[0], IsolationGroup{}), SAI_NULL_OBJECT_ID);
}
