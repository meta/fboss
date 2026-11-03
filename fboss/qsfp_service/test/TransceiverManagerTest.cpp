// Copyright 2021-present Facebook. All Rights Reserved.

#include "fboss/qsfp_service/test/TransceiverManagerTestHelper.h"

#include "fboss/lib/CommonFileUtils.h"
#include "fboss/qsfp_service/module/tests/MockSffModule.h"
#include "fboss/qsfp_service/module/tests/MockTransceiverImpl.h"

namespace facebook::fboss {

class TransceiverManagerTest : public TransceiverManagerTestHelper {
 public:
  std::string warmBootFlagFile = qsfpSvcVolatileDir + "/can_warm_boot";
  std::string coldBootFileName =
      qsfpSvcVolatileDir + "/cold_boot_once_qsfp_service";
};

TEST_F(TransceiverManagerTest, getPortNameToModuleMap) {
  EXPECT_EQ(transceiverManager_->getPortNameToModuleMap().at("eth1/1/1"), 0);
  EXPECT_EQ(transceiverManager_->getPortNameToModuleMap().at("eth1/1/2"), 0);
  EXPECT_EQ(transceiverManager_->getPortNameToModuleMap().at("eth1/2/1"), 1);
  EXPECT_THROW(
      transceiverManager_->getPortNameToModuleMap().at("no_such_port"),
      std::out_of_range);
}

TEST_F(TransceiverManagerTest, getSdkStateWithoutPhyManagerThrows) {
  EXPECT_THROW(transceiverManager_->getSdkState("sdk_dump"), FbossError);
}

TEST_F(TransceiverManagerTest, coldBootTest) {
  auto verifyColdBootLogic = [this]() {
    // Delete the existing wedge manager and create a new one
    resetTransceiverManager();
    // Force cold boot is set
    EXPECT_FALSE(transceiverManager_->canWarmBoot());
    // We expect a cold boot in this case and that should trigger hard resets of
    // QSFP modules
    MockTransceiverPlatformApi* xcvrApi =
        static_cast<MockTransceiverPlatformApi*>(
            transceiverManager_->getQsfpPlatformApi());
    for (int i = 0; i < transceiverManager_->getNumQsfpModules(); i++) {
      EXPECT_CALL(*xcvrApi, triggerQsfpHardReset(i + 1)).Times(1);
    }
    transceiverManager_->init();

    // Confirm that the cold boot file and warm boot flag file were deleted
    EXPECT_FALSE(checkFileExists(coldBootFileName));
    EXPECT_FALSE(checkFileExists(warmBootFlagFile));
  };
  auto gracefulExit = [this]() {
    // Trigger a graceful exit
    transceiverManager_->gracefulExit();
    // Check warm boot flag file is created
    EXPECT_TRUE(checkFileExists(warmBootFlagFile));
  };

  // Create the cold boot file
  auto fd = createFile(coldBootFileName);
  close(fd);
  verifyColdBootLogic();

  // Try cold boot again if last time was using graceful exit
  gracefulExit();
  fd = createFile(coldBootFileName);
  close(fd);
  verifyColdBootLogic();

  // Specifically set can_qsfp_service_warm_boot to false to mimic
  // Elbert8DD pim case which doesn't support warm boot
  gracefulExit();
  gflags::SetCommandLineOptionWithMode(
      "can_qsfp_service_warm_boot", "0", gflags::SET_FLAGS_DEFAULT);
  verifyColdBootLogic();
}

TEST_F(TransceiverManagerTest, warmBootTest) {
  // Trigger a graceful exit
  transceiverManager_->gracefulExit();
  gflags::SetCommandLineOptionWithMode(
      "can_qsfp_service_warm_boot", "1", gflags::SET_FLAGS_DEFAULT);
  // Check warm boot flag file is created
  EXPECT_TRUE(checkFileExists(warmBootFlagFile));

  resetTransceiverManager();

  // We expect a warm boot in this case and that should NOT trigger hard resets
  // of QSFP modules
  MockTransceiverPlatformApi* xcvrApi =
      static_cast<MockTransceiverPlatformApi*>(
          transceiverManager_->getQsfpPlatformApi());
  for (int i = 0; i < transceiverManager_->getNumQsfpModules(); i++) {
    EXPECT_CALL(*xcvrApi, triggerQsfpHardReset(i + 1)).Times(0);
  }
  transceiverManager_->init();

  // Confirm that the warm boot falg was still there
  EXPECT_TRUE(checkFileExists(warmBootFlagFile));
  EXPECT_TRUE(transceiverManager_->canWarmBoot());
}

TEST_F(TransceiverManagerTest, runState) {
  resetTransceiverManager();
  EXPECT_FALSE(transceiverManager_->isSystemInitialized());
  EXPECT_FALSE(transceiverManager_->isFullyInitialized());
  EXPECT_FALSE(transceiverManager_->isExiting());
  EXPECT_EQ(
      transceiverManager_->getRunState(), QsfpServiceRunState::UNINITIALIZED);

  transceiverManager_->init();
  EXPECT_TRUE(transceiverManager_->isSystemInitialized());
  EXPECT_FALSE(transceiverManager_->isFullyInitialized());
  EXPECT_FALSE(transceiverManager_->isExiting());
  EXPECT_EQ(
      transceiverManager_->getRunState(), QsfpServiceRunState::INITIALIZED);

  transceiverManager_->refreshStateMachines();
  EXPECT_TRUE(transceiverManager_->isSystemInitialized());
  EXPECT_TRUE(transceiverManager_->isFullyInitialized());
  EXPECT_FALSE(transceiverManager_->isExiting());
  EXPECT_EQ(transceiverManager_->getRunState(), QsfpServiceRunState::ACTIVE);

  transceiverManager_->gracefulExit();
  EXPECT_TRUE(transceiverManager_->isSystemInitialized());
  EXPECT_TRUE(transceiverManager_->isFullyInitialized());
  EXPECT_TRUE(transceiverManager_->isExiting());
  EXPECT_EQ(transceiverManager_->getRunState(), QsfpServiceRunState::EXITING);
}

ACTION(ThrowFbossError) {
  throw FbossError("Mock FbossError");
}
TEST_F(TransceiverManagerTest, getInterfacePhyInfo) {
  std::map<std::string, phy::PhyInfo> phyInfos;
  // Test a valid interface
  transceiverManager_->getInterfacePhyInfo(phyInfos, "eth1/1/1");
  EXPECT_TRUE(phyInfos.find("eth1/1/1") != phyInfos.end());
  // Simulate an exception from xphyInfo and confirm that the map doesn't
  // include that interface's key
  EXPECT_CALL(*transceiverManager_, getXphyInfo(PortID(9)))
      .Times(1)
      .WillRepeatedly(ThrowFbossError());
  transceiverManager_->getInterfacePhyInfo(phyInfos, "eth1/2/1");
  EXPECT_EQ(phyInfos.size(), 1);
  // We still expect the previous interface to exist in the map
  EXPECT_TRUE(phyInfos.find("eth1/1/1") != phyInfos.end());

  // Test an invalid interface
  EXPECT_THROW(
      transceiverManager_->getInterfacePhyInfo(phyInfos, "no_such_interface"),
      FbossError);
}

TEST_F(TransceiverManagerTest, opticalOrActiveCableTest) {
  TcvrState state;
  Cable cable;

  // Set up cable with OPTICAL transmitter tech
  cable.transmitterTech() = TransmitterTechnology::OPTICAL;
  state.cable() = cable;

  // Test with OPTICAL transmitter - should return true
  EXPECT_TRUE(TransceiverManager::opticalOrActiveCable(state));

  // Change to COPPER transmitter - should return false
  state.cable()->transmitterTech() = TransmitterTechnology::COPPER;
  EXPECT_FALSE(TransceiverManager::opticalOrActiveCable(state));
}

TEST_F(TransceiverManagerTest, activeCableTest) {
  TcvrState state;
  Cable cable;

  // Set up cable with ACTIVE_CABLES media type
  cable.mediaTypeEncoding() = MediaTypeEncodings::ACTIVE_CABLES;
  state.cable() = cable;

  // Test with ACTIVE_CABLES media type - should return true
  EXPECT_TRUE(TransceiverManager::activeCable(state));

  // Change to different media type - should return false
  state.cable()->mediaTypeEncoding() = MediaTypeEncodings::OPTICAL_SMF;
  EXPECT_FALSE(TransceiverManager::activeCable(state));
}

TEST_F(TransceiverManagerTest, opticalOrActiveCmisCableTest) {
  TcvrState state;
  Cable cable;

  // Set up cable with OPTICAL transmitter tech
  cable.transmitterTech() = TransmitterTechnology::OPTICAL;
  state.cable() = cable;
  state.transceiverManagementInterface() = TransceiverManagementInterface::CMIS;

  // Test with CMIS interface and optical cable - should return true
  EXPECT_TRUE(TransceiverManager::opticalOrActiveCmisCable(state));

  // Change interface to non-CMIS - should return false
  state.transceiverManagementInterface() = TransceiverManagementInterface::SFF;
  EXPECT_FALSE(TransceiverManager::opticalOrActiveCmisCable(state));
}

TEST_F(TransceiverManagerTest, getTransceiverInfoOptionalNonExistent) {
  // Test with a non-existent transceiver ID
  auto result =
      transceiverManager_->getTransceiverInfoOptional(TransceiverID(999));

  // Verify that the result is std::nullopt
  EXPECT_FALSE(result.has_value());
}

TEST_F(TransceiverManagerTest, resetProgrammedIPhyPortToPortInfoForPorts) {
  // Set up test data using the override mechanism
  TransceiverManager::OverrideTcvrToPortAndProfile testOverride = {
      {TransceiverID(0),
       {{PortID(1), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL},
        {PortID(2), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL}}},
      {TransceiverID(1),
       {{PortID(9), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL},
        {PortID(10), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL}}}};

  transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(testOverride);

  // Program internal PHY ports to populate tcvrToPortInfo_
  transceiverManager_->programInternalPhyPorts(TransceiverID(0));
  transceiverManager_->programInternalPhyPorts(TransceiverID(1));

  // Verify initial state - both transceivers should have populated port info
  auto tcvr0PortInfo =
      transceiverManager_->getProgrammedIphyPortToPortInfo(TransceiverID(0));
  auto tcvr1PortInfo =
      transceiverManager_->getProgrammedIphyPortToPortInfo(TransceiverID(1));

  EXPECT_EQ(tcvr0PortInfo.size(), 2);
  EXPECT_TRUE(tcvr0PortInfo.count(PortID(1)));
  EXPECT_TRUE(tcvr0PortInfo.count(PortID(2)));

  EXPECT_EQ(tcvr1PortInfo.size(), 2);
  EXPECT_TRUE(tcvr1PortInfo.count(PortID(9)));
  EXPECT_TRUE(tcvr1PortInfo.count(PortID(10)));

  // Test Case 1: Reset specific ports across multiple transceivers
  std::unordered_set<PortID> portsToReset = {PortID(1), PortID(9)};
  transceiverManager_->resetProgrammedIphyPortToPortInfoForPorts(portsToReset);

  // Verify that only specified ports were removed
  tcvr0PortInfo =
      transceiverManager_->getProgrammedIphyPortToPortInfo(TransceiverID(0));
  tcvr1PortInfo =
      transceiverManager_->getProgrammedIphyPortToPortInfo(TransceiverID(1));

  // TransceiverID(0) should have only PortID(2), PortID(1) should be removed
  EXPECT_EQ(tcvr0PortInfo.size(), 1);
  EXPECT_FALSE(tcvr0PortInfo.count(PortID(1)));
  EXPECT_TRUE(tcvr0PortInfo.count(PortID(2)));

  // TransceiverID(1) should have only PortID(10), PortID(9) should be removed
  EXPECT_EQ(tcvr1PortInfo.size(), 1);
  EXPECT_FALSE(tcvr1PortInfo.count(PortID(9)));
  EXPECT_TRUE(tcvr1PortInfo.count(PortID(10)));

  // Test Case 2: Reset non-existent ports (should be no-op)
  std::unordered_set<PortID> nonExistentPorts = {PortID(999), PortID(1000)};
  transceiverManager_->resetProgrammedIphyPortToPortInfoForPorts(
      nonExistentPorts);

  // Verify state remains the same
  auto tcvr0PortInfoAfter =
      transceiverManager_->getProgrammedIphyPortToPortInfo(TransceiverID(0));
  auto tcvr1PortInfoAfter =
      transceiverManager_->getProgrammedIphyPortToPortInfo(TransceiverID(1));

  EXPECT_EQ(tcvr0PortInfoAfter.size(), 1);
  EXPECT_TRUE(tcvr0PortInfoAfter.count(PortID(2)));

  EXPECT_EQ(tcvr1PortInfoAfter.size(), 1);
  EXPECT_TRUE(tcvr1PortInfoAfter.count(PortID(10)));

  // Test Case 3: Reset remaining ports
  std::unordered_set<PortID> remainingPorts = {PortID(2), PortID(10)};
  transceiverManager_->resetProgrammedIphyPortToPortInfoForPorts(
      remainingPorts);

  // Verify all specified ports are removed
  tcvr0PortInfo =
      transceiverManager_->getProgrammedIphyPortToPortInfo(TransceiverID(0));
  tcvr1PortInfo =
      transceiverManager_->getProgrammedIphyPortToPortInfo(TransceiverID(1));

  EXPECT_EQ(tcvr0PortInfo.size(), 0);
  EXPECT_EQ(tcvr1PortInfo.size(), 0);

  // Test Case 4: Reset empty set (should be no-op)
  std::unordered_set<PortID> emptySet;
  transceiverManager_->resetProgrammedIphyPortToPortInfoForPorts(emptySet);

  // Verify state remains empty
  tcvr0PortInfo =
      transceiverManager_->getProgrammedIphyPortToPortInfo(TransceiverID(0));
  tcvr1PortInfo =
      transceiverManager_->getProgrammedIphyPortToPortInfo(TransceiverID(1));

  EXPECT_EQ(tcvr0PortInfo.size(), 0);
  EXPECT_EQ(tcvr1PortInfo.size(), 0);

  // Clean up override
  transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(std::nullopt);
}

TEST_F(TransceiverManagerTest, programTransceiverWithNullQsfpConfig) {
  // Set up test data using the override mechanism first
  TransceiverManager::OverrideTcvrToPortAndProfile testOverride = {
      {TransceiverID(0),
       {{PortID(1), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL}}}};

  transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(testOverride);

  // Program internal PHY ports to populate tcvrToPortInfo_
  transceiverManager_->programInternalPhyPorts(TransceiverID(0));

  // Reset the transceiver manager to create one without a qsfp config
  // This will make qsfpConfig_ NULL, which will test the untested lines in
  // getOpticalChannelConfig
  resetTransceiverManager();

  // Set up the override again since resetTransceiverManager creates a new
  // instance
  transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(testOverride);
  transceiverManager_->programInternalPhyPorts(TransceiverID(0));

  // Call programTransceiver which internally calls getOpticalChannelConfig
  // This should handle the NULL qsfpConfig_ case gracefully without crashing
  // The getOpticalChannelConfig method will return std::nullopt when
  // qsfpConfig_ is NULL
  EXPECT_NO_THROW(
      transceiverManager_->programTransceiver(TransceiverID(0), false));

  // Clean up override
  transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(std::nullopt);
}

TEST_F(TransceiverManagerTest, areAllPortsDownWithEmptyPortInfo) {
  // Test Case 1: FLAGS_port_manager_mode is false (default)
  // When port info is empty, should return {false, {}} (ports are assumed up)
  gflags::SetCommandLineOptionWithMode(
      "port_manager_mode", "0", gflags::SET_FLAGS_DEFAULT);

  // Call areAllPortsDown on a transceiver with no programmed ports
  // TransceiverID(0) exists but has no programmed ports yet
  auto [allPortsDown, downPorts] =
      transceiverManager_->areAllPortsDown(TransceiverID(0));

  // In non-PortManager mode, empty port info means ports are assumed up
  EXPECT_FALSE(allPortsDown);
  EXPECT_TRUE(downPorts.empty());

  // Test Case 2: FLAGS_port_manager_mode is true
  // When port info is empty, should return {true, {}} (no ports up)
  gflags::SetCommandLineOptionWithMode(
      "port_manager_mode", "1", gflags::SET_FLAGS_DEFAULT);

  // Call areAllPortsDown again with port_manager_mode enabled
  auto [allPortsDown2, downPorts2] =
      transceiverManager_->areAllPortsDown(TransceiverID(0));

  // In PortManager mode, empty port info means all ports are down
  EXPECT_FALSE(allPortsDown2);
  EXPECT_TRUE(downPorts2.empty());

  // Reset flag to default
  gflags::SetCommandLineOptionWithMode(
      "port_manager_mode", "0", gflags::SET_FLAGS_DEFAULT);
}

TEST_F(TransceiverManagerTest, exceptionInRefresh) {
  auto tcvrOneID = TransceiverID(0);
  auto tcvrTwoID = TransceiverID(1);

  auto transceiverImpl1 =
      std::make_unique<::testing::NiceMock<MockTransceiverImpl>>();
  EXPECT_CALL(*transceiverImpl1, detectTransceiver())
      .WillRepeatedly(::testing::Return(true));
  EXPECT_CALL(*transceiverImpl1, getNum())
      .WillRepeatedly(::testing::Return(static_cast<int>(tcvrOneID)));
  qsfpImpls_.push_back(std::move(transceiverImpl1));

  transceiverManager_->overrideTransceiverForTesting(
      tcvrOneID,
      std::make_unique<MockSffModule>(
          transceiverManager_->getPortNames(tcvrOneID),
          qsfpImpls_.back().get(),
          tcvrConfig_,
          transceiverManager_->getTransceiverName(tcvrOneID)));

  auto transceiverImpl2 =
      std::make_unique<::testing::NiceMock<MockTransceiverImpl>>();
  EXPECT_CALL(*transceiverImpl2, detectTransceiver())
      .WillRepeatedly(::testing::Return(true));
  EXPECT_CALL(*transceiverImpl2, getNum())
      .WillRepeatedly(::testing::Return(static_cast<int>(tcvrTwoID)));
  qsfpImpls_.push_back(std::move(transceiverImpl2));

  auto tcvrTwo = static_cast<MockSffModule*>(
      transceiverManager_->overrideTransceiverForTesting(
          tcvrTwoID,
          std::make_unique<MockSffModule>(
              transceiverManager_->getPortNames(tcvrTwoID),
              qsfpImpls_.back().get(),
              tcvrConfig_,
              transceiverManager_->getTransceiverName(tcvrTwoID))));

  // Initialize and refresh to get transceivers into a known state
  transceiverManager_->refreshStateMachines();

  // Verify both transceivers start without communication errors
  std::map<int32_t, TransceiverInfo> tcvrInfoBefore;
  transceiverManager_->getTransceiversInfo(
      tcvrInfoBefore, std::make_unique<std::vector<int32_t>>());
  EXPECT_FALSE(*tcvrInfoBefore[static_cast<int>(tcvrOneID)]
                    .tcvrState()
                    ->communicationError());
  EXPECT_FALSE(*tcvrInfoBefore[static_cast<int>(tcvrTwoID)]
                    .tcvrState()
                    ->communicationError());

  // Make tcvrTwo throw an exception during refresh
  ON_CALL(*tcvrTwo, updateQsfpData(::testing::_))
      .WillByDefault(ThrowFbossError());

  transceiverManager_->refreshStateMachines();
  // tcvrOne should NOT have communication error (successful refresh)
  // tcvrTwo SHOULD have communication error (failed refresh)
  std::map<int32_t, TransceiverInfo> tcvrInfoAfter;
  transceiverManager_->getTransceiversInfo(
      tcvrInfoAfter, std::make_unique<std::vector<int32_t>>());

  EXPECT_FALSE(*tcvrInfoAfter[static_cast<int>(tcvrOneID)]
                    .tcvrState()
                    ->communicationError());
  EXPECT_TRUE(*tcvrInfoAfter[static_cast<int>(tcvrTwoID)]
                   .tcvrState()
                   ->communicationError());
}

TEST_F(TransceiverManagerTest, populateSnapshots) {
  auto tcvrID = TransceiverID(0);

  auto transceiverImpl =
      std::make_unique<::testing::NiceMock<MockTransceiverImpl>>();
  EXPECT_CALL(*transceiverImpl, detectTransceiver())
      .WillRepeatedly(::testing::Return(true));
  EXPECT_CALL(*transceiverImpl, getNum())
      .WillRepeatedly(::testing::Return(static_cast<int>(tcvrID)));
  qsfpImpls_.push_back(std::move(transceiverImpl));

  transceiverManager_->overrideTransceiverForTesting(
      tcvrID,
      std::make_unique<MockSffModule>(
          transceiverManager_->getPortNames(tcvrID),
          qsfpImpls_.back().get(),
          tcvrConfig_,
          transceiverManager_->getTransceiverName(tcvrID)));

  auto& snapshotManagers = transceiverManager_->getSnapshotManagersForTesting();

  // populate snapshots
  transceiverManager_->refreshStateMachines();

  auto snapshotManagersLocked = snapshotManagers.rlock();
  ASSERT_TRUE(
      snapshotManagersLocked->find(tcvrID) != snapshotManagersLocked->end());
  auto snapshots = snapshotManagersLocked->at(tcvrID).getSnapshots();
  EXPECT_FALSE(snapshots.empty());

  // Fill the buffer
  snapshotManagersLocked.unlock();
  for (auto i = 1; i < snapshots.maxSize(); i++) {
    transceiverManager_->refreshStateMachines();
  }
  snapshotManagersLocked = snapshotManagers.rlock();
  snapshots = snapshotManagersLocked->at(tcvrID).getSnapshots();

  // Verify that we stay at the max size
  EXPECT_EQ(snapshots.size(), snapshots.maxSize());
  snapshotManagersLocked.unlock();
  transceiverManager_->refreshStateMachines();
  snapshotManagersLocked = snapshotManagers.rlock();
  snapshots = snapshotManagersLocked->at(tcvrID).getSnapshots();
  EXPECT_EQ(snapshots.size(), snapshots.maxSize());
}

} // namespace facebook::fboss
