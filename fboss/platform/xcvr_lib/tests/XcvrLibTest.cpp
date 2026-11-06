// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <fmt/format.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include "fboss/platform/config_lib/ConfigLib.h"
#include "fboss/platform/xcvr_lib/XcvrLib.h"

using namespace facebook::fboss;

namespace {

platform::platform_manager::LedCtrlBlockConfig
makeLedBlock(int startPort, int numPorts, int ledPerPort, int lanesPerPort) {
  platform::platform_manager::LedCtrlBlockConfig block;
  block.startPort() = startPort;
  block.numPorts() = numPorts;
  block.ledPerPort() = ledPerPort;
  block.lanesPerPort() = lanesPerPort;
  return block;
}

platform::platform_manager::PlatformConfig makeTestConfig(
    const std::string& platformName,
    int numXcvrs,
    const std::vector<platform::platform_manager::LedCtrlBlockConfig>&
        ledBlocks = {},
    const std::map<std::string, std::string>& symbolicLinks = {},
    const std::string& bspKmodsRpmName = "fboss_bsp_kmods") {
  platform::platform_manager::PciDeviceConfig pciDevice;
  pciDevice.ledCtrlBlockConfigs() = ledBlocks;

  platform::platform_manager::PmUnitConfig pmUnit;
  pmUnit.pciDeviceConfigs() = {pciDevice};

  platform::platform_manager::PlatformConfig pmConfig;
  pmConfig.platformName() = platformName;
  pmConfig.numXcvrs() = numXcvrs;
  pmConfig.symbolicLinkToDevicePath() = symbolicLinks;
  pmConfig.pmUnitConfigs() = {{"test_unit", pmUnit}};
  pmConfig.bspKmodsRpmName() = bspKmodsRpmName;
  return pmConfig;
}

// Standard 4-transceiver test config:
//   Ports 1-3: 2 LEDs, 8 lanes each
//   Port 4: 1 LED, 4 lanes
platform::platform_manager::PlatformConfig makeStandardTestConfig() {
  return makeTestConfig(
      "TEST_PLATFORM",
      4,
      {makeLedBlock(1, 3, 2, 8), makeLedBlock(4, 1, 1, 4)},
      {{"/run/devmap/xcvrs/xcvr_io_1", "/dev/1"},
       {"/run/devmap/xcvrs/xcvr_ctrl_1", "/dev/ctrl1"},
       {"/run/devmap/xcvrs/xcvr_io_2", "/dev/2"},
       {"/run/devmap/xcvrs/xcvr_ctrl_2", "/dev/ctrl2"},
       {"/run/devmap/xcvrs/xcvr_io_3", "/dev/3"},
       {"/run/devmap/xcvrs/xcvr_ctrl_3", "/dev/ctrl3"},
       {"/run/devmap/xcvrs/xcvr_io_4", "/dev/4"},
       {"/run/devmap/xcvrs/xcvr_ctrl_4", "/dev/ctrl4"}});
}

// Fake host-state reader so getResetHoldHi() is hermetic (no sysfs reads):
// maps a device path to the version reported by its bound driver.
class FakeSystemInterface
    : public platform::platform_manager::package_manager::SystemInterface {
 public:
  std::map<std::string, std::string> boundDriverVersions;
  mutable int getBoundDriverVersionCallCount{0};

  std::optional<std::string> getBoundDriverVersion(
      const std::string& devicePath) const override {
    ++getBoundDriverVersionCallCount;
    auto it = boundDriverVersions.find(devicePath);
    if (it == boundDriverVersions.end()) {
      return std::nullopt;
    }
    return it->second;
  }
};

platform::platform_manager::PlatformConfig makeAristaTestConfig(
    int numXcvrs = 1) {
  return makeTestConfig(
      "TEST_PLATFORM",
      numXcvrs,
      {makeLedBlock(1, numXcvrs, 2, 8)},
      {},
      "arista_bsp_kmods");
}

} // namespace

// --- Smoke test: ConfigLib lookup with the stable sample config ---

TEST(XcvrLibTest, ConfigLibLookupSample) {
  XcvrLib xcvr("sample");
  EXPECT_GT(xcvr.getNumTransceivers(), 0);
  EXPECT_GT(xcvr.getNumLedsForTransceiver(1), 0);
}

TEST(XcvrLibTest, ConstructFromParsedSampleConfig) {
  std::string json = platform::ConfigLib().getPlatformManagerConfig("sample");
  platform::platform_manager::PlatformConfig pmConfig;
  apache::thrift::SimpleJSONSerializer::deserialize(json, pmConfig);
  XcvrLib xcvr(pmConfig);
  EXPECT_GT(xcvr.getNumTransceivers(), 0);
  EXPECT_GT(xcvr.getNumLedsForTransceiver(1), 0);
}

// --- Detailed tests using constructed configs ---

TEST(XcvrLibTest, NumTransceivers) {
  XcvrLib xcvr(makeStandardTestConfig());
  EXPECT_EQ(xcvr.getNumTransceivers(), 4);
}

TEST(XcvrLibTest, GetXcvrIODevicePath) {
  XcvrLib xcvr(makeStandardTestConfig());
  EXPECT_EQ(xcvr.getXcvrIODevicePath(1), "/run/devmap/xcvrs/xcvr_io_1");
  EXPECT_EQ(xcvr.getXcvrIODevicePath(3), "/run/devmap/xcvrs/xcvr_io_3");
}

TEST(XcvrLibTest, GetXcvrResetSysfsPath) {
  XcvrLib xcvr(makeStandardTestConfig());
  EXPECT_EQ(
      xcvr.getXcvrResetSysfsPath(1),
      "/run/devmap/xcvrs/xcvr_ctrl_1/xcvr_reset_1");
  EXPECT_EQ(
      xcvr.getXcvrResetSysfsPath(3),
      "/run/devmap/xcvrs/xcvr_ctrl_3/xcvr_reset_3");
}

TEST(XcvrLibTest, GetXcvrPresenceSysfsPath) {
  XcvrLib xcvr(makeStandardTestConfig());
  EXPECT_EQ(
      xcvr.getXcvrPresenceSysfsPath(1),
      "/run/devmap/xcvrs/xcvr_ctrl_1/xcvr_present_1");
}

TEST(XcvrLibTest, GetPrimaryLedColorDefault) {
  XcvrLib xcvr(makeTestConfig("TEST_PLATFORM", 1, {makeLedBlock(1, 1, 2, 8)}));
  EXPECT_EQ(xcvr.getPrimaryLedColor(), XcvrLib::LedColor::BLUE);
}

TEST(XcvrLibTest, GetPrimaryLedColorDarwin) {
  XcvrLib xcvr(makeTestConfig("DARWIN", 1, {makeLedBlock(1, 1, 2, 8)}));
  EXPECT_EQ(xcvr.getPrimaryLedColor(), XcvrLib::LedColor::GREEN);
}

TEST(XcvrLibTest, GetPrimaryLedColorDarwin48v) {
  XcvrLib xcvr(makeTestConfig("DARWIN48V", 1, {makeLedBlock(1, 1, 2, 8)}));
  EXPECT_EQ(xcvr.getPrimaryLedColor(), XcvrLib::LedColor::GREEN);
}

TEST(XcvrLibTest, GetLedSysfsPath) {
  XcvrLib xcvr(makeStandardTestConfig());
  EXPECT_EQ(
      xcvr.getLedSysfsPath(1, 1, XcvrLib::LedColor::BLUE),
      "/sys/class/leds/port1_led1:blue:status");
  EXPECT_EQ(
      xcvr.getLedSysfsPath(3, 2, XcvrLib::LedColor::BLUE),
      "/sys/class/leds/port3_led2:blue:status");
  EXPECT_EQ(
      xcvr.getLedSysfsPath(1, 1, XcvrLib::LedColor::AMBER),
      "/sys/class/leds/port1_led1:amber:status");
  EXPECT_EQ(
      xcvr.getLedSysfsPath(1, 1, XcvrLib::LedColor::GREEN),
      "/sys/class/leds/port1_led1:green:status");
}

TEST(XcvrLibTest, GetNumLedsForTransceiver) {
  XcvrLib xcvr(makeStandardTestConfig());
  // Ports 1-3 have 2 LEDs
  EXPECT_EQ(xcvr.getNumLedsForTransceiver(1), 2);
  EXPECT_EQ(xcvr.getNumLedsForTransceiver(3), 2);
  // Port 4 has 1 LED
  EXPECT_EQ(xcvr.getNumLedsForTransceiver(4), 1);
}

TEST(XcvrLibTest, ConstructorThrowsForUncoveredXcvrs) {
  // LED blocks exist but don't cover all ports — constructor throws
  auto config = makeTestConfig("TEST_PLATFORM", 3, {makeLedBlock(1, 1, 1, 4)});
  EXPECT_THROW(XcvrLib{config}, std::runtime_error);
}

TEST(XcvrLibTest, ConstructorThrowsForEmptyLedBlocks) {
  // No LED blocks at all with xcvrs present — constructor throws
  auto config = makeTestConfig("TEST_PLATFORM", 3, {});
  EXPECT_THROW(XcvrLib{config}, std::runtime_error);
}

TEST(XcvrLibTest, GetNumLanesForTransceiver) {
  XcvrLib xcvr(makeStandardTestConfig());
  // Ports 1-3 have 8 lanes
  EXPECT_EQ(xcvr.getNumLanesForTransceiver(1), 8);
  EXPECT_EQ(xcvr.getNumLanesForTransceiver(3), 8);
  // Port 4 has 4 lanes
  EXPECT_EQ(xcvr.getNumLanesForTransceiver(4), 4);
}

TEST(XcvrLibTest, GetResetMask) {
  XcvrLib xcvr(makeStandardTestConfig());
  EXPECT_EQ(xcvr.getResetMask(), 1);
}

TEST(XcvrLibTest, GetPresenceMask) {
  XcvrLib xcvr(makeStandardTestConfig());
  EXPECT_EQ(xcvr.getPresenceMask(), 1);
}

TEST(XcvrLibTest, GetResetHoldHiFbossBsp) {
  XcvrLib xcvr(makeTestConfig(
      "TEST_PLATFORM", 1, {makeLedBlock(1, 1, 2, 8)}, {}, "fboss_bsp_kmods"));
  EXPECT_EQ(xcvr.getResetHoldHi(), 1);
}

// The version strings below are mock fixtures, not real BSP releases; they only
// need to sort below/above the source threshold to exercise both polarity
// paths.
constexpr auto kMockVersionBelowThreshold = "0.0.1";
constexpr auto kMockVersionAboveThreshold = "9.9.9";

std::string xcvrCtrlPath(int xcvrId) {
  return fmt::format("/run/devmap/xcvrs/xcvr_ctrl_{}", xcvrId);
}

TEST(XcvrLibTest, GetResetHoldHiAristaBelowThreshold) {
  // Loaded BSP sorts below the threshold -> active-low.
  auto fake = std::make_shared<FakeSystemInterface>();
  fake->boundDriverVersions = {{xcvrCtrlPath(1), kMockVersionBelowThreshold}};
  XcvrLib xcvr(makeAristaTestConfig(), fake);
  EXPECT_EQ(xcvr.getResetHoldHi(), 0);
}

TEST(XcvrLibTest, GetResetHoldHiAristaAtOrAboveThreshold) {
  // Loaded BSP sorts at/above the threshold -> active-high.
  auto fake = std::make_shared<FakeSystemInterface>();
  fake->boundDriverVersions = {{xcvrCtrlPath(1), kMockVersionAboveThreshold}};
  XcvrLib xcvr(makeAristaTestConfig(), fake);
  EXPECT_EQ(xcvr.getResetHoldHi(), 1);
}

TEST(XcvrLibTest, GetResetHoldHiAristaSkipsXcvrWithoutDriverVersion) {
  // xcvr 1 has no readable driver version; xcvr 2's driver decides.
  auto fake = std::make_shared<FakeSystemInterface>();
  fake->boundDriverVersions = {{xcvrCtrlPath(2), kMockVersionAboveThreshold}};
  XcvrLib xcvr(makeAristaTestConfig(2), fake);
  EXPECT_EQ(xcvr.getResetHoldHi(), 1);
}

TEST(XcvrLibTest, GetResetHoldHiAristaNoDriverVersionFailsSafe) {
  // No xcvr_ctrl reports a driver version -> fail safe to active-low.
  auto fake = std::make_shared<FakeSystemInterface>();
  XcvrLib xcvr(makeAristaTestConfig(), fake);
  EXPECT_EQ(xcvr.getResetHoldHi(), 0);
}

TEST(XcvrLibTest, GetResetHoldHiAristaUnparseableVersionFailsSafe) {
  auto fake = std::make_shared<FakeSystemInterface>();
  fake->boundDriverVersions = {{xcvrCtrlPath(1), "notaversion"}};
  XcvrLib xcvr(makeAristaTestConfig(), fake);
  EXPECT_EQ(xcvr.getResetHoldHi(), 0);
}

TEST(XcvrLibTest, GetResetHoldHiAristaSkipsUnparseableVersion) {
  // xcvr 1's driver version is unparseable; xcvr 2's driver decides.
  auto fake = std::make_shared<FakeSystemInterface>();
  fake->boundDriverVersions = {
      {xcvrCtrlPath(1), "notaversion"},
      {xcvrCtrlPath(2), kMockVersionAboveThreshold}};
  XcvrLib xcvr(makeAristaTestConfig(2), fake);
  EXPECT_EQ(xcvr.getResetHoldHi(), 1);
}

TEST(XcvrLibTest, GetResetHoldHiCachesAcrossCalls) {
  // The mapping build calls getResetHoldHi() once per transceiver against one
  // XcvrLib instance; the host-state read must happen only once, not per call.
  auto fake = std::make_shared<FakeSystemInterface>();
  fake->boundDriverVersions = {{xcvrCtrlPath(1), kMockVersionAboveThreshold}};
  XcvrLib xcvr(makeAristaTestConfig(), fake);
  xcvr.getResetHoldHi();
  xcvr.getResetHoldHi();
  xcvr.getResetHoldHi();
  EXPECT_EQ(fake->getBoundDriverVersionCallCount, 1);
}

TEST(XcvrLibTest, GetResetHoldHiCiscoBsp) {
  XcvrLib xcvr(makeTestConfig(
      "TEST_PLATFORM", 1, {makeLedBlock(1, 1, 2, 8)}, {}, "cisco_bsp_kmods"));
  EXPECT_EQ(xcvr.getResetHoldHi(), 0);
}

// Nexthop's xcvr reset line is active-low.
TEST(XcvrLibTest, GetResetHoldHiNexthopBsp) {
  XcvrLib xcvr(makeTestConfig(
      "TEST_PLATFORM", 1, {makeLedBlock(1, 1, 2, 8)}, {}, "nexthop_bsp_kmods"));
  EXPECT_EQ(xcvr.getResetHoldHi(), 0);
}

// --- Tests for std::nullopt return paths (invalid xcvrId) ---

TEST(XcvrLibTest, GetNumLedsForTransceiverInvalidId) {
  XcvrLib xcvr(makeStandardTestConfig());
  EXPECT_EQ(xcvr.getNumLedsForTransceiver(0), std::nullopt);
  EXPECT_EQ(xcvr.getNumLedsForTransceiver(-1), std::nullopt);
  EXPECT_EQ(xcvr.getNumLedsForTransceiver(5), std::nullopt);
}

TEST(XcvrLibTest, GetNumLanesForTransceiverInvalidId) {
  XcvrLib xcvr(makeStandardTestConfig());
  EXPECT_EQ(xcvr.getNumLanesForTransceiver(0), std::nullopt);
  EXPECT_EQ(xcvr.getNumLanesForTransceiver(-1), std::nullopt);
  EXPECT_EQ(xcvr.getNumLanesForTransceiver(5), std::nullopt);
}

TEST(XcvrLibTest, GetXcvrIODevicePathInvalidId) {
  XcvrLib xcvr(makeStandardTestConfig());
  EXPECT_EQ(xcvr.getXcvrIODevicePath(0), std::nullopt);
  EXPECT_EQ(xcvr.getXcvrIODevicePath(5), std::nullopt);
}

TEST(XcvrLibTest, GetXcvrResetSysfsPathInvalidId) {
  XcvrLib xcvr(makeStandardTestConfig());
  EXPECT_EQ(xcvr.getXcvrResetSysfsPath(0), std::nullopt);
  EXPECT_EQ(xcvr.getXcvrResetSysfsPath(5), std::nullopt);
}

TEST(XcvrLibTest, GetXcvrPresenceSysfsPathInvalidId) {
  XcvrLib xcvr(makeStandardTestConfig());
  EXPECT_EQ(xcvr.getXcvrPresenceSysfsPath(0), std::nullopt);
  EXPECT_EQ(xcvr.getXcvrPresenceSysfsPath(5), std::nullopt);
}

TEST(XcvrLibTest, GetLedSysfsPathInvalidXcvrId) {
  XcvrLib xcvr(makeStandardTestConfig());
  EXPECT_EQ(xcvr.getLedSysfsPath(0, 1, XcvrLib::LedColor::BLUE), std::nullopt);
  EXPECT_EQ(xcvr.getLedSysfsPath(5, 1, XcvrLib::LedColor::BLUE), std::nullopt);
}

TEST(XcvrLibTest, GetLedSysfsPathInvalidLedNum) {
  XcvrLib xcvr(makeStandardTestConfig());
  // Port 1 has 2 LEDs, so ledNum 0 and 3 are invalid
  EXPECT_EQ(xcvr.getLedSysfsPath(1, 0, XcvrLib::LedColor::BLUE), std::nullopt);
  EXPECT_EQ(xcvr.getLedSysfsPath(1, 3, XcvrLib::LedColor::BLUE), std::nullopt);
}
