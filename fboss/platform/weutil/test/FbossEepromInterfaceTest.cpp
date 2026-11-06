// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <folly/FileUtil.h>
#include <folly/testing/TestUtil.h>

#include "fboss/platform/weutil/FbossEepromInterface.h"
#include "fboss/platform/weutil/if/gen-cpp2/eeprom_contents_types.h"

namespace facebook::fboss::platform {

namespace {
using EepromData = std::vector<uint8_t>;

// Based on the Spec for V5 EEPROM:
// https://github.com/meta/fboss/blob/main/fboss/docs/meta_eeprom_format_v5.md
const EepromData kEepromV5 = {
    0xfb, 0xfb, 0x05, 0xff, 0x01, 0x0d, 0x46, 0x49, 0x52, 0x53, 0x54, 0x5f,
    0x53, 0x51, 0x55, 0x45, 0x45, 0x5a, 0x45, 0x02, 0x08, 0x32, 0x30, 0x31,
    0x32, 0x33, 0x34, 0x35, 0x36, 0x03, 0x08, 0x53, 0x59, 0x53, 0x41, 0x31,
    0x32, 0x33, 0x34, 0x04, 0x0c, 0x50, 0x43, 0x42, 0x41, 0x31, 0x32, 0x33,
    0x34, 0x35, 0x36, 0x37, 0x20, 0x05, 0x0c, 0x50, 0x43, 0x42, 0x31, 0x32,
    0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x20, 0x06, 0x0c, 0x4d, 0x59, 0x4f,
    0x44, 0x4d, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x07, 0x0d, 0x4f,
    0x53, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x41, 0x42,
    0x08, 0x01, 0x01, 0x09, 0x01, 0x00, 0x0a, 0x01, 0x01, 0x0b, 0x0d, 0x50,
    0x53, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x41,
    0x0c, 0x07, 0x55, 0x4e, 0x41, 0x5f, 0x4d, 0x41, 0x53, 0x0d, 0x08, 0x32,
    0x30, 0x31, 0x33, 0x30, 0x32, 0x30, 0x33, 0x0e, 0x05, 0x54, 0x45, 0x52,
    0x5a, 0x4f, 0x0f, 0x09, 0x4a, 0x55, 0x49, 0x43, 0x45, 0x54, 0x4f, 0x52,
    0x59, 0x10, 0x07, 0x42, 0x55, 0x44, 0x4f, 0x4b, 0x41, 0x4e, 0x11, 0x08,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x01, 0x02, 0x12, 0x08, 0x12, 0x34,
    0x56, 0x78, 0x9a, 0xbc, 0x03, 0x04, 0x13, 0x08, 0x66, 0x55, 0x44, 0x33,
    0x22, 0x11, 0x02, 0x00, 0x14, 0x08, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54,
    0x00, 0x02, 0xfa, 0x02, 0xd5, 0xc6, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

// EEPROM V5 with wrong CRC Programmed (same as the one above, but last 2
// bytes have wrong CRC value programmed.)
const EepromData kEepromV5WrongCrc = {
    0xfb, 0xfb, 0x05, 0xff, 0x01, 0x0d, 0x46, 0x49, 0x52, 0x53, 0x54, 0x5f,
    0x53, 0x51, 0x55, 0x45, 0x45, 0x5a, 0x45, 0x02, 0x08, 0x32, 0x30, 0x31,
    0x32, 0x33, 0x34, 0x35, 0x36, 0x03, 0x08, 0x53, 0x59, 0x53, 0x41, 0x31,
    0x32, 0x33, 0x34, 0x04, 0x0c, 0x50, 0x43, 0x42, 0x41, 0x31, 0x32, 0x33,
    0x34, 0x35, 0x36, 0x37, 0x20, 0x05, 0x0c, 0x50, 0x43, 0x42, 0x31, 0x32,
    0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x20, 0x06, 0x0c, 0x4d, 0x59, 0x4f,
    0x44, 0x4d, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x07, 0x0d, 0x4f,
    0x53, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x41, 0x42,
    0x08, 0x01, 0x01, 0x09, 0x01, 0x00, 0x0a, 0x01, 0x01, 0x0b, 0x0d, 0x50,
    0x53, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x41,
    0x0c, 0x07, 0x55, 0x4e, 0x41, 0x5f, 0x4d, 0x41, 0x53, 0x0d, 0x08, 0x32,
    0x30, 0x31, 0x33, 0x30, 0x32, 0x30, 0x33, 0x0e, 0x05, 0x54, 0x45, 0x52,
    0x5a, 0x4f, 0x0f, 0x09, 0x4a, 0x55, 0x49, 0x43, 0x45, 0x54, 0x4f, 0x52,
    0x59, 0x10, 0x07, 0x42, 0x55, 0x44, 0x4f, 0x4b, 0x41, 0x4e, 0x11, 0x08,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x01, 0x02, 0x12, 0x08, 0x12, 0x34,
    0x56, 0x78, 0x9a, 0xbc, 0x03, 0x04, 0x13, 0x08, 0x66, 0x55, 0x44, 0x33,
    0x22, 0x11, 0x02, 0x00, 0x14, 0x08, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54,
    0x00, 0x02, 0xfa, 0x02, 0xa6, 0xb7, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

// Based on the Spec for V6 EEPROM:
// https://github.com/meta/fboss/blob/main/fboss/docs/meta_eeprom_format_v6.md
const EepromData kEepromV6 = {
    0xfb, 0xfb, 0x06, 0xff, 0x01, 0x0d, 0x46, 0x49, 0x52, 0x53, 0x54, 0x5f,
    0x53, 0x51, 0x55, 0x45, 0x45, 0x5a, 0x45, 0x02, 0x08, 0x32, 0x30, 0x31,
    0x32, 0x33, 0x34, 0x35, 0x36, 0x03, 0x08, 0x53, 0x59, 0x53, 0x41, 0x31,
    0x32, 0x33, 0x34, 0x04, 0x0c, 0x50, 0x43, 0x42, 0x41, 0x31, 0x32, 0x33,
    0x34, 0x35, 0x36, 0x37, 0x20, 0x05, 0x0c, 0x50, 0x43, 0x42, 0x31, 0x32,
    0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x20, 0x06, 0x0c, 0x4d, 0x59, 0x4f,
    0x44, 0x4d, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x07, 0x0d, 0x4f,
    0x53, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x41, 0x42,
    0x08, 0x01, 0x01, 0x09, 0x01, 0x00, 0x0a, 0x01, 0x01, 0x0b, 0x0d, 0x50,
    0x53, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x41,
    0x0c, 0x07, 0x55, 0x4e, 0x41, 0x5f, 0x4d, 0x41, 0x53, 0x0d, 0x08, 0x32,
    0x30, 0x31, 0x33, 0x30, 0x32, 0x30, 0x33, 0x0e, 0x05, 0x54, 0x45, 0x52,
    0x5a, 0x4f, 0x0f, 0x09, 0x4a, 0x55, 0x49, 0x43, 0x45, 0x54, 0x4f, 0x52,
    0x59, 0x10, 0x07, 0x42, 0x55, 0x44, 0x4f, 0x4b, 0x41, 0x4e, 0x11, 0x08,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x01, 0x02, 0x12, 0x08, 0x12, 0x34,
    0x56, 0x78, 0x9a, 0xbc, 0x03, 0x04, 0x13, 0x08, 0x66, 0x55, 0x44, 0x33,
    0x22, 0x11, 0x02, 0x00, 0x14, 0x08, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54,
    0x00, 0x02, 0x15, 0x01, 0x01, 0x65, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x66, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f, 0xfa, 0x02, 0x4a, 0x05, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff};

constexpr auto kProductName = "FIRST_SQUEEZE";
constexpr auto kProductPartNumber = "20123456";
constexpr auto kSystemAssemblyPartNumber = "SYSA1234";
constexpr auto kMetaPCBAPartNumber = "PCBA1234567";
constexpr auto kMetaPCBPartNumber = "PCB12345678";
constexpr auto kOdmJdmPCBAPartNumber = "MYODM1234567";
constexpr auto kOdmJdmPCBASerialNumber = "OS123456789AB";
constexpr auto kProductionState = "1";
constexpr auto kProductionSubState = "0";
constexpr auto kVariantIndicator = "1";
constexpr auto kProductSerialNumber = "PS1234567890A";
constexpr auto kSystemManufacturer = "UNA_MAS";
constexpr auto kSystemManufacturingDate = "20130203";
constexpr auto kPCBManufacturer = "TERZO";
constexpr auto kAssembledAt = "JUICETORY";
constexpr auto kEepromLocationOnFabric = "BUDOKAN";
constexpr auto kX86CpuMac = "11:22:33:44:55:66,258";
constexpr auto kX86CpuMacBase = "11:22:33:44:55:66";
constexpr auto kX86CpuMacSize = "258";
constexpr auto kBmcMac = "12:34:56:78:9a:bc,772";
constexpr auto kBmcMacBase = "12:34:56:78:9a:bc";
constexpr auto kBmcMacSize = "772";
constexpr auto kSwitchAsicMac = "66:55:44:33:22:11,512";
constexpr auto kSwitchAsicMacBase = "66:55:44:33:22:11";
constexpr auto kSwitchAsicMacSize = "512";
constexpr auto kMetaReservedMac = "fe:dc:ba:98:76:54,2";
constexpr auto kMetaReservedMacBase = "fe:dc:ba:98:76:54";
constexpr auto kMetaReservedMacSize = "2";
constexpr auto kRma = "1";
constexpr auto kVendorDefinedField1 = "0x0101010101";
constexpr auto kVendorDefinedField2 = "0x48656c6c6f";
constexpr auto kVendorDefinedField3 = "";
constexpr auto kCrc16V5 = "0xd5c6 (CRC Matched)";
constexpr auto kCrc16V6 = "0x4a05 (CRC Matched)";
constexpr auto kCrc16V5Wrong = "0xa6b7 (CRC Mismatch. Expected 0xd5c6)";

EepromContents createEepromContents(int version, bool crcMatched = true) {
  EepromContents result;
  result.version() = version;
  result.productName() = kProductName;
  result.productPartNumber() = kProductPartNumber;
  result.systemAssemblyPartNumber() = kSystemAssemblyPartNumber;
  result.metaPCBAPartNumber() = kMetaPCBAPartNumber;
  result.metaPCBPartNumber() = kMetaPCBPartNumber;
  result.odmJdmPCBAPartNumber() = kOdmJdmPCBAPartNumber;
  result.odmJdmPCBASerialNumber() = kOdmJdmPCBASerialNumber;
  result.productionState() = kProductionState;
  result.productionSubState() = kProductionSubState;
  result.variantIndicator() = kVariantIndicator;
  result.productSerialNumber() = kProductSerialNumber;
  result.systemManufacturer() = kSystemManufacturer;
  result.systemManufacturingDate() = kSystemManufacturingDate;
  result.pcbManufacturer() = kPCBManufacturer;
  result.assembledAt() = kAssembledAt;
  result.eepromLocationOnFabric() = kEepromLocationOnFabric;
  result.x86CpuMac() = kX86CpuMac;
  result.bmcMac() = kBmcMac;
  result.switchAsicMac() = kSwitchAsicMac;
  result.metaReservedMac() = kMetaReservedMac;

  if (version == 5) {
    result.crc16() = crcMatched ? kCrc16V5 : kCrc16V5Wrong;
  } else if (version == 6) {
    result.crc16() = kCrc16V6;
  }

  if (version == 6) {
    result.rma() = kRma;
    result.vendorDefinedField1() = kVendorDefinedField1;
    result.vendorDefinedField2() = kVendorDefinedField2;
    result.vendorDefinedField3() = kVendorDefinedField3;
  }

  return result;
}

} // namespace

class FbossEepromInterfaceTest : public ::testing::Test {
 protected:
  FbossEepromInterface createFbossEepromInterface(const EepromData& data) {
    std::string fileName = tmpDir_.path().string() + "/eepromContent";
    folly::writeFile(data, fileName.c_str());
    return FbossEepromInterface(fileName, 0);
  }

 private:
  folly::test::TemporaryDirectory tmpDir_;
};

TEST_F(FbossEepromInterfaceTest, V5) {
  auto eeprom = createFbossEepromInterface(kEepromV5);
  EXPECT_EQ(eeprom.getVersion(), 5);
  EXPECT_EQ(eeprom.getEepromContents(), createEepromContents(5));

  EXPECT_EQ(eeprom.getProductName(), kProductName);
  EXPECT_EQ(eeprom.getProductPartNumber(), kProductPartNumber);
  EXPECT_EQ(eeprom.getProductionState(), kProductionState);
  EXPECT_EQ(eeprom.getProductionSubState(), kProductionSubState);
  EXPECT_EQ(eeprom.getVariantVersion(), kVariantIndicator);
  EXPECT_EQ(eeprom.getProductSerialNumber(), kProductSerialNumber);
}

TEST_F(FbossEepromInterfaceTest, V5WrongCRC) {
  auto eeprom = createFbossEepromInterface(kEepromV5WrongCrc);
  EXPECT_EQ(eeprom.getVersion(), 5);
  EXPECT_EQ(eeprom.getEepromContents(), createEepromContents(5, false));
}

TEST_F(FbossEepromInterfaceTest, V6) {
  auto eeprom = createFbossEepromInterface(kEepromV6);
  EXPECT_EQ(eeprom.getVersion(), 6);
  EXPECT_EQ(eeprom.getEepromContents(), createEepromContents(6));

  EXPECT_EQ(eeprom.getProductName(), kProductName);
  EXPECT_EQ(eeprom.getProductPartNumber(), kProductPartNumber);
  EXPECT_EQ(eeprom.getProductionState(), kProductionState);
  EXPECT_EQ(eeprom.getProductionSubState(), kProductionSubState);
  EXPECT_EQ(eeprom.getVariantVersion(), kVariantIndicator);
  EXPECT_EQ(eeprom.getProductSerialNumber(), kProductSerialNumber);
}

TEST_F(FbossEepromInterfaceTest, InvalidEepromSize) {
  const EepromData tooSmall = {0xfb, 0xfb};
  EXPECT_THROW(createFbossEepromInterface(tooSmall), std::runtime_error);
}

TEST_F(FbossEepromInterfaceTest, InvalidEepromVersion) {
  const EepromData invalidVersion = {0xfb, 0xfb, 0x04, 0xff};
  EXPECT_THROW(createFbossEepromInterface(invalidVersion), std::runtime_error);
}

TEST_F(FbossEepromInterfaceTest, GetContentsV5) {
  auto eeprom = createFbossEepromInterface(kEepromV5);

  const std::vector<std::pair<std::string, std::string>> expected = {
      {"Version", "5"},
      {"Product Name", kProductName},
      {"Product Part Number", kProductPartNumber},
      {"System Assembly Part Number", kSystemAssemblyPartNumber},
      {"Meta PCBA Part Number", kMetaPCBAPartNumber},
      {"Meta PCB Part Number", kMetaPCBPartNumber},
      {"ODM/JDM PCBA Part Number", kOdmJdmPCBAPartNumber},
      {"ODM/JDM PCBA Serial Number", kOdmJdmPCBASerialNumber},
      {"Product Production State", kProductionState},
      {"Product Version", kProductionSubState},
      {"Product Sub-Version", kVariantIndicator},
      {"Product Serial Number", kProductSerialNumber},
      {"System Manufacturer", kSystemManufacturer},
      {"System Manufacturing Date", kSystemManufacturingDate},
      {"PCB Manufacturer", kPCBManufacturer},
      {"Assembled At", kAssembledAt},
      {"EEPROM location on Fabric", kEepromLocationOnFabric},
      {"X86 CPU MAC Base", kX86CpuMacBase},
      {"X86 CPU MAC Address Size", kX86CpuMacSize},
      {"BMC MAC Base", kBmcMacBase},
      {"BMC MAC Address Size", kBmcMacSize},
      {"Switch ASIC MAC Base", kSwitchAsicMacBase},
      {"Switch ASIC MAC Address Size", kSwitchAsicMacSize},
      {"META Reserved MAC Base", kMetaReservedMacBase},
      {"META Reserved MAC Address Size", kMetaReservedMacSize},
      {"CRC16", kCrc16V5},
  };

  EXPECT_EQ(eeprom.getContents(), expected);
}

TEST_F(FbossEepromInterfaceTest, GetContentsV6) {
  auto eeprom = createFbossEepromInterface(kEepromV6);

  const std::vector<std::pair<std::string, std::string>> expected = {
      {"Version", "6"},
      {"Product Name", kProductName},
      {"Product Part Number", kProductPartNumber},
      {"System Assembly Part Number", kSystemAssemblyPartNumber},
      {"Meta PCBA Part Number", kMetaPCBAPartNumber},
      {"Meta PCB Part Number", kMetaPCBPartNumber},
      {"ODM/JDM PCBA Part Number", kOdmJdmPCBAPartNumber},
      {"ODM/JDM PCBA Serial Number", kOdmJdmPCBASerialNumber},
      {"Production State", kProductionState},
      {"Production Sub-State", kProductionSubState},
      {"Re-Spin/Variant Indicator", kVariantIndicator},
      {"Product Serial Number", kProductSerialNumber},
      {"System Manufacturer", kSystemManufacturer},
      {"System Manufacturing Date", kSystemManufacturingDate},
      {"PCB Manufacturer", kPCBManufacturer},
      {"Assembled At", kAssembledAt},
      {"EEPROM location on Fabric", kEepromLocationOnFabric},
      {"X86 CPU MAC Base", kX86CpuMacBase},
      {"X86 CPU MAC Address Size", kX86CpuMacSize},
      {"BMC MAC Base", kBmcMacBase},
      {"BMC MAC Address Size", kBmcMacSize},
      {"Switch ASIC MAC Base", kSwitchAsicMacBase},
      {"Switch ASIC MAC Address Size", kSwitchAsicMacSize},
      {"META Reserved MAC Base", kMetaReservedMacBase},
      {"META Reserved MAC Address Size", kMetaReservedMacSize},
      {"RMA", kRma},
      {"Vendor Defined Field 1", kVendorDefinedField1},
      {"Vendor Defined Field 2", kVendorDefinedField2},
      {"Vendor Defined Field 3", kVendorDefinedField3},
      {"CRC16", kCrc16V6},
  };

  EXPECT_EQ(eeprom.getContents(), expected);
}

TEST_F(FbossEepromInterfaceTest, UnknownFieldCode) {
  const EepromData invalidField = {
      0xfb, 0xfb, 0x05, 0xff, 0x99, 0x02, 0x00, 0x00};
  EXPECT_THROW(createFbossEepromInterface(invalidField), std::runtime_error);
}

TEST_F(FbossEepromInterfaceTest, TruncatedTlvLength) {
  // EEPROM blob with a TLV that declares a length exceeding available bytes
  // Header (4 bytes) + field code 0x01 (1 byte) + length 0xFF (255 bytes
  // claimed) but buffer ends after only a few data bytes
  const EepromData truncatedTlv = {
      0xfb, 0xfb, 0x05, 0xff, 0x01, 0xFF, 0x00, 0x00, 0x00, 0x00};
  EXPECT_THROW(createFbossEepromInterface(truncatedTlv), std::runtime_error);
}
} // namespace facebook::fboss::platform
