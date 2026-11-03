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

#include "fboss/qsfp_service/module/sff/Sff8472Module.h"
#include "fboss/qsfp_service/module/sff/SffModule.h"
#include "fboss/qsfp_service/module/tests/FakeTransceiverImpl.h"
#include "fboss/qsfp_service/module/tests/TransceiverTestsHelper.h"
#include "fboss/qsfp_service/test/hw_test/HwTransceiverUtils.h"

namespace facebook::fboss {

class SffTest : public TransceiverManagerTestHelper {
 public:
  template <typename XcvrImplT>
  SffModule* overrideSffModule(TransceiverID id, bool willRefresh = true) {
    qsfpImpls_.push_back(
        std::make_unique<XcvrImplT>(id, transceiverManager_.get()));
    auto xcvr = static_cast<SffModule*>(
        transceiverManager_->overrideTransceiverForTesting(
            id,
            std::make_unique<SffModule>(
                transceiverManager_->getPortNames(id),
                qsfpImpls_.back().get(),
                tcvrConfig_,
                transceiverManager_->getTransceiverName(id))));

    if (willRefresh) {
      // Refresh once to make sure the override transceiver finishes refresh
      transceiverManager_->refreshStateMachines();
    }

    return xcvr;
  }
};

// Tests that the transceiverInfo object is correctly populated
TEST_F(SffTest, cwdm4TransceiverInfoTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideSffModule<SffCwdm4Transceiver>(xcvrID);

  // Verify SffModule logic
  EXPECT_EQ(xcvr->numHostLanes(), 4);
  EXPECT_EQ(xcvr->numMediaLanes(), 4);

  // Verify getTransceiverInfo() result
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_EQ(
      *info.tcvrState()->extendedSpecificationComplianceCode(),
      ExtendedSpecComplianceCode::CWDM4_100G);
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(), MediaInterfaceCode::CWDM4_100G);
  for (auto& media : *info.tcvrState()->settings()->mediaInterface()) {
    EXPECT_EQ(
        *media.media()->extendedSpecificationComplianceCode(),
        ExtendedSpecComplianceCode::CWDM4_100G);
    EXPECT_EQ(media.code(), MediaInterfaceCode::CWDM4_100G);
  }

  EXPECT_EQ(100, info.tcvrState()->cable().value_or({}).om3().value_or({}));
  EXPECT_EQ(
      true, info.tcvrState()->status().value_or({}).interruptL().value_or({}));

  utility::HwTransceiverUtils::verifyDiagsCapability(
      *info.tcvrState(),
      transceiverManager_->getDiagsCapability(xcvrID),
      false /* skipCheckingIndividualCapability */);

  // Using TransceiverTestsHelper to verify TransceiverInfo
  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETEST");
  tests.verifyTemp(31.015625);
  tests.verifyVcc(3.2989);
  std::map<std::string, std::vector<double>> laneDom = {
      {"TxBias", {10.28, 8.738, 17.476, 26.214}},
      {"TxPwr", {1.6705, 1.6962, 1.7219, 1.7476}},
      {"RxPwr", {0.4643, 0.9012, 0.4913, 0.4626}},
  };
  tests.verifyLaneDom(laneDom, xcvr->numMediaLanes());

  std::map<std::string, std::vector<bool>> expectedMediaSignals = {
      {"Tx_Los", {0, 1, 0, 1}},
      {"Rx_Los", {1, 1, 0, 1}},
      {"Tx_Lol", {0, 0, 1, 1}},
      {"Rx_Lol", {1, 0, 1, 1}},
      {"Tx_Fault", {0, 0, 1, 1}},
      {"Tx_AdaptFault", {1, 1, 0, 1}},
  };
  tests.verifyLaneSignals(
      expectedMediaSignals, xcvr->numHostLanes(), xcvr->numMediaLanes());

  std::map<std::string, std::vector<bool>> expectedMediaLaneSettings = {
      {"TxDisable", {0, 1, 1, 1}},
      {"TxSqDisable", {0, 0, 1, 1}},
      {"TxAdaptEqCtrl", {1, 0, 1, 1}},
  };
  std::map<std::string, std::vector<uint8_t>> expectedHostLaneSettings = {
      {"RxOutDisable", {1, 1, 0, 0}},
      {"RxSqDisable", {0, 1, 0, 0}},
      {"TxInputEq", {1, 2, 3, 4}},
      {"RxOutputEmph", {2, 1, 1, 3}},
      {"RxOutputAmp", {1, 3, 2, 3}},
  };
  auto settings = info.tcvrState()->settings().value_or({});
  tests.verifyMediaLaneSettings(
      expectedMediaLaneSettings, xcvr->numMediaLanes());
  tests.verifyHostLaneSettings(expectedHostLaneSettings, xcvr->numHostLanes());

  std::map<std::string, std::vector<bool>> laneInterrupts = {
      {"TxPwrHighAlarm", {1, 0, 0, 0}},
      {"TxPwrLowAlarm", {1, 1, 0, 0}},
      {"TxPwrHighWarn", {0, 0, 0, 1}},
      {"TxPwrLowWarn", {0, 1, 0, 1}},
      {"RxPwrHighAlarm", {1, 0, 1, 0}},
      {"RxPwrLowAlarm", {1, 1, 1, 0}},
      {"RxPwrHighWarn", {0, 0, 1, 1}},
      {"RxPwrLowWarn", {0, 1, 1, 1}},
      {"TxBiasHighAlarm", {1, 0, 0, 1}},
      {"TxBiasLowAlarm", {1, 1, 0, 1}},
      {"TxBiasHighWarn", {0, 0, 1, 0}},
      {"TxBiasLowWarn", {0, 1, 1, 0}},
  };
  tests.verifyLaneInterrupts(laneInterrupts, xcvr->numMediaLanes());

  tests.verifyGlobalInterrupts("temp", 0, 0, 0, 0);
  tests.verifyGlobalInterrupts("vcc", 0, 0, 1, 1);

  tests.verifyThresholds("temp", 75, -5, 70, 0);
  tests.verifyThresholds("vcc", 3.80, 2.84, 3.45, 3.15);
  tests.verifyThresholds("rxPwr", 0.6683, 0.7197, 0.7711, 0.437);
  tests.verifyThresholds("txPwr", 0.4386, 1.3124, 2.1862, 3.06);
  tests.verifyThresholds("txBias", 0.034, 17.51, 34.986, 52.462);

  // This test will write registers, save it to the last
  testCachedMediaSignals(xcvr);

  TransceiverPortState goodPortState1{
      "", 0, cfg::PortSpeed::HUNDREDG, 4, TransmitterTechnology::OPTICAL};
  TransceiverPortState goodPortState2{
      "", 0, cfg::PortSpeed::FORTYG, 4, TransmitterTechnology::OPTICAL};
  for (auto portState : {goodPortState1, goodPortState2}) {
    EXPECT_TRUE(xcvr->tcvrPortStateSupported(portState));
  }

  TransceiverPortState badPortState1{
      "",
      0,
      cfg::PortSpeed::HUNDREDG,
      1,
      TransmitterTechnology::COPPER}; // Copper not supported
  TransceiverPortState badPortState2{
      "",
      1,
      cfg::PortSpeed::XG,
      2,
      TransmitterTechnology::OPTICAL}; // Invalid speed
  TransceiverPortState badPortState3{
      "",
      0,
      cfg::PortSpeed::FORTYG,
      3,
      TransmitterTechnology::OPTICAL}; // Invalid num lanes
  for (auto portState : {badPortState1, badPortState2, badPortState3}) {
    EXPECT_FALSE(xcvr->tcvrPortStateSupported(portState));
  }
  EXPECT_TRUE(info.tcvrState()->errorStates()->empty());
}

// Tests that a SFF DAC module can properly refresh
TEST_F(SffTest, dacTransceiverInfoTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideSffModule<SffDacTransceiver>(xcvrID);

  // Verify SffModule logic
  EXPECT_EQ(xcvr->numHostLanes(), 4);
  EXPECT_EQ(xcvr->numMediaLanes(), 4);

  // Verify getTransceiverInfo() result
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_EQ(
      *info.tcvrState()->extendedSpecificationComplianceCode(),
      ExtendedSpecComplianceCode::CR4_100G);
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(), MediaInterfaceCode::CR4_100G);
  for (auto& media : *info.tcvrState()->settings()->mediaInterface()) {
    EXPECT_EQ(
        *media.media()->extendedSpecificationComplianceCode(),
        ExtendedSpecComplianceCode::CR4_100G);
    EXPECT_EQ(media.code(), MediaInterfaceCode::CR4_100G);
  }

  utility::HwTransceiverUtils::verifyDiagsCapability(
      *info.tcvrState(),
      transceiverManager_->getDiagsCapability(xcvrID),
      false /* skipCheckingIndividualCapability */);

  // Using TransceiverTestsHelper to verify TransceiverInfo
  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETEST");

  // This test will write registers, save it to the last
  testCachedMediaSignals(xcvr);
  EXPECT_TRUE(info.tcvrState()->errorStates()->empty());
}

// Tests that a SFF Fr1 module can properly refresh
TEST_F(SffTest, fr1TransceiverInfoTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideSffModule<SffFr1Transceiver>(xcvrID);

  // Verify SffModule logic
  EXPECT_EQ(xcvr->numHostLanes(), 4);
  EXPECT_EQ(xcvr->numMediaLanes(), 1);

  // Verify getTransceiverInfo() result
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_EQ(
      *info.tcvrState()->extendedSpecificationComplianceCode(),
      ExtendedSpecComplianceCode::FR1_100G);
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(), MediaInterfaceCode::FR1_100G);
  for (auto& media : *info.tcvrState()->settings()->mediaInterface()) {
    EXPECT_EQ(
        media.media()->extendedSpecificationComplianceCode().value(),
        ExtendedSpecComplianceCode::FR1_100G);
    EXPECT_EQ(media.code(), MediaInterfaceCode::FR1_100G);
  }

  auto diagsCap = transceiverManager_->getDiagsCapability(xcvrID);
  utility::HwTransceiverUtils::verifyDiagsCapability(
      *info.tcvrState(),
      diagsCap,
      false /* skipCheckingIndividualCapability */);
  EXPECT_TRUE(diagsCap.has_value());
  std::vector<prbs::PrbsPolynomial> expectedCapabilities = {
      prbs::PrbsPolynomial::PRBS31};
  const auto& prbsSystemCaps = *diagsCap->prbsSystemCapabilities();
  EXPECT_TRUE(
      std::equal(
          prbsSystemCaps.begin(),
          prbsSystemCaps.end(),
          expectedCapabilities.begin()));
  const auto& prbsLineCaps = *diagsCap->prbsLineCapabilities();
  EXPECT_TRUE(
      std::equal(
          prbsLineCaps.begin(),
          prbsLineCaps.end(),
          expectedCapabilities.begin()));

  // Using TransceiverTestsHelper to verify TransceiverInfo
  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETEST");

  // This test will write registers, save it to the last
  testCachedMediaSignals(xcvr);
  EXPECT_TRUE(info.tcvrState()->errorStates()->empty());
}

// Tests that a miniphoton module can properly refresh
TEST_F(SffTest, miniphotonTransceiverInfoTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideSffModule<MiniphotonOBOTransceiver>(xcvrID);

  // Verify SffModule logic
  EXPECT_EQ(xcvr->numHostLanes(), 4);
  EXPECT_EQ(xcvr->numMediaLanes(), 4);

  // Verify getTransceiverInfo() result
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_EQ(
      *info.tcvrState()->extendedSpecificationComplianceCode(),
      ExtendedSpecComplianceCode::CWDM4_100G);
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(), MediaInterfaceCode::CWDM4_100G);
  for (auto& media : *info.tcvrState()->settings()->mediaInterface()) {
    EXPECT_EQ(
        media.media()->extendedSpecificationComplianceCode().value(),
        ExtendedSpecComplianceCode::CWDM4_100G);
    EXPECT_EQ(media.code(), MediaInterfaceCode::CWDM4_100G);
  }

  utility::HwTransceiverUtils::verifyDiagsCapability(
      *info.tcvrState(),
      transceiverManager_->getDiagsCapability(xcvrID),
      false /* skipCheckingIndividualCapability */);

  // Using TransceiverTestsHelper to verify TransceiverInfo
  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETEST");
  EXPECT_TRUE(info.tcvrState()->errorStates()->empty());
}

TEST_F(SffTest, unknownTransceiverInfoTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideSffModule<UnknownModuleIdentifierTransceiver>(xcvrID);

  // Verify SffModule logic
  EXPECT_EQ(xcvr->numHostLanes(), 4);
  EXPECT_EQ(xcvr->numMediaLanes(), 4);

  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_EQ(
      *info.tcvrState()->extendedSpecificationComplianceCode(),
      ExtendedSpecComplianceCode::CWDM4_100G);
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(), MediaInterfaceCode::CWDM4_100G);
  for (auto& media : *info.tcvrState()->settings()->mediaInterface()) {
    EXPECT_EQ(
        media.media()->extendedSpecificationComplianceCode().value(),
        ExtendedSpecComplianceCode::CWDM4_100G);
    EXPECT_EQ(media.code(), MediaInterfaceCode::CWDM4_100G);
  }

  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETEST");

  std::set<TransceiverErrorState> expectedErrorStates = {
      TransceiverErrorState::INVALID_IDENTIFIER};
  EXPECT_EQ(info.tcvrState()->errorStates(), expectedErrorStates);
}

// Tests that a badly programmed module throws an exception
TEST_F(SffTest, badTransceiverSimpleRead) {
  auto xcvr = overrideSffModule<BadSffCwdm4Transceiver>(
      TransceiverID(1), false /* willRefresh */);
  EXPECT_THROW(xcvr->refresh(), QsfpModuleError);
}

// Test that the CWDM4_100G temperature thresholds work correctly
TEST_F(SffTest, cwdm4TemperatureThresholds) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideSffModule<SffCwdm4TempTransceiver>(xcvrID);
  auto tempTransceiver =
      static_cast<SffCwdm4TempTransceiver*>(qsfpImpls_.back().get());

  // Test case 1: Temperature below warning threshold (50°C)
  tempTransceiver->setTemperature(50.0);
  transceiverManager_->refreshStateMachines();

  auto info = xcvr->getTransceiverInfo();
  TransceiverTestsHelper tests(info);
  tests.verifyTemp(50.0);
  // At 50°C, temperature is below both warning (55°C) and alarm (60°C)
  // thresholds, so no temperature flags should be set
  tests.verifyGlobalInterrupts("temp", false, false, false, false);

  // Test case 2: Temperature between warning and alarm thresholds (57°C)
  tempTransceiver->setTemperature(57.0);
  transceiverManager_->refreshStateMachines();

  info = xcvr->getTransceiverInfo();
  TransceiverTestsHelper tests2(info);
  tests2.verifyTemp(57.0);
  // At 57°C, temperature is above warning threshold (55°C) but below alarm
  // threshold (60°C), so only high temperature warning flag (3rd parameter)
  // should be set
  tests2.verifyGlobalInterrupts("temp", false, false, true, false);

  // Test case 3: Temperature above alarm threshold (62°C)
  tempTransceiver->setTemperature(62.0);
  transceiverManager_->refreshStateMachines();

  info = xcvr->getTransceiverInfo();
  TransceiverTestsHelper tests3(info);
  tests3.verifyTemp(62.0);
  // At 62°C, temperature is above both warning (55°C) and alarm (60°C)
  // thresholds, so both high temperature alarm (1st parameter) and warning (3rd
  // parameter) flags should be set
  tests3.verifyGlobalInterrupts("temp", true, false, true, false);

  // Test case 4: Temperature back below alarm but still above warning (58°C)
  tempTransceiver->setTemperature(58.0);
  transceiverManager_->refreshStateMachines();

  info = xcvr->getTransceiverInfo();
  TransceiverTestsHelper tests4(info);
  tests4.verifyTemp(58.0);
  // At 58°C, temperature is back below alarm threshold (60°C) but still above
  // warning threshold (55°C), so only high temperature warning flag (3rd
  // parameter) should be set
  tests4.verifyGlobalInterrupts("temp", false, false, true, false);

  // Test case 5: Temperature back below warning threshold (52°C)
  tempTransceiver->setTemperature(52.0);
  transceiverManager_->refreshStateMachines();

  info = xcvr->getTransceiverInfo();
  TransceiverTestsHelper tests5(info);
  tests5.verifyTemp(52.0);
  // At 52°C, temperature is back below both warning (55°C) and alarm (60°C)
  // thresholds, so no temperature flags should be set
  tests5.verifyGlobalInterrupts("temp", false, false, false, false);
}

TEST_F(SffTest, moduleEepromChecksumTest) {
  // Create SFF FR1 module
  auto xcvr100GFr1 = overrideSffModule<SffFr1Transceiver>(TransceiverID(1));
  // Verify EEPROM checksum for SFF FR1 module
  bool csumValid = xcvr100GFr1->verifyEepromChecksums();
  EXPECT_TRUE(csumValid);

  // Create CWDM4 module
  auto xcvrCwdm4 = overrideSffModule<SffCwdm4Transceiver>(TransceiverID(2));
  // Verify EEPROM checksum for CWDM4 module
  csumValid = xcvrCwdm4->verifyEepromChecksums();
  EXPECT_TRUE(csumValid);

  // Create CWDM4 Bad module
  auto xcvrCwdm4Bad =
      overrideSffModule<BadEepromSffCwdm4Transceiver>(TransceiverID(3));
  // Verify EEPROM checksum Invalid for CWDM4 Bad module
  csumValid = xcvrCwdm4Bad->verifyEepromChecksums();
  EXPECT_FALSE(csumValid);
}

class SfpTest : public TransceiverManagerTestHelper {
 public:
  template <typename XcvrImplT>
  Sff8472Module* overrideSfpModule(TransceiverID id) {
    qsfpImpls_.push_back(
        std::make_unique<XcvrImplT>(id, transceiverManager_.get()));
    // This override function use ids starting from 1
    transceiverManager_->overrideMgmtInterface(
        static_cast<int>(id) + 1,
        uint8_t(TransceiverModuleIdentifier::SFP_PLUS));
    auto portNames = transceiverManager_->getPortNames(id);
    auto tcvrName = transceiverManager_->getTransceiverName(id);
    auto xcvr = static_cast<Sff8472Module*>(
        transceiverManager_->overrideTransceiverForTesting(
            id,
            std::make_unique<Sff8472Module>(
                portNames, qsfpImpls_.back().get(), tcvrName)));
    // Refresh once to make sure the override transceiver finishes refresh
    transceiverManager_->refreshStateMachines();

    return xcvr;
  }
};

// Tests that a SFP module can properly refresh
TEST_F(SfpTest, sfp10GTransceiverInfoTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideSfpModule<Sfp10GTransceiver>(xcvrID);

  // Verify SffModule logic
  EXPECT_EQ(xcvr->numHostLanes(), 1);
  EXPECT_EQ(xcvr->numMediaLanes(), 1);

  // Verify getTransceiverInfo() result
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_EQ(
      (*info.tcvrState()->settings()->mediaInterface()).size(),
      xcvr->numMediaLanes());
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(), MediaInterfaceCode::LR_10G);
  for (auto& media : *info.tcvrState()->settings()->mediaInterface()) {
    EXPECT_EQ(
        media.media()->ethernet10GComplianceCode().value(),
        Ethernet10GComplianceCode::LR_10G);
    EXPECT_EQ(media.code(), MediaInterfaceCode::LR_10G);
  }

  utility::HwTransceiverUtils::verifyDiagsCapability(
      *info.tcvrState(),
      transceiverManager_->getDiagsCapability(xcvrID),
      false /* skipCheckingIndividualCapability */);

  // Using TransceiverTestsHelper to verify TransceiverInfo
  TransceiverTestsHelper tests(info);
  tests.verifyTemp(18.203125);
  tests.verifyVcc(2.2136);
  std::map<std::string, std::vector<double>> laneDom = {
      {"TxBias", {79.224}},
      {"TxPwr", {5.6849}},
      {"RxPwr", {0.8755}},
  };
  tests.verifyLaneDom(laneDom, xcvr->numMediaLanes());

  std::map<std::string, std::vector<bool>> expectedMediaSignals = {
      {"Rx_Los", {1}},
      {"Tx_Fault", {1}},
  };
  // Only check media signals, we don't have any host signals on sfp10g
  tests.verifyMediaLaneSignals(expectedMediaSignals, xcvr->numMediaLanes());

  std::map<std::string, std::vector<bool>> expectedMediaLaneSettings = {
      {"TxDisable", {1}},
  };
  tests.verifyMediaLaneSettings(
      expectedMediaLaneSettings, xcvr->numMediaLanes());
  tests.verifyVendorName("FACETEST");
  EXPECT_TRUE(info.tcvrState()->errorStates()->empty());
}

TEST_F(SfpTest, sfp10GBaseTTransceiverInfoTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideSfpModule<Sfp10GBaseTTransceiver>(xcvrID);

  // Verify SffModule logic
  EXPECT_EQ(xcvr->numHostLanes(), 1);
  EXPECT_EQ(xcvr->numMediaLanes(), 1);

  // Verify getTransceiverInfo() result
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_EQ(
      (*info.tcvrState()->settings()->mediaInterface()).size(),
      xcvr->numMediaLanes());
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(), MediaInterfaceCode::BASE_T_10G);
  for (auto& media : *info.tcvrState()->settings()->mediaInterface()) {
    EXPECT_EQ(
        media.media()->extendedSpecificationComplianceCode().value(),
        ExtendedSpecComplianceCode::BASE_T_10G);
    EXPECT_EQ(media.code(), MediaInterfaceCode::BASE_T_10G);
  }

  utility::HwTransceiverUtils::verifyDiagsCapability(
      *info.tcvrState(),
      transceiverManager_->getDiagsCapability(xcvrID),
      false /* skipCheckingIndividualCapability */);

  // Using TransceiverTestsHelper to verify TransceiverInfo
  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETEST");
  // Verify DOM is not read
  EXPECT_TRUE(info.tcvrStats()->channels()->empty());

  TransceiverPortState goodPortState{
      "", 0, cfg::PortSpeed::XG, 1, TransmitterTechnology::COPPER};
  EXPECT_TRUE(xcvr->tcvrPortStateSupported(goodPortState));

  TransceiverPortState badPortState1{
      "", 0, cfg::PortSpeed::HUNDREDG, 1, TransmitterTechnology::COPPER};
  TransceiverPortState badPortState2{
      "", 1, cfg::PortSpeed::XG, 1, TransmitterTechnology::COPPER};
  TransceiverPortState badPortState3{
      "", 0, cfg::PortSpeed::XG, 1, TransmitterTechnology::OPTICAL};
  for (auto portState : {badPortState1, badPortState2, badPortState3}) {
    EXPECT_FALSE(xcvr->tcvrPortStateSupported(portState));
  }
  EXPECT_TRUE(info.tcvrState()->errorStates()->empty());
}

TEST_F(SffTest, 200GCr4TransceiverInfoTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideSffModule<Sff200GCr4Transceiver>(xcvrID);

  // Verify SffModule logic
  EXPECT_EQ(xcvr->numHostLanes(), 4);
  EXPECT_EQ(xcvr->numMediaLanes(), 4);

  // Verify getTransceiverInfo() result
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_EQ(
      *info.tcvrState()->extendedSpecificationComplianceCode(),
      ExtendedSpecComplianceCode::CR_50G_CHANNELS);
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(), MediaInterfaceCode::CR4_200G);
  for (auto& media : *info.tcvrState()->settings()->mediaInterface()) {
    EXPECT_EQ(
        media.media()->extendedSpecificationComplianceCode().value(),
        ExtendedSpecComplianceCode::CR_50G_CHANNELS);
    EXPECT_EQ(media.code(), MediaInterfaceCode::CR4_200G);
  }

  utility::HwTransceiverUtils::verifyDiagsCapability(
      *info.tcvrState(),
      transceiverManager_->getDiagsCapability(xcvrID),
      false /* skipCheckingIndividualCapability */);

  // Using TransceiverTestsHelper to verify TransceiverInfo
  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETEST");

  TransceiverPortState goodPortState1{
      "", 0, cfg::PortSpeed::TWOHUNDREDG, 4, TransmitterTechnology::COPPER};
  TransceiverPortState goodPortState2{
      "", 2, cfg::PortSpeed::FIFTYG, 2, TransmitterTechnology::COPPER};
  TransceiverPortState goodPortState3{
      "", 1, cfg::PortSpeed::TWENTYFIVEG, 1, TransmitterTechnology::COPPER};
  for (auto portState : {goodPortState1, goodPortState2, goodPortState3}) {
    EXPECT_TRUE(xcvr->tcvrPortStateSupported(portState));
  }

  TransceiverPortState badPortState1{
      "",
      0,
      cfg::PortSpeed::HUNDREDG,
      4,
      TransmitterTechnology::OPTICAL}; // Optical not supported
  TransceiverPortState badPortState2{
      "",
      0,
      cfg::PortSpeed::FORTYG,
      3,
      TransmitterTechnology::OPTICAL}; // Optical not supported
  for (auto portState : {badPortState1, badPortState2}) {
    EXPECT_FALSE(xcvr->tcvrPortStateSupported(portState));
  }
  EXPECT_TRUE(info.tcvrState()->errorStates()->empty());
}

// SFF modules have no CMIS output loopbacks, so OUTPUT is rejected outright.
TEST_F(SffTest, setTransceiverLoopbackRejectsOutputMode) {
  auto xcvrID = TransceiverID(0);
  auto xcvr = overrideSffModule<SffCwdm4Transceiver>(xcvrID);
  auto portName = *transceiverManager_->getPortNames(xcvrID).begin();

  for (auto side : {phy::Side::SYSTEM, phy::Side::LINE}) {
    EXPECT_THROW(
        xcvr->setTransceiverLoopback(
            portName, side, true, phy::LoopbackMode::OUTPUT),
        FbossError);
  }
}
} // namespace facebook::fboss
