#
# Copyright 2004-present Facebook. All Rights Reserved.
#

include "thrift/annotation/thrift.thrift"

@thrift.AllowLegacyMissingUris
package;

namespace py neteng.fboss.qsfp_production_features
namespace py3 neteng.fboss
namespace py.asyncio neteng.fboss.asyncio.qsfp_production_features
namespace cpp2 facebook.fboss.qsfp_production_features
namespace go neteng.fboss.qsfp_production_features

enum QsfpProductionFeature {
  MACSEC = 0,
  EXTERNAL_PHY = 1,
  PIM = 2,
  XPHY_SYSTEM_NRZ_PROFILE = 3,
  XPHY_SYSTEM_PAM4_PROFILE = 4,
  XPHY_LINE_NRZ_PROFILE = 5,
  XPHY_LINE_PAM4_PROFILE = 6,
  TRANSCEIVER_PRBS = 7,
  BMC_LITE = 8,
  // Functional features describing what a test verifies. Unlike the values
  // above (platform-capability gates consumed by --list_production_feature for
  // test selection), these are surfaced only via
  // HwTest::addVerifiedProductionFeatures() for per-test metadata/Scuba
  // reporting and are NOT used for test gating.
  DATA_PATH_PROGRAMMING = 9,
  FIRMWARE_UPGRADE = 10,
  CONFIG_VALIDATION = 11,
  TRANSCEIVER_RESET = 12,
  TRANSCEIVER_DETECTION = 13,
  TRANSCEIVER_REMOVAL = 14,
  REMEDIATION = 15,
  STATS_COLLECTION = 16,
  I2C_ACCESS = 17,
  XPHY_PROGRAMMING = 18,
  THERMAL_VALIDATION = 19,
  TRANSCEIVER_ADVERTISEMENTS = 20,
  XPHY_PRBS_CONTROL = 21,
  SAI_EXTERNAL_PHY = 22,
}

struct PlatformConfigToQsfpProductionFeatures {
  1: map<string, list<QsfpProductionFeature>> platformConfigToFeatures;
  2: map<string, list<string>> platformConfigToFeatureNames;
}
