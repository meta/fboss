/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/qsfp_service/module/cmis/CmisHelper.h"
#include "fboss/qsfp_service/module/cmis/CmisModule.h"
#include "fboss/qsfp_service/module/cmis/gen-cpp2/cmis_types.h"
#include "fboss/qsfp_service/module/properties/TransceiverPropertiesManager.h"
#include "fboss/qsfp_service/module/tests/FakeTransceiverImpl.h"
#include "fboss/qsfp_service/module/tests/TransceiverTestsHelper.h"
#include "fboss/qsfp_service/test/TransceiverManagerTestHelper.h"
#include "fboss/qsfp_service/test/hw_test/HwTransceiverUtils.h"

namespace facebook::fboss {

class MockCmisModule : public CmisModule {
 public:
  template <typename XcvrImplT>
  explicit MockCmisModule(
      std::set<std::string> portNames,
      XcvrImplT* qsfpImpl,
      std::shared_ptr<const TransceiverConfig> cfgPtr,
      std::string tcvrName = "")
      : CmisModule(
            std::move(portNames),
            qsfpImpl,
            cfgPtr,
            true /*supportRemediate*/,
            tcvrName) {
    ON_CALL(*this, getModuleStateChanged).WillByDefault([this]() {
      // Only return true for the first read so that we can mimic the clear
      // on read register
      return (++moduleStateChangedReadTimes_) == 1;
    });
    ON_CALL(*this, ensureTransceiverReadyLocked(testing::_))
        .WillByDefault(testing::Return(true));
  }

  MOCK_METHOD0(getModuleStateChanged, bool());
  MOCK_METHOD1(ensureTransceiverReadyLocked, bool(bool));

  using CmisModule::configuredHostLanes;
  using CmisModule::configuredMediaLanes;
  using CmisModule::configureRxConsActHoldOffTimer;
  using CmisModule::disableTxRxSquelchForTunableOptics;
  using CmisModule::enableRxLfInsertionForTunableOptics;
  using CmisModule::frequencyGridToGridSelection;
  using CmisModule::getAllPortsResetLaneMasks;
  using CmisModule::getApplicationField;
  using CmisModule::getAppSelCodeForSpeed;
  using CmisModule::getBankedQsfpValuePtr;
  using CmisModule::getChannelNumFromFrequency;
  using CmisModule::getCmisRevision;
  using CmisModule::getCurrentAppSelCode;
  using CmisModule::getDiagSelLatchWaitUsec;
  using CmisModule::getInterfaceCodeForAppSel;
  using CmisModule::getLaneValuePtr;
  using CmisModule::getMaxNumBanks;
  using CmisModule::getQsfpValuePtr;
  using CmisModule::getTunableLaserStatus;
  using CmisModule::getVdmLaneValueF16;
  using CmisModule::getVdmLaneValuesF16;
  using CmisModule::getVdmLaneValuesU16;
  using CmisModule::isRxConsActHoldOffTmrImplSupported;
  using CmisModule::isRxConsActImplSupported;
  using CmisModule::isTunableOptics;
  using CmisModule::portDatapathStates_;
  using CmisModule::readModifyWriteCmisField;
  using CmisModule::resetDataPath;
  using CmisModule::triggerModuleReset;

 private:
  uint8_t moduleStateChangedReadTimes_{0};
};

class CmisTest : public TransceiverManagerTestHelper {
 public:
  template <typename XcvrImplT>
  MockCmisModule* overrideCmisModule(
      TransceiverID id,
      TransceiverModuleIdentifier identifier =
          TransceiverModuleIdentifier::QSFP_PLUS_CMIS) {
    qsfpImpls_.push_back(
        std::make_unique<XcvrImplT>(id, transceiverManager_.get()));
    // This override function use ids starting from 1
    transceiverManager_->overrideMgmtInterface(
        static_cast<int>(id) + 1, uint8_t(identifier));
    auto xcvr = static_cast<MockCmisModule*>(
        transceiverManager_->overrideTransceiverForTesting(
            id,
            std::make_unique<MockCmisModule>(
                transceiverManager_->getPortNames(id),
                qsfpImpls_.back().get(),
                tcvrConfig_,
                transceiverManager_->getTransceiverName(id))));

    // Refresh once to make sure the override transceiver finishes refresh
    transceiverManager_->refreshStateMachines();

    return xcvr;
  }

  // The fake EEPROM backing the most recently overridden transceiver.
  FakeTransceiverImpl* lastQsfpImpl() {
    return static_cast<FakeTransceiverImpl*>(qsfpImpls_.back().get());
  }
};

namespace {
// DIAG_SEL is byte 0 of upper page 14h.
constexpr int kPage14 = static_cast<int>(CmisPages::PAGE14);
constexpr int kDiagSelUpperPageOffset = 0;
constexpr uint8_t kDiagSelSnr =
    static_cast<uint8_t>(DiagnosticFeatureEncoding::SNR);
// VDM FreezeRequest is Page 2Fh byte 144 (upper-page offset 16).
constexpr int kPage2f = static_cast<int>(CmisPages::PAGE2F);
constexpr int kVdmLatchRequestUpperPageOffset = 144 - 128;

constexpr uint8_t kPage10h = 0x10;
constexpr uint8_t kPage11h = 0x11;
constexpr int kDataPathDeinitOffset = 128;
constexpr int kActiveAppSelOffset = 206;

void writeUpperPage(
    FakeTransceiverImpl* impl,
    uint8_t page,
    int offset,
    const std::vector<uint8_t>& data) {
  TransceiverAccessParameter pageParam(
      TransceiverAccessParameter::ADDR_QSFP, 127, 1);
  impl->writeTransceiver(pageParam, &page, 0, 0);
  TransceiverAccessParameter param(
      TransceiverAccessParameter::ADDR_QSFP,
      offset,
      static_cast<int>(data.size()));
  impl->writeTransceiver(param, data.data(), 0, 0);
}

std::vector<uint8_t>
readUpperPage(FakeTransceiverImpl* impl, uint8_t page, int offset, int len) {
  TransceiverAccessParameter pageParam(
      TransceiverAccessParameter::ADDR_QSFP, 127, 1);
  impl->writeTransceiver(pageParam, &page, 0, 0);
  std::vector<uint8_t> data(len);
  TransceiverAccessParameter param(
      TransceiverAccessParameter::ADDR_QSFP, offset, len);
  impl->readTransceiver(param, data.data(), 0);
  return data;
}

// Active control set for 3x200G: AppSel 9 on host lanes 0-5 with datapath IDs
// 0, 2 and 4; lanes 6-7 without an application
const std::vector<uint8_t> k3x200GActiveAppSel =
    {0x90, 0x90, 0x94, 0x94, 0x98, 0x98, 0x00, 0x00};

ProgramTransceiverState make3x200GZrProgramState() {
  ProgramTransceiverState programTcvrState;
  for (uint8_t startHostLane : {0, 2, 4}) {
    TransceiverPortState portState;
    portState.portName = fmt::format("eth1/1/{}", startHostLane + 1);
    portState.startHostLane = startHostLane;
    portState.speed = cfg::PortSpeed::TWOHUNDREDG;
    portState.numHostLanes = 2;
    cfg::OpticalChannelConfig optChanConfig;
    cfg::FrequencyConfig freqConfig;
    freqConfig.frequencyGrid() = FrequencyGrid::LASER_6P25GHZ;
    cfg::CenterFrequencyConfig centerFreq;
    centerFreq.set_frequencyMhz(CmisModule::kDefaultFrequencyMhz);
    freqConfig.centerFrequencyConfig() = centerFreq;
    optChanConfig.frequencyConfig() = freqConfig;
    optChanConfig.txPower0P01Dbm() = 0;
    optChanConfig.appSelCode() = 9;
    portState.opticalChannelConfig = optChanConfig;
    programTcvrState.ports.emplace(portState.portName, portState);
  }
  return programTcvrState;
}
} // namespace

// Existing (non-CPO) CMIS modules don't advertise a multi-bank capacity in
// Lower Page 00h byte 70, so getMaxNumBanks() must fall back to a single bank.
TEST_F(CmisTest, getMaxNumBanksDefaultsToOne) {
  EXPECT_EQ(
      overrideCmisModule<Cmis200GTransceiver>(TransceiverID(0))
          ->getMaxNumBanks(),
      1);
  EXPECT_EQ(
      overrideCmisModule<Cmis400GLr4Transceiver>(TransceiverID(1))
          ->getMaxNumBanks(),
      1);
  EXPECT_EQ(
      overrideCmisModule<CmisFlatMemTransceiver>(TransceiverID(2))
          ->getMaxNumBanks(),
      1);
}

// A single-bank module may only ever have bank 0 selected, and a 4-bank CPO is
// left on bank 0 once a refresh completes.
TEST_F(CmisTest, validBankSelectIsNotFlagged) {
  // The CPO fixture is not READY, so its zeroed page 11h separately trips
  // INVALID_DATA_PATH_LANE_STATE -- assert on bank select alone
  auto bankSelectFlagged = [](MockCmisModule* xcvr) {
    return xcvr->getTransceiverInfo().tcvrState()->errorStates()->count(
        TransceiverErrorState::INVALID_BANK_SELECT);
  };

  EXPECT_EQ(
      bankSelectFlagged(
          overrideCmisModule<Cmis200GTransceiver>(TransceiverID(0))),
      0);

  auto cpo = overrideCmisModule<CmisCpo6P4TDrTransceiver>(
      TransceiverID(1), TransceiverModuleIdentifier::CPO);
  ASSERT_EQ(cpo->getMaxNumBanks(), 4);
  EXPECT_EQ(bankSelectFlagged(cpo), 0);
}

// A single-bank module holding bank 2 is what the fleet-wide
// qsfp.numModulesWithInvalidBankSelect counter is looking for.
TEST_F(CmisTest, outOfRangeBankSelectIsFlagged) {
  auto xcvr = overrideCmisModule<Cmis200GInvalidBankSelectTransceiver>(
      TransceiverID(0));
  ASSERT_EQ(xcvr->getMaxNumBanks(), 1);
  std::set<TransceiverErrorState> expectedErrorStates = {
      TransceiverErrorState::INVALID_BANK_SELECT};
  EXPECT_EQ(
      xcvr->getTransceiverInfo().tcvrState()->errorStates(),
      expectedErrorStates);
}

// A CPO module reports identifier 0x80 and 4 banks (Lower Page 00h byte 70).
// Its first application advertises a per-bank 2x800G-DR4 (media 0x77, host
// 0x82), identical to a DR4_2x800G module; the bank count is what
// disambiguates it, yielding MediaInterfaceCode::DR4_8x800G.
TEST_F(CmisTest, cpoModuleIdentifiedByBankCount) {
  auto xcvr = overrideCmisModule<CmisCpo6P4TDrTransceiver>(
      TransceiverID(0), TransceiverModuleIdentifier::CPO);
  EXPECT_EQ(xcvr->getMaxNumBanks(), 4);

  // A 4-bank CPO presents all banks as a single 32-lane (4 x 8) transceiver.
  EXPECT_EQ(xcvr->numHostLanes(), 32);
  EXPECT_EQ(xcvr->numMediaLanes(), 32);

  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(), MediaInterfaceCode::DR4_8x800G);

  // Verify the 5 advertised application modes (per the CPO datapath
  // application mode table).
  struct ExpectedApp {
    SMFMediaInterfaceCode media;
    uint8_t host;
    int hostLaneCount;
    int mediaLaneCount;
    uint8_t apSelCode;
    std::vector<int> startLanes;
  };
  const std::vector<ExpectedApp> expectedApps = {
      {SMFMediaInterfaceCode::DR4_800G, 0x82, 4, 4, 1, {0, 4}},
      {SMFMediaInterfaceCode::DR2_400G, 0x81, 2, 2, 2, {0, 2, 4, 6}},
      {SMFMediaInterfaceCode::DR1_200G,
       0x80,
       1,
       1,
       3,
       {0, 1, 2, 3, 4, 5, 6, 7}},
      {SMFMediaInterfaceCode::DR4_400G, 0x50, 4, 4, 4, {0, 4}},
      {SMFMediaInterfaceCode::DR1_100G,
       0x4b,
       1,
       1,
       5,
       {0, 1, 2, 3, 4, 5, 6, 7}},
  };
  for (const auto& app : expectedApps) {
    auto field = xcvr->getApplicationField(static_cast<uint8_t>(app.media), 0);
    ASSERT_NE(field, std::nullopt);
    EXPECT_EQ(field->moduleMediaInterface, static_cast<uint8_t>(app.media));
    EXPECT_EQ(field->moduleHostInterface, app.host);
    EXPECT_EQ(field->hostLaneCount, app.hostLaneCount);
    EXPECT_EQ(field->mediaLaneCount, app.mediaLaneCount);
    EXPECT_EQ(field->ApSelCode, app.apSelCode);
    EXPECT_EQ(field->hostStartLanes, app.startLanes);
    EXPECT_EQ(field->mediaStartLanes, app.startLanes);
  }
}

// updateQsfpData reads banked page 11h for every bank on a CPO module. The fake
// gives each bank a distinct page 11h (byte 2 = bank index), so verify each
// bank's cached copy is the one that was read.
TEST_F(CmisTest, cpoReadsAllBanksOfBankedPage) {
  auto xcvr = overrideCmisModule<CmisCpo6P4TDrTransceiver>(
      TransceiverID(0), TransceiverModuleIdentifier::CPO);
  ASSERT_EQ(xcvr->getMaxNumBanks(), 4);

  // Page 11h byte 2 (absolute offset 128 + 2) is the per-bank marker.
  const int page11 = static_cast<int>(CmisPages::PAGE11);
  for (uint8_t bank = 0; bank < 4; ++bank) {
    const uint8_t* data = xcvr->getBankedQsfpValuePtr(
        page11, QsfpModule::MAX_QSFP_PAGE_SIZE + 2, 1, bank);
    EXPECT_EQ(data[0], bank);
  }
}

// On a READY multi-bank (CPO) module, page 14h (SNR diagnostics) is read for
// every bank: DIAG_SEL=SNR is written under each bank's selection, then 14h is
// read into that bank's buffer. The READY fixture marks each bank's MEDIA_SNR
// byte with the bank index, so verify each bank's cached copy.
TEST_F(CmisTest, cpoReadsSnrDiagPagePerBank) {
  auto xcvr = overrideCmisModule<CmisCpo6P4TDrReadyTransceiver>(
      TransceiverID(0), TransceiverModuleIdentifier::CPO);
  ASSERT_EQ(xcvr->getMaxNumBanks(), 4);

  // MEDIA_SNR is page 14h, absolute offset 240; the fake marks its first byte
  // with the bank index.
  const int page14 = static_cast<int>(CmisPages::PAGE14);
  for (uint8_t bank = 0; bank < 4; ++bank) {
    const uint8_t* data = xcvr->getBankedQsfpValuePtr(page14, 240, 1, bank);
    EXPECT_EQ(data[0], bank);
  }
}

// Only the Arista XDR4 part numbers need the long wait after DIAG_SEL changes;
// every other part number gets the 10ms the CMIS spec allows for. Both XDR4
// fixtures are real EEPROM dumps, so the part number the gate matches on is the
// one the module actually reports.
TEST_F(CmisTest, diagSelLatchWaitIsGatedOnPartNumber) {
  auto xdr4 = overrideCmisModule<CmisArista400GXdr4Transceiver>(
      TransceiverID(0), TransceiverModuleIdentifier::QSFP_DD);
  EXPECT_EQ(xdr4->getPartNumber(), "QDD-400G-XDR4");
  EXPECT_EQ(
      xdr4->getDiagSelLatchWaitUsec(),
      CmisModule::kUsecDiagSelectLatchWaitSlow);

  auto xdr4x2 = overrideCmisModule<CmisArista2x400GXdr4Transceiver>(
      TransceiverID(1), TransceiverModuleIdentifier::OSFP);
  EXPECT_EQ(xdr4x2->getPartNumber(), "FB-P800G-2XDR4-1");
  EXPECT_EQ(
      xdr4x2->getDiagSelLatchWaitUsec(),
      CmisModule::kUsecDiagSelectLatchWaitSlow);

  auto zr = overrideCmisModule<Cmis800GZrTransceiver>(
      TransceiverID(2), TransceiverModuleIdentifier::OSFP);
  EXPECT_EQ(zr->getPartNumber(), "DP08SFP8-ZRB-29B");
  EXPECT_EQ(
      zr->getDiagSelLatchWaitUsec(), CmisModule::kUsecDiagSelectLatchWait);
}

// Both XDR4 dumps were taken while qsfp_service had the module selected on SNR,
// so DIAG_SEL already reads back as 6 and refreshing must not rewrite it -- the
// steady state on a deployed module never pays the 100ms.
//
// Page 14h offset 240 holds the per-lane Rx SNR as a U16 with a 1/256 dB LSB
// (CMIS 5.2 Table 8-95), so lane 0 of the 400G dump (bytes d7 15) is
// 0x15d7 / 256 = 21.84 dB.
TEST_F(CmisTest, aristaXdr4RefreshLeavesDiagSelAloneAndReadsSnr) {
  auto xcvr = overrideCmisModule<CmisArista400GXdr4Transceiver>(
      TransceiverID(0), TransceiverModuleIdentifier::QSFP_DD);
  auto* qsfpImpl = lastQsfpImpl();

  EXPECT_EQ(
      qsfpImpl->getUpperPageWriteCount(kPage14, kDiagSelUpperPageOffset), 0);
  transceiverManager_->refreshStateMachines();
  EXPECT_EQ(
      qsfpImpl->getUpperPageWriteCount(kPage14, kDiagSelUpperPageOffset), 0);

  const std::vector<uint16_t> expectedRawSnr = {0x15d7, 0x1531, 0x151a, 0x15e1};
  const auto& info = xcvr->getTransceiverInfo();
  const auto& channels = *info.tcvrStats()->channels();
  ASSERT_EQ(channels.size(), expectedRawSnr.size());
  for (size_t lane = 0; lane < expectedRawSnr.size(); ++lane) {
    EXPECT_NEAR(
        *channels[lane].sensors()->rxSnr()->value(),
        expectedRawSnr[lane] / 256.0,
        0.001);
  }
}

// Reading page 14h right after writing DIAG_SEL races the module and yields
// zeros, so the refresh path waits for the module to repopulate the page. The
// wait is only owed when the selection changes, so DIAG_SEL is written once and
// then left alone: the fixture starts at DIAG_SEL=NONE, so the first refresh
// writes SNR and a second refresh finds SNR already selected and skips.
TEST_F(CmisTest, snrDiagSelWrittenOnceAcrossRefreshes) {
  auto xcvr = overrideCmisModule<Cmis2x400GDr4Transceiver>(TransceiverID(0));
  ASSERT_EQ(xcvr->getMaxNumBanks(), 1);
  auto* qsfpImpl = lastQsfpImpl();

  const uint8_t* diagSel = xcvr->getQsfpValuePtr(
      kPage14, QsfpModule::MAX_QSFP_PAGE_SIZE + kDiagSelUpperPageOffset, 1);
  EXPECT_EQ(diagSel[0], kDiagSelSnr);
  EXPECT_EQ(
      qsfpImpl->getUpperPageWriteCount(kPage14, kDiagSelUpperPageOffset), 1);

  transceiverManager_->refreshStateMachines();
  EXPECT_EQ(
      qsfpImpl->getUpperPageWriteCount(kPage14, kDiagSelUpperPageOffset), 1);
}

// DIAG_SEL lives on banked page 14h, so on a multi-bank (CPO) module every bank
// carries its own copy and each must be selected before that bank's 14h read.
// The fixture starts every bank at DIAG_SEL=NONE, so the first refresh writes
// once per bank and a second refresh writes nothing.
TEST_F(CmisTest, cpoSnrDiagSelWrittenOncePerBank) {
  auto xcvr = overrideCmisModule<CmisCpo6P4TDrReadyTransceiver>(
      TransceiverID(0), TransceiverModuleIdentifier::CPO);
  ASSERT_EQ(xcvr->getMaxNumBanks(), 4);
  auto* qsfpImpl = lastQsfpImpl();

  for (uint8_t bank = 0; bank < 4; ++bank) {
    const uint8_t* diagSel = xcvr->getBankedQsfpValuePtr(
        kPage14,
        QsfpModule::MAX_QSFP_PAGE_SIZE + kDiagSelUpperPageOffset,
        1,
        bank);
    EXPECT_EQ(diagSel[0], kDiagSelSnr);
  }
  EXPECT_EQ(
      qsfpImpl->getUpperPageWriteCount(kPage14, kDiagSelUpperPageOffset), 4);

  transceiverManager_->refreshStateMachines();
  EXPECT_EQ(
      qsfpImpl->getUpperPageWriteCount(kPage14, kDiagSelUpperPageOffset), 4);
}

// On a READY VDM-capable multi-bank (CPO) module, the VDM data page (24h) is
// read for every bank. The READY fixture marks each bank's page 24h byte 0 with
// the bank index, so verify each bank's cached copy.
TEST_F(CmisTest, cpoReadsVdmDataPagePerBank) {
  auto xcvr = overrideCmisModule<CmisCpo6P4TDrReadyTransceiver>(
      TransceiverID(0), TransceiverModuleIdentifier::CPO);
  ASSERT_EQ(xcvr->getMaxNumBanks(), 4);
  ASSERT_TRUE(xcvr->isVdmSupported());

  // Page 24h byte 0 (absolute offset 128) is the per-bank marker.
  const int page24 = static_cast<int>(CmisPages::PAGE24);
  for (uint8_t bank = 0; bank < 4; ++bank) {
    const uint8_t* data = xcvr->getBankedQsfpValuePtr(
        page24, QsfpModule::MAX_QSFP_PAGE_SIZE, 1, bank);
    EXPECT_EQ(data[0], bank);
  }
}

// getVdmLaneValues keys per-lane VDM values by GLOBAL lane (bank * 8 + intra)
// across all banks. The READY fixture configures SNR_MEDIA_IN on data page 24h
// and marks each bank's page 24h byte 0 (lane 0's high byte) with the bank
// index, so lane 0 of bank N decodes (byte0 + byte1/256) to N.
TEST_F(CmisTest, cpoGetVdmLaneValuesKeyedByGlobalLane) {
  auto xcvr = overrideCmisModule<CmisCpo6P4TDrReadyTransceiver>(
      TransceiverID(0), TransceiverModuleIdentifier::CPO);
  ASSERT_EQ(xcvr->getMaxNumBanks(), 4);
  ASSERT_TRUE(xcvr->isVdmSupported());

  auto snr = xcvr->getVdmLaneValuesU16(SNR_MEDIA_IN);

  // 8 configured lanes per bank across all 4 banks -> 32 global lanes.
  EXPECT_EQ(snr.size(), 4 * CmisModule::kMaxOsfpNumLanes);
  for (int bank = 0; bank < 4; ++bank) {
    int firstLaneOfBank = bank * CmisModule::kMaxOsfpNumLanes;
    ASSERT_TRUE(snr.find(firstLaneOfBank) != snr.end());
    EXPECT_EQ(snr.at(firstLaneOfBank), static_cast<double>(bank));
  }
}

// getLaneValuePtr maps a global lane to (bank = lane/8, intra = lane%8) and
// returns that bank's per-lane bytes. The fixture marks CHANNEL_RX_PWR's first
// byte (page 11h) with the bank index, so the first lane of each bank should
// resolve to its bank index.
TEST_F(CmisTest, cpoGetLaneValuePtrMapsGlobalLaneToBank) {
  auto xcvr = overrideCmisModule<CmisCpo6P4TDrTransceiver>(
      TransceiverID(0), TransceiverModuleIdentifier::CPO);
  ASSERT_EQ(xcvr->getMaxNumBanks(), 4);

  // Intra-bank lane 0 exercises bank selection: the fake marks byte 0 of
  // CHANNEL_RX_PWR with the bank id.
  for (uint8_t bank = 0; bank < 4; ++bank) {
    int firstLaneOfBank = bank * CmisModule::kMaxOsfpNumLanes;
    const uint8_t* data = xcvr->getLaneValuePtr(
        CmisField::CHANNEL_RX_PWR, firstLaneOfBank, /*bytesPerLane=*/2);
    EXPECT_EQ(data[0], bank);
  }

  // Intra-bank lane 1 exercises the offset + intraLane * bytesPerLane math: the
  // fake marks lane 1 of CHANNEL_RX_PWR with 0x10|bank (banks 1-3).
  for (uint8_t bank = 1; bank < 4; ++bank) {
    int secondLaneOfBank = bank * CmisModule::kMaxOsfpNumLanes + 1;
    const uint8_t* data = xcvr->getLaneValuePtr(
        CmisField::CHANNEL_RX_PWR, secondLaneOfBank, /*bytesPerLane=*/2);
    EXPECT_EQ(data[0], 0x10 | bank);
  }
}

// A 4-bank CPO presents 32 lanes, so getTransceiverInfo must report per-lane
// settings/interfaces for all 32 (4 banks x 8) lanes, not just one bank's 8.
TEST_F(CmisTest, cpoTransceiverInfoReportsAllBankLanes) {
  auto xcvr = overrideCmisModule<CmisCpo6P4TDrReadyTransceiver>(
      TransceiverID(0), TransceiverModuleIdentifier::CPO);
  ASSERT_EQ(xcvr->getMaxNumBanks(), 4);

  const auto& info = xcvr->getTransceiverInfo();
  const auto& settings = *info.tcvrState()->settings();
  EXPECT_EQ(settings.mediaInterface()->size(), 32);
  EXPECT_EQ(settings.mediaLaneSettings()->size(), 32);
  EXPECT_EQ(settings.hostLaneSettings()->size(), 32);
}

// Program the datapath for two ports that live in different banks of a CPO
// module and confirm each lands in its own bank: the application select code
// and datapath init are written under the port's bank, leaving the other banks
// untouched. The fake routes per-lane banked-page writes (and its datapath
// state simulation) to the selected bank, so a correctly bank-targeted program
// activates only that bank's lanes.
TEST_F(CmisTest, cpoMultiPortDatapathProgram) {
  auto xcvr = overrideCmisModule<CmisCpo6P4TDrReadyTransceiver>(
      TransceiverID(0), TransceiverModuleIdentifier::CPO);
  ASSERT_EQ(xcvr->getMaxNumBanks(), 4);
  ASSERT_EQ(xcvr->numHostLanes(), 32);

  // Two 400G ports in different banks. The platform mapping supplies each port
  // with a global startHostLane (bank 0 -> lanes 0-3, bank 1 -> lanes 8-11),
  // and the module derives the bank as lane / kMaxOsfpNumLanes. Each is a
  // 2x400G-DR4 port within its bank. The fixture comes up in the per-bank 800G
  // application, so requesting 400G forces a real datapath reprogram (rather
  // than the "speed already matches" no-op).
  ProgramTransceiverState programTcvrState;
  TransceiverPortState bank0Port;
  bank0Port.portName = "eth1/1/1";
  bank0Port.startHostLane = 0;
  bank0Port.speed = cfg::PortSpeed::FOURHUNDREDG;
  bank0Port.numHostLanes = 4;
  programTcvrState.ports.emplace(bank0Port.portName, bank0Port);

  TransceiverPortState bank1Port;
  bank1Port.portName = "eth1/1/9";
  bank1Port.startHostLane = 8;
  bank1Port.speed = cfg::PortSpeed::FOURHUNDREDG;
  bank1Port.numHostLanes = 4;
  programTcvrState.ports.emplace(bank1Port.portName, bank1Port);

  // Capture the pre-program datapath state of the unprogrammed banks (2 and 3,
  // lanes 16-31) so we can prove programming banks 0 and 1 leaves them
  // untouched, without hardcoding the fixture's exact initial state.
  transceiverManager_->refreshStateMachines();
  std::map<int, CmisLaneState> preStateBanks23;
  {
    const auto& preInfo = xcvr->getTransceiverInfo();
    const auto& preSignals = *preInfo.tcvrState()->hostLaneSignals();
    ASSERT_EQ(preSignals.size(), 32);
    for (int lane = 16; lane < 32; ++lane) {
      preStateBanks23[lane] = *preSignals[lane].cmisLaneState();
    }
  }

  xcvr->programTransceiver(programTcvrState, false);

  // Refresh so the per-bank datapath state written during programming is read
  // back into the cache that getTransceiverInfo reports.
  transceiverManager_->refreshStateMachines();
  const auto& info = xcvr->getTransceiverInfo();
  const auto& hostLaneSignals = *info.tcvrState()->hostLaneSignals();
  ASSERT_EQ(hostLaneSignals.size(), 32);

  // The four lanes of each programmed port (bank 0: 0-3, bank 1: 8-11) are
  // activated.
  for (int lane : {0, 1, 2, 3, 8, 9, 10, 11}) {
    EXPECT_EQ(hostLaneSignals[lane].cmisLaneState(), CmisLaneState::ACTIVATED)
        << "lane " << lane << " should be ACTIVATED";
  }
  // Every lane of the unprogrammed banks 2 and 3 is unchanged from before the
  // program and is not activated -- proving the program targeted only the
  // ports' banks.
  for (int lane = 16; lane < 32; ++lane) {
    EXPECT_EQ(hostLaneSignals[lane].cmisLaneState(), preStateBanks23[lane])
        << "lane " << lane << " (unprogrammed bank) should be unchanged";
    EXPECT_NE(hostLaneSignals[lane].cmisLaneState(), CmisLaneState::ACTIVATED)
        << "lane " << lane << " should not be ACTIVATED";
  }
}

// The host->media lane mapping is per-bank: module capabilities advertise
// intra-bank lane assignments, but a port's lanes carry a global bank offset.
// For the per-bank 2x800G-DR4 application (host/media start lanes {0,4}), a
// port in bank N must report host and media lanes offset by N*8.
TEST_F(CmisTest, cpoConfiguredMediaLanesPerBank) {
  auto xcvr = overrideCmisModule<CmisCpo6P4TDrReadyTransceiver>(
      TransceiverID(0), TransceiverModuleIdentifier::CPO);
  ASSERT_EQ(xcvr->getMaxNumBanks(), 4);
  xcvr->getTransceiverInfo();

  // Host lanes are global and expand from the start lane.
  EXPECT_EQ(xcvr->configuredHostLanes(0), (std::vector<uint8_t>{0, 1, 2, 3}));
  EXPECT_EQ(xcvr->configuredHostLanes(8), (std::vector<uint8_t>{8, 9, 10, 11}));

  // Media lanes are offset into the port's bank (bank = hostStartLane / 8).
  // bank 0: host {0,4} -> media {0-3},{4-7}; bank 1: host {8,12} -> media
  // {8-11},{12-15}.
  EXPECT_EQ(xcvr->configuredMediaLanes(0), (std::vector<uint8_t>{0, 1, 2, 3}));
  EXPECT_EQ(xcvr->configuredMediaLanes(4), (std::vector<uint8_t>{4, 5, 6, 7}));
  EXPECT_EQ(
      xcvr->configuredMediaLanes(8), (std::vector<uint8_t>{8, 9, 10, 11}));
  EXPECT_EQ(
      xcvr->configuredMediaLanes(12), (std::vector<uint8_t>{12, 13, 14, 15}));
}

// The CMIS revision lives in Lower Page byte 1: upper nibble is the major
// number, lower nibble the minor. The 200G FR4 fixture has 0x40 and the 800G
// ZR fixture has 0x53.
TEST_F(CmisTest, getCmisRevision) {
  const std::pair<uint8_t, uint8_t> expected200G{4, 0};
  EXPECT_EQ(
      overrideCmisModule<Cmis200GTransceiver>(TransceiverID(0))
          ->getCmisRevision(),
      expected200G);

  const std::pair<uint8_t, uint8_t> expected800GZr{5, 3};
  EXPECT_EQ(
      overrideCmisModule<Cmis800GZrTransceiver>(
          TransceiverID(1), TransceiverModuleIdentifier::OSFP)
          ->getCmisRevision(),
      expected800GZr);
}

// ModuleState lives in bits 1-3 of Lower Page byte 3; bits 4-7 are reserved
// and real modules do set them. Those bits have to be masked off, not merely
// shifted past, or they leak into CmisModuleState and produce values outside
// the enum. Byte 3 = 0x56 is READY with the reserved bits set.
TEST_F(CmisTest, moduleStateIgnoresReservedBitsOfStatusByte) {
  auto xcvr = overrideCmisModule<Cmis200GReservedStateBitsTransceiver>(
      TransceiverID(0));

  const auto& info = xcvr->getTransceiverInfo();
  auto status = info.tcvrState()->status();
  ASSERT_TRUE(status.has_value());
  ASSERT_TRUE(status->cmisModuleState().has_value());
  EXPECT_EQ(*status->cmisModuleState(), CmisModuleState::READY);

  // updateQsfpData() gates the SNR diag page and the VDM cache on the same
  // decode, so a module misread as not-ready silently loses that data. Page
  // 14h byte 0 is 0x06 in this fixture; it is only populated when the module
  // was seen as READY during the refresh.
  auto cmisData = xcvr->getDOMDataUnion().cmis().as_const().value();
  ASSERT_TRUE(cmisData.page14().has_value());
  EXPECT_EQ(cmisData.page14()->data()[0], 0x06);
}

// Tests that the transceiverInfo object is correctly populated
TEST_F(CmisTest, cmis200GTransceiverInfoTest) {
  auto xcvrID = TransceiverID(0);
  auto xcvr = overrideCmisModule<Cmis200GTransceiver>(xcvrID);

  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_TRUE(info.tcvrState()->transceiverManagementInterface());
  EXPECT_EQ(
      info.tcvrState()->transceiverManagementInterface(),
      TransceiverManagementInterface::CMIS);
  EXPECT_EQ(xcvr->numHostLanes(), 4);
  EXPECT_EQ(xcvr->numMediaLanes(), 4);
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(), MediaInterfaceCode::FR4_200G);
  EXPECT_EQ(
      xcvr->numMediaLanes(),
      info.tcvrState()->settings()->mediaInterface().value_or({}).size());
  for (auto& media : *info.tcvrState()->settings()->mediaInterface()) {
    EXPECT_EQ(
        media.media()->smfCode().value(), SMFMediaInterfaceCode::FR4_200G);
    EXPECT_EQ(media.code(), MediaInterfaceCode::FR4_200G);
  }
  testCachedMediaSignals(xcvr);
  // Check cmisStateChanged
  EXPECT_TRUE(
      info.tcvrState()->status() &&
      info.tcvrState()->status()->cmisStateChanged() &&
      *info.tcvrState()->status()->cmisStateChanged());

  utility::HwTransceiverUtils::verifyDiagsCapability(
      *info.tcvrState(),
      transceiverManager_->getDiagsCapability(xcvrID),
      false /* skipCheckingIndividualCapability */);

  // Verify update qsfp data logic
  if (auto status = info.tcvrState()->status();
      status && status->cmisModuleState()) {
    EXPECT_EQ(*status->cmisModuleState(), CmisModuleState::READY);
    auto cmisData = xcvr->getDOMDataUnion().cmis().as_const().value();
    // NOTE the following cmis data are specifically set to 0x11 in
    // FakeTransceiverImpl
    EXPECT_TRUE(cmisData.page14());
    // SNR will be set
    EXPECT_EQ(cmisData.page14()->data()[0], 0x06);
    // Check VDM cache
    EXPECT_FALSE(xcvr->isVdmSupported());
  } else {
    throw FbossError("Missing CMIS Module state");
  }

  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETEST");
  tests.verifyFwInfo("2.7", "1.10", "3.101");
  tests.verifyTemp(40.26953125);
  tests.verifyVcc(3.3020);

  std::map<std::string, std::vector<double>> laneDom = {
      {"TxBias", {48.442, 50.082, 53.516, 50.028}},
      {"TxPwr", {2.13, 2.0748, 2.0512, 2.1027}},
      {"RxPwr", {0.4032, 0.3969, 0.5812, 0.5176}},
  };
  tests.verifyLaneDom(laneDom, xcvr->numMediaLanes());

  // Optical modules populate the converted dBm power fields
  for (const auto& channel : *info.tcvrStats()->channels()) {
    EXPECT_TRUE(channel.sensors()->rxPwrdBm().has_value());
    EXPECT_TRUE(channel.sensors()->txPwrdBm().has_value());
  }

  std::map<std::string, std::vector<bool>> expectedMediaSignals = {
      {"Tx_Los", {1, 1, 0, 1}},
      {"Rx_Los", {1, 0, 1, 0}},
      {"Tx_Lol", {0, 0, 1, 1}},
      {"Rx_Lol", {0, 1, 1, 0}},
      {"Tx_Fault", {0, 1, 0, 1}},
      {"Tx_AdaptFault", {1, 0, 1, 1}},
  };
  tests.verifyLaneSignals(
      expectedMediaSignals, xcvr->numHostLanes(), xcvr->numMediaLanes());

  std::array<bool, 4> expectedDatapathDeinit = {0, 1, 1, 0};
  std::array<CmisLaneState, 4> expectedLaneState = {
      CmisLaneState::ACTIVATED,
      CmisLaneState::DATAPATHINIT,
      CmisLaneState::TX_ON,
      CmisLaneState::DEINIT};

  EXPECT_EQ(
      xcvr->numHostLanes(),
      info.tcvrState()->hostLaneSignals().value_or({}).size());
  for (auto& signal : *info.tcvrState()->hostLaneSignals()) {
    EXPECT_EQ(
        expectedDatapathDeinit[*signal.lane()],
        signal.dataPathDeInit().value_or({}));
    EXPECT_EQ(
        expectedLaneState[*signal.lane()], signal.cmisLaneState().value_or({}));
  }

  std::map<std::string, std::vector<bool>> expectedMediaLaneSettings = {
      {"TxDisable", {0, 1, 0, 1}},
      {"TxSqDisable", {1, 1, 0, 1}},
      {"TxForcedSq", {0, 0, 1, 1}},
  };

  std::map<std::string, std::vector<uint8_t>> expectedHostLaneSettings = {
      {"RxOutDisable", {1, 1, 0, 0}},
      {"RxSqDisable", {0, 0, 1, 1}},
      {"RxEqPrecursor", {2, 2, 2, 2}},
      {"RxEqPostcursor", {0, 0, 0, 0}},
      {"RxEqMain", {3, 3, 3, 3}},
      {"CurrentAppSel", {1, 1, 1, 1}},
  };

  auto settings = info.tcvrState()->settings().value_or({});
  tests.verifyMediaLaneSettings(
      expectedMediaLaneSettings, xcvr->numMediaLanes());
  tests.verifyHostLaneSettings(expectedHostLaneSettings, xcvr->numHostLanes());

  EXPECT_EQ(PowerControlState::HIGH_POWER_OVERRIDE, settings.powerControl());

  std::map<std::string, std::vector<bool>> laneInterrupts = {
      {"TxPwrHighAlarm", {0, 1, 0, 0}},
      {"TxPwrHighWarn", {0, 0, 1, 0}},
      {"TxPwrLowAlarm", {1, 1, 0, 0}},
      {"TxPwrLowWarn", {1, 0, 1, 0}},
      {"RxPwrHighAlarm", {0, 1, 0, 1}},
      {"RxPwrHighWarn", {0, 0, 1, 1}},
      {"RxPwrLowAlarm", {1, 1, 0, 1}},
      {"RxPwrLowWarn", {1, 0, 1, 1}},
      {"TxBiasHighAlarm", {0, 1, 1, 0}},
      {"TxBiasHighWarn", {0, 0, 0, 1}},
      {"TxBiasLowAlarm", {1, 1, 1, 0}},
      {"TxBiasLowWarn", {1, 0, 0, 1}},
  };
  tests.verifyLaneInterrupts(laneInterrupts, xcvr->numMediaLanes());
  tests.verifyGlobalInterrupts("temp", 1, 1, 0, 1);
  tests.verifyGlobalInterrupts("vcc", 1, 0, 1, 0);

  auto diagsCap = transceiverManager_->getDiagsCapability(xcvrID);
  EXPECT_TRUE(diagsCap.has_value());
  std::vector<prbs::PrbsPolynomial> expectedPolynomials = {
      prbs::PrbsPolynomial::PRBS31Q,
      prbs::PrbsPolynomial::PRBS23Q,
      prbs::PrbsPolynomial::PRBS15Q,
      prbs::PrbsPolynomial::PRBS13Q,
      prbs::PrbsPolynomial::PRBS9Q,
      prbs::PrbsPolynomial::PRBS7Q};

  auto linePrbsCapability = *(*diagsCap).prbsLineCapabilities();
  auto sysPrbsCapability = *(*diagsCap).prbsSystemCapabilities();
  tests.verifyPrbsPolynomials(expectedPolynomials, linePrbsCapability);
  tests.verifyPrbsPolynomials(expectedPolynomials, sysPrbsCapability);

  for (auto unsupportedApplication :
       {SMFMediaInterfaceCode::LR4_10_400G, SMFMediaInterfaceCode::FR4_400G}) {
    EXPECT_EQ(
        xcvr->getApplicationField(
            static_cast<uint8_t>(unsupportedApplication), 0),
        std::nullopt);
  }
  for (auto supportedApplication :
       {SMFMediaInterfaceCode::FR4_200G, SMFMediaInterfaceCode::CWDM4_100G}) {
    auto applicationField = xcvr->getApplicationField(
        static_cast<uint8_t>(supportedApplication), 0);
    EXPECT_NE(applicationField, std::nullopt);
    EXPECT_EQ(applicationField->hostStartLanes, std::vector<int>{0});
    EXPECT_EQ(applicationField->mediaStartLanes, std::vector<int>{0});
    for (uint8_t lane = 1; lane < 7; lane++) {
      EXPECT_EQ(
          xcvr->getApplicationField(
              static_cast<uint8_t>(supportedApplication), lane),
          std::nullopt);
    }
  }

  EXPECT_FALSE(diagsCap.value().vdm().value());
  EXPECT_TRUE(diagsCap.value().cdb().value());
  EXPECT_TRUE(diagsCap.value().prbsLine().value());
  EXPECT_TRUE(diagsCap.value().prbsSystem().value());
  EXPECT_TRUE(diagsCap.value().loopbackLine().value());
  EXPECT_TRUE(diagsCap.value().loopbackSystem().value());
  auto lbCap = diagsCap.value().loopbackCapability().value();
  EXPECT_TRUE(*lbCap.mediaSideOutput());
  EXPECT_TRUE(*lbCap.mediaSideInput());
  EXPECT_TRUE(*lbCap.hostSideOutput());
  EXPECT_TRUE(*lbCap.hostSideInput());
  EXPECT_FALSE(*lbCap.perLaneHostSide());
  EXPECT_FALSE(*lbCap.perLaneMediaSide());
  EXPECT_FALSE(*lbCap.simultaneousHostAndMediaSide());
  EXPECT_TRUE(diagsCap.value().txOutputControl().value());
  EXPECT_TRUE(diagsCap.value().rxOutputControl().value());
  EXPECT_FALSE(diagsCap.value().snrLine().value());
  EXPECT_FALSE(diagsCap.value().snrSystem().value());
  EXPECT_FALSE(xcvr->isVdmSupported());
  EXPECT_TRUE(xcvr->isPrbsSupported(phy::Side::LINE));
  EXPECT_TRUE(xcvr->isPrbsSupported(phy::Side::SYSTEM));
  EXPECT_FALSE(xcvr->isSnrSupported(phy::Side::LINE));
  EXPECT_FALSE(xcvr->isSnrSupported(phy::Side::SYSTEM));

  TransceiverPortState goodPortState1{
      "", 0, cfg::PortSpeed::TWOHUNDREDG, 4, TransmitterTechnology::OPTICAL};
  TransceiverPortState goodPortState2{
      "", 0, cfg::PortSpeed::HUNDREDG, 4, TransmitterTechnology::OPTICAL};
  for (auto portState : {goodPortState1, goodPortState2}) {
    EXPECT_TRUE(xcvr->tcvrPortStateSupported(portState));
  }

  TransceiverPortState badPortState1{
      "",
      0,
      cfg::PortSpeed::HUNDREDG,
      4,
      TransmitterTechnology::COPPER}; // Copper not supported
  TransceiverPortState badPortState2{
      "",
      0,
      cfg::PortSpeed::FOURHUNDREDG,
      8,
      TransmitterTechnology::OPTICAL}; // 400G not supported
  TransceiverPortState badPortState3{
      "",
      1,
      cfg::PortSpeed::HUNDREDG,
      4,
      TransmitterTechnology::OPTICAL}; // BAD START LANE
  for (auto portState : {badPortState1, badPortState2, badPortState3}) {
    EXPECT_FALSE(xcvr->tcvrPortStateSupported(portState));
  }
  EXPECT_TRUE(info.tcvrState()->errorStates()->empty());
}

TEST_F(CmisTest, cmis400GLr4TransceiverInfoTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis400GLr4Transceiver>(xcvrID);
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_TRUE(info.tcvrState()->transceiverManagementInterface());
  EXPECT_EQ(
      info.tcvrState()->transceiverManagementInterface(),
      TransceiverManagementInterface::CMIS);
  EXPECT_EQ(xcvr->numHostLanes(), 8);
  EXPECT_EQ(xcvr->numMediaLanes(), 4);
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(),
      MediaInterfaceCode::LR4_400G_10KM);
  for (auto& media : *info.tcvrState()->settings()->mediaInterface()) {
    EXPECT_EQ(
        media.media()->smfCode().value(), SMFMediaInterfaceCode::LR4_10_400G);
    EXPECT_EQ(media.code(), MediaInterfaceCode::LR4_400G_10KM);
  }
  // Check cmisStateChanged
  EXPECT_TRUE(
      info.tcvrState()->status() &&
      info.tcvrState()->status()->cmisStateChanged() &&
      *info.tcvrState()->status()->cmisStateChanged());

  utility::HwTransceiverUtils::verifyDiagsCapability(
      *info.tcvrState(),
      transceiverManager_->getDiagsCapability(xcvrID),
      false /* skipCheckingIndividualCapability */);

  // Verify update qsfp data logic
  if (auto status = info.tcvrState()->status();
      status && status->cmisModuleState()) {
    EXPECT_EQ(*status->cmisModuleState(), CmisModuleState::READY);
    auto cmisData = xcvr->getDOMDataUnion().cmis().as_const().value();
    // NOTE the following cmis data are specifically set to 0x11 in
    // FakeTransceiverImpl
    EXPECT_TRUE(cmisData.page14());
    // SNR will be set
    EXPECT_EQ(cmisData.page14()->data()[0], 0x06);
    // Check VDM cache
    EXPECT_TRUE(xcvr->isVdmSupported());
    EXPECT_FALSE(xcvr->isVdmSupported(3));
    EXPECT_TRUE(cmisData.page20());
    EXPECT_EQ(cmisData.page20()->data()[0], 0x00);
    EXPECT_TRUE(cmisData.page21());
    EXPECT_EQ(cmisData.page21()->data()[0], 0x00);
    EXPECT_TRUE(cmisData.page24());
    EXPECT_EQ(cmisData.page24()->data()[0], 0x15);
    EXPECT_TRUE(cmisData.page25());
    EXPECT_EQ(cmisData.page25()->data()[0], 0x00);
    EXPECT_EQ(
        int(info.tcvrStats()
                ->vdmDiagsStats()
                .value()
                .errFrameMediaMin()
                .value() *
            10e9),
        736);
    EXPECT_EQ(
        int(info.tcvrStats()
                ->vdmDiagsStats()
                .value()
                .errFrameMediaMax()
                .value() *
            10e9),
        743);
    EXPECT_EQ(
        int(info.tcvrStats()
                ->vdmDiagsStats()
                .value()
                .errFrameMediaAvg()
                .value() *
            10e9),
        738);
    EXPECT_EQ(
        int(info.tcvrStats()
                ->vdmDiagsStats()
                .value()
                .errFrameMediaCur()
                .value() *
            10e9),
        741);
    EXPECT_EQ(
        int(info.tcvrStats()
                ->vdmDiagsStats()
                .value()
                .errFrameHostMin()
                .value() *
            10e10),
        597);
    EXPECT_EQ(
        int(info.tcvrStats()
                ->vdmDiagsStats()
                .value()
                .errFrameHostMax()
                .value() *
            10e10),
        598);
    EXPECT_EQ(
        int(info.tcvrStats()
                ->vdmDiagsStats()
                .value()
                .errFrameHostAvg()
                .value() *
            10e8),
        6);
    EXPECT_EQ(
        int(info.tcvrStats()
                ->vdmDiagsStats()
                .value()
                .errFrameHostCur()
                .value() *
            10e10),
        601);
  } else {
    throw FbossError("Missing CMIS Module state");
  }

  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETEST");

  auto diagsCap = transceiverManager_->getDiagsCapability(xcvrID);
  EXPECT_TRUE(diagsCap.has_value());
  std::vector<prbs::PrbsPolynomial> expectedSysPolynomials = {
      prbs::PrbsPolynomial::PRBS31Q,
      prbs::PrbsPolynomial::PRBS23Q,
      prbs::PrbsPolynomial::PRBS15Q,
      prbs::PrbsPolynomial::PRBS9Q,
      prbs::PrbsPolynomial::PRBS7Q};
  std::vector<prbs::PrbsPolynomial> expectedLinePolynomials = {
      prbs::PrbsPolynomial::PRBS31Q,
      prbs::PrbsPolynomial::PRBS23Q,
      prbs::PrbsPolynomial::PRBS9Q,
      prbs::PrbsPolynomial::PRBS7Q};

  auto linePrbsCapability = *(*diagsCap).prbsLineCapabilities();
  auto sysPrbsCapability = *(*diagsCap).prbsSystemCapabilities();

  tests.verifyPrbsPolynomials(expectedLinePolynomials, linePrbsCapability);
  tests.verifyPrbsPolynomials(expectedSysPolynomials, sysPrbsCapability);

  for (auto unsupportedApplication :
       {SMFMediaInterfaceCode::CWDM4_100G, SMFMediaInterfaceCode::FR4_400G}) {
    EXPECT_EQ(
        xcvr->getApplicationField(
            static_cast<uint8_t>(unsupportedApplication), 0),
        std::nullopt);
  }
  for (auto supportedApplication :
       {SMFMediaInterfaceCode::LR4_10_400G, SMFMediaInterfaceCode::FR4_200G}) {
    auto applicationField = xcvr->getApplicationField(
        static_cast<uint8_t>(supportedApplication), 0);
    EXPECT_NE(applicationField, std::nullopt);
    EXPECT_EQ(applicationField->hostStartLanes, std::vector<int>{0});
    EXPECT_EQ(applicationField->mediaStartLanes, std::vector<int>{0});
    for (uint8_t lane = 1; lane < 7; lane++) {
      EXPECT_EQ(
          xcvr->getApplicationField(
              static_cast<uint8_t>(supportedApplication), lane),
          std::nullopt);
    }
  }

  TransceiverPortState goodPortState1{
      "", 0, cfg::PortSpeed::TWOHUNDREDG, 4, TransmitterTechnology::OPTICAL};
  TransceiverPortState goodPortState2{
      "", 0, cfg::PortSpeed::FOURHUNDREDG, 8, TransmitterTechnology::OPTICAL};
  for (auto portState : {goodPortState1, goodPortState2}) {
    EXPECT_TRUE(xcvr->tcvrPortStateSupported(portState));
  }

  TransceiverPortState badPortState1{
      "",
      0,
      cfg::PortSpeed::HUNDREDG,
      4,
      TransmitterTechnology::COPPER}; // Copper not supported
  TransceiverPortState badPortState2{
      "",
      0,
      cfg::PortSpeed::HUNDREDG,
      4,
      TransmitterTechnology::OPTICAL}; // 100G not supported
  TransceiverPortState badPortState3{
      "",
      1,
      cfg::PortSpeed::HUNDREDG,
      4,
      TransmitterTechnology::OPTICAL}; // BAD START LANE
  for (auto portState : {badPortState1, badPortState2, badPortState3}) {
    EXPECT_FALSE(xcvr->tcvrPortStateSupported(portState));
  }
  EXPECT_TRUE(info.tcvrState()->errorStates()->empty());
}

TEST_F(CmisTest, flatMemTransceiverInfoTest) {
  auto xcvr = overrideCmisModule<CmisFlatMemTransceiver>(TransceiverID(1));
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_TRUE(info.tcvrState()->transceiverManagementInterface());
  EXPECT_EQ(
      info.tcvrState()->transceiverManagementInterface(),
      TransceiverManagementInterface::CMIS);
  EXPECT_EQ(xcvr->numHostLanes(), 0); // Unknown MediaInterface
  EXPECT_EQ(xcvr->numMediaLanes(), 0); // Unknown MediaInterface

  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETEST");
  EXPECT_TRUE(info.tcvrState()->errorStates()->empty());
}

TEST_F(CmisTest, moduleEepromChecksumTest) {
  // Create CMIS 200G FR4 module
  auto xcvrCmis200GFr4 =
      overrideCmisModule<Cmis200GTransceiver>(TransceiverID(1));
  // Verify EEPROM checksum for CMIS 200G FR4 module
  bool csumValid = xcvrCmis200GFr4->verifyEepromChecksums();
  EXPECT_TRUE(csumValid);

  // Create CMIS 400G LR4 module
  auto xcvrCmis400GLr4 =
      overrideCmisModule<Cmis400GLr4Transceiver>(TransceiverID(2));
  // Verify EEPROM checksum for CMIS 400G LR4 module
  csumValid = xcvrCmis400GLr4->verifyEepromChecksums();
  EXPECT_TRUE(csumValid);

  // Create CMIS 200G FR4 Bad module
  auto xcvrCmis200GFr4Bad =
      overrideCmisModule<BadCmis200GTransceiver>(TransceiverID(3));
  // Verify EEPROM checksum Invalid for CMIS 200G FR4 Bad module
  csumValid = xcvrCmis200GFr4Bad->verifyEepromChecksums();
  EXPECT_FALSE(csumValid);
}

TEST_F(CmisTest, cmis400GCr8TransceiverInfoTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis400GCr8Transceiver>(xcvrID);
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_TRUE(info.tcvrState()->transceiverManagementInterface());
  EXPECT_EQ(
      info.tcvrState()->transceiverManagementInterface(),
      TransceiverManagementInterface::CMIS);
  EXPECT_EQ(xcvr->numHostLanes(), 8);
  EXPECT_EQ(xcvr->numMediaLanes(), 8);
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(), MediaInterfaceCode::CR8_400G);
  for (auto& media : *info.tcvrState()->settings()->mediaInterface()) {
    EXPECT_EQ(
        media.media()->passiveCuCode().value(),
        PassiveCuMediaInterfaceCode::COPPER);
    EXPECT_EQ(media.code(), MediaInterfaceCode::CR8_400G);
  }
  // Check cmisStateChanged
  EXPECT_TRUE(
      info.tcvrState()->status() &&
      info.tcvrState()->status()->cmisStateChanged() &&
      *info.tcvrState()->status()->cmisStateChanged());

  // Verify update qsfp data logic
  if (auto status = info.tcvrState()->status();
      status && status->cmisModuleState()) {
    EXPECT_EQ(*status->cmisModuleState(), CmisModuleState::READY);
  } else {
    throw FbossError("Missing CMIS Module state");
  }

  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETEST");

  auto diagsCap = transceiverManager_->getDiagsCapability(xcvrID);
  EXPECT_FALSE(diagsCap.has_value());

  for (auto unsupportedApplication :
       {SMFMediaInterfaceCode::CWDM4_100G,
        SMFMediaInterfaceCode::FR4_200G,
        SMFMediaInterfaceCode::FR4_400G,
        SMFMediaInterfaceCode::LR4_10_400G}) {
    EXPECT_EQ(
        xcvr->getApplicationField(
            static_cast<uint8_t>(unsupportedApplication), 0),
        std::nullopt);
  }
  for (auto supportedApplication : {PassiveCuMediaInterfaceCode::COPPER}) {
    EXPECT_NE(
        xcvr->getApplicationField(
            static_cast<uint8_t>(supportedApplication), 0),
        std::nullopt);
    auto applicationField = xcvr->getApplicationField(
        static_cast<uint8_t>(supportedApplication), 0);
    EXPECT_NE(applicationField, std::nullopt);
    EXPECT_EQ(applicationField->hostStartLanes, std::vector<int>{0});
    EXPECT_EQ(applicationField->mediaStartLanes, std::vector<int>{});
    for (uint8_t lane = 1; lane < 7; lane++) {
      EXPECT_EQ(
          xcvr->getApplicationField(
              static_cast<uint8_t>(supportedApplication), lane),
          std::nullopt);
    }
  }
  EXPECT_TRUE(info.tcvrState()->errorStates()->empty());
}

TEST_F(CmisTest, cmis2x400GFr4TransceiverInfoTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis2x400GFr4Transceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_TRUE(info.tcvrState()->transceiverManagementInterface());
  EXPECT_EQ(
      info.tcvrState()->transceiverManagementInterface(),
      TransceiverManagementInterface::CMIS);
  EXPECT_EQ(xcvr->numHostLanes(), 8);
  EXPECT_EQ(xcvr->numMediaLanes(), 8);
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(), MediaInterfaceCode::FR4_2x400G);
  for (auto& media : *info.tcvrState()->settings()->mediaInterface()) {
    EXPECT_EQ(
        media.media()->smfCode().value(), SMFMediaInterfaceCode::FR4_400G);
    EXPECT_EQ(media.code(), MediaInterfaceCode::FR4_400G);
  }

  // Check cmisStateChanged
  EXPECT_TRUE(
      info.tcvrState()->status() &&
      info.tcvrState()->status()->cmisStateChanged() &&
      *info.tcvrState()->status()->cmisStateChanged());

  utility::HwTransceiverUtils::verifyDiagsCapability(
      *info.tcvrState(),
      transceiverManager_->getDiagsCapability(xcvrID),
      false /* skipCheckingIndividualCapability */);

  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETEST");

  auto diagsCap = transceiverManager_->getDiagsCapability(xcvrID);
  EXPECT_TRUE(diagsCap.has_value());
  std::vector<prbs::PrbsPolynomial> expectedSysPolynomials = {
      prbs::PrbsPolynomial::PRBS31Q,
      prbs::PrbsPolynomial::PRBS23Q,
      prbs::PrbsPolynomial::PRBS15Q,
      prbs::PrbsPolynomial::PRBS13Q,
      prbs::PrbsPolynomial::PRBS9Q,
      prbs::PrbsPolynomial::PRBS7Q,
      prbs::PrbsPolynomial::PRBS31,
      prbs::PrbsPolynomial::PRBS23,
      prbs::PrbsPolynomial::PRBS15,
      prbs::PrbsPolynomial::PRBS13,
      prbs::PrbsPolynomial::PRBS9,
      prbs::PrbsPolynomial::PRBS7};
  std::vector<prbs::PrbsPolynomial> expectedLinePolynomials = {
      prbs::PrbsPolynomial::PRBS31Q,
      prbs::PrbsPolynomial::PRBS23Q,
      prbs::PrbsPolynomial::PRBS15Q,
      prbs::PrbsPolynomial::PRBS13Q,
      prbs::PrbsPolynomial::PRBS9Q,
      prbs::PrbsPolynomial::PRBS7Q,
      prbs::PrbsPolynomial::PRBS31,
      prbs::PrbsPolynomial::PRBS23,
      prbs::PrbsPolynomial::PRBS15,
      prbs::PrbsPolynomial::PRBS13,
      prbs::PrbsPolynomial::PRBS9,
      prbs::PrbsPolynomial::PRBS7};

  auto linePrbsCapability = *(*diagsCap).prbsLineCapabilities();
  auto sysPrbsCapability = *(*diagsCap).prbsSystemCapabilities();

  tests.verifyPrbsPolynomials(expectedLinePolynomials, linePrbsCapability);
  tests.verifyPrbsPolynomials(expectedSysPolynomials, sysPrbsCapability);

  for (auto unsupportedApplication : {SMFMediaInterfaceCode::LR4_10_400G}) {
    EXPECT_EQ(
        xcvr->getApplicationField(
            static_cast<uint8_t>(unsupportedApplication), 0),
        std::nullopt);
  }

  for (auto supportedApplication :
       {SMFMediaInterfaceCode::FR4_400G,
        SMFMediaInterfaceCode::FR1_100G,
        SMFMediaInterfaceCode::FR4_200G,
        SMFMediaInterfaceCode::CWDM4_100G}) {
    auto applicationField = xcvr->getApplicationField(
        static_cast<uint8_t>(supportedApplication), 0);
    EXPECT_NE(applicationField, std::nullopt);
    std::vector<int> expectedStartLanes;
    if (supportedApplication == SMFMediaInterfaceCode::FR1_100G) {
      expectedStartLanes = {0, 1, 2, 3, 4, 5, 6, 7};
    } else {
      expectedStartLanes = {0, 4};
    }
    EXPECT_EQ(applicationField->hostStartLanes, expectedStartLanes);
    EXPECT_EQ(applicationField->mediaStartLanes, expectedStartLanes);
    for (uint8_t lane = 0; lane <= 7; lane++) {
      if (std::find(
              expectedStartLanes.begin(), expectedStartLanes.end(), lane) !=
          expectedStartLanes.end()) {
        continue;
      }
      // For lanes that are not expected to be start lanes, getApplicationField
      // should return nullopt
      EXPECT_EQ(
          xcvr->getApplicationField(
              static_cast<uint8_t>(supportedApplication), lane),
          std::nullopt);
    }
  }

  EXPECT_TRUE(diagsCap.value().vdm().value());
  EXPECT_TRUE(diagsCap.value().cdb().value());
  EXPECT_TRUE(diagsCap.value().prbsLine().value());
  EXPECT_TRUE(diagsCap.value().prbsSystem().value());
  EXPECT_TRUE(diagsCap.value().loopbackLine().value());
  EXPECT_TRUE(diagsCap.value().loopbackSystem().value());
  auto lbCap = diagsCap.value().loopbackCapability().value();
  EXPECT_FALSE(*lbCap.mediaSideOutput());
  EXPECT_TRUE(*lbCap.mediaSideInput());
  EXPECT_FALSE(*lbCap.hostSideOutput());
  EXPECT_TRUE(*lbCap.hostSideInput());
  EXPECT_FALSE(*lbCap.perLaneHostSide());
  EXPECT_FALSE(*lbCap.perLaneMediaSide());
  EXPECT_FALSE(*lbCap.simultaneousHostAndMediaSide());
  EXPECT_TRUE(diagsCap.value().txOutputControl().value());
  EXPECT_TRUE(diagsCap.value().rxOutputControl().value());
  EXPECT_TRUE(diagsCap.value().snrLine().value());
  EXPECT_TRUE(diagsCap.value().snrSystem().value());
  EXPECT_TRUE(xcvr->isVdmSupported());
  EXPECT_TRUE(xcvr->isVdmSupported(3));
  EXPECT_TRUE(xcvr->isPrbsSupported(phy::Side::LINE));
  EXPECT_TRUE(xcvr->isPrbsSupported(phy::Side::SYSTEM));
  EXPECT_TRUE(xcvr->isSnrSupported(phy::Side::LINE));
  EXPECT_TRUE(xcvr->isSnrSupported(phy::Side::SYSTEM));

  TransceiverPortState goodPortState1{
      "", 0, cfg::PortSpeed::TWOHUNDREDG, 4, TransmitterTechnology::OPTICAL};
  TransceiverPortState goodPortState2{
      "", 0, cfg::PortSpeed::HUNDREDG, 4, TransmitterTechnology::OPTICAL};
  TransceiverPortState goodPortState3{
      "", 0, cfg::PortSpeed::FOURHUNDREDG, 4, TransmitterTechnology::OPTICAL};
  TransceiverPortState goodPortState4{
      "", 4, cfg::PortSpeed::FOURHUNDREDG, 4, TransmitterTechnology::OPTICAL};
  for (auto portState :
       {goodPortState1, goodPortState2, goodPortState3, goodPortState4}) {
    EXPECT_TRUE(xcvr->tcvrPortStateSupported(portState));
  }

  TransceiverPortState badPortState1{
      "",
      0,
      cfg::PortSpeed::HUNDREDG,
      4,
      TransmitterTechnology::COPPER}; // Copper not supported
  TransceiverPortState badPortState2{
      "",
      0,
      cfg::PortSpeed::FORTYG,
      4,
      TransmitterTechnology::OPTICAL}; // 40G not supported
  TransceiverPortState badPortState3{
      "",
      1,
      cfg::PortSpeed::HUNDREDG,
      4,
      TransmitterTechnology::OPTICAL}; // BAD START LANE
  for (auto portState : {badPortState1, badPortState2, badPortState3}) {
    EXPECT_FALSE(xcvr->tcvrPortStateSupported(portState));
  }
  EXPECT_TRUE(info.tcvrState()->errorStates()->empty());
}

TEST_F(CmisTest, cmis2x400GFr4LiteTransceiverInfoTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis2x400GFr4LiteTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_TRUE(info.tcvrState()->transceiverManagementInterface());
  EXPECT_EQ(
      info.tcvrState()->transceiverManagementInterface(),
      TransceiverManagementInterface::CMIS);
  EXPECT_EQ(xcvr->numHostLanes(), 8);
  EXPECT_EQ(xcvr->numMediaLanes(), 8);
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(),
      MediaInterfaceCode::FR4_LITE_2x400G);
  for (auto& media : *info.tcvrState()->settings()->mediaInterface()) {
    EXPECT_EQ(
        media.media()->smfCode().value(), SMFMediaInterfaceCode::FR4_400G);
    EXPECT_EQ(media.code(), MediaInterfaceCode::FR4_400G);
  }

  // Check cmisStateChanged
  EXPECT_TRUE(
      info.tcvrState()->status() &&
      info.tcvrState()->status()->cmisStateChanged() &&
      *info.tcvrState()->status()->cmisStateChanged());

  utility::HwTransceiverUtils::verifyDiagsCapability(
      *info.tcvrState(),
      transceiverManager_->getDiagsCapability(xcvrID),
      false /* skipCheckingIndividualCapability */);

  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETEST");

  auto diagsCap = transceiverManager_->getDiagsCapability(xcvrID);
  EXPECT_TRUE(diagsCap.has_value());
  std::vector<prbs::PrbsPolynomial> expectedSysPolynomials = {
      prbs::PrbsPolynomial::PRBS31Q,
      prbs::PrbsPolynomial::PRBS23Q,
      prbs::PrbsPolynomial::PRBS15Q,
      prbs::PrbsPolynomial::PRBS13Q,
      prbs::PrbsPolynomial::PRBS9Q,
      prbs::PrbsPolynomial::PRBS7Q,
      prbs::PrbsPolynomial::PRBS31,
      prbs::PrbsPolynomial::PRBS23,
      prbs::PrbsPolynomial::PRBS15,
      prbs::PrbsPolynomial::PRBS13,
      prbs::PrbsPolynomial::PRBS9,
      prbs::PrbsPolynomial::PRBS7,
      prbs::PrbsPolynomial::PRBSSSPRQ};
  std::vector<prbs::PrbsPolynomial> expectedLinePolynomials = {
      prbs::PrbsPolynomial::PRBS31Q,
      prbs::PrbsPolynomial::PRBS23Q,
      prbs::PrbsPolynomial::PRBS15Q,
      prbs::PrbsPolynomial::PRBS13Q,
      prbs::PrbsPolynomial::PRBS9Q,
      prbs::PrbsPolynomial::PRBS7Q,
      prbs::PrbsPolynomial::PRBS31,
      prbs::PrbsPolynomial::PRBS23,
      prbs::PrbsPolynomial::PRBS15,
      prbs::PrbsPolynomial::PRBS13,
      prbs::PrbsPolynomial::PRBS9,
      prbs::PrbsPolynomial::PRBS7,
      prbs::PrbsPolynomial::PRBSSSPRQ};

  auto linePrbsCapability = *(*diagsCap).prbsLineCapabilities();
  auto sysPrbsCapability = *(*diagsCap).prbsSystemCapabilities();

  tests.verifyPrbsPolynomials(expectedLinePolynomials, linePrbsCapability);
  tests.verifyPrbsPolynomials(expectedSysPolynomials, sysPrbsCapability);

  for (auto unsupportedApplication : {SMFMediaInterfaceCode::LR4_10_400G}) {
    EXPECT_EQ(
        xcvr->getApplicationField(
            static_cast<uint8_t>(unsupportedApplication), 0),
        std::nullopt);
  }

  for (auto supportedApplication :
       {SMFMediaInterfaceCode::FR4_400G,
        SMFMediaInterfaceCode::FR1_100G,
        SMFMediaInterfaceCode::FR4_200G,
        SMFMediaInterfaceCode::CWDM4_100G}) {
    auto applicationField = xcvr->getApplicationField(
        static_cast<uint8_t>(supportedApplication), 0);
    EXPECT_NE(applicationField, std::nullopt);
    std::vector<int> expectedStartLanes;
    if (supportedApplication == SMFMediaInterfaceCode::FR1_100G) {
      expectedStartLanes = {0, 1, 2, 3, 4, 5, 6, 7};
    } else {
      expectedStartLanes = {0, 4};
    }
    EXPECT_EQ(applicationField->hostStartLanes, expectedStartLanes);
    EXPECT_EQ(applicationField->mediaStartLanes, expectedStartLanes);
    for (uint8_t lane = 0; lane <= 7; lane++) {
      if (std::find(
              expectedStartLanes.begin(), expectedStartLanes.end(), lane) !=
          expectedStartLanes.end()) {
        continue;
      }
      // For lanes that are not expected to be start lanes, getApplicationField
      // should return nullopt
      EXPECT_EQ(
          xcvr->getApplicationField(
              static_cast<uint8_t>(supportedApplication), lane),
          std::nullopt);
    }
  }

  EXPECT_TRUE(diagsCap.value().vdm().value());
  EXPECT_TRUE(diagsCap.value().cdb().value());
  EXPECT_TRUE(diagsCap.value().prbsLine().value());
  EXPECT_TRUE(diagsCap.value().prbsSystem().value());
  EXPECT_TRUE(diagsCap.value().loopbackLine().value());
  EXPECT_TRUE(diagsCap.value().loopbackSystem().value());
  EXPECT_TRUE(diagsCap.value().txOutputControl().value());
  EXPECT_TRUE(diagsCap.value().rxOutputControl().value());
  EXPECT_TRUE(diagsCap.value().snrLine().value());
  EXPECT_TRUE(diagsCap.value().snrSystem().value());
  EXPECT_TRUE(xcvr->isVdmSupported());
  EXPECT_TRUE(xcvr->isVdmSupported(3));
  EXPECT_TRUE(xcvr->isPrbsSupported(phy::Side::LINE));
  EXPECT_TRUE(xcvr->isPrbsSupported(phy::Side::SYSTEM));
  EXPECT_TRUE(xcvr->isSnrSupported(phy::Side::LINE));
  EXPECT_TRUE(xcvr->isSnrSupported(phy::Side::SYSTEM));

  TransceiverPortState goodPortState1{
      "", 0, cfg::PortSpeed::TWOHUNDREDG, 4, TransmitterTechnology::OPTICAL};
  TransceiverPortState goodPortState2{
      "", 0, cfg::PortSpeed::HUNDREDG, 4, TransmitterTechnology::OPTICAL};
  TransceiverPortState goodPortState3{
      "", 0, cfg::PortSpeed::FOURHUNDREDG, 4, TransmitterTechnology::OPTICAL};
  TransceiverPortState goodPortState4{
      "", 4, cfg::PortSpeed::FOURHUNDREDG, 4, TransmitterTechnology::OPTICAL};
  for (auto portState :
       {goodPortState1, goodPortState2, goodPortState3, goodPortState4}) {
    EXPECT_TRUE(xcvr->tcvrPortStateSupported(portState));
  }

  TransceiverPortState badPortState1{
      "",
      0,
      cfg::PortSpeed::HUNDREDG,
      4,
      TransmitterTechnology::COPPER}; // Copper not supported
  TransceiverPortState badPortState2{
      "",
      0,
      cfg::PortSpeed::FORTYG,
      4,
      TransmitterTechnology::OPTICAL}; // 40G not supported
  TransceiverPortState badPortState3{
      "",
      1,
      cfg::PortSpeed::HUNDREDG,
      4,
      TransmitterTechnology::OPTICAL}; // BAD START LANE
  for (auto portState : {badPortState1, badPortState2, badPortState3}) {
    EXPECT_FALSE(xcvr->tcvrPortStateSupported(portState));
  }
  EXPECT_TRUE(info.tcvrState()->errorStates()->empty());
}

TEST_F(CmisTest, cmis2x400GFr4LpoTransceiverInfoTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis2x400GFr4LpoTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_TRUE(info.tcvrState()->transceiverManagementInterface());
  EXPECT_EQ(
      info.tcvrState()->transceiverManagementInterface(),
      TransceiverManagementInterface::CMIS);
  EXPECT_EQ(xcvr->numHostLanes(), 8);
  EXPECT_EQ(xcvr->numMediaLanes(), 8);
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(),
      MediaInterfaceCode::FR4_LPO_2x400G);
  for (auto& media : *info.tcvrState()->settings()->mediaInterface()) {
    EXPECT_EQ(
        media.media()->smfCode().value(), SMFMediaInterfaceCode::FR1_100G);
    EXPECT_EQ(media.code(), MediaInterfaceCode::FR1_100G);
  }

  // Check cmisStateChanged
  EXPECT_TRUE(
      info.tcvrState()->status() &&
      info.tcvrState()->status()->cmisStateChanged() &&
      *info.tcvrState()->status()->cmisStateChanged());

  std::optional<DiagsCapability> diagsCapability =
      transceiverManager_->getDiagsCapability(xcvrID);

  // LPO Has limited diags capability, pass skipCheckingIndividualCapability as
  // true.
  utility::HwTransceiverUtils::verifyDiagsCapability(
      *info.tcvrState(),
      diagsCapability,
      true /* skipCheckingIndividualCapability */);

  // Diags Capabilities available in LPO Modules
  EXPECT_TRUE(*diagsCapability->cdb());
  EXPECT_TRUE(*diagsCapability->txOutputControl());

  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETESTLPO");

  auto diagsCap = transceiverManager_->getDiagsCapability(xcvrID);
  EXPECT_TRUE(diagsCap.has_value());
  std::vector<prbs::PrbsPolynomial> expectedSysPolynomials = {};
  std::vector<prbs::PrbsPolynomial> expectedLinePolynomials = {};

  auto linePrbsCapability = *(*diagsCap).prbsLineCapabilities();
  auto sysPrbsCapability = *(*diagsCap).prbsSystemCapabilities();

  tests.verifyPrbsPolynomials(expectedLinePolynomials, linePrbsCapability);
  tests.verifyPrbsPolynomials(expectedSysPolynomials, sysPrbsCapability);

  for (auto unsupportedApplication : {SMFMediaInterfaceCode::LR4_10_400G}) {
    EXPECT_EQ(
        xcvr->getApplicationField(
            static_cast<uint8_t>(unsupportedApplication), 0),
        std::nullopt);
  }

  for (auto supportedApplication :
       {SMFMediaInterfaceCode::FR8_800G,
        SMFMediaInterfaceCode::FR4_400G,
        SMFMediaInterfaceCode::FR1_100G}) {
    auto applicationField = xcvr->getApplicationField(
        static_cast<uint8_t>(supportedApplication), 0);
    EXPECT_NE(applicationField, std::nullopt);
    std::vector<int> expectedStartLanes;
    switch (supportedApplication) {
      case SMFMediaInterfaceCode::FR8_800G:
        expectedStartLanes = {0};
        break;
      case SMFMediaInterfaceCode::FR4_400G:
        expectedStartLanes = {0, 4};
        break;
      case SMFMediaInterfaceCode::FR1_100G:
        expectedStartLanes = {0, 1, 2, 3, 4, 5, 6, 7};
        break;
      default:
        throw FbossError(
            "Unhandled application ",
            apache::thrift::util::enumNameSafe(supportedApplication));
    }
    EXPECT_EQ(applicationField->hostStartLanes, expectedStartLanes);
    EXPECT_EQ(applicationField->mediaStartLanes, expectedStartLanes);
    for (uint8_t lane = 0; lane <= 7; lane++) {
      if (std::find(
              expectedStartLanes.begin(), expectedStartLanes.end(), lane) !=
          expectedStartLanes.end()) {
        continue;
      }
      // For lanes that are not expected to be start lanes, getApplicationField
      // should return nullopt
      EXPECT_EQ(
          xcvr->getApplicationField(
              static_cast<uint8_t>(supportedApplication), lane),
          std::nullopt);
    }
  }

  EXPECT_TRUE(diagsCap.value().cdb().value());
  EXPECT_TRUE(diagsCap.value().txOutputControl().value());

  // 8x100G
  TransceiverPortState goodPortState1{
      "", 0, cfg::PortSpeed::HUNDREDG, 1, TransmitterTechnology::OPTICAL};
  TransceiverPortState goodPortState2{
      "", 1, cfg::PortSpeed::HUNDREDG, 1, TransmitterTechnology::OPTICAL};
  TransceiverPortState goodPortState3{
      "", 2, cfg::PortSpeed::HUNDREDG, 1, TransmitterTechnology::OPTICAL};
  TransceiverPortState goodPortState4{
      "", 3, cfg::PortSpeed::HUNDREDG, 1, TransmitterTechnology::OPTICAL};
  TransceiverPortState goodPortState5{
      "", 4, cfg::PortSpeed::HUNDREDG, 1, TransmitterTechnology::OPTICAL};
  TransceiverPortState goodPortState6{
      "", 5, cfg::PortSpeed::HUNDREDG, 1, TransmitterTechnology::OPTICAL};
  TransceiverPortState goodPortState7{
      "", 6, cfg::PortSpeed::HUNDREDG, 1, TransmitterTechnology::OPTICAL};
  TransceiverPortState goodPortState8{
      "", 7, cfg::PortSpeed::HUNDREDG, 1, TransmitterTechnology::OPTICAL};
  // 2x400G
  TransceiverPortState goodPortState9{
      "", 0, cfg::PortSpeed::FOURHUNDREDG, 4, TransmitterTechnology::OPTICAL};
  TransceiverPortState goodPortState10{
      "", 4, cfg::PortSpeed::FOURHUNDREDG, 4, TransmitterTechnology::OPTICAL};
  for (auto portState : {
           goodPortState1,
           goodPortState2,
           goodPortState3,
           goodPortState4,
           goodPortState5,
           goodPortState6,
           goodPortState7,
           goodPortState8,
           goodPortState9,
           goodPortState10,
       }) {
    EXPECT_TRUE(xcvr->tcvrPortStateSupported(portState));
  }

  TransceiverPortState badPortState1{
      "",
      0,
      cfg::PortSpeed::HUNDREDG,
      4,
      TransmitterTechnology::COPPER}; // Copper not supported
  TransceiverPortState badPortState2{
      "",
      0,
      cfg::PortSpeed::FORTYG,
      4,
      TransmitterTechnology::OPTICAL}; // 40G not supported
  TransceiverPortState badPortState3{
      "",
      1,
      cfg::PortSpeed::HUNDREDG,
      4,
      TransmitterTechnology::OPTICAL}; // BAD START LANE
  for (auto portState : {badPortState1, badPortState2, badPortState3}) {
    EXPECT_FALSE(xcvr->tcvrPortStateSupported(portState));
  }
  EXPECT_TRUE(info.tcvrState()->errorStates()->empty());
}

// TODO: T232388340 Further enhancement for full EEPROM support.
TEST_F(CmisTest, cmis800GZrTransceiverInfoTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis800GZrTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_TRUE(info.tcvrState()->transceiverManagementInterface());
  EXPECT_EQ(
      info.tcvrState()->transceiverManagementInterface(),
      TransceiverManagementInterface::CMIS);
  EXPECT_EQ(xcvr->numHostLanes(), 8);
  EXPECT_EQ(xcvr->numMediaLanes(), 1);
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(), MediaInterfaceCode::ZR_800G);
  for (auto& media : *info.tcvrState()->settings()->mediaInterface()) {
    EXPECT_EQ(
        media.media()->smfCode().value(),
        SMFMediaInterfaceCode::ZR_OROADM_FLEXO_8E_DPO_800G);
    EXPECT_EQ(media.code(), MediaInterfaceCode::ZR_800G);
  }

  // Check cmisStateChanged
  EXPECT_TRUE(
      info.tcvrState()->status() &&
      info.tcvrState()->status()->cmisStateChanged() &&
      *info.tcvrState()->status()->cmisStateChanged());

  std::optional<DiagsCapability> diagsCapability =
      transceiverManager_->getDiagsCapability(xcvrID);

  utility::HwTransceiverUtils::verifyDiagsCapability(
      *info.tcvrState(), diagsCapability, false);

  EXPECT_TRUE(*diagsCapability->cdb());
  EXPECT_TRUE(*diagsCapability->txOutputControl());

  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("METAZR");

  auto diagsCap = transceiverManager_->getDiagsCapability(xcvrID);
  EXPECT_TRUE(diagsCap.has_value());
  std::vector<prbs::PrbsPolynomial> expectedSysPolynomials = {
      prbs::PrbsPolynomial::PRBS31Q,
      prbs::PrbsPolynomial::PRBS23Q,
      prbs::PrbsPolynomial::PRBS15Q,
      prbs::PrbsPolynomial::PRBS13Q,
      prbs::PrbsPolynomial::PRBS9Q,
      prbs::PrbsPolynomial::PRBS7Q,
      prbs::PrbsPolynomial::PRBS31,
      prbs::PrbsPolynomial::PRBS23,
      prbs::PrbsPolynomial::PRBS15,
      prbs::PrbsPolynomial::PRBS13,
      prbs::PrbsPolynomial::PRBS9,
      prbs::PrbsPolynomial::PRBS7};
  std::vector<prbs::PrbsPolynomial> expectedLinePolynomials = {
      prbs::PrbsPolynomial::PRBS31,
      prbs::PrbsPolynomial::PRBS23,
      prbs::PrbsPolynomial::PRBS15,
      prbs::PrbsPolynomial::PRBS7};

  auto linePrbsCapability = *(*diagsCap).prbsLineCapabilities();
  auto sysPrbsCapability = *(*diagsCap).prbsSystemCapabilities();

  tests.verifyPrbsPolynomials(expectedLinePolynomials, linePrbsCapability);
  tests.verifyPrbsPolynomials(expectedSysPolynomials, sysPrbsCapability);

  for (auto unsupportedApplication : {SMFMediaInterfaceCode::LR4_10_400G}) {
    EXPECT_EQ(
        xcvr->getApplicationField(
            static_cast<uint8_t>(unsupportedApplication), 0),
        std::nullopt);
  }

  for (auto supportedApplication :
       {SMFMediaInterfaceCode::ZR_OROADM_FLEXO_8E_DPO_800G}) {
    auto applicationField = xcvr->getApplicationField(
        static_cast<uint8_t>(supportedApplication), 0);
    EXPECT_NE(applicationField, std::nullopt);
    std::vector<int> expectedStartLanes;
    switch (supportedApplication) {
      case SMFMediaInterfaceCode::ZR_OROADM_FLEXO_8E_DPO_800G:
        expectedStartLanes = {0};
        break;
      default:
        throw FbossError(
            "Unhandled application ",
            apache::thrift::util::enumNameSafe(supportedApplication));
    }
    EXPECT_EQ(applicationField->hostStartLanes, expectedStartLanes);
    EXPECT_EQ(applicationField->mediaStartLanes, expectedStartLanes);
  }

  EXPECT_TRUE(diagsCap.value().cdb().value());
  EXPECT_TRUE(diagsCap.value().prbsLine().value());
  EXPECT_TRUE(diagsCap.value().prbsSystem().value());
  EXPECT_TRUE(diagsCap.value().loopbackLine().value());
  EXPECT_TRUE(diagsCap.value().loopbackSystem().value());
  EXPECT_TRUE(diagsCap.value().txOutputControl().value());
  EXPECT_TRUE(diagsCap.value().rxOutputControl().value());

  // 1x800G
  TransceiverPortState goodPortState1{
      "", 0, cfg::PortSpeed::EIGHTHUNDREDG, 8, TransmitterTechnology::OPTICAL};
  for (auto portState : {
           goodPortState1,
       }) {
    EXPECT_TRUE(xcvr->tcvrPortStateSupported(portState));
  }

  TransceiverPortState badPortState1{
      "",
      0,
      cfg::PortSpeed::HUNDREDG,
      4,
      TransmitterTechnology::COPPER}; // Copper not supported
  TransceiverPortState badPortState2{
      "",
      0,
      cfg::PortSpeed::FORTYG,
      4,
      TransmitterTechnology::OPTICAL}; // 40G not supported
  TransceiverPortState badPortState3{
      "",
      1,
      cfg::PortSpeed::HUNDREDG,
      4,
      TransmitterTechnology::OPTICAL}; // BAD START LANE
  for (auto portState : {badPortState1, badPortState2, badPortState3}) {
    EXPECT_FALSE(xcvr->tcvrPortStateSupported(portState));
  }

  // tunable optics check
  EXPECT_TRUE(xcvr->isTunableOptics());

  // Test all supported frequency grids map to correct grid selection values
  EXPECT_EQ(
      0x00, xcvr->frequencyGridToGridSelection(FrequencyGrid::LASER_3P125GHZ));
  EXPECT_EQ(
      0x10, xcvr->frequencyGridToGridSelection(FrequencyGrid::LASER_6P25GHZ));
  EXPECT_EQ(
      0x20, xcvr->frequencyGridToGridSelection(FrequencyGrid::LASER_12P5GHZ));
  EXPECT_EQ(
      0x30, xcvr->frequencyGridToGridSelection(FrequencyGrid::LASER_25GHZ));
  EXPECT_EQ(
      0x40, xcvr->frequencyGridToGridSelection(FrequencyGrid::LASER_50GHZ));
  EXPECT_EQ(
      0x50, xcvr->frequencyGridToGridSelection(FrequencyGrid::LASER_100GHZ));
  EXPECT_EQ(
      0x60, xcvr->frequencyGridToGridSelection(FrequencyGrid::LASER_33GHZ));
  EXPECT_EQ(
      0x70, xcvr->frequencyGridToGridSelection(FrequencyGrid::LASER_75GHZ));
  EXPECT_EQ(
      0x80, xcvr->frequencyGridToGridSelection(FrequencyGrid::LASER_150GHZ));

  // Test getChannelNumFromFrequency: base frequency → channel 0
  EXPECT_EQ(
      xcvr->getChannelNumFromFrequency(
          193100000, FrequencyGrid::LASER_3P125GHZ),
      0);
  EXPECT_EQ(
      xcvr->getChannelNumFromFrequency(193100000, FrequencyGrid::LASER_6P25GHZ),
      0);

  // Test getChannelNumFromFrequency: positive channel numbers
  EXPECT_EQ(
      xcvr->getChannelNumFromFrequency(193775000, FrequencyGrid::LASER_6P25GHZ),
      108);
  EXPECT_EQ(
      xcvr->getChannelNumFromFrequency(193925000, FrequencyGrid::LASER_6P25GHZ),
      132);

  // Test getChannelNumFromFrequency: negative channel numbers (L-band)
  EXPECT_EQ(
      xcvr->getChannelNumFromFrequency(186125000, FrequencyGrid::LASER_6P25GHZ),
      -1116);
  EXPECT_EQ(
      xcvr->getChannelNumFromFrequency(191375000, FrequencyGrid::LASER_6P25GHZ),
      -276);

  // Test getChannelNumFromFrequency across all grids to verify
  // no floating-point truncation (the off-by-one bug)
  // 150 GHz grid: n = (diffMhz * 40) / 1000000 - 3; channel 1 at 193200000
  EXPECT_EQ(
      xcvr->getChannelNumFromFrequency(193200000, FrequencyGrid::LASER_150GHZ),
      1);
  EXPECT_EQ(
      xcvr->getChannelNumFromFrequency(193200000, FrequencyGrid::LASER_100GHZ),
      1);
  // 75 GHz grid: n = (diffMhz * 40) / 1000000; channel 1 at 193125000
  EXPECT_EQ(
      xcvr->getChannelNumFromFrequency(193125000, FrequencyGrid::LASER_75GHZ),
      1);
  EXPECT_EQ(
      xcvr->getChannelNumFromFrequency(193150000, FrequencyGrid::LASER_50GHZ),
      1);
  // 33 GHz grid: n = (diffMhz * 30) / 1000000; channel 3 at 193200000
  EXPECT_EQ(
      xcvr->getChannelNumFromFrequency(193200000, FrequencyGrid::LASER_33GHZ),
      3);
  EXPECT_EQ(
      xcvr->getChannelNumFromFrequency(193125000, FrequencyGrid::LASER_25GHZ),
      1);
  EXPECT_EQ(
      xcvr->getChannelNumFromFrequency(193112500, FrequencyGrid::LASER_12P5GHZ),
      1);
  EXPECT_EQ(
      xcvr->getChannelNumFromFrequency(193106250, FrequencyGrid::LASER_6P25GHZ),
      1);
  EXPECT_EQ(
      xcvr->getChannelNumFromFrequency(
          193103125, FrequencyGrid::LASER_3P125GHZ),
      1);

  auto tunableLaserStatus = xcvr->getTunableLaserStatus();
  EXPECT_NE(tunableLaserStatus, std::nullopt);
  EXPECT_EQ(
      tunableLaserStatus->laserFrequencyMhz(),
      CmisModule::kDefaultFrequencyMhz);
  EXPECT_EQ(
      tunableLaserStatus->tuningStatus(),
      LaserStatusBitMask::LASER_TUNE_NOT_IN_PROGRESS);
  EXPECT_EQ(
      tunableLaserStatus->wavelengthLockingStatus(),
      LaserStatusBitMask::WAVELENGTH_LOCKED);
  EXPECT_EQ(
      xcvr->getInterfaceCodeForAppSel(1, 1),
      static_cast<uint8_t>(SMFMediaInterfaceCode::ZR_OROADM_FLEXO_8E_DPO_800G));
  EXPECT_EQ(xcvr->getCurrentAppSelCode(1), 0x1);
  EXPECT_TRUE(info.tcvrState()->errorStates()->empty());
}

// A module reset (firmware upgrade, remediation) puts the optic back at its
// defaults, but the datapath timers in portDatapathStates_ live on the module
// object and used to survive it. A timer left set by a datapath init that was
// still in flight when the reset happened then reads as "DP_INIT in prog"
// forever, and customizeTransceiverLocked skips both programTunableModule and
// the AppSel write on every subsequent attempt -- leaving the optic on its
// default frequency and AppSel while programming still reports success.
// triggerModuleReset calls resetDatapathProgrammingStateLocked to drop it.
TEST_F(CmisTest, resetDatapathProgrammingStateClearsInFlightDatapathState) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis800GZrTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);
  ASSERT_TRUE(xcvr->isTunableOptics());

  ProgramTransceiverState programTcvrState;
  TransceiverPortState portState;
  portState.portName = "eth1/1/1";
  portState.startHostLane = 0;
  portState.speed = cfg::PortSpeed::EIGHTHUNDREDG;
  portState.numHostLanes = 8;

  cfg::OpticalChannelConfig optChanConfig;
  cfg::FrequencyConfig freqConfig;
  freqConfig.frequencyGrid() = FrequencyGrid::LASER_6P25GHZ;
  cfg::CenterFrequencyConfig centerFreq;
  centerFreq.set_frequencyMhz(CmisModule::kDefaultFrequencyMhz);
  freqConfig.centerFrequencyConfig() = centerFreq;
  optChanConfig.frequencyConfig() = freqConfig;
  optChanConfig.txPower0P01Dbm() = 0;
  optChanConfig.appSelCode() = 1;
  portState.opticalChannelConfig = optChanConfig;
  programTcvrState.ports.emplace(portState.portName, portState);

  xcvr->programTransceiver(programTcvrState, false);
  ASSERT_FALSE(xcvr->portDatapathStates_.empty());

  // Stand in for a reset landing mid datapath init: the start timer is set and
  // nothing will ever clear it, because the optic is about to be reset out
  // from under us.
  auto& initTimers = xcvr->portDatapathStates_[portState.portName].initTimers;
  initTimers.progStartTimer = std::chrono::steady_clock::now();
  ASSERT_NE(initTimers.progStartTimer.time_since_epoch().count(), 0);

  // Drive the real entry point rather than the helper, so this covers the
  // wiring as well: every module reset goes through triggerModuleReset.
  xcvr->triggerModuleReset();

  // With the map cleared, the next programming attempt default-constructs the
  // state and sees progStartTimer == 0, so it takes the branch that actually
  // programs the laser frequency and AppSel.
  EXPECT_TRUE(xcvr->portDatapathStates_.empty());
  EXPECT_EQ(
      xcvr->portDatapathStates_[portState.portName]
          .initTimers.progStartTimer.time_since_epoch()
          .count(),
      0);
}

// Verify TX and RX squelch disable behavior for tunable optics (ZR modules).
// Squelch is only disabled when the module advertises rxConsActImpl
// (Page 45h, Byte 129, Bit 1). When not advertised, squelch remains enabled.
TEST_F(CmisTest, cmis800GZrSquelchDisableWithMacLfCheck) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis800GZrTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);

  ASSERT_TRUE(xcvr->isTunableOptics());

  // Fake ZR transceiver has rxConsActImpl bit set in Page 45h
  EXPECT_TRUE(xcvr->isRxConsActImplSupported());

  // Call disableTxRxSquelchForTunableOptics and verify squelch is disabled
  xcvr->disableTxRxSquelchForTunableOptics();

  // Refresh cached data to pick up the squelch register writes
  transceiverManager_->refreshStateMachines();

  const auto& info = xcvr->getTransceiverInfo();
  auto settings = *info.tcvrState()->settings();

  // Verify TX squelch disable is set on media lanes
  for (auto& mediaLane :
       apache::thrift::can_throw(*settings.mediaLaneSettings())) {
    EXPECT_TRUE(*mediaLane.txSquelch())
        << "Lane " << *mediaLane.lane()
        << ": TX squelch disable should be set for ZR with MAC LF support";
  }

  // Verify RX squelch disable is set on host lanes
  for (auto& hostLane :
       apache::thrift::can_throw(*settings.hostLaneSettings())) {
    EXPECT_TRUE(*hostLane.rxSquelch())
        << "Lane " << *hostLane.lane()
        << ": RX squelch disable should be set for ZR with MAC LF support";
  }
}

TEST_F(CmisTest, cmis800GZrHoldOffTimerCapabilityCheck) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis800GZrTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);

  ASSERT_TRUE(xcvr->isTunableOptics());
  EXPECT_TRUE(xcvr->isRxConsActHoldOffTmrImplSupported());
}

// A CMIS >= 5.1 module advertising all three Meta custom features in Page 01h
// Byte 191 surfaces them in DiagsCapability.
TEST_F(CmisTest, customFeatureCapabilityAdvertised) {
  auto xcvrID = TransceiverID(1);
  overrideCmisModule<Cmis2x800GDr4CustomFeatureTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);

  auto diagsCap = transceiverManager_->getDiagsCapability(xcvrID);
  ASSERT_TRUE(diagsCap.has_value());
  EXPECT_TRUE(*diagsCap->modeMismatchFlag());
  EXPECT_TRUE(*diagsCap->dspTempMargin());
  EXPECT_TRUE(*diagsCap->laserTempMargin());
}

// A CMIS >= 5.1 module that leaves Page 01h Byte 191 clear advertises nothing.
TEST_F(CmisTest, customFeatureCapabilityNotAdvertised) {
  auto xcvrID = TransceiverID(1);
  overrideCmisModule<Cmis2x400GFr4LiteTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);

  auto diagsCap = transceiverManager_->getDiagsCapability(xcvrID);
  ASSERT_TRUE(diagsCap.has_value());
  EXPECT_FALSE(*diagsCap->modeMismatchFlag());
  EXPECT_FALSE(*diagsCap->dspTempMargin());
  EXPECT_FALSE(*diagsCap->laserTempMargin());
}

// A healthy advertised module reports all three latched flags clear and
// positive thermal margins decoded from the quarter-degree S8 registers.
TEST_F(CmisTest, customFlagsAndThermalMarginsHealthy) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis2x800GDr4CustomFeatureTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);

  const auto& info = xcvr->getTransceiverInfo();
  auto& status = *info.tcvrState()->status();
  EXPECT_EQ(status.modeMismatchFlag(), false);
  EXPECT_EQ(status.dspTempNegativeMarginFlag(), false);
  EXPECT_EQ(status.laserTempNegativeMarginFlag(), false);

  // Byte 68 = 0x7f = 127 quarter-degrees, byte 69 = 0x3d = 61 quarter-degrees.
  EXPECT_EQ(info.tcvrStats()->dspTempMargin(), 31.75);
  EXPECT_EQ(info.tcvrStats()->laserTempMargin(), 15.25);
}

// Asserted latched flags and negative (over-temperature) margins are reported
// as such, including the sign of the S8 decode.
TEST_F(CmisTest, customFlagsAndThermalMarginsNegative) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis2x800GDr4NegativeMarginTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);

  const auto& info = xcvr->getTransceiverInfo();
  auto& status = *info.tcvrState()->status();
  EXPECT_EQ(status.modeMismatchFlag(), true);
  EXPECT_EQ(status.dspTempNegativeMarginFlag(), true);
  EXPECT_EQ(status.laserTempNegativeMarginFlag(), true);

  // Byte 68 = 0xf8 = -8 quarter-degrees, byte 69 = 0xfc = -4 quarter-degrees.
  EXPECT_EQ(info.tcvrStats()->dspTempMargin(), -2.0);
  EXPECT_EQ(info.tcvrStats()->laserTempMargin(), -1.0);
}

// Page 14h Bytes 130/131 carry one mode-mismatch bit per lane, which maps onto
// the per-lane host and media signal vectors.
TEST_F(CmisTest, perLaneModeMismatch) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis2x800GDr4NegativeMarginTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);

  const auto& info = xcvr->getTransceiverInfo();

  // Byte 130 = 0x05: host lanes 0 and 2.
  std::vector<bool> expectedHost(xcvr->numHostLanes(), false);
  expectedHost[0] = true;
  expectedHost[2] = true;
  std::vector<bool> actualHost;
  for (const auto& signal : *info.tcvrState()->hostLaneSignals()) {
    actualHost.push_back(*signal.modeMismatch());
  }
  EXPECT_EQ(actualHost, expectedHost);

  // Byte 131 = 0x01: media lane 0 only.
  std::vector<bool> expectedMedia(xcvr->numMediaLanes(), false);
  expectedMedia[0] = true;
  std::vector<bool> actualMedia;
  for (const auto& signal : *info.tcvrState()->mediaLaneSignals()) {
    actualMedia.push_back(*signal.modeMismatch());
  }
  EXPECT_EQ(actualMedia, expectedMedia);
}

// A healthy module reports no lane mismatched.
TEST_F(CmisTest, perLaneModeMismatchClear) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis2x800GDr4CustomFeatureTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);

  const auto& info = xcvr->getTransceiverInfo();
  for (const auto& signal : *info.tcvrState()->hostLaneSignals()) {
    EXPECT_EQ(signal.modeMismatch(), false) << "host lane " << *signal.lane();
  }
  for (const auto& signal : *info.tcvrState()->mediaLaneSignals()) {
    EXPECT_EQ(signal.modeMismatch(), false) << "media lane " << *signal.lane();
  }
}

// A module that doesn't advertise the custom features leaves the flags and
// margins unset rather than reporting whatever the custom bytes happen to hold.
TEST_F(CmisTest, customFlagsAndThermalMarginsUnsetWhenNotAdvertised) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis2x400GFr4LiteTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);

  const auto& info = xcvr->getTransceiverInfo();
  auto& status = *info.tcvrState()->status();
  EXPECT_FALSE(status.modeMismatchFlag().has_value());
  EXPECT_FALSE(status.dspTempNegativeMarginFlag().has_value());
  EXPECT_FALSE(status.laserTempNegativeMarginFlag().has_value());
  EXPECT_FALSE(info.tcvrStats()->dspTempMargin().has_value());
  EXPECT_FALSE(info.tcvrStats()->laserTempMargin().has_value());

  for (const auto& signal : *info.tcvrState()->hostLaneSignals()) {
    EXPECT_FALSE(signal.modeMismatch().has_value());
  }
  for (const auto& signal : *info.tcvrState()->mediaLaneSignals()) {
    EXPECT_FALSE(signal.modeMismatch().has_value());
  }
}

// Byte 191 lives in CMIS Custom space, so it only carries the Meta meaning on
// modules built to the spec that defines it (CMIS >= 5.1). Below that revision
// its contents must be ignored.
TEST_F(CmisTest, customFeatureCapabilityIgnoredBelowCmis51) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis2x800GDr4Cmis50Transceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);

  ASSERT_EQ(xcvr->getCmisRevision(), std::make_pair(uint8_t(5), uint8_t(0)));

  auto diagsCap = transceiverManager_->getDiagsCapability(xcvrID);
  ASSERT_TRUE(diagsCap.has_value());
  EXPECT_FALSE(*diagsCap->modeMismatchFlag());
  EXPECT_FALSE(*diagsCap->dspTempMargin());
  EXPECT_FALSE(*diagsCap->laserTempMargin());
}

TEST_F(CmisTest, cmis800GZrHoldOffTimerDefault10ms) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis800GZrTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);

  ASSERT_TRUE(xcvr->isTunableOptics());
  ASSERT_TRUE(xcvr->isRxConsActHoldOffTmrImplSupported());

  xcvr->configureRxConsActHoldOffTimer(10);
  transceiverManager_->refreshStateMachines();

  // Read hold-off timer from cached Page 38h, offset 141, length 2
  const uint8_t* data =
      xcvr->getQsfpValuePtr(static_cast<int>(CmisPages::PAGE38), 141, 2);
  uint16_t readValue = (static_cast<uint16_t>(data[0]) << 8) | data[1];
  EXPECT_EQ(readValue, 1);
}

TEST_F(CmisTest, cmis800GZrHoldOffTimerDisabled) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis800GZrTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);

  ASSERT_TRUE(xcvr->isRxConsActHoldOffTmrImplSupported());

  xcvr->configureRxConsActHoldOffTimer(0);
  transceiverManager_->refreshStateMachines();

  // Read hold-off timer from cached Page 38h, offset 141, length 2
  const uint8_t* data =
      xcvr->getQsfpValuePtr(static_cast<int>(CmisPages::PAGE38), 141, 2);
  uint16_t readValue = (static_cast<uint16_t>(data[0]) << 8) | data[1];
  EXPECT_EQ(readValue, 0);
}

TEST_F(CmisTest, cmis800GZrHoldOffTimerNegativeValue) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis800GZrTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);

  ASSERT_TRUE(xcvr->isRxConsActHoldOffTmrImplSupported());

  EXPECT_THROW(xcvr->configureRxConsActHoldOffTimer(-1), FbossError);
}

// On a module that does NOT advertise support for programming the hold-off
// timer, configureRxConsActHoldOffTimer reads the current register value and
// only no-ops when it already matches the request; otherwise it throws instead
// of silently leaving a value it couldn't set.
TEST_F(CmisTest, cmis800GZrHoldOffTimerUnsupportedModule) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis800GZrNoHoldOffTmrTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);

  ASSERT_FALSE(xcvr->isRxConsActHoldOffTmrImplSupported());

  // The CONS_ACT_HOLD_OFF_TMR register reads 0, so requesting 0 (the default)
  // is a no-op and must not throw.
  EXPECT_NO_THROW(xcvr->configureRxConsActHoldOffTimer(0));

  // Requesting a non-zero value we can't program (register 0 != requested)
  // throws rather than silently leaving the wrong value.
  EXPECT_THROW(xcvr->configureRxConsActHoldOffTimer(10), FbossError);
}

// Test coherent FEC Performance Monitoring stats from C-CMIS page 34h
// on 800G ZR modules. Validates that fillVdmPerfMonitorFecPm correctly
// decodes all FEC PM counters from the cached page34 data.
TEST_F(CmisTest, cmis800GZrFecPmTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis800GZrTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);
  const auto& info = xcvr->getTransceiverInfo();

  ASSERT_TRUE(xcvr->isVdmSupported());
  ASSERT_TRUE(xcvr->isTunableOptics());

  // Program transceiver with OpticalChannelConfig for tunable optics
  // to populate port-to-media-lane mappings needed for FEC PM stats
  ProgramTransceiverState programTcvrState;
  TransceiverPortState portState;
  portState.portName = "eth1/1/1";
  portState.startHostLane = 0;
  portState.speed = cfg::PortSpeed::EIGHTHUNDREDG;
  portState.numHostLanes = 8;

  cfg::OpticalChannelConfig optChanConfig;
  cfg::FrequencyConfig freqConfig;
  freqConfig.frequencyGrid() = FrequencyGrid::LASER_6P25GHZ;
  cfg::CenterFrequencyConfig centerFreq;
  centerFreq.set_frequencyMhz(CmisModule::kDefaultFrequencyMhz);
  freqConfig.centerFrequencyConfig() = centerFreq;
  optChanConfig.frequencyConfig() = freqConfig;
  optChanConfig.txPower0P01Dbm() = 0;
  optChanConfig.appSelCode() = 1;
  portState.opticalChannelConfig = optChanConfig;

  programTcvrState.ports.emplace(portState.portName, portState);
  xcvr->programTransceiver(programTcvrState, false);

  std::vector<int32_t> xcvrIds = {xcvrID};
  transceiverManager_->triggerVdmStatsCapture(xcvrIds);
  transceiverManager_->refreshStateMachines();

  const auto& newInfo = xcvr->getTransceiverInfo();
  ASSERT_TRUE(newInfo.tcvrStats()->vdmPerfMonitorStats().has_value());
  auto& vdmPerfMonStats = newInfo.tcvrStats()->vdmPerfMonitorStats().value();
  ASSERT_FALSE(vdmPerfMonStats.mediaPortVdmStats()->empty());

  // Validate coherent FEC PM values from C-CMIS page 34h
  // Expected values are decoded from kCmis800GZrPage34 fake data
  auto portIt = vdmPerfMonStats.mediaPortVdmStats()->find("eth1/1/1");
  ASSERT_NE(portIt, vdmPerfMonStats.mediaPortVdmStats()->end());
  ASSERT_TRUE(portIt->second.coherentVdmStats().has_value());
  ASSERT_TRUE(portIt->second.coherentVdmStats()->fecPm().has_value());
  auto& fecPm = portIt->second.coherentVdmStats()->fecPm().value();

  // U64 FEC PM counters
  EXPECT_EQ(*fecPm.rxBitsPm(), 61440720961536);
  EXPECT_EQ(*fecPm.rxBitsSubIntPm(), 1809317888);
  EXPECT_EQ(*fecPm.rxCorrBitsPm(), 20766418419);
  EXPECT_EQ(*fecPm.rxMinCorrBitsSubIntPm(), 323226814);
  EXPECT_EQ(*fecPm.rxMaxCorrBitsSubIntPm(), 395888028);

  // U32 FEC PM counters
  EXPECT_EQ(*fecPm.rxFramesPm(), 44643381);
  EXPECT_EQ(*fecPm.rxFramesSubIntPm(), 744056);
  EXPECT_EQ(*fecPm.rxFramesUncorrErrPm(), 0);
  EXPECT_EQ(*fecPm.rxMinFramesUncorrErrSubIntPm(), 0);
  EXPECT_EQ(*fecPm.rxMaxFramesUncorrErrSubIntPm(), 0);
}

TEST_F(CmisTest, cmis800GZrLinePmTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis800GZrTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);
  const auto& info = xcvr->getTransceiverInfo();

  ASSERT_TRUE(xcvr->isVdmSupported());
  ASSERT_TRUE(xcvr->isTunableOptics());

  // Program transceiver with OpticalChannelConfig for tunable optics
  // to populate port-to-media-lane mappings needed for Link PM stats
  ProgramTransceiverState programTcvrState;
  TransceiverPortState portState;
  portState.portName = "eth1/1/1";
  portState.startHostLane = 0;
  portState.speed = cfg::PortSpeed::EIGHTHUNDREDG;
  portState.numHostLanes = 8;

  cfg::OpticalChannelConfig optChanConfig;
  cfg::FrequencyConfig freqConfig;
  freqConfig.frequencyGrid() = FrequencyGrid::LASER_6P25GHZ;
  cfg::CenterFrequencyConfig centerFreq;
  centerFreq.set_frequencyMhz(CmisModule::kDefaultFrequencyMhz);
  freqConfig.centerFrequencyConfig() = centerFreq;
  optChanConfig.frequencyConfig() = freqConfig;
  optChanConfig.txPower0P01Dbm() = 0;
  optChanConfig.appSelCode() = 1;
  portState.opticalChannelConfig = optChanConfig;

  programTcvrState.ports.emplace(portState.portName, portState);
  xcvr->programTransceiver(programTcvrState, false);

  std::vector<int32_t> xcvrIds = {xcvrID};
  transceiverManager_->triggerVdmStatsCapture(xcvrIds);
  transceiverManager_->refreshStateMachines();

  const auto& newInfo = xcvr->getTransceiverInfo();
  ASSERT_TRUE(newInfo.tcvrStats()->vdmPerfMonitorStats().has_value());
  auto& vdmPerfMonStats = newInfo.tcvrStats()->vdmPerfMonitorStats().value();
  ASSERT_FALSE(vdmPerfMonStats.mediaPortVdmStats()->empty());

  // Validate coherent Link PM values from C-CMIS page 35h
  // Expected values are decoded from kCmis800GZrPage35 fake data
  auto portIt = vdmPerfMonStats.mediaPortVdmStats()->find("eth1/1/1");
  ASSERT_NE(portIt, vdmPerfMonStats.mediaPortVdmStats()->end());
  ASSERT_TRUE(portIt->second.coherentVdmStats().has_value());
  ASSERT_TRUE(portIt->second.coherentVdmStats()->linkPm().has_value());
  auto& linkPm = portIt->second.coherentVdmStats()->linkPm().value();

  // CD: S32 at bytes 128-139, LSB=1.0 ps/nm (exact integer values)
  ASSERT_TRUE(linkPm.cd().has_value());
  EXPECT_EQ(*linkPm.cd()->avg(), -1.0);
  EXPECT_EQ(*linkPm.cd()->min(), -3.0);
  EXPECT_EQ(*linkPm.cd()->max(), 1.0);

  // DGD: U16 at bytes 140-145, LSB=0.01 ps
  ASSERT_TRUE(linkPm.dgd().has_value());
  EXPECT_DOUBLE_EQ(*linkPm.dgd()->avg(), 1.09);
  EXPECT_DOUBLE_EQ(*linkPm.dgd()->min(), 0.95);
  EXPECT_DOUBLE_EQ(*linkPm.dgd()->max(), 1.31);

  // SOPMD: U16 at bytes 146-151, LSB=0.01 ps^2
  ASSERT_TRUE(linkPm.sopmd().has_value());
  EXPECT_DOUBLE_EQ(*linkPm.sopmd()->avg(), 65.0);
  EXPECT_DOUBLE_EQ(*linkPm.sopmd()->min(), 8.0);
  EXPECT_DOUBLE_EQ(*linkPm.sopmd()->max(), 211.0);

  // PDL: U16 at bytes 152-157, LSB=0.1 dB
  ASSERT_TRUE(linkPm.pdl().has_value());
  EXPECT_DOUBLE_EQ(*linkPm.pdl()->avg(), 0.8);
  EXPECT_DOUBLE_EQ(*linkPm.pdl()->min(), 0.7);
  EXPECT_DOUBLE_EQ(*linkPm.pdl()->max(), 0.9);

  // OSNR: U16 at bytes 158-163, LSB=0.1 dB
  ASSERT_TRUE(linkPm.osnr().has_value());
  EXPECT_DOUBLE_EQ(*linkPm.osnr()->avg(), 33.8);
  EXPECT_DOUBLE_EQ(*linkPm.osnr()->min(), 32.8);
  EXPECT_DOUBLE_EQ(*linkPm.osnr()->max(), 34.8);

  // eSNR: U16 at bytes 164-169, LSB=0.1 dB
  ASSERT_TRUE(linkPm.esnr().has_value());
  EXPECT_DOUBLE_EQ(*linkPm.esnr()->avg(), 15.6);
  EXPECT_DOUBLE_EQ(*linkPm.esnr()->min(), 15.5);
  EXPECT_DOUBLE_EQ(*linkPm.esnr()->max(), 15.6);

  // CFO: S16 at bytes 170-175, LSB=1.0 MHz (exact integer values)
  ASSERT_TRUE(linkPm.cfo().has_value());
  EXPECT_EQ(*linkPm.cfo()->avg(), 119.0);
  EXPECT_EQ(*linkPm.cfo()->min(), 58.0);
  EXPECT_EQ(*linkPm.cfo()->max(), 205.0);

  // EVM: U16 at bytes 176-181, LSB=100.0/65535.0 % (irrational LSB, use NEAR)
  ASSERT_TRUE(linkPm.evmModem().has_value());
  EXPECT_NEAR(*linkPm.evmModem()->avg(), 10268.0 * 100.0 / 65535.0, 0.01);
  EXPECT_NEAR(*linkPm.evmModem()->min(), 10258.0 * 100.0 / 65535.0, 0.01);
  EXPECT_NEAR(*linkPm.evmModem()->max(), 10453.0 * 100.0 / 65535.0, 0.01);

  // TxPower: S16 at bytes 182-187, LSB=0.01 dBm
  ASSERT_TRUE(linkPm.txPower().has_value());
  EXPECT_DOUBLE_EQ(*linkPm.txPower()->avg(), -1.99);
  EXPECT_DOUBLE_EQ(*linkPm.txPower()->min(), -2.05);
  EXPECT_DOUBLE_EQ(*linkPm.txPower()->max(), -1.92);

  // RxTotalPower: S16 at bytes 188-193, LSB=0.01 dBm
  ASSERT_TRUE(linkPm.rxPower().has_value());
  EXPECT_DOUBLE_EQ(*linkPm.rxPower()->avg(), -2.04);
  EXPECT_DOUBLE_EQ(*linkPm.rxPower()->min(), -2.14);
  EXPECT_DOUBLE_EQ(*linkPm.rxPower()->max(), -1.93);

  // RxSigPower: S16 at bytes 194-199, LSB=0.01 dBm
  ASSERT_TRUE(linkPm.rxSigPower().has_value());
  EXPECT_DOUBLE_EQ(*linkPm.rxSigPower()->avg(), -2.94);
  EXPECT_DOUBLE_EQ(*linkPm.rxSigPower()->min(), -3.12);
  EXPECT_DOUBLE_EQ(*linkPm.rxSigPower()->max(), -2.69);

  // SOP ROC: S16 at bytes 200-205, LSB=1.0 krad/s (exact integer values)
  ASSERT_TRUE(linkPm.sopcr().has_value());
  EXPECT_EQ(*linkPm.sopcr()->avg(), 4.0);
  EXPECT_EQ(*linkPm.sopcr()->min(), 0.0);
  EXPECT_EQ(*linkPm.sopcr()->max(), 14.0);

  // MER: U16 at bytes 206-211, LSB=0.1 dB
  ASSERT_TRUE(linkPm.mer().has_value());
  EXPECT_DOUBLE_EQ(*linkPm.mer()->avg(), 16.0);
  EXPECT_DOUBLE_EQ(*linkPm.mer()->min(), 15.8);
  EXPECT_DOUBLE_EQ(*linkPm.mer()->max(), 16.0);

  // ClockRecoveryLoop: S16 at bytes 212-217, LSB=100.0/32767.0 % (irrational)
  ASSERT_TRUE(linkPm.clockRecoveryLoop().has_value());
  EXPECT_NEAR(*linkPm.clockRecoveryLoop()->avg(), 83.0 * 100.0 / 32767.0, 0.01);
  EXPECT_EQ(*linkPm.clockRecoveryLoop()->min(), 0.0);
  EXPECT_NEAR(
      *linkPm.clockRecoveryLoop()->max(), 183.0 * 100.0 / 32767.0, 0.01);

  // SNR Margin: S16 at bytes 224-229, LSB=0.1 dB
  ASSERT_TRUE(linkPm.snrMargin().has_value());
  EXPECT_DOUBLE_EQ(*linkPm.snrMargin()->avg(), 4.1);
  EXPECT_DOUBLE_EQ(*linkPm.snrMargin()->min(), 4.1);
  EXPECT_DOUBLE_EQ(*linkPm.snrMargin()->max(), 4.1);

  // Q-factor: U16 at bytes 230-235, LSB=0.1 dB
  ASSERT_TRUE(linkPm.qFactor().has_value());
  EXPECT_DOUBLE_EQ(*linkPm.qFactor()->avg(), 10.6);
  EXPECT_DOUBLE_EQ(*linkPm.qFactor()->min(), 10.6);
  EXPECT_DOUBLE_EQ(*linkPm.qFactor()->max(), 10.7);

  // Q-margin: S16 at bytes 236-241, LSB=0.1 dB
  ASSERT_TRUE(linkPm.qMargin().has_value());
  EXPECT_DOUBLE_EQ(*linkPm.qMargin()->avg(), 4.1);
  EXPECT_DOUBLE_EQ(*linkPm.qMargin()->min(), 4.0);
  EXPECT_DOUBLE_EQ(*linkPm.qMargin()->max(), 4.2);
}

// Test for VDM support on 800G ZR modules
TEST_F(CmisTest, cmis800GZrVdmTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis800GZrTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);

  EXPECT_TRUE(xcvr->isVdmSupported());
  // Verify all 4 VDM groups are supported (VDMSupport bits 1-0 = 0x03)
  EXPECT_TRUE(xcvr->isVdmSupported(4));

  // Verify coherent VDM parameters are found in VDM config (page 23h)
  // with data values on page 27h
  auto modulatorBiasXI =
      xcvr->getVdmDiagsValLocation(VdmConfigType::MODULATOR_BIAS_XI);
  EXPECT_TRUE(modulatorBiasXI.vdmConfImplementedByModule);
  EXPECT_EQ(modulatorBiasXI.vdmValPage, CmisPages::PAGE27);

  auto modulatorBiasXQ =
      xcvr->getVdmDiagsValLocation(VdmConfigType::MODULATOR_BIAS_XQ);
  EXPECT_TRUE(modulatorBiasXQ.vdmConfImplementedByModule);
  EXPECT_EQ(modulatorBiasXQ.vdmValPage, CmisPages::PAGE27);

  auto modulatorBiasYI =
      xcvr->getVdmDiagsValLocation(VdmConfigType::MODULATOR_BIAS_YI);
  EXPECT_TRUE(modulatorBiasYI.vdmConfImplementedByModule);
  EXPECT_EQ(modulatorBiasYI.vdmValPage, CmisPages::PAGE27);

  auto modulatorBiasYQ =
      xcvr->getVdmDiagsValLocation(VdmConfigType::MODULATOR_BIAS_YQ);
  EXPECT_TRUE(modulatorBiasYQ.vdmConfImplementedByModule);
  EXPECT_EQ(modulatorBiasYQ.vdmValPage, CmisPages::PAGE27);

  auto modulatorBiasXPhase =
      xcvr->getVdmDiagsValLocation(VdmConfigType::MODULATOR_BIAS_X_PHASE);
  EXPECT_TRUE(modulatorBiasXPhase.vdmConfImplementedByModule);
  EXPECT_EQ(modulatorBiasXPhase.vdmValPage, CmisPages::PAGE27);

  auto modulatorBiasYPhase =
      xcvr->getVdmDiagsValLocation(VdmConfigType::MODULATOR_BIAS_Y_PHASE);
  EXPECT_TRUE(modulatorBiasYPhase.vdmConfImplementedByModule);
  EXPECT_EQ(modulatorBiasYPhase.vdmValPage, CmisPages::PAGE27);

  auto cdLowGran =
      xcvr->getVdmDiagsValLocation(VdmConfigType::CD_LOW_GRANULARITY);
  EXPECT_TRUE(cdLowGran.vdmConfImplementedByModule);
  EXPECT_EQ(cdLowGran.vdmValPage, CmisPages::PAGE27);

  auto sopmdLowGran =
      xcvr->getVdmDiagsValLocation(VdmConfigType::SOPMD_LOW_GRANULARITY);
  EXPECT_TRUE(sopmdLowGran.vdmConfImplementedByModule);
  EXPECT_EQ(sopmdLowGran.vdmValPage, CmisPages::PAGE27);

  // Program transceiver to populate port-to-media-lane mappings
  ProgramTransceiverState programTcvrState;
  TransceiverPortState portState;
  portState.portName = "eth1/1/1";
  portState.startHostLane = 0;
  portState.speed = cfg::PortSpeed::EIGHTHUNDREDG;
  portState.numHostLanes = 8;

  cfg::OpticalChannelConfig optChanConfig;
  cfg::FrequencyConfig freqConfig;
  freqConfig.frequencyGrid() = FrequencyGrid::LASER_6P25GHZ;
  cfg::CenterFrequencyConfig centerFreq;
  centerFreq.set_frequencyMhz(CmisModule::kDefaultFrequencyMhz);
  freqConfig.centerFrequencyConfig() = centerFreq;
  optChanConfig.frequencyConfig() = freqConfig;
  optChanConfig.txPower0P01Dbm() = 0;
  optChanConfig.appSelCode() = 1;
  portState.opticalChannelConfig = optChanConfig;

  programTcvrState.ports.emplace(portState.portName, portState);
  xcvr->programTransceiver(programTcvrState, false);

  std::vector<int32_t> xcvrIds = {xcvrID};
  transceiverManager_->triggerVdmStatsCapture(xcvrIds);
  transceiverManager_->refreshStateMachines();

  const auto& newInfo = xcvr->getTransceiverInfo();
  ASSERT_TRUE(newInfo.tcvrStats()->vdmPerfMonitorStats().has_value());
  auto& vdmPerfMonStats = newInfo.tcvrStats()->vdmPerfMonitorStats().value();
  ASSERT_FALSE(vdmPerfMonStats.mediaPortVdmStats()->empty());

  // Validate coherent VDM values from page 0x27 (kCmis800GZrPage27)
  // Values are computed as: rawU16 * LSB (or rawS16 * LSB for signed)
  auto portIt = vdmPerfMonStats.mediaPortVdmStats()->find("eth1/1/1");
  ASSERT_NE(portIt, vdmPerfMonStats.mediaPortVdmStats()->end());
  ASSERT_TRUE(portIt->second.coherentVdmStats().has_value());
  auto& coherentVdm = portIt->second.coherentVdmStats().value();

  // Modulator Bias XI: raw U16 = 0x758b, LSB = 100/65535
  EXPECT_EQ(int(*coherentVdm.modulatorBiasXI() * 100), 4591);
  // Modulator Bias XQ: raw U16 = 0x6118, LSB = 100/65535
  EXPECT_EQ(int(*coherentVdm.modulatorBiasXQ() * 100), 3792);
  // Modulator Bias YI: raw U16 = 0x7764, LSB = 100/65535
  EXPECT_EQ(int(*coherentVdm.modulatorBiasYI() * 100), 4663);
  // Modulator Bias YQ: raw U16 = 0x63d1, LSB = 100/65535
  EXPECT_EQ(int(*coherentVdm.modulatorBiasYQ() * 100), 3899);
  // Modulator Bias X Phase: raw U16 = 0x6a5f, LSB = 100/65535
  EXPECT_EQ(int(*coherentVdm.modulatorBiasXPhase() * 100), 4155);
  // Modulator Bias Y Phase: raw U16 = 0x661b, LSB = 100/65535
  EXPECT_EQ(int(*coherentVdm.modulatorBiasYPhase() * 100), 3988);
  // CD low granularity: raw S16 = 0x0000, LSB = 20 ps/nm
  EXPECT_EQ(*coherentVdm.cdLowGranularity(), 0.0);
  // SOPMD low granularity: raw U16 = 0x0045 = 69, LSB = 1 ps^2
  EXPECT_EQ(*coherentVdm.sopmdLowGranularity(), 69.0);
}

TEST_F(CmisTest, cmisCredo800AecInfoTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<CmisCredo800AEC>(
      xcvrID, TransceiverModuleIdentifier::OSFP);
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_TRUE(info.tcvrState()->transceiverManagementInterface());
  EXPECT_EQ(
      info.tcvrState()->transceiverManagementInterface(),
      TransceiverManagementInterface::CMIS);
  EXPECT_EQ(xcvr->numHostLanes(), 8);
  EXPECT_EQ(xcvr->numMediaLanes(), 8);
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(), MediaInterfaceCode::CR8_800G);
  for (auto& media : *info.tcvrState()->settings()->mediaInterface()) {
    EXPECT_EQ(
        media.media()->activeCuCode().value(),
        ActiveCuHostInterfaceCode::AUI_PAM4_8S_800G);
    EXPECT_EQ(media.code(), MediaInterfaceCode::CR8_800G);
  }

  // Check cmisStateChanged
  EXPECT_TRUE(
      info.tcvrState()->status() &&
      info.tcvrState()->status()->cmisStateChanged() &&
      *info.tcvrState()->status()->cmisStateChanged());

  utility::HwTransceiverUtils::verifyDiagsCapability(
      *info.tcvrState(),
      transceiverManager_->getDiagsCapability(xcvrID),
      false /* skipCheckingIndividualCapability */);

  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETEST");

  auto diagsCap = transceiverManager_->getDiagsCapability(xcvrID);
  EXPECT_TRUE(diagsCap.has_value());
  std::vector<prbs::PrbsPolynomial> expectedSysPolynomials = {
      prbs::PrbsPolynomial::PRBS31Q,
      prbs::PrbsPolynomial::PRBS15Q,
      prbs::PrbsPolynomial::PRBS13Q,
      prbs::PrbsPolynomial::PRBS9Q,
      prbs::PrbsPolynomial::PRBS31,
      prbs::PrbsPolynomial::PRBS15,
      prbs::PrbsPolynomial::PRBS13,
      prbs::PrbsPolynomial::PRBS9};
  std::vector<prbs::PrbsPolynomial> expectedLinePolynomials = {
      prbs::PrbsPolynomial::PRBS31Q,
      prbs::PrbsPolynomial::PRBS15Q,
      prbs::PrbsPolynomial::PRBS13Q,
      prbs::PrbsPolynomial::PRBS9Q,
      prbs::PrbsPolynomial::PRBS31,
      prbs::PrbsPolynomial::PRBS15,
      prbs::PrbsPolynomial::PRBS13,
      prbs::PrbsPolynomial::PRBS9};

  auto linePrbsCapability = *(*diagsCap).prbsLineCapabilities();
  auto sysPrbsCapability = *(*diagsCap).prbsSystemCapabilities();

  tests.verifyPrbsPolynomials(expectedLinePolynomials, linePrbsCapability);
  tests.verifyPrbsPolynomials(expectedSysPolynomials, sysPrbsCapability);

  for (auto unsupportedApplication : {SMFMediaInterfaceCode::LR4_10_400G}) {
    EXPECT_EQ(
        xcvr->getApplicationField(
            static_cast<uint8_t>(unsupportedApplication), 0),
        std::nullopt);
  }

  for (auto supportedApplication :
       {ActiveCuHostInterfaceCode::AUI_PAM4_8S_800G,
        ActiveCuHostInterfaceCode::AUI_PAM4_4S_400G}) {
    auto applicationField = xcvr->getApplicationField(
        static_cast<uint8_t>(supportedApplication), 0);
    EXPECT_NE(applicationField, std::nullopt);
    std::vector<int> expectedStartLanes;
    if (supportedApplication == ActiveCuHostInterfaceCode::AUI_PAM4_8S_800G) {
      expectedStartLanes = {0};
    } else {
      expectedStartLanes = {0, 4};
    }
    EXPECT_EQ(applicationField->hostStartLanes, expectedStartLanes);
    EXPECT_EQ(applicationField->mediaStartLanes, expectedStartLanes);
    for (uint8_t lane = 0; lane <= 7; lane++) {
      if (std::find(
              expectedStartLanes.begin(), expectedStartLanes.end(), lane) !=
          expectedStartLanes.end()) {
        continue;
      }
      // For lanes that are not expected to be start lanes, getApplicationField
      // should return nullopt
      EXPECT_EQ(
          xcvr->getApplicationField(
              static_cast<uint8_t>(supportedApplication), lane),
          std::nullopt);
    }
  }

  EXPECT_TRUE(diagsCap.value().cdb().value());
  EXPECT_TRUE(diagsCap.value().prbsLine().value());
  EXPECT_TRUE(diagsCap.value().prbsSystem().value());
  EXPECT_TRUE(diagsCap.value().loopbackLine().value());
  EXPECT_TRUE(diagsCap.value().loopbackSystem().value());
  EXPECT_TRUE(diagsCap.value().txOutputControl().value());
  EXPECT_TRUE(diagsCap.value().rxOutputControl().value());
  EXPECT_TRUE(diagsCap.value().snrLine().value());
  EXPECT_TRUE(diagsCap.value().snrSystem().value());
  EXPECT_TRUE(xcvr->isPrbsSupported(phy::Side::LINE));
  EXPECT_TRUE(xcvr->isPrbsSupported(phy::Side::SYSTEM));
  EXPECT_TRUE(xcvr->isSnrSupported(phy::Side::LINE));
  EXPECT_TRUE(xcvr->isSnrSupported(phy::Side::SYSTEM));

  // 1x800G
  TransceiverPortState goodPortState1{
      "", 0, cfg::PortSpeed::EIGHTHUNDREDG, 8, TransmitterTechnology::COPPER};

  // 2x400G
  TransceiverPortState goodPortState2{
      "", 0, cfg::PortSpeed::FOURHUNDREDG, 4, TransmitterTechnology::COPPER};
  TransceiverPortState goodPortState3{
      "", 4, cfg::PortSpeed::FOURHUNDREDG, 4, TransmitterTechnology::COPPER};
  for (auto portState : {goodPortState1, goodPortState2, goodPortState3}) {
    EXPECT_TRUE(xcvr->tcvrPortStateSupported(portState));
  }

  TransceiverPortState badPortState1{
      "",
      0,
      cfg::PortSpeed::HUNDREDG,
      4,
      TransmitterTechnology::OPTICAL}; // OPTICAL not supported
  TransceiverPortState badPortState2{
      "",
      0,
      cfg::PortSpeed::FORTYG,
      4,
      TransmitterTechnology::OPTICAL}; // 40G not supported
  TransceiverPortState badPortState3{
      "",
      1,
      cfg::PortSpeed::HUNDREDG,
      4,
      TransmitterTechnology::OPTICAL}; // BAD START LANE
  for (auto portState : {badPortState1, badPortState2, badPortState3}) {
    EXPECT_FALSE(xcvr->tcvrPortStateSupported(portState));
  }

  // AEC modules leave the optical dBm fields unset; mW values stay populated
  const auto& channels = *info.tcvrStats()->channels();
  EXPECT_EQ(channels.size(), xcvr->numMediaLanes());
  for (const auto& channel : channels) {
    EXPECT_FALSE(channel.sensors()->rxPwrdBm().has_value());
    EXPECT_FALSE(channel.sensors()->txPwrdBm().has_value());
  }

  EXPECT_TRUE(info.tcvrState()->errorStates()->empty());
}

TEST_F(CmisTest, cmis1600GAecInfoTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis1600GAec>(
      xcvrID, TransceiverModuleIdentifier::OSFP);
  const auto& info = xcvr->getTransceiverInfo();

  EXPECT_EQ(
      info.tcvrState()->transceiverManagementInterface(),
      TransceiverManagementInterface::CMIS);
  // Active cable, copper transmitter: an AEC, not an AOC.
  EXPECT_EQ(
      *info.tcvrState()->cable()->mediaTypeEncoding(),
      MediaTypeEncodings::ACTIVE_CABLES);
  EXPECT_EQ(
      *info.tcvrState()->cable()->transmitterTech(),
      TransmitterTechnology::COPPER);

  // The module-level code comes from the widest advertised application
  // (1.6TAUI-8 over 8 lanes), not from a hardcoded CR8_800G.
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(), MediaInterfaceCode::CR8_1600G);
  EXPECT_EQ(xcvr->numHostLanes(), 8);
  EXPECT_EQ(xcvr->numMediaLanes(), 8);

  // Every advertised application resolves, on each of its advertised host
  // start lanes, with the host lane count from the advertisement.
  const std::vector<
      std::tuple<ActiveCuHostInterfaceCode, uint8_t, std::vector<int>>>
      expected = {
          {ActiveCuHostInterfaceCode::AUI_PAM4_1_200G,
           1,
           {0, 1, 2, 3, 4, 5, 6, 7}},
          {ActiveCuHostInterfaceCode::AUI_PAM4_2_400G, 2, {0, 2, 4, 6}},
          {ActiveCuHostInterfaceCode::AUI_PAM4_4_800G, 4, {0, 4}},
          {ActiveCuHostInterfaceCode::AUI_PAM4_8_1600G, 8, {0}},
      };
  for (const auto& [application, hostLaneCount, startLanes] : expected) {
    for (uint8_t lane = 0; lane <= 7; lane++) {
      auto field =
          xcvr->getApplicationField(static_cast<uint8_t>(application), lane);
      bool isStartLane =
          std::find(startLanes.begin(), startLanes.end(), lane) !=
          startLanes.end();
      if (!isStartLane) {
        EXPECT_EQ(field, std::nullopt)
            << "application " << apache::thrift::util::enumNameSafe(application)
            << " lane " << static_cast<int>(lane);
        continue;
      }
      ASSERT_NE(field, std::nullopt)
          << "application " << apache::thrift::util::enumNameSafe(application)
          << " lane " << static_cast<int>(lane);
      EXPECT_EQ(field->hostLaneCount, hostLaneCount);
      EXPECT_EQ(field->mediaLaneCount, hostLaneCount);
    }
  }

  // The 100G/lane codes this module does not advertise must not resolve.
  for (auto unsupported :
       {ActiveCuHostInterfaceCode::AUI_PAM4_8S_800G,
        ActiveCuHostInterfaceCode::AUI_PAM4_4S_400G,
        ActiveCuHostInterfaceCode::AUI_PAM4_1S_100G}) {
    EXPECT_EQ(
        xcvr->getApplicationField(static_cast<uint8_t>(unsupported), 0),
        std::nullopt);
  }

  // Every breakout the module advertises, at the right lane count.
  const std::vector<std::tuple<cfg::PortSpeed, uint8_t, uint8_t>> goodStates = {
      {cfg::PortSpeed::ONEPOINTSIXT, 0, 8},
      {cfg::PortSpeed::EIGHTHUNDREDG, 0, 4},
      {cfg::PortSpeed::EIGHTHUNDREDG, 4, 4},
      {cfg::PortSpeed::FOURHUNDREDG, 0, 2},
      {cfg::PortSpeed::FOURHUNDREDG, 6, 2},
      {cfg::PortSpeed::TWOHUNDREDG, 0, 1},
      {cfg::PortSpeed::TWOHUNDREDG, 7, 1},
  };
  for (const auto& [speed, startLane, numLanes] : goodStates) {
    TransceiverPortState portState{
        "", startLane, speed, numLanes, TransmitterTechnology::COPPER};
    EXPECT_TRUE(xcvr->tcvrPortStateSupported(portState))
        << apache::thrift::util::enumNameSafe(speed) << " startLane "
        << static_cast<int>(startLane) << " numLanes "
        << static_cast<int>(numLanes);
  }

  // Right speed, wrong lane count. Before the host lane count was compared at
  // every speed, 800G-on-8-lanes was accepted here and would have programmed
  // the 4-lane 800GAUI-4 application onto an 8-lane port.
  const std::vector<std::tuple<cfg::PortSpeed, uint8_t, uint8_t>> badStates = {
      {cfg::PortSpeed::EIGHTHUNDREDG, 0, 8},
      {cfg::PortSpeed::ONEPOINTSIXT, 0, 4},
      {cfg::PortSpeed::FOURHUNDREDG, 0, 4},
      {cfg::PortSpeed::TWOHUNDREDG, 0, 2},
      // Advertised lane count is right but the start lane is not advertised.
      {cfg::PortSpeed::EIGHTHUNDREDG, 2, 4},
      {cfg::PortSpeed::FOURHUNDREDG, 1, 2},
      // Speed the module does not advertise at all.
      {cfg::PortSpeed::HUNDREDG, 0, 1},
  };
  for (const auto& [speed, startLane, numLanes] : badStates) {
    TransceiverPortState portState{
        "", startLane, speed, numLanes, TransmitterTechnology::COPPER};
    EXPECT_FALSE(xcvr->tcvrPortStateSupported(portState))
        << apache::thrift::util::enumNameSafe(speed) << " startLane "
        << static_cast<int>(startLane) << " numLanes "
        << static_cast<int>(numLanes);
  }

  EXPECT_TRUE(info.tcvrState()->errorStates()->empty());
}

// getAppSelCodeForSpeed() chooses which application a port is programmed with.
// A speed can be served by applications with different host lane counts -- 800G
// as 8x100G via 800GAUI-8, or as 4x200G via 800GAUI-4 -- so a request must only
// ever resolve to an application whose lane count matches the port's. This
// fixture advertises only the 200G/lane block, so its 4-lane 800G application
// must never be handed to an 8-lane port.
TEST_F(CmisTest, cmis1600GAecAppSelForSpeedTest) {
  auto xcvr = overrideCmisModule<Cmis1600GAec>(
      TransceiverID(1), TransceiverModuleIdentifier::OSFP);
  xcvr->getTransceiverInfo();

  // Every advertised breakout resolves to its own application, on each of that
  // application's advertised host start lanes.
  const std::vector<std::tuple<
      cfg::PortSpeed,
      uint8_t,
      uint8_t,
      uint8_t,
      ActiveCuHostInterfaceCode>>
      expected = {
          {cfg::PortSpeed::ONEPOINTSIXT,
           0,
           8,
           2,
           ActiveCuHostInterfaceCode::AUI_PAM4_8_1600G},
          {cfg::PortSpeed::EIGHTHUNDREDG,
           0,
           4,
           3,
           ActiveCuHostInterfaceCode::AUI_PAM4_4_800G},
          {cfg::PortSpeed::EIGHTHUNDREDG,
           4,
           4,
           3,
           ActiveCuHostInterfaceCode::AUI_PAM4_4_800G},
          {cfg::PortSpeed::FOURHUNDREDG,
           0,
           2,
           4,
           ActiveCuHostInterfaceCode::AUI_PAM4_2_400G},
          {cfg::PortSpeed::FOURHUNDREDG,
           6,
           2,
           4,
           ActiveCuHostInterfaceCode::AUI_PAM4_2_400G},
      };
  for (const auto& [speed, startLane, numLanes, apSel, code] : expected) {
    auto field =
        xcvr->getAppSelCodeForSpeed("eth1/1/1", speed, startLane, numLanes);
    ASSERT_NE(field, std::nullopt)
        << apache::thrift::util::enumNameSafe(speed) << " startLane "
        << static_cast<int>(startLane);
    EXPECT_EQ(field->ApSelCode, apSel);
    EXPECT_EQ(field->moduleMediaInterface, static_cast<uint8_t>(code));
    EXPECT_EQ(field->hostLaneCount, numLanes);
  }

  // The fixture comes up in its 8x200G default, so asking for the application
  // it is already running reports "nothing to program" rather than a field.
  EXPECT_EQ(
      xcvr->getAppSelCodeForSpeed(
          "eth1/1/1", cfg::PortSpeed::TWOHUNDREDG, 0, 1),
      std::nullopt);

  // Right speed, wrong lane count. Until the host lane count was compared at
  // every speed, 800G and 1.6T skipped the check entirely: an 8-lane 800G
  // request resolved to the 4-lane 800GAUI-4 application, and a 4-lane 1.6T
  // request to the 8-lane 1.6TAUI-8 one.
  const std::vector<std::tuple<cfg::PortSpeed, uint8_t, uint8_t>> mismatched = {
      {cfg::PortSpeed::EIGHTHUNDREDG, 0, 8},
      {cfg::PortSpeed::ONEPOINTSIXT, 0, 4},
      {cfg::PortSpeed::FOURHUNDREDG, 0, 4},
      {cfg::PortSpeed::TWOHUNDREDG, 0, 2},
  };
  for (const auto& [speed, startLane, numLanes] : mismatched) {
    EXPECT_THROW(
        xcvr->getAppSelCodeForSpeed("eth1/1/1", speed, startLane, numLanes),
        FbossError)
        << apache::thrift::util::enumNameSafe(speed) << " on "
        << static_cast<int>(numLanes) << " lanes";
  }
}

TEST_F(CmisTest, cmis2x400GFr4TransceiverVdmTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis2x400GFr4Transceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_TRUE(info.tcvrState()->transceiverManagementInterface());
  EXPECT_EQ(
      info.tcvrState()->transceiverManagementInterface(),
      TransceiverManagementInterface::CMIS);
  EXPECT_EQ(xcvr->numHostLanes(), 8);
  EXPECT_EQ(xcvr->numMediaLanes(), 8);
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(), MediaInterfaceCode::FR4_2x400G);

  utility::HwTransceiverUtils::verifyDiagsCapability(
      *info.tcvrState(),
      transceiverManager_->getDiagsCapability(xcvrID),
      false /* skipCheckingIndividualCapability */);

  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETEST");

  auto diagsCap = transceiverManager_->getDiagsCapability(xcvrID);
  EXPECT_TRUE(diagsCap.has_value());

  EXPECT_TRUE(diagsCap.value().vdm().value());
  EXPECT_TRUE(xcvr->isVdmSupported());
  EXPECT_TRUE(xcvr->isVdmSupported(3));

  auto vdmLocationInfo = xcvr->getVdmDiagsValLocation(SNR_MEDIA_IN);
  EXPECT_TRUE(vdmLocationInfo.vdmConfImplementedByModule);
  EXPECT_EQ(vdmLocationInfo.vdmValPage, static_cast<CmisPages>(0x24));
  EXPECT_EQ(vdmLocationInfo.vdmValOffset, 128);
  EXPECT_EQ(vdmLocationInfo.vdmValLength, 16);
  vdmLocationInfo = xcvr->getVdmDiagsValLocation(PAM4_LTP_MEDIA_IN);
  EXPECT_TRUE(vdmLocationInfo.vdmConfImplementedByModule);
  EXPECT_EQ(vdmLocationInfo.vdmValPage, static_cast<CmisPages>(0x24));
  EXPECT_EQ(vdmLocationInfo.vdmValOffset, 144);
  EXPECT_EQ(vdmLocationInfo.vdmValLength, 16);
  vdmLocationInfo = xcvr->getVdmDiagsValLocation(PRE_FEC_BER_MEDIA_IN_MAX);
  EXPECT_TRUE(vdmLocationInfo.vdmConfImplementedByModule);
  EXPECT_EQ(vdmLocationInfo.vdmValPage, static_cast<CmisPages>(0x24));
  EXPECT_EQ(vdmLocationInfo.vdmValOffset, 160);
  EXPECT_EQ(vdmLocationInfo.vdmValLength, 16);
  vdmLocationInfo = xcvr->getVdmDiagsValLocation(PRE_FEC_BER_MEDIA_IN_AVG);
  EXPECT_TRUE(vdmLocationInfo.vdmConfImplementedByModule);
  EXPECT_EQ(vdmLocationInfo.vdmValPage, static_cast<CmisPages>(0x24));
  EXPECT_EQ(vdmLocationInfo.vdmValOffset, 176);
  EXPECT_EQ(vdmLocationInfo.vdmValLength, 16);
  vdmLocationInfo = xcvr->getVdmDiagsValLocation(ERR_FRAME_MEDIA_IN_MAX);
  EXPECT_TRUE(vdmLocationInfo.vdmConfImplementedByModule);
  EXPECT_EQ(vdmLocationInfo.vdmValPage, static_cast<CmisPages>(0x24));
  EXPECT_EQ(vdmLocationInfo.vdmValOffset, 192);
  EXPECT_EQ(vdmLocationInfo.vdmValLength, 16);
  vdmLocationInfo = xcvr->getVdmDiagsValLocation(ERR_FRAME_MEDIA_IN_AVG);
  EXPECT_TRUE(vdmLocationInfo.vdmConfImplementedByModule);
  EXPECT_EQ(vdmLocationInfo.vdmValPage, static_cast<CmisPages>(0x24));
  EXPECT_EQ(vdmLocationInfo.vdmValOffset, 208);
  EXPECT_EQ(vdmLocationInfo.vdmValLength, 16);
  vdmLocationInfo = xcvr->getVdmDiagsValLocation(FEC_TAIL_MEDIA_IN_MAX);
  EXPECT_TRUE(vdmLocationInfo.vdmConfImplementedByModule);
  EXPECT_EQ(vdmLocationInfo.vdmValPage, static_cast<CmisPages>(0x24));
  EXPECT_EQ(vdmLocationInfo.vdmValOffset, 224);
  EXPECT_EQ(vdmLocationInfo.vdmValLength, 16);
  vdmLocationInfo = xcvr->getVdmDiagsValLocation(FEC_TAIL_MEDIA_IN_CURR);
  EXPECT_TRUE(vdmLocationInfo.vdmConfImplementedByModule);
  EXPECT_EQ(vdmLocationInfo.vdmValPage, static_cast<CmisPages>(0x24));
  EXPECT_EQ(vdmLocationInfo.vdmValOffset, 240);
  EXPECT_EQ(vdmLocationInfo.vdmValLength, 16);
  vdmLocationInfo = xcvr->getVdmDiagsValLocation(SNR_HOST_IN);
  EXPECT_TRUE(vdmLocationInfo.vdmConfImplementedByModule);
  EXPECT_EQ(vdmLocationInfo.vdmValPage, static_cast<CmisPages>(0x25));
  EXPECT_EQ(vdmLocationInfo.vdmValOffset, 128);
  EXPECT_EQ(vdmLocationInfo.vdmValLength, 16);
  vdmLocationInfo = xcvr->getVdmDiagsValLocation(PRE_FEC_BER_HOST_IN_MAX);
  EXPECT_TRUE(vdmLocationInfo.vdmConfImplementedByModule);
  EXPECT_EQ(vdmLocationInfo.vdmValPage, static_cast<CmisPages>(0x25));
  EXPECT_EQ(vdmLocationInfo.vdmValOffset, 160);
  EXPECT_EQ(vdmLocationInfo.vdmValLength, 16);
  vdmLocationInfo = xcvr->getVdmDiagsValLocation(PRE_FEC_BER_HOST_IN_AVG);
  EXPECT_TRUE(vdmLocationInfo.vdmConfImplementedByModule);
  EXPECT_EQ(vdmLocationInfo.vdmValPage, static_cast<CmisPages>(0x25));
  EXPECT_EQ(vdmLocationInfo.vdmValOffset, 176);
  EXPECT_EQ(vdmLocationInfo.vdmValLength, 16);
  vdmLocationInfo = xcvr->getVdmDiagsValLocation(ERR_FRAME_HOST_IN_MAX);
  EXPECT_TRUE(vdmLocationInfo.vdmConfImplementedByModule);
  EXPECT_EQ(vdmLocationInfo.vdmValPage, static_cast<CmisPages>(0x25));
  EXPECT_EQ(vdmLocationInfo.vdmValOffset, 192);
  EXPECT_EQ(vdmLocationInfo.vdmValLength, 16);
  vdmLocationInfo = xcvr->getVdmDiagsValLocation(ERR_FRAME_HOST_IN_AVG);
  EXPECT_TRUE(vdmLocationInfo.vdmConfImplementedByModule);
  EXPECT_EQ(vdmLocationInfo.vdmValPage, static_cast<CmisPages>(0x25));
  EXPECT_EQ(vdmLocationInfo.vdmValOffset, 208);
  EXPECT_EQ(vdmLocationInfo.vdmValLength, 16);
  vdmLocationInfo = xcvr->getVdmDiagsValLocation(FEC_TAIL_HOST_IN_MAX);
  EXPECT_TRUE(vdmLocationInfo.vdmConfImplementedByModule);
  EXPECT_EQ(vdmLocationInfo.vdmValPage, static_cast<CmisPages>(0x25));
  EXPECT_EQ(vdmLocationInfo.vdmValOffset, 224);
  EXPECT_EQ(vdmLocationInfo.vdmValLength, 16);
  vdmLocationInfo = xcvr->getVdmDiagsValLocation(FEC_TAIL_HOST_IN_CURR);
  EXPECT_TRUE(vdmLocationInfo.vdmConfImplementedByModule);
  EXPECT_EQ(vdmLocationInfo.vdmValPage, static_cast<CmisPages>(0x25));
  EXPECT_EQ(vdmLocationInfo.vdmValOffset, 240);
  EXPECT_EQ(vdmLocationInfo.vdmValLength, 16);
  vdmLocationInfo =
      xcvr->getVdmDiagsValLocation(PAM4_LEVEL0_STANDARD_DEVIATION_LINE);
  EXPECT_TRUE(vdmLocationInfo.vdmConfImplementedByModule);
  EXPECT_EQ(vdmLocationInfo.vdmValPage, static_cast<CmisPages>(0x26));
  EXPECT_EQ(vdmLocationInfo.vdmValOffset, 128);
  EXPECT_EQ(vdmLocationInfo.vdmValLength, 16);
  vdmLocationInfo =
      xcvr->getVdmDiagsValLocation(PAM4_LEVEL1_STANDARD_DEVIATION_LINE);
  EXPECT_TRUE(vdmLocationInfo.vdmConfImplementedByModule);
  EXPECT_EQ(vdmLocationInfo.vdmValPage, static_cast<CmisPages>(0x26));
  EXPECT_EQ(vdmLocationInfo.vdmValOffset, 144);
  EXPECT_EQ(vdmLocationInfo.vdmValLength, 16);
  vdmLocationInfo =
      xcvr->getVdmDiagsValLocation(PAM4_LEVEL2_STANDARD_DEVIATION_LINE);
  EXPECT_TRUE(vdmLocationInfo.vdmConfImplementedByModule);
  EXPECT_EQ(vdmLocationInfo.vdmValPage, static_cast<CmisPages>(0x26));
  EXPECT_EQ(vdmLocationInfo.vdmValOffset, 160);
  EXPECT_EQ(vdmLocationInfo.vdmValLength, 16);
  vdmLocationInfo =
      xcvr->getVdmDiagsValLocation(PAM4_LEVEL3_STANDARD_DEVIATION_LINE);
  EXPECT_TRUE(vdmLocationInfo.vdmConfImplementedByModule);
  EXPECT_EQ(vdmLocationInfo.vdmValPage, static_cast<CmisPages>(0x26));
  EXPECT_EQ(vdmLocationInfo.vdmValOffset, 176);
  EXPECT_EQ(vdmLocationInfo.vdmValLength, 16);
  vdmLocationInfo = xcvr->getVdmDiagsValLocation(PAM4_MPI_LINE);
  EXPECT_TRUE(vdmLocationInfo.vdmConfImplementedByModule);
  EXPECT_EQ(vdmLocationInfo.vdmValPage, static_cast<CmisPages>(0x26));
  EXPECT_EQ(vdmLocationInfo.vdmValOffset, 192);
  EXPECT_EQ(vdmLocationInfo.vdmValLength, 16);

  // Check the VDM stats values now
  ProgramTransceiverState programTcvrState;
  TransceiverPortState portState;
  portState.portName = "eth1/1/1";
  portState.startHostLane = 0;
  portState.speed = cfg::PortSpeed::FOURHUNDREDG;
  portState.numHostLanes = 4;
  programTcvrState.ports.emplace(portState.portName, portState);
  portState.portName = "eth1/1/5";
  portState.startHostLane = 4;
  portState.speed = cfg::PortSpeed::FOURHUNDREDG;
  portState.numHostLanes = 4;
  programTcvrState.ports.emplace(portState.portName, portState);

  xcvr->programTransceiver(programTcvrState, false);

  std::vector<int32_t> xcvrIds = {xcvrID};
  transceiverManager_->triggerVdmStatsCapture(xcvrIds);

  // VDM ForOds capture is asynchronous: the first refresh after the trigger
  // writes FreezeRequest, and the next refresh reads the frozen snapshot and
  // populates the *ForOds fields. Drive both cycles before checking stats.
  transceiverManager_->refreshStateMachines();
  transceiverManager_->refreshStateMachines();
  const auto& newInfo = xcvr->getTransceiverInfo();
  EXPECT_TRUE(newInfo.tcvrStats()->vdmPerfMonitorStats().has_value());
  EXPECT_EQ(
      newInfo.tcvrStats()
          ->vdmPerfMonitorStats()
          ->mediaPortVdmStats()
          .value()
          .size(),
      2);
  EXPECT_EQ(
      newInfo.tcvrStats()
          ->vdmPerfMonitorStats()
          ->hostPortVdmStats()
          .value()
          .size(),
      2);
  for (auto& [pName, stats] : newInfo.tcvrStats()
                                  ->vdmPerfMonitorStats()
                                  ->mediaPortVdmStats()
                                  .value()) {
    EXPECT_EQ(stats.laneSNR().value().size(), 4);
  }
  EXPECT_TRUE(newInfo.tcvrStats()->vdmPerfMonitorStatsForOds().has_value());
  EXPECT_EQ(
      newInfo.tcvrStats()
          ->vdmPerfMonitorStatsForOds()
          ->mediaPortVdmStats()
          .value()
          .size(),
      2);
  EXPECT_EQ(
      int(newInfo.tcvrStats()
              ->vdmPerfMonitorStatsForOds()
              ->mediaPortVdmStats()
              .value()
              .begin()
              ->second.laneSNRMin()
              .value() *
          100),
      2103);
  EXPECT_EQ(
      int(newInfo.tcvrStats()
              ->vdmPerfMonitorStatsForOds()
              ->mediaPortVdmStats()
              .value()
              .begin()
              ->second.lanePam4Level0SDMax()
              .value() *
          100),
      178);
  EXPECT_EQ(
      int(newInfo.tcvrStats()
              ->vdmPerfMonitorStatsForOds()
              ->mediaPortVdmStats()
              .value()
              .begin()
              ->second.lanePam4Level1SDMax()
              .value() *
          100),
      184);
  EXPECT_EQ(
      int(newInfo.tcvrStats()
              ->vdmPerfMonitorStatsForOds()
              ->mediaPortVdmStats()
              .value()
              .begin()
              ->second.lanePam4Level2SDMax()
              .value() *
          100),
      217);
  EXPECT_EQ(
      int(newInfo.tcvrStats()
              ->vdmPerfMonitorStatsForOds()
              ->mediaPortVdmStats()
              .value()
              .begin()
              ->second.lanePam4Level3SDMax()
              .value() *
          100),
      213);
  EXPECT_EQ(
      int(newInfo.tcvrStats()
              ->vdmPerfMonitorStatsForOds()
              ->mediaPortVdmStats()
              .value()
              .begin()
              ->second.lanePam4MPIMax()
              .value() *
          100),
      132);
  EXPECT_EQ(
      newInfo.tcvrStats()
          ->vdmPerfMonitorStatsForOds()
          ->mediaPortVdmStats()
          .value()
          .begin()
          ->second.fecTailMax()
          .value(),
      2);
  EXPECT_EQ(
      newInfo.tcvrStats()
          ->vdmPerfMonitorStatsForOds()
          ->hostPortVdmStats()
          .value()
          .begin()
          ->second.fecTailMax()
          .value(),
      3);

  EXPECT_EQ(
      newInfo.tcvrStats()
          ->vdmPerfMonitorStatsForOds()
          ->hostPortVdmStats()
          .value()
          .size(),
      2);
}

// The VDM ForOds capture is asynchronous and non-blocking: the StatsPublisher
// trigger makes the next refresh write FreezeRequest (2Fh:144 bit7) WITHOUT
// waiting for FreezeDone, and the following refresh reads the frozen snapshot
// and clears FreezeRequest. This keeps the slow freeze wait off the refresh
// thread. Verify the two-cycle split (one FreezeRequest write per cycle, not
// set+clear in a single cycle) and that *ForOds only advances on the second
// refresh.
TEST_F(CmisTest, cmisVdmForOdsCaptureIsAsync) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis2x400GFr4Transceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);
  auto* qsfpImpl = lastQsfpImpl();
  ASSERT_TRUE(xcvr->isVdmSupported());

  // Program the module so the VDM cache is valid and perf-monitor stats parse.
  ProgramTransceiverState programTcvrState;
  TransceiverPortState portState;
  portState.portName = "eth1/1/1";
  portState.startHostLane = 0;
  portState.speed = cfg::PortSpeed::FOURHUNDREDG;
  portState.numHostLanes = 4;
  programTcvrState.ports.emplace(portState.portName, portState);
  portState.portName = "eth1/1/5";
  portState.startHostLane = 4;
  portState.speed = cfg::PortSpeed::FOURHUNDREDG;
  portState.numHostLanes = 4;
  programTcvrState.ports.emplace(portState.portName, portState);
  xcvr->programTransceiver(programTcvrState, false);

  // Baseline: with no capture triggered, a refresh writes no FreezeRequest and
  // leaves *ForOds unset.
  const int baseFreezeWrites = qsfpImpl->getUpperPageWriteCount(
      kPage2f, kVdmLatchRequestUpperPageOffset);
  transceiverManager_->refreshStateMachines();
  EXPECT_EQ(
      qsfpImpl->getUpperPageWriteCount(
          kPage2f, kVdmLatchRequestUpperPageOffset),
      baseFreezeWrites);
  EXPECT_FALSE(xcvr->getTransceiverInfo()
                   .tcvrStats()
                   ->vdmPerfMonitorStatsForOds()
                   .has_value());

  // Cycle N: trigger + refresh writes FreezeRequest exactly once (the request)
  // and does NOT read the frozen snapshot, so *ForOds stays unset. The old
  // blocking code wrote set+clear (two writes) in this single refresh.
  std::vector<int32_t> xcvrIds = {xcvrID};
  transceiverManager_->triggerVdmStatsCapture(xcvrIds);
  transceiverManager_->refreshStateMachines();
  EXPECT_EQ(
      qsfpImpl->getUpperPageWriteCount(
          kPage2f, kVdmLatchRequestUpperPageOffset),
      baseFreezeWrites + 1);
  EXPECT_FALSE(xcvr->getTransceiverInfo()
                   .tcvrStats()
                   ->vdmPerfMonitorStatsForOds()
                   .has_value());

  // Cycle N+1: the next refresh reads the frozen snapshot, clears FreezeRequest
  // (the second write), and now *ForOds is populated.
  transceiverManager_->refreshStateMachines();
  EXPECT_EQ(
      qsfpImpl->getUpperPageWriteCount(
          kPage2f, kVdmLatchRequestUpperPageOffset),
      baseFreezeWrites + 2);
  EXPECT_TRUE(xcvr->getTransceiverInfo()
                  .tcvrStats()
                  ->vdmPerfMonitorStatsForOds()
                  .has_value());
}

TEST_F(CmisTest, cmis2x400GFr4DatapathProgramTest) {
  TransceiverPropertiesManager::initDefault();
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis2x400GFr4Transceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_TRUE(info.tcvrState()->transceiverManagementInterface());
  EXPECT_EQ(
      info.tcvrState()->transceiverManagementInterface(),
      TransceiverManagementInterface::CMIS);
  EXPECT_EQ(xcvr->numHostLanes(), 8);
  EXPECT_EQ(xcvr->numMediaLanes(), 8);

  EXPECT_TRUE(xcvr->isRequestValidMultiportSpeedConfig(
      cfg::PortSpeed::FOURHUNDREDG, 0, 4));
  EXPECT_TRUE(xcvr->isRequestValidMultiportSpeedConfig(
      cfg::PortSpeed::FOURHUNDREDG, 4, 4));
  EXPECT_TRUE(xcvr->isRequestValidMultiportSpeedConfig(
      cfg::PortSpeed::TWOHUNDREDG, 0, 4));
  EXPECT_TRUE(xcvr->isRequestValidMultiportSpeedConfig(
      cfg::PortSpeed::TWOHUNDREDG, 4, 4));
  EXPECT_FALSE(xcvr->isRequestValidMultiportSpeedConfig(
      cfg::PortSpeed::TWOHUNDREDG, 2, 4));
  EXPECT_FALSE(
      xcvr->isRequestValidMultiportSpeedConfig(cfg::PortSpeed::HUNDREDG, 0, 1));
  EXPECT_FALSE(
      xcvr->isRequestValidMultiportSpeedConfig(cfg::PortSpeed::HUNDREDG, 0, 4));
  EXPECT_FALSE(
      xcvr->isRequestValidMultiportSpeedConfig(cfg::PortSpeed::HUNDREDG, 4, 4));

  auto fr4Combos =
      TransceiverPropertiesManager::getSpeedCombinations<SMFMediaInterfaceCode>(
          MediaInterfaceCode::FR4_2x400G);

  auto fr4_400gCodes = TransceiverPropertiesManager::getMediaCodesForSpeed<
      SMFMediaInterfaceCode>(
      MediaInterfaceCode::FR4_2x400G, cfg::PortSpeed::FOURHUNDREDG);
  SmfSpeedApplicationMap fr4_400gMapping;
  fr4_400gMapping[cfg::PortSpeed::FOURHUNDREDG] = fr4_400gCodes;

  auto fr4_100gCodes = TransceiverPropertiesManager::getMediaCodesForSpeed<
      SMFMediaInterfaceCode>(
      MediaInterfaceCode::FR4_2x400G, cfg::PortSpeed::HUNDREDG);
  SmfSpeedApplicationMap fr4_100gMapping;
  fr4_100gMapping[cfg::PortSpeed::HUNDREDG] = fr4_100gCodes;

  auto fr4_fr1Codes = TransceiverPropertiesManager::getMediaCodesForSpeed<
      SMFMediaInterfaceCode>(
      MediaInterfaceCode::FR4_2x400G,
      cfg::PortSpeed::HUNDREDANDSIXPOINTTWOFIVEG);
  SmfSpeedApplicationMap fr4_fr1Mapping;
  fr4_fr1Mapping[cfg::PortSpeed::HUNDREDANDSIXPOINTTWOFIVEG] = fr4_fr1Codes;

  auto speedCfgCombo = CmisHelper::getValidMultiportSpeedConfig(
      cfg::PortSpeed::FOURHUNDREDG,
      0,
      4,
      CmisModule::laneMask(0, 4),
      "tcvr1",
      xcvr->getModuleCapabilities(),
      fr4Combos,
      fr4_400gMapping);
  EXPECT_EQ(speedCfgCombo.size(), CmisModule::kMaxOsfpNumLanes);
  EXPECT_EQ(speedCfgCombo[0], (uint8_t)SMFMediaInterfaceCode::FR4_400G);

  speedCfgCombo = CmisHelper::getValidMultiportSpeedConfig(
      cfg::PortSpeed::FOURHUNDREDG,
      4,
      4,
      CmisModule::laneMask(4, 4),
      "tcvr1",
      xcvr->getModuleCapabilities(),
      fr4Combos,
      fr4_400gMapping);
  EXPECT_EQ(speedCfgCombo.size(), CmisModule::kMaxOsfpNumLanes);
  EXPECT_EQ(speedCfgCombo[4], (uint8_t)SMFMediaInterfaceCode::FR4_400G);

  speedCfgCombo = CmisHelper::getValidMultiportSpeedConfig(
      cfg::PortSpeed::HUNDREDG,
      4,
      4,
      CmisModule::laneMask(4, 4),
      "tcvr1",
      xcvr->getModuleCapabilities(),
      fr4Combos,
      fr4_100gMapping);
  EXPECT_EQ(speedCfgCombo.size(), CmisModule::kMaxOsfpNumLanes);
  EXPECT_EQ(speedCfgCombo[4], (uint8_t)SMFMediaInterfaceCode::CWDM4_100G);

  speedCfgCombo = CmisHelper::getValidMultiportSpeedConfig(
      cfg::PortSpeed::HUNDREDANDSIXPOINTTWOFIVEG,
      5,
      1,
      CmisModule::laneMask(4, 4),
      "tcvr1",
      xcvr->getModuleCapabilities(),
      fr4Combos,
      fr4_fr1Mapping);
  EXPECT_EQ(speedCfgCombo.size(), CmisModule::kMaxOsfpNumLanes);
  for (auto& speed : speedCfgCombo) {
    EXPECT_EQ(speed, (uint8_t)SMFMediaInterfaceCode::FR1_100G);
  }
  EXPECT_TRUE(info.tcvrState()->errorStates()->empty());
}

TEST_F(CmisTest, cmis2x400GDr4TransceiverInfoTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis2x400GDr4Transceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_TRUE(info.tcvrState()->transceiverManagementInterface());
  EXPECT_EQ(
      info.tcvrState()->transceiverManagementInterface(),
      TransceiverManagementInterface::CMIS);
  EXPECT_EQ(xcvr->numHostLanes(), 8);
  EXPECT_EQ(xcvr->numMediaLanes(), 8);
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(), MediaInterfaceCode::DR4_2x400G);
  for (auto& media : *info.tcvrState()->settings()->mediaInterface()) {
    EXPECT_EQ(
        media.media()->smfCode().value(), SMFMediaInterfaceCode::DR4_400G);
    EXPECT_EQ(media.code(), MediaInterfaceCode::DR4_400G);
  }

  // Check cmisStateChanged
  EXPECT_TRUE(
      info.tcvrState()->status() &&
      info.tcvrState()->status()->cmisStateChanged() &&
      *info.tcvrState()->status()->cmisStateChanged());

  utility::HwTransceiverUtils::verifyDiagsCapability(
      *info.tcvrState(),
      transceiverManager_->getDiagsCapability(xcvrID),
      false /* skipCheckingIndividualCapability */);

  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETEST");

  auto diagsCap = transceiverManager_->getDiagsCapability(xcvrID);
  EXPECT_TRUE(diagsCap.has_value());
  std::vector<prbs::PrbsPolynomial> expectedSysPolynomials = {
      prbs::PrbsPolynomial::PRBS31Q,
      prbs::PrbsPolynomial::PRBS23Q,
      prbs::PrbsPolynomial::PRBS15Q,
      prbs::PrbsPolynomial::PRBS13Q,
      prbs::PrbsPolynomial::PRBS9Q,
      prbs::PrbsPolynomial::PRBS7Q,
      prbs::PrbsPolynomial::PRBS31,
      prbs::PrbsPolynomial::PRBS23,
      prbs::PrbsPolynomial::PRBS15,
      prbs::PrbsPolynomial::PRBS13,
      prbs::PrbsPolynomial::PRBS9,
      prbs::PrbsPolynomial::PRBS7};
  std::vector<prbs::PrbsPolynomial> expectedLinePolynomials = {
      prbs::PrbsPolynomial::PRBS31Q,
      prbs::PrbsPolynomial::PRBS23Q,
      prbs::PrbsPolynomial::PRBS15Q,
      prbs::PrbsPolynomial::PRBS13Q,
      prbs::PrbsPolynomial::PRBS9Q,
      prbs::PrbsPolynomial::PRBS7Q,
      prbs::PrbsPolynomial::PRBS31,
      prbs::PrbsPolynomial::PRBS23,
      prbs::PrbsPolynomial::PRBS15,
      prbs::PrbsPolynomial::PRBS13,
      prbs::PrbsPolynomial::PRBS9,
      prbs::PrbsPolynomial::PRBS7};

  auto linePrbsCapability = *(*diagsCap).prbsLineCapabilities();
  auto sysPrbsCapability = *(*diagsCap).prbsSystemCapabilities();

  tests.verifyPrbsPolynomials(expectedLinePolynomials, linePrbsCapability);
  tests.verifyPrbsPolynomials(expectedSysPolynomials, sysPrbsCapability);

  for (auto unsupportedApplication :
       {SMFMediaInterfaceCode::LR4_10_400G,
        SMFMediaInterfaceCode::FR4_400G,
        SMFMediaInterfaceCode::FR1_100G,
        SMFMediaInterfaceCode::FR4_200G,
        SMFMediaInterfaceCode::CWDM4_100G}) {
    EXPECT_EQ(
        xcvr->getApplicationField(
            static_cast<uint8_t>(unsupportedApplication), 0),
        std::nullopt);
  }

  for (auto supportedApplication : {SMFMediaInterfaceCode::DR4_400G}) {
    auto applicationField = xcvr->getApplicationField(
        static_cast<uint8_t>(supportedApplication), 0);
    EXPECT_NE(applicationField, std::nullopt);
    std::vector<int> expectedStartLanes = {0, 4};
    EXPECT_EQ(applicationField->hostStartLanes, expectedStartLanes);
    EXPECT_EQ(applicationField->mediaStartLanes, expectedStartLanes);
  }

  EXPECT_TRUE(diagsCap.value().vdm().value());
  EXPECT_TRUE(diagsCap.value().cdb().value());
  EXPECT_TRUE(diagsCap.value().prbsLine().value());
  EXPECT_TRUE(diagsCap.value().prbsSystem().value());
  EXPECT_TRUE(diagsCap.value().loopbackLine().value());
  EXPECT_TRUE(diagsCap.value().loopbackSystem().value());
  EXPECT_TRUE(diagsCap.value().txOutputControl().value());
  EXPECT_TRUE(diagsCap.value().rxOutputControl().value());
  EXPECT_TRUE(diagsCap.value().snrLine().value());
  EXPECT_TRUE(diagsCap.value().snrSystem().value());
  EXPECT_TRUE(xcvr->isVdmSupported());
  EXPECT_TRUE(xcvr->isPrbsSupported(phy::Side::LINE));
  EXPECT_TRUE(xcvr->isPrbsSupported(phy::Side::SYSTEM));
  EXPECT_TRUE(xcvr->isSnrSupported(phy::Side::LINE));
  EXPECT_TRUE(xcvr->isSnrSupported(phy::Side::SYSTEM));
  EXPECT_TRUE(info.tcvrState()->errorStates()->empty());
}

TEST_F(CmisTest, cmis2x400GXdr4TransceiverInfoTest) {
  // XDR4 is the 2km reach variant of DR4 and advertises the same 400G-DR4
  // application, so it must derive the same media interface as the 500m part
  // despite its longer SMF length.
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis2x400GXdr4Transceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);
  const auto& info = xcvr->getTransceiverInfo();

  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(), MediaInterfaceCode::DR4_2x400G);
  EXPECT_EQ(xcvr->numHostLanes(), 8);
  EXPECT_EQ(xcvr->numMediaLanes(), 8);
  for (auto& media : *info.tcvrState()->settings()->mediaInterface()) {
    EXPECT_EQ(
        media.media()->smfCode().value(), SMFMediaInterfaceCode::DR4_400G);
    EXPECT_EQ(media.code(), MediaInterfaceCode::DR4_400G);
  }
  EXPECT_EQ(*info.tcvrState()->cable()->singleMode(), 2000);
}

TEST_F(CmisTest, vdmPam4MpiAlarmsTest) {
  // This test verifies that PAM4 MPI alarm flags are not raised by default
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis2x400GFr4Transceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_TRUE(info.tcvrState()->transceiverManagementInterface());
  EXPECT_EQ(
      info.tcvrState()->transceiverManagementInterface(),
      TransceiverManagementInterface::CMIS);
  EXPECT_EQ(xcvr->numHostLanes(), 8);
  EXPECT_EQ(xcvr->numMediaLanes(), 8);
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(), MediaInterfaceCode::FR4_2x400G);
  // Verify that VDM group 3 is supported
  EXPECT_TRUE(xcvr->isVdmSupported());
  EXPECT_TRUE(xcvr->isVdmSupported(3));

  ProgramTransceiverState programTcvrState;
  TransceiverPortState portState;
  portState.portName = "eth1/1/1";
  portState.startHostLane = 0;
  portState.speed = cfg::PortSpeed::FOURHUNDREDG;
  portState.numHostLanes = 4;
  programTcvrState.ports.emplace(portState.portName, portState);
  portState.portName = "eth1/1/5";
  portState.startHostLane = 4;
  portState.speed = cfg::PortSpeed::FOURHUNDREDG;
  portState.numHostLanes = 4;
  programTcvrState.ports.emplace(portState.portName, portState);

  xcvr->programTransceiver(programTcvrState, false);

  // Trigger VDM stats capture
  std::vector<int32_t> xcvrIds = {xcvrID};
  transceiverManager_->triggerVdmStatsCapture(xcvrIds);

  transceiverManager_->refreshStateMachines();
  const auto& newInfo = xcvr->getTransceiverInfo();

  // Verify that VDM performance monitor stats are available
  ASSERT_TRUE(newInfo.tcvrStats()->vdmPerfMonitorStats().has_value());

  // Verify that we have stats for both ports
  ASSERT_EQ(
      newInfo.tcvrStats()
          ->vdmPerfMonitorStats()
          ->mediaPortVdmStats()
          .value()
          .size(),
      2);

  // Assert that there are 4 keys in lanePam4MPIFlags().value() for each port
  for (const auto& [portName, portStats] : newInfo.tcvrStats()
                                               ->vdmPerfMonitorStats()
                                               ->mediaPortVdmStats()
                                               .value()) {
    EXPECT_EQ(portStats.lanePam4MPIFlags().value().size(), 4);
  }

  // Check that PAM4 MPI flags are not raised by default
  for (const auto& [portName, portStats] : newInfo.tcvrStats()
                                               ->vdmPerfMonitorStats()
                                               ->mediaPortVdmStats()
                                               .value()) {
    // Check each lane's flags
    for (const auto& [lane, flags] : portStats.lanePam4MPIFlags().value()) {
      // Verify that the flags are not raised
      EXPECT_FALSE(*flags.alarm()->high());
      EXPECT_FALSE(*flags.warn()->high());

      // Low flags should always be false for MPI
      EXPECT_FALSE(*flags.alarm()->low());
      EXPECT_FALSE(*flags.warn()->low());
    }
  }
}

TEST_F(CmisTest, vdmPam4MpiAlarmsRaisedTest) {
  // This test verifies that PAM4 MPI alarm/warning flags are correctly read
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis2x400GFr4WithMpiAlarmsTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);

  // Verify that VDM group 3 is supported
  EXPECT_TRUE(xcvr->isVdmSupported());
  EXPECT_TRUE(xcvr->isVdmSupported(3));

  ProgramTransceiverState programTcvrState;
  TransceiverPortState portState;
  portState.portName = "eth1/1/1";
  portState.startHostLane = 0;
  portState.speed = cfg::PortSpeed::FOURHUNDREDG;
  portState.numHostLanes = 4;
  programTcvrState.ports.emplace(portState.portName, portState);
  portState.portName = "eth1/1/5";
  portState.startHostLane = 4;
  portState.speed = cfg::PortSpeed::FOURHUNDREDG;
  portState.numHostLanes = 4;
  programTcvrState.ports.emplace(portState.portName, portState);

  xcvr->programTransceiver(programTcvrState, false);

  // Trigger VDM stats capture
  std::vector<int32_t> xcvrIds = {xcvrID};
  transceiverManager_->triggerVdmStatsCapture(xcvrIds);

  transceiverManager_->refreshStateMachines();

  const auto& info = xcvr->getTransceiverInfo();

  ASSERT_TRUE(info.tcvrStats()->vdmPerfMonitorStats().has_value());

  // Verify that we have stats for both ports
  ASSERT_EQ(
      info.tcvrStats()
          ->vdmPerfMonitorStats()
          ->mediaPortVdmStats()
          .value()
          .size(),
      2);

  // Check that PAM4 MPI flags are raised as expected
  for (const auto& [portName, portStats] :
       info.tcvrStats()->vdmPerfMonitorStats()->mediaPortVdmStats().value()) {
    // Check each lane's flags
    for (const auto& [lane, flags] : portStats.lanePam4MPIFlags().value()) {
      // Verify that the high alarm and high warning flags are raised
      EXPECT_TRUE(*flags.alarm()->high());
      EXPECT_TRUE(*flags.warn()->high());

      // Low flags should always be false for MPI
      EXPECT_FALSE(*flags.alarm()->low());
      EXPECT_FALSE(*flags.warn()->low());
    }
  }
}

TEST_F(CmisTest, cmis400GDr4TransceiverInfoTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis400GDr4Transceiver>(xcvrID);
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_TRUE(info.tcvrState()->transceiverManagementInterface());
  EXPECT_EQ(
      info.tcvrState()->transceiverManagementInterface(),
      TransceiverManagementInterface::CMIS);
  EXPECT_EQ(xcvr->numHostLanes(), 8);
  EXPECT_EQ(xcvr->numMediaLanes(), 4);
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(), MediaInterfaceCode::DR4_400G);
  for (auto& media : *info.tcvrState()->settings()->mediaInterface()) {
    EXPECT_EQ(
        media.media()->smfCode().value(), SMFMediaInterfaceCode::DR4_400G);
    EXPECT_EQ(media.code(), MediaInterfaceCode::DR4_400G);
  }
  // Check cmisStateChanged
  EXPECT_TRUE(
      info.tcvrState()->status() &&
      info.tcvrState()->status()->cmisStateChanged() &&
      *info.tcvrState()->status()->cmisStateChanged());

  utility::HwTransceiverUtils::verifyDiagsCapability(
      *info.tcvrState(),
      transceiverManager_->getDiagsCapability(xcvrID),
      false /* skipCheckingIndividualCapability */);

  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETEST");

  auto diagsCap = transceiverManager_->getDiagsCapability(xcvrID);
  EXPECT_TRUE(diagsCap.has_value());
  std::vector<prbs::PrbsPolynomial> expectedSysPolynomials = {
      prbs::PrbsPolynomial::PRBS31Q,
      prbs::PrbsPolynomial::PRBS23Q,
      prbs::PrbsPolynomial::PRBS15Q,
      prbs::PrbsPolynomial::PRBS13Q,
      prbs::PrbsPolynomial::PRBS9Q,
      prbs::PrbsPolynomial::PRBS7Q};
  std::vector<prbs::PrbsPolynomial> expectedLinePolynomials = {
      prbs::PrbsPolynomial::PRBS31Q,
      prbs::PrbsPolynomial::PRBS23Q,
      prbs::PrbsPolynomial::PRBS15Q,
      prbs::PrbsPolynomial::PRBS13Q,
      prbs::PrbsPolynomial::PRBS9Q,
      prbs::PrbsPolynomial::PRBS7Q};

  auto linePrbsCapability = *(*diagsCap).prbsLineCapabilities();
  auto sysPrbsCapability = *(*diagsCap).prbsSystemCapabilities();

  tests.verifyPrbsPolynomials(expectedLinePolynomials, linePrbsCapability);
  tests.verifyPrbsPolynomials(expectedSysPolynomials, sysPrbsCapability);

  for (auto unsupportedApplication :
       {SMFMediaInterfaceCode::CWDM4_100G,
        SMFMediaInterfaceCode::FR4_400G,
        SMFMediaInterfaceCode::LR4_10_400G,
        SMFMediaInterfaceCode::FR4_200G}) {
    EXPECT_EQ(
        xcvr->getApplicationField(
            static_cast<uint8_t>(unsupportedApplication), 0),
        std::nullopt);
  }
  for (auto supportedApplication : {SMFMediaInterfaceCode::DR4_400G}) {
    auto applicationField = xcvr->getApplicationField(
        static_cast<uint8_t>(supportedApplication), 0);
    EXPECT_NE(applicationField, std::nullopt);
    EXPECT_EQ(applicationField->hostStartLanes, std::vector<int>{0});
    EXPECT_EQ(applicationField->mediaStartLanes, std::vector<int>{0});
    for (uint8_t lane = 1; lane < 7; lane++) {
      EXPECT_EQ(
          xcvr->getApplicationField(
              static_cast<uint8_t>(supportedApplication), lane),
          std::nullopt);
    }
  }
  EXPECT_TRUE(info.tcvrState()->errorStates()->empty());
}

TEST_F(CmisTest, cmis2x800GDr4TransceiverInfoTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis2x800GDr4Transceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_TRUE(info.tcvrState()->transceiverManagementInterface());
  EXPECT_EQ(
      info.tcvrState()->transceiverManagementInterface(),
      TransceiverManagementInterface::CMIS);
  EXPECT_EQ(xcvr->numHostLanes(), 8);
  EXPECT_EQ(xcvr->numMediaLanes(), 8);
  EXPECT_EQ(
      info.tcvrState()->moduleMediaInterface(), MediaInterfaceCode::DR4_2x800G);
  for (auto& media : *info.tcvrState()->settings()->mediaInterface()) {
    EXPECT_EQ(
        media.media()->smfCode().value(), SMFMediaInterfaceCode::DR4_800G);
    EXPECT_EQ(media.code(), MediaInterfaceCode::DR4_800G);
  }

  // Check cmisStateChanged
  EXPECT_TRUE(
      info.tcvrState()->status() &&
      info.tcvrState()->status()->cmisStateChanged() &&
      *info.tcvrState()->status()->cmisStateChanged());

  utility::HwTransceiverUtils::verifyDiagsCapability(
      *info.tcvrState(),
      transceiverManager_->getDiagsCapability(xcvrID),
      false /* skipCheckingIndividualCapability */);

  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETEST");

  auto diagsCap = transceiverManager_->getDiagsCapability(xcvrID);
  EXPECT_TRUE(diagsCap.has_value());
  std::vector<prbs::PrbsPolynomial> expectedSysPolynomials = {
      prbs::PrbsPolynomial::PRBS31Q,
      prbs::PrbsPolynomial::PRBS23Q,
      prbs::PrbsPolynomial::PRBS13Q,
  };
  std::vector<prbs::PrbsPolynomial> expectedLinePolynomials = {
      prbs::PrbsPolynomial::PRBS31Q,
      prbs::PrbsPolynomial::PRBS23Q,
      prbs::PrbsPolynomial::PRBS15Q,
      prbs::PrbsPolynomial::PRBS13Q,
  };

  auto linePrbsCapability = *(*diagsCap).prbsLineCapabilities();
  auto sysPrbsCapability = *(*diagsCap).prbsSystemCapabilities();

  tests.verifyPrbsPolynomials(expectedLinePolynomials, linePrbsCapability);
  tests.verifyPrbsPolynomials(expectedSysPolynomials, sysPrbsCapability);

  for (auto unsupportedApplication :
       {SMFMediaInterfaceCode::LR4_10_400G,
        SMFMediaInterfaceCode::FR4_400G,
        SMFMediaInterfaceCode::FR1_100G,
        SMFMediaInterfaceCode::FR4_200G,
        SMFMediaInterfaceCode::CWDM4_100G}) {
    EXPECT_EQ(
        xcvr->getApplicationField(
            static_cast<uint8_t>(unsupportedApplication), 0),
        std::nullopt);
  }

  for (auto supportedApplication : {SMFMediaInterfaceCode::DR4_800G}) {
    auto applicationField = xcvr->getApplicationField(
        static_cast<uint8_t>(supportedApplication), 0);
    EXPECT_NE(applicationField, std::nullopt);
    std::vector<int> expectedStartLanes = {0, 4};
    EXPECT_EQ(applicationField->hostStartLanes, expectedStartLanes);
    EXPECT_EQ(applicationField->mediaStartLanes, expectedStartLanes);
  }

  EXPECT_TRUE(diagsCap.value().vdm().value());
  EXPECT_TRUE(diagsCap.value().cdb().value());
  EXPECT_TRUE(diagsCap.value().prbsLine().value());
  EXPECT_TRUE(diagsCap.value().prbsSystem().value());
  EXPECT_TRUE(diagsCap.value().loopbackLine().value());
  EXPECT_TRUE(diagsCap.value().loopbackSystem().value());
  EXPECT_TRUE(diagsCap.value().txOutputControl().value());
  EXPECT_TRUE(diagsCap.value().rxOutputControl().value());
  EXPECT_TRUE(diagsCap.value().snrLine().value());
  EXPECT_TRUE(diagsCap.value().snrSystem().value());
  EXPECT_TRUE(xcvr->isVdmSupported());
  EXPECT_TRUE(xcvr->isPrbsSupported(phy::Side::LINE));
  EXPECT_TRUE(xcvr->isPrbsSupported(phy::Side::SYSTEM));
  EXPECT_TRUE(xcvr->isSnrSupported(phy::Side::LINE));
  EXPECT_TRUE(xcvr->isSnrSupported(phy::Side::SYSTEM));
  EXPECT_TRUE(info.tcvrState()->errorStates()->empty());
}

TEST_F(CmisTest, cmisInvalidDatapathTransceiverInfoTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<InvalidDatapathLaneStateTransceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_TRUE(info.tcvrState()->transceiverManagementInterface());
  std::set<TransceiverErrorState> expectedErrorStates = {
      TransceiverErrorState::INVALID_DATA_PATH_LANE_STATE};
  EXPECT_EQ(info.tcvrState()->errorStates(), expectedErrorStates);
}

namespace {
// Seed/read a byte straight in the fake EEPROM, selecting the page first the
// way the module itself does. Lets these tests set up and check a register
// without going through the private readCmisField/writeCmisField helpers.
void pokeEeprom(
    FakeTransceiverImpl* impl,
    uint8_t page,
    int byteOffset,
    uint8_t value) {
  TransceiverAccessParameter pageParam(
      TransceiverAccessParameter::ADDR_QSFP, 127, 1);
  impl->writeTransceiver(pageParam, &page, 0, 0);
  TransceiverAccessParameter param(
      TransceiverAccessParameter::ADDR_QSFP, byteOffset, 1);
  impl->writeTransceiver(param, &value, 0, 0);
}

uint8_t peekEeprom(FakeTransceiverImpl* impl, uint8_t page, int byteOffset) {
  TransceiverAccessParameter pageParam(
      TransceiverAccessParameter::ADDR_QSFP, 127, 1);
  impl->writeTransceiver(pageParam, &page, 0, 0);
  TransceiverAccessParameter param(
      TransceiverAccessParameter::ADDR_QSFP, byteOffset, 1);
  uint8_t value = 0;
  impl->readTransceiver(param, &value, 0);
  return value;
}

// VDM FreezeRequest (Page 2Fh byte 144) is a convenient one-byte register to
// exercise the read-modify-write helper against.
constexpr uint8_t kRmwPage = 0x2f;
constexpr int kRmwByte = 144;
} // namespace

TEST_F(CmisTest, readModifyWriteCmisFieldSetsMaskedBits) {
  auto xcvr = overrideCmisModule<Cmis800GZrTransceiver>(
      TransceiverID(1), TransceiverModuleIdentifier::OSFP);
  pokeEeprom(lastQsfpImpl(), kRmwPage, kRmwByte, 0x0f);

  EXPECT_EQ(
      xcvr->readModifyWriteCmisField(CmisField::VDM_LATCH_REQUEST, 0x80, 0x80),
      0x8f);
  EXPECT_EQ(peekEeprom(lastQsfpImpl(), kRmwPage, kRmwByte), 0x8f);
}

TEST_F(CmisTest, readModifyWriteCmisFieldClearsMaskedBits) {
  auto xcvr = overrideCmisModule<Cmis800GZrTransceiver>(
      TransceiverID(1), TransceiverModuleIdentifier::OSFP);
  pokeEeprom(lastQsfpImpl(), kRmwPage, kRmwByte, 0x8f);

  EXPECT_EQ(
      xcvr->readModifyWriteCmisField(CmisField::VDM_LATCH_REQUEST, 0x80, 0x00),
      0x0f);
  EXPECT_EQ(peekEeprom(lastQsfpImpl(), kRmwPage, kRmwByte), 0x0f);
}

// value is expected to be already positioned within the byte, so bits of it
// outside the mask must not leak into the register.
TEST_F(CmisTest, readModifyWriteCmisFieldIgnoresValueBitsOutsideMask) {
  auto xcvr = overrideCmisModule<Cmis800GZrTransceiver>(
      TransceiverID(1), TransceiverModuleIdentifier::OSFP);
  pokeEeprom(lastQsfpImpl(), kRmwPage, kRmwByte, 0x00);

  EXPECT_EQ(
      xcvr->readModifyWriteCmisField(CmisField::VDM_LATCH_REQUEST, 0x80, 0xff),
      0x80);
}

// The helper reads into a single stack byte, so a wider field would overrun it.
// It has to reject that rather than corrupt the stack.
TEST_F(CmisTest, readModifyWriteCmisFieldRejectsMultiByteField) {
  auto xcvr = overrideCmisModule<Cmis800GZrTransceiver>(
      TransceiverID(1), TransceiverModuleIdentifier::OSFP);

  // PART_NUMBER is 16 bytes wide.
  EXPECT_THROW(
      xcvr->readModifyWriteCmisField(CmisField::PART_NUMBER, 0xff, 0x00),
      FbossError);
}

namespace {
// Page 13h loopback enable registers, per the CMIS spec names.
constexpr uint8_t kLoopbackPage = 0x13;
constexpr int kMediaOutputLbByte = 180;
constexpr int kMediaInputLbByte = 181;
constexpr int kHostOutputLbByte = 182;
constexpr int kHostInputLbByte = 183;
constexpr std::array<int, 4> kLoopbackBytes = {
    kMediaOutputLbByte,
    kMediaInputLbByte,
    kHostOutputLbByte,
    kHostInputLbByte};

struct LoopbackCase {
  phy::Side side;
  phy::LoopbackMode mode;
  int expectedByte;
};

// Loopback is applied to a port's lanes, which are only known once the port
// has been programmed.
void programSinglePort(
    MockCmisModule* xcvr,
    const std::string& portName,
    cfg::PortSpeed speed) {
  ProgramTransceiverState programTcvrState;
  TransceiverPortState portState;
  portState.portName = portName;
  portState.startHostLane = 0;
  portState.speed = speed;
  portState.numHostLanes = 4;
  programTcvrState.ports.emplace(portState.portName, portState);
  xcvr->programTransceiver(programTcvrState, false);
}
} // namespace

TEST_F(CmisTest, setTransceiverLoopbackSelectsRegisterPerMode) {
  auto xcvrID = TransceiverID(0);
  auto xcvr = overrideCmisModule<Cmis200GTransceiver>(xcvrID);
  auto impl = lastQsfpImpl();
  auto portName = *transceiverManager_->getPortNames(xcvrID).begin();
  programSinglePort(xcvr, portName, cfg::PortSpeed::TWOHUNDREDG);

  for (const auto& tc : std::vector<LoopbackCase>{
           {phy::Side::SYSTEM, phy::LoopbackMode::INPUT, kHostInputLbByte},
           {phy::Side::SYSTEM, phy::LoopbackMode::OUTPUT, kHostOutputLbByte},
           {phy::Side::LINE, phy::LoopbackMode::INPUT, kMediaInputLbByte},
           {phy::Side::LINE, phy::LoopbackMode::OUTPUT, kMediaOutputLbByte},
       }) {
    SCOPED_TRACE(
        fmt::format(
            "{} {}",
            apache::thrift::util::enumNameSafe(tc.side),
            apache::thrift::util::enumNameSafe(tc.mode)));

    xcvr->setTransceiverLoopback(portName, tc.side, true, tc.mode);
    for (auto byte : kLoopbackBytes) {
      if (byte == tc.expectedByte) {
        EXPECT_NE(peekEeprom(impl, kLoopbackPage, byte), 0);
      } else {
        EXPECT_EQ(peekEeprom(impl, kLoopbackPage, byte), 0);
      }
    }

    xcvr->setTransceiverLoopback(portName, tc.side, false, tc.mode);
    for (auto byte : kLoopbackBytes) {
      EXPECT_EQ(peekEeprom(impl, kLoopbackPage, byte), 0);
    }
  }
}

// kCmis2x400GFr4Page13 advertises only the media and host input loopbacks.
TEST_F(CmisTest, setTransceiverLoopbackRejectsUnadvertisedMode) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis2x400GFr4Transceiver>(
      xcvrID, TransceiverModuleIdentifier::OSFP);
  auto impl = lastQsfpImpl();
  auto portName = *transceiverManager_->getPortNames(xcvrID).begin();
  programSinglePort(xcvr, portName, cfg::PortSpeed::FOURHUNDREDG);

  EXPECT_THROW(
      xcvr->setTransceiverLoopback(
          portName, phy::Side::SYSTEM, true, phy::LoopbackMode::OUTPUT),
      FbossError);
  EXPECT_THROW(
      xcvr->setTransceiverLoopback(
          portName, phy::Side::LINE, true, phy::LoopbackMode::OUTPUT),
      FbossError);
  for (auto byte : kLoopbackBytes) {
    EXPECT_EQ(peekEeprom(impl, kLoopbackPage, byte), 0);
  }

  // Disabling an unadvertised loopback is a no-op rather than an error.
  EXPECT_NO_THROW(xcvr->setTransceiverLoopback(
      portName, phy::Side::LINE, false, phy::LoopbackMode::OUTPUT));

  EXPECT_NO_THROW(xcvr->setTransceiverLoopback(
      portName, phy::Side::LINE, true, phy::LoopbackMode::INPUT));
  EXPECT_NE(peekEeprom(impl, kLoopbackPage, kMediaInputLbByte), 0);
}

// Neither fixture advertises simultaneous host and media side loopback.
TEST_F(CmisTest, setTransceiverLoopbackRejectsSimultaneousHostAndMedia) {
  auto xcvrID = TransceiverID(0);
  auto xcvr = overrideCmisModule<Cmis200GTransceiver>(xcvrID);
  auto impl = lastQsfpImpl();
  auto portName = *transceiverManager_->getPortNames(xcvrID).begin();
  programSinglePort(xcvr, portName, cfg::PortSpeed::TWOHUNDREDG);

  xcvr->setTransceiverLoopback(
      portName, phy::Side::LINE, true, phy::LoopbackMode::INPUT);
  EXPECT_THROW(
      xcvr->setTransceiverLoopback(
          portName, phy::Side::SYSTEM, true, phy::LoopbackMode::INPUT),
      FbossError);
  EXPECT_EQ(peekEeprom(impl, kLoopbackPage, kHostInputLbByte), 0);

  // Both loopbacks on the same side are allowed together.
  EXPECT_NO_THROW(xcvr->setTransceiverLoopback(
      portName, phy::Side::LINE, true, phy::LoopbackMode::OUTPUT));

  xcvr->setTransceiverLoopback(
      portName, phy::Side::LINE, false, phy::LoopbackMode::INPUT);
  xcvr->setTransceiverLoopback(
      portName, phy::Side::LINE, false, phy::LoopbackMode::OUTPUT);
  EXPECT_NO_THROW(xcvr->setTransceiverLoopback(
      portName, phy::Side::SYSTEM, true, phy::LoopbackMode::INPUT));
}

// Applications 9-15 are advertised in Page 01h. The ZR fixture advertises
// the vendor-custom application on all of them.
TEST_F(CmisTest, applicationCapabilitiesIncludePage01Applications) {
  auto xcvr = overrideCmisModule<Cmis800GZrTransceiver>(
      TransceiverID(1), TransceiverModuleIdentifier::OSFP);

  auto app = xcvr->getApplicationField(
      static_cast<uint8_t>(SMFMediaInterfaceCode::ZR_VENDOR_CUSTOM), 0);

  ASSERT_TRUE(app.has_value());
  EXPECT_EQ(app->ApSelCode, 9);
  EXPECT_EQ(app->hostLaneCount, 8);
  EXPECT_EQ(app->mediaLaneCount, 1);
  EXPECT_EQ(xcvr->getModuleCapabilities().size(), 15);
}

// A Page 01h entry that isn't terminated with 0xFF but has an undefined
// (0) media interface is not an application. The fixture's ApSel 9 entry is
// such an entry and the list ends at ApSel 10, so only the 8 lower page
// applications remain.
TEST_F(CmisTest, applicationCapabilitiesSkipUndefinedPage01Entries) {
  auto xcvr = overrideCmisModule<Cmis2x400GFr4Transceiver>(
      TransceiverID(1), TransceiverModuleIdentifier::OSFP);

  const auto& capabilities = xcvr->getModuleCapabilities();

  EXPECT_EQ(capabilities.size(), 8);
  for (const auto& capability : capabilities) {
    EXPECT_LE(capability.ApSelCode, 8);
  }
}

// Host datapaths multiplexed onto one media lane (3x200G ZR) all map to
// that media lane.
TEST_F(CmisTest, configuredMediaLanesSharedByMultipleHostDatapaths) {
  auto xcvr = overrideCmisModule<Cmis800GZr3x200GTransceiver>(
      TransceiverID(1), TransceiverModuleIdentifier::OSFP);
  writeUpperPage(
      lastQsfpImpl(), kPage11h, kActiveAppSelOffset, k3x200GActiveAppSel);
  xcvr->refresh();

  const std::vector<uint8_t> expectedMediaLanes = {0};
  for (uint8_t startHostLane : {0, 2, 4}) {
    EXPECT_EQ(xcvr->configuredMediaLanes(startHostLane), expectedMediaLanes);
  }
  const std::vector<uint8_t> expectedHostLanes = {2, 3};
  EXPECT_EQ(xcvr->configuredHostLanes(2), expectedHostLanes);
}

// A datapath reset only covers lanes in an active application. Lanes 6-7
// have none in 3x200G and never activate, so including them would never
// complete.
TEST_F(CmisTest, resetDataPathSkipsLanesWithoutApplication) {
  auto xcvr = overrideCmisModule<Cmis800GZr3x200GTransceiver>(
      TransceiverID(1), TransceiverModuleIdentifier::OSFP);
  writeUpperPage(
      lastQsfpImpl(), kPage11h, kActiveAppSelOffset, k3x200GActiveAppSel);
  xcvr->refresh();

  EXPECT_NO_THROW(xcvr->resetDataPath("eth1/1/1"));
}

// Programming all ports deinitializes every lane in the current and new
// configs and initializes every lane the new config puts in an application.
TEST_F(CmisTest, allPortsResetLaneMasks) {
  TransceiverPropertiesManager::initDefault();
  auto xcvr = overrideCmisModule<Cmis2x400GFr4Transceiver>(
      TransceiverID(1), TransceiverModuleIdentifier::OSFP);
  TransceiverPortState state;
  state.portName = "eth1/1/5";
  state.startHostLane = 4;
  state.speed = cfg::PortSpeed::HUNDREDG;
  state.numHostLanes = 4;
  ASSERT_FALSE(xcvr->isRequestValidMultiportSpeedConfig(
      state.speed, state.startHostLane, state.numHostLanes));

  // 100G-CWDM4 on the second port moves both ports (all 8 lanes) to CWDM4
  EXPECT_EQ(
      xcvr->getAllPortsResetLaneMasks(state, state.numHostLanes),
      std::make_pair(uint8_t(0xff), uint8_t(0xff)));

  // With no valid speed combo (no 25G application) only the port's own lane is
  // reset
  state.startHostLane = 1;
  state.speed = cfg::PortSpeed::TWENTYFIVEG;
  state.numHostLanes = 1;
  EXPECT_EQ(
      xcvr->getAllPortsResetLaneMasks(state, state.numHostLanes),
      std::make_pair(uint8_t(0x02), uint8_t(0x02)));
}

// Moving a ZR from 800G to 3x200G programs all three datapaths in one
// stage set 0 commit and one datapath deinit/init, leaving lanes 6-7 in deinit.
TEST_F(CmisTest, cmis800GZr3x200GProgramsAllDatapathsTogether) {
  TransceiverPropertiesManager::initDefault();
  auto xcvr = overrideCmisModule<Cmis800GZr3x200GTransceiver>(
      TransceiverID(1), TransceiverModuleIdentifier::OSFP);
  auto programTcvrState = make3x200GZrProgramState();

  xcvr->programTransceiver(programTcvrState, false);

  EXPECT_EQ(
      readUpperPage(lastQsfpImpl(), kPage11h, kActiveAppSelOffset, 8),
      k3x200GActiveAppSel);
  const std::vector<uint8_t> lanes6And7InDeinit = {0xc0};
  EXPECT_EQ(
      readUpperPage(lastQsfpImpl(), kPage10h, kDataPathDeinitOffset, 1),
      lanes6And7InDeinit);
  EXPECT_EQ(
      lastQsfpImpl()->getUpperPageWriteCount(
          kPage10h, kDataPathDeinitOffset - 128),
      2);
}

// A datapath reset requested in the same programTransceiver call skips the
// lanes the AppSel change just initialized.
TEST_F(CmisTest, programTransceiverSkipsResetOfJustInitializedLanes) {
  TransceiverPropertiesManager::initDefault();
  auto xcvr = overrideCmisModule<Cmis800GZr3x200GTransceiver>(
      TransceiverID(1), TransceiverModuleIdentifier::OSFP);
  auto programTcvrState = make3x200GZrProgramState();

  xcvr->programTransceiver(programTcvrState, true /* needResetDataPath */);

  // Only the AppSel change's deinit and init; no second cycle for the reset
  EXPECT_EQ(
      lastQsfpImpl()->getUpperPageWriteCount(
          kPage10h, kDataPathDeinitOffset - 128),
      2);
}
} // namespace facebook::fboss
