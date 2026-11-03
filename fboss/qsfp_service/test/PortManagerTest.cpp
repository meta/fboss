// Copyright 2021-present Facebook. All Rights Reserved.

#include "fboss/qsfp_service/PortManager.h"
#include <fcntl.h>
#include <folly/FileUtil.h>
#include <folly/json/json.h>
#include <folly/testing/TestUtil.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <optional>
#include "fboss/qsfp_service/platforms/wedge/tests/MockWedgeManager.h"
#include "fboss/qsfp_service/test/FakeConfigsHelper.h"
#include "fboss/qsfp_service/test/MockManagerConstructorArgs.h"
#include "fboss/qsfp_service/test/MockPhyManager.h"
#include "fboss/qsfp_service/test/MockPortManager.h"

namespace facebook::fboss {

class PortManagerTest : public ::testing::Test {
 public:
  folly::test::TemporaryDirectory tmpDir = folly::test::TemporaryDirectory();
  std::string qsfpSvcVolatileDir = tmpDir.path().string();
  std::string qsfpCfgPath = qsfpSvcVolatileDir + "/fakeQsfpConfig";

  void SetUp() override {
    gflags::SetCommandLineOptionWithMode(
        "port_manager_mode", "t", gflags::SET_FLAGS_DEFAULT);

    gflags::SetCommandLineOptionWithMode(
        "qsfp_service_volatile_dir",
        qsfpSvcVolatileDir.c_str(),
        gflags::SET_FLAGS_DEFAULT);

    setupFakeQsfpConfig(qsfpCfgPath);

    initManagers();
  }

 protected:
  void initManagers(
      int numModules = 1,
      int numPortsPerModule = 4,
      bool setPhyManager = false) {
    const auto platformMapping =
        makeFakePlatformMapping(numModules, numPortsPerModule);
    const std::shared_ptr<const PlatformMapping> castedPlatformMapping =
        platformMapping;

    // Create Threads Object
    const auto qsfpServiceThreads = makeQsfpServiceThreads(platformMapping);

    std::unique_ptr<MockPhyManager> phyManager =
        std::make_unique<MockPhyManager>(platformMapping.get());
    phyManager_ = phyManager.get();
    transceiverManager_ = std::make_shared<MockWedgeManager>(
        numModules, 4, platformMapping, qsfpServiceThreads);
    portManager_ = std::make_unique<MockPortManager>(
        transceiverManager_.get(),
        setPhyManager ? std::move(phyManager) : nullptr,
        platformMapping,
        qsfpServiceThreads);

    if (numPortsPerModule == 4) {
      transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
          overrideMultiPortTcvrToPortAndProfile_);
    } else {
      transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
          overrideTcvrToPortAndProfile_);
    }
  }

  void initManagersWithMultiTcvrPort(bool setPhyManager = false) {
    const auto platformMapping = makeFakePlatformMapping(
        2, 4, FakeTestPlatformMappingType::DUAL_TRANSCEIVER);
    const std::shared_ptr<const PlatformMapping> castedPlatformMapping =
        platformMapping;

    // Create Threads Object
    const auto qsfpServiceThreads = makeQsfpServiceThreads(platformMapping);

    std::unique_ptr<MockPhyManager> phyManager =
        std::make_unique<MockPhyManager>(platformMapping.get());
    phyManager_ = phyManager.get();
    transceiverManager_ = std::make_shared<MockWedgeManager>(
        2, 4, platformMapping, qsfpServiceThreads);
    portManager_ = std::make_unique<MockPortManager>(
        transceiverManager_.get(),
        setPhyManager ? std::move(phyManager) : nullptr,
        platformMapping,
        qsfpServiceThreads);

    transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
        overrideMultiTransceiverTcvrToPortAndProfile_);
  }

  void initManagersWithBackplanePlatformMapping() {
    const auto platformMapping =
        makeFakePlatformMapping(1, 1, FakeTestPlatformMappingType::BACKPLANE);
    const std::shared_ptr<const PlatformMapping> castedPlatformMapping =
        platformMapping;

    // Create Threads Object
    const auto qsfpServiceThreads = makeQsfpServiceThreads(platformMapping);

    transceiverManager_ = std::make_shared<MockWedgeManager>(
        0, 0, platformMapping, qsfpServiceThreads);
    portManager_ = std::make_unique<MockPortManager>(
        transceiverManager_.get(),
        nullptr /* phyManager */,
        platformMapping,
        qsfpServiceThreads);
  }

  void initManagersWithXphyBackplanePlatformMapping() {
    const auto platformMapping = makeFakePlatformMapping(
        1, 1, FakeTestPlatformMappingType::XPHY_BACKPLANE);
    const std::shared_ptr<const PlatformMapping> castedPlatformMapping =
        platformMapping;

    // Create Threads Object
    const auto qsfpServiceThreads = makeQsfpServiceThreads(platformMapping);

    std::unique_ptr<MockPhyManager> phyManager =
        std::make_unique<MockPhyManager>(platformMapping.get());
    phyManager_ = phyManager.get();

    transceiverManager_ = std::make_shared<MockWedgeManager>(
        0, 1, platformMapping, qsfpServiceThreads);
    portManager_ = std::make_unique<MockPortManager>(
        transceiverManager_.get(),
        std::move(phyManager),
        platformMapping,
        qsfpServiceThreads);
  }

  void initManagersWithStandardPlatformMapping(bool setPhyManager = false) {
    const auto platformMapping =
        makeFakePlatformMapping(1, 8, FakeTestPlatformMappingType::STANDARD);
    const std::shared_ptr<const PlatformMapping> castedPlatformMapping =
        platformMapping;

    // Create Threads Object
    const auto qsfpServiceThreads = makeQsfpServiceThreads(platformMapping);

    std::unique_ptr<MockPhyManager> phyManager =
        std::make_unique<MockPhyManager>(platformMapping.get());
    phyManager_ = phyManager.get();

    transceiverManager_ = std::make_shared<MockWedgeManager>(
        1, 8, platformMapping, qsfpServiceThreads);
    portManager_ = std::make_unique<MockPortManager>(
        transceiverManager_.get(),
        setPhyManager ? std::move(phyManager) : nullptr,
        platformMapping,
        qsfpServiceThreads);

    transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
        overrideTcvrToPortAndProfile_);
  }

  void validatePortStatusNotInTestingOverride(PortID portId) {
    const auto& overrideStatusMap =
        portManager_->getOverrideAgentPortStatusForTesting();
    auto it = overrideStatusMap.find(portId);
    ASSERT_EQ(it, overrideStatusMap.end())
        << "Port ID " << portId << " found in override status map";
  }
  void validatePortStatusInTestingOverride(
      PortID portId,
      bool expectedUp,
      bool expectedEnabled) {
    const auto& overrideStatusMap =
        portManager_->getOverrideAgentPortStatusForTesting();
    auto it = overrideStatusMap.find(portId);
    ASSERT_NE(it, overrideStatusMap.end())
        << "Port ID " << portId << " not found in override status map";
    const auto& portStatus = it->second;

    ASSERT_EQ(portStatus.portEnabled, expectedEnabled)
        << "Port enabled status mismatch for port " << portId;
    ASSERT_EQ(portStatus.operState, expectedUp)
        << "Port operational state mismatch for port " << portId;
  }

  void programInternalPhyPortsForTest() {
    // Set up transceiver override for testing
    transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
        overrideMultiPortTcvrToPortAndProfile_);

    // Test that programInternalPhyPorts uses the override for testing
    EXPECT_NO_THROW(portManager_->programInternalPhyPorts(TransceiverID(0)));
  }

  void setTransceiverEnabledStatusInCache(
      const std::vector<PortID>& portIds,
      TransceiverID transceiverId) {
    for (const auto& portId : portIds) {
      portManager_->setTransceiverEnabledStatusInCache(portId, transceiverId);
    }
  }

  const TransceiverID tcvrId_ = TransceiverID(0);
  const PortID portId1_ = PortID(1);
  const PortID portId3_ = PortID(3);

  const cfg::PortProfileID profile_ =
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_OPTICAL;
  const TransceiverManager::OverrideTcvrToPortAndProfile
      overrideTcvrToPortAndProfile_ = {
          {tcvrId_,
           {
               {portId1_, profile_},
           }}};

  const cfg::PortProfileID multiPortProfile_ =
      cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_OPTICAL;
  const TransceiverManager::OverrideTcvrToPortAndProfile
      overrideMultiPortTcvrToPortAndProfile_ = {
          {tcvrId_,
           {
               {portId1_, multiPortProfile_},
               {portId3_, multiPortProfile_},
           }}};

  const cfg::PortProfileID multiTcvrProfile_ =
      cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_COPPER;
  const TransceiverManager::OverrideTcvrToPortAndProfile
      overrideMultiTransceiverTcvrToPortAndProfile_ = {
          {tcvrId_,
           {
               {portId1_, multiTcvrProfile_},
           }}};

  // Keeping as raw pointer only for testing mocks.
  MockPhyManager* phyManager_{};
  std::shared_ptr<MockWedgeManager> transceiverManager_;
  std::unique_ptr<MockPortManager> portManager_;
};

TEST_F(
    PortManagerTest,
    getLowestIndexedInitializedPortForTransceiverPortGroup) {
  // Single Tcvr - Single Port
  // Initialized
  initManagers(1, 1);
  portManager_->setPortEnabledStatusInCache(PortID(1), true);
  ASSERT_EQ(
      portManager_->getLowestIndexedInitializedPortForTransceiverPortGroup(
          PortID(1)),
      PortID(1));

  // Uninitialized
  initManagers(1, 1);
  ASSERT_THROW(
      portManager_->getLowestIndexedInitializedPortForTransceiverPortGroup(
          PortID(1)),
      FbossError);

  // Single Tcvr – Multi Port
  initManagers(1, 4);
  portManager_->setPortEnabledStatusInCache(PortID(1), true);
  portManager_->setPortEnabledStatusInCache(PortID(3), true);
  ASSERT_EQ(
      portManager_->getLowestIndexedInitializedPortForTransceiverPortGroup(
          PortID(1)),
      PortID(1));
  ASSERT_EQ(
      portManager_->getLowestIndexedInitializedPortForTransceiverPortGroup(
          PortID(3)),
      PortID(1));

  // Uninitialized
  initManagers(1, 4);
  ASSERT_THROW(
      portManager_->getLowestIndexedInitializedPortForTransceiverPortGroup(
          PortID(1)),
      FbossError);
  ASSERT_THROW(
      portManager_->getLowestIndexedInitializedPortForTransceiverPortGroup(
          PortID(3)),
      FbossError);

  initManagers(1, 4);
  portManager_->setPortEnabledStatusInCache(PortID(3), true);
  // Not enabled, but this is the expected behavior anyways.
  ASSERT_EQ(
      portManager_->getLowestIndexedInitializedPortForTransceiverPortGroup(
          PortID(1)),
      PortID(3));
  ASSERT_EQ(
      portManager_->getLowestIndexedInitializedPortForTransceiverPortGroup(
          PortID(3)),
      PortID(3));

  // Multi Tcvr – Single Port
  initManagersWithMultiTcvrPort(false);
  portManager_->setPortEnabledStatusInCache(PortID(1), true);
  ASSERT_EQ(
      portManager_->getLowestIndexedInitializedPortForTransceiverPortGroup(
          PortID(1)),
      PortID(1));

  // Uninitialized
  initManagersWithMultiTcvrPort(false);
  ASSERT_THROW(
      portManager_->getLowestIndexedInitializedPortForTransceiverPortGroup(
          PortID(1)),
      FbossError);
}

TEST_F(PortManagerTest, getLowestIndexedStaticTransceiverForPort) {
  // Single Tcvr – Single Port
  initManagers(1, 1);
  ASSERT_EQ(
      portManager_->getLowestIndexedStaticTransceiverForPort(PortID(1)),
      std::optional<TransceiverID>(TransceiverID(0)));

  // Single Tcvr – Multi Port
  initManagers(1, 4);
  portManager_->setPortEnabledStatusInCache(PortID(1), true);
  portManager_->setPortEnabledStatusInCache(PortID(3), true);
  ASSERT_EQ(
      portManager_->getLowestIndexedStaticTransceiverForPort(PortID(1)),
      std::optional<TransceiverID>(TransceiverID(0)));
  ASSERT_EQ(
      portManager_->getLowestIndexedStaticTransceiverForPort(PortID(3)),
      std::optional<TransceiverID>(TransceiverID(0)));

  // Multi Tcvr – Single Port
  initManagersWithMultiTcvrPort(false);
  ASSERT_EQ(
      portManager_->getLowestIndexedStaticTransceiverForPort(PortID(1)),
      std::optional<TransceiverID>(TransceiverID(0)));
  // PortID(5) is the controlling port of second transceiver subsumed by
  // PortID(1), but since this helper function is just checking static port
  // assignment, we can test it here.
  ASSERT_EQ(
      portManager_->getLowestIndexedStaticTransceiverForPort(PortID(5)),
      std::optional<TransceiverID>(TransceiverID(1)));
}

TEST_F(PortManagerTest, getStaticTransceiversForPort) {
  // Single Tcvr – Single Port
  initManagers(1, 1);
  ASSERT_EQ(
      portManager_->getStaticTransceiversForPort(PortID(1)),
      std::vector<TransceiverID>{TransceiverID(0)});

  // Single Tcvr – Multi Port
  initManagers(1, 4);
  ASSERT_EQ(
      portManager_->getStaticTransceiversForPort(PortID(1)),
      std::vector<TransceiverID>{TransceiverID(0)});
  ASSERT_EQ(
      portManager_->getStaticTransceiversForPort(PortID(3)),
      std::vector<TransceiverID>{TransceiverID(0)});

  // Multi Tcvr – Single Port
  initManagersWithMultiTcvrPort(false);
  ASSERT_EQ(
      portManager_->getStaticTransceiversForPort(PortID(1)),
      (std::vector<TransceiverID>{TransceiverID(0), TransceiverID(1)}));
  // PortID(5) is the controlling port of second transceiver subsumed by
  // PortID(1), but since this helper function is just checking static port
  // assignment, we can test it here.
  ASSERT_EQ(
      portManager_->getStaticTransceiversForPort(PortID(5)),
      std::vector<TransceiverID>{TransceiverID(1)});
}

TEST_F(PortManagerTest, isLowestIndexedInitializedPortForTransceiverPortGroup) {
  // Single Tcvr - Single Port
  // Initialized
  initManagers(1, 1);
  portManager_->setPortEnabledStatusInCache(PortID(1), true);
  ASSERT_TRUE(
      portManager_->isLowestIndexedInitializedPortForTransceiverPortGroup(
          PortID(1)));
  // Unitialized
  initManagers(1, 1);
  ASSERT_THROW(
      portManager_->isLowestIndexedInitializedPortForTransceiverPortGroup(
          PortID(1)),
      FbossError);

  // Single Tcvr – Multi Port
  // Initialized
  initManagers(1, 4);
  portManager_->setPortEnabledStatusInCache(PortID(1), true);
  portManager_->setPortEnabledStatusInCache(PortID(3), true);
  ASSERT_TRUE(
      portManager_->isLowestIndexedInitializedPortForTransceiverPortGroup(
          PortID(1)));
  ASSERT_FALSE(
      portManager_->isLowestIndexedInitializedPortForTransceiverPortGroup(
          PortID(3)));

  // Unitialized
  initManagers(1, 4);
  ASSERT_THROW(
      portManager_->isLowestIndexedInitializedPortForTransceiverPortGroup(
          PortID(1)),
      FbossError);
  ASSERT_THROW(
      portManager_->isLowestIndexedInitializedPortForTransceiverPortGroup(
          PortID(3)),
      FbossError);

  initManagers(1, 4);
  portManager_->setPortEnabledStatusInCache(PortID(3), true);
  // Not enabled, but this is the expected behavior anyways.
  ASSERT_FALSE(
      portManager_->isLowestIndexedInitializedPortForTransceiverPortGroup(
          PortID(1)));
  ASSERT_TRUE(
      portManager_->isLowestIndexedInitializedPortForTransceiverPortGroup(
          PortID(3)));

  // Multi Tcvr – Single Port (will be added once we validate other use cases)
  initManagersWithMultiTcvrPort(false);
  portManager_->setPortEnabledStatusInCache(PortID(1), true);
  ASSERT_TRUE(
      portManager_->isLowestIndexedInitializedPortForTransceiverPortGroup(
          PortID(1)));
}

TEST_F(PortManagerTest, getInitializedTransceiverIdsForPort) {
  // Single Tcvr – Single Port
  initManagers(1, 1);
  setTransceiverEnabledStatusInCache({PortID(1)}, TransceiverID(0));
  ASSERT_THAT(
      portManager_->getInitializedTransceiverIdsForPort(PortID(1)),
      ::testing::ElementsAre(TransceiverID(0)));

  // Single Tcvr – Multi Port
  initManagers(1, 4);
  setTransceiverEnabledStatusInCache(
      {PortID(1), PortID(2), PortID(3), PortID(4)}, TransceiverID(0));

  ASSERT_THAT(
      portManager_->getInitializedTransceiverIdsForPort(PortID(1)),
      ::testing::ElementsAre(TransceiverID(0)));
  ASSERT_THAT(
      portManager_->getInitializedTransceiverIdsForPort(PortID(3)),
      ::testing::ElementsAre(TransceiverID(0)));

  // Multi Tcvr – Single Port (will be added once we validate other use cases)
  initManagersWithMultiTcvrPort(false);
  setTransceiverEnabledStatusInCache({PortID(1)}, TransceiverID(0));
  setTransceiverEnabledStatusInCache({PortID(1)}, TransceiverID(1));
  ASSERT_THAT(
      portManager_->getInitializedTransceiverIdsForPort(PortID(1)),
      ::testing::ElementsAre(TransceiverID(0), TransceiverID(1)));
}

TEST_F(PortManagerTest, transceiverManagerKnowsPortManagerOwnsPhyManager) {
  initManagers(1, 4, true /* setPhyManager */);
  EXPECT_EQ(transceiverManager_->getPhyManager(), nullptr);
  EXPECT_TRUE(transceiverManager_->hasExternalPhy());
}

TEST_F(PortManagerTest, transceiverManagerHasNoExternalPhyWithoutPhyManager) {
  initManagers(1, 4, false /* setPhyManager */);
  EXPECT_FALSE(transceiverManager_->hasExternalPhy());
}

TEST_F(PortManagerTest, setBothPortsEnabledAndUpForTesting) {
  initManagers(1, 4);
  portManager_->setOverrideAgentPortStatusForTesting(
      {PortID(1), PortID(3)}, {PortID(1), PortID(3)});

  validatePortStatusInTestingOverride(PortID(1), true, true);
  validatePortStatusInTestingOverride(PortID(3), true, true);
}

TEST_F(PortManagerTest, setBothPortsDisabledAndDownForTesting) {
  initManagers(1, 4);
  portManager_->setOverrideAgentPortStatusForTesting({}, {});

  validatePortStatusInTestingOverride(PortID(1), false, false);
  validatePortStatusInTestingOverride(PortID(3), false, false);
}

TEST_F(PortManagerTest, setPortsMixedStatusForTesting) {
  initManagers(1, 4);
  portManager_->setOverrideAgentPortStatusForTesting({PortID(1)}, {PortID(3)});

  validatePortStatusInTestingOverride(PortID(1), true, false);
  validatePortStatusInTestingOverride(PortID(3), false, true);
}

TEST_F(PortManagerTest, clearOverrideAgentPortStatusForTesting) {
  // needs to be modified to account for clear case
  initManagers(1, 4);
  transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
      overrideMultiPortTcvrToPortAndProfile_);
  portManager_->setOverrideAgentPortStatusForTesting(
      {PortID(1), PortID(3)}, {PortID(1), PortID(3)});

  validatePortStatusInTestingOverride(PortID(1), true, true);
  validatePortStatusInTestingOverride(PortID(3), true, true);

  portManager_->setOverrideAgentPortStatusForTesting({}, {}, true);

  validatePortStatusNotInTestingOverride(PortID(1));
  validatePortStatusNotInTestingOverride(PortID(3));
}

TEST_F(PortManagerTest, getCachedXphyPorts) {
  initManagers(1, 1, true);
  ASSERT_THAT(
      portManager_->getCachedXphyPortsForTest(),
      ::testing::UnorderedElementsAre(PortID(1)));
}

TEST_F(PortManagerTest, getPortIDByPortName) {
  // Test valid port name returns correct PortID
  auto portId = portManager_->getPortIDByPortName("eth1/1/1");
  ASSERT_TRUE(portId.has_value());
  ASSERT_EQ(*portId, PortID(1));

  // Test invalid port name returns nullopt
  ASSERT_FALSE(portManager_->getPortIDByPortName("invalid_port").has_value());
}

TEST_F(PortManagerTest, getPortIDByPortNameOrThrow) {
  // Test valid port name returns correct PortID
  ASSERT_EQ(portManager_->getPortIDByPortNameOrThrow("eth1/1/1"), PortID(1));

  // Test invalid port name throws FbossError
  ASSERT_THROW(
      portManager_->getPortIDByPortNameOrThrow("invalid_port"), FbossError);
}

TEST_F(PortManagerTest, getPortNameByPortId) {
  // Test valid port ID returns correct port name
  ASSERT_EQ(portManager_->getPortNameByPortId(PortID(1)), "eth1/1/1");
  ASSERT_EQ(portManager_->getPortNameByPortId(PortID(3)), "eth1/1/3");

  // Test invalid port ID returns nullopt
  ASSERT_EQ(portManager_->getPortNameByPortId(PortID(999)), std::nullopt);
  ASSERT_THROW(
      portManager_->getPortNameByPortIdOrThrow(PortID(999)), FbossError);
}

TEST_F(PortManagerTest, portNameIdMappingConsistency) {
  // Test round-trip consistency for multiple ports
  for (const auto& portId : {PortID(1), PortID(3)}) {
    auto portNameStr = portManager_->getPortNameByPortIdOrThrow(portId);
    ASSERT_EQ(portManager_->getPortIDByPortNameOrThrow(portNameStr), portId);

    auto optionalPortId = portManager_->getPortIDByPortName(portNameStr);
    ASSERT_TRUE(optionalPortId.has_value());
    ASSERT_EQ(*optionalPortId, portId);
  }
}

TEST_F(PortManagerTest, testSetupPortNameToPortIDMapWithNullPlatformMapping) {
  // Attempt to create PortManager with null platform mapping
  ASSERT_THROW(
      std::make_unique<PortManager>(
          transceiverManager_.get(),
          nullptr /* phyManager */,
          nullptr /* platformMapping */,
          nullptr /* threads */),
      FbossError);
}

TEST_F(PortManagerTest, testGetPortNameByPortIdOrThrowWithInvalidId) {
  // Initialize with default settings
  initManagers(4);

  // Try to get port name for an invalid port ID
  // This should throw since the port ID doesn't exist
  ASSERT_THROW(
      portManager_->getPortNameByPortIdOrThrow(PortID(999)), FbossError)
      << "Should throw FbossError for invalid port ID";

  // Verify valid port IDs still work
  ASSERT_NO_THROW(portManager_->getPortNameByPortIdOrThrow(PortID(1)));
  ASSERT_NO_THROW(portManager_->getPortNameByPortIdOrThrow(PortID(3)));
}

ACTION(ThrowFbossError) {
  throw FbossError("Mock FbossError");
}
TEST_F(PortManagerTest, getInterfacePhyInfo) {
  initManagers(16, 8);
  std::map<std::string, phy::PhyInfo> phyInfos;
  // Test a valid interface
  portManager_->getInterfacePhyInfo(phyInfos, "eth1/1/1");
  EXPECT_TRUE(phyInfos.contains("eth1/1/1"));
  // Simulate an exception from xphyInfo and confirm that the map doesn't
  // include that interface's key
  EXPECT_CALL(*portManager_, getXphyInfo(PortID(9)))
      .Times(1)
      .WillRepeatedly(ThrowFbossError());
  portManager_->getInterfacePhyInfo(phyInfos, "eth1/2/1");
  EXPECT_EQ(phyInfos.size(), 1);
  // We still expect the previous interface to exist in the map
  EXPECT_TRUE(phyInfos.contains("eth1/1/1"));

  // Test an invalid interface
  EXPECT_THROW(
      portManager_->getInterfacePhyInfo(phyInfos, "no_such_interface"),
      FbossError);
}

TEST_F(PortManagerTest, programInternalPhyPorts) {
  initManagers(1, 4, false /* setPhyManager */);
  programInternalPhyPortsForTest();

  // Verify that the programmed port info is updated
  auto portToPortInfoPtr =
      transceiverManager_->getSynchronizedProgrammedIphyPortToPortInfo(
          TransceiverID(0));
  ASSERT_NE(portToPortInfoPtr, nullptr);

  auto lockedPortInfo = portToPortInfoPtr->rlock();
  EXPECT_EQ(lockedPortInfo->size(), 2); // Two ports in override

  auto port1Info = lockedPortInfo->find(PortID(1));
  ASSERT_NE(port1Info, lockedPortInfo->end());
  EXPECT_EQ(
      port1Info->second.profile,
      cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_OPTICAL);

  auto port3Info = lockedPortInfo->find(PortID(3));
  ASSERT_NE(port3Info, lockedPortInfo->end());
  EXPECT_EQ(
      port3Info->second.profile,
      cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_OPTICAL);

  initManagersWithMultiTcvrPort(false /* setPhyManager */);
  transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
      overrideMultiTransceiverTcvrToPortAndProfile_);
  portManager_->programInternalPhyPorts(TransceiverID(0));

  // Verify that the programmed port info is updated (we assume that agent
  // profiles have been mapped to tcvr profiles)
  for (auto [tcvrId, portId] :
       {std::make_pair(TransceiverID(0), PortID(1)),
        std::make_pair(TransceiverID(1), PortID(5))}) {
    XLOG(ERR) << "Checking for tcvrId: " << tcvrId << " portId: " << portId;
    auto newPortToPortInfoPtr =
        transceiverManager_->getSynchronizedProgrammedIphyPortToPortInfo(
            tcvrId);
    ASSERT_NE(newPortToPortInfoPtr, nullptr);

    auto newLockedPortInfo = newPortToPortInfoPtr->rlock();
    ASSERT_EQ(newLockedPortInfo->size(), 1); // One ports in override

    auto portInfo = newLockedPortInfo->find(portId);
    ASSERT_NE(portInfo, newLockedPortInfo->end());
    XLOG(ERR) << apache::thrift::util::enumNameSafe(portInfo->second.profile);
    ASSERT_EQ(
        portInfo->second.profile,
        cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_COPPER);
  }
}

TEST_F(PortManagerTest, programXphyPort) {
  initManagers(1, 4, true /* setPhyManager */);
  setTransceiverEnabledStatusInCache({PortID(1), PortID(3)}, TransceiverID(0));

  // Test successful programming with valid transceiver info
  EXPECT_CALL(
      *phyManager_,
      programOnePort(
          PortID(1),
          cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_OPTICAL,
          ::testing::_,
          false))
      .Times(1);

  EXPECT_NO_THROW(portManager_->programXphyPort(
      PortID(1), cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_OPTICAL));
  // We throw if port is not found.
  EXPECT_THROW(
      portManager_->programXphyPort(
          PortID(1000),
          cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_OPTICAL),
      FbossError);
}

TEST_F(PortManagerTest, programXphyPortNoPhyManager) {
  initManagers(1, 4, false /* setPhyManager */);
  setTransceiverEnabledStatusInCache({PortID(1)}, TransceiverID(0));

  // Test that FbossError is thrown when PhyManager is not set
  EXPECT_THROW(
      portManager_->programXphyPort(
          PortID(1), cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_OPTICAL),
      FbossError);
}

TEST_F(PortManagerTest, programExternalPhyPort) {
  initManagers(1, 4, true /* setPhyManager */);
  programInternalPhyPortsForTest();

  // Add ports to the initialized ports cache - this is normally done by the
  // state machine during port initialization
  portManager_->setPortEnabledStatusInCache(PortID(1), true);
  portManager_->setPortEnabledStatusInCache(PortID(3), true);

  // programExternalPhyPort should program a single port at a time
  // Based on overrideMultiPortTcvrToPortAndProfile_, TransceiverID(0) has ports
  // 1 and 3
  EXPECT_CALL(
      *phyManager_,
      programOnePort(
          PortID(1),
          cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_OPTICAL,
          ::testing::_,
          false))
      .Times(1);

  EXPECT_NO_THROW(
      portManager_->programExternalPhyPort(PortID(1), TransceiverID(0), false));

  EXPECT_CALL(
      *phyManager_,
      programOnePort(
          PortID(3),
          cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_OPTICAL,
          ::testing::_,
          false))
      .Times(1);

  EXPECT_NO_THROW(
      portManager_->programExternalPhyPort(PortID(3), TransceiverID(0), false));

  // We don't throw if the port is not an XPHY port.
  EXPECT_NO_THROW(portManager_->programExternalPhyPort(
      PortID(1000), TransceiverID(1000), false));
}

TEST_F(PortManagerTest, programExternalPhyPortNoPhyManager) {
  // Test the early return when phyManager_ is null
  initManagers(1, 4, false /* setPhyManager */);

  // Should return early without throwing when phyManager_ is null
  EXPECT_NO_THROW(
      portManager_->programExternalPhyPort(PortID(1), TransceiverID(0), false));
}

TEST_F(PortManagerTest, initAndExit) {
  initManagers();
  transceiverManager_->init();
  portManager_->init();
}

TEST_F(PortManagerTest, initAndExitGracefully) {
  initManagers();
  transceiverManager_->init();
  portManager_->init();

  portManager_->gracefulExit();
  transceiverManager_->gracefulExit();
}

TEST_F(PortManagerTest, tcvrToInitializedPortsCacheValidation) {
  initManagers(2, 4); // 2 transceivers, 4 ports each

  // Initially, the cache should be empty for all transceivers
  EXPECT_TRUE(portManager_->hasTransceiverInCache(TransceiverID(0)));
  EXPECT_TRUE(portManager_->hasTransceiverInCache(TransceiverID(1)));
  EXPECT_EQ(
      portManager_->getInitializedPortsForTransceiver(TransceiverID(0)).size(),
      0);
  EXPECT_EQ(
      portManager_->getInitializedPortsForTransceiver(TransceiverID(1)).size(),
      0);

  // Test Case 1: Enable some ports for transceivers
  portManager_->setPortEnabledStatusInCache(PortID(1), true);
  portManager_->setPortEnabledStatusInCache(PortID(2), true);
  portManager_->setPortEnabledStatusInCache(PortID(5), true);

  // Verify cache updates
  auto tcvr0Ports =
      portManager_->getInitializedPortsForTransceiver(TransceiverID(0));
  auto tcvr1Ports =
      portManager_->getInitializedPortsForTransceiver(TransceiverID(1));

  EXPECT_EQ(tcvr0Ports.size(), 2);
  EXPECT_TRUE(tcvr0Ports.count(PortID(1)));
  EXPECT_TRUE(tcvr0Ports.count(PortID(2)));

  EXPECT_EQ(tcvr1Ports.size(), 1);
  EXPECT_TRUE(tcvr1Ports.count(PortID(5)));

  // Test Case 2: Disable some ports
  portManager_->setPortEnabledStatusInCache(PortID(1), false);
  portManager_->setPortEnabledStatusInCache(PortID(6), true);

  // Verify cache updates after disabling
  tcvr0Ports =
      portManager_->getInitializedPortsForTransceiver(TransceiverID(0));
  tcvr1Ports =
      portManager_->getInitializedPortsForTransceiver(TransceiverID(1));

  EXPECT_EQ(tcvr0Ports.size(), 1);
  EXPECT_FALSE(tcvr0Ports.count(PortID(1))); // Should be removed
  EXPECT_TRUE(tcvr0Ports.count(PortID(2))); // Should still be there

  EXPECT_EQ(tcvr1Ports.size(), 2);
  EXPECT_TRUE(tcvr1Ports.count(PortID(5))); // Should still be there
  EXPECT_TRUE(tcvr1Ports.count(PortID(6))); // Should be added

  // Test Case 3: Test getTransceiversWithAllPortsInSet function
  std::unordered_set<PortID> testSet1 = {PortID(2), PortID(5), PortID(6)};
  auto transceivers1 = portManager_->getTransceiversWithAllPortsInSet(testSet1);

  // TransceiverID(0) has only port 2, which is in testSet1 → should be included
  // TransceiverID(1) has ports 5,6, both are in testSet1 → should be included
  EXPECT_EQ(transceivers1.size(), 2);
  EXPECT_TRUE(transceivers1.count(TransceiverID(0)));
  EXPECT_TRUE(transceivers1.count(TransceiverID(1)));

  // Test Case 4: Test with partial match
  std::unordered_set<PortID> testSet2 = {PortID(2), PortID(5)};
  auto transceivers2 = portManager_->getTransceiversWithAllPortsInSet(testSet2);

  // TransceiverID(0) has only port 2, which is in testSet2 → should be included
  // TransceiverID(1) has ports 5,6, but 6 is not in testSet2 → should NOT be
  // included
  EXPECT_EQ(transceivers2.size(), 1);
  EXPECT_TRUE(transceivers2.count(TransceiverID(0)));
  EXPECT_FALSE(transceivers2.count(TransceiverID(1)));

  // Test Case 5: Test with empty set
  std::unordered_set<PortID> emptySet;
  auto transceivers3 = portManager_->getTransceiversWithAllPortsInSet(emptySet);

  // No transceiver should match an empty set (since they all have some ports)
  EXPECT_EQ(transceivers3.size(), 0);

  // Test Case 6: Test with superset
  std::unordered_set<PortID> superSet = {
      PortID(1),
      PortID(2),
      PortID(3),
      PortID(4),
      PortID(5),
      PortID(6),
      PortID(7),
      PortID(8)};
  auto transceivers4 = portManager_->getTransceiversWithAllPortsInSet(superSet);

  // Both transceivers should match since all their ports are in the superset
  EXPECT_EQ(transceivers4.size(), 2);
  EXPECT_TRUE(transceivers4.count(TransceiverID(0)));
  EXPECT_TRUE(transceivers4.count(TransceiverID(1)));

  // Test Case 7: Clear all ports for one transceiver
  portManager_->setPortEnabledStatusInCache(PortID(2), false);

  tcvr0Ports =
      portManager_->getInitializedPortsForTransceiver(TransceiverID(0));
  EXPECT_EQ(tcvr0Ports.size(), 0);
}

TEST_F(PortManagerTest, tcvrToInitializedPortsCacheEdgeCases) {
  initManagers(1, 4); // 1 transceiver, 4 ports - valid configuration

  // Test Case 1: Enable and disable the same port multiple times
  portManager_->setPortEnabledStatusInCache(PortID(1), true);

  auto ports =
      portManager_->getInitializedPortsForTransceiver(TransceiverID(0));
  EXPECT_EQ(ports.size(), 1);
  EXPECT_TRUE(ports.count(PortID(1)));

  // Disable it
  portManager_->setPortEnabledStatusInCache(PortID(1), false);

  ports = portManager_->getInitializedPortsForTransceiver(TransceiverID(0));
  EXPECT_EQ(ports.size(), 0);

  // Enable it again
  portManager_->setPortEnabledStatusInCache(PortID(1), true);

  ports = portManager_->getInitializedPortsForTransceiver(TransceiverID(0));
  EXPECT_EQ(ports.size(), 1);
  EXPECT_TRUE(ports.count(PortID(1)));

  // Test Case 2: Test invalid transceiver ID handling
  // This should throw an exception since we only have TransceiverID(0) in our
  // setup
  EXPECT_THROW(
      portManager_->setPortEnabledStatusInCache(PortID(999), true), FbossError);

  // Test Case 3: Batch operations
  portManager_->setPortEnabledStatusInCache(PortID(1), true);
  portManager_->setPortEnabledStatusInCache(PortID(2), true);
  portManager_->setPortEnabledStatusInCache(PortID(1), false);

  ports = portManager_->getInitializedPortsForTransceiver(TransceiverID(0));
  EXPECT_EQ(ports.size(), 1);
  EXPECT_FALSE(ports.count(PortID(1))); // Should be false due to last operation
  EXPECT_TRUE(ports.count(PortID(2))); // Should be true
}

TEST_F(PortManagerTest, setOverrideAllAgentPortStatusForTesting) {
  initManagers(1, 4);

  // Test various combinations and verify they apply to all ports
  portManager_->setOverrideAllAgentPortStatusForTesting(true, true);
  validatePortStatusInTestingOverride(PortID(1), true, true);
  validatePortStatusInTestingOverride(PortID(3), true, true);

  portManager_->setOverrideAllAgentPortStatusForTesting(false, false);
  validatePortStatusInTestingOverride(PortID(1), false, false);
  validatePortStatusInTestingOverride(PortID(3), false, false);

  portManager_->setOverrideAllAgentPortStatusForTesting(false, true);
  validatePortStatusInTestingOverride(PortID(1), false, true);
  validatePortStatusInTestingOverride(PortID(3), false, true);

  // Test that it clears existing individual overrides
  portManager_->setOverrideAgentPortStatusForTesting({PortID(1)}, {PortID(3)});
  portManager_->setOverrideAllAgentPortStatusForTesting(true, true);
  validatePortStatusInTestingOverride(PortID(1), true, true);
  validatePortStatusInTestingOverride(PortID(3), true, true);
}

TEST_F(PortManagerTest, tcvrToInitializedPortsCacheConsistency) {
  initManagers(2, 4); // 2 transceivers, 4 ports each - valid configuration

  // Test that the cache correctly handles multiple transceivers
  portManager_->setPortEnabledStatusInCache(PortID(1), true);
  portManager_->setPortEnabledStatusInCache(PortID(2), true);
  portManager_->setPortEnabledStatusInCache(PortID(5), true);
  portManager_->setPortEnabledStatusInCache(PortID(6), true);

  // Verify each transceiver's cache independently
  auto tcvr0Ports =
      portManager_->getInitializedPortsForTransceiver(TransceiverID(0));
  auto tcvr1Ports =
      portManager_->getInitializedPortsForTransceiver(TransceiverID(1));

  EXPECT_EQ(tcvr0Ports.size(), 2);
  EXPECT_TRUE(tcvr0Ports.count(PortID(1)));
  EXPECT_TRUE(tcvr0Ports.count(PortID(2)));

  EXPECT_EQ(tcvr1Ports.size(), 2);
  EXPECT_TRUE(tcvr1Ports.count(PortID(5)));
  EXPECT_TRUE(tcvr1Ports.count(PortID(6)));

  // Test getTransceiversWithAllPortsInSet with various combinations
  std::unordered_set<PortID> testCombination1 = {PortID(1), PortID(2)};
  auto result1 =
      portManager_->getTransceiversWithAllPortsInSet(testCombination1);
  EXPECT_EQ(result1.size(), 1); // Only TransceiverID(0)
  EXPECT_TRUE(result1.count(TransceiverID(0)));
  EXPECT_FALSE(result1.count(TransceiverID(1)));

  std::unordered_set<PortID> testCombination2 = {PortID(5), PortID(6)};
  auto result2 =
      portManager_->getTransceiversWithAllPortsInSet(testCombination2);
  EXPECT_EQ(result2.size(), 1); // Only TransceiverID(1)
  EXPECT_TRUE(result2.count(TransceiverID(1)));
  EXPECT_FALSE(result2.count(TransceiverID(0)));

  // Test with a combination that matches all
  std::unordered_set<PortID> allPorts = {
      PortID(1), PortID(2), PortID(5), PortID(6)};
  auto resultAll = portManager_->getTransceiversWithAllPortsInSet(allPorts);
  EXPECT_EQ(resultAll.size(), 2); // Both transceivers
  EXPECT_TRUE(resultAll.count(TransceiverID(0)));
  EXPECT_TRUE(resultAll.count(TransceiverID(1)));
}

TEST_F(PortManagerTest, syncPorts) {
  initManagers(2, 4); // 2 transceivers, 4 ports each

  // Create a map of PortStatus objects
  auto ports = std::make_unique<std::map<int32_t, PortStatus>>();

  // Create PortStatus for port 1 with transceiver info
  PortStatus port1Status;
  port1Status.enabled() = true;
  port1Status.up() = true;
  port1Status.speedMbps() = 100000;
  port1Status.profileID() = "PROFILE_100G_2_PAM4_RS544X2N_OPTICAL";
  port1Status.drained() = false;

  TransceiverIdxThrift tcvrIdx1;
  tcvrIdx1.transceiverId() = 0;
  port1Status.transceiverIdx() = tcvrIdx1;

  // Create PortStatus for port 5 with different transceiver info
  PortStatus port5Status;
  port5Status.enabled() = true;
  port5Status.up() = false;
  port5Status.speedMbps() = 100000;
  port5Status.profileID() = "PROFILE_100G_2_PAM4_RS544X2N_OPTICAL";
  port5Status.drained() = false;

  TransceiverIdxThrift tcvrIdx2;
  tcvrIdx2.transceiverId() = 1;
  port5Status.transceiverIdx() = tcvrIdx2;

  // Create PortStatus for port without transceiver info (should be skipped)
  PortStatus port9Status;
  port9Status.enabled() = false;
  port9Status.up() = false;
  port9Status.speedMbps() = 0;
  port9Status.profileID() = "";
  port9Status.drained() = false;
  // No transceiverIdx set - this should be skipped in the loop

  (*ports)[1] = port1Status;
  (*ports)[5] = port5Status;
  (*ports)[9] = port9Status;

  // Call syncPorts
  std::map<int32_t, TransceiverInfo> info;
  EXPECT_NO_THROW(portManager_->syncPorts(info, std::move(ports)));

  // The info map should contain entries for transceivers that have valid info
  // The exact content depends on what the mock TransceiverManager returns
  // but we can at least verify the method doesn't crash and processes the input
}

TEST_F(PortManagerTest, getPortStates) {
  initManagers(2, 4); // 2 transceivers, 4 ports each

  // Create a vector of port IDs - mix of valid and invalid
  auto portIds = std::make_unique<std::vector<int32_t>>();
  portIds->push_back(1); // Valid port
  portIds->push_back(3); // Valid port
  portIds->push_back(999); // Invalid port - should be skipped

  // Create states map to be populated
  std::map<int32_t, PortStateMachineState> states;

  // Call getPortStates
  EXPECT_NO_THROW(portManager_->getPortStates(states, std::move(portIds)));

  // Verify that valid ports are in the map
  EXPECT_TRUE(states.contains(1));
  EXPECT_TRUE(states.contains(3));

  // Verify that invalid port is NOT in the map (it was caught and skipped)
  EXPECT_TRUE(!states.contains(999));

  // Verify we got the expected number of entries (only valid ports)
  EXPECT_EQ(states.size(), 2);
}

TEST_F(PortManagerTest, getNonControllingTransceiverIdForMultiTcvr) {
  // Test Case 1: Single Tcvr – Single Port
  // No multi-transceiver ports, so map should be empty
  initManagers(1, 1);
  portManager_->programInternalPhyPorts(TransceiverID(0));

  auto id = portManager_->getNonControllingTransceiverIdForMultiTcvr(
      TransceiverID(0));
  EXPECT_EQ(id, std::nullopt);

  // Test Case 2: Single Tcvr – Multi Port
  initManagers(1, 4);
  portManager_->programInternalPhyPorts(TransceiverID(0));

  id = portManager_->getNonControllingTransceiverIdForMultiTcvr(
      TransceiverID(0));
  EXPECT_EQ(id, std::nullopt);

  // Test Case 3: Multi Tcvr – Single Port
  // This should create a mapping from controlling to non-controlling
  // transceiver when PHY programming is triggered
  initManagersWithMultiTcvrPort(false);

  // The map is populated during programInternalPhyPorts, not just by
  // setTransceiverEnabledStatusInCache. We need to trigger the programming.
  // Since programInternalPhyPorts calls getDualTransceiverPortProfileIDs
  // which populates the map, we need to call it.
  portManager_->programInternalPhyPorts(TransceiverID(0));

  id = portManager_->getNonControllingTransceiverIdForMultiTcvr(
      TransceiverID(0));
  EXPECT_TRUE(id != std::nullopt);
  // TransceiverID(0) is the lowest, so it should map to TransceiverID(1)
  EXPECT_EQ(*id, TransceiverID(1));
}

TEST_F(PortManagerTest, setTransceiverEnabledStatusInCache) {
  initManagers(2, 4); // 2 transceivers, 4 ports each

  // Test Case 1: Successfully add a transceiver for a port
  portManager_->setTransceiverEnabledStatusInCache(PortID(1), TransceiverID(0));
  auto tcvrIds = portManager_->getInitializedTransceiverIdsForPort(PortID(1));
  EXPECT_THAT(tcvrIds, ::testing::ElementsAre(TransceiverID(0)));

  // Test Case 2: Add multiple transceivers for the same port
  portManager_->setTransceiverEnabledStatusInCache(PortID(1), TransceiverID(1));
  tcvrIds = portManager_->getInitializedTransceiverIdsForPort(PortID(1));
  EXPECT_EQ(tcvrIds.size(), 2);
  EXPECT_TRUE(
      std::find(tcvrIds.begin(), tcvrIds.end(), TransceiverID(0)) !=
      tcvrIds.end());
  EXPECT_TRUE(
      std::find(tcvrIds.begin(), tcvrIds.end(), TransceiverID(1)) !=
      tcvrIds.end());

  // Test Case 3: Adding the same transceiver again is idempotent (no throw)
  EXPECT_NO_THROW(portManager_->setTransceiverEnabledStatusInCache(
      PortID(1), TransceiverID(0)));
  // Verify cache size unchanged (no duplicate added)
  tcvrIds = portManager_->getInitializedTransceiverIdsForPort(PortID(1));
  EXPECT_EQ(tcvrIds.size(), 2);

  // Test Case 4: Try to add transceiver for invalid port (should throw)
  EXPECT_THROW(
      portManager_->setTransceiverEnabledStatusInCache(
          PortID(999), TransceiverID(0)),
      FbossError);

  // Test Case 5: Add transceivers for different ports
  portManager_->setTransceiverEnabledStatusInCache(PortID(5), TransceiverID(1));
  auto port5TcvrIds =
      portManager_->getInitializedTransceiverIdsForPort(PortID(5));
  EXPECT_THAT(port5TcvrIds, ::testing::ElementsAre(TransceiverID(1)));

  // Verify port 1 still has its transceivers
  tcvrIds = portManager_->getInitializedTransceiverIdsForPort(PortID(1));
  EXPECT_EQ(tcvrIds.size(), 2);
}

TEST_F(PortManagerTest, clearEnabledTransceiversForPort) {
  initManagers(2, 4); // 2 transceivers, 4 ports each

  // Test Case 1: Clear transceivers for port with multiple transceivers
  portManager_->setTransceiverEnabledStatusInCache(PortID(1), TransceiverID(0));
  portManager_->setTransceiverEnabledStatusInCache(PortID(1), TransceiverID(1));

  auto tcvrIds = portManager_->getInitializedTransceiverIdsForPort(PortID(1));
  EXPECT_EQ(tcvrIds.size(), 2);

  portManager_->clearEnabledTransceiversForPort(PortID(1));
  tcvrIds = portManager_->getInitializedTransceiverIdsForPort(PortID(1));
  EXPECT_EQ(tcvrIds.size(), 0);

  // Test Case 2: Clear transceivers for port with single transceiver
  portManager_->setTransceiverEnabledStatusInCache(PortID(2), TransceiverID(0));
  tcvrIds = portManager_->getInitializedTransceiverIdsForPort(PortID(2));
  EXPECT_EQ(tcvrIds.size(), 1);

  portManager_->clearEnabledTransceiversForPort(PortID(2));
  tcvrIds = portManager_->getInitializedTransceiverIdsForPort(PortID(2));
  EXPECT_EQ(tcvrIds.size(), 0);

  // Test Case 3: Clear transceivers for port that already has no transceivers
  // (should not throw)
  EXPECT_NO_THROW(portManager_->clearEnabledTransceiversForPort(PortID(3)));
  tcvrIds = portManager_->getInitializedTransceiverIdsForPort(PortID(3));
  EXPECT_EQ(tcvrIds.size(), 0);

  // Test Case 4: Clear and re-add transceivers
  portManager_->setTransceiverEnabledStatusInCache(PortID(4), TransceiverID(0));
  portManager_->clearEnabledTransceiversForPort(PortID(4));
  // Should be able to add again after clearing
  EXPECT_NO_THROW(portManager_->setTransceiverEnabledStatusInCache(
      PortID(4), TransceiverID(0)));
  tcvrIds = portManager_->getInitializedTransceiverIdsForPort(PortID(4));
  EXPECT_THAT(tcvrIds, ::testing::ElementsAre(TransceiverID(0)));

  // Test Case 5: Clear doesn't affect other ports
  portManager_->setTransceiverEnabledStatusInCache(PortID(5), TransceiverID(1));
  portManager_->setTransceiverEnabledStatusInCache(PortID(6), TransceiverID(1));

  portManager_->clearEnabledTransceiversForPort(PortID(5));

  auto port5TcvrIds =
      portManager_->getInitializedTransceiverIdsForPort(PortID(5));
  auto port6TcvrIds =
      portManager_->getInitializedTransceiverIdsForPort(PortID(6));

  EXPECT_EQ(port5TcvrIds.size(), 0);
  EXPECT_EQ(port6TcvrIds.size(), 1);
  EXPECT_THAT(port6TcvrIds, ::testing::ElementsAre(TransceiverID(1)));
}

TEST_F(PortManagerTest, getTransceiversWithAllPortsInSet) {
  // Test Case 1: Single Tcvr - Single Port
  auto validateSingleTcvrSinglePort = [this]() {
    initManagers(1, 1);
    portManager_->programInternalPhyPorts(TransceiverID(0));
    portManager_->setPortEnabledStatusInCache(PortID(1), true);

    // Port is in set - should return the transceiver
    std::unordered_set<PortID> testSet1 = {PortID(1)};
    auto transceivers1 =
        portManager_->getTransceiversWithAllPortsInSet(testSet1);
    EXPECT_EQ(transceivers1.size(), 1);
    EXPECT_TRUE(transceivers1.count(TransceiverID(0)));

    // Port is not in set - should not return the transceiver
    std::unordered_set<PortID> testSet2 = {PortID(2)};
    auto transceivers2 =
        portManager_->getTransceiversWithAllPortsInSet(testSet2);
    EXPECT_EQ(transceivers2.size(), 0);
  };
  validateSingleTcvrSinglePort();

  // Test Case 2: Single Tcvr - Multi Port
  auto validateSingleTcvrMultiPort = [this]() {
    initManagers(1, 4);
    portManager_->programInternalPhyPorts(TransceiverID(0));
    portManager_->setPortEnabledStatusInCache(PortID(1), true);
    portManager_->setPortEnabledStatusInCache(PortID(3), true);

    // All ports are in set - should return the transceiver
    std::unordered_set<PortID> testSet3 = {PortID(1), PortID(3)};
    auto transceivers3 =
        portManager_->getTransceiversWithAllPortsInSet(testSet3);
    EXPECT_EQ(transceivers3.size(), 1);
    EXPECT_TRUE(transceivers3.count(TransceiverID(0)));

    // Only one port in set - should not return the transceiver
    std::unordered_set<PortID> testSet4 = {PortID(1)};
    auto transceivers4 =
        portManager_->getTransceiversWithAllPortsInSet(testSet4);
    EXPECT_EQ(transceivers4.size(), 0);

    // Superset of ports - should still return the transceiver
    std::unordered_set<PortID> testSet5 = {
        PortID(1), PortID(2), PortID(3), PortID(4)};
    auto transceivers5 =
        portManager_->getTransceiversWithAllPortsInSet(testSet5);
    EXPECT_EQ(transceivers5.size(), 1);
    EXPECT_TRUE(transceivers5.count(TransceiverID(0)));
  };
  validateSingleTcvrMultiPort();

  // Test Case 3: Multi Tcvr - Single Port
  auto validateMultiTcvrSinglePort = [this]() {
    initManagersWithMultiTcvrPort(false);
    portManager_->programInternalPhyPorts(TransceiverID(0));
    portManager_->setPortEnabledStatusInCache(PortID(1), true);

    std::unordered_set<PortID> testSet6 = {PortID(1)};
    auto transceivers6 =
        portManager_->getTransceiversWithAllPortsInSet(testSet6);
    EXPECT_EQ(transceivers6.size(), 2);
    EXPECT_TRUE(transceivers6.count(TransceiverID(0)));
    EXPECT_TRUE(transceivers6.count(TransceiverID(1)));

    std::unordered_set<PortID> testSet7 = {PortID(5)};
    auto transceivers7 =
        portManager_->getTransceiversWithAllPortsInSet(testSet7);
    EXPECT_EQ(transceivers7.size(), 0);
  };
  validateMultiTcvrSinglePort();

  // Test Case 4: Empty set
  initManagers(1, 4);
  portManager_->setPortEnabledStatusInCache(PortID(1), true);

  std::unordered_set<PortID> emptySet;
  auto transceivers7 = portManager_->getTransceiversWithAllPortsInSet(emptySet);
  EXPECT_EQ(transceivers7.size(), 0);

  // Test Case 5: Multiple transceivers with different port sets
  auto validateMultiTcvrPlatformMappingInSingleTcvrMode =
      [this](bool setTransceiverCache) {
        initManagersWithMultiTcvrPort(false);
        portManager_->setPortEnabledStatusInCache(PortID(1), true);
        portManager_->setPortEnabledStatusInCache(PortID(3), true);
        portManager_->setPortEnabledStatusInCache(PortID(5), true);
        if (setTransceiverCache) {
          setTransceiverEnabledStatusInCache({PortID(1)}, TransceiverID(0));
          setTransceiverEnabledStatusInCache({PortID(3)}, TransceiverID(0));
          setTransceiverEnabledStatusInCache({PortID(5)}, TransceiverID(1));
        }

        // Set contains all ports for both transceivers
        std::unordered_set<PortID> testSet8 = {PortID(1), PortID(3), PortID(5)};
        auto transceivers8 =
            portManager_->getTransceiversWithAllPortsInSet(testSet8);
        EXPECT_EQ(transceivers8.size(), 2);
        EXPECT_TRUE(transceivers8.count(TransceiverID(0)));
        EXPECT_TRUE(transceivers8.count(TransceiverID(1)));

        std::unordered_set<PortID> testSet9 = {PortID(3), PortID(5)};
        auto transceivers9 =
            portManager_->getTransceiversWithAllPortsInSet(testSet9);
        EXPECT_EQ(transceivers9.size(), 1);
        EXPECT_TRUE(transceivers9.count(TransceiverID(1)));
      };
  validateMultiTcvrPlatformMappingInSingleTcvrMode(true);
  validateMultiTcvrPlatformMappingInSingleTcvrMode(false);

  // Test Case 6: No enabled ports for transceiver
  initManagers(1, 4);
  std::unordered_set<PortID> testSet10 = {PortID(1), PortID(3)};
  auto transceivers10 =
      portManager_->getTransceiversWithAllPortsInSet(testSet10);
  EXPECT_EQ(transceivers10.size(), 0);
}

TEST_F(PortManagerTest, getPerTransceiverProfile) {
  initManagers(1, 4);

  // Single Transceiver
  EXPECT_EQ(
      portManager_->getPerTransceiverProfile(
          1, cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_COPPER),
      cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_COPPER);
  EXPECT_EQ(
      portManager_->getPerTransceiverProfile(
          1, cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_COPPER),
      cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_COPPER);

  // Two transceivers
  EXPECT_EQ(
      portManager_->getPerTransceiverProfile(
          2, cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_COPPER),
      cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_COPPER);
  EXPECT_THROW(
      portManager_->getPerTransceiverProfile(
          2, cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528),
      FbossError);

  // >2 transceivers
  EXPECT_THROW(
      portManager_->getPerTransceiverProfile(
          3, cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_COPPER),
      FbossError);
  EXPECT_THROW(
      portManager_->getPerTransceiverProfile(
          4, cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528),
      FbossError);
}

TEST_F(PortManagerTest, getMultiTransceiverPortProfileIDs) {
  initManagers(1, 4);

  // Single Transceiver
  // When multiple ports are provided, should return input as-is
  initManagers(1, 4);
  std::map<int32_t, cfg::PortProfileID> multiPortInput = {
      {1, multiPortProfile_}, {3, multiPortProfile_}};
  auto result1 =
      portManager_->getMultiTransceiverPortProfileIDs(tcvrId_, multiPortInput);

  EXPECT_EQ(result1.size(), 1);
  EXPECT_TRUE(result1.contains(tcvrId_));
  EXPECT_EQ(result1[tcvrId_].size(), 2);
  EXPECT_EQ(result1[tcvrId_][1], multiPortProfile_);
  EXPECT_EQ(result1[tcvrId_][3], multiPortProfile_);

  // Test Case 2: Single Tcvr – Single Port
  // When single port with single transceiver chip, should return input as-is
  initManagers(1, 1);
  std::map<int32_t, cfg::PortProfileID> singlePortInput = {
      {1, multiPortProfile_}};
  auto result2 =
      portManager_->getMultiTransceiverPortProfileIDs(tcvrId_, singlePortInput);

  EXPECT_EQ(result2.size(), 1);
  EXPECT_TRUE(result2.contains(tcvrId_));
  EXPECT_EQ(result2[tcvrId_].size(), 1);
  EXPECT_EQ(result2[tcvrId_][1], multiPortProfile_);

  // Test Case 3: Multi Tcvr – Single Port
  // When single port spans multiple transceivers, should map to per-transceiver
  // profiles
  initManagersWithMultiTcvrPort(false);
  std::map<int32_t, cfg::PortProfileID> dualTcvrInput = {
      {1, multiTcvrProfile_}};
  auto result3 =
      portManager_->getMultiTransceiverPortProfileIDs(tcvrId_, dualTcvrInput);

  // Should split into two transceivers with per-transceiver profiles
  EXPECT_EQ(result3.size(), 2);
  EXPECT_TRUE(result3.contains(TransceiverID(0)));
  EXPECT_TRUE(result3.contains(TransceiverID(1)));

  // Each transceiver should have one port mapped to the per-transceiver
  // profile
  EXPECT_EQ(result3[TransceiverID(0)].size(), 1);
  EXPECT_EQ(result3[TransceiverID(1)].size(), 1);
  EXPECT_EQ(
      result3[TransceiverID(0)][1],
      cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_COPPER);
  EXPECT_EQ(
      result3[TransceiverID(1)][5],
      cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_COPPER);

  // Test Case 4: Multi Tcvr Port with Single Tcvr Profile
  // When in multi-tcvr port mode but using a profile that only uses one
  // transceiver chip, should return input as-is
  std::map<int32_t, cfg::PortProfileID> singleTcvrProfileInput = {
      {1, cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_COPPER}};
  auto result4 = portManager_->getMultiTransceiverPortProfileIDs(
      tcvrId_, singleTcvrProfileInput);

  EXPECT_EQ(result4.size(), 1);
  EXPECT_TRUE(result4.contains(tcvrId_));
  EXPECT_EQ(result4[tcvrId_].size(), 1);
  EXPECT_EQ(
      result4[tcvrId_][1],
      cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_COPPER);
}

TEST_F(PortManagerTest, portHasTransceiver) {
  // Test Case 1: Single Tcvr – Single Port
  // Port should have a transceiver
  initManagers(1, 1);
  EXPECT_TRUE(portManager_->portHasTransceiver(PortID(1)));

  // Test Case 2: Single Tcvr – Multi Port
  // All ports should have a transceiver
  initManagers(1, 4);
  EXPECT_TRUE(portManager_->portHasTransceiver(PortID(1)));
  EXPECT_TRUE(portManager_->portHasTransceiver(PortID(3)));

  // Test Case 3: Multi Tcvr – Single Port
  // Port should have a transceiver
  initManagersWithMultiTcvrPort(false);
  EXPECT_TRUE(portManager_->portHasTransceiver(PortID(1)));
  EXPECT_TRUE(portManager_->portHasTransceiver(PortID(5)));
}

TEST_F(PortManagerTest, noPortsCreatedForBackplanePlatformMapping) {
  // When platform mapping is BACKPLANE, no transceiver or XPHY ports should be
  // created since backplane ports don't have actual transceivers or XPHYs.
  // This is verified by checking that no port state machines are created.
  initManagersWithBackplanePlatformMapping();
  EXPECT_EQ(portManager_->getStateMachineCountForTest(), 0);
}

TEST_F(PortManagerTest, portsCreatedForXphyBackplanePlatformMapping) {
  // When platform mapping is XPHY_BACKPLANE, 1 port should be created since
  // XPHY_BACKPLANE has XPHY (but no transceiver). This port connects through
  // NPU -> XPHY -> BACKPLANE path.
  initManagersWithXphyBackplanePlatformMapping();
  EXPECT_EQ(portManager_->getStateMachineCountForTest(), 1);
}

TEST_F(PortManagerTest, portsCreatedForStandardPlatformMapping) {
  // When platform mapping is STANDARD with 1 transceiver and 8 ports per
  // module, 8 port state machines should be created since each port connects
  // through NPU -> XPHY -> Transceiver path.
  initManagersWithStandardPlatformMapping(true /* setPhyManager */);
  EXPECT_EQ(portManager_->getStateMachineCountForTest(), 8);
}

TEST_F(PortManagerTest, portsCreatedForDualTransceiverPlatformMapping) {
  // When platform mapping is DUAL_TRANSCEIVER with 2 transceivers and 4 ports
  // per module, 8 port state machines should be created (4 ports x 2
  // transceivers).
  initManagersWithMultiTcvrPort(true /* setPhyManager */);
  EXPECT_EQ(portManager_->getStateMachineCountForTest(), 8);
}

TEST_F(PortManagerTest, warmBootStateRoundtrip) {
  const int64_t expectedLastApplied = 1776116451572;
  const int64_t expectedLastColdboot = 1776116000000;

  folly::dynamic configInfo = folly::dynamic::object;
  configInfo[TransceiverManager::kAgentConfigLastAppliedInMsKey] =
      expectedLastApplied;
  configInfo[TransceiverManager::kAgentConfigLastColdbootAppliedInMsKey] =
      expectedLastColdboot;
  folly::dynamic warmBootState = folly::dynamic::object;
  warmBootState[TransceiverManager::kAgentConfigAppliedInfoStateKey] =
      configInfo;

  std::string stateFile = qsfpSvcVolatileDir + "/qsfp_service_state";
  folly::writeFile(folly::toPrettyJson(warmBootState), stateFile.c_str());

  std::string warmBootFlag = qsfpSvcVolatileDir + "/can_warm_boot";
  auto fd = creat(warmBootFlag.c_str(), 0644);
  close(fd);

  gflags::SetCommandLineOptionWithMode(
      "can_qsfp_service_warm_boot", "1", gflags::SET_FLAGS_DEFAULT);

  initManagers(1, 4, true /* setPhyManager */);
  transceiverManager_->init();
  ASSERT_TRUE(transceiverManager_->canWarmBoot());
  portManager_->init();

  phyManager_->setPortLanesForTest(
      PortID(1), {LaneID(0), LaneID(1)}, {LaneID(0), LaneID(1)});
  phyManager_->setPortLanesForTest(
      PortID(2), {LaneID(2), LaneID(3)}, {LaneID(2), LaneID(3)});

  portManager_->gracefulExit();

  std::string stateJson;
  ASSERT_TRUE(folly::readFile(stateFile.c_str(), stateJson));
  auto state = folly::parseJson(stateJson);

  const auto& savedConfigInfo =
      state[TransceiverManager::kAgentConfigAppliedInfoStateKey];
  EXPECT_EQ(
      savedConfigInfo[TransceiverManager::kAgentConfigLastAppliedInMsKey]
          .asInt(),
      expectedLastApplied);
  EXPECT_EQ(
      savedConfigInfo
          [TransceiverManager::kAgentConfigLastColdbootAppliedInMsKey]
              .asInt(),
      expectedLastColdboot);

  ASSERT_TRUE(state.count(TransceiverManager::kPhyStateKey));
  const auto& phyState = state[TransceiverManager::kPhyStateKey];
  ASSERT_TRUE(phyState.count("portToCacheInfo"));
  const auto& portCache = phyState["portToCacheInfo"];

  ASSERT_TRUE(portCache.count("1"));
  const auto& port1 = portCache["1"];
  EXPECT_EQ(port1["systemLanes"], folly::dynamic::array(0, 1));
  EXPECT_EQ(port1["lineLanes"], folly::dynamic::array(0, 1));

  ASSERT_TRUE(portCache.count("2"));
  const auto& port2 = portCache["2"];
  EXPECT_EQ(port2["systemLanes"], folly::dynamic::array(2, 3));
  EXPECT_EQ(port2["lineLanes"], folly::dynamic::array(2, 3));
}

TEST_F(PortManagerTest, setPortLoopbackStateXphyWithoutPhyManagerThrows) {
  // The default fixture builds PortManager with a null PhyManager, which is
  // the real state on platforms that have no xphy. Setting loopback on an
  // xphy component must surface an error rather than dereference the null.
  EXPECT_THROW(
      portManager_->setPortLoopbackState(
          "eth1/1/1", phy::PortComponent::GB_LINE, true),
      FbossError);
  EXPECT_THROW(
      portManager_->setPortLoopbackState(
          "eth1/1/1", phy::PortComponent::GB_SYSTEM, true),
      FbossError);
  // Disabling must be just as safe as enabling.
  EXPECT_THROW(
      portManager_->setPortLoopbackState(
          "eth1/1/1", phy::PortComponent::GB_LINE, false),
      FbossError);
}

TEST_F(PortManagerTest, setPortLoopbackStateUnsupportedComponentIsNoop) {
  // qsfp_service does not own ASIC loopback; it is ignored rather than being
  // treated as an error, and must not reach the PhyManager.
  EXPECT_NO_THROW(portManager_->setPortLoopbackState(
      "eth1/1/1", phy::PortComponent::ASIC, true));
}

TEST_F(PortManagerTest, setPortLoopbackStateInvalidPortThrows) {
  EXPECT_THROW(
      portManager_->setPortLoopbackState(
          "invalid_port", phy::PortComponent::GB_LINE, true),
      FbossError);
}

TEST_F(PortManagerTest, getSdkStateWithoutPhyManagerThrows) {
  EXPECT_THROW(portManager_->getSdkState("sdk_dump"), FbossError);
}

} // namespace facebook::fboss
