// Copyright (c) Meta Platforms, Inc. and affiliates.

include "thrift/annotation/thrift.thrift"

@thrift.AllowLegacyMissingUris
package;

namespace cpp2 facebook.fboss.platform.bsp_tests

// `BspTestsConfig` contains all extra information for a platform
//  to test BSP functionality and interfaces.
//
// `testData`: mapping of PlatformManager name to DeviceTestData
//  "PlatformManager" name is <PM_UNIT_NAME>.<PM_UNIT_SCOPED_NAME>
//
// `expectedErrors`: Mapping of PlaformManager name to a map of
//  ExpectedErrorType to reason. The reason must be non-empty.
struct BspTestsConfig {
  1: map<string, DeviceTestData> testData;
  2: map<string, map<ExpectedErrorType, string>> expectedErrors; //pmName : {errorType: reason}
}

// `UNKNOWN_ERROR`: should not be used
//
// `I2C_NOT_DETECTABLE`: i2cdetect cannot detect this device for some reason
enum ExpectedErrorType {
  UNKNOWN_ERROR = 0,
  I2C_NOT_DETECTABLE = 1,
}

struct DeviceTestData {
  1: optional I2CTestData i2cTestData;
  2: optional HwmonTestData hwmonTestData;
  3: optional GpioTestData gpioTestData;
  4: optional WatchdogTestData watchdogTestData;
  5: optional LedTestData ledTestData;
  6: optional MdioTestData mdioTestData;
}

struct I2CTestData {
  1: list<I2CDumpData> i2cDumpData;
  2: list<I2CGetData> i2cGetData;
  3: list<I2CSetData> i2cSetData;
}

struct I2CDumpData {
  1: string start;
  2: string end;
  3: list<string> expected;
}

struct I2CGetData {
  1: string reg;
  2: string expected;
}

struct I2CSetData {
  1: string reg;
  2: string value;
}

struct GpioTestData {
  1: i32 numLines;
  2: list<GpioLineInfo> lines;
}

struct GpioLineInfo {
  1: i32 index;
  2: string name;
  3: string direction;
  4: optional i32 getValue;
}

struct HwmonTestData {
  1: list<string> expectedFeatures;
  2: optional list<list<string>> expVersionedFeaturesList;
}

struct WatchdogTestData {
  1: i32 numWatchdogs;
}

struct LedTestData {
  1: bool createsLeds;
}

struct MdioTestData {
  1: list<MdioWriteData> mdioWriteData;
  2: list<MdioReadData> mdioReadData;
}

struct MdioWriteData {
  1: i32 phyAddress;
  2: i32 regAddress;
  3: i32 setValue;
}

struct MdioReadData {
  1: i32 phyAddress;
  2: i32 regAddress;
  3: i32 expected;
}
