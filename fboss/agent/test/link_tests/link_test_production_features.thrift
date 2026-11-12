#
# Copyright 2004-present Facebook. All Rights Reserved.
#

include "thrift/annotation/thrift.thrift"

@thrift.AllowLegacyMissingUris
package;

namespace py neteng.fboss.link_test_production_features
namespace py3 neteng.fboss
namespace py.asyncio neteng.fboss.asyncio.link_test_production_features
namespace cpp2 facebook.fboss.link_test_production_features
namespace go neteng.fboss.link_test_production_features

enum LinkTestProductionFeature {
  L1_LINK_TEST = 0,
  L2_LINK_TEST = 1,
  // Functional features describing what a test verifies. Unlike L1/L2 above
  // (which gate test selection via --list_production_feature), these are
  // surfaced only via AgentEnsembleLinkTest::addVerifiedProductionFeatures()
  // for per-test metadata/Scuba reporting and are NOT used for test gating.
  // Exception: MAC_LEARNING, PTP, MACSEC, REMEDIATION and VDM are also platform
  // features (see below) when declared in getProductionFeatures().
  // New reporting values go here, below 100.
  LINK_BRINGUP = 2,
  TRAFFIC_FORWARDING = 3,
  IPHY_DIAGS = 4,
  XPHY_DIAGS = 5,
  IPHY_FEC_INJECT = 6,
  PRBS = 7,
  LACP = 8,
  MAC_LEARNING = 9,
  PTP = 10,
  WARMBOOT = 11,
  SPEED_CHANGE = 12,
  BMC_UPGRADE = 13,
  FABRIC_LINK = 14,
  FSDB_PUBLISH = 15,
  MACSEC = 16,
  TRANSCEIVER_TX_DISABLE = 17,
  REMEDIATION = 18,
  TRANSCEIVER_TX_RX_LATCHES = 19,
  VDM = 20,
  IPHY_FEC_COUNTERS = 21,
  TRANSCEIVER_LOOPBACK = 22,
  TRANSCEIVER_RX_OUTPUT_DISABLE = 23,
  TRANSCEIVER_MODE_MISMATCH_THERMAL_MARGIN = 24,
  // Platform features used for test selection, reserved range 100 and above.
  // A test that declares any of these (in getProductionFeatures()) should run
  // only on platforms that have all of them; the per-platform map lives in
  // configerator (fboss/agent/test/link_test_production_features). Names must
  // match that config; its numbers are not compared and differ from these.
  // https://docs.google.com/document/d/1rTSDm1sCG2RVuHR8TlighYIU47y9LzXLOfMwvCZlLYk/edit
  ACTIVE_TRANSCEIVER = 100,
  XPHY = 101,
  BMC_LITE = 102,
  BMC_CLASSIC = 103,
  ASIC_LAG = 104,
  DSF_INTERFACE_NODE = 105,
}
