// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <folly/FileUtil.h>
#include <folly/testing/TestUtil.h>

#include "fboss/platform/platform_manager/PlatformManagerHandler.h"

using namespace ::testing;
using namespace facebook::fboss::platform::platform_manager;

namespace facebook::fboss::platform::platform_manager {

namespace {
std::vector<uint8_t> createTestEepromV5Data() {
  // clang-format off
  return {
      0xFB, 0xFB, 0x05, 0xFF,  // Header: Magic, Version 5, Reserved
      0x01, 0x0D, 'T', 'E', 'S', 'T', '_', 'P', 'R', 'O', 'D', 'U', 'C', 'T', 0x00,  // Product name
      0x02, 0x08, '1', '2', '3', '4', '5', '6', '7', '8',  // Product serial
      0x03, 0x08, 'S', 'Y', 'S', 'A', '1', '2', '3', '4',  // System assembly part number
      0x04, 0x0C, 'P', 'C', 'B', 'A', '0', '0', '0', '0', '0', '0', '0', '1',  // PCBA part number
      0x05, 0x08, 'P', 'C', 'B', '1', '2', '3', '4', '5',  // PCB part number
      0x06, 0x02, 0x00, 0x01,  // ODM PCBA part number
      0x07, 0x0B, 'M', 'F', 'G', '_', '2', '0', '2', '4', '0', '1', '1',  // Manufacturing date
      0x08, 0x02, 0x00, 0x02,  // Manufacturing location
      0x09, 0x02, 0x00, 0x03,  // FB PCBA part number
      0x0A, 0x02, 0x00, 0x04,  // FB PCB part number
      0x0B, 0x0C, 'S', 'E', 'R', 'I', 'A', 'L', '1', '2', '3', '4', '5', '6',  // Extended MAC base
      0x0C, 0x0C, 'A', 'S', 'S', 'E', 'T', '1', '2', '3', '4', '5', '6', '7',  // Extended MAC address size
      0x0D, 0x08, 'L', 'O', 'C', 'A', 'L', 'M', 'A', 'C',  // Location on fabric
      0x0E, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,  // MAC address field 1
      0x0F, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,  // MAC address field 2
      0x10, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,  // MAC address field 3
      0x11, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04,  // MAC address field 4
      0x12, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,  // MAC address field 5
      0x13, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06,  // MAC address field 6
      0x14, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07,  // MAC address field 7
      0xFA, 0x02, 0x00, 0x00,  // CRC field
      0xFF  // End marker
  };
  // clang-format on
}

std::vector<uint8_t> createTestEepromV6Data() {
  // clang-format off
  return {
      0xFB, 0xFB, 0x06, 0xFF,  // Header: Magic, Version 6, Reserved
      0x01, 0x0D, 'F', 'I', 'R', 'S', 'T', '_', 'S', 'Q', 'U', 'E', 'E', 'Z', 'E',  // Product name
      0x02, 0x08, '2', '0', '1', '2', '3', '4', '5', '6',  // Product part number
      0x03, 0x08, 'S', 'Y', 'S', 'A', '1', '2', '3', '4',  // System assembly part number
      0x04, 0x0C, 'P', 'C', 'B', 'A', '1', '2', '3', '4', '5', '6', '7', ' ',  // Meta PCBA part number
      0x05, 0x0C, 'P', 'C', 'B', '1', '2', '3', '4', '5', '6', '7', '8', ' ',  // Meta PCB part number
      0x06, 0x0C, 'M', 'Y', 'O', 'D', 'M', '1', '2', '3', '4', '5', '6', '7',  // ODM/JDM PCBA part number
      0x07, 0x0D, 'O', 'S', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B',  // ODM/JDM PCBA serial
      0x08, 0x01, 0x01,  // Production state
      0x09, 0x01, 0x00,  // Production sub-state
      0x0A, 0x01, 0x01,  // Re-Spin/Variant indicator
      0x0B, 0x0D, 'P', 'S', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 'A',  // Product serial
      0x0C, 0x07, 'U', 'N', 'A', '_', 'M', 'A', 'S',  // System manufacturer
      0x0D, 0x08, '2', '0', '1', '3', '0', '2', '0', '3',  // System manufacturing date
      0x0E, 0x05, 'T', 'E', 'R', 'Z', 'O',  // PCB manufacturer
      0x0F, 0x09, 'J', 'U', 'I', 'C', 'E', 'T', 'O', 'R', 'Y',  // Assembled at
      0x10, 0x07, 'B', 'U', 'D', 'O', 'K', 'A', 'N',  // EEPROM location on fabric
      0x11, 0x08, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x01, 0x02,  // X86 CPU MAC
      0x12, 0x08, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0x03, 0x04,  // BMC MAC
      0x13, 0x08, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x02, 0x00,  // Switch ASIC MAC
      0x14, 0x08, 0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x00, 0x02,  // META Reserved MAC
      0x15, 0x01, 0x01,  // RMA (V6 only)
      0x65, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,  // Vendor Defined Field 1
      0x66, 0x05, 'H', 'e', 'l', 'l', 'o',  // Vendor Defined Field 2
      0xFA, 0x02, 0x4A, 0x05,  // CRC16
      0xFF  // End marker
  };
  // clang-format on
}

} // namespace

class MockPlatformExplorer : public PlatformExplorer {
 public:
  explicit MockPlatformExplorer(
      const PlatformConfig& config,
      DataStore& dataStore)
      : PlatformExplorer(
            config,
            dataStore,
            std::make_shared<PlatformFsUtils>()) {}

  void setPMStatus(const PlatformManagerStatus& status) {
    updatePmStatus(status);
  }

  void addExplorationError(
      ExplorationErrorType errorType,
      const std::string& devicePath) {
    explorationSummary_.addError(errorType, devicePath, "test error");
  }
};

class PlatformManagerHandlerTest : public Test {
 protected:
  void SetUp() override {
    setupDefaultConfig();
    initializeComponents();
  }

  void setupDefaultConfig() {
    config_.platformName() = "TestPlatform";
    config_.bspKmodsRpmName() = "test-bsp-rpm";
    config_.bspKmodsRpmVersion() = "1.0.0";
    config_.chassisEepromDevicePath() = "/chassis_eeprom";
  }

  void initializeComponents() {
    dataStore_ = std::make_unique<DataStore>(config_);
    explorer_ = std::make_unique<MockPlatformExplorer>(config_, *dataStore_);
    handler_ = std::make_unique<PlatformManagerHandler>(
        *explorer_, *dataStore_, config_);
  }

  void setExplorationStatus(ExplorationStatus status) {
    PlatformManagerStatus pmStatus;
    pmStatus.explorationStatus() = status;
    explorer_->setPMStatus(pmStatus);
  }

  PmUnitInfo addPmUnit(const std::string& slotPath) {
    PmUnitVersion version;
    version.productionState() = 2;
    version.productionSubState() = 13;
    version.respinVariantIndicator() = 1;
    dataStore_->updatePmUnitName(slotPath, "TEST_PM_UNIT");
    dataStore_->updatePmUnitVersion(slotPath, version);

    PmUnitInfo expected;
    expected.name() = "TEST_PM_UNIT";
    expected.version() = version;
    return expected;
  }

  PresenceInfo addAbsentSlot(const std::string& slotPath) {
    PresenceInfo absent;
    absent.isPresent() = false;
    dataStore_->updatePmUnitPresenceInfo(slotPath, absent);
    return absent;
  }

  PmUnitInfo getPmUnitInfo(const std::string& slotPath) {
    auto request = std::make_unique<PmUnitInfoRequest>();
    request->slotPath() = slotPath;
    PmUnitInfoResponse response;
    handler_->getPmUnitInfo(response, std::move(request));
    return *response.pmUnitInfo();
  }

  void expectGetPmUnitInfoThrows(
      const std::string& slotPath,
      PlatformManagerErrorCode errorCode) {
    try {
      getPmUnitInfo(slotPath);
      FAIL() << "Expected PlatformManagerError to be thrown";
    } catch (const PlatformManagerError& e) {
      EXPECT_EQ(*e.errorCode(), errorCode);
    }
  }

  PlatformConfig config_;
  std::unique_ptr<DataStore> dataStore_;
  std::unique_ptr<MockPlatformExplorer> explorer_;
  std::unique_ptr<PlatformManagerHandler> handler_;
};

TEST_F(
    PlatformManagerHandlerTest,
    GetPmUnitInfo_ThrowsWhenExplorationInProgress) {
  addPmUnit("/TEST_SLOT@0");
  setExplorationStatus(ExplorationStatus::IN_PROGRESS);

  expectGetPmUnitInfoThrows(
      "/TEST_SLOT@0", PlatformManagerErrorCode::EXPLORATION_NOT_SUCCEEDED);
}

class GetPmUnitInfoServedStatusTest
    : public PlatformManagerHandlerTest,
      public WithParamInterface<ExplorationStatus> {};

TEST_P(GetPmUnitInfoServedStatusTest, ReturnsStoredPmUnitInfo) {
  const auto expected = addPmUnit("/TEST_SLOT@0");
  setExplorationStatus(GetParam());

  EXPECT_EQ(getPmUnitInfo("/TEST_SLOT@0"), expected);
}

INSTANTIATE_TEST_SUITE_P(
    ServedStatuses,
    GetPmUnitInfoServedStatusTest,
    Values(
        ExplorationStatus::SUCCEEDED,
        ExplorationStatus::SUCCEEDED_WITH_EXPECTED_ERRORS,
        ExplorationStatus::FAILED));

class GetPmUnitInfoNotFoundStatusTest
    : public PlatformManagerHandlerTest,
      public WithParamInterface<ExplorationStatus> {};

TEST_P(GetPmUnitInfoNotFoundStatusTest, ThrowsOnUnknownSlotPath) {
  setExplorationStatus(GetParam());

  expectGetPmUnitInfoThrows(
      "/NONEXISTENT_SLOT@0", PlatformManagerErrorCode::PM_UNIT_NOT_FOUND);
}

INSTANTIATE_TEST_SUITE_P(
    NotFoundStatuses,
    GetPmUnitInfoNotFoundStatusTest,
    Values(
        ExplorationStatus::UNSTARTED,
        ExplorationStatus::SUCCEEDED,
        ExplorationStatus::SUCCEEDED_WITH_EXPECTED_ERRORS,
        ExplorationStatus::FAILED));

TEST_F(
    PlatformManagerHandlerTest,
    GetPmUnitInfo_ReturnsAbsenceWhenExplorationFailed) {
  const auto absent = addAbsentSlot("/PSU_SLOT@0");
  explorer_->addExplorationError(
      ExplorationErrorType::IDPROM_READ, "/SCM_SLOT@0/[IDPROM]");
  setExplorationStatus(ExplorationStatus::FAILED);

  EXPECT_EQ(*getPmUnitInfo("/PSU_SLOT@0").presenceInfo(), absent);
}

TEST_F(
    PlatformManagerHandlerTest,
    GetPmUnitInfo_ReturnsAbsenceWithOtherErrorOnSlotWhenExplorationFailed) {
  const auto absent = addAbsentSlot("/FAN_SLOT@0");
  explorer_->addExplorationError(
      ExplorationErrorType::SLOT_PM_UNIT_ABSENCE, "/FAN_SLOT@0/[<ABSENT>]");
  setExplorationStatus(ExplorationStatus::FAILED);

  EXPECT_EQ(*getPmUnitInfo("/FAN_SLOT@0").presenceInfo(), absent);
}

TEST_F(
    PlatformManagerHandlerTest,
    GetPmUnitInfo_ThrowsOnPresenceCheckErrorWhenExplorationFailed) {
  addAbsentSlot("/PSU_SLOT@0");
  explorer_->addExplorationError(
      ExplorationErrorType::SLOT_PRESENCE_CHECK, "/PSU_SLOT@0/[<ABSENT>]");
  setExplorationStatus(ExplorationStatus::FAILED);

  expectGetPmUnitInfoThrows(
      "/PSU_SLOT@0", PlatformManagerErrorCode::EXPLORATION_NOT_SUCCEEDED);
}

TEST_F(
    PlatformManagerHandlerTest,
    GetPmUnitInfo_ReturnsOtherSlotOnPresenceCheckErrorWhenExplorationFailed) {
  const auto absent = addAbsentSlot("/PSU_SLOT@1");
  addAbsentSlot("/PSU_SLOT@0");
  explorer_->addExplorationError(
      ExplorationErrorType::SLOT_PRESENCE_CHECK, "/PSU_SLOT@0/[<ABSENT>]");
  setExplorationStatus(ExplorationStatus::FAILED);

  EXPECT_EQ(*getPmUnitInfo("/PSU_SLOT@1").presenceInfo(), absent);
}

TEST_F(PlatformManagerHandlerTest, GetAllPmUnits_ReturnsPopulatedMapWithUnits) {
  PmUnitVersion version1;
  version1.productionState() = 1;
  version1.productionSubState() = 10;
  version1.respinVariantIndicator() = 0;

  PmUnitVersion version2;
  version2.productionState() = 2;
  version2.productionSubState() = 20;
  version2.respinVariantIndicator() = 5;

  DataStore dataStore(config_);
  dataStore.updatePmUnitName("/SLOT_A@0", "PM_UNIT_A");
  dataStore.updatePmUnitVersion("/SLOT_A@0", version1);
  dataStore.updatePmUnitName("/SLOT_B@1", "PM_UNIT_B");
  dataStore.updatePmUnitVersion("/SLOT_B@1", version2);

  MockPlatformExplorer explorer(config_, dataStore);
  PlatformManagerHandler handler(explorer, dataStore, config_);

  PmUnitsResponse response;
  handler.getAllPmUnits(response);

  EXPECT_EQ(response.pmUnits()->size(), 2);

  auto& unitA = response.pmUnits()->at("/SLOT_A@0");
  EXPECT_EQ(*unitA.name(), "PM_UNIT_A");
  EXPECT_EQ(*unitA.version()->productionState(), 1);
  EXPECT_EQ(*unitA.version()->productionSubState(), 10);

  auto& unitB = response.pmUnits()->at("/SLOT_B@1");
  EXPECT_EQ(*unitB.name(), "PM_UNIT_B");
  EXPECT_EQ(*unitB.version()->productionState(), 2);
  EXPECT_EQ(*unitB.version()->productionSubState(), 20);
}

TEST_F(
    PlatformManagerHandlerTest,
    GetEepromContents_ThrowsWhenChassisEepromNotFound) {
  auto request = std::make_unique<PmUnitInfoRequest>();
  request->slotPath() = "";

  EepromContentResponse response;
  try {
    handler_->getEepromContents(response, std::move(request));
    FAIL() << "Expected PlatformManagerError to be thrown";
  } catch (const PlatformManagerError& e) {
    EXPECT_EQ(
        *e.errorCode(), PlatformManagerErrorCode::EEPROM_CONTENTS_NOT_FOUND);
  }
}

TEST_F(
    PlatformManagerHandlerTest,
    GetEepromContents_ThrowsOnNonexistentSlotPath) {
  auto request = std::make_unique<PmUnitInfoRequest>();
  request->slotPath() = "/NONEXISTENT_SLOT";

  EepromContentResponse response;
  try {
    handler_->getEepromContents(response, std::move(request));
    FAIL() << "Expected PlatformManagerError to be thrown";
  } catch (const PlatformManagerError& e) {
    EXPECT_EQ(
        *e.errorCode(), PlatformManagerErrorCode::EEPROM_CONTENTS_NOT_FOUND);
  }
}

TEST_F(
    PlatformManagerHandlerTest,
    GetEepromContents_ReturnsValidEepromV5Contents) {
  auto tmpDir = folly::test::TemporaryDirectory();
  std::string eepromPath = tmpDir.path().string() + "/eeprom";

  std::vector<uint8_t> eepromData = createTestEepromV5Data();
  folly::writeFile(eepromData, eepromPath.c_str());

  DataStore dataStore(config_);
  FbossEepromInterface eepromInterface(eepromPath, 0);
  dataStore.updateEepromContents("/test_slot", eepromInterface);

  MockPlatformExplorer explorer(config_, dataStore);
  PlatformManagerHandler handler(explorer, dataStore, config_);

  auto request = std::make_unique<PmUnitInfoRequest>();
  request->slotPath() = "/test_slot";

  EepromContentResponse response;
  handler.getEepromContents(response, std::move(request));

  EXPECT_EQ(*response.eepromContents()->version(), 5);
  EXPECT_EQ(*response.eepromContents()->productName(), "TEST_PRODUCT");
}

TEST_F(
    PlatformManagerHandlerTest,
    GetEepromContents_ReturnsValidEepromV6Contents) {
  auto tmpDir = folly::test::TemporaryDirectory();
  std::string eepromPath = tmpDir.path().string() + "/eeprom";

  std::vector<uint8_t> eepromData = createTestEepromV6Data();
  folly::writeFile(eepromData, eepromPath.c_str());

  DataStore dataStore(config_);
  FbossEepromInterface eepromInterface(eepromPath, 0);
  dataStore.updateEepromContents("/test_slot_v6", eepromInterface);

  MockPlatformExplorer explorer(config_, dataStore);
  PlatformManagerHandler handler(explorer, dataStore, config_);

  auto request = std::make_unique<PmUnitInfoRequest>();
  request->slotPath() = "/test_slot_v6";

  EepromContentResponse response;
  handler.getEepromContents(response, std::move(request));

  EXPECT_EQ(*response.eepromContents()->version(), 6);
  EXPECT_EQ(*response.eepromContents()->productName(), "FIRST_SQUEEZE");
  EXPECT_EQ(*response.eepromContents()->productPartNumber(), "20123456");
  EXPECT_EQ(*response.eepromContents()->rma(), "1");
  EXPECT_EQ(*response.eepromContents()->vendorDefinedField1(), "0x0101010101");
  EXPECT_EQ(*response.eepromContents()->vendorDefinedField2(), "0x48656c6c6f");
}

TEST_F(
    PlatformManagerHandlerTest,
    GetFirmwareVersions_ReturnsEmptyMapWhenNone) {
  FirmwareVersionsResponse response;
  handler_->getFirmwareVersions(response);
  EXPECT_TRUE(response.firmwareVersions()->empty());
}

TEST_F(PlatformManagerHandlerTest, GetFirmwareVersions_ReturnsPopulatedMap) {
  DataStore dataStore(config_);
  dataStore.updateFirmwareVersion("SMB_CPLD", "1.2.3");
  dataStore.updateFirmwareVersion("MCB_FPGA", "4.5.6");

  MockPlatformExplorer explorer(config_, dataStore);
  PlatformManagerHandler handler(explorer, dataStore, config_);

  FirmwareVersionsResponse response;
  handler.getFirmwareVersions(response);

  EXPECT_EQ(response.firmwareVersions()->size(), 2);
  EXPECT_EQ(response.firmwareVersions()->at("SMB_CPLD"), "1.2.3");
  EXPECT_EQ(response.firmwareVersions()->at("MCB_FPGA"), "4.5.6");
}

TEST_F(PlatformManagerHandlerTest, GetPlatformSnapshot_ReturnsPopulatedTree) {
  // Setup config with root PmUnit (SCM) that has I2C devices and a child slot
  PlatformConfig snapshotConfig;
  snapshotConfig.platformName() = "TEST_PLATFORM";
  snapshotConfig.rootSlotType() = "SCM_SLOT";
  snapshotConfig.rootPmUnitName() = "SCM";
  snapshotConfig.i2cAdaptersFromCpu() = {"SMBus I801 adapter at 2000"};
  snapshotConfig.bspKmodsRpmName() = "test_bsp_kmods";
  snapshotConfig.bspKmodsRpmVersion() = "1.0.0-1";

  SlotTypeConfig scmSlotType;
  scmSlotType.numOutgoingI2cBuses() = 0;
  scmSlotType.pmUnitName() = "SCM";

  SlotTypeConfig smbSlotType;
  smbSlotType.numOutgoingI2cBuses() = 1;
  smbSlotType.pmUnitName() = "SMB";

  snapshotConfig.slotTypeConfigs() = {
      {"SCM_SLOT", scmSlotType},
      {"SMB_SLOT", smbSlotType},
  };

  // Root PmUnit with an I2C sensor (references CPU bus by adapter name)
  // and an outgoing slot whose bus comes from an I2C mux on the root PmUnit
  PmUnitConfig scmConfig;
  scmConfig.pluggedInSlotType() = "SCM_SLOT";

  I2cDeviceConfig sensorConfig;
  sensorConfig.busName() = "SMBus I801 adapter at 2000";
  sensorConfig.address() = "0x48";
  sensorConfig.kernelDeviceName() = "lm75";
  sensorConfig.pmUnitScopedName() = "INLET_SENSOR";
  scmConfig.i2cDeviceConfigs() = {sensorConfig};

  SlotConfig smbSlotConfig;
  smbSlotConfig.slotType() = "SMB_SLOT";
  smbSlotConfig.outgoingI2cBusNames() = {"SCM_MUX@0"};
  scmConfig.outgoingSlotConfigs() = {{"SMB_SLOT@0", smbSlotConfig}};

  // Child PmUnit with a mux
  PmUnitConfig smbConfig;
  smbConfig.pluggedInSlotType() = "SMB_SLOT";

  I2cDeviceConfig muxConfig;
  muxConfig.busName() = "INCOMING@0";
  muxConfig.address() = "0x50";
  muxConfig.kernelDeviceName() = "pca9548";
  muxConfig.pmUnitScopedName() = "SMB_MUX";
  muxConfig.numOutgoingChannels() = 8;
  smbConfig.i2cDeviceConfigs() = {muxConfig};

  snapshotConfig.pmUnitConfigs() = {{"SCM", scmConfig}, {"SMB", smbConfig}};

  // Populate DataStore
  DataStore dataStore(snapshotConfig);
  dataStore.updateI2cBusNum(std::nullopt, "SMBus I801 adapter at 2000", 0);
  dataStore.updateI2cBusNum("/", "SCM_MUX@0", 3);
  dataStore.updateI2cBusNum("/SMB_SLOT@0", "INCOMING@0", 5);
  dataStore.updatePmUnitName("/", "SCM");
  dataStore.updatePmUnitName("/SMB_SLOT@0", "SMB");

  MockPlatformExplorer explorer(snapshotConfig, dataStore);
  PlatformManagerHandler handler(explorer, dataStore, snapshotConfig);

  PlatformSnapshot response;
  handler.getPlatformSnapshot(response);

  // Verify top-level fields
  EXPECT_EQ(*response.platformName(), "TEST_PLATFORM");
  EXPECT_EQ(response.i2cAdaptersFromCpu()->size(), 1);
  EXPECT_EQ(response.i2cAdaptersFromCpu()->at(0), "SMBus I801 adapter at 2000");

  // Verify root PMUnit's I2C device has kernel bus name
  ASSERT_EQ(response.rootSlot()->pluggedInPMUnit()->i2cDevices()->size(), 1);
  auto& sensor = response.rootSlot()->pluggedInPMUnit()->i2cDevices()->at(0);
  EXPECT_EQ(*sensor.busName(), "i2c-0");
  EXPECT_EQ(*sensor.addr(), 0x48);
  EXPECT_EQ(*sensor.kernelDeviceName(), "lm75");
  EXPECT_EQ(*sensor.pmUnitScopedName(), "INLET_SENSOR");

  // Verify outgoing slot
  ASSERT_EQ(response.rootSlot()->pluggedInPMUnit()->outgoingSlots()->size(), 1);
  auto& slot =
      response.rootSlot()->pluggedInPMUnit()->outgoingSlots()->at("SMB_SLOT@0");
  EXPECT_EQ(*slot.slotType(), "SMB_SLOT");
  ASSERT_EQ(slot.outgoingI2cBusNames()->size(), 1);
  EXPECT_EQ(slot.outgoingI2cBusNames()->at(0), "i2c-3");

  // Verify child PMUnit in slot
  ASSERT_TRUE(slot.pluggedInPMUnit().has_value());
  EXPECT_EQ(*slot.pluggedInPMUnit()->name(), "SMB");
  ASSERT_EQ(slot.pluggedInPMUnit()->i2cDevices()->size(), 1);
  auto& mux = slot.pluggedInPMUnit()->i2cDevices()->at(0);
  EXPECT_EQ(*mux.busName(), "i2c-5");
  EXPECT_EQ(*mux.pmUnitScopedName(), "SMB_MUX");
  ASSERT_TRUE(mux.numOutgoingChannels().has_value());
  EXPECT_EQ(*mux.numOutgoingChannels(), 8);

  // Verify i2cPathToHumanFriendlyName map
  EXPECT_EQ(response.i2cPathToHumanFriendlyName()->size(), 3);
  // Global-scope CPU bus uses root path
  EXPECT_EQ(
      response.i2cPathToHumanFriendlyName()->at("i2c-0"),
      "/[SMBus I801 adapter at 2000]");
  // Mux channel on root PmUnit
  EXPECT_EQ(response.i2cPathToHumanFriendlyName()->at("i2c-3"), "/[SCM_MUX@0]");
  // INCOMING bus on child slot
  EXPECT_EQ(
      response.i2cPathToHumanFriendlyName()->at("i2c-5"),
      "/SMB_SLOT@0/[INCOMING@0]");
}

TEST_F(PlatformManagerHandlerTest, GetPlatformSnapshot_EmptyWhenNotExplored) {
  PlatformSnapshot response;
  handler_->getPlatformSnapshot(response);

  EXPECT_EQ(*response.platformName(), "TestPlatform");
  EXPECT_TRUE(response.rootSlot()->slotType()->empty());
}

} // namespace facebook::fboss::platform::platform_manager
