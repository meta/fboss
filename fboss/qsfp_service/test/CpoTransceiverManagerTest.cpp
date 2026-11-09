// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <folly/FileUtil.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/qsfp_service/module/cmis/CmisModule.h"
#include "fboss/qsfp_service/module/tests/FakeTransceiverImpl.h"
#include "fboss/qsfp_service/test/TransceiverManagerTestHelper.h"

namespace facebook::fboss {

namespace {

// Minimal CmisModule mock that stubs only the CDB readiness/state-changed reads
// the fake transceiver can't service. The slow hardware programming steps
// (application select / serdes config / datapath reset, which sleep and poll
// the module for seconds per lane) are stubbed out so the test exercises the
// manager's global-lane derivation and per-port lane recording, not the real
// CDB datapath bring-up (covered by the CMIS module-level tests).
class CpoMockCmisModule : public CmisModule {
 public:
  template <typename XcvrImplT>
  CpoMockCmisModule(
      std::set<std::string> portNames,
      XcvrImplT* qsfpImpl,
      std::shared_ptr<const TransceiverConfig> cfgPtr,
      std::string tcvrName)
      : CmisModule(
            std::move(portNames),
            qsfpImpl,
            std::move(cfgPtr),
            true /*supportRemediate*/,
            std::move(tcvrName)) {
    ON_CALL(*this, getModuleStateChanged).WillByDefault([this]() {
      return (++moduleStateChangedReadTimes_) == 1;
    });
    ON_CALL(*this, ensureTransceiverReadyLocked(testing::_))
        .WillByDefault(testing::Return(true));
  }

  MOCK_METHOD0(getModuleStateChanged, bool());
  MOCK_METHOD1(ensureTransceiverReadyLocked, bool(bool));
  MOCK_METHOD1(configureModule, void(uint8_t));
  MOCK_METHOD1(customizeTransceiverLocked, void(const TransceiverPortState&));
  MOCK_METHOD1(resetDataPath, void(const std::string&));
  MOCK_METHOD1(
      ensureRxOutputSquelchEnabled,
      void(const std::vector<HostLaneSettings>&));

 private:
  // Wider than uint8_t so the "true only on the first call" default action
  // can't wrap around to 0 and spuriously fire again after 256 calls.
  size_t moduleStateChangedReadTimes_{0};
};
// The example integrated-optics (CPO) platform: each module is a single
// transceiver with 4 banks x 8 = 32 global host lanes. Ports keep the per-bank
// front-panel name eth1/<bank+1>/<lane-in-bank>; the platform mapping resolves
// them to global transceiver lanes (bank N -> lanes 8N..8N+7).
constexpr auto kExampleCpoMappingPath =
    "fboss/configs/platforms/generic/example_integrated_optics/"
    "platform_mapping/generated/platform_mapping.json";

constexpr int kNumCpoModules = 2;
constexpr int kLanesPerModule = 32;
} // namespace

// End-to-end test: build a TransceiverManager backed by the real
// example_integrated_optics platform mapping, bring up its CPO transceivers,
// and program each module's banks in different breakout speeds. Confirms the
// manager derives each port's global transceiver host lanes from the mapping
// (single transceiver per module, global lanes 0-31, no per-bank fold).
class CpoTransceiverManagerTest : public TransceiverManagerTestHelper {
 protected:
  void SetUp() override {
    // The fake doesn't service CDB; fail CDB commands fast instead of waiting
    // out the real timeout.
    gflags::SetCommandLineOptionWithMode(
        "cdb_command_timeout_usec", "1", gflags::SET_FLAGS_DEFAULT);
    // Reuse the helper's gflag + fake-config setup, then replace the default
    // synthetic-mapping manager with one backed by the CPO mapping JSON.
    TransceiverManagerTestHelper::SetUp();

    std::string mappingJson;
    ASSERT_TRUE(folly::readFile(kExampleCpoMappingPath, mappingJson))
        << "Failed to read CPO platform mapping: " << kExampleCpoMappingPath;
    auto platformMapping = std::make_shared<PlatformMapping>(mappingJson);

    transceiverManager_ =
        std::make_unique<MockWedgeManager>(platformMapping, kNumCpoModules);
    transceiverManager_->init();
    tcvrConfig_ = transceiverManager_->getTransceiverConfig();
  }

  // Install a ready CPO fake transceiver for the given module and drive it to
  // the discovered state.
  CmisModule* makeCpoTransceiver(TransceiverID id) {
    qsfpImpls_.push_back(
        std::make_unique<CmisCpo6P4TDrReadyTransceiver>(
            id, transceiverManager_.get()));
    // The override I2C bus uses module ids starting from 1.
    transceiverManager_->overrideMgmtInterface(
        static_cast<int>(id) + 1, uint8_t(TransceiverModuleIdentifier::CPO));
    auto* xcvr = static_cast<CmisModule*>(
        transceiverManager_->overrideTransceiverForTesting(
            id,
            std::make_unique<::testing::NiceMock<CpoMockCmisModule>>(
                transceiverManager_->getPortNames(id),
                qsfpImpls_.back().get(),
                tcvrConfig_,
                transceiverManager_->getTransceiverName(id))));
    transceiverManager_->refreshStateMachines();
    return xcvr;
  }
};

TEST_F(CpoTransceiverManagerTest, programBanksInDifferentProfiles) {
  auto* xcvr = makeCpoTransceiver(TransceiverID(0));
  // 32 host lanes == 4 banks x 8 lanes, i.e. the module came up as a single CPO
  // transceiver spanning all banks.
  ASSERT_EQ(xcvr->numHostLanes(), kLanesPerModule);

  // Program each of module 0's four banks in a different speed/breakout. The
  // example mapping groups each bank into two 4-lane controlling ports
  // (eth1/<bank>/1 and eth1/<bank>/5), so each bank is driven as two ports. The
  // manager derives the global transceiver lanes for each (port, profile) from
  // the mapping. PortIDs: eth1/1/1=1, eth1/1/5=5, eth1/2/1=9, eth1/2/5=13,
  // eth1/3/1=17, eth1/3/5=21, eth1/4/1=25, eth1/4/5=29.
  using cfg::PortProfileID;
  TransceiverManager::OverrideTcvrToPortAndProfile
      overrideTcvrToPortAndProfile = {
          {TransceiverID(0),
           {
               // bank 0 -> 2x800G-DR4 (global lanes 0-7)
               {PortID(1), PortProfileID::PROFILE_800G_4_PAM4_RS544X2N_OPTICAL},
               {PortID(5), PortProfileID::PROFILE_800G_4_PAM4_RS544X2N_OPTICAL},
               // bank 1 -> 2x400G-DR4 (global lanes 8-15)
               {PortID(9), PortProfileID::PROFILE_400G_4_PAM4_RS544X2N_OPTICAL},
               {PortID(13),
                PortProfileID::PROFILE_400G_4_PAM4_RS544X2N_OPTICAL},
               // bank 2 -> 2x200G (global lanes 16-23)
               {PortID(17),
                PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_OPTICAL},
               {PortID(21),
                PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_OPTICAL},
               // bank 3 -> 2x100G (global lanes 24-31)
               {PortID(25), PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL},
               {PortID(29), PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL},
           }}};
  transceiverManager_->setOverrideTcvrToPortAndProfileForTesting(
      overrideTcvrToPortAndProfile);

  // Refresh to program the iphy ports, which records the port->profile state
  // that the transceiver programming step consumes.
  transceiverManager_->refreshStateMachines();
  // Drive the manager's transceiver programming directly: this is the code path
  // under test -- it derives each port's transceiver host lanes from the CPO
  // mapping (global lanes, no per-bank fold) and programs the module.
  transceiverManager_->programTransceiver(
      TransceiverID(0), false /* needResetDataPath */);

  // Re-fetch the module from the manager in case discovery re-created it.
  xcvr = static_cast<CmisModule*>(
      transceiverManager_->getTransceiver(TransceiverID(0)));

  // The manager derives each port's transceiver host lanes from the CPO mapping
  // and programs them in the module's unified global lane space (bank N
  // occupies global lanes 8N..8N+7). Confirm every programmed port landed on
  // its expected global lanes -- the end-to-end proof of the
  // single-transceiver/global-lane convention through TransceiverManager.
  const std::unordered_map<std::string, std::set<uint8_t>>
      kExpectedPortToHostLanes = {
          // bank 0: 2x800G-DR4 (4 host lanes each)
          {"eth1/1/1", {0, 1, 2, 3}},
          {"eth1/1/5", {4, 5, 6, 7}},
          // bank 1: 2x400G-DR4 (4 host lanes each)
          {"eth1/2/1", {8, 9, 10, 11}},
          {"eth1/2/5", {12, 13, 14, 15}},
          // bank 2: 2x200G (4 host lanes each)
          {"eth1/3/1", {16, 17, 18, 19}},
          {"eth1/3/5", {20, 21, 22, 23}},
          // bank 3: 2x100G (4 host lanes each)
          {"eth1/4/1", {24, 25, 26, 27}},
          {"eth1/4/5", {28, 29, 30, 31}},
      };
  EXPECT_EQ(xcvr->getPortNameToHostLanes(), kExpectedPortToHostLanes);

  // Datapath status: exactly the eight controlling ports we programmed across
  // the four banks are recorded -- compare the actual PortID set, not just the
  // count, so a wrong port set is caught.
  const auto programmedPorts =
      transceiverManager_->getProgrammedIphyPortToPortInfo(TransceiverID(0));
  std::set<PortID> programmedPortIds;
  for (const auto& [portId, portInfo] : programmedPorts) {
    programmedPortIds.insert(portId);
  }
  const std::set<PortID> kExpectedProgrammedPortIds = {
      PortID(1),
      PortID(5),
      PortID(9),
      PortID(13),
      PortID(17),
      PortID(21),
      PortID(25),
      PortID(29)};
  EXPECT_EQ(programmedPortIds, kExpectedProgrammedPortIds);
}

} // namespace facebook::fboss
