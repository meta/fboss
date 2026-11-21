/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/hw/HwPortFb303Stats.h"
#include "fboss/agent/hw/StatsConstants.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiAclTableGroupManager.h"
#include "fboss/agent/hw/sai/switch/SaiAclTableManager.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/tests/ManagerTestBase.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"
#include "fboss/agent/platforms/sai/SaiPlatformPort.h"
#include "fboss/agent/state/LlrConfig.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/types.h"

#include <fb303/ServiceData.h>

#include <string>

#include <gtest/gtest.h>

using namespace facebook::fboss;

namespace facebook::fboss {
class PortManagerTest : public ManagerTestBase {
 public:
  void SetUp() override {
    setupStage = SetupStage::BLANK;
    ManagerTestBase::SetUp();
    p0 = testInterfaces[0].remoteHosts[0].port;
    p1 = testInterfaces[1].remoteHosts[0].port;
  }
  // TODO: make it properly handle different lanes/speeds for different
  // port ids...
  void checkPort(
      const PortID& swId,
      const SaiPortHandle* handle,
      bool enabled,
      sai_uint32_t mtu = 9412,
      bool ptpTcEnable = false,
      bool isDrained = false) {
    // Check SaiPortApi perspective
    auto& portApi = saiApiTable->portApi();
    auto saiId = handle->port->adapterKey();
    SaiPortTraits::Attributes::AdminState adminStateAttribute;
    SaiPortTraits::Attributes::HwLaneList hwLaneListAttribute;
    SaiPortTraits::Attributes::Speed speedAttribute;
    SaiPortTraits::Attributes::FecMode fecMode;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    SaiPortTraits::Attributes::PortLoopbackMode lbMode;
#else
    SaiPortTraits::Attributes::InternalLoopbackMode ilbMode;
#endif
    SaiPortTraits::Attributes::Mtu mtuAttribute;
#if SAI_API_VERSION >= SAI_VERSION(1, 11, 0)
    SaiPortTraits::Attributes::FabricIsolate fabricIsolateAttribute;
#endif
    auto gotAdminState = portApi.getAttribute(saiId, adminStateAttribute);
    EXPECT_EQ(enabled, gotAdminState);
    auto gotLanes = portApi.getAttribute(saiId, hwLaneListAttribute);
    EXPECT_EQ(1, gotLanes.size());
    EXPECT_EQ(swId, uint16_t(gotLanes[0]));
    auto gotSpeed = portApi.getAttribute(saiId, speedAttribute);
    EXPECT_EQ(25000, gotSpeed);
    auto gotFecMode = portApi.getAttribute(saiId, fecMode);
    EXPECT_EQ(static_cast<int32_t>(SAI_PORT_FEC_MODE_NONE), gotFecMode);
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    auto gotlbMode = portApi.getAttribute(saiId, lbMode);
    EXPECT_EQ(static_cast<int32_t>(SAI_PORT_LOOPBACK_MODE_NONE), gotlbMode);
#else
    auto gotIlbMode = portApi.getAttribute(saiId, ilbMode);
    EXPECT_EQ(
        static_cast<int32_t>(SAI_PORT_INTERNAL_LOOPBACK_MODE_NONE), gotIlbMode);
#endif
    auto gotMtu = portApi.getAttribute(saiId, mtuAttribute);
    EXPECT_EQ(mtu, gotMtu);
    ASSERT_NE(handle->serdes.get(), nullptr);
    checkPortSerdes(handle->serdes.get(), saiId);

    // ptp mode
    SaiPortTraits::Attributes::PtpMode ptpMode;
    auto gotPtpMode = portApi.getAttribute(saiId, ptpMode);
    EXPECT_NE(gotPtpMode, SAI_PORT_PTP_MODE_TWO_STEP_TIMESTAMP);
    EXPECT_EQ(
        ptpTcEnable, (gotPtpMode == SAI_PORT_PTP_MODE_SINGLE_STEP_TIMESTAMP));
#if SAI_API_VERSION >= SAI_VERSION(1, 11, 0)
    auto gotDrainState = portApi.getAttribute(saiId, fabricIsolateAttribute);
    EXPECT_EQ(gotDrainState, isDrained);
#endif
  }

  void checkPortSerdes(SaiPortSerdes* serdes, PortSaiId portId) {
    auto& portApi = saiApiTable->portApi();
    EXPECT_EQ(
        portApi.getAttribute(
            serdes->adapterKey(), SaiPortSerdesTraits::Attributes::PortId{}),
        portId);
  }

  /**
   * DO NOT use this routine for adding ports.
   * This is only used to verify port consolidation logic in sai port
   * manager. It makes certain assumptions about the lane numbers and
   * also adds the port under the hood bypassing the port manager.
   */
  PortSaiId addPort(const PortID& swId, cfg::PortSpeed portSpeed) {
    auto& portApi = saiApiTable->portApi();
    std::vector<uint32_t> ls;
    if (portSpeed == cfg::PortSpeed::TWENTYFIVEG) {
      ls.push_back(swId);
    } else {
      ls.push_back(swId);
      ls.push_back(swId + 1);
      ls.push_back(swId + 2);
      ls.push_back(swId + 3);
    }
    SaiPortTraits::Attributes::AdminState adminState{true};
    SaiPortTraits::Attributes::HwLaneList lanes(ls);
    SaiPortTraits::Attributes::Speed speed{
        static_cast<unsigned int>(portSpeed)};
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
        std::nullopt, // CBFC receiver credit limit
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
        std::nullopt, // Inter Frame Gap
#endif
        std::nullopt, // Link Training Enable,
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
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 1)
        std::nullopt, // IntDelete
        std::nullopt, // IntMetadata
#endif
    };
    return portApi.create<SaiPortTraits>(a, 0);
  }

  void checkSubsumedPorts(
      TestPort port,
      cfg::PortSpeed speed,
      std::vector<PortID> expectedPorts) {
    std::shared_ptr<Port> swPort = makePort(port, speed);
    saiManagerTable->portManager().addPort(swPort);
    SaiPlatformPort* platformPort = saiPlatform->getPort(swPort->getID());
    EXPECT_TRUE(platformPort);
    auto subsumedPorts = platformPort->getSubsumedPorts(swPort->getProfileID());
    EXPECT_EQ(subsumedPorts, expectedPorts);
    saiManagerTable->portManager().removePort(swPort);
  }

  TestPort p0;
  TestPort p1;
};

enum class ExpectExport { NO_EXPORT, EXPORT };

TEST_F(PortManagerTest, addPort) {
  std::shared_ptr<Port> swPort = makePort(p0);
  saiManagerTable->portManager().addPort(swPort);
  auto handle = saiManagerTable->portManager().getPortHandle(swPort->getID());
  checkPort(PortID(0), handle, true);
}

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
TEST_F(PortManagerTest, getExtOperStatusLatch) {
  auto swPort = makePort(p0);
  saiManagerTable->portManager().addPort(swPort);

  const auto* handle =
      saiManagerTable->portManager().getPortHandle(swPort->getID());
  ASSERT_NE(handle, nullptr);
  auto& fakePort =
      FakeSai::getInstance()->portManager.get(handle->port->adapterKey());
  fakePort.portExtOperStatusLatch.current_status = true;
  fakePort.portExtOperStatusLatch.changed = true;

  const auto status = saiManagerTable->portManager().getExtOperStatusLatch(
      handle->port->adapterKey());
  ASSERT_TRUE(status.has_value());
  EXPECT_TRUE(status->current_status);
  EXPECT_TRUE(status->changed);

  const auto* portStats =
      saiManagerTable->portManager().getLastPortStat(swPort->getID());
  ASSERT_NE(portStats, nullptr);
  EXPECT_FALSE(
      fb303::fbData
          ->getCounterIfExists(
              HwPortFb303Stats::statName(
                  "oper_status_latch.current_status", portStats->portName()))
          .has_value());
  EXPECT_FALSE(
      fb303::fbData
          ->getCounterIfExists(
              HwPortFb303Stats::statName(
                  "oper_status_latch.changed", portStats->portName()))
          .has_value());
}

TEST_F(PortManagerTest, snapshotExtOperStatusLatchCapturesPreWarmBootState) {
  auto swPort = makePort(p0);
  auto& portManager = saiManagerTable->portManager();
  portManager.addPort(swPort);

  const auto* handle = portManager.getPortHandle(swPort->getID());
  ASSERT_NE(handle, nullptr);
  auto& fakePort =
      FakeSai::getInstance()->portManager.get(handle->port->adapterKey());
  fakePort.portExtOperStatusLatch = {true, true};

  const auto baseline = portManager.snapshotExtOperStatusLatch();
  ASSERT_EQ(baseline.size(), 1);
  EXPECT_TRUE(baseline.at(swPort->getID()).current_status);
  EXPECT_TRUE(baseline.at(swPort->getID()).changed);
  EXPECT_FALSE(fakePort.portExtOperStatusLatch.changed);
}

TEST_F(
    PortManagerTest,
    warmBootFlapCounterNotCreatedWhenPortLatchStatusUnsupported) {
  auto upPort = makePort(p0);
  auto downPort = makePort(p1);
  auto& portManager = saiManagerTable->portManager();
  portManager.addPort(upPort);
  portManager.addPort(downPort);

  auto* upHandle = portManager.getPortHandle(upPort->getID());
  auto* downHandle = portManager.getPortHandle(downPort->getID());
  ASSERT_NE(upHandle, nullptr);
  ASSERT_NE(downHandle, nullptr);
  auto& fakeSaiPortManager = FakeSai::getInstance()->portManager;
  auto& fakeUpPort = fakeSaiPortManager.get(upHandle->port->adapterKey());
  auto& fakeDownPort = fakeSaiPortManager.get(downHandle->port->adapterKey());
  fakeUpPort.portExtOperStatusLatch = {false, true};
  fakeDownPort.portExtOperStatusLatch = {true, true};

  portManager.updateExtOperStatusLatchAfterWarmBoot(
      {{upPort->getID(), {true, false}}, {downPort->getID(), {false, false}}});

  const auto* upStats = portManager.getLastPortStat(upPort->getID());
  const auto* downStats = portManager.getLastPortStat(downPort->getID());
  ASSERT_NE(upStats, nullptr);
  ASSERT_NE(downStats, nullptr);
  EXPECT_FALSE(
      fb303::fbData
          ->getCounterIfExists(
              HwPortFb303Stats::statName(
                  kLinkDownDuringWarmBoot(), upStats->portName()))
          .has_value());
  EXPECT_FALSE(
      fb303::fbData
          ->getCounterIfExists(
              HwPortFb303Stats::statName(
                  kLinkDownDuringWarmBoot(), downStats->portName()))
          .has_value());
  EXPECT_FALSE(fakeUpPort.portExtOperStatusLatch.changed);
  EXPECT_FALSE(fakeDownPort.portExtOperStatusLatch.changed);

  portManager.updateExtOperStatusLatchAfterWarmBoot({});
  EXPECT_FALSE(
      fb303::fbData
          ->getCounterIfExists(
              HwPortFb303Stats::statName(
                  kLinkDownDuringWarmBoot(), upStats->portName()))
          .has_value());
  EXPECT_FALSE(
      fb303::fbData
          ->getCounterIfExists(
              HwPortFb303Stats::statName(
                  kLinkDownDuringWarmBoot(), downStats->portName()))
          .has_value());
}

TEST_F(PortManagerTest, warmBootFlapCounterNotCreatedOnUnsupportedAsic) {
  auto swPort = makePort(p0);
  auto& portManager = saiManagerTable->portManager();
  portManager.addPort(swPort);

  auto* handle = portManager.getPortHandle(swPort->getID());
  ASSERT_NE(handle, nullptr);
  auto& fakePort =
      FakeSai::getInstance()->portManager.get(handle->port->adapterKey());
  fakePort.portExtOperStatusLatch = {true, true};

  portManager.updateExtOperStatusLatchAfterWarmBoot(
      {{swPort->getID(), {true, false}}});

  const auto* portStats = portManager.getLastPortStat(swPort->getID());
  ASSERT_NE(portStats, nullptr);
  EXPECT_FALSE(
      fb303::fbData
          ->getCounterIfExists(
              HwPortFb303Stats::statName(
                  kLinkDownDuringWarmBoot(), portStats->portName()))
          .has_value());
}
#endif

TEST_F(PortManagerTest, programUserMetaData) {
  auto swPort = makePort(p0);
  swPort->setUserMetaData(cfg::AclLookupClassPort::CLASS_PORT_RESTRICTED);
  saiManagerTable->portManager().addPort(swPort);

  auto* handle = saiManagerTable->portManager().getPortHandle(swPort->getID());
  ASSERT_NE(handle, nullptr);
  auto readMetaData = [&] {
    return saiApiTable->portApi().getAttribute(
        handle->port->adapterKey(), SaiPortTraits::Attributes::Metadata{});
  };
  EXPECT_EQ(
      readMetaData(),
      static_cast<uint32_t>(cfg::AclLookupClassPort::CLASS_PORT_RESTRICTED));

  auto changedPort = swPort->clone();
  changedPort->setUserMetaData(cfg::AclLookupClassPort::CLASS_PORT_BLOCKED);
  saiManagerTable->portManager().changePort(swPort, changedPort);
  EXPECT_EQ(
      readMetaData(),
      static_cast<uint32_t>(cfg::AclLookupClassPort::CLASS_PORT_BLOCKED));

  auto clearedPort = changedPort->clone();
  clearedPort->setUserMetaData(std::nullopt);
  saiManagerTable->portManager().changePort(changedPort, clearedPort);
  EXPECT_EQ(readMetaData(), 0);
}

// Dropping user metadata from switch state resolves to an explicit 0 for a
// port that is already tagged, and to nothing for one that is not.
TEST_F(PortManagerTest, clearUserMetaDataFromSwPort) {
  auto& portManager = saiManagerTable->portManager();
  auto readMetaData = [&](const std::shared_ptr<Port>& swPort) {
    return std::get<std::optional<SaiPortTraits::Attributes::Metadata>>(
        portManager.attributesFromSwPort(swPort));
  };

  auto taggedPort = makePort(p0);
  taggedPort->setUserMetaData(cfg::AclLookupClassPort::CLASS_PORT_RESTRICTED);
  portManager.addPort(taggedPort);
  auto clearedPort = taggedPort->clone();
  clearedPort->setUserMetaData(std::nullopt);
  EXPECT_EQ(readMetaData(clearedPort), SaiPortTraits::Attributes::Metadata{0});

  auto untaggedPort = makePort(p1);
  portManager.addPort(untaggedPort);
  EXPECT_FALSE(readMetaData(untaggedPort).has_value());
}

TEST_F(PortManagerTest, setIngressAcl) {
  const std::string ingressAclTableName{"PortIngressAclTable"};
  const std::string secondIngressAclTableName{"SecondPortIngressAclTable"};
  auto aclTableGroup = std::make_shared<AclTableGroup>(cfg::AclStage::INGRESS);
  aclTableGroup->setName("PortIngressAclGroup");
  aclTableGroup->setBindPoint(cfg::AclTableGroupBindPoint::PORT);
  saiManagerTable->aclTableGroupManager().addAclTableGroup(aclTableGroup);

  auto addAclTable = [&](const std::string& name, int priority) {
    auto aclTable = std::make_shared<AclTable>(priority, name);
    return saiManagerTable->aclTableManager().addAclTable(
        aclTable,
        cfg::AclStage::INGRESS,
        nullptr /*state*/,
        cfg::AclTableGroupBindPoint::PORT);
  };
  const auto aclTableId = addAclTable(ingressAclTableName, 0);
  const auto secondAclTableId = addAclTable(secondIngressAclTableName, 1);

  auto swPort = makePort(p0);
  saiManagerTable->portManager().addPort(swPort);
  saiManagerTable->portManager().setIngressAcl(swPort);

  auto* handle = saiManagerTable->portManager().getPortHandle(swPort->getID());
  ASSERT_NE(handle, nullptr);
  EXPECT_FALSE(
      std::get<std::optional<SaiPortTraits::Attributes::IngressAcl>>(
          handle->port->attributes())
          .has_value());

  swPort->setIngressAclTableName(ingressAclTableName);
  saiManagerTable->portManager().setIngressAcl(swPort);
  EXPECT_EQ(
      saiApiTable->portApi().getAttribute(
          handle->port->adapterKey(), SaiPortTraits::Attributes::IngressAcl{}),
      aclTableId);

  auto recreatedSwPort = makePort(p0, cfg::PortSpeed::FIFTYG);
  recreatedSwPort->setIngressAclTableName(ingressAclTableName);
  saiManagerTable->portManager().changePort(swPort, recreatedSwPort);
  saiManagerTable->portManager().changeIngressAcl(swPort, recreatedSwPort);
  handle =
      saiManagerTable->portManager().getPortHandle(recreatedSwPort->getID());
  ASSERT_NE(handle, nullptr);
  EXPECT_EQ(
      saiApiTable->portApi().getAttribute(
          handle->port->adapterKey(), SaiPortTraits::Attributes::IngressAcl{}),
      aclTableId);

  auto newSwPort = recreatedSwPort->clone();
  newSwPort->setIngressAclTableName(secondIngressAclTableName);
  saiManagerTable->portManager().changeIngressAcl(recreatedSwPort, newSwPort);
  EXPECT_EQ(
      saiApiTable->portApi().getAttribute(
          handle->port->adapterKey(), SaiPortTraits::Attributes::IngressAcl{}),
      secondAclTableId);

  auto portWithoutAcl = newSwPort->clone();
  portWithoutAcl->setIngressAclTableName(std::nullopt);
  saiManagerTable->portManager().setIngressAcl(portWithoutAcl);
  EXPECT_EQ(
      saiApiTable->portApi().getAttribute(
          handle->port->adapterKey(), SaiPortTraits::Attributes::IngressAcl{}),
      SAI_NULL_OBJECT_ID);
}

// SaiSwitch processes the port delta before the ACL delta, so an unbind always
// runs changePort() before changeIngressAcl(). The binding has to leave
// hardware, not just the store.
TEST_F(PortManagerTest, unbindIngressAclAfterChangePort) {
  const std::string ingressAclTableName{"PortIngressAclTable"};
  auto aclTableGroup = std::make_shared<AclTableGroup>(cfg::AclStage::INGRESS);
  aclTableGroup->setName("PortIngressAclGroup");
  aclTableGroup->setBindPoint(cfg::AclTableGroupBindPoint::PORT);
  saiManagerTable->aclTableGroupManager().addAclTableGroup(aclTableGroup);
  const auto aclTableId = saiManagerTable->aclTableManager().addAclTable(
      std::make_shared<AclTable>(0, ingressAclTableName),
      cfg::AclStage::INGRESS,
      nullptr /*state*/,
      cfg::AclTableGroupBindPoint::PORT);

  auto boundPort = makePort(p0);
  boundPort->setIngressAclTableName(ingressAclTableName);
  saiManagerTable->portManager().addPort(boundPort);
  saiManagerTable->portManager().setIngressAcl(boundPort);
  const auto* handle =
      saiManagerTable->portManager().getPortHandle(boundPort->getID());
  ASSERT_NE(handle, nullptr);
  ASSERT_EQ(
      saiApiTable->portApi().getAttribute(
          handle->port->adapterKey(), SaiPortTraits::Attributes::IngressAcl{}),
      aclTableId);

  auto unboundPort = boundPort->clone();
  unboundPort->setIngressAclTableName(std::nullopt);

  // Dropping the table name resolves to an explicit unbind, so changePort()
  // alone must clear the binding in hardware.
  saiManagerTable->portManager().changePort(boundPort, unboundPort);
  EXPECT_EQ(
      saiApiTable->portApi().getAttribute(
          handle->port->adapterKey(), SaiPortTraits::Attributes::IngressAcl{}),
      SAI_NULL_OBJECT_ID);

  // And the ACL phase that follows it leaves the port unbound.
  saiManagerTable->portManager().changeIngressAcl(boundPort, unboundPort);
  EXPECT_EQ(
      saiApiTable->portApi().getAttribute(
          handle->port->adapterKey(), SaiPortTraits::Attributes::IngressAcl{}),
      SAI_NULL_OBJECT_ID);
}

TEST_F(PortManagerTest, resetIngressAcl) {
  const std::string ingressAclTableName{"PortIngressAclTable"};
  auto aclTableGroup = std::make_shared<AclTableGroup>(cfg::AclStage::INGRESS);
  aclTableGroup->setName("PortIngressAclGroup");
  aclTableGroup->setBindPoint(cfg::AclTableGroupBindPoint::PORT);
  saiManagerTable->aclTableGroupManager().addAclTableGroup(aclTableGroup);
  saiManagerTable->aclTableManager().addAclTable(
      std::make_shared<AclTable>(0, ingressAclTableName),
      cfg::AclStage::INGRESS,
      nullptr /*state*/,
      cfg::AclTableGroupBindPoint::PORT);

  auto boundPort = makePort(p0);
  boundPort->setIngressAclTableName(ingressAclTableName);
  saiManagerTable->portManager().addPort(boundPort);
  saiManagerTable->portManager().setIngressAcl(boundPort);
  auto unboundPort = makePort(p1);
  saiManagerTable->portManager().addPort(unboundPort);

  saiManagerTable->portManager().resetIngressAcl();

  for (const auto& portId : {boundPort->getID(), unboundPort->getID()}) {
    auto* handle = saiManagerTable->portManager().getPortHandle(portId);
    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(
        saiApiTable->portApi().getAttribute(
            handle->port->adapterKey(),
            SaiPortTraits::Attributes::IngressAcl{}),
        SAI_NULL_OBJECT_ID);
  }
}

TEST_F(PortManagerTest, addTwoPorts) {
  std::shared_ptr<Port> swPort = makePort(p0);
  saiManagerTable->portManager().addPort(swPort);
  std::shared_ptr<Port> port2 = makePort(p1);
  saiManagerTable->portManager().addPort(port2);
  auto handle = saiManagerTable->portManager().getPortHandle(port2->getID());
  checkPort(PortID(10), handle, true);
}

TEST_F(PortManagerTest, triggerCableLengthMeasurement) {
  auto swPort = makePort(p0);
  saiManagerTable->portManager().addPort(swPort);
  auto port2 = makePort(p1);
  saiManagerTable->portManager().addPort(port2);

  auto& portApi = saiApiTable->portApi();
  auto getMeasureAttr = [&](const PortID& port) {
    auto* handle = saiManagerTable->portManager().getPortHandle(port);
    CHECK(handle);
    return portApi.getAttribute(
        handle->port->adapterKey(),
        SaiPortTraits::Attributes::CablePropagationDelayMeasure{});
  };

  const std::vector<PortID> ports{swPort->getID(), port2->getID()};
  EXPECT_EQ(getMeasureAttr(swPort->getID()), false);
  EXPECT_EQ(getMeasureAttr(port2->getID()), false);
  auto* portStat = const_cast<HwPortFb303Stats*>(
      saiManagerTable->portManager().getLastPortStat(swPort->getID()));
  ASSERT_NE(portStat, nullptr);
  auto cachedStats = portStat->portStats();
  cachedStats.cableLengthMeters() = 100;
  cachedStats.cableDelayNsec() = 500;
  portStat->updateStats(cachedStats, std::chrono::seconds{1});
  EXPECT_EQ(portStat->portStats().cableLengthMeters(), 100);
  EXPECT_EQ(portStat->portStats().cableDelayNsec(), 500);

  saiManagerTable->portManager().triggerCableLengthMeasurement(ports);

  EXPECT_EQ(getMeasureAttr(swPort->getID()), true);
  EXPECT_EQ(getMeasureAttr(port2->getID()), true);
  EXPECT_FALSE(portStat->portStats().cableLengthMeters().has_value());
  EXPECT_FALSE(portStat->portStats().cableDelayNsec().has_value());
}

TEST_F(PortManagerTest, iterator) {
  std::set<PortSaiId> addedPorts;
  auto& portMgr = saiManagerTable->portManager();
  std::shared_ptr<Port> swPort = makePort(p0);
  addedPorts.insert(portMgr.addPort(swPort));
  std::shared_ptr<Port> port2 = makePort(p1);
  addedPorts.insert(portMgr.addPort(port2));
  for (const auto& portIdAnHandle : portMgr) {
    auto& handle = portIdAnHandle.second;
    EXPECT_TRUE(addedPorts.contains(handle->port->adapterKey()));
    addedPorts.erase(handle->port->adapterKey());
  }
  EXPECT_EQ(0, addedPorts.size());
}

TEST_F(PortManagerTest, addDupIdPorts) {
  std::shared_ptr<Port> swPort = makePort(p0);
  saiManagerTable->portManager().addPort(swPort);
  EXPECT_THROW(saiManagerTable->portManager().addPort(swPort), FbossError);
}

TEST_F(PortManagerTest, getBySwId) {
  std::shared_ptr<Port> swPort = makePort(p1);
  saiManagerTable->portManager().addPort(swPort);
  SaiPortHandle* port =
      saiManagerTable->portManager().getPortHandle(PortID(10));
  EXPECT_TRUE(port);
  EXPECT_EQ(GET_OPT_ATTR(Port, AdminState, port->port->attributes()), true);
  EXPECT_EQ(GET_ATTR(Port, Speed, port->port->attributes()), 25000);
  auto hwLaneList = GET_ATTR(Port, HwLaneList, port->port->attributes());
  EXPECT_EQ(hwLaneList.size(), 1);
  EXPECT_EQ(hwLaneList[0], 10);
}

TEST_F(PortManagerTest, getNonExistent) {
  std::shared_ptr<Port> swPort = makePort(p0);
  saiManagerTable->portManager().addPort(swPort);
  SaiPortHandle* port =
      saiManagerTable->portManager().getPortHandle(PortID(10));
  EXPECT_FALSE(port);
}

TEST_F(PortManagerTest, removePort) {
  std::shared_ptr<Port> swPort = makePort(p0);
  saiManagerTable->portManager().addPort(swPort);
  SaiPortHandle* port = saiManagerTable->portManager().getPortHandle(PortID(0));
  EXPECT_TRUE(port);
  saiManagerTable->portManager().removePort(swPort);
  port = saiManagerTable->portManager().getPortHandle(PortID(0));
  EXPECT_FALSE(port);
}

TEST_F(PortManagerTest, removeNonExistentPort) {
  std::shared_ptr<Port> swPort = makePort(p0);
  saiManagerTable->portManager().addPort(swPort);
  SaiPortHandle* port = saiManagerTable->portManager().getPortHandle(PortID(0));
  EXPECT_TRUE(port);
  TestPort p10;
  p10.id = 10;
  std::shared_ptr<Port> swPort10 = makePort(p10);
  EXPECT_THROW(saiManagerTable->portManager().removePort(swPort10), FbossError);
}

TEST_F(PortManagerTest, changePortAdminState) {
  std::shared_ptr<Port> swPort = makePort(p0);
  saiManagerTable->portManager().addPort(swPort);
  swPort->setAdminState(cfg::PortState::DISABLED);
  saiManagerTable->portManager().changePort(swPort, swPort);
  auto handle = saiManagerTable->portManager().getPortHandle(swPort->getID());
  checkPort(swPort->getID(), handle, false);
}

TEST_F(PortManagerTest, changePortDrainState) {
  std::shared_ptr<Port> swPort = makePort(p0);
  saiManagerTable->portManager().addPort(swPort);
  swPort->setPortDrainState(cfg::PortDrainState::DRAINED);
  EXPECT_THROW(
      saiManagerTable->portManager().changePort(swPort, swPort), FbossError);
  swPort->setPortType(cfg::PortType::FABRIC_PORT);
  swPort->setPortDrainState(cfg::PortDrainState::DRAINED);
  saiManagerTable->portManager().changePort(swPort, swPort);
  auto handle = saiManagerTable->portManager().getPortHandle(swPort->getID());
  checkPort(swPort->getID(), handle, true, 9412, false, true);
}

TEST_F(PortManagerTest, changePortMtu) {
  std::shared_ptr<Port> swPort = makePort(p0);
  saiManagerTable->portManager().addPort(swPort);
  swPort->setMaxFrameSize(9000);
  saiManagerTable->portManager().changePort(swPort, swPort);
  auto handle = saiManagerTable->portManager().getPortHandle(swPort->getID());
  checkPort(swPort->getID(), handle, true, 9000);
}

TEST_F(PortManagerTest, changePortNoChange) {
  std::shared_ptr<Port> swPort = makePort(p0);
  saiManagerTable->portManager().addPort(swPort);
  swPort->setSpeed(cfg::PortSpeed::TWENTYFIVEG);
  swPort->setAdminState(cfg::PortState::ENABLED);
  saiManagerTable->portManager().changePort(swPort, swPort);
  auto handle = saiManagerTable->portManager().getPortHandle(swPort->getID());
  checkPort(swPort->getID(), handle, true);
}

TEST_F(PortManagerTest, changeNonExistentPort) {
  std::shared_ptr<Port> swPort = makePort(p0);
  std::shared_ptr<Port> swPort2 = makePort(p1);
  saiManagerTable->portManager().addPort(swPort);
  swPort2->setSpeed(cfg::PortSpeed::TWENTYFIVEG);
  EXPECT_THROW(
      saiManagerTable->portManager().changePort(swPort2, swPort2), FbossError);
}

TEST_F(PortManagerTest, portConsolidationAddPort) {
  PortID portId(0);
  // adds a port "behind the back of" PortManager
  auto saiId0 = addPort(portId, cfg::PortSpeed::TWENTYFIVEG);
  // loads the added port into SaiStore
  saiStore->release();
  saiStore->reload();

  // add a port with the same lanes through PortManager
  std::shared_ptr<Port> swPort = makePort(p0);
  saiManagerTable->portManager().addPort(swPort);
  auto handle1 = saiManagerTable->portManager().getPortHandle(PortID(0));
  auto saiId1 = handle1->port->adapterKey();

  checkPort(portId, handle1, true);
  // expect it to return the existing port rather than create a new one
  EXPECT_EQ(saiId0, saiId1);
}

void checkCounterExport(
    const std::string& portName,
    ExpectExport expectExport) {
  for (auto statKey :
       HwPortFb303Stats("dummy").kPortMonotonicCounterStatKeys()) {
    switch (expectExport) {
      case ExpectExport::EXPORT:
        EXPECT_TRUE(
            facebook::fbData->getStatMap()->contains(
                HwPortFb303Stats::statName(statKey, portName)));
        break;
      case ExpectExport::NO_EXPORT:
        EXPECT_FALSE(
            facebook::fbData->getStatMap()->contains(
                HwPortFb303Stats::statName(statKey, portName)));
        break;
    }
  }
}

TEST_F(PortManagerTest, changePortNameAndCheckCounters) {
  std::shared_ptr<Port> swPort = makePort(p0);
  saiManagerTable->portManager().addPort(swPort);
  for (auto statKey :
       HwPortFb303Stats("dummy").kPortMonotonicCounterStatKeys()) {
    EXPECT_TRUE(
        facebook::fbData->getStatMap()->contains(
            HwPortFb303Stats::statName(statKey, swPort->getName())));
  }
  auto newPort = swPort->clone();
  newPort->setName("eth1/1/1");
  saiManagerTable->portManager().changePort(swPort, newPort);
  checkCounterExport(swPort->getName(), ExpectExport::NO_EXPORT);
  checkCounterExport(newPort->getName(), ExpectExport::EXPORT);
}

TEST_F(PortManagerTest, updateStats) {
  std::shared_ptr<Port> swPort = makePort(p0);
  saiManagerTable->portManager().addPort(swPort);
  auto handle = saiManagerTable->portManager().getPortHandle(swPort->getID());
  checkPort(PortID(0), handle, true);
  saiManagerTable->portManager().updateStats(swPort->getID());
  auto portStat =
      saiManagerTable->portManager().getLastPortStat(swPort->getID());
  for (auto statKey :
       HwPortFb303Stats("dummy").kPortMonotonicCounterStatKeys()) {
    EXPECT_EQ(
        portStat->getCounterLastIncrement(
            HwPortFb303Stats::statName(statKey, swPort->getName())),
        0);
  }
}

TEST_F(PortManagerTest, portDisableStopsCounterExport) {
  std::shared_ptr<Port> swPort = makePort(p0);
  CHECK(swPort->isEnabled());
  saiManagerTable->portManager().addPort(swPort);
  checkCounterExport(swPort->getName(), ExpectExport::EXPORT);
  auto newPort = swPort->clone();
  newPort->setAdminState(cfg::PortState::DISABLED);
  saiManagerTable->portManager().changePort(swPort, newPort);
  checkCounterExport(swPort->getName(), ExpectExport::NO_EXPORT);
}

TEST_F(PortManagerTest, portReenableRestartsCounterExport) {
  std::shared_ptr<Port> swPort = makePort(p0);
  CHECK(swPort->isEnabled());
  saiManagerTable->portManager().addPort(swPort);
  checkCounterExport(swPort->getName(), ExpectExport::EXPORT);
  auto newPort = swPort->clone();
  newPort->setAdminState(cfg::PortState::DISABLED);
  saiManagerTable->portManager().changePort(swPort, newPort);
  checkCounterExport(swPort->getName(), ExpectExport::NO_EXPORT);
  auto newNewPort = newPort->clone();
  newNewPort->setAdminState(cfg::PortState::ENABLED);
  saiManagerTable->portManager().changePort(newPort, newNewPort);
  checkCounterExport(swPort->getName(), ExpectExport::EXPORT);
}

TEST_F(PortManagerTest, collectStatsAfterPortDisable) {
  std::shared_ptr<Port> swPort = makePort(p0);
  CHECK(swPort->isEnabled());
  saiManagerTable->portManager().addPort(swPort);
  checkCounterExport(swPort->getName(), ExpectExport::EXPORT);
  auto newPort = swPort->clone();
  newPort->setAdminState(cfg::PortState::DISABLED);
  saiManagerTable->portManager().changePort(swPort, newPort);
  saiManagerTable->portManager().updateStats(swPort->getID());
  EXPECT_EQ(saiManagerTable->portManager().getPortStats().size(), 0);
  checkCounterExport(swPort->getName(), ExpectExport::NO_EXPORT);
}

TEST_F(PortManagerTest, subsumedPorts) {
  // Port P0 has a port ID 0 and only be configured with all speeds.
  checkSubsumedPorts(p0, cfg::PortSpeed::XG, {});
  checkSubsumedPorts(p0, cfg::PortSpeed::TWENTYFIVEG, {});
  checkSubsumedPorts(p0, cfg::PortSpeed::FIFTYG, {PortID(p0.id + 1)});
  std::vector<PortID> expectedPortList = {
      PortID(p0.id + 1), PortID(p0.id + 2), PortID(p0.id + 3)};
  checkSubsumedPorts(p0, cfg::PortSpeed::FORTYG, expectedPortList);
  checkSubsumedPorts(p0, cfg::PortSpeed::HUNDREDG, expectedPortList);

  // Port P1 has a port ID 10 and only be configured with 10, 25 and 50G modes.
  checkSubsumedPorts(p1, cfg::PortSpeed::XG, {});
  checkSubsumedPorts(p1, cfg::PortSpeed::TWENTYFIVEG, {});
  checkSubsumedPorts(p1, cfg::PortSpeed::FIFTYG, {PortID(p1.id + 1)});
}

TEST_F(PortManagerTest, getTransceiverID) {
  std::vector<uint16_t> controllingPorts = {0, 4, 24};
  std::vector<uint16_t> expectedTcvrIDs = {0, 1, 6};
  for (auto i = 0; i < controllingPorts.size(); i++) {
    for (auto lane = 0; lane < 4; lane++) {
      uint16_t port = controllingPorts[i] + lane;
      SaiPlatformPort* platformPort = saiPlatform->getPort(PortID(port));
      EXPECT_TRUE(platformPort);
      EXPECT_EQ(expectedTcvrIDs[i], platformPort->getTransceiverID().value());
    }
  }
}

TEST_F(PortManagerTest, attributesFromSwPort) {
  std::shared_ptr<Port> swPort = makePort(p0);
  auto& portMgr = saiManagerTable->portManager();
  portMgr.addPort(swPort);
  auto portHandle = portMgr.getPortHandle(PortID(0));
  auto attrs = portMgr.attributesFromSwPort(swPort);
  EXPECT_EQ(attrs, portHandle->port->attributes());
}

TEST_F(PortManagerTest, swPortFromAttributes) {
  std::shared_ptr<Port> swPort = makePort(p0);
  if (swPort->getIngressVlan() == VlanID(0)) {
    // TODO: manager test base class to not set ingress vlan to 0
    swPort->setIngressVlan(VlanID(1)); // vlan 0 is invalid
  }
  auto& portMgr = saiManagerTable->portManager();
  portMgr.addPort(swPort);
  auto attrs = portMgr.attributesFromSwPort(swPort);
  auto newPort =
      portMgr.swPortFromAttributes(attrs, PortSaiId(1), cfg::SwitchType::NPU);
  EXPECT_EQ(attrs, portMgr.attributesFromSwPort(newPort));
}

#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
/**
 * @brief Test that InterFrameGap attribute is set when interpacket gap bits are
 * configured
 */
TEST_F(PortManagerTest, attributesFromSwPortWithInterPacketGap) {
  std::shared_ptr<Port> swPort = makePort(p0);
  auto& portMgr = saiManagerTable->portManager();

  // Test with interpacket gap bits set in switch state
  const uint8_t testInterPacketGapBits = 12;
  swPort->setInterPacketGapBits(testInterPacketGapBits);

  auto attrs = portMgr.attributesFromSwPort(swPort);

  // Verify that InterFrameGap attribute is set correctly
  auto interFrameGapAttr =
      std::get<std::optional<SaiPortTraits::Attributes::InterFrameGap>>(attrs);
  EXPECT_TRUE(interFrameGapAttr.has_value());
  EXPECT_EQ(interFrameGapAttr.value().value(), testInterPacketGapBits);
}

/**
 * @brief Test that InterFrameGap attribute remains unset when no interpacket
 * gap bits are configured
 */
TEST_F(PortManagerTest, attributesFromSwPortWithoutInterPacketGap) {
  std::shared_ptr<Port> swPort = makePort(p0);
  auto& portMgr = saiManagerTable->portManager();

  // Test without interpacket gap bits set (should be nullopt)
  auto attrs = portMgr.attributesFromSwPort(swPort);

  // Verify that InterFrameGap attribute is not set when no interpacket gap bits
  auto interFrameGapAttr =
      std::get<std::optional<SaiPortTraits::Attributes::InterFrameGap>>(attrs);
  EXPECT_FALSE(interFrameGapAttr.has_value());
}
#endif

TEST_F(PortManagerTest, togglePtpTcEnable) {
  std::shared_ptr<Port> swPort = makePort(p0);
  saiManagerTable->portManager().addPort(swPort);
  auto handle = saiManagerTable->portManager().getPortHandle(swPort->getID());
  for (const auto ptpTcEnable : {false, true, false}) {
    saiManagerTable->portManager().setPtpTcEnable(ptpTcEnable);
    checkPort(swPort->getID(), handle, true, 9412, ptpTcEnable);
  }
}

TEST_F(PortManagerTest, getFabricReachabilityForSwitch) {
  std::shared_ptr<Port> swPort0 = makePort(p0);
  swPort0->setPortType(cfg::PortType::FABRIC_PORT);
  saiManagerTable->portManager().addPort(swPort0);

  std::shared_ptr<Port> swPort1 = makePort(p1);
  swPort1->setPortType(cfg::PortType::FABRIC_PORT);
  saiManagerTable->portManager().addPort(swPort1);

  std::vector<PortID> portIds =
      saiManagerTable->portManager().getFabricReachabilityForSwitch(
          static_cast<SwitchID>(0));
  EXPECT_EQ(portIds.size(), 2);
}

TEST_F(PortManagerTest, calculateRate) {
  // test ports have default speed of 25G
  auto speed = cfg::PortSpeed::TWENTYFIVEG;
  for (const auto& testInterface : testInterfaces) {
    for (const auto& remoteHost : testInterface.remoteHosts) {
      std::shared_ptr<Port> swPort = makePort(remoteHost.port);
      auto rate = saiManagerTable->portManager().calculateRate(
          static_cast<int>(swPort->getSpeed()));
      EXPECT_EQ(
          rate,
          static_cast<int>(speed) / kSpeedConversionFactor *
              kRateConversionFactor);
    }
  }
}

TEST_F(PortManagerTest, updatePrbsStatsEntryRate) {
  std::shared_ptr<Port> swPort0 = makePort(p0);
  auto newSpeed = cfg::PortSpeed::FIFTYG;
  saiManagerTable->portManager().addPort(swPort0);
  swPort0->setSpeed(newSpeed);
  saiManagerTable->portManager().updatePrbsStatsEntryRate(swPort0);
  EXPECT_EQ(
      saiManagerTable->portManager()
          .portAsicPrbsStats_[swPort0->getID()][0]
          .getRate(),
      static_cast<int>(newSpeed) / kSpeedConversionFactor *
          kRateConversionFactor);
}

#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
namespace {
// A LLR profile state node with a distinct value per field so a mis-mapped
// attribute is caught.
std::shared_ptr<LlrConfig> makeLlrConfigNode() {
  const std::string kLlrProfileId{"llrProfile"};
  auto llr = std::make_shared<LlrConfig>(kLlrProfileId);
  llr->setOutstandingFramesMax(32);
  llr->setOutstandingBytesMax(4096);
  llr->setReplayTimerMax(5000);
  llr->setReplayCountMax(7);
  llr->setPcsLostTimeout(1000);
  llr->setDataAgeTimeout(200000);
  llr->setInitFrameAction(cfg::LlrFrameAction::BEST_EFFORT);
  llr->setFlushFrameAction(cfg::LlrFrameAction::BLOCK);
  llr->setReInitOnFlush(true);
  llr->setCtlosTargetSpacing(2048);
  return llr;
}

// A second profile whose values (and name) all differ from makeLlrConfigNode,
// so switching between them yields a different content key.
std::shared_ptr<LlrConfig> makeAltLlrConfigNode() {
  const std::string kAltLlrProfileId{"llrProfileAlt"};
  auto llr = std::make_shared<LlrConfig>(kAltLlrProfileId);
  llr->setOutstandingFramesMax(64);
  llr->setOutstandingBytesMax(8192);
  llr->setReplayTimerMax(6000);
  llr->setReplayCountMax(3);
  llr->setPcsLostTimeout(2000);
  llr->setDataAgeTimeout(100000);
  llr->setInitFrameAction(cfg::LlrFrameAction::DISCARD);
  llr->setFlushFrameAction(cfg::LlrFrameAction::DISCARD);
  llr->setReInitOnFlush(false);
  llr->setCtlosTargetSpacing(4096);
  return llr;
}
} // namespace

TEST_F(PortManagerTest, programLlrOnAddPort) {
  auto swPort = makePort(p0);
  swPort->setLlrConfigName("llrProfile");
  swPort->setLlrConfig(makeLlrConfigNode());
  saiManagerTable->portManager().addPort(swPort);

  auto handle = saiManagerTable->portManager().getPortHandle(swPort->getID());
  ASSERT_NE(handle, nullptr);
  ASSERT_NE(handle->llrProfile, nullptr);

  auto& portApi = saiApiTable->portApi();
  auto portSaiId = handle->port->adapterKey();
  auto profileSaiId = handle->llrProfile->adapterKey();

  // Port is bound to the profile with both LLR modes enabled.
  EXPECT_EQ(
      portApi.getAttribute(
          portSaiId, SaiPortTraits::Attributes::LlrModeLocal{}),
      true);
  EXPECT_EQ(
      portApi.getAttribute(
          portSaiId, SaiPortTraits::Attributes::LlrModeRemote{}),
      true);
  EXPECT_EQ(
      portApi.getAttribute(portSaiId, SaiPortTraits::Attributes::LlrProfile{}),
      static_cast<sai_object_id_t>(profileSaiId));

  // Profile object carries the values from the port's LlrConfig.
  EXPECT_EQ(
      portApi.getAttribute(
          profileSaiId,
          SaiPortLlrProfileTraits::Attributes::OutstandingFramesMax{}),
      32);
  EXPECT_EQ(
      portApi.getAttribute(
          profileSaiId, SaiPortLlrProfileTraits::Attributes::ReplayCountMax{}),
      7);
  EXPECT_EQ(
      portApi.getAttribute(
          profileSaiId,
          SaiPortLlrProfileTraits::Attributes::InitLlrFrameAction{}),
      SAI_LLR_FRAME_ACTION_BEST_EFFORT);
  EXPECT_EQ(
      portApi.getAttribute(
          profileSaiId,
          SaiPortLlrProfileTraits::Attributes::CtlosTargetSpacing{}),
      2048);
}

TEST_F(PortManagerTest, clearLlrOnChangePort) {
  // LLR config only changes on an admin disabled port, so these ports are
  // disabled throughout. StateUpdateValidator enforces that upstream.
  auto swPort = makePort(p0);
  swPort->setAdminState(cfg::PortState::DISABLED);
  swPort->setLlrConfigName("llrProfile");
  swPort->setLlrConfig(makeLlrConfigNode());
  saiManagerTable->portManager().addPort(swPort);
  ASSERT_NE(
      saiManagerTable->portManager().getPortHandle(swPort->getID())->llrProfile,
      nullptr);

  // A new port state with no LLR config clears the profile and disables modes.
  auto newPort = makePort(p0);
  newPort->setAdminState(cfg::PortState::DISABLED);
  saiManagerTable->portManager().changePort(swPort, newPort);

  auto handle = saiManagerTable->portManager().getPortHandle(newPort->getID());
  EXPECT_EQ(handle->llrProfile, nullptr);

  auto& portApi = saiApiTable->portApi();
  auto portSaiId = handle->port->adapterKey();
  EXPECT_EQ(
      portApi.getAttribute(
          portSaiId, SaiPortTraits::Attributes::LlrModeLocal{}),
      false);
  EXPECT_EQ(
      portApi.getAttribute(portSaiId, SaiPortTraits::Attributes::LlrProfile{}),
      SAI_NULL_OBJECT_ID);
}

// A port that wants LLR is created administratively disabled so the profile can
// be attached, then enabled. The fake rejects a profile attach on an enabled
// port, so programming succeeding at all is what shows the port was down for
// it; the assertions cover where it ends up.
TEST_F(PortManagerTest, llrOnCreateLeavesPortAdminEnabled) {
  auto swPort = makePort(p0);
  swPort->setLlrConfigName("llrProfile");
  swPort->setLlrConfig(makeLlrConfigNode());
  saiManagerTable->portManager().addPort(swPort);

  auto* handle = saiManagerTable->portManager().getPortHandle(swPort->getID());
  ASSERT_NE(handle->llrProfile, nullptr);
  auto& portApi = saiApiTable->portApi();
  auto portSaiId = handle->port->adapterKey();

  EXPECT_TRUE(
      portApi.getAttribute(portSaiId, SaiPortTraits::Attributes::AdminState{}));
  EXPECT_EQ(
      portApi.getAttribute(
          portSaiId, SaiPortTraits::Attributes::LlrModeLocal{}),
      true);
  EXPECT_EQ(
      portApi.getAttribute(
          portSaiId, SaiPortTraits::Attributes::LlrModeRemote{}),
      true);
}

// Enabling a port and binding LLR to it in one update is the shape the first
// config on a cold boot takes. changePortImpl writes the port object from the
// new port, so without holding the enable the profile attach would land on a
// port the fake already considers enabled and be rejected.
TEST_F(PortManagerTest, llrBindWhileEnablingPort) {
  auto swPort = makePort(p0);
  swPort->setAdminState(cfg::PortState::DISABLED);
  saiManagerTable->portManager().addPort(swPort);

  auto newPort = makePort(p0);
  newPort->setAdminState(cfg::PortState::ENABLED);
  newPort->setLlrConfigName("llrProfile");
  newPort->setLlrConfig(makeLlrConfigNode());
  saiManagerTable->portManager().changePort(swPort, newPort);

  auto* handle = saiManagerTable->portManager().getPortHandle(newPort->getID());
  ASSERT_NE(handle->llrProfile, nullptr);
  auto& portApi = saiApiTable->portApi();
  auto portSaiId = handle->port->adapterKey();
  EXPECT_TRUE(
      portApi.getAttribute(portSaiId, SaiPortTraits::Attributes::AdminState{}));
  EXPECT_EQ(
      portApi.getAttribute(
          portSaiId, SaiPortTraits::Attributes::LlrModeLocal{}),
      true);
}

// A port configured disabled stays disabled: the enable that follows LLR
// programming applies the configured admin state rather than enabling
// unconditionally, so a port an operator drained is not brought up.
TEST_F(PortManagerTest, llrProgrammingLeavesDisabledPortDisabled) {
  auto swPort = makePort(p0);
  swPort->setAdminState(cfg::PortState::DISABLED);
  swPort->setLlrConfigName("llrProfile");
  swPort->setLlrConfig(makeLlrConfigNode());
  saiManagerTable->portManager().addPort(swPort);

  auto& portApi = saiApiTable->portApi();
  auto portSaiId = saiManagerTable->portManager()
                       .getPortHandle(swPort->getID())
                       ->port->adapterKey();
  ASSERT_FALSE(
      portApi.getAttribute(portSaiId, SaiPortTraits::Attributes::AdminState{}));

  auto newPort = makePort(p0);
  newPort->setAdminState(cfg::PortState::DISABLED);
  newPort->setLlrConfigName("llrProfileAlt");
  newPort->setLlrConfig(makeAltLlrConfigNode());
  saiManagerTable->portManager().changePort(swPort, newPort);

  auto handle = saiManagerTable->portManager().getPortHandle(newPort->getID());
  ASSERT_NE(handle->llrProfile, nullptr);
  EXPECT_FALSE(portApi.getAttribute(
      handle->port->adapterKey(), SaiPortTraits::Attributes::AdminState{}));
}

// Retuning LLR on a port the same update disables, the shape an operator drain
// and retune takes in one config push. No enable is held here: the attributes
// carry the new port's admin state, so writing the port object is what takes it
// down ahead of programLlr(). The fake rejects a profile rebind on an enabled
// port, so the rebind landing at all is what shows that ordering.
TEST_F(PortManagerTest, llrChangeWhileDisablingPort) {
  auto swPort = makePort(p0);
  swPort->setAdminState(cfg::PortState::ENABLED);
  swPort->setLlrConfigName("llrProfile");
  swPort->setLlrConfig(makeLlrConfigNode());
  saiManagerTable->portManager().addPort(swPort);

  auto* handle = saiManagerTable->portManager().getPortHandle(swPort->getID());
  ASSERT_NE(handle->llrProfile, nullptr);
  auto originalProfile = handle->llrProfile->adapterKey();
  auto& portApi = saiApiTable->portApi();
  auto portSaiId = handle->port->adapterKey();
  ASSERT_TRUE(
      portApi.getAttribute(portSaiId, SaiPortTraits::Attributes::AdminState{}));

  auto newPort = makePort(p0);
  newPort->setAdminState(cfg::PortState::DISABLED);
  newPort->setLlrConfigName("llrProfileAlt");
  newPort->setLlrConfig(makeAltLlrConfigNode());
  saiManagerTable->portManager().changePort(swPort, newPort);

  handle = saiManagerTable->portManager().getPortHandle(newPort->getID());
  ASSERT_NE(handle->llrProfile, nullptr);
  EXPECT_NE(handle->llrProfile->adapterKey(), originalProfile);
  EXPECT_FALSE(
      portApi.getAttribute(portSaiId, SaiPortTraits::Attributes::AdminState{}));
  EXPECT_EQ(
      portApi.getAttribute(portSaiId, SaiPortTraits::Attributes::LlrProfile{}),
      handle->llrProfile->adapterKey());
}

// The hold is keyed on whether programLlr() will write, not on whether the SAI
// port object is new. changePortByRecreate reaches addPortImpl with an object
// that already exists: on a VCO change it removes the ports in the port macro,
// pre-creates them through createPortWithBasicAttributes -- administratively
// enabled and carrying no LLR profile -- and then calls addPort.
//
// Reproduce that shape by dropping the handle and putting the object back into
// the store enabled and unbound, then adding a port that wants LLR. The fake
// rejects a profile attach on an enabled port, so the attach landing is what
// shows the enable was held even though the object was already there.
TEST_F(PortManagerTest, llrHoldsEnableWhenPortObjectExistsUnbound) {
  auto basic = makePort(p0);
  basic->setAdminState(cfg::PortState::ENABLED);
  saiManagerTable->portManager().addPort(basic);

  auto* handle = saiManagerTable->portManager().getPortHandle(basic->getID());
  auto portKey = handle->port->adapterHostKey();
  auto portAttrs = handle->port->attributes();
  ASSERT_FALSE(
      std::get<std::optional<SaiPortTraits::Attributes::LlrProfile>>(portAttrs)
          .has_value());
  saiManagerTable->portManager().removePort(basic);
  auto preCreated = saiStore->get<SaiPortTraits>().setObject(
      portKey, portAttrs, basic->getID());
  auto& portApi = saiApiTable->portApi();
  auto portSaiId = preCreated->adapterKey();
  ASSERT_TRUE(
      portApi.getAttribute(portSaiId, SaiPortTraits::Attributes::AdminState{}));

  auto withLlr = makePort(p0);
  withLlr->setAdminState(cfg::PortState::ENABLED);
  withLlr->setLlrConfigName("llrProfile");
  withLlr->setLlrConfig(makeLlrConfigNode());
  saiManagerTable->portManager().addPort(withLlr);

  handle = saiManagerTable->portManager().getPortHandle(withLlr->getID());
  ASSERT_NE(handle->llrProfile, nullptr);
  EXPECT_EQ(
      portApi.getAttribute(
          handle->port->adapterKey(), SaiPortTraits::Attributes::LlrProfile{}),
      handle->llrProfile->adapterKey());
  EXPECT_TRUE(portApi.getAttribute(
      handle->port->adapterKey(), SaiPortTraits::Attributes::AdminState{}));
}

TEST_F(PortManagerTest, reconfigureLlrOnChangePort) {
  auto swPort = makePort(p0);
  swPort->setAdminState(cfg::PortState::DISABLED);
  swPort->setLlrConfigName("llrProfile");
  swPort->setLlrConfig(makeLlrConfigNode());
  saiManagerTable->portManager().addPort(swPort);

  auto* handle = saiManagerTable->portManager().getPortHandle(swPort->getID());
  ASSERT_NE(handle->llrProfile, nullptr);
  auto oldProfileSaiId = handle->llrProfile->adapterKey();
  auto fs = FakeSai::getInstance();
  EXPECT_EQ(fs->portLlrProfileManager.map().size(), 1);

  // Change to a different profile: the content key changes, so a new SAI
  // profile is created and bound and the old one is torn down.
  auto newPort = makePort(p0);
  newPort->setAdminState(cfg::PortState::DISABLED);
  newPort->setLlrConfigName("llrProfileAlt");
  newPort->setLlrConfig(makeAltLlrConfigNode());
  saiManagerTable->portManager().changePort(swPort, newPort);

  handle = saiManagerTable->portManager().getPortHandle(newPort->getID());
  ASSERT_NE(handle->llrProfile, nullptr);
  auto newProfileSaiId = handle->llrProfile->adapterKey();
  EXPECT_NE(newProfileSaiId, oldProfileSaiId);
  // Exactly one profile remains and it is not the old one.
  EXPECT_EQ(fs->portLlrProfileManager.map().size(), 1);
  EXPECT_EQ(
      fs->portLlrProfileManager.map().count(
          static_cast<sai_object_id_t>(oldProfileSaiId)),
      0);

  auto& portApi = saiApiTable->portApi();
  auto portSaiId = handle->port->adapterKey();
  // Port now points at the new profile, modes still enabled.
  EXPECT_EQ(
      portApi.getAttribute(portSaiId, SaiPortTraits::Attributes::LlrProfile{}),
      static_cast<sai_object_id_t>(newProfileSaiId));
  EXPECT_EQ(
      portApi.getAttribute(
          portSaiId, SaiPortTraits::Attributes::LlrModeLocal{}),
      true);
  // New profile carries the updated values.
  EXPECT_EQ(
      portApi.getAttribute(
          newProfileSaiId,
          SaiPortLlrProfileTraits::Attributes::OutstandingFramesMax{}),
      64);
  EXPECT_EQ(
      portApi.getAttribute(
          newProfileSaiId,
          SaiPortLlrProfileTraits::Attributes::InitLlrFrameAction{}),
      SAI_LLR_FRAME_ACTION_DISCARD);
}

TEST_F(PortManagerTest, reenableLlrOnChangePort) {
  auto swPort = makePort(p0);
  swPort->setAdminState(cfg::PortState::DISABLED);
  swPort->setLlrConfigName("llrProfile");
  swPort->setLlrConfig(makeLlrConfigNode());
  saiManagerTable->portManager().addPort(swPort);

  // Disable: change to a port with no LLR config.
  auto clearedPort = makePort(p0);
  clearedPort->setAdminState(cfg::PortState::DISABLED);
  saiManagerTable->portManager().changePort(swPort, clearedPort);
  ASSERT_EQ(
      saiManagerTable->portManager()
          .getPortHandle(clearedPort->getID())
          ->llrProfile,
      nullptr);

  // Re-enable: change back to a port carrying LLR config.
  auto reenabledPort = makePort(p0);
  reenabledPort->setAdminState(cfg::PortState::DISABLED);
  reenabledPort->setLlrConfigName("llrProfile");
  reenabledPort->setLlrConfig(makeLlrConfigNode());
  saiManagerTable->portManager().changePort(clearedPort, reenabledPort);

  auto* handle =
      saiManagerTable->portManager().getPortHandle(reenabledPort->getID());
  ASSERT_NE(handle->llrProfile, nullptr);
  auto& portApi = saiApiTable->portApi();
  auto portSaiId = handle->port->adapterKey();
  EXPECT_EQ(
      portApi.getAttribute(portSaiId, SaiPortTraits::Attributes::LlrProfile{}),
      static_cast<sai_object_id_t>(handle->llrProfile->adapterKey()));
  EXPECT_EQ(
      portApi.getAttribute(
          portSaiId, SaiPortTraits::Attributes::LlrModeLocal{}),
      true);
  EXPECT_EQ(
      portApi.getAttribute(
          portSaiId, SaiPortTraits::Attributes::LlrModeRemote{}),
      true);
}

TEST_F(PortManagerTest, updateLlrStatsWhenEnabled) {
  auto swPort = makePort(p0);
  swPort->setLlrConfigName("llrProfile");
  swPort->setLlrConfig(makeLlrConfigNode());
  saiManagerTable->portManager().addPort(swPort);
  ASSERT_NE(
      saiManagerTable->portManager().getPortHandle(swPort->getID())->llrProfile,
      nullptr);

  saiManagerTable->portManager().updateStats(swPort->getID());

  auto* portStat =
      saiManagerTable->portManager().getLastPortStat(swPort->getID());
  ASSERT_NE(portStat, nullptr);
  auto stats = portStat->portStats();
  // The isolated LLR read fired, so the LLR counters are collected. Fake SAI
  // has no dataplane, so the values are 0 -- assert only that they are
  // populated.
  EXPECT_TRUE(stats.llrTxOk_().has_value());
  EXPECT_TRUE(stats.llrRxOk_().has_value());
  EXPECT_TRUE(stats.llrTxReplay_().has_value());
  EXPECT_TRUE(stats.llrRxExpectedSeqGood_().has_value());
  EXPECT_EQ(*stats.llrTxOk_(), 0);
  // The Broadcom LLR stat extensions are not standard SAI enums, so
  // SaiPortTraits::llrExtensionStats() is empty on fake and the second read is
  // skipped entirely. Coverage for those fields is AgentHwLlrTest.
  EXPECT_FALSE(stats.llrTxIneligiblePkts_().has_value());
}

TEST_F(PortManagerTest, noLlrStatsWhenDisabled) {
  auto swPort = makePort(p0);
  saiManagerTable->portManager().addPort(swPort);
  ASSERT_EQ(
      saiManagerTable->portManager().getPortHandle(swPort->getID())->llrProfile,
      nullptr);

  saiManagerTable->portManager().updateStats(swPort->getID());

  auto* portStat =
      saiManagerTable->portManager().getLastPortStat(swPort->getID());
  ASSERT_NE(portStat, nullptr);
  auto stats = portStat->portStats();
  // No profile bound -> the LLR read is skipped and the fields stay unset.
  EXPECT_FALSE(stats.llrTxOk_().has_value());
  EXPECT_FALSE(stats.llrRxOk_().has_value());
}

// Removing an LLR-enabled port tears down its LLR profile (no leak).
TEST_F(PortManagerTest, removeLlrOnRemovePort) {
  auto swPort = makePort(p0);
  swPort->setLlrConfigName("llrProfile");
  swPort->setLlrConfig(makeLlrConfigNode());
  saiManagerTable->portManager().addPort(swPort);

  auto fs = FakeSai::getInstance();
  ASSERT_NE(
      saiManagerTable->portManager().getPortHandle(swPort->getID())->llrProfile,
      nullptr);
  EXPECT_EQ(fs->portLlrProfileManager.map().size(), 1);

  saiManagerTable->portManager().removePort(swPort);

  // Port gone -> its LLR profile is released (refcount -> 0).
  EXPECT_EQ(fs->portLlrProfileManager.map().size(), 0);
}

// Two ports with an identical LlrConfig share a single content-keyed SAI
// profile; the profile is freed only when the last referencing port is removed.
TEST_F(PortManagerTest, shareLlrProfileAcrossPorts) {
  auto fs = FakeSai::getInstance();

  auto port0 = makePort(p0);
  port0->setLlrConfigName("llrProfile");
  port0->setLlrConfig(makeLlrConfigNode());
  saiManagerTable->portManager().addPort(port0);

  auto port1 = makePort(p1);
  port1->setLlrConfigName("llrProfile");
  port1->setLlrConfig(makeLlrConfigNode()); // identical content
  saiManagerTable->portManager().addPort(port1);

  // Identical config -> exactly one shared profile, both ports bound to it.
  EXPECT_EQ(fs->portLlrProfileManager.map().size(), 1);
  auto* h0 = saiManagerTable->portManager().getPortHandle(port0->getID());
  auto* h1 = saiManagerTable->portManager().getPortHandle(port1->getID());
  ASSERT_NE(h0->llrProfile, nullptr);
  ASSERT_NE(h1->llrProfile, nullptr);
  EXPECT_EQ(h0->llrProfile->adapterKey(), h1->llrProfile->adapterKey());

  // Removing one port keeps the profile (still referenced by the other).
  saiManagerTable->portManager().removePort(port0);
  EXPECT_EQ(fs->portLlrProfileManager.map().size(), 1);

  // Removing the last referencing port frees it.
  saiManagerTable->portManager().removePort(port1);
  EXPECT_EQ(fs->portLlrProfileManager.map().size(), 0);
}

TEST_F(PortManagerTest, programPrecodingFromPlatformMappingWhenEnabled) {
  gflags::FlagSaver flagSaver;
  FLAGS_montblanc_precoding = false;

  auto verifyPrecoding = [&](std::optional<bool> txEnabled,
                             std::optional<bool> rxEnabled,
                             const std::vector<int32_t>& expectedTx,
                             const std::vector<int32_t>& expectedRx) {
    auto swPort = makePort(p0);
    auto pinConfigs = swPort->getPinConfigs();
    for (auto& pinConfig : pinConfigs) {
      pinConfig.rx()->precoding() = 2;
      pinConfig.tx()->precoding() = 1;
    }
    swPort->resetPinConfigs(pinConfigs);
    swPort->setTxPrecoding(txEnabled);
    swPort->setRxPrecoding(rxEnabled);

    saiManagerTable->portManager().addPort(swPort);
    auto* handle =
        saiManagerTable->portManager().getPortHandle(swPort->getID());
    const auto& fakeSerdes = FakeSai::getInstance()->portSerdesManager.get(
        handle->serdes->adapterKey());
    EXPECT_EQ(fakeSerdes.txPrecoding, expectedTx);
    EXPECT_EQ(fakeSerdes.rxPrecoding, expectedRx);
    saiManagerTable->portManager().removePort(swPort);
  };

  verifyPrecoding(std::nullopt, std::nullopt, {}, {});
  verifyPrecoding(false, false, {}, {});
  verifyPrecoding(true, false, {1}, {});
  verifyPrecoding(false, true, {}, {2});
  verifyPrecoding(true, true, {1}, {2});
}

TEST_F(PortManagerTest, programPrecodingWhenMontblancFlagEnabled) {
  gflags::FlagSaver flagSaver;
  FLAGS_montblanc_precoding = true;

  auto swPort = makePort(p0);
  auto pinConfigs = swPort->getPinConfigs();
  for (auto& pinConfig : pinConfigs) {
    pinConfig.rx()->precoding() = 2;
    pinConfig.tx()->precoding() = 1;
  }
  swPort->resetPinConfigs(pinConfigs);

  saiManagerTable->portManager().addPort(swPort);
  auto* handle = saiManagerTable->portManager().getPortHandle(swPort->getID());
  const auto& fakeSerdes = FakeSai::getInstance()->portSerdesManager.get(
      handle->serdes->adapterKey());
  EXPECT_EQ(fakeSerdes.txPrecoding, std::vector<int32_t>{1});
  EXPECT_EQ(fakeSerdes.rxPrecoding, std::vector<int32_t>{2});
}

TEST_F(PortManagerTest, preservePrecodingWhenSkippingSerdesProgramming) {
  auto swPort = makePort(p0);
  auto pinConfigs = swPort->getPinConfigs();
  for (auto& pinConfig : pinConfigs) {
    pinConfig.rx()->precoding() = 2;
    pinConfig.tx()->precoding() = 1;
  }

  saiManagerTable->portManager().addPort(swPort);
  const auto* handle =
      saiManagerTable->portManager().getPortHandle(swPort->getID());
  const auto getAttrs = [&](bool txPrecodingEnabled, bool rxPrecodingEnabled) {
    return saiManagerTable->portManager().serdesAttributesFromSwPinConfigs(
        handle->port->adapterKey(),
        pinConfigs,
        handle->serdes,
        false,
        std::nullopt,
        true,
        txPrecodingEnabled,
        rxPrecodingEnabled);
  };

  const auto disabledAttrs = getAttrs(false, false);
  EXPECT_FALSE(
      std::get<std::optional<SaiPortSerdesTraits::Attributes::TxPrecodingAttr>>(
          disabledAttrs)
          .has_value());
  EXPECT_FALSE(
      std::get<std::optional<SaiPortSerdesTraits::Attributes::RxPrecodingAttr>>(
          disabledAttrs)
          .has_value());

  const auto attrs = getAttrs(true, true);
  const auto& txPrecoding =
      std::get<std::optional<SaiPortSerdesTraits::Attributes::TxPrecodingAttr>>(
          attrs);
  const auto& rxPrecoding =
      std::get<std::optional<SaiPortSerdesTraits::Attributes::RxPrecodingAttr>>(
          attrs);
  ASSERT_TRUE(txPrecoding.has_value());
  ASSERT_TRUE(rxPrecoding.has_value());
  EXPECT_EQ(txPrecoding->value(), std::vector<int32_t>{1});
  EXPECT_EQ(rxPrecoding->value(), std::vector<int32_t>{2});
}

TEST_F(PortManagerTest, programPrecodingWhenEnabledAfterPortCreation) {
  gflags::FlagSaver flagSaver;
  FLAGS_montblanc_precoding = false;

  auto swPort = makePort(p0);
  auto pinConfigs = swPort->getPinConfigs();
  for (auto& pinConfig : pinConfigs) {
    pinConfig.rx()->precoding() = 2;
    pinConfig.tx()->precoding() = 1;
#if defined(BRCM_SAI_SDK_GTE_13_0)
    pinConfig.rx()->rxReach() = phy::RxReach::RX_EXTENDED_REACH;
#endif
  }
  swPort->resetPinConfigs(pinConfigs);
  saiManagerTable->portManager().addPort(swPort);

  auto* handle = saiManagerTable->portManager().getPortHandle(swPort->getID());
  const auto serdesId = handle->serdes->adapterKey();
  const auto& initialFakeSerdes =
      FakeSai::getInstance()->portSerdesManager.get(serdesId);
  EXPECT_TRUE(initialFakeSerdes.txPrecoding.empty());
  EXPECT_TRUE(initialFakeSerdes.rxPrecoding.empty());
#if defined(BRCM_SAI_SDK_GTE_13_0)
  EXPECT_TRUE(initialFakeSerdes.rxReach.empty());
#endif
  auto newPort = swPort->clone();
  newPort->setTxPrecoding(true);
  newPort->setRxPrecoding(true);
  saiManagerTable->portManager().changePort(swPort, newPort);

  handle = saiManagerTable->portManager().getPortHandle(newPort->getID());
  const auto& fakeSerdes = FakeSai::getInstance()->portSerdesManager.get(
      handle->serdes->adapterKey());
  EXPECT_EQ(fakeSerdes.txPrecoding, std::vector<int32_t>{1});
  EXPECT_EQ(fakeSerdes.rxPrecoding, std::vector<int32_t>{2});
#if defined(BRCM_SAI_SDK_GTE_13_0)
  EXPECT_EQ(
      fakeSerdes.rxReach, std::vector<int32_t>{SAI_PORT_SERDES_REACH_MODE_ER});
#endif
}

TEST_F(PortManagerTest, ignoresUnsetAuxiliarySerdesAttributesOnWarmBoot) {
  gflags::FlagSaver flagSaver;
  FLAGS_montblanc_precoding = false;

  auto swPort = makePort(p0);
  auto pinConfigs = swPort->getPinConfigs();
  for (auto& pinConfig : pinConfigs) {
    pinConfig.rx()->precoding() = 2;
    pinConfig.tx()->precoding() = 1;
#if defined(BRCM_SAI_SDK_GTE_13_0)
    pinConfig.rx()->rxReach() = phy::RxReach::RX_EXTENDED_REACH;
#endif
  }
  swPort->resetPinConfigs(pinConfigs);
  saiManagerTable->portManager().addPort(swPort);

  auto* handle = saiManagerTable->portManager().getPortHandle(swPort->getID());
  ASSERT_NE(handle, nullptr);
  auto fakeSai = FakeSai::getInstance();
  auto serdesAttributes = handle->serdes->attributes();
  const auto txPrecoding =
      SaiPortSerdesTraits::Attributes::TxPrecodingAttr{std::vector<int32_t>{0}};
  const auto rxPrecoding =
      SaiPortSerdesTraits::Attributes::RxPrecodingAttr{std::vector<int32_t>{0}};
  SaiPortSerdesTraits::AdapterHostKey serdesKey{handle->port->adapterKey()};
  std::get<std::optional<SaiPortSerdesTraits::Attributes::TxPrecodingAttr>>(
      serdesAttributes) = txPrecoding;
  std::get<std::optional<SaiPortSerdesTraits::Attributes::RxPrecodingAttr>>(
      serdesAttributes) = rxPrecoding;
#if defined(BRCM_SAI_SDK_GTE_13_0)
  const auto rxReach = SaiPortSerdesTraits::Attributes::RxReach{
      std::vector<int32_t>{SAI_PORT_SERDES_REACH_MODE_ER}};
  std::get<std::optional<SaiPortSerdesTraits::Attributes::RxReach>>(
      serdesAttributes) = rxReach;
#endif
  handle->serdes = saiStore->get<SaiPortSerdesTraits>().setObject(
      serdesKey, serdesAttributes);

  auto newPort = swPort->clone();
  newPort->setTxPrecoding(false);
  newPort->setRxPrecoding(false);
  saiManagerTable->portManager().changePort(swPort, newPort);

  handle = saiManagerTable->portManager().getPortHandle(newPort->getID());
  ASSERT_NE(handle, nullptr);
  const auto& fakeSerdesAfterWarmBoot =
      fakeSai->portSerdesManager.get(handle->serdes->adapterKey());
  EXPECT_EQ(fakeSerdesAfterWarmBoot.txPrecoding, std::vector<int32_t>{0});
  EXPECT_EQ(fakeSerdesAfterWarmBoot.rxPrecoding, std::vector<int32_t>{0});
#if defined(BRCM_SAI_SDK_GTE_13_0)
  EXPECT_EQ(
      fakeSerdesAfterWarmBoot.rxReach,
      std::vector<int32_t>{SAI_PORT_SERDES_REACH_MODE_ER});
#endif
}
#endif

TEST_F(PortManagerTest, zeroPreemphasisZeroesLanesWithoutTxSettings) {
  auto swPort = makePort(p0);
  saiManagerTable->portManager().addPort(swPort);
  auto* handle = saiManagerTable->portManager().getPortHandle(swPort->getID());

  auto pinConfigs = swPort->getPinConfigs();
  ASSERT_FALSE(pinConfigs.empty());
  for (auto& pinConfig : pinConfigs) {
    pinConfig.tx().reset();
  }

  auto txAttrs = [&](bool zeroPreemphasis) {
    auto attrs =
        saiManagerTable->portManager().serdesAttributesFromSwPinConfigs(
            handle->port->adapterKey(),
            pinConfigs,
            handle->serdes,
            zeroPreemphasis);
    return std::make_tuple(
        std::get<std::optional<SaiPortSerdesTraits::Attributes::TxFirPre1>>(
            attrs),
        std::get<std::optional<SaiPortSerdesTraits::Attributes::TxFirMain>>(
            attrs),
        std::get<std::optional<SaiPortSerdesTraits::Attributes::TxFirPost1>>(
            attrs));
  };

  // Without zero preemphasis, lanes with no TX settings are left to the SDK.
  auto [pre1, mainTap, post1] = txAttrs(false);
  EXPECT_FALSE(pre1.has_value());
  EXPECT_FALSE(mainTap.has_value());
  EXPECT_FALSE(post1.has_value());

  auto [zeroPre1, zeroMain, zeroPost1] = txAttrs(true);
  SaiPortSerdesTraits::Attributes::TxFirMain::ValueType zeros(
      pinConfigs.size(), 0);
  ASSERT_TRUE(zeroPre1.has_value());
  ASSERT_TRUE(zeroMain.has_value());
  ASSERT_TRUE(zeroPost1.has_value());
  EXPECT_EQ(zeroPre1->value(), zeros);
  EXPECT_EQ(zeroMain->value(), zeros);
  EXPECT_EQ(zeroPost1->value(), zeros);
}
} // namespace facebook::fboss
