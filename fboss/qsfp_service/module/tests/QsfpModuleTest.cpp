/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/qsfp_service/test/TransceiverManagerTestHelper.h"

#include "fboss/qsfp_service/module/CdbCommandBlock.h"
#include "fboss/qsfp_service/module/tests/MockSffModule.h"
#include "fboss/qsfp_service/module/tests/MockTransceiverImpl.h"

#include <gmock/gmock.h>

#include <chrono>
#include <thread>

namespace {
std::string kPortName = "eth1/1/1";
}

namespace facebook::fboss {

using namespace ::testing;

class QsfpModuleTest : public TransceiverManagerTestHelper {
 public:
  TransceiverPortState fortyGState{kPortName, 0, cfg::PortSpeed::FORTYG, 4};
  TransceiverPortState hundredGState{kPortName, 0, cfg::PortSpeed::HUNDREDG, 4};

  void SetUp() override {
    TransceiverManagerTestHelper::SetUp();
    setupQsfp();
  }

  void setupQsfp(
      std::shared_ptr<const TransceiverConfig> tcvrConfig = nullptr) {
    auto transceiverImpl = std::make_unique<NiceMock<MockTransceiverImpl>>();
    // So we can check what happens during testing
    transImpl_ = transceiverImpl.get();
    qsfpImpls_.push_back(std::move(transceiverImpl));
    qsfp_ = static_cast<MockSffModule*>(
        transceiverManager_->overrideTransceiverForTesting(
            kTcvrID,
            std::make_unique<MockSffModule>(
                transceiverManager_->getPortNames(kTcvrID),
                qsfpImpls_.back().get(),
                tcvrConfig ? tcvrConfig : tcvrConfig_,
                transceiverManager_->getTransceiverName(kTcvrID))));
    qsfp_->setVendorPN();

    gflags::SetCommandLineOptionWithMode(
        "tx_enable_interval", "0", gflags::SET_FLAGS_DEFAULT);

    gflags::SetCommandLineOptionWithMode(
        "customize_interval", "0", gflags::SET_FLAGS_DEFAULT);

    EXPECT_EQ(transImpl_->getName().toString(), qsfp_->getNameString());
    EXPECT_CALL(*transImpl_, detectTransceiver()).WillRepeatedly(Return(true));
    qsfp_->detectPresence();
  }

  void triggerPortsChanged(
      const TransceiverManager::OverrideTcvrToPortAndProfile&
          newTcvrToPortAndProfile,
      bool resetDataPath = false) {
    transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
        newTcvrToPortAndProfile);
    // Refresh once to trigger iphy
    transceiverManager_->refreshStateMachines();
    // Then we can call programTransceiver() directly so that we can check the
    // error case
    transceiverManager_->programTransceiver(kTcvrID, resetDataPath);
  }

  NiceMock<MockTransceiverImpl>* transImpl_;
  MockSffModule* qsfp_;
  const TransceiverID kTcvrID = TransceiverID(0);
};

TEST_F(QsfpModuleTest, setRateSelect) {
  ON_CALL(*qsfp_, setRateSelectIfSupported(_, _, _))
      .WillByDefault(
          Invoke(qsfp_, &MockSffModule::actualSetRateSelectIfSupported));
  ON_CALL(*qsfp_, customizationSupported()).WillByDefault(Return(true));
  EXPECT_CALL(*qsfp_, setPowerOverrideIfSupportedLocked(_)).Times(AtLeast(1));
  EXPECT_CALL(*qsfp_, setCdrIfSupported(_, _, _)).Times(AtLeast(1));

  // every customize call does 1 write for enabling tx, plus any other
  // writes for settings changes.
  {
    InSequence a;
    // Unsupported
    EXPECT_CALL(*transImpl_, writeTransceiver(_, _, _, _)).Times(0);
    TransceiverPortState fortyGState{kPortName, 0, cfg::PortSpeed::FORTYG, 4};
    TransceiverPortState hundredGState{
        kPortName, 0, cfg::PortSpeed::HUNDREDG, 4};
    qsfp_->customizeTransceiver(fortyGState);
    qsfp_->customizeTransceiver(hundredGState);

    // Using V1
    qsfp_->setRateSelect(
        RateSelectState::EXTENDED_RATE_SELECT_V1,
        RateSelectSetting::FROM_6_6GB_AND_ABOVE);
    qsfp_->customizeTransceiver(fortyGState);

    qsfp_->setRateSelect(
        RateSelectState::EXTENDED_RATE_SELECT_V2,
        RateSelectSetting::LESS_THAN_12GB);
    // 40G + LESS_THAN_12GB -> no change
    qsfp_->customizeTransceiver(fortyGState);
    // 100G + LESS_THAN_12GB -> needs change
    EXPECT_CALL(*transImpl_, writeTransceiver(_, _, _, _)).Times(2);
    qsfp_->customizeTransceiver(hundredGState);

    qsfp_->setRateSelect(
        RateSelectState::EXTENDED_RATE_SELECT_V2,
        RateSelectSetting::FROM_24GB_to_26GB);
    // 40G + FROM_24GB_to_26GB -> needs change
    EXPECT_CALL(*transImpl_, writeTransceiver(_, _, _, _)).Times(2);
    qsfp_->customizeTransceiver(fortyGState);
    // 100G + FROM_24GB_to_26GB -> no change
    EXPECT_CALL(*transImpl_, writeTransceiver(_, _, _, _)).Times(0);
    qsfp_->customizeTransceiver(hundredGState);
  }
}

TEST_F(QsfpModuleTest, retrieveRateSelectSetting) {
  auto data = qsfp_->getRateSelectSettingValue(RateSelectState::UNSUPPORTED);
  EXPECT_EQ(data, RateSelectSetting::UNSUPPORTED);

  data = qsfp_->getRateSelectSettingValue(
      RateSelectState::APPLICATION_RATE_SELECT);
  EXPECT_EQ(data, RateSelectSetting::UNSUPPORTED);

  EXPECT_CALL(*qsfp_, getSettingsValue(_, _))
      .WillRepeatedly(Return(0b01010101));
  data = qsfp_->getRateSelectSettingValue(
      RateSelectState::EXTENDED_RATE_SELECT_V1);
  EXPECT_EQ(data, RateSelectSetting::FROM_2_2GB_TO_6_6GB);

  data = qsfp_->getRateSelectSettingValue(
      RateSelectState::EXTENDED_RATE_SELECT_V2);
  EXPECT_EQ(data, RateSelectSetting::FROM_12GB_TO_24GB);
}

TEST_F(QsfpModuleTest, setCdr) {
  ON_CALL(*qsfp_, setCdrIfSupported(_, _, _))
      .WillByDefault(Invoke(qsfp_, &MockSffModule::actualSetCdrIfSupported));
  ON_CALL(*qsfp_, customizationSupported()).WillByDefault(Return(true));

  EXPECT_CALL(*qsfp_, setPowerOverrideIfSupportedLocked(_)).Times(AtLeast(1));
  EXPECT_CALL(*qsfp_, setRateSelectIfSupported(_, _, _)).Times(AtLeast(1));

  // every customize call does 1 write for enabling tx, plus any other
  // writes for settings changes.
  {
    InSequence a;
    EXPECT_CALL(*transImpl_, writeTransceiver(_, _, _, _)).Times(0);
    // Unsupported
    qsfp_->customizeTransceiver(fortyGState);
    qsfp_->customizeTransceiver(hundredGState);

    qsfp_->setCdrState(FeatureState::DISABLED, FeatureState::DISABLED);
    // Disabled + 40G
    qsfp_->customizeTransceiver(fortyGState);
    // Disabled + 100G
    EXPECT_CALL(*transImpl_, writeTransceiver(_, _, _, _)).Times(1);
    qsfp_->customizeTransceiver(hundredGState); // CHECK

    qsfp_->setCdrState(FeatureState::ENABLED, FeatureState::ENABLED);
    // Enabled + 40G
    EXPECT_CALL(*transImpl_, writeTransceiver(_, _, _, _)).Times(1);
    qsfp_->customizeTransceiver(fortyGState); // CHECK
    // Enabled + 100G
    EXPECT_CALL(*transImpl_, writeTransceiver(_, _, _, _)).Times(0);
    qsfp_->customizeTransceiver(hundredGState); // CHECK

    // One of rx an tx enabled with the other disabled
    qsfp_->setCdrState(FeatureState::DISABLED, FeatureState::ENABLED);
    // 40G
    EXPECT_CALL(*transImpl_, writeTransceiver(_, _, _, _)).Times(1);
    qsfp_->customizeTransceiver(fortyGState); // CHECK
    // 100G
    EXPECT_CALL(*transImpl_, writeTransceiver(_, _, _, _)).Times(1);
    qsfp_->customizeTransceiver(hundredGState);
  }
}

TEST_F(QsfpModuleTest, portsChangedAllDown25G) {
  ON_CALL(*qsfp_, getTransceiverInfo()).WillByDefault(Return(qsfp_->fakeInfo_));
  ON_CALL(*qsfp_, customizationSupported()).WillByDefault(Return(true));

  // should customize w/ 25G
  EXPECT_CALL(*qsfp_, setCdrIfSupported(cfg::PortSpeed::TWENTYFIVEG, _, _))
      .Times(4);

  triggerPortsChanged(
      {{kTcvrID,
        {
            {PortID(1), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL},
            {PortID(2), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL},
            {PortID(3), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL},
            {PortID(4), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL},
        }}});
}

TEST_F(QsfpModuleTest, portsChanged50G) {
  ON_CALL(*qsfp_, getTransceiverInfo()).WillByDefault(Return(qsfp_->fakeInfo_));
  ON_CALL(*qsfp_, customizationSupported()).WillByDefault(Return(true));

  // should customize w/ 50G
  EXPECT_CALL(*qsfp_, setCdrIfSupported(cfg::PortSpeed::FIFTYG, _, _)).Times(2);

  // We only store enabled ports
  triggerPortsChanged(
      {{kTcvrID,
        {
            {PortID(1), cfg::PortProfileID::PROFILE_50G_2_NRZ_CL74_COPPER},
            {PortID(3), cfg::PortProfileID::PROFILE_50G_2_NRZ_CL74_COPPER},
        }}});
}

TEST_F(QsfpModuleTest, portsChangedOnePortPerModule) {
  setupQsfp();
  ON_CALL(*qsfp_, getTransceiverInfo()).WillByDefault(Return(qsfp_->fakeInfo_));
  ON_CALL(*qsfp_, customizationSupported()).WillByDefault(Return(true));

  EXPECT_CALL(*qsfp_, setCdrIfSupported(cfg::PortSpeed::HUNDREDG, _, _))
      .Times(1);

  triggerPortsChanged(
      {{kTcvrID,
        {
            {PortID(1), cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_OPTICAL},
        }}});
}

TEST_F(QsfpModuleTest, portsChangedSpeedMismatch) {
  ON_CALL(*qsfp_, getTransceiverInfo()).WillByDefault(Return(qsfp_->fakeInfo_));
  ON_CALL(*qsfp_, customizationSupported()).WillByDefault(Return(true));

  // should customize twice since we support different speeds per transceiver
  EXPECT_CALL(*qsfp_, setCdrIfSupported(cfg::PortSpeed::FIFTYG, _, _)).Times(1);
  EXPECT_CALL(*qsfp_, setCdrIfSupported(cfg::PortSpeed::TWENTYFIVEG, _, _))
      .Times(1);

  // We only store enabled ports
  triggerPortsChanged(
      {{kTcvrID,
        {
            {PortID(1), cfg::PortProfileID::PROFILE_50G_2_NRZ_CL74_COPPER},
            {PortID(3), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL},
        }}});
}

TEST_F(QsfpModuleTest, skipCustomizingForRefresh) {
  // With new state machine, we no longer use Transceiver::refresh() to
  // customize transceiver. Instead, the state machine will use
  // programTransceiver() to do the job following the new port programming order
  EXPECT_CALL(*qsfp_, setCdrIfSupported(_, _, _)).Times(0);
  TransceiverManager::OverrideTcvrToPortAndProfile oneTcvrTo4X25G = {
      {kTcvrID,
       {
           {PortID(1), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL},
           {PortID(2), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL},
           {PortID(3), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL},
           {PortID(4), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL},
       }}};
  transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
      oneTcvrTo4X25G);
  // Refresh once to trigger iphy
  transceiverManager_->refreshStateMachines();
  qsfp_->refresh();
}

TEST_F(QsfpModuleTest, skipCustomizingMissingIphyPorts) {
  // should not customize if we don't have knowledge of all ports
  EXPECT_CALL(*qsfp_, setCdrIfSupported(_, _, _)).Times(0);
  transceiverManager_->programTransceiver(kTcvrID, false);
}

TEST_F(QsfpModuleTest, skipCustomizingMissingTransceiver) {
  ON_CALL(*qsfp_, getTransceiverInfo()).WillByDefault(Return(qsfp_->fakeInfo_));
  // Trigger transceiver removal
  EXPECT_CALL(*transImpl_, detectTransceiver()).WillRepeatedly(Return(false));
  qsfp_->detectPresence();

  EXPECT_CALL(*qsfp_, setCdrIfSupported(_, _, _)).Times(0);
  triggerPortsChanged(
      {{kTcvrID,
        {
            {PortID(1), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL},
            {PortID(2), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL},
            {PortID(3), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL},
            {PortID(4), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL},
        }}});
}

TEST_F(QsfpModuleTest, skipCustomizingCopperTransceiver) {
  ON_CALL(*qsfp_, getTransceiverInfo()).WillByDefault(Return(qsfp_->fakeInfo_));
  // Should get detected as copper and skip all customization
  EXPECT_CALL(*qsfp_, getQsfpTransmitterTechnology())
      .WillRepeatedly(Return(TransmitterTechnology::COPPER));

  EXPECT_CALL(*qsfp_, setCdrIfSupported(_, _, _)).Times(0);
  triggerPortsChanged(
      {{kTcvrID,
        {
            {PortID(1), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL},
            {PortID(2), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL},
            {PortID(3), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL},
            {PortID(4), cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL},
        }}});
}

TEST_F(QsfpModuleTest, updateQsfpDataPartial) {
  // Ensure that partial updates don't ever call writeTranscevier,
  // which needs to gain control of the bus and slows the call
  // down drastically.
  EXPECT_CALL(*transImpl_, writeTransceiver(_, _, _, _)).Times(0);
  qsfp_->actualUpdateQsfpData(false);
}

TEST_F(QsfpModuleTest, updateQsfpDataFull) {
  // Bit of a hack to ensure we have flatMem_ == false.
  ON_CALL(*transImpl_, readTransceiver(_, _, _))
      .WillByDefault(DoAll(
          InvokeWithoutArgs(qsfp_, &MockSffModule::setFlatMem), Return(0)));

  // Full updates do need to write to select higher pages
  EXPECT_CALL(*transImpl_, writeTransceiver(_, _, _, _)).Times(AtLeast(1));

  qsfp_->actualUpdateQsfpData(true);
}

TEST_F(QsfpModuleTest, periodicRefreshSkippedWhenInfoIsFresh) {
  gflags::FlagSaver flagSaver;
  gflags::SetCommandLineOptionWithMode(
      "qsfp_data_refresh_interval", "10", gflags::SET_FLAGS_VALUE);
  qsfp_->refresh();

  EXPECT_CALL(*qsfp_, updateQsfpData(_)).Times(0);
  qsfp_->refresh();
}

TEST_F(QsfpModuleTest, periodicRefreshNotStarvedByOutOfBandReads) {
  gflags::FlagSaver flagSaver;
  gflags::SetCommandLineOptionWithMode(
      "qsfp_data_refresh_interval", "1", gflags::SET_FLAGS_VALUE);
  qsfp_->refresh();
  /* sleep override */
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));

  // Out-of-band read like a failing programTransceiver() retry does
  qsfp_->actualUpdateQsfpData(false);

  EXPECT_CALL(*qsfp_, updateQsfpData(_)).Times(1);
  qsfp_->refresh();
}

TEST_F(QsfpModuleTest, readTransceiver) {
  // Skip the length field and confirm that the length of data in response is 1.
  // Page is also skipped so there should not be a write to byte 127.
  EXPECT_CALL(*transImpl_, writeTransceiver(_, _, _, _)).Times(0);
  EXPECT_CALL(*transImpl_, readTransceiver(_, _, _)).Times(1);
  TransceiverIOParameters param;
  param.offset() = 0;
  auto buf = qsfp_->readTransceiver(param);
  EXPECT_EQ(buf->length(), 1);

  // Test for a specific length
  EXPECT_CALL(*transImpl_, writeTransceiver(_, _, _, _)).Times(0);
  EXPECT_CALL(*transImpl_, readTransceiver(_, _, _)).Times(1);
  param.length() = 10;
  buf = qsfp_->readTransceiver(param);
  EXPECT_EQ(buf->length(), *param.length());

  // Set the page
  EXPECT_CALL(*transImpl_, writeTransceiver(_, _, _, _)).Times(1);
  EXPECT_CALL(*transImpl_, readTransceiver(_, _, _)).Times(1);
  param.page() = 3;
  buf = qsfp_->readTransceiver(param);
  EXPECT_EQ(buf->length(), *param.length());

  // Test on a transceiver that fails detection
  EXPECT_CALL(*transImpl_, detectTransceiver()).WillRepeatedly(Return(false));
  EXPECT_CALL(*transImpl_, readTransceiver(_, _, _)).Times(0);
  qsfp_->detectPresence();
  buf = qsfp_->readTransceiver(param);
  EXPECT_EQ(buf->length(), 0);
}

TEST_F(QsfpModuleTest, writeTransceiver) {
  // Expect a call to writeTransceiver and the result to be successful
  EXPECT_CALL(*transImpl_, writeTransceiver(_, _, _, _)).Times(1);
  TransceiverIOParameters param;
  param.offset() = 0x23;
  uint8_t data = 0xab;
  EXPECT_EQ(qsfp_->writeTransceiver(param, &data), true);

  // Expect a call to writeTransceiver and the result to be successful
  // (multi-byte)
  EXPECT_CALL(*transImpl_, writeTransceiver(_, _, _, _)).Times(1);
  param.offset() = 0x23;
  std::vector<uint8_t> multiByteData{0xab, 0xcd, 0xef};
  EXPECT_EQ(qsfp_->writeTransceiver(param, multiByteData.data()), true);

  // Set the page
  EXPECT_CALL(*transImpl_, writeTransceiver(_, _, _, _)).Times(2);
  param.page() = 3;
  uint8_t data2 = 0xde;
  EXPECT_EQ(qsfp_->writeTransceiver(param, &data2), true);

  // Test on a transceiver that fails detection, the result should be false
  EXPECT_CALL(*transImpl_, detectTransceiver()).WillRepeatedly(Return(false));
  EXPECT_CALL(*transImpl_, writeTransceiver(_, _, _, _)).Times(0);
  qsfp_->detectPresence();
  uint8_t data3 = 0xac;
  EXPECT_EQ(qsfp_->writeTransceiver(param, &data3), false);
}

TEST_F(QsfpModuleTest, verifyLaneToPortMapping) {
  std::vector<uint8_t> lanes50GPort1 = {0, 1};
  std::vector<uint8_t> lanes50GPort3 = {2, 3};
  std::vector<uint8_t> lanesTwentyFiveGPort1 = {0};
  std::vector<uint8_t> lanesTwentyFiveGPort2 = {1};
  ProgramTransceiverState programTcvrState;
  TransceiverPortState portState;

  auto verify = [this](std::map<std::string, std::vector<int>>& expectedMap) {
    qsfp_->useActualGetTransceiverInfo();
    auto info = qsfp_->getTransceiverInfo();
    EXPECT_EQ(info.tcvrState()->portNameToHostLanes(), expectedMap);
    EXPECT_EQ(info.tcvrState()->portNameToMediaLanes(), expectedMap);
    EXPECT_EQ(info.tcvrStats()->portNameToHostLanes(), expectedMap);
    EXPECT_EQ(info.tcvrStats()->portNameToMediaLanes(), expectedMap);
  };
  // Refresh once to validate the cache before starting the test
  transceiverManager_->refreshStateMachines();

  // Initially in the test we'll call programTransceiver for 2x50G ports,
  // correspondingly set expectations for configuredHostLanes and
  // configuredMediaLanes
  ON_CALL(*qsfp_, configuredHostLanes(0)).WillByDefault(Return(lanes50GPort1));
  ON_CALL(*qsfp_, configuredHostLanes(2)).WillByDefault(Return(lanes50GPort3));
  ON_CALL(*qsfp_, configuredMediaLanes(0)).WillByDefault(Return(lanes50GPort1));
  ON_CALL(*qsfp_, configuredMediaLanes(2)).WillByDefault(Return(lanes50GPort3));

  // Program only one port of 50G. Expect the correct host and media lanes for
  // this port
  portState.portName = "eth1/1/1";
  portState.startHostLane = 0;
  portState.speed = cfg::PortSpeed::FIFTYG;
  portState.numHostLanes = 2;
  programTcvrState.ports.emplace(portState.portName, portState);
  qsfp_->programTransceiver(programTcvrState, false /* needResetDataPath */);
  qsfp_->useActualGetTransceiverInfo();
  std::map<std::string, std::vector<int>> expectedMap = {{"eth1/1/1", {0, 1}}};
  verify(expectedMap);

  // Now program the 2nd 50G port. Expect the correct host and media lanes for
  // both ports
  portState.portName = "eth1/1/3";
  portState.startHostLane = 2;
  portState.speed = cfg::PortSpeed::FIFTYG;
  portState.numHostLanes = 2;
  programTcvrState.ports.emplace(portState.portName, portState);
  qsfp_->programTransceiver(programTcvrState, false /* needResetDataPath */);
  expectedMap = {{"eth1/1/1", {0, 1}}, {"eth1/1/3", {2, 3}}};
  verify(expectedMap);

  // Now we'll change port 1 to 2x25G. Set the expectations correspondingly
  programTcvrState.ports.clear();
  ON_CALL(*qsfp_, configuredHostLanes(0))
      .WillByDefault(Return(lanesTwentyFiveGPort1));
  ON_CALL(*qsfp_, configuredHostLanes(1))
      .WillByDefault(Return(lanesTwentyFiveGPort2));
  ON_CALL(*qsfp_, configuredHostLanes(2)).WillByDefault(Return(lanes50GPort3));
  ON_CALL(*qsfp_, configuredMediaLanes(0)).WillByDefault(Return(lanes50GPort1));
  ON_CALL(*qsfp_, configuredMediaLanes(0))
      .WillByDefault(Return(lanesTwentyFiveGPort1));
  ON_CALL(*qsfp_, configuredMediaLanes(1))
      .WillByDefault(Return(lanesTwentyFiveGPort2));
  ON_CALL(*qsfp_, configuredMediaLanes(2)).WillByDefault(Return(lanes50GPort3));

  // Configure 2x25G + 1x50G ports
  portState.portName = "eth1/1/1";
  portState.startHostLane = 0;
  portState.speed = cfg::PortSpeed::TWENTYFIVEG;
  portState.numHostLanes = 1;
  programTcvrState.ports.emplace(portState.portName, portState);
  portState.portName = "eth1/1/2";
  portState.startHostLane = 1;
  portState.speed = cfg::PortSpeed::TWENTYFIVEG;
  portState.numHostLanes = 1;
  programTcvrState.ports.emplace(portState.portName, portState);
  portState.portName = "eth1/1/3";
  portState.startHostLane = 2;
  portState.speed = cfg::PortSpeed::FIFTYG;
  portState.numHostLanes = 2;
  programTcvrState.ports.emplace(portState.portName, portState);
  qsfp_->programTransceiver(programTcvrState, false /* needResetDataPath */);
  expectedMap = {{"eth1/1/1", {0}}, {"eth1/1/2", {1}}, {"eth1/1/3", {2, 3}}};
  verify(expectedMap);
}

TEST_F(QsfpModuleTest, getFirmwareUpgradeData) {
  qsfp_->overrideVendorPN(getFakePartNumber());

  // Test empty fw status
  transceiverManager_->refreshStateMachines();
  qsfp_->useActualGetTransceiverInfo();
  EXPECT_FALSE(transceiverManager_->getFirmwareUpgradeData(*qsfp_).has_value());

  // Test app fw version match (3-tuple config, module reports 2-tuple +
  // buildNumber)
  qsfp_->setAppFwVersion(getFakeAppFwVersion(), getFakeAppFwBuildNumber());
  transceiverManager_->refreshStateMachines();
  qsfp_->useActualGetTransceiverInfo();
  EXPECT_FALSE(transceiverManager_->getFirmwareUpgradeData(*qsfp_).has_value());

  // Test app fw version match when module has no buildNumber (3-tuple config
  // but module can't be validated, skip upgrade)
  qsfp_->setAppFwVersion(getFakeAppFwVersion());
  transceiverManager_->refreshStateMachines();
  qsfp_->useActualGetTransceiverInfo();
  EXPECT_FALSE(transceiverManager_->getFirmwareUpgradeData(*qsfp_).has_value());

  // Test app fw version mismatch (module reports buildNumber so 3-tuple
  // comparison fires)
  qsfp_->setAppFwVersion("9.9", 9999);
  transceiverManager_->refreshStateMachines();
  qsfp_->useActualGetTransceiverInfo();
  EXPECT_TRUE(transceiverManager_->getFirmwareUpgradeData(*qsfp_).has_value());

  // Test dsp fw status match
  qsfp_->setDspFwVersion(getFakeDspFwVersion());
  transceiverManager_->refreshStateMachines();
  qsfp_->useActualGetTransceiverInfo();
  EXPECT_FALSE(transceiverManager_->getFirmwareUpgradeData(*qsfp_).has_value());

  // Test dsp fw status mismatch
  qsfp_->setDspFwVersion("bar");
  transceiverManager_->refreshStateMachines();
  qsfp_->useActualGetTransceiverInfo();
  EXPECT_TRUE(transceiverManager_->getFirmwareUpgradeData(*qsfp_).has_value());
}

TEST_F(QsfpModuleTest, getFwStorageHandlePrefersQsfpConfig) {
  qsfp_->overrideVendorPN(getFakePartNumber());
  transceiverManager_->refreshStateMachines();
  qsfp_->useActualGetTransceiverInfo();
  EXPECT_EQ(qsfp_->getFwStorageHandle(), getFakeFwStorageHandle());
}

TEST_F(QsfpModuleTest, getFwStorageHandleUnknownPartNumber) {
  // Not in the qsfp config nor in the built in map, so neither source can
  // resolve it.
  qsfp_->overrideVendorPN("PART-NUMBER-IN-NEITHER-MAP");
  transceiverManager_->refreshStateMachines();
  qsfp_->useActualGetTransceiverInfo();
  EXPECT_EQ(qsfp_->getFwStorageHandle(), "");
}

TEST_F(QsfpModuleTest, vendorOverridesFromQsfpConfig) {
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
  setupQsfp(
      std::make_shared<const TransceiverConfig>(
          TransceiverOverrides{},
          std::map<std::string, std::string>{},
          vendorOverrides));
  transceiverManager_->refreshStateMachines();
  qsfp_->useActualGetTransceiverInfo();
  EXPECT_EQ(
      qsfp_->getTransceiverInfo().tcvrState()->vendorOverrides().to_optional(),
      vendorOverrides);
}

TEST_F(QsfpModuleTest, vendorOverridesUnsetWithoutQsfpConfig) {
  transceiverManager_->refreshStateMachines();
  qsfp_->useActualGetTransceiverInfo();
  EXPECT_FALSE(
      qsfp_->getTransceiverInfo().tcvrState()->vendorOverrides().has_value());
}

TEST_F(QsfpModuleTest, cdbFwDownloadStartBufferOverflowProtection) {
  // Test that createCdbCmdFwDownloadStart clamps imageChunkLen to prevent
  // stack buffer overflow. A malicious module could set
  // startCommandPayloadSize=0xFF (255) in the CDB reply, attempting to copy 255
  // bytes into the 112-byte cdbImageHeader buffer. The fix clamps to max 112.
  CdbCommandBlock testBlock;
  const uint8_t oversizedPayloadSize = 0xFF; // Malicious value from module
  const std::vector<uint8_t> fakeImage(256, 0xAA);
  int imageOffset = 0;

  // Call createCdbCmdFwDownloadStart with oversized payload size
  testBlock.createCdbCmdFwDownloadStart(
      oversizedPayloadSize,
      static_cast<int>(fakeImage.size()),
      imageOffset,
      fakeImage.data());

  // Verify that imageOffset (number of bytes copied) is clamped to max header
  // len
  EXPECT_EQ(imageOffset, CdbCommandBlock::kCdbFwDnldStartMaxHeaderLen);

  // Verify the LPL length field is also clamped (imageChunkLen + 8)
  EXPECT_LE(
      testBlock.getCdbLplLength(),
      CdbCommandBlock::kCdbFwDnldStartMaxHeaderLen + 8);
}

} // namespace facebook::fboss
