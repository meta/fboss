// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <folly/IPAddressV4.h> // NOLINT(misc-include-cleaner)
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <thrift/lib/cpp2/reflection/testing.h>
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/cli/fboss2/commands/show/transceiver/CmdShowTransceiver.h"
#include "fboss/cli/fboss2/commands/show/transceiver/gen-cpp2/model_types.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;
using namespace facebook::neteng::fboss::bgp::thrift;

namespace facebook::fboss {

/*
 * Set up port test data
 */
std::map<int32_t, PortInfoThrift> createShowTransceiverTestPortEntries() {
  std::map<int32_t, PortInfoThrift> portMap;
  auto addPortEntry = [&portMap](int id) {
    PortInfoThrift portEntry;
    portEntry.portId() = id;
    portEntry.name() = "eth1/" + std::to_string(id) + "/1";

    portMap[id] = std::move(portEntry);
  };

  for (int i = 1; i < 7; i++) {
    addPortEntry(i);
  }
  return portMap;
}

std::map<int32_t, PortStatus> createPortStatusEntries() {
  std::map<int32_t, PortStatus> portStatusMap;
  auto addPortStatusEntry = [&portStatusMap](int id, bool isUp) {
    PortStatus portStatus;
    TransceiverIdxThrift tcvr;
    tcvr.transceiverId() = id;
    portStatus.transceiverIdx() = tcvr;
    portStatus.up() = isUp;

    portStatusMap[id] = std::move(portStatus);
  };

  addPortStatusEntry(1, true);
  addPortStatusEntry(2, true);
  addPortStatusEntry(3, false);
  addPortStatusEntry(4, false);
  addPortStatusEntry(5, true);
  addPortStatusEntry(6, true);

  return portStatusMap;
}

/*
 * Set up transceiver test data
 */
std::map<int32_t, TransceiverInfo> createShowTransceiverTestEntries() {
  std::map<int, TransceiverInfo> transceiverMap;

  // Lambda Helpers
  auto makeVendor =
      [](std::string name, std::string serialNumber, std::string partNumber) {
        Vendor vendor;
        vendor.name() = name;
        vendor.serialNumber() = serialNumber;
        vendor.partNumber() = partNumber;

        return vendor;
      };
  auto makeModuleForFirmware = [](std::string appFwVersion,
                                  std::string dspFwVersion) {
    ModuleStatus moduleStatus;
    FirmwareStatus fwStatus;
    fwStatus.version() = appFwVersion;
    fwStatus.dspFwVer() = dspFwVersion;
    moduleStatus.fwStatus() = fwStatus;

    return moduleStatus;
  };
  auto makeSensor = [](double temp, double voltage) {
    Sensor tempSensor;
    Sensor voltageSensor;
    tempSensor.value() = temp;
    voltageSensor.value() = voltage;

    GlobalSensors sensors;
    sensors.temp() = tempSensor;
    sensors.vcc() = voltageSensor;

    return sensors;
  };

  TransceiverInfo transceiverEntry1;
  transceiverEntry1.tcvrState()->vendor() = makeVendor("vendorOne", "aa", "1");
  transceiverEntry1.tcvrState()->status() = makeModuleForFirmware("1", "2");
  transceiverEntry1.tcvrState()->present() = true;
  transceiverEntry1.tcvrState()->moduleMediaInterface() =
      MediaInterfaceCode::FR4_400G;
  transceiverEntry1.tcvrState()->interfaces() = {"eth1/1/1"};
  transceiverEntry1.tcvrState()->port() = 1;
  transceiverEntry1.tcvrStats()->sensor() = makeSensor(50.0, 25.0);
  transceiverEntry1.tcvrStats()->channels() = {};

  TransceiverInfo transceiverEntry2;
  transceiverEntry2.tcvrState()->vendor() = makeVendor("vendorTwo", "b", "3");
  transceiverEntry2.tcvrState()->present() = true;
  transceiverEntry2.tcvrState()->interfaces() = {"eth1/2/1"};
  transceiverEntry2.tcvrState()->port() = 2;
  transceiverEntry2.tcvrStats()->sensor() = makeSensor(40.0, 25.0);
  transceiverEntry2.tcvrStats()->channels() = {};

  TransceiverInfo transceiverEntry3;
  transceiverEntry3.tcvrState()->vendor() = makeVendor("vendorOne", "ab", "1");
  transceiverEntry3.tcvrState()->present() = false;
  transceiverEntry3.tcvrState()->moduleMediaInterface() =
      MediaInterfaceCode::UNKNOWN;
  transceiverEntry3.tcvrState()->interfaces() = {"eth1/3/1"};
  transceiverEntry3.tcvrState()->port() = 3;
  transceiverEntry3.tcvrStats()->sensor() = makeSensor(0.0, 0.0);
  transceiverEntry3.tcvrStats()->channels() = {};

  TransceiverInfo transceiverEntry4;
  transceiverEntry4.tcvrState()->vendor() = makeVendor("vendorOne", "ac", "1");
  transceiverEntry4.tcvrState()->present() = false;
  transceiverEntry4.tcvrState()->interfaces() = {"eth1/4/1"};
  transceiverEntry4.tcvrState()->port() = 4;
  transceiverEntry4.tcvrStats()->sensor() = makeSensor(0.0, 0.0);
  transceiverEntry4.tcvrStats()->channels() = {};

  TransceiverInfo transceiverEntry5;
  transceiverEntry5.tcvrState()->vendor() = makeVendor("vendorOne", "ad", "2");
  transceiverEntry5.tcvrState()->present() = true;
  transceiverEntry5.tcvrState()->moduleMediaInterface() =
      MediaInterfaceCode::LR4_400G_10KM;
  transceiverEntry5.tcvrState()->interfaces() = {"eth1/5/1"};
  transceiverEntry5.tcvrState()->port() = 5;
  transceiverEntry5.tcvrStats()->sensor() = makeSensor(0.0, 0.0);
  transceiverEntry5.tcvrStats()->channels() = {};

  TransceiverInfo transceiverEntry6;
  transceiverEntry6.tcvrState()->vendor() = makeVendor("vendorThree", "c", "4");
  transceiverEntry6.tcvrState()->status() = makeModuleForFirmware("3", "4");
  transceiverEntry6.tcvrState()->present() = true;
  transceiverEntry6.tcvrState()->moduleMediaInterface() =
      MediaInterfaceCode::FR4_LITE_2x400G;
  transceiverEntry6.tcvrState()->interfaces() = {"eth1/6/1"};
  transceiverEntry6.tcvrState()->port() = 6;
  transceiverEntry6.tcvrStats()->sensor() = makeSensor(30.0, 30.0);
  transceiverEntry6.tcvrStats()->channels() = {};

  // Entry representing a bypass module, which doesn't have an agent port
  TransceiverInfo bypassEntry;
  bypassEntry.tcvrState()->vendor() = makeVendor("vendorBypass", "d", "5");
  bypassEntry.tcvrState()->status() = makeModuleForFirmware("4", "5");
  bypassEntry.tcvrState()->present() = true;
  bypassEntry.tcvrState()->moduleMediaInterface() = MediaInterfaceCode::UNKNOWN;
  bypassEntry.tcvrState()->interfaces() = {"eth1/7/1"};
  bypassEntry.tcvrState()->port() = 7;
  bypassEntry.tcvrStats()->sensor() = makeSensor(0.0, 0.0);
  bypassEntry.tcvrStats()->channels() = {};

  TransceiverInfo absentBypassEntry;
  absentBypassEntry.tcvrState()->present() = false;
  absentBypassEntry.tcvrState()->interfaces() = {"eth1/8/1"};
  absentBypassEntry.tcvrState()->port() = 8;
  absentBypassEntry.tcvrStats()->sensor() = makeSensor(0.0, 0.0);
  absentBypassEntry.tcvrStats()->channels() = {};

  transceiverMap[1] = transceiverEntry1;
  transceiverMap[2] = transceiverEntry2;
  transceiverMap[3] = transceiverEntry3;
  transceiverMap[4] = transceiverEntry4;
  transceiverMap[5] = transceiverEntry5;
  transceiverMap[6] = transceiverEntry6;
  transceiverMap[7] = bypassEntry;
  transceiverMap[8] = absentBypassEntry;

  return transceiverMap;
}

std::map<int32_t, std::string> createTransceiverValidationEntries() {
  return {
      {1, "nonValidatedVendorPartNumber"},
      {2, "missingVendor"},
      {5, "invalidEepromChecksums"},
      {6, ""}};
}

cli::ShowTransceiverModel createTransceiverModel() {
  auto makeDefaultTcvrDetail = [](std::string name,
                                  std::optional<bool> isUp,
                                  bool isPresent,
                                  MediaInterfaceCode media,
                                  int32_t tcvrId,
                                  std::optional<int32_t> portId) {
    cli::TransceiverDetail detail;
    detail.name() = name;
    if (isUp.has_value()) {
      detail.isUp() = *isUp;
    }
    detail.isPresent() = isPresent;
    detail.mediaInterface() = media;
    detail.transceiverID() = tcvrId;
    if (portId.has_value()) {
      detail.portID() = *portId;
    }

    return detail;
  };
  auto setConfigAttributes = [](cli::TransceiverDetail& detail,
                                std::string vendorName,
                                std::string serialNumber,
                                std::string partNumber,
                                std::string appFwVer,
                                std::string dspFwVer) {
    detail.vendor() = vendorName;
    detail.serial() = serialNumber;
    detail.partNumber() = partNumber;
    detail.appFwVer() = appFwVer;
    detail.dspFwVer() = dspFwVer;
  };
  auto setValidationStatus = [](cli::TransceiverDetail& detail,
                                std::string validationStatus,
                                std::string notValidatedReason) {
    detail.validationStatus() = validationStatus;
    detail.notValidatedReason() = notValidatedReason;
  };
  auto setOperAttributes =
      [](cli::TransceiverDetail& detail, double temperature, double voltage) {
        detail.temperature() = temperature;
        detail.voltage() = voltage;
        detail.currentMA() = {};
        detail.txPower() = {};
        detail.rxPower() = {};
        detail.rxSnr() = {};
      };

  cli::ShowTransceiverModel model;

  auto entry1 = makeDefaultTcvrDetail(
      "eth1/1/1", true, true, MediaInterfaceCode::FR4_400G, 1, 1);
  setConfigAttributes(entry1, "vendorOne", "aa", "1", "1", "2");
  setValidationStatus(entry1, "Not Validated", "nonValidatedVendorPartNumber");
  setOperAttributes(entry1, 50.0, 25.0);

  auto entry2 = makeDefaultTcvrDetail(
      "eth1/2/1", true, true, MediaInterfaceCode::UNKNOWN, 2, 2);
  setConfigAttributes(entry2, "vendorTwo", "b", "3", "", "");
  setValidationStatus(entry2, "Not Validated", "missingVendor");
  setOperAttributes(entry2, 40.0, 25.0);

  auto entry3 = makeDefaultTcvrDetail(
      "eth1/3/1", false, false, MediaInterfaceCode::UNKNOWN, 3, 3);
  setConfigAttributes(entry3, "vendorOne", "ab", "1", "", "");
  setValidationStatus(entry3, "--", "--");
  setOperAttributes(entry3, 0.0, 0.0);

  auto entry4 = makeDefaultTcvrDetail(
      "eth1/4/1", false, false, MediaInterfaceCode::UNKNOWN, 4, 4);
  setConfigAttributes(entry4, "vendorOne", "ac", "1", "", "");
  setValidationStatus(entry4, "--", "--");
  setOperAttributes(entry4, 0.0, 0.0);

  auto entry5 = makeDefaultTcvrDetail(
      "eth1/5/1", true, true, MediaInterfaceCode::LR4_400G_10KM, 5, 5);
  setConfigAttributes(entry5, "vendorOne", "ad", "2", "", "");
  setValidationStatus(entry5, "Not Validated", "invalidEepromChecksums");
  setOperAttributes(entry5, 0.0, 0.0);

  auto entry6 = makeDefaultTcvrDetail(
      "eth1/6/1", true, true, MediaInterfaceCode::FR4_LITE_2x400G, 6, 6);
  setConfigAttributes(entry6, "vendorThree", "c", "4", "3", "4");
  setValidationStatus(entry6, "Validated", "--");
  setOperAttributes(entry6, 30.0, 30.0);

  auto bypassEntry = makeDefaultTcvrDetail(
      "eth1/7/1",
      std::nullopt,
      true,
      MediaInterfaceCode::UNKNOWN,
      7,
      std::nullopt);
  setConfigAttributes(bypassEntry, "vendorBypass", "d", "5", "4", "5");
  setValidationStatus(bypassEntry, "--", "--");
  setOperAttributes(bypassEntry, 0.0, 0.0);

  auto absentBypassEntry = makeDefaultTcvrDetail(
      "eth1/8/1",
      std::nullopt,
      false,
      MediaInterfaceCode::UNKNOWN,
      8,
      std::nullopt);
  setConfigAttributes(absentBypassEntry, "", "", "", "", "");
  setValidationStatus(absentBypassEntry, "--", "--");
  setOperAttributes(absentBypassEntry, 0.0, 0.0);

  model.transceivers()->emplace("eth1/1/1", std::move(entry1));
  model.transceivers()->emplace("eth1/2/1", std::move(entry2));
  model.transceivers()->emplace("eth1/3/1", std::move(entry3));
  model.transceivers()->emplace("eth1/4/1", std::move(entry4));
  model.transceivers()->emplace("eth1/5/1", std::move(entry5));
  model.transceivers()->emplace("eth1/6/1", std::move(entry6));
  model.transceivers()->emplace("eth1/7/1", std::move(bypassEntry));
  model.transceivers()->emplace("eth1/8/1", std::move(absentBypassEntry));

  return model;
}

class CmdShowTransceiverTestFixture : public CmdHandlerTestBase {
 public:
  std::map<int32_t, facebook::fboss::PortInfoThrift> mockPortEntries;
  std::map<int32_t, PortStatus> mockPortStatusEntries;
  std::map<int32_t, facebook::fboss::TransceiverInfo> mockTransceiverEntries;
  std::map<int32_t, std::string> mockTransceiverValidationEntries;
  cli::ShowTransceiverModel normalizedModel;

  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    mockPortEntries = createShowTransceiverTestPortEntries();
    mockPortStatusEntries = createPortStatusEntries();
    mockTransceiverEntries = createShowTransceiverTestEntries();
    mockTransceiverValidationEntries = createTransceiverValidationEntries();
    normalizedModel = createTransceiverModel();
  }
};

TEST_F(CmdShowTransceiverTestFixture, queryClient) {
  setupMockedAgentServer();
  EXPECT_CALL(getMockAgent(), getAllPortInfo(_))
      .WillOnce(Invoke([&](auto& entries) { entries = mockPortEntries; }));
  EXPECT_CALL(getMockAgent(), getPortStatus(_, _))
      .WillOnce(Invoke(
          [&](auto& entries, auto) { entries = mockPortStatusEntries; }));

  EXPECT_CALL(getQsfpService(), getTransceiverInfo(_, _))
      .WillOnce(Invoke(
          [&](auto& entries, auto) { entries = mockTransceiverEntries; }));
  EXPECT_CALL(getQsfpService(), getTransceiverConfigValidationInfo(_, _, _))
      .WillOnce(Invoke([&](auto& entries, const auto& transceiverIds, auto) {
        // Every port maps to a distinct transceiver, so all ids are requested
        const std::vector<int32_t> expectedIds = {1, 2, 3, 4, 5, 6};
        EXPECT_EQ(*transceiverIds, expectedIds);

        entries = mockTransceiverValidationEntries;
      }));

  auto cmd = CmdShowTransceiver();
  CmdShowTransceiverTraits::ObjectArgType queriedEntries;
  auto model = cmd.queryClient(localhost(), queriedEntries);

  EXPECT_THRIFT_EQ(normalizedModel, model);

  // Verify that the model contains all transceivers including bypass modules
  EXPECT_EQ(model.transceivers()->size(), 8);

  // Verify bypass module (transceiver 7) is present
  EXPECT_TRUE(
      model.transceivers()->find("eth1/7/1") != model.transceivers()->end());
  auto& bypassModule = model.transceivers()->at("eth1/7/1");
  EXPECT_EQ(bypassModule.name().value(), "eth1/7/1");
  EXPECT_FALSE(
      bypassModule.isUp().has_value()); // Bypass modules have no port status
  EXPECT_TRUE(bypassModule.isPresent().value());
  EXPECT_EQ(bypassModule.vendor().value(), "vendorBypass");

  // Verify absent bypass module (transceiver 8) is present
  EXPECT_TRUE(
      model.transceivers()->find("eth1/8/1") != model.transceivers()->end());
  auto& absentBypassModule = model.transceivers()->at("eth1/8/1");
  EXPECT_EQ(absentBypassModule.name().value(), "eth1/8/1");
  EXPECT_FALSE(absentBypassModule.isUp().has_value());
  EXPECT_FALSE(absentBypassModule.isPresent().value());
}

TEST_F(CmdShowTransceiverTestFixture, queryClientFilteredByPort) {
  setupMockedAgentServer();

  // Query for a specific port
  CmdShowTransceiverTraits::ObjectArgType queriedEntries = {"eth1/1/1"};

  // getAllPortInfo should still return all ports
  EXPECT_CALL(getMockAgent(), getAllPortInfo(_))
      .WillOnce(Invoke([&](auto& entries) { entries = mockPortEntries; }));

  // getPortStatus should be called with only the filtered port ID (port 1)
  EXPECT_CALL(getMockAgent(), getPortStatus(_, _))
      .WillOnce(Invoke([&](auto& entries, const auto& portIds) {
        // Verify that all ports are requested, filtering is done later in
        // createModel
        EXPECT_EQ(portIds->size(), 6);
        EXPECT_EQ((*portIds)[0], 1);

        // Return only the status for port 1
        std::map<int32_t, PortStatus> filteredStatuses;
        filteredStatuses[1] = mockPortStatusEntries[1];
        entries = filteredStatuses;
      }));

  // getTransceiverInfo should query all transceivers, filtering is done later
  // in createModel
  EXPECT_CALL(getQsfpService(), getTransceiverInfo(_, _))
      .WillOnce(Invoke([&](auto& entries, const auto& transceiverIds) {
        // Verify that all transceivers are requested
        EXPECT_EQ(transceiverIds->size(), 0);

        // Return only the transceiver info for transceiver 1
        std::map<int32_t, TransceiverInfo> filteredTransceivers;
        filteredTransceivers[1] = mockTransceiverEntries[1];
        entries = filteredTransceivers;
      }));

  EXPECT_CALL(getQsfpService(), getTransceiverConfigValidationInfo(_, _, _))
      .WillOnce(Invoke([&](auto& entries, const auto& transceiverIds, auto) {
        // Verify that only transceiver 1 is requested
        EXPECT_EQ(transceiverIds->size(), 1);
        EXPECT_EQ((*transceiverIds)[0], 1);

        // Return only the validation info for transceiver 1
        std::map<int32_t, std::string> filteredValidation;
        filteredValidation[1] = mockTransceiverValidationEntries[1];
        entries = filteredValidation;
      }));

  auto cmd = CmdShowTransceiver();
  auto model = cmd.queryClient(localhost(), queriedEntries);

  // Verify that the model contains only the requested interface
  EXPECT_EQ(model.transceivers()->size(), 1);
  EXPECT_TRUE(
      model.transceivers()->find("eth1/1/1") != model.transceivers()->end());

  // Verify it's the correct transceiver
  auto& tcvrDetail = model.transceivers()->at("eth1/1/1");
  EXPECT_EQ(tcvrDetail.name().value(), "eth1/1/1");
  EXPECT_EQ(tcvrDetail.vendor().value(), "vendorOne");
  EXPECT_EQ(tcvrDetail.serial().value(), "aa");
  EXPECT_TRUE(tcvrDetail.isUp().value());
}

TEST_F(CmdShowTransceiverTestFixture, queryClientFilteredByMultiplePorts) {
  setupMockedAgentServer();

  // Query for multiple specific ports
  CmdShowTransceiverTraits::ObjectArgType queriedEntries = {
      "eth1/1/1", "eth1/6/1"};

  EXPECT_CALL(getMockAgent(), getAllPortInfo(_))
      .WillOnce(Invoke([&](auto& entries) { entries = mockPortEntries; }));

  EXPECT_CALL(getMockAgent(), getPortStatus(_, _))
      .WillOnce(Invoke([&](auto& entries, const auto& portIds) {
        // We query all ports, filtering is done in createModel
        EXPECT_EQ(portIds->size(), 6);
        EXPECT_TRUE(
            std::find(portIds->begin(), portIds->end(), 1) != portIds->end());
        EXPECT_TRUE(
            std::find(portIds->begin(), portIds->end(), 6) != portIds->end());

        // Return only the status for ports 1 and 6
        std::map<int32_t, PortStatus> filteredStatuses;
        filteredStatuses[1] = mockPortStatusEntries[1];
        filteredStatuses[6] = mockPortStatusEntries[6];
        entries = filteredStatuses;
      }));

  EXPECT_CALL(getQsfpService(), getTransceiverInfo(_, _))
      .WillOnce(Invoke([&](auto& entries, const auto& transceiverIds) {
        // We query all transceivers, filtering is done later in createModel
        EXPECT_EQ(transceiverIds->size(), 0);

        // Return only the transceiver info for transceivers 1 and 6
        std::map<int32_t, TransceiverInfo> filteredTransceivers;
        filteredTransceivers[1] = mockTransceiverEntries[1];
        filteredTransceivers[6] = mockTransceiverEntries[6];
        entries = filteredTransceivers;
      }));

  EXPECT_CALL(getQsfpService(), getTransceiverConfigValidationInfo(_, _, _))
      .WillOnce(Invoke([&](auto& entries, const auto& transceiverIds, auto) {
        // Verify that only transceivers 1 and 6 are requested
        EXPECT_EQ(transceiverIds->size(), 2);

        // Return only the validation info for transceivers 1 and 6
        std::map<int32_t, std::string> filteredValidation;
        filteredValidation[1] = mockTransceiverValidationEntries[1];
        filteredValidation[6] = mockTransceiverValidationEntries[6];
        entries = filteredValidation;
      }));

  auto cmd = CmdShowTransceiver();
  auto model = cmd.queryClient(localhost(), queriedEntries);

  // Verify that the model contains only the requested interfaces
  EXPECT_EQ(model.transceivers()->size(), 2);
  EXPECT_TRUE(
      model.transceivers()->find("eth1/1/1") != model.transceivers()->end());
  EXPECT_TRUE(
      model.transceivers()->find("eth1/6/1") != model.transceivers()->end());

  // Verify the correct transceivers
  auto& tcvr1 = model.transceivers()->at("eth1/1/1");
  EXPECT_EQ(tcvr1.vendor().value(), "vendorOne");

  auto& tcvr6 = model.transceivers()->at("eth1/6/1");
  EXPECT_EQ(tcvr6.vendor().value(), "vendorThree");
}

TEST_F(CmdShowTransceiverTestFixture, queryClientFilteredMultiPortModule) {
  // Test filtering by a subset of ports for a multi-port module
  setupMockedAgentServer();

  // Create a multi-port module (transceiver 10) with 8 interfaces
  std::map<int32_t, PortInfoThrift> multiPortPortEntries;
  std::map<int32_t, PortStatus> multiPortPortStatusEntries;
  for (int i = 1; i <= 8; i++) {
    PortInfoThrift portEntry;
    portEntry.portId() = i;
    portEntry.name() = folly::to<std::string>("fab1/1/", i);
    multiPortPortEntries[i] = std::move(portEntry);

    PortStatus portStatus;
    TransceiverIdxThrift tcvr;
    tcvr.transceiverId() = 10;
    portStatus.transceiverIdx() = tcvr;
    portStatus.up() = true;
    multiPortPortStatusEntries[i] = std::move(portStatus);
  }

  // Create a single transceiver with all 8 interfaces
  std::map<int32_t, TransceiverInfo> multiPortTransceiverEntries;
  TransceiverInfo multiPortTransceiver;
  multiPortTransceiver.tcvrState()->vendor() = []() {
    Vendor vendor;
    vendor.name() = "vendorMultiPort";
    vendor.serialNumber() = "mp123";
    vendor.partNumber() = "mp-part-1";
    return vendor;
  }();
  multiPortTransceiver.tcvrState()->present() = true;
  multiPortTransceiver.tcvrState()->moduleMediaInterface() =
      MediaInterfaceCode::FR8_800G;
  multiPortTransceiver.tcvrState()->interfaces() = {
      "fab1/1/1",
      "fab1/1/2",
      "fab1/1/3",
      "fab1/1/4",
      "fab1/1/5",
      "fab1/1/6",
      "fab1/1/7",
      "fab1/1/8"};
  multiPortTransceiver.tcvrState()->port() = 10;
  multiPortTransceiver.tcvrStats()->sensor() = []() {
    Sensor tempSensor;
    Sensor voltageSensor;
    tempSensor.value() = 45.0;
    voltageSensor.value() = 28.0;
    GlobalSensors sensors;
    sensors.temp() = tempSensor;
    sensors.vcc() = voltageSensor;
    return sensors;
  }();
  multiPortTransceiver.tcvrStats()->channels() = {};
  multiPortTransceiverEntries[10] = multiPortTransceiver;

  std::map<int32_t, std::string> multiPortValidationEntries;
  multiPortValidationEntries[10] = "";

  // Query for a subset of ports (fab1/1/2 and fab1/1/5)
  CmdShowTransceiverTraits::ObjectArgType queriedEntries = {
      "fab1/1/2", "fab1/1/5"};

  EXPECT_CALL(getMockAgent(), getAllPortInfo(_))
      .WillOnce(Invoke([&](auto& entries) { entries = multiPortPortEntries; }));

  EXPECT_CALL(getMockAgent(), getPortStatus(_, _))
      .WillOnce(Invoke([&](auto& entries, const auto& portIds) {
        // Verify all ports are requested
        EXPECT_EQ(portIds->size(), 8);

        // Return status for all ports
        entries = multiPortPortStatusEntries;
      }));

  EXPECT_CALL(getQsfpService(), getTransceiverInfo(_, _))
      .WillOnce(Invoke([&](auto& entries, const auto& transceiverIds) {
        // Verify all tcvrs are requested
        EXPECT_EQ(transceiverIds->size(), 0);

        // Return the multi-port transceiver
        entries = multiPortTransceiverEntries;
      }));

  EXPECT_CALL(getQsfpService(), getTransceiverConfigValidationInfo(_, _, _))
      .WillOnce(Invoke([&](auto& entries, const auto& transceiverIds, auto) {
        // All 8 ports share transceiver 10, so it's only requested once
        const std::vector<int32_t> expectedIds = {10};
        EXPECT_EQ(*transceiverIds, expectedIds);

        entries = multiPortValidationEntries;
      }));

  auto cmd = CmdShowTransceiver();
  auto model = cmd.queryClient(localhost(), queriedEntries);

  // Verify that the model contains only the requested interfaces
  EXPECT_EQ(model.transceivers()->size(), 2);
  EXPECT_TRUE(
      model.transceivers()->find("fab1/1/2") != model.transceivers()->end());
  EXPECT_TRUE(
      model.transceivers()->find("fab1/1/5") != model.transceivers()->end());

  // Verify the entries are correct
  auto& tcvr2 = model.transceivers()->at("fab1/1/2");
  EXPECT_EQ(tcvr2.name().value(), "fab1/1/2");
  EXPECT_TRUE(tcvr2.isUp().has_value());
  EXPECT_TRUE(tcvr2.isUp().value());
  EXPECT_EQ(tcvr2.vendor().value(), "vendorMultiPort");
  EXPECT_EQ(tcvr2.serial().value(), "mp123");

  auto& tcvr5 = model.transceivers()->at("fab1/1/5");
  EXPECT_EQ(tcvr5.name().value(), "fab1/1/5");
  EXPECT_TRUE(tcvr5.isUp().has_value());
  EXPECT_TRUE(tcvr5.isUp().value());
  EXPECT_EQ(tcvr5.vendor().value(), "vendorMultiPort");
  EXPECT_EQ(tcvr5.serial().value(), "mp123");
}

TEST_F(CmdShowTransceiverTestFixture, queryClientFilteredBypassModule) {
  // Test filtering for a single bypass module (module without port entry)
  setupMockedAgentServer();

  // Query for a bypass module interface
  CmdShowTransceiverTraits::ObjectArgType queriedEntries = {"eth1/7/1"};

  EXPECT_CALL(getMockAgent(), getAllPortInfo(_))
      .WillOnce(Invoke([&](auto& entries) { entries = mockPortEntries; }));

  EXPECT_CALL(getMockAgent(), getPortStatus(_, _))
      .WillOnce(Invoke([&](auto& entries, const auto& portIds) {
        // All agent ports are queried
        EXPECT_EQ(portIds->size(), 6);

        // Return all port statuses (bypass module won't have a port status)
        entries = mockPortStatusEntries;
      }));

  EXPECT_CALL(getQsfpService(), getTransceiverInfo(_, _))
      .WillOnce(Invoke([&](auto& entries, const auto& transceiverIds) {
        // Verify that we query all tcvrs
        EXPECT_EQ(transceiverIds->size(), 0);

        entries = mockTransceiverEntries;
      }));

  EXPECT_CALL(getQsfpService(), getTransceiverConfigValidationInfo(_, _, _))
      .WillOnce(Invoke([&](auto& entries, const auto& transceiverIds, auto) {
        // Won't include the bypass modules, since there's no port status
        EXPECT_EQ(transceiverIds->size(), 6);

        entries = mockTransceiverValidationEntries;
      }));

  auto cmd = CmdShowTransceiver();
  auto model = cmd.queryClient(localhost(), queriedEntries);

  // Check that only the bypass module is reported
  EXPECT_EQ(model.transceivers()->size(), 1);
  EXPECT_TRUE(
      model.transceivers()->find("eth1/7/1") != model.transceivers()->end());

  // Check that the contents of the entry are correct (should be listed as
  // a bypass module)
  auto& bypassModule = model.transceivers()->at("eth1/7/1");
  EXPECT_EQ(bypassModule.name().value(), "eth1/7/1");
  EXPECT_FALSE(
      bypassModule.isUp().has_value()); // Bypass modules have no port status
  EXPECT_TRUE(bypassModule.isPresent().value());
  EXPECT_EQ(bypassModule.vendor().value(), "vendorBypass");
  EXPECT_EQ(bypassModule.serial().value(), "d");
  EXPECT_EQ(bypassModule.partNumber().value(), "5");
}

// Verify that we properly handle the case of an optic that supports more
// interfaces than are currently active (e.g. 2x800G optic that supports 8x200G
// mode, but is running in 2x800G mode)
TEST_F(CmdShowTransceiverTestFixture, multiPortOpticWithExtraInterfaces) {
  setupMockedAgentServer();

  std::map<int32_t, PortInfoThrift> activePortEntries;
  PortInfoThrift port1;
  port1.portId() = 1;
  port1.name() = "eth1/20/1";
  activePortEntries[1] = port1;

  PortInfoThrift port5;
  port5.portId() = 5;
  port5.name() = "eth1/20/5";
  activePortEntries[5] = port5;

  // Create port status entries for the two active interfaces
  std::map<int32_t, PortStatus> activePortStatusEntries;
  PortStatus status1;
  TransceiverIdxThrift tcvr1;
  tcvr1.transceiverId() = 20;
  status1.transceiverIdx() = tcvr1;
  status1.up() = true;
  activePortStatusEntries[1] = status1;

  PortStatus status5;
  TransceiverIdxThrift tcvr5;
  tcvr5.transceiverId() = 20;
  status5.transceiverIdx() = tcvr5;
  status5.up() = true;
  activePortStatusEntries[5] = status5;

  std::map<int32_t, TransceiverInfo> transceiverEntries;
  TransceiverInfo tcvrInfo;
  Vendor vendor;
  vendor.name() = "vendor2x800G";
  vendor.serialNumber() = "sn800g";
  vendor.partNumber() = "pn800g";
  tcvrInfo.tcvrState()->vendor() = vendor;
  tcvrInfo.tcvrState()->present() = true;
  tcvrInfo.tcvrState()->moduleMediaInterface() = MediaInterfaceCode::FR8_800G;
  // TransceiverInfo lists all 8 possible interfaces
  tcvrInfo.tcvrState()->interfaces() = {
      "eth1/20/1",
      "eth1/20/2",
      "eth1/20/3",
      "eth1/20/4",
      "eth1/20/5",
      "eth1/20/6",
      "eth1/20/7",
      "eth1/20/8"};
  tcvrInfo.tcvrState()->port() = 20;
  Sensor tempSensor;
  Sensor voltageSensor;
  tempSensor.value() = 40.0;
  voltageSensor.value() = 26.0;
  GlobalSensors sensors;
  sensors.temp() = tempSensor;
  sensors.vcc() = voltageSensor;
  tcvrInfo.tcvrStats()->sensor() = sensors;
  tcvrInfo.tcvrStats()->channels() = {};
  transceiverEntries[20] = tcvrInfo;

  std::map<int32_t, std::string> validationEntries;
  validationEntries[20] = "";

  // Query without filtering (should only show the two active interfaces)
  CmdShowTransceiverTraits::ObjectArgType queriedEntries;

  EXPECT_CALL(getMockAgent(), getAllPortInfo(_))
      .WillOnce(Invoke([&](auto& entries) { entries = activePortEntries; }));

  EXPECT_CALL(getMockAgent(), getPortStatus(_, _))
      .WillOnce(Invoke(
          [&](auto& entries, auto) { entries = activePortStatusEntries; }));

  EXPECT_CALL(getQsfpService(), getTransceiverInfo(_, _))
      .WillOnce(
          Invoke([&](auto& entries, auto) { entries = transceiverEntries; }));

  EXPECT_CALL(getQsfpService(), getTransceiverConfigValidationInfo(_, _, _))
      .WillOnce(Invoke([&](auto& entries, const auto& transceiverIds, auto) {
        // Both active ports share transceiver 20, so it's only requested once
        const std::vector<int32_t> expectedIds = {20};
        EXPECT_EQ(*transceiverIds, expectedIds);

        entries = validationEntries;
      }));

  auto cmd = CmdShowTransceiver();
  auto model = cmd.queryClient(localhost(), queriedEntries);

  // Verify that only the two active interfaces are shown, not all 8
  EXPECT_EQ(model.transceivers()->size(), 2);
  EXPECT_TRUE(
      model.transceivers()->find("eth1/20/1") != model.transceivers()->end());
  EXPECT_TRUE(
      model.transceivers()->find("eth1/20/5") != model.transceivers()->end());

  // Verify that the inactive interfaces are NOT shown
  EXPECT_TRUE(
      model.transceivers()->find("eth1/20/2") == model.transceivers()->end());
  EXPECT_TRUE(
      model.transceivers()->find("eth1/20/3") == model.transceivers()->end());
  EXPECT_TRUE(
      model.transceivers()->find("eth1/20/4") == model.transceivers()->end());
  EXPECT_TRUE(
      model.transceivers()->find("eth1/20/6") == model.transceivers()->end());
  EXPECT_TRUE(
      model.transceivers()->find("eth1/20/7") == model.transceivers()->end());
  EXPECT_TRUE(
      model.transceivers()->find("eth1/20/8") == model.transceivers()->end());

  auto& activeIntf1 = model.transceivers()->at("eth1/20/1");
  EXPECT_EQ(activeIntf1.name().value(), "eth1/20/1");
  EXPECT_TRUE(activeIntf1.isUp().has_value());
  EXPECT_TRUE(activeIntf1.isUp().value());
  EXPECT_TRUE(activeIntf1.isPresent().value());
  EXPECT_EQ(activeIntf1.vendor().value(), "vendor2x800G");
  EXPECT_EQ(activeIntf1.serial().value(), "sn800g");

  auto& activeIntf5 = model.transceivers()->at("eth1/20/5");
  EXPECT_EQ(activeIntf5.name().value(), "eth1/20/5");
  EXPECT_TRUE(activeIntf5.isUp().has_value());
  EXPECT_TRUE(activeIntf5.isUp().value());
  EXPECT_TRUE(activeIntf5.isPresent().value());
  EXPECT_EQ(activeIntf5.vendor().value(), "vendor2x800G");
  EXPECT_EQ(activeIntf5.serial().value(), "sn800g");
}

TEST_F(CmdShowTransceiverTestFixture, printOutput) {
  std::stringstream ss;
  CmdShowTransceiver().printOutput(normalizedModel, ss);

  std::string output = ss.str();
  std::string expectOutput =
      " Interface  Status  Transceiver      CfgValidated   Reason                        Vendor        Serial  Part Number  FW App Version  FW DSP Version  Temperature (C)  Voltage (V)  Current (mA)  Tx Power (dBm)  Rx Power (dBm)  Rx SNR \n"
      "---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n"
      " eth1/1/1   Up      FR4_400G         Not Validated  nonValidatedVendorPartNumber  vendorOne     aa      1            1               2               50.00            25.00                                                             \n"
      " eth1/2/1   Up      UNKNOWN          Not Validated  missingVendor                 vendorTwo     b       3                                            40.00            25.00                                                             \n"
      " eth1/3/1   Down    Absent           --             --                            vendorOne     ab      1                                            0.00             0.00                                                              \n"
      " eth1/4/1   Down    Absent           --             --                            vendorOne     ac      1                                            0.00             0.00                                                              \n"
      " eth1/5/1   Up      LR4_400G_10KM    Not Validated  invalidEepromChecksums        vendorOne     ad      2                                            0.00             0.00                                                              \n"
      " eth1/6/1   Up      FR4_LITE_2x400G  Validated      --                            vendorThree   c       4            3               4               30.00            30.00                                                             \n"
      " eth1/7/1   Bypass  UNKNOWN          --             --                            vendorBypass  d       5            4               5               0.00             0.00                                                              \n"
      " eth1/8/1   Bypass  Absent           --             --                                                                                               0.00             0.00                                                              \n\n";

  EXPECT_EQ(output, expectOutput);
}

// CLI reference wiki hooks: a human description and a non-empty sample model.
// Property checks only (no golden text).
TEST_F(CmdShowTransceiverTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowTransceiverTraits::description().empty());
  EXPECT_FALSE(CmdShowTransceiver::sampleModel().transceivers()->empty());
}

TEST_F(CmdShowTransceiverTestFixture, printOutputSortsByNumericId) {
  // Rows are ordered by (transceiver ID, port ID), not by name. The model is
  // keyed by name, so a name that sorts early lexicographically but has a high
  // transceiver ID must still be printed last.
  auto model = normalizedModel;
  auto detail = model.transceivers()->at("eth1/1/1");
  detail.name() = "eth1/10/1";
  detail.transceiverID() = 10;
  detail.portID() = 10;
  model.transceivers()->emplace("eth1/10/1", std::move(detail));

  std::stringstream ss;
  CmdShowTransceiver().printOutput(model, ss);
  std::string output = ss.str();

  ASSERT_NE(output.find("eth1/10/1"), std::string::npos);
  EXPECT_GT(output.find("eth1/10/1"), output.find("eth1/8/1"));
}

TEST_F(CmdShowTransceiverTestFixture, printOutputFreeFormNames) {
  // Interface names can be arbitrary (e.g. SVIs named "downlinks") or unset.
  // Ordering stays numeric and printing must not throw.
  cli::ShowTransceiverModel model;
  auto makeDetail = [](const std::string& name, int32_t tcvrId) {
    cli::TransceiverDetail detail;
    detail.name() = name;
    detail.transceiverID() = tcvrId;
    detail.isPresent() = false;
    detail.mediaInterface() = MediaInterfaceCode::UNKNOWN;
    detail.vendor() = "";
    detail.serial() = "";
    detail.partNumber() = "";
    detail.appFwVer() = "";
    detail.dspFwVer() = "";
    detail.validationStatus() = "--";
    detail.notValidatedReason() = "--";
    detail.temperature() = 0.0;
    detail.voltage() = 0.0;
    detail.currentMA() = {};
    detail.txPower() = {};
    detail.rxPower() = {};
    detail.rxSnr() = {};
    return detail;
  };
  model.transceivers()->emplace("downlinks", makeDetail("downlinks", 3));
  model.transceivers()->emplace("eth1/1/1", makeDetail("eth1/1/1", 1));

  std::stringstream ss;
  ASSERT_NO_THROW(CmdShowTransceiver().printOutput(model, ss));
  std::string output = ss.str();

  ASSERT_NE(output.find("downlinks"), std::string::npos);
  // Parseable names come first; "downlinks" carries no ordering.
  EXPECT_GT(output.find("downlinks"), output.find("eth1/1/1"));
}

} // namespace facebook::fboss
